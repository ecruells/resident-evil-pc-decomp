#include "SpriteRenderer.h"
#include "../Globals.h"
#include "../marni/MarniSystem.h"
#include "../marni/PSXTexture.h"
#include "../marni/MarniBits.h"
#include <cstdio>
#include <algorithm>
#include <cstdlib>

// ============================================================================
// Global variables
// ============================================================================
TextureDraw g_SpriteCommandBuffer[MAX_SPRITE_COMMANDS];
OTEntry g_OT[MAX_OT_ENTRIES];
int g_RenderBufferIndex      = 0;
int g_RenderDisableFlags     = 0;
int g_SubpixelOffsetX        = 0;   // 0x004d2bd0
int g_SubpixelOffsetY        = 0;   // 0x004d2bd4
int g_displayImageOriginX    = 0;   // 0x004c335c - display image origin (FUN_00470a90)
int g_displayImageOriginY    = 0;   // 0x004c3360
int g_MaxFadeValue           = 4095;
int g_DepthSortOverride      = 0;
float g_ColorScaleFactor     = 2.0f / 255.0f;
int g_nFadeInverted          = 0;   // 0x004c333c
int g_renderPrimCount        = 0;   // 0x004c2d10

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
    g_renderPrimCount = 0;
}

// ============================================================================
// FlushSpriteCommands
// Converts TextureDraw entries (in PS1 absolute screen coordinates, 0-319 x 0-239)
// to D3D11 draw calls at the current display resolution.
// ============================================================================
void FlushSpriteCommands(void) {

    if (g_SpriteQueueCount == 0) {
        return;
    }

    // 0x0046d...: Emulate the PS1 ordering table (OT). The original GPU linked
    // primitives by Z-depth so that a LOWER depthSort value rendered ON TOP
    // (nearer the camera). Without this, sprites are drawn in submission order,
    // which makes later-drawn frames occlude the icons/textures they should sit
    // behind (e.g. the equipped-weapon frame covering the weapon texture).
    // Stable sort so equal-depth draws keep their submission order.
    std::stable_sort(
        g_SpriteCommandBuffer,
        g_SpriteCommandBuffer + g_SpriteQueueCount,
        [](const TextureDraw& a, const TextureDraw& b) {
            return a.depthSort > b.depthSort;
        });

    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    (void)pD3D;
    // Scale PS1-space coords to the backbuffer: physical/logical, exactly how
    // the original Marni layer scaled primitives at draw time (FUN_0042ba60).
    float scaleX, scaleY;
    MarniGetRenderScale(&scaleX, &scaleY);

    for (int i = 0; i < g_SpriteQueueCount; i++) {
        TextureDraw* cmd = &g_SpriteCommandBuffer[i];

        // Line primitives (type 11): used by the menu EKG health bar.
        // The original FUN_00470c60 built a line primitive and inserted it
        // into the ordering table with depthSort = depth*16 + 500, exactly
        // like draw_texture, so it participates in the same OT sort.
        if (cmd->type == 11) {
            float sx0 = (float)cmd->x0 * scaleX;
            float sy0 = (float)cmd->y0 * scaleY;
            float sx1 = (float)cmd->x1 * scaleX;
            float sy1 = (float)cmd->y1 * scaleY;

            int cr = (int)(cmd->r * 255.0f);
            int cg = (int)(cmd->g * 255.0f);
            int cb = (int)(cmd->b * 255.0f);
            if (cr > 255) cr = 255; if (cr < 0) cr = 0;
            if (cg > 255) cg = 255; if (cg < 0) cg = 0;
            if (cb > 255) cb = 255; if (cb < 0) cb = 0;
            float alpha = cmd->unk1c;
            if (alpha < 0.0f) alpha = 0.0f;
            if (alpha > 1.0f) alpha = 1.0f;
            int ca = (int)(alpha * 255.0f);
            if (ca > 255) ca = 255; if (ca < 0) ca = 0;
            DWORD color = ((DWORD)ca << 24) | ((DWORD)cr << 16) | ((DWORD)cg << 8) | (DWORD)cb;
            MarniDrawLine(sx0, sy0, sx1, sy1, 1.0f, color);
            continue;
        }
        if (cmd->type != 10) continue;

        float x = (float)cmd->x0 * scaleX;
        float y = (float)cmd->y0 * scaleY;
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
        MarniHandle srv = MARNI_NULL_HANDLE;
        // pageW/pageH must come from g_TexturePageWidth/Height, which every
        // SRV-creating path sets. A 0 here means the slot's SRV metadata was
        // lost (or never set); we must NOT fall back to 256, because that
        // would under-sample a smaller SRV and render a tiny sub-region
        // stretched across the sprite (the statface/blue/staitem 8x8 bug).
        float pageW = 0.0f;
        float pageH = 0.0f;
        if (texSlot >= 0 && texSlot < 256) {
            srv = g_TexturePageSRV[texSlot];
            if (g_TexturePageWidth[texSlot] > 0)  pageW = (float)g_TexturePageWidth[texSlot];
            if (g_TexturePageHeight[texSlot] > 0) pageH = (float)g_TexturePageHeight[texSlot];
        }
        if (srv == MARNI_NULL_HANDLE) {
            texSlot = cmd->texturePage;
            if (texSlot >= 0 && texSlot < 256) {
                srv = g_TexturePageSRV[texSlot];
                if (g_TexturePageWidth[texSlot] > 0)  pageW = (float)g_TexturePageWidth[texSlot];
                if (g_TexturePageHeight[texSlot] > 0) pageH = (float)g_TexturePageHeight[texSlot];
            }
        }
        if (srv == MARNI_NULL_HANDLE) {
            continue;
        }
        if (pageW <= 0.0f || pageH <= 0.0f) {
            // SRV exists but its dimensions are unknown — can't normalize UVs.
            continue;
        }

        // UV normalization: cmd->u0/v0 are pixel offsets within the SRV,
        // cmd->u1/v1 are inclusive pixel endpoints. The original DX5
        // draw code adds +1 to the endpoint before normalizing, converting
        // from inclusive to exclusive range. D3D11 point sampling with
        // pixel-center interpolation correctly samples the full range when
        // endpoint is converted by +1 (no half-texel offset needed).
        // Mirror flags 0x10(X)/0x20(Y) are emulated by swapping UVs.
        float u0 = (float)cmd->u0 / pageW;
        float v0 = (float)cmd->v0 / pageH;
        float u1 = (float)(cmd->u1 + 1) / pageW;
        float v1 = (float)(cmd->v1 + 1) / pageH;

        // Emulate PS1 texture flip (0x10=X, 0x20=Y)
        unsigned int renderFlags = (unsigned int)cmd->unk1c;
        if (renderFlags & 0x10) { float t = u0; u0 = u1; u1 = t; }
        if (renderFlags & 0x20) { float t = v0; v0 = v1; v1 = t; }

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
// SubmitLine (0x00470c60 helper)
// Queues a 1px line primitive (type 11) into the sprite command buffer.
// Coordinates are in PS1 game space (with screen offset applied by the
// caller, matching how draw_texture handles screenX/screenY).
// ============================================================================
int SubmitLine(short x0, short y0, short x1, short y1, unsigned short depth,
               float r, float g, float b, float alpha)
{
    if (g_SpriteQueueCount >= MAX_SPRITE_COMMANDS - 1) return 0;

    TextureDraw* cmd = &g_SpriteCommandBuffer[g_SpriteQueueCount];
    cmd->type = 11;
    cmd->x0 = x0;
    cmd->y0 = y0;
    cmd->x1 = x1;
    cmd->y1 = y1;
    cmd->depthSort = (unsigned int)depth * 16 + 500;
    cmd->unk1c = alpha;
    cmd->r = r;
    cmd->g = g;
    cmd->b = b;
    cmd->texturePage = 0;
    cmd->extraFlags = 0;
    cmd->u0 = 0;
    cmd->v0 = 0;
    cmd->u1 = 0;
    cmd->v1 = 0;

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
    // Only clear the legacy PSX texture-page handle table. The D3D11 SRV
    // metadata (g_TexturePageWidth/Height/Bpp) must stay in sync with
    // g_TexturePageSRV, which is NOT released here — see the preservation
    // note in TexturePage_DeleteSet. Zeroing width/height while the SRV
    // persists made FlushSpriteCommands fall back to pageW=256, which
    // under-sampled 64x64 global textures (statface/blue/staitem) and
    // rendered their sprites as a tiny 8x8 region stretched to size.
    for (int i = 0; i < 256; i++) {
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
            // SRVs, width, height, and Bpp are preserved across clear_textures()
            // so global textures (fonts, status.tim) remain valid for rendering.
        }
    }
}

void delete_texture_set_secondary(int slotIndex) {
    TexturePage_DeleteSet(slotIndex + 0xF);
}

void TexturePage_LoadImage(void* imageData, short param2, short param3) {
    ProcessTextureImage(imageData, param2, param3, 0);
}

// Display_SetParams (0x00470750)
// Sets the display-image origin read by FUN_00470a90 when it rebuilds the
// background sprites: 0x004c335c / 0x004c3360.
//
// These are NOT the subpixel offset (0x004d2bd0 / 0x004d2bd4). Writing the
// subpixel offset here zeroed the projection centre every frame, because
// ResetScreenAndRebuildSprites (0x00401020) calls this with (0,0) and runs once
// per frame from the main loop - so every 3D object was projected around the
// top-left corner of the screen instead of the screen centre.
void Display_SetParams(int param1, int param2) {
    g_displayImageOriginX = param1;
    g_displayImageOriginY = param2;
}

// ============================================================================
// texture_queue_reset (0x004739e0)
// Resets the texture queue state and all 4 queue entries.
// Each entry is 10 bytes. Clears counters DAT_00ae9f04, DAT_00ae9f06,
// DAT_00ae9f00, DAT_00ae9efc.
//
// FOUR entries, not five: the original is `cVar2 = 4; do { ... } while (--cVar2
// != 0);`, and FUN_00473d10 / FUN_00473d60 likewise scan exactly 4. This used to
// loop 5 times and ran 10 bytes off the end of the 40-byte g_textureQueueData.
// Harmless in the original address map, but our .bss puts g_MessageSpeedUpFlag,
// g_MessageCharDelay, g_MessageCharTimer and g_MessageClutBase immediately after
// the array, so the phantom fifth entry's `p[1] = 0` landed exactly on
// g_MessageCharDelay. room_set -> texture_queue_reset runs a few frames into the
// new-game loading message, so the reveal started at the correct speed and then
// dropped to delay 0; msg_skip_char then set the timer to 0 and the next frame's
// `timer - 1` wrapped an unsigned char to 255, stalling ~256 frames per glyph.
// It also clobbered 6 bytes of g_psxTextureArray. Do not "restore" the 5.
// ============================================================================
void texture_queue_reset(void) {
    DAT_00ae9f04 = 0;
    DAT_00ae9f06 = 0;
    DAT_00ae9f00 = 0;
    DAT_00ae9efc = 0;
    unsigned char* p = g_textureQueueData;
    for (int i = 0; i < 4; i++) {
        p[3] = 0;
        p[4] = 0;
        p[5] = 0;
        *(unsigned short*)(p + 6) = 0;
        *(unsigned short*)(p + 8) = 0x100;
        p[1] = 0;
        p += 10;
    }
}

// (0x00470c60) - Queue EKG line primitive (primary line)
// Builds a line primitive from the 16-byte EKG line struct and inserts it
// into the ordering table with OT depth `param_2`. The original wrote the
// primitive into the DAT_008e3e60 render buffer and called the
// CMarniDirect3D vtable[10] entry (SetTexture == OT_InsertPrimitive); the
// port submits the same line into the sprite command queue instead.
// Returns 1 when submitted, 0 when the per-frame primitive cap is reached.
int FUN_00470c60(void* prim, int depth)
{
    if (g_renderPrimCount >= 0x28) return 0;

    unsigned char* p = (unsigned char*)prim;
    unsigned short depthOut = (unsigned short)depth;

    // Software-renderer modes offset the OT depth by 0x28.
    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (pD3D && (pD3D->m_deviceType == 5 || pD3D->m_deviceType == 7)) {
        depthOut = depthOut + 0x28;
    }

    short x0 = *(short*)(p + 4);
    short y0 = *(short*)(p + 6);
    short x1 = *(short*)(p + 8);
    short y1 = *(short*)(p + 10);
    float r = (float)p[0xC] * 0.00390625f;
    float g = (float)p[0xD] * 0.00390625f;
    float b = (float)p[0xE] * 0.00390625f;

    if (g_nFadeInverted != 0) {
        if (g_MaxFadeValue < (int)depthOut) depthOut = (unsigned short)g_MaxFadeValue;
        depthOut = (unsigned short)(g_MaxFadeValue - (int)depthOut);
    }

    if ((g_RenderDisableFlags & 0x10) == 0) {
        SubmitLine(x0, y0, x1, y1, depthOut, r, g, b, 1.0f);
        g_renderPrimCount++;
    }
    return 1;
}

// (0x00470e60) - Queue EKG line primitive (secondary line with gradient)
// Identical to FUN_00470c60 but the line struct carries a second color
// endpoint at bytes 0xF-0x11 (the gradient target computed by FUN_00438800).
int FUN_00470e60(void* prim, int depth)
{
    if (g_renderPrimCount >= 0x28) return 0;

    unsigned char* p = (unsigned char*)prim;
    unsigned short depthOut = (unsigned short)depth;

    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (pD3D && (pD3D->m_deviceType == 5 || pD3D->m_deviceType == 7)) {
        depthOut = depthOut + 0x28;
    }

    short x0 = *(short*)(p + 4);
    short y0 = *(short*)(p + 6);
    short x1 = *(short*)(p + 8);
    short y1 = *(short*)(p + 10);
    float r = (float)p[0xC] * 0.00390625f;
    float g = (float)p[0xD] * 0.00390625f;
    float b = (float)p[0xE] * 0.00390625f;

    if (g_nFadeInverted != 0) {
        if (g_MaxFadeValue < (int)depthOut) depthOut = (unsigned short)g_MaxFadeValue;
        depthOut = (unsigned short)(g_MaxFadeValue - (int)depthOut);
    }

    if ((g_RenderDisableFlags & 0x10) == 0) {
        SubmitLine(x0, y0, x1, y1, depthOut, r, g, b, 1.0f);
        g_renderPrimCount++;
    }
    return 1;
}

