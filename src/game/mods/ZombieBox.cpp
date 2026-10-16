#include "ZombieModeInternal.h"
#include "ZombieNet.h"
#include "ZombieReconnect.h"
#include "../../Globals.h"
#include "../../platform/platform.h"
#include <cstring>

// Atomic host-approved swaps: expected box contents, offered inventory item.
// Receipts survive disconnects until the player's checkpoint acknowledges them.
enum { BOX_REQUEST, BOX_DONE, BOX_DENY, BOX_UPDATE };
struct BoxReceipt { bool used; short args[8]; };
static BoxReceipt s_receipts[4][32];
static short s_latest[4][8];
static bool s_seen[4], s_latestDone[4], s_waiting;
static unsigned short s_sequence, s_awarded;
static short s_request[8];
static int s_reply;
static unsigned int s_revision;
static bool s_revisionHave;

bool zombie_mode_box_shared(void) { return zombie_mode_armed() && zm_game_role() == ZM_NET_SURVIVOR; }
bool zombie_mode_box_waiting(void) { return s_waiting; }
unsigned short zm_box_reconnect_token(void) { return s_awarded; }
void zm_box_reconnect_restore(unsigned short token) { s_sequence = s_awarded = token; }
void zm_box_reset(void)
{
    memset(s_receipts, 0, sizeof(s_receipts));
    memset(s_seen, 0, sizeof(s_seen));
    memset(s_latestDone, 0, sizeof(s_latestDone));
    s_sequence = s_awarded = 0; s_waiting = false; s_reply = -1;
    s_revision = 0; s_revisionHave = false;
}
unsigned int zm_box_snapshot(void* out) { memcpy(out, g_itemboxSlots, sizeof(ItemSlot) * 48); return s_revision; }
void zm_box_snapshot_take(unsigned int revision, const void* slots)
{
    if (s_revisionHave && (int)(revision - s_revision) < 0) return;
    memcpy(g_itemboxSlots, slots, sizeof(ItemSlot) * 48);
    s_revision = revision; s_revisionHave = true;
}
static unsigned short box_item(const ItemSlot& s) { return s.Id | (s.qty << 8); }
static bool box_same(const short* a, const short* b) { return !memcmp(a + 1, b + 1, 7 * sizeof(short)); }
static void box_send(int dst, int op, const short* a)
{
    zm_net_send_event_to(dst, ZM_EV_BOX, (short)op, a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
}
static void box_update(int dst, const short* request)
{
    short a[8]; memcpy(a, request, sizeof(a));
    a[1] = (short)s_revision; a[5] = (short)(s_revision >> 16);
    a[4] = (short)box_item(g_itemboxSlots[a[2]]);
    box_send(dst, BOX_UPDATE, a);
}
void zm_box_checkpoint(int player, unsigned short token)
{
    if (player < 1 || player > 3) return;
    for (int i = 0; i < 32; i++) {
        BoxReceipt& r = s_receipts[player][i];
        if (r.used && (short)(token - (unsigned short)r.args[1]) >= 0) r.used = false;
    }
}
void zm_box_take(const short* a, int src)
{
    unsigned int seed = (unsigned short)a[6] | ((unsigned int)(unsigned short)a[7] << 16);
    if (!zombie_mode_armed() || seed != zm_net_seed() || a[2] < 0 || a[2] >= 48) return;
    // The host's replies and updates (back to the host's own survivor too, in
    // an AI director's game); everything else is a request for the host.
    if (a[0] != BOX_REQUEST) {
        if (zm_game_role() != ZM_NET_SURVIVOR || src != ZM_NET_DIRECTOR) return;
        if (a[0] == BOX_UPDATE) {
            if (zm_net_role() == ZM_NET_ZOMBIE) return;   // the host's box is the box
            unsigned int revision = (unsigned short)a[1] | ((unsigned int)(unsigned short)a[5] << 16);
            if (s_revisionHave && (int)(revision - s_revision) < 0) return;
            g_itemboxSlots[a[2]].Id = (unsigned char)a[4];
            g_itemboxSlots[a[2]].qty = (unsigned short)a[4] >> 8;
            s_revision = revision; s_revisionHave = true;
        } else if (s_waiting && s_reply != BOX_DONE && box_same(a, s_request) &&
                   (a[0] == BOX_DONE || a[0] == BOX_DENY)) s_reply = a[0];
        return;
    }
    if (zm_net_role() != ZM_NET_ZOMBIE || src < 0 || src > 3 || zm_net_char(src) < 0 ||
        a[5] < 0 || a[5] >= (zm_net_char(src) == ZM_CHAR_JILL ? 8 : 6)) return;
    unsigned short token = (unsigned short)a[1];
    // A delayed semantic retry must never replay an older box update.
    for (int i = 0; i < 32; i++) {
        const BoxReceipt& r = s_receipts[src][i];
        if (r.used && (unsigned short)r.args[1] == token) {
            box_update(src, a); // current contents, never the original swap's update
            box_send(src, box_same(a, r.args) ? BOX_DONE : BOX_DENY, a); return;
        }
    }
    if (s_seen[src] && (short)(token - (unsigned short)s_latest[src][1]) <= 0) {
        box_update(src, a);
        box_send(src, s_latestDone[src] && box_same(a, s_latest[src]) ? BOX_DONE : BOX_DENY, a); return;
    }
    s_seen[src] = true; s_latestDone[src] = false;
    memcpy(s_latest[src], a, sizeof(s_latest[src]));
    int free = -1;
    for (int i = 0; i < 32; i++) if (!s_receipts[src][i].used) { free = i; break; }
    const ZmNetPeerState* peer = zm_seat_state(src);
    ItemSlot& slot = g_itemboxSlots[a[2]];
    bool valid = free >= 0 && !zombie_mode_match_over() && peer && !peer->dead &&
                 !peer->spectating && !peer->transitioning && box_item(slot) == (unsigned short)a[3] &&
                 (a[3] || a[4]);
    if (!valid) {
        box_update(src, a); box_send(src, BOX_DENY, a); return;
    }
    BoxReceipt& receipt = s_receipts[src][free]; receipt.used = true;
    memcpy(receipt.args, a, sizeof(receipt.args));
    // The host's own survivor never reconnects: no receipt to keep for it.
    if (src == zm_net_self()) receipt.used = false;
    slot.Id = (unsigned char)a[4]; slot.qty = (unsigned short)a[4] >> 8;
    s_latestDone[src] = true;
    s_revision++;
    box_update(ZM_NET_ALL, a);
    box_send(src, BOX_DONE, a);
}

bool zombie_mode_box_claim(unsigned int playerSlot, unsigned int boxSlot,
                           unsigned char* item, unsigned char* quantity)
{
    if (!zombie_mode_box_shared()) return true;
    if (s_waiting || playerSlot >= (unsigned int)zombie_mode_inventory_slots(6) || boxSlot >= 48 ||
        g_playerEntity.health < 0 || zombie_mode_match_over() || !zm_net_active()) return false;
    const unsigned char* offered = (unsigned char*)g_ItemSlotsPointer + playerSlot * 2;
    short request[8] = { BOX_REQUEST, (short)++s_sequence, (short)boxSlot,
        (short)(*item | (*quantity << 8)), (short)(offered[0] | (offered[1] << 8)),
        (short)playerSlot, (short)zm_net_seed(), (short)(zm_net_seed() >> 16) };
    memcpy(s_request, request, sizeof(request)); s_waiting = true; s_reply = -1;
    unsigned int sent = plat_time_ms(); box_send(ZM_NET_DIRECTOR, BOX_REQUEST, request);
    // A sent request may already have committed. Never guess success/failure
    // from latency, or cancel after the host has transferred ownership.
    while (s_reply < 0 && zm_net_active() && !zombie_mode_match_over()) {
        zombie_mode_menu_frame();
        if (s_reply < 0 && plat_time_ms() - sent >= 500u) {
            box_send(ZM_NET_DIRECTOR, BOX_REQUEST, request); sent = plat_time_ms();
        }
        if (s_reply < 0) Task_sleep(1);
    }
    if (s_reply == BOX_DONE) return true; // held until inventory bookkeeping finishes
    s_waiting = false;
    zm_reinforce_notice(s_reply == BOX_DENY ? "BOX CHANGED - TRY AGAIN" : "BOX TRANSFER CANCELLED");
    return false;
}
void zombie_mode_box_finished(void)
{
    if (!s_waiting) return;
    s_awarded = s_sequence; s_waiting = false;
}

void zm_box_reconnect_reconcile(int player, ZmReconnectPlayer* p)
{
    if (player < 1 || player > 3) return;
    // Slot storage is recycled only after an acknowledged checkpoint. Sort
    // outstanding receipts by token rather than their array position.
    for (;;) {
        const BoxReceipt* next = NULL;
        unsigned short delta = 0x8000;
        for (int i = 0; i < 32; i++) {
            const BoxReceipt& r = s_receipts[player][i];
            unsigned short d = (unsigned short)((unsigned short)r.args[1] - p->boxToken);
            if (r.used && d && d < delta) { next = &r; delta = d; }
        }
        if (!next) break;
        const short* a = next->args;
        int at = a[5], oldCount = p->card.totalHeldItems;
        unsigned short acquired = (unsigned short)a[3];
        // Restore the committed swap; the menu never changes inventory while
        // awaiting its receipt. Compact just as rearrange_item_slots does.
        if (at >= oldCount && acquired) {
            int bit = 0; while (p->inventoryMask & (1u << bit)) bit++;
            p->indices[at] = (unsigned char)bit; p->inventoryMask |= 1u << bit;
        }
        if (p->card.equippedItemId == at + 1) p->card.equippedItemId = 0;
        p->inventory[at * 2] = (unsigned char)acquired;
        p->inventory[at * 2 + 1] = acquired >> 8;
        int count = 0;
        int limit = zm_net_char(player) == ZM_CHAR_JILL ? 8 : 6;
        for (int i = 0; i < limit; i++) {
            if (!p->inventory[i * 2]) { if (i < oldCount) p->inventoryMask &= ~(1u << p->indices[i]); continue; }
            if (p->card.equippedItemId == i + 1) p->card.equippedItemId = (unsigned char)(count + 1);
            p->inventory[count * 2] = p->inventory[i * 2];
            p->inventory[count * 2 + 1] = p->inventory[i * 2 + 1]; p->indices[count] = p->indices[i]; count++;
        }
        memset(p->inventory + count * 2, 0, 16 - count * 2);
        p->card.totalHeldItems = (unsigned char)count;
        p->boxToken = (unsigned short)a[1];
    }
    if (s_seen[player] && (short)((unsigned short)s_latest[player][1] - p->boxToken) > 0)
        p->boxToken = (unsigned short)s_latest[player][1]; // avoid reusing a rejected request's token
}
