// environment.h — sandbox env table + registration helpers
// language: C++20, target: Windows 11 x64, MSVC
#pragma once
#include "pch.h"

struct lua_State;
typedef int (*lua_CFunction)(lua_State* L);

namespace r9k::env {

// install the r9k sandbox env into REGISTRYINDEX[__r9k_env]. every user
// script's fenv is set to this table before resume. env includes all sUNC
// globals mixed with a __index fallback to the real _G so unmodified code
// keeps working.
void install();

// register a single C function under the r9k env. call anytime after install.
void register_fn(const char* name, lua_CFunction fn);

// register a library (table of name→fn) under `libname` inside the env.
void register_lib(const char* libname, std::initializer_list<std::pair<const char*, lua_CFunction>> fns);

// mark a lua_State as "r9k-authored" — checkcaller returns true for threads
// carrying this tag. every executed script's thread + descendants get flagged.
void  tag_thread(lua_State* L);
bool  is_r9k_thread(lua_State* L);

// closure origin tag — used by iscclosure/islclosure/newcclosure identity.
void  tag_closure(lua_State* L, int idx);
bool  is_r9k_closure(lua_State* L, int idx);

}
