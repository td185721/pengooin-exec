# pengooin

R9K executor. Ring-3 manual-mapped DLL. Hijacks the game's own Luau VM via
task-scheduler discovery. Bundled Luau compiler for source→bytecode.
Full sUNC + UNC environment. D3D11-hooked drawing library and overlay UI.

Ships as **one file**: `pengooin.exe`. Launch it once — it sits in the system
tray and auto-injects into `RobloxPlayerBeta.exe` whenever the Roblox client
starts. Payload DLL is embedded as an RCDATA resource inside the exe.

## Layout

```
injector/       loader + watcher + tray shell (produces pengooin.exe)
payload/        in-process runtime DLL (embedded in pengooin.exe)
  include/      public headers, grouped by module
  src/memory/   pattern scanner, RVA helpers
  src/roblox/   task scheduler + lua_State discovery
  src/luau/     compiler bridge + bytecode deserializer
  src/env/      sUNC/UNC surface — closures, debug, instances, fs, net, crypt, input
  src/ui/       Win32 shell + D3D11 drawing renderer
third_party/    luau, minhook, imgui (git submodules)
```

## Install (users)

Grab the latest `pengooin-<version>.exe` from the
[Releases](https://github.com/td185721/pengooin-exec/releases) page and
double-click it.

- Tray icon appears; watcher is now live.
- Launch Roblox — payload attaches automatically.
- `Ctrl+Shift+P` toggles the executor shell window inside the game.
- Right-click the tray icon → **Exit** to stop watching.

Logs: `%LOCALAPPDATA%\pengooin\watcher.log`.
Workspace (fs library sandbox): `%LOCALAPPDATA%\pengooin\workspace`.

## Build

Windows 11 x64, VS 2022, CMake 3.24+.

```
git clone --recursive https://github.com/td185721/pengooin-exec.git
cd pengooin-exec
git clone --depth 1 --branch 0.640 https://github.com/Roblox/luau.git third_party/luau
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Artifact: `build/injector/Release/pengooin.exe` (payload DLL is embedded).

## Cut a release

Tag a commit `v*` and push — GitHub Actions builds and publishes the exe:

```
git tag v1.0.0
git push origin v1.0.0
```

## Hyperion note

On Hyperion-protected experiences, `OpenProcess(..., VM_WRITE|CREATE_THREAD)`
is stripped from user-mode handles. The watcher logs the failure and skips
that instance — no crashes, no infinite retries. Games without Hyperion attach
cleanly on the first poll after launch.
