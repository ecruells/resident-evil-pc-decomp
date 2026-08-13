#pragma once
#include <windows.h>

struct TextureDesc;

#define MAX_SPRITE_COMMANDS 300
#define MAX_OT_ENTRIES 32



// TextureDraw (0x008ec900) - Sprite command buffer entry (0x34 bytes)
struct TextureDraw {
    unsigned int type;          // 0x00 (10 = textured quad, 12 = 4-corner quad)
    unsigned int renderFlags;   // 0x04 (filled by SetTexture vtable call)
    short x0;                   // 0x08
    short y0;                   // 0x0a
    short x1;                   // 0x0c
    short y1;                   // 0x0e
    short u0;                   // 0x10
    short v0;                   // 0x12
    short u1;                   // 0x14
    short v1;                   // 0x16
    unsigned int depthSort;     // 0x18
    float unk1c;                // 0x1c (stores render flags from BuildSpriteRenderFlags as float)
    float r;                    // 0x20
    float g;                    // 0x24
    float b;                    // 0x28
    int texturePage;            // 0x2c
    unsigned int extraFlags;    // 0x30
    // Type 12 (4-corner quad) only: corners 2 and 3 plus their UVs.
    // UVs are 0..4096 fixed point (0..1 of the texture page).
    short x2;                   // 0x34
    short y2;                   // 0x36
    short x3;                   // 0x38
    short y3;                   // 0x3a
    short u2;                   // 0x3c
    short v2;                   // 0x3e
    short u3;                   // 0x40
    short v3;                   // 0x42
    // Type 12: per-corner view-space Z (the perspective-divide w).
    short wz0;                  // 0x44
    short wz1;                  // 0x46
    short wz2;                  // 0x48
    short wz3;                  // 0x4a
};
static_assert(sizeof(TextureDraw) == 0x4C, "TextureDraw size mismatch");

struct OTEntry {
    int   type;
    float data[32];
};

extern TextureDraw g_SpriteCommandBuffer[MAX_SPRITE_COMMANDS];
extern OTEntry g_OT[MAX_OT_ENTRIES];

extern int   g_RenderBufferIndex;
extern int   g_RenderDisableFlags;
extern int   g_SubpixelOffsetX;
extern int   g_SubpixelOffsetY;
extern int   g_displayImageOriginX;
extern int   g_displayImageOriginY;
extern int   g_MaxFadeValue;
extern int   g_DepthSortOverride;
extern float g_ColorScaleFactor;
extern int   g_nFadeInverted;

// Per-frame count of line primitives submitted (DAT_004c2d10). The original
// capped EKG line submissions at 40 per frame (OT capacity).
extern int   g_renderPrimCount;

void BuildSpriteRenderFlags(unsigned int textureFlags, unsigned int* outFlags);
int  GetTextureVariant(unsigned int textureFlags);
void SpriteQueue_Reset(void);
void FlushSpriteCommands(void);
void FlushSpriteCommandsRange(unsigned int minDepth, unsigned int maxDepth);

int draw_texture(TextureDesc* texture, unsigned short depth);
int SubmitLine(short x0, short y0, short x1, short y1, unsigned short depth,
               float r, float g, float b, float alpha);
int AddSprite(TextureDesc* texture, short depth, int tpage, int fade);
int AddTintSprite(TextureDesc* texture, unsigned short fade);
int AddFadePoly(unsigned short alpha, int tpage, unsigned char* rgb,
                const int* px, const int* py, const int* wz,
                const int* cu, const int* cv, int count);
int SubmitEffectSprite(TextureDesc* texture, int depth, int textureId,
                       unsigned char r, unsigned char g, unsigned char b,
                       int scaleX, int scaleY, int blendMode, short brightness);

int DrawPrim_SpriteLarge(int* params, unsigned short alpha, int tpage,
                         unsigned int u, unsigned int v, unsigned int clut);

void TexturePage_Load(int slotIndex, void* imageData);
void TexturePage_ClearAll(void);
void TexturePage_Create(int slotIndex);
void TexturePage_SetupFull(void* imageData, short bankID, short pageOffset, int slotIndex);
void TexturePage_Refresh(int slotIndex, int mode);
void TexturePage_RefreshCLUT(int slotIndex, int mode, int clutIndex);
void TexturePage_DeleteSet(int slotIndex);
void TexturePage_LoadImage(void* imageData, short param2, short param3);
void delete_texture_set_secondary(int slotIndex);

void Display_SetParams(int param1, int param2);
