#include "../../Globals.h"
#include "../../platform/platform.h"
#include "../../system/AssetPath.h"
#include "../../marni/MarniDX.h"
#include "ZombieMode.h"
#include "ZombieNet.h"
#include "ZombiePortraitImage.h"
#include "../../DebugPrint.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static bool s_portraitReady[6];

static bool portrait_read(const char* path, std::vector<unsigned char>& data)
{
    char resolved[1024], normalized[1024];
    path = ResolveAssetRoot(path, resolved, sizeof(resolved));
    path = plat_normalize_path(path, normalized, sizeof(normalized));
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size <= 0 || size > 4 * 1024 * 1024 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return false; }
    data.resize((unsigned int)size);
    bool ok = fread(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    return ok;
}

static bool portrait_barry(unsigned char* portrait)
{
    // Verified in the USA PC prologue: frame 2087 at 10 fps. The JPN/DC
    // prologues have different edits, so don't apply this crop to those.
    // Read the base USA movie directly, independent of the content overlay.
    if (GetAssetVersion() != 0) return false;
    const char* const files[] = { "movie/pu.mp4", "movie/pu.avi" };
    for (int i = 0; i < 2; i++) {
        char path[1024], normalized[1024];
        snprintf(path, sizeof(path), "%s%s", GetAssetRoot(), files[i]);
        const char* movie = plat_normalize_path(path, normalized, sizeof(normalized));
        unsigned char* rgba = NULL;
        int w = 0, h = 0;
        if (!plat_video_read_frame(movie, 208700, &rgba, &w, &h)) continue;
        // Crop measured in the original 320x240 frame; scale for converted files.
        bool ok = zm_portrait_crop(rgba, w, h, 94 * w / 320, 10 * h / 240,
                                   174 * w / 320, 174 * h / 240, portrait);
        free(rgba);
        if (ok) { dbg_printf("[portrait] Barry: USA prologue 208700ms\n"); return true; }
    }
    return false;
}

void zombie_mode_load_portraits(void)
{
    memset(s_portraitReady, 0, sizeof(s_portraitReady));
    if (!g_bPlayAsZombie || !Marni_DX()) return;
    const int slot = 24; // original statface.tim, display_texture slot 9 + 15
    if (!g_TexturePageSRV[slot]) return;
    std::vector<unsigned char> data;
    unsigned char atlas[64 * 96 * 4] = {};
    if (!portrait_read(GAME_DATA_ROOT "data/statface.tim", data) ||
        !zm_portrait_sheet(data.data(), (unsigned int)data.size(), atlas)) return;
    s_portraitReady[ZM_CHAR_CHRIS] = s_portraitReady[ZM_CHAR_JILL] = s_portraitReady[ZM_CHAR_REBECCA] = true;

    const char* const models[] = { "enemy/char12.emd", "enemy/em1027.emd", "enemy/em1028.emd" };
    const int characters[] = { ZM_CHAR_BARRY, ZM_CHAR_RICHARD, ZM_CHAR_ENRICO };
    for (int i = 0; i < 3; i++) {
        unsigned char portrait[30 * 30 * 4];
        bool ok = i == 0 && portrait_barry(portrait);
        if (!ok) {
            char path[1024];
            snprintf(path, sizeof(path), "%s%s", GAME_DATA_ROOT, models[i]);
            ok = portrait_read(path, data) && zm_portrait_model(data.data(), (unsigned int)data.size(), portrait);
            dbg_printf("[portrait] character %d model: %s\n", characters[i], ok ? "OK" : "unavailable");
        }
        if (!ok) continue;
        int x = (characters[i] & 1) * 32, y = (characters[i] >> 1) * 32;
        for (int row = 0; row < 30; row++)
            memcpy(atlas + ((y + row) * 64 + x) * 4, portrait + row * 30 * 4, 30 * 4);
        s_portraitReady[characters[i]] = true;
    }
    MarniHandle handle = Marni_DX()->CreateTexture(64, 96, 32, atlas, NULL, NULL);
    if (!handle) { memset(s_portraitReady, 0, sizeof(s_portraitReady)); return; }
    Marni_DX()->DestroyTexture(g_TexturePageSRV[slot]);
    g_TexturePageSRV[slot] = handle;
    // Keep the original logical 8-bit VRAM/CLUT metadata and sprite path.
    // The renderer uploads both original and generated sheets as RGBA anyway.
    g_TexturePageHeight[slot] = 96;
}

int zombie_mode_inventory_portrait(void)
{
    int original = g_playerEntity.id & 3;
    if (!g_bPlayAsZombie || zm_net_char(zm_net_self()) < 0) return original;
    int ch = zm_net_char(zm_net_self());
    if (ch >= 0 && ch < 6 && s_portraitReady[ch]) return ch;
    // Even if generation/upload failed, Jill and Rebecca's original art exists.
    if (ch == ZM_CHAR_JILL || ch == ZM_CHAR_REBECCA) return ch;
    return ZM_CHAR_CHRIS;
}
