# SCD system — work plan status

Snapshot: **2026-07-28**. Companion to [SCD_SCRIPT_SYSTEM.md](SCD_SCRIPT_SYSTEM.md),
which holds the per-opcode reference and the detailed findings (§6a–§6h).

> **Nothing in this work has been run in-game.** Everything below is
> build-verified only. See "Verification debt" at the end — read it before
> starting anything new here.

---

## Done

| Area | Result |
|---|---|
| `run_command_functions` (`0x00473f60`) | Byte-exact match to the original |
| `script_command_funcs_table` (`0x004c1110`) | All 81 entries match |
| **Instruction lengths** | **81/81 verified**; 6 length bugs fixed (`0x0B 0x21 0x46 0x48 0x4A 0x4B`) |
| **Operand-level diff** | **81/81 verified**; **28 commands were defective (35%)** — all fixed |
| Shadowing stubs (§6a) | `ScdEventEntry_Create`, `cmd_room_action`, `Effect_CreateBillboard` |
| Data tables (§6b) | `g_ScdAnimRemap[32]`, `g_ScdBranchStack[16]`, `room_check_actions` sized to 20 |
| `g_BGM_STATE` (§6c-bis) | Widened to `unsigned int`; duplicate `DAT_00bf07f0` removed |
| Sound globals (§6f) | `SndBankSlot g_SndBank[3]`, `SndPanVol g_SndPanVol[3]`; 8 stride/bound bugs fixed |
| `g_CharacterSfxBanks` size | Settled at **16** records; the 9-record bound in 3 walkers is a Capcom bug (§6g) |
| Leaf dependencies (§6d) | **18 of 21** implemented |

New globals introduced: `SavedEnemyState g_savedEnemyStates[16]` (`0x00be92cc`,
**`.gwipe$92cc`** ordered section, verified with `dumpbin /SYMBOLS`),
`g_pCurrentItemSlot` (`0x00d226f0`), `DAT_00ac98f8`.

---

## Entity render gates — fixed 2026-07-29

No character model drew at all — not the player, not the SCD-spawned cutscene
NPCs — because of two `.data` feature gates the port had wrong:

| Address | Original | Port had | Gates |
|---|---|---|---|
| `0x004d46a4` | `1`, never written | `DAT_004d46a4 = 0` | `room_camera_and_lighting_update` |
| `0x004d46a8` | `1`, never written | read via `g_SpriteQueueCount` | **both** `calc_entity_lighting` calls |

Both have **read-only xrefs** (one and two respectively, all inside `game_loop`),
so they are build-time constants, not runtime state. `0x004d46a8` was being read
through the port's own `g_SpriteQueueCount` — a per-frame sprite counter that
`SpriteRenderer` resets to 0 — so the entity draw was gated off almost always.
Now `g_dwCameraLightingEnabled` / `g_dwEntityRenderEnabled`, both `1`, and
renamed in the Ghidra project (where `0x004d46a8` had been mislabeled
`g_SpriteQueueCount`, which is what caused the aliasing).

Same block had a second bug: `EntityComputeJointWorldMatrices`,
`EntityApplyLookAtRotation` and `calc_entity_lighting` all operate on the
**global** `ENTITY`, but the enemy loop only advanced a local `pEnt`, so every
helper transformed whichever entity was set last. The loop now advances `ENTITY`
and the player block re-points it, as the original does.

`cmd_em_set` (`0x1B`, the opcode that spawns the cutscene NPCs and the only
writer of `g_enemy_count`) was verified byte-faithful, including its `shouldInit`
control flow — it was not the problem.

---

## Player state machine — partially landed 2026-07-29

The room renders but no character models appear and cutscenes stall, because
`update_player_anim` (`0x00494d90`) was an empty stub dispatching a table the
port never had: **16 entries at `0x004d4550`, indexed by `animationId`** (entries
9 and 15 NULL; `0x004d4590` onward is a *different* jumptable, the
`action_behavior` switch inside state 1). Now in `PlayerAnimations.cpp`:

| Landed | Notes |
|---|---|
| `update_player_anim` | Faithful |
| State 0 `FUN_00494eb0` | Spawn/re-init; poses the skeleton via `Joint_move` |
| States 4,5,6,7 | `0x00495280/90`, `0x004952d0`, `0x00495310` |
| `update_player_position` | Room action / door / item interaction layer |
| `FUN_0041b3c0` | Unsigned-compare point-in-box test |

**Deferred, in priority order:**
1. **State 1 `FUN_00495180`** — the control state machine (movement, aiming,
   attacking). ~20 dependencies; the player lands here immediately after state 0,
   so it is the next blocker. Currently logs `unimplemented state animationId=1`.
2. **`g_playerAnimFunctions` (`0x00bebbd8`) static `.data` contents** — 52
   entries, of which `set_player_animations_functions` overwrites only 14. States
   5/6/7 dispatch into it through windows 0/0x13/0x26 and currently log the NULL
   index instead of faulting.
3. `EntityUpdateLookAtAngles` (`0x00459eb0`, 1058 b) — cosmetic head tracking,
   gated on `lookAtFlags & 0x10` which is 0 initially, so the no-op is faithful
   for now.
4. `FUN_00456a10` (785 b) — ground shadow sprite; blocked on `RotAverage4`
   (`0x0040ab00`).
5. `FUN_00429d50` body — joint-15 physics; gated on `velZ != 0` so the no-op is
   faithful until a limb detaches. Needs table `0x004ba950`.
6. `update_sounds` (`0x00474090`, 668 b) — no audio at all until this lands.
   Needs `ChkEntitySlide`, `ChkPlReachEntity`, `FUN_00474500`.
7. Cutscene NPCs dispatch `enemies_update_functions_tbl[id]`; only Zombie exists.

**Also found:** `SetupJointStructures` (`0x0048b9e0`) has **two divergent
implementations** split by parameter type (`void*` in `EngineStubs.cpp`,
`unsigned int` in `EntityModelLoader.cpp`). `MainMenu.cpp:422` passes a pointer
and silently gets the partial copy. Same trap class as
[[stub-overload-by-parameter-type]].

---

## Cutscene actors — landed 2026-07-29 (built, not yet run)

First in-game run of the intro cutscene: the script starts, camera cuts work,
Chris renders but in the wrong place and wrongly lit, Jill and Wesker do not
render at all, and the sequence stalls without crashing. Five defects found.

### 1. `enemies_update_functions_tbl` is 48 entries, not 32
Ids 0–21 are the monsters; **22–47 all point at `0x0046acf0`**, the shared human
character driver. Verified at `0x004d3c90`: the `0046acf0` run ends at
`0x004d3d50` — 48 dwords — and the next dword is 0. `cmd_em_set` gives cutscene
actors ids from 32 up (Chris 32, Jill 33, Barry 34, Rebecca 35, Wesker 36), so
`update_entities` indexed **past the end of the array into
`zombie_states_table`**, which follows it in the same translation unit. Jill read
`zombie_state_check` and ran zombie logic; her character init never ran.

### 2. `g_emdPathTable` was one entry short per block
Each 53-entry block is 4 player models + 22 enemy models + **10** filler
`em100a`/`em110a` entries + 17 character models. The port had **9**, so every
character model from index 35 up was shifted by one (id 37 = Jill loaded Barry's
`em1022`) and the Jill scenario's `+53` block base landed an entry early.
Verified against `0x004c1320`: indices 26–35 are `em100a`, 36 is `em1020`, 53 is
`char10`, 79–88 are `em110a`, 89 is `em1020`.

### 3. `room_events_check`'s restart label was on the wrong statement
`switchD_0041d6f9_default` sits at the **top** of the per-entry opcode loop in the
original: an opcode that completes without yielding re-enters the dispatcher and
runs the next opcode of the *same* entry in the same frame. The port had the label
down at the entry advance, so **every event executed exactly one opcode per frame**
and the `0xFF` end-of-script check was skipped on those paths. Only `0xF6`–`0xF9`,
`0xFE`, `0xFF` and state-0 opcode `0x08` yield.

### 4. §6c-ter resolved
The open question is answered: case 0 of event opcode `0x81` is
`MOV dword ptr [ESI+0xb8],0xbe62e4`, and `0x00be62e4` is `g_playerEntity`'s own
base (`update_player_anim` reaches its position as `0x00be6318` = base+0x34).
Ghidra renders it as `&g_playerEntityPointer` only because it named the entity
*object* `g_playerEntityPointer`. **The port's `&g_playerEntity` was correct** —
only the operand offsets were wrong: the target index is the signed word at **+4**
(the port read +10, i.e. the already-advanced script pointer) and
`lookAtYawStep` comes from the byte at **+8** (the original walks a second cursor
that ends at base+8 in every branch, while `scriptPtr` is at +10). Opcode `0x84`
also clears both timer bytes with one word store; the port cleared only the low one.

### 5. Player state 8 and the character NPC state machine were both absent
Two parallel subsystems, identical in shape:

| | Player | Character NPC |
|---|---|---|
| Update | `update_player_anim` state table `0x004d4550` | `character_npc_update` `0x0046acf0` |
| State 8 | `0x0044cf30` | `0x0047a490` |
| SCD behaviour table | `0x004beca0`, 10 entries | `0x004c4780`, 11 entries |

Both state-8 handlers run the behaviour once, again if `unk_e0`/`scd_entity_flags`
bit 1 is set, then refresh the weapon joint on bit 2 (bit 3 picks the hand). This
is how a cutscene animates anyone: event opcodes `0x83`/`0x84`/`0x85` write state
8 plus a behaviour straight into the entity. With state 8 missing the actor froze
in the pose the script had just set.

Player behaviours **0, 1, 5, 7, 8, 9** are transcribed (`PlayerAnimations.cpp`);
2, 3, 4 and 6 (`0x0044d380`, `0x0044d5e0`, `0x0044d930`, `0x0044dc50`) log their
address. Behaviour 1 is the remapped-animation player and 5 walks to the scripted
destination in `unk_c6`/`unk_c8`.

New file `src/game/entities/CharacterNpc.cpp` holds the NPC side: the update
driver, state 0 init, state 1 idle, state 8, all ten per-character init handlers,
the two trivial idle handlers and (since 2026-08-11) the full state-9 pathfind
layer - the follow-the-player mode - with its four walk behaviours and helpers.
**Not yet transcribed:** nothing in the NPC set - all 11 SCD behaviour handlers
(`0x0047a580`–`0x0047b760`) and idle behaviours 0–3 (`0x0046b580`, `0x0046b620`,
`0x0046b800`, `0x0046bb20`) are in; state 9 was the last remaining log-only
slot and is now transcribed too.

### The one-array-three-views dispatch table
Worth recording as a technique. The original indexes one block of function
pointers three ways with three overlapping bases:

```
base 0x004c2c50, index = entity->state           -> states 0..9      (id-slots 22..31)
base 0x004c2bf8, index = entity->id              -> per-character init (id-slots 32..47)
base 0x004c2cb8, index = entity->action_behavior -> idle behaviours   (id-slots 48..63)
```

`0x004c2c50` is `0x004c2bf8 + 22*4` and `0x004c2cb8` is `0x004c2bf8 + 48*4`.
Read as three separate arrays the overlap looks like a decompilation error;
it is one table, and `CharacterNpc.cpp` declares it as one. `0x004c2bf8` is never
dereferenced at a low index, which is why it can sit inside Wesker's SCA record.
A latent consequence, reproduced rather than guarded: an entity with id 22–31
reaches `npc_state0_init`, which dispatches `[id]` and lands back in the state
table — id 22 recurses into itself. No script uses those ids.

### Second run: NPCs render and animate. Lighting/camera still wrong.

All three actors now appear and animate. Two further defects found, plus one
discovery that invalidates a lot of previous "it logs the missing index" comfort.

**`printf` has never produced any output.** The project links
`/SUBSYSTEM:WINDOWS`, so there is no stdout and every `printf` in the port is
silently discarded — including all the `[player] unimplemented state …` and
`room_check_actions is NULL` diagnostics added in earlier sessions. The port's own
convention is `OutputDebugStringA`. New `src/DebugPrint.h` provides `dbg_printf`;
`CmdFunctions.cpp`, `GameLoop.cpp`, `PlayerAnimations.cpp` and `CharacterNpc.cpp`
are converted. Any future diagnostic must use it, and the build must be run from
Visual Studio (ODS on a switched task stack fail-fasts standalone).

**`RDT_Light` +0x10 is one 16-bit field, not two bytes.** The point-vs-directional
gate in `update_entity_lighting` is a **word** test — `0x00481673:
CMP word ptr [ECX+0x1c],0x0`, where `ECX = g_RdtPointer + i*0x14` and RDT+0x1c is
`lights[i]+0x10`. `cmd_light_set` writes it as a word as well. The port declared
`zero2`/`zero3` as separate bytes and tested only the low one, so a light with a
non-zero high byte was treated as a point light and had radial attenuation applied
where the original passes the colour straight through. Now `unsigned short
lightType`.

**`menu_restore_game_state` reloaded the D3D lights from the wrong array.** It used
base RDT+0x30 with stride 0x2C — the *camera* record stride — instead of the light
array at +0x0C with stride 0x14. Restoring the room after closing a menu therefore
reloaded lights from the middle of the light array and past its end.

**The RDT light set is room-wide, not per-camera — hypothesis closed.** Confirmed
two independent ways: `Room.cpp` derives the camera array as
`g_RdtPointer + sizeof(RDT)` (0x94), and `update_player_anim` reaches
`cameras[id].cam_from_x` as `g_RdtPointer[1].lights - 4 + id*0x2c`, which is
RDT+0x9C = cameras+8. Ghidra's own RDT type agrees: `LIGHT[3]` at offset 12,
`sizeof(LIGHT)` 20. So `update_entity_lighting` lighting every entity from
`lights[0..2]` is faithful, and nothing needs to copy per-camera lights.

**`FUN_004565f0` (`0x004565f0`) is still an empty stub, and it matters more than it
looks.** It builds the entity's ground-shadow / billboard quad at entity+0xE4, and
it takes its colour from scratch global `0x00be0dfc` — which is exactly what every
`char_init_*` and `player_state_init` writes a packed 0xRRGGBB triple to just
before calling it. That global is a general-purpose temp shared with `Joint_move`,
`check_room_collision`, `entity_rotate_toward_target` and a dozen others, so the
tint is a parameter passed by side channel, not persistent state. With the stub in
place no entity has a ground shadow at all — a visible part of "lighting looks
wrong".

### Third run: cuts progress, backgrounds mismatched, ambient truncated

`dbg_printf` output confirmed the cutscene now drives cameras 0 → 6 → 1 → 2 → 1
and spawns Wesker (id 36, slot 0) and Jill (id 33, slot 1), both with 15 joints.

**Background offsets: two bases in the original, one array in the port.** The
reported "camera 6 renders background 5, camera 0 is fine" is exactly this.
`load_room_bg` writes `*(int *)(iVar3 * 4 + 0xaea08c) = offset` with the camera
counter **already incremented**, so image *i*'s offset lands in slot *i+1* of the
array based at `0x00aea08c`. That is not a bug in the original, because
`load_room_bg_image` reads the same table from `g_bgCacheBuffer +
(&DAT_00aea090)[g_roomCameraId]` — a base **4 bytes higher**, which cancels the
shift exactly. The port used one `g_bgCameraOffsets` for both, so every camera
displayed the previous camera's image, and camera 0 only looked right because slot
0 is never written and happens to be 0. The reader now indexes `camera id + 1`,
and the array is 17 entries because the writer reaches slot `cameras_count`.

This is a new instance of a known trap: two pointers to the same object whose
bases differ by less than the object's size are one array, and the difference is
load-bearing.

**RDT ambient light was truncated to 8 bits.** `ambient_light` is a `COLOR` of
three **shorts** (Ghidra: `COLOR` at RDT+6, size 6) and `setBackColor` takes the
full 16-bit values — the menu passes `0x199` (409), well over 255. The port cast
each channel to `unsigned char`, so this room's logged `amb=1775,1983,1984` reached
the renderer as 239,191,192. Fixed in `cmd_light_set` and
`menu_restore_game_state`; `OptionsMenu` was already correct.

**Direction angles — chain verified faithful, data now instrumented.** Every step
that turns an angle into an orientation was checked against the original and
matches: `RotMatrix` (**0x00409df0**, not `0x004406a0` — that is
`GteRotationMatrixCalc`, the helper; both comments corrected),
`EntityComputeJointWorldMatrices` including the `RotMatrix(ENTITY+0x72,
ENTITY+0x20)` call and its `+3 & 0x80` gate, `rotate_entity` (0x0048c2a0), and
`ApplyLVAndMul0Matrix` (**0x0040a1f0**, which is just `CompMatrix`; the port's
comment said 0x0040a0b0, which is `MulMatrix0`). One cosmetic note: the call site
at `0x0048c285` pushes four dwords (`ADD ESP,0x10`) while `rotate_entity` reads
three parameters and takes the joint array from the global `ENTITY` — the fourth
push is dead, and the port's 3-parameter signature is correct.

So the transform chain is not the suspect; the angle data going into it is. The
`[scd]` line now prints, per entity and for the player, the rotation SVECTOR that
`RotMatrix` actually reads (`entity+0x72` = position.pad / angle low / angle high),
the resulting `m[0][0]`/`m[0][2]` yaw pair, and the `+3` gate byte. Distinct
`rot=` with identical `m0`/`m2` means the matrix is being overwritten downstream;
identical `rot=` means the writer is wrong. Spawn angles observed so far are
Wesker `0x900` (clean) and Jill `0x514` (not a typical scripted multiple), which
already hints at the second case.

### NPC and player SCD behaviour handlers the intro actually needs

From the logs, in priority order:

| Handler | Table slot | Used by |
|---|---|---|
| `0x0047a600` | NPC behaviour 1 | Wesker (36) and Jill (33) |
| `0x0047b0a0` | NPC behaviour 6 | Jill (33) |
| `0x0047a740` | NPC behaviour 2 | Wesker (36) |
| `0x0046b580` | NPC idle behaviour 0 | Jill (33) in state 1 |
| `0x0044d5e0` | player behaviour 3 | player |

These spin every frame, which is why the actors animate in place instead of
progressing. Transcribing them is the next block of work.

### Fourth run: backgrounds fixed. Facing and ambient traced to two writes.

Backgrounds now match their camera. Two more defects closed, both nailed by the
`rot=`/`amb=` fields rather than by reasoning.

**The facing bug was mine, introduced with `npc_state0_init`.** The log showed the
angle arriving correctly and then being wiped: `em0 rot=0,2304,0` at spawn, then
`rot=0,0,0` on the next frame. The original zeroes the X and Z components of the
rotation SVECTOR with `MOV word ptr [EAX+0x72],0` and `[EAX+0x76],0`. I wrote those
through the nearest-looking struct fields, `&ENTITY->position.x` and
`&ENTITY->angle` — which are `+0x6C` and `+0x74`. So it cleared `position.x`
(harmless, the renderer uses `localMatrix.t`) and the **yaw** every actor had just
been given by `cmd_em_set`. Now addressed by explicit offset, because no single
named field lands on either address.

Generalisable: when the original writes a 16-bit value at an offset that is not a
struct field's own address, do not reach for the nearest field name. `+0x72` is
`position.pad` and `+0x76` is the high half of `angle`; both look wrong written
that way and are easy to "correct" into a bug.

**Ambient light was truncated to 8 bits in three places, and the one that mattered
was `LoadRoomRdt`.** `setBackColor` takes 12-bit PS1 channels and scales by
255/4096; `g_red_color`/`g_green_color`/`g_blue_color` then feed
`g_d3dAmbientColor`, the ambient term for model rendering. The mansion hall's
`amb=1775,1983,1984` was cast to `unsigned char` first, so 1775 became 239 and the
GTE background colour came out **14 instead of 110** — an 8x underexposure on every
room, which is why the characters rendered near-black against a correctly lit
background. Fixed in `LoadRoomRdt` (the path that runs on every room load),
`cmd_light_set` and `menu_restore_game_state`.

**NPC animation handlers landed.** `npc_scd_01` (`0x0047a600`, plain animation
playback — used by both actors, which is why only the player moved),
`npc_scd_02` (`0x0047a740`, turn then walk to the scripted target) and
`npc_scd_06` (`0x0047b0a0`, turn in place), plus their helper
`npc_apply_walk_speed` (`0x0047a4f0`). All three share one contract: action_state
0 sets up, 1 advances, 2 raises the script's completion flag via
`Flg_on(g_SysFlags, scd_anim_param)`.

Still stubbed and still logging: NPC SCD behaviours 0, 3, 4, 5, 7, 8, 9, 10, NPC
idle behaviours 0–3, NPC state 9, and player behaviours 2, 3, 4, 6. The intro
asked for idle 0 (`0x0046b580`) and player behaviour 3 (`0x0044d5e0`); neither
decompiled cleanly on this pass (`0x0046b580` fails in Ghidra and needs
`create_function` plus a manual read).

**Unrelated but visible: model textures are being truncated on upload.** The run
logs `[TEXPAGE] TRUNCATED: w=256 h=256 bpp=8 pitch=256 need=65536 avail=1264`,
from the guard in `MarniSystem.cpp` that asks the OS how much of the TIM pixel
buffer is actually readable. `m_pPixelData` aliases the loaded file buffer rather
than a sized allocation, and only a fraction of the claimed 64 KB is committed, so
the upload keeps a handful of rows and discards the rest. The same base is reported
twice with different `avail` (1264 then 38128), which means the region's committed
size changes between the two calls — the buffer is not sized for the image the TIM
header describes. This is the texture path, not the SCD system, and it is worth its
own investigation: untextured models are part of what still reads as "bad lighting".

### Fifth run: models and lighting correct. `Entity::angle` was the wrong width.

Ambient and facing both confirmed fixed in-game. One defect left from the run:
Wesker walked in a perfect circle that is not in the cutscene.

**`Entity::angle` was an `int`; the original always touches it as 16 bits.** The
log made it unmistakable — Wesker's yaw climbed by exactly 1200 per 30-frame
sample, i.e. **40 per frame, monotonically, forever**, while `Add_speedXZ` pushed
him along it. A constant step that never converges is a turn-toward-target that
never reaches its target.

`entity_rotate_toward_target` (`0x004899b0`) reads and writes
`*(short *)(_ENTITY + 0x74)` in all four places. The port's `Entity` declared
`int angle; // 0x74`, spanning 0x74–0x77, so:

- `angleStep - ENTITY->angle` pulled `angle_z` (0x76) into the arithmetic, and
- `ENTITY->angle = ...` was a 32-bit store that clobbered `angle_z`,

which broke the "delta is within one step, snap exactly to the target" clause and
left only the unconditional step. Fixed at the struct: `short angle; // 0x74` plus
`short angle_z; // 0x76`, which is what the layout actually is — 0x72/0x74/0x76 are
the three components of the rotation SVECTOR `RotMatrix` reads from entity+0x72.
`sizeof(Entity)` is unchanged and the `static_assert` still holds.

That one declaration fixes every 32-bit angle write in the port at once, including
several in the zombie code (`ENTITY->angle = ENTITY->angle + 12`, etc.) that had
the same latent problem.

Two narrower width bugs in the same two functions, both now matching the original:
`entity_rotate_toward_target` widened the step to int *before* doubling
(`(short)param_2 * 2`, not `(short)(param_2 * 2)`), and `entity_check_angular_los`
compares `param_1 * 2` as a signed int rather than truncating it to 16 bits.

**Chris running forward is player behaviour 3 (`0x0044d5e0`), now landed.** Six
sub-states: turn on the spot until aligned within 0x16a, run at speed 0xd2 with
footsteps on frames 0 and 10 until within 250 units, then four frames shedding
0x1e speed each, then back to state 1 with the completion flag raised.
`healthStatusFlags` bit 7 short-circuits the deceleration. Also landed NPC
behaviour 0 (`0x0047a580`), a bare animation advance.

Still stubbed and logging: NPC SCD behaviours 3, 4, 5, 7, 8, 9, 10, NPC idle
behaviours 0–3 (`0x0046b580` still fails to decompile in Ghidra and needs a
manual read), NPC state 9, and player behaviours 2, 4, 6.

### Sixth run: `RotMatrixY` mixed two fixed-point scales — everything moved 4x

One root cause behind three separate symptoms: the player overshooting his run
target, the crash in `LookupFootstepZone`, and Wesker circling. The `[run]`
diagnostic made it arithmetic rather than guesswork:

```
[run] pos=14511,5835 target=14300,7800 dist=1976 speed=210
[run] pos=14422,6670 target=14300,7800 dist=1136 speed=210
[run] pos=14331,7504 target=14300,7800 dist=297  speed=210
[run] pos=14243,8339 target=14300,7800 dist=542  speed=210   <- overshoot
```

The bearing and target were right and the distance closed correctly — but each
frame moved **840 units on a speed of 210, exactly 4x**. 840 per frame cannot land
inside a 250-unit stop threshold, so the run never terminated.

**`GteSin`/`GteCos` return 14-bit amplitude (1.0 = 0x4000); matrix elements are
4.12 (1.0 = 0x1000).** `RotMatrixY` combined them with `GteFixedMul12`, so
`(0x4000 * 0x1000) >> 12` produced `0x4000` where `0x1000` is correct — every
rotation it built came out 4x oversized. `Add_speedXZ` feeds that matrix to
`ApplyMatrixSV`, which divides by 4096, so the movement came out 4x too far. Added
`GteFixedMul14` and used it in `RotMatrixY`.

The scope is exactly one function: `GteFixedMul12`'s other callers are matrix x
matrix and matrix x vector, where both operands are already 4.12, and every other
sin/cos consumer in the file does its own `>> 0xe`. `RotMatrixY` was the only place
the two scales met.

Note the port's `RotMatrixY` is a closed form, not a transcription — the original
scales the matrix up by 4, calls `FUN_00440e30` (which composes via
`GteRotationMatrixYXZ` at 1.0 = 0x4000, matching the trig tables) and scales back
down. The closed form is legitimate, but it silently inherited the wrong shift.
**Lesson: a rewritten-for-clarity GTE routine needs its fixed-point scale checked
against the original's, because the original's `<<2 ... >>2` sandwich is exactly
what encodes the convention.**

Wesker's circle was the same bug: flying 4x past his target every frame, he could
never close the 150-unit gap, and `entity_rotate_toward_target` kept steering him
at a receding goal. With correct speeds both thresholds are reachable — 93 < 150 for
the NPC walk, 210 < 250 for the player run.

**Also added a port-only guard in `LookupFootstepZone`.** The scan is byte-faithful
and unbounded in the original too; it terminates because the last zone in every
room's table is a catch-all. That only holds while the entity is inside the room, so
an out-of-bounds entity ran off the end of the RDT and faulted. The guard caps the
scan and logs `[footstep] no zone contains (x,z)` instead — it never fires in
correct operation, and it stops this from masking the next thing that moves an
entity out of bounds.

### Seventh run: movement correct. Stall is player behaviour 2.

Movement confirmed right in-game after the `RotMatrixY` fix. The remaining stall is
straightforward and the log names it outright: the player sits in
`st=8 beh=2 act=0` with `[player] unimplemented SCD behavior 0x0044d380` repeating,
and both event entries park on opcode `0xFD`. Behaviour 2 was a stub, so it never
reached its action_state 2 and never called `Flg_on(g_SysFlags, scd_anim_param)` —
the flag the waiting opcode polls.

Landed both remaining player behaviours the intro asks for:

- **`0x0044d380` (behaviour 2)** — turn toward the scripted target, then walk to it.
  The player twin of `npc_scd_02`: state 1 turns until aligned within 0x16a, state 3
  walks with footsteps on frames 8 and 0x16 and finishes within 150 units. One
  ordering detail preserved: the original evaluates
  `dist < 0x96 && (Flg_on(...), (healthStatusFlags & 0x80) == 0)`, so `Flg_on` fires
  on every in-range frame while the return to state 1 is gated on the health flag.
- **`0x0044dc50` (behaviour 6)** — turn in place at a fixed 0x38 per frame,
  finishing when `turn_toward_target` closes the angle at the script's own step.

`FUN_0047a4f0` is now `entity_apply_walk_speed` (non-static, shared) since both the
player and NPC walk behaviours call it.

Remaining stub that will matter soon: **player state 1 (`0x00495180`)**, the control
state machine, already logged once when the script handed the player back to state 1
mid-cutscene. It is the last big player-side gap and the original blocker recorded
at the top of this document.

### Eighth run: intro plays end to end. Sound landed.

**The intro cutscene now runs to completion** — all three actors animate and move
correctly, the camera cuts match their backgrounds, Chris reaches the dining room
door, and the scene hands off to the fade-out and door animation. That closes the
original goal at the top of this document.

Remaining gap was audio, and it was two separate things.

**Voices: `play_sound_and_voice_effect` was a stub under a wrong address.** The stub
cited `0x0047f870`, which is `update_room_bgm` — an unrelated function. The real
address is **`0x00475340`**, found by decompiling `cmd_sfx_set` (`0x00461a80`) and
reading its callee. Now implemented in `SoundSystem.cpp` along with its three
dependencies:

- `voice_load_and_play` (`0x004753c0`) — builds `.\usa\voice\<name>.wav` from
  `g_StageVoiceNamesTable[stageId] + id*9` (the port's tables were already
  extracted at exactly that 9-byte stride) and loads it into `g_BgmSoundBank`.
- `voice_set_pan` (`0x00475640`) and `voice_mixer_reset` (`0x004756b0`) — trivial.
- The voice state block at `0x00ae9ec8`-`0x00ae9ee0`, none of which existed.

The script handshake is the part worth knowing: `cmd_sfx_set` **sets**
`g_main_state_flags` bit 17 after requesting a voice and type 2 **clears** it, and
event opcode `0xF7` waits on that bit. With the stub in place the bit was only ever
cleared as a side effect of `UpdateMusicWaitState` finding no BGM playing — which is
why lines advanced silently rather than hanging.

`FUN_004753b0`, which seeds two bank-offset globals, is an empty function in the
original; those globals are kept but marked inert.

**SFX: `g_emSndBanks` was both too small and only a quarter loaded.** Two defects
in one array:

- The array is **48 records of 2 ints = 96 ints**, declared `int[64]`.
  `PlayEntitySnd` indexes `g_emSndBanks[soundType * 2]` for soundType up to 0x2f,
  i.e. element 94 — past the end of an `int[64]`.
- `Room_LoadEnemySoundBanks` looped `while (piVar7 <= &g_emSndBanks[48])`, treating
  element 48 of an int array (base+192 bytes) as record 48. It stopped after **25
  records**. The original bounds it by absolute address: `piVar7` walks from
  `0x00ac99f0` in 8-byte steps while `piVar7 <= 0xac9b6f`, which is records 0
  through 47.

So every enemy and footstep sound from id 25 up had a null bank, and `PlayEntitySnd`
looked it up, found 0 and returned silently. `sounds_reset` cleared 64 ints for the
same reason and is now 96.

This is the §6g array-stride bug class the plan already flagged for the other four
sound-bank arrays — `g_RoomSfxBanks`, `g_CharacterSfxBanks` and `g_SfxBanks` are
still `int[64]` walked with ad-hoc strides and should be audited the same way.

**Correction to an earlier note in this document:** `PlayEntitySnd` takes **one**
parameter in the original (`0x0047fbf0`), not two. The two-argument call sites push
a second dword the callee never reads — dead pushes, the same pattern as
`rotate_entity`'s fourth argument and `FUN_004756b0(9,0,0)`. Comments in
`PlayerAnimations.cpp` and `CharacterNpc.cpp` claiming the port was missing a
parameter have been corrected.

### Ninth run: voice logic correct, asset root was not

`[voice] could not open file: .\usa\voice\V001_00.wav` — the voice code was right
end to end (correct line names pulled from `g_StageVoiceNamesTable`, one per
scripted line, advancing as the cutscene progressed); it was looking in the retail
location. The original hardcodes `.\usa\...`, and this project keeps assets under
`.\assets\USA\`. `LoadFile` already routes through `ResolveAssetPath` for that
reason, but the sound loaders did not.

Fixed in **`loadSndBankFromWav`** and **`findAndOpenFile`** rather than at the call
sites: those two are the only entry points that take a WAV path, so resolving there
covers cutscene voices, enemy and footstep banks, weapon banks and menu sounds in
one place — and keeps future callers correct by default. `LoadSoundBank` and
`Room_LoadEnemySoundBanks` both build `.\usa\sound\<name>.wav` and were failing the
same way, so this is very likely the rest of the missing SFX on top of the
`g_emSndBanks` sizing fix.

Verified the assets are where the resolver points: `assets/USA/voice/v001_00.wav`
exists (lowercase on disk, uppercase in the table — fine on Windows) and
`assets/USA/sound` holds 547 WAVs.

### Tenth run: voices play. SFX had no data at all.

The missing SFX was not a code bug. `SoundTables.cpp` declared

```c
static const char* g_RoomSndData[145][48] = {};
```

an all-NULL placeholder for the per-room sound name table at **0x004cfae0**. Every
function around it was correct - `Room_LoadEnemySoundBanks` looped properly,
`PlayEntitySnd` computed the right slot, the WAV loader worked - they simply had
nothing to load. The giveaway in the log was an absence: not one
`loadSndBankFromWav` call for `sound\*.wav`, only voice loads.

Sobering detail worth remembering: two **real** bugs were found and fixed in this
same path first (`g_emSndBanks` sized 64 ints instead of 96, and a loop bound
covering 25 records instead of 48). Both were genuine defects, neither could have
produced a sound, and fixing them changed nothing observable because the data was
never there. Check tables for placeholder initialisers before auditing the code
around them.

**Extracted the real table from the binary.** The outer table at 0x004cfae0 is 145
pointers (indexed `stageId * 29 + roomId`), each to an array of 48 `char*` slots.
Recovered with a PE walk over `assets/ResidentEvil.exe` - map VA to file offset via
the section table, then follow both levels of indirection and read the strings. 102
of 145 rooms carry names; 1232 names, 318 distinct.

Doing this through the exe rather than Ghidra was the right call for volume: the same
job via `read_memory` would have been thousands of round trips. Verified twice:

- all 318 distinct names have a matching WAV under `assets/USA/sound`, and
- the generated C table was re-parsed and diffed back against the exe - 145/145 rows
  identical.

**`g_stageId` is 0-based.** The mansion main hall is stage 0 room 0 (table index 0),
which is why its footstep names sit in slots 45-47 (`ft_wdA`, `ft_wdB`, `taore_wd`).
The RDT and background path builders confirm the bias by using `g_stageId + 1`. Index
29 - stage 1 room 0 - is genuinely empty, so reading the table with a 1-based stage
would have looked like "this room has no sounds".

Two diagnostics added pending the next run: `[emsnd]` prints the resolved name table
and every record it loads, and `[entsnd]` prints which `g_emSndBanks` record a
footstep resolves to and whether it holds a bank.

### Eleventh run: full intro with sound. Shadows are the last gap.

Voices, footsteps and the scripted gunshot all play; the whole intro cutscene runs
correctly through to the fade-out. The only remaining visual gap is the ground
shadow sprites.

**Shadows are a subsystem, not a one-liner.** The chain, with current status:

| Piece | Address | Status |
|---|---|---|
| Shadow quad builder | `0x004565f0` | **landed this pass** |
| `BillboardSetColor` | `0x00456710` | already implemented (see the overload note) |
| `BillboardAdjSize` / `BillboardSetSize` | `0x00456760` / `0x00456790` | implemented |
| `entity_add_fade_sprite` queue write | `0x00456810` | **stubbed** - needs `RotAverage4` |
| `RotAverage4` | `0x0040ab00` | **absent from the port entirely** |
| `g_FadeSpr` / `g_FadeSprCount` | `0x00bca0e0` | **do not exist** |
| Sort-key tables | `DAT_004c0630`, `DAT_004c0b34` | **not extracted** |
| `DrawFadeSpr` (the renderer) | `0x00456d30` | **stubbed** |
| Player shadow builder | `0x00456a10` | deferred, also blocked on `RotAverage4` |

So four more pieces plus a new GTE projection routine, two globals and two data
tables. Nothing here is hard, but it is a subsystem's worth of work and it is
independent of the SCD system.

**Landed: `FUN_004565f0` (0x004565f0), the shadow quad builder.** It was an empty
stub mislabelled "SCA init helper", so every character's sprite block at
entity+0xE4 stayed all zeros - there was never anything for a sprite pass to draw.
Transcribed from the disassembly; the quad layout is documented at the
implementation. Two details worth recording:

- The tint is a **single dword store** of scratch global `0x00be0dfc` into
  `quad[1].z`/`.pad`. That is the packed 0xRRGGBB every `char_init_*` and
  `player_state_init` writes there immediately before calling - the parameter is
  passed by side channel, not by argument.
- The four corners land at byte offsets 0x58/0x60/0x68/0x70, which is exactly what
  `BillboardAdjSize` and `BillboardSetSize` patch - a useful cross-check that the
  layout is right.

**Found while landing it: another parameter-type overload split.** `BillboardSetColor`
was declared `extern void BillboardSetColor(void*, int, int, void* color)` in
`Zombie.cpp` with a matching empty stub in the same file, while the real
implementation in `PlayerAnimations.cpp` takes `unsigned int color`. Those are
different C++ overloads, so it linked cleanly and **every zombie call site bound to
the do-nothing one** - zombie death-shadow tinting has never run. The stub is gone
and the extern corrected. Third instance of this trap in the project, after
`SetupJointStructures` and the `cmd_room_action` / `ScdEventEntry_Create`
placeholders.

**Also fixed: the colour argument was an address, not a colour.** All four call
sites passed `DAT_00ffff50` or `&DAT_00ffff50`, a global someone created after
Ghidra rendered the constant `0x00ffff50` as `&DAT_00ffff50` - the same
"value that looks like a pointer" trap already recorded for `&DAT_00808080`. The
argument is a packed 0xRRGGBB tint; all four now pass the literal. `DAT_00ffff50`
in `Globals.cpp` is now unreferenced by this path and its "velocity decay reference
data" comment is wrong.

### Shadow renderer — landed 2026-07-29 (built, not yet run)

New file `src/game/FadeSprite.cpp` holds the whole ground-shadow path:

| Piece | Address | Notes |
|---|---|---|
| `MulMatrixVec3` | `0x004410e0` | 3x3 by vector in the fixed-point pipe's scale |
| `ProjectEffectSprite` | `0x0040aa50` | projects one corner, returns depth |
| `RotAverage4` | `0x0040ab00` | four projections, mean depth |
| `entity_add_fade_sprite` | `0x00456810` | queue write - the half that was missing |
| `DrawFadeSpr` | `0x00456d30` | sort, re-project, submit |
| `g_FadeSpr` / `g_FadeSprCount` | `0x00bca0e0` | 32 records of 144 bytes |
| `g_FadeSprParamIndex` | `0x004c0630` | 5 stages x 32 rooms x 8 cameras of record ids |
| `g_FadeSprParams` | `0x004c0b30` | 20 records of 0x14 bytes |

The three GTE routines went into `GteMatrix.cpp` next to the rest of the pipe.

**The 14-bit convention holds here too.** `MulMatrixVec3` shifts products down by
14, which is right because `SetGlobalScaledRotationMatrix` stores the pipe's
matrices `<< 2` - so 1.0 is 16384 there, not the 4096 a `MATRIX` holds. Same
distinction that made `RotMatrixY` move everything 4x too far.

**Queue record layout**, confirmed from both the writer and the reader:

```
+0x00  world position (x, y, z, pad) as ints
+0x10  120-byte copy of the entity quad from entity+0xE4
+0x88  short y offset      +0x8A short angle      +0x8C short sort key
```

The four corners land at record +0x68/+0x70/+0x78/+0x80, which is quad byte
+0x58/+0x60/+0x68/+0x70 - the offsets `FUN_004565f0` writes and `BillboardAdjSize`
patches. Three independent views agreeing is a good sign the layout is right.

**Tables extracted from the exe**, same PE-walk approach as `g_RoomSndData`. Two
observations that simplify the code: `g_FadeSprParams[*].unk_0c` is 16 in all 20
records and `forceFlag` is 0 in all 20, so the `z != 0` branch inside `AddFadePoly`
that would pin the alpha never fires on this path. The placement constants are
`0x004c0d20 = 2` (per-sprite vertical step, so coplanar shadows do not fight for
depth), `0x004c0624 = 0` and `0x004c0620 = 0`.

**`AddFadePoly`'s parameters are floats, not ints.** The port's signature
(`SpriteRenderer.cpp`) types `u`, `v` and `clut` as `int`, and `DrawFadeSpr` feeds
them `(int)(corner * 0.0025f)`. The original does no such truncation - at
`0x00456ee1`..`0x00456f06` it is `FILD` / `FMUL [0x004af290]` / **`FSTP float ptr`**,
and `0x004af290` is `0x3b23d70a` == `0.0025f` exactly (1/400). The `v` slot is the
immediate `0x3f800000`, i.e. float `1.0`. So the three values are
`(corners[1].x / 400.0f, 1.0f, corners[1].z / 400.0f)` as floats. Passing them as
truncated ints turns `1.0f` into integer `1`, which the renderer reads back as
`1.4e-45` - a shadow scaled to zero. Silent, and it looks like nothing was queued.

**The `rgb` argument is a pointer at record +0x18, not +0x08.** Ghidra types it
`byte` (`rgb = (cVar2 + g_spriteAnimActive * '(') - 8`) which is a decompiler
artifact; the disassembly is
`LEA EDX,[EBX + EBP*0x8]; ADD EDX,0xbca0f8` with `EBP = g_spriteAnimActive * 5`,
so it is `0x00bca0f8 + i*0x90 + slot*0x28` == record `+0x18`. `FadeSprite.cpp`
computes `+0x10 + slot*0x28 - 8` == `+0x08`, which is 0x10 low and reads the
world-offset shorts as a colour.

`FUN_00456a10`, the player's own shadow builder, is still deferred - it was blocked
on `RotAverage4`, which now exists, so it is straightforward next.

#### Still no shadows on screen: the submit path is the wrong one

The queue and the maths are in place, but `AddFadePoly` is where this stops, and it
is an architecture problem rather than a bug to patch. Established facts:

- **The original's universal draw path is the ordering table.** `g_OT`
  (`0x008e1d60`) holds 0x84-byte primitives, 0x20 per render buffer.
  `OT_InsertPrimitive` (`0x004402f0`) links one into a depth bucket, and it has
  **nine** callers - sprites, text, and the TMD renderer (`FUN_00483270`) all go
  through it.
- **`CMarniDirect3D` vtable[10] (`0x00448300`) is that submit**, not a texture
  setter. It branches on the primitive type at `prim[0]`: type 10 on the software
  renderer re-derives the depth from `prim[6] >> 4`, everything else inserts as
  given. The port's name `VTable_SetTexture` is legacy - `Marni3DObject.cpp:649`
  already records the real meaning.
- **The port's `VTable_SetTexture` assumes every primitive is a TMD object.** It
  calls `TmdQueueObject` unconditionally. So routing a fade poly through vtable[10]
  today would hand it to the TMD queue and drop it.
- **The original's fade primitive is type 4**, not the type 10 the port's
  `AddFadePoly` writes. Type 10's special case in vtable[10] only applies when
  `self+0x30c == 5` (the software renderer); otherwise every type inserts as given.

#### Resolved: `0x008f88e8` is the GTE rot+trans scratch, and type 4 is a 3D object

The previous note here claimed `AddFadePoly` reads *projected screen coordinates*
from `0x008f88e8`. That was wrong, and it was the thing blocking progress.
`SetRotAndTransMatrix` (`0x00482e00`) is the **writer**: it copies 8 dwords from its
`MATRIX*` argument and negates `t[1]`. So the block is one 32-byte PSX `MATRIX`
(`short m[3][3]`, 2 pad, `int t[3]`):

```
0x008f88e8  m[0][0] m[0][1] m[0][2]  m[1][0] m[1][1] m[1][2]  m[2][0] m[2][1] m[2][2]
0x008f88fa  (2 bytes pad)
0x008f88fc  t[0]      0x008f8900  t[1] (negated)      0x008f8904  t[2]
```

`DrawFadeSpr` already calls `SetRotAndTransMatrix(&composed)` before projecting, so
this is simply "the current world matrix". `AddFadePoly` needs no projected vertices
at all - it hands the renderer a **transform**, and the geometry lives elsewhere.

**The 0x84-byte OT primitive** (base `g_OT` = `0x008e1d60`, stride `0x84`, `0x20`
per render buffer, so `g_RenderBufferIndex * 0x1080`):

```
+0x00  int    type = 4
+0x04  void*  next            (written by OT_InsertPrimitive, not AddFadePoly)
+0x08  float  world[4][4]     row-major; rows are the *columns* of the PSX matrix
       +0x08 m00/4096  m10/4096  m20/4096  0.0
       +0x18 m01/4096  m11/4096  m21/4096  0.0
       +0x28 m02/4096  m12/4096  m22/4096  0.0
       +0x38 t[0]      g_OTIndex*y + t[1] + x      t[2]      1.0
+0x48  float  scaleX  = corners[1].x / 400
+0x4C  float  scaleY  = 1.0
+0x50  float  scaleZ  = corners[1].z / 400
+0x54  int    objectHandle  = g_VideoDriverArray_068[tpage]   (bail to 0 if zero)
+0x58  int    texturePage   = g_TexturePageTable[g_VideoDriverArray_06c[tpage] * 0xDF]
+0x5C  float  r, g, b        (+0x60, +0x64); 0 if the byte was 0xFF
+0x68  float  0.0
+0x6C  float  copy of +0x5C..+0x68
+0x80  int    6
```

Note the matrix is the **transpose** of the PSX rotation - row-vector D3D
convention. Note also that arg 7 (`x`) is added to the **Y** translation, so
`g_FadeSprParams[].offsetX` is misnamed in `FadeSprite.cpp`: it is a per-camera
shadow *height* bias. And when `r == g == b` the colour is forced to
`(0, 0, 0x3b83126f == 0.004f)`, i.e. a flat black shadow.

**Type 4 is drawn by `FUN_00446e40`, the same routine the TMD renderer uses.** The
OT flush is `FUN_0044b4f0`, called from `CMarniDirect3D_Present`
(`0x00448ff0`) as `FUN_0044b4f0(self + 0x324)`. It walks the depth buckets
**deepest-first** (`local_20` starts at the last bucket and steps back `0xc` bytes)
and dispatches on `prim[0]`:

```
case 1  -> FUN_0042ba60
case 2  -> FUN_0042bd70
case 4  -> FUN_00446e40(prim, self + 0x338)     <- fade / shadow poly
case 10, 0xc, 0xd, 0xe -> OT_InsertPrimitive     (software re-queue)
default -> MarniDebugPrint("doesn't support this type...")
```

So a shadow is not a sprite. It is a **4-vertex 3D object drawn with a world matrix
and a non-uniform scale** - the exact path the port's `TmdRenderer` already drives
successfully for the intro NPCs.

**The actual gate on shadows appearing is `CreateTexturedQuad`, not `AddFadePoly`.**
`AddFadePoly` returns 0 early when `g_VideoDriverArray_068[tpage] == 0`, and that
handle is the execute buffer produced by `FUN_0046c230` at the end of
`CreateTexturedQuad` (`0x0046fb50`). In `TextureLoader.cpp` every Marni viewport
call in that function is commented out as stubbed, so the handle is permanently 0.
`LoadShadowMaskTexture` succeeds and the texture page is live - the geometry is what
was never built.

`CreateTexturedQuad` reads its `vertexData` **column-major at stride 4** and emits
an 11-float vertex (`{x, y, z, nx, ny, nz, 1, 1, 1, u, v}`, matching
`CMarniViewport2::SetVertex`'s documented `0x2C` bytes). For the shadow call
`CreateTexturedQuad(0, 0x2F, rectConfig)` in `GameState.cpp` that is:

```
x:  -400  400  -400   400
y:     0    0     0     0     <- flat, which is why AddFadePoly's scaleY is 1.0
z:   400  400  -400  -400
u:     0 0x1A     0  0x1A     <- divided by DAT_008ed4fc[tpage] (texture width)
v:     0    0  0x1D  0x1D     <- divided by DAT_008ed500[tpage] (texture height)
n:  (0, 1, 0)                 <- rectConfig[20..22], shared by all four vertices
index list: 0, 1, 3, 2
```

The `±400` extent is exactly the `1/400` divisor in `AddFadePoly`: the quad is
authored at 400 units and each entity rescales it by
`corners[1].x/400` and `corners[1].z/400`. The port's comment describing
`rectConfig[20..23]` as "4 texture dims + 2 flags" is wrong - it is a normal.

**Remaining work**, now unblocked and in dependency order:

1. Implement `CreateTexturedQuad`'s body against the real `CMarniViewport2` calls
   (or a D3D11 equivalent) so slot 0 yields a non-zero object handle. Without this
   nothing downstream can draw.
2. Give `AddFadePoly` the real signature (`float u, v, clut`) and have it fill the
   0x84-byte primitive above rather than a `TextureDraw`.
3. Route type 4 to the TMD object draw instead of `TmdQueueObject`'s unconditional
   assumption, keyed on `prim[0]`.
4. Fix `FadeSprite.cpp`: `rgb` at record `+0x18 + slot*0x28`, pass the three scale
   values as floats, rename `offsetX` to `heightBias`, and drop the `[shadow]`
   diagnostic.

### The intro cutscene stall at the dining-room door

**Root cause: `room_check_actions` is an all-NULL placeholder table.**
[GameState.cpp:1764](../src/game/GameState.cpp#L1764) is
`void* room_check_actions[ROOM_CHECK_ACTION_COUNT] = { nullptr };`. It was left
that way deliberately (to avoid the stub-overload trap) but the consequence is that
the entire door / item / examine interaction layer is inert — the same
placeholder-data-table failure as `g_RoomSndData`.

Everything *around* it is already correct. `update_player_position`
(`0x0041c060`, [PlayerAnimations.cpp:2059](../src/game/PlayerAnimations.cpp#L2059))
is fully implemented: it builds the 600-unit reach probe, walks
`g_RoomItemEventTable`, and dispatches `room_check_actions[*entry]`. It even
already carries a `[player] room_check_actions[%u] is NULL` diagnostic, so the
failure was instrumented before it was understood.

**The full door path**, for reference when implementing:

1. `cmd_door_set` (opcode `0x0C`) registers the door in `g_RoomItemEventTable`.
   *Already works.*
2. `update_player_position` detects the player in the door's action zone and calls
   `room_check_actions[5]` = `check_door` (`0x0041b6d0`). **Missing.**
3. `check_door` picks the approach side by comparing the player's X against the
   zone, sets `unk_c4 = ±1`, clears `animFrameId`, clears `g_message_flags & 0x40`,
   sets `has_enter_switch_zone |= 0x20` (and `|= 0x10` when the door swings the
   other way), and sets `g_main_state_flags2 |= 0x400000`. Returns 0.
4. The player state machine routes `action_behavior` 10 (and `0x11`) to
   `FUN_00457390` — the door-opening animation. **Missing.** It is a four-state
   machine on `action_state`:
   - state 0 — `attackAnim = 2`, rotate to face the door, consume `0x400000`
     to pick the turn direction, then `action_state = 1`
   - state 1 — `attackAnim = 0x33`, or `0x35` when `has_enter_switch_zone & 0x10`;
     `action_state = 2`; `FUN_004567d0(0xbe63c8, 800, 700, 700, 700)`
   - state 2 — door SFX via `Play3DSnd(2, 0x2d, ...)` (or `0x23` when
     `g_main_state_flags & 0x80`), joint motion through `Joint_move`
   - state 3 — teleport the player through: `g_playerDisplacement` /
     `player_distance_z` scaled by `attackDirection` (`0x10fe` / `0xb45`, or
     `0x842` / `0x57a` in rooms 5 and 0xE), set `g_message_flags |= 0x40`,
     clear `g_main_state_flags2 & 0x400000`, reset `animationId = 1`
5. The room change then proceeds through the normal fade path
   (`g_fading_state` / `g_fading_counter`, driven by
   [MainLoop.cpp:170](../src/game/MainLoop.cpp#L170)), which already works.

**One hazard to check before wiring step 4.** `FUN_00457390` is reached through the
`action_behavior` switch in `caseD_0` (`0x00495320`), whose jump table resolves to
base `0x004d456c` (index 9 → `0x004d4590` = `FUN_00495df0`, index 10 →
`0x004d4594` = `FUN_00457390`, index `0x11` → `0x004d45b0` = `FUN_00457390`). That
range **overlaps** `g_playerStateFunctions`, which the port places at `0x004d4550`
with 16 entries ending at `0x004d458f`. One of the two bases is wrong. Resolve the
overlap before adding entries to either table — see the overlapping-dispatch-table
note in the audit above.

#### Resolved by tracing: the script is correct, `player_state_01_control` is the gate

An opcode trace of all four slot-deactivation paths settled this. The intro script
does **not** stall and is **not** killed by an unimplemented opcode. Slot 0's tail:

```
04C0:FE  04C1:FD.c04   (x23)   wait loop: yield, poll cmd_bit_test, repeat
04C2:07.c05                    one embedded command, 6 bytes
04C8:FF                        end of script
bytes@04C0: FE FD | 07 06 | 05 06 17 00 | FF
```

The final command is `0x05 cmd_bit_op` with `bank=6` (`g_message_flags`),
`sel=0x17`, `mode=0` (set). Since the mask is MSB-first
(`0x80000000 >> (sel & 0x1F)`), `sel=0x17` is bit 23 → **`0x100`**, confirmed live by
`msgFlags` going `FEFF` → `FFFF` on the frame the script ends.

`0x100` is the player-input gate in [GameLoop.cpp:302](../src/game/GameLoop.cpp#L302):
input is masked whenever `unk_03 & 0x20` is set **or** `g_message_flags & 0x100` is
clear. So the cutscene's last act is deliberately *"give the player control"* — the
handover is correct behaviour, not a premature exit. Slots 1/2/3 exiting via
"opcode 0x09 from another slot" is likewise normal: a master script retiring its
sub-scripts.

**The single blocker is that `player_state_01_control` (`0x00495180`) is a stub**
([PlayerAnimations.cpp:1360](../src/game/PlayerAnimations.cpp#L1360)), which is why
Chris freezes, why input does nothing, and why no door can ever be reached. Its real
body switches on `animFrameId` (`0x85`):

| `animFrameId` | Behaviour |
|---|---|
| 0 | `FUN_004956a0()`, then **falls through to case 2** |
| 2 | switch on `action_behavior`: 0 idle, 1-8 turn/walk/run, 9, **10 and 0x11 -> `FUN_00457390` (door open + warp)**, 0xB-0xF, 0x10 |
| 1 | `g_playerAnimFunctions[animFrameId + 0x26]` |
| 3 | weapon/aim family, `action_behavior` 0x12-0x1A, then `EntityUpdateWeaponJoint(0)` |
| 4 / default | `player_scd_behavior[action_behavior]` + `unk_e0` bits 2/4 |

Two things make this cheaper than it looks: the `default` tail is **identical to the
already-ported `player_state_08`**, and `check_door` sets `entity+0x85 = 0`, i.e.
`animFrameId = 0` — precisely the case that falls through into the `action_behavior`
dispatch. So the door path is: `check_door` arms it, `animFrameId = 0` routes it, and
`FUN_00457390` performs the open and the warp.

Ordering for the remaining work:

1. `player_state_01_control` skeleton: health/death branch, `isBeingAttackedFlag`,
   the poison drain on `healthStatusFlags & 0x62` (120-frame timer, 7 when bit
   `0x40`), `nAttackTmer`, and the `animFrameId` switch. Reuse the
   `player_state_08` body for the `default` tail.
2. `FUN_004956a0` (entered from case 0) and `FUN_00495960` (`action_behavior` 0,
   idle) - enough to unfreeze Chris.
3. `room_check_actions[5] = check_door` plus `FUN_00457390` - the door and the warp.
4. The remaining locomotion handlers (`FUN_00495a70`, `FUN_00495c90`,
   `FUN_00495ed0`, ...) so the player can actually walk to the door.

### Asset root is now compile-time, not a runtime rewrite

The runtime resolver was the wrong design and it broke: `ResolveAssetPath` scanned
for a `\usa\` component and spliced in `\assets\USA\`, which is **not idempotent**.
Once the sound loaders resolved a path and handed it to `CreateSound`, which
resolved again, `.\assets\USA\voice\V001_00.wav` matched its own `\USA\` component
and became `.\assets\assets\USA\voice\V001_00.wav`.

Replaced with a compile-time root in `system/AssetPath.h`:

```c
#ifdef _DEBUG
#define GAME_DATA_ROOT ".\\assets\\USA\\"
#else
#define GAME_DATA_ROOT ".\\usa\\"
#endif
```

All 29 path literals now read `GAME_DATA_ROOT "data\\fontus.tim"` and friends, and
`ResolveAssetPath` / `AssetPath.cpp` are gone. A macro cannot double-apply, there is
no string transform sitting between the game and the filesystem, and the literals in
the decompiled code keep the shape the original's have.

**The one case that needed care:** `g_bgPathTemplate` and `g_maskPathTemplate` are
fixed-layout strings patched by character *index* at runtime, and the original's
indices are relative to its own 6-character root. Those are now built as
`GAME_DATA_ROOT "stageS\\rcSRRC.pak"` with the array grown to 40 bytes for the
longer debug root, and all 31 index sites in `Room.cpp` go through
`GAME_DATA_PATH_IDX(originalIndex)`, which rebases from `GAME_DATA_ROOT_LEN`.
Verified by evaluating the arithmetic for both roots: indices 0x0B/0x0F/0x10/0x11/
0x12 land on `S`/`S`/`R`/`R`/`C` in the bg template and 0x11-0x14 on `S`/`R`/`R`/`C`
in the mask template, in debug and release alike.

`LoadFile`'s install-directory branch keeps the original's literal `filePath + 8`,
which is only meaningful for the retail root, so it is compiled out for debug builds.

### Resolved: `PlayerEntity::speed` is misnamed, not mislaid

Confirmed 2026-07-30. `g_playerEntity` is `0x00be62e4`, so `+0x76` is `0x00be635a`
and `+0x78` is `0x00be635c`. Both are written as **separate 16-bit fields**, and
`InitPlayerEntity` writes both - so they are two distinct fields and `Entity`'s
layout (`angle` 0x74, `angle_z` 0x76, `speed` 0x78) is the correct one.
`PlayerEntity`'s `SVECTOR speed; // 0x76` is wrong.

**But the consequence is much smaller than feared, and the port is not moving the
player wrongly because of it.** Every site that writes `g_playerEntity.speed.*`
actually intends offset `0x76`, and the misnamed field lands there:

| Site | Writes | Original |
|---|---|---|
| `cmd_player_pos_set` (`0x00430f60`) | `position.pad`, `directionAngle`, `speed.x` | writes `0x72`/`0x74`/**`0x76`** |
| `player_anim_dispatch_4ba360` (`PlayerAnimations.cpp:404`) | same triple, zeroed | writes **`0x76`** |
| `display_die_screen` | zeroes it | writes **`0x76`** |

Those three are the rotation SVECTOR `RotMatrix` reads from `entity+0x72`
(`{pad, angle, angle_z}`), not a velocity - so they are at the right address under
the wrong name. And movement never goes through this field at all: `Add_speedXZ`
reaches it as `ENTITY->speed` (`Entity`, `0x78`) and **recomputes it from
`move_speed_current` on every call**, so a stale value cannot persist or drift.

So the fix is a **rename plus a two-byte shift**, not a behavioural change:
`short angle_z; // 0x76` then `SVECTOR speed; // 0x78`, with those sites retargeted
to `angle_z`. `unk_7e` / `unk_82` are referenced nowhere, so the tail is free.
The one site needing care is `InitPlayerEntity`, which writes `0x76` *and* the
speed vector: the port's `speed.x/y/z/pad` currently covers `0x76`-`0x7D`, so it
zeroes `0x76` but misses `0x7E`. Deliberately not changed yet - it is a layout edit
made while position bugs are being chased, and it is provably not their cause.

### Still open on the reported symptoms
- **Wrong position.** Nothing yet proves which write is wrong. The `[em_set]`
  and `[scd]` diagnostics print the spawn coordinates and per-second positions.
- **Wrong lighting.** `update_entity_lighting` (`0x00481660`) is faithful, and it
  lights every entity from `g_RdtPointer->lights[0..2]` — RDT+0x0C, a single
  room-wide set. `g_RdtPointer` is written once at RDT load and never re-pointed,
  so if the original expects per-camera light data there, something has to copy it
  on a camera cut. The `[scd]` line prints the three lights next to
  `g_roomCameraId`: if they never change across a cut, that is the bug.
- `room_camera_and_lighting_update` (`0x00473ff0`) is still an empty stub and is
  called every frame. Despite the port's name it is the **room object** pass —
  it walks the itembox/cover and desk tables, `RotMatrix`es each object and
  updates it. Doors and furniture, not lighting.

---

## Remaining, in recommended order

### 1. `room_check_actions` — 18 handlers, all still `nullptr`
Opcodes `0x24` and `0x2D` are **guarded no-ops** until these land (the guard in
`cmd_room_action` stops them jumping through a null slot). This is the
door / item / typewriter interaction layer — a subsystem, not leaf work.
Addresses and Ghidra names are listed in §6b. Indices `0x0F`–`0x11`
(`0041be70`, `0041bed0`, `0041bf90`) are **not defined as functions in Ghidra**
and need `create_function` first.

### 2. §6c-ter — event-VM opcode `0x81` target index
Reads the target index from byte `+10`; the original reads `+4`.
**Blocked on a question:** the original assigns `&g_playerEntityPointer` (the
address of the pointer global) to `scd_target_ptr` for target type 0, while the
port assigns `&g_playerEntity` (the entity). Resolve which is correct before
touching the offset — fixing the offset alone could just move the bug.
The index is also sign-extended in the original and unsigned in the port.

### 3. §6g — the other four sound-bank arrays
`g_RoomSfxBanks` (2), `g_CharacterSfxBanks` (16), `g_emSndBanks` (48),
`g_SfxBanks` (16) all share `SndBankSlot` but are still declared `int[64]` and
walked with ad-hoc strides. Same bug class §6f fixed for `g_SndBank`.
**Needs a decision:** the three walkers (`sounds_reset`, `DestroyAllSoundBanks`,
`UpdateSoundFade`) stop at 9 records for `g_CharacterSfxBanks` when the array is
16 — faithful (9) reproduces Capcom's leak, corrected (16) diverges. The port
currently clears 32, so it is already diverging by accident.

### 4. Event VM diff
Undiffed: `room_events_check` itself (`0x0041d6a0`), the four `scd_event_cmd_*`
helpers, `scd_event_state2_movement`, `scd_event_state3_set_behavior`.
`scd_event_state1_anim` is done and correct apart from §6c-ter.

### 5. Three blocked leaf stubs
| Function | Blocker |
|---|---|
| `FUN_00473b10` (503 b) | `FUN_00485c60` (820 b) + `FUN_00485fa0` (488 b), neither implemented |
| `FUN_00484d90`, `FUN_00484e40` (43 b each) | 3 undeclared globals each + `ExecAsync` callbacks (`FUN_00484c40` / `LAB_00484dc0`) absent from the port |
| `play_sound_and_voice_effect` | `FUN_004753c0`, `FUN_00475640`, `FUN_004756b0` |

---

## Verification debt — read this first

No part of this work has been exercised in-game. The change set is large and
several items **activate code paths that were previously dead**:

- 28 opcode fixes across `CmdFunctions.cpp`
- 18 leaf functions gone from empty stub to real behaviour
- Opcodes `0x14`, `0x2A`, `0x2D`, `0x3D`, `0x4A`, `0x4B` were **silent no-ops**
  and now execute
- `0x18` and `0x1F` now touch palette data and TMD processing on **every room
  load** — a wrong assumption there should surface immediately as visual
  corruption, which makes it cheap to detect by running it
- A new member in the `.gwipe` wipe block shifts the intra-block layout of
  everything above `0x92cc`

**Known blocker to testing — traced and fixed 2026-07-28, NOT yet run.**
The port reached the game loop after character selection and nothing appeared.
Cause: `room_set` loaded the room completely and then called
`check_camera_switch(1)` (`0x00462cc0`) to put it on screen — and that function
was an empty stub, as was `display_room_camera_bg` (`0x00462d50`). Nothing ever
called `cut_set()`, so the camera transform and background image were never set
up. `is_entity_in_switch_zone` (`0x00462d90`) was also a placeholder that
unconditionally returned 1. All three are now implemented in `Room.cpp`, and
`room_events_check` / `room_state_reset` — commented out in `game_loop` — are
restored to match the original's call sequence at `0x00480d85`.

With the display path connected, the next crash was in the PAK LZW decompressor
(`unpack_pakfile_`, `0x00425ab0`), reached via `cut_set` →
`Room_LoadCameraSprites` → `load_room_masks`. Four bugs, all fixed: the
dictionary was modeled as two separate arrays (`g_pakDictPrefix` /
`g_pakDictChar`) when the original is **one array of 12-byte records** at
`0x00d2b0b0` (prefix at +4, char at +8); new entries were written with the
already-advanced `prevCode`; the KwKwK case appended the previous *code* instead
of the previous *first character*; and `pak_decomp_reset` cleared the wrong field
with the wrong stride.

The trace confirmed the rest of the chain is sound: `game_start`,
`InitializeGame`, `init_room`, `Room_SetupCamera` and `load_room_bg_image` all
match the original, the SCD pointers are set correctly by `LoadRoomRdt`, and
`g_stageId`/`g_roomId` are correctly `BioCardLayout` fields so `bio_card.dat`
supplies the starting room.

---

## Techniques worth reusing

- **Verify every handler's instruction length in 3 queries** by enumerating writes
  to the stream-pointer global (`search_instructions` on `ADD` / `INC` / `MOV`
  against `[0x00bf0800]`). The `MOV` result is what makes it rigorous: if only the
  interpreter assigns the pointer, the ADD/INC totals are provably exhaustive.
- **A correct length proves nothing about the body.** `0x17`, `0x46`, `0x4D` and
  `0x1F` all passed length checks while writing to the wrong place.
- **Diff at the disassembly level, not the decompiled C.** Four defect classes were
  invisible or misleading in Ghidra's C output: instruction length, access width,
  struct field identity, and base-pointer indirection (`MOV ESI,dword ptr [addr]`
  renders the same as taking a global's address).
- **Ghidra's `(x & 0xffffffXX) >> N` is a byte offset**, equal to
  `(x >> 8) << (8 - N)` — never an element index.
- **Compare parameter lists, not symbol presence**, when auditing for leftover
  stubs: a stub with a different signature becomes a C++ *overload* that links
  cleanly and splits callers by argument type.
- Two globals whose commented addresses are closer than `sizeof` apart are
  modeling one original object and will silently diverge.

---

## Twelfth run: the door-open sequence was the last stub on the door path

Reported symptom: the intro plays to the end, Chris walks to the dining-room door,
then the script hands control back and **the cutscene letterbox bars never come
down** and no door transition happens.

### The door chain, and where it stopped

Everything up to the animation was already in place — the earlier entries in this
document had landed `check_door`, `door_try_enter` and `player_state_01_control`
since the "ordering for the remaining work" list above was written. Tracing the
whole chain in the port:

| Step | Function | Status before this pass |
|---|---|---|
| register the door | `cmd_door_set` (`0x0C`) | works |
| detect the zone | `update_player_position` (`0x0041c060`) | works |
| arm the animation | `check_door` (`0x0041b6d0`), `room_check_actions[5]` | landed |
| turn input into a behaviour | `player_input_to_behavior` (`0x004956a0`) | landed |
| dispatch it | `player_ctrl_frame0/1` (`0x00495320`/`0x00495520`) | landed |
| **play it and warp** | **`FUN_00457390`** | **empty reporting stub** |
| load the room | `door_try_enter` (`0x0041b400`) then `room_transition_load` | landed |

`check_door` raises `unk_03 |= 0x20`; `player_input_to_behavior` turns that into
`action_behavior = 0x11` with `animFrameId = 1` **with no action button required**
(`PlayerAnimations.cpp:1864`); `player_ctrl_frame1` routes 10 and `0x11` to
`FUN_00457390`. That was still `player_state_report_missing(...)`, so the player
parked in the door's action zone forever.

### Landed: `player_door_open_sequence` (`0x00457390`)

Four steps on `action_state` (entity+0x87). The original selects them with
`CMP EAX,0x3 / JA default`, so it is a genuinely bounded 0-3 switch, not a table:

- **0** — turn to face the door and advance once the residual angle is inside
  `0x3e0`.
- **1** — pick animation `0x33` (or `0x35` when the door swings the other way) and
  size the shadow quad. **Falls through into case 2** — there is no jump at
  `0x00457504`.
- **2** — advance the opening animation, firing the door SFX at fixed frames.
  `Joint_move`'s completion return is what advances `action_state`
  (`ADD byte ptr [0x00be636b],AL`).
- **3** — the warp: displace the player through the doorway, raise
  `g_message_flags` bit `0x40`, consume `g_main_state_flags2` bit `0x400000`, and
  reset to normal control.

Details that only the disassembly gives:

- **The turn step is proportional, not fixed:** `(angle & 0x3fc) >> 2`, an ease-in
  toward the door. Every angle access is 16-bit, on the `Entity::angle` width fixed
  in the fifth run.
- **Case 0's second turn block writes through the global `ENTITY` pointer**
  (`MOV EDX,dword ptr [0x00bebcd4]` then `[EDX+0x74]`), not `&g_playerEntity`.
  `update_player_anim` points `ENTITY` at the player first so they are the same
  object; written the original's way rather than retargeted onto the named field —
  the same discipline the `+0x72`/`+0x76` incident forced.
- **`move_speed_current` (0xC2) is reused as the SFX step counter** in cases 2 and
  3, not as a speed. The frame tests are `15*counter - animFrame == -0xc` and
  `9*counter - animFrame == 1`, and the first is computed **once**, from the
  pre-increment value, before the `msf & 0x80` branch.
- **`g_main_state_flags & 0x80` is the climb/vault entry** (behaviour 10, set by
  `player_input_to_behavior`) as opposed to a plain door (`0x11`). It selects SFX
  `0x23` over `0x2d` and a different displacement — and in case 3 the X term is
  negated while the Z term is not, an asymmetry that is in the original.
- **`unk_03` bit `0x10` is check_door's "swings the other way" flag**, mirroring
  every Z displacement and the animation id.
- `g_message_flags |= 0x40` is a **byte** OR (`OR byte ptr [0x00bebcc0],0x40`).
- Case 3's reset is **one dword store** to `0x00be6368`, covering `animationId` /
  `animFrameId` / `action_behavior` / `action_state` together.

### Landed: `BillboardSetRect` (`0x004567d0`)

The asymmetric sibling of `BillboardSetSize`: four edges instead of two
half-extents, so the shadow quad can be off-centre. Stores `-left` and `-back`,
which is why the call sites read oddly — `(quad, right, left, front, back)` with
`(800,700,700,700)` in case 1 and `(500,500,700,700)` in case 3. The corners land
at quad+0x58/0x60/0x68/0x70 with x at +0 and z at +4: the same layout
`BillboardAdjSize` patches and `FUN_004565f0` builds, which is a third
independent confirmation of that quad layout.

### The letterbox bars are `g_main_state_flags` bit `0x10000`

Chased separately, because they are not part of the door path. The bars are
`g_ColorRect` / `g_ColorRect2` in `MainLoop.cpp` — `y=-130,h=38` and `y=92,h=38`
at width 328. With `g_ScreenOffsetY = 120` that is screen rows -10..28 and
212..250, i.e. **28 visible rows top and bottom, leaving a 184-row band** — which
matches the reported screenshot exactly (a 640x480 client at 150% DPI shows a
552/718 band).

They are gated purely on the intensity ramp at `0x0042946a`:
`TEST byte ptr [0x00be41c2],0x1` — bit `0x10000` of `g_main_state_flags`. Set gives
`g_spriteAnimIntensity` ramping up by 16 to `0xF0`; clear ramps it down to 0, and at
0 nothing is drawn. The port's transcription of that whole block is byte-faithful,
including the `<= 0xEF` and `>= 0x10` bounds and the `!= 0` draw gate.

**How the script raises them:** nothing in the binary ORs `0x10000` into
`0x00be41c0` with an immediate, and no `OR` touches `0x00be41c2` at all — the write
goes through the SCD bit-bank indirection. `cmd_bit_op`'s **bank 5 is
`g_main_state_flags`**, and its mask is MSB-first (`0x80000000 >> (sel & 0x1F)`),
so **bit index 15 is `0x10000`**. That is the same indirection that hid the
input-enable write recorded in the intro-cutscene note.

Only `title_state` (`0x00430473`) and `die_state` (`0x00481310`) clear the bit with
an immediate, and `InitializeGame`, `TimeoutDeathFadeOut`, `menu_restore_game_state`
and `options_menu` all deliberately **preserve** it through their mask writes.

**Confirmed against the original by the user with Cheat Engine**, breaking on writes
to `0x00be41c0`: the clear happens at **`0x0046070c`** inside `cmd_bit_op`, with
`ESI = 0x00BE41C0` (bank 5), `ECX = 0x0F` (bit index 15) and `EAX = 0xFFFEFFFF`
after the `NOT` — i.e. the `AND` branch,
[CmdFunctions.cpp:194](../src/game/CmdFunctions.cpp#L194). So the script byte
sequence is `05 05 0F 01`, and the port's handler for it is already correct.

### Proven from the RDT: the intro script never lowers the bars

**The main hall is `ROOM1060.RDT`, not `ROOM1000.RDT`.** Its script blocks are
`scd_opcodes` (RDT+0x64) `= 0x2fd8c`, a 0x94-byte block, and `scd_opcodes2`
(RDT+0x68) `= 0x2fe20`, the main block, which opens with a **dword** slot table
whose first entry is its own size (`0x40` = 16 dwords). Slot 0 — the intro — spans
rel `0x40`–`0x4cc`. Note `ROOM1000.RDT` uses a *different* header shape (a bare
dword table with no size prefix), so the parse is per-room, not universal.

Scanning the block for `05 05 0F <mode>` gives four sets and three clears:

| rel | slot | mode |
|---|---|---|
| `0x56` | **0 (intro)** | **set** |
| `0x59e` / `0x694` | 8 | set / clear |
| `0x6b2` / `0x89a` | 9 | set / clear |
| `0x95c` / `0x99a` | 15 | set / clear |

Slots 8, 9 and 15 are self-contained: each raises and lowers the bars itself.
**Slot 0 only ever raises them.** The byte level makes the asymmetry explicit —
slot 0 opens at rel `0x40` with

```
fe 00
07 06  05 06 17 01     cmd_bit_op bank 6 sel 0x17 mode 1  -> msgFlags &= ~0x100 (input OFF)
05 01 01 00
05 02 03 00
f8 f9 0a 00
07 06  05 05 0f 00     cmd_bit_op bank 5 sel 0x0f mode 0  -> msf |= 0x10000  (BARS ON)
```

and closes at rel `0x4c0` with

```
fe fd                  wait loop
07 06  05 06 17 00     cmd_bit_op bank 6 sel 0x17 mode 0  -> msgFlags |= 0x100 (input ON)
ff                     end of script
```

It clears the input bit on entry and sets it on exit — but it *only* sets the bars
bit, never clears it. This also confirms the slot-0 tail recorded earlier in this
document, at rel `0x4c0`: that trace was right, it was simply about `ROOM1060`.

**Conclusion: bars still up after the intro is correct script behaviour, not a
bug.** They are lowered by the *destination* room's script after the door
transition — raising in one slot and clearing in another is the normal pattern
across every room's RDT. So the letterbox is purely a downstream symptom of the door
not firing and needs no fix of its own; do not chase it separately. The `[bars]`
diagnostic stays only to confirm the bit goes down once the transition works.

### Diagnostics added, pending the next run

All are marked `DIAGNOSTIC - remove once confirmed`:

- `[bars]` in `main_loop` — every change of the `0x10000` flag and the intensity
  ramp. Tells us definitively whether anything ever clears the bit.
- `[door] try_enter rec=... lock=... need=... dest=...` — once per distinct door
  record, so the log shows whether this door reads as open, character-barred, or
  needing an item.
- `[door] try_enter bailed: msf&0x80 set` — the one early return in
  `door_try_enter`.
- `[door] BEGIN TRANSITION` — the moment the room change is committed. If this
  never prints the door was never accepted; if it prints and no room loads, the
  fault is downstream in `room_transition_load` / the `g_openMenuFlag` handoff.
- `[dooranim] st=...` — each step change of the new state machine, plus the inputs
  that select the variant.

The pre-existing `[zone]` diagnostic in `update_player_position` already reports
every action-zone hit with the handler index and whether it is present, which is
what distinguishes "the probe never reaches the zone" from "the handler is absent".

### Still open

- **`player_ctrl_frame2` (`0x00495330`) is still a stub.** In the original, case 0
  is `FUN_004956a0()` followed by a fall-through into case 2, so `0x00495330` *is*
  the bare `action_behavior` switch. The port inlines both into
  `player_ctrl_frame0`, so nothing is lost while `animFrameId` is only ever 0 or 1
  on this path — but if `[player] unimplemented ... animFrameId 2` ever appears,
  this is why.
- `door_try_enter`'s note about being "blocked on `g_eventItemUsedFlag`" is stale;
  it is implemented and wired as `room_check_actions[1]`.
- `g_openMenuFlag`'s address is still unresolved: `door_begin_transition` notes the
  original writes a byte at `0x00be961b` while the port declares the symbol at
  `0x00d22760`. The `[door] BEGIN TRANSITION` line prints `openMenu` so the next
  run shows whether the handoff into `room_transition_load` actually takes.

### Thirteenth run: the door fires; the room loader was gated off on a wrong premise

The door chain now works end to end. From the log:

```
[door] try_enter rec=005FF004 lock=00 need=00 dest=05 flags0B=07
[door] BEGIN TRANSITION rec=005FF004 dest=05 cam=07 msf=8001000C openMenu=0
```

`lock=00` (unlocked), `dest=05`, `flags0B=07` so `flags = 0x07 & 0xC0 = 0` - the
full-reload path, door sound enabled. The blackout and the door-close SFX are
therefore correct behaviour for this door: entry 2 of the event table is `act=1`,
i.e. `door_try_enter`, whose transition is an instant black `draw_rect` +
`Task_sleep(1)` + `StMask` - **not** a fade, and not the player's own door
animation. Confirmed by the table dump, which also shows why only this door is
live - every other entry has flag bit `0x80` (disabled):

```
[2:act=1 flg=01 rec=005FF004 zone=1000,15000+1950,3800]   <- dining-room door, enabled
[0,1,3,4,5: flg=81]                                        <- disabled
```

That zone matches the intro's final position exactly, which is why it fires with no
button press.

**The blocker was `room_transition_load`'s own gate, and its stated premise was
wrong.** The comment claimed `0x00444770` "is also what LOADS the destination
room's data". It is not. `FUN_00444770` is just

```c
FUN_004443c0(); FUN_00444540(); FUN_00444500(); Task_exit();
```

the **3D door-opening animation** task. `room_set()` / `init_room()` are the loader,
and the original updates the room id from the door record immediately before calling
them (`0x004814f9`):

```c
BuildEnemySnap();
g_AttractMode_RoomCameraId = g_roomId;      // remember where we came from
g_scaPoolPtr = g_scaPoolBase;               // rewind the SCA pool
g_roomId = g_nextRoomDest & 0x1f;           // <-- the destination, BEFORE room_set
if (g_nextRoomDest < 0x20) room_set();
else { g_stageId = (g_nextRoomDest >> 5) - 1;
       if (Flg_ck(g_PlayerFlags,0) && g_stageId < 2) g_stageId += 5;
       init_room(); }
```

The old gate omitted the whole block **including the `g_roomId` assignment**, so when
it had previously been enabled `room_set()` re-ran the room the player was leaving -
which is the real cause of the `cmd_item_model_set -> Effect_CreateBillboard` crash
that motivated the gate. The animation task and the room load are independent.

Now enabled, along with the post-wait screen rebuild
(`Room_SetupCamera` / `load_room_bg_image` / `Room_ApplySpriteFlags`), which the
original runs on the reload path and `check_camera_switch(1)` only on the
camera-only path (`flags & 0x80`).

**Leaving the animation task unspawned is safe by construction:** `FUN_004443c0` is
what sets `g_main_state_flags` bit `0x4000000`, and the wait loop in
`room_transition_load` polls exactly that bit. With no task the bit is never set and
the wait falls straight through - an instant cut. Tasks here run on switched stacks,
so a stub that plainly returns would unwind off the task stack into address 0.

**Not done: the 3D door animation.** It is its own subsystem - image buffers at
`0x00ac592c` / `0x00acd710`, a model/animation table at `0x00aafce0` relocated in
place, `ResolveAnimPointers`, a renderer (`FUN_00444540`, a `draw_rect` caller) and
a teardown (`FUN_00444500`) that must clear `0x4000000`. Three functions plus data.
Until then every door is an instant cut.

**Also still stubbed on this path:** `BuildEnemySnap` (`0x0048f150`), which snapshots
enemy state into `g_savedEnemyStates` so a room remembers its enemies across a
re-entry. Harmless for a first traversal, wrong on the way back.

**Note on the entry camera.** The door record's camera field is latched into
`g_nextRoomCameraId` (`0x00be0dd4`) and then **never read** - xrefs show one WRITE
from `room_transition_load` and one `.data` reference, no code reads. `room_set`
does its own `g_roomCameraId = 0; check_camera_switch(1)` (gated on the camera-lock
bit `0x100000`, which `room_set` clears at `0x00477879`), so the correct camera is
reached by the zone test against the player's newly placed position, not by the
record. The `[roomtrans] loaded:` diagnostic prints both so a mismatch is visible.

#### The dining room loads. The crash was the diagnostic, not the game.

`Run-Time Check Failure #2 - Stack around the variable 'aux' was corrupted` in
`scd_debug_snapshot`. The room load itself worked - the dining room was visible
before the fault.

`[scd]` and `[scd2]` built their lines with unbounded `sprintf` into `char line[1024]`
and `char aux[512]`, sized against the main hall. The dining room's event table is
longer, so the first successful transition overflowed `aux` and /RTC1 caught it. All
eleven append sites now go through

```c
static void dbg_append(char* buf, int cap, int* pos, const char* fmt, ...)
```

which truncates via `_vsnprintf_s(..., _TRUNCATE, ...)`, and both buffers are 4096.

Two latent faults in the same block were guarded at the same time, both of which
would have fired on a transition frame rather than in steady state:

- `cams[g_roomCameraId]` read the camera array with no bound. `room_set` resets
  `g_roomCameraId` to 0, but a stale higher value can outlive a room that has fewer
  cameras than the one being left. Now bounded against `cameras_count`, printing
  `cam<N>(OUT OF RANGE)` instead.
- `*g_ScdEventTable[i].scriptPtr` was dereferenced unconditionally; it is NULL
  between a room load and the first `room_events_check`. Now prints `op=null`.

Worth generalising: instrumentation sized against the one room that was being
debugged is a latent crash on every other room, and when it fires it reads as a bug
in whatever was just enabled. Bound the writes *and* the reads.

### Fourteenth run: transition confirmed. Ground truth for the entry point.

The dining room loads and is enterable. Two positional complaints remain: Chris has
to be walked closer to the door in the hall before the zone fires, and he arrives in
the dining room further from the door than expected.

**The door record, read straight out of the RDT.** The dining-room door is registered
by the *init* script (`ROOM1060.RDT` block at RDT+0x64), and its 0x18-byte record
sits at file `0x2fbbc`:

```
e8 03 98 3a 9e 07 d8 0e 06 00 03 07 00 05 b4 78 00 00 6c 20 00 08 00 81
zone x=1000 z=15000 w=1950 d=3800   -> x 1000..2950, z 15000..18800
+08 doorType=06  +09 sfx=00  +0B=07 (cam 7, flags 00)  +0C lock=00  +0D dest=05
ENTRY x=30900  y=0  z=8300  ang=2048
```

Every field the runtime logged matches (`lock=00 need=00 dest=05 flags0B=07`), and
the zone matches the value recorded earlier in this document - so `cmd_door_set` and
the record offsets are correct, and the zone rectangle is **not** the problem.

**The entry point is `x=30900`, but the run reported `pos=30624`.** Z (8300) and the
angle (2048) match exactly; only X is off, by 276. A pure-X change with zero Z change
at a heading of `0x800` (180 degrees) is the signature of movement *along the facing
direction*, so the player is being displaced ~276 units **after** placement rather
than placed at the wrong spot. The value 30624 appears nowhere in the record, which
rules out an offset misread.

**Fixed: the entry position's access widths.** X and Z are ZERO-extended into the
32-bit matrix translation and Y is SIGN-extended, which is explicit in the original
and is not a decompiler artifact:

```
0048148f: XOR EAX,EAX / MOV AX,[rec+0x0E] / MOV [0x00be6318],EAX   <- zero-ext
004814a8: MOVSX EAX, word ptr [rec+0x10]  / MOV [0x00be631c],EAX   <- sign-ext
004814b3: XOR EAX,EAX / MOV AX,[rec+0x12] / MOV [0x00be6320],EAX   <- zero-ext
```

X/Z are room coordinates that legitimately exceed `0x7FFF`; Y is a height that goes
negative. The port sign-extended all three, so any entry point with X or Z at or
above `0x8000` landed ~65536 units away. **Latent for this door** (30900 and 8300 are
both positive, so it does not explain the 276), but a real defect and fixed.

**Diagnostics added for the next run:**

- `[roomtrans] entry point from rec:` - the raw values applied, so a drift after
  placement is separable from a bad read. Should print `x=30900 y=0 z=8300 ang=2048`.
- `[zone] MISS ...` in `update_player_position` - the previous `[zone]` line only
  fired on a hit, so "Chris stops short" produced silence. This prints the tested
  point, the zone bounds and the signed per-axis deltas with `(in)`/`(OUT)` markers.
  The deltas matter because both comparisons in `is_point_in_action_zone` are
  UNSIGNED: a point below the origin wraps to a huge value, so the raw compare cannot
  tell you which side you are on.

Note the probe point is **600 units ahead of the facing direction**, not the player's
position, for any entry without flag `0x40` - and the dining-room door's entry is
`flg=01`, so it is the reach probe that has to land in the zone.

### Fifteenth run: `MovePlayerXZ` was an empty stub - the reach probe never rotated

Three of the four open items closed with this run's log, and the fourth turned out to
be one unimplemented leaf.

**Placement is correct.** `[roomtrans] entry point from rec: x=30900 y=0 z=8300
ang=2048` followed by `pos=30900,0,8300` - an exact match to the RDT record. The
`30624` seen in the previous run was **collision push-out in the hall**: the room
load was still gated, so the player was placed at the dining room's coordinates while
still standing in the outgoing room's geometry, and got shoved 276 units. It was
never a bad read. Worth remembering as a trap: a half-enabled transition produces
position numbers that look like a data bug.

**The letterbox bars close out exactly as the RDT analysis predicted.**
`[bars] flag0x10000=0 intensity=0xF0` ramping down through `0x00` - the destination
room's script clears bank-5 bit 15 and the intensity ramp fades the bars out. No code
change was needed; the earlier conclusion that this was purely downstream of the door
not firing is confirmed.

**Root cause of the missed action zone: `MovePlayerXZ` (`0x0041b350`) was
`void MovePlayerXZ(int, SVECTOR*, SVECTOR*) { }`** in `WeaponDamage.cpp`, with no
address recorded anywhere. `update_player_position` builds its 600-unit reach probe
by calling it with the **same buffer as input and output**, so the stub left the
vector unrotated and the probe was always `(+600, 0)` in world space regardless of
facing. The `[zone] MISS` diagnostic made it unmistakable - two stationary frames:

```
pos=2487,0,17568 ang=2250  ->  probe pt=(3087,17568)   delta (+600, 0)
pos=8859,0,13719 ang=3137  ->  probe pt=(9459,13719)   delta (+600, 0)
```

Chris's own position `2487,17568` is **inside** the door zone; only the unrotated
probe overshot the far edge by 137 units, which is why walking him a little closer
worked. With the rotation restored, at `m[0][0] = -3900` the probe becomes
`(1915, 17750)` - inside on both axes.

The real address came from the call site (`0x0041c087`), not from a comment. The body:

```
0041b358: MOV word ptr [ESP+0x2],CX      ; local SVECTOR .y = angle, .x = .z = 0
0041b373: CALL 0x00409df0                ; RotMatrix(&local, &g_matrixScratch)
0041b38a: CALL 0x00409cd0                ; ApplyMatrix(&g_matrixScratch, offset, 0x00be11b0)
0041b396: MOV EDX,dword ptr [0x00be11b0] ; out->x = (short)result.x, etc.
```

Routing through `g_playerPosScratch` (`0x00be11b0`) as the intermediate is faithful
and safe - the caller overwrites it with the final probe on the next instruction.
The angle is read as 16 bits (`MOV CX, word ptr [ESP+4]`) even though callers push a
dword.

This also unblocks weapon hit detection: `WeaponDamage.cpp` calls `MovePlayerXZ` five
times to build the attack box, and every one of those was returning an unrotated
vector too.

**Found while implementing it: `ApplyMatrix`'s destination is a `VECTOR`, not an
`SVECTOR`.** The original ends in three **dword** stores:

```
00409d9c: MOV dword ptr [EAX],ECX        ; ->x
00409d9e: MOV dword ptr [EAX+0x4],EDX    ; ->y
00409da5: MOV dword ptr [EAX+0x8],ECX    ; ->z
```

The port declared `SVECTOR* v1` and wrote shorts at stride 2, so a caller reading the
result back as ints - which is exactly what `MovePlayerXZ` does - would have seen the
low halves of x and y interleaved. Signature corrected and its one other caller
(`Zombie.cpp:2656`) now uses a `VECTOR` and truncates at the store.

Two details of the original that deliberately do **not** change the arithmetic, both
worth recording because they look like discrepancies:

- It copies the matrix into a scratch struct with every element `<< 2` and calls
  `MulMatrixVec3`, which shifts down by 14. Net `(m<<2 * v) >> 14` == `(m*v) >> 12`,
  which is what the port computes directly. Same 14-bit-vs-4.12 distinction as the
  `RotMatrixY` bug.
- It also copies `t[0] / -t[1] / t[2]` into that struct, but `MulMatrixVec3` reads
  only `m[0..8]`, so the translation is dead. `ApplyMatrix` is rotation only.

**Still open on the arrival:** the player lands exactly where the record says, so if
the position still reads as wrong the suspect is the arrival **camera**, not the
placement. `room_set` does `g_roomCameraId = 0; check_camera_switch(1)`, and the
record's `entryCam=7` is never read by the original (write-only global, confirmed by
xrefs) - and is in any case out of range for a room reporting `ncam=7` (cameras 0-6).

### Sixteenth run: door zone fires. The dining-room arrival point is data-correct.

`MovePlayerXZ` fixed the reach probe and the door now auto-triggers at the end of the
intro, with no walking. The remaining report was that Chris arrives too far from the
dining-room door.

**It is not a bug in the placement.** Established, in order:

1. **The record parse is verified.** The byte immediately before the record at
   `ROOM1060.RDT:0x2fbbc` is `0x0C` (`cmd_door_set`) with `doorNum=2` - matching event
   table entry 2, the one that fires. So the record base and every offset read from it
   are confirmed, not inferred.
2. **`Room_SetupCamera` is faithful.** The original reads
   `RDT+0xA0 + id*0x2C + 0x1C`, which is camera-array `+0x28` == `fov`, and hands
   `+0x08` == `cam_from_x` to `MatrixToCamera`. Both match the port.
3. **Camera 0 is the correct arrival camera.** The dining room has 7 cameras; camera
   0's exit zones lead to cameras 1 and 2 at *lower* X (20671..24285 and 3899..9800),
   and its own look-at is `30672,8550` - essentially the arrival point. Nothing in the
   room's switch table contains x=30900, so `check_camera_switch` correctly declines.
4. **Nothing in the original moves the player after placement.** The two remaining
   stubs at the tail of `room_transition_load` are `FUN_00442180` (**empty in the
   original** - a single `RET`, like `0x00442170`) and `FUN_0041d070`. Both are
   now resolved and implemented, 2026-08-16. `FUN_0041d070` is **not** the
   event-VM task, as this note originally claimed: it is
   `ExecAsync(&LAB_0041d050)`, and `LAB_0041d050` checks the sound manager's
   initialised flag at `g_SoundManager+0x10` and calls
   `DirectSound::compact` (`0x0041e680`) - `SetCooperativeLevel(EXCLUSIVE)`,
   `IDirectSound::Compact()`, `SetCooperativeLevel(PRIORITY)`. It is the
   per-room-load sound heap defragmenter and is inert in this port, whose audio
   backend is XAudio2. Ported as `SndCompactAsync` in `MarniSound.cpp`.

**The design, read from both sides of the same doorway.** The dining room's own door
back to the hall is `ROOM1050.RDT` `doorNum=1`, `dest=6`, zone x 31600..33600, entry
point `3400,0,17000`:

| Direction | Arrival point | That door's own zone | Gap |
|---|---|---|---|
| hall to dining | x=**30900** | x 31600..33600 | 700 **outside** |
| dining to hall | x=**3400** | x 1000..2950 | 450 **outside** |

Entry points sit deliberately **just outside** the matching door's action zone. They
have to: `door_try_enter` fires on zone *contact* with no button press, so an arrival
inside the zone would re-trigger on the next frame and bounce the player straight
back. The ~2700 units from the arrival point to the far edge of the zone is the
visible gap to the wall.

That symmetry is the strongest evidence available that `30900,0,8300` is intended, and
it also retro-explains the intro: the cutscene ends at `2487,17568`, which *is* inside
the hall-side zone - which is exactly why the transition fires with no input there,
and why that is a scripted special case rather than the normal arrival behaviour.

**What is left, if the arrival still looks wrong:** the world state is provably right,
so the only remaining variable is projection - how a correct world position lands on a
pre-rendered background. The decisive experiment is a Cheat Engine read of the
*original* immediately after the same transition:

```
0x00be6318   player localMatrix.t[0]   (expect 30900)
0x00be631c   player localMatrix.t[1]   (expect 0)
0x00be6320   player localMatrix.t[2]   (expect 8300)
0x00be6358   directionAngle            (expect 2048)
```

If the original reads the same four values, the placement is correct and the
difference is in the renderer, not the game logic. If it reads something else, some
writer we have not found adjusts the position and that trace names the frame it
happens on.

### Seventeenth run: room actions by the action/confirm key

All 18 `room_check_actions` handlers are now transcribed and wired, the action-key
entry points are implemented, and the per-frame interaction state machines replaced
the four empty stubs that used to sit in EngineStubs.cpp. Build passes.

**What landed (each with its original address):**

- Action-key probes: `check_action_object` (`0x0041c150`) — the event-table scan that
  only fires entries with flag bit `0x80` set (the ones `update_player_position`
  deliberately skips, i.e. objects/items rather than doors); `check_climb_object`
  (`0x00474930`) + `ChkPlReachEntity` (`0x00474a20`) — the crate/climb-object scan of
  `g_itemboxes_covers_table` with the ±299/4096 angle window; `door_transition_update`
  (`0x00495d70`).
- Handlers `room_check_actions[2..0x11]`: `display_msg_room_action`, `include_key`,
  `set_key_flag`, `check_door_side` (the Z-axis sibling of `check_door`), `flag_bank_set`
  (10-way flag-bank bit set/clear), `open_itembox`, `create_room_event`,
  `room_action_noop10`, `room_action_effect`, `set_stairs_zone`, `set_room_event_flag`,
  `check_desk`, `pickup_key_event`, `check_typewriter`, `stairs_height_update`.
- Interaction animations under animFrameId 1/2: `player_behavior_0c_interact`
  (`0x00495e00`), `player_behavior_0b_ladder` (`0x00496480`, 8-state climb with the
  byte-indexed step-sound table at `0x004d45cc`), `player_behavior_10_push`
  (`0x00457230`), plus the empty `0x09`/`0x0e`/`0x0f` slots.
- Per-frame state machines: `check_desk_state` (`0x0041bc90`), `check_itembox_state`
  (`0x0041c240`, lid animation), `check_typewriter_state` (`0x0041c330`, save flow),
  `check_event_item_usage` (`0x0041c490`), `room_event_item_pickup` (`0x00451700`,
  the actual inventory add — stacking, 0xfa cap, slot-index bookkeeping),
  `memset_` (`0x0047cf60`).

**Fixes to pre-existing port code:**

- `use_room_action_item` (`0x004631f0`, SaveLoadScreen.cpp) read `g_firstItemSlotPointer`
  — a `.gwipe` overlay global that is never assigned — and bailed on NULL, so key
  consumption was a silent no-op. Now uses `g_ItemSlotsPointer` like the original.
- `check_desk`'s desks-table writes go *through* the pointer
  (`MOV EAX,[ECX*4+0xd21360]; OR byte ptr [EAX],1`), not at the table base — the
  byte-flag lives on the desk omodel, byte 0. Same for `pickup_key_event` /
  `check_desk_state` case 4.

**Untouched / known limits:**

- `check_and_display_interactive_screen` (`0x0042a030`, lab slides / passcode panels)
  is still a stub — a separate subsystem gated by PlayerFlags bit 0x20, not the
  action-key flow.
- `set_screen_effect_struct` (`0x004567d0`) writes the effect struct at `0x00be63c8`,
  whose consumer (`0x00456a10`) is not ported — inert by design until it is.
- The `[zone]` / `[door]` diagnostics in `update_player_position` and `door_try_enter`
  still print; they were left in place for this test round.

### Eighteenth run: crash fix + pickup/itembox softlock fixes

**The door crash** — `cmd_em_set` passed `(char)g_ScdOpcodes[3]` sign-extended to
`Flg_ck`. The original's assembly is `MOV CL, AL` (zero-extend); the Ghidra
decompiler rendered it as `*(char*)` and the port transcribed the signed cast.
Rooms whose enemy entries use event-flag ids 0x80-0xFE computed a ~33MB byte
offset in Flg_ck and faulted. Fixed with `(unsigned char)`. This is the reverse
of the "Ghidra's U suffix" trap: MOVZX rendered as signed char.

**The pickup softlock** — `menu_update_status_screen` (0x00482910) was an empty
stub returning 0, so main_menu state 8 (the "you got the item" screen, entered
when msf bit 0x100 is set after a key/desk pickup) spun forever. Implemented
the whole chain: pickup_fade_update (0x00482800, the overlay rect ramp),
pickup_mark_seen (0x00482be0), pickup_list_advance/init/input (0x00482250/90/b0),
pickup_screen_render (0x004823a0, the slide-in list with the arror.tim /
filem_lX.pix / file000.tim loads), pickup_load_texture/unpack_list
(0x00482710/c0), pickup_screen_init_table (0x00482c50), pickup_item_seen
(0x00488680), plus the DAT_004d2a08 key-item list, the DAT_004d2620 counts and
the four static TextureDescs at 0x004d2888/2988/29ac/29d0 (byte-transcribed).

**The itembox softlock** — `menu_itembox_interaction` (0x004941f0) was an empty
stub returning 0, so main_menu state 3 never exited. Implemented the full
storage screen: player-row / 48-slot grid cursors, confirm swaps via
g_itemboxSlots, L1/R1 paging with the SUBMENU_STATE_ID slide, plus
itembox_refresh_item (0x00420b80), itembox_draw_cursor (0x00420a70) and
itembox_draw_slot_icon (0x00443040, x=(slot&1)*20, y=((slot&~1)<<4)+0x50 per
the disassembly).

**Still open** — the "menu opens in map mode" report. The map display state
machine (mode 5, msf 0x200) was traced against the original and matches; no
hang found in states 0-8 (the zoom state 6 waits for MAP_HL_STATE==1 which
nothing sets - in the ORIGINAL too - and the menu state 3 exits on zoom==6
regardless, so it is a cosmetic wait, not a hang). If the report persists, the
next step is the [MENU]/[MAP] ODS output to see which state actually stalls.

### Nineteenth run: map-mode root cause + map display fix

**The "pickup opens the menu in map mode" trigger — a port transcription bug,
not the original design (corrected 2026-08-04).** The trigger is real: the
dining room's event 9 runs `room_action slot=6 act=0x4`, i.e.
`set_key_flag(&entry[6])`, and with entry+2 == 0 `set_key_flag` raises an msf
bit that main_menu consumes as a menu mode. But the bit is **0x800** — the
original disassembly is `OR dword ptr [0x00be41c0], 0x800` at 0x0041b6b0, i.e.
menu mode 3/4, the ITEM VIEWER (3D model + description). The port had written
`0x200` (menu mode 5, the map display) — that is why a pickup opened the menu
on the map tab. Fixed in `set_key_flag` (PlayerAnimations.cpp). A byte-level
scan of every `OR [0x00be41c0], imm` in the original shows 0x200 is never set
anywhere; 0x800 is raised by set_key_flag, check_desk_state case 5 and
player_behavior_0c_interact (all three already correct in the port except the
first). The "map display is the record-less fallback" conclusion above was
built on the same wrong 0x200 value and is invalid. Decoded with
a proper SCD walker built from the port's opcode lengths; the hall items
(slots 11-15) use act=0x02 display_msg and set no msf bits at all.

**The map display corruption — found from the user's ODS log.** The log's
`[MAP] zoom0 slot0xd r=0 | zoom1 slot0xf r=1` showed zoom0's AddSprite_Ex
rejected while zoom1/2 passed. AddSprite_Ex (Rendering.cpp:613) rejects a
sprite when `printClutTint - pageClutBase` is out of range: page 0x1C (zoom0's
page, clut base 0) vs the desc's printClutTint 0x1ff -> clutIdx 511 -> return 0
-> the map sprite never drew, leaving a broken-looking display the menu could
not be read from. zoom1/2 matched page 0x1E (clut 0x1ff). Fixed g_MapZoomDesc[0]
printClutTint to 0 to match its page; the zoom-in/park/cancel/zoom-out/close
state machine (verified against the original, including the "highlight wait"
that is a cosmetic no-op in the original too) then completes and the menu
closes with menu_restore_game_state clearing msf 0x100-0x8000.

### Twentieth run: event-VM state-2 audit, the last empty SCD leaf, and naming

Re-audited the two halves of the SCD system against the original. The **command**
side (81 opcodes) and the **event-VM** side (control flow 0xF6-0xFF, state 0, state 1,
state 3) are all faithful — `room_events_check` (`0x0041d6a0`) and
`scd_event_state1_anim` (`0x0041da30`) were re-decompiled instruction-for-instruction
and the port matches, including state-1's exact opcode set (0x00, 0x80-0x8B) and
state-0's `default: deactivate`. No missing opcodes anywhere.

**State 2 (`scd_event_state2_movement`, `0x0041e1a0`) had two real defects.**

*Case 0x08 wrote the wrong field, at the wrong width.* The original is
`MOV DL,[ECX+2] / MOVZX EBX,[ECX+1] / MOV byte ptr [EBX+ECX*1+0x84],DL` — a single
BYTE store at **entity + 0x84 + script[1]**, i.e. into the state block
(`state` / `ignore_player_flag` / `action_behavior` / `action_state` / `health` /
`hit_state`). The port used base **0x34** (`localMatrix.t`) and a 32-bit store, so
this opcode overwrote four bytes of the entity transform instead of one state byte.

*Case 0x0B was merged into case 0x0A and desynced the script stream.* In the original
these are separate cases: 0x0A runs a 6-way field selector and advances 4; 0x0B
(`0x0041e486`) is the rotation sibling of case 0x07 — three WORD stores at entity
+0x72 / +0x74 / +0x76 from the script words at +2 / +4 / +6, then advance **8**. The
port ran 0x0A's parametric store *first*, advanced 4, then read the angles from
`scriptPtr + 4` (already-advanced, so effectively +8) and advanced 8 more. Every use
of opcode 0x0B therefore performed a spurious write and left the instruction pointer
**4 bytes past** where it should be — from there the VM decodes garbage.

Also recorded while transcribing 0x0A: for a selector **above 5** the original does
`MOV EDX,dword ptr [ESP+0x4]` and stores through it, but that stack slot is never
written on this path (cases 0-2 set EDX directly and jump past it). The original has a
latent **wild pointer write** here. Deliberately not reproduced — the port skips the
store and advances like every other path.

**`scd_model_tint_apply` (`0x00473b10`, was `FUN_00473b10`) was an empty stub.** It is
SCD opcode 0x34 **variant 0** — the only variant of that opcode that actually tints
anything. Variants 1 and 2 (`FUN_00473d10` / `FUN_00473d60`) were already implemented
but they only ever rewrite the `g_textureQueueData` entry. Variant 0 accumulates the
three signed deltas onto queue bytes +3/+4/+5 (clamping each into ±31) **and** pushes
the result into the live model. Three leaves had to be written for it, none of which
existed in the port:

| New | Original | Role |
|---|---|---|
| `TmdObjectTintAdd` | `0x00485c60` | accumulate an RGB delta, rebased so `max(R,G)` becomes 0 |
| `TmdObjectTintSet` | `0x00485fa0` | set the multipliers absolutely at 5/31 scale |
| `TmdObjectSetLightScale` | `0x004870a0` | store one negated 1/32 value at modelObj+0x14 |

All three walk the same per-object array `JointSetColorTint` walks (TMD at
modelObj+0x20, count at +0x4C0, objects from +0x4D0 stride 0x84, bound `count * 2`).
As with the already-ported `JointSetColorTint`, only the `modelObj+0x10 == 0` branch is
transcribed; the original's else-branch drives the complex-TMD staging buffer
`g_abComplexTmdObjectData` (`0x008ffd1c`), which this port does not model at all.

Two original quirks kept deliberately: the enemy scan clamps to **30**, not 32
(`if (0x1d < g_enemy_count) count = 0x1e`), and the two object branches mask the
table index differently — `& 0x7f` on the luminance path, `& 0x3f` on the tint path.

**Fixed on the way: `JointSetColorTint` scaled by 1/255 instead of 1/128.** The
original multiplies by the float `0.0078125` at all three channel stores. `1/255` is
not a constant this function uses at all — `0x004af2f8` (255.0f) belongs to
`0x00485c60`'s pack-back step. Every SCD-driven joint tint was coming out at roughly
half its intended intensity, and a tint byte of 0x80 — which the original saturates to
1.0 — landed at 0.5.

**Naming: every numeric `cmd_0xNN` is gone (29 functions).** The old names were also
**off by one against their own table slot** — `cmd_0x3f` sat at slot 0x3E, `cmd_0x42`
at 0x41, and so on through `cmd_0x51` at 0x50 — so the name actively misled about which
opcode it served. All 81 dispatch-table addresses were re-read straight out of
`0x004c1110` to confirm each slot before renaming.

| Slot | Was | Now |
|---|---|---|
| 0x13 | `cmd_0x13` | `cmd_item_event_set` |
| 0x14 | `cmd_0x14` | `cmd_scd_event_create` |
| 0x1C | `cmd_0x1c` | `cmd_room_light_fade_set` |
| 0x22 | `cmd_item_cmd_0x22` | `cmd_item_count_test` |
| 0x2F | `cmd_0x2f` | `cmd_snd_pan_vol_set` |
| 0x31 | `cmd_0x31` | `cmd_fade_state_set` |
| 0x34 | `cmd_0x34` | `cmd_model_tint_set` |
| 0x35 | `cmd_0x35` | `cmd_obj_flag_set` |
| 0x36 | `cmd_0x36` | `cmd_obj_field_test` |
| 0x37 | `cmd_0x37` | `cmd_room_bgm_state_set` |
| 0x38 | `cmd_0x38` | `cmd_dpad_test` |
| 0x39 | `cmd_0x39` | `cmd_enemy_flags_get` |
| 0x3B | `cmd_0x3b` | `cmd_obj_rotation_set` |
| 0x3C | `cmd_0x3c` | `cmd_player_dist_test` |
| 0x3E | `cmd_0x3f` | `cmd_bullet_effect_clear` |
| 0x41 | `cmd_0x42` | `cmd_entity_unk8e_set` |
| 0x42 | `cmd_0x43` | `cmd_effect_clear_typed` |
| 0x43 | `cmd_0x44` | `cmd_bgm_volume_ramp` |
| 0x44 | `cmd_0x45` | `cmd_scd_event_kill` |
| 0x45 | `cmd_0x46` | `cmd_entity_unk8e_add` |
| 0x47 | `cmd_0x48` | `cmd_obj_transform_set` |
| 0x48 | `cmd_0x49` | `cmd_effect_pool_clear` |
| 0x49 | `cmd_0x4a` | `cmd_room_bitmask_set` |
| 0x4B | `cmd_0x4c` | `cmd_bgm_stop_all` |
| 0x4C | `cmd_0x4d` | `cmd_item_record_transfer` |
| 0x4D | `cmd_0x4e` | `cmd_player_joint_tint` |
| 0x4E | `cmd_0x4f` | `cmd_effect_flags_modify` |
| 0x4F | `cmd_0x50` | `cmd_script_flag_set` |
| 0x50 | `cmd_0x51` | `cmd_script_flag_test` |

All mirrored into the Ghidra project and saved, with one exception:
**Ghidra has `0x004323a0` (slot 0x4D) defined as DATA, not code**, so no function
object exists there to rename. The bytes are unambiguously code —
`PUSH 0x606060 / MOV ESI,[0x00be637c] / PUSH 0x080820 / PUSH 0x30 / PUSH ESI /
CALL 0x0048a190`, then `LEA EAX,[ESI+0x7C]` and `LEA EAX,[ESI+0xF8]` — which also
independently confirms the port's `cmd_player_joint_tint` (base is the
`jointsStructs` pointer at `0x00be637c`, stride 0x7C). Worth clearing that data
definition in Ghidra at some point; other functions in the same region may be missing
from the DB for the same reason.

**Not run in-game.** Build-verified only (per-file `cl /Zs`). The state-2 fixes need a
room whose scripts actually use opcodes 0x08 and 0x0B to be exercised; the tint work
needs a room that runs opcode 0x34.

### Twenty-first run: the Barry-shoots-the-zombie freeze, and an FX pool leak

Two reports from a ROOM1051 (Jill dining room) test: no SCD effects render anywhere,
and the scene where Barry shoots the zombie freezes.

**New tool: `tools/evt_disasm.py`.** The project could decode the SCD *command* stream
(`mine_room_scd.py`) and `.dor` scripts (`dor_disasm.py`) but not the **event VM** - the
outer cutscene state machine. It now disassembles the RDT+0x68 script table, tracking
`state` across transitions so the same byte decodes correctly in states 0/1/2/3, and
decoding the SCD payloads of opcodes 0x06/0x07 inline.

One trap worth recording: **event opcode 0x06 (`run_scd`) payloads begin with a u16
BLOCK SIZE, not an opcode.** `run_command_functions` is handed `scriptPtr + 2` and reads
the length from there before stepping to the first command at +2. Decoding `payload[0]`
as the opcode makes `0C 00 ...` read as a bogus `door_set`. Opcode 0x07 (`exec_scd`) is
different - its command really does start at +2.

#### The freeze: `npc_scd_08` (0x0047b280) was a stub - implemented

The chain is now proven end to end, not inferred:

1. ROOM1051 script 15 runs `85 08 11 21` (state-1 opcode 0x85) on enemy 0 (Barry):
   `state = 8`, `action_behavior = 8`, `animationId = 0x11`, `scd_anim_param = 0x21`.
2. It then spins on `FC 06 04 04 21 01 / FE / FD` - the VM's wait idiom: run the SCD
   command on the call stack each frame and loop while it returns non-zero. The command
   is `cmd_bit_test` with bank `0x04` = **`g_SysFlags`**, and operand `0x0121` decodes to
   byte offset 4, bit 1, condition 1 - i.e. **"block until g_SysFlags bit 0x21 is set."**
3. `state = 8` dispatches `npc_state8_action_update` -> `g_npcScdBehaviors[8]` ->
   `npc_scd_08`, whose `action_state == 2` branch is
   `Flg_on(g_SysFlags, ENTITY->scd_anim_param)` - and `scd_anim_param` is the **0x21**
   from step 1. That is the exact bit step 2 polls.
4. `npc_scd_08` was `npc_scd_report("0x0047b280")`, a no-op. The bit was never set, so
   the wait never released. **Freeze.** The same function is what spawns the muzzle
   flash, so "Barry never shoots" and "no flash" were one missing function.

The already-implemented sibling `npc_scd_06` uses the identical
`Flg_on(g_SysFlags, scd_anim_param)` release signal, which independently confirms the
idiom.

Now implemented, with the three 10-byte weapon-FX tables byte-transcribed from
`0x004c0dd8` / `0x004c0e68` / `0x004c0ef8` (14 records each, indexed by
`behavior_flags - 2`): muzzle flash and secondary flash in the weapon joint's space
(`jointsStructs + 0x70C` = joint 14's `world` matrix), ejected shell/smoke in the
entity's own matrix. Frame value `0x63` is the original's "disabled" marker. States 4/5
are the flamethrower path (a type-0x0C billboard every 6th frame, a looping 0x1E/0x1F
sound pair, and a per-frame yaw sweep).

Two guards added where the original is unsafe, both documented in place: the table index
is a raw byte with no bound in the original, and `Effect_CreateBillboard`'s 0xFF
"pool full" return is stored into a **signed** char and used as an index, so a full pool
writes `g_effectPool[-1]`. Our `.bss` neighbours differ from the original's, so that
stray write would corrupt something different here.

Useful property of the result: if `behavior_flags` is ever wrong, the row lookup reports
and returns NULL, the animation still advances, `action_state` still reaches 2, and the
scene **unfreezes anyway** - only the effects are missing. A data bug can no longer hang
the game here.

**Still stubs** (other cutscenes will need them): `npc_scd_03` (0x0047a9c0),
`npc_scd_04` (0x0047ad30), `npc_scd_05` (0x0047aef0), `npc_scd_07` (0x0047b1c0),
`npc_scd_09` (0x0047b6b0), `npc_scd_10` (0x0047b760).

#### The FX: a pool-counter leak that disables effects permanently

ROOM1051 declares no `effect_spawn` at all (its only SCD FX opcode is `cmd_enemy_0x28`),
so "no FX in this room" was partly expected - the flash there comes from `npc_scd_08`
above. ROOM1000 is the room that does use opcode 0x2A, five times in one cutscene.

`Effect_CreateBillboard`'s "sprite not loaded for this room" guard returned **after** the
slot-search loop had already done `g_freeEffectSlots--`, and without ever setting
`eff->animId`. So the slot stayed free in the pool while the counter said it was taken -
one leaked slot per skipped billboard. After 64 skipped spawns `g_freeEffectSlots`
reaches 0 and the `if (g_freeEffectSlots == 0) return type;` at the top of the function
**rejects every effect for the rest of the session**, including ones whose sprites are
perfectly valid. One missing sprite turned into "no FX anywhere, permanently". Fixed by
handing the slot back; the diagnostic now prints the running free count.

Worth noting for the sprite-table question itself: room effect sprites are declared by
the RDT (`ROOM1000` declares exactly one, index 0x26; `ROOM1051` declares 0x03 and 0x04)
and land in `g_effectSpriteInfo[declared_index]`, while `g_abEffSpriteIndexTable` slots
[8..15] track them. The **global** weapon FX come from `CORE00.ESP`, whose index table is
`05 09 0C 11 00 0E 08 0B` - so ROOM1000's `type 9` spawn is a CORE00 sprite, not a room
one. A room that ever declares an index colliding with that set would have its
`InitRoomEffSprite` teardown invalidate a global weapon-FX slot; none of the rooms
checked so far do, but it is a real hazard.

**Not run in-game.** Build-verified only.

### Twenty-second run: it was behaviour 7, not 8 - and a tooling trap that hid it

The dining-room scene still froze after the previous run. The log named it outright:

```
[npc] unimplemented SCD behavior 0x0047b1c0 (id=34 anim=16 frame=0 action=0)
[scd] slot 0 PARKED op=0xFD state=0 depth=0 ...
```

id 34 is Barry, and `0x0047b1c0` is **`npc_scd_07`**. The previous run implemented
`npc_scd_08`. The wait-mechanism analysis was right, the link in the chain was wrong:
the sequence is `85 07 10 21` (behaviour 7 - raise/aim) **then** `85 08 11 21`
(behaviour 8 - fire), so behaviour 7 blocks before 8 is ever reached. Both share
`scd_anim_param = 0x21`, which is why the same `bit_test(g_SysFlags, 0x21)` wait serves
the whole sequence.

**Why the wrong one was picked: `evt_disasm.py` mislabelled opcode 0x85's operands.**
0x85 takes the behaviour from +1 and the animation from +2, but the tool printed
`(anim <byte at +1>)` - so `85 07 10 21` read as "anim 0x7" instead of
"behaviour 7, anim 0x10". Opcode 0x84 is the one where +1 really is the animation
(it forces behaviour 1). Both now print `behavior= anim= scd_param=` explicitly.

`npc_scd_07` (0x0047b1c0) is now implemented: play the animation to completion
(action_state 0 -> 1 -> 2 via Joint_move's loop return being ADDED to action_state),
then `Flg_on(g_SysFlags, scd_anim_param)`. A per-frame yaw step from `scd_timer` runs on
every path including the terminal one.

**`npc_scd_09` (0x0047b6b0) implemented too.** A sweep of every RDT with the fixed
disassembler shows opcode 0x85 selects only three behaviours across the whole game:

| behaviour | uses | status |
|---|---|---|
| 8 | 20 | implemented (previous run) |
| 7 | 15 | implemented (this run) |
| 9 |  4 | implemented (this run) |

So the set the scripts can actually reach is complete. `npc_scd_03/04/05/10` remain
stubs and are unreachable - nothing selects them. (Behaviour 1 is reached separately via
opcode 0x84 and was already implemented.) Behaviour 9 is behaviour 7 with `Joint_move`
reversed, blend counter 3, no yaw step, and a terminal `MOV word ptr [..+0x86],0` that
clears action_behavior and action_state together.

#### Tooling trap: the PE section map, and a misleading comment that caused a wrong turn

While chasing this I disassembled the exe locally with `file_offset = VA - 0x400000` and
got coherent-looking garbage that "proved" the behaviour table pointed into the middle of
functions. It does not. **`.text` is VA `0x401000` at raw `0x600`, so text addresses map
as `VA - 0x400A00`** - the naive mapping lands 0xA00 bytes off, which is small enough to
resync into plausible instructions rather than fail loudly. It briefly looked like the
dispatch table was corrupt or that the local exe differed from the Ghidra one; neither
was true, and a byte-for-byte compare of `0x0047b280` between Ghidra's `read_memory` and
the correctly-mapped file confirmed they are the same binary.

`tools/scd_widths.py` was never actually broken - its `va_to_off()` walks the section
table properly - but its **docstring claimed the naive mapping**, which is what sent me
down this path. Comment corrected in place to say the opposite, loudly.

Rule worth keeping: when a dispatch table appears to point into the middle of functions,
suspect the address mapping before suspecting the data.

**Not run in-game.** Build-verified only (full tree, zero errors).

### Twenty-third run: dining room confirmed working; ROOM1000 effect cull narrowed

The ROOM1051 dining-room scene now plays correctly - `npc_scd_07` was the fix. What
remains is ROOM1000's opcode-0x2A effects, which spawn but never draw.

**What the log establishes.** Effects reach the pool and animate: the active count ramps
1 -> 5 and drains back, matching the script's five `effect_spawn` calls, and the pool
counter is healthy (`free=63`, no leak, no `not loaded` lines). One effect renders
successfully:

```
[effect] submit stage=5 type=11 slot=10 srvNull=0 scr=(-37,123) depth=818 scale=4148
```

`stage = 4 + submitted`, so **stage 5 means SubmitEffectSprite accepted it** with a live
SRV - the render tail works end to end. But `type=11` (decimal) is effect type **0x0B**,
which comes from `cmd_item_model_set`, *not* the type **9** the room's `cmd_effect_spawn`
spawns. `slot=10` is texSlot = 3 + sheetSlot, and CORE00.ESP's index order
(`05 09 0C 11 00 0E 08 0B`) puts 0x0B at sheet slot 7, which confirms the identification.

So: the type-9 effects live, animate and expire without ever reaching
`effect_submit_sprite` - and none of that function's own cull stages (1/2/3) fire either.

**Ruled out statically, so the next run does not have to re-check them:**

- *Both animation-header gates.* Parsing CORE00.ESP's effect blocks directly
  (`tools/` scratch script; the layout is index table at the file head, data offsets
  hanging off the file end read backward) gives type 9 / depth 7:
  `animId=0x02 updateId=0x00 hdr10=0x03 hdr11=0x70`. `hdr10 & 2` is SET (so the
  transform/projection branch runs and `local_10` is a real world position, not the
  zeroed scratch), and `hdr11 & 0x80` is CLEAR (no early return).
- *The switch-zone cull.* The effect's world XZ is (4420, 3800), and
  `is_entity_in_switch_zone` tests it against the FIRST zone record whose `camFrom`
  matches the current camera. Replaying that exact test - including the original's
  zero-extension of every zone coordinate - against ROOM1000's 14 zone records shows the
  point is inside the first zone of **every one of the six cameras**. It cannot be culled
  here regardless of which camera is live.
- *Sprite availability.* Type 9 is a CORE00.ESP global sprite (not a room one), the
  room's own declaration is index 0x26, and there is no collision between the two.

**A diagnostic honesty problem found and fixed.** `effect_submit_diag` keyed its
dedupe on a single last-value tuple, so two effect types alternating would suppress each
other - "only one submit line" could have meant "only one type reaches here" *or* "the
key kept getting overwritten". It is now keyed **per effect type**, so the absence of a
type-9 line is real evidence rather than an artifact. Worth generalising: a
print-on-change diagnostic with a single shared key cannot distinguish "never happened"
from "kept being overwritten", and that ambiguity wastes a whole test round.

**Added for the next run:** a gate-level report in `EffectActor_UpdateAndRender` that
fires for every slot reaching the end of the update, keyed per effect type, printing the
slot, type, animId/updateId, both header bytes, the world position fed to the zone test,
the projected screen coordinates, projDepth, the live zone pointer, and which of the two
gates rejected it (or `DREW`). Between that and the per-type submit diag, the next log
says definitively whether type 9 reaches the gates at all - and if it does, whether the
projection is the problem.

**One value worth watching in that output.** The single effect that does draw reports
`scr=(-37,123)`, which is *after* the `-0xA0/-0x78` centring, i.e. raw screen
(123, 243) on a 320x240 frame - three pixels below the bottom edge. If the type-9
effects report similar Y values, the fault is the projection rather than the culling:
their world Y is -2500 (2500 units **up**), which should project well above centre, not
below the bottom.

**Not run in-game.** Build-verified only.

### Twenty-fourth run: ROOM1000 effects - a VECTOR built from three separate locals

The gate diagnostic answered it in one line:

```
[effect] slot 62 type=9  hdr10=03 hdr11=70 world=(4420,-13108,7)   -> outside switch zone
[effect] slot 63 type=11 hdr10=03 hdr11=40 world=(5160,-930,8690)  -> DREW
```

Type 9's world position should be **(4420, -2500, 3800)**. X is correct and Y/Z are
garbage - and **-13108 is `(short)0xCCCC`**, the /RTC uninitialised-stack fill.

**Root cause: `cmd_effect_spawn` passed the address of three separate `int` locals as a
`VECTOR*`.** `Effect_CreateBillboard` casts its `pos` argument to `VECTOR*` and reads
x/y/z/pad at +0/+4/+8/+12. The original (0x004316c0) uses `local_10` / `local_c` /
`local_8`, which are **adjacent** stack dwords, so `&local_10` really is a VECTOR. Three
separate `int` locals in C are under no such obligation, and with /RTC MSVC inserts guard
bytes between them - so only `x` landed and `y`/`z` read the fill pattern. The corrupt
position then failed the camera-zone test, which is why the effect was culled before
drawing. That also retro-explains the previous run's static analysis: testing the
*correct* position against the zone quads said "passes for every camera", and it does -
the runtime was never using that position.

The effects that always worked are exactly the ones passing the global
`g_playerPosScratch` rather than a local, which is why type 0x0B rendered and type 9
never did.

Fixed in `cmd_effect_spawn` (0x2A) and `cmd_bullet_0x3d` (0x3D), both of which had it.
An audit of every `Effect_CreateBillboard` call site in the tree confirms all others pass
a real `VECTOR` local or the global scratch.

This is the third instance of the same trap in this port - the packed screen-coordinate
pair in `EffectActor_UpdateAndRender` and `ApplyMatrix`'s VECTOR-vs-SVECTOR destination
were the first two. **Whenever the original passes `&local_N` into something that reads a
struct, the locals were adjacent by the compiler's layout; a C transcription must declare
the actual struct.** Separate scalars are a silent corruption, not a compile error.

**Second defect, found by the same diagnostic: the type-0x0B effect was flickering.**

```
type=11 hdr10=03 world=(5160,-930,8690) -> DREW
type=11 hdr10=00 world=(0,0,0)          -> outside switch zone
```

alternating frame to frame. `local_10` is written *only* inside the
`animHeader[10] & 2` branch; the original leaves it uninitialised otherwise and in
practice reads its own previous value, so it keeps drawing through phases with the
transform bit clear. The port zeroed it, so those frames were culled outright and any
effect whose phases alternate drew every other frame. Now seeded from
`eff->posX/Y/Z` - exactly what the transform branch last stored, so it is the
deterministic stand-in for the original's stale stack slot.

**Diagnostics.** The gate report in `EffectActor_UpdateAndRender` and the per-type
`effect_submit_diag` are both still in place; they cost one line per changed outcome per
effect type. Worth keeping until a couple more rooms are confirmed, then removing.

**Not run in-game.** Build-verified only (full tree, zero errors).

### Twenty-fifth run: 2D effects confirmed; the idle-behaviour layer

All cutscene 2D effects render correctly - the VECTOR-from-separate-locals fix was it.
The remaining log noise was `[npc] unimplemented idle behavior 0 (0x0046b580)` from an
idling Rebecca (id 35), and the four idle stubs behind it are now implemented.

**Idle behaviour 0 is not a behaviour - it is a second-level jump table on entity id.**

```
0046b580: MOV EAX,[0x00bebcd4]        ; ENTITY
          XOR ECX,ECX
          MOV CL,byte ptr [EAX+0x1]   ; ENTITY->id
          JMP dword ptr [ECX*0x4 + 0x004c2c48]
```

0x004c2c48 is the shared dispatch block's base + 20*4, so it re-enters the SAME
overlapping array at `array[20 + id]` - a fourth view of the block already documented at
the top of CharacterNpc.cpp (state / per-character init / idle, now also id-from-idle-0).
For the ids scripts actually spawn (32-41) it lands in the idle tail: Chris, Jill, Barry,
Rebecca and Wesker (32-36) plus 39/40 resolve to the **no-op**, while 37, 38 and 41
resolve to the play-animation handler. So the reported behaviour for an idling Rebecca
genuinely is "do nothing" - the message was noise, not a missing feature, which is why
nothing ever looked wrong on screen.

Ids 28-31 map back onto idle 0-3, so id 28 would re-enter idle 0 forever. No script uses
those ids; guarded rather than reproduced, because unbounded recursion takes the process
down instead of hanging one actor.

**The idle table was also three entries short.** The original's idle view runs to index
**18** (array slots 48-66 are all real handlers; 67 onward are NULL). The port declared
16. A sweep of every RDT shows scripts set `action_behavior` via `cmd_enemy_0x28`
sub-command 2 to values 0x0A, 0x0D (30 times), 0x0E, 0x0F, 0x14, 0x15 and 0x17 - so
0x14/0x15/0x17 were landing outside the declared array. They are NULL in the original
too, so `npc_state1_idle` now reports them instead of jumping to address 0, and the
array covers 0-18 to match. Entries 4-15 were verified against the original byte for
byte and all already matched.

**The three real handlers, transcribed:**

| Idle | Address | What it does |
|---|---|---|
| 1 | `0x0046b620` | Walk forward (anim 0x35, speed 1000 decaying 15/frame) until `check_room_collision` hits, then voice 0xA9 and a knock animation 0x36 with sound 0x1C |
| 2 | `0x0046b800` | Scripted death: anim 0x33, death timer 0xB4, blood spray for 10 frames, three joints tinted red on frame 3, wet sound on frame 0x2A, then a ground billboard that grows over 30 frames |
| 3 | `0x0046bb20` | The other scripted death: anim 0x30, 250-frame timer, `hit_state` 0x80, copies enemy 1's facing, five joints tinted on frame 8, blood before frame 9 and after 0x5F, health forced to -1 |

Two details worth recording:

- Idle 1's collision probe saves and restores **four** dwords from entity+0x34 - the three
  `localMatrix.t` components *plus* the first dword of `worldMatrix` at +0x40.
  `check_room_collision` writes through the position it is handed, and the original rolls
  all four back, so the probe is a test and not a move. Restoring only three would leave
  the world matrix corrupted.
- Idle 2's second `Effect_CreateBillboard` call passes the dead-move matrix as the sprite
  space and `jointsStructs + 0xD4` as the **position** - the two arguments are swapped
  relative to the call immediately above it. That is the original's, and it is reproduced;
  it reads joints+0xD4 as a VECTOR.

**Still stubbed:** NPC state 9 (`0x00471950`, the pathfind layer) and the four unreachable
SCD behaviours 3/4/5/10. Nothing currently reaches either.

**Updated (2026-08-11):** NPC state 9 is now transcribed in `CharacterNpc.cpp` - the
follow-the-player mode `cmd_em_set` sub-command 8 selects (Barry after the dining-room
scene). The driver, the four walk behaviours (0x00471a40/0x00471b80/0x00471c40/0x00471d50),
the heading/waypoint helpers (0x00471f20, 0x004720d0, 0x00460090, 0x00460180, 0x00460390)
and the look-at wander (0x00472330) are all in; `FUN_004602b0` now returns the
shared-edge flag its original leaves in AL (0 = X edge, 1 = Z edge). The 0x004c35f8
dispatch block and the 0x004c3608/0x004c3618 threshold/swap tables are extracted. Note
the swap-byte ring from 0x004c3618 is `{3,0,1,3}`/`{1,2,2,0}` - read the dump at
0x004c3608 with a 4-byte stride before "correcting" it.

**Second fix (2026-08-12, user-tested):** the follow walk zigzagged because the
ported zone walkers (`zone_walk_cw/ccw` in EntityCommon.cpp) lost the original's
scratch-array aliasing - `idx[1]` IS `dir[0]`, so the goal branch's
`best[1] = idx[1]` reads the target zone the branch just wrote, and a direct
adjacency (goal at step 0) must return the target as the first step. The split
arrays returned a stale leftover from the previous walk instead, so Barry
shuffled between zones 6 and 7 forever. Spelled out as
`best[n] = (n == step+1) ? zoneTarget : idx[n]` in both walkers. The sim
(`tools/sim_zone_walk.py`) was also fixed - its original-model comparison
hardcoded start zone 0, which is why the divergence went unnoticed.

**Not run in-game.** Build-verified only (full tree, zero errors). Ghidra updated with all
four names and saved.
