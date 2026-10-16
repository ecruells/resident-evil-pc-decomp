"""Exercise the actual upstairs-hole binding and preserve its B1 destination."""
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import evaluate_zombie_routes as routes
source=(ROOT/'src/game/mods/ZombieYawn.cpp').read_text()
hook=source[source.index('static unsigned char s_mainHoleDoor[24];'):source.index('// Yawn 2 beaten,')]
fixture=r'''
#include <cassert>
#include <cstring>
#include <cstdio>
#define STAGE_MANSION_RETURN_2F 6
#define ROOM_LESSON_ROOM 12
int g_stageId=6,g_roomId=12;
bool armed=true,beaten=false;
unsigned char g_RoomActionTable[32*12];
bool zombie_mode_armed(){return armed;}
bool zm_yawn_hole_open(){return beaten;}
'''
checks=r'''
int main(){
 unsigned char door[24];memset(door,0x55,sizeof(door));door[13]=0x1a;
 memset(g_RoomActionTable,0x55,sizeof(g_RoomActionTable));
 *(const unsigned char**)(g_RoomActionTable+20)=door;
 zm_yawn_hole_action();assert(g_RoomActionTable[24]==0);
 beaten=true;
 for(int i=0;i<3;i++){
  g_RoomActionTable[24]=0;g_RoomActionTable[48]=9;
  zm_yawn_hole_action();
  assert(g_RoomActionTable[24]==1 && g_RoomActionTable[25]==0x81);
  assert(*(unsigned short*)(g_RoomActionTable+26)==1);
  const unsigned char* bound=*(const unsigned char**)(g_RoomActionTable+32);
  assert(bound==s_mainHoleDoor);
  assert(memcmp(bound+8,door+8,16)==0 && bound[13]==0x1a);
  const unsigned short* zone=(const unsigned short*)bound;
  // Reach from the accessible east rim at (4900,3900), facing west.
  int px=4900-600,pz=3900;
  assert((unsigned)(px-zone[0])<=zone[2] && (unsigned)(pz-zone[1])<=zone[3]);
  assert(g_RoomActionTable[48]==0);
 }
 armed=false;g_RoomActionTable[24]=0;zm_yawn_hole_action();assert(g_RoomActionTable[24]==0);
 armed=true;g_roomId=11;zm_yawn_hole_action();assert(g_RoomActionTable[24]==0);
 puts("Upstairs hole uses Action door dispatch, reachable rim, and unchanged B1 destination.");
}
'''
with tempfile.TemporaryDirectory(prefix='re1-yawn-hole-') as temp:
 with patch.object(routes,'adapter_source',return_value=fixture+hook+checks):
  executable=routes.build_adapter(Path(temp))
 subprocess.run([str(executable)],check=True)
