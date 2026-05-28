// GameState.cpp - Game state task functions (logos, title, asset loading)
// All functions decompiled from Ghidra with original addresses
#include "../Globals.h"
#include "../marni/MarniSystem.h"
#include "../marni/PSXTexture.h"
#include "FileLoader.h"
#include <cstdio>

// Forward declarations for stub functions (defined in GameStubs.cpp)
extern void clear_textures(void);
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

    g_currentFMVID = 23;
    g_selectedPlayerID = 0;
    g_main_state_flags |= 0x40000;

    Task_sleep(1);

    Task_chain((void*)title_state);
}

// ============================================================================
// debug_state — Task system diagnostics
// Tests: START dispatch, Task_sleep/resume
// ============================================================================
void debug_state(void)
{
    OutputDebugStringA("[DEBUG] Debug state - Font rendering test\n");

    load_file(".\\usa\\data\\fontus.tim", g_image_buffer, 0x20);
    g_TextureBankID = 30;
    ProcessTextureImage(g_image_buffer, 30, 0, 0);

    sprintf(PRINT_TEXT_BUFFER, "Hello World AAAAaaaaa 123456789!#$%%");
    for (int i = 0; i < 120; i++) {
        PrintText8x8(16, 50, 128, 0);
        Task_sleep(1);
    }

    OutputDebugStringA("[DEBUG] Font test done, chaining to normal flow\n");
    Task_chain((void*)load_global_assets);
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
