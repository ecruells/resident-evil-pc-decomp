"""CPU-only checks of the production runtime escape proof. Never starts the game."""
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import evaluate_zombie_routes as routes

STUBS = r'''
bool zm_shotgun_route_open(unsigned char,unsigned char,unsigned char,unsigned char,unsigned int,bool) {return true;}
static bool powered, keypadOpen;
static bool zm_access_powered() { return powered; }
static bool zm_access_keypad_open() { return keypadOpen; }
#include <cassert>
enum { ZM_NET_MAX_PLAYERS=4, ZM_CHAR_JILL=1 };
struct ZmNetPeerState { bool valid=false, dead=false, spectating=false; unsigned char stage=5,room=6; };
static ZmNetPeerState peers[4];
static unsigned char inventory[4][16], chars[4];
static unsigned int g_roomItemsFlags[8], g_LocksFlags[8], g_ScenarioFlags[8];
struct ItemSlot { unsigned char Id,qty; };
static ItemSlot g_itemboxSlots[48];
static bool missingCheckpoint;
static unsigned int crushed;
static int dropRoom=-1;
static unsigned char dropItem;
static const ZmNetPeerState* zm_net_player(int i) { return &peers[i]; }
static const ZmNetPeerState* zm_seat_state(int i) { return &peers[i]; }
static bool zm_net_inventory(int i,unsigned char out[16]) {
  if(missingCheckpoint)return false; memcpy(out,inventory[i],16); return true;
}
static int zm_net_char(int i) { return chars[i]; }
static bool zm_shotgun_crushed(int i) { return (crushed&(1u<<i))!=0; }
static bool Flg_ck(int base,int bit) { return (((unsigned int*)base)[bit/32]&(1u<<(31-bit%32)))!=0; }
static void flag(unsigned int* base,int bit,bool on=true) {
  if(on)base[bit/32]|=1u<<(31-bit%32);else base[bit/32]&=~(1u<<(31-bit%32));
}
unsigned int zm_random_progression_bit(unsigned char);
static unsigned int zm_drops_progression(const bool* reachable) {
  return dropRoom>=0&&reachable[dropRoom]?zm_random_progression_bit(dropItem):0;
}
'''

CHECKS = r'''
static void reset() {
    s_active=s_built=true; s_doorCount=s_spotCount=0; s_lockCount=1;
    s_lockFlag[0]=10; s_lockKey[0]=ITEM_ARMOR_KEY;
    memset(peers,0,sizeof(peers)); memset(inventory,0,sizeof(inventory));
    memset(g_roomItemsFlags,0,sizeof(g_roomItemsFlags)); memset(g_LocksFlags,0,sizeof(g_LocksFlags));
    memset(g_ScenarioFlags,0,sizeof(g_ScenarioFlags)); memset(g_itemboxSlots,0,sizeof(g_itemboxSlots));
    memset(s_roomInfo,0,sizeof(s_roomInfo)); crushed=0; missingCheckpoint=false; dropRoom=-1;
    peers[1].valid=true; peers[1].stage=5; peers[1].room=6;
}
static void door(int a,int b,int lock=0,int need=0) {
    s_doors[s_doorCount++]={5,(unsigned char)a,5,(unsigned char)b,(unsigned char)lock,(unsigned char)need,0,false,true};
}
static void spot(int room,int item,int f) {
    RndSpot& s=s_spots[s_spotCount++]; memset(&s,0,sizeof(s));
    s.stage=5;s.room=(unsigned char)room;s.id=(unsigned char)item;s.qty=1;s.flag=(unsigned char)f;
    flag(g_roomItemsFlags,f);
}
static void crests(int room) { for(int k=0;k<4;k++)spot(room,kCrests[k],20+k); }
int main() {
    // Lost sole key blocks the route; a surviving carrier restores it.
    reset(); door(6,1,0x80|10,ITEM_ARMOR_KEY);door(1,0x1B,0x80|23,0xff);crests(1);
    assert(zm_random_remaining_solvable()==0);
    inventory[1][0]=ITEM_ARMOR_KEY;inventory[1][1]=1;
    assert(zm_random_remaining_solvable()==1);
    // No phantom pickup at an already collected location.
    inventory[1][0]=inventory[1][1]=0;spot(6,ITEM_ARMOR_KEY,30);
    assert(zm_random_remaining_solvable()==1);flag(g_roomItemsFlags,30,false);
    assert(zm_random_remaining_solvable()==0);
    // An opened key door stays open after its key is destroyed.
    flag(g_LocksFlags,10);assert(zm_random_remaining_solvable()==1);
    // ... but a disarmed trigger (kDoorGates) is no route, lock or not.
    s_doors[0].usable=false;assert(zm_random_remaining_solvable()==0);s_doors[0].usable=true;
    flag(g_LocksFlags,10,false);
    // Shared box works only if a box room can actually be reached.
    g_itemboxSlots[0]={ITEM_ARMOR_KEY,1};s_roomInfo[0][1].safe=true;
    assert(zm_random_remaining_solvable()==0);s_roomInfo[0][6].safe=true;
    assert(zm_random_remaining_solvable()==1);g_itemboxSlots[0]={};
    // A reachable live drop, including a teammate's death loot, restores it.
    dropItem=ITEM_ARMOR_KEY;dropRoom=1;assert(zm_random_remaining_solvable()==0);
    dropRoom=6;assert(zm_random_remaining_solvable()==1);dropRoom=-1;
    // Permanently crushed inventories and starting rooms never count.
    peers[2].valid=true;peers[2].stage=5;peers[2].room=6;
    inventory[2][0]=ITEM_ARMOR_KEY;inventory[2][1]=1;
    assert(zm_random_remaining_solvable()==1);crushed=1u<<2;
    assert(zm_random_remaining_solvable()==0);
    missingCheckpoint=true;assert(zm_random_remaining_solvable()==-1);missingCheckpoint=false;
    // Each already inserted crest counts using its native flag, even before
    // the crest-door script runs. Verify all four item-to-flag mappings.
    flag(g_LocksFlags,10);const int placed[4]={0x6C,0x6B,0x6A,0x69};
    for(int k=0;k<4;k++) {
      flag(g_roomItemsFlags,20+k,false);assert(zm_random_remaining_solvable()==0);
      flag(g_ScenarioFlags,placed[k]);assert(zm_random_remaining_solvable()==1);
    }
    // Open crest door makes losing unused duplicate crests harmless.
    memset(g_ScenarioFlags,0,sizeof(g_ScenarioFlags));flag(g_LocksFlags,23);
    assert(zm_random_remaining_solvable()==1);
    // Closure cannot invent a key behind its own door.
    reset();door(6,1,0x80|10,ITEM_ARMOR_KEY);door(1,0x1B);spot(1,ITEM_ARMOR_KEY,0);
    assert(zm_random_remaining_solvable()==0);
    // Jill's carried lockpick provides the sword-key alternative.
    s_lockKey[0]=ITEM_SWORD_KEY;chars[1]=ZM_CHAR_JILL;
    inventory[1][0]=ITEM_LOCK_PICK;inventory[1][1]=1;
    assert(zm_random_remaining_solvable()==1);
    inventory[1][0]=inventory[1][1]=0;s_roomInfo[0][6].safe=true;
    g_itemboxSlots[0]={ITEM_LOCK_PICK,1};assert(zm_random_remaining_solvable()==1);
    g_itemboxSlots[0]={};dropRoom=6;dropItem=ITEM_LOCK_PICK;
    assert(zm_random_remaining_solvable()==1);
    // The back area (ZombieKeypad.cpp): the elevator's door takes the battery
    // or its power, the keypad door the note (read where it lies) or the
    // keypad already open.
    reset();chars[1]=0;door(6,1);s_doors[0].access=RND_BIT_BATTERY;door(1,0x1B);
    assert(zm_random_remaining_solvable()==0);
    inventory[1][0]=ITEM_BATTERY;inventory[1][1]=1;assert(zm_random_remaining_solvable()==1);
    inventory[1][0]=inventory[1][1]=0;assert(zm_random_remaining_solvable()==0);
    powered=true;assert(zm_random_remaining_solvable()==1);powered=false;
    s_doors[0].access=RND_BIT_NOTE;assert(zm_random_remaining_solvable()==0);
    spot(6,ITEM_ZM_PASS_NOTE,31);assert(zm_random_remaining_solvable()==1);
    flag(g_roomItemsFlags,31,false);assert(zm_random_remaining_solvable()==0);
    keypadOpen=true;assert(zm_random_remaining_solvable()==1);keypadOpen=false;
    puts("Remaining progression proof OK");
}
'''

production = (routes.ROOT / "src/game/mods/ZombieRandom.cpp").read_text()
runtime = "unsigned int zm_random_progression_bit" + routes.between(
    production, "unsigned int zm_random_progression_bit", "// Pickups of our own")
# Reuse the adapter's real progression structs and key/door helpers, then add
# the actual runtime proof against synthetic inventories, flags and drops.
source = routes.adapter_source().split("static void generate(unsigned int seed) {")[0]
with tempfile.TemporaryDirectory(prefix="re1-progression-loss-") as temp:
    with patch.object(routes, "adapter_source", return_value=source + STUBS + runtime + CHECKS):
        executable = routes.build_adapter(Path(temp))
    subprocess.run([str(executable)], check=True)
