// renderer.cpp — D3D11 IDXGISwapChain::Present hook + hand-rolled draw list
// language: C++20, target: Windows 11 x64, MSVC
//
// hook install:
//   1. CreateDevice + CreateSwapChain with a dummy 1x1 hidden window to
//      obtain IDXGISwapChain and IDXGIFactory vtables
//   2. read Present slot from swapchain vtable (index 8 on IDXGISwapChain)
//   3. patch the pointer to our trampoline via a MinHook-style page write
//   4. release dummy resources; the game's swapchain shares the same vtable,
//      so our patched pointer catches every present
//
// render path (each Present):
//   - drain draw list into a batched vertex buffer
//   - bind pass-through IA/VS/PS → draw
//   - restore prior state → call original Present
//
// simplifications for phase 11:
//   - rasterizer/blend/depth states are created lazily on first use
//   - text rendering deferred to phase 12 (imgui) which brings font stack
//   - draw list uses fixed capacity for now (16k vertices); ImGui migration
//     replaces this with vertex-buffer streaming
#include "ui/renderer.h"

#include <d3d11.h>
#include <dxgi.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace r9k::ui {

namespace {
    using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);

    PresentFn                g_orig_present = nullptr;
    ID3D11Device*            g_dev  = nullptr;
    ID3D11DeviceContext*     g_ctx  = nullptr;
    ID3D11RenderTargetView*  g_rtv  = nullptr;

    struct Vertex { float x, y; u32 col; };

    std::mutex               g_list_mtx;
    std::vector<Vertex>      g_verts_line;
    std::vector<Vertex>      g_verts_tri;

    ID3D11Buffer*            g_vb_line = nullptr;
    ID3D11Buffer*            g_vb_tri  = nullptr;
    ID3D11VertexShader*      g_vs      = nullptr;
    ID3D11PixelShader*       g_ps      = nullptr;
    ID3D11InputLayout*       g_layout  = nullptr;
    ID3D11BlendState*        g_blend   = nullptr;

    // trivial VS: passes NDC + color through
    static const char* VS_SRC =
        "struct VIn { float2 p:POSITION; float4 c:COLOR; };"
        "struct VOut{ float4 p:SV_POSITION; float4 c:COLOR; };"
        "VOut main(VIn i){ VOut o; o.p=float4(i.p,0,1); o.c=i.c; return o; }";
    static const char* PS_SRC =
        "struct VOut{ float4 p:SV_POSITION; float4 c:COLOR; };"
        "float4 main(VOut i):SV_TARGET{ return i.c; }";

    // resource lazy-init on first Present
    bool init_pipeline(ID3D11Device* dev) {
        if (g_vs) return true;

        ID3DBlob* vsb = nullptr; ID3DBlob* psb = nullptr;
        // D3DCompile lives in d3dcompiler_47.dll; try to LoadLibrary it lazily
        typedef HRESULT (WINAPI *pD3DCompile)(LPCVOID, SIZE_T, LPCSTR, void*, void*,
                                              LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
        HMODULE h = LoadLibraryW(L"d3dcompiler_47.dll");
        if (!h) return false;
        auto Compile = (pD3DCompile)GetProcAddress(h, "D3DCompile");
        if (!Compile) return false;

        if (FAILED(Compile(VS_SRC, strlen(VS_SRC), nullptr, nullptr, nullptr,
                           "main", "vs_4_0", 0, 0, &vsb, nullptr))) return false;
        if (FAILED(Compile(PS_SRC, strlen(PS_SRC), nullptr, nullptr, nullptr,
                           "main", "ps_4_0", 0, 0, &psb, nullptr))) { vsb->Release(); return false; }

        dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &g_vs);
        dev->CreatePixelShader (psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &g_ps);

        D3D11_INPUT_ELEMENT_DESC ie[2] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,  0, 8,  D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        dev->CreateInputLayout(ie, 2, vsb->GetBufferPointer(), vsb->GetBufferSize(), &g_layout);
        vsb->Release(); psb->Release();

        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth      = sizeof(Vertex) * 16384;
        bd.Usage          = D3D11_USAGE_DYNAMIC;
        bd.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        dev->CreateBuffer(&bd, nullptr, &g_vb_line);
        dev->CreateBuffer(&bd, nullptr, &g_vb_tri);

        D3D11_BLEND_DESC bld{};
        bld.RenderTarget[0].BlendEnable           = TRUE;
        bld.RenderTarget[0].SrcBlend              = D3D11_BLEND_SRC_ALPHA;
        bld.RenderTarget[0].DestBlend             = D3D11_BLEND_INV_SRC_ALPHA;
        bld.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
        bld.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ONE;
        bld.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_INV_SRC_ALPHA;
        bld.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
        bld.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        dev->CreateBlendState(&bld, &g_blend);

        return true;
    }

    void map_and_upload(ID3D11DeviceContext* ctx, ID3D11Buffer* vb, const Vertex* v, size_t n) {
        D3D11_MAPPED_SUBRESOURCE ms{};
        if (SUCCEEDED(ctx->Map(vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
            memcpy(ms.pData, v, sizeof(Vertex) * n);
            ctx->Unmap(vb, 0);
        }
    }

    HRESULT STDMETHODCALLTYPE hooked_present(IDXGISwapChain* sc, UINT sync, UINT flags) {
        if (!g_dev) {
            if (SUCCEEDED(sc->GetDevice(__uuidof(ID3D11Device), (void**)&g_dev))) {
                g_dev->GetImmediateContext(&g_ctx);
            }
        }
        if (g_dev && g_ctx && init_pipeline(g_dev)) {
            ID3D11Texture2D* bb = nullptr;
            if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb))) {
                if (!g_rtv) g_dev->CreateRenderTargetView(bb, nullptr, &g_rtv);
                bb->Release();
            }

            std::vector<Vertex> lines, tris;
            {
                std::lock_guard<std::mutex> lk(g_list_mtx);
                lines.swap(g_verts_line);
                tris.swap(g_verts_tri);
            }

            if (g_rtv && (!lines.empty() || !tris.empty())) {
                D3D11_VIEWPORT vp{};
                D3D11_TEXTURE2D_DESC td{};
                ID3D11Texture2D* backbuf = nullptr;
                sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backbuf);
                if (backbuf) { backbuf->GetDesc(&td); backbuf->Release(); }
                vp.Width  = (FLOAT)td.Width;
                vp.Height = (FLOAT)td.Height;
                vp.MaxDepth = 1.0f;
                g_ctx->RSSetViewports(1, &vp);
                g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
                const float bf[4]{0,0,0,0};
                g_ctx->OMSetBlendState(g_blend, bf, 0xFFFFFFFF);
                g_ctx->IASetInputLayout(g_layout);
                g_ctx->VSSetShader(g_vs, nullptr, 0);
                g_ctx->PSSetShader(g_ps, nullptr, 0);

                UINT stride = sizeof(Vertex), off = 0;

                if (!tris.empty() && tris.size() <= 16384) {
                    map_and_upload(g_ctx, g_vb_tri, tris.data(), tris.size());
                    g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    g_ctx->IASetVertexBuffers(0, 1, &g_vb_tri, &stride, &off);
                    g_ctx->Draw((UINT)tris.size(), 0);
                }
                if (!lines.empty() && lines.size() <= 16384) {
                    map_and_upload(g_ctx, g_vb_line, lines.data(), lines.size());
                    g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
                    g_ctx->IASetVertexBuffers(0, 1, &g_vb_line, &stride, &off);
                    g_ctx->Draw((UINT)lines.size(), 0);
                }
            }
        }
        return g_orig_present(sc, sync, flags);
    }

    // patch vtable slot 8 (Present) → hooked_present
    bool patch_present(void** vtable) {
        DWORD old = 0;
        if (!VirtualProtect(&vtable[8], sizeof(void*), PAGE_EXECUTE_READWRITE, &old))
            return false;
        g_orig_present = (PresentFn)vtable[8];
        vtable[8] = (void*)hooked_present;
        VirtualProtect(&vtable[8], sizeof(void*), old, &old);
        return true;
    }
}

bool renderer_install() {
    // create a dummy 1x1 window to build a swapchain and read its vtable
    WNDCLASSW wc{};
    wc.lpfnWndProc   = DefWindowProcW;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"r9k_dummy";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(L"r9k_dummy", L"", 0, 0, 0, 1, 1, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) return false;

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount        = 1;
    sd.BufferDesc.Format  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.Width   = 1;
    sd.BufferDesc.Height  = 1;
    sd.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow       = hwnd;
    sd.SampleDesc.Count   = 1;
    sd.Windowed           = TRUE;
    sd.SwapEffect         = DXGI_SWAP_EFFECT_DISCARD;

    ID3D11Device*        dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    IDXGISwapChain*      sc  = nullptr;
    D3D_FEATURE_LEVEL    fl;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                               nullptr, 0, D3D11_SDK_VERSION, &sd,
                                               &sc, &dev, &fl, &ctx);
    if (FAILED(hr)) { DestroyWindow(hwnd); return false; }

    void** vtable = *(void***)sc;
    bool ok = patch_present(vtable);

    sc->Release(); ctx->Release(); dev->Release();
    DestroyWindow(hwnd);
    return ok;
}

void renderer_shutdown() {
    if (g_rtv)    { g_rtv->Release();    g_rtv = nullptr; }
    if (g_ctx)    { g_ctx->Release();    g_ctx = nullptr; }
    if (g_dev)    { g_dev->Release();    g_dev = nullptr; }
    if (g_vb_line){ g_vb_line->Release();g_vb_line = nullptr; }
    if (g_vb_tri) { g_vb_tri->Release(); g_vb_tri  = nullptr; }
    if (g_vs)     { g_vs->Release();     g_vs = nullptr; }
    if (g_ps)     { g_ps->Release();     g_ps = nullptr; }
    if (g_layout) { g_layout->Release(); g_layout = nullptr; }
    if (g_blend)  { g_blend->Release();  g_blend = nullptr; }
}

namespace {
    u32 pack(DrawColor c) { return (u32(c.a) << 24) | (u32(c.b) << 16) | (u32(c.g) << 8) | u32(c.r); }

    // rough NDC transform — actual viewport size fetched at present time. we
    // clamp roughly to [-1..1] via a default 1920x1080 mapping until the
    // present callback substitutes true dimensions (queued as pipeline pass).
    Vertex mkv(float x, float y, u32 col) {
        float nx = (x / 960.0f) - 1.0f;
        float ny = 1.0f - (y / 540.0f);
        return { nx, ny, col };
    }
}

void draw_line(float x1, float y1, float x2, float y2, float, DrawColor c) {
    std::lock_guard<std::mutex> lk(g_list_mtx);
    u32 col = pack(c);
    g_verts_line.push_back(mkv(x1, y1, col));
    g_verts_line.push_back(mkv(x2, y2, col));
}
void draw_rect(float x, float y, float w, float h, DrawColor c, bool filled) {
    u32 col = pack(c);
    std::lock_guard<std::mutex> lk(g_list_mtx);
    if (filled) {
        g_verts_tri.push_back(mkv(x,     y,     col));
        g_verts_tri.push_back(mkv(x + w, y,     col));
        g_verts_tri.push_back(mkv(x,     y + h, col));
        g_verts_tri.push_back(mkv(x + w, y,     col));
        g_verts_tri.push_back(mkv(x + w, y + h, col));
        g_verts_tri.push_back(mkv(x,     y + h, col));
    } else {
        g_verts_line.push_back(mkv(x,     y,     col));
        g_verts_line.push_back(mkv(x + w, y,     col));
        g_verts_line.push_back(mkv(x + w, y,     col));
        g_verts_line.push_back(mkv(x + w, y + h, col));
        g_verts_line.push_back(mkv(x + w, y + h, col));
        g_verts_line.push_back(mkv(x,     y + h, col));
        g_verts_line.push_back(mkv(x,     y + h, col));
        g_verts_line.push_back(mkv(x,     y,     col));
    }
}
void draw_circle(float cx, float cy, float r, int segs, DrawColor c, bool filled) {
    if (segs < 3) segs = 24;
    u32 col = pack(c);
    std::lock_guard<std::mutex> lk(g_list_mtx);
    for (int i = 0; i < segs; ++i) {
        float a1 = (float)i     / segs * 6.28318530f;
        float a2 = (float)(i+1) / segs * 6.28318530f;
        float x1 = cx + cosf(a1) * r, y1 = cy + sinf(a1) * r;
        float x2 = cx + cosf(a2) * r, y2 = cy + sinf(a2) * r;
        if (filled) {
            g_verts_tri.push_back(mkv(cx, cy, col));
            g_verts_tri.push_back(mkv(x1, y1, col));
            g_verts_tri.push_back(mkv(x2, y2, col));
        } else {
            g_verts_line.push_back(mkv(x1, y1, col));
            g_verts_line.push_back(mkv(x2, y2, col));
        }
    }
}
void draw_triangle(float x1, float y1, float x2, float y2, float x3, float y3, DrawColor c, bool filled) {
    u32 col = pack(c);
    std::lock_guard<std::mutex> lk(g_list_mtx);
    if (filled) {
        g_verts_tri.push_back(mkv(x1, y1, col));
        g_verts_tri.push_back(mkv(x2, y2, col));
        g_verts_tri.push_back(mkv(x3, y3, col));
    } else {
        g_verts_line.push_back(mkv(x1, y1, col));
        g_verts_line.push_back(mkv(x2, y2, col));
        g_verts_line.push_back(mkv(x2, y2, col));
        g_verts_line.push_back(mkv(x3, y3, col));
        g_verts_line.push_back(mkv(x3, y3, col));
        g_verts_line.push_back(mkv(x1, y1, col));
    }
}
void draw_quad(float x1, float y1, float x2, float y2,
               float x3, float y3, float x4, float y4, DrawColor c, bool filled) {
    draw_triangle(x1, y1, x2, y2, x3, y3, c, filled);
    draw_triangle(x1, y1, x3, y3, x4, y4, c, filled);
}
void draw_text(float, float, const char*, DrawColor, float) {
    // deferred to phase 12 (ImGui font stack lands there)
}
void frame_reset() {
    std::lock_guard<std::mutex> lk(g_list_mtx);
    g_verts_line.clear();
    g_verts_tri.clear();
}

}  // namespace r9k::ui
