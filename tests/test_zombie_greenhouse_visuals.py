"""CPU tests of shared greenhouse effects; never launches the game."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import evaluate_zombie_routes as routes

source = (routes.ROOT / "src/game/mods/ZombieGreenhouse.cpp").read_text()
source = re.sub(r'^#include[^\n]*\n', '', source, flags=re.M)
source = re.sub(r'^extern [^\n]*\n', '', source, flags=re.M)
mode = (routes.ROOT / "src/game/mods/ZombieMode.cpp").read_text()
commit = "bool zombie_mode_skip_scd_event(int scriptIndex)\n{" + routes.between(
    mode, "bool zombie_mode_skip_scd_event(int scriptIndex)\n{", "    // The multiplayer shotgun module") + "return false; }\n"
fixture = r'''
#include <cassert>
#include <cstdio>
enum { STAGE_MANSION_RETURN_1F=5, ROOM_GREENHOUSE=12, MSF_ROOM_TRANSITION=1, MSF_MENU_ACTIVE=2 };
static bool s_zombieModeArmed=true, solved=false;
static int g_stageId=5, g_roomId=12, g_roomTransitionBusy=0, g_main_state_flags=0;
static unsigned char g_ScenarioFlags2[32], sentinel, *g_ScdOpcodes=&sentinel;
static unsigned int now=1000;
static int clears, spawns, water, plants;
bool zombie_mode_armed() { return s_zombieModeArmed; }
unsigned int zm_game_time_ms() { return now; }
int Flg_ck(int, unsigned int bit) { assert(bit==0xA6); return solved; }
void Flg_on(int, unsigned int bit) { assert(bit==0xA6); solved=true; }
int cmd_effect_clear_typed() { assert(g_ScdOpcodes[0]==0x42); ++clears; g_ScdOpcodes+=4; return 1; }
int cmd_effect_spawn() { assert(g_ScdOpcodes[0]==0x2A && g_ScdOpcodes[2]==8); ++spawns; g_ScdOpcodes+=12; return 1; }
void scd_model_tint_apply(short r, short g, short b, unsigned short a, unsigned short z, unsigned char id) {
 assert(r==2 && b==0 && a==0);
 if (id==0x80) { assert(g==0 && z==0xFF); ++water; }
 else { assert(id==15 && g==-3 && z==0x100); ++plants; }
}
'''
checks = r'''
int main() {
 // Two already-loaded copies observe the same immediate STORY commit.
 initiator::zm_greenhouse_room(); observer::zm_greenhouse_room();
 initiator::zm_greenhouse_frame(); observer::zm_greenhouse_frame();
 assert(clears==0 && spawns==0 && water==0 && plants==0);
 assert(zombie_mode_skip_scd_event(1) && solved);
 initiator::zm_greenhouse_frame(); observer::zm_greenhouse_frame();
 assert(clears==4 && spawns==4 && water==2 && plants==2);
 assert(g_ScdOpcodes==&sentinel && g_main_state_flags==0);
 now+=8*33; initiator::zm_greenhouse_frame(); observer::zm_greenhouse_frame();
 assert(water==4 && plants==2 && spawns==4);
 now+=10000; initiator::zm_greenhouse_frame(); observer::zm_greenhouse_frame();
 assert(water==20 && spawns==4);
 initiator::zm_greenhouse_frame(); observer::zm_greenhouse_frame();
 assert(water==20 && plants==2); // idempotent while solved
 // Re-entry/reconnect restores final water appearance immediately.
 observer::zm_greenhouse_room(); observer::zm_greenhouse_frame();
 assert(water==30 && spawns==6 && plants==3);
 // Defer effects until the loaded room's objects/textures are usable.
 observer::zm_greenhouse_room(); g_roomTransitionBusy=1; observer::zm_greenhouse_frame();
 assert(water==30 && spawns==6);
 g_roomTransitionBusy=0; g_main_state_flags=MSF_MENU_ACTIVE; observer::zm_greenhouse_frame();
 assert(water==30); g_main_state_flags=0; observer::zm_greenhouse_frame(); assert(water==40);
 // Unrelated rooms and normal USA play retain native event handling.
 g_roomId=13; assert(!zombie_mode_skip_scd_event(1)); observer::zm_greenhouse_frame(); assert(water==40);
 g_roomId=12; s_zombieModeArmed=false; assert(!zombie_mode_skip_scd_event(1));
 observer::zm_greenhouse_frame(); assert(water==40);
 puts("Shared greenhouse start, water tint, re-entry and uninterrupted control checks passed");
}
'''
with tempfile.TemporaryDirectory(prefix="re1-greenhouse-visuals-") as temp:
    code = fixture + commit + "namespace initiator {" + source + "}\nnamespace observer {" + source + "}\n" + checks
    with patch.object(routes, "adapter_source", return_value=code):
        executable = routes.build_adapter(Path(temp))
    subprocess.run([str(executable)], check=True)
