#include "SpriteRenderer.h"
#include "../Globals.h"
#include "../marni/MarniSystem.h"
#include "../marni/PSXTexture.h"
#include "../marni/MarniBits.h"
#include <cstdio>

// ============================================================================
// Global variables
// ============================================================================
TextureDraw g_SpriteCommandBuffer[MAX_SPRITE_COMMANDS];
OTEntry g_OT[MAX_OT_ENTRIES];
int g_RenderBufferIndex      = 0;
int g_RenderDisableFlags     = 0;
int g_SubpixelOffsetX        = 0;
int g_SubpixelOffsetY        = 0;
int g_MaxFadeValue           = 4095;
int g_DepthSortOverride      = 0;
float g_ColorScaleFactor     = 1.0f / 255.0f;

// Page width factors indexed by flags bits 22-23 (0x004c2d78)
static const int g_PageWidthFactor[4] = { 1, 2, 4, 8 };
// Variant data (0x004c2d64)
static const int g_VariantData[5] = { 0, 1, 2, 4, 8 };

// ============================================================================
// BuildSpriteRenderFlags (0x0046d960)
// ============================================================================
void BuildSpriteRenderFlags(unsigned int textureFlags, unsigned int* outFlags) {
    unsigned int flags = 0;
    if (textureFlags & 0x400000) flags |= 0x20;
    if (textureFlags & 0x800000) flags |= 0x10;
    *outFlags = flags;
}

// ============================================================================
// GetTextureVariant (0x0046d940)
// ============================================================================
int GetTextureVariant(unsigned int textureFlags) {
    if (textureFlags & 0x40000000) {
        return ((textureFlags & 0x30000000) >> 28) + 1;
    }
    return 0;
}

// ============================================================================
// SpriteQueue_Reset (0x0046d990)
// ============================================================================
void SpriteQueue_Reset(void) {
    g_SpriteQueueCount = 0;
    g_OTIndex = 0;
}

// ============================================================================
// FlushSpriteCommands
// Converts TextureDraw entries to D3D11 draw calls
// ============================================================================
void FlushSpriteCommands(void) {
    if (g_SpriteQueueCount == 0) return;

    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    float scaleX = pD3D ? (float)pD3D->m_width / 320.0f : 2.0f;
    float scaleY = pD3D ? (float)pD3D->m_height / 240.0f : 2.0f;

    for (int i = 0; i < g_SpriteQueueCount; i++) {
        TextureDraw* cmd = &g_SpriteCommandBuffer[i];
        if (cmd->type != 10) continue;

        float x = ((float)cmd->x0 + 160.0f) * scaleX;
        float y = ((float)cmd->y0 + 120.0f) * scaleY;
        float w = (float)(cmd->x1 - cmd->x0 + 1) * scaleX;
        float h = (float)(cmd->y1 - cmd->y0 + 1) * scaleY;

        int cr = (int)(cmd->r * 255.0f);
        int cg = (int)(cmd->g * 255.0f);
        int cb = (int)(cmd->b * 255.0f);
        if (cr > 255) cr = 255; if (cr < 0) cr = 0;
        if (cg > 255) cg = 255; if (cg < 0) cg = 0;
        if (cb > 255) cb = 255; if (cb < 0) cb = 0;

        // unk1c carries alpha when in (0,1) range, or render flags otherwise
        float alpha = 1.0f;
        if (cmd->unk1c > 0.0f && cmd->unk1c < 1.0f) {
            alpha = cmd->unk1c;
        }
        int ca = (int)(alpha * 255.0f);
        if (ca > 255) ca = 255; if (ca < 0) ca = 0;
        DWORD color = (ca << 24) | (cr << 16) | (cg << 8) | cb;

        int texSlot = cmd->extraFlags;
        ID3D11ShaderResourceView* srv = NULL;
        float pageW = 256.0f;
        float pageH = 256.0f;
        if (texSlot >= 0 && texSlot < 256) {
            srv = g_TexturePageSRV[texSlot];
            if (g_TexturePageWidth[texSlot] > 0) pageW = (float)g_TexturePageWidth[texSlot];
            if (g_TexturePageHeight[texSlot] > 0) pageH = (float)g_TexturePageHeight[texSlot];
        }
        if (srv == NULL) {
            texSlot = cmd->texturePage;
            if (texSlot >= 0 && texSlot < 256) {
                srv = g_TexturePageSRV[texSlot];
                if (g_TexturePageWidth[texSlot] > 0) pageW = (float)g_TexturePageWidth[texSlot];
                if (g_TexturePageHeight[texSlot] > 0) pageH = (float)g_TexturePageHeight[texSlot];
            }
        }
        if (srv == NULL) continue;

        float u0 = (float)cmd->u0 / pageW;
        float v0 = (float)cmd->v0 / pageH;
        float u1 = (float)cmd->u1 / pageW;
        float v1 = (float)cmd->v1 / pageH;

        MarniDrawSprite(x, y, w, h, u0, v0, u1, v1, color, srv);
    }

    g_SpriteQueueCount = 0;
}

// ============================================================================
// draw_texture (0x0046e410)
// ============================================================================
int draw_texture(TextureDesc* texture, unsigned short depth) {
    if (g_SpriteQueueCount >= MAX_SPRITE_COMMANDS - 1) return 0;

    int pageW = g_PageWidthFactor[(texture->flags >> 22) & 3];

    TextureDraw* cmd = &g_SpriteCommandBuffer[g_SpriteQueueCount];
    cmd->type = 10;

    unsigned int flags;
    BuildSpriteRenderFlags(texture->flags, &flags);
    int variant = GetTextureVariant(texture->flags);
    cmd->unk1c = (float)((variant != 0) ? (flags | 8) : flags);

    cmd->r = (float)texture->colorMulR * g_ColorScaleFactor;
    cmd->g = (float)texture->colorMulG * g_ColorScaleFactor;
    cmd->b = (float)texture->colorMulB * g_ColorScaleFactor;

    if (variant == 0) {
        cmd->texturePage = 0;
    } else {
        cmd->texturePage = (int)((float)g_VariantData[variant] * 0.003921569f);
    }

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

    cmd->extraFlags = 0;

    g_SpriteQueueCount++;
    return 1;
}

// ============================================================================
// AddSprite (0x0046ddc0)
// ============================================================================
int AddSprite(TextureDesc* texture, short depth, int tpage, int fade) {
    if (g_SpriteQueueCount >= MAX_SPRITE_COMMANDS - 1) return 0;

    TextureDraw* cmd = &g_SpriteCommandBuffer[g_SpriteQueueCount];
    cmd->type = 10;

    unsigned int flags;
    BuildSpriteRenderFlags(texture->flags, &flags);
    int variant = GetTextureVariant(texture->flags);
    cmd->unk1c = (float)((variant != 0) ? (flags | 8) : flags);

    cmd->r = 1.0f;
    cmd->g = 1.0f;
    cmd->b = 1.0f;
    cmd->texturePage = 0;

    short sx = texture->screenX + g_ScreenOffsetX;
    short sy = texture->screenY + g_ScreenOffsetY;
    cmd->x0 = sx - texture->pivotX;
    cmd->y0 = sy - texture->pivotY;
    cmd->x1 = (texture->width - texture->pivotX) + sx - 1;
    cmd->y1 = (texture->height - texture->pivotY) + sy - 1;

    cmd->depthSort = (depth == 0) ? 550 : (fade * 16);

    cmd->u0 = (unsigned short)texture->texU;
    cmd->v0 = (unsigned short)texture->texV;
    cmd->u1 = cmd->u0 + texture->width - 1;
    cmd->v1 = cmd->v0 + texture->height - 1;

    cmd->extraFlags = tpage + 4;

    g_SpriteQueueCount++;
    return 1;
}

// ============================================================================
// SubmitEffectSprite (0x0046d950)
// ============================================================================
int SubmitEffectSprite(TextureDesc* texture, int depth, int textureId,
                       unsigned char r, unsigned char g, unsigned char b,
                       int scaleX, int scaleY, int blendMode, short brightness) {
    if (g_SpriteQueueCount >= MAX_SPRITE_COMMANDS - 1) return 0;

    bool useSubpixel = !(texture->scaleX == 0x1000 && texture->scaleY == 0x1000);

    short sx, sy;
    if (useSubpixel) {
        sx = texture->screenX + (short)g_SubpixelOffsetX;
        sy = texture->screenY + (short)g_SubpixelOffsetY;
    } else {
        sx = texture->screenX + g_ScreenOffsetX;
        sy = texture->screenY + g_ScreenOffsetY;
    }

    TextureDraw* cmd = &g_SpriteCommandBuffer[g_SpriteQueueCount];
    cmd->type = 10;

    unsigned int flags;
    BuildSpriteRenderFlags(texture->flags, &flags);
    int variant = GetTextureVariant(texture->flags);
    cmd->unk1c = (float)((variant != 0) ? (flags | 8) : flags);

    cmd->r = (float)r * (float)texture->colorMulR * g_ColorScaleFactor;
    cmd->g = (float)g * (float)texture->colorMulG * g_ColorScaleFactor;
    cmd->b = (float)b * (float)texture->colorMulB * g_ColorScaleFactor;

    cmd->texturePage = (int)((float)blendMode * g_ColorScaleFactor);
    cmd->extraFlags = textureId;

    if (useSubpixel) {
        int iVar4 = (int)texture->pivotX * (int)texture->scaleX;
        cmd->x0 = sx - (short)((iVar4 + (iVar4 >> 0x1F & 0xFFF)) >> 12);
        iVar4 = (int)texture->pivotY * (int)texture->scaleY;
        cmd->y0 = sy - (short)((iVar4 + (iVar4 >> 0x1F & 0xFFF)) >> 12);
        iVar4 = ((unsigned int)texture->width - (int)texture->pivotX) * (int)texture->scaleX;
        cmd->x1 = (short)((iVar4 + (iVar4 >> 0x1F & 0xFFF)) >> 12) + sx;
        iVar4 = ((unsigned int)texture->height - (int)texture->pivotY) * (int)texture->scaleY;
        cmd->y1 = (short)((iVar4 + (iVar4 >> 0x1F & 0xFFF)) >> 12) + sy;
        if (cmd->x0 < cmd->x1) cmd->x1 = cmd->x1 - 1;
        if (cmd->y1 > cmd->y0) cmd->y1 = cmd->y1 - 1;
    } else {
        cmd->x0 = sx - texture->pivotX;
        cmd->y0 = sy - texture->pivotY;
        cmd->x1 = (texture->width - texture->pivotX) + sx - 1;
        cmd->y1 = (texture->height - texture->pivotY) + sy - 1;
    }

    cmd->depthSort = ((unsigned int)(depth & 0xFFFF)) * 0x40 - scaleY;

    cmd->u0 = (unsigned short)texture->texU;
    cmd->v0 = (unsigned short)texture->texV;
    cmd->u1 = cmd->u0 + texture->width - 1;
    cmd->v1 = cmd->v0 + texture->height - 1;

    g_SpriteQueueCount++;
    return 1;
}

// ============================================================================
// AddFadePoly (0x0046fea0) - simplified for D3D11 port
// ============================================================================
int AddFadePoly(unsigned short alpha, int tpage, int u, int v, int clut,
                unsigned char* rgb, int x, int y, int z, unsigned short forceAlpha) {
    if (g_SpriteQueueCount >= MAX_SPRITE_COMMANDS - 1) return 0;

    TextureDraw* cmd = &g_SpriteCommandBuffer[g_SpriteQueueCount];
    cmd->type = 10;

    float a = (float)alpha / (float)g_MaxFadeValue;

    cmd->r = (float)rgb[0] * g_ColorScaleFactor;
    cmd->g = (float)rgb[1] * g_ColorScaleFactor;
    cmd->b = (float)rgb[2] * g_ColorScaleFactor;
    cmd->unk1c = a;
    cmd->texturePage = 0;

    cmd->x0 = (short)x;
    cmd->y0 = (short)y;
    cmd->x1 = (short)(x + 64);
    cmd->y1 = (short)(y + 64);
    cmd->u0 = (short)u;
    cmd->v0 = (short)v;
    cmd->u1 = (short)(u + 64);
    cmd->v1 = (short)(v + 64);
    cmd->depthSort = z;
    cmd->extraFlags = tpage;

    g_SpriteQueueCount++;
    return 1;
}

// ============================================================================
// DrawPrim_SpriteLarge - simplified for D3D11 port
// ============================================================================
int DrawPrim_SpriteLarge(int* params, unsigned short alpha, int tpage,
                         unsigned int u, unsigned int v, unsigned int clut) {
    if (g_SpriteQueueCount >= MAX_SPRITE_COMMANDS - 1) return 0;

    TextureDraw* cmd = &g_SpriteCommandBuffer[g_SpriteQueueCount];
    cmd->type = 10;
    cmd->r = (float)params[0] * g_ColorScaleFactor;
    cmd->g = (float)params[1] * g_ColorScaleFactor;
    cmd->b = (float)params[2] * g_ColorScaleFactor;
    cmd->unk1c = 1.0f;
    cmd->texturePage = 0;
    cmd->x0 = 0;
    cmd->y0 = 0;
    cmd->x1 = 320;
    cmd->y1 = 240;
    cmd->u0 = (short)u;
    cmd->v0 = (short)v;
    cmd->u1 = (short)(u + 320);
    cmd->v1 = (short)(v + 240);
    cmd->depthSort = (int)alpha;
    cmd->extraFlags = tpage;

    g_SpriteQueueCount++;
    return 1;
}

// ============================================================================
// TexturePage functions (unchanged)
// ============================================================================
void TexturePage_Load(int slotIndex, void* imageData) {
    ProcessTextureImage(imageData, (short)slotIndex, 0, slotIndex);
}

void TexturePage_ClearAll(void) {
    for (int i = 0; i < 256; i++) {
        if (g_TexturePageSRV[i] != NULL) {
            g_TexturePageSRV[i]->Release();
            g_TexturePageSRV[i] = NULL;
        }
        g_TexturePageWidth[i] = 0;
        g_TexturePageHeight[i] = 0;
        g_TexturePageTable_DAT[i] = 0;
    }
}

void TexturePage_Create(int slotIndex) {
    int mode = (slotIndex < 2) ? 2 : 0x22;
    int handle = create_texture_page(NULL, mode);
    if (handle != 0 && slotIndex >= 0 && slotIndex < 256) {
        g_TexturePageTable_DAT[slotIndex] = (DWORD)handle;
    }
}

void TexturePage_SetupFull(void* imageData, short bankID, short pageOffset, int slotIndex) {
    ProcessTextureImage(imageData, bankID, pageOffset, slotIndex);
}

void TexturePage_Refresh(int slotIndex, int mode) {
    int handle = create_texture_page(NULL, (mode == 0) ? 2 : 1);
    if (handle != 0 && slotIndex >= 0 && slotIndex < 256) {
        g_TexturePageTable_DAT[slotIndex] = (DWORD)handle;
    }
}

void TexturePage_RefreshCLUT(int slotIndex, int mode, int clutIndex) {
    int cmode = (mode != 0) ? 1 : 2;
    int handle = create_texture_page(NULL, cmode);
    if (handle != 0 && slotIndex >= 0 && slotIndex < 256) {
        g_TexturePageTable_DAT[slotIndex] = (DWORD)handle;
    }
}

void TexturePage_DeleteSet(int slotIndex) {
    for (int i = 0; i < 8; i++) {
        int pageIdx = slotIndex * 8 + i;
        if (pageIdx >= 0 && pageIdx < 256) {
            DWORD handle = g_TexturePageTable_DAT[pageIdx];
            if (handle != 0) {
                destroy_texture_page(handle);
                g_TexturePageTable_DAT[pageIdx] = 0;
            }
            if (g_TexturePageSRV[pageIdx] != NULL) {
                g_TexturePageSRV[pageIdx]->Release();
                g_TexturePageSRV[pageIdx] = NULL;
            }
            g_TexturePageWidth[pageIdx] = 0;
            g_TexturePageHeight[pageIdx] = 0;
        }
    }
}

void delete_texture_set_secondary(int slotIndex) {
    TexturePage_DeleteSet(slotIndex + 0xF);
}

void TexturePage_LoadImage(void* imageData, short param2, short param3) {
    ProcessTextureImage(imageData, param2, param3, 0);
}

void Display_SetParams(int param1, int param2) {
    g_SubpixelOffsetX = param1;
    g_SubpixelOffsetY = param2;
}
