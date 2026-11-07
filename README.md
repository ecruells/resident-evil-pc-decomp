# Resident Evil 1 for PC Decompilation

## Introduction

This is a decompilation Resident Evil 1 for PC released in 1997. The original game code, reverse-engineered from the Ghidra decompilation of the 1997 executable, is rebuilt on a modern rendering layer, for **two platforms**:

| Platform | Renderer | Audio | FMV |
|---|---|---|---|
| Windows (Win32) | Direct3D 11 | XAudio2 | MCI |
| Linux (SDL2 + OpenGL 3.3) | OpenGL core | SDL2 mixer | ffmpeg (Cinepak AVI) |

Both builds share every line of game logic; only the platform layer and the renderer backend differ (`src/marni/MarniDX.cpp` vs `MarniDX_GL.cpp`, `src/platform/win32/` vs `src/platform/linux/`). The GL backend is written against the GLES-3-compatible subset so the same code can be repointed at Android/Switch later.

**The port is functional-complete**: the entire original game is playable —
every room, enemy, cutscene, FMV, menu and ending. Extensive playtesting
confirms it behaves like the original release.

The DirectX 5.0 wrapper of the original (Capcom's *Marni System*, built on
DirectDraw/DirectSound) is re-implemented on top of those backends
(`src/marni/`)

## Completion status

Measured against the Ghidra project: **2393 functions in the original binary**
(+161 DLL imports provided by the loader). Every function has been triaged:

| Stream | Count | Status |
|---|---|---|
| Game logic implemented in `src\` | 1716 of 1717 | done |
| Marni System DirectX internals → DX11 / OpenGL backends | 84 of 84 | done |
| CRT / MSVC runtime (provided by toolchain) | 290 | out of scope |
| Compiler SEH / static-init glue (absorbed by real C++ ctors/dtors) | 177 | out of scope |
| Raw D3D5 API paths (replaced by MarniDX) | 108 | out of scope |
| Software-FMV shared-memory player (replaced by MCI / ffmpeg) | 11 | out of scope |
| Import thunks (loader-provided) | 6 | out of scope |

## How to build

### Windows

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
This produces `bin\Debug\residentevil.exe`.

Both scripts find MSBuild with `vswhere.exe`, so any Visual Studio edition and
version (2017 or newer) works without editing a path. The solution itself pins
`PlatformToolset=v145` / SDK `10.0.26100.0`; on an older installation override
them with the `RE1_TOOLSET` and `RE1_SDK` environment variables, e.g.
```
set RE1_TOOLSET=v143
set RE1_SDK=10.0
build.bat
```

> **Note:** Release intentionally builds with `WholeProgramOptimization`
> disabled — `/GL`+`/LTCG` miscompiles the task scheduler's naked-assembly
> stack switching (cross-TU inlining assumes standard prologues), which caused
> intermittent crashes at room load. Don't re-enable it.

> **Note:** never call raw `OutputDebugStringA` from code that can run inside
> a scheduler task — without a debugger it fail-fasts the process
> (`0xC0000409`) because the exception cannot be dispatched on the task's
> switched stack. Use `dbg_printf` / `dbg_safe_str` (`src/DebugPrint.h`),
> which are gated on `IsDebuggerPresent`.

To build from a developer command prompt instead:
```
msbuild Game.sln /p:Configuration=Release /p:Platform=Win32 /t:Build
```

### PlayStation 2 (experimental)

[Download the PS2 test build (ELF and complete package)](downloads/README.md).

A native PS2 target reuses the Linux SDL2 input/audio and shared game code.
It adds an EE task scheduler, a 320x240 CPU renderer with SDL2 presentation,
and streaming MPEG1/MP2 movies. The ELF builds, but boot and gameplay have
**not yet been validated** on PCSX2 or hardware. See [PS2 setup, asset layout,
build and validation instructions](docs/PS2_PORT.md).

### Linux

A native 32-bit binary (`-m32`) built with CMake. The game logic and the Marni
layer are shared with Windows; the platform layer, the OpenGL backend and the
ffmpeg FMV decoder are Linux-only files (`src/platform/linux/`,
`src/marni/MarniDX_GL.cpp`).

Requirements (64-bit host building 32-bit).

Debian/Ubuntu:
```
sudo dpkg --add-architecture i386
sudo apt update
sudo apt install build-essential cmake g++-multilib libc6-dev-i386 \
                 libsdl2-dev:i386 \
                 libavformat-dev:i386 libavcodec-dev:i386 \
                 libavutil-dev:i386 libswresample-dev:i386
```

On Ubuntu 24.04 (noble) `libsdl2-dev:i386` cannot be resolved: it pulls
`libpulse-dev:i386` and `libibus-1.0-dev:i386`, whose `libglib2.0-dev:i386`
dependency has no i386 build there (the glib dev utilities are amd64-only).
None of those headers are used, so install the rest and force the SDL2 dev
package past its dependency check:
```
sudo apt install -y --no-install-recommends libgl-dev libsdl2-2.0-0:i386 \
     libavformat-dev:i386 libavcodec-dev:i386 \
     libavutil-dev:i386 libswresample-dev:i386
cd /tmp && apt-get download libsdl2-dev:i386
sudo dpkg -i --force-depends --force-overwrite libsdl2-dev_*_i386.deb
```

Arch/CachyOS (with `[multilib]` enabled in `/etc/pacman.conf`):
```
sudo pacman -S --needed base-devel cmake lib32-glibc lib32-gcc-libs \
                        sdl2 lib32-sdl2 ffmpeg lib32-ffmpeg lib32-mesa
```

The 32-bit packages are the point: the target is 32-bit. On Arch the `lib32-*`
packages ship runtime objects only, so the ordinary `sdl2`/`ffmpeg` packages
supply the headers, and `lib32-mesa` is the GL driver SDL loads when it creates
the context. The build finds the 32-bit library directory itself (Debian
`/usr/lib/i386-linux-gnu`, Arch `/usr/lib32`) and links the sonames actually
installed, so the same tree configures on both — no pkg-config path or other
environment needed.

Note that the result is dynamically linked against the host's ffmpeg ABI
(ffmpeg 6 = `libavcodec.so.60` on Ubuntu 24.04, ffmpeg 9 = `.so.62` on Arch):
build on the distribution you intend to run on.

Build:
```
cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux -j
```

This produces `build/linux/residentevil`, which can be launched from anywhere —
assets and saves are resolved from the binary's directory plus `config.ini`, not
from the working directory:

```
./build/linux/residentevil
```

Diagnostics go to stderr; `RE1_DEBUGLOG=1` also appends them to
`re1_debug.log`. A fatal signal writes a symbolized `crash.log`. Test hooks:
`--capture <file> [frames]` dumps the back buffer and exits, `--press
<scancode> <frame> [hold]` injects a synthetic key press, which together can
drive an attract-demo run headlessly.

For the sanitizer build (ASan/UBSan) and the rest of the porting notes see
`docs/LINUX_PORT.md`.

### Packaging a portable bundle

`build/linux/residentevil` is linked against the build host's ffmpeg, so it only
runs where those exact sonames exist. To run it on another distribution, whose rootfs is read-only and whose package repo is frozen — build
a bundle:

```
bash tools/package_linux.sh            # --with-assets also copies assets/
```

`dist/residentevil-<version>-linux-x86/` then holds the binary, a launcher that
puts `lib/` on `LD_LIBRARY_PATH`, the initial `config.ini` (from
`config.ini.template`) and the bundled 32-bit libraries. Only the
glibc family and the GL driver stack (`libGL`/`libEGL`/`libdrm`/`libgbm`/
`libvulkan`) stay the host's, because they have to match the running kernel and
GPU — on Arch/CachyOS that means `lib32-mesa`, which SteamOS and any
32-bit-capable desktop already have. Everything else travels, including the
X11/Wayland and ALSA/PulseAudio clients, so the bundle does not depend on the
target's 32-bit package set.

Game data is not included, same as the Windows release — migrate your own copy
into place with the [asset migrator](INSTALLATION.md#asset-migration) and put the `USA/` (and optionally
`JPN/`, and the Director's Cut `DC/`) trees next to the launcher.

## Installation

Getting the game data into place - which releases are supported, the asset
migrator, the folder layout, `config.ini` and choosing the USA / JPN release -
is covered in [INSTALLATION.md](INSTALLATION.md).

## Features

### WebP backgrounds and HD textures

The port has an **HD mode**: high-resolution backgrounds, textures, model skins,
movies and fonts, all optional and all read from files. With no `HD/` folder the
game looks exactly as the original. Everything below is read as **WebP** (lossy
or lossless, any resolution) unless noted:

- **Room backgrounds.** A `Stage<n>/RC*.webp` file is used in place of the
  `.pak` of the same name. The asset migrator can write a whole tree that way
  (*Room backgrounds* option: lossless is the same picture about 20 % smaller),
  and a hand-made high-resolution background is installed the same way.
- **HD textures.** Any other texture - models, items, menus, fonts, room masks
  - can be replaced by dropping `<hash>.webp` into an `HD/` folder beside
  `USA/` (or into a sub-folder of it, to group a pack by type). Set `DumpTextures=1` to have the game write the originals, named by
  that hash, to `HD/dump/`.
- **Model skins.** A character or enemy skin can also be named after its model,
  `HD/<TREE>/ENEMY/EM1010.webp` (`TREE` = the folder the model was read from).
- **HD movies.** `HD/USA/MOVIE/OU.mp4` is played ahead of the tree's own
  `.mp4` / `.avi`.
- **HD fonts.** Text is drawn in real TrueType fonts instead of the bitmap
  sheets: Archivo Black for the title menu, Archivo Narrow for the 8x8 text and
  Libertinus Serif / M PLUS 2 for the in-game messages, item names and files
  (`HD/<USA|JPN>/Data/TITLEFNT.TTF`, `FONTUS.TTF` / `FONT.TTF`,
  `FONTUS_14.TTF` / `FONT_14.TTF`). The asset migrator's HD Pack tab installs
  them; the fonts are under the SIL OFL 1.1.

```ini
[Textures]
HdTextures=1     ; use HD/<hash>.webp replacements (default; free with no HD/ folder)
DumpTextures=0   ; 1 = write every texture to HD/dump/<hash>.png
```

`docs/HD_TEXTURES.md` explains how to make a pack and what its limits are. The
asset migrator's **HD Pack** tab imports the Seamless HD Project pack format (made for Classic REbirth) into `HD/`, grouped by texture type:
backgrounds and masks from a built-in table, model skins under the model's
name, the Seamless HD Project movies, and the rest by matching against the
game's own texture dump. Set `HdTextures=0` or in the game settings menu to switch all of HD mode off without
removing the folder.

### Options menu (title screen)

The title menu is **NEW GAME / LOAD GAME / EXTRA / OPTIONS** in every mode
(EXTRA appears only while the Battle Game is offered). OPTIONS opens a menu of
its own with four pages, so none of the settings below need a text editor:

| Page | What it holds |
|---|---|
| **DISPLAY** | Window mode (windowed / borderless fullscreen), resolution, vsync. The picture always stays 4:3: a widescreen monitor or window size gets black bars instead of a stretched image. Resolution is the window's size; fullscreen uses the desktop's. |
| **INPUT** | Every control, for the keyboard and for the gamepad: select a cell and press the new key or button. Each device has a reset row. The arrow keys, Enter and Esc always work in this menu, whatever is bound. |
| **GAME** | Game mode, region, PS1 ending credits, PS1 FMV subtitles, skip any FMV, Battle Game, western difficulty - the `[Game]` keys and `[Assets] Version` described in the sections below. An entry whose assets are not installed cannot be selected. |
| **BONUS** | The outfit each playable character wears, with the model shown beside the list, and an on/off switch for each bonus weapon you have unlocked. Moving onto a weapon row shows its owner aiming it: Chris with the rocket launcher and the Minimi, Jill with the Colt Python and the Ingram. |

Game mode and region are read once at start-up, so changing either needs a
restart: **APPLY AND RESTART** on the GAME page does it for you, and leaving the
menu without it keeps the change for the next start.

**Bonuses** are kept in `bonus.dat` in the save folder, across every save and
every mode:

| Bonus | How it is unlocked |
|---|---|
| Alternate outfit (Chris, Jill) | getting the special key, in any mode |
| PC outfit (Chris, Jill) | getting the special key in the original mode |
| Director's Cut outfit (Chris, Jill, Rebecca) | clearing the Director's Cut's ADVANCED (arrange) game |
| Saturn outfit (Chris, Jill) | getting the special key in Saturn mode |
| Rocket launcher | earning it at the ending, in any mode |
| Colt Python | the ADVANCED best ending of the Director's Cut |
| Ingram (Jill) / Minimi (Chris) | earning it at the ending in the original mode |

An outfit other than DEFAULT is worn in every mode, in the campaign and in the
Battle Game, and takes priority over the wardrobe. A bonus weapon that is
switched on is put in the item box when a **new game** of the campaign starts;
the Battle Game never uses the bonus weapons (nor the Colt Python's infinite
ammo). The Director's
Cut and Saturn outfits need those releases migrated (see [Asset migration](INSTALLATION.md#asset-migration)).

The menu needs the title font, `Data/TITLEFNT.BIN`, which the asset migrator's
PC tab writes; without it the title keeps the art the release shipped with and
has no OPTIONS item.

### Options menu (in game)

The button that opened the original options screen ("A" key) now opens the same menu in game, cut down to
**DISPLAY**, **INPUT**, **BACK TO TITLE** (asks for confirmation; unsaved
progress is lost) and **BACK** (Esc / cancel does the same) to return to the
game. The pages are the title menu's, so a rebinding made here applies at once.
The room's music keeps playing underneath.

### Director's Cut mode

`[Game] Mode` in `config.ini` picks which release's content runs. It is a single
key that selects both the code branches and the asset overlay folder, so the two
can never disagree:

```ini
[Game]
; OG = the PC release (default); DC = the PS1 Director's Cut overlay
Mode=DC
```

`DC` is an **overlay**, not a second full tree: `DC/` beside `USA/` and `JPN/`
holds only the files the Director's Cut changes or adds, laid out like a base
tree. `ResolveAssetRoot` (`src/system/AssetPath.cpp`) probes it first and falls
through to the base tree for the folders the DC disc genuinely cannot supply, so
the base trees stay complete and pristine and several releases can share one
install. Build the overlay with the Director's Cut tab of the asset migrator; the
full design is in `docs/DC_PORT.md`.

### PS1 ending credits

The PC release plays its credits as movies with the text baked into the
picture (`stfc_r`, `stfj_r`, `stfz_r`, `staf_r`). The PS1 versions draw them
live instead, and the port can do the same, following PS1 `ENDING.EXE`:

- **Endings 1-3 (staff roll):** the `STAFF2.STF` credit strips scroll up over a
  black screen, with the movie playing as music only. The last strips are the
  RESIDENT EVIL logo, which fades out with the rest.
- **Endings 4-7 (cast roll):** the character's movie (`STFC` for Chris, `STFJ`
  for Jill) plays in shots that fade in and out. The `STAFF.STF` name panels
  appear beside it, a black wipe covers part of one shot, and the roll ends on
  the zoomed `BIO.TIM` logo.

Director's Cut mode turns it on automatically. In OG mode, turn it on in
`config.ini`:

```ini
[Game]
; 1 = draw the PS1 credits over the ending movies (DC mode is always on)
Ps1EndingCredits=1
```

It needs these files in the active tree (the `DC/` overlay, or `USA/` / `JPN/`
in OG mode):

| File | Holds |
|---|---|
| `Data/STAFF.STF` | the cast-roll name panels (endings 4-7) |
| `Data/STAFF2.STF` | the staff-roll strips (endings 1-3) |
| `Data/BIO.TIM` | the RESIDENT EVIL logo |
| `Movie/STFC.mp4`, `Movie/STFJ.mp4` | the PS1 movies, which have no text in them |

The Director's Cut tab of the asset migrator already writes all of these into
`DC/`. For OG mode, tick **Add PS1 assets from a PS1 disc image** on the PC
Assets tab and point it at a PS1 disc (the 1996 release or the Director's Cut).
Use a raw `.bin`/`.cue` image so the movies keep their CD-XA audio. If the
`.STF` files are missing, the ending plays the PC credit movie as before.

### Skippable FMVs

Each movie carries its own skip mask in the original's FMV table
(`0x004c39dc`, `src/video/VideoPlayback.cpp`): `0x0fff` for the cutscenes and
the Capcom/Virgin logos, `0x0000` — unskippable — for the endings (`ED1`-`ED8`,
`EU4`/`EU5`), the PC credit movies (`stf*_r`) and `DMF` / `DME`. A single
config key lifts that:

```ini
[Game]
; 1 = let every FMV be skipped, including the ones the original locks
SkipUnskippableFmv=1
```

It is off by default, so the shipped behaviour is the original's masks. The
original's 100-frame grace period still applies, so a press during the first few
seconds of a movie is ignored.

### Battle Game (Saturn mode)

Sega Saturn mode carries a second game mode the other releases never had: the
**Battle Game**, a fixed 15-arena run through `STAGE8` with a preset loadout,
scored on the ammo you did not spend. It is reached from the title menu's
EXTRA item.

The Saturn unlocks that item with **any ending**, or with a port-2
**X + Y + Z + Start** code on the press-start screen. The code is not ported
there is no second controller — so if you would rather not clear a game first,
force it on:

```ini
[Game]
Mode=SATURN
; 1 = offer the BATTLE GAME item without having cleared a game
BattleGame=1
```

It is off by default and only does anything with `Mode=SATURN`. It is a
read-time override: it never writes the unlock into a save, so a game finished
with it on still records only what you actually earned. Clearing the game once
also unlocks it for real, permanently, in that save.

**Status:** the entry into a run is ported — the item, the loadout and the first
arena's art all work. The arena sequence itself is not finished, so a run ends
at the first arena. See `docs/SATURN_PORT.md`.

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

Every backend publishes into the same joystick slot, so the game cannot tell
them apart:

- **XInput** (Xbox pads and anything exposing an XInput device) — preferred on
  Windows, brought up first (`src/marni/MarniXInput.cpp`).
- **WinMM / HID** — the original 1997 joystick path, used on Windows when no
  XInput pad is present (e.g. a DualShock 4 plugged in directly, or an old
  SideWinder).
- **SDL_GameController** — the Linux backend (`src/platform/linux/input.cpp`);
  the standard SDL mapping covers Xbox, DualShock/DualSense and Switch Pro pads.

The original joystick defaults had no OPTIONS binding at all, so the port
installs a usable layout out of the box (`InstallPadDefaultBindings`). It only
ever replaces a table that is still empty or byte-identical to the 1997
default — anything configured in the options screen is left untouched.

| XInput / SDL | WinMM / HID (DualShock naming) | Function |
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

Both layouts are editable from the INPUT page of the title screen's OPTIONS
menu and from the in-game options menu (see *Options menu* above). Bindings
are saved to `config.ini`. The original in-game screen ("Key Def" / "Joy Def",
with its four direction-mapping presets, `src/game/OptionsMenu.cpp`) is no
longer reachable.

As in the original, a save file carries the bindings it was made with, and
loading it restores them - so loading a save made before a rebind brings the
old layout back until you save again.

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
rather than through the remap table — `L1`+`R1` are buttons 5 and 6 in the
XInput, WinMM and SDL orderings, so the combo means the same thing on every
backend.

## Project layout

- `src/marni/` — the Marni System compatibility layer; `MarniDX.h` is the
  backend interface, implemented by `MarniDX.cpp` (DX11/XAudio2, Windows) and
  `MarniDX_GL.cpp` (OpenGL, Linux). Both replace DirectDraw/DirectSound/D3D5.
- `src/game/` — decompiled game logic (rooms, entities, menus, door system,
  TMD renderer, effects, screens), one module per subsystem where practical.
  Shared verbatim by both platforms.
- `src/platform/win32/`, `src/platform/linux/` — entry points, input, audio,
  video, settings, crash reporting; `src/platform/platform.h` is the seam.
- `src/video/` — FMV playback: a shared state machine over `plat_video_*`
  (MCI on Windows, ffmpeg on Linux).
- `docs/` — architecture notes: task scheduler, memory layout, classes and
  vtable conventions, implementation plan, and `LINUX_PORT.md` for the port.
- `src/third_party/` — vendored single-file decoders (`stb_vorbis.c`,
  `simplewebp.h`); their licences are in `THIRD_PARTY_NOTICES.txt`.
- `tests/` — build/verification scripts (compile gate, frame comparison).
- `tools/` — tools used to help decompilation

Every rewritten function carries its original address as a comment, and every
named global documents its original variable address, so any line in `src/`
can be traced back to the Ghidra project.

## License

This project is licensed under the **GNU General Public License v3.0** — see
[`LICENSE`](LICENSE) for the full text.

In practice: fork it, port it, mod it — but derivative works have to ship
their source under the same terms, so improvements stay available to
everyone.

The license covers **only the source in this repository** (`src/`, `tools/`,
`tests/`, `docs/`). It does not and cannot grant any rights over the original
game.

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
