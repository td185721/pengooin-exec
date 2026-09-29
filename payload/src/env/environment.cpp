// environment.cpp — install the r9k sandbox env + track r9k-authored objects
// language: C++20, target: Windows 11 x64, MSVC
//
// registry layout after install():
//   __r9k_env      → env table (user-script fenv)
//   __r9k_threads  → weak-keyed table {thread → true} of r9k-authored threads
//   __r9k_closures → weak-keyed table {closure → true} of r9k-authored closures
//
// __index on env falls through to _G so untouched globals resolve normally.
// __newindex writes stay local to the env — user scripts can shadow but not
// pollute the real _G.
#include "env/environment.h"
#include "luau/api.h"
#include "luau/internal.h"
#include "roblox/luau_state.h"

namespace r9k::env {

namespace {
    constexpr const char* KEY_ENV      = "__r9k_env";
    constexpr const char* KEY_THREADS  = "__r9k_threads";
    constexpr const char* KEY_CLOSURES = "__r9k_closures";

    // create a weak-keyed table on top of stack: {mode="k"} in the metatable
    void push_weak_ktable(lua_State* L) {
        const auto& a = luau::api();
        a.createtable(L, 0, 0);
        a.createtable(L, 0, 1);
        a.pushstring(L, "k");
        a.setfield(L, -2, "__mode");
        a.setmetatable(L, -2);
    }

    // fenv __index fallback → real _G
    int env_index(lua_State* L) {
        const auto& a = luau::api();
        // args: env, key
        a.pushvalue(L, 2);
        a.gettable(L, luau::LUA_GLOBALSINDEX);
        return 1;
    }
}

void install() {
    const auto& a = luau::api();
    lua_State* L = rbx::LuauState::main();
    if (!L) return;

    // env table
    a.createtable(L, 0, 128);
    // metatable with __index = _G fallback
    a.createtable(L, 0, 1);
    a.pushcclosure(L, env_index, "r9k.env.__index", 0);
    a.setfield(L, -2, "__index");
    a.setmetatable(L, -2);
    a.setfield(L, luau::LUA_REGISTRYINDEX, KEY_ENV);

    // threads weak table
    push_weak_ktable(L);
    a.setfield(L, luau::LUA_REGISTRYINDEX, KEY_THREADS);

    // closures weak table
    push_weak_ktable(L);
    a.setfield(L, luau::LUA_REGISTRYINDEX, KEY_CLOSURES);
}

void register_fn(const char* name, lua_CFunction fn) {
    const auto& a = luau::api();
    lua_State* L = rbx::LuauState::main();
    if (!L) return;

    a.getfield(L, luau::LUA_REGISTRYINDEX, KEY_ENV);
    if (a.type(L, -1) != luau::LUA_TTABLE) { a.settop(L, -2); return; }
    a.pushcclosure(L, fn, name, 0);
    // tag as r9k closure by inserting into the weak set
    a.getfield(L, luau::LUA_REGISTRYINDEX, KEY_CLOSURES);
    a.pushvalue(L, -2);            // dup the closure
    a.pushboolean(L, 1);
    a.rawset(L, -3);               // closures[closure] = true
    a.settop(L, -2);               // drop closures table
    a.setfield(L, -2, name);       // env[name] = closure
    a.settop(L, -2);               // drop env
}

void register_lib(const char* libname,
                  std::initializer_list<std::pair<const char*, lua_CFunction>> fns) {
    const auto& a = luau::api();
    lua_State* L = rbx::LuauState::main();
    if (!L) return;

    a.getfield(L, luau::LUA_REGISTRYINDEX, KEY_ENV);
    if (a.type(L, -1) != luau::LUA_TTABLE) { a.settop(L, -2); return; }

    a.createtable(L, 0, (int)fns.size());
    for (auto& [n, f] : fns) {
        a.pushcclosure(L, f, n, 0);
        a.getfield(L, luau::LUA_REGISTRYINDEX, KEY_CLOSURES);
        a.pushvalue(L, -2);
        a.pushboolean(L, 1);
        a.rawset(L, -3);
        a.settop(L, -2);
        a.setfield(L, -2, n);
    }
    a.setfield(L, -2, libname);
    a.settop(L, -2);
}

void tag_thread(lua_State* L) {
    const auto& a = luau::api();
    a.getfield(L, luau::LUA_REGISTRYINDEX, KEY_THREADS);
    if (a.type(L, -1) != luau::LUA_TTABLE) { a.settop(L, -2); return; }
    a.pushlightuserdata(L, L);       // key is thread's own pointer
    a.pushboolean(L, 1);
    a.rawset(L, -3);
    a.settop(L, -2);
}

bool is_r9k_thread(lua_State* L) {
    const auto& a = luau::api();
    a.getfield(L, luau::LUA_REGISTRYINDEX, KEY_THREADS);
    if (a.type(L, -1) != luau::LUA_TTABLE) { a.settop(L, -2); return false; }
    a.pushlightuserdata(L, L);
    a.rawget(L, -2);
    bool ok = a.toboolean(L, -1) != 0;
    a.settop(L, -3);
    return ok;
}

void tag_closure(lua_State* L, int idx) {
    const auto& a = luau::api();
    a.pushvalue(L, idx);
    a.getfield(L, luau::LUA_REGISTRYINDEX, KEY_CLOSURES);
    a.pushvalue(L, -2);
    a.pushboolean(L, 1);
    a.rawset(L, -3);
    a.settop(L, -3);
}

bool is_r9k_closure(lua_State* L, int idx) {
    const auto& a = luau::api();
    a.pushvalue(L, idx);
    a.getfield(L, luau::LUA_REGISTRYINDEX, KEY_CLOSURES);
    a.pushvalue(L, -2);
    a.rawget(L, -2);
    bool ok = a.toboolean(L, -1) != 0;
    a.settop(L, -4);
    return ok;
}

// forward declare per-phase installers so env_install can call them all
namespace closure_lib  { void install(); }
namespace debug_lib    { void install(); }
namespace instance_lib { void install(); }
namespace fs_lib       { void install(); }
namespace net_lib      { void install(); }
namespace crypt_lib    { void install(); }
namespace input_lib    { void install(); }

}  // namespace r9k::env

namespace r9k {
// replace weak stub in runtime.cpp — matches decorated name /alternatename maps
void env_install() {
    env::install();
    env::closure_lib::install();
    env::debug_lib::install();
    env::instance_lib::install();
    env::fs_lib::install();
    env::net_lib::install();
    env::crypt_lib::install();
    env::input_lib::install();
    // drawing_lib, ui_lib — appended as each phase lands.
}
}
