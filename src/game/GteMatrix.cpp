// GteMatrix.cpp - PS1 GTE matrix operations
// Decompiled from Ghidra
//  RotMatrix (0x004406a0)
//  FUN_0040ab80 (0x0040ab80)
//  SetGlobalScaledRotationMatrix (0x0040a9e0)
//  get_matrix_t (0x0040a9c0)
#include "../Globals.h"
#include <cmath>

// ============================================================================
// GTE trig lookup tables (standard PS1 12-bit angle precision)
// ============================================================================
static short g_sinTable[4096];
static short g_cosTable[4096];
static bool g_trigTablesInitialized = false;

static void InitTrigTables(void)
{
    if (g_trigTablesInitialized) return;
    for (int i = 0; i < 4096; i++) {
        double angle = (double)i * 6.283185307179586 / 4096.0;
        g_cosTable[i] = (short)(cos(angle) * 4096.0 + 0.5);
        g_sinTable[i] = (short)(sin(angle) * 4096.0 + 0.5);
    }
    g_trigTablesInitialized = true;
}

// ============================================================================
// GTE sin/cos lookups (0x00440a10 / 0x004409f0)
// ============================================================================
int GteSin(int angle)
{
    return g_sinTable[angle & 0xFFF];
}

int GteCos(int angle)
{
    return g_cosTable[angle & 0xFFF];
}

// ============================================================================
// GteRotationMatrixCalc (0x004406a0)
// Compute rotation matrix components from Euler angles (12-bit fixed)
// ============================================================================
static void GteRotationMatrixCalc(int sx, int sy, int sz, int* result)
{
    InitTrigTables();

    int iVar1, iVar2, iVar3, iVar4, iVar5;

    // result[0] = sin(sz) * sin(sy)  (or cos equivalent)
    iVar1 = GteSin(sz);
    iVar2 = GteSin(sy);
    result[0] = (int)(iVar1 * iVar2 + (iVar1 * iVar2 >> 0x1f & 0x3fffU)) >> 0xe;

    // result[1] = -sin(sy) * cos(sz)
    iVar1 = GteSin(sy);
    iVar2 = GteCos(sz);
    result[1] = -((int)(iVar1 * iVar2 + (iVar1 * iVar2 >> 0x1f & 0x3fffU)) >> 0xe);

    // result[2] = cos(sy)
    iVar1 = GteCos(sy);
    result[2] = iVar1;

    // result[3] = sin(sx) * cos(sy) * sin(sz) + cos(sx) * cos(sz)  (adjusted)
    iVar1 = GteSin(sx);
    iVar2 = GteCos(sy);
    iVar3 = GteSin(sz);
    iVar3 = ((int)(iVar1 * iVar2 + (iVar1 * iVar2 >> 0x1f & 0x3fffU)) >> 0xe) * iVar3;
    iVar1 = GteCos(sx);
    iVar2 = GteCos(sz);
    result[3] = ((int)(iVar3 + (iVar3 >> 0x1f & 0x3fffU)) >> 0xe) +
                ((int)(iVar1 * iVar2 + (iVar1 * iVar2 >> 0x1f & 0x3fffU)) >> 0xe);

    // result[4] = cos(sx) * cos(sz) - sin(sx) * cos(sy) * sin(sz)
    iVar1 = GteCos(sx);
    iVar2 = GteCos(sz);
    iVar3 = GteSin(sx);
    iVar4 = GteCos(sy);
    iVar5 = GteSin(sz);
    iVar5 = ((int)(iVar3 * iVar4 + (iVar3 * iVar4 >> 0x1f & 0x3fffU)) >> 0xe) * iVar5;
    result[4] = ((int)(iVar1 * iVar2 + (iVar1 * iVar2 >> 0x1f & 0x3fffU)) >> 0xe) -
                ((int)(iVar5 + (iVar5 >> 0x1f & 0x3fffU)) >> 0xe);

    // result[5] = -sin(sx) * sin(sy)
    iVar1 = GteSin(sx);
    iVar2 = GteSin(sy);
    result[5] = -((int)(iVar1 * iVar2 + (iVar1 * iVar2 >> 0x1f & 0x3fffU)) >> 0xe);

    // result[6] = cos(sx) * cos(sz) - sin(sx) * cos(sz) * sin(sy)
    iVar1 = GteSin(sx);
    iVar2 = GteCos(sz);
    iVar3 = GteCos(sx);
    iVar4 = GteSin(sy);
    iVar5 = GteCos(sz);
    iVar5 = ((int)(iVar3 * iVar4 + (iVar3 * iVar4 >> 0x1f & 0x3fffU)) >> 0xe) * iVar5;
    result[6] = ((int)(iVar1 * iVar2 + (iVar1 * iVar2 >> 0x1f & 0x3fffU)) >> 0xe) -
                ((int)(iVar5 + (iVar5 >> 0x1f & 0x3fffU)) >> 0xe);

    // result[7] = sin(sx) * cos(sz) + cos(sx) * sin(sy) * sin(sz)
    iVar1 = GteSin(sz);
    iVar2 = GteCos(sy);
    iVar3 = GteCos(sx);
    iVar3 = ((int)(iVar1 * iVar2 + (iVar1 * iVar2 >> 0x1f & 0x3fffU)) >> 0xe) * iVar3;
    iVar1 = GteSin(sx);
    iVar2 = GteCos(sz);
    result[7] = ((int)(iVar3 + (iVar3 >> 0x1f & 0x3fffU)) >> 0xe) +
                ((int)(iVar1 * iVar2 + (iVar1 * iVar2 >> 0x1f & 0x3fffU)) >> 0xe);

    // result[8] = cos(sx) * sin(sy)
    iVar1 = GteCos(sx);
    iVar2 = GteSin(sy);
    result[8] = (int)(iVar1 * iVar2 + (iVar1 * iVar2 >> 0x1f & 0x3fffU)) >> 0xe;
}

// ============================================================================
// RotMatrix (0x004406a0)
// Builds a 3x3 rotation matrix from a PS1 SVECTOR rotation (12-bit angles)
// ============================================================================
MATRIX* RotMatrix(SVECTOR* r, MATRIX* m)
{
    int result[9];

    GteRotationMatrixCalc(0x1000 - r->x, (int)r->y, 0x1000 - r->z, result);

    m->m[0][0] = (short)((int)(result[0] + (result[0] >> 0x1f & 3U)) >> 2);
    m->m[0][1] = (short)((int)(result[1] + (result[1] >> 0x1f & 3U)) >> 2);
    m->m[0][2] = (short)((int)(result[2] + (result[2] >> 0x1f & 3U)) >> 2);
    m->m[1][0] = (short)((int)(result[3] + (result[3] >> 0x1f & 3U)) >> 2);
    m->m[1][1] = (short)((int)(result[4] + (result[4] >> 0x1f & 3U)) >> 2);
    m->m[1][2] = (short)((int)(result[5] + (result[5] >> 0x1f & 3U)) >> 2);
    m->m[2][0] = (short)((int)(result[6] + (result[6] >> 0x1f & 3U)) >> 2);
    m->m[2][1] = (short)((int)(result[7] + (result[7] >> 0x1f & 3U)) >> 2);
    m->m[2][2] = (short)((int)(result[8] + (result[8] >> 0x1f & 3U)) >> 2);

    return m;
}

// ============================================================================
// MatrixSetTranslation (0x0040ab80)
// Copies translation vector into the matrix struct
// ============================================================================
void MatrixSetTranslation(MATRIX* m, int* translation)
{
    m->t[0] = translation[0];
    m->t[1] = translation[1];
    m->t[2] = translation[2];
}

// ============================================================================
// GTE fixed-point pipe globals
// ============================================================================
int g_fixedPointPipe_matrix_m00 = 0;  // 0x004c3790
int g_fixedPointPipe_matrix_m01 = 0;  // 0x004c3794
int g_fixedPointPipe_matrix_m02 = 0;  // 0x004c3798
int g_fixedPointPipe_matrix_m10 = 0;  // 0x004c379c
int g_fixedPointPipe_matrix_m11 = 0;  // 0x004c37a0
int g_fixedPointPipe_matrix_m12 = 0;  // 0x004c37a4
int g_fixedPointPipe_matrix_m20 = 0;  // 0x004c37a8
int g_fixedPointPipe_matrix_m21 = 0;  // 0x004c37ac
int g_fixedPointPipe_matrix_m22 = 0;  // 0x004c37b0
int matrix_t0 = 0;                    // 0x004c37b8
int matrix_t1 = 0;                    // 0x004c37bc
int matrix_t2 = 0;                    // 0x004c37c0

// ============================================================================
// SetGlobalScaledRotationMatrix (0x0040a9e0)
// Copies matrix to the global fixed-point pipe (with 2-bit shift for scaling)
// ============================================================================
void SetGlobalScaledRotationMatrix(MATRIX* m)
{
    g_fixedPointPipe_matrix_m00 = (int)m->m[0][0] << 2;
    g_fixedPointPipe_matrix_m01 = (int)m->m[0][1] << 2;
    g_fixedPointPipe_matrix_m02 = (int)m->m[0][2] << 2;
    g_fixedPointPipe_matrix_m10 = (int)m->m[1][0] << 2;
    g_fixedPointPipe_matrix_m11 = (int)m->m[1][1] << 2;
    g_fixedPointPipe_matrix_m12 = (int)m->m[1][2] << 2;
    g_fixedPointPipe_matrix_m20 = (int)m->m[2][0] << 2;
    g_fixedPointPipe_matrix_m21 = (int)m->m[2][1] << 2;
    g_fixedPointPipe_matrix_m22 = (int)m->m[2][2] << 2;
}

// ============================================================================
// GetMatrixTranslation (0x0040a9c0)
// Copies translation from matrix to globals
// ============================================================================
void GetMatrixTranslation(MATRIX* m)
{
    matrix_t0 = m->t[0];
    matrix_t1 = m->t[1];
    matrix_t2 = m->t[2];
}

// ============================================================================
// GteSpriteHeaderInit (0x0040abc0)
// Initializes a PS1-style sprite primitive header
// ============================================================================
void GteSpriteHeaderInit(SVECTOR* header)
{
    *(unsigned char*)((int)&header->pad + 1) = 44;
    unsigned int packed = (unsigned short)header->x | ((unsigned short)header->y << 16);
    packed = (packed & 0xFFFFFF) | 0x09000000;
    header->x = (short)(packed & 0xFFFF);
    header->y = (short)((packed >> 16) & 0xFFFF);
}

// ============================================================================
// GteClutBuild (0x0040ac00)
// Builds a PS1 CLUT reference value
// ============================================================================
int GteClutBuild(int param_1, short param_2)
{
    param_1 = param_1 + (param_1 >> 0x1f & 0xfU);
    return (int)((short)((unsigned short)(param_1 >> 4) & 0x3f ^ (unsigned short)param_2 << 6) |
                 ((param_1 >> 0x14) << 16));
}

// ============================================================================
// GteTpageBuild (0x0040ac30)
// Builds a PS1 tpage/depth reference value
// ============================================================================
int GteTpageBuild(unsigned short param_1, unsigned short param_2, int param_3, int param_4)
{
    param_3 = param_3 + (param_3 >> 0x1f & 0x3fU);
    return (int)((short)((param_1 & 3) * 0x80 +
                         (short)((param_4 + (param_4 >> 0x1f & 0xffU)) >> 8) * 0x10 +
                         (param_2 & 3) * 0x20 +
                         (short)(param_3 >> 6)) |
                 ((param_3 >> 0x16) << 16));
}
