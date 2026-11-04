#pragma once
// Sega Saturn .EMD -> PC .EMD converter. A C++ port of tools/saturn_emd.py
// (`pc` command), whose output was play-tested in the PC port (the Tick and
// Zombie Wesker, 2026-09-28). Layout: docs/SATURN_VERSION_ANALYSIS.md section 5.
//
// A Saturn .EMD is a 16-byte big-endian index (tag 0x10 + section offsets
// S1/S2/S3), the complete PS1 .EMD, then three Saturn sections: LZ77 SGL mesh,
// LZ77 4 bpp per-polygon textures, raw 16-colour palettes. The output keeps the
// PS1 part's animation data and replaces its mesh and texture with the Saturn
// ones: 0x34000609 triangles and one 8 bpp TIM of 256 x (up to 256 * pages)
// lines. Each packet carries its 256-line page in the pad halfword after
// (u2, v2); the game reads it through PSXObject_SetTexHeight when the bound
// TIM is taller than one page.

#include <cstdint>
#include <string>
#include <vector>

namespace re1 {

struct SaturnEmdInfo {
    int parts = 0;
    int palettes = 0;
    int textures = 0;
    int pages = 0;
    int timHeight = 0;
};

struct SaturnEmdOptions {
    // A model the game hangs a weapon model on: the player (CHAR10..13, the
    // alternate outfits; Players/W??.EMW) and the NPCs (EM102x;
    // Players/WS2??.TMD, CharacterNpc.cpp). A weapon model has no texture of
    // its own - it samples the left 128x256 page of its holder's TIM - so the
    // PS1 section's copy of that page is kept as page 0.
    bool weaponPage = false;
    // Jill's flamethrower. Players/W15.EMW is Chris's model (its packets are
    // W05.EMW's), and the flamethrower's texels are in Chris's page only: on
    // Jill's they land on her forearm and hair, in the PC release too. With
    // these two set (and weaponPage), the texels `donorMask` flags (128 x 256,
    // row-major, non-zero = wanted) are taken from the left page of `donorEmd`
    // - another Saturn .EMD, Chris's - and written to the RIGHT half of page 0,
    // which a weapon holder's page leaves empty. The overlay then ships a
    // W15.EMW whose packets read that half (core/SaturnOverlay.cpp).
    const std::vector<uint8_t>* donorEmd = nullptr;
    const std::vector<uint8_t>* donorMask = nullptr;
    // The DC's custom Beretta (Players/W0F.EMW, W1F.EMW). With these two set
    // (and weaponPage), the left-page texels `leftMask` flags (128 x 256,
    // row-major, non-zero = wanted) take their colour from `leftColours` (same
    // layout, RGB555, 0 = hole) instead of the PS1 page: the DC's own player
    // page, which has the gun's art where the PS1 page has filler.
    const std::vector<uint8_t>* leftMask = nullptr;
    const std::vector<uint16_t>* leftColours = nullptr;
};

// True when `data` starts with the Saturn .EMD index tag (00 00 00 10).
bool isSaturnEmd(const std::vector<uint8_t>& data);

// Convert a Saturn .EMD into a PC-format .EMD.
bool convertSaturnEmd(const std::vector<uint8_t>& saturn,
                      std::vector<uint8_t>* pcEmd, SaturnEmdInfo* info,
                      std::string* error, const SaturnEmdOptions& opts = {});

}  // namespace re1
