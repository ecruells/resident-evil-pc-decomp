"""CPU-only pickup arbitration regressions; does not launch the game.

Run from an x86 VS Developer Command Prompt: python tests/test_zombie_pickups.py
Linux: python3 tests/test_zombie_pickups.py --compiler g++
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    match = re.search(r"^(?:static )?(?:void|bool|unsigned int) " + name + r"\([^;]*?\)\s*\{", source, re.M)
    assert match, name
    end = match.end()
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end] + "\n"

FIXTURE = r'''
#include <cassert>
#include <cstring>
#include <cstdio>
#include <vector>
enum { ZM_NET_OFF, ZM_NET_ZOMBIE, ZM_NET_SURVIVOR };
enum { ZM_NET_IDLE, ZM_NET_HOSTING, ZM_NET_JOINING, ZM_NET_CONNECTED, ZM_NET_LOST };
enum { ZM_NET_MAX_PLAYERS = 4, ZM_NET_DIRECTOR = 0, ZM_NET_ALL = 255,
       ROOM_ACTION_ENTRIES = 128, ROOM_ITEM_MODELS = 64, ZM_DROPS_PER_ROOM = 40,
       ZM_EV_PICKUP = 30, ZM_EV_SHOTGUN=31, ZM_EV_PIANO=32, ZM_EV_TIMEOUT=34, ZM_EV_BOSS=35, ZM_EV_DROP = 18, ZM_EV_DROP_TAKE = 19,
       ZM_EV_WIN = 9, ZM_EV_REVIVE = 21, ZM_EV_ROSTER = 4, ZM_END_ESCAPE = 0,
       ZM_NET_MAX_PENDING = 96 };
static bool armed = true, over;
static int role = ZM_NET_ZOMBIE, self, status = ZM_NET_CONNECTED;
static unsigned int seed = 0x89ABCDEF, now;
static struct { short health = 140; unsigned char id; } g_playerEntity;
static unsigned char g_stageId = 5, g_roomId = 9, g_selectedItemId, g_pickedItemId;
static unsigned char g_RoomActionTable[ROOM_ACTION_ENTRIES * 12];
static unsigned char models[ROOM_ITEM_MODELS][0xA4];
static void* g_item_model_table[ROOM_ITEM_MODELS];
static unsigned char g_roomItemsFlags[32], dropMask[32];
static unsigned int g_effectPool[64][0x21];
static int g_freeEffectSlots;
static void* g_pRoomActionEntry;
static unsigned char inventory[16], *g_ItemSlotsPointer = inventory;
static unsigned char g_TotalHeldItems, g_ItemSlotIndices[8];
static unsigned int g_ItemSlotsBitmask;
static int slots = 6;
struct ZmReconnectPlayer {
    unsigned short pickupToken;
    struct { unsigned char totalHeldItems; } card;
    unsigned char inventory[16], indices[8];
    unsigned int inventoryMask;
};
static bool hostPlays;          // an AI director's game: the host is seat 0's survivor
static int netRole = -1;        // ...its net role (ZOMBIE) beside its game role (SURVIVOR)
static int zm_net_char(int player) { return player == 0 ? (hostPlays ? 0 : -1) : player == 1 ? 0 : 1; }
enum { ZM_CHAR_CHRIS=0, ZM_CHAR_JILL=1 };
enum { ZM_EV_BOX=16 };
static bool zm_shotgun_pickup_valid(const short* a) {return a[2] || (unsigned short)a[3]<=255;}
static unsigned short zm_shotgun_pickup_identity(const unsigned char* r) {return r[20];}
static void zm_shotgun_pickup(const short*) {}
static void zm_shotgun_take(const short*,int) {}
static void zm_piano_take(const short*,int) {}
static void zm_yawn_take(const short*,int) {}
static void zm_timeout_take(const short*,int) {}
static bool zombie_mode_box_waiting() { return false; }
static void zm_box_take(const short*,int) {}
struct ZmNetPeerState { bool dead; };
static ZmNetPeerState peers[4];
static bool connected[4] = {false, true, true, true};
static bool zombie_mode_armed() { return armed; }
static bool zombie_mode_match_over() { return over; }
static int zm_game_role() { return role; }
static int zm_net_role() { return netRole >= 0 ? netRole : role; }
static bool zm_net_active() { return status == ZM_NET_CONNECTED || status == ZM_NET_LOST; }
static int zm_net_status() { return status; }
static unsigned int zm_net_seed() { return seed; }
static unsigned int plat_time_ms() { return now; }
static const ZmNetPeerState* zm_net_player(int p) { return connected[p] ? &peers[p] : NULL; }
static ZmNetPeerState hostSeat;
static const ZmNetPeerState* zm_seat_state(int p) { return p == 0 ? (hostPlays ? &hostSeat : NULL) : zm_net_player(p); }
static int zombie_mode_inventory_slots(int) { return slots; }
static unsigned char dc_item_pickup_quantity(unsigned char, unsigned char q) { return q; }
static unsigned char zombie_mode_pickup_quantity(unsigned char, unsigned char q, const unsigned char*) { return q; }
static const char* notice;
static void zm_reinforce_notice(const char* text) {notice=text;}
static void dbg_printf(const char*, ...) {}
static void memset_(unsigned int* p, int n) { memset(p, 0, n * 4); }
static void LoadHeldItemsImages() {}
static void StMask(int, int) {}
static void zombie_mode_pickup_finished() {}
// The engine's MSB-first bit order, within each 32-bit word.
void Flg_on(int addr, unsigned int f) { ((unsigned int*)addr)[f / 32] |= 0x80000000u >> (f % 32); }
static unsigned int Flg_ck(int addr, unsigned int f) { return ((unsigned int*)addr)[f / 32] & (0x80000000u >> (f % 32)); }
static void FUN_00473f10(int* p, unsigned int f) { ((unsigned int*)p)[f / 32] &= ~(0x80000000u >> (f % 32)); }
static unsigned char zm_drops_flag_mask(int i) { return dropMask[i]; }
static bool zm_drops_on() { return armed && role != ZM_NET_OFF; }
struct ZmDrop { bool live; unsigned short uid; unsigned char stage, room, id, qty; };
static ZmDrop s_drops[256];
static struct { bool alloc, used; int drop; } s_here[ZM_DROPS_PER_ROOM];
static unsigned char s_ops[ZM_DROPS_PER_ROOM][0x20];
static int zm_drop_find(unsigned short uid) {
    for (int i = 0; i < 256; i++) if (s_drops[i].live && s_drops[i].uid == uid) return i;
    return -1;
}
static int dropTakes;
static void zm_drops_take_event(int kind, const short* a, int) {
    if (kind != ZM_EV_DROP_TAKE) return;
    int i = zm_drop_find((unsigned short)a[0]);
    if (i >= 0) { s_drops[i].live = false; dropTakes++; }
}
struct NetEvent { unsigned char kind, src, dst; short args[8]; unsigned short id; };
static std::vector<NetEvent> sent, wire;
static bool simulate, silence;
static bool loseCommit, loseReceipt;
static int abortAt, pollCount, delays;
static void zm_net_send_event_to(int dst, int kind, short a0, short a1, short a2, short a3,
                                 short a4, short a5, short a6, short a7) {
    NetEvent e = {(unsigned char)kind, (unsigned char)self, (unsigned char)dst,
                 {a0,a1,a2,a3,a4,a5,a6,a7}};
    sent.push_back(e);
    if (kind == ZM_EV_PICKUP && a0 == 4 && loseCommit) { loseCommit = false; return; }
    if (kind == ZM_EV_PICKUP && a0 == 6 && loseReceipt) { loseReceipt = false; return; }
    if (simulate) wire.push_back(e);
}
static void zm_net_poll();
static bool zombie_mode_menu_frame() { zm_net_poll(); return g_playerEntity.health < 0; }
static void Task_sleep(int) { now += 100; }
static int s_role, s_self, s_inboxCount, relays;
static struct { NetEvent ev; } s_inbox[2];
static NetEvent s_pendingWin;
static bool s_pendingWinHave;
static bool zm_match_take_win(const short*, int) { return true; }
static bool zm_revive_host_claim(short, short, int) { return false; }
struct NetLink { bool used; int pendingCount; unsigned short nextEventId; NetEvent pending[96]; };
static NetLink s_links[4];
static void net_route(const NetEvent&, int) { relays++; }
static void zm_world_apply_remote(const short*,int) {}
'''

CHECKS = r'''
static void zm_net_poll() {
    pollCount++;
    if (abortAt == pollCount) g_playerEntity.health = -1;
    if (silence || delays-- > 0 || wire.empty()) return;
    NetEvent e = wire.front(); wire.erase(wire.begin());
    int savedRole = role, savedSelf = self;
    if (e.dst == 0) { role = ZM_NET_ZOMBIE; self = 0; }
    else { role = ZM_NET_SURVIVOR; self = 1; }
    s_role = role; s_self = self;
    net_take_event(e, e.dst == 0 ? e.src : 0);
    role = savedRole; self = savedSelf;
}
static void reset() {
    zm_pickups_reset(); sent.clear(); wire.clear();
    role = ZM_NET_ZOMBIE; self = 0; armed = true; over = false;
    simulate = silence = false; abortAt = pollCount = delays = 0;
    loseCommit = loseReceipt = false;
    status = ZM_NET_CONNECTED; now = 0;
    memset(g_RoomActionTable, 0, sizeof(g_RoomActionTable));
    memset(g_roomItemsFlags, 255, sizeof(g_roomItemsFlags));
    memset(dropMask, 0, sizeof(dropMask));
    memset(s_drops, 0, sizeof(s_drops)); memset(s_here, 0, sizeof(s_here));
    memset(inventory, 0, sizeof(inventory)); memset(models, 0, sizeof(models));
    g_TotalHeldItems = g_ItemSlotsBitmask = g_freeEffectSlots = dropTakes = 0;
    for (int i = 0; i < ROOM_ITEM_MODELS; i++) g_item_model_table[i] = models[i];
    for (int i = 1; i < 4; i++) { connected[i] = true; peers[i].dead = false; }
    g_playerEntity.health = 140; slots = 6;
    hostPlays = false; netRole = -1; hostSeat.dead = false;
}
static void request(short* a, int token, bool drop = false, int key = 17) {
    short r[8] = {PK_REQUEST, (short)token, (short)drop, (short)key,
        (short)(5 | (9 << 8)), (short)(ITEM_CLIP | (15 << 8)),
        (short)seed, (short)(seed >> 16)};
    memcpy(a, r, sizeof(r));
}
static unsigned char* floor_item(int flag = 17) {
    static unsigned char rec[0x20]; memset(rec, 0, sizeof(rec));
    rec[8] = ITEM_CLIP; rec[9] = 15; rec[10] = 0; rec[0x14] = flag;
    g_RoomActionTable[0] = 4;
    *(unsigned char**)(g_RoomActionTable + 8) = rec;
    g_pRoomActionEntry = g_RoomActionTable;
    g_selectedItemId = rec[8];
    models[0][0] = 1; *(unsigned short*)(models[0] + 0x86) = 3;
    return rec;
}
int main() {
    short a[8], b[8];
    // Three competing requests get one grant, one consume, one private receipt.
    reset(); request(a, 1); zm_pickups_take(a, 1); assert(sent.back().args[0] == PK_GRANT);
    request(b, 1); zm_pickups_take(b, 2); assert(sent.back().args[0] == PK_DENY);
    zm_pickups_take(b, 3); assert(sent.back().args[0] == PK_DENY);
    zm_pickups_take(a, 1); assert(sent.back().args[0] == PK_GRANT);
    a[0] = PK_COMMIT; zm_pickups_take(a, 1);
    assert(!Flg_ck((int)g_roomItemsFlags, 17) && sent.back().args[0] == PK_DONE);
    int n = (int)sent.size(); zm_pickups_take(a, 1);
    assert(sent.size() == n + 2 && sent.back().args[0] == PK_DONE);
    request(b, 2); zm_pickups_take(b, 2); assert(sent.back().args[0] == PK_DENY);
    // Cancel, reordered cancel/request, stale messages and disconnected claims.
    reset(); request(a, 1); zm_pickups_take(a, 1);
    a[0] = PK_CANCEL; zm_pickups_take(a, 1);
    request(b, 2); zm_pickups_take(b, 2); assert(sent.back().args[0] == PK_GRANT);
    a[0] = PK_COMMIT; zm_pickups_take(a, 1); assert(sent.back().args[0] == PK_DENY);
    reset(); request(a, 1); zm_pickups_take(a, 1); now += 10000;
    zm_pickups_frame(); assert(!s_claims[1].active && sent.back().args[0] == PK_DENY);
    request(b, 1); zm_pickups_take(b, 2); assert(sent.back().args[0] == PK_GRANT);
    a[0] = PK_COMMIT; zm_pickups_take(a, 1); assert(sent.back().args[0] == PK_DENY);
    reset(); request(a, 3); a[0] = PK_CANCEL; zm_pickups_take(a, 1);
    a[0] = PK_REQUEST; zm_pickups_take(a, 1); assert(sent.back().args[0] == PK_DENY);
    request(a, 2); n = (int)sent.size(); zm_pickups_take(a, 1); assert(sent.size() == n);
    reset(); request(a, 1); zm_pickups_take(a, 1); connected[1] = false;
    request(b, 1); zm_pickups_take(b, 2); assert(sent.back().args[0] == PK_GRANT);
    a[0] = PK_COMMIT; zm_pickups_take(a, 1); assert(sent.back().args[0] == PK_DENY);
    // Drop identities, not per-copy model slots/flags, settle competition.
    reset(); s_drops[0] = {true, 0x1007, 5, 9, ITEM_CLIP, 15};
    request(a, 1, true, 0x1007); zm_pickups_take(a, 1); assert(sent.back().args[0] == PK_GRANT);
    request(b, 1, true, 0x1007); zm_pickups_take(b, 2); assert(sent.back().args[0] == PK_DENY);
    a[0] = PK_COMMIT; zm_pickups_take(a, 1); zm_pickups_take(a, 1);
    assert(!s_drops[0].live && dropTakes == 1);
    // Host/source/seed checks; critical delivery with an already-full inbox.
    reset(); request(a, 1); a[6] ^= 1; n = (int)sent.size(); zm_pickups_take(a, 1); assert(sent.size() == n);
    request(a, 1); peers[1].dead = true; zm_pickups_take(a, 1); assert(sent.back().args[0] == PK_DENY);
    reset(); request(a, 1); NetEvent e = {ZM_EV_PICKUP, 1, 0, {}}; memcpy(e.args, a, sizeof(a));
    s_role = ZM_NET_ZOMBIE; s_self = 0; s_inboxCount = 2;
    net_take_event(e, 2); assert(sent.empty());
    net_take_event(e, 1); assert(sent.back().args[0] == PK_GRANT && s_inboxCount == 2);
    // Exercise the actual original award through both host round trips.
    reset(); floor_item(); role = ZM_NET_SURVIVOR; self = 1; simulate = true; delays = 3;
    room_event_item_pickup();
    assert(g_TotalHeldItems == 1 && inventory[0] == ITEM_CLIP && inventory[1] == 15);
    assert(g_freeEffectSlots == 1 && !models[0][0] && !g_RoomActionTable[0]);
    room_event_item_pickup(); assert(g_TotalHeldItems == 1 && inventory[1] == 15);
    assert(notice && !strcmp(notice,"SOMEONE ELSE TOOK THAT ITEM"));
    // Reserved outgoing capacity and semantic retries cover dropped enqueues.
    reset(); s_links[1].used = true; s_links[1].pendingCount = 72;
    NetEvent visual = {5,0,1,{}}; NetEvent critical = {ZM_EV_PICKUP,0,1,{}};
    net_queue(1, visual); assert(s_links[1].pendingCount == 72);
    net_queue(1, critical); assert(s_links[1].pendingCount == 73);
    reset(); floor_item(); role = ZM_NET_SURVIVOR; self = 1; simulate = true;
    loseCommit = loseReceipt = true;
    room_event_item_pickup(); assert(g_TotalHeldItems == 1 && inventory[1] == 15);
    assert(!loseCommit && !loseReceipt && now >= 1000);
    // Full inventories refuse before a request; a matching stack may fit.
    reset(); floor_item(); role = ZM_NET_SURVIVOR; self = 1; simulate = true;
    g_TotalHeldItems = 6; inventory[0] = ITEM_CLIP; inventory[1] = 240;
    room_event_item_pickup(); assert(sent.empty() && inventory[1] == 240 && g_RoomActionTable[0] == 4);
    inventory[1] = 230; room_event_item_pickup(); assert(inventory[1] == 245 && g_TotalHeldItems == 6);
    // A dead requester cancels before commit; no award or world consumption.
    reset(); floor_item(); role = ZM_NET_SURVIVOR; self = 1; simulate = true; abortAt = 2;
    room_event_item_pickup(); assert(!g_TotalHeldItems && Flg_ck((int)g_roomItemsFlags, 17));
    // Silence releases the pending request without an inventory write.
    reset(); floor_item(); role = ZM_NET_SURVIVOR; self = 1; simulate = silence = true;
    room_event_item_pickup(); assert(!g_TotalHeldItems && !s_waiting && now >= 5000);
    assert(sent.back().args[0] == PK_CANCEL);
    // A crash between host commit and the next checkpoint restores the award
    // exactly once, using Chris's bonus only for world ammo.
    reset(); request(a, 1); zm_pickups_take(a, 1); a[0]=PK_COMMIT; zm_pickups_take(a, 1);
    ZmReconnectPlayer checkpoint = {};
    zm_pickups_reconnect_reconcile(1, &checkpoint);
    assert(checkpoint.card.totalHeldItems==1 && checkpoint.inventory[0]==ITEM_CLIP && checkpoint.inventory[1]==22);
    zm_pickups_reconnect_reconcile(1, &checkpoint);
    assert(checkpoint.card.totalHeldItems==1 && checkpoint.inventory[1]==22);
    // The host of an AI director's game claims as seat 0: granted, committed
    // and contested like any survivor; replies to it reach its own claim.
    reset(); hostPlays = true; netRole = ZM_NET_ZOMBIE; role = ZM_NET_SURVIVOR;
    request(a, 1); zm_pickups_take(a, 0); assert(sent.back().args[0] == PK_GRANT && sent.back().dst == 0);
    request(b, 1); zm_pickups_take(b, 2); assert(sent.back().args[0] == PK_DENY);
    a[0] = PK_COMMIT; zm_pickups_take(a, 0);
    assert(!Flg_ck((int)g_roomItemsFlags, 17) && sent.back().args[0] == PK_DONE && sent.back().dst == 0);
    request(a, 2); a[3] = 18; memcpy(s_request, a, sizeof(a)); s_waiting = true; s_reply = -1; s_expected = PK_GRANT;
    a[0] = PK_GRANT; zm_pickups_take(a, 0); assert(s_reply == PK_GRANT);
    s_waiting = false; hostSeat.dead = true; request(a, 3); a[3] = 19; zm_pickups_take(a, 0);
    assert(sent.back().args[0] == PK_DENY);
    // Without a seat of its own the host refuses a forged seat-0 request.
    reset(); request(a, 1); n = (int)sent.size(); zm_pickups_take(a, 0); assert(sent.size() == n);
    // Single player retains the engine's immediate award.
    reset(); floor_item(); role = ZM_NET_OFF;
    room_event_item_pickup(); assert(g_TotalHeldItems == 1 && sent.empty());
    puts("Pickup competition, conservation, retries, cancellation and critical-routing checks passed.");
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="cl")
    args = parser.parse_args()
    types = (ROOT / "src/game/Types.h").read_text(encoding="utf-8")
    constants = "\n".join(re.findall(
        r"^#define ITEM_(?:CLIP|ROCKET_LAUNCHER|EMPTY_BOTTLE|INK_RIBBONS)\s+[^\r\n]+", types, re.M))
    production = (ROOT / "src/game/mods/ZombiePickups.cpp").read_text(encoding="utf-8")
    production = re.sub(r"^#include[^\n]*\n", "", production, flags=re.M)
    drops = (ROOT / "src/game/mods/ZombieDrops.cpp").read_text(encoding="utf-8")
    helpers = "".join(function(drops, name) for name in (
        "zm_drop_is_pickup", "zm_drop_pickup_uid", "zm_drop_available"))
    net_source = (ROOT / "src/game/mods/ZombieNet.cpp").read_text(encoding="utf-8")
    network = function(net_source, "net_queue") + function(net_source, "net_take_event")
    award = function((ROOT / "src/game/RoomEvents.cpp").read_text(encoding="utf-8"), "room_event_item_pickup")
    with tempfile.TemporaryDirectory(prefix="re1-pickups-") as temp:
        directory = Path(temp)
        cpp, exe = directory / "test.cpp", directory / "test.exe"
        cpp.write_text(constants + "\n" + FIXTURE + helpers + production + network + award + CHECKS, encoding="utf-8")
        if Path(args.compiler).stem.lower() == "cl":
            command = [args.compiler, "/nologo", "/EHsc", "/std:c++17", str(cpp), "/Fe" + str(exe)]
        else:
            command = [args.compiler, "-m32", "-std=c++17", str(cpp), "-o", str(exe)]
        subprocess.run(command, cwd=directory, check=True)
        subprocess.run([str(exe)], cwd=directory, check=True)


if __name__ == "__main__":
    main()
