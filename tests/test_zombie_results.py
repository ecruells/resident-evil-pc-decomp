"""CPU-only final-result arbitration checks; never launches the game.

Run in an x86 VS Developer Command Prompt: python tests/test_zombie_results.py
Linux: python3 tests/test_zombie_results.py --compiler g++
Compiles production result handlers with a synthetic reliable transport/clock.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    match = re.search(r"^(?:static )?(?:void|bool|unsigned int) " + name + r"\([^;]*?\)\s*\{", source, re.M)
    assert match, name
    end = match.end()
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end] + "\n"


FIXTURE = r'''
#define zm_game_time_ms plat_time_ms
#include <cassert>
#include <cstring>
#include <cstdio>
#include <vector>
enum { ZM_NET_OFF, ZM_NET_ZOMBIE, ZM_NET_SURVIVOR };
enum { ZM_END_ESCAPE, ZM_END_TIME, ZM_END_ALL_DEAD, ZM_END_CONNECTION, ZM_END_UNSOLVABLE };
enum { ZM_NET_MAX_PLAYERS = 4, ZM_NET_DIRECTOR = 0, ZM_NET_ALL = 255,
       ZM_EV_WIN = 9, ZM_EV_REVIVE = 21, ZM_EV_ROSTER = 4,
       STAGE_MANSION_1F = 0, STAGE_MANSION_RETURN_1F = 5, ZM_BACK_EXIT_ROOM = 0x1B,
       ZM_TIME_LIMIT_MS = 1200000, ZM_TIME_MAX_MS = 2400000, ZM_CLOCK_SEND_MS = 5000, ZM_EV_CLOCK = 17 };
enum { ZM_EV_PICKUP = 30, ZM_EV_DROP = 18, ZM_EV_DROP_TAKE = 19 };
enum { ZM_EV_BOX=16, ZM_EV_SHOTGUN=31, ZM_EV_PIANO=32, ZM_EV_TIMEOUT=34, ZM_EV_BOSS=35 };
static void zm_shotgun_take(const short*,int) {}
static void zm_piano_take(const short*,int) {}
static void zm_yawn_take(const short*,int) {}
static void zm_timeout_take(const short*,int) {}
static bool zm_shotgun_crushed(int) { return false; }
static void zm_box_take(const short*,int) {}
static void zm_pickups_take(const short*, int) {}
static void zm_drops_take_event(int, const short*, int) {}
static int s_gameRole = ZM_NET_ZOMBIE, s_role = ZM_NET_ZOMBIE, s_self = 0;
static bool s_winShown, s_escapePending, s_zombieModeArmed = true;
static unsigned int s_finalElapsedMs, s_winAtMs, s_clockSentMs;
static bool s_clockHave = true;
static int s_winPlayer = -1, s_winReason, elapsed = 1000, allDead;
static unsigned int now = 100;
static unsigned int seed = 0x89ABCDEF;
static int statsCalls, statsFrames, winner, reason, relays;
static unsigned int statsElapsed;
static struct { short health = 140; } g_playerEntity;
static unsigned char g_stageId = 5, g_roomId = ZM_BACK_EXIT_ROOM;
struct NetEvent { unsigned char kind, src, dst; short args[8]; };
struct NetInboxEntry { NetEvent ev; };
static NetInboxEntry s_inbox[2];
static int s_inboxCount;
static NetEvent s_pendingWin;
static bool s_pendingWinHave;
static std::vector<NetEvent> sent;
static unsigned int plat_time_ms() { return now; }
static unsigned int zm_net_seed() { return seed; }
static int zm_net_self() { return s_self; }
static bool hostPlays;   // an AI director's game: the host is seat 0's survivor
static int zm_net_char(int p) { return p == 0 ? (hostPlays ? 3 : -1) : p > 0 && p < 4 ? p - 1 : -1; }
static const int* zm_net_player(int p) { static int marker; return p > 0 && p < 4 ? &marker : NULL; }
static const int* zm_seat_state(int p) { static int own; return p == 0 ? (hostPlays ? &own : NULL) : zm_net_player(p); }
static int zm_net_role() { return s_role; }
static bool zm_match_authority() { return s_gameRole != ZM_NET_SURVIVOR || s_role == ZM_NET_ZOMBIE; }
static int zm_survivors_in_ms() { return elapsed; }
static unsigned int zm_clock_left_ms() { return elapsed >= ZM_TIME_LIMIT_MS ? 0 : ZM_TIME_LIMIT_MS - elapsed; }
// The boss fights' clock holds and bonuses (ZombieYawn.cpp): none here.
static unsigned int s_clockBonusMs, s_clockRunMs, s_clockAtMs, s_clockLeftMs, s_clockHeldMs, s_clockHeldAt;
static bool s_clockRemoteHeld, s_clockHeld;
static unsigned int zm_clock_run_ms() { return elapsed < 0 ? 0 : (unsigned int)elapsed; }
static bool zm_yawn_clock_hold() { return false; }
static bool zm_all_survivors_dead() { return allDead != 0; }
static void zm_stats_frame(unsigned int) { statsFrames++; }
static void zm_stats_match_over(int r, int p, unsigned int ms) {
    statsCalls++; reason = r; winner = p; statsElapsed = ms;
}
static void dbg_printf(const char*, ...) {}
static void zm_net_send_event_to(int dst, int kind, short a0, short a1, short a2, short a3,
                                 short a4, short a5, short a6, short a7) {
    sent.push_back({(unsigned char)kind,(unsigned char)s_self,(unsigned char)dst,{a0,a1,a2,a3,a4,a5,a6,a7}});
}
static void zm_net_send_event8(int k, short a0, short a1, short a2, short a3,
                              short a4, short a5, short a6, short a7) {
    zm_net_send_event_to(ZM_NET_ALL,k,a0,a1,a2,a3,a4,a5,a6,a7);
}
static void zm_net_send_event(int k, short a, short b, short c, short d) {
    zm_net_send_event8(k,a,b,c,d,0,0,0,0);
}
static bool zm_revive_host_claim(short, short, int) { return false; }
static void net_queue(int, const NetEvent&) {}
static void net_route(const NetEvent&, int) { relays++; }
static void zm_world_apply_remote(const short*,int) {}
'''

CHECKS = r'''
static void reset(int role) {
    s_gameRole = s_role = role; s_self = role == ZM_NET_SURVIVOR ? 1 : 0;
    s_winShown = s_escapePending = s_pendingWinHave = false;
    s_finalElapsedMs = s_winAtMs = s_clockSentMs = 0;
    s_zombieModeArmed = s_clockHave = true;
    elapsed = 1000; allDead = 0; statsCalls = statsFrames = relays = 0;
    s_inboxCount = 0; sent.clear(); g_playerEntity.health = 140; hostPlays = false;
}
static NetEvent request(int p = 1) {
    return {ZM_EV_WIN,(unsigned char)p,ZM_NET_DIRECTOR,
        {(short)p,ZM_END_ESCAPE,(short)(5 | ZM_BACK_EXIT_ROOM << 8),140,(short)seed,(short)(seed >> 16),0,0}};
}
int main() {
    reset(ZM_NET_ZOMBIE); zm_match_progression_lost();
    assert(reason==ZM_END_UNSOLVABLE && winner==-1 && sent.back().args[1]==ZM_END_UNSOLVABLE);
    NetEvent lost=sent.back(); reset(ZM_NET_SURVIVOR);
    net_take_event(lost,0); assert(s_winShown && reason==ZM_END_UNSOLVABLE);

    // A survivor requests privately and waits; repeat door presses do not
    // repeatedly queue requests or declare a local result.
    reset(ZM_NET_SURVIVOR); zm_request_escape(); zm_request_escape();
    assert(!s_winShown && s_escapePending && statsCalls == 0 && sent.size() == 1);
    assert(sent[0].dst == ZM_NET_DIRECTOR && sent[0].src == 1);
    reset(ZM_NET_SURVIVOR); g_playerEntity.health = -1; zm_request_escape();
    assert(!s_escapePending && sent.empty());

    // Same host update: received escape beats timeout and all-dead, even
    // with a saturated ordinary inbox. The request is not relayed.
    reset(ZM_NET_ZOMBIE); elapsed = ZM_TIME_LIMIT_MS; allDead = 1; s_inboxCount = 2;
    NetEvent e = request(); net_take_event(e, 1); zm_clock_frame();
    assert(s_winShown && reason == ZM_END_ESCAPE && winner == 1 && statsCalls == 1);
    assert(sent.size() == 1 && sent[0].src == 0 && sent[0].dst == ZM_NET_ALL);
    assert(relays == 0 && s_inboxCount == 2);
    NetEvent escapeResult = sent[0];
    net_take_event(request(2), 2); zm_clock_frame(); assert(statsCalls == 1);
    assert(zm_match_elapsed_ms() == ZM_TIME_LIMIT_MS);

    // Once the host has timed out, a late escape cannot reverse its result.
    reset(ZM_NET_ZOMBIE); elapsed = ZM_TIME_LIMIT_MS; zm_clock_frame();
    assert(reason == ZM_END_TIME && winner == -1 && statsCalls == 1);
    NetEvent timeoutResult = sent.back(); net_take_event(request(), 1);
    assert(reason == ZM_END_TIME && statsCalls == 1);
    reset(ZM_NET_ZOMBIE); allDead = 1; zm_clock_frame();
    assert(reason == ZM_END_ALL_DEAD && winner == -1);
    reset(ZM_NET_OFF); elapsed = ZM_TIME_LIMIT_MS; zm_clock_frame();
    assert(reason == ZM_END_TIME && statsCalls == 1 && sent.empty());

    // Both local outcome guesses and relayed/forged survivor claims are
    // ignored by clients; only the director's confirmed broadcast counts.
    reset(ZM_NET_SURVIVOR); s_escapePending = true; net_take_event(request(2), 0);
    assert(!s_winShown);
    NetEvent bad = timeoutResult; bad.src = 2; net_take_event(bad, 0); assert(!s_winShown);
    net_take_event(timeoutResult, 0);
    assert(s_winShown && !s_escapePending && reason == ZM_END_TIME && statsElapsed == ZM_TIME_LIMIT_MS);
    net_take_event(escapeResult, 0); assert(reason == ZM_END_TIME && statsCalls == 1);
    reset(ZM_NET_SURVIVOR); net_take_event(escapeResult, 0);
    assert(reason == ZM_END_ESCAPE && winner == 1 && statsCalls == 1);
    elapsed = 10; assert(zm_match_elapsed_ms() == ZM_TIME_LIMIT_MS);

    reset(ZM_NET_ZOMBIE); bad = request(); bad.src = 0; net_take_event(bad, 1);
    assert(!s_winShown); bad = request(); bad.dst = ZM_NET_ALL; net_take_event(bad, 1); assert(!s_winShown);
    bad = request(); bad.args[2] = 5 | (2 << 8); net_take_event(bad, 1); assert(!s_winShown);
    bad = request(); bad.args[3] = -1; net_take_event(bad, 1); assert(!s_winShown);
    bad = request(); bad.args[4] ^= 1; net_take_event(bad, 1); assert(!s_winShown);
    reset(ZM_NET_SURVIVOR); bad = timeoutResult; bad.args[3] ^= 1; net_take_event(bad, 0); assert(!s_winShown);
    bad = timeoutResult; bad.args[1] = 99; net_take_event(bad, 0); assert(!s_winShown);
    bad = timeoutResult; bad.args[2] = ZM_TIME_MAX_MS / 1000 + 1; net_take_event(bad, 0); assert(!s_winShown);
    bad = escapeResult; bad.args[0] = 0; net_take_event(bad, 0); assert(!s_winShown);

    // A confirmation arriving while the survivor is still initializing is
    // retained separately from the ordinary inbox and applies once ready.
    reset(ZM_NET_SURVIVOR); s_zombieModeArmed = false; s_inboxCount = 2;
    net_take_event(escapeResult, 0); assert(s_pendingWinHave && !s_winShown);
    s_zombieModeArmed = true;
    assert(zm_match_take_win(s_pendingWin.args, s_pendingWin.src));
    assert(s_winShown && reason == ZM_END_ESCAPE && statsCalls == 1);
    // The host of an AI director's game escapes itself: its own request (the
    // loopback hands it straight to zm_match_take_win) wins for seat 0, and
    // every survivor copy accepts seat 0 as the winner.
    reset(ZM_NET_ZOMBIE); s_gameRole = ZM_NET_SURVIVOR; hostPlays = true;
    assert(zm_match_take_win(request(0).args, 0));
    assert(s_winShown && reason == ZM_END_ESCAPE && winner == 0 && sent.back().dst == ZM_NET_ALL);
    NetEvent hostEscape = sent.back();
    reset(ZM_NET_SURVIVOR); hostPlays = true; net_take_event(hostEscape, 0);
    assert(s_winShown && winner == 0);
    puts("Host result arbitration, race ordering, stale/forged events and initialization checks passed.");
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="cl")
    args = parser.parse_args()
    mode = (ROOT / "src/game/mods/ZombieMode.cpp").read_text(encoding="utf-8")
    net = (ROOT / "src/game/mods/ZombieNet.cpp").read_text(encoding="utf-8")
    frame = function(mode, "zombie_mode_net_frame")
    assert frame.index("zm_net_poll();") < frame.index("zm_clock_frame();")
    assert frame.index("zm_route_events();") < frame.index("zm_clock_frame();")
    source = FIXTURE
    for name in ("zm_match_elapsed_ms", "zm_match_end", "zm_host_finish", "zm_match_progression_lost", "zm_match_take_win", "zm_clock_frame", "zm_request_escape"):
        source += function(mode, name)
    source += function(net, "net_take_event") + CHECKS
    with tempfile.TemporaryDirectory(prefix="re1-results-") as temp:
        directory = Path(temp)
        cpp, exe = directory / "test.cpp", directory / "test.exe"
        cpp.write_text(source, encoding="utf-8")
        if Path(args.compiler).stem.lower() == "cl":
            command = [args.compiler, "/nologo", "/EHsc", "/std:c++17", str(cpp), "/Fe" + str(exe)]
        else:
            command = [args.compiler, "-m32", "-std=c++17", str(cpp), "-o", str(exe)]
        subprocess.run(command, cwd=directory, check=True)
        subprocess.run([str(exe)], cwd=directory, check=True)


if __name__ == "__main__":
    main()
