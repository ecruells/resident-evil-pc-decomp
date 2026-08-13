// TextureLoader.cpp - PSX TIM/PIX texture processing
#include "../Globals.h"
#include "../marni/PSXTexture.h"
#include "../marni/MarniSystem.h"
#include "../marni/MarniBits.h"
#include <stdio.h>

#undef LoadImage  // Win32 WinUser.h macro conflicts with Marni LoadImage

// ============================================================================
// CLUT cache — stores parsed pixel data + all CLUT palettes per texture slot
// so we can rebuild the SRV with a different CLUT palette index.
// ============================================================================
struct TextureCLUTCache {
    BYTE*  pixelData;     // copy of the indexed pixel data (4bpp nibble-packed or 8bpp)
    int    pixelDataSize; // size in bytes
    WORD*  clutData;      // copy of all CLUT palettes (clutW * clutH entries)
    int    clutDataSize;  // size in bytes
    int    numCLUTs;      // number of CLUT palettes (from psxTex.m_NumCLUTs)
    int    clutEntries;   // entries per CLUT (16 for 4bpp, 256 for 8bpp)
    int    bpp;           // bit depth (4 or 8)
};
static TextureCLUTCache g_CLUTCache[256];

// ============================================================================
// LoadEffectTextureSheet — parse one effect-sprite sheet TIM (256-wide PSX
// 4bpp strip from core00.etm or effspr\*.tim) and install its D3D11 SRV at an
// EXPLICIT texture-page slot.
//
// The effect sheets use slots 3-10 (weapon FX) and 11-14 (room esp), which no
// other subsystem touches: the global textures own 0-2, the menu/item images
// own 15-30 and 43-46. LoadTexturePage cannot be used - it adds 0xF to the
// slot and would land on the menu's textures - so the SRV is built directly,
// mirroring the conversion in LoadTexturePage's tail.
// ============================================================================
void LoadEffectTextureSheet(int slot, void* timData)
{
    if (slot < 0 || slot >= 256) return;
    if (timData == NULL) return;

    PSXTexture psxTex;
    if (psxTex.Store((int*)timData, 1) == 0) return;

    int w = psxTex.m_WidthPixels;
    int h = psxTex.m_Height;
    int bpp = psxTex.m_BitDepth;
    if (w <= 0 || h <= 0 || psxTex.m_pPixelData == NULL) return;

    DWORD* rgba = new DWORD[w * h];
    if (bpp == 4 || bpp == 8) {
        int numClutEntries = (bpp == 4) ? 16 : 256;
        WORD* clut = psxTex.m_pCLUTData;
        DWORD* clutRGBA = new DWORD[numClutEntries];
        for (int c = 0; c < numClutEntries; c++) {
            WORD clr = clut[c];
            DWORD r = ((clr >> 0)  & 0x1F) * 255 / 31;
            DWORD g = ((clr >> 5)  & 0x1F) * 255 / 31;
            DWORD a = (c == 0) ? 0x00 : ((clr & 0x8000) ? 0x80 : 0xFF);
            DWORD b = ((clr >> 10) & 0x1F) * 255 / 31;
            clutRGBA[c] = (a << 24) | (b << 16) | (g << 8) | r;
        }
        if (bpp == 4) {
            BYTE* src = (BYTE*)psxTex.m_pPixelData;
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    int byteIdx = y * (w / 2) + x / 2;
                    BYTE nibble = (x & 1) ? (src[byteIdx] >> 4) : (src[byteIdx] & 0xF);
                    rgba[y * w + x] = clutRGBA[nibble];
                }
            }
        } else {
            BYTE* src = (BYTE*)psxTex.m_pPixelData;
            for (int i = 0; i < w * h; i++) {
                rgba[i] = clutRGBA[src[i]];
            }
        }
        delete[] clutRGBA;
    } else {
        for (int i = 0; i < w * h; i++) rgba[i] = 0xFF000000;
    }

    if (g_TexturePageSRV[slot] != MARNI_NULL_HANDLE) {
        Marni_DX()->DestroyTexture(g_TexturePageSRV[slot]);
        g_TexturePageSRV[slot] = MARNI_NULL_HANDLE;
    }
    MarniCreateTexture(w, h, 32, rgba, &g_TexturePageSRV[slot]);
    delete[] rgba;

    g_TexturePageWidth[slot] = w;
    g_TexturePageHeight[slot] = h;
    g_TexturePageBpp[slot] = bpp;
}

// ============================================================================
// RebuildTextureSRV — Rebuild the D3D11 SRV for a slot using a different CLUT
// palette index. Uses cached pixel + CLUT data (no re-parsing).
// Returns 1 on success, 0 on failure.
// ============================================================================
int RebuildTextureSRV(int slotIndex, int clutIndex)
{
    if (slotIndex < 0 || slotIndex >= 256) return 0;
    TextureCLUTCache* cache = &g_CLUTCache[slotIndex];
    if (cache->pixelData == NULL || cache->numCLUTs <= 1) return 0;
    if (clutIndex < 0 || clutIndex >= cache->numCLUTs) return 0;

    int w = g_TexturePageWidth[slotIndex];
    int h = g_TexturePageHeight[slotIndex];
    if (w <= 0 || h <= 0) return 0;

    int bpp = cache->bpp;
    int entriesPerCLUT = cache->clutEntries;

    // Build RGBA palette from the selected CLUT
    WORD* selectedCLUT = cache->clutData + clutIndex * entriesPerCLUT;
    DWORD* clutRGBA = new DWORD[entriesPerCLUT];
    for (int c = 0; c < entriesPerCLUT; c++) {
        WORD clr = selectedCLUT[c];
        DWORD a = (c == 0) ? 0x00 : 0xFF;
        DWORD r = ((clr >> 0)  & 0x1F) * 255 / 31;
        DWORD g = ((clr >> 5)  & 0x1F) * 255 / 31;
        DWORD b = ((clr >> 10) & 0x1F) * 255 / 31;
        clutRGBA[c] = (a << 24) | (b << 16) | (g << 8) | r;
    }

    DWORD* rgba = new DWORD[w * h];
    if (bpp == 4) {
        BYTE* src = cache->pixelData;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int byteIdx = y * (w / 2) + x / 2;
                BYTE nibble = (x & 1) ? (src[byteIdx] >> 4) : (src[byteIdx] & 0xF);
                rgba[y * w + x] = clutRGBA[nibble];
            }
        }
    } else {
        BYTE* src = cache->pixelData;
        for (int i = 0; i < w * h; i++) {
            rgba[i] = clutRGBA[src[i]];
        }
    }

    // Create new SRV first, then swap (avoids NULL SRV during render pass)
    MarniHandle newTex = MARNI_NULL_HANDLE;
    MarniCreateTexture(w, h, 32, rgba, &newTex);

    if (newTex != MARNI_NULL_HANDLE) {
        if (g_TexturePageSRV[slotIndex] != MARNI_NULL_HANDLE) {
            Marni_DX()->DestroyTexture(g_TexturePageSRV[slotIndex]);
        }
        g_TexturePageSRV[slotIndex] = newTex;
    }

    delete[] rgba;
    delete[] clutRGBA;

    return (newTex != MARNI_NULL_HANDLE) ? 1 : 0;
}

int GetTextureNumCLUTs(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= 256) return 0;
    return g_CLUTCache[slotIndex].numCLUTs;
}

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
    // The original always passes a slot's stored PSXTexture work buffer
    // (&DAT_008ed4d0 + slot*0x37c). The port keeps no such copy - the
    // D3D11 SRVs persist instead - and TexturePage_Create/Refresh call here
    // with NULL. CopyFrom would dereference NULL+0x40, so treat it as a
    // no-op that leaves the existing page untouched.
    if (psxTexData == NULL) return 0;

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
                // Build CLUT RGBA palette. The palette dimensions are locals in
                // PSXTexture::Store and are not kept in the object, so derive
                // the entry count: colours per row (by bit depth) x CLUT rows.
                int rows = (int)psxTex.m_NumCLUTs;
                if (rows < 1) rows = 1;
                int numClutEntries = ((bpp == 4) ? 16 : 256) * rows;

                if (numClutEntries > 256) numClutEntries = 256;
                if (numClutEntries < 16)  numClutEntries = 16;

                clutRGBA = new DWORD[numClutEntries];
                WORD* clut = psxTex.m_pCLUTData;   // heap CLUT copy (PSXTexture::Store)

                for (int i = 0; i < numClutEntries; i++) {
                    WORD c = clut[i];
                    if (i == 0) {
                        // Index 0 = transparent
                        clutRGBA[i] = 0x00000000;
                    } else {
                        // PS1 15-bit colour: bits 4-0 red, 9-5 green, 14-10 blue
                        // (bit 15 = STP mask). Was reading red and blue swapped.
                        DWORD a = (c & 0x8000) ? 0xFF : 0xFF; // Always opaque for non-zero indices
                        DWORD r = ((c >> 0)  & 0x1F) * 255 / 31;
                        DWORD g = ((c >> 5)  & 0x1F) * 255 / 31;
                        DWORD b = ((c >> 10) & 0x1F) * 255 / 31;
                        // R8G8B8A8_UNORM wants R in the lowest byte
                        clutRGBA[i] = (a << 24) | (b << 16) | (g << 8) | r;
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
                MarniCreateTexture(w, h, 32, rgbaOut, &pD3D->m_FontTexHandle);
                delete[] rgbaOut;
                delete[] clutRGBA;
            } else {
                MarniCreateTexture(w, h, bpp, srcData, &pD3D->m_FontTexHandle);
            }

            if (pD3D->m_FontTexHandle != MARNI_NULL_HANDLE) {
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
    int storeResult = psxTex.Store((int*)imageBuffer, 1);
    if (storeResult == 0) return;

    // The port's g_VideoDriverArray_838 is a small fragment of the original's
    // ~393KB descriptor table (0x008ed838..0x008f7890), so slot offsets beyond
    // the array land in unrelated .bss globals. The map screen's slots 0x1C-0x1F
    // hit the SCD event table area and corrupt it (menu textures broke, palette
    // changed). The render path reads page state from the g_TexturePage* arrays
    // below, never from this descriptor, so skipping the out-of-range write is
    // safe and the low slots (0-6, used by LoadImage/TitleScreen) keep it.
    if ((unsigned int)slotOffset + 0x24 <= (unsigned int)sizeof(g_VideoDriverArray_838)) {
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
    }

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
    if (psxTex.m_pPixelData != NULL && psxTex.m_WidthPixels > 0 && psxTex.m_Height > 0) {
        int w = psxTex.m_WidthPixels;
        int h = psxTex.m_Height;
        int bpp = psxTex.m_BitDepth;

        if (bpp == 4 || bpp == 8) {
            int numClutEntries = (bpp == 4) ? 16 : 256;
            WORD* clut = psxTex.m_pCLUTData;   // heap CLUT copy (PSXTexture::Store)
            DWORD* clutRGBA = new DWORD[numClutEntries];
            // PS1 15-bit colour is MBBBBBGGGGGRRRRR: bit 15 = STP mask,
            // bits 14-10 = blue, bits 9-5 = green, bits 4-0 = RED. This site
            // used to read red from bits 10-14 and blue from bits 0-4, which
            // swapped the two channels for every CLUT-based model texture -
            // reddish surfaces came out blue. Every other conversion in the
            // tree already uses this layout. PS1 CLUT index 0 is the
            // transparent colour key, so alpha=0 for index 0 (matches
            // RebuildTextureSRV). Bit 15 (STP) marks semi-transparent
            // colours: the map screen's textures (Map_blue grid, floor maps)
            // carry it on their fills, and rendering those opaque made the
            // grid solid instead of the PS1's 50% transparency.
            for (int c = 0; c < numClutEntries; c++) {
                WORD clr = clut[c];
                DWORD r = ((clr >> 0)  & 0x1F) * 255 / 31;
                DWORD g = ((clr >> 5)  & 0x1F) * 255 / 31;
                DWORD a = (c == 0) ? 0x00 : ((clr & 0x8000) ? 0x80 : 0xFF);
                DWORD b = ((clr >> 10) & 0x1F) * 255 / 31;
                // R8G8B8A8_UNORM wants R in the lowest byte
                clutRGBA[c] = (a << 24) | (b << 16) | (g << 8) | r;
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

            if (slotIndex >= 0 && slotIndex < 256) {
                if (g_TexturePageSRV[slotIndex] != MARNI_NULL_HANDLE) {
                    Marni_DX()->DestroyTexture(g_TexturePageSRV[slotIndex]);
                    g_TexturePageSRV[slotIndex] = MARNI_NULL_HANDLE;
                }
                MarniCreateTexture(w, h, 32, rgba, &g_TexturePageSRV[slotIndex]);
                g_TexturePageWidth[slotIndex] = w;
                g_TexturePageHeight[slotIndex] = h;
                g_TexturePageBpp[slotIndex] = bpp;
                g_TexturePageOriginX[slotIndex] = posX;
                g_TexturePageOriginY[slotIndex] = posY;
                g_TexturePageDepth[slotIndex] = texId;
                g_TexturePageClutBase[slotIndex] = pageOffset + 0x1E0;

                // Cache pixel + CLUT data for CLUT palette cycling in texture viewer
                if (psxTex.m_NumCLUTs > 1 && psxTex.m_pPixelData != NULL) {
                    // Free previous cache if any
                    if (g_CLUTCache[slotIndex].pixelData != NULL) {
                        delete[] g_CLUTCache[slotIndex].pixelData;
                    }
                    if (g_CLUTCache[slotIndex].clutData != NULL) {
                        delete[] g_CLUTCache[slotIndex].clutData;
                    }

                    int entriesPerCLUT = (bpp == 4) ? 16 : 256;
                    int totalCLUTEntries = (int)psxTex.m_NumCLUTs * entriesPerCLUT;
                    int pixelBytes = (bpp == 4) ? (w / 2) * h : w * h;

                    // Copy indexed pixel data
                    g_CLUTCache[slotIndex].pixelData = new BYTE[pixelBytes];
                    memcpy(g_CLUTCache[slotIndex].pixelData, psxTex.m_pPixelData, pixelBytes);
                    g_CLUTCache[slotIndex].pixelDataSize = pixelBytes;

                    // Copy all CLUT palettes from the raw TIM buffer
                    // (the heap CLUT copy in PSXTexture only holds the parsed
                    //  copy; reading the file data keeps every palette row)
                    int* hdr = (int*)imageBuffer;
                    WORD* rawCLUT = (WORD*)(hdr + 5);  // CLUT data starts at imageData[5]
                    g_CLUTCache[slotIndex].clutData = new WORD[totalCLUTEntries];
                    memcpy(g_CLUTCache[slotIndex].clutData, rawCLUT, totalCLUTEntries * sizeof(WORD));
                    g_CLUTCache[slotIndex].clutDataSize = totalCLUTEntries * sizeof(WORD);

                    g_CLUTCache[slotIndex].numCLUTs = (int)psxTex.m_NumCLUTs;
                    g_CLUTCache[slotIndex].clutEntries = entriesPerCLUT;
                    g_CLUTCache[slotIndex].bpp = bpp;
                }
            }
            delete[] rgba;
            delete[] clutRGBA;
        } else if (bpp == 16) {
            DWORD* rgba = new DWORD[w * h];
            WORD* src = (WORD*)psxTex.m_pPixelData;
            for (int i = 0; i < w * h; i++) {
                WORD px = src[i];
                // PS1 BGR555 with STP bit (bit 15). This is a TEXTURE PAGE, and
                // the original keyed those on black: CMarniBits::BltFast's
                // colorkey flag (bit 0) skips a source pixel when
                // (color & 0xFFFFFF) == 0 — i.e. black, with the top bit
                // ignored, so both 0x0000 and 0x8000 are cut out. Mirror that
                // exactly:
                //   0x0000 / 0x8000  -> fully transparent (cut-out)
                //   STP set + colour -> semi-transparent (PS1 half blend)
                //   otherwise        -> opaque
                //
                // In the 16bpp pages that reach here the cut-outs are stored as
                // 0x8000 (STP set, colour black): Select_b.tim's round card
                // corners and cursor-arrow surrounds, Optkey03.tim's widget
                // surrounds. None of them contains a 0x0000 texel or an STP
                // texel with a non-zero colour. Black artwork is stored as a
                // near-black colour instead, so keying here does not punch
                // holes in the card's legitimate dark regions — bar a handful
                // of isolated pure-black texels in dithered art (18 in
                // Select_b, 169 in Optkey03's Japanese help text) which the
                // retail PC build dropped out the same way.
                //
                // Do NOT copy this rule into display_image: a background is
                // blitted without the colorkey flag and its black is real
                // artwork. See the note there.
                DWORD a;
                if ((px & 0x7FFF) == 0)  a = 0x00;
                else if (px & 0x8000)    a = 0x80;
                else                     a = 0xFF;
                DWORD r = ((px >> 0)  & 0x1F) * 255 / 31;
                DWORD g = ((px >> 5)  & 0x1F) * 255 / 31;
                DWORD b = ((px >> 10) & 0x1F) * 255 / 31;
                rgba[i] = (a << 24) | (b << 16) | (g << 8) | r;
            }
            if (slotIndex >= 0 && slotIndex < 256) {
                if (g_TexturePageSRV[slotIndex] != MARNI_NULL_HANDLE) {
                    Marni_DX()->DestroyTexture(g_TexturePageSRV[slotIndex]);
                    g_TexturePageSRV[slotIndex] = MARNI_NULL_HANDLE;
                }
                MarniCreateTexture(w, h, 32, rgba, &g_TexturePageSRV[slotIndex]);
                g_TexturePageWidth[slotIndex] = w;
                g_TexturePageHeight[slotIndex] = h;
                g_TexturePageBpp[slotIndex] = 16;
                g_TexturePageOriginX[slotIndex] = posX;
                g_TexturePageOriginY[slotIndex] = posY;
                g_TexturePageDepth[slotIndex] = texId;
                g_TexturePageClutBase[slotIndex] = pageOffset + 0x1E0;
            }
            delete[] rgba;
        }
    }
}

// The single colour every non-transparent CLUT entry is flattened to before
// the page is built (0x0046cd88). RGB555 (15,15,15) - a 48% grey, which is
// exactly how dark the ground shadow can ever make the floor.
#define SHADOW_PAGE_COLOUR  0x3DEF

// ============================================================================
// LoadShadowMaskTexture (0x0046ccd0)
// Shadow/mask texture loader. Reads the 16-bit palette from image offset
// +0x14, flattens every non-transparent colour to SHADOW_PAGE_COLOUR, then
// creates shadow-optimized texture pages (mode=1). Slot shifted by +0x2F.
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

    // The legacy page tables below are byte-offset indexed by slot (0xDF per
    // slot for the counts/handles, 0x37C for the work buffers), but the
    // port's arrays are only 1-4KB fragments of the original's ~393KB
    // descriptor region. Slot 0x2F's offsets (texCheckOffset 10481,
    // slotOffset 41924) exceed every one of them, so the destruction block
    // would read a count ~9KB out of bounds and call destroy_texture_page on
    // arbitrary DWORDs found there - the mechanism that can kill the boot-time
    // menu textures (status.tim @15, statface.tim @24, blue.tim @25,
    // staitem.tim @45), which nothing ever reloads. The port's D3D11 SRVs
    // live in g_TexturePageSRV, not these tables, so the legacy block is only
    // run when its offsets actually land inside the arrays.
    int legacyInRange = (texCheckOffset >= 0) &&
        (texCheckOffset + 8 * (int)sizeof(DWORD) <= (int)sizeof(g_TexturePageTable_DAT)) &&
        (texCheckOffset + 8 * (int)sizeof(DWORD) <= (int)sizeof(g_VideoDriverArray_814)) &&
        (slotOffset + 8 * 0x68 <= (int)sizeof(g_VideoDriverArray_4d0));

    int textureCount = 0;
    if (legacyInRange) {
        textureCount = *(int*)((BYTE*)&g_VideoDriverArray_814 + texCheckOffset);
    }
    if (textureCount != 0 && legacyInRange) {
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
        // Every non-transparent entry collapses to the single page colour
        // 0x3DEF. That is NOT black: RGB555 0x3DEF is (15,15,15) of 31, a
        // 48% grey - the shade the shadow multiplies the floor down to. See
        // the SRV build below, which needs it.
        if (color != 0 && color != 0x8000) {
            srcPalette[i] = SHADOW_PAGE_COLOUR;
        }
    }

    PSXTexture psxTex;
    if (psxTex.Store((int*)imageBuffer, 1) == 0) return;

    // --- D3D11 SRV at the page slot (0x2F) ---
    // The legacy create_texture_page call below only updates the PSX handle
    // table; nothing built the D3D11 SRV DrawFadeSpr samples, and a NULL SRV
    // makes FlushSpriteCommandsRange skip the sprite (no ground shadows, no
    // death blood pool).
    //
    // The original (0x0046ce90) builds this page in two steps. It first draws
    // the BLACKENED image into a work bitmap - every disc pixel becomes the
    // flat 48% grey 0x3DEF, everything outside stays the 0xFFFFFF the bitmap
    // was filled with - and then overwrites each pixel's ALPHA (declared 8
    // bits at shift 24 by the m_alphaShift/Mask/Width stores) from the SAVED,
    // UNBLACKENED palette:
    //     alpha = ((255 - r*8) * 2) & 0xFF          (LEA ECX*2 / SHL 0x18)
    // Note every value in KAGE wraps that mask, so it is a contrast stretch:
    // alpha = 254 - 16r, running 254 at the transparent border down to 62 at
    // the disc core. It is a COVERAGE value, not the shade: the page colour
    // is what the destination gets multiplied toward, and the alpha says how
    // far. So the multiplier the mode-1 page applies is
    //     F = lerp(1.0, 15/31, coverage),   coverage = (255 - alpha) / 255
    // i.e. the shadow bottoms out at 48% of the background, reaching 61% at
    // the disc core (coverage 0.757).
    //
    // This port draws it as a src-alpha sprite over a near-black texel, where
    // out = src*A + dst*(1-A) ~= dst*(1-A), so A must be 1 - F. Baking the
    // raw coverage instead (A = 255 - alpha, giving out = dst*alpha/255 = 24%
    // at the core) threw away the page colour entirely and made the shadow
    // about 2.4x too opaque.
    {
        int w = psxTex.m_WidthPixels;
        int h = psxTex.m_Height;
        BYTE* src = (BYTE*)psxTex.m_pPixelData;
        if (w > 0 && h > 0 && src != NULL) {
            // 1 - 15/31, as 5-bit levels: how much of the destination the
            // page colour can take away at full coverage.
            const unsigned int darken = 31u - (SHADOW_PAGE_COLOUR & 0x1Fu);
            DWORD* rgba = new DWORD[w * h];
            for (int i = 0; i < w * h; i++) {
                unsigned int r = (unsigned int)paletteEntries[src[i]] & 0x1F;
                unsigned int alpha = ((255u - r * 8u) * 2u) & 0xFFu;
                unsigned int coverage = 255u - alpha;
                rgba[i] = (((coverage * darken) / 31u) << 24) | 0x00202020u;
            }
            if (g_TexturePageSRV[slotIndex] != MARNI_NULL_HANDLE) {
                Marni_DX()->DestroyTexture(g_TexturePageSRV[slotIndex]);
                g_TexturePageSRV[slotIndex] = MARNI_NULL_HANDLE;
            }
            MarniCreateTexture(w, h, 32, rgba, &g_TexturePageSRV[slotIndex]);
            delete[] rgba;

            g_TexturePageWidth[slotIndex]  = w;
            g_TexturePageHeight[slotIndex] = h;
            g_TexturePageBpp[slotIndex]    = 8;
        }
    }

    // Create shadow-optimized texture pages with mode=1. Same bounds guard as
    // the destruction block above: slot 0x2F's legacy offsets are far past the
    // port's arrays, and these writes would land in unrelated .bss globals.
    int pageIndex = 0;
    int pageCount = 1;
    if (legacyInRange) {
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
//   vertexData     - array of 24 ints, read COLUMN-MAJOR at stride 4: element [i]
//                    is vertex i's x, [i+4] its y, [i+8] its z, [i+12] its raw u
//                    and [i+16] its raw v. UVs are normalised by the texture page
//                    dimensions at DAT_008ed4fc/DAT_008ed500[texturePageId].
//                    [20],[21],[22] are a single normal shared by all four
//                    vertices; [23] is unread.
//
// Each vertex handed to CMarniViewport2::SetVertex is 11 floats (0x2C bytes):
//   {x, y, z, nx, ny, nz, 1.0f, 1.0f, 1.0f, u, v}
// and the index list is the quad 0, 1, 3, 2.
//
// NOTE: the object handle this produces (g_VideoDriverArray_068[slot], via
// FUN_0046c230) is what AddFadePoly requires to be non-zero; it returns 0 early
// otherwise. While the viewport calls below are stubbed, ground shadows cannot
// draw no matter what the fade-sprite queue contains. See docs/SCD_WORK_PLAN.md.
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

// ============================================================================
// LoadImage (0x0046d3b0)
// Marni System PSYQ LoadImage equivalent.
// Copies raw pixel data from main memory into a CMarniBits surface within the
// texture page table, then creates texture page handles.
//
// Parameters (from Ghidra + assembly analysis):
//   srcData  - pointer to raw pixel data (e.g. 16-bit RGB555)
//   srcSlot  - source texture slot index (template CMarniBits for format)
//   dstSlot  - destination texture slot index
//   format   - pixel format index (1 = 16-bit, bpp=2; indexed into table at 0x4c2d78)
//   x        - X position in destination surface (pixels)
//   y        - Y position in destination surface (pixels)
//   width    - width in pixels to copy
//   height   - height in pixels to copy
//   mode     - CMarniBits sub-page index within the source slot
// ============================================================================
void LoadImage(int srcData, int srcSlot, int dstSlot, short format,
               short x, short y, short width, short height, int mode) // 0x0046d3b0
{
    int destSlot = dstSlot + 0xF;
    int destCheck = destSlot * 0xDF;
    int destOff = destSlot * 0x37C;
    int srcOff = (srcSlot + 0xF) * 0x37C + mode * 0x68;

    // Bytes per pixel from format index (table at 0x4c2d78)
    static const int bppTable[] = { 1, 2, 4 };
    int bpp = (format >= 0 && format < 3) ? bppTable[format] : 2;

    // Destroy existing texture pages at destination slot
    if (g_VideoDriverArray_814[destCheck] != 0) {
        for (DWORD i = 0; i < g_VideoDriverArray_810[destCheck]; i++) {
            DWORD* handles = (DWORD*)((BYTE*)&g_TexturePageTable_DAT + destCheck * sizeof(DWORD));
            if (handles[i] != 0) {
                destroy_texture_page(handles[i]);
                handles[i] = 0;
            }
        }
        VideoDriver_ClearArrayD0();
    }

    // VRAM page simulation: maintain a persistent RGBA buffer per source page
    // (identified by srcOff). LoadImage composites pixel data at position (x, y)
    // within this buffer, just like the PS1 BIOS LoadImage copies to VRAM.
    // The D3D11 SRV is then created from the full VRAM page buffer.
    // Multiple destSlots may share the same source VRAM page (e.g. item images
    // composited at different y-offsets all share srcSlot=0, mode=2). We track
    // all destSlots per VRAM page so the SRV is updated everywhere it's needed.
    #define VRAM_PAGE_W 256
    #define VRAM_PAGE_H 256
    #define VRAM_PAGE_MAX_SLOTS 64
    #define VRAM_MAX_DESTS 16
    struct VRAMPage {
        DWORD* rgba;
        int destSlots[VRAM_MAX_DESTS];
        int numDests;
    };
    static VRAMPage s_vramPages[VRAM_PAGE_MAX_SLOTS];
    static int s_vramInit = 0;
    if (!s_vramInit) { memset(s_vramPages, 0, sizeof(s_vramPages)); s_vramInit = 1; }

    int vramIdx = srcOff & (VRAM_PAGE_MAX_SLOTS - 1);
    if (srcData != 0 && bpp == 2 && (int)width > 0 && (int)height > 0 && vramIdx >= 0 && vramIdx < VRAM_PAGE_MAX_SLOTS) {
        VRAMPage* vp = &s_vramPages[vramIdx];
        if (vp->rgba == NULL) {
            vp->rgba = new DWORD[VRAM_PAGE_W * VRAM_PAGE_H]();
            vp->numDests = 0;
        }

        if (destSlot >= 0 && destSlot < 256) {
            int found = 0;
            for (int i = 0; i < vp->numDests; i++) {
                if (vp->destSlots[i] == destSlot) { found = 1; break; }
            }
            if (!found && vp->numDests < VRAM_MAX_DESTS) {
                vp->destSlots[vp->numDests++] = destSlot;
            }
        }

        DWORD* page = vp->rgba;
        int dstX = ((int)x & 0x3F) * 2;
        int dstY = (int)y;

        // item_all.pix is 8bpp indexed data. The CLUT comes from status.tim
        // (slot 15), loaded earlier by LoadTexturePage with 8bpp + CLUT.
        // LoadImage format=1 (16-bit) copies raw bytes; at 8bpp this means
        // width*2 bytes per row = width*2 pixels per row.
        TextureCLUTCache* clut = &g_CLUTCache[15];
        if (clut->clutData != NULL && clut->bpp == 8 && clut->clutEntries >= 256) {
            BYTE* src8 = (BYTE*)srcData;
            int pixW = (int)width * 2;
            int pixH = (int)height;
            // Use CLUT from printClutTint: 0x1E4 → X=4, 0x1E0 → X=0.
            // CLUT X is in units of 16 halfwords. For 256-entry CLUT (512 bytes
            // = 256 halfwords), CLUT index = X / 16. 0x1E4 X=4 → CLUT 0,
            // but items use 0x1E4 for normal and 0x1E0 for special.
            // Try CLUT 0 first; the correct palette is the one that matches
            // the original PS1 game.
            int clutIndex = 2;
            WORD* clutPalette = clut->clutData + clutIndex * 256;

            for (int row = 0; row < pixH; row++) {
                for (int col = 0; col < pixW; col++) {
                    BYTE idx = src8[row * pixW + col];
                    WORD clr = clutPalette[idx];
                    DWORD r = ((clr >> 0)  & 0x1F) * 255 / 31;
                    DWORD g = ((clr >> 5)  & 0x1F) * 255 / 31;
                    DWORD b = ((clr >> 10) & 0x1F) * 255 / 31;
                    DWORD a = (idx == 0) ? 0x00 : 0xFF;
                    DWORD rgba = (a << 24) | (b << 16) | (g << 8) | r;
                    int px = dstX + col;
                    int py = dstY + row;
                    if (px >= 0 && px < VRAM_PAGE_W && py >= 0 && py < VRAM_PAGE_H) {
                        page[py * VRAM_PAGE_W + px] = rgba;
                    }
                }
            }
        } else {
            WORD* pixels = (WORD*)srcData;
            int w = (int)width;
            int h = (int)height;

            for (int row = 0; row < h; row++) {
                for (int col = 0; col < w; col++) {
                    WORD pixel = pixels[row * w + col];
                    DWORD r = ((pixel >> 0)  & 0x1F) * 255 / 31;
                    DWORD g = ((pixel >> 5)  & 0x1F) * 255 / 31;
                    DWORD b = ((pixel >> 10) & 0x1F) * 255 / 31;
                    DWORD a = (r == 0 && g == 0 && b == 0) ? 0x00 : 0xFF;
                    DWORD rgba = (a << 24) | (b << 16) | (g << 8) | r;
                    for (int sx = 0; sx < 2; sx++) {
                        int px = dstX + col * 2 + sx;
                        int py = dstY + row;
                        if (px >= 0 && px < VRAM_PAGE_W && py >= 0 && py < VRAM_PAGE_H) {
                            page[py * VRAM_PAGE_W + px] = rgba;
                        }
                    }
                }
            }
        }

        for (int d = 0; d < vp->numDests; d++) {
            int slot = vp->destSlots[d];
            if (slot >= 0 && slot < 256) {
                if (g_TexturePageSRV[slot] != MARNI_NULL_HANDLE) {
                    Marni_DX()->DestroyTexture(g_TexturePageSRV[slot]);
                    g_TexturePageSRV[slot] = MARNI_NULL_HANDLE;
                }
                MarniCreateTexture(VRAM_PAGE_W, VRAM_PAGE_H, 32, page, &g_TexturePageSRV[slot]);
                g_TexturePageWidth[slot] = VRAM_PAGE_W;
                g_TexturePageHeight[slot] = VRAM_PAGE_H;
                g_TexturePageBpp[slot] = 16;
                // Set VRAM page metadata so display_texture can locate this
                // SRV by depth/UV bounds. Items are rendered at depth=0x1d
                // with texU=88, texV=slot*32, printClutTint=0x1e4.
                g_TexturePageDepth[slot] = 0x1d;
                g_TexturePageClutBase[slot] = 0x1e0;
                g_TexturePageOriginX[slot] = 0;
                g_TexturePageOriginY[slot] = 0;
            }
        }
    }

    // Lock source CMarniBits to get its pixel data buffer
    CMarniBits* srcBits = (CMarniBits*)((BYTE*)&g_VideoDriverArray_4d0 + srcOff);
    void* lockedPixels = NULL;
    void* lockedPalette = NULL;
    if (!srcBits->Lock(&lockedPixels, (DWORD*)&lockedPalette))
        return;

    // Copy raw pixel data from srcData into the locked CMarniBits buffer.
    // The copy writes width*height 16-bit pixels starting at position (x, y)
    // in the destination surface, row by row.
    // Row stride comes from the source's m_width, NOT m_pitch: the original
    // divides *(src+0x2C) by bpp at 0x0046d47f and again at 0x0046d4d4 to
    // advance a row. bpp here is pixels-per-16-bit-word (1/2/4 for 16/8/4 bpp),
    // so m_width / bpp is the number of 16-bit words in a row - exactly the
    // unit dstOffset is counted in below.
    BYTE* destPixels = (BYTE*)lockedPixels;
    int rowStride = (int)srcBits->m_width / bpp;  // row stride in 16-bit words
    int dstOffset = rowStride * (int)y + (int)x;  // starting offset in words
    BYTE* srcPtr = (BYTE*)srcData;

    for (int row = 0; row < (int)height; row++) {
        if ((int)width > 0) {
            int byteOff = dstOffset * 2;
            for (int col = 0; col < (int)width; col++) {
                *(WORD*)(destPixels + byteOff) = *(WORD*)srcPtr;
                srcPtr += 2;
                byteOff += 2;
            }
        }
        dstOffset += rowStride;
    }

    // CalcAddress on source, then Unlock source.
    // 0x0046d4ea..0x0046d4ff: push [esp+0x1c] (y), then imul bpp by [esp+0x20]
    // (x) and push that - i.e. CalcAddress(bpp * x, y). Those are the same two
    // stack slots dstOffset is built from above, so whatever the arguments are
    // really named, they must match the ones used for dstOffset. This read
    // dstSlot/format instead, which contradicted the dstOffset transcription.
    void* calcAddr = srcBits->CalcAddress(bpp * (int)x, (int)y);
    srcBits->Unlock();

    // SetAddress on destination CMarniBits (first sub-page at destOff)
    CMarniBits* dstBits = (CMarniBits*)((BYTE*)&g_VideoDriverArray_4d0 + destOff);
    dstBits->SetAddress(calcAddr, lockedPixels);

    // Copy pixel format descriptor from source to dest (6 DWORDs + 1 WORD = 26 bytes)
    BYTE* srcDesc = (BYTE*)srcBits + 0x10;
    BYTE* dstDesc = (BYTE*)dstBits + 0x10;
    memcpy(dstDesc, srcDesc, 26);

    // Copy bitDepth and paletteFormat from source
    dstBits->m_bitDepth = srcBits->m_bitDepth;
    dstBits->m_paletteFormat = srcBits->m_paletteFormat;

    // Set destination surface dimensions. The original writes all three of
    // +0x2C/+0x30/+0x34 at 0x0046d564/0x0046d56a/0x0046d586:
    //   m_width  = width * bpp        (bpp = pixels per 16-bit word, so this is
    //                                  the width in PIXELS)
    //   m_height = height
    //   m_pitch  = srcBits->m_pitch   (inherited - the destination is a window
    //                                  into the source surface, so it keeps the
    //                                  source's row stride)
    // This previously put width*bpp into m_pitch and left m_width holding a
    // stale value from whatever last used the slot. Everything downstream that
    // sizes a texture from m_width then read garbage; VTable_CreateTextureHandle
    // walked off the end of the pixel buffer and faulted.
    dstBits->m_width  = (int)width * bpp;
    dstBits->m_height = (int)height;
    dstBits->m_pitch  = srcBits->m_pitch;
    dstBits->m_field38 = srcBits->m_field38;

    // Set flags
    dstBits->m_isValid = 1;
    dstBits->m_dataSource = 0;
    dstBits->m_hasPalette = 1;
    dstBits->m_ownsPalette = 1;
    dstBits->m_flag50 = 1;

    // Set texture descriptor (VRAM position and size)
    WORD* texDesc = (WORD*)((BYTE*)&g_VideoDriverArray_838 + destOff);
    short sign = x >> 15;
    short wrappedX = (short)((((x ^ sign) - sign) & 0x3F) ^ sign) - sign;
    texDesc[0] = (WORD)wrappedX;
    texDesc[1] = (WORD)y;
    texDesc[2] = (WORD)width;
    texDesc[3] = (WORD)height;

    // Copy additional descriptor fields from source slot
    WORD* srcTexDesc = (WORD*)((BYTE*)&g_VideoDriverArray_838 + (srcSlot + 0xF) * 0x37C);
    texDesc[4] = srcTexDesc[4];
    texDesc[5] = srcTexDesc[5];
    texDesc[6] = srcTexDesc[6];
    texDesc[7] = srcTexDesc[7];
    texDesc[8] = srcTexDesc[8] + (short)(((int)(short)x + ((int)(short)x >> 31 & 0x3F)) >> 6);

    // Set slot metadata: 1 page, copy flag from source
    g_VideoDriverArray_810[destCheck] = 1;
    g_VideoDriverArray_814[destCheck] = g_VideoDriverArray_814[(srcSlot + 0xF) * 0xDF];

    // Create texture page handles for each CMarniBits sub-page
    DWORD* pageHandles = (DWORD*)((BYTE*)&g_TexturePageTable_DAT + destCheck * sizeof(DWORD));
    BYTE* pageData = (BYTE*)dstBits + 0x50; // start at m_flag50 of first CMarniBits
    for (DWORD i = 0; i < g_VideoDriverArray_810[destCheck]; i++) {
        *(DWORD*)pageData = 1; // set m_flag50
        int handle = create_texture_page(pageData - 0x50, 2); // pass CMarniBits base
        pageHandles[i] = (DWORD)handle;
        pageData += 0x68; // next CMarniBits
    }

}

// LoadPSXImage - thin wrapper around PSXTexture::Store
void LoadPSXImage(PSXTexture* tex, void* buf, int mode)
{
    tex->Store((int*)buf, mode);
}

