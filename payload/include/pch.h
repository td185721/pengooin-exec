// pch.h — payload-wide precompiled includes and primitives
// language: C++20, target: Windows 11 x64, MSVC
#pragma once
#include <windows.h>
#include <winternl.h>
#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <optional>
#include <atomic>
#include <mutex>
#include <memory>
#include <functional>
#include <unordered_map>

namespace r9k {
using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using i32 = int32_t;
using i64 = int64_t;
using uptr = uintptr_t;

// invoked once from DllMain — starts the runtime thread that stalls on scheduler
// discovery, then binds the environment and hands off to the script executor.
void runtime_boot();
}
