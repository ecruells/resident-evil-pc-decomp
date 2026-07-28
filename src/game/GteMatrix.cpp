// GteMatrix.cpp - PS1 GTE matrix operations
// Decompiled from Ghidra
//  RotMatrix (0x004406a0)
//  RotMatrixY (0x00409aa0)
//  ApplyMatrix (0x00409cd0)
//  ApplyMatrixSV (0x00409db0)
//  FUN_0040ab80 (0x0040ab80)
//  SetGlobalScaledRotationMatrix (0x0040a9e0)
//  get_matrix_t (0x0040a9c0)
#include "../Globals.h"
#include <cmath>
#include <cstdio>
#include <windows.h>

// ============================================================================
// GTE trig lookup tables (standard PS1 12-bit angle precision)
// ============================================================================
static short g_sinTable[4096];
static short g_cosTable[4096];
static bool g_trigTablesInitialized = false;

static void InitTrigTables(void)
{
    if (g_trigTablesInitialized) return;

    // FUN_00440a30: both tables hold 4096 entries at 14-bit amplitude
    // (fsin/fcos * 16384.0, angle step 2*pi/4096 = 0.0015339807880859375),
    // saturated to +/-0x3FFF so a value never reads back as -1.0 exactly.
    //
    // The amplitude matters: GteRotationMatrixCalc combines two table lookups
    // with >> 14 and RotMatrix shifts the result down by 2 more to reach the
    // 4.12 matrix scale. Filling the tables at 4096 instead made every
    // rotation matrix 4-16x too small, and since each joint composes with its
    // parent the error compounded - deep joints ended up with an all-zero
    // rotation, collapsing every vertex of the model onto a single point.
    for (int i = 0; i < 4096; i++) {
        double angle = (double)i * 0.0015339807880859375;
        int s = (int)(sin(angle) * 16384.0);
        int c = (int)(cos(angle) * 16384.0);
        if (s ==  0x4000) s =  0x3FFF;
        if (s == -0x4000) s = -0x3FFF;
        if (c ==  0x4000) c =  0x3FFF;
        if (c == -0x4000) c = -0x3FFF;
        g_sinTable[i] = (short)s;
        g_cosTable[i] = (short)c;
    }
    g_trigTablesInitialized = true;
}

// ============================================================================
// GTE sin/cos lookups (0x004409f0 / 0x00440a10)
// ============================================================================
// Verified against the original's disassembly:
//   FUN_00440a30 allocates 0x004bcacc and fills it with fsin, then allocates
//   0x004bcad0 and fills it with fcos.
//   0x004409f0 reads [0x004bcacc] -> sine.
//   0x00440a10 reads [0x004bcad0] -> cosine.
// (The Ghidra symbols for these two were originally attached the other way
// round; both the names and every call site below have been corrected, so
// GteSin() is sine and GteCos() is cosine throughout.)
int GteSin(int angle)
{
    return g_sinTable[angle & 0xFFF];   // 0x004409f0 -> sin table (0x004bcacc)
}

int GteCos(int angle)
{
    return g_cosTable[angle & 0xFFF];   // 0x00440a10 -> cos table (0x004bcad0)
}

// ============================================================================
// GteRotationMatrixCalc (0x004406a0)
// Compute rotation matrix components from Euler angles (12-bit fixed)
// ============================================================================
static void GteRotationMatrixCalc(int sx, int sy, int sz, int* result)
{
    InitTrigTables();

    int iVar1, iVar2, iVar3, iVar4, iVar5;

    // result[0] = cos(sz) * cos(sy)
    iVar1 = GteCos(sz);
    iVar2 = GteCos(sy);
    result[0] = (int)(iVar1 * iVar2 + (iVar1 * iVar2 >> 0x1f & 0x3fffU)) >> 0xe;

    // result[1] = -cos(sy) * sin(sz)
    iVar1 = GteCos(sy);
    iVar2 = GteSin(sz);
    result[1] = -((int)(iVar1 * iVar2 + (iVar1 * iVar2 >> 0x1f & 0x3fffU)) >> 0xe);

    // result[2] = sin(sy)
    iVar1 = GteSin(sy);
    result[2] = iVar1;

    // result[3] = sin(sx)*sin(sy)*cos(sz) + cos(sx)*sin(sz)
    iVar1 = GteSin(sx);
    iVar2 = GteSin(sy);
    iVar3 = GteCos(sz);
    iVar3 = ((int)(iVar1 * iVar2 + (iVar1 * iVar2 >> 0x1f & 0x3fffU)) >> 0xe) * iVar3;
    iVar1 = GteCos(sx);
    iVar2 = GteSin(sz);
    result[3] = ((int)(iVar3 + (iVar3 >> 0x1f & 0x3fffU)) >> 0xe) +
                ((int)(iVar1 * iVar2 + (iVar1 * iVar2 >> 0x1f & 0x3fffU)) >> 0xe);

    // result[4] = cos(sx)*cos(sz) - sin(sx)*sin(sz)*sin(sy)
    iVar1 = GteCos(sx);
    iVar2 = GteCos(sz);
    iVar3 = GteSin(sx);
    iVar4 = GteSin(sz);
    iVar5 = GteSin(sy);
    iVar5 = ((int)(iVar3 * iVar4 + (iVar3 * iVar4 >> 0x1f & 0x3fffU)) >> 0xe) * iVar5;
    result[4] = ((int)(iVar1 * iVar2 + (iVar1 * iVar2 >> 0x1f & 0x3fffU)) >> 0xe) -
                ((int)(iVar5 + (iVar5 >> 0x1f & 0x3fffU)) >> 0xe);

    // result[5] = -(sin(sx)*cos(sy))
    iVar1 = GteSin(sx);
    iVar2 = GteCos(sy);
    result[5] = -((int)(iVar1 * iVar2 + (iVar1 * iVar2 >> 0x1f & 0x3fffU)) >> 0xe);

    // result[6] = sin(sx)*sin(sz) - cos(sx)*sin(sy)*cos(sz)
    iVar1 = GteSin(sx);
    iVar2 = GteSin(sz);
    iVar3 = GteCos(sx);
    iVar4 = GteSin(sy);
    iVar5 = GteCos(sz);
    iVar5 = ((int)(iVar3 * iVar4 + (iVar3 * iVar4 >> 0x1f & 0x3fffU)) >> 0xe) * iVar5;
    result[6] = ((int)(iVar1 * iVar2 + (iVar1 * iVar2 >> 0x1f & 0x3fffU)) >> 0xe) -
                ((int)(iVar5 + (iVar5 >> 0x1f & 0x3fffU)) >> 0xe);

    // result[7] = sin(sz)*sin(sy)*cos(sx) + sin(sx)*cos(sz)
    iVar1 = GteSin(sz);
    iVar2 = GteSin(sy);
    iVar3 = GteCos(sx);
    iVar3 = ((int)(iVar1 * iVar2 + (iVar1 * iVar2 >> 0x1f & 0x3fffU)) >> 0xe) * iVar3;
    iVar1 = GteSin(sx);
    iVar2 = GteCos(sz);
    result[7] = ((int)(iVar3 + (iVar3 >> 0x1f & 0x3fffU)) >> 0xe) +
                ((int)(iVar1 * iVar2 + (iVar1 * iVar2 >> 0x1f & 0x3fffU)) >> 0xe);

    // result[8] = cos(sx)*cos(sy)
    iVar1 = GteCos(sx);
    iVar2 = GteCos(sy);
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

// ============================================================================
// GteFixedMul12 (helper)
// 4.12 fixed-point multiply: (a * b) >> 12 with rounding toward zero
// ============================================================================
static inline int GteFixedMul12(int a, int b)
{
    int val = a * b;
    return (val + (val >> 31 & 0xFFF)) >> 12;
}

// ============================================================================
// RotMatrixY (0x00409aa0)
// Rotates matrix m around the Y axis by PS1 12-bit angle r.
// m = Ry(r) * m
// The original scales m up by 4, calls GteRotationMatrixYXZ(0, r, 0) and
// composes, then scales back down by 4; this is the equivalent closed form.
// ============================================================================
MATRIX* RotMatrixY(int r, MATRIX* m)
{
    InitTrigTables();

    int sinR = GteSin(r);
    int cosR = GteCos(r);

    short r00 = m->m[0][0], r01 = m->m[0][1], r02 = m->m[0][2];
    short r20 = m->m[2][0], r21 = m->m[2][1], r22 = m->m[2][2];

    // [cos  0  sin]   [r00 r01 r02]   [cos*r00+sin*r20  cos*r01+sin*r21  cos*r02+sin*r22]
    // [  0  1    0] * [r10 r11 r12] = [      r10              r11              r12       ]
    // [-sin 0  cos]   [r20 r21 r22]   [-sin*r00+cos*r20 -sin*r01+cos*r21 -sin*r02+cos*r22]
    m->m[0][0] = (short)(GteFixedMul12(cosR, r00) + GteFixedMul12(sinR, r20));
    m->m[0][1] = (short)(GteFixedMul12(cosR, r01) + GteFixedMul12(sinR, r21));
    m->m[0][2] = (short)(GteFixedMul12(cosR, r02) + GteFixedMul12(sinR, r22));

    m->m[2][0] = (short)(GteFixedMul12(-sinR, r00) + GteFixedMul12(cosR, r20));
    m->m[2][1] = (short)(GteFixedMul12(-sinR, r01) + GteFixedMul12(cosR, r21));
    m->m[2][2] = (short)(GteFixedMul12(-sinR, r02) + GteFixedMul12(cosR, r22));

    return m;
}

// ============================================================================
// ApplyMatrix (0x00409cd0)
// Applies rotation matrix to vector (PS1 GTE convention: Y-negated I/O)
// result = M * v (with PS1 Y-axis handling)
// ============================================================================
void ApplyMatrix(MATRIX* m, SVECTOR* v0, SVECTOR* v1)
{
    int vx = (int)v0->x;
    int vy = -(int)v0->y;  // PS1 Y negation
    int vz = (int)v0->z;

    int rx = (int)m->m[0][0] * vx + (int)m->m[0][1] * vy + (int)m->m[0][2] * vz;
    int ry = (int)m->m[1][0] * vx + (int)m->m[1][1] * vy + (int)m->m[1][2] * vz;
    int rz = (int)m->m[2][0] * vx + (int)m->m[2][1] * vy + (int)m->m[2][2] * vz;

    // Shift right by 12 (4.12 fixed-point) with rounding toward zero
    rx = (rx + (rx >> 31 & 0xFFF)) >> 12;
    ry = (ry + (ry >> 31 & 0xFFF)) >> 12;
    rz = (rz + (rz >> 31 & 0xFFF)) >> 12;

    v1->x = (short)rx;
    v1->y = (short)(-ry);  // PS1 Y negation
    v1->z = (short)rz;
}

// ============================================================================
// ApplyMatrixSV (0x00409db0)
// Applies rotation matrix to SVECTOR, storing result as SVECTOR.
// PS1 GTE convention: Y components are negated on input and output.
// ============================================================================
void ApplyMatrixSV(MATRIX* m, SVECTOR* src, SVECTOR* dst)
{
    int vx = (int)src->x;
    int vy = -(int)src->y;  // PS1 Y negation
    int vz = (int)src->z;

    int rx = (int)m->m[0][0] * vx + (int)m->m[0][1] * vy + (int)m->m[0][2] * vz;
    int ry = (int)m->m[1][0] * vx + (int)m->m[1][1] * vy + (int)m->m[1][2] * vz;
    int rz = (int)m->m[2][0] * vx + (int)m->m[2][1] * vy + (int)m->m[2][2] * vz;

    rx = (rx + (rx >> 31 & 0xFFF)) >> 12;
    ry = (ry + (ry >> 31 & 0xFFF)) >> 12;
    rz = (rz + (rz >> 31 & 0xFFF)) >> 12;

    dst->x = (short)rx;
    dst->y = (short)(-ry);  // PS1 Y negation
    dst->z = (short)rz;
}

// ============================================================================
// fp_lerp (0x0040a3b0)
// Fixed-point SVECTOR interpolation: out = (current * wCur + target * wTgt) >> 12
// ============================================================================
void fp_lerp(SVECTOR* current, SVECTOR* target, int weightCurrent, int weightTarget, SVECTOR* out)
{
    int t, c;

    t = target->x * weightTarget;
    c = current->x * weightCurrent;
    out->x = (short)(((t + (t >> 31 & 0xFFF)) >> 12) + ((c + (c >> 31 & 0xFFF)) >> 12));

    t = target->y * weightTarget;
    c = current->y * weightCurrent;
    out->y = (short)(((t + (t >> 31 & 0xFFF)) >> 12) + ((c + (c >> 31 & 0xFFF)) >> 12));

    t = target->z * weightTarget;
    c = current->z * weightCurrent;
    out->z = (short)(((t + (t >> 31 & 0xFFF)) >> 12) + ((c + (c >> 31 & 0xFFF)) >> 12));
}

// ============================================================================
// MulMatrix0 (0x00409fb0)
// 3x3 matrix multiply: m2 = m0 * m1 (rotation only, 4.12 fixed-point)
// ============================================================================
MATRIX* MulMatrix0(MATRIX* m0, MATRIX* m1, MATRIX* m2)
{
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            int val = GteFixedMul12((int)m0->m[i][0], (int)m1->m[0][j]) +
                      GteFixedMul12((int)m0->m[i][1], (int)m1->m[1][j]) +
                      GteFixedMul12((int)m0->m[i][2], (int)m1->m[2][j]);
            m2->m[i][j] = (short)val;
        }
    }
    return m2;
}

// ============================================================================
// ApplyMatrixLV (0x00409bc0)
// Matrix-vector multiply for translation: v1 = Rotate(m0, v0)
// Rotation-only transform with PS1 Y-axis negation.
// ============================================================================
VECTOR* ApplyMatrixLV(MATRIX* m, VECTOR* v0, VECTOR* v1)
{
    int vx = v0->x;
    int vy = -v0->y;  // PS1 Y negation
    int vz = v0->z;

    int rx = GteFixedMul12((int)m->m[0][0], vx) + GteFixedMul12((int)m->m[0][1], vy) + GteFixedMul12((int)m->m[0][2], vz);
    int ry = GteFixedMul12((int)m->m[1][0], vx) + GteFixedMul12((int)m->m[1][1], vy) + GteFixedMul12((int)m->m[1][2], vz);
    int rz = GteFixedMul12((int)m->m[2][0], vx) + GteFixedMul12((int)m->m[2][1], vy) + GteFixedMul12((int)m->m[2][2], vz);

    v1->x = rx;
    v1->y = -ry;  // PS1 Y negation
    v1->z = rz;
    return v1;
}

// ============================================================================
// CompMatrix (0x0040a190)
// Full matrix composition: m2 = m0 * m1 (rotation + translation)
// ============================================================================
MATRIX* CompMatrix(MATRIX* m0, MATRIX* m1, MATRIX* m2)
{
    MATRIX tmp;
    MulMatrix0(m0, m1, &tmp);
    ApplyMatrixLV(m0, (VECTOR*)m1->t, (VECTOR*)tmp.t);
    tmp.t[0] += m0->t[0];
    tmp.t[1] += m0->t[1];
    tmp.t[2] += m0->t[2];
    *m2 = tmp;
    return m2;
}

// ============================================================================
// ApplyLVAndMulMatrix (0x0040a230)
// In-place matrix composition: m0 = m0 * m1
// ============================================================================
void ApplyLVAndMulMatrix(MATRIX* m0, MATRIX* m1)
{
    CompMatrix(m0, m1, m0);
}

// ============================================================================
// FUN_004403c0 (0x004403c0)
// Euler angle to rotation matrix helper.
// Computes a 9-element rotation matrix from 3 angles (param_1, param_2, param_3)
// and stores the result in param_4[0..8] (3x3 row-major, 4.12 fixed-point).
// ============================================================================
void FUN_004403c0(int param_1, int param_2, int param_3, int* param_4) // 0x004403c0
{
    int sum23 = param_3 + param_2;
    int dif23 = param_2 - param_3;
    int c_sum = GteCos(sum23);
    int c_dif = GteCos(dif23);
    int s1 = GteSin(param_1);
    int tmp = ((c_sum - c_dif) / 2) * s1;
    c_sum = GteCos(sum23);
    c_dif = GteCos(dif23);
    param_4[0] = ((tmp + (tmp >> 0x1f & 0x3fffU)) >> 0xe) + (c_dif + c_sum) / 2;

    int ss1p3 = GteSin(param_3 + param_1);
    int sm1p3 = GteSin(param_1 - param_3);
    int sum12 = param_1 + param_2;
    int dif12 = param_2 - param_1;
    param_4[1] = (ss1p3 - sm1p3) / 2;

    int s_sum12 = GteSin(sum12);
    int s_dif12 = GteSin(dif12);
    int s3 = GteSin(param_3);
    tmp = ((s_sum12 - s_dif12) / 2) * s3;
    int s_dif23 = GteSin(dif23);
    int s_sum23b = GteSin(sum23);
    param_4[2] = ((tmp + (tmp >> 0x1f & 0x3fffU)) >> 0xe) + (s_sum23b + s_dif23) / 2;

    int c_sum12 = GteCos(sum12);
    int c_dif12 = GteCos(dif12);
    int c3 = GteCos(param_3);
    tmp = ((c_sum12 - c_dif12) / 2) * c3;
    s_dif23 = GteSin(dif23);
    s_sum23b = GteSin(sum23);
    param_4[3] = ((tmp + (tmp >> 0x1f & 0x3fffU)) >> 0xe) + (s_dif23 - s_sum23b) / 2;

    int cp1p3 = GteCos(param_3 + param_1);
    int cm1p3 = GteCos(param_1 - param_3);
    param_4[4] = (cm1p3 + cp1p3) / 2;

    s_sum12 = GteSin(sum12);
    s_dif12 = GteSin(dif12);
    c3 = GteCos(param_3);
    tmp = ((s_sum12 - s_dif12) / 2) * c3;
    int c_sum23b = GteCos(sum23);
    int c_dif23 = GteCos(dif23);
    param_4[5] = ((tmp + (tmp >> 0x1f & 0x3fffU)) >> 0xe) + (c_sum23b - c_dif23) / 2;

    int sm1m2 = GteSin(param_1 - param_2);
    int sp1p2 = GteSin(sum12);
    param_4[6] = (sm1m2 - sp1p2) / 2;

    int s1b = GteSin(param_1);
    param_4[7] = -s1b;

    int cm1m2 = GteCos(param_1 - param_2);
    int cp1p2 = GteCos(sum12);
    param_4[8] = (cp1p2 + cm1m2) / 2;
}

// ============================================================================
// MatrixToCamera (0x0040a680)
// Computes a camera view matrix from position/orientation data and stores
// it in g_RoomCameraData (0x004bca88). Used during room/camera transitions.
// Input: pointer to camera from/to position data (ints: from_xyz, to_xyz,
// roll, ...) — also called with a MATRIX* by options_menu.
//
// Rotation formulas (recovered from the original's FPU code):
//   dx = to_x-from_x, dy = to_y-from_y, dz = to_z-from_z
//   len = sqrt(dx²+dy²+dz²), h = sqrt(dx²+dz²)
//   m[0] = ( dz/h,          0,        -dx/len )
//   m[1] = ( dx*dy/len²,    h/len,     dy*dz/(h*len) )
//   m[2] = ( dx/len,       -dy/len,    dz/len )
// (The mixed len² / h*len denominators and the negated m[2].y are quirks of
// the original Capcom code, verified by instruction-level tracing.)
// Translation: t = R * (-from), then (when roll != 0) a roll rotation is
// composed on top via EulerToRotationMatrix(0,0,roll).
// ============================================================================
int MatrixToCamera(MATRIX* m) // 0x0040a680
{
    int camData[8];
    int* src = (int*)m;
    for (int i = 0; i < 8; i++) {
        camData[i] = src[i];
    }

    int fromX = camData[0];
    int fromY = camData[1];
    int fromZ = camData[2];

    double dx = (double)(camData[3] - fromX);
    double dy = (double)(camData[4] - fromY);
    double dz = (double)(camData[5] - fromZ);

    double len = sqrt(dx * dx + dy * dy + dz * dz);
    if (len < 1.0) len = 1.0;
    double h = sqrt(dx * dx + dz * dz);
    if (h < 1.0) h = 1.0;   // original divides by h; clamp to avoid NaN

    MATRIX* camMatrix = (MATRIX*)&g_RoomCameraData;
    camMatrix->m[0][0] = (short)(int)( dz / h * 4096.0);
    camMatrix->m[0][1] = 0;
    camMatrix->m[0][2] = (short)(int)(-dx / len * 4096.0);
    camMatrix->m[1][0] = (short)(int)( dx * dy / (len * len) * 4096.0);
    camMatrix->m[1][1] = (short)(int)( h / len * 4096.0);
    camMatrix->m[1][2] = (short)(int)( dy * dz / (h * len) * 4096.0);
    camMatrix->m[2][0] = (short)(int)( dx / len * 4096.0);
    camMatrix->m[2][1] = (short)(int)(-dy / len * 4096.0);
    camMatrix->m[2][2] = (short)(int)( dz / len * 4096.0);

    // Translation: rotated camera position (-from)
    VECTOR camPos;
    camPos.x = -fromX;
    camPos.y = -fromY;
    camPos.z = -fromZ;
    ApplyMatrixLV(camMatrix, &camPos, (VECTOR*)camMatrix->t);

    return 0;
}

// ============================================================================
// MulMatrix (0x0040a150)
// In-place matrix multiplication: m0 = m0 * m1
// ============================================================================
MATRIX* MulMatrix(MATRIX* m0, MATRIX* m1)
{
    MulMatrix0(m0, m1, m0);
    return m0;
}

// ============================================================================
// ScaleMatrixCols (0x0040a2a0)
// Scale each column of a 3x3 rotation matrix by the corresponding component
// of a VECTOR. Fixed-point: multiply then shift right by 12 (divide by 4096).
// ============================================================================
void ScaleMatrixCols(MATRIX* m, VECTOR* scale)
{
    short* p = &m->m[0][0];
    int sx = scale->x;
    int sy = scale->y;
    int sz = scale->z;

    // Column 0: m[0][0], m[1][0], m[2][0] scaled by sx
    p[0] = (short)((p[0] * sx + ((p[0] * sx >> 31) & 0xFFF)) >> 12);
    p[3] = (short)((p[3] * sx + ((p[3] * sx >> 31) & 0xFFF)) >> 12);
    p[6] = (short)((p[6] * sx + ((p[6] * sx >> 31) & 0xFFF)) >> 12);

    // Column 1: m[0][1], m[1][1], m[2][1] scaled by sy
    p[1] = (short)((p[1] * sy + ((p[1] * sy >> 31) & 0xFFF)) >> 12);
    p[4] = (short)((p[4] * sy + ((p[4] * sy >> 31) & 0xFFF)) >> 12);
    p[7] = (short)((p[7] * sy + ((p[7] * sy >> 31) & 0xFFF)) >> 12);

    // Column 2: m[0][2], m[1][2], m[2][2] scaled by sz
    p[2] = (short)((p[2] * sz + ((p[2] * sz >> 31) & 0xFFF)) >> 12);
    p[5] = (short)((p[5] * sz + ((p[5] * sz >> 31) & 0xFFF)) >> 12);
    p[8] = (short)((p[8] * sz + ((p[8] * sz >> 31) & 0xFFF)) >> 12);
}

// ============================================================================
// GteRotationMatrixYXZ (0x00440b70)
// Compute full rotation matrix from YXZ Euler angles (12-bit fixed).
// Helper for RotMatrixYXZ.
//
// The output is an int[9] (row-major 3x3) at 14-bit amplitude, NOT a MATRIX -
// the original writes nine consecutive dwords at result+0x00..+0x20. Confirmed
// two ways: RotMatrixYXZ (0x00409ed0) reads dwords at [esp+4..esp+0x24] and
// packs them into the nine shorts at m+0..m+0x10, and the matrix multiply at
// 0x00440e80 indexes the same +0x00/+0x04/+0x08 .. +0x20 rows.
//
// With RotMatrixYXZ's argument negation this yields D * Ry(y)Rx(x)Rz(z) * D
// where D = diag(1,-1,1) - the GTE Y-negation baked into the matrix, matching
// the Y handling in ApplyMatrix/ApplyMatrixLV.
// ============================================================================
void GteRotationMatrixYXZ(int x, int y, int z, int* result)
{
    int xMinusY = x - y;
    int xPlusY = x + y;
    int zPlusY = z + y;
    int yMinusZ = y - z;

    int cosXmY = GteCos(xMinusY);
    int cosXpY = GteCos(xPlusY);
    int sinZ = GteSin(z);
    int temp = ((cosXmY - cosXpY) / 2) * sinZ;
    int cosZpY = GteCos(zPlusY);
    int cosYmZ = GteCos(yMinusZ);
    result[0] = ((temp + ((temp >> 31) & 0x3FFF)) >> 14) + (cosYmZ + cosZpY) / 2;

    cosXmY = GteCos(xMinusY);
    cosXpY = GteCos(xPlusY);
    int cosZ = GteCos(z);
    temp = ((cosXmY - cosXpY) / 2) * cosZ;
    int sinYmZ = GteSin(yMinusZ);
    int sinZpY = GteSin(zPlusY);
    result[1] = ((temp + ((temp >> 31) & 0x3FFF)) >> 14) + (sinYmZ - sinZpY) / 2;

    int sinXpY = GteSin(xPlusY);
    int sinXmY = GteSin(xMinusY);
    result[2] = (sinXpY - sinXmY) / 2;

    int sinZpX = GteSin(z + x);
    int sinXmZ = GteSin(x - z);
    result[3] = (sinZpX - sinXmZ) / 2;

    int cosZpX = GteCos(z + x);
    int cosXmZ = GteCos(x - z);
    result[4] = (cosXmZ + cosZpX) / 2;

    int sinX = GteSin(x);
    result[5] = -sinX;

    sinXmY = GteSin(xMinusY);
    sinXpY = GteSin(xPlusY);
    sinZ = GteSin(z);
    temp = ((sinXpY + sinXmY) / 2) * sinZ;
    sinZpY = GteSin(zPlusY);
    sinYmZ = GteSin(yMinusZ);
    result[6] = ((temp + ((temp >> 31) & 0x3FFF)) >> 14) - (sinZpY + sinYmZ) / 2;

    sinXmY = GteSin(xMinusY);
    sinXpY = GteSin(xPlusY);
    cosZ = GteCos(z);
    temp = ((sinXpY + sinXmY) / 2) * cosZ;
    cosYmZ = GteCos(yMinusZ);
    int cosZpY2 = GteCos(zPlusY);
    result[7] = ((temp + ((temp >> 31) & 0x3FFF)) >> 14) + (cosYmZ - cosZpY2) / 2;

    cosXpY = GteCos(xPlusY);
    cosXmY = GteCos(xMinusY);
    result[8] = (cosXmY + cosXpY) / 2;
}

// ============================================================================
// RotMatrixYXZ (0x00409ed0)
// Compute rotation matrix from SVECTOR using YXZ Euler angle order.
// Inverts X and Z axes (PS1 convention), then divides the nine 14-bit
// intermediates by 4 to reach the standard 4.12 matrix scale.
// Only the 3x3 rotation is written; the translation vector is left alone.
// ============================================================================
MATRIX* RotMatrixYXZ(SVECTOR* r, MATRIX* m)
{
    int result[9];
    GteRotationMatrixYXZ(0x1000 - r->x, (int)r->y, 0x1000 - r->z, result);

    m->m[0][0] = (short)((result[0] + ((result[0] >> 31) & 3)) >> 2);
    m->m[0][1] = (short)((result[1] + ((result[1] >> 31) & 3)) >> 2);
    m->m[0][2] = (short)((result[2] + ((result[2] >> 31) & 3)) >> 2);
    m->m[1][0] = (short)((result[3] + ((result[3] >> 31) & 3)) >> 2);
    m->m[1][1] = (short)((result[4] + ((result[4] >> 31) & 3)) >> 2);
    m->m[1][2] = (short)((result[5] + ((result[5] >> 31) & 3)) >> 2);
    m->m[2][0] = (short)((result[6] + ((result[6] >> 31) & 3)) >> 2);
    m->m[2][1] = (short)((result[7] + ((result[7] >> 31) & 3)) >> 2);
    m->m[2][2] = (short)((result[8] + ((result[8] >> 31) & 3)) >> 2);

    return m;
}

// ============================================================================
// rotate_entity (0x0048c2a0)
// Recursively compute world-space matrices for entity joint hierarchy.
// For each child joint of the given index, applies the parent's world matrix
// to the child's local transform, then recurses into the child's children.
// ============================================================================
void rotate_entity(MATRIX* parentMtx, void* animData, unsigned char jointIdx)
{
    Entity* ent = ENTITY;
    if (ent == NULL || ent->jointsStructs == NULL) return;
    JointStruct* joints = ent->jointsStructs;
    JointStruct* joint = &joints[jointIdx];

    unsigned char* animBase = (unsigned char*)animData;
    unsigned short childListOffset = *(unsigned short*)(animBase + 2 + jointIdx * 4);
    unsigned char* childList = animBase + childListOffset;

    // Apply parent transform to joint's local transform → joint's world matrix
    if ((joint->flags & 8) != 0 && (joint->flags & 0x40) == 0) {
        ApplyLVAndMul0Matrix(parentMtx, &joint->transform, &joint->world);
        unsigned char f = joint->flags;
        joint->flags = (f & 0xF5) | 0x50;
        joint->field_02 = 0;
    }
    if ((joint->flags & 2) != 0) {
        ApplyLVAndMul0Matrix(parentMtx, &joint->transform, &joint->world);
    }

    // Recurse into children
    char childCount = *(char*)(animBase + jointIdx * 4);
    for (char i = childCount; i != 0; i--) {
        unsigned char childIdx = *childList;
        childList++;
        rotate_entity(&joint->world, animData, childIdx);
    }
}

// ============================================================================
// EntityComputeJointWorldMatrices (0x0048c190)
// Sets up the entity's camera-relative transform and recursively computes
// all joint world matrices. Called before entity rendering.
// ca: optional scale factor applied to the entity's local matrix (0 = no scale)
// ============================================================================
void EntityComputeJointWorldMatrices(int ca)
{
    Entity* ent = ENTITY;
    if (ent == NULL) return;
    unsigned char* entBytes = (unsigned char*)ent;

    // Skip if entity type is 0x13/0x18 with sub-type 1
    // Original: *(char*)(_ENTITY + 1) and *(char*)(_ENTITY + 2)
    if (((entBytes[1] != 0x0D) && (entBytes[1] != 0x12)) || (entBytes[2] != 1)) {
        if (ent->jointsStructs == NULL || ent->animHeader == 0) return;
        JointStruct* joints = ent->jointsStructs;

        // Parse animation header to get joint hierarchy
        unsigned short* animPtr = (unsigned short*)ent->animHeader;
        unsigned short baseOffset = *animPtr;
        unsigned char* animBase = (unsigned char*)animPtr + (baseOffset & 0xFFFFFFFC);
        unsigned short rootChildOffset = *(unsigned short*)(animBase + 2);
        unsigned char* rootChildList = animBase + rootChildOffset;

        // Build entity rotation matrix from direction angles
        // The rotation SVECTOR is at entity+0x72: {position.pad, directionAngle, speed.x}
        if ((entBytes[3] & 0x80) == 0) {
            SVECTOR* rotVec = (SVECTOR*)((unsigned char*)ent + 0x72);
            RotMatrix(rotVec, &ent->scaMatrixData.localMatrix);
        }

        // Apply optional scale
        if (ca != 0) {
            g_playerPosScratch.x = ca;
            g_playerPosScratch.y = ca;
            g_playerPosScratch.z = ca;
            ScaleMatrixCols(&ent->scaMatrixData.localMatrix, &g_playerPosScratch);
        }

        // Process root joint (index 0): apply entity transform to first joint
        JointStruct* rootJoint = &joints[0];
        if ((rootJoint->flags & 8) != 0 && (rootJoint->flags & 0x40) == 0) {
            ApplyLVAndMul0Matrix(&ent->scaMatrixData.localMatrix,
                                 &rootJoint->transform, &rootJoint->world);
            unsigned char f = rootJoint->flags;
            rootJoint->flags = (f & 0xF5) | 0x50;
            rootJoint->field_02 = 0;
        }
        if ((rootJoint->flags & 2) != 0) {
            ApplyLVAndMul0Matrix(&ent->scaMatrixData.localMatrix,
                                 &rootJoint->transform, &rootJoint->world);
        }

        // Recursively process children of root joint
        char rootChildCount = *(char*)animBase;
        for (char i = rootChildCount; i != 0; i--) {
            unsigned char childIdx = *rootChildList;
            rootChildList++;
            rotate_entity(&rootJoint->world, animBase, childIdx);
        }
    }
}

// ============================================================================
// EntityApplyLookAtRotation (0x0045a2e0)
// Composes the entity's look-at (head/aim tracking) rotation into the world
// matrix of one designated joint, after EntityComputeJointWorldMatrices has
// built the hierarchy:
//
//     joint->world = joint->world * RotMatrixYXZ(0, yaw, pitch)
//
// The two angles are produced by EntityUpdateLookAtAngles (0x00459eb0), which
// aims that joint at the target stored in entity+0xCC/0xD0/0xD4: it derives a
// yaw from getAngleTowardsTarget() minus the entity's own facing, and a pitch
// from atan(dy / sqrt(dx^2 + dz^2)), slews both toward the target by the
// per-frame step limits at entity+0xD9/0xDA, and clamps them to +/-0x2C8 yaw
// (~62 deg) and +/-0x138 pitch (~27 deg) - i.e. a look-at cone.
//
// Gating: the whole lookAtFlags byte is tested, not a single bit - the slew
// updater stops when 0x10 is cleared but this keeps re-applying the last
// angles, which is what freezes a look-at in place instead of snapping back.
// ============================================================================
void EntityApplyLookAtRotation(void)
{
    Entity* ent = ENTITY;
    if (ent == NULL) return;

    if (ent->lookAtFlags != 0) {
        if (ent->jointsStructs == NULL) return;

        // The original indexes jointsStructs by lookAtJointIdx with no bounds
        // check, relying on every writer of entity+0xDD to stay inside the
        // model's joint count. CompMatrix below writes 32 bytes at joint+0x44,
        // so a stale or uninitialised index silently corrupts the heap and
        // surfaces much later as a wild pointer somewhere unrelated. If this
        // ever fires, the real bug is a missing 0xDD write on some entity
        // setup path, not here.
        if (ent->lookAtJointIdx >= ent->jointCount) {
            static int reported = 0;
            if (reported < 8) {
                reported++;
                char buf[160];
                sprintf_s(buf, sizeof(buf),
                          "[LOOKAT] out-of-range joint: idx=%u jointCount=%u "
                          "entity id=%u flags=0x%02X\n",
                          (unsigned)ent->lookAtJointIdx, (unsigned)ent->jointCount,
                          (unsigned)ent->id, (unsigned)ent->lookAtFlags);
                OutputDebugStringA(buf);
            }
            return;
        }

        JointStruct* joint = &ent->jointsStructs[ent->lookAtJointIdx];

        g_svecScratch.x = 0;
        g_svecScratch.y = joint->rotDeltaX;   // yaw   (joint+0x76)
        g_svecScratch.z = joint->rotDeltaY;   // pitch (joint+0x78)

        // Copy identity matrix to scratch
        MATRIX* src = &g_identityMatrixData;
        MATRIX* dst = &g_matrixScratch;
        for (int i = 8; i != 0; i--) {
            *(unsigned int*)dst->m[0] = *(unsigned int*)src->m[0];
            src = (MATRIX*)(src->m[0] + 2);
            dst = (MATRIX*)(dst->m[0] + 2);
        }

        // Apply YXZ rotation and compose with joint's world matrix
        RotMatrixYXZ(&g_svecScratch, &g_matrixScratch);
        CompMatrix(&joint->world, &g_matrixScratch, &joint->world);
    }
}
