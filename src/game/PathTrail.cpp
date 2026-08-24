// PathTrail.cpp - entity path/trail animation system
 //
 // FUN_0048a210 (entity path animation step) and its three async workers:
 //   FUN_00485820 (0x00485820) -> AsyncTrailBuildGeometry (0x00485610)
 //   FUN_00485a00 (0x00485a00) -> AsyncTrailDraw          (0x00485850)
 //   FUN_00485aa0 (0x00485aa0) -> AsyncTrailReleaseSlot   (0x00485a30)
 //
 // A joint whose flags carry 0x20 is driven along an RDT waypoint path
 // (calc_entity_lighting calls FUN_0048a210 for it). Each frame the joint's
 // current segment matrix is built from the path waypoints, handed to the GTE
 // state buffer via SetRotAndTransMatrix, then per-segment geometry is staged
 // into two 16-entry CMarniViewport2 pools through the async workers.
 //
 // Pool addresses in the original binary:
 //   pool A = 0x00a74de0 = g_tmdObjectBuffer + 0x151290 (constructed by
 //            FUN_00485020's eh_vector_constructor_iterator, 16 x 0x38)
 //   pool B = 0x00ac3178 (same layout)
 // Per-slot draw entries live in INT_ARRAY_00922f00 (0x21 dwords = 0x84 each).
 //
 // D3D adaptation: the original validates slot handles against the driver's
 // handle table (FUN_004483b0 reads pD3D+0x8e0/0x8e4) before rebuilding. The
 // DX11 port keeps no exposed handle table, so that check reduces to the
 // m_isInitialized test; everything else follows the original flow.

#include "../Globals.h"
#include "../marni/Marni3DObject.h"
#include "../marni/MarniSystem.h"

// Defined in EntityCommon.cpp / Room.cpp (see Globals.h for the shared-scratch
// notes on 0x00be0de4/0x00be0de8).
extern int player_distance_z;        // 0x00be0de4
extern int g_scaled_down_dist;       // 0x00be0de8
extern int is_entity_in_switch_zone(VECTOR* position, void* zoneData); // Room.cpp 0x00462d90

void AsyncTrailBuildGeometry(void);
void AsyncTrailDraw(void);
void AsyncTrailReleaseSlot(void);

// ---------------------------------------------------------------------------
// Staging globals (original addresses in comments; each written by its wrapper
// and consumed by the async body on the scheduler task).
// ---------------------------------------------------------------------------
static void* s_trailGeomObj;           // 0x00aae73c - staged by FUN_00485820
static int   s_trailGeomCount;         // 0x00a75160 - staged by FUN_00485820
static void* s_trailDrawObj;           // 0x00aadaa8 - staged by FUN_00485a00
static int   s_trailDrawBright;        // 0x00aae800 - staged by FUN_00485a00
static void* s_trailReleaseObj;        // 0x008fc420 - staged by FUN_00485aa0

// ---------------------------------------------------------------------------
// 0x008f88a8 - per-slot vertex capacity, written by the trail creator
// (FUN_004850d0, not yet ported) and read by AsyncTrailBuildGeometry.
// Until the creator runs this stays all zero, exactly as in the original.
// ---------------------------------------------------------------------------
int g_trailSlotCapacity[16] = {};      // 0x008f88a8

// ---------------------------------------------------------------------------
// 0x00aada68 - per-slot "in use" flag array (scanned by FUN_004850d0,
// cleared by AsyncTrailReleaseSlot).
// ---------------------------------------------------------------------------
int g_trailSlotUsed[64] = {};          // 0x00aada68

// ---------------------------------------------------------------------------
// 0x008f8c80 - per-slot UV scale table, 16 entries x 4 dwords. Written by the
// unported trail creator, read back by AsyncTrailDraw.
// ---------------------------------------------------------------------------
static DWORD g_trailSlotUV[16][4] = {};  // 0x008f8c80

// ---------------------------------------------------------------------------
// The two CMarniViewport2 pools (16 x 0x38-byte entries each). Pool A lives
// inside g_tmdObjectBuffer at +0x151290 exactly as in the original; pool B is
// a standalone region. Elements are seeded with MarniViewport2_InitEntry
// (the port equivalent of FUN_00485020's placement ctor iterator).
// ---------------------------------------------------------------------------
#define TRAIL_POOL_A(i) ((CMarniViewport2*)((BYTE*)g_tmdObjectBuffer + 0x151290 + (i) * 0x38))  // 0x00a74de0
#define TRAIL_POOL_B(i) ((CMarniViewport2*)(s_trailPoolB + (i) * 0x38))                          // 0x00ac3178

static bool s_trailPoolsReady = false;
static BYTE s_trailPoolB[16 * 0x38] = {};

static void ensure_trail_pools(void)
{
    if (s_trailPoolsReady) return;
    for (int i = 0; i < 16; i++) {
        MarniViewport2_InitEntry((void*)TRAIL_POOL_A(i));
        MarniViewport2_InitEntry((void*)TRAIL_POOL_B(i));
    }
    s_trailPoolsReady = true;
}

// ===========================================================================
// FUN_00485820 (0x00485820) - stage one geometry build and queue it.
// Original stores DAT_00aae73c / DAT_00a75160 then ExecAsync(0x00485610).
// ===========================================================================
void FUN_00485820(void* trailObj, int count)
{
    ensure_trail_pools();
    s_trailGeomObj = trailObj;
    s_trailGeomCount = count;
    ExecAsync((void*)AsyncTrailBuildGeometry);
}

// ===========================================================================
// FUN_00485a00 (0x00485a00) - stage one draw and queue it.
// Original stores DAT_00aadaa8 / _DAT_00aae800 then ExecAsync(0x00485850).
// ===========================================================================
void FUN_00485a00(void* trailObj, int brightness)
{
    ensure_trail_pools();
    s_trailDrawObj = trailObj;
    s_trailDrawBright = brightness;
    ExecAsync((void*)AsyncTrailDraw);
}

// ===========================================================================
// FUN_00485aa0 (0x00485aa0) - stage one slot release and queue it.
// Original stores DAT_008fc420 then ExecAsync(0x00485a30).
// ===========================================================================
void FUN_00485aa0(void* trailObj)
{
    ensure_trail_pools();
    s_trailReleaseObj = trailObj;
    ExecAsync((void*)AsyncTrailReleaseSlot);
}

// ===========================================================================
// FUN_0048a210 (0x0048a210) - entity path animation step, called from
// calc_entity_lighting for every joint flagged 0x20 (path-driven).
//
// Joint fields (raw offsets - see TmdRenderer/OptionsMenu call sites):
//   +0x02  step counter (byte)         +0x03  total steps (byte)
//   +0x14  -> trail object             +0x18  -> trail object (slot at +0x0C)
//   +0x44  joint world MATRIX          +0x58  VECTOR position (switch zone)
//   +0x5C  int position accumulator    +0x70/+0x72 shorts fed into it
//
// Trail object: +0x0C = pool slot index; points at a path header whose
//   dword[0] = waypoint byte data, dword[4] = 0x1C-byte segment record base,
//   dword[5] = segment count walked downwards.
// ===========================================================================
void FUN_0048a210(void* jointPtr)
{
    unsigned char* j = (unsigned char*)jointPtr;
    int* path = *(int**)(j + 0x14);

    MATRIX m;
    MATRIX* src = &g_identityMatrixData;
    MATRIX* dst = &m;
    for (int i = 8; i != 0; i--) {
        *(unsigned int*)dst->m[0] = *(unsigned int*)src->m[0];
        src = (MATRIX*)(src->m[0] + 2);
        dst = (MATRIX*)(dst->m[0] + 2);
    }

    player_distance_z = path[5];
    g_playerDisplacement = (int)j[2];
    g_scaled_down_dist = (int)j[3] - g_playerDisplacement;
    g_collPushDepthZHi = (int)*(short*)(j + 0x72);

    int acc = (int)*(short*)(j + 0x70) * g_playerDisplacement + *(int*)(j + 0x5C);
    *(int*)(j + 0x5C) = acc;
    if (10000 < acc) {
        j[0x5C] = 0x10;                 // clamp to 10000 (little-endian bytes)
        j[0x5D] = 0x27;
        j[0x5E] = 0;
        j[0x5F] = 0;
    }

    if (is_entity_in_switch_zone((VECTOR*)(j + 0x58), g_CurrentRdtDataTypePtr) != 0 &&
        (int)j[2] + 1 <= (int)j[3]) {

        char* seg = (char*)path[4] + player_distance_z * 0x1C - 0x1C;
        short* wpt = (short*)((unsigned int)*(unsigned short*)(seg + 0x12) * 8 + path[0]);
        m.t[0] = (*wpt >> 4) * g_scaled_down_dist + (*wpt >> 1) * g_playerDisplacement;
        m.t[1] = (wpt[1] >> 4) * g_scaled_down_dist + (wpt[1] >> 1) * g_playerDisplacement;
        m.t[2] = (wpt[2] >> 4) * g_scaled_down_dist + (wpt[2] >> 1) * g_playerDisplacement;
        SetRotAndTransMatrix(&m);
        unsigned int bright = *(unsigned int*)&m.m[0][0];

        while (player_distance_z != 0) {
            player_distance_z--;
            wpt = (short*)((unsigned int)*(unsigned short*)(seg + 0x12) * 8 + path[0]);
            if (player_distance_z % g_collPushDepthZHi == 0) {
                m.t[0] = (*wpt >> 4) * g_scaled_down_dist + (*wpt >> 1) * g_playerDisplacement;
                m.t[1] = (wpt[1] >> 4) * g_scaled_down_dist + (wpt[1] >> 1) * g_playerDisplacement;
                m.t[2] = (wpt[2] >> 4) * g_scaled_down_dist + (wpt[2] >> 1) * g_playerDisplacement;
            }
            SetRotAndTransMatrix(&m);
            if (0xfef < bright) {
                bright = 0xff0;
            }
            seg -= 0x1C;
            FUN_00485820(*(void**)(j + 0x18), player_distance_z);
        }

        ApplyLVAndMul0Matrix(&g_RoomCameraData, j + 0x44, &g_matrixScratch);
        SetRotAndTransMatrix(&g_matrixScratch);
        FUN_00485a00(*(void**)(j + 0x18), (int)bright);
    }

    unsigned char prevStep = j[2];
    j[2] = prevStep + 1;
    if (j[3] < prevStep) {
        FUN_00485aa0(*(void**)(j + 0x18));
        *j &= 0xDE;                     // clear bits 0x01 | 0x20
        j[2] = 0x80;
    }
}

// ===========================================================================
// AsyncTrailBuildGeometry (0x00485610) - consume the staged GTE matrix and
// rewrite the three vertices of this frame's segment.
//
// Reconstructed from the 0x00485610 disassembly:
//  - slot index from trail object +0x0C; skipped when negative or above the
//    slot's capacity (g_trailSlotCapacity[idx]).
//  - both pool slots are Lock()ed around the update (vtable[6], NULL args).
//  - a column-major 4x4 is assembled from g_gteRotTransMatrix: rotation
//    words scaled by 1/4096 (const at 0x004af2d0), translation ints used raw,
//    bottom row (0,0,0,1).
//  - for each of the 3 vertices: GetVertex(index) from pool B, rotate the
//    xyz triple by that matrix (FUN_0048c820), add the translation, write
//    back through SetVertex(index) on pool A (vtable[3]).
//  - both slots Unlock()ed (vtable[7]).
// ===========================================================================
void AsyncTrailBuildGeometry(void)
{
    int idx = *(int*)((char*)s_trailGeomObj + 0x0C);
    int count = s_trailGeomCount;
    if (idx < 0 || count > g_trailSlotCapacity[idx]) {
        return;
    }

    CMarniViewport2* objA = TRAIL_POOL_A(idx);   // 0x00a74de0 + idx*0x38
    CMarniViewport2* objB = TRAIL_POOL_B(idx);   // 0x00ac3178 + idx*0x38

    objA->Lock(NULL, NULL);
    objB->Lock(NULL, NULL);

    const float k = 0.00024414063f;              // 1/4096, const @0x004af2d0
    const MATRIX& g = g_gteRotTransMatrix;

    // Column-major 4x4 from the GTE state buffer (see layout table above).
    float t[16];
    t[0] = (float)(short)g.m[0][0] * k;  t[4] = (float)(short)g.m[0][1] * k;  t[8]  = (float)(short)g.m[0][2] * k;
    t[1] = (float)(short)g.m[1][0] * k;  t[5] = (float)(short)g.m[1][1] * k;  t[9]  = (float)(short)g.m[1][2] * k;
    t[2] = (float)(short)g.m[2][0] * k;  t[6] = (float)(short)g.m[2][1] * k;  t[10] = (float)(short)g.m[2][2] * k;
    t[3] = 0.0f;                         t[7] = 0.0f;                         t[11] = 0.0f;
    t[12] = (float)g.t[0];               t[13] = (float)g.t[1];               t[14] = (float)g.t[2];
    t[15] = 1.0f;

    DWORD prev[11];
    DWORD out[11];
    int baseIdx = count * 3;
    for (int i = 0; i < 3; i++) {
        objB->GetVertex(baseIdx + i, prev);

        float x = *(float*)&prev[0];
        float y = *(float*)&prev[1];
        float z = *(float*)&prev[2];

        // FUN_0048c820(&v, &t): v = rotation columns . v, then + translation.
        float nx = t[0] * x + t[4] * y + t[8]  * z + t[12];
        float ny = t[1] * x + t[5] * y + t[9]  * z + t[13];
        float nz = t[2] * x + t[6] * y + t[10] * z + t[14];

        for (int f = 0; f < 11; f++) {
            out[f] = prev[f];
        }
        *(float*)&out[0] = nx;
        *(float*)&out[1] = ny;
        *(float*)&out[2] = nz;

        objA->SetVertex(baseIdx + i, out);
    }

    objA->Unlock();
    objB->Unlock();
}

// ===========================================================================
// AsyncTrailDraw (FUN_00485850) - build the slot's 0x84-byte D3D draw entry
// in INT_ARRAY_00922f00 and hand it to the driver.
//
// Entry layout (dword indices within idx*0x21):
//   [4..6]/[8..A]/[C..E]  GTE rotation rows scaled by 1/4096
//   [7]/[B]/[F]           0
//   [10..12]              translation ints as floats   [13] = 1.0f
//   [17]                  the slot's driver handle
//   [19..20]/[1D..20]     the slot's UV scale pair, twice
// The entry (+8) is then registered via CMarniDirect3D vtable[10]
// SetTexture(data, 10).
// ===========================================================================
void AsyncTrailDraw(void)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (pD3D == NULL || !pD3D->m_isInitialized) {
        return;
    }

    int idx = *(int*)((char*)s_trailDrawObj + 0x0C);
    if (idx < 0) {
        return;
    }

    // FUN_004483b0(pD3D, e[0x17], &poolA[idx]): the original validates the
    // slot handle against the driver table here; the DX11 port has no exposed
    // handle table, so only the initialized gate applies.

    int* e = &INT_ARRAY_00922f00[idx * 0x21];

    const float k = 0.00024414063f;              // 1/4096, const @0x004af2d0
    const MATRIX& g = g_gteRotTransMatrix;
    e[4]  = (int)((float)(short)g.m[0][0] * k);
    e[5]  = (int)((float)(short)g.m[1][0] * k);
    e[6]  = (int)((float)(short)g.m[2][0] * k);
    e[8]  = (int)((float)(short)g.m[0][1] * k);
    e[9]  = (int)((float)(short)g.m[1][1] * k);
    e[0xA] = (int)((float)(short)g.m[2][1] * k);
    e[0xC] = (int)((float)(short)g.m[0][2] * k);
    e[0xD] = (int)((float)(short)g.m[1][2] * k);
    e[0xE] = (int)((float)(short)g.m[2][2] * k);
    e[7]  = 0;
    e[0xB] = 0;
    e[0xF] = 0;
    e[0x10] = (int)(float)g.t[0];
    e[0x11] = (int)(float)g.t[1];
    e[0x12] = (int)(float)g.t[2];
    e[0x13] = (int)0x3f800000;                   // 1.0f

    e[0x19] = (int)g_trailSlotUV[idx][0];
    e[0x1A] = (int)g_trailSlotUV[idx][1];
    e[0x1B] = (int)g_trailSlotUV[idx][2];
    e[0x1C] = (int)g_trailSlotUV[idx][3];
    e[0x1D] = (int)g_trailSlotUV[idx][0];
    e[0x1E] = (int)g_trailSlotUV[idx][1];
    e[0x1F] = (int)g_trailSlotUV[idx][2];
    e[0x20] = (int)g_trailSlotUV[idx][3];

    if (pD3D->vtable != NULL && pD3D->vtable[10] != NULL) {
        ((void(*)(void*, void*, int))pD3D->vtable[10])(pD3D, (BYTE*)e + 8, 10);
    }
}

// ===========================================================================
// AsyncTrailReleaseSlot (0x00485a30) - free one trail slot: delete the
// recorded driver handle (vtable[9] DeleteObjectHandle), Release() both pool
// viewports (their vtable[0]) and clear the in-use flag.
// ===========================================================================
void AsyncTrailReleaseSlot(void)
{
    int idx = *(int*)((char*)s_trailReleaseObj + 0x0C);
    if (idx < 0) {
        return;
    }

    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (pD3D != NULL && pD3D->vtable != NULL && pD3D->vtable[9] != NULL) {
        int handle = INT_ARRAY_00922f00[idx * 0x21 + 0x17];
        ((void(*)(void*, int))pD3D->vtable[9])(pD3D, handle);
        INT_ARRAY_00922f00[idx * 0x21 + 0x17] = 0;
    }

    TRAIL_POOL_A(idx)->CMarniViewport2::Release();
    TRAIL_POOL_B(idx)->CMarniViewport2::Release();
    g_trailSlotUsed[idx] = 0;
}
