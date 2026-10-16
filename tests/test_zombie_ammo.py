"""CPU-only regression for Chris's scavenging and dropped-ammo conservation.

Run in an x86 Visual Studio Developer Command Prompt:
    python tests/test_zombie_ammo.py
Or on Linux: python3 tests/test_zombie_ammo.py --compiler g++

Compiles the actual production functions with minimal character/drop state,
without linking or launching the game. Outputs stay in a temporary directory.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(path, name):
    source = (ROOT / path).read_text(encoding="utf-8")
    match = re.search(r"^(?:bool|unsigned char) " + name + r"\([^;]*?\)\s*\{", source, re.M)
    assert match, name
    start = match.end() - 1
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="cl")
    args = parser.parse_args()
    types = (ROOT / "src/game/Types.h").read_text(encoding="utf-8")
    defines = "\n".join(re.findall(r"^#define ITEM_(?:ROCKET_LAUNCHER|EMPTY_BOTTLE)\s+[^\r\n]+", types, re.M))
    source = """
#include <cassert>
#include <cstdio>
static const int ZM_CHAR_CHRIS = 0;
static const int ZM_DROPS_PER_ROOM = 40;
static int character = ZM_CHAR_CHRIS;
static bool dropsOn = true;
static int zm_perk_char() { return character; }
static bool zm_drops_on() { return dropsOn; }
static struct { bool alloc; } s_here[ZM_DROPS_PER_ROOM];
static unsigned char s_ops[ZM_DROPS_PER_ROOM][0x20];
static unsigned char s_recs[ZM_DROPS_PER_ROOM][0xA4];
""" + defines + "\n"
    source += function("src/game/mods/ZombieDrops.cpp", "zm_drop_is_pickup") + "\n"
    source += function("src/game/mods/ZombiePerks.cpp", "zombie_mode_pickup_quantity") + "\n"
    source += """
int main() {
    unsigned char world[0xA4] = {};
    assert(!zm_drop_is_pickup(world));
    assert(!zm_drop_is_pickup(NULL));
    for (int item = ITEM_ROCKET_LAUNCHER + 1; item < ITEM_EMPTY_BOTTLE; ++item) {
        assert(zombie_mode_pickup_quantity(item, 15, world) == 22);
        assert(zombie_mode_pickup_quantity(item, 30, world) == 45);
        assert(zombie_mode_pickup_quantity(item, 7, world) == 10);
        assert(zombie_mode_pickup_quantity(item, 6, world) == 9);
        assert(zombie_mode_pickup_quantity(item, 120, world) == 180);
        assert(zombie_mode_pickup_quantity(item, 200, world) == 250);
        assert(zombie_mode_pickup_quantity(item, 0, world) == 0);
        assert(zombie_mode_pickup_quantity(item, 1, world) == 1);
        for (int slot = 0; slot < ZM_DROPS_PER_ROOM; ++slot) {
            s_here[slot].alloc = true;
            assert(zm_drop_is_pickup(s_ops[slot] + 2));
            assert(!zm_drop_is_pickup(s_recs[slot]));
            // Ordinary transfers, death drops and repeated self-reclaims all
            // use these same allocated pickup records.
            for (int q = 0; q <= 250; ++q) {
                unsigned char held = (unsigned char)q;
                for (int reclaim = 0; reclaim < 8; ++reclaim)
                    held = zombie_mode_pickup_quantity(item, held, s_ops[slot] + 2);
                assert(held == q);
            }
        }
        character = 1;
        assert(zombie_mode_pickup_quantity(item, 15, world) == 15);
        character = -1;
        assert(zombie_mode_pickup_quantity(item, 15, world) == 15);
        character = ZM_CHAR_CHRIS;
    }
    for (int item = 0; item < 256; ++item) {
        if (item <= ITEM_ROCKET_LAUNCHER || item >= ITEM_EMPTY_BOTTLE)
            assert(zombie_mode_pickup_quantity(item, 15, world) == 15);
    }
    dropsOn = false;
    assert(!zm_drop_is_pickup(s_ops[0] + 2));
    dropsOn = true;
    s_here[0].alloc = false;
    assert(!zm_drop_is_pickup(s_ops[0] + 2));
    puts("Chris world-ammo bonus and drop-conservation checks passed.");
}
"""
    with tempfile.TemporaryDirectory(prefix="re1-ammo-") as temp:
        directory = Path(temp)
        cpp = directory / "test.cpp"
        exe = directory / "test.exe"
        cpp.write_text(source, encoding="utf-8")
        if Path(args.compiler).stem.lower() == "cl":
            command = [args.compiler, "/nologo", "/EHsc", "/std:c++17", str(cpp), "/Fe" + str(exe)]
        else:
            command = [args.compiler, "-std=c++17", str(cpp), "-o", str(exe)]
        subprocess.run(command, cwd=directory, check=True)
        subprocess.run([str(exe)], cwd=directory, check=True)


if __name__ == "__main__":
    main()
