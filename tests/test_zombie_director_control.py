"""32-bit CPU regressions for persistent director control and room pair leases."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    match = re.search(r"^(?:static )?(?:\w+[ *]+)+" + name + r"\([^;]*?\)\s*\{", source, re.M)
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
#include <vector>
enum { ZM_NET_OFF, ZM_NET_ZOMBIE, ZM_NET_SURVIVOR };
enum { ZM_NET_DIRECTOR=0, ZM_NET_MAX_PLAYERS=4, ZM_FIRST_SURVIVOR_SLOT=27,
       ZM_EV_ROSTER=4, ZM_EV_FEED=25, ENTITY_STATUS_ACTIVE=1,
       ENTITY_STATUS_DEAD=8, ZOMBIE_STATE_ATTACK=5, ZOMBIE_STATE_EATING=15,
       ENEMY_ZOMBIE=0, ENEMY_TYRANT_2=16, NPC_ENTITIES_IDS=32,
       ZM_UID_EXTRA_FIRST=256, ZM_UID_EXTRA_SURVIVOR=0x4000, ZM_UID_MAX=0x7fff,
       ZM_UID_NONE=0xffff, ZM_UID_NEW=0xfffe, ZM_ROSTER_MAX=1024, ZM_HEALTH_FRESH=0x7fff };
enum { ZM_WORLD_UID_NONE=ZM_UID_NONE, ZM_WORLD_UID_NEW=ZM_UID_NEW, ZM_WORLD_UID_BODY=0xfffd };
struct Entity {
    unsigned char id=0,state=1,ignore_player_flag=0,action_behavior=0,behavior_flags=0,status_flags=1;
    short health=100,angle=0;
    struct { struct { int t[3]; } localMatrix; } scaMatrixData={};
};
static Entity g_EnemiesList[30];
static struct { unsigned char animationId=1; } g_playerEntity;
static unsigned char g_stageId=5,g_roomId=1;
static unsigned short s_slotUid[30],s_nextUid=256;
static bool s_worldOn=true,s_zombieModeArmed=true,s_iOwn=true,s_remoteGrab=false;
static bool s_slotRemote[30]={};
static int role=ZM_NET_ZOMBIE,s_gameRole=ZM_NET_ZOMBIE,self=0,s_grabSlot=-1;
static int zm_game_role() { return role; }
static bool zm_match_authority() { return role != ZM_NET_SURVIVOR; }
static int zm_net_self() { return self; }
static int zm_room_owner_here() { return -1; }
static bool zm_survivor_role() { return s_gameRole==ZM_NET_SURVIVOR; }
static bool zm_is_zombie_id(unsigned char id) { return id==0 || id==1 || id==17; }
static void dbg_printf(const char*,...) {}
static void zm_econ_refund(unsigned char,unsigned short) {}
static void zm_shared_remove_departed(unsigned short,unsigned char) {}
static bool away=false;
static bool zm_spec_away() { return away; }
struct ZmNetPeerState { bool roomPairBusy=false; };
static ZmNetPeerState peers[4];
static bool here[4]={true,true,false,false};
static bool zm_player_here(int i) { return here[i]; }
static const ZmNetPeerState* zm_net_player(int i) { return &peers[i]; }
struct Event { short a[8]; };
static std::vector<Event> wire;
static void zm_net_send_event8(int kind,short a,short b,short c,short d,short e,short f,short g,short h) {
    if(kind==ZM_EV_ROSTER) wire.push_back({{a,b,c,d,e,f,g,h}});
}
static void zm_net_send_event(int,short,short,short,short) {}
'''

CHECKS = r'''
int main() {
    memset(s_slotUid,0xff,sizeof(s_slotUid));
    // The FIRST reliable spawn carries its reservation, before any entrance
    // cue or the director's ready STATE. Round-trip on a survivor room owner.
    for (unsigned char id : {0,1,17,2,6,9,16}) {
        role=ZM_NET_ZOMBIE;
        unsigned short uid=zm_world_add_extra(5,1,id,0,1200,0,2400,100,true,73);
        Event spawn=wire.back();
        assert((unsigned short)spawn.a[7]&0x8000);
        assert(spawn.a[6]==73); // the carried body does not reroll health
        memset(s_roster,0,sizeof(s_roster)); role=ZM_NET_SURVIVOR;
        zm_world_apply_remote(spawn.a,0);
        g_EnemiesList[0]=Entity();g_EnemiesList[0].id=id;s_slotUid[0]=uid;
        assert(zm_world_director_controlled(&g_EnemiesList[0]));
        Event stale=spawn;stale.a[7]&=0x7fff;
        zm_world_apply_remote(stale.a,1);
        assert(zm_world_director_controlled(&g_EnemiesList[0]));
        role=ZM_NET_ZOMBIE;zm_world_control(5,1,uid,id,false);
        Event release=wire.back();assert(!((unsigned short)release.a[7]&0x8000));
        role=ZM_NET_SURVIVOR;zm_world_apply_remote(release.a,0);
        assert(!zm_world_director_controlled(&g_EnemiesList[0]));
    }
    // A claim follows the uid through slot changes and reconnect snapshots.
    role=ZM_NET_ZOMBIE;g_EnemiesList[0]=Entity();s_slotUid[0]=ZM_UID_NEW;
    zm_world_control_entity(&g_EnemiesList[0],true);
    assert(zm_world_director_controlled(&g_EnemiesList[0]));
    g_EnemiesList[4]=g_EnemiesList[0];s_slotUid[4]=s_slotUid[0];s_slotUid[0]=ZM_UID_NONE;
    assert(zm_world_director_controlled(&g_EnemiesList[4]));
    static unsigned char saved[sizeof(s_roster)];
    assert(zm_world_reconnect_export(saved,1)==0);
    int savedBytes=zm_world_reconnect_export(saved,sizeof(saved));
    unsigned short sequence=s_nextUid;
    memset(s_roster,0,sizeof(s_roster));
    assert(!zm_world_reconnect_import(saved,savedBytes-1,sequence));
    assert(zm_world_reconnect_import(saved,savedBytes,sequence));
    assert(zm_world_director_controlled(&g_EnemiesList[4]));
    zm_world_control_entity(&g_EnemiesList[4],false);
    assert(!zm_world_director_controlled(&g_EnemiesList[4]));
    role=ZM_NET_SURVIVOR;zm_world_control_entity(&g_EnemiesList[4],true);
    assert(!zm_world_director_controlled(&g_EnemiesList[4]));

    // The bite's status bit 8 must never masquerade as a dead monster.
    g_EnemiesList[4]=Entity();g_EnemiesList[4].state=5;
    g_EnemiesList[4].status_flags|=ENTITY_STATUS_DEAD;
    assert(!zm_monster_dead(&g_EnemiesList[4]));
    g_EnemiesList[4].state=3;assert(zm_monster_dead(&g_EnemiesList[4]));
    g_EnemiesList[4].state=5;g_EnemiesList[4].health=-1;
    assert(zm_monster_dead(&g_EnemiesList[4]));
    // Slow director arrival: local bite, its withdrawal and a remote victim
    // keep the survivor's engine running even when player 0 is present.
    for(auto& e:g_EnemiesList) e.status_flags=0;
    self=1;s_gameRole=ZM_NET_SURVIVOR;s_iOwn=true;
    Entity& attacker=g_EnemiesList[2];attacker=Entity();attacker.state=5;
    assert(zombie_mode_room_pair_busy());assert(zm_compute_owner()==1);
    attacker.state=1;g_playerEntity.animationId=5;
    assert(zm_compute_owner()==1); // victim's release has not finished
    g_playerEntity.animationId=1;s_slotRemote[2]=true;
    assert(zm_compute_owner()==1);
    s_slotRemote[2]=false;attacker.id=16;attacker.action_behavior=7;attacker.ignore_player_flag=1;
    assert(zm_compute_owner()==1); // Tyrant impale
    attacker.action_behavior=0;assert(zm_compute_owner()==0);
    // Director sees the peer's lease; its local idle replica cannot hide it.
    self=0;s_gameRole=ZM_NET_ZOMBIE;s_iOwn=false;peers[1].roomPairBusy=true;
    assert(zm_compute_owner()==1);
    peers[1].roomPairBusy=false;assert(zm_compute_owner()==0);
    // Carrying through a door while the survivor owns the room must retire
    // the source uid. Neither a delayed UDP pose nor a reliable capture can
    // revive it; a new uid in the destination remains independent.
    role=ZM_NET_ZOMBIE;g_EnemiesList[4]=Entity();
    unsigned short oldUid=zm_world_add_extra(5,1,0,0,7000,0,9400,0,true,100);
    s_slotUid[4]=oldUid;s_iOwn=false;
    zm_world_depart_entity(&g_EnemiesList[4]);
    auto* old=zm_roster_find(5,1,oldUid,0,false);
    assert(old->departed&&!old->alive);
    Event departure=wire.back();
    zm_world_reconnect_enemy(5,1,oldUid,0,100,true,7000,0,9400,0);
    assert(old->departed&&!old->alive);
    Event delayed=departure;delayed.a[1]|=0x8000;
    zm_world_apply_remote(delayed.a,1);
    assert(old->departed&&!old->alive);
    unsigned short newUid=zm_world_add_extra(5,2,0,0,7000,0,9400,0,true,100);
    assert(newUid!=oldUid&&zm_roster_find(5,2,newUid,0,false)->alive);
    // Rejoin snapshots preserve the terminal departure as well.
    savedBytes=zm_world_reconnect_export(saved,sizeof(saved));
    assert(zm_world_reconnect_import(saved,savedBytes,s_nextUid));
    assert(zm_roster_find(5,1,oldUid,0,false)->departed);
    s_iOwn=true;attacker.state=5;attacker.id=0;s_remoteGrab=true;
    assert(zombie_mode_room_pair_busy()); // director's existing remote bite
    puts("Atomic control flags, explicit release, uid persistence and paired room leases passed.");
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="g++")
    args = parser.parse_args()
    world = (ROOT / "src/game/mods/ZombieWorld.cpp").read_text()
    mode = (ROOT / "src/game/mods/ZombieMode.cpp").read_text()
    roster = re.search(r"struct ZmRosterEntry \{.*?^\};", world, re.M | re.S).group()
    source = FIXTURE + roster + "\nstatic ZmRosterEntry s_roster[ZM_ROSTER_MAX];\n"
    source += function(mode, "zm_monster_dead")
    for name in ("zm_roster_find", "zm_roster_send", "zm_mint_uid", "zm_world_add_extra",
                 "zm_world_apply_remote", "zm_world_control", "zm_world_control_entity",
                 "zm_world_director_controlled", "zm_world_slot_of_uid", "zm_world_reconnect_export", "zm_world_reconnect_import",
                 "zm_world_depart_entity", "zm_world_reconnect_enemy"):
        source += function(world, name)
    source += function(mode, "zombie_mode_room_pair_busy") + function(mode, "zm_compute_owner")
    source += CHECKS
    with tempfile.TemporaryDirectory(prefix="re1-control-") as temp:
        cpp, exe = Path(temp) / "test.cpp", Path(temp) / "test"
        cpp.write_text(source)
        subprocess.run([args.compiler, "-m32", "-std=c++17", str(cpp), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
