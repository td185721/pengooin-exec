// luau_state.h — grab and cache the game's own lua_State
// language: C++20, target: Windows 11 x64, MSVC
#pragma once
#include "pch.h"

// forward-decl so this header can be included without Luau's own headers.
// impl file pulls in <lua.h> when it needs real access.
struct lua_State;

namespace r9k::rbx {

class LuauState {
public:
    // resolves ScriptContext from the WaitingHybridScriptsJob, then reads the
    // identity-7 (elevated) lua_State out of the state pool. must be called
    // after Scheduler::bind() and after the DataModel has loaded (i.e. after
    // the client is at least at the loading screen).
    static bool bind();

    // returns the cached elevated main thread, or nullptr.
    static lua_State* main();

    // returns the raw ScriptContext pointer for env code that needs to
    // fire signals via RBX internals (getconnections, firetouchinterest).
    static uptr script_context();
};

}
