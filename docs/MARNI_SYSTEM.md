# Marni System — Complete Implementation Reference

## Table of Contents

1. [Overview](#overview)
2. [CMarniDirect3D — Main Graphics Class](#1-cmarnidirect3d--main-graphics-class)
3. [CMarniBits — Surface/Bitmap Class](#2-cmarnibits--surfacebitmap-class)
4. [PSXTexture — PS1 TIM/PIX Texture Loader](#3-psxtexture--ps1-timpix-texture-loader)
5. [CDirect3DObject / CMarniExecuteBuffer — 3D Object Classes](#3a-cdirect3dobject--cmarniexecutebuffer--3d-object-classes)
6. [CMarniDirect3DTMD — TMD 3D Model Renderer](#3b-cmarnidirect3dtmd--tmd-3d-model-renderer)
7. [CMarniViewport2 — 3D Viewport Class](#3c-cmarniviewport2--3d-viewport-class)
8. [DirectInput — Marni Input System](#3d-directinput--marni-input-system)
9. [PSYQ GPU Emulation — Sprite Renderer](#4-psyq-gpu-emulation--sprite-renderer)
10. [Texture Page Management](#5-texture-page-management)
11. [Global Arrays (Static Initialization)](#6-global-arrays-static-initialization)
12. [ExecAsync — Async Task System](#7-execasync--async-task-system)
13. [Data Flow Diagram](#8-data-flow-diagram)
14. [Source File Map](#9-source-file-map)
15. [PSYQ-to-DirectX Mapping](#10-psyq-to-directx-mapping)

---

## Overview

The Marni System is Capcom's PSYQ-to-DirectX compatibility layer that allows PlayStation 1 game code to run on Windows PC with minimal changes. It wraps PS1 graphics, input, and audio APIs behind DirectX interfaces.

| Aspect | Original (1997) | Modern Port |
|--------|-----------------|-------------|
| Graphics | DirectX 5.0 (DirectDraw, Direct3D 5) | Direct3D 11 |
| Audio | DirectSound | XAudio2 |
| Input | DirectInput | XInput |
| Source Files | `src/marni/*`, `src/game/TextureLoader.cpp`, `src/game/SpriteRenderer.*` |

### Key Architectural Decisions

1. **PSYQ Compatibility Layer**: The Marni System lets most PlayStation code run on Windows unchanged
2. **32-bit Architecture**: The game is strictly 32-bit, using Win32 API (no 64-bit types or functions)
3. **VTable Compatibility**: All original class vtables are preserved at their original addresses to maintain binary layout compatibility with the original code that calls through function pointers
4. **Task-Based Game Logic**: Game logic is organized into tasks scheduled each frame

### Entry Point

The game entry point is `main` at `0x00441350`.

---

## 1. CMarniDirect3D — Main Graphics Class

**Files:** `src/marni/MarniSystem.h`, `src/marni/MarniSystem.cpp`  
**Original VTable Address:** `0x004af230`  
**Object Size:** `0x21DC` bytes (8676 bytes)  
**Global Instance:** `g_pMarniDirect3D` at `0x00ac4028`

### VTable (12 Entries)

| Slot | Address | Function | Signature | Description |
|------|---------|----------|-----------|-------------|
| [0] | `0x00448630` | `RequestVideoMemory` | `int(void* self)` | Allocate video memory from D3D device |
| [1] | `0x00449300` | `ChangeDisplayMode` | `int(void* self, int mode)` | Change display resolution / mode |
| [2] | `0x0044a0d0` | `SetD3DRenderer` | `void(void* self, int renderer)` | Select D3D renderer by index (HW/SW) |
| [3] | `0x0044b320` | `Clear` | `int(void* self)` | Clear Z-buffer and/or render target |
| [4] | `0x00448ff0` | `Present` | `int(void* self)` | Present/flip back buffer to front |
| [5] | `0x00448b60` | `HandleWindowMessage` | `int(void* self, HWND, UINT, WPARAM, LPARAM)` | Handle WM_ACTIVATE and window messages |
| [6] | `0x0044c900` | `CreateTextureHandle` | `int(void* self, void* texDesc, uint flags, void* out)` | Create texture handle from pixel data |
| [7] | `0x0044af90` | `CreateObjectHandle` | `uint(void* self, void* objDesc, byte flags)` | Create 3D object handle for rendering |
| [8] | `0x0044b220` | `DeleteTextureHandle` | `int(void* self, int handle)` | Delete texture handle by index |
| [9] | `0x0044b1c0` | `DeleteObjectHandle` | `int(void* self, int handle)` | Delete 3D object handle by index |
| [10] | `0x00448300` | `SetTexture` | `int(void* self, void* texData, uint param)` | Bind texture / submit sprite command |
| [11] | `0x00448380` | `ResetTextures` | `int(void* self)` | Reset texture state |

### VTable Calling Convention

The original game passes `this` explicitly as the first argument (not via `__thiscall`). Each vtable entry is typed accordingly:

```cpp
// Example: VTable_Present wrapper
static PFN_Present VTable_Present_g = NULL;
static int VTable_Present(void* self) {
    return ((PFN_Present)(((void***)self)[0][4]))(self);
}
```

### Original Member Variables (offsets from Ghidra)

| Offset | Type | Field | Description |
|--------|------|-------|-------------|
| 0x00 | void** | `vtable` | VTable pointer (`0x004af230`) |
| 0x10 | DWORD | `m_width` | Screen width |
| 0x14 | DWORD | `m_height` | Screen height |
| 0x18 | DWORD | `m_bitDepth` | Color bit depth |
| 0x3C | BOOL | `m_isInitialized` | Initialization flag |
| 0x68 | BOOL | `m_isFullScreen` | Fullscreen mode flag |
| 0x74 | BOOL | `m_isActive` | Window active flag |
| 0x78 | DWORD | `m_selectedMode` | Display mode index |
| 0x30C | DWORD | `m_deviceType` | Device type (0-6, 5=Software) |
| 0x314 | DWORD | `m_currentMode` | Current display mode |
| 0x324 | DWORD | `m_scratch` | Scratch/state field |

### Modern D3D11 Members (attached at high offsets)

These replace the original DirectDraw/Direct3D 5 interface pointers (`m_lpDD`, `m_lpDD2`, `m_lpDDS_Front`, `m_lpDDS_Back`, etc.):

```cpp
// D3D11 Device & Pipeline
ID3D11Device*           m_pD3DDevice;           // D3D11 device
ID3D11DeviceContext*    m_pD3DContext;          // Immediate context
IDXGISwapChain*         m_pSwapChain;           // Swap chain (back buffer presentation)
ID3D11RenderTargetView* m_pRenderTargetView;     // Back buffer RTV
ID3D11Texture2D*        m_pDepthStencil;         // Depth/stencil buffer
ID3D11DepthStencilView* m_pDepthStencilView;     // DSV
ID3D11RasterizerState*  m_pRasterStateScissor;   // Rasterizer with scissor enabled

// Shaders & Pipeline State
ID3D11VertexShader*     m_pQuadVS;               // Quad vertex shader
ID3D11PixelShader*      m_pQuadPS;               // Quad pixel shader (textured + tint)
ID3D11InputLayout*      m_pQuadInputLayout;      // Input layout for QuadVertex
ID3D11Buffer*           m_pQuadVB;               // Reusable full-screen quad VB
ID3D11Buffer*           m_pSpriteCB;             // Sprite constant buffer (MVP matrix)

// Blend & Sampler State
ID3D11BlendState*       m_pBlendAlpha;            // Alpha blending state
ID3D11SamplerState*     m_pSamplerLinear;         // Linear texture sampler
ID3D11DepthStencilState* m_pDepthDisabled;        // Depth-disabled state (for 2D sprites)

// Fallback Textures
ID3D11Texture2D*        m_pFontTexture;           // Font texture (from TIM bank 0x1E)
ID3D11ShaderResourceView* m_pFontSRV;             // Font SRV
int                     m_FontTexWidth;            // Font texture width
int                     m_FontTexHeight;           // Font texture height
ID3D11Texture2D*        m_pWhiteTex;              // 1x1 white fallback texture
ID3D11ShaderResourceView* m_pWhiteSRV;            // White SRV (for solid-color quads)
```

### Embedded HLSL Shaders

Shaders are compiled at runtime using `D3DCompile`:

```hlsl
// g_QuadVS_Source - Vertex Shader
cbuffer SpriteCB : register(b0) {
    row_major float4x4 g_MVP;
};

struct VS_INPUT {
    float2 pos : POSITION;
    float2 tex : TEXCOORD0;
    float4 col : COLOR0;
};

struct VS_OUTPUT {
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD0;
    float4 col : COLOR0;
};

VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    output.pos = mul(float4(input.pos, 0.0f, 1.0f), g_MVP);
    output.tex = input.tex;
    output.col = input.col;
    return output;
}

// g_QuadPS_Source - Pixel Shader
Texture2D g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD0;
    float4 col : COLOR0;
};

float4 main(PS_INPUT input) : SV_TARGET {
    float4 texColor = g_Texture.Sample(g_Sampler, input.tex);
    return texColor * input.col;
}
```

### QuadVertex Structure

Used for all sprite/quad rendering:

```cpp
struct QuadVertex {
    float x, y;         // Screen position
    float u, v;         // Texture coordinates
    float r, g, b, a;   // Color (0.0 - 1.0)
};
```

### Global Functions

| Function | Description | Original Address |
|----------|-------------|------------------|
| `InitializeMarniSystem()` | Creates CMarniDirect3D instance, inputs, lights, joysticks | — |
| `MarniPresent()` | Presents frame via `vtable[4]` → `IDXGISwapChain::Present` | `0x00448ff0` |
| `MarniClear()` | Clears render target via `vtable[3]` → `ClearRenderTargetView` | `0x0044b320` |
| `MarniDrawSprite()` | Draws textured colored quad at screen coordinates via D3D11 | — |
| `MarniDrawRect()` | Debug helper: filled rectangle | — |
| `MarniCreateTexture()` | Creates `ID3D11Texture2D` + SRV from raw pixel data | — |
| `MarniGetDevice()` | Returns `g_pMarniDirect3D` pointer | — |
| `IsGraphicsSystemReadyForOperation()` | Checks initialization state | — |
| `EnumerateDisplayModes()` | Lists available display modes | — |
| `EnumerateD3DRenderers()` | Lists available D3D renderers (HW/SW) | — |
| `InitJoysticks()` | Initializes XInput gamepads | — |
| `IsSideWinderPadConnected()` | Checks for Microsoft SideWinder pad | — |
| `CreateLights(int numLights)` | Creates 3D scene lights | — |
| `UpdateVideoPlayback()` | FMV playback state machine | — |

---

## 2. CMarniBits — Surface/Bitmap Class

**Files:** `src/marni/MarniBits.h`, `src/marni/MarniBits.cpp`  
**Original VTable Address:** `0x004af008`  
**Object Size:** `0x54` bytes (84 bytes of fields up to offset `0x50`)

CMarniBits is the 2D surface/pixel buffer class. It wraps a raw pixel buffer and optional palette (CLUT), providing Lock/Unlock, pixel read/write, palette fill, and blit operations.

### VTable (7 Entries)

| Slot | Address | Ghidra Name | Method | Signature |
|------|---------|-------------|--------|-----------|
| [0] | `0x00403090` | `CMarniBits_Blt` | `Blt` | `int(void* self, void* srcRect, CMarniBits* srcSurface)` |
| [1] | `0x00402020` | `CMarniBits_BltFast` | `BltFast` | `int(void* self, void* dstRect, CMarniBits* src, void* srcRect2, DWORD flags, DWORD flags2, void* palette)` |
| [2] | `0x00401ee0` | `CMarniBits_UnlockStub` | `UnlockStub` | `int(void* self)` — returns 1 |
| [3] | `0x00401ef0` | `CMarniBits_PalBlt` | `PalBlt` | `int(void* self, CMarniBits* src, DWORD param3, int numEntries)` |
| [4] | `0x00403450` | `CMarniBits_Lock` | `Lock` | `int(void* self, void** outData, DWORD* outPitch)` |
| [5] | `0x004034c0` | `CMarniBits_Unlock` | `Unlock` | `int(void* self)` |
| [6] | `0x00404970` | `CMarniBits_Release` | `Release` | `int(void* self)` |

### VTable Implementation

```cpp
static void* CMarniBits_vtable[7] = {
    (void*)VTable_Blt,          // [0] 0x00403090
    (void*)VTable_BltFast,      // [1] 0x00402020
    (void*)VTable_UnlockStub,   // [2] 0x00401ee0
    (void*)VTable_PalBlt,       // [3] 0x00401ef0
    (void*)VTable_Lock,         // [4] 0x00403450
    (void*)VTable_Unlock,       // [5] 0x004034c0
    (void*)VTable_Release,      // [6] 0x00404970
};
```

### Member Variables

| Offset | Type | Field | Description |
|--------|------|-------|-------------|
| 0x00 | void** | `vtable` | VTable pointer (`0x004af008`) |
| 0x04 | void* | `m_pPixelData` | Pixel/surface data pointer |
| 0x08 | void* | `m_pPalette` | Palette (CLUT) data pointer |
| 0x0C | DWORD | `m_locked` | Lock state (0=unlocked, 1=locked) |
| 0x10 | BYTE | `m_redShift` | Red component bit shift |
| 0x12 | WORD | `m_redMask` | Red component bitmask |
| 0x14 | BYTE | `m_redWidth` | Red component bit width |
| 0x16 | BYTE | `m_greenShift` | Green component bit shift |
| 0x18 | WORD | `m_greenMask` | Green component bitmask |
| 0x1A | BYTE | `m_greenWidth` | Green component bit width |
| 0x1C | BYTE | `m_blueShift` | Blue component bit shift |
| 0x1E | WORD | `m_blueMask` | Blue component bitmask |
| 0x20 | BYTE | `m_blueWidth` | Blue component bit width |
| 0x22 | BYTE | `m_alphaShift` | Alpha component bit shift |
| 0x24 | WORD | `m_alphaMask` | Alpha component bitmask |
| 0x26 | BYTE | `m_alphaWidth` | Alpha component bit width |
| 0x2A | BYTE | `m_bitDepth` | Bits per pixel (4/8/16/24/32) |
| 0x2B | BYTE | `m_paletteFormat` | Palette depth (0x08=8bit, 0x10=16bit, 0x20=32bit) |
| 0x2C | DWORD | `m_width` | Surface width in pixels |
| 0x30 | DWORD | `m_height` | Surface height in pixels |
| 0x34 | DWORD | `m_pitch` | Row stride in bytes |
| 0x40 | DWORD | `m_isValid` | Surface valid flag |
| 0x44 | DWORD | `m_dataSource` | 0=external pointer, 1=owned allocation |
| 0x48 | DWORD | `m_hasPalette` | Has palette (non-zero = indexed/CLUT) |
| 0x4C | DWORD | `m_ownsPalette` | Palette ownership flag |
| 0x50 | DWORD | `m_flag50` | Additional flag |

### Non-Virtual Methods (14 Total)

| Address | Ghidra Name | C++ Method | Signature |
|---------|-------------|------------|-----------|
| `0x00403860` | `CMarniBits_CalcAddress` | `CalcAddress(x, y)` | `void*(int x, int y)` |
| `0x004033f0` | `CMarniBits_SetAddress` | `SetAddress(pixels, clut)` | `int(void* pixels, void* clut)` |
| `0x00403c90` | `CMarniBits_SetColor` | `SetColor(x, y, val, flags)` | `int(int x, int y, DWORD val, DWORD flags)` |
| `0x00404120` | `CMarniBits_GetColor` | `GetColor(x, y, &out)` | `int(int x, int y, DWORD* out)` |
| `0x00403b00` | `CMarniBits_SetPaletteColor` | `SetPaletteColor(idx, color, flags)` | `int(int idx, DWORD color, int flags)` |
| `0x00404390` | `CMarniBits_GetPaletteColor` | `GetPaletteColor(idx, &out)` | `int(int idx, DWORD* out)` |
| `0x00404210` | `CMarniBits_GetCurrentColor` | `GetCurrentColor(x, y, &out)` | `int(int x, int y, DWORD* out)` |
| `0x004044d0` | `CMarniBits_GetIndexColor` | `GetIndexColor(x, y, &out)` | `int(int x, int y, DWORD* out)` |
| `0x004039b0` | `CMarniBits_SetIndexColor` | `SetIndexColor(x, y, color, flags)` | `int(int x, int y, DWORD color, DWORD flags)` |
| `0x00403e20` | `CMarniBits_SetCurrentColor` | `SetCurrentColor(x, y, color, flags)` | `int(int x, int y, DWORD color, DWORD flags)` |
| `0x004034d0` | `CMarniBits_CopyFrom` | `CopyFrom(src)` | `int(CMarniBits* src)` |
| `0x00404690` | `MarniBits::CreateWork` | `CreateWork(w, h, bpp, palFlags)` | `int(int w, int h, int bpp, DWORD palFlags)` |
| `0x00403210` | `SaveBitmapToFile` | `SaveBitmapToFile(filename)` | `int(const char* filename)` |
| `0x00404910` | `CMarniBits_Constructor` | Constructor | Zeroes all fields, sets vtable |
| `0x00404960` | `CMarniBits_DestructorBody` | `DestructorBody()` | Resets vtable, calls Release |

### Key Method Descriptions

#### Blt (vtable[0]) — `0x00403090`
Copies pixels from a source surface with clipping. Uses target rectangle defined by the `m_clip*` fields. Validates dimensions and delegates pixel copy.

#### BltFast (vtable[1]) — `0x00402020`
Pixel-format-converting blit with support for:
- Scaling (stretch/shrink)
- Mirror/flip
- Color key (transparency)
- Alpha blending
- DDBLTFAST flags: `DDBLTFAST_NOCOLORKEY`, `DDBLTFAST_SRCCOLORKEY`, `DDBLTFAST_WAIT`

#### Lock / Unlock (vtable[4]/[5]) — `0x00403450` / `0x004034c0`
Lock returns a pointer to pixel data and the row pitch. Unlock must be called (once) after Lock. Nested locks are not supported; Lock returns an error if already locked.

#### SetCurrentColor — `0x00403e20`
Writes an ARGB color value at (x, y). For indexed surfaces (4bpp/8bpp), it finds the nearest CLUT match via Euclidean RGB distance. For direct-color surfaces (16bpp+), it converts to the native pixel format. Supports alpha blending with the existing pixel value when a non-zero blend parameter is specified.

#### CreateWork — `0x00404690`
Allocates pixel and palette buffers:
- `w * h * (bpp / 8)` bytes for pixel data
- `numColors * paletteEntrySize` bytes for palette
- Sets `m_dataSource = 1` (owned) and `m_isValid = 1`

#### SaveBitmapToFile — `0x00403210`
Exports the surface as a 24-bit BMP file. Handles all pixel format conversions internally.

### Pixel Format Support

| bpp | Type | CLUT Required | Palette Entry Size | Description |
|-----|------|---------------|--------------------|-------------|
| 4 | Indexed | Yes | 2 bytes (RGB555) | 16-color (PS1 4bpp TIM) |
| 8 | Indexed | Yes | 2 bytes (RGB555) | 256-color (PS1 8bpp TIM) |
| 16 | Direct | No | — | PSX RGB 5:5:5 (ABGR 1:5:5:5 or 5:6:5) |
| 24 | Direct | No | — | RGB 8:8:8 |
| 32 | Direct | No | — | ARGB 8:8:8:8 |

---

## 3. PSXTexture — PS1 TIM/PIX Texture Loader

**Files:** `src/marni/PSXTexture.h`, `src/marni/PSXTexture.cpp`  
**Object Size:** `0x348` bytes (840 bytes)  
**Embedded:** 8 × CMarniBits sub-objects at 0x68-byte intervals

### Layout

PSXTexture embeds 8 CMarniBits sub-objects using placement-new at construction:

| Offset | Content |
|--------|---------|
| 0x000 | CMarniBits #0 (main PSX texture surface) |
| 0x068 | CMarniBits #1 (CLUT entry 1) |
| 0x0D0 | CMarniBits #2 (CLUT entry 2) |
| 0x138 | CMarniBits #3 (CLUT entry 3) |
| 0x1A0 | CMarniBits #4 (CLUT entry 4) |
| 0x208 | CMarniBits #5 (CLUT entry 5) |
| 0x270 | CMarniBits #6 (CLUT entry 6) |
| 0x2D8 | CMarniBits #7 (CLUT entry 7) |
| 0x340 | `m_NumCLUTs` (DWORD) — active CLUT count |
| 0x344 | `m_IsInitialized` (DWORD) — initialized flag |

### Constructor — `0x0041fee0`

```cpp
PSXTexture::PSXTexture() {
    // Place-construct 8 CMarniBits sub-objects at 0x68-byte intervals
    for (int i = 0; i < 8; i++) {
        new ((BYTE*)this + i * 0x68) CMarniBits();
    }
    m_NumCLUTs = 0;       // offset 0x340
    m_IsInitialized = 0;  // offset 0x344
}
```

### Member Variables (key fields for TIM parsing)

| Offset | Type | Field | Description |
|--------|------|-------|-------------|
| 0x04 | void* | `m_pPixelData` | Pointer to pixel data buffer |
| 0x08 | DWORD | `m_Pitch` | Row stride |
| 0x2A | BYTE | `m_BitDepth` | Pixel bit depth (4, 8, 16) |
| 0x2B | BYTE | `m_FormatFlags` | Format flags from TIM header |
| 0x2C | DWORD | `m_WidthPixels` | Width in texture-specific units |
| 0x30 | DWORD | `m_Height` | Height in pixels |
| 0x34 | DWORD | `m_RowStride` | Bytes per row |
| 0x40 | DWORD | `m_IsLocked` | Surface lock flag |
| 0x44 | DWORD | `m_DataSource` | 0=file, 1=generated |
| 0x48 | DWORD | `m_HasCLUT` | Has color lookup table flag |
| 0x54-0x63 | WORD[8] | CLUT region | CLUT X, Y, W, H fields |
| 0x68-0x74 | DWORD[4] | `m_CLUT_Entries` | CLUT entry metadata |
| 0xC0-0x13C | DWORD[32] | `m_CLUT_Data` | Multi-CLUT color data |

### Methods

| Address | Name | Description |
|---------|------|-------------|
| `0x0041fee0` | Constructor | Place-constructs 8 embedded CMarniBits |
| `0x0041fb60` | `Store(imageData, copyData)` | Parse PSX TIM/PIX format |
| `0x0041fa60` | `LoadFromFile(filename)` | Open TIM file, read, call Store |
| `0x0041fb10` | `ClearCLUTEntries()` | Release all embedded CMarniBits via vtable[6] |
| `0x00420000` | `ArrayClear(tex)` | Clear + destroy all sub-objects |
| `0x0041ff60` | `ConstructElement(element)` | Construct single embedded CMarniBits |
| `0x0041ffb0` | `DestroyElement(element)` | Destroy single embedded CMarniBits |

### TIM File Format

The `Store` method at `0x0041fb60` parses the PS1 TIM image format:

```
Offset 0x00: Magic number  (must be 0x00000010)
Offset 0x04: Flags         (bit 3 = has CLUT palette)
```

**If CLUT present (bit 3 set):**
```
Offset 0x08: CLUT data size (bytes)
Offset 0x0C: CLUT origin    (low 16 = X, high 16 = Y)
Offset 0x10: CLUT dimensions (low 16 = width, high 16 = height)
Offset 0x14: CLUT color data
```

**Image data (after CLUT section):**
```
Offset +0x00: Image data size (bytes)
Offset +0x04: Image origin     (low 16 = X, high 16 = Y)
Offset +0x08: Width in words   (low 16), Height in pixels (high 16)
Offset +0x0C: Pixel data
```

**Bit depth values:**
- `0x00` = 4 bpp (16-color indexed)
- `0x01` = 8 bpp (256-color indexed)
- `0x02` = 16 bpp (direct color, RGB 5:5:5)

---

## 3A. CDirect3DObject / CMarniExecuteBuffer — 3D Object Classes

**File:** `src/marni/Marni3DObject.h`, `src/game/Marni3DObject.cpp`

### CDirect3DObject (Base Class)
**Size:** 0x38 bytes (14 DWORDs)  
**VTable:** 0x004af090 (8 entries)  
**Vertex Format:** 0x20 bytes/vertex (8 DWORDs: sx, sy, sz, rhw, color, specular, tu, tv)

| Slot | Address | Ghidra Name | Method | Description |
|------|---------|-------------|--------|-------------|
| [0] | 0x00427270 | `CMarniViewport2_Release` | `Release()` | Free vertex/index buffers |
| [1] | 0x00415e90 | `Direct3DObject_CreateWork` | `CreateWork(vtx,list,type)` | Alloc 32-byte vertex + 8/16-byte index buffers |
| [2] | 0x00415a80 | `Direct3DObject_GetVertex` | `GetVertex(idx,out)` | Read 8 DWORDs from vertex buffer |
| [3] | 0x00415b40 | `Direct3DObject_SetVertex` | `SetVertex(idx,data)` | Write 8 DWORDs to vertex buffer |
| [4] | 0x00415c10 | `Direct3DObject_GetList` | `GetList(idx,out)` | Read tri (3×WORD) or quad (4×WORD) indices |
| [5] | 0x00415d20 | `Direct3DObject_SetList` | `SetList(idx,data)` | Write indices; quads split to 2 tris with `0x700` opcodes |
| [6] | 0x004159e0 | `MarniPolyhedra_Lock` | `Lock(&vtx,&idx)` | Return buffer pointers, set lock=1 |
| [7] | 0x00415a50 | `MarniPolyhedra_Unlock` | `Unlock()` | Clear lock flag |

### CMarniExecuteBuffer (inherits CDirect3DObject)
**Constructor:** 0x00415f70  
**Destructor:** 0x00415fd0  
Created via `CMarniDirect3D::CreateObjectHandle` (vtable[7]). Manages D3D execute buffers for 3D rendering commands.

### CMarniPolyhedra (inherits CDirect3DObject)
Adds `CopyFrom()` (0x00426600) for copying vertex/index data between polyhedra. Used by sprite rendering pipeline for 3D geometry.

### Member Layout (CDirect3DObject)
| Offset | Type | Field | Description |
|--------|------|-------|-------------|
| 0x00 | void** | `vtable` | VTable pointer |
| 0x04 | void* | `m_pVertexBuffer` | Vertex data (0x20 bytes/vertex) |
| 0x08 | void* | `m_pIndexBuffer` | Index list data (8/16 bytes/primitive) |
| 0x0C | DWORD | `m_bHasBuffers` | Buffers allocated flag |
| 0x14 | DWORD | `m_locked` | Lock state |
| 0x24 | DWORD | `m_vertexCount` | Current vertex count |
| 0x28 | DWORD | `m_listCount` | Current primitive count |
| 0x2C | DWORD | `m_primitiveType` | 3=triangle, 4=quad |
| 0x30 | DWORD | `m_vertexCapacity` | Max vertices |
| 0x34 | DWORD | `m_listCapacity` | Max primitives |

---

## 3B. CMarniDirect3DTMD — TMD 3D Model Renderer

**File:** `src/marni/Marni3DObject.h`, `src/game/Marni3DObject.cpp`  
**Object Size:** 0x1594+ bytes (~5524 bytes)  
**Constructor:** 0x00415910  
**Debug String:** `"MarniSystem Direct3DTMD"` at 0x004b45e8

Manages up to 16 3D mesh objects with per-object transform matrices and texture/material lookups.

| Method | Address | Description |
|--------|---------|-------------|
| `Constructor` | 0x00415910 | Zeros object data arrays + handle array |
| `Create(ctx,mats)` | 0x00415650 | Creates per-object D3D handles, sets up matrices/textures from material context |
| `Transform(ctx,data,out,db)` | 0x00415520 | Transforms and renders all objects; supports double-buffering for flicker-free rendering |
| `Destroy(ctx)` | 0x00415880 | Releases all D3D object handles |
| `CleanupObjects(param)` | 0x004158e0 | Destroy + additional resource cleanup |

### Per-Object Data (stride 0x84 bytes)
| Offset | Type | Field |
|--------|------|-------|
| 0x00 | float[3] | Scale (x, y, z) |
| 0x0C | DWORD | D3D object handle |
| 0x10 | DWORD | Texture/material ID |

---

## 3C. CMarniViewport2 — 3D Viewport Class

**File:** `src/marni/Marni3DObject.h`, `src/game/Marni3DObject.cpp`  
**VTable:** 0x004af0f8 (Type 2)  
**Object Size:** 0x40 bytes (16 DWORDs)  
**Constructor:** 0x004272e0  
**Vertex Format:** 0x2C bytes/vertex (11 floats: position.xyz, normal.xyz, uv.xy, color.rgb)

Share the same memory layout as CDirect3DObject but with a different vtable and larger vertex stride.

| VTable | Address | Method | Description |
|--------|---------|--------|-------------|
| [0] | 0x00427270 | `Release()` | Free vertex+index buffers, zero all fields |
| [1] | 0x00427100 | `CreateWork(vtx,poly,type)` | Alloc 0x2C/vertex + type*2/poly buffers |
| [2] | 0x00426d60 | `GetVertex(idx,out)` | Read 11 floats (0x2C bytes) |
| [3] | 0x00426df0 | `SetVertex(idx,data)` | Write 11 floats |
| [4] | 0x00426e80 | `GetList(idx,out)` | Read 3 (tri) / 4 (quad) WORD indices |
| [5] | 0x00426f70 | `SetList(idx,data)` | Write indices with bounds checking |
| [6] | 0x004271e0 | `Lock(&vtx,&idx)` | Return buffer pointers, set lock=1 |
| [7] | 0x00427250 | `Unlock()` | Clear lock flag |

**Non-Virtual Methods:**
| Address | Method | Description |
|---------|--------|-------------|
| 0x00426600 | `CopyFrom(src)` | Deep copy with strip↔flat conversion, normal recalculation |
| 0x004262e0 | `Convert0(src)` | Convert strip indices to flat triangle lists |
| 0x00425c10 | `TriangleDivide(arr,n)` | Subdivide triangle polyhedra (free function) |

**Member Layout (additional fields beyond CDirect3DObject):**
| Offset | Type | Field | Description |
|--------|------|-------|-------------|
| 0x38 | DWORD | `m_renderStyle` | Strip (0) vs flat (1) mode |
| 0x3C | DWORD | `m_converted` | Converted flag |

---

## 3D. DirectInput — Marni Input System

**File:** `src/marni/MarniInput.h`, `src/game/MarniInput.cpp`  
**Debug String:** `"MarniSystem DirectInput Class"` at 0x004ba154

NOT the real DirectInput API — Capcom's custom wrapper using Win32 `GetAsyncKeyState` for keyboard and WinMM `joyGetPosEx` for joysticks. Maps PS1 controller semantics to PC input.

| Method | Address | Description |
|--------|---------|-------------|
| `UpdateKeyboardInputState(pState)` | 0x004202f0 | Polls 32 VK codes via `GetAsyncKeyState`, builds 32-bit button bitmask |
| `UpdateAllInputStates(pState)` | 0x00420570 | Keyboard state transitions (prev→curr→repeat) + polls up to 31 joysticks with axis/POV/button parsing |
| `SetDefaultKeyMapping(keyMap)` | 0x00420720 | Default PS1 layout: E/X/S/D + arrows + 0-9 |
| `InitJoysticks(pState)` | 0x00420770 | `joyGetNumDevs()` → validate each with `joyGetDevCapsA` + `joyGetPosEx` |

### MasterInputState Struct
**Size:** ~0x3B2C bytes

| Offset | Type | Field | Description |
|--------|------|-------|-------------|
| 0x00 | BYTE[32] | `keyMap` | Virtual key codes for 32 PS1 buttons |
| 0x24 | DWORD | `keyboardCurr` | Current frame pressed keys (bitmask) |
| 0x28 | DWORD | `keyboardPrev` | Previous frame pressed keys |
| 0x2C | DWORD | `keyboardNewPress` | Newly pressed this frame (~prev & curr) |
| 0x30 | DWORD | `keyboardRepeat` | Repeat latch |
| 0x200 | JoystickEntry[32] | `joysticks` | Up to 32 joystick entries (0x1D8 bytes each) |
| 0x3B28 | DWORD | `joystickCount` | Number of joysticks + 1 |

### Default Key Mapping
| PS1 Button | VK Code | Key |
|-----------|---------|-----|
| Action/Confirm | 0x45 | E |
| Cross | 0x58 | X |
| Square | 0x53 | S |
| Triangle | 0x44 | D |
| D-Pad Up | 0x26 | ↑ |
| D-Pad Down | 0x28 | ↓ |
| D-Pad Left | 0x25 | ← |
| D-Pad Right | 0x27 | → |
| Functions 0-9 | 0x30-0x39 | 0-9 |

### Call Chain
```
InputUpdate()  [0x00497c00]
  └─ UpdateAllInputStates(&g_pMasterInputState)
       ├─ UpdateKeyboardInputState()     [GetAsyncKeyState per VK]
       └─ joyGetPosEx() per joystick     [WinMM polling]
```

---

## 4. PSYQ GPU Emulation — Sprite Renderer

**Files:** `src/game/SpriteRenderer.h`, `src/game/SpriteRenderer.cpp`

### Architecture

The original PS1 used an "ordering table" (OT) where GPU packets were linked by Z-depth. The PC port emulated this with a sprite command buffer (`g_SpriteCommandBuffer[300]`) and an ordering table (`g_OT[32]`).

### Global Variables

| Variable | Type | Address | Description |
|----------|------|---------|-------------|
| `g_SpriteCommandBuffer` | `SpriteCommand[300]` | `0x008ec900` (approx) | Sprite draw command queue |
| `g_OT` | `OTEntry[32]` | `0x008ed000` (approx) | Ordering table entries |
| `g_RenderBufferIndex` | int | `0x004c3310` | Current render buffer index |
| `g_RenderDisableFlags` | int | `0x004c3314` | Render state flags |
| `g_SubpixelOffsetX` | int | `0x004c3358` | Sub-pixel scroll offset X |
| `g_SubpixelOffsetY` | int | `0x004c335c` | Sub-pixel scroll offset Y |
| `g_MaxFadeValue` | int | `0x004c3368` | Maximum fade value (4095) |
| `g_DepthSortOverride` | int | `0x004c2d14` | Depth sorting override |
| `g_ColorScaleFactor` | float | `0x004af2ac` | Color conversion factor (1.0f / 255.0f) |

### SpriteCommand Structure

| Offset | Type | Field | Description |
|--------|------|-------|-------------|
| 0x00 | int | `type` | Primitive type (10 = sprite) |
| 0x04 | float | `r` | Render flags / red component |
| 0x08 | float | `g` | Green / brightness |
| 0x0C | float | `b` | Blue |
| 0x10 | float | `blend` | Alpha blend |
| 0x14 | float | `x0` | Left screen X |
| 0x18 | float | `y0` | Top screen Y |
| 0x1C | float | `x1` | Right screen X |
| 0x20 | float | `y1` | Bottom screen Y |
| 0x24 | int | `depthSort` | Depth sorting key |
| 0x28 | short | `u0` | Left texture U coordinate |
| 0x2A | short | `v0` | Top texture V coordinate |
| 0x2C | short | `u1` | Right texture U coordinate |
| 0x2E | short | `v1` | Bottom texture V coordinate |
| 0x30 | int | `texturePage` | Texture page handle |
| 0x34 | int | `extraFlags` | Extra flags / D3D SRV slot index |

### Sprite Functions

| Address | Ghidra Name | C++ Function | Signature |
|---------|-------------|--------------|-----------|
| `0x0046d960` | `BuildSpriteRenderFlags` | `BuildSpriteRenderFlags` | `void(uint texFlags, uint* out)` |
| `0x0046d940` | `GetTextureVariant` | `GetTextureVariant` | `int(uint texFlags)` |
| `0x0046d990` | `SpriteQueue_Reset` | `SpriteQueue_Reset` | `void()` |
| `0x0046e5a0` | `draw_texture` | `draw_texture` | `int(uint* pak, ushort depth)` |
| `0x0046df60` | `AddSprite` | `AddSprite` | `int(uint* pak, short depth, int tpage, int fade)` |
| `0x0046e200` | `AddTintSprite` | `AddTintSprite` | `int(uint* pak, ushort fade)` |
| `0x0046dc00` | `SubmitEffectSprite` | `SubmitEffectSprite` | `int(void* buf, int depth, int texId, byte r, byte g, byte b, int scaleX, int scaleY, int blend, short bright)` |
| `0x0046edb0` | `SubmitEffectSprite_Ex` | (variant) | Extended effect sprite |
| `0x0046f280` | `AddSprite_Ex` | (variant) | Extended sprite with sub-pixel scrolling |
| `0x0046f8a0` | `AddTintSprite_Ex` | (variant) | Extended tint sprite |
| — | (new) | `FlushSpriteCommands` | Converts buffer → D3D11 draw calls |

### Polygon Functions

| Address | Function | Description |
|---------|----------|-------------|
| `0x0046fcf0` | `DrawPrim_SpriteLarge` | Large full-screen primitive |
| `0x0046d9c0` | `AddFadePoly` | Translucent polygon with Z-sort |

### Rendering Pipeline

```
PS1 Game Code (SPR/DR_MODE GPU packets)
    ↓
AddSprite / draw_texture / AddTintSprite / SubmitEffectSprite
    ↓
g_SpriteCommandBuffer[300] (queued per frame)
    ↓
FlushSpriteCommands()  [called in game_frame_present]
    ↓
MarniDrawSprite()  [D3D11 textured quad via Map/Unmap on quad VB]
    ↓
IDXGISwapChain::Present()
```

### FlushSpriteCommands — New D3D11 Implementation

```cpp
void FlushSpriteCommands(void) {
    // Applies screen-scale transform (320×240 → actual resolution)
    // For each valid sprite command:
    //   1. Scales position/size to D3D11 screen coordinates
    //   2. Looksup texture SRV from g_TexturePageSRV[] by command's texturePage
    //   3. Calls MarniDrawSprite(x, y, w, h, u0, v0, u1, v1, color, srv)
}
```

### BuildSpriteRenderFlags — `0x0046d960`

Extracts mirror/scale flags from PS1 GPU texture flags word:
- Bit 22 (`0x400000`) → Mirror vertically (flag `0x20`)
- Bit 23 (`0x800000`) → Mirror horizontally (flag `0x10`)

### GetTextureVariant — `0x0046d940`

Extracts texture variant (1-4) from flags bit 28-29 when bit 30 is set:
```cpp
if (textureFlags & 0x40000000)
    return ((textureFlags & 0x30000000) >> 28) + 1;
return 0;
```

---

## 5. Texture Page Management

**File:** `src/game/TextureLoader.cpp`

### Async Workers

| Address | Function | Description |
|---------|----------|-------------|
| `0x0046c130` | `AsyncCreateTexturePage` | Calls `CMarniDirect3D::vtable[6]` = `CreateTextureHandle` |
| `0x0046c1b0` | `destroy_texture_page` | Sets handle target, queues `AsyncDestroyTexturePage` |
| `0x0046c210` | `AsyncCreateObject` | Calls `CMarniDirect3D::vtable[7]` = `CreateObjectHandle` |
| `0x0046c260` | `AsyncDeleteObject` | Calls `CMarniDirect3D::vtable[9]` = `DeleteObjectHandle` |

### Core Texture Functions

| Address | Function | Description |
|---------|----------|-------------|
| `0x0046c160` | `create_texture_page` | Copies PSX data → work buffer, queues async texture creation |
| `0x0046c1b0` | `destroy_texture_page` | Queues async texture deletion by handle |
| `0x0046c5f0` | `ProcessTextureImage` | Auto-positioned font texture loader (bank 0x1E → D3D11 font SRV) |
| `0x0046c870` | `LoadTexturePage` | Load PSX texture page with slot-shifted D3D11 SRV creation |
| `0x0046ccd0` | `LoadShadowMaskTexture` | Load and process shadow mask texture (alpha-mask) |
| `0x0046c2a0` | `TexturePage_Load` | Load PSX image into a texture slot |
| `0x0046c360` | `TexturePage_ClearAll` | Clear all texture pages |
| `0x0046c3c0` | `TexturePage_Create` | Create texture page for a slot |
| `0x0046c410` | `TexturePage_SetupFull` | Full texture page setup |
| `0x0046d250` | `TexturePage_Refresh` | Refresh a texture page |
| `0x0046d2d0` | `TexturePage_RefreshCLUT` | Refresh CLUT-based page |
| `0x0046d690` | `TexturePage_LoadImage` | Load and process full image |
| `0x0046d080` | `delete_texture_set_secondary` | Delete texture set (shifted slot) |
| `0x0046fb50` | `CreateTexturedQuad` | Create 4-vertex textured quad for 3D viewport |
| `0x0046d0e0` | `SetupTexturePageHandles` | Rebuild all pages or update single page |

### D3D11 TexturePageSRV Array

During `LoadTexturePage`, `ProcessTextureImage`, and `LoadShadowMaskTexture`, PSX pixel data is converted to RGBA and uploaded to D3D11 via `MarniCreateTexture`:

```cpp
// From LoadTexturePage (0x0046c870)
if (slotIndex >= 0 && slotIndex < 256) {
    ID3D11Texture2D* tex = NULL;
    MarniCreateTexture(w, h, 32, rgba, &tex, &g_TexturePageSRV[slotIndex]);
    if (tex) tex->Release();  // Keep only the SRV
    g_TexturePageWidth[slotIndex] = w;
    g_TexturePageHeight[slotIndex] = h;
}
```

The `g_TexturePageSRV[256]` array is the D3D11 equivalent of the original PS1 texture page table. Each slot holds a shader resource view ready for sprite rendering.

### create_texture_page — `0x0046c160`

```cpp
int create_texture_page(void* psxTexData, int flags) {
    CMarniBits_CopyFrom(&g_MarniBitsWorkBuffer, psxTexData);
    g_texturePageMode = flags;
    ExecAsync((void*)AsyncCreateTexturePage);
    return g_texturePageHandle;
}
```

### destroy_texture_page — `0x0046c1b0`

```cpp
void destroy_texture_page(int id) {
    g_texturePageHandle = id;
    ExecAsync((void*)AsyncDestroyTexturePage);
}
```

---

## 6. Global Arrays (Static Initialization)

### Texture Array (`g_TextureArray`)

| Property | Value |
|----------|-------|
| Address | `0x008ed4d0` |
| Size | 51 elements × 892 bytes each (PSXTexture) |
| Init Chain | `TextureArray_Init` → `TextureArray_ConstructElements` → `_eh_vector_constructor_iterator_` |
| Cleanup | Registered via `_atexit(TextureArray_Cleanup)` |

### Viewport Array (`g_ViewportArray`)

| Property | Value |
|----------|-------|
| Address | `0x008ed030` |
| Size | 16 elements × 64 bytes each (CMarniViewport2) |
| Init Chain | `ViewportArray_Init` → `ViewportArray_ConstructElements` |
| Details | Constructor sets vtable + zeros 13 fields, sets `field[7] = 1` |

### Page Table Array (`DAT_008eca00`)

| Property | Value |
|----------|-------|
| Address | `0x008eca00` |
| Size | 9 elements × 176 bytes each (2 × CMarniBits per element) |
| Init Chain | `PageTableArray_Init` → `PageTableArray_ConstructElements` |

### Initialization Functions Grouped

| Group | Init Function | Constructor | Register Cleanup | Cleanup Function |
|-------|---------------|-------------|------------------|------------------|
| Texture Array | `TextureArray_Init` | `TextureArray_ConstructElements` | `TextureArray_RegisterCleanup` | `TextureArray_Cleanup` |
| Viewport Array | `ViewportArray_Init` | `ViewportArray_ConstructElements` | `ViewportArray_RegisterCleanup` | `ViewportArray_Cleanup` |
| Page Table | `PageTableArray_Init` | `PageTableArray_ConstructElements` | `PageTableArray_RegisterCleanup` | `PageTableArray_Cleanup` |
| Global Bits | `GlobalMarniBits_Init` | `GlobalMarniBits_Constructor` | `GlobalMarniBits_RegisterCleanup` | `GlobalMarniBits_Cleanup` |
| Global Viewport | `GlobalViewport_Init` | `GlobalViewport_Constructor` | `GlobalViewport_RegisterCleanup` | `GlobalViewport_Cleanup` |

### Single Object Globals

| Instance | Type | Init Function | Description |
|----------|------|---------------|-------------|
| Global work buffer | CMarniBits | `GlobalMarniBits_Init` | Reusable work buffer for texture ops |
| Global viewport | CMarniViewport2 | `GlobalViewport_Init` | Standalone viewport object |

Both register cleanup via `_atexit`.

---

## 7. ExecAsync — Async Task System

Texture and object creation/deletion use `ExecAsync` to queue work items that run on the main thread:

```
create_texture_page()   → ExecAsync(AsyncCreateTexturePage)
destroy_texture_page()  → ExecAsync(AsyncDestroyTexturePage)
FUN_0046c230()          → ExecAsync(AsyncCreateObject)
FUN_0046c280()          → ExecAsync(AsyncDeleteObject)
```

The async queue processes items on the main thread during the render loop, ensuring D3D11 API calls happen on the correct thread.

---

## 8. Data Flow Diagram

```
TIM/PIX File (PS1 Texture)
    ↓
PSXTexture::LoadFromFile() → PSXTexture::Store()
    ↓ Parse TIM header, extract pixel data + CLUT
CMarniBits::SetAddress() / CMarniBits::CopyFrom()
    ↓ Copy to work buffer
create_texture_page() → ExecAsync() → CMarniDirect3D::vtable[6] CreateTextureHandle()
    ↓
D3D11 Texture2D + SRV → g_TexturePageSRV[256]
    ↓
══════════════════════════════════════════
Game Logic (Tasks) per frame
    ↓
AddSprite / draw_texture / AddTintSprite / SubmitEffectSprite
    ↓ PS1 GPU packet → SpriteCommand
g_SpriteCommandBuffer[300]
    ↓ Queue all sprites for this frame
FlushSpriteCommands()
    ↓ Scale to screen coords, lookup SRV by slot
MarniDrawSprite() → D3D11 Map/Unmap quad VB, DrawIndexed
    ↓
IDXGISwapChain::Present()
    ↓
Screen
```

---

## 9. Source File Map

| File | Contents |
|------|----------|
| `src/marni/MarniSystem.h` | CMarniDirect3D class declaration, global Marni API |
| `src/marni/MarniSystem.cpp` | CMarniDirect3D vtable impl, D3D11 device, HLSL shaders |
| `src/marni/MarniBits.h` | CMarniBits class declaration (24 methods) |
| `src/marni/MarniBits.cpp` | CMarniBits vtable + surface operations |
| `src/marni/PSXTexture.h` | PSXTexture class declaration, CLUT management |
| `src/marni/PSXTexture.cpp` | PSXTexture TIM/PIX parser, Store, operator= |
| `src/marni/Marni3DObject.h` | CDirect3DObject, CMarniExecuteBuffer, CMarniPolyhedra, CMarniDirect3DTMD, CMarniViewport2 |
| `src/marni/MarniInput.h` | CMarniDirectInput class, MasterInputState struct |
| `src/game/Marni3DObject.cpp` | 3D object class implementations (all vtable + non-virtual methods) |
| `src/game/MarniInput.cpp` | DirectInput keyboard/joystick polling |
| `src/game/TextureLoader.cpp` | Texture page creation, async workers, page management |
| `src/game/SpriteRenderer.h` | SpriteCommand struct, OT entry, sprite functions |
| `src/game/SpriteRenderer.cpp` | PSYQ GPU sprite emulation (19 functions) |
| `src/Globals.h` | Global variable declarations for all Marni subsystems |

---

## 10. PSYQ-to-DirectX Mapping

| PSYQ Operation | Original D3D5 Implementation | Modern D3D11 Implementation |
|----------------|------------------------------|-----------------------------|
| `LoadImage(cr, rect, img)` | `CMarniBits::Blt()` → `IDirectDrawSurface::Blt()` | `CMarniBits::Blt()` → CPU pixel copy |
| `StoreImage(cr, rect, img)` | `CMarniBits::Lock()` → `IDirectDrawSurface::Lock()` | `CMarniBits::Lock()` → CPU buffer access |
| `DrawPrim(prim)` | PSYQ GPU emulation → OT → D3D5 draw | `AddSprite()` → `SprCmd` → `MarniDrawSprite()` → D3D11 |
| `AddPrim(spr, ot)` | `AddSprite()` / `AddTintSprite()` → sprite queue | Same pattern → `g_SpriteCommandBuffer[300]` |
| `DrawOTag(ot)` | Flush sprite queue → batch draw | `FlushSpriteCommands()` → batch `MarniDrawSprite` |
| `LoadImage(tim)` | `CMarniBits::Blt()` → surface load | `PSXTexture::Store()` → `create_texture_page()` → D3D11 SRV |
| `ResetGraph(mode)` | Reset OT, clear buffers | `SpriteQueue_Reset()`, `MarniClear()` |
| `MoveImage(cr, x, y)` | `IDirectDrawSurface::Blt()` with DDBLT_WAIT | `CMarniBits::BltFast()` / CPU pixel copy |
| `LoadTPage(p, p, t)` | `SetTexture` → `IDirect3DDevice3::SetTexture()` | `MarniDrawSprite()` with SRV from `g_TexturePageSRV[]` |

### Key Differences Between Original and Modern Implementation

1. **Surface Management**: Original used `IDirectDrawSurface` COM objects for all surface operations. Modern uses CPU-side pixel buffers (CMarniBits) for pixel manipulation and D3D11 textures only for GPU rendering.

2. **Sprite Rendering**: Original used `IDirect3DDevice3::DrawPrimitive` with execute buffers. Modern uses a single shared quad vertex buffer, mapped each frame with sprite vertices, drawn via `DrawIndexed`.

3. **Texture Atlas**: Original stored textures in a PS1-style 256-slot "texture page" table on VRAM. Modern stores them as D3D11 shader resource views in `g_TexturePageSRV[256]`.

4. **Palette Handling**: Original CLUT was stored as a D3D palette object. Modern converts indexed pixel data to RGBA on CPU upload, eliminating the need for palette textures.

5. **Async Creation**: Original used the Marni async system to defer texture creation to the main thread (D3D APIs must be called from the creation thread). This pattern is preserved with `ExecAsync`.
