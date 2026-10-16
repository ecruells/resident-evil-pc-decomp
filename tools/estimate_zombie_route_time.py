#!/usr/bin/env python3
"""Estimate how long the escape takes on production infestation seeds.

Builds the same offline adapter as evaluate_zombie_routes.py, then turns its
room graph into seconds: in each room the runner goes from where it came in
(the door record's arrival position, +0x0E/+0x12) to the next door's zone or
pickup in a straight line, padded by a detour factor, at the player's run
speed (0xD2 units a 33 ms tick - PlayerAnimations.cpp). Every crossing adds a
fixed door time, every pickup a pickup time and the crest door the four crest
insertions. No combat, healing, menus or mistakes: a lower bound for a runner
who knows where everything lies (the survivors' map shows keys and crests).

Solo: a shortest-time search over (position, keys and crests held).
Team: a coordinated party of N - each round, the key items reachable with
what is already held are shared out (longest round trips first) and everyone
meets in the main hall again; then the run to the exit. Only the keys the solo
route needed are fetched.

python tools/estimate_zombie_route_time.py --assets build/linux/USA --random-seeds --count 1000
"""
import argparse
from collections import deque
import heapq
import json
from pathlib import Path
import random
import re
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import evaluate_zombie_routes as routes
import audit_zombie_puzzle_access as audit

START_POS = (17000, 8500)          # the survivors' start triangle, main hall
TICKS_PER_SECOND = 30
RUN_UNITS_PER_TICK = 0xD2


def room_geometry(assets):
    """Per (stage, room): door zone and arrival by slot, item record zones by flag."""
    mode = (routes.ROOT / "src/game/mods/ZombieMode.cpp").read_text()
    widths = [int(v) for v in re.findall(r"-?\d+", routes.between(
        mode, "static const signed char kScdCmdWidth[0x51] = {", "};"))]
    paths = {p.relative_to(assets).as_posix().lower(): p for p in assets.rglob("*") if p.is_file()}
    geometry = {}
    for stage in (5, 6):
        for room in range(0x1D):
            file_stage = 0 if stage == 5 and room in (0x1A, 0x15, 0x16) else stage
            data = paths[f"stage{file_stage + 1}/room{file_stage + 1}{room:02x}0.rdt"].read_bytes()
            if len(data) < 0x100:
                continue
            doors, items = {}, {}
            for header, context in ((0x60, "init"), (0x64, "frame")):
                p = struct.unpack_from("<I", data, header)[0]
                while True:
                    size = struct.unpack_from("<H", data, p)[0]
                    if not size:
                        break
                    for r in audit.commands(data, p + 2, p + size, widths, context):
                        c = bytes(r["bytes"])
                        if r["op"] == 0x0C and not c[13] & 0x80:
                            x, z, w, d = struct.unpack_from("<HHHH", c, 2)
                            ax, _, az = struct.unpack_from("<hhh", c, 0x10)
                            doors.setdefault(c[1] & 0x7F, ((x, z, w, d), (ax, az)))
                        elif r["op"] == 0x18:
                            x, z, w, d = struct.unpack_from("<HHHH", c, 2)
                            items.setdefault(c[0x16], (x + w // 2, z + d // 2))
                    p += size
            geometry[stage, room] = {"doors": doors, "items": items}
    # The rough passage's door back to the 2F left stairs, which the keypad
    # builds over its locked-door zone (ZombieKeypad.cpp kp_build_rough_door).
    if (6, 0x14) in geometry:
        geometry[6, 0x14]["doors"].setdefault(1, ((10100, 25300, 1700, 2400), (15450, 25000)))
    return geometry


class Model:
    def __init__(self, layout, geometry, args):
        self.args = args
        self.speed = RUN_UNITS_PER_TICK * TICKS_PER_SECOND / args.detour
        self.doors = {}                # room -> [(dest, requirement, zone, arrival)]
        for a, b, c, d, requirement, slot in layout["doors"]:
            zone, arrival = geometry[a, b]["doors"][slot]
            self.doors.setdefault((a, b), []).append(((c, d), requirement, zone, arrival))
        self.items = {}                # room -> [(bit, position)]
        for stage, room, item, flag, x, z in layout["items"]:
            pos = (x, z) if (x or z) else geometry[stage, room]["items"][flag]
            self.items.setdefault((stage, room), []).append((routes.BITS[item], pos))

    def walk(self, p, zone):
        x, z, w, d = zone
        if w <= 1 or d <= 1:           # an event-run door (the elevator car)
            return 0.0
        dx = max(x - p[0], 0, p[0] - (x + w))
        dz = max(z - p[1], 0, p[1] - (z + d))
        return (dx * dx + dz * dz) ** 0.5 / self.speed

    def point(self, p, q):
        return ((p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2) ** 0.5 / self.speed

    def moves(self, room, pos, mask, collect):
        """(seconds, next room, next position, next mask, escaped) from here."""
        if collect:
            for bit, at in self.items.get(room, []):
                if collect & bit and not mask & bit:
                    yield self.point(pos, at) + self.args.pickup, room, at, mask | bit, False
        for dest, requirement, zone, arrival in self.doors.get(room, []):
            if mask & requirement != requirement:
                continue
            seconds = self.walk(pos, zone) + self.args.door
            if requirement == 0xF0:
                seconds += self.args.crests
            if room == routes.EXIT and dest[0] != routes.EXIT[0]:
                if mask & 0xF0 == 0xF0:
                    yield seconds, dest, None, mask, True
                continue
            if dest[0] not in (5, 6) or dest[1] >= 0x1D:
                continue
            yield seconds, dest, tuple(arrival), mask, False

    def search(self, room, pos, mask, collect, goal):
        """Fastest time to `goal(room, mask, escaped)`; returns (seconds, final mask)."""
        heap = [(0.0, 0, room, pos, mask, False)]
        best = {}
        counter = 1
        while heap:
            t, _, r, p, m, escaped = heapq.heappop(heap)
            if goal(r, m, escaped):
                return t, m
            if escaped or best.get((r, p, m), 1e18) < t:
                continue
            for dt, nr, np_, nm, ne in self.moves(r, p, m, collect):
                key = (nr, np_, nm)
                if not ne and best.get(key, 1e18) <= t + dt:
                    continue
                if not ne:
                    best[key] = t + dt
                heapq.heappush(heap, (t + dt, counter, nr, np_, nm, ne))
                counter += 1
        return None, None

    def solo(self):
        # Keys, crests and the back area's battery and note (routes.BITS).
        return self.search(routes.START, START_POS, 0, 0x30FF, lambda r, m, e: e)

    def reachable_items(self, mask):
        seen, queue = {routes.START}, deque([routes.START])
        while queue:
            room = queue.popleft()
            for dest, requirement, _, _ in self.doors.get(room, []):
                if mask & requirement == requirement and dest[0] in (5, 6) and dest[1] < 0x1D and dest not in seen:
                    seen.add(dest)
                    queue.append(dest)
        return [(room, bit, at) for room in seen for bit, at in self.items.get(room, [])]

    def team(self, size, needed):
        held, total, rounds = 0, 0.0, []
        while needed & ~held:
            batch = [(room, bit, at) for room, bit, at in self.reachable_items(held)
                     if needed & bit and not held & bit]
            if not batch:
                return None, rounds
            trips = []
            for room, bit, at in batch:
                there, _ = self.search(routes.START, START_POS, held, bit, lambda r, m, e, b=bit: m & b)
                back, _ = self.search(room, at, held, 0, lambda r, m, e: r == routes.START)
                trips.append(there + back)
            loads = [0.0] * size
            for trip in sorted(trips, reverse=True):
                loads[loads.index(min(loads))] += trip
            total += max(loads)
            rounds.append(round(max(loads), 1))
            held |= sum(bit for _, bit, _ in batch)
        final, _ = self.search(routes.START, START_POS, held, 0, lambda r, m, e: e)
        return (None if final is None else total + final), rounds + [None if final is None else round(final, 1)]


def percentiles(values):
    values = sorted(values)
    return {str(p): round(values[round((len(values) - 1) * p / 100)] / 60, 2)
            for p in (0, 10, 50, 90, 95, 99, 100)} if values else {}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--compiler")
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument("--seed", type=lambda s: int(s, 0), action="append")
    selection.add_argument("--random-seeds", action="store_true")
    parser.add_argument("--sample-seed", type=int, default=0)
    parser.add_argument("--start", type=lambda s: int(s, 0), default=1)
    parser.add_argument("--count", type=int, default=1000)
    parser.add_argument("--door", type=float, default=0.75, help="seconds a door transition costs")
    parser.add_argument("--pickup", type=float, default=2.0, help="seconds a pickup costs")
    parser.add_argument("--crests", type=float, default=16.0, help="seconds to set the four crests")
    parser.add_argument("--detour", type=float, default=1.35, help="path length over straight line")
    parser.add_argument("--team", type=int, default=3, help="survivors for the team estimate")
    parser.add_argument("--top", type=int, default=5)
    parser.add_argument("--output", type=Path, default=routes.ROOT / "tools/zombie_route_time_report.json")
    args = parser.parse_args()
    seeds = args.seed if args.seed else (random.Random(args.sample_seed).sample(range(1, 0x100000000), args.count)
                                         if args.random_seeds else list(range(args.start, args.start + args.count)))
    assets = args.assets.resolve()
    geometry = room_geometry(assets)
    results = []
    with tempfile.TemporaryDirectory(prefix="re1-route-time-") as temp:
        exe = routes.build_adapter(Path(temp), args.compiler)
        for offset in range(0, len(seeds), 100):
            batch = seeds[offset:offset + 100]
            output = subprocess.check_output([str(exe), str(assets)], text=True,
                                             input="".join(f"{seed}\n" for seed in batch))
            for line in output.splitlines():
                layout = json.loads(line)
                if not layout["generated"]:
                    results.append({"seed": layout["seed"], "status": "generation_failed"})
                    continue
                model = Model(layout, geometry, args)
                solo, held = model.solo()
                if solo is None:
                    results.append({"seed": layout["seed"], "status": "no_escape"})
                    continue
                team, rounds = model.team(args.team, held)
                # A party can always run the solo route together.
                team = solo if team is None else min(team, solo)
                results.append({"seed": layout["seed"], "status": "solved", "solo_seconds": round(solo, 1),
                                "team_seconds": None if team is None else round(team, 1), "team_rounds": rounds,
                                "crossings": (routes.shortest_escape(layout) or {}).get("door_crossings")})
            print(f"Estimated {len(results)}/{len(seeds)} seeds", flush=True)
    solved = [r for r in results if r["status"] == "solved"]
    worst = sorted(solved, key=lambda r: -r["solo_seconds"])[:args.top]
    report = {"model": "straight-line room walks at run speed, fixed door/pickup/crest costs; no combat",
              "parameters": {"run_units_per_second": RUN_UNITS_PER_TICK * TICKS_PER_SECOND,
                             "detour": args.detour, "door_seconds": args.door, "pickup_seconds": args.pickup,
                             "crest_seconds": args.crests, "team": args.team},
              "seeds": len(results), "solved": len(solved),
              "failures": [r for r in results if r["status"] != "solved"],
              "solo_minutes": percentiles([r["solo_seconds"] for r in solved]),
              "team_minutes": percentiles([r["team_seconds"] for r in solved if r["team_seconds"] is not None]),
              "slowest_solo": worst}
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: report[k] for k in ("solo_minutes", "team_minutes")}, indent=1))
    for r in worst:
        print(f"  0x{r['seed']:08X}: solo {r['solo_seconds'] / 60:.1f} min, team {r['team_seconds'] / 60:.1f} min,"
              f" {r['crossings']} crossings")
    print(f"Report: {args.output}")
    return 1 if report["failures"] else 0


if __name__ == "__main__":
    sys.exit(main())
