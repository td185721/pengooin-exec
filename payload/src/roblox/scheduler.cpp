// scheduler.cpp — TaskScheduler discovery + job lookup
// language: C++20, target: Windows 11 x64, MSVC
#include "roblox/scheduler.h"
#include "roblox/offsets.h"
#include "memory/pattern.h"
#include <cstring>

namespace r9k::rbx {

namespace {
    std::atomic<uptr> g_sched{0};

    // read a RBX::String — {ptr, size, cap, ssoBuf}. if size <= 15 the string
    // lives inline at the ssoBuf slot; otherwise ptr points to a heap buffer.
    std::string_view read_rbx_string(uptr string_field_ptr) {
        // layout: [+00] char* data (or SSO buffer)
        //         [+10] size_t size
        //         [+18] size_t cap
        //         [+20] char   sso[16]
        uptr data = *reinterpret_cast<uptr*>(string_field_ptr);
        size_t size = *reinterpret_cast<size_t*>(string_field_ptr + 0x10);
        if (!data || size == 0 || size > 0x1000) return {};
        return { reinterpret_cast<const char*>(data), size };
    }
}

bool Scheduler::bind() {
    if (g_sched.load(std::memory_order_acquire)) return true;

    uptr hit = mem::scan_all(off::SIG_TASKSCHED_SINGLETON);
    if (!hit) return false;

    uptr addr_of_singleton = mem::rip_rel(
        hit, off::RIP_TASKSCHED_DISP_OFF, off::RIP_TASKSCHED_INSN_LEN);
    if (!addr_of_singleton) return false;

    // singleton slot holds a pointer to the TaskScheduler instance
    uptr sched = *reinterpret_cast<uptr*>(addr_of_singleton);
    if (!sched) return false;

    g_sched.store(sched, std::memory_order_release);
    return true;
}

uptr Scheduler::raw() { return g_sched.load(std::memory_order_acquire); }

uptr Scheduler::find_job(std::string_view job_name) {
    uptr sched = raw();
    if (!sched) return 0;

    // jobs vector: contiguous shared_ptr<Job> — each shared_ptr is {ptr, ctrl_blk}
    uptr jbegin = *reinterpret_cast<uptr*>(sched + off::TS_JOBS_BEGIN);
    uptr jend   = *reinterpret_cast<uptr*>(sched + off::TS_JOBS_END);
    if (!jbegin || jend < jbegin) return 0;

    for (uptr slot = jbegin; slot < jend; slot += 0x10) {
        uptr job = *reinterpret_cast<uptr*>(slot);
        if (!job) continue;

        auto name = read_rbx_string(job + off::JOB_NAME_PTR);
        if (name == job_name) return job;
    }
    return 0;
}

// filled here so the weak-stub in runtime.cpp gets replaced at link time
void scheduler_bind() { Scheduler::bind(); }

}
