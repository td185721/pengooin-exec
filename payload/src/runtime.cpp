// runtime.cpp — top-level boot chain
// language: C++20, target: Windows 11 x64, MSVC
#include "pch.h"
#include "memory/pattern.h"
#include "luau/api.h"
#include "ui/shell.h"

namespace r9k {

// each phase drops its real impl in its own TU; weak stubs below cover the
// gap until then so the payload keeps linking phase by phase.
void scheduler_bind();
void luau_state_bind();
void compiler_init();
void env_install();

void runtime_boot() {
    // warmup: wait for the main module + imports to be fully mapped and for
    // Hyperion (when present) to finish its own initialization. two seconds
    // is enough on every SKU tested; a proper build gates this on the DataModel
    // reaching Running instead of a sleep.
    Sleep(2000);

    auto main = mem::main_module();
    if (!main.base) return;

    scheduler_bind();       // phase 2 — TaskScheduler singleton
    luau_state_bind();      // phase 2 — elevated lua_State
    luau::api_bind();       // phase 3 — Luau C API jump table
    compiler_init();        // phase 3 — Luau.Compiler
    env_install();          // phase 4-11 — sUNC/UNC surface
    ui::shell_start();      // phase 12 — native Win32 shell
}

// weak stubs — /alternatename redirects any unresolved decorated symbol to the
// matching _stub. once a phase's TU is included in the build, its own symbol
// wins the linker's tie-break and the stub falls out.
#if defined(_MSC_VER)
#pragma comment(linker, "/alternatename:?scheduler_bind@r9k@@YAXXZ=?scheduler_bind_stub@r9k@@YAXXZ")
#pragma comment(linker, "/alternatename:?luau_state_bind@r9k@@YAXXZ=?luau_state_bind_stub@r9k@@YAXXZ")
#pragma comment(linker, "/alternatename:?compiler_init@r9k@@YAXXZ=?compiler_init_stub@r9k@@YAXXZ")
#pragma comment(linker, "/alternatename:?env_install@r9k@@YAXXZ=?env_install_stub@r9k@@YAXXZ")
#endif
void scheduler_bind_stub()  {}
void luau_state_bind_stub() {}
void compiler_init_stub()   {}
void env_install_stub()     {}

}
