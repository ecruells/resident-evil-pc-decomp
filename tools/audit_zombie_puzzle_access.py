#!/usr/bin/env python3
"""Inspect mansion puzzle/access commands without running the game.

Static evidence only: scripts are decoded, not executed; condition truth and
collision accessibility are not proved. Run from the repo root:
python tools/audit_zombie_puzzle_access.py --assets bin/Release/USA
"""
import argparse
import json
from pathlib import Path
import re
import struct

from evaluate_zombie_routes import ROOT, between
from evt_disasm import walk


def commands(data, start, end, widths, context):
    q, stack = start, []
    while q < end:
        while stack and q >= stack[-1]:
            stack.pop()
        op = data[q]
        if op >= len(widths) or q + 1 + widths[op] > end:
            raise ValueError(f"Invalid command at {q:#x} in {context}")
        if op == 1:
            stack.append(q + 2 + data[q + 1])
        yield {"offset": q, "context": context, "conditional": bool(stack),
               "op": op, "bytes": list(data[q:q + 1 + widths[op]])}
        q += 1 + widths[op]


def inspect(data, widths):
    records, movement, warnings = [], [], []
    for header, context in ((0x60, "init"), (0x64, "frame")):
        p = struct.unpack_from("<I", data, header)[0]
        for _ in range(256):
            if p + 2 > len(data):
                raise ValueError("Script offset outside RDT")
            size = struct.unpack_from("<H", data, p)[0]
            if not size:
                break
            if size < 2 or p + size > len(data):
                raise ValueError("Invalid script block")
            records.extend(commands(data, p + 2, p + size, widths, context))
            p += size
    table = struct.unpack_from("<I", data, 0x68)[0]
    offsets = []
    for p in range(table, len(data) - 3, 4):
        relative = struct.unpack_from("<I", data, p)[0]
        if not relative:
            break
        if table + relative >= len(data):
            raise ValueError("Event offset outside RDT")
        offsets.append(relative)
        if len(offsets) > 256:
            raise ValueError("Unterminated event table")
    for index, relative in enumerate(offsets):
        limit = min([table + o for o in offsets if o > relative] or [len(data)])
        target = None
        for rel, state, name, raw, note in walk(data, table, table + relative, limit):
            body = bytes.fromhex(raw)
            if state == 0 and name == "set_entity":
                target = (body[1], body[2])
            if state == 2 and target and target[0] == 2 and name in ("s2_set_pos", "s2_step_pos", "s2_step_pos_rot"):
                movement.append({"event": index, "object": target[1], "offset": table + rel, "command": name})
            try:
                if state == 0 and name == "exec_scd":
                    # The VM supplies an explicit length and dispatches one
                    # command. Several effect/tint commands have different
                    # widths from the room scanner's table; do not decode
                    # their remaining operand bytes as more commands.
                    if len(body) < 3:
                        raise ValueError("Truncated single-command event")
                    records.append({"offset": table + rel + 2, "context": f"event {index}",
                                    "conditional": False, "op": body[2], "bytes": list(body[2:])})
                elif state == 0 and name == "run_scd":
                    size = struct.unpack_from("<H", body, 2)[0]
                    records.extend(commands(body, 4, min(len(body), 2 + size), widths, f"event {index}"))
                elif name.startswith("???"):
                    warnings.append({"event": index, "offset": table + rel, "note": note})
            except (ValueError, struct.error) as error:
                warnings.append({"event": index, "offset": table + rel, "note": str(error), "raw": raw})
    doors = []
    for record in records:
        c = record["bytes"]
        if record["op"] == 0x0C and not c[13] & 0x80:
            x, z, w, d = struct.unpack_from("<HHHH", bytes(c), 2)
            doors.append({**record, "slot": c[1] & 127, "destination": c[15], "armed": bool(c[25]),
                          "lock": c[14], "need": c[24], "zone": [x, z, w, d]})
    slots = {d["slot"] for d in doors}
    changes = [r for r in records if r["op"] in (0x12, 0x13) and r["bytes"][1] & 127 in slots]
    # The production scanner's notion of a gated door (ZombieRandom.cpp
    # rnd_read_room): init/frame records built conditionally, left unarmed,
    # with a zone of one unit, or whose slot an init/frame script rewrites.
    rewritten = {r["bytes"][1] & 127 for r in records
                 if r["op"] in (0x12, 0x13) and not r["context"].startswith("event")}
    for door in doors:
        door["gated"] = not door["context"].startswith("event") and (
            door["conditional"] or not door["armed"] or min(door["zone"][2:]) <= 1 or door["slot"] in rewritten)
    # cmd_room_action (0x24) runs a slot's handler directly - how events use
    # unarmed or zero-size door records (the elevator car, the hole prompts).
    invocations = [r for r in records if r["op"] == 0x24 and r["bytes"][1] in slots]
    return {"doors": doors, "door_action_changes": changes, "door_invocations": invocations,
            "object_movement": movement,
            "used_items": sorted({r["bytes"][1] for r in records if r["op"] == 0x10}),
            "boundary_changes": [r for r in records if r["op"] == 0x30],
            "object_flag_changes": [r for r in records if r["op"] == 0x35], "decode_warnings": warnings}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "tools/zombie_puzzle_access_report.json")
    args = parser.parse_args()
    mode = (ROOT / "src/game/mods/ZombieMode.cpp").read_text()
    widths = [int(v) for v in re.findall(r"-?\d+", between(mode, "static const signed char kScdCmdWidth[0x51] = {", "};"))]
    paths = {p.relative_to(args.assets).as_posix().lower(): p for p in args.assets.rglob("*") if p.is_file()}
    rooms, stubs = [], []
    for stage in (5, 6):
        for room in range(0x1D):
            file_stage = 0 if stage == 5 and room in (0x1A, 0x15, 0x16) else stage
            name = f"stage{file_stage + 1}/room{file_stage + 1}{room:02x}0.rdt"
            data = paths[name].read_bytes()
            label = f"{stage + 1}:{room:02X}"
            if len(data) < 0x100:
                stubs.append(label)
                continue
            rooms.append({"room": label, "asset": name, **inspect(data, widths)})
    report = {"scope": "return mansion, Chris scenario; restored first-visit roofed passage and shotgun rooms",
              "limitation": "Static command evidence; not a runtime or collision proof",
              "rooms_scanned": len(rooms), "stub_rooms": stubs, "rooms": rooms}
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(f"Scanned {len(rooms)} rooms; {len(stubs)} stubs. Report: {args.output}")
    for room in rooms:
        unusual = [d for d in room["doors"] if d["conditional"] or not d["armed"] or d["context"].startswith("event")]
        if unusual or room["door_action_changes"] or room["used_items"]:
            print(f"{room['room']}: {len(unusual)} conditional/inactive/event doors, "
                  f"{len(room['door_action_changes'])} door action writes, "
                  f"used items {[f'{i:02X}' for i in room['used_items']]}")
        for door in room["doors"]:
            if door["gated"]:
                invoked = any(r["bytes"][1] == door["slot"] for r in room["door_invocations"])
                print(f"{room['room']}: gated door slot {door['slot']} ({door['context']}), dest {door['destination']:02X}"
                      f"{', invoked by an event' if invoked else ''}: review in kDoorGates")


if __name__ == "__main__":
    main()
