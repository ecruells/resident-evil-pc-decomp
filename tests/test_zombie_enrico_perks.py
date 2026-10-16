"""Compile Enrico's actual perk hooks and check mode isolation and bite ticks."""
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import evaluate_zombie_routes as routes

source = (ROOT / 'src/game/mods/ZombiePerks.cpp').read_text()
guard = 'static int zm_perk_char(void)' + source.split('static int zm_perk_char(void)', 1)[1].split(
    '// Inventory limits', 1)[0]
hooks = 'int zombie_mode_player_weapon_damage' + source.split(
    'int zombie_mode_player_weapon_damage', 1)[1].split(
    '// ---------------------------------------------------------------------------', 1)[0]
fixture = r'''
#include <cassert>
#include <cstdio>
enum { ZM_NET_OFF, ZM_NET_ZOMBIE, ZM_NET_SURVIVOR };
enum { ZM_CHAR_CHRIS, ZM_CHAR_JILL, ZM_CHAR_BARRY, ZM_CHAR_REBECCA, ZM_CHAR_RICHARD, ZM_CHAR_ENRICO };
enum { ITEM_KNIFE = 1 };
bool armed = true;
int role = ZM_NET_SURVIVOR, character = ZM_CHAR_ENRICO;
bool zombie_mode_armed() { return armed; }
int zm_game_role() { return role; }
int zm_net_self() { return 1; }
int zm_net_char(int) { return character; }
'''
checks = r'''
int bites(int timer) {
    int count = 0;
    for (int tick = 0; timer >= 0; ++tick, --timer)
        if (tick % 19 == 0) ++count;
    return count;
}
int main() {
    for (int ch = 0; ch < 6; ++ch) {
        character = ch;
        for (int weapon = 1; weapon <= 10; ++weapon)
            assert(zombie_mode_player_weapon_damage(weapon, 8) ==
                   (ch == ZM_CHAR_ENRICO && weapon == ITEM_KNIFE ? 16 : 8));
        assert(zombie_mode_player_weapon_damage(ITEM_KNIFE, 0) == 0);
        assert(zombie_mode_player_zombie_grab_time(105) == (ch == ZM_CHAR_ENRICO ? 52 : 105));
        assert(zombie_mode_player_zombie_grab_time(30) == (ch == ZM_CHAR_ENRICO ? 15 : 30));
    }
    character = ZM_CHAR_ENRICO;
    assert(bites(zombie_mode_player_zombie_grab_time(105)) == 3);
    assert(bites(105) == 6);
    assert(bites(zombie_mode_player_zombie_grab_time(30)) == 1);
    assert(bites(30) == 2);
    for (int mode = 0; mode < 3; ++mode) {
        role = mode;
        armed = false;
        assert(zombie_mode_player_weapon_damage(ITEM_KNIFE, 8) == 8);
        assert(zombie_mode_player_zombie_grab_time(105) == 105);
        armed = true;
        if (mode != ZM_NET_SURVIVOR) {
            assert(zombie_mode_player_weapon_damage(ITEM_KNIFE, 8) == 8);
            assert(zombie_mode_player_zombie_grab_time(105) == 105);
        }
    }
    puts("Enrico knife, grab timing, bite count and mode isolation OK");
}
'''
with tempfile.TemporaryDirectory(prefix='re1-enrico-') as temp:
    with patch.object(routes, 'adapter_source', return_value=fixture + guard + hooks + checks):
        executable = routes.build_adapter(Path(temp))
    subprocess.run([str(executable)], check=True)
