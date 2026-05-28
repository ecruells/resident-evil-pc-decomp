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

    if (shadow != 0) {
        g_TexturePrintX = (x - g_ScreenOffsetX) + 1;
        g_TexturePrintY = (y - g_ScreenOffsetY) + 1;
        g_PrintTintR = 0;
        g_PrintTintG = 0;
        g_PrintTintB = 0;

        for (int i = 0; PRINT_TEXT_BUFFER[i] != '\0'; i++) {
            unsigned char ch = (unsigned char)PRINT_TEXT_BUFFER[i];
            if (ch == ' ') continue;

            unsigned char idx = ch - 0x20U;
            g_TextureVramX = (unsigned char)(idx * 8);
            g_TextureVramY = (unsigned char)((idx & 0xE3) >> 2);

            AddTintSprite((TextureDesc*)&g_texPrintState, 10);
            g_TexturePrintX += 8;
        }
    }

    g_TexturePrintX = x - g_ScreenOffsetX;
    g_TexturePrintY = y - g_ScreenOffsetY;
    g_PrintTintR = 128;
    g_PrintTintG = 128;
    g_PrintTintB = 128;

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

    if (flags != 0) {
        g_TexturePrintX = (x - g_ScreenOffsetX) + 1;
        g_TexturePrintY = (y - g_ScreenOffsetY) + 1;
        g_PrintTintR = 0;
        g_PrintTintG = 0;
        g_PrintTintB = 0;

        for (int i = 0; PRINT_TEXT_BUFFER[i] != '\0'; i++) {
            unsigned char ch = (unsigned char)PRINT_TEXT_BUFFER[i];
            if (ch == ' ') continue;

            if (ch == '(')      { g_TextureVramX = 56;  g_TextureVramY = 224; }
            else if (ch == ')') { g_TextureVramX = 70;  g_TextureVramY = 224; }
            else                { g_TextureVramX = (ch % 18) * 8; g_TextureVramY = (ch / 18) * 14; }

            AddTintSprite((TextureDesc*)&g_texPrintState, 10);
            g_TexturePrintX += 8;
        }
    }

    g_TexturePrintX = x - g_ScreenOffsetX;
    g_TexturePrintY = y - g_ScreenOffsetY;
    g_PrintTintR = 0x80;
    g_PrintTintG = 0x80;
    g_PrintTintB = 0x80;

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
        if ((g_STAGE_ID == 3) && (g_ROOM_ID == 17) && (g_roomCamera_id == 4)) {
            finalBrightness = 0;
        }

        AddTintSprite((TextureDesc*)&g_texPrintState, finalBrightness);

        g_TexturePrintX += 8;
    }
}

// ============================================================================
// AddTintSprite (0x0046e0a0)
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

    unsigned char a = (unsigned char)((unsigned int)brightness * 255 / 30);
    if (brightness <= 2) a = 17;
    unsigned char r = (unsigned char)((g_PrintTintR & 0xFF) * 2); if (r > 255) r = 255;
    unsigned char g = (unsigned char)((g_PrintTintG & 0xFF) * 2); if (g > 255) g = 255;
    unsigned char b = (unsigned char)((g_PrintTintB & 0xFF) * 2); if (b > 255) b = 255;
    DWORD color = (a << 24) | (r << 16) | (g << 8) | b;

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

    g_pendingSpriteCount++;
    return 1;
}

// ============================================================================
// draw_rect (0x00470350)
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

    int alpha = 255 - blend;
    if (alpha < 0) alpha = 0;
    if (alpha > 255) alpha = 255;
    unsigned char a = (unsigned char)alpha;

    DWORD color = (a << 24) | (r << 16) | (g << 8) | b;

    ID3D11ShaderResourceView* srv = NULL;
    if (rect->textureId == 0) {
        srv = pD3D->m_pWhiteSRV;
    } else {
        int shiftedSlot = (rect->textureId & 0xFF) + 0xF;
        if (shiftedSlot >= 0 && shiftedSlot < 256) {
            srv = g_TexturePageSRV[shiftedSlot];
        }
    }

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

    g_pendingSpriteCount++;

    (void)flags;
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
            if (g_bFrameRateUnlocked) {
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

        if (!g_bFrameRateUnlocked) {
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

            for (int i = 0; i < g_pendingSpriteCount; i++) {
                if (g_pendingSprites[i].valid) {
                    MarniDrawSprite(
                        g_pendingSprites[i].x, g_pendingSprites[i].y,
                        g_pendingSprites[i].w, g_pendingSprites[i].h,
                        g_pendingSprites[i].u0, g_pendingSprites[i].v0,
                        g_pendingSprites[i].u1, g_pendingSprites[i].v1,
                        g_pendingSprites[i].color,
                        g_pendingSprites[i].srv);
                }
            }

            FlushSpriteCommands();

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
// Screen effect functions
// ============================================================================
void FUN_00442150(int status_flags) {
    if (status_flags != 0) { g_bFrameRateUnlocked = TRUE; }
    else { g_bFrameRateUnlocked = FALSE; }
}

void ResetScreenPanning(void) { g_ScreenOffsetX = 0; g_ScreenOffsetY = 0; }

void ApplyScreenShake(void) {
    g_ScreenOffsetX = (rand() % 5) - 2;
    g_ScreenOffsetY = (rand() % 5) - 2;
}

void FUN_004557b0(void) { g_menu_choice_id &= ~0x80; }

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
