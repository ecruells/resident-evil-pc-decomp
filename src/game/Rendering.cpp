// Rendering.cpp - Frame rendering, present, sprite drawing
// All functions decompiled from Ghidra with original addresses
#include "../Globals.h"
#include "../marni/MarniSystem.h"
#include "../marni/PSXTexture.h"
#include "SpriteRenderer.h"
#include <cstdlib>

extern unsigned int set_message_display(unsigned short msg_id, unsigned short pause_game);

// ============================================================================
// Pending sprite queue (filled by AddTintSprite / draw_rect / OT_InsertPrimitive,
// rendered by FrameRateGovernor)
// ============================================================================
#define MAX_PENDING_SPRITES 300

struct PendingSprite {
    float x, y, w, h;
    float u0, v0, u1, v1;
    DWORD color;
    ID3D11ShaderResourceView* srv;
    BOOL valid;
    unsigned int depth;   // OT depth sort value (lower = closer = on top)
};
static PendingSprite g_pendingSprites[MAX_PENDING_SPRITES];
static int g_pendingSpriteCount = 0;

ID3D11ShaderResourceView* g_displayImageSRV = NULL;

// ============================================================================
// AddTintSprite (0x0046e0a0)
// Adds a tinted font character sprite to the pending sprite queue.
// Brightness controls color intensity. Pending sprites are rendered in
// FrameRateGovernor before FlushSpriteCommands.
// ============================================================================
int AddTintSprite(TextureDesc* texture, unsigned short brightness)
{
    if (g_pendingSpriteCount >= MAX_PENDING_SPRITES) return 0;

    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (pD3D == NULL || pD3D->m_pFontSRV == NULL) return 0;
    if (pD3D->m_FontTexWidth <= 0 || pD3D->m_FontTexHeight <= 0) return 0;

    float gameX = (float)(texture->screenX + g_ScreenOffsetX);
    float gameY = (float)(texture->screenY + g_ScreenOffsetY);

    float scaleX = (float)pD3D->m_width / 320.0f;
    float scaleY = (float)pD3D->m_height / 240.0f;

    float screenX = gameX * scaleX;
    float screenY = gameY * scaleY;
    float charW = (float)texture->width * scaleX;
    float charH = (float)texture->height * scaleY;

    float texW = (float)pD3D->m_FontTexWidth;
    float texH = (float)pD3D->m_FontTexHeight;
    float u0 = (float)texture->texU / texW;
    float v0 = (float)texture->texV / texH;
    float u1 = (float)(texture->texU + texture->width) / texW;
    float v1 = (float)(texture->texV + texture->height) / texH;

    // Calculate RGB from tint values — cast to unsigned int first to avoid overflow
    unsigned int r = ((unsigned int)(texture->colorMulR & 0xFF)) * 2; if (r > 255) r = 255;
    unsigned int g = ((unsigned int)(texture->colorMulG & 0xFF)) * 2; if (g > 255) g = 255;
    unsigned int b = ((unsigned int)(texture->colorMulB & 0xFF)) * 2; if (b > 255) b = 255;

    DWORD color;
    if (texture->colorMulR == 0 && texture->colorMulG == 0 && texture->colorMulB == 0) {
        // Shadow pass: semi-transparent black (brightness controls alpha)
        unsigned int a = ((unsigned int)brightness * 255) / 30;
        if (a > 255) a = 255;
        color = (a << 24) | (0 << 16) | (0 << 8) | 0;
    } else {
        // Text pass: opaque color, brightness dims RGB (original PS1 CLUT-based dimming)
        unsigned int brightnessScale = ((unsigned int)brightness * 255) / 30;
        if (brightnessScale > 255) brightnessScale = 255;
        if (brightness == 2) brightnessScale = 255;
        r = (r * brightnessScale) / 255;
        g = (g * brightnessScale) / 255;
        b = (b * brightnessScale) / 255;
        color = (255u << 24) | (r << 16) | (g << 8) | b;
    }

    PendingSprite* spr = &g_pendingSprites[g_pendingSpriteCount];
    spr->x = screenX;
    spr->y = screenY;
    spr->w = charW;
    spr->h = charH;
    spr->u0 = u0;
    spr->v0 = v0;
    spr->u1 = u1;
    spr->v1 = v1;
    spr->color = color;
    spr->srv = pD3D->m_pFontSRV;
    spr->valid = TRUE;
    spr->depth = (unsigned int)brightness * 16 + 0x1C2;  // OT depth: higher=further behind

    g_pendingSpriteCount++;
    return 1;
}

// ============================================================================
// GetTextureVariant (0x0046d950)
// Returns blend variant index from texture flags.
// ============================================================================
static int GetTextureVariant(unsigned int textureFlags)
{
    if ((textureFlags & 0x40000000) != 0) {
        return ((textureFlags & 0x30000000) >> 0x1c) + 1;
    }
    return 0;
}

// ============================================================================
// draw_rect (0x00470350)
// Fills a rectangle with the given color. Uses pending sprite queue.
// The blend parameter controls depth/ordering.
// The textureId field selects blend variant via GetTextureVariant:
//   0: Opaque fill (g_window_rect, etc.)
//   1: Semi-transparent tinted overlay (special room lighting)
//   2: Semi-transparent white flash (fade_type_id=1)
//   3: Semi-transparent black fade (fade_type_id=2)
// ============================================================================
void draw_rect(RectDrawDesc* rect, int blend, int flags)
{
    if (rect == NULL) return;
    if (g_pendingSpriteCount >= MAX_PENDING_SPRITES) return;

    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (pD3D == NULL) return;

    float gameX = (float)(rect->x + g_ScreenOffsetX);
    float gameY = (float)(rect->y + g_ScreenOffsetY);
    float gameW = (float)rect->w;
    float gameH = (float)rect->h;

    float scaleX = (float)pD3D->m_width / 320.0f;
    float scaleY = (float)pD3D->m_height / 240.0f;

    float screenX = gameX * scaleX;
    float screenY = gameY * scaleY;
    float screenW = gameW * scaleX;
    float screenH = gameH * scaleY;

    unsigned char r = (unsigned char)(rect->r & 0xFF);
    unsigned char g = (unsigned char)(rect->g & 0xFF);
    unsigned char b = (unsigned char)(rect->b & 0xFF);

    int variant = GetTextureVariant(rect->textureId);
    unsigned char a;

    switch (variant) {
    case 2:
        // fade_type_id=1: White flash — white overlay, alpha = brightness
        a = r; if (g > a) a = g; if (b > a) a = b;
        r = 255; g = 255; b = 255;
        break;
    case 3:
        // fade_type_id=2: Fade to black — black overlay, alpha = brightness
        a = r; if (g > a) a = g; if (b > a) a = b;
        r = 0; g = 0; b = 0;
        break;
    case 1:
        // Special room lighting — tinted overlay, alpha = max component
        a = r; if (g > a) a = g; if (b > a) a = b;
        break;
    default:
        // Variant 0 or unknown: fully opaque (g_window_rect, etc.)
        a = 255;
        break;
    }

    if (a == 0) return;

    DWORD color = ((unsigned int)a << 24) | ((unsigned int)r << 16) | ((unsigned int)g << 8) | (unsigned int)b;

    ID3D11ShaderResourceView* srv = pD3D->m_pWhiteSRV;

    PendingSprite* spr = &g_pendingSprites[g_pendingSpriteCount];
    spr->x = screenX;
    spr->y = screenY;
    spr->w = screenW;
    spr->h = screenH;
    spr->u0 = 0.0f;
    spr->v0 = 0.0f;
    spr->u1 = 1.0f;
    spr->v1 = 1.0f;
    spr->color = color;
    spr->srv = srv;
    spr->valid = TRUE;
    // Depth: higher value = further back (drawn first).
    // In original OT: flags==0 → blend+450, else → blend*16+500
    spr->depth = (flags == 0) ? ((unsigned int)blend + 450) : ((unsigned int)blend * 16 + 500);

    g_pendingSpriteCount++;
}

// ============================================================================
// FrameRateGovernor (0x004973d0)
// ============================================================================
void FrameRateGovernor(void)
{
    g_numFramesRendered++;

    DWORD currentTime = timeGetTime();
    int frameDelta = 0;

    if (g_LastFrameTime_ms != 0) {
        frameDelta = (int)(currentTime - g_LastFrameTime_ms);

        if (frameDelta < 300) {
            if (g_bUseFrameSkip) {
                g_frameTimeIndex += 2;
                g_frameTimeBuffer[g_frameTimeIndex & 3] = frameDelta;
                g_frameTimeBuffer[(g_frameTimeIndex - 1) & 3] = frameDelta;
            } else {
                g_frameTimeIndex++;
                g_frameTimeBuffer[g_frameTimeIndex & 3] = frameDelta;
            }

            if (g_frameTimeIndex > 3) g_frameTimeIndex = 0;
        }
    }

    if (g_frameTimeIndex == 0) {
        int frameTimeSum = 0;
        for (int i = 0; i < 4; i++) {
            frameTimeSum += g_frameTimeBuffer[i];
        }

        if (!g_bUseFrameSkip) {
            g_frameTargetTime = (int)((frameTimeSum * 100 + (frameTimeSum * 100 >> 31 & 0x3FU)) >> 6);
        } else {
            g_frameTargetTime = (frameTimeSum * 100) / 132;
        }

        if (g_frameTargetTime < 100) g_frameTargetTime = 100;
        g_bFrameSkipDetected = (g_frameTargetTime > 100);
        if (g_frameTargetTime > 800) g_frameTargetTime = 800;
    }

    g_frameTimeAccumulator += 100;

    if (g_frameTimeAccumulator < g_frameTargetTime) {
        g_LastFrameTime_ms = 0;
        g_pendingSpriteCount = 0;
        SpriteQueue_Reset();
    } else {
        if (g_ScreenAccessReady && g_RenderAccessReady) {
            MarniClear();

            FUN_0040a8f0(NULL);

            // Sort pending sprites by depth (descending: high depth first = behind, low depth last = on top)
            for (int i = 0; i < g_pendingSpriteCount - 1; i++) {
                for (int j = i + 1; j < g_pendingSpriteCount; j++) {
                    if (g_pendingSprites[i].depth < g_pendingSprites[j].depth) {
                        PendingSprite tmp = g_pendingSprites[i];
                        g_pendingSprites[i] = g_pendingSprites[j];
                        g_pendingSprites[j] = tmp;
                    }
                }
            }

            // Render high-depth pending sprites first (background, room lighting)
            // Threshold: depth >= 500 are scene elements, depth < 500 are screen overlays (fade, color rects)
            for (int i = 0; i < g_pendingSpriteCount; i++) {
                if (g_pendingSprites[i].valid && g_pendingSprites[i].depth >= 500) {
                    MarniDrawSprite(
                        g_pendingSprites[i].x, g_pendingSprites[i].y,
                        g_pendingSprites[i].w, g_pendingSprites[i].h,
                        g_pendingSprites[i].u0, g_pendingSprites[i].v0,
                        g_pendingSprites[i].u1, g_pendingSprites[i].v1,
                        g_pendingSprites[i].color,
                        g_pendingSprites[i].srv);
                }
            }

            // Render command buffer sprites (game objects, title text, etc.)
            FlushSpriteCommands();

            // Render low-depth pending sprites last (fade overlays, color tinting)
            for (int i = 0; i < g_pendingSpriteCount; i++) {
                if (g_pendingSprites[i].valid && g_pendingSprites[i].depth < 500) {
                    MarniDrawSprite(
                        g_pendingSprites[i].x, g_pendingSprites[i].y,
                        g_pendingSprites[i].w, g_pendingSprites[i].h,
                        g_pendingSprites[i].u0, g_pendingSprites[i].v0,
                        g_pendingSprites[i].u1, g_pendingSprites[i].v1,
                        g_pendingSprites[i].color,
                        g_pendingSprites[i].srv);
                }
            }

            if (!g_DisablePad) {
                MarniPresent();
            }
            g_numFramesPresented++;

            ResetSpriteQueue();
        }

        g_frameTimeAccumulator -= g_frameTargetTime;
        if (g_frameTimeAccumulator < 0) g_frameTimeAccumulator = 0;

        g_LastFrameTime_ms = currentTime;

        if (g_frameTimeAccumulator > g_frameTargetTime) {
            g_frameTimeAccumulator = 0;
        }
    }

    // StMask countdown (0x004d4684). StMask(0,N) stores N here and clears
    // g_ScreenAccessReady; once it reaches zero, presentation is re-enabled.
    // The original FrameRateGovernor (0x004973d0) decrements THIS variable,
    // not g_ScreenAccessCheck (0x004d2290, which is a separate flag read by
    // main_loop and written by the menu-transition code). Using the wrong
    // variable left g_ScreenAccessReady stuck at 0 after any StMask(0,N),
    // freezing presentation on a stale backbuffer.
    if (g_ScreenAccessCountdown != 0) {
        g_ScreenAccessCountdown--;
        if (g_ScreenAccessCountdown == 0) g_ScreenAccessReady = 1;
    }
    if (g_RenderAccessCheck != 0) {
        g_RenderAccessCheck--;
        if (g_RenderAccessCheck == 0) g_RenderAccessReady = 1;
    }
}

// ============================================================================
// FUN_0040a8f0 (0x0040a8f0)
// ============================================================================
void FUN_0040a8f0(void* param)
{
    g_titlePrimType = 1;
    g_primFlag2 = 2;
    g_primParam = g_sceneRenderParam;
    OT_InsertPrimitive(&g_titlePrimType, 0xFFF);
}

// ============================================================================
// OT_InsertPrimitive (0x004402f0)
// ============================================================================
void OT_InsertPrimitive(void* prim, unsigned int depth)
{
    if (depth != 0xFFF) return;

    DWORD* p = (DWORD*)prim;
    if (p[0] != 1) return;

    if (g_displayImageSRV == NULL) return;
    if ((g_main_state_flags & 0x40000000) != 0) return;
    if (g_pendingSpriteCount >= MAX_PENDING_SPRITES) return;

    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;

    for (int i = g_pendingSpriteCount; i > 0; i--) {
        g_pendingSprites[i] = g_pendingSprites[i - 1];
    }
    g_pendingSprites[0].x = 0;
    g_pendingSprites[0].y = 0;
    g_pendingSprites[0].w = (float)(pD3D ? pD3D->m_width : 640);
    g_pendingSprites[0].h = (float)(pD3D ? pD3D->m_height : 480);
    g_pendingSprites[0].u0 = 0;
    g_pendingSprites[0].v0 = 0;
    g_pendingSprites[0].u1 = 1;
    g_pendingSprites[0].v1 = 1;
    g_pendingSprites[0].color = 0xFFFFFFFF;
    g_pendingSprites[0].srv = g_displayImageSRV;
    g_pendingSprites[0].valid = TRUE;
    g_pendingSprites[0].depth = 0xFFF;  // background (far, drawn first)
    g_pendingSpriteCount++;
}

// ============================================================================
// ResetSpriteQueue
// ============================================================================
void ResetSpriteQueue(void)
{
    g_pendingSpriteCount = 0;
    SpriteQueue_Reset();
}

// ============================================================================
// display_texture (0x0046e8d0)
// ============================================================================
void display_texture(TextureDesc* texture, unsigned short depth, int slot, int pageCount)
{
    if (g_SpriteQueueCount >= MAX_SPRITE_COMMANDS - 1) return;
    if ((g_main_state_flags & 0x40000000) != 0) return;

    int shiftedSlot = slot + 0xF;
    if (shiftedSlot < 0 || shiftedSlot >= 256) return;
    if (g_TexturePageSRV[shiftedSlot] == NULL) return;

    TextureDraw* cmd = &g_SpriteCommandBuffer[g_SpriteQueueCount];
    cmd->type = 10;

    unsigned int flags;
    BuildSpriteRenderFlags(texture->flags, &flags);
    int variant = GetTextureVariant(texture->flags);
    cmd->unk1c = (float)((variant != 0) ? (flags | 8) : flags);

    cmd->r = (float)texture->colorMulR * g_ColorScaleFactor;
    cmd->g = (float)texture->colorMulG * g_ColorScaleFactor;
    cmd->b = (float)texture->colorMulB * g_ColorScaleFactor;

    cmd->texturePage = 0;

    short sx = texture->screenX + g_ScreenOffsetX;
    short sy = texture->screenY + g_ScreenOffsetY;
    cmd->x0 = sx - texture->pivotX;
    cmd->y0 = sy - texture->pivotY;
    cmd->x1 = (texture->width - texture->pivotX) + sx - 1;
    cmd->y1 = (texture->height - texture->pivotY) + sy - 1;

    cmd->depthSort = (unsigned int)depth * 16 + 500;

    cmd->u0 = (unsigned short)texture->texU;
    cmd->v0 = (unsigned short)texture->texV;
    cmd->u1 = cmd->u0 + texture->width - 1;
    cmd->v1 = cmd->v0 + texture->height - 1;

    cmd->extraFlags = shiftedSlot;

    g_SpriteQueueCount++;
}

// ============================================================================
// display_image (0x00470770)
// Loads raw 16-bit PS1 pixel data and creates a D3D11 texture + SRV.
// ============================================================================
void display_image(int slot, void* buffer, int width, int height)
{
    g_DisplayImageWidth = width;
    g_DisplayImageHeight = height;

    if (g_displayImageSRV != NULL) {
        g_displayImageSRV->Release();
        g_displayImageSRV = NULL;
    }

    unsigned short* src = (unsigned short*)buffer;
    int pixelCount = width * height;
    unsigned int* rgba = (unsigned int*)malloc(pixelCount * 4);
    if (rgba == NULL) return;

    for (int i = 0; i < pixelCount; i++) {
        unsigned short px = src[i];
        unsigned char r = ((px >> 0)  & 0x1F) * 255 / 31;
        unsigned char g = ((px >> 5)  & 0x1F) * 255 / 31;
        unsigned char b = ((px >> 10) & 0x1F) * 255 / 31;
        unsigned char a = (((px >> 0) & 0x1F) == 0 && ((px >> 5) & 0x1F) == 0 && ((px >> 10) & 0x1F) == 0) ? 0x00 : 0xFF;
        rgba[i] = (a << 24) | (b << 16) | (g << 8) | r;
    }

    ID3D11Texture2D* tex = NULL;
    MarniCreateTexture(width, height, 32, rgba, &tex, &g_displayImageSRV);
    free(rgba);
    if (tex != NULL) tex->Release();
}

// ============================================================================
// SetFrameRateMode - Set frame rate unlocked mode based on game active state
// Original: FUN_00442150 at 0x00442150
// ============================================================================
void SetFrameRateMode(int bActive) {
    if (bActive != 0) { g_bUseFrameSkip = TRUE; }
    else { g_bUseFrameSkip = FALSE; }
}

void ResetScreenPanning(void) {
    CenterScreenOrigin();
    // Dummy_00429a30()
    g_main_state_flags = g_main_state_flags & 0xfff7ffff;
}

// ============================================================================
// SetScreenOffset (0x00483600)
// Sets both screen offset and subpixel rendering offset.
// The original PS1 code stored absolute screen coordinates (including g_ScreenOffsetX/Y)
// in the sprite command buffer. FlushSpriteCommands converts these PS1-space coordinates
// to screen-space coordinates using scaleX/scaleY.
// ============================================================================
void SetScreenOffset(int x, int y)
{
    g_ScreenOffsetX = x;
    g_ScreenOffsetY = y;
    g_SubpixelOffsetX = x;
    g_SubpixelOffsetY = y;
}

// ============================================================================
// ApplyScreenShake (0x0045aac0)
// Generates random ±1 screen shake offsets and applies them centered at (160,120).
// ============================================================================
void ApplyScreenShake(void)
{
    int val;

    // Random X offset: (rand() & 1) with random sign
    val = rand();
    signed char signX = (signed char)(val >> 31);
    g_ScreenShakeOffsetX = (signed char)((((unsigned char)val ^ signX) - signX) & 1 ^ signX) - signX;

    // Random Y offset: (rand() & 1) with random sign
    val = rand();
    signed char signY = (signed char)(val >> 31);
    g_ScreenShakeOffsetY = (signed char)((((unsigned char)val ^ signY) - signY) & 1 ^ signY) - signY;

    // Random direction (0-3) to optionally negate X and/or Y
    val = rand();
    unsigned int uSign = (unsigned int)((int)val >> 31);
    int dir = (int)(((val ^ uSign) - uSign) & 3 ^ uSign) - uSign;
    if (dir != 1) {
        if (dir == 2) {
            g_ScreenShakeOffsetX = -g_ScreenShakeOffsetX;
        } else if (dir == 3) {
            g_ScreenShakeOffsetX = -g_ScreenShakeOffsetX;
            g_ScreenShakeOffsetY = -g_ScreenShakeOffsetY;
        }
    } else {
        g_ScreenShakeOffsetY = -g_ScreenShakeOffsetY;
    }

    // Apply shake to screen offset and subpixel offset.
    // In the original: SetScreenOffset(shakeX + 0xa0, shakeY + 0x78) set both
    // g_ScreenOffsetX/Y and g_SubpixelOffsetX/Y to the full centering+shake value.
    g_ScreenOffsetX = g_ScreenShakeOffsetX;
    g_ScreenOffsetY = g_ScreenShakeOffsetY;
    SetScreenOffset(g_ScreenShakeOffsetX + 160, g_ScreenShakeOffsetY + 120);
}

// 0x0045ab60
void ApplyShakeAndRebuildSprites() {
    if ( (g_main_state_flags2 & 2) == 0 || ((g_main_state_flags >> 8) & 0xFF) != 0 )
    Display_SetParams(2, 2);
    else
    Display_SetParams(g_ScreenShakeOffsetX + 2, g_ScreenShakeOffsetY + 2);
    SetScreenReady(1);
    FUN_00470a90();
}

// ============================================================================
// FUN_00455140 (0x00455140) - Item name lookup
// Returns a pointer to the item name string. If the item is not yet examined,
// returns the generic "???" name instead.
// TODO: Wire up to the real item name tables (0x004bf0a0, 0x004bf260, 0x004bd823)
// ============================================================================
static unsigned char* message_item_name_lookup(unsigned char itemId)
{
    // Stub: return a placeholder string for now
    static unsigned char unknownName[] = "???";
    (void)itemId;
    return unknownName;
}

// ============================================================================
// FUN_00456020 (0x00456020) - Message character rendering
// Renders the message text characters from g_MessagePtr up to g_MessageCurrentPtr.
// Handles newlines, color changes, item name substitution, and character glyphs.
// ============================================================================
static void message_render_chars(void)
{
    unsigned char bVar1;
    unsigned char* pbVar2;
    unsigned char* pbVar3;
    unsigned short fade;
    unsigned char* savedPtr;

    g_TextureDesc.screenX = 0x30 - g_ScreenOffsetX;
    g_TextureDesc.screenY = g_MessageScreenY;
    g_TextureDesc.flags = 0x40;
    g_TextureDesc.width = 8;
    g_TextureDesc.printClutTint = g_MessageClutBase + 0x1e0;
    g_TextureDesc.height = 0xe;
    g_TextureDesc.unk10 = 0x100;

    pbVar2 = g_MessagePtr;
    if (g_MessagePtr == g_MessageCurrentPtr) {
        g_DepthSortOverride = 0;
        return;
    }

    do {
        bVar1 = *pbVar2;
        if (bVar1 == 0) goto msg_next_char;

        switch (bVar1) {
        case 2: // newline
            pbVar3 = pbVar2 + 1;
            g_TextureDesc.screenY += 0x10;
            g_TextureDesc.screenX = 0x30 - g_ScreenOffsetX;
            break;

        case 3: // unknown tag (skip 1 byte)
        case 4: // unknown tag (skip 1 byte)
            pbVar2 = pbVar2 + 1;
            // fall through
        case 1: // end-of-page delay marker
            pbVar3 = pbVar2 + 1;
            break;

        case 5: // set CLUT color
            g_TextureDesc.unk10 = 0x100;
            g_TextureDesc.printClutTint = pbVar2[1] + 0x1e0;
            pbVar3 = pbVar2 + 2;
            break;

        case 6: // item name lookup
            bVar1 = pbVar2[1];
            if (pbVar2[1] == 0) {
                bVar1 = g_selectedItemId;
            }
            pbVar3 = message_item_name_lookup(bVar1);
            savedPtr = pbVar2;
            break;

        case 7: // return from item name
            pbVar3 = savedPtr + 2;
            break;

        case 0xf8: // single-width character
            pbVar3 = pbVar2 + 1;
            pbVar2 = pbVar2 + 1;
            bVar1 = *pbVar3 / 0x12 + 0xf;
            goto msg_render_char;

        case 0xf9: // medium-width character
            bVar1 = pbVar2[1] / 0x12;
            goto msg_render_char_wide;

        case 0xfa: // full-width character
            bVar1 = pbVar2[1] / 0x12 + 0xe;
msg_render_char_wide:
            g_TextureDesc.depth = 0x1f;
            pbVar2 = pbVar2 + 1;
            goto msg_draw_char;

        default: // normal character
            bVar1 = bVar1 / 0x12 + 2;
msg_render_char:
            g_TextureDesc.depth = 0x1e;
msg_draw_char:
            // Calculate texture coordinates from character index
            g_TextureDesc.texV = bVar1 * 0xe;
            g_TextureDesc.texU = *pbVar2 % 0x12 << 3;

            g_DepthSortOverride = 0;

            // Fade type selection for specific room/camera
            if ((g_stageId == 3) && (g_roomId == 0x11) &&
                (g_roomCameraId == 0x04 || g_roomCameraId == 0x00)) {
                fade = 0;
            } else {
                fade = 2;
            }

            AddTintSprite(&g_TextureDesc, fade);

msg_next_char:
            g_TextureDesc.screenX += 8;
            pbVar3 = pbVar2 + 1;
            break;
        }
        pbVar2 = pbVar3;
    } while (pbVar3 != g_MessageCurrentPtr);

    g_DepthSortOverride = 0;
}

// ============================================================================
// FUN_00455fb0 (0x00455fb0) - Message dismissal handler
// Handles message-triggered actions: yes/no selection, item usage,
// lab slides, and other conditional actions.
// ============================================================================
static void message_dismiss_handler(void)
{
    // Advance past current position
    g_MessageCurrentPtr++;

    unsigned char* pbVar2 = g_MessageCurrentPtr;
    unsigned int choiceOffset = (unsigned int)(g_menu_choice_id & 1) * (unsigned int)*g_MessageCurrentPtr;
    g_MessageCurrentPtr = g_MessageCurrentPtr + choiceOffset + 1;

    unsigned char cmdByte = *g_MessageCurrentPtr;
    g_MessageCurrentPtr = pbVar2 + choiceOffset + 2;

    if (cmdByte == 9) {
        // Display follow-up message
        set_message_display(*g_MessageCurrentPtr, g_PauseGameInMsgFlag);
        return;
    }

    if (cmdByte != 10) {
        return;
    }

    // cmdByte == 10: conditional actions based on sub-command
    switch (*g_MessageCurrentPtr) {
    case 0: // Room event / flag action
        // TODO: Original checks g_room_event_index and calls FUN_00451700
        return;

    case 1: // Use selected item
        g_usedItemId = g_selectedItemId;
        return;

    case 2: // Discard selected item from inventory
        {
            unsigned char slotIdx = 0;
            unsigned char itemId = *(unsigned char*)g_ItemSlotsPointer;
            while (itemId != g_selectedItemId) {
                slotIdx++;
                itemId = ((unsigned char*)g_ItemSlotsPointer)[(unsigned int)slotIdx * 2];
            }
            ((unsigned char*)g_ItemSlotsPointer)[(unsigned int)slotIdx * 2] = 0;
            rearrange_item_slots();
        }
        return;

    case 3: // No action
        return;

    case 4: // Lab slides / cutscene start
    case 6: // Lab slides end
        // Deferred to full implementation
        return;
    }
}

// ============================================================================
// UpdateMessageDisplay (0x004557b0) - Message display state machine
// Called each frame from main_loop to advance the message display.
// Manages character-by-character text reveal, timing, yes/no prompts,
// and message dismissal.
// ============================================================================
void UpdateMessageDisplay(void)
{
    unsigned char bVar1;
    int lineCount;
    unsigned char* pbVar3;
    short screenX;
    unsigned short fade;

    g_TextureDesc.colorMulR = 0x80;
    g_TextureDesc.colorMulG = 0x80;
    g_TextureDesc.colorMulB = 0x80;
    g_TextureDesc.pivotX = 0;
    unk_00be1180 = 0;
    g_TextureDesc.pivotY = 0;
    g_SpriteAsyncFlag = 0;

    switch (g_MessageStateCounter) {

    // === State 0: Initialize message display ===
    case 0:
        g_MessageStateCounter = 1;
        // Set char delay: 1 frame normally, shifted by g_bGameActive
        g_MessageCharDelay = (unsigned char)(1 << (g_bGameActive == 0));
        g_MessageCurrentPtr = g_MessagePtr;
        g_MessageClutBase = 0;
        g_MessageClutCopy = 0;
        g_MessageLineCounter = 0;
        g_MessageCharTimer = g_MessageCharDelay;
        // Fall through to state 1

    // === State 1: Character-by-character text reveal ===
    case 1:
        bVar1 = g_MessageCharTimer;
        g_MessageCharTimer = g_MessageCharTimer - 1;

        if (g_MessageSpeedUpFlag == 0) {
            // Normal speed: wait for timer
            if (g_MessageCharTimer != 0) break;
        } else {
            // Speed-up mode: reduce timer faster
            if (g_MessageCharTimer != 0) {
                if ((g_PlayerDpadHeld & 0x4000) != 0) {
                    g_MessageCharTimer = bVar1 - 2; // double speed
                }
                goto state1_check_timer;
            }
        }

    state1_check_timer:
        if (g_MessageCharTimer != 0) break;

        // Timer expired: process next character
        bVar1 = *g_MessageCurrentPtr;
        lineCount = g_MessageLineCounter;
        pbVar3 = g_MessageCurrentPtr;

    state1_process_char:
        g_MessageCurrentPtr = pbVar3;
        g_MessageLineCounter = lineCount;

        // Check for end of text or special character
        if (bVar1 == 0) goto msg_skip_char;
        bVar1 = *pbVar3;
        if (bVar1 > 0x0b && bVar1 < 0xf8) goto msg_skip_char;

        switch (bVar1) {
        case 1: // Page end marker with delay
            g_MessageCurrentPtr = pbVar3 + 1;
            if (*g_MessageCurrentPtr == 0) {
                // End of message: wait for input
                g_MessageStateCounter = 5;
                message_render_chars();
                return;
            }
            // Auto-advance after delay
            g_MessageStateCounter = 6;
            g_MessageCharTimer = *g_MessageCurrentPtr << (g_bGameActive == 0);
            message_render_chars();
            return;

        case 2: // Next page / skip
            goto state1_case2;

        case 3: // Newline (page break)
            g_SpriteAsyncFlag = 1;
            g_MessageLineCounter = lineCount + 1;
            if (lineCount < 5) {
                g_MessageCharTimer = g_MessageCharTimer + 1;
                message_render_chars();
                return;
            }
            // More than 5 lines: clear and continue
            g_MessageCurrentPtr = pbVar3 + 1;
            g_MessageLineCounter = 0;
            if (*g_MessageCurrentPtr == 0) {
                g_MessageStateCounter = 2;
                g_MessageCurrentPtr = pbVar3 + 2;
                message_render_chars();
                return;
            }
            g_MessageStateCounter = 3;
            g_MessageCharTimer = *g_MessageCurrentPtr << (g_bGameActive == 0);
            g_MessageCurrentPtr = pbVar3 + 2;
            message_render_chars();
            return;

        case 4: // Skip embedded tags
            g_MessageCurrentPtr = pbVar3 + 1;
            if (*g_MessageCurrentPtr == 0) {
                // Skip to end marker (tag 4)
                g_MessageCurrentPtr = pbVar3 + 2;
                bVar1 = *g_MessageCurrentPtr;
                while (bVar1 != 4) {
                    switch (*g_MessageCurrentPtr) {
                    case 5:
                    case 6:
                    case 0xf8:
                    case 0xf9:
                    case 0xfa:
                        g_MessageCurrentPtr++;
                    }
                    g_MessageCurrentPtr++;
                    bVar1 = *g_MessageCurrentPtr;
                }
                g_MessageCurrentPtr++;
            }
            g_MessageCharDelay = *g_MessageCurrentPtr << (g_bGameActive == 0);
            goto state1_case2;

        case 5: // Set CLUT color
            g_MessageCurrentPtr = pbVar3 + 1;
            g_MessageClutCopy = *g_MessageCurrentPtr;
state1_case2:
            g_MessageCurrentPtr++;
            break;

        case 6: // Item name lookup
            g_MessageCurrentPtr = pbVar3 + 1;
            bVar1 = *g_MessageCurrentPtr;
            if (*g_MessageCurrentPtr == 0) {
                bVar1 = g_selectedItemId;
            }
            g_MessageSavedPtr = pbVar3;
            g_MessageCurrentPtr = message_item_name_lookup(bVar1);
            break;

        case 7: // Return from item name
            g_MessageCurrentPtr = g_MessageSavedPtr + 2;
            break;

        case 8: // Yes/No prompt
            g_MessageStateCounter = 4;
            message_render_chars();
            return;

        case 0xf8:
        case 0xf9:
        case 0xfa: // Character codes — skip to render
            g_MessageCurrentPtr = pbVar3 + 1;
msg_skip_char:
            g_MessageCurrentPtr++;
            g_MessageCharTimer = g_MessageCharDelay;
            goto state1_default;
        }

        // Process next character in sequence
        bVar1 = *g_MessageCurrentPtr;
        lineCount = g_MessageLineCounter;
        pbVar3 = g_MessageCurrentPtr;
        goto state1_process_char;

    // === State 2: Waiting with blinking cursor ===
    case 2:
        if ((g_PlayerDpadPressed & 0xC000) != 0) {
            // Button pressed: restart text reveal
            g_MessageStateCounter = 1;
            g_MessagePtr = g_MessageCurrentPtr;
            g_MessageClutBase = g_MessageClutCopy;
            g_MessageCharTimer = (unsigned char)(1 << (g_bGameActive == 0));
            message_render_chars();
            return;
        }
        g_MessageCharTimer = g_MessageCharTimer - 1;
        // Blink cursor every ~24 frames (0x18 = 24)
        if ((((unsigned int)g_MessageCharTimer & (0x18 << (g_bGameActive == 0)))) != 0) {
            // Draw cursor indicator
            g_TextureDesc.flags = 0x40;
            g_TextureDesc.width = 8;
            g_TextureDesc.height = 0xe;
            g_TextureDesc.depth = 0x1e;
            g_TextureDesc.texU = 0x58;
            g_TextureDesc.texV = 0x1c;
            g_TextureDesc.unk10 = 0x100;
            g_TextureDesc.printClutTint = 0x1e0;
            g_TextureDesc.screenX = 0x99 - g_ScreenOffsetX;
            g_TextureDesc.screenY = g_MessageScreenY + 0x1e;
            AddTintSprite(&g_TextureDesc, 2);
            message_render_chars();
            return;
        }
        break;

    // === State 3: Post-newline delay ===
    case 3:
        g_MessageCharTimer = g_MessageCharTimer - 1;
        if (g_MessageCharTimer == 0) {
            g_MessageStateCounter = 1;
            g_MessagePtr = g_MessageCurrentPtr;
            g_MessageClutBase = g_MessageClutCopy;
            g_MessageCharTimer = g_MessageCharDelay << (g_bGameActive == 0);
            message_render_chars();
            return;
        }
        break;

    // === State 4: Yes/No prompt ===
    case 4:
        if ((g_PlayerDpadPressed & 0x4000) == 0) {
            if ((g_RawPadHeld & (0x2000 | 0x8000)) != 0) {
                g_menu_choice_id = g_menu_choice_id ^ 1;
                g_MessageCharTimer = 0;
            }
            g_MessageCharTimer = g_MessageCharTimer - 1;
            // Blink cursor
            if ((((unsigned int)g_MessageCharTimer & (0x18 << (g_bGameActive == 0)))) != 0) {
                g_TextureDesc.flags = 0x40;
                if ((g_menu_choice_id & 1) == 0) {
                    screenX = 0xd0;
                } else {
                    screenX = 0xf8;
                }
                g_TextureDesc.width = 8;
                g_TextureDesc.height = 0xe;
                g_TextureDesc.texU = 0x10;
                g_TextureDesc.texV = 0x1c;
                g_TextureDesc.depth = 0x1e;
                g_TextureDesc.unk10 = 0x100;
                g_TextureDesc.printClutTint = 0x1e0;
                g_TextureDesc.screenX = screenX - g_ScreenOffsetX;
                g_TextureDesc.screenY = g_MessageScreenY + 0x10;

                if ((g_stageId == 3) && (g_roomId == 0x11) && (g_roomCameraId == 0x04)) {
                    fade = 0;
                } else {
                    fade = 2;
                }
                AddTintSprite(&g_TextureDesc, fade);
            }
            sprintf(PRINT_TEXT_BUFFER, "Yes No");
            PrintText8x14(0xd8, g_ScreenOffsetY + g_MessageScreenY + 0x10, 0, 0);
            message_render_chars();
            return;
        }

        // Confirm pressed: dismiss message
        g_menu_choice_id = g_menu_choice_id & 0x7f;
        g_message_flags = g_messageFlagsBackup;
        message_dismiss_handler();
        return;

    // === State 5: Waiting for player input to dismiss ===
    case 5:
        if ((g_PlayerDpadPressed & 0xC000) != 0) {
            g_menu_choice_id = g_menu_choice_id & 0x7f;
            if ((g_message_flags & 1) == 0) {
                g_PlayerDpadHeld = g_PlayerDpadHeld & 0xf000;
                g_PlayerDpadHeldPrev = g_PlayerDpadHeldPrev & 0xf000;
            }
            g_message_flags = g_messageFlagsBackup;
            return;
        }
        break;

    // === State 6: Auto-dismiss after timeout ===
    case 6:
        g_MessageCharTimer = g_MessageCharTimer - 1;
        if (g_MessageCharTimer == 0) {
            g_menu_choice_id = g_menu_choice_id & 0x7f;
            if ((g_message_flags & 1) == 0) {
                g_PlayerDpadHeld = g_PlayerDpadHeld & 0xf000;
                g_PlayerDpadHeldPrev = g_PlayerDpadHeldPrev & 0xf000;
            }
            g_message_flags = g_messageFlagsBackup;
            return;
        }
        break;
    }

state1_default:
    message_render_chars();
}

void SetSubpixelOffset(int x, int y)
{
    g_SubpixelOffsetX = x;
    g_SubpixelOffsetY = y;
}

void CenterScreenOrigin(void)
{
    SetSubpixelOffset(160, 120);
    g_ScreenOffsetX = 160;
    g_ScreenOffsetY = 120;
}

void setMenuScreenOffset(int w, int h, int x, int y, int mode)
{
    g_ScreenOffsetX = 0;
    g_ScreenOffsetY = 0;
}

// ============================================================================
// clear_textures (0x00470a00)
// Clears texture page entries across all active slots.
// Original: calls TexturePage_DeleteSet for slots 0-11, then
// delete_texture_set_secondary for slots 12-26.
// ============================================================================
void clear_textures(void)
{
    for (int i = 0; i < 4; i++) {
        TexturePage_DeleteSet(i);
    }
    for (int i = 4; i < 12; i++) {
        TexturePage_DeleteSet(i);
    }
    for (int i = 12; i < 27; i++) {
        delete_texture_set_secondary(i);
    }
}

// ============================================================================
// FUN_0046c230 / FUN_0046c280 — Marni execute buffer stubs (return 0)
// ============================================================================
int FUN_0046c230(void* data)
{
    return 0;
}

int FUN_0046c280(int id)
{
    return 0;
}

// ============================================================================
// SetScreenReady (0x00497340)
// Sets the MarniDirect3D screen-ready flag and clears the debug color override.
// Original: writes to CMarniDirect3D field_0x2ec and field_0x2f0
// ============================================================================
void SetScreenReady(int param)
{
    g_MarniScreenReady = param;
    g_MarniScreenColor = 0;
}

// ============================================================================
// SetScreenReadyWithDebugColor (0x00497360)
// Enables screen-ready and sets a packed RGB debug color override.
// Original: writes to CMarniDirect3D field_0x2ec=1 and field_0x2f0=(r<<16|g<<8|b)
// ============================================================================
void SetScreenReadyWithDebugColor(int r, int g, int b)
{
    g_MarniScreenReady = 1;
    g_MarniScreenColor = ((unsigned int)r << 16) | ((unsigned int)g << 8) | (unsigned int)b;
}

// ============================================================================
// FUN_00401020 (0x00401020)
// Resets screen offset to center, disables screen-ready, resets subpixel
// params, and rebuilds title background sprites.
// ============================================================================
void ResetScreenAndRebuildSprites(int param)
{
    SetScreenOffset(160, 120);
    SetScreenReady(0);
    Display_SetParams(0, 0);
    FUN_00470a90();
}

// ============================================================================
// StMask (0x00497670)
// Controls frame presentation gating.
//   param_1 != 0 → immediately enable screen present (g_ScreenAccessReady = 1)
//   param_1 == 0 → disable screen present and set countdown timer
// ============================================================================
void StMask(int param_1, int param_2)
{
    if (param_1 != 0) {
        g_ScreenAccessReady = 1;
        return;
    }
    g_ScreenAccessReady = 0;
    g_ScreenAccessCountdown = (char)param_2;
}

// ============================================================================
// FUN_00470a90 (0x00470a90)
// Builds title background sprite commands for the display image.
// In the original, this splits the background into two halves (left/right)
// and inserts them into the ordering table at depths 0xFFE and 0xFFF
// via vtable calls on the CMarniDirect3D object.
// Modern impl: background rendering is already handled by OT_InsertPrimitive
// in FrameRateGovernor via FUN_0040a8f0, so this is a no-op in the modern pipeline.
// ============================================================================
void FUN_00470a90(void)
{
}
