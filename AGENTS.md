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

- **Game OPTIONS menu (port-added, no original address).** With the
  title font present, every mode's title menu is NEW GAME / LOAD GAME / EXTRA
  (only while the Battle Game is offered) / OPTIONS. OPTIONS chains
  `game_options_state` (`src/game/GameOptions.cpp`), a state of its own with
  four pages: DISPLAY, INPUT, GAME (the `[Game]` keys and the region; Mode and
  region are written to config.ini and take effect on the next start) and
  BONUS. Bonuses (outfits, bonus weapons) are kept across saves and modes in
  `<save folder>/bonus.dat` (`src/game/Bonus.h` has the unlock rules); a chosen
  outfit overrides every release's costume logic in `LoadEntityEMD`, and also
  dresses that character's cutscene NPC (`LoadNpcOutfit`: the outfit's mesh and
  texture on the NPC's own skeleton and animations, as the DC builds its own).

- **WebP backgrounds and HD textures (port-added, no original address).** A
  `Stage<n>/rc*.webp` stands in for the `.pak` of the same name
  (`src/game/RoomBackgroundWebp.cpp`; overlay `.webp`, overlay `.pak`, base
  `.webp`, base `.pak`), at any resolution. Every other texture can be replaced
  by content hash: `CreateTextureHd` in `src/marni/MarniSystem.cpp` is the one
  place a finished RGBA8 image becomes a renderer texture, and
  `src/system/HdTextures.cpp` swaps in `<assets>/HD/<hash>.webp` there (the
  file may sit one folder down, e.g. `HD/skin/`; the folder is indexed once
  through `plat_dir_list`)
  (`[Textures] HdTextures`, `DumpTextures` writes the originals to
  `HD/dump/`). Callers keep the LOGICAL size and normalise UVs against it, so
  never read a texture's real size back. Model skins can also be named after
  their EMD, `HD/<TREE>/ENEMY/EM1010.webp` (TREE = the folder the model was
  read from): `LoadEntityEMD` keys the PSXTexture bank, `VTable_CreateTextureHandle`
  looks the key up. The decoder is the vendored
  `src/third_party/simplewebp.h`. See `docs/HD_TEXTURES.md`.
  With HD on, text is drawn in real fonts from the same folder
  (`src/game/HdText.h`): `TITLEFNT.TTF` (Archivo Black) for the title menu and
  the title sheets' option art (`title_font_load_sheet`), `FONTUS.TTF` /
  `FONT.TTF` (Archivo Narrow) for `PrintText8x8` and the copyright line,
  `FONTUS_14.TTF` / `FONT_14.TTF` (Libertinus Serif / M PLUS 2) for every
  8x14 cell `AddTintSprite` draws. The 8x8 text keeps each word's cell span:
  callers align by character cells.

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
| `SLUS_001.70` | original PS1 USA release (1997) | PS1-side engine/format analysis; names were aligned to the PC decomp |
| `SLUS_005.51` | PS1 Director's Cut (1997) | Arrange mode, new rooms/enemies/items and other DC-only data; functions/globals named from `SLUS_001.70`. See `docs/PSX_DIRECTORS_CUT_ANALYSIS.md` |

- Query code through the MCP Ghidra server. **If the server is unavailable,
  stop and ask the user for help — do not guess code from memory.**
- Addresses differ between the programs. If an address does not match what
  you expect, ask the user which program is currently loaded.
- When you port something from `Biohazard.exe`, name the program in the comment
  so it is not mistaken for a USA address, e.g. `// 0x004912C0 (Biohazard.exe)`.
- The entry point in the USA program is `main` at `0x00441350`.
- The two PS1 builds are different compilations: their addresses do **not**
  map by a constant offset. Use `tools/psx_align.py` for cross-build mapping.

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
- `gen_ps1_audio_manifest.py` — name every PS1 sound effect, voice clip and BGM track after the PC `.wav` it replaces (PC slot tables for the candidates, audio correlation to confirm) and emit the asset migrator's `Ps1AudioManifest.h`. Needs a raw PS1 image.

**Verifiers — run them after touching what they cover**
- `verify_msg_encoding.py` — replicates the `STR()` `Encoded` constructor and compares the encoded global messages against the original bytes.
- `verify_msg_fixes.py` — same idea for the fixed `STR()` sources, byte for byte.
- `verify_western_difficulty.py` — the evidence behind `western_difficulty()` (docs/WESTERN_DIFFICULTY.md): the PS1 USA weapon hit table equals the PC's second-playthrough table and all five Saturn `GAME*.PRG` copies, no USA stage overlay calls `Flg_ck`, the zombie bite table is the hard row only, and the Yawn hit is a flat 15. Needs the PS1 USA disc, the extracted Saturn disc and capstone.
- `verify_dc_item_models.py` — cross-checks the Director's Cut item-view mapping (`ItemModels.cpp`) against the generated DC item lookup and the shipped `ITEM_M2` art, and checks `g_ItemsImageBuffer` covers the DC sprite sheet.
- `verify_dc_entity_models.py` — the DC model table (`0x8008d03c`, via the Ghidra bridge) resolved to file names through the disc's ENEMY directory, entity id `0x16`'s handler read out of all 14 PS1 stage overlays, and the models `dc/EntityModels.cpp` names against the overlay.
- `verify_dc_save_mode.py` — recomputes every `BioCardLayout` offset from the struct and checks it against each field's own comment, the DC mode byte → flag-bit mapping against the PS1 load path, and the overlay font's four CLUT rows against the base glyph block and the PS1 colour columns.
- `verify_saturn_code.py` — proves the Saturn modules are big-endian SH-2, by scoring `NOP`/`RTS`/`BSR` density in both byte orders against a random-byte baseline. Run it before importing anything Saturn; the little-endian reading looks plausible word-by-word but collapses to the noise floor.
- `verify_saturn_models.py` — the Saturn's two model-table entries (type `0x16` → `em1016`, `0x17` → `em1017`, both character blocks) against the port's own `g_emdPathTable` in `EntityModelLoader.cpp`, the Tick loader swap's guard width, the `g_bSaturnMode` gating, both build files, and the four models in `assets/SATURN/Enemy/`. It also fails on **any basename collision in `Game.vcxproj`**: MSVC names objects after the basename, so `dc/EntityModels.cpp` and `saturn/EntityModels.cpp` shared one `.obj` and the link lost four `dc_*` symbols. That is why the file is `SaturnModels.*`.
- `verify_saturn_tick.py` — the Tick is **not** a reskinned Hunter: stage byte 2 loads `GAME2.PRG`, whose Hunter is its own build (a stretching-arm stab, a keyframe animator, a retuned variant-2 layer). Checks the module/table mapping out of the three update tables (type 6 is a real function in GAME1/2/5 and an `rts` stub in GAME3/4), that the Tick's keyframe units are the PS1 section's and that `EM1016`'s PS1 section is the Hunter's byte for byte, the hand-copied pounce-roll table, the literals the port's constants come from (`cmp/eq #85`, the state block, the four doubles behind `0x0606E330`, the 800 tip radius), that `SaturnTickAnim.cpp` is current, both build files, no basename collision, and every hook behind `g_bSaturnMode`. Function map in `docs/SATURN_VERSION_ANALYSIS.md` §4i.
- `gen_tick_tables.py` — turn `GAME2.PRG`'s three Tick animations (`0x0609B878`: walk `0x80`, stab wind-up `0x81`, retract `0x82`; 8-byte `{time, x, y, z}` keys ending in `0xFFFE`) into `src/game/saturn/SaturnTickAnim.cpp`. `--check` exits 1 when the checked-in file is stale.
- `verify_saturn_battle.py` — the Battle Game's data: the arena → campaign room table at `GAME1 0x06092B5C`, the arena spawn table at `GAME5 0x060958A8` and the per-room "has enemies" flags at `0x060959A0` (17 single **bytes**, bit 0 per room — read as u16 they match nothing) all read back out of the disc and compared against `src/game/saturn/SaturnBattle.cpp`; the opening message re-encoded from the C++ `STR()` and compared to `GAME5 0x0609E979`; each arena's campaign background pak present; the three rest rooms cross-checked against their door records; the stage/room constants and the `STAGE_ARRANGE_FIRST` coupling; the unlock's card offset and the `[Game] BattleGame` config key that replaces the Saturn's port-2 cheat; the third title item falling through to the load screen rather than chaining `game_start`; the arena index derived from the room byte and never stored; every call site gated so OG/DC cannot reach it — including `saturn_battle_end_run()`, without which the arena background redirect outlives a run and puts the next campaign game in arena 1's room; and the Battle Game's music: the arena BGM group/state tables against `GAME5 0x0609ACCC`/`0x0609ACDC`, group `0x40` = `BATTLE.CDP`, the game's track names against the migrator's `kSaturnMusic`, and the end-of-run fanfare.
- `verify_saturn_ram.py` — read a live disc through **Yaba Sanshiro's MCP debug server** (127.0.0.1:9640, streamable-HTTP JSON-RPC on `POST /mcp`; not registered in `.mcp.json`) and match each module's bytes against the SH-2 RAM dump. This is how the load addresses were found: `0.BIN` at `0x06004000`, the eleven `.PRG` overlays all at `0x0602B000`. `step`/`breakpoint`/`exec_history` also need `set_debug_option {"cpu_core":"interpreter"}`.
- `saturn_module_dump.py` — dump a module's live image out of emulator RAM (`check <mod>` compares disc vs live, `dump <mod>` writes it; also `list`, `status`). **The `.PRG`s are NOT compressed**: a module is the disc file loaded verbatim at `0x0602B000` (GAME1: 309 of 460,608 bytes differ, all runtime data) plus BSS past its end, so for static analysis import the disc `.PRG` directly. The old "compressed tail past +0x6710" claim came from a hexdump-parser bug (it read the ASCII column as bytes and shifted the image 1-2 bytes in patches), now fixed here and in `verify_saturn_ram.py`; any `.ram` dumped before the fix is suspect.
- `saturn_rdt.py` — what is known about the Saturn `.RDT`: `header` (differencing all 363 rooms), `cameras` (structural search for the 0x2C array by its `fov` field), `stride` (any fixed-stride array of N fov-like int32s, all strides/byte phases/endiannesses). **Every detector is validated against the PS1 disc first**, because the Saturn header is restructured (one constant byte offset, and the 4-byte stub rooms are 5 bytes) and the RDT is little-endian on *both* discs. Its negatives were measured on *packed* bytes; run the detectors on a `saturn_room.py rdt` decode instead.
- `saturn_bp.py` — arm/catch the module decompressor `FUN_0602b040` (`0x0602B040`) in the emulator. `arm` switches to the interpreter core with history on and sets the code breakpoint; `catch` waits for the stop and prints the real `param_1` (src, R4) and `param_2` (dst, R5) plus a hexdump of each. The function has **no static callers and no pointer to it** in `GAME1.ram`, so these parameters cannot be inferred from the callee — the hit has to be provoked by a module load, which means **a room/module transition has to be triggered in the emulator first**. It saves the captured `src`/`dst` to `assets/SATURN_RAM_DUMP/captures/`, because the emulator overwrites those buffers on the next room change, save load, or restart. **Always re-run `arm` after an emulator restart**: the core silently reverts to the dynamic recompiler, which ignores breakpoints and leaves junk `0x00000000` entries behind.
- `saturn_lz77.py` — a **verified** decoder for `FUN_0602b040`, the general-purpose LZ77 the engine calls for assets. LSB-first bit stream, first byte is the bit buffer; a set bit is a literal copied from the source; clear bit selects the form (short = 2 length bits + 1 offset byte, long = a 2-byte **little-endian** token with the length field taken from the *first* byte, `0` ends the stream); offsets are **backward** (`| 0xFFFFFF00` → −256…−1, `| 0xFFFFE000` → −8192…−1) relative to the *current* output pointer. Verified live: 256 KB → exactly 76,800 bytes. The two mask constants are **per-module** — read them only with the matching module resident, or you will decode a different module's data. Six traps that each cost real time are written up in `docs/SATURN_VERSION_ANALYSIS.md` §4c; the one that bites hardest is that `BT` tests the `SHLR` **carry** (the bit *before* the shift).
- `saturn_rdt_lzscan.py` — decodes a `.RDT` from **every** byte offset and reports clean terminations on a zero token. Its old "0 of 266,320, not this codec" verdict was **wrong**: a 128 KB output cap discarded the real hit at offset 0 (decoded rooms are 175-616 KB); the cap is now 1 MB.
- `saturn_wardrobe_rdt.py` — the Saturn wardrobe (rooms 11C / 61C) as an RDT patch: the Saturn offers its two outfits from GAME5 code (handler 9 → `0x060490B8`, post-action 4 → `0x0605CA84`), the port from the room script. `patch IN OUT` rewrites a decoded room (area 5's event 0 → 3, new events 0 and 3, the Saturn's texts as room messages 3-5, tables inserted at the VAB body with every later offset moved); `check` compares the migrated `assets/SATURN` rooms with it (`check --jpn`: `assets/SATURN_JPN` against the Japanese disc, whose questions are its own, GAME5 `0x0609EAB7..`). The migrator's `core/SaturnWardrobe.cpp` is byte-identical (`re1am_selftest satwardrobe IN OUT [jpn]`).
- `saturn_room.py` — **the Saturn room decoder.** `rdt FILE [OUT]` decodes one room; `verify` decodes all 363 and compares them with the PS1 disc image in place (348 byte-identical, 15 `STAGE8` new); `bss FILE OUTDIR` renders every background to PNG. The Saturn `.RDT` **is** the PS1 `.RDT`, LZ77-packed with the `saturn_lz77.py` codec from byte 0; the `.BSS` is a new container (BE offset table, 512-B RGB555 palettes, LZ77 320×240 8 bpp images in 8×8 VDP2 cells). See `docs/SATURN_VERSION_ANALYSIS.md` §4.0.
- `saturn_sh2.py` — SH-2 disassembler for the modules (`MODULE START [END]`, `--count N`, `--xref ADDR [--value]`, `--coherent BYTES`, `--selftest`). Every group is decoded by the bit pattern in the Hitachi SH-1/SH-2 programming manual; PC-relative loads print the slot **and the value in it**. `--xref ADDR` finds the loads of a literal slot, `--xref ADDR --value` the loads whose value is ADDR (how you get from "this global" to the code that touches it). **The first version of this tool was wrong** (it printed `JSR @Rn` as a store, mis-keyed groups 2/3/4 and `C8`/`C9`, and got the branch displacements wrong), and its claims that Ghidra's SLEIGH mis-renders stores and `MULS.W` were retracted with it. The old §4d/§4f readings it produced have been replaced; §4c's Ghidra listing was re-checked and is correct. Ghidra's SLEIGH is fine for these modules.
- `saturn_find_literals.py` — find a big-endian 32-bit literal in a module (`MODULE ADDR... --all`), which is how a data address is located in the PC-relative literal pools. `saturn_dump_table.py` then prints the table at a RAM address as records (`--stride N`, `--u16`) — counting hex by hand is how a 16-entry table read as 24.

**Asset and file inspection**
- `dump_tim.py` — parse a PSX TIM and write a viewable PPM.
- `gen_hd_pack_table.py` — pair the backgrounds and masks of the Classic REbirth HD packs with the game's own `Stage*/RC*.pak` / `Objspr/*.pak` by picture (mutual closest match under a threshold) and emit the migrator's `core/HdPackTable.inc`; it also pairs the packs' per-palette model skins with `Enemy/*.EMD` (Classic REbirth's CRC first, then by picture over the half each palette is drawn from), which the importer joins into the named `HD/<TREE>/Enemy/EMxxxx.webp`; the `Data/` textures the game names after their file (and the projector slides, `Data/SLIDE/<NN>`, and the USA file reader's pages, `Item_m2/FILEM_N0/<NN>`: their English pages are in the MulderLoad pack under Classic REbirth's own names); the pairs the user identified in game (`USER_*_PAIRS`, row kind `Named` for a `RoomModels` CRC or an upload hash); with `--jpn <JPN tree>`, the files only the Japanese tree has (the file reader's `Item_m2/FILEI*` backdrops); and, with `--dc <DC overlay folder>`, the Seamless HD Project's `bio1dc` set against the DC overlay (backgrounds, and the masks kept in the arrange room files as `ROOMxxxx_MASK<cam>`). Needs an ORIGINAL (not WebP-migrated) USA tree, the pack folders, numpy and ffmpeg.
- `gen_title_font.py` — cut the title-option typeface (A-Z; nine letters hand-drawn) out of the DC `BT367OAB.TIM` into a BMFont atlas and the migrator's `TitleFontGlyphs.inc`. The migrator's PC tab writes it into the base tree as `<USA|JPN>/Data/TITLEFNT.BIN`; `src/game/TitleMenuFont.cpp` turns it into the title menu's text strips for every mode (NEW GAME / LOAD GAME / EXTRA > BATTLE GAME / BACK / OPTIONS).
- `gen_hd_fonts.py` — the HD fonts, both Omnibus-Type under SIL OFL 1.1 and unmodified in `tools/asset_migrator/fonts/`: Archivo Black (`HD/<USA|JPN>/Data/TITLEFNT.TTF`: the title menu, PRESS START, the DC's STANDARD/TRAINING/ADVANCED) and Archivo Narrow SemiBold (`HD/USA/Data/FONTUS.TTF`, `HD/JPN/Data/FONT.TTF`: the 8x8 font and the title's copyright); and, as subsets it makes with `--subset`, Libertinus Serif SemiBold (`FONTUS_14.TTF`) and M PLUS 2 SemiBold (`FONT_14.TTF`, instanced from the variable font) for the 8x14 game text, drawn with the sheet's outline colour. Checked (TrueType, not variable, the characters each needs, licence in its name table) and embedded as the migrator's `core/HdFontTitle.inc` / `core/HdFontText.inc` (`core/HdFonts.h` writes them). The game draws them through `src/game/HdText.h` (vendored `src/third_party/stb_truetype.h`); docs/HD_TEXTURES.md "HD fonts". `--check` exits 1 when an `.inc` is stale.
- `gen_title_font_outlines.py` — the same 26 letters as hand-drawn vector outlines on the bitmap's grid, checked against it pixel by pixel, written as `assets/fonts/title_font.ttf` / `.svg`. No longer part of a migration (they were `TITLEFNT.VEC`); the `.ttf` can be copied over `TITLEFNT.TTF` to put them on the HD menu instead of Archivo Black. Re-run it after any glyph change in `gen_title_font.py`.
- `dump_ivm.py` — parse an `.IVM` item view (TIM + TMD), render its texture page to a PNG and print both headers.
- `dump_item_pix.py` — render rows of an item sprite sheet (`ITEM_ALL.PIX` / `Item_all_dc.pix`) through `STATUS.TIM`'s palette as a PNG grid.
- `dump_esp_sprite.py` — decode a sprite region of an effspr TIM (pixels + CLUT, with the STP bit).
- `pak_view.py` — view `.pak` background images (LZW → TIM → PNG/PPM).
- `dump_room_masks.py` — per-camera room-mask (overlay) sprite tables from RDTs.
- `dump_init_doors.py` — init-script `door_set` records and other room-action entries.
- `dump_init_scd.py` — an RDT's initialization SCD (header `+0x60`).
- `dor_disasm.py`, `door_script_parse.py` — disassemble / parse the `.dor` door-animation scripts.
- `evt_disasm.py` — an RDT's SCD *event* scripts (the cutscene VM, not the command stream `mine_room_scd.py` handles).
- `sim_zone_walk.py` — simulate the zone-graph walkers with the original's exact memory model over real RDT zone tables.
- `saturn_emd.py` — decode a Saturn `.EMD`: `info` (parts, joints, palettes, textures), `obj OUTDIR` (OBJ/MTL + PNG atlas, bind pose), `png OUT` (software-rendered front/back preview). `pc OUT.EMD` converts it to a PC `.EMD` (PS1 animation data + Saturn mesh as `0x34000609` triangles + a multi-page 8 bpp TIM; the page rides in each packet's pad halfword, read by `PSXObject_SetTexHeight` in the engine). Layout in `SATURN_VERSION_ANALYSIS.md` §5: a full PS1 EMD, then LZ77 SGL mesh + 4 bpp per-polygon textures + 16-colour palettes; Saturn units × 4 = PS1 units.
- `dump_emd.py` — report an `.EMD`'s shape and, with `--vs`, prove the Saturn relationship (16-byte big-endian index, then the PS1 OG file's first `0x800` bytes byte-for-byte, then the Saturn-only mesh). The EMD texture block is Capcom's own layout and is not decoded.
- `saturn_ranking_logo.py` — the Japanese Saturn's rotating "BIO HAZARD" results logo: an SGL model (9 XPDATA parts, 406 polygons) inside `RANKING.PRG` of **both** discs, drawn only by the Japanese module (`0x0602D2E4`; the USA copy `0x0602D33C` has no caller). `info [PRG]`, `png OUT [FRAME] [PRG]` renders a frame with the port's transform, sort and shading. Details in `SATURN_VERSION_ANALYSIS.md` §6b.
- `saturn_mbn.py` — decode the `A_*.DAT` room props (the two Tyrant capsules, the extra plants, the kitchen machine): `info` (container, parts, textures, palette, and a check that the sibling `A_*T.DAT` is exactly as large as the textures need), `obj OUTDIR`, `png OUT` (textured preview). The format is the `.EMD`'s SGL mesh **uncompressed** with no PS1 section, so the field layout is `saturn_emd.py`'s; the pixels are in the sibling `A_*T.DAT` and the palette is the mesh file's 32-byte tail. Layout in `SATURN_VERSION_ANALYSIS.md` §4g.
- `saturn_bss_to_pak.py` — Saturn room background (`.BSS`: 8 bpp + a per-camera RGB555 palette) → PC `.pak` (LZW → 16 bpp TIM), reusing `bss_to_pak.py`'s writer. `--tyrant-room` writes `assets/SATURN/Stage5/RC5130..5137.pak`; `--verify` decodes every Saturn camera frame and the PC pak for the same room **and camera** (through `pak_view`'s LZW) and reports the frames whose luma differs by more than 16 on >5 % of pixels — 115 of 655, plus 8 angles the PC has no pak for, which is how the Saturn's re-draw of the lab Tyrant room was found. Compare at **full resolution** against the same room+camera: the Saturn's palettes run a step dimmer and many rooms reuse another's art, so block means and stage-wide nearest matches both answer wrongly. Traps in `SATURN_VERSION_ANALYSIS.md` §4h.

**Sega Saturn (Resident Evil, USA)**
- `saturn_iso.py` — read the Saturn track-1 image (`MODE1/2352`, or a cooked 2048-byte `.iso`: the layout is detected) as an ISO9660 filesystem; `--jpn` selects the Japanese "Bio Hazard" image (`assets/Bio Hazard (J)(Saturn)/`, extracted to `assets/SATURN_JPN_ISO_CONTENT/`), `--track PATH` any other: `info` / `list` / `tree` / `info <path>` / `cat <path>` / `extract <dir> [--filter SUB]`. Extracts the disc to `assets/SATURN_ISO_CONTENT/`. **That extracted copy is incomplete: 101 files are shorter than the ISO directory says** (`SND/SDDRVS.PCK` 4,096 of 28,064 B, `ROOM*PCM.PCK`, `MOVIE.CPK`, the `CHR`/`JIL` banks, most `PCM*.PCK`); re-extract before trusting a size. `--filter` is a substring (`SND`, not `/SND/`).
- `saturn_snd.py` — the Saturn music packs: `pack` (CD_PACK segments), `ton` (voices, layers), `seq FILE [SONG]` (events), `scan DIR` (every pack). The format is Sega's sound driver Ver-2.10 (`SND/SDDRVS.PCK`, 68000; Ghidra `/AudioResearch/SDDRVS.PCK.0`) and is read out of the driver, which differs from VGMTrans's reading in three places — above all, **an event's step is the wait before it**. The renderer is `tools/asset_migrator/src/core/SaturnSound.cpp` (`re1am_selftest satsong` / `satkeys`); see `docs/SATURN_VERSION_ANALYSIS.md` §6a.
- `saturn_prg.py` — the disc's twelve SH-2 modules: `list` (all twelve, with their code entry offset and the data files each names) and `info <name>`. They are **big-endian SH-2 with no standard Saturn exe header** — import them as they are with `SuperH:BE:32:SH-2`. Do **not** use `SuperH4:LE:32:default` (the SH-4's FPU opcodes sit where an SH-2 has its data-movement ops, so everything decodes as float and analysis finds nothing) and do **not** byte-swap.
- `docs/SATURN_VERSION_ANALYSIS.md` — what is on the Saturn disc and how it differs from the PS1 OG disc; `docs/SATURN_PORT.md` — the port plan. The Saturn `.RDT` is the PS1 file LZ77-packed: decode it with `saturn_room.py rdt` and the `mine_room_scd.py` / `dump_init_scd.py` / `evt_disasm.py` / `dump_room_masks.py` family reads it as it is.

**Japanese (Biohazard.exe)**
- `jpn_font_table.py` — glyph map of the JPN font (`data\FONT.TIM`).
- `jpn_msg_decode.py` — decode / re-encode the JPN text tables straight from the JPN executable.
- `gen_jpn_text.py` — generate the C++ side from those two (`JpnTextTables.cpp`, `JpnFontTable.h`, and `test_str_jp.cpp`).

**Editors (open in a browser, no build)**
- `rdt_event_editor.html` — RDT event editor; `node tools/test_rdt_editor.js` runs its headless tests (add `--quick` for assertions only).
- `save_editor.html` — PC save-file editor.

**Linux** — `package_linux.sh` and `elf_needed.py`; both are described in the Linux port section above.

**Asset migration** — `tools/asset_migrator/` is a standalone Qt 6 GUI for the
player-facing asset import: a PC tab (a USA/JPN tree from a folder or a disc
image, optional AVI→MP4) and a Director's Cut tab (a PS1 image only, so the
`.STR` CD-XA audio survives; builds the `DC/` overlay, `.BSS`→`.pak`
backgrounds and `.STR`→`.mp4`) and a Sega Saturn tab (builds `SATURN/`: the Tick / Zombie Wesker models converted to PC EMDs and the 15 decoded Battle Game arenas; optionally `MOVIE.CPK`'s 28 FILM movies -> `SATURN/Movie/*.mp4`, `re1am_selftest satcpk` lists them). A window-wide **Room backgrounds** option leaves every tab's `Stage*/RC*.pak`
as `.pak`, or converts them to lossless / lossy WebP once the migration is done
(`core/BackgroundWebp.cpp`: ffmpeg's `libwebp` encodes, the game's own decoder
reads each file back; `re1am_selftest bgwebp <tree>` runs the pass alone). An **HD Pack** tab imports one `hires` folder of the Classic REbirth packs (Seamless HD Project, RE-ENHANCE) at a time - *Pack is for* Original game (`hires`) or Director's Cut (SHDP `bio1dc\hires`, into `HD/DC/`) - under the port's names: backgrounds and masks through a picture-matched table (`core/HdPackTable.inc`, regenerate with `tools/gen_hd_pack_table.py`), character/enemy skins through the same table into `HD/<TREE>/Enemy/<EMD>.webp` (the two palette pictures joined), everything else by matching the pack against the game's own dump (`HD/dump`, `[Textures] DumpTextures=1`); the packs' own CRC names are mostly not reproducible, see `docs/HD_TEXTURES.md`. It replaces the old `scripts/build_dc_assets.py`
and is **not** part of the game build. Its core is Qt-free C++17, so
`re1am_selftest.exe` runs it headlessly; see `tools/asset_migrator/README.md`.

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
