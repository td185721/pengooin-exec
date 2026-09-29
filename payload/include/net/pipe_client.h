// pipe_client.h - control-pipe client + payload debug logging
// language: C++20, target: Windows 11 x64, MSVC
//
// connects to the watcher's control pipe (\\.\pipe\pengooin_v1),
// reads length-prefixed scripts, executes them on the elevated Luau state,
// writes result back. reconnects on drop.
//
// dbg_log writes to %LOCALAPPDATA%\pengooin\payload.log so we can see how
// far runtime_boot got when the payload silently fails to connect.
#pragma once
#include "pch.h"

namespace r9k::net {

void pipe_client_start();

}

namespace r9k {

// append one printf-formatted line to %LOCALAPPDATA%\pengooin\payload.log.
// safe to call before Luau/UI/etc. are bound.
void dbg_log(const char* fmt, ...);

}
