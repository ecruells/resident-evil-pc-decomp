"""CPU regression for production possession gates; never launches the game."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    match = re.search(r"static (?:bool|void) " + name + r"\([^;]*?\)\s*\{", source)
    start = match.start()
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


FIXTURE = r'''
#include <cassert>
#include <cstdio>
enum { ENEMY_ZOMBIE, ENEMY_CERBERUS, ENEMY_HUNTER, ENEMY_CHIMERA, ENEMY_TYRANT_2 };
struct Entity {
    short health = 100;
    unsigned char id = 0, state = 1, ignore_player_flag = 0;
    unsigned char action_behavior = 0, action_state = 0, behavior_flags = 0;
    unsigned char animationId = 0, animation_frame_id = 0, timing_control = 0, blend_counter = 0;
    unsigned short move_speed_current = 0;
    int animHeader = 0, animBase = 0;
    char padding[400] = {};
};
static Entity g_EnemiesList[30], *g_zombieModeEntity = nullptr, *s_biteVictim = nullptr;
static bool s_remoteGrab = false, s_riding = false, s_monEngine = false;
static bool remote[30] = {}, feeding = false, reserved = false;
static bool zm_world_director_controlled(const Entity*) { return reserved; }
static unsigned int s_monDeathFrames = 0;
static unsigned char s_monAnim = 0xff;
static bool s_spawnIdlePending[30] = {};
static unsigned int s_spawnIdleUntil[30] = {}, now = 100;
static int g_message_flags = 4, frames = 0, shadows = 0;
enum { ZOMBIE_FLAG_LAYING_DOWN = 2, ZM_ANIM_PRONE = 9, ZM_ANIM_IDLE = 0 };
struct ZmMonsterType { unsigned char idleAnim = 1; };
static ZmMonsterType type;
static const ZmMonsterType* zm_monster_type(unsigned char) { return &type; }
static bool zm_is_possessable_id(unsigned char id) { return id <= ENEMY_TYRANT_2; }
static bool zm_yawn_head(const Entity*) { return false; }   // the Yawn driver (ZombieYawn.cpp): none here
static bool zombie_mode_is_puppet(const Entity*) { return false; }
static bool zm_reinforce_hold(const Entity*) { return false; }
static unsigned int zm_game_time_ms() { return now; }
static void Joint_move(int, int, int, int) { ++frames; }
static void zm_draw_shadow(Entity*) { ++shadows; }
static void zm_puppet_awareness(Entity*,bool) {}
static void SetEntityScaHitData(Entity*) {}
static bool zm_shared_slot_busy(int slot) { return remote[slot]; }
static bool zm_is_zombie_id(unsigned char id) { return id == ENEMY_ZOMBIE; }
static bool zombie_mode_feeding_target(const Entity*) { return feeding; }
'''

CHECKS = r'''
int main() {
    Entity* e = &g_EnemiesList[0];
    // Every supported monster's ordinary initialized idle is pad-owned.
    // ignore_player_flag alone does not distinguish an attack from patrol.
    for (int id = ENEMY_ZOMBIE; id <= ENEMY_TYRANT_2; ++id) {
        *e = Entity(); e->id = id; e->ignore_player_flag = 1;
        assert(!zm_possession_busy(e));
        if (id != ENEMY_ZOMBIE) { zm_monster_possess(e); assert(!s_monEngine); }
        e->state = 0; assert(!zm_possession_busy(e));
        e->state = 2; assert(zm_possession_busy(e));
        e->state = 3; assert(zm_possession_busy(e));
    }
    *e = Entity(); e->state = 5; // zombie bite, including before status bit 8
    assert(zm_possession_busy(e));
    e->state = 1; remote[0] = true; assert(zm_possession_busy(e));
    remote[0] = false; g_zombieModeEntity = e;
    s_remoteGrab = true; assert(zm_possession_busy(e)); s_remoteGrab = false;
    s_biteVictim = &g_EnemiesList[1]; assert(zm_possession_busy(e)); s_biteVictim = nullptr;
    s_riding = true; assert(zm_possession_busy(e)); s_riding = false;
    for (int id : {ENEMY_HUNTER, ENEMY_CHIMERA, ENEMY_TYRANT_2}) {
        *e = Entity(); e->id = id; e->ignore_player_flag = 1;
        e->action_behavior = id == ENEMY_TYRANT_2 ? 7 : 4;
        e->action_state = 2;
        assert(zm_possession_busy(e));
        zm_monster_possess(e); assert(s_monEngine);
        assert(e->action_behavior == (id == ENEMY_TYRANT_2 ? 7 : 4));
        assert(e->action_state == 2); // never restart a paired animation
        e->ignore_player_flag = 0; e->action_behavior = 0;
        assert(!zm_possession_busy(e));
    }
    *e = Entity(); e->id = ENEMY_CERBERUS;
    for (int attack : {5, 8}) {
        e->ignore_player_flag = 0; e->behavior_flags = attack;
        assert(zm_possession_busy(e)); zm_monster_possess(e); assert(s_monEngine);
    }
    for (int attack : {3, 6, 7}) {
        e->ignore_player_flag = attack;
        assert(zm_possession_busy(e)); zm_monster_possess(e); assert(s_monEngine);
    }
    *e = Entity(); e->state = 3; e->health = -1;
    assert(!zm_possession_busy(e)); // automatic death handoff still works
    *e = Entity(); e->state = 15; feeding = true;
    assert(!zm_possession_busy(e)); // corpse feeding can still be disturbed
    feeding = false;
    for (int id = ENEMY_ZOMBIE; id <= ENEMY_TYRANT_2; ++id) {
        *e = Entity(); e->id = id; now = 100;
        s_spawnIdlePending[0] = true; s_spawnIdleUntil[0] = 0;
        e->state = 0; assert(!zm_entry_idle(e)); // initialization still runs
        e->state = 1; frames = 0; assert(zm_entry_idle(e)); assert(frames == 1);
        now = 1099; assert(zm_entry_idle(e)); assert(frames == 2);
        g_message_flags = 0; assert(zm_entry_idle(e)); assert(frames == 2);
        g_message_flags = 4;
        e->state = 2; assert(!zm_entry_idle(e)); // damage may interrupt the idle
        e->state = 1; now = 1100; assert(!zm_entry_idle(e));
        // A reordered entrance cue must not stop an already-started pair.
        s_spawnIdleUntil[0] = now + 1000;
        if (id == ENEMY_ZOMBIE) e->state = 5;
        else if (id == ENEMY_CERBERUS) e->ignore_player_flag = 3;
        else { e->ignore_player_flag = 1; e->action_behavior = 4; }
        assert(!zm_entry_idle(e));
    }
    // A reservation outlives a slow door animation and every timer.
    g_zombieModeEntity = nullptr;
    for (int id = ENEMY_ZOMBIE; id <= ENEMY_TYRANT_2; ++id) {
        *e = Entity(); e->id = id; reserved = true;
        s_spawnIdlePending[0] = false; s_spawnIdleUntil[0] = 0;
        now = 90000; frames = 0;
        assert(zm_entry_idle(e) && frames == 1);
        now += 90000; assert(zm_entry_idle(e) && frames == 2);
        g_zombieModeEntity = e; assert(!zm_entry_idle(e)); // actual director pad
        g_zombieModeEntity = nullptr;
        reserved = false; assert(!zm_entry_idle(e)); // explicit release to AI
    }
    puts("Possession gates: idle, initialization, reactions, local/remote pairs and attacks passed.");
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="g++")
    args = parser.parse_args()
    source = (ROOT / "src/game/mods/ZombieMode.cpp").read_text()
    code = FIXTURE + "\n#include <initializer_list>\n" + "\n".join(
        function(source, name) for name in
        ("zm_monster_attack_over", "zm_possession_busy", "zm_entry_idle", "zm_monster_possess")
    ) + CHECKS
    with tempfile.TemporaryDirectory(prefix="re1-possession-") as temp:
        cpp, exe = Path(temp) / "test.cpp", Path(temp) / "test"
        cpp.write_text(code)
        subprocess.run([args.compiler, "-m32", "-std=c++17", str(cpp), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
