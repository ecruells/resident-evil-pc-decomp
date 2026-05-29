// GameState.cpp - Game state task functions (logos, title, asset loading)
// All functions decompiled from Ghidra with original addresses
#include "../Globals.h"
#include "../marni/MarniSystem.h"
#include "../marni/MarniSound.h"
#include "../marni/PSXTexture.h"
#include "FileLoader.h"
#include <cstdio>

// Forward declarations for stub functions (defined in GameStubs.cpp)
extern void FUN_00470a30(void);
extern void Object_DeleteAll(int a);
extern void SetVideoResolution(int w, int h);
extern void setSomeColor(int r, int g, int b);

// Forward declarations
// ---------------------------------------------------------------------------
// load_all_items_images_in_buffer (0x004...)
// Loads item_all.pix into the items image buffer
// ---------------------------------------------------------------------------
static void load_all_items_images_in_buffer(void)
{
    load_file(".\\usa\\data\\item_all.pix", g_ITEMS_IMAGES_BUFFER, 0x20);
}

// ============================================================================
// load_global_assets (0x00429a40)
// Task function: loads all global textures/assets needed by the game.
// After loading, chains to logos_state.
// ============================================================================
void logos_state(void);

void load_global_assets(void)
{
    load_all_items_images_in_buffer();

    if (g_image_buffer == NULL) {
        OutputDebugStringA("[ASSET] FATAL: g_image_buffer is NULL in load_global_assets!\n");
        Task_exit();
        return;
    }

    load_file(".\\usa\\data\\fontus.tim", g_image_buffer, 0x20);
    g_TextureBankID = 30;
    ProcessTextureImage(g_image_buffer, 30, 0, 0);

    OutputDebugStringA("fontus.tim loaded\n");

    load_file(".\\usa\\data\\Font03t.tim", g_image_buffer, 0x20);
    g_TextureBankID = 1;
    ProcessTextureImage(g_image_buffer, 1, 0, 2);

    load_file(".\\usa\\data\\Optkey03.tim", g_image_buffer, 0x20);
    g_TextureBankID = 2;
    LoadTexturePage(g_image_buffer, 2, 0, 0xB, 0, 0, 0, 0);

    load_file(".\\usa\\data\\status.tim", g_image_buffer, 0x20);
    g_TextureBankID = 0x41C;
    LoadTexturePage(g_image_buffer, 0x1C, 4, 0, 0, 0, 1, 0);

    SetupTexturePageHandles(0, 1);

    load_file(".\\usa\\data\\statface.tim", g_image_buffer, 0x20);
    LoadTexturePage(g_image_buffer, g_TextureBankID & 0xFF, 0, 9, 0, 0, 0, 0);

    load_file(".\\usa\\data\\blue.tim", g_image_buffer, 0x20);
    LoadTexturePage(g_image_buffer, g_TextureBankID & 0xFF, 0, 10, 0, 0, 0, 0);

    load_file(".\\usa\\data\\staitem.tim", g_image_buffer, 0x20);
    LoadTexturePage(g_image_buffer, 0, 0, 0x1E, 0, 0, 0, 0);

    load_file(".\\usa\\data\\kage.tim", g_image_buffer, 0x20);
    LoadShadowMaskTexture(g_image_buffer, 0);

    int rectConfig[24] = {
        -400,     400,     -400,     400,
           0,       0,        0,       0,
         400,     400,     -400,    -400,
           0,    0x1A,        0,    0x1A,
           0,       0,     0x1D,    0x1D,
           0,       1,        0,       0,
    };
    CreateTexturedQuad(0, 0x2F, rectConfig);

    Sound_Dispatch(0);

    // Task_chain((void*)debug_state);
    Task_chain((void*)logos_state);
}

// ============================================================================
// logos_state (0x00442bb0)
// Logos/opening state: plays intro videos then chains to title_state.
// ============================================================================
void logos_state(void)
{
    g_playingGameFlag = 0;
    g_fmvPlayCount = 0;
    g_demoIdleTimer1 = 1;

    clear_textures();
    FUN_00470a30();
    Object_DeleteAll(1);

    SetVideoResolution(640, 480);
    g_demoIdleTimer1 = 0;
    SetVideoResolution(320, 240);

    setSomeColor(128, 128, 128);

    g_currentFMVID = 28;
    g_selectedPlayerID = 0;
    g_main_state_flags |= 0x40000;

    Task_sleep(3);

    // g_currentFMVID = 23;
    // g_selectedPlayerID = 0;
    // g_main_state_flags |= 0x40000;

    Task_sleep(1);

    Task_chain((void*)title_state);
}

// ============================================================================
// debug_state — SFX Player Debug Menu
// Tests the MarniSound system interactively: load sound banks from
// g_SoundBanksTable, browse SFX entries, play/stop/loop individual sounds.
//
// Two-column layout showing 16 SFX slots per bank (0-7 left, 8-15 right).
// Uses direct GetAsyncKeyState for PC keyboard input with edge detection.
//
// Controls:
//   LEFT/RIGHT    : Change sound bank (0-15)
//   UP/DOWN       : Select SFX entry
//   ENTER         : Load selected bank
//   SPACE         : Play selected SFX (respects loop toggle)
//   S             : Stop selected SFX
//   L             : Toggle loop mode on/off
//   W/X           : Volume up/down
//   ESC           : Exit to logos_state
// ============================================================================
void debug_state(void)
{
    // Original address: 0x00429a40 continuation (debug_state inserted at chain point)
    static const char* bankNames[] = {
        "Knife",     "Knife2",   "Gun",      "Shotgun",
        "Magnum",    "Magnum2",  "Flame",    "Grenade",
        "Acid",      "Fire",     "Rocket",   "Bio",
        "Evil",      "Select",   "Ending",   "Win95"
    };

    static int curBank = 0;
    static int curEntry = 0;
    static int loadedBank = -1;
    static int sfxVolume = -1;     // -1 = max, -9999 = min
    static int loopMode = 0;       // 0 = one-shot, 1 = looping
    static int prevKeys = 0;

    OutputDebugStringA("[DEBUG] SFX Player - entering debug_state\n");

    while (1) {
        // --- Draw background (title screen pattern: blend=100, flags=1 → depth=2100 behind text) ---
        g_window_rect.w = 320;
        g_window_rect.textureId = 0;
        g_window_rect.r = 0;
        g_window_rect.g = 0;
        g_window_rect.b = 0;
        g_window_rect.x = -g_ScreenOffsetX;
        g_window_rect.h = 240;
        g_window_rect.y = -g_ScreenOffsetY;
        draw_rect(&g_window_rect, 100, 1);

        int y = 2;

        // Title
        sprintf(PRINT_TEXT_BUFFER, "=== SFX PLAYER ===");
        PrintText8x14(2, y, 0x8F, 0); y += 16;

        // Sound system status
        int dsReady = (g_pDirectSound != NULL && *(int*)((BYTE*)g_pDirectSound + 0x10));
        sprintf(PRINT_TEXT_BUFFER, "DS: %-7s  Vol: %-5d  Loop: %s",
                dsReady ? "READY" : "OFF", sfxVolume, loopMode ? "ON" : "OFF");
        PrintText8x14(2, y, 0x7F, 0); y += 16;

        // Bank selection
        sprintf(PRINT_TEXT_BUFFER, "Bank [%d]: %s  %s",
                curBank, bankNames[curBank],
                (curBank == loadedBank) ? "(loaded)" : "");
        PrintText8x14(2, y, 0x8F, 0); y += 18;

        // Separator
        sprintf(PRINT_TEXT_BUFFER, "----------------------------------------");
        PrintText8x14(2, y, 0x5F, 0); y += 16;

        // SFX entries — two columns: 0-7 on left, 8-15 on right
        if (loadedBank >= 0) {
            const char** tbl = g_SoundBanksTable[loadedBank];
            int startY = y;
            for (int i = 0; i < 8; i++) {
                int col2Idx = i + 8;

                // Left column (entries 0-7)
                {
                    const char* fn = tbl[i];
                    unsigned char col = (i == curEntry) ? 0x8F : 0x7F;
                    if (fn) {
                        int hnd = g_SfxBanks[i * 2];
                        sprintf(PRINT_TEXT_BUFFER, " %c[%2d] %-12s h=%d",
                                (i == curEntry) ? '>' : ' ', i, fn, hnd);
                        PrintText8x14(2, startY, col, 0);
                    } else {
                        sprintf(PRINT_TEXT_BUFFER, "  [%2d] ---", i);
                        PrintText8x14(2, startY, 0x5F, 0);
                    }
                }

                // Right column (entries 8-15)
                {
                    const char* fn = tbl[col2Idx];
                    unsigned char col = (col2Idx == curEntry) ? 0x8F : 0x7F;
                    if (fn) {
                        int hnd = g_SfxBanks[col2Idx * 2];
                        sprintf(PRINT_TEXT_BUFFER, " %c[%2d] %-12s h=%d",
                                (col2Idx == curEntry) ? '>' : ' ', col2Idx, fn, hnd);
                        PrintText8x14(160, startY, col, 0);
                    } else {
                        sprintf(PRINT_TEXT_BUFFER, "  [%2d] ---", col2Idx);
                        PrintText8x14(160, startY, 0x5F, 0);
                    }
                }

                startY += 14;
            }
            y = startY + 4;

            // Played entry status (no async calls — ExecAsync creates scheduler loop)
            int hnd = g_SfxBanks[curEntry * 2];
            sprintf(PRINT_TEXT_BUFFER, "Entry %d: handle=%d  %s",
                    curEntry, hnd,
                    (hnd != 0) ? "READY" : "(not loaded)");
            PrintText8x14(2, y, 0x8F, 0);
            y += 16;
        }

        // Controls legend at bottom
        y = 215;
        sprintf(PRINT_TEXT_BUFFER, "L/R:Bank ENT:Load UP/DN:Sel SPC:Play S:Stop L:Loop W/X:Vol ESC:Exit");
        PrintText8x14(2, y, 0x5F, 0);

        // --- Input handling with edge detection ---
        int keys = 0;
        if (GetAsyncKeyState(VK_UP)     & 0x8000) keys |= 0x001;
        if (GetAsyncKeyState(VK_DOWN)   & 0x8000) keys |= 0x002;
        if (GetAsyncKeyState(VK_LEFT)   & 0x8000) keys |= 0x004;
        if (GetAsyncKeyState(VK_RIGHT)  & 0x8000) keys |= 0x008;
        if (GetAsyncKeyState(VK_RETURN) & 0x8000) keys |= 0x010;
        if (GetAsyncKeyState(VK_SPACE)  & 0x8000) keys |= 0x020;
        if (GetAsyncKeyState('S')       & 0x8000) keys |= 0x040;
        if (GetAsyncKeyState('W')       & 0x8000) keys |= 0x080;
        if (GetAsyncKeyState('X')       & 0x8000) keys |= 0x100;
        if (GetAsyncKeyState('L')       & 0x8000) keys |= 0x200;
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) keys |= 0x400;

        int newKeys = keys & ~prevKeys;
        prevKeys = keys;

        if (newKeys & 0x001) { if (curEntry > 0)  curEntry--; }
        if (newKeys & 0x002) { if (curEntry < 15) curEntry++; }
        if (newKeys & 0x004) { if (curBank > 0)   { curBank--; loadedBank = -1; curEntry = 0; } }
        if (newKeys & 0x008) { if (curBank < 15)  { curBank++; loadedBank = -1; curEntry = 0; } }

        if (newKeys & 0x010) {  // ENTER — load bank
            if (dsReady) {
                char dbg[128];
                sprintf(dbg, "[DEBUG] SFX Player: loading bank %d (%s)\n", curBank, bankNames[curBank]);
                OutputDebugStringA(dbg);
                LoadSoundBank(curBank, NULL);
                loadedBank = curBank;
                g_SfxVolume = sfxVolume;
                curEntry = 0;
            }
        }

        if (newKeys & 0x020) {  // SPACE — play SFX
            if (loadedBank >= 0) {
                int hnd = g_SfxBanks[curEntry * 2];
                if (hnd != 0) {
                    char dbg[128];
                    sprintf(dbg, "[DEBUG] SFX Player: play entry %d (hnd=%d, loop=%d)\n",
                            curEntry, hnd, loopMode);
                    OutputDebugStringA(dbg);
                    set_volume(hnd, sfxVolume);
                    playSnd(hnd, loopMode ? 1 : 0);
                }
            }
        }

        if (newKeys & 0x040) {  // S — stop SFX
            if (loadedBank >= 0) {
                int hnd = g_SfxBanks[curEntry * 2];
                if (hnd != 0) {
                    OutputDebugStringA("[DEBUG] SFX Player: stop\n");
                    setSndStop(hnd);
                }
            }
        }

        if (newKeys & 0x080) {  // W — volume up
            sfxVolume += 500;
            if (sfxVolume > -1) sfxVolume = -1;
            g_SfxVolume = sfxVolume;
            if (loadedBank >= 0) {
                int hnd = g_SfxBanks[curEntry * 2];
                if (hnd != 0) set_volume(hnd, sfxVolume);
            }
        }

        if (newKeys & 0x100) {  // X — volume down
            sfxVolume -= 500;
            if (sfxVolume < -9999) sfxVolume = -9999;
            g_SfxVolume = sfxVolume;
            if (loadedBank >= 0) {
                int hnd = g_SfxBanks[curEntry * 2];
                if (hnd != 0) set_volume(hnd, sfxVolume);
            }
        }

        if (newKeys & 0x200) { loopMode ^= 1; }   // L — toggle loop

        if (newKeys & 0x400) { break; }           // ESC — exit

        Task_sleep(1);
    }

    // Cleanup: destroy loaded bank before leaving
    if (loadedBank >= 0) {
        for (int i = 0; i < 32; i += 2) {
            if (g_SfxBanks[i] != 0) {
                destroySndBank(g_SfxBanks[i]);
                g_SfxBanks[i] = 0;
            }
            g_SfxBanks[i + 1] = 0;
        }
        OutputDebugStringA("[DEBUG] SFX Player: banks cleaned up, exiting\n");
    }

    Task_chain((void*)logos_state);
}

// ============================================================================
// characterSelectionScreen (0x00492340)
// ============================================================================
void characterSelectionScreen(void) {
    OutputDebugStringA("[CharSelection] Character selection screen\n");
}

// ============================================================================
// game_start (0x00480710)
// ============================================================================
void game_start(void) {
    OutputDebugStringA("[GAME] Game start\n");
}
