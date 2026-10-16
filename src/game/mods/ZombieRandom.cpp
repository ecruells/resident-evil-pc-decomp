#include "ZombieModeInternal.h"
#include "ZombieNet.h"
#include "../FileLoader.h"
#include "../../system/AssetPath.h"
#include "../../platform/platform.h"
#include "../../DebugPrint.h"
#include <cstdio>
#include <cstring>
#include <cmath>

// ============================================================================
// ZombieRandom.cpp - port-added: the zombie mod's randomized scenario.
//
// Every game of the mode is a new mansion run (the return mansion, stages 6
// and 7 - zombie_mode_new_game). The survivors still win by RE1's own route:
// the four crests (Wind, Moon, Star, Sun) open room 0x1A's door to the
// storeroom, and the storeroom's back door to the courtyard is the win. What
// changes per game:
//   - which mansion key (sword / armor / shield / helmet) opens each
//     key-locked door;
//   - where those keys and the four crests lie;
//   - which weapon or ammo lies at each weapon / ammo spot.
// The puzzles are left out: every key and crest is a plain pickup, and the
// doors a puzzle or a one-way lock keeps shut start unlocked (all but the
// crest door).
//
// One seed (the host's, handed out in the lobby - ZombieNet.cpp) makes the
// whole scenario: every copy reads the same RDT files and runs the same
// generator, so they agree without sending the result.
//
// The data comes straight from the RDTs, at the start of each game:
//   doors   every door_set (SCD 0x0C) of the room's init and per-frame
//           scripts - lock byte +0x0C (0x80 locked, low 6 bits the lock
//           flag), destination +0x0D, the item it needs +0x16.
//   spots   every item_model_set (0x18) - item +10, quantity +11, the
//           roomItems flag +0x16 (which names the spot), room action slot +1.
// A spot can take a randomized item if its record is at the top level of the
// script (not inside an if block - those are story or puzzle states), no
// room_action_reset / room_action_arm (0x12 / 0x13) re-arms its slot (that is
// how a puzzle hands an item out), it is the only record under its flag, and
// it originally held a key, a crest, a weapon, ammo or a map.
//
// The keys and crests are placed by "assumed fill": in a random order, each
// goes to a random free room's spot reachable from the main hall while every item
// not yet placed is assumed to be in hand - which always leaves a route that
// collects everything. The leftover spots get the shuffled weapons and ammo.
// The route crosses only doors whose triggers are live in the mode
// (kDoorGates), counts a room only when the main hall can be reached back from
// it, and keeps progression out of rooms with unverified furniture
// (kUnverifiedRooms).
// ============================================================================

extern void Flg_on(int baseAddr, unsigned int bitIndex);              // 0x00473ef0 CmdFunctions.cpp
extern void FUN_00473f10(int* baseAddr, unsigned int bitIndex);        // clear flag

#define RND_STAGE_1F     STAGE_MANSION_RETURN_1F     // 5
#define RND_STAGE_2F     STAGE_MANSION_RETURN_2F     // 6
#define RND_ROOMS        0x1D                        // rooms 0x00..0x1C a stage
#define RND_MAX_DOORS    256
#define RND_MAX_SPOTS    192
#define RND_MAX_LOCKS    64
#define RND_INGRAM_ROUNDS 150                        // the candle room's Ingram (rnd_candle_spot)

// ROOM11A0's crest door (loaded for the return mansion by zombie mode): the
// lock flag for the storeroom, 0x1B, which its script raises once all four
// crests are in (bit_test on
// ScenarioFlags 0x69-0x6C, then bit_op LocksFlags 23).
#define RND_CREST_LOCK   23
// The key-locked doors into the attic (from the front of the attic) and into
// the pillar passage (from the C passage): their lock flags (+0x0C & 0x3F).
#define RND_ATTIC_LOCK   0x07
#define RND_PILLAR_LOCK  0x0D
#define RND_LESSON_LOCK  0x19            // front lesson room -> lesson room (Yawn 2's)
#define RND_BATTERY_AWAY 4               // door crossings from the kitchen's elevator, at least
#define RND_ATTIC_ROOM   0x10            // 2F
#define RND_PILLAR_ROOM  0x0D

static const unsigned char kKeys[4]    = { ITEM_SWORD_KEY, ITEM_ARMOR_KEY, ITEM_SHIELD_KEY, ITEM_HELMET_KEY };
static const unsigned char kCrests[4]  = { ITEM_WIND_CREST, ITEM_MOON_CREST, ITEM_STAR_CREST, ITEM_SUN_CREST };

struct RndDoor {
    unsigned char fromStage, fromRoom;
    unsigned char toStage, toRoom;
    unsigned char lock;            // +0x0C
    unsigned char need;            // +0x16
    unsigned char slot;            // room action slot
    bool          gated;           // its trigger is not plainly live (rnd_read_room)
    bool          usable;          // the route may cross it
    unsigned int  access;          // RND_BIT_BATTERY / RND_BIT_NOTE it needs (kAccessDoors), 0 none
};

// Doors whose trigger a script can switch off. Opening a lock flag is not
// the same as an enabled door trigger: room scripts disarm door slots
// (room_action_arm / _reset to handler 0 or dead probe flags), build them in
// if blocks, or leave them unarmed for an event to invoke. Each such door of
// the return mansion is reviewed here against the mode's own flag state
// (zombie_mode_new_game, zombie_mode_room_prepare); one the table does not
// name counts as closed. tools/audit_zombie_puzzle_access.py lists them and
// tests/test_zombie_routes.py keeps the table and the RDTs in step.
enum { RND_GATE_OPEN, RND_GATE_CLOSED, RND_GATE_PUZZLE };
struct RndGate { unsigned char stage, room, slot, state; };
static const RndGate kDoorGates[] = {
    // 1F dining room -> main hall: disarmed only while ScenarioFlags2 bit 2
    // is clear (before the hall's intro scene); the mode sets it.
    // (bit_test's third byte: 0 passes on a set bit, 1 on a clear one.)
    { RND_STAGE_1F, 0x05, 1, RND_GATE_OPEN },
    // Main hall -> dressing room, a second record built for one camera; slot
    // 1 is the same edge, lock and key.
    { RND_STAGE_1F, 0x06, 3, RND_GATE_OPEN },
    // Wardrobe -> wardrobe closet: the costume closet, built only while
    // ScenarioFlags 0x7B (second playthrough) is set - a message otherwise.
    { RND_STAGE_1F, 0x12, 1, RND_GATE_CLOSED },
    // Living room -> trap room: ZombieShotgun.cpp governs it at run time
    // (zm_shotgun_route_open); neither room takes progression.
    { RND_STAGE_1F, ROOM_LIVING_ROOM, 0, RND_GATE_OPEN },
    // The elevator car: 1 x 1 zones its events run by slot - arriving from
    // the 2F front elevator rides to the kitchen, otherwise back up.
    { RND_STAGE_2F, 0x00, 0, RND_GATE_OPEN },
    { RND_STAGE_2F, 0x00, 1, RND_GATE_OPEN },
    // 2F left stairs -> rough passage: the init disarms it unconditionally;
    // the keypad re-arms it once the pass number is keyed in (kAccessDoors).
    { RND_STAGE_2F, 0x01, 1, RND_GATE_OPEN },
    // Deer room -> 2F bedroom: disarmed only while the stage-variant bit is
    // clear (the first visit); the mode always plays the return mansion.
    { RND_STAGE_2F, 0x08, 0, RND_GATE_OPEN },
    // Lesson room <-> B1 passage: unarmed records invoked by the hole's
    // down / up prompts, shut until Yawn 2 there is beaten (zm_yawn_hole_open).
    // Then the hole is the lesson room's only way out (its door to the front
    // lesson room is sealed, kSealedDoors): open downward, so the route leaves
    // the lesson room through the basement. Never counted upward - no way into
    // the lesson room round its locked door.
    { RND_STAGE_2F, ROOM_LESSON_ROOM, 1, RND_GATE_OPEN },
    { RND_STAGE_2F, ROOM_MANSION_B1_PASSAGE_1, 0, RND_GATE_CLOSED },
    // Front of attic -> save room: a 1 x 1 zone at the origin, never touched.
    { RND_STAGE_2F, 0x0E, 3, RND_GATE_CLOSED },
    // Large library -> heliport lookout: re-armed live only on a frame with
    // SysFlags 0x18 set (the bookcase's flag_bank_set zone), dead otherwise;
    // pushing it and the door's clearance are not modelled.
    { RND_STAGE_2F, 0x16, 2, RND_GATE_PUZZLE },
    // Kitchen -> elevator: disarmed while ScenarioFlags 0x33 is clear, which
    // zombie_mode_room_prepare sets before the init.
    { RND_STAGE_2F, ROOM_MANSION_KITCHEN, 2, RND_GATE_OPEN },
};
static int s_unreviewedGates = 0;   // gated doors kDoorGates does not name

// Doors the mode shuts for good, in one direction (each side's record is its
// own): the lesson room's to the front lesson room, sealed once Yawn 2 is
// beaten (ZombieYawn.cpp, zm_yawn_lesson_sealed). The way in - the front
// lesson room's, behind the lesson lock - stays, for the fight.
static const unsigned char kSealedDoors[][3] = {
    { RND_STAGE_2F, ROOM_LESSON_ROOM, 0 },
};

static bool rnd_sealed_door(unsigned char stage, unsigned char room, unsigned char slot)
{
    for (unsigned int i = 0; i < sizeof(kSealedDoors) / sizeof(kSealedDoors[0]); i++)
        if (kSealedDoors[i][0] == stage && kSealedDoors[i][1] == room && kSealedDoors[i][2] == slot) return true;
    return false;
}

// The back area - the 2F back passage and the rooms off it (the rough
// passage, the libraries, the shed) - has two ways in (ZombieKeypad.cpp):
// the small elevator from the kitchen runs once a survivor has put the
// battery in, and the 2F left stairs' keypad door opens on the pass number
// a note tells. Each of those doors needs its access bit in the route, as a
// key door needs its key. The rough passage has no door record back to the
// stairs (its side only says "The door is locked"); rnd_read_room adds one,
// as the keypad builds it in the room (slot 1, over the locked-door zone).
#define RND_BIT_BATTERY 0x1000u
#define RND_BIT_NOTE    0x2000u
// rnd_route_cost's state: the keys and crests (bits 0-7), the battery (8),
// the note (9) and whether room `need` was passed (10).
#define RND_ROUTE_STATES 0x800
#define RND_ROUTE_NEED   0x400u
#define RND_ROUGH_DOOR_SLOT 1
struct RndAccess { unsigned char stage, room, slot; unsigned int bit; };
static const RndAccess kAccessDoors[] = {
    { RND_STAGE_2F, ROOM_MANSION_KITCHEN, 0, RND_BIT_BATTERY },   // kitchen -> elevator car
    { RND_STAGE_2F, 0x13, 1, RND_BIT_BATTERY },                   // 2F back passage -> elevator car
    { RND_STAGE_2F, 0x01, 1, RND_BIT_NOTE },                      // 2F left stairs -> rough passage
    { RND_STAGE_2F, 0x14, RND_ROUGH_DOOR_SLOT, RND_BIT_NOTE },    // rough passage -> 2F left stairs
};

static unsigned int rnd_access_bit(unsigned char stage, unsigned char room, unsigned char slot)
{
    for (unsigned int i = 0; i < sizeof(kAccessDoors) / sizeof(kAccessDoors[0]); i++) {
        const RndAccess& a = kAccessDoors[i];
        if (a.stage == stage && a.room == room && a.slot == slot) return a.bit;
    }
    return 0;
}

// Doors the mode locks with a mansion key of its own: the large gallery's,
// which no key locks in the original, so its puzzle reward would be the
// same first stop every game. Both sides take one lock flag no mansion door
// or script uses, so it joins the key-locked doors (s_lockFlag) and draws its
// key like any other; the record's placeholder need is only there to make it
// a key door (zm_random_door_need maps the flag to this game's key). The
// route reads the patched records (rnd_scan_script), the engine's door_set
// (zombie_mode_door_record) and the AI survivor's door cache too.
#define RND_ADDED_LOCK 0x28
struct RndAddedLock { unsigned char stage, room, slot, dest; };
static const RndAddedLock kAddedLocks[] = {
    { RND_STAGE_1F, 0x0A, 1, 0x17 },   // back passage -> large gallery
    { RND_STAGE_1F, 0x17, 0, 0x0A },   // large gallery -> back passage
};

// A door record's lock (+0x0C) and need (+0x16) with the mode's own locks.
static void rnd_added_lock(unsigned char stage, unsigned char room, unsigned char slot, unsigned char dest,
                           unsigned char* lock, unsigned char* need)
{
    for (unsigned int i = 0; i < sizeof(kAddedLocks) / sizeof(kAddedLocks[0]); i++) {
        const RndAddedLock& a = kAddedLocks[i];
        if (a.stage != stage || a.room != room || a.slot != slot || a.dest != dest) continue;
        *lock = (unsigned char)((*lock & 0x40) | 0x80 | RND_ADDED_LOCK);
        *need = ITEM_SWORD_KEY;
    }
}

// Rooms whose scripts move or swap furniture (events moving an object, or
// object positions chosen by a puzzle flag): a floor pickup there may be
// blocked or need the puzzle. Keys, crests and the puzzle tools stay out until
// each room is play-tested. The piano bar (wall removed), the lesson room
// (hole prepared) and the shotgun rooms (excluded already) are handled.
static const unsigned char kUnverifiedRooms[][2] = {
    { RND_STAGE_1F, 0x05 },    // dining room: the emblem wall
    { RND_STAGE_1F, 0x09 },    // trap passage: event 0 moves object 0
    { RND_STAGE_1F, 0x17 },    // large gallery: the portrait puzzle
    { RND_STAGE_1F, 0x1C },    // wardrobe closet
    { RND_STAGE_2F, 0x02 },    // 2F dining room: the statue
    { RND_STAGE_2F, 0x05 },    // armor room: the statues
    { RND_STAGE_2F, 0x0A },    // 2F study
    { RND_STAGE_2F, 0x16 },    // large library: the bookcase
    { RND_STAGE_2F, 0x17 },    // private library: the switch furniture
};

static bool rnd_room_unverified(unsigned char stage, unsigned char room)
{
    for (unsigned int i = 0; i < sizeof(kUnverifiedRooms) / sizeof(kUnverifiedRooms[0]); i++) {
        if (kUnverifiedRooms[i][0] == stage && kUnverifiedRooms[i][1] == room) return true;
    }
    return false;
}

// A gated door's reviewed state; an unreviewed one is closed.
static int rnd_gate_state(unsigned char stage, unsigned char room, unsigned char slot)
{
    for (unsigned int i = 0; i < sizeof(kDoorGates) / sizeof(kDoorGates[0]); i++) {
        const RndGate& g = kDoorGates[i];
        if (g.stage == stage && g.room == room && g.slot == slot) return g.state;
    }
    s_unreviewedGates++;
    return RND_GATE_CLOSED;
}

struct RndSpot {
    unsigned char stage, room, flag;
    unsigned char origId, origQty;
    bool          pool;            // can take a randomized item
    bool          rewardOnly;      // optional puzzle reward; never keys/crests
    unsigned char puzzle;          // 1 + kPuzzles index: that puzzle's reward slot
    unsigned char id, qty;         // what it holds this game
    bool          overridden;
    // A spot of our own (no record in the room's script): where it lies,
    // its room action slot and item model index (zm_random_room_loaded).
    bool          isNew;
    bool          ammoOnly;        // a new spot held back for the ammo doubling (rnd_fill_ammo)
    unsigned char slot, model;
    short         x, y, z, angle;
};

// Per room: does it exist (not a stub), its item model count (RDT +3; at
// most ZM_RANDOM_MODELS - g_item_model_table) and the highest room action slot
// its scripts use (new spots stay below ZM_DROP_SLOT_FIRST - g_RoomActionTable).
struct RndRoomInfo {
    bool          exists;
    bool          safe;            // a safe room: its script sets an item box (handler 0x08)
    unsigned char itemCount;
    unsigned char highSlot;
};
static RndRoomInfo s_roomInfo[2][0x1D];
static RndRoomInfo* s_curInfo = NULL;
// roomItems flags any mansion script uses (item spots, bank 7 bit ops):
// the new spots take flags from outside this set.
static unsigned char s_usedFlags[32];
#define RND_NEW_SPOTS 16            // extra pickups a game in the pool
#define RND_AMMO_SPOTS 48           // more held back for ammo only (rnd_fill_ammo)
#define RND_NEW_PER_ROOM 3          // one on each of the room's three spawn spots
#ifdef QUICK_DEBUG
#define RND_ROOM_PICKUPS (RND_NEW_PER_ROOM + 17) // eleven guns, four crests, two puzzle tools
#else
#define RND_ROOM_PICKUPS RND_NEW_PER_ROOM
#endif

static RndDoor s_doors[RND_MAX_DOORS];
static int     s_doorCount = 0;
static RndSpot s_spots[RND_MAX_SPOTS];
static int     s_spotCount = 0;
// The one placement that may go into a boss room: Yawn 2's crest (rnd_generate_once).
static bool    s_bossItem = false;

// The scenario
static bool          s_active = false;
static bool          s_built = false;             // the scenario of s_builtSeed is built
static unsigned int  s_seed = 0;
static unsigned char s_lockFlag[RND_MAX_LOCKS];      // the key-locked doors' lock flags
static unsigned char s_lockKey[RND_MAX_LOCKS];       // ...the key each needs now
static int           s_lockCount = 0;

// The heavy weapons come from the mansion's puzzles, one each, by how far
// each puzzle is to solve (rnd_rank_puzzles: door crossings from the main
// hall through the keys it needs and the item it takes): the nearest two
// give a grenade launcher or a flamethrower, the next two the Python, the
// farthest the rocket launcher. The piano never gives the rocket launcher -
// Richard brings its sheet music, which the ranking leaves out. Its reward
// slot is the puzzle's own (the alcove's gold emblem, the greenhouse's key,
// the tiger statue's red-gem magnum, the gallery's and armor room's crests).
// Tier 1 - the handgun and the shotgun - lies anywhere, and only the heavy
// weapons in the game have ammo lying about (magnum rounds with Barry, who
// brings an empty Python, too). Barry may join until every survivor is in,
// so the weapons are placed again from the same draws while his presence
// changes (rnd_weapons_refresh).
#define RND_PUZZLES 5
struct RndPuzzle {
    unsigned char stage, room, flag;   // the reward slot's record
    unsigned char need;                // the item it takes (0: solved in the room)
    bool          noRocket;
};
static const RndPuzzle kPuzzles[RND_PUZZLES] = {
    { RND_STAGE_1F, 0x0F, 0x15, ITEM_MUSIC_NOTES, true  },   // piano bar: the alcove
    { RND_STAGE_1F, 0x0C, 0x1B, ITEM_CHEMICAL,    false },   // greenhouse: past the plants
    { RND_STAGE_1F, 0x0D, 0xAF, ITEM_RED_JEWEL,   false },   // tiger statue: the red gem
    { RND_STAGE_1F, 0x17, 0x1F, 0,                false },   // large gallery: the portraits
    { RND_STAGE_2F, 0x05, 0x24, 0,                false },   // armor room: the statues
};
static int           s_puzzleCost[RND_PUZZLES];      // door crossings to solve it, -1 never
static unsigned char s_puzzleWeapon[RND_PUZZLES];
// Rooms that take the key items (keys, crests, the puzzles' items and tools)
// first: leaf rooms, one door's room away from the rest of the mansion.
static bool          s_leaf[RND_ROOMS * 2];
// The back area: the rooms only the elevator or the keypad lead to - reached
// holding the battery and the note besides every key and crest, but not with
// the keys and crests alone (rnd_find_back). One crest always lies there
// (rnd_generate_once).
static bool          s_back[RND_ROOMS * 2];
static bool          s_backAny = false;
static unsigned int  s_rngWeapons = 0;               // the generator after the keys and crests
static bool          s_t3Barry = false;              // the weapons as placed for Barry in the game
static bool          s_t3Placed = false;
static bool          s_t3Locked = false;             // every survivor in: the picks are final
// The tier 3 ammo in the game (rnd_t3_ammo): magnum rounds, grenade rounds
// (one of the three kinds each time), fuel.
static unsigned char s_t3Ammo[3];
static int           s_t3AmmoCount = 0;

static unsigned char s_rdt[700 * 1024];

// ---------------------------------------------------------------------------
// A small generator of our own: every copy must draw the same numbers.
// ---------------------------------------------------------------------------
static unsigned int s_rng = 1;
static unsigned int rnd_next(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}
static int rnd_below(int n)
{
    return n > 0 ? (int)(rnd_next() % (unsigned int)n) : 0;
}

static bool rnd_is_key(unsigned char id)
{
    return id >= ITEM_SWORD_KEY && id <= ITEM_HELMET_KEY;
}
static bool rnd_is_crest(unsigned char id)
{
    return id == ITEM_WIND_CREST || id == ITEM_MOON_CREST || id == ITEM_STAR_CREST || id == ITEM_SUN_CREST;
}
static bool rnd_is_weapon_or_ammo(unsigned char id)
{
    return (id >= ITEM_BERETTA && id <= ITEM_ROCKET_LAUNCHER) ||
           (id >= ITEM_CLIP && id <= ITEM_FLAME_ROUNDS);
}

// ---------------------------------------------------------------------------
// Reading the mansion
// ---------------------------------------------------------------------------
static void rnd_decode_dest(unsigned char dest, unsigned char fromStage, unsigned char* stage, unsigned char* room)
{
    *room = (unsigned char)(dest & 0x1F);
    if (dest < 0x20) { *stage = fromStage; return; }
    unsigned char s = (unsigned char)((dest >> 5) - 1);
    if (s < 2) s = (unsigned char)(s + 5);       // the return mansion (always, in this mode)
    *stage = s;
}

// An SCD condition opcode (the tests an `if` ANDs together).
static bool rnd_scd_condition(unsigned char op)
{
    return op == 0x04 || op == 0x06 || op == 0x07 || op == 0x10 || op == 0x11 || op == 0x1D ||
           op == 0x22 || op == 0x36 || op == 0x38 || op == 0x3C || op == 0x3F || op == 0x50;
}

// An `if` whose only condition is "Chris's room" (bit_test of
// MSF_CHAR_VARIANT, bank 5 bit 8, true when clear): the mode always plays
// Chris's rooms (ZombieLobby.cpp), so for the item spots its body is the top
// level and its else - Jill's - never runs. Doors keep the plain nesting,
// the route's gates as reviewed (kDoorGates).
static bool rnd_chris_gate(const unsigned char* rdt, unsigned int q, unsigned int end)
{
    return q + 7 <= end && rdt[q + 2] == 0x04 && rdt[q + 3] == 0x05 && rdt[q + 4] == 0x08 &&
           rdt[q + 5] == 0x01 && !rnd_scd_condition(rdt[q + 6]);
}

// One script (init +0x60 or per-frame +0x64): its doors and item spots, and
// which room action slots a 0x12 / 0x13 touches.
static void rnd_scan_script(const unsigned char* rdt, size_t size, unsigned int start, unsigned char stage,
                            unsigned char room, bool* touched, int firstSpot)
{
    unsigned int p = start;
    for (int blocks = 0; blocks < 256 && p + 2 <= size; blocks++) {
        unsigned short blockSize = *(const unsigned short*)(rdt + p);
        if (blockSize == 0) break;
        unsigned int q = p + 2;
        unsigned int end = p + blockSize;
        if (end > size) break;
        unsigned int ifEnd[32];
        bool chrisIf[32];
        int depth = 0;
        int chrisDepth = 0;                          // of those, Chris's-room ifs
        unsigned int jillFrom = 0, jillTo = 0;       // a Chris's-room if's else body
        while (q < end) {
            while (depth > 0 && q >= ifEnd[depth - 1]) {
                depth--;
                if (chrisIf[depth]) chrisDepth--;
            }
            unsigned char op = rdt[q];
            int w = zm_scd_width(op);
            if (w < 0) break;
            if (op == 0x01 && depth < 32) {
                ifEnd[depth] = q + 2 + rdt[q + 1];
                chrisIf[depth] = rnd_chris_gate(rdt, q, end);
                if (chrisIf[depth]) {
                    chrisDepth++;
                    // The else op closes the if's body (its last two bytes);
                    // its length runs from the op itself.
                    unsigned int e = ifEnd[depth] - 2;
                    if (e + 1 < end && rdt[e] == 0x02) { jillFrom = ifEnd[depth]; jillTo = e + rdt[e + 1]; }
                }
                depth++;
            }
            bool jill = q >= jillFrom && q < jillTo;
            if ((op == 0x12 || op == 0x13) && q + 1 < end) touched[rdt[q + 1] & 0x7F] = true;
            if ((op == 0x0C || op == 0x0D || op == 0x18) && q + 1 < end && s_curInfo != NULL) {
                unsigned char slot = (unsigned char)(rdt[q + 1] & 0x7F);
                if (slot > s_curInfo->highSlot) s_curInfo->highSlot = slot;
            }
            // cmd_room_action_set with handler 0x08 (open_itembox): an item
            // box, i.e. a safe room the director keeps out of.
            if (op == 0x0D && q + 11 <= end && rdt[q + 10] == 0x08 && s_curInfo != NULL) {
                s_curInfo->safe = true;
            }
            if (op == 0x18 && q + 0x17 <= end) s_usedFlags[rdt[q + 0x16] >> 3] |= (unsigned char)(1u << (rdt[q + 0x16] & 7));
            if ((op == 0x04 || op == 0x05) && q + 3 <= end && rdt[q + 1] == 7) {
                s_usedFlags[rdt[q + 2] >> 3] |= (unsigned char)(1u << (rdt[q + 2] & 7));
            }
            if (op == 0x0C && q + 26 <= end && s_doorCount < RND_MAX_DOORS) {
                const unsigned char* r = rdt + q + 2;
                if ((r[0x0B] & 0x80) == 0) {                 // not a camera-only record
                    RndDoor& d = s_doors[s_doorCount++];
                    d.fromStage = stage;
                    d.fromRoom = room;
                    rnd_decode_dest(r[0x0D], stage, &d.toStage, &d.toRoom);
                    d.lock = r[0x0C];
                    d.need = r[0x16];
                    d.slot = (unsigned char)(rdt[q + 1] & 0x7F);
                    rnd_added_lock(stage, room, d.slot, r[0x0D], &d.lock, &d.need);
                    // Built in an if block, left unarmed for an event, or a
                    // zone nobody can stand in (width or depth of one unit).
                    // rnd_read_room adds the slots a script disarms.
                    d.gated = depth > 0 || r[0x17] == 0 ||
                              *(const unsigned short*)(r + 4) <= 1 || *(const unsigned short*)(r + 6) <= 1;
                }
            }
            if (op == 0x18 && q + 0x17 <= end && !jill) {
                unsigned char flag = rdt[q + 0x16];
                // One spot per flag: a second record under the same flag
                // (a variant) leaves it out of the pool.
                RndSpot* s = NULL;
                for (int i = firstSpot; i < s_spotCount; i++) {
                    if (s_spots[i].flag == flag) { s = &s_spots[i]; break; }
                }
                if (s != NULL) {
                    s->pool = false;
                } else if (s_spotCount < RND_MAX_SPOTS) {
                    s = &s_spots[s_spotCount++];
                    memset(s, 0, sizeof(*s));
                    s->stage = stage;
                    s->room = room;
                    s->flag = flag;
                    s->origId = rdt[q + 10];
                    s->origQty = rdt[q + 11];
                    s->pool = depth - chrisDepth == 0;
                    // the slot, for the 0x12/0x13 test below
                    s->id = (unsigned char)(rdt[q + 1] & 0x7F);
                }
            }
            q += 1u + (unsigned int)w;
        }
        p = end;
    }
}

// Rooms that were too generous: their listed pickups (by roomItems flag) are
// emptied and the rooms take none of the randomizer's extra spots - reviewed
// by hand. The mansion storeroom (a safe room) keeps its first aid spray and
// one random pickup, the clip's spot; its shells' spot holds its blue herb
// (rnd_add_herb_spots).
static const unsigned char kTrimmedSpots[][3] = {
    { RND_STAGE_1F, 0x18, 218 },     // mansion storeroom: the shells
};
static const unsigned char kNoExtraSpotRooms[][2] = {
    { RND_STAGE_1F, 0x18 },          // mansion storeroom
};

static bool rnd_trimmed_spot(const RndSpot& s)
{
    for (unsigned int i = 0; i < sizeof(kTrimmedSpots) / sizeof(kTrimmedSpots[0]); i++)
        if (s.stage == kTrimmedSpots[i][0] && s.room == kTrimmedSpots[i][1] && s.flag == kTrimmedSpots[i][2]) return true;
    return false;
}

static bool rnd_no_extra_spots(unsigned char stage, unsigned char room)
{
    for (unsigned int i = 0; i < sizeof(kNoExtraSpotRooms) / sizeof(kNoExtraSpotRooms[0]); i++)
        if (stage == kNoExtraSpotRooms[i][0] && room == kNoExtraSpotRooms[i][1]) return true;
    return false;
}

static void rnd_read_room(unsigned char stage, unsigned char room)
{
    static const char hex[] = "0123456789abcdef";
    char path[96];
    // Match LoadRoomRdt's restored crest puzzle, including its model count
    // and action slots when allocating the randomizer's extra pickups.
    unsigned char fileStage = (stage == RND_STAGE_1F &&
        (room == ROOM_ROOFED_PASSAGE || room == ROOM_TRAP_ROOM || room == ROOM_LIVING_ROOM))
        ? STAGE_MANSION_1F : stage;
    snprintf(path, sizeof(path), GAME_DATA_ROOT "stage%c\\room%c%c%c0.rdt",
             hex[(fileStage + 1) & 0xF], hex[(fileStage + 1) & 0xF], hex[room >> 4], hex[room & 0xF]);
    size_t size = LoadFile(path, s_rdt, 1);
    if (size == (size_t)-1 || size < 0x100 || size > sizeof(s_rdt)) return;     // a stub
    s_curInfo = &s_roomInfo[stage - RND_STAGE_1F][room];
    s_curInfo->exists = true;
    s_curInfo->itemCount = s_rdt[3];
    s_curInfo->highSlot = 0;
    bool touched[128];
    memset(touched, 0, sizeof(touched));
    int firstSpot = s_spotCount;
    int firstDoor = s_doorCount;
    rnd_scan_script(s_rdt, size, *(const unsigned int*)(s_rdt + 0x60), stage, room, touched, firstSpot);
    rnd_scan_script(s_rdt, size, *(const unsigned int*)(s_rdt + 0x64), stage, room, touched, firstSpot);
    for (int i = firstDoor; i < s_doorCount; i++) {
        RndDoor& d = s_doors[i];
        if (touched[d.slot]) d.gated = true;
        d.usable = (!d.gated || rnd_gate_state(stage, room, d.slot) == RND_GATE_OPEN) &&
                   !rnd_sealed_door(stage, room, d.slot);
        d.access = rnd_access_bit(stage, room, d.slot);
    }
    // The rough passage's way back to the 2F left stairs, once the keypad
    // has opened (ZombieKeypad.cpp builds the door).
    if (stage == RND_STAGE_2F && room == 0x14 && s_doorCount < RND_MAX_DOORS) {
        RndDoor& d = s_doors[s_doorCount++];
        memset(&d, 0, sizeof(d));
        d.fromStage = d.toStage = stage;
        d.fromRoom = room;
        d.toRoom = 0x01;
        d.slot = RND_ROUGH_DOOR_SLOT;
        d.usable = true;
        d.access = RND_BIT_NOTE;
    }
    for (int i = firstSpot; i < s_spotCount; i++) {
        RndSpot& s = s_spots[i];
        // The small key opens nothing in the mode (check_desk lets every
        // survivor in), so its spots - the terrace passage's - take items too.
        bool wanted = rnd_is_key(s.origId) || rnd_is_crest(s.origId) ||
                      ITEM_IS_MAP(s.origId) || s.origId == ITEM_DESK_KEY ||
                      (rnd_is_weapon_or_ammo(s.origId) && s.origId != ITEM_BROKEN_SHOTGUN);
        if (touched[s.id] || !wanted) s.pool = false;
        // These otherwise-unused scenario items are optional reward spots.
        // Retain their native interactions, but never let an unmodeled climb
        // or furniture puzzle gate the escape items.
        s.rewardOnly = stage == RND_STAGE_2F &&
            ((room == 0x19 && s.flag == 0x39 && s.origId == ITEM_BATTERY) ||
             (room == 0x17 && s.flag == 0x4D && s.origId == ITEM_MO_DISK));
        if (s.rewardOnly) s.pool = true;
        // A puzzle's reward slot holds that puzzle's heavy weapon, never a
        // key or crest.
        for (int p = 0; p < RND_PUZZLES; p++) {
            if (kPuzzles[p].stage == stage && kPuzzles[p].room == room && kPuzzles[p].flag == s.flag) {
                s.puzzle = (unsigned char)(p + 1);
                s.pool = false;
            }
        }
        // ROOM6160's shotgun is the ceiling trap's bait.
        if (stage == RND_STAGE_1F && room == 0x16) s.pool = false;
        if (rnd_trimmed_spot(s)) s.pool = false;          // emptied (rnd_place_weapons)
        s.id = s.origId;
        s.qty = s.origQty;
    }
}

// ---------------------------------------------------------------------------
// The route
// ---------------------------------------------------------------------------
static int rnd_lock_index(unsigned char flag)
{
    for (int i = 0; i < s_lockCount; i++) if (s_lockFlag[i] == flag) return i;
    return -1;
}

static bool rnd_door_key_lock(const RndDoor& d)
{
    return (d.lock & 0x80) != 0 && rnd_is_key(d.need);
}

static int rnd_room_index(unsigned char stage, unsigned char room)
{
    if (room >= RND_ROOMS) return -1;
    if (stage == RND_STAGE_1F) return room;
    if (stage == RND_STAGE_2F) return RND_ROOMS + room;
    return -1;
}

// The route's bit for a progression item: bit per kKeys index, bit 4 + n per
// crest, and the back area's battery and note.
static unsigned int rnd_item_bit(unsigned char id)
{
    for (int k = 0; k < 4; k++) {
        if (id == kKeys[k]) return 1u << k;
        if (id == kCrests[k]) return 1u << (4 + k);
    }
    if (id == ITEM_BATTERY) return RND_BIT_BATTERY;
    if (id == ITEM_ZM_PASS_NOTE) return RND_BIT_NOTE;
    return 0;
}

// Can the route cross `d` holding `owned` (rnd_item_bit's bits)? Puzzle and
// one-way locks start unlocked (zm_random_new_game).
static bool rnd_door_open(const RndDoor& d, unsigned int owned)
{
    if (!d.usable) return false;
    if (d.access != 0 && (owned & d.access) == 0) return false;
    if ((d.lock & 0x80) == 0) return true;
    unsigned char flag = (unsigned char)(d.lock & 0x3F);
    if (flag == RND_CREST_LOCK) return (owned & 0xF0) == 0xF0;
    if (!rnd_door_key_lock(d)) return true;
    int li = rnd_lock_index(flag);
    unsigned char key = li >= 0 ? s_lockKey[li] : d.need;
    return (owned & (1u << (key - ITEM_SWORD_KEY))) != 0;
}

// The rooms reachable from the main hall holding `owned` and from which the
// main hall can be reached again - a one-way door must not strand the
// collector in a room it then counts as visited. Collects placed items in
// reach as it goes when `collect` (the full check); returns the final owned set.
static unsigned int rnd_reach(unsigned int owned, bool collect, bool* reach)
{
    const int start = rnd_room_index(RND_STAGE_1F, ROOM_MAIN_HALL);
    for (;;) {
        bool back[RND_ROOMS * 2];
        memset(reach, 0, sizeof(bool) * RND_ROOMS * 2);
        memset(back, 0, sizeof(back));
        reach[start] = back[start] = true;
        for (bool grew = true; grew; ) {
            grew = false;
            for (int i = 0; i < s_doorCount; i++) {
                const RndDoor& d = s_doors[i];
                int a = rnd_room_index(d.fromStage, d.fromRoom);
                int b = rnd_room_index(d.toStage, d.toRoom);
                if (a < 0 || b < 0 || !reach[a] || reach[b] || !rnd_door_open(d, owned)) continue;
                reach[b] = true;
                grew = true;
            }
        }
        for (bool grew = true; grew; ) {
            grew = false;
            for (int i = 0; i < s_doorCount; i++) {
                const RndDoor& d = s_doors[i];
                int a = rnd_room_index(d.fromStage, d.fromRoom);
                int b = rnd_room_index(d.toStage, d.toRoom);
                if (a < 0 || b < 0 || !reach[a] || back[a] || !back[b] || !rnd_door_open(d, owned)) continue;
                back[a] = true;
                grew = true;
            }
        }
        for (int r = 0; r < RND_ROOMS * 2; r++) reach[r] = reach[r] && back[r];
        if (!collect) return owned;
        unsigned int before = owned;
        for (int i = 0; i < s_spotCount; i++) {
            const RndSpot& s = s_spots[i];
            // Only pool progression survives rnd_place_weapons. Original
            // non-pool keys/crests become supplies, so counting them here would
            // let assumed fill and the final proof rely on removed items.
            if (!s.pool) continue;
            int r = rnd_room_index(s.stage, s.room);
            if (r < 0 || !reach[r]) continue;
            owned |= rnd_item_bit(s.id);
        }
        if (owned == before) return owned;
    }
}

static unsigned int rnd_route_state(unsigned int owned)
{
    return (owned & 0xFF) | ((owned & RND_BIT_BATTERY) ? 0x100u : 0u) | ((owned & RND_BIT_NOTE) ? 0x200u : 0u);
}
static unsigned int rnd_route_owned(unsigned int m)
{
    return (m & 0xFF) | ((m & 0x100u) ? RND_BIT_BATTERY : 0u) | ((m & 0x200u) ? RND_BIT_NOTE : 0u);
}

// Door crossings from room `from` to every room, any door that can be used,
// locked or not (a key does not move a room); -1 where no door leads.
static void rnd_rooms_from(int from, int* dist)
{
    for (int r = 0; r < RND_ROOMS * 2; r++) dist[r] = -1;
    if (from < 0) return;
    int queue[RND_ROOMS * 2], head = 0, tail = 0;
    dist[from] = 0;
    queue[tail++] = from;
    while (head < tail) {
        int r = queue[head++];
        for (int i = 0; i < s_doorCount; i++) {
            const RndDoor& d = s_doors[i];
            if (!d.usable || rnd_room_index(d.fromStage, d.fromRoom) != r) continue;
            int b = rnd_room_index(d.toStage, d.toRoom);
            if (b < 0 || dist[b] >= 0) continue;
            dist[b] = dist[r] + 1;
            queue[tail++] = b;
        }
    }
}

// The fewest door crossings from the main hall to stand in room `goal`
// having passed through room `need` (a room index, or -1) on the way, picking
// up the keys, crests, battery and note lying in the rooms crossed - an
// informed solo runner. -1: never.
static int rnd_route_cost(int goal, int need)
{
    static unsigned short dist[RND_ROOMS * 2][RND_ROUTE_STATES];
    static unsigned short queue[RND_ROOMS * 2 * RND_ROUTE_STATES][2];
    unsigned int roomItems[RND_ROOMS * 2] = {};
    for (int i = 0; i < s_spotCount; i++) {
        const RndSpot& s = s_spots[i];
        int r = rnd_room_index(s.stage, s.room);
        if (s.pool && r >= 0) roomItems[r] |= rnd_route_state(rnd_item_bit(s.id));
    }
    memset(dist, 0xFF, sizeof(dist));
    int start = rnd_room_index(RND_STAGE_1F, ROOM_MAIN_HALL);
    unsigned int m0 = roomItems[start] | (start == need ? RND_ROUTE_NEED : 0u);
    int head = 0, tail = 0;
    dist[start][m0] = 0;
    queue[tail][0] = (unsigned short)start; queue[tail++][1] = (unsigned short)m0;
    while (head < tail) {
        int r = queue[head][0];
        unsigned int m = queue[head++][1];
        if (r == goal && (need < 0 || (m & RND_ROUTE_NEED))) return dist[r][m];
        for (int i = 0; i < s_doorCount; i++) {
            const RndDoor& d = s_doors[i];
            if (rnd_room_index(d.fromStage, d.fromRoom) != r) continue;
            int b = rnd_room_index(d.toStage, d.toRoom);
            if (b < 0 || !rnd_door_open(d, rnd_route_owned(m))) continue;
            unsigned int n = m | roomItems[b] | (b == need ? RND_ROUTE_NEED : 0u);
            if (dist[b][n] != 0xFFFF) continue;
            dist[b][n] = (unsigned short)(dist[r][m] + 1);
            queue[tail][0] = (unsigned short)b; queue[tail++][1] = (unsigned short)n;
        }
    }
    return -1;
}

// Rooms that take key items like a leaf room though more than one door leads
// in - reviewed by hand.
static const unsigned char kExtraLeafRooms[][2] = {
    { RND_STAGE_1F, 0x19 },    // 1F study (a leaf already; this keeps it a key-item spot)
    { RND_STAGE_1F, 0x1A },    // roofed passage
    { RND_STAGE_2F, 0x06 },    // small library
    { RND_STAGE_2F, 0x0B },    // front lesson room
    { RND_STAGE_2F, 0x1A },    // B1 passage 1
};

// Leaf rooms: a single neighbouring room through usable doors (and the
// kExtraLeafRooms).
static void rnd_find_leaves(void)
{
    for (int r = 0; r < RND_ROOMS * 2; r++) {
        int neighbour = -1;
        bool many = false;
        for (int i = 0; i < s_doorCount && !many; i++) {
            const RndDoor& d = s_doors[i];
            if (!d.usable) continue;
            int a = rnd_room_index(d.fromStage, d.fromRoom), b = rnd_room_index(d.toStage, d.toRoom);
            if (a < 0 || b < 0 || a == b || (a != r && b != r)) continue;
            int other = a == r ? b : a;
            if (neighbour < 0) neighbour = other;
            else if (neighbour != other) many = true;
        }
        s_leaf[r] = s_roomInfo[r / RND_ROOMS][r % RND_ROOMS].exists && neighbour >= 0 && !many;
    }
    for (unsigned int i = 0; i < sizeof(kExtraLeafRooms) / sizeof(kExtraLeafRooms[0]); i++) {
        int r = rnd_room_index(kExtraLeafRooms[i][0], kExtraLeafRooms[i][1]);
        if (r >= 0 && s_roomInfo[r / RND_ROOMS][r % RND_ROOMS].exists) s_leaf[r] = true;
    }
}

static bool rnd_puzzle_room(unsigned char stage, unsigned char room)
{
    for (int p = 0; p < RND_PUZZLES; p++) {
        if (kPuzzles[p].stage == stage && kPuzzles[p].room == room) return true;
    }
    return false;
}

// The lesson room is Yawn 2's: no progression item lies there but the crest
// it guards (s_bossItem, rnd_generate_once), held back until it is beaten
// (ZombieYawn.cpp).
static bool rnd_boss_room(unsigned char stage, unsigned char room)
{
    return stage == RND_STAGE_2F && room == ROOM_LESSON_ROOM;
}

// A leaf room that takes key items: not a puzzle's room, the shotgun rooms,
// the storeroom past the crest door, Yawn 2's room or a room of unverified
// furniture.
static bool rnd_key_item_room(unsigned char stage, unsigned char room)
{
    int r = rnd_room_index(stage, room);
    return r >= 0 && s_leaf[r] && !rnd_puzzle_room(stage, room) && !rnd_room_unverified(stage, room) &&
           !rnd_boss_room(stage, room) &&
           !(stage == RND_STAGE_1F && (room == ROOM_TRAP_ROOM || room == ROOM_LIVING_ROOM ||
                                       room == ROOM_STOREROOM));
}

unsigned int zm_random_progression_bit(unsigned char id)
{
    if (id == ITEM_LOCK_PICK) return 0x100u;
    if (id == ITEM_BROKEN_SHOTGUN) return 0x200u;
    if (id == ITEM_SHOTGUN) return 0x400u;
    if (id == ITEM_PICK_AXE) return 0x800u;
    return rnd_item_bit(id);
}

// Runtime proof after irreversible inventory loss. Unlike assumed fill, this
// starts at the remaining survivors, uses actual lock/collection flags, and
// includes shared storage and drops. It never invents a key at a collected spot.
int zm_random_remaining_solvable(void)
{
    if (!s_active || !s_built) return -1;
    bool reach[RND_ROOMS * 2] = {};
    unsigned int carried[ZM_NET_MAX_PLAYERS] = {}, owned = 0;
    bool living = false, jill = false;
    for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
        const ZmNetPeerState* p = zm_seat_state(i);
        if (!p || !p->valid || zm_shotgun_crushed(i)) continue;
        unsigned char inventory[16];
        if (!zm_net_inventory(i, inventory)) return -1;
        for (int j = 0; j < 8; j++) if (inventory[j * 2 + 1] || inventory[j * 2] == ITEM_SHOTGUN) {
            carried[i] |= zm_random_progression_bit(inventory[j * 2]);
        }
        int r = rnd_room_index(p->stage, p->room);
        if (!p->dead && !p->spectating && r >= 0) {
            reach[r] = true; owned |= carried[i]; living = true;
            if (zm_net_char(i) == ZM_CHAR_JILL) jill = true;
        }
    }
    if (!living) return 0;
    if (jill && (owned & 0x100)) owned |= 1;
    // ROOM11A0's native use events: Sun=69, Star=6A, Moon=6B, Wind=6C.
    const unsigned char placedFlags[4] = { 0x6C, 0x6B, 0x6A, 0x69 };
    for (int k = 0; k < 4; k++)
        if (Flg_ck((int)g_ScenarioFlags, placedFlags[k])) owned |= 1u << (4 + k);
    bool grew;
    do {
        grew = false;
        for (int i = 0; i < s_doorCount; i++) {
            const RndDoor& d = s_doors[i];
            int a = rnd_room_index(d.fromStage, d.fromRoom), b = rnd_room_index(d.toStage, d.toRoom);
            if (a < 0 || b < 0 || !reach[a] || reach[b] || !d.usable) continue;
            bool open = true;
            unsigned char flag = d.lock & 0x3F;
            if ((d.lock & 0x80) && !Flg_ck((int)g_LocksFlags, flag)) {
                if (flag == RND_CREST_LOCK) open = (owned & 0xF0) == 0xF0;
                else if (rnd_door_key_lock(d)) {
                    int li = rnd_lock_index(flag);
                    unsigned char key = li >= 0 ? s_lockKey[li] : d.need;
                    open = (owned & rnd_item_bit(key)) != 0;
                }
            }
            // The back area's ways in: open for good once powered / keyed in,
            // else the battery or the note within reach.
            if (open && d.access == RND_BIT_BATTERY && !zm_access_powered()) open = (owned & RND_BIT_BATTERY) != 0;
            if (open && d.access == RND_BIT_NOTE && !zm_access_keypad_open()) open = (owned & RND_BIT_NOTE) != 0;
            if (open) open = zm_shotgun_route_open(d.fromStage, d.fromRoom, d.toStage, d.toRoom,
                owned, reach[rnd_room_index(RND_STAGE_1F, ROOM_LIVING_ROOM)]);
            if (open) { reach[b] = true; grew = true; }
        }
        unsigned int before = owned;
        bool box = false;
        for (int r = 0; r < RND_ROOMS * 2; r++)
            if (reach[r] && s_roomInfo[r / RND_ROOMS][r % RND_ROOMS].safe) box = true;
        if (box) for (int i = 0; i < 48; i++)
            if (g_itemboxSlots[i].qty) owned |= zm_random_progression_bit(g_itemboxSlots[i].Id);
        for (int i = 0; i < s_spotCount; i++) {
            const RndSpot& s = s_spots[i];
            int r = rnd_room_index(s.stage, s.room);
            if (r >= 0 && reach[r] && s.qty && Flg_ck((int)g_roomItemsFlags, s.flag))
                owned |= zm_random_progression_bit(s.id);
        }
        owned |= zm_drops_progression(reach);
        // Ordinary corpses can retain items if the drop list was full; their
        // reachable inventory remains recoverable through revival. Crushes cannot.
        for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
            const ZmNetPeerState* p = zm_seat_state(i);
            if (!p || !p->valid || zm_shotgun_crushed(i)) continue;
            int r = rnd_room_index(p->stage, p->room);
            if (r >= 0 && reach[r]) owned |= carried[i];
        }
        if (jill && (owned & 0x100)) owned |= 1;
        if (owned != before) grew = true;
    } while (grew);
    return reach[rnd_room_index(RND_STAGE_1F, 0x1B)] ? 1 : 0;
}

// Pickups of our own, on the rooms' generated floor spots (ZombieSpawnSpots.cpp),
// each under a roomItems flag no mansion script uses. A room takes up to
// RND_NEW_PER_ROOM - one per spawn spot - while it has a spare item model
// (8 at most) and room action slot (24) for each. Two batches, both at random:
//   - RND_NEW_SPOTS join the pool: keys and crests can land on them, the rest
//     get weapons and supplies;
//   - RND_AMMO_SPOTS are held back for ammo only, as many as the scenario's
//     own ammo pickups (rnd_fill_ammo) - the rest stay empty.
static unsigned char s_newInRoom[2][RND_ROOMS];
static unsigned char s_newFirstSpot[2][RND_ROOMS];


static void rnd_find_back(void)
{
    // Any key opens every lock while all four are in hand.
    for (int i = 0; i < s_lockCount; i++) s_lockKey[i] = ITEM_SWORD_KEY;
    bool open[RND_ROOMS * 2], all[RND_ROOMS * 2];
    rnd_reach(0xFF, false, open);
    rnd_reach(0xFF | RND_BIT_BATTERY | RND_BIT_NOTE, false, all);
    s_backAny = false;
    for (int r = 0; r < RND_ROOMS * 2; r++) {
        s_back[r] = all[r] && !open[r];
        if (s_back[r]) s_backAny = true;
    }
}

// A back-area room that can take its crest: as rnd_key_spot_ok allows.
static bool rnd_back_room_ok(unsigned char stage, unsigned char room)
{
    int r = rnd_room_index(stage, room);
    return r >= 0 && s_back[r] && !rnd_room_unverified(stage, room) && !rnd_puzzle_room(stage, room);
}

// ROOM70C0's camera zones also cover the passage beyond its solid east
// wall. A generated monster spot at (23500,2300) is visible to that camera
// union but unreachable from the fighting floor. Pickups need their own
// reviewed positions here; keep all three on the main floor, off the hole.
static const short kLessonPickupSpots[3][3] = {
    { 2500, 0, 6500 }, { 8500, 0, 15100 }, { 10500, 0, 15100 }
};

static const short* rnd_new_pickup_position(const ZmSpawnSpots& t, int index)
{
    if (t.stage == RND_STAGE_2F && t.room == ROOM_LESSON_ROOM)
        return kLessonPickupSpots[index];
    return t.spot[index];
}

static bool rnd_add_new_spot(const ZmSpawnSpots& t, bool ammoOnly, int* flag)
{
    if (rnd_no_extra_spots(t.stage, t.room)) return false;
    int st = t.stage - RND_STAGE_1F;
    const RndRoomInfo& info = s_roomInfo[st][t.room];
    int k = s_newInRoom[st][t.room];
    if (k >= RND_NEW_PER_ROOM || info.itemCount + k >= ZM_RANDOM_MODELS || info.highSlot + 1 + k >= ZM_DROP_SLOT_FIRST) return false;
    while (*flag > 0 && (s_usedFlags[*flag >> 3] & (1u << (*flag & 7))) != 0) (*flag)--;
    if (*flag <= 0 || s_spotCount >= RND_MAX_SPOTS) return false;
    if (k == 0) s_newFirstSpot[st][t.room] = (unsigned char)rnd_below(3);
    RndSpot& sp = s_spots[s_spotCount++];
    memset(&sp, 0, sizeof(sp));
    sp.stage = t.stage;
    sp.room = t.room;
    sp.flag = (unsigned char)*flag;
    s_usedFlags[*flag >> 3] |= (unsigned char)(1u << (*flag & 7));
    sp.pool = !ammoOnly;
    sp.ammoOnly = ammoOnly;
    sp.isNew = true;
    sp.slot = (unsigned char)(info.highSlot + 1 + k);
    sp.model = (unsigned char)(info.itemCount + k);
    const short* at = rnd_new_pickup_position(t, (s_newFirstSpot[st][t.room] + k) % 3);
    sp.x = at[0];
    sp.y = at[1];
    sp.z = at[2];
    sp.angle = (short)rnd_below(0x1000);
    s_newInRoom[st][t.room] = (unsigned char)(k + 1);
    return true;
}

// The rooms that can take one more, shuffled.
static int rnd_new_candidates(int* candidates)
{
    int count = 0;
    for (int i = 0; i < g_zmSpawnSpotCount; i++) {
        const ZmSpawnSpots& t = g_zmSpawnSpots[i];
        if (t.stage < RND_STAGE_1F || t.stage > RND_STAGE_2F || t.room >= RND_ROOMS) continue;
        const RndRoomInfo& info = s_roomInfo[t.stage - RND_STAGE_1F][t.room];
        int k = s_newInRoom[t.stage - RND_STAGE_1F][t.room];
        if (!info.exists || k >= RND_NEW_PER_ROOM || info.itemCount + k >= ZM_RANDOM_MODELS || info.highSlot + 1 + k >= ZM_DROP_SLOT_FIRST) continue;
        candidates[count++] = i;
    }
    for (int i = count - 1; i > 0; i--) { int j = rnd_below(i + 1); int t = candidates[i]; candidates[i] = candidates[j]; candidates[j] = t; }
    return count;
}

// Yawn's bite poisons, so every safe room holds one blue herb, out of the
// pool and the same every game: on the room's first spawn spot as a spot of
// our own, or - the mansion storeroom, which takes none - on its emptied
// shells' spot (rnd_place_weapons).
static void rnd_add_herb_spots(int* flag)
{
    for (int i = 0; i < g_zmSpawnSpotCount; i++) {
        const ZmSpawnSpots& t = g_zmSpawnSpots[i];
        if (t.stage < RND_STAGE_1F || t.stage > RND_STAGE_2F || t.room >= RND_ROOMS) continue;
        int st = t.stage - RND_STAGE_1F;
        if (!s_roomInfo[st][t.room].exists || !s_roomInfo[st][t.room].safe || rnd_no_extra_spots(t.stage, t.room)) continue;
        int before = s_spotCount;
        if (!rnd_add_new_spot(t, false, flag)) continue;
        s_newFirstSpot[st][t.room] = 0;         // the herb on spot 0; the room's random spots after it
        RndSpot& sp = s_spots[before];
        sp.pool = false;
        sp.id = sp.origId = ITEM_BLUE_HERB;
        sp.qty = sp.origQty = 1;
        sp.x = t.spot[0][0];
        sp.y = t.spot[0][1];
        sp.z = t.spot[0][2];
    }
}

static void rnd_add_new_spots(void)
{
    memset(s_newInRoom, 0, sizeof(s_newInRoom));
    rnd_find_leaves();
    rnd_find_back();
    int flag = 255;
    rnd_add_herb_spots(&flag);
    int candidates[RND_ROOMS * 2];
    // The pool's: one in each back-area room that has no pool spot of its own,
    // for its crest; then one a room, every leaf room that takes key items
    // first.
    int count = rnd_new_candidates(candidates);
    int added = 0;
    for (int n = 0; n < count; n++) {
        const ZmSpawnSpots& t = g_zmSpawnSpots[candidates[n]];
        if (!rnd_back_room_ok(t.stage, t.room)) continue;
        bool has = false;
        for (int i = 0; i < s_spotCount && !has; i++) {
            const RndSpot& s = s_spots[i];
            has = s.stage == t.stage && s.room == t.room && s.pool && !s.rewardOnly;
        }
        if (!has && rnd_add_new_spot(t, false, &flag)) added++;
    }
    // Every key-item room without a pool spot of its own gets one, however
    // many that takes (the boiler had none in about a fifth of the games).
    // Yawn 2's room too, for its crest.
    for (int n = 0; n < count; n++) {
        const ZmSpawnSpots& t = g_zmSpawnSpots[candidates[n]];
        if (!rnd_key_item_room(t.stage, t.room) && !rnd_boss_room(t.stage, t.room)) continue;
        bool has = false;
        for (int i = 0; i < s_spotCount && !has; i++) {
            const RndSpot& sp = s_spots[i];
            has = sp.stage == t.stage && sp.room == t.room && sp.pool && !sp.rewardOnly;
        }
        if (!has && rnd_add_new_spot(t, false, &flag)) added++;
    }
    for (int n = 0; n < count && added < RND_NEW_SPOTS; n++) {
        const ZmSpawnSpots& t = g_zmSpawnSpots[candidates[n]];
        if (s_newInRoom[t.stage - RND_STAGE_1F][t.room] == 0 && rnd_add_new_spot(t, false, &flag)) added++;
    }
    // The ammo's: rounds over the rooms, one a room a round.
    added = 0;
    for (int round = 0; round < RND_NEW_PER_ROOM && added < RND_AMMO_SPOTS; round++) {
        count = rnd_new_candidates(candidates);
        for (int n = 0; n < count && added < RND_AMMO_SPOTS; n++) {
            if (rnd_add_new_spot(g_zmSpawnSpots[candidates[n]], true, &flag)) added++;
        }
    }
}

static bool rnd_is_tier3(unsigned char id)
{
    return id == ITEM_COLT_PYTHON_MAG || (id >= ITEM_FLAMETHROWER && id <= ITEM_ROCKET_LAUNCHER);
}
static bool rnd_is_tier3_ammo(unsigned char id)
{
    return id >= ITEM_DUM_DUM_ROUNDS && id <= ITEM_FLAME_ROUNDS;
}

// One pickup of the game's tier 3 ammo. False: none is in the game.
static bool rnd_t3_ammo(unsigned char* id, unsigned char* qty)
{
    if (s_t3AmmoCount == 0) return false;
    unsigned char a = s_t3Ammo[rnd_below(s_t3AmmoCount)];
    if (a == ITEM_EXPLOSIVE_ROUNDS) a = (unsigned char)(ITEM_EXPLOSIVE_ROUNDS + rnd_below(3));
    *id = a;
    *qty = (a == ITEM_FUEL) ? 120 : 6;
    return true;
}

// One ammo pickup: two clips to one shells, then each tier 3 ammo once.
// The shotgun only comes from the trap room (ZombieShotgun.cpp).
static void rnd_ammo(unsigned char* id, unsigned char* qty)
{
    int k = rnd_below(3 + s_t3AmmoCount);
    if (k >= 3 && rnd_t3_ammo(id, qty)) return;
    if (k == 2) { *id = ITEM_SHELLS; *qty = 7; }
    else        { *id = ITEM_CLIP; *qty = 15; }
}

// The ammo doubling: as many more ammo pickups as the scenario put out
// (every spot holding a clip, shells, magnum or grenade rounds, fuel), on
// the held-back spots, in rnd_ammo's proportions. Spots left over stay empty.
static void rnd_fill_ammo(void)
{
    int ammo = 0;
    for (int i = 0; i < s_spotCount; i++) {
        const RndSpot& s = s_spots[i];
        if (!s.ammoOnly && s.id >= ITEM_CLIP && s.id <= ITEM_FLAME_ROUNDS) ammo++;
    }
    int placed = 0;
    for (int i = 0; i < s_spotCount; i++) {
        RndSpot& s = s_spots[i];
        if (!s.ammoOnly) continue;
        if (placed < ammo) {
            rnd_ammo(&s.id, &s.qty);
            placed++;
        } else {
            s.id = 0;
            s.qty = 0;
        }
        s.overridden = s.id != 0;
    }
    dbg_printf("[random] ammo: %d pickups in the scenario, %d more placed\n", ammo, placed);
}

// What a spot no key, crest or weapon went to gets.
static void rnd_supply(unsigned char* id, unsigned char* qty)
{
    static const unsigned char kSupplies[][2] = {
        { ITEM_GREEN_HERB, 1 }, { ITEM_GREEN_HERB, 1 }, { ITEM_RED_HERB, 1 },
        { ITEM_FIRST_AID_SPRAY, 1 }, { ITEM_CLIP, 15 }, { ITEM_CLIP, 15 },
        { ITEM_SHELLS, 7 },     // no ink ribbons: saving is no use in the mode
    };
    const int n = (int)(sizeof(kSupplies) / sizeof(kSupplies[0]));
    int k = rnd_below(n + s_t3AmmoCount);
    if (k >= n && rnd_t3_ammo(id, qty)) return;
    *id = kSupplies[k][0];
    *qty = kSupplies[k][1];
}

// Where a key item may lie: a free pool spot in a room `reach` marks that
// holds no other key item yet (`taken`), outside the puzzles' rooms, the
// shotgun rooms, the storeroom past the crest door and the rooms of
// unverified furniture.
static bool rnd_key_spot_ok(const RndSpot& s, const bool* reach, const bool* taken)
{
    int r = rnd_room_index(s.stage, s.room);
    return s.pool && !s.rewardOnly && s.id == 0 && r >= 0 && reach[r] && !taken[r] &&
           !rnd_room_unverified(s.stage, s.room) && !rnd_puzzle_room(s.stage, s.room) &&
           (!rnd_boss_room(s.stage, s.room) || s_bossItem) &&
           !(s.stage == RND_STAGE_1F &&
             (s.room == ROOM_TRAP_ROOM || s.room == ROOM_LIVING_ROOM || s.room == ROOM_STOREROOM));
}

// A spot for a key item, at random: in a leaf room whose route cost from the
// main hall lies within [lo, hi] (lo < 0: any), then in any leaf room, then
// anywhere rnd_key_spot_ok allows. Only in the rooms `only` marks, when
// given; only on a new spot (one of our own records) when `newOnly`. -1: none.
static int rnd_pick_key_spot(const bool* reach, const bool* taken, int lo, int hi, bool leafOnly = false,
                             const bool* only = NULL, bool newOnly = false)
{
    int cost[RND_ROOMS * 2];
    for (int r = 0; r < RND_ROOMS * 2; r++) cost[r] = -2;
    int cand[RND_MAX_SPOTS];
    for (int pass = lo < 0 ? 1 : 0; pass < (leafOnly ? 2 : 3); pass++) {
        int n = 0;
        for (int i = 0; i < s_spotCount; i++) {
            const RndSpot& s = s_spots[i];
            if (!rnd_key_spot_ok(s, reach, taken)) continue;
            if (newOnly && !s.isNew) continue;
            if (only != NULL && !only[rnd_room_index(s.stage, s.room)]) continue;
            if (pass < 2 && !rnd_key_item_room(s.stage, s.room)) continue;
            if (pass == 0) {
                int r = rnd_room_index(s.stage, s.room);
                if (cost[r] == -2) cost[r] = rnd_route_cost(r, -1);
                if (cost[r] < lo || cost[r] > hi) continue;
            }
            cand[n++] = i;
        }
        if (n > 0) return cand[rnd_below(n)];
    }
    return -1;
}

// The back area's crest: a back-area room at random (each alike, however many
// spots it has, never a safe room), then a spot in it. -1: none.
static int rnd_pick_back_spot(const bool* reach, const bool* taken)
{
    int rooms[RND_ROOMS * 2], roomCount = 0;
    for (int r = 0; r < RND_ROOMS * 2; r++) {
        if (!s_back[r] || s_roomInfo[r / RND_ROOMS][r % RND_ROOMS].safe) continue;
        for (int i = 0; i < s_spotCount; i++) {
            const RndSpot& s = s_spots[i];
            if (rnd_room_index(s.stage, s.room) == r && rnd_key_spot_ok(s, reach, taken)) { rooms[roomCount++] = r; break; }
        }
    }
    if (roomCount == 0) return -1;
    int room = rooms[rnd_below(roomCount)];
    int cand[RND_MAX_SPOTS], n = 0;
    for (int i = 0; i < s_spotCount; i++) {
        const RndSpot& s = s_spots[i];
        if (rnd_room_index(s.stage, s.room) == room && rnd_key_spot_ok(s, reach, taken)) cand[n++] = i;
    }
    return cand[rnd_below(n)];
}

// The puzzles' and the shotgun trap's items, after the keys and crests: the
// broken shotgun and the pick axe at a medium distance (RND_TOOL_NEAR..FAR
// crossings), then the sheet music and the chemical, each in a key-item room
// of its own. None of them is needed to escape.
#define RND_TOOL_NEAR 5
#define RND_TOOL_FAR  9
static bool rnd_place_key_items(bool* taken)
{
    bool reach[RND_ROOMS * 2];
    rnd_reach(0, true, reach);
    const unsigned char items[4] = { ITEM_BROKEN_SHOTGUN, ITEM_PICK_AXE, ITEM_MUSIC_NOTES, ITEM_CHEMICAL };
    // Exactly one of them in exactly one safe room (none needed to escape, so
    // the director's keeping out of it costs nothing): which at random, in a
    // safe room reached with everything; the others keep out of the safe rooms.
    bool safeOnly[RND_ROOMS * 2], notSafe[RND_ROOMS * 2], all[RND_ROOMS * 2];
    for (int r = 0; r < RND_ROOMS * 2; r++) {
        safeOnly[r] = s_roomInfo[r / RND_ROOMS][r % RND_ROOMS].safe;
        notSafe[r] = !safeOnly[r];
    }
    rnd_reach(0xFF, true, all);
    int safeTool = rnd_below(4);
    {
        // The safe room first, each alike however many spots it has.
        int rooms[RND_ROOMS * 2], nr = 0;
        for (int r = 0; r < RND_ROOMS * 2; r++) {
            if (!safeOnly[r]) continue;
            for (int i = 0; i < s_spotCount; i++) {
                const RndSpot& sp = s_spots[i];
                if (rnd_room_index(sp.stage, sp.room) == r && rnd_key_spot_ok(sp, all, taken)) { rooms[nr++] = r; break; }
            }
        }
        if (nr == 0) return false;
        int room = rooms[rnd_below(nr)];
        for (int r = 0; r < RND_ROOMS * 2; r++) safeOnly[r] = r == room;
        int i = rnd_pick_key_spot(all, taken, -1, -1, false, safeOnly);
        if (i < 0) return false;
        RndSpot& s = s_spots[i];
        s.id = items[safeTool];
        s.qty = 1;
        taken[rnd_room_index(s.stage, s.room)] = true;
    }
    for (int t = 0; t < 4; t++) {
        if (t == safeTool) continue;
        int i = t < 2 ? rnd_pick_key_spot(reach, taken, RND_TOOL_NEAR, RND_TOOL_FAR, false, notSafe)
                      : rnd_pick_key_spot(reach, taken, -1, -1, items[t] == ITEM_CHEMICAL, notSafe);
        if (i < 0) return false;
        RndSpot& s = s_spots[i];
        s.id = items[t];
        s.qty = 1;
        taken[rnd_room_index(s.stage, s.room)] = true;
    }
    return true;
}

// Each puzzle's cost (rnd_route_cost to its room through the room of the
// item it takes) and its heavy weapon by rank.
static void rnd_rank_puzzles(void)
{
    int order[RND_PUZZLES];
    for (int p = 0; p < RND_PUZZLES; p++) {
        int need = -1;
        for (int i = 0; i < s_spotCount && kPuzzles[p].need != 0 && need < 0; i++) {
            if (s_spots[i].id == kPuzzles[p].need) need = rnd_room_index(s_spots[i].stage, s_spots[i].room);
        }
        int goal = rnd_room_index(kPuzzles[p].stage, kPuzzles[p].room);
        s_puzzleCost[p] = (kPuzzles[p].need != 0 && need < 0) ? -1 : rnd_route_cost(goal, need);
        order[p] = p;
    }
    // Ties at random, then the nearest first; never solvable is the farthest.
    for (int i = RND_PUZZLES - 1; i > 0; i--) { int j = rnd_below(i + 1); int t = order[i]; order[i] = order[j]; order[j] = t; }
    for (int i = 1; i < RND_PUZZLES; i++) {
        int p = order[i], j = i;
        unsigned int c = (unsigned int)s_puzzleCost[p];
        while (j > 0 && (unsigned int)s_puzzleCost[order[j - 1]] > c) { order[j] = order[j - 1]; j--; }
        order[j] = p;
    }
    if (kPuzzles[order[RND_PUZZLES - 1]].noRocket) {
        int t = order[RND_PUZZLES - 1]; order[RND_PUZZLES - 1] = order[RND_PUZZLES - 2]; order[RND_PUZZLES - 2] = t;
    }
    for (int rank = 0; rank < RND_PUZZLES; rank++) {
        unsigned char w = rank < 2 ? (rnd_below(2) ? ITEM_FLAMETHROWER : ITEM_BAZOOKA_EXPLOSIVE)
                        : rank < 4 ? ITEM_COLT_PYTHON_MAG : ITEM_ROCKET_LAUNCHER;
        s_puzzleWeapon[order[rank]] = w;
    }
}

// One attempt with the generator's current state. False: no route (try again).
static bool rnd_generate_once(void)
{
    // Reset the spots to their originals.
    for (int i = 0; i < s_spotCount; i++) {
        s_spots[i].id = s_spots[i].origId;
        s_spots[i].qty = s_spots[i].origQty;
        s_spots[i].overridden = false;
    }

    // The keys' roles on the route (which key type plays which is random):
    //   key 1 opens the pillar passage's door and lies in reach of no key;
    //   key 2 opens the attic's door and lies behind a key-1 door;
    //   key 3 is Yawn's: it lies in the attic, held back until Yawn flees
    //     (ZombieYawn.cpp), and opens at least one door;
    //   key 4 opens the lesson room's door and lies behind a key-3 door.
    // So exactly two keys are in reach before the attic fight. The other
    // locks take any key.
    unsigned char role[4];
    for (int k = 0; k < 4; k++) role[k] = kKeys[k];
    for (int k = 3; k > 0; k--) { int j = rnd_below(k + 1); unsigned char t = role[k]; role[k] = role[j]; role[j] = t; }
    int atticLock = rnd_lock_index(RND_ATTIC_LOCK), pillarLock = rnd_lock_index(RND_PILLAR_LOCK);
    int lessonLock = rnd_lock_index(RND_LESSON_LOCK);
    int order[RND_MAX_LOCKS], others = 0;
    for (int i = 0; i < s_lockCount; i++) {
        if (i == pillarLock) s_lockKey[i] = role[0];
        else if (i == atticLock) s_lockKey[i] = role[1];
        else if (i == lessonLock) s_lockKey[i] = role[3];
        else order[others++] = i;
    }
    for (int i = others - 1; i > 0; i--) { int j = rnd_below(i + 1); int t = order[i]; order[i] = order[j]; order[j] = t; }
    for (int i = 0; i < others; i++) s_lockKey[order[i]] = (i == 0) ? role[2] : kKeys[rnd_below(4)];

    // The items to place, in assumed-fill order (each placed in reach of the
    // ones after it): the crests, then keys 4, 3, 2, 1 - as many uses as
    // doors.
    unsigned char items[8], qtys[8];
    int itemCount = 0;
    for (int k = 0; k < 4; k++) { items[itemCount] = kCrests[k]; qtys[itemCount++] = 1; }
    for (int i = itemCount - 1; i > 0; i--) {
        int j = rnd_below(i + 1);
        unsigned char t = items[i]; items[i] = items[j]; items[j] = t;
    }
    for (int k = 3; k >= 0; k--) {
        int uses = 0;
        for (int i = 0; i < s_lockCount; i++) if (s_lockKey[i] == role[k]) uses++;
        if (uses > 0) { items[itemCount] = role[k]; qtys[itemCount++] = (unsigned char)uses; }
    }

    // Clear the pool: every pool spot starts empty (0).
    for (int i = 0; i < s_spotCount; i++) {
        if (s_spots[i].pool) { s_spots[i].id = 0; s_spots[i].qty = 0; }
    }

    // The back area's two ways in first: the battery and the pass number's
    // note (one of our own spots - it is no item), each in a room of its own
    // outside the back area, in reach holding every key and crest. Either
    // opens it. The note lies in a leaf room when one is free; the battery
    // keeps out of them while it can, leaving them to the other key items.
    bool reach[RND_ROOMS * 2];
    bool progressionRoom[RND_ROOMS * 2] = {};
    if (s_backAny) {
        rnd_reach(0xFF, true, reach);
        // The battery lies RND_BATTERY_AWAY door crossings or more from the
        // kitchen, where the small elevator it powers stands - not a walk
        // next door to the elevator it opens.
        int fromKitchen[RND_ROOMS * 2];
        rnd_rooms_from(rnd_room_index(RND_STAGE_2F, ROOM_MANSION_KITCHEN), fromKitchen);
        bool notLeaf[RND_ROOMS * 2], notSafeRoom[RND_ROOMS * 2], farLeaf[RND_ROOMS * 2], farRoom[RND_ROOMS * 2];
        for (int r = 0; r < RND_ROOMS * 2; r++) {
            notSafeRoom[r] = !s_roomInfo[r / RND_ROOMS][r % RND_ROOMS].safe;
            notLeaf[r] = !s_leaf[r] && notSafeRoom[r];
            bool distant = fromKitchen[r] < 0 || fromKitchen[r] >= RND_BATTERY_AWAY;
            farLeaf[r] = notLeaf[r] && distant;
            farRoom[r] = notSafeRoom[r] && distant;
        }
        const unsigned char access[2] = { ITEM_BATTERY, ITEM_ZM_PASS_NOTE };
        for (int a = 0; a < 2; a++) {
            bool note = access[a] == ITEM_ZM_PASS_NOTE;
            int pick = rnd_pick_key_spot(reach, progressionRoom, -1, -1, false, note ? notSafeRoom : farLeaf, note);
            if (pick < 0 && !note) pick = rnd_pick_key_spot(reach, progressionRoom, -1, -1, false, farRoom);
            if (pick < 0) return false;
            RndSpot& s = s_spots[pick];
            s.id = access[a];
            s.qty = 1;
            progressionRoom[rnd_room_index(s.stage, s.room)] = true;
        }
        // One crest behind them, so the survivors must get through one. It
        // goes first: every other item in hand, the battery and the note are
        // in reach, and the back area's room is still free.
        int k = 0;
        unsigned char backCrest = kCrests[rnd_below(4)];
        while (items[k] != backCrest) k++;
        unsigned char t = items[0]; items[0] = items[k]; items[k] = t;
        t = qtys[0]; qtys[0] = qtys[k]; qtys[k] = t;
    }

    // Assumed fill. No key in the back area: everything outside it is then
    // reached without it, so the battery alone or the note alone opens it.
    // No key or crest in a safe room: the director can never guard one there
    // (one of the puzzle tools goes there instead, rnd_place_key_items).
    // The attic holds key 3 and no other key or crest; key 2 lies where no
    // key reaches, key 4 where only key 3 does (see the roles above). The
    // lesson room holds one crest, Yawn 2's, and nothing else of the route's.
    bool notBack[RND_ROOMS * 2], notSafe[RND_ROOMS * 2], keyRooms[RND_ROOMS * 2], atticOnly[RND_ROOMS * 2];
    bool lessonOnly[RND_ROOMS * 2];
    int lesson = rnd_room_index(RND_STAGE_2F, ROOM_LESSON_ROOM);
    int bossCrestAt = s_backAny ? 1 : 0;        // a crest (they come first), not the back area's
    bool key2Rooms[RND_ROOMS * 2], key4Rooms[RND_ROOMS * 2], noKey[RND_ROOMS * 2], noKey3[RND_ROOMS * 2];
    int attic = rnd_room_index(RND_STAGE_2F, RND_ATTIC_ROOM);
    rnd_reach(RND_BIT_BATTERY | RND_BIT_NOTE, false, noKey);
    unsigned int allButKey3 = 0xFFu | RND_BIT_BATTERY | RND_BIT_NOTE;
    allButKey3 &= ~(rnd_item_bit(role[2]) | rnd_item_bit(role[3]));
    rnd_reach(allButKey3, false, noKey3);
    for (int r = 0; r < RND_ROOMS * 2; r++) {
        notBack[r] = !s_back[r];
        notSafe[r] = !s_roomInfo[r / RND_ROOMS][r % RND_ROOMS].safe && r != attic;
        keyRooms[r] = notBack[r] && notSafe[r];
        atticOnly[r] = r == attic;
        lessonOnly[r] = r == lesson;
        key2Rooms[r] = keyRooms[r] && !noKey[r];
        key4Rooms[r] = keyRooms[r] && !noKey3[r];
    }
    for (int n = 0; n < itemCount; n++) {
        unsigned int owned = 0;
        for (int m = n + 1; m < itemCount; m++) owned |= rnd_item_bit(items[m]);
        rnd_reach(owned, true, reach);
        const bool* only = n == bossCrestAt && lesson >= 0 ? lessonOnly
                         : items[n] == role[2] ? atticOnly
                         : items[n] == role[1] ? key2Rooms
                         : items[n] == role[3] ? key4Rooms
                         : rnd_is_key(items[n]) ? keyRooms : notSafe;
        s_bossItem = only == lessonOnly;
        int pick = (n == 0 && s_backAny) ? rnd_pick_back_spot(reach, progressionRoom)
                                         : rnd_pick_key_spot(reach, progressionRoom, -1, -1, false, only);
        s_bossItem = false;
        if (pick < 0) return false;
        RndSpot& s = s_spots[pick];
        s.id = items[n];
        s.qty = qtys[n];
        progressionRoom[rnd_room_index(s.stage, s.room)] = true;
    }

    // The proof: from nothing, everything is collected and the storeroom is reached.
    unsigned int owned = rnd_reach(0, true, reach);
    unsigned int required = 0;
    for (int n = 0; n < itemCount; n++) required |= rnd_item_bit(items[n]);
    if ((owned & required) != required) return false;
    if (!reach[rnd_room_index(RND_STAGE_1F, ROOM_STOREROOM)]) return false;

    if (!rnd_place_key_items(progressionRoom)) return false;
    rnd_rank_puzzles();
    return true;
}

// Who the weapons are for: the survivors in the game (1 in single player)
// and whether Barry is one of them. True once the picks are final - every
// seated survivor is in the game (its STATE has come), which is every copy's
// own reading: a survivor sends STATE only from the game, after its pick.
static bool rnd_party(int* survivors, bool* barry)
{
    *survivors = 1;
    *barry = false;
    if (zm_net_role() == ZM_NET_OFF) return true;
    int n = 0;
    bool allIn = true;
    for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
        int ch = zm_net_char(i);
        if (ch < 0) continue;
        n++;
        if (ch == ZM_CHAR_BARRY) *barry = true;
        if (i != zm_net_self()) {
            const ZmNetPeerState* p = zm_net_player(i);
            if (p == NULL || !p->valid) allIn = false;
        }
    }
    if (n > 0) *survivors = n;
    return allIn && n > 0;
}

// The weapons, ammo and supplies over the spots the key items left, from
// the generator as it stood after them (s_rngWeapons) - so every copy places
// the same, and Barry joining or leaving only adds or takes away the magnum
// rounds he needs.
static bool rnd_is_key_item(unsigned char id)
{
    return rnd_is_key(id) || rnd_is_crest(id) || id == ITEM_BROKEN_SHOTGUN || id == ITEM_PICK_AXE ||
           id == ITEM_MUSIC_NOTES || id == ITEM_CHEMICAL || id == ITEM_BATTERY || id == ITEM_ZM_PASS_NOTE;
}

// ROOM70F0 (2F small dining room): the shells its frame script arms only
// once the lighter has lit the candles (room_action_reset of slot 4).
static bool rnd_candle_spot(const RndSpot& s)
{
    return s.stage == RND_STAGE_2F && s.room == 0x0F && s.flag == 0x0C;
}

// ROOM6060 (the main hall): the Beretta on the floor. Every survivor starts
// armed; the mode takes it out.
static bool rnd_main_hall_beretta(const RndSpot& s)
{
    return s.stage == RND_STAGE_1F && s.room == 0x06 && s.flag == 0x1A && s.origId == ITEM_BERETTA;
}

// ROOM6130 (the bathroom): the small key under the muddy water, armed once
// the bathtub is drained (both its records, flag 0x03, sit under that story
// flag, so it stays out of the pool). The small key opens nothing in the
// mode: the drained bathtub hands out a supply instead.
static bool rnd_bathtub_spot(const RndSpot& s)
{
    return s.stage == RND_STAGE_1F && s.room == 0x13 && s.flag == 0x03 &&
           s.origId == ITEM_DESK_KEY;
}

static void rnd_place_weapons(bool barry)
{
    s_rng = s_rngWeapons;
    s_t3Barry = barry;
    s_t3Placed = true;

    // Back to the key items alone.
    for (int i = 0; i < s_spotCount; i++) {
        RndSpot& s = s_spots[i];
        if (s.pool) {
            if (!rnd_is_key_item(s.id)) { s.id = 0; s.qty = 0; }
        } else {
            s.id = s.origId;
            s.qty = s.origQty;
        }
    }

    // The puzzles' heavy weapons (rnd_rank_puzzles), in their reward slots.
    bool magnum = barry, launcher = false, flame = false;
    for (int i = 0; i < s_spotCount; i++) {
        RndSpot& s = s_spots[i];
        if (s.puzzle == 0) continue;
        s.id = s_puzzleWeapon[s.puzzle - 1];
        s.qty = (s.id == ITEM_FLAMETHROWER) ? 240 : (s.id == ITEM_ROCKET_LAUNCHER) ? 4 : 6;
        if (s.id == ITEM_COLT_PYTHON_MAG) magnum = true;
        if (s.id == ITEM_BAZOOKA_EXPLOSIVE) launcher = true;
        if (s.id == ITEM_FLAMETHROWER) flame = true;
    }

    // Their ammo: the magnum's with the Python or Barry, the grenades with the
    // launcher, fuel with the flamethrower. The rocket launcher has none.
    s_t3AmmoCount = 0;
    if (launcher) s_t3Ammo[s_t3AmmoCount++] = ITEM_EXPLOSIVE_ROUNDS;
    if (flame) s_t3Ammo[s_t3AmmoCount++] = ITEM_FUEL;
    if (magnum) s_t3Ammo[s_t3AmmoCount++] = ITEM_MAGNUM_ROUNDS;

    // Tier 1 and ammo: what the pool spots held (less the keys and crests;
    // tier 3 ammo becomes the game's own tier 3 ammo, else handgun or shotgun
    // ammo), shuffled over the spots still empty.
    // The shotgun is the trap room's alone (ZombieShotgun.cpp): never on the
    // floor - ammo instead. Barry brings no handgun: one Beretta lies about
    // as his fallback when he plays.
    unsigned char wid[RND_MAX_SPOTS], wqty[RND_MAX_SPOTS];
    int wCount = 0;
    bool haveBeretta = false;
    for (int i = 0; i < s_spotCount && wCount < RND_MAX_SPOTS; i++) {
        const RndSpot& s = s_spots[i];
        if (!s.pool || !rnd_is_weapon_or_ammo(s.origId) || rnd_is_tier3(s.origId)) continue;
        wid[wCount] = s.origId;
        wqty[wCount] = s.origQty;
        if (rnd_is_tier3_ammo(s.origId)) rnd_ammo(&wid[wCount], &wqty[wCount]);
        // Shells and the shotgun's spot: shells or a clip, one in two (the
        // shotgun is the trap room's alone).
        if (s.origId == ITEM_SHELLS || s.origId == ITEM_SHOTGUN) {
            if (rnd_below(2)) { wid[wCount] = ITEM_SHELLS; wqty[wCount] = 7; }
            else              { wid[wCount] = ITEM_CLIP; wqty[wCount] = 15; }
        }
        if (wid[wCount] == ITEM_BERETTA) haveBeretta = true;
        wCount++;
    }
    if (barry && !haveBeretta && wCount < RND_MAX_SPOTS) { wid[wCount] = ITEM_BERETTA; wqty[wCount++] = 15; }
    for (int i = wCount - 1; i > 0; i--) {
        int j = rnd_below(i + 1);
        unsigned char t = wid[i]; wid[i] = wid[j]; wid[j] = t;
        t = wqty[i]; wqty[i] = wqty[j]; wqty[j] = t;
    }
    int w = 0;
    for (int i = 0; i < s_spotCount; i++) {
        RndSpot& s = s_spots[i];
        if (!s.pool || s.id != 0) continue;
        if (w < wCount) { s.id = wid[w]; s.qty = wqty[w]; w++; }
        else rnd_supply(&s.id, &s.qty);
    }

    // Spots outside the pool: a key or crest (behind a puzzle) would hand out
    // a second one, a heavy weapon (one outside the puzzles) one more than
    // the puzzles' - a supply instead; tier 3 ammo follows the game's. The
    // puzzles' items lie only where the generator put them.
    for (int i = 0; i < s_spotCount; i++) {
        RndSpot& s = s_spots[i];
        if (s.pool || s.isNew || s.puzzle != 0) continue;
        // The broken shotgun's own spot (the vacant room) is emptied: the
        // generator's leaf-room placement is the only broken shotgun. The
        // living room's record is the shotgun puzzle's plate (ZombieShotgun.cpp).
        if (s.origId == ITEM_BROKEN_SHOTGUN && !(s.stage == RND_STAGE_1F && s.room == ROOM_LIVING_ROOM)) {
            s.id = 0; s.qty = 0;
        }
        else if (rnd_trimmed_spot(s) && s_roomInfo[s.stage - RND_STAGE_1F][s.room].safe) {
            s.id = ITEM_BLUE_HERB; s.qty = 1;           // the safe room's blue herb
        }
        else if (rnd_main_hall_beretta(s) || rnd_trimmed_spot(s)) { s.id = 0; s.qty = 0; }
        else if (s.origId == ITEM_MUSIC_NOTES || s.origId == ITEM_CHEMICAL || s.origId == ITEM_INK_RIBBONS) {
            rnd_supply(&s.id, &s.qty);
        }
        else if (rnd_is_key(s.origId) || rnd_is_crest(s.origId) || rnd_is_tier3(s.origId)) rnd_supply(&s.id, &s.qty);
        else if (rnd_is_tier3_ammo(s.origId)) rnd_ammo(&s.id, &s.qty);
        // Scripted maps (including any re-armed vase pickup) remain supplies:
        // their interaction is not part of the progression route proof.
        else if (ITEM_IS_MAP(s.origId)) rnd_supply(&s.id, &s.qty);
        // The small dining room's shells, armed once its candles are lit:
        // the Ingram, a prize only Chris's lighter reaches (never on the
        // route). The mode's Ingram runs dry (zombie_mode_ingram_finite).
        else if (rnd_candle_spot(s)) { s.id = ITEM_INGRAM; s.qty = RND_INGRAM_ROUNDS; }
        else if (rnd_bathtub_spot(s)) rnd_supply(&s.id, &s.qty);
    }
    for (int i = 0; i < s_spotCount; i++) {
        RndSpot& s = s_spots[i];
        if (!s.ammoOnly) s.overridden = s.isNew || s.id != s.origId || s.qty != s.origQty;
    }
    rnd_fill_ammo();
    dbg_printf("[random] weapons%s: %d heavy ammo kind(s)\n", barry ? " with Barry" : "", s_t3AmmoCount);
}

// Until the picks are final: the floor's weapons, ammo and supplies dealt
// again whenever Barry's presence changed (his magnum rounds). The puzzles'
// weapons and the key items stay put.
static void rnd_weapons_refresh(void)
{
    if (!s_built || s_t3Locked) return;
    int survivors;
    bool barry;
    bool final = rnd_party(&survivors, &barry);
    if (!s_t3Placed || barry != s_t3Barry) rnd_place_weapons(barry);
    if (final) s_t3Locked = true;
}

static void rnd_log(void)
{
    dbg_printf("[random] seed %08X: %d doors, %d spots, %d key locks\n", s_seed, s_doorCount, s_spotCount, s_lockCount);
    for (int i = 0; i < s_doorCount; i++) {
        const RndDoor& d = s_doors[i];
        if (!d.gated) continue;
        dbg_printf("[random]   gated door %d:%02X slot %d -> %d:%02X %s\n", (int)d.fromStage + 1, (int)d.fromRoom,
                   (int)d.slot, (int)d.toStage + 1, (int)d.toRoom, d.usable ? "open" : "closed");
    }
    if (s_unreviewedGates > 0) {
        dbg_printf("[random] %d gated door(s) not reviewed in kDoorGates: counted closed\n", s_unreviewedGates);
    }
    for (int i = 0; i < s_lockCount; i++) {
        dbg_printf("[random]   lock %2d needs item %02X\n", (int)s_lockFlag[i], (unsigned)s_lockKey[i]);
    }
    dbg_printf("[random]   keypad pass number %04u (item %02X is its note)\n",
               zm_random_pass_code(), (unsigned)ITEM_ZM_PASS_NOTE);
    for (int p = 0; p < RND_PUZZLES; p++) {
        const char* name = DebugRoom_Name(kPuzzles[p].stage, kPuzzles[p].room);
        dbg_printf("[random]   puzzle in stage %d room %02X (%s): cost %d, weapon %02X\n",
                   (int)kPuzzles[p].stage + 1, (int)kPuzzles[p].room, name != NULL ? name : "?",
                   s_puzzleCost[p], (unsigned)s_puzzleWeapon[p]);
    }
    for (int i = 0; i < s_spotCount; i++) {
        const RndSpot& s = s_spots[i];
        if (rnd_is_key_item(s.id) || s.isNew) {
            const char* name = DebugRoom_Name(s.stage, s.room);
            dbg_printf("[random]   %s item %02X x%d in stage %d room %02X (%s)\n",
                       s.isNew ? "NEW PICKUP" : "spot", (unsigned)s.id, (int)s.qty, (int)s.stage + 1,
                       (int)s.room, name != NULL ? name : "?");
        }
    }
}

// Build the scenario for `seed` (a no-op when it is already built): read
// the mansion, place the keys, crests and weapons. The lobby builds it to
// show the route map; the game start applies it.
static unsigned int s_builtSeed = 0;

#ifdef QUICK_DEBUG
// Additional loaded weapons, crests and puzzle tools, outside the randomization pool so character
// changes do not replace them. Use unused flags just like ordinary new spots.
static void rnd_add_debug_pickups(void)
{
    static const unsigned char items[] = {
        ITEM_BERETTA, ITEM_SHOTGUN, ITEM_COLT_PYTHON_DUM, ITEM_COLT_PYTHON_MAG,
        ITEM_FLAMETHROWER, ITEM_BAZOOKA_EXPLOSIVE, ITEM_BAZOOKA_ACID,
        ITEM_BAZOOKA_FLAME, ITEM_ROCKET_LAUNCHER, ITEM_INGRAM, ITEM_MINIMI,
        ITEM_WIND_CREST, ITEM_MOON_CREST, ITEM_STAR_CREST, ITEM_SUN_CREST,
        ITEM_BROKEN_SHOTGUN, ITEM_PICK_AXE, ITEM_MUSIC_NOTES, ITEM_CHEMICAL,
        ITEM_ZM_PASS_NOTE, ITEM_BATTERY
    };
    static const unsigned char loads[] = { 15, 7, 6, 6, 240, 6, 6, 6, 4, 100, 100, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
    // Checked against ROOM6060's collision boundaries, camera zones and
    // stair regions. Pickup zones are disjoint and clear of the three
    // ordinary generated-pickup anchors. The last two positions are in
    // ROOM6090 beside the trap-room door, outside its interaction rectangle;
    // both are clear of collision/stairs and covered by a camera zone.
    static const short positions[][3] = {
        { 8300, 0, 26300 }, { 10700, 0, 26300 }, { 10700, 0, 25100 },
        { 7100, 0, 26300 }, { 29500, 0, 2500 }, { 30700, 0, 3700 },
        { 29500, 0, 3700 }, { 28300, 0, 2500 }, { 8700, 0, 2500 },
        { 8700, 0, 3700 }, { 9900, 0, 2500 }, { 9900, 0, 3700 },
        { 9900, 0, 4900 }, { 9900, 0, 6100 }, { 11100, 0, 3700 },
        { 6600, 0, 11600 }, { 6600, 0, 12800 },
        // The sheet music in the piano bar by the piano, outside the alcove
        // (floor gen_spawn_spots.py once picked, before the alcove was left out).
        { 10900, 0, 9900 },
        // Greenhouse entrance: clear floor beside the door's interaction zone
        // (x14200..16000, z7000..9600), visible in a camera zone. The pickup's
        // 1000-square zone ends at x14000, so it cannot intercept the door.
        { 13500, 0, 8300 },
        // ROOM7010, the 2F left stairs: the pass number's note in the
        // corridor just below the keypad door (zone z 25700 on, floor to
        // 25800), clear of the spawn spot at (16100, 23700).
        { 15300, 0, 24900 },
        // ROOM71C0, the kitchen: the battery in the corridor by the elevator
        // door (in its west wall, zone x 17500-19500, z 5300-8200), south of it.
        { 20200, 0, 4500 }
    };
    static_assert(sizeof(items) == sizeof(loads) &&
                  sizeof(items) == sizeof(positions) / sizeof(positions[0]),
                  "Every debug pickup needs a quantity and position");
    int added[2][RND_ROOMS] = {};
    int flag = 255;
    for (unsigned int i = 0; i < sizeof(items); i++) {
        unsigned char stage = (items[i] == ITEM_ZM_PASS_NOTE || items[i] == ITEM_BATTERY) ? RND_STAGE_2F : RND_STAGE_1F;
        unsigned char room = (items[i] == ITEM_BROKEN_SHOTGUN || items[i] == ITEM_PICK_AXE)
            ? ROOM_TRAP_PASSAGE : items[i] == ITEM_MUSIC_NOTES ? ROOM_MANSION_BAR
            : items[i] == ITEM_CHEMICAL ? ROOM_GREENHOUSE : items[i] == ITEM_ZM_PASS_NOTE ? 0x01
            : items[i] == ITEM_BATTERY ? ROOM_MANSION_KITCHEN : ROOM_MAIN_HALL;
        int st = stage - RND_STAGE_1F;
        const RndRoomInfo& info = s_roomInfo[st][room];
        if (!info.exists) continue;
        int k = s_newInRoom[st][room] + added[st][room];
        while (flag > 0 && (s_usedFlags[flag >> 3] & (1u << (flag & 7))) != 0) flag--;
        if (flag <= 0 || s_spotCount >= RND_MAX_SPOTS ||
            info.itemCount + k >= ZM_RANDOM_MODELS || info.highSlot + 1 + k >= ZM_DROP_SLOT_FIRST) break;
        RndSpot& s = s_spots[s_spotCount++];
        memset(&s, 0, sizeof(s));
        s.stage = stage;
        s.room = room;
        s.flag = (unsigned char)flag;
        s_usedFlags[flag >> 3] |= (unsigned char)(1u << (flag & 7));
        s.id = s.origId = items[i];
        s.qty = s.origQty = loads[i];
        s.isNew = s.overridden = true;
        s.slot = (unsigned char)(info.highSlot + 1 + k);
        s.model = (unsigned char)(info.itemCount + k);
        s.x = positions[i][0];
        s.y = positions[i][1];
        s.z = positions[i][2];
        added[st][room]++;
    }
}
#endif

bool zm_random_build(unsigned int seed)
{
    if (seed == 0) seed = 0x5EED1234;
    if (s_built && s_builtSeed == seed) return true;
    s_built = false;
    s_active = false;
    s_doorCount = 0;
    s_spotCount = 0;
    s_lockCount = 0;
    s_unreviewedGates = 0;
    memset(s_roomInfo, 0, sizeof(s_roomInfo));
    memset(s_usedFlags, 0, sizeof(s_usedFlags));
    for (int st = RND_STAGE_1F; st <= RND_STAGE_2F; st++) {
        for (int room = 0; room < RND_ROOMS; room++) rnd_read_room((unsigned char)st, (unsigned char)room);
    }
    s_curInfo = NULL;
    for (int i = 0; i < s_doorCount; i++) {
        const RndDoor& d = s_doors[i];
        unsigned char flag = (unsigned char)(d.lock & 0x3F);
        if (rnd_door_key_lock(d) && rnd_lock_index(flag) < 0 && s_lockCount < RND_MAX_LOCKS) {
            s_lockFlag[s_lockCount++] = flag;
        }
    }
    s_seed = seed;
    s_rng = seed;
    rnd_add_new_spots();
    bool ok = false;
    for (int attempt = 0; attempt < 200 && !ok; attempt++) ok = rnd_generate_once();
    if (!ok) {
        dbg_printf("[random] no route found for seed %08X: the mansion stays as it is\n", seed);
        return false;
    }
    s_rngWeapons = s_rng;
    int survivors;
    bool barry;
    rnd_party(&survivors, &barry);
    rnd_place_weapons(barry);
#ifdef QUICK_DEBUG
    rnd_add_debug_pickups();
#endif
    s_built = true;
    s_builtSeed = seed;
    s_t3Locked = false;
    rnd_log();
    return true;
}

// zombie_mode_new_game, an armed new game: this game's scenario, applied.
void zm_random_new_game(void)
{
    s_active = false;
    // ROOM60C0 tests this bit CLEAR for live vines; chemical use sets it for
    // the rest of the match. Every new match starts with the greenhouse unsolved.
    FUN_00473f10((int*)g_ScenarioFlags2, 0xA6);
    // The back area starts shut: the elevator without power, the keypad
    // door locked (ZombieKeypad.cpp).
    zm_access_new_game();
    // LoadRoomRdt restores the first visit's crest puzzle in the roofed
    // passage. Start with empty recesses and its door locked on every copy;
    // the original room scripts set these flags as survivors place crests.
    for (unsigned int bit = 0x69; bit <= 0x6C; bit++) {
        FUN_00473f10((int*)&g_ScenarioFlags, bit);
    }
    FUN_00473f10((int*)g_LocksFlags, RND_CREST_LOCK);
    FUN_00473f10((int*)g_LocksFlags, RND_ADDED_LOCK);
    unsigned int seed = (zm_game_role() != ZM_NET_OFF) ? zm_net_seed() : (zm_game_time_ms() * 2654435761u);
    if (!zm_random_build(seed)) return;
    s_active = true;
    s_t3Locked = false;
    rnd_weapons_refresh();

    // The puzzles are skipped: every lock that is not a key's or the crest
    // door's starts open.
    for (int i = 0; i < s_doorCount; i++) {
        const RndDoor& d = s_doors[i];
        unsigned char flag = (unsigned char)(d.lock & 0x3F);
        if ((d.lock & 0x80) != 0 && !rnd_door_key_lock(d) && flag != RND_CREST_LOCK) {
            Flg_on((int)g_LocksFlags, flag);
        }
#ifdef QUICK_DEBUG
        // Every locked door starts unlocked - the key doors and the crest door.
        if ((d.lock & 0x80) != 0) Flg_on((int)g_LocksFlags, flag);
#endif
    }
}

bool zm_random_active(void)
{
    return s_active;
}

// cmd_item_model_set: the item and quantity this game puts at the spot.
bool zm_random_item(unsigned char stage, unsigned char room, unsigned char flag,
                    unsigned char* id, unsigned char* qty)
{
    if (!s_active) return false;
    rnd_weapons_refresh();
    for (int i = 0; i < s_spotCount; i++) {
        const RndSpot& s = s_spots[i];
        if (s.stage == stage && s.room == room && s.flag == flag) {
            if (!s.overridden) return false;
            *id = s.id;
            *qty = s.qty;
            return true;
        }
    }
    return false;
}

// door_try_enter: the key a key-locked door needs this game.
unsigned char zm_random_door_need(unsigned char lockFlag, unsigned char need)
{
    if (!s_active || !rnd_is_key(need)) return need;
    int li = rnd_lock_index(lockFlag);
    return li >= 0 ? s_lockKey[li] : need;
}

// cmd_door_set / the AI survivor's door cache: a door record's lock and need
// with the mode's own key locks (kAddedLocks), while this game's scenario is on.
void zm_random_door_lock(unsigned char stage, unsigned char room, unsigned char slot, unsigned char dest,
                         unsigned char* lock, unsigned char* need)
{
    if (s_active) rnd_added_lock(stage, room, slot, dest, lock, need);
}

// For the route map: the key-locked doors of (stage, room), as "to room":"key".
int zm_random_room_locks(unsigned char stage, unsigned char room, unsigned char* toRoom,
                         unsigned char* key, int max)
{
    if (!s_built) return 0;
    int n = 0;
    for (int i = 0; i < s_doorCount && n < max; i++) {
        const RndDoor& d = s_doors[i];
        if (d.fromStage != stage || d.fromRoom != room || !rnd_door_key_lock(d)) continue;
        bool dup = false;
        for (int k = 0; k < n && !dup; k++) dup = toRoom[k] == d.toRoom;
        if (dup) continue;
        int li = rnd_lock_index((unsigned char)(d.lock & 0x3F));
        toRoom[n] = d.toRoom;
        key[n] = li >= 0 ? s_lockKey[li] : d.need;
        n++;
    }
    return n;
}

unsigned int zm_random_seed(void)
{
    return s_seed;
}

// Four digits from the seed, apart from the generator's own draws: every copy
// knows the same number without it being sent.
unsigned int zm_random_pass_code(void)
{
    unsigned int h = s_seed * 2654435761u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return h % 10000u;
}

// ---- Engine hooks (ZombieMode.h) ----
// The key items' sparkle (cmd_item_model_set: bit 0x8000 of the record's
// +0x18 word attaches the billboard, 0x0F00 picks its kind - 0x0700 the sword
// key's in ROOM1000), so a pickup the scenario moved is easy to spot.
#define RND_SPARKLE      0x8000
#define RND_SPARKLE_KIND 0x0700

// ROOM6070 displays the map in model/action slot 2, but its real interaction
// is slot 5 (event 9), re-armed by the frame script. Keep a pickup record with
// that interaction's zone and the randomized model's item/quantity/flag.
static unsigned char s_galleryPickup[0x1A];
static const RndSpot* rnd_spot_here(unsigned char flag);

// Spots whose records have no item model of the room's own (the item is in
// the background picture) and so no position: (0,0,0), no parent. A new
// item's look needs one - on the floor in front of the furniture the
// background shows the item on, clear of the collision boxes (checked
// against ROOM70F0's boundaries).
struct RndLooseSpot {
    unsigned char stage, room, flag;
    short         x, y, z;
};
static const RndLooseSpot kLooseSpots[] = {
    { RND_STAGE_2F, 0x0F, 0x0B, 9800, 0, 11800 },   // the clip: zone x 10000.., z 11300..12300
    { RND_STAGE_2F, 0x0F, 0x0C, 4700, 0, 3100 },    // the candle shells: zone x ..4400, z 1900..4300
};

static const int* rnd_look(unsigned char id);
static const RndSpot* rnd_spot_here(unsigned char flag);
static void rnd_tmd_extent(const unsigned char* tmd, int ext[3], int* lo = NULL, int* hi = NULL);

// The item a spot shows: the pass number's note lies as the red book.
static unsigned char rnd_look_id(unsigned char id)
{
    return id == ITEM_ZM_PASS_NOTE ? ITEM_RED_BOOK : id;
}

// A changed spot lies where and the way the room's own item lay: the new
// look (centred on its origin, bottom at 0 - rnd_rescale) moves onto the
// room model's centre and bottom - a room model need not be centred on the
// record's position (the terrace passage's small key is not, so a clip hung
// off the shelf) - and when the look's long side runs across the room
// model's (x against z) it is turned a quarter (the record's y rotation,
// +0x14). Records without a model of their own (the item is in the
// background) keep their position and rotation.
// A raised spot (on a shelf, a table: its record above the floor) keeps
// the new look's footprint on the furniture under it - the smallest solid
// collision box (RDT_Boundary, shapes 1 and 5) holding the position - moved
// in no further than needed; centred on it when the box is smaller than the
// look. The terrace passage's small key lay 126 units from its shelf's front
// edge, so the larger clip there hung off it.
#define RND_SHELF_MARGIN 30
static void rnd_keep_on_furniture(unsigned char* op, const int lookExt[3])
{
    int x = *(const short*)(op + 0x0E), y = *(const short*)(op + 0x10), z = *(const short*)(op + 0x12);
    if (y > -100 || g_RdtPointer == NULL || g_RdtPointer->boundaries == NULL) return;
    const RDT_BoundaryHeader* hdr = (const RDT_BoundaryHeader*)g_RdtPointer->boundaries;
    const RDT_Boundary* best = NULL;
    long long bestArea = 0;
    for (const RDT_Boundary* r = hdr->group[0]; r < hdr->group[4]; r++) {
        unsigned int shape = r->type & 0xFF;
        if (shape != 1 && shape != 5) continue;
        if (x < (int)r->xMin || x > (int)r->xMax || z < (int)r->zMin || z > (int)r->zMax) continue;
        long long area = (long long)(r->xMax - r->xMin) * (long long)(r->zMax - r->zMin);
        if (best == NULL || area < bestArea) { best = r; bestArea = area; }
    }
    if (best == NULL) return;
    // The look's half footprint at the record's y rotation.
    double rot = (*(const unsigned short*)(op + 0x14) & 0xFFF) * (6.283185307179586 / 4096.0);
    double c = fabs(cos(rot)), sn = fabs(sin(rot));
    int hx = (int)((c * lookExt[0] + sn * lookExt[2]) / 2.0) + RND_SHELF_MARGIN;
    int hz = (int)((sn * lookExt[0] + c * lookExt[2]) / 2.0) + RND_SHELF_MARGIN;
    int x0 = (int)best->xMin + hx, x1 = (int)best->xMax - hx;
    int z0 = (int)best->zMin + hz, z1 = (int)best->zMax - hz;
    int nx = x0 > x1 ? ((int)best->xMin + (int)best->xMax) / 2 : x < x0 ? x0 : x > x1 ? x1 : x;
    int nz = z0 > z1 ? ((int)best->zMin + (int)best->zMax) / 2 : z < z0 ? z0 : z > z1 ? z1 : z;
    if (nx != x || nz != z) {
        dbg_printf("[random] look kept on the furniture: (%d,%d) -> (%d,%d)\n", x, z, nx, nz);
        *(short*)(op + 0x0E) = (short)nx;
        *(short*)(op + 0x12) = (short)nz;
    }
}

static void rnd_match_orientation(unsigned char* op, unsigned char id)
{
    // Only the room's own records: the new spots' model slots lie past the
    // RDT's item_models (item_count is raised for them).
    const RndSpot* spot = rnd_spot_here(op[0x16]);
    if (id == 0 || g_RdtPointer == NULL || spot == NULL || spot->isNew) return;
    int st = g_stageId - RND_STAGE_1F;
    if (st < 0 || st > 1 || g_roomId >= RND_ROOMS || op[0x0C] >= s_roomInfo[st][g_roomId].itemCount) return;
    const unsigned char* own = *(const unsigned char* const*)(g_RdtPointer->item_models + op[0x0C] * 8);
    const int* look = rnd_look(rnd_look_id(id));
    if (own == NULL || look == NULL) return;
    int a[3], b[3], lo[3], hi[3];
    rnd_tmd_extent(own, a, lo, hi);
    rnd_tmd_extent((const unsigned char*)look[0], b);
    if (op[0x0D] == 0xFF) {                               // room coordinates (no parent)
        // The room model's centre in the room: its model-space centre turned
        // by the record's y rotation (4096 a turn, the GTE's RotMatrix y).
        double cx = (lo[0] + hi[0]) / 2.0, cz = (lo[2] + hi[2]) / 2.0;
        double r = (*(const unsigned short*)(op + 0x14) & 0xFFF) * (6.283185307179586 / 4096.0);
        double c = cos(r), sn = sin(r);
        int dx = (int)(cx * c + cz * sn), dz = (int)(-cx * sn + cz * c);
        *(short*)(op + 0x0E) = (short)(*(const short*)(op + 0x0E) + dx);
        *(short*)(op + 0x10) = (short)(*(const short*)(op + 0x10) + hi[1]);   // its bottom (largest y)
        *(short*)(op + 0x12) = (short)(*(const short*)(op + 0x12) + dz);
    }
    if (a[0] != a[2] && b[0] != b[2] && (a[0] > a[2]) != (b[0] > b[2])) {
        *(unsigned short*)(op + 0x14) = (unsigned short)((*(unsigned short*)(op + 0x14) + 0x400) & 0xFFF);
    }
    if (op[0x0D] == 0xFF) rnd_keep_on_furniture(op, b);
}

void zombie_mode_item_spot(unsigned char* op)
{
    unsigned char id, qty;
    // The attic's key or crest, held back while Yawn is there (ZombieYawn.cpp).
    zm_yawn_item_before(op);
    if (zm_random_item(g_stageId, g_roomId, op[0x16], &id, &qty) && id == 0) {
        // An emptied spot: the item counts as taken. The record still runs as
        // the original's does after a pickup (no model, no pickup; the room's
        // script re-points the slot with its zone, as for a taken item).
        FUN_00473f10((int*)g_roomItemsFlags, op[0x16]);
        return;
    }
    if (zm_random_item(g_stageId, g_roomId, op[0x16], &id, &qty)) {
        op[10] = id;
        op[11] = qty;
        rnd_match_orientation(op, id);
        for (unsigned int i = 0; i < sizeof(kLooseSpots) / sizeof(kLooseSpots[0]); i++) {
            const RndLooseSpot& L = kLooseSpots[i];
            if (L.stage != g_stageId || L.room != g_roomId || L.flag != op[0x16] || op[0x0D] != 0xFF) continue;
            *(short*)(op + 0x0E) = L.x;
            *(short*)(op + 0x10) = L.y;
            *(short*)(op + 0x12) = L.z;
        }
        if (g_stageId == RND_STAGE_1F && g_roomId == ROOM_GALLERY && op[0x16] == 0x91)
            memcpy(s_galleryPickup, op, sizeof(s_galleryPickup));
        // A spot's own sparkle (and its height) stays; one without gets the
        // sword key's. The record is the loaded RDT's, read afresh each load.
        unsigned short f = *(unsigned short*)(op + 0x18);
        if (id != 0 && (f & RND_SPARKLE) == 0) {
            *(unsigned short*)(op + 0x18) = (unsigned short)((f & ~0x0F00) | RND_SPARKLE | RND_SPARKLE_KIND);
        }
    }
    zm_yawn_item_after(op);
}

void zombie_mode_item_action(unsigned char slot)
{
    if (slot == 2 || slot == 4 || slot == 7) zm_yawn_hole_action();
    if (!s_active || g_stageId != RND_STAGE_1F || g_roomId != ROOM_GALLERY || slot != 5)
        return;
    const RndSpot* spot = rnd_spot_here(0x91);
    if (spot == NULL || !spot->overridden || ITEM_IS_MAP(spot->id)) return;
    unsigned char* entry = &g_RoomActionTable[slot * 0xC];
    if (spot->id == 0 || Flg_ck((int)&g_roomItemsFlags, spot->flag) == 0) {
        entry[0] = 0;
        return;
    }
    // Preserve the native zone and probe flags (including temporary disarming),
    // but bypass the hardcoded map message and use the ordinary inventory UI.
    const unsigned char* zone = *(const unsigned char**)(entry + 8);
    memmove(s_galleryPickup + 2, zone, 8);
    entry[0] = 4;
    *(unsigned short*)(entry + 2) = *(unsigned short*)(s_galleryPickup + 0x18) & 1;
    *(unsigned short*)(entry + 4) = s_galleryPickup[0xC];
    *(unsigned short*)(entry + 6) = spot->flag;
    *(unsigned char**)(entry + 8) = s_galleryPickup + 2;
}

unsigned char zombie_mode_door_need(unsigned char lockFlag, unsigned char need)
{
    return zm_random_door_need(lockFlag, need);
}

void zombie_mode_door_record(unsigned char slot, unsigned char* record)
{
    if (!zombie_mode_armed()) return;
    zm_random_door_lock(g_stageId, g_roomId, slot, record[0x0D], &record[0x0C], &record[0x16]);
}

// ===========================================================================
// In the room: the items' looks, and the new spots
// ===========================================================================
extern int  cmd_item_model_set(void);                                  // 0x00461220 CmdFunctions.cpp
extern void ProcessTmdAsync(unsigned int param1);                      // 0x004838e0
extern unsigned int ProcessTmdTextures(char mode, unsigned int* tmdBase, int bank, int depth);  // 0x00483560

// ---------------------------------------------------------------------------
// An item's look: its inventory view model (item_m2/iNNv.ivm - the TIM of its
// texture page, then its model, in the same TMD layout as the rooms' item
// models), scaled down to lie in a room. A view model is centred and sized to
// fill the examine screen - four to nine times a room model - so its
// vertices are rescaled to a size for its kind of item, stood on the floor
// (a room item model's y runs from -height up to 0) and, when it is tallest
// along y (a gun stood on end), turned to lie flat. Loaded once per room per
// item, texture on the mod's page counter (zm_ext_begin).
// ---------------------------------------------------------------------------
struct RndLook {
    unsigned char id;
    bool          ok;
    int           model[2];         // { TMD, TIM }, as the RDT's item_models pairs
};
#define RND_MAX_LOOKS 24
static RndLook       s_looks[RND_MAX_LOOKS];
static int           s_lookCount = 0;
static unsigned char s_lookPool[2400 * 1024];   // ~85 KB a view model, RND_MAX_LOOKS of them
static int           s_lookUsed = 0;

static int rnd_look_size(unsigned char id)
{
    if (id == ITEM_INGRAM) return 625;
    if (id == ITEM_MINIMI) return 1500;
    if (id == ITEM_SHOTGUN || (id >= ITEM_FLAMETHROWER && id <= ITEM_ROCKET_LAUNCHER)) return 1500;
    if (id == ITEM_BERETTA || id == ITEM_COLT_PYTHON_DUM || id == ITEM_COLT_PYTHON_MAG) return 450;
    if (rnd_is_key(id)) return 330;
    if (rnd_is_crest(id)) return 380;
    if (id >= ITEM_RED_HERB && id <= ITEM_MIX_GREEN_BLUE) return 450;
    return 400;
}

// A TMD's extent along x, y and z (all its objects, up to 16). Once bound
// (ResolveAnimPointers: bit 0 of +4 set) the object table holds absolute
// pointers instead of offsets from it - the looks are by the time a spot
// asks (ProcessTmdTextures), a room model may be.
static void rnd_tmd_extent(const unsigned char* tmd, int ext[3], int* lo, int* hi)
{
    int nobj = *(const int*)(tmd + 8);
    const unsigned char* objs = tmd + 0xC;
    bool resolved = (tmd[4] & 1) != 0;
    int mn[3] = { 32767, 32767, 32767 }, mx[3] = { -32768, -32768, -32768 };
    for (int o = 0; o < nobj && o < 16; o++) {
        unsigned int vt = *(const unsigned int*)(objs + o * 0x1C);
        int nv = *(const int*)(objs + o * 0x1C + 4);
        const short* v = resolved ? (const short*)vt : (const short*)(objs + vt);
        for (int i = 0; i < nv; i++, v += 4) {
            for (int a = 0; a < 3; a++) {
                if (v[a] < mn[a]) mn[a] = v[a];
                if (v[a] > mx[a]) mx[a] = v[a];
            }
        }
    }
    for (int a = 0; a < 3; a++) {
        ext[a] = mx[a] > mn[a] ? mx[a] - mn[a] : 0;
        if (lo != NULL) lo[a] = mx[a] >= mn[a] ? mn[a] : 0;
        if (hi != NULL) hi[a] = mx[a] >= mn[a] ? mx[a] : 0;
    }
}

static void rnd_rescale(unsigned char* tmd, unsigned char id)
{
    int nobj = *(int*)(tmd + 8);
    unsigned char* objs = tmd + 0xC;
    int mn[3] = { 32767, 32767, 32767 }, mx[3] = { -32768, -32768, -32768 };
    for (int o = 0; o < nobj && o < 16; o++) {
        unsigned int vt = *(unsigned int*)(objs + o * 0x1C);
        int nv = *(int*)(objs + o * 0x1C + 4);
        short* v = (short*)(objs + vt);
        for (int i = 0; i < nv; i++, v += 4) {
            for (int a = 0; a < 3; a++) {
                if (v[a] < mn[a]) mn[a] = v[a];
                if (v[a] > mx[a]) mx[a] = v[a];
            }
        }
    }
    int ext[3] = { mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2] };
    int big = ext[0] > ext[1] ? ext[0] : ext[1];
    if (ext[2] > big) big = ext[2];
    if (big <= 0) return;
    bool layFlat = ext[1] > ext[0] && ext[1] > ext[2];
    // The special weapons stand on their grips (thin along x): lie them on
    // their side instead - a quarter turn about z, x up.
    bool onSide = (id == ITEM_INGRAM || id == ITEM_MINIMI) && ext[0] < ext[1] && ext[0] < ext[2];
    int target = rnd_look_size(id);
    int cx = (mn[0] + mx[0]) / 2, cy = (mn[1] + mx[1]) / 2, cz = (mn[2] + mx[2]) / 2;
    for (int o = 0; o < nobj && o < 16; o++) {
        unsigned char* ob = objs + o * 0x1C;
        unsigned int vt = *(unsigned int*)(ob);
        int nv = *(int*)(ob + 4);
        unsigned int nt = *(unsigned int*)(ob + 8);
        int nn = *(int*)(ob + 12);
        short* v = (short*)(objs + vt);
        for (int i = 0; i < nv; i++, v += 4) {
            int x = (v[0] - cx) * target / big;
            int y = (v[1] - cy) * target / big;
            int z = (v[2] - cz) * target / big;
            if (layFlat) { int t = y; y = -z; z = t; }   // a quarter turn about x
            if (onSide) { int t = y; y = -x; x = t; }    // a quarter turn about z
            v[0] = (short)x; v[1] = (short)y; v[2] = (short)z;
        }
        if (layFlat) {
            short* n = (short*)(objs + nt);
            for (int i = 0; i < nn; i++, n += 4) { short t = n[1]; n[1] = (short)-n[2]; n[2] = t; }
        }
        if (onSide) {
            short* n = (short*)(objs + nt);
            for (int i = 0; i < nn; i++, n += 4) { short t = n[1]; n[1] = (short)-n[0]; n[0] = t; }
        }
    }
    // On the floor: the lowest point (largest y) at 0.
    int maxY = -32768;
    for (int o = 0; o < nobj && o < 16; o++) {
        unsigned int vt = *(unsigned int*)(objs + o * 0x1C);
        int nv = *(int*)(objs + o * 0x1C + 4);
        short* v = (short*)(objs + vt);
        for (int i = 0; i < nv; i++, v += 4) if (v[1] > maxY) maxY = v[1];
    }
    for (int o = 0; o < nobj && o < 16; o++) {
        unsigned int vt = *(unsigned int*)(objs + o * 0x1C);
        int nv = *(int*)(objs + o * 0x1C + 4);
        short* v = (short*)(objs + vt);
        for (int i = 0; i < nv; i++, v += 4) v[1] = (short)(v[1] - maxY);
    }
}

static const int* rnd_look(unsigned char id)
{
    for (int i = 0; i < s_lookCount; i++) {
        if (s_looks[i].id == id) return s_looks[i].ok ? s_looks[i].model : NULL;
    }
    if (s_lookCount >= RND_MAX_LOOKS) return NULL;
    RndLook& L = s_looks[s_lookCount++];
    L.id = id;
    L.ok = false;
    const unsigned char* specialName = NULL;
    if (id == ITEM_INGRAM) specialName = g_ItemModelFileNameING;
    if (id == ITEM_MINIMI) specialName = g_ItemModelFileNameMINI;
    if (id >= 0x4D && specialName == NULL) return NULL;
    unsigned char index = specialName ? 0 : g_ItemImageLookupTable[id * 4];
    if (index >= ITEM_MODEL_NAME_COUNT) return NULL;
    char name[9];
    memcpy(name, specialName ? specialName : (const unsigned char*)g_ItemModelFileNames[index], 8);
    name[8] = '\0';
    char path[96];
    snprintf(path, sizeof(path), GAME_DATA_ROOT "item_m2/%s.ivm", name);
    unsigned char* buf = s_lookPool + s_lookUsed;
    size_t room = sizeof(s_lookPool) - (size_t)s_lookUsed;
    size_t size = LoadFile(path, buf, 0x20);
    if (size == (size_t)-1 || size < 0x40 || size > room) {
        dbg_printf("[random] no look for item %02X (%s)\n", (unsigned)id, path);
        return NULL;
    }
    unsigned int clutLen = *(unsigned int*)(buf + 8);
    unsigned int imgLen = *(unsigned int*)(buf + 8 + clutLen);
    unsigned int tmdOff = 8 + clutLen + imgLen;
    if (tmdOff + 0x28 > size) return NULL;
    s_lookUsed += (int)((size + 3) & ~3u);
    unsigned char* tmd = buf + tmdOff;
    rnd_rescale(tmd, id);
    // The texture on the mod's own pages, then the model's UVs onto them.
    if (!zm_ext_begin(2)) {
        dbg_printf("[random] no texture page left for item %02X\n", (unsigned)id);
        return NULL;
    }
    int bank = g_TextureBankID, page = g_TextureCurrentPage;
    ProcessTmdAsync((unsigned int)buf);
    zm_ext_end();
    ProcessTmdTextures(2, (unsigned int*)tmd, bank, page);
    L.model[0] = (int)tmd;
    L.model[1] = (int)buf;
    L.ok = true;
    return L.model;
}

void zm_random_room_reset(void)
{
    if (s_active) rnd_weapons_refresh();
    s_lookCount = 0;
    s_lookUsed = 0;
}

// An item's floor look for anything else that puts items down (the survivors'
// drops, ZombieDrops.cpp); NULL if it has none.
const int* zm_random_look(unsigned char id)
{
    return rnd_look(id);
}

// Has the item a view model to lie on the floor as?
// cmd_picked_item_test: room scripts disarm a pickup's own scene once "their"
// item is taken (picked_item_test on the original id - the bathtub's small
// key, ROOM6130; the shed's battery, ROOM7190). A changed spot hands out
// another item, so its original id passes too once that spot's new item
// has just been taken.
bool zombie_mode_picked_original(unsigned char testId)
{
    if (!s_active || g_pickedItemId == 0) return false;
    for (int i = 0; i < s_spotCount; i++) {
        const RndSpot& s = s_spots[i];
        if (s.stage != g_stageId || s.room != g_roomId || !s.overridden || s.origId != testId) continue;
        if (s.id == g_pickedItemId && Flg_ck((int)g_roomItemsFlags, s.flag) == 0) return true;
    }
    return false;
}

bool zm_random_has_look(unsigned char id)
{
    if (id == ITEM_INGRAM || id == ITEM_MINIMI) return true;   // ING.ivm / MINI.ivm (rnd_look)
    return id != 0 && id < 0x4D && g_ItemImageLookupTable[id * 4] < ITEM_MODEL_NAME_COUNT;
}

// roomItems flags neither a mansion script nor this scenario uses (ascending).
int zm_random_free_flags(unsigned char* out, int max)
{
    int n = 0;
    for (int f = 1; f < 256 && n < max; f++) {
        if ((s_usedFlags[f >> 3] & (1u << (f & 7))) == 0) out[n++] = (unsigned char)f;
    }
    return n;
}

static const RndSpot* rnd_spot_here(unsigned char flag)
{
    for (int i = 0; i < s_spotCount; i++) {
        const RndSpot& s = s_spots[i];
        if (s.stage == g_stageId && s.room == g_roomId && s.flag == flag) return &s;
    }
    return NULL;
}

// cmd_item_model_set: the model to show at this spot, NULL to keep the
// room's own. A spot whose item changed shows the new item.
const int* zombie_mode_item_look(const unsigned char* op)
{
    const RndSpot* s = s_active ? rnd_spot_here(op[0x16]) : NULL;
    if (s == NULL) return zm_drop_look(op);       // a survivor's dropped item
    if (!s->overridden || s->id == 0) return NULL;
    return rnd_look(rnd_look_id(s->id));
}

bool zm_random_note_slot(unsigned char slot)
{
    if (!s_active) return false;
    for (int i = 0; i < s_spotCount; i++) {
        const RndSpot& s = s_spots[i];
        if (s.isNew && s.id == ITEM_ZM_PASS_NOTE && s.slot == slot && s.stage == g_stageId && s.room == g_roomId)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// The new spots: an item_model_set record of our own per spot, run through
// cmd_item_model_set as the script would run it. The record must outlive the
// command - the pickup reads its item from it later - and the item model
// record (0xA4 bytes, g_item_model_table) is ours too: the room allocates
// only its own count, and the count in the RDT header (which render_room_objects
// walks) goes up by one per spot.
// ---------------------------------------------------------------------------
static unsigned char s_newOps[RND_ROOM_PICKUPS][0x20];
static unsigned char s_newModelRecs[RND_ROOM_PICKUPS][0xA4];

void zombie_mode_room_prepare(void)
{
    if (!zombie_mode_armed() || g_stageId != RND_STAGE_2F) return;
    if (g_roomId == ROOM_LESSON_ROOM || g_roomId == ROOM_MANSION_B1_PASSAGE_1) {
        // ROOM70C0's init/frame scripts use 0x27 for the broken floor and
        // 0x28 for the ladder/furniture moved aside. Both set before init,
        // the native hole boundary, open-room visuals and descent prompt are
        // used on arrival, without the piano's snake entrance scene.
        // ZombieYawn.cpp still gates both travel prompts on Yawn 2's death;
        // the visible hole alone must not provide a way around the fight.
        Flg_on((int)&g_ScenarioFlags, 0x27);
        Flg_on((int)&g_ScenarioFlags, 0x28);
    } else if (g_roomId == ROOM_MANSION_KITCHEN) {
        // ROOM71C0 disables elevator slot 2 while 0x33 is clear, then plays
        // the power-restoration scene before setting it. Start in its enabled
        // state so that scene and its per-frame re-arm are unnecessary.
        Flg_on((int)&g_ScenarioFlags, 0x33);
    }
}

static void rnd_open_piano_room(void)
{
    if (!s_active || !zm_piano_open() || g_RdtPointer == NULL ||
        g_stageId != RND_STAGE_1F || g_roomId != ROOM_MANSION_BAR ||
        g_RdtPointer->omodel_slot_count == 0 || g_omodel_table[0] == NULL) return;
    // ROOM60F0 object 0 is the piano alcove's sliding wall. Once the piano
    // is played (ZombiePiano.cpp) its init puts the wall away; remove its
    // active bit as well after room init: rendering and object collision
    // both skip inactive models, even if an emblem event later moves it.
    // The bar's entrance still uses the seed's assigned mansion key.
    ((unsigned char*)g_omodel_table[0])[0] &= (unsigned char)~1u;
}

void zm_random_room_loaded(void)
{
    if (!s_active || g_RdtPointer == NULL) return;
    rnd_open_piano_room();
    int n = 0;
    for (int i = 0; i < s_spotCount && n < RND_ROOM_PICKUPS; i++) {
        const RndSpot& s = s_spots[i];
        if (!s.isNew || s.id == 0 || s.stage != g_stageId || s.room != g_roomId) continue;
        int idx = g_RdtPointer->item_count;
        if (idx >= ZM_RANDOM_MODELS) break;
        unsigned char* model = s_newModelRecs[n];
        memset(model, 0, sizeof(s_newModelRecs[n]));
        g_item_model_table[idx] = model;
        g_RdtPointer->item_count = (unsigned char)(idx + 1);

        unsigned char* op = s_newOps[n];
        memset(op, 0, sizeof(s_newOps[n]));
        op[0x00] = 0x18;
        op[0x01] = s.slot;
        *(unsigned short*)(op + 0x02) = (unsigned short)(s.x - 500);   // pickup zone, 1000 square
        *(unsigned short*)(op + 0x04) = (unsigned short)(s.z - 500);
        *(unsigned short*)(op + 0x06) = 1000;
        *(unsigned short*)(op + 0x08) = 1000;
        op[0x0A] = s.id;
        op[0x0B] = s.qty;
        op[0x0C] = (unsigned char)idx;
        op[0x0D] = 0xFF;                                    // no parent: room coordinates
        *(short*)(op + 0x0E) = s.x;
        *(short*)(op + 0x10) = s.y;
        *(short*)(op + 0x12) = s.z;
        *(unsigned short*)(op + 0x14) = (unsigned short)s.angle;   // +0x72 rotation, y
        op[0x16] = s.flag;
        op[0x17] = 0x81;                                    // armed, action press
        // As the rooms' floor items (bit 0), sparkling (zombie_mode_item_spot).
        *(unsigned short*)(op + 0x18) = 1 | RND_SPARKLE | RND_SPARKLE_KIND;

        unsigned char* saved = g_ScdOpcodes;
        g_ScdOpcodes = op;
        cmd_item_model_set();
        g_ScdOpcodes = saved;
        // The pass number's note is read where it lies, by everyone, never
        // taken: its action press starts no pickup but zm_access_room_event
        // (create_room_event's handler, through zombie_mode_skip_room_event).
        if (s.id == ITEM_ZM_PASS_NOTE) {
            unsigned char* entry = &g_RoomActionTable[s.slot * 12];
            if (entry[0] != 0) entry[0] = 9;
        }
#ifdef QUICK_DEBUG
        // These unlock weapons normally never lie in a room. Their high
        // item ids select the document handler; use the ordinary pickup
        // handler instead, preserving zero for a gun already collected.
        if (s.id == ITEM_INGRAM || s.id == ITEM_MINIMI) {
            unsigned char* entry = &g_RoomActionTable[s.slot * 12];
            if (entry[0] != 0) entry[0] = 4;
        }
#endif
        n++;
        dbg_printf("[random] new spot: item %02X x%d at (%d,%d,%d), slot %d model %d flag %d\n",
                   (unsigned)s.id, (int)s.qty, (int)s.x, (int)s.y, (int)s.z, (int)s.slot, idx, (int)s.flag);
    }
}

// For the director's route map: what this game puts in (stage, room) - the
// keys, crests, weapons and supplies at changed spots and new pickups (ids,
// quantities, and whether each is a new pickup). Spots holding what they
// always held are left out.
// The director's map in the game: does a key or crest of the scenario still
// lie in (stage, room)? A spot's roomItems flag is cleared when it is taken
// (merged across copies, ZombieWorld.cpp).
bool zm_random_room_key_left(unsigned char stage, unsigned char room)
{
    if (!s_active) return false;
    for (int i = 0; i < s_spotCount; i++) {
        const RndSpot& s = s_spots[i];
        if (s.stage != stage || s.room != room || !s.overridden) continue;
        if (!rnd_is_key(s.id) && !rnd_is_crest(s.id)) continue;
        if (Flg_ck((int)g_roomItemsFlags, s.flag) != 0) return true;
    }
    return false;
}

// A mansion room that is only a 4-byte stub (no way in). False outside the
// mansion's two stages.
bool zm_random_room_stub(unsigned char stage, unsigned char room)
{
    if (!s_built || (stage % 5) > 1) return false;
    if (room >= RND_ROOMS) return true;
    return !s_roomInfo[stage % 5][room].exists;
}

// A safe room (an item box in its scripts): closed to the director.
bool zm_random_room_safe(unsigned char stage, unsigned char room)
{
    if (!s_built || room >= RND_ROOMS) return false;
    int st = stage % 5 + RND_STAGE_1F;
    if (st < RND_STAGE_1F || st > RND_STAGE_2F) return false;
    return s_roomInfo[st - RND_STAGE_1F][room].safe;
}

int zm_random_room_items(unsigned char stage, unsigned char room, unsigned char* ids,
                         unsigned char* qtys, bool* isNew, int max)
{
    if (!s_built) return 0;
    int n = 0;
    for (int i = 0; i < s_spotCount && n < max; i++) {
        const RndSpot& s = s_spots[i];
        if (s.stage != stage || s.room != room || !s.overridden || s.id == 0) continue;
        ids[n] = s.id;
        qtys[n] = s.qty;
        isNew[n] = s.isNew;
        n++;
    }
    return n;
}
