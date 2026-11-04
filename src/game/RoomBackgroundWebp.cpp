// RoomBackgroundWebp.cpp - Room backgrounds stored as WebP (port-only, no
// original address). See RoomBackgroundWebp.h.
#include "../Globals.h"
#include "RoomBackgroundWebp.h"
#include "../platform/platform.h"
#include "../system/AssetPath.h"
#include "../system/WebpImage.h"
#include "../system/HdTextures.h"
#include "../system/HdTextureHash.h"   // hdtex::RoomTimName
#include "dc/ArrangeStages.h"   // room_file_stage(), room_effect_page_entry()
#include "saturn/SaturnBattle.h" // saturn_battle_asset_room() (Saturn mask pages)

extern bool effspr_page_path(unsigned char nameIdx, char* path, size_t pathSize); // EffectSprites.cpp
#include "../DebugPrint.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// The logical background: what a .pak holds and what every reader of the
// display image assumes.
#define ROOM_BG_WIDTH   320
#define ROOM_BG_HEIGHT  240

// One slot per camera digit load_room_bg can write into the path
// (g_hexCharTable, '0'..'F').
#define ROOM_BG_CAMERAS 16

// The .webp files of the room in memory, undecoded - the stand-in for the
// packed .pak bytes the original keeps in g_bgCacheBuffer and unpacks on every
// camera switch. Kept on the heap rather than in that buffer: LoadFile has no
// size limit, and a high-resolution set would run past the buffer's 786 KB.
static std::vector<unsigned char> s_cameraFile[ROOM_BG_CAMERAS];

static bool ReadStandIn(const char* pakPath, std::vector<unsigned char>* out)
{
    char resolved[300];
    if (!ResolveAssetAlternate(pakPath, ".webp", resolved, sizeof(resolved))) {
        return false;
    }
    char normalized[300];
    size_t size = 0;
    void* file = plat_file_read_all(
        plat_normalize_path(resolved, normalized, sizeof(normalized)), &size);
    if (file == NULL) return false;

    const unsigned char* bytes = (const unsigned char*)file;
    const bool ok = WebpIsWebp(bytes, size);
    if (ok) {
        out->assign(bytes, bytes + size);
    } else {
        dbg_printf("[bg] %s is not a WebP file - using the .pak\n", resolved);
    }
    free(file);
    return ok;
}

static int Show(const std::vector<unsigned char>& file, int slot)
{
    std::vector<unsigned char> rgba;
    int w = 0, h = 0;
    if (!WebpDecodeRGBA(file.data(), file.size(), &rgba, &w, &h)) {
        dbg_printf("[bg] background .webp could not be decoded - using the .pak\n");
        return 0;
    }

    // A background is the base layer and has nothing to show through to (see
    // display_image), so whatever alpha the file carries is dropped.
    for (size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 0xFF;

    // Leave the picture in g_TimImageBuffer as well, as the 320x240 16-bit
    // bitmap unpack_pakfile_ would have put there, point-sampled. The original
    // path refills that buffer on every background load and the port keeps it
    // that way, so nothing that looks at it afterwards sees the previous
    // room's picture. `(v * 31 + 127) / 255` is the exact inverse of
    // display_image's `v * 255 / 31`: a lossless .webp made from a .pak gives
    // back the .pak's own words.
    unsigned short* bitmap = (unsigned short*)g_TimImageBuffer__bitmap;
    for (int y = 0; y < ROOM_BG_HEIGHT; y++) {
        const unsigned char* row = &rgba[(size_t)(y * h / ROOM_BG_HEIGHT) * w * 4];
        for (int x = 0; x < ROOM_BG_WIDTH; x++) {
            const unsigned char* px = row + (size_t)(x * w / ROOM_BG_WIDTH) * 4;
            const unsigned short r = (unsigned short)((px[0] * 31 + 127) / 255);
            const unsigned short g = (unsigned short)((px[1] * 31 + 127) / 255);
            const unsigned short b = (unsigned short)((px[2] * 31 + 127) / 255);
            bitmap[y * ROOM_BG_WIDTH + x] = (unsigned short)((b << 10) | (g << 5) | r);
        }
    }

    if ((g_main_state_flags2 & MSF2_SCREEN_BORDER) != 0) {
        // The bordered screen reads the bitmap as 316x236, as the original
        // does; that path stays the original's, fed from the copy above.
        display_image(slot, g_TimImageBuffer__bitmap, 316, 236);
    } else {
        display_image_rgba(slot, rgba.data(), w, h, ROOM_BG_WIDTH, ROOM_BG_HEIGHT);
    }
    return 1;
}

// The HD key of each camera's background and mask file ("" = none noted).
#define ROOM_HD_KEY_LEN 128
static char s_bgKey[ROOM_BG_CAMERAS][ROOM_HD_KEY_LEN];
static char s_maskKey[ROOM_BG_CAMERAS][ROOM_HD_KEY_LEN];

void room_bg_webp_reset(void)
{
    for (int i = 0; i < ROOM_BG_CAMERAS; i++) {
        std::vector<unsigned char>().swap(s_cameraFile[i]);
        s_bgKey[i][0] = '\0';
    }
}

const char* room_hd_key_for(const char* pakPath, char* out, size_t outSize)
{
    // A background may stand as .webp (above); the tree is the one that holds
    // either file, as ResolveAssetAlternate decides.
    if (out == NULL || outSize == 0) return "";
    if (!HdTextures_Active() || AssetSourceKeyAlt(pakPath, ".webp", out, outSize) == NULL) {
        out[0] = '\0';
    }
    return out;
}

// The rooms whose backgrounds the Saturn re-drew - the asset migrator's
// kRoomBgs (tools/asset_migrator/src/core/SaturnOverlay.cpp): the Laboratory
// Tyrant room (0-based stage 4, room 0x13), with the vats emptied and the
// second Tyrant's capsule added. The PC's HD art shows another room there, so
// neither its backgrounds nor its masks borrow it: with no HD picture of the
// Saturn's own, the room keeps the Saturn's art. (An arena that shows the
// room - Battle Game arena 15 - reads the same files.)
static bool saturn_room_redrawn(int stage0, int room)
{
    return stage0 == STAGE_LABORATORY && room == ROOM_MAIN_LAB;
}

// "<TREE>/Stage5/RC5130" -> whether TREE is a Saturn overlay and the file is
// a camera of a re-drawn room.
static bool saturn_redrawn_bg_key(const char* key)
{
    if (_strnicmp(key, "SATURN", 6) != 0) return false;
    const char* name = strrchr(key, '/');
    if (name == NULL || strlen(name) < 6 || _strnicmp(name + 1, "RC", 2) != 0) return false;
    const int stage1 = name[3] - '0';
    char hex[3] = {name[4], name[5], '\0'};
    char* end = NULL;
    const long room = strtol(hex, &end, 16);
    return stage1 >= 1 && stage1 <= 9 && end == hex + 2 && saturn_room_redrawn(stage1 - 1, (int)room);
}

// The DC's arrange rooms that show the base room's picture. The DC's own
// backgrounds there are the PS1's re-encode of the PC art (a little warmer,
// lower quality; mean difference 3.6-21 of 255 against the PC .pak), so the
// base room's HD picture stands in when the DC tree has none of its own.
// Checked in game by the user (2026-11-02), except 21A: 101 (its arrange
// version only adds the broken-window masks), 10D, 30F, 405, 504 and 604, and
// 71A's cameras are base 21A's (3.6-6.0). The rooms the DC re-drew (new
// cameras: 112, 11C, 203, ...) are not here, and stay the DC's.
//
// Rows are the BASE stage (0-based, 0-4) and room. A revisit room reads the
// same files as its 1F/2F room (601 and 604 read Stage8's RC801x / RC804x),
// so it folds back by 5, as the room files do.
struct DcBaseRoom { unsigned char stage; unsigned char room; };
static const DcBaseRoom kDcBaseRooms[] = {
    { 0, 0x01 },   // 101 / 601
    { 0, 0x04 },   // 604 (104 has no arrange version)
    { 0, 0x0D },   // 10D / 60D
    { 1, 0x1A },   // 71A (21A has no arrange version)
    { 2, 0x0F },   // 30F
    { 3, 0x05 },   // 405
    { 4, 0x04 },   // 504
};

static bool dc_base_room(int stage, int room)
{
    if (stage > 4) stage -= 5;
    for (size_t i = 0; i < sizeof(kDcBaseRooms) / sizeof(kDcBaseRooms[0]); i++) {
        if (kDcBaseRooms[i].stage == stage && kDcBaseRooms[i].room == room) return true;
    }
    return false;
}

// "DC/Stage8/RC8010" -> "USA/Stage1/RC1010" when it is a camera of one of the
// rooms above and the DC tree has no HD picture of its own for it.
static void dc_borrow_base_background(char* key)
{
    if (_strnicmp(key, "DC/Stage", 8) != 0) return;
    const char* name = strrchr(key, '/');
    if (name == NULL || strlen(name) != 7 || _strnicmp(name + 1, "RC", 2) != 0) return;
    char digits[4] = {name[3], '\0', '\0', '\0'};
    char* end = NULL;
    const long fileStage = strtol(digits, &end, 16);    // 8..E
    char hex[3] = {name[4], name[5], '\0'};
    char* end2 = NULL;
    const long room = strtol(hex, &end2, 16);
    if (end != digits + 1 || end2 != hex + 2 || fileStage < STAGE_ARRANGE_FIRST + 1 ||
        !dc_base_room((int)fileStage - STAGE_ARRANGE_FIRST - 1, (int)room)) {
        return;
    }
    char path[300];
    if (HdTextures_NamedFile(key, ".webp", path, sizeof(path))) return;   // the DC's own
    const int stage0 = ((int)fileStage - STAGE_ARRANGE_FIRST - 1) % 5;
    snprintf(key, ROOM_HD_KEY_LEN, "%s/Stage%d/RC%d%02X%c", GetAssetVersion() ? "JPN" : "USA",
             stage0 + 1, stage0 + 1, (unsigned)room, name[6]);
}

void room_hd_note_bg(const char* pakPath, int camera)
{
    if (camera < 0 || camera >= ROOM_BG_CAMERAS) return;
    room_hd_key_for(pakPath, s_bgKey[camera], ROOM_HD_KEY_LEN);
    if (s_bgKey[camera][0] != '\0' && saturn_redrawn_bg_key(s_bgKey[camera])) {
        HdTextures_SetBaseFallback(s_bgKey[camera], false);
    }
    if (s_bgKey[camera][0] != '\0' && g_bDcMode) dc_borrow_base_background(s_bgKey[camera]);
}

void room_hd_note_mask(const char* pakPath, int camera)
{
    if (camera < 0 || camera >= ROOM_BG_CAMERAS) return;
    room_hd_key_for(pakPath, s_maskKey[camera], ROOM_HD_KEY_LEN);
}

void room_hd_reset_masks(void)
{
    for (int i = 0; i < ROOM_BG_CAMERAS; i++) s_maskKey[i][0] = '\0';
}

static char s_roomKey[ROOM_HD_KEY_LEN];

// Saturn mode and DC arrange rooms: which PC Objspr page each camera's mask
// is, decided once per room (room_hd_pc_mask_page, below).
struct SaturnMaskPage {
    bool known;                  // decided for this room
    bool same;
    char key[ROOM_HD_KEY_LEN];
    int  pageW, pageH;
};
static SaturnMaskPage s_saturnMask[ROOM_BG_CAMERAS];

void room_hd_note_room(const char* rdtPath)
{
    for (int i = 0; i < ROOM_BG_CAMERAS; i++) s_saturnMask[i].known = false;
    if (!HdTextures_Active() || AssetSourceKey(rdtPath, s_roomKey, sizeof(s_roomKey)) == NULL) {
        s_roomKey[0] = '\0';
    }
}

const char* room_hd_rdt_mask_key(int camera, char* out, size_t outSize)
{
    if (out == NULL || outSize == 0) return "";
    out[0] = '\0';
    if (s_roomKey[0] != '\0' && camera >= 0 && camera < ROOM_BG_CAMERAS) {
        snprintf(out, outSize, "%s_MASK%X", s_roomKey, camera);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Saturn mode's masks borrow the PC pages' HD pictures.
//
// The Saturn reads a camera's mask from the room file (load_room_masks), so it
// is named after it (HD/SATURN/Stage1/ROOM1060_MASK5), and no pack has those.
// The Saturn room file is the PS1 one, and its mask TIM is the PC Objspr page
// for the same room and camera cut down to the rows and columns it uses: of
// the 1,274 cameras compared (tools: every Saturn ROOM*.RDT against the GOG
// Objspr paks, colours through each file's palette), every one is the PC
// page's top-left part within 2 steps a channel - the Battle Game arenas
// (Stage8) against their campaign room (saturn_battle_asset_room), the
// Laboratory Tyrant room included. The palettes are ordered differently, so
// the check is on colours, not indices. It is made here per camera, against
// the page itself, so a mask that differs (another disc) is left alone.
//
// The DC's arrange rooms read their masks from the room file too
// (HD/DC/StageE/ROOME1A0_MASK1), and the pack's bio1dc set only has the new
// camera angles' masks. An arrange room that keeps the base room's picture
// keeps its mask page as well (room 71A against base 21A's osp0126x), and
// takes that page's HD picture the same way; for the rooms in kDcBaseRooms
// the check is on the mask's shape only. Any other arrange room fails the
// colour check and keeps its own mask.
// ---------------------------------------------------------------------------
// (s_saturnMask, above room_hd_note_room, caches the decision per camera.)

// A 4/8/16 bpp TIM's image as 15-bit colours (palette row 0), into `out`.
static bool tim_colours(const unsigned char* tim, size_t cap, std::vector<unsigned short>* out,
                        int* w, int* h)
{
    if (cap < 20 || *(const unsigned int*)tim != 0x10) return false;
    const unsigned int flags = *(const unsigned int*)(tim + 4);
    const int bpp = (flags & 3) == 0 ? 4 : (flags & 3) == 1 ? 8 : (flags & 3) == 2 ? 16 : 0;
    if (bpp == 0) return false;
    size_t img = 8;
    const unsigned short* clut = NULL;
    if (flags & 8) {
        clut = (const unsigned short*)(tim + 20);
        img += *(const unsigned int*)(tim + 8);
    } else if (bpp != 16) {
        return false;
    }
    if (img + 12 > cap) return false;
    const int words = *(const unsigned short*)(tim + img + 8);
    *h = *(const unsigned short*)(tim + img + 10);
    *w = words * 16 / bpp;
    const unsigned char* pix = tim + img + 12;
    if (*w <= 0 || *h <= 0 || img + 12 + (size_t)words * 2 * *h > cap) return false;
    out->resize((size_t)*w * *h);
    for (int y = 0; y < *h; y++) {
        for (int x = 0; x < *w; x++) {
            unsigned short c;
            if (bpp == 16) {
                c = ((const unsigned short*)pix)[(size_t)y * *w + x];
            } else if (bpp == 8) {
                c = clut[pix[(size_t)y * *w + x]];
            } else {
                const unsigned char b = pix[(size_t)y * (*w / 2) + x / 2];
                c = clut[(x & 1) ? (b >> 4) : (b & 0xF)];
            }
            (*out)[(size_t)y * *w + x] = c;
        }
    }
    return true;
}

void room_hd_tim_size(const void* tim, int* width, int* height)
{
    *width = *height = 0;
    if (tim == NULL) return;
    const unsigned char* t = (const unsigned char*)tim;
    if (*(const unsigned int*)t != 0x10) return;
    const unsigned int flags = *(const unsigned int*)(t + 4);
    const int bpp = (flags & 3) == 0 ? 4 : (flags & 3) == 1 ? 8 : (flags & 3) == 2 ? 16 : 0;
    if (bpp == 0) return;
    const size_t img = 8 + ((flags & 8) ? *(const unsigned int*)(t + 8) : 0);
    *width = *(const unsigned short*)(t + img + 8) * 16 / bpp;
    *height = *(const unsigned short*)(t + img + 10);
}

static bool colours_close(unsigned short a, unsigned short b)
{
    if ((a == 0) != (b == 0)) return false;   // transparent in one only
    for (int sh = 0; sh <= 10; sh += 5) {
        const int d = (int)((a >> sh) & 31) - (int)((b >> sh) & 31);
        if (d > 2 || d < -2) return false;
    }
    return true;
}

const char* room_hd_pc_mask_page(int camera, const void* tim, int* pageW, int* pageH)
{
    if (!(g_bSaturnMode || (g_bDcMode && room_file_stage() >= STAGE_ARRANGE_FIRST)) ||
        !HdTextures_Active() || tim == NULL || camera < 0 || camera >= ROOM_BG_CAMERAS) {
        return NULL;
    }
    SaturnMaskPage& m = s_saturnMask[camera];
    if (!m.known) {
        m.known = true;
        m.same = false;
        // The PC page of this room and camera, built as load_room_masks builds
        // g_maskPathTemplate: the 0-based stage digit (a revisit stage folds
        // back by 5), the room in decimal, the camera. An arena is its
        // campaign room. A DC arrange room's g_stageId is the base stage.
        unsigned char stage = g_stageId, room = g_roomId;
        if (!saturn_battle_asset_room(&stage, &room) && stage > 4) stage -= 5;
        if (g_bSaturnMode && saturn_room_redrawn(stage, room)) {
            m.key[0] = '\0';   // the PC's mask belongs to the PC's picture
            return NULL;
        }
        char path[64];
        memcpy(path, g_maskPathTemplate, sizeof(g_maskPathTemplate));
        path[GAME_DATA_PATH_IDX(0x11)] = (char)('0' + stage % 10);
        path[GAME_DATA_PATH_IDX(0x12)] = (char)('0' + (room / 10) % 10);
        path[GAME_DATA_PATH_IDX(0x13)] = (char)('0' + room % 10);
        path[GAME_DATA_PATH_IDX(0x14)] = (char)('0' + camera);
        char resolved[300], normalized[300];
        void* file = NULL;
        size_t size = 0;
        if (room < 100 && AssetSourceKey(path, m.key, sizeof(m.key)) != NULL &&
            ResolveAssetRoot(path, resolved, sizeof(resolved)) != NULL) {
            file = plat_file_read_all(plat_normalize_path(resolved, normalized, sizeof(normalized)),
                                      &size);
        }
        if (file != NULL) {
            // A mask page unpacks to at most 66,080 bytes (room_hd_prefetch).
            static std::vector<unsigned char> page(sizeof(g_TimImageBuffer));
            unpack_pakfile_(file, page.data());
            free(file);
            std::vector<unsigned short> a, b;
            int aw = 0, ah = 0, bw = 0, bh = 0;
            if (tim_colours((const unsigned char*)tim, 0x100000u, &a, &aw, &ah) &&
                tim_colours(page.data(), page.size(), &b, &bw, &bh) && aw <= bw && ah <= bh) {
                // A DC arrange room that shows the base room's picture
                // (kDcBaseRooms) has its masks re-encoded too, so only their
                // SHAPE is compared: every one of those is the PC page's
                // transparent texels exactly, while room 101's broken-window
                // masks miss by ~20,000 (or have no PC page at all).
                const bool shapeOnly = g_bDcMode && dc_base_room(stage, room);
                size_t differ = 0;
                for (int y = 0; y < ah; y++)
                    for (int x = 0; x < aw; x++) {
                        const unsigned short ca = a[(size_t)y * aw + x], cb = b[(size_t)y * bw + x];
                        if (shapeOnly ? ((ca == 0) != (cb == 0)) : !colours_close(ca, cb)) differ++;
                    }
                // At most one texel in a thousand (none differed in any of them).
                m.same = differ * 1000 <= (size_t)aw * ah;
                m.pageW = bw;
                m.pageH = bh;
            }
        }
        if (!m.same) m.key[0] = '\0';
    }
    if (!m.same) return NULL;
    if (pageW != NULL) *pageW = m.pageW;
    if (pageH != NULL) *pageH = m.pageH;
    return m.key;
}

const char* room_hd_model_key(const void* tim, char* out, size_t outSize)
{
    if (out == NULL || outSize == 0) return "";
    out[0] = '\0';
    const char* slash = strchr(s_roomKey, '/');
    char name[9];
    // The read is bounded by the TIM's own header; the cap only stops a
    // corrupt one from walking far past the room buffer.
    if (slash == NULL || !hdtex::RoomTimName((const unsigned char*)tim, 0x100000u, name)) return out;
    snprintf(out, outSize, "%.*s/RoomModels/%s", (int)(slash - s_roomKey), s_roomKey, name);
    return out;
}

const char* room_hd_bg_key(int camera)
{
    return (camera >= 0 && camera < ROOM_BG_CAMERAS) ? s_bgKey[camera] : "";
}

int room_hd_bg_width(void)
{
    return (g_main_state_flags2 & MSF2_SCREEN_BORDER) != 0 ? 316 : ROOM_BG_WIDTH;
}

int room_hd_bg_height(void)
{
    return (g_main_state_flags2 & MSF2_SCREEN_BORDER) != 0 ? 236 : ROOM_BG_HEIGHT;
}

const char* room_hd_mask_key(int camera)
{
    return (camera >= 0 && camera < ROOM_BG_CAMERAS) ? s_maskKey[camera] : "";
}

int room_bg_webp_preload(const char* pakPath, int camera)
{
    if (camera < 0 || camera >= ROOM_BG_CAMERAS) return 0;
    return ReadStandIn(pakPath, &s_cameraFile[camera]) ? 1 : 0;
}

int room_bg_webp_show_cached(int camera, int slot)
{
    if (camera < 0 || camera >= ROOM_BG_CAMERAS || s_cameraFile[camera].empty()) {
        return 0;
    }
    // Handled either way. This camera has no .pak in g_bgCacheBuffer - its
    // offset there is the NEXT camera's - so a file that fails to decode leaves
    // the previous picture up rather than sending the caller to unpack another
    // camera's background.
    Show(s_cameraFile[camera], slot);
    return 1;
}

int room_bg_webp_show_file(const char* pakPath, int slot)
{
    std::vector<unsigned char> file;
    if (!ReadStandIn(pakPath, &file)) return 0;
    return Show(file, slot);
}

// ============================================================================
// room_hd_prefetch (port-only, no original address)
//
// A camera cut uploads that camera's background (load_room_bg_image) and its
// mask page (load_room_masks) from scratch, every time. With an HD pack each
// of those uploads is replaced by a 4x image that takes far longer to decode
// than the cut has, which showed as a hitch on every cut. The room's packed
// backgrounds and masks are all in memory once the room is loaded, so the
// textures the cuts WILL upload can be built here, ahead of time, and handed to
// HdTextures_Prefetch, which decodes their replacements on a worker thread.
//
// Only the names matter here, so each texture is built exactly as its upload
// path builds it - display_image_to_rgba for a background,
// EffectTextureSheet_BuildRGBA for a mask - and then thrown away.
// ============================================================================
// ============================================================================
// room_hd_prefetch_early (port-only, no original address)
//
// The room's init script (item_set, omodel_set, door_set, the effect sprite
// setup) uploads the textures of the models the room file carries and the
// room's Effspr pages one after the other, each a 4x picture decoded on the
// spot - a second or two of a stalled room load. Their names are known as soon
// as the room file is in memory, so they are handed to the decode workers
// first (HdTextures_PrefetchNamed) and decode in parallel while the load goes
// on. Called right before the init script runs; the door pictures are queued
// by door_set itself (DoorSystem.cpp, Door_HdPrefetch).
// ============================================================================
void room_hd_prefetch_early(void)
{
    // The previous room's queue is of no use to anyone now.
    HdTextures_PrefetchCancel();
    if (!HdTextures_Active() || g_RdtPointer == NULL) return;

    // The models inside the room file: RoomModels/<crc>, a row each.
    const struct { const unsigned char* table; unsigned int count; } tables[2] = {
        { g_RdtPointer->object_models, g_RdtPointer->omodel_slot_count },
        { g_RdtPointer->item_models,   g_RdtPointer->item_count },
    };
    for (int t = 0; t < 2; t++) {
        if (tables[t].table == NULL) continue;
        for (unsigned int i = 0; i < tables[t].count; i++) {
            const unsigned char* tim = *(const unsigned char* const*)(tables[t].table + i * 8 + 4);
            if (tim == NULL) continue;
            char key[ROOM_HD_KEY_LEN + 32];
            if (room_hd_model_key(tim, key, sizeof(key))[0] == '\0') continue;
            const unsigned int rows = *(const unsigned short*)(tim + 18);
            if (rows <= 1) {
                HdTextures_PrefetchNamed(key);
                continue;
            }
            for (unsigned int r = 0; r < rows && r < 8; r++) {
                char rowKey[ROOM_HD_KEY_LEN + 40];
                snprintf(rowKey, sizeof(rowKey), "%s_P%u", key, r);
                HdTextures_PrefetchNamed(rowKey);
            }
        }
    }

    // The room's Effspr pages (EffectSprites.cpp, load_effect_sprites).
    const unsigned char* pages = room_effect_page_entry();
    for (int i = 0; i < 4; i++) {
        char path[128], key[ROOM_HD_KEY_LEN];
        if (!effspr_page_path(pages[i], path, sizeof(path))) continue;
        HdTextures_PrefetchNamed(HdTextures_KeyForPath(path, key, sizeof(key)));
    }
}

void room_hd_prefetch(void)
{
    // room_hd_prefetch_early has cancelled the previous room's queue and
    // queued this room's own pictures; they stay queued.
    if (!HdTextures_Active() || g_RdtPointer == NULL) return;
    // The bordered screen and the uncached mode take other paths at the cut
    // (316x236 reads, direct loads); they keep decoding on demand.
    if (g_bgCacheMode == 0 || (g_main_state_flags2 & MSF2_SCREEN_BORDER) != 0) return;
    // The scrapped heliport loads its files directly on every cut.
    if (g_stageId == STAGE_COURTYARD && g_roomId == ROOM_SCRAPPED_HELIPORT) return;

    // unpack_pakfile_ output: a background TIM is 153,620 bytes and a mask page
    // at most 66,080, both inside what the original unpacks into
    // g_TimImageBuffer. A scratch buffer of that size, so the room load does
    // not disturb whatever that buffer holds.
    static std::vector<unsigned char> scratch(sizeof(g_TimImageBuffer));
    static std::vector<unsigned int> pixels(ROOM_BG_WIDTH * ROOM_BG_HEIGHT);

    RDT_Camera* cameras = (RDT_Camera*)((char*)g_RdtPointer + sizeof(RDT));
    int count = (int)(unsigned int)g_RdtPointer->cameras_count;
    if (count > ROOM_BG_CAMERAS) count = ROOM_BG_CAMERAS;

    for (int cam = 0; cam < count; cam++) {
        // --- background, as load_room_bg_image will show it ---
        // A replacement named after the file needs no picture built to find.
        if (HdTextures_PrefetchNamed(s_bgKey[cam])) {
            // queued (or already decoded)
        } else if (!s_cameraFile[cam].empty()) {
            std::vector<unsigned char> rgba;
            int w = 0, h = 0;
            // Only a native-size stand-in is looked up at the cut; a larger
            // one is the replacement already (display_image_rgba).
            if (WebpDecodeRGBA(s_cameraFile[cam].data(), s_cameraFile[cam].size(),
                               &rgba, &w, &h) &&
                w == ROOM_BG_WIDTH && h == ROOM_BG_HEIGHT) {
                for (size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 0xFF;
                HdTextures_Prefetch(w, h, rgba.data());
            }
        } else {
            const int offset = g_bgCameraOffsets[cam + 1];
            if (offset >= 0 && (size_t)offset < sizeof(g_bgCacheBuffer)) {
                unpack_pakfile_(&g_bgCacheBuffer[offset], scratch.data());
                display_image_to_rgba(scratch.data() + 20, ROOM_BG_WIDTH * ROOM_BG_HEIGHT,
                                      pixels.data());
                HdTextures_Prefetch(ROOM_BG_WIDTH, ROOM_BG_HEIGHT, pixels.data());
            }
        }

        // --- mask page, as load_room_masks will load it ---
        int* spriteGroup = (int*)cameras[cam].mask_pointer;
        if (spriteGroup == NULL || *spriteGroup == 0) continue;
        void* tim = NULL;
        if (room_file_stage() >= STAGE_ARRANGE_FIRST ||
            (g_bSaturnMode && cameras[cam].tim_mask_pointer != 0)) {
            tim = (void*)cameras[cam].tim_mask_pointer;   // embedded in the room file
            char key[ROOM_HD_KEY_LEN + 16];
            if (tim != NULL && HdTextures_PrefetchNamed(room_hd_rdt_mask_key(cam, key, sizeof(key))))
                continue;
            // Saturn / DC arrange: the PC page's picture stands in (load_room_masks).
            const char* pcPage = room_hd_pc_mask_page(cam, tim, NULL, NULL);
            if (pcPage != NULL && HdTextures_PrefetchNamed(pcPage)) continue;
        } else {
            // From Objspr: a replacement named after the file needs nothing built.
            if (HdTextures_PrefetchNamed(s_maskKey[cam])) continue;
            const int offset = g_bgMaskOffsets[cam];
            if (offset >= 0 && (size_t)offset < sizeof(g_bgMaskDataBuffer)) {
                unpack_pakfile_(&g_bgMaskDataBuffer[offset], scratch.data());
                tim = scratch.data();
            }
        }
        int w = 0, h = 0, bpp = 0;
        DWORD* mask = EffectTextureSheet_BuildRGBA(tim, &w, &h, &bpp);
        if (mask != NULL) {
            HdTextures_Prefetch(w, h, mask);
            delete[] mask;
        }
    }
}
