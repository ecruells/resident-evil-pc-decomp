// Rendering.cpp - Frame rendering, present, text output, sprite drawing
// All functions decompiled from Ghidra with original addresses
#include "../Globals.h"
#include "../marni/MarniSystem.h"
#include "../marni/PSXTexture.h"
#include "SpriteRenderer.h"
#include <cstdlib>

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
// PrintText8x8 (0x00455420)
// ============================================================================
void PrintText8x8(short x, short y, unsigned char color, char shadow)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (pD3D == NULL) return;
    if (pD3D->m_pFontSRV == NULL || pD3D->m_FontTexWidth <= 0 || pD3D->m_FontTexHeight <= 0) return;

    unsigned char brightness = color >> 4;
    if (brightness == 0) brightness = 2;

    g_TextureBuffer = ((shadow != 0) ? 0x40000000U : 0U) + 0x40;

    g_TextureVramX = 8;
    g_TextureVramY = 8;
    g_texPrintState.vramWidth = 8;
    g_texPrintState.vramHeight = 8;
    g_TextureDepth = 30;

    g_TexturePrintX = x - g_ScreenOffsetX;
    g_TexturePrintY = y - g_ScreenOffsetY;

    g_PrintTintR = 128;
    g_PrintTintG = 128;
    g_PrintTintB = 128;
    g_PrintTintMode = 0;
    g_PrintTintFlagB = 0;
    g_PrintTintFlagA = 0;

    unsigned char clutTint = color & 0xF;
    if (shadow != 0) clutTint += 8;

    g_PrintTintA = 0x100;
    g_PrintClutTint = clutTint + 0x1E0;

    for (int i = 0; PRINT_TEXT_BUFFER[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)PRINT_TEXT_BUFFER[i];

        if (ch == ' ') {
            g_TexturePrintX += 8;
            continue;
        }

        unsigned char idx = ch - 0x20U;
        g_TextureVramX = (unsigned char)(idx * 8);
        g_TextureVramY = (unsigned char)((idx & 0xE3) >> 2);

        AddTintSprite((TextureDesc*)&g_texPrintState, brightness);

        g_TexturePrintX += 8;
    }
}

// ============================================================================
// PrintFormattedText (0x00455190)
// ============================================================================
void PrintFormattedText(short x, short y, unsigned char color, unsigned char* data)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (pD3D == NULL) return;
    if (pD3D->m_pFontSRV == NULL || pD3D->m_FontTexWidth <= 0 || pD3D->m_FontTexHeight <= 0) return;
    if (data == NULL) return;

    unsigned char brightness = color >> 4;
    if (brightness == 0) brightness = 2;

    g_TextureVramY = 14;
    g_TextureBuffer = 0x40;
    g_TexturePrintX = x - g_ScreenOffsetX;
    g_PrintTintA = 0x100;
    g_TexturePrintY = y - g_ScreenOffsetY;
    g_PrintClutTint = (color & 0xF) + 0x1E0;
    g_TextureVramX = 8;
    g_PrintTintR = 128;
    g_PrintTintG = 128;
    g_PrintTintB = 128;
    g_PrintTintMode = 0;
    g_PrintTintFlagB = 0;
    g_PrintTintFlagA = 0;

    g_texPrintState.vramWidth = 8;
    g_texPrintState.vramHeight = 14;

    unsigned char row = 0;
    unsigned char texDepth = 0x1E;
    unsigned char ch = *data;
    while (ch != 1 && ch != 7) {
        ch = *data;

        if (ch == 0) {
            g_TexturePrintX += 8;
        } else {
            switch (ch) {
            case 1:
            case 7:
                return;

            case 0xF8:
                data++;
                row = *data / 0x12 + 0xF;
                texDepth = 0x1E;
                ch = *data;
                goto render;

            case 0xF9:
                data++;
                row = *data / 0x12;
                texDepth = 0x1F;
                ch = *data;
                goto render;

            case 0xFA:
                data++;
                row = *data / 0x12 + 14;
                texDepth = 0x1F;
                ch = *data;
                goto render;

            case 0xFB:
                break;

            case 0xFF:
                g_TexturePrintX += 4;
                break;

            default:
                row = ch / 0x12 + 2;
                texDepth = 0x1E;
                goto render;
            }
        }

        data++;
        ch = *data;
        continue;

render:
        g_TextureDepth = texDepth;
        g_TextureVramY = row * 14;
        g_TextureVramX = (ch % 0x12) * 8;
        AddTintSprite((TextureDesc*)&g_texPrintState, brightness);

        g_TexturePrintX += 8;
        data++;
        ch = *data;
    }
}

// ============================================================================
// PrintText8x14 (0x00455520)
// ============================================================================
void PrintText8x14(short x, short y, unsigned char color, char flags)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (pD3D == NULL) return;

    if (pD3D->m_pFontSRV == NULL || pD3D->m_FontTexWidth <= 0 || pD3D->m_FontTexHeight <= 0) {
        OutputDebugStringA("[TEXT] Font not loaded yet, skipping\n");
        return;
    }

    unsigned char brightness;
    if ((color & 0x80) == 0) {
        brightness = color >> 4;
        if (brightness == 0) brightness = 2;
    } else {
        brightness = 30;
    }

    g_TextureBuffer = ((flags != 0) ? 0x40000000U : 0U) + 0x40;

    g_TextureVramX = 8;
    g_TextureVramY = 14;
    g_texPrintState.vramWidth = 8;
    g_texPrintState.vramHeight = 14;
    g_TextureDepth = 0x1E;

    g_TexturePrintX = x - g_ScreenOffsetX;
    g_TexturePrintY = y - g_ScreenOffsetY;

    g_PrintTintR = 0x80;
    g_PrintTintG = 0x80;
    g_PrintTintB = 0x80;
    g_PrintTintA = 0x100;
    g_PrintTintMode = 0;
    g_PrintTintFlagA = 0;
    g_PrintTintFlagB = 0;

    g_PrintClutTint = (color & 0xF) + 0x1E0;

    for (int i = 0; PRINT_TEXT_BUFFER[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)PRINT_TEXT_BUFFER[i];

        if (ch == ' ') {
            g_TexturePrintX += 8;
            continue;
        }

        if (ch == '(')      { g_TextureVramX = 56;  g_TextureVramY = 224; }
        else if (ch == ')') { g_TextureVramX = 70;  g_TextureVramY = 224; }
        else                { g_TextureVramX = (ch % 18) * 8; g_TextureVramY = (ch / 18) * 14; }

        unsigned char finalBrightness = brightness;
        if ((g_stageId == 3) && (g_roomId == 17) && (g_roomCameraId == 4)) {
            finalBrightness = 0;
        }

        AddTintSprite((TextureDesc*)&g_texPrintState, finalBrightness);

        g_TexturePrintX += 8;
    }
}

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

    float gameX = (float)(g_TexturePrintX + g_ScreenOffsetX);
    float gameY = (float)(g_TexturePrintY + g_ScreenOffsetY);

    float scaleX = (float)pD3D->m_width / 320.0f;
    float scaleY = (float)pD3D->m_height / 240.0f;

    float screenX = gameX * scaleX;
    float screenY = gameY * scaleY;
    float charW = (float)g_texPrintState.vramWidth * scaleX;
    float charH = (float)g_texPrintState.vramHeight * scaleY;

    float texW = (float)pD3D->m_FontTexWidth;
    float texH = (float)pD3D->m_FontTexHeight;
    float u0 = (float)g_TextureVramX / texW;
    float v0 = (float)g_TextureVramY / texH;
    float u1 = (float)(g_TextureVramX + g_texPrintState.vramWidth) / texW;
    float v1 = (float)(g_TextureVramY + g_texPrintState.vramHeight) / texH;

    // Calculate RGB from tint values — cast to unsigned int first to avoid overflow
    unsigned int r = ((unsigned int)(g_PrintTintR & 0xFF)) * 2; if (r > 255) r = 255;
    unsigned int g = ((unsigned int)(g_PrintTintG & 0xFF)) * 2; if (g > 255) g = 255;
    unsigned int b = ((unsigned int)(g_PrintTintB & 0xFF)) * 2; if (b > 255) b = 255;

    DWORD color;
    if (g_PrintTintR == 0 && g_PrintTintG == 0 && g_PrintTintB == 0) {
        // Shadow pass: semi-transparent black (brightness controls alpha)
        unsigned int a = ((unsigned int)brightness * 255) / 30;
        if (a > 255) a = 255;
        color = (a << 24) | (0 << 16) | (0 << 8) | 0;
    } else {
        // Text pass: opaque color, brightness dims RGB (original PS1 CLUT-based dimming)
        unsigned int brightnessScale = ((unsigned int)brightness * 255) / 30;
        if (brightnessScale > 255) brightnessScale = 255;
        if (brightness <= 2) brightnessScale = 17;
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

    if (g_ScreenAccessCheck != 0) {
        g_ScreenAccessCheck--;
        if (g_ScreenAccessCheck == 0) g_ScreenAccessReady = 1;
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
        unsigned char a = (px & 0x8000) ? 0x80 : 0xFF;
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
// Sets subpixel rendering offset. In the original, this also set
// g_ScreenOffsetX/Y, but in the modern port g_ScreenOffsetX/Y is managed
// separately by CenterScreenOrigin/setMenuScreenOffset because
// FlushSpriteCommands already applies a +160/+120 centering offset.
// ============================================================================
void SetScreenOffset(int x, int y)
{
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

    // Apply shake directly to screen offset (g_ScreenOffsetX/Y)
    // In the original: SetScreenOffset(shakeX + 0xa0, shakeY + 0x78) which set both
    // g_ScreenOffsetX and g_SubpixelOffsetX. In the modern port, SetScreenOffset only
    // sets g_SubpixelOffsetX, so we set g_ScreenOffsetX/Y directly for the camera shake.
    g_ScreenOffsetX = g_ScreenShakeOffsetX;
    g_ScreenOffsetY = g_ScreenShakeOffsetY;
    SetScreenOffset(g_ScreenShakeOffsetX + 0xa0, g_ScreenShakeOffsetY + 0x78);
}

void FUN_004557b0(void) { /* stub */ }

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
// FUN_00497340 (0x00497340)
// Sets the MarniDirect3D screen-ready flag and clears the debug color override.
// Original: writes to CMarniDirect3D field_0x2ec and field_0x2f0
// ============================================================================
void FUN_00497340(int param)
{
    g_MarniScreenReady = param;
    g_MarniScreenColor = 0;
}

// ============================================================================
// FUN_00497360 (0x00497360)
// Enables screen-ready and sets a packed RGB debug color override.
// Original: writes to CMarniDirect3D field_0x2ec=1 and field_0x2f0=(r<<16|g<<8|b)
// ============================================================================
void FUN_00497360(int r, int g, int b)
{
    g_MarniScreenReady = 1;
    g_MarniScreenColor = ((unsigned int)r << 16) | ((unsigned int)g << 8) | (unsigned int)b;
}

// ============================================================================
// FUN_00401020 (0x00401020)
// Resets screen offset to center, disables screen-ready, resets subpixel
// params, and rebuilds title background sprites.
// ============================================================================
void FUN_00401020(int param)
{
    SetScreenOffset(160, 120);
    FUN_00497340(0);
    Display_SetParams(0, 0);
    FUN_00470a90();
}

// ============================================================================
// FUN_0045ab60 (0x0045ab60)
// Applies screen shake offsets to display params (if screen shake active),
// enables screen-ready, and rebuilds title background sprites.
// ============================================================================
void FUN_0045ab60(void)
{
    int subY, subX;

    if ((((unsigned char)g_InputFlags & 0x02) == 0) ||
        (((g_main_state_flags >> 8) & 0xFF) != 0)) {
        subY = 2;
        subX = 2;
    } else {
        subY = (int)g_ScreenShakeOffsetY + 2;
        subX = (int)g_ScreenShakeOffsetX + 2;
    }

    Display_SetParams(subX, subY);
    FUN_00497340(1);
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
