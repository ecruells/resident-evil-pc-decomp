"""Compile the elevator exclusion and director room gate with controlled state."""
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import evaluate_zombie_routes as routes


def function(path, signature):
    source = (ROOT / path).read_text()
    start = source.index(signature)
    end = source.index("\n}", start) + 2
    return source[start:end]


fixture = r'''
#include <cassert>
#include <cstdio>
#define KP_ELEVATOR_CAR 0
#define STAGE_MANSION_2F 1
#define KP_STAGE 6
bool s_zombieModeArmed = true, authority = true;
bool zm_match_authority() { return authority; }
bool zm_random_room_safe(unsigned char, unsigned char) { return false; }
bool zm_shotgun_room_blocked(unsigned char, unsigned char) { return false; }
bool zm_yawn_director_closed(unsigned char, unsigned char) { return false; }
'''
fixture += function("src/game/mods/ZombieKeypad.cpp", "bool zm_access_room_elevator(")
fixture += function("src/game/mods/ZombieMode.cpp", "static bool zm_director_safe_room(")
fixture += r'''
int main() {
    for (int stage = 0; stage < 8; ++stage)
        for (int room = 0; room < 32; ++room) {
            bool elevator = (stage == 1 || stage == 6) && room == 0;
            assert(zm_access_room_elevator(stage, room) == elevator);
            assert(zm_director_safe_room(stage, room) == elevator);
        }
    s_zombieModeArmed = false;
    assert(!zm_director_safe_room(6, 0));
    puts("Elevator exclusion and normal-mode isolation OK");
}
'''

with tempfile.TemporaryDirectory(prefix="re1-elevator-") as temp:
    with patch.object(routes, "adapter_source", return_value=fixture):
        executable = routes.build_adapter(Path(temp))
    subprocess.run([str(executable)], check=True)
