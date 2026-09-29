# pengooin

R9K executor. Ring-3 manual-mapped DLL. Hijacks the game's own Luau VM via
task-scheduler discovery. Bundled Luau compiler for source→bytecode.
Full sUNC + UNC environment. D3D11-hooked drawing library and overlay UI.

## Layout

```
injector/       loader — process finder + manual PE mapper
payload/        in-process runtime DLL
  include/      public headers, grouped by module
  src/memory/   pattern scanner, RVA helpers
  src/roblox/   task scheduler + lua_State discovery
  src/luau/     compiler bridge + bytecode deserializer
  src/env/      sUNC/UNC surface — closures, debug, instances, fs, net, crypt, input
  src/ui/       ImGui overlay, script editor
third_party/    luau, minhook, imgui (git submodules)
```

## Build

Windows 11 x64, VS 2022, CMake 3.24+.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Artifacts: `build/injector/Release/r9k.exe`, `build/payload/Release/r9k_payload.dll`.

## Run

```
r9k.exe --attach RobloxPlayerBeta.exe --dll r9k_payload.dll
```
