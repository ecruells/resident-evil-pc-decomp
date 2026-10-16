"""CPU-only shared-box competition, retry and reconnect regressions.
Compiles the production transaction code; never launches the game.
Run from an x86 VS prompt, or pass --compiler g++ on Linux.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = r'''
#include <cassert>
#include <cstring>
#include <cstdio>
#include <vector>
enum { ZM_NET_OFF, ZM_NET_ZOMBIE, ZM_NET_SURVIVOR };
enum { ITEM_SWORD_KEY=0x33, ITEM_HELMET_KEY=0x36, ITEM_WIND_CREST=0x29,
       ITEM_MOON_CREST=0x2C, ITEM_STAR_CREST=0x2D, ITEM_SUN_CREST=0x2E };
enum { ZM_NET_CONNECTED=3, ZM_NET_LOST=4, ZM_NET_EXPIRED=6, ZM_NET_DIRECTOR=0, ZM_NET_ALL=255, ZM_EV_BOX=16, ZM_CHAR_JILL=1 };
struct ItemSlot { unsigned char Id,qty; };
struct ZmReconnectPlayer { unsigned short boxToken; struct { unsigned char totalHeldItems,equippedItemId; } card;
    unsigned char inventory[16],indices[8]; unsigned int inventoryMask; };
struct ZmNetPeerState { bool dead,spectating,transitioning; };
static ZmNetPeerState peers[4];
static bool connected[4]={false,true,true,true},armed=true,over=false;
static int role=ZM_NET_ZOMBIE,status=ZM_NET_CONNECTED,self=0;
static int netRole=-1;          // the host of an AI director's game: ZOMBIE with role SURVIVOR
static bool hostPlays=false;    // ...whose seat 0 is a survivor
static unsigned int now=100,seed=123;
static unsigned char inventory[16];
static unsigned char* g_ItemSlotsPointer=inventory;
static ItemSlot g_itemboxSlots[48],hostBox[48];
static struct { short health; } g_playerEntity;
static bool zombie_mode_armed() { return armed; }
static bool zombie_mode_match_over() { return over; }
static int zm_game_role() { return role; }
static int zm_net_role() { return netRole>=0 ? netRole : role; }
static int zm_net_self() { return self; }
static int zm_net_status() { return status; }
static bool zm_net_active() { return status==ZM_NET_CONNECTED || status==ZM_NET_LOST; }
static int zm_net_char(int p) { return p==0 ? (hostPlays ? 0 : -1) : p==2 ? ZM_CHAR_JILL : 0; }
static int zombie_mode_inventory_slots(int) { return self==2 ? 8 : 6; }
static unsigned int zm_net_seed() { return seed; }
static unsigned int plat_time_ms() { return now; }
static const ZmNetPeerState* zm_net_player(int p) { return connected[p] ? &peers[p] : nullptr; }
static ZmNetPeerState hostSeat;
static const ZmNetPeerState* zm_seat_state(int p) {
    if (p==0) return hostPlays ? &hostSeat : nullptr;
    return zm_net_player(p);
}
struct Event { int dst; short a[8]; };
static std::vector<Event> sent;
static void zm_net_send_event_to(int dst,int kind,short a,short b,short c,short d,short e,short f,short g,short h) {
    assert(kind==ZM_EV_BOX);sent.push_back({dst,{a,b,c,d,e,f,g,h}});
}
static void zm_reinforce_notice(const char*) {}
static void Task_sleep(int) { now+=100; }
static bool zombie_mode_menu_frame();
'''
CHECKS = r'''
static bool simulate,dropRequest,dropDone;
static int exchanges;
static void reset() {
    zm_box_reset();memset(g_itemboxSlots,0,sizeof(g_itemboxSlots));memset(hostBox,0,sizeof(hostBox));
    memset(inventory,0,sizeof(inventory));memset(peers,0,sizeof(peers));sent.clear();
    for(int i=1;i<4;i++)connected[i]=true;
    role=ZM_NET_ZOMBIE;status=ZM_NET_CONNECTED;self=0;armed=true;over=false;now=100;g_playerEntity.health=140;
    netRole=-1;hostPlays=false;memset(&hostSeat,0,sizeof(hostSeat));
    simulate=dropRequest=dropDone=false;exchanges=0;
}
static void request(short* a,int token,int slot,int expected,int offered,int inventorySlot=0) {
    short r[8]={BOX_REQUEST,(short)token,(short)slot,(short)expected,(short)offered,(short)inventorySlot,(short)seed,0};memcpy(a,r,sizeof(r));
}
static bool zombie_mode_menu_frame() {
    assert(simulate);std::vector<Event> outgoing=sent;sent.clear();
    ItemSlot clientBox[48];memcpy(clientBox,g_itemboxSlots,sizeof(clientBox));
    role=ZM_NET_ZOMBIE;memcpy(g_itemboxSlots,hostBox,sizeof(hostBox));
    for(const Event& e:outgoing)if(e.a[0]==BOX_REQUEST) {
        if(dropRequest){dropRequest=false;continue;}zm_box_take(e.a,self);exchanges++;
    }
    memcpy(hostBox,g_itemboxSlots,sizeof(hostBox));role=ZM_NET_SURVIVOR;memcpy(g_itemboxSlots,clientBox,sizeof(clientBox));
    outgoing=sent;sent.clear();
    for(const Event& e:outgoing)if(e.dst==self||e.dst==ZM_NET_ALL) {
        if(e.a[0]==BOX_DONE&&dropDone){dropDone=false;continue;}zm_box_take(e.a,0);
    }
    return false;
}
int main() {
    short a[8],b[8];
    // Any inventory item may go in the box, keys and crests included,
    // into an empty slot or swapped with an occupied one.
    const unsigned char progression[]={0x33,0x34,0x35,0x36,0x29,0x2C,0x2D,0x2E};
    for(unsigned char id:progression) for(int occupied=0;occupied<2;occupied++) {
        reset();g_itemboxSlots[0]={(unsigned char)(occupied?12:0),(unsigned char)(occupied?15:0)};
        unsigned short before=box_item(g_itemboxSlots[0]);
        request(a,1,0,before,id|(1<<8));zm_box_take(a,1);
        assert(sent.back().a[0]==BOX_DONE&&box_item(g_itemboxSlots[0])==(unsigned short)(id|(1<<8))&&s_revision==1);
    }
    // Competing withdrawals: one owner, one denial. Empty weapons (qty 0)
    // are still items; quantities are not used to identify an empty slot.
    reset();g_itemboxSlots[0]={3,0};request(a,1,0,3,0);request(b,1,0,3,0);
    zm_box_take(a,1);assert(sent.back().a[0]==BOX_DONE&&!g_itemboxSlots[0].Id);
    zm_box_take(b,2);assert(sent.back().a[0]==BOX_DENY);
    // Depositing different items in the same empty slot cannot overwrite.
    reset();request(a,1,0,0,3|(7<<8));request(b,1,0,0,12|(20<<8));
    zm_box_take(a,1);zm_box_take(b,2);assert(sent.back().a[0]==BOX_DENY&&g_itemboxSlots[0].Id==3&&g_itemboxSlots[0].qty==7);
    // A swap is atomic: two simultaneous swaps leave the loser untouched.
    reset();g_itemboxSlots[0]={12,15};request(a,1,0,12|(15<<8),3|(7<<8));request(b,1,0,12|(15<<8),25);
    zm_box_take(a,1);zm_box_take(b,2);assert(sent.back().a[0]==BOX_DENY&&box_item(g_itemboxSlots[0])==(3|(7<<8)));
    int messages=(int)sent.size();zm_box_take(a,1);assert(sent.size()==messages+2&&sent.back().a[0]==BOX_DONE&&box_item(g_itemboxSlots[0])==(3|(7<<8)));
    // Another player can take the new contents; an old semantic retry must
    // resend its receipt without resurrecting its outdated broadcast.
    request(b,2,0,3|(7<<8),0);zm_box_take(b,2);zm_box_take(a,1);assert(!g_itemboxSlots[0].Id&&sent.back().a[0]==BOX_DONE);
    // Forged/source/seed/slot/dead/spectator requests cannot mutate the box.
    reset();request(a,1,0,0,3);zm_box_take(a,0);assert(sent.empty());
    a[6]++;zm_box_take(a,1);assert(sent.empty());a[6]--;
    a[2]=48;zm_box_take(a,1);assert(sent.empty());a[2]=0;
    peers[1].dead=true;zm_box_take(a,1);assert(sent.back().a[0]==BOX_DENY&&!g_itemboxSlots[0].Id);
    // Two commits after the last checkpoint reconcile in token order exactly
    // once, including removing a deposited weapon and recovering a withdrawal.
    reset();request(a,1,0,0,3|(7<<8),0);zm_box_take(a,1);
    g_itemboxSlots[1]={12,20};request(b,2,1,12|(20<<8),0,4);zm_box_take(b,1);
    ZmReconnectPlayer cp={};cp.card.totalHeldItems=2;cp.card.equippedItemId=1;
    cp.inventory[0]=3;cp.inventory[1]=7;cp.inventory[2]=25;cp.indices[0]=0;cp.indices[1]=1;cp.inventoryMask=3;
    zm_box_reconnect_reconcile(1,&cp);
    assert(cp.boxToken==2&&cp.card.totalHeldItems==2&&cp.inventory[0]==25&&cp.inventory[2]==12&&cp.inventory[3]==20&&!cp.card.equippedItemId);
    ZmReconnectPlayer again=cp;zm_box_reconnect_reconcile(1,&cp);assert(!memcmp(&again,&cp,sizeof(cp)));
    zm_box_checkpoint(1,cp.boxToken);for(const BoxReceipt& r:s_receipts[1])assert(!r.used);
    // Full receipt storage refuses transfers until checkpoint acknowledgement,
    // without silently discarding crash recovery data.
    reset();for(int i=0;i<32;i++){request(a,i+1,i,0,3);zm_box_take(a,1);assert(sent.back().a[0]==BOX_DONE);}
    request(a,33,32,0,3);zm_box_take(a,1);assert(sent.back().a[0]==BOX_DENY&&!g_itemboxSlots[32].Id);
    zm_box_checkpoint(1,32);request(a,34,32,0,3);zm_box_take(a,1);assert(sent.back().a[0]==BOX_DONE);
    // Lost request and lost receipt: retry confirms one swap; the caller's
    // inventory stays unchanged until approval, and no world-ammo bonus applies.
    reset();role=ZM_NET_SURVIVOR;self=1;simulate=dropRequest=dropDone=true;
    inventory[0]=3;inventory[1]=7;g_itemboxSlots[0]=hostBox[0]={12,15};
    unsigned char item=12,qty=15;assert(zombie_mode_box_claim(0,0,&item,&qty));
    assert(inventory[0]==3&&inventory[1]==7&&hostBox[0].Id==3&&hostBox[0].qty==7&&exchanges==2);
    inventory[0]=item;inventory[1]=qty;zombie_mode_box_finished();assert(inventory[1]==15&&s_awarded==1&&!s_waiting);
    // A stale client loses a conflict without spending its offered item.
    reset();role=ZM_NET_SURVIVOR;self=1;simulate=true;inventory[0]=3;inventory[1]=7;
    hostBox[0]={25,1};item=12;qty=15;assert(!zombie_mode_box_claim(0,0,&item,&qty));
    assert(inventory[0]==3&&inventory[1]==7&&g_itemboxSlots[0].Id==25&&!s_waiting);
    // Full host snapshots repair missed updates. Older updates/snapshots
    // cannot roll back the current contents, even across a revision wrap.
    ItemSlot snapshot[48]={};snapshot[0]={3,7};zm_box_snapshot_take(10,snapshot);
    short update[8]={BOX_UPDATE,9,0,0,12,0,(short)seed,0};zm_box_take(update,0);
    assert(g_itemboxSlots[0].Id==3);snapshot[0]={25,1};zm_box_snapshot_take(9,snapshot);assert(g_itemboxSlots[0].Id==3);
    zm_box_snapshot_take(10,snapshot);assert(g_itemboxSlots[0].Id==25);
    s_revision=0xFFFFFFFE;s_revisionHave=true;zm_box_snapshot_take(1,snapshot);assert(s_revision==1);
    // The host of an AI director's game plays seat 0: its own swap is granted
    // like a survivor's, and the host's box is never overwritten by its own
    // update. Its reply reaches its own waiting claim.
    reset();netRole=ZM_NET_ZOMBIE;role=ZM_NET_SURVIVOR;hostPlays=true;
    g_itemboxSlots[0]={12,15};request(a,1,0,12|(15<<8),3|(7<<8));zm_box_take(a,0);
    assert(sent.back().a[0]==BOX_DONE&&sent.back().dst==0&&box_item(g_itemboxSlots[0])==(3|(7<<8)));
    for(const BoxReceipt& r:s_receipts[0])assert(!r.used);    // the host never reconnects
    short stale[8]={BOX_UPDATE,0,0,0,25,0,(short)seed,0};zm_box_take(stale,0);assert(g_itemboxSlots[0].Id==3);
    hostSeat.dead=true;request(a,2,0,3|(7<<8),0);zm_box_take(a,0);assert(sent.back().a[0]==BOX_DENY);
    // The ordinary single-player box needs no network approval.
    role=ZM_NET_OFF;assert(zombie_mode_box_claim(0,0,&item,&qty));
    puts("Box competition, swaps, conservation, retries, stale requests and reconnect receipt checks passed.");
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="cl")
    args = parser.parse_args()
    production = (ROOT / "src/game/mods/ZombieBox.cpp").read_text()
    production = re.sub(r"^#include[^\n]*\n", "", production, flags=re.M)
    with tempfile.TemporaryDirectory(prefix="re1-box-") as temp:
        directory = Path(temp)
        cpp, exe = directory / "test.cpp", directory / "test.exe"
        cpp.write_text(FIXTURE + production + CHECKS)
        command = ([args.compiler, "/nologo", "/EHsc", "/std:c++17", str(cpp), "/Fe" + str(exe)]
                   if Path(args.compiler).stem.lower() == "cl" else
                   [args.compiler, "-m32", "-std=c++17", str(cpp), "-o", str(exe)])
        subprocess.run(command, cwd=directory, check=True)
        subprocess.run([str(exe)], cwd=directory, check=True)


if __name__ == "__main__":
    main()
