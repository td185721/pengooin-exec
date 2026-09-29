// main.cpp — pengooin watcher: single-exe auto-injector
// language: C++20, target: Windows 11 x64, MSVC
//
// runs as a tray-icon background process. polls the process list every 500ms
// for RobloxPlayerBeta.exe; on new hits, manual-maps the embedded payload
// into the fresh instance. dedup by pid so re-scans don't double-inject.
//
// embedded payload:
//   the payload DLL is compiled by the sibling target r9k_payload and stored
//   as an RCDATA resource (IDR_PAYLOAD) at link time. no companion .dll on
//   disk; users receive one file: pengooin.exe.
//
// UX:
//   - tray icon (right-click → Exit) is the only visible surface
//   - startup + injection events log to
//     %LOCALAPPDATA%\pengooin\watcher.log
//
// hyperion note:
//   OpenProcess with VM_WRITE|CREATE_THREAD is stripped from handles Hyperion
//   protects. the watcher logs the failure and moves on — matches the
//   pre-watcher CLI behavior. games without Hyperion attach cleanly.
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <shlobj.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdint>
#include <string>
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

namespace {

// resource id must match the generated .rc emitted by CMake
constexpr int IDR_PAYLOAD = 1001;

constexpr wchar_t TARGET_EXE[]      = L"RobloxPlayerBeta.exe";
constexpr wchar_t WND_CLASS[]       = L"pengooin_watcher";
constexpr UINT    WM_TRAY           = WM_APP + 1;
constexpr UINT    ID_TRAY_EXIT      = 40001;
constexpr UINT    ID_TRAY_STATUS    = 40002;
constexpr DWORD   POLL_INTERVAL_MS  = 500;
constexpr DWORD   SETTLE_DELAY_MS   = 1500;   // wait for main module to map

std::atomic<bool>        g_stop{false};
std::mutex               g_seen_mtx;
std::set<DWORD>          g_injected;
std::atomic<int>         g_inject_count{0};

HWND                     g_hwnd    = nullptr;
NOTIFYICONDATAW          g_nid{};
const uint8_t*           g_dll_data = nullptr;
size_t                   g_dll_size = 0;

std::wstring log_dir() {
    wchar_t appdata[MAX_PATH]{};
    if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appdata) != S_OK)
        return {};
    std::wstring dir = appdata;
    dir += L"\\pengooin";
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return dir;
}

std::wstring log_path() {
    auto d = log_dir();
    return d.empty() ? std::wstring{} : d + L"\\watcher.log";
}

void logf(const char* fmt, ...) {
    auto p = log_path();
    if (p.empty()) return;
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
    if (blen > (int)sizeof(body) - 2) blen = sizeof(body) - 2;
    body[blen++] = '\r'; body[blen++] = '\n';

    DWORD w = 0;
    WriteFile(h, stamp, (DWORD)slen, &w, nullptr);
    WriteFile(h, body,  (DWORD)blen, &w, nullptr);
    CloseHandle(h);
}

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

// process is still alive?
bool pid_alive(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    DWORD ec = 0;
    bool alive = GetExitCodeProcess(h, &ec) && ec == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
}

// main module fully mapped yet? on a very fresh process EnumProcessModules
// returns nothing until the loader initializes; we treat that as "not ready".
bool main_module_ready(HANDLE proc) {
    HMODULE mods[8]{};
    DWORD needed = 0;
    if (!EnumProcessModules(proc, mods, sizeof(mods), &needed)) return false;
    return needed > 0 && mods[0] != nullptr;
}

bool inject(DWORD pid) {
    HANDLE proc = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_READ |
        PROCESS_VM_WRITE      | PROCESS_QUERY_INFORMATION,
        FALSE, pid);
    if (!proc) {
        logf("OpenProcess(%lu) failed: %lu (Hyperion-protected?)", pid, GetLastError());
        return false;
    }

    // wait briefly for the loader to publish the main module
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
    if (!base) { logf("mm::inject failed for pid %lu", pid); return false; }

    logf("injected pid %lu @ 0x%016llX", pid, (unsigned long long)base);
    g_inject_count.fetch_add(1);
    return true;
}

// walk the process table once; return the set of RobloxPlayerBeta pids.
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

DWORD WINAPI watcher_thread(LPVOID) {
    logf("watcher started");
    while (!g_stop.load(std::memory_order_acquire)) {
        auto pids = find_roblox_pids();

        // prune dead pids so a relaunched Roblox is treated as new
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
                // reserve the slot now — success or failure, we don't retry
                // this pid. relaunching Roblox mints a new pid, which the
                // prune loop above lets through.
                g_injected.insert(pid);
            }

            // let Roblox settle before we probe it
            std::this_thread::sleep_for(std::chrono::milliseconds(SETTLE_DELAY_MS));
            if (!pid_alive(pid)) continue;
            inject(pid);
        }

        for (DWORD slept = 0; slept < POLL_INTERVAL_MS && !g_stop; slept += 50)
            Sleep(50);
    }
    logf("watcher stopping");
    return 0;
}

void tray_add(HWND hwnd) {
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = hwnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon            = LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"pengooin — watching for RobloxPlayerBeta");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}
void tray_remove() { Shell_NotifyIconW(NIM_DELETE, &g_nid); }

void tray_menu(HWND hwnd) {
    POINT p; GetCursorPos(&p);
    HMENU m = CreatePopupMenu();

    wchar_t status[128];
    int n = g_inject_count.load();
    swprintf_s(status, L"pengooin — %d injection%s", n, n == 1 ? L"" : L"s");
    AppendMenuW(m, MF_STRING | MF_GRAYED, ID_TRAY_STATUS, status);
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_TRAY_EXIT, L"Exit");

    SetForegroundWindow(hwnd);   // required so the menu dismisses on click-away
    TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, p.x, p.y, 0, hwnd, nullptr);
    DestroyMenu(m);
}

LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_TRAY:
        if (LOWORD(l) == WM_RBUTTONUP || LOWORD(l) == WM_LBUTTONUP) tray_menu(h);
        return 0;
    case WM_COMMAND:
        if (LOWORD(w) == ID_TRAY_EXIT) { PostMessage(h, WM_CLOSE, 0, 0); }
        return 0;
    case WM_CLOSE:
        g_stop = true;
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        tray_remove();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// single-instance guard so relaunching doesn't stack watchers
bool acquire_single_instance() {
    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"Global\\pengooin_watcher_mutex");
    if (!mtx) return false;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mtx);
        return false;
    }
    return true;   // leak intentional — released when process exits
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
    wc.lpszClassName = WND_CLASS;
    if (!RegisterClassW(&wc)) { logf("RegisterClass failed"); return 2; }

    g_hwnd = CreateWindowExW(0, WND_CLASS, L"pengooin", 0, 0, 0, 0, 0,
                             HWND_MESSAGE, nullptr, hInst, nullptr);
    if (!g_hwnd) { logf("CreateWindow failed"); return 3; }

    tray_add(g_hwnd);

    HANDLE th = CreateThread(nullptr, 0, watcher_thread, nullptr, 0, nullptr);
    if (!th) { logf("watcher thread failed"); return 4; }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    g_stop = true;
    WaitForSingleObject(th, 2000);
    CloseHandle(th);
    logf("clean exit");
    return 0;
}
