// executor.cpp — compile → load → thread → resume pipeline
// language: C++20, target: Windows 11 x64, MSVC
//
// - always spawn a fresh coroutine off the elevated main state. keeps
//   ScriptContext's book-keeping happy and gives every user script its own
//   error/return channel.
// - env sandbox pinned at REGISTRYINDEX[R9K_ENV_KEY] gets applied here once
//   phase 4 lands `env_install()`. until then, scripts inherit main's globals.
// - Luau::compile embeds syntax errors in-band (first byte 0 + message);
//   luau_load returns non-zero and pushes the message either way.
#include "luau/executor.h"
#include "luau/compiler_bridge.h"
#include "luau/api.h"
#include "roblox/luau_state.h"

namespace r9k::luau {

namespace {
    // Luau constants — kept local to avoid pulling <lua.h> into every TU
    constexpr int LUA_TTABLE = 5;
    constexpr int LUA_YIELD  = 1;

    constexpr const char* R9K_ENV_KEY = "__r9k_env";
}

RunResult run(std::string_view source, const char* chunk_name) {
    RunResult r;
    const auto& a = api();

    lua_State* L = rbx::LuauState::main();
    if (!L) { r.error = "lua_State not bound"; return r; }

    std::string bc = compile(source);

    lua_State* T = a.newthread(L);
    if (!T) { r.error = "newthread failed"; return r; }

    // load chunk directly on the new thread's stack
    int lr = a.load(T, chunk_name, bc.data(), bc.size(), 0);
    if (lr != 0) {
        size_t len = 0;
        const char* msg = a.tolstring(T, -1, &len);
        r.error.assign(msg ? msg : "load error", len);
        a.settop(L, -2);  // drop thread from L
        return r;
    }

    // apply r9k sandbox env if it's been installed (phase 4+).
    // if not present, the chunk keeps main's globals — fine for early phases.
    a.getfield(L, API::REGISTRYINDEX, R9K_ENV_KEY);
    if (a.type(L, -1) == LUA_TTABLE) {
        // duplicate env onto T (rawgeti via ref would be cleaner; xmove lands
        // when we sig-scan it in phase 5's debug library work).
        a.pushvalue(L, -1);
        a.setfenv(T, -2);   // setfenv on chunk sitting at T top-1 after xmove
    }
    a.settop(L, -2);        // discard env / nil probe from L

    int rr = a.resume(T, L, 0);
    if (rr != 0 && rr != LUA_YIELD) {
        size_t len = 0;
        const char* msg = a.tolstring(T, -1, &len);
        r.error.assign(msg ? msg : "runtime error", len);
        return r;
    }

    r.ok = true;
    return r;
}

}
