# Infested: the two-Yawn scenario — plan

Status: design agreed with the user on 2026-10-08. Step 1 done (work committed
in f59026e). Step 2, Yawn 1 in the attic: implemented 2026-10-08, untested by
the user (see AGENTS.md "Yawn 1 in the attic"); Yawn 2 not started. From
step 4: the hole shut (generator and game, until Yawn 2's death flag), no
progression item in the lesson room and the fixed key roles (key 3 in the
attic, held back until Yawn 1 flees; key 4 behind key 3) - done 2026-10-08,
2000 seeds verified. Then (2026-10-08, untested by the user): Yawn 2 in the
lesson room (dies, not flees) with its crest, step 3's lockdown, free
revives, revive reset, director closed out, clock holds and +5:00, and the
clock from step 5 (12:00 since, at the user's request).
Applies to **every** Infested game: human director and AI director alike.

## Before starting

- The working tree holds a large amount of **uncommitted** work from the AI
  director sessions (AI director, host-plays-survivor, threat budget, finer
  spots, marches, randomizer room rules, trap camera/ceiling fixes). Ask the
  user to commit it (or commit it on their say-so) before changing anything.
  `AGENTS.md`, `src/game/CmdFunctions.cpp` and `src/game/PlayerAnimations.cpp`
  carry small edits that are the user's own — leave them alone.
- `QUICK_DEBUG` is still defined in `src/game/mods/ZombieModeInternal.h`
  (20000 director points, no hall lockout). Mention it before balance testing.
- Build (Linux): `cmake --build build/linux -j8`. Game data for offline tools:
  `build/linux/USA`.
- CPU test suite (no game launch): every `tests/test_zombie_*.py` plus
  `tests/check_randomizer_progression.py`, each run as
  `python3 <test> --compiler g++`. `test_zombie_greenhouse.py` fails in this
  checkout regardless (needs `bin/Release/USA`, absent) — not a regression.
  Most tests compile *extracted* production functions against hand-written
  fixtures: new helpers a touched function calls must be stubbed in the
  fixture (see how `zm_seat_state`, `zm_match_authority` were added).
- Offline randomizer verification over real seeds:
  `tools/evaluate_zombie_routes.py`'s `build_adapter()` builds a generator
  from production code; copy its `generator.cpp` to the scratchpad, patch
  extra `printf`s in before `printf("],\"items\":[");`, compile with
  `g++ -m32 -std=c++17 -O2`, feed seeds on stdin with the asset path as the
  argument. Every randomizer rule change so far was verified over 2000 seeds
  this way; keep doing that.

## The scenario

Escape is unchanged: all four crests, then the storeroom's courtyard exit.

The route spine:

```
start → key 1 → pillar passage door (lock 0x0D, from C passage 2F 0x04)
      → key 2 → attic door (lock 0x07, front of attic 2F 0x0E → attic 2F 0x10)
      → YAWN 1 (attic) — drops key 3
      → doors key 3 opens → key 4 (ideally far side / back area)
      → lesson room door (lock 0x19, front lesson 2F 0x0B → lesson room 2F 0x0C)
      → YAWN 2 (lesson room) — drops a crest; the ladder/hole to B1 opens
```

- The four key types play fixed *roles* (1, 2, 3 = Yawn 1's drop, 4 = lesson
  room) but which key type plays which role, which other doors each opens and
  where keys 1, 2 and 4 lie stay random. Keys 1 and 2 must differ (already
  enforced: attic and pillar locks never share a key).
- Key 3 is never placed in the world: it comes from Yawn 1. The generator must
  model it as "obtained in the attic" (the current "attic always holds a key
  or crest" rule in `ZombieRandom.cpp` — `RND_ATTIC_ROOM`, `atticItem` — is the
  precursor; replace it).
- One crest comes from Yawn 2; the other three stay random (one of them always
  behind the back area's battery/keypad, as now).
- The lesson room ↔ B1 passage 1 hole is a back door around the lesson room's
  locked door: it must stay shut (both directions) until Yawn 2 is dead, in
  the game *and* in the generator's reachability proof. Today it is opened
  unconditionally (`kDoorGates` entries for `ROOM_LESSON_ROOM` and
  `ROOM_MANSION_B1_PASSAGE_1`; `zombie_mode_room_prepare` sets the open hole;
  runtime hook near `g_roomId == ROOM_LESSON_ROOM` in `ZombieRandom.cpp`).
- Remove the lesson room (2F 0x0C) from `kExtraLeafRooms` — it becomes the
  Yawn 2 arena, not a random key-item room. Revisit the attic likewise.
- **Decided 2026-10-09:** after Yawn 2 the lesson room's door to the front
  lesson room stays sealed; the survivors leave through the hole and the
  basement, a choke point for the director (done).
- Wishlist variety (not required for the first version): sometimes flip the
  order (lesson room first, attic second); sometimes put key 4 in the back
  area; sometimes leave the pillar door open so only the attic door needs a key.

## Clock

- Starts at **10:00** (today `ZM_TIME_LIMIT_MS` is 20 min in
  `ZombieMode.cpp`; the clock lives in `zm_clock_left_ms`,
  `zm_match_elapsed_ms`, `zm_clock_frame`, sent as `ZM_EV_CLOCK` by the host).
- **Paused while every living survivor is inside the boss room** during a live
  fight. Dead survivors elsewhere don't count.
- Each Yawn kill adds **5:00**.
- Assumption (user didn't answer explicitly — confirm): the clock also stays
  paused during the 60-second post-fight lockdown.
- Pause mechanics already exist: the survivors' timeout pauses everything via
  `zm_timeout_active` → `net_has_pause` → `zm_game_time_ms`
  (`ZombieTimeout.cpp`, `ZombieNet.cpp`). The boss pause should stop only the
  *match clock*, not the world (the fight must keep running) — so it is a
  separate mechanism from the timeout's full pause.

## Boss room rules

1. **Yawn wakes immediately** when the first survivor enters.
2. Boss room doors are **one-way** while Yawn lives: survivors can enter, not
   leave. (The shotgun trap already blocks doors per survivor:
   `zm_shotgun_door`, `zombie_mode_door_trapped` — reuse that pattern.)
3. **On the kill — 60-second lockdown:**
   - doors stay locked;
   - the director can only observe that room: no placing, reinforcements,
     traps or possession there;
   - survivors inside may revive teammates left dead by the fight once **without
     a healing item, even past the revive limit**;
   - the drop appears (key 3 / the crest).
4. **When the doors unlock:** every survivor's revive count resets — including
   survivors lying dead outside the room. Those still have to be reached, and
   reviving them still costs a healing item as usual; they are simply eligible
   again. +5:00 on the clock; it resumes.
5. **During the fight:** no reinforcements or traps into a live boss room. The
   director (human or AI) can't buy into it either. Yawn runs on its own engine
   AI for now.
6. The match ends only when **every** survivor is dead (existing rule). Two dead
   in the boss room and one alive outside: the match goes on, the clock runs.
7. **Remove the survivors' timeout feature** (lobby switch
   `zm_net_timeout_enabled`, `ZombieTimeout.cpp`) — boss fights replace it as
   the breathing room. Remove cleanly, including the lobby row and map line.

Revive code: `ZombieSpectate.cpp` (`s_revives`, `ZM_REVIVE_LIMIT` = 1,
`zm_revive_host_claim`, `zm_revive_take`, `zm_revive_item`). Revives are
host-arbitrated; the host may itself be a survivor (see below).

## Monsters / economy

- Hunters, Chimeras and web spinners unlock on the **first Yawn's death**
  instead of on a timer; zombies and Cerberus are open from the start, the
  Tyrant after both Yawns (`kPrices` `bosses` in `ZombieEconomy.cpp`;
  `zm_econ_locked`). Done.
- The **Tyrant leaves the mansion shop** (map list `kMonsters` in
  `ZombieMap.cpp`, `kPrices`, and the AI's candidates in
  `ZombieDirectorAI.cpp` — its `s_tyrantPlaced` logic and Tyrant weight).
- AI director (`ZombieDirectorAI.cpp`): treat the boss rooms as off-limits for
  placement, marches and reinforcements while a fight or lockdown is on; its
  "save for a big unlock" logic (`ai_reserve`) must follow the new unlock
  condition; its route prediction (`ai_survivor_way`) can use the spine
  (attic, then key 4, then lesson room) as goals.

## Yawn itself (the risky part)

- **Decided 2026-10-08:** each boss room spawns its own Yawn (the room's own
  enemy_set, not the roster), and Yawn is never killed: reaching the damage
  threshold makes it flee, and the flee (its death flag) is the defeat. Read
  "kill" below as "flee". Yawn 2's flee path (`s_yawnFleePath`, also the
  reposition path) is in attic coordinates, and id 0x12 normally uses the
  no-flee selector - both need solving for the lesson room.

- Entity code is complete: `src/game/entities/Yawn.cpp` (~2500 lines),
  `ENEMY_YAWN_1` 0x0D (first fight), `ENEMY_YAWN_2` 0x12 (second).
- The mode **never spawns Yawn today**: rooms' own enemy_set monsters never
  spawn in the mode (only roster extras), and `zombie_mode_skip_scd_event`
  skips SCD events that take control and drive an enemy (boss entrances).
- First task: get Yawn 1 spawning and fighting in the attic under its own AI —
  study `Yawn.cpp` for what it expects from the room (the attic's hole, any
  entrance event, state initialised by the skipped script), spawn it via the
  mode (`zm_spawn_monster` / roster), make death produce key 3, then repeat for
  Yawn 2 in the lesson room (in the original game the second fight is
  elsewhere; check what its entrance expects there).
- Multiplayer: shared rooms are simulated by the room owner (lowest-seat
  player present; `zm_compute_owner`). Yawn's grab/bite has no network
  handoff, so on other copies it falls back to a plain damage reaction
  (`zombie_mode_network_match`) — acceptable at first; a proper handoff like
  the zombie grab's can come later. Scale Yawn's health with the number of
  survivors.
- Director-controlled (possessed) Yawn is a **later** step: possession has
  per-type drivers for zombies, Cerberus, Hunter, Chimera and Tyrant
  (`zm_monster_update` and friends in `ZombieMode.cpp`); Yawn would need its own.

## Build order

1. Commit the pending work (with the user's go-ahead).
2. Yawn 1 in the attic under engine AI: spawn, fight, death drop (key 3),
   synced across copies. Then Yawn 2 in the lesson room with the crest drop.
3. Boss room rules: one-way doors, clock pause (all living survivors inside),
   60 s lockdown, free revives inside, revive-count reset on unlock, no
   director actions in the room, +5:00.
4. Generator: fixed key roles, key 3 from the attic, crest from Yawn 2, the
   hole closed until Yawn 2, lesson room out of the random key-item rooms;
   verify over 2000 real seeds (every seed generates; spine holds).
5. Clock 10:00, Hunters/Chimeras on Yawn 1's death, Tyrant out of the shop,
   timeout feature removed.
6. AI director awareness of the fights and the new unlock.
7. Later: possessed Yawn; other-area scenarios (lab with Tyrant, guardhouse
   with Plant 42) as separate projects.

## Standing rules from the user

- The AI director must never place or move a monster within the door stun's
  reach of any door arrival point (`AI_STUN_CLEAR` 2700 in
  `ZombieDirectorAI.cpp`; `PLACE_STUN_CLEAR` in `tools/gen_spawn_spots.py`).
- Don't change ammo amounts without asking (an open question: the user found
  heavy-weapon ammo plentiful but also ran out of magnum rounds).
- Match the surrounding code's comment style (dense, explanatory, no address
  comments on port-added code). Read `AGENTS.md`.

## Key places

| What | Where |
|---|---|
| Mode core: clock, outcome, ownership, possession, placement | `src/game/mods/ZombieMode.cpp` |
| Shared declarations | `src/game/mods/ZombieModeInternal.h` |
| Randomizer (keys, crests, locks, leaf rooms, safe rooms) | `src/game/mods/ZombieRandom.cpp` |
| AI director | `src/game/mods/ZombieDirectorAI.cpp` |
| Economy, prices, unlocks | `src/game/mods/ZombieEconomy.cpp` |
| Revives, spectating | `src/game/mods/ZombieSpectate.cpp` |
| Survivors' timeout (to remove) | `src/game/mods/ZombieTimeout.cpp` |
| Network, loopback for the host's own survivor | `src/game/mods/ZombieNet.cpp` (protocol `ZM_NET_VERSION` 67 — bump on wire changes) |
| Lobby | `src/game/mods/ZombieLobby.cpp` |
| Shotgun trap (door blocking pattern) | `src/game/mods/ZombieShotgun.cpp` |
| Spawn / AI spot tables (generated) | `tools/gen_spawn_spots.py` → `src/game/mods/ZombieSpawnSpots.cpp` |
| Yawn | `src/game/entities/Yawn.cpp` |
