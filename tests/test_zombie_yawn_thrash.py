"""Exercise the production thrash wave without launching the game."""
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import evaluate_zombie_routes as routes
source = (ROOT / "src/game/entities/Yawn.cpp").read_text()
wave = "#define YT_WAVE_AMPLITUDE" + source.split("#define YT_WAVE_AMPLITUDE", 1)[1].split("// ============================================================================", 1)[0]
fixture = r'''
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
struct VECTOR { int x,y,z,pad; };
struct SVECTOR { short x,y,z,pad; };
struct MATRIX { int t[3]; double yaw; };
struct JointStruct { SVECTOR rotation; MATRIX transform,world; };
struct Entity { JointStruct* jointsStructs; short angle; };
Entity* ENTITY;
void RotMatrix(SVECTOR* r, MATRIX* m) { m->yaw = r->y * 6.283185307179586 / 4096.0; }
void ApplyMatrixLV(MATRIX* m, VECTOR* v, VECTOR* out) {
 out->x = (int)(v->x*std::cos(m->yaw)+v->z*std::sin(m->yaw));
 out->y = v->y;
 out->z = (int)(v->z*std::cos(m->yaw)-v->x*std::sin(m->yaw));
}
'''
checks = r'''
int main() {
 JointStruct joints[15]={}; Entity e={joints,123}; ENTITY=&e;
 // A curved body, rather than a straight chain aligned to the root heading.
 for(int k=2;k<=14;k++) {
   double angle=(k-2)*0.12;
   joints[k].world.t[0]=3000+(int)(3000*std::sin(angle));
   joints[k].world.t[2]=3000+(int)(3000*std::cos(angle));
   joints[k].world.t[1]=-100;
   joints[k].rotation.y=321;
 }
 JointStruct base[15]; memcpy(base,joints,sizeof(base));
 yawn_thrash_body_wave(0);
 yawn_thrash_body_wave(8);
 int tailX=joints[14].world.t[0], tailZ=joints[14].world.t[2];
 for(int k=3;k<=14;k++) {
   int prev=k==3?2:k-1, next=k==14?14:k+1;
   double ax=base[next].world.t[0]-base[prev].world.t[0];
   double az=base[next].world.t[2]-base[prev].world.t[2];
   double dx=joints[k].world.t[0]-base[k].world.t[0];
   double dz=joints[k].world.t[2]-base[k].world.t[2];
   assert(dx*dx+dz*dz<=360*360);
   assert(std::abs(dx*ax+dz*az)<=std::abs(ax)+std::abs(az)); // local normal
   assert(joints[k].world.t[1]==base[k].world.t[1]);
 }
 assert(e.angle==123); // never pivot the root
 assert(joints[2].world.t[0]==base[2].world.t[0]);
 yawn_thrash_body_wave(13);
 assert(joints[14].world.t[0]!=tailX || joints[14].world.t[2]!=tailZ);
 int x=joints[14].world.t[0];
 yawn_thrash_body_wave(13);
 assert(joints[14].world.t[0]==x); // no accumulated drift
 yawn_thrash_body_wave(40);
 for(int k=3;k<=14;k++) {
   assert(joints[k].world.t[0]==base[k].world.t[0]);
   assert(joints[k].world.t[2]==base[k].world.t[2]);
   assert(joints[k].rotation.y==base[k].rotation.y);
 }
 puts("Thrash local normals, bounded amplitude, curved arc, tail ripple and recovery OK");
}
'''
with tempfile.TemporaryDirectory(prefix="re1-yawn-thrash-") as temp:
    with patch.object(routes, "adapter_source", return_value=fixture + wave + checks):
        executable = routes.build_adapter(Path(temp))
    subprocess.run([str(executable)], check=True)
