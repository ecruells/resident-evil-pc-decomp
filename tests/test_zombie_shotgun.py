"""CPU-only tests of the production shotgun state machine; never starts the game."""
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
#include "../src/game/PrintText.h"
enum { STAGE_MANSION_RETURN_1F=5, ROOM_TRAP_PASSAGE=9, ROOM_TRAP_ROOM=21,
 ROOM_LIVING_ROOM=22, ITEM_CLIP=10, ITEM_NON_INFINITE_MAX=75, ITEM_SHOTGUN=3, ITEM_BROKEN_SHOTGUN=28, ITEM_PICK_AXE=76,
 ZM_NET_OFF=0, ZM_NET_ZOMBIE=1, ZM_NET_SURVIVOR=2, ZM_NET_DIRECTOR=0,
 ZM_NET_MAX_PLAYERS=4, ZM_EV_SHOTGUN=31, ZM_CHAR_JILL=1, ZM_CHAR_BARRY=2,
 NPC_ENTITIES_IDS=32, MSF_MENU_ACTIVE=1, MSF_ROOM_TRANSITION=2, MSF_GAMEPLAY_ACTIVE=4 };
static unsigned char g_bItemMenuSelectedItemId;
static int role=ZM_NET_ZOMBIE, self=0, g_stageId=5, g_roomId=9;
static bool armed=true, ended=false, busy=false;
static unsigned int now=1000, flag0=0;
static unsigned char slots[16], g_RoomActionTable[128*12];
static unsigned char g_ItemSlotIndices[8], g_ItemImageLookupTable[459];
static unsigned int g_ItemSlotsBitmask;
static int g_TotalHeldItems, g_EquippedItemId;
static int solvable=1;
int zm_random_remaining_solvable() { return solvable; }
void zm_match_progression_lost() { ended=true; }
static void* g_ItemSlotsPointer=slots;
static void* g_item_model_table[2], *g_omodel_table[3];
static unsigned int g_roomItemsFlags[8];
static int g_openMenuFlag, g_main_state_flags, g_roomTransitionBusy, g_pendingDoorRecord, g_message_flags;
static struct { struct { struct { int t[3]; } localMatrix; } scaMatrixData;
 int health=100, isBeingAttackedFlag=0,lookAtFlags=0,lookAtYawStep=0,lookAtPitchStep=0,lookAtTargetX=0,lookAtTargetY=0,lookAtTargetZ=0; } g_playerEntity;
static struct { int textureId,x,y,w,h,r,g,b; } g_rect;
struct ZmNetPeerState { bool valid=false,dead=false,spectating=false,transitioning=false,attacked=false;
 int stage=5, room=9, x=8000,z=9500; };
static ZmNetPeerState peers[4];
static unsigned char peerItem[4];
static int peerChar[4];
static bool hostPlays=false;   // an AI director's game: seat 0 is the host's survivor
int zm_net_char(int p) {return p==0 && !hostPlays ? -1 : peerChar[p];}
struct ZmReconnectPlayer { struct { unsigned char stageId=5,roomId=21,roomCameraId=0,
 totalHeldItems=0,equippedItemId=0; } card; unsigned char inventory[16]={},indices[8]={};
 unsigned int inventoryMask=0; int health=100,x=0,y=0,z=0; short angle=0;
 unsigned short shotgunReplacement=0; unsigned char pickaxeSpent=0; };
struct Message { int dst; short a[8]; };
static std::vector<Message> messages;
bool zombie_mode_armed() { return armed; }
bool zombie_mode_match_over() { return ended; }
int zm_game_role() { return role; }
int zm_net_role() { return role; }
int zm_net_self() { return self; }
unsigned int zm_game_time_ms() { return now; }
unsigned int zm_net_seed() { return 0x12345678; }
int Flg_ck(int base,unsigned int bit) { return bit==0 ? flag0 : (((unsigned int*)base)[bit/32] & (1u << (31-bit%32))); }
void Flg_on(int base,unsigned int bit) { ((unsigned int*)base)[bit/32] |= 1u << (31-bit%32); }
void FUN_00473f10(int* base,unsigned int bit) { if(!bit)flag0=0; else base[bit/32] &= ~(1u << (31-bit%32)); }
void zm_note(const char*) {}
void dbg_printf(const char*,...) {}
void rearrange_item_slots() {}
const ZmNetPeerState* zm_net_player(int p) { return &peers[p]; }
const ZmNetPeerState* zm_seat_state(int p) { return p==0 && !hostPlays ? nullptr : &peers[p]; }
bool zm_net_has_item(int p,unsigned char id) { return peerItem[p]==id; }
void zm_decode_dest(unsigned char dest,unsigned char stage,unsigned char* s,unsigned char* r) { *s=stage; *r=dest; }
void zm_net_send_event_to(int dst,int,short a,short b,short c,short d,short e,short f,short g,short h) {
 Message m={dst,{a,b,c,d,e,f,g,h}}; messages.push_back(m);
}
void zm_net_send_event8(int k,short a,short b,short c,short d,short e,short f,short g,short h) {
 zm_net_send_event_to(255,k,a,b,c,d,e,f,g,h);
}
bool zombie_mode_pickup_waiting() { return busy; }
bool zombie_mode_box_waiting() { return busy; }
struct Entity {int id=0,health=100;};
void zm_shotgun_director_crush() {}
static bool crushCamera; static int crushedMonsters;
void zm_shotgun_crush_view(bool enabled) {crushCamera=enabled;}
void zm_world_shotgun_crush() {crushedMonsters++;}
void zm_world_shotgun_seal() {}
static bool clearArea=true;
static bool trapClear=true;
bool zm_world_shotgun_clear(unsigned char,unsigned char room,int,int) { return room==ROOM_TRAP_ROOM?trapClear:clearArea; }
bool zm_net_inventory(int p,unsigned char* out) { memset(out,0,16);out[0]=peerItem[p];out[1]=1;return true; }
static int clicks,loops,filtered;
static int g_SfxVolume=0;
static const unsigned char* g_MessagePtr;
unsigned int set_message_display(unsigned int,unsigned int) {return 0;}
void play_sfx(int,int,int) {clicks++;}
const char* ResolveAssetRoot(const char* path) {return path;}
static char voicePath[128]; static bool voicePlaying; static int voiceStarts;
int loadSndBankFromWav(const char* path,bool m=false) {filtered+=m;strncpy(voicePath,path,127);return 1;}
int getSndStat(int) {return voicePlaying?1:0;}
void SetSndSlot(int,int) {voicePlaying=true;voiceStarts++;}
void pan_set(int,int) {}
void destroySndBank(int) {}
void setSndStop(int) {}
void set_volume(int,int) {}
void playSnd(int,int) {loops++;}
#define GAME_DATA_ROOT ""
void playSndBank(int,int) {loops++;}
void setSndBankVolume(int,int) {}
static bool s_deathDropped;
static bool s_deathSeen[4];
static int s_revives[4];
#define ZM_REVIVE_LIMIT 1
static bool zm_revive_free(int) { return false; }
static int floorDrops;
#define ZM_DEATH_DROP_RADIUS 600
struct SVECTOR { short x,y,z,pad; };
void MovePlayerXZ(int,SVECTOR*,SVECTOR*) {}
bool zm_drop_add(unsigned char,unsigned char,int,int,int) { floorDrops++;return true; }
void draw_rect(decltype(g_rect)*,int,int) {}
void Task_sleep(int) {}
static int cameraRepairs;
void zm_fix_camera_at(const int*,int) { cameraRepairs++; }
void StMask(int,int) {}
'''

CHECKS = r'''
int main() {
 short request[8]={2,0,0,0,0,0,0x5678,0x1234};
 unsigned char ceiling[0xA4]={},doorModel[0xA4]={},replacement[0xA4]={},rec[24]={};
 g_omodel_table[0]=doorModel;g_item_model_table[1]=replacement;
 zm_shotgun_reset();zm_shotgun_room();assert(doorModel[0]==15);
 peers[1].valid=true;peers[1].room=ROOM_TRAP_ROOM;
 peers[2].valid=true;peers[2].room=ROOM_LIVING_ROOM;
 peers[3].valid=true;peers[3].room=ROOM_TRAP_PASSAGE;
 zm_shotgun_frame();assert(s_active&&!s_blocked&&s_remaining==15000&&clicks==1&&filtered==1);
 role=ZM_NET_SURVIVOR;self=1;g_roomId=ROOM_TRAP_ROOM;g_omodel_table[0]=ceiling;
 rec[13]=ROOM_LIVING_ROOM;assert(!zm_shotgun_door(rec));
 rec[13]=ROOM_TRAP_PASSAGE;assert(zm_shotgun_door(rec));assert(!s_request);
 g_roomId=ROOM_LIVING_ROOM;rec[13]=ROOM_TRAP_ROOM;assert(!zm_shotgun_door(rec));
 role=ZM_NET_ZOMBIE;self=0;peers[1].room=ROOM_LIVING_ROOM;
 zm_shotgun_frame();assert(!s_active&&s_blocked&&!zm_shotgun_crushed(1));
 role=ZM_NET_SURVIVOR;self=1;rec[13]=ROOM_TRAP_ROOM;assert(zm_shotgun_door(rec));assert(g_MessagePtr==s_doorBlocked.bytes);
 g_roomId=ROOM_TRAP_ROOM;zm_shotgun_room();assert(*(int*)(ceiling+0x38)==-3650);now+=1000;zm_shotgun_room();assert(*(int*)(ceiling+0x38)==-3267);now+=2000;zm_shotgun_room();assert(*(int*)(ceiling+0x38)==-2500);assert(zm_shotgun_hide_room());
 Entity crushedMonster;crushedMonster.health=-1;assert(zombie_mode_shotgun_corpse(&crushedMonster));
 slots[0]=ITEM_PICK_AXE;slots[1]=1;g_playerEntity.scaMatrixData.localMatrix.t[0]=1500;g_playerEntity.scaMatrixData.localMatrix.t[2]=5000;
 assert(!zm_shotgun_use_pickaxe());
 g_roomId=ROOM_LIVING_ROOM;g_playerEntity.scaMatrixData.localMatrix.t[0]=11000;g_playerEntity.scaMatrixData.localMatrix.t[2]=6000;
 g_bItemMenuSelectedItemId=ITEM_SHOTGUN;assert(zm_shotgun_can_return_shotgun()&&!menu_item_equips_selected());
 g_bItemMenuSelectedItemId=2;assert(menu_item_equips_selected());
 g_bItemMenuSelectedItemId=ITEM_SHOTGUN;g_playerEntity.scaMatrixData.localMatrix.t[0]=1000;assert(menu_item_equips_selected());
 g_playerEntity.scaMatrixData.localMatrix.t[0]=11000;armed=false;assert(menu_item_equips_selected());armed=true;
 assert(!zm_shotgun_menu_finished());
 // Returning the shotgun resets the slab and preserves a new pickup generation.
 role=ZM_NET_ZOMBIE;self=0;g_roomId=ROOM_TRAP_PASSAGE;g_omodel_table[0]=doorModel;
 peers[2].x=11000;peers[2].z=6000;peerItem[2]=ITEM_BROKEN_SHOTGUN;
 request[0]=1;request[1]=ITEM_BROKEN_SHOTGUN;zm_shotgun_take(request,2);
 assert(s_placed==1&&!s_blocked&&s_serial==1);
 short pickup[8]={5,0,0,257,(short)(5|(22<<8)),(short)(28|(1<<8)),0x5678,0x1234};
 assert(zm_shotgun_pickup_valid(pickup));
 peers[1].room=ROOM_TRAP_ROOM;FUN_00473f10((int*)g_roomItemsFlags,1);zm_shotgun_pickup(pickup);
 assert(s_active&&!s_placed&&!zm_shotgun_pickup_valid(pickup));
 // Action on the door never starts an axe request; inventory USE does.
 role=ZM_NET_SURVIVOR;self=3;g_playerEntity.scaMatrixData.localMatrix.t[0]=8000;g_playerEntity.scaMatrixData.localMatrix.t[2]=9500;
 rec[13]=ROOM_TRAP_ROOM;assert(zm_shotgun_door(rec)&&!s_request);
 clearArea=false;assert(!zm_shotgun_use_pickaxe());assert(g_MessagePtr==s_clearArea.bytes);
 clearArea=true;assert(zm_shotgun_use_pickaxe()&&s_request==2);
 role=ZM_NET_ZOMBIE;self=0;peerItem[3]=ITEM_PICK_AXE;request[0]=2;
 trapClear=false;zm_shotgun_take(request,3);assert(!s_broken);trapClear=true;
 clearArea=false;zm_shotgun_take(request,3);assert(!s_broken);clearArea=true;
 zm_shotgun_take(request,3);assert(s_broken&&!s_active&&!s_blocked&&s_rescuedMask==14&&s_breaker==3&&s_fadeAt>now);
 assert(zm_shotgun_room_blocked(5,21)&&zm_shotgun_room_blocked(5,22)&&!zm_shotgun_room_blocked(5,9));
 rec[13]=ROOM_TRAP_ROOM;assert(zm_shotgun_door(rec));
 assert(!zm_shotgun_route_open(5,9,5,21,0xE00,true));
 g_roomId=ROOM_TRAP_ROOM;rec[13]=ROOM_LIVING_ROOM;assert(zm_shotgun_door(rec));g_roomId=ROOM_TRAP_PASSAGE;
 assert(zm_shotgun_draw());
 Message rescue=messages.back();
 role=ZM_NET_SURVIVOR;self=3;s_broken=false;s_consumedAxe=false;s_request=2;
 g_openMenuFlag=1;s_rescuedMask=0;
 zm_shotgun_take(rescue.a,0);assert(s_consumeAxe&&s_jump&&zm_shotgun_menu_finished()&&s_rescuedMask==14);
 now+=1000;zm_shotgun_frame();assert(slots[0]==ITEM_PICK_AXE&&s_jump); // waits for menu cleanup
 g_openMenuFlag=0;
 g_main_state_flags=0;g_message_flags=0;g_TotalHeldItems=1;g_EquippedItemId=1;
 zm_shotgun_frame();assert(slots[0]==0&&zm_shotgun_pickaxe_consumed()&&!zm_shotgun_menu_finished()&&s_fadeAt>now);
 ZmReconnectPlayer checkpoint;checkpoint.card.totalHeldItems=1;checkpoint.inventory[0]=ITEM_PICK_AXE;checkpoint.inventory[1]=1;
 zm_shotgun_reconcile(3,&checkpoint);assert(checkpoint.pickaxeSpent&&checkpoint.card.totalHeldItems==0&&checkpoint.card.roomId==ROOM_TRAP_PASSAGE);
 checkpoint.card.totalHeldItems=1;checkpoint.inventory[0]=ITEM_PICK_AXE;checkpoint.inventory[1]=1;
 zm_shotgun_reconcile(3,&checkpoint);assert(checkpoint.card.totalHeldItems==1);
 assert(zm_shotgun_monster_blocks(ROOM_TRAP_ROOM,1500,0,0,0));
 assert(zm_shotgun_monster_blocks(ROOM_TRAP_ROOM,1501,0,0,0));
 assert(!zm_shotgun_monster_blocks(ROOM_TRAP_PASSAGE,1000,7500,8000,9500));
 assert(!zm_shotgun_monster_blocks(ROOM_TRAP_PASSAGE,24000,17000,8000,9500));
 // Inside USE, immediate reaction on the activation message, and deadline.
 role=ZM_NET_ZOMBIE;self=0;zm_shotgun_reset();peers[1].room=ROOM_TRAP_ROOM;zm_shotgun_frame();
 Message activation=messages.back();role=ZM_NET_SURVIVOR;self=1;g_roomId=ROOM_TRAP_ROOM;s_active=false;
 zm_shotgun_take(activation.a,0);assert(g_playerEntity.lookAtFlags==0x13&&s_lookUntil>now);
 slots[0]=ITEM_PICK_AXE;slots[1]=1;g_playerEntity.scaMatrixData.localMatrix.t[0]=1500;g_playerEntity.scaMatrixData.localMatrix.t[2]=5000;
 assert(zm_shotgun_use_pickaxe());
 role=ZM_NET_ZOMBIE;self=0;now+=15000;zm_shotgun_frame();assert(s_blocked&&zm_shotgun_crushed(1)&&!zm_shotgun_crushed(2));
 unsigned int saved[16];zm_shotgun_export(saved);zm_shotgun_reset();zm_shotgun_import(saved);assert(s_blocked&&zm_shotgun_crushed(1));
 solvable=-1;now+=2000;zm_shotgun_frame();assert(!ended);solvable=1;zm_shotgun_frame();assert(!ended);solvable=0;now+=1000;zm_shotgun_frame();assert(ended);
    // Permanent deaths suppress drops and revival, including late awards.
    ended=false;role=ZM_NET_SURVIVOR;self=1;g_playerEntity.health=-1;
    peers[1].dead=peers[1].spectating=true;s_deathSeen[1]=true;
    slots[0]=ITEM_SHOTGUN;slots[1]=7;g_TotalHeldItems=1;
    zm_drops_on_death();assert(floorDrops==0&&g_TotalHeldItems==0&&!zm_revive_eligible(1));
    assert(!zm_shotgun_route_open(5,22,5,21,0,true));
    assert(zm_shotgun_route_open(5,22,5,21,0x400,true));
    assert(!zm_shotgun_route_open(5,9,5,21,0x800,false)); // completed slab cannot be axed
    role=ZM_NET_ZOMBIE;self=0;peerItem[2]=ITEM_SHOTGUN;request[0]=1;request[1]=ITEM_SHOTGUN;request[2]=s_serial;
    zm_shotgun_take(request,2);assert(s_placed==2&&!s_blocked&&s_plateQty==1);
    unsigned int receipt=s_serial;
    short lifted[8]={5,0,0,(short)(1|((s_serial&255)<<8)),(short)(5|(22<<8)),(short)(3|(1<<8)),0x5678,0x1234};
    assert(zm_shotgun_pickup_valid(lifted)&&!zm_shotgun_pickup_valid(pickup));
    FUN_00473f10((int*)g_roomItemsFlags,1);zm_shotgun_pickup(lifted);
    zm_shotgun_take(request,2);assert(s_serial==receipt&&!s_placed); // stale retry cannot place another gun
    ZmReconnectPlayer returned;returned.card.totalHeldItems=1;returned.inventory[0]=ITEM_SHOTGUN;returned.inventory[1]=1;
    zm_shotgun_reconcile(2,&returned);assert(!returned.card.totalHeldItems&&returned.shotgunReplacement==receipt);
    returned.card.totalHeldItems=1;returned.inventory[0]=ITEM_SHOTGUN;returned.inventory[1]=1;
    zm_shotgun_reconcile(2,&returned);assert(returned.card.totalHeldItems==1);
    // Native models and action-only pickup: returning a working shotgun must
    // not show the broken prop or turn the mount into a walk-in prompt.
    unsigned char gunModel[0xA4]={},brokenModel[0xA4]={},plateRecord[26]={};
    unsigned char* entry=g_RoomActionTable+2*12;*(unsigned char**)(entry+8)=plateRecord;
    g_item_model_table[0]=gunModel;g_item_model_table[1]=brokenModel;
    role=ZM_NET_ZOMBIE;self=0;g_roomId=ROOM_LIVING_ROOM;request[2]=s_serial;
    zm_shotgun_take(request,2);assert(s_placed==2);zm_shotgun_room();
    assert(gunModel[0]==1&&brokenModel[0]==0&&entry[0]==4&&entry[1]==0x81&&plateRecord[10]==0);
    // NO leaves the pickup available but automatic position probes skip 0x80.
    zm_shotgun_room();assert(entry[1]==0x81);
    lifted[3]=(short)(1|((s_serial&255)<<8));FUN_00473f10((int*)g_roomItemsFlags,1);
    zm_shotgun_room();assert(entry[0]==0&&gunModel[0]==0&&brokenModel[0]==0);
    // Another player's committed pickup clears the model and interaction on
    // this copy; frame updates must never resurrect its stale pickup prompt.
    zm_shotgun_pickup(lifted);zm_shotgun_room();assert(entry[0]==0&&!s_placed);
    peerItem[2]=ITEM_BROKEN_SHOTGUN;request[1]=ITEM_BROKEN_SHOTGUN;request[2]=s_serial;
    zm_shotgun_take(request,2);zm_shotgun_room();
    assert(gunModel[0]==0&&brokenModel[0]==1&&plateRecord[10]==1&&entry[1]==0x81);
    // Inside rescue includes the breaker and all living occupants of both
    // rooms; arrival positions and reconnect recovery agree for every seat.
    role=ZM_NET_ZOMBIE;self=0;zm_shotgun_reset();g_roomId=ROOM_TRAP_ROOM;
    peers[1].dead=peers[1].spectating=false;peers[1].room=ROOM_TRAP_ROOM;peers[1].x=1500;peers[1].z=5000;
    peers[2].room=ROOM_LIVING_ROOM;peers[3].room=ROOM_TRAP_ROOM;peerItem[1]=ITEM_PICK_AXE;
    zm_shotgun_frame();request[0]=2;zm_shotgun_take(request,1);assert(s_rescuedMask==14&&s_breaker==1);
    int ax[4]={},az[4]={};short facing[4]={};
    for(int i=1;i<=3;i++){
      shotgun_arrival(i,&ax[i],&az[i],&facing[i]);
      ZmReconnectPlayer cp;cp.card.roomId=(i==2?ROOM_LIVING_ROOM:ROOM_TRAP_ROOM);
      zm_shotgun_reconcile(i,&cp);assert(cp.card.roomId==ROOM_TRAP_PASSAGE&&cp.x==ax[i]&&cp.z==az[i]&&cp.angle==facing[i]);
    }
    assert(ax[1]==7000&&az[1]==9400&&ax[2]==5800&&az[2]==10300&&ax[3]==6500&&az[3]==11800);
    assert(facing[1]==0xA00&&facing[2]==0x1A0&&facing[3]==0x37A);
    s_breaker=3;shotgun_arrival(3,&ax[3],&az[3],&facing[3]);assert(az[3]==9400);
    shotgun_arrival(1,&ax[1],&az[1],&facing[1]);shotgun_arrival(2,&ax[2],&az[2],&facing[2]);
    assert(az[1]==10300&&az[2]==11800);
    role=ZM_NET_SURVIVOR;self=1;g_roomId=ROOM_TRAP_PASSAGE;
    s_arrivalCamera=true;g_roomTransitionBusy=1;zm_shotgun_frame();assert(s_arrivalCamera);
    g_roomTransitionBusy=0;g_openMenuFlag=0;g_main_state_flags=0;
    int repairs=cameraRepairs;zm_shotgun_frame();assert(!s_arrivalCamera&&cameraRepairs==repairs+1);
    zm_shotgun_frame();assert(cameraRepairs==repairs+1);
    // Both replacement types request full inventory cleanup only after the
    // host accepts, then consume outside the menu without reopening it.
    for (unsigned char item : { (unsigned char)ITEM_SHOTGUN, (unsigned char)ITEM_BROKEN_SHOTGUN }) {
      role=ZM_NET_ZOMBIE;self=0;zm_shotgun_reset();g_roomId=ROOM_LIVING_ROOM;
      peerItem[2]=item;request[0]=1;request[1]=item;request[2]=0;zm_shotgun_take(request,2);
      Message placed=messages.back();role=ZM_NET_SURVIVOR;self=2;s_request=1;s_requestSerial=0;s_requestItem=item;
      slots[0]=item;slots[1]=1;g_TotalHeldItems=1;g_openMenuFlag=1;
      zm_shotgun_take(placed.a,0);assert(zm_shotgun_menu_finished());zm_shotgun_frame();assert(slots[0]==item);
      g_openMenuFlag=0;zm_shotgun_frame();assert(!slots[0]&&!zm_shotgun_menu_finished());
    }
    // Original dialogue requires Barry to rescue Jill; it starts after the
    // fade, runs each recording once, and never holds gameplay or a menu.
    zm_shotgun_reset();g_roomId=ROOM_TRAP_PASSAGE;g_openMenuFlag=0;s_breaker=2;s_rescuedMask=14;
    peerChar[2]=ZM_CHAR_BARRY;peerChar[1]=ZM_CHAR_JILL;
    shotgun_rescue_dialogue();assert(s_rescueVoiceNext==5);
    s_jump=true;shotgun_rescue_voice(true,true);assert(!voiceStarts);
    s_jump=false;s_fadeAt=now+900;shotgun_rescue_voice(true,true);assert(!voiceStarts);
    now+=901;shotgun_rescue_voice(true,false);assert(!voiceStarts);
    shotgun_rescue_voice(true,true);assert(voiceStarts==1&&strstr(voicePath,"V105_05.wav"));
    shotgun_rescue_voice(true,true);assert(voiceStarts==1);
    voicePlaying=false;shotgun_rescue_voice(true,true);assert(voiceStarts==1);
    now+=1999;shotgun_rescue_voice(true,true);assert(voiceStarts==1);
    now++;shotgun_rescue_voice(true,true);assert(voiceStarts==2&&strstr(voicePath,"V105_06.wav"));
    voicePlaying=false;shotgun_rescue_voice(true,true);assert(!s_rescueVoiceNext&&!s_rescueVoiceBank);
    s_rescuedMask=12;shotgun_rescue_dialogue();assert(!s_rescueVoiceNext);
    s_rescuedMask=14;peerChar[2]=0;shotgun_rescue_dialogue();assert(!s_rescueVoiceNext);
    peerChar[2]=ZM_CHAR_BARRY;shotgun_rescue_dialogue();now+=8001;
    shotgun_rescue_voice(true,true);assert(!s_rescueVoiceNext&&voiceStarts==2);
    shotgun_rescue_dialogue();shotgun_rescue_voice(true,true);assert(s_rescueVoiceBank);
    g_roomId=ROOM_LIVING_ROOM;shotgun_rescue_voice(true,true);assert(!s_rescueVoiceBank&&!s_rescueVoiceNext);
    zm_shotgun_reset();
    // Startup-generated icon and text are optional-mode overrides only.
    g_ItemImageLookupTable[ITEM_PICK_AXE*4]=73;
    unsigned char tim[20+3*512]={}; tim[0]=0x10; tim[4]=9; tim[17]=1; tim[18]=3;
    for(int i=1;i<256;i++){unsigned short c=(unsigned short)((i&31)|((i&31)<<5)|((i&31)<<10));
      tim[20+2*512+i*2]=(unsigned char)c; tim[21+2*512+i*2]=(unsigned char)(c>>8);}
    zombie_mode_pickaxe_icon_init(tim,sizeof(tim)); assert(s_iconReady);
    assert(zombie_mode_pickaxe_icon(72) && !zombie_mode_pickaxe_icon(71));
    int pixels=0; for(auto c:s_pickaxeIcon) if(c) pixels++; assert(pixels>80 && pixels<400);
    assert(s_pickaxeIcon[0]==0 && s_pickaxeIcon[39]==0 && s_pickaxeIcon[1160]==0 && s_pickaxeIcon[1199]==0);
    assert(zombie_mode_pickaxe_name(ITEM_PICK_AXE) && !zombie_mode_pickaxe_name(3));
    static constexpr auto uppercase=STR("PICKAXE\x07");
    assert(!memcmp(s_pickaxeName.bytes,uppercase.bytes,8) && s_pickaxeName.bytes[7]==7);
    // Walk the native pickup message through the real name substitution.
    // It must return to '?' and then tag 8, not stop at a name terminator.
    const unsigned char *cursor=s_nativePickup.bytes, *resume=nullptr;
    bool question=false,prompt=false;
    for(int n=0;n<100&&!prompt;n++) {
        unsigned char c=*cursor++;
        if(c==1)break;
        if(c==5){cursor++;continue;}
        if(c==6){cursor++;resume=cursor;cursor=zombie_mode_pickaxe_name(ITEM_PICK_AXE);continue;}
        if(c==7){assert(resume);cursor=resume;continue;}
        if(c==0x1B)question=true;
        if(c==8)prompt=true;
    }
    assert(question && prompt);
    assert(zombie_mode_pickaxe_description(ITEM_PICK_AXE-1));
    // Encoded newlines, no raw LF byte (which hangs the native message VM).
    int lines=0; for(auto c:s_pickaxeDescription.bytes){assert(c!=10);if(c==2)lines++;} assert(lines==1);
    armed=false; assert(!zombie_mode_pickaxe_icon(72) && !zombie_mode_pickaxe_name(ITEM_PICK_AXE));
    zombie_mode_pickaxe_icon_init(tim,10); assert(!s_iconReady);
    // A survivor already in the room descends, sees a remote cancellation,
    // then watches the slab return at the same speed without snapping up.
    armed=true;role=ZM_NET_SURVIVOR;g_roomId=ROOM_TRAP_ROOM;
    zm_shotgun_reset();now=1000;s_active=true;s_deadline=now+ZM_SHOTGUN_MS;
    assert(shotgun_ceiling_depth()==0);
    now+=6000;assert(shotgun_ceiling_depth()==6000);
    s_active=false;assert(shotgun_ceiling_depth()==6000);
    now+=1000;assert(shotgun_ceiling_depth()==5000);
    now+=4000;assert(shotgun_ceiling_depth()==1000);
    now+=1000;assert(shotgun_ceiling_depth()==0);
    now+=1000;assert(shotgun_ceiling_depth()==0);
    puts("Shotgun state machine OK");
}
'''

source = (routes.ROOT / "src/game/mods/ZombieShotgun.cpp").read_text()
source = re.sub(r'^#include[^\n]*\n', '', source, flags=re.M)
menu_source = (routes.ROOT / 'src/game/MainMenu.cpp').read_text()
source += 'static bool menu_item_equips_selected(void)\n{' + routes.between(
    menu_source, 'static bool menu_item_equips_selected(void)\n{', '// (0x00401050) - Equip')
native_pickup = (routes.ROOT / 'src/Globals.cpp').read_text()
native_pickup = re.search(r'static constexpr auto s_gm00 = STR\([^\n]+', native_pickup).group(0)
source += native_pickup.replace('s_gm00', 's_nativePickup') + '\n'
drop_source = (routes.ROOT / 'src/game/mods/ZombieDrops.cpp').read_text()
source += 'static void zm_drops_on_death(void)\n{' + routes.between(
    drop_source, 'static void zm_drops_on_death(void)\n{', '// The DROP row')
revive_source = (routes.ROOT / 'src/game/mods/ZombieSpectate.cpp').read_text()
source += 'static bool zm_revive_eligible(int i)\n{' + routes.between(
    revive_source, 'static bool zm_revive_eligible(int i)\n{', 'static int zm_revive_item')
FIXTURE = FIXTURE.replace('../src/game/PrintText.h', (routes.ROOT / 'src/game/PrintText.h').as_posix())
with tempfile.TemporaryDirectory(prefix="re1-shotgun-tests-") as temp:
    with patch.object(routes, "adapter_source", return_value=FIXTURE + source + CHECKS):
        executable = routes.build_adapter(Path(temp))
    subprocess.run([str(executable)], check=True)
