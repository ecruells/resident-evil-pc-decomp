"""Exercise the production boss music shutdown without launching the game."""
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import evaluate_zombie_routes as routes
source = (ROOT / 'src/game/mods/ZombieYawn.cpp').read_text()
music = source[source.index('static void yw_music_stop(void)'):source.index('bool zm_yawn_traps_exit')]
fixture = r'''
#include <cassert>
#include <cstdio>
#define YW_MUSIC_WAV "boss.wav"
bool s_here=true, s_bgmHeld=false, beaten=false;
int s_music=0, g_BgmSoundBank=10, g_BgmPaused=0, g_bgmDefaultVolume=0;
struct Bank {int handle,paused;}; Bank g_SndBank[3]={{11,0},{12,0},{13,0}};
int stopped[32]={}, destroyed=0, resumed=0;
bool yw_beaten(int) {return beaten;}
void yw_hold_bgm() {s_bgmHeld=true;g_BgmPaused=1;for(auto& b:g_SndBank)b.paused=1;}
void setSndStop(int bank) {stopped[bank]++;}
void destroySndBank(int bank) {destroyed=bank;}
int loadSndBankFromWav(const char*) {return 20;}
void dbg_printf(const char*,...) {}
void pan_set(int,int) {}
void set_volume(int,int) {}
void SetSndSlot(int,int) {}
'''
checks = r'''
int main() {
 for(int boss=0;boss<2;boss++) {
  beaten=false;yw_music_frame(boss);assert(s_music==20);
  beaten=true;yw_music_frame(boss);
  assert(s_music==0 && destroyed==20 && !s_bgmHeld);
  assert(g_BgmPaused==0 && stopped[10]>0);
  for(auto& b:g_SndBank)assert(b.paused==0 && stopped[b.handle]>0);
  yw_music_frame(boss);assert(s_music==0);
 }
 puts("Both boss tracks stop immediately; native boss banks remain stopped.");
}
'''
with tempfile.TemporaryDirectory(prefix='re1-yawn-music-') as temp:
    with patch.object(routes, 'adapter_source', return_value=fixture + music + checks):
        executable = routes.build_adapter(Path(temp))
    subprocess.run([str(executable)], check=True)
