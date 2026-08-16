// RoomStubs.cpp - Room initialization functions (decompiled from Ghidra)
#include "../Globals.h"
#include "FileLoader.h"
#include "SpriteRenderer.h"
#include "../marni/MarniDX.h"
#include "../marni/MarniSystem.h"
#include <cstdio>
#include <cstring>
#include "../system/AssetPath.h"
#include "../DebugPrint.h"

// Forward declarations for functions defined in other files
extern void SetAnimSlot(AnimSlot* slots, int slotPtr, int index);
extern unsigned int* CreateAnimObject(int slotPtr, unsigned int* param2);
extern void SetSpriteBufferFlag(void);

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

// Forward declaration for ParseTmdTextureHeader (defined in EntityModelLoader.cpp)
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

        empty_0047b950(0);

        unsigned short* spriteInfo = (unsigned short*)g_effectSpriteInfo[spriteIdx];
        spriteInfo[2] = curV * 0x40 + pageRow + 0x7810;
        *((unsigned char*)(spriteInfo + 3)) = texY;

        // The D3D11 renderer resolves an effect sprite's texture through this
        // table (see effect_submit_sprite), not through the depth-derived
        // texture id. Port-only bookkeeping - see the header note for why the
        // two blocks index it differently.
        if (startSlot == 0) {
            // Weapon FX: one SRV per sprite, UVs already local to it. Carry the
            // page V separately - it is what picks the blend/colour band, and
            // with a local v of 0 every weapon sprite fell into band 0
            // (colorIdx 0 = 0xffffff), which is why blood came out grey.
            g_effectSpriteSheetSlot[spriteIdx] = slot;
            g_effectSpriteBandV[spriteIdx] = (unsigned char)curU;
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
                               unsigned int yOff)
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

    // 4bpp sheets: CLUT row 0 (16 entries), entry 0 = transparent.
    unsigned short clut[16];
    for (int i = 0; i < 16 && i < clutW * clutH; i++) {
        clut[i] = *(const unsigned short*)(p + i * 2);
    }
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

static void load_effect_sprites(void)
{
    // The room's esp sprites use sheet slots 8-11 (the weapon FX hold 0-7),
    // mapped to SRV slots 11-14. Free exactly those four on each room change.
    for (int i = 0; i < 4; i++) {
        int slot = 11 + i;
        if (g_TexturePageSRV[slot] != MARNI_NULL_HANDLE) {
            if (Marni_DX() != NULL) Marni_DX()->DestroyTexture(g_TexturePageSRV[slot]);
            g_TexturePageSRV[slot] = MARNI_NULL_HANDLE;
        }
    }

    // Composite each declared room sprite into its page buffer.
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
                           g_effectSpritePageV[type]);
    }

    // Upload the pages that got content (some pages may stay empty).
    for (int i = 0; i < 4; i++) {
        int slot = 11 + i;
        bool hasContent = false;
        for (int px = 0; px < 256 * 256; px++) {
            if (s_pageBuffer[i][px] != 0) { hasContent = true; break; }
        }
        if (hasContent) {
            MarniCreateTexture(256, 256, 32, s_pageBuffer[i], &g_TexturePageSRV[slot]);
            g_TexturePageWidth[slot] = 256;
            g_TexturePageHeight[slot] = 256;
            g_TexturePageBpp[slot] = 16;
        }
    }

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

    // 0x0045fc0f: load core00.etm; its trailing dwords are per-sprite image
    // offsets relative to the etm buffer.
    g_loadDataDestPointer = g_DataBuffer;
    sprintf(path, GAME_DATA_ROOT "data\\core00.etm");
    size = LoadFile(path, g_DataBuffer, 0x20);
    unsigned char* etmEnd = (unsigned char*)g_DataBuffer + (size & 0xfffffffc)
                          + ((size & 3) ? 4 : 0);
    for (unsigned char i = 0; i < lastValid; i++) {
        DAT_00ac9cd0[i] = (int)g_DataBuffer + *(int*)(etmEnd - 4 - i * 4);
    }

    setup_effect_sprite_textures(0);

    // Load the eight weapon-FX sheets (core00.etm) into dedicated D3D11
    // texture-page slots so the effect renderer can find them - the ORIGINAL
    // keeps the sheets resident in VRAM; the port must upload each sheet to
    // its SRV slot. Slots 3-10 are free (the global textures own 0-2, the
    // menu/item images own 15-30); the room's esp sprites use 11-14
    // (load_effect_sprites). UVs in the esp data are coordinates within each
    // sheet.
    for (int i = 0; i < 8; i++) {
        if (DAT_00ac9cd0[i] != 0) {
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
        DAT_00ac9cd0[idx] = (int)pRdt->unknown_03 + *(int*)(spriteImBase - idx * 4) - 3;
    } while (i < 8);

    // 0x0047ba5c: Set up effect sprite texture positions
    setup_effect_sprite_textures(8);

    // 0x0047ba64: Load room effect sprite TIM files
    load_effect_sprites();
}

// ============================================================================
// reverse_anim_frame_data (0x0048bea0) - Reverse animation frame data order
// Swaps animation entries to reverse the playback order.
// param_1: pointer to joint anim_field (offset 0x0C within JointStruct)
// Externally visible: FUN_0048c020 (SCD opcode 0x0F) also calls this.
// ============================================================================
void reverse_anim_frame_data(int param_1)
{
    AnimSlot* slot = *(AnimSlot**)(param_1 + 8);
    unsigned short count = slot->entryCount;
    int baseAddr = count * 0x1c + (int)slot->data2;

    short* pRot = (short*)(baseAddr - 0x14);
    int* pTiming = (int*)(baseAddr - 8);

    do {
        short tmpRot = pRot[0];
        pRot[0] = pRot[2];
        pRot[2] = tmpRot;

        int tmpTiming = pTiming[0];
        pTiming[0] = pTiming[1];
        pTiming[1] = tmpTiming;

        count = count - 1;
        pRot = (short*)((int)pRot - 0x1c);
        pTiming = (int*)((int)pTiming - 0x1c);
    } while (count != 0);
}

// ============================================================================
// SetupEntityJointAnimation (0x0048bef0) - Entity joint animation copy and setup
// Copies entity joint data to the load buffer, resolves animation pointers,
// and creates animation objects for each joint.
// ============================================================================
void SetupEntityJointAnimation(void)
{
    // 0x0048bef0: Save load data pointer to entity weapon joints ptr
    ENTITY->weaponJointsPtr = (unsigned int)g_loadDataDestPointer;
    int jointBase = (int)g_loadDataDestPointer;

    // 0x0048bf05: Advance load pointer past joint data
    unsigned char jointCount = ENTITY->jointCount;
    g_loadDataDestPointer = (char*)g_loadDataDestPointer + (unsigned int)jointCount * 0x7c;

    // 0x0048bf1e: Copy animation slot data
    JointStruct* joints = ENTITY->jointsStructs;
    int* animSlotSrc = (int*)joints->anim_slot_ptr;
    int animEnd = *animSlotSrc;
    memcpy(g_loadDataDestPointer, animSlotSrc, animEnd - (int)animSlotSrc);

    // 0x0048bf37: Copy joint structs
    memcpy((void*)jointBase, joints, (unsigned int)jointCount * 0x7c);

    // 0x0048bf4d: Set up new animation slot base
    DAT_00be0e00 = (int)g_loadDataDestPointer;
    *(int*)(jointBase + 0x14) = (int)g_loadDataDestPointer;
    g_loadDataDestPointer = (char*)g_loadDataDestPointer + (animEnd - (int)animSlotSrc & 0xFFFFFFFCU);

    // 0x0048bf6c: Save new and original anim slot pointers for delta fixup
    int newAnimSlotPtr = *(int*)(jointBase + 0x14);
    unsigned int origAnimSlotPtr = (unsigned int)joints->anim_slot_ptr;

    // 0x0048bf7e: Process each joint
    unsigned char j = 0;
    if (jointCount != 0) {
        unsigned char nextJ;
        do {
            int animFieldAddr = jointBase + 0x0c;
            nextJ = j + 1;

            SetAnimSlot((AnimSlot*)DAT_00be0e00, animFieldAddr, j);

            // Point data_ptr to &scale_flag
            *(int*)(jointBase + 0x10) = jointBase + 0x20;

            // Fix up animation data pointer with relocation delta
            int* fixupPtr = (int*)(*(int*)(jointBase + 0x14) + 0x10);
            *fixupPtr = *fixupPtr + (newAnimSlotPtr - (int)origAnimSlotPtr);

            reverse_anim_frame_data(animFieldAddr);

            g_loadDataDestPointer = CreateAnimObject(animFieldAddr, (unsigned int*)g_loadDataDestPointer);

            jointBase = jointBase + 0x7c;
            j = nextJ;
        } while (nextJ < jointCount);
    }
}

// ============================================================================
// SetupTextureBankData (0x00473a30) - Process texture queue bank data
// Sets up texture bank pointers and copies initial texture state when the
// texture queue has entries. Called during room initialization.
// param_1: texture bank ID (short, typically _g_TextureBankID >> 8)
// ============================================================================
void SetupTextureBankData(short param_1)
{
    // 0x00473a30: Skip if no texture queue entries
    if (DAT_00ae9f04 == 0) return;

    // 0x00473a3e: Calculate bank count and pointers
    DAT_00ae9f06 = (DWORD)(param_1 - 10);
    DAT_00ae9f00 = (DWORD)g_loadDataDestPointer;
    DAT_00ae9efc = (DWORD)DAT_00ae9f06 * 0x200 + (DWORD)g_loadDataDestPointer;

    // 0x00473a6d: Advance load pointer
    g_loadDataDestPointer = (char*)g_loadDataDestPointer + (DWORD)DAT_00ae9f06 * 0x400;

    // 0x00473a7e: Process pending texture operations
    empty_0047b950(0);

    // 0x00473a86: Copy texture data to secondary buffer
    unsigned short idx = 0;
    if (DAT_00ae9f06 != 0) {
        do {
            unsigned int i = (unsigned int)idx;
            idx = idx + 1;
            *(DWORD*)(DAT_00ae9efc + i * 4) = *(DWORD*)(DAT_00ae9f00 + i * 4);
        } while ((unsigned int)idx < (DWORD)DAT_00ae9f06 * 0x80);
    }
}

// ============================================================================
// load_slides_images (0x00478110)
// Loads the projector slide TIM image and creates a texture page from it.
// Called during room_set for stage 4, room 4 (the lab projector room).
// ============================================================================
void load_slides_images(void)
{
    // 0x00478110: Load slide TIM file into display image buffer
    LoadFile(GAME_DATA_ROOT "data\\slide.tim", g_TimImageBuffer__bitmap, 0x20);
    // 0x00478124: Create texture page from loaded TIM data
    TexturePage_LoadImage(g_TimImageBuffer__bitmap, 9, 0xd);
}

// ============================================================================
// LZW Decompression (unpack_pakfile_ at 0x00425ab0)
// Helper functions and main decompression routine for PAK files.
// ============================================================================

// FUN_00425a70 - Reset LZW decompression dictionary
static void pak_decomp_reset(void)
{
    // 0x00425a70-0x00425a5e: set field +0 of every 12-byte record to -1. Note
    // this clears the record's UNUSED word, not the prefix — the decoder never
    // reads it, so the reset is effectively vestigial. Reproduced as-is.
    for (int i = 0; i < PAK_DICT_ENTRIES; i++) {
        g_pakDict[i].unused = -1;
    }
    g_pakDecompNextCode = 0x103;
    g_pakDecompCodeSize = 9;
    g_pakDecompMaxCode = 0x1ff;   // 0x00425a68: _DAT_00d2b0a0
}

// FUN_00425a00 - Read a code of 'codeSize' bits from the input bitstream
static unsigned int pak_decomp_read_code(void* src, unsigned int codeSize)
{
    unsigned int result = 0;
    unsigned int bit = 1 << (codeSize - 1);

    while (bit != 0) {
        // 0x00425a10: Refill bit buffer when empty
        if (g_pakDecompBitMask == 0x80) {
            g_pakDecompCurByte = ((unsigned char*)src)[g_pakDecompInputPos];
            g_pakDecompInputPos++;
        }
        // 0x00425a30: Test current bit
        if ((g_pakDecompCurByte & g_pakDecompBitMask) != 0) {
            result |= bit;
        }
        // 0x00425a48: Advance to next bit
        g_pakDecompBitMask >>= 1;
        bit >>= 1;
        if (g_pakDecompBitMask == 0) {
            g_pakDecompBitMask = 0x80;
        }
    }
    return result;
}

// FUN_00425bc0 - Decode a string from the LZW dictionary into g_pakStringBuf
// Returns the count of characters written (starting from param_1)
static int pak_decomp_decode_string(int startPos, unsigned int code)
{
    if (code > 0xFF) {
        // Multi-character: walk the prefix chain, emitting characters in reverse
        int pos = startPos;
        do {
            unsigned int idx = code;
            code = (unsigned int)g_pakDict[idx].prefix;
            g_pakStringBuf[pos] = g_pakDict[idx].ch;
            pos++;
        } while (code > 0xFF);
        g_pakStringBuf[pos] = (char)code;
        return pos + 1;
    }
    // Single character
    g_pakStringBuf[startPos] = (char)code;
    return startPos + 1;
}

// unpack_pakfile_ (0x00425ab0) - LZW decompression of a PAK file
// src: pointer to compressed PAK data
// dst: pointer to output buffer for decompressed data
// Returns: number of bytes written to dst
int unpack_pakfile_(void* src, void* dst)
{
    int outPos = 0;
    g_pakDecompInputPos = 0;
    g_pakDecompBitMask = 0x80;
    g_pakDecompCurByte = 0;

    do {
        // 0x00425abf: Reset dictionary
        pak_decomp_reset();

        // 0x00425ac4: Read first code
        unsigned int curCode = pak_decomp_read_code(src, g_pakDecompCodeSize);
        if (curCode == 0x100) {
            return outPos;
        }

        // 0x00425ade: Output first character
        ((unsigned char*)dst)[outPos] = (unsigned char)curCode;
        outPos++;
        unsigned int prevCode = curCode;

        // The original tracks the first character of the PREVIOUSLY decoded
        // string separately from the previous code (local_4 vs local_8). They
        // only coincide while codes are single characters, so they must not be
        // conflated — the KwKwK case below appends this character.
        unsigned int prevFirstChar = curCode;

        // 0x00425aee: Main decompression loop
        while (true) {
            curCode = pak_decomp_read_code(src, g_pakDecompCodeSize);

            // 0x100 = end of data
            if (curCode == 0x100) {
                return outPos;
            }
            // 0x102 = reset dictionary (restart outer loop)
            if (curCode == 0x102) {
                break;
            }
            // 0x101 = increase code size
            if (curCode == 0x101) {
                g_pakDecompCodeSize++;
                continue;
            }

            // 0x00425b20: KwKwK case — the code is not in the table yet, so
            // decode the PREVIOUS string and append its first character. That
            // trailing character goes in stringBuf[0], which the reversed output
            // loop below emits last.
            unsigned int lookupCode = curCode;
            bool special = (g_pakDecompNextCode <= curCode);
            if (special) {
                g_pakStringBuf[0] = (char)prevFirstChar;
                lookupCode = prevCode;
            }

            // 0x00425b3d: Decode string (reversed into g_pakStringBuf)
            int charCount = pak_decomp_decode_string(special ? 1 : 0, lookupCode);

            // 0x00425b50: The decoded string is reversed, so its first character
            // is the last one written. The original reads this uniformly, with no
            // special-case branch.
            char firstChar = g_pakStringBuf[charCount - 1];
            prevFirstChar = (unsigned int)firstChar;

            // 0x00425b64: Emit the string forwards by walking the buffer back
            // down to index 0 (which is the appended char in the KwKwK case).
            for (int i = charCount; i != 0; i--) {
                ((unsigned char*)dst)[outPos] = (unsigned char)g_pakStringBuf[i - 1];
                outPos++;
            }

            // 0x00425b90: Add the new dictionary entry. Its prefix is the code
            // from the PREVIOUS iteration, so prevCode must not be advanced
            // until after this write.
            unsigned int newIdx = g_pakDecompNextCode;
            g_pakDecompNextCode = newIdx + 1;
            g_pakDict[newIdx].prefix = (int)prevCode;
            g_pakDict[newIdx].ch = firstChar;

            // 0x00425ba9: Update state for next iteration
            prevCode = curCode;
        }
    } while (true);
}
