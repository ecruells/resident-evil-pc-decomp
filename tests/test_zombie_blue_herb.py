"""Exercise actual menu cure dispatch for ordinary and Yawn poison."""
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import evaluate_zombie_routes as routes
source = (ROOT / "src/game/MainMenu.cpp").read_text()
heal = "static int menu_item_use_heal(unsigned char slot)" + source.split("static int menu_item_use_heal(unsigned char slot)",1)[1].split("// Port-added mod: the loaded count",1)[0]
fixture = r'''
#include <cassert>
#include <cstdio>
#include <initializer_list>
struct { short health,maxHealth; unsigned char healthStatusFlags; } g_playerEntity;
unsigned char g_ItemHealTable[256],g_bItemMenuSelectedItemId=0x45;
unsigned char DAT_00ae9f13,DAT_00ae9f2e,DAT_00ae9f12;
unsigned int g_ScenarioFlags2[32];
const int SCENARIO2_FLAG_YAWN_POISONED=0x43;
bool armed=true, storyPoison=true;
bool zombie_mode_armed() { return armed; }
void FUN_00473f10(int*, int bit) { assert(bit==0x43); storyPoison=false; }
int zombie_mode_heal_item_used(unsigned char,int used) { return used; }
'''
checks = r'''
int main() {
 for(int healValue : {0x20,0x21,0x23}) {
   g_ItemHealTable[0x45]=(unsigned char)healValue;
   for(int poison : {0x02,0x20,0x22}) {
     armed=true; storyPoison=true;
     g_playerEntity={140,140,(unsigned char)(poison|0x80)};
     assert(menu_item_use_heal(0)==1);
     assert(g_playerEntity.healthStatusFlags==0x80);
     assert(g_playerEntity.health==140);
     if(poison&0x20) assert(!storyPoison);
   }
   armed=false; storyPoison=true; g_playerEntity={140,140,0x20};
   assert(menu_item_use_heal(0)==0);
   assert(g_playerEntity.healthStatusFlags==0x20 && storyPoison);
   armed=true; g_playerEntity={140,140,0x80};
   assert(menu_item_use_heal(0)==0); // no need to consume an unused cure
 }
 // Healing without blue does not cure poison.
 g_ItemHealTable[0x45]=3; g_playerEntity={50,140,0x20};
 assert(menu_item_use_heal(0)==1 && g_playerEntity.healthStatusFlags==0x20);
 puts("Blue herb/mixture cures, story flag and original-mode isolation OK");
}
'''
with tempfile.TemporaryDirectory(prefix="re1-blue-herb-") as temp:
    with patch.object(routes, "adapter_source", return_value=fixture + heal + checks):
        executable = routes.build_adapter(Path(temp))
    subprocess.run([str(executable)], check=True)
