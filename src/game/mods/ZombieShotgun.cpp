#include "ZombieModeInternal.h"
#include "ZombieNet.h"
#include "ZombieReconnect.h"
#include "../PrintText.h"
#include "../entities/EntityCommon.h"
#include "../../marni/MarniSound.h"
#include "../../system/AssetPath.h"
#include "../../DebugPrint.h"
#include <cstring>
#include <cstdio>

// Port-added optional shotgun puzzle. The native first-visit models are used,
// but its single-player fatal/rescue cutscenes must not control remote players.
extern void Flg_on(int baseAddr, unsigned int bitIndex);

#define ZM_SHOTGUN_MS 15000u
// The slab's height (object +0x38): it starts at the top, reaches KILL as the
// timer runs out (the survivors inside die), then drops to the floor in 3 s.
// KILL is about 3 feet (1100 units) below the original's -4750: lower and
// more menacing before it kills.
#define ZM_CEILING_TOP_Y   (-10280)
#define ZM_CEILING_KILL_Y  (-3650)
#define ZM_CEILING_FLOOR_Y (-2500)
static unsigned char s_placed; // 0: empty, 1: broken replacement, 2: working shotgun returned
static bool s_broken, s_active, s_blocked, s_consume, s_jump, s_rescueSeen, s_consumeAxe;
static unsigned char s_requestItem = ITEM_BROKEN_SHOTGUN;
static unsigned short s_consumedReplacement, s_requestSerial, s_consumeSerial;
static unsigned char s_consumeItem;
static bool s_consumedAxe;
static unsigned int s_serial, s_receipt[4], s_receiptItem[4];
static unsigned char s_plateQty;
static int s_breaker = -1;
static unsigned int s_fadeAt, s_lookUntil, s_crushAt;
static int s_chainBank, s_muffledBank, s_soundPlaying;
static int s_rescueVoiceBank, s_rescueVoiceNext;
static unsigned int s_rescueVoiceUntil, s_rescueVoiceResumeAt;
static int s_placer = -1, s_request;
static unsigned int s_deadline, s_remaining, s_stateAt, s_requestAt, s_noteAt;
static unsigned int s_rescuedMask;
static unsigned int s_crushedMask, s_checkAt;
static bool s_checkProgression;
static int s_lastProof = -1;
static unsigned char s_jumpRecord[24];
static bool s_arrivalCamera;
static unsigned int s_ceilingDepth, s_ceilingAt;

// Depth is measured in milliseconds of descent, so returning uses exactly
// the same speed and takes only as long as the preceding partial descent.
static unsigned int shotgun_ceiling_depth(void)
{
    unsigned int now = zm_game_time_ms();
    unsigned int delta = now - s_ceilingAt;
    s_ceilingAt = now;
    if (s_active) {
        unsigned int left = (int)(s_deadline - now) > 0 ? s_deadline - now : 0;
        if (left > ZM_SHOTGUN_MS) left = ZM_SHOTGUN_MS;
        s_ceilingDepth = ZM_SHOTGUN_MS - left;
    } else if (!s_blocked) {
        s_ceilingDepth = delta < s_ceilingDepth ? s_ceilingDepth - delta : 0;
    }
    return s_ceilingDepth;
}

// Tag 7 returns from an item-name substitution to the enclosing message.
// A bare STR terminator would cut off the pickup question and Yes/No prompt.
static constexpr auto s_pickaxeName = STR("PICKAXE\x07");
static constexpr auto s_pickaxeDescription = STR("A pickaxe for\\nbreaking doors.");
static constexpr auto s_clearArea = STR("You need to clear the area\\nbefore breaking down the door.");
static constexpr auto s_doorBlocked = STR("The door is blocked.");
static constexpr auto s_doorLocked = STR("The door is locked.");
static constexpr auto s_axeUnavailable = STR("Use this at the trap door\\nwhile the ceiling is moving.");
static unsigned char s_pickaxeIcon[40 * 30];
static bool s_iconReady;

unsigned char* zombie_mode_pickaxe_name(unsigned char item)
{
    return zombie_mode_armed() && item == ITEM_PICK_AXE ? (unsigned char*)s_pickaxeName.bytes : NULL;
}
unsigned char* zombie_mode_pickaxe_description(unsigned short description)
{
    return zombie_mode_armed() && description == ITEM_PICK_AXE - 1 ? (unsigned char*)s_pickaxeDescription.bytes : NULL;
}
const unsigned char* zombie_mode_pickaxe_icon(int imageRow)
{
    return zombie_mode_armed() && s_iconReady && imageRow == g_ItemImageLookupTable[ITEM_PICK_AXE * 4] - 1
        ? s_pickaxeIcon : NULL;
}

static unsigned char pickaxe_color(const unsigned char* palette, int r, int g, int b)
{
    int best = 0x7FFFFFFF, index = 1;
    for (int i = 1; i < 256; i++) {
        unsigned int c = palette[i * 2] | ((unsigned int)palette[i * 2 + 1] << 8);
        int dr = (int)(c & 31) - r, dg = (int)((c >> 5) & 31) - g, db = (int)((c >> 10) & 31) - b;
        int distance = dr * dr + dg * dg + db * db;
        if (distance < best) { best = distance; index = i; }
    }
    return (unsigned char)index;
}

static void pickaxe_line(int x0, int y0, int x1, int y1, int radius, unsigned char color)
{
    int dx = x1 - x0, dy = y1 - y0;
    int steps = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (ay > steps) steps = ay;
    if (!steps) steps = 1;
    for (int i = 0; i <= steps; i++) {
        int x = x0 + dx * i / steps, y = y0 + dy * i / steps;
        for (int oy = -radius; oy <= radius; oy++) for (int ox = -radius; ox <= radius; ox++) {
            int px = x + ox, py = y + oy;
            if (px >= 0 && px < 40 && py >= 0 && py < 30) s_pickaxeIcon[py * 40 + px] = color;
        }
    }
}

// The PC sprite sheet has no art for this unused item. Generate its indexed
// 40x30 inventory image once while startup loads STATUS.TIM, using the same
// third CLUT row that LoadImage uses for ITEM_ALL.PIX. Index zero stays clear.
void zombie_mode_pickaxe_icon_init(const void* statusTim, unsigned int bytes)
{
    s_iconReady = false; memset(s_pickaxeIcon, 0, sizeof(s_pickaxeIcon));
    if (!statusTim || bytes < 20) return;
    const unsigned char* tim = (const unsigned char*)statusTim;
    unsigned int width = tim[16] | ((unsigned int)tim[17] << 8);
    unsigned int height = tim[18] | ((unsigned int)tim[19] << 8);
    if (tim[0] != 0x10 || !(tim[4] & 8) || width != 256 || height < 3 || bytes < 20 + 3 * 512) return;
    const unsigned char* palette = tim + 20 + 2 * 512;
    unsigned char edge = pickaxe_color(palette, 5, 5, 6), wood = pickaxe_color(palette, 19, 12, 6);
    unsigned char grain = pickaxe_color(palette, 27, 19, 10), steel = pickaxe_color(palette, 18, 20, 22);
    unsigned char shine = pickaxe_color(palette, 29, 30, 31);
    pickaxe_line(20, 7, 12, 27, 2, edge);
    pickaxe_line(20, 7, 12, 26, 1, wood);
    pickaxe_line(20, 8, 13, 25, 0, grain);
    const int head[][2] = { {6, 12}, {10, 7}, {17, 4}, {23, 5}, {29, 8}, {34, 13} };
    for (int i = 0; i < 5; i++) pickaxe_line(head[i][0], head[i][1], head[i+1][0], head[i+1][1], 1, edge);
    for (int i = 0; i < 5; i++) pickaxe_line(head[i][0], head[i][1], head[i+1][0], head[i+1][1], 0, steel);
    pickaxe_line(10, 6, 17, 3, 0, shine); pickaxe_line(17, 3, 23, 4, 0, shine);
    pickaxe_line(23, 4, 29, 7, 0, shine);
    pickaxe_line(19, 5, 21, 8, 1, steel);
    s_iconReady = true;
}

bool zm_shotgun_crushed(int player)
{
    return player >= 0 && player < ZM_NET_MAX_PLAYERS && (s_crushedMask & (1u << player)) != 0;
}

void zm_shotgun_clear_inventory(void)
{
    if (!g_ItemSlotsPointer) return;
    memset((void*)g_ItemSlotsPointer, 0, 16);
    memset(g_ItemSlotIndices, 0, 8);
    g_ItemSlotsBitmask = 0; g_TotalHeldItems = 0; g_EquippedItemId = 0;
}

static bool shotgun_here(int room) { return g_stageId == STAGE_MANSION_RETURN_1F && g_roomId == room; }
static bool shotgun_empty() { return !s_placed && Flg_ck((int)g_roomItemsFlags, 0) == 0; }
static bool shotgun_near_plate(int x, int z) { return x >= 9820 && x <= 12800 && z >= 4900 && z <= 7350; }
static bool shotgun_near_door(int x, int z) { return x >= 6250 && x <= 9200 && z >= 7900 && z <= 11600; }
static bool shotgun_near_inside(int x, int z) { return x >= 500 && x <= 3300 && z >= 3500 && z <= 7300; }

bool zm_shotgun_monster_blocks(int room, int mx, int mz, int x, int z)
{
    int dx = mx - x, dz = mz - z;
    if (room == ROOM_TRAP_ROOM) return true;
    return dx >= -1500 && dx <= 1500 && dz >= -1500 && dz <= 1500 &&
           dx * dx + dz * dz <= 1500 * 1500; // about five feet around the user
}

static void shotgun_message(const unsigned char* text)
{
    if (set_message_display(0xFB, 0) == 0) g_MessagePtr = (unsigned char*)text;
}

static bool shotgun_clear(int room, int x, int z)
{
    return zm_world_shotgun_clear(STAGE_MANSION_RETURN_1F, ROOM_TRAP_ROOM, x, z) &&
        (room == ROOM_TRAP_ROOM || zm_world_shotgun_clear(STAGE_MANSION_RETURN_1F, (unsigned char)room, x, z));
}
bool zm_shotgun_room_blocked(unsigned char stage, unsigned char room)
{
    return zombie_mode_armed() && stage == STAGE_MANSION_RETURN_1F && (s_blocked || s_broken) &&
        (room == ROOM_TRAP_ROOM || room == ROOM_LIVING_ROOM);
}
static int get_item_slot(unsigned char id)
{
    if (!g_ItemSlotsPointer) return -1;
    const unsigned char* slots = (const unsigned char*)g_ItemSlotsPointer;
    for (int i = 0; i < 8; i++)
        if (slots[i * 2] == id && (slots[i * 2 + 1] || id == ITEM_SHOTGUN)) return i;
    return -1;
}

void zm_shotgun_reset(void)
{
    if (s_rescueVoiceBank) { setSndStop(s_rescueVoiceBank); destroySndBank(s_rescueVoiceBank); }
    s_rescueVoiceBank = s_rescueVoiceNext = 0; s_rescueVoiceUntil = s_rescueVoiceResumeAt = 0;
    zm_shotgun_crush_view(false);
    s_placed = 0; s_broken = s_active = s_blocked = s_consume = s_jump = s_consumeAxe = false;
    s_requestItem = ITEM_BROKEN_SHOTGUN;
    s_consumedReplacement = s_requestSerial = s_consumeSerial = 0; s_consumeItem = 0;
    s_consumedAxe = false; s_serial = s_plateQty = 0; s_breaker = -1;
    memset(s_receipt, 0, sizeof(s_receipt)); memset(s_receiptItem, 0, sizeof(s_receiptItem));
    s_fadeAt = s_lookUntil = s_crushAt = 0;
    s_arrivalCamera = false;
    s_ceilingDepth = 0; s_ceilingAt = zm_game_time_ms();
    if (s_chainBank) destroySndBank(s_chainBank);
    if (s_muffledBank) destroySndBank(s_muffledBank);
    s_chainBank = s_muffledBank = s_soundPlaying = 0;
    s_rescueSeen = false;
    s_placer = -1; s_request = 0;
    s_deadline = s_remaining = s_stateAt = s_requestAt = 0;
    s_noteAt = 0;
    s_rescuedMask = 0;
    s_crushedMask = s_checkAt = 0; s_checkProgression = false;
    s_lastProof = -1;
}

static void shotgun_rescue_dialogue(void)
{
    if (s_breaker < 0 || s_breaker >= ZM_NET_MAX_PLAYERS || zm_net_char(s_breaker) != ZM_CHAR_BARRY) return;
    for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
        if (i != s_breaker && (s_rescuedMask & (1u << i)) && zm_net_char(i) == ZM_CHAR_JILL) {
            // ROOM1091 event 0 uses voice IDs 0x35/0x36 (V105_05/06):
            // Jill's thanks followed by Barry's original sandwich reply.
            s_rescueVoiceNext = 5; s_rescueVoiceUntil = zm_game_time_ms() + 8000;
            s_rescueVoiceResumeAt = 0;
            break;
        }
    }
}

static void shotgun_rescue_voice(bool enabled, bool ready)
{
    if (!enabled || (s_rescueVoiceNext > 5 && !shotgun_here(ROOM_TRAP_PASSAGE))) {
        if (s_rescueVoiceBank) { setSndStop(s_rescueVoiceBank); destroySndBank(s_rescueVoiceBank); }
        s_rescueVoiceBank = s_rescueVoiceNext = 0; s_rescueVoiceUntil = s_rescueVoiceResumeAt = 0;
        return;
    }
    if (!s_rescueVoiceNext) return;
    if (s_rescueVoiceBank) {
        if (getSndStat(s_rescueVoiceBank) == 1) return;
        destroySndBank(s_rescueVoiceBank); s_rescueVoiceBank = 0;
        if (s_rescueVoiceNext <= 6) {
            s_rescueVoiceResumeAt = zm_game_time_ms() + 2000;
            return;
        }
    } else if ((int)(zm_game_time_ms() - s_rescueVoiceUntil) >= 0) {
        s_rescueVoiceNext = 0; return;
    }
    if (s_rescueVoiceResumeAt && (int)(zm_game_time_ms() - s_rescueVoiceResumeAt) < 0) return;
    if (!ready || s_jump || !shotgun_here(ROOM_TRAP_PASSAGE) ||
        (int)(s_fadeAt - zm_game_time_ms()) > 0) return;
    if (s_rescueVoiceNext > 6) { s_rescueVoiceNext = 0; return; }
    char path[128];
    snprintf(path, sizeof(path), GAME_DATA_ROOT "voice\\V105_%02d.wav", s_rescueVoiceNext++);
    s_rescueVoiceBank = loadSndBankFromWav(path);
    if (s_rescueVoiceBank) { pan_set(s_rescueVoiceBank, 0); set_volume(s_rescueVoiceBank, 0); SetSndSlot(s_rescueVoiceBank, 0); }
    // Missing optional voice assets must not stall the queue or the game.
    s_rescueVoiceUntil = zm_game_time_ms() + 12000;
}

static void shotgun_send(int op, unsigned int mask)
{
    unsigned int seed = zm_net_seed();
    zm_net_send_event8(ZM_EV_SHOTGUN, (short)op, (short)(s_placed | (s_plateQty << 2)), s_broken,
        s_active ? (short)((s_remaining + 99) / 100) : s_blocked ? (short)-2 : (short)-1,
        (short)(mask | (s_crushedMask << 4)),
        (short)((s_placer + 1) | ((s_breaker + 1) << 3) | (s_serial << 6)),
        (short)seed, (short)(seed >> 16));
}

static void shotgun_remove_broken(void)
{
    int slot = get_item_slot(s_consumeItem);
    if (slot >= 0 && g_ItemSlotsPointer != NULL) {
        unsigned char* item = (unsigned char*)g_ItemSlotsPointer + slot * 2;
        if (g_EquippedItemId == slot + 1) g_EquippedItemId = 0;
        item[0] = item[1] = 0;
        rearrange_item_slots();
    }
    s_consume = false;
    s_consumedReplacement = s_consumeSerial;
}

unsigned short zm_shotgun_replacement_consumed(void) { return s_consumedReplacement; }
bool zm_shotgun_pickaxe_consumed(void) { return s_consumedAxe; }

static void shotgun_refuse(int src, int reason = 0)
{
    unsigned int seed = zm_net_seed();
    zm_net_send_event_to(src, ZM_EV_SHOTGUN, 4, (short)reason, 0, 0, 0, 0, (short)seed, (short)(seed >> 16));
}

// USE from the inventory at the empty mounting plate. The normal item-use
// script is bypassed; only host confirmation consumes the replacement.
bool zm_shotgun_can_return_shotgun(void)
{
    return zombie_mode_armed() && zm_game_role() == ZM_NET_SURVIVOR &&
        shotgun_here(ROOM_LIVING_ROOM) && !s_broken && shotgun_empty() &&
        shotgun_near_plate(g_playerEntity.scaMatrixData.localMatrix.t[0],
                           g_playerEntity.scaMatrixData.localMatrix.t[2]);
}

static bool shotgun_use_replacement(unsigned char item)
{
    if (zm_game_role() != ZM_NET_SURVIVOR || !shotgun_here(ROOM_LIVING_ROOM) || !shotgun_empty() ||
        !shotgun_near_plate(g_playerEntity.scaMatrixData.localMatrix.t[0],
                            g_playerEntity.scaMatrixData.localMatrix.t[2]) ||
        g_playerEntity.health < 0 || g_playerEntity.isBeingAttackedFlag ||
        get_item_slot(item) < 0) return false;
    s_requestItem = item; s_requestSerial = (unsigned short)s_serial; s_request = 1; s_requestAt = 0;
    return true;
}

bool zm_shotgun_use_broken(void) { return shotgun_use_replacement(ITEM_BROKEN_SHOTGUN); }
bool zm_shotgun_use_shotgun(void) { return shotgun_use_replacement(ITEM_SHOTGUN); }

// Only an accepted use/rescue closes the menu. Refusals keep their normal
// feedback path; closing restores player control before consumption/jumping.
bool zm_shotgun_menu_finished(void)
{
    return zombie_mode_armed() && (s_consume || s_consumeAxe || s_jump);
}

bool zm_shotgun_pending(void) { return s_request != 0 || s_consume || s_consumeAxe || s_jump; }

bool zm_shotgun_use_pickaxe(void)
{
    int x = g_playerEntity.scaMatrixData.localMatrix.t[0], z = g_playerEntity.scaMatrixData.localMatrix.t[2];
    bool atDoor = (shotgun_here(ROOM_TRAP_PASSAGE) && shotgun_near_door(x, z)) ||
                  (shotgun_here(ROOM_TRAP_ROOM) && shotgun_near_inside(x, z));
    if (zm_game_role() != ZM_NET_SURVIVOR || !s_active || s_blocked || s_broken || !atDoor ||
        g_playerEntity.health < 0 || g_playerEntity.isBeingAttackedFlag || get_item_slot(ITEM_PICK_AXE) < 0) {
        shotgun_message(s_axeUnavailable.bytes); return false;
    }
    if (!shotgun_clear(g_roomId, x, z)) { shotgun_message(s_clearArea.bytes); return false; }
    s_request = 2; s_requestAt = 0;
    return true;
}

// Called at the door interaction, before the normal door lock handling.
bool zm_shotgun_door(const unsigned char* rec)
{
    if ((!s_broken && !shotgun_empty()) ||
        g_stageId != STAGE_MANSION_RETURN_1F) return false;
    unsigned char stage, room;
    zm_decode_dest(rec[0x0D], g_stageId, &stage, &room);
    bool outside = g_roomId == ROOM_TRAP_PASSAGE && room == ROOM_TRAP_ROOM;
    bool inside = g_roomId == ROOM_TRAP_ROOM && room == ROOM_TRAP_PASSAGE;
    bool inner = (g_roomId == ROOM_TRAP_ROOM && room == ROOM_LIVING_ROOM) ||
                 (g_roomId == ROOM_LIVING_ROOM && room == ROOM_TRAP_ROOM);
    if (stage == g_stageId && (inner || outside || inside) && (s_blocked || s_broken)) {
        shotgun_message(s_doorBlocked.bytes);
        return true;
    }
    if (stage != g_stageId || (!outside && !inside)) return false;
    if (zm_game_role() != ZM_NET_SURVIVOR) return false;
    if (!s_active && !s_blocked) return false;
    shotgun_message(s_blocked ? s_doorBlocked.bytes : s_doorLocked.bytes);
    return true;
}

// Runtime reachability may reset the slab with a reachable replacement, or
// rescue an active trap with an axe. Clearing monsters is possible gameplay.
bool zm_shotgun_route_open(unsigned char fromStage, unsigned char fromRoom,
    unsigned char toStage, unsigned char toRoom, unsigned int owned, bool plateReachable)
{
    if (fromStage != STAGE_MANSION_RETURN_1F || toStage != fromStage || (!s_broken && !shotgun_empty())) return true;
    bool inner = (fromRoom == ROOM_TRAP_ROOM && toRoom == ROOM_LIVING_ROOM) ||
                 (fromRoom == ROOM_LIVING_ROOM && toRoom == ROOM_TRAP_ROOM);
    bool outer = (fromRoom == ROOM_TRAP_ROOM && toRoom == ROOM_TRAP_PASSAGE) ||
                 (fromRoom == ROOM_TRAP_PASSAGE && toRoom == ROOM_TRAP_ROOM);
    if (s_broken && (inner || outer)) return false;
    if ((!inner && !outer) || (!s_active && !s_blocked)) return true;
    if (plateReachable && (owned & 0x600u)) return true;
    if (s_active && (owned & 0x800u)) return true;
    return inner && !s_blocked;
}

static bool shotgun_occupied(void)
{
    for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
        const ZmNetPeerState* p = zm_seat_state(i);
        if (p && p->valid && !zm_shotgun_crushed(i) && !p->dead && !p->spectating && !p->transitioning &&
            p->stage == STAGE_MANSION_RETURN_1F && p->room == ROOM_TRAP_ROOM) return true;
    }
    return false;
}

static void shotgun_start(void)
{
    if (s_active || s_blocked || s_broken || !shotgun_empty() || !shotgun_occupied()) return;
    s_active = true; s_remaining = ZM_SHOTGUN_MS;
    s_deadline = zm_game_time_ms() + ZM_SHOTGUN_MS; s_stateAt = 0;
    if (shotgun_here(ROOM_TRAP_PASSAGE)) play_sfx(2, 0x21, 0);
    shotgun_send(5, 0);
    dbg_printf("[shotgun] ceiling started immediately with an occupied trap room\n");
}

static void shotgun_finish(void)
{
    if (!s_active) return;
    unsigned int now = zm_game_time_ms();
    s_remaining = (int)(s_deadline - now) > 0 ? s_deadline - now : 0;
    if (s_remaining && shotgun_occupied()) return;
    if (!s_remaining) for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
        const ZmNetPeerState* p = zm_seat_state(i);
        if (p && p->valid && !zm_shotgun_crushed(i) && !p->dead && !p->spectating && !p->transitioning &&
            p->stage == STAGE_MANSION_RETURN_1F && p->room == ROOM_TRAP_ROOM) {
            s_crushedMask |= 1u << i; s_checkProgression = true; s_checkAt = now + 2000;
            dbg_printf("[shotgun] permanently crushed survivor %d; inventory lost\n", i);
        }
    }
    s_active = false; s_blocked = true; s_remaining = 0; s_crushAt = now;
    shotgun_send(0, 0);
    dbg_printf("[shotgun] ceiling finished; both doors blocked by slab\n");
}

unsigned short zm_shotgun_pickup_identity(const unsigned char* record)
{
    if (shotgun_here(ROOM_LIVING_ROOM) && record[0x14] == 1 && s_placed)
        return (unsigned short)(1u | ((s_serial & 255u) << 8));
    return record[0x14];
}

bool zm_shotgun_pickup_valid(const short* a)
{
    unsigned int identity = (unsigned short)a[3];
    if (!a[2] && (unsigned short)a[4] == (STAGE_MANSION_RETURN_1F | (ROOM_LIVING_ROOM << 8)) &&
        (identity & 255) == 1) {
        unsigned char item = s_placed == 2 ? ITEM_SHOTGUN : ITEM_BROKEN_SHOTGUN;
        return s_placed && (identity >> 8) == (s_serial & 255) &&
               (unsigned char)a[5] == item && ((unsigned short)a[5] >> 8) == s_plateQty;
    }
    return a[2] || identity <= 255;
}

void zm_shotgun_pickup(const short* a)
{
    if (a[2] || (unsigned short)a[4] != (STAGE_MANSION_RETURN_1F | (ROOM_LIVING_ROOM << 8))) return;
    if (((unsigned short)a[3] & 255) == 1) s_placed = 0;
    else if (a[3] != 0) return;
    if (zm_net_role() == ZM_NET_ZOMBIE) {
        // Run directly in the host's committed pickup path: a third survivor
        // already standing in the trap room does not wait for the lifter's STATE.
        shotgun_start(); shotgun_send(0, 0);
    }
}

// Host decides state changes, consumption and rescue. Requests are retried;
// a placement serial and the breaker's receipt make reconciliation idempotent.
void zm_shotgun_take(const short* a, int src)
{
    unsigned int seed = (unsigned short)a[6] | ((unsigned int)(unsigned short)a[7] << 16);
    if (!zombie_mode_armed() || seed != zm_net_seed()) return;
    // Requests (1 place, 2 break) are the host's; the rest come from it - to
    // its own survivor too, in an AI director's game.
    if (a[0] == 1 || a[0] == 2) {
        if (zm_net_role() != ZM_NET_ZOMBIE || src < 0 || src >= ZM_NET_MAX_PLAYERS || zm_net_char(src) < 0) return;
        const ZmNetPeerState* p = zm_seat_state(src);
        if (!p || !p->valid || zm_shotgun_crushed(src) || p->dead || p->spectating || p->transitioning || p->attacked ||
            p->stage != STAGE_MANSION_RETURN_1F) { shotgun_refuse(src); return; }
        shotgun_finish(); // finish/kill before considering a request at the deadline
        if (a[0] == 1) {
            if (s_placed || (unsigned short)a[2] != s_serial) {
                if (s_placer == src && s_serial == ((unsigned short)a[2] % 1023 + 1)) shotgun_send(0, 0);
                else shotgun_refuse(src);
                return;
            }
            unsigned char replacement = (unsigned char)a[1], inventory[16];
            if ((replacement != ITEM_BROKEN_SHOTGUN && replacement != ITEM_SHOTGUN) ||
                s_broken || !shotgun_empty() || p->room != ROOM_LIVING_ROOM ||
                !shotgun_near_plate(p->x, p->z) || !zm_net_inventory(src, inventory)) { shotgun_refuse(src); return; }
            int slot = -1;
            for (int i = 0; i < 8; i++) if (inventory[i*2] == replacement) { slot = i; break; }
            if (slot < 0 || (replacement != ITEM_SHOTGUN && !inventory[slot*2+1])) { shotgun_refuse(src); return; }
            s_placed = replacement == ITEM_SHOTGUN ? 2 : 1; s_placer = src;
            s_plateQty = inventory[slot*2+1]; s_serial = s_serial % 1023 + 1;
            s_receipt[src] = s_serial; s_receiptItem[src] = replacement;
            s_active = s_blocked = false; s_remaining = 0;
            Flg_on((int)g_roomItemsFlags, 1);
            shotgun_send(0, 0);
            // The host's own survivor gets no broadcast back: its swap now.
            if (src == zm_net_self() && s_request == 1) {
                s_consume = true; s_consumeItem = s_requestItem; s_consumeSerial = (unsigned short)s_serial;
                s_request = 0;
            }
            dbg_printf("[shotgun] survivor %d placed %s shotgun, receipt %u\n", src,
                s_placed == 2 ? "working" : "broken", s_serial);
        } else {
            if (s_broken) { shotgun_send(0, 0); return; }
            bool atDoor = (p->room == ROOM_TRAP_PASSAGE && shotgun_near_door(p->x, p->z)) ||
                          (p->room == ROOM_TRAP_ROOM && shotgun_near_inside(p->x, p->z));
            if (!s_active || s_blocked || !s_remaining || !shotgun_empty() || !atDoor ||
                !zm_net_has_item(src, ITEM_PICK_AXE)) { shotgun_refuse(src); return; }
            if (!shotgun_clear(p->room, p->x, p->z)) { shotgun_refuse(src, 1); return; }
            unsigned int mask = 1u << src;
            for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
                const ZmNetPeerState* victim = zm_seat_state(i);
                if (victim && victim->valid && !zm_shotgun_crushed(i) && !victim->dead && !victim->spectating &&
                    victim->stage == STAGE_MANSION_RETURN_1F &&
                    (victim->room == ROOM_TRAP_ROOM || victim->room == ROOM_LIVING_ROOM)) mask |= 1u << i;
            }
            s_breaker = src; s_broken = true; s_active = s_blocked = false; s_remaining = 0;
            s_rescuedMask = mask;
            shotgun_rescue_dialogue();
            if (shotgun_here(ROOM_TRAP_PASSAGE)) s_fadeAt = zm_game_time_ms() + 900;
            shotgun_send(3, mask);
            // The host's own survivor: the broadcast's effects here.
            if (src == zm_net_self()) { s_consumeAxe = !s_consumedAxe; s_request = 0; }
            if ((mask & (1u << zm_net_self())) && zm_game_role() == ZM_NET_SURVIVOR &&
                (shotgun_here(ROOM_TRAP_ROOM) || shotgun_here(ROOM_LIVING_ROOM) || shotgun_here(ROOM_TRAP_PASSAGE))) s_jump = true;
            dbg_printf("[shotgun] survivor %d broke door and spent pickaxe, rescue mask %X\n", src, mask);
        }
        return;
    }
    if (src != ZM_NET_DIRECTOR) return;
    if (a[0] == 4) {
        s_request = 0;
        shotgun_message(a[1] == 1 ? s_clearArea.bytes : s_axeUnavailable.bytes);
        return;
    }
    if (a[0] != 0 && a[0] != 3 && a[0] != 5) return;
    unsigned int placed = (unsigned short)a[1] & 3u, metadata = (unsigned short)a[5];
    if (placed > 2 || a[3] < -2 || a[3] > 150) return;
    bool wasActive = s_active, wasBlocked = s_blocked;
    if (s_placed && !placed) FUN_00473f10((int*)g_roomItemsFlags, 1);
    s_placed = (unsigned char)placed; s_plateQty = (unsigned char)((unsigned short)a[1] >> 2);
    s_broken = a[2] != 0; s_blocked = !s_broken && a[3] == -2;
    if (s_blocked && !wasBlocked) s_crushAt = zm_game_time_ms();
    s_placer = (int)(metadata & 7) - 1; s_breaker = (int)((metadata >> 3) & 7) - 1;
    s_serial = metadata >> 6;
    s_crushedMask |= ((unsigned short)a[4] >> 4) & 15u;
    s_remaining = a[3] >= 0 ? (unsigned int)a[3] * 100 : 0;
    s_deadline = zm_game_time_ms() + s_remaining;
    s_active = !s_broken && !s_blocked && a[3] >= 0;
    if (s_placed) Flg_on((int)g_roomItemsFlags, 1);
    if (s_placer == zm_net_self() && s_request == 1 && s_serial == (s_requestSerial % 1023 + 1)) {
        s_consume = true; s_consumeItem = s_requestItem; s_consumeSerial = (unsigned short)s_serial; s_request = 0;
    }
    if (s_broken && s_breaker == zm_net_self() && !s_consumedAxe) s_consumeAxe = true;
    if (s_placed || s_broken) s_request = 0;
    if (s_active && !wasActive) {
        if (shotgun_here(ROOM_TRAP_ROOM) || shotgun_here(ROOM_TRAP_PASSAGE)) play_sfx(2, 0x21, 0);
        if (shotgun_here(ROOM_TRAP_ROOM) && zm_game_role() == ZM_NET_SURVIVOR) {
            s_lookUntil = zm_game_time_ms() + 900;
            g_playerEntity.lookAtFlags = 0x13; g_playerEntity.lookAtYawStep = 0x40;
            g_playerEntity.lookAtPitchStep = 0x40;
            g_playerEntity.lookAtTargetX = g_playerEntity.scaMatrixData.localMatrix.t[0];
            g_playerEntity.lookAtTargetY = -9000;
            g_playerEntity.lookAtTargetZ = g_playerEntity.scaMatrixData.localMatrix.t[2] + 1000;
        }
    }
    if (a[0] == 3 && !s_rescueSeen) {
        s_rescuedMask = (unsigned short)a[4] & 15u;
        s_rescueSeen = true;
        shotgun_rescue_dialogue();
        if (shotgun_here(ROOM_TRAP_PASSAGE)) s_fadeAt = zm_game_time_ms() + 900;
        if (((unsigned short)a[4] & (1u << zm_net_self())) &&
            (shotgun_here(ROOM_TRAP_ROOM) || shotgun_here(ROOM_LIVING_ROOM) || shotgun_here(ROOM_TRAP_PASSAGE))) s_jump = true;
    }
}

void zm_shotgun_room(void)
{
    if (shotgun_here(ROOM_LIVING_ROOM)) {
        bool mounted = s_placed && Flg_ck((int)g_roomItemsFlags, 1);
        // The room has both native models already: 0 is the working shotgun,
        // 1 is the broken replacement. Show the model matching the plate item.
        if (g_item_model_table[0]) ((unsigned char*)g_item_model_table[0])[0] =
            (Flg_ck((int)g_roomItemsFlags, 0) || (mounted && s_placed == 2)) ? 1 : 0;
        if (g_item_model_table[1]) ((unsigned char*)g_item_model_table[1])[0] = mounted && s_placed == 1 ? 1 : 0;
        // Returning either shotgun resets the slab and leaves a real, exclusive
        // pickup on the plate. Lifting it again can start a fresh trap cycle.
        unsigned char* pickup = g_RoomActionTable + 2 * 12;
        unsigned char* item = *(unsigned char**)(pickup + 8);
        pickup[0] = 0;
        if (mounted && item) {
            *(unsigned short*)(item + 0) = 9820; *(unsigned short*)(item + 2) = 4900;
            *(unsigned short*)(item + 4) = 2980; *(unsigned short*)(item + 6) = 2450;
            item[8] = s_placed == 2 ? ITEM_SHOTGUN : ITEM_BROKEN_SHOTGUN;
            item[9] = s_plateQty; item[10] = s_placed == 2 ? 0 : 1; item[0x14] = 1;
            // 0x80 routes through the Action press. Without it the normal
            // position probe opens this pickup every frame, even after NO.
            pickup[0] = 4; pickup[1] = 0x81;
        }
        g_RoomActionTable[5*12] = g_RoomActionTable[6*12] = g_RoomActionTable[7*12] = 0;
        g_RoomActionTable[0] = 1;
        unsigned char* rec = *(unsigned char**)(g_RoomActionTable + 8);
        if (rec) rec[0x0C] = 0;
    }
    if (shotgun_here(ROOM_TRAP_PASSAGE) && g_omodel_table[0])
        ((unsigned char*)g_omodel_table[0])[0] = s_broken ? 0 : 0x0F;
    if (shotgun_here(ROOM_TRAP_ROOM)) {
        g_RoomActionTable[0] = g_RoomActionTable[12] = 1;
        for (int slot = 0; slot < 2; slot++) {
            unsigned char* rec = *(unsigned char**)(g_RoomActionTable + slot*12 + 8);
            if (rec) rec[0x0C] = 0;
        }
        if (g_omodel_table[0]) {
            unsigned int depth = shotgun_ceiling_depth();
            unsigned int crushElapsed = s_blocked ? zm_game_time_ms() - s_crushAt : 0;
            if (crushElapsed > 3000) crushElapsed = 3000;
            // Keep descending through death until the slab reaches the floor.
            // A disarmed trap returns at the normal descent speed.
            *(int*)((unsigned char*)g_omodel_table[0] + 0x38) = s_blocked ?
                ZM_CEILING_KILL_Y + (int)(crushElapsed * (unsigned)(ZM_CEILING_FLOOR_Y - ZM_CEILING_KILL_Y) / 3000u) :
                ZM_CEILING_TOP_Y + (int)(depth * (unsigned)(ZM_CEILING_KILL_Y - ZM_CEILING_TOP_Y) / ZM_SHOTGUN_MS);
        }
    }
}

bool zm_shotgun_hide_room(void)
{
    unsigned int now = zm_game_time_ms();
    return zombie_mode_armed() && shotgun_here(ROOM_TRAP_ROOM) &&
        (s_blocked || (s_active && (int)(s_deadline-now) <= 1800));
}

bool zombie_mode_shotgun_corpse(const Entity* e)
{
    return zombie_mode_armed() && shotgun_here(ROOM_TRAP_ROOM) && s_blocked &&
        e && e->id < NPC_ENTITIES_IDS && e->health < 0;
}

static void shotgun_audio(bool enable)
{
    int wanted = enable && (s_active || (!s_blocked && s_ceilingDepth) || (s_blocked && zm_game_time_ms()-s_crushAt < 3000)) ? shotgun_here(ROOM_TRAP_ROOM) ? 1 : shotgun_here(ROOM_TRAP_PASSAGE) ? 2 : 0 : 0;
    if (wanted == s_soundPlaying) return;
    if (s_soundPlaying == 1 && s_chainBank) setSndStop(s_chainBank);
    if (s_soundPlaying == 2 && s_muffledBank) setSndStop(s_muffledBank);
    s_soundPlaying = wanted;
    if (!wanted) return;
    int& bank = wanted == 1 ? s_chainBank : s_muffledBank;
    if (!bank) bank = loadSndBankFromWav(GAME_DATA_ROOT "sound\\chain1.wav", wanted == 2);
    if (bank) { set_volume(bank, g_SfxVolume - (wanted == 2 ? 1800 : 0)); playSnd(bank, 1); }
}

bool zm_shotgun_draw(void)
{
    unsigned int now = zm_game_time_ms();
    if (!shotgun_here(ROOM_TRAP_PASSAGE) || !s_fadeAt || (int)(s_fadeAt - now) <= 0) return false;
    int delta = (int)(s_fadeAt - now), alpha = delta > 700 ? (900-delta)*255/200 : delta < 250 ? delta*255/250 : 255;
    if (alpha < 0) alpha = 0; if (alpha > 255) alpha = 255;
    g_rect.textureId = 0; g_rect.x = -160; g_rect.y = -120; g_rect.w = 320; g_rect.h = 240;
    g_rect.textureId = 0x60000000u;
    g_rect.r = g_rect.g = g_rect.b = (unsigned char)alpha;
    draw_rect(&g_rect, 0, 0);
    return true;
}

// ROOM6090's corridor runs along x=6500; the old x=8100..8800 points
// were inside the east wall and collision pushed everyone onto the same spot.
// Face the rescuer diagonally down the passage toward the double doors.
// The two rescued players stand ahead of him and turn back toward him.
static void shotgun_arrival(int player, int* x, int* z, short* angle)
{
    int rank = 0;
    if (player != s_breaker) {
        rank = 1;
        for (int i = 1; i < player; i++)
            if (i != s_breaker && (s_rescuedMask & (1u << i))) rank++;
    }
    if (rank > 2) rank = 2;
    static const short positions[3][3] = {
        {7000, 9400, 0xA00},
        {5800, 10300, 0x1A0},
        {6500, 11800, 0x37A}
    };
    *x = positions[rank][0]; *z = positions[rank][1];
    *angle = positions[rank][2];
}

void zm_shotgun_frame(void)
{
    if (!zombie_mode_armed() || zombie_mode_match_over()) { shotgun_audio(false); shotgun_rescue_voice(false, false); zm_shotgun_crush_view(false); return; }
    unsigned int now = zm_game_time_ms();
    shotgun_ceiling_depth();
    if (zm_net_role() == ZM_NET_ZOMBIE) {
        shotgun_start(); shotgun_finish();
        if (s_placed) Flg_on((int)g_roomItemsFlags, 1);
        if (!s_stateAt || now - s_stateAt >= 500) { shotgun_send(0, 0); s_stateAt = now ? now : 1; }
        if (s_checkProgression && (int)(now - s_checkAt) >= 0) {
            int solvable = zm_random_remaining_solvable();
            if (solvable >= 0) {
                s_checkAt = now + 1000;
                if (solvable != s_lastProof) dbg_printf("[shotgun] remaining escape route: %s\n", solvable ? "solvable" : "impossible");
                s_lastProof = solvable; if (!solvable) zm_match_progression_lost();
            }
        }
    }
    if (s_request && (!s_requestAt || now-s_requestAt >= 500)) {
        unsigned int seed = zm_net_seed();
        zm_net_send_event_to(ZM_NET_DIRECTOR, ZM_EV_SHOTGUN, (short)s_request,
            s_request == 1 ? s_requestItem : 0, s_request == 1 ? (short)s_requestSerial : 0, 0, 0, 0, (short)seed, (short)(seed >> 16));
        s_requestAt = now ? now : 1;
    }
    bool ready = !g_openMenuFlag && !(g_main_state_flags & (MSF_MENU_ACTIVE | MSF_ROOM_TRANSITION)) && !g_roomTransitionBusy;
    shotgun_rescue_voice(true, ready);
    if (s_consume && ready) shotgun_remove_broken();
    if (s_consumeAxe && ready) {
        int slot = get_item_slot(ITEM_PICK_AXE);
        if (slot >= 0) {
            unsigned char* item = (unsigned char*)g_ItemSlotsPointer + slot*2;
            if (g_EquippedItemId == slot + 1) g_EquippedItemId = 0;
            item[0] = item[1] = 0; rearrange_item_slots();
        }
        s_consumedAxe = true; s_consumeAxe = false;
    }
    if (ready) zm_shotgun_room();
    if (s_broken) {
        zm_world_shotgun_seal();
        if (ready) zm_shotgun_director_crush();
    }
    // Every copy in the room, including the director and spectators, keeps the
    // low camera throughout descent and retraction. The normal death path carries
    // each survivor's scream to the other copies without replaying it here.
    if (ready) {
        zm_shotgun_crush_view(shotgun_here(ROOM_TRAP_ROOM) &&
            (s_blocked || s_active || s_ceilingDepth != 0));
        if (s_blocked && shotgun_here(ROOM_TRAP_ROOM)) {
            zm_world_shotgun_crush();
            if (now-s_crushAt >= 1000) zm_shotgun_director_crush();
        }
    }
    if (s_arrivalCamera && ready && shotgun_here(ROOM_TRAP_PASSAGE)) {
        // Synthetic doors start at camera zero, which watches the L Passage.
        // Select coverage at the rescued player's actual position before input.
        zm_fix_camera_at((const int*)g_playerEntity.scaMatrixData.localMatrix.t,
                         ZM_NET_MAX_PLAYERS * 10 + zm_net_self());
        s_arrivalCamera = false;
    }
    if (s_lookUntil && ((int)(s_lookUntil-now) <= 0 || !s_active || !shotgun_here(ROOM_TRAP_ROOM) ||
        g_playerEntity.health < 0 || g_playerEntity.isBeingAttackedFlag)) {
        g_playerEntity.lookAtFlags = 0; s_lookUntil = 0;
    }
    shotgun_audio(true);
    if (s_active && shotgun_here(ROOM_TRAP_ROOM) && zm_game_role() == ZM_NET_SURVIVOR && (!s_noteAt || now-s_noteAt >= 1000)) {
        char line[48]; unsigned int left = (int)(s_deadline-now) > 0 ? s_deadline-now : 0;
        snprintf(line,sizeof(line),"CEILING TRAP - %u S",(left+999)/1000); zm_note(line); s_noteAt = now ? now : 1;
    }
    if (zm_game_role() == ZM_NET_SURVIVOR && zm_shotgun_crushed(zm_net_self())) {
        zm_shotgun_clear_inventory(); g_playerEntity.health = -1; s_consume = s_consumeAxe = false;
    }
    if (g_playerEntity.health < 0 && zm_game_role() == ZM_NET_SURVIVOR) { s_request = 0; s_jump = false; }
    if (s_jump && ready && g_playerEntity.health >= 0 && !g_playerEntity.isBeingAttackedFlag && !zombie_mode_pickup_waiting()) {
        // Start the rescue fade after inventory cleanup, so network latency or
        // time spent in the menu cannot use up the fade before the room changes.
        s_fadeAt = zm_game_time_ms() + 900;
        memset(s_jumpRecord,0,sizeof(s_jumpRecord)); s_jumpRecord[0x0D]=ROOM_TRAP_PASSAGE;
        s_jumpRecord[0x0B]=0x40; s_jumpRecord[0x16]=0xFF;
        int arrivalX, arrivalZ; short arrivalAngle;
        shotgun_arrival(zm_net_self(), &arrivalX, &arrivalZ, &arrivalAngle);
        *(short*)(s_jumpRecord+0x0E)=(short)arrivalX;
        *(short*)(s_jumpRecord+0x12)=(short)arrivalZ; *(short*)(s_jumpRecord+0x14)=arrivalAngle;
        g_pendingDoorRecord=(int)s_jumpRecord; g_main_state_flags|=MSF_GAMEPLAY_ACTIVE; g_message_flags=0;
        s_arrivalCamera = true;
        g_rect.textureId=0; g_rect.x=-160;g_rect.y=-120;g_rect.w=320;g_rect.h=240;
        g_rect.r=g_rect.g=g_rect.b=0;
        g_openMenuFlag=1;draw_rect(&g_rect,0,0);Task_sleep(1);StMask(0,0);s_jump=false;
    }
}

void zm_shotgun_export(unsigned int out[16])
{
    out[0]=s_placed | ((unsigned int)s_plateQty<<2); out[1]=(s_broken?1u:0u) | (s_blocked?2u:0u);
    out[2]=s_active?s_remaining:ZM_SHOTGUN_MS+1;out[3]=(unsigned int)(s_placer+1);
    out[4]=s_rescuedMask;out[5]=s_crushedMask|(s_checkProgression?16u:0u);
    out[6]=s_serial;out[7]=(unsigned int)(s_breaker+1);
    for(int i=0;i<4;i++){out[8+i]=s_receipt[i];out[12+i]=s_receiptItem[i];}
}
void zm_shotgun_import(const unsigned int in[16])
{
    s_placed=(unsigned char)(in[0]&3);s_plateQty=(unsigned char)(in[0]>>2);
    s_broken=(in[1]&1)!=0;s_blocked=(in[1]&2)!=0;s_crushAt=zm_game_time_ms();
    s_remaining=in[2]<=ZM_SHOTGUN_MS?in[2]:0;
    s_placer=(int)in[3]-1;s_active=in[2]<=ZM_SHOTGUN_MS&&!s_broken&&!s_blocked;
    s_deadline=zm_game_time_ms()+s_remaining;s_rescuedMask=in[4];
    s_crushedMask=in[5]&15u;s_checkProgression=(in[5]&16u)!=0;s_checkAt=zm_game_time_ms()+2000;
    s_serial=in[6];s_breaker=(int)in[7]-1;
    for(int i=0;i<4;i++){s_receipt[i]=in[8+i];s_receiptItem[i]=in[12+i];}
    int self=zm_net_self();s_consumedReplacement=self>0?(unsigned short)s_receipt[self]:0;
    s_consumedAxe=s_breaker==self;
}

static void shotgun_checkpoint_remove(ZmReconnectPlayer* p,unsigned char id)
{
    for(int i=0;i<8;i++) if(p->inventory[i*2]==id) {
        if(p->card.equippedItemId==i+1)p->card.equippedItemId=0;
        p->inventory[i*2]=p->inventory[i*2+1]=0;p->inventoryMask&=~(1u<<p->indices[i]);
        int count=0;
        for(int j=0;j<8;j++)if(p->inventory[j*2]){
            if(p->card.equippedItemId==j+1)p->card.equippedItemId=(unsigned char)(count+1);
            p->inventory[count*2]=p->inventory[j*2];p->inventory[count*2+1]=p->inventory[j*2+1];p->indices[count++]=p->indices[j];
        }
        memset(p->inventory+count*2,0,16-count*2);p->card.totalHeldItems=(unsigned char)count;break;
    }
}

void zm_shotgun_reconcile(int player,ZmReconnectPlayer* p)
{
    if(player<1||player>=ZM_NET_MAX_PLAYERS)return;
    if(zm_shotgun_crushed(player)){
        p->health=-1;memset(p->inventory,0,sizeof(p->inventory));memset(p->indices,0,sizeof(p->indices));
        p->inventoryMask=0;p->card.totalHeldItems=p->card.equippedItemId=0;return;
    }
    if(s_broken&&(s_rescuedMask&(1u<<player))&&p->health>=0&&p->card.stageId==STAGE_MANSION_RETURN_1F&&
       (p->card.roomId==ROOM_TRAP_ROOM||p->card.roomId==ROOM_LIVING_ROOM)){
        p->card.roomId=ROOM_TRAP_PASSAGE;p->card.roomCameraId=0;
        shotgun_arrival(player, &p->x, &p->z, &p->angle);p->y=0;
    }
    if(s_receipt[player]&&p->shotgunReplacement!=s_receipt[player]){
        shotgun_checkpoint_remove(p,(unsigned char)s_receiptItem[player]);p->shotgunReplacement=(unsigned short)s_receipt[player];
    }
    if(s_breaker==player&&!p->pickaxeSpent){shotgun_checkpoint_remove(p,ITEM_PICK_AXE);p->pickaxeSpent=1;}
}
