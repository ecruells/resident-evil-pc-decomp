// RoomBackgroundWebp.h - Room backgrounds stored as WebP (port-only).
//
// The game names a camera's background "stageS\rcSRRC.pak": an LZW stream
// holding a 320x240 16-bit TIM. A file "rcSRRC.webp" in the same place stands
// in for it - lossless or lossy, and at any resolution, which is how a
// high-resolution background set is installed. The asset migrator can write a
// whole tree's backgrounds this way (tools/asset_migrator, "Backgrounds as
// WebP").
//
// Which file wins is AssetPath's ResolveAssetAlternate: the mode overlay's
// .webp, the overlay's .pak, the base tree's .webp, the base tree's .pak.
//
// These are the three places load_room_bg / load_room_bg_image (Room.cpp) read
// a background; each returns 0 when there is no .webp for that camera, and the
// caller then takes the original .pak path untouched.
#pragma once

#include <stddef.h>

// Forget the files room_bg_webp_preload stored. Called when a room's
// backgrounds are loaded, before the first camera.
void room_bg_webp_reset(void);

// Cached mode: if `pakPath` has a .webp stand-in, read the file and keep it for
// `camera` (0-based) - the counterpart of copying the .pak into
// g_bgCacheBuffer. Returns 1 when stored.
int room_bg_webp_preload(const char* pakPath, int camera);

// Cached mode: decode and show what room_bg_webp_preload stored for `camera`,
// as display_image(slot, ...) would. Returns 1 when that camera has a stored
// file - even one that then fails to decode, because the cache holds no .pak
// to fall back to for it.
int room_bg_webp_show_cached(int camera, int slot);

// Direct mode: resolve, read, decode and show `pakPath`'s .webp stand-in in one
// step. Returns 1 when shown.
int room_bg_webp_show_file(const char* pakPath, int slot);

// HD replacements by file name (system/HdTextures.h): a background or mask
// page can be replaced by "HD/<TREE>/<path>.webp" - HD/USA/Stage1/RC1060.webp,
// HD/USA/Objspr/OSP01060.webp - where TREE is the tree the file is read from.
// The room code notes each camera's file while it loads the room and names
// the upload with it (HdSourceScope) when the camera is shown.
// `pakPath` is the asset-rooted path the game built; the key is kept per
// camera. room_bg_webp_reset forgets the background keys, room_hd_reset_masks
// the mask keys.
void room_hd_note_bg(const char* pakPath, int camera);
void room_hd_note_mask(const char* pakPath, int camera);
void room_hd_reset_masks(void);
// The key noted for `camera`, or "" (never NULL).
const char* room_hd_bg_key(int camera);
const char* room_hd_mask_key(int camera);
// The key of a file loaded directly (the scrapped heliport's), into `out`.
const char* room_hd_key_for(const char* pakPath, char* out, size_t outSize);
// The size a room background is uploaded at, which is what its key names: 320
// x 240, or 316 x 236 on the bordered screen (MSF2_SCREEN_BORDER: the room 307
// cameras, whose .pak holds a 316-wide picture under a 320x240 header).
int room_hd_bg_width(void);
int room_hd_bg_height(void);

// A mask page stored inside the room file (Director's Cut arrange rooms,
// Saturn mode) has no file of its own; it is named after the room file and
// the camera: HD/DC/Stage8/ROOM8000_MASK3.webp. room_hd_note_room takes the
// .rdt path the room was just loaded from; room_hd_rdt_mask_key writes the
// key for `camera` into `out` ("" when there is none).
void room_hd_note_room(const char* rdtPath);
const char* room_hd_rdt_mask_key(int camera, char* out, size_t outSize);

// Saturn mode and DC arrange rooms only: the PC Objspr page whose top-left
// part the camera's mask (`tim`, the TIM inside the room file) is - the Saturn
// room file is the PS1 one, and its mask TIM is the used rows/columns of the
// PC page for the same room and camera (an arena's, its campaign room's; an
// arrange room that kept the base room's picture, the base room's). Checked
// against the page itself, colour for colour. Returns the page's key
// ("USA/objspr/osp01063") with its size in *pageW / *pageH, or NULL when the
// mask is not that page.
// Its HD picture stands in when the mode's tree has none of its own.
const char* room_hd_pc_mask_page(int camera, const void* tim, int* pageW, int* pageH);
// The image size of the TIM at `tim` in texels (0 x 0 when it is not one).
void room_hd_tim_size(const void* tim, int* width, int* height);

// The texture of a model kept inside the room file (a room object, an item
// lying in the room): the same TIM is copied into many rooms, so it is named
// by its contents, in one folder of the tree the room was read from -
// HD/USA/RoomModels/<crc>.webp (hdtex::RoomTimName); a TIM with several
// palette rows adds _P<row> (HdTextures.h). "" when `tim` is not a TIM.
const char* room_hd_model_key(const void* tim, char* out, size_t outSize);

// Tells the HD texture layer (system/HdTextures.h) which textures this room's
// cameras will upload - every camera's background and mask page - so their
// replacements are decoded in the background before the first cut needs them.
// Called once the room's backgrounds and masks are in memory. Does nothing
// when there is no HD folder.
void room_hd_prefetch(void);
