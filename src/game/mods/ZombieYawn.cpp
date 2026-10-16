#include "ZombieModeInternal.h"
#include "ZombieNet.h"
#include "../PrintText.h"
#include "../entities/EntityCommon.h"
#include "../../marni/MarniSound.h"
#include "../../system/AssetPath.h"
#include "../../DebugPrint.h"
#include <cstdio>
#include <cstring>

static constexpr auto s_gatherMessage = STR("Something feels wrong...\\nI should wait for my squadmates.");
static_assert(s_gatherMessage.bytes[24] == 0x02, "Boss-door message must use the encoded newline tag");
static constexpr auto s_atticPrompt = STR("Will you enter the attic?\\c");
static constexpr auto s_lessonPrompt = STR("Will you enter the lesson room?\\c");

// ============================================================================
// ZombieYawn.cpp - port-added: the two Yawn fights of the mode.
//
// Yawn 1 lives in the attic (ROOM7100), Yawn 2 in the lesson room (ROOM70C0).
// Neither room's own script places it in the mode: the attic's enemy_set (id
// 0x0D, death flag 0x18) runs only on the first visit (ScenarioFlags bit 0
// clear), and the lesson room's (id 0x12, death flag 0x3E) is refused like
// every room's own monster (zombie_mode_enemy_spawn). So the mode places each
// itself after its room's init script (zm_yawn_room), on every visit until it
// is beaten, as the script's record already in the room would: Yawn 1 with
// behaviour 0x02 at (11000, 10000) facing 0x400, Yawn 2 with behaviour 0x06
// at (5300, 17500) facing 0xC00 (the others - 0x80 / 0x84 - park a hidden
// snake for an entrance scene the mode never plays). The return attic's
// enemy sounds have no Yawn in them; it takes the first visit's
// (zm_monster_sound_row). The lesson room has its own.
//
// Beaten is the original's: Yawn 1 flees up into the ceiling hole once its
// health drops under 0x0AF0 (yawn_action_flee); Yawn 2 (behaviour 6, the
// scripted selector, which never flees - its flee path is the attic's) dies.
// Either raises its death flag. The original's Yawn 1 has 250 to take before
// the flee; the mode gives each 500 damage budget, doubled for each extra
// living survivor (500 / 1000 / 2000 / 4000).
//
// The fight: the snake hunts from the moment the room loads, its music plays
// (Bgm_08 - the return rooms have none of their own), and a survivor cannot
// leave the room while it is there. Beaten, the room stays shut for
// YW_LOCKDOWN_MS - the survivors regroup: each corpse left by the fight gets
// one revive without a healing item, ignoring the revive limit
// (ZombieSpectate.cpp) - then the
// doors open, every survivor's revives are counted afresh and the game clock
// gets YW_CLOCK_BONUS_MS. The clock does not run while every living survivor
// is in a boss room with its snake, nor during the lockdown. The director
// cannot place, jump, trap or send reinforcements into a boss room before it
// is done. The host (single player: this copy) decides the phases and tells
// the survivors (ZM_EV_BOSS).
//
// What each guards: the key or crest the randomizer puts in its room - key 3
// in the attic, a crest in the lesson room. While the death flag is down that
// pickup is set up hidden - no model, no sparkle, no pickup, and neither the
// pickup nor the close-up event that leads to it can start
// (zombie_mode_yawn_guards). When the flag goes up - here, or from another
// copy through the flag merge - the record runs again as the room's init ran
// it, sparkle and all.
//
// Across copies: the room's owner runs the real snake. It is one skeleton in
// thirteen enemy slots - yawn_init copies the head (slot 0) into slots 1-12,
// one per body joint, so each segment collides and can be shot - and joints
// 3-14 are not a hierarchy: Yawn's own animator places each of them in the
// world (rotation and translation) every frame. The head travels as an
// ordinary ENEMIES entry (its pose carries every joint's rotation) with a
// ZmYawnBody after it (the body joints' world positions and the thirteen
// slots' status bytes); the segments are not sent. Every other copy runs
// yawn_init once, so it has the segments too, then copies the owner's pose
// (zm_yawn_pose_apply); its shots on the head or a segment go to the owner as
// HIT, where the segment's own update drains the head as in the original.
// ============================================================================

extern int  cmd_item_model_set(void);                                   // 0x00461220 CmdFunctions.cpp
extern void ScdEventEntry_Create(unsigned int slot, int scriptIndex);   // RoomEvents.cpp
extern void Flg_on(int baseAddr, unsigned int bitIndex);                // 0x00473ef0
extern void MovePlayerXZ(int angle, SVECTOR* offset, SVECTOR* out);     // WeaponDamage.cpp

#define YW_BOSSES           2
#define YW_FLEE_HEALTH      0x0AF0      // yawn_pick_action_normal flees below this
#define YW_BUDGET           500         // solo budget; doubles per extra living survivor
#define YW_ITEM_OP_LEN      0x1A        // an item_model_set command
#define YW_HELD_MAX         4
#define YW_LOCKDOWN_MS      60000
#define YW_CLOCK_BONUS_MS   (5 * 60000)
// A survivor's copy that sees a death flag up but no word from the host (a
// rejoin after the lockdown): the room counts as done after this long.
#define YW_PENDING_MS       (YW_LOCKDOWN_MS + 10000)
// The fight's music: the first visit's attic music group 6 (and Yawn 2's
// lesson room, group 0x12 channel 1) - Bgm_08, looped (g_BgmLoopTable 1).
// The return rooms' own music is held off under it.
#define YW_MUSIC_WAV        GAME_DATA_ROOT "sound\\bgm_08.wav"

struct YwBoss {
    unsigned char stage, room, flag, id, behavior;
    short         x, z, angle;
    bool          flees;                // beaten by fleeing (else by dying)
    const char*   name;
};
static const YwBoss kBoss[YW_BOSSES] = {
    { STAGE_MANSION_RETURN_2F, ROOM_ATTIC,       0x18, ENEMY_YAWN_1, 0x02, 11000, 10000, 0x0400, true,  "YAWN 1" },
    { STAGE_MANSION_RETURN_2F, ROOM_LESSON_ROOM, 0x3E, ENEMY_YAWN_2, 0x06,  5300, 17500, 0x0C00, false, "YAWN 2" },
};

// The match's: each boss's phase (the host's word on a survivor's copy).
enum { YW_WAITING, YW_LOCKDOWN, YW_DONE };
static unsigned char s_phase[YW_BOSSES];
static unsigned char s_freeRevives[YW_BOSSES]; // corpses left by this fight, consumed on revive
static unsigned int  s_lockEnd[YW_BOSSES];      // game ms the lockdown ends
static unsigned int  s_pendingAt[YW_BOSSES];    // a survivor's: the flag first seen up, 0 not yet

// This visit's.
static bool s_scaled = false;           // this room's Yawn has its mode health
static bool s_here = false;             // Yawn was placed in this visit's room
static int  s_music = 0;                // Bgm_08's sound bank, 0 none
static int  s_blockedDoorSound = 0;     // heavier metal-door rattle, room lifetime
static bool s_bgmHeld = false;          // the room's own music is held off

// The room's guarded pickups, held back this visit.
struct YwHeld {
    unsigned char op[YW_ITEM_OP_LEN];   // the record as the room had it, before the randomizer's patch
    unsigned char slot, model, flag;
    short         zone[4];              // x, z, width, depth
};
static YwHeld        s_held[YW_HELD_MAX];
static int           s_heldCount = 0;
static bool          s_holding = false; // the record now running is being held
// The room action entry keeps a pointer into the record it was set from (the
// pickup reads the item there), so each shown record has a buffer of its own.
static unsigned char s_shown[YW_HELD_MAX][0x20];
static bool          s_fledSeen = false;

static ZmYawnBody    s_body;
static bool          s_bodyHave = false;
static unsigned char s_bodyStage, s_bodyRoom;
static int           s_bodyOrigin = -1;

static int yw_boss_at(unsigned char stage, unsigned char room)
{
    for (int b = 0; b < YW_BOSSES; b++) {
        if (kBoss[b].stage == stage && kBoss[b].room == room) return b;
    }
    return -1;
}

static int yw_here(void)
{
    return yw_boss_at(g_stageId, g_roomId);
}

// A possessed snake's animator moves the root as well as its body. Endpoint
// collision alone can miss a wall crossed by a long animation step.
extern int is_entity_in_switch_zone(VECTOR* position, void* zoneData);
static bool s_boundSaved = false;
static Entity* s_boundHead = NULL;
static MATRIX s_boundMatrix;
static JointStruct s_boundJoints[15];

static bool yw_floor_point(int x, int z)
{
    if (g_RdtPointer == NULL || g_RdtPointer->cam_switch_zones == NULL) return false;
    VECTOR p = { x, 0, z, 0 };
    const CAM_SWITCH_ZONE* zone = (const CAM_SWITCH_ZONE*)g_RdtPointer->cam_switch_zones;
    for (int i = 0; i < 1024; i++, zone++) {
        if ((unsigned short)zone->camFrom >= g_RdtPointer->cameras_count) break;
        if (is_entity_in_switch_zone(&p, (void*)zone) != 0) return true;
    }
    return false;
}

static bool yw_clear_point(int x, int z)
{
    if (!yw_floor_point(x, z)) return false;
    VECTOR p = { x, 0, z, 0 };
    SVECTOR zero = { 0, 0, 0, 0 };
    VECTOR scratch = g_playerPosScratch;
    bool clear = room_collision_check_0047da50(&p, (VECTOR*)&zero) != 1;
    g_playerPosScratch = scratch;
    return clear;
}

// Sweep in short steps, retaining the tangential component at a wall.
// Each link is constrained independently; a tail collision must never cancel
// the head's animation or reset an attack.
static void yw_slide(int ax, int az, int* bx, int* bz)
{
    int dx = *bx - ax, dz = *bz - az;
    if (abs(dx) > 65535 || abs(dz) > 65535) { *bx = ax; *bz = az; return; }
    int span = abs(dx) > abs(dz) ? abs(dx) : abs(dz);
    int steps = span / 64 + 1;
    int x = ax, z = az, prevX = 0, prevZ = 0;
    for (int i = 1; i <= steps; i++) {
        int nextX = dx * i / steps, nextZ = dz * i / steps;
        int sx = nextX - prevX, sz = nextZ - prevZ;
        prevX = nextX; prevZ = nextZ;
        if (yw_clear_point(x + sx, z + sz)) { x += sx; z += sz; }
        else {
            if (yw_clear_point(x + sx, z)) x += sx;
            if (yw_clear_point(x, z + sz)) z += sz;
        }
    }
    *bx = x; *bz = z;
}

static void yw_bounds_save(Entity* e)
{
    s_boundHead = e;
    s_boundSaved = true;
    s_boundMatrix = e->scaMatrixData.localMatrix;
    memcpy(s_boundJoints, e->jointsStructs, sizeof(s_boundJoints));
}

static void yw_bounds_restore(Entity* e)
{
    // Recover translation only. Animation, steering and attack state continue.
    int* t = e->scaMatrixData.localMatrix.t;
    int dx = s_boundMatrix.t[0] - t[0], dz = s_boundMatrix.t[2] - t[2];
    t[0] += dx; t[2] += dz;
    for (int k = 0; k < 15; k++) {
        e->jointsStructs[k].world.t[0] += dx;
        e->jointsStructs[k].world.t[2] += dz;
    }
    e->position.x = (short)t[0];
    e->position.z = (short)t[2];
}

void zm_yawn_bounds_begin(Entity* e)
{
    // The slam returns to its captured pose. Its temporary head/neck pose
    // may pass through a wall; do not clip it or save it as a movement origin.
    if (e->state == 1 && e->action_behavior == 10) {
        s_boundSaved = false;
        s_boundHead = NULL;
        return;
    }
    if (!zombie_mode_armed() || yw_here() < 0 || e->jointsStructs == NULL || e->jointCount != 15) return;
    if (s_boundHead != e) s_boundSaved = false;
    // Segment collision runs after the head's update and can drag the head
    // joints again. Check those changes before replacing the safe snapshot.
    if (s_boundSaved) zm_yawn_bounds_end(e);
    const int* t = e->scaMatrixData.localMatrix.t;
    if (yw_clear_point(t[0], t[2])) {
        yw_bounds_save(e);
    } else if (s_boundSaved) {
        yw_bounds_restore(e);
    } else {
        // Already outside when possession began: move the entire skeleton to
        // the room's boss spawn, preserving the spacing between its joints.
        int b = yw_here(), dx = kBoss[b].x - t[0], dz = kBoss[b].z - t[2];
        e->scaMatrixData.localMatrix.t[0] = kBoss[b].x;
        e->scaMatrixData.localMatrix.t[2] = kBoss[b].z;
        for (int k = 0; k < 15; k++) {
            e->jointsStructs[k].world.t[0] += dx;
            e->jointsStructs[k].world.t[2] += dz;
        }
        yw_bounds_save(e);
        yw_bounds_restore(e);
    }
}

void zm_yawn_bounds_end(Entity* e)
{
    if (e->state == 1 && e->action_behavior == 10) return;
    if (!s_boundSaved || s_boundHead != e || e->jointsStructs == NULL) return;
    int* t = e->scaMatrixData.localMatrix.t;
    yw_slide(s_boundMatrix.t[0], s_boundMatrix.t[2], &t[0], &t[2]);
    for (int k = 0; k < 15; k++) {
        if (k == 1 || k == 2) continue; // animator's head helpers
        const int* a = s_boundJoints[k].world.t;
        int* b = e->jointsStructs[k].world.t;
        // Existing embedded links are left to the original segment collision.
        if (yw_clear_point(a[0], a[2])) yw_slide(a[0], a[2], &b[0], &b[2]);
    }
    e->position.x = (short)t[0];
    e->position.z = (short)t[2];
}

static bool yw_beaten(int b)
{
    return Flg_ck((int)g_EnemiesFlags, kBoss[b].flag) != 0;
}

static bool yw_guarded_item(unsigned char id)
{
    return (id >= ITEM_SWORD_KEY && id <= ITEM_HELMET_KEY) || id == ITEM_WIND_CREST ||
           id == ITEM_MOON_CREST || id == ITEM_STAR_CREST || id == ITEM_SUN_CREST;
}

// The fight music's sound bank (0 none): ducked under voice lines.
int zm_yawn_music_bank(void) { return s_music; }

void zm_yawn_blocked_sound(void)
{
    if (s_blockedDoorSound == 0)
        s_blockedDoorSound = loadSndBankFromWav(GAME_DATA_ROOT "sound\\DM_gacha.wav");
    if (s_blockedDoorSound == 0) {
        play_sfx(2, 0x14, 0);
        return;
    }
    pan_set(s_blockedDoorSound, 0);
    set_volume(s_blockedDoorSound, g_SfxVolume);
    SetSndSlot(s_blockedDoorSound, 0);
}

bool zm_yawn_entity(const Entity* e)
{
    if (!zombie_mode_armed() || e == NULL) return false;
    if (e->id != ENEMY_YAWN_1 && e->id != ENEMY_YAWN_2) return false;
    int slot = (int)(e - g_EnemiesList);
    return slot >= 0 && slot < ZM_YAWN_SLOTS;
}

bool zm_yawn_segment(const Entity* e)
{
    return zm_yawn_entity(e) && e->behavior_flags == 1;
}

bool zm_yawn_head(const Entity* e)
{
    return zm_yawn_entity(e) && (e->behavior_flags & 1) == 0;
}

// After the init script, before the roster's extras (which keep above slot
// 12: zm_yawn_last_slot) and the room's model loading.
static void yw_spawn(int b)
{
    if (yw_beaten(b)) return;
    if ((g_EnemiesList[0].status_flags & ENTITY_STATUS_ACTIVE) != 0) {
        dbg_printf("[yawn] %s: slot 0 is taken (id %02X), no Yawn\n", kBoss[b].name, (unsigned)g_EnemiesList[0].id);
        return;
    }
    const YwBoss& k = kBoss[b];
    Entity* e = zm_spawn_monster(0, k.id, k.behavior, k.x, 0, k.z, k.angle);
    if (e == NULL) return;
    s_here = true;
    e->death_event_id = k.flag;     // raised by the flee (yawn_action_flee) or the death (yawn_die_run)
    zm_world_track_uid(e, 0);
    dbg_printf("[yawn] %s in slot 0 (stage %d room %02X)\n", k.name, (int)k.stage + 1, (unsigned)k.room);
}

int zm_yawn_last_slot(unsigned char id, int slot)
{
    return ((id == ENEMY_YAWN_1 || id == ENEMY_YAWN_2) && slot == 0) ? ZM_YAWN_SLOTS - 1 : slot;
}

void zm_yawn_mark_scaled(void)
{
    s_scaled = true;
}

// Bound the roster before shifting; even a four-survivor AI-hosted game
// remains comfortably inside the original signed 16-bit health field.
static int yw_health_budget(int alive)
{
    if (alive < 1) alive = 1;
    if (alive > 4) alive = 4;
    return YW_BUDGET << (alive - 1);
}

// After the head's update on the copy that runs it.
void zm_yawn_after_update(Entity* e)
{
    if (!zm_yawn_head(e) || e->state == 0 || zombie_mode_is_puppet(e)) return;
    // yawn_init leaves ignore_player (+0x85) set, which keeps yawn_state_check
    // from ever choosing a move: the original clears it at the end of the
    // ceiling entrance (yawn_action_emerge), a scene the in-room snake never
    // plays - it would only crawl until shot (yawn_damaged_run clears it too).
    // Idling or crawling (actions 0 / 1) it is cleared; the attacks, the flee
    // and the reposition set it themselves while they run.
    // A possessed Yawn's choosing is the pad's (zm_yawn_possessed).
    if (e->state == 1 && e->action_behavior <= 1 && e->ignore_player_flag != 0 && e != g_zombieModeEntity) {
        e->ignore_player_flag = 0;
        dbg_printf("[yawn] awake: it hunts the survivors\n");
    }
    if (s_scaled) return;
    ZmSurvivorInfo surv[ZM_NET_MAX_PLAYERS];
    int n = zm_survivor_list(surv, ZM_NET_MAX_PLAYERS);
    int alive = 0;
    for (int i = 0; i < n; i++) if (!surv[i].dead) alive++;
    if (alive < 1) alive = 1;
    bool flees = e->id == ENEMY_YAWN_1;
    e->health = (short)((flees ? YW_FLEE_HEALTH : 0) + yw_health_budget(alive));
    s_scaled = true;
    dbg_printf("[yawn] health %d (%d survivors), %s under %d\n", (int)e->health, alive,
               flees ? "flees" : "dies", flees ? YW_FLEE_HEALTH : 0);
}

// ---------------------------------------------------------------------------
// The match's phases
// ---------------------------------------------------------------------------
static void yw_vote_reset(void);      // with the entry vote below

void zm_yawn_new_game(void)
{
    yw_vote_reset();
    memset(s_phase, YW_WAITING, sizeof(s_phase));
    memset(s_freeRevives, 0, sizeof(s_freeRevives));
    memset(s_lockEnd, 0, sizeof(s_lockEnd));
    memset(s_pendingAt, 0, sizeof(s_pendingAt));
}

static void yw_send(int op, int b, int tenths)
{
    if (zm_net_role() != ZM_NET_ZOMBIE) return;
    unsigned int seed = zm_net_seed();
    zm_net_send_event8(ZM_EV_BOSS, (short)op, (short)b, (short)tenths, (short)seed, (short)(seed >> 16), s_freeRevives[b], 0, 0);
}

static void yw_lockdown(int b, unsigned int ms)
{
    zm_econ_points();               // the income up to now is earned; none from here
    s_phase[b] = YW_LOCKDOWN;
    s_lockEnd[b] = zm_game_time_ms() + ms;
    dbg_printf("[yawn] %s beaten: lockdown %u ms\n", kBoss[b].name, ms);
}

static void yw_done(int b)
{
    zm_econ_points();               // the lockdown's time stays unpaid
    s_phase[b] = YW_DONE;
    s_freeRevives[b] = 0;
    zm_revive_reset_counts();
    dbg_printf("[yawn] %s: lockdown over, the doors open\n", kBoss[b].name);
}

// The host (single player: this copy), once a frame: a death flag up starts
// that boss's lockdown; its end opens the room and adds to the clock.
static void yw_match_frame(void)
{
    if (!zm_match_authority() || zombie_mode_match_over()) return;
    unsigned int now = zm_game_time_ms();
    for (int b = 0; b < YW_BOSSES; b++) {
        if (s_phase[b] == YW_WAITING && yw_beaten(b)) {
            // Freeze eligibility at the end of the fight. Later deaths in this
            // room (including after a free revive) must pay the normal cost.
            ZmSurvivorInfo surv[ZM_NET_MAX_PLAYERS];
            int n = zm_survivor_list(surv, ZM_NET_MAX_PLAYERS);
            for (int i = 0; i < n; i++) {
                if (surv[i].dead && surv[i].stage == kBoss[b].stage && surv[i].room == kBoss[b].room)
                    s_freeRevives[b] |= (unsigned char)(1u << surv[i].player);
            }
            yw_lockdown(b, YW_LOCKDOWN_MS);
            yw_send(1, b, YW_LOCKDOWN_MS / 100);
        } else if (s_phase[b] == YW_LOCKDOWN && (int)(now - s_lockEnd[b]) >= 0) {
            yw_done(b);
            zm_clock_add_bonus(YW_CLOCK_BONUS_MS);
            yw_send(2, b, 0);
        }
    }
}

// ---------------------------------------------------------------------------
// Going in together. A boss room's door, open, takes nobody alone while its
// Yawn waits: every living survivor must stand by the one at the door
// (YW_GATHER_RADIUS, in the same room), else it says so and stays shut.
// Gathered, the host asks each of them through the native Yes/No message
// (YW_ASK_MS to answer, standing still); all yes, every copy walks through
// that door at once -
// the room's own door record, its own door animation - and the fight, and the
// clock's hold, start with all of them in. A no, a survivor gone off or dead,
// or the time running out calls it off. A lone network survivor confirms too;
// single player as director has no vote. ZM_EV_BOSS ops 3-8 (ZombieModeInternal.h).
// ---------------------------------------------------------------------------
#define YW_GATHER_RADIUS 3000
#define YW_ASK_MS        15000
#define YW_GO_MS         5000           // a copy busy (a hit, a menu) may still go this long after
enum { YW_OP_LOCKDOWN = 1, YW_OP_DONE = 2, YW_OP_REQUEST = 3, YW_OP_ASK = 4, YW_OP_ANSWER = 5,
       YW_OP_GO = 6, YW_OP_OFF = 7, YW_OP_REFUSED = 8 };

// The host's vote.
static int           s_voteBoss = -1;
static unsigned short s_voteToken = 0;
static unsigned char s_voteVoters = 0, s_voteYes = 0;
static unsigned int  s_voteEnd = 0;
static int           s_voteFrom = -1;               // the survivor at the door
// A survivor's: the question, its answer, and the walk in.
static int           s_askBoss = -1;
static unsigned short s_askToken = 0;
static bool          s_askAnswered = false;
static bool          s_askHeld = false;             // Action / Aim still down from before the question
static bool          s_askShown = false;
static unsigned char s_askVoters = 0;               // everyone asked (seat bits)
// The team's places by the door inside, in seat order among those asked: the
// door's own arrival point, either side of it, then a step into the room -
// (ahead, to the side) of the arrival facing. Each copy keeps the n-th one
// its walls leave clear, the same on every copy.
static const short kTeamPlaces[][2] = {
    { 0, 0 }, { 0, 800 }, { 0, -800 }, { 800, 0 }, { 800, 800 }, { 800, -800 }, { 1600, 0 },
};
static int           s_arriveBoss = -1;             // came in with the team: place itself
static int           s_arriveRank = 0;
static unsigned int  s_askEnd = 0;
static int           s_goBoss = -1;
static unsigned int  s_goUntil = 0;
static unsigned char s_goRecord[0x20];

static const unsigned char* yw_prompt_text(int b)
{
    return b == 0 ? (const unsigned char*)s_atticPrompt : (const unsigned char*)s_lessonPrompt;
}

static void yw_prompt_close(void)
{
    if (s_askBoss >= 0 && s_askShown &&
        g_MessagePtr == yw_prompt_text(s_askBoss) && (g_menu_choice_id & 0x80) != 0) {
        g_menu_choice_id &= 0x7F;
        zombie_mode_message_prompt_end();
    }
    s_askShown = false;
}

static void yw_vote_reset(void)
{
    s_voteBoss = s_askBoss = s_goBoss = -1;
}

static unsigned int yw_seed(void) { return zm_net_seed(); }

static bool yw_host_survivor(void)
{
    return zm_net_role() == ZM_NET_ZOMBIE && zm_net_char(zm_net_self()) >= 0;
}

// The host's word, to everyone (its own survivor - an AI director's game -
// takes it at once).
static void yw_host_send(int op, int b, int dst, unsigned short token, unsigned char mask)
{
    unsigned int seed = yw_seed();
    short a[8] = { (short)op, (short)b, (short)(dst < 0 ? 0 : dst), (short)seed, (short)(seed >> 16),
                   (short)token, (short)mask, 0 };
    if (zm_net_role() == ZM_NET_ZOMBIE) {
        if (dst < 0) zm_net_send_event8(ZM_EV_BOSS, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
        else if (dst != zm_net_self()) zm_net_send_event_to(dst, ZM_EV_BOSS, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
    }
    if (yw_host_survivor() && (dst < 0 || dst == zm_net_self())) zm_yawn_take(a, ZM_NET_DIRECTOR);
}

// A survivor's word to the host (the host's own survivor: straight in).
static void yw_ask_host(int op, int b, unsigned short token, int yes)
{
    unsigned int seed = yw_seed();
    short a[8] = { (short)op, (short)b, (short)zm_net_self(), (short)seed, (short)(seed >> 16),
                   (short)token, (short)yes, 0 };
    if (zm_net_role() == ZM_NET_ZOMBIE) zm_yawn_take(a, zm_net_self());
    else zm_net_send_event_to(ZM_NET_DIRECTOR, ZM_EV_BOSS, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
}

// The host: every living survivor in `from`'s room, within reach of it. Their
// mask in `voters`.
static bool yw_host_gathered(int from, unsigned char* voters)
{
    const ZmNetPeerState* f = zm_seat_state(from);
    if (f == NULL || f->dead || f->spectating) return false;
    *voters = 0;
    for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
        if (zm_net_char(i) < 0) continue;
        const ZmNetPeerState* p = zm_seat_state(i);
        if (p == NULL || p->dead || p->spectating) continue;
        if (p->stage != f->stage || p->room != f->room || p->transitioning) return false;
        int dx = p->x - f->x, dz = p->z - f->z;
        if (dx < -YW_GATHER_RADIUS || dx > YW_GATHER_RADIUS || dz < -YW_GATHER_RADIUS || dz > YW_GATHER_RADIUS ||
            dx * dx + dz * dz > YW_GATHER_RADIUS * YW_GATHER_RADIUS) return false;
        *voters |= (unsigned char)(1u << i);
    }
    return *voters != 0;
}

static void yw_vote_off(const char* why)
{
    if (s_voteBoss < 0) return;
    dbg_printf("[yawn] entry to %s called off: %s\n", kBoss[s_voteBoss].name, why);
    yw_host_send(YW_OP_OFF, s_voteBoss, -1, s_voteToken, 0);
    s_voteBoss = -1;
}

// The host, once a frame: the vote holds while every voter stays gathered.
static void yw_vote_frame(void)
{
    if (s_voteBoss < 0 || !zm_match_authority()) return;
    unsigned char voters;
    if (s_phase[s_voteBoss] != YW_WAITING || yw_beaten(s_voteBoss)) { yw_vote_off("the fight is over"); return; }
    if ((int)(zm_game_time_ms() - s_voteEnd) >= 0) { yw_vote_off("no answer in time"); return; }
    if (!yw_host_gathered(s_voteFrom, &voters) || voters != s_voteVoters) { yw_vote_off("the team split up"); return; }
    if (s_voteYes != s_voteVoters) return;
    dbg_printf("[yawn] everyone in for %s: go\n", kBoss[s_voteBoss].name);
    yw_host_send(YW_OP_GO, s_voteBoss, -1, s_voteToken, 0);
    s_voteBoss = -1;
}

// ZM_EV_BOSS: the phases from the host, and the entry vote both ways.
void zm_yawn_take(const short* a, int src)
{
    unsigned int seed = (unsigned short)a[3] | ((unsigned int)(unsigned short)a[4] << 16);
    if (!zombie_mode_armed() || seed != zm_net_seed()) return;
    int b = a[1], op = a[0];
    if (b < 0 || b >= YW_BOSSES) return;
    unsigned short token = (unsigned short)a[5];
    if (zm_match_authority() && (op == YW_OP_REQUEST || op == YW_OP_ANSWER)) {
        int from = a[2];
        if (from < 0 || from >= ZM_NET_MAX_PLAYERS || from != src) return;
        if (op == YW_OP_REQUEST) {
            unsigned char voters;
            if (s_voteBoss >= 0) return;                    // one at a time
            if (s_phase[b] != YW_WAITING || yw_beaten(b) || !yw_host_gathered(from, &voters)) {
                yw_host_send(YW_OP_REFUSED, b, from, 0, 0);
                return;
            }
            s_voteBoss = b;
            s_voteToken = (unsigned short)(s_voteToken + 1);
            s_voteVoters = voters;
            s_voteYes = 0;
            s_voteFrom = from;
            s_voteEnd = zm_game_time_ms() + YW_ASK_MS;
            dbg_printf("[yawn] survivor %d at %s's door: asking (voters %02X)\n", from, kBoss[b].name, voters);
            yw_host_send(YW_OP_ASK, b, -1, s_voteToken, voters);
        } else if (s_voteBoss == b && token == s_voteToken && (s_voteVoters & (1u << from)) != 0) {
            if (a[6] == 0) { yw_vote_off("a survivor said no"); return; }
            s_voteYes |= (unsigned char)(1u << from);
        }
        return;
    }
    if (src != ZM_NET_DIRECTOR) return;
    switch (op) {
    case YW_OP_LOCKDOWN:
        if (!zm_match_authority() && s_phase[b] == YW_WAITING) {
            s_freeRevives[b] = (unsigned char)a[5];
            yw_lockdown(b, (unsigned int)(unsigned short)a[2] * 100u);
        }
        break;
    case YW_OP_DONE:
        if (!zm_match_authority() && s_phase[b] != YW_DONE) yw_done(b);
        break;
    case YW_OP_ASK:
        if (((unsigned char)a[6] & (1u << zm_net_self())) == 0 || g_playerEntity.health < 0) break;
        s_askBoss = b;
        s_askToken = token;
        s_askAnswered = false;
        s_askShown = false;
        s_askHeld = true;
        s_askVoters = (unsigned char)a[6];
        s_askEnd = zm_game_time_ms() + YW_ASK_MS;
        break;
    case YW_OP_GO:
        if (s_askBoss != b || s_askToken != token) break;
        yw_prompt_close();
        s_askBoss = -1;
        s_goBoss = b;
        s_goUntil = zm_game_time_ms() + YW_GO_MS;
        s_arriveRank = 0;
        for (int i = 0; i < zm_net_self(); i++) if ((s_askVoters & (1u << i)) != 0) s_arriveRank++;
        break;
    case YW_OP_OFF:
        if (s_askBoss == b && s_askToken == token) {
            yw_prompt_close();
            s_askBoss = -1;
        }
        break;
    case YW_OP_REFUSED:
        zm_message_show(s_gatherMessage);
        break;
    default:
        break;
    }
}

// A boss room's door this copy's survivor is taking: going in alone only with
// nobody else alive; else gathered, or told to gather (a first check here, the
// host's after).
bool zombie_mode_boss_door_gate(const unsigned char* record)
{
    if (!zombie_mode_armed() || zm_game_role() != ZM_NET_SURVIVOR || record == NULL) return false;
    unsigned char ds, dr;
    zm_decode_dest(record[0x0D], g_stageId, &ds, &dr);
    int b = yw_boss_at(ds, dr);
    if (b < 0 || s_phase[b] != YW_WAITING || yw_beaten(b)) return false;
    ZmSurvivorInfo surv[ZM_NET_MAX_PLAYERS];
    // The director's list omits this client's own survivor, but the room
    // list below includes it. Count the local player once on either host
    // or client, so a remote teammate cannot be mistaken for a solo game.
    int n = zm_survivor_list(surv, ZM_NET_MAX_PLAYERS);
    int living = g_playerEntity.health >= 0 ? 1 : 0;
    for (int i = 0; i < n; i++) {
        if (surv[i].player != zm_net_self() && !surv[i].dead) living++;
    }
    if (s_askBoss >= 0) return true;                        // the question is up already
    int xz[ZM_NET_MAX_PLAYERS][2], who[ZM_NET_MAX_PLAYERS];
    int here = zm_room_survivors(xz, who, ZM_NET_MAX_PLAYERS), gathered = 0;
    int mx = g_playerEntity.scaMatrixData.localMatrix.t[0], mz = g_playerEntity.scaMatrixData.localMatrix.t[2];
    for (int k = 0; k < here; k++) {
        int dx = xz[k][0] - mx, dz = xz[k][1] - mz;
        if (dx >= -YW_GATHER_RADIUS && dx <= YW_GATHER_RADIUS && dz >= -YW_GATHER_RADIUS && dz <= YW_GATHER_RADIUS &&
            dx * dx + dz * dz <= YW_GATHER_RADIUS * YW_GATHER_RADIUS) gathered++;
    }
    if (gathered < living) {
        zm_message_show(s_gatherMessage);
        return true;
    }
    yw_ask_host(YW_OP_REQUEST, b, 0, 0);
    return true;
}

// The native message renderer owns the Yes/No cursor and confirm button.
// Consume its answer before the original item/event post-action dispatch.
bool zombie_mode_boss_prompt_answer(bool yes)
{
    if (!zombie_mode_armed() || s_askBoss < 0 || !s_askShown || s_askAnswered ||
        g_MessagePtr != yw_prompt_text(s_askBoss)) return false;
    s_askAnswered = true;
    yw_ask_host(YW_OP_ANSWER, s_askBoss, s_askToken, yes ? 1 : 0);
    return true;
}

// Let the native prompt read the pad; after Yes, stand still for the team.
bool zm_yawn_prompt_input(void)
{
    if (s_askBoss < 0) return false;
    if (g_playerEntity.health < 0 || (int)(zm_game_time_ms() - s_askEnd) >= 0) {
        yw_prompt_close();
        if (!s_askAnswered) yw_ask_host(YW_OP_ANSWER, s_askBoss, s_askToken, 0);
        s_askBoss = -1;
        return false;
    }
    // The press that took the door is no answer: both buttons up first (the
    // game's pressed word stays set while a button is held).
    if (s_askHeld && ((unsigned short)g_PlayerDpadHeld & (ZM_PAD_ACTION | ZM_PAD_AIM)) == 0) s_askHeld = false;
    if (!s_askAnswered) {
        if (!s_askShown && (g_menu_choice_id & 0x80) == 0 &&
            (g_main_state_flags & (MSF_MENU_ACTIVE | MSF_DOOR_TRANSITION)) == 0 && g_openMenuFlag == 0) {
            zm_message_show(yw_prompt_text(s_askBoss));
            s_askShown = g_MessagePtr == yw_prompt_text(s_askBoss) && (g_menu_choice_id & 0x80) != 0;
        }
        if (s_askHeld) g_PlayerDpadPressed &= ~ZM_PAD_ACTION;
        if (s_askShown) return true;
    }
    g_PlayerPadHeld = g_PlayerPadPressed = g_PlayerDpadHeld = g_PlayerDpadPressed = 0;
    return true;
}

// Everyone said yes: through the loaded room's own door into the boss room,
// as door_begin_transition takes a door (ZombieShotgun.cpp's rescue does the
// same from this frame).
static void yw_go_frame(void)
{
    if (s_goBoss < 0) return;
    if ((int)(zm_game_time_ms() - s_goUntil) >= 0 || g_playerEntity.health < 0) {
        s_goBoss = -1;
        return;
    }
    if (g_playerEntity.isBeingAttackedFlag != 0 || (g_main_state_flags & (MSF_MENU_ACTIVE | MSF_DOOR_TRANSITION)) != 0 ||
        (g_message_flags & 0x0101) != 0x0101 || zombie_mode_pickup_waiting()) return;
    const unsigned char* door = NULL;
    for (int i = 0; i < ROOM_ACTION_ENTRIES && door == NULL; i++) {
        const unsigned char* entry = &g_RoomActionTable[i * 12];
        if (entry[0] != 1) continue;
        const unsigned char* rec = *(const unsigned char* const*)(entry + 8);
        if (rec == NULL) continue;
        unsigned char ds, dr;
        zm_decode_dest(rec[0x0D], g_stageId, &ds, &dr);
        if (ds == kBoss[s_goBoss].stage && dr == kBoss[s_goBoss].room) door = rec;
    }
    int b = s_goBoss;
    s_goBoss = -1;
    if (door == NULL) {
        dbg_printf("[yawn] no door to %s in this room\n", kBoss[b].name);
        return;
    }
    dbg_printf("[yawn] in to %s with the team\n", kBoss[b].name);
    s_arriveBoss = b;
    memcpy(s_goRecord, door, sizeof(s_goRecord));
    g_pendingDoorRecord = (int)s_goRecord;
    g_main_state_flags |= MSF_GAMEPLAY_ACTIVE;
    g_message_flags = 0;
    g_rect.textureId = 0;
    g_rect.x = -160; g_rect.y = -120; g_rect.w = 320; g_rect.h = 240;
    g_rect.r = g_rect.g = g_rect.b = 0;
    g_openMenuFlag = 1;
    draw_rect(&g_rect, 0, 0);
    Task_sleep(1);
    StMask(0, 0);
}

// A survivor's copy: a death flag up the host has not spoken of in a long
// while (a rejoin that missed the events) counts as done.
static void yw_pending_frame(void)
{
    if (zm_match_authority()) return;
    unsigned int now = zm_game_time_ms();
    for (int b = 0; b < YW_BOSSES; b++) {
        if (s_phase[b] != YW_WAITING || !yw_beaten(b)) { s_pendingAt[b] = 0; continue; }
        if (s_pendingAt[b] == 0) s_pendingAt[b] = now != 0 ? now : 1;
        else if (now - s_pendingAt[b] >= YW_PENDING_MS) yw_done(b);
    }
}

static unsigned int yw_lock_left(int b)
{
    if (s_phase[b] != YW_LOCKDOWN) return 0;
    int left = (int)(s_lockEnd[b] - zm_game_time_ms());
    return left > 0 ? (unsigned int)left : 0;
}

bool zm_yawn_clock_hold(void)
{
    if (zm_yawn_lockdown()) return true;
    ZmSurvivorInfo surv[ZM_NET_MAX_PLAYERS];
    int n = zm_survivor_list(surv, ZM_NET_MAX_PLAYERS);
    for (int b = 0; b < YW_BOSSES; b++) {
        if (s_phase[b] != YW_WAITING || yw_beaten(b)) continue;
        int alive = 0, in = 0;
        for (int i = 0; i < n; i++) {
            if (surv[i].dead) continue;
            alive++;
            if (surv[i].stage == kBoss[b].stage && surv[i].room == kBoss[b].room) in++;
        }
        if (alive > 0 && in == alive) return true;
    }
    return false;
}

bool zm_yawn_lockdown(void)
{
    for (int b = 0; b < YW_BOSSES; b++) if (s_phase[b] == YW_LOCKDOWN) return true;
    return false;
}

int zm_yawn_beaten_count(void)
{
    int n = 0;
    for (int b = 0; b < YW_BOSSES; b++) if (s_phase[b] != YW_WAITING) n++;
    return n;
}

bool zm_yawn_director_closed(unsigned char stage, unsigned char room)
{
    int b = yw_boss_at(stage, room);
    return zombie_mode_armed() && b >= 0 && s_phase[b] != YW_DONE;
}

unsigned char zm_yawn_jump_room(unsigned char stage, unsigned char room)
{
    int b = yw_boss_at(stage, room);
    return (zombie_mode_armed() && b >= 0 && s_phase[b] == YW_WAITING && !yw_beaten(b)) ? kBoss[b].id : 0;
}

void zm_yawn_spawn_spot(unsigned char stage, unsigned char room, short* x, short* z, short* angle)
{
    int b = yw_boss_at(stage, room);
    if (b < 0) return;
    *x = kBoss[b].x;
    *z = kBoss[b].z;
    *angle = kBoss[b].angle;
}

// ---------------------------------------------------------------------------
// Its moves against several survivors (Yawn.cpp actions 9 and 10), for its
// own AI - a possessing director starts them from the pad. The selectors ask
// every frame they choose:
//   the thrash: two survivors or more within YW_BODY_REACH of the body, or
//     one along its back half (out of the bite's reach, it whips round);
//   the slam: a survivor YW_SLAM_NEAR..YW_SLAM_FAR in front of the head, now
//     and then (1 in YW_SLAM_ODDS a choosing frame).
// Each has a cooldown, and any of them waits YW_SPECIAL_GAP_MS after the last.
// ---------------------------------------------------------------------------
#define YW_BODY_REACH       2500
#define YW_THRASH_MS        9000
#define YW_SLAM_MS          12000
#define YW_SPECIAL_GAP_MS   3000
#define YW_SLAM_NEAR        2500
#define YW_SLAM_FAR         7000
#define YW_SLAM_ARC         0x300       // either side of straight ahead (4096 a turn)
#define YW_SLAM_ODDS        45
static unsigned int s_thrashAt = 0, s_slamAt = 0, s_specialAt = 0;

static bool yw_ready(unsigned int at, unsigned int gap, unsigned int now)
{
    return at == 0 || now - at >= gap;
}

int zombie_mode_yawn_special(void)
{
    Entity* e = ENTITY;
    if (!zombie_mode_armed() || !zm_yawn_head(e) || e == g_zombieModeEntity || e->jointsStructs == NULL) return 0;
    unsigned int now = zm_game_time_ms();
    if (!yw_ready(s_specialAt, YW_SPECIAL_GAP_MS, now)) return 0;
    int xz[ZM_NET_MAX_PLAYERS][2], who[ZM_NET_MAX_PLAYERS];
    int n = zm_room_survivors(xz, who, ZM_NET_MAX_PLAYERS);
    if (n == 0) return 0;
    const JointStruct* j = e->jointsStructs;
    int nearBody = 0;
    bool behind = false, front = false;
    for (int k = 0; k < n; k++) {
        int best = 0x7FFFFFFF, bestJoint = 0;
        for (int i = 0; i <= 14; i++) {
            if (i == 1 || i == 2) continue;
            int dx = xz[k][0] - j[i].world.t[0], dz = xz[k][1] - j[i].world.t[2];
            int d = (dx < 0 ? -dx : dx) + (dz < 0 ? -dz : dz);
            if (d < best) { best = d; bestJoint = i; }
        }
        if (best < YW_BODY_REACH) {
            nearBody++;
            if (bestJoint >= 8) behind = true;
        }
        int dx = xz[k][0] - j[0].world.t[0], dz = xz[k][1] - j[0].world.t[2];
        int d = (dx < 0 ? -dx : dx) + (dz < 0 ? -dz : dz);
        if (d >= YW_SLAM_NEAR && d <= YW_SLAM_FAR) {
            VECTOR to = { xz[k][0], 0, xz[k][1], 0 };
            short turn = (short)turn_toward_target(&to, 0x1000);
            if (turn > -YW_SLAM_ARC && turn < YW_SLAM_ARC) front = true;
        }
    }
    int pick = 0;
    if ((nearBody >= 2 || behind) && yw_ready(s_thrashAt, YW_THRASH_MS, now)) {
        pick = 9;
        s_thrashAt = now;
    } else if (front && yw_ready(s_slamAt, YW_SLAM_MS, now) && rand() % YW_SLAM_ODDS == 0) {
        pick = 10;
        s_slamAt = now;
    }
    if (pick != 0) {
        s_specialAt = now;
        dbg_printf("[yawn] %s (%d survivors near the body)\n", pick == 9 ? "thrash" : "slam", nearBody);
    }
    return pick;
}

// ---------------------------------------------------------------------------
// A human director plays the Yawn of a fight under way: a boss room whose
// Yawn is alive with a living survivor in it. Its copy jumps there and takes
// the snake (zm_director_input -> zm_jump_to, zm_yawn_possessed) as soon as
// nothing holds it, and stays until the Yawn is beaten. An AI director's
// game leaves the snake its own AI.
// ---------------------------------------------------------------------------
#define YW_FORCE_RETRY_MS 1000
static unsigned int s_forceTryAt = 0;

static bool yw_human_director(void)
{
    return zombie_mode_armed() && zm_match_authority() && zm_game_role() != ZM_NET_SURVIVOR;
}

static int yw_fight_room(void)
{
    ZmSurvivorInfo surv[ZM_NET_MAX_PLAYERS];
    int n = zm_survivor_list(surv, ZM_NET_MAX_PLAYERS);
    for (int b = 0; b < YW_BOSSES; b++) {
        if (s_phase[b] != YW_WAITING || yw_beaten(b)) continue;
        for (int i = 0; i < n; i++) {
            if (!surv[i].dead && surv[i].stage == kBoss[b].stage && surv[i].room == kBoss[b].room) return b;
        }
    }
    return -1;
}

bool zm_yawn_fight_on(void) { return yw_fight_room() >= 0; }

bool zm_yawn_director_bound(const Entity* e)
{
    if (!yw_human_director() || e == NULL || !zm_yawn_head(e)) return false;
    int b = yw_here();
    return b >= 0 && b == yw_fight_room();
}

bool zm_yawn_force_jump(const Entity* e, unsigned char* stage, unsigned char* room)
{
    if (!yw_human_director()) return false;
    int b = yw_fight_room();
    if (b < 0 || zm_yawn_director_bound(e)) return false;
    unsigned int now = zm_game_time_ms();
    if (s_forceTryAt != 0 && now - s_forceTryAt < YW_FORCE_RETRY_MS) return false;
    s_forceTryAt = now != 0 ? now : 1;
    *stage = kBoss[b].stage;
    *room = kBoss[b].room;
    dbg_printf("[yawn] the fight in %s's room: the director takes it\n", kBoss[b].name);
    return true;
}

bool zm_yawn_free_revive(int player, unsigned char stage, unsigned char room)
{
    int b = yw_boss_at(stage, room);
    return zombie_mode_armed() && b >= 0 && player >= 0 && player < ZM_NET_MAX_PLAYERS &&
           s_phase[b] == YW_LOCKDOWN && (s_freeRevives[b] & (1u << player)) != 0;
}

void zm_yawn_revived(int player)
{
    if (player < 0 || player >= ZM_NET_MAX_PLAYERS) return;
    for (int b = 0; b < YW_BOSSES; b++) s_freeRevives[b] &= (unsigned char)~(1u << player);
}

// ---------------------------------------------------------------------------
// The guarded pickup
// ---------------------------------------------------------------------------
static void yw_run_item(unsigned char* op)
{
    unsigned char* saved = g_ScdOpcodes;
    g_ScdOpcodes = op;
    cmd_item_model_set();
    g_ScdOpcodes = saved;
}

void zm_yawn_item_before(const unsigned char* op)
{
    s_holding = false;
    int b = yw_here();
    if (!zombie_mode_armed() || b < 0 || yw_beaten(b) || s_heldCount >= YW_HELD_MAX) return;
    unsigned char id, qty;
    if (!zm_random_item(g_stageId, g_roomId, op[0x16], &id, &qty) || !yw_guarded_item(id)) return;
    if (Flg_ck((int)g_roomItemsFlags, op[0x16]) == 0) return;     // already taken
    for (int i = 0; i < s_heldCount; i++) if (s_held[i].flag == op[0x16]) return;   // a re-run
    YwHeld& h = s_held[s_heldCount++];
    memcpy(h.op, op, YW_ITEM_OP_LEN);
    h.slot = (unsigned char)(op[1] & 0x7F);
    h.model = op[0x0C];
    h.flag = op[0x16];
    memcpy(h.zone, op + 2, sizeof(h.zone));
    s_holding = true;
    dbg_printf("[yawn] %s: item %02X (flag %02X) held until it is beaten\n", kBoss[b].name,
               (unsigned)id, (unsigned)h.flag);
}

void zm_yawn_item_after(unsigned char* op)
{
    if (!s_holding) return;
    s_holding = false;
    *(unsigned short*)(op + 0x18) &= 0x7FFF;                         // no sparkle
}

static void yw_hide_held(void)
{
    for (int i = 0; i < s_heldCount; i++) {
        g_RoomActionTable[s_held[i].slot * 12] = 0;
        unsigned char* model = (unsigned char*)g_item_model_table[s_held[i].model];
        if (model != NULL) model[0] = 0;
    }
}

static bool yw_zones_meet(const short* a, const short* b)
{
    return a[0] < b[0] + (unsigned short)b[2] && b[0] < a[0] + (unsigned short)a[2] &&
           a[1] < b[1] + (unsigned short)b[3] && b[1] < a[1] + (unsigned short)a[3];
}

// ---------------------------------------------------------------------------
// The lesson room's hole down to B1 passage 1: a way into the lesson room
// round its locked door, so it stays shut until Yawn 2 there is beaten (its
// death flag). The generator counts it shut all game (ZombieRandom.cpp,
// kDoorGates). The broken floor and moved shelf are present on arrival
// (zombie_mode_room_prepare), but neither B1 passage 1's climb-up prompt
// (slot 2, event 0) nor the lesson room's way down (slot 7, event 12) starts.
// ---------------------------------------------------------------------------
bool zm_yawn_hole_open(void)
{
    return yw_beaten(1);
}

// The native slot 2 zone lies inside the fireplace/fall scenery, beyond
// the standing player's reach. Cover the accessible south/east rim too,
// and use an action-press door to the native B1 destination. No original
// fall/rope event is needed. The normal door gate still enforces regroup.
static unsigned char s_mainHoleDoor[24];
bool zm_yawn_hole_door(const unsigned char* record)
{
    if (!zombie_mode_armed() || g_stageId != STAGE_MANSION_RETURN_2F ||
        g_roomId != ROOM_LESSON_ROOM || record == NULL) return false;
    const unsigned char* basement = *(const unsigned char* const*)(&g_RoomActionTable[12 + 8]);
    return record == s_mainHoleDoor || record == basement;
}

void zm_yawn_hole_action(void)
{
    if (!zombie_mode_armed() || g_stageId != STAGE_MANSION_RETURN_2F ||
        g_roomId != ROOM_LESSON_ROOM) return;
    g_RoomActionTable[4 * 12] = 0;
    g_RoomActionTable[4 * 12 + 1] = 0;
    unsigned char* entry = &g_RoomActionTable[2 * 12];
    const unsigned char* basement = *(const unsigned char* const*)(&g_RoomActionTable[12 + 8]);
    entry[0] = 0;
    if (basement != NULL && zm_yawn_hole_open()) {
        memcpy(s_mainHoleDoor, basement, sizeof(s_mainHoleDoor));
        const unsigned short rim[4] = { 1200, 1400, 4200, 4900 };
        memcpy(s_mainHoleDoor, rim, sizeof(rim));
        entry[0] = 1;                     // door_try_enter, on Action only
        entry[1] = 0x81;
        *(unsigned short*)(entry + 2) = 1; // native basement door number
        *(const unsigned char**)(entry + 8) = s_mainHoleDoor;
    }
    g_RoomActionTable[7 * 12] = zm_yawn_hole_open() ? 9 : 0;
    g_RoomActionTable[7 * 12 + 1] = zm_yawn_hole_open() ? 0x81 : 0;
}

// Yawn 2 beaten, the lesson room's door to the front lesson room stays shut
// both ways: the way out is the hole, through the basement (B1 passage 1,
// passage 2, the kitchen), where the director can wait for them. The
// generator models it (kSealedDoors, the hole open downward).
bool zm_yawn_lesson_sealed(unsigned char stage, unsigned char room, unsigned char dest)
{
    if (!zombie_mode_armed() || stage != STAGE_MANSION_RETURN_2F || !zm_yawn_hole_open()) return false;
    unsigned char ds, dr;
    zm_decode_dest(dest, stage, &ds, &dr);
    return ds == stage && ((room == ROOM_LESSON_ROOM && dr == ROOM_FRONT_LESSON_ROOM) ||
                           (room == ROOM_FRONT_LESSON_ROOM && dr == ROOM_LESSON_ROOM));
}

bool zm_yawn_hole_shut(unsigned char stage, unsigned char room, unsigned char dest)
{
    if (!zombie_mode_armed() || stage != STAGE_MANSION_RETURN_2F || zm_yawn_hole_open()) return false;
    unsigned char ds, dr;
    zm_decode_dest(dest, stage, &ds, &dr);
    return ds == stage && ((room == ROOM_LESSON_ROOM && dr == ROOM_MANSION_B1_PASSAGE_1) ||
                           (room == ROOM_MANSION_B1_PASSAGE_1 && dr == ROOM_LESSON_ROOM));
}

bool zombie_mode_yawn_guards(const unsigned char* entry, unsigned char handler)
{
    if (entry == NULL || !zombie_mode_armed()) return false;
    // ROOM70C0 slot 3 / event 0 is the piano's snake entrance scene.
    // Its frame script re-arms it until Yawn dies, so refuse the action
    // even after init. The mode places its own Yawn and prepares the hole.
    if (handler == 9 && g_stageId == STAGE_MANSION_RETURN_2F &&
        g_roomId == ROOM_LESSON_ROOM && entry[4] == 0) return true;
    // B1 passage 1's climb-up prompt (event 0) and the lesson room's way down
    // (event 12, on slot 7 over the hole: a message, then door slot 1).
    if (handler == 9 && g_stageId == STAGE_MANSION_RETURN_2F && !zm_yawn_hole_open() &&
        ((g_roomId == ROOM_MANSION_B1_PASSAGE_1 && entry[4] == 0) ||
         (g_roomId == ROOM_LESSON_ROOM && entry[4] == 12))) {
        dbg_printf("[yawn] the lesson room's hole is shut (room %02X)\n", (unsigned)g_roomId);
        return true;
    }
    int b = yw_here();
    if (s_heldCount == 0 || b < 0 || yw_beaten(b)) return false;
    for (int i = 0; i < s_heldCount; i++) {
        const YwHeld& h = s_held[i];
        if ((handler == 4 || handler == 0x0D || handler == 0x0F) &&
            *(const unsigned short*)(entry + 6) == h.flag) return true;
        // A close-up event that ends in the pickup (ROOM7100 event 1, on the
        // item's own zone).
        const short* zone = *(const short* const*)(entry + 8);
        if (handler == 9 && zone != NULL && yw_zones_meet(zone, h.zone)) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// The fight's music and its locked doors
// ---------------------------------------------------------------------------
// The room's own music off under the fight's: whatever plays is stopped and
// marked paused, for ResumePausedSounds to bring back (as the piano does).
static void yw_hold_bgm(void)
{
    if (g_BgmSoundBank != 0 && getSndStat(g_BgmSoundBank) == 1) {
        setSndStop(g_BgmSoundBank);
        g_BgmPaused = 1;
    }
    for (int i = 0; i < 3; i++) {
        if (g_SndBank[i].handle != 0 && getSndStat(g_SndBank[i].handle) == 1) {
            setSndStop(g_SndBank[i].handle);
            g_SndBank[i].paused = 1;
        }
    }
    s_bgmHeld = true;
}

static void yw_music_stop(void)
{
    if (s_music != 0) {
        setSndStop(s_music);
        destroySndBank(s_music);
    }
    s_music = 0;
}

static void yw_music_frame(int b)
{
    if (zm_bumpers_silenced()) { yw_music_stop(); return; }   // Wesker's ending: silence
    if (!s_here) return;
    if (!yw_beaten(b)) {
        yw_hold_bgm();
        if (s_music != 0) return;
        s_music = loadSndBankFromWav(YW_MUSIC_WAV);
        if (s_music == 0) {
            dbg_printf("[yawn] %s failed to load\n", YW_MUSIC_WAV);
            s_here = false;                 // no second try every frame
            return;
        }
        pan_set(s_music, 0);
        set_volume(s_music, g_bgmDefaultVolume);
        SetSndSlot(s_music, 1);             // looped
        return;
    }
    yw_music_stop();
    // The native lesson-room track is Bgm_08 too. Resuming the paused
    // room banks here would start the boss music again after the kill.
    if (s_bgmHeld) {
        if (g_BgmSoundBank != 0) setSndStop(g_BgmSoundBank);
        g_BgmPaused = 0;
        for (int i = 0; i < 3; i++) {
            if (g_SndBank[i].handle != 0) setSndStop(g_SndBank[i].handle);
            g_SndBank[i].paused = 0;
        }
        s_bgmHeld = false;
    }
}

bool zm_yawn_traps_exit(unsigned char stage, unsigned char room)
{
    int b = yw_boss_at(stage, room);
    if (!zombie_mode_armed() || b < 0 || stage != g_stageId || room != g_roomId) return false;
    if (s_phase[b] == YW_LOCKDOWN) return true;
    if (s_phase[b] == YW_DONE) return false;
    // Waiting: the snake here and alive - or beaten, and the host's lockdown
    // on its way.
    return s_here || yw_beaten(b);
}

void zm_yawn_draw(void)
{
    int b = yw_here();
    if (!zombie_mode_armed() || b < 0 || s_phase[b] != YW_LOCKDOWN) return;
    unsigned int sec = (yw_lock_left(b) + 999) / 1000;
    char line[48];
    snprintf(line, sizeof(line), "REGROUP - DOORS OPEN IN %u", sec);
    zm_draw_centered(line, 24, 0);
}

void zm_yawn_room_reset(void)
{
    if (s_blockedDoorSound != 0) {
        setSndStop(s_blockedDoorSound);
        destroySndBank(s_blockedDoorSound);
        s_blockedDoorSound = 0;
    }
    s_boundSaved = false;
    s_boundHead = NULL;
    s_thrashAt = s_slamAt = s_specialAt = 0;
    yw_music_stop();
    s_bgmHeld = false;      // the next room loads its own music
    s_here = false;
    s_scaled = false;
    s_heldCount = 0;
    s_holding = false;
    s_bodyHave = false;
    s_bodyOrigin = -1;
    s_fledSeen = false;
}

// In with the team: from the door's arrival point (where the transition put
// it) to this survivor's place among them.
static void yw_take_place(void)
{
    int at = 0;
    short angle = (short)g_playerEntity.directionAngle;
    int x0 = g_playerEntity.scaMatrixData.localMatrix.t[0], z0 = g_playerEntity.scaMatrixData.localMatrix.t[2];
    for (unsigned int i = 0; i < sizeof(kTeamPlaces) / sizeof(kTeamPlaces[0]); i++) {
        SVECTOR off = { kTeamPlaces[i][0], 0, kTeamPlaces[i][1], 0 };
        MovePlayerXZ(angle, &off, &off);
        int x = x0 + off.x, z = z0 + off.z;
        if (i != 0 && !zm_spot_free(x, z)) continue;     // the arrival point is the door's own
        if (at++ != s_arriveRank) continue;
        g_playerEntity.scaMatrixData.localMatrix.t[0] = x;
        g_playerEntity.scaMatrixData.localMatrix.t[2] = z;
        g_playerEntity.position.x = (short)x;
        g_playerEntity.position.z = (short)z;
        dbg_printf("[yawn] in with the team: place %d at (%d, %d)\n", (int)i, x, z);
        return;
    }
    dbg_printf("[yawn] in with the team: no clear place %d, at the door\n", s_arriveRank);
}

// zombie_mode_room_spawn, after the init script and the randomizer's new spots.
void zm_yawn_room(void)
{
    int b = yw_here();
    if (zombie_mode_armed() && s_arriveBoss >= 0 && s_arriveBoss == b) yw_take_place();
    s_arriveBoss = -1;
    if (!zombie_mode_armed() || b < 0) return;
    s_fledSeen = yw_beaten(b);
    yw_spawn(b);
    yw_hide_held();
    zm_yawn_hole_action();
}

void zm_yawn_frame(void)
{
    if (!zombie_mode_armed()) return;
    yw_match_frame();
    yw_pending_frame();
    yw_vote_frame();
    yw_go_frame();
    int b = yw_here();
    if (b < 0) return;
    // Puppets receive the owner's dying/fleeing state without running the
    // original action's first step, which raises the death flag. Release
    // the guarded pickup and stop the music on those copies as well.
    const Entity* head = &g_EnemiesList[0];
    if (s_here && zm_yawn_head(head) &&
        (head->state == 3 || (kBoss[b].flees && head->state == 1 && head->action_behavior == 7)))
        Flg_on((int)g_EnemiesFlags, kBoss[b].flag);
    yw_music_frame(b);
    bool beaten = yw_beaten(b);
    zm_yawn_hole_action();
    if (beaten && !s_fledSeen) {
        s_fledSeen = true;
        dbg_printf("[yawn] %s beaten (death flag %02X)\n", kBoss[b].name, (unsigned)kBoss[b].flag);
        // The shelf is already aside; the death flag releases the hole's
        // travel guards without replaying the native shelf-moving scene.
    }
    if (s_heldCount == 0) return;
    if (!beaten) {
        yw_hide_held();
        return;
    }
    // Out now, as the room's init would have set it. Its look and texture
    // were bound when it was held, so nothing loads - but not under a menu.
    if ((g_main_state_flags & MSF_MENU_ACTIVE) != 0 || zombie_mode_pickup_waiting()) return;
    bool ext = zm_ext_begin(2);
    for (int i = 0; i < s_heldCount; i++) {
        memcpy(s_shown[i], s_held[i].op, YW_ITEM_OP_LEN);
        yw_run_item(s_shown[i]);
        dbg_printf("[yawn] %s: flag %02X item out at (%d,%d,%d), slot %d model %d\n",
                   kBoss[b].name, (unsigned)s_held[i].flag,
                   (int)*(const short*)(s_shown[i] + 0x0E),
                   (int)*(const short*)(s_shown[i] + 0x10),
                   (int)*(const short*)(s_shown[i] + 0x12),
                   (int)s_held[i].slot, (int)s_held[i].model);
    }
    if (ext) zm_ext_end();
    s_heldCount = 0;
}

// ---------------------------------------------------------------------------
// The body across copies
// ---------------------------------------------------------------------------
bool zm_yawn_body_capture(ZmYawnBody* out)
{
    const Entity* head = &g_EnemiesList[0];
    if ((head->status_flags & ENTITY_STATUS_ACTIVE) == 0 || !zm_yawn_head(head) || head->state == 0 ||
        head->jointsStructs == NULL || head->jointCount < 15) return false;
    memset(out, 0, sizeof(*out));
    const JointStruct* j = head->jointsStructs;
    for (int k = 3; k < 15; k++) {
        for (int c = 0; c < 3; c++) out->t[k - 3][c] = (short)j[k].world.t[c];
    }
    for (int s = 0; s < ZM_YAWN_SLOTS; s++) out->status[s] = g_EnemiesList[s].status_flags;
    return true;
}

void zm_yawn_body_take(const ZmYawnBody& body, unsigned char stage, unsigned char room, int origin)
{
    if (stage != g_stageId || room != g_roomId) return;
    s_body = body;
    s_bodyStage = stage;
    s_bodyRoom = room;
    s_bodyOrigin = origin;
    s_bodyHave = true;
}

static bool yw_body_from(int owner)
{
    return s_bodyHave && s_bodyOrigin == owner && s_bodyStage == g_stageId && s_bodyRoom == g_roomId;
}

// Joints 0-2 as Yawn's animator poses them (their local rotation; the
// renderer composes them onto the head), joints 3-14 straight into the world,
// as the animator places them.
bool zm_yawn_pose_apply(Entity* head, const ZmNetPose& pose, int owner)
{
    JointStruct* j = head->jointsStructs;
    if (!yw_body_from(owner) || j == NULL || head->jointCount < 15 || pose.jointCount < 15) return false;
    j[0].transform.t[0] = pose.root[0];
    j[0].transform.t[1] = pose.root[1];
    j[0].transform.t[2] = pose.root[2];
    for (int k = 0; k < 15; k++) {
        j[k].rotation.x = pose.rot[k][0];
        j[k].rotation.y = pose.rot[k][1];
        j[k].rotation.z = pose.rot[k][2];
        if (k < 3) {
            RotMatrix(&j[k].rotation, &j[k].transform);
        } else {
            RotMatrix(&j[k].rotation, &j[k].world);
            for (int c = 0; c < 3; c++) j[k].world.t[c] = s_body.t[k - 3][c];
        }
    }
    return true;
}

// The owner's status byte for this slot (shown, hidden in the hole, aimable),
// this copy's active bit kept.
void zm_yawn_status_apply(Entity* e, int slot)
{
    if (!s_bodyHave || slot < 0 || slot >= ZM_YAWN_SLOTS) return;
    e->status_flags = (unsigned char)((e->status_flags & ENTITY_STATUS_ACTIVE) |
                                      (s_body.status[slot] & ~ENTITY_STATUS_ACTIVE));
}
