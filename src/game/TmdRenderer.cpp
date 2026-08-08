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
//   +0x80  DWORD  render flags. Bit 2 = UNLIT: the original's renderer tests
//          it at 0x00446e99 (skip transforming the light directions) and
//          0x00447043 (skip accumulating the lights - write the vertex colour
//          straight into the primitive). Only the door animation sets it, in
//          DoorAsyncCreateTmd; the door is meant to be full-bright rather than
//          shaded by whatever room lighting was last in the globals.
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

// Forward declarations for dependencies defined elsewhere
extern unsigned int AsyncCreateTmdObject(unsigned int param1, unsigned int param2, unsigned int param3);
extern void FUN_00486df0(void* spriteData);                 // sprite render mode (EngineStubs.cpp)
extern void FUN_004896c0(void* joint, short p1, short p2, int p3); // 0x004896c0 (EngineStubs.cpp)
extern void FUN_0048a210(void* joint);                      // 0x0048a210 (EngineStubs.cpp)
extern int  is_entity_in_switch_zone(VECTOR* pos, void* zoneData); // 0x00462d90 (Room.cpp)
extern void FUN_00483580(int* joint, MATRIX* out);          // 0x00483580 item_viewer_compose_matrix (MainMenu.cpp)
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

// Lighting state AS OF QUEUE TIME, one record per queue slot. Unlike the
// transform (which the caller writes after insertion, see TmdQueueObject) the
// lights are already set when an object is queued: calc_entity_lighting calls
// update_entity_lighting for the entity, then SetLightMatrix per joint, and only
// then queues. On real hardware the driver latched the light state per
// DrawPrimitive, so each entity kept its own lighting; reading the globals at
// flush time instead shaded EVERY entity with whatever was set last - the
// player, since the render loop draws enemies first and the player after. A
// corpse lying still then appeared to be lit by a lamp the player was carrying.
//
// This lives on the heap rather than beside g_tmdQueue on purpose: 152KB of
// extra .bss shifts every static that follows it, and this port has globals
// whose addresses other code derives arithmetically. Keeping the fix out of
// .bss keeps the layout byte-identical to before it.
struct TmdLightState {
    float dir[3][3];   // g_d3dLightData[i*12 + 3..5]
    float col[3][3];   // g_d3dLightData[i*12 + 6..8]
    DWORD ambient;     // g_d3dAmbientColor
};

// Pinned so the light-latch fix cannot silently grow the .bss footprint again.
static_assert(sizeof(TmdDrawEntry) == 16, "TmdDrawEntry must stay 16 bytes");

static TmdDrawEntry  g_tmdQueue[TMD_MAX_QUEUE];
static int           g_tmdQueueCount = 0;
static TmdLightState* g_tmdLight = NULL;   // TMD_MAX_QUEUE records, heap, never freed

// Per-frame triangle pool. Triangles from every queued object are gathered here
// and submitted only after a global depth sort (see FlushTmdObjects).
// 3 vertices x {x, y, z(ndc), w, u, v, r, g, b, a}. `w` is the vertex's
// view-space Z: DrawTriangles3D's vertex shader divides by it so the UVs and
// colours interpolate perspective-correctly instead of affinely (see
// g_Model3DVS_Source). Without it, large near polygons - the door panel in the
// room transition above all - swim their texture as they turn.
#define TMD_VERT_FLOATS  10
#define TMD_TRI_FLOATS   (TMD_VERT_FLOATS * 3)
struct TmdTri {
    float v[TMD_TRI_FLOATS];
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

    if (g_tmdLight == NULL) {
        g_tmdLight = (TmdLightState*)calloc(TMD_MAX_QUEUE, sizeof(TmdLightState));
        if (g_tmdLight == NULL) return;
    }

    BYTE* p = (BYTE*)objData;
    const int slotStride = TMD_SLOT_STRIDE;

    // Resolve the pointer to (owning slot, offset within it). Every region that
    // can own a CMarniDirect3DTMD slot is listed here; they are disjoint, so
    // order does not matter. Each bound is the region's own size - notably the
    // main buffer's is its capacity, NOT TMD_CLEANUP_SLOT_COUNT, which is a
    // fact about what the cleanup destroys rather than about what is
    // addressable. Tying those two together is what broke the door animation.
    static const struct { BYTE* base; size_t size; } kSlotRegions[] = {
        { g_renderStateTMD,     sizeof(g_renderStateTMD)     },  // item examine / render state
        { g_doorTmdSlotBuffer,  sizeof(g_doorTmdSlotBuffer)  },  // door animation
        { g_itemTmdSlotBuffer,  sizeof(g_itemTmdSlotBuffer)  },  // item viewer
        { g_itemSharedTmdSlot,  sizeof(g_itemSharedTmdSlot)  },  // item viewer, shared transparent
        { g_tmdObjectBuffer,    sizeof(g_tmdObjectBuffer)    },  // entities, room objects
    };

    BYTE* slotBase = NULL;
    ptrdiff_t within = 0;
    for (size_t r = 0; r < sizeof(kSlotRegions) / sizeof(kSlotRegions[0]); r++) {
        BYTE* base = kSlotRegions[r].base;
        if (p < base || p >= base + kSlotRegions[r].size) continue;
        ptrdiff_t diff = p - base;
        ptrdiff_t off  = (diff / slotStride) * slotStride;
        slotBase = base + off;
        within   = diff - off;
        break;
    }
    if (slotBase == NULL) return;

    int objIndex;
    if (within >= 0x4D0 && within < 0x4D0 + 16 * 0x84) {
        objIndex = (int)(within - 0x4D0) / 0x84;
    }
    else if (within >= 0xD10 && within < 0xD10 + 16 * 0x84) {
        objIndex = (int)(within - 0xD10) / 0x84;
    }
    else {
        return;
    }

    int idx = g_tmdQueueCount++;
    TmdDrawEntry* e = &g_tmdQueue[idx];
    e->slot     = slotBase;
    e->objData  = p;
    e->objIndex = objIndex;
    e->depth    = depth;

    // Latch the light state for this object (see the note on TmdLightState).
    TmdLightState* ls = &g_tmdLight[idx];
    const float* lights = (const float*)g_d3dLightData;
    for (int i = 0; i < 3; i++) {
        const float* L = lights + i * 12;
        ls->dir[i][0] = L[3]; ls->dir[i][1] = L[4]; ls->dir[i][2] = L[5];
        ls->col[i][0] = L[6]; ls->col[i][1] = L[7]; ls->col[i][2] = L[8];
    }
    ls->ambient = g_d3dAmbientColor;
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
static void TmdComputeLight(const TmdLightState* ls, const float* n, const float* rot,
                            float* outR, float* outG, float* outB)
{
    // Normal into view space (rotation part of the objData matrix)
    float nx = rot[0] * n[0] + rot[4] * n[1] + rot[8]  * n[2];
    float ny = rot[1] * n[0] + rot[5] * n[1] + rot[9]  * n[2];
    float nz = rot[2] * n[0] + rot[6] * n[1] + rot[10] * n[2];

    // Ambient from the latched g_d3dAmbientColor (packed r<<16|g<<8|b, 0..255)
    // - NOT the live global, see TmdLightState.
    float r = (float)((ls->ambient >> 16) & 0xFF) / 255.0f;
    float g = (float)((ls->ambient >> 8)  & 0xFF) / 255.0f;
    float b = (float)( ls->ambient        & 0xFF) / 255.0f;

    for (int i = 0; i < 3; i++) {
        // L[3..5] = direction, L[6..8] = colour, from the latched copy.
        const float L[9] = {
            0.0f, 0.0f, 0.0f,
            ls->dir[i][0], ls->dir[i][1], ls->dir[i][2],
            ls->col[i][0], ls->col[i][1], ls->col[i][2],
        };
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

    if (queued > 0 && g_tmdLight != NULL && Marni_DX() != NULL) {
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

            // TmdQueueObject only ever stores a slot inside g_tmdObjectBuffer,
            // so a null slot means this record was never filled - i.e. the
            // count outran the writes. Dereferencing it read address 4 and
            // faulted; skip it and say so instead.
            if (e->slot == NULL) {
                dbg_printf("FlushTmdObjects: unfilled queue entry %d of %d "
                           "(objData=%p objIndex=%d)\n",
                           i, queued, (void*)e->objData, e->objIndex);
                continue;
            }

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
            // Render flags at +0x80: bit 2 = unlit, take the vertex colour as
            // it stands (see the layout note at the top of this file).
            bool unlit = (*(DWORD*)(e->objData + 0x80) & 2) != 0;

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

                if (unlit) {
                    // The original's else-branch at 0x00447043: no ambient, no
                    // lights, the vertex colour becomes the primitive colour.
                    // PSXObject_Store writes 1.0 for textured primitives, so a
                    // door renders at full texture brightness every time.
                    cr[v] = vtx[6]; cg[v] = vtx[7]; cb[v] = vtx[8];
                }
                else {
                    // Lighting: vertices without a normal (flat prims) reuse
                    // the first vertex's normal.
                    const float* n = vtx + 3;
                    if (n[0] == 0.0f && n[1] == 0.0f && n[2] == 0.0f) n = flatN;
                    TmdComputeLight(&g_tmdLight[i], n, M, &cr[v], &cg[v], &cb[v]);
                    cr[v] *= vtx[6]; cg[v] *= vtx[7]; cb[v] *= vtx[8];
                }
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
                    o[3]  = vzArr[i0];
                    o[4]  = v0[9];  o[5]  = v0[10];
                    o[6]  = cr[i0]; o[7]  = cg[i0]; o[8]  = cb[i0]; o[9]  = 1.0f;
                    o[10] = x1; o[11] = y1; o[12] = TmdDepthNdc(vzArr[i1]);
                    o[13] = vzArr[i1];
                    o[14] = v1[9];  o[15] = v1[10];
                    o[16] = cr[i1]; o[17] = cg[i1]; o[18] = cb[i1]; o[19] = 1.0f;
                    o[20] = x2; o[21] = y2; o[22] = TmdDepthNdc(vzArr[i2]);
                    o[23] = vzArr[i2];
                    o[24] = v2[9];  o[25] = v2[10];
                    o[26] = cr[i2]; o[27] = cg[i2]; o[28] = cb[i2]; o[29] = 1.0f;
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

        static float triVerts[TMD_MAX_TRIS_FLUSH * TMD_TRI_FLOATS];
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
            memcpy(triVerts + triCount * TMD_TRI_FLOATS, t3->v, sizeof(t3->v));
            triCount++;
        }
        if (triCount > 0) {
            Marni_DX()->DrawTriangles3D(triVerts, triCount, (MarniHandle)triTex,
                                        MARNI_SAMPLER_POINT, MARNI_BLEND_ALPHA);
        }
    }

    g_tmdQueueCount = 0;
}

// (0x00481660) - Update entity lighting from RDT point lights
// Recomputes the 3 D3D lights based on the entity's distance to each RDT
// light. Lights with lightType == 0 are point lights with radial falloff
// (direction = light->entity, color attenuated by distance); the others are
// used as-is (directional).
void update_entity_lighting(VECTOR* entityPos)
{
    if (g_RdtPointer == NULL) return;

    for (int i = 0; i < 3; i++) {
        RDT_Light* light = &g_RdtPointer->lights[i];
        if (light->lightType == 0) {
            struct { int x, y, z; unsigned char r, g, b; } pointLight;
            pointLight.x = entityPos->x - light->pos_x;
            pointLight.y = entityPos->y - light->pos_y;
            pointLight.z = entityPos->z - light->pos_z;

            int atten = (int)(unsigned short)light->radius -
                        SquareRoot0(pointLight.z * pointLight.z +
                                    pointLight.x * pointLight.x);
            if (atten < 0) atten = 0;

            if ((unsigned short)light->radius == 0) {
                pointLight.r = pointLight.g = pointLight.b = 0;
            }
            else {
                pointLight.r = (unsigned char)((light->red   * atten) / (int)(unsigned short)light->radius);
                pointLight.g = (unsigned char)((light->green * atten) / (int)(unsigned short)light->radius);
                pointLight.b = (unsigned char)((light->blue  * atten) / (int)(unsigned short)light->radius);
            }
            FUN_0040ac80(i, &pointLight);
        }
        else {
            FUN_0040ac80(i, light);
        }
    }
}

// (0x0048c350) - Render entity joints with lighting (in-game entity renderer)
// Per-joint loop: computes camera-space matrices, sets light/rot matrices,
// and queues each visible joint's TMD object for rendering. Skipped for
// entity types 0x0D/0x12 with sub-type 1 (they render elsewhere).
void calc_entity_lighting(Entity* ent)
{
    int param_1 = (int)ent;
    unsigned char* entBytes = (unsigned char*)ENTITY;

    if (((entBytes[1] == 0x0D) || (entBytes[1] == 0x12)) && (entBytes[2] == 1)) {
        return;
    }

    g_animFrameIdSave = (unsigned int)((*(unsigned char*)(param_1 + 3) & 0x7f) == 0);

    unsigned char jointIdx = *(char*)(param_1 + 0x8d) - 1;
    MATRIX* pJoint = (MATRIX*)((unsigned int)jointIdx * 0x7c + *(int*)(param_1 + 0x98));

    update_entity_lighting((VECTOR*)(param_1 + 0x34));

    do {
        short jointFlags = pJoint->m[0][0];

        if ((entBytes[1] == 0x0D) || (entBytes[1] == 0x12)) {
            update_entity_lighting((VECTOR*)(pJoint[2].t + 1));
        }

        if ((jointFlags & 4) != 0) {
            g_svecScratch.x = 0;
            g_svecScratch.z = 0;
            g_svecScratch.y = 0x1e;
            pJoint->m[0][2] = -0x14;
            pJoint->m[1][1] = 0;
            pJoint->m[1][0] = 200;
            FUN_004896c0(pJoint, (short)0xffdd, (short)0xff9c, 1);
        }

        if ((jointFlags & 1) == 0) {
            if ((jointFlags & 0x20) != 0) {
                FUN_0048a210(pJoint);
            }
        }
        else {
            MATRIX localMatrix;
            ApplyLVAndMul0Matrix(&g_RoomCameraData, pJoint[2].m[0] + 2, &localMatrix);

            // Copy g_lightMatrix to g_matrixScratch
            MATRIX* src = &g_lightMatrix;
            MATRIX* dst = &g_matrixScratch;
            for (int i = 8; i != 0; i--) {
                *(unsigned int*)dst->m[0] = *(unsigned int*)src->m[0];
                src = (MATRIX*)(src->m[0] + 2);
                dst = (MATRIX*)(dst->m[0] + 2);
            }

            if (g_animFrameIdSave == 0) {
                if ((jointFlags & 0x74) != 0) goto checkSwitchZone;
doRender:
                // Original skips a specific hunter in stage 6 / room 0xC / camera 3
                if (((entBytes[1] != 18) || (g_stageId != 6)) ||
                    ((g_roomId != 0x0C) || (g_roomCameraId != 3))) {
                    g_entityJointPosX = pJoint->t[0];
                    SetLightMatrix(&g_matrixScratch);
                    SetRotAndTransMatrix(&localMatrix);
                    FUN_00483250(0, 0, 0, pJoint->t[1], 0, 4,
                        (BYTE*)&g_spriteAnimSlots[2] + (unsigned int)g_spriteAnimActive * 0x14);
                }
            }
            else if ((jointFlags & 0x74) != 0) {
checkSwitchZone:
                if (is_entity_in_switch_zone((VECTOR*)(pJoint[2].t + 1), g_CurrentRdtDataTypePtr) != 0) {
                    goto doRender;
                }
            }
        }

        pJoint = (MATRIX*)(pJoint[-4].m[0] + 2);
        bool done = (jointIdx == 0);
        jointIdx--;
        if (!done) continue;
        return;
    } while (true);
}


// (0x00483250) - Entity sprite rendering helper
// Forwards joint sprite data and depth shift to the TMD renderer.
void FUN_00483250(int p0, int p1, int p2, int p3, int p4, int p5, void* p6)
{
    // Assembly: MOV EAX,[ESP+0x18]; MOV ECX,[ESP+0x10]; PUSH EAX; PUSH ECX; CALL FUN_00483080
    FUN_00483080((void*)p3, p5);
}

// (0x0048cc50) - Build view matrix from eye/target positions
static unsigned int FUN_0048cc50(float* eyeTarget, float* eyePos, float* outMatrix)
{
    float dx = eyePos[0] - eyeTarget[0];
    float dy = eyePos[1] - eyeTarget[1];
    float dz = eyePos[2] - eyeTarget[2];
    float len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (len == 0.0f) len = 1.0f;
    float invLen = 1.0f / len;
    float ny = -(dy * invLen);
    float horiz = sqrtf(1.0f - ny * ny);
    float nx, nz;
    if (horiz == 0.0f) {
        nx = 0.0f;
        nz = 1.0f;
    } else {
        nx = -((dx * invLen) / horiz);
        nz = (dz * invLen) / horiz;
    }
    outMatrix[0] = nz;      outMatrix[4] = 0.0f;  outMatrix[8]  = nx;
    outMatrix[1] = -(nx * ny); outMatrix[5] = horiz; outMatrix[9]  = nz * ny;
    outMatrix[2] = -(nx * horiz); outMatrix[6] = -ny; outMatrix[10] = nz * horiz;
    outMatrix[12] = outMatrix[0] * dx + outMatrix[8] * dz;
    outMatrix[13] = outMatrix[1] * dx + outMatrix[5] * dy + outMatrix[9] * dz;
    outMatrix[14] = outMatrix[2] * dx + outMatrix[6] * dy + outMatrix[10] * dz;
    outMatrix[3] = 0.0f; outMatrix[7] = 0.0f; outMatrix[11] = 0.0f; outMatrix[15] = 1.0f;
    return 1;
}

// (0x0048c730) - 4x4 matrix multiply (rotation part only, 3x3)
static void FUN_0048c730(float* a, float* b, float* out)
{
    out[0]  = a[0]*b[0] + a[1]*b[4] + a[2]*b[8];
    out[1]  = a[0]*b[1] + a[1]*b[5] + a[2]*b[9];
    out[2]  = a[0]*b[2] + a[1]*b[6] + a[2]*b[10];
    out[4]  = a[4]*b[0] + a[5]*b[4] + a[6]*b[8];
    out[5]  = a[4]*b[1] + a[5]*b[5] + a[6]*b[9];
    out[6]  = a[4]*b[2] + a[5]*b[6] + a[6]*b[10];
    out[8]  = a[8]*b[0] + a[9]*b[4] + a[10]*b[8];
    out[9]  = a[8]*b[1] + a[9]*b[5] + a[10]*b[9];
    out[10] = a[8]*b[2] + a[9]*b[6] + a[10]*b[10];
}

// (0x0048c820) - Transform translation vector by rotation matrix
static void FUN_0048c820(float* translation, float* rotMatrix)
{
    float x = translation[0], y = translation[1], z = translation[2];
    translation[0] = rotMatrix[0]*x + rotMatrix[4]*y + rotMatrix[8]*z;
    translation[1] = rotMatrix[1]*x + rotMatrix[5]*y + rotMatrix[9]*z;
    translation[2] = rotMatrix[2]*x + rotMatrix[6]*y + rotMatrix[10]*z;
}

// (0x00486190) - Camera/projection matrix setup
// Builds view matrix from camera parameters and composites with the model matrix.
// The original builds the two input vectors as
//   from = (0, 0, -g_sceneRenderParam)
//   to   = (0xA0 - subpixelX, subpixelY - 0x78, 0)
// so the direction handed to FUN_0048cc50 (to - from) has a POSITIVE Z of
// g_sceneRenderParam. An earlier revision folded -g_sceneRenderParam into `to`
// and left `from` at the origin, which negated the Z axis (an extra 180 degree
// yaw) and only happened to look right while the subpixel offset was exactly
// the screen centre.
static void FUN_00486190(float* modelMatrix)
{
    float from[3];
    from[0] = 0.0f;
    from[1] = 0.0f;
    from[2] = (float)-g_sceneRenderParam;

    float to[3];
    to[0] = (float)(0xA0 - g_SubpixelOffsetX);
    to[1] = (float)(g_SubpixelOffsetY + (-0x78));
    to[2] = 0.0f;

    float viewMatrix[16];
    FUN_0048cc50(from, to, viewMatrix);
    FUN_0048c730(modelMatrix, viewMatrix, modelMatrix);
    FUN_0048c820(modelMatrix + 12, viewMatrix);
}

// (0x00482fa0) - Copy light data to TMD render object and insert into ordering table
static void FUN_00482fa0(void* spriteData, int depthShift)
{
    if (spriteData == NULL || g_gteRotTransMatrix.t[2] < 0) return;

    int depth = g_gteRotTransMatrix.t[2] >> (depthShift & 0x1F);
    int* data = (int*)spriteData;
    float* lightDst = (float*)((unsigned char*)data + 0x24);
    DWORD* pLight = g_d3dLightData;

    for (int i = 0; i < 3; i++) {
        memcpy(lightDst, pLight, 12 * sizeof(DWORD));
        if (data[6] != 0) {
            lightDst[6] = (float)((data[6] & 0xFF0000) >> 16);
            lightDst[7] = (float)((data[6] >> 8) & 0xFF);
            lightDst[8] = (float)(data[6] & 0xFF);
        }
        OT_InsertPrimitive(lightDst, depth);
        pLight += 12;
        lightDst += 12;
    }
}

// (0x00483080) - Main TMD entity render function
// Reads GTE state buffers, creates TMD object, builds transform matrix, renders
void FUN_00483080(void* spriteData, int depthShift)
{
    int depthField = g_gteRotTransMatrix.t[2];
    if (spriteData == NULL || depthField < 0) {
        return;
    }

    int depth = depthField >> (depthShift & 0x1F);
    int* data = (int*)spriteData;

    FUN_00482fa0(spriteData, depthShift);

    // data[1] is the minimum CLUT depth of the animation slot (FindMinClutDepth)
    // and doubles as the texture bank id; zero means the object carries no
    // textured primitives and is not rendered.
    if (data[1] == 0) {
        return;
    }

    if (data[4] == 1) {
        FUN_00486df0(spriteData);
        return;
    }

    unsigned int tmdObj = AsyncCreateTmdObject(data[1], data[0], (unsigned int)spriteData);
    data[8] = tmdObj;
    if (tmdObj == 0) {
        return;
    }

    // Build 4x4 transform matrix from GTE rotation/translation buffer.
    // Column layout, matching the original store order at 0x004830ef:
    // GTE row 0 (m[0][0..2]) lands in M[0], M[4], M[8], so the consumer reads
    // a transformed X as M[0]*x + M[4]*y + M[8]*z. An earlier revision wrote
    // m[0][1] to M[1] etc., i.e. the transposed (inverse) rotation.
    float transformMatrix[16];
    float scale = 0.00024414063f; // 1/4096

    transformMatrix[0]  = (float)g_gteRotTransMatrix.m[0][0] * scale;
    transformMatrix[4]  = (float)g_gteRotTransMatrix.m[0][1] * scale;
    transformMatrix[8]  = (float)g_gteRotTransMatrix.m[0][2] * scale;
    transformMatrix[1]  = (float)g_gteRotTransMatrix.m[1][0] * scale;
    transformMatrix[5]  = (float)g_gteRotTransMatrix.m[1][1] * scale;
    transformMatrix[9]  = (float)g_gteRotTransMatrix.m[1][2] * scale;
    transformMatrix[2]  = (float)g_gteRotTransMatrix.m[2][0] * scale;
    transformMatrix[6]  = (float)g_gteRotTransMatrix.m[2][1] * scale;
    transformMatrix[10] = (float)g_gteRotTransMatrix.m[2][2] * scale;
    transformMatrix[12] = (float)g_gteRotTransMatrix.t[0];
    transformMatrix[13] = (float)g_gteRotTransMatrix.t[1];
    transformMatrix[14] = (float)depthField;
    transformMatrix[3]  = 0.0f;
    transformMatrix[7]  = 0.0f;
    transformMatrix[11] = 0.0f;
    transformMatrix[15] = 1.0f;

    FUN_00486190(transformMatrix);

    // Call CMarniDirect3DTMD::Transform(ctx, depth, matrix, doubleBuffer=0)
    // Original: Direct3DTMD_Transform(g_pMarniDirect3D, iVar2, &local_40, 0)
    // (ECX = [spriteData+0x20] = the TMD object handle; depth is the OT depth,
    // NOT the matrix — an earlier revision passed the matrix as arg 2, which
    // left every object with a garbage depth and no stored transform).
    CMarniDirect3DTMD* tmd = (CMarniDirect3DTMD*)(void*)tmdObj;
    tmd->Transform(g_pMarniDirect3D, (void*)(size_t)depth, transformMatrix, 0);
}

// ============================================================================
// Room item / 3D object per-frame rendering (0x00473ff0 -> 0x004745f0 ->
// 0x00483270). This is the per-frame pass the original runs between the player
// update and the entity render in game_loop; the port had it as an empty stub
// in EngineStubs.cpp, so the RDT's item models (g_itemboxes_covers_table) and
// obstacle models (g_desks_pointers_table) were never queued and FlushTmdObjects
// only ever drew entities.
//
// Item/desk record layout (0xA4 bytes, one per RDT model slot):
//   +0x00  flags byte (bit 0 = visible, bit 7 = coarse depth shift 10)
//   +0x01  model type id (low 6 bits)
//   +0x04  pointer to the +0x88 sub-record
//   +0x0C  anim field: [0] = 0x40000000 flags, [1] = ScaMatrixData ptr,
//          [2] = AnimSlot ptr (written by SetAnimSlot via FUN_00473ea0),
//          [3] = spriteData ptr (written by CreateAnimObject)
//   +0x1C  ScaMatrixData (field_00 = "world recomputed" dirty flag)
//   +0x20  rotation MATRIX (rebuilt from the +0x72 SVECTOR every frame)
//   +0x34  position VECTOR (doubles as the matrix translation)
//   +0x54  position used for the camera switch-zone cull
//   +0x72  rotation SVECTOR (RotMatrix input)
// ============================================================================

// (0x00483270) - Render one room object into the TMD queue
// objPtr points at the record's anim field (+0x0C); the spriteData block hangs
// off its +0x0C (record +0x18) where CreateAnimObject stored it. Reads the GTE
// rotation/translation buffer (set by SetRotAndTransMatrix in the caller) for
// the transform, exactly like the entity render FUN_00483080. Depth is normally
// GTE t[2] >> shift; DAT_00ae9ef8 / DAT_00ae9ee4 pin it to the fixed 0x32/0x33
// ordering-table slot for the rooms that need it.
static void FUN_00483270(unsigned char* objPtr, int depthShift)
{
    // spriteData = *(record + 0x18): the animation object CreateAnimObject
    // built when the SCD command bound the TMD (FUN_00473ea0).
    int* spriteData = *(int**)(objPtr + 0xc);

    if (spriteData == NULL) return;

    int depth = (DAT_00ae9ee4 != 0) ? 0x33 : 0x32;
    if (DAT_00ae9ef8 == 0) {
        int depthField = g_gteRotTransMatrix.t[2];
        if (depthField < 0) return;
        depth = depthField >> (depthShift & 0x1F);
    }

    // 0x004832cc-0x00483358: per-light colour override records copied into
    // spriteData+0x24 and OT_InsertPrimitive'd at `depth`. The DX11 port's
    // ordering table only consumes depth 0xFFF (the background) and the flush
    // latches the live g_d3dLightData at queue time (TmdQueueObject), so the
    // copy is a no-op here — same call as FUN_00482fa0 in the entity path.

    // data[1] is the minimum CLUT depth (texture bank id); zero means the
    // object carries no textured primitives and is not rendered.
    if (spriteData[1] == 0) return;

    if (spriteData[4] == 1) {
        FUN_00486df0(spriteData);
        return;
    }

    unsigned int tmdObj = AsyncCreateTmdObject(spriteData[1], spriteData[0], (unsigned int)spriteData);
    spriteData[8] = (int)tmdObj;
    if (tmdObj == 0) return;

    // Build the transform from the GTE buffer — same layout as FUN_00483080.
    float m[16];
    float scale = 0.00024414063f; // 1/4096
    m[0]  = (float)g_gteRotTransMatrix.m[0][0] * scale;
    m[4]  = (float)g_gteRotTransMatrix.m[0][1] * scale;
    m[8]  = (float)g_gteRotTransMatrix.m[0][2] * scale;
    m[1]  = (float)g_gteRotTransMatrix.m[1][0] * scale;
    m[5]  = (float)g_gteRotTransMatrix.m[1][1] * scale;
    m[9]  = (float)g_gteRotTransMatrix.m[1][2] * scale;
    m[2]  = (float)g_gteRotTransMatrix.m[2][0] * scale;
    m[6]  = (float)g_gteRotTransMatrix.m[2][1] * scale;
    m[10] = (float)g_gteRotTransMatrix.m[2][2] * scale;
    m[12] = (float)g_gteRotTransMatrix.t[0];
    m[13] = (float)g_gteRotTransMatrix.t[1];
    m[14] = (float)g_gteRotTransMatrix.t[2];
    m[3] = 0.0f; m[7] = 0.0f; m[11] = 0.0f; m[15] = 1.0f;

    FUN_00486190(m);

    CMarniDirect3DTMD* tmd = (CMarniDirect3DTMD*)(void*)tmdObj;
    tmd->Transform(g_pMarniDirect3D, (void*)(size_t)depth, m, 0);
}

// (0x00484eb0) 
// FUN_00486190 view rotation — the original transforms the raw GTE matrix.
static void FUN_00484eb0(void)
{
    int depth = g_gteRotTransMatrix.t[2] >> 6;
    if (depth < 0) depth = 0;
    if (depth > 0xffa) depth = 0xffa;

    float m[16];
    float scale = 0.00024414063f; // 1/4096
    m[0]  = (float)g_gteRotTransMatrix.m[0][0] * scale;
    m[4]  = (float)g_gteRotTransMatrix.m[0][1] * scale;
    m[8]  = (float)g_gteRotTransMatrix.m[0][2] * scale;
    m[1]  = (float)g_gteRotTransMatrix.m[1][0] * scale;
    m[5]  = (float)g_gteRotTransMatrix.m[1][1] * scale;
    m[9]  = (float)g_gteRotTransMatrix.m[1][2] * scale;
    m[2]  = (float)g_gteRotTransMatrix.m[2][0] * scale;
    m[6]  = (float)g_gteRotTransMatrix.m[2][1] * scale;
    m[10] = (float)g_gteRotTransMatrix.m[2][2] * scale;
    m[12] = (float)g_gteRotTransMatrix.t[0];
    m[13] = (float)g_gteRotTransMatrix.t[1];
    m[14] = (float)g_gteRotTransMatrix.t[2];
    m[3] = 0.0f; m[7] = 0.0f; m[11] = 0.0f; m[15] = 1.0f;

    CMarniDirect3DTMD* tmd = (CMarniDirect3DTMD*)g_renderStateTMD;
    tmd->Transform(g_pMarniDirect3D, (void*)(size_t)depth, m, 0);
}

// (0x00485000) - schedule the dining-hall table render asynchronously
static void FUN_00485000(void)
{
    ExecAsync((void*)FUN_00484eb0);
}

// (0x004745f0) - Render one item/desk record: compose matrices, set lights,
// cull against the camera switch zones, and queue the object's TMD.
static void RoomObjectRender(unsigned char* obj)
{
    MATRIX localMatrix;

    // 0x004745fd: per-object lighting from its world position (record +0x34,
    // which doubles as the +0x20 rotation matrix's translation).
    update_entity_lighting((VECTOR*)(obj + 0x34));

    // 0x0047460e: compose the ScaMatrixData chain (rooted at record +0x10)
    // into localMatrix, then fold in the camera.
    FUN_00483580(*(int**)(obj + 0x10), &localMatrix);

    // 0x00474624: object light matrix = g_lightMatrix * obj rotation matrix
    MulMatrix0(&g_lightMatrix, (MATRIX*)(obj + 0x20), &g_matrixScratch);
    SetLightMatrix(&g_matrixScratch);

    // 0x0047465f: stage 2 room 3 item models 1-4 (pass 0 only) sit 1000 units
    // further along the view axis — the room's shelf displays.
    if ((g_stageId == 2) && (g_roomId == 3) && (DAT_008f8688 == 0)) {
        int t = obj[1] & 0x3f;
        if (t != 0 && t < 5) {
            localMatrix.t[2] += 1000;
        }
    }

    // 0x0047466e: DAT_00ae9ee4 — the stage-1 rooms 0xA/0xB open-lid model
    // renders at the fixed depth 0x33 instead of 0x32.
    DAT_00ae9ee4 = 0;
    int stageMod = g_stageId % 5;
    if (stageMod == 1) {
        if ((g_roomId == 0x0a) && (DAT_008f8688 == 0) && ((obj[1] & 0x3f) == 1)) DAT_00ae9ee4 = 1;
        if ((g_roomId == 0x0b) && (DAT_008f8688 == 0) && ((obj[1] & 0x3f) == 1)) DAT_00ae9ee4 = 1;
    }

    // 0x004746d2-0x004747b7: room-specific objects that must not render
    // (mirror/door-frame stand-ins the SCD keeps for interaction but that
    // have their own model elsewhere, e.g. the stage-3 room 6 mirror).
    bool skip = false;
    if ((g_stageId == 3) && (g_roomId == 6) && (g_roomCameraId == 4) &&
        (DAT_008f8688 == 0) && ((obj[1] & 0x3f) == 0)) {
        skip = true;
    } else if ((g_stageId == 4) && (g_roomId == 10) &&
               ((g_roomCameraId == 0) || (g_roomCameraId == 4)) &&
               (DAT_008f8688 == 0) && ((obj[1] & 0x3f) == 0)) {
        skip = true;
    } else if ((stageMod == 0) && (g_roomId == 0x15) && (g_roomCameraId == 0) &&
               (DAT_008f8688 == 0) && ((obj[1] & 0x3f) == 0) &&
               (*(int*)(obj + 0x34) == 0x12fc) &&
               (*(int*)(obj + 0x38) == -0x2828) &&
               (*(int*)(obj + 0x3c) == 0x12fc)) {
        skip = true;
    } else if ((stageMod == 2) && (g_roomId == 0x0f) && (g_roomCameraId == 3) &&
               (DAT_008f8688 == 0) && ((obj[1] & 0x3f) == 0) &&
               (*(int*)(obj + 0x34) <= 0x7274)) {
        skip = true;
    }
    if (skip) return;

    // 0x004747b7-0x0047488b: DAT_00ae9ef8 — keep the fixed ordering-table
    // depth for these rooms instead of sorting by GTE t[2].
    DAT_00ae9ef8 = 0;
    int stageModP1 = (g_stageId + 1) % 5;
    if (stageModP1 == 4) {
        if ((g_roomId == 0x0d) || (g_roomId == 0x0f) || (g_roomId == 0x0e) ||
            (g_roomId == 0x10) || (g_roomId == 0x11)) {
            DAT_00ae9ef8 = 1;
        }
    }
    if ((stageModP1 == 3) && (g_roomId == 1)) DAT_00ae9ef8 = 1;
    if ((stageMod == 1) && (g_roomId == 0x0b) && (DAT_008f8688 == 0) && ((obj[1] & 0x3f) < 2)) DAT_00ae9ef8 = 1;

    // 0x00474890: push the composed matrix into the GTE rotation/translation
    // buffer — FUN_00483270 reads the transform from there.
    SetRotAndTransMatrix(&localMatrix);

    // 0x004748a2: cull objects outside the current camera's switch-zone group.
    if (is_entity_in_switch_zone((VECTOR*)(obj + 0x54), g_CurrentRdtDataTypePtr) == 0) return;

    // 0x004748d2
    // path (FUN_00485000) with a clamped depth.
    if ((stageModP1 == 2) && (g_roomId == 0x0a) && ((obj[1] & 0x3f) == 0)) {
        FUN_00485000();
    }

    // 0x004748d7: records with bit 7 set use the coarse depth shift (10);
    // everything else uses 4.
    if ((obj[0] & 0x80) != 0) {
        FUN_00483270(obj + 0xc, 10);
    } else {
        FUN_00483270(obj + 0xc, 4);
    }
}

// (0x00473ff0) - room_camera_and_lighting_update
// Per-frame render pass for the room's own 3D content. Pass 0 walks the item
// models (g_itemboxes_covers_table, count = RDT sound_banks_count), pass 1 the
// desk/obstacle models (g_desks_pointers_table, count = RDT unknown_03[0]).
// Each visible record with a bound model gets its rotation matrix rebuilt from
// its +0x72 SVECTOR, its ScaMatrixData marked dirty for the compose, and is
// handed to RoomObjectRender. Called from game_loop while
// g_dwCameraLightingEnabled is set; was an empty stub, which is why room items
// and 3D objects never appeared.
void room_camera_and_lighting_update(void)
{
    // One-shot per room: report the item/desk pass totals so a room with no
    // models is distinguishable from one whose models fail to bind.
    static int s_lastDiagRoom = -1;
    if (s_lastDiagRoom != g_roomId) {
        s_lastDiagRoom = g_roomId;
        dbg_printf("[roomobj] stage %d room %d: %d items, %d desks\n",
                   g_stageId, g_roomId,
                   g_RdtPointer->sound_banks_count, g_RdtPointer->unknown_03[0]);
    }

    for (int pass = 0; pass < 2; pass++) {
        int count = (pass == 0)
            ? g_RdtPointer->sound_banks_count
            : g_RdtPointer->unknown_03[0];
        DAT_008f8688 = pass;

        for (int i = 0; i < count; i++) {
            unsigned char* obj = (pass == 0)
                ? (unsigned char*)g_itemboxes_covers_table[i]
                : (unsigned char*)g_desks_pointers_table[i];

            // Visible (bit 0) and with a bound model (AnimSlot ptr at +0x14).
            if ((obj != NULL) && ((*obj & 1) != 0) && (*(int*)(obj + 0x14) != 0)) {
                // 0x0047405b: rebuild the rotation matrix from the record's
                // SVECTOR; 0x00474063: mark the ScaMatrixData dirty so
                // FUN_00483580 recomposes it.
                RotMatrix((SVECTOR*)(obj + 0x72), (MATRIX*)(obj + 0x20));
                *(int*)(obj + 0x1c) = 0;
                RoomObjectRender(obj);
            }
        }
    }
}

