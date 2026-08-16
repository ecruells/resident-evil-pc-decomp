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
    // SPRITE_FLAG_* bits from BuildSpriteRenderFlags/GetTextureVariant. The
    // original stores this as a plain integer (0x0046df3d is a `mov dword`, not
    // a float store); the neighbouring r/g/b ARE floats, which is why the
    // decompiler renders this one as a float cast too.
    unsigned int spriteFlags;   // 0x1c
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
    // Port-only. Which pass owns this command: SPRITE_CLASS_SCENE entries carry
    // a real view-space Z in depthSort and are drawn interleaved with the TMD
    // triangles (FlushTmdObjects) instead of in the flat 2D passes.
    unsigned int sortClass;     // 0x4c
    // Port-only. Vertex alpha, 0..1. The original has no such field: its blend
    // came from the texture page's own mode, which this renderer does not
    // reproduce. This used to be crammed into the 0x1c flags word and told
    // apart from a flags value by testing whether it fell inside (0,1), so a
    // sprite could never carry both a mirror flag and a translucency.
    float alpha;                // 0x50
};
static_assert(sizeof(TextureDraw) == 0x54, "TextureDraw size mismatch");

// Sprite command classes. These are a bitmask: FlushSpriteCommandsRange draws
// only the classes it is asked for, so the primitives that belong in the scene
// can be pulled out of the 2D passes and handed to the 3D pass without being
// drawn twice.
//
// The original kept all of these in one ordering table keyed by an OT index,
// and this port's equivalent key is depthSort = OT index * 16 (draw_texture's
// depth*16 + 500 is the same scale with a 2D offset). An entity enters that
// table at t[2] >> 4, so an OT index scaled back up by 16 is view-space Z -
// which is what makes SCENE commands directly comparable with a TMD triangle.
#define SPRITE_CLASS_NORMAL    0x1u
#define SPRITE_CLASS_ROOMMASK  0x2u   // DrawRoomSpr overlays, key = posData
#define SPRITE_CLASS_SHADOW    0x4u   // ground shadow / blood pool (AddFadePoly)
#define SPRITE_CLASS_EFFECT    0x8u   // 2D billboard effects (SubmitEffectSprite)
#define SPRITE_CLASS_SCENE     (SPRITE_CLASS_ROOMMASK | SPRITE_CLASS_SHADOW \
                                | SPRITE_CLASS_EFFECT)
#define SPRITE_CLASS_ALL       (SPRITE_CLASS_NORMAL | SPRITE_CLASS_SCENE)

// ---------------------------------------------------------------------------
// TextureDesc::flags - the bits BuildSpriteRenderFlags and GetTextureVariant
// read off a descriptor. The low 24 bits carry unrelated per-sprite state
// (blend mode, texture bit depth); only these are decoded here.
// ---------------------------------------------------------------------------
#define TEXDESC_MIRROR_V       0x00400000u  // flip the sprite vertically
#define TEXDESC_MIRROR_U       0x00800000u  // flip the sprite horizontally
#define TEXDESC_VARIANT_MASK   0x30000000u  // which variant, 0-3
#define TEXDESC_VARIANT_ENABLE 0x40000000u  // gate on the variant field

// ---------------------------------------------------------------------------
// TextureDraw::spriteFlags - what the renderer actually acts on. The PS1 had no
// negative texture coordinates, so a mirrored sprite was signalled out of band
// and the flip applied at draw time; FlushSpriteCommandsRange reproduces it by
// swapping the U or V pair.
// ---------------------------------------------------------------------------
#define SPRITE_FLAG_VARIANT    0x08u  // GetTextureVariant() != 0; see the note
#define SPRITE_FLAG_MIRROR_U   0x10u  // <- TEXDESC_MIRROR_U
#define SPRITE_FLAG_MIRROR_V   0x20u  // <- TEXDESC_MIRROR_V

// SPRITE_FLAG_VARIANT is set by every producer that builds flags from a
// descriptor, and no draw path reads it. In the original it reached the Marni
// texture-page layer, which selected a blend/CLUT variant from it; this port
// resolves the texture per command through the page slot instead, so the bit
// is carried for fidelity and never acted on. Do not "clean it up": it is the
// only surviving record that the descriptor asked for a variant.

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

// The `variant ? (flags | 8) : flags` pattern the original repeats at every
// descriptor-driven producer (0x0046deff-0x0046df07).
static inline unsigned int SpriteBuildFlags(unsigned int textureFlags) {
    unsigned int flags;
    BuildSpriteRenderFlags(textureFlags, &flags);
    if (GetTextureVariant(textureFlags) != 0) flags |= SPRITE_FLAG_VARIANT;
    return flags;
}
void SpriteQueue_Reset(void);
void FlushSpriteCommands(void);
void FlushSpriteCommandsRange(unsigned int minDepth, unsigned int maxDepth,
                              unsigned int classMask = SPRITE_CLASS_NORMAL);

// Collect the distinct depthSort values of the queued SPRITE_CLASS_SCENE
// commands, sorted far to near. FlushTmdObjects walks these alongside its own
// sorted triangles so the two streams interleave the way the original ordering
// table did. Returns the number written (at most maxOut).
int  SpriteQueue_CollectSceneDepths(unsigned int* out, int maxOut);

int draw_texture(TextureDesc* texture, unsigned short depth);
int SubmitLine(short x0, short y0, short x1, short y1, unsigned short depth,
               float r, float g, float b, float alpha);
int AddSprite(TextureDesc* texture, short depth, int tpage, int fade);
int AddTintSprite(TextureDesc* texture, unsigned short fade);
// transZ is the shadow's composed-matrix t[2] (view-space Z of the quad's
// origin). The original stores it in the OT record and compares it against the
// fade polys already inserted this frame to keep two overlapping shadows off
// one ordering-table slot; see the de-collision pass in AddFadePoly.
int AddFadePoly(unsigned short alpha, int transZ, int tpage, unsigned char* rgb,
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
