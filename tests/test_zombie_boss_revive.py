"""Exercise production post-boss grace and corpse eligibility in a C++ harness."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import evaluate_zombie_routes as routes

source = (ROOT / "src/game/mods/ZombieYawn.cpp").read_text()

def function(signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

fixture = r'''
#include <cassert>
#include <cstring>
#define ZM_NET_MAX_PLAYERS 4
#define YW_BOSSES 2
#define YW_CLOCK_BONUS_MS 300000
enum { YW_WAITING, YW_LOCKDOWN, YW_DONE };
struct Boss { unsigned char stage, room; };
Boss kBoss[2] = {{6,16},{6,12}};
unsigned char s_phase[2], s_freeRevives[2];
unsigned int s_lockEnd[2], now;
bool beaten[2], armed = true;
struct ZmSurvivorInfo { int player; bool dead; unsigned char stage, room; };
ZmSurvivorInfo roster[4] = {{0,false,6,16},{1,true,6,16},{2,false,6,16},{3,true,6,5}};
int zm_survivor_list(ZmSurvivorInfo* out, int) { memcpy(out,roster,sizeof(roster)); return 4; }
bool zm_match_authority() { return true; }
bool zombie_mode_match_over() { return false; }
bool zombie_mode_armed() { return armed; }
bool yw_beaten(int b) { return beaten[b]; }
unsigned int zm_game_time_ms() { return now; }
void yw_lockdown(int b, unsigned int ms) { s_phase[b]=YW_LOCKDOWN; s_lockEnd[b]=now+ms; }
void yw_done(int b) { s_phase[b]=YW_DONE; s_freeRevives[b]=0; }
void yw_send(int, int, int) {}
void zm_clock_add_bonus(int) {}
int yw_boss_at(unsigned char stage, unsigned char room) {
    for(int b=0;b<2;b++) if(kBoss[b].stage==stage && kBoss[b].room==room) return b;
    return -1;
}
'''
fixture += re.search(r"#define YW_LOCKDOWN_MS\s+\d+", source)[0] + "\n"
fixture += function("static void yw_match_frame(void)") + "\n"
fixture += function("bool zm_yawn_free_revive(") + "\n"
fixture += function("void zm_yawn_revived(") + "\n"
fixture += r'''
int main() {
    assert(!zm_yawn_free_revive(1,6,16));
    beaten[0]=true; yw_match_frame();
    assert(s_lockEnd[0]==60000);
    assert(zm_yawn_free_revive(1,6,16));
    assert(!zm_yawn_free_revive(3,6,5));
    roster[2].dead=true; yw_match_frame(); // died after the fight
    assert(!zm_yawn_free_revive(2,6,16));
    assert(!zm_yawn_free_revive(1,6,12));
    assert(!zm_yawn_free_revive(-1,6,16));
    now=59999; yw_match_frame(); assert(zm_yawn_free_revive(1,6,16));
    zm_yawn_revived(1); assert(!zm_yawn_free_revive(1,6,16));
    yw_match_frame(); assert(!zm_yawn_free_revive(1,6,16)); // later death cannot renew it
    now=60000; yw_match_frame(); assert(s_phase[0]==YW_DONE);
    beaten[1]=true; roster[1].room=12; yw_match_frame();
    assert(zm_yawn_free_revive(1,6,12));
    now=120000; yw_match_frame(); assert(!zm_yawn_free_revive(1,6,12));
}
'''
with tempfile.TemporaryDirectory(prefix="re1-boss-revive-") as temp:
    with patch.object(routes, "adapter_source", return_value=fixture):
        executable = routes.build_adapter(Path(temp))
    subprocess.run([str(executable)], check=True)
print("Boss grace: 60 seconds; only fight corpses qualify, once, in the matching room.")
