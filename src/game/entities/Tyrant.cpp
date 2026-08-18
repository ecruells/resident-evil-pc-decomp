// Tyrant.cpp - Tyrant boss (enemy types 12 and 16), decompiled from Ghidra.
//
// Both ids run the SAME code: enemies_update_functions_tbl[12] and [16] point
// at 0x00421990.  id 12 is em100C, the lab Tyrant that starts strapped to the
// slab and impales Wesker; id 16 is em1010, the heliport Tyrant that erupts from
// the floor and dies to the rocket launcher.  Everything that differs is
// gated on `ENTITY->id == 0x10` inside the shared behaviours.
//
// Original PC addresses
// ---------------------------------------------------------------------------
//   tyrant_update            0x00421990   per-frame entry
//   state table              0x004ba280   10 slots, indexed by ENTITY->state
//     [0] 0x004212b0  init          [5] NULL
//     [1] 0x00421d10  think + act   [6] NULL
//     [2] 0x00421d60  hit reaction  [7] NULL
//     [3] 0x00421e10  forced act    [8] 0x0045c610  SCD-driven
//     [4] 0x00421e50  RET           [9] NULL
//   behaviour table          0x004ba308   16 slots (see s_tyrantBehaviors)
//   SCD behaviour table      0x004c10c8   24 slots, ONLY reachable from state 8
//   tyrant SCA record        0x004ba240   (PTR_DAT_004ba24c points at it)
//   claw-ghost/heart scratch 0x004ba250 - 0x004ba27f
//   heart wobble table       0x004ba370   22 signed bytes
//   pad-rumble tables        0x004ba2a8 / 0x004ba2c8   -> DEAD, see below
//
// TWO DISPATCH SITES, TWO BASES - the load-bearing detail
// ---------------------------------------------------------------------------
// The behaviour jumptable is indexed by ENTITY->action_behavior from two
// different places, with bases EIGHT BYTES apart:
//
//   0x00421e47  (state 3)  JMP [ECX*4 + 0x4ba308]      -> table[behavior]
//   0x00421ea9  (state 1)  JMP [ECX*4 + 0x4ba310]      -> table[behavior + 2]
//
// So the same action_behavior byte selects a DIFFERENT behaviour depending on
// which state wrote it.  State 1 is the normal "pathfind, then act" path and
// state 3 is "act right now, no pathfinding"; state 3 only ever runs with
// behaviour 0 (id 12: strapped to the slab) or 1 (id 16: hand control to the
// SCD), which is why the +2 skew is invisible in normal play and lethal if you
// collapse the two tables into one.  Reproduced here as TYRANT_STATE1_BEHAVIOR_BIAS.
//
// Everything the AI writes into action_behavior is therefore in STATE-1 space:
//
//   written  table  behaviour
//     0        2    pause          5       7    claw thrust
//     1        3    walk           6       8    backhand
//     2        4    (RET)          7       9    impale (the kill move)
//     3        5    claw swipe     8      10    charge
//     4        6    claw slash     9      11    eruption entrance
//                                 10      12    rush
//                                 11      13    rise / power up
//
// which is why `action_behavior += 4` turns a swipe into an impale (3 -> 7) at
// three separate sites, and why init's 0x00090101 for id 16 selects the
// eruption rather than table[9].
//
// What is deliberately NOT ported
// ---------------------------------------------------------------------------
//   * FUN_004259f0 (0x004259f0) is a bare RET in the shipped exe - the PSX pad
//     rumble, compiled out for PC.  Its two argument tables (0x004ba2a8 signed
//     bytes and 0x004ba2c8 shorts, 30 entries each, indexed by the 0x16D step
//     phase) are therefore dead data and are not reproduced.  The 0x16D counter
//     itself still ticks, because the behaviours read it.
//   * FUN_0048aec0 / FUN_0048aef0 (0x0048aec0 / 0x0048aef0) are the claw-swipe
//     motion-blur RIBBON: a CMarniViewport2 quad strip built straight on the
//     D3D device through FUN_00486280 / FUN_004865a0 / RotAverage4, none of
//     which exist in the port yet.  The allocation half of FUN_0048aec0 IS
//     reproduced (it advances g_loadDataDestPointer by 9 * 0x100 bytes, and
//     skipping that would make every later room allocation overlap); the draw
//     half is a no-op.  All of the ribbon's STATE - DAT_004ba264's countdown and
//     its 0x8000 "arm" bit, DAT_004ba268, the 0x004ba27a sweep - is kept exact,
//     so behaviour timing is unaffected.
//   The two CLAW GHOST copies (FUN_00421790) and the exposed HEART
//   (FUN_00425840) are fully ported - they go through FUN_00483250, which the
//   port has.
//
// Field offsets are raw on purpose.  The Tyrant reuses the generic movement
// bytes at different widths and meanings (0x170 is a POINTER, 0x174 is a
// 32-bit copy of the whole state word, 0x17C is a 16-bit distance), so naming
// them through the generic Entity fields would be actively misleading.
// ===========================================================================
#include "EntityCommon.h"
#include "../../Globals.h"
#include "../Items.h"
#include "../../DebugPrint.h"
#include <cstring>
#include <cstdlib>

extern void ResetJointTransforms(void);                                   // 0x0048bad0
extern void SetAnimSlot(AnimSlot* slots, int slotPtr, int index);         // 0x0048b6b0
extern unsigned int* CreateAnimObject(int slotPtr, unsigned int* param2); // 0x0048b700
extern void Flg_on(int baseAddr, unsigned int bitIndex);                  // 0x00473ef0
extern void update_entity_lighting(VECTOR* entityPos);                    // 0x004830d0
extern int  is_entity_in_switch_zone(VECTOR* pos, void* zoneData);        // 0x00462d90
extern void play_sound_and_voice_effect(int type, int id);                // SoundSystem.cpp
extern void EntityUpdateLookAtAngles(void);                               // 0x00459eb0
extern int  player_distance_z;                                            // 0x00be0de4
extern unsigned int g_entity_bkp;                                         // 0x00be0df4
extern int  g_collPushDepthZHi;                                           // 0x00be0dec
extern int  g_collPushDepthZLo;                                           // 0x00be0df0
extern void TexturePage_DeleteSet(int set);

namespace {

// ---------------------------------------------------------------------------
// Raw field access.  See the file header for why these are not Entity fields.
// ---------------------------------------------------------------------------
inline signed char&    eb (void* e, unsigned o) { return *reinterpret_cast<signed char*>((char*)e + o); }
inline unsigned char&  eub(void* e, unsigned o) { return *reinterpret_cast<unsigned char*>((char*)e + o); }
inline short&          ew (void* e, unsigned o) { return *reinterpret_cast<short*>((char*)e + o); }
inline unsigned short& euw(void* e, unsigned o) { return *reinterpret_cast<unsigned short*>((char*)e + o); }
inline int&            ei (void* e, unsigned o) { return *reinterpret_cast<int*>((char*)e + o); }
inline unsigned int&   eu (void* e, unsigned o) { return *reinterpret_cast<unsigned int*>((char*)e + o); }

// ENTITY+0x84 is written as one DWORD at ~30 sites:
// state | ignore_player<<8 | action_behavior<<16 | action_state<<24.
inline void set_state_word(unsigned int v) { eu(ENTITY, 0x84) = v; }

// The Tyrant's own named views of the shared bytes.
inline short&          ty_speed(void)  { return ew(ENTITY, 0xc2);  }  // move_speed_current
inline short&          ty_ticks(void)  { return ew(ENTITY, 0xc4);  }  // 16-bit frame timer
inline unsigned char&  ty_anim(void)   { return eub(ENTITY, 0xbd); } // animationId
inline unsigned char&  ty_frame(void)  { return eub(ENTITY, 0xbe); } // animation_frame_id
inline unsigned char&  ty_sub(void)    { return eub(ENTITY, 0x87); } // action_state

// 0x16E - which attack has already connected this swing.
//   0x01 wide swipe   0x02 big slash   0x04 thrust   0x08 backhand
//   0x80 "this hit would have killed the player" (the impale trigger)
inline unsigned char&  ty_hitMask(void) { return eub(ENTITY, 0x16e); }

// 0x179 - Tyrant render/AI flags.
//   0x01 draw the ground shadow   0x02 look-at target is live
//   0x04 the "connected" sound for this swing already played
//   0x08 hidden: skip the heart, the claw ghosts and the rocket check
inline unsigned char&  ty_flags(void)   { return eub(ENTITY, 0x179); }

// Joint N's world matrix (joints are 0x7C, world at +0x44).  The Tyrant's claw
// is joint 8, which is why 0x3E0 (8*0x7C) and 0x424 (8*0x7C+0x44) appear raw
// all over the original.
inline JointStruct* ty_joints(void) { return ENTITY->jointsStructs; }
inline MATRIX* ty_clawWorld(void)   { return reinterpret_cast<MATRIX*>((char*)ENTITY->jointsStructs + 0x424); }
inline JointStruct* ty_clawJoint(void) { return reinterpret_cast<JointStruct*>((char*)ENTITY->jointsStructs + 0x3e0); }

inline int* ty_playerT(void) { return g_playerEntity.scaMatrixData.localMatrix.t; }

// ---------------------------------------------------------------------------
// 0x004ba240 - the Tyrant's SCA collision record, six shorts.
// {terminator|id, x, y, z, half-height, radius}.  PTR_DAT_004ba24c points here
// and state 0 stores that pointer into Entity+0x04.
// ---------------------------------------------------------------------------
const short s_tyrantScaInfo[6] = { (short)0x8000, 0, (short)-2000, 0, 2000, 800 };

// ---------------------------------------------------------------------------
// 0x004ba370 - 22 signed bytes, indexed by the heart copy's own 0xC4 counter as
// it runs 21 -> 0 and reloads.  Drives the exposed heart's beat.
// ---------------------------------------------------------------------------
const signed char s_tyrantHeartBeat[22] = {
    -122, -114,  -98,  -74,    0,   82,   72,   52,
       0,  -52,  -72,  -82,    0,   74,   98,  114,
     122,  102,   62,    0,  -62, -102
};

} // namespace

// ===========================================================================
// The 0x004ba250 - 0x004ba27f scratch block.
//
// NOT file-static and NOT a struct: the original addresses every field
// individually and two of them (0x004ba254 / 0x004ba258) are the claw's
// "growth" counters that behaviours 0x0D and 0x0C push around.
// ===========================================================================
static void*        g_tyClawGhostBlock = nullptr;   // 0x004ba250 - 2 x 0x7C joint copies
static int          g_tyClawScaleA     = 3000;      // 0x004ba254
static int          g_tyClawScaleB     = 300;       // 0x004ba258
static int          g_tyClawScaleStep  = 200;       // 0x004ba25c
static void*        g_tyTrailBlock     = nullptr;   // 0x004ba260 - ribbon buffer
static unsigned short g_tyTrailTimer   = 0;         // 0x004ba264 - 0x8000 = arm, low bits = frames
static int          g_tyTrailSegments  = 8;         // 0x004ba268
static SVECTOR      g_tyTrailNear      = { 0, (short)-300, 0, 0 };  // 0x004ba270
static SVECTOR      g_tyTrailFar       = { 1500, 0, 0, 0 };         // 0x004ba278

namespace {

// ---------------------------------------------------------------------------
// 0x0048aec0 - reserve the ribbon's vertex pool and build its D3D works.
//
// The allocation must happen even though the draw is stubbed: the original
// hands out 9 * 0x100 bytes of the room data buffer here and every later
// CreateAnimObject call starts from the advanced pointer.  Only the
// FUN_00486280 device work is skipped.
// ---------------------------------------------------------------------------
void tyrant_trail_alloc(unsigned char count, void* /*base*/, unsigned int /*tint*/)
{
    g_playerDisplacement = (int)count;
    g_loadDataDestPointer = (char*)g_loadDataDestPointer + (unsigned int)count * 0x100;
    // FUN_00486280(count * 2, tint) - CMarniViewport2 work creation, unported.
}

// ---------------------------------------------------------------------------
// 0x0048aef0 - push one matrix pair into the ribbon history (param_7 == 0) or
// scroll the history and draw it (param_7 != 0).
//
// Only the state half is meaningful without the renderer, and the state half is
// what the Tyrant's own code reads back, so both entry shapes are kept as
// no-ops here rather than half-implemented against a buffer nothing draws.
// ---------------------------------------------------------------------------
void tyrant_trail_push(void* /*buf*/, MATRIX* /*a*/, MATRIX* /*b*/,
                       SVECTOR* /*near*/, SVECTOR* /*far*/,
                       unsigned char /*slot*/, unsigned char /*count*/)
{
}

// ---------------------------------------------------------------------------
// Is `p` inside one of the room data pools?  The claw-ghost and ribbon blocks
// are raw offsets into g_loadDataDestPointer taken at spawn; a room reload
// leaves them dangling, and the original simply crashed.  Same guard shape
// Plant 42 uses for its clones.
// ---------------------------------------------------------------------------
bool tyrant_pool_pointer(const void* p, unsigned int bytes)
{
    uintptr_t ptr = (uintptr_t)p;
    uintptr_t end = ptr + bytes;
    if (ptr == 0 || end < ptr) return false;

    const struct { uintptr_t base; size_t size; } pools[] = {
        { (uintptr_t)&g_DataBuffer[0],         sizeof(g_DataBuffer)         },
        { (uintptr_t)&g_entityModelBuffer[0],  sizeof(g_entityModelBuffer)  },
        { (uintptr_t)&g_entityModelBuffer2[0], sizeof(g_entityModelBuffer2) },
    };
    for (const auto& pool : pools) {
        if (ptr >= pool.base && end <= pool.base + pool.size) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// The |dx| + |dz| distance to the player that the AI runs on, spelled out at
// eight sites in the original and always dropped into g_playerDisplacement.
// It is a Manhattan distance, NOT Euclidean - the thresholds (2000, 0xA8C,
// 5000, 6000, 7000, 12000) only make sense against this metric.
// ---------------------------------------------------------------------------
int tyrant_player_distance(void)
{
    int dz = (int)g_playerEntity.scaMatrixData.localMatrix.t[2] - ei(ENTITY, 0x3c);
    int dx = (int)g_playerEntity.scaMatrixData.localMatrix.t[0] - ei(ENTITY, 0x34);
    int sz = dz >> 31;
    int sx = dx >> 31;
    g_playerDisplacement = (((dz ^ sz) - sz) - sx) + (dx ^ sx);
    return g_playerDisplacement;
}

// Load g_playerPosScratch with the room's "dead move" anchor, y overridden.
// Every hit test and blood billboard in the file starts with this.
void tyrant_load_fx_anchor(int y)
{
    const int* dead = (const int*)((char*)(uintptr_t)g_deadMoveValue + 0x14);
    g_playerPosScratch.x   = dead[0];
    g_playerPosScratch.z   = dead[2];
    g_playerPosScratch.pad = dead[3];
    g_playerPosScratch.y   = y;
}

// ---------------------------------------------------------------------------
// Set the player's knock-back facing from the Tyrant->player delta.
//
// `base` is the angle bias the site loads into AX (0xFC00 for the wide slash at
// 0x00422b04, 0x0400 for the thrust and the rush at 0x00422f41 / 0x004249xx).
// `zeroBias` is the lone `INC AX` the 0x0400 sites carry on the dx == 0 path and
// the 0xFC00 site does not - checked instruction by instruction, it is a real
// asymmetry and not a decompiler artifact.
// ---------------------------------------------------------------------------
void tyrant_set_player_knockback(short base, int zeroBias)
{
    g_collPushDepthZHi = (int)g_playerEntity.scaMatrixData.localMatrix.t[0] - ei(ENTITY, 0x34);
    g_collPushDepthZLo = (int)g_playerEntity.scaMatrixData.localMatrix.t[2] - ei(ENTITY, 0x3c);

    g_playerEntity.animationId = 6;
    g_playerEntity.animFrameId = 0xc;
    g_playerEntity.action_behavior = 2;
    g_playerEntity.action_state = 0;

    if (g_collPushDepthZHi == 0) {
        unsigned short v = (unsigned short)(((g_collPushDepthZLo > 0) ? 1 : 0) + zeroBias);
        g_playerEntity.directionAngle = (short)(unsigned short)(v << 11);
        return;
    }
    g_entity_bkp = (unsigned int)GetAngleQuadrantValue(
        (g_collPushDepthZLo * 0x1000) / g_collPushDepthZHi);
    unsigned short cx = (unsigned short)(((g_collPushDepthZHi < 0) ? 1 : 0) << 11);
    g_playerEntity.directionAngle =
        (short)((unsigned short)((unsigned short)base - cx - (unsigned short)g_entity_bkp) & 0xfff);
}

// The common "claw connected" bookkeeping: tint the claw joint red-hot and put
// the player into the generic hit animation.
void tyrant_flash_claw_and_stagger(unsigned char playerBehavior)
{
    JointApplyColorTint(ty_clawJoint(), 0xff, 0x80880, (void*)0x808080);
    g_playerEntity.animationId = 6;
    g_playerEntity.animFrameId = 0xc;
    g_playerEntity.action_behavior = playerBehavior;
    g_playerEntity.action_state = 0;
}

// Point the player at the Tyrant and latch the "who hit me" pointer.
// `yawBias` is the temporary shift of the Tyrant's own yaw the original applies
// around is_facing_toward_entity so the reaction picks the right side.
void tyrant_latch_player_attacker(short yawBias)
{
    g_playerEntity.unk_b8 = (unsigned int)(uintptr_t)ENTITY;
    ew(ENTITY, 0x74) = (short)(ew(ENTITY, 0x74) + yawBias);
    g_playerEntity.attackAnim = (unsigned char)is_facing_toward_entity(&g_playerEntity);
    ew(ENTITY, 0x74) = (short)(ew(ENTITY, 0x74) - yawBias);
}

// Damage with the "defense item equipped" (g_PlayerFlags bit 0x7B) variant.
void tyrant_damage_player(int base, int withFlag)
{
    if (Flg_ck((int)g_PlayerFlags, 0x7b) == 0)
        g_playerEntity.health = (short)(g_playerEntity.health - base);
    else
        g_playerEntity.health = (short)(g_playerEntity.health - withFlag);
}

// Bleed the accumulated animation speed off, clamping at zero.  The original
// spells this out after every attack (`speed += frame * -N; if (speed < 0)
// speed = 0;`) with the multiply done in 16-bit.
void tyrant_decay_speed(int perFrame)
{
    euw(ENTITY, 0xc2) = (unsigned short)(ty_speed() + (unsigned short)ty_frame() * perFrame);
    if (ty_speed() < 0) ty_speed() = 0;
}

// ---------------------------------------------------------------------------
// FUN_00425710 (0x00425710) - the Tyrant's root-motion extractor.
//
// Composes the entity yaw with the claw chain of animation object `set`
// (0 = the walk set, 1 = the run set) and turns the resulting translation into
// this frame's move_speed_current.  With `apply` set it also subtracts the
// translation straight off the entity position, which is what makes the
// lunge/thrust behaviours slide the whole body along the animation.
// ---------------------------------------------------------------------------
void tyrant_root_motion(unsigned char set, char apply)
{
    JointStruct* joints = ENTITY->jointsStructs;
    if (joints == nullptr) return;

    RotMatrix(reinterpret_cast<SVECTOR*>(&ENTITY->position.pad),
              &ENTITY->scaMatrixData.localMatrix);
    ApplyLVAndMul0Matrix(&ENTITY->scaMatrixData.localMatrix,
                         (char*)joints + 0x24, &g_matrixScratch);

    if (euw(ENTITY, 0xca) != 0) {
        g_playerPosScratch.x = (int)euw(ENTITY, 0xca);
        g_playerPosScratch.y = g_playerPosScratch.x;
        g_playerPosScratch.z = g_playerPosScratch.x;
        ScaleMatrixCols(&g_matrixScratch, &g_playerPosScratch);
    }

    char* base = (char*)joints + (unsigned int)set * 0x174 + 0x554;
    for (unsigned char n = 3; n != 0; n--)
        ApplyLVAndMulMatrix(&g_matrixScratch,
                            reinterpret_cast<MATRIX*>(base - (unsigned int)n * 0x7c + 0xa0));

    g_matrixScratch.t[0] -= *(int*)(base + 0x58);
    g_matrixScratch.t[1]  = 0;
    g_matrixScratch.t[2] -= *(int*)(base + 0x60);

    FUN_0040a380(reinterpret_cast<VECTOR*>(g_matrixScratch.t), &g_playerPosScratch);
    ty_speed() = (short)SquareRoot0(g_playerPosScratch.z + g_playerPosScratch.x);

    if (apply != 0) {
        ei(ENTITY, 0x34) -= g_matrixScratch.t[0];
        ei(ENTITY, 0x3c) -= g_matrixScratch.t[2];
    }
}

// ---------------------------------------------------------------------------
// FUN_004216a0 (0x004216a0) - advance the claw ribbon one frame.
// The push itself is stubbed (see the file header); the countdown is not,
// because DAT_004ba264 gates the whole ribbon block in tyrant_update.
// ---------------------------------------------------------------------------
void tyrant_trail_update(void)
{
    JointStruct* joints = ENTITY->jointsStructs;
    if (joints == nullptr) return;

    RotMatrix(reinterpret_cast<SVECTOR*>(&ENTITY->position.pad),
              &ENTITY->scaMatrixData.localMatrix);
    ApplyLVAndMul0Matrix(&ENTITY->scaMatrixData.localMatrix,
                         (char*)joints + 0x24, &g_matrixScratch);
    ApplyLVAndMulMatrix(&g_matrixScratch, reinterpret_cast<MATRIX*>((char*)joints + 0x0a0));
    ApplyLVAndMulMatrix(&g_matrixScratch, reinterpret_cast<MATRIX*>((char*)joints + 0x30c));
    ApplyLVAndMulMatrix(&g_matrixScratch, reinterpret_cast<MATRIX*>((char*)joints + 0x388));
    ApplyLVAndMulMatrix(&g_matrixScratch, reinterpret_cast<MATRIX*>((char*)joints + 0x404));

    tyrant_trail_push(g_tyTrailBlock, ty_clawWorld(), &g_matrixScratch,
                      &g_tyTrailNear, &g_tyTrailFar, 0,
                      (unsigned char)g_tyTrailSegments);

    if ((g_message_flags & 4) != 0) {
        if ((int)(unsigned int)g_tyTrailTimer < g_tyTrailSegments)
            g_tyTrailSegments--;
        g_tyTrailTimer = (unsigned short)(g_tyTrailTimer - 1);
    }
}

// ---------------------------------------------------------------------------
// FUN_00421790 (0x00421790) - the two claw AFTERIMAGES.
//
// Copy 1 takes the claw matrix unscaled; copy 0 takes it scaled by the
// 0x004ba254 / 0x004ba258 pair, which oscillate between 3000 and 6000 by
// +-0x004ba25c every frame.  Behaviour 0x0D and behaviour 0x0C push 0x004ba258
// up and down on their own - that is the claw visibly swelling as the Tyrant
// powers up.
// ---------------------------------------------------------------------------
void tyrant_draw_claw_ghosts(void)
{
    if (ENTITY->id == 12) return;

    JointStruct* joints = ENTITY->jointsStructs;
    if (joints == nullptr) return;
    if (!tyrant_pool_pointer(g_tyClawGhostBlock, 0xf8)) return;

    char* block = (char*)g_tyClawGhostBlock;

    g_entityJointPosX = *(int*)((char*)joints + 0x3f4);

    RotMatrix(reinterpret_cast<SVECTOR*>(&ENTITY->position.pad),
              &ENTITY->scaMatrixData.localMatrix);
    ApplyLVAndMul0Matrix(&ENTITY->scaMatrixData.localMatrix,
                         (char*)joints + 0x24, &g_matrixScratch);
    ApplyLVAndMulMatrix(&g_matrixScratch, reinterpret_cast<MATRIX*>((char*)joints + 0x0a0));
    ApplyLVAndMulMatrix(&g_matrixScratch, reinterpret_cast<MATRIX*>((char*)joints + 0x30c));
    ApplyLVAndMulMatrix(&g_matrixScratch, reinterpret_cast<MATRIX*>((char*)joints + 0x388));
    ApplyLVAndMulMatrix(&g_matrixScratch, reinterpret_cast<MATRIX*>((char*)joints + 0x404));

    if ((g_message_flags & 4) != 0) {
        std::memcpy(block + 0xc0, &g_matrixScratch, 32);      // copy 1: unscaled

        g_tyClawScaleA += g_tyClawScaleStep;
        g_playerPosScratch.x = g_tyClawScaleA;
        g_tyClawScaleB += g_tyClawScaleStep;
        g_playerPosScratch.y = g_tyClawScaleB;
        g_playerPosScratch.z = g_playerPosScratch.x;
        ScaleMatrixCols(&g_matrixScratch, &g_playerPosScratch);
        if (g_tyClawScaleA > 6000 || g_tyClawScaleA < 3000)
            g_tyClawScaleStep = -g_tyClawScaleStep;

        std::memcpy(block + 0x44, &g_matrixScratch, 32);      // copy 0: scaled
    }

    if (ENTITY->has_enter_switch_zone == 0) return;
    if (g_RoomCameraDataCopy == 0) return;

    void* spriteSlot = (void*)((char*)&g_spriteAnimSlots[2] +
                               (unsigned int)g_spriteAnimActive * 0x14);

    for (int i = 1; i >= 0; i--) {
        MATRIX view;
        ApplyLVAndMul0Matrix(reinterpret_cast<void*>(static_cast<uintptr_t>(g_RoomCameraDataCopy)),
                             block + i * 0x7c + 0x44, &view);
        SetRotAndTransMatrix(&view);
        FUN_00483250(0, 0, 0, *(int*)(block + 0x18 + i * 0x7c), 0, 4, spriteSlot);
    }
    g_playerDisplacement = -1;   // the original's loop counter falls out at -1
}

// ---------------------------------------------------------------------------
// FUN_00425840 (0x00425840) - the exposed heart.
//
// The heart is a full Entity clone parked at ENTITY+0x170, riding joint 1's
// world matrix at a fixed offset.  Its own 0xC4 counter walks the 22-entry
// wobble table to drive a pulsing scale in its position.x field.
//
// The original SWAPS the global ENTITY for the duration; so does this, because
// Joint-less helpers below it read ENTITY.
// ---------------------------------------------------------------------------
void tyrant_draw_heart(void)
{
    if ((ENTITY->has_enter_switch_zone & 0x7f) == 0) return;

    Entity* owner = ENTITY;
    unsigned short ownerHealth = euw(owner, 0x88);
    JointStruct* joints = owner->jointsStructs;
    if (joints == nullptr) return;
    if (g_RoomCameraDataCopy == 0) return;

    Entity* heart = reinterpret_cast<Entity*>(static_cast<uintptr_t>(eu(owner, 0x170)));
    if (!tyrant_pool_pointer(heart, sizeof(Entity))) return;
    if (!tyrant_pool_pointer((const void*)(uintptr_t)heart->unk_18, 0xb4)) return;

    update_entity_lighting(reinterpret_cast<VECTOR*>(&owner->scaMatrixData.localMatrix.t[0]));

    g_tempVar = owner;
    ENTITY = heart;

    if ((g_message_flags & 4) != 0) {
        RotMatrix(reinterpret_cast<SVECTOR*>(&heart->position.pad),
                  &heart->scaMatrixData.localMatrix);
        if ((ownerHealth & 0x8000) == 0) {
            g_playerPosScratch.x = (int)ew(heart, 0x6c) + 500;
            g_playerPosScratch.y = g_playerPosScratch.x;
            g_playerPosScratch.z = g_playerPosScratch.x;
            ScaleMatrixCols(&heart->scaMatrixData.localMatrix, &g_playerPosScratch);

            short beat = ew(heart, 0xc4);
            if (beat >= 0 && beat < 22)
                ew(heart, 0x6c) = (short)(ew(heart, 0x6c) + (short)s_tyrantHeartBeat[beat] * 2);
            short prev = ew(heart, 0xc4);
            ew(heart, 0xc4) = (short)(prev - 1);
            if (prev == 0) ew(heart, 0xc4) = 0x15;
        }
    }

    MATRIX* anchor = reinterpret_cast<MATRIX*>((char*)joints + 0xc0);   // joint 1 world

    MATRIX local;
    ApplyLVAndMul0Matrix(anchor, &heart->scaMatrixData.localMatrix, &local);
    ew(heart, 0x6e) = (short)local.t[1];

    ApplyLVAndMul0Matrix(reinterpret_cast<void*>(static_cast<uintptr_t>(g_RoomCameraDataCopy)),
                         &local, &g_matrixScratch);

    MATRIX lightMatrix = local;
    if (g_lightMatrixPtr != 0)
        MulMatrix0(reinterpret_cast<MATRIX*>(static_cast<uintptr_t>(g_lightMatrixPtr)),
                   anchor, &lightMatrix);

    g_entityJointPosX = (int)heart->modelLoadBuffer;
    SetLightMatrix(&lightMatrix);
    SetRotAndTransMatrix(&g_matrixScratch);
    FUN_00483250(0, 0, 0, (int)heart->unk_18, 0, 4,
                 (void*)((char*)&g_spriteAnimSlots[2] +
                         (unsigned int)g_spriteAnimActive * 0x14));

    ENTITY = reinterpret_cast<Entity*>(g_tempVar);
}

// ---------------------------------------------------------------------------
// 0x0048a630 - clone the current entity `count` times into the room data buffer
// and give each clone its own animation object.  Same helper Plant 42 uses;
// that copy is file-static there, so the Tyrant carries its own.
// ---------------------------------------------------------------------------
void tyrant_clone_entity(unsigned char count, int /*animSlotBytes*/,
                         unsigned char jointIndex, unsigned int* out)
{
    g_playerDisplacement = (int)(*(unsigned int*)((char*)ENTITY->jointsStructs + 0x14) +
                                 (unsigned int)jointIndex * 0x1c);

    *out = (unsigned int)(uintptr_t)g_loadDataDestPointer;
    g_loadDataDestPointer = (char*)g_loadDataDestPointer + (unsigned int)count * 0x18c;

    Entity* copy = reinterpret_cast<Entity*>(static_cast<uintptr_t>(*out));
    MATRIX savedLocal = ENTITY->scaMatrixData.localMatrix;

    unsigned char remaining = count;
    do {
        std::memcpy(copy, ENTITY, 0x18c);
        copy->scaMatrixData.localMatrix = savedLocal;
        ew(copy, 0x74) = (short)(ew(copy, 0x74) + (short)((unsigned short)remaining * 0x100));
        copy->modelLoadBuffer = (unsigned int)g_playerDisplacement;
        copy->unk_18 = (unsigned int)(uintptr_t)g_loadDataDestPointer;
        SetAnimSlot(reinterpret_cast<AnimSlot*>(static_cast<uintptr_t>(copy->modelLoadBuffer)),
                    (int)&copy->unk_0c, 0);
        g_loadDataDestPointer = CreateAnimObject((int)&copy->unk_0c,
            reinterpret_cast<unsigned int*>(static_cast<uintptr_t>(copy->unk_18)));
        copy->state = 0;
        copy->action_state = (unsigned char)((rand() & 3) == 0);
        copy = reinterpret_cast<Entity*>((char*)copy + 0x18c);
        remaining--;
    } while (remaining != 0);
}

// ===========================================================================
// The 16 behaviours (jumptable 0x004ba308)
// ===========================================================================

// --- 0x00424c40 - behaviour 0: id 12 strapped to the slab -------------------
// Only ever reached from state 3, which id 12's init selects.  The room event
// flag it raises is what the lab SCD polls before starting the Wesker scene.
void tyrant_behavior_restrained(void)
{
    unsigned char sub = ty_sub();
    if (sub == 0) {
        ty_sub() = 1;
        ty_frame() = 0;
        eub(ENTITY, 0xbf) = 0;
        eub(ENTITY, 0x8c) = 0x0f;
        ty_anim() = 8;
        euw(ENTITY, 0x88) = 0xffff;          // health = -1: unkillable while bound
        Flg_on((int)g_RoomEventFlags, ENTITY->death_event_id);
    } else if (sub != 1) {
        if (sub == 2) {
            ENTITY->status_flags |= 10;
            Flg_on((int)g_RoomEventFlags, ENTITY->death_event_id);
        }
        tyrant_root_motion(0, 0);
        Add_speedXZ(0);
        return;
    }

    ty_sub() = (unsigned char)(ty_sub() +
        (char)Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x100));

    if (ty_frame() == 0x19 && (eub(ENTITY, 0xbf) & 1) != 0) Snd_em(6);
    if (ty_frame() == 0x32 && (eub(ENTITY, 0xbf) & 1) != 0) Snd_em(6);

    tyrant_root_motion(0, 0);
    Add_speedXZ(0);
}

// --- 0x00424d70 - behaviour 1: hand control to the SCD ----------------------
void tyrant_behavior_yield_to_scd(void)
{
    Flg_on((int)g_RoomEventFlags, ENTITY->death_event_id);
    ENTITY->behavior_flags |= 0x40;
    ty_frame() = 0;
    eub(ENTITY, 0xbf) = 0;
    ty_anim() = 0;
    eub(ENTITY, 0x8c) = 0;
    set_state_word(0x01000008);   // state 8, SCD behaviour 0, sub 1
}

// --- 0x00422280 - behaviour 2: hold still for 60 frames ---------------------
void tyrant_behavior_pause(void)
{
    if (ty_sub() == 0) {
        ty_sub() = 1;
        ty_frame() = 0;
        eub(ENTITY, 0xbf) = 0;
        ty_anim() = 0;
        ty_ticks() = 0x3c;
        eub(ENTITY, 0x8c) = 0x1f;
    } else if (ty_sub() != 1) {
        return;
    }

    Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x80);

    short t = ty_ticks();
    ty_ticks() = (short)(t - 1);
    if (t == 0) {
        ENTITY->ignore_player_flag = 0;
        ENTITY->action_behavior = 1;
        ty_sub() = 0;
    }
}

// --- 0x00422340 - behaviour 3: walk the waypoint ----------------------------
void tyrant_behavior_walk(void)
{
    bool step = true;
    if (ty_sub() == 0) {
        ty_sub() = 1;
        ty_frame() = 0;
        eub(ENTITY, 0xbf) = 0;
        eub(ENTITY, 0x8c) = 7;
        ty_anim() = 1;
    } else if (ty_sub() != 1) {
        step = false;                    // LAB_0042244d: tail only
    }

    if (step) {
        g_playerPosScratch.x = (int)ew(ENTITY, 0x166);
        g_playerPosScratch.y = 0;
        g_playerPosScratch.z = (int)ew(ENTITY, 0x168);

        entity_update_wander_turn((unsigned int)(unsigned short)ew(ENTITY, 0x17c),
                                  &eub(ENTITY, 0x16c), &eub(ENTITY, 0x17e), 0x28, 0x28);
        if (eub(ENTITY, 0x183) != 0) {
            eub(ENTITY, 0x183)--;
            entity_update_wander_turn((unsigned int)(unsigned short)ew(ENTITY, 0x17c),
                                      &eub(ENTITY, 0x16c), &eub(ENTITY, 0x17e), 0x28, 0x28);
        }
        Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x200);
    }

    // Frames 6..0x1E use the walk animation set, everything else the run set;
    // the footstep sound fires on the frame each set starts.
    unsigned char frame = ty_frame();
    if (frame > 5 && frame < 0x1f) {
        if (frame == 6) Snd_em(0);
        tyrant_root_motion(0, 0);
    } else {
        if (frame == 0x1f) Snd_em(0);
        tyrant_root_motion(1, 0);
    }
    ty_speed() = (short)(ty_speed() + 0x19);
    Add_speedXZ(0);
}

// --- 0x004224d0 - behaviour 4: RET ------------------------------------------
void tyrant_behavior_null(void)
{
}

// --- 0x004224e0 - behaviour 5: wide claw swipe ------------------------------
void tyrant_behavior_claw_swipe(void)
{
    unsigned char sub = ty_sub();
    if (sub == 0) {
        ty_flags() &= 0xfb;
        ty_sub() = 1;
        ty_frame() = 0;
        eub(ENTITY, 0xbf) = 0;
        eub(ENTITY, 0x8c) = 7;
        ty_anim() = 5;
        ty_speed() = 200;
        Snd_em(1);
        eub(ENTITY, 0x17f) = 10;
        eub(ENTITY, 0x182) = (unsigned char)(eub(ENTITY, 0x182) + 0x14);
        g_tyTrailTimer = 0x801f;
    } else if (sub != 1) {
        if (sub == 2) {
            set_state_word(0x00010001);        // behaviour 1 -> table[3], walk
            ty_hitMask() &= 0x74;
        }
        tyrant_decay_speed(-5);
        Add_speedXZ(0);
        return;
    }

    ty_sub() = (unsigned char)(ty_sub() +
        (char)Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x200));

    if (ty_speed() != 0) {
        entity_rotate_toward_target((VECTOR*)ty_playerT(), 0x10);
        if (eub(ENTITY, 0x183) != 0)
            entity_rotate_toward_target((VECTOR*)ty_playerT(), 0x10);
    }

    unsigned char frame = ty_frame();
    if ((unsigned short)(frame - 5) < 4) {
        tyrant_load_fx_anchor(500);
        if (FUN_0048ae00(ty_clawWorld(), &g_playerPosScratch, 800, ty_playerT()) != 0 &&
            ((unsigned short)g_playerEntity.health & 0x8000) == 0) {
            tyrant_flash_claw_and_stagger(1);
            tyrant_load_fx_anchor(800);
            Effect_CreateBillboard(0, 3, 0, ty_clawWorld(), &g_playerPosScratch, 0);
            g_playerEntity.animationId = 6;
            g_playerEntity.animFrameId = 0xc;
            g_playerEntity.action_behavior = 1;
            g_playerEntity.action_state = 0;
            tyrant_latch_player_attacker(0);
            ty_hitMask() |= 1;
            if ((ty_flags() & 4) == 0) {
                Snd_em(2);
                ty_flags() |= 4;
            }
        }
    }
    if ((unsigned short)(frame - 5) == 3 && (ty_hitMask() & 1) != 0)
        tyrant_damage_player(10, 0x12);

    if (ty_frame() == 0x32) ty_sub() = 2;

    tyrant_decay_speed(-5);
    Add_speedXZ(0);
}

// --- 0x004227e0 - behaviour 6: big overhead slash ---------------------------
void tyrant_behavior_claw_slash(void)
{
    unsigned char sub = ty_sub();
    if (sub == 0) {
        ty_flags() &= 0xfb;
        ty_sub() = 1;
        ty_frame() = 0;
        eub(ENTITY, 0xbf) = 0;
        eub(ENTITY, 0x8c) = 7;
        ty_anim() = 4;
        eub(ENTITY, 0x17f) = 0x0f;
    } else if (sub != 1) {
        if (sub != 2) return;
        set_state_word(0x00010001);            // state 1, behaviour 1, sub 0
        tyrant_player_distance();
        if (ENTITY->id == 0x10 && g_playerDisplacement > 5000 &&
            (ty_hitMask() & 2) != 0 && (rand() & 1) != 0) {
            set_state_word(0x000a0101);        // behaviour 10 -> table[12], rush
        }
        ty_hitMask() &= 0x75;
        return;
    }

    entity_rotate_toward_target((VECTOR*)ty_playerT(), 8);
    if (eub(ENTITY, 0x183) != 0)
        entity_rotate_toward_target((VECTOR*)ty_playerT(), 0x28);

    if (ty_frame() == 7) {
        Snd_em(1);
        g_tyTrailTimer = 0x8008;
    }

    unsigned char frame = ty_frame();
    if ((unsigned short)(frame - 7) <= 4) {
        tyrant_load_fx_anchor(0);
        if (FUN_0048ae00(ty_clawWorld(), &g_playerPosScratch, 0x5dc, ty_playerT()) != 0 &&
            ((unsigned short)g_playerEntity.health & 0x8000) == 0) {
            tyrant_flash_claw_and_stagger(1);

            // The 0xFC00 knock-back bias, and NO +1 on the dx == 0 path - both
            // verified against 0x00422b04 / 0x00422b21.
            if (Flg_ck((int)g_PlayerFlags, 0x7b) == 0) {
                if (ENTITY->id == 0x10 && g_playerEntity.health > 0x0c) {
                    tyrant_set_player_knockback((short)0xfc00, 0);
                    eub(ENTITY, 0x181) = 0xd2;
                }
            } else if (ENTITY->id == 0x10 && g_playerEntity.health > 0x12) {
                tyrant_set_player_knockback((short)0xfc00, 0);
                eub(ENTITY, 0x181) = 0xd2;
            }

            tyrant_latch_player_attacker(0x400);
            ty_hitMask() |= 2;
            ty_flags() |= 4;
        }
    }

    if ((unsigned short)(frame - 7) == 5 && (ty_hitMask() & 2) != 0) {
        tyrant_damage_player(0x0c, 0x12);
        unsigned char snd;
        if (((unsigned short)g_playerEntity.health & 0x8000) == 0) {
            snd = 3;
        } else {
            g_playerEntity.health = 1;
            ty_hitMask() |= 0x80;
            snd = 2;
        }
        Snd_em(snd);
    }

    ty_sub() = (unsigned char)(ty_sub() +
        (char)Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x200));

    // (hitMask & 0x82) == 0x82: the slash connected AND it would have killed -
    // roll straight into the impale (behaviour 6 sub 0 restarts as the grab).
    if ((ty_hitMask() & 0x82) == 0x82 && ty_frame() == 0x11) {
        ENTITY->action_behavior = 6;   // -> table[8], backhand
        ty_sub() = 0;
        entity_rotate_toward_target((VECTOR*)ty_playerT(), 0x20);
    }
}

// --- 0x00422c70 - behaviour 7: claw thrust ----------------------------------
void tyrant_behavior_claw_thrust(void)
{
    unsigned char sub = ty_sub();
    if (sub == 0) {
        ty_sub() = 1;
        ty_frame() = 0;
        eub(ENTITY, 0xbf) = 0;
        eub(ENTITY, 0x8c) = 7;
        ty_anim() = 7;
        eub(ENTITY, 0x17f) = 0x0f;
    } else if (sub != 1) {
        if (sub == 2) {
            set_state_word(0x00010001);
            tyrant_player_distance();
            if (ENTITY->id == 0x10 && g_playerDisplacement > 5000 &&
                (ty_hitMask() & 4) != 0 && (rand() & 1) != 0) {
                set_state_word(0x000a0101);
            }
            ty_hitMask() &= 0xfb;
        }
        if (ty_frame() < 0x0e) tyrant_root_motion(0, 1);
        else                   tyrant_root_motion(1, 1);
        return;
    }

    if (ty_frame() == 10) g_tyTrailTimer = 0x8008;
    if (ty_frame() == 0x0c) Snd_em(1);

    unsigned char frame = ty_frame();
    if ((unsigned short)(frame - 8) < 8) {
        tyrant_load_fx_anchor(0);
        if (FUN_0048ae00(ty_clawWorld(), &g_playerPosScratch, 0x5dc, ty_playerT()) != 0 &&
            ((unsigned short)g_playerEntity.health & 0x8000) == 0) {
            tyrant_flash_claw_and_stagger(1);

            int hp = (int)g_playerEntity.health;
            if (Flg_ck((int)g_PlayerFlags, 0x7b) == 0) {
                if (hp > 0x10) tyrant_set_player_knockback(0x400, 1);
            } else {
                if (hp > 0x14) tyrant_set_player_knockback(0x400, 1);
            }

            eub(ENTITY, 0x181) = 0xd2;
            tyrant_latch_player_attacker(0x400);
            ty_hitMask() |= 4;
        }
    }

    if ((unsigned short)(frame - 8) == 7 && (ty_hitMask() & 4) != 0) {
        tyrant_damage_player(0x10, 0x14);
        Snd_em(g_playerEntity.health < 0 ? 2 : 3);
    }

    if ((char)Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x200) != 0) {
        ty_sub() = 2;
        ew(ENTITY, 0x74) = (short)(ew(ENTITY, 0x74) + 0x800);
    }

    if (ty_frame() < 0x0e) tyrant_root_motion(0, 1);
    else                   tyrant_root_motion(1, 1);
}

// --- 0x00423080 - behaviour 8: backhand -------------------------------------
void tyrant_behavior_backhand(void)
{
    unsigned char sub = ty_sub();
    if (sub == 0) {
        ty_flags() &= 0xfb;
        ty_sub() = 1;
        ty_frame() = 0;
        eub(ENTITY, 0xbf) = 0;
        eub(ENTITY, 0x8c) = 7;
        ty_anim() = 3;
        ty_speed() = 0x12c;
        entity_rotate_toward_target((VECTOR*)ty_playerT(), 0x30);
        g_tyTrailSegments = 8;
        g_tyTrailTimer = 0x0f;
    } else if (sub != 1) {
        if (sub == 2) {
            set_state_word(0x00010001);
            ty_hitMask() &= 0x75;
        }
        Add_speedXZ(0);
        return;
    }

    if (ty_frame() == 4) Snd_em(1);

    unsigned char frame = ty_frame();
    if ((unsigned short)(frame - 4) < 4) {
        tyrant_load_fx_anchor(500);
        if (FUN_0048ae00(ty_clawWorld(), &g_playerPosScratch, 800, ty_playerT()) != 0 &&
            ((unsigned short)g_playerEntity.health & 0x8000) == 0) {
            tyrant_flash_claw_and_stagger(0);
            tyrant_latch_player_attacker((short)-0x400);
            ty_hitMask() |= 8;
            if ((ty_flags() & 4) == 0) {
                Snd_em(2);
                ty_flags() |= 4;
            }
        }
    }

    if ((unsigned short)(frame - 4) == 3 && (ty_hitMask() & 8) != 0) {
        tyrant_damage_player(0x0c, 0x12);
        if (((unsigned short)g_playerEntity.health & 0x8000) != 0 &&
            (ty_hitMask() & 0x82) == 0x82) {
            g_playerEntity.health = 1;
        }
    }

    ty_sub() = (unsigned char)(ty_sub() +
        (char)Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x200));

    tyrant_decay_speed(-3);

    // The finisher hand-off: backhand landed (8), it would have killed (0x80),
    // and the player is already reacting -> switch to the impale.
    if ((ty_hitMask() & 0x8a) == 0x8a && ty_frame() == 0x0c &&
        g_playerEntity.isBeingAttackedFlag != 0) {
        entity_rotate_toward_target((VECTOR*)ty_playerT(), 0x20);
        // 3 -> claw swipe; +4 when the player is facing away -> 7, the impale.
        ENTITY->action_behavior = 3;
        ty_sub() = 0;
        if (g_playerEntity.health < 0x10) {
            ENTITY->action_behavior = (unsigned char)(ENTITY->action_behavior +
                ((char)is_facing_toward_entity(&g_playerEntity) == 0 ? 4 : 0));
        }
    }

    Add_speedXZ(0);
}

// --- 0x00423390 - behaviour 9: grab and impale (kill move) ------------------
void tyrant_behavior_impale(void)
{
    unsigned char sub = ty_sub();
    if (sub == 0) {
        ty_sub() = 1;
        ty_frame() = 0;
        eub(ENTITY, 0xbf) = 0;
        eub(ENTITY, 0x8c) = 7;
        ty_anim() = 5;
        ty_speed() = 200;
        ENTITY->hit_state = 1;
        snap_player_to_grab_position(&g_playerEntity);
        ENTITY->status_flags |= 2;
        entity_rotate_toward_target((VECTOR*)ty_playerT(), 0x400);
        g_playerEntity.directionAngle = ew(ENTITY, 0x74);
        g_playerEntity.animationId = 7;
        g_playerEntity.animFrameId = 0xc;
        g_playerEntity.action_behavior = 0;
        g_playerEntity.action_state = 0;
        g_playerEntity.unk_b8 = (unsigned int)(uintptr_t)ENTITY;
        Snd_em(1);
        g_tyTrailTimer = 0x802f;
    } else if (sub != 1) {
        if (sub == 2) {
            ENTITY->ignore_player_flag = 0;
            ENTITY->action_behavior = 1;
            ty_sub() = 0;
            ty_hitMask() &= 0xf8;
            ENTITY->status_flags &= 0xfd;
        }
        goto tail;
    }

    if (ty_frame() == 5 || ty_frame() == 0x5c) {
        tyrant_load_fx_anchor(800);
        Effect_CreateBillboard(0, 3, 0, ty_clawWorld(), &g_playerPosScratch, 0);
    }
    if (ty_frame() == 5) Snd_em(4);

    if (ty_frame() < 0x61 && ty_frame() % 7 == 0) {
        tyrant_load_fx_anchor(800);
        Effect_CreateBillboard(0, 0, 0, ty_clawWorld(), &g_playerPosScratch, 0);
    }
    if (ty_frame() == 0x5f) Snd_em(7);

    entity_apply_anim_vertex(ENTITY, ENTITY->animHeader, ENTITY->animBase);
    ty_sub() = (unsigned char)(ty_sub() +
        (char)Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x200));
    if (ty_speed() != 0)
        entity_rotate_toward_target((VECTOR*)ty_playerT(), 0x10);

tail:
    tyrant_decay_speed(-5);
    Add_speedXZ(0);
    // Drag the grabbed player along with the Tyrant's own root motion.
    ew(ENTITY, 0xc6) = (short)(ew(ENTITY, 0xc6) + ENTITY->speed.x);
    ew(ENTITY, 0xc8) = (short)(ew(ENTITY, 0xc8) + ENTITY->speed.z);
    g_playerEntity.unk_c6 = (unsigned short)(g_playerEntity.unk_c6 + ENTITY->speed.x);
    g_playerEntity.unk_c8 = (unsigned short)(g_playerEntity.unk_c8 + ENTITY->speed.z);
}

// --- 0x00423680 - behaviour 10: charge, then heavy swing --------------------
void tyrant_behavior_charge(void)
{
    switch (ty_sub()) {
    case 0:
        ty_sub() = 1;
        ty_frame() = (unsigned char)(ty_frame() >> 1);
        eub(ENTITY, 0xbf) = 0;
        eub(ENTITY, 0x8c) = 7;
        ty_anim() = 2;
        ty_speed() = 0x190;
        if (Flg_ck((int)g_SysFlags, 0x1e) != 0) ty_speed() = 0x12c;
        eub(ENTITY, 0x17f) = 0x0f;
        // fallthrough
    case 1: {
        entity_rotate_toward_target((VECTOR*)ty_playerT(),
                                    Flg_ck((int)g_SysFlags, 0x1e) == 0 ? 0x20 : 0x30);
        Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x200);

        tyrant_player_distance();
        if ((g_playerDisplacement < 5000 &&
             (short)turn_toward_target((VECTOR*)ty_playerT(), 0x80) == 0) ||
            (short)turn_toward_target((VECTOR*)ty_playerT(), 900) != 0) {
            ty_sub() = 2;
        }

        // check_room_collision moves the entity; the original saves and
        // restores all three position words around it and keeps only the flag.
        int sx = ei(ENTITY, 0x34), sy = ei(ENTITY, 0x38), sz = ei(ENTITY, 0x3c);
        g_playerDisplacement = (int)check_room_collision(
            reinterpret_cast<VECTOR*>(&ei(ENTITY, 0x34)),
            *(short*)((char*)(uintptr_t)ENTITY->Sca_info + 10));
        ei(ENTITY, 0x34) = sx; ei(ENTITY, 0x38) = sy; ei(ENTITY, 0x3c) = sz;
        if (g_playerDisplacement != 0) ty_sub() = 2;

        if (ty_frame() == 3 || ty_frame() == 0x0f) Snd_em(0);
        break;
    }
    case 2:
        ty_sub() = 3;
        ty_frame() = 0;
        eub(ENTITY, 0xbf) = 0;
        eub(ENTITY, 0x8c) = 7;
        ty_anim() = 9;
        ty_speed() = 0x12c;
        // fallthrough
    case 3: {
        if (ty_frame() == 8) g_tyTrailTimer = 0x800f;

        unsigned short phase = (unsigned short)(ty_frame() - 0x0f);
        if (phase < 5) {
            tyrant_load_fx_anchor(500);
            if (FUN_0048ae00(ty_clawWorld(), &g_playerPosScratch, 0x708, ty_playerT()) != 0 &&
                ((unsigned short)g_playerEntity.health & 0x8000) == 0) {
                tyrant_flash_claw_and_stagger(0);
                tyrant_latch_player_attacker((short)-0x400);
                ty_hitMask() |= 8;
                eub(ENTITY, 0x181) = 0xd2;
            }
        }
        if (phase == 4 && (ty_hitMask() & 8) != 0) {
            tyrant_damage_player(0x14, 0x1e);
            Snd_em(2);
        }

        if (ty_frame() > 7 && ty_frame() < 0x0f) {
            tyrant_load_fx_anchor(1000);
            Effect_CreateBillboard(0x11, 4, 0, ty_clawWorld(), &g_playerPosScratch, 0);
            g_playerPosScratch.y = 900;
            Effect_CreateBillboard(0x11, 4, 0, ty_clawWorld(), &g_playerPosScratch, 0);
        }
        if (ty_frame() == 7)    Snd_em(5);
        if (ty_frame() == 0x0d) Snd_em(1);

        ty_sub() = (unsigned char)(ty_sub() +
            (char)Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x200));

        euw(ENTITY, 0xc2) = (unsigned short)(ty_speed() - (unsigned short)ty_frame());
        if (ty_speed() < 0) ty_speed() = 0;
        break;
    }
    case 4:
        set_state_word(0x00010001);
        ty_hitMask() &= 0xf7;
        break;
    default:
        break;
    }
    Add_speedXZ(0);
}

// --- 0x00423b60 - behaviour 11: the eruption entrance -----------------------
// id 16's opening: 60 frames of rubble and smoke while the Tyrant tears up
// through the platform, then it walks itself forward out of the hole and turns
// its ground shadow on (ty_flags bit 0) at frame 0x2B.  This is what init
// selects for id 16 (state word 0x00090101 -> state 1 behaviour 9 -> table[11]),
// NOT a death handler - the rocket-launcher kill is the health pin at the very
// bottom of tyrant_update.
void tyrant_behavior_erupt(void)
{
    switch (ty_sub()) {
    case 0:
        ei(ENTITY, 0x38) = 0x12c;          // lift to y = 300 for the blast
        ty_sub() = 1;
        ty_frame() = 0;
        eub(ENTITY, 0xbf) = 0;
        eub(ENTITY, 0x8c) = 7;
        ty_anim() = 6;
        Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x200);
        ty_ticks() = 0x3c;
        play_sound_and_voice_effect(1, 0x2d);
        g_main_state_flags |= 0x20000;
        Play3DSnd(2, 0x1c, 0, (int)&ei(ENTITY, 0x34));
        srand(0xb23);
        // fallthrough
    case 1: {
        ty_ticks() = (short)(ty_ticks() - 1);
        short t = ty_ticks();
        if (t == 0) {
            ty_sub() = (unsigned char)(ty_sub() + 1);
            return;
        }
        if (t > 0x19 && t < 0x21) {
            // Eleven debris billboards, each with its own random offset.
            static const struct { unsigned char type, depth, life; } kBurst[11] = {
                { 0x18, 3, 0x0f }, { 0x18, 5, 0x14 }, { 0x18, 5, 0x0f },
                { 0x19, 6, 0x19 }, { 0x19, 6, 0x0a }, { 0x19, 6, 0x0f },
                { 0x19, 6, 0x0a }, { 0x18, 3, 0x14 }, { 0x19, 6, 0x0a },
                { 0x19, 6, 0x05 }, { 0x18, 3, 0x05 }
            };
            for (const auto& b : kBurst) {
                g_playerPosScratch.x = (rand() & 0x7ff) - 0x1194;
                g_playerPosScratch.y = -(rand() & 0x1ff);
                g_playerPosScratch.z = -(rand() & 0x7ff);
                g_animFrameIdSave = (unsigned int)rand() & 0xfff;
                Effect_CreateBillboard(b.type, b.depth, (short)g_animFrameIdSave,
                                       &ENTITY->scaMatrixData.localMatrix,
                                       &g_playerPosScratch, (char)b.life);
            }
        }
        short u = ty_ticks();
        if (u > 0x25 && u < 0x37) {
            for (int i = 0; i < 3; i++) {
                g_playerPosScratch.x = (rand() & 0xfff) - 3000;
                g_playerPosScratch.y = -(rand() & 0x1ff);
                // The first of the three uses a POSITIVE z; the other two negative.
                g_playerPosScratch.z = (i == 0) ? (rand() & 0x7ff) : -(rand() & 0x7ff);
                Effect_CreateBillboard(9, 5, 0, &ENTITY->scaMatrixData.localMatrix,
                                       &g_playerPosScratch, 0x1e);
            }
        }
        break;
    }
    case 2:
        Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x200);
        if (ty_frame() == 0x21) {
            ty_sub() = (unsigned char)(ty_sub() + 1);
            ty_ticks() = 1;
        }
        ei(ENTITY, 0x3c) -= 8;
        ei(ENTITY, 0x34) -= 8;
        return;
    case 3:
        ty_ticks() = (short)(ty_ticks() - 1);
        if (ty_ticks() < 1) {
            ty_sub() = (unsigned char)(ty_sub() +
                (char)Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x200));
            if (ty_frame() < 0x23) {
                ei(ENTITY, 0x3c) -= 0x14;
                ei(ENTITY, 0x34) -= 0x14;
            }
            if (ty_frame() == 0x34) Play3DSnd(2, 0x1d, 0, (int)&ei(ENTITY, 0x34));
            if (ty_frame() == 0x40) Play3DSnd(2, 0x1e, 0, (int)&ei(ENTITY, 0x34));

            ei(ENTITY, 0x38) -= 10;
            if (ei(ENTITY, 0x38) < 0) ei(ENTITY, 0x38) = 0;

            unsigned char frame = ty_frame();
            if (frame > 0x23 && frame < 0x5a && (frame & 3) == 0) {
                // Two puffs riding a random joint.  The rand() ORDER differs
                // between them in the original - the first draws the joint
                // then the y jitter, the second draws the y jitter then the
                // joint - and the sequence is deterministic because sub 0
                // seeded it with srand(0xB23).  Kept as-is.
                JointStruct* joints = ENTITY->jointsStructs;
                int r = rand();
                int s = r >> 31;
                g_animFrameIdSave = (unsigned int)((((r ^ s) - s) & 0xf ^ s) - s);
                g_playerPosScratch.x = 0;
                g_playerPosScratch.y = -(rand() & 0x1ff);
                g_playerPosScratch.z = 0;
                Effect_CreateBillboard(9, 1, 0,
                    (char*)joints + (int)g_animFrameIdSave * 0x7c + 0x44,
                    &g_playerPosScratch, 0x14);

                g_playerPosScratch.x = 0;
                g_playerPosScratch.z = 0;
                g_playerPosScratch.y = -(rand() & 0x1ff);
                r = rand();
                s = r >> 31;
                g_animFrameIdSave = (unsigned int)((((r ^ s) - s) & 0xf ^ s) - s);
                Effect_CreateBillboard(9, 1, 0,
                    (char*)joints + (int)g_animFrameIdSave * 0x7c + 0x44,
                    &g_playerPosScratch, 0x14);
            }
            if (ty_frame() == 0x2b) {
                ty_flags() |= 1;             // ground shadow back on
                return;
            }
        }
        break;
    case 4:
        ENTITY->ignore_player_flag = 0;
        ENTITY->action_behavior = 1;
        ty_sub() = 0;
        ty_flags() |= 2;                     // look-at live: room hands over
        return;
    default:
        break;
    }
}

// --- 0x00424460 - behaviour 12: run in, then the heaviest swing -------------
// Reached only through the jumptable (nothing writes action_behavior 12
// directly); the SCD sets it for the rooftop Tyrant's enraged phase.
void tyrant_behavior_rush(void)
{
    switch (ty_sub()) {
    case 0:
        ty_sub() = 1;
        ty_frame() = (unsigned char)(ty_frame() >> 1);
        eub(ENTITY, 0xbf) = 0;
        eub(ENTITY, 0x8c) = 7;
        ty_anim() = 2;
        ty_speed() = 0x190;
        if (Flg_ck((int)g_SysFlags, 0x1e) != 0) ty_speed() = 0x12c;
        ty_ticks() = 0x3c;
        eub(ENTITY, 0x17f) = 0x0d;
        // fallthrough
    case 1: {
        entity_rotate_toward_target((VECTOR*)ty_playerT(), 0x80);
        Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x200);

        tyrant_player_distance();
        if (g_playerDisplacement < 6000 &&
            (short)turn_toward_target((VECTOR*)ty_playerT(), 0x200) == 0) {
            set_state_word(0x00010001);
            if (eub(ENTITY, 0x181) == 0) set_state_word(0x020a0101);
            eub(ENTITY, 0x181) = 0xd2;
        }

        int sx = ei(ENTITY, 0x34), sy = ei(ENTITY, 0x38), sz = ei(ENTITY, 0x3c);
        g_playerDisplacement = (int)check_room_collision(
            reinterpret_cast<VECTOR*>(&ei(ENTITY, 0x34)),
            *(short*)((char*)(uintptr_t)ENTITY->Sca_info + 10));
        ei(ENTITY, 0x34) = sx; ei(ENTITY, 0x38) = sy; ei(ENTITY, 0x3c) = sz;

        if (ty_ticks() != 0) ty_ticks() = (short)(ty_ticks() - 1);
        if (g_playerDisplacement != 0 || ty_ticks() == 0) {
            set_state_word(0x00010001);
            if (eub(ENTITY, 0x181) == 0) set_state_word(0x020a0101);
            eub(ENTITY, 0x181) = 0xd2;
        }

        if (ty_frame() == 3 || ty_frame() == 0x0f) Snd_em(0);
        break;
    }
    case 2:
        ty_flags() &= 0xfb;
        ty_sub() = 3;
        ty_frame() = 0;
        eub(ENTITY, 0xbf) = 0;
        eub(ENTITY, 0x8c) = 3;
        ty_anim() = 0x0b;
        eub(ENTITY, 0x17f) = 0x0f;
        // fallthrough
    case 3: {
        entity_rotate_toward_target((VECTOR*)ty_playerT(), 8);
        if (ty_frame() == 6)  g_tyTrailTimer = 0x800a;
        if (ty_frame() == 10) Snd_em(1);
        if (ty_frame() < 3)   ty_speed() = (short)(ty_speed() + 0x1e);

        unsigned short phase = (unsigned short)(ty_frame() - 9);
        if (phase < 8) {
            tyrant_load_fx_anchor(0);
            if (FUN_0048ae00(ty_clawWorld(), &g_playerPosScratch, 0x4b0, ty_playerT()) != 0 &&
                ((unsigned short)g_playerEntity.health & 0x8000) == 0) {
                tyrant_flash_claw_and_stagger(1);

                int hp = (int)g_playerEntity.health;
                if (Flg_ck((int)g_PlayerFlags, 0x7b) == 0) {
                    if (hp > 0x0c) tyrant_set_player_knockback(0x400, 1);
                } else {
                    if (hp > 0x12) tyrant_set_player_knockback(0x400, 1);
                }

                tyrant_latch_player_attacker(0x400);
                ty_hitMask() |= 2;
                ty_flags() |= 4;
            }
        }

        if (g_tyClawScaleB > 5000 && phase > 9) g_tyClawScaleB -= 800;

        if (phase == 8 && (ty_hitMask() & 2) != 0) {
            tyrant_damage_player(0x14, 0x1e);
            unsigned char snd;
            if (((unsigned short)g_playerEntity.health & 0x8000) == 0) {
                snd = 3;
            } else {
                g_playerEntity.health = 1;
                ty_hitMask() |= 0x80;
                snd = 2;
            }
            Snd_em(snd);
        }

        ty_sub() = (unsigned char)(ty_sub() +
            (char)Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x400));

        if ((ty_hitMask() & 0x82) == 0x82 && ty_frame() == 0x11) {
            ENTITY->action_behavior = 6;
            ty_sub() = 0;
            entity_rotate_toward_target((VECTOR*)ty_playerT(), 0x20);
        }
        if ((ty_hitMask() & 2) == 0 && ty_frame() < 0x11)
            entity_rotate_toward_target((VECTOR*)ty_playerT(), 0x40);

        if (ty_speed() != 0 && ty_frame() > 8) {
            euw(ENTITY, 0xc2) = (unsigned short)(ty_speed() - (unsigned short)ty_frame());
            if (ty_speed() < 0) ty_speed() = 0;
        }
        break;
    }
    case 4:
        g_tyClawScaleB = g_tyClawScaleA;
        set_state_word(0x00010001);
        if (ENTITY->id == 0x10 && (ty_hitMask() & 2) != 0 && (rand() & 1) != 0) {
            set_state_word(0x000a0101);
            set_state_word(0x00050101);
        }
        ty_hitMask() &= 0x75;
        // fallthrough
    default:
        break;
    }
    Add_speedXZ(0);
}

// --- 0x00424b80 - behaviour 13: rise / power up -----------------------------
void tyrant_behavior_rise(void)
{
    if (ty_sub() == 0) {
        ty_sub() = 1;
        ty_frame() = 0;
        eub(ENTITY, 0xbf) = 0;
        eub(ENTITY, 0x8c) = 0x1f;
        ty_anim() = 0x0c;
    }

    unsigned char frame = ty_frame();
    if (frame > 0x28) {
        if (g_tyClawScaleB < 9000) g_tyClawScaleB += 1000;
        frame = ty_frame();
    }
    if (frame == 0x28) Snd_em(1);

    if ((char)Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x80) != 0)
        set_state_word(0x000a0101);          // behaviour 10 -> rush

    tyrant_root_motion(1, 1);
}

// --- 0x00421f60 - behaviour 14: id 12's decision ----------------------------
void tyrant_think_lab(void)
{
    if (eub(ENTITY, 0x17f) != 0) eub(ENTITY, 0x17f)--;
    eub(ENTITY, 0x181) = 0;
    if (eub(ENTITY, 0x17f) != 0) return;

    if (g_playerDisplacement < 0xa8c &&
        (short)turn_toward_target((VECTOR*)ty_playerT(), 0x340) == 0) {
        set_state_word(0x00040101);          // behaviour 4 -> claw slash
    }

    if (eub(ENTITY, 0x183) != 0 && g_playerDisplacement < 0xa8c &&
        (short)turn_toward_target((VECTOR*)ty_playerT(), 0x340) == 0) {
        set_state_word(0x00040101);
        ty_hitMask() |= 0x80;
        return;
    }

    if (g_playerDisplacement < 2000 &&
        (short)turn_toward_target((VECTOR*)ty_playerT(), 0x200) == 0) {
        set_state_word(0x00030101);          // behaviour 3 -> claw swipe (+4 = impale)
        if (g_playerEntity.health < 0x10) {
            ENTITY->action_behavior = (unsigned char)(ENTITY->action_behavior +
                ((char)is_facing_toward_entity(&g_playerEntity) == 0 ? 4 : 0));
        }
    }
}

// --- 0x00422080 - behaviour 15: id 16's decision ----------------------------
void tyrant_think_roof(void)
{
    if (eub(ENTITY, 0x17f) != 0) eub(ENTITY, 0x17f)--;
    if (g_playerEntity.equippedWeaponId == 10) eub(ENTITY, 0x180) |= 0x80;
    if (eub(ENTITY, 0x181) != 0) eub(ENTITY, 0x181)--;

    if (eub(ENTITY, 0x17f) == 0) {
        if (g_playerDisplacement > 12000 &&
            (short)turn_toward_target((VECTOR*)ty_playerT(), 0x40) == 0) {
            set_state_word(0x00080101);      // behaviour 8 -> charge
        }

        bool skipShort = false;
        if (eub(ENTITY, 0x183) != 0 && g_playerDisplacement < 0xa8c) {
            if ((short)turn_toward_target((VECTOR*)ty_playerT(), 0x340) == 0) {
                set_state_word(0x00040101);
                ty_hitMask() |= 0x80;
                return;
            }
        } else {
            skipShort = (eub(ENTITY, 0x183) != 0);
        }
        if (!skipShort) {
            if (g_playerDisplacement < 0xa8c &&
                (short)turn_toward_target((VECTOR*)ty_playerT(), 0x340) == 0) {
                set_state_word(0x00040101);
            }
        }

        if (g_playerDisplacement < 2000 &&
            (short)turn_toward_target((VECTOR*)ty_playerT(), 0x200) == 0) {
            set_state_word(0x00030101);
            if (g_playerEntity.health < 0x10) {
                ENTITY->action_behavior = (unsigned char)(ENTITY->action_behavior +
                    ((char)is_facing_toward_entity(&g_playerEntity) == 0 ? 4 : 0));
            }
        }

        if (g_playerDisplacement < 7000 &&
            (short)turn_toward_target((VECTOR*)ty_playerT(), 0x4c8) != 0) {
            set_state_word(0x00050101);      // behaviour 5 -> claw thrust
        }

        if (eub(ENTITY, 0x181) == 0) {
            set_state_word(0x000b0101);      // behaviour 11 -> rise / power up
        }
    }

    if (Flg_ck((int)g_SysFlags, 0x1e) != 0 && g_playerDisplacement < 0xa8c &&
        (short)turn_toward_target((VECTOR*)ty_playerT(), 0x200) == 0) {
        set_state_word(0x00070101);          // behaviour 7 -> impale
    }
}

// ---------------------------------------------------------------------------
// The behaviour jumptable itself (0x004ba308).
// ---------------------------------------------------------------------------
typedef void (*TyrantBehavior)(void);

const TyrantBehavior s_tyrantBehaviors[16] = {
    tyrant_behavior_restrained,     // [0]  0x00424c40
    tyrant_behavior_yield_to_scd,   // [1]  0x00424d70
    tyrant_behavior_pause,          // [2]  0x00422280
    tyrant_behavior_walk,           // [3]  0x00422340
    tyrant_behavior_null,           // [4]  0x004224d0
    tyrant_behavior_claw_swipe,     // [5]  0x004224e0
    tyrant_behavior_claw_slash,     // [6]  0x004227e0
    tyrant_behavior_claw_thrust,    // [7]  0x00422c70
    tyrant_behavior_backhand,       // [8]  0x00423080
    tyrant_behavior_impale,         // [9]  0x00423390
    tyrant_behavior_charge,         // [10] 0x00423680
    tyrant_behavior_erupt,          // [11] 0x00423b60
    tyrant_behavior_rush,           // [12] 0x00424460
    tyrant_behavior_rise,           // [13] 0x00424b80
    tyrant_think_lab,               // [14] 0x00421f60
    tyrant_think_roof               // [15] 0x00422080
};

// State 1 dispatches through 0x004ba310, which is &table[2] - see the file
// header.  State 3 dispatches through 0x004ba308 with no bias.
const int TYRANT_STATE1_BEHAVIOR_BIAS = 2;

void tyrant_run_behavior(int index)
{
    if (index >= 0 && index < 16) s_tyrantBehaviors[index]();
}

} // namespace

// ===========================================================================
// State 0 - 0x004212b0.  One-time setup.
// ===========================================================================
static void tyrant_init(void)
{
    ei(ENTITY, 0x38) = -210;                 // y
    ENTITY->scaMatrixData.field_00 = 0;
    ENTITY->hit_state = 0;
    ResetJointTransforms();

    g_svecScratch = { 0, 0, 0, 0 };
    g_animFrameIdSave = 0x808080;
    FUN_004565f0(&g_svecScratch, (SVECTOR*)&ENTITY->pushVelocity, 1000, 1000);

    ENTITY->health = 0xdc;                                   // 220
    if (ENTITY->id == 0x10) ENTITY->health = 600;

    ENTITY->Sca_info = (unsigned int)(uintptr_t)s_tyrantScaInfo;   // PTR_DAT_004ba24c

    // The exposed heart: one clone parked at ENTITY+0x170, riding joint 1.
    tyrant_clone_entity(1, 5000, ENTITY->jointCount, &eu(ENTITY, 0x170));
    Entity* heart = reinterpret_cast<Entity*>(static_cast<uintptr_t>(eu(ENTITY, 0x170)));
    ew(heart, 0x72) = 0;
    ew(heart, 0x74) = 0;
    ew(heart, 0x76) = 0;
    ei(heart, 0x34) = 0x138;                 // 312
    ei(heart, 0x38) = -760;
    ei(heart, 0x3c) = -287;
    ew(heart, 0x6c) = 0x1000;                // scale base
    ew(heart, 0xc4) = 0x15;                  // beat phase

    eub(ENTITY, 0x16d) = 0;
    eub(ENTITY, 0x16e) = 0;
    eub(ENTITY, 0x16f) = 3;
    eub(ENTITY, 0x178) = 3;
    eub(ENTITY, 0x179) = 0;
    eub(ENTITY, 0x17f) = 0;
    eub(ENTITY, 0x180) = 0;
    eub(ENTITY, 0x181) = 0xd2;
    eub(ENTITY, 0x16c) = 0;
    eub(ENTITY, 0x17e) = 0;
    eub(ENTITY, 0x182) = 0;
    eub(ENTITY, 0x183) = 0;

    // id 12 starts in state 8 behaviour 0x0B = 11, which is the STASIS POD
    // (0x0045e2d0), not the impale - see the table at s_emScdBehaviors.
    // id 16 starts in state 1 behaviour 9, the eruption entrance.
    set_state_word(0x000b0008);
    eub(ENTITY, 0xbe) = 0;
    eub(ENTITY, 0xbf) = 0;
    eub(ENTITY, 0x8c) = 0;
    eub(ENTITY, 0xbd) = 6;
    if (ENTITY->id == 0x10) set_state_word(0x00090101);   // behaviour 9 -> eruption

    Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x40);

    ENTITY->lookAtJointIdx  = 2;
    ENTITY->lookAtFlags     = 2;
    ENTITY->lookAtYawStep   = 0xa0;
    ENTITY->lookAtPitchStep = 0x60;

    if (ENTITY->id == 12) return;

    // --- id 16 only: the two claw afterimage copies + the ribbon pool ---
    JointStruct* joints = ENTITY->jointsStructs;
    g_tyClawGhostBlock = g_loadDataDestPointer;
    g_loadDataDestPointer = (char*)g_loadDataDestPointer + 0xf8;    // 2 * 0x7C
    char* block = (char*)g_tyClawGhostBlock;

    for (int i = 1; i >= 0; i--) {
        char* dst = block + i * 0x7c;
        *(unsigned char*)dst = *((unsigned char*)joints + 0x3e0);
        std::memcpy(dst + 0x44, (char*)joints + 0x424, 32);
        *(int*)(dst + 0x14) = *(int*)((char*)joints + 0x3f4);
        *(int*)(dst + 0x0c) = *(int*)((char*)joints + 0x3ec);
        *(int*)(dst + 0x1c) = *(int*)((char*)joints + 0x3fc);
        *(int*)(dst + 0x20) = *(int*)((char*)joints + 0x400);
    }
    for (int i = 1; i >= 0; i--) {
        char* dst = block + i * 0x7c;
        *(unsigned int**)(dst + 0x18) = (unsigned int*)g_loadDataDestPointer;
        g_loadDataDestPointer = CreateAnimObject((int)(dst + 0x0c),
                                                 (unsigned int*)g_loadDataDestPointer);
    }
    JointApplyColorTint(reinterpret_cast<JointStruct*>(block), 0xff, 0xff, (void*)0xff);
    JointApplyColorTint(reinterpret_cast<JointStruct*>(block + 0x7c),
                        0x30000, 0x300000, (void*)0x300000);

    g_tyTrailBlock = g_loadDataDestPointer;
    tyrant_trail_alloc(9, g_loadDataDestPointer, 0x70);
    eub(ENTITY, 0x17f) = 0x5a;
}

// ===========================================================================
// State 1 - 0x00421d10.  Think, then act.
//
// The action half (0x00421e60) pathfinds first and then dispatches the
// behaviour table with the +2 bias.  The think half (0x00421eb0) picks between
// tyrant_think_lab and tyrant_think_roof by entity id.
// ===========================================================================
static void tyrant_think(void)                  // 0x00421eb0
{
    // health >> 8 & 0x80: the player is already dead.
    if ((eub(&g_playerEntity, 0x89) & 0x80) != 0) {
        set_state_word(0x00000101);             // behaviour 0 -> table[2], pause
        return;
    }
    if (ENTITY->health < 0x3c) ty_hitMask() |= 0x80;

    tyrant_player_distance();
    if ((unsigned int)g_playerDisplacement < 4000) {
        eb(ENTITY, 0x182)++;
        if (eub(ENTITY, 0x182) > 0x78) {
            eub(ENTITY, 0x183) = 0x3c;
            ty_hitMask() |= 0x80;
        }
    } else {
        eub(ENTITY, 0x182) = 0;
    }

    // `(*(byte*)(ENTITY+1) & 0xfffffffc)` indexes 0x004ba334 by BYTES, so
    // id 12 -> 0x004ba340 (behaviour 14) and id 16 -> 0x004ba344 (behaviour 15).
    // Every other id would run off the end; only 12 and 16 reach this code.
    tyrant_run_behavior(ENTITY->id == 0x10 ? 15 : 14);
}

static void tyrant_act(void)                    // 0x00421e60
{
    g_animFrameIdSave = (unsigned int)entity_pathfind_update();
    FUN_0045f970(g_playerEntity.scaMatrixData.localMatrix.t[0],
                 g_playerEntity.scaMatrixData.localMatrix.t[2],
                 reinterpret_cast<int*>(&ew(ENTITY, 0x166)),
                 reinterpret_cast<int*>(&ew(ENTITY, 0x168)));
    tyrant_run_behavior((int)ENTITY->action_behavior + TYRANT_STATE1_BEHAVIOR_BIAS);
}

static void tyrant_state1(void)
{
    ENTITY->status_flags &= 0x1f;
    ENTITY->status_flags |= 0x40;
    entity_check_visual_range(4000);
    entity_check_alert_range(4000);

    if (ENTITY->ignore_player_flag == 0) {
        tyrant_think();
    } else if (ENTITY->ignore_player_flag != 1) {
        return;
    }
    tyrant_act();
}

// ===========================================================================
// State 2 - 0x00421d60.  Hit reaction: one blood spray, then straight back to
// whatever the state word was before the hit (the 0x174 backup).
// ===========================================================================
static void tyrant_state_hit(void)
{
    if (ENTITY->ignore_player_flag != 0) return;

    eu(ENTITY, 0x84) = eu(ENTITY, 0x174);
    if (eub(ENTITY, 0x16f) == 0) eub(ENTITY, 0x16f) = 8;
    if (eub(ENTITY, 0x181) != 0) eub(ENTITY, 0x181) = 0xd2;

    tyrant_load_fx_anchor(-0x834);
    short yaw = (short)getAngleTowardsTarget(g_playerEntity.scaMatrixData.localMatrix.t[0],
                                             g_playerEntity.scaMatrixData.localMatrix.t[2]);
    player_distance_z = (int)yaw;
    Effect_CreateBillboard(0, 0, yaw, &ENTITY->scaMatrixData.localMatrix,
                           &g_playerPosScratch, 0);
}

// ===========================================================================
// State 3 - 0x00421e10.  Act with NO pathfinding and NO behaviour bias.
// ===========================================================================
static void tyrant_state_forced(void)
{
    if (ENTITY->ignore_player_flag == 0) {
        set_state_word(0x00000103);          // state 3, ignore 1, behaviour 0
        if (ENTITY->id == 0x10) ENTITY->action_behavior = 1;
    }
    tyrant_run_behavior((int)ENTITY->action_behavior);
}

// ===========================================================================
// State 4 - 0x00421e50.  A bare RET in the original.
// ===========================================================================
static void tyrant_state_nop(void)
{
}

// ===========================================================================
// State 8 - 0x0045c610.  SCD-driven.
//
// The 24-slot table at 0x004c10c8 is reachable ONLY from here (verified: one
// instruction in the whole exe references it), so it is Tyrant-specific rather
// than shared enemy infrastructure - behaviour 0 even pokes the claw-ribbon
// timer at 0x004ba264.
//
// The lab scene runs: 11 (float in the pod, blocking on SysFlags 0x1F) -> 12
// (impale Wesker, raising SysFlags 0x1E) -> 0 (idle), with 2 / 14 / 15 / 16
// used for the scripted walks, turns and swings in between.
// ===========================================================================
namespace {

// 0x004c10c0 - set to &g_EnemiesList[0] on every entry.  The behaviours use it
// as "the entity being grabbed": in the lab that is enemy slot 0, Wesker.
Entity* g_emScdVictim = nullptr;

// 0x0045c650 - behaviour 0: hold the idle pose.
void em_scd_behavior_idle(void)
{
    if (ty_sub() == 0) {
        ty_sub() = 1;
        ty_frame() = 0;
        eub(ENTITY, 0xbf) = 0;
        ty_anim() = 0;
        eub(ENTITY, 0x8c) = 0x1f;
        g_tyTrailTimer = 0;
    }
    Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x80);
}

// ---------------------------------------------------------------------------
// 0x0045c6c0 - behaviour 2: walk to the SCD target point.
//
// The target is the 16-bit pair at 0xC6/0xC8 (the same words snap_player_to_-
// grab_position writes), read UNSIGNED - that is the original, sign extension
// would move the goal.  Arriving within 0x5DC advances to sub 2, which raises
// the entity's scd_anim_param bit in g_SysFlags: THE flag the event VM blocks
// on.  A stub here freezes the cutscene.
// ---------------------------------------------------------------------------
void em_scd_behavior_walk_to(void)
{
    bool step = true;
    char sub = (char)ty_sub();

    if (sub == 0) {
        ty_sub() = 1;
        ty_frame() = 0;
        eub(ENTITY, 0xbf) = 0;
        eub(ENTITY, 0x8c) = 0x1f;
        ty_anim() = 1;
        ty_ticks() = 2;
        if ((ENTITY->behavior_flags & 1) != 0) ty_ticks() = 1;
    } else if (sub != 1) {
        if (sub == 2) {
            Flg_on((int)g_SysFlags, ENTITY->scd_anim_param);
            if ((ENTITY->collisionFlags & 0x80) == 0) {
                // ONE 16-bit store: it clears action_behavior AND action_state.
                euw(ENTITY, 0x86) = 0;
            } else {
                ty_sub() = 1;                // 0xDC bit 7 = loop the walk
            }
        }
        step = false;
    }

    if (step) {
        g_playerPosScratch.x = (int)euw(ENTITY, 0xc6);
        g_playerPosScratch.y = 0;
        g_playerPosScratch.z = (int)euw(ENTITY, 0xc8);
        entity_rotate_toward_target(&g_playerPosScratch, 0x28);

        ty_ticks() = (short)(ty_ticks() - 1);
        if (ty_ticks() == 0) {
            Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x80);
            ty_ticks() = 2;
            if ((ENTITY->behavior_flags & 1) != 0) ty_ticks() = 1;
        }

        int dz = ei(ENTITY, 0x3c) - (int)euw(ENTITY, 0xc8);
        int dx = ei(ENTITY, 0x34) - (int)euw(ENTITY, 0xc6);
        if (SquareRoot0(dz * dz + dx * dx) < 0x5dc) ty_sub()++;
    }

    if ((ty_frame() == 3 || ty_frame() == 0x24) && (eub(ENTITY, 0xc4) & 1) != 0)
        Snd_em(0);

    if (ty_frame() > 3 && ty_frame() < 0x23) tyrant_root_motion(0, 0);
    else                                     tyrant_root_motion(1, 0);
    ty_speed() = (short)(ty_speed() + 5);
    Add_speedXZ(0);
}

// ---------------------------------------------------------------------------
// 0x0045c900 - behaviour 10: pound the glass and break out of the capsule.
//
// Runs straight after the pod releases (behaviour 11 sub 3).  Everything is
// keyed off animation_frame_id:
//   0x1E  first impact - one shard burst and the glass-hit sound
//   0x55  the capsule shatters: Wesker's line, the screen-shake request
//         (g_main_state_flags 0x20000) and the two break sounds
//   0x59  53 glass shards across six position clusters
//   0x91  the ground shadow comes back on
//   0x8F / 0x97  the two footsteps as it steps out
// Sub 2 raises scd_anim_param, which is what lets the script continue.
//
// The capsule position (0x2A30, -0x1004, 5000) is hard-coded in the original -
// this behaviour only ever runs in room5130.
// ---------------------------------------------------------------------------
struct TyrantShard {
    short rotBase;      // added to (rand & 0x1FF)
    short xBase;        // added to (rand & xzMask)
    short zBase;
    unsigned short xzMask;
    unsigned char variant;
    unsigned char life;
};

// The 53 shards, in the original's exact order - the rand() stream is consumed
// rot, y, x, z per shard, so re-ordering them changes every shard's position.
const TyrantShard s_tyrantGlassShards[53] = {
    // cluster A: (rand & 0x1FF) + 0x2E18 / + 0x12C0, rotation biased -0x254.
    { (short)-0x254, 0x2e18, 0x14b4, 0x1ff, 1, 0x0a },
    { (short)-0x254, 0x2e18, 0x12c0, 0x1ff, 2, 0x14 },
    { (short)-0x254, 0x2e18, 0x12c0, 0x1ff, 3, 0x0a },
    { (short)-0x254, 0x2e18, 0x12c0, 0x1ff, 4, 0x1e },
    { (short)-0x254, 0x2e18, 0x12c0, 0x1ff, 5, 0x0a },
    { (short)-0x254, 0x2e18, 0x12c0, 0x1ff, 6, 0x14 },
    { (short)-0x254, 0x2e18, 0x12c0, 0x1ff, 7, 0x14 },
    { (short)-0x254, 0x2e18, 0x12c0, 0x1ff, 1, 0x0a },
    { (short)-0x254, 0x2e18, 0x12c0, 0x1ff, 5, 0x0a },
    { (short)-0x254, 0x2e18, 0x12c0, 0x1ff, 6, 0x1e },
    { (short)-0x254, 0x2e18, 0x12c0, 0x1ff, 7, 0x14 },
    { (short)-0x254, 0x2e18, 0x12c0, 0x1ff, 1, 0x0a },
    { (short)-0x254, 0x2e18, 0x12c0, 0x1ff, 5, 0x0a },
    { (short)-0x254, 0x2e18, 0x12c0, 0x1ff, 6, 0x1e },
    { (short)-0x254, 0x2e18, 0x12c0, 0x1ff, 7, 0x14 },
    { (short)-0x254, 0x2e18, 0x12c0, 0x1ff, 1, 0x0a },
    // cluster B
    {        0x0b00, 0x28a0,  5000,  0x1ff, 1, 0x0a },
    {        0x0b00, 0x28a0,  5000,  0x1ff, 2, 0x14 },
    {        0x0b00, 0x28a0,  5000,  0x1ff, 3, 0x0a },
    {        0x0b00, 0x28a0,  5000,  0x1ff, 4, 0x14 },
    {        0x0b00, 0x28a0,  5000,  0x1ff, 5, 0x0a },
    {        0x0b00, 0x28a0,  5000,  0x1ff, 6, 0x19 },
    {        0x0b00, 0x28a0,  5000,  0x1ff, 7, 0x14 },
    {        0x0b00, 0x28a0,  5000,  0x1ff, 1, 0x0a },
    {        0x0b00, 0x28a0,  5000,  0x1ff, 5, 0x0a },
    {        0x0b00, 0x28a0,  5000,  0x1ff, 6, 0x14 },
    {        0x0b00, 0x28a0,  5000,  0x1ff, 7, 0x0f },
    // cluster C
    { (short)-0x31c, 11000, 0x1324, 0x7ff, 7, 0x0f },
    { (short)-0x31c, 11000, 0x1324, 0x7ff, 1, 0x19 },
    { (short)-0x31c, 11000, 0x1324, 0x7ff, 5, 0x0f },
    { (short)-0x31c, 11000, 0x1324, 0x7ff, 6, 0x0a },
    { (short)-0x31c, 11000, 0x1324, 0x7ff, 7, 0x0a },
    { (short)-0x31c, 11000, 0x1324, 0x7ff, 1, 0x0a },
    { (short)-0x31c, 11000, 0x1324, 0x7ff, 6, 0x14 },
    { (short)-0x31c, 11000, 0x1324, 0x7ff, 7, 0x0a },
    { (short)-0x31c, 11000, 0x1324, 0x7ff, 1, 0x0a },
    // cluster D
    {        0x0cf4, 11000, 0x1324, 0x7ff, 1, 0x0f },
    {        0x0cf4, 11000, 0x1324, 0x7ff, 2, 0x19 },
    {        0x0cf4, 11000, 0x1324, 0x7ff, 3, 0x0f },
    {        0x0cf4, 11000, 0x1324, 0x7ff, 4, 0x0a },
    {        0x0cf4, 11000, 0x1324, 0x7ff, 5, 0x05 },
    {        0x0cf4, 11000, 0x1324, 0x7ff, 3, 0x0f },
    {        0x0cf4, 11000, 0x1324, 0x7ff, 4, 0x0f },
    {        0x0cf4, 11000, 0x1324, 0x7ff, 5, 0x0a },
    // cluster E
    { (short)-0x31c, 0x2a30, 0x1324, 0x7ff, 7, 0x19 },
    { (short)-0x31c, 0x2a30, 0x1324, 0x7ff, 1, 0x05 },
    { (short)-0x31c, 0x2a30, 0x1324, 0x7ff, 6, 0x0f },
    { (short)-0x31c, 0x2a30, 0x1324, 0x7ff, 7, 0x19 },
    { (short)-0x31c, 0x2a30, 0x1324, 0x7ff, 1, 0x05 },
    // cluster F
    {        0x0cf4, 0x2a30, 0x1324, 0x7ff, 1, 0x05 },
    {        0x0cf4, 0x2a30, 0x1324, 0x7ff, 2, 0x05 },
    {        0x0cf4, 0x2a30, 0x1324, 0x7ff, 3, 0x0f },
    {        0x0cf4, 0x2a30, 0x1324, 0x7ff, 4, 0x0f }
};

void em_scd_behavior_break_glass(void)
{
    char sub = (char)ty_sub();

    if (sub == 0) {
        ei(ENTITY, 0x38) = 0;
        ty_sub() = 1;
        ty_frame() = 0;
        eub(ENTITY, 0xbf) = 0;
        eub(ENTITY, 0x8c) = 0x1f;
        ty_anim() = 6;
    } else if (sub != 1) {
        if (sub != 2) return;
        Flg_on((int)g_SysFlags, ENTITY->scd_anim_param);
        euw(ENTITY, 0x86) = 0;
        return;
    }

    ty_sub() = (unsigned char)(ty_sub() +
        (char)Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x80));

    if (ty_frame() == 0x1e) {
        g_playerPosScratch.x = 0x2a30;
        g_playerPosScratch.y = -0x1004;
        g_playerPosScratch.z = 5000;
        Effect_CreateBillboard(1, 0, 0, nullptr, &g_playerPosScratch, 0);
        Play3DSnd(2, 0x19, 0, (int)&g_playerPosScratch);
    }

    if (ty_frame() == 0x55) {
        g_playerPosScratch.x = 0x2a30;
        g_playerPosScratch.y = -0x1004;
        g_playerPosScratch.z = 5000;
        play_sound_and_voice_effect(1, 0xb3);
        g_main_state_flags |= 0x20000;
        Play3DSnd(2, 0x1a, 0, (int)&g_playerPosScratch);
        Play3DSnd(2, 0x1b, 0, (int)&g_playerPosScratch);
    }

    if (ty_frame() == 0x59) {
        for (const auto& s : s_tyrantGlassShards) {
            g_animFrameIdSave = (unsigned int)((rand() & 0x1ff) + s.rotBase);
            g_playerPosScratch.y = -0xa28 - (rand() & 0xfff);
            g_playerPosScratch.x = (rand() & s.xzMask) + s.xBase;
            g_playerPosScratch.z = (rand() & s.xzMask) + s.zBase;
            Effect_CreateBillboard(0x15, s.variant, (short)g_animFrameIdSave,
                                   (void*)(uintptr_t)g_deadMoveValue,
                                   &g_playerPosScratch, (char)s.life);
        }
    }

    if (ty_frame() == 0x91) ty_flags() |= 1;                    // shadow back on
    if (ty_frame() == 0x97 || ty_frame() == 0x8f) Snd_em(0);    // the two steps out
}

// ---------------------------------------------------------------------------
// 0x0045e2d0 - behaviour 0x0A: float in the stasis pod.
//
// This is the one the lab entrance needs.  Subs 1 and 2 bob the body between
// y = -200 and y = -350 in steps of 5 every 5 frames and go nowhere on their
// own; ONLY `Flg_ck(g_SysFlags, 0x1F)` - the pod activation - forces sub 3,
// which lifts the Tyrant to y > 0, pins it there and raises scd_anim_param so
// the script moves on.  With this stubbed the Tyrant never waited for the pod.
// ---------------------------------------------------------------------------
void em_scd_behavior_pod(void)
{
    switch (ty_sub()) {
    case 0:
        ei(ENTITY, 0x38) = -200;
        ty_sub()++;
        ty_frame() = 0;
        eub(ENTITY, 0xbf) = 0;
        eub(ENTITY, 0x8c) = 0;
        ty_anim() = 6;
        ty_ticks() = 5;
        Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x80);
        if (g_stageId == 4) StMask(0, 3);
        // fallthrough
    case 1:
        ty_ticks() = (short)(ty_ticks() - 1);
        if (ty_ticks() == 0) {
            ty_ticks() = 5;
            ei(ENTITY, 0x38) -= 5;
            if (ei(ENTITY, 0x38) == -0x15e) ty_sub()++;
        }
        break;
    case 2:
        ty_ticks() = (short)(ty_ticks() - 1);
        if (ty_ticks() == 0) {
            ty_ticks() = 5;
            ei(ENTITY, 0x38) += 5;
            if (ei(ENTITY, 0x38) == -200) ty_sub()--;
        }
        break;
    case 3:
        ei(ENTITY, 0x38) += 4;
        if (ei(ENTITY, 0x38) > 0) {
            Flg_on((int)g_SysFlags, ENTITY->scd_anim_param);
            ty_sub()++;
            ei(ENTITY, 0x38) = 0;
        }
        break;
    default:
        break;
    }

    // The release: checked every frame, from any sub.
    if (Flg_ck((int)g_SysFlags, 0x1f) != 0) ty_sub() = 3;
}

// ---------------------------------------------------------------------------
// 0x0045f520 / 0x0045f570 - the severed-limb launcher and its integrator.
//
// A joint flagged this way stops following the skeleton (bit 3 of the joint's
// flag byte) and becomes a free body: an angular velocity at +0x70/+0x72/+0x74,
// a gravity step at +0x76, a bounce budget at +0x78, and a tumble SVECTOR at
// +0x04 that RotMatrix turns into the joint's own rotation each frame.  It
// bounces when its world Y crosses -200, halving and inverting the fall speed,
// and stops once the budget runs out.
// ---------------------------------------------------------------------------
void tyrant_limb_launch(char* joint, const SVECTOR* vel, const SVECTOR* tumble,
                        short gravity, unsigned char bounces)
{
    joint[0] = (char)(joint[0] | 8);
    // The velocity is stored ROTATED: .y first, then .x, then .z.
    *(short*)(joint + 0x70) = vel->y;
    *(short*)(joint + 0x72) = vel->x;
    *(short*)(joint + 0x74) = vel->z;
    *(short*)(joint + 0x76) = gravity;
    *(unsigned short*)(joint + 0x78) = (unsigned short)bounces;
    *(int*)(joint + 4) = *(const int*)tumble;
    *(int*)(joint + 8) = *((const int*)tumble + 1);
}

void tyrant_limb_update(char* joint)
{
    if (*(short*)(joint + 0x78) == 0) return;

    *(int*)(joint + 0x58) += (int)*(short*)(joint + 0x72);          // world X
    short vy = (short)(*(short*)(joint + 0x76) + *(short*)(joint + 0x70));
    *(short*)(joint + 0x70) = vy;                                    // gravity
    *(int*)(joint + 0x60) += (int)*(short*)(joint + 0x74);          // world Z
    int y = *(int*)(joint + 0x5c) + (int)vy;
    *(int*)(joint + 0x5c) = y;

    if (y > -200) {                                                  // floor
        *(int*)(joint + 0x5c) = y - vy;
        *(short*)(joint + 0x78) = (short)(*(short*)(joint + 0x78) - 1);
        *(short*)(joint + 0x70) = (short)-(vy / 2);
    }

    RotMatrix(reinterpret_cast<SVECTOR*>(joint + 4),
              reinterpret_cast<MATRIX*>(joint + 0x24));
    MulMatrixInPlace(reinterpret_cast<MATRIX*>(joint + 0x24),
                     reinterpret_cast<MATRIX*>(joint + 0x44));
}

// ---------------------------------------------------------------------------
// 0x0045e790 - behaviour 13: the rocket-launcher death.
//
// The heliport Tyrant's finish.  Sub 0 blows five joints off as free bodies,
// hides the entity's own render (ty_flags bit 3) and aims a smoke trail at the
// active room camera; sub 1 runs the blast - the fireball streak, the joint
// tint flash and the debris - and sub 2 smoulders for 200 frames before setting
// health to -1 so the room can move on.
//
// The five joints are 2 (0xF8), 4 (0x1F0), 7 (0x364), 10 (0x4D8) and 12 (0x5D0)
// at the 0x7C stride.
// ---------------------------------------------------------------------------
void em_scd_behavior_rocket_death(void)
{
    char* joints = (char*)ENTITY->jointsStructs;
    if (joints == nullptr) return;

    char sub = (char)ty_sub();

    if (sub == 0) {
        ty_flags() |= 8;                       // stop drawing the intact body
        ty_sub() = 1;
        ty_frame() = 0;
        eub(ENTITY, 0xbf) = 0;
        eub(ENTITY, 0x8c) = 7;
        ty_anim() = 0;
        ty_flags() &= 0xfd;
        ENTITY->lookAtFlags = 0;
        ty_ticks() = 0;
        srand(1534);

        Entity* heart = reinterpret_cast<Entity*>(static_cast<uintptr_t>(eu(ENTITY, 0x170)));
        if (tyrant_pool_pointer(heart, sizeof(Entity)))
            ew(heart, 0x70) = (short)0xfed4;

        Play3DSnd(2, 0x19, 0, (int)&ei(ENTITY, 0x34));
        Play3DSnd(2, 0x1a, 0, (int)&ei(ENTITY, 0x34));
        Play3DSnd(2, 0x1b, 0, (int)&ei(ENTITY, 0x34));

        joints[0] = (char)(joints[0] | 0x28);
        joint_setup_attack_effect((int)(joints         ), 0x13, 0x0c, 3);
        joint_setup_attack_effect((int)(joints + 0x07c), 0x13, 0,    3);
        joint_setup_attack_effect((int)(joints + 0x174), 0x13, 0,    3);
        joint_setup_attack_effect((int)(joints + 0x2e8), 0x13, 0,    3);
        joint_setup_attack_effect((int)(joints + 0x45c), 0x13, 0,    3);

        // Five limbs, each with its own launch velocity and tumble axis.
        struct { unsigned int off; short vx, vy, vz; short tx, ty, tz; } kLimbs[5] = {
            { 0x0f8,      0,  -500,     0, 0x40,    0, 0x60 },
            { 0x1f0,     10,  -400, 0x19, 0x40, 0x60,    0 },
            { 0x364, -0x1e, (short)-0x1c2, 0x0f,    0,    0, 0x40 },
            { 0x4d8, -0x14, (short)-0x15e,  -10,    0, 0x60,    0 },
            { 0x5d0, -0x14,  -300, 0x0f,    0, 0x40, 0x40 }
        };
        for (const auto& L : kLimbs) {
            g_svecScratch.x = L.vx;
            g_svecScratch.y = L.vy;
            g_svecScratch.z = L.vz;
            SVECTOR tumble = { L.tx, L.ty, L.tz, 0 };
            tyrant_limb_launch(joints + L.off, &g_svecScratch, &tumble, 0x0f, 3);
        }

        ty_ticks() = 0;

        // Aim the smoke trail at the live room camera (RDT light block - 4).
        const int* cam = (const int*)((char*)g_RdtPointer[1].lights +
                                      (unsigned int)g_roomCameraId * 0x2c - 4);
        ENTITY->speed.x = (short)((cam[0] - ei(ENTITY, 0x34)) / 0x32);
        ENTITY->speed.y = (short)((short)cam[1] + (short)((1000 - ei(ENTITY, 0x38)) / 0x32));
        ENTITY->speed.z = (short)((cam[2] - ei(ENTITY, 0x3c)) / 0x32);
        return;
    }

    if (sub != 1) {
        if (sub != 2) return;
        // Smoulder, then hand the room the death.
        if (ty_ticks() > 200) {
            ty_sub() = 3;
            euw(ENTITY, 0x88) = 0xffff;        // health = -1
        }
        ty_ticks() = (short)(ty_ticks() + 1);

        if ((eub(ENTITY, 0xc4) & 3) == 0) {
            g_playerPosScratch.y = 0;
            g_playerPosScratch.x = (rand() & 0x7ff) - 1000;
            g_playerPosScratch.z = (rand() & 0x7ff) - 1000;
            Effect_CreateBillboard(9, 0x0d, 0, &ENTITY->scaMatrixData.localMatrix,
                                   &g_playerPosScratch, 0x1e);
        }
        if ((eub(ENTITY, 0xc4) & 7) != 0) return;

        g_playerPosScratch.x = 0;
        g_playerPosScratch.y = 1000;
        g_playerPosScratch.z = 0;
        Effect_CreateBillboard(9, 0x0d, 0, joints + 0x234, &g_playerPosScratch, 0x1e);
        Effect_CreateBillboard(9, 0x0d, 0, joints + 0x3a8, &g_playerPosScratch, 0x1e);
        Effect_CreateBillboard(9, 0x0d, 0, joints + 0x51c, &g_playerPosScratch, 0x1e);
        Effect_CreateBillboard(9, 0x0d, 0, joints + 0x614, &g_playerPosScratch, 0x1e);
        return;
    }

    // ---- sub 1: the blast ----
    if (ty_ticks() == 2) {
        Play3DSnd(2, 0x19, 0, (int)&ei(ENTITY, 0x34));
        Play3DSnd(2, 0x1a, 0, (int)&ei(ENTITY, 0x34));
        Play3DSnd(2, 0x1b, 0, (int)&ei(ENTITY, 0x34));
    }
    if (ty_ticks() == 6) {
        Play3DSnd(2, 0x19, 0, (int)&ei(ENTITY, 0x34));
        Play3DSnd(2, 0x1a, 0, (int)&ei(ENTITY, 0x34));
        Play3DSnd(2, 0x1b, 0, (int)&ei(ENTITY, 0x34));
    }
    if (ty_ticks() == 9) Play3DSnd(2, 0x19, 0, (int)&ei(ENTITY, 0x34));

    if (ty_frame() < 0x0e) {
        // The rocket's smoke streak, walking away from the body along a fixed
        // vector scaled by the tick counter.
        short t = ty_ticks();
        VECTOR head;
        head.x = ei(ENTITY, 0x34) + (t * 0x0e7a) / 0x0c;
        head.y = ei(ENTITY, 0x38) + (t * -0x0fb0) / 0x0c - 0x9c4;
        head.z = ei(ENTITY, 0x3c) + (t * -0x09ae) / 0x0c;
        head.pad = 0;
        g_playerPosScratch = head;
        Effect_CreateBillboard(0x0e, 3, 0, nullptr, &head, 0x0a);

        g_playerPosScratch.x = (head.x - (rand() & 0x7f)) + 0x40;
        g_playerPosScratch.y = (head.y - (rand() & 0x7f)) + 0x40;
        g_playerPosScratch.z = (head.z - (rand() & 0x7f)) + 0x40;
        Effect_CreateBillboard(0x0e, 3, 0, nullptr, &g_playerPosScratch, 0x14);

        ty_ticks() = (short)(ty_ticks() + 1);
        if (ty_ticks() > 10) {
            ty_ticks() = 10;
            g_playerPosScratch.x = (head.x - (rand() & 0x1ff)) + 0x100;
            g_playerPosScratch.y = (head.y - (rand() & 0x7f))  + 0x40;
            g_playerPosScratch.z = (head.z - (rand() & 0x1ff)) + 0x100;
            Effect_CreateBillboard(0x0e, 3, 0, nullptr, &g_playerPosScratch, 0x0f);

            g_playerPosScratch.x = (head.x - (rand() & 0x1ff)) + 0x100;
            g_playerPosScratch.y = (head.y - (rand() & 0x7f))  + 0x40;
            g_playerPosScratch.z = (head.z - (rand() & 0x1ff)) + 0x100;
            Effect_CreateBillboard(0x0e, 0x0b, 0, nullptr, &g_playerPosScratch, 0x0a);

            ty_flags() &= 0xfe;                // ground shadow off
        }
    }

    if (ty_frame() == 8) {
        // The white-hot flash across every surviving joint.
        static const unsigned int kTintJoints[10] = {
            0x0f8, 0x1f0, 0x26c, 0x364, 0x3e0, 0x4d8, 0x554, 0x5d0, 0x64c, 0x6c8
        };
        for (unsigned int o : kTintJoints) {
            JointApplyColorTint(reinterpret_cast<JointStruct*>(joints + o),
                                0, 0x102810, (void*)0x202030);
        }
    }

    if (ty_frame() < 10 && (ty_frame() & 7) == 0) {
        g_playerPosScratch.x = (rand() & 0x3ff) - 500;
        g_playerPosScratch.y = (rand() & 0x3ff);
        g_playerPosScratch.z = (rand() & 0x3ff) - 500;
        Effect_CreateBillboard(0x0e, 3, 0, joints + 0x13c, &g_playerPosScratch, 0x1e);
        Effect_CreateBillboard(0x0e, 3, 0, joints + 0x234, &g_playerPosScratch, 0x1e);
        Effect_CreateBillboard(0x0e, 3, 0, joints + 0x3a8, &g_playerPosScratch, 0x1e);
        Effect_CreateBillboard(0x0e, 3, 0, joints + 0x51c, &g_playerPosScratch, 0x1e);
        Effect_CreateBillboard(0x0e, 3, 0, joints + 0x614, &g_playerPosScratch, 0x1e);
    }

    if (ty_frame() > 0x10 && ty_frame() < 0x13) {
        // Six alternating smoke / fire puffs off the body.
        static const struct { unsigned char type, depth; } kPuffs[6] = {
            { 9, 0x05 }, { 9, 0x15 }, { 9, 0x15 },
            { 0x0e, 0x01 }, { 0x0e, 0x11 }, { 0x0e, 0x01 }
        };
        for (const auto& p : kPuffs) {
            g_playerPosScratch.x = (rand() & 0x7ff) - 1000;
            g_playerPosScratch.y = -1000 - (rand() & 0x7ff);
            g_playerPosScratch.z = (rand() & 0x7ff) - 1000;
            Effect_CreateBillboard(p.type, p.depth, 0,
                                   &ENTITY->scaMatrixData.localMatrix,
                                   &g_playerPosScratch, 0x1e);
        }
    }

    if (ty_frame() > 0x12 && ((ty_frame() + 1) & 3) == 0) {
        g_playerPosScratch.x = (rand() & 0x3ff) - 500;
        g_playerPosScratch.y = (rand() & 0x3ff);
        g_playerPosScratch.z = (rand() & 0x3ff) - 500;
        Effect_CreateBillboard(9, 0x0d, 0, joints + 0x13c, &g_playerPosScratch, 0x1e);
        Effect_CreateBillboard(9, 0x0d, 0, joints + 0x234, &g_playerPosScratch, 0x1e);
        Effect_CreateBillboard(9, 0x0d, 0, joints + 0x3a8, &g_playerPosScratch, 0x1e);
        Effect_CreateBillboard(9, 0x0d, 0, joints + 0x51c, &g_playerPosScratch, 0x1e);
        Effect_CreateBillboard(9, 0x0d, 0, joints + 0x614, &g_playerPosScratch, 0x1e);
    }

    if ((char)Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x200) != 0) {
        ty_sub() = 2;
        ty_ticks() = 0;
    }

    // The exposed heart drops out of the chest once it clears -400.
    Entity* heart = reinterpret_cast<Entity*>(static_cast<uintptr_t>(eu(ENTITY, 0x170)));
    if (tyrant_pool_pointer(heart, sizeof(Entity))) {
        if (ew(heart, 0x6e) < -400) {
            ei(heart, 0x38) += (int)ew(heart, 0x70);
            ew(heart, 0x76) = (short)(ew(heart, 0x76) + 0x18);
        }
        ew(heart, 0x70) = (short)(ew(heart, 0x70) + 0x0f);
    }

    tyrant_limb_update(joints + 0x0f8);
    tyrant_limb_update(joints + 0x1f0);
    tyrant_limb_update(joints + 0x364);
    tyrant_limb_update(joints + 0x4d8);
    tyrant_limb_update(joints + 0x5d0);
}

// 0x0045f5f0 - behaviour 0x0D: play animation 10 to its end, then report.
void em_scd_behavior_anim(void)
{
    if (ty_sub() == 0) {
        ty_sub() = 1;
        ty_frame() = 0;
        eub(ENTITY, 0xbf) = 0;
        eub(ENTITY, 0x8c) = 0x1f;
        ty_anim() = 10;
        ty_speed() = 0;
    } else if (ty_sub() != 1) {
        return;
    }

    if ((char)Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x80) != 0) {
        Flg_on((int)g_SysFlags, ENTITY->scd_anim_param);
        euw(ENTITY, 0x86) = 0;
    }
}

// 0x0045f6a0 - behaviour 0x0E: turn to face the player, then report.
// Gives up after (rand & 0x3F) + 0x28 frames even if the turn never converges.
void em_scd_behavior_face_player(void)
{
    if (ty_sub() == 0) {
        ty_frame() = 0;
        eub(ENTITY, 0xbf) = 0;
        eub(ENTITY, 0x8c) = 0x1f;
        ENTITY->hit_state = 0;
        if ((short)turn_toward_target((VECTOR*)ty_playerT(), 0x400) != 0)
            ty_anim() = 1;
        ty_sub() = 1;
        ty_ticks() = (short)(((unsigned short)rand() & 0x3f) + 0x28);
    }

    short stepSize = ((ENTITY->behavior_flags & 1) == 0) ? 0x28 : 0x14;
    g_animFrameIdSave = (unsigned int)(int)(short)turn_toward_target((VECTOR*)ty_playerT(), stepSize);

    short prev = ty_ticks();
    ty_ticks() = (short)(prev - 1);
    if (prev == 0 || (int)g_animFrameIdSave == 0) {
        Flg_on((int)g_SysFlags, ENTITY->scd_anim_param);
        euw(ENTITY, 0x86) = 0;
        ew(ENTITY, 0x166) = (short)g_playerEntity.scaMatrixData.localMatrix.t[0];
        ew(ENTITY, 0x168) = (short)g_playerEntity.scaMatrixData.localMatrix.t[2];
    } else {
        Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x80);
        ew(ENTITY, 0x74) = (short)(ew(ENTITY, 0x74) + (short)(int)g_animFrameIdSave);
    }

    if (ty_frame() == 3 || ty_frame() == 0x24) Snd_em(0);
}

// 0x0045f810 - behaviour 0x0F: the scripted swing at the victim entity.
// Frame 0x0B knocks the victim into its own reaction and raises SysFlags 0x1E
// on completion - the same "Tyrant is loose" bit behaviour 0x0B sets.
void em_scd_behavior_strike(void)
{
    char sub = (char)ty_sub();
    if (sub == 0) {
        ty_sub() = 1;
        ty_frame() = 0;
        eub(ENTITY, 0xbf) = 0;
        ty_anim() = 4;
        eub(ENTITY, 0x8c) = 7;
        Snd_em(1);
    } else if (sub != 1) {
        if (sub != 2) return;
        Flg_on((int)g_SysFlags, 0x1e);
        euw(ENTITY, 0x86) = 0;
        return;
    }

    ty_sub() = (unsigned char)(ty_sub() +
        (char)Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x200));

    if (ty_frame() == 0x0b) {
        eu(g_emScdVictim, 0x84) = 0x00010001;   // victim -> state 1, behaviour 1
        tyrant_load_fx_anchor(800);
        Effect_CreateBillboard(0, 3, 0, ty_clawWorld(), &g_playerPosScratch, 0);
        JointApplyColorTint(ty_clawJoint(), 0xff, 0x80880, (void*)0x808080);
        Snd_em(2);
    }
}

// 0x0045e470 - behaviour 0x0B: the scripted impale.
void em_scd_behavior_impale(void)
{
    Entity* victim = g_emScdVictim;
    unsigned char sub = ty_sub();

    if (sub == 0) {
        // 0x0047d120 - drop texture set 2 on stage 4 room 0x13 only.
        if (g_stageId == 4 && g_roomId == 0x13) TexturePage_DeleteSet(2);

        ty_sub() = 1;
        ty_frame() = 0;
        eub(ENTITY, 0xbf) = 0;
        eub(ENTITY, 0x8c) = 0x1f;
        ty_anim() = 5;
        ty_speed() = 100;
        snap_player_to_grab_position(victim);
        ENTITY->status_flags |= 2;
        entity_rotate_toward_target(reinterpret_cast<VECTOR*>(&ei(victim, 0x34)), 0x400);
        victim->status_flags |= 6;
        eu(victim, 0x84) = 0x00030001;       // state 1, behaviour 3, sub 0
        Snd_em(1);
        return;
    }
    if (sub != 1) {
        if (sub != 2) return;
        Flg_on((int)g_SysFlags, 0x1e);
        ENTITY->action_behavior = 0;
        ty_sub() = 0;
        ty_hitMask() &= 0xf8;
        ENTITY->status_flags &= 0xfd;
        return;
    }

    if (ty_frame() == 6) Snd_em(4);
    if (ty_frame() == 6 || ty_frame() == 0x5c) {
        tyrant_load_fx_anchor(800);
        Effect_CreateBillboard(0, 3, 0, ty_clawWorld(), &g_playerPosScratch, 0);
        JointApplyColorTint(ty_clawJoint(), 0xff, 0x80880, (void*)0x808080);
    }
    if (ty_frame() < 0x61 && ty_frame() % 7 == 0) {
        tyrant_load_fx_anchor(800);
        Effect_CreateBillboard(0, 0, 0, ty_clawWorld(), &g_playerPosScratch, 0);
    }
    if (ty_frame() == 0x5f) Snd_em(7);

    entity_apply_anim_vertex(ENTITY, ENTITY->animHeader, ENTITY->animBase);
    ty_sub() = (unsigned char)(ty_sub() +
        (char)Joint_move(0, ENTITY->animHeader, ENTITY->animBase, 0x80));
    if (ty_speed() != 0)
        entity_rotate_toward_target(reinterpret_cast<VECTOR*>(&ei(victim, 0x34)), 0x10);

    tyrant_decay_speed(-5);
    Add_speedXZ(0);
    ew(ENTITY, 0xc6) = (short)(ew(ENTITY, 0xc6) + ENTITY->speed.x);
    ew(ENTITY, 0xc8) = (short)(ew(ENTITY, 0xc8) + ENTITY->speed.z);
    ew(victim, 0xc6) = (short)(ew(victim, 0xc6) + ENTITY->speed.x);
    ew(victim, 0xc8) = (short)(ew(victim, 0xc8) + ENTITY->speed.z);
}

// Reached only when the SCD asks for a behaviour the table has no entry for.
// The original does NOT bounds-check - 0x0045c62d is a bare
// `JMP [ECX*4 + 0x4c10c8]` on the raw action_behavior byte - so out of range
// there means jumping into whatever data follows the table.  See the table
// comment below for what that data actually is.
void em_scd_out_of_range(void)
{
    static unsigned int reported = 0;
    unsigned int bit = 1u << (ENTITY->action_behavior & 0x1f);
    if ((reported & bit) == 0) {
        reported |= bit;
        dbg_printf("[TYRANT] em_scd behaviour %u has no 0x004c10c8 entry\n",
                   ENTITY->action_behavior);
    }
}

typedef void (*EmScdBehavior)(void);

// 0x004c10c8 - the Tyrant's SCD behaviour table.  EIGHTEEN slots, 0..17.
//
// The length is load-bearing, and it is easy to get wrong in both directions.
// The NULL runs (slot 1, slots 3..9, slot 17) make it tempting to stop early;
// being one slot out puts the Wesker impale where the stasis pod belongs, which
// is exactly what made the lab Tyrant kill Wesker the instant the room loaded
// instead of floating in the capsule.  Verified with aligned 16-byte reads at
// 0x004c10e8 / 0x004c10f0 / 0x004c1108.
//
// It is equally easy to run PAST the end.  This table was previously written
// with 24 slots, the last six pointing at 0x004604d0, 0x004604e0, 0x00460520,
// 0x00460550, 0x00460570 and 0x00460650 and marked "not ported".  Those are not
// Tyrant code at all: 0x004c10c8 + 18*4 == 0x004c1110, which is
// script_command_funcs_table, and those six addresses are its opcodes
// 0x00..0x05 - cmd_nop, cmd_if, cmd_else, cmd_end_if, cmd_bit_test, cmd_bit_op,
// all already implemented in CmdFunctions.cpp.  They were the adjacent table
// bleeding in, not six unported behaviours, so there is nothing to port there.
//
// The dispatcher at 0x0045c62d does no bounds check whatsoever, so in the
// original a behaviour byte >= 18 really does jump into the SCD command table
// and call an opcode handler with the wrong ABI.  No Tyrant SCD emits one; the
// port reports it instead of reproducing the jump.
const EmScdBehavior s_emScdBehaviors[18] = {
    /*  0  0x0045c650 */ em_scd_behavior_idle,
    /*  1  -          */ nullptr,
    /*  2  0x0045c6c0 */ em_scd_behavior_walk_to,
    /*  3  -          */ nullptr,
    /*  4  -          */ nullptr,
    /*  5  -          */ nullptr,
    /*  6  -          */ nullptr,
    /*  7  -          */ nullptr,
    /*  8  -          */ nullptr,
    /*  9  -          */ nullptr,
    /* 10  0x0045c900 */ em_scd_behavior_break_glass,
    /* 11  0x0045e2d0 */ em_scd_behavior_pod,          // <- tyrant_init selects this one
    /* 12  0x0045e470 */ em_scd_behavior_impale,
    /* 13  0x0045e790 */ em_scd_behavior_rocket_death,
    /* 14  0x0045f5f0 */ em_scd_behavior_anim,
    /* 15  0x0045f6a0 */ em_scd_behavior_face_player,
    /* 16  0x0045f810 */ em_scd_behavior_strike,
    /* 17  -          */ nullptr
};

} // namespace

static void tyrant_state_scd(void)
{
    g_emScdVictim = &g_EnemiesList[0];

    if ((ENTITY->behavior_flags & 0x40) != 0) {
        unsigned char b = ENTITY->action_behavior;
        // Trace only on change - the hand-off ORDER is what matters when the
        // lab scene desyncs, and a per-frame line would drown the log.
        static unsigned char lastBehavior = 0xff;
        static unsigned char lastSub = 0xff;
        if (b != lastBehavior || ENTITY->action_state != lastSub) {
            lastBehavior = b;
            lastSub = ENTITY->action_state;
            dbg_printf("[TYRANT] scd behaviour %u sub %u  y=%d flg1e=%u flg1f=%u victim=%p\n",
                       b, ENTITY->action_state, ei(ENTITY, 0x38),
                       Flg_ck((int)g_SysFlags, 0x1e), Flg_ck((int)g_SysFlags, 0x1f),
                       (void*)g_emScdVictim);
        }
        if (b < 18 && s_emScdBehaviors[b] != nullptr) s_emScdBehaviors[b]();
        else em_scd_out_of_range();
        return;
    }

    set_state_word(0x00010001);      // behaviour 1 -> table[3], walk
    ty_flags() |= 2;
}

// ===========================================================================
// The state table (0x004ba280).  Slots 5, 6, 7 and 9 are genuinely NULL in the
// original - nothing ever writes those state values, and the original would
// jump through a null pointer if anything did.
// ===========================================================================
namespace {

typedef void (*TyrantState)(void);

const TyrantState s_tyrantStates[10] = {
    tyrant_init,            // [0] 0x004212b0
    tyrant_state1,          // [1] 0x00421d10
    tyrant_state_hit,       // [2] 0x00421d60
    tyrant_state_forced,    // [3] 0x00421e10
    tyrant_state_nop,       // [4] 0x00421e50
    nullptr, nullptr, nullptr,
    tyrant_state_scd,       // [8] 0x0045c610
    nullptr
};

} // namespace

// ===========================================================================
// 0x00421990 - per-frame entry, both ids.
// ===========================================================================
void tyrant_update(void)
{
    // Any live Tyrant whose behavior_flags carry 0x40 is handed to the SCD.
    if (ENTITY->state != 0 && (ENTITY->behavior_flags & 0x40) != 0)
        ENTITY->state = 8;

    // behavior_flags 0x80 suspends everything except the init pass.
    if (ENTITY->state != 0 && (ENTITY->behavior_flags & 0x80) != 0)
        return;

    if ((g_message_flags & 4) != 0) {
        if (ENTITY->state < 10 && s_tyrantStates[ENTITY->state] != nullptr)
            s_tyrantStates[ENTITY->state]();

        // action_behavior 9 is the impale: the grabbed player must not be
        // pushed out of the animation by the collision solver.
        if (ENTITY->action_behavior != 9) {
            ResolveEntityScaCollision(reinterpret_cast<Entity*>(&g_playerEntity), ENTITY);
            HandleEnemyPlayerCollisions();
            eub(ENTITY, 0x16c) |= check_room_collision(
                reinterpret_cast<VECTOR*>(&ei(ENTITY, 0x34)),
                *(short*)((char*)(uintptr_t)ENTITY->Sca_info + 10));
            ew(ENTITY, 0x17c) = (short)(unsigned int)(uintptr_t)g_tempVar;
        }

        // FUN_004259f0 (pad rumble) is a bare RET on PC - see the file header.
        // The phase counter it consumed still has to advance.
        eb(ENTITY, 0x16d)++;
        if (eub(ENTITY, 0x16d) > 0x1d) eub(ENTITY, 0x16d) = 0;

        if ((ty_flags() & 2) != 0) {
            unsigned char mode = eub(ENTITY, 0x16f);
            if (mode == 0) {
                ENTITY->lookAtFlags = 0x13;
                JointStruct* pj = g_playerEntity.jointsStructs;
                ENTITY->scd_pos_x = pj[1].world.t[0];
                ENTITY->scd_pos_y = pj[1].world.t[1];
                ENTITY->scd_pos_z = pj[1].world.t[2];
            } else if (mode < 4) {
                eub(ENTITY, 0x16f) = (unsigned char)(mode - 1);
                ENTITY->lookAtFlags = 0;
                ENTITY->hit_state = 0;
            } else {
                eub(ENTITY, 0x16f) = (unsigned char)(mode - 1);
                ENTITY->lookAtFlags = 0x33;
                ENTITY->scd_pos_x = 400;
                ENTITY->scd_pos_y = 0xe0c;
            }
        }

        // health >> 8 & 0x80 == 0: still alive.
        if ((eub(ENTITY, 0x89) & 0x80) == 0) EntityUpdateLookAtAngles();

        eu(ENTITY, 0x174) = eu(ENTITY, 0x84);   // state-word backup for state 2
    }

    if ((ty_flags() & 8) == 0) tyrant_draw_heart();

    ENTITY->has_enter_switch_zone = (unsigned char)is_entity_in_switch_zone(
        reinterpret_cast<VECTOR*>(&ei(ENTITY, 0x34)), g_CurrentRdtDataTypePtr);

    if (ENTITY->id != 12 && (ty_flags() & 8) == 0) {
        tyrant_draw_claw_ghosts();

        // The 0x8000 bit arms the ribbon: seed the whole history from the
        // current claw pose, sweeping the far point through 0x004ba27a.
        if ((g_tyTrailTimer & 0x8000) != 0) {
            MATRIX* claw = ENTITY->jointsStructs
                         ? ty_clawWorld() : nullptr;
            g_tyTrailSegments = 8;
            g_entity_bkp = 7;
            if (g_tyClawScaleB > 8000) g_tyTrailFar.y = (short)(g_tyTrailFar.y + 1000);
            g_tyTrailFar.y = (short)(g_tyTrailFar.y - 800);
            do {
                g_tyTrailFar.y = (short)(g_tyTrailFar.y + 100);
                tyrant_trail_push(g_tyTrailBlock, claw, claw,
                                  &g_tyTrailNear, &g_tyTrailFar,
                                  (unsigned char)g_entity_bkp, 0);
                unsigned int n = g_entity_bkp;
                g_entity_bkp = n - 1;
                if (n == 0) break;
            } while (true);
            tyrant_trail_push(g_tyTrailBlock, claw, claw,
                              &g_tyTrailNear, &g_tyTrailNear, 8, 0);
            g_tyTrailTimer &= 0x7fff;
            if (g_tyClawScaleB > 8000) g_tyTrailFar.y = (short)(g_tyTrailFar.y - 1000);
        }

        if (g_tyTrailTimer != 0) tyrant_trail_update();
    }

    if ((ty_flags() & 1) != 0 && ENTITY->has_enter_switch_zone != 0) {
        entity_add_fade_sprite(reinterpret_cast<VECTOR*>(&ei(ENTITY, 0x34)),
                               (short*)&ENTITY->pushVelocity, 0, ENTITY->angle);
    }

    // The rocket launcher kill: once the rooftop Tyrant drops below 201 HP the
    // room's "boss dead" flag goes up and the health is pinned so the death
    // behaviour runs exactly once.
    if ((ty_flags() & 8) == 0 && ENTITY->id == 0x10 && ENTITY->health < 0xc9) {
        Flg_on((int)g_SysFlags, 0x1f);
        ENTITY->health = 200;
    }

    update_player_position(reinterpret_cast<PlayerEntity*>(ENTITY), 2);
}

// ===========================================================================
// The PLAYER side of a Tyrant hit (table 0x004ba360, three entries).
//
// player_anim_limb_physics (0x00424fb0, g_playerAnimFunctions[31]) dispatches
// this table by the PLAYER's action_behavior - exactly the byte the Tyrant's
// attack behaviours write: 0 for the backhand and the heavy swing, 1 for the
// swipe / slash / thrust, 2 for the knock-back.  Index 31 is reached as
// animationId 6 -> player_state_anim_window1 -> animFrameId 0x0C + 0x13.
//
// The table lives at 0x004ba360, between the Tyrant's per-id think table
// (0x004ba334) and its heart-beat ramp (0x004ba370) - Tyrant data, not shared
// player data, which is why it is defined here.  Globals.cpp had it as an
// all-NULL placeholder, so the reaction dispatched nothing and Chris stayed
// frozen in the hit pose the first time the Tyrant connected.
//
// These run with ENTITY pointing at the player (update_player_anim sets it),
// and they animate through the player's DAMAGE animation pointers at
// +0x16C/+0x170, not the ordinary animHeader/animBase.
// ===========================================================================
namespace {

// The attacker the player latched into unk_b8 when the hit landed.
inline char* ty_attacker(void) { return (char*)(uintptr_t)g_playerEntity.unk_b8; }

inline int ty_playerSoundPos(void) { return (int)g_playerEntity.scaMatrixData.localMatrix.t; }

// Bleed the player's knock-back speed off, clamped at zero.  0xC2 is unsigned
// in the struct but the original compares it signed.
inline void ty_playerDecaySpeed(int perFrame)
{
    short v = (short)(g_playerEntity.move_speed_current +
                      (unsigned short)g_playerEntity.animation_frame_id * perFrame);
    g_playerEntity.move_speed_current = (unsigned short)(v < 0 ? 0 : v);
}

// Shared body of handlers 0 and 1 - identical bar four constants.
void player_tyrant_stagger(short startSpeed, int decay, int bloodY, int pushBias)
{
    unsigned char sub = g_playerEntity.action_state;

    if (sub == 0) {
        g_playerEntity.action_state = 1;
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf = 0;
        g_playerEntity.isBeingAttackedFlag = 1;
        g_playerEntity.unk_8c = 3;
        g_playerEntity.move_speed_current = (unsigned short)startSpeed;
    } else if (sub != 1) {
        if (sub == 2) {
            // Hand the player back to the controller.
            g_playerEntity.animationId = 1;
            g_playerEntity.animFrameId = 0;
            g_playerEntity.action_behavior = 0;
            g_playerEntity.action_state = 0;
            g_playerEntity.isBeingAttackedFlag = 0;
        }
        ty_playerDecaySpeed(decay);
        Add_speedXZ(((int)*(short*)(ty_attacker() + 0x74)
                     - (int)g_playerEntity.directionAngle) + pushBias);
        return;
    }

    if (g_playerEntity.health < 0x1f) {
        entity_rotate_toward_target(
            reinterpret_cast<VECTOR*>(ty_attacker() + 0x34), 0x40);
    }

    if (g_playerEntity.animation_frame_id == 3)
        Play3DSnd(3, g_playerEntity.attackAnim + 1, 0, ty_playerSoundPos());

    if (g_playerEntity.animation_frame_id < 4) {
        const int* dead = (const int*)((char*)(uintptr_t)g_deadMoveValue + 0x14);
        g_playerPosScratch.x   = dead[0];
        g_playerPosScratch.z   = dead[2];
        g_playerPosScratch.pad = dead[3];
        g_playerPosScratch.y   = 800;
        // Blood off the Tyrant's claw (its joint 8 world matrix)...
        Effect_CreateBillboard(0, 0, 0,
            (void*)(*(int*)(ty_attacker() + 0x98) + 0x424), &g_playerPosScratch, 0);
        // ...and off the player.
        g_playerPosScratch.y = bloodY;
        Effect_CreateBillboard(0, 0, 0,
            &g_playerEntity.scaMatrixData.localMatrix, &g_playerPosScratch, 0);
    }

    g_playerEntity.action_state = (unsigned char)(g_playerEntity.action_state +
        (char)Joint_move(0, g_playerEntity.emdScratchPtr1,
                         g_playerEntity.emdScratchPtr2, 0x400));

    ty_playerDecaySpeed(decay);
    Add_speedXZ(((int)*(short*)(ty_attacker() + 0x74)
                 - (int)g_playerEntity.directionAngle) + pushBias);
}

// 0x00424fc0 - behaviour 0: the backhand / heavy-swing stagger.
void player_tyrant_hit_00(void) { player_tyrant_stagger(600, -0x28, -0x514, 0xdf4); }

// 0x00425140 - behaviour 1: the swipe / slash / thrust stagger.
void player_tyrant_hit_01(void) { player_tyrant_stagger(500, -0x1e, -0x5dc, 500); }

// 0x004252c0 - behaviour 2: the full knock-back - launched, slide, wall impact,
// then the get-up.  Sub 1 aborts into sub 5 (the wall slam) the moment
// check_room_collision reports a hit.
void player_tyrant_hit_02(void)
{
    switch (g_playerEntity.action_state) {
    case 0:
        g_playerEntity.action_state = 1;
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf = 0;
        g_playerEntity.isBeingAttackedFlag = 1;
        g_playerEntity.attackAnim = 5;
        g_playerEntity.unk_8c = 4;
        g_playerEntity.move_speed_current = 900;
        // fallthrough
    case 1: {
        g_playerEntity.move_speed_current = (unsigned short)
            (g_playerEntity.move_speed_current +
             (unsigned short)g_playerEntity.animation_frame_id * -0xc);
        if (g_playerEntity.animation_frame_id == 3)
            Play3DSnd(3, 2, 0, ty_playerSoundPos());

        g_playerEntity.action_state = (unsigned char)(g_playerEntity.action_state +
            (char)Joint_move(0, g_playerEntity.emdScratchPtr1,
                             g_playerEntity.emdScratchPtr2, 0x400));
        Add_speedXZ(0x800);

        // check_room_collision takes a VECTOR, whose .pad overruns localMatrix.t
        // into the first dword of worldMatrix - so the original saves and
        // restores that dword too, not just the three position words.
        int sx = g_playerEntity.scaMatrixData.localMatrix.t[0];
        int sy = g_playerEntity.scaMatrixData.localMatrix.t[1];
        int sz = g_playerEntity.scaMatrixData.localMatrix.t[2];
        unsigned int sw = *(unsigned int*)&g_playerEntity.scaMatrixData.worldMatrix.m[0][0];

        g_playerDisplacement = (int)check_room_collision(
            (VECTOR*)g_playerEntity.scaMatrixData.localMatrix.t,
            *(short*)((char*)(uintptr_t)g_playerEntity.Sca_info + 10));

        g_playerEntity.scaMatrixData.localMatrix.t[0] = sx;
        g_playerEntity.scaMatrixData.localMatrix.t[1] = sy;
        g_playerEntity.scaMatrixData.localMatrix.t[2] = sz;
        *(unsigned int*)&g_playerEntity.scaMatrixData.worldMatrix.m[0][0] = sw;

        if (g_playerDisplacement != 0) {
            g_playerEntity.action_state = 5;          // hit a wall -> slam
            g_playerEntity.isBeingAttackedFlag = 0;
            return;
        }
        break;
    }
    case 2:
        g_playerEntity.action_state = 3;
        g_playerEntity.unk_8c = 3;
        g_playerEntity.unk_bf = 0;
        g_playerEntity.attackAnim = 6;
        Play3DSnd(2, 0x1f, 0, ty_playerSoundPos());
        // fallthrough
    case 3: {
        if ((g_playerEntity.animation_frame_id & 1) == 0 &&
            g_playerEntity.animation_frame_id < 10) {
            const int* dead = (const int*)((char*)(uintptr_t)g_deadMoveValue + 0x14);
            g_playerPosScratch.x   = dead[0];
            g_playerPosScratch.y   = dead[1];   // full anchor here, y NOT overridden
            g_playerPosScratch.z   = dead[2];
            g_playerPosScratch.pad = dead[3];
            JointStruct* j = g_playerEntity.jointsStructs;
            Effect_CreateBillboard(9, 0x16, 0, &j[5].world, &g_playerPosScratch, 0);
            Effect_CreateBillboard(9, 0x16, 0, &j[8].world, &g_playerPosScratch, 0);
        }
        g_playerEntity.action_state = (unsigned char)(g_playerEntity.action_state +
            (char)Joint_move(0, g_playerEntity.emdScratchPtr1,
                             g_playerEntity.emdScratchPtr2, 0x400));
        Add_speedXZ(0x800);
        short v = (short)(g_playerEntity.move_speed_current - 0xc);
        if (v < 0) { g_playerEntity.move_speed_current = 0; return; }
        g_playerEntity.move_speed_current = (unsigned short)v;
        break;
    }
    case 4:
        g_playerEntity.animationId = 1;
        g_playerEntity.animFrameId = 0;
        g_playerEntity.action_behavior = 0;
        g_playerEntity.action_state = 0;
        g_playerEntity.isBeingAttackedFlag = 0;
        return;
    case 5: {
        g_playerEntity.action_state = 6;
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf = 0;
        g_playerEntity.attackAnim = 3;
        g_playerEntity.unk_8c = 3;
        Play3DSnd(2, 0x20, 0, ty_playerSoundPos());
        Play3DSnd(3, 2, 0, ty_playerSoundPos());
        const int* dead = (const int*)((char*)(uintptr_t)g_deadMoveValue + 0x14);
        JointStruct* j = g_playerEntity.jointsStructs;
        g_playerPosScratch.y   = dead[1];
        g_playerPosScratch.z   = dead[2];
        g_playerPosScratch.pad = dead[3];
        g_playerPosScratch.x   = dead[0] - 400;
        Effect_CreateBillboard(9, 0x16, 0, &j[5].world, &g_playerPosScratch, 0);
        Effect_CreateBillboard(9, 0x16, 0, &j[8].world, &g_playerPosScratch, 0);
        Effect_CreateBillboard(9, 0x11, 0, &j[0].world, &g_playerPosScratch, 0);
        Effect_CreateBillboard(9, 0x11, 0, &j[3].world, &g_playerPosScratch, 0);
        Effect_CreateBillboard(9, 0x11, 0, &j[6].world, &g_playerPosScratch, 0);
        // fallthrough
    }
    case 6:
        g_playerEntity.action_state = (unsigned char)(g_playerEntity.action_state +
            (char)Joint_move(0, g_playerEntity.emdScratchPtr1,
                             g_playerEntity.emdScratchPtr2, 0x400));
        return;
    case 7:
        g_playerEntity.action_state = 8;
        g_playerEntity.unk_bf = 0;
        g_playerEntity.attackAnim = 4;
        g_playerEntity.unk_8c = 3;
        // fallthrough
    case 8:
        g_playerEntity.action_state = (unsigned char)(g_playerEntity.action_state +
            (char)Joint_move(0, g_playerEntity.emdScratchPtr1,
                             g_playerEntity.emdScratchPtr2, 0x400));
        return;
    case 9:
        g_playerEntity.action_state = 10;
        g_playerEntity.unk_bf = 0;
        g_playerEntity.attackAnim = 4;
        g_playerEntity.unk_8c = 3;
        // fallthrough
    case 10:
        // REVERSE playback, and through jointMoveData0/1 rather than the damage
        // pointers - this is the get-up.
        g_playerEntity.action_state = (unsigned char)(g_playerEntity.action_state +
            (char)Joint_move(1, g_playerEntity.jointMoveData0,
                             g_playerEntity.jointMoveData1, 0x400));
        break;
    case 11:
        g_playerEntity.animationId = 1;
        g_playerEntity.animFrameId = 0;
        g_playerEntity.action_behavior = 0;
        g_playerEntity.action_state = 0;
        g_playerEntity.isBeingAttackedFlag = 0;
        g_playerEntity.flags &= 0xfd;
        return;
    default:
        break;
    }
}

} // namespace

// 0x004ba360 - four slots, the last NULL in the original.
void* DAT_004ba360[4] = {
    /* 0  0x00424fc0 */ (void*)player_tyrant_hit_00,
    /* 1  0x00425140 */ (void*)player_tyrant_hit_01,
    /* 2  0x004252c0 */ (void*)player_tyrant_hit_02,
    /* 3  -          */ nullptr
};
