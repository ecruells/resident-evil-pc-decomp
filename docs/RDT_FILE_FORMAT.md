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
| 0x06   | 2     | short | Ambient light — Red component |
| 0x08   | 2     | short | Ambient light — Green component |
| 0x0A   | 2     | short | Ambient light — Blue component |

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
| 0x48   | 4     | ptr    | Camera switch zone definitions | — |
| 0x4C   | 4     | ptr    | Room collision boundaries | .blk |
| 0x50   | 4     | ptr    | Room 3D item models & textures | .tmd/.tim |
| 0x54   | 4     | ptr    | Obstacles & movable object models | .tmd/.tim |
| 0x58   | 4     | ptr    | Block data | .blk |
| 0x5C   | 4     | ptr    | Footstep sound zone map | .flr |
| 0x60   | 4     | ptr    | Initialization scripts (run once at room load) | .scd |
| 0x64   | 4     | ptr    | Main scripts (run every frame) | .scd |
| 0x68   | 4     | ptr    | Event/conditional scripts | .scd |
| 0x6C   | 4     | ptr    | Unknown | — |
| 0x70   | 4     | ptr    | Unknown | — |
| 0x74   | 4     | ptr    | Message text data | .msg |
| 0x78   | 4     | ptr    | Unknown | — |
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
| `set_message_display` | 0x00455670 | Resolves message from RDT or global table |
| `run_command_functions` | — | Processes SCD script opcodes |

## Memory Layout

The RDT is loaded into `g_loadDataDestPointer` (typically `g_image_buffer`).
After pointer relocation, `g_RdtPointer` points to the loaded RDT header.

Key globals:
- `g_RdtPointer` (0x00bebcd0): Pointer to current room's RDT data
- `g_stageId` (0x00be9820): Current stage ID
- `g_roomId` (0x00be9821): Current room ID within the stage
- `g_roomCameraId` (0x00be9822): Current active camera index