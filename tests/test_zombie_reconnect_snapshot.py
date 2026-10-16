"""Compile the production checkpoint capture/restore with the real save-card layout.
CPU only; never launches the game. Run from an x86 VS prompt.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = r'''
#include <cassert>
#include <cstring>
#include <cstdio>
enum { ITEM_SHOTGUN=3, ITEM_BROKEN_SHOTGUN=28 };
typedef unsigned char BYTE;
typedef unsigned int DWORD;
struct ItemSlot { unsigned char item, quantity; };
'''
STUBS = r'''
static BioCardLayout g_BioCard;
static unsigned char slots[16], g_ItemSlotIndices[8];
#define g_ItemsSlots slots
static unsigned char* g_ItemSlotsPointer=slots;
static unsigned int g_ItemSlotsBitmask;
static int g_PlayerDpadHeld,g_PlayerDpadPressed;
static short g_PlayerPosXCopy,g_PlayerPosZCopy,g_PlayerDirAngleCopy,g_PlayerHealthCopy;
static struct { struct { struct { int t[3]; } localMatrix; } scaMatrixData;
    struct { short x,y,z; } position; short directionAngle,health;
    unsigned char maxHealth,healthStatusFlags; unsigned short posY; } g_playerEntity;
static unsigned int zm_net_seed() { return 123; }
static int zm_survivors_in_ms() { return 60000; }
static unsigned short zm_pickups_reconnect_token() { return 9; }
static unsigned short zm_box_reconnect_token() { return 12; }
static void zm_box_reconnect_restore(unsigned short p) { assert(p==12); }
static unsigned short zm_drops_reconnect_sequence() { return 10; }
static unsigned short zm_world_reconnect_sequence() { return 11; }
static unsigned char zm_spec_reconnect_spent() { return 4; }
static int stats[4]={1,2,3,4},revives[4]={0,1,0,0},clockRestored;
static int zm_world_reconnect_export(void* p,int cap) { if(cap<4)return 0; memcpy(p,"room",4); return 4; }
static int zm_drops_reconnect_export(void* p,int cap) { if(cap<4)return 0; memcpy(p,"drop",4); return 4; }
static bool zm_world_reconnect_import(const void* p,int n,unsigned short seq) { assert(n==4&&seq==11&&!memcmp(p,"room",4)); return true; }
static bool zm_drops_reconnect_import(const void* p,int n,unsigned short seq) { assert(n==4&&seq==10&&!memcmp(p,"drop",4)); return true; }
static void zm_world_reconnect_shared(const BioCardLayout* p) { g_BioCard.locksFlags[0]=p->locksFlags[0]; }
static void zm_stats_reconnect_export(int* p) { memcpy(p,stats,sizeof(stats)); }
static void zm_stats_reconnect_import(const int* p) { memcpy(stats,p,sizeof(stats)); }
static void zm_spec_reconnect_export(int* p) { memcpy(p,revives,sizeof(revives)); }
static void zm_spec_reconnect_import(const int* p) { memcpy(revives,p,sizeof(revives)); }
static void zm_spec_reconnect_restore_spent(unsigned char p) { assert(p==4); }
static void zm_trap_reconnect_export(unsigned int* p,unsigned char* r) { p[0]=5000;r[0]=5; }
static void zm_trap_reconnect_import(const unsigned int* p,const unsigned char* r) { assert(p[0]==5000&&r[0]==5); }
static void zm_shotgun_export(unsigned int* p) { p[0]=1; p[1]=0; p[2]=15001; p[3]=2; p[4]=0; p[5]=2; }
static unsigned short zm_shotgun_replacement_consumed() { return 1; }
static bool zm_shotgun_pickaxe_consumed() {return false;}
static void zm_shotgun_import(const unsigned int* p) { assert(p[0]==1&&p[1]==0&&p[2]==15001&&p[3]==2&&p[4]==0&&p[5]==2); }
static void zm_pickups_reconnect_restore(unsigned short p) { assert(p==9); }
static void zm_reconnect_match_clock(int p) { clockRestored=p; }
static void zm_reconnect_failed() { assert(false); }
'''
CHECKS = r'''
int main() {
    static_assert(sizeof(BioCardLayout)==1052,"Use actual save-card layout");
    g_BioCard.stageId=5;g_BioCard.roomId=17;g_BioCard.totalHeldItems=2;g_BioCard.equippedItemId=1;
    g_BioCard.locksFlags[0]=3;slots[0]=3;slots[1]=7;slots[2]=12;slots[3]=20;
    g_ItemSlotIndices[0]=0;g_ItemSlotIndices[1]=1;g_ItemSlotsBitmask=3;
    g_playerEntity.scaMatrixData.localMatrix.t[0]=1200;
    g_playerEntity.scaMatrixData.localMatrix.t[1]=-1800;
    g_playerEntity.scaMatrixData.localMatrix.t[2]=-3500;
    g_playerEntity.health=70;g_playerEntity.maxHealth=140;g_playerEntity.healthStatusFlags=2;g_playerEntity.directionAngle=800;
    ZmReconnectPlayer p;zm_reconnect_capture(&p);
    unsigned char blob[ZM_RECONNECT_BLOB_MAX];
    assert(!zm_reconnect_build(blob,1,&p));
    int bytes=zm_reconnect_build(blob,sizeof(blob),&p);assert(bytes==sizeof(ZmReconnectWorld)+8);
    ZmReconnectWorld* h=(ZmReconnectWorld*)blob;
    assert(!zm_reconnect_import(blob,bytes-1));
    h->seed++;assert(!zm_reconnect_import(blob,bytes));h->seed--;
    h->rosterBytes=-1;assert(!zm_reconnect_import(blob,bytes));h->rosterBytes=4;
    h->player.card.totalHeldItems=9;assert(!zm_reconnect_import(blob,bytes));h->player.card.totalHeldItems=2;
    h->player.card.roomId=58;assert(!zm_reconnect_import(blob,bytes));h->player.card.roomId=17;
    h->shotgun[0]=3;assert(!zm_reconnect_import(blob,bytes));h->shotgun[0]=1;
    h->player.shotgunReplacement=1024;assert(!zm_reconnect_import(blob,bytes));h->player.shotgunReplacement=1;
    h->shotgun[2]=15002;assert(!zm_reconnect_import(blob,bytes));h->shotgun[2]=15001;
    h->shotgun[3]=5;assert(!zm_reconnect_import(blob,bytes));h->shotgun[3]=2;
    h->shotgun[4]=1;assert(!zm_reconnect_import(blob,bytes));h->shotgun[4]=0;
    h->shotgun[5]=1;assert(!zm_reconnect_import(blob,bytes));h->shotgun[5]=2;
    assert(zm_reconnect_import(blob,bytes));
    memset(&g_BioCard,0,sizeof(g_BioCard));memset(slots,0,sizeof(slots));memset(&g_playerEntity,0,sizeof(g_playerEntity));
    // A relaunched client enters the new-game hook before InitializeGame
    // binds the inventory pointer. An existing process masks this crash.
    g_ItemSlotsPointer=nullptr;
    zm_reconnect_new_game();zm_reconnect_player_ready();zm_reconnect_room_ready();
    assert(g_ItemSlotsPointer==g_ItemsSlots);
    assert(g_BioCard.stageId==5&&g_BioCard.roomId==17&&g_BioCard.totalHeldItems==2&&g_BioCard.equippedItemId==1);
    assert(!memcmp(slots,p.inventory,16)&&g_ItemSlotsBitmask==3&&g_ItemSlotIndices[1]==1);
    assert(g_playerEntity.health==70&&g_playerEntity.maxHealth==140&&g_playerEntity.healthStatusFlags==2);
    assert(g_playerEntity.position.x==1200&&g_playerEntity.position.y==-1800&&g_playerEntity.position.z==-3500&&g_playerEntity.directionAngle==800);
    assert(clockRestored==60000&&revives[1]==1&&stats[3]==4);
    g_playerEntity.position.x=2500;zm_reconnect_room_ready();assert(g_playerEntity.position.x==2500);
    h->player.health=-1;assert(zm_reconnect_import(blob,bytes));zm_reconnect_new_game();zm_reconnect_player_ready();assert(g_playerEntity.health==-1);
    zm_reconnect_forget();assert(!zm_reconnect_restore_pending());
    puts("Checkpoint validation, inventory, position, health/death, rescue counts and clock restore checks passed.");
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="cl")
    args = parser.parse_args()
    card = (ROOT / "src/game/BioCard.h").read_text()
    card = card[card.index("#pragma pack(push"):card.index("#pragma pack(pop)") + len("#pragma pack(pop)")]
    header = (ROOT / "src/game/mods/ZombieReconnect.h").read_text()
    header = header[header.index("struct ZmReconnectPlayer"):header.index("void zm_reconnect_capture")]
    source = (ROOT / "src/game/mods/ZombieReconnect.cpp").read_text()
    source = source[source.index("static unsigned char s_restore"):source.index("bool zombie_mode_reconnect_wait")]
    with tempfile.TemporaryDirectory(prefix="re1-checkpoint-") as temp:
        directory = Path(temp)
        cpp, exe = directory / "test.cpp", directory / "test.exe"
        cpp.write_text(FIXTURE + card + "\n" + header + STUBS + source + CHECKS)
        command = ([args.compiler, "/nologo", "/EHsc", "/std:c++17", str(cpp), "/Fe" + str(exe)]
                   if Path(args.compiler).stem.lower() == "cl" else
                   [args.compiler, "-m32", "-std=c++17", str(cpp), "-o", str(exe)])
        subprocess.run(command, cwd=directory, check=True)
        subprocess.run([str(exe)], cwd=directory, check=True)


if __name__ == "__main__":
    main()
