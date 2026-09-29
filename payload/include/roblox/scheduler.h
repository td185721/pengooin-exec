// scheduler.h — RBX::TaskScheduler wrapper
// language: C++20, target: Windows 11 x64, MSVC
#pragma once
#include "pch.h"

namespace r9k::rbx {

class Scheduler {
public:
    // resolves the TaskScheduler singleton via signature scan; caches the ptr.
    // returns false if the pattern doesn't hit (client version drift).
    static bool bind();

    // raw singleton — nullptr until bind() succeeds.
    static uptr raw();

    // walk jobs vector, find one whose name matches `job_name`, return its ptr.
    // O(n) over the jobs list; typical n is ~20, so a linear walk is fine.
    static uptr find_job(std::string_view job_name);
};

}
