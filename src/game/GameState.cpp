// GameState.cpp - Game state task functions (logos, title, asset loading)
// All functions decompiled from Ghidra with original addresses
#include "../Globals.h"
#include "../DebugPrint.h"
#include "../marni/MarniSystem.h"
#include "../marni/MarniSound.h"
#include "../marni/PSXTexture.h"
#include "../marni/Marni3DObject.h"
#include "FileLoader.h"
#include <cstdio>
#include "../system/AssetPath.h"

extern void FUN_00470a30(void);
extern void Object_DeleteAll(int a);
extern void SetVideoResolution(int w, int h);
extern void setSomeColor(int r, int g, int b);
extern void empty_00412380(void);
extern void SetSpriteBufferFlag(void);
extern void setBackColor(unsigned short r, unsigned short g, unsigned short b);
extern void empty_40ae40(int);
extern void vram_clr(int x, int y, int w, int h);

// Forward declarations
// ---------------------------------------------------------------------------
// LoadAllItemsTexture (0x004...)
// Loads item_all.pix into the items image buffer
// ---------------------------------------------------------------------------
static void LoadAllItemsTexture(void)
{
    LoadFile(GAME_DATA_ROOT "data\\item_all.pix", g_ItemsImageBuffer, 0x20);
}

// ============================================================================
// load_global_assets (0x00429a40)
// Task function: loads all global textures/assets needed by the game.
// After loading, chains to logos_state.
// ============================================================================
void logos_state(void);
void texture_viewer_state(void);

void load_global_assets(void)
{
    LoadAllItemsTexture();

    // Load main Fonts textures
    LoadFile(GAME_DATA_ROOT "data\\fontus.tim", g_DataBuffer, 0x20);
    g_TextureBankID = 30;
    ProcessTextureImage(g_DataBuffer, 30, 0, 0);

    // Load numeric panel and puzzles font textures
    LoadFile(GAME_DATA_ROOT "data\\Font03t.tim", g_DataBuffer, 0x20);
    g_TextureBankID = 1;
    ProcessTextureImage(g_DataBuffer, 1, 0, 2);

    // Load Options menu textures (24bits)
    LoadFile(GAME_DATA_ROOT "data\\Optkey03.tim", g_DataBuffer, 0x20);
    g_TextureBankID = 2;
    LoadTexturePage(g_DataBuffer, 2, 0, 0xB, 0, 0, 0, 0);

    // Load Main menu textures (8bits)
    LoadFile(GAME_DATA_ROOT "data\\status.tim", g_DataBuffer, 0x20);
    g_TextureBankID = 0x41C;
    LoadTexturePage(g_DataBuffer, 0x1C, 4, 0, 0, 0, 0, 1);

    SetupTexturePageHandles(0, 1);

    // Loan Main menu characters faces texture (8bits)
    LoadFile(GAME_DATA_ROOT "data\\statface.tim", g_DataBuffer, 0x20);
    LoadTexturePage(g_DataBuffer, g_TextureBankID & 0xFF, 0, 9, 0, 0, 0, 0);

    // Load Inventory slot background texture (8bits)
    LoadFile(GAME_DATA_ROOT "data\\blue.tim", g_DataBuffer, 0x20);
    LoadTexturePage(g_DataBuffer, g_TextureBankID & 0xFF, 0, 10, 0, 0, 0, 0);

    // Load unused weapons texture (Uzi and machinegun) (8bits)
    LoadFile(GAME_DATA_ROOT "data\\staitem.tim", g_DataBuffer, 0x20);
    LoadTexturePage(g_DataBuffer, 0, 0, 0x1E, 0, 0, 0, 0);

    // Load character shadow texture (8bits)
    LoadFile(GAME_DATA_ROOT "data\\kage.tim", g_DataBuffer, 0x20);
    LoadShadowMaskTexture(g_DataBuffer, 0);

    int rectConfig[24] = {
        -400,     400,     -400,     400,
           0,       0,        0,       0,
         400,     400,     -400,    -400,
           0,    0x1A,        0,    0x1A,
           0,       0,     0x1D,    0x1D,
           0,       1,        0,       0,
    };
    CreateTexturedQuad(0, 0x2F, rectConfig);

    // Task_chain((void*)debug_state);
    // Task_chain((void*)input_test_state);
    // Task_chain((void*)texture_viewer_state);
    // Task_chain((void*)game_start);
    Task_chain((void*)logos_state);
}

// ============================================================================
// input_test_state — Input system verification screen
// Displays all input globals each frame to verify the pipeline:
//   InputUpdate → ReadPadBoth → PlayerPad_Update
//
// After PlayerPad_Update:
//   g_RawPadState       = raw newHeldRaw low 16 (NOT edge-detected)
//   g_PlayerPadHeld     = edge-detected full 32-bit (== g_PlayerPadPressed)
//   g_PlayerPadPressed  = edge-detected full 32-bit
//   g_button_pressed_id = full 32-bit copy of g_PlayerPadHeld (pre-overwrite)
//
// Controls:
//   F1 → proceed to logos_state
// ============================================================================
void input_test_state(void)
{
    OutputDebugStringA("[INPUT] input_test_state - entering\n");

    static int prevF1 = 0;

    while (1) {
        // Run the full input pipeline
        InputUpdate();
        PlayerPad_Update();

        // --- F1 detection (direct GetAsyncKeyState, not via PS1 pipeline) ---
        int f1Down = (GetAsyncKeyState(VK_F1) & 0x8000) ? 1 : 0;
        int f1Pressed = f1Down && !prevF1;
        prevF1 = f1Down;

        // --- Display ---
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
        int col1 = 2;
        int col2 = 170;

        sprintf(PRINT_TEXT_BUFFER, "=== INPUT TEST ===");
        PrintText8x14(col1, y, 0x8F, 0); y += 16;

        // Raw pipeline output (NOT edge-detected)
        sprintf(PRINT_TEXT_BUFFER, "PadBtnWord:   0x%04X", (WORD)g_PadBtnWord);
        PrintText8x14(col1, y, 0x7F, 0); y += 14;
        sprintf(PRINT_TEXT_BUFFER, "RawPadHeld:   0x%08X  (raw, for menu nav)", g_RawPadHeld);
        PrintText8x14(col1, y, 0x7F, 0); y += 14;
        sprintf(PRINT_TEXT_BUFFER, "RawPadState:  0x%04X  (newHeldRaw lo16)", g_RawPadState);
        PrintText8x14(col1, y, 0x7F, 0); y += 14;
        sprintf(PRINT_TEXT_BUFFER, "RawPadPressed:0x%04X  (edge-det via Prev)", g_padEdgeDetectedWord);
        PrintText8x14(col1, y, 0x7F, 0); y += 14;
        sprintf(PRINT_TEXT_BUFFER, "HeldPrev:     0x%04X  (prev RawPadState)", (WORD)g_PlayerPadHeldPrev);
        PrintText8x14(col1, y, 0x7F, 0); y += 14;

        y += 4;

        // Edge-detected full 32-bit state
        sprintf(PRINT_TEXT_BUFFER, "--- Edge-detected (32-bit) ---");
        PrintText8x14(col1, y, 0x8F, 0); y += 14;
        sprintf(PRINT_TEXT_BUFFER, "PadHeld:      0x%08X  (edge-det, gameplay)", g_PlayerPadHeld);
        PrintText8x14(col1, y, 0x7F, 0); y += 14;
        sprintf(PRINT_TEXT_BUFFER, "PadPressed:   0x%08X", g_PlayerPadPressed);
        PrintText8x14(col1, y, 0x7F, 0); y += 14;
        sprintf(PRINT_TEXT_BUFFER, "btn_pressed:  0x%08X  (copy of RawPadHeld)", g_button_pressed_id);
        PrintText8x14(col1, y, 0x7F, 0); y += 14;

        y += 4;

        // DPad states (remapped via padRemapTable)
        sprintf(PRINT_TEXT_BUFFER, "--- DPad (remapped) ---");
        PrintText8x14(col1, y, 0x8F, 0); y += 14;
        sprintf(PRINT_TEXT_BUFFER, "DpadHeld:     0x%04X", (WORD)g_PlayerDpadHeld);
        PrintText8x14(col1, y, 0x7F, 0); y += 14;
        sprintf(PRINT_TEXT_BUFFER, "DpadPressed:  0x%04X", g_PlayerDpadPressed);
        PrintText8x14(col1, y, 0x7F, 0); y += 14;
        sprintf(PRINT_TEXT_BUFFER, "DpadPrev:     0x%04X", g_PlayerDpadHeldPrev);
        PrintText8x14(col1, y, 0x7F, 0); y += 14;

        y += 4;

        // Button status (right column)
        y = 2;
        sprintf(PRINT_TEXT_BUFFER, "--- PS1 Buttons ---");
        PrintText8x14(col2, y, 0x8F, 0); y += 14;

        WORD held = (WORD)g_RawPadHeld;
        WORD pressed = (WORD)g_PlayerPadPressed;
        WORD dpad = (WORD)g_PlayerDpadHeld;

        #define BTN(name, mask, cy) \
            sprintf(PRINT_TEXT_BUFFER, "%-10s held=%c press=%c dpad=%c", name, \
                    (held & mask) ? 'X' : '-', \
                    (pressed & mask) ? 'X' : '-', \
                    (dpad & mask) ? 'X' : '-'); \
            PrintText8x14(col2, cy, (held & mask) ? 0x8F : 0x5F, 0)

        BTN("UP",       0x0010, y); y += 14;
        BTN("DOWN",     0x0040, y); y += 14;
        BTN("LEFT",     0x0080, y); y += 14;
        BTN("RIGHT",    0x0020, y); y += 14;
        BTN("TRIANGLE", 0x1000, y); y += 14;
        BTN("CIRCLE",   0x2000, y); y += 14;
        BTN("CROSS",    0x4000, y); y += 14;
        BTN("SQUARE",   0x8000, y); y += 14;
        BTN("L1",       0x0004, y); y += 14;
        BTN("R1",       0x0008, y); y += 14;
        BTN("L2",       0x0002, y); y += 14;
        BTN("R2",       0x0001, y); y += 14;
        BTN("START",    0x0800, y); y += 14;
        #undef BTN

        y = 220;
        sprintf(PRINT_TEXT_BUFFER, "F1=proceed  Arrows=Dpad  Enter=START+CROSS  Space=CROSS  Esc=SQUARE");
        PrintText8x14(2, y, 0x5F, 0);

        // Exit condition: F1 pressed → proceed to logos_state
        if (f1Pressed) break;

        Task_sleep(1);
    }

    OutputDebugStringA("[INPUT] input_test_state - exiting, chaining to logos_state\n");
    Task_chain((void*)logos_state);
}

// ============================================================================
// texture_viewer_state — Texture page verification screen
// Scans g_TexturePageSRV for loaded textures and displays them visually.
// Shows texture info and a preview of each loaded page.
//
// Controls:
//   LEFT/RIGHT      : Navigate textures
//   UP/DOWN         : Navigate textures
//   A+ARROWS        : Move texture preview offset
//   Z+LEFT/RIGHT    : Cycle CLUT palette (for multi-CLUT textures)
//   R               : Reset offset to 0
//   F1              : Proceed to logos_state
//   ESC             : Proceed to logos_state
// ============================================================================
void texture_viewer_state(void)
{
    OutputDebugStringA("[TEX] texture_viewer_state - entering\n");

    static int selectedPage = 0;
    static int prevKeys = 0;
    static int foundCount = 0;
    static int validPages[256];
    static float texOffsetX = 0.0f;
    static float texOffsetY = 0.0f;
    static int clutIndex[256];  // current CLUT palette index per slot

    // Scan for loaded texture pages once on entry
    foundCount = 0;
    for (int i = 0; i < 256; i++) {
        clutIndex[i] = 0;
        if (g_TexturePageSRV[i] != NULL) {
            validPages[foundCount] = i;
            foundCount++;
        }
    }

    char dbg[128];
    sprintf(dbg, "[TEX] texture_viewer_state: found %d loaded texture pages\n", foundCount);
    OutputDebugStringA(dbg);

    if (foundCount > 0) selectedPage = 0;
    else selectedPage = -1;

    while (1) {
        // --- Background (drawn via draw_rect which queues PendingSprites) ---
        g_window_rect.w = 320;
        g_window_rect.textureId = 0;
        g_window_rect.r = 0;
        g_window_rect.g = 0;
        g_window_rect.b = 0;
        g_window_rect.x = -g_ScreenOffsetX;
        g_window_rect.h = 240;
        g_window_rect.y = -g_ScreenOffsetY;
        draw_rect(&g_window_rect, 100, 1);

        // Title line
        sprintf(PRINT_TEXT_BUFFER, "TEXTURE VIEWER [%d]", foundCount);
        PrintText8x14(2, 2, 0x8F, 0);

        if (foundCount == 0) {
            sprintf(PRINT_TEXT_BUFFER, "No textures loaded!");
            PrintText8x14(2, 18, 0x4F, 0);
        } else {
            int pageIdx = validPages[selectedPage];
            int texW = g_TexturePageWidth[pageIdx];
            int texH = g_TexturePageHeight[pageIdx];
            int texBpp = g_TexturePageBpp[pageIdx];
            int numCLUTs = GetTextureNumCLUTs(pageIdx);

            // Info line: slot, index, dimensions, bpp, offset, CLUT
            if (numCLUTs > 1) {
                sprintf(PRINT_TEXT_BUFFER, "[%d/%d] %dx%d %dbpp clut=%d/%d off=(%d,%d)",
                        selectedPage + 1, foundCount, texW, texH, texBpp,
                        clutIndex[pageIdx] + 1, numCLUTs,
                        (int)texOffsetX, (int)texOffsetY);
            } else {
                sprintf(PRINT_TEXT_BUFFER, "[%d/%d] %dx%d %dbpp off=(%d,%d)",
                        selectedPage + 1, foundCount, texW, texH, texBpp,
                        (int)texOffsetX, (int)texOffsetY);
            }
            PrintText8x14(2, 18, 0x7F, 0);

            // Page list: show rows of page indices, 5 columns
            int listY = 34;
            int cols = 5;
            for (int i = 0; i < foundCount && i < 25; i++) {
                int col = i % cols;
                int row = i / cols;
                unsigned char color = (i == selectedPage) ? 0x8F : 0x5F;
                sprintf(PRINT_TEXT_BUFFER, "%d", validPages[i]);
                PrintText8x14((short)(2 + col * 60), (short)(listY + row * 14), color, 0);
            }
            int listRows = (foundCount + cols - 1) / cols;
            if (listRows > 3) listRows = 3;
            int previewY = listY + listRows * 14 + 6;

            // Texture preview: queue a PendingSprite with the texture's SRV
            // Coordinates must be in game-space (320x240)
            if (texW > 0 && texH > 0) {
                // Available preview area in game coords
                float availW = 310.0f;
                float availH = 236.0f - (float)previewY;

                // Show textures at native pixel size (1:1): never upscale
                // small textures, only downscale large ones to fit the area.
                float scX = availW / (float)texW;
                float scY = availH / (float)texH;
                float sc = (scX < scY) ? scX : scY;
                if (sc > 1.0f) sc = 1.0f;

                float drawW = (float)texW * sc;
                float drawH = (float)texH * sc;
                float drawX = 5.0f + (availW - drawW) * 0.5f + texOffsetX;
                float drawY = (float)previewY + texOffsetY;

                QueueTexturedSprite(drawX, drawY, drawW, drawH,
                                    g_TexturePageSRV[pageIdx], 200);
            }
        }

        // Controls line at bottom
        sprintf(PRINT_TEXT_BUFFER, "Arrows:Nav A+Arrows:Move Z+LR:CLUT R:Reset F1/ESC:Exit");
        PrintText8x14(2, 228, 0x5F, 0);

        // --- Input handling (direct GetAsyncKeyState with edge detection) ---
        int keys = 0;
        int aHeld = (GetAsyncKeyState('A') & 0x8000) ? 1 : 0;
        int zHeld = (GetAsyncKeyState('Z') & 0x8000) ? 1 : 0;
        if (GetAsyncKeyState(VK_LEFT)   & 0x8000) keys |= 0x001;
        if (GetAsyncKeyState(VK_RIGHT)  & 0x8000) keys |= 0x002;
        if (GetAsyncKeyState(VK_UP)     & 0x8000) keys |= 0x004;
        if (GetAsyncKeyState(VK_DOWN)   & 0x8000) keys |= 0x008;
        if (GetAsyncKeyState('R')       & 0x8000) keys |= 0x010;
        if (GetAsyncKeyState(VK_F1)     & 0x8000) keys |= 0x020;
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) keys |= 0x040;

        int newKeys = keys & ~prevKeys;
        prevKeys = keys;

        if (aHeld) {
            // A+Arrow: move texture offset (hold for continuous movement)
            float moveSpeed = 1.0f;
            if (keys & 0x001) texOffsetX -= moveSpeed;
            if (keys & 0x002) texOffsetX += moveSpeed;
            if (keys & 0x004) texOffsetY -= moveSpeed;
            if (keys & 0x008) texOffsetY += moveSpeed;
        } else if (zHeld && foundCount > 0) {
            // Z+Left/Right: cycle CLUT palette
            int pageIdx = validPages[selectedPage];
            int numCLUTs = GetTextureNumCLUTs(pageIdx);
            if (numCLUTs > 1 && (newKeys & (0x001 | 0x002))) {
                if (newKeys & 0x001) {
                    clutIndex[pageIdx]--;
                    if (clutIndex[pageIdx] < 0) clutIndex[pageIdx] = numCLUTs - 1;
                }
                if (newKeys & 0x002) {
                    clutIndex[pageIdx]++;
                    if (clutIndex[pageIdx] >= numCLUTs) clutIndex[pageIdx] = 0;
                }
                if (RebuildTextureSRV(pageIdx, clutIndex[pageIdx])) {
                    sprintf(dbg, "[TEX] Rebuilt SRV[%d] with CLUT %d/%d\n",
                            pageIdx, clutIndex[pageIdx], numCLUTs);
                    OutputDebugStringA(dbg);
                }
            }
        } else {
            // Arrow keys: navigate pages
            if (foundCount > 0) {
                if (newKeys & 0x001) { selectedPage--; if (selectedPage < 0) selectedPage = foundCount - 1; }
                if (newKeys & 0x002) { selectedPage++; if (selectedPage >= foundCount) selectedPage = 0; }
                if (newKeys & 0x004) { selectedPage--; if (selectedPage < 0) selectedPage = foundCount - 1; }
                if (newKeys & 0x008) { selectedPage++; if (selectedPage >= foundCount) selectedPage = 0; }
            }
        }

        // R: reset offset (edge-triggered)
        if (newKeys & 0x010) { texOffsetX = 0.0f; texOffsetY = 0.0f; }

        if (newKeys & 0x020) break;  // F1
        if (newKeys & 0x040) break;  // ESC

        Task_sleep(1);
    }

    OutputDebugStringA("[TEX] texture_viewer_state - exiting, chaining to logos_state\n");
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

    // if (g_bIsSoftwareRendering == FALSE) {
        // g_CurrentFMVID = 28;
        // g_FmvCharacterId = 0;
        // g_main_state_flags |= 0x40000;
    // } else {
    //     QueueVideoPlayback(29, 0);
    // }

    Task_sleep(3);

    // if (g_bIsSoftwareRendering == FALSE) {
        g_selectedFmvId = 23;
        // g_CurrentFMVID = 23;
        g_FmvCharacterId = 0;
        g_main_state_flags |= 0x40000;
    // } else {
    //     QueueVideoPlayback(29, 0);
    // }

    Task_sleep(1);

    // Task_chain((void*)game_start); // debug only, to go directly to game
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


// ---------------------------------------------------------------------------
// Flg_on (0x00473f60)
// Sets a bit flag in a flag array.
// baseAddr: base address of the flag array
// bitIndex: bit position to set
// ---------------------------------------------------------------------------
void Flg_on(int baseAddr, unsigned int bitIndex)
{
    unsigned int* flagWord = (unsigned int*)(((bitIndex & 0xffffffe7) >> 3) + baseAddr);
    *flagWord = *flagWord | (0x80000000U >> ((unsigned char)bitIndex & 0x1f));
}

// ---------------------------------------------------------------------------
// memclr (0x00475720)
// Zeros memory from start up to (but not including) end. Operates on DWORDs.
// ---------------------------------------------------------------------------
void memclr(void* start, void* end)
{
    unsigned int* p = (unsigned int*)start;
    unsigned int* e = (unsigned int*)end;
    while (p < e) {
        *p = 0;
        p++;
    }
}

// ---------------------------------------------------------------------------
// ResetGetAsyncKeyStateFlags (0x00497e60)
//
// Calls GetAsyncKeyState() for every possible virtual key (0-255) to reset
// the internal low-bit flag that indicates whether a key was pressed since
// the last query. This flushes stale/buffered keyboard input when
// transitioning between game states (menus, gameplay, cutscenes, pause,
// camera changes), preventing unintended actions from leftover key presses.
// This does NOT disable input or read gameplay input — it only resets the
// OS-level key press history so that only new key presses after this call
// will be detected.
// Called via ScheduleInputFlush to prevent FMV-skip button from immediately
// dismissing loading messages.
// ---------------------------------------------------------------------------
void ResetGetAsyncKeyStateFlags(void)
{
    BYTE vk;
    GetAsyncKeyState(0);
    for (vk = 1; vk != 0; vk++) {
        GetAsyncKeyState(vk);
    }
}

// ---------------------------------------------------------------------------
// ScheduleInputFlush (0x00497e80)
// Schedules an async call to ResetGetAsyncKeyStateFlags.
// ---------------------------------------------------------------------------
void ScheduleInputFlush(void)
{
    ExecAsync((void*)ResetGetAsyncKeyStateFlags);
}

// ---------------------------------------------------------------------------
// SetInitialItems (0x004513f0)
// Sets up the initial inventory based on selected character.
// ---------------------------------------------------------------------------
void SetInitialItems(void)
{
    unsigned char slot_index;
    unsigned char total_items_slots;
    unsigned char* item_slot;
    unsigned char item_qty;

    unsigned char initial_items[] = {
        // chris items
        ITEM_KNIFE,             0,
        ITEM_FIRST_AID_SPRAY,   1,
        ITEM_NONE,              0,
        ITEM_NONE,              0,
        // jill items
        ITEM_KNIFE,             0,
        ITEM_BERETTA,          15,
        ITEM_FIRST_AID_SPRAY,   1,
        ITEM_NONE,              0
    };

    // init room items flags (bit set = item not taken)
    {
        static const unsigned char roomItemsFlagsInit[32] = {
            0xff, 0xff, 0xff, 0xbf,
            0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xf7, 0xff,
            0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff
        };
        memcpy(g_roomItemsFlags, roomItemsFlagsInit, 32);
    }

    // Set display values
    DAT_00be982c = 7;
    DAT_00be982d = 0xf0;
    DAT_00be982e = 0xf0;

    if ((g_playerEntity.id & 3) == CHAR_CHRIS) {
        // Chris: 6 slots, Rebecca gets Baretta with 15 bullets
        item_slot = initial_items;           // Chris items at offset 0
        total_items_slots = 6;
        g_RebeccaItemSlots[0].Id = ITEM_BERETTA;
        g_RebeccaItemSlots[0].qty = 15;
    } else {
        // Jill: 8 slots
        item_slot = initial_items + 8;       // Jill items at offset 8
        total_items_slots = 8;
    }

    // Copy items into inventory slots
    item_qty = *item_slot;
    slot_index = 0;
    while (item_qty != 0) {
        g_ItemsSlots[slot_index].Id = *item_slot;
        item_qty = item_slot[1];
        g_ItemSlotIndices[slot_index] = slot_index;
        g_ItemsSlots[slot_index].qty = item_qty;
        item_qty = item_slot[2];
        item_slot = item_slot + 2;
        slot_index = slot_index + 1;
    }
    g_ItemSlotsBitmask = (1 << (slot_index & 0x1f)) - 1;
    g_TotalHeldItems = slot_index;

    // Clear remaining slots
    for (; slot_index < total_items_slots; slot_index++) {
        g_ItemsSlots[slot_index].Id = 0;
        g_ItemsSlots[slot_index].qty = 0;
    }
}

// ---------------------------------------------------------------------------
// CountHeldItems (0x00451600)
// Counts non-empty item slots in the current character's inventory.
// Max slots: 8 for Jill (characterId & 3 == 1), 6 for Chris/Rebecca.
// ---------------------------------------------------------------------------
void CountHeldItems(void) // 0x00451600
{
    g_TotalHeldItems = 0;
    unsigned char itemSlot = *(unsigned char*)g_ItemSlotsPointer;
    while (itemSlot != 0 &&
           g_TotalHeldItems < (unsigned char)((4 - ((g_playerEntity.id & 3) != 1)) * 2)) {
        g_TotalHeldItems = g_TotalHeldItems + 1;
        itemSlot = ((unsigned char*)g_ItemSlotsPointer)[(unsigned int)g_TotalHeldItems * 2];
    }
}

// ---------------------------------------------------------------------------
// LoadHeldItemsImages (0x00451640)
// Loads inventory item images into the image buffer for HUD display.
// Counts held items, sets up slot bitmask and indices, then loads each
// item's image sprite via LoadItemImage using the item image lookup table.
// After loading, composites all items into a single D3D11 SRV at the
// texture slot the renderer expects.
// ---------------------------------------------------------------------------
void LoadHeldItemsImages(void) // 0x00451640
{
    unsigned char totalItems;
    unsigned int index;
    void* savedSlotPointer;

    CountHeldItems();
    g_ItemSlotsBitmask = (1 << (g_TotalHeldItems & 0x1f)) - 1;
    totalItems = g_TotalHeldItems;
    savedSlotPointer = g_ItemSlotsPointer;

    while (totalItems != 0) {
        totalItems = totalItems - 1;
        index = (unsigned int)totalItems;
        g_ItemSlotsPointer = savedSlotPointer;
        g_ItemSlotIndices[index] = totalItems;
        unsigned char itemId = ((unsigned char*)savedSlotPointer)[index * 2];
        unsigned char imageType = g_ItemImageLookupTable[itemId * 4];
        LoadItemImage(imageType - 1, (int)index, (int)g_ItemsImageBuffer);
        savedSlotPointer = g_ItemSlotsPointer;
    }
    g_ItemSlotsPointer = savedSlotPointer;
}

// ---------------------------------------------------------------------------
// InitPlayerData (0x00481880)
// Initializes starting position, angle, and calls SetInitialItems.
// Starting position: (17000, 5000), angle: 3072 (about 270 degrees)
// ---------------------------------------------------------------------------
void InitPlayerData(void)
{
    SetInitialItems();
    g_playerEntity.position.x = 17000;
    g_main_state_flags2 = g_main_state_flags2 | 0x20000000;
    g_playerEntity.position.z = 5000;
    g_playerEntity.directionAngle = 3072;
}

// ---------------------------------------------------------------------------
// InitPlayerEntity (0x004950b0)
// Zeroes out all entity state fields: flags, animation, position, etc.
// Called at the start of SetupCharacterData.
// ---------------------------------------------------------------------------
void InitPlayerEntity(void)
{
    g_playerEntity.unk_bc = 0;
    g_playerEntity.attackAnim = 0;
    g_playerEntity.animation_frame_id = 0;
    g_playerEntity.unk_bf = 0;
    g_playerEntity.unk_c0 = 0;
    g_playerEntity.flags = 1;
    g_playerEntity.move_speed_current = 0;
    g_playerEntity.unk_03 = 1;
    g_playerEntity.speed.y = 0;
    g_playerEntity.animationId = 0;
    g_playerEntity.animFrameId = 0;
    g_playerEntity.action_behavior = 0;
    g_playerEntity.action_state = 0;
    g_playerEntity.speed.z = 0;
    g_playerEntity.unk_c1 = 0;
    g_playerEntity.speed.pad = 0;
    g_playerEntity.isBeingAttackedFlag = 0;
    g_playerEntity.position.pad = 0;
    g_playerEntity.unk_10 = 0;
    g_playerEntity.unk_11 = 99;
    g_playerEntity.unk_12 = 0xBE;
    g_playerEntity.unk_13 = 0;
    g_playerEntity.speed.x = 0;
    g_playerEntity.scaMatrixData.localMatrix.m[0][0] = 0x1000;
    g_playerEntity.scaMatrixData.localMatrix.m[0][1] = 0;
    g_playerEntity.scaMatrixData.localMatrix.m[0][2] = 0;
    g_playerEntity.scaMatrixData.localMatrix.m[1][0] = 0;
    g_playerEntity.scaMatrixData.localMatrix.m[1][1] = 0x1000;
    g_playerEntity.scaMatrixData.localMatrix.m[1][2] = 0;
    g_playerEntity.lookAtFlags = 0;   // disable head/aim tracking
    g_playerEntity.scaMatrixData.localMatrix.m[2][0] = 0;
    g_playerEntity.scaMatrixData.localMatrix.m[2][1] = 0;
    g_playerEntity.scaMatrixData.localMatrix.m[2][2] = 0x1000;
    g_playerEntity.unk_ca = 0;
}

// ===========================================================================
// InitializeGame (0x004807a0)
// Main game initialization. Loads bio_card.dat, sets up player entity, health,
// inventory, character data, room SFX, character SFX, and initializes the
// starting room.
// ===========================================================================
void InitializeGame(void)
{
    int has_alternate_outfit;

    g_AttractModeIdleTimer = 1;
    ScheduleInputFlush();
    vram_clr(0, 0, 320, 480);

    g_main_state_flags = (g_main_state_flags & 0x3fffffff) | 0x40000000;

    Task_sleep(1);
    g_bGameActive = 2;

    g_main_state_flags = g_main_state_flags & 0xd4e900f0;
    g_main_state_flags = g_main_state_flags | 0x4000000;

    empty_00412380();

    // memclr(&g_defaultItemSlot, g_BioCardData);

    g_loadDataDestPointer = g_DataBuffer;
    g_SpecialRoomLightDelta = 0;
    g_fading_counter = 0;

    Task_execute(1, (void*)display_game_loading_message);

    LoadFile(GAME_DATA_ROOT "data\\bio_card.dat", g_loadDataDestPointer, 32);

    if ((g_main_state_flags & 0x10000000) == 0) {
        g_gameSessionInitFlag = 0;
        Game_timer = 0;

        g_playerEntity.id = g_SelectedCharactedId;
        g_playerEntity.healthStatusFlags = 0x10;

        memcpy(&g_BioCardData[0], g_loadDataDestPointer, 1052);

        g_SpecialRoomLightState = (short)0xFFFF;
        g_CharacterModelId = g_playerEntity.id;

        if ((g_main_state_flags2 & 0x10000000) == 0) {
            InitPlayerData();
            /*
            * chris: 140hp
            * jill: 96hp
            */
            g_playerEntity.health = (short)((g_playerEntity.id & 1) * -44 + 140);
            g_PlayerHealthCopy = g_playerEntity.health;
        } else {
            LoadAttractModePlayerData();
        }
    } else {
        memcpy(&g_BioCardData[0], g_loadDataDestPointer, 0x200);
        empty_0047eb90((int)((~g_controllerConfig) >> 7));

        g_playerEntity.position.x = g_PlayerPosXCopy;
        g_playerEntity.position.z = g_PlayerPosZCopy;
        g_playerEntity.healthStatusFlags = g_PlayerHealthStatusCopy;
        g_playerEntity.directionAngle = g_PlayerDirAngleCopy;
        g_playerEntity.health = g_PlayerHealthCopy;
        g_playerEntity.id = g_SelectedCharactedId;
        g_CharacterModelId = g_SelectedCharactedId;

        /*  check alternative outfit flag */
        has_alternate_outfit = Flg_ck((int)g_PlayerFlags, 0x2a);
        if (has_alternate_outfit != 0) {
            g_CharacterModelId = g_CharacterModelId + 8;
        }

        if (g_SavesCounter == 0) {
            Game_timer = 0;
        }
        g_SavesCounter = g_SavesCounter + 1;
    }

    g_deadMoveValue = (DWORD)&g_identityMatrixData;
    g_RoomCameraDataCopy = (DWORD)&g_RoomCameraData;
    g_lightMatrixPtr = (DWORD)&g_lightMatrix;

    // 0x004809c5: g_ItemSlotsPointer = g_ItemsSlots
    g_ItemSlotsPointer = g_ItemsSlots;
    g_usedItemId = 0;
    DAT_00be9833 = 0;
    DAT_00be41e1 = 0;
    g_defaultItemSlot = 0;
    DAT_00be9614 = 0;

    LoadHeldItemsImages();

    // 0x00480a0d: DAT_00d91bc0 = &g_RoomItemEventTable. Redundant in practice —
    // room_set -> room_item_event_table_reset sets the same head pointer — but
    // present in the original.
    g_RoomItemEventHead = g_RoomItemEventTable;

    g_playerEntity.pSca_hit_data = (DWORD)g_entityDataBlock;

    g_playerEntity.maxHealth = (unsigned char)((g_playerEntity.id & 1) * -44 + 140);

    g_playerEntity.Sca_info = (unsigned int)g_scaDataTable;

    g_scaPoolPtr = (DWORD)g_entityDataBlock + 6;
    g_scaPoolBase = (DWORD)g_entityDataBlock + 6;

    Task_sleep(1);

    SetupCharacterData();

    g_loadDataDestPointer = g_shootDirEspBuffer;
    load_shoot_direction_data();

    g_SndFadeType = 0;
    g_loadDataDestPointer = g_DataBuffer;
    g_BGM_STATE = 0xFF;

    load_room_sfx(0);
    load_character_sfx(g_playerEntity.id & 1);

    LoadSoundBank(g_playerEntity.equippedWeaponId, g_DataBuffer);

    g_main_state_flags = g_main_state_flags & 0xfbffffff;

    init_room();

    g_AttractModeIdleTimer = 1;
    update_room_bgm();

    if ((g_playerEntity.id & 3) == CHAR_JILL) {
        has_alternate_outfit = Flg_ck((int)g_PlayerFlags, 0x7b);
        if (has_alternate_outfit == 0) {
            Flg_on((int)g_roomItemsFlags, 0x34);
            Flg_on((int)g_PlayerFlags3, 0x0b);
        }
    }

    g_AttractModeIdleTimer = 0;
    printf("end of game init\n");
}

// ============================================================================
// ending_state (0x00410820) — STUB
// Ending sequence: plays ending FMVs, credits, result screen.
// Sets up next-cycle save data and chains to title_state.
// ============================================================================
void ending_state(void)
{
    // 0x00410820
    OutputDebugStringA("[GAME] ending_state — stub\n");

    g_main_state_flags |= 0x80000;
    Task_sleep(1);

    sounds_reset();
    LoadSoundBank(0xe, g_DataBuffer);

    // Stub: brief delay then return to title
    for (int i = 0; i < 90; i++) {
        Task_sleep(1);
    }

    g_dwClearCount++;
    Task_chain((void*)title_state);
}

// ============================================================================
// game_start (0x00480710)
// Entry point for gameplay. Initializes game, runs the main game loop,
// then chains to the appropriate next state based on how the game ended.
//
// State transitions:
//   end_game_status == 1 → title_state (player died or quit)
//   end_game_status == 0 → ending_state (game completed)
//   otherwise            → logos_state  (fallback)
// ============================================================================
void game_start(void)
{
    int end_game_status;
    g_playingGameFlag = 1;

    g_message_flags = g_message_flags & 0xfdff;

    InitializeGame();

    // Task_chain((void*)texture_viewer_state);

    end_game_status = game_loop();

    g_main_state_flags = 0;

    if (end_game_status == 1) {
        g_main_state_flags2 = g_main_state_flags2 & 0x20080000;
        Task_chain((void*)title_state);
    }

    g_main_state_flags2 = g_main_state_flags2 & 0x20080000;

    if (end_game_status == 0) {
        g_gameTimerSnapshot = Game_timer;
        Task_chain((void*)ending_state);
    }

    Task_chain((void*)logos_state);
}

// ============================================================================
// Stub implementations for functions not yet decompiled
// ============================================================================

// 0x00443000 - LoadItemImage
// Wrapper around LoadImage for inventory item sprites.
// Loads a 20x30 16-bit item image from the buffer into a texture page slot.
void LoadItemImage(int item_id, int image_index, int img_buffer) // 0x00443000
{
    LoadImage(item_id * 1200 + img_buffer, 0, image_index + 1, 1, 108, (short)image_index << 5, 20, 30, 2);
}

// (0x00481060) - Load attract mode (demo) player save data
void LoadAttractModePlayerData(void) { }

// (0x0047eb90) - Restore game state from bio card on load
void empty_0047eb90(int param) { }


// (0x0045a6d0) - Play title screen selection SFX
void title_select_sfx(void) { }


// (0x00477d90) - Load RDT file for current room
// Loads the Room Definition Table for the current stage/room, resolves internal
// relative pointers to absolute addresses, and sets up SCD script pointers.
void LoadRoomRdt(void)
{
    static const char hexDigits[] = "0123456789abcdef";

    // 0x00477d97: Set g_RdtPointer to the current load buffer
    g_RdtPointer = (RDT*)g_loadDataDestPointer;

    // 0x00477d9c: ESI = start of camera data (past RDT header)
    unsigned char* cameras = (unsigned char*)(g_RdtPointer + 1);

    // 0x00477da4-0x00477e02: Build RDT file path
    // Format: ./usa/stageX/roomXYYZ.rdt where X=stage, YY=room, Z=flag
    sprintf(FILE_PATH, GAME_DATA_ROOT "stage%c\\room%c%c%c%c.rdt",
            hexDigits[g_stageId + 1],
            hexDigits[g_stageId + 1],
            hexDigits[g_roomId >> 4],
            hexDigits[g_roomId & 0xF],
            hexDigits[(g_main_state_flags & 0x800000) ? 1 : 0]);

    SetSpriteBufferFlag();

    LoadFile(FILE_PATH, g_RdtPointer, 1);

    // 0x00477e2a-0x00477e46: Resolve camera pointers
    // Each camera has 2 relative pointer fields (mask_pointer, tim_mask_pointer)
    // that need to be converted to absolute addresses.
    int cameraCount = g_RdtPointer->cameras_count;
    for (int i = 0; i < cameraCount; i++) {
        *(int*)(cameras) += (int)g_RdtPointer;
        *(int*)(cameras + 4) += (int)g_RdtPointer;
        cameras += 0x2C; // sizeof(RDT_Camera)
    }

    // 0x00477e48-0x00477e75: Resolve RDT pointer fields (offset 0x48 to 0x93)
    // These are relative offsets stored as ints, converted to absolute pointers.
    int* ptrField = (int*)((unsigned char*)g_RdtPointer + 0x48);
    int* ptrEnd = (int*)((unsigned char*)g_RdtPointer + 0x94);
    while (ptrField < ptrEnd) {
        *ptrField += (int)g_RdtPointer;
        ptrField++;
    }

    // 0x00477e7e-0x00477ec0: Resolve item model pointers
    // Iterates forward through entries, zeros table backward
    int* itemPtr = (int*)g_RdtPointer->items_models;
    int itemCount = g_RdtPointer->sound_banks_count;
    for (int i = itemCount; i > 0; i--) {
        // Zero out table entry (reverse order: table[count-1] down to table[0])
        ((int*)g_itemboxes_covers_table)[i - 1] = 0;
        if (itemPtr[0] != 0) itemPtr[0] += (int)g_RdtPointer;
        if (itemPtr[1] != 0) itemPtr[1] += (int)g_RdtPointer;
        itemPtr += 2;
    }

    // 0x00477ec9-0x00477f0b: Resolve obstacle model pointers
    // Same pattern: forward through entries, backward through table
    int* obstPtr = (int*)g_RdtPointer->obstacles_models;
    int obstCount = g_RdtPointer->unknown_03[0];
    for (int i = obstCount; i > 0; i--) {
        ((int*)g_desks_pointers_table)[i - 1] = 0;
        if (obstPtr[0] != 0) obstPtr[0] += (int)g_RdtPointer;
        if (obstPtr[1] != 0) obstPtr[1] += (int)g_RdtPointer;
        obstPtr += 2;
    }

    // 0x00477f12-0x00477f27: Set up SCD script pointers
    g_RoomInitScd = g_RdtPointer->initialization_scd;
    g_RoomScdOpcodes = g_RdtPointer->scd_opcodes;
    g_EvtScripts = g_RdtPointer->scd_opcodes2;

    // 0x00477f2d-0x00477f3f: Resolve EVT script relative offsets
    int* evtPtr = (int*)g_EvtScripts;
    while (*evtPtr != 0) {
        *evtPtr += (int)g_EvtScripts;
        evtPtr++;
    }

    // 0x00477f49-0x00477f64: Set back color from ambient light.
    //
    // ambient_light is a COLOR of three SHORTS (Ghidra: COLOR at RDT+6, size 6)
    // and setBackColor takes 12-bit PS1 channels, scaling by 255/4096. Casting to
    // unsigned char first was an 8x underexposure of the ambient term on every
    // room: the mansion hall's 1775 became 239, so the GTE background colour came
    // out 14 instead of 110 and every character model rendered near-black.
    setBackColor(
        (unsigned short)g_RdtPointer->ambient_light_r,
        (unsigned short)g_RdtPointer->ambient_light_g,
        (unsigned short)g_RdtPointer->ambient_light_b);

    // 0x00477f6e: empty_40ae40(0)
    empty_40ae40(0);
}

// (0x0040ada0) - Set background clear color
void setBackColor(unsigned short r, unsigned short g, unsigned short b) {
    // 0x0040ada0-0x0040ae36: Clamp PS1 12-bit color (0-4095) and convert to 8-bit (0-255)
    if (r > 0xFFF) r = 0x1000;
    g_red_color = (unsigned char)((r * 255) / 4096);

    if (g > 0xFFF) g = 0x1000;
    g_green_color = (unsigned char)((g * 255) / 4096);

    if (b > 0xFFF) b = 0x1000;
    g_blue_color = (unsigned char)((b * 255) / 4096);
}

// (0x0040ae40) - Empty function called by LoadRoomRdt
void empty_40ae40(int param) { }

// ============================================================================
// Model colour-tint helpers used by SCD opcode 0x34 variant 0.
//
// All three walk the SAME per-object array JointSetColorTint (0x00485ac0) walks:
// modelObj+0x20 is the CMarniDirect3DTMD, its object count is the dword at
// +0x4C0, and the objects start at +0x4D0 with a stride of 0x84. The loop bound
// is count * 2 (each object has a mirrored copy). Within an object,
// +0x5C/+0x60/+0x64 are the R/G/B tint multipliers as floats and
// +0x6C/+0x70/+0x74 are the second copy the renderer actually samples.
//
// Like the already-ported JointSetColorTint, only the `modelObj+0x10 == 0`
// branch is transcribed. The original's else-branch drives the complex-TMD
// staging buffer g_abComplexTmdObjectData (0x008ffd1c), which this port does not
// model at all - the same omission, and made for the same reason.
// ============================================================================

// 1/31, the fixed-point scale the two tint helpers share (float at 0x004af2e8).
static const float kTintScale = 0.032258064f;

// ============================================================================
// TmdObjectTintAdd (0x00485c60)
// Adds a signed RGB delta to every object of a model, rebased so the brighter of
// the R/G deltas becomes zero:
//
//   fr = r/31,  fg = g/31,  fmax = max(fr, fg)
//   dR = fr - fmax,  dG = fg - fmax,  dB = b/31 - fmax
//
// so the tint only ever darkens. Each component is accumulated onto the existing
// multiplier and clamped into [0, 1]; the second copy at +0x6C/+0x70/+0x74 is set
// to a constant 2.0f rather than mirrored (that is the original's behaviour, not
// a transcription slip). Green is then forced to 0 unconditionally, and blue is
// snapped to 0 below 0.36 (double at 0x004af2f0).
//
// On the FIRST object only, the resulting tint is packed back into modelObj+0x18
// as 0x00RRGGBB with each channel scaled by 255.0 (float at 0x004af2f8).
// ============================================================================
static void TmdObjectTintAdd(void* modelObj, int r, int g, int b)
{
    unsigned char* obj = (unsigned char*)modelObj;
    if (obj == NULL || *(int*)(obj + 0x10) != 0) {
        return;   // complex-TMD path, not modelled (see the note above)
    }

    unsigned char* tmd = *(unsigned char**)(obj + 0x20);
    if (tmd == NULL || (*(unsigned int*)(tmd + 0x4C0) & 0x7FFFFFFF) == 0) {
        return;
    }

    float fr   = (float)r * kTintScale;
    float fg   = (float)g * kTintScale;
    float fmax = (fr <= fg) ? fg : fr;
    float dR   = fr - fmax;
    float dG   = fg - fmax;
    float dB   = (float)b * kTintScale - fmax;

    unsigned char* rec = tmd + 0x4D0;
    unsigned int count = (unsigned int)(*(int*)(tmd + 0x4C0) * 2);

    for (unsigned int i = 0; i < count; i++) {
        *(unsigned int*)(rec + 0x80) |= 2;

        *(float*)(rec + 0x5C) += dR;
        *(unsigned int*)(rec + 0x6C) = 0x40000000u;   // 2.0f
        *(float*)(rec + 0x60) += dG;
        *(unsigned int*)(rec + 0x70) = 0x40000000u;   // 2.0f
        *(float*)(rec + 0x64) += dB;
        *(unsigned int*)(rec + 0x74) = 0x40000000u;   // 2.0f

        // The clamps are integer compares on the float bit patterns, exactly as
        // the original: signed `> 0x3F800000` catches anything above 1.0f, and
        // unsigned `> 0x80000000` catches any negative value except -0.0f.
        for (int off = 0x5C; off <= 0x64; off += 4) {
            if (*(int*)(rec + off) > 0x3F800000) {
                *(unsigned int*)(rec + off) = 0x3F800000u;   // 1.0f
            }
            if (*(unsigned int*)(rec + off) > 0x80000000u) {
                *(unsigned int*)(rec + off) = 0;
            }
        }

        *(unsigned int*)(rec + 0x60) = 0;                    // 0x00485db3
        if (*(float*)(rec + 0x64) < 0.36f) {
            *(unsigned int*)(rec + 0x64) = 0;
        }

        if (i == 0) {
            unsigned int pr = (unsigned int)(int)(*(float*)(rec + 0x5C) * 255.0f);
            unsigned int pg = (unsigned int)(int)(*(float*)(rec + 0x60) * 255.0f);
            unsigned int pb = (unsigned int)(int)(*(float*)(rec + 0x64) * 255.0f);
            *(unsigned int*)(obj + 0x18) =
                ((pr & 0xFF) << 16) | ((pg << 8) & 0xFF00) | (pb & 0xFF);
        }

        rec += 0x84;
    }
}

// ============================================================================
// TmdObjectTintSet (0x00485fa0)
// Sets (rather than accumulates) the RGB multipliers from a signed delta scaled
// by 5/31. The three values are rebased so that every positive component is
// subtracted from all three - repeated for R, then G, then B - which drives the
// brightest channel to exactly 0 and leaves the others negative. The stored
// multiplier is `1.0f + delta`, so the result only ever darkens.
//
// Unlike TmdObjectTintAdd this branch does NOT set the +0x80 dirty bit, and it
// writes the second copy (+0x6C/+0x70/+0x74) with the same value as the first.
// ============================================================================
static void TmdObjectTintSet(void* modelObj, int r, int g, int b)
{
    unsigned char* obj = (unsigned char*)modelObj;
    if (obj == NULL || *(int*)(obj + 0x10) != 0) {
        return;   // complex-TMD path, not modelled
    }

    unsigned char* tmd = *(unsigned char**)(obj + 0x20);
    if (tmd == NULL || (*(unsigned int*)(tmd + 0x4C0) & 0x7FFFFFFF) == 0) {
        return;
    }

    float fr = (float)(r * 5) * kTintScale;
    float fg = (float)(g * 5) * kTintScale;
    float fb = (float)(b * 5) * kTintScale;

    if (fr > 0.0f) { fg -= fr; fb -= fr; fr = 0.0f; }
    if (fg > 0.0f) { fr -= fg; fb -= fg; fg = 0.0f; }
    if (fb > 0.0f) { fr -= fb; fg -= fb; fb = 0.0f; }

    unsigned char* rec = tmd + 0x4D0;
    unsigned int count = (unsigned int)(*(int*)(tmd + 0x4C0) * 2);

    for (unsigned int i = 0; i < count; i++) {
        *(float*)(rec + 0x5C) = fr + 1.0f;
        *(float*)(rec + 0x6C) = fr + 1.0f;
        *(float*)(rec + 0x60) = fg + 1.0f;
        *(float*)(rec + 0x70) = fg + 1.0f;
        *(float*)(rec + 0x64) = fb + 1.0f;
        *(float*)(rec + 0x74) = fb + 1.0f;
        rec += 0x84;
    }
}

// ============================================================================
// TmdObjectSetLightScale (0x004870a0)
// Stores a single negated 1/32-scaled value at modelObj+0x14. The original is
// called with FOUR arguments but its body reads only two - the same dead-argument
// pattern as JointApplyColorTint / JointSetColorTint.
// ============================================================================
static void TmdObjectSetLightScale(void* modelObj, int value)
{
    if (modelObj != NULL) {
        *(float*)((unsigned char*)modelObj + 0x14) = (float)(-value) * 0.03125f;
    }
}

// ============================================================================
// scd_model_tint_apply (0x00473b10) - Accumulate a colour tint on a queue entry and
// apply it to the live model. SCD opcode 0x34 variant 0.
//
// Was an empty stub, so variant 0 of opcode 0x34 - the only variant that
// actually tints anything - did nothing at all. Variants 1 and 2
// (FUN_00473d10 / FUN_00473d60) only ever rewrote the queue entry.
//
// Finds the g_textureQueueData entry whose id byte matches p6, ADDS the three
// signed deltas onto bytes +3/+4/+5 (clamping each into [-31, +31]), stores the
// two 16-bit parameters at +6/+8 and arms the entry via +1. It then pushes the
// tint straight into the live model:
//
//   p6 bit 7 clear -> an ENEMY id. Scan up to 30 entries of g_EnemiesList for a
//     matching id and tint every joint. Ids 8, 0x0F and 0x12 use the absolute
//     TmdObjectTintSet with the CLAMPED queue bytes; every other id uses the
//     accumulating TmdObjectTintAdd with the RAW deltas.
//   p6 bit 7 set   -> an object index into g_itemboxes_covers_table. When all
//     three clamped bytes are equal the tint is a pure luminance change and goes
//     through TmdObjectSetLightScale; otherwise TmdObjectTintAdd.
//
// Two original quirks preserved deliberately:
//   - the enemy bound is `if (0x1d < g_enemy_count) count = 0x1e`, i.e. clamp to
//     30, not 32.
//   - the two object branches mask the index differently (0x7f for the
//     luminance path, 0x3f for the tint path). That asymmetry is the
//     original's; it is not a transcription slip.
// ============================================================================
void scd_model_tint_apply(unsigned char p1, unsigned short p2, unsigned short p3, unsigned char p4, unsigned char p5, char p6)
{
    unsigned char* e = g_textureQueueData;
    unsigned char slot = 0;
    while (*e != (unsigned char)p6) {
        e += 10;
        slot++;
        if (slot > 3) {
            return;
        }
    }

    e[3] = (unsigned char)(e[3] + (char)p1);
    e[4] = (unsigned char)(e[4] + (char)p2);
    e[5] = (unsigned char)(e[5] + (char)p3);
    *(unsigned short*)(e + 6) = p4;
    *(unsigned short*)(e + 8) = p5;

    for (int i = 3; i <= 5; i++) {
        if ((char)e[i] < -0x1F) e[i] = 0xE1;    // -31
        if ((char)e[i] >  0x1F) e[i] = 0x1F;    // +31
    }
    e[1] = 1;

    if (((unsigned char)p6 & 0x80) == 0) {
        unsigned char count = g_enemy_count;
        if (count > 0x1D) {
            count = 0x1E;
        }
        for (unsigned int n = 0; n < (unsigned int)count; n++) {
            Entity* enemy = &g_EnemiesList[n];
            if (enemy->id != (unsigned char)p6) {
                continue;
            }
            JointStruct* joints = enemy->jointsStructs;
            if (enemy->id == 8 || enemy->id == 0x0F || enemy->id == 0x12) {
                for (int j = 0; j < (int)(unsigned int)enemy->jointCount; j++) {
                    TmdObjectTintSet(joints[j].anim_object,
                                     (int)(char)e[3], (int)(char)e[4], (int)(char)e[5]);
                }
            } else {
                for (int j = 0; j < (int)(unsigned int)enemy->jointCount; j++) {
                    TmdObjectTintAdd(joints[j].anim_object, (int)p1, (int)p2, (int)p3);
                }
            }
        }
        return;
    }

    if (e[3] == e[4] && e[3] == e[5]) {
        int obj = (int)g_itemboxes_covers_table[(unsigned char)p6 & 0x7F];
        TmdObjectSetLightScale(*(void**)(obj + 0x18), (int)(char)e[3]);
    } else {
        int obj = (int)g_itemboxes_covers_table[(unsigned char)p6 & 0x3F];
        TmdObjectTintAdd(*(void**)(obj + 0x18), (int)p1, (int)p2, (int)p3);
    }
}

// ============================================================================
// FUN_00473d10 (0x00473d10) - Retarget a texture-queue entry with explicit bytes
// Same scan as FUN_00473d60 (match the id byte at +0 against p6, arm via +1), but
// stores p1/p2/p3 into bytes +3/+4/+5 instead of clearing them, and p4/p5 into the
// words at +6/+8. SCD opcode 0x34 variant 1.
// Every source is a byte in cmd_model_tint_set, so the low byte of the wider parameters is
// what the original actually stores - the declared widths differ from Ghidra's
// inferred ones but the stored values are identical.
// ============================================================================
void FUN_00473d10(unsigned char p1, unsigned short p2, unsigned short p3, unsigned char p4, unsigned char p5, char p6)
{
    unsigned char* e = g_textureQueueData;
    for (unsigned char i = 0; i < 4; i++) {
        if ((char)e[0] == p6) {
            e[3] = p1;
            e[4] = (unsigned char)p2;
            e[5] = (unsigned char)p3;
            *(unsigned short*)(e + 6) = p4;
            *(unsigned short*)(e + 8) = p5;
            e[1] = 1;
            return;
        }
        e += 10;
    }
}

// ============================================================================
// FUN_00473d60 (0x00473d60) - Retarget an existing texture-queue entry
// Scans the 4 entries of g_textureQueueData (10 bytes each, 0x00d22740) for one
// whose id byte matches p1; on a match, clears bytes +3..+5, stores the two
// 16-bit parameters at +6 and +8, and arms the entry by setting +1 to 1.
// No match = no-op. SCD opcode 0x34 variant 2.
// ============================================================================
void FUN_00473d60(char p1, unsigned char p2, unsigned char p3)
{
    unsigned char* e = g_textureQueueData;
    for (unsigned char i = 0; i < 4; i++) {
        if ((char)e[0] == p1) {
            e[3] = 0;
            e[4] = 0;
            e[5] = 0;
            *(unsigned short*)(e + 6) = p2;
            *(unsigned short*)(e + 8) = p3;
            e[1] = 1;
            return;
        }
        e += 10;
    }
}

// ============================================================================
// FUN_00473e40 (0x00473e40) - Force semi-transparency on a TMD's textured prims
// Resolves the TMD's animation pointers if needed, then walks each primitive
// group (7 dwords per group, count at +8; prim list pointer at group+0x10, prim
// count at group+0x14). Any primitive whose command dword has bit 0x04000000 set
// also gets 0x02000000 set. Primitive stride is ((cmd >> 8) & 0xFF) + 1 dwords.
// The original returns *param_1; every caller ignores it. SCD opcode 0x1F.
// ============================================================================
extern void ResolveAnimPointers(unsigned char* data);   // TmdAnimation.cpp

void FUN_00473e40(int param)
{
    unsigned int* p = (unsigned int*)param;
    if (p[1] == 0) {
        ResolveAnimPointers((unsigned char*)(p + 1));
    }
    unsigned int* group = p + 3;
    for (int groups = (int)p[2]; groups != 0; groups--) {
        unsigned int* prim = (unsigned int*)group[4];
        for (int prims = (int)group[5]; prims != 0; prims--) {
            unsigned int cmd = *prim;
            if ((cmd & 0x04000000) != 0) {
                *prim = cmd | 0x02000000;
            }
            prim += ((cmd >> 8) & 0xFF) + 1;
        }
        group += 7;
    }
}

// ============================================================================
// FUN_00473ea0 (0x00473ea0) - Bind a TMD to an object's animation slot
// Resolves the TMD's animation pointers if needed, links the object's anim slot
// to the TMD's slot table, stores the SCA matrix pointer at param2+4, zeroes
// param2+0, and allocates the animation object from the load arena.
// SCD opcodes 0x18 and 0x1F.
// ============================================================================
void FUN_00473ea0(int param1, void* param2, ScaMatrixData* param3)
{
    extern void SetAnimSlot(AnimSlot* slots, int slotPtr, int index);
    extern unsigned int* CreateAnimObject(int slotPtr, unsigned int* param2);

    if (*(int*)(param1 + 4) == 0) {
        ResolveAnimPointers((unsigned char*)(param1 + 4));
    }
    SetAnimSlot((AnimSlot*)(param1 + 0xc), (int)param2, 0);
    ((unsigned int*)param2)[1] = (unsigned int)param3;
    *((unsigned int*)param2) = 0;
    g_loadDataDestPointer = CreateAnimObject((int)param2, (unsigned int*)g_loadDataDestPointer);
}

// ============================================================================
// FUN_00473f10 (0x00473f10) - Clear a bit flag
// Exact counterpart of Flg_ck (0x00473f40): same byte-offset idiom
// ((bitIndex & 0xFFFFFFE7) >> 3, i.e. (bitIndex >> 5) * 4) and the same MSB-first
// bit order within the dword.
// ============================================================================
void FUN_00473f10(int* baseAddr, unsigned int bitIndex)
{
    unsigned int byteOffset = (bitIndex & 0xFFFFFFE7u) >> 3;
    unsigned int bitMask    = 0x80000000u >> (bitIndex & 0x1F);
    unsigned int* flagWord  = (unsigned int*)((unsigned char*)baseAddr + byteOffset);
    *flagWord &= ~bitMask;
}

// ============================================================================
// memset_ (0x0047cf60) - Zero N dwords
// The original's 2-arg helper (distinct from _memset): writes 0 over
// `dwordCount` consecutive dwords. Used by room_event_item_pickup to clear an
// effect slot (0x21 dwords = one 0x84-byte Effect).
// ============================================================================
void memset_(unsigned int* dst, int dwordCount)
{
    for (; dwordCount != 0; dwordCount--) {
        *dst = 0;
        dst++;
    }
}

// ============================================================================
// FUN_0047cf80 (0x0047cf80) - Free every effect slot matching selected criteria
// param1 is a criteria MASK; each set bit enables one comparison, and a slot is
// freed only when every enabled comparison matches (the original builds an
// accumulator and tests `accumulator == mask`, so bits above 3 make it unmatchable):
//   bit 0 -> effect->effectType     == (u8)param2   (+0x26)
//   bit 1 -> effect->depthGroup     == (u8)param3   (+0x27)
//   bit 2 -> effect->animHeader[2]  == (u8)param4   (+0x06, unnamed field)
//   bit 3 -> effect->spriteInfo     == (int)param5  (+0x64)
// Freeing = bump g_freeEffectSlots and zero updateId then animId (animId 0 marks
// the slot free), the same two bytes SCD opcode 0x48 clears.
//
// The original walks the 64 slots backwards (index 63 down to 0), pointing at
// effect+0x26 and stepping by -0x84; reproduced here as an index loop.
// Callers: SCD opcode 0x3E (mask 9 = type + spriteInfo) and 0x42 (mask 3 = type +
// depthGroup).
// ============================================================================
void FUN_0047cf80(int param1, unsigned int param2, unsigned int param3, unsigned int param4, MATRIX* param5)
{
    unsigned char mask = (unsigned char)param1;

    for (int i = 63; i >= 0; i--) {
        Effect* e = &g_effectPool[i];
        unsigned char matched = 0;

        if ((mask & 1) != 0 && e->effectType == (unsigned char)param2) {
            matched = 1;
        }
        if ((mask & 2) != 0 && e->depthGroup == (unsigned char)param3) {
            matched |= 2;
        }
        if ((mask & 4) != 0 && e->animHeader[2] == (unsigned char)param4) {
            matched |= 4;
        }
        if ((mask & 8) != 0 && e->spriteInfo == (int)param5) {
            matched |= 8;
        }

        if (matched == mask) {
            g_freeEffectSlots++;
            e->updateId = 0;
            e->animId   = 0;
        }
    }
}

// ============================================================================
// FUN_004804a0 (0x004804a0) - Start a volume ramp on one BGM channel
// param1 is unused by the original. param2 is the channel index and IS bounds
// checked (`param_2 < 3`, signed) before touching g_SndBank. SCD opcode 0x43.
// NOTE: the original divides by param4 with no zero check, so a script passing
// 0 there would fault. Reproduced faithfully.
// ============================================================================
void FUN_004804a0(short param1, unsigned int param2, short param3, unsigned int param4)
{
    (void)param1;
    if ((short)param2 < 3 && g_SndBank[param2].handle != 0) {
        g_SndRampBankIndex   = (short)param2;
        g_SndRampDirection   = (short)((int)param3 / (int)param4) * 0x4E;
        g_SndRampFramesLeft  = (int)param4 * 2;
    }
}

// ============================================================================
// FUN_004805d0 (0x004805d0) - snd_set_channel_pan_volume
// Applies a pan/volume pair to one BGM channel. param1 is unused by the original.
// param2 is the channel index (indexed as [EAX*0x8 + g_SndBank], i.e. a record
// index into SndBankSlot[3]). SCD opcode 0x2F.
// ============================================================================
void FUN_004805d0(short param1, unsigned int param2, unsigned int param3, unsigned int param4)
{
    (void)param1;   // pushed by callers, never read by the original
    int handle = g_SndBank[param2].handle;
    if (handle != 0) {
        set_volume(handle, CalcPanVolume((int)(short)param3, (int)(short)param4));
    }
}



// (0x00484c40)
static void FUN_00484c40(void)
{
    int tmdData = DAT_008f8c74;
    int bank = DAT_009104c0;
    int depth = DAT_008ffc34;

    BYTE* page = (BYTE*)g_renderStateTex;

    // 0x00484c6c: release the page's previous texture handles
    VideoDriver_ClearState348(page, g_pMarniDirect3D);

    // 0x00484c71: already set up — the flag at +0x348 is what
    // Direct3DTIM_Create also uses for the same purpose.
    if (*(DWORD*)(page + 0x348) == 1) return;

    // 0x00484c86: load the TIM image + CLUT into the page
    ((PSXTexture*)page)->Store((int*)tmdData, 1);

    // 0x00484c8d-0x00484cfb: per-material transparent-colour cleanup. Every
    // palette entry with 5551 bit 15 set has its index zeroed out of the pixel
    // data so those texels sample CLUT 0 (the transparent colour).
    int matCount = *(DWORD*)(page + 0x340);     // m_NumCLUTs
    for (int i = 0; i < matCount; i++) {
        BYTE* mat = page + i * 0x68;
        int* vtable = *(int**)mat;
        void* pixelData = NULL;
        DWORD clutPtr = 0;
        typedef int (*LockFn)(void* self, void** outData, DWORD* outClut);
        typedef int (*UnlockFn)(void* self);
        // vtable[4] = CMarniBits::Lock (0x00403450): outData = m_pPixelData
        // (+0x04), outClut = m_pPalette (+0x08, the CLUT heap copy).
        // The original ignores the result; the guard is port-only defence.
        if (((LockFn)vtable[4])(mat, &pixelData, &clutPtr) != 0) {
            BYTE* clut = (BYTE*)(ULONG_PTR)clutPtr;
            BYTE* px = (BYTE*)pixelData;
            int size = *(DWORD*)(mat + 0x2c) * *(DWORD*)(mat + 0x30);
            for (int clutIdx = 0; clutIdx < 0x100; clutIdx++) {
                if ((clut[clutIdx * 2 + 1] & 0x80) != 0) {   // 5551 bit 15
                    for (int p = 0; p < size; p++) {
                        if (px[p] == clutIdx) px[p] = 0;
                    }
                }
            }
        }
        ((UnlockFn)vtable[5])(mat);
    }

    // 0x00484cfd-0x00484d77: patch each material's CLUT descriptor (the same
    // +0x54/+0x58/+0x5C/+0x60 layout PSXObject_Store matches against) and
    // create the D3D texture handle for it.
    for (int i = 0; i < matCount; i++) {
        BYTE* mat = page + i * 0x68;
        *(DWORD*)(mat + 0x54) = 0;
        *(DWORD*)(mat + 0x58) = depth + 0x1e0;
        *(DWORD*)(mat + 0x5c) = (bank & 0xf) << 6;
        *(DWORD*)(mat + 0x60) = (bank & 0x10) << 4;
        void** d3dVtable = *(void***)g_pMarniDirect3D;
        typedef DWORD (*CreateTextureFn)(void*, BYTE*, int, int);
        CreateTextureFn createTex = (CreateTextureFn)d3dVtable[6];
        DWORD handle = createTex(g_pMarniDirect3D, mat, 0x21, 0);
        *(DWORD*)(page + 0x34C + i * 4) = handle;
    }

    *(DWORD*)(page + 0x348) = 1;
}

// (0x00484dc0) 
static void FUN_00484dc0(void)
{
    int* tmdHdr = (int*)DAT_00aae740;
    int bank = DAT_00aad6ec;

    CMarniDirect3DTMD* tmd = (CMarniDirect3DTMD*)g_renderStateTMD;

    // 0x00484dd9: clean the slot before re-storing
    tmd->CleanupObjects(g_pMarniDirect3D);

    // 0x00484dee: parse the TMD geometry (texRef 0x80 = the page's UV divisor)
    PSXObject_Store(tmd, tmdHdr, 0, bank, 0x80);

    // 0x00484e05: bind the render-state texture page
    tmd->Create(g_pMarniDirect3D, g_renderStateTex, (void*)1);

    // 0x00484e0c-0x00484e2f: mark every embedded object's transparency flag
    // (stride 0x108: each m_objectData entry and its copy, +0 and +0x84)
    int count = *(int*)((BYTE*)tmd + 0x4C0);    // m_objectCount
    for (int i = 0; i < count; i++) {
        *(DWORD*)((BYTE*)tmd + 0x550 + i * 0x108) |= 2;
        *(DWORD*)((BYTE*)tmd + 0x550 + i * 0x108 + 0x84) |= 2;
    }
}

// (0x00484d90) 
void FUN_00484d90(int param1, unsigned char param2, unsigned char param3)
{
    DAT_008f8c74 = param1;
    DAT_009104c0 = param2;
    DAT_008ffc34 = param3;
    ExecAsync((void*)FUN_00484c40);
}

// (0x00484e40) 
void FUN_00484e40(int param1, unsigned char param2, unsigned char param3)
{
    DAT_00aae740 = param1;
    DAT_00aad6ec = param2;
    DAT_00ac34f8 = param3;
    ExecAsync((void*)FUN_00484dc0);
}

// ============================================================================
// FUN_004870d0 (0x004870d0) - Set flag bit 1 on 32 consecutive 0x84-byte records
// param is a pointer whose +0x20 field holds the base of a record array; each
// record is 0x84 bytes and the flag dword sits at +0x4CC relative to that base.
// The original increments the offset BEFORE using it, so the first record touched
// is base+0x4CC+0x84 and the last is base+0x4CC+0x1080 (32 iterations).
// Called from SCD opcode 0x18 when the item type is 0x1E.
// Field names are left as raw offsets: the pointed-to type is not yet modeled.
// ============================================================================
void FUN_004870d0(int param)
{
    int base = *(int*)(param + 0x20);
    int offset = 0;
    do {
        offset += 0x84;
        unsigned int* flags = (unsigned int*)(base + 0x4CC + offset);
        *flags |= 2;
    } while (offset < 0x1080);
}

// ============================================================================
// FUN_0048a190 / JointApplyColorTint (0x0048a190)
// Marks a joint dirty (bit 0x80 of its first byte), publishes its vertex count
// doubled into g_playerDisplacement, and applies a colour tint to its model
// object. When g_main_state_flags bit 0 is set the same is repeated on the
// mirrored weapon-joint copy, reached by adding
// (ENTITY->weaponJointsPtr - ENTITY->jointsStructs) to the joint pointer.
//
// NOTE: JointSetColorTint (0x00485ac0) reads only TWO arguments - verified by
// disassembly: it takes arg2 from [ESP+8] at entry and arg1 from [ESP+0x20], and
// never references arg3/arg4. The original pushes four and cleans 0x10, so
// param3/param4 are DEAD. The colour actually applied is param2, so SCD opcode
// 0x4D tints with 0x30 (r=0x30,g=0,b=0), not with the 0x00606060 it also pushes.
// ============================================================================
void FUN_0048a190(void* param1, int param2, int param3, int param4)
{
    (void)param3;   // pushed by the original, never read by JointSetColorTint
    (void)param4;

    unsigned char* joint = (unsigned char*)param1;
    *joint |= 0x80;
    g_playerDisplacement = *(int*)(*(int*)(joint + 0x14) + 0x14) * 2;
    JointSetColorTint(*(int*)(joint + 0x18), (unsigned int)param2);

    if ((g_main_state_flags & 1) != 0) {
        joint += (*(int*)((unsigned char*)ENTITY + 0xac) -
                  *(int*)((unsigned char*)ENTITY + 0x98));
        g_tempVar = joint;
        *joint |= 0x80;
        g_playerDisplacement = *(int*)(*(int*)(joint + 0x14) + 0x14) * 2;
        JointSetColorTint(*(int*)(joint + 0x18), (unsigned int)param2);
    }
}

// ============================================================================
// FUN_0048bfe0 (0x0048bfe0) - Allocate two work buffers for the current entity
// Carves 0x7A00 and 0x1A00 bytes off the load arena and stores the two pointers
// at entity +0xB0 and +0xB4. Both offsets fall inside the unnamed padding of
// Entity/PlayerEntity (pad_b0 / pad_a4), so they are written by offset rather
// than invented field names. Called from SCD opcode 0x0F.
// ============================================================================
void FUN_0048bfe0(void)
{
    unsigned char* ent = (unsigned char*)ENTITY;
    *(void**)(ent + 0xB0) = g_loadDataDestPointer;
    g_loadDataDestPointer = (char*)g_loadDataDestPointer + 0x7A00;
    *(void**)(ent + 0xB4) = g_loadDataDestPointer;
    g_loadDataDestPointer = (char*)g_loadDataDestPointer + 0x1A00;
}

// ============================================================================
// FUN_0048c020 (0x0048c020) - Clone one joint's animation into the weapon-joint copy
// param is a joint index (SCD opcode 0x0F passes 0x0E). Copies the source joint's
// animation slot table into the first buffer allocated by FUN_0048bfe0
// (entity +0xB0), points the destination joint at that buffer and at the second
// buffer (+0xB4), relinks the slot, fixes up the relocated animation-data pointer
// by the buffer delta, reverses the frame order, and builds the anim object.
//
//   source joint      = ENTITY->jointsStructs   (+0x98) + index * 0x7C
//   destination joint = ENTITY->weaponJointsPtr (+0xAC) + index * 0x7C
//
// The copy length is *animSlot - animSlot: the slot table stores its own end
// pointer in the first dword.
// ============================================================================
void FUN_0048c020(int param)
{
    extern void SetAnimSlot(AnimSlot* slots, int slotPtr, int index);
    extern unsigned int* CreateAnimObject(int slotPtr, unsigned int* param2);
    extern void reverse_anim_frame_data(int animFieldAddr);

    unsigned char* ent = (unsigned char*)ENTITY;
    int dst = *(int*)(ent + 0xac) + (unsigned int)(unsigned char)param * 0x7c;
    int src = *(int*)(ent + 0x98) + (unsigned int)(unsigned char)param * 0x7c;

    int* animSlot = *(int**)(src + 0x14);
    void* buf0 = *(void**)(ent + 0xb0);
    void* buf1 = *(void**)(ent + 0xb4);
    memcpy(buf0, animSlot, (size_t)(*animSlot - (int)animSlot));

    int slotPtr = dst + 0xc;
    *(void**)(dst + 0x14) = buf0;
    *(void**)(dst + 0x18) = buf1;
    SetAnimSlot((AnimSlot*)buf0, slotPtr, 0);

    int* fixup = (int*)(*(int*)(dst + 0x14) + 0x10);
    *fixup += *(int*)(dst + 0x14) - *(int*)(src + 0x14);

    reverse_anim_frame_data(slotPtr);
    CreateAnimObject(slotPtr, (unsigned int*)buf1);
}

// ============================================================================
// FUN_0048f330 (0x0048f330) - restore_saved_enemy_state
// Scans the 16 slots of g_savedEnemyStates for an occupied entry matching the
// current room and the given enemy type. On a hit, copies the saved flags,
// position and angle into ENTITY, consumes the slot (valid = 0) and returns 1.
// Returns 0 when nothing matches.
//
// SCD opcode 0x1B (cmd_em_set) uses the return value to decide whether to skip
// its own spawn initialisation - a hit means "this enemy already has state, keep
// it where it was" rather than respawning at the script's coordinates.
//
// posY is applied only when the saved behaviorFlags have any of 0x70 set; the
// original tests ENTITY->behavior_flags, which it has just written from the slot.
// ============================================================================
int FUN_0048f330(unsigned char param)
{
    g_pSavedEnemyState = g_savedEnemyStates;
    int i = 0;
    while (g_pSavedEnemyState->valid == 0 ||
           g_pSavedEnemyState->roomId != g_roomId ||
           g_pSavedEnemyState->enemyType != param) {
        i++;
        g_pSavedEnemyState++;
        if (i > 0xF) {
            return 0;
        }
    }

    ENTITY->status_flags   = g_pSavedEnemyState->statusFlags;
    ENTITY->behavior_flags = g_pSavedEnemyState->behaviorFlags;
    ENTITY->scaMatrixData.localMatrix.t[0] = (int)g_pSavedEnemyState->posX;
    if ((ENTITY->behavior_flags & 0x70) != 0) {
        ENTITY->scaMatrixData.localMatrix.t[1] = (int)g_pSavedEnemyState->posY;
    }
    ENTITY->scaMatrixData.localMatrix.t[2] = (int)g_pSavedEnemyState->posZ;
    *(unsigned short*)&ENTITY->angle = g_pSavedEnemyState->angle;
    g_pSavedEnemyState->valid = 0;
    return 1;
}

// ============================================================================
// get_item_slot (0x004516a0)
// Linear search of the player's inventory for itemId. On a hit, points
// g_pCurrentItemSlot (0x00d226f0) at the matched 2-byte slot and returns its
// index; on a miss, points it at g_defaultItemSlot and returns -1.
// (The address previously commented here, 0x0047ee20, is inside LoadSoundBank.)
// ============================================================================
int get_item_slot(unsigned char itemId)
{
    unsigned char* slot = (unsigned char*)g_ItemSlotsPointer;
    if (g_TotalHeldItems != 0) {
        unsigned int i = 0;
        do {
            if (*slot == itemId) {
                g_pCurrentItemSlot = (unsigned char*)g_ItemSlotsPointer + i * 2;
                return (int)i;
            }
            i++;
            slot += 2;
        } while (i < (unsigned int)g_TotalHeldItems);
    }
    g_pCurrentItemSlot = &g_defaultItemSlot;
    return -1;
}

// ============================================================================
// BuildSndFadeTbl (0x0047ff90)
// For each of the 3 BGM channels, computes how many g_SndDistSteps-sized volume
// steps it takes to drive that channel from its current volume down to inaudible
// (-10000), and stores the count in g_SndFadeStepTbl (clamped at 0).
// Parameter 1 is the distance-step count (scaled by 0x4E), parameter 2 the fade
// type - the previous stub had these names inverted. SCD opcode 0x27 passes
// (op >> 8, 0x7F).
// NOTE: divides by g_SndDistSteps with no zero check, exactly as the original.
// ============================================================================
void BuildSndFadeTbl(char distSteps, int fadeType)
{
    DAT_00ac98f8   = 0;
    g_SndDistSteps = distSteps * 0x4E;
    g_SndFadeType  = (unsigned char)fadeType;

    for (int i = 0; i < 3; i++) {
        if (g_SndBank[i].handle == 0) {
            g_SndFadeStepTbl[i] = 0;
        } else {
            g_SndFadeStepTbl[i] = (-10000 - getSndVol(g_SndBank[i].handle)) / g_SndDistSteps;
        }
        if (g_SndFadeStepTbl[i] < 0) {
            g_SndFadeStepTbl[i] = 0;
        }
    }
}

// ============================================================================
// FUN_0040c560 (0x0040c560) - Store the low bit of the parameter into DAT_004d6444
// SCD opcode 0x4F writes this flag; opcode 0x50 (cmd_script_flag_test) returns it as its
// condition result, so a script can set a flag with 0x4F and branch on it later.
// ============================================================================
void FUN_0040c560(int param)
{
    DAT_004d6444 = (unsigned char)param & 1;
}

// Global stubs
unsigned long g_gameTimerSnapshot = 0;            // 0x00be9844
// ============================================================================
// g_ScdAnimRemap (0x004bec80)
// Animation remap table for SCD event state-1 opcode 0x89 (set animation frame).
// 16 (actionStateBase, animationId) pairs indexed by the entity's incoming
// animationId; the handler writes action_state = pair[0] + 1 and
// animationId = pair[1]. Only applied for entity ids < 0x20 and animationId
// <= 0x0F - the handler forces action_state = 3 above that, which is why the
// table is 32 bytes with pairs 10-15 left zero in the original.
//
//   anim: 0     1     2     3     4     5     6     7     8     9
//   pair: (0,0) (0,1) (0,2) (0,3) (0,4) (1,0) (1,1) (1,2) (1,3) (1,4)
// ============================================================================
extern const unsigned char g_ScdAnimRemap[32] = {
    0, 0,   0, 1,   0, 2,   0, 3,   0, 4,
    1, 0,   1, 1,   1, 2,   1, 3,   1, 4,
    0, 0,   0, 0,   0, 0,   0, 0,   0, 0,   0, 0,
};


// ============================================================================
// room_transition_load (0x004813c0) — the room/stage transition loader
//
// Previously stubbed in EngineStubs.cpp as "restore room state after menu close".
// That was wrong: it reads g_pendingDoorRecord seven times and is what actually
// carries the player through a door. door_try_enter stores the destination record
// and blacks the screen; this loads the room behind it.
//
// The branch flags Ghidra reports as `unaff_retaddr & 0x80/0x40` are NOT a
// parameter. The disassembly at 0x00481430 is:
//     MOV AL,[EAX] ; AND AL,0xC0 ; MOV byte ptr [ESP+0xb],AL
// with EAX = &record[0x0B]. So they are the top two bits of the record's own byte
// +0x0B: 0x80 = do not reload the room (camera-only transition), 0x40 = suppress
// the door sound. The low six bits are the entry camera.
//
// Destination encoding in record+0x0D: values < 0x20 are a room in the current
// stage; >= 0x20 also changes stage, as (dest >> 5) - 1, with +5 applied once
// g_PlayerFlags bit 0 is set (the second-visit stage variants).
// ============================================================================
extern unsigned int Flg_ck(int baseAddr, unsigned int bitIndex);

// Not yet transcribed. Reporting rather than silent so a missing one is visible in
// the log instead of just producing a subtly wrong room.
static void room_trans_report(const char* what)
{
    static const char* last = nullptr;
    if (what != last) { last = what; dbg_printf("[roomtrans] missing %s\n", what); }
}
static void object_delete_00442170(int a) { (void)a; room_trans_report("0x00442170 object_delete"); }
static void BuildEnemySnap(void)          { room_trans_report("0x0048f150 BuildEnemySnap"); }
static void FUN_0041d070(void)            { room_trans_report("0x0041d070"); }
static void FUN_00442180(void)            { room_trans_report("0x00442180"); }

void room_transition_load(void)
{
    unsigned char* record = (unsigned char*)g_pendingDoorRecord;
    if (record == nullptr) {
        dbg_printf("[roomtrans] g_pendingDoorRecord is NULL - nothing to load\n");
        return;
    }

    g_roomTransitionBusy   = 1;
    g_AttractModeIdleTimer = 1;

    object_delete_00442170(0);
    SetScreenOffset(160, 120);

    // 0x004813ef: latch the record's fields.
    g_nextRoomDoorType = record[0x08];
    g_nextRoomSfxId    = record[0x09];
    g_nextRoom_be05b7  = record[0x0A];
    g_nextRoomCameraId = (unsigned char)(record[0x0B] & 0x3f);
    g_nextRoomDest     = record[0x0D];
    unsigned char flags = (unsigned char)(record[0x0B] & 0xc0);

    // 0x00481438: reset the three positional sound channels to centre/default.
    for (int i = 0; i < 3; i++) {
        g_SndPanVol[i].volume = 0x5f;
        g_SndPanVol[i].pan    = 0x5f;
    }

    load_room_sfx(g_nextRoomSfxId);
    door_system_load_data();            // FUN_00412300 - load the .dor + start the texture page

    // The original does:
    //     Task_execute(1, FUN_00444770);   // spawns the door-animation task
    //     Task_sleep(1);                   // yields so it can run
    //
    // 0x00444770 is `FUN_004443c0(); FUN_00444540(); FUN_00444500(); Task_exit();`
    // - the 3D door-opening animation (DoorSystem.cpp): it sets g_main_state_flags
    // bit 0x4000000 on init, animates the door (black rect, camera dolly, door
    // panels through the TMD queue) while this task loads the destination room,
    // and clears the bit on teardown. The wait loop below polls that bit.
    door_system_start_animation();
    Task_sleep(1);

    // 0x0048148c: place the player at the destination's entry point.
    //
    // X and Z are ZERO-extended into the 32-bit matrix translation, Y is SIGN-extended.
    // That asymmetry is explicit in the original and is not a decompiler artifact:
    //
    //   0048148f: XOR EAX,EAX / MOV AX,[rec+0x0E] / MOV [0x00be6318],EAX   <- zero-ext
    //   004814a8: MOVSX EAX, word ptr [rec+0x10]  / MOV [0x00be631c],EAX   <- sign-ext
    //   004814b3: XOR EAX,EAX / MOV AX,[rec+0x12] / MOV [0x00be6320],EAX   <- zero-ext
    //
    // It makes sense: X/Z are room coordinates that legitimately exceed 0x7FFF, while
    // Y is a height that goes negative. The port sign-extended all three, so any
    // entry point with X or Z >= 0x8000 landed ~65536 units away. The position
    // SVECTOR stores are plain 16-bit copies, so only the matrix writes differ.
    unsigned short ux = *(unsigned short*)(record + 0x0E);
    short          sy = *(short*)(record + 0x10);
    unsigned short uz = *(unsigned short*)(record + 0x12);
    g_playerEntity.scaMatrixData.localMatrix.t[0] = (int)(unsigned int)ux;
    g_playerEntity.scaMatrixData.localMatrix.t[1] = (int)sy;
    g_playerEntity.scaMatrixData.localMatrix.t[2] = (int)(unsigned int)uz;
    g_playerEntity.directionAngle = *(short*)(record + 0x14);
    g_playerEntity.unk_8e     = (unsigned short)sy;
    g_playerEntity.animationId = 0;
    g_playerEntity.position.x = (short)ux;
    g_playerEntity.position.y = sy;
    g_playerEntity.position.z = (short)uz;

    // DIAGNOSTIC - remove once placement is confirmed. Prints the raw entry point the
    // door record specifies, so "the model is placed further from the door than it
    // should be" can be attributed to the data or to how we apply it.
    dbg_printf("[roomtrans] entry point from rec: x=%u y=%d z=%u ang=%d (raw %04X %04X %04X)\n",
               (unsigned int)ux, (int)sy, (unsigned int)uz,
               (int)g_playerEntity.directionAngle,
               (unsigned int)ux, (unsigned int)(unsigned short)sy, (unsigned int)uz);

    // 0x004814f9: load the destination room.
    //
    // Bit 0x80 of record+0x0B means "camera-only transition" - stay in this room and
    // just re-aim the camera, which is why the else branch below is only a
    // check_camera_switch.
    //
    // The order here is load-bearing and was what the old gate lost:
    //   1. BuildEnemySnap saves the outgoing room's enemy state
    //   2. g_AttractMode_RoomCameraId remembers which room we came from
    //   3. the SCA pool rewinds to its base, freeing the outgoing room's hit data
    //   4. g_roomId becomes the DESTINATION before room_set/init_room reads it
    //
    // Destination encoding in record+0x0D: < 0x20 is a room in the current stage;
    // >= 0x20 also changes stage, as (dest >> 5) - 1, with +5 once g_PlayerFlags
    // bit 0 is set (the second-visit stage variants). A stage change needs the
    // heavier init_room, which re-points the stage data and BGM tables first.
    if ((flags & 0x80) == 0) {
        BuildEnemySnap();
        g_AttractMode_RoomCameraId = g_roomId;
        g_scaPoolPtr = g_scaPoolBase;
        g_roomId = (unsigned char)(g_nextRoomDest & 0x1f);

        if (g_nextRoomDest < 0x20) {
            dbg_printf("[roomtrans] loading same-stage room %u (stage %u)\n",
                       (unsigned int)g_roomId, (unsigned int)g_stageId);
            room_set();
        } else {
            g_stageId = (unsigned char)((g_nextRoomDest >> 5) - 1);
            if ((Flg_ck((int)&g_PlayerFlags, 0) != 0) && (g_stageId < 2)) {
                g_stageId = (unsigned char)(g_stageId + 5);
            }
            dbg_printf("[roomtrans] loading stage %u room %u (stage change)\n",
                       (unsigned int)g_stageId, (unsigned int)g_roomId);
            init_room();
        }
    }

    g_AttractModeIdleTimer = 1;
    // Waits for the door-animation task to clear bit 0x4000000. That task is not
    // spawned yet, so nothing sets the bit and this falls straight through.
    while ((g_main_state_flags & 0x4000000) != 0) {
        Task_sleep(1);
    }

    // 0x0048156c: put the newly loaded room on screen. The camera-only branch has no
    // new data to build, so it re-runs the zone test instead.
    if ((flags & 0x80) == 0) {
        Room_SetupCamera();
        load_room_bg_image();
        Room_ApplySpriteFlags();
    } else {
        check_camera_switch(1);
    }

    // DIAGNOSTIC - remove once the transition is confirmed. Proves which room's data
    // is actually live after the load, which is the thing the old gate hid.
    dbg_printf("[roomtrans] loaded: stage=%u room=%u cam=%u rdt=%p entryCam=%u msf=%08X\n",
               (unsigned int)g_stageId, (unsigned int)g_roomId,
               (unsigned int)g_roomCameraId, (void*)g_RdtPointer,
               (unsigned int)g_nextRoomCameraId, (unsigned int)g_main_state_flags);

    update_room_bgm();
    if ((flags & 0x40) == 0) {
        play_sfx(0, 1, 0);
    }
    FUN_0041d070();
    FUN_00442180();

    g_AttractModeIdleTimer = 0;
    g_roomTransitionBusy   = 0;
}

// ============================================================================
// room_check_actions (0x004b9340)
// Dispatch table for SCD command opcode 0x24 (cmd_room_action) and 0x2D
// (cmd_got_item). 18 real entries (0x00-0x11) followed by two NULL slots in the
// original. Each handler takes a pointer to a 12-byte g_RoomItemEventTable entry.
//
// Sized correctly here so cmd_room_action's bounds check works; the handler
// bodies are still to be decompiled. Deliberately left nullptr rather than
// filled with empty placeholders - a placeholder with the real name would
// silently overload the real implementation once it lands (see the
// ScdEventEntry_Create / cmd_room_action incident above).
//
// Original entries, in table order:
//   0x00 0041c050  no_room_action          0x01 0041b400  use_mansion_key
//   0x02 0041b630  display_msg_0041b630    0x03 0041b650  include_key
//   0x04 0041b6a0  set_key_flag            0x05 0041b6d0  check_door
//   0x06 0041b790  FUN_0041b790            0x07 0041b850  FUN_0041b850
//   0x08 0041b990  open_itembox            0x09 0041b9e0  FUN_0041b9e0
//   0x0A 0041ba00  FUN_0041ba00            0x0B 0041ba10  FUN_0041ba10
//   0x0C 0041baa0  FUN_0041baa0            0x0D 0041bae0  FUN_0041bae0
//   0x0E 0041bb10  check_desk              0x0F 0041be70  (not yet analyzed)
//   0x10 0041bed0  (not yet analyzed)      0x11 0041bf90  (not yet analyzed)
// ============================================================================
// Entries land here as they are transcribed. The remaining slots stay nullptr on
// purpose: cmd_room_action and update_player_position both null-check, so a missing
// handler is an inert no-op with a diagnostic rather than a jump through garbage.
//
// [1] door_try_enter is the one the dining-room door needs (its event entry has
// act=1). Ghidra called it `use_mansion_key`, which is misleading - that is only one
// of the messages it can emit (0xc3). What it actually does is:
//   - reject the door for the wrong character (lock bit 0x40 + player id&3 == 3)
//   - run the room transition when the door is open, or already unlocked per
//     Flg_ck(g_LocksFlags, lockBits & 0x3f)
//   - otherwise look up the required item at record+0x16 and either consume it and
//     Flg_on the lock, or emit "locked" / "locked from the other side"
// The transition itself is an instant blackout - full-screen black draw_rect,
// Task_sleep(1), StMask(0,0) - not a fade.
// Blocked on g_eventItemUsedFlag, which the port does not declare yet.
void* room_check_actions[ROOM_CHECK_ACTION_COUNT] = {
    /* 0x00 */ (void*)no_room_action,        // 0x0041c050 - returns 0, does nothing
    /* 0x01 */ (void*)door_try_enter,        // 0x0041b400
    /* 0x02 */ (void*)display_msg_room_action, // 0x0041b630
    /* 0x03 */ (void*)include_key,           // 0x0041b650
    /* 0x04 */ (void*)set_key_flag,          // 0x0041b6a0
    /* 0x05 */ (void*)check_door,            // 0x0041b6d0
    /* 0x06 */ (void*)check_door_side,       // 0x0041b790
    /* 0x07 */ (void*)flag_bank_set,         // 0x0041b850
    /* 0x08 */ (void*)open_itembox,          // 0x0041b990
    /* 0x09 */ (void*)create_room_event,     // 0x0041b9e0
    /* 0x0A */ (void*)room_action_noop10,    // 0x0041ba00
    /* 0x0B */ (void*)room_action_effect,    // 0x0041ba10
    /* 0x0C */ (void*)set_stairs_zone,       // 0x0041baa0
    /* 0x0D */ (void*)set_room_event_flag,   // 0x0041bae0
    /* 0x0E */ (void*)check_desk,            // 0x0041bb10
    /* 0x0F */ (void*)pickup_key_event,      // 0x0041be70
    /* 0x10 */ (void*)check_typewriter,      // 0x0041bed0
    /* 0x11 */ (void*)stairs_height_update,  // 0x0041bf90
    /* 0x12 */ nullptr,                      // NULL in the original
    /* 0x13 */ nullptr,                      // NULL in the original
};

// ============================================================================
// room_event_item_pickup (0x00451700)
// Adds the armed room event's item to the inventory. Called by the message
// system (handle_message_post_action, message action 10/0) after the pickup
// prompt is dismissed.
//
// Reads the record at g_room_event_index+8: +8 = item id, +9 = quantity,
// +0x14 = roomItems flag index, +10 = desk slot. The entry itself is
// deactivated (first byte 0) and the desk's opened flag cleared.
//
// Stackable items (ids 0x0b-0x12 and 0x2f) merge into an existing slot first:
// up to the character's slot count ((4 - (id&3)!=1) * 2 - Chris 8, Jill 6),
// capping a slot at 0xfa and spilling the overflow into a new slot. A fresh
// slot records the first free index in g_ItemSlotIndices and raises its bit in
// g_ItemSlotsBitmask, then the menu images are rebuilt.
// ============================================================================
void room_event_item_pickup(void)
{
    unsigned char* evt = (unsigned char*)g_room_event_index;
    unsigned char* record = *(unsigned char**)(evt + 8);

    *evt = 0;                                     // deactivate the event entry
    ((unsigned char*)g_desks_pointers_table[record[10]])[0] = 0;
    if (*(short*)((char*)g_desks_pointers_table[record[10]] + 0x86) != 0) {
        g_freeEffectSlots++;
        // The original indexes the pool in DWORDs (stride 4), clearing 0x21
        // dwords = exactly one 0x84-byte effect slot.
        memset_((unsigned int*)g_effectPool +
                *(unsigned short*)((char*)g_desks_pointers_table[record[10]] + 0x86),
                0x21);
    }
    FUN_00473f10((int*)&g_roomItemsFlags, record[0x14]);

    DAT_00be9833 = record[8];
    unsigned char itemId = record[8];
    unsigned char quantity = record[9];
    if (itemId == 0x2f) {                         // '/': ammo pickup always yields 3
        quantity = 3;
    }

    if (((10 < g_selectedItemId) && (g_selectedItemId < 0x13)) ||
        (g_selectedItemId == 0x2f)) {
        // Stackable: merge into an existing slot of the same item id.
        unsigned char slotCount = (unsigned char)((4 - ((g_playerEntity.id & 3) != 1)) * 2);
        unsigned char idx = 0;
        while (slotCount != 0) {
            unsigned char* slot = (unsigned char*)g_ItemSlotsPointer + (unsigned int)idx * 2;
            if (slot[0] == itemId) {
                unsigned short merged = (unsigned short)(slot[1] + (unsigned short)quantity);
                if (merged < 0xfb) {
                    slot[1] = (unsigned char)merged;
                    return;
                }
                if (g_TotalHeldItems < slotCount) {
                    quantity = (unsigned char)(merged + 6);
                    slot[1] = 0xfa;
                    break;
                }
            }
            slotCount--;
            idx++;
        }
    }

    // New slot.
    ((unsigned char*)g_ItemSlotsPointer)[(unsigned int)g_TotalHeldItems * 2] = itemId;
    ((unsigned char*)g_ItemSlotsPointer)[1 + (unsigned int)g_TotalHeldItems * 2] = quantity;

    unsigned char freeIdx = 0;
    if ((g_ItemSlotsBitmask & 1) != 0) {
        do {
            freeIdx++;
        } while ((g_ItemSlotsBitmask & (1u << (freeIdx & 0x1f))) != 0);
    }
    unsigned int held = (unsigned int)g_TotalHeldItems;
    g_TotalHeldItems++;
    g_ItemSlotIndices[held] = freeIdx;
    g_ItemSlotsBitmask |= 1u << (freeIdx & 0x1f);
    LoadHeldItemsImages();
    StMask(0, 1);
}

// ============================================================================
// check_event_item_usage (0x0041c490)
// Per-frame: after a door or desk consumed a key item (g_eventItemUsedFlag
// raised by door_try_enter / use_room_action_item), once the prompt message
// is dismissed, physically remove the item from the inventory.
// ============================================================================
void check_event_item_usage(void)
{
    if ((g_eventItemUsedFlag == 1) && ((g_menu_choice_id & 0x80) == 0)) {
        use_room_action_item();
        g_eventItemUsedFlag = 0;
    }
}

// use_room_action_item (0x004631f0) is implemented in SaveLoadScreen.cpp.

// ============================================================================
// check_itembox_state (0x0041c240)
// Per-frame itembox lid animation. State 1 arms the travel accumulator and
// latches the lid omodel (g_itemboxes_covers_table[entry+4]); states 2/3
// rotate the lid open past -199 then let it settle back; state 4 (the box
// menu closed) resets. The lid angle lives at omodel+0x76, the step at
// g_counter_increase (reversed at the -199 stop so the lid eases back).
// ============================================================================
void check_itembox_state(void)
{
    switch (g_itembox_state) {
    case 1:
        g_short_itembox_open_timer = 1;
        g_counter_increase = 1;
        g_itembox_state = 2;
        g_itembox_cover_pointer =
            g_itemboxes_covers_table[*(unsigned short*)((char*)g_room_event_index + 4)];
        // fall through
    case 2:
        *(short*)((char*)g_itembox_cover_pointer + 0x76) -= g_short_itembox_open_timer;
        g_short_itembox_open_timer = (unsigned short)(g_short_itembox_open_timer + g_counter_increase);
        if (*(short*)((char*)g_itembox_cover_pointer + 0x76) < -199) {
            g_itembox_state = 3;
            g_counter_increase = -g_counter_increase;
        }
        break;
    case 3:
        *(short*)((char*)g_itembox_cover_pointer + 0x76) -= g_short_itembox_open_timer;
        g_short_itembox_open_timer = (unsigned short)(g_short_itembox_open_timer + g_counter_increase);
        if (g_short_itembox_open_timer < 1) {
            // Lid settled: the box menu may open.
            g_main_state_flags |= 0x1000;
            g_message_flags = 0xffff;
            g_itembox_state = 4;
            return;
        }
        break;
    case 4:
        g_itembox_state = 0;
        *(short*)((char*)g_itembox_cover_pointer + 0x76) = 0;
        return;
    }
}

// ============================================================================
// check_desk_state (0x0041bc90)
// Per-frame desk flow. States:
//   1/2  - desk is locked: prompt to use the small key (0x3d) or lockpick (0x31)
//   3    - key prompt answered: yes unlocks (LocksFlags bit at entry+2, "key
//          turned" message 0xc3), no just closes
//   4    - desk menu closed: restore the room camera, clear the opened flag
//   5    - open the take-item menu over the desk, re-arm the entry
//   35   - desk camera pan (counts down one per frame through `default`)
// Stage 3 room 10 (Chris's study) resets the flow until PlayerFlags bit 0x7b.
// ============================================================================
void check_desk_state(void)
{
    if ((g_stageId == 3) && (g_roomId == 0xa) && ((g_playerEntity.id & 3) == 1) &&
        (Flg_ck((int)g_PlayerFlags, 0x7b) == 0)) {
        g_desk_check_state = 0;
    }

    switch (g_desk_check_state) {
    case 0:
        break;
    case 1:
    case 2:
        // The original stores get_item_slot's result in a write-only scratch
        // global (has_desk_key @ 0x004d6eb4); the call itself is kept for its
        // g_pCurrentItemSlot side effect.
        (void)get_item_slot(0x3d);
        g_selectedItemId = Flg_ck((int)g_PlayerFlags, 0x7c) ? 0x31 : 0x3d;
        set_message_display(0xd9, 0xff);
        g_desk_check_state = 3;
        return;
    case 3:
        if ((g_menu_choice_id & 0x80) == 0) {
            if ((g_menu_choice_id & 1) == 0) {
                // "Yes": unlock and show the key-turned message.
                Flg_on((int)g_LocksFlags, *(unsigned short*)((char*)g_room_event_index + 2));
                play_sfx(2, 0x26, 0);
                g_selectedItemId = Flg_ck((int)g_PlayerFlags, 0x7c) ? 0x31 : 0x3d;
                set_message_display(0xc3, 0xff);
            }
            g_desk_check_state = 0;
            return;
        }
        break;
    case 4:
        display_room_camera_bg();
        g_desk_check_state = 0;
        ((unsigned char*)g_desks_pointers_table[*(unsigned short*)((char*)g_room_event_index + 4)])[0] &=
            0xfe;
        return;
    case 5:
        // Open the take-item menu; the entry re-arms to the desk's item entry.
        g_main_state_flags |= 0x800;
        ((unsigned char*)&g_message_flags)[0] |= 0x45;
        g_desk_check_state = 4;
        g_roomCameraId = g_cutId;
        g_room_event_index =
            &g_RoomItemEventTable[*(unsigned short*)((char*)g_room_event_index + 4) * 12];
        return;
    case 35:
        StMask(0, 1);
        display_room_camera_bg();
        // fall through
    default:
        // Counts the desk camera pan down to 0 (35 -> 0).
        g_desk_check_state--;
        break;
    }
}

// ============================================================================
// check_typewriter_state (0x0041c330)
// Per-frame save-point flow. State 1 prompts "use ink ribbon?" (223) or, for
// Chris before PlayerFlags bit 0x7b, "save your progress?" (224); state 2
// waits for the choice - no closes, yes fades out; state 3 calls
// LoadSaveGameState with the ribbon slot (entry+2); state 4 waits for the
// fade back in and closes.
// ============================================================================
void check_typewriter_state(void)
{
    switch (g_typewriter_state) {
    case 1:
        g_typewriter_id = *(unsigned short*)((char*)g_room_event_index + 2);
        if (((g_playerEntity.id == 1) || (g_playerEntity.id == 5)) &&
            (Flg_ck((int)g_PlayerFlags, 0x7b) == 0)) {
            set_message_display(224, 0xff);   // "Will you save your progress?"
        } else {
            set_message_display(223, 0xff);   // "Will you use the INK RIBBON?"
        }
        g_typewriter_state = 2;
        return;
    case 2:
        if ((g_menu_choice_id & 0x80) == 0) {
            if ((g_menu_choice_id & 1) != 0) {
                g_typewriter_state = 0;
                ((unsigned char*)&g_message_flags)[0] |= 0x45;
                return;
            }
            // "Yes": fade out and load the save screen.
            g_fade_type_id = 2;
            g_fading_counter = 0x1000;
            fade_update();
            g_typewriter_state = 3;
            return;
        }
        break;
    case 3:
        if ((short)g_fading_state < 0) {
            LoadSaveGameState(0, (int)g_loadDataDestPointer, (int)g_typewriter_id + 1, 2, 0);
            g_loadSaveStateFlag = 0;
            cut_set();
            g_main_state_flags = (g_main_state_flags & 0x3fffffff) | 0x80000000;
            StMask(1, 0);
            g_fade_type_id = 2;
            g_fading_counter = 0xf000;
            fade_update();
            g_typewriter_state = 4;
            return;
        }
        break;
    case 4:
        if ((short)g_fading_state < 0) {
            g_typewriter_state = 0;
            ((unsigned char*)&g_message_flags)[0] |= 0x45;
        }
        break;
    }
}

// (0x0047f960) - Lab slides: stop sound slot
void lab_slides_stop_snd(short slot) { }

// (0x0047f930) - Lab slides: set sound slot
void lab_slides_set_snd_slot(short slot) { }

// (0x0047f990) - Lab slides: set sound params
void lab_slides_set_snd_params(int a, int b, int c) { }

// (0x0046f8a0) - AddTintSprite variant for lab slides texture rendering
int AddTintSprite_Ex(TextureDesc* texture, unsigned short brightness) { return 0; }
