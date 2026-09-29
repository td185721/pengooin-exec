// offsets.h — per-build offsets & signatures for RobloxPlayerBeta.exe
// language: C++20, target: Windows 11 x64, MSVC
//
// EVERYTHING IN THIS FILE IS VERSION-LOCKED. re-derive with IDA/Ghidra after
// every Roblox client patch. change one #define, rebuild, ship.
//
// derivation notes for each entry live next to it. patterns are IDA-style
// ("48 8B ? ?" — hex bytes or ? for wildcard). rip_rel entries include the
// disp32 offset and full instruction length.
#pragma once
#include "pch.h"

namespace r9k::rbx::off {

// ---- TaskScheduler singleton --------------------------------------------
// derived from: xref to string "TaskScheduler::Job" then trace up to the
// singleton getter. classic anchor: MOV RAX, [rip+disp32] loading g_scheduler,
// wrapped in a `cmp rax, 0; jne skip; call init` pattern.
// captured: LEA/MOV instruction; disp32 at +3, insn_len 7.
constexpr auto SIG_TASKSCHED_SINGLETON =
    "48 89 5C 24 ? 57 48 83 EC 20 48 8B 05 ? ? ? ? 48 85 C0";
constexpr int  RIP_TASKSCHED_DISP_OFF = 13;   // offset of disp32 in the sig hit
constexpr int  RIP_TASKSCHED_INSN_LEN = 17;   // MOV r/m + jmp target after

// ---- TaskScheduler layout -----------------------------------------------
// jobs vector: `std::vector<std::shared_ptr<Job>>` — {begin, end, cap}, 3*ptr.
// found via observing TaskScheduler::stepJobs iterating from off 0x1D0.
constexpr size_t TS_JOBS_BEGIN = 0x1D0;
constexpr size_t TS_JOBS_END   = 0x1D8;

// ---- Job base class -----------------------------------------------------
// vtable[0..N] holds: dtor, step, statistics, ...
// job name string lives at + 0x18 (RBX::String {ptr, size, cap, ssoBuf}).
constexpr size_t JOB_NAME_PTR  = 0x18;
constexpr size_t JOB_NAME_LEN  = 0x20;

// ---- WaitingHybridScriptsJob → ScriptContext ----------------------------
// this job holds a strong ref to the DataModel's ScriptContext at + 0x1C0.
constexpr size_t WAITJOB_SCRIPTCONTEXT = 0x1C0;

// ---- ScriptContext ------------------------------------------------------
// getGlobalState(identity, thread_slot) — VFT slot 0x1B on last known build.
// alternative: direct layout access — the state pool sits at + 0x140 as an
// array of pointers indexed by identity (0..7).
constexpr size_t SC_STATE_POOL     = 0x140;
constexpr size_t SC_IDENTITY_MAX   = 8;

// ---- lua_State (matches Roblox/luau lua.h) ------------------------------
// we don't redefine the struct; we #include Luau's own header. this section
// only holds offsets used for the tiny bits of raw pointer math the env code
// needs before Luau headers are pulled in (rare — most access goes through
// the C API).
constexpr size_t LSTATE_GLOBAL     = 0x08;   // gt (global table)
constexpr size_t LSTATE_MAINTHREAD = 0x28;
constexpr size_t LSTATE_USERDATA   = 0x60;   // userdata slot Roblox stores
                                             // the ExtraSpace pointer in

// ---- ExtraSpace (Roblox's per-lua_State user block) ---------------------
// stores identity + capabilities + script pointer + task scheduler ref.
// identity is a bit-flag / integer at + 0x30 last verified.
constexpr size_t XS_IDENTITY       = 0x30;
constexpr size_t XS_CAPABILITIES   = 0x48;
constexpr size_t XS_SCRIPT_PTR     = 0x50;

}
