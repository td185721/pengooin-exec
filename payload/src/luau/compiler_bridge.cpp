// compiler_bridge.cpp — thin wrapper around Luau.Compiler
// language: C++20, target: Windows 11 x64, MSVC
//
// links against the bundled Luau.Compiler + Luau.Ast targets from
// third_party/luau (added by root CMakeLists). the game's own VM will accept
// this bytecode because both are the same fork — sync the submodule to the
// commit hash Roblox is currently shipping (they announce it in luau release
// tags; find latest at github.com/Roblox/luau/releases).
#include "luau/compiler_bridge.h"

#if __has_include(<Luau/Compiler.h>)
    #include <Luau/Compiler.h>
    #define R9K_HAS_LUAU_COMPILER 1
#else
    #define R9K_HAS_LUAU_COMPILER 0
#endif

namespace r9k::luau {

std::string compile(std::string_view source, const CompileOptions& opt) {
#if R9K_HAS_LUAU_COMPILER
    Luau::CompileOptions o;
    o.optimizationLevel = opt.optimization;
    o.debugLevel        = opt.debug;
    o.coverageLevel     = opt.coverage;
    if (!opt.mutable_globals.empty()) o.mutableGlobals = opt.mutable_globals.data();
    if (!opt.vector_lib_ctor.empty()) o.vectorLib      = "Vector3";
    return Luau::compile(std::string(source), o);
#else
    // if the submodule isn't checked out yet, still ship a well-formed error
    // that luau_load will surface without crashing.
    (void)opt;
    std::string err = "\0Luau.Compiler not linked — sync third_party/luau";
    return err;
#endif
}

// replace weak stub in runtime.cpp
void compiler_init() {
    // no lazy state; the compiler is functional. hook point kept so future
    // phases (e.g. bytecode encryption tag negotiation) have somewhere to land.
}

}
