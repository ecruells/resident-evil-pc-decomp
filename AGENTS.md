# Resident Evil 1 for PC Decompilation Project

## What this is

A decompilation of the 1997 Resident Evil 1 PC release (the USA build, as shipped
by GOG — same binary as the 1997 retail disc). The code is reconstructed as a
Win32 C++ project that reproduces the original binary's behaviour, not a
redesign: original function and global addresses stay in comments, and the
original's quirks are preserved rather than cleaned up.

## Current state

- **USA port: function-complete and playable.** 1800 of 1801 in-scope game
  functions are implemented; the single remainder
  (`ent_setanim_walkto_helper`, `0x0040c3d0`) is documented. The Release build
  has been play-tested end to end by the user. Remaining work is bug fixes and
  new features — not finding missing functions.
- **JPN (Biohazard, MediaKite release): partial.** The Japanese text encoding,
  the message / item-name / item-description / font tables and the save/load
  screen strings are ported, generated into `src/game/JpnTextTables.cpp` and
  selected at runtime with `config.ini [Assets] Version=JPN`. Everything else
  that release has and the USA build does not is **not ported yet**.
- **Linux port: playable.** A native 32-bit binary (`-m32`) shares every game
  translation unit and swaps the platform layer plus the renderer backend
  (SDL2 + OpenGL 3.3 core, ffmpeg for FMV). Verified running on Ubuntu and
  CachyOS, keyboard and gamepad. See `docs/LINUX_PORT.md`.

## Project goal

The USA release is no longer the end of the project. The goal now is to **add
features from other RE1 releases** on top of this port, starting with the
Japanese MediaKite version. Extend behaviour; keep the existing USA behaviour
as the default so the two do not silently diverge.

## Ghidra programs

Two programs are available in the Ghidra project. Both are reverse-engineering
sources for this repo:

| Program | What it is | Use it for |
|---|---|---|
| `ResidentEvil.exe` | USA PC release (1997 / GOG) | the primary source — every address, function and global already in this repo comes from it |
| `Biohazard.exe` | Japanese MediaKite PC release | the features, text and data the USA build does not have; the JPN tables in `src/game/JpnTextTables.cpp` were generated from it |

- Query code through the MCP Ghidra server (`ghidraMcp`, configured in
  `opencode.json`, `http://127.0.0.1:8081/mcp`). **If the server is unavailable,
  stop and ask the user for help — do not guess code from memory.**
- Addresses differ between the two programs. If an address does not match what
  you expect, ask the user which program is currently loaded.
- When you port something from `Biohazard.exe`, name the program in the comment
  so it is not mistaken for a USA address, e.g. `// 0x004912C0 (Biohazard.exe)`.
- The entry point in the USA program is `main` at `0x00441350`.

## Reverse engineering rules

- Comment the original address of every function you rewrite and every global
  you declare.
- Name unnamed functions, globals and locals — and rename them in the Ghidra
  project too, so the next session sees the same names.
- Implement the dependencies of what you port. Do not stub or skip functions;
  this project keeps as much of the original code as possible.
- **Global placement.** Before defining a new global, read
  `docs/MEMORY_LAYOUT.md` and check its original address:
  - `0x00be41e0..0x00be9620` (game-init wipe range) → the global must also be
    cleared by `ResetGameStateBlock()` in `src/game/GameStart.cpp`. The old
    `.gwipe` / `.sched` / `.items` linker sections are **gone**; every global is
    now an ordinary definition and the wipe is an explicit per-name clear.
  - `0x00be9620..0x00be9a3c` (bio card / save block) → a `BioCardLayout` field.
  - Never rely on linker adjacency for a range operation (`memclr`/`memcpy`/
    pointer walk across two different globals) or for past-the-end addressing —
    model the range explicitly. Getting this wrong corrupts memory silently.
  - The "Mechanism" sections of `docs/MEMORY_LAYOUT.md` predate the section
    removal; treat them as history, not as instructions.
- If you cannot test a change yourself (i.e. playing the game), do not assume it
  is solved — ask the user to test and tell you the result.

## Tools (`tools/`)

`tools/` holds decompilation tooling and the verifiers whose artifacts live
there; `tests/` holds the test harness (`check_platform_boundary.py`,
`compile_linux.sh`, `rgba_to_png.py`). Keep that split.

Most tools read the original binaries (`assets/ResidentEvil.exe`,
`assets/Biohazard.exe`) or the shipped assets (`assets/USA|JPN/...`) and print
decoded data — they are how a table is confirmed against the original instead
of guessed. Run them from the repo root.

**Extraction from the original binaries**
- `decode_re1.py` — decode the game's custom text encoding (`PrintFormattedText` / `PrintText8x14` strings).
- `decode_msg_table.py` — decode the global message table from `ResidentEvil.exe` for comparison with the `STR()` sources in `Globals.cpp`.
- `extract_global_messages.py`, `extract_item_descriptions.py` (table at `0x004C6160`), `extract_string_table.py` — pull specific tables out of the original binary.
- `mine_effect_tables.py`, `gen_effect_c_tables.py` — mine the billboard-effect tables and emit them as C initializer lists for `EffectSystem.cpp`.
- `mine_room_scd.py` — dump and decode an RDT's per-frame SCD room script.
- `scd_widths.py` — derive SCD command argument widths from the original's dispatch table (`0x4c1110`).
- `progress_report.py` — how many original functions are implemented in `src/` (needs a Ghidra function-entry dump; see its header).

**Verifiers — run them after touching what they cover**
- `verify_msg_encoding.py` — replicates the `STR()` `Encoded` constructor and compares the encoded global messages against the original bytes.
- `verify_msg_fixes.py` — same idea for the fixed `STR()` sources, byte for byte.

**Asset and file inspection**
- `dump_tim.py` — parse a PSX TIM and write a viewable PPM.
- `dump_esp_sprite.py` — decode a sprite region of an effspr TIM (pixels + CLUT, with the STP bit).
- `pak_view.py` — view `.pak` background images (LZW → TIM → PNG/PPM).
- `dump_room_masks.py` — per-camera room-mask (overlay) sprite tables from RDTs.
- `dump_init_doors.py` — init-script `door_set` records and other room-action entries.
- `dump_init_scd.py` — an RDT's initialization SCD (header `+0x60`).
- `dor_disasm.py`, `door_script_parse.py` — disassemble / parse the `.dor` door-animation scripts.
- `evt_disasm.py` — an RDT's SCD *event* scripts (the cutscene VM, not the command stream `mine_room_scd.py` handles).
- `sim_zone_walk.py` — simulate the zone-graph walkers with the original's exact memory model over real RDT zone tables.

**Japanese (Biohazard.exe)**
- `jpn_font_table.py` — glyph map of the JPN font (`data\FONT.TIM`).
- `jpn_msg_decode.py` — decode / re-encode the JPN text tables straight from the JPN executable.
- `gen_jpn_text.py` — generate the C++ side from those two (`JpnTextTables.cpp`, `JpnFontTable.h`, and `test_str_jp.cpp`).

**Editors (open in a browser, no build)**
- `rdt_event_editor.html` — RDT event editor; `node tools/test_rdt_editor.js` runs its headless tests (add `--quick` for assertions only).
- `save_editor.html` — PC save-file editor.

**Linux** — `package_linux.sh` and `elf_needed.py`; both are described in the Linux port section above.

`test_str_jp.cpp` is **generated** by `gen_jpn_text.py` — do not edit it by hand.

## Code style

- The original is C++ compiled into a Win32 game. The decomp follows that style
  and keeps the original's structure.
- Strictly 32-bit: no 64-bit libraries, types or arithmetic assumptions.
- Capcom's **Marni System** is a DirectX 5 wrapper over the PSYQ (PS1 SDK). On
  modern Windows it is backed by a DX11 / XInput layer (`MarniDX`,
  `MarniXInput`); on Linux by OpenGL + SDL2. Both keep the original Marni method
  set — see `docs/MARNI_SYSTEM.md`.

## Platform boundary

`src/game/` must stay OS-agnostic: no `<windows.h>`, no Win32 API calls.
Everything OS-specific sits behind `src/platform/` (`platform.h` plus `win32/`
and `linux/`). `tests/check_platform_boundary.py` enforces this in CI — run it
before committing.

## Linux port

- `CMakeLists.txt` is the source-of-truth translation-unit list for Linux;
  `Game.vcxproj` for Windows. **Keep both in sync when adding a file.**
- Backends: `src/platform/linux/{platform,input,audio,video,config,crash,main,stubs}.cpp`
  and `src/marni/MarniDX_GL.cpp` + `src/marni/MarniGLFuncs.{h,cpp}` (hand-rolled
  GL loader, no glad/GLEW).
- Build on a 64-bit host with 32-bit support (WSL Ubuntu 24.04 is the reference
  environment):
  ```
  cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=Release
  cmake --build build/linux -j
  ```
  Dependencies and the Arch/CachyOS package names are in the README.
- The binary can be launched from anywhere: assets and saves come from
  `config.ini` (`[Assets] Path`, `[Save] Path`), resolved against the
  **executable's** directory. Defaults are `<exe dir>/USA` and
  `<exe dir>/SAVE` (case-resolved against the filesystem).
- Headless test hooks (`src/platform/linux/main.cpp`):
  `--press <SDL scancode> <frame> [hold]`, `--capture <file> [frames]` (writes
  `RE1CAP <w> <h>\n` + RGBA; convert with `python3 tests/rgba_to_png.py`), and
  `RE1_DEBUGLOG=1` to append trace output to `re1_debug.log`. A fatal signal
  writes a symbolized `crash.log`.
- Portable bundle for other distributions: `bash tools/package_linux.sh` →
  `dist/residentevil-<version>-linux-x86/` (binary + bundled 32-bit libraries +
  launcher). Game data is never bundled.
- `python3 tools/elf_needed.py <binary>` prints the ELF class and `DT_NEEDED`
  order — use it when a run fails with "error while loading shared libraries".

## VTable calling conventions

When rewriting code that calls through raw vtable pointers (not C++ virtual
methods), check the calling convention used by the vtable's adapter layer:

- **CMarniDirect3D** vtables use `__cdecl` wrappers (`self` as first stack arg)
- **CMarniViewport2** vtables use `__stdcall` wrappers (callee cleans stack, `RET N`)
- **CMarniBits** vtables use `__cdecl` wrappers (`self` as first stack arg)

A mismatch (e.g. calling a `__stdcall` vtable entry with a `__cdecl` function
pointer type) causes Run-Time Check Failure #0 at runtime. See
`docs/CLASSES_AND_VTABLES.md` for the full convention table and adapter
signatures.

## Game design

- Stage directories and room RDT files are 1-indexed, but `g_stageId` is
  0-indexed. Most stage tests use the 0-index form — use the 0-index stage
  constants in `src/game/Types.h`. Some tests use the 1-index form to get the
  absolute index of mansion stages (see the same header).
- When commenting about a stage, use the 1-index number or its name; when
  documenting a memory index, use hex with the 0-index notation.
- **Task-based game logic**: gameplay is organized into tasks scheduled every
  frame (`docs/TASK_SCHEDULER.md`). The engine depends on this system, so adapt
  it exactly — including the naked-assembly stack switch on MSVC.


## Build

- **Windows**: `build.bat` (Release) / `build_debug.bat` (Debug), MSVC v145
  (VS 2026) locally; CI uses v143. `WholeProgramOptimization` **must stay
  disabled** in Release — `/GL` + `/LTCG` miscompiles the task scheduler's naked
  assembly and causes intermittent crashes at room load.
- **Linux**: see the Linux port section above.
