#!/usr/bin/env python3
"""Evaluate production infestation seeds without launching the game.

Compiles the actual RDT scanner, extra-spot allocator and assumed-fill code
from ZombieRandom.cpp into a small offline adapter, then solves each layout.
Only Python's standard library and a C++17 compiler are required.
See docs/INFESTATION_ROUTE_EVALUATION.md for the model and commands.
"""
import argparse
from collections import Counter, deque
import hashlib
import json
import os
from pathlib import Path
import re
import random
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
START = (5, 0x06)
EXIT = (5, 0x1B)
ITEM_IDS = (0x33, 0x34, 0x35, 0x36, 0x29, 0x2C, 0x2D, 0x2E)
ITEM_NAMES = ("sword key", "armor key", "shield key", "helmet key",
              "wind crest", "moon crest", "star crest", "sun crest")
BITS = {item: 1 << i for i, item in enumerate(ITEM_IDS)}
# The back area's ways in (ZombieRandom.cpp RND_BIT_*): the battery runs the
# small elevator, the note's pass number opens the keypad door.
ACCESS_IDS = (0x27, 0xF0)
ACCESS_NAMES = {0x1000: "battery", 0x2000: "pass number note"}
BITS.update({0x27: 0x1000, 0xF0: 0x2000})


def between(source, start, end):
    """Fail explicitly if production boundaries change; never use stale code."""
    if source.count(start) != 1:
        raise RuntimeError(f"Production extraction needs one start marker: {start}")
    tail = source.split(start, 1)[1]
    if end not in tail:
        raise RuntimeError(f"Production extraction missing end marker: {end}")
    return tail.split(end, 1)[0]


def adapter_source():
    production = (ROOT / "src/game/mods/ZombieRandom.cpp").read_text()
    mode = (ROOT / "src/game/mods/ZombieMode.cpp").read_text()
    internal = (ROOT / "src/game/mods/ZombieModeInternal.h").read_text()
    types = (ROOT / "src/game/Types.h").read_text()
    globals_ = (ROOT / "src/Globals.h").read_text()
    definitions = []
    for text in (types, globals_, internal):
        for line in text.splitlines():
            if re.match(r"#define (ITEM_\w+|STAGE_MANSION\w*|ROOM_MAIN_HALL|"
                        r"ROOM_ROOFED_PASSAGE|ROOM_TRAP_ROOM|ROOM_LIVING_ROOM|ROOM_STOREROOM|ROOM_ITEM_MODELS|"
                        r"ROOM_LESSON_ROOM|ROOM_MANSION_B1_PASSAGE_1|ROOM_MANSION_KITCHEN|"
                        r"ROOM_ACTION_ENTRIES|ZM_DROPS_PER_ROOM|ZM_DROP_SLOT_FIRST|ZM_SPAWN_CAP_FIXED|"
                        r"ZM_RANDOM_MODELS)\b", line):
                definitions.append(line)
    definitions += ["struct ZmSpawnSpots {" + between(internal, "struct ZmSpawnSpots {", "};") + "};",
                    "#define ZM_PLACE_SPOTS 16",
                    "struct ZmPlaceSpot {" + between(internal, "struct ZmPlaceSpot {", "};") + "};",
                    "struct ZmPlaceSpots {" + between(internal, "struct ZmPlaceSpots {", "};") + "};",
                    "static const signed char kScdCmdWidth[0x51] = {" +
                    between(mode, "static const signed char kScdCmdWidth[0x51] = {", "};") + "};",
                    "int zm_scd_width(unsigned char op) { return op < sizeof(kScdCmdWidth) ? kScdCmdWidth[op] : -1; }"]
    spawn = (ROOT / "src/game/mods/ZombieSpawnSpots.cpp").read_text()
    definitions.append(re.sub(r"^#include[^\n]*\n", "", spawn, flags=re.M))
    generator = "#define RND_STAGE_1F" + between(production, "#define RND_STAGE_1F", "static bool rnd_is_tier3(")
    # Runtime loss checks depend on network inventories and world flags; the
    # offline generator deliberately supplies neither.
    runtime = "unsigned int zm_random_progression_bit" + between(
        production, "unsigned int zm_random_progression_bit", "// Pickups of our own")
    generator = generator.replace(runtime, "")
    generator += "static bool rnd_key_spot_ok(" + between(
        production, "static bool rnd_key_spot_ok(", "// One attempt with the generator")
    generator += "static bool rnd_generate_once(void)" + between(
        production, "static bool rnd_generate_once(void)", "static bool rnd_party(")
    template = (ROOT / "tools/zombie_route_generator.cpp").read_text()
    return template.replace("// @production-definitions", "\n".join(definitions)).replace(
        "// @production-generator", generator)


def build_adapter(directory, compiler=None):
    env = None
    compiler = compiler or ("cl" if os.name == "nt" else "g++")
    if Path(compiler).stem.lower() == "cl" and not shutil.which(compiler):
        vswhere = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
        found = subprocess.check_output([str(vswhere), "-latest", "-products", "*",
                                         "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                                         "-find", "VC/Auxiliary/Build/vcvars32.bat"], text=True).strip()
        if not found:
            raise RuntimeError("Install the Visual Studio x86 C++ tools or pass --compiler.")
        batch = directory / "compiler_env.bat"
        batch.write_text('@echo off\ncall "' + found + '" >nul\nif errorlevel 1 exit /b 1\nset\n')
        output = subprocess.check_output(["cmd", "/d", "/c", str(batch)], text=True)
        env = dict(line.split("=", 1) for line in output.splitlines() if "=" in line and not line.startswith("="))
        search_path = next(value for key, value in env.items() if key.lower() == "path")
        compiler = shutil.which(compiler, path=search_path)
        if compiler is None:
            raise RuntimeError("Visual Studio environment did not provide cl.exe.")
    cpp, exe = directory / "generator.cpp", directory / "generator.exe"
    cpp.write_text(adapter_source())
    command = ([compiler, "/nologo", "/EHsc", "/O2", "/std:c++17", str(cpp), "/Fe" + str(exe)]
               if Path(compiler).stem.lower() == "cl" else
               [compiler, "-m32", "-std=c++17", "-O2", str(cpp), "-o", str(exe)])
    result = subprocess.run(command, cwd=directory, env=env, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError("Adapter compilation failed:\n" + result.stdout + result.stderr)
    return exe


def names(mask):
    return ([name for i, name in enumerate(ITEM_NAMES) if mask & (1 << i)] +
            [name for bit, name in ACCESS_NAMES.items() if mask & bit])


def room_label(room):
    return f"{room[0] + 1}:{room[1]:02X}"


def graph_of(layout):
    adjacency = {}
    for a, b, c, d, requirement, *_ in layout["doors"]:
        adjacency.setdefault((a, b), []).append(((c, d), requirement))
    pickups = {}
    for stage, room, item, _flag, *_ in layout["items"]:
        pickups[stage, room] = pickups.get((stage, room), 0) | BITS[item]
    return adjacency, pickups


def shortest_escape(layout, ignore_keys=False):
    """BFS is exact for unit-cost directed edges and monotone item ownership.

    Entering a room collects its progression for zero cost. Exit is the
    stage-changing door from the storeroom, matching zm_is_last_door.
    No other edges outside the two return-mansion stages are traversed.
    """
    graph, pickups = graph_of(layout)
    initial = (START, pickups.get(START, 0))
    queue = deque([initial])
    parents = {initial: None}
    goal = None
    while queue and goal is None:
        state = queue.popleft()
        room, mask = state
        for destination, required in graph.get(room, []):
            if ignore_keys:
                required &= 0xF0
            if mask & required != required:
                continue
            terminal = room == EXIT and destination[0] != EXIT[0] and mask & 0xF0 == 0xF0
            if not terminal and (destination[0] not in (5, 6) or destination[1] >= 0x1D):
                continue
            owned = mask if terminal else mask | pickups.get(destination, 0)
            following = (destination, owned)
            if following in parents:
                continue
            parents[following] = state
            if terminal:
                goal = following
                break
            queue.append(following)
    if goal is None:
        return None
    states = []
    while goal is not None:
        states.append(goal)
        goal = parents[goal]
    states.reverse()
    visits = Counter(room for room, _ in states[:-1])
    previous = 0
    route = []
    for i, (room, mask) in enumerate(states):
        route.append({"crossing": i, "room": room_label(room), "collect": names(mask & ~previous),
                      "escape": i == len(states) - 1})
        previous = mask
    return {"door_crossings": len(states) - 1, "repeated_room_entries": sum(n - 1 for n in visits.values()),
            "unique_mansion_rooms": len(visits), "keys_collected": names(states[-1][1] & 15),
            "states_explored": len(parents), "route": route}


def progression_spheres(layout):
    graph, pickups = graph_of(layout)
    owned = 0
    spheres = []
    while True:
        reachable = {START}
        queue = deque([START])
        while queue:
            for destination, required in graph.get(queue.popleft(), []):
                if destination[0] in (5, 6) and destination[1] < 0x1D and destination not in reachable and owned & required == required:
                    reachable.add(destination)
                    queue.append(destination)
        gained = 0
        for room in reachable:
            gained |= pickups.get(room, 0)
        gained &= ~owned
        if not gained:
            break
        spheres.append({"sphere": len(spheres) + 1, "new_items": names(gained), "reachable_rooms": len(reachable)})
        owned |= gained
    return spheres


def evaluate(layout):
    if layout.get("unreviewed_gates"):
        # kDoorGates must name every door a script can switch off.
        return {"seed": layout["seed"], "status": "unreviewed_door_gates",
                "unreviewed_gates": layout["unreviewed_gates"], "gated_doors": layout.get("gated_doors", [])}
    if not layout["generated"]:
        return {"seed": layout["seed"], "status": "generation_failed", "attempts": layout["attempts"]}
    result = shortest_escape(layout)
    if result is None:
        proof_layout = {**layout, "items": layout["items"] + layout.get("proof_only_items", [])}
        return {"seed": layout["seed"], "status": "no_escape_in_model",
                "escape_with_removed_original_items": shortest_escape(proof_layout) is not None,
                "progression_spheres": progression_spheres(layout), "layout": layout}
    baseline = shortest_escape(layout, ignore_keys=True)
    return {"seed": layout["seed"], "status": "solved", "attempts": layout["attempts"], **result,
            "keyless_crossings": baseline["door_crossings"],
            "key_gate_extra_crossings": result["door_crossings"] - baseline["door_crossings"],
            "progression_spheres": progression_spheres(layout), "layout": layout}


def summarize(results, top):
    solved = [r for r in results if r["status"] == "solved"]
    costs = sorted(r["door_crossings"] for r in solved)
    histogram = Counter(costs)
    hardest = sorted(solved, key=lambda r: (-r["door_crossings"], r["seed"]))[:top]
    for entry in hardest:
        entry["sample_percentile"] = round(100 * sum(n for cost, n in histogram.items() if cost <= entry["door_crossings"]) / len(costs), 2)
    failures = [r for r in results if r["status"] != "solved"]
    return {"seeds_evaluated": len(results), "solved": len(solved),
            "failures": [{k: v for k, v in r.items() if k != "layout"} for r in failures],
            "failure_examples": failures[:3],
            "crossing_histogram": dict(sorted(histogram.items())),
            "crossing_percentiles": {str(p): costs[round((len(costs) - 1) * p / 100)] for p in (0, 10, 25, 50, 75, 90, 95, 99, 100)} if costs else {},
            "hardest_found": hardest}


def fingerprint_assets(assets):
    digest = hashlib.sha256()
    files = sorted((p for p in assets.rglob("*") if p.is_file() and p.suffix.lower() == ".rdt"
                    and p.parent.name.lower() in ("stage1", "stage6", "stage7")),
                   key=lambda p: p.relative_to(assets).as_posix().lower())
    for path in files:
        digest.update(path.relative_to(assets).as_posix().lower().encode())
        digest.update(path.read_bytes())
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets", type=Path, help="USA tree containing stage1, stage6 and stage7")
    parser.add_argument("--compiler", help="cl (auto-discovers VS) or g++/clang++ with 32-bit support")
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument("--seed", type=lambda s: int(s, 0), action="append", help="Explicit seed; repeatable (decimal or 0xHEX)")
    selection.add_argument("--random-seeds", action="store_true", help="Sample distinct seeds across the unsigned 32-bit range")
    parser.add_argument("--sample-seed", type=int, default=0, help="Reproducible sampling seed for --random-seeds")
    parser.add_argument("--start", type=lambda s: int(s, 0), default=1)
    parser.add_argument("--count", type=int, default=1000, help="Consecutive seeds when --seed is absent")
    parser.add_argument("--top", type=int, default=10)
    parser.add_argument("--output", type=Path, default=ROOT / "tools/zombie_route_report.json")
    args = parser.parse_args()
    seeds = args.seed if args.seed is not None else range(args.start, args.start + args.count)
    if args.count < 1 or args.top < 1 or not seeds or any(seed < 0 or seed > 0xFFFFFFFF for seed in seeds):
        parser.error("Use positive count/top and unsigned 32-bit seeds.")
    if args.random_seeds:
        seeds = random.Random(args.sample_seed).sample(range(1, 0x100000000), args.count)
    if args.assets is None:
        args.assets = next((p for p in (ROOT / "assets/USA", ROOT / "bin/Release/USA", ROOT / "build/linux/USA") if p.is_dir()), None)
    if args.assets is None or not args.assets.is_dir():
        parser.error("Provide --assets pointing to a USA data tree.")
    args.assets = args.assets.resolve()
    results = []
    with tempfile.TemporaryDirectory(prefix="re1-routes-") as temp:
        print("Building offline production generator...", flush=True)
        exe = build_adapter(Path(temp), args.compiler)
        # Batches bound output memory and allow regular progress updates.
        seeds = list(seeds)
        for offset in range(0, len(seeds), 100):
            batch = seeds[offset:offset + 100]
            output = subprocess.check_output([str(exe), str(args.assets)],
                                             input="".join(f"{seed}\n" for seed in batch), text=True)
            layouts = [json.loads(line) for line in output.splitlines()]
            if len(layouts) != len(batch):
                raise RuntimeError("Adapter did not return every requested seed.")
            results.extend(evaluate(layout) for layout in layouts)
            print(f"Evaluated {len(results)}/{len(seeds)} seeds", flush=True)
    gated = [{"door": f"{room_label((a, b))} slot {slot} -> {room_label((c, d))}", "usable": usable}
             for a, b, c, d, slot, usable in (results[0].get("layout") or results[0]).get("gated_doors", [])] if results else []
    report = {"model": "solo room-graph minimum door crossings v2", "absolute_maximum_proven": False,
              "assumptions": ["Full layout knowledge; zero-cost room pickups and crest placement",
                              "Unlimited progression inventory; keys retained; no character perks",
                              "No walking, combat, communication, door-time or collision validation",
                              "Directed production door graph; puzzle/one-way locks open as in randomizer",
                              "Script-gated door triggers follow the reviewed kDoorGates table; puzzle gates closed",
                              "No progression in rooms with unverified furniture (kUnverifiedRooms)"],
              "gated_doors": gated,
              "production_adapter_sha256": hashlib.sha256(adapter_source().encode()).hexdigest(),
              "assets_rdt_sha256": fingerprint_assets(args.assets),
              "seed_selection": {"method": "random sample" if args.random_seeds else "explicit" if args.seed else "consecutive",
                                 "sampling_seed": args.sample_seed if args.random_seeds else None},
              "requested_seeds": seeds, **summarize(results, args.top)}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(f"Report: {args.output}")
    if report["hardest_found"]:
        hardest = report["hardest_found"][0]
        print(f"Hardest found: 0x{hardest['seed']:08X}, {hardest['door_crossings']} crossings "
              f"({hardest['key_gate_extra_crossings']} extra from key gates). Not a proven absolute maximum.")
    print(f"Solved: {report['solved']}; failures: {len(report['failures'])}")
    return 1 if report["failures"] else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
