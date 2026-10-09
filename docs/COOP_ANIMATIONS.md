# Co-op: character animation map

Reference for driving the co-op partner (Rebecca / Barry, an NPC entity) with
the same animations the player uses. Port addition; see `src/game/Coop.cpp`
and `npc_coop_partner_control` in `src/game/entities/CharacterNpc.cpp`.

## One skeleton, many animation sets

Every human character model and every player weapon file starts its animation
header with the same eight bytes, `64 00 b0 00 0f 00 68 00`: 15 joints, 52
values per frame. That covers the NPC models (`EM1020`-`EM102E`), the playable
models (`CHAR10`-`CHAR13`), the weapon files (`W*.EMW`), the RDT player sets and
the enemy models' player-reaction sets.

`Joint_move(reverse, animHeader, animBase, blend)` takes the set as
parameters and indexes it with the entity's animation id, so any entity with
this skeleton can play any of these sets.

An `animBase` is an array of `{ u16 frameCount, u16 frameTableOffset }`; the
first frame table follows the array, so the slot count is the first offset / 4.
Many slots are 1-frame placeholders.

| Set | Where it comes from | Player's pointers | Holds |
|---|---|---|---|
| MODEL | character `.EMD` (last 0x14 bytes: [1] header, [2] base) | `animHeader` / `animBase` | idle, first few motions |
| WEAPON | equipped weapon's `.EMW` (header = file start, base = last 8 bytes [0]) | `jointMoveData0` / `jointMoveData1` | idle, walk, run, pick-up, aim, fire, reload |
| ROOM | the room's RDT (`player_anim_header` / `player_anim_base`) | `jointMoveData2` / `jointMoveData3` | push, doors, ladders, stairs |
| EXTRA | the *enemy* `.EMD` loaded last (last 0x14 bytes: [0] base, header = file start) | `emdScratchPtr1` / `emdScratchPtr2` | the player's reactions to that enemy: grabs, throws, deaths |

`LoadEntityEMD` writes `emdScratchPtr1/2` whenever any model with an extra set
loads, so in a room the EXTRA set is the last enemy model's.

## What the player plays for each action

Derived from the code, not by eye: each `g_playerEntity.attackAnim = N` in
`PlayerAnimations.cpp` is the index the player's `Joint_move` plays, from the
set its function passes. (Player `attackAnim` is at the same offset as an
`Entity`'s `animationId`.)

| Action (function) | Set | Index |
|---|---|---|
| idle (`player_behavior_00_idle`) | WEAPON | 0, then 1 (idle loop) |
| walk (`player_ctrl_behavior_walk`), turn on the spot | WEAPON | 2 |
| back-step (`player_ctrl_behavior_run`*) | MODEL | 3 |
| run (`player_behavior_0d_run`) | WEAPON | 3 |
| interact / pick up (`player_behavior_0c_interact`) | WEAPON | 4 |
| gun aim (`player_behavior_12_gun_aim`) | WEAPON | 5 raise, 7 still, 8 / 0xB to up / down |
| gun fire (`player_behavior_14_*`, `15_gun_fire`) | WEAPON | `dir * 3 + 5`.. (forward / up / down rows) |
| hold fire (`player_behavior_18_hold_fire`) | WEAPON | 7, 0xE |
| knife aim / holster (`player_behavior_12_knife_aim`) | WEAPON | 5 |
| push (`player_behavior_10_push`) | ROOM | 0x30 |
| door / ladder (`player_door_open_sequence`, `player_behavior_0b_ladder`) | ROOM | 0x33, 0x35 |
| grabbed (`player_anim_grabbed`) | EXTRA | 0, 1, 2 (+ the zombie's direction row, below) |
| thrown (`player_anim_thrown`) | EXTRA | 3 |
| knockdown / recover (`player_anim_knockdown_recover`) | EXTRA | 4-7 |
| poison death (`player_anim_poison_death`) | EXTRA | 0 |
| hit reaction (`player_hit_react_common`) | EXTRA / MODEL | `isBeingAttackedFlag - 1`, 1 |

\* named "run" in the port, but it moves backwards (`Add_speedXZ(0x800)`).

### Zombie grab

`zombie_attack_anim_tbl` (Zombie.cpp, 0x004bb3a0): per attack direction
(standing/not facing, standing/facing, laying/not facing, laying/facing) the
zombie plays its own animation 17 / 20 / 23 / 26 and the player plays EXTRA
0 / 3 / 6 / 9, three phases each. `Em1000.emd`'s extra set has those 12, with
frame counts matching the zombie's 17-28 frame for frame.

## Weapon set layout (W30 / W32 / W11 / W13)

The same rows in every weapon file; confirmed with the animation browser.

| Index | Gun (W32 Rebecca, W13 shotgun) | Knife (W11) |
|---|---|---|
| 0 | idle: hand on hip | same |
| 1 | idle loop: tapping thigh | same |
| 2 | walk | same |
| 3 | run | same |
| 4 | pick up item | same |
| 5 | raise / ready | ready |
| 6 | fire forward | slash forward |
| 7 | aim forward (still) | slash up |
| 8 | forward to up | slash down |
| 9 | fire up | stance down |
| 10 | aim up (still) | between down and forward |
| 11 | forward to down (shotgun: pump after firing down) | stance forward |
| 12 | fire down | stance up |
| 13 | aim down (still) | - |
| 14 | reload | - |

Playable Rebecca's weapon table (`g_weaponPathTable`, Rebecca block) uses her
own `W30` (no weapon) and `W32` (Beretta) and Jill's files for the rest
(`W11` knife, `W13` shotgun, ...).

## Rebecca's NPC model (EM1023)

65 slots. Confirmed with the browser: 1 recoil as if hit, 4 death fall
(second), 6 idle variant, 7 walk, 8 run, 9 crouch, 16 gun raise (13 frames,
ends in the aiming pose), 17 fire (15 frames), 58 lying dead (1 frame), 59
death fall on her side. 15 is a single waist-level frame, not the aim.
`EM102C` is a cutscene variant with a different set.

## Tools

- `[Coop] AnimBrowser=1` - player 2's pad steps her through any of the sets
  above (aim switches set).
- `[Coop] FreezeEnemies=1` - enemies run their init and then stop, so any
  enemy room is a safe place to look.
