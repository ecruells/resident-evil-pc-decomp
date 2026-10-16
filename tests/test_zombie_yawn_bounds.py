"""Exercise the production possessed-Yawn guard without launching the game."""
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import evaluate_zombie_routes as routes

source = (ROOT / "src/game/mods/ZombieYawn.cpp").read_text()
guard = source.split("// A possessed snake's animator", 1)[1].split(
    "static bool yw_beaten", 1)[0]
guard = "// A possessed snake's animator" + guard
fixture = r'''
#include <initializer_list>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
struct VECTOR { int x,y,z,pad; };
struct SVECTOR { short x,y,z,pad; };
struct MATRIX { int t[3]; };
struct JointStruct { MATRIX world; };
struct Entity {
 struct { MATRIX localMatrix; } scaMatrixData;
 JointStruct* jointsStructs;
 short angle;
 struct { short x,y,z; } position;
 unsigned char action_behavior,action_state,ignore_player_flag,jointCount,state;
 unsigned char padding[512];
};
struct CAM_SWITCH_ZONE { short camFrom; };
struct RDT { void* cam_switch_zones; int cameras_count; };
static CAM_SWITCH_ZONE zones[2]={{0},{1}};
static RDT room={zones,1}, *g_RdtPointer=&room;
static VECTOR g_playerPosScratch={7,8,9,0};
static bool wall=false;
static struct { int x,z; } kBoss[1]={{1000,1000}};
bool zombie_mode_armed() { return true; }
int yw_here() { return 0; }
void dbg_printf(const char*, ...) {}
void zm_yawn_bounds_end(Entity* e);
int is_entity_in_switch_zone(VECTOR* p, void*) {
 return p->x>=0 && p->z>=0 && p->x<=10000 && p->z<=10000;
}
short room_collision_check_0047da50(VECTOR* p, VECTOR*) {
 g_playerPosScratch=*p;
 return wall && p->x>=1950 && p->x<=2150 ? 1 : 0;
}
'''
checks = r'''
int main() {
 Entity e={}; JointStruct joints[15]={}; e.jointsStructs=joints; e.jointCount=15;
 e.scaMatrixData.localMatrix.t[0]=1000; e.scaMatrixData.localMatrix.t[2]=1000;
 for(auto& j:joints) { j.world.t[0]=1000; j.world.t[2]=1000; }
 zm_yawn_bounds_begin(&e);
 // Normal movement remains legal.
 e.scaMatrixData.localMatrix.t[0]=1100;
 zm_yawn_bounds_end(&e); assert(e.scaMatrixData.localMatrix.t[0]==1100);
 zm_yawn_bounds_begin(&e);
 // A long step cannot tunnel through a wall even with a legal endpoint.
 wall=true; e.scaMatrixData.localMatrix.t[0]=3000; e.action_behavior=9;
 joints[4].world.t[0]=3000;
 zm_yawn_bounds_end(&e);
 assert(e.scaMatrixData.localMatrix.t[0]>1100 && e.scaMatrixData.localMatrix.t[0]<1950);
 assert(joints[4].world.t[0]>1000 && joints[4].world.t[0]<1950);
 assert(e.action_behavior==9);
 // All attacks retain their state and animation while sliding along the wall.
 for (int attack : {2,9,10}) {
   zm_yawn_bounds_begin(&e);
   int oldZ=e.scaMatrixData.localMatrix.t[2];
   e.action_behavior=attack; e.action_state=3; e.angle=123;
   e.scaMatrixData.localMatrix.t[0]+=500;
   e.scaMatrixData.localMatrix.t[2]+=500;
   zm_yawn_bounds_end(&e);
   assert(e.scaMatrixData.localMatrix.t[0]<1950);
   assert(e.scaMatrixData.localMatrix.t[2]==oldZ+500);
   assert(e.action_behavior==attack && e.action_state==3 && e.angle==123);
 }
 assert(g_playerPosScratch.x==7 && g_playerPosScratch.z==9);
 // The head cannot leave coverage while the root remains in the room.
 wall=false; zm_yawn_bounds_begin(&e); joints[0].world.t[2]=12000;
 zm_yawn_bounds_end(&e); assert(joints[0].world.t[2]<=10000);
 // A later segment update cannot overwrite the safe snapshot with an escape.
 joints[3].world.t[2]=12000;
 zm_yawn_bounds_begin(&e); assert(joints[3].world.t[2]<=10000);
 // Escaping between updates restores the saved pose.
 e.scaMatrixData.localMatrix.t[0]=-500;
 zm_yawn_bounds_begin(&e); assert(e.scaMatrixData.localMatrix.t[0]>=0 && e.scaMatrixData.localMatrix.t[0]<=10000);
 // With no saved pose, recover to the boss's spawn and carry the body too.
 s_boundSaved=false; s_boundHead=nullptr;
 e.scaMatrixData.localMatrix.t[0]=12000;
 for(auto& j:joints) j.world.t[0]=12000;
 zm_yawn_bounds_begin(&e);
 assert(e.scaMatrixData.localMatrix.t[0]==1000 && joints[14].world.t[0]==1000);
 // Slam poses cross walls without clipping or replacing a movement snapshot.
 e.state=1; e.action_behavior=10;
 zm_yawn_bounds_begin(&e);
 e.scaMatrixData.localMatrix.t[0]=12000; joints[4].world.t[0]=12000;
 zm_yawn_bounds_end(&e);
 assert(e.scaMatrixData.localMatrix.t[0]==12000 && joints[4].world.t[0]==12000);
 assert(!s_boundSaved);
 // Recovery restores the origin; ordinary bounds resume from that position.
 e.scaMatrixData.localMatrix.t[0]=1000;
 for(auto& j:joints) { j.world.t[0]=1000; j.world.t[2]=1000; }
 e.action_behavior=1;
 zm_yawn_bounds_begin(&e);
 e.scaMatrixData.localMatrix.t[0]=12000;
 zm_yawn_bounds_end(&e);
 assert(e.scaMatrixData.localMatrix.t[0]<=10000);
 puts("Possessed Yawn boundary checks OK");
}
'''
with tempfile.TemporaryDirectory(prefix="re1-yawn-bounds-") as temp:
    with patch.object(routes, "adapter_source", return_value=fixture + guard + checks):
        executable = routes.build_adapter(Path(temp))
    subprocess.run([str(executable)], check=True)
