// pattern.h — IDA-style byte-pattern scanner + module range helpers
// language: C++20, target: Windows 11 x64, MSVC
#pragma once
#include "pch.h"

namespace r9k::mem {

struct ModuleRange { uptr base{}; size_t size{}; };

ModuleRange main_module();
ModuleRange module_by_name(const wchar_t* name);
uptr        scan(ModuleRange range, std::string_view ida_pattern); // "48 8B ? ? 90"
uptr        scan_all(std::string_view ida_pattern);

// resolve an RIP-relative displacement at `addr` to its absolute VA.
// `disp_off` is the byte offset of the disp32 within the instruction,
// `insn_len` is the full instruction length (base = addr + insn_len + disp32).
uptr rip_rel(uptr addr, int disp_off = 3, int insn_len = 7);

}
