#include "ZombieModeInternal.h"
#include "ZombieNet.h"
#include "../entities/EntityCommon.h"
#include "../FileLoader.h"
#include "../../system/AssetPath.h"
#include "../../DebugPrint.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ============================================================================
// ZombieSurvivor.cpp - the "play as a zombie" mod's AI survivor: the character
// picked at the start (Chris or Jill), exploring the mansion while you hunt it.
// See ZombieMode.h for the overview. New code; the engine routines it drives
// keep their own address comments where they are defined.
//
// One room is loaded at a time, so the survivor is either
//   PRESENT - in the zombie's room, as the real g_playerEntity, running the
//             real player state machine on pad input this file synthesizes; or
//   AWAY    - a record (stage, room, position, timer) stepping along the door
//             graph. g_playerEntity is parked in a corner of the room's
//             coordinate square (see ZM_PARK_MAX).
// ============================================================================

extern unsigned int  Flg_ck(int baseAddr, unsigned int bitIndex);    // 0x00473f40
extern unsigned char weapon_autoaim_check(void);                     // 0x0045a4b0 PlayerAnimations.cpp
extern void MovePlayerXZ(int angle, SVECTOR* offset, SVECTOR* out);   // WeaponDamage.cpp

// ===========================================================================
// The door graph, read out of each room's RDT
// ===========================================================================

// Operand widths of the SCD commands 0x00-0x50 (the bytes after the opcode),
// as tools/mine_room_scd.py lists them with two corrections taken from the
// handlers' own pointer advances: 0x1F omodel_set is 28 bytes
// (cmd_omodel_set: g_ScdOpcodes += 0x1c) and 0x46 room_lights_set is 44
// (cmd_room_lights_set: 2 + 3 x 12 + 3 x 2). With those, every init script of
// all 320 shipped USA RDTs walks to exactly the end of each block.
static const unsigned char kScdOperandWidth[0x51] = {
    0, 1, 1, 1, 3, 3, 3, 5, 3, 1, 1, 3, 25, 17, 1, 7,
    1, 1, 9, 3, 3, 1, 1, 9, 25, 3, 1, 21, 5, 1, 3, 27,
    13, 13, 3, 1, 3, 3, 0, 1, 5, 1, 11, 3, 1, 1, 0, 3,
    11, 3, 3, 1, 1, 3, 3, 3, 3, 1, 3, 5, 5, 11, 1, 5,
    15, 3, 3, 3, 1, 1, 43, 13, 1, 1, 1, 1, 3, 1, 3, 1,
    1,
};

struct ZmRoomDoors {
    unsigned char stage, room, valid, count;
    ZmDoor doors[ZM_MAX_DOORS];
    bool loadable;
    unsigned char spawnCount;
    ZmSpawn spawns[ZM_MAX_SPAWNS];
};

static ZmRoomDoors s_doorCache[64];
static int s_doorCacheNext = 0;
// Large enough for any shipped RDT (the biggest is ~630 KB), as the debug
// menu's neighbour lookup uses.
static unsigned char s_rdtScratch[768 * 1024];

void zm_decode_dest(unsigned char dest, unsigned char fromStage,
                    unsigned char* stage, unsigned char* room)
{
    // room_transition_load (DoorSystem.cpp): < 0x20 stays in the stage;
    // otherwise (dest >> 5) - 1, +5 for stages 0/1 once the scenario's
    // stage-variant bit is set.
    *room = (unsigned char)(dest & 0x1F);
    if (dest < 0x20) {
        *stage = fromStage;
        return;
    }
    unsigned char s = (unsigned char)((dest >> 5) - 1);
    if (Flg_ck((int)&g_ScenarioFlags, SCENARIO_FLAG_STAGE_VARIANT) != 0 && s < 2) {
        s = (unsigned char)(s + 5);
    }
    *stage = s;
}

// Every door_set (0x0C) in the room's init SCD, in script order. Conditional
// ones (inside if blocks) are included: the graph only needs to know where a
// door can lead.
static void zm_read_room_doors(ZmRoomDoors* out)
{
    static const char hexDigits[] = "0123456789abcdef";
    char path[96];
    int variant = (g_main_state_flags & MSF_CHAR_VARIANT) ? 1 : 0;
    snprintf(path, sizeof(path), GAME_DATA_ROOT "stage%c\\room%c%c%c%c.rdt",
             hexDigits[(out->stage + 1) & 0xF], hexDigits[(out->stage + 1) & 0xF],
             hexDigits[out->room >> 4], hexDigits[out->room & 0xF], hexDigits[variant]);

    out->count = 0;
    out->spawnCount = 0;
    out->loadable = false;
    size_t size = LoadFile(path, s_rdtScratch, 1);
    if (size == (size_t)-1 || size < 0x100 || size > sizeof(s_rdtScratch)) {
        dbg_printf("[survivor] %s: no doors (not loadable)\n", path);
        return;
    }
    out->loadable = true;
    unsigned int p = *(unsigned int*)(s_rdtScratch + 0x60);
    while (p + 2 <= size) {
        unsigned short blockSize = *(unsigned short*)(s_rdtScratch + p);
        if (blockSize == 0) break;
        unsigned int q = p + 2;
        unsigned int end = p + blockSize;
        if (end > size) break;
        while (q < end) {
            unsigned char op = s_rdtScratch[q];
            if (op >= sizeof(kScdOperandWidth)) break;
            if (op == 0x0C && q + 26 <= end && out->count < ZM_MAX_DOORS) {
                const unsigned char* r = s_rdtScratch + q + 2;
                ZmDoor* d = &out->doors[out->count++];
                d->zoneX = *(const unsigned short*)(r + 0);
                d->zoneZ = *(const unsigned short*)(r + 2);
                d->zoneW = *(const unsigned short*)(r + 4);
                d->zoneD = *(const unsigned short*)(r + 6);
                d->type = r[8];
                d->sfx = r[9];
                d->flags0B = r[0x0B];
                d->lock = r[0x0C];
                d->dest = r[0x0D];
                d->arriveX = *(const short*)(r + 0x0E);
                d->arriveY = *(const short*)(r + 0x10);
                d->arriveZ = *(const short*)(r + 0x12);
                d->arriveAngle = *(const short*)(r + 0x14);
                d->needItem = r[0x16];
                zm_random_door_lock(out->stage, out->room, (unsigned char)(s_rdtScratch[q + 1] & 0x7F), d->dest,
                                    &d->lock, &d->needItem);
            }
            // enemy_set: the same fields cmd_enemy_set reads (CmdFunctions.cpp).
            if (op == 0x1B && q + 22 <= end && out->spawnCount < ZM_MAX_SPAWNS) {
                const unsigned char* r = s_rdtScratch + q;
                ZmSpawn* sp = &out->spawns[out->spawnCount++];
                sp->slot = r[0x12] & 0x0F;
                sp->id = r[0x01];
                sp->deathFlag = r[0x03];
                sp->uncond = r[0x04];
                sp->angle = *(const short*)(r + 0x08);
                sp->x = *(const short*)(r + 0x0C);
                sp->y = *(const short*)(r + 0x0E);
                sp->z = *(const short*)(r + 0x10);
            }
            q += 1u + kScdOperandWidth[op];
        }
        p = end;
    }
    dbg_printf("[survivor] stage %d room %02X: %d doors\n",
               (int)out->stage, (int)out->room, (int)out->count);
}

// cmd_enemy_set takes its slot from operand byte 0x12 (& 0xF).
static int zm_scan_enemy_slots(const unsigned char* script)
{
    int highest = -1;
    if (script == NULL) return -1;
    const unsigned char* p = script;
    for (int blocks = 0; blocks < 256; blocks++) {
        unsigned short blockSize = *(const unsigned short*)p;
        if (blockSize == 0) break;
        const unsigned char* q = p + 2;
        const unsigned char* end = p + blockSize;
        while (q < end) {
            unsigned char op = *q;
            if (op >= sizeof(kScdOperandWidth)) break;
            if (op == 0x1B) {
                // Yawn takes twelve slots after its own for its body.
                int slot = zm_yawn_last_slot(q[1], q[0x12] & 0x0F);
                if (slot > highest) highest = slot;
            }
            q += 1u + kScdOperandWidth[op];
        }
        p = end;
    }
    return highest;
}

int zm_room_highest_script_slot(void)
{
    int a = zm_scan_enemy_slots((const unsigned char*)g_RoomInitScd);
    int b = zm_scan_enemy_slots(g_RoomScdOpcodes);
    return a > b ? a : b;
}

static ZmRoomDoors* zm_room_find(unsigned char stage, unsigned char room)
{
    for (int i = 0; i < (int)(sizeof(s_doorCache) / sizeof(s_doorCache[0])); i++) {
        if (s_doorCache[i].valid && s_doorCache[i].stage == stage && s_doorCache[i].room == room) {
            return &s_doorCache[i];
        }
    }
    return NULL;
}

static ZmRoomDoors* zm_room_entry(unsigned char stage, unsigned char room)
{
    ZmRoomDoors* slot = zm_room_find(stage, room);
    if (slot != NULL) return slot;
    slot = &s_doorCache[s_doorCacheNext];
    s_doorCacheNext = (s_doorCacheNext + 1) % (int)(sizeof(s_doorCache) / sizeof(s_doorCache[0]));
    slot->stage = stage;
    slot->room = room;
    slot->valid = 1;
    zm_read_room_doors(slot);
    return slot;
}

int zm_room_doors(unsigned char stage, unsigned char room, const ZmDoor** out)
{
    ZmRoomDoors* slot = zm_room_entry(stage, room);
    *out = slot->doors;
    return slot->count;
}

int zm_room_spawns(unsigned char stage, unsigned char room, const ZmSpawn** out)
{
    ZmRoomDoors* slot = zm_room_entry(stage, room);
    *out = slot->spawns;
    return slot->loadable ? slot->spawnCount : -1;
}

bool zm_room_cached(unsigned char stage, unsigned char room)
{
    return zm_room_find(stage, room) != NULL;
}

// The test door_try_enter (0x0041b400) makes before it lets anyone through,
// minus the key use: a locked door stays shut to the survivor, which does not
// manage its inventory. Camera-only records (0x80 in +0x0B) and doors back
// into the same room are not ways out.
bool zm_door_usable(const ZmDoor* d, unsigned char stage, unsigned char room)
{
    if ((d->flags0B & 0x80) != 0) return false;
    if ((d->lock & 0x40) != 0 && (g_playerEntity.id & 3) == 3) return false;
    if ((d->lock & 0x80) != 0 && Flg_ck((int)g_LocksFlags, d->lock & 0x3F) == 0) return false;
    // The director's LOCK DOORS trap on either side (ZombieTraps.cpp).
    if (zm_trap_door_locked(stage, room, d->dest, d->flags0B, NULL)) return false;
    // The unpowered elevator, the keypad door before its pass number (ZombieKeypad.cpp).
    if (zm_access_door_closed(stage, room, d->dest)) return false;
    // The lesson room's hole before Yawn 2 is beaten, and the attic while
    // Yawn is in it (ZombieYawn.cpp).
    if (zm_yawn_hole_shut(stage, room, d->dest)) return false;
    if (zm_yawn_traps_exit(stage, room)) return false;
    if (zm_yawn_lesson_sealed(stage, room, d->dest)) return false;
    unsigned char ds, dr;
    zm_decode_dest(d->dest, stage, &ds, &dr);
    return !(ds == stage && dr == room);
}

// ===========================================================================
// Survivor state
// ===========================================================================
enum {
    ZS_TRAVEL = 0,   // walk to the chosen door
    ZS_FIGHT  = 1,   // aim at the zombie and shoot
    ZS_EVADE  = 2,   // it is too close: run
};

#define ZS_ROUTE_MAX        512     // walk-grid route points kept

struct Survivor {
    bool present;
    unsigned char stage, room;         // the room it is in
    unsigned char prevStage, prevRoom; // the room it came from (to avoid doubling back)
    int x, y, z;                       // PRESENT: where it is. AWAY: where it came in
    short angle;
    int timer;                         // AWAY: frames until it takes nextDoor
    int crossTotal;                    // AWAY: frames the whole crossing takes
    int nextDoor;                      // AWAY: the door it is walking to, -1 none

    // PRESENT: the AI
    int targetDoor;                    // index into this room's doors, -1 none
    int arrivalGrace;                  // frames during which doors are not taken
    int mode, modeTimer;
    int checkX, checkZ, checkTimer;    // stuck detection
    int unstickTimer, unstickTurn;
    int fireCooldown;
    unsigned int prevHeld;

    // PRESENT: the route it is following (ZombieNav.cpp), start first.
    int routeX[ZS_ROUTE_MAX], routeZ[ZS_ROUTE_MAX];
    int routeLen, routeIdx;
    int routeTX, routeTZ;              // where that route goes
    int repathTimer;
};

static Survivor s_survivor;

// The current room-event ride (zm_survivor_begin_ride .. zm_zombie_ride_end).
static int  s_rideFrames = 0, s_rideIdle = 0;
static bool s_rideTaken = false;
static int  s_rideLastX = 0, s_rideLastZ = 0;

// Where the riding character really is while it is hidden from the enemies
// for their update (zm_survivor_enemies_begin / _end).
static bool s_hiddenForEnemies = false;
static int  s_hideT[3];
static SVECTOR s_hidePos;
static unsigned short s_hidePosY;

// Where the survivor stood when the zombie pressed action at a door - the
// transition overwrites the player position with the zombie's arrival point
// before the outgoing room is torn down.
static bool s_captureValid = false;
static int  s_captureX, s_captureY, s_captureZ;
static short s_captureAngle;

// Off-screen walking pace, units per frame. A little under the player's walk
// (0x5d) - it stops to look around - and the same pace zs_replay_walk uses.
#define ZS_AWAY_SPEED       70
// Frames a door transition costs the zombie that the survivor keeps walking
// through (room_transition_load runs outside the frame loop, so the timer
// does not see them).
#define ZS_DOOR_FRAMES      ZM_DOOR_FRAMES
// zs_replay_walk stops this far from the door it is heading for, so the
// survivor is found short of it rather than leaving the instant it appears.
#define ZS_REPLAY_STOP      1200

#define ZS_ARRIVAL_GRACE    45
#define ZS_REPATH_FRAMES    45
#define ZS_ROUTE_GOAL       700     // a route ends this close to its target
#define ZS_ROUTE_REACHED    350     // a route point counts as passed this close
#define ZS_ROUTE_LOOKAHEAD  6       // steer at the furthest visible of the next N
#define ZS_EVADE_RANGE      1600
#define ZS_FIGHT_RANGE      7000
#define ZS_RUN_RANGE        9000
#define ZS_EVADE_FRAMES     40
#define ZS_FIRE_ARC         0x70
#define ZS_FIRE_COOLDOWN    20
#define ZS_STUCK_WINDOW     30
#define ZS_STUCK_DIST       150
#define ZS_UNSTICK_FRAMES   25
#define ZS_CLIP_ROUNDS      15

bool zm_survivor_present(void)
{
    return s_survivor.present;
}

// Where the AI survivor is (single player; zm_survivor_list covers both).
bool zm_survivor_location(unsigned char* stage, unsigned char* room)
{
    if (zm_game_role() != ZM_NET_OFF) return false;
    *stage = s_survivor.stage;
    *room = s_survivor.room;
    return true;
}

// The parking spot for this room, and the health the survivor had when it
// was parked (restored every parked frame: nothing that happens to the empty
// player entity here should hurt the survivor somewhere else).
static int s_parkX = 0, s_parkZ = 0;
static unsigned char s_parkStage = 0xFF, s_parkRoom = 0xFF;
static short s_parkHealth = 0;

// Of the four corners of the coordinate square, the one whose nearest enemy
// is furthest away.
static void zs_pick_park(void)
{
    static const int corners[4][2] = {
        { 0, 0 }, { ZM_PARK_MAX, 0 }, { 0, ZM_PARK_MAX }, { ZM_PARK_MAX, ZM_PARK_MAX },
    };
    long long best = -1;
    for (int c = 0; c < 4; c++) {
        long long nearest = 0x7FFFFFFFFFFFLL;
        for (int i = 0; i < 30; i++) {
            const Entity* e = &g_EnemiesList[i];
            if ((e->status_flags & ENTITY_STATUS_ACTIVE) == 0) continue;
            long long dx = (long long)e->scaMatrixData.localMatrix.t[0] - corners[c][0];
            long long dz = (long long)e->scaMatrixData.localMatrix.t[2] - corners[c][1];
            long long d = dx * dx + dz * dz;
            if (d < nearest) nearest = d;
        }
        if (nearest > best) {
            best = nearest;
            s_parkX = corners[c][0];
            s_parkZ = corners[c][1];
        }
    }
    s_parkStage = g_stageId;
    s_parkRoom = g_roomId;
}

static void zs_park(void)
{
    if (s_survivor.present || s_parkStage != g_stageId || s_parkRoom != g_roomId) {
        zs_pick_park();
        s_parkHealth = g_playerEntity.health;
    }
    s_survivor.present = false;
    g_playerEntity.scaMatrixData.localMatrix.t[0] = s_parkX;
    g_playerEntity.scaMatrixData.localMatrix.t[1] = 0;
    g_playerEntity.scaMatrixData.localMatrix.t[2] = s_parkZ;
    g_playerEntity.position.x = (short)s_parkX;
    g_playerEntity.position.y = 0;
    g_playerEntity.position.z = (short)s_parkZ;
    g_playerEntity.posY = 0;
    g_playerEntity.isBeingAttackedFlag = 0;
    if (g_playerEntity.health < s_parkHealth) {
        g_playerEntity.health = s_parkHealth;
    }
}

// A fresh pick among this room's usable doors, preferring not to go straight
// back where it came from. -1 if there is no way out.
static int zs_pick_door(unsigned char stage, unsigned char room, int avoid)
{
    const ZmDoor* doors;
    int n = zm_room_doors(stage, room, &doors);
    int choices[ZM_MAX_DOORS];
    int fallback[ZM_MAX_DOORS];
    int nc = 0, nf = 0;
    for (int i = 0; i < n; i++) {
        if (!zm_door_usable(&doors[i], stage, room)) continue;
        unsigned char ds, dr;
        zm_decode_dest(doors[i].dest, stage, &ds, &dr);
        bool back = (ds == s_survivor.prevStage && dr == s_survivor.prevRoom);
        if (i == avoid || back) {
            fallback[nf++] = i;
        } else {
            choices[nc++] = i;
        }
    }
    if (nc > 0) return choices[rand() % nc];
    if (nf > 0) return fallback[rand() % nf];
    return -1;
}

// Put the survivor into the loaded room as the real player at (x, y, z).
static void zs_materialize(int x, int y, int z, short angle, int targetDoor)
{
    g_playerEntity.scaMatrixData.localMatrix.t[0] = x;
    g_playerEntity.scaMatrixData.localMatrix.t[1] = y;
    g_playerEntity.scaMatrixData.localMatrix.t[2] = z;
    g_playerEntity.position.x = (short)x;
    g_playerEntity.position.y = (short)y;
    g_playerEntity.position.z = (short)z;
    g_playerEntity.posY = (unsigned short)y;
    g_playerEntity.directionAngle = angle;
    // As room_transition_load leaves the player: state 0 (player_state_init)
    // picks it up next frame.
    g_playerEntity.animationId = 0;
    g_playerEntity.animFrameId = 0;
    g_playerEntity.action_behavior = 0;
    g_playerEntity.action_state = 0;
    g_playerEntity.isBeingAttackedFlag = 0;

    Survivor& s = s_survivor;
    s.present = true;
    s.stage = g_stageId;
    s.room = g_roomId;
    s.targetDoor = (targetDoor >= 0) ? targetDoor : zs_pick_door(s.stage, s.room, -1);
    s.arrivalGrace = ZS_ARRIVAL_GRACE;
    s.mode = ZS_TRAVEL;
    s.modeTimer = 0;
    s.checkX = x;
    s.checkZ = z;
    s.checkTimer = ZS_STUCK_WINDOW;
    s.unstickTimer = 0;
    s.fireCooldown = 0;
    s.prevHeld = 0;
    dbg_printf("[survivor] enters stage %d room %02X at (%d, %d), heading for door %d\n",
               (int)s.stage, (int)s.room, x, z, s.targetDoor);
}

static void zs_door_center(const ZmDoor* d, int* x, int* z)
{
    *x = (int)d->zoneX + (int)d->zoneW / 2;
    *z = (int)d->zoneZ + (int)d->zoneD / 2;
}

// AWAY: choose the door it walks to next (or keep `keepDoor`) and time the
// walk from where it stands (s.x, s.z): the distance at a walking pace, plus a
// little dawdling, so a big room takes longer to cross than a corridor.
static void zs_plan_crossing(int keepDoor)
{
    Survivor& s = s_survivor;
    s.nextDoor = (keepDoor >= 0) ? keepDoor : zs_pick_door(s.stage, s.room, -1);
    if (s.nextDoor < 0) {
        s.crossTotal = s.timer = 300;   // shut in; look again later (a lock may open)
        return;
    }
    const ZmDoor* doors;
    zm_room_doors(s.stage, s.room, &doors);
    int dx, dz;
    zs_door_center(&doors[s.nextDoor], &dx, &dz);
    int dist = SquareRoot0((dx - s.x) * (dx - s.x) + (dz - s.z) * (dz - s.z));
    int frames = dist / ZS_AWAY_SPEED + 30 + rand() % 120;
    if (frames < 90) frames = 90;
    if (frames > 900) frames = 900;
    s.crossTotal = s.timer = frames;
}

static void zs_replay_walk(int elapsed, int doorX, int doorZ);   // below, with the AI
static bool zs_place_on_route(int elapsed, int doorX, int doorZ);

// Move the record through a door of its current room, into the next one, and
// plan its walk across it.
static void zs_go_through(const ZmDoor* d)
{
    Survivor& s = s_survivor;
    unsigned char ds, dr;
    zm_decode_dest(d->dest, s.stage, &ds, &dr);
    s.prevStage = s.stage;
    s.prevRoom = s.room;
    s.stage = ds;
    s.room = dr;
    s.x = (unsigned short)d->arriveX;   // X/Z zero-extended, as room_transition_load
    s.y = d->arriveY;
    s.z = (unsigned short)d->arriveZ;
    s.angle = d->arriveAngle;
    zs_plan_crossing(-1);
}

static void zs_give_kit(void);

void zm_survivor_new_game(void)
{
    Survivor& s = s_survivor;
    memset(&s, 0, sizeof(s));
    s_captureValid = false;
    // The character decides which RDT variant the doors are read from.
    memset(s_doorCache, 0, sizeof(s_doorCache));
    s_doorCacheNext = 0;

    // Start a room ahead of the zombie: through the main hall's door to the
    // dining room (room 5), as the opening does, so the hunt starts with the
    // survivor out of sight. Without that door, start in the main hall.
    s.stage = STAGE_MANSION_RETURN_1F;
    s.room = ROOM_MAIN_HALL;
    s.prevStage = 0xFF;
    s.prevRoom = 0xFF;
    s.x = 17000;
    s.z = 9000;
    s.angle = 0;
    s.nextDoor = -1;

    const ZmDoor* doors;
    int n = zm_room_doors(STAGE_MANSION_RETURN_1F, ROOM_MAIN_HALL, &doors);
    for (int i = 0; i < n; i++) {
        unsigned char ds, dr;
        zm_decode_dest(doors[i].dest, STAGE_MANSION_RETURN_1F, &ds, &dr);
        if (ds == STAGE_MANSION_RETURN_1F && dr == ROOM_DINING_ROOM) {
            zs_go_through(&doors[i]);
            break;
        }
    }
    if (s.room == ROOM_MAIN_HALL) {
        zs_plan_crossing(-1);
    }

    zs_give_kit();
}

// A gun to fight back with. Chris starts with only the knife; Jill keeps
// her Beretta. Equipped in slot 1, with two clips (one stack) behind it. (Called after
// SetInitialItems and before SetupCharacterData, which loads the equipped
// weapon's model; LoadHeldItemsImages recounts the slots.)
static void zs_give_kit(void)
{
    unsigned char kit[6][2] = {
        { ITEM_BERETTA, 15 },
        { ITEM_CLIP, ZS_CLIP_ROUNDS * 2 },  // two clips, one slot
        { ITEM_KNIFE, 0 },
        { ITEM_FIRST_AID_SPRAY, 1 },
    };
    int kitCount = 4;
    // A multiplayer survivor starts with its character's own kit (ZombiePerks.cpp).
    int perkCount = zm_perk_kit(kit, 6);
    if (perkCount >= 0) kitCount = perkCount;
    int slots = zombie_mode_inventory_slots(((g_playerEntity.id & 3) == CHAR_JILL) ? 8 : 6);
    for (int i = 0; i < slots; i++) {
        bool used = i < kitCount;
        g_ItemsSlots[i].Id = used ? kit[i][0] : 0;
        g_ItemsSlots[i].qty = used ? kit[i][1] : 0;
    }
    g_EquippedItemId = 1;
}

// A multiplayer survivor's start: the main hall, the three survivors on the
// corners of a triangle around its middle (by player number), facing the back
// door the director's zombie comes in at (zombie_mode_room_spawn).
void zm_survivor_start_as_player(void)
{
    memset(&s_survivor, 0, sizeof(s_survivor));
    memset(s_doorCache, 0, sizeof(s_doorCache));
    s_doorCacheNext = 0;
    static const int kCorner[4][2] = {
        {  0,    -1400 },      // nearest the front door
        { -1212,   700 },      // back left
        {  1212,   700 },      // back right
        {  0,     2100 },      // back middle: the host's, against the AI director
    };
    int seat = zm_net_self() - 1;
    if (seat < 0) seat = 3;
    if (seat > 3) seat = 0;
    int x = 17000 + kCorner[seat][0];
    int z = 8500 + kCorner[seat][1];
    int backX, backZ;
    zm_hall_back_door(&backX, &backZ);
    g_stageId = STAGE_MANSION_RETURN_1F;
    g_roomId = ROOM_MAIN_HALL;
    g_playerEntity.position.x = (short)x;
    g_playerEntity.position.z = (short)z;
    g_playerEntity.directionAngle = (short)CalculateAngleBetweenPointsXZ(x, z, backX, backZ);
    zs_give_kit();
}

// ---------------------------------------------------------------------------
// Multiplayer, this copy is the director: the survivors are the other
// players', shown as stand-ins (ZombieMode.cpp), and the player entity is only
// the monsters' target - parked, and swapped to the nearest survivor around
// each monster's update (zm_target_begin). "Present" means a survivor is in
// this room.
// ---------------------------------------------------------------------------
static bool zs_net_any_here(void)
{
    for (int i = 1; i < ZM_NET_MAX_PLAYERS; i++) {
        if (zm_net_char(i) < 0) continue;
        const ZmNetPeerState* p = zm_net_player(i);
        if (p != NULL && p->valid && p->stage == g_stageId && p->room == g_roomId) return true;
    }
    return false;
}

static bool zs_ride_frame(void);

static void zs_net_frame(void)
{
    Survivor& s = s_survivor;
    // The zombie riding a room event (stairs walked by the room's script):
    // the hidden player does the walk, as in single player. Parking it every
    // frame instead left the ride - and the zombie - waiting forever.
    if (zs_ride_frame()) {
        s.present = false;
        return;
    }
    s.present = false;
    zs_park();
    s.present = zs_net_any_here();
    g_main_state_flags2 &= ~MSF2_EFFECT_ZONE;
}

void zm_survivor_room_loaded(void)
{
    Survivor& s = s_survivor;
    zm_nav_build();
    if (zm_game_role() == ZM_NET_ZOMBIE) {
        zs_net_frame();
        return;
    }
    s.routeLen = 0;
    s.repathTimer = 0;
    if (s.present && s.stage == g_stageId && s.room == g_roomId && s_captureValid) {
        // A camera-only door: same room, the zombie moved, the survivor did not.
        zs_materialize(s_captureX, s_captureY, s_captureZ, s_captureAngle, s.targetDoor);
        s_captureValid = false;
        return;
    }
    s_captureValid = false;
    if (s.stage != g_stageId || s.room != g_roomId) {
        zs_park();
        return;
    }

    // The zombie walked in on it. Start it where it came in and replay the
    // walk it has had time for, through this room's own zones and collision,
    // so it is found wherever that walk actually got it to.
    zs_materialize(s.x, s.y, s.z, s.angle, s.nextDoor);
    if (s.nextDoor >= 0) {
        const ZmDoor* doors;
        zm_room_doors(s.stage, s.room, &doors);
        int dx, dz;
        zs_door_center(&doors[s.nextDoor], &dx, &dz);
        int elapsed = s.crossTotal - s.timer + ZS_DOOR_FRAMES;
        if (elapsed < 0) elapsed = 0;
        if (!zs_place_on_route(elapsed, dx, dz)) {
            zs_replay_walk(elapsed, dx, dz);
        }
        // Facing the way it was walking.
        Entity* saved = ENTITY;
        ENTITY = (Entity*)&g_playerEntity;
        g_playerEntity.directionAngle = (short)getAngleTowardsTarget(dx, dz);
        ENTITY = saved;
    }
}

void zm_survivor_note_zombie_door(void)
{
    if (!s_survivor.present) {
        s_captureValid = false;
        return;
    }
    s_captureValid = true;
    s_captureX = g_playerEntity.scaMatrixData.localMatrix.t[0];
    s_captureY = g_playerEntity.scaMatrixData.localMatrix.t[1];
    s_captureZ = g_playerEntity.scaMatrixData.localMatrix.t[2];
    s_captureAngle = g_playerEntity.directionAngle;
}

void zm_survivor_zombie_leaving(void)
{
    Survivor& s = s_survivor;
    if (!s.present) {
        return;
    }
    // It stays behind in this room, where it stood, and carries on from there.
    s.present = false;
    s.stage = g_stageId;
    s.room = g_roomId;
    if (s_captureValid) {
        s.x = s_captureX;
        s.y = s_captureY;
        s.z = s_captureZ;
        s.angle = s_captureAngle;
    }
    s_captureValid = false;
    // On to the door it was walking to, from here.
    zs_plan_crossing(s.targetDoor);
}

bool zm_survivor_take_door(const unsigned char* record)
{
    if (!s_survivor.present) {
        return false;
    }
    // The survivor walked into a door the engine would have opened (the AI
    // never presses action, so this is a walk-in door). It leaves instead of
    // the whole room changing.
    ZmDoor d;
    d.flags0B = record[0x0B];
    d.lock = record[0x0C];
    d.dest = record[0x0D];
    d.arriveX = *(const short*)(record + 0x0E);
    d.arriveY = *(const short*)(record + 0x10);
    d.arriveZ = *(const short*)(record + 0x12);
    d.arriveAngle = *(const short*)(record + 0x14);
    play_sfx(0, 1, 0);
    if ((d.flags0B & 0x80) != 0) {
        // Camera-only: a door within this room. The engine would run the full
        // transition and move the camera after the survivor; just move the
        // survivor to the far side, as room_transition_load would place it.
        zs_materialize((unsigned short)d.arriveX, d.arriveY, (unsigned short)d.arriveZ,
                       d.arriveAngle, -1);
        dbg_printf("[survivor] crossed a camera-only door within the room\n");
        return true;
    }
    s_survivor.stage = g_stageId;
    s_survivor.room = g_roomId;
    zs_go_through(&d);
    zs_park();
    dbg_printf("[survivor] left through a walk-in door to stage %d room %02X\n",
               (int)s_survivor.stage, (int)s_survivor.room);
    return true;
}

char zm_survivor_mash(void)
{
    // reduce_attack_time_by_btn_press gives 3 for directions + 2 for buttons;
    // a struggling-but-not-frantic survivor shortens the bite by a third.
    return (char)((rand() % 3 == 0) ? 2 : 0);
}

// ===========================================================================
// AWAY: the record walks the door graph
// ===========================================================================
static void zs_offscreen_tick(void)
{
    Survivor& s = s_survivor;
    if (--s.timer > 0) {
        return;
    }
    const ZmDoor* doors;
    int n = zm_room_doors(s.stage, s.room, &doors);
    int pick = s.nextDoor;
    if (pick < 0 || pick >= n || !zm_door_usable(&doors[pick], s.stage, s.room)) {
        pick = zs_pick_door(s.stage, s.room, -1);
    }
    if (pick < 0) {
        s.crossTotal = s.timer = 300;      // shut in; try again later (a lock may open)
        return;
    }
    zs_go_through(&doors[pick]);
    dbg_printf("[survivor] (away) moved to stage %d room %02X\n", (int)s.stage, (int)s.room);

    // Into the zombie's room: it comes through the door.
    if (g_zombieModeEntity != NULL && s.stage == g_stageId && s.room == g_roomId) {
        play_sfx(0, 1, 0);
        zs_materialize(s.x, s.y, s.z, s.angle, s.nextDoor);
        zm_stun_room("the survivor");
    }
}

// ===========================================================================
// PRESENT: the AI that drives the player
// ===========================================================================

// Angle from the survivor to (x, z) relative to its facing, -0x800..0x7FF.
static int zs_relative_angle(int x, int z)
{
    Entity* saved = ENTITY;
    ENTITY = (Entity*)&g_playerEntity;
    unsigned short to = getAngleTowardsTarget(x, z);
    ENTITY = saved;
    int rel = ((int)to - (int)(unsigned short)g_playerEntity.directionAngle) & 0xFFF;
    return rel >= 0x800 ? rel - 0x1000 : rel;
}

// Turn bits toward a relative angle: +angle is ZM_PAD_TURN_A.
static unsigned int zs_turn_toward(int rel, int deadZone)
{
    if (rel > deadZone) return ZM_PAD_TURN_A;
    if (rel < -deadZone) return ZM_PAD_TURN_B;
    return 0;
}

// The next waypoint toward (tx, tz) through the room's walk zones
// (zone_path_find, the zombies' own route planner), or the target itself
// when the room has no zone grid or no route.
static void zs_waypoint(int tx, int tz, int* wx, int* wz)
{
    *wx = tx;
    *wz = tz;
    if (g_RdtPointer == NULL || g_RdtPointer->walk_zones == NULL ||
        *(unsigned char*)g_RdtPointer->walk_zones == 0) {
        return;
    }
    Entity* saved = ENTITY;
    ENTITY = (Entity*)&g_playerEntity;
    int ox = 0, oz = 0;
    unsigned char r = zone_path_find(tx, tz, &ox, &oz);
    ENTITY = saved;
    if (r != 0xFF) {
        *wx = (unsigned short)ox;   // zone_path_find stores 16-bit coordinates
        *wz = (unsigned short)oz;
    }
}

// Place the survivor (already standing where it came in) the distance it has
// had time to walk, measured along the room's walk-grid route to its door
// rather than a straight line, stopping a few cells short of the door. Every
// route point is a cell the grid already found clear for a body. False if the
// grid has no route (zs_replay_walk takes over).
static bool zs_place_on_route(int elapsed, int doorX, int doorZ)
{
    Survivor& s = s_survivor;
    int* t = (int*)g_playerEntity.scaMatrixData.localMatrix.t;
    int n = zm_nav_path(t[0], t[2], doorX, doorZ, ZS_ROUTE_GOAL,
                        s.routeX, s.routeZ, ZS_ROUTE_MAX);
    if (n < 2) return false;

    int budget = elapsed * ZS_AWAY_SPEED;
    int last = n - 1 - 3;                  // keep clear of the door itself
    if (last < 0) last = 0;
    int at = 0;
    for (int i = 1; i <= last; i++) {
        int dx = s.routeX[i] - s.routeX[i - 1];
        int dz = s.routeZ[i] - s.routeZ[i - 1];
        int seg = SquareRoot0(dx * dx + dz * dz);
        if (budget < seg) break;
        budget -= seg;
        at = i;
    }
    t[0] = s.routeX[at];
    t[2] = s.routeZ[at];
    g_playerEntity.position.x = (short)t[0];
    g_playerEntity.position.z = (short)t[2];
    s.checkX = t[0];
    s.checkZ = t[2];
    // Keep the rest of the route to walk on.
    s.routeLen = n;
    s.routeIdx = at;
    s.routeTX = doorX;
    s.routeTZ = doorZ;
    s.repathTimer = ZS_REPATH_FRAMES;
    dbg_printf("[survivor] placed %d/%d along a %d-point route\n", at, last, n);
    return true;
}

// The point to steer at on the way to (tx, tz): follow the walk-grid route,
// re-planned every ZS_REPATH_FRAMES or when the target moves, aiming at the
// furthest of the next few route points it can see straight to (so it cuts
// corners rather than stepping cell by cell). Falls back to zs_waypoint when
// the grid has no route.
static void zs_route_point(int tx, int tz, int* wx, int* wz)
{
    Survivor& s = s_survivor;
    int px = g_playerEntity.scaMatrixData.localMatrix.t[0];
    int pz = g_playerEntity.scaMatrixData.localMatrix.t[2];

    int moved = (s.routeTX - tx) * (s.routeTX - tx) + (s.routeTZ - tz) * (s.routeTZ - tz);
    if (--s.repathTimer <= 0 || moved > 600 * 600 || s.routeLen == 0) {
        s.routeLen = zm_nav_path(px, pz, tx, tz, ZS_ROUTE_GOAL, s.routeX, s.routeZ, ZS_ROUTE_MAX);
        s.routeIdx = 0;
        s.routeTX = tx;
        s.routeTZ = tz;
        s.repathTimer = ZS_REPATH_FRAMES;
    }
    if (s.routeLen < 2) {
        zs_waypoint(tx, tz, wx, wz);
        return;
    }

    while (s.routeIdx < s.routeLen - 1) {
        int dx = s.routeX[s.routeIdx] - px, dz = s.routeZ[s.routeIdx] - pz;
        if (dx * dx + dz * dz > ZS_ROUTE_REACHED * ZS_ROUTE_REACHED) break;
        s.routeIdx++;
    }
    int j = s.routeIdx + ZS_ROUTE_LOOKAHEAD;
    if (j > s.routeLen - 1) j = s.routeLen - 1;
    for (; j > s.routeIdx; j--) {
        if (zm_nav_segment_clear(px, pz, s.routeX[j], s.routeZ[j])) break;
    }
    *wx = s.routeX[j];
    *wz = s.routeZ[j];
    // The route ends short of the target; past its end, head straight in.
    if (j == s.routeLen - 1 && s.routeIdx >= s.routeLen - 2) {
        *wx = tx;
        *wz = tz;
    }
}

// Fast-forward the survivor (the player entity, already placed where it came
// in) `elapsed` frames along its walk to (doorX, doorZ): each step toward the
// next zone_path_find waypoint at ZS_AWAY_SPEED, then check_room_collision
// pushes it out of whatever it walked into - the same sliding the real walk
// gets. Stops short of the door, or where it has wedged itself.
static void zs_replay_walk(int elapsed, int doorX, int doorZ)
{
    Entity* saved = ENTITY;
    ENTITY = (Entity*)&g_playerEntity;
    int* t = (int*)g_playerEntity.scaMatrixData.localMatrix.t;
    short radius = *(short*)(g_playerEntity.Sca_info + 10);
    int stuckFrames = 0;

    for (int i = 0; i < elapsed; i++) {
        int toDoorX = doorX - t[0];
        int toDoorZ = doorZ - t[2];
        if (SquareRoot0(toDoorX * toDoorX + toDoorZ * toDoorZ) < ZS_REPLAY_STOP) {
            break;
        }
        int wx, wz;
        zs_waypoint(doorX, doorZ, &wx, &wz);
        int dx = wx - t[0];
        int dz = wz - t[2];
        int len = SquareRoot0(dx * dx + dz * dz);
        if (len == 0) break;
        int stepLen = len < ZS_AWAY_SPEED ? len : ZS_AWAY_SPEED;
        int ox = t[0], oz = t[2];
        t[0] += dx * stepLen / len;
        t[2] += dz * stepLen / len;
        check_room_collision((VECTOR*)t, radius);
        int moved = SquareRoot0((t[0] - ox) * (t[0] - ox) + (t[2] - oz) * (t[2] - oz));
        stuckFrames = (moved < 8) ? stuckFrames + 1 : 0;
        if (stuckFrames > 15) break;
    }

    g_playerEntity.position.x = (short)t[0];
    g_playerEntity.position.z = (short)t[2];
    Survivor& s = s_survivor;
    s.checkX = t[0];
    s.checkZ = t[2];
    ENTITY = saved;
}

static bool zs_in_zone(int x, int z, const ZmDoor* d)
{
    return (unsigned int)(x - (int)d->zoneX) <= (unsigned int)d->zoneW &&
           (unsigned int)(z - (int)d->zoneZ) <= (unsigned int)d->zoneD;
}

// Is the survivor at a usable door (its reach probe or its body in the zone)?
// Returns the door's index or -1. `only` restricts the test to one door.
static int zs_at_door(int only)
{
    const ZmDoor* doors;
    int n = zm_room_doors(g_stageId, g_roomId, &doors);
    SVECTOR reach = { 600, 0, 0, 0 };
    MovePlayerXZ(g_playerEntity.directionAngle, &reach, &reach);
    int x = g_playerEntity.scaMatrixData.localMatrix.t[0];
    int z = g_playerEntity.scaMatrixData.localMatrix.t[2];
    for (int i = 0; i < n; i++) {
        if (only >= 0 && i != only) continue;
        if (!zm_door_usable(&doors[i], g_stageId, g_roomId)) continue;
        if (zs_in_zone(x + reach.x, z + reach.z, &doors[i]) || zs_in_zone(x, z, &doors[i])) {
            return i;
        }
    }
    return -1;
}

// RE1 reloads through the inventory's combine screen; the survivor does not
// open menus, so an empty gun takes a clip straight from the bag.
static void zs_reload(void)
{
    if (g_EquippedItemId == 0) return;
    ItemSlot* gun = &g_ItemsSlots[g_EquippedItemId - 1];
    if (gun->Id != ITEM_BERETTA || gun->qty != 0) return;
    for (int i = 0; i < 8; i++) {
        if (g_ItemsSlots[i].Id == ITEM_CLIP && g_ItemsSlots[i].qty != 0) {
            unsigned char take = g_ItemsSlots[i].qty < ZS_CLIP_ROUNDS ? g_ItemsSlots[i].qty
                                                                      : (unsigned char)ZS_CLIP_ROUNDS;
            gun->qty = take;
            g_ItemsSlots[i].qty = (unsigned char)(g_ItemsSlots[i].qty - take);
            return;
        }
    }
}

// True while the engine, not the AI, has the player: player states other than
// 0 (init) / 1 (control) - 4 blocked, 5-7 grabbed / hit, 8 SCD-driven - or
// dying. The pad is left empty then.
static bool zs_engine_has_player(void)
{
    if (g_playerEntity.health < 0) return true;
    if (g_playerEntity.animationId >= 4 && g_playerEntity.animationId <= 8) return true;
    return (g_playerEntity.isBeingAttackedFlag & 0x7F) != 0;
}

static void zs_leave_through(int door)
{
    const ZmDoor* doors;
    zm_room_doors(g_stageId, g_roomId, &doors);
    play_sfx(0, 1, 0);
    zs_go_through(&doors[door]);
    zs_park();
    dbg_printf("[survivor] left for stage %d room %02X\n",
               (int)s_survivor.stage, (int)s_survivor.room);
}

// One frame of decisions. Returns the held pad bits; may leave the room.
static unsigned int zs_think(void)
{
    Survivor& s = s_survivor;
    if (zs_engine_has_player()) {
        return 0;
    }
    zs_reload();

    int px = g_playerEntity.scaMatrixData.localMatrix.t[0];
    int pz = g_playerEntity.scaMatrixData.localMatrix.t[2];

    // Where is the zombie?
    bool zombieThere = zm_zombie_alive();
    int zx = 0, zz = 0, zdist = 0x7FFFFFFF, zrel = 0;
    if (zombieThere) {
        zx = g_zombieModeEntity->scaMatrixData.localMatrix.t[0];
        zz = g_zombieModeEntity->scaMatrixData.localMatrix.t[2];
        zdist = SquareRoot0((zx - px) * (zx - px) + (zz - pz) * (zz - pz));
        zrel = zs_relative_angle(zx, zz);
    }
    bool hurt = g_playerEntity.health * 3 < (int)g_playerEntity.maxHealth;
    bool hasAmmo = weapon_autoaim_check() != 0;

    // Pick a mode. Evading holds for a while once started.
    if (s.modeTimer > 0) {
        s.modeTimer--;
    } else if (zombieThere && zdist < ZS_EVADE_RANGE) {
        s.mode = ZS_EVADE;
        s.modeTimer = ZS_EVADE_FRAMES;
    } else if (zombieThere && zdist < ZS_FIGHT_RANGE && hasAmmo && !hurt) {
        s.mode = ZS_FIGHT;
    } else {
        s.mode = ZS_TRAVEL;
    }
    if (s.arrivalGrace > 0) s.arrivalGrace--;
    if (s.fireCooldown > 0) s.fireCooldown--;

    unsigned int held = 0;

    if (s.mode == ZS_FIGHT) {
        // Hold aim, line up, and squeeze off a shot whenever it is lined up
        // (a fresh press each time: the Beretta fires on the press).
        held |= ZM_PAD_AIM;
        held |= zs_turn_toward(zrel, ZS_FIRE_ARC / 2);
        if (zrel >= -ZS_FIRE_ARC && zrel <= ZS_FIRE_ARC && s.fireCooldown == 0 &&
            (s.prevHeld & ZM_PAD_AIM) != 0) {
            held |= ZM_PAD_FIRE;
            s.fireCooldown = ZS_FIRE_COOLDOWN;
        }
        return held;
    }

    // Moving: away from the zombie, or to the chosen door.
    int tx, tz;
    bool run = false;
    if (s.mode == ZS_EVADE) {
        // For the usable door furthest from the zombie; with none, a point
        // well behind it, away from the zombie.
        tx = px + (px - zx) * 4;
        tz = pz + (pz - zz) * 4;
        const ZmDoor* doors;
        int n = zm_room_doors(g_stageId, g_roomId, &doors);
        long long bestD = -1;
        for (int i = 0; i < n; i++) {
            if (!zm_door_usable(&doors[i], g_stageId, g_roomId)) continue;
            int dx, dz;
            zs_door_center(&doors[i], &dx, &dz);
            long long d = (long long)(dx - zx) * (dx - zx) + (long long)(dz - zz) * (dz - zz);
            if (d > bestD) { bestD = d; tx = dx; tz = dz; }
        }
        run = true;
    } else {
        if (s.targetDoor < 0) {
            s.targetDoor = zs_pick_door(g_stageId, g_roomId, -1);
        }
        if (s.targetDoor < 0) {
            return 0;      // nowhere to go: wait for the zombie
        }
        const ZmDoor* doors;
        zm_room_doors(g_stageId, g_roomId, &doors);
        const ZmDoor* d = &doors[s.targetDoor];
        tx = d->zoneX + d->zoneW / 2;
        tz = d->zoneZ + d->zoneD / 2;
        run = hurt || !hasAmmo || (zombieThere && zdist < ZS_RUN_RANGE);
    }

    // Through a door? Any usable one will do when fleeing; otherwise the one
    // it was walking to.
    if (s.arrivalGrace == 0) {
        int door = zs_at_door(s.mode == ZS_EVADE ? -1 : s.targetDoor);
        if (door >= 0) {
            zs_leave_through(door);
            return 0;
        }
    }

    // Stuck against something: back off and turn for a moment.
    if (s.unstickTimer > 0) {
        s.unstickTimer--;
        return ZM_PAD_BACK | s.unstickTurn;
    }
    if (--s.checkTimer <= 0) {
        int moved = SquareRoot0((px - s.checkX) * (px - s.checkX) + (pz - s.checkZ) * (pz - s.checkZ));
        if ((s.prevHeld & ZM_PAD_FORWARD) != 0 && moved < ZS_STUCK_DIST) {
            s.unstickTimer = ZS_UNSTICK_FRAMES;
            s.unstickTurn = (rand() & 1) ? ZM_PAD_TURN_A : ZM_PAD_TURN_B;
            if (s.mode == ZS_TRAVEL && (rand() % 3) == 0) {
                s.targetDoor = zs_pick_door(g_stageId, g_roomId, s.targetDoor);
            }
        }
        s.checkX = px;
        s.checkZ = pz;
        s.checkTimer = ZS_STUCK_WINDOW;
    }

    int wx, wz;
    zs_route_point(tx, tz, &wx, &wz);
    int rel = zs_relative_angle(wx, wz);
    held |= zs_turn_toward(rel, 0x60);
    if (rel > -0x300 && rel < 0x300) {
        held |= ZM_PAD_FORWARD;
        if (run) held |= ZM_PAD_RUN;
    }
    return held;
}

// ===========================================================================
// game_loop's player turn
// ===========================================================================
// One survivor's line: where it is, and whether that is next door.
static void zs_draw_survivor_line(int y, const ZmSurvivorInfo& v)
{
    // Is its room one door away from the zombie's? (No brackets in the text:
    // PrintText8x14 draws '(' and ')' as controller button icons.)
    const ZmDoor* doors;
    int n = zm_room_doors(g_stageId, g_roomId, &doors);
    bool adjacent = false;
    for (int i = 0; i < n && !adjacent; i++) {
        unsigned char ds, dr;
        zm_decode_dest(doors[i].dest, g_stageId, &ds, &dr);
        adjacent = (ds == v.stage && dr == v.room);
    }
    bool here = v.stage == g_stageId && v.room == g_roomId;

    const char* name = DebugRoom_Name(v.stage, v.room);
    char fallback[24];
    if (name == NULL) {
        // The room number as the files name it (stage digit, two-digit room).
        snprintf(fallback, sizeof(fallback), "ROOM %X%02X",
                 (unsigned int)(v.stage + 1) & 0xF, (unsigned int)v.room);
        name = fallback;
    }
    snprintf(PRINT_TEXT_BUFFER, sizeof(PRINT_TEXT_BUFFER), "%s: %s%s",
             zm_char_name(v.character), v.dead ? "DEAD" : name,
             v.dead ? "" : here ? " - HERE" : adjacent ? " - NEXT ROOM" : "");
    zm_text_encode(PRINT_TEXT_BUFFER);
    PrintText8x14(8, (short)y, 1, 0);
}

static void zs_draw_hud(void)
{
    if (g_zombieModeEntity == NULL) return;
    ZmSurvivorInfo list[ZM_NET_MAX_PLAYERS];
    int n = zm_survivor_list(list, ZM_NET_MAX_PLAYERS);
    int y = 8;
    for (int i = 0; i < n; i++) {
        // Single player keeps the old look: no line while it is in the room.
        if (zm_game_role() == ZM_NET_OFF && s_survivor.present) continue;
        zs_draw_survivor_line(y, list[i]);
        y += 14;
    }
    snprintf(PRINT_TEXT_BUFFER, sizeof(PRINT_TEXT_BUFFER), "YOU: ");
    const char* name = DebugRoom_Name(g_stageId, g_roomId);
    if (name != NULL) {
        snprintf(PRINT_TEXT_BUFFER, sizeof(PRINT_TEXT_BUFFER), "YOU: %s", name);
    } else {
        snprintf(PRINT_TEXT_BUFFER, sizeof(PRINT_TEXT_BUFFER), "YOU: ROOM %X%02X",
                 (unsigned int)(g_stageId + 1) & 0xF, (unsigned int)g_roomId);
    }
    zm_text_encode(PRINT_TEXT_BUFFER);
    PrintText8x14(8, (short)(y + 2), 1, 0);
}

// The zombie is riding a room event (zm_try_event): one frame of it. True
// while there is a ride (single player and the director's copy alike - the
// director's player entity is the same hidden stand-in).
static bool zs_ride_frame(void)
{
    // The zombie is riding a room event: the hidden player does the walk,
    // with an empty pad, and the zombie follows it. Over when the event has
    // let go of the player (no longer in state 4-8) for a few frames, or
    // after a generous timeout.
    if (!zm_zombie_riding()) return false;
    s_rideFrames++;
    int ox = g_playerEntity.scaMatrixData.localMatrix.t[0];
    int oz = g_playerEntity.scaMatrixData.localMatrix.t[2];
    short realHeld = g_PlayerDpadHeld;
    short realPressed = g_PlayerDpadPressed;
    g_PlayerDpadHeld = 0;
    g_PlayerDpadPressed = 0;
    update_player_anim();
    g_PlayerDpadHeld = realHeld;
    g_PlayerDpadPressed = realPressed;
    int* t = (int*)g_playerEntity.scaMatrixData.localMatrix.t;
    // The event walks the character with its animation's root motion and
    // only moves the entity's own position when it ends (as the step in
    // player_door_open_sequence does), so follow the root joint: its
    // animated translation, turned by the facing, with the height taken
    // relative to the character's standing hip height (joint 0's rest
    // offset in the EMD, animHeader + 8).
    const JointStruct* root = g_playerEntity.jointsStructs;
    SVECTOR rot = { 0, g_playerEntity.directionAngle, 0, 0 };
    MATRIX m;
    RotMatrix(&rot, &m);
    SVECTOR local = { (short)root->transform.t[0], 0, (short)root->transform.t[2], 0 };
    VECTOR world;
    ApplyMatrix(&m, &local, &world);
    int restY = *(const short*)(g_playerEntity.animHeader + 8 + 2);
    int fx = t[0] + world.x;
    int fy = t[1] + (root->transform.t[1] - restY);
    int fz = t[2] + world.z;
    bool moving = (t[0] - ox) * (t[0] - ox) + (t[2] - oz) * (t[2] - oz) > 4 ||
                  (fx - s_rideLastX) * (fx - s_rideLastX) + (fz - s_rideLastZ) * (fz - s_rideLastZ) > 4;
    s_rideLastX = fx;
    s_rideLastZ = fz;
    zm_zombie_ride_follow(fx, fy, fz, g_playerEntity.directionAngle, moving);

    bool scripted = g_playerEntity.animationId >= 4 && g_playerEntity.animationId <= 8;
    if (scripted) s_rideTaken = true;
    s_rideIdle = scripted ? 0 : s_rideIdle + 1;
    // Over once the event has had the player and let it go again; if it
    // never takes the player at all (an event that does something else),
    // give up after three seconds.
    if ((s_rideTaken && s_rideIdle > 5) || (!s_rideTaken && s_rideFrames > 90) ||
        s_rideFrames > 900) {
        zm_zombie_ride_end();
        zs_park();
    }
    g_main_state_flags2 &= ~MSF2_EFFECT_ZONE;
    return true;
}

void zm_survivor_frame(void)
{
    Survivor& s = s_survivor;

    if (zm_game_role() == ZM_NET_ZOMBIE) {
        zs_net_frame();
        return;
    }

    if (s.present) {
        // The survivor is the player: synthesize its pad for this one update,
        // then put the real pad (the zombie's) back.
        short realHeld = g_PlayerDpadHeld;
        short realPressed = g_PlayerDpadPressed;
        unsigned int held = zm_bumpers_hold_input() ? 0 : zs_think();
        if (s.present) {
            g_PlayerDpadHeld = (short)held;
            g_PlayerDpadPressed = (short)(held & ~s.prevHeld);
            s.prevHeld = held;
            update_player_anim();
            g_main_state_flags2 &= ~MSF2_EFFECT_ZONE;
            update_player_position(&g_playerEntity, 1);
            if (s.present) {
                s.x = g_playerEntity.scaMatrixData.localMatrix.t[0];
                s.y = g_playerEntity.scaMatrixData.localMatrix.t[1];
                s.z = g_playerEntity.scaMatrixData.localMatrix.t[2];
                s.angle = g_playerEntity.directionAngle;
            }
        }
        g_PlayerDpadHeld = realHeld;
        g_PlayerDpadPressed = realPressed;
        return;
    }

    if (zs_ride_frame()) {
        zs_offscreen_tick();
        return;
    }

    // Parked. A room script can still take the player over (an event's
    // animation opcodes put it in state 8 or 4); let the player's own update
    // finish what the script waits on, invisibly, then park it again.
    if (g_playerEntity.animationId == 8 || g_playerEntity.animationId == 4) {
        short realHeld = g_PlayerDpadHeld;
        short realPressed = g_PlayerDpadPressed;
        g_PlayerDpadHeld = 0;
        g_PlayerDpadPressed = 0;
        update_player_anim();
        g_PlayerDpadHeld = realHeld;
        g_PlayerDpadPressed = realPressed;
    } else {
        zs_park();
    }
    g_main_state_flags2 &= ~MSF2_EFFECT_ZONE;
    zs_offscreen_tick();
}

void zm_survivor_begin_ride(const Entity* zombie)
{
    s_rideLastX = zombie->scaMatrixData.localMatrix.t[0];
    s_rideLastZ = zombie->scaMatrixData.localMatrix.t[2];
    s_rideFrames = 0;
    s_rideIdle = 0;
    s_rideTaken = false;
    g_playerEntity.scaMatrixData.localMatrix.t[0] = zombie->scaMatrixData.localMatrix.t[0];
    g_playerEntity.scaMatrixData.localMatrix.t[1] = zombie->scaMatrixData.localMatrix.t[1];
    g_playerEntity.scaMatrixData.localMatrix.t[2] = zombie->scaMatrixData.localMatrix.t[2];
    g_playerEntity.position.x = (short)zombie->scaMatrixData.localMatrix.t[0];
    g_playerEntity.position.y = (short)zombie->scaMatrixData.localMatrix.t[1];
    g_playerEntity.position.z = (short)zombie->scaMatrixData.localMatrix.t[2];
    g_playerEntity.posY = (unsigned short)zombie->scaMatrixData.localMatrix.t[1];
    g_playerEntity.directionAngle = zombie->angle;
    g_playerEntity.animationId = 1;      // under control, as the press finds it
    g_playerEntity.animFrameId = 0;
    g_playerEntity.action_behavior = 0;
    g_playerEntity.action_state = 0;
    g_playerEntity.isBeingAttackedFlag = 0;
}

void zm_survivor_draw_hud(void)
{
    zs_draw_hud();
}

// update_entities runs every enemy's AI against g_playerEntity. While the
// survivor is away the player entity is only the zombie's stand-in for a room
// event, and must not be seen: park it for the enemies' turn and put it back.
void zm_survivor_enemies_begin(void)
{
    s_hiddenForEnemies = false;
    if (s_survivor.present || !zm_zombie_riding()) return;
    for (int i = 0; i < 3; i++) s_hideT[i] = g_playerEntity.scaMatrixData.localMatrix.t[i];
    s_hidePos = g_playerEntity.position;
    s_hidePosY = g_playerEntity.posY;
    g_playerEntity.scaMatrixData.localMatrix.t[0] = s_parkX;
    g_playerEntity.scaMatrixData.localMatrix.t[1] = 0;
    g_playerEntity.scaMatrixData.localMatrix.t[2] = s_parkZ;
    g_playerEntity.position.x = (short)s_parkX;
    g_playerEntity.position.y = 0;
    g_playerEntity.position.z = (short)s_parkZ;
    s_hiddenForEnemies = true;
}

void zm_survivor_enemies_end(void)
{
    if (!s_hiddenForEnemies) return;
    for (int i = 0; i < 3; i++) g_playerEntity.scaMatrixData.localMatrix.t[i] = s_hideT[i];
    g_playerEntity.position = s_hidePos;
    g_playerEntity.posY = s_hidePosY;
    s_hiddenForEnemies = false;
}
