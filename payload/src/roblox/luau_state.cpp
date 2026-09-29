// luau_state.cpp — walk TaskScheduler → ScriptContext → global state pool
// language: C++20, target: Windows 11 x64, MSVC
//
// identity selection: Roblox stores per-thread capabilities in the ExtraSpace
// block. we pin identity 7 (RCC / elevated) because it's what LocalScripts
// running under a plugin context receive — same clearance every real executor
// runs against. any script we `luau_load` into it inherits the identity.
#include "roblox/luau_state.h"
#include "roblox/scheduler.h"
#include "roblox/offsets.h"
#include "pch.h"

namespace r9k::rbx {

namespace {
    std::atomic<lua_State*> g_main{nullptr};
    std::atomic<uptr>       g_sc{0};

    // pull ScriptContext out of the WaitingHybridScriptsJob (a job that owns
    // a strong pointer to it). alternative anchors: WaitingScriptsJob,
    // ModelMeshJob's dm ref — WaitingHybridScriptsJob is the most stable.
    uptr resolve_script_context() {
        uptr job = Scheduler::find_job("WaitingHybridScriptsJob");
        if (!job) return 0;
        return *reinterpret_cast<uptr*>(job + off::WAITJOB_SCRIPTCONTEXT);
    }

    // state pool: array of lua_State* indexed by identity (0..7).
    // pick the highest occupied identity so we always land on the elevated one.
    lua_State* pick_elevated(uptr sc) {
        auto pool = reinterpret_cast<lua_State**>(sc + off::SC_STATE_POOL);
        for (int i = static_cast<int>(off::SC_IDENTITY_MAX) - 1; i >= 0; --i) {
            if (pool[i]) return pool[i];
        }
        return nullptr;
    }
}

bool LuauState::bind() {
    if (g_main.load(std::memory_order_acquire)) return true;
    if (!Scheduler::bind()) return false;

    // ScriptContext appears late in load — poll for it. bounded to 30s so a
    // client that never reaches the DataModel doesn't leak a wait thread.
    uptr sc = 0;
    for (int i = 0; i < 300; ++i) {
        sc = resolve_script_context();
        if (sc) break;
        Sleep(100);
    }
    if (!sc) return false;

    lua_State* L = nullptr;
    for (int i = 0; i < 100; ++i) {
        L = pick_elevated(sc);
        if (L) break;
        Sleep(50);
    }
    if (!L) return false;

    g_sc.store(sc, std::memory_order_release);
    g_main.store(L, std::memory_order_release);
    return true;
}

lua_State* LuauState::main()          { return g_main.load(std::memory_order_acquire); }
uptr       LuauState::script_context(){ return g_sc.load(std::memory_order_acquire); }

// replace weak stub in runtime.cpp
void luau_state_bind() { LuauState::bind(); }

}
