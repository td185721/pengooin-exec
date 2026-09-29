// pe.cpp — read DLL from disk, validate PE64, translate RVAs
// language: C++20, target: Windows 11 x64, MSVC
#include "pe.h"
#include <fstream>

namespace pe {

bool load(const std::wstring& path, Image& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    auto size = f.tellg();
    f.seekg(0);
    out.raw.resize(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(out.raw.data()), size);
    return valid_pe(out);
}

bool valid_pe(const Image& img) {
    if (img.raw.size() < sizeof(IMAGE_DOS_HEADER)) return false;
    if (img.dos()->e_magic != IMAGE_DOS_SIGNATURE) return false;
    if (img.raw.size() < static_cast<size_t>(img.dos()->e_lfanew) + sizeof(IMAGE_NT_HEADERS64)) return false;
    if (img.nt()->Signature != IMAGE_NT_SIGNATURE) return false;
    if (img.nt()->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) return false;
    return true;
}

uint32_t rva_to_raw(const Image& img, uint32_t rva) {
    auto s = img.sect();
    for (WORD i = 0; i < img.nt()->FileHeader.NumberOfSections; ++i, ++s) {
        if (rva >= s->VirtualAddress && rva < s->VirtualAddress + s->Misc.VirtualSize)
            return s->PointerToRawData + (rva - s->VirtualAddress);
    }
    return 0;
}

}
