# SCD Script System

The SCD (SCript Data) system is the room scripting VM. Every room's logic —
door placement, item placement, enemy spawns, camera cuts, cutscene
choreography, lighting, BGM — is data in the RDT file interpreted by two
cooperating interpreters:

| Interpreter | Address | Source | Role |
|---|---|---|---|
| `run_command_functions` | `0x00473f60` | [RoomEvents.cpp:99](../src/game/RoomEvents.cpp#L99) | **Command VM.** Runs a linear block of SCD commands to completion, synchronously. Used for room init. |
| `room_events_check` | `0x0041d6a0` | [RoomEvents.cpp:555](../src/game/RoomEvents.cpp#L555) | **Event VM.** Runs 8 concurrent coroutine-like event scripts, one step per frame. Used for cutscenes and per-frame room logic. |

Both dispatch individual commands through the same 81-entry table
`script_command_funcs_table` (`0x004c1110`), implemented in
[CmdFunctions.cpp](../src/game/CmdFunctions.cpp).

---

## 1. Data sources

`room_set` (`0x00477xxx`, [GameState.cpp:1228](../src/game/GameState.cpp#L1228))
wires three RDT sections into globals:

```c
g_RoomInitScd    = g_RdtPointer->initialization_scd;  // 0x00d213bc — block list for the command VM
g_RoomScdOpcodes = g_RdtPointer->scd_opcodes;         // 0x00d213b8
g_RoomEventScripts     = g_RdtPointer->scd_opcodes2;        // 0x00d213b4 — event script pointer table
```

`g_RoomEventScripts` is a NULL-terminated array of `int` **relative** offsets that
`room_set` relocates in place to absolute pointers:

```c
int* evtPtr = (int*)g_RoomEventScripts;
while (*evtPtr != 0) { *evtPtr += (int)g_RoomEventScripts; evtPtr++; }
```

So `((unsigned char**)g_RoomEventScripts)[scriptIndex]` is the entry point of event
script `scriptIndex`.

---

## 2. Command VM — `run_command_functions`

### Container format

The initialization SCD is a chain of **blocks**. Each block is:

```
+0x00  u16  blockSize    // total bytes of this block INCLUDING these 2 header bytes
+0x02  ...  opcodes      // command stream, terminated by opcode 0x00 (cmd_nop)
```

A `blockSize` of `0` terminates the chain.

```c
g_CmdOpcodesPointer = DAT_00bf0808;   // reset the branch stack
g_ScriptContinueFlag = 0;             // reset the nesting depth
for (u16 sz = *p; sz != 0; sz = *p) {
    g_ScdOpcodes = (u8*)(p + 1);      // skip the size word
    ... run until cmd_nop, then unwind pending branches ...
    p = (u16*)((u8*)p + sz);          // next block
}
g_ScdOpcodes = (u8*)p;                // leave the pointer on the terminator
```

### Execution + branch unwinding

```c
while (true) {
    do {
        cmdResult = table[*g_ScdOpcodes]();
    } while (cmdResult != 0);         // run until a command returns 0

    if (g_ScriptContinueFlag == 0) break;   // no pending branch → block done

    g_CmdOpcodesPointer--;                  // pop a saved resume address
    g_ScdOpcodes = (u8*)*g_CmdOpcodesPointer;
    g_ScriptContinueFlag--;
}
```

Two return conventions drive this:

- **Return 1** — "continue": the interpreter immediately dispatches the next
  opcode. Every side-effecting command returns 1.
- **Return 0** — "stop": ends the straight-line run. Used by `cmd_nop` (end of
  stream) and by *condition* commands whose test failed. A failed condition
  therefore aborts the rest of the stream, and the outer loop resumes at the
  address `cmd_if` pushed — that is how `if` skips its body.

Conditional commands are `0x04 0x06 0x07 0x10 0x11 0x1A 0x1D 0x22 0x36 0x38
0x3C 0x3F 0x50`. They return the boolean directly, so `false` = 0 = stop = skip.

### Interpreter state

| Global | Address | Type | Meaning |
|---|---|---|---|
| `g_ScdOpcodes` | `0x00bf0800` | `u8*` | Instruction pointer. **In the original this is a `ushort*`** — see §5. |
| `g_CmdOpcodesPointer` | `0x00bf0804` | `u32*` | Branch stack pointer (grows up). |
| `DAT_00bf0808` | `0x00bf0808` | `u32[16]` | Branch stack storage. 64 bytes, ends at `g_pScdEventCurrent` (`0x00bf0848`). |
| `g_ScriptContinueFlag` | `0x00bf07fa` | `u8` | Pending-branch / nesting depth counter. |

### Callers

- `room_init` — runs `g_RoomInitScd` once when a room loads.
- Event opcode `0x06` (`scd_event_cmd_run_scd`) — runs an inline block embedded
  in an event script.

---

## 3. Event VM — `room_events_check`

Runs every frame from `game_loop`, plus once from `room_set`. Gated on
`g_message_flags & 0x80`. Iterates all 8 slots of `g_ScdEventTable`.

### `ScdEventEntry` (0x34 bytes, table at `0x00bf084c`)

```c
struct ScdEventEntry {
    u8   state;            // 0x00  0=cmd, 1=wait-anim, 2=movement, 3=set-behavior
    u8   pad_01;           // 0x01  cleared when leaving state 1
    u8   active;           // 0x02  0 = slot free
    u8   stackDepth;       // 0x03  index into the three stacks below; 0xFF = empty
    Entity* entity;        // 0x04  entity this script drives
    u8*  scriptPtr;        // 0x08  instruction pointer
    u32  returnStack[4];   // 0x0C  loop / call resume addresses
    u32  callStack[4];     // 0x1C  saved command addresses (0xFC / 0xFD)
    s16  counterStack[4];  // 0x2C  loop counters
};
```

`stackDepth` is indexed as `(signed char)stackDepth`, so the `0xFF` empty
sentinel means the first push lands at index 0 (`0xFF + 1`).

### Entry lifecycle

`ScdEventEntry_Init` (`0x0041d620`) / `ScdEventEntry_Create` (`0x0041d650`):

```c
entry->active     = 1;
entry->state      = 0;
entry->scriptPtr  = ((u8**)g_RoomEventScripts)[scriptIndex];
entry->stackDepth = 0xFF;
entry->entity     = ENTITY;
```

`ScdEventEntry_Create(slot, scriptIndex)` with `slot > 7` allocates the first
free slot.

### Control-flow opcodes (checked before the state switch, so they work in every state)

| Op | Len | Effect |
|---|---|---|
| `0xF6` | 1 | Push stack level, advance, then fall into `0xF7`. |
| `0xF7` | 1 | **Wait**: if `((u8*)&g_main_state_flags)[1] & 2` is clear *and* `g_menu_choice_id & 0x80` is clear, advance and pop. Otherwise stay. Yields the frame either way. |
| `0xF8` | 3 | Push level, `counterStack[top] = *(s16*)(p+2)`, advance 1, fall into `0xF9`. |
| `0xF9` | 3 | `--counterStack[top]`; when it hits 0, skip 3 bytes and pop. Yields the frame. |
| `0xFA` | 4 | Loop head: push level, `counterStack[top] = *(s16*)(p+2)`, advance 4, `returnStack[top] = scriptPtr`. |
| `0xFB` | 1 | Loop tail: `--counterStack[top]`; 0 → advance 1 and pop, else jump to `returnStack[top]`. |
| `0xFC` | 2 | Call: push level, `callStack[top] = p+2`, `scriptPtr += p[1]`, `returnStack[top] = scriptPtr`. |
| `0xFD` | 1 | Run the command at `callStack[top]` through `script_command_funcs_table`. Returned 0 → advance 1 and pop; non-zero → jump to `returnStack[top]`. This is the event VM's `if`. |
| `0xFE` | 1 | Advance 1, yield the frame. |
| `0xFF` | 1 | `active = 0` then advance 1, yield. |

### State 0 — command opcodes ([RoomEvents.cpp:655](../src/game/RoomEvents.cpp#L655))

| Op | Len | Effect |
|---|---|---|
| `0x00` | 1 | NOP. |
| `0x01` | 1 | → state 1 (wait animation). |
| `0x02` | 1 | → state 2, and `entity->ignore_player_flag = 2`, `action_behavior = 0`, `action_state = 0`. |
| `0x03` | 1 | → state 2 without touching the entity. |
| `0x04` | 3 | Set `entity` from `[type][index]`: 0=player, 1=`g_EnemiesList[i]`, 2=`g_omodel_table[i]`, 3=`g_interactable_table[i]`. |
| `0x05` | 3 | `ScdEventEntry_Create(p[1], p[2])`. |
| `0x06` | var | `run_command_functions(p+2)`, then `scriptPtr += p[1]`. |
| `0x07` | var | Set `g_ScdOpcodes = p+2`, `scriptPtr += (*(u16*)p >> 8)`, then dispatch one command through the table. |
| `0x08` | 2 | `ScdEventEntry_Init(self, p[1])` — restart with a different script. Yields. |
| `0x09` | 2 | `g_ScdEventTable[p[1]].active = 0`. |
| other | — | Unknown opcode: deactivate the entry and bail out of the whole function. |

### State 1 — animation opcodes (`scd_event_state1_anim`, `0x0041da30`)

Operates on `g_pScdEventCurrent->entity`. All return 1.

| Op | Len | Effect |
|---|---|---|
| `0x00` | 1 | Advance. |
| `0x80` | 1 | Clear `ignore_player_flag`, then as `0x8B`. |
| `0x8B` | 1 | `pad_01 = 0`, advance, → state 0. |
| `0x81` | 2 or 12 | Set `lookAtFlags = p[1]`. If `flags & 0x0F`, consume 10 more bytes: `0x93` → resolve `scd_target_ptr` from a `[type][index]` pair; otherwise set `scd_pos_x/y/z` (with `flags & 0x20` wrapping negatives by `+0x1000`). Then `lookAtYawStep` (default `0xC0`) and `lookAtPitchStep` (default `0x40`). |
| `0x82` | 1 | `lookAtFlags &= ~0x10`. |
| `0x83` | 9 | `state=8`, behavior/action from `p[1..2]` (bit `0x1000` = additive mode gated on `collisionFlags & 0x80`), then `unk_c6`, `unk_c8`, `scd_anim_param`; `scd_timer_lo=0x28`. |
| `0x84` | 5 | `state=8`, `action_behavior=1`; `animationId`, `scd_anim_param`, `scd_entity_flags = (animData >> 6) & 0x3FC`. |
| `0x85` | 5 | `state=8`; behavior/action from `p[1..2]`, `animationId` + `scd_anim_param` from the next word. |
| `0x86` | 1 | Idle: `state=1`, behavior/action/hit_state cleared. |
| `0x87` | 4 | `scd_entity_flags` OR / SET / XOR by mode `p[1]`. |
| `0x88` | 3 | `scd_timer_lo`, `scd_timer_hi`. |
| `0x89` | 2 | Set `animation_frame_id`, reset blend. For `id < 0x20` and `animationId <= 0x0F`, remap through `DAT_004bec80`; `animationId > 0x0F` → `action_state = 3`. For `id >= 0x20` → `action_state = 1`. |
| `0x8A` | 2 | As `0x89` but no remap. |

`DAT_004bec80` (`0x004bec80`, 20 bytes) — `(action_state - 1, animationId)` pairs
indexed by the incoming `animationId`:

```
anim: 0      1      2      3      4      5      6      7      8      9
pair: (0,0) (0,1) (0,2) (0,3) (0,4) (1,0) (1,1) (1,2) (1,3) (1,4)
```

### State 2 — movement opcodes (`scd_event_state2_movement`, `0x0041e1a0`)

| Op | Len | Effect |
|---|---|---|
| `0x00` | 1 | NOP. |
| `0x01` | 1 | → state 0. |
| `0x02` | 1 | `localMatrix.t[0..2] += speed.x/y/z`. |
| `0x03` | 1 | Apply `move_step_x` to `position.pad` and `move_step_z` / `state` to the two angle halves. |
| `0x04` | 1 | `0x02` + `0x03`. |
| `0x05` | 4 | Set `speed.x/y/z` from 3 signed bytes. |
| `0x06` | 4 | Set `move_step_x`, `move_step_z`, and `state`/`ignore_player_flag` from 3 signed bytes. |
| `0x07` | 8 | Absolute position from 3 `s16`. |
| `0x08` | 3 | Write one transform component at byte offset `p[1]`. |
| `0x09` | 2 | `hit_state = p[1]`. |
| `0x0A` | 4 | Parametric set by `p[1]`: 0/1/2 = `localMatrix.t[i]`, 3 = `health`, 4 = `unk_c6`, 5 = `unk_c8`. |
| `0x0B` | 12 | As `0x0A`, then also `position.pad` and both angle halves. |

### State 3 — `scd_event_state3_set_behavior` (`0x0041e150`)

2 bytes. Sets `g_PlayerDpadHeld = 0xFF`; if `p[0] != entity->action_behavior`
resets `action_state`; assigns `action_behavior = p[0]`. Returns 1.

---

## 4. Command opcode reference (`script_command_funcs_table`, `0x004c1110`)

81 entries, `0x00`–`0x50`. There is **no padding** in the original — the byte
right after entry `0x50` is the string `"DOOR_AT_SET "`, so an opcode above
`0x50` executes string data. The decomp declares the array as 256 entries with
`0x51`–`0xFF` left `nullptr`, which turns that into a null-pointer crash instead
of arbitrary execution.

Notation: `[a,b]` is one 16-bit word, `a` = low byte, `b` = high byte. `Len` is
the total instruction length in bytes. **Cond** marks commands whose return
value is a condition (0 aborts the straight-line run).

| Op | Name | Addr | Len | Layout / effect |
|---|---|---|---|---|
| `0x00` | `cmd_nop` | `004604d0` | 1 | Clears `g_ScriptContinueFlag`, returns 0. End of block. |
| `0x01` | `cmd_if` | `004604e0` | 2 | `[op, skipLen]`. Pushes `(p+2) + skipLen` on the branch stack, `++g_ScriptContinueFlag`. |
| `0x02` | `cmd_else` | `00460520` | 2 | `[op, jumpLen]`. Pops the branch stack and jumps `p += jumpLen`. |
| `0x03` | `cmd_end_if` | `00460550` | 2 | Pops the branch stack. |
| `0x04` | `cmd_bit_test` | `00460570` | 4 | **Cond.** `[op, bank][sel, expect]`. `sel & 0x1F` = bit index, `(sel & 0xE0) >> 3` = byte offset into the bank. Returns `bitIsSet ^ expect`. Banks: 0 `g_PlayerFlags`, 1 `g_PlayerFlags3`, 2 `g_LocksFlags` (0x00be9874 — the same array `door_try_enter` checks), 3 `g_RoomEventFlags`, 4 `g_SysFlags`, 5 `g_main_state_flags`, 6 `g_message_flags`, 7 `g_roomItemsFlags`, 8 `g_RoomFlags`, 9 `DAT_00d213a0`. |
| `0x05` | `cmd_bit_op` | `00460650` | 4 | `[op, bank][sel, mode]`. `mode` 0=set, 1=clear, 2=toggle. Same bank/sel encoding as `0x04`. |
| `0x06` | `cmd_obj06_test` | `00460760` | 4 | **Cond.** `[op, fieldIdx][mode, cmpVal]`. Compares the byte at `(&g_stageId)[fieldIdx]`. `mode` 0 `==`, 1 `>`, 2 `>=`, 3 `<`, 4 `<=`, 5 `!=` (relative to `cmpVal`). |
| `0x07` | `cmd_obj07_test` | `00460800` | 6 | **Cond.** `[op, pad][fieldIdx, mode][cmpVal:u16]`. Compares `((u16*)&g_fading_state)[fieldIdx]`. |
| `0x08` | `cmd_room_cam_set` | `004608a0` | 4 | `[op, fieldIdx][value, pad]`. `(&g_stageId)[fieldIdx] = value`. |
| `0x09` | `cmd_cut_lock_set` | `00460920` | 2 | `[op, camId]`. Saves the current camera in `g_cutId`, switches to `camId`, walks `cam_switch_zones` (stride `0x14`, id at `+2`) to find it, calls `cut_set`, sets `g_main_state_flags |= 0x100000` (lock camera). |
| `0x0A` | `cmd_current_cut_set` | `00460990` | 2 | Restores the camera saved in `g_cutId` and clears `0x100000`. |
| `0x0B` | `cmd_message_set` | `004609f0` | 4 | `[op, msgId][pause:u16]`. `set_message_display(msgId, pause)`. The pause operand is a word, not a byte. |
| `0x0C` | `cmd_door_set` | `004611b0` | 26 | `[op, slot]` + a 24-byte door record. Writes `g_RoomItemEventTable[slot*0xC]`: `[0]=1`, `[1]=p[0x19]`, `[2..3]=slot`, `[8..11]= p+2` (record pointer). Bumps `g_RoomItemEventHead`. |
| `0x0D` | `cmd_item_set` | `00461130` | 18 | `[op, slot]` + 16 bytes. Writes the same 12-byte event entry: `[0]=p[0xA]`, `[1]=p[0xB]`, `[2..7]` = three `u16` from `p[0xC..0x11]`, `[8..11]= p+2`. |
| `0x0E` | `cmd_skip_2bytes_opcode` | `00460900` | 2 | No-op that advances. |
| `0x0F` | `cmd_entities_0x0f` | `004610b0` | 8 | `[op, modeBits][DAT_00d211c4:u16][DAT_00d21350:u16][DAT_00d2276c:u16]`. Sets `g_main_state_flags` bits 0-1, then re-runs `SetupEntityJointAnimation` on the player. |
| `0x10` | `cmd_obj10_test` | `00460f30` | 2 | **Cond.** `[op, itemId]`. `itemId == g_usedItemId`. |
| `0x11` | `cmd_obj11_test` | `00460f10` | 2 | **Cond.** `[op, value]`. `value == DAT_00be9833`. |
| `0x12` | `cmd_item_flag_0x12` | `00460fc0` | 10 | `[op, slot]` + 8 bytes → overwrites bytes `[0..7]` of the event entry. |
| `0x13` | `cmd_0x13` | `00461010` | 4 | `[op, slot][b0, b1]` → event entry bytes `[0]`, `[1]`. |
| `0x14` | `cmd_0x14` | `00461040` | 4 | `[op, pad][slot, scriptIdx]`. `ScdEventEntry_Create(slot, scriptIdx)`. |
| `0x15` | `cmd_bgm_0x15` | `00460a80` | 2 | `[op, ch]`. Sound channel record = `(u8*)&g_SndBank + ch*8` (bank `int` at `+0`, slot byte at `+5`). Calls `SetSndSlot`, sets `g_BGM_STATE |= 1 << (ch+3)`. |
| `0x16` | `cmd_volume_set` | `00460c70` | 2 | `[op, ch]`. If `g_BGM_STATE` bit `ch+3` set: `setSndStop`, clear the bit, `set_volume(bank, -1)`. |
| `0x17` | `cmd_player_pos_0x17` | `00460d80` | 6 or 10 | `[op, bank][sndId, vol][posType, enemyIdx]` then 4 more bytes for `posType` 0-3. `posType` 0 = explicit `(x,0,z)` written into `g_playerPosScratch` (`0x00be11b0`, three **ints**); 1 = `g_playerEntity.scaMatrixData.localMatrix.t` (`+0x34`); 2 = `g_EnemiesList[enemyIdx].scaMatrixData.localMatrix.t`; 3 = `play_sfx(bank, bank)`. `posType > 3` consumes only 6 bytes. Positions come from the 3-int `localMatrix.t`, **not** the packed `position` SVECTOR at `+0x6C`. |
| `0x18` | `cmd_item_model_set` | `00461220` | 26 | Interactive obstacle/desk model. `p[1] & 0x7F` = event slot (bit 7 = alt rotation), `p[0xA]` = item type, `p[0xC]` = desk index, `p[0xD]` = SCA parent (`0xFF` none, `0xFE` player, else itembox), `p[0xE..0x13]` = position `s16 x/y/z`, `p[0x14..0x15]` = anim word, `p[0x16]` = `g_roomItemsFlags` bit, `p[0x17..0x19]` = entry flags. Loads the TMD, may spawn a billboard when the flag is set and bit `0x8000` is present. |
| `0x19` | `cmd_obj19_set` | `00460f50` | 4 | `[op, deskIdx][value, pad]`. `*(u8*)g_interactable_table[deskIdx] = value`. |
| `0x1A` | `cmd_item_search` | `00460f80` | 2 | **Cond.** `[op, itemId]`. Scans `g_ItemSlotsPointer` (stride 2) over `g_TotalHeldItems`. |
| `0x1B` | `cmd_em_set` | `004617d0` | 22 | Enemy spawn. `p[1]` = enemy type id, `p[2]` = `behavior_flags`, `p[3]` = `g_RoomEventFlags` guard bit (`0xFF` = none; if already set, skip the spawn), `p[4]` = force-init, `p[5]` = SCA hit-data size / 6, `p[6..7]` = `position.pad`, `p[8..9]` = yaw, `p[0xA..0xB]` = pitch, `p[0xC..0xD]` = x, `p[0xE..0xF]` = y, `p[0x10..0x11]` = z, `p[0x12] & 0xF` = slot, `p[0x13]` = `animationId`, `p[0x14]` = `animation_frame_id`, `p[0x15]` = extra flags. |
| `0x1C` | `cmd_0x1c` | `00462210` | 6 | `[op, lightR][delta:s16][rgbMask:u16]`. Special room light: `delta != 0` seeds `g_SpecialRoomLightState` to `0` or `0x7FFF` by sign. Mask bits 0/1/2 → B/G/R = `0xFF`. |
| `0x1D` | `cmd_weapon_set` | `00460ee0` | 2 | **Cond.** `[op, weaponId]`. Compares the equipped slot's item id. |
| `0x1E` | `cmd_sfx_set` | `00461a80` | 4 | `[op, type][param:u16]`. `play_sound_and_voice_effect`, then `g_main_state_flags |= 0x20000`. |
| `0x1F` | `cmd_omodel_set` | `00461ac0` | 28 | Static object model. `p[1] & 0x3F` = itembox slot (bit 7 = queue texture), `p[2]` = entry flags (bit 4 = alt rotation), `p[3]` = SCA parent (`0xFF` none, `0xFE` player, `<0x80` itembox, else enemy), `p[4..9]` = position `s16 x/y/z`, `p[0xA..0xB]` = anim word, `p[0xC..0x1B]` = sprite/anim parameters. Contains many stage/room-specific palette and position fixups. |
| `0x20` | `cmd_player_pos_set` | `00430f60` | 14 | `[op,pad][posPad:s16][directionAngle:s16][speedX:s16][x:s16][y:s16][z:s16]` — offsets `+2,+4,+6,+8,+10,+12`. Clears `unk_e0` bits 2-3 via `&= 0xFFF3`. Positions mirror into `localMatrix.t[0..2]`. Note `+6` is `speed.x` (`0x76`) in `PlayerEntity`, whereas the same offset in `Entity` (opcode `0x21`) is the upper half of `angle`. |
| `0x21` | `cmd_enemy_pos_set` | `00430fe0` | 14 | `[op, enemyIdx][pad:s16][yaw:s16][pitch:s16][x:s16][y:s16][z:s16]` — offsets `+2,+4,+6,+8,+10,+12`, same shape as `0x20`. `enemyIdx` is `(s16)word0 >> 8` (arithmetic). Clears `scd_entity_flags` (`+0xE0`) bits 2-3 via `&= 0xFFF3`. Positions mirror into `localMatrix.t[0..2]`. |
| `0x22` | `cmd_item_cmd_0x22` | `00431100` | 4 | **Cond.** `[op, searchId][mode, cmpVal]`. Sums inventory quantities for an ammo *family* (`searchId` 10-0x12 group several item ids), compares the total. Returns 0 if nothing matched. |
| `0x23` | `cmd_cut_toogle` | `00431280` | 2 | `[op, lock]`. `lock != 0` sets `g_main_state_flags |= 0x100000`, else clears it. |
| `0x24` | `cmd_room_action` | `004312b0` | 4 | `[op, itemSlot][actionIdx, pad]`. Calls `room_check_actions[actionIdx](&g_RoomItemEventTable[itemSlot*0xC])`. |
| `0x25` | `cmd_rdt_0x25` | `004621d0` | 4 | `[op, pad][sprId, disable]`. `disable == 0` → `RoomSpr_SetActive(sprId)`, else `RoomSpr_SetInactive(sprId)`. |
| `0x26` | `cmd_nop_0x26` | `00460ce0` | 0 | **Dead slot.** Bare `ret`; returns the opcode value left in `EAX` and consumes nothing, so the interpreter spins forever. See §6c. |
| `0x27` | `cmd_snd_fade_set` | `00460cf0` | 2 | `[op, fadeType]`. `BuildSndFadeTbl(fadeType, 0x7F)`. |
| `0x28` | `cmd_enemy_0x28` | `004312f0` | 4/6/8 | `[op,pad][enemyIdx, subCmd][param:u16]...`. `subCmd`: 0 `behavior_flags`, 1 `state=2` + `health` + `hit_state` (8 bytes), 2 `action_behavior`, 3 `status_flags` SET/OR/XOR, 5 yaw, 6 `blend_counter=0` (4 bytes), 8 `state=9` (4 bytes), 9 toggle joint flags by bitmask, 10 `action_state`. |
| `0x29` | `cmd_fmv_set` | `00461a40` | 2 | `[op, fmvId]`. `g_main_state_flags |= 0x40000`. |
| `0x2A` | `cmd_effect_spawn` | `004316c0` | 12 | `[op, type][parentIdx, parentType][x:s16][y:s16][z:s16][flags:u16]`. `parentType` 0 = identity matrix, 1 = player matrix, else effect-pool or itembox matrix (bit `0x8000`). Calls `Effect_CreateBillboard`. |
| `0x2B` | `cmd_player_anim_0x2b` | `00431990` | 4 | `[behavior:s16][animParam:u16]`. Sets `attackAnim`, `action_behavior/state` from `(val + 0x200) & 0xFF00`, `animation_frame_id`, `animationId = 8`. |
| `0x2C` | `cmd_item_remove` | `004319e0` | 2 | **Cond.** `[op, itemId]`. Zeroes the slot and calls `rearrange_item_slots`. Returns 0 if the item was absent. |
| `0x2D` | `cmd_got_item` | `00431a20` | 0 | Calls `cmd_room_action` (**the same function as opcode `0x24`**), sets `g_main_state_flags |= 0x400` and toggles `0x800`, returns 0. Consumes the operand bytes via the nested `cmd_room_action`. |
| `0x2E` | `cmd_nop_0x2e` | `00460a70` | 0 | **Dead slot.** Bare `ret`, identical to `0x26`. |
| `0x2F` | `cmd_0x2f` | `00460c00` | 4 | `[op, ch][paramA, paramB]`. `FUN_004805d0`, then stores `paramA`/`paramB` at byte offset `ch*8` in `DAT_00ac98e0` / `DAT_00ac98e4`. |
| `0x30` | `cmd_boundaries_0x30` | `00431a40` | 12 | `[op, listIdx][boundIdx, flagMode][z:u16][w:u16][x:u16][y:u16]`. Boundary record = `boundaries[listIdx][boundIdx]` (stride `0xC`). `flagMode != 0` rewrites bits `0x0F00` of `boundary[5]`. |
| `0x31` | `cmd_0x31` | `004608d0` | 4 | `[op, fieldIdx][value:u16]`. `*(u16*)((u8*)&g_fading_state + fieldIdx*2) = value`. |
| `0x32` | `cmd_skip_4bytes` | `00431b00` | 4 | No-op that advances. |
| `0x33` | `cmd_damage_set` | `004314b0` | 2/4 | `[op, subCmd][param:u16]`. `subCmd`: 0 unequip (2 bytes), 1 set `isBeingAttackedFlag` + reset anim, 3 `flags` SET/OR/XOR, 4 `action_behavior=1, action_state=6` (2 bytes), 5 `directionAngle`, 6 clear `unk_8c` (2 bytes), 7 reset to idle (2 bytes), 8 `healthStatusFlags` SET/OR/XOR, 9 toggle joint flags, 10 set/clear `unk_e0 & 0x40`. |
| `0x34` | `cmd_0x34` | `00431b10` | 8 | `[op][variant][bias][p3][p4][p5][p6][p7]`, all bytes; `bias = byte - 0x80`. `variant` 0 → `FUN_00473b10`, 1 → `FUN_00473d10`, 2 → `FUN_00473d60`. |
| `0x35` | `cmd_0x35` | `00431bf0` | 4 | `[op, table][objIdx, value]`. `table` 0 = `g_omodel_table`, 1 = `g_interactable_table`; writes byte `[0]`. Special-cases stage 3 / room 13 / object 5 → force 0. |
| `0x36` | `cmd_0x36` | `00431c90` | 4 | **Cond.** `[op, objIdx][mode, cmpVal]`. Compares the `u16` at `itembox[objIdx] + 0x86` (the billboard effect handle). |
| `0x37` | `cmd_0x37` | `00460a30` | 4 | `[op, stage][roomIdx, value]`. `g_roomBgmState[stage*32 + roomIdx] = value`. |
| `0x38` | `cmd_0x38` | `00431dc0` | 4 | **Cond.** `[op, invert][mask:u16]`. Tests `g_PlayerDpadHeld & mask`; `invert != 0` negates. |
| `0x39` | `cmd_0x39` | `00431e10` | 2 | `[op, enemyIdx]`. `DAT_00be982a = g_EnemiesList[enemyIdx].behavior_flags`. |
| `0x3A` | `cmd_cut_zone_set` | `00431e50` | 4 | `[op, zoneIdx][toCam, fromCam]`. Rewrites `cam_switch_zones[zoneIdx]` fields at `+2` and `+0`. |
| `0x3B` | `cmd_0x3b` | `00431ea0` | 6 | `[op, sel][a:u16][b:u16]`. `sel < 0x8000` → desk `(sel >> 6) & 0x3F`, else itembox `(sel & 0x7F00) >> 8`. Writes `+0x72` and `+0x76` only when the object is active. |
| `0x3C` | `cmd_0x3c` | `00431f20` | 6 | **Cond.** `[op, pad][targetSpec:u16][maxDist:u16]`. `targetSpec & 0xFF`: 0 = enemy `spec >> 8`, 1 = itembox, 2 = desk. Returns `SquareRoot0(dx²+dz²) <= maxDist` against the player. |
| `0x3D` | `cmd_bullet_0x3d` | `00431770` | 12 | Same layout as `0x2A`. Additionally stashes the type in `DAT_00be982b` and the matrix in `DAT_00bf0a34` for `0x3E`. |
| `0x3E` | `cmd_0x3f` | `00431840` | 2 | `FUN_0047cf80(9, DAT_00be982b, 0, 0, DAT_00bf0a34)` — fires the effect prepared by `0x3D`. |
| `0x3F` | `cmd_player_dir_set` | `00431fd0` | 6 | **Cond.** `[op,pad][minAngle:u16][maxAngle:u16]`. Wrap-aware range test on `directionAngle`. |
| `0x40` | `cmd_lights_0x41` | `00432010` | 16 | `[op, lightIdx]` + seven `s16`. Writes light fields `[0..5]` and `[8]` at `&g_RdtPointer[1].lights + lightIdx*0x2C - 4`. |
| `0x41` | `cmd_0x42` | `00432090` | 4 | `[op, entIdx][value:u16]`. `entIdx == 0` → `g_playerEntity.unk_8e`, else entity `entIdx` at `+0x82`. |
| `0x42` | `cmd_0x43` | `00431870` | 4 | `[op, type][param:u16]`. `FUN_0047cf80(3, type, param, 0, 0)`. |
| `0x43` | `cmd_0x44` | `00460d20` | 4 | `[op, ch][a, b]`. Only acts when `g_BGM_STATE` bit `ch+3` is set. |
| `0x44` | `cmd_0x45` | `00461080` | 2 | `[op, slot]`. `g_ScdEventTable[slot].active = 0`. |
| `0x45` | `cmd_0x46` | `004320f0` | 2 | `[op, delta]`. `g_playerEntity.unk_8e += (s8)delta`. |
| `0x46` | `cmd_light_set_0x47` | `00432110` | 44 | `[op,pad]` then 3 × 12-byte light records `[x:s16][y:s16][z:s16][r][g][b][zero2:u8][radius:s16]` written into `RDT.lights[0..2]` (`+0x00/04/08` as ints, `+0x0C/0D/0E` bytes, word at `+0x10`, `radius` at `+0x12`), then 3 × `s16` into `RDT+6/8/10`. Ends with `setBackColor(RDT+6, RDT+8, RDT+10)`. |
| `0x47` | `cmd_0x48` | `00431080` | 14 | `[op, objIdx]` + six `s16`: rotation `+0x72/+0x74/+0x76` and position `+0x6C/+0x6E/+0x70` (mirrored into `+0x34/+0x38/+0x3C`) of `g_omodel_table[objIdx]`. |
| `0x48` | `cmd_0x49` | `004318a0` | 2 | Clears `animId`/`updateId` on all 64 effect-pool slots. |
| `0x49` | `cmd_0x4a` | `00432290` | 2 | `[op, bit]`. `bit == 0xFF` clears `DAT_00d22770`, else sets bit `bit & 0x1F`. |
| `0x4A` | `cmd_snd_set0x4b` | `00460ae0` | 2 | No-op unless `g_targetBgmState != 0xFF`. Restores the three sound channels saved by `0x4B`: `g_BGM_STATE >>= 8`, then `SetSndSlot` per set bit (`0x08` / `0x10` / `0x20`). |
| `0x4B` | `cmd_0x4c` | `00460b80` | 2 | No-op unless `g_targetBgmState != 0xFF`. Stops all four sound banks, then `g_BGM_STATE <<= 8` to save the live channel mask into the high byte. |
| `0x4C` | `cmd_0x4d` | `004322d0` | 4 | `[op, mode][slotIdx, fieldIdx]`. 0 = event-entry byte → `(&g_stageId)[fieldIdx]`; 1 = the reverse; 2 = look the entry's item up in the inventory and copy the quantity into both. |
| `0x4D` | `cmd_0x4e` | `004323a0` | 2 | Resets joints 0-13 of `g_playerEntity.jointsStructs` (stride `0x7c`) to tint `0x606060` via `JointApplyColorTint`. The base is the **pointer** at entity `+0x98`, not the entity address. |
| `0x4E` | `cmd_0x4f` | `00431910` | 4 | `[op, mode][mask:u16]`. OR / AND-NOT / XOR `mask` into `+2` of every live effect-pool slot. |
| `0x4F` | `cmd_0x50` | `004622b0` | 2 | `[op, param]`. `FUN_0040c560(param)`. |
| `0x50` | `cmd_0x51` | `004622e0` | 2 | **Cond.** Returns `DAT_004d6444`. |

### `room_check_actions` (`0x004b9340`)

18 entries, indices `0x00`–`0x11`, followed by two NULL slots. Each takes a
pointer to a 12-byte `g_RoomItemEventTable` entry.

```
0x00 0041c050   0x01 0041b400   0x02 0041b630   0x03 0041b650   0x04 0041b6a0
0x05 0041b6d0   0x06 0041b790   0x07 0041b850   0x08 0041b990   0x09 0041b9e0
0x0A 0041ba00   0x0B 0041ba10   0x0C 0041baa0   0x0D 0041bae0   0x0E 0041bb10
0x0F 0041be70   0x10 0041bed0   0x11 0041bf90
```

### Room item event entry (12 bytes, `g_RoomItemEventTable` at `0x00d91aa0`, 24 slots)

```
+0x00  u8   type / visibility flags
+0x01  u8   sub-type
+0x02  u16  id or flag word
+0x04  u16  parameter
+0x06  u16  flag bit index
+0x08  u32  pointer to the originating SCD record (opcodes + 2)
```

`g_RoomItemEventHead` (`0x00d91bc0`) tracks the highest entry written so far.

---

## 5. `g_ScdOpcodes` pointer-width hazard

In the original binary Ghidra types `g_ScdOpcodes` as `ushort*` in most command
functions and as `byte*`/`short*`/`uint*` in others, per function. That matters
for two things:

- `g_ScdOpcodes + 1` means **+2 bytes** when the local type is `ushort*`
  (and +4 for `uint*`, +2 for `short*`).
- `*g_ScdOpcodes` reads a **16-bit word**, so `*g_ScdOpcodes >> 8` is the second
  byte of the instruction and `*g_ScdOpcodes & 0xFF00` is a real test.

The decomp declares a single `unsigned char* g_ScdOpcodes`. Each ported function
must therefore convert `p++` → `p += 2` (or `+= 4`) and `*p` →
`*(unsigned short*)p` to match. Where that conversion was missed the command
reads the wrong operand or advances by half the instruction length, desynchronising
the whole rest of the block. See §6.

### Ghidra index idiom

Ghidra renders "extract the high byte and scale it" as a masked shift. Read it as:

```
(word & ~((1 << N) - 1 ... )) >> N   ==   (word >> 8) << (8 - N)
```

| Ghidra form | Meaning | Used by |
|---|---|---|
| `(x & 0xffffff07) >> 3` | `(x >> 8) * 32` — byte offset | `cmd_0x37` (`g_roomBgmState`, 32 rooms/stage) |
| `(x & 0xffffff1f) >> 5` | `(x >> 8) * 8` — byte offset | `cmd_bgm_0x15`, `cmd_volume_set`, `cmd_0x2f` (8-byte sound channel records) |
| `(x >> 6) & 0xfffffffc` | `(x >> 8) * 4` — byte offset | `cmd_0x36`, `cmd_0x3c`, `cmd_boundaries_0x30` (pointer tables) |
| `(x >> 7) & 0xfffffffe` | `(x >> 8) * 2` — byte offset | `cmd_0x31` (`u16` array) |

These are **byte offsets added to a base address**, not element indices. Writing
them as `array[idx]` in C double-scales the offset.

---

## 6. Known gaps (as of 2026-07-28)

`run_command_functions` itself is a byte-exact match to `0x00473f60`, and
`script_command_funcs_table` matches all 81 entries of `0x004c1110`. All 81
command functions have real bodies — none is a stub. The remaining problems are
downstream.

### 6a. Placeholder stubs that shadow real implementations — FIXED

`GameState.cpp` held leftover empty definitions that won at link time over the
real code (the "stub overload trap"):

| Symbol | Real implementation | Effect of the stub |
|---|---|---|
| `ScdEventEntry_Create` | `0x0041d650` in [RoomEvents.cpp](../src/game/RoomEvents.cpp#L27) | Opcode `0x14` silently did nothing. |
| `cmd_room_action` | `0x004312b0` — **is** opcode `0x24`'s handler in [CmdFunctions.cpp](../src/game/CmdFunctions.cpp#L1166) | Opcode `0x2D` (got-item) never dispatched its room action. |

Resolved: `ScdEventEntry_Create` is no longer `static`, `cmd_room_action_impl`
was renamed back to `cmd_room_action`, and both placeholders were deleted.

Note the incorrect address comments that hid this — `0x00487590` and
`0x0046a250` are both mid-body of unrelated functions. When a placeholder's
address does not resolve to a function entry point in Ghidra, the symbol is
probably a duplicate of something already implemented.

### 6b. Truncated data tables

| Symbol | Was declared | Original | Status |
|---|---|---|---|
| `room_check_actions` | `{ nullptr }` (1 entry), commented `0x004c1420` | 20 slots (18 handlers + 2 NULL) at `0x004b9340` | **Sized**; entries still `nullptr`, guarded in `cmd_room_action` |
| `g_ScdAnimRemap` (was `DAT_004bec80`) | `{ 0 }` (1 byte) | **32** bytes at `0x004bec80` | **FIXED** — filled and renamed |
| `g_ScdBranchStack` (was `DAT_00bf0808`) | `unsigned int[8]` | 16 dwords (`0x00bf0808`–`0x00bf0847`) | **FIXED** — resized and renamed |

`g_ScdAnimRemap` is 32 bytes, not 20: the consumer guards on `animationId > 0x0F`,
so indices `0`–`31` are reachable, and pairs 10-15 are genuinely zero in the
original. Pointer data begins at `0x004beca0`, which fixes the upper bound.

`g_ScdBranchStack` at `0x00bf0808` runs up to `g_pScdEventCurrent` (`0x00bf0848`)
with no other global in between → 64 bytes → 16 dwords. Both addresses are
outside the `.gwipe` / bio-card / `.sched` ranges, so no ordered-section
placement is required.

`room_check_actions` is deliberately left as null pointers rather than filled
with empty placeholder functions: a placeholder carrying the real name would
re-create the §6a trap the moment the real handler lands. `cmd_room_action` now
bounds-checks and null-checks the index, so an unimplemented action is a no-op
instead of a wild jump.

Handlers still to decompile (`0x0041b400`–`0x0041c050`):

```
0x00 0041c050  no_room_action        0x01 0041b400  use_mansion_key
0x02 0041b630  display_msg_0041b630  0x03 0041b650  include_key
0x04 0041b6a0  set_key_flag          0x05 0041b6d0  check_door
0x06 0041b790  FUN_0041b790          0x07 0041b850  FUN_0041b850
0x08 0041b990  open_itembox          0x09 0041b9e0  FUN_0041b9e0
0x0A 0041ba00  FUN_0041ba00          0x0B 0041ba10  FUN_0041ba10
0x0C 0041baa0  FUN_0041baa0          0x0D 0041bae0  FUN_0041bae0
0x0E 0041bb10  check_desk            0x0F 0041be70  (not yet analyzed)
0x10 0041bed0  (not yet analyzed)    0x11 0041bf90  (not yet analyzed)
```

Indices `0x0F`–`0x11` are not yet defined as functions in the Ghidra project and
need `create_function` before they can be decompiled.

### 6c. Operand-decoding defects (confirmed against the original)

| Op | Function | Defect | Status |
|---|---|---|---|
| `0x07` | `cmd_obj07_test` | Read `cmpVal` at `+2` instead of `+4` (`scd_read_u16(-4)` should be `-2`). | **FIXED** |
| `0x14` | `cmd_0x14` | Byte-dereferenced a word operand → `scriptIndex` always 0. MSVC flags this class as `C4333`. | **FIXED** |
| `0x20` | `cmd_player_pos_set` | Masked `~8` instead of `0xFFF3` — cleared only bit 3 of `unk_e0`, not bits 2 and 3. Found while diffing `0x21`; was not on the original list. | **FIXED** |
| `0x21` | `cmd_enemy_pos_set` | Advanced 7 bytes instead of 14; `enemyIdx` read from `+2` instead of `+0` (and unsigned, not `SAR`); operand offsets wrong from the third field on; masked `~8` into `pad_ec[0x70]` (entity offset `0x15C`) when the target is `scd_entity_flags` at `0xE0` with mask `0xFFF3`. | **FIXED** |
| `0x25` | `cmd_rdt_0x25` | `*g_ScdOpcodes & 0xff00` on a `u8*` is always 0 → always took the SetActive branch. | **FIXED** |
| `0x31` | `cmd_0x31` | Treated a byte offset as an element index → wrote at double the intended offset. | **FIXED** |
| `0x37` | `cmd_0x37` | `(op1 & 0x07) >> 3` is always 0 → every stage wrote `g_roomBgmState[0..31]`. | **FIXED** |
| `0x0B` | `cmd_message_set` | Advanced 3 bytes instead of 4, and read the `pause` operand as a byte when the original reads a word (`MOV CX,word ptr [EAX]`). Left the stream one byte short for everything after it. | **FIXED** |
| `0x17` | `cmd_player_pos_0x17` | Three separate wrong sound positions: case 0 built a local `SVECTOR` of shorts instead of writing the global `g_playerPosScratch` (`0x00be11b0`) as three ints; cases 1 and 2 passed `&…position` (`+0x6C`) where the original passes `&…scaMatrixData.localMatrix.t` (`+0x34`). | **FIXED** |
| `0x46` | `cmd_light_set_0x47` | Advanced 4 at the header instead of 2 (46 vs 44 bytes total), and wrote `radius` at light offset `+0x14` instead of `+0x12` — landing in the **next** `RDT_Light`'s `pos_x`, and past `lights[2]` on the final iteration. | **FIXED** |
| `0x48` | `cmd_0x49` | Advanced 4 bytes instead of 2. | **FIXED** |
| `0x4D` | `cmd_0x4e` | Tinted offsets from `&g_playerEntity` instead of from the pointer at entity `+0x98` (`jointsStructs`) — so it modified unrelated entity fields and never touched the joints. | **FIXED** |
| `0x4A` `0x4B` | `cmd_snd_set0x4b` `cmd_0x4c` | See §6c-bis — width, duplicate global, and instruction length. | **FIXED** |
| `0x15` | `cmd_bgm_0x15` | `(val & 0x1f) >> 5` is always 0; needed the `SndBankSlot` record to reach `slot` at `+5`. | **FIXED** (§6f) |
| `0x16` | `cmd_volume_set` | Same as `0x15`. | **FIXED** (§6f) |
| `0x2F` | `cmd_0x2f` | Same masked-shift error as `0x15`; needed the overlapping pan/volume arrays unified. | **FIXED** (§6f) |

### Not a defect: opcodes `0x26` and `0x2E` hang, and that is faithful

Both are a bare `RET` in the original. The dispatcher leaves the opcode value in
`EAX` at the call site:

```
00473f9b  XOR EAX,EAX
00473f9d  MOV AL,byte ptr [ECX]          ; EAX = opcode
00473f9f  CALL dword ptr [EAX*0x4 + 0x4c1110]
```

so a handler that never writes `EAX` returns the opcode number — `0x26` / `0x2E`,
both non-zero — while consuming no opcode bytes. `run_command_functions` loops
`do { r = f(); } while (r != 0)`, so **the original spins forever** on either
opcode. They are dead table slots that no shipped script can contain.

By contrast `cmd_nop` (`0x00`) explicitly does `XOR EAX,EAX` before `RET`, which
is what makes it a real block terminator.

The port now returns `*g_ScdOpcodes`, reproducing the original exactly. An earlier
revision of this document listed these as defects to fix by returning 0 — that was
wrong: returning 0 would end the block and silently diverge. Do not change them.

This also means: for any command function in the original that falls through
without setting `EAX`, the return value is the opcode number, i.e. "continue".

### 6c-ter. Event VM state-1 defect: opcode `0x81` target index

Diffed while verifying `0x89`. In target mode (`lookAtFlags == 0x93`) the
original reads the target index from `puVar6[2]` — byte offset `+4` of the
instruction:

```c
puVar2 = puVar6 + 2;              // byte +4
switch (*puVar1) { ... g_EnemiesList + (short)*puVar2 ... }
```

[RoomEvents.cpp](../src/game/RoomEvents.cpp#L326) instead reads `params[0]` where
`params` is taken from `scriptPtr` *after* it has already advanced 8 bytes — byte
offset `+10`. Every `0x93` target resolves against the wrong index.

Two further discrepancies in the same handler, not yet resolved:

- The original assigns `&g_playerEntityPointer` (the address of the *pointer*
  global) to `scd_target_ptr` for target type 0; the port assigns
  `&g_playerEntity` (the entity itself). One of the two is wrong — needs the
  `g_playerEntityPointer` layout checked before changing.
- The index is sign-extended (`(short)*puVar2`) in the original; the port uses an
  unsigned read.

### 6c-bis. `g_BGM_STATE` width and the opcodes `0x4A` / `0x4B` — FIXED

`g_BGM_STATE` was `unsigned char`, but `g_dwBgmState` at `0x00d226a0` is 32-bit.
Three separate defects compounded here; opcodes `0x4A` and `0x4B` were complete
no-ops as a result.

**1. Width.** `0x4B` saves the live channel mask by shifting it into the high
byte and `0x4A` shifts it back:

```
00460bec  SHL dword ptr [0x00d226a0],0x8      ; cmd_0x4c   (0x4B)
00460af4  SHR dword ptr [0x00d226a0],0x8      ; cmd_snd_set0x4b (0x4A)
```

On a byte both are identically zero — the save discarded the mask and the
restore wiped the state. `0x15` / `0x16` / `0x43` also set bit `channel + 3`,
which needs bit 8+ for channel 5 and up. Now `unsigned int`.

The reset value is `0xFF`, not `0xFFFFFFFF` — `sounds_reset` does
`MOV dword ptr [0x00d226a0],0xff` — so every existing `!= 0xFF` comparison stays
correct. The `(unsigned char)` casts already present in `SoundSystem.cpp` are
faithful to the original, which genuinely mixes widths: `update_room_bgm` tests
`(byte)BGM_STATE != 0xff` but masks `(BGM_STATE & 0x38)` as a dword, and ends
with `BGM_STATE = (uint)g_bTargetBgmState`, zeroing the saved high byte. Do not
"clean up" those casts.

**2. Duplicate global at `0x00bf07f0`.** An `int DAT_00bf07f0 = -1` was declared
for the same address as `g_targetBgmState` (`unsigned char`). Separate C storage,
so nothing ever wrote it, and the `(int)DAT_00bf07f0 != -1` guard in both
commands was permanently false. The original guard is a byte test against the
value `update_room_bgm` writes:

```
00460ae7  CMP byte ptr [0x00bf07f0],0xff
```

Both commands now use `g_targetBgmState != 0xFF`; the duplicate is deleted.
`g_targetBgmState` and `g_prevBgmState` are correctly `unsigned char` — they are
adjacent bytes (`0x00bf07f0` / `0x00bf07f1`) and the original accesses both with
`byte ptr`, so neither needed widening.

**3. Instruction length.** Both commands are **2 bytes**, not 4 — the first
instruction of each is `ADD dword ptr [0x00bf0800],0x2`. The 4-byte advance was
desynchronising the rest of the block. Fixed; the opcode table in §4 reflects the
corrected lengths.

### 6f. One memory region modeled as several overlapping C globals — FIXED

Opcodes `0x15`, `0x16` and `0x2F` index a table by a byte offset with stride 8,
and could not be fixed as written because the region each one indexes was declared
in the port as **multiple independent C objects** rather than one array — no
contiguous storage for the offset to walk.

**Sound channel records at `0x00ac99d0`** (`0x15`, `0x16`, `0x4A`, `0x4B`):

```
00460a9c  MOV  ECX,dword ptr [EAX + 0xac99d0]     ; handle, EAX = (op >> 8) * 8
00460aa6  MOVSX EAX,byte ptr [EAX + 0xac99d5]     ; slot, sign-extended
```

The port declared `g_SndBank[64]` *and* five separate named globals for addresses
inside its range (`g_snd_slot_00ac99d5`, `g_snd_bank_00ac99d8`,
`g_snd_slot_00ac99dd`, `g_snd_bank_00ac99e0`, `g_snd_slot_00ac99e5`). Being
distinct C objects, the linker placed them independently, so a write through one
name was invisible through every other.

Now one `SndBankSlot g_SndBank[3]` (see `Types.h`). The record and the count are
established independently by four functions — `sounds_reset`, `PauseSounds`,
`ResumePausedSounds`, `bgm_fade_out_all` — which all walk with stride 8 and stop
at `0x00ac99e8`:

```c
struct SndBankSlot {      // 8 bytes
    int           handle;    // +0  createSndBank handle; 0 = empty
    unsigned char field_04;  // +4  cleared by sounds_reset; purpose unknown
    signed char   slot;      // +5  passed to SetSndSlot / playSnd
    unsigned char paused;    // +6  PauseSounds sets, ResumePausedSounds clears
    unsigned char pad_07;    // +7
};
```

**Screen-effect / pan-volume pairs at `0x00ac98e0`** (`0x2F`). `DAT_00ac98e0[4]`
and `DAT_00ac98e4[4]` were two arrays whose ranges overlapped
(`0x00ac98e0 + 4 == 0x00ac98e4`). Now `SndPanVol g_SndPanVol[3]` — three 8-byte
`{pan, volume}` records, bound `0x00ac98f8` (confirmed by `BuildSndFadeTbl`
writing `0x00ac98f8` immediately after). `FUN_004805d0` is renamed
`snd_set_channel_pan_volume`; it takes the same channel index.

Stride/bound errors this exposed and fixed along the way:

| Site | Was |
|---|---|
| `sounds_reset` | `i += 3` over 64 ints; cleared `[i+1]`/`[i+2]` as ints instead of the `+4`/`+5`/`+6` bytes |
| `PauseSounds` | wrote the paused flag at int index `i+2` (byte `+8`, the *next* record) instead of `+6` |
| `ResumePausedSounds` | read `slot` at int index `i+1` (byte `+4`) instead of `+5` |
| `UpdateSoundFadeState` ×2 | `i += 3` stride |
| `UpdateSoundDecay` | `g_SndBank[idx * 3]` — the original is `[EAX*0x8 + base]`, a record index |
| `bgm_fade_out_all` ×3 | `i += 2` over 64 ints — 32 iterations instead of 3 |
| `bgm_load_and_start` | cleared three bytes starting at `+4` via `&bankPtr[1]` casts |
| `UpdateSoundFade` (MarniSound) | `p < g_SndBank + 48` on an `int*` — 24 records instead of 3 |

### 6g. The other four sound-bank arrays have the same record layout

Found while doing §6f, **not yet acted on**. `sounds_reset` (`0x0047ea90`) walks
five arrays with identical per-record clearing (`handle`, `+4`, `+5`), which means
they all share `SndBankSlot`:

| Array | Base | Count | End | `play_sfx` guard |
|---|---|---|---|---|
| `g_RoomSfxBanks` | `0x00ac9910` | 2 | `0x00ac9920` | `id <= 1` ✓ |
| `g_CharacterSfxBanks` | `0x00ac9950` | **16** | `0x00ac99d0` | `id <= 0x0F` ✓ |
| `g_SndBank` | `0x00ac99d0` | 3 | `0x00ac99e8` | — (BGM) |
| `g_emSndBanks` | `0x00ac99f0` | 48 | `0x00ac9b70` | `id <= 0x2F` ✓ |
| `g_SfxBanks` | `0x00ac9b80` | 16 | `0x00ac9c00` | `id <= 0x1F`, then `-= 0x10` ✓ |

Each count is cross-checked against `play_sfx`'s own bounds check, which is
independent of the reset loops. Only `g_SndBank` gets its `paused` byte cleared.
All four non-BGM arrays are still declared `int[64]` in the port and walked with
ad-hoc strides (`g_SfxBanks + 32`, `g_CharacterSfxBanks + 48`, `i += 2` over 64 …),
so they carry the class of bug §6f just fixed for `g_SndBank`.

#### `g_CharacterSfxBanks` is 16 records, and the 9-record bound is an original bug

The earlier "9 entries" reading came from trusting a reset loop's bound. The
**writer** settles it — `load_character_sfx` (`0x0047f070`) is the only function
that populates the array:

```c
piVar7 = piVar7 + 2;                     // stride 8
if ((int *)0xac99cf < piVar7) return;    // last record starts at 0xac99c8
```

That covers records 0-15, spanning `0x00ac9950`–`0x00ac99cf` — ending exactly where
`g_SndBank` begins. Three facts agree: the writer's bound, `play_sfx`'s
`id <= 0x0F` guard, and the `0x80`-byte address gap to the next global.

Three walkers nevertheless stop at `0xac9998`, covering only the first **9** of 16:

```
sounds_reset          0047eaa8   CMP ESI,0xac9998
DestroyAllSoundBanks  00480249   CMP ESI,0xac9998
UpdateSoundFade       004803d1   CMP EDI,0xac9998
```

So in the original, character SFX records 9-15 are loaded but never reset, never
destroyed (a sound-bank handle leak) and never volume-faded. All three share the
same wrong constant, consistent with a copy-pasted loop.

**Decision needed when §6g is implemented.** Writing these three loops faithfully
(bound 9) reproduces the leak; writing them at 16 fixes a bug Capcom shipped and
diverges from the original. Nothing else in the engine depends on the leak, and
`load_character_sfx` destroys each slot itself before reloading it, so the practical
impact is bounded. Default to **faithful (9)** per the project's
preserve-the-original rule, with this note as the justification — but it is the
user's call, not a silent one.

The port currently clears 32 records in `sounds_reset` (`i += 2` over `int[64]`),
which is neither 9 nor 16 — so it is already diverging in the "fixed" direction by
accident. The port's `load_character_sfx` and `play_sfx` bounds are both correct.

This whole family is the same failure mode as the duplicate `DAT_00bf07f0` in
§6c-bis, and it is worth grepping for generally: **two globals whose commented
addresses are less than `sizeof` apart are modeling one original object and will
silently diverge.**

### 6d-status. Leaf dependencies: 18 of 21 implemented

Implemented (2026-07-28): `FUN_0040c560`, `FUN_00473d10`, `FUN_00473d60`,
`FUN_00473e40`, `FUN_00473ea0`, `FUN_00473f10`, `FUN_0047cf80`, `FUN_004804a0`,
`FUN_004805d0`, `FUN_004870d0`, `FUN_0048a190`, `FUN_0048bfe0`, `FUN_0048c020`,
`FUN_0048f330`, `BuildSndFadeTbl`, `get_item_slot`, plus `Effect_CreateBillboard`
un-shadowed (see below). That clears the callee side of opcodes `0x0F 0x18 0x1F
0x27 0x2A 0x2C 0x2F 0x34 0x3D 0x3E 0x42 0x43 0x4C 0x4D 0x4F`.

#### `Effect_CreateBillboard` was never actually missing — §6a again, by overload

`GameState.cpp` held an empty placeholder whose parameter list differed from the
real implementation in `PlayerAnimations.cpp:896`:

```
real:  (u8 type, u8 depthGroup, short  yaw,   void*   spriteInfo, void* pos, char lightFactor)
stub:  (u8 type, u8 param,      ushort flags, MATRIX* spriteInfo, int*  pos, char mode)
```

Different types make this a C++ **overload**, not a duplicate definition, so it
linked cleanly and silently split the callers by argument type. Everything passing
`MATRIX*`/`int*` — SCD opcodes `0x18`, `0x2A`, `0x3D` — resolved to the empty stub,
while `Zombie.cpp` and `PlayerAnimations.cpp` (passing `void*`) reached the real
function. Script-driven effect and bullet spawning therefore did nothing at all.

This is the third instance of the trap, and the nastiest: the previous two
(§6a) were plain duplicate definitions that a grep for the symbol name would
reveal. Here both declarations are legitimate C++ and the symbol *is* implemented
— only the overload set is wrong. **When auditing for this, compare parameter
lists, not just symbol presence**, and be suspicious of any `extern` re-declaration
of a function that `Globals.h` already declares.

#### Other findings while implementing

- `FUN_0048f330` is `restore_saved_enemy_state`: it searches a 16-entry table of
  `SavedEnemyState` (0x1C bytes) at `0x00be92cc` for a slot matching the current
  room and enemy type, restores position/angle into `ENTITY`, and consumes the
  slot. Opcode `0x1B` uses a hit to skip its own spawn init, so enemies keep their
  position across room re-entry. The table is **inside the `.gwipe` wipe block**
  and needed a `.gwipe$92cc` ordered section; verified with `dumpbin /SYMBOLS`
  (`0x1C0` bytes, ordered between `6464` and `9614`, `g_BioCard` still last).
- `FUN_0048c020` consumes the two buffers `FUN_0048bfe0` allocates at entity
  `+0xB0`/`+0xB4` — the pair implements opcode `0x0F`'s weapon-joint clone.
- `FUN_0047cf80` frees effect slots by a criteria **mask**; only the bits set in
  the mask are compared, and all of them must match. Its Ghidra decompile is
  badly mangled by bad pointer typing — it was read from disassembly instead.

Corrections made while implementing:

- `get_item_slot` — the commented address `0x0047ee20` is inside `LoadSoundBank`;
  the real function is `0x004516a0`. It also sets a global the port did not model
  (`g_pCurrentItemSlot`, `0x00d226f0`) to the matched slot, or to
  `&g_defaultItemSlot` on a miss.
- `BuildSndFadeTbl` — the stub's parameters were named `(fadeType, maxVol)` but
  positionally they are `(distSteps, fadeType)`. Real address `0x0047ff90`, not
  the commented `0x0047b410`.
- `FUN_0048a190` / opcode `0x4D` — `JointSetColorTint` (`0x00485ac0`) reads only
  **two** arguments (verified by disassembly: arg2 from `[ESP+8]` at entry, arg1
  from `[ESP+0x20]`, no reference to arg3/arg4 anywhere in the body). The original
  pushes four and cleans `0x10`, so the third and fourth are dead. The tint
  actually applied is the second argument, `0x30` — **not** the `0x00606060` that
  is also pushed. The port's "reset to medium grey" comment was wrong.
- `FUN_004804a0` bounds-checks its channel index (`param_2 < 3`, signed) before
  touching `g_SndBank`; `FUN_004805d0` does not.
- Both `FUN_004804a0` and `BuildSndFadeTbl` divide without a zero check, exactly
  as the original. Reproduced faithfully; a script passing 0 would fault.

Still stubbed, with the reason:

| Function | Blocker |
|---|---|
| `FUN_00484d90`, `FUN_00484e40` | Each sets 3 globals the port does not declare, then `ExecAsync`es a callback (`FUN_00484c40` / `LAB_00484dc0`) that does not exist in the port at all. Two more functions behind each. |
| `play_sound_and_voice_effect` | Needs `FUN_004753c0`, `FUN_00475640`, `FUN_004756b0` — three unimplemented functions. |
| `FUN_00473b10` (503 b) | Needs `FUN_00485c60` (820 b) and `FUN_00485fa0` (488 b), neither implemented — ~1.8 KB of work behind it. `FUN_004870a0` (37 b) is the only small one. Despite being the sibling of `FUN_00473d10`/`d60`, it is the *most* blocked of the set, not the least. |

### 6d. Stubbed leaf dependencies (original survey)

Ranked by original body size (fastest first). Addresses corrected where the
existing comments were wrong.

| Function | Real address | Bytes | Used by |
|---|---|---|---|
| `FUN_0040c560` | `0040c560` | 12 | `0x4F` |
| `FUN_00473f10` | `00473f10` | 34 | `0x18` |
| `FUN_004870d0` | `004870d0` | 36 | `0x18` |
| `FUN_00484d90` | `00484d90` | 43 | `0x1F` |
| `FUN_00484e40` | `00484e40` | 43 | `0x1F` |
| `FUN_004805d0` | `004805d0` | 49 | `0x2F` |
| `FUN_0048bfe0` | `0048bfe0` | 55 | `0x0F` |
| `FUN_00473d60` | `00473d60` | 59 | `0x34` |
| `FUN_004804a0` | `004804a0` | 68 | `0x43` |
| `FUN_00473d10` | `00473d10` | 70 | `0x34` |
| `FUN_00473ea0` | `00473ea0` | 79 | `0x18`, `0x1F` |
| `get_item_slot` | `004516a0` | 83 | `0x2C`, `0x4C` |
| `FUN_00473e40` | `00473e40` | 91 | `0x1F` |
| `play_sound_and_voice_effect` | `00475340` | 110 | `0x1E` |
| `BuildSndFadeTbl` | `0047ff90` | 123 | `0x27` |
| `JointApplyColorTint` (`FUN_0048a190`) | `0048a190` | 128 | `0x4D` |
| `FUN_0047cf80` | `0047cf80` | 154 | `0x3E`, `0x42` |
| `FUN_0048c020` | `0048c020` | 172 | `0x0F` |
| `FUN_0048f330` | `0048f330` | 219 | `0x1B` |
| `FUN_00473b10` | `00473b10` | 503 | `0x34` |
| `Effect_CreateBillboard` | `0047be30` | 646 | `0x18`, `0x2A`, `0x3D` |
| `room_check_actions[0..0x11]` | `0041b400`–`0041c050` | 18 functions | `0x24`, `0x2D` |

### 6h. Sweep results — batch 2 (opcodes verified 2026-07-28)

Eight more defects found and fixed. The field-identity class dominates: five of the
eight were writes to the wrong struct member, which no length or width check can
catch.

| Op | Defect |
|---|---|
| `0x3B` | Desk selector used element index `(op1 >> 6) & 0x3F`; the original's byte offset `(op1 >> 6) & ~3` means element `op1 >> 8` — 4x too large. Only the itembox branch masks with `0x7F` (the original's own asymmetry). |
| `0x4E` | Flag word is at effect `+0x0E` (inside `animHeader`), not `+0x02`; the OR/AND/XOR was hitting `type`/`lightFactor`. |
| `0x4C` | Entry `+8` holds a **pointer** to the originating SCD record; the code read/wrote the pointer's own bytes at `+8`/`+9` instead of dereferencing it. |
| `0x2A` | `pAnimHeader + 0x10` is the **address** effect `+0x14` (the array at `+0x04`), not the value of the `animDataBase` field at `+0x7C`. |
| `0x3D` | Same `animDataBase` error; plus positions are **zero**-extended here (unlike `0x2A`, which sign-extends); plus the itembox selector shifts the already-shifted high byte, so it degenerates to index 0. |
| `0x1B` | Three wrong fields: `+0x161` is `pad_160[1]` not `pad_164[0]`; `+0x163` is `death_event_id` not `pad_167`; `+0xD8` is `lookAtFlags` not `death_timer`. |
| `0x18` | Five defects — see below. |

`0x1F` (`cmd_omodel_set`), the largest command, had three defects:

1. **Control flow.** The stage-4 texture-bank overrides have *three* outcomes in the
   original, not two: a bank override falls straight past both the palette block and
   `ProcessTmdAsync`, a non-matching bank jumps to `ProcessTmdAsync` only, and any
   other stage-4 room runs both. The port nested `ProcessTmdAsync` inside the
   non-stage-4 `else`, so **stage 4 never called it at all**, and stage-4 rooms other
   than 4 and 6 also skipped `ClearTmdProcessingFlag`.
2. The stage-1/room-11 transparency fix zeroes palette entry 0 before its scan; that
   write was missing.
3. The enemy-parent case stored the **literal** `0xBE6480`. That is
   `g_EnemiesList[n].scaMatrixData` in the original's address space, but a raw
   absolute address means nothing in the port — it now computes from the symbol. This
   was the last hardcoded entity address in `CmdFunctions.cpp`.

`0x18` (`cmd_item_model_set`) had five:

1. The original **writes back** `word & 1` into the opcode stream at `+0x18`, and
   stores that *masked* value into entry `+2` while continuing to use the unmasked
   value for the effect-type selection. Both the write-back and the mask were absent.
2. `spriteInfo` is selected per parent-type branch (desk matrix / player local
   matrix / itembox matrix); the port always used `deskPtr + 0x20`.
3. A whole missing feature: item types `'R'` (`0x52`) and `'P'` (`0x50`) get their
   256-entry 5551 palette darkened — each 5-bit channel minus 9, clamped at 0, bit 15
   preserved. The loop did not exist in the port.
4. The billboard position is the global scratch `VECTOR` at `0x00be11b0`
   (`g_playerPosScratch`, three **ints**), not a local `SVECTOR` of shorts — the same
   defect class already fixed in `0x17`.
5. The two final visibility writes target the **desk object**'s byte 0, not the
   room-item-event entry's byte 0 (which was already set from `visFlag` earlier).

Also confirmed correct despite looking suspicious: `0x33` writes
`*(u32*)(player + 0x84)`, and `PlayerEntity.animationId` genuinely lives at `0x84`
— unlike `Entity`, where `animationId` is at `0xBD`. The two structs differ at the
same offset, so "which entity type is this?" has to be answered before trusting any
field name.

### 6e. Verification status

**Instruction lengths: all 81 verified — complete.** Every write to
`g_ScdOpcodes` (`0x00bf0800`) in the binary was enumerated in three queries, which
gives the exact advance of every command without reading 81 functions:

```
search_instructions  mnemonic=ADD  operand=[0x00bf0800]     -> 126 hits
search_instructions  mnemonic=INC  operand=[0x00bf0800]     ->  12 hits
search_instructions  mnemonic=MOV  operand="dword ptr [0x00bf0800],"
                                                            ->   4 hits
```

The `MOV` result is the useful negative: only `run_command_functions` and
`room_events_check` ever assign the pointer, so no command computes its own
advance and the ADD/INC totals are exhaustive. Multi-path commands (`0x28` nine
branches, `0x33` ten, `0x17` four, `0x3B` two) were matched branch-by-branch
against the port's per-subcommand advances. Length errors found and fixed: `0x0B`,
`0x21`, `0x46`, `0x48`, `0x4A`, `0x4B`.

**Operand-level verification: 81 of 81 — COMPLETE.** Every command in the table
has been diffed against the original.

Confirmed correct without change (46): `0x00 0x01 0x02 0x03 0x04 0x05 0x06 0x08
0x09 0x0A 0x0C 0x0D 0x0E 0x0F 0x10 0x11 0x12 0x13 0x19 0x1A 0x1C 0x1D 0x1E 0x22
0x23 0x24 0x27 0x28 0x29 0x2B 0x2C 0x2D 0x30 0x32 0x33 0x34 0x35 0x36 0x38 0x39
0x3A 0x3C 0x3E 0x3F 0x40 0x41 0x42 0x43 0x44 0x45 0x47 0x49 0x4F 0x50`.

Diffed and fixed (28): `0x07 0x0B 0x14 0x15 0x16 0x17 0x18 0x1B 0x1F 0x20 0x21
0x25 0x2A 0x2F 0x31 0x37 0x3B 0x3D 0x46 0x48 0x4A 0x4B 0x4C 0x4D 0x4E`.

Diffed and deliberately left faithful (2): `0x26 0x2E` (dead slots that hang).

Final tally: **28 of 81 commands were defective — a 35% rate.** The
dominant class by a wide margin is *wrong struct field*, not wrong offset or width:
writes that land on a plausible neighbouring member. Those survive every mechanical
check (length, access width, operand offset) and are only caught by resolving each
target address back to a named field.

Diff at the **disassembly** level, not the decompiled C. Four defect classes so
far were invisible or misleading in Ghidra's C output:

- instruction length — the C shows `p + 1` whose byte count depends on the
  pointer type Ghidra inferred for that function
- access width — `byte ptr` vs `word ptr` vs `dword ptr` (the §5 hazard)
- struct field identity — `0x21` wrote entity `0x15C` instead of `0xE0`; `0x46`
  wrote light `+0x14` instead of `+0x12`
- base-pointer indirection — `0x4D` and `0x46` load a **pointer** from a global
  (`MOV ESI,dword ptr [addr]`); the C renders this the same way as taking a
  global's address

Also still undiffed: `room_events_check` itself (`0x0041d6a0`), the four
`scd_event_cmd_*` helpers, `scd_event_state2_movement`, and
`scd_event_state3_set_behavior`. `scd_event_state1_anim` has been diffed and is
correct apart from §6c-ter.

Note: `cmd_0x4e` (`0x004323a0`) and `room_check_actions[0x0F..0x11]` were not
defined as functions in the Ghidra project and had to be disassembled by address.
Other table targets may be in the same state.
