// MarniSystem.h - Marni System wrapper interface
// Modern implementation using Direct3D 11, XAudio2, XInput
// Replaces original DirectX 5.0 (DirectDraw/Direct3D/DirectSound/DirectInput)
#pragma once

#include <windows.h>
#include <new>
#include <d3d11.h>
#include <dxgi.h>
#include <xaudio2.h>
#include <xinput.h>
#include <d3dcompiler.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "xinput.lib")
#pragma comment(lib, "d3dcompiler.lib")

// ============================================================================
// CMarniDirect3D - Replaces the original Direct3D 5 wrapper
// Object size in original: 0x21DC (8676 bytes)
// vtable at 0x004af230
// ============================================================================
class CMarniDirect3D {
public:
    // VTable pointer at offset 0x00
    void** vtable;
    
    // Screen dimensions (offset 0x10, 0x14, 0x18)
    DWORD m_width;          // 0x10
    DWORD m_height;         // 0x14
    DWORD m_bitDepth;       // 0x18
    
    // Padding to reach 0x3C (initialization flag)
    BYTE  m_pad1[0x20];    // 0x1C - 0x3B
    BOOL  m_isInitialized; // 0x3C - initialization flag
    
    // Padding to reach 0x68 (fullscreen flag)
    BYTE  m_pad2[0x28];    // 0x40 - 0x67
    BOOL  m_isFullScreen;  // 0x68 - fullscreen mode
    
    // Window active flag at 0x74
    BOOL  m_isActive;      // 0x74
    
    // Display mode index at 0x78
    DWORD m_selectedMode;  // 0x78
    
    // Padding to D3D11 members area
    BYTE  m_pad3[0x290];   // 0x7C - 0x30B (approximate)
    DWORD m_deviceType;    // 0x30C - device type (0-6, 5=SW)
    DWORD m_currentMode;   // 0x314 - current display mode
    
    // Custom data at 0x324 (field792 in original)
    BYTE  m_pad4[0x10];    // 0x318 - 0x327
    DWORD m_scratch;       // 0x324 - scratch/state field
    
    // --- Modern D3D11 members (placed at high offsets) ---
    // These map to original DirectDraw/D3D5 interface pointers
    // Original had:
    //   0x761: LPDIRECTDRAW m_lpDD
    //   0x762: LPDIRECTDRAW2 m_lpDD2
    //   0x7B4: LPDIRECTDRAWSURFACE m_lpDDS_Front
    //   0x7B5: LPDIRECTDRAWSURFACE m_lpDDS_Back
    
    // D3D11 equivalents
    ID3D11Device*           m_pD3DDevice;       // D3D11 device
    ID3D11DeviceContext*    m_pD3DContext;      // D3D11 immediate context
    IDXGISwapChain*         m_pSwapChain;       // D3D11 swap chain
    ID3D11RenderTargetView* m_pRenderTargetView; // Back buffer RTV
    ID3D11Texture2D*        m_pDepthStencil;     // Depth/stencil buffer
    ID3D11DepthStencilView* m_pDepthStencilView; // DSV
    ID3D11RasterizerState*  m_pRasterStateScissor; // Rasterizer with scissor enabled
    ID3D11VertexShader*     m_pQuadVS;             // Quad vertex shader
    ID3D11PixelShader*      m_pQuadPS;             // Quad pixel shader (solid color)
    ID3D11InputLayout*      m_pQuadInputLayout;    // Input layout for quad vertices
    ID3D11Buffer*           m_pQuadVB;             // Reusable quad vertex buffer
    ID3D11Buffer*           m_pSpriteCB;           // Sprite constant buffer (MVP matrix)
    ID3D11BlendState*       m_pBlendAlpha;         // Alpha blend state
    ID3D11SamplerState*     m_pSamplerLinear;      // Linear texture sampler
    ID3D11SamplerState*     m_pSamplerPoint;       // Point sampler (pixelated, for fonts)
    ID3D11DepthStencilState* m_pDepthDisabled;     // Depth-disabled state for 2D sprites

    // Font texture (created during ProcessTextureImage for bank 0x1E)
    ID3D11Texture2D*        m_pFontTexture;        // Font texture (from fontus.tim)
    ID3D11ShaderResourceView* m_pFontSRV;          // Font SRV
    int                     m_FontTexWidth;         // Font texture width in pixels
    int                     m_FontTexHeight;        // Font texture height in pixels
    ID3D11Texture2D*        m_pWhiteTex;           // 1x1 white fallback texture
    ID3D11ShaderResourceView* m_pWhiteSRV;         // White SRV (for solid color quads)

    // Constructor / Destructor
    CMarniDirect3D(HWND hWnd, int width, int height, int modeID, int adapterID);
    ~CMarniDirect3D();
    
    // Virtual function interface (matching original vtable)
    // [0] RequestVideoMemory  - 0x00448630
    // [1] ChangeDisplayMode   - 0x00449300
    // [2] SetD3DRenderer      - 0x0044a0d0
    // [3] Clear               - 0x0044b320
    // [4] Present             - 0x00448ff0
    // [5] HandleWindowMessage - 0x00448b60
    // [6] CreateTextureHandle - 0x0044c900
    // [7] CreateObjectHandle  - 0x0044af90
    // [8] DeleteTextureHandle - 0x0044b220
    // [9] DeleteObjectHandle  - 0x0044b1c0
    // [10] SetTexture         - 0x00448300
    // [11] ResetTextures      - 0x00448380
};

// ============================================================================
// Global Marni system functions
// ============================================================================

// Create the Marni D3D object (original constructor wrapper)
void* CMarniDirect3D_Constructor(void* self, HWND hWnd, int width, int height, int modeID, int adapterID);

// Check if graphics system is initialized and ready
BOOL IsGraphicsSystemReadyForOperation(void);

// Initialize the Marni System (creates graphics, input, lights)
void InitializeMarniSystem(void);

// Enumerate available display modes
void EnumerateDisplayModes(void);

// Enumerate available D3D renderers
void EnumerateD3DRenderers(void);

// Initialize joystick/gamepad input
void InitJoysticks(void);

// Check if Microsoft SideWinder pad is connected
int IsSideWinderPadConnected(void);

// Create 3D scene lights
void CreateLights(int numLights);

// Video playback state machine (FMV)
void UpdateVideoPlayback(void);

// Present frame to screen
void MarniPresent(void);        // Forward to CMarniDirect3D vtable[4] Present

// Clear render target
void MarniClear(void);          // Forward to CMarniDirect3D vtable[3] Clear

    // Debug: draw a filled rectangle on screen (test helper)
void MarniDrawRect(int x, int y, int w, int h, DWORD color);

// Sprite drawing: textured colored quad at screen coordinates
void MarniDrawSprite(float x, float y, float w, float h,
                     float u0, float v0, float u1, float v1,
                     DWORD color, ID3D11ShaderResourceView* srv);

// Create a D3D11 texture from raw pixel data (e.g. parsed TIM)
void MarniCreateTexture(int width, int height, int bpp, void* pixelData,
                        ID3D11Texture2D** outTex, ID3D11ShaderResourceView** outSRV);

// Get the Marni D3D object for direct access
void* MarniGetDevice(void);

// Present frame to screen (for external callers)
void PresentFrame(void);        // Equivalent to vtable[4]=Present

// Clear screen
void ClearScreen(void);

// Memory operators used by Marni
void* operator_new(size_t size);
void operator_delete(void* ptr);
