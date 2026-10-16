#include "ZombieModeInternal.h"
#include "ZombieNet.h"
#include "../entities/EntityCommon.h"
#include "../../DebugPrint.h"
#include "../../platform/platform.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

extern void MovePlayerXZ(int angle, SVECTOR* offset, SVECTOR* out); // WeaponDamage.cpp
extern unsigned int Flg_ck(int baseAddr, unsigned int bitIndex);  // 0x00473f40 CmdFunctions.cpp

// Port-added doorway interceptions. One outstanding purchase and one global
// 60-second cooldown. The host owns payment/roster; the loaded room's owner
// owns collision validation and the one-second warning. Rejected or timed-out
// requests spend neither points nor cooldown. No generic survivor-entry stun.
#define ZM_REINFORCE_COOL_MS 60000
#define ZM_REINFORCE_WARN_MS 1000
#define ZM_REINFORCE_ENTRY_MS 1000
#define ZM_REINFORCE_TIMEOUT_MS 5000

enum { RF_REQUEST, RF_READY, RF_REFUSED, RF_WARNING, RF_ENTRANCE };
// args: {op|id<<8, stage|room<<8, token/uid, x, y, z, angle, door}.
struct ZmReinforcement {
    bool active;
    unsigned char stage, room, id;
    unsigned short token;
    int owner, door;
    short x, y, z, angle;
    unsigned int at;
};
static ZmReinforcement s_buy, s_warning;
static unsigned int s_readyAt;
static unsigned short s_sequence;
static struct {
    unsigned short uid;
    unsigned char stage, room;
    unsigned int until;
} s_entry;

// The buyer's notices: the director's screen only (the AI director's host
// plays a survivor, who must not see them).
static void rf_buyer_note(const char* text)
{
    if (zm_game_role() != ZM_NET_SURVIVOR) zm_reinforce_notice(text);
    else dbg_printf("[reinforce] %s\n", text);
}

void zm_reinforce_reset(void)
{
    memset(&s_buy, 0, sizeof(s_buy));
    memset(&s_warning, 0, sizeof(s_warning));
    memset(&s_entry, 0, sizeof(s_entry));
    s_readyAt = 0;
    s_sequence = 0;
}

static bool rf_type(unsigned char id)
{
    return id == ENEMY_ZOMBIE || id == ENEMY_HUNTER;
}

static void rf_send(int dst, int op, const ZmReinforcement& r)
{
    short a[8] = { (short)(op | (r.id << 8)), (short)(r.stage | (r.room << 8)),
                   (short)r.token, r.x, r.y, r.z, r.angle, (short)r.door };
    if (dst == zm_net_self()) zm_reinforce_take(a, zm_net_self());
    else zm_net_send_event_to(dst, ZM_EV_REINFORCE, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
}

// Current positions include the local player's live position, not its last
// relayed STATE. Corpse/spectator/door-animation bodies are not spawn targets.
static bool rf_survivor(int player, unsigned char stage, unsigned char room,
                        int* x, int* y, int* z, int* entrance)
{
    if (player == zm_net_self() && zm_game_role() == ZM_NET_SURVIVOR) {
        if (g_stageId != stage || g_roomId != room || g_playerEntity.health < 0 || zm_spec_away()) return false;
        *x = g_playerEntity.scaMatrixData.localMatrix.t[0];
        *y = g_playerEntity.scaMatrixData.localMatrix.t[1];
        *z = g_playerEntity.scaMatrixData.localMatrix.t[2];
        *entrance = zm_survivor_entrance_door();
        return *x >= 0 && *x <= 32767 && *z >= 0 && *z <= 32767;
    }
    if (zm_net_char(player) < 0) return false;
    const ZmNetPeerState* p = zm_net_player(player);
    if (p == NULL || p->dead || p->spectating || p->transitioning || p->stage != stage || p->room != room) return false;
    *x = p->x; *y = p->y; *z = p->z; *entrance = p->entranceDoor;
    return *x >= 0 && *x <= 32767 && *z >= 0 && *z <= 32767;
}

static bool rf_clear(int x, int y, int z)
{
    if (x < 500 || z < 500 || x > 32267 || z > 32267) return false;
    static const short probes[5][2] = { {0,0}, {400,0}, {-400,0}, {0,400}, {0,-400} };
    VECTOR saved = g_playerPosScratch;
    bool clear = true;
    for (int i = 0; i < 5; i++) {
        VECTOR point = { x + probes[i][0], y, z + probes[i][1], 0 };
        SVECTOR zero = {0,0,0,0};
        if (room_collision_check_0047da50(&point, (VECTOR*)&zero) == 1) { clear = false; break; }
    }
    g_playerPosScratch = saved;
    if (!clear) return false;
    for (int player = 1; player < ZM_NET_MAX_PLAYERS; player++) {
        int sx, sy, sz, entrance;
        if (!rf_survivor(player, g_stageId, g_roomId, &sx, &sy, &sz, &entrance)) continue;
        int dx = sx - x, dz = sz - z;
        if (abs(sy - y) < 1200 && abs(dx) < 1000 && abs(dz) < 1000 && dx * dx + dz * dz < 1000 * 1000) return false;
    }
    for (int i = 0; i < ZM_FIRST_SURVIVOR_SLOT; i++) {
        const Entity& e = g_EnemiesList[i];
        if (!(e.status_flags & ENTITY_STATUS_ACTIVE) || e.health < 0) continue;
        int dx = e.scaMatrixData.localMatrix.t[0] - x, dz = e.scaMatrixData.localMatrix.t[2] - z;
        if (abs(e.scaMatrixData.localMatrix.t[1] - y) < 1200 && abs(dx) < 900 && abs(dz) < 900 &&
            dx * dx + dz * dz < 900 * 900) return false;
    }
    // Leave existing pickup interaction zones clear, including new/death drops.
    for (int i = 0; i < ROOM_ACTION_ENTRIES; i++) {
        const unsigned char* action = &g_RoomActionTable[i * 12];
        if (action[0] != 4 && action[0] != 0x0D && action[0] != 0x0F) continue;
        if (!Flg_ck((int)g_roomItemsFlags, *(const unsigned short*)(action + 6))) continue;
        const unsigned char* rec = *(const unsigned char* const*)(action + 8);
        if (rec == NULL) continue;
        int left = *(const short*)(rec), top = *(const short*)(rec + 2);
        int right = left + *(const unsigned short*)(rec + 4), bottom = top + *(const unsigned short*)(rec + 6);
        if (x > left - 400 && x < right + 400 && z > top - 400 && z < bottom + 400) return false;
    }
    return true;
}

static bool rf_capacity(unsigned char id)
{
    if (zm_shotgun_room_blocked(g_stageId, g_roomId)) return false;
    if (zm_room_monster_slots(g_stageId, g_roomId) + zm_econ_monster_slots(id) > zm_econ_room_cap(g_stageId, g_roomId)) return false;
    for (int i = zm_room_highest_script_slot() + 1; i < ZM_FIRST_SURVIVOR_SLOT; i++)
        if (!(g_EnemiesList[i].status_flags & ENTITY_STATUS_ACTIVE)) return true;
    return false;
}

// A leaf room: every real door leads to the same neighbouring room. There
// the reinforcement may come through the door the survivors came in by -
// it is the only way in.
static bool rf_leaf(unsigned char stage, unsigned char room)
{
    const ZmDoor* doors;
    int count = zm_room_doors(stage, room, &doors);
    int neighbour = -1;
    for (int i = 0; i < count; i++) {
        if (doors[i].flags0B & 0x80) continue;
        unsigned char ns, nr;
        zm_decode_dest(doors[i].dest, stage, &ns, &nr);
        if ((ns == stage && nr == room) || zm_random_room_stub(ns, nr)) continue;
        int other = ns << 8 | nr;
        if (neighbour >= 0 && neighbour != other) return false;
        neighbour = other;
    }
    return neighbour >= 0;
}

// A survivor counts for a door unless it came in by it (any door in a leaf room).
static bool rf_entry_ok(int entrance, int door, bool leaf)
{
    return leaf || entrance != door;
}

// Pick the closest eligible door/survivor pair. A reciprocal door's arrival
// supplies a point on the correct side/floor and a facing into this room.
static bool rf_choose(ZmReinforcement* r)
{
    const ZmDoor* cached;
    int count = zm_room_doors(r->stage, r->room, &cached);
    bool leaf = rf_leaf(r->stage, r->room);
    ZmDoor doors[ZM_MAX_DOORS];
    memcpy(doors, cached, sizeof(ZmDoor) * count);
    int best = 0x7FFFFFFF;
    bool found = false;
    for (int door = 0; door < count; door++) {
        const ZmDoor& d = doors[door];
        if (d.flags0B & 0x80) continue;
        unsigned char ns, nr;
        zm_decode_dest(d.dest, r->stage, &ns, &nr);
        if ((ns == r->stage && nr == r->room) || zm_random_room_stub(ns, nr)) continue;
        int cx = (int)d.zoneX + d.zoneW / 2, cz = (int)d.zoneZ + d.zoneD / 2;
        if (cx < 0 || cz < 0 || cx > 32767 || cz > 32767) continue;
        const ZmDoor* back;
        int nb = zm_room_doors(ns, nr, &back);
        // Prefer the reciprocal arrival nearest this particular door, rather
        // than another door connecting the same two rooms.
        int arrival = -1, proximity = 0x7FFFFFFF;
        for (int k = 0; k < nb; k++) {
            unsigned char bs, br;
            zm_decode_dest(back[k].dest, ns, &bs, &br);
            if (bs != r->stage || br != r->room || (back[k].flags0B & 0x80)) continue;
            int distance = abs((unsigned short)back[k].arriveX - cx) + abs((unsigned short)back[k].arriveZ - cz);
            if (distance < proximity) { proximity = distance; arrival = k; }
        }
        if (arrival < 0) continue;
        const ZmDoor& entry = back[arrival];
        int nearest = best;
        bool eligible = false;
        for (int player = 1; player < ZM_NET_MAX_PLAYERS; player++) {
            int x, y, z, entrance;
            if (!rf_survivor(player, r->stage, r->room, &x, &y, &z, &entrance) ||
                !rf_entry_ok(entrance, door, leaf) || abs(y - entry.arriveY) >= 1200) continue;
            int dx = x - cx, dz = z - cz;
            int distance = dx * dx + dz * dz; // bounded 0..32767 coordinates
            if (distance < nearest) { nearest = distance; eligible = true; }
        }
        if (!eligible) continue;
        static const short offsets[] = { 400, 200, 600, 0 };
        for (int i = 0; i < 4; i++) {
            SVECTOR offset = { offsets[i], 0, 0, 0 };
            MovePlayerXZ(entry.arriveAngle, &offset, &offset);
            int x = (unsigned short)entry.arriveX + offset.x;
            int z = (unsigned short)entry.arriveZ + offset.z;
            if (!rf_clear(x, entry.arriveY, z)) continue;
            best = nearest;
            r->x = (short)x; r->y = entry.arriveY; r->z = (short)z;
            r->angle = entry.arriveAngle; r->door = door;
            found = true;
            break;
        }
    }
    return found;
}

bool zm_reinforce_request(unsigned char stage, unsigned char room, unsigned char id, char* why, int whyLen)
{
    if (zm_net_role() != ZM_NET_ZOMBIE || !rf_type(id)) {
        snprintf(why, whyLen, "REINFORCE WITH A ZOMBIE OR HUNTER"); return false;
    }
    if (s_buy.active) { snprintf(why, whyLen, "REINFORCEMENT PENDING"); return false; }
    int left = (int)(s_readyAt - zm_game_time_ms());
    if (s_readyAt && left > 0) {
        snprintf(why, whyLen, "REINFORCEMENT READY IN %d S", (left + 999) / 1000); return false;
    }
    const char* name = id == ENEMY_HUNTER ? "HUNTER" : "ZOMBIE";
    if (!zm_econ_can_place(stage, room, id, name, why, whyLen)) return false;
    int owner = -1;
    if (stage == g_stageId && room == g_roomId) owner = zm_room_owner_here();
    else for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
        const ZmNetPeerState* p = zm_net_player(i);
        if (p && !p->spectating && !p->transitioning && p->stage == stage && p->room == room) { owner = i; break; }
    }
    if (owner < 0) { snprintf(why, whyLen, "NO ROOM OWNER AVAILABLE"); return false; }
    memset(&s_buy, 0, sizeof(s_buy));
    s_buy.active = true; s_buy.stage = stage; s_buy.room = room; s_buy.id = id;
    if (++s_sequence == 0) ++s_sequence;
    s_buy.token = s_sequence; s_buy.owner = owner; s_buy.at = zm_game_time_ms();
    rf_send(owner, RF_REQUEST, s_buy);
    if (!s_buy.active) { snprintf(why, whyLen, "NO SAFE REINFORCEMENT DOOR"); return false; }
    snprintf(why, whyLen, "CHECKING REINFORCEMENT DOOR");
    return true;
}

void zm_reinforce_take(const short* a, int src)
{
    int op = (unsigned short)a[0] & 255;
    ZmReinforcement r = {};
    r.id = (unsigned char)((unsigned short)a[0] >> 8);
    r.stage = (unsigned char)a[1]; r.room = (unsigned char)((unsigned short)a[1] >> 8);
    r.token = (unsigned short)a[2];
    r.x = a[3]; r.y = a[4]; r.z = a[5]; r.angle = a[6]; r.door = a[7];
    if (!rf_type(r.id) || zombie_mode_match_over()) return;
    if (op == RF_REQUEST && src == ZM_NET_DIRECTOR) {
        if (r.stage != g_stageId || r.room != g_roomId || zm_room_owner_here() != zm_net_self() ||
            zm_access_room_elevator(r.stage, r.room) || zm_random_room_safe(r.stage, r.room) || zm_yawn_director_closed(r.stage, r.room) ||
            !rf_capacity(r.id) || !rf_choose(&r)) {
            rf_send(ZM_NET_DIRECTOR, RF_REFUSED, r); return;
        }
        r.active = true; r.at = zm_game_time_ms(); s_warning = r;
        rf_send(ZM_NET_ALL, RF_WARNING, r);
        if (zm_game_role() == ZM_NET_SURVIVOR) {
            zm_reinforce_notice("REINFORCEMENT AT A DOOR");
            zombie_mode_room_entry_sound();
        }
        dbg_printf("[reinforce] warning token %u room %d/%02X door %d at %d/%d/%d\n",
                   r.token, r.stage, r.room, r.door, r.x, r.y, r.z);
    } else if (op == RF_WARNING && r.stage == g_stageId && r.room == g_roomId && src == zm_room_owner_here()) {
        if (zm_game_role() == ZM_NET_SURVIVOR) {
            zm_reinforce_notice("REINFORCEMENT AT A DOOR");
            zombie_mode_room_entry_sound();
        }
    } else if ((op == RF_READY || op == RF_REFUSED) && zm_net_role() == ZM_NET_ZOMBIE) {
        if (!s_buy.active || src != s_buy.owner || r.token != s_buy.token ||
            r.stage != s_buy.stage || r.room != s_buy.room || r.id != s_buy.id) return;
        s_buy.active = false;
        char why[64];
        if (op == RF_REFUSED) { rf_buyer_note("NO SAFE REINFORCEMENT DOOR"); return; }
        bool occupied = false, leaf = rf_leaf(r.stage, r.room);
        for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
            int x, y, z, entrance;
            if (rf_survivor(i, r.stage, r.room, &x, &y, &z, &entrance) && rf_entry_ok(entrance, r.door, leaf)) occupied = true;
        }
        const ZmNetPeerState* runner = src == zm_net_self() ? NULL : zm_net_player(src);
        bool ownerHere = src == zm_net_self() ?
            (g_stageId == r.stage && g_roomId == r.room && zm_room_owner_here() == src) :
            (runner && !runner->transitioning && !runner->spectating && runner->stage == r.stage && runner->room == r.room);
        if (!occupied || !ownerHere) { rf_buyer_note("SURVIVORS LEFT THAT ROOM"); return; }
        if (!zm_econ_can_place(r.stage, r.room, r.id, r.id == ENEMY_HUNTER ? "HUNTER" : "ZOMBIE", why, sizeof(why))) {
            rf_buyer_note(why); return;
        }
        unsigned short uid = zm_world_add_extra(r.stage, r.room, r.id, 0, r.x, r.y, r.z, r.angle);
        if (!uid) { rf_buyer_note("THE ROSTER IS FULL"); return; }
        zm_econ_pay(r.id);
        s_readyAt = zm_game_time_ms() + ZM_REINFORCE_COOL_MS;
        r.token = uid;
        rf_send(ZM_NET_ALL, RF_ENTRANCE, r);
        // Broadcasts do not echo to the host.
        short entry[8] = { (short)(RF_ENTRANCE | (r.id << 8)), (short)(r.stage | (r.room << 8)), (short)uid };
        zm_reinforce_take(entry, ZM_NET_DIRECTOR);
        rf_buyer_note("DOOR REINFORCEMENT PURCHASED");
        dbg_printf("[reinforce] bought uid %04X id %02X room %d/%02X door %d\n", uid, r.id, r.stage, r.room, r.door);
    } else if (op == RF_ENTRANCE && src == ZM_NET_DIRECTOR) {
        s_entry.uid = r.token; s_entry.stage = r.stage; s_entry.room = r.room;
        s_entry.until = zm_game_time_ms() + ZM_REINFORCE_ENTRY_MS;
    }
}

void zm_reinforce_frame(void)
{
    if (s_buy.active && zm_game_time_ms() - s_buy.at >= ZM_REINFORCE_TIMEOUT_MS) {
        s_buy.active = false;
        rf_buyer_note("REINFORCEMENT REQUEST TIMED OUT");
    }
    if (!s_warning.active || zm_game_time_ms() - s_warning.at < ZM_REINFORCE_WARN_MS) return;
    ZmReinforcement r = s_warning;
    s_warning.active = false;
    bool eligible = false, leaf = rf_leaf(r.stage, r.room);
    for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
        int x, y, z, entrance;
        if (rf_survivor(i, r.stage, r.room, &x, &y, &z, &entrance) && rf_entry_ok(entrance, r.door, leaf)) eligible = true;
    }
    bool valid = !zombie_mode_match_over() && r.stage == g_stageId && r.room == g_roomId &&
                 zm_room_owner_here() == zm_net_self() && eligible && rf_capacity(r.id) &&
                 rf_clear((unsigned short)r.x, r.y, (unsigned short)r.z);
    rf_send(ZM_NET_DIRECTOR, valid ? RF_READY : RF_REFUSED, r);
}

bool zm_reinforce_hold(const Entity* e)
{
    if (!s_entry.uid || s_entry.stage != g_stageId || s_entry.room != g_roomId ||
        (int)(s_entry.until - zm_game_time_ms()) <= 0) return false;
    int slot = (int)(e - g_EnemiesList);
    return slot >= 0 && slot < ZM_FIRST_SURVIVOR_SLOT && zm_world_uid_of_slot(slot) == s_entry.uid;
}
