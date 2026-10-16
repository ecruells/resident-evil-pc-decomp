"""CPU-only tests of a room's shared puzzle state (ZombieRoomSync.cpp); never starts the game.

Pushed statues and the large gallery's portrait order travel between the
survivors' copies in the room. Run: python tests/test_zombie_roomsync.py
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import evaluate_zombie_routes as routes

FIXTURE = r'''
#include <cassert>
#include <cstring>
#include <cstdio>
#include <vector>
enum { STAGE_MANSION_RETURN_1F=5, STAGE_MANSION_RETURN_2F=6, ROOM_LARGE_GALLERY=0x17, ROOM_ARMOR_ROOM=5,
 ZM_NET_OFF=0, ZM_NET_ZOMBIE=1, ZM_NET_SURVIVOR=2, ZM_NET_MAX_PLAYERS=4, ZM_EV_ROOMSYNC=33 };
#define MSF_OBJECT_PUSH 0x40u
#define MSF_ROOM_TRANSITION 0x2u
typedef unsigned int DWORD;
static int role=ZM_NET_SURVIVOR, self=1;
static unsigned char g_stageId=STAGE_MANSION_RETURN_2F, g_roomId=ROOM_ARMOR_ROOM;
static unsigned int now=1000, g_main_state_flags; static int g_roomTransitionBusy;
static unsigned short g_message_flags=0xFFFF;
static DWORD g_SysFlags[2]; static unsigned int g_ScenarioFlags[4];
static struct { unsigned char action_behavior; } g_playerEntity;
static unsigned char objs[8][0xA4]; static void* g_omodel_table[8];
static struct { unsigned char omodel_slot_count; } rdt={3}, *g_RdtPointer=&rdt;
struct ZmNetPeerState { bool valid, dead, spectating; unsigned char viewStage, viewRoom; };
static ZmNetPeerState peers[4];
const ZmNetPeerState* zm_net_player(int p) { return p==self ? nullptr : &peers[p]; }
bool zombie_mode_armed() { return true; }
int zm_game_role() { return role; }
int zm_net_char(int p) { return p==0 ? -1 : 0; }
int zm_net_self() { return self; }
unsigned int zm_game_time_ms() { return now; }
struct Msg { short a[8]; };
static std::vector<Msg> sent;
void zm_net_send_event8(int kind,short a0,short a1,short a2,short a3,short a4,short a5,short a6,short a7) {
 assert(kind==ZM_EV_ROOMSYNC); sent.push_back({{a0,a1,a2,a3,a4,a5,a6,a7}}); }
int Flg_ck(int base,unsigned int bit) { return (((unsigned int*)base)[bit/32] & (1u << (31-bit%32))) != 0; }
void Flg_on(int base,unsigned int bit) { ((unsigned int*)base)[bit/32] |= 1u << (31-bit%32); }
static void Flg_off(void* base,unsigned int bit) { ((unsigned int*)base)[bit/32] &= ~(1u << (31-bit%32)); }
static unsigned char g_RoomActionTable[128*12]; static unsigned int seed=0x1234;
unsigned int zm_random_seed() { return seed; }
static int armEvents; void ScdEventEntry_Create(unsigned int slot,int script) { assert(slot==9 && script==22); armEvents++; }
void dbg_printf(const char*,...) {}
'''

CHECKS = r'''
static int ops(int op) { int n=0; for (auto& m:sent) if (m.a[0]==op) n++; return n; }
static void put(int i,int x,int z) { *(int*)(objs[i]+0x34)=x; *(int*)(objs[i]+0x3c)=z; *(short*)(objs[i]+0x6c)=(short)x; *(short*)(objs[i]+0x70)=(short)z; }
static int ox(int i) { return *(int*)(objs[i]+0x34); }
static int oz(int i) { return *(int*)(objs[i]+0x3c); }
static void slide(int i,int x,int z) { *(int*)(objs[i]+0x34)=x; *(int*)(objs[i]+0x3c)=z; }   // update_room_objects' push
// The room's init: three statues where the RDT puts them, the system flags cleared.
static void load(unsigned char stage,unsigned char room,bool other) {
 g_stageId=stage; g_roomId=room; memset(g_SysFlags,0,sizeof(g_SysFlags)); memset(objs,0,sizeof(objs));
 for (int i=0;i<8;i++) g_omodel_table[i]=objs[i];
 for (int i=0;i<3;i++) { objs[i][0]=1; put(i,1000*(i+1),2000); }
 peers[2] = { other, false, false, stage, room };
 zm_roomsync_room();
}
static short where() { return (short)(g_stageId | (g_roomId << 8)); }
int main() {
 for (int i=0;i<8;i++) g_omodel_table[i]=objs[i];
 // A push goes out at most every 150 ms while it lasts, and where it stopped.
 zm_roomsync_reset(); load(STAGE_MANSION_RETURN_2F,ROOM_ARMOR_ROOM,false); sent.clear();
 g_main_state_flags=MSF_OBJECT_PUSH; g_playerEntity.action_behavior=0x10;
 slide(1,2010,2000); zm_roomsync_frame(); assert(ops(1)==1 && sent[0].a[2]==1 && sent[0].a[3]==2010 && sent[0].a[5]==2000);
 now+=33; slide(1,2020,2000); zm_roomsync_frame(); assert(ops(1)==1);
 now+=33; g_main_state_flags=0; g_playerEntity.action_behavior=0; zm_roomsync_frame();
 assert(ops(1)==2 && sent[1].a[3]==2020);
 // Objects the copy's own events move are every copy's own: not sent.
 sent.clear(); slide(0,500,500); zm_roomsync_frame(); assert(ops(1)==0);
 // ...nor a push frame while a scene holds the control.
 g_main_state_flags=MSF_OBJECT_PUSH; g_message_flags=0xFEFF; slide(2,3100,2000); zm_roomsync_frame(); assert(ops(1)==0);
 g_main_state_flags=0; g_message_flags=0xFFFF;

 // Another survivor's push lands on this copy when it is in the room, on
 // the same statue (where the room's init put it where the pusher's did).
 zm_roomsync_reset(); load(STAGE_MANSION_RETURN_2F,ROOM_ARMOR_ROOM,true); sent.clear();
 short push[8]={1,where(),2,3500,2600,3000,2000,0}; zm_roomsync_take(push,2);
 assert(ox(2)==3500 && oz(2)==2600 && *(short*)(objs[2]+0x6c)==3500);
 zm_roomsync_frame(); assert(ops(1)==0);                 // not echoed back
 short other[8]={1,where(),1,9000,9000,7777,2000,0}; zm_roomsync_take(other,2);
 assert(ox(1)==2000);                                     // a different init: left alone
 // The director's copy takes no part.
 role=ZM_NET_ZOMBIE; short later[8]={1,where(),2,4000,2600,3000,2000,0}; zm_roomsync_take(later,2);
 assert(ox(2)==3500); role=ZM_NET_SURVIVOR;

 // Coming into a room another survivor is in takes its state over...
 zm_roomsync_reset(); load(STAGE_MANSION_RETURN_1F,0x02,false);
 zm_roomsync_take(push,2);                                // the armor room, from elsewhere
 load(STAGE_MANSION_RETURN_2F,ROOM_ARMOR_ROOM,true); assert(ox(2)==3500 && oz(2)==2600);
 // ...an empty one starts afresh, and tells the others to forget it.
 load(STAGE_MANSION_RETURN_1F,0x02,false); zm_roomsync_take(push,2); sent.clear();
 load(STAGE_MANSION_RETURN_2F,ROOM_ARMOR_ROOM,false); assert(ox(2)==3000 && ops(3)==1);
 load(STAGE_MANSION_RETURN_2F,ROOM_ARMOR_ROOM,true); assert(ox(2)==3000);
 // A reset from another copy clears this one's record of that room.
 load(STAGE_MANSION_RETURN_1F,0x02,false); zm_roomsync_take(push,2);
 short reset[8]={3,(short)(STAGE_MANSION_RETURN_2F|(ROOM_ARMOR_ROOM<<8)),0,0,0,0,0,0}; zm_roomsync_take(reset,2);
 load(STAGE_MANSION_RETURN_2F,ROOM_ARMOR_ROOM,true); assert(ox(2)==3000);

 // The gallery's portrait order: this copy's presses go out, only the
 // puzzle's bits, and the other copies' presses land here.
 zm_roomsync_reset(); load(STAGE_MANSION_RETURN_1F,ROOM_LARGE_GALLERY,true); sent.clear();
 Flg_on((int)g_SysFlags,0x19); Flg_on((int)g_SysFlags,0x02); zm_roomsync_frame();
 assert(ops(2)==1);
 unsigned char mask[8]={}; Flg_on((int)mask,0x19); int b=0; while (!mask[b]) b++;
 assert(sent[0].a[2]==b && (unsigned char)sent[0].a[3]==mask[b] && sent[0].a[4]==0);
 Flg_off(g_SysFlags,0x19); zm_roomsync_frame(); assert(ops(2)==2 && (unsigned char)sent[1].a[4]==mask[b]);
 sent.clear();
 unsigned char m2[8]={}; Flg_on((int)m2,0x1B); Flg_on((int)m2,0x05); int b2=0; while (!m2[b2]) b2++;
 short press[8]={2,where(),(short)b2,m2[b2],0,0,0,0}; zm_roomsync_take(press,3);
 assert(Flg_ck((int)g_SysFlags,0x1B) && (b2!=0 || !Flg_ck((int)g_SysFlags,0x05)));
 zm_roomsync_frame(); assert(ops(2)==0);                 // not echoed back
 // A survivor coming in mid-puzzle has the presses made so far.
 load(STAGE_MANSION_RETURN_1F,0x02,false); load(STAGE_MANSION_RETURN_1F,ROOM_LARGE_GALLERY,true);
 assert(Flg_ck((int)g_SysFlags,0x1B));
 // Outside the gallery the system flags are nobody else's.
 load(STAGE_MANSION_RETURN_2F,ROOM_ARMOR_ROOM,true); sent.clear(); Flg_on((int)g_SysFlags,0x19); zm_roomsync_frame(); assert(ops(2)==0);

 // Solved on another copy: the panel aside and the reward's zone freed here.
 zm_roomsync_reset(); load(STAGE_MANSION_RETURN_1F,ROOM_LARGE_GALLERY,true); armEvents=0;
 unsigned char m3[8]={}; Flg_on((int)m3,0x1F); int b3=0; while (!m3[b3]) b3++;
 short solve[8]={2,where(),(short)b3,m3[b3],0,0,0,0}; zm_roomsync_take(solve,2);
 Flg_on((int)g_ScenarioFlags,0x03); zm_roomsync_frame();
 assert(armEvents==1 && objs[0][0]==0x81 && ox(0)==3440 && oz(0)==3310 && *(int*)(objs[0]+0x38)==300);
 zm_roomsync_frame(); assert(armEvents==1);
 // Solved here: the copy's own reveal does it.
 zm_roomsync_reset(); memset(g_ScenarioFlags,0,sizeof(g_ScenarioFlags)); load(STAGE_MANSION_RETURN_1F,ROOM_LARGE_GALLERY,true); armEvents=0;
 Flg_on((int)g_SysFlags,0x1F); Flg_on((int)g_ScenarioFlags,0x03); zm_roomsync_frame(); assert(armEvents==0);
 // Already solved when the room loads: its init did it.
 load(STAGE_MANSION_RETURN_1F,ROOM_LARGE_GALLERY,true); zm_roomsync_frame(); assert(armEvents==0);
 // The portrait questions change places by the seed; the plaque and the last
 // portrait stay, and every copy (the director's too) agrees.
 auto unsolved=[]{ memset(g_RoomActionTable,0,sizeof(g_RoomActionTable));
  for (int s=1;s<=8;s++) { g_RoomActionTable[s*12]=0x09; g_RoomActionTable[s*12+4]=(unsigned char)(s+4); } };
 auto layout=[&](unsigned int sd,int r){ seed=sd; role=r; unsolved(); load(STAGE_MANSION_RETURN_1F,ROOM_LARGE_GALLERY,false);
  role=ZM_NET_SURVIVOR; unsigned long long v=0; for (int s=2;s<=7;s++) v=v*16+g_RoomActionTable[s*12+4]; return v; };
 unsigned long long a=layout(0x1234,ZM_NET_SURVIVOR);
 assert(g_RoomActionTable[1*12+4]==5 && g_RoomActionTable[8*12+4]==12);
 bool seen[16]={}; for (int s=2;s<=7;s++) { int e=g_RoomActionTable[s*12+4]; assert(e>=6 && e<=11 && !seen[e]); seen[e]=true; }
 assert(layout(0x1234,ZM_NET_ZOMBIE)==a);
 bool differs=false; for (unsigned int sd=1; sd<20; sd++) differs |= layout(sd,ZM_NET_SURVIVOR)!=a;
 assert(differs && a!=0x6789AB);
 // The solved room's message portraits are left as they are.
 seed=0x1234; memset(g_RoomActionTable,0,sizeof(g_RoomActionTable));
 for (int s=2;s<=7;s++) { g_RoomActionTable[s*12]=0x02; g_RoomActionTable[s*12+4]=(unsigned char)(s+4); }
 load(STAGE_MANSION_RETURN_1F,ROOM_LARGE_GALLERY,false); for (int s=2;s<=7;s++) assert(g_RoomActionTable[s*12+4]==s+4);
 puts("Room puzzle sync OK");
}
'''

source = (routes.ROOT / "src/game/mods/ZombieRoomSync.cpp").read_text()
source = re.sub(r'^#include[^\n]*\n', '', source, flags=re.M)
source = re.sub(r'^extern [^\n]*\n', '', source, flags=re.M)
with tempfile.TemporaryDirectory(prefix="re1-roomsync-tests-") as temp:
    with patch.object(routes, "adapter_source", return_value=FIXTURE + source + CHECKS):
        executable = routes.build_adapter(Path(temp))
    subprocess.run([str(executable)], check=True)
