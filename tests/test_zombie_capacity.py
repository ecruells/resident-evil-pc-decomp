"""CPU-only checks of weighted room capacity; never launches the game.
Run in an x86 VS prompt, or pass --compiler g++ on Linux.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(path, name):
    source = (ROOT / path).read_text(encoding="utf-8")
    match = re.search(r"^(?:static )?(?:int|bool|void) " + name + r"\([^;]*?\)\s*\{", source, re.M)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end] + "\n"


FIXTURE = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
enum { ENEMY_HUNTER=6, ENEMY_CHIMERA=9, ENEMY_TYRANT_1=12, ENEMY_TYRANT_2=16,
       ENTITY_STATUS_ACTIVE=1, ENTITY_STATUS_DEAD=2, NPC_ENTITIES_IDS=20,
       ZM_FIRST_SURVIVOR_SLOT=27, ZM_ROSTER_MAX=8, ZM_UID_EXTRA_FIRST=256 };
struct Entity { unsigned char id; int status_flags, health; };
static Entity g_EnemiesList[30];
struct ZmRosterEntry { bool used, alive; unsigned short uid; unsigned char stage, room, id; bool departed; };
static ZmRosterEntry s_roster[ZM_ROSTER_MAX];
static unsigned char g_stageId=5, g_roomId=1;
static bool s_worldOn=true, s_on=true;
static int cap=3, money=2000, unlock;
static int zm_world_slot_of_uid(unsigned short uid) { return uid == 256 ? 0 : -1; }
static int zm_econ_locked(unsigned char) { return unlock; }
static int zm_econ_unlock_bosses(unsigned char) { return 1; }
static bool zombie_mode_armed() { return true; }
static bool zm_access_room_elevator(unsigned char, unsigned char) { return false; }
static int zm_econ_room_cap(unsigned char, unsigned char) { return cap; }
static int zm_econ_cost(unsigned char) { return 100; }
static int zm_econ_points() { return money; }
static bool zm_yawn_entity(const Entity*) { return false; }
int zm_world_room_extra_count(unsigned char, unsigned char, bool slots=false);
int zm_world_room_unspawned(bool slots=false);
'''

CHECKS = r'''
int main() {
    assert(zm_econ_monster_slots(0)==1);
    assert(zm_econ_monster_slots(ENEMY_HUNTER)==2);
    assert(zm_econ_monster_slots(ENEMY_CHIMERA)==2);
    assert(zm_econ_monster_slots(ENEMY_TYRANT_2)==3);
    g_EnemiesList[0] = {ENEMY_HUNTER,ENTITY_STATUS_ACTIVE,100};
    s_roster[0] = {true,true,256,5,1,ENEMY_HUNTER};
    s_roster[1] = {true,true,257,5,1,ENEMY_CHIMERA};
    s_roster[2] = {true,true,258,5,2,ENEMY_TYRANT_2};
    s_roster[3] = {true,false,259,5,2,ENEMY_HUNTER};
    // Loaded Hunter plus queued Chimera: four slots, two monsters.
    assert(zm_room_monster_slots(5,1)==4);
    assert(zm_room_monster_count(5,1)==2);
    // Offscreen Tyrant: three slots, one monster. Dead entries do not count.
    assert(zm_room_monster_slots(5,2)==3);
    assert(zm_room_monster_count(5,2)==1);
    g_EnemiesList[1] = {ENEMY_CHIMERA,ENTITY_STATUS_ACTIVE,-1};
    assert(zm_room_monster_slots(5,1)==4);
    g_EnemiesList[0].status_flags |= ENTITY_STATUS_DEAD;
    assert(zm_room_monster_slots(5,1)==2);
    memset(g_EnemiesList,0,sizeof(g_EnemiesList));
    memset(s_roster,0,sizeof(s_roster));
    char why[100];
    // Two-slot arrivals must actually fit; zombies can use the last slot.
    g_EnemiesList[0] = {0,ENTITY_STATUS_ACTIVE,100};
    cap=2;
    assert(!zm_econ_can_place(5,1,ENEMY_HUNTER,"HUNTER",why,sizeof(why)));
    assert(!zm_econ_can_place(5,1,ENEMY_CHIMERA,"CHIMERA",why,sizeof(why)));
    assert(zm_econ_can_place(5,1,0,"ZOMBIE",why,sizeof(why)));
    assert(!zm_econ_can_place(5,1,ENEMY_TYRANT_2,"TYRANT",why,sizeof(why)));
    // Empty-room exception still enforces unlocks and points.
    memset(g_EnemiesList,0,sizeof(g_EnemiesList)); cap=1;
    assert(zm_econ_can_place(5,1,ENEMY_TYRANT_2,"TYRANT",why,sizeof(why)));
    money=0; assert(!zm_econ_can_place(5,1,ENEMY_TYRANT_2,"TYRANT",why,sizeof(why)));
    money=2000; unlock=1;
    assert(!zm_econ_can_place(5,1,ENEMY_TYRANT_2,"TYRANT",why,sizeof(why)));
    unlock=0;
    s_roster[0] = {true,true,257,5,1,0};
    assert(!zm_econ_can_place(5,1,ENEMY_TYRANT_2,"TYRANT",why,sizeof(why)));
    s_roster[0].alive=false;
    assert(zm_econ_can_place(5,1,ENEMY_TYRANT_2,"TYRANT",why,sizeof(why)));
    g_EnemiesList[0] = {ENEMY_TYRANT_2,ENTITY_STATUS_ACTIVE,600};
    assert(!zm_econ_can_place(5,1,0,"ZOMBIE",why,sizeof(why)));
    cap=4; assert(zm_econ_can_place(5,1,0,"ZOMBIE",why,sizeof(why)));
    puts("Weighted room accounting and empty-room Tyrant checks passed.");
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="cl")
    args = parser.parse_args()
    source = FIXTURE
    for filename, names in [
        ("ZombieEconomy.cpp", ["zm_econ_monster_slots"]),
        ("ZombieWorld.cpp", ["zm_world_room_extra_count", "zm_world_room_unspawned"]),
        ("ZombieMode.cpp", ["zm_room_monster_count", "zm_room_monster_slots"]),
        ("ZombieEconomy.cpp", ["zm_econ_room_fits", "zm_econ_can_place"]),
    ]:
        for name in names:
            source += function("src/game/mods/" + filename, name)
    with tempfile.TemporaryDirectory(prefix="re1-capacity-") as temp:
        directory = Path(temp)
        cpp, exe = directory / "test.cpp", directory / "test.exe"
        cpp.write_text(source + CHECKS, encoding="utf-8")
        if Path(args.compiler).stem.lower() == "cl":
            command = [args.compiler, "/nologo", "/EHsc", "/std:c++17", str(cpp), "/Fe" + str(exe)]
        else:
            command = [args.compiler, "-m32", "-std=c++17", str(cpp), "-o", str(exe)]
        subprocess.run(command, cwd=directory, check=True)
        subprocess.run([str(exe)], cwd=directory, check=True)


if __name__ == "__main__":
    main()
