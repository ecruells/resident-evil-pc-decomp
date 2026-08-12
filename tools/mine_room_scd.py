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
    0x1b: ('em_set', 3, ""),
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

BANKS = {0: "PlayerFlags", 1: "PlayerFlags3", 2: "desks_locks", 3: "RoomEventFlags",
         4: "SysFlags", 5: "main_state_flags", 6: "message_flags",
         7: "roomItemsFlags", 8: "RoomFlags", 9: "DAT_00d213a0"}


def u16(data, off):
    # zero-pad short bodies so detail lines never overrun on odd widths
    if off + 2 > len(data):
        return 0
    return struct.unpack_from("<H", data, off)[0]


def bit_desc(u):
    bank = u >> 8
    off = (u & 0xE0) >> 3
    bit = u & 0x1F
    cond = u >> 8
    return f"bank{bank}({BANKS.get(bank,'?')})+{off} bit{bit} cond{cond}"


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
            b1 = u16(body, 0)
            b2 = u16(body, 2)
            detail = (f"bank={b1 >> 8:#x}({BANKS.get(b1 >> 8, '?')}) "
                      f"off={(b2 & 0xE0) >> 3} bit={b2 & 0x1F} cond={b2 >> 8:#x}")
        elif op == 0x0B:
            detail = f"msg={u16(body, 0) >> 8:#x} pause={u16(body, 2):#x}"
        elif op == 0x0D:
            slot = body[0]
            action = body[9]          # entry[0] = opcode[10] -> body[9]
            flags = body[10]          # entry[1] = opcode[0xb] -> body[10]
            zx, zz, zw, zh = struct.unpack_from("<HHHH", body, 11)
            detail = (f"slot={slot} action={action:#x} flags={flags:#x} "
                      f"zone=({zx},{zz},{zw},{zh}) itemrec@{off+3:#x}")
        elif op == 0x0C:
            door = body[0]
            flags = body[0x18]
            detail = f"door={door} flags={flags:#x}"
        elif op == 0x01:
            # 1-byte body: the jump offset is the byte itself (the u16 read
            # below used to overrun on short bodies)
            detail = f"jump+{body[0]:#x}" if body else "jump?"
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
    args = ap.parse_args()

    data = open(args.rdt, "rb").read()
    if len(data) < 0x94:
        print("too small")
        return
    sprites = data[0]
    cams = data[1]
    print(f"sprites={sprites} cameras={cams} size={len(data):#x}")

    ptr_off = 0x64 if not args.scd2 else 0x68
    rel = u16(data, ptr_off) | (data[ptr_off + 2] << 16) | (data[ptr_off + 3] << 24)
    if rel == 0:
        print("no script pointer")
        return
    print(f"scd ptr offset {ptr_off:#x}: {rel:#x}")

    # scd section: blocks with 16-bit size prefix, terminated by 0
    off = rel
    block_no = 0
    while True:
        size = u16(data, off)
        if size == 0:
            print(f"=== end of script (0 terminator at {off:#x}) ===")
            break
        block_no += 1
        off = decode(data, off + 2, size, f"block {block_no}")
        if off >= len(data):
            break


if __name__ == "__main__":
    main()