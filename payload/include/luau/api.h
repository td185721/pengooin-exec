// api.h — jump table into the game's own Luau C API
// language: C++20, target: Windows 11 x64, MSVC
//
// we cannot statically link Luau.VM into the payload — the game's VM owns the
// lua_State*, so every C API call must land in the game's copy. we sig-scan
// the anchor functions once at boot; the rest resolve as adjacent RVAs from
// the anchor (Luau functions statically cluster in a single .text region).
//
// after every Roblox client patch: re-run the sig hunt in IDA, update
// SIG_* macros in offsets.h, ANCHOR_* offsets here.
#pragma once
#include "pch.h"

struct lua_State;
typedef int (*lua_CFunction)(lua_State* L);
typedef struct luaL_Reg { const char* name; lua_CFunction func; } luaL_Reg;

namespace r9k::luau {

// resolved function pointers to the game's Luau C API.
// pinned once at boot; safe to snapshot into locals in hot loops.
struct API {
    // load / execute
    int  (*load)     (lua_State*, const char* chunk, const char* data, size_t size, int env);
    int  (*pcall)    (lua_State*, int nargs, int nresults, int errfunc);
    int  (*resume)   (lua_State*, lua_State* from, int narg);
    void (*call)     (lua_State*, int nargs, int nresults);

    // stack
    int  (*gettop)   (lua_State*);
    void (*settop)   (lua_State*, int idx);
    void (*pushvalue)(lua_State*, int idx);
    void (*remove)   (lua_State*, int idx);
    void (*insert)   (lua_State*, int idx);
    void (*replace)  (lua_State*, int idx);
    int  (*checkstack)(lua_State*, int extra);

    // typing
    int  (*type)     (lua_State*, int idx);
    const char* (*typename_)(lua_State*, int t);

    // push
    void (*pushnil)     (lua_State*);
    void (*pushboolean) (lua_State*, int b);
    void (*pushnumber)  (lua_State*, double n);
    void (*pushinteger) (lua_State*, int n);
    void (*pushlstring) (lua_State*, const char* s, size_t l);
    void (*pushstring)  (lua_State*, const char* s);
    void (*pushcclosure)(lua_State*, lua_CFunction fn, const char* debugname, int nup);
    void (*pushlightuserdata)(lua_State*, void* p);

    // convert
    const char* (*tolstring)(lua_State*, int idx, size_t* len);
    double      (*tonumberx)(lua_State*, int idx, int* isnum);
    int         (*tointegerx)(lua_State*, int idx, int* isnum);
    int         (*toboolean)(lua_State*, int idx);
    void*       (*touserdata)(lua_State*, int idx);
    lua_CFunction (*tocfunction)(lua_State*, int idx);

    // tables
    void (*createtable)(lua_State*, int narr, int nrec);
    void (*getfield)   (lua_State*, int idx, const char* k);
    void (*setfield)   (lua_State*, int idx, const char* k);
    void (*gettable)   (lua_State*, int idx);
    void (*settable)   (lua_State*, int idx);
    void (*rawget)     (lua_State*, int idx);
    void (*rawset)     (lua_State*, int idx);
    void (*rawgeti)    (lua_State*, int idx, int n);
    void (*rawseti)    (lua_State*, int idx, int n);
    int  (*next)       (lua_State*, int idx);
    size_t (*objlen)   (lua_State*, int idx);

    // metatables
    int  (*getmetatable)(lua_State*, int idx);
    int  (*setmetatable)(lua_State*, int idx);
    void (*getfenv)    (lua_State*, int idx);
    int  (*setfenv)    (lua_State*, int idx);

    // refs (luaL_ref-style, used for callback storage)
    int  (*ref)        (lua_State*, int t);
    void (*unref)      (lua_State*, int t, int ref);
    void (*getref)     (lua_State*, int ref);

    // threads
    lua_State* (*newthread)(lua_State*);
    lua_State* (*mainthread)(lua_State*);

    // errors
    int  (*error)      (lua_State*);
    void (*pushfstring)(lua_State*, const char* fmt, ...);

    // globals convenience — Luau uses LUA_GLOBALSINDEX for the global env
    static constexpr int GLOBALSINDEX = -10002;
    static constexpr int REGISTRYINDEX = -10000;
};

// resolves the whole table via sig scan. returns false if any anchor missed.
bool api_bind();

// returns the resolved API. undefined behavior if called before api_bind() ok.
const API& api();

}
