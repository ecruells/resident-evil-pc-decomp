#pragma once

// ============================================================================
// TexturePages.h - the emulated PS1 texture pages ("banks"), widened (port).
//
// The original addresses textures the way the PS1 GPU does: a TMD primitive's
// TSB word names one of 32 texture pages (bits 0-4: x 0-15, y 0-1 - the
// 1024x512 VRAM in 64x256 pages), and its CBA word names the CLUT by VRAM
// position (y in bits 6-15, x / 16 in bits 0-5). The PC build has no VRAM:
// each page is a PSXTexture slot (g_psxTextureArray), and a primitive finds its
// texture by matching keys - its page's VRAM origin against the TIM's image
// origin, its CBA against the TIM's CLUT origin. So nothing beyond the field
// widths limits how many pages and CLUT rows there can be.
//
// The page number is the one hard limit: 5 bits. This port keeps pages 0-31
// exactly as the original and stores pages 32+ with their high bits in TSB
// bits 9-11, which no PS1 TSB uses (0-4 page, 5-6 blend, 7-8 depth). Every
// reader of the page decodes through TexPageFromTsb, every writer encodes
// through TexPageTsbBits. Pages >= 16 sit at VRAM x (page - 16) * 64, y 256 -
// the original's formula for its second row of pages, simply carried on past
// x 1024; both the TIM's image origin (TmdProcessingCallback) and the
// primitive's page key (PSXObject_Store) use it, so they keep matching.
//
// The game's own allocation is left exactly as the original's: room models
// from page 6 and CLUT row 0x0A upward (room_set), then the room's masks,
// objects and items after them, all assuming that range. Only the zombie mod's
// models (survivor stand-ins, placed monsters, the director's body, a borrowed
// texture sheet) are put elsewhere, on a counter of their own: pages from
// TEX_BANK_ROOM_EXT_FIRST and CLUT rows from TEX_ROW_EXT_FIRST
// (ZombieMode.cpp, zm_ext_begin). Moving the shared counters instead shifted
// everything loaded after the models - the room masks lost their texture and
// characters were drawn over the furniture in front of them.
// ============================================================================

#define TEX_BANK_COUNT            128     // the original: 32
#define TEX_BANK_ROOM_LIMIT       0x15    // the original room range ends before the item page
#define TEX_BANK_ROOM_EXT_FIRST   0x20    // the port's pages for the mod's models
#define TEX_ROW_EXT_FIRST         0x40    // ...and their CLUT rows (y 0x220 up)

// The texture page a primitive's TSB (its high 16-bit half) names.
static inline unsigned int TexPageFromTsb(unsigned int tsb)
{
    return (tsb & 0x1F) | (((tsb >> 9) & 7) << 5);
}

// What adding `page` to a TSB whose page bits are zero must add.
static inline unsigned int TexPageTsbBits(unsigned int page)
{
    return (page & 0x1F) | ((page >> 5) << 9);
}

// The VRAM origin of a texture page: x in the low half, y in the high half
// (the TIM image block's x/y dword).
static inline unsigned int TexPageVramOrigin(unsigned int page)
{
    if (page < 0x10) return page << 6;
    return (0x100u << 16) | ((page - 0x10) << 6);
}
