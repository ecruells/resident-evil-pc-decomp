"""CPU-only doorway reinforcement regression; never launches the game.

Run in an x86 VS Developer Command Prompt: python tests/test_zombie_reinforcements.py
Linux: python3 tests/test_zombie_reinforcements.py --compiler g++
Compiles the production module against synthetic doors, collision and network.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

FIXTURE = r'''
#define zm_game_time_ms plat_time_ms
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
enum { ZM_NET_OFF, ZM_NET_ZOMBIE, ZM_NET_SURVIVOR };
enum { ENEMY_ZOMBIE = 0, ENEMY_HUNTER = 6, ENTITY_STATUS_ACTIVE = 1,
       ROOM_ACTION_ENTRIES = 128, ZM_MAX_DOORS = 16, ZM_NET_MAX_PLAYERS = 4,
       ZM_NET_DIRECTOR = 0, ZM_NET_ALL = 255, ZM_EV_REINFORCE = 29 };
struct VECTOR { int x,y,z,pad; };
struct SVECTOR { short x,y,z,pad; };
struct Matrix { int t[3]; };
struct Entity {
    int status_flags = 0, health = 100;
    struct { Matrix localMatrix; } scaMatrixData;
};
struct ZmNetPeerState {
    bool dead = false, spectating = false, transitioning = false;
    unsigned char stage = 5, room = 1, entranceDoor = 0;
    int x = 6000, y = 0, z = 5000;
};
struct ZmDoor {
    unsigned short zoneX, zoneZ, zoneW, zoneD;
    unsigned char type, sfx, flags0B, lock, dest;
    short arriveX, arriveY, arriveZ, arriveAngle;
    unsigned char needItem;
};
static Entity g_playerEntity, g_EnemiesList[30];
static VECTOR g_playerPosScratch;
static unsigned char g_stageId = 5, g_roomId = 1;
static unsigned char g_RoomActionTable[ROOM_ACTION_ENTRIES * 12];
static unsigned char g_roomItemsFlags[32];
static ZmDoor doors[3], back[3];
static ZmNetPeerState peers[4];
static bool present[4], safe = false, matchOver = false, walls = false, rosterFull = false;
static int self = 0, role = ZM_NET_ZOMBIE, owner = 0, cap = 4, count = 0, money = 1000, purchases = 0;
static unsigned int now = 100;
static unsigned short spawnedUid = 0;
static int spawnedX, spawnedZ, warnings;
static std::string notice;
struct Event { int dst, src; short a[8]; };
static std::vector<Event> events;
static unsigned int plat_time_ms() { return now; }
static int zm_game_role() { return role; }
static int zm_net_role() { return role; }
static int zm_net_char(int i) { return i == 0 ? -1 : 0; }   // seat 0: the human director
#define ZM_FIRST_SURVIVOR_SLOT 26
static int zm_net_self() { return self; }
static bool zm_spec_away() { return false; }
static bool zombie_mode_match_over() { return matchOver; }
static unsigned char zm_survivor_entrance_door() { return peers[self].entranceDoor; }
static int zm_room_owner_here() { return owner; }
static void zm_reinforce_notice(const char* s) { notice = s; }
static void zombie_mode_room_entry_sound() { warnings++; }
static const ZmNetPeerState* zm_net_player(int i) { return i != self && present[i] ? &peers[i] : NULL; }
static int zm_room_monster_slots(unsigned char, unsigned char) { return count; }
static int zm_econ_monster_slots(unsigned char id) { return id == ENEMY_HUNTER ? 2 : 1; }
static int zm_econ_room_cap(unsigned char, unsigned char) { return cap; }
static int zm_room_highest_script_slot() { return -1; }
static bool zm_random_room_safe(unsigned char, unsigned char) { return safe; }
static bool zm_yawn_director_closed(unsigned char, unsigned char) { return false; }
static bool zm_shotgun_room_blocked(unsigned char, unsigned char) { return false; }
static bool zm_random_room_stub(unsigned char, unsigned char) { return false; }
static int zm_room_doors(unsigned char, unsigned char room, const ZmDoor** out) {
    *out = room == 1 ? doors : &back[room - 2];
    return room == 1 ? 3 : 1;
}
static void zm_decode_dest(unsigned char dest, unsigned char, unsigned char* s, unsigned char* r) { *s = 5; *r = dest; }
static int room_collision_check_0047da50(VECTOR* p, VECTOR*) { return walls && p->x < 15000 ? 1 : 0; }
void MovePlayerXZ(int, SVECTOR*, SVECTOR*) {} // fixture faces along +X
static bool zm_econ_can_place(unsigned char, unsigned char, unsigned char id, const char*, char* why, int len) {
    if (count + zm_econ_monster_slots(id) > cap || money < (id == ENEMY_HUNTER ? 500 : 100) || (id == ENEMY_HUNTER && now < 600000)) {
        snprintf(why, len, "DENIED"); return false;
    }
    return true;
}
static void zm_econ_pay(unsigned char id) { money -= id == ENEMY_HUNTER ? 500 : 100; purchases++; }
static unsigned short zm_world_add_extra(unsigned char, unsigned char, unsigned char, unsigned char,
                                         short x, short, short z, short) {
    if (rosterFull) return 0;
    spawnedX = (unsigned short)x; spawnedZ = (unsigned short)z;
    spawnedUid = 0x100; return spawnedUid;
}
static unsigned short zm_world_uid_of_slot(int slot) { return slot == 0 ? spawnedUid : 0; }
static void zm_net_send_event_to(int dst, int, short a0, short a1, short a2, short a3,
                                 short a4, short a5, short a6, short a7) {
    events.push_back({dst,self,{a0,a1,a2,a3,a4,a5,a6,a7}});
}
static void dbg_printf(const char*, ...) {}
unsigned int Flg_ck(int, unsigned int) { return 1; }
void zm_reinforce_take(const short*, int);
'''

CHECKS = r'''
static void reset() {
    now = 100; self = 0; role = ZM_NET_ZOMBIE; owner = 0;
    safe = matchOver = walls = rosterFull = false;
    cap = 4; count = 0; money = 1000; purchases = warnings = 0; spawnedUid = 0;
    memset(g_RoomActionTable, 0, sizeof(g_RoomActionTable));
    for (auto& e : g_EnemiesList) e = Entity{};
    for (int i = 0; i < 4; i++) { present[i] = false; peers[i] = ZmNetPeerState{}; }
    present[1] = true;
    for (int i = 0; i < 3; i++) {
        doors[i] = {}; back[i] = {};
        int x = i == 0 ? 5000 : i == 1 ? 10000 : 20000;
        doors[i].zoneX = (unsigned short)(x - 100); doors[i].zoneW = 200;
        doors[i].zoneZ = 4900; doors[i].zoneD = 200; doors[i].dest = (unsigned char)(2 + i);
        back[i].dest = 1; back[i].arriveX = (short)x; back[i].arriveZ = 5000;
    }
    events.clear(); zm_reinforce_reset();
}
int main() {
    char why[64];
    reset(); ZmReinforcement r = {}; r.stage = 5; r.room = 1;
    assert(rf_choose(&r) && r.door == 1); // nearest door 0 is the entrance
    peers[1].entranceDoor = 1; assert(rf_choose(&r) && r.door == 0);
    peers[1].entranceDoor = 0; walls = true; assert(rf_choose(&r) && r.door == 2);
    walls = false; present[2] = true; peers[2].x = 18000; peers[2].entranceDoor = 1;
    assert(rf_choose(&r) && r.door == 2); // closest eligible pair across survivors
    present[2] = false; doors[1].flags0B = doors[2].flags0B = 0x80;
    // A leaf room (one real door, plus camera transitions): the survivor's
    // own entrance is the way in.
    assert(rf_leaf(5, 1) && rf_choose(&r) && r.door == 0);
    // Two real doors to the same neighbour are still a leaf room.
    doors[1].flags0B = 0; doors[1].dest = 2; back[0].arriveX = 5000;
    assert(rf_leaf(5, 1) && rf_choose(&r));
    doors[1].dest = 3; assert(!rf_leaf(5, 1));
    // No real door at all: nothing comes in.
    reset(); doors[0].flags0B = doors[1].flags0B = doors[2].flags0B = 0x80;
    assert(!rf_leaf(5, 1) && !rf_choose(&r));
    // A full purchase in a leaf room, through the door the survivor came in by.
    reset(); doors[1].flags0B = doors[2].flags0B = 0x80; peers[1].x = 8000;
    assert(zm_reinforce_request(5, 1, ENEMY_ZOMBIE, why, sizeof(why)) && s_warning.door == 0);
    now += 1000; zm_reinforce_frame(); assert(purchases == 1 && spawnedX == 5400);
    reset(); peers[1].dead = true; assert(!rf_choose(&r));
    peers[1].dead = false; peers[1].transitioning = true; assert(!rf_choose(&r));
    reset(); peers[1].x = 10400; assert(!rf_clear(10400, 0, 5000));
    assert(!rf_clear(0, 0, 5000));
    reset(); assert(zm_reinforce_request(5, 1, ENEMY_ZOMBIE, why, sizeof(why)));
    assert(purchases == 0 && s_warning.active);
    now += 999; zm_reinforce_frame(); assert(purchases == 0);
    now++; zm_reinforce_frame(); assert(purchases == 1 && money == 900 && spawnedX == 10400);
    assert(zm_reinforce_hold(&g_EnemiesList[0]));
    now += 500; assert(zm_reinforce_hold(&g_EnemiesList[0]));
    now += 499; assert(zm_reinforce_hold(&g_EnemiesList[0]));
    now++; assert(!zm_reinforce_hold(&g_EnemiesList[0]));
    assert(!zm_reinforce_request(5, 1, ENEMY_ZOMBIE, why, sizeof(why)));
    now += 59000; assert(zm_reinforce_request(5, 1, ENEMY_ZOMBIE, why, sizeof(why)));
    reset(); assert(!zm_reinforce_request(5, 1, ENEMY_HUNTER, why, sizeof(why)));
    now = 600000; assert(zm_reinforce_request(5, 1, ENEMY_HUNTER, why, sizeof(why)));
    now += 1000; zm_reinforce_frame(); assert(purchases == 1 && money == 500);
    reset(); assert(!zm_reinforce_request(5, 1, 2, why, sizeof(why)));
    count = cap; assert(!zm_reinforce_request(5, 1, ENEMY_ZOMBIE, why, sizeof(why)));
    reset(); safe = true; zm_reinforce_request(5, 1, ENEMY_ZOMBIE, why, sizeof(why));
    assert(!s_buy.active && purchases == 0 && s_readyAt == 0);
    reset(); zm_reinforce_request(5, 1, ENEMY_ZOMBIE, why, sizeof(why));
    // A survivor moves onto the warned spawn before its countdown completes.
    peers[1].x = 10400; now += 1000; zm_reinforce_frame();
    assert(purchases == 0 && !s_buy.active && s_readyAt == 0);
    reset(); zm_reinforce_request(5, 1, ENEMY_ZOMBIE, why, sizeof(why));
    owner = 1; now += 1000; zm_reinforce_frame(); assert(purchases == 0);
    reset(); rosterFull = true; zm_reinforce_request(5, 1, ENEMY_ZOMBIE, why, sizeof(why));
    now += 1000; zm_reinforce_frame(); assert(purchases == 0 && s_readyAt == 0);
    // Remote room owner: host requests, owner warns, host commits its reply.
    reset(); g_roomId = 9; owner = 1;
    assert(zm_reinforce_request(5, 1, ENEMY_ZOMBIE, why, sizeof(why)));
    Event request = events.back(); events.clear();
    self = 1; role = ZM_NET_SURVIVOR; g_roomId = 1;
    g_playerEntity.scaMatrixData.localMatrix.t[0] = 6000;
    g_playerEntity.scaMatrixData.localMatrix.t[2] = 5000;
    zm_reinforce_take(request.a, 0); assert(warnings == 1);
    now += 1000; zm_reinforce_frame(); Event reply = events.back();
    self = 0; role = ZM_NET_ZOMBIE; g_roomId = 9;
    zm_reinforce_take(reply.a, 2); assert(purchases == 0); // wrong sender
    zm_reinforce_take(reply.a, 1); assert(purchases == 1);
    zm_reinforce_take(reply.a, 1); assert(purchases == 1); // duplicate is inert
    reset(); g_roomId = 9; owner = 1;
    zm_reinforce_request(5, 1, ENEMY_ZOMBIE, why, sizeof(why));
    now += 5000; zm_reinforce_frame();
    assert(!s_buy.active && purchases == 0 && s_readyAt == 0);
    puts("Door selection, warning, payment, cooldown and remote-owner checks passed.");
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="cl")
    args = parser.parse_args()
    production = (ROOT / "src/game/mods/ZombieReinforcements.cpp").read_text(encoding="utf-8")
    production = re.sub(r"^#include[^\n]*\n", "", production, flags=re.M)
    with tempfile.TemporaryDirectory(prefix="re1-reinforce-") as temp:
        directory = Path(temp)
        cpp, exe = directory / "test.cpp", directory / "test.exe"
        cpp.write_text(FIXTURE + production + CHECKS, encoding="utf-8")
        if Path(args.compiler).stem.lower() == "cl":
            command = [args.compiler, "/nologo", "/EHsc", "/std:c++17", str(cpp), "/Fe" + str(exe)]
        else:
            command = [args.compiler, "-m32", "-std=c++17", str(cpp), "-o", str(exe)]
        subprocess.run(command, cwd=directory, check=True)
        subprocess.run([str(exe)], cwd=directory, check=True)


if __name__ == "__main__":
    main()
