#pragma once
#include "Types.h"

// ============================================================================
// AnimSlot (0x1c bytes each, inside EMD file data)
// After ResolveAnimPointers resolves relative offsets to absolute pointers.
// ============================================================================
#pragma pack(push, 1)
struct AnimSlot {
    void* data0;            // +0x00: pointer to animation data block 0
    int   pad_04;           // +0x04
    void* data1;            // +0x08: pointer to animation data block 1
    int   pad_0c;           // +0x0C
    void* data2;            // +0x10: pointer to animation data block 2 (TMD/texture data)
    int   entryCount;       // +0x14: number of entries in data2
    int   pad_18;           // +0x18
};
#pragma pack(pop)
static_assert(sizeof(AnimSlot) == 0x1c, "AnimSlot size mismatch");

// ============================================================================
// AnimDataHeader - Layout of the animation data header pointed to by
// Entity.modelLoadBuffer. The slots[] array starts at offset +0x0C.
// ============================================================================
#pragma pack(push, 1)
struct AnimDataHeader {
    int            unknown_00;    // +0x00
    unsigned char  resolved;      // +0x04: bit 0 = pointers resolved
    unsigned char  pad_05[3];     // +0x05
    int            slotCount;     // +0x08
    AnimSlot       slots[];       // +0x0C
};
#pragma pack(pop)

// ============================================================================
// PlayerEntity (0x00be62e4) - 0x180 bytes
// Main player entity. Shares base layout with Entity up to 0x6C.
// ============================================================================
#pragma pack(push, 1)
struct PlayerEntity {
    // ---- Header (0x00 - 0x0B) ----
    unsigned char  flags;               // 0x00
    unsigned char  id;                  // 0x01
    unsigned char  equippedWeaponId;    // 0x02
    unsigned char  unk_03;              // 0x03
    unsigned int   Sca_info;            // 0x04
    unsigned int   pSca_hit_data;       // 0x08

    // ---- SCA data + model (0x0C - 0x1F) ----
    short          unk_0c;              // 0x0C
    short          unk_0e;              // 0x0E
    unsigned char  unk_10;              // 0x10
    unsigned char  unk_11;              // 0x11
    unsigned char  unk_12;              // 0x12
    unsigned char  unk_13;              // 0x13
    unsigned int   modelLoadBuffer;     // 0x14
    unsigned int   unk_18;              // 0x18
    ScaMatrixData* scaMatrixData;       // 0x1C

    // ---- Transform MATRIX (0x20 - 0x3F) ----
    MATRIX         transform;           // 0x20 (32 bytes)

    // ---- Post-transform gap (0x40 - 0x6B) ----
    int            t_pad;               // 0x40
    unsigned char  pad_44[0x28];        // 0x44-0x6B

    // ---- Position + movement (0x6C - 0x83) ----
    SVECTOR        position;            // 0x6C (8 bytes)
    short          directionAngle;      // 0x74
    SVECTOR        speed;               // 0x76 (8 bytes)
    unsigned int   unk_7e;              // 0x7E
    unsigned short unk_82;              // 0x82

    // ---- State (0x84 - 0x8F) ----
    unsigned char  animationId;         // 0x84
    unsigned char  animFrameId;         // 0x85
    unsigned char  anim_86;             // 0x86
    unsigned char  anim_87;             // 0x87
    short          health;              // 0x88
    unsigned char  isBeingAttackedFlag; // 0x8A
    unsigned char  unk_8b;              // 0x8B
    unsigned char  unk_8c;              // 0x8C
    unsigned char  jointCount;          // 0x8D
    unsigned short unk_8e;              // 0x8E

    // ---- Model/animation pointers (0x90 - 0xA3) ----
    unsigned int   animHeader;          // 0x90
    unsigned int   animBase;            // 0x94
    JointStruct*   jointsStructs;       // 0x98
    unsigned int   weaponPartAnimSlot;  // 0x9C
    unsigned int   weaponPartAnimObject;// 0xA0

    // ---- Padding + combat state (0xA4 - 0xCB) ----
    unsigned char  pad_a4[0x14];        // 0xA4-0xB7
    unsigned int   unk_b8;              // 0xB8
    unsigned char  unk_bc;              // 0xBC
    unsigned char  attackAnim;          // 0xBD
    unsigned char  unk_be;              // 0xBE
    unsigned char  unk_bf;              // 0xBF
    unsigned char  unk_c0;              // 0xC0
    unsigned char  unk_c1;              // 0xC1
    unsigned short unk_c2;              // 0xC2
    unsigned short attackDirection;     // 0xC4
    unsigned short unk_c6;              // 0xC6
    unsigned short unk_c8;              // 0xC8
    unsigned short unk_ca;              // 0xCA

    // ---- Status (0xCC - 0xE3) ----
    unsigned char  pad_cc[0x0C];        // 0xCC-0xD7
    unsigned char  unk_d8;              // 0xD8
    unsigned char  pad_d9[2];           // 0xD9-0xDA
    unsigned char  unk_db;              // 0xDB
    unsigned char  healthStatusFlags;   // 0xDC
    unsigned char  unk_dd;              // 0xDD
    unsigned short unk_de;              // 0xDE
    unsigned short unk_e0;              // 0xE0
    unsigned short attackTimer;         // 0xE2

    // ---- Physics (0xE4 - 0xEB) ----
    SVECTOR        pushVelocity;        // 0xE4

    // ---- Large padding (0xEC - 0x15B) ----
    unsigned char  pad_ec[0x70];        // 0xEC-0x15B

    // ---- Joint movement data (0x15C - 0x173) ----
    unsigned int   jointMoveData0;      // 0x15C
    unsigned int   jointMoveData1;      // 0x160
    unsigned int   jointMoveData2;      // 0x164
    unsigned int   jointMoveData3;      // 0x168
    DWORD          emdScratchPtr1;      // 0x16C
    DWORD          emdScratchPtr2;      // 0x170

    // ---- Tail (0x174 - 0x17F) ----
    unsigned char  pad_174;             // 0x174
    unsigned char  maxHealth;           // 0x175
    unsigned char  pad_176[10];         // 0x176-0x17F
};
#pragma pack(pop)
static_assert(sizeof(PlayerEntity) == 0x180, "PlayerEntity size mismatch");

// ============================================================================
// Entity (0x00be6464) - 0x18C bytes
// Generic entity (enemies, player when cast). Shares base layout with
// PlayerEntity up to 0x6C.
// ============================================================================
#pragma pack(push, 1)
struct Entity {
    // ---- Header (0x00 - 0x0B) ----
    unsigned char  status_flags;        // 0x00
    unsigned char  id;                  // 0x01
    unsigned char  behavior_flags;      // 0x02
    unsigned char  has_enter_switch_zone; // 0x03
    unsigned int   Sca_info;            // 0x04
    unsigned int   pSca_hit_data;       // 0x08

    // ---- SCA data + model (0x0C - 0x1F) ----
    short          unk_0c;              // 0x0C
    short          unk_0e;              // 0x0E
    unsigned char  unk_10;              // 0x10
    unsigned char  unk_11;              // 0x11
    unsigned char  unk_12;              // 0x12
    unsigned char  unk_13;              // 0x13
    unsigned int   modelLoadBuffer;     // 0x14
    unsigned int   unk_18;              // 0x18
    ScaMatrixData* scaMatrixData;       // 0x1C

    // ---- Transform MATRIX (0x20 - 0x3F) ----
    MATRIX         transform;           // 0x20 (32 bytes)

    // ---- Post-transform gap (0x40 - 0x6B) ----
    int            t_pad;               // 0x40
    unsigned char  pad_44[0x28];        // 0x44-0x6B

    // ---- Position + movement (0x6C - 0x83) ----
    SVECTOR        position;            // 0x6C (8 bytes)
    int            angle;               // 0x74
    SVECTOR        speed;               // 0x78 (8 bytes)
    short          move_step_x;         // 0x80 - movement step X (used by SCD event state 2)
    short          move_step_z;         // 0x82 - movement step Z (used by SCD event state 2)

    // ---- State (0x84 - 0x8F) ----
    unsigned char  state;               // 0x84
    unsigned char  ignore_player_flag;  // 0x85
    unsigned char  action_behavior;     // 0x86
    unsigned char  action_state;        // 0x87
    short          health;              // 0x88
    unsigned char  field_0x8a;          // 0x8A
    unsigned char  pad_8b;              // 0x8B
    unsigned char  blend_counter;       // 0x8C
    unsigned char  jointCount;          // 0x8D
    unsigned char  pad_8e[2];           // 0x8E-0x8F

    // ---- Model/animation pointers (0x90 - 0xA3) ----
    unsigned int   animHeader;          // 0x90
    unsigned int   animBase;            // 0x94
    JointStruct*   jointsStructs;       // 0x98
    unsigned int   weaponPartAnimSlot;  // 0x9C
    unsigned int   weaponPartAnimObject;// 0xA0

    // ---- Padding (0xA4 - 0xBC) ----
    unsigned char  pad_a4[8];           // 0xA4-0xAB
    unsigned int   weaponJointsPtr;     // 0xAC - pointer to weapon part joint data
    // ---- SCD event data (0xB0 - 0xBC) ----
    unsigned char  pad_b0[8];           // 0xB0-0xB7
    unsigned int   scd_target_ptr;      // 0xB8 - SCD event target pointer (entity/item/desk)
    unsigned char  pad_bc;              // 0xBC

    // ---- Animation fields (0xBD - 0xBF) ----
    unsigned char  animationId;         // 0xBD
    unsigned char  animation_frame_id;  // 0xBE
    unsigned char  timing_control;      // 0xBF

    // ---- Empty gap (0xC0 - 0xC3) ----
    unsigned char  pad_c0[2];           // 0xC0-0xC1
    unsigned short unk_c2;              // 0xC2 - movement speed value (used by Add_speedXZ)

    // ---- Tick counter (0xC4) ----
    unsigned char  action_ticks_counter;// 0xC4

    // ---- Position offsets for animation (0xC5 - 0xDC) ----
    unsigned char  pad_c5;              // 0xC5
    unsigned short unk_c6;              // 0xC6 - base X position offset for animation
    unsigned short unk_c8;              // 0xC8 - base Z position offset for animation
    // ---- SCD event movement data (0xCA - 0xDB) ----
    unsigned char  pad_ca[2];           // 0xCA-0xCB
    int            scd_pos_x;           // 0xCC - SCD event target position X
    int            scd_pos_y;           // 0xD0 - SCD event target position Y
    int            scd_pos_z;           // 0xD4 - SCD event target position Z
    unsigned char  scd_behavior_type;   // 0xD8 - SCD event behavior type (0x93 = target entity)
    unsigned char  scd_step_size;       // 0xD9 - SCD event step/movement size
    unsigned char  scd_step_flags;      // 0xDA - SCD event step flags
    unsigned char  scd_anim_param;      // 0xDB - SCD event animation parameter
    unsigned char  collisionFlags;      // 0xDC - collision callback flags (bit 3 = wall push)

    // ---- Joint init flag (0xDD) ----
    unsigned char  jointResetFlag;      // 0xDD

    // ---- SCD event timing (0xDE - 0xE1) ----
    unsigned char  scd_timer_lo;        // 0xDE - SCD event timer low byte
    unsigned char  scd_timer_hi;        // 0xDF - SCD event timer high byte
    unsigned short scd_entity_flags;    // 0xE0 - SCD event entity flags

    // ---- Timer (0xE2) ----
    unsigned char  next_turn_timer;     // 0xE2

    // ---- Pad to physics (0xE3) ----
    unsigned char  pad_e3;              // 0xE3

    // ---- Physics (0xE4 - 0xEB) ----
    SVECTOR        pushVelocity;        // 0xE4

    // ---- Large padding (0xEC - 0x15B) ----
    unsigned char  pad_ec[0x70];        // 0xEC-0x15B

    // ---- Data buffer + player tracking (0x15C - 0x16B) ----
    unsigned char  some_data_buffer_0x15c; // 0x15C
    unsigned char  pad_15d[9];          // 0x15D-0x165
    unsigned char  player_pos_x;        // 0x166
    unsigned char  pad_167;             // 0x167
    unsigned char  player_pos_z;        // 0x168
    unsigned char  pad_169[3];          // 0x169-0x16B

    // ---- Extended movement state (0x16C - 0x173) ----
    unsigned char  attacking_direction; // 0x16C
    unsigned char  dir_control_flags;   // 0x16D
    unsigned char  texBank;             // 0x16E
    unsigned char  pad_16f;             // 0x16F
    unsigned char  angle_turn_delta;    // 0x170
    unsigned char  pad_171;             // 0x171
    unsigned char  is_moving;           // 0x172
    unsigned char  pad_173;             // 0x173

    // ---- Extended combat state (0x174 - 0x18B) ----
    unsigned char  splatter_flag;       // 0x174
    unsigned char  pad_175[2];          // 0x175-0x176
    unsigned char  attacking_timer;     // 0x177
    unsigned char  pad_178[2];          // 0x178-0x179
    unsigned char  movement_distance;   // 0x17A
    unsigned char  pad_17b;             // 0x17B
    unsigned char  flags_0x17c;         // 0x17C
    unsigned char  behavior_type_0x17d; // 0x17D
    unsigned char  flags_0x17e;         // 0x17E
    unsigned char  pad_17f[2];          // 0x17F-0x180
    unsigned char  turn_speed;          // 0x181
    unsigned char  internal_timer;      // 0x182
    unsigned char  pad_183;             // 0x183
    unsigned char  state_mirror;        // 0x184
    unsigned char  ignore_player_flag_mirror; // 0x185
    unsigned char  action_behavior_mirror;    // 0x186
    unsigned char  attack_behavior_mirror;    // 0x187
    unsigned char  pad_188[4];          // 0x188-0x18B
};
#pragma pack(pop)
static_assert(sizeof(Entity) == 0x18C, "Entity size mismatch");
