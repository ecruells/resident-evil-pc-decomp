// CharacterSelectionScreen.cpp - Character select screen state and rendering
// Decompiled from Ghidra at 0x00492340
#include "../Globals.h"
#include "../marni/MarniSystem.h"
#include "../marni/PSXTexture.h"
#include "FileLoader.h"
#include "SpriteRenderer.h"
#include "SFXIds.h"
#include <cstdio>

// ============================================================================
// Forward declarations for external functions
// ============================================================================
extern void logos_state(void);
extern void title_state(void);
extern void game_start(void);

// ============================================================================
// DisplayIntroAndStartGame (0x00420060)
// Plays intro FMV then chains to game_start
// ============================================================================
static void DisplayIntroAndStartGame(void)
{
    sounds_reset();
    g_CurrentFMVID = 1;
    g_main_state_flags |= (0x00000000 | 0x00080000);
    title_select_sfx();
    Task_sleep(1);
    Task_chain((void*)game_start);
}

// ============================================================================
// set_fading (0x0047b980)
// ============================================================================
static void SetFading(unsigned char fade_type, short fade_counter)
{
    if (g_fading_state < 1) {
        g_main_state_flags |= 0x20000000;
        g_fading_counter = fade_counter;
        g_fade_type_id = fade_type;
    }
}

// ============================================================================
// Character selection screen state variables (0xac9xxx range)
// ============================================================================

static BYTE g_charSelImageBuffer[320 * 240 * 2];

// Selection state
static unsigned char g_selState = 0;       // DAT_00ac9880 - main state
static unsigned char g_selSubState = 0;    // DAT_00ac9881
static unsigned char g_resetGameFlag = 0;  // 0x00ac9882

static unsigned char g_selSelected = 0;    // DAT_00ac93f0 - 0=Chris, 1=Jill
static char g_selTimer = 0;                // DAT_00ac93f1
static unsigned char g_selSwapDir = 0;     // DAT_00ac93f2

// Character 0 (left panel) data
static short g_char0PosX = 0;             // DAT_00ac9400
static short g_char0PosY = 0;             // DAT_00ac9402
static short g_char0Scale = 0;            // DAT_00ac9404
static short g_char0VelX = 0;             // DAT_00ac9406
static short g_char0VelY = 0;             // DAT_00ac9408
static signed char g_char0AccX = 0;       // DAT_00ac940a
static signed char g_char0AccY = 0;       // DAT_00ac940b
static unsigned char g_char0Tpage = 0;    // DAT_00ac940c
static unsigned char g_char0Bright = 0;   // DAT_00ac940d

// Character 1 (right panel) data
static short g_char1PosX = 0;             // DAT_00ac9640
static short g_char1PosY = 0;             // DAT_00ac9642
static short g_char1Scale = 0;            // DAT_00ac9644
static short g_char1VelX = 0;             // DAT_00ac9646
static short g_char1VelY = 0;             // DAT_00ac9648
static signed char g_char1AccX = 0;       // DAT_00ac964a
static signed char g_char1AccY = 0;       // DAT_00ac964b
static unsigned char g_char1Tpage = 0;    // DAT_00ac964c
static unsigned char g_char1Bright = 0;   // DAT_00ac964d

// Rotation and matrix data (for PS1 GTE compatibility)
static short g_selRotVec[3] = {};          // DAT_00ac93d0
static int g_selTransVec[3] = {};          // DAT_00ac93e0
static int g_selMatrix[12] = {};           // DAT_00ac93b0

// Global TextureDesc for rendering
static TextureDesc g_selTexDesc;

// PS1-style sprite primitive data blocks (0xac9410, 0xac94b0, 0xac9650, 0xac96f0)
static unsigned char g_char0PortraitSprites[0x28 * 4];
static unsigned char g_char0NameSprites[0x28 * 10];
static unsigned char g_char1PortraitSprites[0x28 * 4];
static unsigned char g_char1NameSprites[0x28 * 10];

// Sprite UV data tables (from Ghidra at 0x4d3ed0, 0x4d3ed8, 0x4d3ee0)
static const unsigned char g_charPortraitTable[2][4] = {
    { 0x00, 0x00, 0x50, 0x80 },
    { 0x50, 0x00, 0x70, 0x80 },
};
static const unsigned char g_char1PortraitTable[2][4] = {
    { 0x00, 0x80, 0x50, 0x80 },
    { 0x00, 0x80, 0x70, 0x80 },
};
static const unsigned char g_nameSpriteTable[5][4] = {};

// ============================================================================
// GetScaledValue - Apply fix16.12 scale to a dimension
// ============================================================================
static int GetScaledValue(int value, short scaleFix12)
{
    return (value * scaleFix12) >> 12;
}

// ============================================================================
// ComputeScale - Convert panel scale value to fix16.12
// Original: scale * -0x18 + 0x2680 (from AddSprite_Ex in FUN_00492d80)
// At scale=0xF0(240): -5760+9856=4096=0x1000=1.0
// ============================================================================
static short ComputeScale(short panelScale)
{
    return panelScale * -24 + 0x2680;
}

// ============================================================================
// CharSelectDrawShadowRect - Renders a semi-transparent shadow overlay on a card
// Submits to g_SpriteCommandBuffer so it renders between back and front cards
// ============================================================================
static void CharSelectDrawShadowRect(short posX, short posY, short scaleFix12)
{
    if (g_SpriteQueueCount >= MAX_SPRITE_COMMANDS - 1) return;

    // Scaled dimensions: card total is 0xC0 x 0x80 = 192 x 128 at 1.0x
    int shadowW = (int)(192.0f * (float)scaleFix12 / 4096.0f);
    int shadowH = (int)(128.0f * (float)scaleFix12 / 4096.0f);

    int sx = posX - 0x100 + g_ScreenOffsetX;
    int sy = posY - 0x98 + g_ScreenOffsetY;

    TextureDraw* cmd = &g_SpriteCommandBuffer[g_SpriteQueueCount];
    cmd->type = 10;
    cmd->unk1c = 100.0f / 255.0f;  // alpha = ~0.392
    cmd->r = 0.0f;
    cmd->g = 0.0f;
    cmd->b = 0.0f;
    cmd->texturePage = 0;

    cmd->x0 = (short)sx;
    cmd->y0 = (short)sy;
    cmd->x1 = (short)(sx + shadowW - 1);
    cmd->y1 = (short)(sy + shadowH - 1);

    cmd->depthSort = 42 * 16 + 500;

    // Use an opaque pixel from the portrait texture page (slot 0x0C)
    cmd->u0 = 0x40;
    cmd->v0 = 0x40;
    cmd->u1 = 0x40;
    cmd->v1 = 0x40;

    cmd->extraFlags = 0x0C + 0xF;

    g_SpriteQueueCount++;
}

// ============================================================================
// CharSelectRenderSprite - Renders a portrait sprite with scaling
// Replaces AddSprite_Ex calls from FUN_00492d80 with D3D11-compatible rendering
// ============================================================================
static void CharSelectRenderSprite(unsigned char texU, unsigned char texV,
                                   unsigned short width, unsigned short height,
                                   short posX, short posY,
                                   short pivotX, short pivotY,
                                   short scaleFix12, unsigned short depth,
                                   int slot)
{
    if (g_SpriteQueueCount >= MAX_SPRITE_COMMANDS - 1) return;
    if ((g_main_state_flags & 0x40000000) != 0) return;

    int shiftedSlot = slot + 0xF;
    if (shiftedSlot < 0 || shiftedSlot >= 256) return;
    if (g_TexturePageSRV[shiftedSlot] == NULL) return;

    // Apply scaling
    int scaledW = GetScaledValue(width, scaleFix12);
    int scaledH = GetScaledValue(height, scaleFix12);
    int scaledPX = GetScaledValue(pivotX, scaleFix12);
    int scaledPY = GetScaledValue(pivotY, scaleFix12);

    short sx = posX + g_ScreenOffsetX;
    short sy = posY + g_ScreenOffsetY;

    TextureDraw* cmd = &g_SpriteCommandBuffer[g_SpriteQueueCount];
    cmd->type = 10;
    cmd->unk1c = 0.0f;
    cmd->r = 1.0f;
    cmd->g = 1.0f;
    cmd->b = 1.0f;
    cmd->texturePage = 0;

    cmd->x0 = sx - scaledPX;
    cmd->y0 = sy - scaledPY;
    cmd->x1 = sx + scaledW - scaledPX - 1;
    cmd->y1 = sy + scaledH - scaledPY - 1;

    cmd->depthSort = (unsigned int)depth * 16 + 500;

    cmd->u0 = (unsigned short)texU;
    cmd->v0 = (unsigned short)texV;
    cmd->u1 = cmd->u0 + width - 1;
    cmd->v1 = cmd->v0 + height - 1;

    cmd->extraFlags = shiftedSlot;

    g_SpriteQueueCount++;
}

// ============================================================================
// CharSelectDrawCursor (FUN_00493200)
// Draws the blinking selection cursor arrows
// ============================================================================
static void CharSelectDrawCursor(void)
{
    g_selTexDesc.texU = 0xC0;
    g_selTexDesc.texV = 0x30;
    g_selTexDesc.screenX = -0x88;
    g_selTexDesc.screenY = -0x16;
    g_selTexDesc.width = 0x0C;
    g_selTexDesc.height = 0x0B;
    g_selTexDesc.printClutTint = 0x1EA;

    if ((g_selTimer & 0x30) == 0) {
        g_selTexDesc.texV = 0x50;
    }

    display_texture(&g_selTexDesc, 1, 0x0C, 1);
    g_selTexDesc.texV += 0x10;
    g_selTexDesc.screenX = 0x4C;
    display_texture(&g_selTexDesc, 1, 0x0C, 1);
}

// ============================================================================
// CharSelectUpdateVelocity (FUN_004932d0)
// Updates position deltas from acceleration values
// ============================================================================
static void CharSelectUpdateVelocity(void)
{
    g_char0VelX += g_char0AccX;
    g_char0VelY += g_char0AccY;
    g_char1VelX += g_char1AccX;
    g_char1VelY += g_char1AccY;
}

// ============================================================================
// CharSelectApplyPosition (FUN_00493290)
// Applies velocity to position
// ============================================================================
static void CharSelectApplyPosition(void)
{
    g_char0PosX += g_char0VelX;
    g_char0PosY += g_char0VelY;
    g_char1PosX += g_char1VelX;
    g_char1PosY += g_char1VelY;
}

// ============================================================================
// BakeBorderMaskIntoCardTexture (post-load helper)
// Reads the card texture (slot 0x0C) and mask texture (slot 0x0D) from D3D11,
// then zeroes the card texture's alpha wherever the mask is transparent.
// This replicates the PS1 mask-bit feature that clips card round corners.
// ============================================================================
static void BakeBorderMaskIntoCardTexture(void)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (!pD3D || !pD3D->m_pD3DDevice || !pD3D->m_pD3DContext) return;

    int cardSlot = 0x0C + 0xF;  // 27
    int maskSlot = 0x0D + 0xF;  // 28

    ID3D11ShaderResourceView* cardSRV = g_TexturePageSRV[cardSlot];
    ID3D11ShaderResourceView* maskSRV = g_TexturePageSRV[maskSlot];
    if (!cardSRV || !maskSRV) {
        OutputDebugStringA("[MASK] Card or mask SRV is NULL, skipping bake\n");
        return;
    }

    // Get the underlying textures from the SRVs
    ID3D11Resource* cardRes = NULL;
    ID3D11Resource* maskRes = NULL;
    cardSRV->GetResource(&cardRes);
    maskSRV->GetResource(&maskRes);
    if (!cardRes || !maskRes) { 
        if (cardRes) cardRes->Release();
        if (maskRes) maskRes->Release();
        return;
    }

    // Get texture dimensions
    D3D11_TEXTURE2D_DESC cardDesc, maskDesc;
    ((ID3D11Texture2D*)cardRes)->GetDesc(&cardDesc);
    ((ID3D11Texture2D*)maskRes)->GetDesc(&maskDesc);

    int w = (int)cardDesc.Width;
    int h = (int)cardDesc.Height;
    int mw = (int)maskDesc.Width;
    int mh = (int)maskDesc.Height;

    // Create staging textures for CPU read/write
    D3D11_TEXTURE2D_DESC stagingDesc = cardDesc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
    stagingDesc.MiscFlags = 0;

    ID3D11Texture2D* cardStaging = NULL;
    ID3D11Texture2D* maskStaging = NULL;
    HRESULT hr = pD3D->m_pD3DDevice->CreateTexture2D(&stagingDesc, NULL, &cardStaging);
    if (FAILED(hr)) { cardRes->Release(); maskRes->Release(); return; }

    D3D11_TEXTURE2D_DESC maskStagingDesc = maskDesc;
    maskStagingDesc.Usage = D3D11_USAGE_STAGING;
    maskStagingDesc.BindFlags = 0;
    maskStagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    maskStagingDesc.MiscFlags = 0;
    hr = pD3D->m_pD3DDevice->CreateTexture2D(&maskStagingDesc, NULL, &maskStaging);
    if (FAILED(hr)) { cardStaging->Release(); cardRes->Release(); maskRes->Release(); return; }

    // Copy card and mask textures to staging
    pD3D->m_pD3DContext->CopyResource(cardStaging, (ID3D11Texture2D*)cardRes);
    pD3D->m_pD3DContext->CopyResource(maskStaging, (ID3D11Texture2D*)maskRes);

    // Map both for reading
    D3D11_MAPPED_SUBRESOURCE cardMap, maskMap;
    hr = pD3D->m_pD3DContext->Map(cardStaging, 0, D3D11_MAP_READ_WRITE, 0, &cardMap);
    if (FAILED(hr)) { cardStaging->Release(); maskStaging->Release(); cardRes->Release(); maskRes->Release(); return; }
    hr = pD3D->m_pD3DContext->Map(maskStaging, 0, D3D11_MAP_READ, 0, &maskMap);
    if (FAILED(hr)) { pD3D->m_pD3DContext->Unmap(cardStaging, 0); cardStaging->Release(); maskStaging->Release(); cardRes->Release(); maskRes->Release(); return; }

    DWORD* cardPixels = (DWORD*)cardMap.pData;
    DWORD* maskPixels = (DWORD*)maskMap.pData;
    int cardModified = 0;

    // Apply mask alpha to card texture
    // Where mask alpha is 0, set card alpha to 0 (transparent corners)
    for (int y = 0; y < h && y < mh; y++) {
        for (int x = 0; x < w && x < mw; x++) {
            int ci = y * (cardMap.RowPitch / 4) + x;
            int mi = y * (maskMap.RowPitch / 4) + x;
            DWORD maskPixel = maskPixels[mi];
            DWORD maskAlpha = (maskPixel >> 24) & 0xFF;
            if (maskAlpha == 0) {
                // Mask is transparent here → make card pixel transparent
                cardPixels[ci] = cardPixels[ci] & 0x00FFFFFF;  // zero alpha
                cardModified++;
            }
        }
    }

    // Cursor arrow UV regions in the card texture (4 arrows: normal+blink × left+right)
    // Each is 0x0C×0x0B (12×11) pixels. Background should be fully transparent.
    static const struct { int u, v; } s_cursorRegions[] = {
        { 0xC0, 0x30 },  // left arrow normal
        { 0xC0, 0x40 },  // right arrow normal
        { 0xC0, 0x50 },  // left arrow blink
        { 0xC0, 0x60 },  // right arrow blink
    };
    int cursorW = 0x0C, cursorH = 0x0B;

    for (int rc = 0; rc < 4; rc++) {
        int cu = s_cursorRegions[rc].u;
        int cv = s_cursorRegions[rc].v;
        // Sample the top-left corner pixel — it should be background
        if (cu >= w || cv >= h) continue;
        int cornerIdx = cv * (cardMap.RowPitch / 4) + cu;
        DWORD cornerRGB = cardPixels[cornerIdx] & 0x00FFFFFF;
        DWORD cornerAlpha = (cardPixels[cornerIdx] >> 24) & 0xFF;

        if (cornerAlpha == 0) continue;  // already transparent

        // Background pixel is opaque — make all matching pixels in this region transparent
        int cursorFixed = 0;
        for (int dy = 0; dy < cursorH && (cv + dy) < h; dy++) {
            for (int dx = 0; dx < cursorW && (cu + dx) < w; dx++) {
                int pi = (cv + dy) * (cardMap.RowPitch / 4) + (cu + dx);
                DWORD pixRGB = cardPixels[pi] & 0x00FFFFFF;
                if (pixRGB == cornerRGB) {
                    cardPixels[pi] = pixRGB;  // zero alpha
                    cursorFixed++;
                }
            }
        }
        cardModified += cursorFixed;
        {
            char dbg[128];
            sprintf(dbg, "[MASK] Cursor region (%d,%d): bg=0x%06X, cleared %d pixels\n",
                    cu, cv, (unsigned)cornerRGB, cursorFixed);
            OutputDebugStringA(dbg);
        }
    }

    {
        char dbg[128];
        sprintf(dbg, "[MASK] Total: %d/%d pixels made transparent (card %dx%d, mask %dx%d)\n",
                cardModified, w * h, w, h, mw, mh);
        OutputDebugStringA(dbg);
    }

    pD3D->m_pD3DContext->Unmap(maskStaging, 0);
    pD3D->m_pD3DContext->Unmap(cardStaging, 0);

    // Copy modified card staging back to the actual texture
    pD3D->m_pD3DContext->CopyResource((ID3D11Texture2D*)cardRes, cardStaging);

    cardStaging->Release();
    maskStaging->Release();
    cardRes->Release();
    maskRes->Release();
}

// ============================================================================
// CharSelectDrawPortraits (FUN_00492d80)
// Renders the character portrait sprites with scaling and shadow
// Back card (higher tpage) is drawn first, shadow on back card, then front card
// Border mask alpha is baked into the card texture (see BakeBorderMaskIntoCardTexture)
// ============================================================================
static void CharSelectDrawPortraits(void)
{
    short scale0 = ComputeScale(g_char0Scale);
    short scale1 = ComputeScale(g_char1Scale);

    if (g_char0Tpage > g_char1Tpage) {
        // Char 0 has higher tpage = back panel → draw first
        CharSelectRenderSprite(0, 0, 0x50, 0x80, g_char0PosX - 0x100, g_char0PosY - 0x98, 0, 0, scale0, g_char0Tpage << 4, 0x0C);
        CharSelectRenderSprite(0x50, 0, 0x70, 0x80, g_char0PosX - 0x100, g_char0PosY - 0x98, -0x50, 0, scale0, g_char0Tpage << 4, 0x0C);
        // Shadow on back card
        CharSelectDrawShadowRect(g_char0PosX, g_char0PosY, scale0);
        // Char 1 is front panel → draw last
        // Original FUN_00492d80: char1 first sprite uses texU=0, texV=0 (same as char0 left half)
        CharSelectRenderSprite(0, 0, 0x50, 0x80, g_char1PosX - 0x100, g_char1PosY - 0x98, 0, 0, scale1, g_char1Tpage << 4, 0x0C);
        CharSelectRenderSprite(0, 0x80, 0x70, 0x80, g_char1PosX - 0x100, g_char1PosY - 0x98, -0x50, 0, scale1, g_char1Tpage << 4, 0x0C);
    } else {
        // Char 1 has higher tpage = back panel → draw first
        CharSelectRenderSprite(0, 0, 0x50, 0x80, g_char1PosX - 0x100, g_char1PosY - 0x98, 0, 0, scale1, g_char1Tpage << 4, 0x0C);
        CharSelectRenderSprite(0, 0x80, 0x70, 0x80, g_char1PosX - 0x100, g_char1PosY - 0x98, -0x50, 0, scale1, g_char1Tpage << 4, 0x0C);
        // Shadow on back card
        CharSelectDrawShadowRect(g_char1PosX, g_char1PosY, scale1);
        // Char 0 is front panel → draw last
        CharSelectRenderSprite(0, 0, 0x50, 0x80, g_char0PosX - 0x100, g_char0PosY - 0x98, 0, 0, scale0, g_char0Tpage << 4, 0x0C);
        CharSelectRenderSprite(0x50, 0, 0x70, 0x80, g_char0PosX - 0x100, g_char0PosY - 0x98, -0x50, 0, scale0, g_char0Tpage << 4, 0x0C);
    }
}

// ============================================================================
// characterSelectionScreen (0x00492340)
// Main character selection state - state machine driven by g_selState
// ============================================================================
void characterSelectionScreen(void)
{
    OutputDebugStringA("[CharSelection] Entered character selection screen\n");

    g_playingGameFlag = 1;
    g_bGameActive = 0;

    g_selState = 0;
    g_selSubState = 0;
    g_resetGameFlag = 0;

    sounds_reset();
    g_loadDataDestPointer = g_image_buffer;
    g_roomId = 0x1B;

    LoadSoundBank(BANK_SELECT, g_image_buffer);

    // Load characters police cards texture (select_b.tim → slot 0xC)
    LoadFile(".\\usa\\data\\select_b.tim", g_charSelImageBuffer, 0x20);
    g_TextureBankID = 0x0A;
    g_TextureDepthByte = 5;
    LoadTexturePage(g_charSelImageBuffer, 5, 10, 0x0C, 0, 0, 0, 0);

    // Load card round borders masks texture (select_k.tim → slot 0xD)
    LoadFile(".\\usa\\data\\select_k.tim", g_charSelImageBuffer, 0x20);
    LoadTexturePage(g_charSelImageBuffer, 5, 10, 0x0D, 7, 0, 0, 0);

    // Bake border mask alpha into card texture for round corner transparency
    BakeBorderMaskIntoCardTexture();

    // Load background image
    LoadFile(".\\usa\\data\\sel_back.pix", g_charSelImageBuffer, 0x20);
    display_image(0, g_charSelImageBuffer, 320, 240);

    title_setup_texture_pages(0, 1);
    empty_00470960(0);

    // Initialize rotation matrix
    g_selRotVec[0] = 0;
    g_selRotVec[1] = 0;
    g_selRotVec[2] = 0;
    g_selTransVec[0] = 0;
    g_selTransVec[1] = 0;
    g_selTransVec[2] = 0;
    RotMatrix((SVECTOR*)g_selRotVec, (MATRIX*)g_selMatrix);
    MatrixSetTranslation((MATRIX*)g_selMatrix, g_selTransVec);
    SetGlobalScaledRotationMatrix((MATRIX*)g_selMatrix);
    GetMatrixTranslation((MATRIX*)g_selMatrix);
    set_title_render_param(0xF0);

    //Init Character Sprite Data
    for (int local_6 = 1; local_6 >= 0; local_6--) {
        for (int local_5 = 1; local_5 >= 0; local_5--) {
            int uVar9 = local_5;
            int iVar1 = local_6 * 2 + uVar9;
            int iVar2 = iVar1 * 0x28;

            GteSpriteHeaderInit((SVECTOR*)(g_char0PortraitSprites + iVar2));
            GteSpriteHeaderInit((SVECTOR*)(g_char1PortraitSprites + iVar2));

            unsigned char b0 = g_charPortraitTable[uVar9][0];
            unsigned char b1 = g_charPortraitTable[uVar9][1];
            unsigned char b2 = g_charPortraitTable[uVar9][2];
            unsigned char b3 = g_charPortraitTable[uVar9][3];

            *(g_char0PortraitSprites + iVar2 + 0x0c) = b0;
            *(g_char0PortraitSprites + iVar2 + 0x0d) = b1;
            *(g_char0PortraitSprites + iVar2 + 0x14) = b0 + b2;
            *(g_char0PortraitSprites + iVar2 + 0x15) = b1;
            *(g_char0PortraitSprites + iVar2 + 0x1c) = b0;
            *(g_char0PortraitSprites + iVar2 + 0x1d) = b1 + b3;
            *(g_char0PortraitSprites + iVar2 + 0x24) = b0 + b2;
            *(g_char0PortraitSprites + iVar2 + 0x25) = b1 + b3;

            b0 = g_char1PortraitTable[uVar9][0];
            b1 = g_char1PortraitTable[uVar9][1];
            b2 = g_char1PortraitTable[uVar9][2];
            b3 = g_char1PortraitTable[uVar9][3];

            *(g_char1PortraitSprites + iVar2 + 0x0c) = b0;
            *(g_char1PortraitSprites + iVar2 + 0x0d) = b1;
            *(g_char1PortraitSprites + iVar2 + 0x14) = b0 + b2;
            *(g_char1PortraitSprites + iVar2 + 0x15) = b1;
            *(g_char1PortraitSprites + iVar2 + 0x1c) = b0;
            *(g_char1PortraitSprites + iVar2 + 0x1d) = b1 + b3;
            *(g_char1PortraitSprites + iVar2 + 0x24) = b0 + b2;
            *(g_char1PortraitSprites + iVar2 + 0x25) = b1 + b3;

            *(short*)(g_char0PortraitSprites + iVar2 + 0x0e) =
                (short)GteClutBuild(0, (short)(uVar9 + 0x1ea));
            *(short*)(g_char1PortraitSprites + iVar2 + 0x0e) =
                (short)GteClutBuild(0, (short)(uVar9 * 2 + 0x1ea));

            *(short*)(g_char0PortraitSprites + iVar2 + 0x16) =
                (short)GteTpageBuild(1, 0, 0x140, 0);
            *(short*)(g_char1PortraitSprites + iVar2 + 0x16) =
                (short)GteTpageBuild(1, 0, 0x140, 0);
        }

        for (int local_5 = 4; local_5 >= 0; local_5--) {
            int uVar9 = local_5;
            int iVar1 = local_6 * 5 + uVar9;
            int iVar2 = iVar1 * 0x28;

            GteSpriteHeaderInit((SVECTOR*)(g_char0NameSprites + iVar2));
            GteSpriteHeaderInit((SVECTOR*)(g_char1NameSprites + iVar2));

            unsigned char b0 = g_nameSpriteTable[uVar9][0];
            unsigned char b1 = g_nameSpriteTable[uVar9][1];
            unsigned char b2 = g_nameSpriteTable[uVar9][2];
            unsigned char b3 = g_nameSpriteTable[uVar9][3];

            *(g_char0NameSprites + iVar2 + 0x0c) = b0;
            *(g_char0NameSprites + iVar2 + 0x0d) = b1;
            *(g_char0NameSprites + iVar2 + 0x14) = b0 + b2;
            *(g_char0NameSprites + iVar2 + 0x15) = b1;
            *(g_char0NameSprites + iVar2 + 0x1c) = b0;
            *(g_char0NameSprites + iVar2 + 0x1d) = b1 + b3;
            *(g_char0NameSprites + iVar2 + 0x24) = b0 + b2;
            *(g_char0NameSprites + iVar2 + 0x25) = b1 + b3;

            *(g_char1NameSprites + iVar2 + 0x0c) = b0;
            *(g_char1NameSprites + iVar2 + 0x0d) = b1;
            *(g_char1NameSprites + iVar2 + 0x14) = b0 + b2;
            *(g_char1NameSprites + iVar2 + 0x15) = b1;
            *(g_char1NameSprites + iVar2 + 0x1c) = b0;
            *(g_char1NameSprites + iVar2 + 0x1d) = b1 + b3;
            *(g_char1NameSprites + iVar2 + 0x24) = b0 + b2;
            *(g_char1NameSprites + iVar2 + 0x25) = b1 + b3;

            *(short*)(g_char0NameSprites + iVar2 + 0x0e) =
                (short)GteClutBuild(0, 0x1ea);
            *(short*)(g_char1NameSprites + iVar2 + 0x0e) =
                (short)GteClutBuild(0, 0x1ea);

            *(short*)(g_char0NameSprites + iVar2 + 0x16) =
                (short)GteTpageBuild(1, 2, 0x140, 0);
            *(short*)(g_char1NameSprites + iVar2 + 0x16) =
                (short)GteTpageBuild(1, 2, 0x140, 0);

            *(g_char0NameSprites + iVar2 + 0x04) = 0x40;
            *(g_char0NameSprites + iVar2 + 0x05) = 0x40;
            *(g_char0NameSprites + iVar2 + 0x06) = 0x40;
            *(g_char1NameSprites + iVar2 + 0x04) = 0x40;
            *(g_char1NameSprites + iVar2 + 0x05) = 0x40;
            *(g_char1NameSprites + iVar2 + 0x06) = 0x40;
        }
    }

    // Initialize character panel state
    g_char0Tpage = 2;
    g_char1Tpage = 3;
    g_char0Bright = 0x80;
    g_char1Bright = 0x50;
    g_char0PosX = 0x88;
    g_char0PosY = 0x48;
    g_char0Scale = 0xF0;
    g_selSelected = 0;
    g_fade_type_id = 2;
    g_char1PosX = 200;
    g_char1PosY = 0x68;
    g_char1Scale = 0x110;
    g_fading_counter = 0xF800;
    g_main_state_flags = (g_main_state_flags & 0x3FFFFFFF) | 0x80000000;
    fade_update();

    // Main loop
    while (1) {
        // Check game reset flag
        if (g_resetGameFlag != 0) {
            g_resetGameFlag = 0;
            StMask(0, 3);
            g_main_state_flags2 = g_main_state_flags2 & 0x20080000;
            g_main_state_flags = (g_main_state_flags & 0x2FFFFFFF) | 0x40000000;
            Task_chain((void*)title_state);
        }

        // Common texture descriptor setup
        g_selTexDesc.unk10 = 0;
        g_selTexDesc.pivotX = 0;
        g_selTexDesc.pivotY = 0;
        g_selTexDesc.printClutTint = 0;
        g_selTexDesc.flags = 0x01000040;
        g_selTexDesc.colorMulR = 0x80;
        g_selTexDesc.colorMulG = 0x80;
        g_selTexDesc.colorMulB = 0x80;
        g_selTexDesc.depth = 5;

        switch (g_selState) {
        case 0:
            // Wait for initial fade-in to complete
            if (g_fading_state < 0) {
                g_selState = 1;
                g_selTimer = 0;
            }
            break;

        case 1:
        {
            // Character selection input handling
            // Cancel: hold R3 (0x04) + START (0x08) → return to title
            if ((g_PlayerPadHeld & 4) && (g_PlayerPadHeld & 8)) {
                g_main_state_flags = (g_main_state_flags & 0x3FFFFFFF) | 0x40000000;
                g_selSubState = 0;
                g_selState = 7;
                SetFading(2, 0xC00);
                break;
            }

            // SideWinder check
            DWORD sidewinderPress = 0;
            if (g_isSideWinderConnected) {
                sidewinderPress = read_sidewinder_pad();
                sidewinderPress &= 0x10000;
            }

            // Swap characters on LEFT or RIGHT press
            if (g_PlayerPadPressed & (PAD_LEFT | PAD_RIGHT)) {
                g_selSubState = 0;
                g_selState = 2;
                g_selSwapDir = (g_PlayerPadPressed & PAD_RIGHT) ? 1 : 0;

                play_sfx(1, 0);

                if (g_selSwapDir == g_selSelected) {
                    g_char0AccX = 2;
                    g_char0AccY = 1;
                } else {
                    g_char0AccX = -2;
                    g_char0AccY = -1;
                }

                g_char0VelX = 0;
                g_char0VelY = 0;
                g_char1AccX = -g_char0AccX;
                g_char1AccY = -g_char0AccY;
                g_char1VelX = 0;
                g_char1VelY = 0;
                g_selTimer = 8;
                goto case_2;
            }

            // Confirm selection on CROSS or START press
            if ((g_PlayerPadPressed & (PAD_CROSS | PAD_START)) || sidewinderPress) {
                play_sfx(1, 1);
                g_selSubState = 0;
                g_selState = 3;
                g_fade_type_id = 1;
                g_fading_counter = 0x100;
                fade_update();
                break;
            }

            // Idle: decrement blink timer
            g_selTimer--;
            break;
        }

        case 2:
            // Character swap animation
case_2:
            g_selTimer--;

            switch (g_selSubState) {
            case 0:
                // Phase 0: wait for initial timer
                if (g_selTimer == 0) {
                    g_selSubState = 1;
                    g_char0VelX += g_char0AccX;
                    g_char0VelY += g_char0AccY;
                    g_selTimer = (g_selSwapDir ^ 1) << 2;
                    g_char1VelX += g_char1AccX;
                    g_char1VelY += g_char1AccY;
                    goto sub_1;
                }
                goto do_update_velocity;

            default:
            do_update_velocity:
                CharSelectUpdateVelocity();
                break;

            case 1:
sub_1:
                // Phase 1: movement phase
                if (g_selTimer == 0) {
                    g_selSubState = 2;
                    g_selTimer = 0x0F;
                    g_char0AccX = -g_char0AccX;
                    g_char0AccY = -g_char0AccY;
                    g_char1AccX = -g_char1AccX;
                    g_char1AccY = -g_char1AccY;
                    goto sub_2;
                }
                break;

            case 2:
sub_2:
                // Phase 2: velocity reaches zero → swap tpage (front/back flip)
                if (g_char0VelX == 0) {
                    g_selSubState = 3;
                    g_char0Tpage ^= 1;
                    g_char1Tpage ^= 1;
                }

            case 3:
                {
                    // Phase 3: scale and brightness adjustment
                    unsigned char sel = g_selSelected;
                    unsigned char other = g_selSelected ^ 1;

                    // Adjust scale: selected panel grows, other shrinks
                    if (sel == 0) {
                        g_char0Scale += 2;
                        g_char1Scale -= 2;
                    } else {
                        g_char1Scale += 2;
                        g_char0Scale -= 2;
                    }

                    // Adjust brightness
                    if (sel == 0) {
                        g_char0Bright -= 3;
                        g_char1Bright += 3;
                    } else {
                        g_char1Bright -= 3;
                        g_char0Bright += 3;
                    }

                    if (g_selTimer == 0) {
                        g_selSubState = 4;
                        g_char0VelX += g_char0AccX;
                        g_char0VelY += g_char0AccY;
                        g_selTimer = g_selSwapDir << 2;
                        g_char1VelX += g_char1AccX;
                        g_char1VelY += g_char1AccY;
                    } else {
                        goto do_update_velocity;
                    }
                }

            case 4:
                // Phase 4: second movement phase
                // Original (0x00492b3d case 4): just breaks when timer!=0 (NO velocity update)
                // This is intentional - cards coast at constant velocity during this phase
                if (g_selTimer == 0) {
                    g_selSubState = 5;
                    g_selTimer = 7;
                    g_char0AccX = -g_char0AccX;
                    g_char0AccY = -g_char0AccY;
                    g_char1AccX = -g_char1AccX;
                    g_char1AccY = -g_char1AccY;
                    goto sub_5;
                }
                break;

            case 5:
sub_5:
                // Phase 5: final adjustment
                if (g_selTimer == 0) {
                    g_selState = 1;
                    g_selSelected ^= 1;
                }
                goto do_update_velocity;
            }

            CharSelectApplyPosition();
            break;

        case 3:
            // Fade out after selection confirmed
            if (g_fading_state < 0) {
                g_selState = 4;
                g_fade_type_id = 1;
                g_fading_counter = 0xFF00;
                fade_update();
                g_main_state_flags = (g_main_state_flags & 0x3FFFFFFF) | 0x40000000;
            }
            break;

        case 4:
            // Wait for fade to complete
            if (g_fading_state < 0) {
                g_selState = 5;
                g_selTimer = -16;
            }
            break;

        case 5:
            // Post-fade timer delay
            g_selTimer--;
            if (g_selTimer == 0) {
                goto case_6;
            }
            break;

        case 6:
case_6:
            // Set selected player and transition to game
            g_FmvCharacterId = g_selSelected;
            if (g_selSelected != 0) {
                g_main_state_flags |= 0x800000;
            }
            g_bGameActive = 2;
            title_select_sfx();
            cleanup_texture_slot(12);
            cleanup_texture_slot(13);
            cleanup_texture_slot(14);
            DisplayIntroAndStartGame();
            return;

        case 7:
            // Return to title screen
            if ((g_main_state_flags & 0x20000000) == 0) {
                g_bGameActive = 2;
                title_select_sfx();
                cleanup_texture_slot(0x0C);
                cleanup_texture_slot(0x0D);
                cleanup_texture_slot(0x0E);
                Task_chain((void*)title_state);
                return;
            }
            break;
        }

        // Render portraits when in selection or transition states
        if (g_selState < 4) {
            CharSelectDrawPortraits();
        }

        // Draw cursor arrows on top of portraits when idle in selection state
        if (g_selState == 1) {
            CharSelectDrawCursor();
        }

        Task_sleep(1);
    }
}
