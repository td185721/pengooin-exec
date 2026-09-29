// executor.h — top-level "run this Luau source" entry
// language: C++20, target: Windows 11 x64, MSVC
#pragma once
#include "pch.h"

namespace r9k::luau {

struct RunResult {
    bool        ok{false};
    std::string error;   // filled on ok == false
};

// compiles `source`, loads into a fresh thread spawned from the elevated
// main state, then resumes. all execution happens on Roblox's own scheduler
// via the resumed coroutine — never blocks the injecting thread.
RunResult run(std::string_view source, const char* chunk_name = "=[r9k]");

}
