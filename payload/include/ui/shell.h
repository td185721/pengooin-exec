// shell.h — executor UI window
// language: C++20, target: Windows 11 x64, MSVC
#pragma once
#include "pch.h"

namespace r9k::ui {

// spawns the UI on its own thread. safe to call once from runtime_boot after
// env_install completes so the Execute button can reach the luau executor.
void shell_start();

// forcibly close and clean up the window / thread. no-op if not started.
void shell_stop();

// toggle visibility from anywhere (also bound to Ctrl+Shift+P via global hotkey)
void shell_toggle();

}
