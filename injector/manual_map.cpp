// manual_map.cpp — reflective-style mapper. no LoadLibrary; payload lives outside
// the PEB loader lists so a module walk from the game won't see it.
// language: C++20, target: Windows 11 x64, MSVC
#include "manual_map.h"
#include <cstdio>

namespace mm {

static DWORD g_last_exit = STILL_ACTIVE;
DWORD last_entry_exit_code() { return g_last_exit; }

static bool write_remote(HANDLE proc, uintptr_t dst, const void* src, size_t n) {
    SIZE_T w = 0;
    return WriteProcessMemory(proc, reinterpret_cast<LPVOID>(dst), src, n, &w) && w == n;
}

static bool map_sections(HANDLE proc, uintptr_t remote_base, const pe::Image& img) {
    if (!write_remote(proc, remote_base, img.raw.data(), img.headers_size())) return false;
    auto s = img.sect();
    for (WORD i = 0; i < img.nt()->FileHeader.NumberOfSections; ++i, ++s) {
        if (!s->SizeOfRawData) continue;
        if (!write_remote(proc, remote_base + s->VirtualAddress,
                          img.raw.data() + s->PointerToRawData, s->SizeOfRawData)) return false;
    }
    return true;
}

static bool relocate(HANDLE proc, uintptr_t remote_base, const pe::Image& img) {
    auto& dir = img.nt()->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (!dir.Size) return true;
    const uintptr_t preferred = img.nt()->OptionalHeader.ImageBase;
    const int64_t   delta     = static_cast<int64_t>(remote_base) - static_cast<int64_t>(preferred);
    if (!delta) return true;

    uint32_t raw = pe::rva_to_raw(img, dir.VirtualAddress);
    if (!raw) return false;
    auto* block = reinterpret_cast<IMAGE_BASE_RELOCATION*>(const_cast<uint8_t*>(img.raw.data()) + raw);

    for (DWORD walked = 0; walked < dir.Size && block->SizeOfBlock; ) {
        DWORD count = (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
        auto* entries = reinterpret_cast<WORD*>(block + 1);
        for (DWORD i = 0; i < count; ++i) {
            WORD type = entries[i] >> 12, offset = entries[i] & 0x0FFF;
            if (type != IMAGE_REL_BASED_DIR64) continue;
            uintptr_t patch_va = remote_base + block->VirtualAddress + offset;
            uintptr_t value = 0;
            SIZE_T r = 0;
            if (!ReadProcessMemory(proc, (LPCVOID)patch_va, &value, sizeof(value), &r) || r != sizeof(value)) return false;
            value += delta;
            if (!write_remote(proc, patch_va, &value, sizeof(value))) return false;
        }
        walked += block->SizeOfBlock;
        block = reinterpret_cast<IMAGE_BASE_RELOCATION*>((BYTE*)block + block->SizeOfBlock);
    }
    return true;
}

// resolve IAT entries locally. safe on x64 because ntdll/kernel32/etc. are
// mapped at the same base in every process (ASLR is per-boot, not per-process).
// modules we import that don't hold that invariant (rare) would need remote
// LoadLibraryA — trivial to add when a payload actually pulls one in.
static bool resolve_imports(HANDLE proc, uintptr_t remote_base, const pe::Image& img) {
    auto& dir = img.nt()->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.Size) return true;

    uint32_t desc_raw = pe::rva_to_raw(img, dir.VirtualAddress);
    if (!desc_raw) return false;
    auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(const_cast<uint8_t*>(img.raw.data()) + desc_raw);

    for (; desc->Name; ++desc) {
        uint32_t name_raw = pe::rva_to_raw(img, desc->Name);
        if (!name_raw) return false;
        const char* dll_name = reinterpret_cast<const char*>(img.raw.data() + name_raw);
        HMODULE mod = LoadLibraryA(dll_name);
        if (!mod) return false;

        uint32_t oft_raw = pe::rva_to_raw(img,
            desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk);
        if (!oft_raw) return false;
        auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA64*>(const_cast<uint8_t*>(img.raw.data()) + oft_raw);
        uintptr_t iat_va = remote_base + desc->FirstThunk;

        for (size_t i = 0; thunk[i].u1.AddressOfData; ++i) {
            uintptr_t resolved = 0;
            if (IMAGE_SNAP_BY_ORDINAL64(thunk[i].u1.Ordinal)) {
                resolved = (uintptr_t)GetProcAddress(
                    mod, MAKEINTRESOURCEA(IMAGE_ORDINAL64(thunk[i].u1.Ordinal)));
            } else {
                uint32_t by_raw = pe::rva_to_raw(img, (DWORD)thunk[i].u1.AddressOfData);
                if (!by_raw) return false;
                auto* by_name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                    const_cast<uint8_t*>(img.raw.data()) + by_raw);
                resolved = (uintptr_t)GetProcAddress(mod, by_name->Name);
            }
            if (!resolved) return false;
            if (!write_remote(proc, iat_va + i * sizeof(uintptr_t), &resolved, sizeof(resolved)))
                return false;
        }
    }
    return true;
}

static bool apply_protections(HANDLE proc, uintptr_t remote_base, const pe::Image& img) {
    auto s = img.sect();
    for (WORD i = 0; i < img.nt()->FileHeader.NumberOfSections; ++i, ++s) {
        DWORD prot = PAGE_NOACCESS;
        bool r = (s->Characteristics & IMAGE_SCN_MEM_READ)    != 0;
        bool w = (s->Characteristics & IMAGE_SCN_MEM_WRITE)   != 0;
        bool x = (s->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
        if      ( x &&  w) prot = PAGE_EXECUTE_READWRITE;
        else if ( x &&  r) prot = PAGE_EXECUTE_READ;
        else if ( x)       prot = PAGE_EXECUTE;
        else if ( w)       prot = PAGE_READWRITE;
        else if ( r)       prot = PAGE_READONLY;
        DWORD old = 0;
        SIZE_T sz = s->Misc.VirtualSize ? s->Misc.VirtualSize : s->SizeOfRawData;
        if (!VirtualProtectEx(proc, (LPVOID)(remote_base + s->VirtualAddress), sz, prot, &old))
            return false;
    }
    return true;
}

// launch DllMain in remote via CreateRemoteThread. shellcode sets up the x64
// caller-save frame, loads args (base, DLL_PROCESS_ATTACH, MM_MAGIC), calls the
// entry, then returns cleanly so the thread exits without an exception.
static bool call_entry(HANDLE proc, uintptr_t remote_base, uint64_t entry_rva) {
    uint8_t sc[] = {
        0x48,0x83,0xEC,0x28,                                   // sub  rsp, 0x28
        0x48,0xB9,0,0,0,0,0,0,0,0,                             // mov  rcx, base
        0xBA,0x01,0x00,0x00,0x00,                              // mov  edx, 1
        0x49,0xB8,0,0,0,0,0,0,0,0,                             // mov  r8,  MM_MAGIC
        0x48,0xB8,0,0,0,0,0,0,0,0,                             // mov  rax, entry
        0xFF,0xD0,                                             // call rax
        0x48,0x83,0xC4,0x28,                                   // add  rsp, 0x28
        0xC3                                                   // ret
    };
    *reinterpret_cast<uint64_t*>(sc + 6)  = remote_base;
    *reinterpret_cast<uint64_t*>(sc + 21) = MM_MAGIC;
    *reinterpret_cast<uint64_t*>(sc + 31) = remote_base + entry_rva;

    LPVOID shell = VirtualAllocEx(proc, nullptr, sizeof(sc),
                                  MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!shell) return false;
    if (!write_remote(proc, (uintptr_t)shell, sc, sizeof(sc))) return false;

    HANDLE th = CreateRemoteThread(proc, nullptr, 0,
                                   reinterpret_cast<LPTHREAD_START_ROUTINE>(shell),
                                   nullptr, 0, nullptr);
    if (!th) return false;
    WaitForSingleObject(th, INFINITE);
    DWORD exit_code = STILL_ACTIVE;
    GetExitCodeThread(th, &exit_code);
    g_last_exit = exit_code;
    CloseHandle(th);
    VirtualFreeEx(proc, shell, 0, MEM_RELEASE);
    return true;
}

uintptr_t inject(HANDLE proc, const pe::Image& img) {
    uintptr_t base = reinterpret_cast<uintptr_t>(VirtualAllocEx(
        proc, nullptr, img.image_size(),
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!base) return 0;
    if (!map_sections(proc, base, img))      return 0;
    if (!relocate(proc, base, img))          return 0;
    if (!resolve_imports(proc, base, img))   return 0;
    if (!apply_protections(proc, base, img)) return 0;
    if (!call_entry(proc, base, img.entry_rva())) return 0;
    return base;
}

}
