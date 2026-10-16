"""CPU-only tests of the production piano puzzle (ZombiePiano.cpp); never starts the game.

Run: python tests/test_zombie_piano.py
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
#include <cmath>
#include <cstring>
#include <cstdio>
#include <vector>
#include "PRINTTEXT"
enum { STAGE_MANSION_RETURN_1F=5, ROOM_MANSION_BAR=15, ITEM_MUSIC_NOTES=0x23, ITEM_KNIFE=1,
 ZM_NET_OFF=0, ZM_NET_ZOMBIE=1, ZM_NET_SURVIVOR=2, ZM_NET_DIRECTOR=0, ZM_NET_ALL=255,
 ZM_NET_MAX_PLAYERS=4, ZM_EV_PIANO=32, ZM_CHAR_CHRIS=0, ZM_CHAR_JILL=1, ZM_CHAR_BARRY=2,
 ZM_CHAR_REBECCA=3, ZM_CHAR_RICHARD=4, ZM_CHAR_ENRICO=5,
 ZM_PAD_FORWARD=1, ZM_PAD_TURN_A=2, ZM_PAD_BACK=4, ZM_PAD_TURN_B=8, ZM_PAD_AIM=0x100, ZM_PAD_RUN=0x200 };
static int role=ZM_NET_SURVIVOR, self=1, g_stageId=5, g_roomId=15, chars[4]={-1,ZM_CHAR_JILL,ZM_CHAR_CHRIS,ZM_CHAR_RICHARD};
static unsigned int now=1000, g_PlayerDpadHeld, g_BGM_STATE;
static unsigned int g_ScenarioFlags2[8];
static unsigned char slots[16];
static void* g_ItemSlotsPointer=slots;
static int g_EquippedItemId;
static struct { unsigned char flags; struct { struct { int t[3]; } localMatrix; } scaMatrixData; short health=100; int isBeingAttackedFlag=0;
 struct { short x,y,z,pad; } position; unsigned char animationId=1, animFrameId, action_behavior, action_state, attackAnim, scd_anim_param;
 unsigned short unk_c6, unk_c8, unk_de, unk_e0; unsigned int jointMoveData2, jointMoveData3; short directionAngle; } g_playerEntity;
enum { MSF_MENU_ACTIVE=1, MSF_ROOM_TRANSITION=2 };
static int g_openMenuFlag, g_main_state_flags, g_roomTransitionBusy;
bool zombie_mode_armed() { return true; }
static bool mapOpen; bool zm_map_is_open() { return mapOpen; }
// The room's own player animations, and Jill's room file (ROOM60F1).
static unsigned char roomHeader[4], roomBase[4];
static struct { unsigned char* player_anim_header; unsigned char* player_anim_base; } rdt = { roomHeader, roomBase }, *g_RdtPointer = &rdt;
static bool jillFile = true; static int jillLoads;
size_t LoadFile(const char* path, void* buffer, unsigned char) {
 assert(strstr(path, "room60f1.rdt")); jillLoads++;
 if (!jillFile) return (size_t)-1;
 unsigned char* b = (unsigned char*)buffer; memset(b, 0, 0x2000);
 *(unsigned int*)(b + 0x6C) = 0x1000; *(unsigned int*)(b + 0x70) = 0x1800; return 0x2000;
}
#define GAME_DATA_ROOT ""
static int g_bgmDefaultVolume=-500, loads, music, wallEvents, messageCount;
int loadSndBankFromWav(const char* path,bool=false) { assert(strstr(path,"bgm_2b.wav")); loads++; return 7; }
void pan_set(int,int) {}
void destroySndBank(int) {}
// The room's music: three channels (10, 11, 12) and the stage bank (13).
struct SndBankSlot { int handle; signed char slot; char paused; };
static SndBankSlot g_SndBank[3]={{10,1,0},{11,1,0},{0,0,0}};
static int g_BgmSoundBank=13; static char g_BgmPaused; static bool roomPlaying[16], tunePlaying; static int resumes;
int getSndStat(int bank) { return bank==7 ? tunePlaying : roomPlaying[bank]; }
void ResumePausedSounds() { resumes++; for (auto& c:g_SndBank) if (c.handle && c.paused) { roomPlaying[c.handle]=true; c.paused=0; }
 if (g_BgmPaused) { roomPlaying[13]=true; g_BgmPaused=0; } }
static const unsigned char* g_MessagePtr;
struct Message { int dst; short a[8]; };
static std::vector<Message> sent;
int zm_game_role() { return role; }
bool zm_match_authority() { return role != ZM_NET_SURVIVOR; }
int zm_net_self() { return self; }
int zm_net_char(int p) { return chars[p]; }
const char* zm_char_name(int) { return "X"; }
unsigned int zm_game_time_ms() { return now; }
unsigned int zm_net_seed() { return 0x12345678; }
int Flg_ck(int base,unsigned int bit) { return (((unsigned int*)base)[bit/32] & (1u << (31-bit%32))) != 0; }
void Flg_on(int base,unsigned int bit) { ((unsigned int*)base)[bit/32] |= 1u << (31-bit%32); }
void ScdEventEntry_Create(unsigned int slot,int script) { assert(slot==9 && script==9); wallEvents++; }
void SetSndSlot(int bank,int slot) { assert(bank==7 && slot==0); music++; tunePlaying=true; }
void setSndStop(int bank) { if (bank==7) { music--; tunePlaying=false; } else roomPlaying[bank]=false; }
void set_volume(int,int) {}
unsigned int set_message_display(unsigned int,unsigned int) { messageCount++; return 0; }
void rearrange_item_slots() {}
void zm_note(const char*) {}
void zm_draw_centered(const char*,short,unsigned char) {}
void dbg_printf(const char*,...) {}
void zm_net_send_event_to(int dst,int kind,short a,short b,short c,short d,short e,short f,short g,short h) {
 assert(kind==ZM_EV_PIANO); Message m={dst,{a,b,c,d,e,f,g,h}}; sent.push_back(m);
}
'''

CHECKS = r'''
static void at(int x,int z) { g_playerEntity.scaMatrixData.localMatrix.t[0]=x; g_playerEntity.scaMatrixData.localMatrix.t[2]=z; }
static int count(int op,int dst) { int n=0; for (auto& m:sent) if (m.a[0]==op && m.dst==dst) n++; return n; }
static void reset() {
 zm_piano_reset(); memset(g_ScenarioFlags2,0,sizeof(g_ScenarioFlags2)); sent.clear();
 role=ZM_NET_SURVIVOR; self=1; g_roomId=15; g_playerEntity.health=100; g_playerEntity.isBeingAttackedFlag=0;
 g_PlayerDpadHeld=0; memset(slots,0,sizeof(slots)); slots[0]=ITEM_MUSIC_NOTES; slots[1]=1; at(10500,8000);
 g_playerEntity.animationId=1; g_playerEntity.action_behavior=0; g_playerEntity.action_state=0; g_playerEntity.flags=0;
 zm_piano_room(); music=0;
}
// USE from the inventory, then the first frame with the menu closed.
static bool use() { if (!zm_piano_use()) return false; zm_piano_frame(); return true; }
int main() {
 // Refusals: away from the piano, the wrong character, already open.
 reset(); at(2000,2000); assert(!use() && g_MessagePtr==s_notHere.bytes);
 reset(); self=2; assert(!use() && g_MessagePtr==s_notPianist.bytes);
 // Who cannot play is told so wherever they try, at the piano or not.
 reset(); self=2; at(2000,2000); assert(!use() && g_MessagePtr==s_notPianist.bytes);
 reset(); self=2; g_roomId=4; assert(!use() && g_MessagePtr==s_notPianist.bytes);
 // The margin round the piano zone.
 reset(); at(10200-700,7300-700); assert(use());
 reset(); at(10800+900,8000); assert(!use() && g_MessagePtr==s_notHere.bytes);
 reset(); g_roomId=4; assert(!use() && g_MessagePtr==s_notHere.bytes);
 reset(); Flg_on((int)g_ScenarioFlags2,0xA2); assert(!use() && g_MessagePtr==s_alreadyOpen.bytes);
 reset(); slots[0]=0; slots[1]=0; assert(!use());
 for (int ch : {ZM_CHAR_JILL, ZM_CHAR_REBECCA, ZM_CHAR_RICHARD}) {
  reset(); chars[1]=ch; assert(use()); assert(count(1,ZM_NET_ALL)==1 && music==1);
 }
 chars[1]=ZM_CHAR_JILL;
 // Moving, aiming, a hit, a grab or leaving stops it, and the sheet music stays.
 const unsigned int pads[] = { ZM_PAD_FORWARD, ZM_PAD_BACK, ZM_PAD_TURN_A, ZM_PAD_TURN_B, ZM_PAD_AIM, ZM_PAD_RUN };
 for (unsigned int pad : pads) {
  reset(); assert(use()); now+=5000; zm_piano_frame(); assert(s_playing);
  g_PlayerDpadHeld=pad; zm_piano_frame(); assert(!s_playing && count(2,ZM_NET_ALL)==1 && music==0);
  assert(slots[0]==ITEM_MUSIC_NOTES && count(3,ZM_NET_DIRECTOR)==0);
 }
 reset(); assert(use()); at(11000,9000); zm_piano_frame(); assert(!s_playing);
 // The pose: placed at the stool, turning to the keys, then the room's
 // piano animations in turn; stopping stands the player back up.
 reset(); assert(use());
 // Two feet to the right of Jill's spot and six inches closer, facing the keys (+x).
 assert(g_playerEntity.scaMatrixData.localMatrix.t[0]==10100 && g_playerEntity.scaMatrixData.localMatrix.t[2]==7500);
 assert(g_playerEntity.directionAngle==0);
 assert(g_playerEntity.flags & 4);                  // the piano's box does not push it back
 assert(g_playerEntity.animationId==8 && g_playerEntity.action_behavior==1 && g_playerEntity.attackAnim==0x37 &&
        g_playerEntity.scd_anim_param==0x20);
 // Jill's two-handed animations, from her room file, for every pianist.
 assert(g_playerEntity.jointMoveData2==(unsigned int)(s_jillRdt+0x1000) &&
        g_playerEntity.jointMoveData3==(unsigned int)(s_jillRdt+0x1800) && jillLoads==1);
 g_playerEntity.action_state=3; zm_piano_frame(); assert(g_playerEntity.attackAnim==0x37);
 g_playerEntity.action_state=5; zm_piano_frame(); assert(g_playerEntity.attackAnim==0x38 && g_playerEntity.action_state==0);
 g_playerEntity.action_state=5; zm_piano_frame(); assert(g_playerEntity.attackAnim==0x39);
 g_playerEntity.action_state=5; zm_piano_frame(); assert(g_playerEntity.attackAnim==0x38);
 g_PlayerDpadHeld=ZM_PAD_FORWARD; zm_piano_frame(); assert(!s_playing && g_playerEntity.animationId==1 && g_playerEntity.action_behavior==0);
 assert(!(g_playerEntity.flags & 4));               // collision back once standing
 // Stopping gives the room its own animations back.
 assert(g_playerEntity.jointMoveData2==(unsigned int)roomHeader && g_playerEntity.jointMoveData3==(unsigned int)roomBase);
 // Without Jill's room file: Chris's scene, from the room's own data.
 s_jillState=0; jillFile=false; reset(); assert(use());
 assert(g_playerEntity.scaMatrixData.localMatrix.t[0]==9823 && g_playerEntity.scaMatrixData.localMatrix.t[2]==6623);
 assert(g_playerEntity.directionAngle==0xFAF);       // toward (12000, 7500)
 assert(g_playerEntity.attackAnim==0x37 && g_playerEntity.jointMoveData2==(unsigned int)roomHeader);
 jillFile=true; s_jillState=0;
 // Something else taking the player (a grab) stops it and is left alone.
 reset(); assert(use());
 g_playerEntity.animationId=4; g_playerEntity.isBeingAttackedFlag=1; zm_piano_frame();
 assert(!s_playing && g_playerEntity.animationId==4);
 reset(); assert(use());
 g_playerEntity.action_behavior=2; zm_piano_frame(); assert(!s_playing && g_playerEntity.action_behavior==2);
 // Finishing stands the player back up too.
 reset(); assert(use()); now+=15000; zm_piano_frame(); assert(!s_playing && g_playerEntity.animationId==1);
 reset(); assert(use()); g_playerEntity.health=90; zm_piano_frame(); assert(!s_playing);
 reset(); assert(use()); g_playerEntity.isBeingAttackedFlag=1; zm_piano_frame(); assert(!s_playing);
 reset(); assert(use()); g_roomId=4; zm_piano_frame(); assert(!s_playing);
 // A fresh try starts from zero: 14.9 s after the restart is not enough.
 reset(); assert(use()); now+=10000; g_PlayerDpadHeld=ZM_PAD_FORWARD; zm_piano_frame();
 g_PlayerDpadHeld=0; assert(use()); now+=14900; zm_piano_frame(); assert(s_playing);
 // Finished: the sheet music is used up and the host is told; nothing opens yet.
 now+=100; zm_piano_frame(); assert(!s_playing && slots[0]==0 && count(3,ZM_NET_DIRECTOR)==1);
 assert(!zm_piano_open() && wallEvents==0);
 short done[8]; memcpy(done,sent[sent.size()-2].a,sizeof(done)); assert(done[0]==3 && done[1]==1);

 // The host: an alert on START, the wall on DONE, OPEN to everybody - once.
 reset(); role=ZM_NET_ZOMBIE; self=0; g_roomId=4; zm_piano_room();
 short start[8]={1,1,5,15,0,0,0x5678,0x1234};
 zm_piano_take(start,1); assert(s_alertPlayer==1);
 short forged[8]={3,2,5,15,0,0,0x5678,0x1234}; zm_piano_take(forged,1); assert(!zm_piano_open());
 short old[8]={3,1,5,15,0,0,0x1111,0x1234}; zm_piano_take(old,1); assert(!zm_piano_open());
 zm_piano_take(done,1); assert(zm_piano_open() && s_alertPlayer==-1 && count(4,ZM_NET_ALL)==1);
 zm_piano_take(done,1); assert(count(4,ZM_NET_ALL)==1);
 zm_piano_frame(); assert(wallEvents==0);                   // the host is not in the bar
 // A survivor in the bar sees the wall sink once, however the flag came.
 reset(); short open[8]={4,1,0,0,0,0,0x5678,0x1234}; zm_piano_take(open,0); assert(zm_piano_open());
 zm_piano_frame(); zm_piano_frame(); assert(wallEvents==1);
 reset(); Flg_on((int)g_ScenarioFlags2,0xA2); zm_piano_frame(); assert(wallEvents==2);
 // Entering the bar with the wall already down: the room's init put it away.
 reset(); Flg_on((int)g_ScenarioFlags2,0xA2); zm_piano_room(); zm_piano_frame(); assert(wallEvents==2);
 // Every copy hears the tune, wherever it is - the host too.
 reset(); g_roomId=4; zm_piano_take(start,2); assert(music==1);
 reset(); role=ZM_NET_ZOMBIE; self=0; g_stageId=6; g_roomId=2; zm_piano_take(start,2); assert(music==1);
 g_stageId=5;
 reset(); zm_piano_take(start,2); assert(music==1);
 short stop[8]={2,1,5,15,0,0,0x5678,0x1234}; zm_piano_take(stop,2); zm_piano_frame(); assert(music==0);
 // The room's music is off under the tune and back 2 s after it stops.
 reset(); roomPlaying[10]=roomPlaying[11]=roomPlaying[13]=true; resumes=0;
 short start1[8]={1,1,5,15,0,0,0x5678,0x1234}, stop1[8]={2,1,5,15,0,0,0x5678,0x1234};
 zm_piano_take(start1,1); assert(tunePlaying && !roomPlaying[10] && !roomPlaying[11] && !roomPlaying[13]);
 roomPlaying[11]=true; now+=500; zm_piano_frame(); assert(!roomPlaying[11]);       // a room change's music
 zm_piano_take(stop1,1); zm_piano_frame(); assert(!tunePlaying && !roomPlaying[10]);
 now+=1999; zm_piano_frame(); assert(resumes==0);
 now+=1; zm_piano_frame(); assert(resumes==1 && roomPlaying[10] && roomPlaying[11] && roomPlaying[13]);
 now+=5000; zm_piano_frame(); assert(resumes==1);
 // Starting again inside the 2 s keeps it off, and the tune starts over.
 zm_piano_take(start1,1); now+=500; zm_piano_frame(); zm_piano_take(stop1,1); zm_piano_frame();
 now+=1000; zm_piano_frame(); int before=music; zm_piano_take(start1,1); assert(music==before+1 && tunePlaying);
 now+=3000; zm_piano_frame(); assert(resumes==1 && !roomPlaying[10]);
 // The recording running out on its own counts as stopped.
 tunePlaying=false; now+=100; zm_piano_frame(); now+=2000; zm_piano_frame(); assert(resumes==2 && roomPlaying[10]);
 // A finished tune fades out over a second - for the pianist and everyone
 // else - and the room's music is back 2 s later.
 reset(); roomPlaying[10]=true; resumes=0; assert(use() && tunePlaying);
 now+=15000; zm_piano_frame(); assert(!s_playing && tunePlaying && count(5,ZM_NET_ALL)==1 && count(2,ZM_NET_ALL)==0);
 now+=500; zm_piano_frame(); assert(tunePlaying && !roomPlaying[10]);
 now+=500; zm_piano_frame(); assert(!tunePlaying && resumes==0);
 now+=2000; zm_piano_frame(); assert(resumes==1 && roomPlaying[10]);
 reset(); roomPlaying[10]=true; resumes=0; self=2;
 short fin[8]={5,1,5,15,0,0,0x5678,0x1234};
 zm_piano_take(start1,1); now+=15000; zm_piano_take(fin,1); zm_piano_frame(); assert(tunePlaying && s_listeners==0);
 now+=1000; zm_piano_frame(); assert(!tunePlaying);
 // A start mid-fade plays it again from the top, at full volume.
 reset(); self=2; zm_piano_take(start1,1); zm_piano_take(fin,1); now+=500; zm_piano_frame();
 int plays=music; zm_piano_take(start1,1); assert(tunePlaying && music==plays && s_fadeAt==0);   // stopped and started again
 now+=2000; zm_piano_frame(); assert(tunePlaying);
 // ...but an interruption cuts it off.
 reset(); self=2; zm_piano_take(start1,1); zm_piano_take(stop1,1); assert(!tunePlaying);
 // USE only closes the menu; the playing starts once it is closed.
 reset(); g_openMenuFlag=1; assert(zm_piano_use() && zm_piano_menu_finished());
 zm_piano_frame(); assert(!s_playing && g_playerEntity.animationId==1 && count(1,ZM_NET_ALL)==0);
 g_openMenuFlag=0; g_main_state_flags=MSF_MENU_ACTIVE; zm_piano_frame(); assert(!s_playing);
 g_main_state_flags=0; zm_piano_frame(); assert(s_playing && !zm_piano_menu_finished() && g_playerEntity.attackAnim==0x37);
 // Whatever changed meanwhile is checked again at the start.
 reset(); g_openMenuFlag=1; assert(zm_piano_use()); g_roomId=4; g_openMenuFlag=0; zm_piano_frame();
 assert(!s_playing && !s_pending);
 // Opening the inventory or the map stops it and stands the player up, so
 // the menu's idle shows instead of a frozen pose.
 reset(); assert(use()); now+=3000; g_openMenuFlag=1; zm_piano_frame();
 assert(!s_playing && g_playerEntity.animationId==1 && g_playerEntity.action_behavior==0 && slots[0]==ITEM_MUSIC_NOTES);
 g_openMenuFlag=0;
 reset(); assert(use()); g_main_state_flags=MSF_MENU_ACTIVE; zm_piano_frame(); assert(!s_playing); g_main_state_flags=0;
 reset(); assert(use()); mapOpen=true; zm_piano_frame(); assert(!s_playing && g_playerEntity.animationId==1); mapOpen=false;
 // A bit a room script set stays set.
 reset(); g_playerEntity.flags=4; assert(use()); g_PlayerDpadHeld=ZM_PAD_FORWARD; zm_piano_frame();
 assert(!s_playing && (g_playerEntity.flags & 4)); g_playerEntity.flags=0;
 // Finishing and being grabbed give the collision back too.
 reset(); assert(use()); now+=15000; zm_piano_frame(); assert(!(g_playerEntity.flags & 4));
 reset(); assert(use()); g_playerEntity.animationId=4; g_playerEntity.isBeingAttackedFlag=1; zm_piano_frame();
 assert(!s_playing && !(g_playerEntity.flags & 4));
 // Richard sits where Jill and Rebecca do.
 reset(); chars[1]=ZM_CHAR_RICHARD; assert(use());
 assert(g_playerEntity.scaMatrixData.localMatrix.t[0]==10100 && g_playerEntity.scaMatrixData.localMatrix.t[2]==7500);
 chars[1]=ZM_CHAR_JILL;
 puts("Piano puzzle OK");
}
'''

source = (routes.ROOT / "src/game/mods/ZombiePiano.cpp").read_text()
source = re.sub(r'^#include[^\n]*\n', '', source, flags=re.M)
source = re.sub(r'^extern [^\n]*\n', '', source, flags=re.M)
fixture = FIXTURE.replace("PRINTTEXT", (routes.ROOT / "src/game/PrintText.h").as_posix())
with tempfile.TemporaryDirectory(prefix="re1-piano-tests-") as temp:
    with patch.object(routes, "adapter_source", return_value=fixture + source + CHECKS):
        executable = routes.build_adapter(Path(temp))
    subprocess.run([str(executable)], check=True)
