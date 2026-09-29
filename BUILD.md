# build guide — pengooin

## prerequisites

- Windows 11 x64
- Visual Studio 2022 (v143 toolset, MSVC 19.34+)
- CMake 3.24+
- Git 2.40+

## clone with submodules

```bash
git clone https://github.com/td185721/pengooin-exec.git
cd pengooin-exec
git submodule add https://github.com/Roblox/luau.git third_party/luau
# pin to whatever tag matches the current RobloxPlayerBeta bytecode version
git -C third_party/luau checkout 0.640
```

`third_party/luau` is required for the compiler bridge; without it,
`luau::compile` emits a well-formed error but the executor still links.

## build

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Single artifact: `build/injector/Release/pengooin.exe`. The payload DLL is
compiled by the sibling `r9k_payload` target and embedded into the exe as an
RCDATA resource at link time — no companion DLL is shipped.

Intermediate artifacts (for debugging):

- `build/injector/Release/pengooin.exe`
- `build/payload/Release/r9k_payload.dll`   (embedded copy of this ships)

## run

```bash
build/injector/Release/pengooin.exe
```

The watcher lives in the tray, polls the process list every 500ms, and
manual-maps the embedded payload into any `RobloxPlayerBeta.exe` it sees.
Injections are logged to `%LOCALAPPDATA%\pengooin\watcher.log`. Dedup by pid
prevents double-injection; when Roblox exits the pid falls out of the seen
set so a relaunch is treated fresh.

The payload waits ~2s after DllMain for the client to settle, then binds the
task scheduler and elevated Luau state. `Ctrl+Shift+P` toggles the shell
window from anywhere.

## release

Push a `v*` tag; the `release.yml` workflow builds and uploads
`pengooin-<tag>.exe` to a GitHub Release.

## per-patch derivation workflow

Every Roblox client patch shifts signatures and struct offsets. To roll the
executor onto a new client build:

1. **grab a fresh `RobloxPlayerBeta.exe`** from
   `%LOCALAPPDATA%\Roblox\Versions\<latest>\`.
2. open in IDA Pro or Ghidra. wait for auto-analysis.
3. **find the TaskScheduler singleton getter**:
   - search for the string `"WaitingHybridScriptsJob"` (or `"TaskScheduler::Job"`)
   - xref to the function that references it
   - trace up to the outer getter that does
     `mov rax, [rip+g_scheduler]; test rax, rax; jne skip; call init`
   - copy the `mov rax, [rip+...]` line's bytes as an IDA-style pattern
   - update `SIG_TASKSCHED_SINGLETON` in `payload/include/roblox/offsets.h`
4. **verify offsets** on TaskScheduler / Job / ScriptContext:
   - `TS_JOBS_BEGIN`, `TS_JOBS_END`, `JOB_NAME_PTR`,
     `WAITJOB_SCRIPTCONTEXT`, `SC_STATE_POOL`
   - use IDA's structure view; if any field moved, bump the offset.
5. **re-derive Luau API sigs**:
   - each function in `payload/src/luau/api.cpp` has an `SIG_*` line
   - xref from `"luau_load"` / `"lua_pcall"` string tables or the function
     names emitted by RTTI (RobloxPlayerBeta ships symbols in DWARF-lite form
     that IDA can parse)
   - update every mismatched sig, one at a time; run
     `payload/tests/api_probe.exe` (todo, phase 14) to confirm each
     resolves to a plausible function prologue

## test suite

`scripts/sunc_runner.luau` — paste into the Execute tab, hit Run, read the
console tab. Reports per-function pass/fail + total coverage percentage.

## known parity stubs

The following functions ship as safe no-ops or partial impls; each unblocks
once its dependency lands. All are marked with inline comments referencing
this section.

| function            | blocked by                                    |
|---------------------|-----------------------------------------------|
| `hookfunction` (L → *) | `lua_topointer` sig                        |
| `getupvalue{s}`, `setupvalue` | `lua_getupvalue`, `lua_setupvalue` sigs |
| `getconstants`, `setconstant` | `Proto*` extractor via `lua_topointer` |
| `setreadonly`         | `Table*` extractor via `lua_topointer`      |
| `setnamecallmethod`   | `luaV_settvalue` sig                        |
| `getinstances`, `getnilinstances`, `getscripts` | DataModel walker |
| `firesignal`          | `RBX::Signal::invokeSlot` sig               |
| `fireclickdetector`   | `RBX::ClickDetector::sendMouseClick` sig    |
| `fireproximityprompt` | `RBX::ProximityPrompt::trigger` sig         |
| `firetouchinterest`   | `RBX::TouchTransmitter::onTouch` sig        |
| `cloneref` full fidelity | `lua_newuserdatatagged` sig              |
| `getscriptclosure`    | `RBX::LuaSourceContainer::getBytecode` sig  |
| text rendering        | ImGui font stack (phase 12 extension)       |

All of these follow the same derivation pattern documented above.
