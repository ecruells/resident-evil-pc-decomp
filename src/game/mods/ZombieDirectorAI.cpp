#include "ZombieModeInternal.h"
#include "ZombieNet.h"
#include "../entities/EntityCommon.h"
#include "../../DebugPrint.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

extern unsigned int Flg_ck(int baseAddr, unsigned int bitIndex);    // 0x00473f40 CmdFunctions.cpp

// ============================================================================
// ZombieDirectorAI.cpp - the AI director (port-added), [Mods] AiDirector.
//
// Spends the director's points on the copy that keeps the match: the host of
// an "AI DIRECTOR" game (ZombieLobby.cpp), whose own player is a survivor in
// seat 0. A human director never shares control with the AI -
// through exactly the calls the map makes (zm_director_place_ai, the doorway
// reinforcement, the LOCK DOORS trap), so every placement rule, price, cap
// and the roster sync apply unchanged. It never takes a monster over.
//
//   Pacing - a threat budget. Monsters stay where they are put, so what
//     matters is the threat standing on the survivors' way forward: each
//     living monster's threat (kThreat) in the rooms one to `lookahead` doors
//     ahead of a survivor, nearer rooms counting more. The target for it
//     rises steadily with the match clock (the difficulty's base, per-minute
//     rise and ceiling), a quarter more for each survivor past the first. It
//     buys only below the target, and the gap sizes the monster: a small gap
//     a zombie or a dog, a large one (late, or a cleared stretch) a Hunter or
//     Chimera. A death or a badly hurt survivor holds the big ones back for a
//     while.
//   Where - the door graph (each room's RDT, cached here as an adjacency
//     table) searched outward from every living survivor. Rooms one to
//     `lookahead` doors ahead score, the room a survivor just left does not,
//     rooms still holding a key or crest (their next objective) score more,
//     rooms already holding monsters less.
//   What - weighted by the room's capacity, what is unlocked and the gap;
//     points are held back for a big monster that is about to unlock.
//   Where in the room - one of the room's finer spots (g_zmPlaceSpots,
//     tools/gen_spawn_spots.py), by the door the survivors will most likely
//     come in through (the room the search reached it from): zombies near
//     it - outside the door stun's reach - in the narrow places; the dogs further back
//     with room to run; the big ones in the open. Away from the monsters
//     already there, facing that door.
//   Marching - idle monsters left behind (rooms off the way ahead, the room a
//     survivor just left) are walked toward the way ahead, unseen: one door
//     at a time, at the type's pace (kHop), and only between rooms no copy
//     has loaded. Their threat counts toward the target from the start, so
//     the AI marches before it buys. In every room on the way it takes one of
//     the finer spots. A room a player walks into stops the march there until
//     they leave.
//   Never in the door stun's reach (AI_STUN_CLEAR): that is a check on
//     human directors camping a doorway, not a tool for the AI.
//   Pressure - a survivor that lingers in one room gets a doorway
//     reinforcement; a hurt survivor in a room with monsters gets its doors
//     locked (hard and above).
//   The opening - as soon as the game starts, the rooms around the main hall
//     are filled to the base target (the human director's setup), within part
//     of the starting points.
// ============================================================================

int g_zmAiDirector = 0;

struct ZmAiLevel {
    const char*  name;
    int          incomePct;      // income and starting points
    int          lookahead;      // doors ahead of a survivor it places
    unsigned int thinkMs;        // between decisions
    int          threatBase;     // the threat target ahead (x10: a zombie is 10) at the start
    int          threatPerMin;   // ...its rise each minute of the match
    int          threatMax;      // ...and its ceiling
    unsigned int dwellMs;        // a survivor this long in one room: reinforce (0 never)
    bool         traps;
    bool         focusWeak;      // weight the hurt survivor's path up
    int          spendPerMin;    // points a minute at most (the opening aside)
    int          openingPct;     // of the starting points
    unsigned int marchEveryMs;   // a new march at most this often (0 never)
    int          marchMax;       // marches under way at once
    int          hopPct;         // of kHop: how fast marches cross a room
    bool         ambush;         // zombies wait just inside the entrance (stunned on arrival, then on you)
};

static const ZmAiLevel kLevels[6] = {
    //                          think  base /min  max   dwell
    //                                                                         march
    // Income: the human director's rate (1 / 2 / 4 a second for 1 / 2 / 3
    // survivors) is sized for a player who also earns by hitting with the
    // body it possesses; the AI never does, so its levels start higher.
    //                                                                         march      hop
    { "OFF",       100, 0,    0,   0,  0,   0,     0, false, false,    0,   0,     0, 0, 100, false },
    { "EASY",      100, 2, 6000,  30,  6, 120,     0, false, false,  600,  50,     0, 0, 100, false },
    { "NORMAL",    150, 2, 4000,  40, 10, 200, 75000, false, false,  900,  70, 60000, 2, 100, false },
    { "HARD",      225, 3, 2500,  50, 14, 280, 45000, true,  false, 1600,  85, 40000, 3,  80, true  },
    // Nightmare and Hell on Earth use 2.2x / 3x NORMAL income and starting points.
    { "NIGHTMARE", 330, 3, 1500,  60, 20, 380, 30000, true,  true,  2400, 100, 20000, 4,  60, true  },
    { "HELL ON EARTH", 450, 3, 1500,  60, 20, 380, 30000, true,  true,  2400, 100, 20000, 4,  60, true  },
};

// What one monster adds to the threat on a route (x10: the zombie is 10).
static const struct { unsigned char id; int threat; } kThreat[] = {
    { ENEMY_ZOMBIE, 10 }, { ENEMY_ZOMBIE_VARIANT, 12 }, { ENEMY_ZOMBIE_NAKED, 15 },
    { ENEMY_WEB_SPINNER, 15 }, { ENEMY_CERBERUS, 20 }, { ENEMY_CHIMERA, 40 },
    { ENEMY_HUNTER, 50 }, { ENEMY_TYRANT_2, 100 },
};
#define AI_BIG_THREAT  40          // a monster this threatening is a "big" one
#define AI_BIG_HOLD_MS 30000       // no big one this long after a death or a bad hurt
// How long a monster takes to cross a room when it walks unseen.
static const struct { unsigned char id; unsigned int ms; } kHop[] = {
    { ENEMY_ZOMBIE, 20000 }, { ENEMY_ZOMBIE_VARIANT, 20000 }, { ENEMY_ZOMBIE_NAKED, 18000 },
    { ENEMY_WEB_SPINNER, 25000 }, { ENEMY_CERBERUS, 8000 }, { ENEMY_HUNTER, 10000 },
    { ENEMY_CHIMERA, 12000 }, { ENEMY_TYRANT_2, 15000 },
};
#define AI_MARCHES      4
#define AI_MARCH_RANGE  8          // doors a march may cover at most
#define AI_MARCH_WAITS  15         // tries a blocked march waits (2 s each) before it stops
// The door stun (ZombieMode.cpp, ZM_STUN_RADIUS 2200 around where a survivor
// comes in) punishes a human director's monsters camping a doorway. The AI
// never puts one there: every spot it uses keeps this far from every point a
// door lets a survivor into its room.
#define AI_STUN_CLEAR  2700
#define AI_NO_ENTRY    0xFFFF

#define AI_STAGES      7
#define AI_ROOMS       0x20
#define AI_MAX_LINKS   ZM_MAX_DOORS
#define AI_FAR         0x7F
#define AI_LOW_HEALTH  45          // a survivor this hurt is "weak" (Jill has 96, Chris 140)

static const struct { unsigned char id; const char* name; } kNames[] = {
    { ENEMY_ZOMBIE, "ZOMBIE" }, { ENEMY_ZOMBIE_VARIANT, "GREEN ZOMBIE" },
    { ENEMY_ZOMBIE_NAKED, "NAKED ZOMBIE" }, { ENEMY_CERBERUS, "CERBERUS" },
    { ENEMY_HUNTER, "HUNTER" }, { ENEMY_CHIMERA, "CHIMERA" }, { ENEMY_TYRANT_2, "TYRANT" },
    { ENEMY_WEB_SPINNER, "WEB SPINNER" }, { ZM_TRAP_LOCK_DOORS, "LOCK DOORS" },
};

static int ai_threat(unsigned char id)
{
    for (unsigned int i = 0; i < sizeof(kThreat) / sizeof(kThreat[0]); i++)
        if (kThreat[i].id == id) return kThreat[i].threat;
    return 10;
}

static const char* ai_name(unsigned char id)
{
    for (unsigned int i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++)
        if (kNames[i].id == id) return kNames[i].name;
    return "MONSTER";
}

// ---------------------------------------------------------------------------
// The door graph
// ---------------------------------------------------------------------------
struct AiRoom {
    bool          built;
    unsigned char count;
    unsigned char stage[AI_MAX_LINKS], room[AI_MAX_LINKS];
    unsigned char dest[AI_MAX_LINKS];   // the record's +0x0D, for the keypad / elevator test
    unsigned char lock[AI_MAX_LINKS];   // +0x0C
    // Where survivors come in: each neighbour's door_set into this room, its
    // arrival point (the stun's centre), and that neighbour.
    bool          arrBuilt;
    unsigned char arrCount;
    short         arrX[AI_MAX_LINKS * 2], arrZ[AI_MAX_LINKS * 2];
    unsigned short arrFrom[AI_MAX_LINKS * 2];
};
static AiRoom s_graph[AI_STAGES][AI_ROOMS];

static const AiRoom* ai_room(unsigned char stage, unsigned char room)
{
    AiRoom& r = s_graph[stage][room];
    if (r.built) return &r;
    r.built = true;
    r.count = 0;
    if (zm_random_room_stub(stage, room)) return &r;
    const ZmDoor* doors;
    int n = zm_room_doors(stage, room, &doors);
    for (int i = 0; i < n && r.count < AI_MAX_LINKS; i++) {
        if ((doors[i].flags0B & 0x80) != 0) continue;              // camera-only
        unsigned char ns, nr;
        zm_decode_dest(doors[i].dest, stage, &ns, &nr);
        if (ns >= AI_STAGES || nr >= AI_ROOMS || (ns == stage && nr == room)) continue;
        bool dup = false;
        for (int k = 0; k < r.count && !dup; k++) dup = r.stage[k] == ns && r.room[k] == nr;
        if (dup) continue;
        r.stage[r.count] = ns;
        r.room[r.count] = nr;
        r.dest[r.count] = doors[i].dest;
        r.lock[r.count] = doors[i].lock;
        r.count++;
    }
    return &r;
}

static const AiRoom* ai_arrivals(unsigned char stage, unsigned char room)
{
    ai_room(stage, room);
    AiRoom& r = s_graph[stage][room];
    if (r.arrBuilt) return &r;
    r.arrBuilt = true;
    r.arrCount = 0;
    for (int n = 0; n < r.count; n++) {
        const ZmDoor* doors;
        int nd = zm_room_doors(r.stage[n], r.room[n], &doors);
        for (int i = 0; i < nd && r.arrCount < AI_MAX_LINKS * 2; i++) {
            if ((doors[i].flags0B & 0x80) != 0) continue;              // camera-only
            unsigned char ds, dr;
            zm_decode_dest(doors[i].dest, r.stage[n], &ds, &dr);
            if (ds != stage || dr != room) continue;
            r.arrX[r.arrCount] = doors[i].arriveX;
            r.arrZ[r.arrCount] = doors[i].arriveZ;
            r.arrFrom[r.arrCount] = (unsigned short)(r.stage[n] << 8 | r.room[n]);
            r.arrCount++;
        }
    }
    return &r;
}

// Doors from (stage, room) outward, `max` deep: a key-locked door still shut
// on this copy counts two, the dead elevator / keypad door is no way at all.
static void ai_distances(unsigned char stage, unsigned char room, int max,
                         unsigned char dist[AI_STAGES][AI_ROOMS],
                         unsigned short from[AI_STAGES][AI_ROOMS] = NULL)
{
    memset(dist, AI_FAR, AI_STAGES * AI_ROOMS);
    if (from != NULL) memset(from, 0xFF, AI_STAGES * AI_ROOMS * sizeof(unsigned short));
    if (stage >= AI_STAGES || room >= AI_ROOMS) return;
    static unsigned short queue[AI_STAGES * AI_ROOMS * 4];
    int head = 0, tail = 0;
    dist[stage][room] = 0;
    queue[tail++] = (unsigned short)(stage << 8 | room);
    while (head < tail) {
        unsigned char s = (unsigned char)(queue[head] >> 8), r = (unsigned char)queue[head];
        head++;
        int d = dist[s][r];
        if (d >= max) continue;
        const AiRoom* a = ai_room(s, r);
        for (int i = 0; i < a->count; i++) {
            if (zm_access_door_closed(s, r, a->dest[i])) continue;
            bool locked = (a->lock[i] & 0x80) != 0 && Flg_ck((int)g_LocksFlags, a->lock[i] & 0x3F) == 0;
            int nd = d + (locked ? 2 : 1);
            unsigned char ns = a->stage[i], nr = a->room[i];
            if (nd > max || nd >= dist[ns][nr]) continue;
            dist[ns][nr] = (unsigned char)nd;
            if (from != NULL) from[ns][nr] = (unsigned short)(s << 8 | r);   // came in from here
            if (tail < (int)(sizeof(queue) / sizeof(queue[0]))) queue[tail++] = (unsigned short)(ns << 8 | nr);
        }
    }
}

// ---------------------------------------------------------------------------
// The survivors, as the AI sees them
// ---------------------------------------------------------------------------
struct AiSurvivor {
    bool          valid, dead;
    unsigned char stage, room;
    unsigned char prevStage, prevRoom;
    short         health;
    unsigned int  enteredMs;     // came into this room
    unsigned int  pressedMs;     // last reinforcement / trap aimed at it here
};
// The monster it is saving for (0xFF none) - chosen before it could afford
// it, bought once it can, given up at s_planUntilMs.
#define AI_PLAN_MS 60000
static unsigned char s_planId = 0xFF;
static unsigned int  s_planUntilMs = 0;
static AiSurvivor   s_surv[ZM_NET_MAX_PLAYERS];
static unsigned int s_bigHoldUntilMs = 0;  // no Hunter, Chimera or Tyrant before this

static void ai_read_survivors(unsigned int now)
{
    // Every survivor, by seat - the host's own among them in its own game.
    ZmSurvivorInfo list[ZM_NET_MAX_PLAYERS];
    int count = zm_survivor_list(list, ZM_NET_MAX_PLAYERS);
    for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
        AiSurvivor& v = s_surv[i];
        const ZmSurvivorInfo* info = NULL;
        for (int k = 0; k < count; k++) if (list[k].player == i) info = &list[k];
        if (info == NULL) { v.valid = false; continue; }
        unsigned char stage = info->stage, room = info->room;
        short health = info->health;
        bool dead = info->dead;
        if (!v.valid) {
            v.valid = true;
            v.dead = dead;
            v.stage = v.prevStage = stage;
            v.room = v.prevRoom = room;
            v.health = health;
            v.enteredMs = now;
            v.pressedMs = 0;
            continue;
        }
        if (stage != v.stage || room != v.room) {
            v.prevStage = v.stage;
            v.prevRoom = v.room;
            v.stage = stage;
            v.room = room;
            v.enteredMs = now;
            v.pressedMs = 0;
        }
        // A death or a survivor newly in danger: the big monsters wait.
        if ((dead && !v.dead) || (!dead && health < AI_LOW_HEALTH && v.health >= AI_LOW_HEALTH)) {
            s_bigHoldUntilMs = now + AI_BIG_HOLD_MS;
            if (s_planId != 0xFF && ai_threat(s_planId) >= AI_BIG_THREAT) s_planId = 0xFF;
            dbg_printf("[ai] survivor %d %s: big monsters held %u s\n", i, dead ? "died" : "badly hurt",
                       AI_BIG_HOLD_MS / 1000);
        }
        v.dead = dead;
        v.health = health;
    }
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static const ZmAiLevel* s_level = &kLevels[0];
static unsigned int s_thinkMs = 0;
static unsigned int s_minuteMs = 0;
static int          s_minuteSpent = 0;
static bool         s_opened = false;
static bool         s_tyrantPlaced = false;
static int          s_threatAhead = 0;         // x10, as last measured (ai_route)
static unsigned int s_nextMarchMs = 0;
struct AiMarch {
    bool          used;
    unsigned short uid;                        // its roster uid now (new each room)
    unsigned char id, stage, room, toStage, toRoom;
    unsigned int  nextMs;
    int           waits;
};
static AiMarch s_march[AI_MARCHES];
static int          s_threatTarget = 0;
static char         s_lastAction[40];

// Only an explicit "AI DIRECTOR" match runs the AI. The config value is
// the lobby's default difficulty, never an autopilot for a human director.
int zm_ai_level(void)
{
    if (!g_bPlayAsZombie || !zombie_mode_armed() || !zm_match_authority()) return 0;
    if (zm_ai_hosted()) return zm_net_ai_level();
    return 0;
}

bool zm_ai_hosted(void)
{
    return zm_net_role() == ZM_NET_ZOMBIE && zm_net_ai_level() > 0;
}

const char* zm_ai_level_name(int level)
{
    return (level > 0 && level <= 5) ? kLevels[level].name : kLevels[0].name;
}

int zm_ai_income_pct(void) { return kLevels[zm_ai_level()].incomePct; }

void zm_ai_new_game(void)
{
    s_level = &kLevels[zm_ai_level()];
    memset(s_graph, 0, sizeof(s_graph));
    memset(s_surv, 0, sizeof(s_surv));
    s_bigHoldUntilMs = 0;
    s_thinkMs = zm_game_time_ms();
    s_minuteMs = s_thinkMs;
    s_minuteSpent = 0;
    s_opened = false;
    s_tyrantPlaced = false;
    s_threatAhead = s_threatTarget = 0;
    s_nextMarchMs = 0;
    memset(s_march, 0, sizeof(s_march));
    s_planId = 0xFF;
    s_planUntilMs = 0;
    s_lastAction[0] = '\0';
    if (zm_ai_level() > 0) dbg_printf("[ai] director autopilot: %s\n", s_level->name);
}

static void ai_note(const char* fmt, const char* what, unsigned char stage, unsigned char room)
{
    const char* rn = DebugRoom_Name(stage, room);
    snprintf(s_lastAction, sizeof(s_lastAction), fmt, what);
    dbg_printf("[ai] %s - stage %d room %02X %s (%d pts, threat ahead %d of %d)\n", s_lastAction,
               (int)stage, (int)room, rn != NULL ? rn : "", zm_econ_points(), s_threatAhead, s_threatTarget);
}

// ---------------------------------------------------------------------------
// Pacing: the threat target
// ---------------------------------------------------------------------------
static int ai_alive_survivors(void)
{
    int alive = 0;
    for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) if (s_surv[i].valid && !s_surv[i].dead) alive++;
    return alive;
}

// The threat the route ahead should hold now: rising with the match clock to
// the level's ceiling, a quarter more for each living survivor past one.
static int ai_threat_target(void)
{
    int in = zm_survivors_in_ms();
    int minutes = in > 0 ? in / 60000 : 0;
    int target = s_level->threatBase + s_level->threatPerMin * minutes;
    if (target > s_level->threatMax) target = s_level->threatMax;
    int alive = ai_alive_survivors();
    if (alive > 1) target = target * (100 + 25 * (alive - 1)) / 100;
    return target;
}

static void ai_minute(unsigned int now)
{
    if (now - s_minuteMs >= 60000) {
        s_minuteMs = now;
        s_minuteSpent = 0;
    }
}

// ---------------------------------------------------------------------------
// Choosing
// ---------------------------------------------------------------------------

// Rooms scored by the survivors' ways forward, and how much each room's
// monsters count toward the threat ahead (s_ahead, x10: the nearest rooms
// fully, further ones less; the room a survivor just left not at all).
// `originStage/Room` stands in for the survivors before any are in (the
// opening: the main hall). Leaves s_threatAhead measured.
static int            s_score[AI_STAGES][AI_ROOMS];
static unsigned char  s_ahead[AI_STAGES][AI_ROOMS];
// The room a survivor will most likely come in from (the nearest one's way):
// stage << 8 | room, AI_NO_ENTRY none.
static unsigned short s_entry[AI_STAGES][AI_ROOMS];
static unsigned char  s_entryDist[AI_STAGES][AI_ROOMS];

static int ai_room_threat(unsigned char stage, unsigned char room)
{
    unsigned char ids[32];
    int n = zm_world_room_extra_ids(stage, room, ids, 32), t = 0;
    for (int i = 0; i < n; i++) t += ai_threat(ids[i]);
    return t;
}

static void ai_measure_threat(void)
{
    int total = 0;
    for (int s = 0; s < AI_STAGES; s++)
        for (int r = 0; r < AI_ROOMS; r++)
            if (s_ahead[s][r]) total += ai_room_threat((unsigned char)s, (unsigned char)r) * s_ahead[s][r] / 10;
    s_threatAhead = total;
}

static void ai_note_entries(unsigned char dist[AI_STAGES][AI_ROOMS], unsigned short from[AI_STAGES][AI_ROOMS])
{
    for (int s = 0; s < AI_STAGES; s++)
        for (int r = 0; r < AI_ROOMS; r++)
            if (dist[s][r] > 0 && dist[s][r] < s_entryDist[s][r]) {
                s_entryDist[s][r] = dist[s][r];
                s_entry[s][r] = from[s][r];
            }
}

// A survivor's likely way: to the nearest room still holding a key or crest
// (in doors), else to the storeroom and its exit. The rooms on it, by their
// distance from the survivor, into route[] (0 not on it). False: no goal.
#define AI_GOAL_RANGE 14
static bool ai_survivor_way(const AiSurvivor& v, unsigned char route[AI_STAGES][AI_ROOMS])
{
    static unsigned char dist[AI_STAGES][AI_ROOMS];
    static unsigned short from[AI_STAGES][AI_ROOMS];
    memset(route, 0, AI_STAGES * AI_ROOMS);
    ai_distances(v.stage, v.room, AI_GOAL_RANGE, dist, from);
    int gs = -1, gr = -1, best = AI_FAR;
    for (int s = 0; s < AI_STAGES; s++)
        for (int r = 0; r < AI_ROOMS; r++)
            if (dist[s][r] > 0 && dist[s][r] < best && zm_random_room_key_left((unsigned char)s, (unsigned char)r)) {
                best = dist[s][r]; gs = s; gr = r;
            }
    if (gs < 0) {
        unsigned char exitStage = (v.stage >= 5) ? STAGE_MANSION_RETURN_1F : STAGE_MANSION_1F;
        if (dist[exitStage][ROOM_STOREROOM] == AI_FAR || dist[exitStage][ROOM_STOREROOM] == 0) return false;
        gs = exitStage; gr = ROOM_STOREROOM;
    }
    for (int s = gs, r = gr, guard = 0; guard < AI_GOAL_RANGE + 2; guard++) {
        if (dist[s][r] == 0) break;
        route[s][r] = dist[s][r];
        unsigned short f = from[s][r];
        if (f == AI_NO_ENTRY) break;
        s = f >> 8; r = f & 0xFF;
    }
    return true;
}

static void ai_route(bool opening, unsigned char originStage, unsigned char originRoom)
{
    static unsigned char dist[AI_STAGES][AI_ROOMS];
    static unsigned short from[AI_STAGES][AI_ROOMS];
    static unsigned char route[AI_STAGES][AI_ROOMS];
    memset(s_entry, 0xFF, sizeof(s_entry));
    memset(s_entryDist, AI_FAR, sizeof(s_entryDist));
    static const int kAhead[4] = { 0, 20, 35, 25 };
    static const unsigned char kWeight[4] = { 0, 10, 8, 6 };
    memset(s_score, 0, sizeof(s_score));
    memset(s_ahead, 0, sizeof(s_ahead));
    int look = s_level->lookahead;
    bool any = false;
    for (int i = 0; i < ZM_NET_MAX_PLAYERS && !opening; i++) {
        const AiSurvivor& v = s_surv[i];
        if (!v.valid || v.dead) continue;
        any = true;
        // Its likely way first: the next rooms on it are where the threat
        // has to stand (full weight, one door further than the lookahead);
        // the other rooms around it count a little, as a detour.
        bool way = ai_survivor_way(v, route);
        ai_distances(v.stage, v.room, look + 1, dist, from);
        ai_note_entries(dist, from);
        int weight = (s_level->focusWeak && v.health < AI_LOW_HEALTH) ? 2 : 1;
        for (int s = 0; s < AI_STAGES; s++) {
            for (int r = 0; r < AI_ROOMS; r++) {
                int d = dist[s][r];
                bool onWay = way && route[s][r] > 0 && route[s][r] <= look + 1;
                if (d == 0 || (d > look && !onWay)) continue;
                bool behind = s == v.prevStage && r == v.prevRoom;      // where it came from
                int add = behind ? -60 : onWay ? 60 : kAhead[d > 3 ? 3 : d] / 2;
                s_score[s][r] += add * weight;
                unsigned char w = behind ? 0 : onWay ? 10 : (way ? 3 : kWeight[d > 3 ? 3 : d]);
                if (w > s_ahead[s][r]) s_ahead[s][r] = w;
            }
        }
    }
    if (!any) {
        ai_distances(originStage, originRoom, 3, dist, from);
        ai_note_entries(dist, from);
        for (int s = 0; s < AI_STAGES; s++)
            for (int r = 0; r < AI_ROOMS; r++)
                if (dist[s][r] > 0 && dist[s][r] <= 3) {
                    s_score[s][r] = kAhead[dist[s][r]];
                    s_ahead[s][r] = kWeight[dist[s][r]];
                }
    }
    // What is in each candidate room.
    for (int s = 0; s < AI_STAGES; s++) {
        for (int r = 0; r < AI_ROOMS; r++) {
            if (s_score[s][r] <= 0) continue;
            if (zm_random_room_key_left((unsigned char)s, (unsigned char)r)) s_score[s][r] += 30;
            unsigned char ids[8], qty[8];
            bool isNew[8];
            if (zm_random_room_items((unsigned char)s, (unsigned char)r, ids, qty, isNew, 8) > 0)
                s_score[s][r] += 8;
            s_score[s][r] -= 12 * zm_room_monster_slots((unsigned char)s, (unsigned char)r);
            s_score[s][r] += rand() % 11;
        }
    }
    ai_measure_threat();
}

// The best-scoring room some monster may go into now. False: none.
// The best-scoring room a monster of type `id` (0xFF: any) may go into,
// price aside. False: none.
static bool ai_best_room(unsigned char* outStage, unsigned char* outRoom, unsigned char id = 0xFF)
{
    int best = 0;
    for (int s = 0; s < AI_STAGES; s++) {
        for (int r = 0; r < AI_ROOMS; r++) {
            if (s_score[s][r] <= best) continue;
            if (id != 0xFF ? !zm_director_move_allowed((unsigned char)s, (unsigned char)r, id)
                           : !zm_director_move_allowed((unsigned char)s, (unsigned char)r, ENEMY_ZOMBIE) &&
                             !zm_director_move_allowed((unsigned char)s, (unsigned char)r, ENEMY_CERBERUS)) continue;
            best = s_score[s][r];
            *outStage = (unsigned char)s;
            *outRoom = (unsigned char)r;
        }
    }
    return best > 0;
}

// Points to keep back: the price of a big monster the Yawn the survivors are
// fighting now will unlock, not bought yet (normal and above).
static int ai_reserve(void)
{
    if (s_level->incomePct < 100) return 0;
    static const unsigned char big[] = { ENEMY_HUNTER, ENEMY_CHIMERA, ENEMY_TYRANT_2 };
    int reserve = 0;
    for (unsigned int i = 0; i < sizeof(big); i++) {
        if (big[i] == ENEMY_TYRANT_2 && s_tyrantPlaced) continue;
        if (zm_econ_locked(big[i]) != 1 || !zm_yawn_fight_on()) continue;
        int cost = zm_econ_cost(big[i]);
        if (cost > reserve) reserve = cost;
    }
    return reserve;
}

// A monster for (stage, room), weighted by the room's capacity and sized to
// the threat `gap` (x10) the route ahead is short of: a big one only for a
// gap nearly its size, and not while they are held back; one larger than the
// gap less often; with a large gap, the bigger ones more often.
static bool ai_pick_monster(unsigned char stage, unsigned char room, bool opening, int gap, unsigned char* outId)
{
    bool bigHeld = (int)(zm_game_time_ms() - s_bigHoldUntilMs) < 0;
    struct { unsigned char id; int weight; } c[8];
    int n = 0;
    int cap = zm_econ_room_cap(stage, room);
    int used = zm_room_monster_slots(stage, room);
    int freeSlots = cap - used;
    bool objective = zm_random_room_key_left(stage, room);
    int points = zm_econ_points();
    int reserve = opening ? 0 : ai_reserve();

    const struct { unsigned char id; int weight; } base[] = {
        { ENEMY_ZOMBIE,          40 },
        { ENEMY_ZOMBIE_VARIANT,  25 },
        { ENEMY_ZOMBIE_NAKED,    20 },
        { ENEMY_WEB_SPINNER,     cap >= 3 ? 5 : 0 },
        { ENEMY_CERBERUS,        cap <= 3 ? 60 : 35 },          // corridors
        { ENEMY_HUNTER,          freeSlots >= 2 ? 45 : 0 },
        { ENEMY_CHIMERA,         freeSlots >= 2 ? 30 : 0 },
        { ENEMY_TYRANT_2,        (!s_tyrantPlaced && used == 0 && (objective || s_level->traps)) ? 80 : 0 },
    };
    int total = 0;
    for (unsigned int i = 0; i < sizeof(base) / sizeof(base[0]); i++) {
        if (base[i].weight <= 0) continue;
        // Unlocked and fitting; the price is the saving's business (ai_place_one).
        if (zm_econ_locked(base[i].id) != 0 || !zm_director_move_allowed(stage, room, base[i].id)) continue;
        int cost = zm_econ_cost(base[i].id);
        // Small fry would eat the savings for a big one about to unlock.
        if (reserve > 0 && cost < reserve && points - cost < reserve) continue;
        int threat = ai_threat(base[i].id);
        int weight = base[i].weight;
        if (threat >= AI_BIG_THREAT && (bigHeld || gap < threat * 8 / 10)) continue;
        if (threat > gap) weight /= 4;
        else if (gap >= 2 * AI_BIG_THREAT && threat >= 20) weight *= 2;
        if (weight <= 0) continue;
        c[n].id = base[i].id;
        c[n].weight = weight;
        total += c[n].weight;
        n++;
    }
    if (n == 0) return false;
    int roll = rand() % total;
    for (int i = 0; i < n; i++) {
        roll -= c[i].weight;
        if (roll < 0) { *outId = c[i].id; return true; }
    }
    *outId = c[n - 1].id;
    return true;
}

// Distance on the floor. Room coordinates run to 65535: squares need 64 bits
// (SquareRoot0's 32-bit form overflows past a 46340 diagonal).
static int ai_dist(int dx, int dz)
{
    return (int)sqrt((double)((long long)dx * dx + (long long)dz * dz));
}

// Where in (stage, room) a monster `id` goes: one of the room's finer spots,
// written to at[4] = { x, y, z, angle }. False: the room has none - the map's
// own spot is used then.
static bool ai_pick_spot(unsigned char stage, unsigned char room, unsigned char id, short at[4])
{
    const ZmPlaceSpots* t = NULL;
    for (int i = 0; i < g_zmPlaceSpotCount && t == NULL; i++)
        if (g_zmPlaceSpots[i].stage == stage && g_zmPlaceSpots[i].room == room) t = &g_zmPlaceSpots[i];
    if (t == NULL || t->count == 0) return false;

    // Where the survivors come in: the arrival point of the door from the
    // room the search came from, else none (the nearest arrival then counts).
    const AiRoom* arr = ai_arrivals(stage, room);
    int ex = -1, ez = -1;
    unsigned short entry = s_entry[stage][room];
    for (int i = 0; i < arr->arrCount && entry != AI_NO_ENTRY; i++) {
        if (arr->arrFrom[i] != entry) continue;
        ex = (unsigned short)arr->arrX[i];
        ez = (unsigned short)arr->arrZ[i];
        break;
    }

    // The type's place: how far from that door (lo..hi), how open, and the
    // least open floor a big body needs.
    int threat = ai_threat(id), lo, hi, openWant, openMin = 0;
    if (id == ENEMY_CERBERUS)              { lo = 5000; hi = 9000; openWant = 1;  }
    else if (id == ENEMY_WEB_SPINNER)      { lo = 3000; hi = 7000; openWant = 0;  }
    else if (id == ENEMY_TYRANT_2)         { lo = 4000; hi = 9000; openWant = 2;  openMin = 30; }
    else if (threat >= AI_BIG_THREAT)      { lo = 3000; hi = 6000; openWant = 2;  openMin = 20; }
    else if (s_level->ambush)              { lo = AI_STUN_CLEAR; hi = 3800; openWant = -1; }   // just past the stun
    else                                   { lo = 3000; hi = 4500; openWant = -1; }

    unsigned char ids[32];
    short xz[64];
    int others = zm_world_room_extra_ids(stage, room, ids, 32, xz);
    int best = -1, bestScore = -0x7FFFFFFF;
    for (int k = 0; k < t->count; k++) {
        const ZmPlaceSpot& sp = t->spot[k];
        if (sp.open < openMin) continue;
        int score = 0;
        // Never where a survivor coming through any door would stun it.
        int nearest = 0x7FFFFFFF;
        for (int i = 0; i < arr->arrCount; i++) {
            int dd = ai_dist(sp.x - (unsigned short)arr->arrX[i], sp.z - (unsigned short)arr->arrZ[i]);
            if (dd < nearest) nearest = dd;
        }
        if (nearest < AI_STUN_CLEAR) continue;
        int d = ex >= 0 ? ai_dist(sp.x - ex, sp.z - ez) : nearest;
        if (d < lo) score -= (lo - d) * 3;
        else if (d > hi) score -= d - hi;
        if (openWant < 0) score += (49 - sp.open) * 20; // the narrow places
        else score += sp.open * 10 * openWant;
        bool crowded = false;
        for (int i = 0; i < others && !crowded; i++) {
            // Roster x / z keep the low 16 bits of a 0..65535 coordinate.
            int dd = ai_dist(sp.x - (unsigned short)xz[i * 2], sp.z - (unsigned short)xz[i * 2 + 1]);
            if (dd < 600) crowded = true;
            else if (dd < 1200) score -= 3000;
        }
        if (crowded) continue;
        score += rand() % 400;
        if (score > bestScore) { bestScore = score; best = k; }
    }
    if (best < 0) return false;
    const ZmPlaceSpot& sp = t->spot[best];
    at[0] = sp.x;
    at[1] = t->y;
    at[2] = sp.z;
    // Facing the way they come in (any way, without one).
    if (ex >= 0) at[3] = (short)CalculateAngleBetweenPointsXZ(sp.x, sp.z, ex, ez);
    else at[3] = (short)(rand() & 0xFFF);
    return true;
}

static int ai_marching_threat(void);    // with the marching below

static bool ai_place_one(bool opening)
{
    unsigned char stage, room, id;
    int gap = s_threatTarget - s_threatAhead - (opening ? 0 : ai_marching_threat());
    if (gap <= 0) return false;                 // the way ahead holds enough (or is on its way)
    unsigned int now = zm_game_time_ms();
    if (s_planId != 0xFF && (int)(now - s_planUntilMs) >= 0) {
        dbg_printf("[ai] gave up saving for a %s\n", ai_name(s_planId));
        s_planId = 0xFF;
    }
    if (s_planId != 0xFF && !opening) {
        // Saving: nothing else until the planned one is affordable.
        id = s_planId;
        if (zm_econ_points() < zm_econ_cost(id)) return false;
        if (!ai_best_room(&stage, &room, id)) { s_planId = 0xFF; return false; }
    } else {
        if (!ai_best_room(&stage, &room)) return false;
        if (!ai_pick_monster(stage, room, opening, gap, &id)) {
            s_score[stage][room] = 0;       // try elsewhere next time
            return false;
        }
        if (zm_econ_points() < zm_econ_cost(id)) {
            if (opening) return false;
            s_planId = id;                  // save up for it
            s_planUntilMs = now + AI_PLAN_MS;
            dbg_printf("[ai] saving for a %s (%d of %d points)\n", ai_name(id), zm_econ_points(), zm_econ_cost(id));
            return false;
        }
    }
    int cost = zm_econ_cost(id);
    // The minute's cap; its first purchase may go over (a Tyrant).
    if (!opening && s_minuteSpent > 0 && s_minuteSpent + cost > s_level->spendPerMin) return false;
    // Only on a spot clear of the door stun (the map's own spots may not be).
    short at[4];
    if (!ai_pick_spot(stage, room, id, at)) {
        s_score[stage][room] = 0;
        return false;
    }
    if (!zm_director_place_ai(stage, room, id, ai_name(id), at)) {
        s_score[stage][room] = 0;
        return false;
    }
    dbg_printf("[ai] %s at (%d, %d) in stage %d room %02X, facing %03X, from room %04X\n",
                         ai_name(id), at[0], at[2], (int)stage, (int)room, (unsigned)(at[3] & 0xFFF),
                         (unsigned)s_entry[stage][room]);
    if (!opening) s_minuteSpent += cost;
    if (id == s_planId) s_planId = 0xFF;
    if (id == ENEMY_TYRANT_2) s_tyrantPlaced = true;
    // Spread the next one: this room is less attractive now.
    s_score[stage][room] -= 25;
    s_threatAhead += ai_threat(id) * s_ahead[stage][room] / 10;
    ai_note("%s PLACED", ai_name(id), stage, room);
    return true;
}

// ---------------------------------------------------------------------------
// Marching: idle monsters walked toward the way ahead, unseen
// ---------------------------------------------------------------------------
static unsigned int ai_hop_ms(unsigned char id)
{
    for (unsigned int i = 0; i < sizeof(kHop) / sizeof(kHop[0]); i++)
        if (kHop[i].id == id) return kHop[i].ms * (unsigned int)s_level->hopPct / 100;
    return 20000;
}

static int ai_marching(void)
{
    int n = 0;
    for (int i = 0; i < AI_MARCHES; i++) if (s_march[i].used) n++;
    return n;
}

// The threat already on its way: marches not yet in a room ahead.
static int ai_marching_threat(void)
{
    int t = 0;
    for (int i = 0; i < AI_MARCHES; i++)
        if (s_march[i].used && !s_ahead[s_march[i].stage][s_march[i].room]) t += ai_threat(s_march[i].id);
    return t;
}

static bool ai_is_marching(unsigned short uid)
{
    for (int i = 0; i < AI_MARCHES; i++) if (s_march[i].used && s_march[i].uid == uid) return true;
    return false;
}

static void ai_march_end(AiMarch& m, const char* why)
{
    dbg_printf("[ai] march of %s %04X ends in stage %d room %02X: %s\n", ai_name(m.id), m.uid,
               (int)m.stage, (int)m.room, why);
    m.used = false;
}

// Is the march's monster still alive where the march thinks it is?
static bool ai_march_alive(const AiMarch& m)
{
    unsigned char ids[32];
    unsigned short uids[32];
    int n = zm_world_room_extra_ids(m.stage, m.room, ids, 32, NULL, uids);
    for (int i = 0; i < n; i++) if (uids[i] == m.uid) return true;
    return false;
}

// One step of a march: through the door toward its goal, unseen.
static void ai_march_step(AiMarch& m, unsigned int now)
{
    if (!ai_march_alive(m)) { ai_march_end(m, "gone"); return; }
    if (m.stage == m.toStage && m.room == m.toRoom) { ai_march_end(m, "arrived"); return; }
    // Seen where it stands, or the next room seen or full: wait a little.
    static unsigned char dist[AI_STAGES][AI_ROOMS];
    ai_distances(m.toStage, m.toRoom, AI_MARCH_RANGE, dist);
    const AiRoom* a = ai_room(m.stage, m.room);
    int here = dist[m.stage][m.room], best = -1;
    for (int i = 0; i < a->count; i++) {
        if (zm_access_door_closed(m.stage, m.room, a->dest[i])) continue;
        // A key-locked door still shut: no monster opens it unseen.
        if ((a->lock[i] & 0x80) != 0 && Flg_ck((int)g_LocksFlags, a->lock[i] & 0x3F) == 0) continue;
        if (dist[a->stage[i]][a->room[i]] >= here) continue;
        if (best < 0 || dist[a->stage[i]][a->room[i]] < dist[a->stage[best]][a->room[best]]) best = i;
    }
    if (best < 0) { ai_march_end(m, "no way on"); return; }
    unsigned char ns = a->stage[best], nr = a->room[best];
    if (zm_room_watched(m.stage, m.room) || zm_room_watched(ns, nr) || !zm_director_move_allowed(ns, nr, m.id)) {
        if (++m.waits > AI_MARCH_WAITS) { ai_march_end(m, "blocked"); return; }
        m.nextMs = now + 2000;
        return;
    }
    // Where it stands in each room: one of its spots, as if it came through
    // the door it did, facing it - never in the door stun's reach. None free:
    // it waits.
    short at[4];
    unsigned short entry = s_entry[ns][nr];
    s_entry[ns][nr] = (unsigned short)(m.stage << 8 | m.room);
    bool spot = ai_pick_spot(ns, nr, m.id, at);
    s_entry[ns][nr] = entry;
    if (!spot) {
        if (++m.waits > AI_MARCH_WAITS) { ai_march_end(m, "no spot"); return; }
        m.nextMs = now + 2000;
        return;
    }
    unsigned short uid = zm_world_move_extra(m.stage, m.room, m.uid, ns, nr, at[0], at[1], at[2], at[3]);
    if (uid == 0) { ai_march_end(m, "could not move"); return; }
    dbg_printf("[ai] %s walks to stage %d room %02X (goal %d/%02X)\n", ai_name(m.id), (int)ns, (int)nr,
               (int)m.toStage, (int)m.toRoom);
    m.uid = uid;
    m.stage = ns;
    m.room = nr;
    m.waits = 0;
    m.nextMs = now + ai_hop_ms(m.id);
}

static void ai_march_frame(unsigned int now)
{
    for (int i = 0; i < AI_MARCHES; i++) {
        AiMarch& m = s_march[i];
        if (m.used && (int)(now - m.nextMs) >= 0) ai_march_step(m, now);
    }
}

// Start a march when the way ahead is short: the nearest idle monster that
// is off the way ahead and in a room nobody sees, to the best room ahead that
// will take it. Only when the route is measured (ai_route) this decision.
static bool ai_march_start(unsigned int now, int gap)
{
    if (s_level->marchEveryMs == 0 || gap <= 0 || ai_marching() >= s_level->marchMax) return false;
    if ((int)(now - s_nextMarchMs) < 0) return false;
    // The goal: the best-scoring room ahead.
    int bestScore = 0;
    int gs = -1, gr = -1;
    for (int st = 0; st < AI_STAGES; st++)
        for (int r = 0; r < AI_ROOMS; r++)
            if (s_ahead[st][r] && s_score[st][r] > bestScore && !zm_room_watched((unsigned char)st, (unsigned char)r)) {
                bestScore = s_score[st][r]; gs = st; gr = r;
            }
    if (gs < 0) return false;
    static unsigned char dist[AI_STAGES][AI_ROOMS];
    ai_distances((unsigned char)gs, (unsigned char)gr, AI_MARCH_RANGE, dist);
    int bestDist = AI_FAR;
    AiMarch pick = {};
    for (int st = 0; st < AI_STAGES; st++) {
        for (int r = 0; r < AI_ROOMS; r++) {
            int d = dist[st][r];
            if (d == 0 || d >= bestDist || s_ahead[st][r]) continue;
            if (zm_room_watched((unsigned char)st, (unsigned char)r)) continue;
            unsigned char ids[32];
            unsigned short uids[32];
            int n = zm_world_room_extra_ids((unsigned char)st, (unsigned char)r, ids, 32, NULL, uids);
            for (int i = 0; i < n; i++) {
                if (uids[i] == 0 || ai_is_marching(uids[i])) continue;
                if (!zm_director_move_allowed((unsigned char)gs, (unsigned char)gr, ids[i])) continue;
                bestDist = d;
                pick.used = true;
                pick.uid = uids[i];
                pick.id = ids[i];
                pick.stage = (unsigned char)st;
                pick.room = (unsigned char)r;
                break;
            }
        }
    }
    if (!pick.used) return false;
    pick.toStage = (unsigned char)gs;
    pick.toRoom = (unsigned char)gr;
    pick.nextMs = now;                 // the first step at once
    for (int i = 0; i < AI_MARCHES; i++) {
        if (s_march[i].used) continue;
        s_march[i] = pick;
        s_nextMarchMs = now + s_level->marchEveryMs;
        ai_note("%s MARCHES", ai_name(pick.id), pick.toStage, pick.toRoom);
        dbg_printf("[ai] ...from stage %d room %02X, %d doors away\n", (int)pick.stage, (int)pick.room, bestDist);
        return true;
    }
    return false;
}

// A survivor lingering in one room: a zombie (a Hunter, hard and up) through
// one of its doors. The host checks the door and the room's owner validates it.
static bool ai_reinforce(unsigned int now)
{
    if (s_level->dwellMs == 0 || zm_net_role() != ZM_NET_ZOMBIE) return false;
    if ((int)(now - s_bigHoldUntilMs) < 0) return false;    // just after a death or a bad hurt
    if (s_planId != 0xFF) return false;                      // saving for something bigger
    for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
        AiSurvivor& v = s_surv[i];
        if (!v.valid || v.dead || now - v.enteredMs < s_level->dwellMs) continue;
        if (v.pressedMs != 0 && now - v.pressedMs < s_level->dwellMs) continue;
        if (zm_random_room_safe(v.stage, v.room) || zm_yawn_director_closed(v.stage, v.room)) continue;
        v.pressedMs = now;
        unsigned char id = ENEMY_ZOMBIE;
        if (s_level->traps && zm_econ_locked(ENEMY_HUNTER) == 0 &&
            zm_econ_points() >= zm_econ_cost(ENEMY_HUNTER)) id = ENEMY_HUNTER;
        char why[48];
        if (zm_reinforce_request(v.stage, v.room, id, why, sizeof(why))) {
            s_minuteSpent += zm_econ_cost(id);
            ai_note("%s REINFORCES", ai_name(id), v.stage, v.room);
            return true;
        }
        dbg_printf("[ai] reinforcement refused: %s\n", why);
    }
    return false;
}

// A hurt survivor in a room with monsters: its doors shut for a while.
static bool ai_trap(unsigned int now)
{
    if (!s_level->traps || zm_trap_cooldown_ms(ZM_TRAP_LOCK_DOORS) > 0) return false;
    for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
        AiSurvivor& v = s_surv[i];
        if (!v.valid || v.dead || v.health >= AI_LOW_HEALTH) continue;
        if (zm_random_room_safe(v.stage, v.room) || zm_yawn_director_closed(v.stage, v.room)) continue;
        if (zm_room_monster_count(v.stage, v.room) <= 0) continue;
        if (!zm_director_place_ai(v.stage, v.room, ZM_TRAP_LOCK_DOORS, ai_name(ZM_TRAP_LOCK_DOORS))) continue;
        v.pressedMs = now;
        ai_note("%s", "DOORS LOCKED", v.stage, v.room);
        return true;
    }
    return false;
}

// The opening: as soon as the game is on, part of the starting points go into
// the rooms around the main hall (its doors' rooms, and theirs).
static void ai_opening(void)
{
    s_opened = true;
    unsigned char hallStage = (g_stageId % 5 == STAGE_MANSION_1F) ? g_stageId
        : (Flg_ck((int)&g_ScenarioFlags, SCENARIO_FLAG_STAGE_VARIANT) != 0 ? 5 : STAGE_MANSION_1F);
    int budget = zm_econ_points() * s_level->openingPct / 100;
    int spentFrom = zm_econ_points();
    ai_route(true, hallStage, ROOM_MAIN_HALL);
    s_threatTarget = s_level->threatBase;
    for (int tries = 0; tries < 24 && spentFrom - zm_econ_points() < budget; tries++) {
        if (!ai_place_one(true) && s_threatAhead >= s_threatTarget) break;
    }
    dbg_printf("[ai] opening: spent %d of %d, threat around the hall %d of %d\n",
               spentFrom - zm_econ_points(), budget, s_threatAhead, s_threatTarget);
}

void zm_ai_frame(void)
{
    if (zm_ai_level() == 0 || !zm_econ_on() || zombie_mode_match_over()) return;
    if (zm_game_role() != ZM_NET_OFF && zm_net_pause_active()) return;
    unsigned int now = zm_game_time_ms();
    ai_read_survivors(now);
    ai_minute(now);
    ai_march_frame(now);
    if (!s_opened) {
        ai_opening();
        s_thinkMs = now;
        return;
    }
    if (now - s_thinkMs < s_level->thinkMs) return;
    s_thinkMs = now;
    // Survivors not in yet: nothing to aim at.
    if (zm_survivors_in_ms() < 0) return;
    if (ai_trap(now)) return;
    if (ai_reinforce(now)) return;
    ai_route(false, 0, 0);
    s_threatTarget = ai_threat_target();
    // Walk a monster up from behind before buying one.
    if (ai_march_start(now, s_threatTarget - s_threatAhead - ai_marching_threat())) return;
    ai_place_one(false);
}

// Under the points, top right: who is playing and what it did last.
void zm_ai_draw(void)
{
    if (zm_ai_level() == 0) return;
    char line[64];
    snprintf(line, sizeof(line), "AI %s THREAT %d.%d OF %d.%d", s_level->name,
             s_threatAhead / 10, s_threatAhead % 10, s_threatTarget / 10, s_threatTarget % 10);
    snprintf(PRINT_TEXT_BUFFER, sizeof(PRINT_TEXT_BUFFER), "%s", line);
    int width = (int)strlen(PRINT_TEXT_BUFFER) * 8;
    zm_text_encode(PRINT_TEXT_BUFFER);
    PrintText8x14((short)(320 - 4 - width), 22, 0x7F, 0);
    if (s_lastAction[0] != '\0') {
        snprintf(PRINT_TEXT_BUFFER, sizeof(PRINT_TEXT_BUFFER), "%s", s_lastAction);
        width = (int)strlen(PRINT_TEXT_BUFFER) * 8;
        zm_text_encode(PRINT_TEXT_BUFFER);
        PrintText8x14((short)(320 - 4 - width), 36, 0x7F, 0);
    }
}
