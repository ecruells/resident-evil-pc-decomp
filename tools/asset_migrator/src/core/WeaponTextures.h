// Standalone gun textures for the weapon models (the game's
// src/game/WeaponTexture.h reads them).
//
// A weapon model (Players/W??.EMW) has no texture: it samples the left
// 128x256 page of the model holding it. Its packets cover the gun and the hand,
// and outfits repaint the hand, so the gun only looks right on a model whose
// page has that gun's art. For each weapon of each player character this
// writes Players/W<nn>_<c>.TIM (c = the character block: 0 Chris, 1 Jill), a
// 256x256 8 bpp TIM with one CLUT row:
//   left half   the holder's left page, through CLUT row 0 (what the weapon
//               was drawn for)
//   right half  the gun mask: 1 on the footprint texels that the holder and
//               every outfit of that character in the tree agree on, 0 else
// The game then points the packets lying wholly on the mask at this texture
// and leaves the hand's packets on the outfit's own page.
//
// Jill's flamethrower (W15) is Chris's model (W05's packets) and only Chris's
// page has its art, so its texture is Chris's page with the whole footprint
// flagged. The DC's custom Beretta (W0F / W1F) only exists in the DC overlay,
// whose CHAR10/11 hold its art; running this on that tree writes its texture.
#pragma once

#include "core/Types.h"

#include <string>
#include <vector>

namespace re1 {

// Writes the textures for every weapon model `tree` (a USA/JPN tree or the DC
// overlay) carries whose holder model is in the same tree. Returns the number
// written; logs and skips what it cannot do.
int writeWeaponTextures(const std::string& tree, const Progress& progress);

// The gun mask and page for one weapon, as written into the .TIM: `pix` is
// 256x256 (left = page indices, right = mask), `clut` the 256 colours. False
// when the files are not the expected shape. For tools/tests and the HD pass.
bool buildWeaponTexture(const std::vector<uint8_t>& emw, const std::vector<uint8_t>& holderEmd,
                        const std::vector<const std::vector<uint8_t>*>& outfits,
                        bool wholeFootprint, std::vector<uint8_t>* pix,
                        std::vector<uint16_t>* clut);

}  // namespace re1
