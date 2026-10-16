"""Check production Yawn health scaling and the flee threshold budget."""
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import evaluate_zombie_routes as routes
source = (ROOT / "src/game/mods/ZombieYawn.cpp").read_text()
budget = "static int yw_health_budget(int alive)" + source.split("static int yw_health_budget(int alive)",1)[1].split("// After the head",1)[0]
fixture = "#include <cassert>\n#include <cstdio>\n#define YW_BUDGET 500\n"
checks = r'''
int main() {
 assert(yw_health_budget(0)==500 && yw_health_budget(-1)==500);
 for(int n=1;n<=4;n++) {
   int budget=yw_health_budget(n);
   assert(budget==500*(1<<(n-1)));
   assert(budget>=2*250*n);
   short yawn1=(short)(0xaf0+budget), yawn2=(short)budget;
   assert(yawn1-0xaf0==budget && yawn2==budget);
 }
 assert(yw_health_budget(100)==4000);
 puts("Yawn 500/1000/2000/4000 HP budgets and signed-16-bit flee health OK");
}
'''
with tempfile.TemporaryDirectory(prefix="re1-yawn-health-") as temp:
    with patch.object(routes, "adapter_source", return_value=fixture + budget + checks):
        executable = routes.build_adapter(Path(temp))
    subprocess.run([str(executable)], check=True)
