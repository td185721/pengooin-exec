// main.cpp — r9k injector entry
// language: C++20, target: Windows 11 x64, MSVC
// usage: r9k.exe --attach RobloxPlayerBeta.exe --dll r9k_payload.dll
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cwchar>
#include "pe.h"
#include "manual_map.h"

static DWORD find_pid(const wchar_t* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W e{ sizeof(e) };
    DWORD hit = 0;
    if (Process32FirstW(snap, &e)) {
        do { if (!_wcsicmp(e.szExeFile, name)) { hit = e.th32ProcessID; break; } }
        while (Process32NextW(snap, &e));
    }
    CloseHandle(snap);
    return hit;
}

static const wchar_t* arg_val(int argc, wchar_t** argv, const wchar_t* key) {
    for (int i = 1; i + 1 < argc; ++i) if (!_wcsicmp(argv[i], key)) return argv[i + 1];
    return nullptr;
}

int wmain(int argc, wchar_t** argv) {
    const wchar_t* target = arg_val(argc, argv, L"--attach");
    const wchar_t* dllp   = arg_val(argc, argv, L"--dll");
    if (!target || !dllp) {
        std::fwprintf(stderr, L"usage: r9k --attach <process.exe> --dll <payload.dll>\n");
        return 1;
    }

    DWORD pid = find_pid(target);
    if (!pid) { std::fwprintf(stderr, L"process not found: %s\n", target); return 2; }

    HANDLE proc = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_READ |
        PROCESS_VM_WRITE      | PROCESS_QUERY_INFORMATION,
        FALSE, pid);
    if (!proc) { std::fwprintf(stderr, L"OpenProcess failed: %lu\n", GetLastError()); return 3; }

    pe::Image img;
    if (!pe::load(dllp, img)) { std::fwprintf(stderr, L"pe::load failed\n"); return 4; }

    uintptr_t base = mm::inject(proc, img);
    CloseHandle(proc);
    if (!base) { std::fwprintf(stderr, L"inject failed\n"); return 5; }

    std::wprintf(L"injected @ 0x%016llX (pid %lu)\n", (unsigned long long)base, pid);
    return 0;
}
