// main.cpp - pengooin watcher + executor GUI
// language: C++20, target: Windows 11 x64, MSVC
//
// single-window app:
//   - top pane:    script editor (multi-line EDIT, Consolas)
//   - bottom pane: console (read-only EDIT) that receives payload results
//   - toolbar:     Run / Clear / Save Editor / status text
//   - tray icon:   minimizing hides to tray; right-click -> Show / Exit
//   - global hotkey F5 = Run (any focus in the app)
//
// background workers:
//   - watcher thread: polls the process list every 500 ms for
//     RobloxPlayerBeta.exe, manual-maps the embedded payload into each new
//     instance. dedup by pid; single attempt per pid.
//   - pipe server thread: creates instances of the well-known named pipe
//     \\.\pipe\pengooin_v1 and spawns a client handler per connection.
//     Run button writes the current editor buffer to every connected pipe.
//
// wire format on the pipe (both directions):
//   uint32_t length (little-endian) + <length> UTF-8 bytes
//
// embedded payload:
//   r9k_payload.dll is stored as RCDATA resource 1001. no companion DLL is
//   shipped; users get one file: pengooin.exe.
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <shlobj.h>
#include <shellapi.h>
#include <commctrl.h>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>
#include <set>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>

#include "pe.h"
#include "manual_map.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' \
    name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
    processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

constexpr int    IDR_PAYLOAD       = 1001;
constexpr wchar_t TARGET_EXE[]     = L"RobloxPlayerBeta.exe";
constexpr wchar_t WND_CLASS[]      = L"pengooin_main";
constexpr wchar_t PIPE_NAME[]      = L"\\\\.\\pipe\\pengooin_v1";
constexpr UINT   WM_TRAY           = WM_APP + 1;
constexpr UINT   WM_CONSOLE_APPEND = WM_APP + 2;
constexpr UINT   ID_EDITOR         = 1010;
constexpr UINT   ID_CONSOLE        = 1011;
constexpr UINT   ID_RUN            = 1020;
constexpr UINT   ID_CLEAR          = 1021;
constexpr UINT   ID_SAVE           = 1022;
constexpr UINT   ID_TRAY_SHOW      = 40001;
constexpr UINT   ID_TRAY_EXIT      = 40002;
constexpr UINT   HOTKEY_RUN        = 0x9F01;
constexpr DWORD  POLL_INTERVAL_MS  = 500;
constexpr DWORD  SETTLE_DELAY_MS   = 1500;
constexpr uint32_t MAX_SCRIPT_BYTES = 16u * 1024u * 1024u;

std::atomic<bool>       g_stop{false};
std::mutex              g_seen_mtx;
std::set<DWORD>         g_injected;
std::atomic<int>        g_inject_count{0};
std::atomic<int>        g_client_count{0};

std::mutex              g_pipes_mtx;
std::vector<HANDLE>     g_pipes;

HWND                    g_hwnd    = nullptr;
HWND                    g_editor  = nullptr;
HWND                    g_console = nullptr;
HWND                    g_btn_run = nullptr;
HWND                    g_btn_clr = nullptr;
HWND                    g_btn_sav = nullptr;
HWND                    g_status  = nullptr;
HFONT                   g_font    = nullptr;
NOTIFYICONDATAW         g_nid{};
const uint8_t*          g_dll_data = nullptr;
size_t                  g_dll_size = 0;

std::mutex              g_out_mtx;
std::string             g_pending_out;

// ---- log to %LOCALAPPDATA%\pengooin\watcher.log ------------------------

std::wstring log_dir() {
    wchar_t appdata[MAX_PATH]{};
    if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appdata) != S_OK)
        return {};
    std::wstring dir = appdata;
    dir += L"\\pengooin";
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return dir;
}

void logf(const char* fmt, ...) {
    auto d = log_dir();
    if (d.empty()) return;
    std::wstring p = d + L"\\watcher.log";
    HANDLE h = CreateFileW(p.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;

    SYSTEMTIME t; GetLocalTime(&t);
    char stamp[64];
    int slen = sprintf_s(stamp, "[%04d-%02d-%02d %02d:%02d:%02d] ",
                         t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);

    char body[2048];
    va_list ap; va_start(ap, fmt);
    int blen = vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    if (blen < 0) blen = 0;
    if (blen > (int)sizeof(body) - 2) blen = (int)sizeof(body) - 2;
    body[blen++] = '\r'; body[blen++] = '\n';

    DWORD w = 0;
    WriteFile(h, stamp, (DWORD)slen, &w, nullptr);
    WriteFile(h, body,  (DWORD)blen, &w, nullptr);
    CloseHandle(h);
}

// ---- console output helpers --------------------------------------------

void console_enqueue(const std::string& line) {
    {
        std::lock_guard<std::mutex> lk(g_out_mtx);
        g_pending_out += line;
        g_pending_out += "\r\n";
    }
    if (g_hwnd) PostMessageW(g_hwnd, WM_CONSOLE_APPEND, 0, 0);
}

void console_drain() {
    std::string pending;
    {
        std::lock_guard<std::mutex> lk(g_out_mtx);
        pending.swap(g_pending_out);
    }
    if (pending.empty()) return;
    int len = GetWindowTextLengthA(g_console);
    SendMessageA(g_console, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageA(g_console, EM_REPLACESEL, FALSE, (LPARAM)pending.c_str());
}

// ---- embedded payload extraction ---------------------------------------

bool extract_embedded_payload() {
    HRSRC rc = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_PAYLOAD), RT_RCDATA);
    if (!rc) { logf("FindResource failed: %lu", GetLastError()); return false; }
    HGLOBAL h = LoadResource(nullptr, rc);
    if (!h)  { logf("LoadResource failed: %lu", GetLastError());  return false; }
    g_dll_data = static_cast<const uint8_t*>(LockResource(h));
    g_dll_size = SizeofResource(nullptr, rc);
    if (!g_dll_data || !g_dll_size) { logf("resource empty"); return false; }
    logf("embedded payload: %zu bytes", g_dll_size);
    return true;
}

// ---- process helpers ---------------------------------------------------

bool pid_alive(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    DWORD ec = 0;
    bool alive = GetExitCodeProcess(h, &ec) && ec == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
}

bool main_module_ready(HANDLE proc) {
    HMODULE mods[8]{};
    DWORD needed = 0;
    if (!EnumProcessModules(proc, mods, sizeof(mods), &needed)) return false;
    return needed > 0 && mods[0] != nullptr;
}

std::set<DWORD> find_roblox_pids() {
    std::set<DWORD> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W e{ sizeof(e) };
    if (Process32FirstW(snap, &e)) {
        do {
            if (!_wcsicmp(e.szExeFile, TARGET_EXE)) out.insert(e.th32ProcessID);
        } while (Process32NextW(snap, &e));
    }
    CloseHandle(snap);
    return out;
}

bool inject(DWORD pid) {
    HANDLE proc = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_READ |
        PROCESS_VM_WRITE      | PROCESS_QUERY_INFORMATION,
        FALSE, pid);
    if (!proc) {
        logf("OpenProcess(%lu) failed: %lu (Hyperion-protected?)", pid, GetLastError());
        console_enqueue("[watcher] OpenProcess failed for pid " + std::to_string(pid) +
                        " (Hyperion-protected game blocks handle rights)");
        return false;
    }
    for (int i = 0; i < 20; ++i) {
        if (main_module_ready(proc)) break;
        Sleep(100);
    }
    pe::Image img;
    if (!pe::load_from_memory(g_dll_data, g_dll_size, img)) {
        logf("pe::load_from_memory failed");
        CloseHandle(proc);
        return false;
    }
    uintptr_t base = mm::inject(proc, img);
    CloseHandle(proc);
    if (!base) {
        logf("mm::inject failed for pid %lu", pid);
        console_enqueue("[watcher] injection failed for pid " + std::to_string(pid));
        return false;
    }
    logf("injected pid %lu @ 0x%016llX", pid, (unsigned long long)base);
    g_inject_count.fetch_add(1);
    console_enqueue("[watcher] injected pid " + std::to_string(pid));
    return true;
}

DWORD WINAPI watcher_thread(LPVOID) {
    logf("watcher started");
    while (!g_stop.load(std::memory_order_acquire)) {
        auto pids = find_roblox_pids();
        {
            std::lock_guard<std::mutex> lk(g_seen_mtx);
            for (auto it = g_injected.begin(); it != g_injected.end(); ) {
                if (pids.find(*it) == pids.end() || !pid_alive(*it))
                    it = g_injected.erase(it);
                else
                    ++it;
            }
        }
        for (DWORD pid : pids) {
            {
                std::lock_guard<std::mutex> lk(g_seen_mtx);
                if (g_injected.count(pid)) continue;
                g_injected.insert(pid);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(SETTLE_DELAY_MS));
            if (!pid_alive(pid)) continue;
            inject(pid);
        }
        for (DWORD slept = 0; slept < POLL_INTERVAL_MS && !g_stop; slept += 50)
            Sleep(50);
    }
    return 0;
}

// ---- pipe server -------------------------------------------------------

bool read_exact(HANDLE h, void* buf, DWORD n) {
    DWORD got = 0;
    while (got < n) {
        DWORD r = 0;
        if (!ReadFile(h, static_cast<uint8_t*>(buf) + got, n - got, &r, nullptr)) return false;
        if (r == 0) return false;
        got += r;
    }
    return true;
}

bool write_exact(HANDLE h, const void* buf, DWORD n) {
    DWORD sent = 0;
    while (sent < n) {
        DWORD w = 0;
        if (!WriteFile(h, static_cast<const uint8_t*>(buf) + sent, n - sent, &w, nullptr)) return false;
        if (w == 0) return false;
        sent += w;
    }
    return true;
}

void pipe_client_loop(HANDLE h) {
    {
        std::lock_guard<std::mutex> lk(g_pipes_mtx);
        g_pipes.push_back(h);
        g_client_count.store((int)g_pipes.size());
    }
    console_enqueue("[watcher] payload connected");

    for (;;) {
        uint32_t rlen = 0;
        if (!read_exact(h, &rlen, sizeof(rlen))) break;
        if (rlen > MAX_SCRIPT_BYTES) break;
        std::string resp(rlen, '\0');
        if (rlen && !read_exact(h, resp.data(), rlen)) break;
        console_enqueue("[result] " + resp);
    }

    {
        std::lock_guard<std::mutex> lk(g_pipes_mtx);
        for (auto it = g_pipes.begin(); it != g_pipes.end(); ++it) {
            if (*it == h) { g_pipes.erase(it); break; }
        }
        g_client_count.store((int)g_pipes.size());
    }
    CloseHandle(h);
    console_enqueue("[watcher] payload disconnected");
}

DWORD WINAPI pipe_server_thread(LPVOID) {
    while (!g_stop.load(std::memory_order_acquire)) {
        HANDLE h = CreateNamedPipeW(
            PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            64 * 1024, 64 * 1024, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            logf("CreateNamedPipe failed: %lu", GetLastError());
            Sleep(1000);
            continue;
        }
        BOOL ok = ConnectNamedPipe(h, nullptr);
        if (!ok && GetLastError() != ERROR_PIPE_CONNECTED) {
            CloseHandle(h);
            continue;
        }
        std::thread(pipe_client_loop, h).detach();
    }
    return 0;
}

void broadcast_script(const std::string& script) {
    if (script.empty()) return;
    uint32_t len = static_cast<uint32_t>(script.size());
    std::vector<HANDLE> snapshot;
    {
        std::lock_guard<std::mutex> lk(g_pipes_mtx);
        snapshot = g_pipes;
    }
    if (snapshot.empty()) {
        console_enqueue("[watcher] no payload connected - launch Roblox and wait for injection");
        return;
    }
    int sent = 0;
    for (HANDLE h : snapshot) {
        if (write_exact(h, &len, sizeof(len)) && write_exact(h, script.data(), len)) ++sent;
    }
    console_enqueue("[watcher] sent " + std::to_string(script.size()) +
                    " bytes to " + std::to_string(sent) + " client(s)");
}

// ---- UI ---------------------------------------------------------------

void update_status_bar() {
    wchar_t buf[192];
    swprintf_s(buf, L"payload clients: %d  |  injections: %d",
               g_client_count.load(), g_inject_count.load());
    if (g_status) SetWindowTextW(g_status, buf);
}

void run_current_editor() {
    int len = GetWindowTextLengthA(g_editor);
    std::string buf(len, '\0');
    GetWindowTextA(g_editor, buf.data(), len + 1);
    buf.resize(len);
    if (buf.empty()) {
        console_enqueue("[watcher] editor empty");
        return;
    }
    std::thread([b = std::move(buf)]() { broadcast_script(b); }).detach();
}

void save_editor_to_disk() {
    auto d = log_dir();
    if (d.empty()) return;
    std::wstring p = d + L"\\last_script.luau";
    int len = GetWindowTextLengthA(g_editor);
    std::string buf(len, '\0');
    GetWindowTextA(g_editor, buf.data(), len + 1);
    buf.resize(len);
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { console_enqueue("[watcher] save failed"); return; }
    DWORD w = 0;
    WriteFile(h, buf.data(), (DWORD)buf.size(), &w, nullptr);
    CloseHandle(h);
    console_enqueue("[watcher] saved editor to %LOCALAPPDATA%\\pengooin\\last_script.luau");
}

void load_last_script() {
    auto d = log_dir();
    if (d.empty()) return;
    std::wstring p = d + L"\\last_script.luau";
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    std::string buf(static_cast<size_t>(sz.QuadPart), '\0');
    DWORD r = 0;
    ReadFile(h, buf.data(), (DWORD)buf.size(), &r, nullptr);
    CloseHandle(h);
    buf.resize(r);
    SetWindowTextA(g_editor, buf.c_str());
}

void layout_children(HWND parent) {
    RECT rc; GetClientRect(parent, &rc);
    int W = rc.right, H = rc.bottom;
    int tool_h = 40, status_h = 22;
    int split_y = (H - tool_h - status_h) * 60 / 100;

    MoveWindow(g_editor,  0, 0,        W, split_y, TRUE);
    MoveWindow(g_console, 0, split_y,  W, H - split_y - tool_h - status_h, TRUE);

    int y = H - tool_h - status_h;
    MoveWindow(g_btn_run, 8,   y + 6, 90, 28, TRUE);
    MoveWindow(g_btn_clr, 106, y + 6, 90, 28, TRUE);
    MoveWindow(g_btn_sav, 204, y + 6, 90, 28, TRUE);
    MoveWindow(g_status,  0, H - status_h, W, status_h, TRUE);
}

void tray_add(HWND hwnd) {
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = hwnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon            = LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"pengooin - executor watcher");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}
void tray_remove() { Shell_NotifyIconW(NIM_DELETE, &g_nid); }

void tray_menu(HWND hwnd) {
    POINT p; GetCursorPos(&p);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_TRAY_SHOW, L"Show");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_TRAY_EXIT, L"Exit");
    SetForegroundWindow(hwnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, p.x, p.y, 0, hwnd, nullptr);
    DestroyMenu(m);
}

LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
        InitCommonControlsEx(&icc);

        g_font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            FF_MODERN | FIXED_PITCH, L"Consolas");

        g_editor = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
            ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
            0, 0, 0, 0, h, (HMENU)(uintptr_t)ID_EDITOR, nullptr, nullptr);

        g_console = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            0, 0, 0, 0, h, (HMENU)(uintptr_t)ID_CONSOLE, nullptr, nullptr);

        for (HWND ctrl : { g_editor, g_console })
            SendMessageW(ctrl, WM_SETFONT, (WPARAM)g_font, TRUE);

        g_btn_run = CreateWindowW(L"BUTTON", L"Run (F5)",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            0, 0, 0, 0, h, (HMENU)(uintptr_t)ID_RUN, nullptr, nullptr);
        g_btn_clr = CreateWindowW(L"BUTTON", L"Clear",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, h, (HMENU)(uintptr_t)ID_CLEAR, nullptr, nullptr);
        g_btn_sav = CreateWindowW(L"BUTTON", L"Save",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, h, (HMENU)(uintptr_t)ID_SAVE, nullptr, nullptr);

        g_status = CreateWindowW(L"STATIC", L"payload clients: 0  |  injections: 0",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 0, 0, h, nullptr, nullptr, nullptr);

        RegisterHotKey(h, HOTKEY_RUN, 0, VK_F5);
        SetTimer(h, 1, 250, nullptr);   // status + console drain tick

        load_last_script();
        console_enqueue("[watcher] ready - launch Roblox to auto-inject");
        return 0;
    }
    case WM_SIZE:
        layout_children(h);
        return 0;
    case WM_TIMER:
        console_drain();
        update_status_bar();
        return 0;
    case WM_CONSOLE_APPEND:
        console_drain();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case ID_RUN:       run_current_editor();      return 0;
        case ID_CLEAR:     SetWindowTextA(g_console, ""); return 0;
        case ID_SAVE:      save_editor_to_disk();     return 0;
        case ID_TRAY_SHOW:
            ShowWindow(h, SW_SHOW);
            SetForegroundWindow(h);
            return 0;
        case ID_TRAY_EXIT:
            g_stop = true;
            DestroyWindow(h);
            return 0;
        }
        return 0;
    case WM_HOTKEY:
        if (w == HOTKEY_RUN) run_current_editor();
        return 0;
    case WM_TRAY:
        if (LOWORD(l) == WM_LBUTTONDBLCLK) {
            ShowWindow(h, SW_SHOW);
            SetForegroundWindow(h);
        } else if (LOWORD(l) == WM_RBUTTONUP) {
            tray_menu(h);
        }
        return 0;
    case WM_CLOSE:
        ShowWindow(h, SW_HIDE);
        return 0;
    case WM_DESTROY:
        KillTimer(h, 1);
        UnregisterHotKey(h, HOTKEY_RUN);
        tray_remove();
        if (g_font) DeleteObject(g_font);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

bool acquire_single_instance() {
    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"Global\\pengooin_watcher_mutex");
    if (!mtx) return false;
    if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(mtx); return false; }
    return true;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    if (!acquire_single_instance()) {
        MessageBoxW(nullptr, L"pengooin is already running.", L"pengooin", MB_ICONINFORMATION);
        return 0;
    }

    log_dir();
    logf("=== pengooin watcher boot ===");

    if (!extract_embedded_payload()) {
        MessageBoxW(nullptr, L"Failed to load embedded payload resource.",
                    L"pengooin", MB_ICONERROR);
        return 1;
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = WND_CLASS;
    if (!RegisterClassW(&wc)) { logf("RegisterClass failed"); return 2; }

    g_hwnd = CreateWindowExW(0, WND_CLASS, L"pengooin",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1000, 700,
        nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd) { logf("CreateWindow failed"); return 3; }

    tray_add(g_hwnd);
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    HANDLE tw = CreateThread(nullptr, 0, watcher_thread,     nullptr, 0, nullptr);
    HANDLE tp = CreateThread(nullptr, 0, pipe_server_thread, nullptr, 0, nullptr);
    if (!tw || !tp) { logf("worker thread spawn failed"); return 4; }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(g_hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    g_stop = true;
    WaitForSingleObject(tw, 2000);
    CloseHandle(tw);
    // pipe server may block in ConnectNamedPipe; leave the handle - process exit
    // cleans it up on the last GetMessage return.
    CloseHandle(tp);
    logf("clean exit");
    return 0;
}
