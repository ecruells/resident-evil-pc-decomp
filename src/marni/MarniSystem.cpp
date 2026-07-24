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

#include "MarniSystem.h"
#include "MarniBits.h"
#include "MarniDX.h"
#include "Globals.h"
#include <cstdio>
#include <cstring>
#include <new>
#include <xinput.h>
#pragma comment(lib, "xinput.lib")

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

// [6] CreateTextureHandle — 0x0044c900
static int VTable_CreateTextureHandle(void* self, void* texDesc,
                                      unsigned int flags, void* outHandle)
{
    // The modern port handles texture creation through MarniCreateTexture
    // and ProcessTextureImage in TextureLoader.cpp. This vtable entry
    // existed in the original for legacy D3D5 internal tables.
    if (outHandle) *(unsigned int*)outHandle = 1;
    (void)self; (void)texDesc; (void)flags;
    return 1;
}

// [7] CreateObjectHandle — 0x0044af90
static unsigned int VTable_CreateObjectHandle(void* self, void* objDesc,
                                              unsigned char flags)
{
    (void)self; (void)objDesc; (void)flags;
    return 1;
}

// [8] DeleteTextureHandle — 0x0044b220
static int VTable_DeleteTextureHandle(void* self, int handle)
{
    (void)self; (void)handle;
    return 1;
}

// [9] DeleteObjectHandle — 0x0044b1c0
static int VTable_DeleteObjectHandle(void* self, int handle)
{
    (void)self; (void)handle;
    return 1;
}

// [10] SetTexture — 0x00448300
static int VTable_SetTexture(void* self, void* texData, unsigned int param)
{
    (void)self; (void)texData; (void)param;
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
        DestroyWindow(g_hWnd);
        return;
    }

    InitJoysticks();
    int swResult = IsSideWinderPadConnected();
    g_isSideWinderConnected = (swResult == 0);
    CreateLights(3);

    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (pD3D && pD3D->m_isFullScreen) {
        ShowCursor(FALSE);
        g_isGameCursorHiddenFlag = FALSE;
    }

    g_GameInitTime = timeGetTime();
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

// InitJoysticks — 0x00420770
void InitJoysticks(void)
{
    XINPUT_STATE state;
    DWORD result = XInputGetState(0, &state);
    char dbg[128];
    sprintf_s(dbg, "[Marni] XInput: controller 0 %s\n",
              (result == ERROR_SUCCESS) ? "connected" : "not connected");
    OutputDebugStringA(dbg);
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

    *outTex = pD3D->m_pDX->CreateTexture(width, height, bpp, pixelData,
                                         NULL, NULL);
    return (*outTex != MARNI_NULL_HANDLE);
}
