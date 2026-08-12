// CharacterNpc.cpp - Cutscene character entities (Chris/Jill/Barry/Rebecca/
// Wesker/Richard/Enrico and the corpse props), decompiled from Ghidra.
//
// Every entity id from 22 up dispatches to character_npc_update (0x0046acf0)
// through enemies_update_functions_tbl - it is the shared "human character"
// driver, as opposed to the per-monster update functions for ids 0-21. The SCD
// script spawns these with cmd_em_set (opcode 0x1B) and then drives them from
// the room event VM, which writes state 8 plus an animation id straight into the
// entity. Before this file existed the port's dispatch table stopped at 32
// entries, so id 33 (Jill) read one entry past the end - straight into
// zombie_states_table - and ran zombie logic on a cutscene actor.
//
// ---------------------------------------------------------------------------
// One array, three views (0x004c2bf8 / 0x004c2c50 / 0x004c2cb8)
// ---------------------------------------------------------------------------
// The original indexes the same block of function pointers three different ways,
// each with its own base register, and the bases overlap. In id-space (index =
// entity id) the block starts at id 22:
//
//   base 0x004c2c50, index = entity->state        -> states 0..9   (ids 22..31)
//   base 0x004c2bf8, index = entity->id           -> per-character init
//   base 0x004c2cb8, index = entity->action_behavior -> idle behaviours
//
// 0x004c2c50 is 0x004c2bf8 + 22*4 and 0x004c2cb8 is 0x004c2bf8 + 48*4, so the
// state table occupies id-slots 22-31 and the idle table id-slots 48-63. Reading
// them as three separate arrays makes the overlap look like a bug; they are one
// table, so that is how it is declared here. A consequence worth recording: an
// entity whose id is 22-31 reaches npc_state0_init, which then dispatches
// g_npcDispatch[id - 22] and lands back inside the state table - id 22 calls
// npc_state0_init recursively. No SCD script uses those ids, so the original
// never hits it. Reproduced rather than guarded, to keep the layout honest.
// ============================================================================
#include "../../Globals.h"
#include "../Items.h"
#include "EntityCommon.h"
#include "../FileLoader.h"
#include <cstdio>
#include "../../DebugPrint.h"
#include "../../system/AssetPath.h"

// turn_toward_target, entity_rotate_toward_target, entity_pathfind_update,
// SetEntityScaHitData, ResolveEntityScaCollision, entity_add_fade_sprite and
// FUN_004565f0 all come from EntityCommon.h. Declaring them locally again is
// how a signature can drift and silently become a do-nothing overload.
extern int  is_entity_in_switch_zone(VECTOR* pos, void* zoneData);
extern void play_sound_and_voice_effect(int type, int id);   // SoundSystem.cpp
extern void ResetJointTransforms(void);
extern unsigned int  ProcessTmdTextures(char mode, unsigned int* tmdBase, int bank, int depth);
extern unsigned int* CreateAnimObject(int slotPtr, unsigned int* param2);
extern void SetSpriteBufferFlag(void);
extern void EntityUpdateLookAtAngles(void);                              // 0x00459eb0
extern void Flg_on(int baseAddr, unsigned int bitIndex);                 // 0x00473ef0

// g_dwJointAnimCopyBase (0x00be0e00) - scratch base written by the weapon-TMD
// loader; only ever read back through the joint block it also fills in.
static void* g_dwJointAnimCopyBase = nullptr;

// ============================================================================
// Per-character SCA info records (0x004c2bb0, 16 bytes each, indexed by id-32).
// Entity+0x04 points at one of these. check_room_collision reads the collision
// radius from +10 and the hit-box width from +4, which is why a character with a
// null Sca_info walks through walls instead of standing on the floor.
// ============================================================================
#pragma pack(push, 1)
struct CharScaInfo {
    short field_00;   // 0x00
    short field_02;   // 0x02
    short field_04;   // 0x04 - hit box width
    short field_06;   // 0x06
    short field_08;   // 0x08
    short radius;     // 0x0A - collision radius
    short field_0c;   // 0x0C
    short field_0e;   // 0x0E
};
#pragma pack(pop)
static_assert(sizeof(CharScaInfo) == 16, "CharScaInfo size mismatch");

// 0x004c2bb0 - ids 32..42. Note 0x004c2bf8 (the init table's nominal base) falls
// inside the Wesker record: the base is never dereferenced at a low index, so
// the original happily overlaps it with unrelated data.
static const CharScaInfo g_charScaInfo[11] = {
    /* 32 chris   0x004c2bb0 */ { (short)0x8000, 0, (short)0xfa06, 0, 0x05fa, 0x01a6, 0, 0 },
    /* 33 jill    0x004c2bc0 */ { (short)0x8000, 0, (short)0xfa06, 0, 0x05fa, 0x0174, 0, 0 },
    /* 34 barry   0x004c2bd0 */ { (short)0x8000, 0, (short)0xfa06, 0, 0x05fa, 0x01a6, 0, 0 },
    /* 35 rebecca 0x004c2be0 */ { (short)0x8000, 0, (short)0xfa06, 0, 0x05fa, 0x0174, 0, 0 },
    /* 36 wesker  0x004c2bf0 */ { (short)0x8000, 0, (short)0xfa06, 0, 0x05fa, 0x01a6, 0, 0 },
    /* 37         0x004c2c00 */ { (short)0x8000, 0, (short)0xfa06, 0, 0x05fa, 0x01a6, 0, 0 },
    /* 38         0x004c2c10 */ { (short)0x8000, 0x0258, (short)0xff4c, (short)0xff38, 0x00b4, 0x01f4, 0, 0 },
    /* 39 richard 0x004c2c20 */ { (short)0x8000, 0, (short)0xfa06, 0, 0x05fa, 0x01a6, 0, 0 },
    /* 40 enrico  0x004c2c30 */ { (short)0x8000, 0x0258, (short)0xff4c, 0, 0x00b4, 0x01f4, 0, 0 },
    /* 41         0x004c2c40 */ { (short)0x8000, 0, (short)0xfa06, 0, 0x05fa, 0x01a6, 0, 0 },
    /* 42         0x004c2c50 */ { 0, 0, 0, 0, 0, 0, 0, 0 },   // unused; overlaps the dispatch table
};

// ============================================================================
// Held-weapon TMD path table (0x004c1a30) - 4 blocks of 7, 18-byte entries.
// Indexed by (characterBlock * 7 + behavior_flags) * 0x12.
// ============================================================================
static const char g_charWeaponTmdTable[4][7][18] = {
    {   // block 0 - Chris
        "players/ws202.tmd", "players/ws202.tmd", "players/ws202.tmd",
        "players/ws202.tmd", "players/ws202.tmd", "players/ws202.tmd",
        "players/ws202.tmd",
    },
    {   // block 1 - Jill
        "players/ws212.tmd", "players/ws212.tmd", "players/ws212.tmd",
        "players/ws212.tmd", "players/ws212.tmd", "players/ws212.tmd",
        "players/ws212.tmd",
    },
    {   // block 2
        "players/ws224.tmd", "players/ws224.tmd", "players/ws224.tmd",
        "players/ws224.tmd", "players/ws224.tmd", "players/ws225.tmd",
        "players/ws225.tmd",
    },
    {   // block 3 - Rebecca
        "players/ws232.tmd", "players/ws232.tmd", "players/ws232.tmd",
        "players/ws232.tmd", "players/ws232.tmd", "players/ws232.tmd",
        "players/ws236.tmd",
    }
};

// ============================================================================
// LoadCharacterWeaponTmd (0x004624f0)
// Loads the TMD for whatever the character is holding into the joint block at
// jointsStructs+0x6c8, then runs it through the texture pass and builds its
// animation object. Gated on behavior_flags: a character with 0 holds nothing.
//
// The block index is clamped twice in the original:
//     block = id - 0x20;  if (block > 4) block = 2;  if (block > 0xb) block = 3;
// The second test can never fire - block is at most 4 by then - so the ws232
// block is dead code. Kept as-is.
// ============================================================================
static void LoadCharacterWeaponTmd(void)
{
    if (ENTITY->behavior_flags == 0) {
        return;
    }

    unsigned char block = (unsigned char)(ENTITY->id - 0x20);
    if (block > 4)    block = 2;
    if (block > 0xb)  block = 3;

    // 0x0046251e: the weapon slot lives at jointsStructs + 0x6c8, with the TMD
    // pointer at +0x14 and the anim object at +0x18.
    unsigned char* weaponSlot = (unsigned char*)ENTITY->jointsStructs + 0x6c8;

    sprintf(FILE_PATH, "%s%s", GAME_DATA_ROOT,
            g_charWeaponTmdTable[block][ENTITY->behavior_flags]);
    SetSpriteBufferFlag();

    unsigned int fileSize = LoadFile(FILE_PATH, g_loadDataDestPointer, 0x20);
    void* tmdBase = g_loadDataDestPointer;
    g_dwJointAnimCopyBase = tmdBase;
    g_loadDataDestPointer = (void*)((char*)g_loadDataDestPointer + (fileSize & 0xFFFFFFFC));
    *(void**)(weaponSlot + 0x14) = tmdBase;

    // 0x004625a0: the character's own texture bank/depth is swapped in for the
    // texture pass and restored afterwards.
    unsigned char savedDepth = g_TextureDepthByte;
    unsigned char savedBank  = g_TextureBankID;
    g_TextureDepthByte = ENTITY->attacking_direction;   // +0x16c
    g_TextureBankID    = ENTITY->texBank;               // +0x16e
    ProcessTmdTextures(2, *(unsigned int**)(weaponSlot + 0x14),
                       g_TextureBankID, g_TextureDepthByte);
    g_TextureBankID    = savedBank;
    g_TextureDepthByte = savedDepth;

    *(char**)(weaponSlot + 0x14) = *(char**)(weaponSlot + 0x14) + 0xc;
    *(void**)(weaponSlot + 0x18) = g_loadDataDestPointer;
    g_loadDataDestPointer = CreateAnimObject((int)(weaponSlot + 0xc),
                                            (unsigned int*)g_loadDataDestPointer);
}

// ============================================================================
// Per-character init handlers, dispatched from npc_state0_init by entity id.
// All of them follow the same shape: pick the ambient tint, build the ground
// shadow quad, pose the skeleton at frame 0 and point Sca_info at the character's
// collision record.
//
// g_animFrameIdSave (0x00be0dfc) takes a packed 0xRRGGBB triple here, not a
// pointer - Ghidra renders 0x808080 as &DAT_00808080 because the value happens to
// look like an address. Same trap as player_state_init.
// ============================================================================
static void char_init_common(unsigned int tint,
                             short shadowX, short shadowY, short shadowZ,
                             int shadowW, int shadowH,
                             const CharScaInfo* scaInfo)
{
    g_animFrameIdSave = tint;
    g_svecScratch.x = shadowX;
    g_svecScratch.y = shadowY;
    g_svecScratch.z = shadowZ;
    FUN_004565f0(&g_svecScratch, (SVECTOR*)&ENTITY->pushVelocity, shadowW, shadowH);
    ENTITY->blend_counter = 0;
    Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);
    ENTITY->Sca_info = (unsigned int)scaInfo;
}

// 0x0046adf0 - id 32, Chris
static void char_init_chris(void)
{
    char_init_common(0x00808080, 0, 0, -0x50, 0x200, 0x280, &g_charScaInfo[0]);
    LoadCharacterWeaponTmd();
}

// 0x0046ae80 - id 33, Jill
static void char_init_jill(void)
{
    char_init_common(0x00808080, 0, 0, -0x50, 0x200, 0x280, &g_charScaInfo[1]);
    LoadCharacterWeaponTmd();
}

// 0x0046af10 - id 34, Barry (also aliased by ids 42, 43 and 45)
static void char_init_barry(void)
{
    char_init_common(0x00808080, 0, 0, -0x50, 0x200, 0x280, &g_charScaInfo[2]);
    LoadCharacterWeaponTmd();
}

// 0x0046afa0 - id 35, Rebecca (also aliased by id 44)
// Once flag 0xC0 of bank 3 is set she wears the darkened/wounded variant: a
// different animation, a bigger shadow and three joint groups tinted down.
static void char_init_rebecca(void)
{
    g_animFrameIdSave = 0x00808080;
    g_svecScratch.z = -0x50;
    g_svecScratch.x = 0;
    g_svecScratch.y = 0;
    FUN_004565f0(&g_svecScratch, (SVECTOR*)&ENTITY->pushVelocity, 0x200, 0x280);
    ENTITY->blend_counter = 0;

    if (Flg_ck((int)g_PlayerFlags3, 0xc0) != 0) {
        JointStruct* joints = ENTITY->jointsStructs;
        ENTITY->timing_control     = 0;
        ENTITY->animationId        = 0x33;
        ENTITY->animation_frame_id = 0x3d;
        BillboardSetColor(&ENTITY->pushVelocity, 1, 2, 0x00ffff70);
        BillboardAdjSize(&ENTITY->pushVelocity, 0x1e0, 0x1e0);
        *((unsigned char*)joints + 0x7c) &= 0xfe;
        JointApplyColorTint(joints, 0x30, 0x80820, (void*)0x00606060);
        JointApplyColorTint((JointStruct*)((char*)joints + 0x45c), 0x30, 0x80820, (void*)0x00606060);
        JointApplyColorTint((JointStruct*)((char*)joints + 0x5d0), 0x30, 0x80820, (void*)0x00606060);
    }

    Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);
    ENTITY->Sca_info = (unsigned int)&g_charScaInfo[3];
    LoadCharacterWeaponTmd();
}

// 0x0046b100 - id 36, Wesker (also aliased by id 46)
// Flag 0x37 of bank 1 swaps in his later animation and a much larger shadow;
// stage 0x1104 additionally forces status bit 1.
static void char_init_wesker(void)
{
    g_animFrameIdSave = 0x00808080;
    g_svecScratch.x = 0;
    g_svecScratch.y = 0;
    g_svecScratch.z = -0x50;
    FUN_004565f0(&g_svecScratch, (SVECTOR*)&ENTITY->pushVelocity, 0x200, 0x280);
    ENTITY->blend_counter = 0;

    if (Flg_ck((int)g_PlayerFlags, 0x37) != 0) {
        ENTITY->animationId        = 0x30;
        ENTITY->animation_frame_id = 0x6d;
        BillboardSetColor(&ENTITY->pushVelocity, 1, 2, 0x00ffff70);
        BillboardAdjSize(&ENTITY->pushVelocity, 0x5dc, 0x5dc);
    }

    Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);

    // 0x0046b1f0: zero the tracking joint's yaw/pitch and give it a pitch step
    unsigned char* joints = (unsigned char*)ENTITY->jointsStructs;
    *(short*)(joints + 0xf2) = 0;
    *(short*)(joints + 0xf4) = 0;
    *(short*)(joints + 0xf6) = 0x10;

    ENTITY->Sca_info = (unsigned int)&g_charScaInfo[4];

    // 0x0046b21c: compares the packed stage/room word, not g_stageId alone
    if (*(unsigned short*)&g_stageId == 0x1104) {
        ENTITY->status_flags |= 2;
    }

    LoadCharacterWeaponTmd();
}

// 0x0046b230 - id 37 (Kenneth's corpse). No weapon TMD pass.
static void char_init_37(void)
{
    g_animFrameIdSave = 0x00ffff50;
    g_svecScratch.x = 0;
    g_svecScratch.y = 0;
    g_svecScratch.z = 0;
    FUN_004565f0(&g_svecScratch, (SVECTOR*)&ENTITY->pushVelocity, 500, 700);
    ENTITY->animationId        = 0;
    ENTITY->animation_frame_id = 0;
    ENTITY->timing_control     = 0;
    ENTITY->blend_counter      = 0;
    Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);
    ENTITY->Sca_info = (unsigned int)&g_charScaInfo[5];
}

// 0x0046b2e0 - id 38 (Forest's corpse)
static void char_init_38(void)
{
    g_animFrameIdSave = 0x00ffff50;
    g_svecScratch.x = -600;
    g_svecScratch.y = 0;
    g_svecScratch.z = 200;
    FUN_004565f0(&g_svecScratch, (SVECTOR*)&ENTITY->pushVelocity, 700, 700);
    ENTITY->animationId        = 0;
    ENTITY->animation_frame_id = 0;
    ENTITY->timing_control     = 0;
    ENTITY->blend_counter      = 0;
    Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);
    ENTITY->Sca_info = (unsigned int)&g_charScaInfo[6];
}

// 0x0046b390 - id 39, Richard
static void char_init_richard(void)
{
    char_init_common(0x00606060, 0, 0, 0, 500, 700, &g_charScaInfo[7]);
}

// 0x0046b420 - id 40, Enrico
static void char_init_enrico(void)
{
    char_init_common(0x00404040, -600, 0, 200, 700, 700, &g_charScaInfo[8]);
}

// 0x0046b4b0 - id 41
static void char_init_41(void)
{
    g_animFrameIdSave = 0x00606060;
    g_svecScratch.x = 0;
    g_svecScratch.y = 0;
    g_svecScratch.z = 0;
    FUN_004565f0(&g_svecScratch, (SVECTOR*)&ENTITY->pushVelocity, 500, 700);
    ENTITY->animationId        = 0;
    ENTITY->animation_frame_id = 0;
    ENTITY->timing_control     = 0;
    ENTITY->blend_counter      = 0;
    Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);
    ENTITY->Sca_info = (unsigned int)&g_charScaInfo[9];
}

// ============================================================================
// Idle behaviours, dispatched from npc_state1_idle by action_behavior.
// ============================================================================

// 0x0046b5a0 - idle behaviours 4-8, 11, 12, 14, 15: do nothing at all.
static void npc_idle_nop(void) { }

// 0x0046b5b0 - idle behaviours 9, 10, 13: rewind to frame 0 on entry, then
// advance the animation. action_state values above 1 fall through untouched.
static void npc_idle_play_anim(void)
{
    if (ENTITY->action_state == 0) {
        ENTITY->action_state       = 1;
        ENTITY->animationId        = 0;
        ENTITY->animation_frame_id = 0;
        ENTITY->timing_control     = 0;
        ENTITY->blend_counter      = 0;
    }
    else if (ENTITY->action_state != 1) {
        return;
    }
    Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);
}

// ----------------------------------------------------------------------------
// NPC state 9 (0x00471950) is the pathfind layer and is still not transcribed -
// it needs entity_pathfind_update, FUN_00471e70/e90/2570 (FUN_00460230 and
// ResolveEntityScaCollision are now ported).
// Logging the index beats a NULL slot: it names the missing handler the moment a
// script asks for it, instead of faulting with no context.
// ----------------------------------------------------------------------------
static void npc_report_missing(const char* what)
{
    static const char* lastReported = nullptr;
    if (what != lastReported) {
        lastReported = what;
        dbg_printf("[npc] unimplemented %s (id=%u state=%u behavior=%u action=%u)\n",
               what, (unsigned int)ENTITY->id, (unsigned int)ENTITY->state,
               (unsigned int)ENTITY->action_behavior,
               (unsigned int)ENTITY->action_state);
    }
}

static void npc_state9_pathfind(void) { npc_report_missing("state 9 (0x00471950)"); }
static void char_init_missing(void) { npc_report_missing("character init"); }

// ============================================================================
// npc_idle_walk_01 (0x0046b620) - idle 1: walk forward until you hit something,
// then bang on it.
//
// action_state 0 sets animation 0x35 and a starting speed of 1000; state 1 walks
// (the speed is trimmed by 15 per animation frame so the character decelerates),
// probes the room collision, and on contact plays voice 0xA9 and advances.
// State 2 switches to animation 0x36 with a knock sound, state 3 plays it out.
//
// The collision probe saves and restores FOUR dwords from entity+0x34: the three
// translation components of localMatrix (t[0..2]) plus the first dword of
// worldMatrix at +0x40. check_room_collision writes through the position it is
// handed, and the original explicitly rolls all four back - so the probe is a
// test, not a move.
// ============================================================================
static void npc_idle_walk_01(void)
{
    switch (ENTITY->action_state) {
    case 0:
        ENTITY->action_state        = 1;
        ENTITY->animation_frame_id  = 0;
        ENTITY->timing_control      = 0;
        ENTITY->animationId         = 0x35;
        ENTITY->blend_counter       = 0;
        ENTITY->move_speed_current  = 1000;
        // fall through
    case 1: {
        *(short*)&ENTITY->move_speed_current =
            (short)(*(short*)&ENTITY->move_speed_current
                    - (short)((unsigned int)ENTITY->animation_frame_id * 0xF));
        Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);
        Add_speedXZ(0x800);

        int* t = ENTITY->scaMatrixData.localMatrix.t;
        int saved0 = t[0], saved1 = t[1], saved2 = t[2];
        int saved3 = *(int*)((char*)ENTITY + 0x40);
        unsigned char hit = check_room_collision(
            (VECTOR*)t, *(short*)((char*)ENTITY->Sca_info + 10));
        g_playerDisplacement = (int)(unsigned int)hit;
        t[0] = saved0; t[1] = saved1; t[2] = saved2;
        *(int*)((char*)ENTITY + 0x40) = saved3;

        if (g_playerDisplacement != 0) {
            ENTITY->action_state = (unsigned char)(ENTITY->action_state + 1);
            play_sound_and_voice_effect(1, 0xA9);
            g_main_state_flags |= 0x20000;
            return;
        }
        break;
    }

    case 2:
        ENTITY->action_state       = (unsigned char)(ENTITY->action_state + 1);
        ENTITY->animation_frame_id = 0;
        ENTITY->timing_control     = 0;
        ENTITY->animationId        = 0x36;
        ENTITY->blend_counter      = 3;
        Play3DSnd(2, 0x1C, 0, (unsigned int)ENTITY->scaMatrixData.localMatrix.t);
        // fall through
    case 3: {
        char done = (char)Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);
        ENTITY->action_state = (unsigned char)(ENTITY->action_state + done);
        EntityUpdateWeaponJoint(0);
        break;
    }

    default:
        break;
    }
}

// ============================================================================
// npc_idle_walk_02 (0x0046b800) - idle 2: the scripted death with the blood
// spray and the fading billboard.
//
// action_state 0 sets animation 0x33, arms the death timer (0xB4), flags joint 1
// and spawns two type-0 billboards off g_deadMoveValue. State 1 sprays blood for
// the first 10 frames, tints three joints red on frame 3, plays the wet sound on
// frame 0x2A, and advances when the animation ends. State 2 builds the ground
// billboard from the dead-move matrix and arms a 30-frame grow; state 3 grows it.
//
// g_deadMoveValue HOLDS a pointer (see the note in Zombie.cpp) - the spawn
// position is the 4-dword block at *(g_deadMoveValue + 0x14).
// ============================================================================
static void npc_idle_walk_02(void)
{
    const int* deadPos = (const int*)((char*)g_deadMoveValue + 0x14);

    switch (ENTITY->action_state) {
    case 0: {
        ENTITY->action_state       = (unsigned char)(ENTITY->action_state + 1);
        ENTITY->animation_frame_id = 0;
        ENTITY->timing_control     = 0;
        ENTITY->animationId        = 0x33;
        ENTITY->death_timer        = 0xB4;
        ENTITY->blend_counter      = 3;

        char* joints = (char*)ENTITY->jointsStructs;
        ((unsigned char*)joints)[0x7C] |= 8;      // joint 1's flags byte

        g_playerPosScratch.x   = deadPos[0];
        g_playerPosScratch.y   = deadPos[1];
        g_playerPosScratch.z   = deadPos[2];
        g_playerPosScratch.pad = deadPos[3];
        Effect_CreateBillboard(0, 3, 0, (void*)(joints + 0xC0), &g_playerPosScratch, 0);
        // NOTE: the original passes the dead-move matrix as the sprite space and
        // joints+0xD4 as the POSITION - the two arguments are swapped relative to
        // the call above. Faithful; joints+0xD4 is read as a VECTOR.
        Effect_CreateBillboard(0, 3, 0, (void*)g_deadMoveValue,
                               (void*)(joints + 0xD4), 0);
        // fall through
    }
    case 1: {
        if (ENTITY->animation_frame_id < 10) {
            g_playerPosScratch.x   = deadPos[0];
            g_playerPosScratch.z   = deadPos[2];
            g_playerPosScratch.pad = deadPos[3];
            g_playerPosScratch.y   = -0x898;
            Effect_CreateBillboard(0, 0, 0,
                                   &ENTITY->scaMatrixData.localMatrix,
                                   &g_playerPosScratch, 0);
        }
        if ((char)ENTITY->animation_frame_id == 3) {
            char* joints = (char*)ENTITY->jointsStructs;
            JointApplyColorTint((JointStruct*)joints,           0x30, 0x80820, (void*)0x606060);
            JointApplyColorTint((JointStruct*)(joints + 0x45C), 0x30, 0x80820, (void*)0x606060);
            JointApplyColorTint((JointStruct*)(joints + 0x5D0), 0x30, 0x80820, (void*)0x606060);
        }
        if ((char)ENTITY->animation_frame_id == 0x2A) {
            Play3DSnd(2, 0x2F, 0, (unsigned int)ENTITY->scaMatrixData.localMatrix.t);
        }
        char done = (char)Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);
        ENTITY->action_state = (unsigned char)(ENTITY->action_state + done);
        EntityUpdateWeaponJoint(0);
        break;
    }

    case 2: {
        g_svecScratch.y = 0;
        g_svecScratch.z = 0;
        g_svecScratch.x = -900;
        // copy the dead-move matrix (8 dwords) into the scratch matrix
        memcpy(&g_matrixScratch, (void*)g_deadMoveValue, 0x20);
        RotMatrixY((int)ENTITY->angle, &g_matrixScratch);
        ApplyMatrixSV(&g_matrixScratch, &g_svecScratch, &g_svecScratch);

        short* quad = (short*)((char*)ENTITY + 0xE4);
        quad[0] = (short)(quad[0] + g_svecScratch.x);
        quad[1] = (short)(quad[1] + g_svecScratch.y);
        quad[2] = (short)(quad[2] + g_svecScratch.z);
        BillboardSetColor(quad, 1, 2, 0x00FFFF50);
        BillboardAdjSize(quad, (short)-200, (short)-200);
        ENTITY->action_state = (unsigned char)(ENTITY->action_state + 1);
        ENTITY->action_ticks_counter = 0x1E;
        return;
    }

    case 3:
        BillboardAdjSize((short*)((char*)ENTITY + 0xE4), 0x10, 0x10);
        ENTITY->action_ticks_counter =
            (unsigned short)((short)ENTITY->action_ticks_counter - 1);
        if ((short)ENTITY->action_ticks_counter == 0) {
            ENTITY->action_state = (unsigned char)(ENTITY->action_state + 1);
        }
        return;

    default:
        return;
    }
}

// ============================================================================
// npc_idle_walk_03 (0x0046bb20) - idle 3: the other scripted death, the one that
// faces enemy 1 and bleeds out over 250 frames.
//
// action_state 0 sets animation 0x30, a 250-frame timer, hit_state 0x80, raises
// status bits 6, and copies enemy 1's facing. States 0 and 1 share the body:
// tint five joints red on frame 8, spray blood before frame 9 and after frame
// 0x5F, run the vertex-animation pass, and on animation end advance and shrink
// the billboard. State 2 clears status bit 1, forces health to -1, grows the
// billboard and counts the timer down.
// ============================================================================
static void npc_idle_walk_03(void)
{
    const int* deadPos = (const int*)((char*)g_deadMoveValue + 0x14);
    unsigned char st = ENTITY->action_state;

    if (st == 0) {
        ENTITY->action_state         = 1;
        ENTITY->animation_frame_id   = 0;
        ENTITY->timing_control       = 0;
        ENTITY->animationId          = 0x30;
        ENTITY->blend_counter        = 0;
        ENTITY->action_ticks_counter = 0xFA;     // one 16-bit store of 250
        ENTITY->hit_state            = 0x80;
        ENTITY->status_flags        |= 6;
        ENTITY->angle                = (short)g_EnemiesList[1].angle;
    } else if (st != 1) {
        if (st != 2) {
            return;
        }
        ENTITY->status_flags &= (unsigned char)0xFD;
        ENTITY->health = -1;                     // 0x88/0x89 written as 0xFF,0xFF
        BillboardAdjSize((short*)((char*)ENTITY + 0xE4), 6, 6);
        ENTITY->action_ticks_counter =
            (unsigned short)((short)ENTITY->action_ticks_counter - 1);
        if ((short)ENTITY->action_ticks_counter != 0) {
            return;
        }
        ENTITY->action_state = (unsigned char)(ENTITY->action_state + 1);
        return;
    }

    if (ENTITY->animation_frame_id == 8) {
        char* joints = (char*)ENTITY->jointsStructs;
        JointApplyColorTint((JointStruct*)joints,           0x30, 0x80820, (void*)0x606060);
        JointApplyColorTint((JointStruct*)(joints + 0x7C),  0x30, 0x80820, (void*)0x606060);
        JointApplyColorTint((JointStruct*)(joints + 0xF8),  0x30, 0x80820, (void*)0x606060);
        JointApplyColorTint((JointStruct*)(joints + 0x45C), 0x30, 0x80820, (void*)0x606060);
        JointApplyColorTint((JointStruct*)(joints + 0x5D0), 0x30, 0x80820, (void*)0x606060);
    }
    if (ENTITY->animation_frame_id < 9) {
        g_playerPosScratch.x   = deadPos[0];
        g_playerPosScratch.z   = deadPos[2];
        g_playerPosScratch.pad = deadPos[3];
        g_playerPosScratch.y   = -0x5DC;
        Effect_CreateBillboard(0, 0, 0, &ENTITY->scaMatrixData.localMatrix,
                               &g_playerPosScratch, 0);
    }
    if (ENTITY->animation_frame_id > 0x5F) {
        g_playerPosScratch.x   = deadPos[0];
        g_playerPosScratch.y   = deadPos[1];
        g_playerPosScratch.z   = deadPos[2];
        g_playerPosScratch.pad = deadPos[3];
        Effect_CreateBillboard(0, 0, 0, &ENTITY->scaMatrixData.localMatrix,
                               &g_playerPosScratch, 0);
    }

    entity_apply_anim_vertex(ENTITY, ENTITY->animHeader, ENTITY->animBase);
    if ((char)Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400) != 0) {
        ENTITY->action_state = (unsigned char)(ENTITY->action_state + 1);
        short* quad = (short*)((char*)ENTITY + 0xE4);
        BillboardSetColor(quad, 1, 2, 0x00FFFF50);
        BillboardAdjSize(quad, (short)-100, (short)-100);
    }
}

// ============================================================================
// SCD-driven animation handlers, dispatched from npc_state8_action_update by
// action_behavior through the table at 0x004c4780 (11 entries).
// Not transcribed yet - the event VM reaches these as soon as a cutscene sets a
// character's animation, so the log tells us which one to do next.
// ============================================================================
// ============================================================================
// entity_apply_walk_speed (0x0047a4f0)
// Sets move_speed_current, then trims it on the frames where the foot is planted
// so the character does not slide. The four range tests are written as unsigned
// byte subtractions in the original, so a frame id below the window wraps to a
// large value and fails - reproduced with the same casts.
// ============================================================================
void entity_apply_walk_speed(short speed)
{
    ENTITY->move_speed_current = (unsigned short)speed;
    unsigned char frame = ENTITY->animation_frame_id;

    if ((unsigned char)(frame - 0x15) < 7) {
        *(short*)&ENTITY->move_speed_current -= 0xd;
    }
    if ((unsigned char)(frame - 7) < 7) {
        *(short*)&ENTITY->move_speed_current -= 0xd;
    }
    if ((unsigned char)(frame - 0x17) < 3) {
        *(short*)&ENTITY->move_speed_current -= 0xe;
    }
    if ((unsigned char)(frame - 9) < 3) {
        *(short*)&ENTITY->move_speed_current -= 0xe;
    }
}

static void npc_scd_report(const char* addr)
{
    static const char* lastReported = nullptr;
    if (addr != lastReported) {
        lastReported = addr;
        dbg_printf("[npc] unimplemented SCD behavior %s (id=%u anim=%u frame=%u action=%u)\n",
               addr, (unsigned int)ENTITY->id, (unsigned int)ENTITY->animationId,
               (unsigned int)ENTITY->animation_frame_id,
               (unsigned int)ENTITY->action_state);
    }
}

// 0x0047a580 - behaviour 0: advance the animation and nothing else. action_state
// 0 rewinds to frame 0, 1 keeps playing, anything else is a no-op. There is no
// completion state here - the script ends this one with an explicit opcode.
static void npc_scd_00(void)
{
    if (ENTITY->action_state == 0) {
        ENTITY->animation_frame_id = 0;
        ENTITY->timing_control     = 0;
        ENTITY->animationId        = 0;
        ENTITY->action_state       = 1;
        ENTITY->blend_counter      = 7;
    }
    else if (ENTITY->action_state != 1) {
        return;
    }
    Joint_move((int)(ENTITY->scd_entity_flags & 1),
               ENTITY->animHeader, ENTITY->animBase, 0x200);
}

// ---------------------------------------------------------------------------
// The three handlers the mansion intro actually drives. All of them share the
// same action_state contract: 0 = rewind the animation and set up, 1 = advance
// it, 2 = raise the script's completion flag via Flg_on(g_SysFlags,
// scd_anim_param) so the waiting event opcode can move on.
//
// Field notes, since several of these have no single named struct member:
//   +0xc2 move_speed_current   +0xc4 a 16-bit frame-hold counter
//   +0xde the SCD timer word, reused here as a per-completion yaw delta
//   +0x74 the yaw component of the rotation SVECTOR at entity+0x72
// ---------------------------------------------------------------------------

// 0x0047a600 - behaviour 1: plain animation playback. Both Wesker and Jill use
// this one, which is why nothing but the player was moving.
static void npc_scd_01(void)
{
    unsigned char st = ENTITY->action_state;
    if (st == 0) {
        ENTITY->animation_frame_id = 0;
        ENTITY->timing_control     = 0;
        ENTITY->action_state       = 1;
        ENTITY->blend_counter      = 7;
        ENTITY->move_speed_current = 0;
        if ((ENTITY->scd_entity_flags & 0x20) != 0) {
            ENTITY->blend_counter = 0;
        }
        *(short*)((char*)ENTITY + 0xc4) = 1;
    }
    else if (st != 1) {
        if (st != 2) {
            return;
        }
        Flg_on((int)g_SysFlags, ENTITY->scd_anim_param);
        ENTITY->scd_timer = 0;
        if ((ENTITY->scd_entity_flags & 0x10) == 0) {
            return;
        }
        ENTITY->action_state = 0;
        return;
    }

    // Flag 0x80 holds each animation frame for an extra tick
    if ((ENTITY->scd_entity_flags & 0x80) != 0) {
        short* hold = (short*)((char*)ENTITY + 0xc4);
        short prev = *hold;
        *hold = prev - 1;
        if (prev == 0) {
            *hold = 1;
            return;
        }
    }

    char done = Joint_move((int)(ENTITY->scd_entity_flags & 1),
                           ENTITY->animHeader, ENTITY->animBase, 0x200);
    if (done != 0) {
        ENTITY->action_state = 2;
        *(short*)((char*)ENTITY + 0x74) += *(short*)((char*)ENTITY + 0xde);
    }
}

// 0x0047a740 - behaviour 2: turn to face the scripted target, then walk to it.
// State 1 turns on the spot until aligned within 0x16a; state 3 walks, playing a
// footstep on frames 8 and 0x16, and finishes within 150 units of the target.
static void npc_scd_02(void)
{
    switch (ENTITY->action_state) {
    case 0:
        ENTITY->animation_frame_id = 0;
        ENTITY->timing_control     = 0;
        ENTITY->animationId        = 7;
        ENTITY->action_state       = 1;
        ENTITY->blend_counter      = 7;
        // fall through
    case 1:
        g_playerPosScratch.x = (int)ENTITY->unk_c6;
        g_playerPosScratch.z = (int)ENTITY->unk_c8;
        g_playerPosScratch.y = 0;
        *(short*)((char*)ENTITY + 0x74) += (short)turn_toward_target(
            &g_playerPosScratch, *(short*)((char*)ENTITY + 0xde));
        Joint_move((int)(ENTITY->scd_entity_flags & 1),
                   ENTITY->animHeader, ENTITY->animBase, 0x200);
        if ((short)turn_toward_target(&g_playerPosScratch, 0x16a) == 0) {
            ENTITY->action_state = 2;
        }
        break;

    case 2:
        ENTITY->animation_frame_id = 0;
        ENTITY->timing_control     = 0;
        ENTITY->animationId        = 7;
        ENTITY->action_state       = 3;
        ENTITY->blend_counter      = 7;
        // fall through
    case 3:
        // PlayEntitySnd takes ONE parameter (0x0047fbf0). The call sites push a
        // second dword (0 or -4) that the callee never reads - a dead push, the
        // same pattern as rotate_entity's fourth argument.
        if (ENTITY->animation_frame_id == 8)    PlayEntitySnd(0);
        if (ENTITY->animation_frame_id == 0x16) PlayEntitySnd(0);

        entity_apply_walk_speed(0x5d);

        g_playerPosScratch.x = (int)ENTITY->unk_c6;
        g_playerPosScratch.z = (int)ENTITY->unk_c8;
        g_playerPosScratch.y = 0;
        entity_rotate_toward_target(&g_playerPosScratch,
                                    *(unsigned short*)((char*)ENTITY + 0xde));
        Joint_move((int)(ENTITY->scd_entity_flags & 1),
                   ENTITY->animHeader, ENTITY->animBase, 0x200);
        Add_speedXZ(0);

        {
            int dz = ENTITY->scaMatrixData.localMatrix.t[2] - (int)ENTITY->unk_c8;
            int dx = ENTITY->scaMatrixData.localMatrix.t[0] - (int)ENTITY->unk_c6;
            if ((unsigned int)SquareRoot0(dz * dz + dx * dx) < 0x96) {
                Flg_on((int)g_SysFlags, ENTITY->scd_anim_param);
                if ((ENTITY->collisionFlags & 0x80) == 0) {
                    ENTITY->action_behavior = 0;
                    ENTITY->action_state    = 0;
                }
            }
        }
        break;
    }
}
// ----------------------------------------------------------------------------
// Shared tail of behaviours 4 and 5: both walk BACKWARDS to the scripted target.
//
// The backward walk is done by flipping the facing 180 degrees, running the
// normal "rotate toward target" step, then flipping back - so the turn aligns
// the character's BACK with the target while `Add_speedXZ(0x800)` drives motion
// along the (also flipped) heading. The `& 0xfff` is the original's; the angle
// space is 0..0xFFF.
//
// Arrival is 100 units. On arrival the completion flag goes up, and unless
// collisionFlags bit 7 is set the behaviour clears itself - the original writes
// action_behavior and action_state with ONE 16-bit store at +0x86.
// ----------------------------------------------------------------------------
static void npc_walk_backward_step(void)
{
    g_playerPosScratch.x = (int)ENTITY->unk_c6;
    g_playerPosScratch.z = (int)ENTITY->unk_c8;
    g_playerPosScratch.y = 0;

    *(unsigned short*)&ENTITY->angle =
        (unsigned short)((*(unsigned short*)&ENTITY->angle + 0x800) & 0xfff);
    entity_rotate_toward_target(&g_playerPosScratch, ENTITY->scd_timer);
    ENTITY->angle = (short)(ENTITY->angle - 0x800);

    Joint_move((char)(ENTITY->scd_entity_flags & 1),
               ENTITY->animHeader, ENTITY->animBase, 0x200);
    Add_speedXZ(0x800);

    int dz = ENTITY->scaMatrixData.localMatrix.t[2] - (int)ENTITY->unk_c8;
    int dx = ENTITY->scaMatrixData.localMatrix.t[0] - (int)ENTITY->unk_c6;
    if ((unsigned int)SquareRoot0(dz * dz + dx * dx) < 100) {
        Flg_on((int)g_SysFlags, ENTITY->scd_anim_param);
        if ((ENTITY->collisionFlags & 0x80) == 0) {
            ENTITY->action_behavior = 0;
            ENTITY->action_state    = 0;
        }
    }
}

// ============================================================================
// npc_scd_03 (0x0047a9c0) - behaviour 3: turn to face the target, walk to it,
// then decelerate to a stop.
//
// A six-state machine. 0/1 turn in place with animation 8 until
// turn_toward_target reports aligned within 0x16A. 2/3 walk with a footstep on
// frames 0 and 10; on closing to 250 units it moves to state 4 - UNLESS
// collisionFlags bit 7 is set, in which case it stays in 3 and raises the
// completion flag immediately (the original writes state 4 first and then
// overwrites it, which is why the flag path never reaches the deceleration).
// 4/5 play the idle animation for four ticks while bleeding 30 off the speed
// each frame, and 6 clears the behaviour and signals.
// ============================================================================
static void npc_scd_03(void)
{
    switch (ENTITY->action_state) {
    case 0:
        ENTITY->animation_frame_id = 0;
        ENTITY->timing_control     = 0;
        ENTITY->animationId        = 8;
        ENTITY->action_state       = 1;
        ENTITY->blend_counter      = 7;
        // fall through
    case 1: {
        g_playerPosScratch.x = (int)ENTITY->unk_c6;
        g_playerPosScratch.z = (int)ENTITY->unk_c8;
        g_playerPosScratch.y = 0;
        int turn = turn_toward_target(&g_playerPosScratch, (short)ENTITY->scd_timer);
        ENTITY->angle = (short)(ENTITY->angle + (short)turn);
        Joint_move((char)(ENTITY->scd_entity_flags & 1),
                   ENTITY->animHeader, ENTITY->animBase, 0x200);
        // second call is a pure alignment TEST at a wider tolerance
        if ((short)turn_toward_target(&g_playerPosScratch, 0x16a) == 0) {
            ENTITY->action_state = 2;
            return;
        }
        break;
    }

    case 2:
        ENTITY->move_speed_current = 0xd2;
        ENTITY->animation_frame_id = 0;
        ENTITY->timing_control     = 0;
        ENTITY->animationId        = 8;
        ENTITY->action_state       = 3;
        ENTITY->blend_counter      = 7;
        // fall through
    case 3: {
        if ((char)ENTITY->animation_frame_id == 0)  PlayEntitySnd(1);
        if ((char)ENTITY->animation_frame_id == 10) PlayEntitySnd(1);

        g_playerPosScratch.x = (int)ENTITY->unk_c6;
        g_playerPosScratch.z = (int)ENTITY->unk_c8;
        g_playerPosScratch.y = 0;
        entity_rotate_toward_target(&g_playerPosScratch, ENTITY->scd_timer);
        Joint_move((char)(ENTITY->scd_entity_flags & 1),
                   ENTITY->animHeader, ENTITY->animBase, 0x200);
        Add_speedXZ(0);

        int dz = ENTITY->scaMatrixData.localMatrix.t[2] - (int)ENTITY->unk_c8;
        int dx = ENTITY->scaMatrixData.localMatrix.t[0] - (int)ENTITY->unk_c6;
        if ((unsigned int)SquareRoot0(dz * dz + dx * dx) < 0xfa) {
            ENTITY->action_state = 4;
            if ((ENTITY->collisionFlags & 0x80) != 0) {
                ENTITY->action_state = 3;
                Flg_on((int)g_SysFlags, ENTITY->scd_anim_param);
                return;
            }
        }
        break;
    }

    case 4:
        ENTITY->animation_frame_id   = 0;
        ENTITY->timing_control       = 0;
        ENTITY->animationId          = 0;
        ENTITY->action_state         = 5;
        ENTITY->blend_counter        = 7;
        ENTITY->action_ticks_counter = 0;
        // fall through
    case 5:
        Joint_move((char)(ENTITY->scd_entity_flags & 1),
                   ENTITY->animHeader, ENTITY->animBase, 0x200);
        ENTITY->action_ticks_counter =
            (unsigned short)((short)ENTITY->action_ticks_counter + 1);
        if ((short)ENTITY->action_ticks_counter > 3) {
            ENTITY->action_state = 6;
        }
        *(short*)&ENTITY->move_speed_current =
            (short)(*(short*)&ENTITY->move_speed_current - 0x1e);
        Add_speedXZ(0);
        break;

    case 6:
        ENTITY->action_behavior = 0;
        ENTITY->action_state    = 0;
        Flg_on((int)g_SysFlags, ENTITY->scd_anim_param);
        return;

    default:
        break;
    }
}

// ============================================================================
// npc_scd_04 (0x0047ad30) - behaviour 4: walk backwards to the target at the
// faster pace (animation 3), with a footstep on frames 8 and 0x16.
//
// NOTE the speed line is the original's and is a no-op dressed as a condition:
// `if (frame > 4 || frame < 8)` is true for every possible byte, so the +4 is
// applied unconditionally and the speed is always 0x40. Transcribed as written
// rather than simplified, because it is the kind of thing that looks like a
// transcription slip when you meet it later.
// ============================================================================
static void npc_scd_04(void)
{
    if (ENTITY->action_state == 0) {
        ENTITY->animation_frame_id = 0;
        ENTITY->timing_control     = 0;
        ENTITY->action_state       = 1;
        ENTITY->blend_counter      = 7;
        ENTITY->animationId        = 3;
    } else if (ENTITY->action_state != 1) {
        return;
    }

    if ((char)ENTITY->animation_frame_id == 8 ||
        (char)ENTITY->animation_frame_id == 0x16) {
        PlayEntitySnd(0);
    }

    ENTITY->move_speed_current = 0x3c;
    if (ENTITY->animation_frame_id > 4 || ENTITY->animation_frame_id < 8) {
        *(short*)&ENTITY->move_speed_current =
            (short)(*(short*)&ENTITY->move_speed_current + 4);
    }

    npc_walk_backward_step();
}

// ============================================================================
// npc_scd_05 (0x0047aef0) - behaviour 5: walk backwards to the target at the
// slower pace (animation 2, speed 0x1D).
//
// The footstep fires on frames 7 and 0x1B, and only on the tick where
// timing_control is exactly 2 - so it plays once per frame rather than on every
// update the frame is held for.
// ============================================================================
static void npc_scd_05(void)
{
    if (ENTITY->action_state == 0) {
        ENTITY->animation_frame_id = 0;
        ENTITY->timing_control     = 0;
        ENTITY->action_state       = 1;
        ENTITY->blend_counter      = 7;
        ENTITY->animationId        = 2;
        ENTITY->move_speed_current = 0x1d;
    } else if (ENTITY->action_state != 1) {
        return;
    }

    if (((char)ENTITY->animation_frame_id == 7 ||
         (char)ENTITY->animation_frame_id == 0x1b) &&
        (char)ENTITY->timing_control == 2) {
        PlayEntitySnd(0);
    }

    npc_walk_backward_step();
}
// 0x0047b0a0 - behaviour 6: turn to face the scripted target in place, without
// walking. Finishes once aligned within 0x28.
static void npc_scd_06(void)
{
    unsigned char st = ENTITY->action_state;
    if (st == 0) {
        ENTITY->animation_frame_id = 0;
        ENTITY->timing_control     = 0;
        ENTITY->animationId        = 7;
        ENTITY->action_state       = 1;
        ENTITY->blend_counter      = 7;
    }
    else if (st != 1) {
        if (st != 2) {
            return;
        }
        // 0x0047b10?: MOV word ptr [.. + 0x86],0 - clears behaviour and sub-state
        ENTITY->action_behavior = 0;
        ENTITY->action_state    = 0;
        Flg_on((int)g_SysFlags, ENTITY->scd_anim_param);
        return;
    }

    g_playerPosScratch.x = (int)ENTITY->unk_c6;
    g_playerPosScratch.z = (int)ENTITY->unk_c8;
    g_playerPosScratch.y = 0;
    entity_rotate_toward_target(&g_playerPosScratch,
                                *(unsigned short*)((char*)ENTITY + 0xde));
    Joint_move((int)(ENTITY->scd_entity_flags & 1),
               ENTITY->animHeader, ENTITY->animBase, 0x200);
    if ((short)turn_toward_target(&g_playerPosScratch, 0x28) == 0) {
        ENTITY->action_state = 2;
    }
}
// ============================================================================
// npc_scd_07 (0x0047b1c0) - behaviour 7: play a scripted animation to its end,
// then release the event script.
//
// This is the one the ROOM1051 dining-room scene blocks on. Barry is set up with
// `85 07 10 21` (state-1 opcode 0x85): action_behavior 7, animationId 0x10,
// scd_anim_param 0x21 - and the script then spins on
// `bit_test(g_SysFlags, 0x21)`. action_state walks 0 -> 1 -> 2 because Joint_move
// returns 1 on the frame the animation loops and that result is ADDED to
// action_state; at 2 this sets the bit and the script proceeds (to `85 08 11 21`,
// the firing behaviour npc_scd_08).
//
// Structurally identical to npc_scd_06's terminal branch, minus the turning:
// no target tracking, just play the anim and signal. The yaw step at the tail
// runs on EVERY path including state 2, which is what lets a script nudge the
// character's facing while the animation plays.
// ============================================================================
static void npc_scd_07(void)
{
    char st = (char)ENTITY->action_state;

    if (st == 0) {
        ENTITY->animation_frame_id = 0;
        ENTITY->timing_control     = 0;
        ENTITY->action_state       = 1;
        ENTITY->blend_counter      = 7;
        // falls through into the Joint_move block, as the original does
    } else if (st != 1) {
        if (st == 2) {
            // The completion signal the event script's bit_test is waiting on.
            Flg_on((int)g_SysFlags, ENTITY->scd_anim_param);
        }
        goto tail;
    }

    {
        // Ghidra renders the first argument as a CONCAT31(...) & 0xffffff01 -
        // that is just `scd_entity_flags & 1` widened to the char parameter,
        // the same idiom npc_scd_06 uses.
        char done = (char)Joint_move((char)(ENTITY->scd_entity_flags & 1),
                                     ENTITY->animHeader, ENTITY->animBase, 0x200);
        ENTITY->action_state = (unsigned char)(ENTITY->action_state + done);
    }

tail:
    ENTITY->angle = (short)(ENTITY->angle + (short)ENTITY->scd_timer);
}

// ============================================================================
// Weapon-FX spawn tables for behaviour 8 (0x004c0dd8 / 0x004c0e68 / 0x004c0ef8)
//
// Three parallel tables of 10-byte records, indexed by `behavior_flags - 2`
// (the weapon the character is holding). Each record names the animation frame
// that triggers the spawn, the effect type and depth group, and a local offset.
// 14 records each; the byte after the last record is not part of the table.
//
//   A - muzzle flash, spawned in the weapon joint's space
//   B - ejected shell / smoke, spawned in the ENTITY's own matrix
//   C - secondary flash, again in the weapon joint's space
//
// A frame value of 0x63 (99) is the original's "disabled" marker: no animation
// reaches frame 99, so the test never fires.
// ============================================================================
struct NpcFireFx {
    unsigned char frame;    // animation_frame_id that triggers this spawn
    unsigned char type;     // Effect_CreateBillboard type
    unsigned char depth;    // depth group
    unsigned char pad;
    short x, y, z;          // local offset
};

static const NpcFireFx kFireFxMuzzle[14] = {   // 0x004c0dd8
    { 0x01, 0x11, 0x00, 0,  110,  540,   0 }, { 0x01, 0x11, 0x01, 0,  640, 1110,   0 },
    { 0x01, 0x11, 0x02, 0,  160,  610,   0 }, { 0x01, 0x11, 0x0A, 0,  160,  610,   0 },
    { 0x00, 0x00, 0x00, 0,    0,    0,   0 }, { 0x02, 0x08, 0x07, 0,  400,  660,   0 },
    { 0x02, 0x08, 0x07, 0,  400,  660,   0 }, { 0x02, 0x08, 0x07, 0,  400,  660,   0 },
    { 0x01, 0x0B, 0x09, 0, -190, 1020,  90 }, { 0x01, 0x0B, 0x09, 0, -190, 1020, -60 },
    { 0x01, 0x0B, 0x09, 0,  -60, 1040,  90 }, { 0x01, 0x0B, 0x09, 0,  -60, 1040, -60 },
    { 0x01, 0x11, 0x00, 0,  110,  540,   0 }, { 0x01, 0x11, 0x00, 0,  600, 1370,   0 },
};

static const NpcFireFx kFireFxShell[14] = {    // 0x004c0e68
    { 0x03, 0x05, 0x00, 0,  370, -2870, -220 }, { 0x19, 0x05, 0x09, 0,  360, -2050, -440 },
    { 0x63, 0x00, 0x00, 0,    0,     0,    0 }, { 0x63, 0x00, 0x00, 0,    0,     0,    0 },
    { 0x00, 0x00, 0x00, 0,    0,     0,    0 }, { 0x63, 0x00, 0x00, 0,    0,     0,    0 },
    { 0x63, 0x00, 0x00, 0,    0,     0,    0 }, { 0x63, 0x00, 0x00, 0,    0,     0,    0 },
    { 0x02, 0x09, 0x0B, 0, 1400, -2800, -300 }, { 0x00, 0x00, 0x00, 0,    0,     0,    0 },
    { 0x00, 0x00, 0x00, 0,    0,     0,    0 }, { 0x00, 0x00, 0x00, 0,    0,     0,    0 },
    { 0x03, 0x05, 0x00, 0,  250, -1900, -250 }, { 0x03, 0x05, 0x00, 0,  250, -1900, -250 },
};

static const NpcFireFx kFireFxFlash2[14] = {   // 0x004c0ef8
    { 0x02, 0x09, 0x0B, 0, 110,  500,   0 }, { 0x02, 0x09, 0x0B, 0, 640, 1060,   0 },
    { 0x02, 0x09, 0x0B, 0, 160,  610,   0 }, { 0x02, 0x09, 0x0B, 0, 160,  610,   0 },
    { 0x00, 0x00, 0x00, 0,   0,    0,   0 }, { 0x02, 0x09, 0x0B, 0, 640, 1060,   0 },
    { 0x02, 0x09, 0x0B, 0, 640, 1060,   0 }, { 0x02, 0x09, 0x0B, 0, 640, 1060,   0 },
    { 0x02, 0x08, 0x02, 0, 430, -830,  90 }, { 0x02, 0x08, 0x02, 0, 430, -830, -60 },
    { 0x02, 0x08, 0x02, 0, 570, -810,  90 }, { 0x02, 0x08, 0x02, 0, 570, -810, -60 },
    { 0x02, 0x09, 0x0B, 0, 110,  500,   0 }, { 0x02, 0x09, 0x0B, 0, 640, 1500,   0 },
};

// The original indexes the three tables with a raw byte and never bounds it.
// The port reports instead of reading past them - a weapon id this high means
// behavior_flags was never initialised, which is worth seeing in the log.
static const NpcFireFx* fire_fx_row(const NpcFireFx* table, unsigned int idx)
{
    return (idx < 14) ? &table[idx] : NULL;
}

// Effect_CreateBillboard returns 0xFF when the pool is full, and the original
// stores that into a SIGNED char before using it as an index - so a full pool
// writes g_effectPool[-1]. The port's .bss neighbours differ from the original's,
// so that stray write would corrupt something else entirely here; guarded.
static void fire_fx_tag(unsigned char slot, unsigned int field, unsigned char value)
{
    g_playerDisplacement = (int)(signed char)slot;
    if (g_playerDisplacement >= 0 && g_playerDisplacement < 64) {
        g_effectPool[g_playerDisplacement].animHeader[field] = value;
    }
}

// ============================================================================
// npc_scd_08 (0x0047b280) - behaviour 8: fire the equipped weapon.
//
// THE cutscene gunfire behaviour. Barry shooting the zombie in the Jill dining
// room (ROOM1051) is `85 08 11 21` -> state 8, action_behavior 8,
// animationId 0x11, scd_anim_param 0x21; the script then spins on
// `bit_test(g_SysFlags, 0x21)` until action_state 2 sets that bit. While this
// was a stub nothing ever set it, so the scene hung forever with no muzzle
// flash - both halves of the same missing function.
//
// action_state walks 0 -> 1 -> 2 (Joint_move returns 1 on the frame the
// animation loops, and the result is ADDED to action_state, so the state
// advances exactly when the anim completes). Weapon id 3 takes the 0/3 path
// instead, which plays animation 0x17 with no effects at all.
// States 4/5 are the flamethrower: a continuous type-0x0C billboard every 6th
// frame plus a looping two-sound burst, and a per-frame yaw sweep.
// ============================================================================
static void npc_scd_08(void)
{
    unsigned char weapon = (unsigned char)(ENTITY->behavior_flags - 2);
    unsigned int  idx    = (unsigned int)weapon;

    switch (ENTITY->action_state) {
    case 0:
        ENTITY->animation_frame_id = 0;
        ENTITY->timing_control     = 0;
        ENTITY->action_state       = 1;
        ENTITY->blend_counter      = 3;
        if (weapon == 3) {
            ENTITY->action_state = 3;
            ENTITY->animationId  = 0x17;
            goto play_anim;
        }
        // fall through to the firing frame tests
    case 1: {
        char* joints = (char*)ENTITY->jointsStructs;
        // +0x70C is joint 14's `world` matrix (14 * 0x7C + 0x44) - the weapon hand.
        void* weaponSpace = (void*)(joints + 0x70C);

        const NpcFireFx* a = fire_fx_row(kFireFxMuzzle, idx);
        if (a != NULL && ENTITY->animation_frame_id == a->frame) {
            g_playerPosScratch.x = (int)a->x;
            g_playerPosScratch.y = (int)a->y;
            g_playerPosScratch.z = (int)a->z;
            Effect_CreateBillboard(a->type, a->depth, 0, weaponSpace,
                                   &g_playerPosScratch, 0);
            if (weapon == 2) {
                g_playerPosScratch.x = 0x96;
                g_playerPosScratch.y = 0x17C;
                g_playerPosScratch.z = 0;
                Effect_CreateBillboard(0x11, 0x03, 0, weaponSpace,
                                       &g_playerPosScratch, 0);
            }
        }

        const NpcFireFx* b = fire_fx_row(kFireFxShell, idx);
        if (b != NULL && ENTITY->animation_frame_id == b->frame) {
            g_playerPosScratch.x = (int)b->x;
            g_playerPosScratch.z = (int)b->z;
            // Odd-id characters get a 300-unit lift, scaled by (1 - weapon) -
            // which goes NEGATIVE for weapon >= 2. The original computes this in
            // unsigned arithmetic and stores into an int, so the wrap is the
            // intended signed result; written signed here for the same bits.
            g_playerPosScratch.y = (int)(ENTITY->id & 1) * (1 - (int)idx) * 300
                                 + (int)b->y;
            unsigned char slot = Effect_CreateBillboard(
                b->type, b->depth, (short)(((weapon == 8) - 1) & 0x555),
                &ENTITY->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
            fire_fx_tag(slot, 3, weapon);
        }

        const NpcFireFx* c = fire_fx_row(kFireFxFlash2, idx);
        if (c != NULL && ENTITY->animation_frame_id == c->frame) {
            g_playerPosScratch.x = (int)c->x;
            g_playerPosScratch.y = (int)c->y;
            g_playerPosScratch.z = (int)c->z;
            unsigned char slot = Effect_CreateBillboard(
                c->type, c->depth, 0, weaponSpace, &g_playerPosScratch, 0);
            fire_fx_tag(slot, 0, weapon);
        }
        goto play_anim;
    }

    case 2:
        // The completion signal the event script is waiting on.
        Flg_on((int)g_SysFlags, ENTITY->scd_anim_param);
        return;

    case 3:
    play_anim: {
        int done = (int)Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);
        ENTITY->action_state = (unsigned char)(ENTITY->action_state + (char)done);
        return;
    }

    case 4:
        ENTITY->action_state         = 5;
        ENTITY->timing_control       = 0;
        ENTITY->animationId          = 0x14;
        ENTITY->blend_counter        = 3;
        ENTITY->action_ticks_counter = 0x0F;
        // fall through
    case 5: {
        if (ENTITY->animation_frame_id % 6 == 0) {
            g_playerPosScratch.x = 0x21C;
            g_playerPosScratch.y = 0x4EC;
            g_playerPosScratch.z = 0;
            Effect_CreateBillboard(0x0C, 0, 0,
                                   (void*)((char*)ENTITY->jointsStructs + 0x70C),
                                   &g_playerPosScratch, 0);
        }
        short ticks = (short)ENTITY->action_ticks_counter;
        ENTITY->action_ticks_counter = (unsigned short)(ticks - 1);
        if (ticks == 0) {
            ENTITY->action_ticks_counter = 0x0F;
            Play3DSnd(2, 0x1E, 0, (unsigned int)ENTITY->scaMatrixData.localMatrix.t);
            Play3DSnd(2, 0x1F, 0, (unsigned int)ENTITY->scaMatrixData.localMatrix.t);
        }
        Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400);
        ENTITY->angle = (short)(ENTITY->angle + (short)ENTITY->scd_timer);
        return;
    }

    default:
        return;
    }
}
// ============================================================================
// npc_scd_09 (0x0047b6b0) - behaviour 9: play an animation in REVERSE, then
// clear the behaviour and release the event script.
//
// Same shape as npc_scd_07 with three differences: Joint_move is called with
// reverse = 1 (a hard-coded 1, not `scd_entity_flags & 1`) and a 0x400 blend
// step, the blend counter starts at 3 rather than 7, and the terminal state
// clears action_behavior AND action_state together (`MOV word ptr [..+0x86],0`)
// before signalling. There is no yaw step at the tail.
//
// A sweep of every RDT shows opcode 0x85 only ever selects behaviours 7, 8 and
// 9, so with this one the whole set the scripts actually reach is implemented;
// 3/4/5/10 remain stubs because nothing selects them.
// ============================================================================
static void npc_scd_09(void)
{
    char st = (char)ENTITY->action_state;

    if (st == 0) {
        ENTITY->animation_frame_id = 0;
        ENTITY->timing_control     = 0;
        ENTITY->action_state       = 1;
        ENTITY->blend_counter      = 3;
        // falls through into the Joint_move block
    } else if (st != 1) {
        if (st != 2) {
            return;
        }
        // One 16-bit store at +0x86 clears action_behavior and action_state.
        ENTITY->action_behavior = 0;
        ENTITY->action_state    = 0;
        Flg_on((int)g_SysFlags, ENTITY->scd_anim_param);
        return;
    }

    {
        char done = (char)Joint_move(1, ENTITY->animHeader, ENTITY->animBase, 0x400);
        ENTITY->action_state = (unsigned char)(ENTITY->action_state + done);
    }
}
// ============================================================================
// npc_scd_10 (0x0047b760) - behaviour 10: play the animation, signal, and
// optionally LOOP.
//
// npc_scd_07 with two differences: the entry state zeroes move_speed_current and
// uses blend counter 7, and the terminal state re-arms itself. At action_state 2
// it raises the completion flag every frame, and when scd_entity_flags bit 4 is
// set it resets action_state to 0 so the whole animation replays - a scripted
// idle loop that keeps the event VM's wait satisfied. Without bit 4 it parks at
// state 2 and just keeps re-raising the flag.
// ============================================================================
static void npc_scd_10(void)
{
    char st = (char)ENTITY->action_state;

    if (st == 0) {
        ENTITY->animation_frame_id = 0;
        ENTITY->timing_control     = 0;
        ENTITY->action_state       = 1;
        ENTITY->blend_counter      = 7;
        ENTITY->move_speed_current = 0;
        // fall through into the animation step
    } else if (st != 1) {
        if (st != 2) {
            return;
        }
        Flg_on((int)g_SysFlags, ENTITY->scd_anim_param);
        if ((ENTITY->scd_entity_flags & 0x10) == 0) {
            return;
        }
        ENTITY->action_state = 0;   // loop
        return;
    }

    {
        char done = (char)Joint_move((char)(ENTITY->scd_entity_flags & 1),
                                     ENTITY->animHeader, ENTITY->animBase, 0x200);
        ENTITY->action_state = (unsigned char)(ENTITY->action_state + done);
    }
}

// 0x004c4780 - 11 entries, indexed by action_behavior.
static void* const g_npcScdBehaviors[11] = {
    (void*)npc_scd_00, (void*)npc_scd_01, (void*)npc_scd_02, (void*)npc_scd_03,
    (void*)npc_scd_04, (void*)npc_scd_05, (void*)npc_scd_06, (void*)npc_scd_07,
    (void*)npc_scd_08, (void*)npc_scd_09, (void*)npc_scd_10,
};

// ============================================================================
// npc_state8_action_update (0x0047a490) - NPC state 8
// The SCD-driven animation state. Runs the behaviour handler once, a second time
// when scd_entity_flags bit 1 asks for a double step, then updates the held
// weapon joint when bit 2 is set (bit 3 selects which hand).
// ============================================================================
static void npc_state8_action_update(void)
{
    unsigned char behavior = ENTITY->action_behavior;
    if (behavior >= 11) {
        npc_scd_report("action_behavior out of range");
        return;
    }

    ((void(*)(void))g_npcScdBehaviors[behavior])();
    if ((ENTITY->scd_entity_flags & 2) != 0) {
        ((void(*)(void))g_npcScdBehaviors[ENTITY->action_behavior])();
    }
    if ((ENTITY->scd_entity_flags & 4) != 0) {
        EntityUpdateWeaponJoint((ENTITY->scd_entity_flags >> 3) & 1);
    }
}

// ============================================================================
// The dispatch table, in id-space starting at id 22. See the header comment for
// why the state, init and idle views all live in one array.
// ============================================================================
static void npc_state0_init(void);
static void npc_state1_idle(void);
static void npc_idle_walk_00(void);   // indexes the table below - see its body

#define NPC_DISPATCH_BASE_ID 22
#define NPC_IDLE_VIEW_ID     48

// 45 entries, not 42: the idle view runs to index 18 in the original (array
// slots 48-66 are all real handlers, and 67 onward are NULL). Scripts do set
// action_behavior as high as 0x17 via cmd_enemy_0x28 sub-command 2, which would
// be a NULL call in the original too - npc_state1_idle reports those rather than
// jumping to address 0.
static void* const g_npcDispatch[45] = {
    // --- states 0..9, reached as g_npcDispatch[state] (base 0x004c2c50) ---
    /* id 22 / state 0 */ (void*)npc_state0_init,          // 0x0046ad80
    /* id 23 / state 1 */ (void*)npc_state1_idle,          // 0x0046b560
    /* id 24 / state 2 */ nullptr,
    /* id 25 / state 3 */ nullptr,
    /* id 26 / state 4 */ nullptr,
    /* id 27 / state 5 */ nullptr,
    /* id 28 / state 6 */ nullptr,
    /* id 29 / state 7 */ nullptr,
    /* id 30 / state 8 */ (void*)npc_state8_action_update,  // 0x0047a490
    /* id 31 / state 9 */ (void*)npc_state9_pathfind,       // 0x00471950
    // --- per-character init, reached as g_npcDispatch[id - 22] ---
    /* id 32 chris   */ (void*)char_init_chris,             // 0x0046adf0
    /* id 33 jill    */ (void*)char_init_jill,              // 0x0046ae80
    /* id 34 barry   */ (void*)char_init_barry,             // 0x0046af10
    /* id 35 rebecca */ (void*)char_init_rebecca,           // 0x0046afa0
    /* id 36 wesker  */ (void*)char_init_wesker,            // 0x0046b100
    /* id 37         */ (void*)char_init_37,                // 0x0046b230
    /* id 38         */ (void*)char_init_38,                // 0x0046b2e0
    /* id 39 richard */ (void*)char_init_richard,           // 0x0046b390
    /* id 40 enrico  */ (void*)char_init_enrico,            // 0x0046b420
    /* id 41         */ (void*)char_init_41,                // 0x0046b4b0
    /* id 42         */ (void*)char_init_barry,             // 0x0046af10 (alias)
    /* id 43         */ (void*)char_init_barry,             // 0x0046af10 (alias)
    /* id 44         */ (void*)char_init_rebecca,           // 0x0046afa0 (alias)
    /* id 45         */ (void*)char_init_barry,             // 0x0046af10 (alias)
    /* id 46         */ (void*)char_init_wesker,            // 0x0046b100 (alias)
    /* id 47         */ nullptr,
    // --- idle behaviours, reached as g_npcDispatch[26 + action_behavior]
    //     (base 0x004c2cb8 = 0x004c2bf8 + 48*4) ---
    /* id 48 / idle 0  */ (void*)npc_idle_walk_00,          // 0x0046b580
    /* id 49 / idle 1  */ (void*)npc_idle_walk_01,          // 0x0046b620
    /* id 50 / idle 2  */ (void*)npc_idle_walk_02,          // 0x0046b800
    /* id 51 / idle 3  */ (void*)npc_idle_walk_03,          // 0x0046bb20
    /* id 52 / idle 4  */ (void*)npc_idle_nop,              // 0x0046b5a0
    /* id 53 / idle 5  */ (void*)npc_idle_nop,
    /* id 54 / idle 6  */ (void*)npc_idle_nop,
    /* id 55 / idle 7  */ (void*)npc_idle_nop,
    /* id 56 / idle 8  */ (void*)npc_idle_nop,
    /* id 57 / idle 9  */ (void*)npc_idle_play_anim,        // 0x0046b5b0
    /* id 58 / idle 10 */ (void*)npc_idle_play_anim,
    /* id 59 / idle 11 */ (void*)npc_idle_nop,
    /* id 60 / idle 12 */ (void*)npc_idle_nop,
    /* id 61 / idle 13 */ (void*)npc_idle_play_anim,       // 0x0046b5b0
    /* id 62 / idle 14 */ (void*)npc_idle_nop,             // 0x0046b5a0
    /* id 63 / idle 15 */ (void*)npc_idle_nop,
    /* id 64 / idle 16 */ (void*)npc_idle_nop,
    /* id 65 / idle 17 */ (void*)npc_idle_nop,
    /* id 66 / idle 18 */ (void*)npc_idle_nop,             // last real entry;
                                                           // 67+ are NULL in the
                                                           // original
};

// ============================================================================
// npc_state1_idle (0x0046b560) - NPC state 1
// A bare tail-jump through the idle table: JMP [action_behavior*4 + 0x4c2cb8].
// ============================================================================
static void npc_state1_idle(void)
{
    unsigned int idx = (NPC_IDLE_VIEW_ID - NPC_DISPATCH_BASE_ID) + ENTITY->action_behavior;
    if (idx >= 45 || g_npcDispatch[idx] == nullptr) {
        // action_behavior above 18 is a NULL slot in the original's table too, so
        // this reports what would have been a jump to address 0.
        npc_report_missing("idle behavior slot");
        return;
    }
    ((void(*)(void))g_npcDispatch[idx])();
}

// ============================================================================
// npc_idle_walk_00 (0x0046b580) - idle behaviour 0.
//
// Not a behaviour at all: a bare tail-jump that re-dispatches on the ENTITY'S ID
// through the SAME overlapping pointer block, at id-space index 20 + id.
//
//   0046b580: MOV EAX,[0x00bebcd4]          ; ENTITY
//             XOR ECX,ECX
//             MOV CL,byte ptr [EAX+0x1]     ; ENTITY->id
//             JMP dword ptr [ECX*0x4 + 0x004c2c48]
//
// 0x004c2c48 is the block base + 20*4, so the slot reached is array[20 + id],
// i.e. g_npcDispatch[id - 2] in this file's id-space numbering. For the ids the
// scripts actually spawn (32-41) that lands in the idle tail: Chris/Jill/Barry/
// Rebecca/Wesker (32-36) and 39/40 resolve to the no-op, while 37, 38 and 41
// resolve to the play-animation handler. That is why an idling Rebecca produced
// "unimplemented idle behavior 0" and yet nothing looked wrong - the correct
// behaviour for her IS to do nothing.
//
// Ids 28-31 map back onto idle 0-3, so id 28 re-enters this function forever.
// No script uses those ids; guarded rather than reproduced, because unbounded
// recursion here takes the whole process down instead of hanging one actor.
// ============================================================================
static void npc_idle_walk_00(void)
{
    unsigned int id = ENTITY->id;

    if (id == 28) {
        npc_report_missing("idle behavior 0 self-recursion (id 28)");
        return;
    }
    if (id < 2) {
        npc_report_missing("idle behavior 0 with id < 2");
        return;
    }

    unsigned int idx = id - 2;   // array[20 + id] in g_npcDispatch's numbering
    if (idx >= 45 || g_npcDispatch[idx] == nullptr) {
        npc_report_missing("idle behavior 0 id slot");
        return;
    }
    ((void(*)(void))g_npcDispatch[idx])();
}

// ============================================================================
// npc_state0_init (0x0046ad80) - NPC state 0
// One-shot spawn init. The first store is a dword at +0x84, which sets state to
// 1 and clears ignore_player_flag, action_behavior and action_state in one go -
// so a character drops straight into state 1 on the frame after cmd_em_set.
// ============================================================================
static void npc_state0_init(void)
{
    // 0x0046ad87: MOV dword ptr [EAX+0x84],1
    ENTITY->state              = 1;
    ENTITY->ignore_player_flag = 0;
    ENTITY->action_behavior    = 0;
    ENTITY->action_state       = 0;

    // 0x0046ad96 / 0x0046ad9f: MOV word ptr [EAX+0x72],0 and [EAX+0x76],0.
    // These are the X and Z components of the rotation SVECTOR that RotMatrix
    // reads from entity+0x72 - they must NOT touch +0x74, which is the yaw
    // cmd_em_set just wrote from the script. Writing them via the nearest struct
    // field names hit +0x6C (position.x) and +0x74 (angle) instead, which zeroed
    // every actor's facing one frame after it spawned. Addressed by offset here
    // because no single named field lands on either address.
    *(short*)((char*)ENTITY + 0x72) = 0;
    *(short*)((char*)ENTITY + 0x76) = 0;
    ENTITY->health               = -1;  // +0x88
    *(unsigned short*)ENTITY->pad_ca = 0;

    ResetJointTransforms();

    // 0x0046adcd: CALL [id*4 + 0x4c2bf8] - the per-character init
    unsigned int idx = (unsigned int)ENTITY->id - NPC_DISPATCH_BASE_ID;
    if (idx >= 42 || g_npcDispatch[idx] == nullptr) {
        char_init_missing();
    } else {
        ((void(*)(void))g_npcDispatch[idx])();
    }

    SetEntityScaHitData(ENTITY);
}

// ============================================================================
// character_npc_update (0x0046acf0)
// The shared per-frame driver for every character entity. Bit 1 of
// g_message_flags is the "entities may think" gate, so during a message box the
// state machine freezes but the shadow sprite is still queued.
// ============================================================================
void character_npc_update(void)
{
    if ((g_message_flags & 2) != 0) {
        unsigned char state = ENTITY->state;
        if (state < 10 && g_npcDispatch[state] != nullptr) {
            ((void(*)(void))g_npcDispatch[state])();
        } else {
            npc_report_missing("state slot");
        }
        EntityUpdateLookAtAngles();
    }

    ENTITY->scaMatrixData.field_00 = 0;
    ENTITY->has_enter_switch_zone = (unsigned char)is_entity_in_switch_zone(
        (VECTOR*)ENTITY->scaMatrixData.localMatrix.t, g_CurrentRdtDataTypePtr);

    if ((ENTITY->has_enter_switch_zone != 0) &&
        ((ENTITY->scd_entity_flags & 0x40) == 0)) {
        entity_add_fade_sprite(
            (VECTOR*)ENTITY->scaMatrixData.localMatrix.t,
            (short*)&ENTITY->pushVelocity,
            (short)ENTITY->scaMatrixData.localMatrix.t[1],
            (short)ENTITY->angle);
    }
}
