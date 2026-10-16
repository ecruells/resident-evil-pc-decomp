#include "ZombieModeInternal.h"
#include "ZombieNet.h"
#include "ZombieReconnect.h"
#include "../../Globals.h"
#include "../../DebugPrint.h"
#include "../../platform/platform.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ============================================================================
// ZombieDrops.cpp - the survivors' dropped items (port-added).
//
// A survivor drops the item under the cursor with the inventory's fourth
// command, DROP (MainMenu.cpp, zombie_mode_drop_item). It lies where the
// survivor stood, spaced from other pickups, until somebody picks it up:
//   - every copy keeps the same list of drops (ZM_EV_DROP, by a uid the
//     dropping copy mints: player << 12 | sequence);
//   - in the loaded room each drop is a pickup of our own, as the randomizer's
//     new spots are: an item_model_set record run through cmd_item_model_set,
//     on room action slot ZM_DROP_SLOT_FIRST + k and an item model record of
//     ours, its look the item's view model (zm_random_look). Placed at room
//     load, or - dropped while this copy is in the room - once no menu is
//     open (the look loads textures);
//   - each placed drop k carries roomItems flag s_pool[k], one the mansion
//     and this scenario leave free. These flags are this copy's own: left out
//     of the flag merge (zm_drops_flag_mask), and re-set whenever a drop is
//     placed on them. The host's PICKUP confirmation consumes the stable uid
//     on every copy; a survivor's local flag cannot consume it remotely.
// Multiplayer pickup awards first commit an exclusive host reservation.
// ============================================================================

#define ZM_DROPS_MAX 256
#define ZM_DROP_SPACING 1100      // 1000-square pickup areas, with a 100-unit gap
#define ZM_DROP_SEARCH_RINGS 4
#define ZM_DROP_BODY_RADIUS 260

extern int  cmd_item_model_set(void);                                    // 0x00461220 CmdFunctions.cpp
extern void Flg_on(int baseAddr, unsigned int bitIndex);                 // 0x00473ef0
extern void FUN_00473f10(int* baseAddr, unsigned int bitIndex);          // 0x00473f10 - Flg_off
extern void MovePlayerXZ(int angle, SVECTOR* offset, SVECTOR* out);      // WeaponDamage.cpp

struct ZmDrop {
    bool           live;
    unsigned short uid;
    unsigned char  stage, room, id, qty;
    short          x, y, z, angle;
};
static ZmDrop         s_drops[ZM_DROPS_MAX];
static unsigned short s_dropSeq = 0;

unsigned int zm_drops_progression(const bool* reachable)
{
    unsigned int owned = 0;
    for (int i = 0; i < ZM_DROPS_MAX; i++) {
        const ZmDrop& d = s_drops[i];
        if (!d.live || !d.qty || d.stage < 5 || d.stage > 6 || d.room >= 0x1D) continue;
        if (reachable[(d.stage - 5) * 0x1D + d.room]) owned |= zm_random_progression_bit(d.id);
    }
    return owned;
}

unsigned short zm_drops_reconnect_sequence(void) { return s_dropSeq; }
int zm_drops_reconnect_export(void* out, int capacity)
{
    if (capacity < (int)sizeof(s_drops)) return 0;
    memcpy(out, s_drops, sizeof(s_drops));
    return sizeof(s_drops);
}
bool zm_drops_reconnect_import(const void* data, int size, unsigned short sequence)
{
    if (size != sizeof(s_drops)) return false;
    memcpy(s_drops, data, sizeof(s_drops));
    s_dropSeq = sequence;
    return true;
}

void zm_drops_reconnect_reconcile(int player, ZmReconnectPlayer* p)
{
    unsigned short next = p->dropSequence;
    for (int di = 0; di < ZM_DROPS_MAX; di++) {
        const ZmDrop& d = s_drops[di];
        if (!d.id || (d.uid >> 12) != player) continue;
        int delta = ((d.uid & 0xFFF) - (p->dropSequence & 0xFFF)) & 0xFFF;
        if (delta >= 0x800) continue; // belongs to an older checkpoint
        unsigned short after = (unsigned short)(p->dropSequence + delta + 1);
        if ((short)(after - next) > 0) next = after;
        bool stack = (d.id > ITEM_ROCKET_LAUNCHER && d.id < ITEM_EMPTY_BOTTLE) || d.id == ITEM_INK_RIBBONS;
        int left = stack ? d.qty : 1;
        for (int i = 0; i < p->card.totalHeldItems && left > 0; i++) {
            if (p->inventory[i * 2] != d.id) continue;
            if (stack) {
                int take = p->inventory[i * 2 + 1] < left ? p->inventory[i * 2 + 1] : left;
                p->inventory[i * 2 + 1] -= (unsigned char)take; left -= take;
                if (p->inventory[i * 2 + 1]) continue;
            } else left = 0;
            p->inventoryMask &= ~(1u << p->indices[i]);
            if (p->card.equippedItemId == i + 1) p->card.equippedItemId = 0;
            else if (p->card.equippedItemId > i + 1) p->card.equippedItemId--;
            int n = --p->card.totalHeldItems;
            memmove(p->inventory + i * 2, p->inventory + (i + 1) * 2, (n - i) * 2);
            memmove(p->indices + i, p->indices + i + 1, n - i);
            p->inventory[n * 2] = p->inventory[n * 2 + 1] = 0;
            i--;
        }
    }
    p->dropSequence = next;
}

void zm_drops_reconnect_abandon(const ZmReconnectPlayer* p)
{
    // The old process is no longer admitted after the deadline. Its carried
    // supplies remain recoverable by the team, including unique keys/crests.
    for (int i = 0; i < p->card.totalHeldItems; i++) {
        unsigned char id = p->inventory[i * 2], qty = p->inventory[i * 2 + 1];
        if (!id || !zm_random_has_look(id)) continue;
        for (int di = 0; di < ZM_DROPS_MAX; di++) {
            ZmDrop& d = s_drops[di];
            if (d.live) continue;
            memset(&d, 0, sizeof(d));
            d.live = true; d.uid = s_dropSeq++ & 0xFFF; // host's unused uid range
            d.stage = p->card.stageId; d.room = p->card.roomId; d.id = id; d.qty = qty;
            d.x = (short)p->x; d.y = (short)p->y; d.z = (short)p->z; d.angle = p->angle;
            zm_net_send_event8(ZM_EV_DROP, (short)d.uid, (short)(id | (qty << 8)), d.x, d.y, d.z,
                d.angle, (short)(d.stage | (d.room << 8)), 0);
            break;
        }
    }
}

// The loaded room's drop positions: k is room action slot ZM_DROP_SLOT_FIRST + k
// and roomItems flag s_pool[k]; its item model index is taken on first use.
struct ZmDropHere {
    bool          alloc;     // has its item model index
    bool          used;      // a drop lies on it
    int           drop;      // s_drops index
    unsigned char model;
};
static ZmDropHere    s_here[ZM_DROPS_PER_ROOM];
static unsigned char s_ops[ZM_DROPS_PER_ROOM][0x20];
#define ZM_DROP_SPARKLES 12
static unsigned char s_recs[ZM_DROPS_PER_ROOM][0xA4];
static bool          s_roomReady = false;

static unsigned char s_pool[ZM_DROPS_PER_ROOM];
static int           s_poolCount = 0;
static unsigned char s_poolMask[32];

// A survivor's death on its own copy: the inventory goes on the floor once.
#define ZM_DEATH_DROP_RADIUS 600
static bool s_deathDropped = false;
static void zm_drops_on_death(void);

static bool zm_drops_on(void)
{
    return zombie_mode_armed() && zm_game_role() != ZM_NET_OFF;
}

void zm_drops_new_game(void)
{
    memset(s_drops, 0, sizeof(s_drops));
    s_dropSeq = 0;
    s_deathDropped = false;
    memset(s_here, 0, sizeof(s_here));
    s_roomReady = false;
    s_poolCount = zm_random_free_flags(s_pool, ZM_DROPS_PER_ROOM);
    memset(s_poolMask, 0, sizeof(s_poolMask));
    // Flg_on's own bit order (MSB first within each dword) builds the mask.
    for (int k = 0; k < s_poolCount; k++) Flg_on((int)s_poolMask, s_pool[k]);
    dbg_printf("[drop] %d flags for drops, first %d\n", s_poolCount, s_poolCount > 0 ? (int)s_pool[0] : -1);
}

unsigned char zm_drops_flag_mask(int byteIndex)
{
    return (byteIndex >= 0 && byteIndex < 32) ? s_poolMask[byteIndex] : 0;
}

static int zm_drop_find(unsigned short uid)
{
    for (int i = 0; i < ZM_DROPS_MAX; i++) {
        if (s_drops[i].live && s_drops[i].uid == uid) return i;
    }
    return -1;
}

static int zm_drop_here_of(int drop)
{
    for (int k = 0; k < ZM_DROPS_PER_ROOM; k++) {
        if (s_here[k].used && s_here[k].drop == drop) return k;
    }
    return -1;
}

// Into the loaded room. False: no position, model or flag left (it stays on
// the list, and shows when the room is next loaded).
static bool zm_drop_place(int di)
{
    const ZmDrop& d = s_drops[di];
    int k = -1;
    for (int i = 0; i < s_poolCount && k < 0; i++) if (s_here[i].alloc && !s_here[i].used) k = i;
    for (int i = 0; i < s_poolCount && k < 0; i++) if (!s_here[i].alloc) k = i;
    if (k < 0 || g_RdtPointer == NULL) return false;
    ZmDropHere& h = s_here[k];
    if (!h.alloc) {
        int idx = g_RdtPointer->item_count;
        if (idx >= ROOM_ITEM_MODELS) return false;
        h.model = (unsigned char)idx;
        h.alloc = true;
        g_RdtPointer->item_count = (unsigned char)(idx + 1);
    }
    unsigned char* rec = s_recs[k];
    memset(rec, 0, sizeof(s_recs[k]));
    g_item_model_table[h.model] = rec;
    Flg_on((int)g_roomItemsFlags, s_pool[k]);

    unsigned char* op = s_ops[k];
    memset(op, 0, sizeof(s_ops[k]));
    op[0x00] = 0x18;
    op[0x01] = (unsigned char)(ZM_DROP_SLOT_FIRST + k);
    *(unsigned short*)(op + 0x02) = (unsigned short)(d.x - 500);    // pickup zone, 1000 square
    *(unsigned short*)(op + 0x04) = (unsigned short)(d.z - 500);
    *(unsigned short*)(op + 0x06) = 1000;
    *(unsigned short*)(op + 0x08) = 1000;
    op[0x0A] = d.id;
    op[0x0B] = d.qty;
    op[0x0C] = h.model;
    op[0x0D] = 0xFF;                                     // no parent: room coordinates
    *(short*)(op + 0x0E) = d.x;
    *(short*)(op + 0x10) = d.y;
    *(short*)(op + 0x12) = d.z;
    *(unsigned short*)(op + 0x14) = (unsigned short)d.angle;   // +0x72 rotation, y
    op[0x16] = s_pool[k];
    op[0x17] = 0x81;                                     // armed, action press
    // As the rooms' floor items (bit 0), plus the key items' sparkle so a drop
    // is easy to spot: bit 0x8000 has cmd_item_model_set attach the sparkle
    // billboard, 0x0700 picks its kind - the sword key's in ROOM1000 (0x8700),
    // drawn at the item's own height (bits 0xF0 clear). The pickup frees it
    // (room_event_item_pickup, zm_drop_unplace: the record's +0x86). Each
    // sparkle holds one of the 64 effect slots for as long as it lies there,
    // so only the first ZM_DROP_SPARKLES of a room's drops get one - the rest
    // would take the slots blood and muzzle flashes need.
    *(unsigned short*)(op + 0x18) = (k < ZM_DROP_SPARKLES) ? 0x8701 : 1;

    h.used = true;
    h.drop = di;
    unsigned char* saved = g_ScdOpcodes;
    g_ScdOpcodes = op;
    cmd_item_model_set();
    g_ScdOpcodes = saved;
    dbg_printf("[drop] placed %04X: item %02X x%d at (%d,%d,%d), slot %d model %d flag %d\n",
               (unsigned)d.uid, (unsigned)d.id, (int)d.qty, (int)d.x, (int)d.y, (int)d.z,
               (int)op[0x01], (int)h.model, (int)s_pool[k]);
    return true;
}

// Out of the loaded room: taken on another copy. As room_event_item_pickup
// leaves a picked item: model hidden, its sparkle freed, the entry inert.
static void zm_drop_unplace(int k)
{
    ZmDropHere& h = s_here[k];
    if (!h.used) return;
    h.used = false;
    unsigned char* rec = s_recs[k];
    rec[0] = 0;
    unsigned short fxSlot = *(unsigned short*)(rec + 0x86);
    if (fxSlot != 0) {
        g_freeEffectSlots++;
        memset_((unsigned int*)&g_effectPool[fxSlot], 0x21);
        *(unsigned short*)(rec + 0x86) = 0;
    }
    g_RoomActionTable[(ZM_DROP_SLOT_FIRST + k) * 12] = 0;
    FUN_00473f10((int*)&g_roomItemsFlags, s_pool[k]);
}

void zm_drops_room_reset(void)
{
    memset(s_here, 0, sizeof(s_here));
    s_roomReady = false;
}

// zombie_mode_room_spawn, after the room's init script and new spots.
void zm_drops_room_loaded(void)
{
    if (!zm_drops_on()) return;
    s_roomReady = true;
    for (int i = 0; i < ZM_DROPS_MAX; i++) {
        const ZmDrop& d = s_drops[i];
        if (d.live && d.stage == g_stageId && d.room == g_roomId) zm_drop_place(i);
    }
}

const int* zm_drop_look(const unsigned char* op)
{
    for (int k = 0; k < ZM_DROPS_PER_ROOM; k++) {
        if (s_here[k].used && op == s_ops[k]) return zm_random_look(s_drops[s_here[k].drop].id);
    }
    return NULL;
}

bool zm_drop_is_pickup(const unsigned char* record)
{
    if (!zm_drops_on() || record == NULL) return false;
    for (int k = 0; k < ZM_DROPS_PER_ROOM; k++) {
        // The pickup deactivates its room action before awarding the item.
        // Keep identifying the record until the room's allocations reset.
        if (s_here[k].alloc && record == s_ops[k] + 2) return true;
    }
    return false;
}

bool zm_drop_pickup_uid(const unsigned char* record, unsigned short* uid)
{
    for (int k = 0; k < ZM_DROPS_PER_ROOM; k++) {
        if (s_here[k].used && record == s_ops[k] + 2 && s_drops[s_here[k].drop].live) {
            *uid = s_drops[s_here[k].drop].uid;
            return true;
        }
    }
    return false;
}

bool zm_drop_available(unsigned short uid, unsigned short room, unsigned short item)
{
    int di = zm_drop_find(uid);
    if (di < 0) return false;
    const ZmDrop& d = s_drops[di];
    return room == (unsigned short)(d.stage | (d.room << 8)) &&
           item == (unsigned short)(d.id | (d.qty << 8));
}

void zombie_mode_pickup_finished(void)
{
    zm_pickups_reconnect_awarded();
    // A death during the host round trip must put newly awarded loot on the
    // corpse, even if its first inventory drop has already happened.
    if (zm_drops_on() && zm_game_role() == ZM_NET_SURVIVOR && g_playerEntity.health < 0) {
        s_deathDropped = false;
        zm_drops_on_death();
    }
}

// zombie_mode_net_frame.
void zm_drops_frame(void)
{
    if (!zm_drops_on() || !s_roomReady) return;
    if (!zombie_mode_pickup_waiting()) zm_drops_on_death();
    // Picked up here: the flag cleared (all of it) or the quantity lowered
    // (the inventory took part of it).
    for (int k = 0; k < ZM_DROPS_PER_ROOM; k++) {
        ZmDropHere& h = s_here[k];
        if (!h.used) continue;
        ZmDrop& d = s_drops[h.drop];
        if (Flg_ck((int)g_roomItemsFlags, s_pool[k]) == 0) {
            h.used = false;
            d.live = false;
            dbg_printf("[drop] took %04X (item %02X)\n", (unsigned)d.uid, (unsigned)d.id);
        } else if (s_ops[k][0x0B] != d.qty) {
            d.qty = s_ops[k][0x0B];
            dbg_printf("[drop] took part of %04X: %d left\n", (unsigned)d.uid, (int)d.qty);
        }
    }
    // Drops made in this room since it loaded, once no menu is up.
    if ((g_main_state_flags & MSF_MENU_ACTIVE) != 0 || zombie_mode_pickup_waiting()) return;
    for (int i = 0; i < ZM_DROPS_MAX; i++) {
        const ZmDrop& d = s_drops[i];
        if (!d.live || d.stage != g_stageId || d.room != g_roomId || zm_drop_here_of(i) >= 0) continue;
        if (!zm_drop_place(i)) break;
    }
}

// ZM_EV_DROP / ZM_EV_DROP_TAKE from another copy.
void zm_drops_take_event(int kind, const short* a, int src)
{
    if (!zm_drops_on()) return;
    unsigned short uid = (unsigned short)a[0];
    if (kind == ZM_EV_DROP) {
        if (zm_drop_find(uid) >= 0) return;
        for (int i = 0; i < ZM_DROPS_MAX; i++) {
            ZmDrop& d = s_drops[i];
            if (d.live) continue;
            d.live = true;
            d.uid = uid;
            d.id = (unsigned char)(a[1] & 0xFF);
            d.qty = (unsigned char)((unsigned short)a[1] >> 8);
            d.x = a[2];
            d.y = a[3];
            d.z = a[4];
            d.angle = a[5];
            d.stage = (unsigned char)(a[6] & 0xFF);
            d.room = (unsigned char)((unsigned short)a[6] >> 8);
            dbg_printf("[drop] player %d dropped %04X: item %02X x%d in stage %d room %02X\n", src,
                       (unsigned)uid, (unsigned)d.id, (int)d.qty, (int)d.stage + 1, (unsigned)d.room);
            return;
        }
        return;
    }
    int di = zm_drop_find(uid);
    if (di < 0) return;
    ZmDrop& d = s_drops[di];
    int k = zm_drop_here_of(di);
    if (a[1] == 0) {
        if (k >= 0) zm_drop_unplace(k);
        d.live = false;
        dbg_printf("[drop] %04X taken by player %d\n", (unsigned)uid, src);
    } else {
        d.qty = (unsigned char)a[1];
        if (k >= 0) s_ops[k][0x0B] = d.qty;
    }
}

// ---------------------------------------------------------------------------
// The inventory's DROP (MainMenu.cpp)
// ---------------------------------------------------------------------------

bool zombie_mode_can_drop(void)
{
    return zm_drops_on() && zm_game_role() == ZM_NET_SURVIVOR;
}

// Check the pickup areas, including drops not yet instantiated because the
// inventory is open. Do not square world-coordinate differences (32-bit).
static bool zm_drop_items_clear(int x, int z)
{
    for (int i = 0; i < ZM_DROPS_MAX; i++) {
        const ZmDrop& d = s_drops[i];
        if (!d.live || d.stage != g_stageId || d.room != g_roomId) continue;
        if (abs(x - d.x) < ZM_DROP_SPACING && abs(z - d.z) < ZM_DROP_SPACING) return false;
    }
    for (int i = 0; i < ZM_DROP_SLOT_FIRST; i++) {
        const unsigned char* entry = &g_RoomActionTable[i * 12];
        if (entry[0] != 4 && entry[0] != 0x0D && entry[0] != 0x0F) continue;
        // Floor items only; a shelf pickup is not a ground item.
        if ((*(const unsigned short*)(entry + 2) & 1) == 0) continue;
        if (!Flg_ck((int)g_roomItemsFlags, *(const unsigned short*)(entry + 6))) continue;
        const unsigned char* rec = *(unsigned char* const*)(entry + 8);
        if (rec == NULL) continue;
        int left = *(const short*)(rec + 0);
        int top = *(const short*)(rec + 2);
        int right = left + *(const unsigned short*)(rec + 4);
        int bottom = top + *(const unsigned short*)(rec + 6);
        // Our half-width (500), plus the gap (100), expands the other zone.
        if (x > left - 600 && x < right + 600 &&
            z > top - 600 && z < bottom + 600) return false;
    }
    return true;
}

// Probe body clearance along the whole route from the survivor, so a clear
// destination across a thin wall cannot receive a drop. The collision query
// uses an SVECTOR offset and writes the engine's position scratch.
static bool zm_drop_route_clear(int x, int z)
{
    int bx = g_playerEntity.scaMatrixData.localMatrix.t[0];
    int bz = g_playerEntity.scaMatrixData.localMatrix.t[2];
    int dx = x - bx, dz = z - bz;
    int distance = abs(dx) > abs(dz) ? abs(dx) : abs(dz);
    int steps = distance / 100 + 1;
    static const short probes[5][2] = {
        {0, 0}, {ZM_DROP_BODY_RADIUS, 0}, {-ZM_DROP_BODY_RADIUS, 0},
        {0, ZM_DROP_BODY_RADIUS}, {0, -ZM_DROP_BODY_RADIUS}
    };
    for (int step = 1; step <= steps; step++) {
        VECTOR pos = { bx + dx * step / steps, 0, bz + dz * step / steps, 0 };
        for (int p = 0; p < 5; p++) {
            SVECTOR off = { probes[p][0], 0, probes[p][1], 0 };
            if (room_collision_check_0047da50(&pos, (VECTOR*)&off) == 1) return false;
        }
    }
    return true;
}

static bool zm_drop_find_spot(int* x, int* z)
{
    VECTOR savedScratch = g_playerPosScratch;
    int bx = *x, bz = *z;
    for (int ring = 0; ring <= ZM_DROP_SEARCH_RINGS; ring++) {
        for (int iz = -ring; iz <= ring; iz++) {
            for (int ix = -ring; ix <= ring; ix++) {
                if (abs(ix) != ring && abs(iz) != ring) continue;
                int nx = bx + ix * ZM_DROP_SPACING;
                int nz = bz + iz * ZM_DROP_SPACING;
                if (nx < -32768 || nx > 32767 || nz < -32768 || nz > 32767) continue;
                if (!zm_drop_items_clear(nx, nz) || !zm_drop_route_clear(nx, nz)) continue;
                *x = nx;
                *z = nz;
                g_playerPosScratch = savedScratch;
                return true;
            }
        }
    }
    g_playerPosScratch = savedScratch;
    dbg_printf("[drop] no spaced floor position near (%d,%d)\n", bx, bz);
    return false;
}

// A new drop of this copy's at (x, y, z) in the loaded room, told to the
// others. False: no space, the room/list is full, or the item has no look.
static bool zm_drop_add(unsigned char id, unsigned char qty, int x, int y, int z)
{
    if (!zm_random_has_look(id)) return false;
    int inRoom = 0, freeIdx = -1;
    for (int i = 0; i < ZM_DROPS_MAX; i++) {
        const ZmDrop& d = s_drops[i];
        if (!d.live) { if (freeIdx < 0) freeIdx = i; continue; }
        if (d.stage == g_stageId && d.room == g_roomId) inRoom++;
    }
    if (freeIdx < 0 || inRoom >= s_poolCount) return false;
    if (!zm_drop_find_spot(&x, &z)) return false;
    ZmDrop& d = s_drops[freeIdx];
    d.live = true;
    d.uid = (unsigned short)((zm_net_self() << 12) | (s_dropSeq++ & 0x0FFF));
    d.stage = g_stageId;
    d.room = g_roomId;
    d.id = id;
    d.qty = qty;
    d.x = (short)x;
    d.y = (short)y;
    d.z = (short)z;
    d.angle = (short)(rand() & 0xFFF);
    zm_net_send_event8(ZM_EV_DROP, (short)d.uid, (short)(id | (qty << 8)), d.x, d.y, d.z, d.angle,
                       (short)(g_stageId | (g_roomId << 8)), 0);
    dbg_printf("[drop] dropped %04X: item %02X x%d at (%d,%d,%d)\n", (unsigned)d.uid, (unsigned)id,
               (int)qty, (int)d.x, (int)d.y, (int)d.z);
    return true;
}

bool zombie_mode_drop_item(unsigned char id, unsigned char qty)
{
    if (!zombie_mode_can_drop()) return false;
    return zm_drop_add(id, qty, g_playerEntity.scaMatrixData.localMatrix.t[0],
                       g_playerEntity.scaMatrixData.localMatrix.t[1],
                       g_playerEntity.scaMatrixData.localMatrix.t[2]);
}

// zm_drops_frame, a survivor's copy: the first frame its survivor is dead,
// everything it carried goes on the floor round the body, searching for
// clear, spaced positions for the others to pick up. The
// death sequence runs for seconds after, so the events are out before the
// copy leaves the game.
static void zm_drops_on_death(void)
{
    if (zm_game_role() == ZM_NET_SURVIVOR && zm_shotgun_crushed(zm_net_self())) {
        // Also erase late committed pickup/box awards; never scatter crush loot.
        zm_shotgun_clear_inventory(); s_deathDropped = true; return;
    }
    if (zombie_mode_box_waiting()) return; // settle the inventory exchange first
    if (g_playerEntity.health >= 0) { s_deathDropped = false; return; }
    if (s_deathDropped || zm_game_role() != ZM_NET_SURVIVOR || g_playerEntity.health >= 0) return;
    s_deathDropped = true;
    unsigned char* slots = (unsigned char*)g_ItemSlotsPointer;
    int n = 0;
    for (int i = 0; i < g_TotalHeldItems; i++) if (slots[i * 2] != 0) n++;
    if (n == 0) return;
    int bx = g_playerEntity.scaMatrixData.localMatrix.t[0];
    int by = g_playerEntity.scaMatrixData.localMatrix.t[1];
    int bz = g_playerEntity.scaMatrixData.localMatrix.t[2];
    int k = 0, dropped = 0;
    for (int i = 0; i < g_TotalHeldItems; i++) {
        unsigned char id = slots[i * 2];
        if (id == 0) continue;
        SVECTOR off = { (short)ZM_DEATH_DROP_RADIUS, 0, 0, 0 };
        MovePlayerXZ((k++ * 0x1000) / n, &off, &off);
        int x = bx + off.x, z = bz + off.z;
        if (zm_drop_add(id, slots[i * 2 + 1], x, by, z)) {
            dropped++;
            // A revived player must not retain a second copy of its floor drops.
            if (g_EquippedItemId == i + 1) g_EquippedItemId = 0;
            slots[i * 2] = slots[i * 2 + 1] = 0;
        }
    }
    if (dropped != 0) rearrange_item_slots();
    dbg_printf("[drop] died: %d of %d items dropped round the body\n", dropped, n);
}

// The DROP row's word, over the blank button MainMenu.cpp draws.
void zombie_mode_drop_label(short x, short y)
{
    snprintf(PRINT_TEXT_BUFFER, sizeof(PRINT_TEXT_BUFFER), "DROP");
    zm_text_encode(PRINT_TEXT_BUFFER);
    PrintText8x14(x, y, 0x83, 0);
}
