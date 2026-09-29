// dllmain.cpp — payload entry; spins runtime on a fresh thread
// language: C++20, target: Windows 11 x64, MSVC
#include "pch.h"
#include "net/pipe_client.h"

// raw_canary: prove DllMain ran without using ANY CRT function. Only kernel32
// APIs, which resolve through the IAT that mm::inject already fixed up. If
// CRT init crashed silently before user DllMain, dbg_log would never fire
// (sprintf_s etc. may not be safe) but this canary still lands.
static void raw_canary(const char* tag) {
    // build a %TEMP%\pengooin_canary.txt candidate first (respects AppContainer)
    wchar_t temp_path[MAX_PATH + 32]{};
    DWORD tlen = GetTempPathW(MAX_PATH, temp_path);
    if (tlen > 0 && tlen < MAX_PATH) {
        const wchar_t* suffix = L"pengooin_canary.txt";
        DWORD sfx = 0; while (suffix[sfx]) ++sfx;
        // ensure trailing backslash
        if (temp_path[tlen - 1] != L'\\') { temp_path[tlen++] = L'\\'; temp_path[tlen] = 0; }
        for (DWORD i = 0; i <= sfx; ++i) temp_path[tlen + i] = suffix[i];
    } else {
        temp_path[0] = 0;
    }

    const wchar_t* candidates[] = {
        (temp_path[0] ? temp_path : nullptr),         // %TEMP%\pengooin_canary.txt
        L"C:\\Users\\Public\\pengooin_canary.txt",
        L"C:\\Windows\\Temp\\pengooin_canary.txt",
        L"C:\\pengooin_canary.txt",
    };
    for (const wchar_t* path : candidates) {
        if (!path) continue;
        HANDLE h = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        DWORD w = 0;
        // manual strlen so we skip <cstring>
        DWORD n = 0; while (tag[n]) ++n;
        WriteFile(h, tag, n, &w, nullptr);
        WriteFile(h, "\r\n", 2, &w, nullptr);
        CloseHandle(h);
        return;   // first success wins
    }
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID reserved) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;

    raw_canary("DllMain reached (post-CRT init)");
    OutputDebugStringA("pengooin: DllMain reached\n");

    r9k::dbg_log("DllMain: DLL_PROCESS_ATTACH");

    // manual-mapper stub passes MM_MAGIC as reserved; a normal loader passes null.
    // never do heavy work inside DllMain — spawn a thread and return immediately.
    HANDLE th = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
        raw_canary("runtime thread reached");
        r9k::dbg_log("runtime thread: entering runtime_boot");
        r9k::runtime_boot();
        r9k::dbg_log("runtime thread: runtime_boot returned");
        raw_canary("runtime_boot returned");
        return 0;
    }, nullptr, 0, nullptr);
    if (th) CloseHandle(th);
    else    r9k::dbg_log("DllMain: CreateThread failed: %lu", GetLastError());
    (void)reserved;
    return TRUE;
}

// ---------------------------------------------------------------------------
// pengooin_dll_entry - custom DLL entry point (set via /ENTRY link flag).
//
// Manual-mapped DLLs don't get RtlAddFunctionTable registration automatically,
// which means SEH __try/__except inside _DllMainCRTStartup (used by the MSVC
// CRT for guarded init) has no unwind tables to consult. Any exception during
// CRT init then bubbles up to the OS's unhandled-exception path and kills the
// thread silently -- which is exactly the "injected pid X, no payload
// connected, no canary anywhere" symptom.
//
// This entry runs BEFORE the CRT, writes an early canary (proving the shell-
// code actually called us), registers our own unwind info, then hands off to
// _DllMainCRTStartup so C++ globals still get constructed normally.
// ---------------------------------------------------------------------------
extern "C" BOOL WINAPI _DllMainCRTStartup(HINSTANCE, DWORD, LPVOID);

extern "C" BOOL WINAPI pengooin_dll_entry(HINSTANCE hDll, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        raw_canary("pengooin_dll_entry: pre-CRT");

        // register x64 unwind info so any AV during CRT init unwinds cleanly
        BYTE* base = reinterpret_cast<BYTE*>(hDll);
        auto* dos  = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        auto* nt   = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        auto& exc  = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        if (exc.Size) {
            PRUNTIME_FUNCTION rf = reinterpret_cast<PRUNTIME_FUNCTION>(base + exc.VirtualAddress);
            DWORD count = exc.Size / sizeof(RUNTIME_FUNCTION);
            if (RtlAddFunctionTable(rf, count, reinterpret_cast<DWORD64>(base)))
                raw_canary("pengooin_dll_entry: unwind table registered");
            else
                raw_canary("pengooin_dll_entry: RtlAddFunctionTable FAILED");
        } else {
            raw_canary("pengooin_dll_entry: no unwind table in image");
        }
    }

    BOOL result = _DllMainCRTStartup(hDll, reason, reserved);

    if (reason == DLL_PROCESS_ATTACH) {
        raw_canary(result ? "pengooin_dll_entry: CRT startup returned TRUE"
                          : "pengooin_dll_entry: CRT startup returned FALSE");
    }
    return result;
}
