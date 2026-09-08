# Resident Evil 1 for PC Decompilation

## Introduction

This is a decompilation Resident Evil 1 for PC released in 1997. The original game code, reverse-engineered from the Ghidra decompilation of the 1997 executable, is rebuilt as a Win32 game on a
modern DirectX 11 rendering layer.

**The port is functional-complete**: the entire original game is playable —
every room, enemy, cutscene, FMV, menu and ending. Extensive playtesting
confirms it behaves like the original release.

The DirectX 5.0 wrapper of the original (Capcom's *Marni System*, built on
DirectDraw/DirectSound) is re-implemented on top of a DX11/XAudio2 layer
(`src/marni/MarniDX`)

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

## How to build

Requirements:
- Windows
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

The decompilation took as base the GOG USA version, which is the same binary
released in 1997, so this build expects the assets of any of those versions.

Some features of the Japanese PC release (*Biohazard* Mediakite version) is supported as well

Every asset path is built against a data root that differs by build
configuration and by selected version:

| Configuration | USA root | JPN root | Save folder |
|---|---|---|---|
| Debug | `.\assets\USA\` | `.\assets\JPN\` | `.\assets\save\` |
| Release | `.\USA\` | `.\JPN\` | `.\SAVE\` |

Copy the assets directory to the release build path, or copy the build exe to
an existing RE1 PC directory; no external DLLs required. When running under the
VS debugger, put the assets in `.\assets\` instead.

### Asset version (USA / JPN)

`[Assets] Version` in `config.ini` selects which release the build runs. It is
read once at startup (`src/main.cpp` → `SetAssetVersion`,
`src/system/AssetPath.cpp`) and swaps the data root every asset reader uses, so
**one binary runs either version** — no rebuild:

```ini
[Assets]
; USA = North American (default)
; JPN = Japanese (Biohazard)
Version=USA
```

Diagnostics: with no debugger attached, trace output is suppressed (see
below); set the environment variable `RE1_DEBUGLOG=1` to append every trace
line to `re1_debug.log` next to the exe instead. Fatal startup errors also
append a symbolized stack trace to `crash.log`.

> **Note:** never call raw `OutputDebugStringA` from code that can run inside
> a scheduler task — without a debugger it fail-fasts the process
> (`0xC0000409`) because the exception cannot be dispatched on the task's
> switched stack. Use `dbg_printf` / `dbg_safe_str` (`src/DebugPrint.h`),
> which are gated on `IsDebuggerPresent`.

## Controls

The port keeps the original 1997 input model: every binding is a *function*
(action, cancel, aim, inventory, options), and both the keyboard and the pad
are remapped onto the same PS1 button word before the game logic sees them
(`JoyToPSX`, `src/game/InputSystem.cpp`). Both devices are always live and can
be used interchangeably.

### Keyboard (default layout)

The original "Key Def" defaults, unchanged (`g_keyBindingData`, `src/Globals.cpp`):

| Key | Function |
|---|---|
| Arrow Up / Down | Walk forward / backward |
| Arrow Left / Right | Turn left / right |
| `C`, `Enter`, `Space` | Action / confirm (open, examine, fire while aiming) |
| `V`, `Ctrl` | Cancel / run (hold while moving to run) |
| `Esc` | Cancel / back |
| `X` | Aim — hold to ready the weapon, then press the action key to fire |
| `Z` | Inventory / status screen |
| `A` | Options screen |

### Game pad

Two backends are supported and both publish into the same joystick slot, so
the game cannot tell them apart:

- **XInput** (Xbox pads and anything exposing an XInput device) — preferred,
  brought up first (`src/marni/MarniXInput.cpp`).
- **WinMM / HID** — the original 1997 joystick path, used when no XInput pad is
  present (e.g. a DualShock 4 plugged in directly, or an old SideWinder).

The original joystick defaults had no OPTIONS binding at all, so the port
installs a usable layout out of the box (`InstallPadDefaultBindings`). It only
ever replaces a table that is still empty or byte-identical to the 1997
default — anything configured in the options screen is left untouched.

| XInput | WinMM / HID (DualShock naming) | Function |
|---|---|---|
| Left stick / D-pad | Left stick / D-pad / POV hat | Move and turn |
| `A` | Cross | Action / confirm |
| `B` | Circle | Cancel / run |
| `X`, `LB`, `RB`, `RT` | Square, L1, R1, R2 | Aim |
| `Y`, `Start` | Triangle, Options | Inventory / status screen |
| `LT` | L2 | Run |
| `Back` | Share | Options screen |

A pad may be plugged in or unplugged at any time; the XInput backend rescans
on a throttle and takes over slot 0 when a pad appears.

### Rebinding

Both layouts are editable in-game from the options screen (the original
"Key Def" / "Joy Def" screens, `src/game/OptionsMenu.cpp`), including the four
original direction-mapping presets. Bindings are persisted with the rest of
the video/sound settings on exit.

### System keys

| Key | Action |
|---|---|
| `F9` | In game: return to title screen (press again to confirm). At the title: exit the game (press again to confirm). Debounced, and blocked during FMV playback. |
| `Print Screen` | Save the current frame as a timestamped `.BMP` next to the game (original behaviour; skipped when the target drive has under 1 MB free). |

### Debug keys (port addition)

Compiled into both configurations but gated at runtime on
`[Debug] EnableDebug` in `config.ini` (default: on in Debug builds, off in
Release):

| Key | Action |
|---|---|
| `F1`, or pad `L1`+`R1` together | Open / close the debug menu: room change, inventory editor, flag editor, quick access (save / load / item box). Freezes gameplay underneath. Navigate with the arrows / `Enter` / `Esc`, or with the pad's *bound* action, cancel, aim and direction functions. |
| `F6` | Texture viewer overlay. Arrows select a page, hold `A` + arrows to pan, `R` resets the pan, `F6` or `Esc` closes. |
| `F8` | Toggle the collision boundary overlay. |

The debug menu's open/close pad combo is the one input read from raw hardware
rather than through the remap table — `L1`+`R1` are buttons 5 and 6 in both the
XInput and the WinMM orderings, so the combo means the same thing on either
backend.

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

## License

This project is licensed under the **GNU General Public License v3.0** — see
[`LICENSE`](LICENSE) for the full text.

In practice: fork it, port it, mod it — but derivative works have to ship
their source under the same terms, so improvements stay available to
everyone.

The license covers **only the source in this repository** (`src\`, `tools\`,
`docs\`). It does not and cannot grant any rights over the original game.

### Legal notice

- **No game assets are distributed here.** The repository contains source code
  only; `assets\` is git-ignored. Running the build requires your own legally
  obtained copy of *Resident Evil* for PC (the GOG or 1997 retail USA release,
  or the Japanese *Biohazard* PC release) to supply the data files.
- *Resident Evil*, its code, assets, characters and trademarks are the
  property of **CAPCOM CO., LTD.** This project is an independent
  reverse-engineering and preservation effort, **not affiliated with,
  authorized, endorsed or sponsored by Capcom** in any way.
- The decompiled logic in `src\` is derived from the original executable for
  interoperability, documentation and preservation purposes. It is published
  in the belief that this constitutes fair use / lawful reverse engineering in
  the contributors' jurisdictions; no rights over Capcom's copyrighted work
  are claimed or granted.
