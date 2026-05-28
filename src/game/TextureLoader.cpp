// TextureLoader.cpp - PSX TIM/PIX texture processing
#include "../Globals.h"
#include "../marni/PSXTexture.h"
#include "../marni/MarniSystem.h"
#include "../marni/MarniBits.h"
#include <stdio.h>

// 0x0046c130 - async texture page creation worker
// Calls CMarniDirect3D vtable[6] = CreateTextureHandle
void AsyncCreateTexturePage(void)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (pD3D != NULL && pD3D->vtable != NULL && pD3D->vtable[6] != NULL) {
        g_texturePageHandle = ((int(*)(void*, void*, int, void*))pD3D->vtable[6])(
            pD3D, &g_MarniBitsWorkBuffer, g_texturePageMode, &g_MarniBitsOutput);
    }
}

// destroy_texture_page_callback - async texture page deletion worker
// Calls CMarniDirect3D vtable[8] = DeleteTextureHandle
void AsyncDestroyTexturePage(void)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (pD3D != NULL && pD3D->vtable != NULL && pD3D->vtable[8] != NULL) {
        g_AsyncResult = ((int(*)(void*, int))pD3D->vtable[8])(pD3D, g_texturePageHandle);
    }
}

// 0x0046c210 - async object creation worker
// Calls CMarniDirect3D vtable[7] = CreateObjectHandle
void AsyncCreateObject(void)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (pD3D != NULL && pD3D->vtable != NULL && pD3D->vtable[7] != NULL) {
        g_ExecuteBufferHandle = ((unsigned int(*)(void*, void*, unsigned char))pD3D->vtable[7])(
            pD3D, &g_ObjectWorkBuffer, 0);
    }
}

// 0x0046c260 - async object deletion worker
// Calls CMarniDirect3D vtable[9] = DeleteObjectHandle
void AsyncDeleteObject(void)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (pD3D != NULL && pD3D->vtable != NULL && pD3D->vtable[9] != NULL) {
        g_AsyncResult = ((int(*)(void*, int))pD3D->vtable[9])(pD3D, g_ExecuteBufferHandle);
    }
}

static void VideoDriver_ClearArrayD0(void) { /* stub */ }

// 0x0046c1b0 - destroy_texture_page
// Sets handle, queues async deletion callback, returns result
void destroy_texture_page(int id)
{
    g_texturePageHandle = id;
    ExecAsync((void*)AsyncDestroyTexturePage);
}

// 0x0046c160 - create_texture_page
// Copies PSXTexture data into work buffer, stores flags, queues async creation
int create_texture_page(void* psxTexData, int flags)
{
    CMarniBits_CopyFrom(&g_MarniBitsWorkBuffer, psxTexData);
    g_texturePageMode = flags;
    ExecAsync((void*)AsyncCreateTexturePage);
    return g_texturePageHandle;
}

// ============================================================================
// ProcessTextureImage (0x0046c5f0)
// Auto-positioned font texture loader. Computes X/Y from bank ID.
// ============================================================================
void ProcessTextureImage(void* imageBuffer, short textureBankID, short pageOffset, int slotIndex)
{
    int slotOffset = slotIndex * 0x37C;

    int textureCount = *(int*)((BYTE*)&g_VideoDriverArray_814 + slotOffset);
    if (textureCount != 0) {
        DWORD* pageTable = (DWORD*)((BYTE*)&g_TexturePageTable_DAT + slotOffset);
        for (int i = 0; i < textureCount; i++) {
            if (pageTable[i] != 0) { destroy_texture_page(pageTable[i]); pageTable[i] = 0; }
        }
        VideoDriver_ClearArrayD0();
    }

    if (imageBuffer == NULL) {
        OutputDebugStringA("[TEX] ProcessTextureImage: imageBuffer is NULL!\n");
        return;
    }

    PSXTexture psxTex;
    if (psxTex.Store((int*)imageBuffer, 1) == 0) return;

    short baseX = (textureBankID >= 0x10) ? 0x400 : 0;
    short baseY = (textureBankID >= 0x10) ? 0x100 : 0;

    WORD* texDesc = (WORD*)((BYTE*)&g_VideoDriverArray_838 + slotOffset);
    texDesc[0] = textureBankID * 0x40 - baseX;
    texDesc[1] = baseY;
    *(short*)(texDesc + 2) = (short)psxTex.m_WidthPixels;
    *(short*)(texDesc + 3) = (short)psxTex.m_Height;
    texDesc[4] = 0;
    texDesc[5] = pageOffset + 0x1E0;
    texDesc[6] = 0;
    texDesc[7] = 1;
    texDesc[8] = textureBankID;

    if (psxTex.m_BitDepth == 4)
        texDesc[2] = (short)(((int)texDesc[2] + ((int)texDesc[2] >> 31 & 3)) >> 2);
    else if (psxTex.m_BitDepth == 8)
        texDesc[2] = texDesc[2] / 2;

    DWORD* pageTable = (DWORD*)((BYTE*)&g_TexturePageTable_DAT + slotOffset);
    pageTable[0] = create_texture_page(&psxTex, 2);

    *(DWORD*)((BYTE*)&g_VideoDriverArray_810 + slotOffset) = 1;
    *(DWORD*)((BYTE*)&g_VideoDriverArray_814 + slotOffset) = 1;

    // --- Create D3D11 font texture for text rendering ---
    if (textureBankID == 0x1E && psxTex.m_pPixelData != NULL) {
        CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
        OutputDebugStringA("[TEX] Creating font D3D11 texture...\n");
        if (pD3D != NULL) {
            OutputDebugStringA("[TEX] pD3D valid\n");

            int w = psxTex.m_WidthPixels;
            int h = psxTex.m_Height;
            int bpp = psxTex.m_BitDepth;
            void* srcData = psxTex.m_pPixelData;

            // For paletted formats (4bpp/8bpp), pre-convert to RGBA using CLUT
            DWORD* clutRGBA = NULL;
            DWORD* rgbaOut = NULL;
            int useDirect = 0;

            if (bpp == 4 || bpp == 8) {
                // Build CLUT RGBA palette using actual CLUT entry count
                int numClutEntries;
                if (bpp == 4) numClutEntries = (psxTex.m_CLUT_W > 0) ? (int)(psxTex.m_CLUT_W * psxTex.m_CLUT_H) : 16;
                else           numClutEntries = (psxTex.m_CLUT_W > 0) ? (int)(psxTex.m_CLUT_W * psxTex.m_CLUT_H) : 256;

                if (numClutEntries > 256) numClutEntries = 256;
                if (numClutEntries < 16)  numClutEntries = 16;

                clutRGBA = new DWORD[numClutEntries];
                WORD* clut = (WORD*)psxTex.m_CLUT_Data;

                for (int i = 0; i < numClutEntries; i++) {
                    WORD c = clut[i];
                    if (i == 0) {
                        // Index 0 = transparent
                        clutRGBA[i] = 0x00000000;
                    } else {
                        // Convert RGB555 to RGBA (bit 15 = alpha flag in some TIM variants)
                        DWORD a = (c & 0x8000) ? 0xFF : 0xFF; // Always opaque for non-zero indices
                        DWORD r = ((c >> 10) & 0x1F) * 255 / 31;
                        DWORD g = ((c >> 5) & 0x1F) * 255 / 31;
                        DWORD b = (c & 0x1F) * 255 / 31;
                        clutRGBA[i] = (a << 24) | (r << 16) | (g << 8) | b;
                    }
                }

                // Convert pixel indices to RGBA using CLUT
                int totalPixels = w * h;
                rgbaOut = new DWORD[totalPixels];

                if (bpp == 4) {
                    WORD* src = (WORD*)srcData;
                    int totalWords = totalPixels / 4;
                    for (int i = 0; i < totalWords; i++) {
                        WORD word = src[i];
                        int p0 = (word >> 0)  & 0xF;
                        int p1 = (word >> 4)  & 0xF;
                        int p2 = (word >> 8)  & 0xF;
                        int p3 = (word >> 12) & 0xF;
                        int base = i * 4;
                        rgbaOut[base]     = clutRGBA[p0];
                        rgbaOut[base + 1] = clutRGBA[p1];
                        rgbaOut[base + 2] = clutRGBA[p2];
                        rgbaOut[base + 3] = clutRGBA[p3];
                    }
                } else { // bpp == 8
                    BYTE* src = (BYTE*)srcData;
                    for (int i = 0; i < totalPixels; i++) {
                        rgbaOut[i] = clutRGBA[src[i]];
                    }
                }

                // Use pre-converted RGBA data
                useDirect = 1;
            }

            if (useDirect) {
                MarniCreateTexture(w, h, 32, rgbaOut, &pD3D->m_pFontTexture, &pD3D->m_pFontSRV);
                delete[] rgbaOut;
                delete[] clutRGBA;
            } else {
                MarniCreateTexture(w, h, bpp, srcData, &pD3D->m_pFontTexture, &pD3D->m_pFontSRV);
            }

            if (pD3D->m_pFontSRV) {
                OutputDebugStringA("[TEX] Font SRV created OK\n");
                pD3D->m_FontTexWidth = w;
                pD3D->m_FontTexHeight = h;
            } else OutputDebugStringA("[TEX] Font SRV creation FAILED\n");
        } else {
            OutputDebugStringA("[TEX] pD3D NULL - cannot create font texture\n");
        }
    }
}

// ============================================================================
// LoadTexturePage (0x0046c870)
// General-purpose texture loader with explicit positioning and flag control.
// Slot shifted by +0xF; uses alternate 0xDF scaling for descriptor table lookups.
// ============================================================================
void LoadTexturePage(void* imageBuffer, short texId, short pageOffset, int slotIndex,
                     int unused, short posX, short posY, unsigned int flags)
{
    slotIndex = slotIndex + 0xF;
    int slotOffset = slotIndex * 0x37C;
    int texCheckOffset = slotIndex * 0xDF;

    int textureCount = *(int*)((BYTE*)&g_VideoDriverArray_814 + texCheckOffset);
    if (textureCount != 0) {
        DWORD* pageTable = (DWORD*)((BYTE*)&g_TexturePageTable_DAT + texCheckOffset);
        for (int i = 0; i < textureCount; i++) {
            if (pageTable[i] != 0) { destroy_texture_page(pageTable[i]); pageTable[i] = 0; }
        }
        VideoDriver_ClearArrayD0();
    }

    if (imageBuffer == NULL) {
        OutputDebugStringA("[TEX] LoadTexturePage: imageBuffer is NULL!\n");
        return;
    }

    PSXTexture psxTex;
    if (psxTex.Store((int*)imageBuffer, 1) == 0) return;

    WORD* texDesc = (WORD*)((BYTE*)&g_VideoDriverArray_838 + slotOffset);
    texDesc[0] = posX;
    texDesc[1] = posY;
    *(short*)(texDesc + 2) = (short)psxTex.m_WidthPixels;
    *(short*)(texDesc + 3) = (short)psxTex.m_Height;
    texDesc[4] = 0;
    texDesc[5] = pageOffset + 0x1E0;
    texDesc[6] = 0;
    texDesc[7] = (short)textureCount;
    texDesc[8] = texId;

    BYTE bpp = *(BYTE*)((BYTE*)&g_VideoDriverArray_FA + texCheckOffset);
    if (bpp == 4)
        texDesc[2] = (short)(((int)texDesc[2] + ((int)texDesc[2] >> 31 & 3)) >> 2);
    else if (bpp == 8)
        texDesc[2] = texDesc[2] / 2;

    int pageIndex = 0;
    int pageCount = *(int*)((BYTE*)&g_VideoDriverArray_810 + texCheckOffset);
    if (pageCount > 0) {
        BYTE* pageData = (BYTE*)&g_VideoDriverArray_4d0 + slotOffset;
        DWORD* pageTable = (DWORD*)((BYTE*)&g_TexturePageTable_DAT + texCheckOffset);
        for (int i = 0; i < pageCount; i++) {
            if ((flags & 1) == 0) {
                *(DWORD*)(pageData + 0x50) = 1;
                int mode = ((flags & 2) == 0) ? 2 : 0x12;
                *pageTable = (DWORD)create_texture_page(pageData, mode);
            } else {
                *pageTable = 0;
            }
            pageData += 0x68;
            pageTable++;
        }
        pageIndex = pageCount;
    }

    if (pageIndex < 8) {
        DWORD* pageTable = (DWORD*)((BYTE*)&g_TexturePageTable_DAT + texCheckOffset + pageIndex * sizeof(DWORD));
        for (int i = 8 - pageIndex; i > 0; i--) { *pageTable = 0; pageTable++; }
    }

    // 0x0046c870: Create D3D11 SRV from the loaded PSXTexture
    // Store SRV indexed by shifted slot index (slotIndex = original + 0xF)
    {
        char dbg[256];
        sprintf(dbg, "[TEX] LoadTexturePage slot=%d w=%d h=%d bpp=%d\n", slotIndex, (int)psxTex.m_WidthPixels, (int)psxTex.m_Height, (int)psxTex.m_BitDepth);
        OutputDebugStringA(dbg);
    }
    if (psxTex.m_pPixelData != NULL && psxTex.m_WidthPixels > 0 && psxTex.m_Height > 0) {
        int w = psxTex.m_WidthPixels;
        int h = psxTex.m_Height;
        int bpp = psxTex.m_BitDepth;

        if (bpp == 4 || bpp == 8) {
            int numClutEntries = (bpp == 4) ? 16 : 256;
            WORD* clut = (WORD*)psxTex.m_CLUT_Data;
            DWORD* clutRGBA = new DWORD[numClutEntries];
            for (int c = 0; c < numClutEntries; c++) {
                WORD clr = clut[c];
                DWORD a = (c == 0) ? 0x00 : 0xFF;
                DWORD r = ((clr >> 0)  & 0x1F) * 255 / 31;
                DWORD g = ((clr >> 5)  & 0x1F) * 255 / 31;
                DWORD b = ((clr >> 10) & 0x1F) * 255 / 31;
                clutRGBA[c] = (a << 24) | (r << 16) | (g << 8) | b;
            }

            DWORD* rgba = new DWORD[w * h];
            int opaqueCount = 0;
            if (bpp == 4) {
                BYTE* src = (BYTE*)psxTex.m_pPixelData;
                for (int y = 0; y < h; y++) {
                    for (int x = 0; x < w; x++) {
                        int byteIdx = y * (w / 2) + x / 2;
                        BYTE nibble = (x & 1) ? (src[byteIdx] >> 4) : (src[byteIdx] & 0xF);
                        rgba[y * w + x] = clutRGBA[nibble];
                        if (nibble != 0) opaqueCount++;
                    }
                }
            } else {
                BYTE* src = (BYTE*)psxTex.m_pPixelData;
                for (int i = 0; i < w * h; i++) {
                    rgba[i] = clutRGBA[src[i]];
                    if (src[i] != 0) opaqueCount++;
                }
            }

            {
                char dbg[256];
                sprintf(dbg, "[TEX] rgba pixels: total=%d opaque=%d sample[0]=%08X [100]=%08X clut[1]=%08X\n",
                    w*h, opaqueCount, rgba[0], rgba[100], clutRGBA[1]);
                OutputDebugStringA(dbg);
            }

            if (slotIndex >= 0 && slotIndex < 256) {
                ID3D11Texture2D* tex = NULL;
                MarniCreateTexture(w, h, 32, rgba, &tex, &g_TexturePageSRV[slotIndex]);
                if (tex) tex->Release();
                g_TexturePageWidth[slotIndex] = w;
                g_TexturePageHeight[slotIndex] = h;
                {
                    char dbg[256];
                    sprintf(dbg, "[TEX] SRV[%d] created=%s w=%d h=%d bpp=%d\n", slotIndex, g_TexturePageSRV[slotIndex] ? "YES" : "NO", w, h, bpp);
                    OutputDebugStringA(dbg);
                }
            }
            delete[] rgba;
            delete[] clutRGBA;
        } else if (bpp == 16) {
            DWORD* rgba = new DWORD[w * h];
            WORD* src = (WORD*)psxTex.m_pPixelData;
            for (int i = 0; i < w * h; i++) {
                WORD px = src[i];
                DWORD a = (px & 0x8000) ? 0x80 : 0xFF;
                DWORD r = ((px >> 0)  & 0x1F) * 255 / 31;
                DWORD g = ((px >> 5)  & 0x1F) * 255 / 31;
                DWORD b = ((px >> 10) & 0x1F) * 255 / 31;
                rgba[i] = (a << 24) | (b << 16) | (g << 8) | r;
            }
            if (slotIndex >= 0 && slotIndex < 256) {
                ID3D11Texture2D* tex = NULL;
                MarniCreateTexture(w, h, 32, rgba, &tex, &g_TexturePageSRV[slotIndex]);
                if (tex) tex->Release();
                g_TexturePageWidth[slotIndex] = w;
                g_TexturePageHeight[slotIndex] = h;
                {
                    char dbg[256];
                    sprintf(dbg, "[TEX] SRV[%d] created=%s w=%d h=%d bpp=16\n", slotIndex, g_TexturePageSRV[slotIndex] ? "YES" : "NO", w, h);
                    OutputDebugStringA(dbg);
                }
            }
            delete[] rgba;
        }
    } else {
        char dbg[256];
        sprintf(dbg, "[TEX] LoadTexturePage SKIP SRV: pData=%p w=%d h=%d\n", psxTex.m_pPixelData, (int)psxTex.m_WidthPixels, (int)psxTex.m_Height);
        OutputDebugStringA(dbg);
    }
}

// ============================================================================
// LoadShadowMaskTexture (0x0046ccd0)
// Shadow/mask texture loader. Reads 16-bit palette from image offset +0x14,
// replaces non-transparent colors with black for alpha-mask effect,
// then creates shadow-optimized texture pages (mode=1).
// Slot shifted by +0x2F.
// ============================================================================
void LoadShadowMaskTexture(void* imageBuffer, int slotBase)
{
    if (imageBuffer == NULL) {
        OutputDebugStringA("[TEX] LoadShadowMaskTexture: imageBuffer is NULL!\n");
        return;
    }

    int slotIndex = slotBase + 0x2F;
    int slotOffset = slotIndex * 0x37C;
    int texCheckOffset = slotIndex * 0xDF;

    int textureCount = *(int*)((BYTE*)&g_VideoDriverArray_814 + texCheckOffset);
    if (textureCount != 0) {
        DWORD* pageTable = (DWORD*)((BYTE*)&g_TexturePageTable_DAT + texCheckOffset);
        for (int i = 0; i < textureCount; i++) {
            if (pageTable[i] != 0) { destroy_texture_page(pageTable[i]); pageTable[i] = 0; }
        }
        VideoDriver_ClearArrayD0();
    }

    // Read color palette from image offset +0x14
    WORD paletteEntries[256] = {0};
    WORD* srcPalette = (WORD*)((BYTE*)imageBuffer + 0x14);
    for (int i = 0; i < 256; i++) {
        WORD color = srcPalette[i];
        paletteEntries[i] = color;
        // Non-transparent, non-zero colors → black (0x3DEF = RGB555 black)
        if (color != 0 && color != 0x8000) {
            srcPalette[i] = 0x3DEF;
        }
    }

    PSXTexture psxTex;
    if (psxTex.Store((int*)imageBuffer, 1) == 0) return;

    // Create shadow-optimized texture pages with mode=1
    int pageIndex = 0;
    int pageCount = 1;
    BYTE* pageData = (BYTE*)&g_VideoDriverArray_4d0 + slotOffset;
    DWORD* pageTable = (DWORD*)((BYTE*)&g_TexturePageTable_DAT + texCheckOffset);

    for (int page = 0; page < pageCount; page++) {
        *(DWORD*)(pageData + 0x50) = 1;
        *(DWORD*)(pageData + 0x54) = 0;
        *(DWORD*)(pageData + 0x58) = 0;
        *(DWORD*)(pageData + 0x5C) = 0;
        *(DWORD*)(pageData + 0x60) = 0;
        *pageTable = (DWORD)create_texture_page(pageData, 1);  // mode=1 = shadow/alpha blend
        pageData += 0x68;
        pageTable++;
        pageIndex++;
    }

    if (pageIndex < 8) {
        DWORD* pageTable = (DWORD*)((BYTE*)&g_TexturePageTable_DAT + texCheckOffset + pageIndex * sizeof(DWORD));
        for (int i = 8 - pageIndex; i > 0; i--) { *pageTable = 0; pageTable++; }
    }

    *(DWORD*)((BYTE*)&g_VideoDriverArray_810 + slotOffset) = pageIndex;
    *(DWORD*)((BYTE*)&g_VideoDriverArray_814 + slotOffset) = 1;
}

// ============================================================================
// CreateTexturedQuad (0x0046fb50, renamed from FUN_0046fb50)
//
// Creates a renderable textured quad primitive in the Marni 3D viewport system.
// Sets up 4 vertices with position/UV coordinates from the provided vertex data,
// forms two triangles, and creates an execute buffer for GPU rendering.
//
// Parameters:
//   viewportSlot   - slot index (scaled internally by 0x40)
//   texturePageId  - texture page handle stored at DAT_008ed06c[slot]
//   vertexData     - array of 24 ints: 4 vertices × 6 values each
//                    Each vertex: {x, y, z, u, v, color_flag}
//                    Plus 4 texture dims: {w, h, w2, h2}
//                    Plus 2 flags
//
// Sub-functions (Marni viewport API, stubbed until full implementation):
//   FUN_0046c280(handle)              - release old execute buffer
//   FUN_00427270()                    - viewport cleanup
//   FUN_00427100(4, 1, 4)            - init viewport (vertices, mode, type)
//   FUN_004271e0(0, 0)               - get background color
//   FUN_00426df0(id, &vertex)        - register vertex in viewport
//   FUN_00426f70(0, &indices)        - apply lighting/material
//   FUN_00427250()                    - finalize background
//   FUN_0046c230(&DAT_008ed030[slot]) - create execute buffer → returns handle
// ============================================================================
void CreateTexturedQuad(int viewportSlot, int texturePageId, int* vertexData)
{
    // 0x0046fb50: Scale slot to byte offset
    int slotOffset = viewportSlot * 0x40;

    // 0x0046fb60: If slot is in use, release old resources
    if (g_VideoDriverArray_03c[slotOffset / sizeof(DWORD)] != 0) {
        int oldHandle = g_VideoDriverArray_068[slotOffset / sizeof(DWORD)];
        if (oldHandle != 0) {
            FUN_0046c280(oldHandle);
        }
        // FUN_00427270();  // Viewport cleanup - stubbed
    }

    // 0x0046fb90: Initialize viewport: 4 vertices, mode 1, type 4
    // FUN_00427100(4, 1, 4);  // - stubbed

    // 0x0046fba0: Clear the surface flag
    g_VideoDriverArray_04c[slotOffset / sizeof(DWORD)] = 0;

    // 0x0046fbb0: Store the texture page ID for this slot
    g_VideoDriverArray_06c[slotOffset / sizeof(DWORD)] = texturePageId;

    // 0x0046fbc0: Check if the texture page exists in the page table
    int texCheckOffset = texturePageId * 0xDF;
    DWORD* pageTable = (DWORD*)((BYTE*)&g_TexturePageTable_DAT + texCheckOffset);

    if (*pageTable != 0) {
        // 0x0046fbd0: Get and check background color
        // int bgColor = FUN_004271e0(0, 0);  // - stubbed
        int bgColor = 1;  // Assume non-zero for now

        if (bgColor != 0) {
            // 0x0046fbf0: Set up 4 vertices
            for (int i = 0; i < 4; i++) {
                // Each vertex entry in vertexData contains position, UV, and a placeholder
                // vertexData layout:
                //   [0]  = v0.x,  [1]  = v1.x,  [2]  = v2.x,  [3]  = v3.x
                //   [4]  = v0.y,  ...  (interleaved by stride 4 in the reverse direction)
                // Actually, from the decompilation: *piVar4 directly accesses each group
                // piVar4[0]  = x for vertex i
                // piVar4[4]  = y for vertex i
                // piVar4[8]  = z for vertex i
                // piVar4[12] = u for vertex i
                // piVar4[16] = v for vertex i
                // param_3[20] = r, param_3[21] = g, param_3[22] = b

                float x = (float)vertexData[i];           // Position X
                float y = (float)vertexData[i + 4];        // Position Y  
                float z = (float)vertexData[i + 8];        // Position Z
                float r = 1.0f;                             // Color R (0x3f800000 = 1.0f)
                float g = 1.0f;                             // Color G
                float b = 1.0f;                             // Color B

                // Texture coordinates normalized by page dimensions
                float texW = (float)g_VideoDriverArray_4fc[texturePageId];
                float texH = (float)g_VideoDriverArray_500[texturePageId];
                float u = (texW != 0) ? (float)vertexData[i + 12] / texW : 0.0f;
                float v = (texH != 0) ? (float)vertexData[i + 16] / texH : 0.0f;

                // Vertex descriptor structure (matches local_2c layout)
                float vertexDesc[10] = { x, y, z, r, g, b, 0.0f, 0.0f, u, v };
                // FUN_00426df0(i, vertexDesc);  // Register vertex - stubbed
            }

            // 0x0046fc80: Triangle indices for quad: 0,1,3,2
            WORD indices[4] = { 0, 1, 3, 2 };
            // FUN_00426f70(0, indices);  // Light vertices - stubbed

            // 0x0046fca0: Finalize background
            // FUN_00427250();  // - stubbed

            // 0x0046fcb0: Create execute buffer for this quad
            int handle = FUN_0046c230(&g_VideoDriverArray_D0 + slotOffset);
            g_VideoDriverArray_068[slotOffset / sizeof(DWORD)] = handle;
        }
    }
}

// ============================================================================
// SetupTexturePageHandles (0x0046d0e0, renamed from FUN_0046d0e0)
//
// Manages GPU texture handle creation for previously loaded texture pages.
// Called after LoadTexturePage to actually register the handles in the page table.
//
// Two modes:
//   pageIndex == 0:  Rebuild ALL texture page handles for the slot.
//                    Iterates through each page descriptor, creates GPU handles
//                    via create_texture_page with mode 2, and registers them.
//                    Fills remaining 8 slots with zeros.
//
//   pageIndex != 0:  Clear all 8 slots, then create a SINGLE page handle
//                    at slot[pageIndex - 1]. Used for multi-CLUT textures
//                    where only one specific page needs updating.
//
// Both modes apply the same 0xF slot shift as LoadTexturePage.
// ============================================================================
void SetupTexturePageHandles(int slotIndex, int pageIndex)
{
    // 0x0046d0e0: Shift slot by 0xF (matching LoadTexturePage)
    slotIndex = slotIndex + 0xF;

    if (pageIndex == 0) {
        // --- Rebuild all pages ---
        int texCheckOffset = slotIndex * 0xDF;
        int pageCount = g_VideoDriverArray_810[texCheckOffset];
        int count = 0;

        if (pageCount > 0) {
            BYTE* pageData = (BYTE*)&g_VideoDriverArray_4d0 + slotIndex * 0x37C;
            DWORD* pageTable = (DWORD*)((BYTE*)&g_TexturePageTable_DAT + texCheckOffset);

            for (int i = 0; i < pageCount; i++) {
                // 0x0046d120: Set page flag and create GPU handle with mode 2
                *(DWORD*)(pageData + 0x50) = 1;
                int handle = create_texture_page(pageData, 2);
                *pageTable = (DWORD)handle;

                // 0x0046d140: Commit/flush if debug mode enabled
                // if (DAT_008ed470 != 0) { (**(code**)(*pageData + 0x18))(); }

                count++;
                pageData += 0x68;     // Next page descriptor (0x1A dwords = 0x68 bytes)
                pageTable++;
            }
        }

        // 0x0046d170: Fill remaining 8 slots with zero
        if (count < 8) {
            DWORD* pageTable = (DWORD*)((BYTE*)&g_TexturePageTable_DAT + texCheckOffset + count * sizeof(DWORD));
            for (int i = 8 - count; i > 0; i--) {
                *pageTable = 0;
                pageTable++;
            }
        }
    } else {
        // --- Setup single page at index (pageIndex - 1) ---
        int texCheckOffset = slotIndex * 0xDF;

        // 0x0046d1a0: Clear all 8 page table slots to zero
        DWORD* pageTable = (DWORD*)((BYTE*)&g_TexturePageTable_DAT + texCheckOffset);
        for (int i = 0; i < 8; i++) {
            *pageTable = 0;
            pageTable++;
        }

        // 0x0046d1c0: Compute offset for the specific page
        int pageOffset = slotIndex * 0x37C + (pageIndex - 1) * 0x68;

        // 0x0046d1d0: Set flag and create GPU handle
        g_VideoDriverArray_520[pageOffset / sizeof(DWORD)] = 1;
        int handle = create_texture_page((BYTE*)&g_VideoDriverArray_4d0 + pageOffset, 2);

        // Store handle in the correct page table slot
        int pageSlot = slotIndex * 0xDF + pageIndex - 1;
        ((DWORD*)&g_TexturePageTable_DAT)[pageSlot] = (DWORD)handle;
    }
}
