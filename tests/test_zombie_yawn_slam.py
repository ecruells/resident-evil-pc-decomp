"""Check the production slam pose and circular green impact cloud."""
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import evaluate_zombie_routes as routes
source = (ROOT / "src/game/entities/Yawn.cpp").read_text()
helpers = "#define YS_DROP_FRAMES" + source.split("#define YS_DROP_FRAMES",1)[1].split("void yawn_action_slam(void)",1)[0]
fixture = r'''
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
struct VECTOR { int x,y,z,pad; };
struct SVECTOR { short x,y,z,pad; };
struct MATRIX { short m[3][3]; int t[3]; };
struct JointStruct { SVECTOR rotation; MATRIX transform,world; unsigned char flags; };
struct Entity { JointStruct* jointsStructs; short angle; struct { MATRIX localMatrix; } scaMatrixData; };
Entity* ENTITY;
const int YS_REACH=1875, YS_CLOUD_RADIUS=1500, EFFECT_ZM_SLAM_CLOUD=0x7e;
int cloudCount=0,centerX=0,centerZ=0;
void RotMatrix(SVECTOR*,MATRIX*) {}
void Effect_CreateBillboard(int type,int depth,int,void* parent,void* point,int marker) {
 assert(type==9 && depth==0x11 && parent==nullptr && marker==0x7e);
 VECTOR* p=(VECTOR*)point;
 int dx=p->x-centerX, dz=p->z-centerZ;
 assert(dx*dx+dz*dz<=YS_REACH*YS_REACH);
 ++cloudCount;
}
'''
checks = r'''
int main() {
 JointStruct joints[15]={}; Entity e={joints,0}; ENTITY=&e;
 for(int k=0;k<15;k++) { joints[k].world.t[0]=1000+k*50;
   joints[k].world.t[1]=k>=7?-250:-2000; joints[k].world.t[2]=4000+k*300;
   for(int axis=0;axis<3;axis++) joints[k].world.m[axis][axis]=4096;
 }
 MATRIX& root=e.scaMatrixData.localMatrix;
 root.m[0][2]=4096; root.m[1][1]=4096; root.m[2][0]=-4096;
 root.t[0]=1000; root.t[2]=4000;
 memcpy(s_slamBody,joints,sizeof(joints));
 s_slamRoot=root; s_slamHeading=e.angle;
 double dx=joints[0].world.t[0]-joints[7].world.t[0];
 double dz=joints[0].world.t[2]-joints[7].world.t[2];
 double flat=-std::atan2(double(joints[0].world.t[1]-joints[7].world.t[1]), std::sqrt(dx*dx+dz*dz));
 yawn_slam_pose(flat/2); int halfY=joints[0].world.t[1];
 yawn_slam_pose(flat);
 assert(joints[0].world.t[1]<=-249 && halfY<-250);
 // The renderer's root/local recomposition must retain the falling head.
 for(int row=0;row<3;row++) {
   int rendered=root.t[row];
   for(int col=0;col<3;col++) rendered+=root.m[row][col]*joints[0].transform.t[col]/4096;
   assert(std::abs(rendered-joints[0].world.t[row])<=1);
 }
 // Rigid front motion preserves attachments, including the grounded hinge.
 for(int k=0;k<7;k++) {
   double before=0,after=0;
   for(int axis=0;axis<3;axis++) {
     double a=s_slamBody[k+1].world.t[axis]-s_slamBody[k].world.t[axis];
     double b=joints[k+1].world.t[axis]-joints[k].world.t[axis];
     before+=a*a; after+=b*b;
   }
   assert(std::abs(std::sqrt(before)-std::sqrt(after))<3);
 }
 for(int k=7;k<15;k++) assert(memcmp(&joints[k].world,&s_slamBody[k].world,sizeof(MATRIX))==0);
 int x=joints[0].world.t[2]; yawn_slam_pose(flat);
 assert(joints[0].world.t[2]==x); // no accumulated drift
 int impact[3]; yawn_slam_impact(impact);
 assert(impact[0]!=joints[0].world.t[0] || impact[2]!=joints[0].world.t[2]);
 centerX=impact[0]; centerZ=impact[2];
 yawn_slam_cloud(impact); assert(cloudCount==21);
 // Recovery approaches the captured pose smoothly rather than changing
 // Euler representations underneath the original animator in one frame.
 for(int remaining=23;remaining>=0;remaining--) {
   double t=remaining/24.0; t=t*t*(3-2*t);
   yawn_slam_pose(flat*t);
 }
 for(int k=0;k<7;k++) for(int axis=0;axis<3;axis++)
   assert(std::abs(joints[k].world.t[axis]-s_slamBody[k].world.t[axis])<=1);
 // A long neck still stays inside the height cap, with a scaled root.
 double dy=s_slamBody[0].world.t[1]-s_slamBody[7].world.t[1];
 double raised=yawn_slam_raise_angle(flat,dx,dy,dz);
 s_slamRoot.m[0][2]=5120; s_slamRoot.m[1][1]=5120; s_slamRoot.m[2][0]=-5120;
 for(int attack=0;attack<100;attack++) {
   // Mimic mutations by the later segment updates; the tail is re-anchored.
   joints[12].world.t[1]-=5000;
   yawn_slam_pose(raised);
   assert(s_slamBody[7].world.t[1]-joints[0].world.t[1]<=YS_MAX_RISE+1);
   for(int k=7;k<15;k++) assert(memcmp(&joints[k].world,&s_slamBody[k].world,sizeof(MATRIX))==0);
   for(int row=0;row<3;row++) {
     int rendered=s_slamRoot.t[row];
     for(int col=0;col<3;col++) rendered+=s_slamRoot.m[row][col]*joints[0].transform.t[col]/4096;
     assert(std::abs(rendered-joints[0].world.t[row])<=3);
   }
   yawn_slam_pose(flat);
   yawn_slam_pose(0);
   for(int k=0;k<15;k++) for(int axis=0;axis<3;axis++)
     assert(std::abs(joints[k].world.t[axis]-s_slamBody[k].world.t[axis])<=1);
 }
 // Curled front: a full drop would bury intermediate neck links.
 // Limit the whole bend while keeping the rear matrices fixed.
 for(int k=0;k<=6;k++) {
   s_slamBody[k].world.t[0]=1000+k*100;
   s_slamBody[k].world.t[1]=-250-k*40;
   s_slamBody[k].world.t[2]=4000+k*250;
 }
 for(double angle=-1.5;angle<=1.5;angle+=0.05) {
   yawn_slam_pose(angle);
   for(int k=0;k<=6;k++) assert(joints[k].world.t[1]<=-249);
   for(int k=7;k<15;k++) assert(memcmp(&joints[k].world,&s_slamBody[k].world,sizeof(MATRIX))==0);
   for(int k=3;k<15;k++) assert((joints[k].flags&0x42)==0x40);
 }
 // A director's heading cannot be replaced by a nearby survivor or a hit.
 e.angle=2048; yawn_slam_pose(flat/2); assert(e.angle==s_slamHeading);
 puts("Slam height cap, scaled root, grounded tail, 100 repetitions and mouth impact OK");
}
'''
with tempfile.TemporaryDirectory(prefix="re1-yawn-slam-") as temp:
    with patch.object(routes, "adapter_source", return_value=fixture + helpers + checks):
        executable = routes.build_adapter(Path(temp))
    subprocess.run([str(executable)], check=True)
