#!/usr/bin/env python3
"""Generate three monster spawn spots and a monster cap per mansion room for
the zombie mod.

Reads the shipped Chris-scenario RDTs (assets tree, default build/linux/USA)
and writes src/game/mods/ZombieSpawnSpots.cpp: for each room of the mansion
stages (1, 2 and their return variants 6, 7) the three spots the director's
placed monsters wait at (ZombieMode.cpp, zm_remote_spot), and how many
monsters the room holds at the start of a game (ZombieEconomy.cpp).

How a spot is found, per room:
  * the floor: a 200-unit grid, flood-filled from every point a door arrives
    at in the room (the door_set records of its neighbours), through cells where
    a body of radius 400 touches no blocking boundary record - shape 1/5
    rectangles and shape 3 circles (RDT_FILE_FORMAT.md, ".blk"; type-5 records
    are the ones that keep monsters off the player's paths). Shape-4 records are
    floor steps (stairs): their cells are a different height and are left out.
  * candidates: floor cells at least 2000 units from every door zone and
    arrival point of the room.
  * the three: farthest-point picks - the candidate furthest from the doors,
    then each next one furthest from those already picked - kept as far as
    the room's floor allows of ITEM_CLEAR from the room's own pickups (item_model_set: its pickup zone and, placed
    in room coordinates, its model), while the room has floor that far. The
    randomizer's extra pickups lie on these spots (ZombieRandom.cpp), so one
    must not end up among the room's own items - the boiler room's spot on
    its four green herbs was all but out of reach.
  * the height: the most common arrival height in the room.

The cap is the room's floor area: the cells of the flood fill a camera shows
(CAP_STEPS, smallest first) - the main hall (some 10000 cells) holds 5, a
dead-end corridor 1. EXCLUDED_AREAS and CAP_OVERRIDES hold the reviewed
exceptions (the piano bar's alcove, the bathroom).

The AI director's placement spots (g_zmPlaceSpots, ZombieDirectorAI.cpp):
up to PLACE_SPOTS per room from the same floor, so it can choose where in a
room a monster stands, not only the room:
  * candidates: shown floor cells off the steps, at least PLACE_DOOR_CLEAR
    from every door zone (nothing blocks a doorway) and PLACE_STUN_CLEAR from
    every arrival point - outside the door stun's reach (ZombieMode.cpp,
    ZM_STUN_RADIUS 2200), which the AI must never trigger;
  * the spots: farthest-point picks again, the first furthest from the doors,
    until PLACE_SPOTS or no candidate lies PLACE_SPACING from the others;
  * each spot's openness: its floor cells within PLACE_OPEN_CELLS - low in a
    corridor or a gap between furniture, high in the middle of a hall.
The doors themselves are not stored: the game reads them from the RDT.

Run from the repo root:  python3 tools/gen_spawn_spots.py [assets/USA]
"""
import collections
import os
import re
import struct
import sys

# SCD opcode operand widths (ZombieSurvivor.cpp kScdOperandWidth).
W = [0, 1, 1, 1, 3, 3, 3, 5, 3, 1, 1, 3, 25, 17, 1, 7, 1, 1, 9, 3, 3, 1, 1, 9, 25, 3, 1, 21, 5, 1, 3, 27,
     13, 13, 3, 1, 3, 3, 0, 1, 5, 1, 11, 3, 1, 1, 0, 3, 11, 3, 3, 1, 1, 3, 3, 3, 3, 1, 3, 5, 5, 11, 1, 5,
     15, 3, 3, 3, 1, 1, 43, 13, 1, 1, 1, 1, 3, 1, 3, 1, 1]

MANSION_STAGES = [0, 1, 5, 6]
CELL = 200
BODY = 400
DOOR_CLEAR = 2000
PLACE_SPOTS = 16
PLACE_DOOR_CLEAR = 1000
PLACE_STUN_CLEAR = 2700
PLACE_SPACING = 1000
ITEM_CLEAR = (4000, 3000, 2000, 1500)      # the most a room's floor allows
PLACE_OPEN_CELLS = 3            # a 7 x 7 cell square around the spot: openness 1..49
# (floor cells at least, cap): the first row a room reaches.
CAP_STEPS = [(6000, 5), (1500, 4), (700, 3), (250, 2), (0, 1)]

# Floor the flood fill reaches that no spot may use, (x0, z0, x1, z1), by
# (stage, room) - reviewed by hand. The piano bar's alcove lies behind the
# sliding wall (an object, not a boundary record) until the piano is played.
EXCLUDED_AREAS = {
    (0, 0x0F): [(7800, 12000, 10600, 19000)],
    (5, 0x0F): [(7800, 12000, 10600, 19000)],
}
# Caps set by hand where the floor area misleads, by (stage, room): the
# bathroom off the trap passage is mostly its tub. These do not grow over the
# game (ZM_SPAWN_CAP_FIXED).
CAP_OVERRIDES = {
    (0, 0x13): 1,
    (5, 0x13): 1,
}


def excluded(stage, room, x, z):
    return any(x0 <= x <= x1 and z0 <= z <= z1 for x0, z0, x1, z1 in EXCLUDED_AREAS.get((stage, room), []))


def cap_of(cells):
    for least, cap in CAP_STEPS:
        if cells >= least:
            return cap
    return 1


def find_ci(directory, name):
    low = name.lower()
    for f in os.listdir(directory):
        if f.lower() == low:
            return os.path.join(directory, f)
    return None


def load_rdt(root, stage, room):
    d = find_ci(root, 'stage%d' % (stage + 1))
    if d is None:
        return None
    p = find_ci(d, 'room%d%02x0.rdt' % (stage + 1, room))
    if p is None:
        return None
    b = open(p, 'rb').read()
    return b if len(b) >= 0x94 else None


def doors_of(b):
    """door_set records of the init SCD: (zone x, z, w, d, dest, flags0B, arrive x, y, z)."""
    out = []
    p = struct.unpack_from('<I', b, 0x60)[0]
    while p + 2 <= len(b):
        bs = struct.unpack_from('<H', b, p)[0]
        if bs == 0:
            break
        q, e = p + 2, p + bs
        while q < e:
            op = b[q]
            if op >= len(W):
                break
            if op == 0x0C and q + 26 <= len(b):
                r = b[q + 2:q + 26]
                zx, zz, zw, zd = struct.unpack_from('<4H', r, 0)
                ax, ay, az = struct.unpack_from('<HhH', r, 0x0E)
                out.append((zx, zz, zw, zd, r[0x0D], r[0x0B], ax, ay, az))
            q += 1 + W[op]
        p = e
    return out


def items_of(b):
    """item_model_set records of the init SCD: (zone x, z, w, d, x, z or None) - the
    model's position only when it is in room coordinates (no parent)."""
    out = []
    p = struct.unpack_from('<I', b, 0x60)[0]
    while p + 2 <= len(b):
        bs = struct.unpack_from('<H', b, p)[0]
        if bs == 0:
            break
        q, e = p + 2, p + bs
        while q < e:
            op = b[q]
            if op >= len(W):
                break
            if op == 0x18 and q + 0x1A <= len(b):
                zx, zz, zw, zd = struct.unpack_from('<4H', b, q + 2)
                x, z = struct.unpack_from('<hxxh', b, q + 0x0E)
                out.append((zx, zz, zw, zd, x if b[q + 0x0D] == 0xFF else None, z))
            q += 1 + W[op]
        p = e
    return out


def dest_room(dest, from_stage):
    if dest < 0x20:
        return from_stage, dest
    s = (dest >> 5) - 1
    if from_stage >= 5 and s < 2:      # the return mansion's doors keep to it
        s += 5
    return s, dest & 0x1F


def boundaries(b):
    bd = struct.unpack_from('<I', b, 0x4C)[0]
    counts = struct.unpack_from('<4i', b, bd + 4)
    recs = set()
    off = bd + 0x18
    for i in range(sum(c for c in counts if c > 0)):
        if off + 12 > len(b):
            break
        recs.add(struct.unpack_from('<4H2H', b, off))
        off += 12
    blocks, steps = [], []
    for xmax, zmax, xmin, zmin, typ, flags in recs:
        shape = typ & 0xFF
        if shape in (1, 5):
            blocks.append(('r', xmin, zmin, xmax, zmax))
        elif shape == 3:
            r = (xmax - xmin) // 2
            blocks.append(('c', (xmin + xmax) // 2, (zmin + zmax) // 2, r, 0))
        elif shape == 4:
            steps.append((xmin, zmin, xmax, zmax))
    return blocks, steps


def blocked(x, z, blocks):
    for b in blocks:
        if b[0] == 'r':
            _, x0, z0, x1, z1 = b
            if x0 - BODY <= x <= x1 + BODY and z0 - BODY <= z <= z1 + BODY:
                return True
        else:
            _, cx, cz, r, _ = b
            if (x - cx) ** 2 + (z - cz) ** 2 <= (r + BODY) ** 2:
                return True
    return False


def camera_zones(b):
    """The room's camera switch-zone quads (RDT+0x48, 0x14-byte records grouped
    by camera, each group's first record its header): together they cover
    everywhere a camera shows - the floor the player can stand on. The flood
    fill alone can leak through gaps in the collision outline."""
    cams = b[1]
    p = struct.unpack_from('<I', b, 0x48)[0]
    quads, group = [], None
    for _ in range(1024):
        if p + 0x14 > len(b):
            break
        to, frm = struct.unpack_from('<HH', b, p)
        if frm >= cams:
            break
        pts = struct.unpack_from('<8H', b, p + 4)
        if frm != group:
            group = frm                      # header
        else:
            quads.append([(pts[0], pts[1]), (pts[2], pts[3]), (pts[4], pts[5]), (pts[6], pts[7])])
        p += 0x14
    return quads


def in_poly(x, z, pts):
    inside = False
    n = len(pts)
    for i in range(n):
        x1, z1 = pts[i]
        x2, z2 = pts[(i + 1) % n]
        if (z1 > z) != (z2 > z):
            xc = x1 + (z - z1) * (x2 - x1) / (z2 - z1)
            if x < xc:
                inside = not inside
    return inside


def in_camera(x, z, quads):
    for q in quads:
        # The corner order is not documented: accept either winding of the quad.
        if in_poly(x, z, q) or in_poly(x, z, [q[0], q[1], q[3], q[2]]):
            return True
    return False


def in_steps(x, z, steps):
    return any(x0 - CELL <= x <= x1 + CELL and z0 - CELL <= z <= z1 + CELL for x0, z0, x1, z1 in steps)


def rect_dist(x, z, zx, zz, zw, zd):
    dx = max(zx - x, 0, x - (zx + zw))
    dz = max(zz - z, 0, z - (zz + zd))
    return (dx * dx + dz * dz) ** 0.5


def spots_for(root, stage, room, arrivals):
    b = load_rdt(root, stage, room)
    if b is None or not arrivals.get((stage, room)):
        return None
    blocks, steps = boundaries(b)
    own = doors_of(b)
    seeds = arrivals[(stage, room)]
    # Flood fill from the arrival points.
    def key(x, z):
        return (x // CELL, z // CELL)
    seen, floor = set(), []
    queue = collections.deque()
    for ax, ay, az in seeds:
        k = key(ax, az)
        queue.append(k)
    while queue:
        k = queue.popleft()
        if k in seen:
            continue
        seen.add(k)
        x, z = k[0] * CELL + CELL // 2, k[1] * CELL + CELL // 2
        if not (0 <= x <= 65535 and 0 <= z <= 65535):
            continue
        if blocked(x, z, blocks):
            continue
        floor.append((x, z))
        if len(floor) > 40000:
            break
        for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            queue.append((k[0] + dx, k[1] + dz))
    floor = [(x, z) for x, z in floor if not excluded(stage, room, x, z)]
    # Away from doors and arrivals, off the steps.
    def door_gap(x, z):
        g = min([rect_dist(x, z, d[0], d[1], d[2], d[3]) for d in own] or [99999])
        g = min([g] + [((x - a[0]) ** 2 + (z - a[2]) ** 2) ** 0.5 for a in seeds])
        return g
    quads = camera_zones(b)
    if quads:
        shown = [(x, z) for x, z in floor if in_camera(x, z, quads)]
        if shown:
            floor = shown
    cand = [(x, z) for x, z in floor if not in_steps(x, z, steps) and door_gap(x, z) >= DOOR_CLEAR]
    if not cand:
        cand = [(x, z) for x, z in floor if not in_steps(x, z, steps)] or floor
    if not cand:
        return None
    # Clear of the room's own pickups (the randomizer's extra ones lie here).
    items = items_of(b)
    def item_gap(x, z):
        g = min([rect_dist(x, z, i[0], i[1], i[2], i[3]) for i in items] or [99999])
        return min([g] + [((x - i[4]) ** 2 + (z - i[5]) ** 2) ** 0.5 for i in items if i[4] is not None])
    for need in ITEM_CLEAR:
        clear = [(x, z) for x, z in cand if item_gap(x, z) >= need]
        if clear:
            cand = clear
            break
    heights = collections.Counter(a[1] for a in seeds)
    y = heights.most_common(1)[0][0]
    picks = [max(cand, key=lambda p: door_gap(*p))]
    while len(picks) < 3:
        nxt = max(cand, key=lambda p: min(((p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2) for q in picks))
        if nxt in picks:
            break
        picks.append(nxt)
    while len(picks) < 3:
        picks.append(picks[-1])
    # The AI director's spots.
    cells = set(key(x, z) for x, z in floor)
    def stun_gap(x, z):
        return min([((x - a[0]) ** 2 + (z - a[2]) ** 2) ** 0.5 for a in seeds] or [99999])
    near = [(x, z) for x, z in floor if not in_steps(x, z, steps) and door_gap(x, z) >= PLACE_DOOR_CLEAR
            and stun_gap(x, z) >= PLACE_STUN_CLEAR]
    place = []
    if near:
        place.append(max(near, key=lambda p: door_gap(*p)))
        while len(place) < PLACE_SPOTS:
            nxt = max(near, key=lambda p: min(((p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2) for q in place))
            if min(((nxt[0] - q[0]) ** 2 + (nxt[1] - q[1]) ** 2) for q in place) < PLACE_SPACING ** 2:
                break
            place.append(nxt)
    def openness(x, z):
        kx, kz = key(x, z)
        return sum((kx + i, kz + j) in cells for i in range(-PLACE_OPEN_CELLS, PLACE_OPEN_CELLS + 1)
                   for j in range(-PLACE_OPEN_CELLS, PLACE_OPEN_CELLS + 1))
    place = [(x, z, openness(x, z)) for x, z in place]
    if (stage, room) in CAP_OVERRIDES:
        return [(x, y, z) for x, z in picks], '%d | ZM_SPAWN_CAP_FIXED' % CAP_OVERRIDES[(stage, room)], y, place
    return [(x, y, z) for x, z in picks], '%d' % cap_of(len(floor)), y, place


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else 'build/linux/USA'
    # Every door's arrival point, by the room it arrives in.
    arrivals = collections.defaultdict(list)
    for stage in MANSION_STAGES:
        for room in range(0x20):
            b = load_rdt(root, stage, room)
            if b is None:
                continue
            for d in doors_of(b):
                if d[5] & 0x80:          # camera-only: lands in the same room
                    ds, dr = stage, room
                else:
                    ds, dr = dest_room(d[4], stage)
                arrivals[(ds, dr)].append((d[6], d[7], d[8]))
    rows = []
    for stage in MANSION_STAGES:
        for room in range(0x20):
            s = spots_for(root, stage, room, arrivals)
            if s:
                rows.append((stage, room, s[0], s[1], s[2], s[3]))
    out = []
    out.append('// GENERATED by tools/gen_spawn_spots.py - do not edit by hand.')
    out.append('// Three monster spawn spots per mansion room (see the tool for how they')
    out.append('// are chosen): where the director\'s placed monsters wait for a room that')
    out.append('// is not loaded (ZombieMode.cpp, zm_remote_spot). Then the room\'s monster')
    out.append('// cap by its floor area (ZombieEconomy.cpp).')
    out.append('#include "ZombieModeInternal.h"')
    out.append('')
    out.append('const ZmSpawnSpots g_zmSpawnSpots[] = {')
    for stage, room, s, cap, y, place in rows:
        pts = ', '.join('{ %d, %d, %d }' % p for p in s)
        out.append('    { %d, 0x%02X, %s, { %s } },' % (stage, room, cap, pts))
    out.append('};')
    out.append('const int g_zmSpawnSpotCount = (int)(sizeof(g_zmSpawnSpots) / sizeof(g_zmSpawnSpots[0]));')
    out.append('')
    out.append('// The AI director\'s placement spots (ZombieDirectorAI.cpp): up to %d a room,' % PLACE_SPOTS)
    out.append('// { x, z, openness } - its floor cells in the %d x %d square around it.'
               % (PLACE_OPEN_CELLS * 2 + 1, PLACE_OPEN_CELLS * 2 + 1))
    out.append('const ZmPlaceSpots g_zmPlaceSpots[] = {')
    total = 0
    for stage, room, s, cap, y, place in rows:
        if not place:
            continue
        total += len(place)
        pts = ', '.join('{ %d, %d, %d }' % p for p in place)
        out.append('    { %d, 0x%02X, %d, %d, { %s } },' % (stage, room, len(place), y, pts))
    out.append('};')
    out.append('const int g_zmPlaceSpotCount = (int)(sizeof(g_zmPlaceSpots) / sizeof(g_zmPlaceSpots[0]));')
    path = 'src/game/mods/ZombieSpawnSpots.cpp'
    open(path, 'w').write('\n'.join(out) + '\n')
    print('%d rooms, %d AI placement spots -> %s' % (len(rows), total, path))


if __name__ == '__main__':
    main()
