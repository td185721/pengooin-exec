// api.cpp — sig-scan the game's Luau C API and populate the jump table
// language: C++20, target: Windows 11 x64, MSVC
//
// strategy: anchor on a small set of functions with distinctive prologues,
// then trust that Luau functions cluster tightly enough that follow-up hunts
// stay local. we scan each key one independently to keep failures granular —
// missing pushvalue shouldn't hide a missing pcall.
//
// sig format: IDA-style hex + '?' wildcards. every entry is version-locked;
// see offsets.h for the derivation-per-patch note.
#include "luau/api.h"
#include "memory/pattern.h"

namespace r9k::luau {

namespace {
    API g_api{};

    // signature table. tuned for a RobloxPlayerBeta build near late-2025.
    // each entry: {name, IDA-pattern}. re-derive after every client patch.
    struct Sig { const char* name; std::string_view pat; };

    #define S(name, pat) constexpr std::string_view SIG_##name = pat

    // load & execute
    S(luau_load,     "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 30 49 8B F1 41 8B F8");
    S(lua_pcall,     "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 41 8B F8 44 8B C2");
    S(lua_resume,    "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 ?");
    S(lua_call,      "48 89 5C 24 ? 57 48 83 EC 30 8B FA 48 8B D9 8B D2");

    // stack
    S(lua_gettop,    "48 8B 41 20 48 2B 41 18 48 C1 F8 04 C3");
    S(lua_settop,    "48 89 5C 24 ? 57 48 83 EC 20 48 8B D9 85 D2");
    S(lua_pushvalue, "48 89 5C 24 ? 57 48 83 EC 20 48 8B F9 E8 ? ? ? ?");
    S(lua_remove,    "48 89 5C 24 ? 57 48 83 EC 20 48 8B F9 E8 ? ? ? ? 48 8B D8");
    S(lua_insert,    "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B F1 8B DA");
    S(lua_replace,   "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B F1 41 8B F8");
    S(lua_checkstack,"48 83 EC 28 48 8B 41 20 48 8B 49 18 48 2B C1");

    // typing / conversion
    S(lua_type,      "48 89 5C 24 ? 57 48 83 EC 20 48 8B D9 E8 ? ? ? ? 8B 48 08");
    S(lua_typename,  "48 8B 41 20 48 89 4C 24 ? 48 83 EC 28");
    S(lua_tolstring, "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 49 8B F0 8B FA");
    S(lua_tonumberx, "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 49 8B F8 8B DA");
    S(lua_tointegerx,"48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 30 49 8B F8 8B FA");
    S(lua_toboolean, "48 89 5C 24 ? 57 48 83 EC 20 8B DA 48 8B F9");
    S(lua_touserdata,"48 89 5C 24 ? 57 48 83 EC 20 8B DA 48 8B F9 E8 ? ? ? ?");
    S(lua_tocfunction,"48 89 5C 24 ? 57 48 83 EC 20 8B FA 48 8B D9 E8 ? ? ? ?");

    // push
    S(lua_pushnil,       "48 8B 41 20 48 83 C0 10 48 89 41 20 C7 40 F0 00 00 00 00");
    S(lua_pushboolean,   "48 89 5C 24 ? 57 48 83 EC 20 8B FA 48 8B D9");
    S(lua_pushnumber,    "48 89 5C 24 ? 57 48 83 EC 20 48 8B F9 F2 0F 11 44 24 ?");
    S(lua_pushinteger,   "48 89 5C 24 ? 57 48 83 EC 20 8B FA 48 8B D9");
    S(lua_pushlstring,   "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 40 49 8B F0 48 8B FA");
    S(lua_pushstring,    "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B FA 48 8B D9");
    S(lua_pushcclosurek, "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 40 41 8B F0 48 8B FA");
    S(lua_pushlightuserdata,"48 89 5C 24 ? 57 48 83 EC 20 48 8B FA 48 8B D9");

    // tables
    S(lua_createtable, "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 41 8B F0 8B DA");
    S(lua_getfield,    "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 30 49 8B F8 8B DA");
    S(lua_setfield,    "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 30 49 8B F8 8B DA");
    S(lua_gettable,    "48 89 5C 24 ? 57 48 83 EC 30 8B FA 48 8B D9");
    S(lua_settable,    "48 89 5C 24 ? 57 48 83 EC 30 8B FA 48 8B D9");
    S(lua_rawget,      "48 89 5C 24 ? 57 48 83 EC 30 8B FA 48 8B D9 E8 ? ? ? ?");
    S(lua_rawset,      "48 89 5C 24 ? 57 48 83 EC 20 8B FA 48 8B D9");
    S(lua_rawgeti,     "48 89 5C 24 ? 57 48 83 EC 20 41 8B F8 8B DA");
    S(lua_rawseti,     "48 89 5C 24 ? 57 48 83 EC 20 41 8B F8 8B DA");
    S(lua_next,        "48 89 5C 24 ? 57 48 83 EC 20 8B FA 48 8B D9");
    S(lua_objlen,      "48 89 5C 24 ? 57 48 83 EC 20 8B FA 48 8B D9 E8 ? ? ? ?");

    // metatables / envs
    S(lua_getmetatable,"48 89 5C 24 ? 57 48 83 EC 20 8B FA 48 8B D9 E8 ? ? ? ? 48 85 C0 74 ?");
    S(lua_setmetatable,"48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 8B DA 48 8B F1");
    S(lua_getfenv,     "48 89 5C 24 ? 57 48 83 EC 20 8B FA 48 8B D9 E8 ? ? ? ? 48 8B 40 10");
    S(lua_setfenv,     "48 89 5C 24 ? 57 48 83 EC 20 8B FA 48 8B D9 E8 ? ? ? ? 48 85 C0");

    // refs
    S(lua_ref,     "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 8B FA 48 8B F1");
    S(lua_unref,   "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 41 8B F8 8B DA");
    S(lua_getref,  "48 89 5C 24 ? 57 48 83 EC 20 8B FA 48 8B D9 E8 ? ? ? ? 48 8B D8");

    // threads
    S(lua_newthread,   "48 89 5C 24 ? 57 48 83 EC 20 48 8B D9 E8 ? ? ? ? 48 8B F8");
    S(lua_mainthread,  "48 8B 41 60 C3");

    // errors
    S(lua_error,       "48 89 5C 24 ? 57 48 83 EC 20 48 8B D9 E8 ? ? ? ? CC");
    S(lua_pushfstring, "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 30 4C 8B C2 48 8B F9");

    #undef S

    template<class Fn>
    Fn bind_sig(std::string_view sig) {
        return reinterpret_cast<Fn>(mem::scan_all(sig));
    }
}

bool api_bind() {
    #define B(field, sig_name, fn_t) \
        g_api.field = bind_sig<decltype(g_api.field)>(SIG_##sig_name); \
        if (!g_api.field) return false;

    B(load,        luau_load,     void*);
    B(pcall,       lua_pcall,     void*);
    B(resume,      lua_resume,    void*);
    B(call,        lua_call,      void*);

    B(gettop,      lua_gettop,    void*);
    B(settop,      lua_settop,    void*);
    B(pushvalue,   lua_pushvalue, void*);
    B(remove,      lua_remove,    void*);
    B(insert,      lua_insert,    void*);
    B(replace,     lua_replace,   void*);
    B(checkstack,  lua_checkstack,void*);

    B(type,        lua_type,      void*);
    B(typename_,   lua_typename,  void*);
    B(tolstring,   lua_tolstring, void*);
    B(tonumberx,   lua_tonumberx, void*);
    B(tointegerx,  lua_tointegerx,void*);
    B(toboolean,   lua_toboolean, void*);
    B(touserdata,  lua_touserdata,void*);
    B(tocfunction, lua_tocfunction,void*);

    B(pushnil,     lua_pushnil,   void*);
    B(pushboolean, lua_pushboolean,void*);
    B(pushnumber,  lua_pushnumber,void*);
    B(pushinteger, lua_pushinteger,void*);
    B(pushlstring, lua_pushlstring,void*);
    B(pushstring,  lua_pushstring, void*);
    B(pushcclosure,lua_pushcclosurek,void*);
    B(pushlightuserdata, lua_pushlightuserdata, void*);

    B(createtable, lua_createtable,void*);
    B(getfield,    lua_getfield,   void*);
    B(setfield,    lua_setfield,   void*);
    B(gettable,    lua_gettable,   void*);
    B(settable,    lua_settable,   void*);
    B(rawget,      lua_rawget,     void*);
    B(rawset,      lua_rawset,     void*);
    B(rawgeti,     lua_rawgeti,    void*);
    B(rawseti,     lua_rawseti,    void*);
    B(next,        lua_next,       void*);
    B(objlen,      lua_objlen,     void*);

    B(getmetatable,lua_getmetatable,void*);
    B(setmetatable,lua_setmetatable,void*);
    B(getfenv,     lua_getfenv,    void*);
    B(setfenv,     lua_setfenv,    void*);

    B(ref,         lua_ref,        void*);
    B(unref,       lua_unref,      void*);
    B(getref,      lua_getref,     void*);

    B(newthread,   lua_newthread,  void*);
    B(mainthread,  lua_mainthread, void*);

    B(error,       lua_error,      void*);
    B(pushfstring, lua_pushfstring,void*);

    #undef B
    return true;
}

const API& api() { return g_api; }

}
