#pragma once
#include "ZombieMode.h"

// Quick testing: 15-second character select, 20000 director points, no monster
// unlock wait or main-hall lockout, and
// loaded guns, all four crests in the main hall and every locked door (key
// doors, the crest door, the elevator's power, the keypad) open. Comment out to disable; rebuild
// every multiplayer copy after changing it.
//#define QUICK_DEBUG

// ============================================================================
// ZombieModeInternal.h - what ZombieMode.cpp (the possessed zombie) and
// ZombieSurvivor.cpp (the AI-driven human) share. Not for the rest of the game.
// ============================================================================

// Remapped pad bits (g_PlayerDpadHeld / g_PlayerDpadPressed), as the player's
// own state machine in PlayerAnimations.cpp reads them.
// Every door animation in the mode lasts this many 33 ms frames: short, and
// the same for everyone - no player gets through a door faster by skipping.
#define ZM_DOOR_FRAMES   20

#define ZM_PAD_FORWARD   0x0001
#define ZM_PAD_TURN_A    0x0002   // + angle, as player_ctrl_behavior_walk
#define ZM_PAD_BACK      0x0004
#define ZM_PAD_TURN_B    0x0008   // - angle
#define ZM_PAD_FIRE      0x0040   // fire while aiming
#define ZM_PAD_ACTION    0x0080   // action / check
#define ZM_PAD_AIM       0x0100
#define ZM_PAD_RUN       0x0200

// While the survivor is in another room the player entity is parked in a
// corner of the 0..32767 square, the corner furthest from the room's enemies.
// NOT outside it: the engine measures distance as SquareRoot0(dx*dx + dz*dz)
// in 32-bit ints, which only fits while both points are inside that square
// (the diagonal, 46340, is the limit). An earlier (-20000, -20000) overflowed
// for zombies on the far side of a room: they read the player as on top of
// them, and the SCA push between them threw the possessed zombie billions of
// units off the map.
#define ZM_PARK_MAX 32767

// One door_set record (cmd_door_set, 0x0C) read out of a room's init SCD. The
// 24 bytes after the opcode and slot: zone box, then the destination block.
struct ZmDoor {
    unsigned short zoneX, zoneZ, zoneW, zoneD;  // +0x00 action zone
    unsigned char  type;                        // +0x08 .dor animation index
    unsigned char  sfx;                         // +0x09
    unsigned char  flags0B;                     // +0x0B: 0x80 camera-only, low 6 bits camera
    unsigned char  lock;                        // +0x0C: 0x80 locked, 0x40 char-restricted, low 6 = flag
    unsigned char  dest;                        // +0x0D: room, | stage << 5 when it changes stage
    short          arriveX, arriveY, arriveZ;   // +0x0E arrival point in the destination
    short          arriveAngle;                 // +0x14
    unsigned char  needItem;                    // +0x16
};

#define ZM_MAX_DOORS 16

// The doors of (stage, room), read from its RDT and cached. Returns the count.
int  zm_room_doors(unsigned char stage, unsigned char room, const ZmDoor** out);
// room_transition_load's destination decode for a door's +0x0D byte, for a
// door that is in stage `fromStage` (a dest below 0x20 stays in that stage).
void zm_decode_dest(unsigned char dest, unsigned char fromStage,
                    unsigned char* stage, unsigned char* room);
// Can a character walk through this door right now (unlocked, not camera-only,
// not leading back into the same room)?
bool zm_door_usable(const ZmDoor* d, unsigned char stage, unsigned char room);

// One enemy_set record (cmd_enemy_set, 0x1B) of a room's init SCD.
struct ZmSpawn {
    unsigned char slot;          // +0x12 & 0xF
    unsigned char id;            // +0x01 enemy type
    unsigned char deathFlag;     // +0x03 g_EnemiesFlags bit, 0xFF = none
    unsigned char uncond;        // +0x04 != 0: spawned every visit, never snapshotted
    short         x, y, z;       // +0x0C, +0x0E, +0x10
    short         angle;         // +0x08
};
#define ZM_MAX_SPAWNS 16

// The enemy_set records of (stage, room) - read with its doors and cached.
// -1 when the room has no loadable RDT.
int  zm_room_spawns(unsigned char stage, unsigned char room, const ZmSpawn** out);
// Is (stage, room) already in the cache (no file load needed)?
bool zm_room_cached(unsigned char stage, unsigned char room);
bool zm_is_zombie_id(unsigned char id);
// A zombie, or a monster the director can take over (Cerberus, Hunter, Chimera).
bool zm_is_possessable_id(unsigned char id);

// The highest enemy slot any enemy_set (0x1B) in the loaded room's init or
// per-frame SCD writes, or -1. Slots above it are never touched by the room.
int  zm_room_highest_script_slot(void);

// ---- The loaded room's walk grid (ZombieNav.cpp) ----
// Build it for the room that just loaded (a no-op for the same room again).
void zm_nav_build(void);
bool zm_nav_ready(void);
// A route of cell centres from (fromX, fromZ) to within goalRadius of
// (toX, toZ), start first. Returns the point count, 0 if there is no route.
int  zm_nav_path(int fromX, int fromZ, int toX, int toZ, int goalRadius,
                 int* outX, int* outZ, int maxPoints);
// Nothing fully blocking between the two points?
bool zm_nav_segment_clear(int ax, int az, int bx, int bz);

// ---- The multiplayer link (ZombieNet.cpp; public half in ZombieNet.h) ----
// Joints carried per pose: the player models have 15, the Hunter 16, the
// Chimera 17, the Web Spinner and the giant spider 20.
#define ZM_NET_JOINTS 20
// A placed, posed skeleton: position, facing, and Joint_move's inputs (the
// root translation and each joint's rotation).
struct ZmNetPose {
    int            x, y, z;
    short          angle;
    unsigned char  jointCount;
    short          root[3];
    short          rot[ZM_NET_JOINTS][3];
};
// Another copy's character, as of its newest STATE packet.
struct ZmNetPeerState {
    bool           valid;          // in a game
    bool           dead;
    // A dead survivor watching the others: stage, room, spot and pose are its
    // corpse's; the player itself is in no room (ZombieSpectate.cpp).
    bool           spectating;
    bool           transitioning; // hidden while using a door between rooms
    bool           roomPairBusy; // owner retains simulation until paired playback ends
    unsigned char  viewStage, viewRoom;   // the room its copy has loaded
    unsigned char  camera;               // the active camera in that room
    unsigned char  entranceDoor;         // current room's door index, 0xFF if none
    unsigned char  stage, room;
    int            x, y, z;
    short          angle, health;
    unsigned char  jointCount;
    short          root[3];
    short          rot[ZM_NET_JOINTS][3];
    unsigned int   receivedMs;
    unsigned char  weapon;         // the item id in hand (a survivor's; 0 none)
    bool           attacked;       // a survivor reacting to a hit (isBeingAttackedFlag)
    // A survivor's copy, during a grab it is running: the zombie, as the real
    // zombie_attack there is moving it (the attacker's owner shows that).
    bool           hasZombie;
    unsigned char  zombieSlot;     // ...which monster that is (enemy slot)
    ZmNetPose      zombie;
};
// Pointer-free controller fields needed to continue a death across owners.
// Keep the ranges explicit: +0x170 (Tyrant heart) and +0x174 (Hunter partner)
// can hold local pointers and must never travel to another copy.
struct ZmDeathPlayback {
    unsigned char status, behavior;
    unsigned char movement[0x18]; // Entity +0x6C..0x83
    unsigned char state[9];       // Entity +0x84..0x8C
    unsigned char animation[16];  // Entity +0xBC..0xCB
    unsigned char phase[4];       // Entity +0x16C..0x16F
    unsigned char extra[8];       // type-specific scalars +0x170..0x177
    unsigned char tail[20];       // Entity +0x178..0x18B
};
inline unsigned int zm_death_extra_bytes(unsigned char id)
{
    // Zombies, Cerberus and Chimera use all eight bytes as scalar timers /
    // velocity. Hunter uses the first four, followed by a partner pointer.
    if (id == 0 || id == 1 || id == 0x11 || id == 2 || id == 3 || id == 4 || id == 9) return 8;
    if (id == 6) return 4;
    return 0;
}
// Persistent joint appearance, independent of one-shot hit/burst events.
// A late arrival needs the finished damage, without replaying the explosion.
struct ZmMonsterAppearance {
    unsigned int hiddenMask;
    unsigned int tintMask;
    unsigned int color[ZM_NET_JOINTS];
};
void zm_monster_appearance_capture(const Entity* e, ZmMonsterAppearance* out);
void zm_monster_appearance_apply(Entity* e, const ZmMonsterAppearance& appearance);
// One monster of a room, as its owning copy sent it.
struct ZmNetEnemy {
    unsigned short uid;            // the owner's ZombieWorld uid for it (ZM_WORLD_UID_*)
    unsigned char  slot, id, state;
    bool           dead;
    short          health;
    ZmNetPose      pose;
    bool           settled;        // death animation finished; safe to freeze joints
    ZmDeathPlayback death;
    ZmMonsterAppearance appearance;
};
bool zm_monster_death_settled(const Entity* e);
bool zm_monster_dead(const Entity* e);
// Event kinds (reliable, in order per link). Room-bound kinds carry the
// sender's stage | room << 8 so a receiver elsewhere can drop them.
enum {
    ZM_EV_HIT = 1,       // shooter -> room owner: { damage, hit_state, slot }
    ZM_EV_GRAB = 2,      // room owner -> victim: { prone, slot } - run zombie_attack on that puppet
    ZM_EV_GRAB_END = 3,  // victim -> room owner: { x, z, angle, refused, slot } - control back
    ZM_EV_ROSTER = 4,    // anyone -> all: { stage | room << 8, uid | alive << 15, x, y, z, angle,
                         //   health, behaviour | id << 8 | directorControlled << 15 } - one roster monster (ZombieWorld.cpp)
    ZM_EV_SOUND = 5,     // room owner -> all: { Snd_em id, slot, stage | room << 8 }
    ZM_EV_FX = 6,        // anyone -> all: { type | depth << 8, yaw, x, y, z, light,
                         //   stage | room << 8 } - a billboard
    ZM_EV_PSND = 7,      // survivor -> all: { bank-1 sound id, vol, stage | room << 8 } -
                         //   its own shot / reload / click, played from its stand-in
    ZM_EV_PHURT = 8,     // room owner -> victim: { damage, isBeingAttackedFlag, animationId
                         //   or -1, action_behavior or -1, facing or 0x7FFF, healthStatusFlags
                         //   gained, attacker id, stage | room << 8 } - a monster's hit on it
    ZM_EV_WIN = 9,       // survivor -> host: {player,0,exit room,health,seed low,seed high};
                         // host -> all: {winner/-1,reason,elapsed seconds,seed low,seed high}
    ZM_EV_BURST = 10,    // room owner -> all: { slot, joint mask, stage | room << 8 } - joints
                         //   of a monster that burst (a magnum head shot, rocket gore)
    ZM_EV_HEAL = 11,     // Rebecca -> all: { x, z, stage | room << 8 } - her spray heals
                         //   the survivors near her (ZombiePerks.cpp)
    ZM_EV_STUN = 12,     // survivor -> all: { x, z, stage | room << 8 } - it came into the room
                         //   there; the owner stuns the monsters near it (the door stun)
    ZM_EV_STORY = 13,    // survivor -> all: { bank, byte, bits set, bits cleared } - a story
                         //   flag (g_ScenarioFlags / g_ScenarioFlags2) changed (ZombieWorld.cpp)
    ZM_EV_CREDIT = 14,   // survivor owning a room -> director: { slot, stage | room << 8 } - one
                         //   of the director's monsters there hit a survivor (ZombieEconomy.cpp)
    ZM_EV_TRAP = 15,     // director -> all: { trap id, stage | room << 8, duration / 100 ms } -
                         //   a trap set on that room (ZombieTraps.cpp)
    ZM_EV_BOX = 16,      // private atomic box request/receipt; host -> all updates
                         //   changed; the box is shared (ZombieWorld.cpp)
    ZM_EV_CLOCK = 17,    // director -> all: { seconds left, standing still, seconds run } - the game
                         //   clock, every 5 s and on every hold or bonus;
                         //   at zero the director sends ZM_EV_WIN { player, 1 } (it won)
    ZM_EV_DROP = 18,     // survivor -> all: { uid, item | qty << 8, x, y, z, angle,
                         //   stage | room << 8 } - it dropped an item there (ZombieDrops.cpp)
    ZM_EV_DROP_TAKE = 19,// survivor -> all: { uid, qty left } - a drop picked up (0: all of it)
    ZM_EV_PUSH_OFF = 22, // victim -> room owner: { x, z, stage | room << 8, y }
    ZM_EV_REVIVE = 21,   // survivor -> all: { target player, health }
    ZM_EV_TYRANT_REACT = 23, // owner -> victim: {room, damage, state|behavior<<8, uid, facing, x/baseX, z/baseZ, attackAnim}
    ZM_EV_FEED = 25, // roster companion: {stage|room<<8, uid, id, feeding}
    ZM_EV_FX_BASIS = 26, // FX companion: first eight parent rotation coefficients
    ZM_EV_FX_ORIGIN = 27, // FX companion: last coefficient, parent xyz, local xyz, shooter floor
    ZM_EV_JOINT_TINT = 28, // {uid, joint, color low/high, stage|room<<8, enemy id}
    ZM_EV_REINFORCE = 29, // doorway reinforcement request/reply/warning/entrance hold
    ZM_EV_PICKUP = 30, // host-reserved floor/drop item transaction
    ZM_EV_SHOTGUN = 31, // host-authorized replacement / door rescue / ceiling state
    ZM_EV_PIANO = 32,   // survivor -> all: {1 start / 2 stop / 5 finished, player, stage, room, 0, 0,
                        //   seed low, seed high}; survivor -> host {3 done, ...}; host -> all {4 open, ...}
    ZM_EV_ROOMSYNC = 33, // survivor -> all: {1 object, stage|room<<8, slot, x, z, orig x, orig z} /
                         //   {2 flags, stage|room<<8, byte, set, cleared} / {3 reset, stage|room<<8} -
                         //   a room's pushed objects and puzzle flags (ZombieRoomSync.cpp)
    ZM_EV_TIMEOUT = 34,  // survivor -> host {1 request, player, seed low, seed high}; host -> all
                         //   {2 start, player, seed low, seed high, duration / 100 ms} / {4 end, ...};
                         //   host -> requester {3 refused, reason, ...} (ZombieTimeout.cpp)
    ZM_EV_BOSS = 35,     // host -> all: {1 lockdown, boss, tenths of a second, seed low, seed high} /
                         //   {2 done, boss, 0, seed low, seed high} - a Yawn fight's phases; the
                         //   entry vote (ZombieYawn.cpp): survivor -> host {3 request, boss, player,
                         //   seed, seed} / {5 answer, boss, player, seed, seed, token, yes}; host -> all
                         //   {4 ask, boss, 0, seed, seed, token, voters} / {6 go, ...token} /
                         //   {7 called off, ...token}; host -> requester {8 refused, boss, player, ...}
    ZM_EV_VOICE = 36,    // survivor -> all: { line, take, character, stage | room << 8, x, y, z, 0 } -
                         //   a voice line; each copy hears it in that room or through a door
                         //   from a neighbouring one (ZombieVoice.cpp)
    ZM_EV_TYRANT_TRAIL = 24, // room owner -> all: { stage|room<<8, attacker uid, trail frames }
    ZM_EV_STATS = 20,    // anyone -> all, once the match is over: a survivor's { 0, death s
                         //   (-1 alive), hits, kills, damage taken }, the director's { 1, points
                         //   spent, earned, monsters bought, traps set, monsters lost, hits on
                         //   survivors } (ZombieStats.cpp)
};
bool zm_net_active(void);
// Another player's newest state (NULL for this copy, a free seat, or one not
// yet in the game).
const ZmNetPeerState* zm_net_player(int player);
// `zombieOverride`: a survivor's copy, mid-grab, also sends the zombie it is
// running (NULL otherwise).
// `overrideSlot`: the room owner's slot number for that zombie.
void zm_net_send_state(const Entity* character, bool inGame, const Entity* zombieOverride,
                       int overrideSlot, bool departing = false);
// A dead survivor watching the others: from the next send on, every STATE is
// this one's (its corpse's) with the spectating flag; false lets it go.
void zm_net_freeze_state(bool on);
// To one player, or ZM_NET_ALL.
void zm_net_send_event_to(int dst, int kind, short a0, short a1, short a2, short a3,
                          short a4, short a5, short a6, short a7);
void zm_net_send_event(int kind, short a, short b, short c, short d);        // to all
bool zm_net_has_item(int player, unsigned char item);
bool zm_net_inventory(int player, unsigned char out[16]);
bool zm_shotgun_crushed(int player);
void zm_shotgun_clear_inventory(void);
unsigned int zm_random_progression_bit(unsigned char item);
unsigned int zm_drops_progression(const bool* reachable);
int zm_random_remaining_solvable(void); // -1: checkpoints not ready; 0: blocked; 1: escape reachable
void zm_match_progression_lost(void);
void zm_shotgun_reset(void);
// The bar's piano (ZombiePiano.cpp).
void zm_piano_reset(void);
// The survivors' timeout (ZombieTimeout.cpp).
void zm_timeout_reset(void);
void zm_timeout_take(const short* args, int src);
bool zm_timeout_active(unsigned int now);   // plat_time_ms; the match is held
void zm_timeout_poll(void);
bool zm_timeout_pause_frame(void);
const char* zm_timeout_map_line(void);
void zm_timeout_map_input(unsigned int edge);
void zm_timeout_draw(void);
unsigned int zm_match_elapsed_ms(void);     // ms on the game clock since every survivor came in
// A room's puzzle state among the survivors in it (ZombieRoomSync.cpp).
void zm_roomsync_reset(void);
void zm_roomsync_room(void);
void zm_roomsync_frame(void);
void zm_roomsync_take(const short* args, int src);
// Yawn, the attic's own boss (ZombieYawn.cpp). Thirteen enemy slots: the head
// in slot 0 and its body segments in 1-12 (yawn_init's copies).
#define ZM_YAWN_SLOTS 13
bool zm_yawn_entity(const Entity* e);       // a Yawn slot, head or segment
bool zm_yawn_segment(const Entity* e);      // one of the twelve body segments
bool zm_yawn_head(const Entity* e);
// A room script's enemy_set slot as the slots it fills (Yawn: twelve more).
int  zm_yawn_last_slot(unsigned char id, int slot);
void zm_yawn_after_update(Entity* e);
void zm_yawn_room_reset(void);
void zm_yawn_room(void);
void zm_yawn_frame(void);
int  zm_yawn_music_bank(void);
// The randomizer's item_model_set hook: before its patch (`op` as the room
// has it) and after it.
void zm_yawn_item_before(const unsigned char* op);
void zm_yawn_item_after(unsigned char* op);
// The owner's body (joints 3-14 placed by Yawn's own animator) on the wire,
// after the head's ZmNetEnemy in an ENEMIES packet.
struct ZmYawnBody {
    short         t[ZM_YAWN_SLOTS - 1][3];   // joints 3..14, world translation
    unsigned char status[ZM_YAWN_SLOTS];     // slots 0..12, status_flags
    unsigned char pad;
};
bool zm_yawn_body_capture(ZmYawnBody* out);
void zm_yawn_body_take(const ZmYawnBody& body, unsigned char stage, unsigned char room, int origin);
// A puppet Yawn posed from the owner's head entry and body. False: no body
// from `owner` for this room yet.
bool zm_yawn_pose_apply(Entity* head, const ZmNetPose& pose, int owner);
void zm_yawn_status_apply(Entity* e, int slot);
void zm_yawn_mark_scaled(void);
void zm_yawn_bounds_begin(Entity* e);
void zm_yawn_bounds_end(Entity* e);
// While Yawn is in the loaded boss room, or its lockdown holds, its doors do
// not let a survivor out.
bool zm_yawn_traps_exit(unsigned char stage, unsigned char room);
// The match's boss phases (the host's; ZM_EV_BOSS on a survivor's copy).
void zm_yawn_new_game(void);
void zm_yawn_take(const short* args, int src);
// The survivor's pad while the entry vote asks it (true: taken).
bool zm_yawn_prompt_input(void);
// A body-sized spot of the loaded room clear of its walls (ZombieMode.cpp).
bool zm_spot_free(int x, int z);
bool zm_yawn_clock_hold(void);                  // the game clock stands still
// Yawns beaten this match (lockdown or done; the host's word on a survivor's
// copy) - the director's monster unlocks - and whether survivors are in a
// fight with the next one now.
int  zm_yawn_beaten_count(void);
bool zm_yawn_lockdown(void);    // a beaten Yawn's 60 s: the director earns no points
bool zm_yawn_fight_on(void);
bool zm_yawn_director_closed(unsigned char stage, unsigned char room);
bool zm_yawn_free_revive(int player, unsigned char stage, unsigned char room);
void zm_yawn_revived(int player);
// A boss room the director may jump into: its Yawn's id while that is alive
// (its fight not done), else 0 - and where the mode places it.
unsigned char zm_yawn_jump_room(unsigned char stage, unsigned char room);
void zm_yawn_spawn_spot(unsigned char stage, unsigned char room, short* x, short* z, short* angle);
// A human director and a fight under way: where it must go to play the Yawn
// (false: nowhere, or already there in it), and whether it is held to `e`.
bool zm_yawn_force_jump(const Entity* e, unsigned char* stage, unsigned char* room);
bool zm_yawn_director_bound(const Entity* e);
// The living survivors standing in this room as this copy knows them: x, z
// and player index (-1: single player's own) - ZombieMode.cpp.
int  zm_room_survivors(int (*xz)[2], int* who, int max);
void zm_yawn_draw(void);
// The game clock (ZombieMode.cpp): time added (a boss beaten).
void zm_clock_add_bonus(unsigned int ms);
// Every survivor's revives counted afresh (ZombieSpectate.cpp).
void zm_revive_reset_counts(void);
// The lesson room <-> B1 passage 1 hole: open once Yawn 2 is beaten. Shut, a
// door record of either room through it (`dest` from `stage`/`room`) is no way.
bool zm_yawn_hole_open(void);
void zm_yawn_hole_action(void);
bool zm_yawn_hole_door(const unsigned char* record);
void zm_yawn_blocked_sound(void);
bool zm_yawn_hole_shut(unsigned char stage, unsigned char room, unsigned char dest);
// Yawn 2 beaten: the lesson room <-> front lesson room door, shut for good.
bool zm_yawn_lesson_sealed(unsigned char stage, unsigned char room, unsigned char dest);

// The 2F statue falling into the 1F dining room (ZombieStatue.cpp).
void zm_statue_room_reset(void);
void zm_statue_room(void);
void zm_statue_frame(void);
void zm_greenhouse_room(void);
void zm_greenhouse_frame(void);
void zm_piano_room(void);
void zm_piano_frame(void);
void zm_piano_take(const short* args, int src);
void zm_piano_draw(void);
bool zm_piano_open(void);
int  zm_piano_music_bank(void);
void zm_shotgun_room(void);
void zm_shotgun_frame(void);
void zm_shotgun_take(const short* args, int src);
bool zm_shotgun_door(const unsigned char* record);
bool zm_shotgun_pending(void);
unsigned short zm_shotgun_replacement_consumed(void);
bool zm_shotgun_pickaxe_consumed(void);
bool zm_shotgun_route_open(unsigned char fromStage, unsigned char fromRoom, unsigned char toStage, unsigned char toRoom, unsigned int owned, bool plateReachable);
bool zm_shotgun_monster_blocks(int room, int mx, int mz, int x, int z);
bool zm_world_shotgun_clear(unsigned char stage, unsigned char room, int x, int z);
void zm_shotgun_pickup(const short* args);
bool zm_shotgun_pickup_valid(const short* args);
unsigned short zm_shotgun_pickup_identity(const unsigned char* record);
bool zm_shotgun_draw(void);
void zm_shotgun_crush_view(bool enable);
void zm_world_shotgun_crush(void);
void zm_world_shotgun_seal(void);
bool zm_shotgun_room_blocked(unsigned char stage, unsigned char room);
bool zm_shotgun_hide_room(void);
void zm_shotgun_director_crush(void);
void zm_note(const char* text);
// ---- The survivors' voice lines (ZombieVoice.cpp) ----
void zm_voice_reset(void);
void zm_voice_frame(void);
void zm_voice_draw(void);
void zm_voice_take(const short* args, int src);
// The mod's own message text (encoded, 0x01-terminated) on the message line,
// as a room's message: passive in the room (ZombieMessages.cpp), as it was
// under a menu.
void zm_message_show(const unsigned char* text);
void zm_net_send_event8(int kind, short a0, short a1, short a2, short a3,    // to all
                        short a4, short a5, short a6, short a7);
bool zm_net_take_event(int* kind, short args[8], int* src);
// WIN requests/confirmations bypass the ordinary event inbox.
bool zm_match_take_win(const short* args, int src); // false: retain until game initialization finishes
// A room owner's monsters (ZombieMode.cpp receives them), each with the uid
// it is kept under (`uids`, parallel to `list`).
void zm_net_send_enemies(const Entity* const* list, const unsigned short* uids, int count, bool adopt);
void zm_shared_receive_enemies(const ZmNetEnemy* list, int count, bool adopt,
                               unsigned char stage, unsigned char room, int origin);

// An SCD command's operand width (after the opcode byte), -1 if unknown -
// tools/mine_room_scd.py's table (ZombieMode.cpp).
int  zm_scd_width(unsigned char op);

// Load a texture on the mod's own page / CLUT-row counter (the widened
// range, restarted each room) instead of the room's (ZombieMode.cpp). False:
// no pages left, nothing switched. zm_ext_end after the load.
bool zm_ext_begin(int pages);
void zm_ext_end(void);

// ---- The randomized scenario (ZombieRandom.cpp) ----
void zm_random_new_game(void);
// Build the scenario for a seed (cached): the lobby's route map uses it
// before the game applies it.
bool zm_random_build(unsigned int seed);
bool zm_random_active(void);
bool zm_random_item(unsigned char stage, unsigned char room, unsigned char flag,
                    unsigned char* id, unsigned char* qty);
unsigned char zm_random_door_need(unsigned char lockFlag, unsigned char need);
// A door record's lock (+0x0C) and need (+0x16) with the mode's own key locks.
void zm_random_door_lock(unsigned char stage, unsigned char room, unsigned char slot, unsigned char dest,
                         unsigned char* lock, unsigned char* need);
// The key-locked doors of a room: destination room and the key it needs now.
int  zm_random_room_locks(unsigned char stage, unsigned char room, unsigned char* toRoom,
                          unsigned char* key, int max);
unsigned int zm_random_seed(void);
// What the scenario puts in a room (the director's route map).
int  zm_random_room_items(unsigned char stage, unsigned char room, unsigned char* ids,
                          unsigned char* qtys, bool* isNew, int max);
// A key or crest of the scenario not yet taken in (stage, room).
bool zm_random_room_key_left(unsigned char stage, unsigned char room);
// Item looks and free roomItems flags, for the dropped items (ZombieDrops.cpp).
const int* zm_random_look(unsigned char id);
bool zm_random_has_look(unsigned char id);
int  zm_random_free_flags(unsigned char* out, int max);
// The pass number's note: a pickup of the scenario's own that is no item -
// reading it tells the keypad's pass number (ZombieKeypad.cpp). Lies as the
// red book's model.
#define ITEM_ZM_PASS_NOTE 0xF0
// This game's pass number for the 2F left stairs' keypad, 0-9999 (from the seed).
unsigned int zm_random_pass_code(void);
// Is the loaded room's action slot `slot` the pass number's note?
bool zm_random_note_slot(unsigned char slot);

// ---- The back area's ways in (ZombieKeypad.cpp) ----
// The small elevator (kitchen <-> 2F back passage) runs once a survivor has
// put the battery in; the 2F left stairs' keypad door to the rough passage
// opens on the note's pass number. Story flags (ScenarioFlags2), so the
// STORY sync and the reconnect snapshot carry them.
#define ZM_FLAG2_ELEVATOR_POWER 0xF8
#define ZM_FLAG2_KEYPAD_OPEN    0xF9
bool zm_access_powered(void);
bool zm_access_keypad_open(void);
void zm_access_new_game(void);
void zm_access_room(void);
void zm_access_frame(void);
void zm_access_draw(void);
// zombie_mode_survivor_input: true while the keypad has the pad.
bool zm_access_input(void);
bool zm_access_keypad_up(void);
// create_room_event (zombie_mode_skip_room_event): the keypad or the note. True: handled.
bool zm_access_room_event(const unsigned char* entry);
// door_try_enter, a survivor's: the unpowered elevator. True: refused (message shown).
bool zm_access_door_refused(const unsigned char* record);
// The AI survivor's door graph: is the door of (stage, room) to `dest` shut?
bool zm_access_door_closed(unsigned char stage, unsigned char room, unsigned char dest);
// The survivors' route map: the files line ("PASS NUMBER 1234"), NULL until the note is read.
const char* zm_access_files_line(void);

// ---- The survivors' dropped items (ZombieDrops.cpp) ----
// Room action slots ZM_DROP_SLOT_FIRST.. are the drops' (the randomizer's new
// spots stay below); up to ZM_DROPS_PER_ROOM a room, on item models after
// the first ZM_RANDOM_MODELS.
#define ZM_DROP_SLOT_FIRST   (ROOM_ACTION_ENTRIES - ZM_DROPS_PER_ROOM)
#define ZM_DROPS_PER_ROOM    40
#define ZM_RANDOM_MODELS     (ROOM_ITEM_MODELS - ZM_DROPS_PER_ROOM)
void zm_drops_new_game(void);
void zm_drops_room_loaded(void);
void zm_drops_room_reset(void);
void zm_drops_frame(void);
void zm_drops_take_event(int kind, const short* args, int src);
// cmd_item_model_set: a dropped item's look (NULL: not a drop here).
const int* zm_drop_look(const unsigned char* op);
// True for a dropped item's pickup record, including a death drop.
bool zm_drop_is_pickup(const unsigned char* record);
bool zm_drop_pickup_uid(const unsigned char* record, unsigned short* uid);
bool zm_drop_available(unsigned short uid, unsigned short room, unsigned short item);
void zm_pickups_reset(void);
void zm_pickups_frame(void);
void zm_pickups_take(const short* args, int src);
// The roomItems flags the drops own on this copy (left out of the flag merge).
unsigned char zm_drops_flag_mask(int byteIndex);

// A mansion room with no room file (a 4-byte stub RDT).
bool zm_random_room_stub(unsigned char stage, unsigned char room);
// A safe room (its scripts set an item box): the director may not go in or place there.
bool zm_random_room_safe(unsigned char stage, unsigned char room);
// Elevator car: no director entry, placement, marching or reinforcement.
bool zm_access_room_elevator(unsigned char stage, unsigned char room);
// Room hooks: the room's new item spots go in after its init script
// (zombie_mode_room_spawn); the per-room model cache is dropped on reset.
void zm_random_room_loaded(void);
void zm_random_room_reset(void);

// Three monster spawn spots per mansion room (ZombieSpawnSpots.cpp) - declared
// below; ZombieRandom.cpp puts its new item spots on them too.

// ---- Survivor perks (ZombiePerks.cpp) ----
// A character's card for the character select screen.
struct ZmPerkInfo {
    const char*   fullName;
    const char*   description;
    const char*   perk;
    int           toughness;        // 0-10
    int           health;           // max health it gives
    unsigned char weapon;           // the item it starts holding
    int           itemCount;
    const char*   items[6];
    unsigned char itemIds[6];
    int           itemCounts[6];
};
void zm_perk_describe(int ch, ZmPerkInfo* out);
// The model loader's character while the lobby previews one (-1: back to
// the game's own choice). ZombieMode.cpp.
void zombie_mode_preview_skin(int ch);
// The starting kit's (item, quantity) pairs, -1 for the default kit.
int  zm_perk_kit(unsigned char (*kit)[2], int max);
// ZM_EV_HEAL on a survivor's copy.
void zm_perk_take_heal(const short* a);
// Richard's radio has OPTIONS (and START while it is up).
bool zm_perk_radio_blocks_menu(void);

// ---- The persistent world (ZombieWorld.cpp) ----
void zm_world_reset(bool on);
// On / off without clearing (a survivor's roster lives from its join).
void zm_world_set_on(bool on);
void zm_world_capture_room(const Entity* skip);
void zm_world_depart_entity(const Entity* e);
void zm_world_apply_feeding(const short* a);
void zm_world_queue_feeding(Entity* e);
bool zm_world_restore_enemy(Entity* e, unsigned char slot, unsigned char id);
void zm_world_after_update(Entity* e);
void zm_world_accept_net_state(Entity* e);
// A monster whose init has not run yet: its health after the init, from a net snapshot.
void zm_world_pending_health(Entity* e, short health);
void zm_world_room_reset(void);
int  zm_world_spawn_extras(int firstSlot, int endSlot);
void zm_world_track(const Entity* e);
void zm_world_track_uid(const Entity* e, unsigned short uid);
#define ZM_WORLD_UID_NONE 0xFFFF
#define ZM_WORLD_UID_NEW  0xFFFE
#define ZM_WORLD_UID_BODY 0xFFFD     // on the wire only: the director's own body
unsigned short zm_world_uid_of_slot(int slot);
int  zm_world_slot_of_uid(unsigned short uid);
unsigned short zm_world_add_extra(unsigned char stage, unsigned char room, unsigned char id,
                                  unsigned char behavior, short x, short y, short z, short angle,
                                  bool directorControlled = false, short initialHealth = 0x7FFF);
int  zm_world_room_extra_count(unsigned char stage, unsigned char room, bool slots = false);
int  zm_world_room_extra_ids(unsigned char stage, unsigned char room, unsigned char* ids, int max,
                             short* xz = NULL, unsigned short* uids = NULL);   // uid 0: busy (feeding, possessed)
unsigned short zm_world_move_extra(unsigned char stage, unsigned char room, unsigned short uid,
                                   unsigned char toStage, unsigned char toRoom,
                                   short x, short y, short z, short angle);
bool zm_world_missing_extra(unsigned short* uid, unsigned char* id, unsigned char* behavior,
                            short* x, short* y, short* z, short* angle,
                            const unsigned short* skip, int skipCount);
// A monster of any type into an enemy slot, as cmd_enemy_set would set one up
// (ZombieMode.cpp). Its own init runs on its first update. NULL if the slot
// is taken.
Entity* zm_spawn_monster(int slot, unsigned char id, unsigned char behavior,
                         int x, int y, int z, short angle);
void zm_world_apply_remote(const short* args, int src);
// Director-only persistent ownership; a timer/room handoff cannot release it.
void zm_world_control(unsigned char stage, unsigned char room, unsigned short uid,
                      unsigned char id, bool controlled);
void zm_world_control_entity(const Entity* e, bool controlled);
bool zm_world_director_controlled(const Entity* e);
bool zombie_mode_room_pair_busy(void);
bool zm_world_departed(unsigned short uid, unsigned char id);
void zm_shared_remove_departed(unsigned short uid, unsigned char id);
int  zm_world_pack_flags(unsigned char* out);
void zm_world_merge_flags(const unsigned char* in);
// Story flags, sent as changes (ZM_EV_STORY): reset at a new game, watched on a
// survivor's copy each frame, applied from the other copies.
void zm_world_story_reset(void);
void zm_world_story_watch(void);
void zm_world_story_rebase(void);
void zm_world_story_apply(const short* args);
// The shared item box: emptied at a new game, transfers approved by the host.
void zm_world_box_reset(void);
void zm_box_reset(void);
void zm_box_take(const short* args, int src);
void zm_box_checkpoint(int player, unsigned short token);
unsigned int zm_box_snapshot(void* slots);
void zm_box_snapshot_take(unsigned int revision, const void* slots);
// Possessable monsters (zombies, Cerberus, Hunter, Chimera) that would be
// standing in (stage, room) if it were entered now: its
// enemy_set records less the dead, at their roster positions. The first is
// written to *first (when non-NULL). -1 when the room has no RDT.
struct ZmJumpTarget { unsigned char slot, id; unsigned short uid; short x, y, z, angle; };
int  zm_world_room_zombies(unsigned char stage, unsigned char room, ZmJumpTarget* first);

// ---- The possessed zombie (ZombieMode.cpp) ----
bool zm_zombie_alive(void);
// ZM_NET_OFF / ZM_NET_ZOMBIE / ZM_NET_SURVIVOR for the current game.
int  zm_game_role(void);
// This copy keeps the match - the clock, the outcome, the director's points
// and the arbitration of shared items: single player, or the host. In an AI
// director's game the host's game role is ZM_NET_SURVIVOR (its player is a
// survivor in seat 0) while it still arbitrates.
bool zm_match_authority(void);
// A survivor seat as this copy knows it: another player's last STATE, or this
// copy's own survivor (built from its player now - the host's, in an AI
// director's game). NULL for a free seat, the human director's, or one not in.
const ZmNetPeerState* zm_seat_state(int player);
// The other survivors' stand-ins: seat n in enemy slot ZM_SURVIVOR_SLOT_BASE + n
// (seat 0 only in an AI director's game). Monsters use the slots below.
#define ZM_SURVIVOR_SLOT_BASE  26
#define ZM_FIRST_SURVIVOR_SLOT ZM_SURVIVOR_SLOT_BASE
// Pose an entity from the other copy's skeleton.
void zm_apply_net_pose(Entity* e, const ZmNetPeerState* p);
void zm_apply_pose(Entity* e, const ZmNetPose* pose);
// A room event is walking the hidden player and the zombie follows it.
bool zm_zombie_riding(void);
void zm_zombie_ride_follow(int x, int y, int z, short angle, bool moving);
void zm_zombie_ride_end(void);

// ---- The survivor (ZombieSurvivor.cpp) ----
// True while the survivor is the loaded room's real player entity.
bool zm_survivor_present(void);
bool zm_survivor_location(unsigned char* stage, unsigned char* room);
// Session start: put the survivor in its first room.
void zm_survivor_new_game(void);
// Multiplayer, this copy is a survivor: start it in the main hall (its
// corner of the survivors' triangle) with the survivor's kit.
void zm_survivor_start_as_player(void);
// Where the survivors face at the start (the back of the main hall).
void zm_hall_back_door(int* x, int* z);
// Room hooks.
void zm_survivor_room_loaded(void);        // after the zombie spawned (room_set)
// The door stun (ZombieMode.cpp): a survivor came into the loaded room.
void zm_stun_room(const char* who);
void zm_survivor_zombie_leaving(void);     // before the outgoing room is torn down
void zm_survivor_note_zombie_door(void);   // the zombie is about to try a door
// game_loop, in place of the player's own update.
void zm_survivor_frame(void);
// Put the (parked) player entity where the zombie stands, ready for a room
// event to drive it.
void zm_survivor_begin_ride(const Entity* zombie);
// Around update_entities: hide a riding stand-in from the enemies.
void zm_survivor_enemies_begin(void);
void zm_survivor_enemies_end(void);
// game_loop, after the scene: where the survivor is, while it is away.
void zm_survivor_draw_hud(void);
// The survivor walked into a door: it leaves (true = transition swallowed).
bool zm_survivor_take_door(const unsigned char* record);
// Button-mash strength while held by a zombie (reduce_attack_time_by_btn_press).
char zm_survivor_mash(void);

// Every survivor's whereabouts, for the director's HUD and map: the remote
// players' (multiplayer) or the AI's (single player). ZombieMode.cpp.
struct ZmSurvivorInfo {
    unsigned char stage, room;
    int           character;     // ZM_CHAR_*
    bool          dead;
    short         health;
    int           player;        // seat (0 the host's, in an AI director's game)
};
int  zm_survivor_list(ZmSurvivorInfo* out, int max);
// Doorway reinforcements: host purchases, loaded room owner validates geometry.
unsigned char zm_survivor_entrance_door(void);
int zm_room_owner_here(void);
void zm_reinforce_notice(const char* text);
void zm_reinforce_reset(void);
bool zm_reinforce_request(unsigned char stage, unsigned char room, unsigned char id,
                          char* why, int whyLen);
void zm_reinforce_take(const short* args, int src);
void zm_reinforce_frame(void);
bool zm_reinforce_hold(const Entity* e);

// Three monster spawn spots per mansion room (ZombieSpawnSpots.cpp,
// generated by tools/gen_spawn_spots.py): x, y, z. `cap`: how many monsters
// the room holds at the start of a game, by its floor area (ZombieEconomy.cpp).
// A cap set by hand (tools/gen_spawn_spots.py CAP_OVERRIDES): it does not
// grow with the game's time (zm_econ_room_cap).
#define ZM_SPAWN_CAP_FIXED 0x80
struct ZmSpawnSpots {
    unsigned char stage, room;
    unsigned char cap;          // | ZM_SPAWN_CAP_FIXED
    short         spot[3][3];
};
extern const ZmSpawnSpots g_zmSpawnSpots[];
extern const int g_zmSpawnSpotCount;
// The AI director's finer spots (same tool): up to ZM_PLACE_SPOTS per room,
// spread over its floor, off the steps and clear of the doorways. `open`: the
// floor cells in the 7 x 7 cell square around the spot (1..49) - low in a
// corridor or between furniture. `y`: the room's common floor height.
#define ZM_PLACE_SPOTS 16
struct ZmPlaceSpot { short x, z; unsigned char open; };
struct ZmPlaceSpots {
    unsigned char stage, room, count;
    short         y;
    ZmPlaceSpot   spot[ZM_PLACE_SPOTS];
};
extern const ZmPlaceSpots g_zmPlaceSpots[];
extern const int g_zmPlaceSpotCount;

// The director's points (ZombieEconomy.cpp): prices, the Yawns beaten that
// unlock each monster, and the rooms' monster caps.
void zm_econ_new_game(bool on);
bool zm_econ_on(void);
int  zm_econ_points(void);
int  zm_econ_cost(unsigned char id);
int  zm_econ_unlock_bosses(unsigned char id);   // Yawns to beat first; normal rules, without debug bypass
int  zm_econ_locked(unsigned char id);          // Yawns still to beat for it: 0 now
int  zm_econ_room_cap(unsigned char stage, unsigned char room);
int  zm_econ_monster_slots(unsigned char id);
bool zm_econ_room_fits(unsigned char stage, unsigned char room, unsigned char id);
bool zm_econ_can_place(unsigned char stage, unsigned char room, unsigned char id,
                       const char* name, char* why, int whyLen);
void zm_econ_pay(unsigned char id);
void zm_econ_refund(unsigned char id, unsigned short uid);
void zm_econ_after_update(const Entity* e, int slot, unsigned short uid);
void zm_econ_room_reset(void);
void zm_econ_hit(void);
void zm_econ_monster_hit(void);
void zm_econ_draw(void);
struct ZmEconStats {
    int spent, earned;           // points (earned: income, hit bonuses and refunds)
    int placed, trapsSet, lost;  // monsters bought, traps set, monsters that died
    int survivorHits;
};
void zm_econ_stats(ZmEconStats* out);

// The AI director (ZombieDirectorAI.cpp) runs only in AI DIRECTOR matches.
// [Mods] AiDirector sets the lobby's default difficulty.
// _level: 0 off for this copy, else 1 easy .. 5 hell on earth. The economy scales
// the income and starting points (_income_pct) by it.
void zm_ai_new_game(void);
void zm_ai_frame(void);
void zm_ai_draw(void);
int  zm_ai_level(void);
int  zm_ai_income_pct(void);
// A lobby "AI DIRECTOR" game, on its host: the AI plays the director there
// while the host's own player is the survivor in seat 0.
bool zm_ai_hosted(void);
const char* zm_ai_level_name(int level);   // "EASY" .. "HELL ON EARTH"
// ZombieMode.cpp: the map placement's rules and the placement itself.
bool zm_director_place_allowed(unsigned char stage, unsigned char room, unsigned char id);
// The AI's unseen walk: may a monster `id` come into (stage, room) - the
// placement rules less the price and unlock - and does any copy have the room
// loaded or a player in it (then nothing may move in or out unseen)?
bool zm_director_move_allowed(unsigned char stage, unsigned char room, unsigned char id);
bool zm_room_watched(unsigned char stage, unsigned char room);
// `at`: { x, y, z, angle } for a room that is not loaded (the AI's spot), or
// NULL for the map's own choice (zm_remote_spot).
bool zm_director_place_ai(unsigned char stage, unsigned char room, unsigned char id, const char* name,
                          const short* at = NULL);

// The end of a match (ZombieStats.cpp): how it ended, each player's numbers
// (kept on its own copy, sent with ZM_EV_STATS), and the screen every copy
// ends on.
enum { ZM_END_ESCAPE = 0, ZM_END_TIME = 1, ZM_END_ALL_DEAD = 2, ZM_END_CONNECTION = 3,
       ZM_END_UNSOLVABLE = 4 }; // connection is local only
void zm_stats_new_game(void);
void zm_stats_frame(unsigned int elapsedMs);
void zm_stats_match_over(int reason, int winner, unsigned int elapsedMs);
void zm_stats_take(const short* a, int src);
void zm_stats_draw(unsigned int sinceMs);
bool zm_stats_prompt_up(unsigned int sinceMs);
// ZombieMode.cpp: ms since every survivor came into the game (-1 while they
// are still choosing; single player: since the start), and the live monsters
// of a room (the loaded one's entities, else the roster's waiting extras).
int  zm_survivors_in_ms(void);
int  zm_room_monster_count(unsigned char stage, unsigned char room);
int  zm_room_monster_slots(unsigned char stage, unsigned char room);
// The director's traps (ZombieTraps.cpp). On the map's list beside the monster
// types, under ids no entity uses.
#define ZM_TRAP_FIRST      0xF0
#define ZM_TRAP_LOCK_DOORS 0xF0      // the room's doors held shut for survivors
void zm_trap_new_game(void);
bool zm_is_trap_id(unsigned char id);
int  zm_trap_cooldown_ms(unsigned char id);
bool zm_trap_room_locked(unsigned char stage, unsigned char room, unsigned int* leftMs);
bool zm_trap_door_locked(unsigned char stage, unsigned char room, unsigned char dest,
                         unsigned char flags0B, unsigned int* leftMs);
bool zm_trap_place(unsigned char stage, unsigned char room, unsigned char id, const char* name,
                   char* why, int whyLen);
bool zm_trap_take(const short* a);
// ZombieWorld.cpp: a monster made by zm_spawn_monster (its first init is the
// one the naked zombie's extra health goes on), and the loaded room's
// roster extras that have no entity yet.
void zm_world_note_spawn(int slot);
int  zm_world_room_unspawned(bool slots = false);

// The mod's text, in place, from ASCII to what PrintText8x14 draws. Its
// 8x14 sheet is the game's own character set, laid out so a byte draws glyph
// byte - 36: letters, digits, ':' ';' '?' and space come out as ASCII, the
// rest do not ('<' drew ',', '>' '!', '-' a cross, '.' a box). Those are
// moved to their glyphs; '>' becomes the sheet's arrow (a cursor), and
// '<', '/' and '%', which the sheet does not have, a space / a dash.
// ZombieMode.cpp.
void zm_text_encode(char* s);

// A dead survivor watching the living ones (ZombieSpectate.cpp).
void zm_spec_new_game(void);
bool zm_revive_input(void);
void zm_revive_cancel(void);
void zm_revive_take(const short* a, int src);
bool zm_revive_host_claim(int target, int health, int src);
void zm_revive_draw(void);
void zm_stats_revived(void);
void zm_spec_frame(void);                 // zombie_mode_net_frame, before the STATE send
bool zm_spec_input(void);                 // zombie_mode_survivor_input: true while it has the pad
bool zm_spec_away(void);                  // left its corpse's room: in no room for the others
const ZmNetPeerState* zm_spec_corpse(void); // the watching survivor's body (stage, room, spot), else NULL
bool zm_spec_player_frozen(void);         // no animation, collision or shadow while watching
void zm_spec_room_exit(void);             // zombie_mode_room_exit
bool zm_spec_jump_record(const unsigned char* record);
bool zm_spec_camera_update(void);         // use the watched survivor's camera
const VECTOR* zm_spec_camera_target(void);
bool zm_spec_draw(void);                  // its HUD; false before the watching has begun
// ZombieMode.cpp helpers it uses.
void zm_fix_camera_at(const int* t, int slot);
// An action-press "examine" event: control, close-up, message, cut back - no
// entity, event, door or item command (ZombieMode.cpp, for ZombieMessages.cpp).
bool zm_evt_is_examine(int script);
void zm_draw_centered(const char* text, short y, unsigned char color);
void zm_clock_draw(void);

// ZombieMap.cpp - the director's map overlay
int  zm_map_area_of(unsigned char stage, unsigned char room);
bool zm_map_is_open(void);
void zm_map_toggle(void);
void zm_map_input(unsigned int rawPressed);
// The room the director picked to jump to (action on a zombie room); true
// once, then the map is closed.
bool zm_map_take_jump(unsigned char* stage, unsigned char* room);
// A monster the director placed from the map (aim on a room): once.
bool zm_map_take_place(unsigned char* stage, unsigned char* room, unsigned char* id, const char** name);
void zm_map_draw(void);
// Richard's radio: the map to look at only (no placing, no jumps).
void zm_map_set_read_only(bool on);
// The route map: look-only, showing this game's scenario. `level`: what of
// it (ZM_ROUTE_DOORS the key-locked doors; ZM_ROUTE_KEYS also where the keys
// and crests lie - the survivors'; ZM_ROUTE_ALL also every new pickup - the
// director's). `monsters`: the monster counts (Richard's radio); `players`:
// this survivor's room and the others' (in the game). `status` and `help`
// are the caller's two bottom lines.
enum { ZM_ROUTE_DOORS = 0, ZM_ROUTE_KEYS = 1, ZM_ROUTE_ALL = 2 };
void zm_map_set_route(int level, bool monsters, bool players, const char* status, const char* help);
// A popup menu over the route map (the pregame lobby's), until the next
// zm_map_set_route. `enabled` may be NULL (all on).
void zm_map_set_menu(const char* const* items, const bool* enabled, int count, int sel);

// Title guide shares the lobby character-preview scene. Negative closes it.
void zm_guide_profile(int survivor, int monster);

void zm_guide_background(bool open); // restore the title image when closing

// The CRT's content rectangle leaves space between text/maps and the bezel.
inline int zm_setup_x(int x) { return 16 + x * 9 / 10; }
inline int zm_setup_y(int y) { return 16 + y * 13 / 15; }
void zm_setup_text(int x, int y, unsigned char color, const char* text);
void zm_map_set_setup(bool on); // inset the pregame map; gameplay maps keep their layout
