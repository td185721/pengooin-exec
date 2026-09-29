// runtime.cpp — top-level boot: wait for the client to finish init, then wire up
// language: C++20, target: Windows 11 x64, MSVC
#include "pch.h"
#include "memory/pattern.h"

namespace r9k {

// forward — each phase fills in its own translation unit
void scheduler_bind();   // src/roblox/scheduler.cpp    (phase 2)
void luau_state_bind();  // src/roblox/luau_state.cpp   (phase 2)
void compiler_init();    // src/luau/compiler_bridge.cpp (phase 3)
void env_install();      // src/env/environment.cpp     (phase 4+)

void runtime_boot() {
    // warmup: main module + all imports need to be settled before we scan.
    // two seconds is enough on every SKU tested; longer is fine because the
    // player isn't in-game yet when the DLL lands via a pre-launch inject.
    Sleep(2000);

    auto main = mem::main_module();
    if (!main.base) return;

    scheduler_bind();
    luau_state_bind();
    compiler_init();
    env_install();
}

// weak stubs so the payload links until each phase drops its real symbol.
// MSVC uses /alternatename to route the missing decorated name to the stub.
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
