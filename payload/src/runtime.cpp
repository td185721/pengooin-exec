// runtime.cpp — top-level boot chain
// language: C++20, target: Windows 11 x64, MSVC
#include "pch.h"
#include "memory/pattern.h"
#include "luau/api.h"
#include "ui/shell.h"
#include "net/pipe_client.h"

namespace r9k {

// each phase drops its real impl in its own TU; weak stubs below cover the
// gap until then so the payload keeps linking phase by phase.
void scheduler_bind();
void luau_state_bind();
void compiler_init();
void env_install();

void runtime_boot() {
    dbg_log("runtime_boot: entry");

    // pipe client goes FIRST, before the fragile bind chain. this way even if
    // sig scans fail on a new client patch the watcher still sees a connected
    // payload (with luau::run reporting "lua_State not bound" per exec) and
    // we can debug the rest from console output.
    net::pipe_client_start();
    dbg_log("runtime_boot: pipe client kicked");

    // warmup: wait for the main module + imports to be fully mapped and for
    // Hyperion (when present) to finish its own initialization.
    Sleep(2000);
    dbg_log("runtime_boot: warmup done");

    auto main = mem::main_module();
    if (!main.base) { dbg_log("runtime_boot: main_module null, abort"); return; }
    dbg_log("runtime_boot: main module @ 0x%016llX size=0x%x",
            (unsigned long long)main.base, (unsigned)main.size);

    scheduler_bind();      dbg_log("runtime_boot: scheduler_bind done");
    luau_state_bind();     dbg_log("runtime_boot: luau_state_bind done");
    luau::api_bind();      dbg_log("runtime_boot: luau::api_bind done");
    compiler_init();       dbg_log("runtime_boot: compiler_init done");
    env_install();         dbg_log("runtime_boot: env_install done");
    ui::shell_start();     dbg_log("runtime_boot: shell_start done");
    dbg_log("runtime_boot: chain complete");
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
