// TmdRenderer.cpp - 3D TMD object render queue (DX11 replacement for the
// original DX5 ordering-table 3D path). See TmdRenderer.h for the data flow.
//
// Per-object data layout inside a CMarniDirect3DTMD slot (0x84-byte entries,
// base at slot+0x4D0 for m_objectData / slot+0xD10 for m_objectDataCopy):
//   +0x00  DWORD  primitive type (4 = TMD mesh object)
//   +0x08  float[16] model->view transform, written by
//          CMarniDirect3DTMD::Transform from the FUN_00483080 matrix. The GTE
//          rotation is stored so that row r of g_gteRotTransMatrix lands in
//          M[r], M[r+4], M[r+8] (matching the original store order at
//          0x004830ef), translation in M[12..14], and the FUN_00486190 view is
//          already folded in. A transformed position is therefore
//          vx = M[0]*x + M[4]*y + M[8]*z + M[12], i.e. the D3D row-vector
//          convention.
//   +0x54  DWORD  D3D object handle (unused in the DX11 port)
//   +0x58  DWORD  texture handle (MarniHandle via VTable_CreateTextureHandle)
//
// Geometry lives in the slot's embedded CMarniViewport2 elements
// (slot + objIndex*0x4C): m_pVertexBuffer holds 11-float vertices
// {x,y,z, nx,ny,nz, r,g,b, u,v}, m_pIndexBuffer holds WORD indices
// (3 per triangle, 4 per quad in PS1 order 0,1,3,2). Both position and normal
// have Y negated by PSXObject_Store (PS1 +Y is down, D3D +Y is up).
#include "TmdRenderer.h"
#include "SpriteRenderer.h"
#include "../Globals.h"
#include "../marni/MarniDX.h"
#include "../marni/MarniSystem.h"
#include "../marni/Marni3DObject.h"
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cmath>
#include "../DebugPrint.h"

// ============================================================================
// Tuning constants for the projection. The original DX5 renderer projected
// with a perspective viewport; the GTE-equivalent pinhole model uses
//   sx = cx + vx * f / vz
//   sy = cy - vy * f / vz
// where (vx,vy,vz) is the objData-matrix view position, (cx,cy) = subpixel
// offset (screen centre) and f = g_sceneRenderParam (the RDT camera fov /
// title render param).
//
// vz is POSITIVE in front of the camera: FUN_00483080 stores the GTE depth
// (g_gteRotTransMatrix.t[2], positive in front) in the matrix translation and
// FUN_00486190 only rotates it. The Y term is subtracted because
// SetRotAndTransMatrix negates the GTE Y translation while screen Y grows
// downwards.
// ============================================================================
#define TMD_NEAR_Z          (1.0f)    // cull verts with vz <= this (behind/near)
#define TMD_FAR_Z      (131072.0f)    // depth-buffer range; every scene z fits
#define TMD_MAX_QUEUE       2048
#define TMD_MAX_TRIS_FLUSH  1024      // MarniDX::DrawTriangles3D per-call cap
#define TMD_MAX_TRIS_COLLECT 8192     // per-frame triangle pool for the depth sort

// View-space Z -> normalised [0,1] depth for the depth buffer. Linear: a D24
// buffer over TMD_FAR_Z still resolves better than a hundredth of a world unit,
// and a linear ramp keeps distant room geometry from collapsing into one value
// the way a 1/z ramp would.
static inline float TmdDepthNdc(float vz)
{
    float d = (vz - TMD_NEAR_Z) * (1.0f / (TMD_FAR_Z - TMD_NEAR_Z));
    if (d < 0.0f) d = 0.0f;
    if (d > 1.0f) d = 1.0f;
    return d;
}

struct TmdDrawEntry {
    BYTE* slot;        // owning CMarniDirect3DTMD slot in g_tmdObjectBuffer
    BYTE* objData;     // the queued 0x84-byte object entry (matrix at +0x08)
    int   objIndex;    // embedded CMarniViewport2 index
    int   depth;       // original OT depth (gte t[2] >> shift); kept for
                       // reference - ordering is per-triangle, see FlushTmdObjects
};

static TmdDrawEntry g_tmdQueue[TMD_MAX_QUEUE];
static int          g_tmdQueueCount = 0;

// Per-frame triangle pool. Triangles from every queued object are gathered here
// and submitted only after a global depth sort (see FlushTmdObjects).
struct TmdTri {
    float v[27];   // 3 vertices x {x, y, z, u, v, r, g, b, a}
    float depth;   // mean view-space Z (larger = farther)
    DWORD tex;
};
static TmdTri g_tmdTris[TMD_MAX_TRIS_COLLECT];
static int    g_tmdTriOrder[TMD_MAX_TRIS_COLLECT];

// ============================================================================
// TmdQueue_Reset
// ============================================================================
void TmdQueue_Reset(void)
{
    g_tmdQueueCount = 0;
}

// ============================================================================
// TmdQueueObject (called from CMarniDirect3D vtable[10], original 0x00448300)
// Recovers the owning TMD slot + object index from the objData pointer and
// records a reference to it for this frame.
//
// Only the POINTER may be recorded here, never a copy of the object's render
// state: CMarniDirect3DTMD::Transform (0x00415520) inserts every object into
// the ordering table FIRST and writes the model->view matrix to objData + 0x08
// afterwards, exactly as the original does. Snapshotting the matrix at queue
// time therefore yields the previous frame's transform - or an all-zero matrix
// the first time a slot is used, which collapses every vertex onto the near
// plane and draws nothing at all.
// ============================================================================
void TmdQueueObject(void* objData, int depth)
{
    if (objData == NULL || g_tmdQueueCount >= TMD_MAX_QUEUE) return;

    BYTE* p   = (BYTE*)objData;
    BYTE* buf = (BYTE*)g_tmdObjectBuffer;
    const int slotStride = 0x1594;
    const int maxSlot    = 250;

    ptrdiff_t diff = p - buf;
    if (diff < 0) return;
    int slotIdx = (int)(diff / slotStride);
    if (slotIdx >= maxSlot) return;
    int within = (int)(diff - (ptrdiff_t)slotIdx * slotStride);

    int objIndex;
    if (within >= 0x4D0 && within < 0x4D0 + 16 * 0x84) {
        objIndex = (within - 0x4D0) / 0x84;
    }
    else if (within >= 0xD10 && within < 0xD10 + 16 * 0x84) {
        objIndex = (within - 0xD10) / 0x84;
    }
    else {
        return;
    }

    TmdDrawEntry* e = &g_tmdQueue[g_tmdQueueCount++];
    e->slot     = buf + (ptrdiff_t)slotIdx * slotStride;
    e->objData  = p;
    e->objIndex = objIndex;
    e->depth    = depth;
}

// ============================================================================
// Lighting. g_d3dLightData holds 3 lights x 12 DWORDs:
//   [3..5] = direction float3 (GTE camera space, written by SetLightMatrix)
//   [6..8] = color float3 0..1 (written by FUN_0040ac80)
// Normals are rotated by the objData matrix, which is the GTE joint rotation
// composed with the FUN_00486190 view rotation. That view is a small tilt away
// from identity (it only leans by the subpixel offset), so the light directions
// are used in GTE camera space as-is: both the normals and the light vectors go
// through the same camera rotation, and a rotation preserves the dot product.
// A previous revision negated X and Z here to cancel out an incorrect 180 degree
// view flip in FUN_00486190.
// ============================================================================
static void TmdComputeLight(const float* n, const float* rot,
                            float* outR, float* outG, float* outB)
{
    // Normal into view space (rotation part of the objData matrix)
    float nx = rot[0] * n[0] + rot[4] * n[1] + rot[8]  * n[2];
    float ny = rot[1] * n[0] + rot[5] * n[1] + rot[9]  * n[2];
    float nz = rot[2] * n[0] + rot[6] * n[1] + rot[10] * n[2];

    // Ambient from g_d3dAmbientColor (packed r<<16|g<<8|b, 0..255)
    float r = (float)((g_d3dAmbientColor >> 16) & 0xFF) / 255.0f;
    float g = (float)((g_d3dAmbientColor >> 8)  & 0xFF) / 255.0f;
    float b = (float)( g_d3dAmbientColor        & 0xFF) / 255.0f;

    const float* lights = (const float*)g_d3dLightData;
    for (int i = 0; i < 3; i++) {
        const float* L = lights + i * 12;
        // SetLightMatrix writes pLight[0] = 2 (D3DLIGHT_DIRECTIONAL) and stores
        // the normalised light POSITION in the direction field, so [3..5] is a
        // D3D dvDirection: the direction the light travels. D3D's diffuse term
        // is dot(N, -dvDirection), hence the negation. Dotting without it lit
        // only the faces pointing away from the camera - the options-menu lights
        // all sit just behind the model (z = -780 with the model at z = -700),
        // so the visible side came out at ambient level (~10%).
        float d = -(nx * L[3] + ny * L[4] + nz * L[5]);
        if (d > 0.0f) {
            r += d * L[6];
            g += d * L[7];
            b += d * L[8];
        }
    }

    *outR = (r > 1.0f) ? 1.0f : r;
    *outG = (g > 1.0f) ? 1.0f : g;
    *outB = (b > 1.0f) ? 1.0f : b;
}

// ============================================================================
// FlushTmdObjects - transform, light and draw every queued TMD object.
//
// The queue entry's `depth` (the original OT depth) is not used for ordering:
// triangles from all objects go into one pool and are sorted individually by
// view-space Z, which is what the original's depth buffer did within an object.
// ============================================================================
void FlushTmdObjects(void)
{
    int queued = g_tmdQueueCount;

    if (queued > 0 && Marni_DX() != NULL) {
        float scaleX, scaleY;
        MarniGetRenderScale(&scaleX, &scaleY);
        float cx = (float)g_SubpixelOffsetX * scaleX;
        float cy = (float)g_SubpixelOffsetY * scaleY;
        float f  = (float)g_sceneRenderParam * scaleX;

        // Triangles are collected across every queued object and submitted only
        // after a per-triangle depth sort. The original inserts each TMD object
        // into the ordering table at a single depth and lets the D3D depth
        // buffer resolve the triangles inside it; MarniDX::DrawTriangles carries
        // no Z, so without this the far side of each limb paints over its own
        // near side and the model renders as a dark shell.
        int collected = 0;

        for (int i = 0; i < queued; i++) {
            TmdDrawEntry* e = &g_tmdQueue[i];
            CMarniViewport2* elem =
                (CMarniViewport2*)(e->slot + e->objIndex * 0x4C);

            const float* vbuf = (const float*)elem->m_pVertexBuffer;
            const WORD*  ibuf = (const WORD*)elem->m_pIndexBuffer;
            int vtxCount = (int)elem->m_vertexCount;
            int listCount = (int)elem->m_listCount;
            int primType = (int)elem->m_primitiveType;
            if (vbuf == NULL || ibuf == NULL || vtxCount <= 0 || listCount <= 0)
                continue;
            if (primType != 3 && primType != 4)
                continue;

            // Read the transform and texture handle now (see TmdQueueObject):
            // both are written to the object entry after it was queued.
            const float* M = (const float*)(e->objData + 0x08);
            DWORD tex = (*(DWORD*)(e->slot + e->objIndex * 0x4C + 0x48) != 0)
                        ? *(DWORD*)(e->objData + 0x58) : 0;

            // Transform + project + light every vertex
            // (heap-allocate per object; vertex counts are small)
            float* sx = (float*)malloc(sizeof(float) * vtxCount * 4);
            float* sy = sx + vtxCount;
            float* vzArr = sx + vtxCount * 2;   // view-space Z, for the depth sort
            float* cr = (float*)malloc(sizeof(float) * vtxCount * 3);
            float* cg = cr + vtxCount;
            float* cb = cg + vtxCount;
            char* clipped = (char*)malloc(vtxCount);
            if (!sx || !cr || !clipped) {
                free(sx); free(cr); free(clipped);
                continue;
            }

            // First normal (flat-shaded primitives only fill vertex 0)
            float flatN[3] = { vbuf[3], vbuf[4], vbuf[5] };

            for (int v = 0; v < vtxCount; v++) {
                const float* vtx = vbuf + v * 11;
                float x = vtx[0], y = vtx[1], z = vtx[2];
                float vx = M[0] * x + M[4] * y + M[8]  * z + M[12];
                float vy = M[1] * x + M[5] * y + M[9]  * z + M[13];
                float vz = M[2] * x + M[6] * y + M[10] * z + M[14];

                vzArr[v] = vz;
                if (vz <= TMD_NEAR_Z) {
                    clipped[v] = 1;
                    sx[v] = sy[v] = 0.0f;
                }
                else {
                    clipped[v] = 0;
                    float iz = f / vz;
                    sx[v] = cx + vx * iz;
                    sy[v] = cy - vy * iz;
                }

                // Lighting: vertices without a normal (flat prims) reuse
                // the first vertex's normal.
                const float* n = vtx + 3;
                if (n[0] == 0.0f && n[1] == 0.0f && n[2] == 0.0f) n = flatN;
                TmdComputeLight(n, M, &cr[v], &cg[v], &cb[v]);
                cr[v] *= vtx[6]; cg[v] *= vtx[7]; cb[v] *= vtx[8];
            }

            // Emit triangles
            for (int pIdx = 0; pIdx < listCount; pIdx++) {
                WORD idx[4];
                int vertsInPrim = (primType == 3) ? 3 : 4;
                const WORD* ip = ibuf + pIdx * vertsInPrim;
                idx[0] = ip[0]; idx[1] = ip[1]; idx[2] = ip[2];
                if (vertsInPrim == 4) idx[3] = ip[3];

                // Triangulate: (0,1,2) + (0,2,3) for quads
                for (int t = 0; t < vertsInPrim - 2; t++) {
                    WORD i0 = idx[0];
                    WORD i1 = idx[t + 1];
                    WORD i2 = idx[t + 2];
                    if (i0 >= vtxCount || i1 >= vtxCount || i2 >= vtxCount)
                        continue;
                    if (clipped[i0] || clipped[i1] || clipped[i2])
                        continue;

                    float x0 = sx[i0], y0 = sy[i0];
                    float x1 = sx[i1], y1 = sy[i1];
                    float x2 = sx[i2], y2 = sy[i2];

                    // No backface culling: the PS1 GPU draws both sides and the
                    // original PC path leaned on the D3D depth buffer to decide
                    // which survives. The per-triangle depth sort below stands in
                    // for that buffer, so the nearer face always wins and there
                    // is no winding convention to get wrong here. Degenerate
                    // (zero-area) triangles are still dropped.
                    float area2 = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
                    if (area2 == 0.0f)
                        continue;

                    if (collected >= TMD_MAX_TRIS_COLLECT) continue;

                    TmdTri* t3 = &g_tmdTris[collected];
                    float* o = t3->v;
                    const float* v0 = vbuf + i0 * 11;
                    const float* v1 = vbuf + i1 * 11;
                    const float* v2 = vbuf + i2 * 11;
                    o[0]  = x0; o[1]  = y0; o[2]  = TmdDepthNdc(vzArr[i0]);
                    o[3]  = v0[9];  o[4]  = v0[10];
                    o[5]  = cr[i0]; o[6]  = cg[i0]; o[7]  = cb[i0]; o[8]  = 1.0f;
                    o[9]  = x1; o[10] = y1; o[11] = TmdDepthNdc(vzArr[i1]);
                    o[12] = v1[9];  o[13] = v1[10];
                    o[14] = cr[i1]; o[15] = cg[i1]; o[16] = cb[i1]; o[17] = 1.0f;
                    o[18] = x2; o[19] = y2; o[20] = TmdDepthNdc(vzArr[i2]);
                    o[21] = v2[9];  o[22] = v2[10];
                    o[23] = cr[i2]; o[24] = cg[i2]; o[25] = cb[i2]; o[26] = 1.0f;
                    t3->depth = (vzArr[i0] + vzArr[i1] + vzArr[i2]) * (1.0f / 3.0f);
                    t3->tex   = tex;
                    g_tmdTriOrder[collected] = collected;
                    collected++;
                }
            }

            free(sx); free(cr); free(clipped);
        }

        // The depth buffer resolves which face wins, so the sort is no longer
        // load-bearing for opaque geometry - it stays because it keeps the
        // alpha-blended triangles blending back-to-front and it groups runs of
        // one texture together, which halves the draw calls.
        std::sort(g_tmdTriOrder, g_tmdTriOrder + collected, [](int a, int b) {
            return g_tmdTris[a].depth > g_tmdTris[b].depth;
        });

        static float triVerts[TMD_MAX_TRIS_FLUSH * 3 * 9];
        int   triCount = 0;
        DWORD triTex   = 0;

        for (int k = 0; k < collected; k++) {
            const TmdTri* t3 = &g_tmdTris[g_tmdTriOrder[k]];
            if ((triCount > 0 && t3->tex != triTex) || triCount >= TMD_MAX_TRIS_FLUSH) {
                Marni_DX()->DrawTriangles3D(triVerts, triCount, (MarniHandle)triTex,
                                            MARNI_SAMPLER_POINT, MARNI_BLEND_ALPHA);
                triCount = 0;
            }
            triTex = t3->tex;
            memcpy(triVerts + triCount * 27, t3->v, sizeof(t3->v));
            triCount++;
        }
        if (triCount > 0) {
            Marni_DX()->DrawTriangles3D(triVerts, triCount, (MarniHandle)triTex,
                                        MARNI_SAMPLER_POINT, MARNI_BLEND_ALPHA);
        }
    }

    g_tmdQueueCount = 0;
}
