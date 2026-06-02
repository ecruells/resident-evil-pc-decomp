#pragma once
#include <windows.h>
#include <d3d11.h>

struct TextureDesc;

#define MAX_SPRITE_COMMANDS 300
#define MAX_OT_ENTRIES 32



// TextureDraw (0x008ec900) - Sprite command buffer entry (0x34 bytes)
struct TextureDraw {
    unsigned int type;          // 0x00 (always 10 for textured quads)
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
};
static_assert(sizeof(TextureDraw) == 0x34, "TextureDraw size mismatch");

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
extern int   g_MaxFadeValue;
extern int   g_DepthSortOverride;
extern float g_ColorScaleFactor;

void BuildSpriteRenderFlags(unsigned int textureFlags, unsigned int* outFlags);
int  GetTextureVariant(unsigned int textureFlags);
void SpriteQueue_Reset(void);
void FlushSpriteCommands(void);

int draw_texture(TextureDesc* texture, unsigned short depth);
int AddSprite(TextureDesc* texture, short depth, int tpage, int fade);
int AddTintSprite(TextureDesc* texture, unsigned short fade);
int AddFadePoly(unsigned short alpha, int tpage, int u, int v, int clut, unsigned char* rgb,
                int x, int y, int z, unsigned short forceAlpha);
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
