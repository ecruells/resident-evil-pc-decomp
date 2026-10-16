#!/usr/bin/env python3
"""Validate PvP survivor reaction blocks in shipped PC enemy EMD files.

Run from the repo root: python tools/verify_zm_damage_models.py <USA directory>
Reads assets only; it never starts the game. Checks both scenario variants,
the survivor skeleton/rotation stride, and every referenced reaction frame.
"""
import argparse
import struct
from pathlib import Path


CREATURES = {
    0x00: ("zombie", 12),
    0x01: ("naked zombie", 12),
    0x11: ("green zombie", 12),
    0x02: ("Cerberus", 3),
    0x03: ("web spinner", 3),
    0x06: ("Hunter", 3),
    0x09: ("Chimera", 6),
    0x10: ("rooftop Tyrant", 7),
}


def child(directory, name):
    return next(p for p in directory.iterdir() if p.name.lower() == name.lower())


def validate(path, expected_slots):
    data = path.read_bytes()
    footer = (len(data) & ~3) - 20
    offsets = [v & ~3 for v in struct.unpack_from("<5I", data, footer)]
    edd, next_emr = offsets[:2]
    _, frame_start, joints, stride = struct.unpack_from("<4H", data)
    assert 0 < frame_start < edd < next_emr <= footer, "invalid damage block boundaries"
    assert joints == 15, f"survivor reaction has {joints} joints"
    assert stride >= 12 + joints * 6 and stride % 4 == 0, "invalid survivor rotation stride"
    slots = (struct.unpack_from("<H", data, edd + 2)[0] & ~3) // 4
    assert slots == expected_slots, f"expected {expected_slots} slots, found {slots}"
    referenced = 0
    for slot in range(slots):
        count, sequence = struct.unpack_from("<2H", data, edd + slot * 4)
        sequence &= ~3
        assert count > 0 and sequence >= slots * 4, f"invalid sequence for slot {slot}"
        assert edd + sequence + count * 4 <= next_emr, f"sequence {slot} overruns EDD"
        for frame in range(count):
            index = struct.unpack_from("<H", data, edd + sequence + frame * 4)[0]
            assert frame_start + (index + 1) * stride <= edd, f"slot {slot} frame {frame} overruns EMR"
        referenced += count
    return data[:next_emr], referenced


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("assets", type=Path, help="USA asset directory")
    args = parser.parse_args()
    enemy_dir = child(args.assets, "enemy")
    failures = 0
    for filename in ("char10.emd", "char11.emd", "char12.emd", "char13.emd", "em1027.emd", "em1028.emd"):
        try:
            data = child(enemy_dir, filename).read_bytes()
            footer = (len(data) & ~3) - 20
            header = struct.unpack_from("<I", data, footer + 4)[0] & ~3
            _, _, joints, stride = struct.unpack_from("<4H", data, header)
            assert joints == 15 and stride >= 102, "player skeleton cannot consume survivor reaction frames"
            print(f"OK {filename}: survivor skeleton has {joints} joints")
        except (AssertionError, OSError, StopIteration, struct.error) as exc:
            failures += 1
            print(f"FAIL {filename}: {exc or 'file missing'}")
    for enemy, (name, slots) in CREATURES.items():
        blocks = []
        for variant in ("10", "11"):
            filename = f"em{variant}{enemy:02x}.emd"
            try:
                block, frames = validate(child(enemy_dir, filename), slots)
                blocks.append(block)
                print(f"OK {filename}: {name}, {slots} reactions, {frames} referenced frames")
            except (AssertionError, OSError, StopIteration, struct.error) as exc:
                failures += 1
                print(f"FAIL {filename}: {exc or 'file missing'}")
        if len(blocks) == 2:
            print(f"  Chris/Jill damage blocks: {'identical' if blocks[0] == blocks[1] else 'different'}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
