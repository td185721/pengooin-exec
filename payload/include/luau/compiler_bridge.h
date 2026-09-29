// compiler_bridge.h — source → Luau bytecode via the bundled compiler
// language: C++20, target: Windows 11 x64, MSVC
#pragma once
#include "pch.h"

namespace r9k::luau {

// Luau::compile options mirror. optimization: 0=none, 1=baseline, 2=aggressive.
// debug: 0=none, 1=lines, 2=full. coverage: instrumented for hit-tracking.
struct CompileOptions {
    int optimization = 1;
    int debug        = 1;
    int coverage     = 0;
    std::vector<const char*> mutable_globals;      // pointers into stable storage
    std::vector<const char*> vector_lib_ctor;
};

// returns encoded bytecode string. on syntax error, first byte is 0 and the
// rest of the string is the error message — matches Luau::compile's protocol,
// so it can be passed straight into api().load without a separate branch.
std::string compile(std::string_view source, const CompileOptions& opt = {});

}
