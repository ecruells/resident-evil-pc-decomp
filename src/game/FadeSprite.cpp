// FadeSprite.cpp - Ground-shadow / fade-sprite queue and renderer
//
// Entities queue a translucent ground shadow each frame via
// entity_add_fade_sprite (0x00456810); game_loop then drains the queue through
// DrawFadeSpr (0x00456d30). Both were stubbed, so no shadow reached the renderer
// even once the quad builder (FUN_004565f0) was in place.
//
// The queue record is 144 bytes:
//   +0x00  world position (x, y, z, pad) as ints
//   +0x10  120-byte copy of the entity's shadow quad from entity+0xE4
//          (quad[0] world offset, quad[1..5] header/CLUT/tpage/UVs,
//           quad[0xB..0xE] the four corners - see FUN_004565f0)
//   +0x88  short  y offset
//   +0x8A  short  angle
//   +0x8C  short  sort key
//
// Within that copy the corners sit at short offsets 0x2C/0x30/0x34/0x38, and the
// projected screen coordinates are written back into the quad's vertex slots
// offset by g_spriteAnimActive * 0x14 shorts - the double-buffered half the
// original alternates between.
// ============================================================================
#include "../Globals.h"
#include "SpriteRenderer.h"
#include "../DebugPrint.h"
#include <cstring>

extern void SetRotAndTransMatrix(MATRIX* m);
extern int  RotAverage4(SVECTOR* v0, SVECTOR* v1, SVECTOR* v2, SVECTOR* v3,
                        int* sxy0, int* sxy1, int* sxy2, int* sxy3,
                        int* p, int* flag);

// ============================================================================
// The fade-sprite queue (0x00bca0e0). 32 records of 144 bytes; g_FadeSprCount is
// the fill level, which DrawFadeSpr resets to 0 after draining.
// ============================================================================
#define FADE_SPR_MAX      32
#define FADE_SPR_RECSIZE  0x90

unsigned char g_FadeSpr[FADE_SPR_MAX * FADE_SPR_RECSIZE] = {};   // 0x00bca0e0
int           g_FadeSprCount = 0;

#define FS_REC(i)    (g_FadeSpr + (i) * FADE_SPR_RECSIZE)
#define FS_POS(i)    ((int*)(FS_REC(i)))
#define FS_QUAD(i)   ((short*)(FS_REC(i) + 0x10))
#define FS_YOFS(i)   (*(short*)(FS_REC(i) + 0x88))
#define FS_ANGLE(i)  (*(short*)(FS_REC(i) + 0x8A))
#define FS_SORT(i)   (*(unsigned short*)(FS_REC(i) + 0x8C))

// Placement constants read alongside the tables below.
//   0x004c0d20 = 2  per-sprite vertical step, so coplanar shadows do not fight
//   0x004c0624 = 0  added into the alpha/sort base
//   0x004c0620 = 0  added into the screen X offset
static const int g_FadeSprLayerStep = 2;    // 0x004c0d20
static const int g_FadeSprAlphaBias = 0;    // 0x004c0624
static const int g_FadeSprOffsetX   = 0;    // 0x004c0620

struct FadeSprParam {
    unsigned char flag;      // +0x00  selects which alpha branch DrawFadeSpr takes
    int           sortBias;  // +0x04
    int           offsetX;   // +0x08
    int           unk_0c;    // +0x0C  16 in every record; unread on this path
    int           forceFlag; // +0x10  0 in every record; unread on this path
};

// ============================================================================
// Fade-sprite (ground shadow) placement tables
//
// g_FadeSprParamIndex (0x004c0630): one byte per (stage, room, camera), laid
// out as [(room + stage * 0x20) * 8 + camera]. Selects a record in
// g_FadeSprParams. Stages above 4 are folded down by 5 before indexing, which
// is why only 5 stage blocks exist.
// ============================================================================
static const unsigned char g_FadeSprParamIndex[5 * 0x20 * 8] = {
    // stage 0
    0, 0, 0, 0, 0, 0, 0, 0,    // room  0
    0, 0, 0, 0, 0, 0, 0, 0,    // room  1
    0, 0, 0, 0, 0, 0, 0, 0,    // room  2
    0, 0, 0, 0, 0, 0, 0, 0,    // room  3
    4, 4, 4, 4, 4, 4, 4, 4,    // room  4
    3, 3, 3, 3, 3, 3, 3, 3,    // room  5
    0, 0, 0, 0, 0, 0, 0, 0,    // room  6
    0, 0, 0, 0, 0, 0, 0, 0,    // room  7
    0, 0, 0, 0, 0, 0, 0, 0,    // room  8
    0, 0, 0, 0, 0, 0, 0, 0,    // room  9
    5, 5, 5, 5, 5, 5, 5, 5,    // room 10
    0, 0, 0, 0, 0, 0, 0, 0,    // room 11
    0, 0, 0, 0, 0, 0, 0, 0,    // room 12
    0, 0, 0, 0, 0, 0, 0, 0,    // room 13
    0, 0, 0, 0, 0, 0, 0, 0,    // room 14
    0, 0, 0, 0, 0, 0, 0, 0,    // room 15
    0, 0, 0, 0, 0, 0, 0, 0,    // room 16
    0, 0, 0, 0, 0, 0, 0, 0,    // room 17
    0, 0, 0, 0, 0, 0, 0, 0,    // room 18
    3, 3, 3, 3, 3, 3, 3, 3,    // room 19
    0, 0, 0, 0, 0, 0, 0, 0,    // room 20
    0, 0, 0, 0, 0, 0, 0, 0,    // room 21
    0, 0, 0, 0, 0, 0, 0, 0,    // room 22
    0, 0, 0, 0, 0, 9, 0, 0,    // room 23
    0, 0, 0, 0, 0, 0, 0, 0,    // room 24
    0, 0, 0, 0, 0, 0, 0, 0,    // room 25
    0, 0, 0, 0, 0, 0, 0, 0,    // room 26
    0, 0, 0, 0, 0, 0, 0, 0,    // room 27
    0, 0, 0, 0, 0, 0, 0, 0,    // room 28
    0, 0, 0, 0, 0, 0, 0, 0,    // room 29
    0, 0, 0, 0, 0, 0, 0, 0,    // room 30
    0, 0, 0, 0, 0, 0, 0, 0,    // room 31
    // stage 1
    0, 0, 0, 0, 0, 0, 0, 0,    // room  0
    0, 0, 0, 0, 0, 0, 0, 0,    // room  1
    0, 0, 0, 0, 0, 0, 0, 0,    // room  2
    0, 0, 0, 0, 0, 0, 0, 0,    // room  3
    0, 0, 0, 0, 0, 0, 0, 0,    // room  4
    0, 0, 0, 0, 0, 0, 0, 0,    // room  5
    0, 0, 0, 0, 0, 0, 0, 0,    // room  6
    0, 0, 0, 0, 0, 0, 0, 0,    // room  7
    0, 0, 0, 0, 0, 0, 0, 0,    // room  8
    0, 0, 0, 0, 0, 0, 0, 0,    // room  9
    0, 0, 0, 0, 0, 0, 0, 0,    // room 10
    0, 0, 0, 0, 0, 0, 0, 0,    // room 11
    0, 0, 0, 0, 0, 0, 0, 0,    // room 12
    0, 0, 0, 0, 0, 0, 0, 0,    // room 13
    0, 0, 0, 0, 0, 0, 0, 0,    // room 14
    0, 0, 0, 0, 0, 0, 0, 0,    // room 15
    0, 0, 0, 0, 0, 0, 0, 0,    // room 16
    0, 0, 0, 0, 0, 0, 0, 0,    // room 17
    0, 0, 0, 0, 0, 0, 0, 0,    // room 18
    0, 0, 0, 0, 0, 0, 0, 0,    // room 19
    0, 0, 0, 0, 0, 0, 0, 0,    // room 20
    0, 0, 0, 0, 0, 0, 0, 0,    // room 21
    0, 0, 0, 0, 0, 0, 0, 0,    // room 22
    1, 1, 1, 1, 12, 1, 1, 1,    // room 23
    0, 0, 0, 0, 0, 0, 0, 0,    // room 24
    0, 0, 0, 0, 0, 0, 0, 0,    // room 25
    0, 0, 0, 0, 0, 0, 0, 0,    // room 26
    0, 0, 0, 0, 0, 0, 0, 0,    // room 27
    0, 0, 0, 0, 0, 0, 0, 0,    // room 28
    0, 0, 0, 0, 0, 0, 0, 0,    // room 29
    0, 0, 0, 0, 0, 0, 0, 0,    // room 30
    0, 0, 0, 0, 0, 0, 0, 0,    // room 31
    // stage 2
    1, 0, 12, 0, 0, 0, 0, 0,    // room  0
    6, 6, 6, 6, 6, 6, 6, 14,    // room  1
    19, 6, 6, 6, 6, 6, 6, 6,    // room  2
    0, 0, 0, 0, 0, 0, 0, 0,    // room  3
    0, 0, 0, 0, 0, 0, 0, 0,    // room  4
    10, 0, 0, 0, 0, 0, 0, 0,    // room  5
    0, 0, 0, 0, 0, 0, 0, 0,    // room  6
    0, 0, 0, 0, 16, 0, 0, 0,    // room  7
    0, 0, 0, 0, 0, 0, 0, 0,    // room  8
    0, 0, 0, 0, 0, 0, 0, 0,    // room  9
    0, 0, 0, 0, 0, 0, 0, 0,    // room 10
    0, 0, 0, 0, 0, 0, 0, 0,    // room 11
    0, 0, 0, 0, 0, 0, 0, 0,    // room 12
    0, 0, 0, 0, 0, 0, 0, 0,    // room 13
    0, 0, 0, 0, 0, 0, 0, 0,    // room 14
    0, 18, 0, 0, 0, 14, 14, 0,    // room 15
    0, 0, 0, 0, 0, 0, 0, 0,    // room 16
    0, 0, 0, 0, 0, 0, 0, 0,    // room 17
    0, 0, 0, 0, 0, 0, 0, 0,    // room 18
    0, 0, 0, 0, 0, 0, 0, 0,    // room 19
    0, 0, 0, 0, 0, 0, 0, 0,    // room 20
    0, 0, 0, 0, 0, 0, 0, 0,    // room 21
    0, 0, 0, 0, 0, 0, 0, 0,    // room 22
    0, 0, 0, 0, 0, 0, 0, 0,    // room 23
    0, 0, 0, 0, 0, 0, 0, 0,    // room 24
    0, 0, 0, 0, 0, 0, 0, 0,    // room 25
    0, 0, 0, 0, 0, 0, 0, 0,    // room 26
    0, 0, 0, 0, 0, 0, 0, 0,    // room 27
    0, 0, 0, 0, 0, 0, 0, 0,    // room 28
    0, 0, 0, 0, 0, 0, 0, 0,    // room 29
    0, 0, 0, 0, 0, 0, 0, 0,    // room 30
    0, 0, 0, 0, 0, 0, 0, 0,    // room 31
    // stage 3
    0, 0, 0, 0, 0, 0, 0, 0,    // room  0
    17, 17, 17, 17, 17, 17, 17, 17,    // room  1
    0, 0, 0, 0, 0, 0, 0, 0,    // room  2
    0, 0, 0, 0, 0, 0, 0, 0,    // room  3
    0, 0, 0, 0, 0, 0, 0, 0,    // room  4
    0, 0, 0, 0, 0, 0, 0, 0,    // room  5
    7, 7, 7, 7, 7, 7, 7, 7,    // room  6
    0, 0, 0, 0, 0, 0, 0, 0,    // room  7
    0, 0, 0, 0, 0, 0, 0, 0,    // room  8
    0, 0, 0, 0, 0, 0, 0, 0,    // room  9
    0, 0, 0, 0, 0, 0, 0, 0,    // room 10
    0, 0, 0, 0, 0, 0, 0, 0,    // room 11
    0, 0, 0, 0, 0, 0, 0, 0,    // room 12
    0, 0, 0, 0, 0, 11, 0, 0,    // room 13
    8, 8, 8, 8, 8, 8, 8, 8,    // room 14
    0, 0, 0, 0, 0, 0, 0, 0,    // room 15
    0, 0, 0, 0, 0, 0, 0, 0,    // room 16
    0, 0, 0, 0, 0, 0, 0, 0,    // room 17
    0, 0, 0, 0, 0, 0, 0, 0,    // room 18
    0, 0, 0, 0, 0, 0, 0, 0,    // room 19
    0, 0, 0, 0, 0, 0, 0, 0,    // room 20
    0, 0, 0, 0, 0, 0, 0, 0,    // room 21
    0, 0, 0, 0, 0, 0, 0, 0,    // room 22
    0, 0, 0, 0, 0, 0, 0, 0,    // room 23
    0, 0, 0, 0, 0, 0, 0, 0,    // room 24
    0, 0, 0, 0, 0, 0, 0, 0,    // room 25
    0, 0, 0, 0, 0, 0, 0, 0,    // room 26
    0, 0, 0, 0, 0, 0, 0, 0,    // room 27
    0, 0, 0, 0, 0, 0, 0, 0,    // room 28
    0, 0, 0, 0, 0, 0, 0, 0,    // room 29
    0, 0, 0, 0, 0, 0, 0, 0,    // room 30
    0, 0, 0, 0, 0, 0, 0, 0,    // room 31
    // stage 4
    0, 0, 0, 0, 0, 0, 0, 0,    // room  0
    0, 0, 0, 0, 0, 0, 0, 0,    // room  1
    0, 0, 0, 0, 0, 0, 0, 0,    // room  2
    0, 0, 0, 0, 0, 0, 0, 0,    // room  3
    0, 0, 0, 0, 0, 0, 0, 0,    // room  4
    0, 0, 0, 0, 0, 0, 0, 0,    // room  5
    0, 0, 0, 0, 0, 0, 0, 0,    // room  6
    2, 2, 2, 2, 2, 13, 2, 2,    // room  7
    0, 0, 0, 0, 0, 0, 0, 0,    // room  8
    0, 0, 0, 0, 0, 0, 0, 0,    // room  9
    0, 0, 0, 0, 0, 0, 0, 0,    // room 10
    0, 0, 0, 0, 0, 0, 0, 0,    // room 11
    0, 0, 0, 0, 0, 0, 0, 0,    // room 12
    0, 0, 0, 0, 0, 0, 0, 0,    // room 13
    0, 0, 0, 0, 0, 0, 0, 0,    // room 14
    0, 0, 0, 0, 0, 0, 0, 0,    // room 15
    0, 0, 0, 0, 0, 0, 0, 0,    // room 16
    0, 0, 0, 0, 0, 0, 0, 0,    // room 17
    0, 0, 0, 0, 0, 0, 0, 0,    // room 18
    0, 0, 0, 0, 0, 0, 0, 0,    // room 19
    0, 0, 0, 0, 0, 0, 0, 0,    // room 20
    0, 0, 0, 0, 0, 0, 0, 0,    // room 21
    0, 0, 0, 0, 0, 0, 0, 0,    // room 22
    0, 0, 0, 0, 0, 0, 0, 0,    // room 23
    0, 0, 0, 0, 0, 0, 0, 0,    // room 24
    0, 0, 0, 0, 0, 0, 0, 0,    // room 25
    0, 0, 0, 0, 0, 0, 0, 0,    // room 26
    0, 0, 0, 0, 0, 0, 0, 0,    // room 27
    0, 0, 0, 0, 0, 0, 0, 0,    // room 28
    0, 0, 0, 0, 0, 0, 0, 0,    // room 29
    0, 0, 0, 0, 0, 0, 0, 0,    // room 30
    0, 0, 0, 0, 0, 0, 0, 0,    // room 31
};

// ============================================================================
// g_FadeSprParams (0x004c0b30): 20 records of 0x14 bytes. DrawFadeSpr reads
// the flag at +0, the sort bias at +4 and the screen offsets at +8 and +0x10;
// entity_add_fade_sprite reads the +4 bias too. +0xC is 16 in every record and
// +0x10 is 0 in every record - neither is read by any call site reached from
// the shadow path, so a non-zero +0x10 (which would force the alpha) never
// occurs in practice.
// ============================================================================
static const FadeSprParam g_FadeSprParams[20] = {
    { 0,      0,      0,  16, 0 },   //  0
    { 1,      0,      0,  16, 0 },   //  1
    { 0,   -100,      0,  16, 0 },   //  2
    { 0,   1500,      0,  16, 0 },   //  3
    { 0,   -300,      0,  16, 0 },   //  4
    { 0,      0,   -200,  16, 0 },   //  5
    { 0,     10,    -40,  16, 0 },   //  6
    { 1,     50,      0,  16, 0 },   //  7
    { 1,      0,      0,  16, 0 },   //  8
    { 0,  16000,      0,  16, 0 },   //  9
    { 0,     -2,    100,  16, 0 },   // 10
    { 0,  15000,    120,  16, 0 },   // 11
    { 0,  15000,      0,  16, 0 },   // 12
    { 0,  11500,      0,  16, 0 },   // 13
    { 0,  16600,      0,  16, 0 },   // 14
    { 0,   5000,      0,  16, 0 },   // 15
    { 0,  13800,      0,  16, 0 },   // 16
    { 1,    100,      0,  16, 0 },   // 17
    { 0,   -102,    100,  16, 0 },   // 18
    { 0,   -110,    -40,  16, 0 },   // 19
};

// ============================================================================
// fade_spr_param_index - the (stage, room, camera) lookup, with the original's
// stage folding: anything above stage 4 has 5 subtracted first.
//
// The bounds checks are port-only. The original indexes straight in; a stage or
// room past the table would read adjacent data rather than fault, which is not
// worth reproducing.
// ============================================================================
static unsigned int fade_spr_param_index(void)
{
    unsigned int stage = (unsigned int)g_stageId;
    if (g_stageId > 4) {
        stage -= 5;
    }
    unsigned int i = ((unsigned int)g_roomId + stage * 0x20) * 8 + (unsigned int)g_roomCameraId;
    if (i >= sizeof(g_FadeSprParamIndex)) {
        return 0;
    }
    unsigned int rec = g_FadeSprParamIndex[i];
    return (rec < 20) ? rec : 0;
}

// ============================================================================
// entity_add_fade_sprite (0x00456810)
// Builds the shadow's world matrix, projects its four corners for a depth, then
// copies the whole quad into the queue with a sort key.
// ============================================================================
void entity_add_fade_sprite(VECTOR* pos, short* quad, short yOffset, short angle)
{
    MATRIX local, composed;
    int discard0 = 0, discard1 = 0;

    local = g_identityMatrixData;
    local.t[1] = (int)yOffset;
    local.t[0] = (int)quad[0] + pos->x;
    local.t[2] = (int)quad[2] + pos->z;

    RotMatrixY((int)angle, &local);
    ApplyLVAndMul0Matrix(&g_RoomCameraData, &local, &composed);
    SetRotAndTransMatrix(&composed);
    SetGlobalScaledRotationMatrix(&composed);
    GetMatrixTranslation(&composed);

    unsigned int slot = (unsigned int)g_spriteAnimActive;
    unsigned int depth = (unsigned int)RotAverage4(
        (SVECTOR*)(quad + 0x2C), (SVECTOR*)(quad + 0x30),
        (SVECTOR*)(quad + 0x34), (SVECTOR*)(quad + 0x38),
        (int*)(quad + slot * 0x14 + 8),    (int*)(quad + slot * 0x14 + 0xC),
        (int*)(quad + slot * 0x14 + 0x10), (int*)(quad + slot * 0x14 + 0x14),
        &discard0, &discard1);

    const FadeSprParam* prm = &g_FadeSprParams[fade_spr_param_index()];
    short sortBase = (short)(prm->sortBias + 100);

    // The original duplicates the store block across the two branches of
    // g_main_state_flags2 bit 3; the only difference is that the depth clamp is
    // skipped when the bit is set. Folded together here.
    if ((g_main_state_flags2 & 8) == 0) {
        if (depth > 0xFEF) {
            depth = 0xFF0;
        }
    }

    if (g_FadeSprCount >= FADE_SPR_MAX) {
        return;
    }

    int i = g_FadeSprCount;
    FS_POS(i)[0] = pos->x;
    FS_POS(i)[1] = pos->y;
    FS_POS(i)[2] = pos->z;
    FS_POS(i)[3] = pos->pad;
    memcpy(FS_QUAD(i), quad, 0x1E * 4);   // 30 dwords = 120 bytes
    g_FadeSprCount++;
    FS_YOFS(i)  = yOffset;
    FS_ANGLE(i) = angle;
    FS_SORT(i)  = (unsigned short)((short)depth + sortBase);
}

// ============================================================================
// DrawFadeSpr (0x00456d30)
// Sorts the queued shadows by sort key, then re-projects and submits each one.
// Clears the queue on the way out.
//
// The sort keeps the original's shape - a selection pass swapping whole 144-byte
// records, not an index sort.
// ============================================================================
void DrawFadeSpr(void)
{
    unsigned char tmp[FADE_SPR_RECSIZE];

    for (int a = 0; a < g_FadeSprCount - 1; a++) {
        for (int b = a + 1; b < g_FadeSprCount; b++) {
            if (FS_SORT(b) < FS_SORT(a)) {
                memcpy(tmp,       FS_REC(a), FADE_SPR_RECSIZE);
                memcpy(FS_REC(a), FS_REC(b), FADE_SPR_RECSIZE);
                memcpy(FS_REC(b), tmp,       FADE_SPR_RECSIZE);
            }
        }
    }

    for (int i = 0; i < g_FadeSprCount; i++) {
        MATRIX local, composed;
        int discard0 = 0, discard1 = 0;

        short* quad = FS_QUAD(i);

        local = g_identityMatrixData;
        local.t[0] = (int)quad[0] + FS_POS(i)[0];
        local.t[2] = (int)quad[2] + FS_POS(i)[2];
        local.t[1] = (int)FS_YOFS(i) + g_FadeSprLayerStep * i;

        RotMatrixY((int)FS_ANGLE(i), &local);
        ApplyLVAndMul0Matrix(&g_RoomCameraData, &local, &composed);
        SetRotAndTransMatrix(&composed);
        SetGlobalScaledRotationMatrix(&composed);
        GetMatrixTranslation(&composed);

        unsigned int slot = (unsigned int)g_spriteAnimActive;
        unsigned int depth = (unsigned int)RotAverage4(
            (SVECTOR*)(quad + 0x2C), (SVECTOR*)(quad + 0x30),
            (SVECTOR*)(quad + 0x34), (SVECTOR*)(quad + 0x38),
            (int*)(quad + slot * 0x14 + 8),    (int*)(quad + slot * 0x14 + 0xC),
            (int*)(quad + slot * 0x14 + 0x10), (int*)(quad + slot * 0x14 + 0x14),
            &discard0, &discard1);

        const FadeSprParam* prm = &g_FadeSprParams[fade_spr_param_index()];
        unsigned short forceAlpha =
            (unsigned short)(prm->sortBias + 100 + g_FadeSprAlphaBias);

        // Three branches in the original collapse to two distinct outcomes: the
        // clamped projected depth, or forceAlpha + 1 when the record's flag is
        // set and g_main_state_flags2 bit 3 is clear.
        unsigned short alpha;
        if (prm->flag != 0 && (g_main_state_flags2 & 8) == 0) {
            alpha = (unsigned short)(forceAlpha + 1);
        } else {
            if (depth > 0xFEF) {
                depth = 0xFF0;
            }
            alpha = (unsigned short)((short)depth + forceAlpha);
        }

        // The colour is read as three bytes out of the primitive header of the
        // half g_spriteAnimActive selects. Record +0x10 is quad[0]; the original
        // biases by -8 bytes from there, landing on the position dword's tail.
        unsigned char* rgb = (unsigned char*)FS_REC(i) + 0x10
                           + (unsigned int)g_spriteAnimActive * 0x28 - 8;

        int arg_u    = (int)((float)(int)quad[0x30] * 0.0025f);
        int arg_clut = (int)((float)(int)quad[0x32] * 0.0025f);
        int arg_x    = prm->offsetX + g_FadeSprOffsetX;

        // DIAGNOSTIC: the values handed to AddFadePoly for the first sprite of a
        // frame. This is the one seam that could not be checked statically - the
        // port's AddFadePoly is a D3D11 reimplementation whose parameter names
        // (u/v/clut/x/y/z) were inferred, and the original passes a float bit
        // pattern (0x3f800000 == 1.0f) in the `v` slot. If shadows appear in the
        // wrong place, at the wrong size or not at all, compare these against what
        // the renderer does with them. Remove once verified.
        if (i == 0) {
            static int lastAlpha = -1;
            if ((int)alpha != lastAlpha) {
                lastAlpha = alpha;
                // dbg_printf("[shadow] n=%d prm=%u flag=%u alpha=%u force=%u depth=%u"
                //            " u=%d clut=%d x=%d rgb=%u,%u,%u corners=(%d,%d)(%d,%d)\n",
                //            g_FadeSprCount, fade_spr_param_index(),
                //            (unsigned int)prm->flag, (unsigned int)alpha,
                //            (unsigned int)forceAlpha, depth, arg_u, arg_clut, arg_x,
                //            (unsigned int)rgb[0], (unsigned int)rgb[1], (unsigned int)rgb[2],
                //            (int)quad[0x2C], (int)quad[0x2E],
                //            (int)quad[0x30], (int)quad[0x32]);
            }
        }

        AddFadePoly(alpha, 0, arg_u, 0x3f800000, arg_clut, rgb,
                    arg_x, 0, prm->forceFlag, forceAlpha);
    }

    g_FadeSprCount = 0;
}
