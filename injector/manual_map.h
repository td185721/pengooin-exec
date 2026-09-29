// manual_map.h — manual PE mapper; keeps payload off the PEB loader lists
// language: C++20, target: Windows 11 x64, MSVC
#pragma once
#include <windows.h>
#include "pe.h"

namespace mm {

// map `img` into the process referenced by `proc`, resolve imports + relocs,
// then invoke DllMain(base, DLL_PROCESS_ATTACH, MM_MAGIC) via a shellcode stub.
// returns remote base, or 0 on failure.
uintptr_t inject(HANDLE proc, const pe::Image& img);

// remote thread's exit code from the last call_entry inside inject().
// STILL_ACTIVE if no injection has completed. Read after inject() to
// distinguish "shellcode returned cleanly (0)" from "thread killed by
// unhandled exception (e.g. 0xC0000005 STATUS_ACCESS_VIOLATION)".
DWORD last_entry_exit_code();

// magic passed as Reserved so DllMain can distinguish manual-map from Win32 loader
constexpr uintptr_t MM_MAGIC = 0x9EEDECAFULL;

}
