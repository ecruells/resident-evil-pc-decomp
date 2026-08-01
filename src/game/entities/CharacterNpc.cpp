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
// Idle behaviours 0-3 (0x0046b580, 0x0046b620, 0x0046b800, 0x0046bb20) and NPC
// state 9 (0x00471950) are the walk/pathfind layer. They are not transcribed
// yet - state 9 alone needs entity_pathfind_update, FUN_00471e70/e90/2570,
// FUN_00460230 and ResolveEntityScaCollision, none of which the port has.
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

static void npc_idle_walk_00(void)  { npc_report_missing("idle behavior 0 (0x0046b580)"); }
static void npc_idle_walk_01(void)  { npc_report_missing("idle behavior 1 (0x0046b620)"); }
static void npc_idle_walk_02(void)  { npc_report_missing("idle behavior 2 (0x0046b800)"); }
static void npc_idle_walk_03(void)  { npc_report_missing("idle behavior 3 (0x0046bb20)"); }
static void npc_state9_pathfind(void) { npc_report_missing("state 9 (0x00471950)"); }
static void char_init_missing(void) { npc_report_missing("character init"); }

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
static void npc_scd_03(void) { npc_scd_report("0x0047a9c0"); }
static void npc_scd_04(void) { npc_scd_report("0x0047ad30"); }
static void npc_scd_05(void) { npc_scd_report("0x0047aef0"); }
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
static void npc_scd_07(void) { npc_scd_report("0x0047b1c0"); }
static void npc_scd_08(void) { npc_scd_report("0x0047b280"); }
static void npc_scd_09(void) { npc_scd_report("0x0047b6b0"); }
static void npc_scd_10(void) { npc_scd_report("0x0047b760"); }

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

#define NPC_DISPATCH_BASE_ID 22
#define NPC_IDLE_VIEW_ID     48

static void* const g_npcDispatch[42] = {
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
    /* id 61 / idle 13 */ (void*)npc_idle_play_anim,
    /* id 62 / idle 14 */ (void*)npc_idle_nop,
    /* id 63 / idle 15 */ (void*)npc_idle_nop,
};

// ============================================================================
// npc_state1_idle (0x0046b560) - NPC state 1
// A bare tail-jump through the idle table: JMP [action_behavior*4 + 0x4c2cb8].
// ============================================================================
static void npc_state1_idle(void)
{
    unsigned int idx = (NPC_IDLE_VIEW_ID - NPC_DISPATCH_BASE_ID) + ENTITY->action_behavior;
    if (idx >= 42 || g_npcDispatch[idx] == nullptr) {
        npc_report_missing("idle behavior slot");
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
