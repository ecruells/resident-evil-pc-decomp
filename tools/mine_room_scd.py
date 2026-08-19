#!/usr/bin/env python3
"""
mine_room_scd.py - dump and decode the per-frame SCD room script from an RDT.

RDT header is 0x94 bytes; scd_opcodes pointer is at offset 0x64 (relative).
Command widths taken from the port's CmdFunctions.cpp. Widths for commands
not yet transcribed are flagged so the stream can be re-synced on 0x0C/0x0D
item/door setup blocks.

Usage: mine_room_scd.py <room.rdt> [--scd2]
"""
import argparse
import struct
import sys

# opcode -> (name, width_in_bytes_after_opcode, comment)
CMDS = {
    0x00: ('nop', 0, ""),
    0x01: ('if', 1, ""),
    0x02: ('else', 1, ""),
    0x03: ('end_if', 1, ""),
    0x04: ('bit_test', 3, ""),
    0x05: ('bit_op', 3, ""),
    0x06: ('obj06_test', 3, ""),
    0x07: ('obj07_test', 5, ""),
    0x08: ('room_cam_set', 3, ""),
    0x09: ('cut_set', 1, ""),
    0x0a: ('current_cut_set', 1, ""),
    0x0b: ('message_set', 3, ""),
    0x0c: ('door_set', 25, ""),
    0x0d: ('item_set', 17, ""),
    0x0e: ('skip_2bytes', 1, ""),
    0x0f: ('entities_0x0f', 7, ""),
    0x10: ('obj10_test', 1, ""),
    0x11: ('obj11_test', 1, ""),
    0x12: ('item_flag_0x12', 9, ""),
    0x13: ('cmd_0x13', 3, ""),
    0x14: ('cmd_0x14', 3, ""),
    0x15: ('bgm_0x15', 1, ""),
    0x16: ('volume_set', 1, ""),
    0x17: ('player_pos_0x17', 9, ""),
    0x18: ('item_model_set', 25, ""),
    0x19: ('obj19_set', 3, ""),
    0x1a: ('item_search', 1, ""),
    0x1b: ('em_set', 21, "22 bytes total - cmd_em_set adds 0x16"),
    0x1c: ('cmd_0x1c', 5, ""),
    0x1d: ('weapon_set', 1, ""),
    0x1e: ('sfx_set', 3, ""),
    0x1f: ('omodel_set', 3, ""),
    0x20: ('player_pos_set', 13, ""),
    0x21: ('enemy_pos_set', 13, ""),
    0x22: ('item_cmd_0x22', 3, ""),
    0x23: ('cut_toggle', 1, ""),
    0x24: ('room_action', 3, ""),
    0x25: ('rdt_0x25', 3, ""),
    0x26: ('nop_0x26', 0, ""),
    0x27: ('snd_fade_set', 1, ""),
    0x28: ('enemy_0x28', 5, ""),
    0x29: ('fmv_set', 1, ""),
    0x2a: ('effect_spawn', 11, ""),
    0x2b: ('player_anim_0x2b', 3, ""),
    0x2c: ('item_remove', 1, ""),
    0x2d: ('got_item', 1, ""),
    0x2e: ('nop_0x2e', 0, ""),
    0x2f: ('cmd_0x2f', 3, ""),
    0x30: ('boundaries_0x30', 11, ""),
    0x31: ('cmd_0x31', 3, ""),
    0x32: ('skip_4bytes', 3, ""),
    0x33: ('damage_set', 1, ""),
    0x34: ('cmd_0x34', 1, ""),
    0x35: ('cmd_0x35', 3, ""),
    0x36: ('cmd_0x36', 3, ""),
    0x37: ('cmd_0x37', 3, ""),
    0x38: ('cmd_0x38', 3, ""),
    0x39: ('cmd_0x39', 1, ""),
    0x3a: ('cut_0x3a', 3, ""),
    0x3b: ('cmd_0x3b', 5, ""),
    0x3c: ('cmd_0x3c', 5, ""),
    0x3d: ('bullet_0x3d', 11, ""),
    0x3e: ('cmd_0x3f', 1, ""),
    0x3f: ('player_dir_set', 5, ""),
    0x40: ('lights_0x41', 15, ""),
    0x41: ('cmd_0x42', 3, ""),
    0x42: ('cmd_0x43', 3, ""),
    0x43: ('cmd_0x44', 3, ""),
    0x44: ('cmd_0x45', 1, ""),
    0x45: ('cmd_0x46', 1, ""),
    0x46: ('light_set_0x47', 1, ""),
    0x47: ('cmd_0x48', 13, ""),
    0x48: ('cmd_0x49', 1, ""),
    0x49: ('cmd_0x4a', 1, ""),
    0x4a: ('snd_set0x4b', 1, ""),
    0x4b: ('cmd_0x4c', 1, ""),
    0x4c: ('cmd_0x4d', 3, ""),
    0x4d: ('cmd_0x4e', 1, ""),
    0x4e: ('cmd_0x4f', 3, ""),
    0x4f: ('cmd_0x50', 1, ""),
    0x50: ('cmd_0x51', 1, ""),
}

BANKS = {0: "PlayerFlags", 1: "PlayerFlags3", 2: "locksFlags", 3: "RoomEventFlags",
         4: "SysFlags", 5: "main_state_flags", 6: "message_flags",
         7: "roomItemsFlags", 8: "RoomFlags", 9: "DAT_00d213a0"}


def u16(data, off):
    # zero-pad short bodies so detail lines never overrun on odd widths
    if off + 2 > len(data):
        return 0
    return struct.unpack_from("<H", data, off)[0]


def decode(data, off, length, label):
    end = off + length
    print(f"=== {label} @ {off:#x} len={length:#x} ===")
    while off < end:
        op = data[off]
        name, width, comment = CMDS.get(op, (f"UNKNOWN_{op:02X}", None, ""))
        if width is None:
            print(f"  {off:#06x}: ?? {op:02X}  <-- unknown width, stream desync from here")
            return off + 1
        body = data[off + 1:off + 1 + width]
        detail = ""
        if op == 0x04 or op == 0x05:
            # cmd_bit_test (0x00460570) / cmd_bit_op (0x00460650) read through
            # g_ScdOpcodes, which points at the OPCODE - so scd_read_u16(0) >> 8
            # is the byte immediately AFTER it. Per byte that is
            #   body[0] = bank, body[1] = offset+bit, body[2] = cond/operation.
            # Reading the pair one byte later (u16(body, 0) >> 8) reported the
            # wrong bank for every flag test in every room.
            #
            # The bit index is counted from the MSB: cmd_bit_op builds its mask
            # as `0x80000000 >> bitIndex` and cmd_bit_test shifts LEFT by it and
            # tests the sign. So raw index 31 is bit 0, index 0 is bit 31.
            # Printing the raw index as "bit=31" is how ROOM1110's mirror-enable
            # (`05 05 1f 00`, which sets bit 0) got read as a write to bit 31.
            # Report the LSB bit number, with the raw index alongside.
            bank, sel, cond = body[0], body[1], body[2]
            raw = sel & 0x1F
            opname = {0: 'set', 1: 'clear', 2: 'toggle'}.get(cond, f'op{cond:#x}')
            detail = (f"bank={bank:#x}({BANKS.get(bank, '?')}) "
                      f"off={(sel & 0xE0) >> 3} bit={31 - raw}(msbidx {raw}) "
                      f"mask={0x80000000 >> raw:#010x} "
                      + (opname if op == 0x05 else f"cond={cond:#x}"))
        elif op == 0x0B:
            detail = f"msg={u16(body, 0) >> 8:#x} pause={u16(body, 2):#x}"
        elif op == 0x0D:
            slot = body[0]
            action = body[9]          # entry[0] = opcode[10] -> body[9]
            flags = body[10]          # entry[1] = opcode[0xb] -> body[10]
            # cmd_item_set (0x00460970) advances 0x12, so the body is 17 bytes
            # and holds only THREE u16 after the two flag bytes - entry+2/+4/+6
            # come from opcode +0xc/+0xe/+0x10. Reading a fourth ran off the end.
            zx, zz, zw = u16(body, 11), u16(body, 13), u16(body, 15)
            detail = (f"slot={slot} action={action:#x} flags={flags:#x} "
                      f"zone=({zx},{zz},{zw}) itemrec@{off+3:#x}")
        elif op == 0x0C:
            door = body[0]
            flags = body[0x18]
            detail = f"door={door} flags={flags:#x}"
        elif op == 0x01:
            # 1-byte body: the jump offset is the byte itself (the u16 read
            # below used to overrun on short bodies)
            detail = f"jump+{body[0]:#x}" if body else "jump?"
        elif op == 0x0F:
            # cmd_entities_0x0f (0x004610b0) - arms the room mirror.
            # body[0] -> g_main_state_flags bits 0-1, then three u16 params.
            # body[0] goes into g_main_state_flags bits 0-1: bit 0 enables the
            # pass, bit 1 picks the plane axis. A plane-X room writes 0x02 here
            # and then enables with a separate bit_op (bank 5, msb index 31), so
            # bit0=0 on this line does NOT mean the mirror is off - check the
            # following commands.
            axis = "X" if (body[0] >> 1) & 1 else "Z"
            detail = (f"MIRROR bit0={body[0] & 1} axis={axis} "
                      f"extent={u16(body, 1)}..{u16(body, 3)} "
                      f"plane={axis}={u16(body, 5)}")
        elif op == 0x02:
            detail = f"jump+{body[0]:#x}"
        hx = " ".join(f"{b:02X}" for b in body)
        print(f"  {off:#06x}: {op:02X} {name:<16} {hx:<56} {detail} {comment}")
        off += 1 + width
    return off


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rdt")
    ap.add_argument("--scd2", action="store_true", help="dump scd_opcodes2 (events) instead")
    ap.add_argument("--init", action="store_true",
                    help="dump initialization_scd (RDT+0x60, runs once at room load)")
    args = ap.parse_args()

    data = open(args.rdt, "rb").read()
    if len(data) < 0x94:
        print("too small")
        return
    sprites = data[0]
    cams = data[1]
    print(f"sprites={sprites} cameras={cams} size={len(data):#x}")

    if args.init:
        ptr_off = 0x60
    elif args.scd2:
        ptr_off = 0x68
    else:
        ptr_off = 0x64
    rel = u16(data, ptr_off) | (data[ptr_off + 2] << 16) | (data[ptr_off + 3] << 24)
    if rel == 0:
        print("no script pointer")
        return
    print(f"scd ptr offset {ptr_off:#x}: {rel:#x}")

    # A script section ends at the next section, not only at a 0 size word.
    # Every dword in the RDT header (0x08..0x90) is a section pointer, so the
    # smallest one greater than `rel` is this section's hard end. Without that
    # bound the block chain runs off into whatever follows and decodes it as
    # opcodes: ROOM1130's init walked 0x3a bytes past the start of its own main
    # script into a dword offset table and reported a bogus desync.
    limit = len(data)
    for h in range(0x08, 0x94, 4):
        q = struct.unpack_from("<I", data, h)[0]
        if rel < q < limit:
            limit = q
    if limit < len(data):
        print(f"section ends at {limit:#x} (next section pointer)")

    # scd section: blocks with 16-bit size prefix, terminated by 0
    off = rel
    block_no = 0
    while off + 2 <= limit:
        size = u16(data, off)
        if size == 0:
            print(f"=== end of script (0 terminator at {off:#x}) ===")
            break
        if off + 2 + size > limit:
            print(f"=== block {block_no + 1} @ {off:#x} claims {size:#x} bytes, "
                  f"past the section end - stopping ===")
            break
        block_no += 1
        off = decode(data, off + 2, size, f"block {block_no}")
        if off >= len(data):
            break


if __name__ == "__main__":
    main()