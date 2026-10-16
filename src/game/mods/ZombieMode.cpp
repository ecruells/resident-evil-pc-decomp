#include "ZombieModeInternal.h"
#include "ZombieNet.h"
#include "ZombieReconnect.h"
#include "../entities/Zombie.h"
#include "../FileLoader.h"
#include "../PrintText.h"
#include "../../system/AssetPath.h"
#include "../../platform/platform.h"
#include "../../DebugPrint.h"
#include <cmath>
#include <cstdio>
#include <cstring>

// ============================================================================
// ZombieMode.cpp - port-added "play as a zombie" mod: the possessed zombie.
// See ZombieMode.h for the overview and ZombieSurvivor.cpp for the human.
// Everything here is new code built from the engine's own pieces
// (zombie_init, zombie_attack, zombie_damaged, Joint_move, Add_speedXZ,
// check_room_collision, Snd_em, door_try_enter), so there are no original
// addresses to cite except for the functions it calls.
// ============================================================================

extern void Flg_on(int baseAddr, unsigned int bitIndex);              // 0x00473ef0 CmdFunctions.cpp
extern int  is_entity_in_switch_zone(VECTOR* pos, void* zoneData);    // 0x00462d90
extern void MovePlayerXZ(int angle, SVECTOR* offset, SVECTOR* out);   // WeaponDamage.cpp
extern int  Room_SetupZombieSoundGroup(const char* const* row);       // SoundSystem.cpp (port-added)
extern void zombie_body_part_physics(unsigned char param);            // 0x00437c30 Zombie.cpp
extern const char* WeaponModelPath(int block, unsigned char weaponId); // EntityModelLoader.cpp (port-added)
extern unsigned int ProcessTmdTextures(char mode, unsigned int* tmdBase, int bank, int depth);  // 0x00483560
extern unsigned int* CreateAnimObject(int slotPtr, unsigned int* param2);                       // TmdAnimation.cpp
extern void SetSpriteBufferFlag(void);                                 // 0x00411e00
extern void ResetJointTransforms(void);                                // 0x0048bad0
extern void ProcessTmdAsync(unsigned int param1);                      // 0x004838e0
extern void LoadEntityEMD(Entity* em, unsigned char entity_id);        // 0x00462370
extern void Entity_SetJoints(Entity* em, unsigned int param2);         // 0x0048bc60
extern void InitAnimStructure(void* animHeaderValue);                  // 0x0048b6b0
extern unsigned int SetupJointStructures(unsigned int param1);         // 0x0048b9e0
extern void joint_setup_attack_effect(int joint, unsigned char effectType,
                                      unsigned short timer, unsigned short frameMatch);  // 0x0048a070
extern int  cmd_bit_op(void);                                          // 0x00460650 CmdFunctions.cpp
extern int  cmd_room_action_arm(void);                                 // 0x00461010 CmdFunctions.cpp
extern void update_zombie_action(void);                                // 0x004342f0 Zombie.cpp

bool    g_bPlayAsZombie = false;
Entity* g_zombieModeEntity = NULL;

static constexpr auto s_bossDoorClosed = STR("The door won't open.");
static constexpr auto s_bossDoorBlocked = STR("The door is blocked.");
static constexpr auto s_holeRegroup = STR("We should regroup and\\ncatch our breath.");

// Armed for the session by a new game with the key on; a continued game or an
// attract demo leaves it off, so neither ever spawns a possessed zombie.
static bool s_zombieModeArmed = false;

// The zombie being held and bitten, or NULL. Lives in this room's enemy list,
// so it is dropped whenever the room is.
static Entity* s_biteVictim = NULL;

// Set while the zombie's own action press is inside door_try_enter, so
// zombie_mode_door_begin lets that transition through.
static bool s_zombieOpeningDoor = false;

// True while the zombie's state machine belongs to the engine's zombie code
// (being hit, dying, grabbing the survivor) rather than to the pad.
static bool s_engineOwned = false;

// The pad started a naked / green zombie's vomit (zm_start_vomit); the
// zombie's own zombie_vomiting runs until it hands back.
static bool s_vomiting = false;

// Raised once when the zombie has died, so game over is requested once.
static bool s_zombieDeathReported = false;
static bool s_directorMapOnly = false;

// A room event is walking the hidden player and the zombie follows it
// (zm_try_event).
static bool s_riding = false;
// The scene last skipped (zombie_mode_skip_scd_event), reset at each room load:
// a scene asked for again the next visit gives its control back again.
static int s_lastSkippedEvent = -1;

// Which side of a two-player game this copy plays (ZM_NET_OFF in single
// player), fixed when the game starts.
static int s_gameRole = ZM_NET_OFF;

// ZM_NET_SURVIVOR only: the director's zombie, as an enemy in this room.
// Kept apart from g_zombieModeEntity so every zombie-mode hook stays inert and
// this copy plays exactly like the normal game.
static Entity* s_puppetZombie = NULL;
// Another player is in this room, and who runs its monsters (see "The shared
// room" below): the room's owner, a player index.
static bool    s_shared = false;
static int     s_owner = -1;
static bool    s_iOwn = true;
static int     s_ownerFrames = 0;              // frames since this copy took the room
static int     s_puppetFrames = 0;             // frames since another copy took it
static int     s_corpseAdoptFrames = 0;        // repeat corpse handoff while the new owner loads

// The other survivors, as NPC-model entities in fixed slots at the top of the
// enemy list (above anything a room or the roster uses): survivor n in slot
// ZM_SURVIVOR_SLOT_BASE + n (ZombieModeInternal.h). The same layout on every copy.
static bool zm_is_survivor_slot(int slot)
{
    return slot >= ZM_FIRST_SURVIVOR_SLOT && slot < 30;
}

// Characters -> their models (see "Characters and their models" below).
static unsigned char zm_char_npc_id(int ch)
{
    if (ch == ZM_CHAR_RICHARD) return 0x27;
    if (ch == ZM_CHAR_ENRICO) return 0x28;
    return (unsigned char)(0x20 + ch);
}

static int zm_char_of_npc_id(unsigned char id)
{
    if (id == 0x27) return ZM_CHAR_RICHARD;
    if (id == 0x28) return ZM_CHAR_ENRICO;
    return (id >= 0x20 && id <= 0x23) ? id - 0x20 : -1;
}

static bool zm_char_borrowed(int ch)
{
    return ch == ZM_CHAR_RICHARD || ch == ZM_CHAR_ENRICO;
}

// The EMD table index for a character's model: the player block for 0-3, the
// NPC entry (entity id + 4, as room_set loads it) for the borrowed ones.
static int zm_char_model_index(int ch)
{
    return zm_char_borrowed(ch) ? zm_char_npc_id(ch) + 4 : ch;
}


// What a survivor's stand-in holds. It wears that player's own model (char10
// Chris / char11 Jill / char12 Barry, loaded under the NPC id), so the weapon
// is that character's in-hand file - the same .EMW the player's own copy
// loads - swapped into joint 14 exactly as LoadEquippedWeaponAnimation does,
// with its textures pointed at the stand-in's own texture page.
#define ZM_WEAPON_JOINT   0x0e
#define ZM_EMW_MAX        (40 * 1024)
struct ZmStandIn {
    bool          baseSaved;            // joint 14's own (empty hand) mesh, as loaded
    int           baseSlot;
    void*         baseObject;
    unsigned char weapon;               // item id now in hand, 0 none
    unsigned char emw[ZM_EMW_MAX];      // the weapon file
    unsigned int  object[0x680];        // its animation object (g_animObjectBuffer's size)
};
static ZmStandIn s_standIns[ZM_NET_MAX_PLAYERS];

// ---------------------------------------------------------------------------
// Texture pages for the mod's models
//
// The game's texture pages and CLUT rows are handed out by two running
// counters (g_TextureBankID / g_TextureCurrentPage): the room's models from
// page 6 / row 0x0A, then its masks, objects and items after them. The mod's
// own models - survivor stand-ins, placed monsters, the director's body, a
// borrowed texture sheet - would push everything after them along, out of
// the range those later loads were written for (the room masks lose their
// texture and characters show through the furniture in front of them). So
// they load on a counter of their own in the widened range (TexturePages.h):
// pages from TEX_BANK_ROOM_EXT_FIRST, rows from TEX_ROW_EXT_FIRST, restarted
// with each room. The game's counters come back exactly as they were.
// ---------------------------------------------------------------------------
static unsigned int  s_extBank = TEX_BANK_ROOM_EXT_FIRST;
static unsigned int  s_extRow = TEX_ROW_EXT_FIRST;
static unsigned char s_extSavedBank, s_extSavedRow;
static DWORD         s_extSavedQueue;
static bool          s_extActive = false;

static void zm_ext_reset(void)
{
    s_extBank = TEX_BANK_ROOM_EXT_FIRST;
    s_extRow = TEX_ROW_EXT_FIRST;
}

static bool zm_ext_available(int pages)
{
    return s_extBank + (unsigned int)pages <= TEX_BANK_COUNT && s_extRow + 2 <= 0xFF;
}

// Around one texture load (LoadEntityEMD / ProcessTmdAsync). False: no
// pages left, nothing switched.
bool zm_ext_begin(int pages)
{
    if (s_extActive || !zm_ext_available(pages)) return false;
    s_extSavedBank = g_TextureBankID;
    s_extSavedRow = g_TextureCurrentPage;
    g_TextureBankID = (unsigned char)s_extBank;
    g_TextureCurrentPage = (unsigned char)s_extRow;
    // The death-fade tint queue (QueueTextureForProcessing) keys its CLUT
    // copy on the room's rows; a model on these rows stays out of it.
    s_extSavedQueue = DAT_00ae9f04;
    DAT_00ae9f04 = 4;
    s_extActive = true;
    return true;
}

void zm_ext_end(void)
{
    if (!s_extActive) return;
    s_extBank = g_TextureBankID;
    s_extRow = g_TextureCurrentPage;
    g_TextureBankID = s_extSavedBank;
    g_TextureCurrentPage = s_extSavedRow;
    DAT_00ae9f04 = s_extSavedQueue;
    s_extActive = false;
}

// The monster's target for its update (zm_target_begin / zm_target_end): the
// nearest survivor in the room. A remote one is written into the player entity
// for the length of the update - the engine's monsters only ever chase
// g_playerEntity - and the real player entity put back after.
static int     s_targetIdx = -1;               // player index, -1 none
static bool    s_targetSwapped = false;
static int     s_swapT[3];
static SVECTOR s_swapPos;
static unsigned short s_swapPosY;
static short   s_swapHealth;
static unsigned char s_swapAttacked;
static short   s_swapDirection;
static unsigned int s_swapAnim;            // animationId, animFrameId, action_behavior, action_state
static unsigned char s_swapStatus;         // healthStatusFlags
// What the stand-in held when the monster's update began, to tell its hit.
static short   s_probeHealth;
static unsigned int s_probeAnim;
static short   s_probeDirection;
static unsigned char s_probeStatus;
static unsigned char s_probeFlag;
// A hit just sent to a survivor: treat it as reacting until its own state says
// so (the engine's monsters only strike a player whose flag is clear).
static unsigned int s_hurtUntilMs[4];
// A Tyrant's attack is a paired sequence: keep its victim until recovery.
static signed char s_tyrantTarget[30];
static unsigned short s_tyrantReaction[30];
static unsigned int s_swapAttacker;
static unsigned char s_swapAttackAnim, s_swapFlags;
static unsigned short s_swapGrabX, s_swapGrabZ;
// Pointers into this room's model storage. Shared instances of a creature
// reuse the same EMD, so its type identifies the reaction set.
static unsigned int s_damageHeader[NPC_ENTITIES_IDS];
static unsigned int s_damageBase[NPC_ENTITIES_IDS];
static int s_tyrantVictimSlot = -1;
static int s_tyrantVictimX, s_tyrantVictimZ;
static short s_tyrantPending[8];
static bool s_tyrantPendingHave = false;
static unsigned int s_tyrantPendingMs;
struct ZmTyrantTrailCue {
    unsigned short uid, frames;
    unsigned int receivedMs;
};
static ZmTyrantTrailCue s_tyrantTrailCues[30];
// Mod-only state (no original address): shared across possession and room changes.
static unsigned int s_hunterLeapMs = 0;
static bool s_hunterLeapUsed = false;
static bool s_hunterAimWasDown = false; // Mod-only input latch, no original address.
// Mod-only state, no original addresses: the last room this survivor left.
static int s_survivorPreviousStage = -1;
static int s_survivorPreviousRoom = -1;
static unsigned char s_survivorEntranceDoor = 0xFF;

// "ZOMBIE n OF m" in the top-right corner for a couple of seconds after a
// possession switch: n counts the room's living zombies in slot order, the
// order the switch walks them in.
#define ZM_SWITCH_NOTE_FRAMES 60
static int s_switchNoteFrames = 0;
// A jump from the director's map: the room is loading, then this zombie of
// it is taken (or the director's own body stands in if the script did not
// place it). Its own init has not run yet when it is taken.
// The start of a game: the director's zombie stands just inside the
// mansion's back exit - the storeroom by the courtyard (ROOM1B, the one door
// out of the mansion: its door_set at x 8800-10500, z 4300-7500 leads to the
// courtyard), where the courtyard's own door lets a character in: (8200, 0,
// 5900) facing 2048, read off ROOM3000's door_set back. The survivors start in
// the main hall (zm_survivor_start_as_player); getting out through this door
// is to be their goal.
#define ZM_BACK_EXIT_ROOM    ROOM_STOREROOM
#define ZM_BACK_EXIT_X       8200
#define ZM_BACK_EXIT_Z       5900
#define ZM_BACK_EXIT_ANGLE   2048

// The game's first body: its camera is set to it (zombie_mode_room_spawn).
static bool s_firstBody = false;
static bool s_jumpPending = false;
static bool s_jumpFreshInit = false;
static bool s_jumpFixCamera = false;
static ZmJumpTarget s_jumpTarget;
static unsigned char s_jumpRecord[0x18];
static void zm_start(Entity* e, unsigned char action, unsigned char anim);
static void zm_note_switch(const Entity* now);
static void zm_monster_possess(Entity* e);
static bool zm_possessable(const Entity* e);
static bool zm_monster_attack_over(const Entity* e);
static bool zm_possession_busy(const Entity* e);
static void zm_puppet_awareness(Entity* e, bool dead);
// Fresh mod spawns cannot run a decision brain before possession or the
// doorway cue arrives. Start the full idle second after initialization.
static bool s_spawnIdlePending[30];
static unsigned int s_spawnIdleUntil[30];
static bool s_directorAwaitingOwner = false;
static int s_switchNoteIndex = 0;
static int s_switchNoteCount = 0;
// Or a line of its own in the same corner (the director's placements).
static char s_noteText[48] = "";

// The director's copy: the body it spawned in this room (it stays this
// room's monster, as the survivors' copies' puppet zombie, when the director
// switches to another).
static Entity* s_bodyEntity = NULL;

// The grab across the link. The zombie's copy asks (ZM_EV_GRAB) and hands its
// zombie to the survivor's copy, which runs the real zombie_attack on its
// puppet - where the survivor is real, so the struggle, the bites and the
// release are all the original's - and sends the zombie's pose back with
// every STATE until it ends (ZM_EV_GRAB_END).
static bool         s_remoteGrab = false;        // zombie's copy: waiting / being driven
static int          s_remoteGrabVictim = -1;     // ...by that survivor's copy
static unsigned int s_remoteGrabMs = 0;
static bool         s_remoteGrabSeen = false;    // an override pose has arrived
static unsigned int s_remoteGrabLastMs = 0;      // ...most recently at
#define ZM_REMOTE_GRAB_WAIT_MS   6000            // no answer: take control back (the
                                                 // survivor may first be pulled out
                                                 // of its menu, which fades)
#define ZM_REMOTE_GRAB_GAP_MS    2000            // answers stopped: take control back
#define ZM_PUPPET_GRAB_MAX       900             // frames; a stuck grab lets go

// Survivor's copy: a grab that arrived while the survivor had its menu open.
// The shared world does not stop for a menu, so the survivor is pulled out of
// it (START pressed for it until the menu closes - main_menu's state 1 closes
// on START from any submenu) and the grab goes ahead once play resumes.
static bool s_forceMenuClose = false;
static int  s_forceMenuFrames = 0;

// status_flags bit 1: ResolveEntityScaCollision skips the pair (a hidden
// puppet must not shove anything).
#define ZM_STATUS_NO_PAIR 0x02

int zm_game_role(void)
{
    return s_gameRole;
}

bool zm_match_authority(void)
{
    return s_gameRole != ZM_NET_SURVIVOR || zm_net_role() == ZM_NET_ZOMBIE;
}

// This copy's own survivor as a STATE would carry it (the host's arbitration
// reads it like any other seat's).
static ZmNetPeerState s_selfState;

const ZmNetPeerState* zm_seat_state(int player)
{
    if (player < 0 || player >= ZM_NET_MAX_PLAYERS) return NULL;
    if (player != zm_net_self()) return zm_net_player(player);
    if (!s_zombieModeArmed || s_gameRole != ZM_NET_SURVIVOR || zm_net_char(player) < 0) return NULL;
    ZmNetPeerState& p = s_selfState;
    memset(&p, 0, sizeof(p));
    p.valid = true;
    p.dead = g_playerEntity.health < 0;
    p.spectating = zm_spec_corpse() != NULL;
    p.stage = g_stageId;
    p.room = g_roomId;
    p.x = g_playerEntity.scaMatrixData.localMatrix.t[0];
    p.y = g_playerEntity.scaMatrixData.localMatrix.t[1];
    p.z = g_playerEntity.scaMatrixData.localMatrix.t[2];
    p.angle = g_playerEntity.directionAngle;
    if (const ZmNetPeerState* corpse = zm_spec_corpse()) {
        p.stage = corpse->stage; p.room = corpse->room;
        p.x = corpse->x; p.y = corpse->y; p.z = corpse->z; p.angle = corpse->angle;
    }
    p.viewStage = g_stageId;
    p.viewRoom = g_roomId;
    p.health = g_playerEntity.health;
    p.weapon = g_playerEntity.equippedWeaponId;
    p.attacked = g_playerEntity.isBeingAttackedFlag != 0;
    p.entranceDoor = s_survivorEntranceDoor;
    p.receivedMs = zm_game_time_ms();
    return &p;
}

// action_behavior values while the pad drives the zombie. The engine's own
// values take over while s_engineOwned is set.
enum {
    ZM_ACT_IDLE   = 0,
    ZM_ACT_WALK   = 1,
    ZM_ACT_BACK   = 2,
    ZM_ACT_ATTACK = 3,
    // On the floor. Everything from LIE_DOWN to GET_UP counts as prone.
    ZM_ACT_LIE_DOWN     = 4,
    ZM_ACT_PRONE        = 5,
    ZM_ACT_CRAWL        = 6,
    ZM_ACT_PRONE_ATTACK = 7,
    ZM_ACT_GET_UP       = 8,
    // Stepping through an in-room step (check_door / check_door_side zones).
    ZM_ACT_STEP         = 9,
    // Following the hidden player through a room event (zm_try_event).
    ZM_ACT_RIDE         = 10,
};

// Zombie EMD animation ids (Zombie.cpp): 0 idle, 1 slow walk, 3 arms-out chase
// walk, 0x11 the standing grab (zombie_attack_anim_tbl dir 0).
#define ZM_ANIM_IDLE     0x00
#define ZM_ANIM_SHAMBLE  0x01
#define ZM_ANIM_CHASE    0x03
#define ZM_ANIM_GRAB     0x11
#define ZM_ANIM_BITE     0x12     // zombie_attack case 2: the grab anim + 1
// Prone set: zombie_falldown plays 0x08 forward to go down and backward to get
// up; zombie_pushback_idle (0x09) and zombie_pushback_stagger (0x0E) are the
// lying idle and the crawl; 0x17 is the laying-front grab
// (zombie_attack_anim_tbl dir 2).
#define ZM_ANIM_FALL       0x08
#define ZM_ANIM_PRONE      0x09
#define ZM_ANIM_CRAWL      0x0E
#define ZM_ANIM_PRONE_GRAB 0x17

#define ZM_TURN_IDLE     0x28     // per frame; the player turns at 0x30
#define ZM_TURN_WALK     0x20
#define ZM_SPEED_WALK    45       // zombie_chase_walk's speed
#define ZM_SPEED_RUN     90       // two cycles per frame, as the DC fast zombie
#define ZM_SPEED_BACK    18
#define ZM_SPEED_FALL    20       // zombie_falldown's drift while going down
#define ZM_TURN_CRAWL    0x10
#define ZM_PRONE_HIT_FRAME 10     // frame of the floor grab on which the hit is tested

// Ends of a prone body along its own X axis, for the two-point room collision
// zombie_update runs on a laying zombie.
static SVECTOR s_bodyEndFront = { 600, 0, 0, 0 };
static SVECTOR s_bodyEndBack  = { -600, 0, 0, 0 };

#define ZM_ATTACK_LUNGE_FRAMES  12   // frames of forward lunge at the start of the grab
#define ZM_ATTACK_LUNGE_SPEED   30
#define ZM_ATTACK_HIT_FRAME     14   // frame of the grab on which the hit is tested
#define ZM_ATTACK_MAX_FRAMES    75   // fallback end if the grab never reports a loop
#define ZM_ATTACK_REACH         700  // added to both collision radii
#define ZM_ATTACK_HALF_ARC      0x200 // +/-45 degrees
#define ZM_ATTACK_DAMAGE        30   // a single swipe at a non-zombie
// Grabbing the survivor: zombie_chase_player hands over to zombie_attack
// inside checkAngularViewAndDistance(700, 1500); the pad version is a little
// more forgiving on the arc.
#define ZM_GRAB_SURVIVOR_RANGE  1500
#define ZM_GRAB_SURVIVOR_ARC    0x180

// Holding a zombie and biting it, after zombie_attack cases 2-3: a bite on
// the first frame and every 19 after, for at most ZM_BITE_FRAMES.
#define ZM_BITE_INTERVAL        19
#define ZM_BITE_DAMAGE          15
#define ZM_BITE_FRAMES          80
#define ZM_BITE_HOLD_DIST       600  // victim origin, in front of the biter

// A tough zombie, but one the survivor can put down: about twenty handgun hits
// (zombie_health_tbl tops out at 99).
#define ZM_HEALTH               160

// collisionFlags (+0xDC) bit 0x10: check_room_collision skips type-5 boundary
// records (RDT_Boundary, Types.h). That is how rooms keep their zombies off
// areas the player can walk - the player's byte at +0xDC is healthStatusFlags,
// which carries 0x10 from the new game on. 2F Right Stairs (ROOM2070) fences
// its stairs and lower landing that way; without the bit the possessed zombie
// is pushed out of the landing every frame and cannot walk on it.
#define ZM_COLLISION_LIKE_PLAYER  0x10

// The tea room's (ROOM1040) zombie sound row: Snd_em(1)/(2) footsteps,
// (3) bite, (4) roar. Room_SetupZombieSoundGroup finds a group for it.
static const char* const s_zombieSoundRow[10] = {
    "z_taore", "z_ftL", "z_ftR", "z_kamu", "z_mika02", "z_mika01",
    "z_head", "z_Hkick", "z_Ugoron", "z_mika03",
};

// The zombie's health and prone state outlive the room: it is the same zombie
// in the next one.
static short s_carriedHealth = ZM_HEALTH;
static bool  s_carriedProne = false;
// ...and so does its type: a possessed Cerberus, Hunter or Chimera that walks
// through a door arrives as itself (zombie_mode_room_spawn), with its own
// behaviour byte. Its type's init picks a fresh health on the first update;
// s_carriedPending puts the carried one back over it.
static unsigned char s_carriedId = ENEMY_ZOMBIE;
static unsigned char s_carriedBehavior = 0;
static bool          s_carriedPending = false;

// The match is over (ZM_END_*): a survivor got out through the mansion's back
// exit (zombie_mode_door_begin on its copy), the clock ran out or every
// survivor is dead (zm_clock_frame on the director's copy); ZM_EV_WIN tells
// the others. Every copy fades to white and shows the result and the match's
// stats (ZombieStats.cpp) until its player presses Action (or a minute
// passes); then the game ends through the death fade, without the death
// screen (zombie_mode_match_over).
#define ZM_END_LEAVE_MS 60000
static bool s_winShown = false;
static bool s_escapePending = false; // exit attempted; only the host may confirm it
static unsigned int s_finalElapsedMs = 0;
static unsigned int s_winAtMs = 0;
static bool s_endLeaving = false;
static int  s_winPlayer = -1;      // who opened the door
static int  s_winReason = ZM_END_ESCAPE;

// The game clock: the survivors have ZM_TIME_LIMIT_MS from the moment they
// are all in (zm_survivors_in_ms) to get out, plus what beating a boss adds
// (zm_clock_add_bonus); at zero the director wins. It stands still while a
// boss fight holds it (zm_yawn_clock_hold). The director's copy (and single
// player) keeps it and sends ZM_EV_CLOCK every ZM_CLOCK_SEND_MS and on every
// hold or bonus; a survivor's copy counts down from the last one.
#define ZM_TIME_LIMIT_MS (12 * 60000)
#define ZM_TIME_MAX_MS   (ZM_TIME_LIMIT_MS + 2 * 5 * 60000)    // with both bosses' time
#define ZM_CLOCK_SEND_MS 5000
static unsigned int s_clockLeftMs = ZM_TIME_LIMIT_MS;   // a survivor's: the last ZM_EV_CLOCK
static unsigned int s_clockRunMs = 0;                   // ...the time it had run
static bool         s_clockRemoteHeld = false;          // ...and standing still
static unsigned int s_clockAtMs = 0;                    // ...and when it came
static bool         s_clockHave = false;
static unsigned int s_clockSentMs = 0;
static unsigned int s_clockBonusMs = 0;                 // the keeper's: time added
static unsigned int s_clockHeldMs = 0;                  // ...time stood still, holds over
static unsigned int s_clockHeldAt = 0;                  // ...the current hold's start
static bool         s_clockHeld = false;

// The director's points for a hit (ZombieEconomy.cpp): the survivor in the
// player entity, before the possessed monster's update and after it. A
// zombie's grab of a remote survivor counts when that copy takes it
// (zm_remote_grab_frame).
static bool          s_hitProbe = false;
static short         s_hitProbeHealth = 0;
static unsigned char s_hitProbeFlag = 0;
// The last credit for each monster's hit (zm_monster_credit).
#define ZM_CREDIT_GAP_MS 2000
static unsigned int  s_creditMs[30];
static void zm_hit_probe_begin(const Entity* e);
static void zm_hit_probe_end(const Entity* e);

// The main hall is the survivors' to start in: the director may not place a
// monster there, jump there or walk in until ZM_HALL_GRACE_MS after every
// survivor has come into the game (they choose their characters for up to
// two minutes while the director already plays). A survivor that never comes
// in stops counting after ZM_SPAWN_WAIT_MS.
#define ZM_HALL_GRACE_MS  60000
#define ZM_SPAWN_WAIT_MS  150000
static unsigned int s_gameStartMs = 0;
static unsigned int s_allSpawnedMs = 0;

// The main hall and its 2F gallery: both closed to the director together.
static bool zm_is_hall(unsigned char stage, unsigned char room)
{
    return (stage % 5 == STAGE_MANSION_1F && room == ROOM_MAIN_HALL) ||
           (stage % 5 == STAGE_MANSION_2F && room == ROOM_MAIN_HALL_2F);
}

// The 2F main hall's stairs trip up the dogs and the other non-walkers: only
// zombies and Hunters may be placed there or walked in.
static bool zm_hall_2f_refuses(unsigned char stage, unsigned char room, unsigned char id)
{
    if (stage % 5 != STAGE_MANSION_2F || room != ROOM_MAIN_HALL_2F) return false;
    return !zm_is_zombie_id(id) && id != 0x06;
}

// Is the hall still closed to the director? *leftMs: how long (0xFFFFFFFF
// while survivors are still choosing).
static bool zm_hall_closed(unsigned int* leftMs)
{
    if (zm_net_role() != ZM_NET_ZOMBIE || s_gameRole == ZM_NET_OFF) return false;
    unsigned int now = zm_game_time_ms();
    if (s_allSpawnedMs == 0) {
        bool all = true;
        int seated = 0;
        for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
            if (zm_net_char(i) < 0) continue;
            seated++;
            const ZmNetPeerState* p = zm_seat_state(i);
            if (p == NULL || !p->valid) all = false;
        }
        // In once their intro has released them too: the director has been
        // playing since GO, the survivors only start at the barrier.
        if ((all && seated > 0 && zm_net_bumper_released()) || now - s_gameStartMs > ZM_SPAWN_WAIT_MS) {
            s_allSpawnedMs = now;
            dbg_printf("[zombie] all survivors are in; the hall opens in %d s\n", ZM_HALL_GRACE_MS / 1000);
        }
    }
#ifdef QUICK_DEBUG
    // Keep tracking survivor arrival for the match clock and economy, but
    // allow immediate hall access and placement while testing.
    if (leftMs != NULL) *leftMs = 0;
    return false;
#else
    if (s_allSpawnedMs == 0) {
        if (leftMs != NULL) *leftMs = 0xFFFFFFFFu;
        return true;
    }
    unsigned int open = s_allSpawnedMs + ZM_HALL_GRACE_MS;
    if ((int)(open - now) <= 0) return false;
    if (leftMs != NULL) *leftMs = open - now;
    return true;
#endif
}

int zm_survivors_in_ms(void)
{
    if (s_gameRole == ZM_NET_OFF) return (int)(zm_game_time_ms() - s_gameStartMs);
    zm_hall_closed(NULL);       // notes when they are all in
    if (s_allSpawnedMs == 0) return -1;
    return (int)(zm_game_time_ms() - s_allSpawnedMs);
}

void zm_note(const char* text);

// Safe rooms (an item box, ZombieRandom.cpp) and the elevator car are closed
// to the director for the whole game: no entering, no placing.
static bool zm_director_safe_room(unsigned char stage, unsigned char room)
{
    return s_zombieModeArmed && zm_match_authority() &&
        (zm_access_room_elevator(stage, room) || zm_random_room_safe(stage, room) || zm_shotgun_room_blocked(stage, room) ||
         zm_yawn_director_closed(stage, room));     // a boss room until it is done (ZombieYawn.cpp)
}

// A living survivor stands in (stage, room): the director may not place a
// monster there (traps still may).
static bool zm_room_has_survivor(unsigned char stage, unsigned char room)
{
    ZmSurvivorInfo surv[ZM_NET_MAX_PLAYERS];
    int n = zm_survivor_list(surv, ZM_NET_MAX_PLAYERS);
    for (int i = 0; i < n; i++) {
        if (!surv[i].dead && surv[i].stage == stage && surv[i].room == room) return true;
    }
    return false;
}

// The note for a refused hall action.
static void zm_note_hall_closed(void)
{
    unsigned int left = 0;
    zm_hall_closed(&left);
    char line[48];
    if (left == 0xFFFFFFFFu) snprintf(line, sizeof(line), "THE HALL IS CLOSED - SURVIVORS ARRIVING");
    else snprintf(line, sizeof(line), "THE HALL OPENS IN %u S", (left + 999) / 1000);
    zm_note(line);
}

// check_room_collision (0x0047d310) rolls a stuck entity back to `position`
// (+0x6C), the last spot it accepted, and only advances that point when it
// runs. Anything that moves an entity without it - the step, a held victim, a
// re-placement after a door - has to move the rollback point too, or the next
// bump snaps the entity back there (keeping only the new height).
static void zm_set_rollback(Entity* e)
{
    e->position.x = (short)e->scaMatrixData.localMatrix.t[0];
    e->position.y = (short)e->scaMatrixData.localMatrix.t[1];
    e->position.z = (short)e->scaMatrixData.localMatrix.t[2];
}

bool zm_is_zombie_id(unsigned char id)
{
    return id == ENEMY_ZOMBIE || id == ENEMY_ZOMBIE_NAKED || id == ENEMY_ZOMBIE_VARIANT;
}

// A living zombie temporarily borrows DEAD as the bite's busy bit. It is
// not a corpse in packets, roster captures or a room-ownership transfer.
bool zm_monster_dead(const Entity* e)
{
    if (e->health < 0) return true;
    return (e->status_flags & ENTITY_STATUS_DEAD) != 0 &&
        !(zm_is_zombie_id(e->id) && e->state == ZOMBIE_STATE_ATTACK);
}

bool zm_zombie_alive(void)
{
    Entity* e = g_zombieModeEntity;
    return e != NULL && e->health >= 0 && (e->status_flags & ENTITY_STATUS_DEAD) == 0;
}

// ===========================================================================
// Session / room hooks
// ===========================================================================
// The door stun (with the shared-room state below).
static void zm_stun_new_game(void);
static void zm_stun_after_update(void);

void zombie_mode_new_game(int isNewGame)
{
    zm_bumpers_reset();
    s_zombieModeArmed = isNewGame && g_bPlayAsZombie;
    s_hunterLeapUsed = false;
    s_hunterAimWasDown = false;
    s_survivorPreviousStage = -1;
    s_survivorPreviousRoom = -1;
    s_survivorEntranceDoor = 0xFF;
    g_zombieModeEntity = NULL;
    s_carriedHealth = ZM_HEALTH;
    s_carriedProne = false;
    s_carriedId = ENEMY_ZOMBIE;
    s_carriedBehavior = 0;
    s_carriedPending = false;
    s_winShown = false;
    s_escapePending = false;
    s_finalElapsedMs = 0;
    s_winAtMs = 0;
    s_endLeaving = false;
    s_winPlayer = -1;
    s_winReason = ZM_END_ESCAPE;
    zm_stats_new_game();
    s_clockLeftMs = ZM_TIME_LIMIT_MS;
    s_clockRunMs = 0;
    s_clockRemoteHeld = false;
    s_clockAtMs = 0;
    s_clockHave = false;
    s_clockSentMs = 0;
    s_clockBonusMs = 0;
    s_clockHeldMs = 0;
    s_clockHeldAt = 0;
    s_clockHeld = false;
    zm_yawn_new_game();
    s_gameStartMs = zm_game_time_ms();
    s_allSpawnedMs = 0;
    s_zombieDeathReported = false;
    s_directorMapOnly = false;
    s_gameRole = ZM_NET_OFF;
    s_jumpPending = false;
    s_jumpFreshInit = false;
    s_jumpFixCamera = false;
    s_firstBody = true;
    s_hitProbe = false;
    memset(s_creditMs, 0, sizeof(s_creditMs));
    zm_econ_new_game(false);
    zm_trap_new_game();
    zm_reinforce_reset();
    zm_stun_new_game();
    zm_spec_new_game();
    zm_shotgun_reset();
    zm_piano_reset();
    zm_voice_reset();
    zm_roomsync_reset();
    zm_world_story_reset();
    if (g_bPlayAsZombie && zm_net_active() && zm_net_role() == ZM_NET_SURVIVOR) {
        // A survivor's roster was started when it joined (zm_net_join): it
        // holds what the director placed while this survivor chose its
        // character, which a reset here would throw away. That includes
        // InitializeGame's first, disarming call (isNewGame 0) - which wiped
        // it when only the armed call was spared.
        zm_world_set_on(true);
    } else {
        zm_world_reset(s_zombieModeArmed);
    }
    if (!s_zombieModeArmed) {
        return;
    }
    // The item box starts empty (bio_card.dat fills it) and is shared.
    zm_world_box_reset();

    // Multiplayer: meet the other copies at the start line (the lobby already
    // settled who plays whom; everyone runs Chris's scenario).
    if (zm_net_active()) {
        int role = zm_net_role();
        if (zm_net_start_game(60000)) {
            // An AI director's host plays a survivor itself (seat 0) and still
            // arbitrates (zm_match_authority).
            s_gameRole = (role == ZM_NET_ZOMBIE && zm_net_ai_level() > 0) ? ZM_NET_SURVIVOR : role;
        } else {
            zm_net_stop();      // nobody came: single player
        }
    }
    // The director's points: its copy, or single player's.
    zm_econ_new_game(zm_match_authority() && (s_gameRole != ZM_NET_SURVIVOR || zm_ai_hosted()));
    zm_ai_new_game();

    // ROOM1060's init SCD starts an intro event on each of these:
    //   bank 1 bit 0 clear            -> opening cutscene (event 0)
    //   bit 0 set,   bit 2 clear      -> Wesker / partner scene (event 8)
    //   bit 2 set,   bit 3 clear      -> event 9
    // Marking them seen gives a quiet main hall to start in.
    Flg_on((int)g_ScenarioFlags2, 0x00);
    Flg_on((int)g_ScenarioFlags2, 0x02);
    Flg_on((int)g_ScenarioFlags2, 0x03);

    // The mode plays the return mansion (stages 6/7): every mansion room is
    // built there - the first visit carries 14 of its rooms (the basement
    // among them) as stub RDTs. The scenario's stage-variant bit is what
    // room_transition_load adds 5 to a mansion door's stage on.
    Flg_on((int)&g_ScenarioFlags, SCENARIO_FLAG_STAGE_VARIANT);
    zm_random_new_game();
    zm_drops_new_game();
    zm_pickups_reset();
    zm_box_reset();
    zm_timeout_reset();

    if (s_gameRole == ZM_NET_SURVIVOR) {
        zm_survivor_start_as_player();
        zm_reconnect_new_game();
    } else {
        zm_survivor_new_game();
        // The director (and single player's zombie) starts at the back exit;
        // the body spawns where the player entity is (zombie_mode_room_spawn).
        g_stageId = STAGE_MANSION_RETURN_1F;
        g_roomId = ZM_BACK_EXIT_ROOM;
        g_playerEntity.position.x = ZM_BACK_EXIT_X;
        g_playerEntity.position.z = ZM_BACK_EXIT_Z;
        g_playerEntity.directionAngle = ZM_BACK_EXIT_ANGLE;
    }
    dbg_printf("[zombie] play-as-zombie armed (role %d)\n", s_gameRole);
}

static void zm_shared_room_reset(void);   // with the shared-room state below
static void zm_slot_map_reset(void);

void zombie_mode_room_reset(void)
{
    memset(s_damageHeader, 0, sizeof(s_damageHeader));
    memset(s_damageBase, 0, sizeof(s_damageBase));
    if (s_zombieModeArmed) {
        g_playerEntity.emdScratchPtr1 = 0;
        g_playerEntity.emdScratchPtr2 = 0;
    }
    s_survivorEntranceDoor = 0xFF;
    zm_world_room_reset();
    // A grab cannot outlive the room on either side.
    zm_shared_room_reset();
    s_remoteGrab = false;
    s_puppetZombie = NULL;
    s_bodyEntity = NULL;
    s_riding = false;
    g_zombieModeEntity = NULL;
    s_biteVictim = NULL;
    s_engineOwned = false;
    s_vomiting = false;
    // A death that sent the director to another room's monster is over: the
    // monster taken there can die in its turn.
    s_zombieDeathReported = false;
    s_lastSkippedEvent = -1;
    zm_random_room_reset();
    zm_statue_room_reset();
    zm_yawn_room_reset();
    zm_drops_room_reset();
    zm_econ_room_reset();
}

// The ten sound names (Room_LoadEnemySoundBanks' row) a monster type's cues
// play from: group 0 of a room the original places that type in, read off
// g_RoomSoundNameTable (stage * 29 + room) - Cerberus from the dining room
// corridor, the Hunter from the mansion return's main hall and so on, mined
// from the shipped RDTs' enemy_set records. Zombies (white and green coat)
// share the tea room's row. NULL: no known row (the type stays silent).
static const char* const* zm_monster_sound_row(unsigned char id)
{
    struct Source { unsigned char id, stage, room; };
    static const Source kSources[] = {
        { 0x01, 4, 0x05 },     // naked zombie   - lab              (z_taore, zep_ftL, ...)
        { 0x02, 0, 0x08 },     // Cerberus       - mansion 1F       (cer_foot, cer_bite, ...)
        { 0x03, 3, 0x04 },     // Web Spinner    - guardhouse       (kuasi_A, sp_atck, ...)
        { 0x05, 0, 0x17 },     // crow           - mansion 1F       (RVcar1, RVwing1, ...)
        { 0x06, 5, 0x01 },     // Hunter         - mansion return   (HU_walkA, HU_att, ...)
        { 0x07, 3, 0x08 },     // wasp           - guardhouse       (bee4_ed, hatinage, ...)
        { 0x09, 4, 0x0F },     // Chimera        - lab              (FL_walk, FL_slash, ...)
        { 0x0A, 2, 0x0D },     // Adder          - courtyard        (PYe_mena, PYe_hit, ...)
        { 0x10, 4, 0x13 },     // rooftop Tyrant - same lab sound bank (TY_foot, TY_slice, ...)
        { 0x0D, 1, 0x10 },     // Yawn 1         - the first visit's attic (GV_move, GV_bite, ...)
    };
    if (id == ENEMY_ZOMBIE || id == ENEMY_ZOMBIE_VARIANT) return s_zombieSoundRow;
    for (unsigned int i = 0; i < sizeof(kSources) / sizeof(kSources[0]); i++) {
        if (kSources[i].id != id) continue;
        unsigned int index = kSources[i].stage * 29u + kSources[i].room;
        if (index >= sizeof(g_RoomSoundNameTable) / sizeof(g_RoomSoundNameTable[0])) return NULL;
        const char** names = g_RoomSoundNameTable[index];
        return (names != NULL && names[0] != NULL) ? names : NULL;
    }
    return NULL;
}

// A monster of any type into `slot`, mirroring cmd_enemy_set (0x004617d0) for
// a script record of: this type and behaviour, no death flag, unconditional,
// 2 SCA cells. Cleared first: the slot can still hold an earlier room's enemy,
// and cmd_enemy_set's own fields are not everything a type's init reads. Its
// state 0 (the type's own init) runs on its first update; room_set's model
// loop, after zombie_mode_room_spawn, loads its EMD like a scripted enemy's.
Entity* zm_spawn_monster(int slot, unsigned char id, unsigned char behavior,
                         int x, int y, int z, short angle)
{
    if (slot < 0 || slot >= 30 || (g_EnemiesList[slot].status_flags & ENTITY_STATUS_ACTIVE) != 0) {
        return NULL;
    }
    // A monster's cries and steps come from one of the room's four enemy
    // sound groups (Snd_em reads the group from +0x161). The row its type
    // needs is found in the room or loaded into a free group.
    int soundGroup = 0;
    const char* const* row = zm_monster_sound_row(id);
    if (row != NULL) {
        soundGroup = Room_SetupZombieSoundGroup(row);
        if (soundGroup < 0) {
            dbg_printf("[zombie] no free sound group for id %d in stage %d room %d\n",
                       (int)id, (int)g_stageId, (int)g_roomId);
            soundGroup = 0;
        }
    }

    Entity* em = &g_EnemiesList[slot];
    memset(em, 0, sizeof(Entity));
    em->status_flags = ENTITY_STATUS_ACTIVE;
    em->id = id;
    em->behavior_flags = behavior;
    em->death_event_id = 0xFF;
    // +0x161: slot in the low nibble, the sound group in bits 4-6, and bit 7 =
    // "spawned unconditionally", which BuildEnemySnap never snapshots.
    em->pad_160[1] = (unsigned char)(0x80 | ((soundGroup & 7) << 4) | (slot & 0x0F));
    em->scaMatrixData.localMatrix.t[0] = x;
    em->scaMatrixData.localMatrix.t[1] = y;
    em->scaMatrixData.localMatrix.t[2] = z;
    em->position.x = (short)x;
    em->position.y = (short)y;
    em->position.z = (short)z;
    em->angle = angle;
    em->animationId = 0;
    em->animation_frame_id = 0;
    em->timing_control = 1;
    em->state = 0;
    em->Sca_info = g_scaDataTable[0];      // the type's init replaces it
    em->pSca_hit_data = g_scaPoolPtr;
    g_scaPoolPtr += 2 * 6;
    g_enemy_count++;
    s_spawnIdlePending[slot] = zm_is_possessable_id(id);
    s_spawnIdleUntil[slot] = 0;
    zm_world_note_spawn(slot);
    return em;
}

// The other survivors of a multiplayer game, each as its character's NPC
// model (em1020 Chris, em1021 Jill, em1022 Barry: entity ids 0x20-0x22, the
// same 15-joint skeleton as the player models) in its fixed slot, hidden until
// its player is in this room. Spawned before room_set's model loop, which
// loads their EMDs; their own (NPC) update never runs - zombie_mode_puppet_update
// poses them from the network.
static Entity* zm_standin_spawn(int who, int ch)
{
    Entity* e = zm_spawn_monster(ZM_SURVIVOR_SLOT_BASE + who, zm_char_npc_id(ch), 0, 0, 0, 0, 0);
    if (e == NULL) return NULL;
    e->state = ZOMBIE_STATE_IDLE;            // no NPC init: posed, not run
    e->status_flags |= ZM_STATUS_NO_PAIR;
    // The NPC inits' shadow quad (char_init_37's size), which
    // zm_draw_shadow draws from.
    SVECTOR origin = { 0, 0, 0, 0 };
    FUN_004565f0(&origin, (SVECTOR*)&e->pushVelocity, 500, 700);
    return e;
}

static void zm_spawn_survivor_puppets(void)
{
    if (s_gameRole == ZM_NET_OFF) return;
    for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
        int ch = zm_net_char(i);
        if (ch < 0 || i == zm_net_self()) continue;
        int slot = ZM_SURVIVOR_SLOT_BASE + i;
        if ((g_EnemiesList[slot].status_flags & ENTITY_STATUS_ACTIVE) != 0) {
            dbg_printf("[zombie] survivor %d's slot %d is taken\n", i, slot);
            continue;
        }
        zm_standin_spawn(i, ch);
    }
}

// Once a frame: a stand-in wears the character its player had when this room
// was loaded. The picks stay open for the character select's two minutes
// while the director already plays (and a seat starts out on the first free
// character, Chris), so a room the director loaded in that time showed the
// old pick for as long as it stayed there. A changed pick - or a survivor
// seated after the load - gets its model now, loaded the way room_set's model
// loop loads a stand-in. Each load takes fresh room memory and two texture
// banks (both come back with the next room); a load that does not fit is
// tried again only for a different pick.
static int s_standInFailedChar[ZM_NET_MAX_PLAYERS] = { -1, -1, -1, -1 };

static void zm_standin_refresh(void)
{
    if (s_gameRole == ZM_NET_OFF) return;
    for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
        int ch = zm_net_char(i);
        if (ch < 0 || i == zm_net_self()) continue;
        Entity* e = &g_EnemiesList[ZM_SURVIVOR_SLOT_BASE + i];
        bool active = (e->status_flags & ENTITY_STATUS_ACTIVE) != 0;
        if (active && e->id < NPC_ENTITIES_IDS) continue;          // not a stand-in's
        if (active && e->id == zm_char_npc_id(ch)) continue;
        if (s_standInFailedChar[i] == ch) continue;
        if (!zm_ext_available(2) ||
            (unsigned char*)g_loadDataDestPointer + 256 * 1024 > g_DataBuffer + sizeof(g_DataBuffer)) {
            s_standInFailedChar[i] = ch;
            dbg_printf("[zombie] no room left to dress survivor %d's stand-in as character %d\n", i, ch);
            continue;
        }
        if (active) {
            e->status_flags = 0;
            g_enemy_count--;
        }
        e = zm_standin_spawn(i, ch);
        if (e == NULL) continue;
        int model = zombie_mode_standin_model(e);
        bool loaded = false;
        if (model >= 0) {
            Entity* saved = ENTITY;
            ENTITY = e;
            bool ext = zombie_mode_model_load_begin(e);
            if (ext) {
                LoadEntityEMD(e, (unsigned char)model);
                zombie_mode_model_load_end();
                Entity_SetJoints(e, 0x7c);
                InitAnimStructure((void*)e->modelLoadBuffer);
                g_loadDataDestPointer = (void*)SetupJointStructures((unsigned int)g_loadDataDestPointer);
                if ((g_main_state_flags & MSF_MIRROR_ENABLE) != 0) {
                    SetupEntityJointAnimation();
                }
                loaded = true;
            }
            ENTITY = saved;
        }
        // Laid out and armed afresh on its next pose (zm_survivor_puppet_update).
        s_standIns[i].baseSaved = false;
        s_standIns[i].weapon = 0;
        if (!loaded) {
            e->status_flags = 0;
            g_enemy_count--;
            s_standInFailedChar[i] = ch;
            dbg_printf("[zombie] survivor %d's stand-in could not load character %d\n", i, ch);
            continue;
        }
        s_standInFailedChar[i] = -1;
        dbg_printf("[zombie] survivor %d's stand-in now wears character %d\n", i, ch);
    }
}

// ---------------------------------------------------------------------------
// Monsters made mid-room (the director's placements, and their copies on the
// other players' screens)
// ---------------------------------------------------------------------------

// room_set's model loop for one entity, after the room is up: share the model
// of a monster of the same type already here (the loop's reuse branch - the
// -0xc undoes the +0xc InitAnimStructure leaves in modelLoadBuffer through
// SetAnimSlot), or load its EMD fresh. Then the loop's joint set-up. False
// when there is no texture bank or room data left for a fresh load.
static bool zm_load_live_model(Entity* e)
{
    const Entity* donor = NULL;
    for (int i = 0; i < ZM_FIRST_SURVIVOR_SLOT && donor == NULL; i++) {
        const Entity* o = &g_EnemiesList[i];
        if (o != e && (o->status_flags & ENTITY_STATUS_ACTIVE) != 0 && o->id == e->id &&
            o->animHeader != 0 && o->jointsStructs != NULL) {
            donor = o;
        }
    }
    Entity* saved = ENTITY;
    ENTITY = e;
    if (donor != NULL) {
        e->animHeader = donor->animHeader;
        e->animBase = donor->animBase;
        e->modelLoadBuffer = donor->modelLoadBuffer - 0xc;
    } else {
        if ((unsigned char*)g_loadDataDestPointer + 256 * 1024 > g_DataBuffer + sizeof(g_DataBuffer) ||
            !zm_ext_begin(2)) {
            ENTITY = saved;
            return false;
        }
        LoadEntityEMD(e, (unsigned char)(e->id + 4));
        zm_ext_end();
    }
    Entity_SetJoints(e, 0x7c);
    InitAnimStructure((void*)e->modelLoadBuffer);
    g_loadDataDestPointer = (void*)SetupJointStructures((unsigned int)g_loadDataDestPointer);
    if ((g_main_state_flags & MSF_MIRROR_ENABLE) != 0) {
        SetupEntityJointAnimation();
    }
    ENTITY = saved;
    return true;
}

// A kept monster into the loaded room now, under `uid`. NULL if there is no
// slot, bank or memory for it.
static Entity* zm_spawn_live(unsigned char id, unsigned char behavior, int x, int y, int z,
                             short angle, unsigned short uid)
{
    int slot = -1;
    for (int i = zm_room_highest_script_slot() + 1; i < ZM_FIRST_SURVIVOR_SLOT && slot < 0; i++) {
        if ((g_EnemiesList[i].status_flags & ENTITY_STATUS_ACTIVE) == 0) slot = i;
    }
    if (slot < 0) return NULL;
    Entity* e = zm_spawn_monster(slot, id, behavior, x, y, z, angle);
    if (e == NULL) return NULL;
    if (!zm_load_live_model(e)) {
        e->status_flags = 0;
        g_enemy_count--;
        return NULL;
    }
    zm_world_track_uid(e, uid);
    dbg_printf("[zombie] live spawn of id %d (%04X) in slot %d\n", (int)id, (int)uid, slot);
    return e;
}

// The AI director's own placements say nothing on screen: the host's copy is
// a survivor's then (zm_director_place_ai).
static bool s_notesMuted = false;

void zm_note(const char* text)
{
    if (s_notesMuted) {
        dbg_printf("[ai] %s\n", text);
        return;
    }
    snprintf(s_noteText, sizeof(s_noteText), "%s", text);
    s_switchNoteFrames = ZM_SWITCH_NOTE_FRAMES * 2;
}

// Is a body-sized spot free of walls? The blocking point query
// (room_collision_check_0047da50) at the centre and four body-radius offsets,
// called as its own callers do: (absolute position, zero SVECTOR offset).
static bool zm_spot_clear(int x, int z)
{
    static const int r = 300;
    static const int off[5][2] = { {0,0}, {r,0}, {-r,0}, {0,r}, {0,-r} };
    SVECTOR zero = { 0, 0, 0, 0 };
    for (int i = 0; i < 5; i++) {
        VECTOR p = { x + off[i][0], 0, z + off[i][1], 0 };
        if (room_collision_check_0047da50(&p, (VECTOR*)&zero) == 1) return false;
    }
    return true;
}

bool zm_spot_free(int x, int z)
{
    return zm_spot_clear(x, z);
}

// A spot in a room that is not loaded: one of its three generated spawn spots,
// else one of its own enemy_set positions (known good), else where a door from
// a neighbouring room lets in. Placed
// monsters already waiting there push the pick along so they do not stack.
static bool zm_remote_spot(unsigned char stage, unsigned char room, short* x, short* y, short* z,
                           short* angle)
{
    int nth = zm_world_room_extra_count(stage, room);
    // The room's own three spawn spots, in turn (ZombieSpawnSpots.cpp); a
    // fourth and later monster beside the first spots.
    for (int i = 0; i < g_zmSpawnSpotCount; i++) {
        const ZmSpawnSpots& t = g_zmSpawnSpots[i];
        if (t.stage != stage || t.room != room) continue;
        const short* p = t.spot[nth % 3];
        *x = (short)(p[0] + (nth / 3) * 400);
        *y = p[1];
        *z = p[2];
        *angle = (short)(rand() & 0xFFF);
        return true;
    }
    const ZmSpawn* spawns;
    int n = zm_room_spawns(stage, room, &spawns);
    if (n < 0) return false;
    int usable = 0;
    for (int i = 0; i < n; i++) {
        if (spawns[i].x > 0 && spawns[i].z > 0 && spawns[i].y > -5000 && spawns[i].y < 5000) usable++;
    }
    if (usable > 0) {
        int want = nth % usable;
        for (int i = 0; i < n; i++) {
            const ZmSpawn& sp = spawns[i];
            if (!(sp.x > 0 && sp.z > 0 && sp.y > -5000 && sp.y < 5000)) continue;
            if (want-- == 0) {
                *x = (short)(sp.x + (nth / usable) * 400);
                *y = sp.y; *z = sp.z; *angle = sp.angle;
                return true;
            }
        }
    }
    // A door into it, from each of the rooms its own doors lead to.
    const ZmDoor* doors;
    int nd = zm_room_doors(stage, room, &doors);
    ZmDoor own[ZM_MAX_DOORS];
    memcpy(own, doors, sizeof(ZmDoor) * nd);    // the cache may move under the next lookups
    for (int i = 0; i < nd; i++) {
        unsigned char ns, nr;
        zm_decode_dest(own[i].dest, stage, &ns, &nr);
        const ZmDoor* back;
        int nb = zm_room_doors(ns, nr, &back);
        for (int k = 0; k < nb; k++) {
            unsigned char bs, br;
            zm_decode_dest(back[k].dest, ns, &bs, &br);
            if (bs != stage || br != room || (back[k].flags0B & 0x80) != 0) continue;
            *x = (short)(back[k].arriveX + nth * 400);
            *y = back[k].arriveY;
            *z = back[k].arriveZ;
            *angle = back[k].arriveAngle;
            return true;
        }
    }
    return false;
}

// The live monsters in (stage, room), against its cap: the loaded room's
// entities (the director's body among them) and its extras still to come in;
// another room's waiting extras. Another copy's kills there only count once
// its owner has left the room (zm_world_capture_room).
int zm_room_monster_count(unsigned char stage, unsigned char room)
{
    if (stage != g_stageId || room != g_roomId) return zm_world_room_extra_count(stage, room);
    int n = 0;
    for (int i = 0; i < ZM_FIRST_SURVIVOR_SLOT; i++) {
        const Entity* e = &g_EnemiesList[i];
        if ((e->status_flags & ENTITY_STATUS_ACTIVE) == 0 || e->id >= NPC_ENTITIES_IDS) continue;
        if (e->health < 0 || (e->status_flags & ENTITY_STATUS_DEAD) != 0) continue;
        if (zm_yawn_entity(e)) continue;        // the room's boss, not the director's
        n++;
    }
    return n + zm_world_room_unspawned();
}

// Weighted capacity, separate from head count used to choose spawn spots.
int zm_room_monster_slots(unsigned char stage, unsigned char room)
{
    if (stage != g_stageId || room != g_roomId) return zm_world_room_extra_count(stage, room, true);
    int n = 0;
    for (int i = 0; i < ZM_FIRST_SURVIVOR_SLOT; i++) {
        const Entity* e = &g_EnemiesList[i];
        if ((e->status_flags & ENTITY_STATUS_ACTIVE) == 0 || e->id >= NPC_ENTITIES_IDS) continue;
        if (e->health < 0 || (e->status_flags & ENTITY_STATUS_DEAD) != 0) continue;
        if (zm_yawn_entity(e)) continue;
        n += zm_econ_monster_slots(e->id);
    }
    return n + zm_world_room_unspawned(true);
}

// The director puts a monster of type `id` into (stage, room): in front of
// its zombie when that is the room it is in, else at a spot of that room for
// whenever someone walks in. Kept in the roster either way, so every copy
// has it.
static void zm_director_place(unsigned char stage, unsigned char room, unsigned char id,
                              const char* name, const short* at = NULL)
{
    char line[48];
    if (zm_is_hall(stage, room) && zm_hall_closed(NULL)) {
        zm_note_hall_closed();
        return;
    }
    if (zm_director_safe_room(stage, room)) {
        zm_note("NOTHING CAN BE PLACED IN A SAFE ROOM");
        return;
    }
    if (!zm_is_trap_id(id) && zm_hall_2f_refuses(stage, room, id)) {
        zm_note("ONLY ZOMBIES AND HUNTERS IN THE 2F HALL");
        return;
    }
    // A trap, not a monster (ZombieTraps.cpp): no cap, its own cooldown.
    if (zm_is_trap_id(id)) {
        zm_trap_place(stage, room, id, name, line, sizeof(line));
        zm_note(line);
        return;
    }
    if (zm_room_has_survivor(stage, room)) {
        zm_reinforce_request(stage, room, id, line, sizeof(line));
        zm_note(line);
        return;
    }
    // Unlocked, room in the room, and the points for it (ZombieEconomy.cpp).
    if (!zm_econ_can_place(stage, room, id, name, line, sizeof(line))) {
        zm_note(line);
        return;
    }
    Entity* z = g_zombieModeEntity;
    if (stage == g_stageId && room == g_roomId && z != NULL) {
        int x = 0, zz = 0;
        bool found = false;
        static const int reach[3] = { 1400, 900, 0 };
        for (int r = 0; r < 3 && !found; r++) {
            SVECTOR ahead = { (short)reach[r], 0, 0, 0 };
            MovePlayerXZ(z->angle, &ahead, &ahead);
            x = z->scaMatrixData.localMatrix.t[0] + ahead.x;
            zz = z->scaMatrixData.localMatrix.t[2] + ahead.z;
            found = zm_spot_clear(x, zz);
        }
        int y = z->scaMatrixData.localMatrix.t[1];
        short angle = (short)((z->angle + 0x800) & 0xFFF);     // facing the director
        unsigned short uid = zm_world_add_extra(stage, room, id, 0, (short)x, (short)y, (short)zz, angle);
        if (uid != 0) zm_econ_pay(id);     // kept either way: it comes in on the next load
        if (uid == 0 || zm_spawn_live(id, 0, x, y, zz, angle, uid) == NULL) {
            snprintf(line, sizeof(line), "NO ROOM HERE FOR A %s", name);
            zm_note(line);
            if (uid != 0) {
                // Not here after all: let it wait for the next load of the room.
                dbg_printf("[zombie] placed %04X waits for the room to reload\n", (int)uid);
            }
            return;
        }
        snprintf(line, sizeof(line), "%s PLACED", name);
        zm_note(line);
        return;
    }
    short x, y, zz, angle;
    if (at != NULL) {
        x = at[0]; y = at[1]; zz = at[2]; angle = at[3];
    } else if (!zm_remote_spot(stage, room, &x, &y, &zz, &angle)) {
        zm_note("NO SPOT FOR IT THERE");
        return;
    }
    if (zm_world_add_extra(stage, room, id, 0, x, y, zz, angle) == 0) {
        zm_note("THE ROSTER IS FULL");
        return;
    }
    zm_econ_pay(id);
    const char* rn = DebugRoom_Name(stage, room);
    snprintf(line, sizeof(line), "%s WAITS IN %s", name, rn != NULL ? rn : "THAT ROOM");
    zm_note(line);
}

// The AI director (ZombieDirectorAI.cpp) places through the same rules as the
// map. _allowed: would zm_director_place take a monster `id` (not a trap) in
// (stage, room) now? Silent - no note. _place: place it (or set the trap);
// true when it was bought.
bool zm_director_place_allowed(unsigned char stage, unsigned char room, unsigned char id)
{
    if (zm_is_hall(stage, room) && zm_hall_closed(NULL)) return false;
    if (zm_director_safe_room(stage, room)) return false;
    if (zm_is_trap_id(id)) return true;
    if (zm_hall_2f_refuses(stage, room, id)) return false;
    if (zm_room_has_survivor(stage, room)) return false;
    char why[48];
    return zm_econ_can_place(stage, room, id, "", why, sizeof(why));
}

bool zm_director_move_allowed(unsigned char stage, unsigned char room, unsigned char id)
{
    if (zm_is_hall(stage, room) && zm_hall_closed(NULL)) return false;
    if (zm_director_safe_room(stage, room)) return false;
    if (zm_hall_2f_refuses(stage, room, id)) return false;
    if (zm_room_has_survivor(stage, room)) return false;
    return zm_econ_room_fits(stage, room, id);
}

bool zm_room_watched(unsigned char stage, unsigned char room)
{
    if (stage == g_stageId && room == g_roomId) return true;          // this copy's
    if (s_gameRole == ZM_NET_OFF) {
        unsigned char s, r;                                            // the AI survivor's
        return zm_survivor_location(&s, &r) && s == stage && r == room;
    }
    for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
        if (i == zm_net_self()) continue;
        const ZmNetPeerState* p = zm_net_player(i);
        if (p == NULL || !p->valid) continue;
        if ((p->stage == stage && p->room == room) || (p->viewStage == stage && p->viewRoom == room)) return true;
    }
    return false;
}

bool zm_director_place_ai(unsigned char stage, unsigned char room, unsigned char id, const char* name,
                          const short* at)
{
    ZmEconStats before, after;
    zm_econ_stats(&before);
    s_notesMuted = true;
    zm_director_place(stage, room, id, name, at);
    s_notesMuted = false;
    zm_econ_stats(&after);
    return after.placed != before.placed || after.trapsSet != before.trapsSet;
}

// Extras the roster has for this room that this copy has no entity for -
// placed by the director while this copy was in the room - come in now.
// One that cannot (no slot/bank/memory) is not tried again this visit.
static unsigned short s_liveFailed[16];
static int            s_liveFailedCount = 0;

static void zm_spawn_missing_extras(void)
{
    unsigned short uid;
    unsigned char id, behavior;
    short x, y, z, angle;
    for (int guard = 0; guard < 4; guard++) {
        if (!zm_world_missing_extra(&uid, &id, &behavior, &x, &y, &z, &angle,
                                    s_liveFailed, s_liveFailedCount)) {
            return;
        }
        if (zm_spawn_live(id, behavior, (unsigned short)x, y, (unsigned short)z, angle, uid) == NULL &&
            s_liveFailedCount < 16) {
            s_liveFailed[s_liveFailedCount++] = uid;
        }
    }
}

// room_set's model loop, around a model load: the mod's own entities load
// their textures on the mod's counter (see "Texture pages for the mod's
// models"). The room's script spawns load on the game's, as in the original.
bool zombie_mode_model_load_begin(const Entity* e)
{
    if (!s_zombieModeArmed) return false;
    int slot = (int)(e - g_EnemiesList);
    if (slot < 0 || slot >= 30) return false;
    unsigned short uid = zm_world_uid_of_slot(slot);
    bool mods = zm_is_survivor_slot(slot) || e == s_puppetZombie || e == g_zombieModeEntity ||
                (uid >= 0x100 && uid != ZM_WORLD_UID_NONE);
    return mods && zm_ext_begin(2);
}

void zombie_mode_model_load_end(void)
{
    zm_ext_end();
}

int zombie_mode_standin_model(const Entity* e)
{
    if (s_gameRole == ZM_NET_OFF) return -1;
    int slot = (int)(e - g_EnemiesList);
    if (!zm_is_survivor_slot(slot) || zm_char_of_npc_id(e->id) < 0) return -1;
    // Two texture banks per model (a 256-wide sheet); bank 0x16 is the local
    // player's own, which TmdProcessingCallback would overwrite.
    if (!zm_ext_available(2)) {
        dbg_printf("[zombie] no texture bank left for survivor %d's stand-in\n", slot - ZM_SURVIVOR_SLOT_BASE);
        return -2;
    }
    if ((unsigned char*)g_loadDataDestPointer + 160 * 1024 > g_DataBuffer + sizeof(g_DataBuffer)) {
        dbg_printf("[zombie] no room data left for survivor %d's stand-in\n", slot - ZM_SURVIVOR_SLOT_BASE);
        return -2;
    }
    return zm_char_model_index(zm_char_of_npc_id(e->id));
}

// Where the survivors face at the start: the back of the main hall.
void zm_hall_back_door(int* x, int* z)
{
    *x = 17000;
    *z = 24000;
}

void zombie_mode_room_spawn(void)
{
    if (!s_zombieModeArmed) {
        return;
    }
    s_directorAwaitingOwner = false;
    memset(s_spawnIdlePending, 0, sizeof(s_spawnIdlePending));
    memset(s_spawnIdleUntil, 0, sizeof(s_spawnIdleUntil));
    zm_reconnect_room_ready();
    // Capture the entrance before the player starts moving. Camera-only
    // transitions do not reset it. Multiple doors to the previous room are
    // distinguished by their distance from the actual arrival position.
    if (s_gameRole == ZM_NET_SURVIVOR && s_survivorPreviousStage >= 0) {
        const ZmDoor* doors;
        int n = zm_room_doors(g_stageId, g_roomId, &doors);
        int best = 0x7FFFFFFF;
        for (int i = 0; i < n; i++) {
            unsigned char ds, dr;
            zm_decode_dest(doors[i].dest, g_stageId, &ds, &dr);
            if ((doors[i].flags0B & 0x80) || ds != s_survivorPreviousStage || dr != s_survivorPreviousRoom) continue;
            int dx = (int)doors[i].zoneX + doors[i].zoneW / 2 - g_playerEntity.scaMatrixData.localMatrix.t[0];
            int dz = (int)doors[i].zoneZ + doors[i].zoneD / 2 - g_playerEntity.scaMatrixData.localMatrix.t[2];
            int distance = abs(dx) + abs(dz);
            if (distance < best) { best = distance; s_survivorEntranceDoor = (unsigned char)i; }
        }
    }
    // This game's extra pickups for the room (ZombieRandom.cpp), then the
    // survivors' dropped items (ZombieDrops.cpp).
    zm_random_room_loaded();
    zm_shotgun_room();
    zm_piano_room();
    zm_access_room();
    zm_greenhouse_room();
    zm_roomsync_room();
    zm_statue_room();
    zm_yawn_room();
    zm_drops_room_loaded();

    // The roster's extra monsters first, in the free slots above every slot
    // the room's scripts spawn into, so a mid-room enemy_set can never land on
    // them; then the zombie, right behind. As low as that allows: the routines
    // that walk only the first g_enemy_count slots (HandleEnemyPlayerCollisions,
    // the aim reticle scan) reach it when it sits right behind the room's own
    // enemies.
    int firstFree = zm_world_spawn_extras(zm_room_highest_script_slot() + 1, ZM_FIRST_SURVIVOR_SLOT);
    int slot = -1;
    for (int i = firstFree; i < ZM_FIRST_SURVIVOR_SLOT; i++) {
        if ((g_EnemiesList[i].status_flags & ENTITY_STATUS_ACTIVE) == 0) {
            slot = i;
            break;
        }
    }

    // Arriving by a map jump: take the zombie that was picked, as the room
    // has just placed it (by slot, else the room's zombie nearest the spot -
    // an extra has no slot until it is spawned).
    zm_spawn_survivor_puppets();

    if (s_jumpPending && s_gameRole != ZM_NET_SURVIVOR) {
        s_jumpPending = false;
        s_jumpFixCamera = true;
        Entity* target = NULL;
        int reservedSlot = zm_world_slot_of_uid(s_jumpTarget.uid);
        Entity* bySlot = reservedSlot >= 0 ? &g_EnemiesList[reservedSlot] :
            ((s_jumpTarget.slot < 30) ? &g_EnemiesList[s_jumpTarget.slot] : NULL);
        if (bySlot != NULL && (bySlot->status_flags & ENTITY_STATUS_ACTIVE) != 0 &&
            zm_possessable(bySlot) && (bySlot->status_flags & ENTITY_STATUS_DEAD) == 0 &&
            !zm_possession_busy(bySlot)) {
            target = bySlot;
        } else {
            int bestDist = 0x7FFFFFFF;
            for (int i = 0; i < 30; i++) {
                Entity* e = &g_EnemiesList[i];
                if ((e->status_flags & ENTITY_STATUS_ACTIVE) == 0 || !zm_possessable(e)) continue;
                if ((e->status_flags & ENTITY_STATUS_DEAD) != 0 || zm_possession_busy(e)) continue;
                int dx = (int)e->scaMatrixData.localMatrix.t[0] - s_jumpTarget.x;
                int dz = (int)e->scaMatrixData.localMatrix.t[2] - s_jumpTarget.z;
                int d = (dx < 0 ? -dx : dx) + (dz < 0 ? -dz : dz);
                if (d < bestDist) { bestDist = d; target = e; }
            }
        }
        if (target == NULL || zm_world_uid_of_slot((int)(target - g_EnemiesList)) != s_jumpTarget.uid)
            zm_world_control(g_stageId, g_roomId, s_jumpTarget.uid, s_jumpTarget.id, false);
        if (target != NULL) {
            zm_world_control_entity(target, true);
            s_directorMapOnly = false;
            s_zombieDeathReported = false;
            g_zombieModeEntity = target;
            s_engineOwned = false;
            s_jumpFreshInit = target->state == ZOMBIE_STATE_INIT;
            target->collisionFlags |= ZM_COLLISION_LIKE_PLAYER;
            if (!zm_is_zombie_id(target->id)) {
                // A monster: its own init runs on its first update.
                if (!s_jumpFreshInit) zm_monster_possess(target);
            } else if (!s_jumpFreshInit) {
                if (zombie_mode_feeding_target(target)) {
                    zombie_mode_stand_feeding(target);
                    s_engineOwned = true;
                } else {
                    bool prone = (target->behavior_flags & ZOMBIE_FLAG_LAYING_DOWN) != 0;
                    zm_start(target, prone ? ZM_ACT_PRONE : ZM_ACT_IDLE, prone ? ZM_ANIM_PRONE : ZM_ANIM_IDLE);
                }
            }
            zm_note_switch(target);
            dbg_printf("[zombie] jumped in: possessed slot %d, stage %d room %d\n",
                       (int)(target - g_EnemiesList), (int)g_stageId, (int)g_roomId);
            zm_survivor_room_loaded();
            return;
        }
        s_directorMapOnly = true;
        if (!zm_map_is_open()) zm_map_toggle();
        zm_note("NO IDLE MONSTER - TRY AGAIN OR PLACE ONE");
        zm_survivor_room_loaded();
        return;
    }

    if (slot < 0) {
        dbg_printf("[zombie] no free enemy slot in stage %d room %d\n",
                   (int)g_stageId, (int)g_roomId);
        return;
    }

    // Always the white-coat zombie (em1000 / em1100), whatever the room's own
    // zombies wear, so the player's zombie looks the same in every room. At
    // the player's position and facing - the door arrival point, which
    // room_transition_load has just written there.
    // A possessed Cerberus, Hunter or Chimera that came through the door is
    // that monster here too.
    // So does a naked or green zombie: only the white-coat zombie is the
    // director's own body (s_puppetZombie on the other copies, a white coat).
    unsigned char id = (s_gameRole != ZM_NET_SURVIVOR && zm_is_possessable_id(s_carriedId))
                           ? s_carriedId : ENEMY_ZOMBIE;
    bool rosterBody = id != ENEMY_ZOMBIE;
    bool monsterBody = rosterBody && !zm_is_zombie_id(id);
    int bodyX = g_playerEntity.scaMatrixData.localMatrix.t[0];
    int bodyY = g_playerEntity.scaMatrixData.localMatrix.t[1];
    int bodyZ = g_playerEntity.scaMatrixData.localMatrix.t[2];
    short bodyAngle = g_playerEntity.directionAngle;
    if (s_gameRole != ZM_NET_SURVIVOR && s_firstBody) {
        // The game's first room: show it (room_set starts every room on camera
        // 0 - the main hall's is the front door - as after a map jump).
        s_firstBody = false;
        s_jumpFixCamera = true;
    }
    Entity* em = zm_spawn_monster(slot, id, monsterBody ? s_carriedBehavior : 0,
                                  bodyX, bodyY, bodyZ, bodyAngle);
    if (!monsterBody) em->animationId = ZM_ANIM_IDLE;
    em->state = ZOMBIE_STATE_INIT;
    em->collisionFlags = ZM_COLLISION_LIKE_PLAYER;

    if (s_gameRole == ZM_NET_SURVIVOR) {
        // The other player's zombie: an enemy here, posed from the network.
        s_puppetZombie = em;
        dbg_printf("[zombie] puppet zombie in slot %d\n", slot);
        return;
    }

    g_zombieModeEntity = em;
    s_engineOwned = false;
    if (rosterBody || s_gameRole != ZM_NET_OFF) {
        // Every multiplayer body has a persistent monster uid, including the
        // white-coat zombie. The temporary BODY puppet is excluded from room
        // handovers; using it for a real monster made its corpse disappear
        // when the director moved on. Other copies make this monster and
        // the owner's packets pose it. Its init runs on its first update - a
        // zombie's in zombie_mode_update, which puts the carried health and
        // prone state on; a monster's in zm_monster_update, the carried health
        // after (s_carriedPending).
        s_bodyEntity = NULL;
        s_carriedPending = monsterBody;
        unsigned short uid = zm_world_add_extra(g_stageId, g_roomId, id, monsterBody ? s_carriedBehavior : 0,
                                                (short)bodyX, (short)bodyY, (short)bodyZ, bodyAngle, true, s_carriedHealth);
        if (uid != 0) {
            zm_world_track_uid(em, uid);
        } else {
            zm_world_track(em);
        }
    } else {
        s_bodyEntity = em;
        // The body is kept by the roster from here on: left behind (a switch to
        // another zombie, a map jump) it stays in this room like any monster.
        zm_world_track(em);
    }
    zm_world_control_entity(em, true);
    dbg_printf("[zombie] possessed zombie (id %d) in slot %d, stage %d room %d at (%d, %d)\n",
               (int)id, slot, (int)g_stageId, (int)g_roomId,
               (int)em->scaMatrixData.localMatrix.t[0],
               (int)em->scaMatrixData.localMatrix.t[2]);

    // Now the survivor: it either walks this room for real or gets parked.
    zm_survivor_room_loaded();
}

void zombie_mode_room_exit(void)
{
    // Called before DoorSystem changes stage/room, only for full room loads.
    // Camera-only transitions therefore preserve the previous room and timer.
    if (s_gameRole == ZM_NET_SURVIVOR) {
        s_survivorPreviousStage = g_stageId;
        s_survivorPreviousRoom = g_roomId;
    }
    // The room's monsters, as they are now, go into the persistent roster
    // (before the puppet and the possessed zombie leave the list; the capture
    // skips the puppet, and a body walking out with the director). A map jump leaves the body it was in behind: back
    // to its AI where it stands, and captured with the rest.
    bool jumping = s_jumpPending && g_zombieModeEntity != NULL;
    // Only the copy that runs the room writes its monsters back to the
    // roster: a copy that showed them as puppets would send stale or wrong
    // states (the director's placements vanished that way).
    bool owner = s_gameRole == ZM_NET_OFF || s_iOwn;
    if (jumping) {
        Entity* left = g_zombieModeEntity;
        zm_world_control_entity(left, false);
        // A body left dead (the director moving on after its death) stays
        // dead: the capture below keeps it so.
        if (left->health >= 0 && (left->status_flags & ENTITY_STATUS_DEAD) == 0) {
            left->state = ZOMBIE_STATE_IDLE;
            left->ignore_player_flag = 0;
            left->action_behavior = 0;
            left->action_state = 0;
            left->hit_state = 0;
        }
        zm_set_rollback(left);
        g_zombieModeEntity = NULL;
        if (owner) zm_world_capture_room(s_puppetZombie);
        g_zombieModeEntity = left;
    } else if (owner) {
        zm_world_capture_room(s_puppetZombie);
    }
    if (!jumping) zm_world_depart_entity(g_zombieModeEntity);
    zm_spec_room_exit();        // a spectator's follow: in no room from here

    if (s_puppetZombie != NULL) {
        s_puppetZombie->status_flags = 0;
        g_enemy_count--;
        s_puppetZombie = NULL;
    }
    // The other survivors' stand-ins: BuildEnemySnap walks the first
    // g_enemy_count slots, so they must not stay counted.
    for (int slot = ZM_FIRST_SURVIVOR_SLOT; slot < 30; slot++) {
        Entity* sp = &g_EnemiesList[slot];
        if ((sp->status_flags & ENTITY_STATUS_ACTIVE) != 0 && sp->id >= NPC_ENTITIES_IDS &&
            s_gameRole != ZM_NET_OFF) {
            sp->status_flags = 0;
            g_enemy_count--;
        }
    }
    Entity* em = g_zombieModeEntity;
    if (em == NULL) {
        return;
    }
    zm_survivor_zombie_leaving();

    if (!jumping) {
        s_carriedHealth = em->health;
        s_carriedId = em->id;
        s_carriedBehavior = em->behavior_flags;
        s_carriedProne = zm_is_zombie_id(em->id) && (em->behavior_flags & ZOMBIE_FLAG_LAYING_DOWN) != 0;
    }

    // BuildEnemySnap walks the first g_enemy_count slots as if they were the
    // room's enemies; leaving this one counted would make it snapshot a spare
    // slot instead of the room's last real enemy.
    em->status_flags = 0;
    g_enemy_count--;
    g_zombieModeEntity = NULL;
    s_biteVictim = NULL;
}

void zombie_mode_after_transition(void)
{
    Entity* em = g_zombieModeEntity;
    if (em == NULL) {
        return;
    }
    // Still set after room_transition_load: the door was camera-only and the
    // room was not reloaded. The player was moved to the far side; follow it.
    em->scaMatrixData.localMatrix.t[0] = g_playerEntity.scaMatrixData.localMatrix.t[0];
    em->scaMatrixData.localMatrix.t[1] = g_playerEntity.scaMatrixData.localMatrix.t[1];
    em->scaMatrixData.localMatrix.t[2] = g_playerEntity.scaMatrixData.localMatrix.t[2];
    em->angle = g_playerEntity.directionAngle;
    em->action_behavior = ZM_ACT_IDLE;
    em->action_state = 0;
    zm_set_rollback(em);
    zm_survivor_room_loaded();
}

// ===========================================================================
// Hooks into the rest of the game
// ===========================================================================
VECTOR* zombie_mode_camera_target(void)
{
    // A dead survivor's copy: the camera follows the survivor it watches.
    const VECTOR* watched = zm_spec_camera_target();
    if (watched != NULL) return (VECTOR*)watched;
    if (g_zombieModeEntity == NULL) {
        return NULL;
    }
    return (VECTOR*)g_zombieModeEntity->scaMatrixData.localMatrix.t;
}

bool zombie_mode_spectator_camera(void)
{
    return zombie_mode_armed() && zm_spec_camera_update();
}

bool zombie_mode_spectator_player_frozen(void)
{
    return zombie_mode_armed() && zm_spec_player_frozen();
}

// ---------------------------------------------------------------------------
// Characters and their models
//
// Chris, Jill, Barry and Rebecca are player models (char10-char13): their own
// animations and in-hand weapon files. Richard and Enrico exist only as NPC
// models (em1027 / em1028, a cutscene's worth of animation), so they are
// "borrowed characters": their mesh and texture, moved by Chris's skeleton and
// animations (char10's EMR / EDD), holding Chris's weapons. A weapon file's
// arm-and-gun mesh is UV-mapped onto its character's own texture sheet, so
// Chris's weapons need Chris's sheet: wherever a borrowed character is in a
// room, char10's texture is loaded into two of the room's texture banks too,
// and the weapon meshes point there (the arm is Chris's sleeve - accepted).
// ---------------------------------------------------------------------------
// The lobby's character select screen previews a character on the player
// entity before any game is loaded (-1: none).
static int s_previewSkin = -1;

void zombie_mode_preview_skin(int ch)
{
    s_previewSkin = ch;
}

int zombie_mode_player_skin(int base)
{
    if (s_previewSkin >= 0) return s_previewSkin;
    if (!g_bPlayAsZombie || zm_net_role() == ZM_NET_OFF) return base;
    int ch = zm_net_char(zm_net_self());
    return (ch >= 0) ? ch : base;
}

int zombie_mode_player_model_index(int base)
{
    return zm_char_model_index(zombie_mode_player_skin(base));
}

// Every survivor is the scenario's Chris (id 0), whatever it wears; the
// player code's per-character tables - aim heights, fire frames, walk
// footfalls, muzzle flash - are keyed to the body's own animations, so they
// go by the skin. (Barry is Chris's half in the original too - id 2 - and
// Rebecca Jill's, id 3; Richard and Enrico move on Chris's animations.)
int zombie_mode_player_body(void)
{
    if (s_targetSwapped && s_targetIdx >= 0) {
        int targetChar = zm_net_char(s_targetIdx);
        return (targetChar == ZM_CHAR_JILL || targetChar == ZM_CHAR_REBECCA) ? 1 : 0;
    }
    int ch = zombie_mode_player_skin(g_playerEntity.id & 3);
    return (ch == ZM_CHAR_JILL || ch == ZM_CHAR_REBECCA) ? 1 : 0;
}

int zombie_mode_player_voice(int base)
{
    if (!s_zombieModeArmed) return base;
    int ch = zombie_mode_player_skin(g_playerEntity.id & 3);
    // The shipped character banks have Chris, Jill and Rebecca voices.
    // Preserve the room's echo variant (bit 2), independent of its scenario.
    int voice = ch == ZM_CHAR_JILL ? 1 : ch == ZM_CHAR_REBECCA ? 3 : 0;
    return (base & 4) | voice;
}

bool zombie_mode_network_match(void)
{
    return s_zombieModeArmed && s_gameRole != ZM_NET_OFF;
}

bool zombie_mode_target_remote(void)
{
    return s_zombieModeArmed && s_targetSwapped;
}

void zombie_mode_damage_model_loaded(const Entity* e, unsigned int header, unsigned int base)
{
    if (!s_zombieModeArmed || e == (const Entity*)&g_playerEntity || e->id >= NPC_ENTITIES_IDS) return;
    s_damageHeader[e->id] = header;
    s_damageBase[e->id] = base;
}

bool zombie_mode_damage_anim_select(unsigned char enemyId)
{
    if (!s_zombieModeArmed) return true;
    if (enemyId >= NPC_ENTITIES_IDS || s_damageHeader[enemyId] == 0 || s_damageBase[enemyId] == 0) {
        dbg_printf("[reaction] missing damage animation set for id %02X room %d/%02X\n",
            enemyId, (int)g_stageId, (int)g_roomId);
        return false;
    }
    g_playerEntity.emdScratchPtr1 = s_damageHeader[enemyId];
    g_playerEntity.emdScratchPtr2 = s_damageBase[enemyId];
    return true;
}

int zombie_mode_player_weapon_block(int base)
{
    int ch = zombie_mode_player_skin(base);
    return zm_char_borrowed(ch) ? ZM_CHAR_CHRIS : ch;
}

// Chris's model file, read whole: its EMR/EDD block for the borrowed
// characters' animations, its TIM for their weapons' texture.
static unsigned char s_chrisEmd[120 * 1024];
static unsigned char s_chrisAnim[24 * 1024];     // char10's EMR + EDD, kept for the session
static unsigned int  s_chrisEmdSize = 0;

static bool zm_load_chris_emd(void)
{
    char path[96];
    snprintf(path, sizeof(path), "%senemy/char10.emd", GAME_DATA_ROOT);
    size_t size = LoadFile(path, s_chrisEmd, 0x20);
    if (size == (size_t)-1 || size < 0x40 || size > sizeof(s_chrisEmd)) {
        dbg_printf("[zombie] could not read %s\n", path);
        s_chrisEmdSize = 0;
        return false;
    }
    s_chrisEmdSize = (unsigned int)size;
    return true;
}

// LoadEntityModel, right after the player's EMD is in: a borrowed character
// keeps its own mesh and texture, and takes Chris's skeleton and animation
// tables (the block before char10's TMD, copied out - its offsets are
// relative, so it works from anywhere).
void zombie_mode_player_model_loaded(void)
{
    if (!zm_char_borrowed(zombie_mode_player_skin(g_playerEntity.id & 3))) return;
    if (!zm_load_chris_emd()) return;
    const unsigned int* dir = (const unsigned int*)(s_chrisEmd + (s_chrisEmdSize & 0xFFFFFFFC) - 0x14);
    unsigned int header = dir[1] & 0xFFFFFFFC;
    unsigned int base = dir[2] & 0xFFFFFFFC;
    unsigned int tmd = dir[3] & 0xFFFFFFFC;
    if (tmd > sizeof(s_chrisAnim) || header >= tmd || base >= tmd) {
        dbg_printf("[zombie] char10's animation block does not fit\n");
        return;
    }
    memcpy(s_chrisAnim, s_chrisEmd, tmd);
    g_playerEntity.animHeader = (unsigned int)(s_chrisAnim + header);
    g_playerEntity.animBase = (unsigned int)(s_chrisAnim + base);
    dbg_printf("[zombie] borrowed character: Chris's animations\n");
}

// Chris's texture sheet in the loaded room, for the borrowed characters'
// weapons (-1: not loaded here).
static int s_chrisSheetBank = -1;
static int s_chrisSheetPage = -1;

// Jill's texture sheet in the loaded room, for the Ingram: its in-hand mesh
// (players/w18.emw, every character block's) is cut for her sheet, so on
// anyone else's body it read their own sheet and came out in toy colours.
// Loaded the first time someone else holds it in the room (char11.emd's
// TIM, kept for the session like Chris's); -1: not loaded, or no room.
static unsigned char s_jillEmd[120 * 1024];
static int  s_jillSheetBank = -1;
static int  s_jillSheetPage = -1;
static bool s_jillSheetTried = false;

static bool zm_jill_sheet(int* bank, int* page)
{
    if (!s_jillSheetTried) {
        s_jillSheetTried = true;
        char path[96];
        snprintf(path, sizeof(path), "%senemy/char11.emd", GAME_DATA_ROOT);
        size_t size = LoadFile(path, s_jillEmd, 0x20);
        if (size == (size_t)-1 || size < 0x40 || size > sizeof(s_jillEmd)) {
            dbg_printf("[zombie] could not read %s: the Ingram keeps the holder's sheet\n", path);
        } else if (!zm_ext_begin(2)) {
            dbg_printf("[zombie] no texture bank for Jill's sheet here: the Ingram keeps the holder's sheet\n");
        } else {
            s_jillSheetBank = g_TextureBankID;
            s_jillSheetPage = g_TextureCurrentPage;
            unsigned int tim = *(const unsigned int*)(s_jillEmd + (size & 0xFFFFFFFC) - 4) & 0xFFFFFFFC;
            ProcessTmdAsync((unsigned int)(s_jillEmd + tim));
            zm_ext_end();
        }
    }
    if (s_jillSheetBank < 0) return false;
    *bank = s_jillSheetBank;
    *page = s_jillSheetPage;
    return true;
}

// Chris's sheet, loaded on demand (the flamethrower) when room_set did not
// load it for a borrowed character; tried once a room.
static bool s_chrisSheetTried = false;

static bool zm_chris_sheet(int* bank, int* page)
{
    if (s_chrisSheetBank < 0 && !s_chrisSheetTried) {
        s_chrisSheetTried = true;
        if (!zm_load_chris_emd() || !zm_ext_begin(2)) {
            dbg_printf("[zombie] no texture bank for Chris's sheet here: borrowed weapons keep the holder's sheet\n");
        } else {
            s_chrisSheetBank = g_TextureBankID;
            s_chrisSheetPage = g_TextureCurrentPage;
            unsigned int tim = *(const unsigned int*)(s_chrisEmd + (s_chrisEmdSize & 0xFFFFFFFC) - 4) & 0xFFFFFFFC;
            ProcessTmdAsync((unsigned int)(s_chrisEmd + tim));
            zm_ext_end();
        }
    }
    if (s_chrisSheetBank < 0) return false;
    *bank = s_chrisSheetBank;
    *page = s_chrisSheetPage;
    return true;
}

// The Ingram on a body other than Jill's takes her sheet.
static bool zm_ingram_borrows_sheet(int ch, unsigned char weapon)
{
    return s_zombieModeArmed && weapon == ITEM_INGRAM && ch != ZM_CHAR_JILL;
}

// The flamethrower's in-hand mesh (players/w05.emw - w15, w25 and w35 are the
// same file) is cut for Chris's sheet: on Jill's, Barry's or Rebecca's body
// its barrel and tank read their own sheet's skin and came out flesh-coloured.
// The borrowed characters already read Chris's sheet for every weapon.
static bool zm_flamer_borrows_sheet(int ch, unsigned char weapon)
{
    return s_zombieModeArmed && weapon == ITEM_FLAMETHROWER && ch != ZM_CHAR_CHRIS && !zm_char_borrowed(ch);
}

static bool zm_weapon_borrows_sheet(int ch, unsigned char weapon)
{
    return zm_ingram_borrows_sheet(ch, weapon) || zm_flamer_borrows_sheet(ch, weapon);
}

// The sheet a weapon cut for another character reads on this body.
static bool zm_weapon_sheet(int ch, unsigned char weapon, int* bank, int* page)
{
    if (zm_ingram_borrows_sheet(ch, weapon)) return zm_jill_sheet(bank, page);
    if (zm_flamer_borrows_sheet(ch, weapon)) return zm_chris_sheet(bank, page);
    return false;
}

bool zombie_mode_weapon_texture(int* bank, int* page)
{
    int ch = zombie_mode_player_skin(g_playerEntity.id & 3);
    if (zm_weapon_borrows_sheet(ch, g_playerEntity.equippedWeaponId)) {
        return zm_weapon_sheet(ch, g_playerEntity.equippedWeaponId, bank, page);
    }
    if (!zm_char_borrowed(ch) || s_chrisSheetBank < 0) return false;
    *bank = s_chrisSheetBank;
    *page = s_chrisSheetPage;
    return true;
}

// room_set, after its model loop: if a borrowed character is here - this
// copy's own survivor, or another player's stand-in - load Chris's sheet into
// the next two banks (as LoadEntityEMD loads a model's), then reload this
// copy's weapon so its mesh points at it.
void zombie_mode_room_models_loaded(void)
{
    s_chrisSheetBank = -1;
    s_chrisSheetPage = -1;
    s_chrisSheetTried = false;
    s_jillSheetBank = -1;
    s_jillSheetPage = -1;
    s_jillSheetTried = false;
    if (!s_zombieModeArmed) return;
    // The room's banks are new: this copy's Ingram or flamethrower reads the
    // other character's sheet again.
    bool sheet = zm_weapon_borrows_sheet(zombie_mode_player_skin(g_playerEntity.id & 3),
                                         g_playerEntity.equippedWeaponId);
    if (s_gameRole == ZM_NET_OFF) {
        if (sheet) {
            LoadEquippedWeaponAnimation(g_playerEntity.equippedWeaponId, 0xE,
                                        (unsigned int)g_animationBuffer, (unsigned int)g_animObjectBuffer);
        }
        return;
    }
    bool own = s_gameRole == ZM_NET_SURVIVOR && zm_char_borrowed(zombie_mode_player_skin(g_playerEntity.id & 3));
    bool needed = own;
    for (int slot = ZM_FIRST_SURVIVOR_SLOT; slot < 30 && !needed; slot++) {
        const Entity* e = &g_EnemiesList[slot];
        needed = (e->status_flags & ENTITY_STATUS_ACTIVE) != 0 && zm_char_borrowed(zm_char_of_npc_id(e->id));
    }
    if (!needed) {
        if (sheet) {
            LoadEquippedWeaponAnimation(g_playerEntity.equippedWeaponId, 0xE,
                                        (unsigned int)g_animationBuffer, (unsigned int)g_animObjectBuffer);
        }
        return;
    }
    int bank, page;
    zm_chris_sheet(&bank, &page);
    if (own || sheet) {
        LoadEquippedWeaponAnimation(g_playerEntity.equippedWeaponId, 0xE,
                                    (unsigned int)g_animationBuffer, (unsigned int)g_animObjectBuffer);
    }
}

bool zombie_mode_armed(void)
{
    return s_zombieModeArmed;
}

bool zombie_mode_ingram_finite(unsigned char itemId)
{
    return s_zombieModeArmed && itemId == ITEM_INGRAM;
}

bool zombie_mode_manual_reload(void)
{
    return s_zombieModeArmed && s_gameRole == ZM_NET_SURVIVOR;
}

bool zombie_mode_skip_intro(void)
{
    // The mode has no story: no logo, opening or prologue movie, single
    // player or not.
    return g_bPlayAsZombie;
}

// ---------------------------------------------------------------------------
// No story: the room scripts' cutscenes are not started
//
// A room script starts an event (cmd_scd_event_create, opcode 0x14) when its
// story moment comes: Barry and Wesker in the main hall, Kenneth's body, the
// dog at the window. Those are told apart from the scripts' other events
// (props that animate, the stairs a press walks the player down - those come
// from an action press, create_room_event, and are not filtered here) by what
// they say: a story event plays a voice line (SCD 0x1E, voice_play) or a movie
// (0x29, fmv_set), in its own script or in an event it starts (0x05) or
// re-inits to (0x08). A monster's entrance is one too, voice or not: an
// event that takes the player's control (bit_op clearing message flag 0x100)
// and drives an enemy (set_entity on an enemy, enemy_pos_set 0x21,
// enemy_prop_set 0x28) - the Hunter coming in at the back passage (ROOM60A0),
// the 2F small library (ROOM7060), the attic (ROOM7100). The rooms' own
// monsters are refused in the mode (zombie_mode_enemy_spawn), so it would play
// to an empty room, or move a monster the director placed in that slot.
//
// Such an event is not created; the flag writes in it (bit_op on the
// scenario, lock, enemy and item banks) are made at once, so the room counts
// the scene as seen - the same thing the start of the mode does for the main
// hall's intro (zombie_mode_new_game). The scene's own script and the scripts
// it re-inits to (not the events it starts alongside) also give up what they
// hold: their control-flag writes (main state and message flags) are made in
// order, which ends where the scene would - control back - and their
// room_action_arm writes disarm the zone that started it. The room script
// often takes the control itself just before it starts the scene (the roofed
// passage, ROOM61A0: message flag 0x100 cleared, then the radio call; the
// room init's attack_anim_set before the next one), and only the scene's end
// gives it back.
//
// The scan walks the event VM's opcodes linearly, with the widths of
// RoomEvents.cpp / tools/evt_disasm.py, and an embedded SCD block's commands
// with the widths tools/mine_room_scd.py derives from the original's command
// table (0x004c1110). -1: not a command.
// ---------------------------------------------------------------------------
static const signed char kScdCmdWidth[0x51] = {
     0,  1,  1,  1,  3,  3,  3,  5,  3,  1,  1,  3, 25, 17,  1,  7,
     1,  1,  9,  3,  3,  1,  1,  9, 25,  3,  1, 21,  5,  1,  3, 27,
    13, 13,  3,  1,  3,  3,  0,  1,  5,  1, 11,  3,  1,  1,  0,  3,
    11,  3,  3,  1,  1,  3,  3,  3,  3,  1,  3,  5,  5, 11,  1,  5,
    15,  3,  3,  3,  1,  1, 43, 13,  1,  1,  1,  1,  3,  1,  3,  1,
     1,
};
#define ZM_EVT_MAX_SCRIPTS 64

int zm_scd_width(unsigned char op)
{
    return op < sizeof(kScdCmdWidth) ? kScdCmdWidth[op] : -1;
}

struct ZmEvtScan {
    bool               story;      // a voice line or a movie somewhere in it
    bool               control;    // takes the player's control (message flag 0x100)
    bool               enemy;      // drives an enemy
    bool               apply;      // second pass: make its flag writes
    bool               release;    // ... and the scene's control-flag writes
    unsigned long long seen;       // scripts already walked
};

// `main`: the scene's own script or one it re-inits to, not an event it starts.
static void zm_evt_scd_cmd(const unsigned char* c, ZmEvtScan* s, bool main)
{
    unsigned char op = c[0];
    if (op == 0x1E || op == 0x29) s->story = true;
    if (op == 0x21 || op == 0x28) s->enemy = true;
    if (op == 0x05 && c[1] == 6 && c[2] == 0x17 && c[3] == 1) s->control = true;
    if (!s->apply) return;
    // bit_op { bank, bit, operation }: set / clear on the persistent banks;
    // the main state (5) and message (6) flags a running scene juggles only
    // from the scene's own script, and only on the release pass.
    bool bitOp = op == 0x05 && c[3] <= 1 &&
                 (c[1] <= 3 || c[1] == 7 || (main && s->release && (c[1] == 5 || c[1] == 6)));
    // room_action_arm { slot, handler, flags }: the scene disarming its trigger.
    bool arm = op == 0x13 && main;
    if (bitOp || arm) {
        unsigned char* saved = g_ScdOpcodes;     // inside the room script's own run
        g_ScdOpcodes = (unsigned char*)c;
        if (bitOp) cmd_bit_op(); else cmd_room_action_arm();
        g_ScdOpcodes = saved;
    }
}

static void zm_evt_scan(int script, int depth, ZmEvtScan* s, bool main)
{
    if (script < 0 || script >= ZM_EVT_MAX_SCRIPTS || depth > 4 || g_RoomEventScripts == NULL) return;
    if ((s->seen & (1ull << script)) != 0) return;
    s->seen |= 1ull << script;
    const unsigned char* p = ((const unsigned char* const*)g_RoomEventScripts)[script];
    if (p == NULL) return;
    int state = 0;
    for (int steps = 0; steps < 2000; steps++) {
        unsigned char op = p[0];
        int w;
        if (op >= 0xF6) {
            // Control flow, ahead of the state's own opcodes as in the VM.
            switch (op) {
            case 0xFF: return;                         // end
            case 0xF8: case 0xFA: w = 4; break;
            case 0xF9: w = 3; break;
            case 0xFC: w = p[1]; break;                // call: jump over the target
            default: w = 1; break;
            }
        } else if (state == 0) {
            switch (op) {
            case 0x00: w = 1; break;
            case 0x01: w = 1; state = 1; break;
            case 0x02: case 0x03: w = 1; state = 2; break;
            case 0x04: if (p[1] == 1) s->enemy = true; w = 3; break;   // set_entity: 1 = enemy
            case 0x05: zm_evt_scan(p[2], depth + 1, s, false); w = 4; break;
            case 0x06: {                                // run_scd: size word, then commands
                w = p[1];
                const unsigned char* q = p + 2;
                int block = *(const unsigned short*)q;
                if (block > w - 2) block = w - 2;
                for (int off = 2; off < block;) {
                    unsigned char cop = q[off];
                    if (cop >= sizeof(kScdCmdWidth) || kScdCmdWidth[cop] < 0) break;
                    if (cop == 0x00) { off++; continue; }
                    if (kScdCmdWidth[cop] == 0) break;
                    zm_evt_scd_cmd(q + off, s, main);
                    off += 1 + kScdCmdWidth[cop];
                }
                break;
            }
            case 0x07: w = p[1]; if (w > 2) zm_evt_scd_cmd(p + 2, s, main); break;   // exec_scd
            case 0x08: zm_evt_scan(p[1], depth + 1, s, main); return;                 // re-init
            case 0x09: w = 2; break;
            default: return;
            }
        } else if (state == 1) {
            switch (op) {
            case 0x00: case 0x82: case 0x86: w = 1; break;
            case 0x80: case 0x8B: w = 1; state = 0; break;
            case 0x81: w = (p[1] & 0x0F) != 0 ? 10 : 2; break;
            case 0x83: w = 7; break;
            case 0x84: case 0x85: case 0x87: case 0x88: w = 4; break;
            case 0x89: case 0x8A: w = 2; break;
            default: return;
            }
        } else {
            switch (op) {
            case 0x00: case 0x02: case 0x03: case 0x04: w = 1; break;
            case 0x01: w = 1; state = 0; break;
            case 0x05: case 0x06: case 0x0A: w = 4; break;
            case 0x07: case 0x0B: w = 8; break;
            case 0x08: w = 3; break;
            case 0x09: w = 2; break;
            default: return;
            }
        }
        if (w <= 0) return;
        p += w;
    }
}

// An "examine" event (ZombieMessages.cpp): what an action press on a painting,
// a statue or a shelf starts - take the control, cut to a close-up, a message,
// cut back, give the control back. Only those commands, the tests around them,
// flag writes and a sound: no entity driven, no event started or re-inited, no
// door, item or enemy set. Such an event's message does not pause, and its
// close-up gives way to the room's camera once the survivor walks off.
static bool zm_evt_scd_examine_op(unsigned char op)
{
    switch (op) {
    case 0x00: case 0x01: case 0x02: case 0x03:    // block end, if / else / end_if
    case 0x04: case 0x05: case 0x06: case 0x07:    // bit test / op, state tests
    case 0x08:                                     // state_byte_set
    case 0x09: case 0x0A: case 0x0B:               // cut_lock_set, current_cut_set, message_set
    case 0x10: case 0x11: case 0x13:               // item tests, room_action_arm
    case 0x17: case 0x1D: case 0x22: case 0x23:    // sfx, item tests, cut_lock_write
    case 0x38: case 0x3C: case 0x3F:               // pad / player distance / direction tests
        return true;
    default:
        return false;
    }
}

bool zm_evt_is_examine(int script)
{
    if (script < 0 || script >= ZM_EVT_MAX_SCRIPTS || g_RoomEventScripts == NULL) return false;
    const unsigned char* p = ((const unsigned char* const*)g_RoomEventScripts)[script];
    if (p == NULL) return false;
    bool message = false;
    for (int steps = 0; steps < 2000; steps++) {
        unsigned char op = p[0];
        int w;
        const unsigned char* cmd = NULL;    // an SCD command to check
        if (op >= 0xF6) {
            switch (op) {
            case 0xFF: return message;
            case 0xF8: case 0xFA: w = 4; break;
            case 0xF9: w = 3; break;
            case 0xFC: w = p[1]; if (w > 2) cmd = p + 2; break;   // call: its command inline
            default: w = 1; break;
            }
        } else {
            switch (op) {
            case 0x00: w = 1; break;
            case 0x06: {                                // run_scd
                w = p[1];
                const unsigned char* q = p + 2;
                int block = *(const unsigned short*)q;
                if (block > w - 2) block = w - 2;
                for (int off = 2; off < block;) {
                    unsigned char cop = q[off];
                    if (!zm_evt_scd_examine_op(cop)) return false;
                    if (cop == 0x0B) message = true;
                    if (cop == 0x00) { off++; continue; }
                    off += 1 + kScdCmdWidth[cop];
                }
                break;
            }
            case 0x07: w = p[1]; if (w > 2) cmd = p + 2; break;   // exec_scd
            default: return false;    // entity, state, event create / init / kill
            }
        }
        if (cmd != NULL) {
            if (!zm_evt_scd_examine_op(cmd[0])) return false;
            if (cmd[0] == 0x0B) message = true;
        }
        if (w <= 0) return false;
        p += w;
    }
    return false;
}

// The scene skipped was to pose the player and stand it back at its end (the
// event VM's 0x86, state 1): a player the room script left in the scripted
// state 8 for it (attack_anim_set) is stood back now - unless an event that is
// running holds it (the director's body riding a room event, zm_try_event).
static void zm_evt_release_player(void)
{
    if (g_playerEntity.animationId != 8 || s_riding) return;
    for (int i = 0; i < 8; i++) {
        if (g_ScdEventTable[i].active != 0 && g_ScdEventTable[i].entity == (Entity*)&g_playerEntity) return;
    }
    g_playerEntity.animationId = 1;
    g_playerEntity.animFrameId = 0;
    g_playerEntity.action_behavior = 0;
    g_playerEntity.action_state = 0;
    g_playerEntity.isBeingAttackedFlag = 0;
    dbg_printf("[zombie] player stood back from a skipped scene's pose\n");
}

bool zombie_mode_skip_scd_event(int scriptIndex)
{
    if (!s_zombieModeArmed) return false;
    // Commit poisoning immediately. STORY relays the flag so every copy
    // starts its visual transition while the player keeps normal control.
    if (g_stageId == STAGE_MANSION_RETURN_1F && g_roomId == ROOM_GREENHOUSE && scriptIndex == 1) {
        Flg_on((int)g_ScenarioFlags2, 0xA6);
        return true;
    }
    // The multiplayer shotgun module owns the mounting plate and ceiling;
    // native story rescue/fatal scripts must not move or kill one local player.
    if (g_stageId == STAGE_MANSION_RETURN_1F &&
        (g_roomId == ROOM_TRAP_ROOM || g_roomId == ROOM_LIVING_ROOM)) return true;
    // The attic's scene once Yawn has fled (ROOM7100 event 0) takes the
    // camera and places the player: skipped like a monster's entrance.
    bool yawnScene = g_stageId == STAGE_MANSION_RETURN_2F && g_roomId == ROOM_ATTIC && scriptIndex == 0;
    ZmEvtScan scan = { false, false, false, false, false, 0 };
    zm_evt_scan(scriptIndex, 0, &scan, true);
    if (!yawnScene && !scan.story && !(scan.control && scan.enemy)) return false;
    // A per-frame script can ask again every frame: the persistent flags are
    // written each time (cheap, idempotent), the control flags and the player
    // only the first time - a scene asked for over and over must not hand the
    // control back in the middle of a door or a menu.
    int key = scriptIndex | (g_roomId << 8) | (g_stageId << 16);
    bool first = key != s_lastSkippedEvent;
    ZmEvtScan apply = { false, false, false, true, first, 0 };
    zm_evt_scan(scriptIndex, 0, &apply, true);
    if (first) {
        s_lastSkippedEvent = key;
        zm_evt_release_player();
        dbg_printf("[zombie] %s event %d skipped (stage %d room %02X)\n",
                   scan.story ? "story" : "monster entrance", scriptIndex,
                   (int)g_stageId, (int)g_roomId);
    }
    return true;
}

// create_room_event (PlayerAnimations.cpp, room_check_actions[9]) for a zone
// entered on foot (probe flags without 0x80): a scene the player walks into,
// as the Hunter's entrance in the back passage (ROOM60A0 event 0, the FMV and
// the player backing off from the door) - filtered as the room scripts' scenes
// are. Action-press events (stairs, puzzles) still run.
bool zombie_mode_skip_room_event(const unsigned char* entry)
{
    // The keypad's key panel and the pass number's note (ZombieKeypad.cpp).
    if (s_zombieModeArmed && zm_access_room_event(entry)) return true;
    if (!s_zombieModeArmed || (entry[1] & 0x80) != 0) return false;
    return zombie_mode_skip_scd_event(entry[4]);
}

bool zombie_mode_blocks_menu(void)
{
    if (s_zombieModeArmed && zm_shotgun_pending()) return true;
    if (s_zombieModeArmed && zm_access_keypad_up()) return true;   // the keypad has the pad
    // Whoever plays a zombie has no inventory: START is the possession key.
    // Richard's OPTIONS is his radio.
    // A dead survivor waits for the match to end; nobody opens one on the end screen.
    if (s_zombieModeArmed && (s_winShown || (s_gameRole == ZM_NET_SURVIVOR && g_playerEntity.health < 0))) {
        return true;
    }
    return g_zombieModeEntity != NULL || zm_perk_radio_blocks_menu();
}

bool zombie_mode_draw_player(void)
{
    if (zm_shotgun_hide_room()) return false;
    // The director's copy shows the survivors as stand-ins; its player entity
    // is only the monsters' target.
    if (s_gameRole == ZM_NET_ZOMBIE) return false;
    // A spectator's player is only where the follow put it, not its corpse.
    if (zm_spec_away()) return false;
    return g_zombieModeEntity == NULL || zm_survivor_present();
}

bool zombie_mode_enemy_spawn(Entity* e, unsigned char slot, unsigned char id)
{
    if (!s_zombieModeArmed) return true;
    // Puzzle vines run locally: their controllers animate individual segments,
    // which the ordinary monster pose packets cannot reproduce. The shared
    // chemical flag owns their lifetime, rather than the director's roster.
    if (g_stageId == STAGE_MANSION_RETURN_1F && g_roomId == ROOM_GREENHOUSE && id == 0x0F)
        return Flg_ck((int)g_ScenarioFlags2, 0xA6) == 0;
    // No starting monsters: every monster of the mode is the director's -
    // placed from the map. The rooms' own enemy_set records are refused (the
    // NPCs, ids from NPC_ENTITIES_IDS, still appear).
    if (id < NPC_ENTITIES_IDS) return false;
    return zm_world_restore_enemy(e, slot, id);
}

static void zm_shared_after_update(Entity* e);   // with the shared-room state below
static void zm_yawn_init_here(Entity* e);         // with the puppets below
static void zm_area_resolve(void);                 // with the target swap below
static void zm_owner_intercept_attack(Entity* e);

static bool zm_greenhouse_vine(const Entity* e)
{
    return s_zombieModeArmed && e != NULL && e->id == 0x0F &&
        g_stageId == STAGE_MANSION_RETURN_1F && g_roomId == ROOM_GREENHOUSE;
}

void zombie_mode_after_entity_update(Entity* e)
{
    if (!s_zombieModeArmed) return;
    if (zm_greenhouse_vine(e)) return;
    zm_stun_after_update();
    // First: an attack handed to another copy takes back the attack's status
    // bit before the death tests below read it (zm_owner_intercept_attack).
    if (s_gameRole != ZM_NET_OFF) zm_owner_intercept_attack(e);
    zm_world_after_update(e);
    zm_yawn_after_update(e);
    // Yawn is nobody's purchase: no refund, no kill.
    if (zm_econ_on() && !zm_yawn_entity(e)) {
        int slot = (int)(e - g_EnemiesList);
        if (slot >= 0 && slot < ZM_FIRST_SURVIVOR_SLOT && e->id < NPC_ENTITIES_IDS && !zombie_mode_is_puppet(e)) {
            zm_econ_after_update(e, slot, zm_world_uid_of_slot(slot));
        }
    }
    zm_hit_probe_end(e);
    zm_shared_after_update(e);      // ends the target swap
    zm_area_resolve();              // a Yawn's strike on everyone in reach
}

// Snd_em (SoundSystem.cpp), for every enemy sound cue. Sharing a room, the
// director's copy owns every monster in it, so all their cues - the possessed
// zombie's footsteps and roar, the AI zombies' moans, the bites - go across
// and play from the puppets on the survivor's copy, menu open or not.
void zombie_mode_on_snd_em(unsigned char id)
{
    if (s_gameRole == ZM_NET_OFF || !s_iOwn || !s_shared || ENTITY == NULL) return;
    int slot = (int)(ENTITY - g_EnemiesList);
    if (slot < 0 || slot >= 30) return;
    // A zombie's head explosion goes with its ZM_EV_BURST (zm_burst_apply).
    if (id == 6 && zm_is_zombie_id(ENTITY->id)) return;
    zm_net_send_event(ZM_EV_SOUND, id, (short)slot, (short)(g_stageId | (g_roomId << 8)), 0);
}

// The survivor's own monsters (a room the director is not in) do not stop
// for its menu either: the menu task leaves g_message_flags at 0, which
// freezes every enemy (their updates gate on bit 0x04), so the enemies' turn
// gets that bit back for the length of update_entities.
static bool s_menuEnemiesRun = false;
static WORD s_menuSavedMessageFlags = 0;

// ---------------------------------------------------------------------------
// Effects across the link
//
// Effect_CreateBillboard keeps a pointer to a parent matrix (an entity's or a
// joint's world) plus a local offset, and draws the billboard there for its
// life. The copy that spawns one works out where that is now - the matrix's
// translation plus its 4.12 rotation applied to the offset - and the other
// copy spawns the same type there off the identity (g_deadMoveValue), which
// is how the engine itself spawns free-standing billboards.
//
// Only billboards from the monsters' and the player's turns go across, and
// only in a shared room: those come from one copy's fight. Room scripts'
// effects (steam, fire, a cutscene's sparks) run in both copies already.
// ---------------------------------------------------------------------------
static bool s_fxCapture = false;
static bool s_fxReplaying = false;
static short s_fxPending[64][8];
static unsigned char s_fxPendingSlot[64];
static int s_fxPendingCount = 0;
static MATRIX s_fxPendingParent[64];
static SVECTOR s_fxPendingLocal[64];
static bool s_fxPendingFrame[64];
static MATRIX s_fxIncomingParent[ZM_NET_MAX_PLAYERS];
static SVECTOR s_fxIncomingLocal[ZM_NET_MAX_PLAYERS];
static unsigned short s_fxIncomingFloor[ZM_NET_MAX_PLAYERS];
static bool s_fxIncomingFrame[ZM_NET_MAX_PLAYERS];
static unsigned char s_fxWeapon[64], s_fxAim[64];
static unsigned short s_fxFloor[64];
static unsigned char s_fxSavedWeapon, s_fxSavedAim;
static unsigned short s_fxSavedFloor;
static unsigned int s_jointTint[30][ZM_NET_JOINTS];
static unsigned int s_jointTintMask[30];
static unsigned int s_jointHiddenMask[30];

void zombie_mode_fx_capture(bool on)
{
    if (!on) {
        for (int i = 0; i < s_fxPendingCount; i++) {
            short* a = s_fxPending[i];
            const Effect& e = g_effectPool[s_fxPendingSlot[i]];
            a[7] = (short)(e.animHeader[0] | (e.animHeader[3] << 8));
            if (s_fxPendingFrame[i]) {
                const MATRIX& m = s_fxPendingParent[i];
                const SVECTOR& v = s_fxPendingLocal[i];
                zm_net_send_event8(ZM_EV_FX_BASIS, m.m[0][0], m.m[0][1], m.m[0][2],
                    m.m[1][0], m.m[1][1], m.m[1][2], m.m[2][0], m.m[2][1]);
                zm_net_send_event8(ZM_EV_FX_ORIGIN, m.m[2][2], (short)m.t[0], (short)m.t[1],
                    (short)m.t[2], v.x, v.y, v.z, (short)s_fxFloor[s_fxPendingSlot[i]]);
            }
            dbg_printf("[fx] p%d send type %d depth %d slot %d anim %d weapon %d aim %02X header %02X/%02X\n",
                zm_net_self(), a[0] & 255, (unsigned short)a[0] >> 8, s_fxPendingSlot[i],
                e.animId, s_fxWeapon[s_fxPendingSlot[i]], s_fxAim[s_fxPendingSlot[i]],
                e.animHeader[0], e.animHeader[3]);
            zm_net_send_event8(ZM_EV_FX, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
        }
        s_fxPendingCount = 0;
    }
    s_fxCapture = on;
}

void zombie_mode_effect_created(unsigned char slot)
{
    // Child projectiles must use the original shooter's context too; their
    // scripts latch weapon/aim/floor from the player during later phases.
    s_fxWeapon[slot] = g_playerEntity.equippedWeaponId;
    s_fxAim[slot] = g_playerEntity.flags;
    s_fxFloor[slot] = g_playerEntity.posY;
}

void zombie_mode_on_effect(unsigned char type, unsigned char depthGroup, short yaw,
                           const void* spriteInfo, const void* pos, char lightFactor, unsigned char slot)
{
    if (zombie_mode_effect_damage_blocked()) {
        dbg_printf("[fx] p%d replay child slot %d parent %d type %d depth %d weapon %d\n",
            zm_net_self(), slot, g_activeEffectIndex, type, depthGroup, s_fxWeapon[slot]);
    }
    if (s_gameRole == ZM_NET_OFF || !s_shared || !s_fxCapture || s_fxReplaying) return;
    if (pos == NULL || s_fxPendingCount >= 64) return;
    short localYaw = yaw;
    const MATRIX* m = spriteInfo != NULL ? (const MATRIX*)spriteInfo : &g_identityMatrixData;
    const VECTOR* v = (const VECTOR*)pos;
    int x = m->t[0] + ((m->m[0][0] * v->x + m->m[0][1] * v->y + m->m[0][2] * v->z) >> 12);
    int y = m->t[1] + ((m->m[1][0] * v->x + m->m[1][1] * v->y + m->m[1][2] * v->z) >> 12);
    int z = m->t[2] + ((m->m[2][0] * v->x + m->m[2][1] * v->y + m->m[2][2] * v->z) >> 12);
    // A moving effect (a grenade, a rocket) flies along its yaw turned by its
    // parent matrix - EffectActor_UpdateAndRender: velocity through
    // RotMatrixY(yaw), then the parent's snapshot. The replay has no parent,
    // so the parent's heading is folded into the yaw sent: RotMatrixY builds
    // [[c,0,s],[0,1,0],[-s,0,c]], which makes the heading atan2(m02, m00).
    if (spriteInfo != (const void*)g_deadMoveValue) {
        double heading = atan2((double)m->m[0][2], (double)m->m[0][0]);
        yaw = (short)((yaw + (int)(heading * 4096.0 / (2.0 * 3.14159265358979))) & 0xFFF);
    }
    int i = s_fxPendingCount++;
    // Projectile scripts use LOCAL offsets for floor/bounce math. Flattening
    // a parent transform into world xyz + yaw changes those calculations.
    // Preserve the full basis (including pitch/roll from a weapon joint).
    s_fxPendingFrame[i] = type == 8 || type == 11 ||
        (g_playerEntity.equippedWeaponId >= 6 && g_playerEntity.equippedWeaponId <= 10);
    s_fxPendingParent[i] = *m;
    s_fxPendingLocal[i].x = (short)v->x;
    s_fxPendingLocal[i].y = (short)v->y;
    s_fxPendingLocal[i].z = (short)v->z;
    if (s_fxPendingFrame[i]) yaw = localYaw;
    short* a = s_fxPending[i];
    a[0] = (short)(type | (depthGroup << 8)); a[1] = yaw;
    a[2] = (short)x; a[3] = (short)y; a[4] = (short)z;
    a[5] = (short)((unsigned char)lightFactor | ((g_playerEntity.equippedWeaponId & 15) << 8) |
        ((g_playerEntity.flags & 0xE0) << 7));
    a[6] = (short)(g_stageId | (g_roomId << 8));
    // Called after allocation: the caller may still customize the header.
    s_fxPendingSlot[i] = slot;
}

// The local survivor's own sounds - Play3DSnd on bank 1 at the player's
// position during the player's turn: its shots, reloads, empty clicks and
// knife - go to whoever shares the room.
static bool s_psndReplaying = false;

static void zm_shot_audit(void);   // with the shared-room state below

void zombie_mode_on_player_sound(int bank, int soundId, int vol, int pos)
{
    if (s_gameRole == ZM_NET_SURVIVOR && bank == 1 && s_fxCapture && !s_psndReplaying &&
        (g_playerEntity.flags & 0xE0) != 0 && pos == (int)&g_playerEntity.scaMatrixData.localMatrix.t) {
        zm_shot_audit();
    }
    if (s_gameRole != ZM_NET_SURVIVOR || bank != 1 || !s_shared || !s_fxCapture || s_psndReplaying) return;
    if (pos != (int)&g_playerEntity.scaMatrixData.localMatrix.t) return;
    zm_net_send_event8(ZM_EV_PSND, (short)soundId, (short)vol,
                       (short)(g_stageId | (g_roomId << 8)), 0, 0, 0, 0, 0);
}

static void zm_replay_player_sound(const short* a, int src)
{
    if (a[2] != (short)(g_stageId | (g_roomId << 8))) return;
    if (src < 0 || src >= ZM_NET_MAX_PLAYERS || zm_net_char(src) < 0) return;
    Entity* e = &g_EnemiesList[ZM_SURVIVOR_SLOT_BASE + src];
    if ((e->status_flags & ENTITY_STATUS_ACTIVE) == 0) return;
    s_psndReplaying = true;
    Play3DSnd(1, a[0], a[1], (int)&e->scaMatrixData.localMatrix.t);
    s_psndReplaying = false;
}

// Replayed world-space effects use this identity; projectile emitters retain
// their original parent basis in s_replayFrames. Both mark visual-only FX: a
// replayed grenade or
// rocket still finds what it hits and blows up there, but the damage is the
// shooter's copy's to deal (zombie_mode_effect_damage_blocked).
static MATRIX s_replayParent;
// A parent's snapshot lives until every effect referring to it has expired,
// including children which outlive the slot that created them.
static MATRIX s_replayFrames[65];
static bool   s_replayParentSet = false;
static bool   s_inEffects = false;

void zombie_mode_effects_running(bool on)
{
    s_inEffects = on;
}

bool zombie_mode_effect_damage_blocked(void)
{
    if (!s_inEffects || !s_replayParentSet || g_activeEffectIndex >= 64) return false;
    int parent = g_effectPool[g_activeEffectIndex].spriteInfo;
    if (parent == (int)&s_replayParent) return true;
    for (int i = 0; i < 65; i++) {
        if (parent == (int)&s_replayFrames[i]) return true;
    }
    return false;
}
bool zombie_mode_weapon_fx_begin(void)
{
    dbg_printf("[fx impact] p%d effect %u capture %d shared %d replay %d weapon %d\n",
        zm_net_self(), g_activeEffectIndex, s_fxCapture, s_shared,
        zombie_mode_effect_damage_blocked(), g_playerEntity.equippedWeaponId);
    if (!s_inEffects || s_fxCapture || zombie_mode_effect_damage_blocked()) return false;
    zombie_mode_fx_capture(true);
    return true;
}
void* zombie_mode_effect_replay_parent(void)
{
    return zombie_mode_effect_damage_blocked() ? &s_replayParent : NULL;
}
bool zombie_mode_effect_context_begin(void)
{
    if (!zombie_mode_effect_damage_blocked()) return false;
    s_fxSavedWeapon = g_playerEntity.equippedWeaponId;
    s_fxSavedAim = g_playerEntity.flags;
    s_fxSavedFloor = g_playerEntity.posY;
    g_playerEntity.equippedWeaponId = s_fxWeapon[g_activeEffectIndex];
    g_playerEntity.flags = s_fxAim[g_activeEffectIndex];
    g_playerEntity.posY = s_fxFloor[g_activeEffectIndex];
    return true;
}
void zombie_mode_effect_context_end(void)
{
    g_playerEntity.equippedWeaponId = s_fxSavedWeapon;
    g_playerEntity.flags = s_fxSavedAim;
    g_playerEntity.posY = s_fxSavedFloor;
}

static void zm_replay_effect(const short* a, int source)
{
    if (source < 0 || source >= ZM_NET_MAX_PLAYERS) return;
    bool frame = s_fxIncomingFrame[source];
    s_fxIncomingFrame[source] = false;
    if (a[6] != (short)(g_stageId | (g_roomId << 8))) return;
    VECTOR w;
    w.x = (unsigned short)a[2];       // room X/Z are 0..65535
    w.y = a[3];
    w.z = (unsigned short)a[4];
    w.pad = 0;
    if (!s_replayParentSet) {
        s_replayParent = g_identityMatrixData;
        s_replayParentSet = true;
    }
    MATRIX* parent = &s_replayParent;
    if (frame) {
        for (int i = 0; i < 65; i++) {
            bool used = false;
            for (int j = 0; j < 64; j++) {
                if (g_effectPool[j].animId != 0 &&
                    g_effectPool[j].spriteInfo == (int)&s_replayFrames[i]) {
                    used = true;
                    break;
                }
            }
            if (used) continue;
            parent = &s_replayFrames[i];
            *parent = s_fxIncomingParent[source];
            w.x = s_fxIncomingLocal[source].x;
            w.y = s_fxIncomingLocal[source].y;
            w.z = s_fxIncomingLocal[source].z;
            break;
        }
    }
    s_fxReplaying = true;
    unsigned char savedWeapon = g_playerEntity.equippedWeaponId;
    unsigned char savedAim = g_playerEntity.flags;
    unsigned short savedFloor = g_playerEntity.posY;
    const ZmNetPeerState* shooter = zm_net_player(source);
    g_playerEntity.equippedWeaponId = (unsigned char)(((unsigned short)a[5] >> 8) & 15);
    g_playerEntity.flags = (unsigned char)(((unsigned short)a[5] >> 7) & 0xE0);
    g_playerEntity.posY = frame ? s_fxIncomingFloor[source] :
        (shooter != NULL ? (unsigned short)shooter->y : 0);
    unsigned char slot = Effect_CreateBillboard((unsigned char)(a[0] & 0xFF), (unsigned char)((a[0] >> 8) & 0xFF),
                           a[1], parent, &w, (char)(a[5] & 0xFF));
    g_playerEntity.equippedWeaponId = savedWeapon;
    g_playerEntity.flags = savedAim;
    g_playerEntity.posY = savedFloor;
    if (slot < 64 && g_effectPool[slot].animId != 0) {
        g_effectPool[slot].animHeader[0] = (unsigned char)a[7];
        g_effectPool[slot].animHeader[3] = (unsigned char)((unsigned short)a[7] >> 8);
        s_fxWeapon[slot] = (unsigned char)(((unsigned short)a[5] >> 8) & 15);
        s_fxAim[slot] = (unsigned char)(((unsigned short)a[5] >> 7) & 0xE0);
        s_fxFloor[slot] = frame ? s_fxIncomingFloor[source] :
            (shooter != NULL ? (unsigned short)shooter->y : 0);
        dbg_printf("[fx] p%d replay from p%d type %d depth %d slot %d weapon %d aim %02X pos %d/%d/%d\n",
            zm_net_self(), source, a[0] & 255, ((unsigned short)a[0] >> 8), slot,
            s_fxWeapon[slot], s_fxAim[slot], w.x, w.y, w.z);
    }
    s_fxReplaying = false;
}

void zombie_mode_enemies_begin(void)
{
    if (g_zombieModeEntity != NULL) zm_survivor_enemies_begin();
    s_menuEnemiesRun = false;
    if (s_gameRole == ZM_NET_SURVIVOR && (g_main_state_flags & MSF_MENU_ACTIVE) != 0) {
        s_menuSavedMessageFlags = g_message_flags;
        g_message_flags |= 0x0004;
        s_menuEnemiesRun = true;
    }
}

void zombie_mode_enemies_end(void)
{
    if (g_zombieModeEntity != NULL) zm_survivor_enemies_end();
    if (s_menuEnemiesRun) {
        g_message_flags = s_menuSavedMessageFlags;
        s_menuEnemiesRun = false;
    }
}

void zm_text_encode(char* s)
{
    for (; *s != '\0'; s++) {
        switch (*s) {
        case '-':  *s = (char)95;  break;     // glyph 59, the dash
        case '/':  *s = (char)95;  break;     // no slash: a dash
        case '_':  *s = (char)95;  break;
        case '.':  *s = (char)157; break;     // glyph 121
        case ',':  *s = (char)60;  break;     // glyph 24
        case '!':  *s = (char)62;  break;     // glyph 26
        case '\'': *s = (char)94;  break;    // glyph 58
        case '(':  *s = (char)91;  break;     // glyph 55 (a raw '(' is a pad icon)
        case ')':  *s = (char)93;  break;     // glyph 57
        case '+':  *s = (char)161; break;     // glyph 125
        case '=':  *s = (char)162; break;     // glyph 126
        case '>':  *s = (char)38;  break;     // glyph 2, the arrow
        case '<':
        case '%':
        case '^':  *s = ' ';       break;
        default:   break;
        }
    }
}

void zm_draw_centered(const char* text, short y, unsigned char color)
{
    snprintf(PRINT_TEXT_BUFFER, sizeof(PRINT_TEXT_BUFFER), "%s", text);
    zm_text_encode(PRINT_TEXT_BUFFER);
    PrintText8x14((short)((320 - (int)strlen(text) * 8) / 2), y, color, 0);
}

// The keeper's: the time the clock has run since every survivor came in,
// the holds taken out.
static unsigned int zm_clock_run_ms(void)
{
    int in = zm_survivors_in_ms();
    if (in < 0) return 0;
    unsigned int held = s_clockHeldMs + (s_clockHeld ? zm_game_time_ms() - s_clockHeldAt : 0);
    return (unsigned int)in > held ? (unsigned int)in - held : 0;
}

static bool zm_clock_held(void)
{
    return zm_match_authority() ? s_clockHeld : s_clockRemoteHeld;
}

// Time left on the game clock (ZM_TIME_LIMIT_MS before it starts).
static unsigned int zm_clock_left_ms(void)
{
    if (zm_match_authority()) {
        if (zm_survivors_in_ms() < 0) return ZM_TIME_LIMIT_MS;
        unsigned int limit = ZM_TIME_LIMIT_MS + s_clockBonusMs, run = zm_clock_run_ms();
        return run >= limit ? 0 : limit - run;
    }
    if (!s_clockHave) return ZM_TIME_LIMIT_MS;
    unsigned int gone = s_clockRemoteHeld ? 0 : zm_game_time_ms() - s_clockAtMs;
    return gone >= s_clockLeftMs ? 0 : s_clockLeftMs - gone;
}

void zm_clock_add_bonus(unsigned int ms)
{
    if (!zm_match_authority()) return;
    s_clockBonusMs += ms;
    s_clockSentMs = 0;                  // out at once
    dbg_printf("[zombie] the game clock gains %u s\n", ms / 1000);
}

// Every player's: bottom right.
void zm_clock_draw(void)
{
    unsigned int sec = (zm_clock_left_ms() + 999) / 1000;
    char line[24];
    snprintf(line, sizeof(line), zm_clock_held() ? "PAUSED %u:%02u" : "TIME %u:%02u", sec / 60, sec % 60);
    snprintf(PRINT_TEXT_BUFFER, sizeof(PRINT_TEXT_BUFFER), "%s", line);
    zm_text_encode(PRINT_TEXT_BUFFER);
    PrintText8x14((short)(320 - 8 - (int)strlen(line) * 8), 222, 0x8F, 0);
    zm_yawn_draw();
}

// A survivor's room view: the room it is in, bottom left in green (the
// font's tint 1), on the clock's line.
static void zm_room_name_draw(void)
{
    const char* name = DebugRoom_Name(g_stageId, g_roomId);
    if (name != NULL) {
        snprintf(PRINT_TEXT_BUFFER, sizeof(PRINT_TEXT_BUFFER), "%s", name);
    } else {
        snprintf(PRINT_TEXT_BUFFER, sizeof(PRINT_TEXT_BUFFER), "ROOM %X%02X",
                 (unsigned int)(g_stageId + 1) & 0xF, (unsigned int)g_roomId);
    }
    zm_text_encode(PRINT_TEXT_BUFFER);
    PrintText8x14(8, 222, 1, 0);
}

void zombie_mode_draw_overlay(void)
{
    if (zombie_mode_armed() && zm_shotgun_draw()) return;
    zm_bumpers_draw_hud();                     // "SURVIVE!" as the match starts
    if (zm_bumpers_intro_playing()) return;    // the intro clips have the screen
    if (!s_winShown) zm_revive_draw();
    if (s_winShown) {
        if (zm_bumpers_end_playing()) return;   // the ending clip first
        zm_stats_draw(zm_game_time_ms() - s_winAtMs);
        return;
    }
    if (s_escapePending) {
        zm_draw_centered("WAITING FOR HOST RESULT", 96, 0);
        zm_clock_draw();
        return;
    }
    // A dead survivor stays in the match until it ends.
    if ((s_gameRole == ZM_NET_SURVIVOR) && g_playerEntity.health < 0) {
        if (zm_spec_draw()) {           // watching another survivor
            zm_clock_draw();
            return;
        }
        zm_draw_centered("YOU ARE DEAD", 96, 2);
        zm_draw_centered("WAITING FOR THE MATCH TO END", 116, 0);
        zm_clock_draw();
        return;
    }
    // Richard's radio: the map on a survivor's copy.
    if (s_gameRole == ZM_NET_SURVIVOR && zm_map_is_open()) {
        zm_map_draw();
        return;
    }
    if ((g_zombieModeEntity != NULL || s_directorMapOnly) && zm_map_is_open()) {
        zm_map_draw();
        zm_econ_draw();
        zm_ai_draw();
        zm_piano_draw();
        // The placements' notes, under the map's title.
        if (s_switchNoteFrames > 0 && s_noteText[0] != '\0') {
            s_switchNoteFrames--;
            snprintf(PRINT_TEXT_BUFFER, sizeof(PRINT_TEXT_BUFFER), "%s", s_noteText);
            zm_text_encode(PRINT_TEXT_BUFFER);
            PrintText8x14((short)((320 - (int)strlen(s_noteText) * 8) / 2), 26, 0x8F, 0);
        }
        return;
    }
    if (g_zombieModeEntity != NULL) {
        zm_survivor_draw_hud();
        zm_econ_draw();
        zm_ai_draw();
    }
    // The director: how long the main hall stays closed.
    unsigned int hallLeft = 0;
    if (s_gameRole != ZM_NET_SURVIVOR && zm_hall_closed(&hallLeft)) {
        char line[40];
        if (hallLeft == 0xFFFFFFFFu) snprintf(line, sizeof(line), "HALLS CLOSED - SURVIVORS ARRIVING");
        else snprintf(line, sizeof(line), "HALLS OPEN IN %u:%02u", (hallLeft + 999) / 1000 / 60, (hallLeft + 999) / 1000 % 60);
        zm_draw_centered(line, 208, 0x7F);
    }
    zm_piano_draw();
    zm_access_draw();
    zm_timeout_draw();
    zm_voice_draw();
    if (s_gameRole == ZM_NET_SURVIVOR) zm_room_name_draw();
    zm_clock_draw();
    if (s_switchNoteFrames > 0) {
        s_switchNoteFrames--;
        if (s_noteText[0] != '\0') {
            snprintf(PRINT_TEXT_BUFFER, sizeof(PRINT_TEXT_BUFFER), "%s", s_noteText);
        } else {
            snprintf(PRINT_TEXT_BUFFER, sizeof(PRINT_TEXT_BUFFER), "MONSTER %d OF %d",
                     s_switchNoteIndex, s_switchNoteCount);
        }
        int width = (int)strlen(PRINT_TEXT_BUFFER) * 8;
        zm_text_encode(PRINT_TEXT_BUFFER);
        PrintText8x14((short)(320 - 8 - width), 24, 1, 0);     // under the points
    }
}

// The survivors' goal: the mansion's back exit - the storeroom's (ROOM1B)
// door out of the mansion stage, to the courtyard. Opened by any survivor, it
// requests their win from the host (ZM_EV_WIN); the door stays shut until
// the host confirms the one final result for every copy.
static bool zm_is_last_door(const unsigned char* record)
{
    if (g_stageId % 5 != STAGE_MANSION_1F || g_roomId != ZM_BACK_EXIT_ROOM || (record[0x0B] & 0x80) != 0) {
        return false;
    }
    unsigned char stage, room;
    zm_decode_dest(record[0x0D], g_stageId, &stage, &room);
    return stage != g_stageId;
}

// The match clock: ms since every survivor came in (0 until then), on this
// copy's clock.
unsigned int zm_match_elapsed_ms(void)
{
    if (s_winShown) return s_finalElapsedMs;
    if (zm_match_authority()) {
        unsigned int limit = ZM_TIME_LIMIT_MS + s_clockBonusMs, run = zm_clock_run_ms();
        return run >= limit ? limit : run;
    }
    if (!s_clockHave) return 0;
    unsigned int gone = s_clockRemoteHeld ? 0 : zm_game_time_ms() - s_clockAtMs;
    if (gone > s_clockLeftMs) gone = s_clockLeftMs;
    return s_clockRunMs + gone;
}

// The match is over, for `reason` (ZM_END_*); `player` opened the back exit.
static void zm_match_end(int reason, int player, unsigned int elapsedMs)
{
    if (s_winShown) return;
    // Capture the last health/death change before freezing the stats.
    zm_stats_frame(elapsedMs);
    s_winShown = true;
    s_escapePending = false;
    s_finalElapsedMs = elapsedMs;
    s_winAtMs = zm_game_time_ms();
    s_winPlayer = player;
    s_winReason = reason;
    zm_stats_match_over(reason, player, elapsedMs);
    zm_bumpers_match_end(reason);
    dbg_printf("[zombie] the match is over: %s\n",
               reason == ZM_END_ESCAPE ? "a survivor escaped" :
               reason == ZM_END_UNSOLVABLE ? "required items lost; escape is impossible" :
               reason == ZM_END_TIME ? "time is up" : "every survivor is dead");
}

// Only the host chooses a multiplayer outcome. Its seed scopes both requests
// and confirmations to this match. A confirmed outcome is immutable.
static void zm_host_finish(int reason, int player)
{
    if (s_winShown || !zm_match_authority()) return;
    unsigned int elapsed = zm_match_elapsed_ms();
    if (zm_net_role() == ZM_NET_ZOMBIE) {
        unsigned int seed = zm_net_seed();
        zm_net_send_event8(ZM_EV_WIN, (short)player, (short)reason, (short)(elapsed / 1000),
                          (short)seed, (short)(seed >> 16), 0, 0, 0);
    }
    zm_match_end(reason, player, elapsed);
}

void zm_match_progression_lost(void)
{
    zm_host_finish(ZM_END_UNSOLVABLE, -1);
}

// Reliable WIN events are handled immediately by ZombieNet, so a full inbox
// of combat/FX events cannot discard the decisive request or confirmation.
bool zm_match_take_win(const short* a, int src)
{
    // A connected survivor may still be loading/choosing when the match ends.
    // Let the network retain the confirmation until this copy enters the game.
    if (!s_zombieModeArmed || s_gameRole == ZM_NET_OFF) return false;
    if (s_winShown) return true;
    unsigned int seed = zm_net_seed();
    if (zm_net_role() == ZM_NET_ZOMBIE) {
        // The request includes the alive survivor's exit-room snapshot. Do
        // not reject an escape on a newer dead STATE from the same poll: the
        // survivor may have been hit after it successfully attempted the exit.
        unsigned int requestSeed = (unsigned short)a[4] | ((unsigned int)(unsigned short)a[5] << 16);
        unsigned char stage = (unsigned char)a[2], room = (unsigned char)((unsigned short)a[2] >> 8);
        if (src < 0 || src >= ZM_NET_MAX_PLAYERS || a[0] != src || a[1] != ZM_END_ESCAPE ||
            requestSeed != seed || zm_shotgun_crushed(src) || zm_net_char(src) < 0 || zm_seat_state(src) == NULL ||
            a[3] < 0 || a[3] > 255 || room != ZM_BACK_EXIT_ROOM ||
            (stage != STAGE_MANSION_1F && stage != STAGE_MANSION_RETURN_1F)) return true;
        zm_host_finish(ZM_END_ESCAPE, src);
    } else if (s_gameRole == ZM_NET_SURVIVOR && src == ZM_NET_DIRECTOR) {
        unsigned int resultSeed = (unsigned short)a[3] | ((unsigned int)(unsigned short)a[4] << 16);
        if (resultSeed != seed || (a[1] != ZM_END_UNSOLVABLE &&
            (a[1] < ZM_END_ESCAPE || a[1] > ZM_END_ALL_DEAD)) ||
            (unsigned short)a[2] > ZM_TIME_MAX_MS / 1000) return true;
        if (a[1] == ZM_END_ESCAPE ? (a[0] < 0 || a[0] >= ZM_NET_MAX_PLAYERS || zm_net_char(a[0]) < 0)
                                  : a[0] != -1) return true;
        zm_match_end(a[1], a[1] == ZM_END_ESCAPE ? a[0] : -1, (unsigned short)a[2] * 1000u);
    }
    return true;
}

// Every survivor is dead: the director's copy (single player: the AI
// survivor). Only once they are all in; one that left the game no longer
// counts.
static bool zm_all_survivors_dead(void)
{
    if (zm_survivors_in_ms() < 0) return false;
    if (s_gameRole == ZM_NET_OFF) return g_playerEntity.health < 0;      // the AI survivor, here or parked
    ZmSurvivorInfo surv[ZM_NET_MAX_PLAYERS];
    int n = zm_survivor_list(surv, ZM_NET_MAX_PLAYERS);
    if (n == 0) return false;
    for (int i = 0; i < n; i++) {
        if (!surv[i].dead) return false;
    }
    return true;
}

// Once a frame, the director's copy and single player: send the clock, and
// end the game at zero.
static void zm_clock_frame(void)
{
    if (!s_zombieModeArmed || s_winShown || !zm_match_authority()) return;
    if (zm_survivors_in_ms() < 0) return;
    unsigned int now = zm_game_time_ms();
    // A boss fight holds the clock (ZombieYawn.cpp); the survivors hear at once.
    bool hold = zm_yawn_clock_hold();
    if (hold != s_clockHeld) {
        if (hold) s_clockHeldAt = now;
        else s_clockHeldMs += now - s_clockHeldAt;
        s_clockHeld = hold;
        s_clockSentMs = 0;
        dbg_printf("[zombie] the game clock %s\n", hold ? "stands still" : "runs again");
    }
    unsigned int left = zm_clock_left_ms();
    if (zm_net_role() == ZM_NET_ZOMBIE && (s_clockSentMs == 0 || now - s_clockSentMs >= ZM_CLOCK_SEND_MS)) {
        zm_net_send_event(ZM_EV_CLOCK, (short)((left + 999) / 1000), (short)(hold ? 1 : 0),
                          (short)(zm_match_elapsed_ms() / 1000), 0);
        s_clockSentMs = now != 0 ? now : 1;
    }
    int reason = left == 0 ? ZM_END_TIME : zm_all_survivors_dead() ? ZM_END_ALL_DEAD : -1;
    if (reason >= 0) {
        zm_host_finish(reason, -1);
    }
}

// Once a frame, every copy: the end screen's numbers, and leaving it.
static void zm_end_frame(void)
{
    if (!s_zombieModeArmed) return;
    zm_stats_frame(zm_match_elapsed_ms());
    if (!s_winShown || s_endLeaving || zm_bumpers_end_playing()) return;
    unsigned int since = zm_game_time_ms() - s_winAtMs;
    if ((zm_stats_prompt_up(since) && (g_PlayerPadPressed & 0x0080) != 0) || since >= ZM_END_LEAVE_MS) {
        s_endLeaving = true;
        g_main_state_flags |= MSF_PLAYER_DEAD;
    }
}

bool zombie_mode_hold_death(void)
{
    // The mode ends the game itself (zm_end_frame): a dead survivor stays.
    return s_zombieModeArmed && !s_endLeaving;
}

bool zombie_mode_match_over(void)
{
    return s_zombieModeArmed && s_winShown;
}

void zm_reconnect_match_clock(int elapsedMs)
{
    unsigned int now = zm_game_time_ms();
    s_clockLeftMs = elapsedMs < 0 || elapsedMs >= ZM_TIME_LIMIT_MS ? (elapsedMs < 0 ? ZM_TIME_LIMIT_MS : 0)
                                                                    : ZM_TIME_LIMIT_MS - (unsigned int)elapsedMs;
    s_clockRunMs = elapsedMs < 0 ? 0 : (unsigned int)elapsedMs;
    s_clockRemoteHeld = false;          // the host's next CLOCK puts the bonus and holds right
    s_clockAtMs = now; s_clockHave = elapsedMs >= 0;
    s_gameStartMs = now - (elapsedMs < 0 ? 0 : (unsigned int)elapsedMs);
    s_allSpawnedMs = elapsedMs < 0 ? 0 : now - (unsigned int)elapsedMs;
}

void zm_reconnect_failed(void)
{
    if (s_gameRole == ZM_NET_SURVIVOR && !s_winShown)
        zm_match_end(ZM_END_CONNECTION, -1, zm_match_elapsed_ms());
}

// door_try_enter: a survivor's door the director's LOCK DOORS trap holds
// shut (ZombieTraps.cpp). Each refusal plays its door sound here.
bool zombie_mode_door_trapped(const unsigned char* record)
{
    if (!s_zombieModeArmed) return false;
    if (zm_shotgun_door(record)) {
        play_sfx(2, 0x14, 0);
        return true;
    }
    if (s_gameRole != ZM_NET_SURVIVOR) return false;
    // No way out of a boss room while its Yawn is there or its lockdown
    // holds, and the lesson room's door for good once Yawn 2 is beaten
    // (ZombieYawn.cpp).
    if (zm_yawn_traps_exit(g_stageId, g_roomId) && (record[0x0B] & 0x80) == 0) {
        if (zm_yawn_hole_door(record)) {
            zm_message_show(s_holeRegroup);
            return true;
        }
        zm_yawn_blocked_sound();
        zm_message_show(s_bossDoorClosed);
        return true;
    }
    if (zm_yawn_lesson_sealed(g_stageId, g_roomId, record[0x0D]) && (record[0x0B] & 0x80) == 0) {
        zm_yawn_blocked_sound();
        zm_message_show(s_bossDoorBlocked);
        return true;
    }
    // The small elevator before the battery is in (ZombieKeypad.cpp).
    if (zm_access_door_refused(record)) {
        play_sfx(2, 0x14, 0);
        return true;
    }
    unsigned int left = 0;
    if (!zm_trap_door_locked(g_stageId, g_roomId, record[0x0D], record[0x0B], &left)) return false;
    play_sfx(2, 0x14, 0);
    char line[48];
    snprintf(line, sizeof(line), "THE DOOR IS JAMMED - %u S", (left + 999) / 1000);
    zm_note(line);
    return true;
}

bool zombie_mode_door_ignores_lock(const unsigned char* record)
{
    if (!s_zombieModeArmed || !s_zombieOpeningDoor) return false;
    // A door into a room that is only a stub file (a locked-for-good door can
    // lead to one) keeps its lock.
    unsigned char stage, room;
    zm_decode_dest(record[0x0D], g_stageId, &stage, &room);
    return !zm_random_room_stub(stage, room);
}

static void zm_request_escape(void)
{
    if (s_winShown || s_escapePending || g_playerEntity.health < 0) return;
    s_escapePending = true;
    unsigned int seed = zm_net_seed();
    zm_net_send_event_to(ZM_NET_DIRECTOR, ZM_EV_WIN, (short)zm_net_self(), ZM_END_ESCAPE,
                         (short)(g_stageId | (g_roomId << 8)), g_playerEntity.health,
                         (short)seed, (short)(seed >> 16), 0, 0);
}

bool zombie_mode_door_begin(const unsigned char* record)
{
    if (s_gameRole == ZM_NET_SURVIVOR && zm_is_last_door(record)) {
        zm_request_escape();
        return true;
    }
    // The director's own door into the main hall, while it is closed.
    if (s_zombieOpeningDoor && (record[0x0B] & 0x80) == 0 && zm_hall_closed(NULL)) {
        unsigned char stage, room;
        zm_decode_dest(record[0x0D], g_stageId, &stage, &room);
        if (zm_is_hall(stage, room)) {
            zm_note_hall_closed();
            return true;
        }
    }
    // The director's own door into a safe room: never.
    if (s_zombieOpeningDoor && (record[0x0B] & 0x80) == 0) {
        unsigned char stage, room;
        zm_decode_dest(record[0x0D], g_stageId, &stage, &room);
        if (zm_director_safe_room(stage, room)) {
            zm_note("MONSTERS CANNOT ENTER A SAFE ROOM");
            return true;
        }
        if (g_zombieModeEntity != NULL && zm_hall_2f_refuses(stage, room, g_zombieModeEntity->id)) {
            zm_note("ONLY ZOMBIES AND HUNTERS IN THE 2F HALL");
            return true;
        }
    }
    if (g_zombieModeEntity == NULL || s_zombieOpeningDoor) {
        if (s_gameRole == ZM_NET_ZOMBIE && s_zombieOpeningDoor &&
            (record[0x0B] & 0x80) == 0) {
            zm_net_send_state(g_zombieModeEntity, true, NULL, -1, true);
        }
        if (s_gameRole == ZM_NET_SURVIVOR && (record[0x0B] & 0x80) == 0) {
            // Send before the door task sleeps: peers hide the outgoing body
            // throughout the animation, until gameplay resumes in the new room.
            zm_net_send_state((const Entity*)&g_playerEntity, true, NULL, -1, true);
        }
        return false;           // the zombie's own door, or the mod is off
    }
    return zm_survivor_take_door(record);
}

bool zombie_mode_grab_mash(char* reduce)
{
    if (g_zombieModeEntity == NULL) {
        return false;
    }
    *reduce = zm_survivor_mash();
    return true;
}

// ===========================================================================
// Room action probes from the zombie's position
// ===========================================================================

// How far ahead the action-press probes look. The player's is 600
// (check_action_object), but the zombie's collision radius (422) is larger
// than the characters', so walls and stair rails stop it further from the
// zones the scripts lay out for the player - in 2F Right Stairs it stands
// ~1150 short of the event zones. zm_action_pressed tries 600 first and
// widens to ZM_PRESS_REACHES' later entries only if nothing is in reach.
static int s_pressReach = 600;
static const int ZM_PRESS_REACHES[3] = { 600, 900, 1200 };

// is_point_in_action_zone (0x0041b3c0), unsigned compares and all.
static bool zm_in_action_zone(int x, int z, const unsigned short* zone)
{
    return (unsigned int)(x - (int)zone[0]) <= (unsigned int)zone[2] &&
           (unsigned int)(z - (int)zone[1]) <= (unsigned int)zone[3];
}

// The action-press probe of check_action_object (0x0041c150) from the
// zombie's position and facing, restricted to doors (handler 1,
// door_try_enter): doors, stairs doors, locked doors and their key prompts.
// Items, typewriters, the item box and examine messages are left alone - a
// zombie does not pick things up. Returns true if a door entry was reached.
static bool zm_try_door(Entity* e)
{
    if (g_RoomActionTail == NULL || (unsigned char*)g_RoomActionTail < g_RoomActionTable) {
        return false;
    }

    SVECTOR reach = { (short)s_pressReach, 0, 0, 0 };
    MovePlayerXZ(e->angle, &reach, &reach);
    int probeX = reach.x + e->scaMatrixData.localMatrix.t[0];
    int probeZ = reach.z + e->scaMatrixData.localMatrix.t[2];

    unsigned char* entry = g_RoomActionTable;
    char index = 0;
    do {
        unsigned char flags = entry[1];
        if (entry[0] == 1 && (flags & 1) != 0 && (flags & 0x80) != 0) {
            const unsigned short* zone = *(unsigned short**)(entry + 8);
            bool hit;
            if ((flags & 0x40) == 0) {
                hit = zm_in_action_zone(probeX, probeZ, zone);
                if (hit) g_fwdPosActionId = (unsigned char)(index + 1);
            } else {
                hit = zm_in_action_zone(e->scaMatrixData.localMatrix.t[0],
                                        e->scaMatrixData.localMatrix.t[2], zone);
                if (hit) g_entPosActionId = (unsigned char)(index + 1);
            }
            if (hit) {
                zm_survivor_note_zombie_door();
                g_playerPosScratch.x = probeX;
                g_playerPosScratch.z = probeZ;
                s_zombieOpeningDoor = true;
                ((int(*)(unsigned char*))room_check_actions[1])(entry);
                s_zombieOpeningDoor = false;
                ENTITY = e;     // the transition may have retargeted it
                return true;
            }
        }
        entry += 12;
        index++;
    } while (entry <= (unsigned char*)g_RoomActionTail);
    return false;
}

// stairs_height_update (0x0041bf90, room_check_actions[0x11]) for the zombie:
// the same ramp, read off the zombie's own position instead of the player's.
// The original writes the result to the player, who is not the zombie.
static void zm_stairs_height(Entity* e)
{
    if (g_RoomActionTail == NULL || (unsigned char*)g_RoomActionTail < g_RoomActionTable) {
        return;
    }
    SVECTOR reach = { 600, 0, 0, 0 };
    MovePlayerXZ(e->angle, &reach, &reach);
    int probeX = reach.x + e->scaMatrixData.localMatrix.t[0];
    int probeZ = reach.z + e->scaMatrixData.localMatrix.t[2];
    int x = e->scaMatrixData.localMatrix.t[0];
    int z = e->scaMatrixData.localMatrix.t[2];

    unsigned char* entry = g_RoomActionTable;
    do {
        unsigned char flags = entry[1];
        if (entry[0] == 0x11 && (flags & 1) != 0 && (flags & 0x80) == 0) {
            unsigned short* zone = *(unsigned short**)(entry + 8);
            bool hit = ((flags & 0x40) == 0) ? zm_in_action_zone(probeX, probeZ, zone)
                                             : zm_in_action_zone(x, z, zone);
            if (hit) {
                int along;
                switch (*(unsigned short*)(entry + 2)) {
                case 0:  along = x - (int)zone[0]; break;
                case 1:  along = ((int)zone[2] + (int)zone[0]) - x; break;
                case 2:  along = z - (int)zone[1]; break;
                default: along = ((int)zone[1] + (int)zone[3]) - z; break;
                }
                int stepLen = (int)*(unsigned short*)(entry + 4);
                if (stepLen != 0) {
                    e->scaMatrixData.localMatrix.t[1] =
                        ((short)(along / stepLen) + 1) * (int)*(short*)(entry + 6);
                }
                return;
            }
        }
        entry += 12;
    } while (entry <= (unsigned char*)g_RoomActionTail);
}

// ===========================================================================
// In-room steps: check_door (0x0041b6d0, room_check_actions[5]) and
// check_door_side (0x0041b790, [6])
//
// The small flights of steps inside a room (Main Hall 2F's side landings, for
// one) are not doors. On the action press the player's check_door picks the
// side it came from, player_door_open_sequence (0x00457390, behaviour 0x11)
// squares it up to the axis, plays a step animation from the room's own player
// set (jointMoveData2/3) and then warps it across: 0x10FE along the facing
// axis and 0xB45 up or down (0x842 / 0x57A in rooms 5 and 0xE).
//
// The zombie does the same move with the same numbers, walking its own
// shamble across the distance instead of teleporting at the end of Chris's
// animation - that animation drives a skeleton whose joints are laid out
// differently (Chris: legs 3-8 under a pelvis joint, arms 9-14 on the root;
// the zombie: arms 3-8 on the torso, legs 9-14 on the root), so it cannot pose
// the zombie.
// ===========================================================================
#define ZM_STEP_SPEED   40          // units per frame across the step

static int  s_stepFrom[3], s_stepTo[3];
static int  s_stepFrames;

// Which way check_door / check_door_side would send the player for this entry,
// from the zombie's position and facing. False when the facing test fails (the
// handler then sends nobody anywhere either).
static bool zm_step_vector(Entity* e, const unsigned char* entry, int* dx, int* dy, int* dz)
{
    const unsigned short* rec = *(const unsigned short* const*)(entry + 8);
    int angle = (unsigned short)e->angle;
    int appr = 0;
    if (entry[0] == 5) {
        // check_door: which half of the zone along X, and facing into it.
        if ((e->scaMatrixData.localMatrix.t[0] - (int)rec[0]) < (int)(rec[2] >> 1)) {
            if (((angle + 0x400) & 0x800) == 0) appr = 1;
        } else {
            if (((angle - 0x400) & 0x800) == 0) appr = -1;
        }
    } else {
        // check_door_side: the same along Z.
        if ((e->scaMatrixData.localMatrix.t[2] - (int)rec[1]) < (int)(rec[3] >> 1)) {
            if (((angle + 0x800) & 0x800) == 0) appr = 1;
        } else {
            if ((angle & 0x800) == 0) appr = -1;
        }
    }
    if (appr == 0) return false;

    // zoneFlags 0x10, "the other way": ((appr >> 1) ^ entry word +2) & 1.
    bool otherWay = (((appr >> 1) ^ *(const unsigned short*)(entry + 2)) & 1) != 0;

    // Case 0 squares the facing up to an axis before case 3 reads bit 0x400.
    e->angle = (short)((angle + 0x200) & 0xC00);
    bool sideways = (e->angle & 0x400) != 0;

    int along = 0x10FE, rise = 0xB45;
    if (g_roomId == ROOM_DINING_ROOM || g_roomId == ROOM_KEEPERS_BEDROOM) {
        along = 0x842;
        rise = 0x57A;
    }
    *dx = sideways ? 0 : appr * along;
    *dz = sideways ? appr * along : 0;
    *dy = otherWay ? rise : -rise;
    return true;
}

// The action-press probe again, for the step handlers. True if a step began.
static bool zm_try_step(Entity* e)
{
    if (g_RoomActionTail == NULL || (unsigned char*)g_RoomActionTail < g_RoomActionTable) {
        return false;
    }
    SVECTOR reach = { (short)s_pressReach, 0, 0, 0 };
    MovePlayerXZ(e->angle, &reach, &reach);
    int probeX = reach.x + e->scaMatrixData.localMatrix.t[0];
    int probeZ = reach.z + e->scaMatrixData.localMatrix.t[2];

    unsigned char* entry = g_RoomActionTable;
    do {
        unsigned char flags = entry[1];
        if ((entry[0] == 5 || entry[0] == 6) && (flags & 1) != 0 && (flags & 0x80) != 0) {
            const unsigned short* zone = *(unsigned short**)(entry + 8);
            bool hit = ((flags & 0x40) == 0)
                ? zm_in_action_zone(probeX, probeZ, zone)
                : zm_in_action_zone(e->scaMatrixData.localMatrix.t[0],
                                    e->scaMatrixData.localMatrix.t[2], zone);
            int dx, dy, dz;
            if (hit && zm_step_vector(e, entry, &dx, &dy, &dz)) {
                for (int i = 0; i < 3; i++) s_stepFrom[i] = e->scaMatrixData.localMatrix.t[i];
                s_stepTo[0] = s_stepFrom[0] + dx;
                s_stepTo[1] = s_stepFrom[1] + dy;
                s_stepTo[2] = s_stepFrom[2] + dz;
                int dist = SquareRoot0(dx * dx + dz * dz);
                s_stepFrames = dist / ZM_STEP_SPEED;
                if (s_stepFrames < 20) s_stepFrames = 20;
                return true;
            }
        }
        entry += 12;
    } while (entry <= (unsigned char*)g_RoomActionTail);
    return false;
}

// ===========================================================================
// Room events on the action press: create_room_event (0x0041b9e0,
// room_check_actions[9])
//
// Some rooms move the player through a scripted event instead of a door or a
// step - 2F Right Stairs (ROOM2070) disarms its step entry (room_action_arm
// 05 00 81: handler 0) and lays two action-press events over the same spot,
// event scripts 1 and 0, which walk the player down or up the short flight.
//
// The zombie borrows that: the parked player entity is put where the zombie
// stands, the event is started on it (ScdEventEntry_Init takes ENTITY as the
// event's entity), and while the event runs the hidden player does the walk -
// the room's own script, the player's own SCD behaviours - and the zombie
// follows its position and height, shambling. Only while the survivor is in
// another room: when it is here, the player entity is the survivor.
// ===========================================================================
bool zm_zombie_riding(void)
{
    return s_riding;
}

void zm_zombie_ride_follow(int x, int y, int z, short angle, bool moving)
{
    Entity* e = g_zombieModeEntity;
    if (e == NULL || !s_riding) return;
    e->scaMatrixData.localMatrix.t[0] = x;
    e->scaMatrixData.localMatrix.t[1] = y;
    e->scaMatrixData.localMatrix.t[2] = z;
    e->angle = angle;
    zm_set_rollback(e);
    unsigned char anim = moving ? ZM_ANIM_SHAMBLE : ZM_ANIM_IDLE;
    if (e->animationId != anim) {
        e->animationId = anim;
        e->animation_frame_id = 0;
        e->timing_control = 0;
        e->blend_counter = 3;
    }
}

void zm_zombie_ride_end(void)
{
    s_riding = false;
    Entity* e = g_zombieModeEntity;
    if (e == NULL) return;
    zm_set_rollback(e);
    e->action_behavior = 0;      // ZM_ACT_IDLE
    e->action_state = 0;
    dbg_printf("[zombie] event ride over at (%d,%d,%d)\n",
               (int)e->scaMatrixData.localMatrix.t[0], (int)e->scaMatrixData.localMatrix.t[1],
               (int)e->scaMatrixData.localMatrix.t[2]);
}

static bool zm_try_event(Entity* e)
{
    if (zm_survivor_present() || g_RoomActionTail == NULL ||
        (unsigned char*)g_RoomActionTail < g_RoomActionTable) {
        return false;
    }
    SVECTOR reach = { (short)s_pressReach, 0, 0, 0 };
    MovePlayerXZ(e->angle, &reach, &reach);
    int probeX = reach.x + e->scaMatrixData.localMatrix.t[0];
    int probeZ = reach.z + e->scaMatrixData.localMatrix.t[2];

    unsigned char* entry = g_RoomActionTable;
    char index = 0;
    do {
        unsigned char flags = entry[1];
        if (entry[0] == 9 && (flags & 1) != 0 && (flags & 0x80) != 0) {
            const unsigned short* zone = *(unsigned short**)(entry + 8);
            bool hit;
            if ((flags & 0x40) == 0) {
                hit = zm_in_action_zone(probeX, probeZ, zone);
                if (hit) g_fwdPosActionId = (unsigned char)(index + 1);
            } else {
                hit = zm_in_action_zone(e->scaMatrixData.localMatrix.t[0],
                                        e->scaMatrixData.localMatrix.t[2], zone);
                if (hit) g_entPosActionId = (unsigned char)(index + 1);
            }
            if (hit) {
                zm_survivor_begin_ride(e);
                ENTITY = (Entity*)&g_playerEntity;
                ((int(*)(unsigned char*))room_check_actions[9])(entry);
                ENTITY = e;
                s_riding = true;
                dbg_printf("[zombie] riding room event (entry %d, script %d)\n",
                           (int)index, (int)*(unsigned short*)(entry + 4));
                return true;
            }
        }
        entry += 12;
        index++;
    } while (entry <= (unsigned char*)g_RoomActionTail);
    return false;
}

// ===========================================================================
// Control
// ===========================================================================
static void zm_start(Entity* e, unsigned char action, unsigned char anim)
{
    e->action_behavior = action;
    e->action_state = 0;
    e->animationId = anim;
    e->animation_frame_id = 0;
    e->timing_control = 0;
    e->blend_counter = 3;
    e->action_ticks_counter = 0;
}

static bool zm_is_prone(const Entity* e)
{
    return e->action_behavior >= ZM_ACT_LIE_DOWN && e->action_behavior <= ZM_ACT_GET_UP;
}

static void zm_steer(Entity* e, unsigned int held, short rate)
{
    if ((held & ZM_PAD_TURN_A) != 0) e->angle = (short)((e->angle + rate) & 0xFFF);
    if ((held & ZM_PAD_TURN_B) != 0) e->angle = (short)((e->angle - rate) & 0xFFF);
}

static unsigned char zm_joint_move(Entity* e, char reverse)
{
    return (unsigned char)Joint_move(reverse, e->animHeader, e->animBase, 0x400);
}

// Angle from the zombie to (x, z) relative to its facing, folded to
// -0x800..0x7FF. ENTITY must be the zombie (getAngleTowardsTarget reads it).
static int zm_relative_angle(Entity* self, int x, int z)
{
    unsigned short to = getAngleTowardsTarget(x, z);
    int rel = ((int)to - (int)(unsigned short)self->angle) & 0xFFF;
    return rel >= 0x800 ? rel - 0x1000 : rel;
}

// The nearest damageable enemy in front of the zombie's hands, or NULL.
static Entity* zm_find_target(Entity* self)
{
    int selfRadius = *(short*)(self->Sca_info + 10);
    Entity* best = NULL;
    int bestDist = 0x7FFFFFFF;

    for (int i = 0; i < 30; i++) {
        Entity* e = &g_EnemiesList[i];
        if (e == self || (e->status_flags & ENTITY_STATUS_ACTIVE) == 0) continue;
        if ((e->status_flags & (0x02 | ENTITY_STATUS_DEAD)) != 0) continue;
        if (e->id >= NPC_ENTITIES_IDS || e->health < 0 || e->hit_state != 0 || zm_yawn_entity(e)) continue;

        int dx = e->scaMatrixData.localMatrix.t[0] - self->scaMatrixData.localMatrix.t[0];
        int dz = e->scaMatrixData.localMatrix.t[2] - self->scaMatrixData.localMatrix.t[2];
        int dist = SquareRoot0(dx * dx + dz * dz);
        int reach = selfRadius + *(short*)(e->Sca_info + 10) + ZM_ATTACK_REACH;
        if (dist > reach || dist >= bestDist) continue;

        int rel = zm_relative_angle(self, e->scaMatrixData.localMatrix.t[0],
                                    e->scaMatrixData.localMatrix.t[2]);
        if (rel < -ZM_ATTACK_HALF_ARC || rel > ZM_ATTACK_HALF_ARC) continue;

        best = e;
        bestDist = dist;
    }
    return best;
}

// Is the survivor close enough, and in front, for the real grab?
static bool zm_can_grab_survivor(Entity* self)
{
    // Networked, the target is whichever survivor zm_target_begin put in the
    // player entity for this update.
    bool here = (s_gameRole == ZM_NET_OFF) ? zm_survivor_present() : (s_targetSwapped && s_targetIdx >= 0);
    if (!here || g_playerEntity.health < 0) return false;
    // In single player the player entity's own state says whether it is free.
    // With two players it is a puppet; the survivor's copy decides (and
    // refuses a grab it cannot take).
    if (s_gameRole == ZM_NET_OFF &&
        (g_playerEntity.isBeingAttackedFlag != 0 || g_playerEntity.animationId == 5)) return false;
    int dx = g_playerEntity.scaMatrixData.localMatrix.t[0] - self->scaMatrixData.localMatrix.t[0];
    int dz = g_playerEntity.scaMatrixData.localMatrix.t[2] - self->scaMatrixData.localMatrix.t[2];
    if (SquareRoot0(dx * dx + dz * dz) > ZM_GRAB_SURVIVOR_RANGE) return false;
    int rel = zm_relative_angle(self, g_playerEntity.scaMatrixData.localMatrix.t[0],
                                g_playerEntity.scaMatrixData.localMatrix.t[2]);
    return rel >= -ZM_GRAB_SURVIVOR_ARC && rel <= ZM_GRAB_SURVIVOR_ARC;
}

// Hand the zombie to zombie_attack (state 5, 0x004342e0 -> 0x00435200): the
// roar, the grab, the paired player animation, the bites and the release are
// all the original's. zombie_chase_player enters it with a word store of 5 at
// +0x84, which clears ignore_player_flag too.
static void zm_grab_survivor(Entity* self)
{
    if (s_gameRole == ZM_NET_ZOMBIE) {
        // The survivor's copy runs it; this zombie is posed from there until
        // ZM_EV_GRAB_END comes back.
        zm_net_send_event_to(s_targetIdx, ZM_EV_GRAB,
                             (self->behavior_flags & ZOMBIE_FLAG_LAYING_DOWN) ? 1 : 0,
                             (short)(self - g_EnemiesList), 0, 0, 0, 0, 0, 0);
        s_remoteGrabVictim = s_targetIdx;
        s_remoteGrab = true;
        s_remoteGrabSeen = false;
        s_remoteGrabMs = zm_game_time_ms();
        dbg_printf("[zombie] grab requested\n");
        return;
    }
    self->state = ZOMBIE_STATE_ATTACK;
    self->ignore_player_flag = 0;
    self->action_behavior = 0;
    self->action_state = 0;
    s_engineOwned = true;
}

// Hand a hit to the target's own state machine, the way apply_weapon_damage
// (0x0043c020) finishes one: hit_state set, then state 2 (damaged) or 3
// (dead). hit_state 0x09 = reaction type 1 in the knife phase (weapon 1 << 3),
// the zombie's short push-back.
static void zm_apply_reaction(Entity* target)
{
    target->hit_state = 0x09;
    target->ignore_player_flag = 0;
    target->action_behavior = 0;
    target->action_state = 0;
    target->state = (target->health >= 0) ? ZOMBIE_STATE_DAMAGED : ZOMBIE_STATE_DIE;
}

// A single swipe, for targets that are not zombies (no hold for them: state 4
// is only "do nothing" in the zombie's own state table).
static void zm_swipe(Entity* self, Entity* target)
{
    Snd_em(3);
    VECTOR bloodPos = { target->scaMatrixData.localMatrix.t[0],
                        target->scaMatrixData.localMatrix.t[1] - 1400,
                        target->scaMatrixData.localMatrix.t[2], 0 };
    Effect_CreateBillboard(0, 0, (short)(self->angle + 2048), (void*)g_deadMoveValue, &bloodPos, 0);
    target->health = (short)(target->health - ZM_ATTACK_DAMAGE);
    zm_apply_reaction(target);
    dbg_printf("[zombie] hit entity id %d, health now %d\n", (int)target->id, (int)target->health);
}

// Put the held victim in front of the biter, facing it, and step its pose.
// Its own update is parked on state 4 (zombie_no_action, a bare RET) for the
// duration, which also makes ResolveEntityScaCollision ignore it.
static void zm_hold_victim(Entity* self, Entity* victim)
{
    SVECTOR ahead = { ZM_BITE_HOLD_DIST, 0, 0, 0 };
    MovePlayerXZ(self->angle, &ahead, &ahead);
    victim->scaMatrixData.localMatrix.t[0] = self->scaMatrixData.localMatrix.t[0] + ahead.x;
    victim->scaMatrixData.localMatrix.t[1] = self->scaMatrixData.localMatrix.t[1];
    victim->scaMatrixData.localMatrix.t[2] = self->scaMatrixData.localMatrix.t[2] + ahead.z;
    victim->angle = (short)((self->angle + 0x800) & 0xFFF);
    zm_set_rollback(victim);

    Entity* saved = ENTITY;
    ENTITY = victim;
    Joint_move(0, victim->animHeader, victim->animBase, 0x400);
    ENTITY = saved;
}

static void zm_begin_bite(Entity* self, Entity* victim)
{
    s_biteVictim = victim;
    victim->state = ZOMBIE_STATE_DEAD_ANIM;     // 4: zombie_no_action
    victim->ignore_player_flag = 1;
    victim->action_behavior = 0;
    victim->action_state = 0;
    victim->animationId = 0;                    // standing idle
    victim->animation_frame_id = 0;
    victim->timing_control = 0;
    victim->blend_counter = 3;

    self->action_state = 1;
    self->animationId = ZM_ANIM_BITE;
    self->animation_frame_id = 0;
    self->timing_control = 0;
    self->blend_counter = 3;
    self->action_ticks_counter = 0;
    zm_hold_victim(self, victim);
}

static void zm_end_bite(Entity* self)
{
    Entity* victim = s_biteVictim;
    s_biteVictim = NULL;
    if (victim != NULL) {
        zm_apply_reaction(victim);
        dbg_printf("[zombie] released entity id %d, health %d\n",
                   (int)victim->id, (int)victim->health);
    }
    zm_start(self, ZM_ACT_IDLE, ZM_ANIM_IDLE);
}

// zombie_attack case 3, aimed at another zombie: bite on the first frame and
// every 19 after, blood from the biter's head joint (the same +0x150 the
// original's bite billboard uses), then let go.
static void zm_bite_update(Entity* self)
{
    Entity* victim = s_biteVictim;
    if (victim == NULL || (victim->status_flags & ENTITY_STATUS_ACTIVE) == 0) {
        s_biteVictim = NULL;
        zm_start(self, ZM_ACT_IDLE, ZM_ANIM_IDLE);
        return;
    }

    unsigned short tick = self->action_ticks_counter++;
    zm_joint_move(self, 0);
    zm_hold_victim(self, victim);

    if (tick % ZM_BITE_INTERVAL == 0) {
        Snd_em(3);
        Effect_CreateBillboard(0, 0, (short)(self->angle + 2048), (void*)g_deadMoveValue,
                               (void*)((int)self->jointsStructs + 0x150), 0);
        victim->health = (short)(victim->health - ZM_BITE_DAMAGE);
    }

    if (victim->health < 0 || tick >= ZM_BITE_FRAMES) {
        zm_end_bite(self);
    }
}

static void zm_start_lie_down(Entity* e)
{
    zm_start(e, ZM_ACT_LIE_DOWN, ZM_ANIM_FALL);
    e->behavior_flags |= ZOMBIE_FLAG_LAYING_DOWN;
}

// Action press standing up: a door in reach takes it, then the survivor in
// grabbing range, otherwise the lunge.
static void zm_action_pressed(Entity* e)
{
    for (int r = 0; r < 3; r++) {
        s_pressReach = ZM_PRESS_REACHES[r];
        bool door = zm_try_door(e);
        bool step = !door && zm_try_step(e);
        bool event = !door && !step && zm_try_event(e);
        s_pressReach = 600;
        if (door) {
            return;
        }
        if (step) {
            zm_start(e, ZM_ACT_STEP, ZM_ANIM_SHAMBLE);
            return;
        }
        if (event) {
            zm_start(e, ZM_ACT_RIDE, ZM_ANIM_IDLE);
            return;
        }
    }
    if (zm_can_grab_survivor(e)) {
        zm_grab_survivor(e);
        return;
    }
    zm_start(e, ZM_ACT_ATTACK, ZM_ANIM_GRAB);
    Snd_em(4);   // roar
}

// ---------------------------------------------------------------------------
// The vomit (naked and green zombies: aim)
//
// zombie_vomiting (0x00436520, update_zombie_action's behaviour 6) as the
// zombie's own AI plays it: animation 5, the acid blob (billboard type 0x20,
// 2500 up and 500 ahead of the zombie) whose splash (effect_projectile_hit_check)
// hurts the player it lands near, Snd_em(7), then a short idle before it hands
// back by clearing ignore_player_flag. The blob is a billboard of the
// zombie's turn, so it is replayed on the other copies (ZM_EV_FX), where it
// splashes against each survivor's own player entity.
// ---------------------------------------------------------------------------
#define ZM_ENGINE_ACT_VOMIT 6       // update_zombie_action's zombie_vomiting

static bool zm_can_vomit(const Entity* e)
{
    return e->id == ENEMY_ZOMBIE_NAKED || e->id == ENEMY_ZOMBIE_VARIANT;
}

static void zm_start_vomit(Entity* e)
{
    e->ignore_player_flag = 1;
    e->action_behavior = ZM_ENGINE_ACT_VOMIT;
    e->action_state = 0;
    e->blend_counter = 3;
    s_vomiting = true;
}

// One frame of it; false once it has handed back.
static bool zm_vomit_frame(Entity* e)
{
    update_zombie_action();
    ENTITY = e;
    if (e->ignore_player_flag != 0 && e->action_behavior == ZM_ENGINE_ACT_VOMIT) return true;
    s_vomiting = false;
    e->ignore_player_flag = 0;
    zm_start(e, ZM_ACT_IDLE, ZM_ANIM_IDLE);
    return false;
}

static void zm_control(Entity* e, unsigned int held, unsigned int pressed)
{
    const bool action = (pressed & (ZM_PAD_ACTION | ZM_PAD_FIRE)) != 0;
    // Aim: a naked or green zombie vomits; any other lies down / gets up.
    const bool vomit = (pressed & ZM_PAD_AIM) != 0 && zm_can_vomit(e);
    const bool lieToggle = (pressed & ZM_PAD_AIM) != 0 && !vomit;

    switch (e->action_behavior) {
    case ZM_ACT_IDLE:
    default:
        if (vomit) {
            zm_start_vomit(e);
            break;
        }
        if (lieToggle) {
            zm_start_lie_down(e);
            break;
        }
        if (action) {
            zm_action_pressed(e);
            break;
        }
        if ((held & ZM_PAD_FORWARD) != 0) {
            zm_start(e, ZM_ACT_WALK, ZM_ANIM_CHASE);
            e->move_speed_current = ZM_SPEED_WALK;
            break;
        }
        if ((held & ZM_PAD_BACK) != 0) {
            zm_start(e, ZM_ACT_BACK, ZM_ANIM_SHAMBLE);
            break;
        }
        if (e->action_behavior != ZM_ACT_IDLE || e->animationId != ZM_ANIM_IDLE) {
            zm_start(e, ZM_ACT_IDLE, ZM_ANIM_IDLE);
        }
        zm_steer(e, held, ZM_TURN_IDLE);
        zm_joint_move(e, 0);
        break;

    case ZM_ACT_WALK: {
        if (vomit) {
            zm_start_vomit(e);
            break;
        }
        if (lieToggle) {
            zm_start_lie_down(e);
            break;
        }
        if (action) {
            zm_action_pressed(e);
            break;
        }
        if ((held & ZM_PAD_FORWARD) == 0) {
            zm_start(e, ZM_ACT_IDLE, ZM_ANIM_IDLE);
            zm_joint_move(e, 0);
            break;
        }
        const bool run = (held & ZM_PAD_RUN) != 0;
        if (e->timing_control == 1) {
            if (e->animation_frame_id == 8)  Snd_em(1);   // as zombie_chase_walk
            if (e->animation_frame_id == 29) Snd_em(1);
        }
        zm_steer(e, held, ZM_TURN_WALK);
        zm_joint_move(e, 0);
        if (run) {
            zm_joint_move(e, 0);
        }
        e->move_speed_current = run ? ZM_SPEED_RUN : ZM_SPEED_WALK;
        Add_speedXZ(0);
        break;
    }

    case ZM_ACT_BACK:
        if (vomit) {
            zm_start_vomit(e);
            break;
        }
        if (lieToggle) {
            zm_start_lie_down(e);
            break;
        }
        if ((held & ZM_PAD_BACK) == 0) {
            zm_start(e, ZM_ACT_IDLE, ZM_ANIM_IDLE);
            zm_joint_move(e, 0);
            break;
        }
        zm_steer(e, held, ZM_TURN_WALK);
        zm_joint_move(e, 1);           // the shamble, played backwards
        e->move_speed_current = ZM_SPEED_BACK;
        Add_speedXZ(0x800);
        break;

    case ZM_ACT_ATTACK: {
        if (e->action_state != 0) {
            zm_bite_update(e);
            break;
        }
        unsigned short tick = e->action_ticks_counter++;
        unsigned char looped = zm_joint_move(e, 0);
        if (tick < ZM_ATTACK_LUNGE_FRAMES) {
            e->move_speed_current = ZM_ATTACK_LUNGE_SPEED;
            Add_speedXZ(0);
        }
        if (tick == ZM_ATTACK_HIT_FRAME) {
            if (zm_can_grab_survivor(e)) {
                zm_grab_survivor(e);       // the lunge closed the gap
                break;
            }
            Entity* target = zm_find_target(e);
            if (target != NULL) {
                if (zm_is_zombie_id(target->id)) {
                    zm_begin_bite(e, target);
                    break;
                }
                zm_swipe(e, target);
            }
        }
        if ((looped != 0 && tick > ZM_ATTACK_HIT_FRAME) || tick >= ZM_ATTACK_MAX_FRAMES) {
            zm_start(e, ZM_ACT_IDLE, ZM_ANIM_IDLE);
        }
        break;
    }

    // ---- On the floor -----------------------------------------------------

    case ZM_ACT_LIE_DOWN:
        // zombie_falldown case 0/1: the fall cue, then animation 8 forward
        // with a slow drift, thud on frame 0x1A.
        if (e->action_state == 0) {
            e->action_state = 1;
            e->blend_counter = 7;
            Snd_em(5);
        }
        if (e->timing_control == 1 && e->animation_frame_id == 0x1A) {
            Snd_em(0);
        }
        e->move_speed_current = ZM_SPEED_FALL;
        if (zm_joint_move(e, 0) != 0) {
            zm_start(e, ZM_ACT_PRONE, ZM_ANIM_PRONE);
            break;
        }
        Add_speedXZ(0);
        break;

    case ZM_ACT_PRONE:
        // On the floor (knocked down) aim gets up, whatever the type.
        if ((pressed & ZM_PAD_AIM) != 0) {
            zm_start(e, ZM_ACT_GET_UP, ZM_ANIM_FALL);
            e->blend_counter = 0;   // zombie_falldown case 2 -> 3
            Snd_em(5);
            break;
        }
        if (action) {
            bool door = false;
            for (int r = 0; r < 3 && !door; r++) {
                s_pressReach = ZM_PRESS_REACHES[r];
                door = zm_try_door(e);
            }
            s_pressReach = 600;
            if (!door) {
                zm_start(e, ZM_ACT_PRONE_ATTACK, ZM_ANIM_PRONE_GRAB);
                Snd_em(4);
            }
            break;
        }
        if ((held & ZM_PAD_FORWARD) != 0) {
            zm_start(e, ZM_ACT_CRAWL, ZM_ANIM_CRAWL);
            break;
        }
        if (e->animationId != ZM_ANIM_PRONE) {
            zm_start(e, ZM_ACT_PRONE, ZM_ANIM_PRONE);
        }
        zm_steer(e, held, ZM_TURN_CRAWL);
        zm_joint_move(e, 0);
        break;

    case ZM_ACT_CRAWL:
        // zombie_pushback_stagger case 2, steered by the pad: the drag cue on
        // frames 7 and 0x18, and the speed taken from how far the arms pulled
        // the body this frame (zombie_body_part_physics).
        if ((pressed & ZM_PAD_AIM) != 0 || action || (held & ZM_PAD_FORWARD) == 0) {
            zm_start(e, ZM_ACT_PRONE, ZM_ANIM_PRONE);
            zm_joint_move(e, 0);
            break;
        }
        if (e->timing_control == 1 &&
            (e->animation_frame_id == 7 || e->animation_frame_id == 0x18)) {
            Snd_em(2);
        }
        zm_steer(e, held, ZM_TURN_CRAWL);
        zm_joint_move(e, 0);
        zombie_body_part_physics(1);
        Add_speedXZ(0);
        break;

    case ZM_ACT_PRONE_ATTACK: {
        // A grab from the floor. The survivor gets the real laying grab
        // (zombie_attack picks the laying rows itself from behavior_flags);
        // anything else gets one swipe.
        unsigned short tick = e->action_ticks_counter++;
        unsigned char looped = zm_joint_move(e, 0);
        if (tick == ZM_PRONE_HIT_FRAME) {
            if (zm_can_grab_survivor(e)) {
                zm_grab_survivor(e);
                break;
            }
            Entity* target = zm_find_target(e);
            if (target != NULL) {
                zm_swipe(e, target);
            }
        }
        if ((looped != 0 && tick > ZM_PRONE_HIT_FRAME) || tick >= ZM_ATTACK_MAX_FRAMES) {
            zm_start(e, ZM_ACT_PRONE, ZM_ANIM_PRONE);
        }
        break;
    }

    case ZM_ACT_RIDE:
        // The survivor frame moves the zombie (zm_zombie_ride_follow); here it
        // only animates. If the ride ended without handing back, recover.
        if (!s_riding) {
            zm_start(e, ZM_ACT_IDLE, ZM_ANIM_IDLE);
        }
        zm_joint_move(e, 0);
        break;

    case ZM_ACT_STEP: {
        // Across the step at a steady shamble, rising or dropping as it goes.
        unsigned short tick = ++e->action_ticks_counter;
        if (e->timing_control == 1) {
            if (e->animation_frame_id == 13)   Snd_em(1);   // as zombie_slow_walk
            if (e->animation_frame_id == 0x15) Snd_em(2);
        }
        zm_joint_move(e, 0);
        int f = tick < s_stepFrames ? tick : s_stepFrames;
        for (int i = 0; i < 3; i++) {
            e->scaMatrixData.localMatrix.t[i] =
                s_stepFrom[i] + (s_stepTo[i] - s_stepFrom[i]) * f / s_stepFrames;
        }
        zm_set_rollback(e);
        if (tick >= s_stepFrames) {
            zm_start(e, ZM_ACT_IDLE, ZM_ANIM_IDLE);
        }
        break;
    }

    case ZM_ACT_GET_UP:
        // zombie_falldown case 3: animation 8 played backwards, drifting back.
        e->move_speed_current = ZM_SPEED_FALL;
        if (zm_joint_move(e, 1) != 0) {
            e->behavior_flags &= ~ZOMBIE_FLAG_LAYING_DOWN;
            zm_start(e, ZM_ACT_IDLE, ZM_ANIM_IDLE);
            break;
        }
        Add_speedXZ(0x800);
        break;
    }
}

// ===========================================================================
// The engine's turn: hit reactions, death, the grab
// ===========================================================================

// zombie_state_check's (0x00433ae0) awareness bits, without the AI dispatch.
// apply_weapon_damage only considers an enemy whose status_flags share a bit
// with the shooter's aim (player flags 0x20 up / 0x40 level / 0x80 down), so
// these are what make the zombie shootable.
// Compare floor heights, not model origins. The rooftop Tyrant stands at
// y=-210 even on the survivor's floor, including on remote puppet copies.
// All weapon families use these awareness bits to decide aim elevation.
static int zm_target_height_delta(const Entity* e)
{
    int dy = g_playerEntity.scaMatrixData.localMatrix.t[1] - e->scaMatrixData.localMatrix.t[1];
    if (e->id == ENEMY_TYRANT_2) dy -= 210;
    return dy;
}

static void zm_update_awareness(Entity* e)
{
    e->status_flags &= 0x1F;
    if (!zm_survivor_present()) {
        return;
    }
    if ((e->behavior_flags & ZOMBIE_FLAG_LAYING_DOWN) == 0 && (e->action_speed & 0x80) == 0) {
        e->status_flags |= ENTITY_STATUS_ALIGNED;
        entity_check_alert_range(3000);
    }
    entity_check_visual_range(4500);
    int dy = zm_target_height_delta(e);
    if (dy < -100 || dy > 100) {
        e->status_flags &= 0x1F;
        e->status_flags |= (dy < 0) ? ENTITY_STATUS_PLAYER_ABOVE : ENTITY_STATUS_PLAYER_BELOW;
    }
}

// Run whichever engine state the zombie is in. Returns true while the engine
// still has it. When it hands back (state 1 again), the pad resumes on the
// floor or on its feet, as the reaction left it.
static bool zm_engine_turn(Entity* e)
{
    if (e->state == ZOMBIE_STATE_IDLE || e->state == ZOMBIE_STATE_INIT) {
        if (s_engineOwned) {
            s_engineOwned = false;
            e->ignore_player_flag = 0;
            e->hit_state = 0;
            if ((e->behavior_flags & ZOMBIE_FLAG_LAYING_DOWN) != 0) {
                zm_start(e, ZM_ACT_PRONE, ZM_ANIM_PRONE);
            } else {
                zm_start(e, ZM_ACT_IDLE, ZM_ANIM_IDLE);
            }
        }
        return false;
    }

    // apply_weapon_damage set state 2/3, or zm_grab_survivor set 5.
    if (!s_engineOwned && s_biteVictim != NULL) {
        Entity* v = s_biteVictim;
        s_biteVictim = NULL;
        zm_apply_reaction(v);   // let go of whoever was being bitten
    }
    s_engineOwned = true;
    // zombie_falldown and the get-up path store an absolute height (1 lying,
    // 0 standing): right for a zombie on the floor it spawned on, wrong for one
    // that has climbed a step. Keep the floor it is on.
    int floorY = e->scaMatrixData.localMatrix.t[1];
    if (e->state < 22 && zombie_states_table[e->state] != NULL) {
        ((void(*)())zombie_states_table[e->state])();
    }
    int y = e->scaMatrixData.localMatrix.t[1];
    if ((y == 0 || y == 1) && floorY != 0 && floorY != 1) {
        e->scaMatrixData.localMatrix.t[1] = floorY;
    }
    return true;
}

// The zombie is down for good: zombie_dead_animation reached its hold state
// (action_state 3, shadow shrinking) without taking the get-back-up branch.
// Hand control to another monster, or keep the director on the map.
static bool zm_possessed_died(Entity* e);   // with the map jumps below
static int zm_room_monsters(const Entity** out, unsigned short* uids, bool includePuppetZombie);

static void zm_check_death(Entity* e)
{
    if (s_zombieDeathReported) return;
    if (e->state == ZOMBIE_STATE_DIE && e->health < 0 && e->action_state >= 3) {
        s_zombieDeathReported = true;
        dbg_printf("[zombie] the possessed zombie died\n");
        zm_possessed_died(e);
    }
}

// ===========================================================================
// Per-frame update (from update_entities, ENTITY = the possessed zombie)
// ===========================================================================
// Shadow, as zombie_update - but at the zombie's own height. zombie_update
// passes 0 (enemies stay on the floor they spawned on); the player passes its
// posY (player_update_shadow_sprite), and this zombie climbs steps.
static void zm_draw_shadow(Entity* e)
{
    e->has_enter_switch_zone = (unsigned char)is_entity_in_switch_zone(
        (VECTOR*)&e->scaMatrixData.localMatrix.t, g_CurrentRdtDataTypePtr);
    if (e->has_enter_switch_zone != 0) {
        entity_add_fade_sprite((VECTOR*)&e->scaMatrixData.localMatrix.t,
                               (short*)&e->pushVelocity,
                               (short)e->scaMatrixData.localMatrix.t[1],
                               *(unsigned short*)&e->angle);
    }
}

static void zm_resume_control(Entity* e)
{
    bool prone = (e->behavior_flags & ZOMBIE_FLAG_LAYING_DOWN) != 0;
    zm_set_rollback(e);
    zm_start(e, prone ? ZM_ACT_PRONE : ZM_ACT_IDLE, prone ? ZM_ANIM_PRONE : ZM_ANIM_IDLE);
}

// Zombie's copy, mid-grab: take the pose the survivor's copy sends; give up
// waiting if it never answers.
static void zm_remote_grab_frame(Entity* e)
{
    const ZmNetPeerState* p = zm_net_player(s_remoteGrabVictim);
    unsigned int now = zm_game_time_ms();
    if (p != NULL && p->hasZombie && p->zombieSlot == (unsigned char)(e - g_EnemiesList) &&
        now - p->receivedMs < ZM_REMOTE_GRAB_GAP_MS) {
        if (!s_remoteGrabSeen) zm_econ_hit();     // the survivor's copy took the grab
        s_remoteGrabSeen = true;
        s_remoteGrabLastMs = now;
        e->scaMatrixData.localMatrix.t[0] = p->zombie.x;
        e->scaMatrixData.localMatrix.t[1] = p->zombie.y;
        e->scaMatrixData.localMatrix.t[2] = p->zombie.z;
        e->angle = p->zombie.angle;
        zm_set_rollback(e);
        zm_apply_pose(e, &p->zombie);
        return;
    }
    bool neverAnswered = !s_remoteGrabSeen && now - s_remoteGrabMs > ZM_REMOTE_GRAB_WAIT_MS;
    bool wentQuiet = s_remoteGrabSeen && now - s_remoteGrabLastMs > ZM_REMOTE_GRAB_GAP_MS;
    if (neverAnswered || wentQuiet) {
        dbg_printf("[zombie] grab %s; taking control back\n",
                   neverAnswered ? "never answered" : "went quiet");
        s_remoteGrab = false;
        zm_resume_control(e);
    }
}

// ZM_EV_GRAB_END { x, z, angle, refused }: control back where the grab left
// the zombie (the last pose shown is already there; the event is the backstop).
static void zm_remote_grab_over(Entity* e, const short* args)
{
    if (!s_remoteGrab) return;
    s_remoteGrab = false;
    if (args[3] == 0 && !s_remoteGrabSeen) {
        e->scaMatrixData.localMatrix.t[0] = (unsigned short)args[0];
        e->scaMatrixData.localMatrix.t[2] = (unsigned short)args[1];
        e->angle = args[2];
    }
    zm_resume_control(e);
    if (args[5] != 0) zombie_mode_begin_feeding(e);
    dbg_printf("[zombie] grab over%s\n", args[3] ? " (refused)" : "");
}

// Possession: leave this zombie to its AI and take the next one in the room,
// in enemy-slot order (wrapping), that is alive and free - so repeated
// presses visit every zombie in the room, the first body included. False
// when there is no other.
static bool zm_shared_slot_busy(int slot);   // with the shared-room state below
static bool s_startWasDown = false;
static bool s_optionsWasDown = false;
static unsigned int s_rawWasDown = 0;

// (The "ZOMBIE n OF m" note's state sits with the other statics at the top.)

static void zm_note_switch(const Entity* now)
{
    int index = 0, count = 0;
    for (int i = 0; i < 30; i++) {
        const Entity* e = &g_EnemiesList[i];
        if ((e->status_flags & ENTITY_STATUS_ACTIVE) == 0) continue;
        if (!zm_possessable(e) || e->health < 0) continue;
        if ((e->status_flags & ENTITY_STATUS_DEAD) != 0) continue;
        count++;
        if (e == now) index = count;
    }
    s_switchNoteIndex = index;
    s_switchNoteCount = count;
    s_noteText[0] = '\0';
    s_switchNoteFrames = ZM_SWITCH_NOTE_FRAMES;
}

static bool zm_possess_next(Entity* cur)
{
    // Never abandon either half of a paired attack, including local grabs
    // and attacks temporarily played on the victim's copy.
    if (cur != NULL && zm_possession_busy(cur)) return false;
    Entity* best = NULL;
    int curSlot = cur != NULL ? (int)(cur - g_EnemiesList) : -1;
    for (int step = 1; step <= 30 && best == NULL; step++) {
        int i = (curSlot + step) % 30;
        Entity* e = &g_EnemiesList[i];
        if (e == cur || (e->status_flags & ENTITY_STATUS_ACTIVE) == 0) continue;
        if (!zm_possessable(e) || e->health < 0) continue;
        if ((e->status_flags & ENTITY_STATUS_DEAD) != 0 || zm_shared_slot_busy(i)) continue;
        if (e == s_biteVictim || zm_possession_busy(e)) continue;
        best = e;
    }
    if (best == NULL) return false;

    // Back to the AI, from the zombie's idle state (zombie_state_check picks
    // up its behaviour byte - a crawler crawls, a standing one wanders). The
    // other monsters' state 1 with ignore_player_flag 0 is the same hand-back
    // (their brain picks the next behaviour); one mid-reaction keeps it.
    // A dead one (the switch after the possessed monster died) keeps dying.
    bool curAlive = cur != NULL && cur->health >= 0 && (cur->status_flags & ENTITY_STATUS_DEAD) == 0;
    if (curAlive && (zm_is_zombie_id(cur->id) || cur->state == 1)) {
        cur->state = ZOMBIE_STATE_IDLE;
        cur->ignore_player_flag = 0;
        cur->action_behavior = 0;
        cur->action_state = 0;
        cur->hit_state = 0;
    }
    if (cur != NULL) {
        zm_world_control_entity(cur, false);
        zm_set_rollback(cur);
    }

    s_vomiting = false;     // the one left behind goes back to its AI below
    zm_world_control_entity(best, true);
    g_zombieModeEntity = best;
    s_directorMapOnly = false;
    s_zombieDeathReported = false;
    // Possession can jump across several camera zones without walking through
    // the current camera's triggers. Re-select from the full room on its update.
    s_jumpFixCamera = true;
    best->collisionFlags |= ZM_COLLISION_LIKE_PLAYER;
    if (zm_is_zombie_id(best->id)) {
        if (zombie_mode_feeding_target(best)) zombie_mode_stand_feeding(best);
        s_engineOwned = best->state != ZOMBIE_STATE_IDLE;   // mid-reaction: the engine finishes it
        if (!s_engineOwned) {
            bool prone = (best->behavior_flags & ZOMBIE_FLAG_LAYING_DOWN) != 0;
            zm_start(best, prone ? ZM_ACT_PRONE : ZM_ACT_IDLE, prone ? ZM_ANIM_PRONE : ZM_ANIM_IDLE);
        }
    } else {
        s_engineOwned = false;
        zm_monster_possess(best);
    }
    zm_note_switch(best);
    dbg_printf("[zombie] possessed slot %d (left slot %d to its AI)\n",
               (int)(best - g_EnemiesList), curSlot);
    return true;
}

// ---------------------------------------------------------------------------
// Map jumps
// ---------------------------------------------------------------------------

// Into (stage, room), to the first of its zombies. A door record of our own,
// handed off the way door_begin_transition (PlayerAnimations.cpp) and the
// debug menu's room change hand theirs: room_transition_load then runs the
// room load without a door animation. The arrival point is the zombie's spot,
// so the camera and the survivor's parking start from there.
static bool zm_jump_to(unsigned char stage, unsigned char room)
{
    ZmJumpTarget t;
    if (room >= 0x20) return false;
    if (zm_is_hall(stage, room) && zm_hall_closed(NULL)) {
        zm_note_hall_closed();
        return false;
    }
    // A boss room is the director's only to take its Yawn (ZombieYawn.cpp).
    if (zm_director_safe_room(stage, room) && !zm_yawn_jump_room(stage, room)) {
        zm_note("MONSTERS CANNOT ENTER A SAFE ROOM");
        return false;
    }
    if (zm_world_room_zombies(stage, room, &t) <= 0) {
        // No monster there: refused. Arriving as the director's own body
        // would leave the monster just driven behind, one extra in the world.
        zm_note("NO POSSESSABLE MONSTERS IN THAT ROOM");
        return false;
    }

    unsigned char* rec = s_jumpRecord;
    memset(rec, 0, sizeof(s_jumpRecord));
    // +0x0D: room_transition_load's destination - the room alone within this
    // stage, else (stage + 1) << 5; it adds the return-mansion +5 itself.
    unsigned char base = (unsigned char)(stage >= 5 ? stage - 5 : stage);
    rec[0x0D] = (stage == g_stageId) ? room : (unsigned char)(((base + 1) << 5) | room);
    rec[0x0A] = 0;          // door00.dor
    rec[0x0B] = 0x40;       // camera 0 (corrected on arrival), full load, no door sound
    rec[0x16] = 0xFF;       // no key
    *(short*)(rec + 0x0E) = t.x;
    *(short*)(rec + 0x10) = t.y;
    *(short*)(rec + 0x12) = t.z;
    *(short*)(rec + 0x14) = t.angle;

    // Reserve the destination before the transition sleeps. The current
    // room owner must not run this monster's AI while the director loads.
    zm_world_control(stage, room, t.uid, t.id, true);
    s_jumpTarget = t;
    s_jumpPending = true;
    zm_survivor_note_zombie_door();     // where the survivor is, if it is here
    dbg_printf("[zombie] map jump to stage %d room %02X (slot %d)\n",
               (int)stage, (int)room, (int)t.slot);

    g_pendingDoorRecord = (int)rec;
    // Hand the old room over before our fade/load stops its monsters.
    if (s_gameRole == ZM_NET_ZOMBIE) {
        zm_net_send_state(g_zombieModeEntity, true, NULL, -1, true);
    }
    g_main_state_flags |= MSF_GAMEPLAY_ACTIVE;
    g_message_flags = 0;
    g_rect.textureId = 0;
    g_rect.x = -160;
    g_rect.y = -120;
    g_rect.w = 320;
    g_rect.h = 240;
    g_rect.r = 0;
    g_rect.g = 0;
    g_rect.b = 0;
    g_openMenuFlag = 1;
    draw_rect(&g_rect, 0, 0);
    Task_sleep(1);
    StMask(0, 0);
    return true;
}

bool zombie_mode_skip_door_animation(const unsigned char* record)
{
    return zombie_mode_armed() && (record == s_jumpRecord || zm_spec_jump_record(record));
}

int zombie_mode_door_frames(void)
{
    return zombie_mode_armed() ? ZM_DOOR_FRAMES : -1;
}

// room_set starts every room on camera 0 and lets check_camera_switch move
// on from there - one step, through camera 0's own zones, which is all a door
// arrival needs. A jump or possession switch can land anywhere, so find the camera whose zones
// (any group's) lead into the spot the zombie stands on, and cut to it the
// way check_camera_switch does.
// At a spot: the spectator's (ZombieSpectate.cpp) follows another survivor.
// The trap room's (ROOM1150 / ROOM6150) cameras: 0 overhead, 1 level, 2 low
// along the room toward the passage door (the one the original's event 0
// cuts to), 3 from below the floor looking straight up at the ceiling, 4
// high. The mode holds camera 3 while the slab moves: the ceiling and the
// pickaxe's door both stay readable. Keep it on all local roles throughout
// movement, then release only the camera lock owned here when the room/cycle
// ends.
#define ZM_TRAP_CAMERA 3
void zm_shotgun_crush_view(bool enable)
{
    static bool locked = false;
    if (!enable) {
        if (locked) g_main_state_flags &= ~MSF_CAMERA_LOCK;
        locked = false;
        return;
    }
    if (!g_RdtPointer || !g_RdtPointer->cam_switch_zones || !g_RdtPointer->cameras_count) return;
    if (g_RdtPointer->cameras_count <= ZM_TRAP_CAMERA) return;
    if (g_roomCameraId != ZM_TRAP_CAMERA) {
        const CAM_SWITCH_ZONE* zone = (const CAM_SWITCH_ZONE*)g_RdtPointer->cam_switch_zones;
        for (int i = 0; i < 1024; i++, zone++) {
            if ((unsigned short)zone->camFrom >= g_RdtPointer->cameras_count) return;
            if (zone->camFrom != ZM_TRAP_CAMERA) continue;
            g_CurrentRdtDataTypePtr = (void*)zone;
            g_cutId = g_roomCameraId; g_roomCameraId = ZM_TRAP_CAMERA;
            cut_set();
            break;
        }
        if (g_roomCameraId != ZM_TRAP_CAMERA) return;
    }
    if (!(g_main_state_flags & MSF_CAMERA_LOCK)) locked = true;
    g_main_state_flags |= MSF_CAMERA_LOCK;
}

void zm_fix_camera_at(const int* t, int slot)
{
    if (g_RdtPointer == NULL || g_RdtPointer->cam_switch_zones == NULL) return;
    if ((g_main_state_flags & MSF_CAMERA_LOCK) != 0) return;
    const CAM_SWITCH_ZONE* zone = (const CAM_SWITCH_ZONE*)g_RdtPointer->cam_switch_zones;
    unsigned short cams = g_RdtPointer->cameras_count;
    unsigned short group = 0xFFFF;
    unsigned short coverageCamera = 0xFFFF;
    unsigned short targetCamera = 0xFFFF;
    for (int i = 0; i < 1024; i++, zone++) {
        unsigned short from = (unsigned short)zone->camFrom;
        if (from >= cams) break;
        if (from != group) {
            group = from;
            // The header's quad describes this camera's coverage, including
            // positions outside every transition trigger (e.g. main hall).
            if (is_entity_in_switch_zone((VECTOR*)t, (void*)zone) != 0 &&
                (coverageCamera == 0xFFFF || from == g_roomCameraId)) {
                coverageCamera = from;
            }
            continue;
        }
        unsigned short to = (unsigned short)zone->camTo;
        if (to >= cams || to == from) continue;
        if (is_entity_in_switch_zone((VECTOR*)t, (void*)zone) == 0) continue;
        targetCamera = to;
        break;
    }
    if (targetCamera == 0xFFFF) targetCamera = coverageCamera;
    if (targetCamera == 0xFFFF) {
        dbg_printf("[zombie] camera reposition: no zone for slot %d at %d,%d,%d\n",
                   slot, t[0], t[1], t[2]);
        return;
    }
    if (targetCamera == g_roomCameraId) return;
    g_roomCameraId = (unsigned char)targetCamera;
    if ((g_main_state_flags & MSF_CAMERA_DEFER) != 0) {
        g_main_state_flags |= MSF_CAMERA_REDRAW;
        StMask(0, 5);
    } else {
        StMask(0, 4);
        display_room_camera_bg();
    }
    dbg_printf("[zombie] camera reposition: slot %d camera %d\n",
               slot, (int)targetCamera);
}

static void zm_fix_camera(Entity* e)
{
    zm_fix_camera_at((const int*)e->scaMatrixData.localMatrix.t, (int)(e - g_EnemiesList));
}

// ---------------------------------------------------------------------------
// The possessed monster died
//
// The director is not out of the game: it takes the next living monster of
// the room (the order START walks), else the nearest room that has one - a
// breadth-first walk of the whole door graph read out of the RDTs - and
// jumps there as from the map. A distant monster must keep the director in
// the match too; the old four-door limit could end its copy prematurely.
// ---------------------------------------------------------------------------
#define ZM_DEATH_SEARCH_ROOMS (7 * 32)

static bool zm_nearest_monster_room(unsigned char* outStage, unsigned char* outRoom)
{
    unsigned char qs[ZM_DEATH_SEARCH_ROOMS], qr[ZM_DEATH_SEARCH_ROOMS];
    int head = 0, tail = 0;
    qs[tail] = g_stageId; qr[tail] = g_roomId; tail++;
    while (head < tail) {
        unsigned char stage = qs[head], room = qr[head];
        head++;
        if (head > 1 && room < 0x20 && !(zm_is_hall(stage, room) && zm_hall_closed(NULL)) &&
            !zm_director_safe_room(stage, room) &&
            zm_world_room_zombies(stage, room, NULL) > 0) {
            *outStage = stage;
            *outRoom = room;
            return true;
        }
        const ZmDoor* doors;
        int n = zm_room_doors(stage, room, &doors);
        if (n <= 0) continue;
        ZmDoor own[ZM_MAX_DOORS];
        if (n > ZM_MAX_DOORS) n = ZM_MAX_DOORS;
        memcpy(own, doors, sizeof(ZmDoor) * n);     // the cache may move under the next lookups
        for (int i = 0; i < n && tail < ZM_DEATH_SEARCH_ROOMS; i++) {
            if ((own[i].flags0B & 0x80) != 0) continue;           // camera-only
            unsigned char ns, nr;
            zm_decode_dest(own[i].dest, stage, &ns, &nr);
            if (ns >= 7 || nr >= 0x20) continue;
            bool seen = false;
            for (int k = 0; k < tail && !seen; k++) seen = qs[k] == ns && qr[k] == nr;
            if (seen) continue;
            qs[tail] = ns; qr[tail] = nr;
            tail++;
        }
    }
    // Placements can also lie outside the current room's outgoing door graph.
    // Prefer a nearby monster above, then use any eligible room in the world.
    for (unsigned char stage = 0; stage < 7; stage++) {
        for (unsigned char room = 0; room < 0x20; room++) {
            if (stage == g_stageId && room == g_roomId) continue;
            if (zm_is_hall(stage, room) && zm_hall_closed(NULL)) continue;
            if (zm_director_safe_room(stage, room)) continue;
            if (zm_world_room_zombies(stage, room, NULL) <= 0) continue;
            *outStage = stage;
            *outRoom = room;
            return true;
        }
    }
    return false;
}

// True: the director has a new monster, is on its way, or waits on the map.
static bool zm_possessed_died(Entity* e)
{
    // Publish the final corpse pose before a jump changes this copy's room.
    if (s_gameRole != ZM_NET_OFF) {
        const Entity* list[30];
        unsigned short uids[30];
        int n = zm_room_monsters(list, uids, false);
        zm_net_send_enemies(list, uids, n, false);
    }
    // Whatever comes next starts fresh, should it end up as the director's
    // own body (a jump whose room does not place the monster after all).
    s_carriedHealth = ZM_HEALTH;
    s_carriedProne = false;
    s_carriedId = ENEMY_ZOMBIE;
    s_carriedBehavior = 0;
    if (zm_possess_next(e)) {
        s_zombieDeathReported = false;          // the new one can die in its turn
        zm_note("MONSTER DOWN - TOOK THE NEXT ONE");
        dbg_printf("[zombie] possessed monster died: switched in the room\n");
        return true;
    }
    unsigned char stage, room;
    // If the destination monster cannot be spawned, stay on the map there
    // rather than creating a replacement director body for free.
    s_directorMapOnly = true;
    if (zm_nearest_monster_room(&stage, &room) && zm_jump_to(stage, room)) {
        ENTITY = e;     // the hand-off slept a frame
        dbg_printf("[zombie] possessed monster died: jumping to stage %d room %02X\n",
                   (int)stage, (int)room);
        return true;
    }
    zm_map_set_read_only(false);
    if (!zm_map_is_open()) zm_map_toggle();
    zm_note("MONSTER DOWN - PLACE A NEW MONSTER");
    dbg_printf("[zombie] possessed monster died: waiting on the map\n");
    return true;
}

void zm_shotgun_director_crush(void)
{
    if (s_gameRole == ZM_NET_SURVIVOR || !g_zombieModeEntity ||
        g_zombieModeEntity->health >= 0 || s_zombieDeathReported) return;
    // The slab hides the corpse, so bypass its normal death animation/sound.
    // Give the overhead camera a moment before the standard monster/map handoff.
    s_zombieDeathReported = true;
    zm_shotgun_crush_view(false);
    zm_possessed_died(g_zombieModeEntity);
}


// The director's controls that are not the possessed body's own: the map
// (OPTIONS), monster placement and map jumps, and START's switch to the next
// monster of the room. True when the frame is spent (a jump or a switch);
// otherwise *held / *pressed are the pad for the body (zero while the map is
// up). `busy`: the body is in the middle of something a switch must not cut.
static bool zm_director_input(Entity* e, bool busy, unsigned int* heldOut, unsigned int* pressedOut)
{
    // The game-frame map path also calls here while the engine plays an
    // attack. Enforce this gate centrally, independent of the caller.
    busy = busy || (e != NULL && zm_possession_busy(e));
    // Input follows the player's gates: game_loop blanks the pad unless
    // 0x100 is set, and update_player_anim needs bit 0.
    unsigned int held = 0;
    unsigned int pressed = 0;
    if ((g_message_flags & 0x0101) == 0x0101 &&
        (g_main_state_flags & MSF_MENU_ACTIVE) == 0) {
        held = (unsigned short)g_PlayerDpadHeld;
        pressed = (unsigned short)g_PlayerDpadPressed;
    }
    *heldOut = 0;
    *pressedOut = 0;
    // START (raw 0x0800, the inventory key): jump into the next
    // zombie in the room; this one goes back to its own AI where it
    // stands. Edge-detected here off the raw held word - the game's
    // own "pressed" word stays set while the key is down, which
    // switched every frame and ping-ponged between two zombies.
    // OPTIONS (raw 0x0900 - the START bit plus 0x0100) opens the
    // director's map instead, so START alone is the bare 0x0800.
    unsigned int raw = g_button_pressed_id;
    unsigned int rawEdge = raw & ~s_rawWasDown;
    s_rawWasDown = raw;
    bool optionsDown = (raw & 0x0900) == 0x0900;
    bool startDown = (raw & 0x0800) != 0 && !optionsDown;
    bool startPressed = startDown && !s_startWasDown;
    s_startWasDown = startDown;
    bool optionsPressed = optionsDown && !s_optionsWasDown;
    s_optionsWasDown = optionsDown;
    bool gates = (g_message_flags & 0x0101) == 0x0101 &&
                 (g_main_state_flags & MSF_MENU_ACTIVE) == 0;
    // A boss fight under way plays its Yawn by a human director's hand
    // (ZombieYawn.cpp): taken there as soon as nothing holds the director, and
    // kept until it is beaten - no switching to another monster, no map jump.
    unsigned char bossStage, bossRoom;
    if (gates && !busy && !s_jumpPending && zm_yawn_force_jump(e, &bossStage, &bossRoom)) {
        if (zm_map_is_open()) zm_map_toggle();
        if (zm_jump_to(bossStage, bossRoom)) {
            ENTITY = e;
            return true;
        }
    }
    bool bossBound = zm_yawn_director_bound(e);
    if (bossBound) startPressed = false;
    bool mapWasOpen = zm_map_is_open();
    if (gates && optionsPressed && !s_directorMapOnly) {
        zm_map_set_read_only(false);
        zm_map_toggle();
    } else if (gates) {
        zm_map_input(rawEdge);
    }
    // LB + a fresh direction buys one monster in the loaded room. Use the
    // map's placement path so prices, caps and reinforcements stay identical.
    if (gates && !zm_map_is_open() && !mapWasOpen && !s_jumpPending &&
        (raw & PAD_MOD1) != 0) {
        if ((rawEdge & 0xF000) != 0 && zm_yawn_director_closed(g_stageId, g_roomId))
            zm_note("NOTHING CAN BE PLACED IN A BOSS ROOM");
        else if (rawEdge & 0x1000) zm_director_place(g_stageId, g_roomId, 0x00, "ZOMBIE");
        else if (rawEdge & 0x4000) zm_director_place(g_stageId, g_roomId, 0x02, "CERBERUS");
        else if (rawEdge & 0x8000) zm_director_place(g_stageId, g_roomId, 0x06, "HUNTER");
        else if (rawEdge & 0x2000) zm_director_place(g_stageId, g_roomId, 0x09, "CHIMERA");
        ENTITY = e;     // a live spawn uses the engine's entity scratch pointer
        held &= ~(ZM_PAD_FORWARD | ZM_PAD_BACK | ZM_PAD_TURN_A | ZM_PAD_TURN_B);
        pressed &= ~(ZM_PAD_FORWARD | ZM_PAD_BACK | ZM_PAD_TURN_A | ZM_PAD_TURN_B);
    }
    // Placed a monster from the map.
    unsigned char placeStage, placeRoom, placeId;
    const char* placeName;
    if (zm_map_take_place(&placeStage, &placeRoom, &placeId, &placeName)) {
        zm_director_place(placeStage, placeRoom, placeId, placeName);
        ENTITY = e;
        if (s_directorMapOnly && zm_is_possessable_id(placeId)) {
            bool took = false;
            if (placeStage == g_stageId && placeRoom == g_roomId) {
                zm_spawn_missing_extras();
                took = zm_possess_next(e);
            } else {
                took = zm_jump_to(placeStage, placeRoom);
            }
            if (took) {
                if (zm_map_is_open()) zm_map_toggle();
                ENTITY = e;
                return true;
            }
        }
    }
    // Picked a zombie room on the map: go there.
    unsigned char jumpStage, jumpRoom;
    // The map stays open when the jump is refused (an empty room, the closed
    // hall), so the note shows under its title.
    bool jumpPicked = zm_map_take_jump(&jumpStage, &jumpRoom);
    if (jumpPicked && bossBound) {
        zm_note("THE FIGHT HOLDS YOU TO YAWN");
    } else if (jumpPicked && !busy && zm_jump_to(jumpStage, jumpRoom)) {
        if (zm_map_is_open()) zm_map_toggle();
        ENTITY = e;     // as after a door: the hand-off slept a frame
        return true;
    }
    // The map is up: the zombie stands where it is (the world runs on).
    // The press that closed it is the map's, too.
    if (zm_map_is_open() || mapWasOpen) {
        held = 0;
        pressed = 0;
        startPressed = false;
    }
    bool jump = (g_message_flags & 0x0101) == 0x0101 && startPressed && !busy;
    if (jump && zm_possess_next(e)) {
        return true;
    }
    *heldOut = held;
    *pressedOut = pressed;
    return false;
}

// ===========================================================================
// Possessed monsters other than zombies: the Cerberus, the Hunter and the
// Chimera
//
// Each runs on its own engine code for everything that is not walking: the
// attacks (its own behaviour, started by the button and left to finish), the
// hit reactions (state 2) and the death (state 3). Walking, running and
// turning are the pad's, with the type's own animations; the AI that would
// pick behaviours never runs while it is possessed.
//
// How each type is driven, read off its update (entities/*.cpp):
//   Cerberus  state 1 dispatches the behaviour in ignore_player_flag; with it
//             at 0 the selector turns behavior_flags into one. Flags 5 = a
//             running leap (cerberus_beh_select_entrance: anim 6, then
//             behaviour 3, which bites what it lands on), 8 = the close snap
//             (behaviour 7). Either ends by clearing ignore_player_flag with
//             behavior_flags back at 4 (chase) or 9 (strafe).
//   Hunter    state 1 runs the behaviour in action_behavior while
//             ignore_player_flag is 1 (0 lets the variant AI choose). 4 = the
//             claw swipe, 6 = the jumping claw attack (dodge/swipe chain).
//             Over when ignore_player_flag
//             drops to 0 or the behaviour is back to walking (0-2).
//   Chimera   the same layering: 3 = the double-claw swipe, 12 = the claw
//             with grab. A ceiling Chimera (variant 2) is dropped to the floor
//             first (behaviour 6) - the pad drives the floor variants only.
// ===========================================================================
struct ZmMonsterType {
    unsigned char id;
    unsigned char idleAnim, walkAnim, runAnim;
    short         walkSpeed, runSpeed;
    short         turnIdle, turnMoving;
};
static const ZmMonsterType kMonsterTypes[] = {
    // Cerberus: walk 1 at 40 from cerberus_walk_params; straight chase run 2
    // from cerberus_set_turn_anim (0x0049b3b0), at double speed for director
    // control. 0 is the alert standing pose.
    { ENEMY_CERBERUS, 0x00, 0x01, 0x02,  40, 260, 0x30, 0x28 },
    // Hunter: hunter_idle_stand (0x00417130, anim 0x15), the former director
    // run from hunter_behavior_chase (0x00417290, anim 0x10 at 40) as the walk,
    // and hunter_scd_run (0x0048f680, anim 2 at 200) as the hunched run.
    { ENEMY_HUNTER,   0x15, 0x10, 0x02,  40, 200, 0x30, 0x20 },
    // Chimera: the floor idle (the variant's own animation, 0), the brain's
    // walk (3 at 100) and its fast gait (2 at 200).
    { ENEMY_CHIMERA,  0x00, 0x03, 0x02, 100, 200, 0x30, 0x20 },
    // Rooftop charge: animation 2, speed 0x190 (tyrant_behavior_charge).
    { ENEMY_TYRANT_2, 0x00, 0x01, 0x02,  60, 400, 0x28, 0x20 },
};

static const ZmMonsterType* zm_monster_type(unsigned char id)
{
    for (unsigned int i = 0; i < sizeof(kMonsterTypes) / sizeof(kMonsterTypes[0]); i++) {
        if (kMonsterTypes[i].id == id) return &kMonsterTypes[i];
    }
    return NULL;
}

bool zm_is_possessable_id(unsigned char id)
{
    return zm_is_zombie_id(id) || zm_monster_type(id) != NULL;
}

// A monster the director can take: a possessable type, or a Yawn's head
// (never its body segments) - ZombieYawn.cpp, zm_yawn_possessed.
static bool zm_possessable(const Entity* e)
{
    return zm_is_possessable_id(e->id) || zm_yawn_head(e);
}

// The possessed monster's own engine owns it this frame (an attack under way,
// or the drop of a ceiling Chimera).
static bool s_monEngine = false;
static unsigned int s_monDeathFrames = 0;
static unsigned char s_monAnim = 0xFF;

// Taking a monster: everything the pad does not drive is left as its engine
// had it; a monster mid-attack or mid-reaction finishes that first.
static void zm_monster_possess(Entity* e)
{
    s_monEngine = false;
    s_monDeathFrames = 0;
    s_monAnim = 0xFF;
    e->behavior_flags &= (unsigned char)~0x40;      // a Hunter's "SCD-controlled" bit
    if (e->id == ENEMY_TYRANT_2 && e->state == 8) {
        // A scripted pose must hand back to the director's combat controls.
        e->state = 1;
        e->ignore_player_flag = 0;
        e->action_behavior = 1;
        e->action_state = 0;
        *(unsigned int*)((char*)e + 0x174) = *(unsigned int*)&e->state;
    }
    if (e->id == ENEMY_CHIMERA && e->state == 1 && e->behavior_flags == 2) {
        // Off the ceiling first.
        e->ignore_player_flag = 1;
        e->action_behavior = 6;
        e->action_state = 0;
        s_monEngine = true;
    }
    if (e->state == 1 && !zm_monster_attack_over(e) && !s_monEngine) {
        // Whatever it was doing - an attack - plays out.
        s_monEngine = true;
    }
}

// Start an attack: `strong` is the second one (aim + action).
static bool zm_monster_attack(Entity* e, bool strong, bool sprint = false)
{
    if (e->id == ENEMY_HUNTER && strong) {
        unsigned int now = zm_game_time_ms();
        if (s_hunterLeapUsed && now - s_hunterLeapMs < 5000) {
            dbg_printf("[hunter leap] blocked: %u ms remaining\n", 5000 - (now - s_hunterLeapMs));
            return false;
        }
        s_hunterLeapMs = now;
        s_hunterLeapUsed = true;
        dbg_printf("[hunter leap] started at %u ms; cooldown 5000 ms\n", now);
    }
    switch (e->id) {
    case ENEMY_TYRANT_2:
        e->ignore_player_flag = 1; // suppress the Tyrant's decision AI
        // Sprint + Action enters the charge's wide sweep directly, leaving
        // the running direction under the pad instead of the charge AI.
        e->action_behavior = sprint && !strong ? 8 : strong ? 4 : 3;
        e->action_state = sprint && !strong ? 2 : 0;
        dbg_printf("[tyrant] director attack %u sub %u\n", e->action_behavior, e->action_state);
        break;
    case ENEMY_CERBERUS:
        e->ignore_player_flag = 0;
        e->behavior_flags = strong ? 8 : 5;
        break;
    case ENEMY_HUNTER: {
        e->ignore_player_flag = 1;
        e->action_behavior = strong ? 6 : 4;
        e->action_state = 0;
        // hunter_behavior_dodge (0x00417a80) runs the wind-up, then
        // hunter_dodge_swipe (0x00417ba0) launches the jumping claw attack
        // with vertical speed 580 and gravity -50, including its hit tests.
        break;
    }
    case ENEMY_CHIMERA:
        e->ignore_player_flag = 1;
        e->action_behavior = strong ? 12 : 3;
        e->action_state = 0;
        break;
    }
    s_monEngine = true;
    return true;
}

// Is the engine's attack over (back to something the pad drives)?
static bool zm_monster_attack_over(const Entity* e)
{
    switch (e->id) {
    case ENEMY_TYRANT_2:
        return e->ignore_player_flag == 0 || e->action_behavior <= 1;
    case ENEMY_CERBERUS:
        if (e->ignore_player_flag == 0) {
            return e->behavior_flags != 5 && e->behavior_flags != 8;   // not yet selected
        }
        return e->ignore_player_flag != 3 && e->ignore_player_flag != 6 && e->ignore_player_flag != 7;
    case ENEMY_HUNTER:
    case ENEMY_CHIMERA:
        return e->ignore_player_flag == 0 || e->action_behavior <= 2;
    }
    return true;
}

// Possession may wait for an attack/reaction to finish, but must never reset
// its animation counters or leave the victim's animation without its driver.
static bool zm_possession_busy(const Entity* e)
{
    // A dead body must still allow the existing automatic death handoff.
    if (e->health < 0) return false;
    // Room jumps take the freshly spawned body before its first update.
    // Yawn must be reserved here too: its controller runs the init next frame.
    if (e->state == 0) return false;
    // Yawn: its attacks (bite, rear, swallow, thrash, slam) and its reactions.
    if (zm_yawn_head(e)) return e->state != 1 || e->action_behavior >= 2;
    int slot = (int)(e - g_EnemiesList);
    if (slot >= 0 && slot < 30 && zm_shared_slot_busy(slot)) return true;
    if (e == g_zombieModeEntity && (s_remoteGrab || s_biteVictim != NULL || s_riding)) return true;
    if (zombie_mode_feeding_target(e)) return false;
    if (e->state != 1) return true;
    return !zm_is_zombie_id(e->id) && !zm_monster_attack_over(e);
}

static bool zm_entry_idle(Entity* e)
{
    // The idle animator has no decision AI or attack hit tests. Keep hurt,
    // death and already-started paired attacks on their normal paths.
    int spawnSlot = (int)(e - g_EnemiesList);
    bool spawnHold = false;
    if (spawnSlot >= 0 && spawnSlot < 30 && e->state != 0) {
        if (s_spawnIdlePending[spawnSlot]) {
            s_spawnIdlePending[spawnSlot] = false;
            s_spawnIdleUntil[spawnSlot] = zm_game_time_ms() + 1000;
        }
        spawnHold = s_spawnIdleUntil[spawnSlot] != 0 &&
                    (int)(s_spawnIdleUntil[spawnSlot] - zm_game_time_ms()) > 0;
    }
    if (e->state == 1 && e->health >= 0 && zm_is_possessable_id(e->id) &&
        !zombie_mode_is_puppet(e) && !zm_possession_busy(e) &&
        (spawnHold || zm_reinforce_hold(e) ||
         (e != g_zombieModeEntity && zm_world_director_controlled(e)))) {
        if ((g_message_flags & 4) != 0) {
            unsigned char idle = zm_is_zombie_id(e->id)
                ? ((e->behavior_flags & ZOMBIE_FLAG_LAYING_DOWN) ? ZM_ANIM_PRONE : ZM_ANIM_IDLE)
                : zm_monster_type(e->id)->idleAnim;
            if (e->animationId != idle) {
                e->animationId = idle;
                e->animation_frame_id = 0;
                e->timing_control = 0;
                e->blend_counter = 3;
            }
            e->move_speed_current = 0;
            Joint_move(0, e->animHeader, e->animBase, 0x200);
            // Idle still means visible, solid and shootable.
            zm_puppet_awareness(e, false);
            SetEntityScaHitData(e);
        }
        zm_draw_shadow(e);
        return true;
    }
    return false;
}

static void zm_monster_set_anim(Entity* e, unsigned char anim)
{
    if (s_monAnim == anim) return;
    s_monAnim = anim;
    e->animationId = anim;
    e->animation_frame_id = 0;
    e->timing_control = 0;
    e->blend_counter = 7;
}

// One frame of the pad's walking: the type's gait, the turn, and the
// collision its own update would run.
static void zm_monster_walk(Entity* e, const ZmMonsterType* t, unsigned int held)
{
    bool forward = (held & ZM_PAD_FORWARD) != 0;
    bool back = (held & ZM_PAD_BACK) != 0;
    bool run = (held & ZM_PAD_RUN) != 0;
    short turn = (forward || back) ? t->turnMoving : t->turnIdle;
    if ((held & ZM_PAD_TURN_A) != 0) e->angle = (short)((e->angle + turn) & 0xFFF);
    if ((held & ZM_PAD_TURN_B) != 0) e->angle = (short)((e->angle - turn) & 0xFFF);

    if (forward) {
        zm_monster_set_anim(e, run ? t->runAnim : t->walkAnim);
        e->move_speed_current = (unsigned short)(run ? t->runSpeed : t->walkSpeed);
    } else if (back) {
        zm_monster_set_anim(e, t->walkAnim);
        e->move_speed_current = (unsigned short)(-(t->walkSpeed / 2));
    } else {
        zm_monster_set_anim(e, t->idleAnim);
        e->move_speed_current = 0;
    }
    if (e->move_speed_current != 0) Add_speedXZ(0);
    if ((char)Joint_move(0, e->animHeader, e->animBase, 0x200) != 0 && (forward || back)) {
        Snd_em(0);     // the step cue, on the loop
    }
    e->scaMatrixData.localMatrix.t[1] = e->id == ENEMY_TYRANT_2 ? -210 : 0;

    // Shootable as its own state 1 makes it: the aligned bit and the
    // vertical relation to the player.
    e->status_flags = (unsigned char)((e->status_flags & 0x1F) | ENTITY_STATUS_ALIGNED);
    int dy = zm_target_height_delta(e);
    if (dy < -100 || dy > 100) {
        e->status_flags &= 0x1F;
        e->status_flags |= (dy < 0) ? ENTITY_STATUS_PLAYER_ABOVE : ENTITY_STATUS_PLAYER_BELOW;
    }

    SetEntityScaHitData(e);
    if (zm_survivor_present()) {
        ResolveEntityScaCollision((Entity*)&g_playerEntity, e);
    }
    HandleEnemyPlayerCollisions();
    e->collisionFlags |= ZM_COLLISION_LIKE_PLAYER;
    check_room_collision((VECTOR*)&e->scaMatrixData.localMatrix.t, *(short*)(e->Sca_info + 10));
    zm_set_rollback(e);
    zm_draw_shadow(e);
    if (e->id == ENEMY_TYRANT_2) {
        extern void tyrant_director_walk_finish(void);
        tyrant_director_walk_finish();
    }
}

// ---------------------------------------------------------------------------
// A possessed Yawn (its own update, yawn_update, for everything but the
// choosing): the decision tree is kept off (ignore_player, +0x85, held at 1),
// and the pad picks its action - Forward crawls (Run with it, faster), the
// turns steer, Action bites, Aim + Action thrashes, Run + Action rears and
// slams (Yawn.cpp actions 2, 9, 10). An attack, a flinch or a death is the
// engine's until it hands back to the crawl. Beaten - Yawn 1 under its flee
// health, or Yawn 2 dead - it goes back to its own AI (the flee, the death)
// and the director to the next monster in reach (zm_possessed_died).
// ---------------------------------------------------------------------------
#define ZM_YAWN_CRAWL  450
#define ZM_YAWN_RUSH   900
#define ZM_YAWN_TURN   0x30
#define ZM_YAWN_FLEE_HEALTH 0x0AF0          // yawn_pick_action_normal's flee
static void zm_yawn_possessed(Entity* e)
{
    if (s_jumpFixCamera && (g_main_state_flags & MSF_CAMERA_LOCK) == 0) {
        s_jumpFixCamera = false;
        zm_fix_camera(e);
    }
    if (e->state == 0) {                    // its init: the skeleton and the body
        s_jumpFreshInit = false;
        yawn_update();
        ENTITY = e;
        return;
    }
    if ((g_message_flags & 0x0004) == 0) return;
    bool beaten = (e->id == ENEMY_YAWN_1 && e->health < ZM_YAWN_FLEE_HEALTH) || e->health < 0;
    if (beaten) {
        if (e->id == ENEMY_YAWN_1 && e->health >= 0 && e->action_behavior != 7) {
            *(unsigned int*)((char*)e + 0x84) = 0x70101;    // the flee (yawn_action_flee)
        }
        yawn_update();
        ENTITY = e;
        if (!s_zombieDeathReported && (e->health >= 0 || ++s_monDeathFrames > 90)) {
            s_zombieDeathReported = true;
            dbg_printf("[yawn] the possessed Yawn is beaten: back to its own AI\n");
            zm_possessed_died(e);
        }
        return;
    }
    bool busy = e->state != 1 || e->action_behavior >= 2;
    unsigned int held = 0, pressed = 0;
    if (zm_director_input(e, busy, &held, &pressed)) return;
    if (!busy) {
        e->ignore_player_flag = 1;
        if ((pressed & (ZM_PAD_ACTION | ZM_PAD_FIRE)) != 0) {
            unsigned int action = (held & ZM_PAD_AIM) != 0 ? 9 : (held & ZM_PAD_RUN) != 0 ? 10 : 2;
            *(unsigned int*)((char*)e + 0x84) = 0x101u | (action << 16);
            dbg_printf("[yawn] the director's %s\n", action == 9 ? "thrash" : action == 10 ? "slam" : "bite");
        } else {
            if ((held & ZM_PAD_TURN_A) != 0) e->angle = (short)((e->angle + ZM_YAWN_TURN) & 0xFFF);
            if ((held & ZM_PAD_TURN_B) != 0) e->angle = (short)((e->angle - ZM_YAWN_TURN) & 0xFFF);
            // Steering a snake carries its body forward instead of pivoting
            // the head in place. Run also speeds up a turn-only crawl.
            bool forward = (held & (ZM_PAD_FORWARD | ZM_PAD_TURN_A | ZM_PAD_TURN_B)) != 0;
            unsigned char want = forward ? 1 : 0;
            if (e->action_behavior != want) {
                e->action_behavior = want;
                e->action_state = 0;
            }
            *(short*)((char*)e + 0x172) = (short)(!forward ? 0 : (held & ZM_PAD_RUN) != 0 ? ZM_YAWN_RUSH : ZM_YAWN_CRAWL);
        }
    }
    zm_yawn_bounds_begin(e);
    yawn_update();
    ENTITY = e;
    zm_yawn_bounds_end(e);
}

static void zm_monster_update(Entity* e)
{
    const ZmMonsterType* t = zm_monster_type(e->id);
    void (*own)(void) = (void (*)(void))enemies_update_functions_tbl[e->id];
    if (t == NULL || own == NULL) return;

    // Sample even while the engine owns the attack, so holding Aim through
    // its recovery cannot become another press when control returns.
    bool aimDown = (g_PlayerDpadHeld & ZM_PAD_AIM) != 0;
    bool aimPressed = aimDown && !s_hunterAimWasDown;
    s_hunterAimWasDown = aimDown;

    if (s_jumpFixCamera && (g_main_state_flags & MSF_CAMERA_LOCK) == 0) {
        s_jumpFixCamera = false;
        zm_fix_camera(e);
    }

    // Its own init (state 0) - the type's update runs it - then the pad.
    if (e->state == 0) {
        s_jumpFreshInit = false;
        own();
        ENTITY = e;
        if (s_carriedPending) {
            // Came through a door as the director's body: as hurt as it left.
            s_carriedPending = false;
            if (s_carriedHealth >= 0) e->health = s_carriedHealth;
        }
        zm_monster_possess(e);
        return;
    }

    if ((g_message_flags & 0x0004) == 0) {
        zm_draw_shadow(e);
        return;
    }

    // A reaction can hand back into an attack as well as idle. Remember
    // that ownership so its first state-1 frame cannot truncate the pair.
    if (e->state != 1) s_monEngine = true;
    // Check BEFORE calling the engine: after a reaction/attack returns to
    // idle, another engine tick would run its autonomous decision brain.
    if (e->state == 1 && s_monEngine && zm_monster_attack_over(e)) {
        s_monEngine = false;
        s_monAnim = 0xFF;
    }
    // Hit reactions, death and attacks are the engine's.
    if (e->state != 1 || s_monEngine) {
        own();
        ENTITY = e;
        if (e->state == 1 && s_monEngine && zm_monster_attack_over(e)) {
            s_monEngine = false;
            s_monAnim = 0xFF;
            if (e->id == ENEMY_CHIMERA && e->behavior_flags == 2) {
                // Still a ceiling variant after the drop: stand it on the floor.
                e->behavior_flags = 0;
                e->angle_z = 0;
                e->scaMatrixData.localMatrix.t[1] = 0;
            }
        }
        if (e->state == 3 && e->health < 0 && !s_zombieDeathReported && ++s_monDeathFrames > 90) {
            s_zombieDeathReported = true;
            dbg_printf("[zombie] the possessed monster (id %d) died\n", (int)e->id);
            zm_possessed_died(e);
        }
        return;
    }

    unsigned int held = 0, pressed = 0;
    if (zm_director_input(e, zm_possession_busy(e), &held, &pressed)) {
        return;
    }
    if (e->id == ENEMY_HUNTER && aimPressed && (held & ZM_PAD_AIM) != 0) {
        // Aim alone starts the jumping claw attack, hunter_behavior_dodge (0x00417a80).
        if (zm_monster_attack(e, true)) own();
        else zm_monster_walk(e, t, held);
        ENTITY = e;
        return;
    }
    if ((pressed & (ZM_PAD_ACTION | ZM_PAD_FIRE)) != 0) {
        // Action at a door goes through it, as for a zombie (zm_action_pressed):
        // the room's own door entry, the same reaches. Aim + action is always
        // the second attack.
        if ((held & ZM_PAD_AIM) == 0) {
            bool door = false;
            for (int r = 0; r < 3 && !door; r++) {
                s_pressReach = ZM_PRESS_REACHES[r];
                door = zm_try_door(e);
            }
            s_pressReach = 600;
            if (door) {
                s_monAnim = 0xFF;
                return;
            }
        }
        bool sprint = (held & (ZM_PAD_RUN | ZM_PAD_FORWARD)) == (ZM_PAD_RUN | ZM_PAD_FORWARD);
        if (zm_monster_attack(e, (held & ZM_PAD_AIM) != 0, sprint)) own();
        else zm_monster_walk(e, t, held);
        ENTITY = e;
        return;
    }
    zm_monster_walk(e, t, held);
}

void zombie_mode_update(void)
{
    Entity* e = ENTITY;
    // Map controls run from the game frame even if the corpse stops updating.
    if (s_directorMapOnly) return;

    // The director can finish loading before the previous owner finishes
    // a paired animation. Display that owner's pose until the room lease
    // ends; do not drive a second copy of this monster or reset the victim.
    if (s_gameRole != ZM_NET_OFF && !s_iOwn) {
        s_directorAwaitingOwner = true;
        zombie_mode_puppet_update();
        return;
    }
    if (s_directorAwaitingOwner) {
        s_directorAwaitingOwner = false;
        if (s_carriedPending) {
            s_carriedPending = false;
            if (s_carriedHealth >= 0) e->health = s_carriedHealth;
        }
        if (zm_is_zombie_id(e->id)) {
            s_engineOwned = e->state != ZOMBIE_STATE_IDLE;
            if (!s_engineOwned) {
                bool prone = (e->behavior_flags & ZOMBIE_FLAG_LAYING_DOWN) != 0;
                zm_start(e, prone ? ZM_ACT_PRONE : ZM_ACT_IDLE, prone ? ZM_ANIM_PRONE : ZM_ANIM_IDLE);
            }
        } else zm_monster_possess(e);
    }
    // A possessed Yawn, Cerberus, Hunter or Chimera has its own controller.
    if (zm_yawn_head(e)) {
        zm_yawn_possessed(e);
        return;
    }
    if (!zm_is_zombie_id(e->id)) {
        zm_monster_update(e);
        return;
    }

    if (e->state == ZOMBIE_STATE_INIT && s_jumpFreshInit) {
        // A room zombie taken on arrival: its own init (health, behaviour),
        // then the pad. The roster's saved health lands after this update.
        s_jumpFreshInit = false;
        zombie_init();
        ENTITY = e;
        e->state = ZOMBIE_STATE_IDLE;
        bool prone = (e->behavior_flags & ZOMBIE_FLAG_LAYING_DOWN) != 0;
        zm_start(e, prone ? ZM_ACT_PRONE : ZM_ACT_IDLE, prone ? ZM_ANIM_PRONE : ZM_ANIM_IDLE);
    } else if (e->state == ZOMBIE_STATE_INIT) {
        // The real init: SCA record, shadow quads, poise, joint state.
        zombie_init();
        e->state = ZOMBIE_STATE_IDLE;
        e->behavior_flags = 0;
        e->health = s_carriedHealth;
        if (s_carriedProne) {
            e->behavior_flags |= ZOMBIE_FLAG_LAYING_DOWN;
            zm_start(e, ZM_ACT_PRONE, ZM_ANIM_PRONE);
        } else {
            zm_start(e, ZM_ACT_IDLE, ZM_ANIM_IDLE);
        }
    }

    if (s_jumpFixCamera && (g_main_state_flags & MSF_CAMERA_LOCK) == 0) {
        s_jumpFixCamera = false;
        zm_fix_camera(e);
    }

    // Mid-grab on the survivor's copy: show the zombie exactly as it is being
    // played there. Nothing local moves it.
    if (s_remoteGrab) {
        zm_remote_grab_frame(e);
        zm_draw_shadow(e);
        return;
    }

    // Same gate as zombie_update: enemies freeze while the message system
    // holds them (cutscenes, menus).
    if ((g_message_flags & 0x0004) != 0) {
        if (s_vomiting && e->state != ZOMBIE_STATE_IDLE) {
            s_vomiting = false;     // a hit cut it short: the engine has it now
        }
        if (e->state == ZOMBIE_STATE_EATING) {
            unsigned int held = 0, pressed = 0;
            if (zm_director_input(e, false, &held, &pressed)) return;
            // The killer may already be possessed when feeding starts. Its
            // movement/attack input gets it up; START/map controls still work.
            if (e->action_state != 3 && held != 0) zombie_mode_stand_feeding(e);
            s_engineOwned = true;
            zombie_mode_feeding_update(e);
            zm_draw_shadow(e);
            return;
        }
        bool engine = zm_engine_turn(e);
        if (!engine && s_vomiting) {
            zm_vomit_frame(e);
        } else if (!engine) {
            unsigned int held = 0, pressed = 0;
            bool busy = s_biteVictim != NULL || s_riding || e->action_behavior == ZM_ACT_STEP;
            if (zm_director_input(e, busy, &held, &pressed)) {
                return;
            }
            zm_control(e, held, pressed);
            ENTITY = e;
            // The engine may have taken over during the press (the grab).
            if (e->state != ZOMBIE_STATE_IDLE) {
                zm_engine_turn(e);
            }
        }
        ENTITY = e;

        zm_update_awareness(e);
        e->state_mirror = e->state;
        e->ignore_player_flag_mirror = e->ignore_player_flag;
        e->action_behavior_mirror = e->action_behavior;
        e->attack_behavior_mirror = e->action_state;
        if (e->internal_timer != 0) {
            e->internal_timer--;
        }

        // zombie_update's collision tail. The survivor push only while it is
        // actually in the room (parked, it is nowhere near).
        // Mid-step the walls of the stair well would shove it back to where it
        // started; the player's own step skips collision the same way.
        bool stepping = !s_engineOwned &&
                        (e->action_behavior == ZM_ACT_STEP || e->action_behavior == ZM_ACT_RIDE);
        if (e->state != ZOMBIE_STATE_ATTACK && !stepping) {
            SetEntityScaHitData(e);
            if (zm_survivor_present()) {
                ResolveEntityScaCollision((Entity*)&g_playerEntity, e);
            }
            HandleEnemyPlayerCollisions();
            e->collisionFlags &= ~0x08;
            e->collisionFlags |= ZM_COLLISION_LIKE_PLAYER;
            e->dir_control_flags |= check_room_collision(
                (VECTOR*)&e->scaMatrixData.localMatrix.t, *(short*)(e->Sca_info + 10));
            // A body on the floor is ~1200 units long: push both ends out of
            // the walls as zombie_update does for a laying zombie.
            if ((e->behavior_flags & ZOMBIE_FLAG_LAYING_DOWN) != 0 &&
                e->action_behavior != ZM_ACT_LIE_DOWN && e->action_behavior != ZM_ACT_GET_UP) {
                e->dir_control_flags |= check_room_collision_two_point(&s_bodyEndBack, &s_bodyEndFront);
            }
        }
        if ((!zm_is_prone(e) || s_engineOwned) && !stepping) {
            zm_stairs_height(e);
        }
        zm_check_death(e);
    }

    // Every two seconds, what the zombie and the room look like - for tracking
    // down anything that makes it vanish (RE1_DEBUGLOG=1 -> re1_debug.log).
    static unsigned int s_diagTick = 0;
    if (++s_diagTick % 60 == 0) {
        dbg_printf("[zombie] slot %d status %02X state %d act %d/%d pos (%d,%d,%d) "
                   "hp %d | enemies %d cam %d msg %04X msf %08X survivor %s\n",
                   (int)(e - g_EnemiesList), (int)e->status_flags, (int)e->state,
                   (int)e->action_behavior, (int)e->action_state,
                   (int)e->scaMatrixData.localMatrix.t[0], (int)e->scaMatrixData.localMatrix.t[1],
                   (int)e->scaMatrixData.localMatrix.t[2], (int)e->health, g_enemy_count,
                   (int)g_roomCameraId, (unsigned int)g_message_flags,
                   (unsigned int)g_main_state_flags, zm_survivor_present() ? "here" : "away");
    }

    zm_draw_shadow(e);
}

// ===========================================================================
// The player entity's turn (from game_loop, in place of the player's update)
// ===========================================================================
void zombie_mode_player_update(void)
{
    zm_survivor_frame();
}

// ===========================================================================
// Two players: puppets and the per-frame link
// ===========================================================================

// Pose an entity from the other copy's skeleton: Joint_move's direct mode
// (PlayerAnimations.cpp, blend_counter 0), fed from the packet instead of an
// animation frame - root translation, then each joint's rotation into its
// transform. Joints flagged 0x10 keep their own rotation, as Joint_move does.
void zm_apply_pose(Entity* e, const ZmNetPose* pose)
{
    JointStruct* j = e->jointsStructs;
    if (j == NULL || pose->jointCount == 0) return;
    int n = e->jointCount < pose->jointCount ? e->jointCount : pose->jointCount;
    j[0].transform.t[0] = pose->root[0];
    j[0].transform.t[1] = pose->root[1];
    j[0].transform.t[2] = pose->root[2];
    for (int i = 0; i < n; i++) {
        if ((j[i].flags & 0x10) != 0) continue;
        j[i].rotation.x = pose->rot[i][0];
        j[i].rotation.y = pose->rot[i][1];
        j[i].rotation.z = pose->rot[i][2];
        RotMatrix(&j[i].rotation, &j[i].transform);
        int slot = (int)(e - g_EnemiesList);
        if (slot >= 0 && slot < 30 && (s_jointTintMask[slot] & (1u << i)) != 0 &&
            (j[i].flags & 0x80) == 0) {
            j[i].flags |= 0x80;
            JointSetColorTint((int)j[i].anim_object, s_jointTint[slot][i]);
        }
    }
}

void zm_apply_net_pose(Entity* e, const ZmNetPeerState* p)
{
    JointStruct* j = e->jointsStructs;
    if (j == NULL || p->jointCount == 0) return;
    int n = e->jointCount < p->jointCount ? e->jointCount : p->jointCount;
    j[0].transform.t[0] = p->root[0];
    j[0].transform.t[1] = p->root[1];
    j[0].transform.t[2] = p->root[2];
    for (int i = 0; i < n; i++) {
        if ((j[i].flags & 0x10) != 0) continue;
        j[i].rotation.x = p->rot[i][0];
        j[i].rotation.y = p->rot[i][1];
        j[i].rotation.z = p->rot[i][2];
        RotMatrix(&j[i].rotation, &j[i].transform);
    }
}

static bool zm_state_here(const ZmNetPeerState* p)
{
    return p != NULL && p->valid && p->stage == g_stageId && p->room == g_roomId;
}

static int zm_slot_of(const Entity* e)
{
    int slot = (int)(e - g_EnemiesList);
    return (slot >= 0 && slot < 30) ? slot : -1;
}

// ---------------------------------------------------------------------------
// The shared room
//
// Each copy loads only its own player's room. When players share one, a
// single copy runs its monsters - the room's OWNER: the director if it is
// there, otherwise the lowest-numbered survivor there. The owner runs every
// monster in it - the AI ones and the possessed one alike - and sends them all
// (PKT_ENEMIES) every frame. The other copies there turn their own copies of
// those monsters into puppets: placed and posed from the packet by enemy slot,
// shots forwarded to the owner (ZM_EV_HIT), sound cues played (ZM_EV_SOUND).
//
// The owner's monsters go for the nearest survivor (zm_target_begin). One that
// goes for a survivor of another copy hands the attack to that copy
// (ZM_EV_GRAB), which runs the real zombie_attack on its puppet - where that
// survivor is real - and sends the zombie's pose back with its STATE until it
// is over (ZM_EV_GRAB_END).
//
// Ownership moves with the players. A copy that loses the room to a new owner
// sends its monsters once ("adopt") so the new owner starts from where they
// really are; a copy that gains it from an owner that left takes its puppets
// back to their own AI from where they stand.
// ---------------------------------------------------------------------------
static bool  s_slotShown[30];                  // puppets: drawn this frame
static short s_slotHealth[30];                 // puppets: health they were given
// Dead when this copy took the room over from an owner that left: the corpse
// stays where the owner last posed it and never runs its own update again (a
// puppet's state is held at idle, so its AI would walk it off alive).
static bool  s_slotCorpse[30];
// Negative health starts death; it does not mean the fall has finished.
// The original monster controllers set DEAD when the body reaches its rest
// pose. Zombies also use that bit during earlier death phases.
bool zm_monster_death_settled(const Entity* e)
{
    int slot = (int)(e - g_EnemiesList);
    if (slot >= 0 && slot < 30 && s_slotCorpse[slot]) return true;
    if (e->health >= 0 || (e->status_flags & ENTITY_STATUS_DEAD) == 0) return false;
    return !zm_is_zombie_id(e->id) ||
        (e->state == ZOMBIE_STATE_DIE && e->action_state >= 3);
}
static ZmNetEnemy s_snap[30];                  // puppets: newest from the owner
static bool  s_snapHave[30];
static unsigned int s_snapMs = 0;

// Victim's copy: the monster running zombie_attack for its owner (-1), and
// which player asked.
static int   s_grabSlot = -1;
static int   s_grabFrames = 0;
static int   s_grabOwner = -1;
static int   s_grabOwnerSlot = -1;         // the owner's slot number for it
static int   s_grabPendingSlot = -1;           // asked while the menu was open
static bool  s_grabPendingProne = false;
static int   s_grabPendingOwner = -1;

// Owner's copy: AI zombies whose attack is running on a victim's copy.
static bool         s_slotRemote[30];
static int          s_slotRemoteVictim[30];
static unsigned int s_slotRemoteMs[30];
static bool         s_slotRemoteSeen[30];
// Owner's copy: each monster's burst joints already sent (ZM_EV_BURST, below).
static unsigned short s_burstSent[30];

static bool zm_survivor_role(void) { return s_gameRole == ZM_NET_SURVIVOR; }
static bool zm_director_role(void) { return s_gameRole == ZM_NET_ZOMBIE; }

// ---------------------------------------------------------------------------
// The door stun
//
// A survivor coming through a door stuns the monsters within ZM_STUN_RADIUS of
// where it stands, for ZM_STUN_MS, so a director camping a doorway with a
// pack gets no free grab on whoever opens it. Monsters further into the room
// carry on. Stunned is the engine's own freeze: every monster's update skips
// its state handler while bit 0x0004 of g_message_flags is clear (cutscenes,
// menus), so that bit is cleared around the update. Only the normal state (1)
// freezes - hit reactions, deaths and a grab already under way (a zombie's
// state 5) still play. The collision sits inside the same gate, so a stunned
// monster is not solid either: the survivor can slip past.
//
// The survivor's own copy says when: the first frame it has control in the new
// room (ZM_EV_STUN { x, z, stage | room << 8 } to everyone). Only then is its
// position the one it came in at - its STATE names the new room while it is
// still loading. The room's owner, which runs the monsters, stuns them; a
// survivor that owns the room it came into stuns its own. A monster cannot be
// stunned again until ZM_STUN_IMMUNITY_MS after its last stun ends, so
// survivors taking turns at a door cannot keep it frozen.
// ---------------------------------------------------------------------------
#define ZM_STUN_MS          5000
#define ZM_STUN_IMMUNITY_MS 10000       // recovery after the five-second stun
#define ZM_STUN_RADIUS      2200        // about 6 feet
static unsigned int s_slotStunUntilMs[30];
static unsigned int s_slotStunImmuneMs[30];
static unsigned char s_slotStunRun[30];      // its own updates in this room (a pose to freeze in)
static int  s_stunRoomKey = -1;
static bool s_stunAnnounce = false;          // this survivor came in: tell the owner once in play
static bool s_stunHeld = false;              // this update runs with bit 0x0004 cleared

static bool zm_ms_before(unsigned int a, unsigned int b) { return (int)(a - b) < 0; }

static void zm_stun_slot_reset(int slot)
{
    s_slotStunUntilMs[slot] = 0;
    s_slotStunImmuneMs[slot] = 0;
    s_slotStunRun[slot] = 0;
}

static void zm_stun_new_game(void)
{
    for (int i = 0; i < 30; i++) zm_stun_slot_reset(i);
    s_stunRoomKey = -1;
    s_stunAnnounce = false;
    s_stunHeld = false;
}

// Stun the monsters this copy runs within reach of (x, z).
static void zm_stun_at(int x, int z, const char* who)
{
    unsigned int now = zm_game_time_ms();
    int stunned = 0;
    for (int slot = 0; slot < ZM_FIRST_SURVIVOR_SLOT; slot++) {
        const Entity* e = &g_EnemiesList[slot];
        if ((e->status_flags & ENTITY_STATUS_ACTIVE) == 0 || e->id >= NPC_ENTITIES_IDS) continue;
        if (zombie_mode_is_puppet(e) || e->health < 0 || zm_yawn_entity(e)) continue;   // no boss stun
        long long dx = (long long)e->scaMatrixData.localMatrix.t[0] - x;
        long long dz = (long long)e->scaMatrixData.localMatrix.t[2] - z;
        if (dx * dx + dz * dz > (long long)ZM_STUN_RADIUS * ZM_STUN_RADIUS) continue;
        if (s_slotStunImmuneMs[slot] != 0 && zm_ms_before(now, s_slotStunImmuneMs[slot])) {
            dbg_printf("[stun] slot %d (id %02X) in reach, still on cooldown\n", slot, (unsigned)e->id);
            continue;
        }
        s_slotStunUntilMs[slot] = (now + ZM_STUN_MS) | 1;
        s_slotStunImmuneMs[slot] = (s_slotStunUntilMs[slot] + ZM_STUN_IMMUNITY_MS) | 1;
        stunned++;
        dbg_printf("[stun] slot %d (id %02X%s) stunned for %d ms\n", slot, (unsigned)e->id,
                   e == g_zombieModeEntity ? ", the director's" : "", ZM_STUN_MS);
    }
    dbg_printf("[stun] %s came in at (%d, %d), stage %d room %02X: %d stunned\n", who, x, z,
               (int)g_stageId, (int)g_roomId, stunned);
}

// The single-player survivor, now the loaded room's player entity.
void zm_stun_room(const char* who)
{
    zm_stun_at(g_playerEntity.scaMatrixData.localMatrix.t[0], g_playerEntity.scaMatrixData.localMatrix.t[2], who);
}

// Room load. A camera-only door reloads the same room: that is no entrance.
static void zm_stun_room_reset(void)
{
    int key = (int)g_stageId | ((int)g_roomId << 8);
    bool newRoom = key != s_stunRoomKey;
    s_stunRoomKey = key;
    if (!newRoom) return;
    for (int i = 0; i < 30; i++) zm_stun_slot_reset(i);
    s_stunAnnounce = zm_survivor_role();
}

// Once a frame: this survivor has control in the room it came into.
static void zm_stun_frame(void)
{
    if (!s_stunAnnounce) return;
    if ((g_message_flags & 0x0101) != 0x0101 || (g_main_state_flags & MSF_MENU_ACTIVE) != 0 ||
        g_playerEntity.health < 0) {
        return;
    }
    s_stunAnnounce = false;
    int x = g_playerEntity.scaMatrixData.localMatrix.t[0];
    int z = g_playerEntity.scaMatrixData.localMatrix.t[2];
    zm_net_send_event(ZM_EV_STUN, (short)x, (short)z, (short)(g_stageId | (g_roomId << 8)), 0);
    if (s_iOwn) zm_stun_at(x, z, "this survivor");
}

// Give the survivor breathing room at the end of a bite. Use the original
// withdrawal reaction, starting at its first frame for zombies that were not
// in the paired attack. Facing the victim makes its backwards steps move away
// from the victim; the normal zombie collision tail keeps them out of walls.
static void zm_push_off_at(int x, int y, int z)
{
    const int radius = 1800;
    Entity* saved = ENTITY;
    for (int slot = 0; slot < 30; slot++) {
        Entity* e = &g_EnemiesList[slot];
        if ((e->status_flags & ENTITY_STATUS_ACTIVE) == 0 ||
            (e->status_flags & ENTITY_STATUS_DEAD) != 0 || e->health < 0 ||
            !zm_is_zombie_id(e->id) ||
            (e->behavior_flags & ZOMBIE_FLAG_LAYING_DOWN) != 0 ||
            (e->action_speed & 0x80) != 0 ||
            (e == g_zombieModeEntity && !s_engineOwned && zm_is_prone(e))) continue;
        // Leave paired attacks and reactions already under way intact. This
        // also excludes the zombie that the survivor just pushed off.
        if (e->state != ZOMBIE_STATE_IDLE && e->state != ZOMBIE_STATE_CHASE &&
            e->state != ZOMBIE_STATE_CHASE2 &&
            e->state != ZOMBIE_STATE_RANDOM_CHASE &&
            e->state != ZOMBIE_STATE_PUSHED_BACK && e->state != ZOMBIE_STATE_PUSHED_BACK2)
            continue;
        int dx = e->scaMatrixData.localMatrix.t[0] - x;
        int dz = e->scaMatrixData.localMatrix.t[2] - z;
        int dy = e->scaMatrixData.localMatrix.t[1] - y;
        // Bound the deltas before squaring: coordinates are 16-bit, and the
        // game's arithmetic stays 32-bit.
        if (dx < -radius || dx > radius || dz < -radius || dz > radius ||
            dy < -400 || dy > 400 || dx * dx + dz * dz > radius * radius) continue;
        ENTITY = e;
        VECTOR victim = { x, y, z, 0 };
        if (check_line_of_sight(&victim)) continue;
        e->angle = getAngleTowardsTarget(x, z);
        e->state = ZOMBIE_STATE_DAMAGED;
        e->ignore_player_flag = 1;
        e->action_behavior = 6;
        e->action_state = 1;
        e->animationId = 7;
        e->animation_frame_id = 0;
        e->timing_control = 0;
        e->blend_counter = 3;
        e->move_speed_current = 30;
        e->hit_state = 1;
        dbg_printf("[push off] slot %d recoils\n", slot);
    }
    ENTITY = saved;
}

void zombie_mode_grab_push_off(void)
{
    if (!s_zombieModeArmed || g_playerEntity.health < 0) return;
    int x = g_playerEntity.scaMatrixData.localMatrix.t[0];
    int y = g_playerEntity.scaMatrixData.localMatrix.t[1];
    int z = g_playerEntity.scaMatrixData.localMatrix.t[2];
    if (s_iOwn) zm_push_off_at(x, y, z);
    else zm_net_send_event(ZM_EV_PUSH_OFF, (short)x, (short)z,
                          (short)(g_stageId | (g_roomId << 8)), (short)y);
}

// ZM_EV_STUN from another survivor's copy.
static void zm_stun_take(const short* a, int src)
{
    if (!s_iOwn || a[2] != (short)(g_stageId | (g_roomId << 8))) return;
    char who[16];
    snprintf(who, sizeof(who), "survivor %d", src);
    zm_stun_at((unsigned short)a[0], (unsigned short)a[1], who);
}

// update_entities, around one monster's update.
static void zm_stun_before_update(Entity* e)
{
    s_stunHeld = false;
    int slot = zm_slot_of(e);
    if (slot < 0 || e->id >= NPC_ENTITIES_IDS || zm_yawn_entity(e)) return;
    if (e->state == 0) {
        zm_stun_slot_reset(slot);       // a new monster in this slot
        return;
    }
    bool stunned = s_slotStunUntilMs[slot] != 0 && zm_ms_before(zm_game_time_ms(), s_slotStunUntilMs[slot]);
    // Not before it has run twice: its own updates pose it first.
    if (!stunned || s_slotStunRun[slot] < 2 || e->state != 1 || zombie_mode_is_puppet(e) ||
        zombie_mode_is_remote_grabbed(e) || (g_message_flags & 0x0004) == 0) {
        if (s_slotStunRun[slot] < 2) s_slotStunRun[slot]++;
        return;
    }
    g_message_flags &= ~0x0004;
    s_stunHeld = true;
}

static void zm_stun_after_update(void)
{
    if (!s_stunHeld) return;
    s_stunHeld = false;
    g_message_flags |= 0x0004;
}

// Is this player in the loaded room? This copy's own player is, unless it
// is a dead survivor watching from elsewhere.
static bool zm_player_here(int i)
{
    // A dead survivor watching the others is in no room: never the owner,
    // never "here" for anyone (ZombieSpectate.cpp).
    if (i == zm_net_self()) return !zm_spec_away();
    if (i != ZM_NET_DIRECTOR && zm_net_char(i) < 0) return false;
    const ZmNetPeerState* p = zm_net_player(i);
    if (p != NULL && (p->spectating || p->transitioning)) return false;
    return zm_state_here(p);
}

// This is a room ownership lease, not a new grab. Keep the original
// attack driver on its current copy until both sides complete playback.
bool zombie_mode_room_pair_busy(void)
{
    if (!s_zombieModeArmed || s_gameRole == ZM_NET_OFF || !s_iOwn) return false;
    if (s_remoteGrab || s_grabSlot >= 0) return true;
    // A local victim may be in the release phase after the monster left
    // attack state. Preserve that phase as well as the damage loop.
    if (zm_survivor_role() && (g_playerEntity.animationId == 5 ||
        g_playerEntity.animationId == 6 || g_playerEntity.animationId == 7)) return true;
    for (int i = 0; i < ZM_FIRST_SURVIVOR_SLOT; i++) {
        const Entity* e = &g_EnemiesList[i];
        if ((e->status_flags & ENTITY_STATUS_ACTIVE) == 0) continue;
        if (s_slotRemote[i]) return true;
        if (zm_is_zombie_id(e->id) && e->state == ZOMBIE_STATE_ATTACK && e->health >= 0) return true;
        if (e->id == ENEMY_TYRANT_2 && e->health >= 0 &&
            (e->state == 1 || e->state == 2) && e->ignore_player_flag != 0 &&
            e->action_behavior == 7) return true;
    }
    return false;
}

static int zm_compute_owner(void)
{
    if (!zm_spec_away() && zombie_mode_room_pair_busy()) return zm_net_self();
    for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
        if (i == zm_net_self() || !zm_player_here(i)) continue;
        const ZmNetPeerState* p = zm_net_player(i);
        if (p != NULL && p->roomPairBusy) return i;
    }
    for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
        if (zm_player_here(i)) return i;
    }
    // Nobody: a spectator's copy runs nothing - its monsters wait as puppets.
    return zm_spec_away() ? -1 : zm_net_self();
}

unsigned char zm_survivor_entrance_door(void)
{
    return s_gameRole == ZM_NET_SURVIVOR ? s_survivorEntranceDoor : 0xFF;
}

int zm_room_owner_here(void)
{
    return zm_compute_owner();
}

void zm_reinforce_notice(const char* text)
{
    zm_note(text);
}

// A dead survivor's copy is showing this room (ZombieSpectate.cpp): the owner
// sends its monsters for it, as for a player here.
static bool zm_spectated_here(void)
{
    for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
        if (i == zm_net_self()) continue;
        const ZmNetPeerState* p = zm_net_player(i);
        if (p != NULL && p->spectating && p->viewStage == g_stageId && p->viewRoom == g_roomId) return true;
    }
    return false;
}

static bool zm_others_here(void)
{
    for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
        if (i != zm_net_self() && zm_player_here(i)) return true;
    }
    return false;
}

static void zm_shared_room_reset(void)
{
    memset(s_jointTintMask, 0, sizeof(s_jointTintMask));
    memset(s_jointHiddenMask, 0, sizeof(s_jointHiddenMask));
    s_shared = false;
    s_owner = (s_gameRole == ZM_NET_OFF) ? -1 : zm_compute_owner();
    s_iOwn = (s_gameRole == ZM_NET_OFF) || s_owner == zm_net_self();
    s_ownerFrames = 0;
    s_puppetFrames = 0;
    s_corpseAdoptFrames = 0;
    memset(s_slotShown, 0, sizeof(s_slotShown));
    memset(s_slotCorpse, 0, sizeof(s_slotCorpse));
    memset(s_snapHave, 0, sizeof(s_snapHave));
    memset(s_slotRemote, 0, sizeof(s_slotRemote));
    memset(s_tyrantTarget, -1, sizeof(s_tyrantTarget));
    memset(s_tyrantReaction, 0, sizeof(s_tyrantReaction));
    s_tyrantVictimSlot = -1;
    s_tyrantPendingHave = false;
    memset(s_tyrantTrailCues, 0, sizeof(s_tyrantTrailCues));
    memset(s_burstSent, 0, sizeof(s_burstSent));
    s_grabSlot = -1;
    s_grabPendingSlot = -1;
    s_targetIdx = -1;
    s_targetSwapped = false;
    for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
        s_standIns[i].baseSaved = false;
        s_standIns[i].weapon = 0;
        s_standInFailedChar[i] = -1;        // a new room has its own memory and banks
    }
    zm_slot_map_reset();
    s_liveFailedCount = 0;
    zm_ext_reset();
    zm_stun_room_reset();
}

bool zombie_mode_is_puppet(const Entity* e)
{
    if (zm_greenhouse_vine(e)) return false;
    if (s_gameRole == ZM_NET_OFF || e == NULL) return false;
    int slot = zm_slot_of(e);
    if (slot < 0 || e == g_zombieModeEntity) return false;
    if (zm_is_survivor_slot(slot) && e->id >= NPC_ENTITIES_IDS) return true;
    return e == s_puppetZombie || !s_iOwn || slot == s_grabSlot || s_slotCorpse[slot];
}

bool zombie_mode_hide_entity(const Entity* e)
{
    if (zm_shotgun_hide_room()) return true;
    if (!zombie_mode_is_puppet(e)) return false;
    return !s_slotShown[zm_slot_of(e)];
}

bool zombie_mode_is_remote_grabbed(const Entity* e)
{
    int slot = zm_slot_of(e);
    return s_gameRole != ZM_NET_OFF && s_iOwn && slot >= 0 && e != g_zombieModeEntity && s_slotRemote[slot];
}

// From the network: the room owner's monsters, or - adopt - the room handed
// over by the copy that ran it until now.
// The owner's monsters arrive by ITS slot numbers, which need not be this
// copy's: a placed monster lands in whatever slot was free on each copy, and
// the director's body sits wherever its copy put it. Each is matched by the
// uid it is kept under instead - a script spawn by its spawn slot, a placed
// one by its roster uid (and made here if this copy has none yet), the
// director's body to this copy's puppet zombie - and every slot number that
// crosses the link afterwards (hits, grabs, sounds) goes through this map.
static signed char s_localOf[30];       // owner slot -> slot here, -1 unknown
static signed char s_ownerOf[30];       // slot here -> owner slot, -1 unknown

static void zm_slot_map_reset(void)
{
    memset(s_localOf, -1, sizeof(s_localOf));
    memset(s_ownerOf, -1, sizeof(s_ownerOf));
}

static int zm_local_slot_for(const ZmNetEnemy& n, bool mayCreate)
{
    unsigned short uid = n.uid;
    // Late UDP poses must not recreate a body that the reliable roster has
    // already removed from this room.
    if (zm_world_departed(uid, n.id)) return -1;
    if (uid == ZM_WORLD_UID_BODY) {
        return s_puppetZombie != NULL ? (int)(s_puppetZombie - g_EnemiesList) : -1;
    }
    if (uid < 16) return uid;
    if (uid >= 0x100 && uid < ZM_WORLD_UID_BODY) {
        int slot = zm_world_slot_of_uid(uid);
        if (slot < 0 && mayCreate) {
            Entity* e = zm_spawn_live(n.id, 0, n.pose.x, n.pose.y, n.pose.z, n.pose.angle, uid);
            if (e != NULL) slot = (int)(e - g_EnemiesList);
        }
        return slot;
    }
    return n.slot;                      // untracked: same slot on every copy
}

void zm_shared_remove_departed(unsigned short uid, unsigned char id)
{
    int slot = zm_world_slot_of_uid(uid);
    if (slot < 0 || slot >= ZM_FIRST_SURVIVOR_SLOT) return;
    Entity* e = &g_EnemiesList[slot];
    if (e->id != id || e == g_zombieModeEntity || e == s_puppetZombie) return;
    // A reliable departure can overtake an in-progress victim-side grab.
    // Do not leave the survivor waiting for an attacker that no longer exists.
    if (s_grabSlot == slot) {
        s_grabSlot = s_grabOwner = s_grabOwnerSlot = -1;
        g_playerEntity.animationId = 1;
        g_playerEntity.animFrameId = 0;
        g_playerEntity.action_behavior = 0;
        g_playerEntity.action_state = 0;
        g_playerEntity.isBeingAttackedFlag = 0;
        g_playerEntity.flags &= (unsigned char)~6;
        g_playerEntity.unk_b8 = 0;
        // A negative health now reaches the ordinary survivor death path.
    }
    if (s_grabPendingSlot >= 0 &&
        (s_grabPendingSlot == s_ownerOf[slot] || (s_ownerOf[slot] < 0 && s_grabPendingSlot == slot)))
        s_grabPendingSlot = -1;
    if ((e->status_flags & ENTITY_STATUS_ACTIVE) != 0) g_enemy_count--;
    e->status_flags = 0;
    s_slotShown[slot] = s_slotCorpse[slot] = s_snapHave[slot] = false;
    s_jointTintMask[slot] = 0;
    s_jointHiddenMask[slot] = 0;
    s_slotRemote[slot] = false;
    if (s_ownerOf[slot] >= 0) s_localOf[s_ownerOf[slot]] = -1;
    s_ownerOf[slot] = -1;
    zm_stun_slot_reset(slot);
    dbg_printf("[world] carried uid %04X id %02X removed from slot %d room %d/%02X\n",
        uid, id, slot, (int)g_stageId, (int)g_roomId);
}

// The slot number the owner knows this copy's monster by.
static int zm_owner_slot(int localSlot)
{
    if (localSlot < 0 || localSlot >= 30) return localSlot;
    return s_ownerOf[localSlot] >= 0 ? s_ownerOf[localSlot] : localSlot;
}

static int zm_local_slot(int ownerSlot)
{
    if (ownerSlot < 0 || ownerSlot >= 30) return -1;
    return s_localOf[ownerSlot] >= 0 ? s_localOf[ownerSlot] : ownerSlot;
}

// The uid each monster is kept under, for the packet.
static unsigned short zm_wire_uid(const Entity* e)
{
    if (e == s_bodyEntity) return ZM_WORLD_UID_BODY;
    return zm_world_uid_of_slot((int)(e - g_EnemiesList));
}
void zombie_mode_on_joint_tint(const JointStruct* joint, unsigned int color)
{
    if (s_gameRole == ZM_NET_OFF) return;
    for (int slot = 0; slot < ZM_FIRST_SURVIVOR_SLOT; slot++) {
        Entity* e = &g_EnemiesList[slot];
        if ((e->status_flags & ENTITY_STATUS_ACTIVE) == 0 || e->id >= NPC_ENTITIES_IDS ||
            e->jointsStructs == NULL) continue;
        for (int i = 0; i < e->jointCount && i < ZM_NET_JOINTS; i++) {
            if (joint != &e->jointsStructs[i]) continue;
            if ((s_jointTintMask[slot] & (1u << i)) != 0 && s_jointTint[slot][i] == color) return;
            s_jointTintMask[slot] |= 1u << i;
            s_jointTint[slot][i] = color;
            zm_net_send_event8(ZM_EV_JOINT_TINT, (short)zm_wire_uid(e), (short)i,
                (short)color, (short)(color >> 16), (short)(g_stageId | (g_roomId << 8)), e->id, 0, 0);
            return;
        }
    }
}
static void zm_take_joint_tint(const short* a)
{
    if (a[4] != (short)(g_stageId | (g_roomId << 8)) || a[1] < 0 || a[1] >= ZM_NET_JOINTS) return;
    ZmNetEnemy n = {};
    n.uid = (unsigned short)a[0]; n.id = (unsigned char)a[5]; n.slot = 0xFF;
    int slot = zm_local_slot_for(n, false);
    if (slot < 0 || slot >= ZM_FIRST_SURVIVOR_SLOT) return;
    Entity* e = &g_EnemiesList[slot];
    if (e->id != n.id || e->jointsStructs == NULL || a[1] >= e->jointCount) return;
    unsigned int color = (unsigned short)a[2] | ((unsigned int)(unsigned short)a[3] << 16);
    s_jointTintMask[slot] |= 1u << a[1];
    s_jointTint[slot][a[1]] = color;
    JointStruct& joint = e->jointsStructs[a[1]];
    joint.flags |= 0x80;
    JointSetColorTint((int)joint.anim_object, color);
    dbg_printf("[tint] p%d received uid %04X slot %d joint %d color %08X\n",
        zm_net_self(), (unsigned short)a[0], slot, a[1], color);
}
void zm_monster_appearance_capture(const Entity* e, ZmMonsterAppearance* out)
{
    memset(out, 0, sizeof(*out));
    int slot = (int)(e - g_EnemiesList);
    if (slot < 0 || slot >= 30 || e->jointsStructs == NULL) return;
    out->hiddenMask = s_jointHiddenMask[slot];
    out->tintMask = s_jointTintMask[slot];
    memcpy(out->color, s_jointTint[slot], sizeof(out->color));
    for (int i = 0; i < e->jointCount && i < ZM_NET_JOINTS; i++) {
        unsigned char f = e->jointsStructs[i].flags;
        if ((f & 1) == 0 && (f & 0x28) != 0) out->hiddenMask |= 1u << i;
    }
}
void zm_monster_appearance_apply(Entity* e, const ZmMonsterAppearance& appearance)
{
    int slot = (int)(e - g_EnemiesList);
    if (slot < 0 || slot >= 30 || e->jointsStructs == NULL) return;
    s_jointHiddenMask[slot] |= appearance.hiddenMask;
    for (int i = 0; i < e->jointCount && i < ZM_NET_JOINTS; i++) {
        JointStruct& joint = e->jointsStructs[i];
        if ((s_jointHiddenMask[slot] & (1u << i)) != 0) {
            // Restore missing pieces silently. Setting up a new burst would
            // replay gore/sound and allocate a trail for an old corpse.
            joint.flags = (unsigned char)((joint.flags | 0x20) & ~1);
        }
        if ((appearance.tintMask & (1u << i)) == 0) continue;
        unsigned int color = appearance.color[i];
        if ((s_jointTintMask[slot] & (1u << i)) == 0 || s_jointTint[slot][i] != color ||
            (joint.flags & 0x80) == 0) {
            joint.flags |= 0x80;
            JointSetColorTint((int)joint.anim_object, color);
        }
        s_jointTintMask[slot] |= 1u << i;
        s_jointTint[slot][i] = color;
    }
}

void zombie_mode_tyrant_trail_started(const Entity* e, unsigned short frames)
{
    if (!s_zombieModeArmed || s_gameRole == ZM_NET_OFF || !s_iOwn ||
        e->id != ENEMY_TYRANT_2 || zombie_mode_is_puppet(e)) return;
    zm_net_send_event(ZM_EV_TYRANT_TRAIL, (short)(g_stageId | (g_roomId << 8)),
        (short)zm_wire_uid(e), (short)frames, 0);
    dbg_printf("[tyrant trail] sent uid %04X frames %u\n", zm_wire_uid(e), frames);
}

static void zm_tyrant_take_trail(const short* a)
{
    if (a[0] != (short)(g_stageId | (g_roomId << 8)) || a[2] <= 0 || a[2] > 120) return;
    unsigned short uid = (unsigned short)a[1];
    int freeSlot = -1;
    unsigned int now = zm_game_time_ms();
    for (int i = 0; i < 30; i++) {
        ZmTyrantTrailCue& c = s_tyrantTrailCues[i];
        if (c.uid == uid && c.frames != 0) { freeSlot = i; break; }
        if (freeSlot < 0 && (c.frames == 0 || now - c.receivedMs > 2000)) freeSlot = i;
    }
    if (freeSlot < 0) return;
    ZmTyrantTrailCue& c = s_tyrantTrailCues[freeSlot];
    c.uid = uid;
    c.frames = (unsigned short)a[2];
    c.receivedMs = now;
    dbg_printf("[tyrant trail] received uid %04X frames %u\n", uid, c.frames);
}

static void zm_tyrant_draw_puppet(Entity* e)
{
    if (e->id != ENEMY_TYRANT_2) return;
    unsigned short uid = e == s_puppetZombie ? ZM_WORLD_UID_BODY : zm_wire_uid(e);
    unsigned short frames = 0;
    unsigned int now = zm_game_time_ms();
    for (int i = 0; i < 30; i++) {
        ZmTyrantTrailCue& c = s_tyrantTrailCues[i];
        if (now - c.receivedMs > 2000) c.frames = 0;
        if (c.frames != 0 && c.uid == uid) {
            frames = c.frames;
            c.frames = 0;
            break;
        }
    }
    extern void tyrant_puppet_visuals(unsigned short trailFrames);
    tyrant_puppet_visuals(frames);
}

// Mid-room model loads still need the type's skeleton and collision setup
// before a network pose is applied, even when the monster is already dead.
static void zm_init_puppet(Entity* e)
{
    if (e->state != ZOMBIE_STATE_INIT) return;
    Entity* saved = ENTITY;
    ENTITY = e;
    void* init = enemies_update_functions_tbl[e->id];
    if (init != NULL) ((void(*)())init)();
    ENTITY = saved;
    e->state = ZOMBIE_STATE_IDLE;
    e->collisionFlags |= ZM_COLLISION_LIKE_PLAYER;
    s_slotHealth[zm_slot_of(e)] = e->health;
}

static void zm_resume_death(Entity* e, const ZmNetEnemy& n)
{
    unsigned char* raw = (unsigned char*)e;
    const ZmDeathPlayback& d = n.death;
    e->status_flags = d.status;
    e->behavior_flags = d.behavior;
    memcpy(raw + 0x6C, d.movement, sizeof(d.movement));
    memcpy(raw + 0x84, d.state, sizeof(d.state));
    memcpy(raw + 0xBC, d.animation, sizeof(d.animation));
    memcpy(raw + 0x16C, d.phase, sizeof(d.phase));
    memcpy(raw + 0x170, d.extra, zm_death_extra_bytes(e->id));
    memcpy(raw + 0x178, d.tail, sizeof(d.tail));
    e->scaMatrixData.localMatrix.t[0] = n.pose.x;
    e->scaMatrixData.localMatrix.t[1] = n.pose.y;
    e->scaMatrixData.localMatrix.t[2] = n.pose.z;
    e->angle = n.pose.angle;
    zm_apply_pose(e, &n.pose);
    zm_monster_appearance_apply(e, n.appearance);
    zm_set_rollback(e);
    dbg_printf("[corpse] resumed slot %d anim %d frame %d phase %d/%d\n",
               zm_slot_of(e), e->animationId, e->animation_frame_id,
               e->ignore_player_flag, e->action_state);
}

void zm_shared_receive_enemies(const ZmNetEnemy* list, int count, bool adopt,
                               unsigned char stage, unsigned char room, int origin)
{
    if (stage != g_stageId || room != g_roomId || origin == zm_net_self()) return;
    if (adopt) {
        // Live AI handoffs have a short ownership window. Corpse refreshes
        // are valid for every copy still loading/watching this room, even
        // after that window: death is final for this monster uid this visit.
        for (int i = 0; i < count; i++) {
            const ZmNetEnemy& n = list[i];
            bool corpse = n.dead || n.health < 0;
            // An in-progress fall remains animated by the room owner. A
            // departing owner cannot publish it as a final frozen corpse.
            if (corpse && !n.settled) {
                if (s_iOwn && s_ownerFrames <= 120) {
                    int slot = zm_local_slot_for(n, true);
                    if (slot >= 0 && slot < ZM_FIRST_SURVIVOR_SLOT && !s_slotCorpse[slot]) {
                        Entity* e = &g_EnemiesList[slot];
                        if (e != g_zombieModeEntity && e->id == n.id &&
                            !(e->health < 0 && e->state == ZOMBIE_STATE_DIE)) {
                            zm_resume_death(e, n);
                        }
                    }
                }
                continue;
            }
            if (!corpse && (!s_iOwn || s_ownerFrames > 120)) continue;
            if (n.uid == ZM_WORLD_UID_BODY) continue;
            int slot = zm_local_slot_for(n, true);
            if (slot < 0 || slot >= ZM_FIRST_SURVIVOR_SLOT) continue;
            Entity* e = &g_EnemiesList[slot];
            if ((e->status_flags & ENTITY_STATUS_ACTIVE) == 0 || e->id != n.id) continue;
            if (e == g_zombieModeEntity) {
                // Taken by a map jump as the room loaded, at its roster spot -
                // which the old owner never updated while it ran the room.
                // Only where it really is: the director's controller keeps it.
                if (corpse || zm_yawn_entity(e)) continue;
                e->scaMatrixData.localMatrix.t[0] = n.pose.x;
                e->scaMatrixData.localMatrix.t[1] = n.pose.y;
                e->scaMatrixData.localMatrix.t[2] = n.pose.z;
                e->angle = n.pose.angle;
                if (e->state == ZOMBIE_STATE_INIT) {
                    zm_world_pending_health(e, n.health);   // its init sets its own first
                } else {
                    zm_world_accept_net_state(e);
                    e->health = n.health;
                }
                s_jumpFixCamera = true;
                dbg_printf("[zombie] possessed slot %d moved to its owner's spot (%d, %d)\n",
                           slot, (int)n.pose.x, (int)n.pose.z);
                continue;
            }
            if (!corpse && s_slotCorpse[slot] && s_snapHave[slot] && s_snap[slot].uid == n.uid)
                continue; // a delayed live handoff cannot undo confirmed death
            if (zm_yawn_head(e)) {
                // Yawn's init (its body segments) runs only with the
                // monsters' turn on; the snake waits for it below otherwise.
                if (e->state == ZOMBIE_STATE_INIT && (g_message_flags & 0x0004) == 0) continue;
                zm_yawn_init_here(e);
            } else {
                zm_init_puppet(e);
            }
            zm_world_accept_net_state(e);
            e->scaMatrixData.localMatrix.t[0] = n.pose.x;
            e->scaMatrixData.localMatrix.t[1] = n.pose.y;
            e->scaMatrixData.localMatrix.t[2] = n.pose.z;
            e->angle = n.pose.angle;
            e->health = n.health;
            if (zm_yawn_head(e)) {
                zm_yawn_pose_apply(e, n.pose, origin);
                for (int k = 0; k < ZM_YAWN_SLOTS; k++) zm_yawn_status_apply(&g_EnemiesList[k], k);
                zm_yawn_mark_scaled();
            }
            if (corpse) {
                if (e->health >= 0) e->health = -1;
                // Adopt the final joints, not an idle state that replays death.
                Entity* saved = ENTITY;
                ENTITY = e;
                ResetJointTransforms();
                ENTITY = saved;
                zm_apply_pose(e, &n.pose);
                zm_monster_appearance_apply(e, n.appearance);
                // Original death hold: dead, intangible, collision disabled.
                e->status_flags |= 0x0E;
                s_slotCorpse[slot] = s_slotShown[slot] = true;
                s_slotHealth[slot] = e->health;
                s_snap[slot] = n;
                s_snap[slot].dead = true;
                s_snap[slot].health = e->health;
                s_snapHave[slot] = true;
                dbg_printf("[corpse] p%d restored uid %04X slot %d from p%d joints %d room %d/%02X\n",
                           zm_net_self(), (unsigned)n.uid, slot, origin, (int)n.pose.jointCount,
                           (int)g_stageId, (int)g_roomId);
            }
            if (n.state == ZOMBIE_STATE_EATING && !n.dead) zombie_mode_begin_feeding(e, true);
            zm_set_rollback(e);
        }
        dbg_printf("[zombie] took over %d monsters from player %d\n", count, origin);
        return;
    }
    if (s_iOwn || origin != s_owner) return;
    // Corpses belong to this loaded room, even if the next owner has not
    // included them yet. Room reset clears them once this copy leaves.
    for (int i = 0; i < 30; i++) {
        if (!s_snapHave[i] || !s_snap[i].dead) s_snapHave[i] = false;
    }
    zm_slot_map_reset();
    for (int i = 0; i < count; i++) {
        const ZmNetEnemy& n = list[i];
        if (n.slot >= 30) continue;
        int slot = zm_local_slot_for(n, true);
        if (slot < 0 || slot >= ZM_FIRST_SURVIVOR_SLOT) continue;
        s_localOf[n.slot] = (signed char)slot;
        s_ownerOf[slot] = (signed char)n.slot;
        // An owner that just loaded may still send its stale living roster.
        // It cannot resurrect a corpse already confirmed under this uid.
        if (s_slotCorpse[slot] && s_snapHave[slot] && s_snap[slot].uid == n.uid && !n.dead && n.health >= 0)
            continue;
        s_snap[slot] = n;
        s_snapHave[slot] = true;
    }
    s_snapMs = zm_game_time_ms();
}

static int zm_room_monsters(const Entity** out, unsigned short* uids, bool includePuppetZombie)
{
    int n = 0;
    for (int i = 0; i < ZM_FIRST_SURVIVOR_SLOT; i++) {
        const Entity* e = &g_EnemiesList[i];
        if (zm_greenhouse_vine(e)) continue;
        if ((e->status_flags & ENTITY_STATUS_ACTIVE) == 0) continue;
        if (!includePuppetZombie && e == s_puppetZombie) continue;
        // Yawn's body segments go as the head's ZmYawnBody (ZombieYawn.cpp).
        if (zm_yawn_segment(e)) continue;
        uids[n] = zm_wire_uid(e);
        out[n++] = e;
    }
    return n;
}

// Once a frame: who owns this room now, and the hand-overs when that changes.
static void zm_room_update(void)
{
    int self = zm_net_self();
    int owner = zm_compute_owner();
    bool others = zm_others_here();
    static bool pairLeaseReported = false;
    bool waiting = owner > ZM_NET_DIRECTOR && zm_player_here(ZM_NET_DIRECTOR);
    if (waiting && !pairLeaseReported)
        dbg_printf("[control] director waits for p%d's paired playback in %d/%02X\n",
            owner, (int)g_stageId, (int)g_roomId);
    pairLeaseReported = waiting;
    if (owner != s_owner) {
        s_corpseAdoptFrames = 0;
        if (s_iOwn && owner != self) {
            // Someone outranks this copy here now (the director walked in, or
            // a lower-numbered survivor): hand it the monsters as they are.
            const Entity* list[30];
            unsigned short uids[30];
            int n = zm_room_monsters(list, uids, false);
            zm_net_send_enemies(list, uids, n, true);
            if (owner >= 0) s_corpseAdoptFrames = 60;
            for (int i = 0; i < 30; i++) {
                if (!s_snapHave[i] || !s_snap[i].dead) s_snapHave[i] = false;
            }
            s_puppetFrames = 0;
            dbg_printf("[zombie] player %d runs this room now\n", owner);
        } else if (!s_iOwn && owner == self) {
            // The owner left: the room's monsters live again, from here.
            for (int i = 0; i < ZM_FIRST_SURVIVOR_SLOT; i++) {
                Entity* e = &g_EnemiesList[i];
                if ((e->status_flags & ENTITY_STATUS_ACTIVE) == 0 || e == s_puppetZombie || zm_greenhouse_vine(e)) continue;
                if (i == s_grabSlot) continue;
                s_slotShown[i] = true;
                // Yawn goes on from the pose its owner last sent. Its body
                // segments' -1 health is how the original keeps them, not a
                // death; a fled snake's head flees again from where it is.
                if (zm_yawn_entity(e)) {
                    if (zm_yawn_head(e) && e->state != ZOMBIE_STATE_INIT) {
                        e->state = ZOMBIE_STATE_IDLE;
                        e->ignore_player_flag = 0;
                        e->action_behavior = 0;
                        e->action_state = 0;
                        e->hit_state = 0;
                    }
                    continue;
                }
                if (zm_monster_dead(e)) {
                    if (s_snapHave[i] && !s_snap[i].settled && !s_slotCorpse[i]) {
                        // Continue the owner's exact death phase and frame,
                        // with this copy's model/animation pointers intact.
                        zm_resume_death(e, s_snap[i]);
                        continue;
                    }
                    // Killed on the owner's copy: a puppet's state is always
                    // idle here, so its own AI would get it back up.
                    s_slotCorpse[i] = true;
                    e->status_flags |= 0x0E;
                    if (e->health >= 0) e->health = -1;
                    dbg_printf("[zombie] slot %d stays a corpse\n", i);
                    continue;
                }
                e->status_flags &= ~ZM_STATUS_NO_PAIR;
                if (s_snapHave[i] && s_snap[i].state == ZOMBIE_STATE_EATING && !s_snap[i].dead) {
                    zombie_mode_begin_feeding(e, true);
                } else if (e->state != ZOMBIE_STATE_DIE) {
                    e->state = ZOMBIE_STATE_IDLE;
                    e->ignore_player_flag = 0;
                    e->action_behavior = 0;
                    e->action_state = 0;
                    e->hit_state = 0;
                }
            }
            s_ownerFrames = 0;
            dbg_printf("[zombie] this room is ours again\n");
        }
        s_owner = owner;
        s_iOwn = owner == self;
    }
    s_shared = others || zm_spectated_here();
    if (s_iOwn) {
        s_ownerFrames++;
        if (s_shared) {
            // Not the director's-body puppet of a survivor's copy: with the
            // director away it stands hidden, nobody else should show it.
            const Entity* list[30];
            unsigned short uids[30];
            int n = zm_room_monsters(list, uids, false);
            zm_net_send_enemies(list, uids, n, false);
        }
    } else {
        s_puppetFrames++;
    }
    if (s_corpseAdoptFrames > 0 || (s_shared && ((s_iOwn ? s_ownerFrames : s_puppetFrames) % 30) == 0)) {
        // Refresh confirmed corpses throughout occupancy, not just during
        // the first handoff. A returning copy can finish loading later.
        if (s_corpseAdoptFrames > 0) s_corpseAdoptFrames--;
        const Entity* list[30];
        unsigned short uids[30];
        int count = 0;
        for (int i = 0; i < ZM_FIRST_SURVIVOR_SLOT; i++) {
            const Entity* e = &g_EnemiesList[i];
            if ((e->status_flags & ENTITY_STATUS_ACTIVE) == 0 || e == s_puppetZombie ||
                e->id >= NPC_ENTITIES_IDS || e->health >= 0 || zm_greenhouse_vine(e) || zm_yawn_entity(e)) continue;
            // Do not turn another copy's in-progress death into a frozen
            // corpse. Only its owner can first confirm the completed fall.
            if (!s_slotCorpse[i] && (!s_iOwn || !zm_monster_death_settled(e))) continue;
            list[count] = e;
            uids[count++] = zm_wire_uid(e);
        }
        if (count > 0) zm_net_send_enemies(list, uids, count, true);
    }
}

// ---------------------------------------------------------------------------
// The monsters' target
// ---------------------------------------------------------------------------

// update_entities, before a monster's update on the room's owner: put the
// nearest survivor in the room in the player entity for it (a remote one is
// swapped in and out again by zm_target_end).
static void zm_target_begin(Entity* e)
{
    s_targetIdx = -1;
    s_targetSwapped = false;
    if (s_gameRole == ZM_NET_OFF || !s_iOwn || zombie_mode_is_puppet(e)) return;

    long long best = 0x7FFFFFFFFFFFFFFFLL;
    int bestIdx = -1;
    const ZmNetPeerState* bestState = NULL;
    int self = zm_net_self();
    int slot = zm_slot_of(e);
    // A bite already playing on this local survivor cannot retarget a
    // different survivor when the director arrives or walks nearer.
    if (zm_survivor_role() && zm_is_zombie_id(e->id) &&
        e->state == ZOMBIE_STATE_ATTACK && g_playerEntity.animationId == 5) {
        s_targetIdx = self;
        return;
    }
    int pinned = -1;
    if (e->id == ENEMY_TYRANT_2 && slot >= 0 && slot < 30) {
        if ((e->state != 1 && e->state != 2) || (e->state == 1 && e->action_behavior <= 1)) {
            s_tyrantTarget[slot] = -1;
            s_tyrantReaction[slot] = 0;
        }
        pinned = s_tyrantTarget[slot];
    }
    long long ex = e->scaMatrixData.localMatrix.t[0];
    long long ez = e->scaMatrixData.localMatrix.t[2];
    if (zm_survivor_role() && g_playerEntity.health >= 0 && (pinned < 0 || pinned == self)) {
        long long dx = g_playerEntity.scaMatrixData.localMatrix.t[0] - ex;
        long long dz = g_playerEntity.scaMatrixData.localMatrix.t[2] - ez;
        best = dx * dx + dz * dz;
        bestIdx = self;
    }
    for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
        if (i == self || zm_net_char(i) < 0) continue;
        if (pinned >= 0 && i != pinned) continue;
        const ZmNetPeerState* p = zm_net_player(i);
        if (!zm_state_here(p) || (p->dead && pinned != i)) continue;
        long long dx = p->x - ex;
        long long dz = p->z - ez;
        long long d = dx * dx + dz * dz;
        if (d < best) {
            best = d;
            bestIdx = i;
            bestState = p;
        }
    }
    s_targetIdx = bestIdx;
    if (bestState == NULL) return;

    s_swapT[0] = g_playerEntity.scaMatrixData.localMatrix.t[0];
    s_swapT[1] = g_playerEntity.scaMatrixData.localMatrix.t[1];
    s_swapT[2] = g_playerEntity.scaMatrixData.localMatrix.t[2];
    s_swapPos = g_playerEntity.position;
    s_swapPosY = g_playerEntity.posY;
    s_swapHealth = g_playerEntity.health;
    s_swapAttacked = g_playerEntity.isBeingAttackedFlag;
    s_swapDirection = g_playerEntity.directionAngle;
    s_swapAnim = *(unsigned int*)&g_playerEntity.animationId;
    s_swapStatus = g_playerEntity.healthStatusFlags;
    s_swapAttacker = g_playerEntity.unk_b8;
    s_swapAttackAnim = g_playerEntity.attackAnim;
    s_swapFlags = g_playerEntity.flags;
    s_swapGrabX = g_playerEntity.unk_c6;
    s_swapGrabZ = g_playerEntity.unk_c8;
    g_playerEntity.scaMatrixData.localMatrix.t[0] = bestState->x;
    g_playerEntity.scaMatrixData.localMatrix.t[1] = bestState->y;
    g_playerEntity.scaMatrixData.localMatrix.t[2] = bestState->z;
    g_playerEntity.position.x = (short)bestState->x;
    g_playerEntity.position.y = (short)bestState->y;
    g_playerEntity.position.z = (short)bestState->z;
    g_playerEntity.posY = (unsigned short)bestState->y;
    g_playerEntity.health = bestState->health;
    bool reacting = bestState->attacked || (int)(s_hurtUntilMs[bestIdx] - zm_game_time_ms()) > 0;
    g_playerEntity.isBeingAttackedFlag = reacting ? 1 : 0;
    g_playerEntity.directionAngle = bestState->angle;
    // A free player, as far as the monster can tell: standing, under control.
    *(unsigned int*)&g_playerEntity.animationId = 0x00000001;
    s_probeHealth = g_playerEntity.health;
    s_probeAnim = *(unsigned int*)&g_playerEntity.animationId;
    s_probeDirection = g_playerEntity.directionAngle;
    s_probeStatus = g_playerEntity.healthStatusFlags;
    s_probeFlag = g_playerEntity.isBeingAttackedFlag;
    s_targetSwapped = true;
}

// After a monster's update with a remote survivor in the player entity: did it
// strike? The engine's attacks all write the player - health down, the hit
// flag (direction / reaction animation), the reaction behaviour 0x64-0x68,
// sometimes the facing or a poison bit. Whatever it wrote goes to that
// survivor's copy, which plays it as a hit of its own. Zombies are not here:
// their grab is handed over whole (zm_owner_intercept_attack).
static void zm_target_report_hit(const Entity* monster)
{
    if (!s_targetSwapped || s_targetIdx < 0 || zm_is_zombie_id(monster->id)) return;
    short damage = (short)(s_probeHealth - g_playerEntity.health);
    unsigned char flag = g_playerEntity.isBeingAttackedFlag;
    // The monsters only strike a clear flag, and set it when they do.
    bool flagged = flag != s_probeFlag;
    unsigned int anim = *(unsigned int*)&g_playerEntity.animationId;
    if (monster->id == ENEMY_TYRANT_2) {
        int slot = zm_slot_of(monster);
        unsigned char state = g_playerEntity.animationId;
        unsigned short reaction = (unsigned short)(state | (g_playerEntity.action_behavior << 8));
        bool special = (state == 6 || state == 7) && g_playerEntity.animFrameId == 0x0C;
        if (slot >= 0 && slot < 30 && special &&
            (damage > 0 || s_tyrantReaction[slot] != reaction)) {
            s_tyrantTarget[slot] = (signed char)s_targetIdx;
            s_tyrantReaction[slot] = reaction;
            zm_net_send_event_to(s_targetIdx, ZM_EV_TYRANT_REACT,
                (short)(g_stageId | (g_roomId << 8)), damage > 0 ? damage : 0,
                (short)reaction, (short)zm_wire_uid(monster), g_playerEntity.directionAngle,
                state == 7 ? (short)g_playerEntity.unk_c6 : (short)g_playerEntity.scaMatrixData.localMatrix.t[0],
                state == 7 ? (short)g_playerEntity.unk_c8 : (short)g_playerEntity.scaMatrixData.localMatrix.t[2],
                (short)g_playerEntity.attackAnim);
            dbg_printf("[tyrant] reaction %u victim %d damage %d uid %04X\n",
                reaction, s_targetIdx, damage, zm_wire_uid(monster));
        }
        if (special) return;
    }
    if (damage <= 0 && !flagged) return;
    short animId = (anim != s_probeAnim) ? (short)(anim & 0xFF) : -1;
    short behavior = (anim != s_probeAnim) ? (short)((anim >> 16) & 0xFF) : -1;
    short facing = (g_playerEntity.directionAngle != s_probeDirection) ? g_playerEntity.directionAngle : 0x7FFF;
    unsigned char gained = (unsigned char)(g_playerEntity.healthStatusFlags & ~s_probeStatus);
    zm_net_send_event_to(s_targetIdx, ZM_EV_PHURT, damage > 0 ? damage : 0, flag, animId, behavior,
                         facing, gained, monster->id, (short)(g_stageId | (g_roomId << 8)));
    s_hurtUntilMs[s_targetIdx] = zm_game_time_ms() + 700;
    dbg_printf("[zombie] monster id %02X hit survivor %d for %d (flag %d)\n",
               (unsigned)monster->id, s_targetIdx, (int)damage, (int)flag);
}

static void zm_target_end(void)
{
    if (!s_targetSwapped) return;
    g_playerEntity.directionAngle = s_swapDirection;
    *(unsigned int*)&g_playerEntity.animationId = s_swapAnim;
    g_playerEntity.healthStatusFlags = s_swapStatus;
    g_playerEntity.unk_b8 = s_swapAttacker;
    g_playerEntity.attackAnim = s_swapAttackAnim;
    g_playerEntity.flags = s_swapFlags;
    g_playerEntity.unk_c6 = s_swapGrabX;
    g_playerEntity.unk_c8 = s_swapGrabZ;
    g_playerEntity.scaMatrixData.localMatrix.t[0] = s_swapT[0];
    g_playerEntity.scaMatrixData.localMatrix.t[1] = s_swapT[1];
    g_playerEntity.scaMatrixData.localMatrix.t[2] = s_swapT[2];
    g_playerEntity.position = s_swapPos;
    g_playerEntity.posY = s_swapPosY;
    g_playerEntity.health = s_swapHealth;
    g_playerEntity.isBeingAttackedFlag = s_swapAttacked;
    s_targetSwapped = false;
}

// ---------------------------------------------------------------------------
// A strike on every survivor in reach (Yawn's thrash and slam, Yawn.cpp)
//
// The engine's monsters hit only g_playerEntity - one survivor, the target
// swapped in for the update. A strike along a whole body hits all of them: it
// is kept from the monster's update and dealt after it (zm_area_resolve,
// once the swap has ended), to this copy's own survivor as a PHURT would be
// played here and to each other copy's as a PHURT - each with the reaction
// a Yawn bite gives, facing or not.
// ---------------------------------------------------------------------------
static void zm_take_hurt(const short* a);

int zm_room_survivors(int (*xz)[2], int* who, int max)
{
    int n = 0;
    // This copy's own survivor - the real one, under a target swap too.
    bool local = s_gameRole == ZM_NET_OFF ? zm_survivor_present() : zm_survivor_role();
    short health = s_targetSwapped ? s_swapHealth : g_playerEntity.health;
    if (local && health >= 0 && !zm_spec_away() && n < max) {
        xz[n][0] = s_targetSwapped ? s_swapT[0] : g_playerEntity.scaMatrixData.localMatrix.t[0];
        xz[n][1] = s_targetSwapped ? s_swapT[2] : g_playerEntity.scaMatrixData.localMatrix.t[2];
        who[n++] = s_gameRole == ZM_NET_OFF ? -1 : zm_net_self();
    }
    if (s_gameRole == ZM_NET_OFF) return n;
    for (int i = 0; i < ZM_NET_MAX_PLAYERS && n < max; i++) {
        if (i == zm_net_self() || zm_net_char(i) < 0) continue;
        const ZmNetPeerState* p = zm_net_player(i);
        if (!zm_state_here(p) || p->dead || p->spectating || p->transitioning) continue;
        xz[n][0] = p->x;
        xz[n][1] = p->z;
        who[n++] = i;
    }
    return n;
}

struct ZmAreaHit {
    bool          have;
    int           count, radius;
    short         damage, angle;
    unsigned char id;
    int           pts[16][3];
};
static ZmAreaHit s_areaHit;

void zombie_mode_yawn_area_hit(const int (*points)[3], int count, int radius, int damage)
{
    if (!s_zombieModeArmed || ENTITY == NULL || count <= 0) return;
    if (count > 16) count = 16;
    s_areaHit.have = true;
    s_areaHit.count = count;
    s_areaHit.radius = radius;
    s_areaHit.damage = (short)damage;
    s_areaHit.angle = ENTITY->angle;
    s_areaHit.id = ENTITY->id;
    memcpy(s_areaHit.pts, points, sizeof(int) * 3 * count);
}

static void zm_area_resolve(void)
{
    if (!s_areaHit.have) return;
    s_areaHit.have = false;
    int xz[ZM_NET_MAX_PLAYERS][2], who[ZM_NET_MAX_PLAYERS];
    int n = zm_room_survivors(xz, who, ZM_NET_MAX_PLAYERS);
    unsigned int now = zm_game_time_ms();
    int r = s_areaHit.radius;
    for (int k = 0; k < n; k++) {
        bool hit = false;
        for (int i = 0; i < s_areaHit.count && !hit; i++) {
            int dx = xz[k][0] - s_areaHit.pts[i][0], dz = xz[k][1] - s_areaHit.pts[i][2];
            // Each axis bounded before squaring: strictly 32-bit arithmetic.
            if (dx < -r || dx > r || dz < -r || dz > r) continue;
            hit = dx * dx + dz * dz <= r * r;
        }
        if (!hit) continue;
        bool local = who[k] < 0 || who[k] == zm_net_self();
        short facingAngle = local ? (short)g_playerEntity.directionAngle : zm_net_player(who[k])->angle;
        short facing = (short)((((facingAngle - s_areaHit.angle) + 0x400) & 0xFFF) < 0x800 ? 1 : 0);
        short a[8] = { s_areaHit.damage, (short)(facing + 1), 2, (short)(0x66 + facing), 0x7FFF, 0,
                       (short)s_areaHit.id, (short)(g_stageId | (g_roomId << 8)) };
        int hurtSlot = who[k] < 0 ? 0 : who[k];
        if ((int)(s_hurtUntilMs[hurtSlot] - now) > 0) continue;
        if (local) {
            zm_take_hurt(a);
        } else {
            zm_net_send_event_to(who[k], ZM_EV_PHURT, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
        }
        s_hurtUntilMs[hurtSlot] = now + 700;
        dbg_printf("[yawn] area strike on survivor %d for %d\n", who[k], (int)s_areaHit.damage);
    }
}

// Before a monster's update on the copy that runs it, with its target in the
// player entity: a survivor of another copy swapped in, a survivor's own
// player, or single player's AI survivor when it is in the room.
static void zm_hit_probe_begin(const Entity* e)
{
    s_hitProbe = false;
    if (!zm_econ_on() && s_gameRole != ZM_NET_SURVIVOR) return;
    int slot = (int)(e - g_EnemiesList);
    if (slot < 0 || slot >= ZM_FIRST_SURVIVOR_SLOT || e->id >= NPC_ENTITIES_IDS || zombie_mode_is_puppet(e)) return;
    if (zm_yawn_entity(e)) return;          // the room's boss earns the director nothing
    bool target;
    if (s_gameRole == ZM_NET_OFF) target = zm_survivor_present();
    else if (!s_iOwn) target = false;
    else if (s_targetSwapped) target = s_targetIdx >= 0;
    else target = s_gameRole == ZM_NET_SURVIVOR;     // the director's own player is no survivor
    if (!target || g_playerEntity.health < 0) return;
    s_hitProbe = true;
    s_hitProbeHealth = g_playerEntity.health;
    s_hitProbeFlag = g_playerEntity.isBeingAttackedFlag;
}

// One of the director's monsters other than its body hit a survivor: +25 for
// the director, at most once per ZM_CREDIT_GAP_MS a monster (a grab's bites
// are one hit). A survivor's copy running the room sends it (ZM_EV_CREDIT).
static void zm_monster_credit(int slot)
{
    if (slot < 0 || slot >= 30) return;
    unsigned int now = zm_game_time_ms();
    if (s_creditMs[slot] != 0 && now - s_creditMs[slot] < ZM_CREDIT_GAP_MS) return;
    s_creditMs[slot] = now;
    if (zm_econ_on()) {
        zm_econ_monster_hit();
    } else if (s_gameRole == ZM_NET_SURVIVOR) {
        zm_net_send_event_to(0, ZM_EV_CREDIT, (short)slot, (short)(g_stageId | (g_roomId << 8)),
                             0, 0, 0, 0, 0, 0);
    }
}

// After it, before the swap ends: did it strike (health down, or the hit flag
// newly set - zm_target_report_hit's test)? The possessed body earns the
// director its own bonus.
static void zm_hit_probe_end(const Entity* e)
{
    if (!s_hitProbe) return;
    s_hitProbe = false;
    bool hurt = g_playerEntity.health < s_hitProbeHealth;
    bool flagged = s_hitProbeFlag == 0 && g_playerEntity.isBeingAttackedFlag != 0;
    if (!hurt && !flagged) return;
    if (e == g_zombieModeEntity) zm_econ_hit();
    else zm_monster_credit((int)(e - g_EnemiesList));
}

bool zombie_mode_before_entity_update(Entity* e)
{
    if (!s_zombieModeArmed) return true;
    if (zm_greenhouse_vine(e)) {
        // Local vines still need the victim's hit/hold animation set from
        // their EMD (em110f for Jill). Do not replace it during a reaction.
        // This branch bypasses the normal monster targeting setup below.
        if (g_playerEntity.health >= 0 && g_playerEntity.isBeingAttackedFlag == 0 &&
            g_playerEntity.animationId == 1 && !zombie_mode_damage_anim_select(e->id))
            return false;
        // Event 1 kills its local vines first. Its persistent flag is relayed
        // by STORY, so every other copy in this room runs the same death path.
        // Room re-entry (including reconnect) refuses spawns once it is set.
        if (e->state != 0 && Flg_ck((int)g_ScenarioFlags2, 0xA6) != 0)
            e->behavior_flags |= 0x40;
        return true;
    }
    if (zm_entry_idle(e)) return false;
    // A zombie about to run its init (state 0) whose animation data is not in
    // the room's model memory never got its model loaded: zombie_init's
    // Joint_move would fault on it. Left out (and logged) instead.
    if (e->state == 0 && zm_is_zombie_id(e->id) && e != g_zombieModeEntity && !zombie_mode_is_puppet(e)) {
        unsigned int lo = (unsigned int)g_DataBuffer, hi = lo + (unsigned int)sizeof(g_DataBuffer);
        if (e->animHeader < lo || e->animHeader >= hi || e->animBase < lo || e->animBase >= hi ||
            e->jointsStructs == NULL) {
            int slot = (int)(e - g_EnemiesList);
            dbg_printf("[zombie] slot %d (id %02X, behaviour %02X) has no model in stage %d room %02X: "
                       "left out\n", slot, (unsigned)e->id, (unsigned)e->behavior_flags,
                       (int)g_stageId, (int)g_roomId);
            e->status_flags = 0;
            g_enemy_count--;
            return false;
        }
    }
    zm_stun_before_update(e);
    zm_target_begin(e);
    // A remote target never changes the local survivor's latched pointers.
    // Once a reaction starts, subsequent enemies cannot replace its set.
    if (!s_targetSwapped && !zombie_mode_is_puppet(e) && e->id < NPC_ENTITIES_IDS &&
        g_playerEntity.health >= 0 && g_playerEntity.isBeingAttackedFlag == 0 &&
        g_playerEntity.animationId == 1)
        zombie_mode_damage_anim_select(e->id);
    zm_hit_probe_begin(e);
    return true;
}

// ---------------------------------------------------------------------------
// A puppet monster (any copy but the room's owner)
// ---------------------------------------------------------------------------

// ZM_EV_GRAB: take it if the survivor is free to be grabbed (this monster is
// here and drawn, the survivor alive and not already held), else refuse at
// once. Taking it is exactly how zombie_chase_player enters the attack - a
// word store of 5 at +0x84 - with the laying-down bit for a grab from the
// floor, so zombie_attack picks its laying rows itself.
static void zm_puppet_begin_grab(int ownerSlot, bool prone, int owner)
{
    int slot = zm_local_slot(ownerSlot);
    if (slot < 0 || slot >= ZM_FIRST_SURVIVOR_SLOT) {
        zm_net_send_event_to(owner, ZM_EV_GRAB_END, 0, 0, 0, 1, (short)ownerSlot, 0, 0, 0);
        return;
    }
    Entity* e = &g_EnemiesList[slot];
    bool free = s_grabSlot < 0 && (e->status_flags & ENTITY_STATUS_ACTIVE) != 0 &&
                s_slotShown[slot] && g_playerEntity.health >= 0 &&
                g_playerEntity.isBeingAttackedFlag == 0 && g_playerEntity.animationId != 5 &&
                zm_is_zombie_id(e->id) && zombie_mode_damage_anim_select(e->id);
    if (!free) {
        zm_net_send_event_to(owner, ZM_EV_GRAB_END, (short)e->scaMatrixData.localMatrix.t[0],
                             (short)e->scaMatrixData.localMatrix.t[2], e->angle, 1,
                             (short)ownerSlot, 0, 0, 0);
        dbg_printf("[zombie] refused a grab by slot %d\n", slot);
        return;
    }
    e->behavior_flags = (e->behavior_flags & ~ZOMBIE_FLAG_LAYING_DOWN) |
                        (prone ? ZOMBIE_FLAG_LAYING_DOWN : 0);
    e->state = ZOMBIE_STATE_ATTACK;
    e->ignore_player_flag = 0;
    e->action_behavior = 0;
    e->action_state = 0;
    e->hit_state = 0;
    e->status_flags &= ~ZM_STATUS_NO_PAIR;
    s_grabSlot = slot;
    s_grabFrames = 0;
    s_grabOwner = owner;
    s_grabOwnerSlot = ownerSlot;
    dbg_printf("[zombie] slot %d grabs (for player %d)\n", slot, owner);
}

// One frame of that grab: the engine's zombie states, as zombie_update runs
// them (zombie_attack, then the withdrawal through zombie_damaged /
// long_push_back), with zombie_update's collision tail. Over when the zombie
// is back in state 1 - which every way out of zombie_attack ends in, the
// survivor's death included.
static void zm_puppet_grab_frame(Entity* e)
{
    int slot = zm_slot_of(e);
    s_grabFrames++;
    if ((g_message_flags & 0x0004) != 0) {
        if (e->state < 22 && zombie_states_table[e->state] != NULL) {
            ((void(*)())zombie_states_table[e->state])();
        }
        ENTITY = e;
        e->state_mirror = e->state;
        e->ignore_player_flag_mirror = e->ignore_player_flag;
        e->action_behavior_mirror = e->action_behavior;
        e->attack_behavior_mirror = e->action_state;
        if (e->internal_timer != 0) e->internal_timer--;
        if (e->state != ZOMBIE_STATE_ATTACK) {
            SetEntityScaHitData(e);
            ResolveEntityScaCollision((Entity*)&g_playerEntity, e);
            HandleEnemyPlayerCollisions();
            e->collisionFlags &= ~0x08;
            e->collisionFlags |= ZM_COLLISION_LIKE_PLAYER;
            check_room_collision((VECTOR*)&e->scaMatrixData.localMatrix.t,
                                 *(short*)(e->Sca_info + 10));
        }
    }
    zm_draw_shadow(e);
    s_slotShown[slot] = true;

    if (e->state == ZOMBIE_STATE_IDLE || e->state == ZOMBIE_STATE_EATING || s_grabFrames > ZM_PUPPET_GRAB_MAX) {
        s_grabSlot = -1;
        zm_net_send_event_to(s_grabOwner, ZM_EV_GRAB_END, (short)e->scaMatrixData.localMatrix.t[0],
                             (short)e->scaMatrixData.localMatrix.t[2], e->angle, 0,
                             (short)s_grabOwnerSlot, e->state == ZOMBIE_STATE_EATING ? 1 : 0, 0, 0);
        if (e->state != ZOMBIE_STATE_EATING) e->state = ZOMBIE_STATE_IDLE;
        e->hit_state = 0;
        s_slotHealth[slot] = e->health;     // not a hit
        dbg_printf("[zombie] slot %d grab over\n", slot);
    }
}

static void zm_puppet_awareness(Entity* e, bool dead)
{
    e->status_flags &= 0x1F;
    if (dead) {
        // Down: no longer a target, no longer in the way.
        e->status_flags |= 0x0E;
        return;
    }
    // Targetable and solid like any zombie: zombie_state_check's awareness
    // bits (apply_weapon_damage needs one shared with the aim).
    e->status_flags &= ~(ENTITY_STATUS_DEAD | ZM_STATUS_NO_PAIR);
    e->status_flags |= ENTITY_STATUS_ALIGNED;
    entity_check_alert_range(3000);
    entity_check_visual_range(4500);
    int dy = zm_target_height_delta(e);
    if (dy < -100 || dy > 100) {
        e->status_flags &= 0x1F;
        e->status_flags |= (dy < 0) ? ENTITY_STATUS_PLAYER_ABOVE : ENTITY_STATUS_PLAYER_BELOW;
    }
}

// Another survivor's stand-in: where its copy says it is, in its pose; hidden
// while it is in another room. Never a target or an obstacle here - its own
// copy is where it is shot, grabbed and pushed.
static void zm_standin_set_weapon(Entity* e, int who, unsigned char weapon)
{
    ZmStandIn& st = s_standIns[who];
    JointStruct* joint = &e->jointsStructs[ZM_WEAPON_JOINT];
    if (!st.baseSaved) {
        st.baseSlot = joint->anim_slot_ptr;
        st.baseObject = joint->anim_object;
        st.baseSaved = true;
    }
    if (weapon == st.weapon) return;
    st.weapon = weapon;
    int ch = zm_char_of_npc_id(e->id);
    bool borrowed = zm_char_borrowed(ch);
    int sheetBank = -1, sheetPage = -1;
    bool sheet = zm_weapon_borrows_sheet(ch, weapon) && zm_weapon_sheet(ch, weapon, &sheetBank, &sheetPage);
    if (weapon != 0 && borrowed && s_chrisSheetBank < 0 && !sheet) {
        weapon = 0;                     // no room for Chris's sheet here: empty-handed
    }
    if (weapon != 0) {
        char path[96];
        snprintf(path, sizeof(path), "%s%s", GAME_DATA_ROOT,
                 WeaponModelPath(borrowed ? ZM_CHAR_CHRIS : ch, weapon));
        SetSpriteBufferFlag();
        size_t size = LoadFile(path, st.emw, 32);
        if (size != (size_t)-1 && size >= 16 && size <= sizeof(st.emw)) {
            // LoadEquippedWeaponAnimation's mesh: the second word of the
            // file's tail table, rebased; textures onto this model's page.
            unsigned int* tail = (unsigned int*)(st.emw + (size & 0xFFFFFFFC) - 8);
            unsigned int mesh = (tail[1] & 0xFFFFFFFC) + (unsigned int)st.emw;
            if (sheet) {
                ProcessTmdTextures(2, (unsigned int*)mesh, sheetBank, sheetPage);
            } else if (borrowed) {
                ProcessTmdTextures(2, (unsigned int*)mesh, s_chrisSheetBank, s_chrisSheetPage);
            } else {
                ProcessTmdTextures(2, (unsigned int*)mesh, e->texBank, e->attacking_direction);
            }
            joint->anim_slot_ptr = (int)(mesh + 0xc);
            joint->anim_object = st.object;
            CreateAnimObject((int)&joint->anim_field, st.object);
            return;
        }
        dbg_printf("[zombie] stand-in %d: no weapon file %s\n", who, path);
    }
    // Empty-handed: the model's own hand back.
    joint->anim_slot_ptr = st.baseSlot;
    joint->anim_object = st.baseObject;
    CreateAnimObject((int)&joint->anim_field, (unsigned int*)st.baseObject);
}

static void zm_survivor_puppet_update(Entity* e, int slot)
{
    int who = slot - ZM_SURVIVOR_SLOT_BASE;
    const ZmNetPeerState* p = zm_net_player(who);
    bool here = zm_state_here(p) && !p->transitioning;
    s_slotShown[slot] = here;
    e->status_flags &= 0x1F;
    e->status_flags |= ZM_STATUS_NO_PAIR;
    if (!here) return;
    e->scaMatrixData.localMatrix.t[0] = p->x;
    e->scaMatrixData.localMatrix.t[1] = p->y;
    e->scaMatrixData.localMatrix.t[2] = p->z;
    e->angle = p->angle;
    e->health = p->health;
    zm_set_rollback(e);
    if (e->jointsStructs != NULL && e->jointCount > ZM_WEAPON_JOINT) {
        if (!s_standIns[who].baseSaved) {
            // Lay out the skeleton once, as every monster's init and the
            // player's LoadEntityModel do: ResetJointTransforms (0x0048bad0)
            // gives each joint its offset from its parent (the EMD's table at
            // animHeader + 8). The network pose sets only the rotations and
            // the root's translation - Joint_move does no more - so without
            // this every limb sits at the torso's origin.
            Entity* saved = ENTITY;
            ENTITY = e;
            ResetJointTransforms();
            ENTITY = saved;
        }
        zm_standin_set_weapon(e, who, p->dead ? 0 : p->weapon);
    }
    zm_apply_net_pose(e, p);
    zm_draw_shadow(e);

    // Solid to this copy's own survivor: the pair resolver pushes its second
    // argument out of the first, so only the local player moves - the stand-in
    // stays where its own copy put it. (The stand-in keeps ZM_STATUS_NO_PAIR
    // for everything else: monsters meet the real survivor on the room owner's
    // copy, where the owner's monsters are pushed off it.) The player's own
    // update, after the enemies', runs its wall collision on the result.
    if (s_gameRole == ZM_NET_SURVIVOR && !p->dead && g_playerEntity.health >= 0 &&
        g_playerEntity.isBeingAttackedFlag == 0 && e->Sca_info != 0 && e->pSca_hit_data != 0) {
        SetEntityScaHitData(e);
        unsigned char savedStatus = e->status_flags;
        e->status_flags &= ~ZM_STATUS_NO_PAIR;
        ResolveEntityScaCollision(e, (Entity*)&g_playerEntity);
        e->status_flags = savedStatus;
    }
}

// ---------------------------------------------------------------------------
// Bursting joints across the link
//
// A magnum head shot (enemy_hit_reaction_zombie, zombie_headshot) and the
// rocket's gore spurts burst a monster's joints: joint_setup_attack_effect
// (0x0048a070) clears the joint's draw bit and sets 0x28, which hands it to
// the trail pipeline (PathTrail.cpp) that throws the mesh apart. That is a
// joint flag, which a pose does not carry - so the copy that burst a joint
// tells the others which ones:
//   - a survivor shooting another copy's monster sends the mask with its hit
//     (ZM_EV_HIT a[3]) and the room's owner bursts the same joints;
//   - the owner sends every joint of its monsters that has burst
//     (ZM_EV_BURST) and the other copies burst them on their puppets.
// The head-explosion cue (Snd_em 6) goes with the burst rather than as a
// sound event, so the shooter does not hear it twice.
// ---------------------------------------------------------------------------
#define ZM_BURST_JOINTS 16
#define ZM_HEAD_JOINT   2

static unsigned short zm_burst_mask(const Entity* e)
{
    if (e->jointsStructs == NULL) return 0;
    int n = e->jointCount < ZM_BURST_JOINTS ? e->jointCount : ZM_BURST_JOINTS;
    unsigned short mask = 0;
    for (int i = 0; i < n; i++) {
        unsigned char f = e->jointsStructs[i].flags;
        if ((f & 0x01) == 0 && (f & 0x28) != 0) mask |= (unsigned short)(1u << i);
    }
    return mask;
}

static void zm_burst_apply(Entity* e, unsigned short mask)
{
    if (e->jointsStructs == NULL || mask == 0) return;
    unsigned short newly = (unsigned short)(mask & ~zm_burst_mask(e));
    if (newly == 0) return;
    Entity* saved = ENTITY;
    ENTITY = e;     // joint_setup_attack_effect and Snd_em read it
    int n = e->jointCount < ZM_BURST_JOINTS ? e->jointCount : ZM_BURST_JOINTS;
    for (int i = 0; i < n; i++) {
        if ((newly & (1u << i)) != 0) {
            joint_setup_attack_effect((int)&e->jointsStructs[i], 0x1E, 2, 3);
        }
    }
    if ((newly & (1u << ZM_HEAD_JOINT)) != 0 && zm_is_zombie_id(e->id)) {
        Snd_em(6);  // the head explosion
    }
    ENTITY = saved;
    dbg_printf("[zombie] slot %d: joints %04X burst\n", (int)(e - g_EnemiesList), (unsigned)newly);
}

// Every burst joint whole again: the director's body puppet coming into view
// alive (a burst it took while hidden would show it headless). A joint's
// effect only steps - and frees its trail slot - while the entity is drawn,
// so the slot is released here as FUN_0048a210's end does.
extern void FUN_00485aa0(void* trailObj);     // 0x00485aa0 PathTrail.cpp
static void zm_burst_clear(Entity* e)
{
    if (e->jointsStructs == NULL) return;
    int n = e->jointCount < ZM_BURST_JOINTS ? e->jointCount : ZM_BURST_JOINTS;
    for (int i = 0; i < n; i++) {
        unsigned char* j = (unsigned char*)&e->jointsStructs[i];
        if ((j[0] & 0x01) != 0 || (j[0] & 0x28) == 0) continue;
        if ((j[0] & 0x20) != 0) FUN_00485aa0(*(void**)(j + 0x18));
        j[0] = (unsigned char)((j[0] & ~0x28) | 0x01);
        j[2] = 0;
        dbg_printf("[zombie] slot %d: joint %d whole again\n", (int)(e - g_EnemiesList), i);
    }
}

// Owner's copy, once a frame in a shared room.
static void zm_burst_watch(void)
{
    if (s_gameRole == ZM_NET_OFF || !s_iOwn || !s_shared) return;
    for (int i = 0; i < ZM_FIRST_SURVIVOR_SLOT; i++) {
        const Entity* e = &g_EnemiesList[i];
        if ((e->status_flags & ENTITY_STATUS_ACTIVE) == 0 || e->id >= NPC_ENTITIES_IDS || zm_yawn_entity(e)) continue;
        unsigned short mask = zm_burst_mask(e);
        if ((mask & ~s_burstSent[i]) == 0) continue;
        s_burstSent[i] |= mask;
        zm_net_send_event(ZM_EV_BURST, (short)i, (short)mask, (short)(g_stageId | (g_roomId << 8)), 0);
    }
}

// Yawn's init on a copy that does not run it: its skeleton, and the twelve
// body segments it copies itself into (slots 1-12).
static void zm_yawn_init_here(Entity* e)
{
    if (e->state != ZOMBIE_STATE_INIT) return;
    Entity* saved = ENTITY;
    ENTITY = e;
    yawn_update();
    ENTITY = saved;
    s_slotHealth[zm_slot_of(e)] = e->health;
    dbg_printf("[yawn] p%d: body set up here (slots 0-%d)\n", zm_net_self(), ZM_YAWN_SLOTS - 1);
}

// A Yawn slot where another copy runs the room (ZombieYawn.cpp): the head
// gets its skeleton once, then the owner's pose each frame; a segment rides
// its joint. A shot that landed on either here (apply_weapon_damage set its
// state) is the owner's to count: the head's by the health it lost, a
// segment's by its hit state, which drains the owner's head as it would here.
static void zm_yawn_puppet_update(Entity* e, int slot)
{
    bool segment = zm_yawn_segment(e);
    if (!segment && e->state == ZOMBIE_STATE_INIT) {
        if ((g_message_flags & 0x0004) == 0) return;     // the monsters' turn is off
        zm_yawn_init_here(e);
    }
    if (e->state != ZOMBIE_STATE_IDLE) {
        short damage = segment ? 0 : (short)(s_slotHealth[slot] - e->health);
        if (!s_iOwn && s_owner >= 0 && (damage > 0 || (e->hit_state & 0x07) != 0)) {
            zm_net_send_event_to(s_owner, ZM_EV_HIT, damage, e->hit_state, (short)slot, 0, 0, 0, 0, 0);
            dbg_printf("[yawn] p%d: hit slot %d for %d (hit state %02X)\n", zm_net_self(), slot,
                       (int)damage, (unsigned)e->hit_state);
        }
        e->state = ZOMBIE_STATE_IDLE;
        e->ignore_player_flag = 0;
        e->action_behavior = 0;
        e->action_state = 0;
        e->hit_state = 0;
        if (segment) e->health = -1;
    }

    Entity* head = &g_EnemiesList[0];
    if (segment) {
        if (!s_slotShown[0] || head->state == ZOMBIE_STATE_INIT) {
            s_slotShown[slot] = false;
            e->status_flags &= 0x1F;
            return;
        }
        const JointStruct* j = (const JointStruct*)(uintptr_t)e->scd_target_ptr;
        if (j != NULL) {
            e->scaMatrixData.localMatrix.t[0] = j->world.t[0];
            e->scaMatrixData.localMatrix.t[1] = j->world.t[1];
            e->scaMatrixData.localMatrix.t[2] = j->world.t[2];
            zm_set_rollback(e);
        }
        zm_yawn_status_apply(e, slot);
        s_slotShown[slot] = true;
        SetEntityScaHitData(e);
        return;
    }

    const ZmNetEnemy* n = (!s_iOwn && s_snapHave[slot]) ? &s_snap[slot] : NULL;
    if (n == NULL || e->state == ZOMBIE_STATE_INIT || !zm_yawn_pose_apply(e, n->pose, s_owner)) {
        s_slotShown[slot] = false;
        e->status_flags &= 0x1F;
        return;
    }
    e->scaMatrixData.localMatrix.t[0] = n->pose.x;
    e->scaMatrixData.localMatrix.t[1] = n->pose.y;
    e->scaMatrixData.localMatrix.t[2] = n->pose.z;
    e->angle = n->pose.angle;
    e->health = n->health;
    s_slotHealth[slot] = e->health;
    zm_world_accept_net_state(e);
    zm_set_rollback(e);
    zm_yawn_status_apply(e, slot);
    zm_yawn_mark_scaled();
    s_slotShown[slot] = true;
    SetEntityScaHitData(e);
}

void zombie_mode_puppet_update(void)
{
    Entity* e = ENTITY;
    int slot = zm_slot_of(e);
    if (slot < 0) return;
    if (zm_yawn_entity(e)) {
        zm_yawn_puppet_update(e, slot);
        return;
    }

    if (zm_is_survivor_slot(slot) && e->id >= NPC_ENTITIES_IDS) {
        zm_survivor_puppet_update(e, slot);
        return;
    }
    if (s_slotCorpse[slot] && s_iOwn) {
        e->status_flags |= 0x0E;
        s_slotShown[slot] = true;
        return;     // lies where the last owner left it, never a collider
    }

    zm_init_puppet(e);

    if (slot == s_grabSlot) {
        zm_puppet_grab_frame(e);
        return;
    }

    // apply_weapon_damage lowered health and set state 2 or 3 since last
    // frame: the room's owner has the health and plays the reaction.
    if (e->state != ZOMBIE_STATE_IDLE) {
        short damage = (short)(s_slotHealth[slot] - e->health);
        if (damage > 0 && !s_iOwn && s_owner >= 0) {
            // With the joints the shot burst here (a head shot), for the owner
            // to burst too.
            zm_net_send_event_to(s_owner, ZM_EV_HIT, damage, e->hit_state, (short)zm_owner_slot(slot),
                                 (short)zm_burst_mask(e), 0, 0, 0, 0);
            dbg_printf("[zombie p%d] hit slot %d (owner's %d) for %d\n", zm_net_self(), slot,
                       zm_owner_slot(slot), (int)damage);
        }
        e->state = ZOMBIE_STATE_IDLE;
        e->ignore_player_flag = 0;
        e->action_behavior = 0;
        e->action_state = 0;
        e->hit_state = 0;
    }

    const ZmNetEnemy* n = (!s_iOwn && s_snapHave[slot]) ? &s_snap[slot] : NULL;
    if (n == NULL) {
        // A roster spawn without its owner's current snapshot has no known
        // collision state. It must not become an invisible wall on re-entry.
        e->status_flags |= ZM_STATUS_NO_PAIR;
        // Not (yet) in the owner's packet. Just after the owner came in, hold
        // still where it was rather than blink out.
        bool justShared = !s_iOwn && s_puppetFrames < 30 && e != s_puppetZombie;
        s_slotShown[slot] = justShared;
        if (!justShared) {
            // Hidden: not in the way, and not a target either - the aim bits
            // from when it was last shown would still draw the gun's shots
            // to wherever it was left (apply_weapon_damage picks by them).
            e->status_flags &= 0x1F;
            e->status_flags |= ZM_STATUS_NO_PAIR;
        }
        e->health = s_slotHealth[slot];
        return;
    }
    if (!s_slotShown[slot] && e == s_puppetZombie && !n->dead) zm_burst_clear(e);
    s_slotShown[slot] = true;
    e->scaMatrixData.localMatrix.t[0] = n->pose.x;
    e->scaMatrixData.localMatrix.t[1] = n->pose.y;
    e->scaMatrixData.localMatrix.t[2] = n->pose.z;
    e->angle = n->pose.angle;
    e->health = n->health;
    zm_world_accept_net_state(e);
    s_slotHealth[slot] = e->health;
    zm_set_rollback(e);
    zm_apply_pose(e, &n->pose);
    zm_monster_appearance_apply(e, n->appearance);
    zm_tyrant_draw_puppet(e);
    zm_puppet_awareness(e, n->dead);
    if (zombie_mode_feeding_target(e)) e->status_flags |= ENTITY_STATUS_PLAYER_BELOW;
    SetEntityScaHitData(e);
    zm_draw_shadow(e);
}

static void zm_puppet_play_sound(int slot, unsigned char id)
{
    if (slot < 0 || slot >= 30 || !s_slotShown[slot]) return;
    Entity* saved = ENTITY;
    ENTITY = &g_EnemiesList[slot];   // Snd_em pans from ENTITY and picks its sound group
    Snd_em(id);
    ENTITY = saved;
}

// ---------------------------------------------------------------------------
// The owner's copy: a zombie whose attack runs on its victim's copy
// ---------------------------------------------------------------------------

// update_entities, after a zombie of the owner's copy ran: it just decided to
// attack a survivor of another copy (zombie_chase_player's word store of 5).
// Hand that to the victim's copy instead.
//
// zombie_attack's first step (case 0) may already have run in the same
// update, and it sets status bit 0x08 (ENTITY_STATUS_DEAD - the attack's
// "busy" bit, cleared again by its withdrawal, which only the victim's copy
// plays). Left set here, the living zombie counted as dead on this copy: out
// of the room's monster count, skipped by the possession switch, refunded,
// and captured as dead when the room was left - so it vanished. Called before
// those tests (zombie_mode_after_entity_update).
static void zm_owner_intercept_attack(Entity* e)
{
    int slot = zm_slot_of(e);
    if (slot < 0 || e == g_zombieModeEntity || !s_iOwn || s_slotRemote[slot]) return;
    if (!s_targetSwapped || s_targetIdx < 0) return;      // its target is local
    // zombie_chase_player enters the attack with a WORD store of 5 at +0x84
    // (state and ignore_player_flag only), so action_state can still hold the
    // walk's value; the state alone says it, and this hook sees it the same
    // update, before zombie_attack has run.
    if (!zm_is_zombie_id(e->id) || e->state != ZOMBIE_STATE_ATTACK) return;
    zm_net_send_event_to(s_targetIdx, ZM_EV_GRAB, (e->behavior_flags & ZOMBIE_FLAG_LAYING_DOWN) ? 1 : 0,
                         (short)slot, 0, 0, 0, 0, 0, 0);
    e->status_flags &= ~ENTITY_STATUS_DEAD;
    s_slotRemote[slot] = true;
    s_slotRemoteVictim[slot] = s_targetIdx;
    s_slotRemoteSeen[slot] = false;
    s_slotRemoteMs[slot] = zm_game_time_ms();
    dbg_printf("[zombie] slot %d goes for survivor %d\n", slot, s_targetIdx);
}

static bool zm_shared_slot_busy(int slot)
{
    return s_slotRemote[slot];
}

static void zm_shared_after_update(Entity* e)
{
    int slot = zm_slot_of(e);
    // Other zombies already bending over the same corpse keep feeding too.
    if (zm_is_zombie_id(e->id) && e->health >= 0 && g_playerEntity.health < 0 &&
        e->state == ZOMBIE_STATE_IDLE && e->ignore_player_flag != 0 &&
        (e->action_behavior == 4 || e->action_behavior == 5)) {
        zombie_mode_begin_feeding(e);
    }
    if (e->id == ENEMY_TYRANT_2 && slot >= 0 && slot < 30 &&
        !s_targetSwapped && s_targetIdx >= 0 &&
        (g_playerEntity.animationId == 6 || g_playerEntity.animationId == 7) &&
        g_playerEntity.animFrameId == 0x0C) {
        s_tyrantTarget[slot] = (signed char)s_targetIdx;
    }
    if (s_gameRole != ZM_NET_OFF) {
        zm_target_report_hit(e);
        zm_target_end();
    }
    // Close for a local zombie grab before its paired animation advances.
    // Other attacks close on actual hurt, rather than a distant attack wind-up.
    if (zm_survivor_role() && (g_main_state_flags & MSF_MENU_ACTIVE) != 0 &&
        zm_is_zombie_id(e->id) && s_targetIdx == zm_net_self() &&
        e->state == ZOMBIE_STATE_ATTACK && !zombie_mode_is_puppet(e)) {
        s_forceMenuClose = true;
    }
}

static void zm_owner_release_remote(Entity* e, int slot)
{
    s_slotRemote[slot] = false;
    // zombie_attack_withdraw's AND 0xF5 (bits 1 and 3): the withdrawal ran on
    // the victim's copy, not here.
    e->status_flags &= 0xF5;
    e->state = ZOMBIE_STATE_IDLE;
    e->ignore_player_flag = 0;
    e->action_behavior = 0;
    e->action_state = 0;
    e->hit_state = 0;
    zm_set_rollback(e);
}

// update_entities, instead of its own update, while its attack runs remotely:
// shown as the victim's copy plays it.
void zombie_mode_remote_grabbed_update(void)
{
    Entity* e = ENTITY;
    int slot = zm_slot_of(e);
    if (slot < 0) return;
    const ZmNetPeerState* p = zm_net_player(s_slotRemoteVictim[slot]);
    unsigned int now = zm_game_time_ms();
    if (p != NULL && p->hasZombie && p->zombieSlot == slot && now - p->receivedMs < ZM_REMOTE_GRAB_GAP_MS) {
        if (!s_slotRemoteSeen[slot]) zm_monster_credit(slot);     // the victim's copy took the grab
        s_slotRemoteSeen[slot] = true;
        s_slotRemoteMs[slot] = now;
        e->scaMatrixData.localMatrix.t[0] = p->zombie.x;
        e->scaMatrixData.localMatrix.t[1] = p->zombie.y;
        e->scaMatrixData.localMatrix.t[2] = p->zombie.z;
        e->angle = p->zombie.angle;
        zm_set_rollback(e);
        zm_apply_pose(e, &p->zombie);
    } else if (now - s_slotRemoteMs[slot] >
               (s_slotRemoteSeen[slot] ? ZM_REMOTE_GRAB_GAP_MS : ZM_REMOTE_GRAB_WAIT_MS)) {
        dbg_printf("[zombie] slot %d's remote attack went quiet\n", slot);
        zm_owner_release_remote(e, slot);
    }
    zm_draw_shadow(e);
}

// ---------------------------------------------------------------------------
// Events, routed once a frame
// ---------------------------------------------------------------------------

// ZM_EV_HIT on the room's owner: a shot from another copy's survivor.
static void zm_owner_take_hit(const short* a)
{
    int slot = (a[2] >= 0 && a[2] < 30) ? a[2] : -1;
    if (slot < 0 || !s_iOwn) {
        dbg_printf("[zombie] hit on slot %d ignored (%s)\n", (int)a[2], s_iOwn ? "bad slot" : "not the owner");
        return;
    }
    Entity* e = &g_EnemiesList[slot];
    Entity* z = g_zombieModeEntity;
    if ((e->status_flags & ENTITY_STATUS_ACTIVE) == 0) {
        dbg_printf("[zombie] hit on slot %d ignored (empty slot)\n", slot);
        return;
    }
    // The joints the shot burst on the shooter's copy (a magnum head shot):
    // here too, before the reaction - zombie_die reads the head joint.
    zm_burst_apply(e, (unsigned short)a[3]);
    if (e == z) {
        if (e->state == ZOMBIE_STATE_DIE || s_riding) return;
        if (s_remoteGrab) {                  // mid-grab: the damage still counts
            e->health = (short)(e->health - a[0]);
            return;
        }
        if (s_biteVictim != NULL) zm_end_bite(e);
    }
    if (s_slotRemote[slot]) {
        e->health = (short)(e->health - a[0]);
        dbg_printf("[zombie] slot %d shot mid-bite for %d, health %d\n", slot, (int)a[0], (int)e->health);
        return;
    }
    // apply_weapon_damage's tail (0x0043c020): the reaction is the monster's
    // own state machine's.
    bool feeding = zombie_mode_feeding_target(e);
    e->health = (short)(e->health - a[0]);
    e->hit_state = (unsigned char)a[1];
    e->ignore_player_flag = 0;
    e->action_behavior = 0;
    e->action_state = 0;
    e->state = (e->health >= 0) ? ZOMBIE_STATE_DAMAGED : ZOMBIE_STATE_DIE;
    if (feeding && e->health >= 0) zombie_mode_stand_feeding(e);
    dbg_printf("[zombie] slot %d shot for %d, health %d\n", slot, (int)a[0], (int)e->health);
}

// ZM_EV_PHURT on the victim: a monster of the room's owner hit this copy's
// survivor. Played the way the monster's own write would have been: health,
// a status bit, and - if the survivor is free to react - the hit flag and
// reaction behaviour the player's state machine picks up next frame
// (player_state_01_control -> state 2, player_hit_react_common). A death
// comes from the health alone, as in single player.
static short s_hurtPending[8];
static bool  s_hurtPendingHave = false;

static void zm_take_hurt(const short* a)
{
    if (a[7] != (short)(g_stageId | (g_roomId << 8)) || g_playerEntity.health < 0) return;
    if ((g_main_state_flags & MSF_MENU_ACTIVE) != 0) {
        // Out of the menu first, as for a grab.
        memcpy(s_hurtPending, a, sizeof(s_hurtPending));
        s_hurtPendingHave = true;
        s_forceMenuClose = true;
        return;
    }
    g_playerEntity.health = (short)(g_playerEntity.health - a[0]);
    g_playerEntity.healthStatusFlags |= (unsigned char)a[5];
    // PHURT carries ordinary damage, never a paired death pose. A fatal hit
    // during another reaction must reach control's death check immediately.
    if (g_playerEntity.health < 0 && s_grabSlot < 0) {
        g_playerEntity.animationId = 1;
        g_playerEntity.animFrameId = 0;
        g_playerEntity.action_behavior = 0;
        g_playerEntity.action_state = 0;
        g_playerEntity.flags &= (unsigned char)~6;
    }
    unsigned char flag = (unsigned char)a[1];
    if (g_playerEntity.isBeingAttackedFlag == 0 && (flag & 0x3F) != 0 && g_playerEntity.health >= 0) {
        g_playerEntity.isBeingAttackedFlag = flag;
        short behavior = a[3];
        g_playerEntity.action_behavior = (behavior >= 0x64 && behavior <= 0x68) ? (unsigned char)behavior : 0x64;
        if (!zombie_mode_damage_anim_select((unsigned char)a[6]))
            g_playerEntity.action_behavior = 0x64; // ordinary player set, never a stale creature set
        if (a[4] != 0x7FFF) g_playerEntity.directionAngle = a[4];
        if (a[2] == 2) {
            g_playerEntity.animationId = 2;
            g_playerEntity.animFrameId = 0;
            g_playerEntity.action_state = 0;
        }
    }
    dbg_printf("[zombie] hurt by monster id %02X for %d, health %d\n",
               (unsigned)a[6], (int)a[0], (int)g_playerEntity.health);
}

// Resolve the attacker's stable uid, never a pointer from another process.
// Reliable events can arrive before the first ENEMIES snapshot creates it.
static bool zm_take_tyrant_reaction(const short* a)
{
    if (a[0] != (short)(g_stageId | (g_roomId << 8)) || g_playerEntity.health < 0) return true;
    unsigned char state = (unsigned char)a[2];
    unsigned char behavior = (unsigned char)((unsigned short)a[2] >> 8);
    if ((state != 6 && state != 7) || behavior > 2 || a[1] < 0) return true;
    if ((g_main_state_flags & MSF_MENU_ACTIVE) != 0) {
        s_forceMenuClose = true;
        return false;
    }
    unsigned short uid = (unsigned short)a[3];
    Entity* attacker = NULL;
    if (uid == ZM_WORLD_UID_BODY) attacker = s_puppetZombie;
    else {
        int slot = uid < 16 ? uid : zm_world_slot_of_uid(uid);
        if (slot >= 0 && slot < ZM_FIRST_SURVIVOR_SLOT) attacker = &g_EnemiesList[slot];
    }
    if (attacker == NULL || attacker->id != ENEMY_TYRANT_2 ||
        (attacker->status_flags & ENTITY_STATUS_ACTIVE) == 0) return false;
    if (!zombie_mode_damage_anim_select(attacker->id)) return false;
    g_playerEntity.health = (short)(g_playerEntity.health - a[1]);
    g_playerEntity.unk_b8 = (unsigned int)(uintptr_t)attacker;
    g_playerEntity.directionAngle = a[4];
    g_playerEntity.attackAnim = (unsigned char)a[7];
    if (g_playerEntity.animationId != state || g_playerEntity.animFrameId != 0x0C ||
        g_playerEntity.action_behavior != behavior) {
        g_playerEntity.animationId = state;
        g_playerEntity.animFrameId = 0x0C;
        g_playerEntity.action_behavior = behavior;
        g_playerEntity.action_state = 0;
        if (state == 7) {
            g_playerEntity.unk_c6 = (unsigned short)a[5];
            g_playerEntity.unk_c8 = (unsigned short)a[6];
            s_tyrantVictimSlot = zm_slot_of(attacker);
            s_tyrantVictimX = attacker->scaMatrixData.localMatrix.t[0];
            s_tyrantVictimZ = attacker->scaMatrixData.localMatrix.t[2];
        }
    }
    dbg_printf("[tyrant] received reaction %u damage %d uid %04X health %d\n",
        (unsigned short)a[2], a[1], uid, g_playerEntity.health);
    return true;
}

static void zm_tyrant_victim_frame(void)
{
    if (s_tyrantPendingHave) {
        if (zm_take_tyrant_reaction(s_tyrantPending) || zm_game_time_ms() - s_tyrantPendingMs > 2000)
            s_tyrantPendingHave = false;
    }
    if (s_iOwn) s_tyrantVictimSlot = -1; // the actual engine supplies its root motion now
    if (s_tyrantVictimSlot < 0) return;
    Entity* attacker = &g_EnemiesList[s_tyrantVictimSlot];
    if (g_playerEntity.animationId != 7 || g_playerEntity.animFrameId != 0x0C ||
        g_playerEntity.health < 0 || attacker->id != ENEMY_TYRANT_2 ||
        (attacker->status_flags & ENTITY_STATUS_ACTIVE) == 0) {
        s_tyrantVictimSlot = -1;
        return;
    }
    int x = attacker->scaMatrixData.localMatrix.t[0];
    int z = attacker->scaMatrixData.localMatrix.t[2];
    g_playerEntity.unk_c6 = (unsigned short)(g_playerEntity.unk_c6 + x - s_tyrantVictimX);
    g_playerEntity.unk_c8 = (unsigned short)(g_playerEntity.unk_c8 + z - s_tyrantVictimZ);
    s_tyrantVictimX = x;
    s_tyrantVictimZ = z;
}

static void zm_route_events(void)
{
    int kind, src;
    short a[8];
    while (zm_net_take_event(&kind, a, &src)) {
        switch (kind) {
        case ZM_EV_REINFORCE:
            zm_reinforce_take(a, src);
            break;
        case ZM_EV_TYRANT_TRAIL:
            if (!s_iOwn && src == s_owner) zm_tyrant_take_trail(a);
            break;
        case ZM_EV_TYRANT_REACT:
            if (zm_survivor_role() && src == s_owner) {
                zm_revive_cancel();
                if (!zm_take_tyrant_reaction(a)) {
                    if (s_tyrantPendingHave && s_tyrantPending[0] == a[0] && s_tyrantPending[3] == a[3])
                        a[1] = (short)(a[1] + s_tyrantPending[1]);
                    memcpy(s_tyrantPending, a, sizeof(s_tyrantPending));
                    s_tyrantPendingHave = true;
                    s_tyrantPendingMs = zm_game_time_ms();
                }
            }
            break;
        case ZM_EV_FX_BASIS: {
            if (src < 0 || src >= ZM_NET_MAX_PLAYERS) break;
            MATRIX& m = s_fxIncomingParent[src];
            m = g_identityMatrixData;
            for (int i = 0; i < 8; i++) m.m[i / 3][i % 3] = a[i];
            s_fxIncomingFrame[src] = false;
            break;
        }
        case ZM_EV_FX_ORIGIN: {
            if (src < 0 || src >= ZM_NET_MAX_PLAYERS) break;
            MATRIX& m = s_fxIncomingParent[src];
            m.m[2][2] = a[0];
            m.t[0] = (unsigned short)a[1]; m.t[1] = a[2]; m.t[2] = (unsigned short)a[3];
            s_fxIncomingLocal[src].x = a[4]; s_fxIncomingLocal[src].y = a[5]; s_fxIncomingLocal[src].z = a[6];
            s_fxIncomingFloor[src] = (unsigned short)a[7];
            s_fxIncomingFrame[src] = true;
            break;
        }
        case ZM_EV_FX:
            zm_replay_effect(a, src);
            break;
        case ZM_EV_JOINT_TINT:
            zm_take_joint_tint(a);
            break;
        case ZM_EV_PSND:
            zm_replay_player_sound(a, src);
            break;
        case ZM_EV_PHURT:
            zm_revive_cancel();
            if (zm_survivor_role()) zm_take_hurt(a);
            break;
        case ZM_EV_HIT:
            zm_owner_take_hit(a);
            break;
        case ZM_EV_FEED:
            zm_world_apply_feeding(a);
            break;
        case ZM_EV_GRAB_END: {
            int slot = (a[4] >= 0 && a[4] < 30) ? a[4] : -1;
            if (slot < 0) break;
            Entity* e = &g_EnemiesList[slot];
            if (e == g_zombieModeEntity) {
                zm_remote_grab_over(e, a);
            } else if (s_slotRemote[slot]) {
                zm_owner_release_remote(e, slot);
                if (a[5] != 0) zombie_mode_begin_feeding(e);
            }
            break;
        }
        case ZM_EV_SOUND:
            if (a[2] == (short)(g_stageId | (g_roomId << 8)) && !s_iOwn && src == s_owner) {
                zm_puppet_play_sound(zm_local_slot(a[1]), (unsigned char)a[0]);
            }
            break;
        case ZM_EV_BURST:
            if (a[2] == (short)(g_stageId | (g_roomId << 8)) && !s_iOwn && src == s_owner) {
                int slot = zm_local_slot(a[0]);
                if (slot >= 0 && slot < ZM_FIRST_SURVIVOR_SLOT &&
                    (g_EnemiesList[slot].status_flags & ENTITY_STATUS_ACTIVE) != 0) {
                    zm_burst_apply(&g_EnemiesList[slot], (unsigned short)a[1]);
                }
            }
            break;
        case ZM_EV_STATS:
            zm_stats_take(a, src);
            break;
        case ZM_EV_REVIVE:
            zm_revive_take(a, src);
            break;
        case ZM_EV_DROP:
        case ZM_EV_DROP_TAKE:
            zm_drops_take_event(kind, a, src);
            break;
        case ZM_EV_CLOCK:
            s_clockLeftMs = (unsigned int)(unsigned short)a[0] * 1000u;
            s_clockRemoteHeld = a[1] != 0;
            s_clockRunMs = (unsigned int)(unsigned short)a[2] * 1000u;
            s_clockAtMs = zm_game_time_ms();
            s_clockHave = true;
            break;
        case ZM_EV_HEAL:
            zm_perk_take_heal(a);
            break;
        case ZM_EV_STUN:
            zm_stun_take(a, src);
            break;
        case ZM_EV_PUSH_OFF:
            if (s_iOwn && a[2] == (short)(g_stageId | (g_roomId << 8)))
                zm_push_off_at((unsigned short)a[0], a[3], (unsigned short)a[1]);
            break;
        case ZM_EV_STORY:
            zm_world_story_apply(a);
            break;
        case ZM_EV_ROOMSYNC:
            zm_roomsync_take(a, src);
            break;
        case ZM_EV_VOICE:
            zm_voice_take(a, src);
            break;
        case ZM_EV_CREDIT:
            zm_econ_monster_hit();
            break;
        case ZM_EV_BOX:
            zm_box_take(a, src);
            break;
        case ZM_EV_TRAP:
            if (zm_trap_take(a) && s_gameRole == ZM_NET_SURVIVOR) play_sfx(2, 0x14, 0);
            break;
        case ZM_EV_GRAB: {
            zm_revive_cancel();
            int slot = a[1];
            if (!zm_survivor_role() || slot < 0 || slot >= 30) break;
            if ((g_main_state_flags & MSF_MENU_ACTIVE) != 0) {
                // The world does not stop for a menu: pull the survivor out,
                // the grab follows once play resumes.
                s_grabPendingSlot = slot;
                s_grabPendingProne = a[0] != 0;
                s_grabPendingOwner = src;
                s_forceMenuClose = true;
                dbg_printf("[zombie] grabbed in the menu: closing it\n");
            } else {
                zm_puppet_begin_grab(slot, a[0] != 0, src);
            }
            break;
        }
        }
    }
    if (s_hurtPendingHave && (g_main_state_flags & MSF_MENU_ACTIVE) == 0 &&
        (g_message_flags & 0x0004) != 0) {
        s_hurtPendingHave = false;
        zm_take_hurt(s_hurtPending);
    }
    if (s_grabPendingSlot >= 0 && (g_main_state_flags & MSF_MENU_ACTIVE) == 0 &&
        (g_message_flags & 0x0004) != 0) {
        int slot = s_grabPendingSlot;
        s_grabPendingSlot = -1;
        zm_puppet_begin_grab(slot, s_grabPendingProne, s_grabPendingOwner);
    }
}

bool zombie_mode_feeding_target(const Entity* e)
{
    if (!zombie_mode_armed() || !zm_is_zombie_id(e->id) || e->health < 0) return false;
    if (e->state == ZOMBIE_STATE_EATING) return true;
    int slot = zm_slot_of(e);
    return slot >= 0 && slot < 30 && !s_iOwn && s_snapHave[slot] &&
           !s_snap[slot].dead && s_snap[slot].state == ZOMBIE_STATE_EATING;
}

void zombie_mode_stand_feeding(Entity* e)
{
    if (!zombie_mode_armed() || !zm_is_zombie_id(e->id) || e->health < 0) return;
    e->state = ZOMBIE_STATE_EATING;
    e->ignore_player_flag = 1;
    e->action_behavior = 0;
    e->action_state = 3;
    e->animationId = 0x0D;
    e->animation_frame_id = 0;
    e->timing_control = 0;
    e->blend_counter = 3;
    dbg_printf("[feed] zombie slot %d disturbed: standing up%s\n",
        (int)(e - g_EnemiesList), e == g_zombieModeEntity ? " for possession" : "");
}

void zombie_mode_begin_feeding(Entity* e, bool restored)
{
    if (!zombie_mode_armed() || !zm_is_zombie_id(e->id) || e->health < 0) return;
    if (e->state == ZOMBIE_STATE_INIT) {
        zm_world_queue_feeding(e);
        return; // the model and collision setup must run before changing state
    }
    e->status_flags &= ~ENTITY_STATUS_DEAD; // grab lock, not a monster death
    e->state = ZOMBIE_STATE_EATING;
    e->ignore_player_flag = 1;
    e->action_behavior = 0;
    e->action_state = 0;
    e->hit_state = 0;
    if (restored) {
        // Apply the feeding loop before this entity's first visible render.
        // No standing pose or blend from the init animation on room arrival.
        e->action_state = 2;
        e->animationId = 0x0C;
        e->animation_frame_id = 0;
        e->timing_control = 0;
        e->blend_counter = 0;
        e->action_ticks_counter = 120;
        Entity* saved = ENTITY;
        ENTITY = e;
        Joint_move(0, e->animHeader, e->animBase, 0x400);
        ENTITY = saved;
        if (e == g_zombieModeEntity) zombie_mode_stand_feeding(e);
    }
    dbg_printf("[feed] zombie slot %d feeding in stage %d room %02X\n",
        (int)(e - g_EnemiesList), (int)g_stageId, (int)g_roomId);
}

bool zombie_mode_feeding_update(Entity* e)
{
    if (!zombie_mode_armed() || e->state != ZOMBIE_STATE_EATING || e->health < 0) return false;
    // State 15 bypasses zombie_state_check, which normally publishes the aim
    // mask. Keep feeding zombies eligible for level and downward shots.
    zm_puppet_awareness(e, false);
    int dy = g_playerEntity.scaMatrixData.localMatrix.t[1] - e->scaMatrixData.localMatrix.t[1];
    if (dy >= -100 && dy <= 100) e->status_flags |= ENTITY_STATUS_PLAYER_BELOW;
    e->hit_state = 0;
    if (e->action_state == 0) {
        e->action_state = 1;
        e->animationId = 0x0B; // bend down
        e->animation_frame_id = 0;
        e->timing_control = 0;
        e->blend_counter = 3;
        e->action_ticks_counter = 120; // four seconds to recover on room arrival
    }
    int done = Joint_move(0, e->animHeader, e->animBase, 0x400);
    if (e->action_state == 1 && done) {
        e->action_state = 2;
        e->animationId = 0x0C; // feeding loop, without references to the former victim's joints
        e->timing_control = 0;
    } else if (e->action_state == 2) {
        if ((e->animation_frame_id & 7) == 0) Snd_em(3);
        if (e->action_ticks_counter != 0) e->action_ticks_counter--;
        int dx = g_playerEntity.scaMatrixData.localMatrix.t[0] - e->scaMatrixData.localMatrix.t[0];
        int dz = g_playerEntity.scaMatrixData.localMatrix.t[2] - e->scaMatrixData.localMatrix.t[2];
        if (e->action_ticks_counter == 0 && g_playerEntity.health >= 0 &&
            !zm_spec_away() && abs(dx) + abs(dz) < 1200) {
            zombie_mode_stand_feeding(e);
        }
    } else if (e->action_state == 3 && done) {
        e->state = ZOMBIE_STATE_IDLE;
        e->ignore_player_flag = 0;
        e->action_behavior = e->action_state = 0;
    }
    return true;
}

void zm_reconnect_route_events(void)
{
    if (s_gameRole != ZM_NET_OFF) zm_route_events();
}

bool zombie_mode_live_menu(void)
{
    return zombie_mode_armed() && zm_survivor_role();
}

bool zombie_mode_menu_frame(void)
{
    zombie_mode_reconnect_wait();
    unsigned int held = g_PlayerDpadHeld, pressed = g_PlayerDpadPressed;
    unsigned int rawHeld = g_PlayerPadHeld, rawPressed = g_button_pressed_id;
    WORD messages = g_message_flags;
    Entity* savedEntity = ENTITY;
    unsigned int mirror = g_main_state_flags & MSF_MIRROR_ENABLE;
    g_main_state_flags &= ~MSF_MIRROR_ENABLE; // update_entities also submits mirror geometry
    short health = g_playerEntity.health;
    g_PlayerDpadHeld = g_PlayerDpadPressed = 0;
    g_PlayerPadHeld = g_button_pressed_id = 0;
    player_menu_idle();
    // The menu clears the engine's monster-update permission. Restore it only
    // for this simulation step; the inventory retains its own message state.
    g_message_flags |= 0x0004;
    zombie_mode_enemies_begin();
    zombie_mode_fx_capture(true);
    update_entities();
    update_room_objects();
    zombie_mode_fx_capture(false);
    zombie_mode_enemies_end();
    zombie_mode_net_frame();
    bool attacked = s_forceMenuClose || g_playerEntity.health < health ||
                    g_playerEntity.health < 0 || g_playerEntity.isBeingAttackedFlag != 0;
    if (attacked) dbg_printf("[zombie] attack interrupted survivor menu: health %d -> %d, hurt %02X\n",
        (int)health, (int)g_playerEntity.health, (unsigned)g_playerEntity.isBeingAttackedFlag);
    s_forceMenuClose = false;
    ENTITY = savedEntity;
    g_main_state_flags |= mirror;
    g_message_flags = messages;
    g_PlayerDpadHeld = held; g_PlayerDpadPressed = pressed;
    g_PlayerPadHeld = rawHeld; g_button_pressed_id = rawPressed;
    return attacked || s_winShown; // leave inventory so the ending bumper can run
}

// game_loop, after the player's turn: pump the link, route what came in, and
// send this copy's character (and, owning a shared room, its monsters).
void zombie_mode_net_frame(void)
{
    // Receive escape requests/confirmed results before checking the host's
    // clock or all-dead condition. Same-update escapes take precedence.
    if (s_gameRole != ZM_NET_OFF) {
        zm_net_poll();
        zm_route_events();
    }
    zm_bumpers_frame();
    zm_clock_frame();
    zm_shotgun_frame();
    zm_piano_frame();
    zm_access_frame();
    zm_greenhouse_frame();
    zm_roomsync_frame();
    zm_statue_frame();
    zm_yawn_frame();
    zm_end_frame();
    zm_voice_frame();      // after the music's owners: its duck is the last volume set
    if (zm_match_authority() && !s_winShown && !zm_bumpers_hold_input()) zm_ai_frame();
    if (s_directorMapOnly && !s_winShown && !s_jumpPending) {
        unsigned int held, pressed;
        zm_director_input(g_zombieModeEntity, false, &held, &pressed);
    }
    if (s_gameRole == ZM_NET_OFF) return;
    zm_pickups_frame();
    if (zm_survivor_role()) {
        // A spectator's room runs another survivor's scripts, not its own.
        if (zm_spec_away()) zm_world_story_rebase();
        else zm_world_story_watch();
    }
    zm_drops_frame();
    zm_room_update();
    zm_reinforce_frame();
    zm_stun_frame();
    zm_standin_refresh();
    zm_burst_watch();
    // Monsters the director placed in this room since this copy loaded it.
    zm_spawn_missing_extras();
    zm_tyrant_victim_frame();
    zm_spec_frame();

    // Pulling the survivor out of its menu: a START press every few frames
    // (one press per menu screen; pressing every frame would reopen it the
    // moment it closed). Set late in the game task's frame, so the menu task
    // after it sees the press and game_loop's own menu check, next frame,
    // sees the real pad again.
    if (s_forceMenuClose) {
        if ((g_main_state_flags & MSF_MENU_ACTIVE) == 0) {
            s_forceMenuClose = false;
        } else if (!zombie_mode_live_menu() && ++s_forceMenuFrames % 15 == 1) {
            g_PlayerPadHeld |= 0x0800;
            g_button_pressed_id |= 0x0800;
        }
    }
    if (zm_director_role()) {
        zm_net_send_state(g_zombieModeEntity, true, NULL, -1);
    } else {
        // Mid-grab this copy is the one moving that zombie: send it along,
        // the owner shows it as is.
        zm_net_send_state((const Entity*)&g_playerEntity, true,
                          s_grabSlot >= 0 ? &g_EnemiesList[s_grabSlot] : NULL, s_grabOwnerSlot);
    }
}

// ---------------------------------------------------------------------------
// The survivors, for the director's HUD and map
// ---------------------------------------------------------------------------
int zm_survivor_list(ZmSurvivorInfo* out, int max)
{
    int n = 0;
    if (s_gameRole == ZM_NET_OFF) {
        if (max > 0 && zm_survivor_location(&out[0].stage, &out[0].room)) {
            out[0].character = (g_playerEntity.id & 1) ? ZM_CHAR_JILL : ZM_CHAR_CHRIS;
            out[0].dead = g_playerEntity.health < 0;
            out[0].health = g_playerEntity.health;
            out[0].player = 1;
            n = 1;
        }
        return n;
    }
    // The arbiter counts its own survivor too (an AI director's host).
    bool own = zm_match_authority();
    for (int i = 0; i < ZM_NET_MAX_PLAYERS && n < max; i++) {
        int ch = zm_net_char(i);
        if (ch < 0 || (i == zm_net_self() && !own)) continue;
        const ZmNetPeerState* p = zm_seat_state(i);
        if (p == NULL) continue;
        out[n].stage = p->stage;
        out[n].room = p->room;
        out[n].character = ch;
        out[n].dead = p->dead;
        out[n].health = p->health;
        out[n].player = i;
        n++;
    }
    return n;
}

// RE1_DEBUGLOG: on each of this survivor's shots, what the gun could aim at
// here - for chasing shots that do not land on a shared room's monsters.
static void zm_shot_audit(void)
{
    dbg_printf("[shot p%d] aim %02X dir %d at (%d,%d) owner %d%s weapon %d\n", zm_net_self(),
               (unsigned)(g_playerEntity.flags & 0xE0), (int)g_playerEntity.directionAngle,
               (int)g_playerEntity.scaMatrixData.localMatrix.t[0],
               (int)g_playerEntity.scaMatrixData.localMatrix.t[2], s_owner, s_iOwn ? " (us)" : "",
               (int)g_playerEntity.equippedWeaponId);
    for (int i = 0; i < ZM_FIRST_SURVIVOR_SLOT; i++) {
        const Entity* e = &g_EnemiesList[i];
        if ((e->status_flags & ENTITY_STATUS_ACTIVE) == 0) continue;
        int dx = (int)e->scaMatrixData.localMatrix.t[0] - (int)g_playerEntity.scaMatrixData.localMatrix.t[0];
        int dz = (int)e->scaMatrixData.localMatrix.t[2] - (int)g_playerEntity.scaMatrixData.localMatrix.t[2];
        dbg_printf("[shot p%d]   slot %d id %02X st %02X hs %d state %d hp %d shown %d snap %d own %d "
                   "dx %d dz %d dy %d\n", zm_net_self(), i, (unsigned)e->id, (unsigned)e->status_flags,
                   (int)e->hit_state, (int)e->state, (int)e->health, (int)s_slotShown[i],
                   (int)s_snapHave[i], (int)s_ownerOf[i], dx, dz,
                   (int)(e->scaMatrixData.localMatrix.t[1] - g_playerEntity.scaMatrixData.localMatrix.t[1]));
    }
}

void zm_bumper_intro_clock_release(void)
{
    if (!s_zombieModeArmed || zm_net_rejoining()) return;
    s_gameStartMs = zm_game_time_ms();
    s_allSpawnedMs = 0;
}
void zm_bumper_end_clock_release(void)
{
    s_winAtMs = zm_game_time_ms(); // results timings begin after the cinematic
}
