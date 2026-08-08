# RDT File Format (Room Definition Table)

RDT files (.rdt) define all data for a single room in Resident Evil 1.
Each room has its own .rdt file located at:

    .\usa\stage{X}\room{YY}.rdt

Where `X` is the stage ID + 1 (hex digit) and `YY` is the room ID (two hex digits).

## File Structure

The RDT file is a contiguous binary blob. After loading via `LoadFile()`, all
embedded relative pointers are relocated to absolute memory addresses by
`LoadRoomRdt()` (0x00477d90).

### Header (0x00 - 0x0B)

| Offset | Size  | Type  | Description |
|--------|-------|-------|-------------|
| 0x00   | 1     | char  | Unknown |
| 0x01   | 1     | char  | Number of cameras in this room |
| 0x02   | 1     | char  | Number of sound bank table entries |
| 0x03   | 3     | char  | Unknown |
| 0x06   | 2     | short | Ambient light � Red component |
| 0x08   | 2     | short | Ambient light � Green component |
| 0x0A   | 2     | short | Ambient light � Blue component |

### Lights (0x0C - 0x47)

3 light structures, each 0x14 (20) bytes:

| Offset | Size  | Type  | Description |
|--------|-------|-------|-------------|
| 0x00   | 4     | int   | Position X |
| 0x04   | 4     | int   | Position Y |
| 0x08   | 4     | int   | Position Z |
| 0x0C   | 1     | char  | Red component |
| 0x0D   | 1     | char  | Green component |
| 0x0E   | 1     | char  | Blue component |
| 0x0F   | 1     | char  | Zero (padding) |
| 0x10   | 1     | char  | Zero (padding) |
| 0x11   | 1     | char  | Zero (padding) |
| 0x12   | 2     | short | Light radius |

### Data Type Pointers (0x48 - 0x93)

All pointers are relative offsets within the .rdt file. After loading,
`LoadRoomRdt()` relocates them to absolute addresses by adding the file's
base address minus 3 (due to the PS1 pointer format).

| Offset | Size  | Type   | Description | Asset Type |
|--------|-------|--------|-------------|------------|
| 0x48   | 4     | ptr    | Camera switch zone definitions | � |
| 0x4C   | 4     | ptr    | Room collision boundaries | .blk |
| 0x50   | 4     | ptr    | Room 3D item models & textures | .tmd/.tim |
| 0x54   | 4     | ptr    | Obstacles & movable object models | .tmd/.tim |
| 0x58   | 4     | ptr    | Block data | .blk |
| 0x5C   | 4     | ptr    | Footstep sound zone map | .flr |
| 0x60   | 4     | ptr    | Initialization scripts (run once at room load) | .scd |
| 0x64   | 4     | ptr    | Main scripts (run every frame) | .scd |
| 0x68   | 4     | ptr    | Event/conditional scripts | .scd |
| 0x6C   | 4     | ptr    | Unknown | � |
| 0x70   | 4     | ptr    | Unknown | � |
| 0x74   | 4     | ptr    | Message text data | .msg |
| 0x78   | 4     | ptr    | Unknown | � |
| 0x7C   | 4     | ptr    | Effect animation index | .esp |
| 0x80   | 4     | ptr    | Effect animation data | .eff |
| 0x84   | 4     | ptr    | Effect sprite image textures | .tim |
| 0x88   | 4     | ptr    | Sound attribute table | .snd |
| 0x8C   | 4     | ptr    | VAB sound bank header | .vh |
| 0x90   | 4     | ptr    | VAB sound bank data | .vb |

### Cameras (0x94 +)

Variable-length array of camera entries. The number of entries is given by
`cameras_count` in the header. Each camera is 0x2C (44) bytes:

| Offset | Size  | Type  | Description |
|--------|-------|-------|-------------|
| 0x00   | 4     | int   | Mask data pointer (relocated) |
| 0x04   | 4     | int   | TIM mask texture pointer (relocated) |
| 0x08   | 4     | int   | Camera position X |
| 0x0C   | 4     | int   | Camera position Y |
| 0x10   | 4     | int   | Camera position Z |
| 0x14   | 4     | int   | Camera look-at target X |
| 0x18   | 4     | int   | Camera look-at target Y |
| 0x1C   | 4     | int   | Camera look-at target Z |
| 0x20   | 4     | int   | Camera roll angle |
| 0x24   | 4     | int   | Light index / reserved |
| 0x28   | 4     | int   | Field of view (FOV) |

## Collision Boundary Data (.blk)

The `boundaries` pointer (RDT+0x4C) points at the room's collision geometry:
a 0x18-byte header followed by a flat array of 0x0C-byte records.

### Header (0x00 - 0x17)

| Offset | Size | Type   | Description |
|--------|------|--------|-------------|
| 0x00   | 2    | short  | cellX — X of the quadrant split point |
| 0x02   | 2    | short  | cellZ — Z of the quadrant split point |
| 0x04   | 4    | int    | Record count for quadrant 0 |
| 0x08   | 4    | int    | Record count for quadrant 1 |
| 0x0C   | 4    | int    | Record count for quadrant 2 |
| 0x10   | 4    | int    | Record count for quadrant 3 |
| 0x14   | 4    | int    | Unused (read and discarded) |

`Room_SetupCollisionCallbacks` (0x0047d140) rewrites 0x04-0x14 **in place** into
five absolute pointers, so after room load quadrant `q` spans
`[group[q], group[q+1])`. The rewrite is not idempotent — it runs exactly once
per `LoadRoomRdt`.

### Record (0x0C bytes)

| Offset | Size | Type   | Description |
|--------|------|--------|-------------|
| 0x00   | 2    | short  | xMax |
| 0x02   | 2    | short  | zMax |
| 0x04   | 2    | short  | xMin |
| 0x06   | 2    | short  | zMin |
| 0x08   | 2    | ushort | Shape / step: low byte = shape index, bits 8-14 = floor step magnitude, bit 15 = step is downward |
| 0x0A   | 2    | ushort | Flags: low byte = step fine value, bit 8 = blocks movement, bit 9 = participates in the "still stuck" re-test |

The **MAX corner comes first**. This is not the `(x, z, w, d)` layout the
RE2-era notes on this format describe: `boundary_point_outside` (0x0047d2a0)
accepts a point only when `field[2] - r <= px <= field[0] + r`, which is an
empty interval for any box wider than `2r` under the width reading. Rasterising
the shipped rooms with the max-first reading reproduces their real floorplans.

Shape index selects a handler from `g_CollisionShapeHandlers` (0x00ac9c00).
Only four values occur across all 320 shipped RDTs:

| Shape | Handler | Address | Behaviour |
|-------|---------|---------|-----------|
| 1 | `collision_push_rect` | 0x0047df10 | Push out of a rectangle |
| 3 | `collision_push_circle` | 0x0047e0e0 | Push out of a circle (radius from the X extent) |
| 4 | `collision_flag_set` | 0x0047e1b0 | Soft zone — raises `collisionFlags` bit 3, no push |
| 5 | `collision_push_rect` | 0x0047df10 | As 1, but skipped while `collisionFlags` bit 4 is set |

### Quadrants

`ChkOutsideCell` (0x0047d270) returns a 2-bit quadrant index for a position:
bit 0 = the point is on the low side of `cellX`, bit 1 = low side of `cellZ`.
Records whose box straddles a split are duplicated into every quadrant they
touch, so testing one quadrant's list is sufficient for any single point.

### Resolution

`check_room_collision` (0x0047d310) runs two passes over the quadrant list.
Pass 1 tests the incoming position and lets each hit record's handler push the
entity out along the axis whose push opposes this frame's movement. Pass 2
re-tests the pushed position; if it is still inside a blocking record the whole
frame's movement is rolled back to `Entity+0x6C` (the last safe position),
otherwise `Entity+0x6C` advances to the resolved position.

Return values: 0 = clear, 1 = pushed out and now clear, 2 = still stuck
(position reverted), 3 = a floor/step zone was crossed, with its step value left
in the scratch dword at 0x00be0dfc. The distance actually travelled is left at
0x00be0df8.

**`boundary_classify` (0x0047d1b0) tests a global, not its argument.** The point
it classifies is always `g_playerPosScratch` (0x00be11b0); the `SVECTOR*` first
parameter is only an offset added to it. Every caller has to publish the point
it means into that global first. `check_room_collision` does so at the top of
each pass, and `FUN_0047d6f0` does so once per endpoint (0x0047d80a) and again
before its re-walk (0x0047d8ec) — as a plain 16-byte VECTOR copy that Ghidra
renders as five stores through short halves (`player_pos.x._0_2_ = centre->x`
and friends), which reads like scratch bookkeeping and is easy to mistake for
dead code. Drop it and the routine silently classifies whatever position the
previous caller left behind.

### Two-point body test (0x0047d6f0)

`FUN_0047d6f0(endA, endB)` resolves a body that is too long for the single-point
`check_room_collision`: a prone zombie, or a pushed room object's footprint. The
two `SVECTOR` endpoints are in body-local space and get rotated twice — by the
entity yaw (+0x74) to produce the test centres, and by the mirror angle (+0x7E)
to produce the values the shape handlers write back into.

End B is walked first (the original's loop counter starts at 1). If end A came
out clear the push stands and the position/angle backups advance; otherwise end
B's records are re-walked and, if anything still blocks, position **and** angle
roll back together from +0x6C/+0x70 and +0x7E.

Returns 0 = clear, 1 = pushed clear, 0x80 = still stuck (rolled back). Note that
when end A is clear it returns end B's bits, so a hit on either end is enough to
report blocked — the test is not direction-aware.

Quirk of the original, reproduced: the re-walk uses the **previous** quadrant
(`group[cellB - 1] .. group[cellB]`), not end B's own quadrant, because the
saved slot holds `&group[cellB]` and the loop reads through `[-1]`.

### Room 3D-object collision, pushing and climbing

Room 3D objects do NOT go through `check_room_collision` — that function only
ever walks the boundary records. Objects have their own resolver,
`update_room_objects` (0x00474090, Ghidra's `update_sounds` — the name is
wrong, it touches no sound code), called once per frame from `game_loop`
between `DrawFadeSpr` and the camera/lighting update.

#### The `omodel_set` instruction (SCD 0x1F, 0x1C bytes)

`cmd_omodel_set` (0x00461ac0) unpacks it into a 0xA4-byte block:

| Operand | Size | Goes to | Meaning |
| ------- | ---- | ------- | ------- |
| +0x00 | 1 | — | opcode 0x1F |
| +0x01 | 1 | record +0x01 (masked 0x7F) | model index; bit 0x80 queues the texture, bits 0-5 also index the RDT model table |
| +0x02 | 1 | record +0x00 | flag byte |
| +0x03 | 1 | record +0x64 | SCA parent: 0xFE = player, 0xFF = none, <0x80 = another object, else enemy `& 0x7F` |
| +0x04 | 2 | record +0x34 / +0x6C | position X |
| +0x06 | 2 | record +0x38 / +0x6E | position Y |
| +0x08 | 2 | record +0x3C / +0x70 | position Z |
| +0x0A | 2 | record +0x74 and +0x7E | yaw |
| +0x0C | 2 | record +0x94 | floor probe end A, X |
| +0x0E | 2 | record +0x98 | floor probe end A, Z |
| +0x10 | 2 | record +0x9C | floor probe end B, X |
| +0x12 | 2 | record +0xA0 | floor probe end B, Z |
| +0x14 | 2 | record +0x92 | body radius (`Sca_info` +10) |
| +0x16 | 2 | record +0x8C **and** +0x90 | half-extent Y |
| +0x18 | 2 | record +0x8A | half-extent X |
| +0x1A | 2 | record +0x8E | half-extent Z |

The endpoints' Y components (+0x96, +0x9E) are never written — not by the port
and not by the original — so they hold whatever was in the buffer. Harmless: a
Y rotation cannot mix Y into X or Z, and the boundary test is 2D.

`tools/parse_init_scd.py` decodes 0x1F one byte late (it reads position from
+5/+7/+9); use the table above, not that script's output.

#### The runtime record

The block lives in `g_itemboxes_covers_table[0 .. RDT.sound_banks_count)` and is
laid out like the head of an `Entity`, so the shared helpers can take one on
either side of a collision test:

| Offset | Meaning |
| ------ | ------- |
| +0x00  | flag byte — see below |
| +0x01  | model byte; bit 0x40 selects the heavy grunt SFX (0x17 vs 0x16) |
| +0x04  | `Sca_info` — points at the record's own +0x88 |
| +0x34/+0x38/+0x3C | live position X/Y/Z (`localMatrix.t`, 32-bit) |
| +0x64  | SCA parent matrix pointer |
| +0x6C/+0x6E/+0x70 | last committed position X/Y/Z (16-bit) |
| +0x74  | yaw; +0x7E is its rollback copy |
| +0x86  | push hold counter (16-bit; +0x86/+0x87, an Entity's behavior/state) |
| +0x88  | 0x8000 — the negative terminator of the size list |
| +0x8A / +0x8C / +0x8E | half-extents X / Y / Z (`Sca_info` +2/+4/+6) |
| +0x90 / +0x92 | the entity-side extents (`Sca_info` +8/+10) |
| +0x94, +0x9C | the two floor-probe endpoints fed to `FUN_0047d6f0` |

Flag byte bits: 0x01 = active, 0x02 = intangible, 0x04 = skip the floor probe,
0x08 = no collision at all, 0x20 = not pushable, 0x40 = climbable.

Only bit 0 is cleared at room load; the rest of the block is uninitialised until
an `omodel_set` fills it, which is why every pass gates on it.

#### Per frame

For every active record:

1. every enemy is resolved out of it with `ChkEntitySlide(enemy, obj, 0)`;
2. `ChkEntitySlide(player, obj, 1)` runs in "move the object" mode purely as a
   test — if it reports an overlap **and** the player is holding forward
   (D-pad bit 0) **and** `ChkPlReachEntity` puts the object inside the 470-unit
   reach probe, the record's +0x86 counter increments, otherwise it is reset;
3. at counter == 9 (nine straight frames of pushing into it) the push starts:
   the player's facing snaps to the nearest cardinal, `DAT_00ae9ee8` remembers
   the record, and the push is cancelled again if the object's own floor probe
   (`FUN_0047d6f0` on +0x94/+0x9C, skipped when flag bit 0x04 is set) hits a
   wall, if an enemy is in the way, or if another object overlaps
   (`ChkObjSlide`), each of which parks the counter at 10;
4. `ChkEntitySlide(player, obj, 0)` resolves the player out of the object —
   this, and only this, is what makes room objects solid;
5. if the object moved, it shoves the other objects with `ChkObjSlide` and
   commits the new position to +0x6C/+0x70;
6. `update_player_position(obj, 4)` fires the object's own event-zone probes.

Step 2 moves the object as a side effect, and step 3 normally puts it straight
back. The **one** path that keeps the displacement is the frame where the push
animation is already running (`action_behavior` already 0x10) — that is what
actually slides the object across the floor. A successful start parks the
counter at 8, not 0, so it climbs back to 9 on every subsequent frame and the
whole block re-runs for the duration of the push.

After the loop, a started push raises `g_main_state_flags` bit 0x40, and that
bit is the sole trigger for `action_behavior` 0x10 (`behavior_10_push`,
0x00457230). Releasing forward clears the bit, which is how the push animation
knows to end. The grunt SFX it plays on animation frame 1 comes from bit 0x40 of
byte +0x01 of the pushed record (0x17 heavy / 0x16 light).

#### The box tests

Three routines share one idiom: containment is checked as an unsigned wrap,
`(unsigned)(delta + ext) <= (unsigned)(2 * ext)`, which rejects both sides of
the interval in a single compare because a delta below `-ext` wraps to a huge
unsigned value.

| Routine | Address | Test |
| ------- | ------- | ---- |
| `ChkPlReachEntity` | 0x00474a20 | Is the player's 470-unit forward probe inside the record's +0x8A/+0x8E box? Side effect: zeroes the shallower axis of `g_svecScratch`. The delta is `probe - centre`, i.e. the object position is **subtracted out of** the extent (0x00474a8c / 0x00474a8f) — reversing it makes the sum ≈ `probe + centre` and the test fails for every object not at the world origin. |
| `ChkEntitySlide` | 0x00474330 | Entity vs object on all three axes, then resolve along whichever of X/Z is cheaper to escape (compared via the cross products `extX*dz` and `extZ*dx`, so no divide). Mode 0 moves the entity, mode 1 moves the object and returns the number of axes moved. |
| `ChkObjSlide` | 0x00474500 | Object vs object, X/Z only, both extents from `Sca_info`. Places the pushed object one unit clear of touching so the next frame does not re-trigger. |

`ChkEntitySlide` walks the entity's part list: offsets advance 6 bytes per part
(from +0x08) while the size list advances 0xC (from +0x04), terminating on a
negative first short — tested *after* the part is processed, so a list is always
walked at least once. The extents themselves are re-read from the **first** size
record every iteration (0x004743b6 reloads `[ECX+4]`, not the advanced pointer),
so only the part offsets really vary. Object records set +0x88 to 0x8000, giving
them exactly one part.

#### Climbing

Climbing is a separate path and does not involve `update_room_objects`.
`check_climb_object` (0x00474930) scans the same table on the action-button
press, from `g_omodelCount - 1` down, for the first record with flag bit 0x40,
inside `ChkPlReachEntity`, and facing within 299/4096 (~26°) of the player on
either wrap-around side. It raises `g_main_state_flags` bit 0x80, stores the
record at 0x00ae9ef0, and picks `attackDirection` from the facing.

Note the two scratch globals sit next to each other and are easy to confuse:
0x00ae9ef0 is the **climb** candidate, 0x00ae9ee8 is the object
`update_room_objects` is **pushing**. `behavior_10_push` reads the latter.

`player_input_to_behavior` turns that into `action_behavior` 0x0A — the same
handler as a door (`FUN_00457390`) — where bit 0x80 selects the vault
displacement and SFX (0x23) instead of the door ones (0x2D). While the bit is
raised, calling `check_climb_object` again verifies the player has not turned
away: drifting cancels, staying clears the bit and raises `unk_03` bit 0x10,
which mirrors every Z displacement (climbing down rather than up).

#### Reference room

Room 1070 has both cases:

| Slot | Flags | Object | Half-extents X/Z | Radius |
| ---- | ----- | ------ | ---------------- | ------ |
| 0 | 0x41 | step ladder — climbable **and** pushable | 990 / 800 | 725 |
| 1 | 0x01 | shelf — pushable only | 1050 / 1350 | 540 |

Neither sets bit 0x20, so both push; only slot 0 sets bit 0x40, so only it
climbs.

## Message Data Format (.msg)

The `messages` pointer (RDT+0x74) points to a message table with the format:

```
uint16  offset[msg_count]   // Offset table: each entry is a byte offset
                             // from the start of the messages block
char    message_data[]       // Null-terminated message strings
```

To resolve message `i`: `msgBase + offset[i]`

Global messages (msg_id bit 6 set) use the `global_messages[]` table at
0x004bfc58 instead of the RDT-local messages.

## SCD Script Format (.scd)

The SCD (Script) data contains bytecode opcodes that control room behavior:

- **initialization_scd** (RDT+0x60): Runs once when the room is loaded
- **scd_opcodes** (RDT+0x64): Runs every frame (main loop)
- **scd_opcodes2** (RDT+0x68): Event handlers and conditional checks

Script opcodes are processed by `run_command_functions()` during `game_loop()`.

## Related Functions

| Function | Address | Description |
|----------|---------|-------------|
| `LoadRoomRdt` | 0x00477d90 | Loads .rdt file and relocates pointers |
| `init_room` | 0x00409990 | Initializes room: stage tables, animations, room_set |
| `room_set` | 0x00477720 | Sets up room collision, cameras, items, obstacles |
| `Room_SetupCollisionCallbacks` | 0x0047d140 | Relocates boundary counts to pointers, installs shape handlers |
| `ChkOutsideCell` | 0x0047d270 | Boundary quadrant index for a position |
| `boundary_classify` | 0x0047d1b0 | Shape index if `g_playerPosScratch` is inside a record, else 0xFFFF |
| `check_room_collision` | 0x0047d310 | Resolves an entity against the room boundaries |
| `FUN_0047d6f0` | 0x0047d6f0 | Two-point body test (prone entities, pushed objects) |
| `cmd_omodel_set` | 0x00461ac0 | SCD 0x1F — builds a room 3D-object record |
| `update_room_objects` | 0x00474090 | Per-frame object collision + push driver (Ghidra: `update_sounds`) |
| `ChkEntitySlide` | 0x00474330 | Entity vs object box resolve |
| `ChkObjSlide` | 0x00474500 | Object vs object box resolve |
| `ChkPlReachEntity` | 0x00474a20 | Player's 470-unit forward probe vs an object box |
| `check_climb_object` | 0x00474930 | Action-key climb candidate scan |
| `behavior_10_push` | 0x00457230 | The push animation (`action_behavior` 0x10) |
| `set_message_display` | 0x00455670 | Resolves message from RDT or global table |
| `run_command_functions` | � | Processes SCD script opcodes |

## Memory Layout

The RDT is loaded into `g_loadDataDestPointer` (typically `g_DataBuffer`).
After pointer relocation, `g_RdtPointer` points to the loaded RDT header.

Key globals:
- `g_RdtPointer` (0x00bebcd0): Pointer to current room's RDT data
- `g_stageId` (0x00be9820): Current stage ID
- `g_roomId` (0x00be9821): Current room ID within the stage
- `g_roomCameraId` (0x00be9822): Current active camera index