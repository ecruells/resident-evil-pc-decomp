"""Check the production possession gate used on arrival, without running the game."""
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import evaluate_zombie_routes as routes

source = (ROOT / "src/game/mods/ZombieMode.cpp").read_text()
gate = "static bool zm_possession_busy(const Entity* e)\n{" + source.split(
    "static bool zm_possession_busy(const Entity* e)\n{", 1)[1].split(
    "static bool zm_entry_idle", 1)[0]
fixture = r'''
#include <cassert>
#include <cstdio>
struct Entity { int health, state, action_behavior, id; };
Entity g_EnemiesList[30];
Entity* g_zombieModeEntity = nullptr;
Entity* s_biteVictim = nullptr;
bool s_remoteGrab = false, s_riding = false;
bool zm_yawn_head(const Entity* e) { return e->id == 1 || e->id == 2; }
bool zm_shared_slot_busy(int) { return false; }
bool zombie_mode_feeding_target(const Entity*) { return false; }
bool zm_is_zombie_id(int id) { return id == 0; }
bool zm_monster_attack_over(const Entity*) { return true; }
'''
checks = r'''
int main() {
    Entity* e = &g_EnemiesList[0];
    // Both boss heads arrive before yawn_init creates their skeleton/segments.
    for (int id : {1, 2}) {
        *e = {100, 0, 6, id};
        assert(!zm_possession_busy(e));
        e->state = 1; e->action_behavior = 0;
        assert(!zm_possession_busy(e));
        e->action_behavior = 1;
        assert(!zm_possession_busy(e));
        for (int action = 2; action <= 10; ++action) {
            e->action_behavior = action;
            assert(zm_possession_busy(e));
        }
        e->state = 2; e->action_behavior = 0;
        assert(zm_possession_busy(e));
        e->health = -1;
        assert(!zm_possession_busy(e));
    }
    puts("Yawn arrival, attacks, reactions and death possession checks OK");
}
'''
with tempfile.TemporaryDirectory(prefix="re1-yawn-possession-") as temp:
    with patch.object(routes, "adapter_source", return_value=
                      "#include <initializer_list>\n" + fixture + gate + checks):
        executable = routes.build_adapter(Path(temp))
    subprocess.run([str(executable)], check=True)
