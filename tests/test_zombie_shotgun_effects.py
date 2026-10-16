"""CPU regressions for real trap clearance and through-door PCM filtering."""
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import evaluate_zombie_routes as routes

def function(source, name):
    start = source.index(name + "(")
    start = source.rfind("\n", 0, start) + 1
    brace = source.index("{", start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"

fixture = r'''
#include <cassert>
#include <cstring>
#include <cstdio>
#include "AUDIO_HEADER"
enum {ROOM_TRAP_PASSAGE=9,ROOM_TRAP_ROOM=21,ROOM_LIVING_ROOM=22,STAGE_MANSION_RETURN_1F=5,ZM_NET_SURVIVOR=2,ZM_NET_ZOMBIE=1,ENTITY_STATUS_ACTIVE=1,ZM_ROSTER_MAX=1024,MSF_CAMERA_LOCK=0x100000};
int g_stageId=5,g_roomId=9;
int role=1;int zm_net_role(){return role;}
#define ZM_FIRST_SURVIVOR_SLOT 26
struct CAM_SWITCH_ZONE {short camFrom,camTo;};
CAM_SWITCH_ZONE zones[]={{0,0},{1,1},{2,2},{3,3},{4,4},{-1,-1}};
struct Rdt {void* cam_switch_zones=zones;unsigned short cameras_count=5;} rdt;
Rdt* g_RdtPointer=&rdt;void* g_CurrentRdtDataTypePtr;
int g_main_state_flags=0,g_roomCameraId=2,g_cutId=0,cuts=0;
void cut_set(){cuts++;}
struct Entity {int id=0,state=0,action_state=0,status_flags=0,health=100;struct {struct {int t[3]={};} localMatrix;} scaMatrixData;};
Entity g_EnemiesList[27];
Entity* g_zombieModeEntity=g_EnemiesList;
#define NPC_ENTITIES_IDS 0x20
#define ZM_TRAP_CAMERA 3
static Entity* hiddenEntity;
bool zombie_mode_hide_entity(const Entity* e) {return e==hiddenEntity;}
int s_gameRole=1,handoffs=0;bool s_zombieDeathReported=false;
bool zm_possessed_died(Entity*) {handoffs++;return true;}
struct ZmRosterEntry {bool used=false,alive=false,departed=false;unsigned char stage=5,room=9,id=0;unsigned short uid=0;short x=0,z=0,health=100;};
int refunds=0;void zm_econ_refund(unsigned char,unsigned short){refunds++;}
ZmRosterEntry s_roster[ZM_ROSTER_MAX];
int zm_world_slot_of_uid(unsigned short uid) {return uid==1?0:-1;}
'''
checks = r'''
int main() {
 assert(zm_world_shotgun_clear(5,9,8000,9500));
 auto &enemy=g_EnemiesList[0];enemy.status_flags=1;enemy.scaMatrixData.localMatrix.t[0]=8000;enemy.scaMatrixData.localMatrix.t[2]=9500;
 assert(!zm_world_shotgun_clear(5,9,8000,9500));
 enemy.health=-1;assert(zm_world_shotgun_clear(5,9,8000,9500));
 auto &roster=s_roster[0];roster.used=roster.alive=true;roster.uid=1;roster.x=8000;roster.z=9500;
 assert(zm_world_shotgun_clear(5,9,8000,9500)); // stale alive entry cannot revive a killed local monster
 g_roomId=21;assert(!zm_world_shotgun_clear(5,9,8000,9500)); // host validates a remote hallway
 roster.departed=true;assert(zm_world_shotgun_clear(5,9,8000,9500));
 roster.departed=false;roster.x=24000;roster.z=17000;assert(zm_world_shotgun_clear(5,9,8000,9500));
 roster.room=21;roster.x=1500;roster.z=5000;roster.uid=2;
 assert(!zm_world_shotgun_clear(5,21,1500,5000)); // inside axe user protected too
 roster.alive=false;assert(zm_world_shotgun_clear(5,21,1500,5000));
 // A hidden entity (the unused director-body stand-in) is not in the way.
 roster.alive=false;g_roomId=ROOM_TRAP_ROOM;enemy.health=100;enemy.status_flags=ENTITY_STATUS_ACTIVE;
 assert(!zm_world_shotgun_clear(5,21,1500,5000));
 hiddenEntity=&enemy;assert(zm_world_shotgun_clear(5,21,1500,5000));hiddenEntity=nullptr;
 // Switch away from the overhead intro and hold the from-below shot (camera
 // 3, looking up at the ceiling) for every role.
 g_roomCameraId=0;
 zm_shotgun_crush_view(true);assert(g_roomCameraId==3&&cuts==1&&(g_main_state_flags&MSF_CAMERA_LOCK));
 zm_shotgun_crush_view(true);assert(cuts==1);zm_shotgun_crush_view(false);assert(!(g_main_state_flags&MSF_CAMERA_LOCK));
 enemy.health=100;roster.alive=true;g_roomId=ROOM_TRAP_ROOM;
 zm_world_shotgun_crush();assert(enemy.health==-1&&!roster.alive&&roster.health==-1&&enemy.state==3&&enemy.action_state==3);
 zm_shotgun_crush_view(true);zm_shotgun_director_crush();assert(handoffs==1&&!(g_main_state_flags&MSF_CAMERA_LOCK));
 zm_shotgun_director_crush();assert(handoffs==1);
 s_zombieDeathReported=false;s_gameRole=ZM_NET_SURVIVOR;zm_shotgun_director_crush();assert(handoffs==1);
 enemy.health=100;g_roomId=9;zm_world_shotgun_crush();assert(enemy.health==100);
 roster.room=ROOM_LIVING_ROOM;roster.alive=true;g_roomId=ROOM_LIVING_ROOM;
 int beforeRefund=refunds;zm_world_shotgun_seal();assert(enemy.health<0&&!roster.alive&&refunds==beforeRefund+1);
 zm_world_shotgun_seal();assert(refunds==beforeRefund+1);
 unsigned char pcm8[200];for(int i=0;i<200;i++)pcm8[i]=i%2?255:0;
 AudioFileData a={};a.pcm=pcm8;a.pcmSize=200;a.channels=1;a.bitsPerSample=8;a.sampleRate=22050;
 AudioFile_Muffle(&a);for(int i=100;i<200;i++)assert(pcm8[i]>105&&pcm8[i]<150);
 short pcm16[200];for(int i=0;i<100;i++){pcm16[i*2]=20000;pcm16[i*2+1]=-20000;}
 a.pcm=(unsigned char*)pcm16;a.pcmSize=sizeof(pcm16);a.channels=2;a.bitsPerSample=16;
 AudioFile_Muffle(&a);assert(pcm16[0]>0&&pcm16[0]<20000&&pcm16[1]<0&&pcm16[1]>-20000);
 assert(pcm16[198]>19900&&pcm16[199]<-19900);
 unsigned char before[sizeof(pcm16)];memcpy(before,pcm16,sizeof(before));a.channels=3;
 AudioFile_Muffle(&a);assert(!memcmp(before,pcm16,sizeof(before)));AudioFile_Muffle(nullptr);
 puts("Trap monster clearance and muffled PCM checks passed.");
}
'''
shotgun = (routes.ROOT / "src/game/mods/ZombieShotgun.cpp").read_text()
world = (routes.ROOT / "src/game/mods/ZombieWorld.cpp").read_text()
mode = (routes.ROOT / "src/game/mods/ZombieMode.cpp").read_text()
audio = (routes.ROOT / "src/system/AudioFile.cpp").read_text()
code = fixture.replace("AUDIO_HEADER", (routes.ROOT / "src/system/AudioFile.h").as_posix())
code += function(shotgun,"zm_shotgun_monster_blocks") + function(world,"zm_world_shotgun_clear") + function(world,"zm_world_shotgun_kill") + function(world,"zm_world_shotgun_crush") + function(world,"zm_world_shotgun_seal") + function(mode,"zm_shotgun_crush_view") + function(mode,"zm_shotgun_director_crush") + function(audio,"AudioFile_Muffle") + checks
with tempfile.TemporaryDirectory(prefix="re1-trap-effects-") as temp:
    with patch.object(routes,"adapter_source",return_value=code):
        executable = routes.build_adapter(Path(temp))
    subprocess.run([str(executable)],check=True)
