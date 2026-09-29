// internal.h — Luau internal struct layouts (Closure, Proto, Table, TValue)
// language: C++20, target: Windows 11 x64, MSVC
//
// mirrors Roblox/luau HEAD layout — 3-byte GCheader (tt/marked/memcat),
// isC at +0x03, nupvalues at +0x04, env at +0x10, c/l union at +0x18.
// re-verify after every Luau bump the Roblox client picks up.
#pragma once
#include "pch.h"

namespace r9k::luau {

// GCObject base — every GC-tracked value starts with this header
struct GCHeader { u8 tt; u8 marked; u8 memcat; };

// TValue — Luau tagged value (16 bytes, matches lua_Object)
struct TValue {
    u64 value;
    u32 extra[2];
    u32 tt;
};

// Table (Luau)
struct Table {
    GCHeader hdr;
    u8  flags;
    i32 readonly : 1;
    i32 safeenv  : 1;
    i32 sizearray;
    i32 nodemask8;
    // ... more fields; we only touch flags/readonly here
};

// Proto (Luau) — function prototype for L closures
struct Proto {
    GCHeader hdr;
    u8       nups;
    u8       numparams;
    u8       is_vararg;
    u8       maxstacksize;
    u8       flags;
    u8       _pad[2];
    void*    code;
    void*    p;
    void*    k;
    void*    lineinfo;
    void*    abslineinfo;
    void*    locvars;
    void*    upvalues;
    void*    source;
    // ... more; offsets pinned above only
};

// Closure (Luau)
// c-closure: f, cont, debugname, upvals[nupvalues]
// l-closure: p (Proto*), uprefs[nupvalues]
struct Closure {
    GCHeader hdr;         // +0x00
    u8       isC;         // +0x03
    u8       nupvalues;   // +0x04
    u8       stacksize;   // +0x05
    u8       preload;     // +0x06
    u8       _pad;        // +0x07
    void*    gclist;      // +0x08
    Table*   env;         // +0x10
    union {
        struct {
            void*       f;         // lua_CFunction
            void*       cont;      // continuation
            const char* debugname;
            TValue      upvals[1]; // flexible
        } c;
        struct {
            Proto*      p;
            TValue      uprefs[1]; // flexible
        } l;
    } u;                  // +0x18
};

// Luau constant types (matches ltm.h / lobject.h)
constexpr int LUA_TNIL           = 0;
constexpr int LUA_TBOOLEAN       = 1;
constexpr int LUA_TLIGHTUSERDATA = 2;
constexpr int LUA_TNUMBER        = 3;
constexpr int LUA_TVECTOR        = 4;
constexpr int LUA_TSTRING        = 5;
constexpr int LUA_TTABLE         = 6;
constexpr int LUA_TFUNCTION      = 7;
constexpr int LUA_TUSERDATA      = 8;
constexpr int LUA_TTHREAD        = 9;
constexpr int LUA_TBUFFER        = 10;

// Luau pcall / resume result codes
constexpr int LUA_OK        = 0;
constexpr int LUA_YIELD     = 1;
constexpr int LUA_ERRRUN    = 2;
constexpr int LUA_ERRSYNTAX = 3;
constexpr int LUA_ERRMEM    = 4;
constexpr int LUA_ERRERR    = 5;

// registry indexing — used across env code
constexpr int LUA_GLOBALSINDEX     = -10002;
constexpr int LUA_REGISTRYINDEX    = -10000;
constexpr int LUA_ENVIRONINDEX     = -10001;

// helper: convert TValue at stack index → Closure*. Roblox stack layout stores
// TValue.value as GCObject* when tt is TFUNCTION; we cross-check tt first.
inline Closure* tovalue_closure(const TValue* tv) {
    if (!tv || tv->tt != LUA_TFUNCTION) return nullptr;
    return reinterpret_cast<Closure*>(tv->value);
}

}
