// dllmain.cpp — payload entry; spins runtime on a fresh thread
// language: C++20, target: Windows 11 x64, MSVC
#include "pch.h"
#include "net/pipe_client.h"

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID reserved) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;

    r9k::dbg_log("DllMain: DLL_PROCESS_ATTACH");

    // manual-mapper stub passes MM_MAGIC as reserved; a normal loader passes null.
    // never do heavy work inside DllMain — spawn a thread and return immediately.
    HANDLE th = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
        r9k::dbg_log("runtime thread: entering runtime_boot");
        r9k::runtime_boot();
        r9k::dbg_log("runtime thread: runtime_boot returned");
        return 0;
    }, nullptr, 0, nullptr);
    if (th) CloseHandle(th);
    else    r9k::dbg_log("DllMain: CreateThread failed: %lu", GetLastError());
    (void)reserved;
    return TRUE;
}
