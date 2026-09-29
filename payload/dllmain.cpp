// dllmain.cpp — payload entry; spins runtime on a fresh thread
// language: C++20, target: Windows 11 x64, MSVC
#include "pch.h"

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID reserved) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;

    // manual-mapper stub passes MM_MAGIC as reserved; a normal loader passes null.
    // never do heavy work inside DllMain — spawn a thread and return immediately.
    HANDLE th = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
        r9k::runtime_boot();
        return 0;
    }, nullptr, 0, nullptr);
    if (th) CloseHandle(th);
    (void)reserved;
    return TRUE;
}
