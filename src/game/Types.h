#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <cstdio>
#include <d3d11.h>
#include "../marni/MarniBits.h"
#include "../marni/Marni3DObject.h"
#include "../marni/MarniInput.h"

struct TextureDesc;
struct Entity;

// PS1 GTE matrix type (0x00ac93b0 layout, 32 bytes)
struct MATRIX {
    short m[3][3];  // 0x00: Rotation matrix (9 x short = 18 bytes)
    short _pad;     // 0x12: Padding for int alignment
    int t[3];       // 0x14: Translation vector (3 x int = 12 bytes)
};

// PSQY short vector type
struct SVECTOR {
    short x, y, z;
    short pad;
};

// PSYQ - Character vector
struct CVECTOR {
    short r, g, b;  // Color palette
    short cd;       // GPU code
};

// 32-bit integer vector
struct VECTOR {
    int x, y, z;
    int pad;
};

struct POLY_F4 {
    unsigned int tag; // Next primitive pointer + size (OT tag)
    unsigned char r0, g0, b0; // RGB color values
    unsigned char code; // Primitive ID (reserved)
    short x0, y0; // Vertex coordinates
    short x1, y1; // Vertex coordinates
    short x2, y2; // Vertex coordinates
    short x3, y3; // Vertex coordinates
}; 

// Forward declarations for circular references
struct JointStruct;
struct ScaMatrixData;

// ============================================================================
// JointStruct (0x7C bytes) - Per-joint animation/transform data
// ============================================================================
#pragma pack(push, 1)
struct JointStruct {
    unsigned char  flags;              // 0x00 - bit 0x10 = skip rotation; 3=active
    unsigned char  index;              // 0x01 - joint index number
    unsigned char  field_02;           // 0x02
    unsigned char  pad_03;             // 0x03
    SVECTOR        rotation;           // 0x04 - rotation angles
    int            anim_field;         // 0x0C - anim sub-struct base
    void*          data_ptr;           // 0x10 - points to &scale_flag
    int            anim_slot_ptr;      // 0x14
    void*          anim_object;        // 0x18
    int            field_1c;           // 0x1C
    int            scale_flag;         // 0x20 - set to 1
    MATRIX         transform;          // 0x24 - current joint transform
    MATRIX         world;              // 0x44 - composite world-space matrix
    int            unk_64;             // 0x64
    int            unk_68;             // 0x68
    int            unk_6c;             // 0x6C
    short          velX;               // 0x70 - X velocity / flags (upper byte = bounce flag 0x80)
    short          velY;               // 0x72 - Y velocity
    short          velZ;               // 0x74 - Z velocity / rotation damping
    short          rotDeltaX;           // 0x76 - rotation delta X
    short          rotDeltaY;           // 0x78 - rotation delta Y
    short          rotDeltaZ;           // 0x7A - rotation delta Z
};
#pragma pack(pop)
static_assert(sizeof(JointStruct) == 0x7C, "JointStruct size mismatch");

// ============================================================================
// ScaMatrixData (0x50 bytes)
// ============================================================================
struct ScaMatrixData {
    unsigned int   field_00;          // 0x00 - zeroed
    MATRIX         localMatrix;       // 0x04 - identity x 0x1000, zero translation
    MATRIX         worldMatrix;       // 0x24 - identity x 0x1000, zero translation
    unsigned int   field_44;          // 0x44 - untouched by InitScaMatrix
    unsigned int   owner;             // 0x48 - back-pointer to owning entity (NULL for player)
    unsigned int   field_4c;          // 0x4C - zeroed
};
static_assert(sizeof(ScaMatrixData) == 0x50, "ScaMatrixData size mismatch");

// ============================================================================
// Character IDs (0x00be9823 / 0x00be984b area)
// ============================================================================
#define CHAR_CHRIS      0
#define CHAR_JILL       1
#define CHAR_REBECCA    3

// ============================================================================
// Item IDs - Inventory items (0x00be9944 area, ItemSlot.Id field)
// ============================================================================
#define ITEM_NONE               0x00

// Weapons (0x01-0x0A)
#define ITEM_KNIFE              0x01
#define ITEM_BERETTA            0x02
#define ITEM_SHOTGUN            0x03
#define ITEM_COLT_PYTHON_DUM    0x04
#define ITEM_COLT_PYTHON_MAG    0x05
#define ITEM_FLAMETHROWER       0x06
#define ITEM_BAZOOKA_EXPLOSIVE  0x07
#define ITEM_BAZOOKA_ACID       0x08
#define ITEM_BAZOOKA_FLAME      0x09
#define ITEM_ROCKET_LAUNCHER    0x0A

// Ammunition (0x0B-0x12)
#define ITEM_CLIP               0x0B
#define ITEM_SHELLS             0x0C
#define ITEM_DUM_DUM_ROUNDS     0x0D
#define ITEM_MAGNUM_ROUNDS      0x0E
#define ITEM_FUEL               0x0F
#define ITEM_EXPLOSIVE_ROUNDS   0x10
#define ITEM_ACID_ROUNDS        0x11
#define ITEM_FLAME_ROUNDS       0x12

// Chemicals & Fluids (0x13-0x1B)
#define ITEM_EMPTY_BOTTLE       0x13
#define ITEM_BOTTLE_WATER       0x14
#define ITEM_UMB_NO2            0x15
#define ITEM_UMB_NO4            0x16
#define ITEM_UMB_NO7            0x17
#define ITEM_UMB_NO13           0x18
#define ITEM_YELLOW6            0x19
#define ITEM_NP003              0x1A
#define ITEM_VJOLT              0x1B

// Quest Items (0x1C-0x2E)
#define ITEM_BROKEN_SHOTGUN     0x1C
#define ITEM_CRANK_SQUARE       0x1D
#define ITEM_CRANK_HEX          0x1E
#define ITEM_EMBLEM             0x1F
#define ITEM_GOLD_EMBLEM        0x20
#define ITEM_BLUE_JEWEL         0x21
#define ITEM_RED_JEWEL          0x22
#define ITEM_MUSIC_NOTES        0x23
#define ITEM_WOLF_MEDAL         0x24
#define ITEM_EAGLE_MEDAL        0x25
#define ITEM_CHEMICAL           0x26
#define ITEM_BATTERY            0x27
#define ITEM_MO_DISK            0x28
#define ITEM_WIND_CREST         0x29
#define ITEM_FLARE              0x2A
#define ITEM_SLIDES             0x2B
#define ITEM_MOON_CREST         0x2C
#define ITEM_STAR_CREST         0x2D
#define ITEM_SUN_CREST          0x2E

// Utility Items (0x2F-0x32)
#define ITEM_INK_RIBBONS        0x2F
#define ITEM_LIGHTER            0x30
#define ITEM_LOCK_PICK          0x31
#define ITEM_OIL                0x32

// Keys (0x33-0x3D)
#define ITEM_SWORD_KEY          0x33
#define ITEM_ARMOR_KEY          0x34
#define ITEM_SHIELD_KEY         0x35
#define ITEM_HELMET_KEY         0x36
#define ITEM_LAB_KEY_A          0x37
#define ITEM_SPECIAL_KEY        0x38
#define ITEM_DORMITORY_KEY_A    0x39
#define ITEM_DORMITORY_KEY_B    0x3A
#define ITEM_CROOM_KEY          0x3B
#define ITEM_LAB_KEY_B          0x3C
#define ITEM_DESK_KEY           0x3D

// Books (0x3E-0x40)
#define ITEM_RED_BOOK           0x3E
#define ITEM_DOOM_BOOK2         0x3F
#define ITEM_DOOM_BOOK1         0x40

// Healing (0x41-0x4B)
#define ITEM_FIRST_AID_SPRAY    0x41
#define ITEM_SERUM              0x42
#define ITEM_RED_HERB           0x43
#define ITEM_GREEN_HERB         0x44
#define ITEM_BLUE_HERB          0x45
#define ITEM_MIX_BLUE_RED       0x46
#define ITEM_MIX_2GREEN         0x47
#define ITEM_MIX_GREEN_BLUE     0x48
#define ITEM_MIX_GREEN_RED_BLUE 0x49
#define ITEM_MIX_3GREEN         0x4A
#define ITEM_MIX_2GREEN_RED     0x4B

// Misc (0x4C-0x4F)
#define ITEM_PICK_AXE           0x4C
#define ITEM_COMM_RADIO         0x4D
#define ITEM_BOTTLE_WATER2      0x4E
#define ITEM_EAGLE_WOLF_BOOK    0x4F

// ============================================================================
// RDT Light structure (0x14 / 20 bytes each)
// ============================================================================
struct RDT_Light {
    int            pos_x;       // 0x00
    int            pos_y;       // 0x04
    int            pos_z;       // 0x08
    unsigned char  red;         // 0x0C
    unsigned char  green;       // 0x0D
    unsigned char  blue;        // 0x0E
    unsigned char  zero1;       // 0x0F
    unsigned char  zero2;       // 0x10
    unsigned char  zero3;       // 0x11
    short          radius;      // 0x12
};
static_assert(sizeof(RDT_Light) == 0x14, "RDT_Light size mismatch");

// ============================================================================
// RDT Camera structure (0x2C / 44 bytes each)
// ============================================================================
struct RDT_Camera {
    int   mask_pointer;         // 0x00
    int   tim_mask_pointer;     // 0x04
    int   cam_from_x;           // 0x08
    int   cam_from_y;           // 0x0C
    int   cam_from_z;           // 0x10
    int   cam_to_x;             // 0x14
    int   cam_to_y;             // 0x18
    int   cam_to_z;             // 0x1C
    int   roll;                 // 0x20
    int   zero;                 // 0x24
    int   fov;                  // 0x28
};
static_assert(sizeof(RDT_Camera) == 0x2C, "RDT_Camera size mismatch");

// ============================================================================
// RDT (Room Definition Table) - 0x94 byte header + variable data
// ============================================================================
#pragma pack(push, 1)
struct RDT {
    unsigned char  sprites_count;       // 0x00 - number of room sprite entries in g_RoomSprEntries
    unsigned char  cameras_count;       // 0x01
    unsigned char  sound_banks_count;   // 0x02
    unsigned char  unknown_03[3];       // 0x03-0x05
    short          ambient_light_r;     // 0x06
    short          ambient_light_g;     // 0x08
    short          ambient_light_b;     // 0x0A
    RDT_Light      lights[3];           // 0x0C-0x47
    unsigned char* cam_switch_zones;    // 0x48
    unsigned char* boundaries;          // 0x4C
    unsigned char* items_models;        // 0x50
    unsigned char* obstacles_models;    // 0x54
    unsigned char* unknown_58;          // 0x58
    unsigned char* footstep_sound_zones;// 0x5C
    unsigned char* initialization_scd;  // 0x60
    unsigned char* scd_opcodes;         // 0x64
    unsigned char* scd_opcodes2;        // 0x68
    unsigned char* unknown_6c;          // 0x6C
    unsigned char* unknown_70;          // 0x70
    unsigned char* messages;            // 0x74
    unsigned char* unknown_78;          // 0x78
    unsigned char* effect_anim_index;   // 0x7C
    unsigned char* effect_anim_data;    // 0x80
    unsigned char* effect_anim_sprite;  // 0x84
    unsigned char* sound_attribute_table;// 0x88
    unsigned char* vab_header_file;     // 0x8C
    unsigned char* vab_sound_file;      // 0x90
};
#pragma pack(pop)
static_assert(sizeof(RDT) == 0x94, "RDT header size mismatch");

// ============================================================================
// ScdEventEntry (0x34 bytes) - SCD event execution context
// One entry per active room event script. 8-slot table at 0x00bf084c.
// Initialized by FUN_0041d620, executed by room_events_check.
// ============================================================================
#pragma pack(push, 1)
struct ScdEventEntry {
    unsigned char   state;              // 0x00: execution state (0=idle, 1=wait anim, 2=run script, 3=wait condition)
    unsigned char   pad_01;             // 0x01: unused (zeroed by init)
    unsigned char   active;             // 0x02: active flag (0=inactive, non-zero=active)
    unsigned char   stackDepth;         // 0x03: operand stack index (0xFF = empty)
    Entity*         entity;             // 0x04: associated entity pointer
    unsigned char*  scriptPtr;          // 0x08: current SCD instruction pointer
    unsigned int    returnStack[4];     // 0x0C: return address stack (for SCD loops)
    unsigned int    callStack[4];       // 0x1C: saved script pointer stack (for SCD subroutines)
    short           counterStack[4];    // 0x2C: loop counter stack
};
#pragma pack(pop)
static_assert(sizeof(ScdEventEntry) == 0x34, "ScdEventEntry size mismatch");

// ============================================================================
// Effect (0x84 bytes) - Billboard/sprite effect slot in the 64-slot pool
// Pool base address: 0x00be41e4, allocated by Effect_CreateBillboard
// ============================================================================
#pragma pack(push, 1)
struct Effect {
    // Animation header (0x00-0x17, 24 bytes, bulk-copied from frame data)
    unsigned char  animId;               // 0x00 - animation ID (0 = free slot)
    unsigned char  updateId;             // 0x01 - behavior update index
    unsigned char  type;                 // 0x02 - 1 = active billboard
    unsigned char  lightFactor;          // 0x03 - lighting intensity
    unsigned char  animHeader[0x12];     // 0x04-0x15 - rest of animation header
    short          yaw;                  // 0x16 - yaw rotation angle (base + param)

    // Movement / physics (0x18-0x1F)
    short          rotSpeedX;            // 0x18 - rotation speed X
    short          rotSpeedY;            // 0x1A - rotation speed Y
    short          rotSpeedZ;            // 0x1C - rotation speed Z
    unsigned char  frameDelay;           // 0x1E - frame delay counter
    unsigned char  frameIndex;           // 0x1F - current animation frame

    // World position (0x20-0x25)
    short          posX;                 // 0x20 - world position X
    short          posY;                 // 0x22 - world position Y
    short          posZ;                 // 0x24 - world position Z

    // Spawn parameters (0x26-0x2F)
    unsigned char  effectType;           // 0x26 - effect type ID (param_1)
    unsigned char  depthGroup;           // 0x27 - depth group + flags (param_2)
    short          localOffsetX;         // 0x28 - local offset X (truncated spawn pos)
    short          localOffsetY;         // 0x2A - local offset Y
    short          localOffsetZ;         // 0x2C - local offset Z
    short          depthScaled;          // 0x2E - depth-scaled value

    // 3x3 rotation matrix (0x30-0x41, 9 shorts = 18 bytes)
    short          transform[9];         // 0x30 - rotation matrix rows

    // Padding for alignment (0x42-0x43)
    short          pad_42;               // 0x42

    // Sprite world offset (0x44-0x50)
    int            spriteOffsetX;        // 0x44 - sprite world offset X
    int            spriteOffsetY;        // 0x48 - sprite world offset Y
    int            spriteOffsetZ;        // 0x4C - sprite world offset Z
    int            projDepth;            // 0x50 - projected depth value

    // Spawn position copy (0x54-0x63, full precision from VECTOR)
    int            spawnPosX;            // 0x54 - spawn position X
    int            spawnPosY;            // 0x58 - spawn position Y
    int            spawnPosZ;            // 0x5C - spawn position Z
    int            spawnPosW;            // 0x60 - spawn position W (pad)

    // Texture / rendering pointers (0x64-0x83)
    int            spriteInfo;           // 0x64 - MATRIX pointer or identity
    int            clutInfo;             // 0x68 - CLUT texture header pointer
    int            vramInfo;             // 0x6C - VRAM sprite info pointer
    int            vramInfoBackup;       // 0x70 - VRAM info backup
    int            uvData;               // 0x74 - UV data pointer
    int            uvDataBackup;         // 0x78 - UV data backup
    int            animDataBase;         // 0x7C - animation frame data base
    int            animDataFrame;        // 0x80 - current animation frame pointer
};
#pragma pack(pop)
static_assert(sizeof(Effect) == 0x84, "Effect size mismatch");

#define MAX_EFFECTS 64

// ============================================================================
// Item Slot structure (each inventory entry: Id + quantity)
// ============================================================================
#pragma pack(push, 1)
struct ItemSlot {
    unsigned char Id;       // +0x00: Item ID (0 = empty)
    unsigned char qty;      // +0x01: Quantity / ammo count
};
#pragma pack(pop)
static_assert(sizeof(ItemSlot) == 2, "ItemSlot size mismatch");

// ============================================================================
// Display and rendering structures
// ============================================================================
struct DisplayModeInfo {
    DWORD dwWidth;
    DWORD dwHeight;
    DWORD dwBPP;
    DWORD dwRefreshRate;
    DWORD dwFlags;
};

struct RectDrawDesc {
    unsigned int textureId;
    short x;
    short y;
    short w;
    short h;
    unsigned char r;
    unsigned char g;
    unsigned char b;
};
static_assert(sizeof(RectDrawDesc) == 16, "RectDrawDesc size mismatch");

#pragma pack(push, 1)
struct TextureDesc {
    unsigned int flags;         // 0x00
    short screenX;              // 0x04
    short screenY;              // 0x06
    unsigned short width;       // 0x08
    unsigned short height;      // 0x0a
    short depth;                // 0x0c
    unsigned char texU;         // 0x0e
    unsigned char texV;         // 0x0f
    short unk10;                // 0x10 legacy PS1 alpha/tint field
    short printClutTint;        // 0x12
    unsigned char colorMulR;    // 0x14
    unsigned char colorMulG;    // 0x15
    unsigned char colorMulB;    // 0x16
    unsigned char unk17;        // 0x17
    short pivotX;               // 0x18
    short pivotY;               // 0x1a
    short scaleX;               // 0x1c (fix16.12, 0x1000 = 1.0)
    short scaleY;               // 0x1e (fix16.12, 0x1000 = 1.0)
};
#pragma pack(pop)
static_assert(sizeof(TextureDesc) == 0x20, "TextureDesc size mismatch");

// ============================================================================
// RoomSprEntry (0x24 bytes) - Room sprite/object entry
// Each room has an array of these, populated from RDT data by FUN_004757c0.
// The 'active' flag controls visibility; 'id' is the sprite type identifier
// used by SCD commands (cmd_rdt_0x25) to show/hide objects.
// Base address: 0x00d213d0 (g_RoomSprEntries)
// ============================================================================
#pragma pack(push, 1)
struct RoomSprEntry {
    TextureDesc      texDesc;   // 0x00-0x1F: texture/rendering descriptor
    unsigned char    active;    // 0x20: visibility flag (1=visible, 0=hidden)
    unsigned char    id;        // 0x21: sprite type identifier
    unsigned short   posData;   // 0x22: position/z-depth data
};
#pragma pack(pop)
static_assert(sizeof(RoomSprEntry) == 0x24, "RoomSprEntry size mismatch");

struct TaskControlBlock {
    short state;           // 0x00 - State/flags
    short sleepCounter;    // 0x02 - Sleep counter
    BYTE  reserved[0x78];
};

struct D3DRendererInfo {
    char name[256];
    DWORD flags;
};

// ============================================================================
// PS1 digital controller button bit constants
// ============================================================================
#define PAD_SELECT      0x0001
#define PAD_L3          0x0002
#define PAD_R3          0x0004
#define PAD_START       0x0008
#define PAD_UP          0x0010
#define PAD_RIGHT       0x0020
#define PAD_DOWN        0x0040
#define PAD_LEFT        0x0080
#define PAD_L2          0x0100
#define PAD_R2          0x0200
#define PAD_L1          0x0400
#define PAD_R1          0x0800
#define PAD_TRIANGLE    0x1000
#define PAD_CIRCLE      0x2000
#define PAD_CROSS       0x4000
#define PAD_SQUARE      0x8000

#define PAD_ANY         0xFFFF
#define PAD_DPAD        (PAD_UP|PAD_DOWN|PAD_LEFT|PAD_RIGHT)
#define PAD_SHOULDER    (PAD_L1|PAD_L2|PAD_R1|PAD_R2)
#define PAD_FACE        (PAD_TRIANGLE|PAD_CIRCLE|PAD_CROSS|PAD_SQUARE)
#define PAD_MENU_CONFIRM PAD_CROSS
#define PAD_MENU_BACK   PAD_SQUARE
#define PAD_MENU_UP     PAD_UP
#define PAD_MENU_DOWN   PAD_DOWN
#define PAD_CONFIRM     (PAD_CROSS|PAD_START)
#define PAD_TITLE_ANY   (PAD_SELECT|PAD_L3|PAD_R3|PAD_START|PAD_RIGHT|PAD_LEFT|PAD_R2|PAD_L1|PAD_R1|PAD_CROSS)

// ============================================================================
// Registry
// ============================================================================
#define REGKEY_PATH "Software\\CAPCOM\\RESIDENT EVIL"
#define MAX_DISPLAY_MODES 100
#define MAX_DRIVES 26
