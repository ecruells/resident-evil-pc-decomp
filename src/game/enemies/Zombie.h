#pragma once
#include "../../Globals.h"

// ============================================================================
// Zombie enemy type - Resident Evil 1
//
// Enemy IDs: 0 (standard zombie), 1 (naked zombie)
//
// Original addresses from Ghidra:
//   zombie_update          @ 0x004338c0
//   zombie_init            @ 0x00433440
//   zombie_state_check     @ 0x00433ae0
//   zombie_states_table    @ 0x004bb2c8
//   zombie_behavior_tbl    @ 0x004bb280
//   zombie_health_tbl      @ 0x004bb290
//   field_0xbd_table       @ 0x004bb2a0
//   field_0x188_tbl        @ 0x004bb2b0
// ============================================================================

// Enemy ID for zombies
#define ENEMY_ID_ZOMBIE        0
#define ENEMY_ID_NAKED_ZOMBIE  1

// PS1 angle encoding: full circle = 0x1000 (4096), 180° = 0x800 (2048)
#define ANGLE_FULL_CIRCLE       0x1000
#define ANGLE_HALF_CIRCLE       0x0800
#define ANGLE_SEMI_TOLERANCE    0x0801  // 180° + 1, used as boundary in turn_toward_target

// Enemy type IDs used by enemies_update_functions_tbl
// These are the entity->id values set by cmd_omodel_set in SCD scripts.
enum EnemyType {
    ENEMY_ZOMBIE           = 0,   // zombie_update
    ENEMY_ZOMBIE_NAKED     = 1,   // zombie_update (naked variant)
    ENEMY_CERBERUS         = 2,   // 0x00497fb0 - dog
    ENEMY_CROW             = 3,   // 0x00478310 - crow
    ENEMY_SPIDER           = 4,   // 0x0044f300 - spider
    ENEMY_5                = 5,   // 0x0042e520
    ENEMY_HUNTER           = 6,   // hunter_update
    ENEMY_BEE              = 7,   // 0x0048daf0 - bee
    ENEMY_WEB_SPINNER      = 8,   // 0x00464d10 - web spinner (big spider)
    ENEMY_9                = 9,   // 0x00438a70
    ENEMY_ADDER            = 10,  // adder_update - snake
    ENEMY_NEPTUNE          = 11,  // neptune_update - shark
    ENEMY_TYRANT           = 12,  // tyrant_update
    ENEMY_YAWN             = 13,  // yawn_update - giant snake
    ENEMY_PLANT42_ROOTS    = 14,  // plant42_roots_update
    ENEMY_MONSTER_PLANT    = 15,  // monster_plant_update
    ENEMY_TYRANT_2         = 16,  // tyrant_update (variant)
    ENEMY_17               = 17,  // zombie_update (variant)
    ENEMY_YAWN_2           = 18,  // yawn_update (variant)
    ENEMY_CHIMERA          = 19,  // 0x00443640 - chimera
    ENEMY_20               = 20,  // 0x00427330
    ENEMY_BLACK_TIGER      = 21,  // 0x0040b760 - black tiger (giant spider)
    ENEMY_GENERIC          = 22,  // 0x0046acf0 - generic/simple enemy handler
    ENEMY_COUNT            = 32
};

// Zombie states (entity->state at 0x84)
enum ZombieState {
    ZOMBIE_STATE_INIT           = 0,  // zombie_init - one-time init
    ZOMBIE_STATE_IDLE           = 1,  // zombie_state_check - main idle/behavior dispatch
    ZOMBIE_STATE_DAMAGED        = 2,  // zombie_damaged - hit reaction
    ZOMBIE_STATE_DIE            = 3,  // zombie_die - death sequence
    ZOMBIE_STATE_DEAD_ANIM      = 4,  // (0x004342d0) - dead animation
    ZOMBIE_STATE_ATTACK         = 5,  // zombie_attack - attacking player
    ZOMBIE_STATE_6              = 6,  // (unused - NULL in table)
    ZOMBIE_STATE_7              = 7,  // (unused - NULL in table)
    ZOMBIE_STATE_ACTION_UPDATE  = 8,  // zombie_action_update - action dispatch
    ZOMBIE_STATE_9              = 9,  // (unused - NULL in table)
    ZOMBIE_STATE_CHASE          = 10, // zombie_chase_player - chasing
    ZOMBIE_STATE_CHASE2         = 11, // zombie_chase_player - chasing (mirror)
    ZOMBIE_STATE_PUSHED_BACK    = 12, // zombie_pushed_back - knockback
    ZOMBIE_STATE_PUSHED_BACK2   = 13, // zombie_pushed_back - knockback (mirror)
    ZOMBIE_STATE_RANDOM_CHASE   = 14, // zombie_random_chase - staggered chase
    ZOMBIE_STATE_EATING         = 15, // zombie_eating - eating animation
    ZOMBIE_STATE_COUNT          = 16
};

// Zombie behavior types (entity->behavior_flags & 0x0F, used to index behavior table)
enum ZombieBehavior {
    ZOMBIE_BEH_NORMAL           = 0,
    ZOMBIE_BEH_1                = 1,
    ZOMBIE_BEH_2                = 2,  // laying down
    ZOMBIE_BEH_3                = 3,
    ZOMBIE_BEH_4                = 4,
    ZOMBIE_BEH_5                = 5,  // laying down eating
    ZOMBIE_BEH_6                = 6,  // dead (laying down, doing nothing)
    ZOMBIE_BEH_7                = 7,  // laying down eating
    ZOMBIE_BEH_8                = 8,
    ZOMBIE_BEH_9                = 9,
    ZOMBIE_BEH_10               = 10, // lying on floor
};

// Behavior flags (entity->behavior_flags bits)
#define ZOMBIE_FLAG_LAYING_DOWN      0x02  // bit 1: zombie is on the ground
#define ZOMBIE_FLAG_INACTIVE         0x04  // bit 2: disabled (dead on floor)
#define ZOMBIE_FLAG_VOMITING         0x40  // bit 6: vomiting state
#define ZOMBIE_FLAG_SCD_CONTROLLED   0x80  // bit 7: SCD event is controlling zombie

// Status flags (entity->status_flags bits)
#define ENTITY_STATUS_ACTIVE         0x01  // bit 0: entity is active/visible
#define ENTITY_STATUS_DEAD           0x08  // bit 3: entity is dead
#define ENTITY_STATUS_PLAYER_ABOVE   0x20  // bit 5: player above (vertical) / in visual range (distance)
#define ENTITY_STATUS_PLAYER_BELOW   0x80  // bit 7: player below (vertical) / in alert range (distance)
#define ENTITY_STATUS_ALIGNED        0x40  // bit 6: entity is aligned with player

// zombie_states_table @ 0x004bb2c8 - state function pointers indexed by entity->state
extern void* zombie_states_table[ZOMBIE_STATE_COUNT];

// enemies_update_functions_tbl @ 0x004d3c90 - enemy type dispatch table
// Indexed by entity->id (enemy type), holds per-type update function pointers.
// Entry 0 and 1 = zombie_update
extern void* enemies_update_functions_tbl[32];

// zombie_behavior_tbl @ 0x004bb280 - behavior dispatch table indexed by behavior_flags & 0x0F
extern void* zombie_behavior_tbl[16];

// zombie_health_tbl @ 0x004bb290 - base health values per random index
extern const unsigned char zombie_health_tbl[16];

// field_0xbd_table @ 0x004bb2a0 - initial animation ID per behavior type
extern const unsigned char zombie_anim_id_tbl[16];

// field_0x188_tbl @ 0x004bb2b0 - behavior modifier table
extern const unsigned char zombie_behavior_mod_tbl[32];

// Zombie state handler functions
extern void zombie_init(void);            // 0x00433440
extern void zombie_state_check(void);     // 0x00433ae0
extern void zombie_damaged(void);         // 0x00433db0
extern void zombie_die(void);             // 0x004340e0
extern void zombie_dead_animation(void);  // 0x004342d0
extern void zombie_attack(void);          // 0x004342e0
extern void zombie_action_update(void);   // 0x00454ab0
extern void zombie_chase_player(void);    // 0x00433bb0
extern void zombie_pushed_back(void);     // 0x00433c60
extern void zombie_random_chase(void);    // 0x00433cf0
extern void zombie_eating(void);          // 0x00436690

// Zombie helper functions
extern void zombie_check_player_distance(void);  // 0x004345b0
extern void entity_check_visual_range(unsigned int range);  // 0x0043bfa0 - sets bit 0x20 if within range
extern void entity_check_alert_range(unsigned int range);   // 0x0043bfe0 - sets bit 0x80 if within range

// Dependencies (stubs for now)
extern void SetEntityScaHitData(Entity* ent);         // 0x0041b2c0
extern unsigned int ResolveEntityScaCollision(Entity* a, Entity* b); // 0x0041b0a0
extern unsigned int HandleEnemyPlayerCollisions(void);         // 0x00489e10
extern unsigned char check_room_collision(VECTOR* pos, short radius); // 0x0047d310
extern unsigned char FUN_0047d6f0(short* a, short* b); // 0x0047d6f0 - floor/boundary check
extern void FUN_00437d20(int jointData, int mode);     // 0x00437d20 - splatter/blood effect
extern void FUN_00456810(VECTOR* pos, void* vel, int param3, short angle); // 0x00456810 - physics add velocity
extern void FUN_004565f0(SVECTOR* a, SVECTOR* b, int c, int d); // 0x004565f0 - SCA init helper
extern void set_next_entity_data_buffer(int count);     // 0x00488f90
extern unsigned char FUN_0048bd00(void* light, unsigned char param2, int param3); // 0x0048bd00 - lighting check
extern void FUN_0048bda0(void);                        // 0x0048bda0 - lighting response
extern void FUN_0048c0d0(void);                        // 0x0048c0d0 - pre-flip setup
extern void FlipSprite(int light, MATRIX* out, unsigned char param3, int param4); // 0x00460610
extern void Matrix_MulMatrix(MATRIX* a, MATRIX* b);    // 0x0040a2e0
