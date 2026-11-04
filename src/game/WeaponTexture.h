// WeaponTexture.h - Port-only: a weapon model's own gun texture (no original address)
//
// A weapon model (Players/W??.EMW) has no texture of its own: its packets are
// bound to the player's texture (bank 0x16, CLUT page 7) and sample the left
// 128x256 page of whatever model the player wears. The packets cover the gun
// AND the hand holding it, and outfits repaint the hand. A gun whose art is
// only on one model's page (the DC's custom Beretta W0F/W1F, only on the DC's
// CHAR10/11; Jill's flamethrower W15, only on Chris's CHAR10) comes out wrong
// on every other model.
//
// The asset migrator can write `Players/W<nn>_<c>.TIM` (c = the character
// block, player id & 3) next to the weapon: 256x256 8 bpp, one CLUT row. The
// left half is the page the weapon was drawn for; the right half is a mask,
// non-zero on the GUN's texels (the ones no outfit of that character
// repaints). With it, the weapon's packets that lie wholly on gun texels read
// a second texture slot of bank 0x16, built from that page; the hand's packets
// keep reading the player's own texture. Without it - or when the file is in a
// different tree than the weapon model - nothing changes.
#pragma once

// Called from LoadEquippedWeaponAnimation (0x00462620) once the weapon's TMD
// has been bound to bank 0x16 / page 7 (`tmd` = the TMD, before the +0xC
// skip). `emwPath` is the asset-rooted path the model was loaded from, NULL
// for "no weapon model". Drops the previous weapon's gun texture either way.
void WeaponTexture_Apply(const char* emwPath, int charBlock, unsigned int* tmd);

// The player's model texture was re-uploaded (TmdProcessingCallback, bank
// 0x16): the gun slot was built at the old texture's size, so drop it.
void WeaponTexture_PlayerTextureReloaded(void);

// How many texture slots bank 0x16 holds: 1, or 2 with a gun texture. Every
// place that resets g_objectCountArray[22] uses this instead of 1.
int WeaponTexture_PlayerBankSlots(void);
