#include "ZombieModeInternal.h"
#include "ZombieNet.h"
#include "ZombieReconnect.h"
#include "../../Globals.h"
#include "../../DebugPrint.h"
#include "../../platform/platform.h"
#include "../dc/Items.h"
#include <cstring>

extern void Flg_on(int baseAddr, unsigned int bitIndex);

// A pickup reserves its stable identity, then commits on the host before the
// original award runs. Reliable replies bypass the ordinary effects inbox.
// Yielding the pickup task keeps its record alive through both round trips;
// input and spectator room changes are held until the transaction completes.
enum { PK_REQUEST, PK_GRANT, PK_DENY, PK_CANCEL, PK_COMMIT, PK_TAKEN, PK_DONE };
struct PickupClaim {
    bool seen, active, done;
    unsigned int atMs;
    short args[8];
};
static PickupClaim s_claims[ZM_NET_MAX_PLAYERS];
static bool s_waiting = false;
static unsigned short s_sequence = 0;
static short s_request[8];
static int s_reply = -1;
static int s_expected = PK_GRANT;
static unsigned short s_awardedToken;

unsigned short zm_pickups_reconnect_token(void) { return s_awardedToken; }
void zm_pickups_reconnect_awarded(void) { s_awardedToken = s_sequence; }
void zm_pickups_reconnect_restore(unsigned short token) { s_sequence = s_awardedToken = token; }

void zm_pickups_reconnect_reconcile(int player, ZmReconnectPlayer* p)
{
    PickupClaim& c = s_claims[player];
    if (!c.done || (short)((unsigned short)c.args[1] - p->pickupToken) <= 0) return;
    unsigned char id = (unsigned char)c.args[5];
    int quantity = (unsigned short)c.args[5] >> 8;
    if (id == ITEM_INK_RIBBONS) quantity = 3;
    quantity = dc_item_pickup_quantity(id, (unsigned char)quantity);
    if (!c.args[2] && zm_net_char(player) == ZM_CHAR_CHRIS && id > ITEM_ROCKET_LAUNCHER && id < ITEM_EMPTY_BOTTLE)
        quantity = quantity + quantity / 2 > 250 ? 250 : quantity + quantity / 2;
    bool stack = (id > ITEM_ROCKET_LAUNCHER && id < ITEM_EMPTY_BOTTLE) || id == ITEM_INK_RIBBONS;
    if (stack) for (int i = 0; i < p->card.totalHeldItems && quantity > 0; i++) {
        if (p->inventory[i * 2] != id) continue;
        int add = 250 - p->inventory[i * 2 + 1];
        if (add > quantity) add = quantity;
        p->inventory[i * 2 + 1] += (unsigned char)add;
        quantity -= add;
    }
    if (quantity > 0 && p->card.totalHeldItems < (zm_net_char(player) == ZM_CHAR_JILL ? 8 : 6)) {
        int at = p->card.totalHeldItems++;
        p->inventory[at * 2] = id; p->inventory[at * 2 + 1] = (unsigned char)quantity;
        int bit = 0; while (p->inventoryMask & (1u << bit)) bit++;
        p->indices[at] = (unsigned char)bit; p->inventoryMask |= 1u << bit;
    }
    p->pickupToken = (unsigned short)c.args[1];
}

void zm_pickups_reset(void)
{
    memset(s_claims, 0, sizeof(s_claims));
    s_waiting = false;
    s_sequence = 0;
    s_awardedToken = 0;
    s_reply = -1;
}

bool zombie_mode_pickup_waiting(void) { return s_waiting || zombie_mode_box_waiting(); }

static void pickup_send(int dst, int op, const short* a)
{
    zm_net_send_event_to(dst, ZM_EV_PICKUP, (short)op, a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
}

static bool pickup_same(const short* a, const short* b)
{
    return memcmp(a + 1, b + 1, 7 * sizeof(short)) == 0;
}

void zm_pickups_frame(void)
{
    if (zm_net_role() != ZM_NET_ZOMBIE) return;
    for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
        PickupClaim& c = s_claims[i];
        if (!c.active || (zm_seat_state(i) != NULL && plat_time_ms() - c.atMs < 10000u)) continue;
        c.active = false;
        pickup_send(i, PK_DENY, c.args);
    }
}

static bool pickup_available(const short* a)
{
    if (a[2] == 1) return zm_drop_available((unsigned short)a[3], (unsigned short)a[4], (unsigned short)a[5]);
    if (a[2] != 0 || !zm_shotgun_pickup_valid(a)) return false;
    unsigned int flag = (unsigned short)a[3] & 255;
    unsigned char mask[32] = {};
    Flg_on((int)mask, flag);
    for (int i = 0; i < 32; i++) if ((mask[i] & zm_drops_flag_mask(i)) != 0) return false;
    return Flg_ck((int)g_roomItemsFlags, flag) != 0;
}

static void pickup_hide_world(int flag)
{
    FUN_00473f10((int*)g_roomItemsFlags, (unsigned int)flag);
    for (int i = 0; i < ROOM_ACTION_ENTRIES; i++) {
        unsigned char* evt = g_RoomActionTable + i * 12;
        if (evt[0] != 4) continue; // maps/documents have personal collection paths
        unsigned char* rec = *(unsigned char**)(evt + 8);
        if (rec == NULL || rec[0x14] != flag || zm_drop_is_pickup(rec)) continue;
        evt[0] = 0;
        if (rec[10] >= ROOM_ITEM_MODELS || g_item_model_table[rec[10]] == NULL) continue;
        unsigned char* model = (unsigned char*)g_item_model_table[rec[10]];
        model[0] = 0;
        unsigned short* fx = (unsigned short*)(model + 0x86);
        if (*fx != 0) {
            g_freeEffectSlots++;
            memset_((unsigned int*)&g_effectPool[*fx], 0x21);
            *fx = 0;
        }
    }
}

static void pickup_taken(const short* a)
{
    if (a[2] == 0) {
        if (!zm_shotgun_pickup_valid(a)) return;
        pickup_hide_world((unsigned short)a[3] & 255);
        zm_shotgun_pickup(a);
    }
    else {
        short drop[8] = { a[3], 0 };
        zm_drops_take_event(ZM_EV_DROP_TAKE, drop, ZM_NET_DIRECTOR);
    }
}

void zm_pickups_take(const short* a, int src)
{
    unsigned int seed = (unsigned short)a[6] | ((unsigned int)(unsigned short)a[7] << 16);
    if (!zombie_mode_armed() || seed != zm_net_seed()) return;
    if (a[2] != 0 && a[2] != 1) return;
    // The host's replies; on the host of an AI director's game they also come
    // back to its own survivor's requests (src is the host either way).
    bool reply = a[0] == PK_GRANT || a[0] == PK_DENY || a[0] == PK_TAKEN || a[0] == PK_DONE;
    if (reply) {
        if (zm_game_role() != ZM_NET_SURVIVOR || src != ZM_NET_DIRECTOR) return;
        if (a[0] == PK_TAKEN) pickup_taken(a);
        else if (s_waiting && s_reply != PK_DONE && pickup_same(a, s_request) &&
                 (a[0] == s_expected || a[0] == PK_DENY)) s_reply = a[0];
        return;
    }
    if (zm_net_role() != ZM_NET_ZOMBIE || src < 0 || src >= ZM_NET_MAX_PLAYERS || zm_net_char(src) < 0) return;
    zm_pickups_frame();
    PickupClaim& c = s_claims[src];
    unsigned short token = (unsigned short)a[1];
    if (c.seen && token != (unsigned short)c.args[1] &&
        (short)(token - (unsigned short)c.args[1]) <= 0) return;
    if (a[0] == PK_CANCEL) {
        // Remember cancellation even if it arrived ahead of the request.
        if (!c.seen || token != (unsigned short)c.args[1]) {
            memset(&c, 0, sizeof(c));
            c.seen = true;
            memcpy(c.args, a, sizeof(c.args));
        }
        if (pickup_same(a, c.args) && !c.done) c.active = false;
        return;
    }
    if (a[0] == PK_COMMIT) {
        if (!c.seen || !pickup_same(a, c.args)) { pickup_send(src, PK_DENY, a); return; }
        if (c.done) {
            pickup_send(ZM_NET_ALL, PK_TAKEN, a);
            pickup_send(src, PK_DONE, a);
            return;
        }
        if (!c.active || zombie_mode_match_over() || !pickup_available(a)) {
            c.active = false;
            pickup_send(src, PK_DENY, a);
            return;
        }
        c.active = false;
        c.done = true;
        pickup_taken(a);
        pickup_send(ZM_NET_ALL, PK_TAKEN, a);
        pickup_send(src, PK_DONE, a);
        dbg_printf("[pickup] player %d committed %s %04X token %u\n", src,
                   a[2] ? "drop" : "world", (unsigned short)a[3], token);
        return;
    }
    if (a[0] != PK_REQUEST) return;
    if (c.seen && token == (unsigned short)c.args[1]) {
        int reply = PK_DENY;
        if (pickup_same(a, c.args)) reply = c.done ? PK_DONE : c.active ? PK_GRANT : PK_DENY;
        pickup_send(src, reply, a);
        return;
    }
    memset(&c, 0, sizeof(c));
    c.seen = true;
    memcpy(c.args, a, sizeof(c.args));
    const ZmNetPeerState* peer = zm_seat_state(src);
    bool available = !zombie_mode_match_over() && peer != NULL && !peer->dead && pickup_available(a);
    for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
        PickupClaim& other = s_claims[i];
        if (other.active && zm_seat_state(i) == NULL) other.active = false;
        if (i != src && other.active && other.args[2] == a[2] && other.args[3] == a[3]) available = false;
    }
    c.active = available;
    c.atMs = plat_time_ms();
    pickup_send(src, available ? PK_GRANT : PK_DENY, a);
}

static bool pickup_fits(const unsigned char* rec)
{
    int slots = zombie_mode_inventory_slots((4 - ((g_playerEntity.id & 3) != 1)) * 2);
    if (g_TotalHeldItems < slots) return true;
    unsigned char id = rec[8];
    if (!((ITEM_ROCKET_LAUNCHER < id && id < ITEM_EMPTY_BOTTLE) || id == ITEM_INK_RIBBONS)) return false;
    unsigned char qty = dc_item_pickup_quantity(id, id == ITEM_INK_RIBBONS ? 3 : rec[9]);
    qty = zombie_mode_pickup_quantity(id, qty, rec);
    const unsigned char* held = (const unsigned char*)g_ItemSlotsPointer;
    for (int i = 0; i < g_TotalHeldItems; i++)
        if (held[i * 2] == id && (int)held[i * 2 + 1] + qty <= 250) return true;
    return false;
}

bool zombie_mode_pickup_claim(unsigned char* evt, const unsigned char* rec)
{
    if (!zombie_mode_armed() || zm_game_role() != ZM_NET_SURVIVOR) return true;
    if (s_waiting || evt == NULL || rec == NULL || g_playerEntity.health < 0 ||
        zombie_mode_match_over() || !zm_net_active()) return false;
    unsigned short uid = 0;
    bool drop = zm_drop_pickup_uid(rec, &uid);
    if ((zm_drop_is_pickup(rec) && !drop) ||
        (!drop && Flg_ck((int)g_roomItemsFlags, rec[0x14]) == 0)) {
        zm_reinforce_notice("SOMEONE ELSE TOOK THAT ITEM");
        return false;
    }
    if (!pickup_fits(rec)) return false;
    short request[8] = { PK_REQUEST, (short)++s_sequence, (short)drop,
        (short)(drop ? uid : zm_shotgun_pickup_identity(rec)), (short)(g_stageId | (g_roomId << 8)),
        (short)(rec[8] | (rec[9] << 8)), (short)zm_net_seed(), (short)(zm_net_seed() >> 16) };
    memcpy(s_request, request, sizeof(request));
    s_waiting = true;
    s_reply = -1;
    s_expected = PK_GRANT;
    pickup_send(ZM_NET_DIRECTOR, PK_REQUEST, request);
    unsigned int start = plat_time_ms();
    unsigned int sentAt = start;
    bool interrupted = false;
    while (s_reply < 0 && !interrupted && !zombie_mode_match_over() && g_playerEntity.health >= 0 &&
           zm_net_active() && plat_time_ms() - start < 5000u) {
        // Keep the same live-world simulation as the pickup viewer. Network
        // latency must not give the survivor a pause/invulnerability window.
        interrupted = zombie_mode_menu_frame() || interrupted;
        if (s_reply < 0 && plat_time_ms() - sentAt >= 500u) {
            pickup_send(ZM_NET_DIRECTOR, PK_REQUEST, request);
            sentAt = plat_time_ms();
        }
        if (s_reply < 0) Task_sleep(1);
    }
    // Still here: its flag, which a pickup anywhere clears (pickup_hide_world,
    // zm_drop_unplace) - not the entry's handler, which a room's script may
    // switch off meanwhile. ROOM7150's frame script disarms the red jewel's
    // slot 5 whenever another item's flag (0xC8) is clear; the deer's scene
    // still hands the jewel over through room_action, as in the original.
    bool grant = s_reply == PK_GRANT && zm_net_active() &&
        !interrupted && !zombie_mode_match_over() && g_playerEntity.health >= 0 &&
        request[4] == (short)(g_stageId | (g_roomId << 8)) &&
        *(unsigned char**)(evt + 8) == rec && Flg_ck((int)g_roomItemsFlags, rec[0x14]) != 0 &&
        pickup_fits(rec);
    if (!grant) {
        pickup_send(ZM_NET_DIRECTOR, PK_CANCEL, request);
        s_waiting = false;
        zm_reinforce_notice(s_reply == PK_DENY ? "SOMEONE ELSE TOOK THAT ITEM" : "PICKUP CANCELLED");
        return false;
    }
    s_reply = -1;
    s_expected = PK_DONE;
    pickup_send(ZM_NET_DIRECTOR, PK_COMMIT, request);
    sentAt = plat_time_ms();
    // Once committed, a timeout must not release the identity for another
    // award. Wait for the reliable receipt or a lost connection.
    while (s_reply < 0 && zm_net_active()) {
        zombie_mode_menu_frame();
        // Semantic retry also recovers a message rejected by a full outgoing
        // reliable queue. A duplicate commit only resends the host receipt.
        if (s_reply < 0 && plat_time_ms() - sentAt >= 500u) {
            pickup_send(ZM_NET_DIRECTOR, PK_COMMIT, request);
            sentAt = plat_time_ms();
        }
        if (s_reply < 0) Task_sleep(1);
    }
    bool done = s_reply == PK_DONE;
    s_waiting = false;
    if (done) g_selectedItemId = rec[8];
    else zm_reinforce_notice(s_reply == PK_DENY ? "SOMEONE ELSE TOOK THAT ITEM" : "PICKUP CANCELLED");
    return done;
}
