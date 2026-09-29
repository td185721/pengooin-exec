// pattern.cpp — IDA-style scanner. one pass, no allocations after parse.
// language: C++20, target: Windows 11 x64, MSVC
#include "memory/pattern.h"

namespace r9k::mem {

ModuleRange main_module() {
    HMODULE m = GetModuleHandleW(nullptr);
    if (!m) return {};
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(m);
    auto nt  = reinterpret_cast<IMAGE_NT_HEADERS64*>((u8*)m + dos->e_lfanew);
    return { reinterpret_cast<uptr>(m), nt->OptionalHeader.SizeOfImage };
}

ModuleRange module_by_name(const wchar_t* name) {
    HMODULE m = GetModuleHandleW(name);
    if (!m) return {};
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(m);
    auto nt  = reinterpret_cast<IMAGE_NT_HEADERS64*>((u8*)m + dos->e_lfanew);
    return { reinterpret_cast<uptr>(m), nt->OptionalHeader.SizeOfImage };
}

namespace {
struct Token { u8 val; bool wild; };

std::vector<Token> parse(std::string_view s) {
    std::vector<Token> out;
    for (size_t i = 0; i < s.size(); ) {
        while (i < s.size() && s[i] == ' ') ++i;
        if (i >= s.size()) break;
        if (s[i] == '?') {
            out.push_back({0, true});
            ++i;
            if (i < s.size() && s[i] == '?') ++i;
        } else {
            u8 v = 0;
            for (int k = 0; k < 2 && i < s.size(); ++k, ++i) {
                char c = s[i];
                u8 d = (c >= '0' && c <= '9') ? u8(c - '0')
                     : (c >= 'a' && c <= 'f') ? u8(c - 'a' + 10)
                     : (c >= 'A' && c <= 'F') ? u8(c - 'A' + 10) : 0;
                v = u8((v << 4) | d);
            }
            out.push_back({ v, false });
        }
    }
    return out;
}
}

uptr scan(ModuleRange range, std::string_view ida) {
    if (!range.base || !range.size) return 0;
    auto pat = parse(ida);
    if (pat.empty()) return 0;
    auto* base = reinterpret_cast<const u8*>(range.base);
    const size_t last = range.size - pat.size();
    for (size_t i = 0; i <= last; ++i) {
        bool ok = true;
        for (size_t j = 0; j < pat.size(); ++j)
            if (!pat[j].wild && base[i + j] != pat[j].val) { ok = false; break; }
        if (ok) return reinterpret_cast<uptr>(base + i);
    }
    return 0;
}

uptr scan_all(std::string_view ida) { return scan(main_module(), ida); }

uptr rip_rel(uptr addr, int disp_off, int insn_len) {
    if (!addr) return 0;
    int32_t disp = *reinterpret_cast<int32_t*>(addr + disp_off);
    return addr + insn_len + disp;
}

}
