// pipe_client.h — control-pipe client
// language: C++20, target: Windows 11 x64, MSVC
//
// connects to the watcher's control pipe (\\.\pipe\pengooin_v1),
// reads length-prefixed scripts, executes them on the elevated Luau state,
// writes result back. reconnects on drop.
#pragma once
#include "pch.h"

namespace r9k::net {

// spawn the background pipe client thread. idempotent.
void pipe_client_start();

}
