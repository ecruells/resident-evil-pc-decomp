#include "ZombieModeInternal.h"
#include "ZombieNet.h"
#include "ZombieReconnect.h"
#include "../entities/EntityCommon.h"
#include "../entities/Zombie.h"
#include "../../DebugPrint.h"
#include <cstring>

extern void Flg_on(int baseAddr, unsigned int bitIndex);              // 0x00473ef0 CmdFunctions.cpp

// ============================================================================
// ZombieWorld.cpp - the zombie mod's persistent world (port-added).
//
// RE1 rebuilds a room from its RDT every time it is entered: the init SCD's
// enemy_set records spawn the monsters, and two things carry over from the
// last visit - the death flags (g_EnemiesFlags) and a 16-slot snapshot of
// where the survivors stood (BuildEnemySnap / FUN_0048f330), which ages out
// after five room changes and does not keep health.
//
// The roster is that snapshot made permanent and complete: every monster of
// every visited room, with its position, facing, behaviour byte, health and
// whether it is dead. Captured when a room is left, passed to the other copy
// so both sides agree where the monsters are, and put back on entry.
//
// Two kinds, told apart by the entry's uid:
//   SCRIPT (uid < 16)   - one of the room's own enemy_set records, by its
//                         spawn slot. cmd_enemy_set still creates it; the
//                         roster only moves it, re-heals it, or keeps a dead
//                         one away. Both the snapshotted records and the
//                         "unconditional" ones (+0x04 != 0) the original
//                         re-spawns fresh on every visit.
//   EXTRA  (uid >= 0x100) - anything the room's script does not create: the
//                         director's own body left behind, and later the
//                         monsters the director places. Spawned by the mod
//                         on entry (zm_world_spawn_extras), any enemy type.
// Monsters with neither (a plant's clones, the possessed body while it is
// being carried through a door, the survivor's-copy puppet) are not kept.
// ============================================================================

struct ZmRosterEntry {
    unsigned char  used;
    unsigned char  stage, room, id;
    unsigned short uid;
    unsigned char  alive;
    unsigned char  behavior;
    bool           feeding;
    bool           departed;        // carried through a door, not killed
    bool           directorControlled; // never relinquished by a room owner or timer
    short          x, y, z;          // x / z are stored as the low 16 bits (rooms are 0..65535)
    short          angle;
    short          health;
};

#define ZM_ROSTER_MAX 1024
static ZmRosterEntry s_roster[ZM_ROSTER_MAX];

// Uids for EXTRA monsters. Each copy mints its own range so the two never hand
// out the same one: the director's copy (and single player) from 0x100, the
// survivor's copy from 0x4000. 15 bits: bit 15 carries "alive" on the wire.
#define ZM_UID_EXTRA_FIRST     0x0100
#define ZM_UID_EXTRA_SURVIVOR  0x4000
#define ZM_UID_MAX             0x7FFF
static unsigned short s_nextUid = ZM_UID_EXTRA_FIRST;

// What each enemy slot of the loaded room is, for the capture: its uid, or
// ZM_UID_NONE (not kept), or ZM_UID_NEW (kept, uid minted when first captured).
#define ZM_UID_NONE 0xFFFF
#define ZM_UID_NEW  0xFFFE
static unsigned short s_slotUid[30];

// Health to re-apply once a restored monster's own init has run (each type's
// init rolls a fresh health), by enemy slot.
static bool  s_pendingHealth[30];
static short s_pendingHealthValue[30];
static bool s_pendingFeeding[30];

// Made by zm_spawn_monster and its init not run yet: a naked zombie's health
// is raised by a quarter once it has rolled it (the director pays more for one).
static bool  s_freshInit[30];

static bool s_worldOn = false;

bool zm_world_shotgun_clear(unsigned char stage, unsigned char room, int x, int z)
{
    bool loaded = g_stageId == stage && g_roomId == room;
    // A hidden entity (the human director's body stand-in while it is
    // elsewhere - every survivor copy keeps one, unused in an AI director's
    // game) and anything that is no monster are not in the way.
    if (loaded) for (int i = 0; i < ZM_FIRST_SURVIVOR_SLOT; i++) {
        const Entity& e = g_EnemiesList[i];
        if ((e.status_flags & ENTITY_STATUS_ACTIVE) == 0 || e.health < 0 || e.id >= NPC_ENTITIES_IDS) continue;
        if (zombie_mode_hide_entity(&e)) continue;
        if (zm_shotgun_monster_blocks(room, e.scaMatrixData.localMatrix.t[0],
                e.scaMatrixData.localMatrix.t[2], x, z)) return false;
    }
    for (int i = 0; i < ZM_ROSTER_MAX; i++) {
        const ZmRosterEntry& r = s_roster[i];
        if (!r.used || !r.alive || r.departed || r.stage != stage || r.room != room) continue;
        if (loaded) {
            int slot = zm_world_slot_of_uid(r.uid);
            if (slot >= 0 && ((g_EnemiesList[slot].status_flags & ENTITY_STATUS_ACTIVE) ||
                             g_EnemiesList[slot].health < 0)) continue;
        }
        if (zm_shotgun_monster_blocks(room, (unsigned short)r.x, (unsigned short)r.z, x, z)) return false;
    }
    return true;
}

// A completed slab kills monsters as well as survivors, including the
// director's possessed entity. Persist remote-room deaths on the host; local
// entities follow their usual death handlers and ownership/roster capture.
static void zm_world_shotgun_kill(unsigned char room)
{
    if (g_stageId == STAGE_MANSION_RETURN_1F && g_roomId == room) for (int i = 0; i < ZM_FIRST_SURVIVOR_SLOT; i++) {
        Entity& e = g_EnemiesList[i];
        if ((e.status_flags & ENTITY_STATUS_ACTIVE) && e.health >= 0) {
            e.health = -1;
            e.status_flags |= 0x0E; // dead, intangible, no collision
            e.state = 3; e.action_state = 3; // settled corpse, no death sound
        }
    }
    if (zm_net_role() == ZM_NET_ZOMBIE) for (int i = 0; i < ZM_ROSTER_MAX; i++) {
        ZmRosterEntry& r = s_roster[i];
        if (r.used && r.alive && !r.departed && r.stage == STAGE_MANSION_RETURN_1F && r.room == room) {
            r.alive = false; r.health = -1;
            zm_econ_refund(r.id, r.uid);
        }
    }
}
void zm_world_shotgun_crush(void) { zm_world_shotgun_kill(ROOM_TRAP_ROOM); }
void zm_world_shotgun_seal(void) { zm_world_shotgun_kill(ROOM_LIVING_ROOM); }

unsigned short zm_world_reconnect_sequence(void) { return s_nextUid; }
int zm_world_reconnect_export(void* out, int capacity)
{
    if (capacity < (int)sizeof(s_roster)) return 0;
    if (zm_room_owner_here() == zm_net_self()) {
        for (int i = 0; i < ZM_ROSTER_MAX; i++) {
            ZmRosterEntry& r = s_roster[i];
            if (!r.used || r.departed || r.stage != g_stageId || r.room != g_roomId) continue;
            int slot = zm_world_slot_of_uid(r.uid);
            if (slot < 0 || slot >= ZM_FIRST_SURVIVOR_SLOT) continue;
            const Entity& e = g_EnemiesList[slot];
            if (!(e.status_flags & ENTITY_STATUS_ACTIVE) || e.id != r.id) continue;
            r.health = e.health; r.alive = !zm_monster_dead(&e);
            r.x = (short)e.scaMatrixData.localMatrix.t[0]; r.y = (short)e.scaMatrixData.localMatrix.t[1];
            r.z = (short)e.scaMatrixData.localMatrix.t[2]; r.angle = e.angle;
            r.behavior = e.behavior_flags; r.feeding = r.alive && e.state == ZOMBIE_STATE_EATING;
        }
    }
    memcpy(out, s_roster, sizeof(s_roster));
    return sizeof(s_roster);
}
bool zm_world_reconnect_import(const void* data, int size, unsigned short nextUid)
{
    if (size != sizeof(s_roster)) return false;
    memcpy(s_roster, data, sizeof(s_roster));
    s_nextUid = nextUid;
    return true;
}

// A placed monster's health before it has ever run: its type's init rolls it.
#define ZM_HEALTH_FRESH 0x7FFF

void zm_world_reset(bool on)
{
    memset(s_roster, 0, sizeof(s_roster));
    memset(s_pendingHealth, 0, sizeof(s_pendingHealth));
    memset(s_pendingFeeding, 0, sizeof(s_pendingFeeding));
    memset(s_freshInit, 0, sizeof(s_freshInit));
    for (int i = 0; i < 30; i++) s_slotUid[i] = ZM_UID_NONE;
    s_nextUid = ZM_UID_EXTRA_FIRST;
    s_worldOn = on;
}

void zm_world_set_on(bool on)
{
    s_worldOn = on;
}

static ZmRosterEntry* zm_roster_find(unsigned char stage, unsigned char room,
                                     unsigned short uid, unsigned char id, bool create)
{
    ZmRosterEntry* freeEntry = NULL;
    for (int i = 0; i < ZM_ROSTER_MAX; i++) {
        ZmRosterEntry* r = &s_roster[i];
        if (!r->used) {
            if (freeEntry == NULL) freeEntry = r;
            continue;
        }
        if (r->stage == stage && r->room == room && r->uid == uid && r->id == id) return r;
    }
    if (!create) return NULL;
    if (freeEntry == NULL) {
        // Full: reuse the slot of a dead EXTRA (it is never spawned again).
        for (int i = 0; i < ZM_ROSTER_MAX && freeEntry == NULL; i++) {
            if (!s_roster[i].alive && !s_roster[i].departed && s_roster[i].uid >= ZM_UID_EXTRA_FIRST) freeEntry = &s_roster[i];
        }
        // Then a departed one: the AI director's unseen walks leave one a
        // room (zm_world_move_extra), from rooms no copy had loaded - no late
        // capture can come back for them.
        for (int i = 0; i < ZM_ROSTER_MAX && freeEntry == NULL; i++) {
            if (s_roster[i].departed && s_roster[i].uid >= ZM_UID_EXTRA_FIRST) freeEntry = &s_roster[i];
        }
        if (freeEntry == NULL) {
            dbg_printf("[world] roster full\n");
            return NULL;
        }
    }
    memset(freeEntry, 0, sizeof(*freeEntry));
    freeEntry->used = 1;
    freeEntry->stage = stage;
    freeEntry->room = room;
    freeEntry->uid = uid;
    freeEntry->id = id;
    return freeEntry;
}

static void zm_roster_send(const ZmRosterEntry* r)
{
    zm_net_send_event8(ZM_EV_ROSTER,
                       (short)(r->stage | (r->room << 8)),
                       (short)(r->uid | (r->alive ? 0x8000 : 0)),
                       r->x, r->y, r->z, r->angle, r->health,
                       (short)(r->behavior | (r->id << 8) | (r->directorControlled ? 0x8000 : 0)));
    zm_net_send_event(ZM_EV_FEED, (short)(r->stage | (r->room << 8)),
        (short)r->uid, r->id, r->alive && r->feeding ? 1 : 0);
}

void zm_world_reconnect_enemy(unsigned char stage, unsigned char room, unsigned short uid,
    unsigned char id, short health, bool alive, int x, int y, int z, short angle)
{
    if (uid == ZM_WORLD_UID_NONE || uid == ZM_WORLD_UID_NEW || uid == ZM_WORLD_UID_BODY) return;
    ZmRosterEntry* r = zm_roster_find(stage, room, uid, id, true);
    if (r == NULL || r->departed) return;
    if (r->alive && !alive && health < 0) zm_econ_refund(id, uid);
    r->alive = alive;
    r->health = health;
    r->x = (short)x; r->y = (short)y; r->z = (short)z; r->angle = angle;
}

// The range is picked at the first mint, not at reset: the network role is
// only settled after the world is reset for a new game.
static unsigned short zm_mint_uid(void)
{
    // The arbiter (the host, the AI director's too) takes the director's range.
    bool survivor = !zm_match_authority();
    unsigned short lo = survivor ? ZM_UID_EXTRA_SURVIVOR : ZM_UID_EXTRA_FIRST;
    unsigned short hi = survivor ? ZM_UID_MAX : (unsigned short)(ZM_UID_EXTRA_SURVIVOR - 1);
    if (s_nextUid < lo || s_nextUid > hi) s_nextUid = lo;
    return s_nextUid++;
}

// The loaded room's slots, as the room is built: cleared on reset, then
// marked by cmd_enemy_set (script), the extras' spawn and zm_world_track.
void zm_world_room_reset(void)
{
    memset(s_pendingHealth, 0, sizeof(s_pendingHealth));
    memset(s_pendingFeeding, 0, sizeof(s_pendingFeeding));
    memset(s_freshInit, 0, sizeof(s_freshInit));
    for (int i = 0; i < 30; i++) s_slotUid[i] = ZM_UID_NONE;
}

// Keep this monster from now on (the director's body, a placed monster).
void zm_world_track(const Entity* e)
{
    int slot = (int)(e - g_EnemiesList);
    if (slot < 0 || slot >= 30) return;
    if (s_slotUid[slot] == ZM_UID_NONE) s_slotUid[slot] = ZM_UID_NEW;
}

// Leaving the loaded room: write down every monster the roster keeps and tell
// the other copy. `skip` is not this room's to keep (the survivor's-copy puppet).
void zm_world_capture_room(const Entity* skip)
{
    if (!s_worldOn) return;
    for (int i = 0; i < 30; i++) {
        Entity* e = &g_EnemiesList[i];
        unsigned short uid = s_slotUid[i];
        if (uid == ZM_UID_NONE || e == skip) continue;
        if ((e->status_flags & ENTITY_STATUS_ACTIVE) == 0 || e->id >= NPC_ENTITIES_IDS) continue;
        // Yawn is the attic's own, back on every visit until its death flag is up.
        if (zm_yawn_entity(e)) continue;
        // The director's body, never captured yet, walking out with it: it is
        // the body in the next room, nothing to keep here.
        if (e == g_zombieModeEntity && uid == ZM_UID_NEW) continue;
        if (uid == ZM_UID_NEW) {
            uid = zm_mint_uid();
            s_slotUid[i] = uid;
        }
        ZmRosterEntry* r = zm_roster_find(g_stageId, g_roomId, uid, e->id, true);
        if (r == NULL) break;
        if (r->departed) continue;
        r->alive = zm_monster_dead(e) ? 0 : 1;
        r->health = e->health;
        // Dead: a negative health says so on the wire, apart from the body
        // carried out below (the director's refund, zm_world_apply_remote).
        if (!r->alive && r->health >= 0) r->health = -1;
        // A kept zombie the director is riding out through the door is not
        // here any more: it goes with the director (as its body in the next
        // room), so it must not also wait here.
        r->departed = e == g_zombieModeEntity && r->alive;
        if (e == g_zombieModeEntity) r->alive = 0;
        r->feeding = e->state == ZOMBIE_STATE_EATING && r->alive;
        r->behavior = e->behavior_flags;
        r->x = (short)e->scaMatrixData.localMatrix.t[0];
        r->y = (short)e->scaMatrixData.localMatrix.t[1];
        r->z = (short)e->scaMatrixData.localMatrix.t[2];
        r->angle = e->angle;
        zm_roster_send(r);
    }
}

// A director can leave while another copy still owns a paired animation.
// Publish only its carried body's departure; never capture that owner's room.
void zm_world_depart_entity(const Entity* e)
{
    if (!s_worldOn || e == NULL || zm_game_role() != ZM_NET_ZOMBIE) return;
    int slot = (int)(e - g_EnemiesList);
    if (slot < 0 || slot >= 30) return;
    unsigned short uid = s_slotUid[slot];
    if (uid == ZM_UID_NONE || uid == ZM_UID_NEW) return;
    ZmRosterEntry* r = zm_roster_find(g_stageId, g_roomId, uid, e->id, false);
    if (r == NULL || r->departed || !r->alive) return;
    r->alive = 0;
    r->departed = true;
    r->feeding = false;
    // A nonnegative health distinguishes a departure from a kill on the wire.
    if (r->health < 0) r->health = 0;
    zm_roster_send(r);
    dbg_printf("[world] director departure uid %04X room %d/%02X\n", uid, g_stageId, g_roomId);
}

// cmd_enemy_set, after the record's own setup: is this monster in the roster?
// Returns false when it should not appear at all (it died), true otherwise -
// with its saved position, facing and behaviour put on ENTITY when known.
bool zm_world_restore_enemy(Entity* e, unsigned char slot, unsigned char id)
{
    if (!s_worldOn || slot >= 16) return true;
    s_slotUid[slot] = slot;
    ZmRosterEntry* r = zm_roster_find(g_stageId, g_roomId, slot, id, false);
    if (r == NULL) return true;
    if (!r->alive) return false;
    e->scaMatrixData.localMatrix.t[0] = (unsigned short)r->x;
    e->scaMatrixData.localMatrix.t[1] = r->y;
    e->scaMatrixData.localMatrix.t[2] = (unsigned short)r->z;
    e->position.x = r->x;
    e->position.y = r->y;
    e->position.z = r->z;
    e->angle = r->angle;
    e->behavior_flags = r->behavior;
    s_pendingHealth[slot] = true;
    s_pendingHealthValue[slot] = r->health;
    s_pendingFeeding[slot] = r->feeding;
    return true;
}

// room_set, after the init script (zombie_mode_room_spawn): the room's EXTRA
// monsters that are alive, in uid order - both copies build the same slot
// layout from the same roster - into free slots from `firstSlot` up to (not
// including) `endSlot`. Returns the first slot left free after them.
int zm_world_spawn_extras(int firstSlot, int endSlot)
{
    if (!s_worldOn) return firstSlot;
    unsigned short last = 0;
    int slot = firstSlot;
    for (;;) {
        // The next uid above `last` for this room (a selection walk: rooms
        // hold a handful, the roster is small).
        const ZmRosterEntry* next = NULL;
        for (int i = 0; i < ZM_ROSTER_MAX; i++) {
            const ZmRosterEntry* r = &s_roster[i];
            if (!r->used || !r->alive || r->departed || r->uid < ZM_UID_EXTRA_FIRST) continue;
            if (r->stage != g_stageId || r->room != g_roomId || r->uid <= last) continue;
            if (next == NULL || r->uid < next->uid) next = r;
        }
        if (next == NULL) break;
        last = next->uid;
        while (slot < endSlot && (g_EnemiesList[slot].status_flags & ENTITY_STATUS_ACTIVE) != 0) slot++;
        if (slot >= endSlot) {
            dbg_printf("[world] no slot for extra %04X in stage %d room %d\n",
                       (int)next->uid, (int)g_stageId, (int)g_roomId);
            break;
        }
        Entity* e = zm_spawn_monster(slot, next->id, next->behavior, next->x, next->y, next->z,
                                     next->angle);
        if (e != NULL) {
            s_slotUid[slot] = next->uid;
            s_pendingHealth[slot] = next->health != ZM_HEALTH_FRESH;
            s_pendingHealthValue[slot] = next->health;
            s_pendingFeeding[slot] = next->feeding;
            dbg_printf("[world] extra %04X (id %d) back in slot %d\n",
                       (int)next->uid, (int)next->id, slot);
        }
        slot++;
    }
    return slot;
}

// update_entities, after each monster's own update: once its init has run
// (state no longer 0), give a restored monster back the health it had.
void zm_world_note_spawn(int slot)
{
    if (slot >= 0 && slot < 30) s_freshInit[slot] = true;
}

void zm_world_accept_net_state(Entity* e)
{
    // A current room snapshot supersedes health/feeding saved on an earlier
    // visit. The after-update restore must not overwrite it on this frame.
    int slot = (int)(e - g_EnemiesList);
    if (slot < 0 || slot >= 30) return;
    s_pendingHealth[slot] = false;
    s_pendingFeeding[slot] = false;
    s_freshInit[slot] = false;
}

void zm_world_pending_health(Entity* e, short health)
{
    int slot = (int)(e - g_EnemiesList);
    if (slot < 0 || slot >= 30) return;
    s_pendingHealth[slot] = true;
    s_pendingHealthValue[slot] = health;
}

void zm_world_after_update(Entity* e)
{
    if (!s_worldOn) return;
    int slot = (int)(e - g_EnemiesList);
    if (slot < 0 || slot >= 30 || e->state == 0) return;
    if (s_freshInit[slot]) {
        s_freshInit[slot] = false;
        // A restored one gets its saved health below instead.
        if (e->id == ENEMY_ZOMBIE_NAKED && !s_pendingHealth[slot] && e->health > 0) {
            e->health = (short)(e->health + e->health / 4);
        }
    }
    if (s_pendingHealth[slot]) {
        e->health = s_pendingHealthValue[slot];
        s_pendingHealth[slot] = false;
    }
    if (s_pendingFeeding[slot]) {
        s_pendingFeeding[slot] = false;
        zombie_mode_begin_feeding(e, true);
    }
}

void zm_world_queue_feeding(Entity* e)
{
    int slot = (int)(e - g_EnemiesList);
    if (slot >= 0 && slot < 30) s_pendingFeeding[slot] = true;
}

void zm_world_apply_feeding(const short* a)
{
    if (!s_worldOn) return;
    ZmRosterEntry* r = zm_roster_find((unsigned char)a[0], (unsigned char)(a[0] >> 8),
        (unsigned short)a[1], (unsigned char)a[2], true);
    if (r != NULL) r->feeding = a[3] != 0;
}

// ZM_EV_ROSTER from the other copy: newest write wins.
bool zm_world_departed(unsigned short uid, unsigned char id)
{
    ZmRosterEntry* r = zm_roster_find(g_stageId, g_roomId, uid, id, false);
    return r != NULL && r->departed;
}

void zm_world_apply_remote(const short* a, int src)
{
    if (!s_worldOn) return;
    unsigned short uid = (unsigned short)(a[1] & 0x7FFF);
    ZmRosterEntry* r = zm_roster_find((unsigned char)(a[0] & 0xFF), (unsigned char)((a[0] >> 8) & 0xFF),
                                      uid, (unsigned char)(((unsigned short)a[7] >> 8) & 0x7F), false);
    // Carried bodies get a new uid in the destination. This uid can never
    // return alive here; a delayed capture must not undo its departure.
    if (r != NULL && r->departed) return;
    if (r == NULL) r = zm_roster_find((unsigned char)(a[0] & 0xFF), (unsigned char)((a[0] >> 8) & 0xFF),
                                      uid, (unsigned char)(((unsigned short)a[7] >> 8) & 0x7F), true);
    if (r == NULL) return;
    // Only the director can claim/release possession. A survivor's delayed
    // roster capture must never turn a reserved arrival back into AI.
    if (src == ZM_NET_DIRECTOR) {
        bool controlled = ((unsigned short)a[7] & 0x8000) != 0;
        if (controlled != r->directorControlled)
            dbg_printf("[control] received uid %04X id %02X room %d/%02X %s\n",
                r->uid, r->id, r->stage, r->room, controlled ? "reserved for director" : "released to AI");
        r->directorControlled = controlled;
    }
    bool wasAlive = r->alive != 0;
    r->x = a[2];
    r->y = a[3];
    r->z = a[4];
    r->angle = a[5];
    r->health = a[6];
    r->alive = (a[1] & 0x8000) ? 1 : 0;
    r->departed = !r->alive && r->health >= 0;
    r->behavior = (unsigned char)(a[7] & 0xFF);
    // Killed on another copy (that room's owner, leaving it): the director's
    // refund. Not alive with health left is a body carried out of the room.
    if (wasAlive && !r->alive && r->health < 0) zm_econ_refund(r->id, r->uid);
    // A carried monster leaves this room outright, rather than becoming AI
    // when the director leaves. The reliable roster event covers all types.
    if (!r->alive && r->health >= 0 && r->stage == g_stageId && r->room == g_roomId)
        zm_shared_remove_departed(r->uid, r->id);
}

// ---------------------------------------------------------------------------
// World flags. Only the banks that move one way in normal play are shared,
// and merged so neither copy can undo the other's progress:
//   g_LocksFlags     a set bit = unlocked              -> OR
//   g_EnemiesFlags   a set bit = that monster is dead  -> OR
//   g_roomItemsFlags a CLEAR bit = item taken          -> AND
// Story flags (ScenarioFlags*) are not: room scripts set AND clear them.
// ---------------------------------------------------------------------------
int zm_world_pack_flags(unsigned char* out)
{
    memcpy(out, g_LocksFlags, 8);
    memcpy(out + 8, g_EnemiesFlags, 32);
    memcpy(out + 40, g_roomItemsFlags, 32);
    return 72;
}

void zm_world_merge_flags(const unsigned char* in)
{
    if (!s_worldOn) return;
    for (int i = 0; i < 8; i++)  g_LocksFlags[i] |= in[i];
    for (int i = 0; i < 32; i++) g_EnemiesFlags[i] |= in[8 + i];
    // The dropped items' flags are each copy's own (ZombieDrops.cpp).
    for (int i = 0; i < 32; i++) g_roomItemsFlags[i] &= (unsigned char)(in[40 + i] | zm_drops_flag_mask(i));
}

// ---------------------------------------------------------------------------
// Story flags (g_ScenarioFlags, g_ScenarioFlags2). They cannot be merged - room
// scripts set AND clear them - so a survivor's copy sends each change instead:
// it compares the banks with what it last saw, once a frame, and every byte
// that differs goes out as ZM_EV_STORY { bank, byte, bits set, bits cleared }.
// The other copies apply the change and take it into their own "last seen", so
// it does not echo back. Example: the 2F dining room's statue - the fall event
// sets bank 0 bit 0x0B, which is what ROOM7020 (statue gone) and ROOM6050
// (the broken statue and its jewel) test.
//
// Bits that describe one player rather than the world stay local.
// ---------------------------------------------------------------------------
#define ZM_STORY_BANK0_BYTES 16
#define ZM_STORY_BANK1_BYTES 32

static unsigned char s_storySeen0[ZM_STORY_BANK0_BYTES];
static unsigned char s_storySeen1[ZM_STORY_BANK1_BYTES];
static unsigned char s_storyLocal0[ZM_STORY_BANK0_BYTES];   // per-player bits, by byte
static unsigned char s_storyLocal1[ZM_STORY_BANK1_BYTES];
static bool s_storySeenValid = false;
static bool s_storyLocalBuilt = false;

static void zm_story_build_local(void)
{
    static const unsigned char kBank0[] = {
        SCENARIO_FLAG_STAGE_VARIANT, SCENARIO_FLAG_YAWN_BITE, SCENARIO_FLAG_ALTERNATE_OUTFIT,
        SCENARIO_FLAG_YAWN_SERUM, SCENARIO_FLAG_SECOND_PLAYTHROUGH, SCENARIO_FLAG_HAS_LOCKPICK,
        SCENARIO_FLAG_MENU_FADE_LATCH, SCENARIO_FLAG_INF_R_LAUNCHER, SCENARIO_FLAG_HAS_RADIO,
        DC_SCENARIO_FLAG_INF_COLT_PYTHON,
    };
    static const unsigned char kBank1[] = {
        SCENARIO2_FLAG_JILL_FIRST_RUN, SCENARIO2_FLAG_PROGRESS_22, SCENARIO2_FLAG_YAWN_POISONED,
    };
    memset(s_storyLocal0, 0, sizeof(s_storyLocal0));
    memset(s_storyLocal1, 0, sizeof(s_storyLocal1));
    // Flg_on's own bit order (MSB first within each dword) builds the masks.
    for (unsigned i = 0; i < sizeof(kBank0); i++) Flg_on((int)s_storyLocal0, kBank0[i]);
    for (unsigned i = 0; i < sizeof(kBank1); i++) Flg_on((int)s_storyLocal1, kBank1[i]);
    s_storyLocalBuilt = true;
}

// A new game: the next watch takes the banks as they are, sending nothing.
void zm_world_story_reset(void)
{
    s_storySeenValid = false;
}

static void zm_story_watch_bank(int bank, const unsigned char* cur, unsigned char* seen,
                                const unsigned char* local, int count)
{
    for (int i = 0; i < count; i++) {
        unsigned char diff = (unsigned char)((cur[i] ^ seen[i]) & ~local[i]);
        if (diff == 0) continue;
        unsigned char set = (unsigned char)(diff & cur[i]);
        unsigned char clr = (unsigned char)(diff & ~cur[i]);
        zm_net_send_event(ZM_EV_STORY, (short)bank, (short)i, (short)set, (short)clr);
        dbg_printf("[story] send bank %d byte %d set %02X clear %02X\n", bank, i, set, clr);
        seen[i] = (unsigned char)((seen[i] & ~diff) | (cur[i] & diff));
    }
}

// zombie_mode_net_frame, survivor copies only.
void zm_world_story_watch(void)
{
    if (!s_worldOn) return;
    if (!s_storyLocalBuilt) zm_story_build_local();
    if (!s_storySeenValid) {
        memcpy(s_storySeen0, &g_ScenarioFlags, ZM_STORY_BANK0_BYTES);
        memcpy(s_storySeen1, g_ScenarioFlags2, ZM_STORY_BANK1_BYTES);
        s_storySeenValid = true;
        return;
    }
    zm_story_watch_bank(0, (const unsigned char*)&g_ScenarioFlags, s_storySeen0, s_storyLocal0,
                        ZM_STORY_BANK0_BYTES);
    zm_story_watch_bank(1, g_ScenarioFlags2, s_storySeen1, s_storyLocal1, ZM_STORY_BANK1_BYTES);
}

// A copy that stopped watching (a dead survivor following the others: its
// room's scripts are not its own player's) takes the flags as they are when
// it starts again, rather than sending what changed meanwhile.
void zm_world_story_rebase(void)
{
    s_storySeenValid = false;
}

// ZM_EV_STORY from a survivor's copy: { bank, byte, set, clear }.
void zm_world_story_apply(const short* a)
{
    if (!s_worldOn) return;
    if (!s_storyLocalBuilt) zm_story_build_local();
    int bank = a[0], i = a[1];
    unsigned char* cur;
    unsigned char* seen;
    const unsigned char* local;
    if (bank == 0 && i >= 0 && i < ZM_STORY_BANK0_BYTES) {
        cur = (unsigned char*)&g_ScenarioFlags; seen = s_storySeen0; local = s_storyLocal0;
    } else if (bank == 1 && i >= 0 && i < ZM_STORY_BANK1_BYTES) {
        cur = g_ScenarioFlags2; seen = s_storySeen1; local = s_storyLocal1;
    } else {
        return;
    }
    unsigned char set = (unsigned char)(a[2] & ~local[i]);
    unsigned char clr = (unsigned char)(a[3] & ~local[i]);
    cur[i] = (unsigned char)((cur[i] | set) & ~clr);
    seen[i] = (unsigned char)((seen[i] | set) & ~clr);
    dbg_printf("[story] take bank %d byte %d set %02X clear %02X\n", bank, i, set, clr);
}

// ---------------------------------------------------------------------------
// The item box (g_itemboxSlots, 48 slots), one for every survivor. It starts
// empty (zm_world_box_reset, at the armed new game). ZombieBox.cpp arbitrates
// every multiplayer transfer; only host-approved updates change shared slots.
// ---------------------------------------------------------------------------
#define ZM_BOX_SLOTS 48

void zm_world_box_reset(void)
{
    memset(g_itemboxSlots, 0, sizeof(ItemSlot) * ZM_BOX_SLOTS);
}

void zm_world_reconnect_shared(const BioCardLayout* card)
{
    if (!s_storyLocalBuilt) zm_story_build_local();
    for (int i = 0; i < 16; i++) g_ScenarioFlags[i] =
        (g_ScenarioFlags[i] & s_storyLocal0[i]) | (card->scenarioFlags[i] & ~s_storyLocal0[i]);
    for (int i = 0; i < 32; i++) g_ScenarioFlags2[i] =
        (g_ScenarioFlags2[i] & s_storyLocal1[i]) | (card->scenarioFlags2[i] & ~s_storyLocal1[i]);
    memcpy(g_LocksFlags, card->locksFlags, sizeof(card->locksFlags));
    memcpy(g_EnemiesFlags, card->enemiesFlags, sizeof(card->enemiesFlags));
    memcpy(g_roomItemsFlags, card->roomItemsFlags, sizeof(card->roomItemsFlags));
    memcpy(g_itemboxSlots, card->itemboxSlots, sizeof(card->itemboxSlots));
    zm_world_story_rebase();
}

// ---------------------------------------------------------------------------
// Where the zombies are, for the director's map. A room that is not loaded is
// what its init script would spawn on entry - the same tests cmd_enemy_set
// makes (death flag, then the roster this mod restores from) - plus the
// roster's extras. Script blocks
// guarded by story conditions are counted as if they ran, so a room can
// promise a zombie its script will not place; the jump then falls back to the
// director's own body.
// ---------------------------------------------------------------------------
int zm_world_room_zombies(unsigned char stage, unsigned char room, ZmJumpTarget* first)
{
    int count = 0;
    if (stage == g_stageId && room == g_roomId) {
        for (int i = 0; i < 30; i++) {
            const Entity* e = &g_EnemiesList[i];
            if ((e->status_flags & ENTITY_STATUS_ACTIVE) == 0 ||
                (!zm_is_possessable_id(e->id) && !zm_yawn_head(e))) continue;
            if (e->health < 0 || (e->status_flags & ENTITY_STATUS_DEAD) != 0) continue;
            if (count == 0 && first != NULL) {
                first->slot = (unsigned char)i;
                first->id = e->id; first->uid = s_slotUid[i];
                first->x = (short)e->scaMatrixData.localMatrix.t[0];
                first->y = (short)e->scaMatrixData.localMatrix.t[1];
                first->z = (short)e->scaMatrixData.localMatrix.t[2];
                first->angle = e->angle;
            }
            count++;
        }
        return count;
    }

    // A boss room with its Yawn alive: the mode places it in slot 0 (uid 0)
    // when the room loads (ZombieYawn.cpp).
    if (unsigned char yawn = zm_yawn_jump_room(stage, room)) {
        if (first != NULL) {
            memset(first, 0, sizeof(*first));
            first->id = yawn;
            zm_yawn_spawn_spot(stage, room, &first->x, &first->z, &first->angle);
        }
        return 1;
    }
    // The rooms' own monsters never spawn in the mode (zombie_mode_enemy_spawn):
    // only the roster's extras - the director's placements - are there.
    const ZmSpawn* spawns;
    int n = zm_room_spawns(stage, room, &spawns);
    if (n < 0) return -1;
    n = 0;
    unsigned int seenSlots = 0;
    for (int i = 0; i < n; i++) {
        const ZmSpawn& sp = spawns[i];
        if (!zm_is_possessable_id(sp.id) || (seenSlots & (1u << sp.slot)) != 0) continue;
        if (sp.deathFlag != 0xFF && Flg_ck((int)g_EnemiesFlags, sp.deathFlag) != 0) continue;
        short x = sp.x, y = sp.y, z = sp.z, angle = sp.angle;
        const ZmRosterEntry* r = zm_roster_find(stage, room, sp.slot, sp.id, false);
        if (r != NULL) {
            if (!r->alive) continue;
            x = r->x; y = r->y; z = r->z; angle = r->angle;
        }
        seenSlots |= 1u << sp.slot;
        if (count == 0 && first != NULL) {
            first->slot = sp.slot;
            first->id = sp.id; first->uid = sp.slot;
            first->x = x; first->y = y; first->z = z;
            first->angle = angle;
        }
        count++;
    }
    // The extras waiting there (no slot until they are spawned: 0xFF).
    for (int i = 0; i < ZM_ROSTER_MAX; i++) {
        const ZmRosterEntry* r = &s_roster[i];
        if (!r->used || !r->alive || r->departed || r->uid < ZM_UID_EXTRA_FIRST) continue;
        if (r->stage != stage || r->room != room || !zm_is_possessable_id(r->id)) continue;
        if (count == 0 && first != NULL) {
            first->slot = 0xFF;
            first->id = r->id; first->uid = r->uid;
            first->x = r->x; first->y = r->y; first->z = r->z;
            first->angle = r->angle;
        }
        count++;
    }
    return count;
}

// ---------------------------------------------------------------------------
// Placed monsters (the director's), and the uid <-> enemy slot map the shared
// room's sync goes through.
// ---------------------------------------------------------------------------

// A new monster for (stage, room), alive, kept and told to everyone. Its own
// init picks its health the first time it runs. Returns its uid, 0 on failure.
unsigned short zm_world_add_extra(unsigned char stage, unsigned char room, unsigned char id,
                                  unsigned char behavior, short x, short y, short z, short angle,
                                  bool directorControlled, short initialHealth)
{
    if (!s_worldOn) return 0;
    unsigned short uid = zm_mint_uid();
    ZmRosterEntry* r = zm_roster_find(stage, room, uid, id, true);
    if (r == NULL) return 0;
    r->alive = 1;
    r->behavior = behavior;
    r->x = x; r->y = y; r->z = z;
    r->angle = angle;
    r->health = initialHealth;
    r->directorControlled = directorControlled; // atomic with the first spawn announcement
    zm_roster_send(r);
    dbg_printf("[world] placed id %d as %04X in stage %d room %02X\n",
               (int)id, (int)uid, (int)stage, (int)room);
    return uid;
}

// The AI director walks an idle monster of a room nobody has loaded through a
// door, unseen: it departs there (as a body carried out does - never back
// alive under that uid) and comes in at (toStage, toRoom) with its type,
// behaviour and health under a new uid. Returns that uid, 0 if it cannot move.
unsigned short zm_world_move_extra(unsigned char stage, unsigned char room, unsigned short uid,
                                   unsigned char toStage, unsigned char toRoom,
                                   short x, short y, short z, short angle)
{
    if (!s_worldOn || uid < ZM_UID_EXTRA_FIRST) return 0;
    ZmRosterEntry* r = NULL;
    for (int i = 0; i < ZM_ROSTER_MAX && r == NULL; i++) {
        ZmRosterEntry* e = &s_roster[i];
        if (e->used && e->stage == stage && e->room == room && e->uid == uid) r = e;
    }
    if (r == NULL || !r->alive || r->departed || r->feeding || r->directorControlled) return 0;
    unsigned char id = r->id, behavior = r->behavior;
    short health = r->health;
    unsigned short moved = zm_world_add_extra(toStage, toRoom, id, behavior, x, y, z, angle, false, health);
    if (moved == 0) return 0;
    r->alive = 0;
    r->departed = true;
    if (r->health < 0) r->health = 0;        // a departure, not a kill, on the wire
    zm_roster_send(r);
    dbg_printf("[world] moved %04X id %02X from room %d/%02X to %d/%02X as %04X\n",
               uid, id, stage, room, toStage, toRoom, moved);
    return moved;
}

// Possession is attached to the persistent uid, not a slot or the current
// room owner. ROSTER is reliable and bypasses the effects inbox; its initial
// creation already contains this bit, so no later cue can leave an AI gap.
void zm_world_control(unsigned char stage, unsigned char room, unsigned short uid,
                      unsigned char id, bool controlled)
{
    if (!s_worldOn || zm_game_role() == ZM_NET_SURVIVOR) return;
    ZmRosterEntry* r = zm_roster_find(stage, room, uid, id, false);
    if (r == NULL || r->directorControlled == controlled) return;
    r->directorControlled = controlled;
    zm_roster_send(r);
    dbg_printf("[control] uid %04X id %02X room %d/%02X %s\n",
        uid, id, stage, room, controlled ? "reserved for director" : "released to AI");
}

void zm_world_control_entity(const Entity* e, bool controlled)
{
    if (!s_worldOn || e == NULL || zm_game_role() == ZM_NET_SURVIVOR) return;
    int slot = (int)(e - g_EnemiesList);
    if (slot < 0 || slot >= 30) return;
    unsigned short uid = s_slotUid[slot];
    if (uid == ZM_UID_NONE || uid == ZM_UID_NEW) {
        if (!controlled) return;
        uid = zm_mint_uid();
        s_slotUid[slot] = uid;
    }
    ZmRosterEntry* r = zm_roster_find(g_stageId, g_roomId, uid, e->id, false);
    if (r == NULL) {
        r = zm_roster_find(g_stageId, g_roomId, uid, e->id, true);
        if (r == NULL) return;
        r->alive = e->health >= 0;
        r->health = e->state == 0 ? ZM_HEALTH_FRESH : e->health;
        r->behavior = e->behavior_flags;
        r->x = (short)e->scaMatrixData.localMatrix.t[0];
        r->y = (short)e->scaMatrixData.localMatrix.t[1];
        r->z = (short)e->scaMatrixData.localMatrix.t[2];
        r->angle = e->angle;
    }
    zm_world_control(g_stageId, g_roomId, uid, e->id, controlled);
}

bool zm_world_director_controlled(const Entity* e)
{
    if (!s_worldOn || e == NULL) return false;
    int slot = (int)(e - g_EnemiesList);
    if (slot < 0 || slot >= 30) return false;
    const ZmRosterEntry* r = zm_roster_find(g_stageId, g_roomId, s_slotUid[slot], e->id, false);
    return r != NULL && r->directorControlled && !r->departed;
}

// How many alive extras (any type) wait in (stage, room).
int zm_world_room_extra_count(unsigned char stage, unsigned char room, bool slots)
{
    int n = 0;
    for (int i = 0; i < ZM_ROSTER_MAX; i++) {
        const ZmRosterEntry* r = &s_roster[i];
        if (r->used && r->alive && !r->departed && r->uid >= ZM_UID_EXTRA_FIRST && r->stage == stage && r->room == room)
            n += slots ? zm_econ_monster_slots(r->id) : 1;
    }
    return n;
}

// The alive extras of (stage, room) - in the mode, every monster there - by
// type: up to `max` ids, and where each stands (`xz`, pairs) when given.
// Returns how many were written.
int zm_world_room_extra_ids(unsigned char stage, unsigned char room, unsigned char* ids, int max, short* xz,
                            unsigned short* uids)
{
    int n = 0;
    for (int i = 0; i < ZM_ROSTER_MAX && n < max; i++) {
        const ZmRosterEntry* r = &s_roster[i];
        if (!r->used || !r->alive || r->departed || r->uid < ZM_UID_EXTRA_FIRST || r->stage != stage || r->room != room)
            continue;
        if (xz != NULL) { xz[n * 2] = r->x; xz[n * 2 + 1] = r->z; }
        if (uids != NULL) uids[n] = r->feeding || r->directorControlled ? 0 : r->uid;   // 0: not free to move
        ids[n++] = r->id;
    }
    return n;
}

// The loaded room's alive extras that have no entity here (a live spawn
// that failed waits for the next load): they count against its cap.
int zm_world_room_unspawned(bool slots)
{
    if (!s_worldOn) return 0;
    int n = 0;
    for (int i = 0; i < ZM_ROSTER_MAX; i++) {
        const ZmRosterEntry* r = &s_roster[i];
        if (!r->used || !r->alive || r->departed || r->uid < ZM_UID_EXTRA_FIRST) continue;
        if (r->stage != g_stageId || r->room != g_roomId) continue;
        if (zm_world_slot_of_uid(r->uid) < 0) n += slots ? zm_econ_monster_slots(r->id) : 1;
    }
    return n;
}

void zm_world_track_uid(const Entity* e, unsigned short uid)
{
    int slot = (int)(e - g_EnemiesList);
    if (slot >= 0 && slot < 30) {
        s_slotUid[slot] = uid;
        const ZmRosterEntry* r = zm_roster_find(g_stageId, g_roomId, uid, e->id, false);
        if (r != NULL && r->alive) {
            if (r->feeding) s_pendingFeeding[slot] = true;
            if (e->state == 0 && r->health != ZM_HEALTH_FRESH) {
                s_pendingHealth[slot] = true;
                s_pendingHealthValue[slot] = r->health;
            }
        }
    }
}

// The uid a slot of the loaded room is kept under (ZM_WORLD_UID_NONE if none,
// ZM_WORLD_UID_NEW for one not captured yet).
unsigned short zm_world_uid_of_slot(int slot)
{
    return (slot >= 0 && slot < 30) ? s_slotUid[slot] : ZM_UID_NONE;
}

int zm_world_slot_of_uid(unsigned short uid)
{
    for (int i = 0; i < 30; i++) {
        if (s_slotUid[i] == uid && (g_EnemiesList[i].status_flags & ENTITY_STATUS_ACTIVE) != 0) return i;
    }
    return -1;
}

// An extra the roster says is alive in the loaded room but that this copy has
// no entity for (placed by the director while this copy was here). Returns
// true and fills the spawn data for the first such one not in `skip`.
bool zm_world_missing_extra(unsigned short* uid, unsigned char* id, unsigned char* behavior,
                            short* x, short* y, short* z, short* angle,
                            const unsigned short* skip, int skipCount)
{
    if (!s_worldOn) return false;
    for (int i = 0; i < ZM_ROSTER_MAX; i++) {
        const ZmRosterEntry* r = &s_roster[i];
        if (!r->used || !r->alive || r->departed || r->uid < ZM_UID_EXTRA_FIRST) continue;
        if (r->stage != g_stageId || r->room != g_roomId) continue;
        if (zm_world_slot_of_uid(r->uid) >= 0) continue;
        bool skipped = false;
        for (int k = 0; k < skipCount && !skipped; k++) skipped = skip[k] == r->uid;
        if (skipped) continue;
        *uid = r->uid; *id = r->id; *behavior = r->behavior;
        *x = r->x; *y = r->y; *z = r->z; *angle = r->angle;
        return true;
    }
    return false;
}
