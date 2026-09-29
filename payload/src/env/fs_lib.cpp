// fs_lib.cpp — filesystem library, sandboxed under %LOCALAPPDATA%\pengooin\workspace
// language: C++20, target: Windows 11 x64, MSVC
//
// exposes: writefile, readfile, appendfile, listfiles, makefolder, delfolder,
//          delfile, isfile, isfolder, loadfile, dofile, getcustomasset.
//
// sandbox rules:
//   - all paths resolved relative to the workspace root
//   - `..` segments and absolute prefixes stripped before joining
//   - reject any resolved path that lands outside the workspace canonical dir
//   - forward slashes normalized to backslashes so Windows APIs accept them
//
// getcustomasset returns `rbxasset://pengooin/<relpath>` which the Roblox
// content pipeline will resolve if we register a content-provider stub — for
// now it returns a `file://` URI that FileMesh / Sound loaders accept.
#include "env/fs_lib.h"
#include "env/environment.h"
#include "luau/api.h"
#include "luau/internal.h"
#include "luau/compiler_bridge.h"
#include "roblox/luau_state.h"

#include <shlwapi.h>
#include <shlobj.h>
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")

namespace r9k::env::fs_lib {

using luau::api;

namespace {
    std::wstring g_workspace;

    std::wstring workspace_root() {
        if (!g_workspace.empty()) return g_workspace;
        wchar_t appdata[MAX_PATH]{};
        if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appdata) != S_OK)
            return {};
        g_workspace  = appdata;
        g_workspace += L"\\pengooin\\workspace";
        SHCreateDirectoryExW(nullptr, g_workspace.c_str(), nullptr);
        return g_workspace;
    }

    // normalize + reject traversal. returns empty string on rejection.
    std::wstring resolve(std::string_view rel) {
        std::wstring w;
        w.reserve(rel.size());
        for (char c : rel) {
            if (c == '/') w += L'\\';
            else if (c == '\0') break;
            else w += (wchar_t)(u8)c;
        }
        // strip leading separators + drive prefixes to keep users inside sandbox
        while (!w.empty() && (w.front() == L'\\' || w.front() == L'/')) w.erase(w.begin());
        if (w.size() >= 2 && w[1] == L':') return {};

        // reject any `..` segment
        for (size_t i = 0; i + 1 < w.size(); ++i) {
            if (w[i] == L'.' && w[i + 1] == L'.') return {};
        }

        auto root = workspace_root();
        if (root.empty()) return {};

        std::wstring full = root + L"\\" + w;
        // canonicalize + confirm containment
        wchar_t canon[MAX_PATH]{};
        if (!PathCanonicalizeW(canon, full.c_str())) return {};
        if (_wcsnicmp(canon, root.c_str(), root.size()) != 0) return {};
        return canon;
    }

    std::string read_all(const std::wstring& p) {
        HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return {};
        LARGE_INTEGER sz{};
        GetFileSizeEx(h, &sz);
        std::string out;
        out.resize(static_cast<size_t>(sz.QuadPart));
        DWORD read = 0;
        ReadFile(h, out.data(), (DWORD)out.size(), &read, nullptr);
        CloseHandle(h);
        out.resize(read);
        return out;
    }

    bool write_all(const std::wstring& p, std::string_view data, bool append) {
        HANDLE h = CreateFileW(p.c_str(),
                               append ? FILE_APPEND_DATA : GENERIC_WRITE,
                               0, nullptr,
                               append ? OPEN_ALWAYS : CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        DWORD wr = 0;
        BOOL ok = WriteFile(h, data.data(), (DWORD)data.size(), &wr, nullptr);
        CloseHandle(h);
        return ok && wr == data.size();
    }
}

static int l_writefile(lua_State* L) {
    const auto& a = api();
    size_t plen=0, dlen=0;
    const char* p = a.tolstring(L, 1, &plen);
    const char* d = a.tolstring(L, 2, &dlen);
    if (!p || !d) return 0;
    auto w = resolve({p, plen});
    if (w.empty()) { a.pushstring(L, "writefile: sandbox violation"); return a.error(L); }
    // ensure parent dir exists
    std::wstring parent = w.substr(0, w.find_last_of(L"\\/"));
    SHCreateDirectoryExW(nullptr, parent.c_str(), nullptr);
    if (!write_all(w, {d, dlen}, false)) {
        a.pushstring(L, "writefile: I/O error"); return a.error(L);
    }
    return 0;
}
static int l_appendfile(lua_State* L) {
    const auto& a = api();
    size_t plen=0, dlen=0;
    const char* p = a.tolstring(L, 1, &plen);
    const char* d = a.tolstring(L, 2, &dlen);
    if (!p || !d) return 0;
    auto w = resolve({p, plen});
    if (w.empty()) { a.pushstring(L, "appendfile: sandbox violation"); return a.error(L); }
    write_all(w, {d, dlen}, true);
    return 0;
}
static int l_readfile(lua_State* L) {
    const auto& a = api();
    size_t plen=0;
    const char* p = a.tolstring(L, 1, &plen);
    if (!p) return 0;
    auto w = resolve({p, plen});
    if (w.empty()) { a.pushnil(L); return 1; }
    auto data = read_all(w);
    a.pushlstring(L, data.data(), data.size());
    return 1;
}
static int l_isfile(lua_State* L) {
    const auto& a = api();
    size_t plen=0;
    const char* p = a.tolstring(L, 1, &plen);
    auto w = resolve({p, plen});
    DWORD attr = w.empty() ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(w.c_str());
    a.pushboolean(L, attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
    return 1;
}
static int l_isfolder(lua_State* L) {
    const auto& a = api();
    size_t plen=0;
    const char* p = a.tolstring(L, 1, &plen);
    auto w = resolve({p, plen});
    DWORD attr = w.empty() ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(w.c_str());
    a.pushboolean(L, attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
    return 1;
}
static int l_delfile(lua_State* L) {
    const auto& a = api();
    size_t plen=0;
    const char* p = a.tolstring(L, 1, &plen);
    auto w = resolve({p, plen});
    if (!w.empty()) DeleteFileW(w.c_str());
    return 0;
}
static int l_delfolder(lua_State* L) {
    const auto& a = api();
    size_t plen=0;
    const char* p = a.tolstring(L, 1, &plen);
    auto w = resolve({p, plen});
    if (w.empty()) return 0;
    // SHFileOperation for recursive delete — double-null-terminated path
    std::vector<wchar_t> buf(w.begin(), w.end());
    buf.push_back(0);
    buf.push_back(0);
    SHFILEOPSTRUCTW op{};
    op.wFunc  = FO_DELETE;
    op.pFrom  = buf.data();
    op.fFlags = FOF_NO_UI | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    SHFileOperationW(&op);
    return 0;
}
static int l_makefolder(lua_State* L) {
    const auto& a = api();
    size_t plen=0;
    const char* p = a.tolstring(L, 1, &plen);
    auto w = resolve({p, plen});
    if (!w.empty()) SHCreateDirectoryExW(nullptr, w.c_str(), nullptr);
    return 0;
}
static int l_listfiles(lua_State* L) {
    const auto& a = api();
    size_t plen=0;
    const char* p = a.tolstring(L, 1, &plen);
    auto w = resolve({p, plen});
    a.createtable(L, 0, 0);
    if (w.empty()) return 1;

    std::wstring glob = w + L"\\*";
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(glob.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 1;
    int n = 0;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        // convert wide → utf8 in-line for the return value
        char narrow[MAX_PATH * 3];
        int nn = WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, narrow, sizeof(narrow), nullptr, nullptr);
        if (nn <= 0) continue;
        // join with the original relative path
        std::string joined(p, plen);
        if (!joined.empty() && joined.back() != '/' && joined.back() != '\\') joined += '/';
        joined.append(narrow, nn - 1);
        a.pushlstring(L, joined.data(), joined.size());
        a.rawseti(L, -2, ++n);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return 1;
}

static int l_loadfile(lua_State* L) {
    const auto& a = api();
    size_t plen=0;
    const char* p = a.tolstring(L, 1, &plen);
    if (!p) { a.pushnil(L); return 1; }
    auto w = resolve({p, plen});
    if (w.empty()) { a.pushnil(L); a.pushstring(L, "sandbox violation"); return 2; }
    auto src = read_all(w);
    if (src.empty()) { a.pushnil(L); a.pushstring(L, "file empty or unreadable"); return 2; }
    std::string bc = luau::compile(src);
    int lr = a.load(L, p, bc.data(), bc.size(), 0);
    if (lr != 0) {
        a.pushnil(L);
        a.insert(L, -2);
        return 2;
    }
    return 1;
}
static int l_dofile(lua_State* L) {
    const auto& a = api();
    int nres = l_loadfile(L);
    if (nres == 2) return 0;   // load failed; nil,err on stack — drop and return
    a.call(L, 0, -1);
    return a.gettop(L);
}
static int l_getcustomasset(lua_State* L) {
    const auto& a = api();
    size_t plen=0;
    const char* p = a.tolstring(L, 1, &plen);
    auto w = resolve({p, plen});
    if (w.empty()) { a.pushnil(L); return 1; }
    // return a file:// URI — Roblox's content loader accepts it for Sound/
    // FileMesh via the local dev shim. real deployment swaps to rbxasset://
    // once content-provider registration lands.
    char narrow[MAX_PATH * 3];
    int nn = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, narrow, sizeof(narrow), nullptr, nullptr);
    if (nn <= 0) { a.pushnil(L); return 1; }
    std::string uri = "file://";
    uri.append(narrow, nn - 1);
    a.pushlstring(L, uri.data(), uri.size());
    return 1;
}

void install() {
    workspace_root();  // eager create
    register_fn("writefile",      l_writefile);
    register_fn("readfile",       l_readfile);
    register_fn("appendfile",     l_appendfile);
    register_fn("listfiles",      l_listfiles);
    register_fn("makefolder",     l_makefolder);
    register_fn("delfolder",      l_delfolder);
    register_fn("delfile",        l_delfile);
    register_fn("isfile",         l_isfile);
    register_fn("isfolder",       l_isfolder);
    register_fn("loadfile",       l_loadfile);
    register_fn("dofile",         l_dofile);
    register_fn("getcustomasset", l_getcustomasset);
    register_fn("getsynasset",    l_getcustomasset);   // Synapse alias
}

}  // namespace r9k::env::fs_lib
