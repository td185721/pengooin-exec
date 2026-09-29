// pe.h — PE64 header helpers for the manual mapper
// language: C++20, target: Windows 11 x64, MSVC
#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>
#include <string>

namespace pe {

struct Image {
    std::vector<uint8_t> raw;
    IMAGE_DOS_HEADER*     dos()  const { return (IMAGE_DOS_HEADER*)raw.data(); }
    IMAGE_NT_HEADERS64*   nt()   const { return (IMAGE_NT_HEADERS64*)(raw.data() + dos()->e_lfanew); }
    IMAGE_SECTION_HEADER* sect() const { return IMAGE_FIRST_SECTION(nt()); }
    uint32_t              image_size()   const { return nt()->OptionalHeader.SizeOfImage; }
    uint32_t              headers_size() const { return nt()->OptionalHeader.SizeOfHeaders; }
    uint64_t              entry_rva()    const { return nt()->OptionalHeader.AddressOfEntryPoint; }
};

bool load(const std::wstring& path, Image& out);
bool valid_pe(const Image& img);

// helper: resolve an RVA to an offset within the on-disk file image
uint32_t rva_to_raw(const Image& img, uint32_t rva);

}
