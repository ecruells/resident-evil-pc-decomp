#include "ZombieModeInternal.h"
#include "ZombieNet.h"
#include "ZombieReconnect.h"
#include "../../DebugPrint.h"
#include "../../platform/platform.h"
#include <cstdio>
#include <cstring>

// ============================================================================
// ZombieTraps.cpp - the director's traps (port-added).
//
// Picked from the map's list like a monster type (ZombieMap.cpp kMonsters,
// ids from ZM_TRAP_FIRST) and set on the selected room with Aim. Paid from the
// director's points (ZombieEconomy.cpp); each has its own cooldown.
//
//   LOCK DOORS  every door of the room, and every door into it from its
//               neighbours, is held shut for survivors for ZM_TRAP_LOCK_MS.
//               ZM_EV_TRAP takes it to every copy, which times it on its own
//               clock from when it arrives. Survivors in the room hear the
//               lock click on activation, without a message. Trying one plays
//               the lock click (door_try_enter, zombie_mode_door_trapped); the
//               single-player AI survivor treats those doors as unusable
//               (zm_door_usable). The director's own body is not held.
// ============================================================================

#define ZM_TRAP_LOCK_MS      10000
#define ZM_TRAP_LOCK_COOL_MS 60000
#define ZM_TRAP_MAX_LOCKS    8

struct ZmRoomLock {
    unsigned char stage, room;
    unsigned int  untilMs;
};
static ZmRoomLock   s_locks[ZM_TRAP_MAX_LOCKS];
static unsigned int s_lockReadyMs = 0;     // the director's cooldown: when it may set the next

void zm_trap_reconnect_export(unsigned int remaining[8], unsigned char rooms[16])
{
    for (int i = 0; i < 8; i++) {
        int left = (int)(s_locks[i].untilMs - zm_game_time_ms());
        remaining[i] = s_locks[i].untilMs && left > 0 ? (unsigned int)left : 0;
        rooms[i * 2] = s_locks[i].stage; rooms[i * 2 + 1] = s_locks[i].room;
    }
}
void zm_trap_reconnect_import(const unsigned int remaining[8], const unsigned char rooms[16])
{
    for (int i = 0; i < 8; i++) {
        s_locks[i].stage = rooms[i * 2]; s_locks[i].room = rooms[i * 2 + 1];
        s_locks[i].untilMs = remaining[i] ? zm_game_time_ms() + remaining[i] : 0;
    }
}

void zm_trap_new_game(void)
{
    memset(s_locks, 0, sizeof(s_locks));
    s_lockReadyMs = 0;
}

bool zm_is_trap_id(unsigned char id)
{
    return id >= ZM_TRAP_FIRST;
}

// How long until the director may set trap `id` again (0 now).
int zm_trap_cooldown_ms(unsigned char id)
{
    if (id != ZM_TRAP_LOCK_DOORS || s_lockReadyMs == 0) return 0;
    int left = (int)(s_lockReadyMs - zm_game_time_ms());
    return left > 0 ? left : 0;
}

// The room's doors held shut on this copy, from now.
static void zm_trap_lock_add(unsigned char stage, unsigned char room, unsigned int ms)
{
    unsigned int now = zm_game_time_ms();
    ZmRoomLock* slot = NULL;
    for (int i = 0; i < ZM_TRAP_MAX_LOCKS; i++) {
        ZmRoomLock& l = s_locks[i];
        bool live = l.untilMs != 0 && (int)(l.untilMs - now) > 0;
        if (live && l.stage == stage && l.room == room) { slot = &l; break; }
        if (!live && slot == NULL) slot = &l;
    }
    if (slot == NULL) slot = &s_locks[0];      // all busy: the oldest-placed goes
    slot->stage = stage;
    slot->room = room;
    slot->untilMs = now + ms;
    dbg_printf("[trap] doors of stage %d room %02X locked for %u ms\n", (int)stage, (int)room, ms);
}

// Is (stage, room) held shut? *leftMs: how long still.
bool zm_trap_room_locked(unsigned char stage, unsigned char room, unsigned int* leftMs)
{
    unsigned int now = zm_game_time_ms();
    for (int i = 0; i < ZM_TRAP_MAX_LOCKS; i++) {
        const ZmRoomLock& l = s_locks[i];
        if (l.untilMs == 0 || l.stage != stage || l.room != room) continue;
        int left = (int)(l.untilMs - now);
        if (left <= 0) continue;
        if (leftMs != NULL) *leftMs = (unsigned int)left;
        return true;
    }
    return false;
}

// A door from (stage, room) to `dest` (the record's +0x0D): held shut when
// either side is a locked room. Camera-only records (+0x0B bit 0x80) are not
// doors.
bool zm_trap_door_locked(unsigned char stage, unsigned char room, unsigned char dest,
                         unsigned char flags0B, unsigned int* leftMs)
{
    if ((flags0B & 0x80) != 0) return false;
    if (zm_trap_room_locked(stage, room, leftMs)) return true;
    unsigned char ds, dr;
    zm_decode_dest(dest, stage, &ds, &dr);
    return zm_trap_room_locked(ds, dr, leftMs);
}

// The director sets trap `id` on (stage, room). False with `why` filled when
// it cannot (cooling down, too dear).
bool zm_trap_place(unsigned char stage, unsigned char room, unsigned char id, const char* name,
                   char* why, int whyLen)
{
    if (id != ZM_TRAP_LOCK_DOORS) {
        snprintf(why, whyLen, "NO SUCH TRAP");
        return false;
    }
    int cool = zm_trap_cooldown_ms(id);
    if (cool > 0) {
        int s = (cool + 999) / 1000;
        snprintf(why, whyLen, "%s READY IN %d:%02d", name, s / 60, s % 60);
        return false;
    }
    int cost = zm_econ_cost(id);
    if (zm_econ_on() && zm_econ_points() < cost) {
        snprintf(why, whyLen, "%s COSTS %d POINTS", name, cost);
        return false;
    }
    zm_econ_pay(id);
    s_lockReadyMs = zm_game_time_ms() + ZM_TRAP_LOCK_COOL_MS;
    if (s_lockReadyMs == 0) s_lockReadyMs = 1;
    zm_trap_lock_add(stage, room, ZM_TRAP_LOCK_MS);
    zm_net_send_event(ZM_EV_TRAP, (short)id, (short)(stage | (room << 8)),
                      (short)(ZM_TRAP_LOCK_MS / 100), 0);
    snprintf(why, whyLen, "DOORS LOCKED FOR %d S", ZM_TRAP_LOCK_MS / 1000);
    return true;
}

// ZM_EV_TRAP from the director. True when it is this copy's room (the
// survivor hears the lock click).
bool zm_trap_take(const short* a)
{
    if ((unsigned char)a[0] != ZM_TRAP_LOCK_DOORS) return false;
    unsigned char stage = (unsigned char)(a[1] & 0xFF);
    unsigned char room = (unsigned char)((a[1] >> 8) & 0xFF);
    zm_trap_lock_add(stage, room, (unsigned int)(unsigned short)a[2] * 100u);
    return stage == g_stageId && room == g_roomId;
}
