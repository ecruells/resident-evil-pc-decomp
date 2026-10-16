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
- **Zombie mod / asymmetric multiplayer (branch `playable_zombie`): active
  work.** See "Zombie mod" below — that is what current sessions work on.

## Project goal

The USA release is no longer the end of the project. The goal now is to **add
features from other RE1 releases** on top of this port, starting with the
Japanese MediaKite version. Extend behaviour; keep the existing USA behaviour
as the default so the two do not silently diverge.

On the `playable_zombie` branch the current goal is the zombie mod: a 1 director
vs 3 survivors multiplayer mode built on the port (see the section below).


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
- `verify_dc_item_models.py` — cross-checks the Director's Cut item-view mapping (`ItemModels.cpp`) against the generated DC item lookup and the shipped `ITEM_M2` art, and checks `g_ItemsImageBuffer` covers the DC sprite sheet.
- `verify_dc_entity_models.py` — the DC model table (`0x8008d03c`, via the Ghidra bridge) resolved to file names through the disc's ENEMY directory, entity id `0x16`'s handler read out of all 14 PS1 stage overlays, and the models `dc/EntityModels.cpp` names against the overlay.
- `verify_dc_save_mode.py` — recomputes every `BioCardLayout` offset from the struct and checks it against each field's own comment, the DC mode byte → flag-bit mapping against the PS1 load path, and the overlay font's four CLUT rows against the base glyph block and the PS1 colour columns.

**Asset and file inspection**
- `dump_tim.py` — parse a PSX TIM and write a viewable PPM.
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
- `estimate_zombie_route_time.py` — turns each seed's fastest escape into seconds (room walks at run speed from the RDT door/pickup positions, fixed door/pickup/crest costs; solo and a coordinated team); report `tools/zombie_route_time_report.json`.
- `gen_spawn_spots.py` — three monster spawn spots per mansion room (floor flood-fill inside the collision outline and camera zones, away from doors) and each room's monster cap by its floor area (`CAP_STEPS`); writes the zombie mod's `src/game/mods/ZombieSpawnSpots.cpp` (generated - do not edit by hand).

**Zombie mod assets**
- `convert_bumper_videos.py` — convert a folder of videos to the match bumpers' format (`docs/INFESTED_BUMPERS.md`): H.264 yuv420p MP4s at a constant 24 fps, silent except `wesker_victory` (or `--keep-audio`); clips in an `intro` folder are made exactly 72 frames, the rest are cut to 30 s.

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
backgrounds and `.STR`→`.mp4`). It replaces the old `scripts/build_dc_assets.py`
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

## Zombie mod (`src/game/mods/`)

An asymmetric multiplayer mode. Everything is gated on `g_bPlayAsZombie`
(`zombie_mode_armed()`), so the USA game is unchanged when the mode is off.

**Working with the user**
- **Never launch the game yourself.** The user play-tests by hand. Build it,
  then ask them to test. Nothing counts as fixed until they confirm it.
- Commit only when the user asks.
- Debug traces go through `RE1_DEBUGLOG=1` into `re1_debug.log`: `[shot pN]`
  shot audit, hits, monster hits and hurts. Ask the user for that log.
- Keep both `CMakeLists.txt` and `Game.vcxproj` updated. The Windows build has
  not been compiled since the multiplayer work started. Only Linux is built and
  checked with `tests/check_platform_boundary.py`.

**Design**
- **Roles** (`ZM_NET_OFF` / `ZM_NET_ZOMBIE` / `ZM_NET_SURVIVOR`):
  - Player 0 is the host and the **director**. The director possesses and places
    monsters.
  - Players 1–3 are **survivors**. Each picks a different character: Chris,
    Jill, Barry, Rebecca, Richard or Enrico (`ZombieNet.h` enum 0–5).
    `net_grant_char` keeps the picks unique.
  - Everyone plays Chris's scenario; the character picks the model and the
    perks (`ZombiePerks.cpp`): max health 140 + (toughness - 8) * 22 (Barry 10,
    Chris/Enrico/Richard 8, Jill/Rebecca 6), a starting kit each (Barry: magnum,
    no ammo; Jill: lockpick; Richard: the sheet music), Jill, Rebecca and
    Richard can play the bar's piano (`ZombiePiano.cpp`, below), Chris +50% world ammo pick-ups (rounded down;
    dropped ammo, including death drops, gets no bonus), Jill's lockpick
    opens sword-key doors, Barry's gun shots penetrate, Rebecca's spray heals
    survivors within 3000 (HEAL event), Enrico +10% movement, Richard's radio:
    OPTIONS opens the director's map look-only (`zm_map_set_read_only`) - which
    rooms have monsters; he stands still while it is up. Everyone also keeps
    the knife.
  - Single player is director vs an AI survivor (`ZombieSurvivor.cpp`).
- **No story.** The intro, title and in-game FMVs are skipped
  (`zombie_mode_skip_intro`, `cmd_fmv_set`). Story events are not started:
  `cmd_scd_event_create` asks `zombie_mode_skip_scd_event`, which scans the
  event script (and the events it starts) for a voice line or a movie and, if
  found, makes its flag writes (scenario/lock/enemy/item banks) instead.
  A monster's entrance is skipped too, voice or not: an event that takes the
  control (message flag 0x100 cleared) and drives an enemy (back passage
  Hunter, 2F small library, attic). A skipped scene's own script also has its
  main-state/message flag writes and `room_action_arm` made in order (control
  comes back - the roofed passage's room script takes it before the radio
  call) and a player left in state 8 by the room script is stood back
  (`zm_evt_release_player`). Walk-in zones (`create_room_event`, probe flags
  without 0x80) are filtered the same way (`zombie_mode_skip_room_event`);
  action-press events (stairs, puzzles) still run.
  The new game's opening narration (global message 0x5B, "They have escaped
  into the mansion...") cannot be skipped in the mode (the original's any-key
  skip in main_loop is off) and its timed pages run at 70%
  (`zombie_mode_message_page_delay`); game_loop's wait for it before its first
  frame pumps the link (`zombie_mode_reconnect_wait`) - it used to go silent
  long enough for the host to start a survivor's disconnect grace. Untested
  by the user.
- **Messages without the pause** (`ZombieMessages.cpp`): a message that asks
  no Yes/No (locked doors, desks, examined objects, no ink ribbon...) leaves
  `g_message_flags` alone - the survivor keeps control and the room runs. It
  types out on the bottom line, turns pages and closes by itself after
  0.7 s + 25 ms a character (1.2-4 s); the buttons do not touch it, except
  one an event holding the control waits on (Action turns it on). Text types
  4 characters per tick in the mode (`zombie_mode_message_chars_per_frame`).
  A grab, hit or death closes a passive message at once. A different
  message replaces it, a door or the menu closes it (`zombie_mode_message_drop`
  in game_loop), and the same one is not restarted for 1 s after. A Yes/No
  prompt (take an item, use a key, save) clears only 0x0140 instead of 0xff:
  game_loop cuts the pad to the menu bits (the script "took the control"
  path), so the survivor settles idle while monsters, effects, events and its
  STATE keep running; a grab or death answers No; closing raises back only
  those two bits. Menu and door-script messages still pause.
  A door key is used up as it turns (`zombie_mode_key_turned` in
  door_try_enter), not when its message closes; on its last lock it is
  discarded without the inventory's "This key is useless now. Discard?"
  (`zombie_mode_key_used_up`, both KEY_DEPLETED sites), and "The <key> is no
  longer useful. You discarded it." shows once the survivor has the control
  with no other text up - in the next room when it walked straight through.
  Untested by the user.
  Examine events (`zm_evt_is_examine`: an action press's script that only
  takes the control, cuts to a close-up, shows a message and cuts back - no
  entity, event, door or item command; e.g. ROOM61A0/6050/6170/7010/7050)
  get the same passive message even with pause 0, and the control back as it
  starts. The close-up stays until the survivor moves or turns, then the
  camera returns to the one before it (`zm_fix_camera_at` after), and that
  event's later cut commands are dropped (`zombie_mode_cut_skip`). Desks
  (`check_desk`) still pan and open the take screen. Untested by the user.
  Events never stop the world in the mode: an SCD event's bit_op clearing
  message flags 0x01/0x04/0x08 (player, monsters, effects) is dropped
  (`zombie_mode_event_flag_clear` in `cmd_bit_op`), only the control (0x100)
  is taken - the survivor idles and grabs/hits land (their GRAB/PHURT wait on
  0x04). Every Yes/No prompt, a script-held one too, is answered No by a grab
  or hit. The skipped scenes' flag replay is outside any event and unchanged.
  Untested by the user.
  Reveals (`kReveals` in ZombieMessages.cpp) play as an examine close-up:
  the 1F dining room (ROOM6050 ev1), the bar's emblem cabinet (60F0 ev10/11),
  the bathtub (6130 ev1), the large gallery's solve (6170 ev20/21), the 1F
  study switch (6190 ev0), the crest door (11A0 ev3-6), the armor room
  (7050 ev1), the 2F study shelf (70A0 ev1), the stove (70B0 ev0), the
  trophy room switch and eye (7150 ev0/4), the private library (7170 ev1).
  Events a reveal starts or re-inits are reveals too (one close-up group).
  The control is not taken, the player is neither posed nor placed
  (`zombie_mode_event_pose_skip` in the event VM's state 1 raises a pose's
  end flag itself; `zombie_mode_player_pos_skip` in cmd_player_pos_set), and
  moving, turning or being hit/grabbed cuts back to the camera before the
  first close-up; the group's later cuts are dropped while it runs on.
  The pickup close-ups a monster can walk in on - the bathtub (6130 ev4)
  and the attic (7100 ev1) - are in the list too. The heliport lookout's
  (7180) and the rope (70C0) are not. Untested by the user.
  An Action press never starts an examine/reveal still running (it used to
  restart it in its slot: the close-up re-locked onto itself and stuck, and
  each run asked for the pickup again), nor any pickup or event while a
  close-up is up (`zombie_mode_action_busy` in check_action_object). A hit,
  grab or death cuts every running examine/reveal short: its close-up cuts
  back and its later pickup (`cmd_room_action` handler 4/8, `cmd_got_item`)
  is dropped (`zombie_mode_event_room_action_skip`). Untested by the user.
  The letterbox (main-state `MSF_INTENSITY_RAMP`, `bit_op 05 0F`) is not
  turned on by an examine or a reveal (`zombie_mode_event_letterbox` in
  `cmd_bit_op`): those can end without the command that takes it away (the
  armor room, the large gallery's solve), leaving the bars up. Other scenes
  keep theirs; up for 30 frames with no SCD event running, it is taken away
  (`zm_letterbox_frame`). Untested by the user.
- **The return mansion.** The mode always plays stages 6/7 (0-indexed 5/6):
  `zombie_mode_new_game` sets `SCENARIO_FLAG_STAGE_VARIANT`. The first visit
  carries 14 of the mansion's 58 rooms as 4-byte stub RDTs.
- **Randomized scenario** (`ZombieRandom.cpp`): one seed per game (the host's,
  or the clock's in single player), and every copy builds the same scenario
  from the RDTs. Which mansion key opens each key-locked door (14 locks, by
  lock flag - 13 of the mansion's and the large gallery's door, which the
  mode locks itself: `kAddedLocks`, lock flag 0x28 on both sides, patched
  in `cmd_door_set` (`zombie_mode_door_record`), the route scan and the AI
  survivor's door cache; untested by the user), where the keys and the four crests lie (assumed fill over the
  23 pool spots - top-level, un-armed item_model_set records that held a key,
  crest, weapon or ammo), and the weapons/ammo over the rest. Puzzle and
  one-way locks start open (all but the crest door, lock flag 23). Hooks:
  `cmd_item_model_set` → `zombie_mode_item_spot` (patches the record's item
  and quantity), `door_try_enter` → `zombie_mode_door_need`. The lobby's
  route map (Aim) shows the key doors and which key each takes; not where
  the keys lie. `[random]` lines in the debug log list the scenario.
  - Looks: a spot whose item changed shows the new item - its inventory view
    model (`item_m2/iNNv.ivm`), rescaled to a size for its kind, stood on the
    floor (turned flat when it is tallest along y), its texture on the mod's
    page counter (`zombie_mode_item_look` in `cmd_item_model_set`). A record
    of the room's own turns the new look a quarter when its long side runs
    across the room model's (x against z), so it lies along the shelf or
    table the original did (`rnd_match_orientation`); it also moves onto
    the room model's centre and bottom, and a raised spot (record y above
    -100) keeps its footprint inside the smallest solid collision box
    under it, moved in only as far as needed (`rnd_keep_on_furniture`,
    `[random] look kept on the furniture` in the debug log). Untested by
    the user.
  - The item viewer (pickups and the inventory's CHECK) runs at four steps
    a frame in the mode (`FUN_0044e1b0`'s `pickupAnimStep`).
  - Route proof (`rnd_reach`): doors whose trigger a script can switch off
    follow the reviewed `kDoorGates` table (unlisted ones count as closed);
    a room counts only if the main hall is reachable back from it; keys,
    crests and puzzle tools stay out of the furniture rooms in
    `kUnverifiedRooms`. See `docs/INFESTATION_PUZZLE_ACCESS.md`.
  - New spots: 16 rooms a game get an extra pickup on one of their spawn
    spots, under a roomItems flag no mansion script uses, as an
    item_model_set record of our own run through `cmd_item_model_set`
    (`zm_random_room_loaded`, from `zombie_mode_room_spawn`); the RDT's
    `item_count` is raised so `render_room_objects` draws it. They are in the
    pool; spots left over after keys, crests and weapons get supplies.
  - Key items in leaf rooms (rooms with one neighbouring room, `s_leaf`,
    `rnd_key_item_room`; not a puzzle's room, the shotgun rooms, the
    storeroom or an unverified room): every such room gets a new pool spot
    first; keys and crests take a leaf room each when one is free, then the
    broken shotgun and the pick axe (route cost `RND_TOOL_NEAR`..`FAR`, 5..9
    crossings, `rnd_route_cost`), the sheet music and the chemical, each in
    a leaf room of its own (`rnd_place_key_items`). The bar's own sheet
    music record becomes a supply.
  - Chris's-room gates (`rnd_chris_gate`): an `if` whose only condition is
    "Chris's version of the room" (MSF_CHAR_VARIANT clear) always runs in
    the mode, so its item records count as top level and its else (Jill's)
    is left out - for the item spots only; doors keep the plain nesting.
    The small key opens nothing in the mode (`check_desk` lets everyone
    in), so its spots take items too: the terrace passage's (ROOM7110,
    flag 0x95). The storeroom's (ROOM61B0, flag 0x22) stays a small key:
    its frame script re-arms that slot, which keeps it out of the pool.
    The 1F 0x18 room's Chris clip joined the pool the same way.
    ROOM6130's (the bathtub, flag 0x03, gated on the drained-tub story
    flag, so outside the pool) becomes a supply (`rnd_bathtub_spot`).
    A room script testing for its spot's original item (`picked_item_test`:
    the bathtub's small key, the shed's battery - each disarms its pickup
    scene once taken) also passes for the spot's new item
    (`zombie_mode_picked_original`). Untested by the user.
  - Outside the pool: the five ink ribbon spots (main hall, gallery,
    wardrobe, 2F study, heliport lookout), the sheet music and the
    chemical become supplies (the supply table holds no ink ribbons:
    saving is no use in the mode); the main hall's Beretta (flag 0x1A) and the
    vacant room's broken shotgun (flag 0x30) are emptied - the spot's
    roomItems flag is cleared at `cmd_item_model_set`, so the record runs
    as for a taken item (no model, no pickup, the room's taken-state
    script). The generator's leaf-room broken shotgun is the only one.
    Untested by the user.
  - The 2F small dining room's (ROOM70F0) shells, armed only once Chris's
    lighter lights the candles, are the Ingram with 150 rounds
    (`rnd_candle_spot`, `RND_INGRAM_ROUNDS`) - a prize only Chris reaches
    (the lit candles are a story flag, so anyone can take it after). In the
    mode it runs dry: a round a shot in the hold-fire, no top-up in
    `weapon_autoaim_check`, its count shown instead of the infinity
    (`zombie_mode_ingram_finite`). Handgun damage per bullet (9 a zombie,
    14 a Hunter, 20 Cerberus/Chimera/Tyrant), a bullet every 2 frames while
    fire is held. Its floor look is `ING.ivm`, 625 long (the Minimi 1500), laid on its side
    (`rnd_rescale`'s quarter turn about z for the special weapons). Its in-hand mesh
    (players/w18.emw, every block's) is cut for Jill's texture sheet: on any
    other body - the player's own and the stand-ins - it reads Jill's sheet
    (char11.emd's TIM, loaded on demand into two mod banks a room,
    `zm_jill_sheet`). Droppable (`zm_random_has_look`), picked up with the
    item handler (cmd_item_model_set gives ids 0x6F/0x70 handler 4 in the
    mode). Untested by the user. That room's two pickups have no model of the
    room's own (the background shows them) and a zero position, so a
    changed item is drawn on the floor in front of its furniture
    (`kLooseSpots` in `zombie_mode_item_spot`). Untested by the user.
  - Heavy weapons come from five puzzles only (`kPuzzles`: piano bar
    alcove, greenhouse past the plants, tiger statue's red gem, large
    gallery portraits, armor room), each in its own reward slot.
    `rnd_rank_puzzles` orders them by route cost to the puzzle room through
    the room of the item it takes (keys picked up on the way; Richard's
    sheet music ignored): the nearest two a grenade launcher or flamethrower
    each, the next two the Python, the farthest the rocket launcher - never
    the piano. Loads: Python 6, launcher 6 explosive, flamethrower 240,
    rocket launcher 4. No heavy weapon lies on the floor. Tier 1 (handgun,
    shotgun) lies anywhere. Ammo lies only for the heavy weapons in the game
    (magnum rounds also with Barry, the three grenade kinds, fuel 120).
    `rnd_weapons_refresh` deals the floor's weapons and ammo again while
    Barry's presence changes, until every seated survivor's STATE has come
    (`rnd_party`). `[random] puzzle` lines.
    The flamethrower's reload keeps its full count in the mode
    (`menu_ammo_loaded` in MainMenu.cpp; the original keeps 7 bits).
  - Ammo doubling: up to 48 more new spots (a room takes up to 3 new spots,
    one per spawn spot) are held back for ammo only; `rnd_fill_ammo` puts as
    many ammo pickups on them as the scenario already placed (`[random] ammo:`
    in the debug log) and leaves the rest empty. Pickups themselves are not
    doubled.
  - The back area (`ZombieKeypad.cpp`): the 2F back passage (7:13) and the
    rooms off it (rough passage, libraries, shed) are reached only by the small
    elevator from the kitchen or by the 2F left stairs' keypad door into the
    rough passage. The elevator's two doors say it has no power until a
    survivor USEs the battery near either; the keypad (ROOM7010 slot 5, the
    original's key panel event) takes this game's 4-digit pass number
    (`zm_random_pass_code`, from the seed). A note (`ITEM_ZM_PASS_NOTE` 0xF0,
    a new spot drawn as the red book, read where it lies, never taken) shows
    it and puts it on that survivor's route map. Both stay open for everyone
    (ScenarioFlags2 0xF8 power / 0xF9 keypad: STORY sync and the reconnect
    snapshot). The rough passage has no door back natively; an open keypad
    builds one over its locked-door zone. The route (`kAccessDoors`,
    `RND_BIT_BATTERY` / `RND_BIT_NOTE`): the back area is computed from the
    graph (`rnd_find_back`); the battery (out of leaf rooms when it can, and at least 4 door crossings from
    the kitchen's elevator - `RND_BATTERY_AWAY`, locks ignored) and
    the note (leaf room) lie outside it, one crest always lies in it (its
    room picked evenly, placed first in the fill), no key does - so either
    way in alone is enough. Each back room without a pool spot gets a new one.
    The director's monsters ignore both; the AI survivor treats them as shut.
    Untested by the user.
  - Shotgun puzzle (`ZombieShotgun.cpp`): first-visit trap/living RDTs restored
    under return-stage IDs. No keys/crests in either room. One broken shotgun
    and one Pick Axe lie in distinct keyless rooms 3–8 door crossings from the
    hall. USE the broken shotgun at the empty mounting plate; Action at the
    passage-side trapped door with the pick axe rescues living occupants and
    the rescuer through a black-screen transition. Host-authorized event 31,
    reusable pick axe, 60-second ceiling timer and persistent broken door.
    State and committed replacement/rescue reconcile on reconnect. CPU tests:
    `tests/test_zombie_shotgun.py`; user play-testing still required.
- **Yawn 2 in the lesson room** (`ZombieYawn.cpp`, `kBoss`): placed by the
  mode as for Yawn 1 (the room's own enemy_set, id 0x12 death flag 0x3E, is
  refused like every room monster), behaviour 0x06 at (5300, 17500) facing
  0xC00 - the scripted selector, which never flees (the flee path is the
  attic's): it dies at 250 health per living survivor. The lesson room's own
  sounds. Its crest: the generator puts one crest (never the back area's) on
  a lesson room pool spot (`s_bossItem`, the room gets a new spot if it has
  none), held back until it is beaten. Its death opens the hole to B1. The
  same music, locked doors and lockdown as Yawn 1's. Untested by the user.
- **Blue herbs in the safe rooms** (Yawn poisons): one in each, the same
  every game and out of the random fill (`rnd_add_herb_spots`). The 1F save
  room (0x00) gets a spot of its own on its first spawn spot; the mansion
  storeroom (0x18), which takes no extra spots, has its emptied shells' spot
  (flag 0xDA, `kTrimmedSpots`) hold it (`rnd_place_weapons`). One pickup each
  per game. Untested by the user.
- **Yawn's moves for several survivors** (Yawn.cpp actions 9 and 10, past
  the original's 8; `zombie_mode_yawn_special` picks them in both selectors,
  before the flee). The thrash: a hiss (anim 5, 24 frames), then the head
  whipped side to side (anim 6 with the head's angle swung 0x280 each way,
  two swings in 40 frames) while the body is dragged after it with a long
  step, striking everyone within 1300 of any body joint at each swing's end
  (20). Chosen with two survivors within 2500 of the body, or one along its
  back half; 9 s cooldown. The slam: the unused reared weave (anim 12, 45
  frames) toward its target, then the rear-up backwards at double speed;
  strikes within 2000 of the head and front body (30) with dust; chosen now
  and then against a survivor 2500-7000 ahead; 12 s cooldown. 3 s between
  any two. Both hold +0x17C (no flinch) and ignore_player.
  Area strikes (`zombie_mode_yawn_area_hit` -> `zm_area_resolve`, after the
  target swap ends): every living survivor in the room in reach
  (`zm_room_survivors`) gets the bite's reaction - this copy's own through
  `zm_take_hurt`, the others' by PHURT (700 ms per survivor). Untested by the
  user.
- **The director in a Yawn** (`zm_yawn_possessed`): a boss room with its
  Yawn alive can be jumped into to take it (`zm_yawn_jump_room` in
  `zm_jump_to` and `zm_world_room_zombies`); placing, traps and doors there
  stay refused. Forward crawls (Run: faster), the turns steer, Action
  bites, Aim + Action thrashes, Run + Action slams; START / the map as for any
  monster. Its decision tree is held off (ignore_player at 1). Beaten (Yawn 1
  under its flee health - the flee is started - or Yawn 2 dead), it goes
  back to its own AI and the director to the next monster
  (`zm_possessed_died`). Its body segments are never possessable
  (`zm_possessable`). A human director (single player too; not an AI
  director's game) is made to: once a living survivor is in a boss room with
  its Yawn alive, its copy jumps there and takes the snake as soon as nothing
  holds it (`zm_yawn_force_jump` in `zm_director_input`), and stays until it
  is beaten - no START switch, map jumps refused ("THE FIGHT HOLDS YOU TO
  YAWN", `zm_yawn_director_bound`). Untested by the user.
- **Going in together** (`zombie_mode_boss_door_gate`, before
  `door_begin_transition` in `door_try_enter`, after any key): a boss room's
  open door takes nobody alone while its Yawn waits. Every living survivor
  must stand within 3000 of the one at the door, in that room, else "SOMETHING
  FEELS WRONG - GATHER YOUR TEAM". Gathered, the host asks them all ("ENTER
  THE FIGHT? ACTION YES - AIM NO", 15 s, the survivor held still, a fresh
  press needed); all yes, every copy goes through its own room's door to the
  boss room at once (`yw_go_frame`, the shotgun rescue's way), each then
  moved (`yw_take_place`, at room spawn) to its place by the door: in seat
  order among those asked, the n-th of `kTeamPlaces` (the arrival point, 800
  either side, a step in, ...) the room's walls leave clear - the same on
  every copy. A no, someone
  dead, moved off or out of the room, or the time running out calls it off
  ("ENTRY CALLED OFF"). One living survivor goes straight in; single player
  has no vote. ZM_EV_BOSS ops 3 request / 5 answer (survivor -> host), 4 ask
  / 6 go / 7 off (host -> all), 8 refused (host -> requester). Untested by the
  user.
- **After Yawn 2:** the lesson room's door to the front lesson room is shut
  for good both ways ("THE DOOR IS BLOCKED", `zm_yawn_lesson_sealed` in
  `zombie_mode_door_trapped` and `zm_door_usable`); the way out is the hole,
  down through the basement (B1 passages, the kitchen) - a choke point for
  the director. Beaten while the room is loaded, ROOM70C0 event 6 slides the
  shelf off the hole (and arms the way down, sets ScenarioFlags 0x28; 0x27 set
  with it). The generator: that door sealed (`kSealedDoors`), the hole open
  downward only (`kDoorGates`); every seed verified to lead from the lesson
  room back to the main hall and the exit. Untested by the user.
- **Boss lockdown** (`ZombieYawn.cpp`): a Yawn beaten (its death flag up),
  its room stays shut 60 s (`YW_LOCKDOWN_MS`, "REGROUP - DOORS OPEN IN n"); a
  revive there needs no healing item and ignores the limit
  (`zm_yawn_free_revive` in ZombieSpectate.cpp). At its end every survivor's
  revives are counted afresh (`zm_revive_reset_counts`) and the clock gains
  5:00. Elsewhere the director plays on through the lockdown (places,
  possesses, jumps, traps) but earns nothing - no income, hit bonus or
  refund (`zm_yawn_lockdown` in ZombieEconomy.cpp; settled at the
  lockdown's start and end; the HUD reads "HELD n PTS"). Untested by the
  user. The director cannot place, jump, trap or send reinforcements into a
  boss room until it is done (`zm_yawn_director_closed` in
  `zm_director_safe_room`, the AI director, reinforcements, the map - "BOSS",
  dark). The host decides: BOSS=35 `{1 lockdown, boss, tenths, seed low,
  seed high}` / `{2 done, boss, 0, seed low, seed high}`; a survivor's copy
  that sees a death flag but no word for 40 s counts it done. Untested by the
  user.
- **Yawn 1 in the attic** (`ZombieYawn.cpp`): the attic's own enemy_set
  (id 0x0D, slot 0, death flag 0x18) runs only on the first visit
  (ScenarioFlags bit 0 clear - `bit_test`'s third byte 1 passes on a clear
  bit), so the mode places it itself after the init script (`zm_yawn_room`,
  `zm_spawn_monster`) as that record's "already in the room" version
  (behaviour 0x02 at (11000, 10000)), on every visit until the flag is up. The
  return attic's sound row has no Yawn; it takes the first visit's
  (`zm_monster_sound_row`). `yawn_init` leaves ignore_player (+0x85) set -
  the original clears it at the end of the ceiling entrance - so it is
  cleared while the head idles or crawls (`zm_yawn_after_update`) and the
  snake hunts at once. The fight plays `Bgm_08` looped (the first-visit
  attic's music group; the return attic has none), faded out over 3 s once it
  has fled. While it is in the loaded attic a survivor's door out is refused
  ("THE DOOR WON'T OPEN", `zm_yawn_traps_exit` in `zombie_mode_door_trapped`
  and the AI survivor's `zm_door_usable`). It runs on its own engine AI; the original's flee at health
  under 0x0AF0 is the defeat (the flee raises the death flag). Health starts
  250 above that per living survivor. While the flag is down the attic's
  randomized key/crest pickup is held back - no model, no sparkle, its pickup
  and ROOM7100 event 1's close-up refused (`zombie_mode_yawn_guards` in
  `check_action_object` and `cmd_room_action`) - and comes out as the room's
  init set it once the flag is up (here or through the flag merge). Attic
  event 0 (the after-flee scene) is skipped. Yawn's 13 slots (head 0, body
  segments 1-12 from `yawn_init`) are reserved from the extras
  (`zm_room_highest_script_slot`) and kept out of the economy, stun, roster
  capture, monster counts, the director's hit credit and the corpse hand-over
  (segments keep the original's health -1). Across copies the owner sends the
  head as an ENEMIES entry plus a `ZmYawnBody` after the entries (header flag
  2: joints 3-14 world positions, the 13 slots' status bytes); other copies
  run `yawn_init` once and copy the pose (`zm_yawn_puppet_update`), forwarding
  head and segment shots as HIT. In a networked match the swallow is off and a
  bite can kill (no victim handoff, as the Hunter's drag); single player keeps
  the original. `[yawn]` lines in the debug log. Untested by the user.
- **The lesson room's hole** (lesson room 2F 0x0C <-> B1 passage 1 2F 0x1A) is
  shut until Yawn 2 is beaten (`zm_yawn_hole_open`: its death flag 0x3E; Yawn 2
  is not in the mode yet, so all game for now). `zombie_mode_room_prepare`
  clears ScenarioFlags 0x27/0x28 (floor whole, no way down) instead of setting
  them, B1 passage 1's climb-up prompt (slot 2, event 0) is refused, and the
  AI survivor's `zm_door_usable` and the generator (`kDoorGates`: both records
  closed) count it shut. B1 stays reachable from the kitchen. No progression
  item lies in the lesson room (`rnd_boss_room` in `rnd_key_spot_ok` and
  `rnd_key_item_room`): it is Yawn 2's arena. The lesson room's way down
  (slot 7, event 12) is refused too. Verified over 2000 seeds; untested by the
  user.
- **Key roles** (`rnd_generate_once`): which key type plays which role is
  random. Key 1 opens the pillar passage's door (lock 0x0D) and lies where no
  key is needed; key 2 opens the attic's door (lock 0x07) and lies behind a
  key-1 door; key 3 lies in the attic (its only key or crest), held back until
  Yawn flees, and opens at least one door; key 4 opens the lesson room's door
  (lock 0x19) and lies where only key 3 reaches. The other locks take any key.
  So exactly two keys are in reach before the attic fight. Verified over 2000
  real seeds (keys 1 and 2 one at a time, then the attic, then key 4).
  `tests/check_randomizer_progression.py` (a synthetic star of 1F rooms, no
  attic) predates the attic rule and fails; it needs a new fixture.
- **Win condition:** a survivor opening the storeroom's (stage 1 room 0x1B)
  door out to the courtyard requests an escape from the host (`zm_is_last_door`).
  Only the host confirms the final result: escape=0, timeout=1, all-dead=2
  (`zm_all_survivors_dead`; departed survivors no longer count). Received
  escape requests precede timeout/all-dead checks in the same host update;
  later requests cannot reverse a confirmed outcome. Survivors wait for the
  host's result, including its elapsed time. WIN=9 requests are private
  `{player,0,exit room,health,seed low,seed high}`; confirmations are
  `{winner/-1,reason,elapsed seconds,seed low,seed high}`. Critical results
  bypass the normal inbox and are retained if game initialization is unfinished.
  Seed checks exclude old-match results. Untested by the user.
- **Dead survivors stay in the match:** `zombie_mode_hold_death` keeps
  game_loop from ending the game on a survivor's death; its copy keeps
  sending STATE.
- **Spectating** (`ZombieSpectate.cpp`): 4 s after its death a survivor's
  copy watches a living survivor (left/right picks the next one): it loads
  that survivor's room by a door record of its own, as the map jump does,
  and the camera follows the watched position (`zombie_mode_camera_target`,
  `zm_fix_camera_at` on arrival). A door taken by the watched survivor is
  followed after 400 ms. From the start of the watching its STATE is frozen
  on the corpse (`zm_net_freeze_state`: stage, room, spot, pose) with flag
  0x10, so the body stays where it fell on every copy; STATE also carries
  the room the copy has loaded (`viewStage` / `viewRoom`). A spectating
  player is never "here" (`zm_player_here`) and never the owner (with
  nobody else in the room, its copy runs nothing); a room's owner sends
  ENEMIES for a spectator watching it (`zm_spectated_here`). It counts gone
  from the corpse's room once it has left it (`zm_spec_room_exit`, after the
  room capture), or at once when someone else is there to run it. Its pad
  is cleared (the hidden player entity stays put) and its story-flag watch
  is off (`zm_world_story_rebase`). `[spec]` lines in the debug log.
- **Reviving** (`ZombieSpectate.cpp`, multiplayer survivors only): hold Action
  within 800 units of a frozen corpse for 5 s, consuming a First Aid Spray;
  Rebecca takes 2 s and can also use a green herb. Returns at 25% max health
  (Rebecca: 50%). No deadline after death; once per survivor per match.
  Release, hurt or grab cancels. REVIVE=21 carries `{target player, health}`.
  The host reserves a target before the item is consumed: negative health is
  a private reservation request, zero cancels/refuses, and the positive commit
  is broadcast (including an echo to the reviver). Simultaneous revivers spend
  only the winner's item.
  The target returns from spectating to the corpse room, unfreezes STATE and
  runs player state 0 to restore normal control; the stats death time clears.
  A revivable corpse still counts dead for the director's all-dead win.
  Tuning constants are together in ZombieSpectate.cpp. Untested by the user.
- **End screen** (`ZombieStats.cpp`): every copy fades to white and shows
  YOU WIN! / YOU LOSE / GAME OVER, how the match ended and the stats - the
  director's points spent and earned, monsters bought and lost, traps, hits
  on survivors (`zm_econ_stats`); each survivor's fate (died at m:ss /
  escaped / alive), hits, kills and damage taken. Each copy sends its own
  player's numbers once the match is over (STATS). Action (after 4 s) or a
  minute leaves it through the death fade, without the death screen
  (`zombie_mode_match_over` in die_state).
- **Game clock:** 12 minutes (`ZM_TIME_LIMIT_MS`) from the moment every
  survivor is in (`zm_survivors_in_ms`), shown bottom right on every copy
  (the hall countdown sits a line above it), +5:00 for each Yawn beaten (at
  the end of its lockdown, `zm_clock_add_bonus`). It stands still ("PAUSED
  m:ss") while every living survivor is in a boss room with its snake alive,
  and during a lockdown (`zm_yawn_clock_hold`). The director's copy (and
  single player) keeps it and sends CLOCK `{seconds left, standing still,
  seconds run}` every 5 s and on every hold or bonus; a survivor's copy counts
  down from the last one. Untested by the user. At zero the director wins: WIN
  `{-1, 1, ...}` (an escape is `{winner, 0, ...}`), a "TIME IS UP" banner, then the
  same end.
- **Disconnect recovery** (`ZombieNet.cpp`, `ZombieReconnect.cpp`): five seconds
  without gameplay packets starts a 30-second grace period. All copies pause
  simulation and gameplay timers while continuing network polling. The original
  client reconnects automatically; a crashed survivor uses New Game → REJOIN
  THE LAST MATCH on the same computer/save directory: `infestation-rejoin.dat`
  holds the host, the seed and the survivor's match id, and the host gives
  back the seat that id was seated under, in that seeded match only. A new connection epoch rejects
  delayed packets from the old process. The host sends a retried chunked snapshot
  with the player's last received inventory, room/position, health/death, stats,
  rescue usage, shared flags/box, monsters and drops. Pending committed pickups,
  drops and rescues are reconciled before restoration; transient animations and
  menus restart. Resume waits for the restored room to finish loading. On expiry
  the seat is removed and the host drops its carried supplies at the last known
  position (subject to existing drop-list/room capacity). No host migration or
  host crash persistence: clients wait 30 seconds, then show CONNECTION LOST.
  CPU regressions: `tests/test_zombie_reconnect.py` and
  `tests/test_zombie_reconnect_snapshot.py`. User play-testing still required.
- **Networking** (`ZombieNet.cpp`) is a UDP star through the host. Per-link
  reliable events carry a source and a destination, and the host relays them.
  - Packets: HELLO, WELCOME, GO, STATE, PING, BYE, FLAGS, ENEMIES, READY, START.
  - Event kinds: HIT=1, GRAB=2, GRAB_END=3, ROSTER=4, SOUND=5, FX=6, PSND=7,
    PHURT=8, WIN=9, BURST=10, HEAL=11, STUN=12, STORY=13, CREDIT=14, TRAP=15, BOX=16, CLOCK=17, DROP=18, DROP_TAKE=19, STATS=20, REVIVE=21, PIANO=32, ROOMSYNC=33, TIMEOUT=34, BOSS=35, VOICE=36. FX, SOUND, PSND, BURST, HEAL and STUN carry
    `stage|room<<8`. PICKUP=30 is the host's exclusive pickup transaction.
  - **Bump `ZM_NET_VERSION` (currently 72) whenever a packet layout changes.**
  - Lobby version mismatches are decoded from the stable magic/version/type
    prefix, before parsing the full packet layout. The client shows GAME VERSION
    MISMATCH with an update hint; a nonresponding host shows address/port/version
    troubleshooting guidance. Quick debug remains developer-only.
  - WELCOME / GO carry the host's scenario seed (`zm_net_seed`), the lobby
    phase, the veto flags and the survivors' votes; a survivor's HELLO carries
    its character, its vote, the lobby revision (a vote counts only for the
    map it was cast on) and its match id - two words the survivor draws when
    it joins; the host seats it under that id and a rejoin's HELLO presents
    it again to reclaim the seat. Picks stay open after GO until that survivor
    sends READY; the host no longer waits at the start barrier.
- **Game start flow** (`ZombieLobby.cpp`): every choice is a menu item
  picked with Action (Cancel backs out); Aim is not used. The host hosts and
  the survivors join; the join screen lists the director and the survivor
  seats; the host picks GENERATE THE MAP (or, against the AI director,
  CONTINUE TO THE MAP - also the AI level and the timeout switch there).
  The map review: every copy builds the seed's scenario and shows the route
  map: key doors (blue) and where the keys and crests lie (purple); the
  director's also shows every new pickup (teal). The arrows browse rooms,
  Action opens a popup menu (`zm_map_set_menu`). Against a human director
  each side may veto once (the survivors' veto needs every survivor), which
  re-rolls the seed, and the director's START THE GAME is open once every
  survivor accepted or after 90 s; the director plays at once - no
  narration, intro or barrier wait on its copy (`director_plays_at_once` in
  ZombieBumpers.cpp); the host runs the survivors' intro barrier from their
  reports, and the clock starts once they are all in and released. The survivors
  then get the character select and the pregame lobby: the survivors and
  their characters, CHANGE CHARACTER, VIEW THE MAP and the countdown; all
  spawn in the main hall when its two minutes run out. Against the AI there
  are no votes: everyone (the host too) goes from the map to the character
  select and the pregame lobby, where survivors toggle READY and the host
  has START THE GAME (asked twice with someone not ready) and BACK TO THE
  LOBBY. Untested by the user.
- **Survivors' timeout** (`ZombieTimeout.cpp`, multiplayer): once a match,
  from 5:00 on the game clock, a living survivor calls it from its map
  (OPTIONS, then Run twice within 3 s) while every living survivor stands in
  a safe room (`zm_random_room_safe`, not in a door). The host checks the
  same from STATE and holds the match for 60 s on every copy: it is a second
  cause of the disconnect pause (`net_has_pause` = link pause or
  `zm_timeout_active`), so the game clock, the director's points and every
  `zm_game_time_ms` timer stop and nothing simulates. Survivors get the route
  map (arrows pick a room) inside `zombie_mode_reconnect_wait`'s loop; the
  director sees "SURVIVORS TIMEOUT" and the time left. A link going down
  meanwhile shows the reconnect screen instead. The map shows the timeout's
  state right of the room name (`zm_timeout_map_line`); the HUD shows
  TIMEOUT READY while it can be had. The director turns it on/off on the
  lobby's join screen (arrows; NetLobbyBody `options` bit 0 =
  off, default on). TIMEOUT=34 `{1 request, player, seed low, seed high}`
  survivor -> host; `{2 start, player, seed, seed, duration/100}` and
  `{4 end}` host -> all; `{3 refused, reason}` host -> requester. Taken
  straight from the net layer (every copy is paused). Untested by the user.
- **Survivors' map:** OPTIONS in the game opens the route map for every
  survivor (look-only: key doors, keys and crests, the survivors' rooms);
  Richard's radio adds the monster counts. OPTIONS no longer opens the options
  screen for survivors.
- **Story flags:** `g_ScenarioFlags` / `g_ScenarioFlags2` are not merged
  (scripts set and clear them). A survivor's copy diffs both banks every
  frame (`zm_world_story_watch`) and sends each changed byte as STORY
  `{bank, byte, set, clear}`; the others apply it. Per-player bits (stage
  variant, outfit, Yawn poison, lockpick, radio, menu latch, ...) stay local.
  This carries e.g. the 2F dining statue (bank 0 bit 0x0B, read by ROOM7020
  and ROOM6050). A copy already in the room sees the change on re-entry.
  `[story]` lines go to the debug log.
- **Shared item box:** the box starts empty (`zm_world_box_reset` at the
  armed new game; `bio_card.dat` would fill it) and is one box for every
  survivor. `ZombieBox.cpp` arbitrates atomic inventory/box swaps on the host.
  BOX carries `{operation,token,box slot,expected item|qty,offered item|qty,
  inventory slot,seed low,seed high}`. Conflicting requests refresh the slot
  and leave inventory unchanged. Reliable receipts and semantic retries award
  once; outstanding receipts reconcile a crashed client's inventory before
  rejoin or timeout loot recovery. Death drops and spectator jumps wait for the
  transfer. Host STATE includes all 48 box slots plus a revision; stale updates
  cannot roll back newer contents and missed broadcasts are repaired. Any
  inventory item can go in, keys and crests included. Normal
  single-player box behavior is unchanged. `tests/test_zombie_box.py` covers
  races, swaps, stale requests, retries and reconnects. User testing required.
- **Exclusive pickups** (`ZombiePickups.cpp`, multiplayer): floor pickups use
  their global roomItems flag, drops their stable uid. PICKUP=30 carries
  `{operation,token,world/drop,flag/uid,stage|room<<8,item|qty<<8,seed low,seed high}`.
  The host reserves one claimant, then confirms a commit before inventory is
  awarded. Competing attempts cannot duplicate the item. Cancellations and
  disconnected/ten-second abandoned reservations release uncommitted items;
  retries are idempotent. Critical transactions/drop creation bypass the effects
  inbox. The pickup task yields through confirmation with live monster updates;
  input, drop-slot reuse and spectator room changes wait until it completes.
  An award arriving after death joins the corpse's inventory drops. Maps and
  documents retain their original collection paths. The shared item box uses
  the separate atomic swap protocol above. Untested by the user.
  After the host's grant the item must still be there by its roomItems flag,
  not by its action entry's handler: a room script may disarm the entry while
  the viewer is up (ROOM7150's frame script switches the red jewel's slot 5
  off whenever flag 0xC8, another item's, is clear - the deer's scene still
  hands the jewel over). It gave "PICKUP CANCELLED". Untested by the user.
- **Dropped items** (`ZombieDrops.cpp`): a survivor's inventory command box
  has a fourth row, DROP (MainMenu.cpp `menu_item_drop` / `menu_draw_drop_row`:
  status.tim has no DROP button, so it is the CHECK button's edges with a
  letter-free column tiled over the word, and "DROP" in the 8x14 font). The
  item leaves the inventory (as an item-box deposit) and lies where the
  survivor stood for the rest of the game. Every copy keeps the list (DROP
  `{uid, item|qty<<8, x, y, z, angle, stage|room<<8}`, uid = player << 12 |
  sequence); in the loaded room each is a pickup of our own like the
  randomizer's new spots - room action slots `ZM_DROP_SLOT_FIRST` (88)..127,
  item models after the first 24, up to 40 a room - placed at room load or,
  dropped while the copy is in the room, once no menu is open. A placed
  drop's roomItems flag comes from a pool the mansion and scenario leave
  free; it is each copy's own (left out of the FLAGS merge). Its flag
  cleared on an accepted pickup removes its local model. Multiplayer consumption
  is confirmed by the host's PICKUP transaction; survivors cannot broadcast an
  unreserved DROP_TAKE to consume another copy's drop.
  Items without a view model (ids 0x4D+) cannot be dropped. `[drop]` lines.
  A survivor's death drops its whole inventory (its own copy, the first
  frame its health is below 0, `zm_drops_on_death`): in a ring 600 out round
  the body, a spot in a wall falling back to the body's own.
  - Engine widening for it: `g_RoomActionTable` 24 → 128 entries
    (`ROOM_ACTION_ENTRIES`; room init clears the new ones outright),
    `g_item_model_table` 8 → 64 (`ROOM_ITEM_MODELS`). The randomizer's new
    spots stay below slot 88 and model 24.
- **Feeding zombies:** a fatal bite leaves the living zombie feeding over the
  corpse (the grab's status bit 0x08 is cleared). Feeding survives room capture
  and ownership changes, restored directly in the all-fours pose; FEED=25 is a reliable roster companion, and GRAB_END
  carries the fatal-bite feeding cue. The feeding loop references only its own
  joints. On arrival it feeds for at least 4 seconds, then stands if a living
  survivor comes within 1200 units. It remains targetable while down; nonlethal
  weapon hits and director possession make it stand, then AI or pad control
  resumes. Lethal hits retain the death path. Untested by the user.
- **Safe rooms:** a room whose scripts set an item box (`room_action_set`,
  handler 0x08 - in the mansion the 1F save room 0x00 and the storeroom 0x18;
  the main hall's typewriter alone does not count) is closed to the director
  for the whole game: no placing or traps, no map jump, no door
  (`zm_director_safe_room`, from `zm_random_room_safe`). Drawn dark on its map.
- **Occupied rooms:** map purchases of a normal zombie or an unlocked Hunter
  request a doorway reinforcement (`ZombieReinforcements.cpp`), once per minute
  globally at normal cost and within the room cap. The room owner chooses the
  nearest safe real door to any living survivor, excluding that survivor's
  entrance (carried in STATE) - except in a leaf room (every real door leads
  to one neighbouring room, `rf_leaf`), where the way in is the entrance.
  Untested by the user. A one-second door warning precedes revalidation;
  the host then places/pays and the monster holds its entrance for 500 ms.
  Failed requests spend no points or cooldown. Other monsters remain refused;
  safe rooms and protected halls stay excluded. Multiplayer only, untested by
  the user. Traps and map jumps there are still allowed. REINFORCE=29 carries
  request, reply, warning and entrance-hold cues; introduced in protocol 40.
- **Roster sync:** a survivor's roster starts when it joins (`zm_net_join`)
  and is not reset at game start, so placements made during character select
  survive; only the copy that runs a room (the owner) captures it to the
  roster on leaving.
- **Main-hall lock:** the director cannot place a monster in, jump to or walk
  into the main hall or the 2F main hall (`zm_is_hall`) until 60 s after every
  survivor has spawned (or 150 s after the start). A HUD line counts it down.
  The 2F main hall takes only zombies and Hunters, placed or walked in
  (`zm_hall_2f_refuses`): its stairs trip up the dogs.
- **Director's points** (`ZombieEconomy.cpp`; the director's copy and single
  player only): 500 / 800 / 1000 to start with 1 / 2 / 3 survivors
  (500 in single player), then 1 a second with one survivor alive, 2 with
  two, 4 with three (`zm_econ_rate`), +50 when its possessed monster hits
  a survivor (at most one per 1.5 s; a remote zombie grab counts when the
  victim's copy takes it), +25 when any of its other monsters does (at most
  once per 2 s a monster; seen by whichever copy runs the room - a survivor's
  copy sends CREDIT to the director), and 25% of a monster's price back when it dies
  (locally after its update, or from another copy's ROSTER capture: not alive
  with health < 0). Shown top right (the notes moved under it).
  - Prices: zombie 100, green 125, naked 150 (+25% health, on every copy, in
    `zm_world_after_update`), Cerberus 200, web spinner 150,
    Chimera 400, Hunter 500, Tyrant 1800.
  - Unlocks by Yawns beaten (`kPrices` `bosses`, `zm_econ_locked`, counted by
    `zm_yawn_beaten_count` from the boss phases - a lockdown counts): zombies
    and Cerberus from the start; Hunter, Chimera and web spinner after the
    first Yawn; the Tyrant after both. The map's type list shows "YAWN n" for
    a locked type; the AI director saves for the next tier only while the
    survivors fight the Yawn that unlocks it. Untested by the user.
  - Room caps: the generated `cap` in `ZombieSpawnSpots.cpp` (main hall 5,
    1-4 elsewhere, 2 for a room not in the table), +1 at 6 min and +1 at 10 -
    except a cap set by hand (gen_spawn_spots.py `CAP_OVERRIDES`,
    `ZM_SPAWN_CAP_FIXED`: the bathroom off the trap passage holds 1 all game).
    `EXCLUDED_AREAS` keeps spots out of the piano bar's alcove.
    Hunters and Chimeras use two slots, Tyrants three, other monsters one.
    A Tyrant can be bought into an empty room even when its cap is below three;
    further placements must fit the normal cap. Counted by `zm_room_monster_slots`;
    another copy's kills in a room only
    count once its owner leaves. Monsters carried through doors are not capped.
  - The placed Tyrant uses the rooftop model (id 0x10, 600 HP), with its
    entrance and scripted final-battle health latch skipped in the mod.
    Possessable: Run+Forward sprints, Action swipes, Run+Forward+Action does
    the charge's wide sweep, and Aim+Action slashes. The low-health finisher can chain into an
    impale. Tyrant hurt reactions and impales use TYRANT_REACT=23 (stable
    attacker uid, victim reaction and grab anchor); the owner holds the same
    victim through the attack. TYRANT_TRAIL=24 carries a visual-only ribbon
    start cue to the other copies in the room; puppets draw it from the net
    joint pose. Each Tyrant keeps its own ribbon history and claw-effect
    state. Untested by the user.
- **Director's traps** (`ZombieTraps.cpp`): on the map's type list after the
  monsters (ids from `ZM_TRAP_FIRST` 0xF0), set on the selected room with Aim.
  - LOCK DOORS (100 points, 1 min cooldown): for 10 s every door of the room
    and every neighbour's door into it is shut for survivors (camera-only
    records excepted). TRAP `{id, stage|room<<8, duration/100}` goes to every
    copy, timed from arrival. `door_try_enter` asks `zombie_mode_door_trapped`
    (lock click, "THE DOOR IS JAMMED - N S"); the single-player AI survivor's
    `zm_door_usable` refuses them. Not allowed in the main hall while it is
    closed. The director's body is not held.
- **No starting monsters:** the rooms' own enemy_set records are refused
  (`zombie_mode_enemy_spawn`, ids below `NPC_ENTITIES_IDS`); every monster is
  one the director places.
- **One room per copy.** Each copy runs only its own room.
  - The **room owner** is the lowest present player index (the director, if
    present). It runs the room's monsters and sends ENEMIES. Everyone else shows
    puppets.
  - When ownership changes, the old owner sends the new one an adopt snapshot.
    When an owner leaves, its puppets revert to AI.
- **Monster ids (uids)** map remote slots through `s_localOf` / `s_ownerOf`.
  - Below 16: a script slot.
  - From 0x100: a roster extra. The director mints 0x100+, survivors 0x4000+.
  - 0xFFFD: the director's own body (`s_puppetZombie`).
- **Roster** (`ZombieWorld.cpp`): 1024 entries that persist monsters across
  rooms, including ones the director creates. `ZM_HEALTH_FRESH` is 0x7FFF.
  Extras spawn at room load in uid order.
- **Survivor stand-ins** sit in entity slots 27–29 (`ZM_SURVIVOR_SLOT_BASE` 26
  + player index).
  - Chris, Jill, Barry and Rebecca use player models char10–13. Richard (id
    0x27) and Enrico (0x28) use the NPC models em1027/em1028, Chris's animations
    and Chris's weapon textures.
  - They are posed from net STATE, with `ResetJointTransforms` on first show.
    Their weapon EMW goes into joint 14.
  - They block the local player (`ResolveEntityScaCollision`).
- **Combat across copies.** `zm_target_begin` / `zm_target_end` wrap each
  monster update and put the nearest survivor into `g_playerEntity`.
  - A zombie grabbing a remote survivor hands off by GRAB/GRAB_END: the
    victim's copy runs the bite.
  - Other monsters' hits send PHURT. The victim's copy applies the damage, with a
    700 ms cooldown.
  - Projectiles and effects are replayed from FX. Replayed effects do no damage
    (`zombie_mode_effect_damage_blocked`).
  - Burst joints (magnum head shots, rocket gore: joint flags 0x28 with the draw
    bit cleared) go with HIT as a joint mask and from the owner as BURST; the
    head-explosion cue (`Snd_em(6)`) travels with the burst, not as SOUND.
    A hidden puppet (the director's body on a survivor's copy while the
    director is elsewhere, `zombie_mode_hide_entity`) takes no weapon hits;
    the body coming into view alive has any burst joint made whole
    (`zm_burst_clear`, which frees the joint's trail slot).
- **Possession** (`zm_monster_update`). The director can drive zombies, plus
  Cerberus, Hunter and Chimera through a hand-built controller. The pad drives
  movement, and the type's own engine function runs its attacks. Other monster
  types cannot be possessed.
  - When the possessed monster dies, the director takes the next monster in the
    room, else jumps to the nearest room with one (door-graph search, 4 doors
    deep, `zm_possessed_died`). Game over only if none is in reach.
  - Possessed monsters go through doors with Action (Aim+Action is still the
    second attack), locked or not (`zombie_mode_door_ignores_lock` in
    `door_try_enter`; the lock stays for the survivors). A door into a stub
    room keeps its lock. A non-zombie travels as itself: it is re-spawned as a
    roster extra in the new room (`s_carriedId`), not as the zombie body.
- **Modifier buttons** (`PAD_MOD1` / `PAD_MOD2`, docs/GAMEPAD_INPUT.md 6a):
  in the mode LB / RB (keyboard Shift / Tab) are two new buttons, no longer
  aim, read past the remap tables in `ReadPadBoth`. Untested by the user.
- **Reload button** (survivors): cancel/run pressed while aiming (pad B / LT,
  keyboard V / Ctrl / Esc) plays the empty gun's reload (behaviour 0x18,
  motion 0xE) on a part-loaded handgun, shotgun or Python with its ammo in
  the inventory; `fire_consume_ammo_stack` tops it up, keeping the rounds in
  it (`zombie_mode_manual_reload`, `weapon_manual_reload_ready`). In the mode
  that press no longer switches targets (the original's behaviour 0x1A). The
  other weapons have no reload motion and still reload through the
  inventory. Untested by the user.
- **Voice lines** (`ZombieVoice.cpp`, survivors): hold LB (Shift) and press
  up HELP, down WATCH OUT, left GUARD ME, right LEAD THE WAY; aim (X)
  FOLLOW ME, inventory (Y) STAY HERE, action (A) USE THAT, run (B) RUN AWAY.
  RB (Tab) groups supplies/social: up JOKE, down GOOD JOB, left NEED AMMO,
  right NEED HEALTH (hurt/dying by HP), X TAKE THIS, A THANKS, B TAUNT,
  Y WE'RE SCREWED. See docs/GAMEPAD_INPUT.md 6a.
  Read as the bound functions, so RT / LT and keyboard keys say them too.
  While either modifier is held those never reach gameplay
  (`zombie_mode_pad_filter` in PlayerPad_Update, before the remap), except a
  face function already held when LB went down (aiming keeps the gun up).
  Recordings per character:
  `<exe dir>/infested/voices/<character>/<line>_<n>.wav` (chris, jill, barry,
  rebecca, richard, enrico; help, takethis, needammo, needhealth, followme,
  stayhere, watchout, guardme, leadtheway, runaway, usethat, thanks,
  goodjob, joke, werescrewed, hurt, dying, taunt; takes 1..
  up to the first missing, max 16; .ogg too). The speaker picks the take;
  1.5 s between two lines. VOICE=36 `{line, take, character, stage|room<<8,
  x, y, z}`. Each copy (the director's too) hears it in the speaker's room
  panned from the speaker, or in a room with a door to it muffled (-6 dB,
  AudioFile_Muffle) and panned from that door; a caption ("JILL: HELP!" /
  "JILL - NEXT ROOM: HELP!") either way. The music ducks under a line this
  copy hears (-10 dB, -8 dB through a door; the room's banks, the Yawn fight's,
  the piano's), easing back after it; an owner's fade is taken as its new base. One bank per speaker, loaded on
  play and freed after (Windows has 80 banks). `[voice]` lines. Untested by
  the user.
- **Director controls:**
  - START switches monster. Action attacks; Aim+Action is the second attack.
  - Aim: a white-coat zombie lies down / gets up; a naked or green zombie
    vomits (the zombie's own `zombie_vomiting`, behaviour 6).
  - OPTIONS opens the map (`ZombieMap.cpp`): rooms where a scenario key or
    crest still lies (its roomItems flag not cleared) are purple, over the
    monsters' orange (`zm_random_room_key_left`); the mansion's 1F and 2F side by
    side (the rooms up to the back-exit door), every room with a room file
    selectable. B1 is drawn under 1F only in the return mansion - the first
    mansion's B1 RDTs (stage 2 rooms 0x1A-0x1C) are 4-byte stubs. On the
    map: the arrows move to the nearest room in that direction (on the drawn
    map, 1F's right edge leads to 2F), Run opens the monster-type list
    (up/down, Action or Run to keep), LB / RB (Shift / Tab) step the
    monster type list open or not, Aim places one, Action jumps there,
    OPTIONS closes.
  - Mod text goes through `zm_text_encode` before `PrintText8x14`: the 8x14
    sheet draws glyph byte - 36, so only letters, digits, `:` `;` `?` and
    space are ASCII; `<` drew `,` and `>` drew `!`.
  - Jumping to a room takes over its first monster; a room without one is
    refused (the map stays open and notes "NO POSSESSABLE MONSTERS IN THAT
    ROOM").
- **Placement and start positions:**
  - Placed monsters in rooms that aren't loaded wait at `g_zmSpawnSpots`.
    `ZombieSpawnSpots.cpp` is generated by `tools/gen_spawn_spots.py`; don't
    edit it by hand.
    The three spots keep as far from the room's own pickups (item_model_set
    zones and positions) as its floor allows, up to 4000 (`ITEM_CLEAR`): the
    randomizer's extra pickups lie on them, and the boiler room's sat on its
    four green herbs.
  - Survivors start in a triangle in the main hall around (17000, 8500).
  - The director starts inside the back exit.
- **Piano** (`ZombiePiano.cpp`, multiplayer survivors): Jill, Rebecca or
  Richard USE the sheet music at the bar's piano (`zm_piano_use` from
  MainMenu.cpp's item USE; the original's voiced Rebecca scenes never start)
  and play for 15 s. The USE closes the menu (`zm_piano_menu_finished`); the
  playing starts on the first frame the menu is closed, posed as Jill's own
  bar (ROOM60F1 event 6) does: placed two feet right of her spot and six
  inches closer to the keys, at (10100, 7500), facing them, with room
  collision off while playing (player flags bit 4, as room scripts use it:
  the piano's boundary box at x 10127 would push them back to ~9690) (angle 0, set directly), then
  animations 0x37, 0x38 / 0x39 in turn (state 8 behavior 1) from Jill's room
  file - the mode loads Chris's rooms, whose player plays with one hand - by
  pointing jointMoveData2/3 at ROOM60F1's player animation pair while
  playing (Chris's scene is the fallback without that file). Stopping or
  finishing stands them back up unless a grab has the player. A HUD timer,
  the tune (`sound/bgm_2b.wav`, 24.5 s, the whole piece from the first-visit
  bar's music group 0x0E - bgm_11 / bgm_1f are the practice that breaks off;
  the return bar has no music - in the piano's own bank, on every copy
  wherever it is while anyone plays; it ends with the playing - faded out
  over 1 s when finished, cut when interrupted; the room's music is stopped under it and resumed
  2 s after it stops or runs out, unless someone starts again), and
  the director's copy shows "PIANO BEING PLAYED IN THE BAR" with the count.
  Moving, turning, aiming, running, a hit, a grab or leaving the bar stops it
  (start again from zero; the sheet music is kept). Finished: the sheet music
  is used up, DONE goes to the host, which sets ScenarioFlags2 0xA2 (the
  original's "wall open" - the room's init puts the wall away on every entry)
  and sends OPEN; a copy in the bar plays event 9 (the wall sinking).
  PIANO=32 `{op, player, stage, room, 0, 0, seed low, seed high}`: 1 start /
  2 stop / 5 finished survivor -> all, 3 done survivor -> host, 4 open
  host -> all. The
  wall stays closed until then (`rnd_open_piano_room` waits for the flag).
  CPU test `tests/test_zombie_piano.py`. Untested by the user.
- **Shared room puzzles** (`ZombieRoomSync.cpp`, survivors' copies only):
  pushed objects (2F dining statue, armor room, 2F study, libraries) and the
  large gallery's portrait order (system flags 0x12-0x1F in ROOM6170) travel
  as ROOMSYNC `{1 object, stage|room<<8, slot, x, z, orig x, orig z}` /
  `{2 flags, stage|room<<8, byte, set, cleared}` / `{3 reset, stage|room<<8}`.
  A survivor's pushes are sent (every 150 ms while pushing, and where it
  stopped); each copy's own object-probe zones then reach the puzzle's result
  itself, so the fall / reveal plays for everyone in the room. Entering a room
  another living survivor has loaded takes its state over; entering an empty
  one resets it, as the original's init does, and tells the others. The
  gallery solved elsewhere: the panel moves aside and event 22 frees the
  reward here (the reveal, which places the player, stays the solver's). The
  director's copy keeps its own (these scenes would turn its camera). CPU test
  `tests/test_zombie_roomsync.py`. Tested by the user.
  The 2F statue seen from below (`ZombieStatue.cpp`): a copy that loads the
  1F dining room (ROOM6050) with ScenarioFlags 0x0B still clear preloads the
  broken statue (omodel 3) and the jewel (item 0x12, sparkle off) hidden,
  ROOM7020's standing statue (object model 0) as an extra non-solid item
  record, and `brk_stn.wav`. When the bit is set there (STORY), after 10
  frames the statue drops straight down from 5650 above, tipping from 45
  degrees to lying flat along the pieces' footprint (`st_orient`: long side,
  top towards its far end), then the pieces and jewel appear with the crash
  and dust puffs (billboard type 9). `[statue]` lines. Untested by the user.
  The gallery's six portrait questions (events 6-11 on action slots 2-7)
  change places each game by the seed (`rs_gallery_shuffle`); the answer order
  is unchanged, the plaque (slot 1) and last portrait (slot 8) stay. Untested
  by the user.
- **Doors:** every door animation in the mode ends after `ZM_DOOR_FRAMES`
  (20 frames, about 0.7 s, `zombie_mode_door_frames` in DoorSystem.cpp's
  DoorAnimLoop), counted from the script gate opening (the sound load), not
  from the start, so a slower load does not shorten the shown opening; 60
  frames if the gate never opens. Door scripts play at 5x after the approach
  is skipped; stairs (kai01-04) at 2x with the cut stretched to 50 frames.
  The held-button skip is off, so every player's transition
  takes the same time. The AI survivor's off-screen door cost uses the same
  constant. Untested by the user.
- **Door stun:** a survivor coming into a room stuns the monsters within
  about 6 feet (`ZM_STUN_RADIUS` 2200) of where it stands, for 5 s
  (`ZM_STUN_MS`). The survivor's copy sends STUN `{x, z, stage|room}` on its
  first frame with control in the new room (`zm_stun_frame`) - its STATE names
  the room while it is still loading, so the owner cannot judge the position
  from that. The room owner stuns its monsters in reach (`zm_stun_at`); a
  survivor that owns the room stuns its own. Each monster can be stunned at most
  once per 15 s: the five-second stun is followed by ten seconds of immunity
  (`ZM_STUN_IMMUNITY_MS`, from its last stun's end). The freeze is
  the engine's own: bit 0x0004 of `g_message_flags` is cleared around a stunned
  monster's update in state 1 (`zm_stun_before_update`), but only after the
  monster has run two updates in the room, so it has a pose. Hit reactions,
  deaths and grabs that have started still play. A stunned monster is not
  solid. The director's possessed body is stunned too. `[stun]` lines go to the
  debug log.
- **Texture pages** (`src/marni/TexturePages.h`) are widened to 128 banks.
  - TSB page bits 0–4 hold the low bits and bits 9–11 the high bits. Always go
    through `TexPageFromTsb` / `TexPageTsbBits`.
  - The engine's own page and CLUT counters stay as in the original. Mod models
    load inside `zm_ext_begin` / `zm_ext_end`, on a separate counter: pages from
    0x20 and CLUT rows from 0x40. Moving the shared counters breaks room masks.
  - 1000 TMD entity slots (`TMD_ENTITY_SLOT_COUNT`). `g_DataBuffer` has
    +0x400000.
- **Engine hooks.** The mod calls `zombie_mode_*` functions (declared in
  `ZombieMode.h`) from:
  - models and textures: EntityModelLoader, RoomInit, TmdAnimation,
    Marni3DObject, PathTrail, ObjectManager
  - effects, combat and sound: EffectSystem, WeaponDamage, SoundSystem
  - scripts and entities: CmdFunctions, EntityCommon
  - startup screens: LogosScreen, TitleScreen, CharacterSelectionScreen.

  Barry's char12 model has 15 objects, so EntityModelLoader guards the 16th
  joint.

**Files**

| File | Contents |
|---|---|
| `ZombieMode.cpp` | the mode's main file: the director's body, room hooks, owner sync, stand-ins, placement, map jump, monster controller, FX/PSND/PHURT, target swap, ext texture counter, cooldown, skins |
| `ZombieSurvivor.cpp` | the AI survivor, the RDT door/spawn cache, `zs_net_frame` / `zs_ride_frame`, survivor start positions, HUD |
| `ZombieWorld.cpp` | the roster, extras and uid↔slot mapping, flag merge |
| `ZombieNet.cpp/.h` | the 4-player link |
| `ZombieLobby.cpp` | the menu (Single / Host / Join / Address / Port) and the lobby screen. A survivor presses Action for the character select screen: the list, the highlighted character's name, toughness, description, items and perk (`zm_perk_describe`), and its model turning on the right, drawn like the options menu's player (`options_render_entity`, the player entity borrowed through `zombie_mode_preview_skin` - no game is loaded yet). The host presses Enter to start, which calls `lobby_start_game` → `game_start` |
| `ZombieMap.cpp` | the director's map overlay; areas come from the map TIM palettes |
| `ZombiePerks.cpp` | the survivors' per-character perks and starting kits |
| `ZombieRandom.cpp` | the randomized scenario: keys onto locks, keys/crests/weapons onto item spots, puzzle locks opened |
| `ZombieDrops.cpp` | the survivors' dropped items: the list, the in-room pickups, the inventory's DROP |
| `ZombieTimeout.cpp` | the survivors' one timeout: the request, the host's safe-room check, the paused route map |
| `ZombiePiano.cpp` | the bar's piano: who can play, the 15 s playing and its interruptions, the director's alert, the wall opening |
| `ZombieKeypad.cpp` | the back area's ways in: the battery powering the small elevator, the keypad door and its pass number's note, the rough passage's door back |
| `ZombieMessages.cpp` | messages that do not pause the game: the passive decision, the reading timer, the hand-over to doors and the menu |
| `ZombieRoomSync.cpp` | a room's pushed objects and puzzle flags, shared by the survivors in it |
| `ZombieStatue.cpp` | the 2F dining statue falling into the 1F dining room, seen live from below |
| `ZombieYawn.cpp` | the two Yawn fights: spawns, health, music, locked doors, lockdowns and the clock's holds, the guarded key/crest, the lesson room's hole, the body across copies |
| `ZombieVoice.cpp` | the survivors' voice lines: LB + a direction, the recordings, who hears them (the room, muffled through a door) |
| `ZombieTraps.cpp` | the director's traps: LOCK DOORS (the room's doors shut for survivors) |
| `ZombieSpectate.cpp` | a dead survivor watching the living ones: the follow, the frozen corpse STATE, the HUD |
| `ZombieStats.cpp` | the end of a match: each player's stats, their exchange (STATS) and the white end screen |
| `ZombieEconomy.cpp` | the director's points: prices, unlock times, room caps, refunds and hit bonuses, the points HUD |
| `ZombieNav.cpp` | navigation |
| `ZombieModeInternal.h` | shared declarations between the mod files |
| `ZombieMode.h` | the hooks the engine calls |

**Open items**
- Untested by the user:
  - monster possession and speeds
  - survivor collision
  - projectile replay
  - texture widening
  - Richard and Enrico
  - the lobby
  - the room-leave cooldown
  - start positions and spawn spots
  - the stair-ride fix.
- Shots sometimes don't hit zombies with 3 players in the main hall. A shot
  audit is in place; waiting for the user's log.
- Untested by the user (this round): the Enrico crash fix (`LoadEntityModel`
  moves the joints' animation objects past a TMD that ends late), death
  hand-over, the win condition, the story-event filter, burst sync, the extra
  sound groups, monsters through doors.
- Untested by the user: a dead survivor's inventory dropped round the body,
  dropping items (the DROP row's look - it may touch
  the equipped-weapon panel - drops in a room another survivor is in, part
  pickups), the widened room tables, the 20-minute game clock and the director's win,
  the ammo doubling (more pickups), the per-survivor
  point rate, the shared item box, safe rooms closed to the
  director, the director's in-game purple key rooms, the director ignoring
  door locks, the 2F main hall (closed with the hall, zombies and Hunters
  only), the headless director's-body fix.
- Untested by the user (economy round): the points, prices, unlock times,
  room caps, refunds, hit bonus, naked-zombie health, the placed Tyrant,
  the other monsters' +25 hit credit, the LOCK DOORS trap.
- Untested by the user: an AI zombie's grab handed to another copy no longer
  leaves it flagged dead on the room owner (status bit 0x08 from
  `zombie_attack`'s first step - it dropped out of the monster count and the
  START switch, was refunded and vanished when the room was left); a
  survivor's stand-in re-dresses when that player's pick changes after the
  room loaded (`zm_standin_refresh`).
- Untested by the user: the player code's per-character tables (`id & 1`:
  aim heights, fire frames, walk footfalls, muzzle flash, turn rates, knife
  offsets) follow the body worn (`zombie_mode_player_body`: Jill and Rebecca
  1, everyone else 0) instead of the scenario's Chris - Jill's straight aim
  read as aiming down against Chris's aim heights.
- Untested by the user: skipped scenes giving the control back (roofed
  passage), the back passage's Hunter entrance (walk-in) and the other
  monster entrances skipped.
- Untested by the user: the puzzle weapons (ranked by solve distance, ammo
  only for those, the rocket launcher's 4 shots, fuel refilling the
  flamethrower), key items in leaf rooms, Richard's sheet music and 162 HP. Tested: the grenade launcher
  on Chris and the flamethrower on Jill work (their own W06 / W15 files,
  never held in the original game).
  The flamethrower's in-hand mesh (W05 = W15 = W25 = W35) is cut for
  Chris's sheet: on Jill, Barry and Rebecca it reads Chris's TIM, loaded on
  demand like the Ingram's (`zm_flamer_borrows_sheet`, `zm_chris_sheet`) -
  on their own sheets it came out flesh-coloured. Untested by the user.
- Untested by the user: spectating after death (the follow through doors,
  the camera, the corpse staying put on the other copies, the monsters
  shown in a room only the watched survivor is in); the auto-aim no longer
  locking onto the hidden director's-body puppet (`zombie_mode_hide_entity`
  in `player_find_aim_target` / `player_reticle_enemy`).
- Untested by the user: the director's win when every survivor is dead, dead
  survivors kept in the match, the white end screen and its stats on every
  copy (single player too: the AI survivor's death is now the director's win
  instead of the death screen).
- Known gaps:
  - the director's own game over (its possessed monster died with no other
    in reach) still ends only the director's copy, without the end screen
  - about 26 monsters per room; 8 sound groups per room (the room's 4 plus the
    mod's 4, `Room_SetupZombieSoundGroup`)
  - paired kill animations other than the Tyrant's impale are not synced;
    the Tyrant synchronization and possession still need user play-testing
  - mod models skip the death-fade tint
  - possessed non-zombies cannot use stairs or in-room steps.
- Long-term:
  - a server-authoritative director host
  - randomized scenarios (route, keys, weapons) with a route map shown before
    the game.

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
