#include "ZombieModeInternal.h"
#include "ZombieNet.h"
#include "../entities/EntityCommon.h"
#include "../../DebugPrint.h"
#include "../../platform/platform.h"
#include <cstdio>
#include <cstring>

// ============================================================================
// ZombieEconomy.cpp - the director's points (port-added).
//
// The director buys every monster it places from the map:
//   * points come in from the start of the game at a rate set by how many
//     survivors are alive (zm_econ_rate: 1 a second for one, 2 for two, 4
//     for three - three are much harder to hold back), ZM_ECON_HIT_BONUS for each hit its possessed monster lands
//     on a survivor, ZM_ECON_MONSTER_HIT for each hit of its other monsters
//     (from whichever copy runs the room: ZM_EV_CREDIT), and a quarter of a
//     monster's price back when it dies - none of it while a Yawn's
//     lockdown holds (zm_yawn_lockdown: the survivors regroup, the director
//     plays on but earns nothing);
//   * the bigger monsters unlock as the Yawns fall (zm_yawn_beaten_count,
//     ZombieYawn.cpp): zombies and Cerberus from the start, the Hunter,
//     Chimera and web spinner once the first Yawn is beaten, the Tyrant
//     once both are;
//   * each room holds a few monsters, by its floor area (the cap in
//     ZombieSpawnSpots.cpp), one more at 6 and again at 10 minutes.
// Only the director's copy (and single player) keeps the account; the
// survivors' copies never place anything.
// ============================================================================

#define ZM_ECON_HIT_BONUS    50
#define ZM_ECON_MONSTER_HIT  25       // one of its other monsters hit a survivor
#define ZM_ECON_HIT_GAP_MS   1500     // one bonus per hit, not per bite of a grab
#define ZM_ECON_REFUND_DIV   4        // a dead monster gives back 25%
#define ZM_ECON_DEFAULT_CAP  2        // a room the generated table lacks
#define ZM_ECON_CAP_STEP1_MS (6 * 60000)
#define ZM_ECON_CAP_STEP2_MS (10 * 60000)

struct ZmPrice {
    unsigned char id;
    short         cost;
    unsigned char bosses;      // Yawns beaten before it may be bought; 0 from the start
};

static const ZmPrice kPrices[] = {
    { ENEMY_ZOMBIE,         100, 0 },
    { ENEMY_ZOMBIE_VARIANT, 125, 0 },             // green coat
    { ENEMY_ZOMBIE_NAKED,   150, 0 },             // +25% health (zm_world_after_update)
    { ENEMY_CERBERUS,       200, 0 },
    { ENEMY_WEB_SPINNER,    150, 1 },
    { ENEMY_HUNTER,        500, 1 },
    { ENEMY_CHIMERA,       400, 1 },
    { ENEMY_TYRANT_2,     1800, 2 },
    { ZM_TRAP_LOCK_DOORS,   100, 0 },             // a trap (ZombieTraps.cpp), not a monster
};

static bool         s_on = false;
static int          s_startPoints = 500; // 500 / 800 / 1000 for 1 / 2 / 3 survivors
static unsigned int s_startMs = 0;
static int          s_bonus = 0;      // hits and refunds
static int          s_spent = 0;
static unsigned int s_lastHitMs = 0;
// The time-based income, accumulated as it goes (the rate changes with the
// survivors): milli-points, and when it was last brought up to date.
static unsigned int s_earnedMilli = 0;
static unsigned int s_earnedAtMs = 0;

// Each monster pays back once: by uid (the roster's, from either path that
// sees it die), and per enemy slot of the loaded room for one with no uid yet.
static unsigned short s_refunded[256];
static int            s_refundedCount = 0;
static bool           s_deadSeen[30];

// For the end of the match's stats (ZombieStats.cpp).
static int s_placed = 0;       // monsters bought
static int s_trapsSet = 0;
static int s_lost = 0;         // monsters of the director's that died
static int s_survivorHits = 0; // hits on survivors, the bonus's and the monsters'

static const ZmPrice* zm_econ_price(unsigned char id)
{
    for (unsigned int i = 0; i < sizeof(kPrices) / sizeof(kPrices[0]); i++) {
        if (kPrices[i].id == id) return &kPrices[i];
    }
    return NULL;
}

void zm_econ_new_game(bool on)
{
    s_on = on;
    int survivors = zm_net_role() == ZM_NET_OFF ? 1 : zm_net_survivor_count();
    s_startPoints = survivors >= 3 ? 1200 : survivors == 2 ? 900 : 500;
    s_startPoints = s_startPoints * zm_ai_income_pct() / 100;   // the AI director's difficulty
#ifdef QUICK_DEBUG
    s_startPoints = 20000;
#endif
    s_startMs = zm_game_time_ms();
    s_bonus = 0;
    s_spent = 0;
    s_lastHitMs = 0;
    s_earnedMilli = 0;
    s_earnedAtMs = s_startMs;
    s_refundedCount = 0;
    memset(s_deadSeen, 0, sizeof(s_deadSeen));
    s_placed = 0;
    s_trapsSet = 0;
    s_lost = 0;
    s_survivorHits = 0;
}

bool zm_econ_on(void) { return s_on; }

// Points a second: by the survivors alive (at least one counted - they are
// not listed until their first state arrives).
static int zm_econ_rate(void)
{
    ZmSurvivorInfo surv[ZM_NET_MAX_PLAYERS];
    int n = zm_survivor_list(surv, ZM_NET_MAX_PLAYERS);
    int alive = 0;
    for (int i = 0; i < n; i++) if (!surv[i].dead) alive++;
    if (alive >= 3) return 4;
    if (alive == 2) return 2;
    return 1;
}

int zm_econ_points(void)
{
    if (!s_on) return 0;
    unsigned int now = zm_game_time_ms();
    if (zm_yawn_lockdown()) s_earnedAtMs = now;     // no income until the doors open
    s_earnedMilli += (now - s_earnedAtMs) * (unsigned int)zm_econ_rate() * (unsigned int)zm_ai_income_pct() / 100;
    s_earnedAtMs = now;
    return s_startPoints + (int)(s_earnedMilli / 1000) + s_bonus - s_spent;
}

int zm_econ_cost(unsigned char id)
{
    const ZmPrice* p = zm_econ_price(id);
    return p != NULL ? p->cost : 0;
}

// Normal match rules for the guide, independent of QUICK_DEBUG: the Yawns
// to beat before `id` may be bought.
int zm_econ_unlock_bosses(unsigned char id)
{
    const ZmPrice* p = zm_econ_price(id);
    return p != NULL ? p->bosses : 0;
}

// How many more Yawns must fall before `id` may be placed: 0 now.
int zm_econ_locked(unsigned char id)
{
#ifdef QUICK_DEBUG
    // Testing: every monster is available, even before survivors spawn.
    return 0;
#else
    int left = zm_econ_unlock_bosses(id) - zm_yawn_beaten_count();
    return left > 0 ? left : 0;
#endif
}

int zm_econ_monster_slots(unsigned char id)
{
    if (id == ENEMY_HUNTER || id == ENEMY_CHIMERA) return 2;
    if (id == ENEMY_TYRANT_1 || id == ENEMY_TYRANT_2) return 3;
    return 1;
}

int zm_econ_room_cap(unsigned char stage, unsigned char room)
{
    int cap = ZM_ECON_DEFAULT_CAP;
    for (int i = 0; i < g_zmSpawnSpotCount; i++) {
        if (g_zmSpawnSpots[i].stage == stage && g_zmSpawnSpots[i].room == room) {
            cap = g_zmSpawnSpots[i].cap & ~ZM_SPAWN_CAP_FIXED;
            if (g_zmSpawnSpots[i].cap & ZM_SPAWN_CAP_FIXED) return cap;
            break;
        }
    }
    int in = zm_survivors_in_ms();
    if (in >= ZM_ECON_CAP_STEP1_MS) cap++;
    if (in >= ZM_ECON_CAP_STEP2_MS) cap++;
    return cap;
}

// Is there room in (stage, room) for a monster of type `id` (its slots under
// the cap; a Tyrant always fits an empty room)?
bool zm_econ_room_fits(unsigned char stage, unsigned char room, unsigned char id)
{
    if (zombie_mode_armed() && zm_access_room_elevator(stage, room)) return false;
    int cap = zm_econ_room_cap(stage, room);
    int n = zm_room_monster_slots(stage, room);
    bool emptyTyrant = n == 0 && (id == ENEMY_TYRANT_1 || id == ENEMY_TYRANT_2);
    return n + zm_econ_monster_slots(id) <= cap || emptyTyrant;
}

// May the director put a monster of type `id` into (stage, room)? If not,
// `why` says so (for the map's note).
bool zm_econ_can_place(unsigned char stage, unsigned char room, unsigned char id,
                       const char* name, char* why, int whyLen)
{
    if (!s_on) return true;
    if (zm_access_room_elevator(stage, room)) {
        snprintf(why, whyLen, "MONSTERS CANNOT ENTER THE ELEVATOR");
        return false;
    }
    if (zm_econ_locked(id) > 0) {
        if (zm_econ_unlock_bosses(id) >= 2) snprintf(why, whyLen, "%s: BEAT BOTH YAWNS FIRST", name);
        else snprintf(why, whyLen, "%s: BEAT A YAWN FIRST", name);
        return false;
    }
    if (!zm_econ_room_fits(stage, room, id)) {
        snprintf(why, whyLen, "ROOM FULL: %d OF %d SLOTS", zm_room_monster_slots(stage, room),
                 zm_econ_room_cap(stage, room));
        return false;
    }
    int cost = zm_econ_cost(id);
    if (zm_econ_points() < cost) {
        snprintf(why, whyLen, "%s COSTS %d POINTS", name, cost);
        return false;
    }
    return true;
}

void zm_econ_pay(unsigned char id)
{
    if (!s_on) return;
    s_spent += zm_econ_cost(id);
    if (zm_is_trap_id(id)) s_trapsSet++;
    else s_placed++;
    dbg_printf("[econ] bought id %02X for %d: %d left\n", (unsigned)id, zm_econ_cost(id), zm_econ_points());
}

// A monster died: a quarter of its price back, once per monster. uid: its
// roster uid, ZM_WORLD_UID_NEW for one not kept under a uid yet.
void zm_econ_refund(unsigned char id, unsigned short uid)
{
    if (!s_on) return;
    if (uid >= 0x100 && uid < ZM_WORLD_UID_BODY) {
        int n = s_refundedCount < 256 ? s_refundedCount : 256;
        for (int i = 0; i < n; i++) {
            if (s_refunded[i] == uid) return;
        }
        s_refunded[s_refundedCount % 256] = uid;
        s_refundedCount++;
    }
    int back = zm_yawn_lockdown() ? 0 : zm_econ_cost(id) / ZM_ECON_REFUND_DIV;
    s_bonus += back;
    s_lost++;
    dbg_printf("[econ] id %02X (%04X) died: +%d\n", (unsigned)id, (unsigned)uid, back);
}

// After each monster's update on the director's copy: one that has just died.
// A live monster in the slot clears the mark, so the slot's next one counts.
void zm_econ_after_update(const Entity* e, int slot, unsigned short uid)
{
    if (!s_on || slot < 0 || slot >= 30) return;
    bool dead = e->health < 0 || (e->status_flags & ENTITY_STATUS_DEAD) != 0;
    if (!dead) {
        s_deadSeen[slot] = false;
        return;
    }
    if (s_deadSeen[slot]) return;
    s_deadSeen[slot] = true;
    if (uid == ZM_WORLD_UID_NONE) return;     // not one the director placed
    zm_econ_refund(e->id, uid);
}

// The room's slots are emptied (room change): no death carries over.
void zm_econ_room_reset(void)
{
    memset(s_deadSeen, 0, sizeof(s_deadSeen));
}

// The director's possessed monster hit a survivor.
void zm_econ_hit(void)
{
    if (!s_on) return;
    unsigned int now = zm_game_time_ms();
    if (s_lastHitMs != 0 && now - s_lastHitMs < ZM_ECON_HIT_GAP_MS) return;
    s_lastHitMs = now;
    if (!zm_yawn_lockdown()) s_bonus += ZM_ECON_HIT_BONUS;
    s_survivorHits++;
    dbg_printf("[econ] hit: +%d\n", ZM_ECON_HIT_BONUS);
}

// One of the director's other monsters hit a survivor (rate-limited per
// monster by the copy that saw it, ZombieMode.cpp zm_monster_credit).
void zm_econ_monster_hit(void)
{
    if (!s_on) return;
    if (!zm_yawn_lockdown()) s_bonus += ZM_ECON_MONSTER_HIT;
    s_survivorHits++;
    dbg_printf("[econ] monster hit: +%d\n", ZM_ECON_MONSTER_HIT);
}

// The director's side of the end-of-match stats.
void zm_econ_stats(ZmEconStats* out)
{
    memset(out, 0, sizeof(*out));
    if (!s_on) return;
    int points = zm_econ_points();      // brings the income up to date
    out->spent = s_spent;
    out->earned = points - s_startPoints + s_spent;
    out->placed = s_placed;
    out->trapsSet = s_trapsSet;
    out->lost = s_lost;
    out->survivorHits = s_survivorHits;
}

// The account, top right.
void zm_econ_draw(void)
{
    if (!s_on) return;
    int pts = zm_econ_points();
    if (pts > 99999) pts = 99999;
    snprintf(PRINT_TEXT_BUFFER, sizeof(PRINT_TEXT_BUFFER), zm_yawn_lockdown() ? "HELD %d PTS" : "%d PTS", pts);
    int width = (int)strlen(PRINT_TEXT_BUFFER) * 8;
    zm_text_encode(PRINT_TEXT_BUFFER);
    PrintText8x14((short)(320 - 4 - width), 8, 0x8F, 0);
}
