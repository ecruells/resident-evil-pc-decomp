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
| `check_room_collision` | 0x0047d310 | Resolves an entity against the room boundaries |
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