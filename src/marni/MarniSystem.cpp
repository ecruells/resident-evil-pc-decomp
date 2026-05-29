// MarniSystem.cpp - Marni System wrapper implementation
// Modern implementation using Direct3D 11, XAudio2, XInput
// Replaces original DirectX 5.0 (DirectDraw/Direct3D/DirectSound/DirectInput)
//
// Original class: CMarniDirect3D at vtable 0x004af230, size 0x21DC
// Original functions referenced by address throughout

#include "MarniSystem.h"
#include "Globals.h"
#include <cstdio>
#include <cstring>
#include <new>

// ============================================================================
// VTable function pointer types (matching WindowProc.cpp calling convention:
// 'this' passed explicitly as first argument)
// ============================================================================
typedef int  (*PFN_RequestVideoMemory)(void* self);
typedef int  (*PFN_ChangeDisplayMode)(void* self, int mode);
typedef void (*PFN_SetD3DRenderer)(void* self, int renderer);
typedef int  (*PFN_Clear)(void* self);
typedef int  (*PFN_Present)(void* self);
typedef int  (*PFN_HandleWindowMessage)(void* self, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
typedef int  (*PFN_CreateTextureHandle)(void* self, void* texDesc, unsigned int flags, void* outHandle);
typedef unsigned int (*PFN_CreateObjectHandle)(void* self, void* objDesc, unsigned char flags);
typedef int  (*PFN_DeleteTextureHandle)(void* self, int handle);
typedef int  (*PFN_DeleteObjectHandle)(void* self, int handle);
typedef int  (*PFN_SetTexture)(void* self, void* texData, unsigned int param);
typedef int  (*PFN_ResetTextures)(void* self);

// ============================================================================
// Quad vertex structure for sprite rendering
// ============================================================================
struct QuadVertex {
    float x, y;         // Screen position
    float u, v;         // Texture coordinates
    float r, g, b, a;   // Color (0.0 - 1.0)
};

// ============================================================================
// Constant buffer for sprite MVP matrix
// ============================================================================
struct SpriteConstantBuffer {
    float mvp[4][4];    // Model-View-Projection matrix
};

// ============================================================================
// Forward declarations of vtable function implementations
// ============================================================================
static int  VTable_RequestVideoMemory(void* self);
static int  VTable_ChangeDisplayMode(void* self, int mode);
static void VTable_SetD3DRenderer(void* self, int renderer);
static int  VTable_Clear(void* self);
static int  VTable_Present(void* self);
static int  VTable_HandleWindowMessage(void* self, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static int  VTable_CreateTextureHandle(void* self, void* texDesc, unsigned int flags, void* outHandle);
static unsigned int VTable_CreateObjectHandle(void* self, void* objDesc, unsigned char flags);
static int  VTable_DeleteTextureHandle(void* self, int handle);
static int  VTable_DeleteObjectHandle(void* self, int handle);
static int  VTable_SetTexture(void* self, void* texData, unsigned int param);
static int  VTable_ResetTextures(void* self);

// ============================================================================
// HLSL Shader source (embedded, compiled at runtime via D3DCompile)
// ============================================================================
static const char* g_QuadVS_Source = R"(
cbuffer SpriteCB : register(b0)
{
    row_major float4x4 g_MVP;
};

struct VS_INPUT
{
    float2 pos : POSITION;
    float2 tex : TEXCOORD0;
    float4 col : COLOR0;
};

struct VS_OUTPUT
{
    float4 pos : SV_Position;
    float2 tex : TEXCOORD0;
    float4 col : COLOR0;
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    output.pos = mul(float4(input.pos.x, input.pos.y, 0.0f, 1.0f), g_MVP);
    output.tex = input.tex;
    output.col = input.col;
    return output;
}
)";

static const char* g_QuadPS_Source = R"(
Texture2D    g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

struct PS_INPUT
{
    float4 pos : SV_Position;
    float2 tex : TEXCOORD0;
    float4 col : COLOR0;
};

float4 main(PS_INPUT input) : SV_Target
{
    float4 texColor = g_Texture.Sample(g_Sampler, input.tex);
    return texColor * input.col;
}
)";

// ============================================================================
// Static vtable array (12 entries, matching original vtable layout at 0x004af230)
// ============================================================================
static void* g_CMarniDirect3D_VTable[12] = {
    (void*)VTable_RequestVideoMemory,       // [0] 0x00448630
    (void*)VTable_ChangeDisplayMode,        // [1] 0x00449300
    (void*)VTable_SetD3DRenderer,           // [2] 0x0044a0d0
    (void*)VTable_Clear,                    // [3] 0x0044b320
    (void*)VTable_Present,                  // [4] 0x00448ff0
    (void*)VTable_HandleWindowMessage,      // [5] 0x00448b60
    (void*)VTable_CreateTextureHandle,      // [6] 0x0044c900
    (void*)VTable_CreateObjectHandle,       // [7] 0x0044af90
    (void*)VTable_DeleteTextureHandle,      // [8] 0x0044b220
    (void*)VTable_DeleteObjectHandle,       // [9] 0x0044b1c0
    (void*)VTable_SetTexture,               // [10] 0x00448300
    (void*)VTable_ResetTextures,            // [11] 0x00448380
};

// ============================================================================
// Internal helpers
// ============================================================================

static void BuildOrthoMatrix(float* outMatrix, float left, float right, float bottom, float top)
{
    // Build a left-handed orthographic projection matrix (row-major for HLSL)
    // Maps screen coords (left,top) -> (-1,1), (right,bottom) -> (1,-1)
    // Y is flipped so that screen Y increases downward
    for (int i = 0; i < 16; i++) outMatrix[i] = 0.0f;

    outMatrix[0]  = 2.0f / (right - left);
    outMatrix[5]  = 2.0f / (top - bottom);
    outMatrix[10] = 1.0f;
    outMatrix[12] = (left + right) / (left - right);
    outMatrix[13] = (top + bottom) / (bottom - top);
    outMatrix[15] = 1.0f;
}

static bool CompileQuadShaders(CMarniDirect3D* pD3D)
{
    HRESULT hr;
    ID3DBlob* pVSBlob = NULL;
    ID3DBlob* pPSBlob = NULL;
    ID3DBlob* pErrorBlob = NULL;

    // Compile vertex shader
    hr = D3DCompile(g_QuadVS_Source, strlen(g_QuadVS_Source),
                    "QuadVS", NULL, NULL,
                    "main", "vs_4_0",
                    D3DCOMPILE_ENABLE_STRICTNESS, 0,
                    &pVSBlob, &pErrorBlob);
    if (FAILED(hr)) {
        if (pErrorBlob) {
            OutputDebugStringA("[Marni] VS compile error: ");
            OutputDebugStringA((char*)pErrorBlob->GetBufferPointer());
            OutputDebugStringA("\n");
            pErrorBlob->Release();
        }
        return false;
    }

    // Compile pixel shader
    hr = D3DCompile(g_QuadPS_Source, strlen(g_QuadPS_Source),
                    "QuadPS", NULL, NULL,
                    "main", "ps_4_0",
                    D3DCOMPILE_ENABLE_STRICTNESS, 0,
                    &pPSBlob, &pErrorBlob);
    if (FAILED(hr)) {
        if (pErrorBlob) {
            OutputDebugStringA("[Marni] PS compile error: ");
            OutputDebugStringA((char*)pErrorBlob->GetBufferPointer());
            OutputDebugStringA("\n");
            pErrorBlob->Release();
        }
        pVSBlob->Release();
        return false;
    }

    // Create vertex shader
    hr = pD3D->m_pD3DDevice->CreateVertexShader(
        pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(),
        NULL, &pD3D->m_pQuadVS);
    if (FAILED(hr)) {
        pVSBlob->Release();
        pPSBlob->Release();
        return false;
    }

    // Create pixel shader
    hr = pD3D->m_pD3DDevice->CreatePixelShader(
        pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(),
        NULL, &pD3D->m_pQuadPS);
    if (FAILED(hr)) {
        pPSBlob->Release();
        pVSBlob->Release();
        return false;
    }

    // Create input layout
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
          D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8,
          D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16,
          D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    hr = pD3D->m_pD3DDevice->CreateInputLayout(
        layout, ARRAYSIZE(layout),
        pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(),
        &pD3D->m_pQuadInputLayout);

    pVSBlob->Release();
    pPSBlob->Release();

    if (FAILED(hr)) return false;

    return true;
}

static bool CreateQuadVertexBuffer(CMarniDirect3D* pD3D)
{
    HRESULT hr;

    // Create dynamic vertex buffer for quads (6 vertices per sprite = 2 triangles, max ~512 sprites)
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.ByteWidth = sizeof(QuadVertex) * 6 * 512;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = pD3D->m_pD3DDevice->CreateBuffer(&bd, NULL, &pD3D->m_pQuadVB);
    if (FAILED(hr)) return false;

    // Create constant buffer for sprite MVP
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.ByteWidth = sizeof(SpriteConstantBuffer);
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = pD3D->m_pD3DDevice->CreateBuffer(&cbDesc, NULL, &pD3D->m_pSpriteCB);
    if (FAILED(hr)) return false;

    return true;
}

static bool CreateBlendAndSamplerStates(CMarniDirect3D* pD3D)
{
    HRESULT hr;

    // Alpha blend state (standard pre-multiplied alpha)
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    hr = pD3D->m_pD3DDevice->CreateBlendState(&blendDesc, &pD3D->m_pBlendAlpha);
    if (FAILED(hr)) return false;

    // Linear sampler state (clamp addressing)
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

    hr = pD3D->m_pD3DDevice->CreateSamplerState(&sampDesc, &pD3D->m_pSamplerLinear);
    if (FAILED(hr)) return false;

    // Point (nearest-neighbor) sampler for pixelated fonts
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    hr = pD3D->m_pD3DDevice->CreateSamplerState(&sampDesc, &pD3D->m_pSamplerPoint);
    if (FAILED(hr)) return false;

    // Depth-disabled state for 2D sprite rendering
    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = FALSE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dsDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
    dsDesc.StencilEnable = FALSE;
    hr = pD3D->m_pD3DDevice->CreateDepthStencilState(&dsDesc, &pD3D->m_pDepthDisabled);
    if (FAILED(hr)) return false;

    return true;
}

static bool CreateRasterizerState(CMarniDirect3D* pD3D)
{
    HRESULT hr;

    D3D11_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode = D3D11_FILL_SOLID;
    rastDesc.CullMode = D3D11_CULL_NONE;
    rastDesc.ScissorEnable = TRUE;
    rastDesc.DepthClipEnable = TRUE;

    hr = pD3D->m_pD3DDevice->CreateRasterizerState(&rastDesc, &pD3D->m_pRasterStateScissor);
    if (FAILED(hr)) return false;

    return true;
}

static bool CreateWhiteTexture(CMarniDirect3D* pD3D)
{
    HRESULT hr;
    DWORD whitePixel = 0xFFFFFFFF;

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = 1;
    texDesc.Height = 1;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_IMMUTABLE;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = &whitePixel;
    initData.SysMemPitch = 4;

    hr = pD3D->m_pD3DDevice->CreateTexture2D(&texDesc, &initData, &pD3D->m_pWhiteTex);
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    hr = pD3D->m_pD3DDevice->CreateShaderResourceView(pD3D->m_pWhiteTex, &srvDesc, &pD3D->m_pWhiteSRV);
    if (FAILED(hr)) return false;

    return true;
}

// ============================================================================
// CMarniDirect3D Constructor and Destructor
// Original constructor: 0x0044baf0
// ============================================================================

static CMarniDirect3D* MarniDirect3D_Construct(CMarniDirect3D* pThis, HWND hWnd, int width, int height, int modeID, int adapterID)
{
    if (pThis == NULL) return NULL;

    // Initialize vtable pointer
    pThis->vtable = g_CMarniDirect3D_VTable;

    // Initialize basic fields
    pThis->m_width       = (DWORD)width;
    pThis->m_height      = (DWORD)height;
    pThis->m_bitDepth    = (g_dwBitDepth == 16) ? 16 : 32;
    pThis->m_isInitialized = FALSE;
    pThis->m_isFullScreen  = g_bFullScreen;
    pThis->m_isActive      = TRUE;
    pThis->m_selectedMode  = (DWORD)modeID;
    pThis->m_deviceType    = (DWORD)adapterID;
    pThis->m_currentMode   = (DWORD)modeID;
    pThis->m_scratch       = 0;

    // Init all D3D11 members to NULL
    pThis->m_pD3DDevice        = NULL;
    pThis->m_pD3DContext       = NULL;
    pThis->m_pSwapChain        = NULL;
    pThis->m_pRenderTargetView = NULL;
    pThis->m_pDepthStencil     = NULL;
    pThis->m_pDepthStencilView = NULL;
    pThis->m_pRasterStateScissor = NULL;
    pThis->m_pQuadVS           = NULL;
    pThis->m_pQuadPS           = NULL;
    pThis->m_pQuadInputLayout  = NULL;
    pThis->m_pQuadVB           = NULL;
    pThis->m_pSpriteCB         = NULL;
    pThis->m_pBlendAlpha       = NULL;
    pThis->m_pSamplerLinear   = NULL;
    pThis->m_pSamplerPoint    = NULL;
    pThis->m_pDepthDisabled    = NULL;
    pThis->m_pFontTexture      = NULL;
    pThis->m_pFontSRV          = NULL;
    pThis->m_FontTexWidth      = 0;
    pThis->m_FontTexHeight     = 0;
    pThis->m_pWhiteTex         = NULL;
    pThis->m_pWhiteSRV         = NULL;

    // Clamp dimensions
    if (pThis->m_width < 320)  pThis->m_width = 640;
    if (pThis->m_height < 240) pThis->m_height = 480;

    OutputDebugStringA("[Marni] Creating D3D11 device...\n");

    // Create D3D11 device and swap chain
    DXGI_SWAP_CHAIN_DESC scDesc = {};
    scDesc.BufferCount = 2;
    scDesc.BufferDesc.Width = pThis->m_width;
    scDesc.BufferDesc.Height = pThis->m_height;
    scDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.BufferDesc.RefreshRate.Numerator = 60;
    scDesc.BufferDesc.RefreshRate.Denominator = 1;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.OutputWindow = hWnd;
    scDesc.SampleDesc.Count = 1;
    scDesc.Windowed = !pThis->m_isFullScreen;
    scDesc.Flags = 0;

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    D3D_FEATURE_LEVEL selectedFeatureLevel;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        NULL,                           // default adapter
        D3D_DRIVER_TYPE_HARDWARE,
        NULL,
        0,                              // no device flags
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &scDesc,
        &pThis->m_pSwapChain,
        &pThis->m_pD3DDevice,
        &selectedFeatureLevel,
        &pThis->m_pD3DContext);

    if (FAILED(hr)) {
        // Try WARP (software) renderer
        OutputDebugStringA("[Marni] Hardware D3D11 failed, trying WARP...\n");
        hr = D3D11CreateDeviceAndSwapChain(
            NULL,
            D3D_DRIVER_TYPE_WARP,
            NULL,
            0,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            &scDesc,
            &pThis->m_pSwapChain,
            &pThis->m_pD3DDevice,
            &selectedFeatureLevel,
            &pThis->m_pD3DContext);
    }

    if (FAILED(hr)) {
        OutputDebugStringA("[Marni] D3D11 device creation FAILED\n");
        return pThis;
    }

    OutputDebugStringA("[Marni] D3D11 device created OK\n");

    // Create render target view from back buffer
    ID3D11Texture2D* pBackBuffer = NULL;
    hr = pThis->m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (SUCCEEDED(hr)) {
        hr = pThis->m_pD3DDevice->CreateRenderTargetView(pBackBuffer, NULL, &pThis->m_pRenderTargetView);
        pBackBuffer->Release();

        if (FAILED(hr)) {
            OutputDebugStringA("[Marni] Failed to create RTV\n");
            return pThis;
        }
    }

    // Create depth/stencil buffer
    D3D11_TEXTURE2D_DESC dsDesc = {};
    dsDesc.Width = pThis->m_width;
    dsDesc.Height = pThis->m_height;
    dsDesc.MipLevels = 1;
    dsDesc.ArraySize = 1;
    dsDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsDesc.SampleDesc.Count = 1;
    dsDesc.Usage = D3D11_USAGE_DEFAULT;
    dsDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    hr = pThis->m_pD3DDevice->CreateTexture2D(&dsDesc, NULL, &pThis->m_pDepthStencil);
    if (SUCCEEDED(hr)) {
        pThis->m_pD3DDevice->CreateDepthStencilView(pThis->m_pDepthStencil, NULL, &pThis->m_pDepthStencilView);
    }

    // Create pipeline states
    if (!CreateRasterizerState(pThis)) {
        OutputDebugStringA("[Marni] Failed to create rasterizer state\n");
        return pThis;
    }

    if (!CreateBlendAndSamplerStates(pThis)) {
        OutputDebugStringA("[Marni] Failed to create blend/sampler states\n");
        return pThis;
    }

    // Create white fallback texture
    if (!CreateWhiteTexture(pThis)) {
        OutputDebugStringA("[Marni] Failed to create white texture\n");
    }

    // Compile shaders
    if (!CompileQuadShaders(pThis)) {
        OutputDebugStringA("[Marni] Failed to compile shaders\n");
        return pThis;
    }

    OutputDebugStringA("[Marni] Shaders compiled OK\n");

    // Create vertex and constant buffers
    if (!CreateQuadVertexBuffer(pThis)) {
        OutputDebugStringA("[Marni] Failed to create vertex buffers\n");
        return pThis;
    }

    // Set default render target
    pThis->m_pD3DContext->OMSetRenderTargets(1, &pThis->m_pRenderTargetView, pThis->m_pDepthStencilView);

    // Set scissor rect to full viewport
    D3D11_VIEWPORT vp = {};
    vp.Width    = (float)pThis->m_width;
    vp.Height   = (float)pThis->m_height;
    vp.MaxDepth = 1.0f;
    pThis->m_pD3DContext->RSSetViewports(1, &vp);

    D3D11_RECT scissorRect = { 0, 0, (LONG)pThis->m_width, (LONG)pThis->m_height };
    pThis->m_pD3DContext->RSSetScissorRects(1, &scissorRect);
    pThis->m_pD3DContext->RSSetState(pThis->m_pRasterStateScissor);

    pThis->m_isInitialized = TRUE;
    OutputDebugStringA("[Marni] Initialization complete\n");

    return pThis;
}

// ============================================================================
// operator_new / operator_delete - memory management (0x00433370, 0x004333e0)
// Used by original code to allocate the 0x21DC byte object
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
// CMarniDirect3D_Constructor - External C-style constructor wrapper
// Original: 0x0044baf0
// ============================================================================
void* CMarniDirect3D_Constructor(void* self, HWND hWnd, int width, int height, int modeID, int adapterID)
{
    return MarniDirect3D_Construct((CMarniDirect3D*)self, hWnd, width, height, modeID, adapterID);
}

// ============================================================================
// VTable Function Implementations
// ============================================================================

// [0] RequestVideoMemory - 0x00448630
// Returns available video memory in bytes (or a sentinel for SW mode)
static int VTable_RequestVideoMemory(void* self)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)self;
    if (pD3D == NULL || !pD3D->m_isInitialized) return 0;

    // Software renderer path (device type 5) returns a magic value
    if (pD3D->m_deviceType == 5) return 0x75bcd15;

    // Hardware: query available video memory via DXGI
    if (pD3D->m_pD3DDevice) {
        IDXGIDevice* pDXGIDevice = NULL;
        HRESULT hr = pD3D->m_pD3DDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&pDXGIDevice);
        if (SUCCEEDED(hr)) {
            IDXGIAdapter* pAdapter = NULL;
            hr = pDXGIDevice->GetAdapter(&pAdapter);
            if (SUCCEEDED(hr)) {
                DXGI_ADAPTER_DESC desc;
                hr = pAdapter->GetDesc(&desc);
                pAdapter->Release();
                if (SUCCEEDED(hr)) {
                    // Return dedicated video memory in bytes
                    return (int)(desc.DedicatedVideoMemory & 0x7FFFFFFF);
                }
            }
            pDXGIDevice->Release();
        }
    }

    return 128 * 1024 * 1024; // Default: report 128 MB
}

// [1] ChangeDisplayMode - 0x00449300
// Changes the display mode (resolution). In modern D3D11, resize the swap chain.
static int VTable_ChangeDisplayMode(void* self, int mode)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)self;
    if (pD3D == NULL || !pD3D->m_isInitialized) return 0;

    // Map mode index to resolution from display mode buffer
    if (mode < 0 || mode >= g_NumDisplayModes) return 0;

    DisplayModeInfo* pMode = &g_DisplayModeBuffer[mode];
    DWORD newWidth  = pMode->dwWidth;
    DWORD newHeight = pMode->dwHeight;
    BOOL newFullscreen = (pMode->dwFlags & 1) ? TRUE : FALSE;

    if (newWidth == 0 || newHeight == 0) return 0;

    // Resize swap chain
    if (pD3D->m_pSwapChain && pD3D->m_pD3DContext) {
        // Release render target view before resize
        if (pD3D->m_pRenderTargetView) {
            pD3D->m_pD3DContext->OMSetRenderTargets(0, NULL, NULL);
            pD3D->m_pRenderTargetView->Release();
            pD3D->m_pRenderTargetView = NULL;
        }

        HRESULT hr = pD3D->m_pSwapChain->ResizeBuffers(
            2, newWidth, newHeight,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            0);

        if (SUCCEEDED(hr)) {
            pD3D->m_width = newWidth;
            pD3D->m_height = newHeight;
            pD3D->m_currentMode = (DWORD)mode;
            pD3D->m_isFullScreen = newFullscreen;

            // Recreate render target view
            ID3D11Texture2D* pBackBuffer = NULL;
            hr = pD3D->m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
            if (SUCCEEDED(hr)) {
                pD3D->m_pD3DDevice->CreateRenderTargetView(pBackBuffer, NULL, &pD3D->m_pRenderTargetView);
                pBackBuffer->Release();
            }

            // Update viewport and scissor
            D3D11_VIEWPORT vp = {};
            vp.Width = (float)newWidth;
            vp.Height = (float)newHeight;
            vp.MaxDepth = 1.0f;
            pD3D->m_pD3DContext->RSSetViewports(1, &vp);

            D3D11_RECT scissor = { 0, 0, (LONG)newWidth, (LONG)newHeight };
            pD3D->m_pD3DContext->RSSetScissorRects(1, &scissor);

            // Rebind render target
            pD3D->m_pD3DContext->OMSetRenderTargets(1, &pD3D->m_pRenderTargetView,
                                                     pD3D->m_pDepthStencilView);

            // Update global screen dimensions
            g_dwScreenWidth = newWidth;
            g_dwScreenHeight = newHeight;

            return 1;
        }
    }

    return 0;
}

// [2] SetD3DRenderer - 0x0044a0d0
// Selects the D3D renderer. In modern D3D11, this is a no-op since we only have one device.
static void VTable_SetD3DRenderer(void* self, int renderer)
{
    // No-op in modern implementation - D3D11 handles this internally
    (void)self;
    (void)renderer;
}

// [3] Clear - 0x0044b320
// Clears the render target with the current clear color
static int VTable_Clear(void* self)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)self;
    if (pD3D == NULL || !pD3D->m_isInitialized) return 0;
    if (!pD3D->m_isActive) return 0;

    if (pD3D->m_pD3DContext && pD3D->m_pRenderTargetView) {
        // Use debug clear color if set, otherwise dark blue-black
        float clearColor[4];
        if (g_debugClearR != 0.0f || g_debugClearG != 0.0f || g_debugClearB != 0.0f) {
            clearColor[0] = g_debugClearR / 255.0f;
            clearColor[1] = g_debugClearG / 255.0f;
            clearColor[2] = g_debugClearB / 255.0f;
        } else {
            // Original default: dark background
            clearColor[0] = 0.0f;
            clearColor[1] = 0.0f;
            clearColor[2] = 0.05f;  // slight blue tint match original
        }
        clearColor[3] = 1.0f;

        pD3D->m_pD3DContext->ClearRenderTargetView(pD3D->m_pRenderTargetView, clearColor);

        if (pD3D->m_pDepthStencilView) {
            pD3D->m_pD3DContext->ClearDepthStencilView(pD3D->m_pDepthStencilView,
                D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
        }
        return 1;
    }

    return 0;
}

// [4] Present - 0x00448ff0
// Presents the back buffer to the screen
static int VTable_Present(void* self)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)self;
    if (pD3D == NULL || !pD3D->m_isInitialized) return 1;

    if (pD3D->m_pSwapChain) {
        // Present with VSync interval 1 (or 0 for immediate)
        HRESULT hr = pD3D->m_pSwapChain->Present(1, 0);

        if (FAILED(hr)) {
            // Handle device lost
            if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
                OutputDebugStringA("[Marni] Device lost on Present\n");
            }
        }

        // Re-bind render target after present
        if (pD3D->m_pRenderTargetView) {
            pD3D->m_pD3DContext->OMSetRenderTargets(1, &pD3D->m_pRenderTargetView,
                                                     pD3D->m_pDepthStencilView);
        }
    }

    return 1;
}

// [5] HandleWindowMessage - 0x00448b60
// Handles window messages. Returns 1 to continue default processing, 0 to skip.
static int VTable_HandleWindowMessage(void* self, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)self;

    switch (msg) {
    case WM_ACTIVATE:
        // Window activation state
        if (LOWORD(wParam) != WA_INACTIVE) {
            pD3D->m_isActive = TRUE;
        } else {
            pD3D->m_isActive = FALSE;
        }
        return 1;

    case WM_SIZE:
        // Handle window resize if windowed
        if (!pD3D->m_isFullScreen && pD3D->m_pSwapChain && LOWORD(lParam) > 0) {
            if (pD3D->m_pRenderTargetView) {
                pD3D->m_pD3DContext->OMSetRenderTargets(0, NULL, NULL);
                pD3D->m_pRenderTargetView->Release();
                pD3D->m_pRenderTargetView = NULL;
            }

            DWORD newW = LOWORD(lParam);
            DWORD newH = HIWORD(lParam);
            if (newW < 320) newW = 320;
            if (newH < 240) newH = 240;

            HRESULT hr = pD3D->m_pSwapChain->ResizeBuffers(2, newW, newH,
                DXGI_FORMAT_R8G8B8A8_UNORM, 0);
            if (SUCCEEDED(hr)) {
                pD3D->m_width = newW;
                pD3D->m_height = newH;
                g_dwScreenWidth = newW;
                g_dwScreenHeight = newH;

                ID3D11Texture2D* pBB = NULL;
                hr = pD3D->m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBB);
                if (SUCCEEDED(hr)) {
                    pD3D->m_pD3DDevice->CreateRenderTargetView(pBB, NULL, &pD3D->m_pRenderTargetView);
                    pBB->Release();
                }

                D3D11_VIEWPORT vp = {};
                vp.Width = (float)newW;
                vp.Height = (float)newH;
                vp.MaxDepth = 1.0f;
                pD3D->m_pD3DContext->RSSetViewports(1, &vp);
            }
        }
        return 1;

    case WM_DESTROY:
        pD3D->m_isActive = FALSE;
        return 1;

    default:
        return 1;
    }
}

// [6] CreateTextureHandle - 0x0044c900
// Creates a texture from a PSX texture description.
// Returns a texture handle index (1-based) or 0 on failure.
static int VTable_CreateTextureHandle(void* self, void* texDesc, unsigned int flags, void* outHandle)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)self;
    if (pD3D == NULL || !pD3D->m_isInitialized) return 0;

    // This is a simplified version - the original has complex logic
    // for managing 512 texture slots with CMarniBits objects.
    // For the modern port, texture creation is handled by MarniCreateTexture
    // and ProcessTextureImage in TextureLoader.cpp.

    // Return a dummy handle for compatibility
    if (outHandle) {
        *(unsigned int*)outHandle = 1;
    }
    return 1;
}

// [7] CreateObjectHandle - 0x0044af90
// Creates a 3D object (mesh) handle. Returns handle index (1-based) or 0.
static unsigned int VTable_CreateObjectHandle(void* self, void* objDesc, unsigned char flags)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)self;
    if (pD3D == NULL || !pD3D->m_isInitialized) return 0;

    // 3D objects not yet implemented - return a dummy handle
    return 1;
}

// [8] DeleteTextureHandle - 0x0044b220
// Deletes a texture by handle index
static int VTable_DeleteTextureHandle(void* self, int handle)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)self;
    if (pD3D == NULL || handle == 0) return 0;

    // Simplified - texture cleanup handled by MarniCreateTexture outputs
    return 1;
}

// [9] DeleteObjectHandle - 0x0044b1c0
// Deletes a 3D object by handle index
static int VTable_DeleteObjectHandle(void* self, int handle)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)self;
    if (pD3D == NULL || handle == 0) return 0;

    // 3D objects not yet implemented
    return 1;
}

// [10] SetTexture - 0x00448300
// Sets the active texture for rendering
static int VTable_SetTexture(void* self, void* texData, unsigned int param)
{
    // In modern implementation, texture binding is done at draw time
    // via MarniDrawSprite which takes an explicit SRV parameter
    return 1;
}

// [11] ResetTextures - 0x00448380
// Resets texture state
static int VTable_ResetTextures(void* self)
{
    // Clear any bound texture state
    return 1;
}

// ============================================================================
// Global Marni System Functions
// ============================================================================

// IsGraphicsSystemReadyForOperation - 0x00497060
// Checks if the Marni D3D object exists and has its init flag set
BOOL IsGraphicsSystemReadyForOperation(void)
{
    if (g_pMarniDirect3D != NULL) {
        CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
        if (pD3D->m_isInitialized) {
            return TRUE;
        }
        OutputDebugStringA("[Marni] Graphics system not initialized\n");
        return FALSE;
    }
    if (g_hWnd == NULL) {
        return TRUE; // No window = no graphics needed
    }
    OutputDebugStringA("[Marni] Graphics system unavailable (fallback)\n");
    return FALSE;
}

// InitializeMarniSystem - 0x004970c0
// Creates the CMarniDirect3D object, initializes joysticks, lights
void InitializeMarniSystem(void)
{
    // Clamp screen dimensions
    if (g_dwScreenWidth < 320)  g_dwScreenWidth = 640;
    if (g_dwScreenHeight < 240) g_dwScreenHeight = 480;

    // Validate display mode ID
    if ((int)g_dwSelectedDisplayModeID < 0) {
        g_dwSelectedDisplayModeID = 0;
    }

    // Allocate the Marni D3D object (original size: 0x21DC)
    void* pMem = operator_new(0x21DC);
    if (pMem == NULL) {
        OutputDebugStringA("[Marni] Failed to allocate CMarniDirect3D\n");
        g_pMarniDirect3D = NULL;
        return;
    }

    // Call constructor
    g_pMarniDirect3D = CMarniDirect3D_Constructor(
        pMem,
        g_hWnd,
        g_dwScreenWidth,
        g_dwScreenHeight,
        g_dwSelectedDisplayModeID,
        g_dwSelectedDisplayAdapterID
    );

    if (!IsGraphicsSystemReadyForOperation()) {
        ShowMessageBox(NULL,
            "Failed to initialize the Graphics System",
            "RESIDENT EVIL", MB_OK | MB_ICONSTOP);
        CleanupVideoConfigAndSaveAllSettings();
        DestroyWindow(g_hWnd);
        return;
    }

    // Initialize joysticks (XInput)
    InitJoysticks();

    // Check SideWinder pad (legacy check, not relevant for XInput)
    int swResult = IsSideWinderPadConnected();
    g_bIsSideWinderConnected = (swResult == 0);

    // Create default lights
    CreateLights(3);

    // Hide cursor in fullscreen
    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (pD3D != NULL && pD3D->m_isFullScreen) {
        ShowCursor(FALSE);
        g_isGameCursorHiddenFlag = FALSE;
    }

    g_GameInitTime = timeGetTime();
}

// EnumerateDisplayModes - 0x004976c0
// Enumerates available display modes from the Marni D3D object
void EnumerateDisplayModes(void)
{
    if (g_pMarniDirect3D == NULL) return;

    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;

    // Use DXGI to enumerate display modes from the adapter
    int count = 0;
    IDXGIDevice* pDXGIDevice = NULL;
    if (pD3D->m_pD3DDevice) {
        HRESULT hr = pD3D->m_pD3DDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&pDXGIDevice);
        if (SUCCEEDED(hr)) {
            IDXGIAdapter* pAdapter = NULL;
            hr = pDXGIDevice->GetAdapter(&pAdapter);
            if (SUCCEEDED(hr)) {
                IDXGIOutput* pOutput = NULL;
                hr = pAdapter->EnumOutputs(0, &pOutput);
                if (SUCCEEDED(hr)) {
                    UINT numModes = 0;
                    // Get number of display modes
                    pOutput->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, 0, &numModes, NULL);
                    if (numModes > 0 && numModes <= (UINT)MAX_DISPLAY_MODES) {
                        DXGI_MODE_DESC* modes = new DXGI_MODE_DESC[numModes];
                        pOutput->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, 0, &numModes, modes);

                        for (UINT i = 0; i < numModes && count < MAX_DISPLAY_MODES; i++) {
                            // Deduplicate by resolution
                            bool bDuplicate = false;
                            for (int j = 0; j < count; j++) {
                                if (g_DisplayModeBuffer[j].dwWidth == modes[i].Width &&
                                    g_DisplayModeBuffer[j].dwHeight == modes[i].Height) {
                                    bDuplicate = true;
                                    break;
                                }
                            }
                            if (!bDuplicate) {
                                g_DisplayModeBuffer[count].dwWidth  = modes[i].Width;
                                g_DisplayModeBuffer[count].dwHeight = modes[i].Height;
                                g_DisplayModeBuffer[count].dwBPP    = 32;
                                g_DisplayModeBuffer[count].dwRefreshRate = modes[i].RefreshRate.Numerator / modes[i].RefreshRate.Denominator;
                                g_DisplayModeBuffer[count].dwFlags  = 1; // fullscreen
                                count++;
                            }
                        }
                        delete[] modes;
                    }

                    // Add windowed mode as first entry
                    // Shift existing entries up
                    for (int i = count; i > 0; i--) {
                        g_DisplayModeBuffer[i] = g_DisplayModeBuffer[i - 1];
                    }
                    g_DisplayModeBuffer[0].dwWidth  = 640;
                    g_DisplayModeBuffer[0].dwHeight = 480;
                    g_DisplayModeBuffer[0].dwBPP    = 32;
                    g_DisplayModeBuffer[0].dwRefreshRate = 60;
                    g_DisplayModeBuffer[0].dwFlags  = 0; // windowed
                    count++;

                    pOutput->Release();
                }
                pAdapter->Release();
            }
            pDXGIDevice->Release();
        }
    }

    // Fallback: provide default modes if enumeration failed
    if (count == 0) {
        g_DisplayModeBuffer[0].dwWidth  = 640;
        g_DisplayModeBuffer[0].dwHeight = 480;
        g_DisplayModeBuffer[0].dwBPP    = 32;
        g_DisplayModeBuffer[0].dwRefreshRate = 60;
        g_DisplayModeBuffer[0].dwFlags  = 0;

        g_DisplayModeBuffer[1].dwWidth  = 800;
        g_DisplayModeBuffer[1].dwHeight = 600;
        g_DisplayModeBuffer[1].dwBPP    = 32;
        g_DisplayModeBuffer[1].dwRefreshRate = 60;
        g_DisplayModeBuffer[1].dwFlags  = 1;

        g_DisplayModeBuffer[2].dwWidth  = 1024;
        g_DisplayModeBuffer[2].dwHeight = 768;
        g_DisplayModeBuffer[2].dwBPP    = 32;
        g_DisplayModeBuffer[2].dwRefreshRate = 60;
        g_DisplayModeBuffer[2].dwFlags  = 1;

        g_DisplayModeBuffer[3].dwWidth  = 1280;
        g_DisplayModeBuffer[3].dwHeight = 720;
        g_DisplayModeBuffer[3].dwBPP    = 32;
        g_DisplayModeBuffer[3].dwRefreshRate = 60;
        g_DisplayModeBuffer[3].dwFlags  = 1;

        g_DisplayModeBuffer[4].dwWidth  = 1920;
        g_DisplayModeBuffer[4].dwHeight = 1080;
        g_DisplayModeBuffer[4].dwBPP    = 32;
        g_DisplayModeBuffer[4].dwRefreshRate = 60;
        g_DisplayModeBuffer[4].dwFlags  = 1;

        count = 5;
    }

    g_NumDisplayModes = count;
}

// EnumerateD3DRenderers - 0x004977f0
// Enumerates available D3D renderers (adapters)
void EnumerateD3DRenderers(void)
{
    int count = 0;

    // Enumerate DXGI adapters
    IDXGIFactory* pFactory = NULL;
    HRESULT hr = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&pFactory);
    if (SUCCEEDED(hr)) {
        IDXGIAdapter* pAdapter = NULL;
        for (UINT i = 0; pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND && count < 8; i++) {
            DXGI_ADAPTER_DESC desc;
            if (SUCCEEDED(pAdapter->GetDesc(&desc))) {
                // Convert wide char description to char
                WideCharToMultiByte(CP_ACP, 0, desc.Description, -1,
                                    g_D3DRenderers[count].name, 256, NULL, NULL);
                g_D3DRenderers[count].flags = 1; // available
                count++;
            }
            pAdapter->Release();
        }
        pFactory->Release();
    }

    if (count == 0) {
        // Fallback
        strcpy_s(g_D3DRenderers[0].name, "Primary Display Adapter");
        g_D3DRenderers[0].flags = 1;
        count = 1;
    }

    g_NumD3DRenderersAvailable = count;
}

// InitJoysticks - 0x00420770
// Initializes joystick/gamepad input via XInput
void InitJoysticks(void)
{
    // XInput automatically handles up to 4 controllers
    // Just verify XInput is available by checking controller 0
    XINPUT_STATE state;
    DWORD result = XInputGetState(0, &state);

    char debugMsg[128];
    sprintf_s(debugMsg, "[Marni] XInput: controller 0 %s\n",
              (result == ERROR_SUCCESS) ? "connected" : "not connected");
    OutputDebugStringA(debugMsg);
}

// IsSideWinderPadConnected - 0x0040b610
// Checks if a Microsoft SideWinder pad is connected (legacy check)
// For modern implementation, use XInput instead
int IsSideWinderPadConnected(void)
{
    // Check if any XInput controller is connected
    XINPUT_STATE state;
    for (DWORD i = 0; i < 4; i++) {
        if (XInputGetState(i, &state) == ERROR_SUCCESS) {
            return 0; // Connected
        }
    }
    return 1; // Not connected
}

// CreateLights - 0x00448440
// Creates default 3D scene lights. Simplified for modern D3D11.
void CreateLights(int numLights)
{
    // In the original, this created D3D lights for 3D rendering.
    // In our modern port, lighting is handled by pixel shaders.
    // Store the count for reference.
    (void)numLights;
}

// ============================================================================
// Drawing Functions
// ============================================================================

// MarniPresent - Present frame to screen (wrapper for vtable[4])
// 0x00448ff0
void MarniPresent(void)
{
    VTable_Present(g_pMarniDirect3D);
}

// MarniClear - Clear render target (wrapper for vtable[3])
// 0x0044b320
void MarniClear(void)
{
    VTable_Clear(g_pMarniDirect3D);
}

// PresentFrame - Alias for MarniPresent
void PresentFrame(void)
{
    MarniPresent();
}

// ClearScreen - Alias for MarniClear
void ClearScreen(void)
{
    MarniClear();
}

// MarniGetDevice - Returns the Marni D3D object pointer
void* MarniGetDevice(void)
{
    return g_pMarniDirect3D;
}

// MarniDrawRect - Debug/test helper: draw a filled rectangle
void MarniDrawRect(int x, int y, int w, int h, DWORD color)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (pD3D == NULL || !pD3D->m_isInitialized) return;
    if (pD3D->m_pD3DContext == NULL) return;
    if (pD3D->m_pQuadVB == NULL || pD3D->m_pQuadVS == NULL) return;

    // Build quad vertices - 6 vertices for 2 triangles (triangle list)
    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = (color & 0xFF) / 255.0f;
    float a = ((color >> 24) & 0xFF) / 255.0f;

    float x0 = (float)x;
    float x1 = (float)(x + w);
    float y0 = (float)y;
    float y1 = (float)(y + h);

    QuadVertex vertices[6];
    // Triangle 1: TL, TR, BL
    vertices[0] = { x0, y0, 0.0f, 0.0f, r, g, b, a };
    vertices[1] = { x1, y0, 1.0f, 0.0f, r, g, b, a };
    vertices[2] = { x0, y1, 0.0f, 1.0f, r, g, b, a };
    // Triangle 2: BL, TR, BR
    vertices[3] = { x0, y1, 0.0f, 1.0f, r, g, b, a };
    vertices[4] = { x1, y0, 1.0f, 0.0f, r, g, b, a };
    vertices[5] = { x1, y1, 1.0f, 1.0f, r, g, b, a };

    // Map vertex buffer
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = pD3D->m_pD3DContext->Map(pD3D->m_pQuadVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return;

    memcpy(mapped.pData, vertices, sizeof(vertices));
    pD3D->m_pD3DContext->Unmap(pD3D->m_pQuadVB, 0);

    // Build ortho projection matrix
    SpriteConstantBuffer cb;
    BuildOrthoMatrix(&cb.mvp[0][0], 0.0f, (float)pD3D->m_width, (float)pD3D->m_height, 0.0f);

    // Update constant buffer
    D3D11_MAPPED_SUBRESOURCE cbMapped;
    hr = pD3D->m_pD3DContext->Map(pD3D->m_pSpriteCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &cbMapped);
    if (SUCCEEDED(hr)) {
        memcpy(cbMapped.pData, &cb, sizeof(cb));
        pD3D->m_pD3DContext->Unmap(pD3D->m_pSpriteCB, 0);
    }

    // Set pipeline state
    UINT stride = sizeof(QuadVertex);
    UINT offset = 0;
    pD3D->m_pD3DContext->IASetVertexBuffers(0, 1, &pD3D->m_pQuadVB, &stride, &offset);
    pD3D->m_pD3DContext->IASetInputLayout(pD3D->m_pQuadInputLayout);
    pD3D->m_pD3DContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    pD3D->m_pD3DContext->VSSetShader(pD3D->m_pQuadVS, NULL, 0);
    pD3D->m_pD3DContext->VSSetConstantBuffers(0, 1, &pD3D->m_pSpriteCB);
    pD3D->m_pD3DContext->PSSetShader(pD3D->m_pQuadPS, NULL, 0);

    // Use white texture for solid color rects
    ID3D11ShaderResourceView* pSRV = pD3D->m_pWhiteSRV ? pD3D->m_pWhiteSRV : pD3D->m_pFontSRV;
    pD3D->m_pD3DContext->PSSetShaderResources(0, 1, &pSRV);
    pD3D->m_pD3DContext->PSSetSamplers(0, 1, &pD3D->m_pSamplerLinear);

    // Set blend state
    float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    pD3D->m_pD3DContext->OMSetBlendState(pD3D->m_pBlendAlpha, blendFactor, 0xFFFFFFFF);

    // Disable depth for 2D sprite rendering
    if (pD3D->m_pDepthDisabled) {
        pD3D->m_pD3DContext->OMSetDepthStencilState(pD3D->m_pDepthDisabled, 0);
    }

    // Draw 6 vertices (2 triangles)
    pD3D->m_pD3DContext->Draw(6, 0);
}

// MarniDrawSprite - Draw a textured colored quad at screen coordinates
// Used by PrintText8x14 and sprite rendering
void MarniDrawSprite(float x, float y, float w, float h,
                     float u0, float v0, float u1, float v1,
                     DWORD color, ID3D11ShaderResourceView* srv)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (pD3D == NULL || !pD3D->m_isInitialized) return;
    if (pD3D->m_pD3DContext == NULL) return;
    if (pD3D->m_pQuadVB == NULL || pD3D->m_pQuadVS == NULL) return;

    // Extract color components
    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = (color & 0xFF) / 255.0f;
    float a = ((color >> 24) & 0xFF) / 255.0f;

    // Build quad - 6 vertices for 2 triangles (triangle list)
    QuadVertex vertices[6];
    float x1 = x + w;
    float y1 = y + h;
    // Triangle 1: TL, TR, BL  (counter-clockwise in NDC)
    vertices[0] = { x,  y,  u0, v0, r, g, b, a };
    vertices[1] = { x1, y,  u1, v0, r, g, b, a };
    vertices[2] = { x,  y1, u0, v1, r, g, b, a };
    // Triangle 2: BL, TR, BR  (counter-clockwise in NDC)
    vertices[3] = { x,  y1, u0, v1, r, g, b, a };
    vertices[4] = { x1, y,  u1, v0, r, g, b, a };
    vertices[5] = { x1, y1, u1, v1, r, g, b, a };

    // Map vertex buffer (discard and rewrite)
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = pD3D->m_pD3DContext->Map(pD3D->m_pQuadVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return;

    memcpy(mapped.pData, vertices, sizeof(vertices));
    pD3D->m_pD3DContext->Unmap(pD3D->m_pQuadVB, 0);

    // Build ortho projection matrix (screen coordinates: top-left origin)
    SpriteConstantBuffer cb;
    BuildOrthoMatrix(&cb.mvp[0][0], 0.0f, (float)pD3D->m_width, (float)pD3D->m_height, 0.0f);

    // Update constant buffer
    D3D11_MAPPED_SUBRESOURCE cbMapped;
    hr = pD3D->m_pD3DContext->Map(pD3D->m_pSpriteCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &cbMapped);
    if (SUCCEEDED(hr)) {
        memcpy(cbMapped.pData, &cb, sizeof(cb));
        pD3D->m_pD3DContext->Unmap(pD3D->m_pSpriteCB, 0);
    } else {
        return;
    }

    // Set pipeline state
    UINT stride = sizeof(QuadVertex);
    UINT offset = 0;
    pD3D->m_pD3DContext->IASetVertexBuffers(0, 1, &pD3D->m_pQuadVB, &stride, &offset);
    pD3D->m_pD3DContext->IASetInputLayout(pD3D->m_pQuadInputLayout);
    pD3D->m_pD3DContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    pD3D->m_pD3DContext->VSSetShader(pD3D->m_pQuadVS, NULL, 0);
    pD3D->m_pD3DContext->VSSetConstantBuffers(0, 1, &pD3D->m_pSpriteCB);
    pD3D->m_pD3DContext->PSSetShader(pD3D->m_pQuadPS, NULL, 0);

    // Set texture (use white texture fallback if srv is NULL)
    ID3D11ShaderResourceView* pBindSRV = srv;
    if (pBindSRV == NULL) {
        pBindSRV = pD3D->m_pWhiteSRV;
    }
    pD3D->m_pD3DContext->PSSetShaderResources(0, 1, &pBindSRV);
    // Use point sampler for fonts, linear for other textures
    static int s_fontDrawCount = 0;
    if (pBindSRV == pD3D->m_pFontSRV && pD3D->m_pSamplerPoint) {
        if (s_fontDrawCount < 5) {
            char dbg[128];
            sprintf(dbg, "[FONT] point sampler applied, draw #%d, srv=%p, sampler=%p\n",
                    s_fontDrawCount, pBindSRV, pD3D->m_pSamplerPoint);
            OutputDebugStringA(dbg);
            s_fontDrawCount++;
        }
        pD3D->m_pD3DContext->PSSetSamplers(0, 1, &pD3D->m_pSamplerPoint);
    } else {
        if (pBindSRV == pD3D->m_pFontSRV && s_fontDrawCount < 5) {
            char dbg[128];
            sprintf(dbg, "[FONT] WARNING: point sampler MISSING! srv=%p, sampler=%p\n",
                    pBindSRV, pD3D->m_pSamplerPoint);
            OutputDebugStringA(dbg);
            s_fontDrawCount++;
        }
        pD3D->m_pD3DContext->PSSetSamplers(0, 1, &pD3D->m_pSamplerLinear);
    }

    // Set blend state
    float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    pD3D->m_pD3DContext->OMSetBlendState(pD3D->m_pBlendAlpha, blendFactor, 0xFFFFFFFF);

    // Disable depth for 2D sprite rendering
    if (pD3D->m_pDepthDisabled) {
        pD3D->m_pD3DContext->OMSetDepthStencilState(pD3D->m_pDepthDisabled, 0);
    }

    // Draw 6 vertices (2 triangles)
    pD3D->m_pD3DContext->Draw(6, 0);
}

// MarniCreateTexture - Create a D3D11 texture from raw pixel data
// Supports 16-bit (RGB555/ARGB1555), 24-bit (RGB888), and 32-bit (RGBA8888) pixel formats
void MarniCreateTexture(int width, int height, int bpp, void* pixelData,
                        ID3D11Texture2D** outTex, ID3D11ShaderResourceView** outSRV)
{
    if (width <= 0 || height <= 0 || pixelData == NULL || outTex == NULL || outSRV == NULL) return;

    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (pD3D == NULL || pD3D->m_pD3DDevice == NULL) return;

    *outTex = NULL;
    *outSRV = NULL;

    HRESULT hr;
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = (UINT)width;
    texDesc.Height = (UINT)height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    // Convert pixel data to RGBA8 based on bpp
    DWORD* rgbaData = NULL;
    D3D11_SUBRESOURCE_DATA initData = {};

    if (bpp == 32) {
        // Assume RGBA8888 (or BGRA8888). Use directly.
        texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        initData.pSysMem = pixelData;
        initData.SysMemPitch = width * 4;
    } else if (bpp == 24) {
        // Convert RGB888 to RGBA8888
        texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        DWORD totalPixels = width * height;
        rgbaData = new DWORD[totalPixels];
        BYTE* src = (BYTE*)pixelData;
        for (DWORD i = 0; i < totalPixels; i++) {
            rgbaData[i] = (src[2] << 16) | (src[1] << 8) | src[0] | 0xFF000000;
            src += 3;
        }
        initData.pSysMem = rgbaData;
        initData.SysMemPitch = width * 4;
    } else if (bpp == 16) {
        // Convert RGB555/ARGB1555 to RGBA8888
        texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        DWORD totalPixels = width * height;
        rgbaData = new DWORD[totalPixels];
        WORD* src16 = (WORD*)pixelData;
        for (DWORD i = 0; i < totalPixels; i++) {
            WORD pixel = src16[i];
            // Assume ARGB1555 (bit 15 = alpha)
            DWORD a = (pixel & 0x8000) ? 0xFF : 0x00;
            DWORD r = ((pixel >> 10) & 0x1F) * 255 / 31;
            DWORD g = ((pixel >> 5) & 0x1F) * 255 / 31;
            DWORD b = (pixel & 0x1F) * 255 / 31;
            rgbaData[i] = (a << 24) | (r << 16) | (g << 8) | b;
        }
        initData.pSysMem = rgbaData;
        initData.SysMemPitch = width * 4;
    } else if (bpp == 8) {
        // 8-bit paletted - treated as grayscale for simplicity
        texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        DWORD totalPixels = width * height;
        rgbaData = new DWORD[totalPixels];
        BYTE* src8 = (BYTE*)pixelData;
        for (DWORD i = 0; i < totalPixels; i++) {
            DWORD c = src8[i];
            rgbaData[i] = 0xFF000000 | (c << 16) | (c << 8) | c;
        }
        initData.pSysMem = rgbaData;
        initData.SysMemPitch = width * 4;
    } else if (bpp == 4) {
        // 4-bit paletted - 4 pixels per 16-bit WORD, expand to grayscale
        texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        DWORD totalPixels = width * height;
        int totalWords = totalPixels / 4;  // 4 pixels per WORD
        rgbaData = new DWORD[totalPixels];
        WORD* src = (WORD*)pixelData;
        for (int i = 0; i < totalWords; i++) {
            WORD w = src[i];
            DWORD p0 = ((w >> 0)  & 0xF) * 17;
            DWORD p1 = ((w >> 4)  & 0xF) * 17;
            DWORD p2 = ((w >> 8)  & 0xF) * 17;
            DWORD p3 = ((w >> 12) & 0xF) * 17;
            int base = i * 4;
            rgbaData[base]     = 0xFF000000 | (p0 << 16) | (p0 << 8) | p0;
            rgbaData[base + 1] = 0xFF000000 | (p1 << 16) | (p1 << 8) | p1;
            rgbaData[base + 2] = 0xFF000000 | (p2 << 16) | (p2 << 8) | p2;
            rgbaData[base + 3] = 0xFF000000 | (p3 << 16) | (p3 << 8) | p3;
        }
        initData.pSysMem = rgbaData;
        initData.SysMemPitch = width * 4;
    } else {
        // Unsupported format
        OutputDebugStringA("[Marni] Unsupported pixel format BPP\n");
        return;
    }

    // Create texture
    hr = pD3D->m_pD3DDevice->CreateTexture2D(&texDesc, &initData, outTex);
    if (FAILED(hr)) {
        if (rgbaData) delete[] rgbaData;
        OutputDebugStringA("[Marni] Failed to create D3D11 texture\n");
        return;
    }

    // Create shader resource view
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    hr = pD3D->m_pD3DDevice->CreateShaderResourceView(*outTex, &srvDesc, outSRV);
    if (FAILED(hr)) {
        (*outTex)->Release();
        *outTex = NULL;
        if (rgbaData) delete[] rgbaData;
        OutputDebugStringA("[Marni] Failed to create SRV\n");
        return;
    }

    if (rgbaData) delete[] rgbaData;
}

// ============================================================================
// Display Mode Helper Functions - implemented in DisplayConfig.cpp
// ============================================================================
