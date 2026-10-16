#!/usr/bin/env python3
"""Exercise the actual C++ progression generator without launching the game.

Run with a C++ compiler on PATH: python tests/check_randomizer_progression.py
For MSVC, use a developer prompt and pass --compiler cl.
Synthetic graphs cover room sharing and key-gated routes; shipped RDTs and
scripted map interactions still need play-testing.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    start = re.search(r"^static [^\n]+\b" + name + r"\(", source, re.M).start()
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="c++")
    args = parser.parse_args()
    source = (ROOT / "src/game/mods/ZombieRandom.cpp").read_text()
    types = (ROOT / "src/game/Types.h").read_text()
    names = ["STAGE_MANSION_RETURN_1F", "STAGE_MANSION_RETURN_2F",
             "ROOM_MAIN_HALL", "ROOM_STOREROOM", "ROOM_TRAP_ROOM", "ROOM_LIVING_ROOM",
             "ROOM_LESSON_ROOM", "ROOM_MANSION_B1_PASSAGE_1", "ROOM_MANSION_KITCHEN",
             "ITEM_MUSIC_NOTES", "ITEM_CHEMICAL", "ITEM_RED_JEWEL", "ITEM_BATTERY"]
    names += re.findall(r"\bITEM_[A-Z_]+", source[source.index("static const unsigned char kKeys"):
                                               source.index("struct RndDoor")])
    defines = []
    for name in names:
        defines.append(re.search(r"^#define\s+" + name + r"\s+[^\n]+", types, re.M).group())
    defines.append("#define ITEM_ZM_PASS_NOTE 0xF0")      # ZombieModeInternal.h
    declarations = source[source.index("#define RND_STAGE_1F"):source.index("static unsigned char s_rdt[")]
    functions = "\n".join(function(source, name) for name in (
        "rnd_next", "rnd_below", "rnd_is_key", "rnd_lock_index",
        "rnd_door_key_lock", "rnd_room_index", "rnd_item_bit", "rnd_door_open", "rnd_reach",
        "rnd_route_state", "rnd_route_owned", "rnd_rooms_from", "rnd_route_cost", "rnd_puzzle_room", "rnd_boss_room",
        "rnd_key_item_room",
        "rnd_key_spot_ok", "rnd_pick_key_spot", "rnd_pick_back_spot", "rnd_generate_once"))
    harness = r'''
static void fixture(int gated, int rooms) {
    s_spotCount = s_doorCount = 0;
    s_lockCount = 4;
    for (int i = 0; i < 4; i++) s_lockFlag[i] = (unsigned char)i;
    // Five free pickup spots per room: spot uniqueness alone is insufficient.
    for (int room = 0; room < rooms; room++) {
        for (int spot = 0; spot < 5; spot++) {
            RndSpot& s = s_spots[s_spotCount++];
            memset(&s, 0, sizeof(s));
            s.stage = RND_STAGE_1F;
            s.room = (unsigned char)room;
            s.pool = true;
        }
        if (room == ROOM_MAIN_HALL) continue;
        RndDoor& d = s_doors[s_doorCount++];
        memset(&d, 0, sizeof(d));
        d.fromStage = d.toStage = RND_STAGE_1F;
        d.fromRoom = ROOM_MAIN_HALL;
        d.toRoom = (unsigned char)room;
        d.lock = (unsigned char)(gated && room < 4 ? 0x80 | room : 0);
        d.need = ITEM_SWORD_KEY;
        d.usable = true;
        if (room == ROOM_STOREROOM) d.lock = 0x80 | RND_CREST_LOCK;
        // The way back: a room counts only when the hall is reachable from it.
        RndDoor& back = s_doors[s_doorCount++];
        back = d;
        back.fromRoom = d.toRoom;
        back.toRoom = d.fromRoom;
    }
}
int main() {
    for (int gated = 0; gated < 2; gated++) {
        fixture(gated, RND_ROOMS);
        for (unsigned int seed = 1; seed <= 10000; seed++) {
            s_rng = seed;
            bool ok = false;
            for (int attempt = 0; attempt < 200 && !ok; attempt++) ok = rnd_generate_once();
            if (!ok) return 1;
            bool occupied[RND_ROOMS * 2] = {};
            int count = 0;
            for (int i = 0; i < s_spotCount; i++) {
                const RndSpot& s = s_spots[i];
                if (!rnd_item_bit(s.id)) continue;
                int room = rnd_room_index(s.stage, s.room);
                if (occupied[room]) return 2;
                occupied[room] = true;
                count++;
            }
            bool reach[RND_ROOMS * 2];
            if (count != 8 || rnd_reach(0, true, reach) != 0xFF ||
                !reach[rnd_room_index(RND_STAGE_1F, ROOM_STOREROOM)]) return 3;
        }
    }
    // Seven rooms cannot hold eight progression items, even with spare spots.
    fixture(0, 7);
    s_rng = 1;
    for (int attempt = 0; attempt < 200; attempt++) if (rnd_generate_once()) return 4;
    puts("Randomizer progression OK: 20,000 seeds, distinct rooms, reachable keys/crests and exit; impossible layout rejected.");
}
'''
    with tempfile.TemporaryDirectory(prefix="re1-randomizer-") as directory:
        directory = Path(directory)
        cpp = directory / "check.cpp"
        exe = directory / "check.exe"
        cpp.write_text("#include <cstdio>\n#include <cstring>\n" + "\n".join(defines) +
                       "\n" + declarations + "\nstatic unsigned int s_rng;\n" +
                       # The tools, puzzle items and weapon ranks need the real
                       # mansion; tests/test_zombie_routes.py places them there.
                       "static bool rnd_place_key_items(bool*) { return true; }\n"
                       "static void rnd_rank_puzzles(void) {}\n" +
                       functions + harness)
        if Path(args.compiler).stem.lower() == "cl":
            command = [args.compiler, "/nologo", "/EHsc", str(cpp), "/Fe:" + str(exe)]
        else:
            command = [args.compiler, "-std=c++17", str(cpp), "-o", str(exe)]
        subprocess.run(command, cwd=directory, check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
