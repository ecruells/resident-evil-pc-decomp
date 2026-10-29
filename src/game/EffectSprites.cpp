// EffectSprites.cpp - Effect sprite loading and texture-page setup
// (decompiled from Ghidra)
//
// Owns the room/weapon effect-sprite pipeline: the RDT effect table walk,
// the 256-tall texture page packing (FUN_0047bc80), the per-room TIM blit
// (0x0047d020), the global weapon-FX sheet loader core00.esp/.etm
// (0x0045fa80) and InitRoomEffSprite (0x0047b9b0). The behaviour dispatchers
// live in EffectSystem.cpp, which indexes the tables declared here.
#include "../Globals.h"
#include "FileLoader.h"
#include "SpriteRenderer.h"
#include "../marni/MarniDX.h"
#include "../marni/MarniSystem.h"
#include <cstdio>
#include <cstring>
#include "../system/AssetPath.h"
#include "../DebugPrint.h"
#include "../system/HdTextures.h"   // HdRegionScope, HdPageScope (port-only)
#include "dc/ArrangeStages.h"          // room_effect_page_entry

// TMD texture header struct (output of ParseTmdTextureHeader)
// Packed struct matching the original byte layout (28 bytes = 0x1C)
#pragma pack(push, 1)
struct TmdTextureHeader {
    int   count;       // offset 0x00: (*data & 0xF) sign-extended
    short field_04;    // offset 0x04: first short from aligned sub-section
    short field_06;    // offset 0x06: second short
    short field_08;    // offset 0x08: third short (used for page count calc)
    short field_0A;    // offset 0x0A: fourth short
    int   dataPtr;     // offset 0x0C: pointer into sub-section data
    short field_10;    // offset 0x10: first short from data+8 (CLUT descriptor low)
    short field_12;    // offset 0x12: second short from data+8
    short field_14;    // offset 0x14: third short from data+8
    short field_16;    // offset 0x16: fourth short from data+8 (depth increment)
    int   ptr_10;      // offset 0x18: data + 0x10 pointer
};
#pragma pack(pop)

// ParseTmdTextureHeader is defined in TmdAnimation.cpp
extern void ParseTmdTextureHeader(void* data, TmdTextureHeader* header);

// ============================================================================
// Room effect sprite relation table (0x004c48b8)
// Indexed by (stageId * 32 + roomId) * 4 + slotIndex.
// Each group of 4 bytes: effect sprite indices for texture pages 0-3.
// 0xFF = no effect sprite for that slot.
// ============================================================================
// Shared with the effect renderer (EffectSystem.cpp), which indexes the same
// table through the depth offset 0x004c48a0. `extern` keeps external linkage:
// a namespace-scope `const` would be internal to this TU.
//
// VERIFIED BYTE-FOR-BYTE against the exe at 0x004c48b8 (all 896 bytes) on
// 2026-08-16.
//
// It is SEVEN stages, not five. The table runs to 0x004c4c38, which is exactly
// where g_EffectSpriteNames begins, and 0x004c4c38 - 0x004c48b8 = 0x380 =
// 7 * 32 * 4. Sized at 5 stages, effect_depth_record's bounds guard returned
// 0xFF for every effect in stages 5 and 6 - and 0xFF is a hard cull - so the
// last two stages had no 2D effects at all. Same truncation as the room sound
// name table (see Room_LoadEnemySoundBanks); when a table is indexed by stage,
// check how many stages it really covers before sizing it.
//
// Two blocks inside stages 1-2 were also wrong and are now corrected:
//   - stage 1 room 0x1E was 0x09, is 0x01
//   - the whole stage 2 block was shifted two rooms early, so every stage 2
//     room read a neighbour's sprite set. The visible casualty was room 2, the
//     courtyard: it owns {0x0E,0x0F,0x10} = esp212/esp213/esp214, the waterfall
//     sheets, and was reading room 4's {0x04} instead - so the waterfall had no
//     art to draw and simply never appeared. Rooms 0x1D and 0x1F were also off.
// If this table is ever edited again, re-diff it against the exe rather than
// hand-counting rows: a whole-row shift reads as plausible data everywhere.
extern const unsigned char g_RoomEffectSpriteTable[7 * 32 * 4] = {
    // Stage 0 (32 rooms x 4 bytes)
    0x00,0x02,0xFF,0xFF, 0x00,0x03,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x04,0xFF,0xFF,
    0x00,0x04,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x04,0xFF,0xFF,
    0x00,0x05,0x06,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x03,0xFF,0xFF,
    0x00,0x07,0x08,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x04,0xFF,0xFF,
    0x00,0x09,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0x0A,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x09,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF,
    // Stage 1 (32 rooms x 4 bytes)
    0x00,0x01,0xFF,0xFF, 0x00,0x03,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x04,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x02,0xFF,0xFF, 0x00,0x03,0xFF,0xFF,
    0x00,0x04,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x0B,0xFF,0xFF, 0x00,0x04,0x0C,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x03,0xFF,0xFF, 0x00,0x0D,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0x0A,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    // Stage 2 (32 rooms x 4 bytes)
    0x00,0x09,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x0E,0x0F,0x10, 0x00,0x04,0x11,0xFF,
    0x00,0x04,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x04,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x04,0x12,0xFF,
    0x00,0x13,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x14,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    // Stage 3 (32 rooms x 4 bytes)
    0x00,0x01,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x13,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x04,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x04,0xFF,0xFF,
    0x00,0x15,0x16,0xFF, 0x00,0x18,0xFF,0xFF, 0x00,0x18,0xFF,0xFF, 0x00,0x18,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x19,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    // Stage 4 (32 rooms x 4 bytes)
    0x00,0x19,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x03,0x1A,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x03,0x1A,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x19,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x19,0xFF,0xFF,
    0x00,0x1B,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x04,0x1C,0xFF,
    0x00,0x04,0x1C,0xFF, 0x00,0x04,0x1C,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0x1D,0x1E,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    // Stage 5 (32 rooms x 4 bytes)
    0x00,0x01,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x04,0xFF,0xFF,
    0x00,0x04,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x04,0xFF,0xFF,
    0x00,0x13,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x04,0xFF,0xFF,
    0x00,0x07,0x08,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x04,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0x0A,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    // Stage 6 (32 rooms x 4 bytes)
    0x00,0x01,0xFF,0xFF, 0x00,0x03,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x04,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x03,0xFF,0xFF,
    0x00,0x04,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x0B,0xFF,0xFF, 0x00,0x04,0x0C,0xFF,
    0x00,0x01,0x1F,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x03,0xFF,0xFF, 0x00,0x0D,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0x0A,0xFF, 0x00,0x04,0xFF,0xFF,
    0x00,0x04,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x04,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x04,0xFF,0xFF,
    0x00,0x04,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
};

// ============================================================================
// Effect sprite name table (0x004c4c38)
// 8 bytes per entry (padded name). Index from g_RoomEffectSpriteTable values.
// ============================================================================
static const char g_EffectSpriteNames[][8] = {
    "esp000", "esp001", "esp200", "esp201", "esp202", "esp203",
    "esp204", "esp205", "esp206", "esp207", "esp208", "esp209",
    "esp210", "esp211", "esp212", "esp213", "esp214", "esp215",
    "esp216", "esp217", "esp218", "esp219", "esp220", "esp221",
    "esp222", "esp223", "esp225", "esp226", "esp227", "esp228",
    "esp229", "esp230",
};

// ============================================================================
// Effect sprite texture config table (0x004c4f50)
// 8 bytes per entry: [mode (4 bytes)] [data pointer (4 bytes)]
// ============================================================================
struct EffSpriteTexConfig {
    int   mode;
    void* dataPtr;
};

static const EffSpriteTexConfig g_EffectSpriteTexConfig[] = {
    { 4, (void*)0x004C4D38 }, { 4, (void*)0x004C4D48 }, { 5, (void*)0x004C4D58 },
    { 7, (void*)0x004C4D70 }, { 6, (void*)0x004C4D90 }, { 7, (void*)0x004C4DA8 },
    { 2, (void*)0x004C4DC8 }, { 5, (void*)0x004C4DD0 }, { 1, (void*)0x004C4DE4 },
    { 5, (void*)0x004C4DE8 }, { 1, (void*)0x004C4DFC }, { 5, (void*)0x004C4E00 },
    { 1, (void*)0x004C4E14 }, { 7, (void*)0x004C4E18 }, { 6, (void*)0x004C4E38 },
    { 1, (void*)0x004C4E50 }, { 1, (void*)0x004C4E54 }, { 3, (void*)0x004C4E58 },
    { 1, (void*)0x004C4E64 }, { 5, (void*)0x004C4E68 }, { 5, (void*)0x004C4E80 },
    { 5, (void*)0x004C4E98 }, { 1, (void*)0x004C4EAC }, { 5, (void*)0x004C4EB0 },
    { 6, (void*)0x004C4EC8 }, { 6, (void*)0x004C4EE0 }, { 2, (void*)0x004C4EF8 },
    { 7, (void*)0x004C4F00 }, { 2, (void*)0x004C4F20 }, { 1, (void*)0x004C4F28 },
    { 6, (void*)0x004C4F30 }, { 1, (void*)0x004C4F48 },
};

// ============================================================================
// FUN_0047bbe0 (0x0047bbe0) - Load effect sprite data from RDT
// Iterates through effect animation index table, resolves sprite info and
// animation data pointers relative to the RDT base.
// Returns the index of the last valid entry (or 8 if all valid).
// ============================================================================
unsigned char load_effect_sprite_data(unsigned char* effectAnimIndex, unsigned char* effectAnimData, void* rdtBase, unsigned char startSlot)
{
    unsigned char lastValid = 8;
    unsigned char i = 0;
    do {
        unsigned int idx = (unsigned int)i;
        unsigned char spriteIdx = effectAnimIndex[idx];
        g_abEffSpriteIndexTable[startSlot + idx] = spriteIdx;
        if (spriteIdx == 0xFF) {
            lastValid = i;
            i = 8;
        } else {
            i = i + 1;
            unsigned int si = (unsigned int)spriteIdx;
            int dataOffset = *(int*)(effectAnimData - idx * 4);
            g_effectSpriteInfo[si] = (DWORD)rdtBase + dataOffset;
            g_effectAnimData[si] = (DWORD)rdtBase + dataOffset;
            unsigned char* spriteInfo = (unsigned char*)g_effectSpriteInfo[si];
            g_effectAnimData[si] = g_effectAnimData[si] +
                ((unsigned int)spriteInfo[2] + (unsigned int)spriteInfo[0]) * 4 + 8;
        }
    } while (i < 8);
    return lastValid;
}

// The OG weapon-FX sheet layout inside the Effspr TIM pages. Declared here so
// setup_effect_sprite_textures (below) and the baker (further down) share it;
// the full layout comment lives next to load_shoot_direction_data.
extern const unsigned char kWeaponSheetPage[8];
extern const unsigned short kWeaponSheetPageV[8];
extern const int kWeaponSheetShiftedSrv;          // also read by EffectSystem.cpp
extern const unsigned char kEffectTexVHackShift;

// ============================================================================
// FUN_0047bc80 (0x0047bc80) - Set up effect sprite texture pages
//
// Packs every declared effect sprite into 256-tall texture pages. `curU` is
// misleadingly named: it is the V cursor down the page, advanced by the sprite's
// V extent (header.field_0A) and wrapped at 0x100, and `texY` is the PAGE index
// (it increments on each wrap). At 0x0047bdc6 the original then adds curU to
// byte +1 of every one of the sprite's UV records - and byte +1 is texV
// (effect_submit_sprite reads uv[0]=U, uv[1]=V, uv[2]/uv[3]=pivot). So a
// sprite's UVs ship LOCAL to its own image and this turns them into
// page-absolute coordinates.
//
// The two blocks reach the D3D11 renderer through different upload models, and
// that is why the V offset applies to only one of them:
//
//   startSlot 0  (weapon FX, core00): load_shoot_direction_data uploads ONE SRV
//                per sprite from that sprite's own core00.etm image
//                (DAT_00ac9cd0[slot] -> SRV 3+slot). UVs stay local, so adding
//                curU would push them off the sheet - that is the smoke bug an
//                earlier pass hit when it added the offset unconditionally.
//
//   startSlot 8  (room): load_effect_sprites uploads one SRV per effspr TIM
//                PAGE (up to 4 -> SRV 11..14), and several sprites SHARE a
//                page. Here the sprite's page and its V offset both matter.
//                Every effspr*.tim is 256x256, and rooms declare up to 7
//                sprites against as few as 2 pages (ROOM1010 declares types
//                3, 4, 32 against esp000 + esp201), so a per-sprite mapping is
//                simply not available.
//
// The old code recorded `slot + startSlot` as the sheet for both blocks, so a
// room's first declared sprite went to page 0 = esp000. esp000 is the GUNFIRE
// sheet (glass, smoke, muzzle flash, sparks); the blood-splatter frames the
// zombie head FX wants are on page 1 (esp001 / esp201). Both the page index and
// the V offset come out of the running texY / curU cursors below.
// ============================================================================
void setup_effect_sprite_textures(unsigned char startSlot)
{
    unsigned char texY;
    short texX;
    short pageRow;
    unsigned short curU;
    unsigned short curV;

    if (startSlot == 8) {
        texY = DAT_00bf0a38;
        texX = DAT_00bf0a3c;
        pageRow = DAT_00bf0a40;
    } else {
        texY = 0x18;
        texX = 0;
        DAT_00bf0a38 = 0x18;
        DAT_00bf0a3e = 0;
        pageRow = 0;
        DAT_00bf0a42 = 4;
    }

    unsigned char slot = 0;
    curV = DAT_00bf0a42;
    curU = DAT_00bf0a3e;
    DAT_00bf0a3c = texX;
    DAT_00bf0a40 = pageRow;

    // texY IS the page index, biased by 0x18 - the same `texY - 0x18` the
    // original uses as its texture id. It runs across BOTH blocks: the weapon
    // pass starts it at 0x18 and the room pass resumes from where that left off.
    //
    // That bias is the whole answer to which effspr TIM a room sprite lands in.
    // esp000 (page 0) holds exactly the eight core00-declared weapon FX sprites -
    // glass, smoke, muzzle flash, sparks - stacked down its 256 rows, so the V
    // cursor reaches ~256 by the end of the weapon pass and the FIRST room sprite
    // wraps to page 1 at V=3. That is why esp001/esp201, the blood-splatter
    // sheets, begin their first row at y=3.
    //
    // Measuring the page from the start of the room block instead put the zombie
    // head FX on page 0 and it drew muzzle-flash frames.
    const unsigned char PAGE_BIAS = 0x18;

    do {
        unsigned char spriteIdx = g_abEffSpriteIndexTable[slot + startSlot];
        if (spriteIdx == 0xFF) break;

        TmdTextureHeader header;
        ParseTmdTextureHeader((void*)(DAT_00ac9cd0[slot] + 4), &header);

        unsigned short texW = header.field_0A;
        unsigned short texH = header.field_16;

        if ((unsigned int)texW + (unsigned int)curU > 0x100) {
            curU = 3;
            texY = texY + 1;
            texX = texX + 0x40;
        }
        if ((unsigned int)texH + (unsigned int)curV > 0x1F) {
            curV = 4;
            pageRow = pageRow + 1;
        }

        // 0x0047bd82: the original calls 0x00483510 here, a stub that just
        // returns 0 - call dropped

        unsigned short* spriteInfo = (unsigned short*)g_effectSpriteInfo[spriteIdx];
        spriteInfo[2] = curV * 0x40 + pageRow + 0x7810;
        *((unsigned char*)(spriteInfo + 3)) = texY;

        // The D3D11 renderer resolves an effect sprite's texture through this
        // table (see effect_submit_sprite), not through the depth-derived
        // texture id. Port-only bookkeeping - see the header note for why the
        // two blocks index it differently.
        if (startSlot == 0) {
            // Weapon FX: one SRV per sheet, UVs local to it. The band scan
            // needs the sheet's PAGE-ABSOLUTE V - the OG page position the
            // band tables (g_EffectBlendTable) were authored against, NOT the
            // port's packing cursor (with a local v of 0 and the cursor value
            // here, sheets 2/5/6/7 landed in the wrong band and got the wrong
            // blend mode and colour record). See kWeaponSheetPageV for the
            // verified positions.
            g_effectSpriteSheetSlot[spriteIdx] = slot;
            g_effectSpriteBandV[spriteIdx] = (unsigned char)kWeaponSheetPageV[slot];
        } else {
            // Room: one SRV per shared TIM page. load_effect_sprites only ever
            // uploads 4 pages, but 8 of the 320 RDTs declare enough sprites to
            // reach a 5th (ROOM5130 declares seven). Those used to resolve to
            // SRV 15+, which belongs to the menu/item images - a real texture,
            // so it drew a menu graphic instead of failing. Mark them unmapped
            // (0xFF) so effect_submit_sprite skips them, and say so.
            unsigned char page = (unsigned char)(texY - PAGE_BIAS);
            if (page > 3) {
                dbg_printf("[effspr] room sprite type %u wants page %u but only "
                           "4 effspr pages are loaded - effect skipped\n",
                           (unsigned int)spriteIdx, (unsigned int)page);
                g_effectSpriteSheetSlot[spriteIdx] = 0xFF;
            } else {
                g_effectSpriteSheetSlot[spriteIdx] = (unsigned char)(startSlot + page);
            }

            // 0x0047bdb4-0x0047bdd1: uvPtr = spriteInfo + 8 + spriteInfo[1]*4,
            // then `ADD byte ptr [EDI+1],BL` over spriteInfo[0] records of 4
            // bytes - the V byte of each record gains this sprite's V offset in
            // the page. Only meaningful for a shared page, hence room-only.
            //
            // Like the original this edits the RDT buffer in place, so it is
            // only correct once per RDT load; the caller (the room effect init)
            // runs load_effect_sprite_data immediately before it, which
            // re-resolves these pointers into the freshly read RDT.
            unsigned char* uvPtr =
                (unsigned char*)(spriteInfo + spriteInfo[1] * 2 + 4);
            unsigned int uvCount = spriteInfo[0];
            unsigned char vAdd = (unsigned char)curU;
            for (unsigned int u = 0; u < uvCount; u++) {
                uvPtr[u * 4 + 1] = (unsigned char)(uvPtr[u * 4 + 1] + vAdd);
            }
            // The page V offset is carried in g_effectSpritePageV so
            // load_effect_sprites can blit this sprite's RDT-embedded TIM (the
            // per-room art with its own CLUT) back into the page at the same
            // place the UVs point. The band scan keeps reading the edited
            // (page-absolute) texV, so bandV itself stays 0 for room sprites.
            g_effectSpritePageV[spriteIdx] = (unsigned char)curU;
            g_effectSpriteBandV[spriteIdx] = 0;
        }

        curU = curU + texW;
        slot = slot + 1;
        curV = curV + (texH & 0xFF);
    } while (slot < 8);

    if (startSlot == 0) {
        DAT_00bf0a38 = texY;
        DAT_00bf0a3c = texX;
        DAT_00bf0a40 = pageRow;
        DAT_00bf0a3e = curU;
        DAT_00bf0a42 = curV;
    }
}

// Re-bake the four page-1 weapon sheets from the room's page-1 Effspr file
// (defined below, next to load_shoot_direction_data).
void RebakeWeaponSheetsForRoom(void);

// ============================================================================
// load_effect_sprites (0x0047d020) - Build room effect sprite texture pages
//
// The room's effect sprites are TIMs embedded in the RDT (DAT_00ac9cd0,
// resolved by InitRoomEffSprite). Each carries the PER-ROOM art with its own
// CLUT - the flooded rooms' type-0x17 ripple is a light-blue 64x64 sheet that
// exists ONLY in the RDT (the shared effspr\esp*.tim files hold the default
// warm art, which is why the port's water rendered in the wrong palette).
//
// setup_effect_sprite_textures already placed every declared sprite at page
// (spriteInfo[3] - 0x18) and page-V g_effectSpritePageV; here each TIM is
// converted with ITS OWN CLUT (4bpp, CLUT row 0) and blitted into a 256x256
// page buffer at that offset. The renderer keeps sampling page-absolute UVs,
// so nothing else in the effect path changes.
// ============================================================================
static void blit_effect_tim_at(DWORD* page, const unsigned char* tim,
                               unsigned int yOff, unsigned int clutRow)
{
    if (tim == NULL) return;
    if (*(const unsigned int*)tim != 0x10) return;   // TIM magic

    const unsigned char* p = tim + 8;
    unsigned int flags = *(const unsigned int*)(tim + 4);
    unsigned short clutW = 0, clutH = 0;
    if (flags & 8) {
        p += 4;                                      // CLUT data size
        p += 4;                                      // CLUT origin
        clutW = *(const unsigned short*)p;
        clutH = *(const unsigned short*)(p + 2);
        p += 4;
    }
    if (clutW == 0 || clutH == 0 || clutW * clutH > 64) return;

    // 4bpp sheets: 16-entry CLUT rows, entry 0 = transparent. The rows are
    // palette VARIANTS of the same art (see load_effect_sprites); row 0 is the
    // base palette and the effect renderer picks the row matching the spawn's
    // tint index from the per-row SRVs.
    if (clutRow >= clutH) clutRow = clutH - 1;
    p += (int)clutRow * (clutW * 2);
    unsigned short clut[16];
    for (int i = 0; i < 16 && i < clutW; i++) {
        clut[i] = *(const unsigned short*)(p + i * 2);
    }
    p -= (int)clutRow * (clutW * 2);
    p += clutW * clutH * 2;

    p += 4;                                          // image data size
    p += 4;                                          // image origin
    unsigned short imgW = *(const unsigned short*)p; // width in 16-bit words
    unsigned short imgH = *(const unsigned short*)(p + 2);
    p += 4;
    if (imgW == 0 || imgH == 0 || imgW > 64 || imgH > 0x100) return;

    unsigned int w = (unsigned int)imgW * 4;         // 4bpp -> 4 px per word
    unsigned int h = imgH;
    if (w == 0 || h == 0) return;

    // Blit into the page at (0, yOff); the page is 256x256.
    if (yOff >= 256) return;
    unsigned int hClip = (h + yOff <= 256) ? h : (256 - yOff);
    for (unsigned int y = 0; y < hClip; y++) {
        for (unsigned int x = 0; x < w; x++) {
            const unsigned char* src = p + y * imgW * 2 + x / 2;
            unsigned int idx = (x & 1) ? ((*src >> 4) & 0xF) : (*src & 0xF);
            if (idx == 0) continue;                  // transparent key
            unsigned short c = clut[idx];
            unsigned int r = ((c >> 0)  & 0x1F) * 255 / 31;
            unsigned int g = ((c >> 5)  & 0x1F) * 255 / 31;
            unsigned int b = ((c >> 10) & 0x1F) * 255 / 31;
            page[(y + yOff) * 256 + x] = 0xFF000000u | (b << 16) | (g << 8) | r;
        }
    }
}

// ---------------------------------------------------------------------------
// HD names for the room's effect pages (port-only, HdTextures.h).
//
// The pages composited above are the room's Effspr page files: page p is
// g_EffectSpriteNames[room_effect_page_entry()[p]], the same row the effect
// renderer reads, and the RDT's TIMs are that file's art at the same place
// (ESP204/208/213/214/220/227/228 came out texel for texel in a dump of the
// mansion, the lab and the Tyrant room). Classic REbirth's packs replace the
// file's whole page, and the asset migrator installs that picture as
// HD/<TREE>/Effspr/espNNN.webp - which until now only the weapon bands
// (BakeWeaponSheetFromPage) were named by, so the glass bursts and the Tyrant
// tube's cracks stayed low-res. A room may draw one sprite in another palette
// row than the file (ESP203's and ESP229's flare / drop rows): the page source
// then draws those texels from the composite, and a page holding a texel the
// file does not have at all is not named.
// ---------------------------------------------------------------------------
// The path of Effspr page file `nameIdx` (a g_RoomEffectSpriteTable byte):
// false for 0xFF and anything past the name table. Also used to queue the
// room's pages for decoding (RoomBackgroundWebp.cpp, room_hd_prefetch_early).
bool effspr_page_path(unsigned char nameIdx, char* path, size_t pathSize)
{
    if (nameIdx >= sizeof(g_EffectSpriteNames) / sizeof(g_EffectSpriteNames[0])) return false;
    snprintf(path, pathSize, GAME_DATA_ROOT "effspr\\%s.tim", g_EffectSpriteNames[nameIdx]);
    return true;
}

static int render_effspr_page(unsigned char nameIdx, DWORD* out, char* path, size_t pathSize)
{
    if (!effspr_page_path(nameIdx, path, pathSize)) return 0;
    const unsigned char* tim = (const unsigned char*)g_TimImageBuffer;
    unsigned int size = LoadFile(path, (void*)tim, 0x20);
    // 8 bpp with a 256 x 1 CLUT and a 256 x 256 image: the layout every
    // shipped Effspr file has (see BakeWeaponSheetFromPage).
    if (size < 20 + 512 + 12 + 256 * 256) return 0;
    if (*(const unsigned int*)tim != 0x10 || *(const unsigned int*)(tim + 4) != 9) return 0;
    if (*(const unsigned short*)(tim + 16) != 256 || *(const unsigned short*)(tim + 18) != 1) return 0;
    const unsigned short* clut = (const unsigned short*)(tim + 20);
    const unsigned char* img = tim + 20 + 512;
    if (*(const unsigned short*)(img + 8) != 128 || *(const unsigned short*)(img + 10) != 256) return 0;
    const unsigned char* pix = img + 12;
    for (int i = 0; i < 256 * 256; i++) {
        unsigned int idx = pix[i];
        if (idx == 0) { out[i] = 0; continue; }       // the same key as the blit
        unsigned short c = clut[idx];
        unsigned int r = ((c >> 0)  & 0x1F) * 255 / 31;
        unsigned int g = ((c >> 5)  & 0x1F) * 255 / 31;
        unsigned int b = ((c >> 10) & 0x1F) * 255 / 31;
        out[i] = 0xFF000000u | (b << 16) | (g << 8) | r;
    }
    return 1;
}

// True when every texel `page` draws is drawn by the file too.
static bool effspr_page_within_file(const DWORD* page, const DWORD* file)
{
    for (int i = 0; i < 256 * 256; i++) {
        if (page[i] != 0 && file[i] == 0) return false;
    }
    return true;
}

// Port-only: g_effectSpriteStpRows' mask for one sheet TIM. A row counts as
// STP when most of its non-zero entries (0x0000 is the transparent key and
// never drawn) carry bit 15. The shipped rows are all-or-nothing apart from a
// stray black entry, so the majority is the row's rule.
static unsigned char effect_tim_stp_rows(const unsigned char* tim)
{
    if (tim == NULL || *(const unsigned int*)tim != 0x10) return 0;
    if ((*(const unsigned int*)(tim + 4) & 8) == 0) return 0;     // no CLUT
    unsigned int clutW = *(const unsigned short*)(tim + 16);
    unsigned int clutH = *(const unsigned short*)(tim + 18);
    if (clutW == 0 || clutW > 256) return 0;
    if (clutH > 4) clutH = 4;
    const unsigned short* clut = (const unsigned short*)(tim + 20);
    unsigned char mask = 0;
    for (unsigned int r = 0; r < clutH; r++) {
        int stp = 0, opaque = 0;
        for (unsigned int i = 1; i < clutW; i++) {
            unsigned short c = clut[r * clutW + i];
            if (c == 0) continue;
            if (c & 0x8000) stp++; else opaque++;
        }
        if (stp > opaque) mask |= (unsigned char)(1u << r);
    }
    return mask;
}

static void load_effect_sprites(void)
{
    // The room's esp sprites use sheet slots 8-11 (the weapon FX hold 0-7),
    // mapped to SRV slots 11-14. Free exactly those four on each room change,
    // plus the per-row variant pages at 152..167 (see below).
    for (int i = 0; i < 4; i++) {
        int slot = 11 + i;
        if (g_TexturePageSRV[slot] != MARNI_NULL_HANDLE) {
            if (Marni_DX() != NULL) Marni_DX()->DestroyTexture(g_TexturePageSRV[slot]);
            g_TexturePageSRV[slot] = MARNI_NULL_HANDLE;
        }
    }
    for (int i = 0; i < 16; i++) {
        int slot = 152 + i;
        if (g_TexturePageSRV[slot] != MARNI_NULL_HANDLE) {
            if (Marni_DX() != NULL) Marni_DX()->DestroyTexture(g_TexturePageSRV[slot]);
            g_TexturePageSRV[slot] = MARNI_NULL_HANDLE;
        }
    }

    // Record each declared sprite's CLUT row count. Room esp TIMs are 4bpp with
    // up to four 16-entry CLUT rows, and the rows are palette VARIANTS of the
    // same art selected per spawn by the tint index (clutY =
    // depthGroup >> 3) - exactly like the weapon-FX sheets in core00.etm (the
    // Plant 42 white-blood fix). ROOM5080's passcode light is the room-side
    // proof: its sheet ships row 0 red / row 1 cyan-blue / row 2 orange /
    // row 3 bright blue, the init SCD spawns the red instances (depthGroup
    // 0x00-0x02) and the main SCD re-spawns the same type with depthGroup
    // 0x08-0x0A when a pass code is entered - one art block, one palette per
    // tint. clutW/clutH live at TIM+16/+18 (the +24/+26 misread made the first
    // LoadEffectTextureSheetVariants attempt a silent no-op - see that note).
    for (unsigned int slot = 0; slot < 8; slot++) {
        unsigned char type = g_abEffSpriteIndexTable[8 + slot];
        if (type == 0xFF) continue;
        unsigned int rows = 1;
        const unsigned char* tim = (const unsigned char*)DAT_00ac9cd0[slot];
        if (tim != NULL && *(const unsigned int*)tim == 0x10 &&
            (*(const unsigned int*)(tim + 4) & 8)) {
            unsigned int clutH = *(const unsigned short*)(tim + 18);
            if (clutH > 4) clutH = 4;
            if (clutH > 1) rows = clutH;
        }
        g_effectSpriteClutRows[type] = (unsigned char)rows;
        g_effectSpriteStpRows[type] = effect_tim_stp_rows(tim);
    }

    // Composite each declared room sprite into its page buffer (CLUT row 0 -
    // the base palette, uploaded to SRV 11-14 as before).
    static DWORD s_pageBuffer[4][256 * 256];
    for (int i = 0; i < 4; i++) {
        memset(s_pageBuffer[i], 0, sizeof(s_pageBuffer[i]));
    }
    for (unsigned int slot = 0; slot < 8; slot++) {
        unsigned char type = g_abEffSpriteIndexTable[8 + slot];
        if (type == 0xFF) continue;
        unsigned char sheetSlot = g_effectSpriteSheetSlot[type];
        if (sheetSlot == 0xFF) continue;             // page > 3, unmapped
        unsigned int page = sheetSlot - 8;
        if (page >= 4) continue;
        // Blit at the sprite's page V (g_effectSpritePageV); the UV records
        // were already made page-absolute with the same offset, so the render
        // samples exactly the blitted region.
        blit_effect_tim_at(s_pageBuffer[page],
                           (const unsigned char*)DAT_00ac9cd0[slot],
                           g_effectSpritePageV[type], 0);
    }

    // Port-only: each page's Effspr file as the game would draw it, the HD
    // reference for the uploads below (see render_effspr_page).
    static DWORD s_pageFile[4][256 * 256];
    char hdKey[4][160];
    bool hdFile[4] = { false, false, false, false };
    if (HdTextures_Active()) {
        const unsigned char* entry = room_effect_page_entry();
        for (int i = 0; i < 4; i++) {
            char path[256];
            hdKey[i][0] = '\0';
            if (entry[i] == 0xFF || !render_effspr_page(entry[i], s_pageFile[i], path, sizeof(path)))
                continue;
            HdTextures_KeyForPath(path, hdKey[i], sizeof(hdKey[i]));
            hdFile[i] = hdKey[i][0] != '\0';
        }
    }

    // Upload the pages that got content (some pages may stay empty).
    for (int i = 0; i < 4; i++) {
        int slot = 11 + i;
        bool hasContent = false;
        for (int px = 0; px < 256 * 256; px++) {
            if (s_pageBuffer[i][px] != 0) { hasContent = true; break; }
        }
        if (hasContent) {
            const bool named = hdFile[i] && effspr_page_within_file(s_pageBuffer[i], s_pageFile[i]);
            HdPageScope hdSource(named ? hdKey[i] : NULL, 256, 256, s_pageFile[i]);
            MarniCreateTexture(256, 256, 32, s_pageBuffer[i], &g_TexturePageSRV[slot]);
            g_TexturePageWidth[slot] = 256;
            g_TexturePageHeight[slot] = 256;
            g_TexturePageBpp[slot] = 16;
        }
    }

    // Bake the variant rows. Row r (r >= 1) of a page composites every sprite
    // whose sheet declares more than r rows and lands in
    // SRV 152 + page*4 + (r-1); the renderer redirects there on the matching
    // tint (see effect_submit_sprite). Single-row sheets are untouched, so
    // rooms whose art ships one palette behave exactly as before.
    for (unsigned int row = 1; row < 4; row++) {
        bool anyRow = false;
        for (int i = 0; i < 4; i++) {
            memset(s_pageBuffer[i], 0, sizeof(s_pageBuffer[i]));
        }
        for (unsigned int slot = 0; slot < 8; slot++) {
            unsigned char type = g_abEffSpriteIndexTable[8 + slot];
            if (type == 0xFF) continue;
            unsigned char sheetSlot = g_effectSpriteSheetSlot[type];
            if (sheetSlot == 0xFF) continue;
            unsigned int page = sheetSlot - 8;
            if (page >= 4) continue;
            if ((unsigned int)g_effectSpriteClutRows[type] <= row) continue;
            blit_effect_tim_at(s_pageBuffer[page],
                               (const unsigned char*)DAT_00ac9cd0[slot],
                               g_effectSpritePageV[type], row);
            anyRow = true;
        }
        if (!anyRow) break;
        for (int i = 0; i < 4; i++) {
            bool hasContent = false;
            for (int px = 0; px < 256 * 256; px++) {
                if (s_pageBuffer[i][px] != 0) { hasContent = true; break; }
            }
            if (hasContent) {
                int slot = 152 + i * 4 + (int)row - 1;
                const bool named = hdFile[i] &&
                                   effspr_page_within_file(s_pageBuffer[i], s_pageFile[i]);
                HdPageScope hdSource(named ? hdKey[i] : NULL, 256, 256, s_pageFile[i]);
                MarniCreateTexture(256, 256, 32, s_pageBuffer[i], &g_TexturePageSRV[slot]);
                g_TexturePageWidth[slot] = 256;
                g_TexturePageHeight[slot] = 256;
                g_TexturePageBpp[slot] = 16;
            }
        }
    }

    // The weapon-FX sheets 4-7 (blood, ember, glass, 0xb) live on the page-1
    // Effspr file, which is a per-room palette variant (esp001 base / esp2xx).
    // The OG re-loads that page every room, so re-bake those four SRVs from
    // this room's page-1 file to pick up the room's palette (stage lighting).
    RebakeWeaponSheetsForRoom();

    STAGE_ID_00ac9cf0 = (unsigned int)g_stageId;
    ROOM_ID_00ac9cf4 = (unsigned int)g_roomId;
}

// ============================================================================
// load_shoot_direction_data (0x0045fa80)
// Loads the GLOBAL weapon-FX sprite file core00.esp/.etm into effect sprite
// slots 0-7 (the "shoot direction" sprites: muzzle flashes, fire billboards,
// lock-on fan, sparks). Runs once at InitializeGame; the per-room RDT effect
// table covers slots 8-15. With this a stub, the weapon FX types (5, 8, 9,
// 0xb, 0xc, 0xe, 0x11, 0) stayed 0xffffffff in every room and every muzzle
// flash / fire billboard was skipped as "not loaded for this room".
// ============================================================================

// ---------------------------------------------------------------------------
// The OG weapon-FX sheet layout inside the Effspr TIM pages.
//
// The shipped Effspr\esp*.tim files are 256x256 8bpp pages with a baked
// 256-entry palette; the eight weapon sheets are composited INTO them at
// fixed positions. The OG's own band tables (g_EffectBlendTable, rec 0/1)
// were authored against exactly these positions, and the strict art matcher
// run over esp000/esp001 confirms every region:
//
//   page 0 (esp000, the page-0 file for every room):  type 5 [0,64),
//          smoke 9 [64,176), fire 0xc [176,240), 0x11 [240,256)
//   page 1 (esp001 / the room's esp2xx file):
//          blood 0 [3,27), ember 0xe [27,99), glass 8 [99,123), 0xb [123,147)
//
// The sheet art in core00.etm is the same shapes, but each Effspr file ships
// its own palette mapping (per-stage lighting variants - esp001 vs esp201
// differ in 65 palette entries), so the OG's colours come from the PAGE
// palette, not from core00.etm's CLUT rows. Baking the weapon SRVs straight
// from the page pixels is the faithful serve.
// ---------------------------------------------------------------------------
const unsigned char kWeaponSheetPage[8] = { 0, 0, 0, 0, 1, 1, 1, 1 };
const unsigned short kWeaponSheetPageV[8] = { 0, 64, 176, 240, 3, 27, 99, 123 };

// Port-only: SRVs 168..175 hold each weapon sheet baked kEffectTexVHackShift
// rows further down its page - the art the original's room 70C special case
// (EffectActor_UpdateAndRender 0x0047c2f0) samples. Only baked in that room.
const int kWeaponSheetShiftedSrv = 168;
const unsigned char kEffectTexVHackShift = 0x7b;

// Bake weapon sheet `slot` (its SRV lives at 3+slot) from the Effspr page file
// `fileName` (an esp*.tim name, no extension). Returns 1 on success.
//
// `vShift` / `srvSlot` bake the same sheet-sized band from `vShift` rows further
// down the page into another SRV (see kWeaponSheetShiftedSrv); 0 / -1 is the
// normal bake.
static int BakeWeaponSheetFromPage(int slot, const char* pageName,
                                   unsigned short vShift = 0, int srvSlotArg = -1)
{
    if (slot < 0 || slot >= 8) return 0;
    unsigned char type = g_abEffSpriteIndexTable[slot];
    if (type == 0xFF) return 0;

    char path[256];
    sprintf(path, GAME_DATA_ROOT "effspr\\%s.tim", pageName);
    unsigned char* tim = (unsigned char*)g_TimImageBuffer;
    unsigned int size = LoadFile(path, tim, 0x20);
    if (size == 0) return 0;

    const unsigned char* p = tim;
    if (*(const unsigned int*)p != 0x10) return 0;
    unsigned int flags = *(const unsigned int*)(p + 4);
    if ((flags & 0x3) != 1) return 0;
    p += 8;
    if (!(flags & 8)) return 0;
    p += 4;                                                        // CLUT size
    p += 4;                                                        // CLUT origin
    unsigned short clutW = *(const unsigned short*)p;
    p += 2;
    unsigned short clutH = *(const unsigned short*)p; p += 2;
    if (clutW != 256 || clutH != 1) return 0;
    unsigned short clut[256];
    for (int i = 0; i < 256; i++) {
        clut[i] = *(const unsigned short*)(p + i * 2);
    }
    p += 256 * 2;
    p += 4;                                                        // image size
    p += 4;                                                        // image origin
    // The TIM image width field is in 16-bit HALFWORDS, not pixels (PSX TIM
    // spec; for 8bpp the pixel width is imgW * 2). The shipped Effspr pages are
    // 256 px wide and therefore store 128 here. The original check compared the
    // raw field against 256, so EVERY bake of these files failed at this check
    // and every weapon sheet silently fell back to LoadEffectTextureSheet's
    // core00.etm CLUT-row-0 bake - a RED gore palette.
    unsigned short imgW = *(const unsigned short*)p;
    unsigned short imgH = *(const unsigned short*)(p + 2); p += 4;
    unsigned int pixW = (unsigned int)imgW * 2;                    // 8bpp halfword units
    if (pixW != 256 || imgH != 256) return 0;

    unsigned short pageV = kWeaponSheetPageV[slot];
    unsigned short height = 256 - pageV;                           // rest of the page
    // Clip at the next sheet's top: the regions are packed to page boundaries,
    // but esp files can carry room art right below - stop at the sheet's own
    // height by using the NEXT sheet's pageV when it shares our page.
    unsigned short nextV = 256;
    for (int s = 0; s < 8; s++) {
        if (s != slot && kWeaponSheetPage[s] == kWeaponSheetPage[slot]
            && kWeaponSheetPageV[s] > pageV && kWeaponSheetPageV[s] < nextV) {
            nextV = kWeaponSheetPageV[s];
        }
    }
    if (nextV < height + pageV) height = (unsigned short)(nextV - pageV);

    // The shifted bake reads the same-sized band `vShift` rows lower, so the
    // sheet's local UVs address it unchanged.
    pageV = (unsigned short)(pageV + vShift);
    if (pageV >= 256) return 0;
    if (pageV + height > 256) height = (unsigned short)(256 - pageV);

    static DWORD rgba[256 * 256];
    const unsigned char* pix = p;
    for (unsigned short y = 0; y < height; y++) {
        const unsigned char* src = pix + (pageV + y) * pixW;
        DWORD* row = &rgba[y * 256];
        for (unsigned short x = 0; x < 256; x++) {
            unsigned int idx = src[x];
            if (idx == 0) { row[x] = 0x00000000u; continue; }
            unsigned short c = clut[idx];
            unsigned int r = ((c >> 0)  & 0x1F) * 255 / 31;
            unsigned int g = ((c >> 5)  & 0x1F) * 255 / 31;
            unsigned int b = ((c >> 10) & 0x1F) * 255 / 31;
            row[x] = 0xFF000000u | (b << 16) | (g << 8) | r;
        }
    }

    int srvSlot = (srvSlotArg >= 0) ? srvSlotArg : 3 + slot;
    if (g_TexturePageSRV[srvSlot] != MARNI_NULL_HANDLE) {
        if (Marni_DX() != NULL) Marni_DX()->DestroyTexture(g_TexturePageSRV[srvSlot]);
        g_TexturePageSRV[srvSlot] = MARNI_NULL_HANDLE;
    }
    // Port-only: the band is rows [pageV, pageV + height) of the esp page, so
    // an HD picture of the page, "HD/<TREE>/effspr/espNNN.webp", gives it
    // (HdTextures.h).
    char hdKey[160];
    HdRegionScope hdSource(HdTextures_KeyForPath(path, hdKey, sizeof(hdKey)),
                           0, pageV, 256, height, 256, 256);
    MarniCreateTexture(256, height, 32, rgba, &g_TexturePageSRV[srvSlot]);
    g_TexturePageWidth[srvSlot] = 256;
    g_TexturePageHeight[srvSlot] = height;
    g_TexturePageBpp[srvSlot] = 8;
    return 1;
}

// Re-bake the four page-1 weapon sheets (blood, ember, glass, 0xb) from the
// ROOM's own page-1 Effspr file. The OG loads a per-room variant of that page
// (esp001 base, esp201+ stage variants) and every weapon sprite on it picks up
// that file's palette; without this the blood/embers render with the base
// mansion palette everywhere.
void RebakeWeaponSheetsForRoom(void)
{
    const unsigned int row = (get_stage_id() * 0x20 + (unsigned int)g_roomId) * 4;

    // The shifted copies belong to one room only; drop the last room's.
    for (int slot = 0; slot < 8; slot++) {
        int srv = kWeaponSheetShiftedSrv + slot;
        if (g_TexturePageSRV[srv] != MARNI_NULL_HANDLE) {
            if (Marni_DX() != NULL) Marni_DX()->DestroyTexture(g_TexturePageSRV[srv]);
            g_TexturePageSRV[srv] = MARNI_NULL_HANDLE;
        }
    }

    unsigned char nameIdx = g_RoomEffectSpriteTable[row + 1];
    if (nameIdx == 0xFF) return;
    const char* pageName = (const char*)(g_EffectSpriteNames + nameIdx);
    for (int slot = 4; slot < 8; slot++) {
        BakeWeaponSheetFromPage(slot, pageName);
    }

    // Port-only: room 70C's Yawn dust (see effect_submit_sprite). The original
    // samples the band 0x7B rows below a tint 1/2 sprite on the same page, so
    // bake that band for every weapon sheet, from the page file it lives on.
    if (g_stageId == STAGE_MANSION_RETURN_2F && g_roomId == ROOM_LESSON_ROOM) {
        for (int slot = 0; slot < 8; slot++) {
            unsigned char fileIdx = g_RoomEffectSpriteTable[row + kWeaponSheetPage[slot]];
            if (fileIdx == 0xFF) continue;
            BakeWeaponSheetFromPage(slot, (const char*)(g_EffectSpriteNames + fileIdx),
                                    kEffectTexVHackShift, kWeaponSheetShiftedSrv + slot);
        }
    }
}

// ============================================================================
// apply_dc_shoot_camera_glass_fix - port-added, no original address.
//
// The shoot-the-camera easter egg (weapon_shoot_camera_effect) spawns type 5,
// animation 2 (depthGroup 0x12). In core00.esp that animation has two phases:
//   phase 1: behaviour 0x1F (set frame 0x12), update 0x02 (timer, 0 ticks)
//   phase 2: behaviour 0x36 (effect_behavior_random_spin)
// The 1997 PC release and the PS1 original (SLUS_001.70) ship this data
// as-is, so the glass jitters by +-5/15 units a frame, 512 units from the lens,
// for as long as its sprite animation runs (about a second). The PS1
// Director's Cut changed exactly ONE byte of CORE00.ESP (file offset 0x1E0,
// 0x36 -> 0x00): phase 2's behaviour id becomes 0, which is the pool's "free"
// marker, so the glass shows for one or two frames and never moves. The port
// takes the Director's Cut behaviour in every mode.
//
// It writes 0x2F (effect_behavior_kill) rather than the DC's 0x00. A slot whose
// id is zeroed by a phase change is dropped without g_freeEffectSlots++, so the
// DC data leaks one pool count per glass sprite until the next room load (64
// camera shots in one room and no effect spawns any more). Kill frees the slot
// properly; the glass is drawn on the spawn frame and the one after it.
//
// The patch only touches the loaded buffer, and only when the block has the
// shape and values above, so a modified core00.esp is left alone.
// ============================================================================
static void apply_dc_shoot_camera_glass_fix(void)
{
    if (g_effectAnimData[5] == 0 || g_effectAnimData[5] == 0xFFFFFFFF) return;
    unsigned char* animBase = (unsigned char*)g_effectAnimData[5];
    unsigned int* table = (unsigned int*)(animBase + (unsigned int)animBase[2] * 4);
    if (table[0] != 1 || table[1] != 2) return;            // one slot, two phases
    unsigned char* phase1 = (unsigned char*)&table[2];
    unsigned char* phase2 = phase1 + 0x18;
    if (phase1[0] != 0x1F || phase1[1] != 0x02) return;
    if (phase2[0] != 0x36 && phase2[0] != 0x00) return;   // PC/PS1 OG or DC file
    phase2[0] = 0x2F;
}

void load_shoot_direction_data(void)
{
    g_freeEffectSlots = 0x40;

    // 0x0045fa90: invalidate the whole sprite table and the 16 index slots
    for (int i = 0; i < 16; i++) g_abEffSpriteIndexTable[i] = 0xff;
    for (int i = 0; i < 0x32; i++) {
        g_effectSpriteInfo[i] = 0xffffffff;
        g_effectAnimData[i] = 0xffffffff;
    }

    // 0x0045fb31: load core00.esp; its index table is at the file start and its
    // animation-data offsets hang off the file END, read backward (the same
    // layout load_effect_sprite_data consumes for the room RDT).
    char path[256];
    sprintf(path, GAME_DATA_ROOT "data\\core00.esp");
    unsigned char* espBase = (unsigned char*)g_loadDataDestPointer;
    unsigned int size = LoadFile(path, espBase, 0x20);
    unsigned char* espEnd = espBase + (size & 0xfffffffc) + ((size & 3) ? 4 : 0) - 4;
    g_loadDataDestPointer = espEnd;
    unsigned char lastValid = load_effect_sprite_data(espBase, espEnd, espBase, 0);
    apply_dc_shoot_camera_glass_fix();

    // 0x0045fc0f: load core00.etm; its trailing dwords are per-sprite image
    // offsets relative to the etm buffer.
    g_loadDataDestPointer = g_DataBuffer;
    sprintf(path, GAME_DATA_ROOT "data\\core00.etm");
    size = LoadFile(path, g_DataBuffer, 0x20);
    unsigned char* etmEnd = (unsigned char*)g_DataBuffer + (size & 0xfffffffc)
                          + ((size & 3) ? 4 : 0);
    for (unsigned char i = 0; i < lastValid; i++) {
        DAT_00ac9cd0[i] = (int)g_DataBuffer + *(int*)(etmEnd - 4 - i * 4);
        // Port-only: the PS1 STP rows of each weapon sheet. core00.etm keeps
        // them (it is byte-identical to the PS1 disc's); the Effspr pages the
        // sheets are baked from below do not.
        unsigned char type = g_abEffSpriteIndexTable[i];
        if (type < 50) {
            g_effectSpriteStpRows[type] =
                effect_tim_stp_rows((const unsigned char*)DAT_00ac9cd0[i]);
        }
    }

    setup_effect_sprite_textures(0);

    // Load the eight weapon-FX sheets into dedicated D3D11 texture-page slots
    // so the effect renderer can find them. Slots 3-10 are free (the global
    // textures own 0-2, the menu/item images own 15-30); the room's esp sprites
    // use 11-14 (load_effect_sprites).
    //
    //
    //   page 0 (esp000 - every room): type 5 [0,64), smoke [64,176),
    //          fire [176,240), 0x11 [240,256)
    //   page 1 (esp001 base / the room's esp2xx variant):
    //          blood [3,27), ember [27,99), glass [99,123), 0xb [123,147)
    //
    // These positions are confirmed by the OG's own band tables (rec 0/1 of
    // g_EffectBlendTable) AND by a strict art match against the shipped files.
    // The gore art indices are light; the RGB tint record (blood red/green/
    // white per enemy) is what colors them - the smoke therefore renders its
    // WHITE ramp at tint 1, which is what the original does.
    //
    // The core00.etm bake below is only a fallback (file missing / parse fail):
    // it bakes sheet CLUT row 0, so blood lands red - wrong for hunter/Plant42
    // but better than nothing until the page file shows up.
    static const char* kWeaponSheetPageFile[8] = {
        "esp000", "esp000", "esp000", "esp000",
        "esp001", "esp001", "esp001", "esp001",
    };
    for (int i = 0; i < 8; i++) {
        if (DAT_00ac9cd0[i] == 0) continue;
        if (!BakeWeaponSheetFromPage(i, kWeaponSheetPageFile[i])) {
            LoadEffectTextureSheet(3 + i, (void*)DAT_00ac9cd0[i]);
        }
    }
}

// ============================================================================
// InitRoomEffSprite (0x0047b9b0)
// Initializes room effect sprites: clears the effect pool, loads effect
// animation data from the RDT, sets up texture pages, and loads sprite TIMs.
// ============================================================================
void InitRoomEffSprite(void)
{
    // 0x0047b9b0: Reset effect pool
    g_freeEffectSlots = 64;
    memset(g_effectPool, 0, sizeof(g_effectPool));

    // 0x0047b9c3: invalidate the PREVIOUS room's effect entries before the new
    // room's are loaded.
    //
    // The index is `+ 8`, not bare. The original reads and writes
    // g_abEffSpriteIndexTable[uVar4 + 8], and it has to: load_effect_sprite_data
    // below is called with startSlot = 8, so the room's entries live at [8..15].
    // Slots [0..7] are the shoot-direction entries and belong to nobody here.
    //
    // Without the +8 this loop invalidated the wrong block, so a departing room's
    // g_effectSpriteInfo[] slots kept stale pointers while entries the new room did
    // not declare stayed 0 instead of 0xFFFFFFFF - and Effect_CreateBillboard then
    // dereferenced 0+2, faulting at 0x00000002 inside opcode 0x18.
    unsigned char i = 0;
    do {
        unsigned int idx = (unsigned int)i;
        unsigned char spriteIdx = g_abEffSpriteIndexTable[idx + 8];
        if (spriteIdx == 0xFF) {
            i = 8;
        } else {
            i = i + 1;
            g_effectSpriteInfo[spriteIdx] = 0xFFFFFFFF;
            g_effectAnimData[spriteIdx] = 0xFFFFFFFF;
            g_abEffSpriteIndexTable[idx + 8] = 0xFF;
            // Port-only companion tables - keep them in step so a departing
            // room's sheet mapping cannot be reached from the next room.
            g_effectSpriteSheetSlot[spriteIdx] = 0xFF;
            g_effectSpritePageV[spriteIdx] = 0;
            g_effectSpriteClutRows[spriteIdx] = 0;
            g_effectSpriteStpRows[spriteIdx] = 0;
        }
    } while (i < 8);

    // 0x0047ba1d: Load room effect animation data from RDT
    load_effect_sprite_data(g_RdtPointer->effect_anim_index, g_RdtPointer->effect_anim_data, g_RdtPointer, 8);

    // 0x0047ba41: Compute effect sprite image data pointers
    RDT* pRdt = g_RdtPointer;
    i = 0;
    unsigned char* spriteImBase = pRdt->effect_anim_sprite;
    do {
        unsigned int idx = (unsigned int)i;
        i = i + 1;
        // (RDT+3)-3 in the decompile: the offsets are relative to the RDT base.
        DAT_00ac9cd0[idx] = (int)pRdt + *(int*)(spriteImBase - idx * 4);
    } while (i < 8);

    // 0x0047ba5c: Set up effect sprite texture positions
    setup_effect_sprite_textures(8);

    // 0x0047ba64: Load room effect sprite TIM files
    load_effect_sprites();
}
