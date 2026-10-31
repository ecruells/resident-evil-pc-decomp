// MarniSystem.cpp - Marni System wrapper implementation.
//
// Originally this file contained a mixed DirectX 5.0-API / D3D11 backend with
// raw ID3D11* pointers scattered everywhere. Every D3D11 resource has been
// moved into the MarniDX shim (MarniDX.cpp), and this file now only forwards
// the CMarniDirect3D 12-entry vtable + C-style Marni* wrappers to MarniDX.
// The game layer sees the same CMarniDirect3D class ABI and C functions as
// before, but <d3d11.h> is no longer transitively included from here.
//
// Original class: CMarniDirect3D at vtable 0x004af230, size 0x21DC
// Original functions referenced by address in comments.

#include <cstdlib>                   // malloc/free - MSVC got this via <windows.h>
#include "../platform/platform.h"
#include "MarniSystem.h"
#include "MarniBits.h"
#include "MarniDX.h"
#include "MarniInput.h"
#include "MarniXInput.h"
#include "Globals.h"
#include "../game/TmdRenderer.h"
#include "../system/HdTextures.h"
#include <cstdio>
#include <cstring>
#include <new>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Verify the struct size is exactly what the original binary expects.
// operator_new(0x21DC) in InitializeMarniSystem must match sizeof.
static_assert(sizeof(CMarniDirect3D) == 0x21DC, "CMarniDirect3D size mismatch — padding fix needed");

// ============================================================================
// VTable function pointer types
// (unchanged — WindowProc.cpp / TmdAnimation.cpp cast vtable slots to these)
// ============================================================================
typedef int  (*PFN_RequestVideoMemory)(void* self);
typedef int  (*PFN_ChangeDisplayMode)(void* self, int mode);
typedef void (*PFN_SetD3DRenderer)(void* self, int renderer);
typedef int  (*PFN_Clear)(void* self);
typedef int  (*PFN_Present)(void* self);
typedef int  (*PFN_HandleWindowMessage)(void* self, HWND hwnd, UINT msg,
                                        WPARAM wParam, LPARAM lParam);
typedef int  (*PFN_CreateTextureHandle)(void* self, void* texDesc,
                                        unsigned int flags, void* outHandle);
typedef unsigned int (*PFN_CreateObjectHandle)(void* self, void* objDesc,
                                                unsigned char flags);
typedef int  (*PFN_DeleteTextureHandle)(void* self, int handle);
typedef int  (*PFN_DeleteObjectHandle)(void* self, int handle);
typedef int  (*PFN_SetTexture)(void* self, void* texData, unsigned int param);
typedef int  (*PFN_ResetTextures)(void* self);

// ============================================================================
// Forward declarations of vtable function implementations
// ============================================================================
static int  VTable_RequestVideoMemory(void* self);
static int  VTable_ChangeDisplayMode(void* self, int mode);
static void VTable_SetD3DRenderer(void* self, int renderer);
static int  VTable_Clear(void* self);
static int  VTable_Present(void* self);
static int  VTable_HandleWindowMessage(void* self, HWND hwnd, UINT msg,
                                       WPARAM wParam, LPARAM lParam);
static int  VTable_CreateTextureHandle(void* self, void* texDesc,
                                       unsigned int flags, void* outHandle);
static unsigned int VTable_CreateObjectHandle(void* self, void* objDesc,
                                              unsigned char flags);
static int  VTable_DeleteTextureHandle(void* self, int handle);
static int  VTable_DeleteObjectHandle(void* self, int handle);
static int  VTable_SetTexture(void* self, void* texData, unsigned int param);
static int  VTable_ResetTextures(void* self);

// ============================================================================
// Static vtable (12 entries, matching original layout at 0x004af230)
// The callers in WindowProc.cpp / TmdAnimation.cpp / ObjectManager.cpp walk
// this by index, so order is frozen.
// ============================================================================
static void* g_CMarniDirect3D_VTable[12] = {
    (void*)VTable_RequestVideoMemory,   // [0] 0x00448630
    (void*)VTable_ChangeDisplayMode,    // [1] 0x00449300
    (void*)VTable_SetD3DRenderer,       // [2] 0x0044a0d0
    (void*)VTable_Clear,                // [3] 0x0044b320
    (void*)VTable_Present,              // [4] 0x00448ff0
    (void*)VTable_HandleWindowMessage,  // [5] 0x00448b60
    (void*)VTable_CreateTextureHandle,  // [6] 0x0044c900
    (void*)VTable_CreateObjectHandle,   // [7] 0x0044af90
    (void*)VTable_DeleteTextureHandle,  // [8] 0x0044b220
    (void*)VTable_DeleteObjectHandle,   // [9] 0x0044b1c0
    (void*)VTable_SetTexture,           // [10] 0x00448300
    (void*)VTable_ResetTextures,        // [11] 0x00448380
};

// ============================================================================
// Constructor / Destructor
// Original C'tor: 0x0044baf0
// ============================================================================
static CMarniDirect3D* MarniDirect3D_Construct(CMarniDirect3D* pThis,
    HWND hWnd, int width, int height, int modeID, int adapterID)
{
    if (!pThis) return NULL;

    // Set vtable
    pThis->vtable = g_CMarniDirect3D_VTable;

    // Basic fields (matching original offsets)
    // Original base ctor (0x0044efc0): field_0x8/0xc (logical resolution) are
    // initialized to the same width/height as the physical surface (0x10/0x14);
    // SetVideoResolution later toggles ONLY the logical pair.
    pThis->m_hWnd         = (DWORD)hWnd;
    pThis->m_logicalWidth = (DWORD)width;
    pThis->m_logicalHeight= (DWORD)height;
    pThis->m_width        = (DWORD)width;
    pThis->m_height       = (DWORD)height;
    pThis->m_bitDepth     = (g_dwBitDepth == 16) ? 16 : 32;
    pThis->m_isInitialized = FALSE;
    pThis->m_isFullScreen  = g_bFullScreen;
    pThis->m_isActive      = TRUE;
    pThis->m_selectedMode  = (DWORD)modeID;
    pThis->m_deviceType    = (DWORD)adapterID;
    pThis->m_currentMode   = (DWORD)modeID;
    pThis->m_scratch       = 0;

    // Font fields
    pThis->m_FontTexHandle = MARNI_NULL_HANDLE;
    pThis->m_FontTexWidth  = 0;
    pThis->m_FontTexHeight = 0;

    // Clamp
    if (pThis->m_width  < 320) pThis->m_width  = 640;
    if (pThis->m_height < 240) pThis->m_height = 480;

    // Delegate D3D11 creation to MarniDX
    OutputDebugStringA("[Marni] Creating D3D11 device...\n");
    pThis->m_pDX = MarniDX_Create();
    if (pThis->m_pDX) {
        int actualW, actualH;
        if (pThis->m_pDX->Create(hWnd, (int)pThis->m_width,
                                 (int)pThis->m_height,
                                 pThis->m_isFullScreen,
                                 &actualW, &actualH)) {
            pThis->m_width  = (DWORD)actualW;
            pThis->m_height = (DWORD)actualH;
            pThis->m_isInitialized = TRUE;
            OutputDebugStringA("[Marni] D3D11 device created OK\n");
        } else {
            OutputDebugStringA("[Marni] D3D11 device creation FAILED\n");
        }
    } else {
        OutputDebugStringA("[Marni] MarniDX_Create failed\n");
    }

    // Pre-validate the framebuffer proxy surface so SaveBitmapToFile will
    // capture the D3D11 backbuffer when called on it (original: the
    // framebuffer CMarniBits at g_pMarniDirect3D + 0x2064 was always
    // populated by software rendering).
    g_MarniFrameBuffer.m_isValid = 1;

    return pThis;
}

// ============================================================================
// Memory operators (unchanged from original: 0x00433370, 0x004333e0)
// ============================================================================
void* operator_new(size_t size)
{
    return malloc(size);
}

void operator_delete(void* ptr)
{
    if (ptr) free(ptr);
}

// ============================================================================
// CMarniDirect3D_Constructor — C-style constructor wrapper (0x0044baf0)
// ============================================================================
void* CMarniDirect3D_Constructor(void* self, HWND hWnd, int width, int height,
                                  int modeID, int adapterID)
{
    return MarniDirect3D_Construct((CMarniDirect3D*)self, hWnd,
                                    width, height, modeID, adapterID);
}

// ============================================================================
// VTable function implementations — all forward to MarniDX.
// ============================================================================

// [0] RequestVideoMemory — 0x00448630
static int VTable_RequestVideoMemory(void* self)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)self;
    if (!pD3D || !pD3D->m_isInitialized) return 0;
    return (int)pD3D->m_pDX->QueryVideoMemory(pD3D->m_deviceType == 5);
}

// [1] ChangeDisplayMode — 0x00449300
static int VTable_ChangeDisplayMode(void* self, int mode)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)self;
    if (!pD3D || !pD3D->m_isInitialized) return 0;
    if (mode < 0 || mode >= g_NumDisplayModes) return 0;

    DisplayModeInfo* pMode = &g_DisplayModeBuffer[mode];
    int result = pD3D->m_pDX->ChangeDisplayMode(
        pMode->dwWidth, pMode->dwHeight, (pMode->dwFlags & 1) != 0);
    if (result) {
        pD3D->m_width      = pMode->dwWidth;
        pD3D->m_height     = pMode->dwHeight;
        pD3D->m_currentMode = (DWORD)mode;
        pD3D->m_isFullScreen = (pMode->dwFlags & 1) != 0;
        g_dwScreenWidth     = pMode->dwWidth;
        g_dwScreenHeight    = pMode->dwHeight;
    }
    return result;
}

// [2] SetD3DRenderer — 0x0044a0d0 (no-op in modern D3D11)
static void VTable_SetD3DRenderer(void* self, int renderer)
{
    (void)self; (void)renderer;
}

// [3] Clear — 0x0044b320
static int VTable_Clear(void* self)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)self;
    if (!pD3D || !pD3D->m_isInitialized) return 0;
    if (!pD3D->m_isActive) return 0;

    float r, g, b;
    if (g_debugClearR != 0.0f || g_debugClearG != 0.0f || g_debugClearB != 0.0f) {
        r = g_debugClearR / 255.0f;
        g = g_debugClearG / 255.0f;
        b = g_debugClearB / 255.0f;
    } else {
        r = 0.0f; g = 0.0f; b = 0.05f; // slight blue tint
    }
    pD3D->m_pDX->Clear(r, g, b, 1.0f);
    return 1;
}

// [4] Present — 0x00448ff0
static int VTable_Present(void* self)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)self;
    if (!pD3D || !pD3D->m_isInitialized) return 1;
    pD3D->m_pDX->Present();
    return 1;
}

// [5] HandleWindowMessage — 0x00448b60
static int VTable_HandleWindowMessage(void* self, HWND hwnd, UINT msg,
                                       WPARAM wParam, LPARAM lParam)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)self;

    switch (msg) {
    case WM_ACTIVATE:
        pD3D->m_isActive = (LOWORD(wParam) != WA_INACTIVE);
        return 1;
    case WM_SIZE:
    case WM_DESTROY:
        if (msg == WM_DESTROY) pD3D->m_isActive = FALSE;
        // Forward resize to MarniDX; it will also update pD3D->m_width/height
        // through GetBackBufferSize, but the WM_SIZE handler in MarniDX does
        // its own resize — read back dims afterwards.
        pD3D->m_pDX->HandleWindowMessage(hwnd, msg, wParam, lParam);
        if (msg == WM_SIZE && LOWORD(lParam) > 0) {
            DWORD nw = 0, nh = 0;
            pD3D->m_pDX->GetBackBufferSize(&nw, &nh);
            if (nw > 0)  pD3D->m_width  = nw;
            if (nh > 0)  pD3D->m_height = nh;
        }
        return 1;
    default:
        return 1;
    }
}

// ============================================================================
// STP knock-out twins — per-texel semi-transparency for TMD models.
//
// The PS1 GPU blends PER TEXEL: on a polygon with ABE set, a texel whose
// palette entry carries the STP bit blends with the framebuffer, and a texel
// whose entry does not carry it draws opaque. The DC/PS1 room models rely on
// this - room 107's display case is 80 ABE-flagged primitives over a palette
// where only entries 63 and 66 are STP, so its glass top blends and its wooden
// frame stays solid. This build has no per-texel mode, and it cannot use the
// item viewer's trick of knocking the STP texels out of the page itself
// (FUN_004842c0 / ItemStpPage_KnockOutStpTexels, MainMenu.cpp): a model page is
// shared, and every other primitive drawn from it wants those texels intact.
//
// So the knock-out becomes a SECOND TEXTURE rather than a second page. For a
// material whose palette marks some - but not most - of its used texels STP,
// ---------------------------------------------------------------------------
// Deferred HD pictures (port-only, HdTextures.h "Deferred uploads")
//
// While a room loads, a texture whose HD picture is still being decoded is
// uploaded as the original and remembered here with what turns the decoded
// picture into the final one (the whole picture, an Effspr band cut out of it,
// a recoloured effect page). MarniHd_PumpDeferred swaps each in under the same
// handle once it is decoded - the door animation and the load message are
// what the decoding hides behind, as the CD read did on the PlayStation - and
// MarniHd_DeferredPending tells the door when everything has arrived.
// ---------------------------------------------------------------------------
typedef std::function<bool(const HdPixels&, int, int, std::vector<unsigned char>*, int*, int*)>
    HdBuild;

struct HdPending {
    MarniDX* dx;
    MarniHandle tex;
    unsigned int serial;
    std::string token;
    HdBuild build;
    // A model's knock-out twin, cut from the picture once it is decoded:
    // `stp` marks the original's blending texels (w x h).
    MarniHandle twin;
    unsigned int twinSerial;
    std::vector<unsigned char> stp;
    int w, h;
};
static std::vector<HdPending> s_hdPending;

static bool HdBuildWhole(const HdPixels& hd, int hdW, int hdH,
                         std::vector<unsigned char>* out, int* ow, int* oh)
{
    *out = *hd;
    *ow = hdW;
    *oh = hdH;
    return true;
}

// Part of a named page (an Effspr band): the same part of the HD page.
static bool HdBuildRegion(const HdPixels& hd, int hdW, int hdH, int rx, int ry, int rw, int rh,
                          int pageW, int pageH, std::vector<unsigned char>* out, int* ow, int* oh)
{
    if (pageW <= 0 || pageH <= 0) return false;
    const int x0 = rx * hdW / pageW, x1 = (rx + rw) * hdW / pageW;
    const int y0 = ry * hdH / pageH, y1 = (ry + rh) * hdH / pageH;
    if (x0 < 0 || y0 < 0 || x1 > hdW || y1 > hdH || x1 <= x0 || y1 <= y0) return false;
    out->resize((size_t)(x1 - x0) * (y1 - y0) * 4);
    for (int y = y0; y < y1; y++) {
        memcpy(&(*out)[(size_t)(y - y0) * (x1 - x0) * 4],
               &(*hd)[((size_t)y * hdW + x0) * 4], (size_t)(x1 - x0) * 4);
    }
    *ow = x1 - x0;
    *oh = y1 - y0;
    return true;
}

// A page that stands for a file (a room's effect-sprite page): the HD file's
// picture, recoloured where the page is the file's art in another palette row
// (a room's tinted copy of a sprite: the greenhouse fountain's water, the
// ESP203/ESP229 flares) - each channel scaled by the ratio of the page's
// colour to the file's for the texel under it, so the detail stays HD and the
// colours are the room's. A texel the page has transparent stays transparent.
static bool HdBuildPage(const HdPixels& hd, int hdW, int hdH, const unsigned int* src,
                        const unsigned int* ref, int width, int height,
                        std::vector<unsigned char>* out, int* ow, int* oh)
{
    if (hdW < width || hdH < height) return false;
    *out = *hd;
    unsigned int* dst = (unsigned int*)out->data();
    for (int y = 0; y < hdH; y++) {
        const int ty = y * height / hdH;
        for (int x = 0; x < hdW; x++) {
            const int t = ty * width + x * width / hdW;
            const unsigned int s = src[t], f = ref[t];
            if (s == f) continue;
            unsigned int& d = dst[(size_t)y * hdW + x];
            if ((s >> 24) == 0) { d = 0; continue; }
            unsigned int o = d & 0xFF000000u;
            for (int c = 0; c < 24; c += 8) {
                const unsigned int sc = (s >> c) & 0xFF, fc = (f >> c) & 0xFF;
                const unsigned int dc = (d >> c) & 0xFF;
                unsigned int v = (fc != 0) ? dc * sc / fc : sc;
                if (v > 255) v = 255;
                o |= v << c;
            }
            d = o;
        }
    }
    *ow = hdW;
    *oh = hdH;
    return true;
}

// The knock-out twin of an HD picture: every texel over a blending texel of
// the original (stp, w x h) cleared to alpha 0.
static void HdKnockOut(const unsigned char* hd, int hdW, int hdH, const unsigned char* stp,
                       int w, int h, std::vector<unsigned char>* out)
{
    out->assign(hd, hd + (size_t)hdW * hdH * 4);
    unsigned int* d = (unsigned int*)out->data();
    for (int y = 0; y < hdH; y++) {
        const unsigned char* row = stp + (size_t)(y * h / hdH) * w;
        for (int x = 0; x < hdW; x++) {
            if (row[x * w / hdW]) d[(size_t)y * hdW + x] &= 0x00FFFFFFu;
        }
    }
}

// Uploads the original and remembers `tex` for the picture behind `token`.
static MarniHandle HdUploadDeferred(MarniDX* dx, int width, int height, const void* rgba,
                                    const std::string& token, const HdBuild& build)
{
    MarniHandle tex = dx->CreateTexture(width, height, 32, rgba, NULL, NULL);
    if (tex == MARNI_NULL_HANDLE) return tex;
    HdPending p;
    p.dx = dx;
    p.tex = tex;
    p.serial = dx->TextureSerial(tex);
    p.token = token;
    p.build = build;
    p.twin = MARNI_NULL_HANDLE;
    p.twinSerial = 0;
    p.w = width;
    p.h = height;
    s_hdPending.push_back(p);
    return tex;
}

void MarniHd_PumpDeferred(void)
{
    for (size_t i = 0; i < s_hdPending.size();) {
        HdPending& p = s_hdPending[i];
        // Released since (a room left before its picture arrived): the
        // handle may already name something else.
        if (p.dx->TextureSerial(p.tex) != p.serial) {
            s_hdPending.erase(s_hdPending.begin() + i);
            continue;
        }
        HdPixels hd;
        int hdW = 0, hdH = 0;
        const int r = HdTextures_Poll(p.token, &hd, &hdW, &hdH);
        if (r == 0) { ++i; continue; }
        if (r == 1) {
            std::vector<unsigned char> pic;
            int w = 0, h = 0;
            if (p.build(hd, hdW, hdH, &pic, &w, &h)) {
                p.dx->ReplaceTexture(p.tex, w, h, pic.data());
                if (p.twin != MARNI_NULL_HANDLE && p.dx->TextureSerial(p.twin) == p.twinSerial &&
                    !p.stp.empty()) {
                    std::vector<unsigned char> knock;
                    HdKnockOut(pic.data(), w, h, p.stp.data(), p.w, p.h, &knock);
                    p.dx->ReplaceTexture(p.twin, w, h, knock.data());
                }
            }
        }
        s_hdPending.erase(s_hdPending.begin() + i);
    }
}

bool MarniHd_DeferredPending(void)
{
    MarniHd_PumpDeferred();
    return !s_hdPending.empty();
}

// ============================================================================
// CreateTextureHd (port-only)
// The one place a finished RGBA8 image becomes a renderer texture. When the HD
// texture layer (system/HdTextures.h) holds a replacement for these exact
// pixels, that image is uploaded instead - at its own resolution, which the
// callers never see: they keep the logical size and normalise UVs against it.
// While a room loads (HdTextures_Deferring) a picture still being decoded
// does not stall the upload: the original goes up and the picture follows
// (deferred HD pictures, above).
// ============================================================================
static MarniHandle CreateTextureHd(MarniDX* dx, int width, int height,
                                   const void* rgba)
{
    if (HdTextures_Active()) {
        HdPixels hd;
        int hdW = 0, hdH = 0;
        std::string token;
        std::vector<unsigned char> pic;
        int pw = 0, ph = 0;
        // An upload the caller named by its file (HdTextures_BeginSource:
        // room backgrounds and masks) is looked up by that name first.
        const char* key = HdTextures_SourceFor(width, height);
        int rx = 0, ry = 0, rw = 0, rh = 0, pageW = 0, pageH = 0;
        const bool region = key != NULL &&
                            HdTextures_SourceRegion(&rx, &ry, &rw, &rh, &pageW, &pageH);
        const unsigned int* ref = (key != NULL)
            ? (const unsigned int*)HdTextures_SourceReference() : NULL;
        if (ref != NULL) {
            const int r = HdTextures_TryNamed(key, &hd, &hdW, &hdH, &token);
            if (r == 1 && HdBuildPage(hd, hdW, hdH, (const unsigned int*)rgba, ref, width, height,
                                      &pic, &pw, &ph)) {
                MarniHandle tex = dx->CreateTexture(pw, ph, 32, pic.data(), NULL, NULL);
                if (tex != MARNI_NULL_HANDLE) return tex;
            }
            if (r == 2) {
                std::shared_ptr<std::vector<unsigned int> > src(new std::vector<unsigned int>(
                    (const unsigned int*)rgba, (const unsigned int*)rgba + width * height));
                std::shared_ptr<std::vector<unsigned int> > rf(new std::vector<unsigned int>(
                    ref, ref + width * height));
                const int w = width, h = height;
                return HdUploadDeferred(dx, width, height, rgba, token,
                    [src, rf, w, h](const HdPixels& px, int a, int b, std::vector<unsigned char>* o,
                                    int* ow, int* oh) {
                        return HdBuildPage(px, a, b, src->data(), rf->data(), w, h, o, ow, oh);
                    });
            }
        }
        if (key != NULL && !region && ref == NULL) {
            HdTextures_DumpNamed(key, width, height, rgba);
            const int r = HdTextures_TryNamed(key, &hd, &hdW, &hdH, &token);
            if (r == 1) {
                MarniHandle tex = dx->CreateTexture(hdW, hdH, 32, hd->data(), NULL, NULL);
                if (tex != MARNI_NULL_HANDLE) return tex;
            }
            if (r == 2) return HdUploadDeferred(dx, width, height, rgba, token, HdBuildWhole);
        }
        if (region && pageW > 0 && pageH > 0) {
            const int r = HdTextures_TryNamed(key, &hd, &hdW, &hdH, &token);
            if (r == 1 && HdBuildRegion(hd, hdW, hdH, rx, ry, rw, rh, pageW, pageH, &pic, &pw, &ph)) {
                MarniHandle tex = dx->CreateTexture(pw, ph, 32, pic.data(), NULL, NULL);
                if (tex != MARNI_NULL_HANDLE) return tex;
            }
            if (r == 2) {
                return HdUploadDeferred(dx, width, height, rgba, token,
                    [rx, ry, rw, rh, pageW, pageH](const HdPixels& px, int a, int b,
                                                   std::vector<unsigned char>* o, int* ow, int* oh) {
                        return HdBuildRegion(px, a, b, rx, ry, rw, rh, pageW, pageH, o, ow, oh);
                    });
            }
        }
        const int r = HdTextures_TryHash(width, height, rgba, &hd, &hdW, &hdH, &token);
        if (r == 1) {
            MarniHandle tex = dx->CreateTexture(hdW, hdH, 32, hd->data(), NULL, NULL);
            if (tex != MARNI_NULL_HANDLE) return tex;
        }
        if (r == 2) return HdUploadDeferred(dx, width, height, rgba, token, HdBuildWhole);
        // A picture the game drew at HD size itself (HdTextures_BeginPicture:
        // the title menu's text). A file for the hash above outranks it.
        const void* drawn = NULL;
        int drawnW = 0, drawnH = 0;
        if (r == 0 && HdTextures_PictureFor(width, height, rgba, &drawn, &drawnW, &drawnH)) {
            MarniHandle tex = dx->CreateTexture(drawnW, drawnH, 32, drawn, NULL, NULL);
            if (tex != MARNI_NULL_HANDLE) return tex;
        }
    }
    return dx->CreateTexture(width, height, 32, rgba, NULL, NULL);
}

// [6] CreateTextureHandle also builds a copy of the texture holding only the
// opaque texels, every STP texel forced to palette index 0 - which the
// conversion below already maps to alpha 0, exactly as the item page's knock-out
// relies on. Both bounds matter:
//
//   no STP texels at all      nothing to knock out, and nothing to blend;
//   ALL used texels STP       the whole primitive blends uniformly, which the
//                             record's blend weight (+0x68) already reproduces
//                             and whose no-depth-write it depends on. The twin
//                             would be an EMPTY copy that the opaque pass would
//                             then depth-write on behalf of a surface that had
//                             never written depth - the flooded-room water
//                             regression in reverse. (Scanned every shipped
//                             RDT: the all-STP models are ROOM10D0/60D0/80D0/
//                             D0D0 omodel[2] and ROOM7150 item[2], 32761 of
//                             32768 texels STP, plus ROOM2050/7050 omodel[0] at
//                             60%.)
//
// With both bounds the only model in the game that gains a pass is room 107's
// display case and its counterparts in rooms 108/607/608/807/D07 - and the
// aquarium shell in room 20A0, which already draws unblended through
// g_renderStateTMD (FUN_00484c40's punched page), so its pixels do not move
// either.
//
// FlushTmdObjects then draws a semi-transparent TMD object twice - the whole
// model through the texture at its blend weight, then the same geometry through
// this twin at full alpha. The opaque pass covers the blended one everywhere
// the texture is opaque, so the blend survives only where the PS1 would have
// blended: the arrangement FUN_004844c0 uses for the examine screen.
//
// Registration is by public handle, so a twin is destroyed with its source
// texture in vtable[8]. The table is a heap array rather than .bss for the same
// reason as g_tmdLight in TmdRenderer.cpp: a static this size shifts every
// global that follows it in the link.
// ============================================================================
#define MARNI_HANDLE_MAX 2048            // == MarniDX.cpp's MARNI_MAX_TEXTURES
static unsigned int* s_stpKnock = NULL;  // [MARNI_HANDLE_MAX], lazily allocated

static void MarniStpKnock_Set(unsigned int src, unsigned int twin)
{
    if (src == 0 || src >= MARNI_HANDLE_MAX) return;
    if (s_stpKnock == NULL) {
        s_stpKnock = (unsigned int*)calloc(MARNI_HANDLE_MAX, sizeof(unsigned int));
        if (s_stpKnock == NULL) return;
    }
    s_stpKnock[src] = twin;
}

// Detach the twin of `src` and return it (0 if none). The caller destroys it:
// this file has no back-pointer from a handle to the MarniDX that owns it.
static unsigned int MarniStpKnock_Take(unsigned int src)
{
    if (s_stpKnock == NULL || src == 0 || src >= MARNI_HANDLE_MAX) return 0;
    unsigned int twin = s_stpKnock[src];
    s_stpKnock[src] = 0;
    return twin;
}

MarniHandle MarniStpKnockoutTwin(MarniHandle tex)
{
    if (s_stpKnock == NULL) return MARNI_NULL_HANDLE;
    if ((unsigned int)tex == 0 || (unsigned int)tex >= MARNI_HANDLE_MAX)
        return MARNI_NULL_HANDLE;
    return (MarniHandle)s_stpKnock[(unsigned int)tex];
}

// ============================================================================
// HD model skins by file (port-only)
// A model's TIM is loaded into a PSXTexture bank (TmdProcessingCallback), and
// the renderer builds one texture per palette row from it: each is the WHOLE
// image in that palette. The loader registers the bank with the EMD's source
// key (HdTextures_SetOwnerKey), so a material asked for here is traced back to
// its bank - `bits` is material i of the bank, i * 0x68 bytes in - and the
// bank's pixel pointer must still be the one it was registered with.
//
// Every shipped EMD keeps its two palettes on separate halves: palette row 0
// is used only by the polygons on the left 128-texel page, row 1 only by the
// right one (scanned CHAR10, EM1000, EM1020; single-palette TIMs use theirs
// everywhere). So one picture serves all of a model's materials: the left half
// in palette 0 and the right half in palette 1 - what the model looks like.
// That is what is dumped and what "HD/<key>.webp" replaces, for each material.
// ============================================================================
//
// A model inside a room file (HdTextures_SetOwnerKeyPerPalette) is named per
// palette row instead: `outPerPalette` is set, and when the TIM has more than
// one row `rowKey` receives "<key>_P<row>" and is returned.
static const char* HdModelKeyFor(const CMarniBits* bits, int* outMaterial,
                                 bool* outPerPalette, char* rowKey, size_t rowKeySize)
{
    for (int i = 0; i < 8; i++) {
        const BYTE* owner = (const BYTE*)bits - i * 0x68;
        const void* tag = NULL;
        const char* key = HdTextures_OwnerKey(owner, &tag);   // no read of owner
        if (key == NULL) continue;
        if (((const CMarniBits*)owner)->m_pPixelData != tag) return NULL;
        const DWORD palettes = *(const DWORD*)(owner + 0x340);    // m_NumCLUTs
        if ((DWORD)i >= palettes) return NULL;
        *outMaterial = i;
        *outPerPalette = HdTextures_OwnerPerPalette(owner);
        if (*outPerPalette && palettes > 1) {
            snprintf(rowKey, rowKeySize, "%s_P%d", key, i);
            return rowKey;
        }
        return key;
    }
    return NULL;
}

// Which texel of a CLUT texture is the transparent one: index 0, but only while
// palette entry 0 is black (the STP bit is ignored).
//
// The original gives every texture surface a DirectDraw source colour key of
// VALUE 0 (ddrawsurface_create_with_palette, 0x004331f8: SetColorKey(SRCBLT)).
// On a palettized texture that value is index 0; on an RGB texture it is black,
// and for the flag-8 textures Direct3DTIM_Create makes (models, room objects,
// doors) d3d_reload_texture_entry (0x00449a40) first turns every black texel
// into 0x080808, so nothing is keyed at all. The two paths agree on exactly
// one case: index 0 holding black - which is how all the shipped art is drawn.
//
// Keying index 0 whatever its colour was wrong for models made for the retail
// PC / Classic REbirth (GitHub issue: a custom CHAR10.EMD): their skins put a
// real colour at index 0 - the dark blue of the trouser camo, 10158 texels -
// and it rendered as holes. A black colour key on every index would be wrong
// the other way: it cuts the dark areas out of 80 shipped room-object and
// model textures (ROOM4080's object page is 97% black). Against the old rule
// this one changes no visible texel of the USA tree: the index-0 texels it
// turns opaque sit in unused filler blocks, apart from 196 white edge texels
// of the ROOM4090 sink, whose index 0 is white.
static inline bool ClutIndexZeroIsKey(const WORD* clut)
{
    return clut != NULL && (clut[0] & 0x7FFF) == 0;
}

static DWORD HdModelTexel(const BYTE* base, int pitch, int bpp, const WORD* clut,
                          int x, int y)
{
    int idx;
    if (bpp == 8) {
        idx = base[y * pitch + x];
    } else {
        BYTE two = base[y * pitch + x / 2];
        idx = (x & 1) ? (two >> 4) : (two & 0xF);
    }
    WORD c = clut[idx];
    DWORD a = (idx == 0 && ClutIndexZeroIsKey(clut)) ? 0x00 : 0xFF;
    DWORD r = (c & 0x1F) * 255 / 31;
    DWORD g = ((c >> 5) & 0x1F) * 255 / 31;
    DWORD b = ((c >> 10) & 0x1F) * 255 / 31;
    return (a << 24) | (b << 16) | (g << 8) | r;
}

// The model's picture as described above, into a new w*h buffer (NULL when
// the bank's palettes are not readable). Caller frees with operator_delete.
static DWORD* HdModelComposite(const CMarniBits* bits, int material, int w, int h,
                               const BYTE* base, int pitch, int bpp)
{
    const BYTE* owner = (const BYTE*)bits - material * 0x68;
    const int palettes = (int)*(const DWORD*)(owner + 0x340);         // m_NumCLUTs
    const WORD* pal0 = (const WORD*)((const CMarniBits*)owner)->m_pPalette;
    const WORD* pal1 = (palettes >= 2)
                     ? (const WORD*)((const CMarniBits*)(owner + 0x68))->m_pPalette : pal0;
    const SIZE_T need = (bpp == 4) ? 16 * sizeof(WORD) : 256 * sizeof(WORD);
    if (pal0 == NULL || pal1 == NULL ||
        plat_readable_bytes(pal0) < need || plat_readable_bytes(pal1) < need) {
        return NULL;
    }
    DWORD* out = (DWORD*)operator_new((size_t)w * h * sizeof(DWORD));
    if (out == NULL) return NULL;
    const int split = (palettes >= 2) ? w / 2 : w;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            out[y * w + x] = HdModelTexel(base, pitch, bpp, x < split ? pal0 : pal1, x, y);
        }
    }
    return out;
}

// [6] CreateTextureHandle — 0x0044c900
// Converts a CMarniBits surface (PSX VRAM-format pixel data + RGB555 CLUT)
// into a D3D11 texture and returns its MarniHandle (>= 1), exactly how the
// original returned a texture slot index. Writes 1 to outHandle on success.
static int VTable_CreateTextureHandle(void* self, void* texDesc,
                                       unsigned int flags, void* outHandle)
{
    (void)flags;
    CMarniDirect3D* pD3D = (CMarniDirect3D*)self;
    CMarniBits* bits = (CMarniBits*)texDesc;
    if (!pD3D || !pD3D->m_pDX || !bits) return 0;

    int w = (int)bits->m_width;
    int h = (int)bits->m_height;
    int bpp = (int)bits->m_bitDepth;
    const WORD* pixels = (const WORD*)bits->m_pPixelData;
    const WORD* clut = (const WORD*)bits->m_pPalette;
    if (w <= 0 || h <= 0 || !pixels) return 0;

    // Row stride comes from the descriptor, exactly as CMarniBits::CalcAddress
    // (0x00403860) computes it: base + m_pitch * y + (x/2 | x | x*2) by depth.
    // PSXTexture::Store sets m_pitch = imgW*2 and m_width = imgW*4 (4bpp), so a
    // packed w*bpp/8 stride happens to agree - but only for well-formed TIMs.
    // Trust m_pitch, falling back to the packed stride when it is unset.
    const BYTE* base = (const BYTE*)pixels;
    int pitch = (int)bits->m_pitch;
    if (pitch <= 0) pitch = (bpp == 4) ? (w / 2) : (bpp == 8) ? w : (w * 2);

    // The dimensions come straight out of the TIM header, and m_pPixelData
    // aliases the loaded file buffer rather than a sized allocation, so a
    // truncated or mis-parsed image walks off the end of committed memory.
    // Ask the OS how much is actually readable and clamp instead of faulting.
    {
        int lastByteInRow = (bpp == 4) ? ((w - 1) / 2)
                          : (bpp == 8) ? (w - 1)
                                       : ((w - 1) * 2 + 1);
        SIZE_T need = (SIZE_T)pitch * (h - 1) + lastByteInRow + 1;
        SIZE_T avail = plat_readable_bytes(base);
        if (avail < need) {
            char dbg[224];
            sprintf_s(dbg, sizeof(dbg),
                      "[TEXPAGE] TRUNCATED: w=%d h=%d bpp=%d pitch=%d base=%p "
                      "clut=%p need=%zu avail=%zu\n",
                      w, h, bpp, pitch, (const void*)base, (const void*)clut,
                      need, avail);
            OutputDebugStringA(dbg);
            // Keep only the rows that are fully readable.
            int safeRows = (avail >= (SIZE_T)lastByteInRow + 1)
                         ? (int)((avail - lastByteInRow - 1) / (SIZE_T)pitch) + 1
                         : 0;
            if (safeRows <= 0) return 0;
            if (safeRows < h) h = safeRows;
        }
    }

    // The palette pointer needs the same treatment as the pixel base, and never
    // had it. m_pPalette is whatever PSXTexture::Store parsed out of the TIM /
    // whatever CopyFrom carried into g_MarniBitsWorkBuffer; the loops below
    // index it by a full pixel byte (0-255 at 8bpp, 0-15 at 4bpp) with no
    // check, so one mis-parsed descriptor faults on `clut[idx]` deep inside the
    // async task instead of failing the page. Verify the whole CLUT is readable
    // and drop to the untextured path if it is not.
    if (clut != NULL && (bpp == 4 || bpp == 8)) {
        SIZE_T need = (bpp == 4) ? 16 * sizeof(WORD) : 256 * sizeof(WORD);
        SIZE_T avail = plat_readable_bytes(clut);
        if (avail < need) {
            char dbg[224];
            sprintf_s(dbg, sizeof(dbg),
                      "[TEXPAGE] BAD CLUT: w=%d h=%d bpp=%d pitch=%d base=%p "
                      "clut=%p need=%zu avail=%zu src=%p\n",
                      w, h, bpp, pitch, (const void*)base, (const void*)clut,
                      need, avail, g_texturePageSrcDesc);
            OutputDebugStringA(dbg);
            return 0;
        }
    }

    // --- STP accounting for the knock-out twin (see the note above) ---
    // Index-only. The colour conversion below is left alone and the twin is
    // then built from the finished RGBA by clearing the alpha of the texels
    // this marks, so the two can never disagree about a texel's colour.
    const bool clutKeyed = (clut != NULL) && (bpp == 4 || bpp == 8);
    // Index 0 is the transparent texel only while it holds black (see
    // ClutIndexZeroIsKey); -1 matches no index.
    const int keyIndex = (clutKeyed && ClutIndexZeroIsKey(clut)) ? 0 : -1;
    int  stpTexels = 0, opaqueTexels = 0;
    BYTE stpIndex[256];
    memset(stpIndex, 0, sizeof(stpIndex));
    if (clutKeyed) {
        const int entries = (bpp == 4) ? 16 : 256;
        for (int i = 0; i < entries; i++)
            stpIndex[i] = (clut[i] & 0x8000) ? 1 : 0;
        for (int y = 0; y < h; y++) {
            const BYTE* row = base + (size_t)y * pitch;
            if (bpp == 8) {
                for (int x = 0; x < w; x++) {
                    int idx = row[x];
                    if (stpIndex[idx])         stpTexels++;
                    else if (idx != keyIndex)  opaqueTexels++;
                }
            }
            else {
                for (int x = 0; x < w; x += 2) {
                    int lo = row[x >> 1] & 0x0F;
                    int hi = (row[x >> 1] >> 4) & 0x0F;
                    if (stpIndex[lo]) stpTexels++; else if (lo != keyIndex) opaqueTexels++;
                    if (x + 1 < w) {
                        if (stpIndex[hi]) stpTexels++; else if (hi != keyIndex) opaqueTexels++;
                    }
                }
            }
        }
    }

    // Output is uploaded as DXGI_FORMAT_R8G8B8A8_UNORM, so each DWORD must be
    // 0xAABBGGRR - red in the lowest byte. PS1 15-bit source colour is
    // MBBBBBGGGGGRRRRR, i.e. red in bits 0-4. Packing 0xAARRGGBB here (the
    // Win32 ARGB habit) swapped red and blue on every model texture.
    DWORD* rgba = (DWORD*)operator_new((size_t)w * h * sizeof(DWORD));
    if (!rgba) return 0;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            DWORD color = 0xFF000000; // opaque black default
            if (bpp == 4 && clut) {
                BYTE twoPix = base[y * pitch + x / 2];
                int nib = (x & 1) ? (twoPix >> 4) : (twoPix & 0xF);
                WORD c = clut[nib];
                DWORD a = (nib == keyIndex) ? 0x00 : 0xFF;
                DWORD r = (c & 0x1F) * 255 / 31;
                DWORD g = ((c >> 5) & 0x1F) * 255 / 31;
                DWORD b = ((c >> 10) & 0x1F) * 255 / 31;
                color = (a << 24) | (b << 16) | (g << 8) | r;
            }
            else if (bpp == 8 && clut) {
                int idx = base[y * pitch + x];
                WORD c = clut[idx];
                DWORD a = (idx == keyIndex) ? 0x00 : 0xFF;
                DWORD r = (c & 0x1F) * 255 / 31;
                DWORD g = ((c >> 5) & 0x1F) * 255 / 31;
                DWORD b = ((c >> 10) & 0x1F) * 255 / 31;
                color = (a << 24) | (b << 16) | (g << 8) | r;
            }
            else if (bpp == 16) {
                WORD c = *(const WORD*)(base + y * pitch + x * 2);
                DWORD a = (c == 0) ? 0x00 : 0xFF;
                DWORD r = (c & 0x1F) * 255 / 31;
                DWORD g = ((c >> 5) & 0x1F) * 255 / 31;
                DWORD b = ((c >> 10) & 0x1F) * 255 / 31;
                color = (a << 24) | (b << 16) | (g << 8) | r;
            }
            rgba[y * w + x] = color;
        }
    }

    // Port-only: an HD model skin named by its EMD (see HdModelKeyFor). It
    // outranks a hash-named replacement; without one the upload goes on as
    // before.
    int hdMaterial = 0;
    bool hdPerPalette = false;
    char hdRowKey[160];
    const char* hdKey = (clutKeyed && HdTextures_Active())
                      ? HdModelKeyFor(bits, &hdMaterial, &hdPerPalette, hdRowKey, sizeof(hdRowKey))
                      : NULL;
    HdPixels hdSkin;
    int hdW = 0, hdH = 0;
    bool hdNamed = false;
    std::string hdToken;          // a picture still decoding (deferred upload)
    bool hdDeferred = false;
    if (hdKey != NULL) {
        if (hdPerPalette) {
            // The row's own picture is the upload itself.
            HdTextures_DumpNamed(hdKey, w, h, rgba);
        } else if (HdTextures_NamedDumpWanted(hdKey)) {
            DWORD* pic = HdModelComposite(bits, hdMaterial, w, h, base, pitch, bpp);
            if (pic != NULL) {
                HdTextures_DumpNamed(hdKey, w, h, pic);
                operator_delete(pic);
            }
        }
        const int found = HdTextures_TryNamed(hdKey, &hdSkin, &hdW, &hdH, &hdToken);
        hdNamed = found == 1;
        hdDeferred = found == 2;
    }

    // Without a named skin, a replacement by hash is looked up here rather than
    // inside CreateTextureHd, so the knock-out twin below can be cut from it
    // as well. (Looked up on its own, the twin was a texture of its own: it
    // was dumped, and the importer could hand it the pack's picture of the
    // texture it is a copy of.) An upload a source names is left to
    // CreateTextureHd.
    if (!hdNamed && !hdDeferred && HdTextures_Active() && HdTextures_SourceFor(w, h) == NULL) {
        const int found = HdTextures_TryHash(w, h, rgba, &hdSkin, &hdW, &hdH, &hdToken);
        hdNamed = found == 1;
        hdDeferred = found == 2;
    }

    MarniHandle tex = MARNI_NULL_HANDLE;
    if (hdNamed) {
        tex = pD3D->m_pDX->CreateTexture(hdW, hdH, 32, hdSkin->data(), NULL, NULL);
        if (tex == MARNI_NULL_HANDLE) hdNamed = false;
    }
    if (hdDeferred)
        tex = HdUploadDeferred(pD3D->m_pDX, w, h, rgba, hdToken, HdBuildWhole);
    if (tex == MARNI_NULL_HANDLE) tex = CreateTextureHd(pD3D->m_pDX, w, h, rgba);
    if (tex == MARNI_NULL_HANDLE) { operator_delete(rgba); return 0; }

    // Knock-out twin - the same image with every STP texel made fully
    // transparent, i.e. holding only the opaque texels. Built from the finished
    // RGBA so the colours are identical by construction; only the alpha moves.
    // Built only when the knock-out still carries MOST of the material, which
    // is the glass-pane shape this emulates - see the note above.
    //
    // With a named HD skin the twin is the HD picture instead, each texel
    // knocked out when the original texel under it is STP in this material's
    // palette: which texels blend is the TIM's business, not the artist's.
    if (hdNamed && stpTexels > 0 && opaqueTexels > stpTexels) {
        DWORD* knock = (DWORD*)operator_new((size_t)hdW * hdH * sizeof(DWORD));
        if (knock != NULL) {
            const DWORD* src = (const DWORD*)hdSkin->data();
            for (int y = 0; y < hdH; y++) {
                const BYTE* row = base + (size_t)(y * h / hdH) * pitch;
                for (int x = 0; x < hdW; x++) {
                    const int lx = x * w / hdW;
                    int idx;
                    if (bpp == 8)    idx = row[lx];
                    else if (lx & 1) idx = (row[lx >> 1] >> 4) & 0x0F;
                    else             idx = row[lx >> 1] & 0x0F;
                    DWORD c = src[y * hdW + x];
                    if (stpIndex[idx]) c &= 0x00FFFFFFu;
                    knock[y * hdW + x] = c;
                }
            }
            MarniHandle twin = pD3D->m_pDX->CreateTexture(hdW, hdH, 32, knock, NULL, NULL);
            operator_delete(knock);
            if (twin != MARNI_NULL_HANDLE)
                MarniStpKnock_Set((unsigned int)tex, (unsigned int)twin);
        }
    }
    else if (clutKeyed && stpTexels > 0 && opaqueTexels > stpTexels) {
        DWORD* knock = (DWORD*)operator_new((size_t)w * h * sizeof(DWORD));
        if (knock != NULL) {
            for (int y = 0; y < h; y++) {
                const BYTE* row = base + (size_t)y * pitch;
                for (int x = 0; x < w; x++) {
                    int idx;
                    if (bpp == 8)   idx = row[x];
                    else if (x & 1) idx = (row[x >> 1] >> 4) & 0x0F;
                    else            idx = row[x >> 1] & 0x0F;
                    DWORD c = rgba[y * w + x];
                    if (stpIndex[idx]) c &= 0x00FFFFFFu;   // alpha 0, colour kept
                    knock[y * w + x] = c;
                }
            }
            // Not looked up or dumped: the twin is derived from the texture,
            // and with an HD picture it is cut from that (above).
            MarniHandle twin = pD3D->m_pDX->CreateTexture(w, h, 32, knock, NULL, NULL);
            operator_delete(knock);
            if (twin != MARNI_NULL_HANDLE)
                MarniStpKnock_Set((unsigned int)tex, (unsigned int)twin);
            // A deferred picture is cut into a twin too when it arrives.
            if (twin != MARNI_NULL_HANDLE && hdDeferred && !s_hdPending.empty() &&
                s_hdPending.back().tex == tex) {
                HdPending& pend = s_hdPending.back();
                pend.twin = twin;
                pend.twinSerial = pD3D->m_pDX->TextureSerial(twin);
                pend.stp.resize((size_t)w * h);
                for (int y = 0; y < h; y++) {
                    const BYTE* row = base + (size_t)y * pitch;
                    for (int x = 0; x < w; x++) {
                        int idx;
                        if (bpp == 8)   idx = row[x];
                        else if (x & 1) idx = (row[x >> 1] >> 4) & 0x0F;
                        else            idx = row[x >> 1] & 0x0F;
                        pend.stp[(size_t)y * w + x] = stpIndex[idx];
                    }
                }
            }
        }
    }

    operator_delete(rgba);
    if (outHandle) *(unsigned int*)outHandle = 1;
    return (int)tex;
}

// [7] CreateObjectHandle — 0x0044af90
// The DX5 original built an execute buffer per 3D object and returned a slot
// index. The DX11 port draws straight from the CMarniViewport2 vertex/index
// buffers, so the handle is only an opaque "created ok" token — return a
// unique non-zero id.
static unsigned int VTable_CreateObjectHandle(void* self, void* objDesc,
                                               unsigned char flags)
{
    (void)self; (void)objDesc; (void)flags;
    static unsigned int s_nextObjectHandle = 1;
    unsigned int h = s_nextObjectHandle++;
    if (s_nextObjectHandle == 0) s_nextObjectHandle = 1;
    return h;
}

// [8] DeleteTextureHandle — 0x0044b220
static int VTable_DeleteTextureHandle(void* self, int handle)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)self;
    if (!pD3D || !pD3D->m_pDX) return 1;
    if (handle > 0) {
        // The knock-out twin is not reachable from its source texture, so it
        // has to die with it here. Without this its slot leaks AND a later
        // texture allocated the same handle would inherit the stale twin.
        unsigned int twin = MarniStpKnock_Take((unsigned int)handle);
        if (twin != 0) pD3D->m_pDX->DestroyTexture((MarniHandle)twin);
        pD3D->m_pDX->DestroyTexture((MarniHandle)handle);
    }
    return 1;
}

// [9] DeleteObjectHandle — 0x0044b1c0
static int VTable_DeleteObjectHandle(void* self, int handle)
{
    (void)self; (void)handle;
    return 1;
}

// [10] SetTexture — 0x00448300
// Despite the legacy name, in the original this is the TMD draw-queue entry
// point: CMarniDirect3DTMD::Transform calls it per object as
// vtable[10](objData, depth) and it inserted the object into the ordering
// table. The DX11 port queues it into the per-frame TMD draw list.
static int VTable_SetTexture(void* self, void* texData, unsigned int param)
{
    (void)self;
    TmdQueueObject(texData, (int)param);
    return 1;
}

// [11] ResetTextures — 0x00448380
static int VTable_ResetTextures(void* self)
{
    (void)self;
    return 1;
}

// ============================================================================
// Global Marni System functions
// ============================================================================

// IsGraphicsSystemReadyForOperation — 0x00497060
BOOL IsGraphicsSystemReadyForOperation(void)
{
    if (g_pMarniDirect3D) {
        CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
        if (pD3D->m_isInitialized) return TRUE;
        char dbg[128];
        sprintf(dbg, "[Marni] Graphics system: ptr=%p vt=%p init=%d\n",
                (void*)g_pMarniDirect3D, (void*)pD3D->vtable, pD3D->m_isInitialized);
        OutputDebugStringA(dbg);
        OutputDebugStringA("[Marni] Graphics system not initialized\n");
        return FALSE;
    }
    if (g_hWnd == NULL) return TRUE;
    OutputDebugStringA("[Marni] Graphics system unavailable (fallback)\n");
    return FALSE;
}

// InitializeMarniSystem — 0x004970c0
void InitializeMarniSystem(void)
{
    if (g_dwScreenWidth  < 320)  g_dwScreenWidth  = 640;
    if (g_dwScreenHeight < 240)  g_dwScreenHeight = 480;
    if ((int)g_dwSelectedDisplayModeID < 0) g_dwSelectedDisplayModeID = 0;

    void* pMem = operator_new(0x21DC);
    if (!pMem) {
        OutputDebugStringA("[Marni] Failed to allocate CMarniDirect3D\n");
        g_pMarniDirect3D = NULL;
        return;
    }

    g_pMarniDirect3D = CMarniDirect3D_Constructor(
        pMem, g_hWnd, g_dwScreenWidth, g_dwScreenHeight,
        g_dwSelectedDisplayModeID, g_dwSelectedDisplayAdapterID);

    if (!IsGraphicsSystemReadyForOperation()) {
        ShowMessageBox(NULL,
            "Failed to initialize the Graphics System",
            "RESIDENT EVIL", MB_OK | MB_ICONSTOP);
        CleanupVideoConfigAndSaveAllSettings();
        plat_window_destroy(g_hWnd);
        return;
    }

    InitJoysticks();
    // Capability flag only. g_isSideWinderConnected is the one-shot START
    // injection main_loop consumes and must NOT be raised here - doing so pops
    // the inventory open on the first frame after a pad is detected.
    g_bPadConnected = MarniPadIsConnected();
    CreateLights(3);

    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (pD3D && pD3D->m_isFullScreen) {
        plat_cursor_show(FALSE);
        g_isGameCursorHiddenFlag = FALSE;
    }

    g_GameInitTime = plat_time_ms();
}

// EnumerateDisplayModes — 0x004976c0
// Now delegates directly to MarniDX (DXGI stays inside marni/).
void EnumerateDisplayModes(void)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (!pD3D || !pD3D->m_isInitialized) return;

    pD3D->m_pDX->EnumerateDisplayModes(g_DisplayModeBuffer,
        MAX_DISPLAY_MODES, &g_NumDisplayModes);
}

// EnumerateD3DRenderers — 0x004977f0
void EnumerateD3DRenderers(void)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (!pD3D) return;
    pD3D->m_pDX->EnumerateAdapters(g_D3DRenderers, 8,
        &g_NumD3DRenderersAvailable);
}

// GetDirect3DDriverCount — 0x004486e0
// Original: __fastcall on the CMarniDirect3D object; guards on +0x3C
// (m_isInitialized) and prints "Direct3D::RequestDriverCount" when the
// renderer was never initialized, otherwise returns the adapter count
// filled in by EnumerateD3DRenderers (0x004977f0).
int GetDirect3DDriverCount(void)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (!pD3D || !pD3D->m_isInitialized) {
        printf("Direct3D::RequestDriverCount\n");
        return 0;
    }
    return g_NumD3DRenderersAvailable;
}

// GetDirect3DDriverName — 0x00448710
// Original: __thiscall; same m_isInitialized guard ("Direct3D::RequestDriverName"),
// then bounds-checks index > 4 (the original driver table holds 5 entries of
// 0x11C bytes at 0x007e0e10, name string first) and returns a pointer to that
// entry's name. Port keeps the exact >4 check; g_D3DRenderers holds the names.
const char* GetDirect3DDriverName(int index)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (!pD3D || !pD3D->m_isInitialized || index > 4) {
        printf("Direct3D::RequestDriverName\n");
        return NULL;
    }
    return g_D3DRenderers[index].name;
}

// InitJoysticks — 0x00420770
// Enumerates the WinMM devices and brings up the XInput backend. This used to
// be a stand-in that only probed XInput slot 0 and printed the result, which
// left CMarniDirectInput::InitJoysticks (the real implementation) with no
// caller at all - so joystickCount stayed 0, the per-frame poll loop never
// ran, and no pad input could reach ReadPadBoth or read_sidewinder_pad.
void InitJoysticks(void)
{
    CMarniDirectInput::InitJoysticks(&g_pMasterInputState);
}

// IsSideWinderPadConnected — 0x0040b610
int IsSideWinderPadConnected(void)
{
    return 1; // No legacy SideWinder on modern PC
}

// CreateLights — 0x00448440
void CreateLights(int numLights)
{
    (void)numLights;
}

// ============================================================================
// Drawing Functions
// ============================================================================

void MarniPresent(void)   { VTable_Present(g_pMarniDirect3D); }
void MarniClear(void)     { VTable_Clear(g_pMarniDirect3D); }
void PresentFrame(void)   { MarniPresent(); }
// Original ClearScreen (0x004298c0) ends with clearscreen_present_tail
// (0x00497640): vtable[3] Clear, FUN_0040a8f0 background quad insert,
// vtable[4] Present, then state = 3. The port's MarniClear + the governor's
// background-quad path cover that tail; this alias is the entry point.
void ClearScreen(void)    { MarniClear(); }

void* MarniGetDevice(void)
{
    return g_pMarniDirect3D;
}

void MarniDrawRect(int x, int y, int w, int h, DWORD color)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (!pD3D || !pD3D->m_isInitialized) return;
    pD3D->m_pDX->DrawRect(x, y, w, h, color);
}

void MarniDrawLine(float x0, float y0, float x1, float y1,
                   float thickness, DWORD color)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (!pD3D || !pD3D->m_isInitialized) return;
    pD3D->m_pDX->DrawLine(x0, y0, x1, y1, thickness, color);
}

void MarniDrawSprite(float x, float y, float w, float h,
                     float u0, float v0, float u1, float v1,
                     DWORD color, MarniHandle tex)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (!pD3D || !pD3D->m_isInitialized) return;
    pD3D->m_pDX->DrawSprite(x, y, w, h, u0, v0, u1, v1,
                             color, tex, MARNI_SAMPLER_POINT,
                             MARNI_BLEND_ALPHA);
}

void MarniDrawSpriteAdd(float x, float y, float w, float h,
                        float u0, float v0, float u1, float v1,
                        DWORD color, MarniHandle tex)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (!pD3D || !pD3D->m_isInitialized) return;
    pD3D->m_pDX->DrawSprite(x, y, w, h, u0, v0, u1, v1,
                             color, tex, MARNI_SAMPLER_POINT,
                             MARNI_BLEND_ADD);
}

// Port-only: src * srcAlpha + dst. The PS1's additive semi-transparency modes
// (ABR 1 = B + F, ABR 3 = B + F/4) for the effect sprites, with the rate in
// the vertex alpha; unlike MarniDrawSpriteAdd the texel alpha still keys out
// the transparent texels, so an HD page with colour under alpha 0 adds nothing.
void MarniDrawSpriteAddAlpha(float x, float y, float w, float h,
                             float u0, float v0, float u1, float v1,
                             DWORD color, MarniHandle tex)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (!pD3D || !pD3D->m_isInitialized) return;
    pD3D->m_pDX->DrawSprite(x, y, w, h, u0, v0, u1, v1,
                             color, tex, MARNI_SAMPLER_POINT,
                             MARNI_BLEND_ADD_ALPHA);
}

// Only the ground shadow / blood pool (AddFadePoly, type 12) draws through
// here, and it stretches a 26x29 gradient over a quad several times that size.
// Point sampling turned the soft blob into visible texel blocks with straight
// faceted edges; the original's D3D7 device filtered it, which is what makes
// the shadow in the retail game a smooth oval. Everything else in the sprite
// path stays POINT on purpose - that is the PS1 look for the 2D art.
void MarniDrawTrianglesPersp(const float* verts, int triCount, MarniHandle tex,
                              BOOL depthTest)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (!pD3D || !pD3D->m_isInitialized) return;
    pD3D->m_pDX->DrawTrianglesPersp(verts, triCount, tex,
                                    MARNI_SAMPLER_LINEAR, MARNI_BLEND_ALPHA,
                                    depthTest != FALSE);
}

// ============================================================================
// MarniGetRenderScale
// Game-space -> backbuffer scale factors. The original Marni layer applied
// this at draw time as physical/logical (FUN_0042ba60 built the transform
// matrix with (field_0x10 / field_0x8) and (field_0x14 / field_0xc)):
//   physical = real surface dims (field_0x10/0x14 = our m_width/m_height)
//   logical  = render resolution set by SetVideoResolution (field_0x8/0xc)
// With a 640x480 surface and the game's logical 320x240, everything is
// scaled x2 at draw time — filling the window exactly like the original.
// The decomp applies the same ratio at sprite-queue time instead.
// ============================================================================
void MarniGetRenderScale(float* outScaleX, float* outScaleY)
{
    DWORD bw = 0, bh = 0;
    DWORD lw = 320, lh = 240;
    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (pD3D) {
        if (pD3D->m_pDX) pD3D->m_pDX->GetBackBufferSize(&bw, &bh);
        if (pD3D->m_logicalWidth  >= 320) lw = pD3D->m_logicalWidth;
        if (pD3D->m_logicalHeight >= 240) lh = pD3D->m_logicalHeight;
    }
    if (bw < 320) bw = 320;
    if (bh < 240) bh = 240;
    if (outScaleX) *outScaleX = (float)bw / (float)lw;
    if (outScaleY) *outScaleY = (float)bh / (float)lh;
}

BOOL MarniCreateTexture(int width, int height, int bpp, const void* pixelData,
                        MarniHandle* outTex)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (!pD3D || !pD3D->m_isInitialized || !outTex) return FALSE;

    // Every caller in the game layer hands over finished RGBA8; the other
    // depths are the renderer's own conversions and are left alone.
    if (bpp == 32) {
        *outTex = CreateTextureHd(pD3D->m_pDX, width, height, pixelData);
    } else {
        *outTex = pD3D->m_pDX->CreateTexture(width, height, bpp, pixelData,
                                             NULL, NULL);
    }
    return (*outTex != MARNI_NULL_HANDLE);
}

BOOL MarniCreateTextureRaw(int width, int height, const void* rgba,
                           MarniHandle* outTex)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (!pD3D || !pD3D->m_isInitialized || !outTex) return FALSE;

    *outTex = pD3D->m_pDX->CreateTexture(width, height, 32, rgba, NULL, NULL);
    return (*outTex != MARNI_NULL_HANDLE);
}
