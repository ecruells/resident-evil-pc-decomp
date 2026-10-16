"""CPU-only boss doorway regression tests; never launches the game."""
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import evaluate_zombie_routes as routes

source = (ROOT / "src/game/mods/ZombieYawn.cpp").read_text()
gate = source.split("bool zombie_mode_boss_door_gate(", 1)[1].split(
    "// The native message renderer", 1)[0]
fixture = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
enum { ZM_NET_SURVIVOR=2, ZM_NET_MAX_PLAYERS=4, YW_WAITING=0,
       YW_GATHER_RADIUS=3000, YW_OP_REQUEST=1 };
static int g_stageId=5, s_phase[1]={0}, s_askBoss=-1;
static bool beaten=false;
static struct { int health=100; struct { struct { int t[3]={0,0,0}; }
 localMatrix; } scaMatrixData; } g_playerEntity;
struct ZmSurvivorInfo { int player; bool dead; };
static ZmSurvivorInfo survivors[4];
static int count, nearby, requests, messages;
static unsigned char s_gatherMessage[1];
bool zombie_mode_armed() { return true; }
int zm_game_role() { return ZM_NET_SURVIVOR; }
int zm_net_self() { return 1; }
void zm_decode_dest(unsigned char, int, unsigned char* s, unsigned char* r) { *s=5; *r=16; }
int yw_boss_at(unsigned char, unsigned char) { return 0; }
bool yw_beaten(int) { return beaten; }
int zm_survivor_list(ZmSurvivorInfo* out, int) {
 memcpy(out, survivors, count*sizeof(*out)); return count;
}
int zm_room_survivors(int (*xz)[2], int* who, int) {
 for(int i=0;i<nearby;i++) { xz[i][0]=xz[i][1]=0; who[i]=i+1; }
 return nearby;
}
void zm_message_show(const unsigned char*) { messages++; }
void zm_note(const char*) {}
void yw_ask_host(int, int, int, int) { requests++; }
'''
checks = r'''
int main() {
 unsigned char door[24]={};
 // Client list excludes self: one living teammate in a different room.
 count=1; survivors[0]={2,false}; nearby=1;
 assert(zombie_mode_boss_door_gate(door)); assert(messages==1 && requests==0);
 // Three survivors, only one teammate nearby: still refuse entry.
 count=2; survivors[1]={3,false}; nearby=2;
 assert(zombie_mode_boss_door_gate(door)); assert(messages==2 && requests==0);
 // Everyone nearby: block direct entry and request the host's vote.
 nearby=3;
 assert(zombie_mode_boss_door_gate(door)); assert(requests==1 && messages==2);
 // Host list includes self: do not count the local survivor twice.
 count=2; survivors[0]={1,false}; survivors[1]={2,false}; nearby=2;
 assert(zombie_mode_boss_door_gate(door)); assert(requests==2);
 // Last living survivor still gets the normal confirmation.
 count=1; survivors[0]={2,true}; nearby=1;
 assert(zombie_mode_boss_door_gate(door)); assert(requests==3);
 // A beaten boss does not hold the squad at its door.
 beaten=true; survivors[0].dead=false;
 assert(!zombie_mode_boss_door_gate(door));
 puts("Boss doorway gate OK");
}
'''
with tempfile.TemporaryDirectory(prefix="re1-boss-gate-") as temp:
    with patch.object(routes, "adapter_source", return_value=fixture +
                      "bool zombie_mode_boss_door_gate(" + gate + checks):
        executable = routes.build_adapter(Path(temp))
    subprocess.run([str(executable)], check=True)

prompt_source = source.split("bool zombie_mode_boss_prompt_answer(", 1)[1].split(
    "// Let the native prompt", 1)[0]
vote_source = source.split("static void yw_vote_frame(void)", 1)[1].split(
    "// ZM_EV_BOSS:", 1)[0]
prompt_fixture = r'''
#include <cassert>
#include <cstdio>
static bool armed=true, s_askShown=true, s_askAnswered=false;
static int s_askBoss=0, s_askToken=7, answers=0, lastAnswer=-1;
static unsigned char prompt[2], other[2];
static unsigned char* g_MessagePtr=prompt;
bool zombie_mode_armed() { return armed; }
const unsigned char* yw_prompt_text(int) { return prompt; }
void yw_ask_host(int, int, int token, int yes) {
 assert(token==7); answers++; lastAnswer=yes;
}
enum { YW_OP_ANSWER=5, YW_OP_GO=6, YW_WAITING=0 };
static int s_voteBoss=0, s_voteFrom=1, s_phase[1]={0}, s_voteToken=7;
static unsigned char s_voteVoters=6, s_voteYes=0;
static unsigned int s_voteEnd=10000;
static int cancelled=0, go=0;
static bool together=true;
static struct { const char* name="attic"; } kBoss[1];
bool zm_match_authority() { return true; }
bool yw_beaten(int) { return false; }
unsigned int zm_game_time_ms() { return 1000; }
bool yw_host_gathered(int, unsigned char* mask) { *mask=6; return together; }
void yw_vote_off(const char*) { cancelled++; s_voteBoss=-1; }
void yw_host_send(int op, int, int, int, int) { assert(op==YW_OP_GO); go++; }
void dbg_printf(const char*, ...) {}
'''
prompt_checks = r'''
int main() {
 // A normal message must retain its native post-action dispatch.
 g_MessagePtr=other;
 assert(!zombie_mode_boss_prompt_answer(true) && answers==0);
 g_MessagePtr=prompt;
 assert(zombie_mode_boss_prompt_answer(true) && answers==1 && lastAnswer==1);
 assert(!zombie_mode_boss_prompt_answer(true) && answers==1);
 s_askAnswered=false;
 assert(zombie_mode_boss_prompt_answer(false) && answers==2 && lastAnswer==0);
 // The host waits for ALL votes, then sends one group entry.
 s_voteYes=2; yw_vote_frame(); assert(go==0 && s_voteBoss==0);
 s_voteYes=6; yw_vote_frame(); assert(go==1 && s_voteBoss==-1);
 // A split squad cancels, even with unanimous Yes votes.
 s_voteBoss=0; together=false;
 yw_vote_frame(); assert(cancelled==1 && go==1 && s_voteBoss==-1);
 puts("Native boss prompt and unanimous vote OK");
}
'''
with tempfile.TemporaryDirectory(prefix="re1-boss-vote-") as temp:
    with patch.object(routes, "adapter_source", return_value=prompt_fixture +
                      "bool zombie_mode_boss_prompt_answer(" + prompt_source +
                      "static void yw_vote_frame(void)" + vote_source + prompt_checks):
        executable = routes.build_adapter(Path(temp))
    subprocess.run([str(executable)], check=True)
