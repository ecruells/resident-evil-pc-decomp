// WeaponTexture.cpp - Port-only: a weapon model's own gun texture (no original address)
// See WeaponTexture.h for what the file is and why.
#include "../Globals.h"
#include "../marni/MarniSystem.h"
#include "../marni/PSXTexture.h"
#include "../platform/platform.h"
#include "../system/AssetPath.h"
#include "../system/HdTextures.h"
#include "WeaponTexture.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

const int kBank       = 0x16;   // the player's texture bank
const int kPlayerPage = 7;      // the CLUT page the weapon is bound to (0x1E7)
const int kMaxHeight  = 1024;   // tallest player texture the slot is built for
const int kPage       = 256;    // the file: 256 x 256, page left, mask right

// The TIM uploaded into the slot: header, two CLUT rows, 256 x height pixels.
const size_t kTimHead = 8 + (12 + 1024) + 12;
unsigned char s_tim[kTimHead + 256 * kMaxHeight];

bool s_loaded = false;          // slot 1 of bank 0x16 is matched (count 2)
char s_hdKey[128] = "";

unsigned char* GunSlot()
{
    return &g_psxTextureArray[kBank * 0x1b60 + 0x36C];
}

unsigned int Rd32(const unsigned char* p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }
unsigned int Rd16(const unsigned char* p) { return p[0] | p[1] << 8; }
void Wr32(unsigned char* p, unsigned int v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24); }
void Wr16(unsigned char* p, unsigned int v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }

// The slot stops being matched; its texture is left alone, because the old
// weapon's render object still holds it until CreateAnimObject replaces it
// (and an ExecAsync yields a frame). The next upload clears it first.
void Drop()
{
    s_loaded = false;
    g_objectCountArray[kBank] = 1;
}

// Run through ExecAsync, like TmdProcessingCallback: PSXTexture::Store wants
// the main stack, not a task's. The clear and the store happen together.
void UploadCallback(void)
{
    PSXTexture* t = (PSXTexture*)GunSlot();
    VideoDriver_ClearState348(t, g_pMarniDirect3D);
    t->Store((int*)s_tim, 1);
    HdTextures_SetOwnerKey(t, ((CMarniBits*)t)->m_pPixelData, s_hdKey[0] ? s_hdKey : NULL);
}

// A packet is the gun's when every texel of its UV box is flagged in the mask.
bool PacketOnGun(const unsigned int* p, const unsigned char* pix)
{
    int u0 = 255, u1 = 0, v0 = 255, v1 = 0;
    for (int c = 0; c < 3; ++c) {
        const int u = p[1 + c] & 0xFF, v = (p[1 + c] >> 8) & 0xFF;
        if (u >= 128) return false;
        if (u < u0) u0 = u;
        if (u > u1) u1 = u;
        if (v < v0) v0 = v;
        if (v > v1) v1 = v;
    }
    for (int y = v0; y <= v1; ++y)
        for (int x = u0; x <= u1; ++x)
            if (pix[y * kPage + 128 + x] == 0) return false;
    return true;
}

// The textured triangles of the weapon's TMD (ProcessTmdTextures' walk).
template <typename F>
void ForEachTexturedTriangle(unsigned int* tmd, F f)
{
    int objects = (int)tmd[2];
    unsigned int* obj = tmd + 3;
    for (; objects > 0; --objects, obj += 7) {
        unsigned int* p = (unsigned int*)obj[4];
        for (int n = (int)obj[5]; n > 0; --n) {
            if ((*p & 0x4000000) != 0 && (*p & 0xFDFFFFFF) == 0x34000609) f(p);
            p += ((*p >> 8) & 0xFF) + 1;
        }
    }
}

// The key's tree, the part before the first '/'.
bool SameTree(const char* a, const char* b)
{
    const size_t n = strcspn(a, "/");
    return n == strcspn(b, "/") && strncmp(a, b, n) == 0;
}

}  // namespace

void WeaponTexture_Apply(const char* emwPath, int charBlock, unsigned int* tmd)
{
    Drop();
    if (emwPath == NULL || tmd == NULL) return;

    // players/w12.emw -> players/w12_1.tim
    const size_t n = strlen(emwPath);
    char timPath[300];
    if (n < 4 || n + 8 > sizeof(timPath)) return;
    snprintf(timPath, sizeof(timPath), "%.*s_%d.tim", (int)(n - 4), emwPath, charBlock & 3);

    // Only a texture from the weapon model's own tree: an overlay's copy of a
    // weapon (the Saturn's W15) can address its page differently.
    char emwKey[128], timKey[128];
    if (AssetSourceKey(emwPath, emwKey, sizeof(emwKey)) == NULL ||
        AssetSourceKey(timPath, timKey, sizeof(timKey)) == NULL || !SameTree(emwKey, timKey))
        return;

    char resolved[300], norm[300];
    const char* path = plat_normalize_path(ResolveAssetRoot(timPath, resolved, sizeof(resolved)),
                                           norm, sizeof(norm));
    size_t size = 0;
    unsigned char* file = (unsigned char*)plat_file_read_all(path, &size);
    if (file == NULL) return;   // no gun texture: the original behaviour

    // 8 bpp, one CLUT row of 256, 256 x 256 pixels.
    const size_t clen = size >= 20 ? Rd32(file + 8) : 0;
    const size_t img = 8 + clen;
    const bool fileOk = size >= 20 && Rd32(file) == 0x10 && Rd32(file + 4) == 9 &&
                        Rd16(file + 16) == 256 && Rd16(file + 18) >= 1 &&
                        clen == 12 + 512 * (size_t)Rd16(file + 18) && img + 12 <= size &&
                        Rd16(file + img + 8) * 2 == kPage && Rd16(file + img + 10) == kPage &&
                        img + 12 + (size_t)kPage * kPage <= size;

    // The slot is built at the player texture's size, because the weapon's
    // UVs are normalised against bank 0x16's first slot (CreateTmdObjectInternal
    // reads its width and height for the whole object).
    const PSXTexture* player = (const PSXTexture*)&g_psxTextureArray[kBank * 0x1b60];
    const CMarniBits* playerBits = (const CMarniBits*)player;
    const unsigned char* playerPix = (const unsigned char*)playerBits->m_pPixelData;
    const unsigned short* playerPal = (const unsigned short*)playerBits->m_pPalette;
    const int w = (int)player->m_WidthPixels, h = (int)player->m_Height;
    const int pitch = (int)player->m_RowStride;
    const int rows = (int)player->m_NumCLUTs;
    const int slotPage = kPlayerPage + (rows > 2 ? rows : 2);
    if (!fileOk || playerPix == NULL || playerPal == NULL || player->m_BitDepth != 8 ||
        w != 256 || pitch < 256 || h < kPage || h > kMaxHeight || 0x1E0 + slotPage + 1 >= 0x200) {
        free(file);
        return;
    }
    const unsigned char* pix = file + img + 12;
    const unsigned char* gunPal = file + 20;

    // Only when the model worn now does not already show this gun: then the
    // weapon stays on the player's own texture, exactly as the original draws
    // it (and keeps the player's HD skin). Same = every gun texel the same
    // index-0 state and every channel within 2 steps.
    bool differs = false;
    int gun = 0;
    ForEachTexturedTriangle(tmd, [&](unsigned int* p) {
        if (!PacketOnGun(p, pix)) return;
        ++gun;
        int u0 = 255, u1 = 0, v0 = 255, v1 = 0;
        for (int c = 0; c < 3; ++c) {
            const int u = p[1 + c] & 0xFF, v = (p[1 + c] >> 8) & 0xFF;
            if (u < u0) u0 = u;
            if (u > u1) u1 = u;
            if (v < v0) v0 = v;
            if (v > v1) v1 = v;
        }
        for (int y = v0; y <= v1 && !differs; ++y)
            for (int x = u0; x <= u1 && !differs; ++x) {
                const int gi = pix[y * kPage + x], pi = playerPix[y * pitch + x];
                if ((gi == 0) != (pi == 0)) { differs = true; break; }
                if (gi == 0) continue;
                const unsigned a = Rd16(gunPal + 2 * gi) & 0x7FFF, b = playerPal[pi] & 0x7FFF;
                for (int sh = 0; sh < 15; sh += 5) {
                    const int d = (int)((a >> sh) & 31) - (int)((b >> sh) & 31);
                    if (d > 2 || d < -2) differs = true;
                }
            }
    });
    if (gun == 0 || !differs) {
        free(file);
        return;
    }

    // One render object takes all its materials from ONE slot of the bank (the
    // first one any of its packets matches, CreateTmdObjectInternal), so the
    // whole weapon moves to the new slot: its left half is the player's page
    // as worn (the hand, CLUT row A = the player's row 0), its right half the
    // gun's page from the file (CLUT row B). Two rows at slotPage, slotPage+1;
    // the image at bank 0x16's VRAM position (what TmdProcessingCallback
    // writes into a model's TIM before storing it).
    unsigned char* t = s_tim;
    Wr32(t + 0, 0x10);
    Wr32(t + 4, 9);
    Wr32(t + 8, 12 + 1024);
    Wr16(t + 12, Rd16(file + 12));
    Wr16(t + 14, 0x1E0 + slotPage);
    Wr16(t + 16, 256);
    Wr16(t + 18, 2);
    for (int c = 0; c < 256; ++c) Wr16(t + 20 + 2 * c, playerPal[c]);
    memcpy(t + 20 + 512, gunPal, 512);
    unsigned char* im = t + 8 + 12 + 1024;
    Wr32(im + 0, 12 + 256 * (unsigned)h);
    Wr16(im + 4, (kBank & 0xF) << 6);
    Wr16(im + 6, (kBank & 0x10) << 4);
    Wr16(im + 8, 128);
    Wr16(im + 10, (unsigned)h);
    unsigned char* dst = im + 12;
    memset(dst, 0, 256 * (size_t)h);
    for (int y = 0; y < kPage; ++y) {
        memcpy(dst + y * 256, playerPix + y * pitch, 128);
        memcpy(dst + y * 256 + 128, pix + y * kPage, 128);
    }
    // No HD picture: the slot's left half is whatever model is worn.
    s_hdKey[0] = '\0';

    ExecAsync((void*)UploadCallback);
    s_loaded = true;
    g_objectCountArray[kBank] = 2;

    // Every packet onto the slot (ProcessTmdTextures adds the page as
    // depth * 0x400000): the hand's to row A, the gun's to row B and 128
    // texels right, onto the gun's half.
    const unsigned int toA = (unsigned int)(slotPage - kPlayerPage) * 0x400000;
    ForEachTexturedTriangle(tmd, [&](unsigned int* p) {
        if (PacketOnGun(p, pix)) {
            p[1] += toA + 0x400000;
            for (int c = 0; c < 3; ++c) p[1 + c] += 128;
        } else {
            p[1] += toA;
        }
    });
    free(file);
}

void WeaponTexture_PlayerTextureReloaded(void)
{
    Drop();
}

int WeaponTexture_PlayerBankSlots(void)
{
    return s_loaded ? 2 : 1;
}
