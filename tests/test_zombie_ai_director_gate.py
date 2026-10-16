"""Check AI runs only on the host of an explicitly selected AI match."""
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import evaluate_zombie_routes as routes
source = (ROOT / "src/game/mods/ZombieDirectorAI.cpp").read_text()
gate = "int zm_ai_level(void)" + source.split("int zm_ai_level(void)",1)[1].split("const char* zm_ai_level_name",1)[0]
fixture = r'''
#include <cassert>
#include <cstdio>
bool g_bPlayAsZombie=true, armed=true, authority=true;
int role=0, level=0, g_zmAiDirector=4;
const int ZM_NET_ZOMBIE=1, ZM_NET_SURVIVOR=2;
bool zombie_mode_armed() { return armed; }
bool zm_match_authority() { return authority; }
int zm_net_role() { return role; }
int zm_net_ai_level() { return level; }
bool zm_ai_hosted(void);
'''
checks = r'''
int main() {
 for(int config=0;config<=5;config++) {
   g_zmAiDirector=config;
   role=0; level=0; assert(zm_ai_level()==0); // solo human director
   role=1; assert(zm_ai_level()==0); // human director host
   role=2; assert(zm_ai_level()==0); // survivor client
   for(int difficulty=1;difficulty<=5;difficulty++) {
     role=1; level=difficulty; assert(zm_ai_level()==difficulty);
     authority=false; assert(zm_ai_level()==0); authority=true;
     role=2; assert(zm_ai_level()==0);
     role=1; armed=false; assert(zm_ai_level()==0); armed=true;
     g_bPlayAsZombie=false; assert(zm_ai_level()==0); g_bPlayAsZombie=true;
   }
 }
 puts("Human director isolation and AI host difficulty checks OK");
}
'''
with tempfile.TemporaryDirectory(prefix="re1-ai-director-gate-") as temp:
    with patch.object(routes, "adapter_source", return_value=fixture + gate + checks):
        executable = routes.build_adapter(Path(temp))
    subprocess.run([str(executable)], check=True)
