# Resident Evil 1 for PC Decompilation Project

## Introduction

This is a decompilation of Resident Evil 1 for PC released in 1997. The original game code, reverse-engineered from the
Ghidra decompilation of the 1997 executable, is rebuilt as a Win32 game on a
modern DirectX 11 rendering layer.

**The port is functional-complete**: the entire original game is playable —
every room, enemy, cutscene, FMV, menu and ending. Extensive playtesting
confirms it behaves like the original release.

The DirectX 5.0 wrapper of the original (Capcom's *Marni System*, built on
DirectDraw/DirectSound) is re-implemented on top of a DX11/XAudio2 layer
(`src/marni/MarniDX`), so the original PSYQ-derived game code runs unmodified
on current versions of Windows.

## Completion status

Measured against the Ghidra project: **2393 functions in the original binary**
(+161 DLL imports provided by the loader). Every function has been triaged:

| Stream | Count | Status |
|---|---|---|
| Game logic implemented in `src\` | 1716 of 1717 | done |
| Marni System DirectX internals → DX11 layer | 84 of 84 | done |
| CRT / MSVC runtime (provided by toolchain) | 290 | out of scope |
| Compiler SEH / static-init glue (absorbed by real C++ ctors/dtors) | 177 | out of scope |
| Raw D3D5 API paths (replaced by MarniDX) | 108 | out of scope |
| Software-FMV shared-memory player (replaced by native MCI) | 11 | out of scope |
| Import thunks (loader-provided) | 6 | out of scope |

**In-scope coverage: 1800 / 1801 functions = 99.9% — function-complete.**
The single documented remainder is a trivial animation/velocity setter whose
caller is undefined even in the Ghidra project; its two-line semantics are
already available through covered helpers.

Tracking tools:
- `tools/progress_report.py <ghidra_dump.txt> src` regenerates this table.
- `progress_remaining.txt` lists whatever is left after each run.

## How to build

Requirements:
- Windows (the game is strictly 32-bit)
- Visual Studio with C++ toolset and MSBuild (`Game.sln`, Win32 platform)

Build:
```
build.bat
```
This invokes MSBuild on `Game.sln` (Release / Win32) and produces
`bin\Release\residentevil.exe`. For a Debug build use:
```
build_debug.bat
```
This produces `bin\Debug\residentevil.exe`. Adjust the MSBuild path inside the
scripts if your Visual Studio installation differs.

> **Note:** Release intentionally builds with `WholeProgramOptimization`
> disabled — `/GL`+`/LTCG` miscompiles the task scheduler's naked-assembly
> stack switching (cross-TU inlining assumes standard prologues), which caused
> intermittent crashes at room load. Don't re-enable it.

To build from a developer command prompt instead:
```
msbuild Game.sln /p:Configuration=Release /p:Platform=Win32 /t:Build
```

### Running the game

The asset root is chosen **at compile time** (`src/system/AssetPath.h`), so
each configuration expects a different directory layout next to the exe:

| Configuration | Data root | Save folder |
|---|---|---|
| Debug | `.\assets\USA\` | `.\assets\save\` |
| Release | `.\usa\` | `.\SAVE\` |

Copy the USA assets directory to the release build path or copy the build exe to an
existing RE1 PC directory, no external DLLs required. In case of running VS debugger, 
copy the USA assets in the ./assets directory.

Diagnostics: with no debugger attached, trace output is suppressed (see
below); set the environment variable `RE1_DEBUGLOG=1` to append every trace
line to `re1_debug.log` next to the exe instead. Fatal startup errors also
append a symbolized stack trace to `crash.log`.

> **Note:** never call raw `OutputDebugStringA` from code that can run inside
> a scheduler task — without a debugger it fail-fasts the process
> (`0xC0000409`) because the exception cannot be dispatched on the task's
> switched stack. Use `dbg_printf` / `dbg_safe_str` (`src/DebugPrint.h`),
> which are gated on `IsDebuggerPresent`.

## Project layout

- `src\marni\` — the Marni System compatibility layer; `MarniDX.h/.cpp` is the
  DX11/XAudio2 rendering backend that replaces DirectDraw/DirectSound/D3D5.
- `src\game\` — decompiled game logic (rooms, entities, menus, door system,
  TMD renderer, effects, screens), one module per subsystem where practical.
- `src\video\` — native MCI-based FMV playback.
- `docs\` — architecture notes: task scheduler, memory layout, classes and
  vtable conventions, implementation plan.
- `tools\` — tools used to help decompilation

Every rewritten function carries its original address as a comment, and every
named global documents its original variable address, so any line in `src\`
can be traced back to the Ghidra project.
