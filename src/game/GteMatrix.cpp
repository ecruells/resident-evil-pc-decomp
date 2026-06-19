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
    int s_sum = GteSin(sum23);
    int s_dif = GteSin(dif23);
    int c1 = GteCos(param_1);
    int tmp = ((s_sum - s_dif) / 2) * c1;
    s_sum = GteSin(sum23);
    s_dif = GteSin(dif23);
    param_4[0] = ((tmp + (tmp >> 0x1f & 0x3fffU)) >> 0xe) + (s_dif + s_sum) / 2;

    int cs1p3 = GteCos(param_3 + param_1);
    int cm1p3 = GteCos(param_1 - param_3);
    int sum12 = param_1 + param_2;
    int dif12 = param_2 - param_1;
    param_4[1] = (cs1p3 - cm1p3) / 2;

    int c_sum12 = GteCos(sum12);
    int c_dif12 = GteCos(dif12);
    int c3 = GteCos(param_3);
    tmp = ((c_sum12 - c_dif12) / 2) * c3;
    int c_dif23 = GteCos(dif23);
    int c_sum23b = GteCos(sum23);
    param_4[2] = ((tmp + (tmp >> 0x1f & 0x3fffU)) >> 0xe) + (c_sum23b + c_dif23) / 2;

    int s_sum12 = GteSin(sum12);
    int s_dif12 = GteSin(dif12);
    int s3 = GteSin(param_3);
    tmp = ((s_sum12 - s_dif12) / 2) * s3;
    c_dif23 = GteCos(dif23);
    c_sum23b = GteCos(sum23);
    param_4[3] = ((tmp + (tmp >> 0x1f & 0x3fffU)) >> 0xe) + (c_dif23 - c_sum23b) / 2;

    int sp1p3 = GteSin(param_3 + param_1);
    int sm1p3 = GteSin(param_1 - param_3);
    param_4[4] = (sm1p3 + sp1p3) / 2;

    c_sum12 = GteCos(sum12);
    c_dif12 = GteCos(dif12);
    s3 = GteSin(param_3);
    tmp = ((c_sum12 - c_dif12) / 2) * s3;
    int s_sum23b = GteSin(sum23);
    int s_dif23 = GteSin(dif23);
    param_4[5] = ((tmp + (tmp >> 0x1f & 0x3fffU)) >> 0xe) + (s_sum23b - s_dif23) / 2;

    int cm1m2 = GteCos(param_1 - param_2);
    int cp1p2 = GteCos(sum12);
    param_4[6] = (cm1m2 - cp1p2) / 2;

    int c1b = GteCos(param_1);
    param_4[7] = -c1b;

    int sm1m2 = GteSin(param_1 - param_2);
    int sp1p2 = GteSin(sum12);
    param_4[8] = (sp1p2 + sm1m2) / 2;
}

// ============================================================================
// MatrixToCamera (0x0040a680)
// Computes a camera view matrix from position/orientation data and stores
// it in g_RoomCameraData (0x004bca88). Used during room/camera transitions.
// Input: pointer to camera from/to position data (6 ints = from_xyz, to_xyz)
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
    int toX   = camData[3];
    int toY   = camData[4];
    int toZ   = camData[5];
    int roll  = camData[6];

    int dx = toX - fromX;
    int dy = -(toY - fromY); // PS1 Y negation
    int dz = toZ - fromZ;

    int dist = (int)sqrt((double)(dx * dx + dy * dy + dz * dz));

    int pitchAngle;
    if (dist != 0) {
        pitchAngle = (int)(asin((double)dy / (double)dist) * (4096.0 / 3.14159265));
    } else {
        pitchAngle = 0;
    }

    int yawAngle;
    if (dx != 0 || dz != 0) {
        yawAngle = (int)(atan2((double)dx, (double)dz) * (4096.0 / 3.14159265));
    } else {
        yawAngle = 0;
    }

    int rotResult[9];
    FUN_004403c0(0, 0, pitchAngle, rotResult);

    MATRIX* camMatrix = (MATRIX*)&g_RoomCameraData;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            camMatrix->m[i][j] = (short)rotResult[i * 3 + j];
        }
    }
    camMatrix->t[0] = -fromX;
    camMatrix->t[1] = fromY;
    camMatrix->t[2] = -fromZ;

    MATRIX yawMatrix;
    int yawResult[9];
    FUN_004403c0(0, yawAngle, 0, yawResult);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            yawMatrix.m[i][j] = (short)yawResult[i * 3 + j];
        }
    }
    yawMatrix.t[0] = 0;
    yawMatrix.t[1] = 0;
    yawMatrix.t[2] = 0;

    MulMatrix0(&yawMatrix, camMatrix, camMatrix);

    VECTOR translation;
    translation.x = -fromX;
    translation.y = fromY;
    translation.z = -fromZ;
    ApplyMatrixLV(camMatrix, &translation, (VECTOR*)camMatrix->t);

    return 0;
}
