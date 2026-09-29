// pipe_client.cpp - receive scripts from the watcher over a named pipe
// language: C++20, target: Windows 11 x64, MSVC
//
// wire format (both directions):
//   uint32_t length (little-endian) + <length> bytes UTF-8 payload
//
// server-to-client: script text
// client-to-server: response "ok" or "err: <message>"
//
// worker thread loops:
//   1. CreateFileW to \\.\pipe\pengooin_v1
//   2. if that fails, sleep 1s and retry (watcher may not be up yet)
//   3. drain script frames, hand each to r9k::luau::run
//   4. write result frame back
//   5. on read/write failure, close and reconnect after 500 ms
//
// every state transition writes to %LOCALAPPDATA%\pengooin\payload.log so
// silent failures (offset drift, ACL block, DllMain race) are visible.
#include "net/pipe_client.h"
#include "luau/executor.h"

#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <atomic>
#include <string>

#pragma comment(lib, "shell32.lib")

namespace r9k {

void dbg_log(const char* fmt, ...) {
    wchar_t appdata[MAX_PATH]{};
    if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appdata) != S_OK) return;
    std::wstring dir = appdata; dir += L"\\pengooin";
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    std::wstring p = dir + L"\\payload.log";
    HANDLE h = CreateFileW(p.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;

    SYSTEMTIME t; GetLocalTime(&t);
    char stamp[64];
    int slen = sprintf_s(stamp, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] ",
                         t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);

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

}  // namespace r9k

namespace r9k::net {

namespace {
    constexpr const wchar_t* PIPE_NAME = L"\\\\.\\pipe\\pengooin_v1";
    constexpr uint32_t MAX_SCRIPT_BYTES = 16u * 1024u * 1024u;

    std::atomic<bool> g_running{false};

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

    void handle_connection(HANDLE h) {
        dbg_log("pipe connected, entering read loop");
        for (;;) {
            uint32_t len = 0;
            if (!read_exact(h, &len, sizeof(len))) { dbg_log("read len failed: %lu", GetLastError()); return; }
            if (len == 0 || len > MAX_SCRIPT_BYTES) { dbg_log("bad frame length: %u", len); return; }
            std::string script(len, '\0');
            if (!read_exact(h, script.data(), len)) { dbg_log("read body failed: %lu", GetLastError()); return; }
            dbg_log("received script (%u bytes), executing", len);

            auto result = luau::run(script, "=[pengooin]");
            std::string response = result.ok ? std::string("ok") : ("err: " + result.error);
            dbg_log("execution result: %s", response.c_str());

            uint32_t rlen = static_cast<uint32_t>(response.size());
            if (!write_exact(h, &rlen, sizeof(rlen))) { dbg_log("write rlen failed: %lu", GetLastError()); return; }
            if (rlen && !write_exact(h, response.data(), rlen)) { dbg_log("write body failed: %lu", GetLastError()); return; }
        }
    }

    DWORD WINAPI worker(LPVOID) {
        dbg_log("pipe worker thread started");
        int fail_count = 0;
        while (g_running.load(std::memory_order_acquire)) {
            HANDLE h = CreateFileW(PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                   OPEN_EXISTING, 0, nullptr);
            if (h == INVALID_HANDLE_VALUE) {
                DWORD err = GetLastError();
                // only log every ~10 tries to avoid spamming
                if ((fail_count++ % 10) == 0)
                    dbg_log("CreateFile pipe failed: %lu (attempt %d)", err, fail_count);
                Sleep(1000);
                continue;
            }
            fail_count = 0;
            handle_connection(h);
            CloseHandle(h);
            dbg_log("pipe closed, reconnecting in 500ms");
            Sleep(500);
        }
        dbg_log("pipe worker exiting");
        return 0;
    }
}

void pipe_client_start() {
    if (g_running.exchange(true, std::memory_order_acq_rel)) {
        dbg_log("pipe_client_start called twice, ignoring");
        return;
    }
    dbg_log("pipe_client_start: spawning worker");
    HANDLE th = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
    if (th) CloseHandle(th);
    else    dbg_log("CreateThread failed: %lu", GetLastError());
}

}  // namespace r9k::net
