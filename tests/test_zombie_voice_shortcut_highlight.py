"""Exercise the HUD's actual raw-input test for Options versus inventory."""
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import evaluate_zombie_routes as routes

source = (ROOT / 'src/game/mods/ZombieVoice.cpp').read_text()
held = 'static bool voice_hint_held' + source.split('static bool voice_hint_held', 1)[1].split(
    'static void voice_hint_card', 1)[0]
fixture = r'''
#include <cassert>
#include <cstdio>
typedef unsigned int DWORD;
#define VOICE_INVENTORY 0x0800
#define VOICE_OPTIONS_BIT 0x0100
DWORD g_RawPadHeld;
'''
checks = r'''
int main() {
    g_RawPadHeld = 0x0800;
    assert(voice_hint_held(VOICE_INVENTORY));
    g_RawPadHeld = 0x0900;
    assert(!voice_hint_held(VOICE_INVENTORY));
    g_RawPadHeld = 0x0100;
    assert(!voice_hint_held(VOICE_INVENTORY));
    g_RawPadHeld = 0x0900 | 0x0080;
    assert(!voice_hint_held(VOICE_INVENTORY));
    assert(voice_hint_held(0x0080));
    g_RawPadHeld = 0;
    assert(!voice_hint_held(VOICE_INVENTORY));
    puts("Options does not highlight Y; inventory and other shortcuts still highlight");
}
'''
with tempfile.TemporaryDirectory(prefix='re1-voice-highlight-') as temp:
    with patch.object(routes, 'adapter_source', return_value=fixture + held + checks):
        executable = routes.build_adapter(Path(temp))
    subprocess.run([str(executable)], check=True)
