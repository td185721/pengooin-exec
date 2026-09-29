// pipe_client.cpp — receive scripts from the watcher over a named pipe
// language: C++20, target: Windows 11 x64, MSVC
//
// wire format (both directions):
//   uint32_t length (little-endian) + <length> bytes UTF-8 payload
//
// client-to-server: request { "script": <utf8 bytes> }
// server-to-client: response { "ok" | "err: <message>" }
//
// worker thread loops:
//   1. WaitNamedPipeW + CreateFileW → connect
//   2. drain script frames, hand each to r9k::luau::run on the elevated state
//   3. write result frame back
//   4. on read/write failure, close and reconnect after 1 s
#include "net/pipe_client.h"
#include "luau/executor.h"

#include <windows.h>
#include <cstdint>
#include <atomic>
#include <string>

namespace r9k::net {

namespace {
    constexpr const wchar_t* PIPE_NAME = L"\\\\.\\pipe\\pengooin_v1";
    constexpr uint32_t MAX_SCRIPT_BYTES = 16u * 1024u * 1024u;   // 16 MB cap

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
        for (;;) {
            uint32_t len = 0;
            if (!read_exact(h, &len, sizeof(len))) return;
            if (len == 0 || len > MAX_SCRIPT_BYTES) return;

            std::string script(len, '\0');
            if (!read_exact(h, script.data(), len)) return;

            auto result = luau::run(script, "=[pengooin]");
            std::string response = result.ok ? std::string("ok") : ("err: " + result.error);

            uint32_t rlen = static_cast<uint32_t>(response.size());
            if (!write_exact(h, &rlen, sizeof(rlen))) return;
            if (rlen && !write_exact(h, response.data(), rlen)) return;
        }
    }

    DWORD WINAPI worker(LPVOID) {
        while (g_running.load(std::memory_order_acquire)) {
            HANDLE h = CreateFileW(PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                   OPEN_EXISTING, 0, nullptr);
            if (h == INVALID_HANDLE_VALUE) {
                Sleep(1000);
                continue;
            }
            handle_connection(h);
            CloseHandle(h);
            Sleep(500);   // brief backoff before reconnect
        }
        return 0;
    }
}

void pipe_client_start() {
    if (g_running.exchange(true, std::memory_order_acq_rel)) return;
    HANDLE th = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
    if (th) CloseHandle(th);
}

}  // namespace r9k::net
