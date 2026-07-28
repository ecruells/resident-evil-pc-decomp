// Zombie.cpp - Zombie enemy type update logic
// All functions decompiled from Ghidra with original addresses.
// Enemy IDs: 0 (standard zombie), 1 (naked zombie)
#include "Zombie.h"
#include <cstdlib>

// Forward declarations for cross-TU functions
extern int is_entity_in_switch_zone(VECTOR* pos, void* zoneData);
extern void ResetJointTransforms(void);
extern void blood_splatter_physics(int jointData, short gravityStep);
extern void entity_add_fade_sprite(VECTOR* pos, short* velocity, short yOffset, short angle);
extern void SetRotAndTransMatrix(MATRIX* m);

// ============================================================================
// Forward declarations for zombie state/behavior functions
// (defined later in this file; needed for the dispatch tables below)
// ============================================================================
static void zombie_idle(void);              // 0x004349d0
static void zombie_slow_walk(void);         // 0x00434cd0
static void zombie_idling(void);             // 0x00434730
static void zombie_walk2(void);              // 0x00434750

// ============================================================================
// Data tables (original addresses from Ghidra)
// ============================================================================

// enemies_update_functions_tbl @ 0x004d3c90
// Per-enemy-type update function dispatch table (32 entries).
// Entry 0, 1, 17 are zombie_update; the rest are not yet implemented.
void* enemies_update_functions_tbl[32] = {
    (void*)zombie_update,  // [0]  zombie (standard)
    (void*)zombie_update,  // [1]  zombie (naked)
    NULL,                  // [2]  enemy type 2  (0x00497fb0)
    NULL,                  // [3]  enemy type 3  (0x00478310)
    NULL,                  // [4]  enemy type 4  (0x0044f300)
    NULL,                  // [5]  enemy type 5  (0x0042e520)
    NULL,                  // [6]  enemy type 6  (0x004161f0)
    NULL,                  // [7]  enemy type 7  (0x0048daf0)
    NULL,                  // [8]  enemy type 8  (0x00464d10)
    NULL,                  // [9]  enemy type 9  (0x00438a70)
    NULL,                  // [10] enemy type 10 (0x004727f0)
    NULL,                  // [11] enemy type 11 (0x0043d8d0)
    NULL,                  // [12] enemy type 12 (0x00421990)
    NULL,                  // [13] enemy type 13 (0x004051e0)
    NULL,                  // [14] enemy type 14 (0x0047e1c0)
    NULL,                  // [15] enemy type 15 (0x0045abb0)
    NULL,                  // [16] enemy type 16 (0x00421990)
    (void*)zombie_update,  // [17] zombie variant 3
    NULL,                  // [18] enemy type 18 (0x004051e0)
    NULL,                  // [19] enemy type 19 (0x00443640)
    NULL,                  // [20] enemy type 20 (0x00427330)
    NULL,                  // [21] enemy type 21 (0x0040b760)
    NULL,                  // [22] enemy type 22 (0x0046acf0)
    NULL,                  // [23] (0x0046acf0)
    NULL,                  // [24] (0x0046acf0)
    NULL,                  // [25] (0x0046acf0)
    NULL,                  // [26] (0x0046acf0)
    NULL,                  // [27] (0x0046acf0)
    NULL,                  // [28] (0x0046acf0)
    NULL,                  // [29] (0x0046acf0)
    NULL,                  // [30] (0x0046acf0)
    NULL                   // [31] (0x0046acf0)
};

// zombie_states_table @ 0x004bb2c8
// State handler function pointers indexed by entity->state (0x84).
// State 0 = init, State 1 = main behavior dispatch, etc.
void* zombie_states_table[ZOMBIE_STATE_COUNT] = {
    (void*)zombie_init,            // [0]  init
    (void*)zombie_state_check,     // [1]  idle / behavior dispatch
    (void*)zombie_damaged,         // [2]  hit reaction
    (void*)zombie_die,             // [3]  death sequence
    (void*)zombie_dead_animation,  // [4]  dead animation
    (void*)zombie_attack,          // [5]  attacking player
    NULL,                          // [6]  unused
    NULL,                          // [7]  unused
    (void*)zombie_action_update,   // [8]  action dispatch
    NULL,                          // [9]  unused
    (void*)zombie_chase_player,    // [10] chasing player
    (void*)zombie_chase_player,    // [11] chasing player (mirror)
    (void*)zombie_pushed_back,     // [12] knockback
    (void*)zombie_pushed_back,     // [13] knockback (mirror)
    (void*)zombie_random_chase,    // [14] staggered chase
    (void*)zombie_eating           // [15] eating corpse
};

// zombie_behavior_tbl @ 0x004bb280
// Behavior dispatch table indexed by behavior_flags & 0x0F
void* zombie_behavior_tbl[16] = {
    (void*)zombie_idle,       // [0] stand idle
    (void*)zombie_slow_walk,  // [1] slow walk
    (void*)zombie_slow_walk,  // [2] slow walk (laying down variant)
    (void*)zombie_idling,     // [3] idling
    (void*)zombie_walk2,      // [4] walk type 2
};

// zombie_health_tbl @ 0x004bb290 - base health for random calculation
const unsigned char zombie_health_tbl[16] = {
    200, 180, 200, 180, 200, 220, 200, 180,
    180, 200, 200, 220, 240, 200, 200, 240
};

// zombie_anim_id_tbl @ 0x004bb2a0 - initial animation ID per behavior type
const unsigned char zombie_anim_id_tbl[16] = {
    0, 2, 3, 4, 5, 6, 2, 3,
    4, 3, 2, 3, 2, 3, 2, 4
};

// zombie_behavior_mod_tbl @ 0x004bb2b0 - behavior modifier (drops/difficulty)
const unsigned char zombie_behavior_mod_tbl[32] = {
    3, 2, 3, 3, 2, 3, 4, 3,
    2, 3, 3, 4, 3, 2, 3, 3,
    3, 2, 3, 3, 3, 3, 2, 3,
    3, 2, 3, 4, 3, 4, 3, 2
};

// ============================================================================
// zombie_update @ 0x004338c0
// Main per-frame zombie update.
// Dispatches to the state handler, handles collision, movement, and
// camera switch zone checks.
// ============================================================================
void zombie_update(void)
{
    // 0x004338c0-0x00433906: local vars and joint pointer
    short local_8 = 0xfda8;     // -600
    short local_10 = 600;        // 600
    short local_6 = 0;
    short local_4 = 0;
    short local_2 = 0;
    short local_e = 0;
    short local_c = 0;
    short local_a = 0;

    int joint = (int)ENTITY->jointsStructs;

    // 0x00433906-0x00433914: Only update if message system allows (g_message_flags & 0x04)
    if ((g_message_flags & 0x0004) != 0) {
        // 0x0043390c-0x00433914: Dispatch to state handler
        void* stateHandler = zombie_states_table[ENTITY->state];
        if (stateHandler != NULL) {
            ((void(*)())stateHandler)();
        }

        // 0x0043391b-0x00433927: Mirror state fields (for animation blending)
        ENTITY->state_mirror = ENTITY->state;
        ENTITY->ignore_player_flag_mirror = ENTITY->ignore_player_flag;
        ENTITY->action_behavior_mirror = ENTITY->action_behavior;
        ENTITY->attack_behavior_mirror = ENTITY->action_state;

        // 0x0043392d-0x00433943: Decrement internal timer
        if (ENTITY->internal_timer != 0) {
            ENTITY->internal_timer--;
        }

        // 0x00433948-0x0043394f: Skip collision if in attack state (5)
        if (ENTITY->state != ZOMBIE_STATE_ATTACK) {
            // 0x00433955: Set up SCA hit data for collision
            SetEntityScaHitData(ENTITY);

            // 0x0043395e-0x0043396e: Resolve collision vs player
            ResolveEntityScaCollision((Entity*)&g_playerEntity, ENTITY);

            // 0x00433971: Handle enemy-player collision response
            HandleEnemyPlayerCollisions();

            // 0x0043397b: Clear collision push flag (bit 3)
            ENTITY->collisionFlags &= ~0x08;

            // 0x00433987-0x00433a0f: Room collision check
            if ((ENTITY->behavior_flags & ZOMBIE_FLAG_LAYING_DOWN) == 0) {
                // 0x004339dc-0x00433a0f: Not laying down - standard collision
                unsigned char collisionResult = check_room_collision(
                    (VECTOR*)&ENTITY->scaMatrixData.localMatrix.t,
                    *(short*)(ENTITY->Sca_info + 10));
                ENTITY->dir_control_flags |= collisionResult;
                ((unsigned char*)&ENTITY->subpixel_pos_x)[2] = (unsigned char)g_tempVar;
            } else {
                // 0x0043398d-0x004339da: Laying down - also check floor
                unsigned char collisionResult = check_room_collision(
                    (VECTOR*)&ENTITY->scaMatrixData.localMatrix.t,
                    *(short*)(ENTITY->Sca_info + 10));
                ENTITY->dir_control_flags |= collisionResult;
                ((unsigned char*)&ENTITY->subpixel_pos_x)[2] = (unsigned char)g_tempVar;

                unsigned char floorResult = FUN_0047d6f0(&local_8, &local_10);
                ENTITY->dir_control_flags |= floorResult;
            }
        }

        // 0x00433a14-0x00433a2c: Splatter/blood effect (if splatter_flag active)
        if (ENTITY->splatter_flag != 0) {
            blood_splatter_physics(joint + 0xf8, 6);
        }
    }

    // 0x00433a2f-0x00433a4c: Camera switch zone check
    ENTITY->has_enter_switch_zone = (unsigned char)is_entity_in_switch_zone(
        (VECTOR*)&ENTITY->scaMatrixData.localMatrix.t,
        g_CurrentRdtDataTypePtr);

    // 0x00433a4f-0x00433a74: Apply push/velocity physics if visible
    if (ENTITY->has_enter_switch_zone != 0) {
        entity_add_fade_sprite(
            (VECTOR*)&ENTITY->scaMatrixData.localMatrix.t,
            (short*)&ENTITY->pushVelocity,
            0,
            *(unsigned short*)&ENTITY->angle);
    }

    // 0x00433a77-0x00433aca: Joint-based secondary collision (weapon/hand joint)
    joint = (int)ENTITY->jointsStructs;
    if (((*(unsigned char*)(joint + 0x1f0) & 4) != 0) &&
        (*(int*)(joint + 0x24c) == -100))
    {
        int jointVis = is_entity_in_switch_zone(
            (VECTOR*)(joint + 0x248),
            g_CurrentRdtDataTypePtr);
        if (jointVis != 0) {
            entity_add_fade_sprite(
                (VECTOR*)(joint + 0x248),
                (short*)&ENTITY->sca_data_ptr,
                0,
                *(unsigned short*)(joint + 0x1f6));
        }
    }
}

// ============================================================================
// zombie_init @ 0x00433440
// One-time initialization for a zombie entity.
// Sets up health, behavior, animation, and SCA collision data.
// Called as state 0 in the zombie state machine.
// ============================================================================
void zombie_init(void)
{
    // 0x00433440-0x0043352d: Initialize local behavior table
    // This table maps random seed to behavior modifier values.
    // Values for the second half (at offset 32) are for non-standard difficulty.
    unsigned char local_40[64];
    local_40[0x20] = 1; local_40[0x21] = 2; local_40[0x22] = 3; local_40[0x23] = 2;
    local_40[0x24] = 3; local_40[0x25] = 3; local_40[0x26] = 2; local_40[0x27] = 3;
    local_40[0x28] = 3; local_40[0x29] = 2; local_40[0x2a] = 2; local_40[0x2b] = 3;
    local_40[0x2c] = 2; local_40[0x2d] = 3; local_40[0x2e] = 2; local_40[0x2f] = 3;
    local_40[0x30] = 2; local_40[0x31] = 3; local_40[0x32] = 2; local_40[0x33] = 4;
    local_40[0x34] = 2; local_40[0x35] = 2; local_40[0x36] = 2; local_40[0x37] = 2;
    local_40[0x38] = 3; local_40[0x39] = 2; local_40[0x3a] = 2; local_40[0x3b] = 3;
    local_40[0x3c] = 2; local_40[0x3d] = 2; local_40[0x3e] = 2; local_40[0x3f] = 3;
    local_40[0] = 3;   local_40[1] = 2;   local_40[2] = 3;   local_40[3] = 3;
    local_40[4] = 2;   local_40[5] = 3;   local_40[6] = 4;   local_40[7] = 3;
    local_40[8] = 2;   local_40[9] = 3;   local_40[10] = 3;  local_40[0xb] = 4;
    local_40[0xc] = 3; local_40[0xd] = 2;  local_40[0xe] = 3; local_40[0xf] = 3;
    local_40[0x10] = 3; local_40[0x11] = 2; local_40[0x12] = 3; local_40[0x13] = 3;
    local_40[0x14] = 3; local_40[0x15] = 3; local_40[0x16] = 2; local_40[0x17] = 3;
    local_40[0x18] = 3; local_40[0x19] = 2; local_40[0x1a] = 3; local_40[0x1b] = 4;
    local_40[0x1c] = 3; local_40[0x1d] = 4; local_40[0x1e] = 3; local_40[0x1f] = 2;

    // 0x0043352d-0x0043353d: Set initial state to idle (1)
    ENTITY->state = ZOMBIE_STATE_IDLE;
    ENTITY->ignore_player_flag = 0;
    ENTITY->action_behavior = 0;
    ENTITY->action_state = 0;

    // 0x0043353d-0x00433582: Zero out SCA matrix data base fields
    *(int*)(&ENTITY->scaMatrixData) = 0;

    // 0x00433582-0x004335b5: Clear counters and flags
    ENTITY->pad_c0[1] = 0;
    ENTITY->action_ticks_counter = 0;
    ENTITY->pad_c5 = 0;
    ENTITY->death_timer = 0;
    ENTITY->hit_state = 0;

    // 0x004335b5-0x004335c2: Set SCA info pointer (collision data)
    ENTITY->Sca_info = (unsigned int)(void*)0x004bb280; // PTR_WORD_004bb280

    // 0x004335c2-0x004335f0: Initialize SCA hit/joint data
    g_svecScratch.x = 0;
    g_svecScratch.y = 0;
    g_svecScratch.z = 0;

    ENTITY->sca_data_ptr = (unsigned int)g_loadDataDestPointer;
    set_next_entity_data_buffer(2);

    g_tempVar = (void*)&DAT_00ffff50;
    FUN_004565f0(&g_svecScratch, *(SVECTOR**)&ENTITY->sca_data_ptr, 400, 400);
    ResetJointTransforms();

    g_tempVar = (void*)&DAT_00808080;
    FUN_004565f0(&g_svecScratch, (SVECTOR*)&ENTITY->pushVelocity, 700, 900);

    // 0x004335f0-0x00433680: Calculate random health
    {
        unsigned char health_base;
        unsigned short health_variance;
        unsigned int randVal;

        if ((*((unsigned char*)&g_main_state_flags2 + 3) & 0x10) == 0) {
            randVal = rand();
            health_base = zombie_health_tbl[randVal & 0xF];
            randVal = rand();
            health_variance = (unsigned short)(randVal % 22);
        } else {
            health_base = zombie_health_tbl[g_RandSeed & 0xF];
            health_variance = g_RandSeed % 22;
        }
        ENTITY->health = health_base - health_variance;
    }

    // 0x00433680-0x004336a3: Laying down eating check
    if ((ENTITY->behavior_flags & 0xF) == ZOMBIE_BEH_5) {
        ENTITY->action_state = 2;
    }

    // 0x004336a3-0x004336b0: Naked zombie uses different SCA data
    if (ENTITY->id == ENEMY_ID_NAKED_ZOMBIE) {
        ENTITY->Sca_info = (unsigned int)(void*)0x004bb284; // PTR_DAT_004bb284
    }

    // 0x004336b0-0x00433700: Clear movement and splatter flags
    ENTITY->is_moving = 0;
    ENTITY->move_max_steps = 0;
    ENTITY->bob_speed = 0;               // 0x175
    ENTITY->reaction_timer = 0;          // 0x176
    ENTITY->splatter_flag = 0;
    ENTITY->dir_control_flags = 0;
    ((unsigned char*)&ENTITY->subpixel_pos_x)[1] = 0;  // 0x179
    ENTITY->action_speed = 0;

    // 0x00433700-0x0043374f: Set behavior type based on difficulty
    {
        unsigned char behVal;
        if (Flg_ck((int)g_PlayerFlags, 0x7b) == 0) {
            behVal = local_40[(g_RandSeed & 0x1F) + 32];
        } else {
            behVal = local_40[g_RandSeed & 0x1F];
        }
        ENTITY->hit_threshold = behVal;
    }

    // 0x0043374f-0x0043379f: Set misc behavior/timing values
    ENTITY->stagger_timer = zombie_behavior_mod_tbl[g_RandSeed & 0x1F];
    ((unsigned char*)&ENTITY->subpixel_pos_x)[0] = 0;  // 0x178
    ENTITY->behavior_step = 0;
    ENTITY->move_speed = 45;
    ENTITY->turn_speed = 24;
    ENTITY->action_counter = 0;
    ENTITY->internal_timer = 0;
    ENTITY->blend_counter = 0;

    // 0x0043379f-0x004337cf: Set initial animation ID
    ENTITY->animationId = zombie_anim_id_tbl[ENTITY->behavior_flags & 0xF];
    ENTITY->animation_frame_id = 0;
    ENTITY->timing_control = 0;

    // 0x004337cf-0x004337e5: Initialize joint animation
    Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);

    // 0x004337e5-0x00433818: Set laying down state if behavior bit 1 is set
    if ((ENTITY->behavior_flags & ZOMBIE_FLAG_LAYING_DOWN) != 0) {
        // Set scaMatrixData.localMatrix.m[1] to identity-like laying state
        *(unsigned char*)&ENTITY->scaMatrixData.localMatrix.m[1][0] = 1;
        *(unsigned char*)&ENTITY->scaMatrixData.localMatrix.m[1][1] = 0;
        *(unsigned char*)&ENTITY->scaMatrixData.localMatrix.m[1][2] = 0;
        *(unsigned char*)&ENTITY->scaMatrixData.localMatrix.m[1][3] = 0; // pad
    }

    // 0x00433818-0x0043387f: Dead zombie (behavior == 6: laying down, doing nothing)
    if ((ENTITY->behavior_flags & 0xF) == ZOMBIE_BEH_6) {
        ENTITY->status_flags |= (ENTITY_STATUS_ACTIVE | ENTITY_STATUS_DEAD);
        ENTITY->health = -1;          // 0xff, 0xff short
        ENTITY->state = ZOMBIE_STATE_DIE;
        ENTITY->ignore_player_flag = 1;
        ENTITY->action_behavior = 4;
        ENTITY->action_state = 0;
    }

    // 0x0043387f-0x004338b0: Lying-on-floor zombie (behavior == 10)
    if ((ENTITY->behavior_flags & 0xF) == ZOMBIE_BEH_10) {
        int jointPtr = (int)ENTITY->jointsStructs;
        ENTITY->status_flags |= 0x04;
        // Disable joints (set bit 0 to 0 at specific offsets)
        *(unsigned char*)(jointPtr + 0x45c) &= 0xFE;
        *(unsigned char*)(jointPtr + 0x4d8) &= 0xFE;
        *(unsigned char*)(jointPtr + 0x554) &= 0xFE;
        *(unsigned char*)(jointPtr + 0x5d0) &= 0xFE;
        *(unsigned char*)(jointPtr + 0x64c) &= 0xFE;
        *(unsigned char*)(jointPtr + 0x6c8) &= 0xFE;
    }

    // 0x004338b0-0x004338be: Vomiting state check
    if ((ENTITY->behavior_flags & ZOMBIE_FLAG_VOMITING) != 0) {
        ENTITY->state = ZOMBIE_STATE_ACTION_UPDATE;
    }
}

// ============================================================================
// zombie_state_check @ 0x00433ae0
// Main behavior dispatcher for idle zombies.
// Checks distance to player and determines whether to chase, idle, or wander.
// ============================================================================
void zombie_state_check(void)
{
    // 0x00433ae0-0x00433aea: Skip if SCD-controlled (bit 7)
    if ((ENTITY->behavior_flags & ZOMBIE_FLAG_SCD_CONTROLLED) != 0) {
        return;
    }

    // 0x00433af0-0x00433afd: Dispatch to behavior handler
    void* behaviorHandler = zombie_behavior_tbl[ENTITY->behavior_flags & 0x0F];
    if (behaviorHandler != NULL) {
        ((void(*)())behaviorHandler)();
    }

    // 0x00433afd-0x00433b06: Clear status flags (keep lower 5 bits)
    ENTITY->status_flags &= 0x1F;

    // 0x00433b06-0x00433b30: Check long-range player detection
    if (((ENTITY->behavior_flags & ZOMBIE_FLAG_LAYING_DOWN) == 0) &&
        ((ENTITY->action_speed & 0x80) == 0))
    {
        ENTITY->status_flags |= ENTITY_STATUS_ALIGNED;
        entity_check_alert_range(3000);
    }

    // 0x00433b2b-0x00433b35: Check medium-range player detection
    entity_check_visual_range(4500);

    // 0x00433b35-0x00433b73: Check player vertical position
    g_playerDisplacement =
        (int)g_playerEntity.scaMatrixData.localMatrix.t[1] -
        (int)ENTITY->scaMatrixData.localMatrix.t[1];

    if ((g_playerDisplacement < -100) || (g_playerDisplacement > 100)) {
        ENTITY->status_flags &= 0x1F;
        if (g_playerDisplacement < 0) {
            ENTITY->status_flags |= ENTITY_STATUS_PLAYER_ABOVE;
        } else {
            ENTITY->status_flags |= ENTITY_STATUS_PLAYER_BELOW;
        }
    }

    // 0x00433b73-0x00433b98: Eating zombies (behavior 5 or 7) check
    if ((ENTITY->behavior_flags == (ZOMBIE_FLAG_LAYING_DOWN | 0x05)) ||
        (ENTITY->behavior_flags == ZOMBIE_BEH_5))
    {
        ENTITY->status_flags &= 0x1F;
        entity_check_visual_range(4000);
    }

    // 0x00433b98-0x00433ba9: Follow-player flag check (behavior_step >= 4)
    if ((ENTITY->behavior_step & 0x04) != 0) {
        ENTITY->status_flags |= ENTITY_STATUS_ALIGNED;
    }
}

// ============================================================================
// zombie_damage_action_tbl @ 0x004bb31f
// Index: (hit_state & 7) + (laying_down ? 3 : 0) → action_behavior value
static const unsigned char zombie_damage_action_tbl[11] = {
    0, 0, 5, 0, 2, 0, 0, 4, 4, 0, 0
};

// Forward declarations for zombie state helper dependencies
static void zombie_dead_animation(void);
extern int turn_toward_target(VECTOR* pos, short angle_step);
extern unsigned char check_line_of_sight(SVECTOR* targetPos);
extern unsigned short getAngleTowardsTarget(int px, int pz);
extern void entity_update_player_distance(void);
extern void update_zombie_action(void);
extern unsigned int entity_pathfind_update(void);
extern void magnum_shot_pushback(void);
extern unsigned int entity_update_wander_turn(unsigned int movement_dist, unsigned char* control_flags, unsigned char* turn_counter, unsigned short angle_step, unsigned char turn_limit);
extern unsigned char checkAngularViewAndDistance(short fovHalfAngle, short maxDistance, VECTOR* targetPos);
extern void vectorMul3(VECTOR* a, VECTOR* b, VECTOR* dst);
extern void zombie_pushback_idle(void);
extern void zombie_pushback_stagger(void);
extern void zombie_pushback_action(void);
extern void zombie_check_special_weapon(void);    // 0x0043d8a0
extern void zombie_body_part_physics(unsigned char param);
extern void FUN_0045f970(int px, int pz, int* a, int* b);
extern unsigned int g_entity_bkp;
extern void* _ENTITY_SAVE;
extern int g_scaled_down_dist;
extern unsigned int entity_check_angular_los(short fovHalfAngle, VECTOR* pos);
extern void entity_rotate_toward_target(VECTOR* pos, unsigned short angleStep);
extern void Flg_on(int baseAddr, unsigned int bitIndex);
extern unsigned int ChkOutsideCell(SVECTOR* pos, SVECTOR* dir, int bx, int bz);
extern unsigned int room_collision_check_0047db90(VECTOR* vec, unsigned int boundaryResult);
extern int player_distance_z;
extern void snap_player_to_grab_position(void* player);
extern unsigned int is_facing_toward_entity(void* player);
extern void entity_apply_anim_vertex(void* entity, unsigned int animHeader, unsigned int animBase);
extern void joint_setup_attack_effect(int joint, unsigned char effectType, unsigned short timer, unsigned short frameMatch);
extern void joint_enable_special_effect(int joint, unsigned char a, int b, unsigned char c);
extern void BillboardSetColor(void* mat, int a, int b, void* color);
extern char reduce_attack_time_by_btn_press(void);

// Accessor for byte at entity+0x177 (high byte of reaction_timer used as attack countdown)
#define ATTACK_TIMER  (((unsigned char*)&ENTITY->reaction_timer)[1])
extern unsigned char FUN_0048ae00(int joint, VECTOR* pos, int radius, int playerPtr);

// Zombie damage behavior handlers (dispatched from zombie_damaged)
extern void zombie_falldown(void);    // 0x00436af0 - pushed to ground / falldown animation
extern void short_push_back(void);    // 0x00436c80 - short hit reaction / stagger backwards
extern void push_and_stagger(void);   // 0x00436f00 - push back and stagger

// ============================================================================
// zombie_damaged @ 0x00433db0
// Zombie hit reaction state. Dispatches damage behavior based on hit flags,
// increments hit counter, triggers falldown when threshold exceeded, or
// when stagger_timer expires during sustained damage.
// ============================================================================
void zombie_damaged(void)
{
    // 0x00433db0: Damage threshold lookup table (difficulty-dependent)
    unsigned char local_40[64];
    local_40[0x21] = 2; local_40[0x22] = 3; local_40[0x23] = 2; local_40[0x24] = 3;
    local_40[0x25] = 3; local_40[0x26] = 2; local_40[0x27] = 3; local_40[0x28] = 3;
    local_40[0x29] = 2; local_40[0x2a] = 2; local_40[0x2b] = 3; local_40[0x2c] = 2;
    local_40[0x2d] = 3; local_40[0x2e] = 2; local_40[0x2f] = 3; local_40[0x30] = 2;
    local_40[0x31] = 3; local_40[0x32] = 2; local_40[0x33] = 4; local_40[0x34] = 2;
    local_40[0x35] = 2; local_40[0x36] = 2; local_40[0x37] = 2; local_40[0x38] = 3;
    local_40[0x39] = 2; local_40[0x3a] = 2; local_40[0x3b] = 3; local_40[0x3c] = 2;
    local_40[0x3d] = 2; local_40[0x3e] = 2; local_40[0x3f] = 3;
    local_40[0]  = 3; local_40[1]  = 2; local_40[2]  = 3; local_40[3]  = 3;
    local_40[4]  = 2; local_40[5]  = 3; local_40[6]  = 4; local_40[7]  = 3;
    local_40[8]  = 2; local_40[9]  = 3; local_40[10] = 3; local_40[0xb]= 4;
    local_40[0xc]= 3; local_40[0xd]= 2; local_40[0xe]= 3; local_40[0xf]= 3;
    local_40[0x10]=3; local_40[0x11]=2; local_40[0x12]=3; local_40[0x13]=3;
    local_40[0x14]=3; local_40[0x20]=1; local_40[0x15]=3; local_40[0x16]=2;
    local_40[0x17]=3; local_40[0x18]=3; local_40[0x19]=2; local_40[0x1a]=3;
    local_40[0x1b]=4; local_40[0x1c]=3; local_40[0x1f]=2; local_40[0x1d]=4;
    local_40[0x1e]=3;

    if (ENTITY->ignore_player_flag == 0) {
        unsigned char behType = ENTITY->behavior_flags & 0x0F;

        // 0x00433dd0: If behavior_step bit 2 set, restore previous state
        if ((ENTITY->behavior_step & 0x04) != 0) {
            *(unsigned int*)(&ENTITY->state) = *(unsigned int*)(&ENTITY->state_mirror);
            ENTITY->hit_state = 0;
            return;
        }

        // 0x00433df8: If falldown flag (action_speed bit 7), transition to falldown
        if ((ENTITY->action_speed & 0x80) != 0) {
            ENTITY->state      = ZOMBIE_STATE_DIE;
            ENTITY->action_state = 3;
            ENTITY->ignore_player_flag = 1;
            ENTITY->blend_counter = 0;
            ENTITY->hit_state = 1;
            if ((ENTITY->behavior_step & 1) == 0) {
                Snd_em(9);  // falldown moan
            }
            return;
        }

        // 0x00433e54: Increment hit counter if hit-with-bullet flag set
        if ((ENTITY->hit_state & 0x78) == 0x08) {
            ENTITY->action_speed++;
            if ((signed char)ENTITY->hit_threshold <= (signed char)ENTITY->action_speed) {
                if ((ENTITY->behavior_flags & ZOMBIE_FLAG_LAYING_DOWN) == 0) {
                    // hit threshold exceeded → trigger falldown
                    ENTITY->action_speed = 0x80;
                    if (Flg_ck((int)g_PlayerFlags, 0x7b) == 0)
                        ENTITY->hit_threshold = local_40[(g_RandSeed & 0x1F) + 32];
                    else
                        ENTITY->hit_threshold = local_40[g_RandSeed & 0x1F];
                }
            }
        }

        // 0x00433ecf: stagger_timer countdown during sustained damage
        if ((ENTITY->hit_state & 0x78) == 0x10
            && ENTITY->action_state == 0
            && --ENTITY->stagger_timer == 0
            && (ENTITY->behavior_flags & ZOMBIE_FLAG_LAYING_DOWN) == 0)
        {
            ENTITY->action_speed = 0x80;
        }

        // 0x00433f00: Determine damage behavior from table
        ENTITY->action_behavior = zombie_damage_action_tbl[
            (ENTITY->hit_state & 7) + ((ENTITY->behavior_flags & ZOMBIE_FLAG_LAYING_DOWN) ? 3 : 0)];

        // 0x00433f30: Adjust behavior based on hit direction
        if ((ENTITY->hit_state - 1) & 0x02) {
            unsigned int angle = turn_toward_target(
                (VECTOR*)g_playerEntity.scaMatrixData.localMatrix.t, 1024);
            ENTITY->action_behavior += ((angle >> 10) & 1);
        }

        // 0x00433f88: Eating zombie special case
        if ((behType == 7 || behType == 5) && ENTITY->animationId == 13) {
            ENTITY->action_behavior = 7;
            ENTITY->action_state = 1;
        }

        ENTITY->ignore_player_flag = 1;
    }

    // 0x00433fac: Dispatch to damage behavior handler
    unsigned char behavior = ENTITY->action_behavior;
    if (behavior < 5) {
        static void* zombie_damage_behavior_tbl[] = {
            (void*)zombie_falldown,   // [0] - falldown to ground
            (void*)short_push_back,   // [1] - short stagger back
            (void*)short_push_back,   // [2] - short stagger back
            (void*)push_and_stagger,  // [3] - push back and stagger
            (void*)push_and_stagger   // [4] - push back and stagger
        };
        if (zombie_damage_behavior_tbl[behavior] != NULL) {
            ((void(*)())zombie_damage_behavior_tbl[behavior])();
        }
    }
}

// ============================================================================
// zombie_die @ 0x004340e0
// Zombie death sequence state machine. Handles fall-back animation,
// headshot/magnum pushback, and laying-down death.
// ============================================================================
void zombie_die(void)
{
    if (ENTITY->ignore_player_flag == 0) {
        ENTITY->ignore_player_flag = 1;
        ENTITY->action_behavior = 0;

        // 0x00434110: Determine death behavior
        if ((ENTITY->behavior_flags & ZOMBIE_FLAG_LAYING_DOWN)
            || (ENTITY->action_speed & 0x80)
            || (ENTITY->behavior_flags & 0x0F) == ZOMBIE_BEH_5)
        {
            ENTITY->action_behavior = 2;
        }

        // 0x00434151: Headshot death if hit from behind
        if ((ENTITY->hit_state & 7) == 4) {
            int angle = turn_toward_target(
                (VECTOR*)g_playerEntity.scaMatrixData.localMatrix.t, 1024);
            if ((short)angle == 0) {
                ENTITY->action_behavior = 1;
            }
        }

        // 0x0043419b: Magnum/explosive death if headshot joint flag set
        if (ENTITY->action_behavior == 1
            && (*(unsigned char*)((int)ENTITY->jointsStructs + 0xF8) & 0x40)
            && (g_RandSeed & 1))
        {
            ENTITY->action_behavior = 3;
        }

        ENTITY->behavior_step &= ~0x04;
        Flg_on((int)g_roomItemsFlags, ENTITY->death_event_id);  // 0x163 - SCD event room flag index
    }

    // 0x004341f0: Death behavior dispatch
    switch (ENTITY->action_behavior) {
    case 0:
        // 0x00434270: Standard death fall backward
        ENTITY->animationId = 8;
        ENTITY->move_speed_current = 20;
        if (ENTITY->animation_frame_id == 8 && ENTITY->timing_control == 1)
            Snd_em(1);
        if (ENTITY->animation_frame_id < 5)
            ENTITY->move_speed_current = (unsigned short)(ENTITY->move_speed_current + 90);
        zombie_dead_animation();
        Add_speedXZ(2048);
        return;
    case 1:
        // 0x00434228: Headshot death
        ENTITY->animationId = 10;
        ENTITY->move_speed_current = 20;
        if (ENTITY->animation_frame_id == 18 && ENTITY->timing_control == 1)
            Snd_em(0);
        if (ENTITY->animation_frame_id < 5)
            ENTITY->move_speed_current = (unsigned short)(ENTITY->move_speed_current + 90);
        zombie_dead_animation();
        Add_speedXZ(2048);
        return;
    case 2:
        // 0x00434246: Lay-down death (already on ground)
        ENTITY->animationId = 16;
        zombie_dead_animation();
        return;
    case 3:
        // 0x00434251: Magnum shot pushback
        magnum_shot_pushback();
        return;
    }
}

// ============================================================================
// zombie_dead_animation @ 0x004342d0
// No-op — the dead animation is handled by joint/sprite system directly.
// ============================================================================
static void zombie_dead_animation(void) { }

// ============================================================================
// Zombie attack data tables
// ============================================================================

// zombie_attack_animations @ 0x004bb4?? — attack animation IDs indexed by attacking_direction * 3
static const unsigned char zombie_attack_anim_tbl[18] = {
    0x0B, 0x0C, 0x0D,   // facing player (0)
    0x14, 0x13, 0x14,   // laying down front (1)
    0x16, 0x17, 0x18    // laying down back (2)
};

// zombie_attack_anim__player @ — player reaction animation IDs
static const unsigned char zombie_attack_player_anim_tbl[9] = {
    0x04, 0x05, 0x14,   // facing player
    0x05, 0x06, 0x15,   // laying down front
    0x07, 0x08, 0x16    // laying down back
};

// attack_direction_relative @ — player angle offset per attack type
static const short attack_dir_offset_tbl[3] = { 0x800, 0, 0 };

// zombie_attack_frame_check @ — specific animation frames to trigger effects
static const unsigned char zombie_attack_keyframe_tbl[9] = {
    0, 0x0A, 0x1C,   // facing player
    0, 0x10, 0x10,   // laying down front
    0, 0x10, 0x10    // laying down back
};

// zombie_damage_values @ — damage per behavior type (easy difficulty)
static const unsigned char zombie_damage_easy_tbl[16] = {
    20, 35, 20, 35, 40, 35, 20, 35,
    45, 35, 20, 35, 20, 35, 20, 35
};

// zombie_damage_values_normal @ — damage per behavior type (normal difficulty)
static const unsigned char zombie_damage_normal_tbl[16] = {
    40, 35, 40, 40, 45, 35, 40, 40,
    45, 40, 40, 40, 40, 35, 40, 35
};

// ============================================================================
// zombie_attack @ 0x004342e0 (thunk) -> 0x00435200 (real)
// Full zombie attack FSM — 11 states controlling bite/grab/vomit attacks.
// Manages player reaction animation, damage application, head explosion,
// vomiting effects, and attack withdrawal.
// ============================================================================
void zombie_attack(void)
{
    char reduce = 0;  // declared before switch to avoid case-label initialization errors

    switch (ENTITY->action_state) {
    case 0:
        // ---- INIT: setup attack type ----
        ENTITY->animation_frame_id = 0;
        ENTITY->timing_control = 0;
        ENTITY->action_state = 1;
        ENTITY->blend_counter = 3;
        ENTITY->status_flags |= (ENTITY_STATUS_ACTIVE | ENTITY_STATUS_DEAD);
        Snd_em(4);                      // attack roar

        ENTITY->attacking_direction =
            ((ENTITY->behavior_flags & ZOMBIE_FLAG_LAYING_DOWN) ? 1 : 0)
            + (is_facing_toward_entity(&g_playerEntity) ? 0 : 0);

        ENTITY->animationId =
            zombie_attack_anim_tbl[ENTITY->attacking_direction * 3];
        g_playerEntity.attackAnim =
            zombie_attack_player_anim_tbl[ENTITY->attacking_direction * 3];
        g_playerEntity.attackDirection =
            attack_dir_offset_tbl[ENTITY->attacking_direction];

        ENTITY->hit_state = 1;
        ENTITY->animation_frame_id = 0;
        ENTITY->timing_control = 0;

        snap_player_to_grab_position(&g_playerEntity);
        g_playerEntity.isBeingAttackedFlag = 1;
        g_playerEntity.animationId = 5;
        g_playerEntity.animFrameId = 0;
        g_playerEntity.action_behavior = 0;
        g_playerEntity.action_state = 0;
        g_playerEntity.directionAngle = ENTITY->angle;
        break;

    case 1:
        // ---- WIND-UP: play attack wind-up animation ----
        entity_apply_anim_vertex(ENTITY, ENTITY->animHeader, ENTITY->animBase);
        ENTITY->action_state += (char)Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);
        break;

    case 2:
        // ---- BITE START: transition to damage loop ----
        ENTITY->action_state = 3;
        ENTITY->animationId = ENTITY->animationId + 1;
        ENTITY->timing_control = 0;
        ENTITY->action_ticks_counter = 0;
        ATTACK_TIMER = 105;
        break;

    case 3:
        // ---- DAMAGE LOOP: deal damage every 19 frames ----
        ENTITY->action_ticks_counter++;
        if ((short)ENTITY->action_ticks_counter % 19 == 0) {
            unsigned char damage;
            if (Flg_ck((int)g_PlayerFlags, 0x7b) == 0)
                damage = zombie_damage_easy_tbl[ENTITY->behavior_flags & 0x0F];
            else
                damage = zombie_damage_normal_tbl[ENTITY->behavior_flags & 0x0F];
            g_playerEntity.health -= damage;
            Snd_em(3);

            Effect_CreateBillboard(0, 0,
                g_playerEntity.directionAngle + 2048,
                (void*)g_deadMoveValue,
                (void*)((int)ENTITY->jointsStructs + 0x150), 0);

            if (g_playerEntity.health < 0
                && (ENTITY->attacking_direction & 2) != 0) {
                g_playerEntity.health = 1;
            }
        }

        entity_apply_anim_vertex(ENTITY, ENTITY->animHeader, ENTITY->animBase);
        Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);

        reduce = reduce_attack_time_by_btn_press();
        ATTACK_TIMER -= ((unsigned char)reduce + 1);

        if ((char)ATTACK_TIMER < 0) {
            ENTITY->action_state = 4;
            g_playerEntity.action_state = 3;
        }

        // Player died during attack
        if (g_playerEntity.health < 0) {
            ENTITY->state = ZOMBIE_STATE_IDLE;
            ENTITY->ignore_player_flag = 1;
            ENTITY->action_behavior = 5;
            ENTITY->action_state = 0;
            ENTITY->action_behavior +=
                (((int)ENTITY->behavior_flags & ZOMBIE_FLAG_LAYING_DOWN) ? -5 : 0);

            g_playerEntity.animationId = 1;
            g_playerEntity.action_state = 3;
            g_playerEntity.action_behavior = 200;
        }
        break;

    case 4:
        // ---- RELEASE: end attack or transition to vomit/headbite ----
        ENTITY->action_state = 5;
        ENTITY->animationId = ENTITY->animationId + 1;
        ENTITY->animation_frame_id = 0;
        ENTITY->timing_control = 0;
        ENTITY->blend_counter = 3;

        if ((ENTITY->behavior_flags & ZOMBIE_FLAG_LAYING_DOWN) == 0)
            break;  // -> state 5: normal withdrawal

        if ((g_playerEntity.id & 1) == 0 || ENTITY->attacking_direction != 2) {
            ENTITY->action_state = 6;   // -> head bite
            break;
        }
        ENTITY->action_state = 8;       // -> vomiting
        break;

    case 5:
        // ---- WITHDRAWAL: finish attack, return to damaged state ----
        entity_apply_anim_vertex(ENTITY, ENTITY->animHeader, ENTITY->animBase);
        if ((char)Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400) != 0) {
            ENTITY->state = ZOMBIE_STATE_DAMAGED;
            ENTITY->ignore_player_flag = 1;
            ENTITY->action_behavior = 6;
            ENTITY->action_state = 0;
            ENTITY->status_flags &= ~(ENTITY_STATUS_ACTIVE | ENTITY_STATUS_DEAD);
            ENTITY->hit_state = 1;
        }
        break;

    case 6: {
        // ---- HEAD BITE: grab player's head, apply effects ----
        entity_apply_anim_vertex(ENTITY, ENTITY->animHeader, ENTITY->animBase);
        ENTITY->action_state += (char)Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);

        if (zombie_attack_keyframe_tbl[ENTITY->attacking_direction * 3]
            == ENTITY->animation_frame_id)
        {
            ENTITY->action_state = 10;
            int jointPtr = (int)ENTITY->jointsStructs;

            joint_setup_attack_effect(jointPtr + 0xF8, 0x1E, 0, 3);

            VECTOR tmpPos = { 0, 0, 0, 0 };

            Effect_CreateBillboard(3, 0, 0, (void*)(jointPtr + 0x13C), &tmpPos, 0);
            tmpPos.x += 500;
            Effect_CreateBillboard(4, 0, 0x800, &ENTITY->scaMatrixData, &tmpPos, 0);
            Effect_CreateBillboard(4, 1, 0x5E8, &ENTITY->scaMatrixData, &tmpPos, 0);
            Effect_CreateBillboard(4, 2, 0x9F4, &ENTITY->scaMatrixData, &tmpPos, 0);
            Effect_CreateBillboard(4, 4, 0xB84, &ENTITY->scaMatrixData, &tmpPos, 0);
            Effect_CreateBillboard(4, 2, 0xDB8, &ENTITY->scaMatrixData, &tmpPos, 0);

            JointApplyColorTint((JointStruct*)(jointPtr + 0x174), 0x30, 0x80820, &DAT_00606060);
            JointApplyColorTint((JointStruct*)(jointPtr + 0x2E8), 0x30, 0x80820, &DAT_00606060);
            Snd_em(6);
        }
        break;
    }

    case 7:
        // ---- POST HEAD-EXPLOSION: enter dead-headless state ----
        BillboardSetColor((void*)&ENTITY->scaMatrixData, 1, 2, &DAT_00ffff50);
        Flg_on((int)g_roomItemsFlags, ENTITY->death_event_id);
        ENTITY->death_timer = 0x46;
        ENTITY->state = ZOMBIE_STATE_DIE;
        ENTITY->ignore_player_flag = 1;
        ENTITY->status_flags |= 0x0E;
        ENTITY->health = 0xFFFF;
        ENTITY->hit_state = 1;
        break;

    case 8: {
        // ---- VOMITING ATTACK: vomit on player ----
        entity_apply_anim_vertex(ENTITY, ENTITY->animHeader, ENTITY->animBase);
        {
            char looped = Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);
            ENTITY->action_state += looped;
        }

        {
            int jointPtr = (int)ENTITY->jointsStructs;
            if (zombie_attack_keyframe_tbl[ENTITY->attacking_direction * 3]
                == ENTITY->animation_frame_id)
            {
                unsigned char* jointFlag = (unsigned char*)(jointPtr + 0xF8);
                *jointFlag |= 0x88;
                JointApplyColorTint((JointStruct*)jointFlag, 0x30, 0x80820, &DAT_00606060);
                *(unsigned short*)(jointPtr + 0xFC) = 0xFED4;
                *(unsigned short*)(jointPtr + 0xFE) = 0xFA;
                *(unsigned char*)(jointPtr + 0xFA) = 0;
                *(unsigned char*)(jointPtr + 0xFB) = 0;

                VECTOR tmpPos = { 0, 0, 0, 0 };
                Effect_CreateBillboard(0, 0, 0, (void*)(jointPtr + 0x13C), &tmpPos, 0);
            }

            if ((*(unsigned char*)(jointPtr + 0xF8) & 0x40) != 0) {
                ENTITY->splatter_flag = 1;
                ENTITY->bob_speed = 0;
            }
        }

        if (g_playerEntity.animation_frame_id == 6) {
            Snd_em(7);
        }
        break;
    }

    case 9:
        // ---- POST-VOMIT: cleanup, enter dead-headless state ----
        BillboardSetColor((void*)&ENTITY->scaMatrixData, 1, 2, &DAT_00ffff50);
        ENTITY->death_timer = 0x46;
        ENTITY->state = ZOMBIE_STATE_DIE;
        ENTITY->ignore_player_flag = 1;
        Flg_on((int)g_roomItemsFlags, ENTITY->death_event_id);
        ENTITY->status_flags |= 0x0E;
        ENTITY->health = 0xFFFF;
        ENTITY->hit_state = 1;
        break;

    case 10:
        // ---- POST HEAD-BITE: finish animation, transition to head-exploded ----
        entity_apply_anim_vertex(ENTITY, ENTITY->animHeader, ENTITY->animBase);
        if ((char)Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400) != 0) {
            ENTITY->action_state = 7;
        }
        break;
    }
}

// ============================================================================
// zombie_action_update @ 0x00454ab0
// Per-behavior action dispatcher. Indexes into zombie_action_tbl by
// action_behavior (0x86) to run the current behavior's update logic.
// Behavior 1 (chase walk) further dispatches by action_state (0x87).
// ============================================================================
void zombie_action_update(void)
{
    unsigned char behavior = ENTITY->action_behavior;

    switch (behavior) {
    case 0:
        // No action active — return immediately
        return;

    case 1:
        // ---- CHASE WALK: sub-dispatch by action_state ----
        {
            if (ENTITY->action_state > 3) break;

            switch (ENTITY->action_state) {
            case 0:
                // Init chase walk
                ENTITY->action_state = 1;
                ENTITY->move_speed_current = 0;
                ENTITY->animation_frame_id = 0;
                ENTITY->timing_control = 0;
                ENTITY->animationId = 2;
                ENTITY->blend_counter = 3;
                Snd_em(4);
                if (Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400) == 0) break;
                // fall through to case 1 when anim loops
                ENTITY->action_state = 1;
                break;

            case 1:
                // Continue chase — update direction toward target
                g_tempVar = (void*)7;
                entity_update_wander_turn(
                    *(unsigned short*)&((unsigned char*)&ENTITY->subpixel_pos_x)[2],
                    &ENTITY->dir_control_flags,
                    &((unsigned char*)&ENTITY->subpixel_pos_x)[1],
                    ENTITY->turn_speed, 60);
        Add_speedXZ(0);
        }
        break;

    case 2:
        {
                // End chase — decelerate
                entity_update_wander_turn(
                    *(unsigned short*)&((unsigned char*)&ENTITY->subpixel_pos_x)[2],
                    &ENTITY->dir_control_flags,
                    &((unsigned char*)&ENTITY->subpixel_pos_x)[1],
                    ENTITY->turn_speed, 60);
                if (ENTITY->animation_frame_id > 15)
                    Add_speedXZ(64);
                break;

            case 3:
                // Release — return to idle
                ENTITY->action_behavior = 2;
                ENTITY->action_state = 0;
                ENTITY->animation_frame_id = 0;
                ENTITY->timing_control = 0;
                break;
            }
        }
        return;

    case 4:
        // ---- CHASE BEHAVIOR ----
        zombie_chase_player();
        return;

    case 5:
        // ---- ATTACK FOLLOW-THROUGH ----
        {
            if ((ENTITY->action_speed & 0x80) != 0) {
                ENTITY->state = ZOMBIE_STATE_DIE;
                ENTITY->action_state = 1;
                return;
            }
            if (ENTITY->action_state == 0) {
                ENTITY->action_state = 1;
                ENTITY->animation_frame_id = 0;
                ENTITY->timing_control = 0;
                ENTITY->animationId = 4;
                ENTITY->blend_counter = 3;
                ENTITY->action_ticks_counter =
                    (unsigned short)((g_RandSeed & 0x1F) + 0x78);
            }
            if (ENTITY->timing_control == 1) {
                if (ENTITY->animation_frame_id == 10)
                    Snd_em(0);
                if (ENTITY->animation_frame_id == 21)
                    Snd_em(1);
            }
            Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);
            if (--ENTITY->action_ticks_counter == 0) {
                ENTITY->action_state = 2;
            }
            g_tempVar = (void*)7;
            entity_update_wander_turn(
                *(unsigned short*)&((unsigned char*)&ENTITY->subpixel_pos_x)[2],
                &ENTITY->dir_control_flags,
                &((unsigned char*)&ENTITY->subpixel_pos_x)[1],
                ENTITY->turn_speed, 60);
            Add_speedXZ(0);
        }
        return;

    case 6:
        // ---- APPROACH PLAYER ----
        {
            Add_speedXZ(0);
            if ((ENTITY->action_speed & 0x80) != 0) {
                ENTITY->state = ZOMBIE_STATE_DIE;
                ENTITY->action_state = 1;
                return;
            }
            if (ENTITY->action_state == 0) {
                ENTITY->action_state = 1;
                ENTITY->animationId = 2;
                ENTITY->blend_counter = 3;
                ENTITY->move_speed_current = 0x14;
                ENTITY->animation_frame_id = 0;
                ENTITY->timing_control = 0;
                ENTITY->action_ticks_counter = 0;
            }
            if (ENTITY->timing_control == 1) {
                if (ENTITY->animation_frame_id == 4)
                    Snd_em(1);
                if (ENTITY->animation_frame_id == 25)
                    Snd_em(0);
            }
            ENTITY->action_ticks_counter =
                (unsigned short)((unsigned int)ENTITY->action_ticks_counter + 1);
            if ((short)ENTITY->action_ticks_counter == 19) {
                unsigned char dmg;
                if (Flg_ck((int)g_PlayerFlags, 0x7b) == 0)
                    dmg = zombie_damage_easy_tbl[ENTITY->behavior_flags & 0x0F];
                else
                    dmg = zombie_damage_normal_tbl[ENTITY->behavior_flags & 0x0F];
                g_playerEntity.health -= dmg;
                Snd_em(3);
                Effect_CreateBillboard(0, 0,
                    g_playerEntity.directionAngle + 2048,
                    (void*)g_deadMoveValue,
                    (void*)((int)ENTITY->jointsStructs + 0x150), 0);
            }
            Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);

            char result = Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);
            if (result != 0) {
                ENTITY->state = ZOMBIE_STATE_DAMAGED;
                ENTITY->ignore_player_flag = 1;
                ENTITY->action_behavior = 6;
                ENTITY->action_state = 0;
                ENTITY->status_flags &= ~0x0A;
                ENTITY->hit_state = 1;
            }
        }
        return;

    case 7:
        // ---- PUSH BACK ----
        zombie_pushed_back();
        return;

    default:
        // Unknown behavior — return to idle
        return;
    }
}

// ============================================================================
// zombie_chase_player @ 0x00433bb0
// Chases the player. Checks if close enough to attack, otherwise follows.
// ============================================================================
void zombie_chase_player(void)
{
    if ((ENTITY->behavior_step & 0x04) == 0
        && (ENTITY->action_speed & 0x80) == 0)
    {
        unsigned char canAttack = checkAngularViewAndDistance(700, 1500,
            (VECTOR*)&g_playerEntity.scaMatrixData.localMatrix.t);

        if (canAttack) {
            unsigned char lineOfSight = check_line_of_sight((SVECTOR*)&g_playerEntity.scaMatrixData.localMatrix.t);
            if (!lineOfSight && g_playerEntity.isBeingAttackedFlag == 0) {
                ENTITY->angle = getAngleTowardsTarget(
                    g_playerEntity.scaMatrixData.localMatrix.t[0],
                    g_playerEntity.scaMatrixData.localMatrix.t[2]);
                ENTITY->state = ZOMBIE_STATE_ATTACK;
                ENTITY->action_state = 0;
                zombie_attack();
                return;
            }
        }
    }

    if (ENTITY->ignore_player_flag == 0) {
        entity_update_player_distance();
    }
    update_zombie_action();
}

// ============================================================================
// zombie_pushed_back @ 0x00433c60
// Knockback state. Can transition to attack if close enough, otherwise
// continues pushback recovery.
// ============================================================================
void zombie_pushed_back(void)
{
    unsigned char canAttack = checkAngularViewAndDistance(512, 2200,
        (VECTOR*)&g_playerEntity.scaMatrixData.localMatrix.t);

    if (canAttack) {
        unsigned char lineOfSight = check_line_of_sight((SVECTOR*)&g_playerEntity.scaMatrixData.localMatrix.t);
        if (!lineOfSight && g_playerEntity.isBeingAttackedFlag == 0) {
            ENTITY->angle = getAngleTowardsTarget(
                g_playerEntity.scaMatrixData.localMatrix.t[0],
                g_playerEntity.scaMatrixData.localMatrix.t[2]);
            ENTITY->state = ZOMBIE_STATE_ATTACK;
            ENTITY->action_state = 0;
            zombie_attack();
            return;
        }
    }

    if (ENTITY->ignore_player_flag == 0) {
        entity_update_player_distance();
    }
    zombie_pushback_action();
}

// ============================================================================
// zombie_random_chase @ 0x00433cf0
// Staggered chase. Similar to chase but requires collision push flag clear.
// ============================================================================
void zombie_random_chase(void)
{
    if ((ENTITY->behavior_step & 0x04) == 0
        && (ENTITY->action_speed & 0x80) == 0
        && (ENTITY->collisionFlags & 0x08) == 0)
    {
        unsigned char canAttack = checkAngularViewAndDistance(700, 1500,
            (VECTOR*)&g_playerEntity.scaMatrixData.localMatrix.t);

        if (canAttack) {
            unsigned char lineOfSight = check_line_of_sight((SVECTOR*)&g_playerEntity.scaMatrixData.localMatrix.t);
            if (!lineOfSight && g_playerEntity.isBeingAttackedFlag == 0) {
                ENTITY->angle = getAngleTowardsTarget(
                    g_playerEntity.scaMatrixData.localMatrix.t[0],
                    g_playerEntity.scaMatrixData.localMatrix.t[2]);
                ENTITY->state = ZOMBIE_STATE_ATTACK;
                ENTITY->action_state = 0;
                zombie_attack();
                return;
            }
        }
    }

    if (ENTITY->ignore_player_flag == 0) {
        entity_update_player_distance();
    }
    update_zombie_action();
}

// ============================================================================
// zombie_eating @ 0x00436690
// Eating corpse animation. Loops eating animation until player gets within
// 3000 units, then stands up and enters die state.
// ============================================================================
void zombie_eating(void)
{
    switch (ENTITY->action_state) {
    case 0:
        ENTITY->action_state = 1;
        ENTITY->animation_frame_id = (unsigned char)g_RandSeed & 0x1F;
        ENTITY->timing_control = 0;
        ENTITY->blend_counter = 3;
        ENTITY->animationId = 0x1E;
        ENTITY->action_ticks_counter = (unsigned short)((g_RandSeed & 0xF) + 0x2D);
        break;
    case 1:
        if (Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400)) {
            ENTITY->animationId = (g_RandSeed & 1) ? 0x1D : 0x1E;
        }
        if (ENTITY->animation_frame_id == 0x19) {
            VECTOR eatPos = { 800, -300, 0, 0 };
            Effect_CreateBillboard(0, 0, 0, (void*)&ENTITY->scaMatrixData.localMatrix, &eatPos, 0);
            Snd_em(3);
        }
        {
            int dx = (int)g_playerEntity.scaMatrixData.localMatrix.t[0]
                   - (int)ENTITY->scaMatrixData.localMatrix.t[0];
            int dz = (int)g_playerEntity.scaMatrixData.localMatrix.t[2]
                   - (int)ENTITY->scaMatrixData.localMatrix.t[2];
            int absDx = (dx ^ (dx >> 31)) - (dx >> 31);
            int absDz = (dz ^ (dz >> 31)) - (dz >> 31);
            unsigned int dist = absDx - absDz + absDz; // Manhattan
            if (dist < 3000) {
                ENTITY->action_state = 2;
                return;
            }
        }
        break;
    case 2:
        ENTITY->action_state = 3;
        ENTITY->animation_frame_id = 0;
        ENTITY->timing_control = 0;
        ENTITY->animationId = 0x0D;
        break;
    case 3:
        if (Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400)) {
            ENTITY->behavior_flags &= ~ZOMBIE_FLAG_LAYING_DOWN;
            if (*(int*)&g_stageId == 0x504) // stage 5, room 4 special case
                ENTITY->behavior_flags |= 0x04;
            ENTITY->state = ZOMBIE_STATE_DIE;
            ENTITY->ignore_player_flag = 1;
            ENTITY->scaMatrixData.localMatrix.m[1][0] = 0; // clear laying-down state
        }
        break;
    }
}

// ============================================================================
// zombie_idle @ 0x004349d0
// Idle behavior: standing still with random head turns side to side.
// Cycles through looking left, right, and center with randomized durations.
// ============================================================================
static void zombie_idle(void)
{
    switch (ENTITY->action_state) {
    case 0:
        ENTITY->action_state = 1;
        ENTITY->animation_frame_id = 0;
        ENTITY->timing_control = 0;
        ENTITY->animationId = 0;
        ENTITY->action_ticks_counter = (unsigned short)((g_RandSeed & 0x7F) + 50);
        ENTITY->blend_counter = 3;
        break;
    case 1:
        Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);
        if (--ENTITY->action_ticks_counter == 0) {
            ENTITY->action_behavior = 1;  // switch to slow walk
            return;
        }
        break;
    case 2:
        ENTITY->action_state = 3;
        ENTITY->animation_frame_id = 0;
        ENTITY->timing_control = 0;
        ENTITY->animationId = 0;
        ENTITY->blend_counter = 3;
        ENTITY->action_ticks_counter = (unsigned short)((g_RandSeed & 0xF) + 8);
        break;
    case 3:
        Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);
        if (--ENTITY->action_ticks_counter == 0) {
            ENTITY->action_state = 4;
            ENTITY->action_ticks_counter = (unsigned short)((g_RandSeed & 0xF) + 8);
        }
        ENTITY->angle = ENTITY->angle + 12;  // turn right
        break;
    case 4:
        Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);
        if (--ENTITY->action_ticks_counter == 0) {
            ENTITY->action_state = 5;
            ENTITY->action_ticks_counter = (unsigned short)((g_RandSeed & 0xF) + 4);
        }
        ENTITY->angle = ENTITY->angle - 24;  // turn left (faster)
        break;
    case 5:
        Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);
        if (--ENTITY->action_ticks_counter == 0) {
            ENTITY->action_state = 6;
            ENTITY->action_ticks_counter = (unsigned short)((g_RandSeed & 0xF) + 8);
        }
        ENTITY->angle = ENTITY->angle - 32;  // turn left (fastest)
        break;
    case 6:
        Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);
        if (--ENTITY->action_ticks_counter == 0) {
            ENTITY->action_state = 7;
            ENTITY->action_ticks_counter = (unsigned short)((g_RandSeed & 0xF) + 4);
        }
        ENTITY->angle = ENTITY->angle + 24;  // turn right (faster)
        break;
    case 7:
        ENTITY->action_behavior = 0;  // restart idle loop
        break;
    }
}

// ============================================================================
// zombie_slow_walk @ 0x00434cd0
// Slow walk behavior. Walks toward a random target position, plays footsteps.
// Returns to idle after timer expires (~300 frames).
// ============================================================================
static void zombie_slow_walk(void)
{
    if (ENTITY->action_state == 0) {
        ENTITY->move_speed_current = 20;
        ENTITY->action_state = 1;
        ENTITY->animation_frame_id = 0;
        ENTITY->timing_control = 0;
        ENTITY->animationId = 1;
        ENTITY->blend_counter = 3;
        ENTITY->action_ticks_counter = (unsigned short)((g_RandSeed & 0x7F) + 300);

        // Set walk target 5000 units ahead
        memcpy(&g_matrixScratch, &g_identityMatrixData, sizeof(MATRIX));
        g_svecScratch.x = 5000;
        g_svecScratch.z = 0;
        g_svecScratch.y = 0;
        RotMatrixY(ENTITY->angle + 8, &g_matrixScratch);
        ApplyMatrixSV(&g_matrixScratch, &g_svecScratch, &g_svecScratch);
        ENTITY->player_pos_x = (unsigned char)((short)(*(int*)&ENTITY->scaMatrixData.localMatrix.t[0]) + g_svecScratch.x);
        ENTITY->player_pos_z = (unsigned char)((short)(*(int*)&ENTITY->scaMatrixData.localMatrix.t[2]) + g_svecScratch.z);
    }

    // Footstep sounds
    if (ENTITY->timing_control == 1) {
        if (ENTITY->animation_frame_id == 13)
            Snd_em(1);  // walk sfx
        if (ENTITY->animation_frame_id == 0x15)
            Snd_em(2);  // drag leg sfx
    }

    Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);

    if (--ENTITY->action_ticks_counter == 0) {
        ENTITY->action_behavior = 0;
        ENTITY->action_state = 0;
    }

    // Move toward target
    g_tempVar = (void*)7;
    entity_update_wander_turn(*(unsigned short*)&((unsigned char*)&ENTITY->subpixel_pos_x)[2],
                     &ENTITY->dir_control_flags,
                     &((unsigned char*)&ENTITY->subpixel_pos_x)[1],
                     ENTITY->turn_speed, 60);
    if (ENTITY->animation_frame_id > 15) {
        Add_speedXZ(0);
    }
}

// ============================================================================
// zombie_idling @ 0x00434730
// Simple idling behavior — resets to idle if action_behavior isn't idle.
// ============================================================================
static void zombie_idling(void)
{
    if (ENTITY->action_behavior != 0) {
        ENTITY->action_behavior = 0;
        ENTITY->action_state = 0;
    }
    entity_pathfind_update();
}

// ============================================================================
// zombie_walk2 @ 0x00434750
// Walk type 2 — similar to slow walk but with player awareness.
// Transitions to chase if player is close and facing the entity.
// ============================================================================
static void zombie_walk2(void)
{
    int px = (int)*(short*)&ENTITY->player_pos_x;
    int pz = (int)*(short*)&ENTITY->player_pos_z;
    VECTOR walkTarget = { px, 0, pz, 0 };

    g_tempVar = (void*)6;
    unsigned int temp = (unsigned int)ENTITY->action_behavior;

    if ((ENTITY->is_moving & 0xFF) == 0 && ENTITY->move_max_steps == 0) {
        unsigned int result = entity_pathfind_update();
        unsigned int isBlocked = result;
        if ((result & 0xFE) == 0) {
            ENTITY->is_moving &= 0xFE;
            *(unsigned short*)&ENTITY->is_moving |= (unsigned short)(isBlocked & 1);
        }
    }

    if (ENTITY->action_behavior != 0 && ENTITY->action_behavior != 1) {
        g_tempVar = (void*)7;
        entity_update_wander_turn(*(unsigned short*)&((unsigned char*)&ENTITY->subpixel_pos_x)[2],
                         &ENTITY->dir_control_flags,
                         &((unsigned char*)&ENTITY->subpixel_pos_x)[1],
                         ENTITY->turn_speed, 60);
    }

    // Falldown check
    if ((ENTITY->action_speed & 0x80) != 0) {
        ENTITY->state = ZOMBIE_STATE_DIE;
        ENTITY->action_state = 1;
        return;
    }

    // Player in range → start chasing
    if (g_playerDisplacement < 3000) {
        int angle = turn_toward_target(
            (VECTOR*)g_playerEntity.scaMatrixData.localMatrix.t, 0x400);
        if ((short)angle != 0) {
            ENTITY->ignore_player_flag = 1;
            ENTITY->action_behavior = 3;
            return;
        }
    }

    // Player close and facing → attack
    if ((ENTITY->collisionFlags & 8) && g_playerDisplacement < 2000) {
        int angle = turn_toward_target(
            (VECTOR*)g_playerEntity.scaMatrixData.localMatrix.t, 0x200);
        if ((short)angle == 0) {
            ENTITY->ignore_player_flag = 1;
            ENTITY->action_behavior = 6;
            if (g_playerEntity.isBeingAttackedFlag != 0) {
                ENTITY->action_state = 2;
            }
            return;
        }
    }

    // Blocked by player → switch to chase walk
    if ((*(unsigned short*)&ENTITY->is_moving) != 0 && g_playerEntity.isBeingAttackedFlag == 0) {
        if ((unsigned int)ENTITY->action_behavior != 2) {
            ENTITY->action_state = 0;
            ENTITY->blend_counter = 3;
        }
        ENTITY->action_behavior = 2;
        ENTITY->ignore_player_flag = 0;
    }

    // Player being attacked → move toward and eat
    if ((g_playerEntity.isBeingAttackedFlag & 0x80) != 0) {
        unsigned int blocked = *(unsigned int*)&ENTITY->is_moving;
        if (ENTITY->action_behavior == 2) {
            ENTITY->action_state = 0;
            ENTITY->blend_counter = 3;
        }
        ENTITY->action_behavior = 2;
        ENTITY->ignore_player_flag = 0;

        if (!(ENTITY->collisionFlags & 8) && g_playerDisplacement < 1200) {
            int angle = turn_toward_target(
                (VECTOR*)g_playerEntity.scaMatrixData.localMatrix.t, 0x2C8);
            if ((short)angle == 0) {
                ENTITY->ignore_player_flag = 1;
                ENTITY->action_behavior = 4;  // bend down and eat
            }
        }
    }
}

// ============================================================================
// zombie_check_player_distance @ 0x004345b0
// Periodic player distance check for eating/laying zombies.
// Slowly rotates toward the player; if close enough and facing, stands up.
// ============================================================================
void zombie_check_player_distance(void)
{
    ENTITY->action_ticks_counter++;

    int px = (int)*(short*)&ENTITY->player_pos_x;
    int pz = (int)*(short*)&ENTITY->player_pos_z;
    VECTOR target = { px, 0, pz, 0 };

    int angle = turn_toward_target(&target, 4);
    ENTITY->angle = ENTITY->angle + (short)angle;

    unsigned int result = entity_pathfind_update();
    if ((result & 1) == 0 || g_playerDisplacement > 8999) {
        if (ENTITY->action_behavior != 0 || g_playerDisplacement < 3000) {
            ENTITY->behavior_flags--;
            if ((ENTITY->behavior_flags & 0x0F) == ZOMBIE_BEH_7) {
                ENTITY->behavior_flags = (ENTITY->behavior_flags & 0xF0) | 0x04;
            }
        }
    } else {
        ENTITY->behavior_flags--;
        if ((ENTITY->behavior_flags & 0x0F) == ZOMBIE_BEH_7) {
            ENTITY->behavior_flags = (ENTITY->behavior_flags & 0xF0) | 0x04;
        }
    }
}

// ============================================================================
// Dependency stubs for zombie state handlers
// ============================================================================
// ============================================================================
// turn_toward_target @ 0x00489960
// Returns an angular step (+step, -step, or 0) to rotate the entity toward
// the target position. Uses getAngleTowardsTarget to compute the desired
// angle, then returns the shortest signed step to reduce the delta.
// If already facing the target within angle_step*2, returns 0.
// ============================================================================
int turn_toward_target(VECTOR* target_pos, short angle_step)
{
    unsigned short targetAngle = getAngleTowardsTarget(target_pos->x, target_pos->z);
    int delta = ((short)targetAngle - (short)(unsigned short)ENTITY->angle) + angle_step;
    delta &= 0xFFF;

    if (delta < (int)(unsigned short)(angle_step * 2)) {
        return 0;                    // already facing target
    }
    if (delta < ANGLE_HALF_CIRCLE + 1) {
        return angle_step;           // turn clockwise (shorter path right)
    }
    return -angle_step;              // turn counter-clockwise (shorter path left)
}
// ============================================================================
// check_line_of_sight @ 0x0048a4b0
// Checks whether the entity has a clear line of sight to the target position.
// Computes vector from entity to target, calls ChkOutsideCell for room
// boundaries, then room_collision_check_0047db90 for obstacle detection.
// Returns 0 if the path is clear, non-zero if blocked.
// ============================================================================
unsigned char check_line_of_sight(SVECTOR* targetPos)
{
    g_svecScratch.z = 0;
    g_svecScratch.y = 0;
    g_svecScratch.x = 0;

    unsigned int boundaryResult = ChkOutsideCell(targetPos, &g_svecScratch,
        (int)*(short*)g_RdtPointer->boundaries,
        (int)((short*)g_RdtPointer->boundaries)[1]);

    VECTOR local_10;
    local_10.x = *(int*)targetPos - *(int*)&ENTITY->scaMatrixData.localMatrix.t[0];
    local_10.y = *(int*)&targetPos->z - *(int*)&ENTITY->scaMatrixData.localMatrix.t[1];
    local_10.z = *(int*)(targetPos + 1) - *(int*)&ENTITY->scaMatrixData.localMatrix.t[2];

    unsigned int result = room_collision_check_0047db90(&local_10, boundaryResult);
    player_distance_z = result & 0xFF;
    return (unsigned char)(result & 0xFF);
}
// ============================================================================
// getAngleTowardsTarget @ 0x00460450
// Returns the PS1 angle (0-0xFFF, 4096 = 360°) from the current entity's
// position to the target (px, pz). Wraps CalculateAngleBetweenPointsXZ.
// ============================================================================
unsigned short getAngleTowardsTarget(int px, int pz)
{
    return CalculateAngleBetweenPointsXZ(
        *(int*)&ENTITY->scaMatrixData.localMatrix.t[0],  // entity pos X
        *(int*)&ENTITY->scaMatrixData.localMatrix.t[2],  // entity pos Z
        px, pz);
}

// zombie_attack_data_tbl @ 0x004bb3e8
// Falling attack data per attacking_direction: {speed_offset, timer, angle_offset}
static const unsigned short zombie_attack_data_tbl[12] = {
    0x0004, 0x0020, 0x0020,     // [0] facing player
    0x0004, 0x0055, 0x000C,     // [1] laying front
    0x0233, 0x0041, 0xFFE0,     // [2] laying back
    0x0000, 0x0A0A, 0x090C      // [3]
};
// Computes Manhattan distance to player and dispatches via the zombie move
// behavior table indexed by behavior_flags & 0x0F. If is_moving != 0, also
// calls FUN_0045f970 to update the distance-based pathfinding.
// ============================================================================
void entity_update_player_distance(void)
{
    if (*(unsigned short*)&ENTITY->is_moving != 0) {
        FUN_0045f970(
            g_playerEntityPointer.scaMatrixData.localMatrix.t[0],
            g_playerEntityPointer.scaMatrixData.localMatrix.t[2],
            (int*)&ENTITY->player_pos_x,
            (int*)&ENTITY->player_pos_z);
    }

    int dz = (int)g_playerEntityPointer.scaMatrixData.localMatrix.t[2]
           - *(int*)&ENTITY->scaMatrixData.localMatrix.t[2];
    int dx = (int)g_playerEntityPointer.scaMatrixData.localMatrix.t[0]
           - *(int*)&ENTITY->scaMatrixData.localMatrix.t[0];
    int absDz = (dz ^ (dz >> 31)) - (dz >> 31);
    int absDx = (dx ^ (dx >> 31)) - (dx >> 31);
    g_playerDisplacement = absDz - (dx >> 31) + absDx;

    unsigned char behavior = ENTITY->behavior_flags & 0x0F;
    switch (behavior) {
    case 0: zombie_idle(); break;
    case 1: zombie_slow_walk(); break;
    case 2: break; // chase walk handled by caller
    case 10: zombie_check_player_distance(); break;
    default: break;
    }
}

// ============================================================================
// update_zombie_action @ 0x004342f0
// Master zombie behavior dispatcher. Switches on action_behavior (0x86)
// to dispatch to the appropriate behavior handler. Handles behaviors 0-12.
// Many sub-cases delegate to the already-implemented behavior functions.
// ============================================================================
void update_zombie_action(void)
{
    unsigned char behavior = ENTITY->action_behavior;

    switch (behavior) {
    case 0:
        // ---- IDLE: random head turns ----
        zombie_idle();
        return;

    case 1:
        // ---- SLOW WALK ----
        zombie_slow_walk();
        return;

    case 2:
        // ---- CHASE WALK ----
        zombie_chase_player();  // handles chase walk logic
        return;

    case 3:
        // No-op
        break;

    case 4:
        // ---- LAYING-DOWN APPROACH ----
        if (ENTITY->action_state == 0) {
            ENTITY->action_state = 3;
            ENTITY->animation_frame_id = 0;
            ENTITY->timing_control = 0;
            ENTITY->blend_counter = 3;
            ENTITY->animationId = 0x0B;
        } else if (ENTITY->action_state == 1) {
            Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);
            if (--ENTITY->action_ticks_counter == 0) {
                ENTITY->action_state++;
            }
        } else if (ENTITY->action_state == 2) {
            ENTITY->animation_frame_id = 0;
            ENTITY->timing_control = 0;
            ENTITY->action_state++;
            ENTITY->blend_counter = 3;
            ENTITY->animationId = 0x0B;
        } else if (ENTITY->action_state == 3) {
            int result = Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);
            if ((char)result != 0) {
                ENTITY->action_state++;
            }
            int turnStep = turn_toward_target(
                (VECTOR*)g_playerEntityPointer.scaMatrixData.localMatrix.t, 0x20);
            ENTITY->angle = ENTITY->angle + (short)turnStep;
        } else if (ENTITY->action_state == 4) {
            ENTITY->action_state++;
            ENTITY->animationId++;
            ENTITY->timing_control = 0;
            ENTITY->action_ticks_counter = 120;
        } else if (ENTITY->action_state == 5) {
            Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);
            if ((ENTITY->animation_frame_id & 7) == 0) {
                VECTOR eatPos = { 800, -300, 0, 0 };
                Effect_CreateBillboard(0, 0, 0, &ENTITY->scaMatrixData, &eatPos, 0);
                Snd_em(3);
            }
            if (ENTITY->animation_frame_id == 0x0E) {
                _ENTITY_SAVE = &g_playerEntityPointer;
                JointApplyColorTint(g_playerEntityPointer.jointsStructs, 0x30, 0x80820, &DAT_00606060);
                VECTOR pos = { 100, -700, 0, 0 };
                Effect_CreateBillboard(0, 0, 0, &g_playerEntityPointer.jointsStructs->world, &pos, 0);
                JointApplyColorTint(g_playerEntityPointer.jointsStructs + 2, 0x30, 0x80820, &DAT_00606060);
                Effect_CreateBillboard(0, 0, 0, &g_playerEntityPointer.jointsStructs[2].world, &pos, 0);
            }
            if (--ENTITY->action_ticks_counter == 0) {
                ENTITY->action_state++;
                ENTITY->animationId++;
                ENTITY->animation_frame_id = 0;
                ENTITY->timing_control = 0;
                ENTITY->blend_counter = 3;
            }
        } else if (ENTITY->action_state == 6) {
            if (Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400) != 0) {
                ENTITY->state = ZOMBIE_STATE_IDLE;
            }
        }
        return;

    case 5:
        // ---- FALLING ATTACK ----
        if (ENTITY->action_state == 0) {
            ENTITY->action_state = 1;
            ENTITY->animation_frame_id = 0;
            ENTITY->timing_control = 0;
            ENTITY->blend_counter = 3;
            ENTITY->animationId = 3;
            ENTITY->move_speed_current = 45;
            Add_speedXZ(*(unsigned short*)(&zombie_attack_data_tbl
                + (unsigned int)ENTITY->attacking_direction * 6));
            ENTITY->action_ticks_counter = *(unsigned short*)(&zombie_attack_data_tbl
                + (unsigned int)ENTITY->attacking_direction * 6 + 2);
        }
        if (ENTITY->timing_control == 1) {
            if (ENTITY->animation_frame_id == 8) Snd_em(1);
            if (ENTITY->animation_frame_id == 0x1D) Snd_em(1);
        }
        ENTITY->angle = ENTITY->angle
            - *(short*)(&zombie_attack_data_tbl
                + (unsigned int)ENTITY->attacking_direction * 6 + 4);
        Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);
        if (--ENTITY->action_ticks_counter == 0) {
            ENTITY->ignore_player_flag = 1;
            ENTITY->action_behavior = 4;
        }
        return;

    case 6:
        // ---- VOMITING/EATING ----
        switch (ENTITY->action_state) {
        case 0:
            ENTITY->action_state = 1;
            ENTITY->animation_frame_id = 0;
            ENTITY->timing_control = 0;
            ENTITY->animationId = 5;
            {
                VECTOR vPos = { 500, -2500, 0, 0 };
                Effect_CreateBillboard(0x20, 0, 0, &ENTITY->scaMatrixData, &vPos, 0);
            }
            Snd_em(7);
            break;
        case 1:
            ENTITY->action_state += (char)Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);
            break;
        case 2:
            ENTITY->action_state = 3;
            ENTITY->animation_frame_id = 0;
            ENTITY->timing_control = 0;
            ENTITY->animationId = 0;
            ENTITY->action_ticks_counter = (unsigned short)((g_RandSeed & 0x1F) + 20);
            ENTITY->blend_counter = 3;
            break;
        case 3:
            Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);
            if (--ENTITY->action_ticks_counter == 0) {
                ENTITY->ignore_player_flag = 0;
                ENTITY->action_behavior = 0;
            }
            break;
        }
        return;

    case 7:
        // ---- FALLDOWN ----
        zombie_falldown();
        return;

    case 8:
        // ---- WALK2 ----
        zombie_walk2();
        return;

    default:
        if (behavior > 8) {
            // ---- STANDING IDLE with head turns / WALK2 variant ----
            // Cases 9 (default/standing), 10 (laying walk2), 11 (idling), 12 (walk2 variant)
            if (behavior == 10) {
                // laying walk2
                int dx = (int)*(short*)&ENTITY->player_pos_x
                       - *(int*)&ENTITY->scaMatrixData.localMatrix.t[0];
                int dz = (int)*(short*)&ENTITY->player_pos_z
                       - *(int*)&ENTITY->scaMatrixData.localMatrix.t[2];
                g_scaled_down_dist = ((dx ^ (dx >> 31)) - (dx >> 31))
                                   + ((dz ^ (dz >> 31)) - (dz >> 31));
                player_distance_z = (unsigned int)ENTITY->action_behavior;

                if (*(unsigned short*)&ENTITY->is_moving == 0) {
                    unsigned int check = entity_pathfind_update();
                    // g_entity_bkp = check
                    if ((check & 0xFE) == 0) {
                        ENTITY->is_moving &= 0xFE;
                        *(unsigned short*)&ENTITY->is_moving |= (unsigned short)(check & 1);
                    }
                }
                // Fall through to common behavior handling
            }
            zombie_check_player_distance();
        } else {
            // Unknown behavior — fall back to idle
            zombie_idle();
        }
        return;
    }
}
// ============================================================================
// zombie_pushback_action @ 0x00434310
// Dispatcher for pushback behaviors: calls zombie_pushback_idle if
// action_behavior == 0 (simple idle stagger), otherwise calls
// zombie_pushback_stagger (active backward walk with stagger).
// ============================================================================
void zombie_pushback_action(void)
{
    if (ENTITY->action_behavior == 0) {
        zombie_pushback_idle();
    } else {
        zombie_pushback_stagger();
    }
}
// ============================================================================
// entity_pathfind_update @ 0x0048ad10
// Obstacle-detection pathfinding state machine. Uses entity+0x164 as a
// 3-bit counter (bits 0-4, clamped to 15) + direction flag (bit 5).
// Returns: 0 = no target, 1 = target acquired/path clear, 2 = waiting.
// When counter == 3, stores player position as new movement target.
// ============================================================================
unsigned int entity_pathfind_update(void)
{
    unsigned char* state = (unsigned char*)ENTITY + 0x164;  // pathfind_state
    unsigned char val = *state;
    unsigned char counter = val & 0x1F;

    if (counter > 3) {
        *state = counter + 1;
        if ((*state & 0x1F) > 0x0F)
            *state &= 0xC0;  // clamp counter
        return 2;
    }

    char result = entity_check_angular_los(1512, (VECTOR*)g_playerEntityPointer.scaMatrixData.localMatrix.t);
    *state = (result << 5) | val;

    val = *state;
    counter = val & 0x1F;

    if (counter == 3) {
        if ((val & 0x20) == 0) {
            ENTITY->player_pos_x = (unsigned char)(unsigned short)g_playerEntityPointer.scaMatrixData.localMatrix.t[0];
            ENTITY->player_pos_z = (unsigned char)(unsigned short)g_playerEntityPointer.scaMatrixData.localMatrix.t[2];
            *state = counter + 1;
            *state &= ~0x20;
            return 1;
        }
        *state = counter + 1;
        *state &= ~0x20;
        return 0;
    }

    *state = counter + 1;
    return 2;
}
// ============================================================================
// magnum_shot_pushback @ 0x00436b70
// Magnum/explosive headshot death — the zombie is violently pushed back.
// Plays animation 3 with speed 45, orienting away from the player.
// On completion, transitions to dead state with the die animation.
// ============================================================================
void magnum_shot_pushback(void)
{
    if (ENTITY->action_state == 0) {
        ENTITY->behavior_step |= 0x01;
        ENTITY->move_speed_current = 45;
        ENTITY->action_state = 1;
        ENTITY->animation_frame_id = 0;
        ENTITY->timing_control = 0;
        ENTITY->animationId = 3;
        ENTITY->blend_counter = 3;
        return;
    }

    if (ENTITY->action_state == 1) {
        entity_rotate_toward_target((VECTOR*)g_playerEntityPointer.scaMatrixData.localMatrix.t, 32);

        if (ENTITY->timing_control == 1) {
            if (ENTITY->animation_frame_id == 8) Snd_em(1);
            if (ENTITY->animation_frame_id == 29) Snd_em(1);
        }

        if (Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400) != 0) {
            *(unsigned int*)(&ENTITY->state) = 0x103;  // die state: state=3, ignore=1, behavior=0, sub=3
        }
    }

    Add_speedXZ(0);
}

// ============================================================================
// checkAngularViewAndDistance @ 0x00489cf0
// Checks whether a target position is within the entity's angular field of
// view (a wedge defined by fovHalfAngle) and within maxDistance. Creates two
// edge vectors from entity angle ± fovHalfAngle, then uses 2D cross products
// to test if the direction to target lies between them. Returns true if the
// target is both within range and within the FOV wedge.
// ============================================================================
unsigned char checkAngularViewAndDistance(short fovHalfAngle, short maxDistance, VECTOR* targetPos)
{
    VECTOR referenceForward = { 2000, 0, 0, 0 };

    VECTOR dirToTarget;
    dirToTarget.x = targetPos->x - *(int*)&ENTITY->scaMatrixData.localMatrix.t[0];
    dirToTarget.z = targetPos->z - *(int*)&ENTITY->scaMatrixData.localMatrix.t[2];
    dirToTarget.y = 0;

    int absDx = (dirToTarget.x ^ (dirToTarget.x >> 31)) - (dirToTarget.x >> 31);
    int absDz = (dirToTarget.z ^ (dirToTarget.z >> 31)) - (dirToTarget.z >> 31);
    if ((int)(unsigned short)maxDistance < absDx - (dirToTarget.x >> 31) + absDz)
        return 0;

    MATRIX local_20;
    local_20 = g_identityMatrixData;

    VECTOR leftEdge, rightEdge;
    RotMatrixY(ENTITY->angle - (int)fovHalfAngle, &local_20);
    ApplyMatrixLV(&local_20, &referenceForward, &leftEdge);

    RotMatrixY(fovHalfAngle * 2, &local_20);
    ApplyMatrixLV(&local_20, &referenceForward, &rightEdge);

    vectorMul3(&leftEdge, &dirToTarget, &leftEdge);
    vectorMul3(&rightEdge, &dirToTarget, &rightEdge);

    return (unsigned char)((leftEdge.y & 0x80000000U) < (rightEdge.y & 0x80000000U));
}
// ============================================================================
// entity_update_wander_turn @ 0x00489800
// Controls randomized wandering turns when the entity gets stuck. If movement
// distance falls below a threshold derived from the entity's speed divider,
// a turn counter increments. When it exceeds turn_limit, a random turn
// (direction from g_RandSeed bit 6) activates. Also applies angular rotation
// toward the current waypoint using getAngleTowardsTarget.
// ============================================================================
unsigned int entity_update_wander_turn(unsigned int movement_dist, unsigned char* control_flags, unsigned char* turn_counter, unsigned short angle_step, unsigned char turn_limit)
{
    unsigned short* entity_angle = (unsigned short*)&ENTITY->angle;
    unsigned char* speedDiv = (unsigned char*)ENTITY + 0x61;  // speed_divider (inside scaMatrixData — struct layout gap)

    if ((*control_flags & 0x80) != 0) {
        *entity_angle = *entity_angle
            + (1 - (unsigned short)((*control_flags & 0x40) >> 5)) * angle_step;

        int speedDiv3 = *speedDiv * 3;
        int threshold = (speedDiv3 + (speedDiv3 >> 31 & 3)) >> 2;

        if ((unsigned int)threshold < movement_dist) {
            unsigned char newCount = *turn_counter - 1;
            *turn_counter = newCount;
            if (newCount == 0) {
                *control_flags = 0;
                *turn_counter = 0;
            }
        }
        return 1;
    }

    if (movement_dist < (unsigned int)((*speedDiv * 2) / 3)) {
        unsigned char newCount = *turn_counter + 1;
        *turn_counter = newCount;
        if (turn_limit < newCount) {
            unsigned char flags = *control_flags;
            *control_flags = flags | 0x80;
            *control_flags = ((unsigned char)g_RandSeed & 0x40) | flags | 0x80;
            *turn_counter = turn_limit / 6;
        }
    } else {
        *control_flags = 0;
        *turn_counter = 0;
    }

    short waypointAngle = getAngleTowardsTarget(
        (int)*(unsigned char*)((char*)ENTITY + 0xB3),
        (int)*(unsigned char*)((char*)ENTITY + 0xB4));
    unsigned short targetAngle = (unsigned short)waypointAngle;

    if ((angle_step & 0x8000) != 0) {
        angle_step = -angle_step;
        targetAngle = (targetAngle + 0x800) & 0xFFF;
    }

    unsigned short delta = ((unsigned short)(angle_step - *entity_angle) + targetAngle) & 0xFFF;

    if ((int)delta < (short)(angle_step * 2)) {
        *entity_angle = targetAngle;
        return 0;
    }

    *entity_angle = *entity_angle - angle_step;
    if (delta < 0x801) {
        *entity_angle = *entity_angle + angle_step * 2;
    }
    return 0;
}
// ============================================================================
// entity_rotate_toward_target @ 0x004899b0
// Smoothly rotates the entity toward or away from a target position by
// angular steps. Bit 15 of angleStep flips the direction (face away).
// Computes desired angle via getAngleTowardsTarget, then adjusts entity
// angle by +angleStep (toward) or -angleStep + 180° (away).
// ============================================================================
void entity_rotate_toward_target(VECTOR* pos, unsigned short angleStep)
{
    short targetAngle = getAngleTowardsTarget(pos->x, pos->z);
    unsigned short baseAngle = (unsigned short)targetAngle;

    if ((angleStep & 0x8000) != 0) {
        angleStep = -angleStep;
        baseAngle = (baseAngle + 0x800) & 0xFFF;  // +180°
    }

    unsigned short delta = ((unsigned short)(angleStep - ENTITY->angle) + baseAngle) & 0xFFF;

    if ((int)(unsigned short)delta < (short)(angleStep * 2)) {
        ENTITY->angle = (short)baseAngle;
        return;
    }

    ENTITY->angle = ENTITY->angle - (short)angleStep;
    if (delta < ANGLE_HALF_CIRCLE + 1) {
        ENTITY->angle = ENTITY->angle + (short)(angleStep * 2);
    }
}
unsigned char FUN_0048ae00(int joint, VECTOR* pos, int radius, int playerPtr) { return 0; }
void vectorMul3(VECTOR* a, VECTOR* b, VECTOR* dst) { }
void FUN_0040a380(VECTOR* v0, VECTOR* v1) { }
short room_collision_check_0047da50(VECTOR* pos, VECTOR* dir) { return 0; }
void FUN_0045f970(int px, int pz, int* a, int* b) { }
unsigned int g_entity_bkp = 0;
void* _ENTITY_SAVE = NULL;
int g_scaled_down_dist = 0;
// ============================================================================
// entity_check_angular_los @ 0x00489c60
// Checks if the target position is within the entity's angular FOV (half-angle
// param_1) AND has a clear line of sight. Returns 0 if path is clear, non-zero
// if blocked or outside the angular wedge.
// ============================================================================
unsigned int entity_check_angular_los(short fovHalfAngle, VECTOR* targetPos)
{
    short targetAngle = getAngleTowardsTarget(targetPos->x, targetPos->z);
    unsigned short delta = ((unsigned short)(fovHalfAngle - ENTITY->angle) + (unsigned short)targetAngle) & 0xFFF;

    if ((int)(unsigned short)(fovHalfAngle * 2) < (int)(unsigned short)delta)
        return 1;  // outside angular FOV

    VECTOR dir;
    dir.x = targetPos->x - *(int*)&ENTITY->scaMatrixData.localMatrix.t[0];
    dir.z = targetPos->z - *(int*)&ENTITY->scaMatrixData.localMatrix.t[2];
    dir.y = 0;

    unsigned char boundaryIndex = *(unsigned char*)((char*)ENTITY + 0x164);
    return room_collision_check_0047db90(&dir, boundaryIndex);
}

// Zombie damage behavior stubs (dispatched from zombie_damaged)
// zombie_recovery_timer_tbl @ 0x004bb400
// Random recovery time multipliers (×30 frames) before zombie stands up after falldown.
static const unsigned char zombie_recovery_timer_tbl[16] = {
    2, 4, 4, 4, 4, 4, 4, 6, 6, 6, 6, 6, 6, 9, 9, 9
};

// ============================================================================
// zombie_falldown @ 0x00436af0
// Falls down to the ground after a strong hit. Plays fall animation, waits
// a random time on the ground, then attempts to stand up. If stagger_timer
// is 0, gives up and enters damaged state; otherwise restarts cycle.
// ============================================================================
void zombie_falldown(void)
{
    switch (ENTITY->action_state) {
    case 0:
        ENTITY->action_state = 1;
        ENTITY->animation_frame_id = 0;
        ENTITY->timing_control = 0;
        ENTITY->animationId = 8;
        ENTITY->blend_counter = 7;
        ENTITY->move_speed_current = 20;
        Snd_em(5);
        ENTITY->scaMatrixData.localMatrix.m[1][0] = 1;  // set laying-down matrix
        break;

    case 1:
        ENTITY->behavior_step &= ~0x04;
        ENTITY->hit_state = 1;
        ENTITY->action_speed |= 0x80;

        if (ENTITY->animation_frame_id < 4)
            ENTITY->action_speed &= ~0x80;  // still falling forward
        else
            ENTITY->behavior_step |= 0x04;  // actually on ground

        if (ENTITY->timing_control == 1) {
            if (ENTITY->animation_frame_id == 8)
                Snd_em(1);  // walk SFX
            if (ENTITY->animation_frame_id == 0x1A)
                Snd_em(0);  // thud SFX
        }

        if ((char)Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x200) != 0) {
            ENTITY->hit_state = 0;
            ENTITY->action_ticks_counter =
                (unsigned short)zombie_recovery_timer_tbl[g_RandSeed & 0x0F] * 30;
            ENTITY->action_state = 2;
        }
        Add_speedXZ(0);
        break;

    case 2:
        if (--ENTITY->action_ticks_counter == 0) {
            ENTITY->action_state = 3;
            ENTITY->blend_counter = 0;
            ENTITY->hit_state = 1;
            Snd_em(5);  // get-up SFX
            return;
        }
        break;

    case 3:
        ENTITY->behavior_step &= ~0x04;
        ENTITY->hit_state = 1;
        ENTITY->action_speed |= 0x80;

        if (ENTITY->animation_frame_id > 0x1A)
            ENTITY->action_speed &= ~0x7F;  // almost stood up

        if ((char)Joint_move(1, ENTITY->animHeader, ENTITY->animBase, 0x200) != 0) {
            if (ENTITY->stagger_timer == 0) {
                ENTITY->action_speed = 0;
            } else {
                ENTITY->action_speed &= ~0x80;
            }
            ENTITY->hit_state = 0;
            ENTITY->state = ZOMBIE_STATE_DIE;
            ENTITY->ignore_player_flag = 1;
            ENTITY->behavior_step &= ~0x04;
            ENTITY->scaMatrixData.localMatrix.m[1][0] = 0;  // clear laying state
        }
        Add_speedXZ(0x800);
        break;
    }
}

// ============================================================================
// short_push_back @ 0x00436c80
// Short hit reaction — the zombie staggers backward. Plays either stagger
// animation (5) or vomit stagger (4). Sets up joint damage visual effects
// (limb severing with blood billboards), plays moan SFX, and transitions
// back to damaged state when the animation finishes.
// ============================================================================
void short_push_back(void)
{
    if ((ENTITY->behavior_flags & ZOMBIE_FLAG_VOMITING) == 0)
        ENTITY->animationId = 5 - (ENTITY->action_behavior == 0);
    ENTITY->move_speed_current = 15;

    if (ENTITY->action_state == 0) {
        ENTITY->action_state = 1;
        ENTITY->move_speed_current = 0;
        ENTITY->animation_frame_id = 0;
        ENTITY->timing_control = 0;
        ENTITY->blend_counter = 3;

        if ((ENTITY->behavior_flags & ZOMBIE_FLAG_VOMITING) != 0) {
            VECTOR bloodPos = { 100, -2620, 0, 0 };
            Effect_CreateBillboard(0, 0, 0, (void*)&ENTITY->scaMatrixData.localMatrix, &bloodPos, 0);
        }

        int jointPtr = (int)ENTITY->jointsStructs;
        if ((ENTITY->hit_state & 7) == 3) {
            unsigned char* jointFlag = (unsigned char*)(jointPtr + 0x1F0);
            if ((*jointFlag & 4) == 0) {
                unsigned char randBit = (0x40 >> ((unsigned char)g_RandSeed & 7)) & 1;
                if (randBit != 0) {
                    *jointFlag |= 12;  // disable and flag as severed
                    VECTOR zeroPos = { 0, 0, 0, 0 };
                    Effect_CreateBillboard(0, 0, 0, (void*)(jointPtr + 0x234), &zeroPos, 0);
                    Effect_CreateBillboard(0, 0, 0, (void*)0, (void*)(jointPtr + 0x248), 0);
                    *(unsigned char*)(jointPtr + 0x26C) |= 0x10;
                    JointApplyColorTint((JointStruct*)(jointPtr + 0x1F0), 0x30, 0x80820, &DAT_00606060);
                    JointApplyColorTint((JointStruct*)(jointPtr + 0x174), 0x30, 0x80820, &DAT_00606060);
                }
            }
        }

        if (ENTITY->internal_timer == 0) {
            Snd_em(9);  // moan SFX
            ENTITY->internal_timer = 150;
        }
    }

    char done = Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 1024);
    if (done != 0) {
        ENTITY->action_state++;

        if ((ENTITY->behavior_flags & ZOMBIE_FLAG_VOMITING) == 0) {
            if ((ENTITY->behavior_flags & 0x0F) == ZOMBIE_BEH_5)
                ENTITY->behavior_flags &= ~0x02;  // clear laying flag
            ENTITY->state = ZOMBIE_STATE_DIE;
            ENTITY->ignore_player_flag = 1;
        }

        ENTITY->player_pos_x = (unsigned char)(unsigned short)g_playerEntity.scaMatrixData.localMatrix.t[0];
        ENTITY->player_pos_z = (unsigned char)(unsigned short)g_playerEntity.scaMatrixData.localMatrix.t[2];
        ENTITY->hit_state = 0;
    }

    zombie_check_special_weapon();

    if ((ENTITY->hit_state & 1) != 0) {
        ENTITY->action_counter++;
        if (ENTITY->action_counter == 2) {
            ENTITY->move_speed = 20;
            ENTITY->turn_speed = 14;
        }
    }

    Add_speedXZ(0);
}

// ============================================================================
// push_and_stagger @ 0x00436f00
// Strong pushback — staggers backward harder. Uses animation 6 (front push)
// or 7 (side push). Plays moan SFX and transitions to damaged state on
// completion.
// ============================================================================
void push_and_stagger(void)
{
    ENTITY->animationId = (ENTITY->action_behavior == 2) ? 7 : 6;
    ENTITY->move_speed_current = 15;

    if (ENTITY->action_state == 0) {
        ENTITY->action_state = 1;
        ENTITY->animation_frame_id = 0;
        ENTITY->timing_control = 0;
        ENTITY->blend_counter = 3;
        ENTITY->animationId = 6;

        if (ENTITY->internal_timer == 0) {
            Snd_em(9);  // moan SFX
            ENTITY->internal_timer = 150;
        }
    }

    char done = Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 1024);
    if (done == 0) {
        if (ENTITY->animation_frame_id < 5)
            ENTITY->move_speed_current = (unsigned short)(ENTITY->move_speed_current + 90);
        if (ENTITY->animation_frame_id < 0x14) {
            zombie_check_special_weapon();
            Add_speedXZ(0x800);
            return;
        }
    } else {
        if ((ENTITY->behavior_flags & 0x0F) == ZOMBIE_BEH_5)
            ENTITY->behavior_flags &= ~0x02;
        ENTITY->state = ZOMBIE_STATE_DIE;
        ENTITY->ignore_player_flag = 1;
        ENTITY->player_pos_x = (unsigned char)(unsigned short)g_playerEntity.scaMatrixData.localMatrix.t[0];
        ENTITY->player_pos_z = (unsigned char)(unsigned short)g_playerEntity.scaMatrixData.localMatrix.t[2];
    }

    ENTITY->hit_state = 0;
    zombie_check_special_weapon();
    Add_speedXZ(0x800);
}

// ============================================================================
// zombie_pushback_idle @ 0x00435d90
// Idle stagger animation (animation 9). Just plays the animation with
// no movement — used when the zombie is pushed back while idling.
// ============================================================================
void zombie_pushback_idle(void)
{
    ENTITY->animationId = 9;

    if (ENTITY->action_state == 0) {
        ENTITY->action_state = 1;
        ENTITY->animation_frame_id = 0;
        ENTITY->timing_control = 0;
        ENTITY->blend_counter = 3;
    }

    Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);
}

// ============================================================================
// zombie_pushback_stagger @ 0x004362a0
// Active stagger backward walk. 4-state FSM: init (animation 14), wait for
// frame 5, walk backward with body-part physics and SFX, then decelerate
// with random timer.
// ============================================================================
void zombie_pushback_stagger(void)
{
    switch (ENTITY->action_state) {
    case 0:
        ENTITY->action_state = 1;
        ENTITY->animation_frame_id = 0;
        ENTITY->timing_control = 0;
        ENTITY->blend_counter = 3;
        ENTITY->animationId = 0x0E;
        break;

    case 1:
        Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);
        if (ENTITY->animation_frame_id == 5) {
            ENTITY->action_state = 2;
            ENTITY->blend_counter = 3;
            ENTITY->move_speed_current = 20;
            ENTITY->action_ticks_counter = 1;
        }
        break;

    case 2:
        {
        if ((ENTITY->animation_frame_id == 7 || ENTITY->animation_frame_id == 0x18)
            && ENTITY->timing_control == 1) {
            Snd_em(2);  // walk-drag SFX
        }

        Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);
        zombie_body_part_physics(1);

        int jointPtr = (int)ENTITY->jointsStructs;
        if ((*(unsigned char*)(jointPtr + 0x1F0) & 4) != 0
            && ENTITY->animation_frame_id > 0x11) {
            return;
        }

        {
            VECTOR target;
            target.x = (int)*(short*)&ENTITY->player_pos_x;
            target.z = (int)*(short*)&ENTITY->player_pos_z;
            target.y = 0;
            int turnStep = turn_toward_target(&target, 8);
            ENTITY->angle_turn_delta = (unsigned char)(unsigned short)turnStep;

            if ((ENTITY->dir_control_flags & 0x80) != 0) {
                ENTITY->angle_turn_delta = -ENTITY->angle_turn_delta;
                ENTITY->action_state = 3;
                ENTITY->action_ticks_counter = (unsigned short)((g_RandSeed & 0x1F) + 30);
            }
        }

        ENTITY->angle = ENTITY->angle + (short)ENTITY->angle_turn_delta;
        Add_speedXZ(0);
        }
        break;
    case 3:
        Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);
        zombie_body_part_physics(1);
        ENTITY->angle = ENTITY->angle + (short)ENTITY->angle_turn_delta;
        Add_speedXZ(0);

        if (--ENTITY->action_ticks_counter == 0)
            ENTITY->action_state = 3;  // loop
        break;
    }

    if ((ENTITY->collisionFlags & 8) != 0) {
        ENTITY->behavior_flags++;
    }
}

// ============================================================================
// zombie_check_special_weapon @ 0x0043d8a0
// If special weapon equipped (>110) and anim frame % 5 == 0, clears hit_state
// to prevent stagger effect during special weapon damage.
// ============================================================================
void zombie_check_special_weapon(void)
{
    if (g_playerEntityPointer.equippedWeaponId > 0x6E
        && ENTITY->animation_frame_id % 5 == 0) {
        ENTITY->hit_state = 0;
    }
}

// ============================================================================
// zombie_body_part_physics @ 0x00437c30
// Computes movement speed from body-part joint physics. Rotates entity
// matrix, applies to body part joints with reverse scaling, extracts
// displacement, computes its magnitude via SquareRoot0, and stores it
// in move_speed_current.
// ============================================================================
void zombie_body_part_physics(unsigned char param)
{
    int jointPtr = (int)ENTITY->jointsStructs;

    RotMatrix((SVECTOR*)((char*)ENTITY + 0x72), (MATRIX*)((char*)ENTITY + 0x20));
    ApplyLVAndMul0Matrix((void*)((char*)ENTITY + 0x20), (void*)(jointPtr + 0x24), &g_matrixScratch);
    ApplyLVAndMulMatrix(&g_matrixScratch, (MATRIX*)(jointPtr + 0xA0));

    unsigned char count = 3;
    int base = jointPtr + (unsigned int)param * 0x174 + 0x26C;
    do {
        ApplyLVAndMulMatrix(&g_matrixScratch, (MATRIX*)((unsigned int)count * -0x7C + base + 0xA0));
        count--;
    } while (count != 0);

    g_matrixScratch.t[0] = g_matrixScratch.t[0] - *(int*)(base + 0x58);
    g_matrixScratch.t[1] = 0;
    g_matrixScratch.t[2] = g_matrixScratch.t[2] - *(int*)(base + 0x60);

    VECTOR result;
    FUN_0040a380((VECTOR*)g_matrixScratch.t, &result);

    unsigned int mag = SquareRoot0(result.z + result.x);
    ENTITY->move_speed_current = (unsigned short)mag;
}

unsigned int is_facing_toward_entity(void* player) { return 0; }
// ============================================================================
// snap_player_to_grab_position @ 0x00489ee0
// Positions the player at the entity's grab point by extracting the current
// animation vertex for the entity's attacking joint, rotating it to world
// space, and computing the target position. Sets the player's grab-target
// coordinates so the player model snaps to the bite/grab spot.
// ============================================================================
void snap_player_to_grab_position(void* player)
{
    entity_extract_anim_vertex(ENTITY, ENTITY->animHeader, ENTITY->animBase, 0);

    g_matrixScratch = g_identityMatrixData;
    RotMatrixY(ENTITY->angle, &g_matrixScratch);
    ApplyMatrixSV(&g_matrixScratch, &g_svecScratch, &g_svecScratch);

    *(short*)((char*)ENTITY + 0xC6) =
        (short)(*(int*)&ENTITY->scaMatrixData.localMatrix.t[0]) - g_svecScratch.x;
    *(short*)((char*)ENTITY + 0xC8) =
        (short)(*(int*)&ENTITY->scaMatrixData.localMatrix.t[2]) - g_svecScratch.z;

    *(short*)((char*)player + 0xC6) = *(short*)((char*)ENTITY + 0xC6);
    *(short*)((char*)player + 0xC8) = *(short*)((char*)ENTITY + 0xC8);
}

void entity_apply_anim_vertex(void* entity, unsigned int animHeader, unsigned int animBase) { }

// ============================================================================
// joint_setup_attack_effect @ 0x0048a070
// Configures a joint for an attack special effect (blood, bite mark, etc.).
// Sets size parameters (0x28 standard or 0x30 for alt costumes), a timer
// at +0x70, effect type at +3, and frame match at +0x72. Also applies to
// the weapon-part joint if g_main_state_flags has bit 0 set.
// ============================================================================
void joint_setup_attack_effect(int joint, unsigned char effectType, unsigned short timer, unsigned short frameMatch)
{
    if ((*(unsigned char*)(joint + 2) & 0x80) != 0) return;

    int sizeVal = 0x28;
    unsigned char sizeB = 0x60;
    unsigned char sizeC = 0x28;

    // Larger effect size for alternate costumes (entity ID 3 or 4)
    if (*(char*)((char*)ENTITY + 1) == 3 || *(char*)((char*)ENTITY + 1) == 4) {
        sizeVal = 0x30;
        sizeB = 0x18;
        sizeC = 0x18;
    }

    // Apply effect to the main joint
    joint_enable_special_effect(joint, sizeB, sizeVal, sizeC);
    *(unsigned short*)(joint + 0x70) = timer;
    *(unsigned char*)(joint + 3) = effectType;
    *(unsigned short*)(joint + 0x72) = frameMatch;

    // Also apply to weapon-part joint if active
    if (((unsigned char)g_main_state_flags & 1) != 0) {
        int weaponJoint = (*(int*)((char*)ENTITY + 0xAC) - *(int*)&ENTITY->jointsStructs) + joint;
        joint_enable_special_effect(weaponJoint, sizeB, sizeVal, sizeC);
        *(unsigned char*)(weaponJoint + 3) = effectType;
        *(unsigned short*)(weaponJoint + 0x70) = timer;
        *(unsigned short*)(weaponJoint + 0x72) = frameMatch;
    }
}

void joint_enable_special_effect(int joint, unsigned char a, int b, unsigned char c) { }
void BillboardSetColor(void* mat, int a, int b, void* color) { }
char reduce_attack_time_by_btn_press(void) { return 0; }

unsigned int ChkOutsideCell(SVECTOR* pos, SVECTOR* dir, int bx, int bz) { return 0; }
unsigned int room_collision_check_0047db90(VECTOR* vec, unsigned int boundaryResult) { return 0; }
int player_distance_z = 0;

// ============================================================================
// entity_check_visual_range @ 0x0043bfa0
// Computes Euclidean distance from entity to player via SquareRoot0.
// If distance < range, sets status_flags bit 5 (0x20) — "player in visual range".
// ============================================================================
void entity_check_visual_range(unsigned int range)
{
    int dx = (int)g_playerEntity.scaMatrixData.localMatrix.t[0]
           - (int)ENTITY->scaMatrixData.localMatrix.t[0];
    int dz = (int)g_playerEntity.scaMatrixData.localMatrix.t[2]
           - (int)ENTITY->scaMatrixData.localMatrix.t[2];
    unsigned int distance = SquareRoot0(dx * dx + dz * dz);

    if (distance < range) {
        ENTITY->status_flags |= ENTITY_STATUS_PLAYER_ABOVE;
    }
}

// ============================================================================
// entity_check_alert_range @ 0x0043bfe0
// Computes Euclidean distance from entity to player via SquareRoot0.
// If distance < range, sets status_flags bit 7 (0x80) — "player in alert range".
// ============================================================================
void entity_check_alert_range(unsigned int range)
{
    int dx = (int)g_playerEntity.scaMatrixData.localMatrix.t[0]
           - (int)ENTITY->scaMatrixData.localMatrix.t[0];
    int dz = (int)g_playerEntity.scaMatrixData.localMatrix.t[2]
           - (int)ENTITY->scaMatrixData.localMatrix.t[2];
    unsigned int distance = SquareRoot0(dx * dx + dz * dz);

    if (distance < range) {
        ENTITY->status_flags |= ENTITY_STATUS_PLAYER_BELOW;
    }
}

// ============================================================================
// Dependency stubs (pending full decompilation)
// ============================================================================

// SetEntityScaHitData @ 0x0041b2c0 - stub
// ============================================================================
// SetEntityScaHitData @ 0x0041b2c0
// Converts the entity's local SCA collision points into world-space hit
// coordinates by rotating them around the Y-axis using the entity's angle.
// Iterates the SCA volume list (6 shorts per entry, terminated by negative
// first short), rotating each point so the collision/hit-check system can
// operate in world space.
// ============================================================================
void SetEntityScaHitData(Entity* ent)
{
    short* srcVol = *(short**)((char*)ent + 4);
    short* dstVol = *(short**)((char*)ent + 8);

    g_svecScratch.y = ent->angle;
    g_svecScratch.z = 0;
    g_svecScratch.x = 0;

    RotMatrix(&g_svecScratch, &g_matrixScratch);

    while (*srcVol >= 0) {
        SVECTOR localVertex;
        localVertex.x = srcVol[1];
        localVertex.z = srcVol[3];
        localVertex.y = srcVol[2];

        SVECTOR worldVertex;
        ApplyMatrix(&g_matrixScratch, &localVertex, &worldVertex);

        dstVol[0] = worldVertex.x;
        dstVol[1] = srcVol[2];
        dstVol[2] = worldVertex.z;

        dstVol += 3;
        srcVol += 6;
    }
}

// ResolveEntityScaCollision @ 0x0041b0a0 - stub
// ============================================================================
// ResolveEntityScaCollision @ 0x0041b0a0
// Resolves SCA (Sphere/Cylinder Area) collision between two entities.
// Iterates both entities' SCA volume lists, checks each pair for overlap
// using Euclidean distance + SquareRoot0, and pushes the second entity
// away from the first by the penetration depth. Returns 1 if collision
// occurred, 0 otherwise.
// ============================================================================
unsigned int ResolveEntityScaCollision(Entity* entA, Entity* entB)
{
    if (entB->state == 4) return 0;       // eating/headless state — skip collision
    if ((entA->status_flags | entB->status_flags) & 2) return 0;  // one is deactivated

    short* volAStart = *(short**)((char*)entA + 4);   // SCA volume list start
    short* volAEnd   = *(short**)((char*)entA + 8);   // SCA volume list end
    unsigned char hitFlag = 0;

    while (*volAStart >= 0) {                          // terminate on negative first short
        short* volBStart = *(short**)((char*)entB + 4);
        short* volBEnd   = *(short**)((char*)entB + 8);

        while (*volBStart >= 0) {
            int dx = ((int)volBStart[0] - (int)volAStart[0])
                   - *(int*)&entA->scaMatrixData.localMatrix.t[0]
                   + *(int*)&entB->scaMatrixData.localMatrix.t[0];
            int dz = ((int)volBStart[2] - (int)volAStart[2])
                   - *(int*)&entA->scaMatrixData.localMatrix.t[2]
                   + *(int*)&entB->scaMatrixData.localMatrix.t[2];

            unsigned short radiusA = volAStart[5];  // cylinder radius
            unsigned short radiusB = volBStart[5];
            unsigned short heightA = volAStart[4];  // cylinder half-height
            unsigned short heightB = volBStart[4];

            int dist = SquareRoot0(dz * dz + dx * dx);
            int penetration = (unsigned int)(radiusA + radiusB) - (dist + 1);

            if (penetration > 0) {
                int dy = (int)volBStart[1]
                       + (*(int*)&entA->scaMatrixData.localMatrix.t[1] - (int)volAStart[1])
                       - *(int*)&entB->scaMatrixData.localMatrix.t[1];

                int maxHeight = (unsigned int)(unsigned short)heightA
                              + (unsigned int)(unsigned short)heightB;

                if (-maxHeight < dy && dy < maxHeight) {
                    int pushX = (penetration * dx) / (dist + 1);
                    int pushZ = (penetration * dz) / (dist + 1);

                    int dy2 = (int)volBStart[1]
                            + ((int)entA->position.y - (int)volAStart[1])
                            - *(int*)&entB->scaMatrixData.localMatrix.t[1];

                    if (dy2 <= -maxHeight || maxHeight <= dy2) {
                        int posXA = *(int*)&entA->scaMatrixData.localMatrix.t[0];
                        if ((entB->position.x < posXA && posXA < *(int*)&entB->scaMatrixData.localMatrix.t[0])
                         || (posXA < entB->position.x && *(int*)&entB->scaMatrixData.localMatrix.t[0] < posXA)) {
                            if (-pushX < 1)
                                pushX = -(-pushX + (unsigned int)(unsigned short)radiusA * 2);
                            else
                                pushX = (unsigned int)(unsigned short)radiusA * 2 + pushX;
                        }
                        int posZA = *(int*)&entA->scaMatrixData.localMatrix.t[2];
                        if ((entB->position.z < posZA && posZA < *(int*)&entB->scaMatrixData.localMatrix.t[2])
                         || (posZA < entB->position.z && *(int*)&entB->scaMatrixData.localMatrix.t[2] < posZA)) {
                            if (-pushZ < 1)
                                pushZ = -(-pushZ + (unsigned int)(unsigned short)radiusA * 2);
                            else
                                pushZ = (unsigned int)(unsigned short)radiusA * 2 + pushZ;
                        }
                    }

                    *(int*)&entB->scaMatrixData.localMatrix.t[0] += pushX;
                    *(int*)&entB->scaMatrixData.localMatrix.t[2] += pushZ;
                    hitFlag = 1;
                }
            }

            volBStart += 3;       // next volume: advance 3 shorts (6 bytes)
        }

        volAStart += 3;           // next volume: advance 3 shorts (6 bytes)
    }

    return hitFlag;
}

// HandleEnemyPlayerCollisions @ 0x00489e10 - stub
// ============================================================================
// HandleEnemyPlayerCollisions @ 0x00489e10
// Resolves SCA collisions between all active enemies and the current ENTITY,
// computing a combined hit flag. Also handles Yawn-specific player pushback:
// if player moved >450 units and Yawn (ID 13/18) is active, pushes the player
// back by 1/4 of the displacement.
// ============================================================================
unsigned int HandleEnemyPlayerCollisions(void)
{
    unsigned char hitFlag = 0;
    Entity* enemies = g_EnemiesList;
    signed char count = g_enemy_count;

    while (count != 0) {
        if (enemies->status_flags != 0 && enemies != ENTITY) {
            hitFlag |= (unsigned char)ResolveEntityScaCollision(enemies, ENTITY);
        }
        count--;
        enemies = (Entity*)((char*)enemies + sizeof(Entity));
    }

    int dx = (int)g_playerEntityPointer.scaMatrixData.localMatrix.t[0]
           - (int)g_playerEntityPointer.position.x;
    int dz = (int)g_playerEntityPointer.scaMatrixData.localMatrix.t[2]
           - (int)g_playerEntityPointer.position.z;

    int absDx = (dx ^ (dx >> 31)) - (dx >> 31);
    int absDz = (dz ^ (dz >> 31)) - (dz >> 31);
    g_playerDisplacement = absDx - (dz >> 31) + absDz;

    if (g_playerDisplacement > 450
        && (g_playerEntityPointer.flags & 2) == 0
        && (g_EnemiesList[0].id == 13 || g_EnemiesList[0].id == 18))
    {
        g_playerDisplacement = dx >> 4;
        player_distance_z = dz >> 4;
        g_playerEntityPointer.scaMatrixData.localMatrix.t[0] =
            g_playerEntityPointer.position.x + g_playerDisplacement;
        g_playerEntityPointer.scaMatrixData.localMatrix.t[2] =
            g_playerEntityPointer.position.z + player_distance_z;
    }

    return hitFlag;
}

// check_room_collision @ 0x0047d310 - stub
unsigned char check_room_collision(VECTOR* pos, short radius) { return 0; }

// FUN_0047d6f0 @ 0x0047d6f0 - floor/boundary collision stub
unsigned char FUN_0047d6f0(short* a, short* b) { return 0; }

// blood_splatter_physics @ 0x00437d20 - splatter/blood effect stub
// ============================================================================
// blood_splatter_physics @ 0x00437d20
// Blood drop physics after a hit. Moves the blood joint downward with
// gravity, checks room collision for wall/floor hits, creates blood
// billboards at impact points, plays impact SFX, and decrements the
// speed parameter. Called from zombie_update for the hand joint.
// ============================================================================
void blood_splatter_physics(int jointData, short gravityStep)
{
    if (*(int*)(jointData + 0x5C) >= -100 && (*(unsigned char*)(jointData + 3) & 0x1F) >= 6)
        return;

    unsigned char jointFlag = *(unsigned char*)(jointData + 3);
    unsigned char animFrame = jointFlag & 0x1F;

    SVECTOR splatterDir;
    splatterDir.z = 0x40;
    splatterDir.y = (6 - animFrame) * 0x10;
    splatterDir.x = (6 - animFrame) * 8;

    RotMatrix(&splatterDir, &g_matrixScratch);
    MulMatrix((MATRIX*)(jointData + 0x44), &g_matrixScratch);

    g_matrixScratch = g_identityMatrixData;
    RotMatrixY(ENTITY->angle, &g_matrixScratch);

    VECTOR* jointPos = (VECTOR*)(jointData + 0x58);
    SVECTOR local_18;
    ApplyMatrixSV(&g_matrixScratch, (SVECTOR*)(jointData + 4), &local_18);

    int savedX = jointPos->x;
    int savedZ = *(int*)(jointData + 0x60);

    jointPos->x += (1 - (unsigned int)((jointFlag & 0x40) >> 5)) * (int)local_18.x;
    *(int*)(jointData + 0x60) += (1 - (unsigned int)((jointFlag & 0xBF) >> 6)) * (int)local_18.z;

    g_svecScratch.z = 0; g_svecScratch.y = 0; g_svecScratch.x = 0;
    short collision = room_collision_check_0047da50(jointPos, (VECTOR*)&g_svecScratch);

    if (collision != 0) {
        jointFlag ^= 0x40;
        jointPos->x = savedX;
        *(int*)(jointData + 0x60) = savedZ;
        *(unsigned char*)(jointData + 3) = jointFlag;

        jointPos->x = (1 - (unsigned int)((jointFlag & 0x40) >> 5)) * (int)local_18.x + savedX;
        *(int*)(jointData + 0x60) = (1 - (unsigned int)((jointFlag & 0xBF) >> 6)) * (int)local_18.z + savedZ;

        collision = room_collision_check_0047da50(jointPos, (VECTOR*)&g_svecScratch);
        if (collision != 0) {
            *(unsigned char*)(jointData + 3) ^= 0xC0;
        }

        jointPos->x = savedX;
        *(int*)(jointData + 0x60) = savedZ;
        *(short*)(jointData + 4) >>= 1;

        VECTOR zero = { 0, 0, 0, 0 };
        Effect_CreateBillboard(0, 0, 0, (void*)(jointData + 0x44), &zero, 0);
    }

    short accel = *(short*)(jointData + 6) - (unsigned short)*(unsigned char*)(jointData + 2) * gravityStep;
    *(short*)(jointData + 6) = accel;
    *(int*)(jointData + 0x5C) -= (int)accel;

    if (*(int*)(jointData + 0x5C) > -0x65) {
        *(int*)(jointData + 0x5C) = -0x63;  // -99
        *(unsigned char*)(jointData + 2) = 0;
        *(short*)(jointData + 6) = -accel;
        *(unsigned char*)(jointData + 3) += 1;
        *(short*)(jointData + 4) += 0x28;
        *(short*)(jointData + 6) = -accel >> 2;

        VECTOR zero = { 0, 0, 0, 0 };
        Effect_CreateBillboard(0, 0, 0, (void*)(jointData + 0x44), &zero, 0);
        Snd_em(8);
    }

    if (*(short*)(jointData + 4) > 0)
        *(short*)(jointData + 4) = 0;

    *(unsigned char*)(jointData + 2) += 1;
}

// entity_add_fade_sprite @ 0x00456810 - physics velocity stub
// ============================================================================
// entity_add_fade_sprite @ 0x00456810
// Adds the entity to the fade sprite render queue for the current frame.
// Rotates position to entity angle, applies camera matrix, and stores
// the sprite in g_FadeSpr for later rendering by the sprite system.
// ============================================================================
void entity_add_fade_sprite(VECTOR* pos, short* velocity, short yOffset, short angle)
{
    MATRIX local_40, local_20;
    local_40 = g_identityMatrixData;

    local_40.t[1] = (int)yOffset;
    local_40.t[0] = (int)*velocity + pos->x;
    local_40.t[2] = (int)velocity[2] + pos->z;

    RotMatrixY((int)angle, &local_40);
    ApplyLVAndMul0Matrix(&g_RoomCameraData, &local_40, &local_20);
    SetRotAndTransMatrix(&local_20);
    SetGlobalScaledRotationMatrix(&local_20);

    // Fade sprite buffer write — stubbed (renderer dependency)
    // Original stores pos/velocity/angle/timing in g_FadeSpr[g_FadeSprCount++]
}

// FUN_004565f0 @ 0x004565f0 - SCA init helper stub
void FUN_004565f0(SVECTOR* a, SVECTOR* b, int c, int d) { }

// set_next_entity_data_buffer @ 0x00488f90 - stub
void set_next_entity_data_buffer(int count) { }

// FUN_0048bd00 @ 0x0048bd00 - lighting check stub
unsigned char FUN_0048bd00(void* light, unsigned char param2, int param3) { return 0; }

// FUN_0048bda0 @ 0x0048bda0 - lighting response stub
void FUN_0048bda0(void) { }

// FUN_0048c0d0 @ 0x0048c0d0 - pre-flip setup stub
void FUN_0048c0d0(void) { }

// FlipSprite @ 0x00460610 - stub
void FlipSprite(int light, MATRIX* out, unsigned char param3, int param4) { }

// Matrix_MulMatrix @ 0x0040a2e0 - stub (may already exist in GteMatrix.cpp)
void Matrix_MulMatrix(MATRIX* a, MATRIX* b) { }
