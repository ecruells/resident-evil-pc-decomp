// GameStubs.cpp - Game logic: initialization, asset loading, state chain
// All functions decompiled from Ghidra with original addresses
// Texture/Marni stubs marked; will be fully implemented with MarniSystem
#include "../Globals.h"
#include "../marni/MarniSystem.h"
#include "../marni/PSXTexture.h"
#include "../system/AssetPath.h"
#include "FileLoader.h"
#include "SpriteRenderer.h"
#include <cstdlib>
#include <cstdio>

// Forward declarations for internal functions
static void InitTaskDataEntry(DWORD* entry);

// ---------------------------------------------------------------------------
// Global asset loading buffer (original: DAT_00bebce8, 320x240 x 2 bytes = 153600)
// ---------------------------------------------------------------------------
static BYTE g_AssetLoadBuffer[256 * 1024];  // 256KB for texture loads
static BYTE g_ItemsImageBuffer[128 * 1024]; // 128KB for item images

// ---------------------------------------------------------------------------
// Marni texture/video stubs - will be fully implemented later
// ---------------------------------------------------------------------------
static void clear_textures(void)                                 { /* stub */ }
static void FUN_00470a30(void)                                   { /* stub */ }
static void Object_DeleteAll(int a)                              { /* stub */ }
static void SetVideoResolution(int w, int h)                     { /* stub */ }
static void setSomeColor(int r, int g, int b)                   { /* stub */ }
static void QueueVideoPlayback(int id, int b)                    { /* stub */ }
static void VideoDriver_ClearArrayD0(void)                       { /* stub */ }
static void LoadPSXImage(void* buf, int mode)                   { /* stub */ }
static int  FUN_0046c160(void* data, int mode)                  { return 0; }
int  FUN_0046c230(void* data)                                     { return 0; }
int  FUN_0046c280(int id)                                         { return 0; }
static void FUN_00427270(void)                                   { /* stub */ }
static void FUN_00427100(int a, int b, int c)                   { /* stub */ }
static int  FUN_004271e0(int a, int b)                          { return 0; }
static void FUN_00426df0(int id, void* data)                    { /* stub */ }
static void FUN_00426f70(int a, void* b)                        { /* stub */ }
static void FUN_00427250(void)                                   { /* stub */ }

// ============================================================================
// InitTaskDataEntry (0x0040abe0)
// Initializes a task data entry structure. Writes byte at offset +7 = 0x28
// and ORs the first dword with 0x05000000.
// ============================================================================
static void InitTaskDataEntry(DWORD* entry)
{
    // 0x0040abe0
    if (entry == NULL) return;
    // Write 0x28 at byte offset +7
    *((BYTE*)entry + 7) = 0x28;
    // OR the low 24 bits with 0x05000000
    *entry = (*entry & 0x000000FF) | 0x05000000;
}

// ============================================================================
// ClearGameStateFlags (0x00429a...)
// Clears 7 dwords starting at g_main_state_flags, then clears g_menu_choice_id
// ============================================================================
static void ClearGameStateFlags(void)
{
    DWORD* p = &g_main_state_flags;
    for (int i = 0; i < 7; i++) {
        *p = 0;
        p++;
    }
    g_menu_choice_id = 0;
}

// ============================================================================
// InitSoundAndFadeState (0x00429a...)
// Initializes sound status and fade state variables
// ============================================================================
static void InitSoundAndFadeState(void)
{
    // status_flags_pointer = 2;  // (used by main_loop assertion check)
    g_SndFadeType = 0;
    g_fading_state = -1;
    g_SpecialRoomLightState = (short)0xFFFF;
}

// ============================================================================
// InitInputKeyBindings (0x00497c20)
// Copies key binding configuration data from the config area to the master input state
// ============================================================================
static void InitInputKeyBindings(void)
{
    // Original: copies 32 bytes from DAT_004d4730 area to DAT_00ac4030 area
    // g_pMasterInputState = g_keyBindingData;
    for (int i = 0; i < 32; i++) {
        g_KeyBindingVectors[i] = g_KeyBindingConfig[i] & 0xFF;
    }
    g_pMasterInputState = (MasterInputState*)g_keyBindingData;
}

// ============================================================================
// InitPlayerInputData (0x004...)
// Initializes player input configuration values
// ============================================================================
static void InitPlayerInputData(void)
{
    g_PlayerDpadHeld = 0;
    g_PlayerInputConfig_3e = 0x2000;
    g_PlayerInputConfig_42 = 0x8000;
    g_PlayerInputConfig_3c = 0x1000;
    g_PlayerInputConfig_40 = 0x4000;
    g_PlayerInputConfig_44 = 0x1000;
    g_PlayerInputConfig_46 = 0x4000;
    g_PlayerInputConfig_48 = 0x80;
    g_PlayerInputConfig_52 = 4;
    g_PlayerInputConfig_4a = 0x80;
    g_PlayerInputConfig_56 = 0x10;
    g_PlayerInputConfig_58 = 0x40;
    g_PlayerInputConfig_4c = 8;
    g_PlayerInputConfig_4e = 0x20;
    g_PlayerInputConfig_50 = 8;
    g_PlayerInputConfig_54 = 0x20;
    g_PlayerInputConfig_5a = 0x80;
}

// ============================================================================
// load_all_items_images_in_buffer (0x004...)
// Loads item_all.pix into the items image buffer
// ============================================================================
static void load_all_items_images_in_buffer(void)
{
    load_file(".\\usa\\data\\item_all.pix", g_ITEMS_IMAGES_BUFFER, 0x20);
}

// ---------------------------------------------------------------------------
// logos_state (0x00442bb0)
// Logos/opening state: plays intro videos then chains to title_state.
// This runs as a task (chained from load_global_assets).
// ============================================================================
static void debug_state(void);
static void title_state(void);
static void load_global_assets(void);

static void logos_state(void)
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

    // First FMV: Virgin logo (vlogo.avi, ID 28)
    g_currentFMVID = 28;
    g_selectedPlayerID = 0;
    g_main_state_flags |= 0x40000;

    Task_sleep(3);

    // Second FMV: Capcom logo (capcom.avi, ID 23)
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
static void debug_state(void)
{
    OutputDebugStringA("[DEBUG] Debug state - Font rendering test\n");

    // Load and process the font texture
    load_file(".\\usa\\data\\fontus.tim", g_image_buffer, 0x20);
    g_TextureBankID = 30;
    ProcessTextureImage(g_image_buffer, 30, 0, 0);

    // Display test text for 120 frames (~2 seconds at 60fps)
    sprintf(PRINT_TEXT_BUFFER, "Hello World AAAAaaaaa 123456789!#$%%");
    for (int i = 0; i < 120; i++) {
        PrintText8x8(16, 50, 128, 0);
        Task_sleep(1);
    }

    // Chain to load_global_assets for normal game flow
    OutputDebugStringA("[DEBUG] Font test done, chaining to normal flow\n");
    Task_chain((void*)load_global_assets);
}

// ============================================================================
// title_state (0x00430470)
// Title screen state: displays title, waits for player selection,
// then chains to game_start, logos_state, or characterSelectionScreen.
// ============================================================================
static void title_state(void)
{
    int i;

    g_main_state_flags &= ~0x10000;
    g_PlayerPadHeldPrev = 0;
    g_playingGameFlag = 0;
    g_menu_choice_id = 0;

    setMenuScreenOffset(320, 240, 0, 0, 0);
    CenterScreenOrigin();
    clear_textures();
    set_title_render_param(192);
    sounds_reset();

    g_loadDataDestPointer = g_image_buffer;
    load_sfx(12, g_image_buffer);

    g_fading_state = -1;
    g_titleLoopFlag = 0;
    g_SpecialRoomLightState = (short)0xFFFF;
    g_titleMode = 0;
    g_titleOptionsFading = 0;

    init_title_screen();
    reset_title_pad_state(0);
    title_init_render_state((void*)0, 0, 0, 0);

    g_main_state_flags = (g_main_state_flags & 0x3FFFFFFF) | 0x40000000;
    Sound_Dispatch(0);

    setMenuScreenOffset(320, 240, 0, 0, 1);
    Task_sleep(1);

    // if (g_fmvPlayCount < 1) {
    //     g_currentFMVID = 0;
    //     g_fmvDataPointer = g_loadDataDestPointer;
    //     g_fmvPlayCount = 0x10;
    //     g_main_state_flags |= 0x40000;
    //     Task_sleep(1);
    // }

    g_main_state_flags = (g_main_state_flags & 0x3FFFFFFF) | 0x80000000;
    Task_sleep(1);

    do {
        update_title_options();
        Task_sleep(1);
    } while (g_titleLoopFlag != 0);

    if (g_GPU_VENDOR_ID == 1) {
        for (i = 180; i != 0; i--) {
            Task_sleep(1);
        }
    }

    cleanup_texture_slot(12);

    switch (g_titleSelectionId) {
    case 0:
        title_select_sfx();
        g_InputFlags |= 0x10000000;
        Task_chain((void*)game_start);
        Task_chain((void*)logos_state);
        return;

    case 1:
        title_select_sfx();
        Task_chain((void*)characterSelectionScreen);
        // Falls through to case 2/3

    case 2:
    case 3:
        g_main_state_flags = (g_main_state_flags & 0x3FFFFFFF) | 0x40000000;
        save_load_game_state(1, 0x80180000, 0, 1, 0);
        g_loadSaveStateFlag = 0;
        Game_timer = g_gameTimerSnapshot;
        g_main_state_flags = (g_main_state_flags & 0x3FFFFFFF) | 0x40000000;
        title_select_sfx();
        Task_chain((void*)game_start);

    default:
        return;
    }
}

// ============================================================================
// load_global_assets (0x00429a40)
// Task function: loads all global textures/assets needed by the game.
// After loading, chains to logos_state.
// ============================================================================
static void load_global_assets(void)
{
    // 0x00429a40: Load item images first
    load_all_items_images_in_buffer();

    // Validate image buffer pointer before loading
    if (g_image_buffer == NULL) {
        OutputDebugStringA("[ASSET] FATAL: g_image_buffer is NULL in load_global_assets!\n");
        Task_exit();
        return;
    }

    // Load font texture (0x20 flag = install prefix, skipped in dev mode)
    // Original: load_file(".\usa\data\fontus.tim", &g_image_buffer, 0x20)
    load_file(".\\usa\\data\\fontus.tim", g_image_buffer, 0x20);
    g_TextureBankID = 30;
    ProcessTextureImage(g_image_buffer, 30, 0, 0);

    OutputDebugStringA("fontus.tim loaded\n");

    // Font03t
    load_file(".\\usa\\data\\Font03t.tim", g_image_buffer, 0x20);
    g_TextureBankID = 1;
    ProcessTextureImage(g_image_buffer, 1, 0, 2);

    // Optkey03
    load_file(".\\usa\\data\\Optkey03.tim", g_image_buffer, 0x20);
    g_TextureBankID = 2;
    LoadTexturePage(g_image_buffer, 2, 0, 0xB, 0, 0, 0, 0);

    // Status screen
    load_file(".\\usa\\data\\status.tim", g_image_buffer, 0x20);
    g_TextureBankID = 0x41C;
    LoadTexturePage(g_image_buffer, 0x1C, 4, 0, 0, 0, 1, 0);

    SetupTexturePageHandles(0, 1);

    // Status face
    load_file(".\\usa\\data\\statface.tim", g_image_buffer, 0x20);
    LoadTexturePage(g_image_buffer, g_TextureBankID & 0xFF, 0, 9, 0, 0, 0, 0);

    // Blue texture
    load_file(".\\usa\\data\\blue.tim", g_image_buffer, 0x20);
    LoadTexturePage(g_image_buffer, g_TextureBankID & 0xFF, 0, 10, 0, 0, 0, 0);

    // Status item
    load_file(".\\usa\\data\\staitem.tim", g_image_buffer, 0x20);
    LoadTexturePage(g_image_buffer, 0, 0, 0x1E, 0, 0, 0, 0);

    // Kage (shadow) texture
    load_file(".\\usa\\data\\kage.tim", g_image_buffer, 0x20);
    LoadShadowMaskTexture(g_image_buffer, 0);

    // 0x00429d50: Set up 4-vertex textured quad for the shadow page
    // Layout: [x0..x3] [y0..y3] [z0..z3] [u0..u3] [v0..v3] [r,g,b,pad]
    int rectConfig[24] = {
        -400,     400,     -400,     400,       // x0-x3
           0,       0,        0,       0,       // y0-y3
         400,     400,     -400,    -400,       // z0-z3
           0,    0x1A,        0,    0x1A,       // u0-u3 (0x1A=26)
           0,       0,     0x1D,    0x1D,       // v0-v3 (0x1D=29)
           0,       1,        0,       0,       // r,g,b,pad
    };
    CreateTexturedQuad(0, 0x2F, rectConfig);

    // Sound setup
    Sound_Dispatch(0);
    
    Task_chain((void*)logos_state);
}

// ============================================================================
// init_and_start_game (0x00429920)
// Main game initialization. Sets up input, clears state, initializes task
// data entries, registers the asset loading task.
// ============================================================================
void init_and_start_game(void)
{
    // 0x00429920: Initialize key bindings and screen
    InitInputKeyBindings();
    CenterScreenOrigin();

    // 0x00429a30: Dummy function (returns immediately)
    // FUN_00429a30() - empty in original

    // Set status flags and initial audio/menu config
    // status_flags_pointer = 2;
    // Various DAT_ assignments for task data config arrays
    g_demoIdleTimer1 = 10;
    // Multiple config fields initialized (addresses 0x00be9a60-0x00be9ac8)
    // These are pointer/reference arrays for task data processing

    ClearGameStateFlags();
    InitSoundAndFadeState();

    // Set up image buffer pointers
    g_image_buffer = g_AssetLoadBuffer;
    g_ITEMS_IMAGES_BUFFER = g_ItemsImageBuffer;
    // _DAT_00d213ac = &DAT_00bee268;  (secondary buffer)

    // Set debug overlay flag: bit 30 = 1
    g_main_state_flags = (g_main_state_flags & 0x3FFFFFFF) | 0x40000000;

    // Initialize task scheduler
    TaskScheduler_Reset();

    // Set up player input
    InitPlayerInputData();

    // 0x004299a0: Initialize task data entries (two per iteration, 4 iterations)
    // The original loops from DAT_004ba750 to DAT_004ba780,
    // calling InitTaskDataEntry on pairs of entries.
    // Each entry is 0x18 bytes apart, each pair is 0x30 bytes apart.
    DWORD* pEntry = g_TaskDataArray_ba750;
    for (int i = 0; i < 4; i++) {
        InitTaskDataEntry(pEntry);
        InitTaskDataEntry(pEntry + (0x30 / sizeof(DWORD)));
        pEntry += (0x18 / sizeof(DWORD));
    }

    // 0x00429a1c: Register the asset loading task
    // This task (slot 0) will start next frame and load all global assets,
    // then chain to logos_state which plays intro videos and goes to title.
    Task_execute(0, (void*)load_global_assets);
    // Task_execute(0, (void*)debug_state);
}

// ============================================================================
// General game engine stubs (referenced from GameLoop.cpp and WindowProc.cpp)
// Will be fully decompiled from Ghidra later
// ============================================================================

void FUN_00442150(int status_flags) {
    if (status_flags != 0) { g_bFrameRateUnlocked = TRUE; }
    else { g_bFrameRateUnlocked = FALSE; }
}

void ResetScreenPanning(void) { g_ScreenOffsetX = 0; g_ScreenOffsetY = 0; }

void ApplyScreenShake(void) {
    g_ScreenOffsetX = (rand() % 5) - 2;
    g_ScreenOffsetY = (rand() % 5) - 2;
}

void FUN_004557b0(void) { g_menu_choice_id &= ~0x80; }
void FUN_00401020(int param) { /* stub */ }
void FUN_0045ab60(void) { /* stub */ }
void FUN_00497360(int r, int g, int b) { /* stub */ }
void FUN_00497340(int param) { /* stub */ }
void FUN_004973a0(int param) { /* stub */ }
void UpdateDemoTimer(void) { /* stub */ }
void StMask(int param, int param2) { /* stub */ }

void CenterScreenOrigin(void)
{
    // 0x00483650
    SetSubpixelOffset(160, 120);
    g_ScreenOffsetX = 160;
    g_ScreenOffsetY = 120;
}

// setMenuScreenOffset (0x004836a0)
// Always resets screen offset to (0, 0), arguments ignored
void setMenuScreenOffset(int w, int h, int x, int y, int mode)
{
    // 0x004836a0
    g_ScreenOffsetX = 0;
    g_ScreenOffsetY = 0;
}

void SetSubpixelOffset(int x, int y) { /* stub */ }
void CreateTimestampedLogFile(void) { /* stub */ }

// ============================================================================
// PrintText8x8 (0x00455420)
// Renders text from PRINT_TEXT_BUFFER using 8x8 font via AddTintSprite.
// Font atlas uses (ch - 0x20) based offset, 4 columns layout.
// color: upper nibble = brightness (2-30), lower nibble = CLUT tint index.
// shadow: 0 = plain, non-zero = shadow pass with offset tint.
// ============================================================================
void PrintText8x8(short x, short y, unsigned char color, char shadow)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (pD3D == NULL) return;
    if (pD3D->m_pFontSRV == NULL || pD3D->m_FontTexWidth <= 0 || pD3D->m_FontTexHeight <= 0) return;

    // 0x00455420: Compute brightness
    unsigned char brightness = color >> 4;
    if (brightness == 0) brightness = 2;

    // 0x00455430: Set texture_buffer flags
    g_TextureBuffer = ((shadow != 0) ? 0x40000000U : 0U) + 0x40;

    // 0x00455440: Set font character size (8x8)
    g_TextureVramX = 8;
    g_TextureVramY = 8;
    g_texPrintState.vramWidth = 8;
    g_texPrintState.vramHeight = 8;
    g_TextureDepth = 30;

    // 0x00455450: Set print position
    g_TexturePrintX = x - g_ScreenOffsetX;
    g_TexturePrintY = y - g_ScreenOffsetY;

    // 0x00455460: Set tint defaults
    g_PrintTintR = 128;
    g_PrintTintG = 128;
    g_PrintTintB = 128;
    g_PrintTintMode = 0;
    g_PrintTintFlagB = 0;
    g_PrintTintFlagA = 0;

    // 0x00455470: Shadow via offset CLUT tint (+8 = darker palette)
    unsigned char clutTint = color & 0xF;
    if (shadow != 0) clutTint += 8;

    // 0x00455480
    g_PrintTintA = 0x100;
    g_PrintClutTint = clutTint + 0x1E0;

    // 0x00455490: Shadow pass
    if (shadow != 0) {
        g_TexturePrintX = (x - g_ScreenOffsetX) + 1;
        g_TexturePrintY = (y - g_ScreenOffsetY) + 1;
        g_PrintTintR = 0;
        g_PrintTintG = 0;
        g_PrintTintB = 0;

        for (int i = 0; PRINT_TEXT_BUFFER[i] != '\0'; i++) {
            unsigned char ch = (unsigned char)PRINT_TEXT_BUFFER[i];
            if (ch == ' ') continue;

            unsigned char idx = ch - 0x20U;
            g_TextureVramX = (unsigned char)(idx * 8);
            g_TextureVramY = (unsigned char)((idx & 0xE3) >> 2);

            AddTintSprite((TextureDesc*)&g_texPrintState, 10);
            g_TexturePrintX += 8;
        }
    }

    // Reset for main pass
    g_TexturePrintX = x - g_ScreenOffsetX;
    g_TexturePrintY = y - g_ScreenOffsetY;
    g_PrintTintR = 128;
    g_PrintTintG = 128;
    g_PrintTintB = 128;

    // 0x004554c0: Main text pass
    for (int i = 0; PRINT_TEXT_BUFFER[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)PRINT_TEXT_BUFFER[i];

        // 0x004554d0: Skip spaces
        if (ch == ' ') {
            g_TexturePrintX += 8;
            continue;
        }

        // 0x004554e0: Compute atlas position (ch - 0x20 based offset, 4 columns)
        unsigned char idx = ch - 0x20U;
        g_TextureVramX = (unsigned char)(idx * 8);
        g_TextureVramY = (unsigned char)((idx & 0xE3) >> 2);

        // 0x00455501: Queue tinted sprite
        AddTintSprite((TextureDesc*)&g_texPrintState, brightness);

        // 0x00455510: Advance X
        g_TexturePrintX += 8;
    }
}

// ============================================================================
// PrintFormattedText (0x00455190)
// Control-code-based formatted text renderer (debug/menu text).
// Operates on a BYTE stream with opcodes for page selection, row offset,
// and spacing control. Uses 18-column, 14-pixel-tall font layout.
//
// Control codes:
//   0x00     : advance X by 8 (space)
//   0x01/0x07: return
//   0xF8     : next byte = character, TEXTURE_DEPTH=0x1E, row = next/18 + 15
//   0xF9     : next byte = character, TEXTURE_DEPTH=0x1F, row = next/18
//   0xFA     : next byte = character, TEXTURE_DEPTH=0x1F, row = next/18 + 14
//   0xFB     : ??? (falls through, just increments pointer)
//   0xFF     : advance X by 4 (half-width space)
//   default  : TEXTURE_DEPTH=0x1E, col = ch%18, row = ch/18 + 2
// ============================================================================
void PrintFormattedText(short x, short y, unsigned char color, unsigned char* data)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (pD3D == NULL) return;
    if (pD3D->m_pFontSRV == NULL || pD3D->m_FontTexWidth <= 0 || pD3D->m_FontTexHeight <= 0) return;
    if (data == NULL) return;

    // 0x00455190: Compute brightness
    unsigned char brightness = color >> 4;
    if (brightness == 0) brightness = 2;

    // 0x004551a0: Init print state
    g_TextureVramY = 14;
    g_TextureBuffer = 0x40;
    g_TexturePrintX = x - g_ScreenOffsetX;
    g_PrintTintA = 0x100;
    g_TexturePrintY = y - g_ScreenOffsetY;
    g_PrintClutTint = (color & 0xF) + 0x1E0;
    g_TextureVramX = 8;
    g_PrintTintR = 128;
    g_PrintTintG = 128;
    g_PrintTintB = 128;
    g_PrintTintMode = 0;
    g_PrintTintFlagB = 0;
    g_PrintTintFlagA = 0;

    g_texPrintState.vramWidth = 8;
    g_texPrintState.vramHeight = 14;

    // 0x004551d0: Parse control code stream
    unsigned char row = 0;
    unsigned char texDepth = 0x1E;
    unsigned char ch = *data;
    while (ch != 1 && ch != 7) {
        ch = *data;

        if (ch == 0) {
            // 0x004552da: Advance X by 8 (space/end of command)
            g_TexturePrintX += 8;
        } else {
            switch (ch) {
            case 1:
            case 7:
                // 0x00455258: Return
                return;

            case 0xF8:
                // 0x00455265: next byte = char, row = next/18 + 15, page 0x1E
                data++;
                row = *data / 0x12 + 0xF;
                texDepth = 0x1E;
                ch = *data;
                goto render;

            case 0xF9:
                // 0x004552e0: next byte = char, row = next/18, page 0x1F
                data++;
                row = *data / 0x12;
                texDepth = 0x1F;
                ch = *data;
                goto render;

            case 0xFA:
                // 0x004552ad: next byte = char, row = next/18 + 14, page 0x1F
                data++;
                row = *data / 0x12 + 14;
                texDepth = 0x1F;
                ch = *data;
                goto render;

            case 0xFB:
                // 0x004552f2: ??? (falls through, pointer advanced below)
                break;

            case 0xFF:
                // 0x00455300: Half-width advance (4px)
                g_TexturePrintX += 4;
                break;

            default:
                // 0x00455275: Normal char — col = ch%18, row = ch/18 + 2, page 0x1E
                row = ch / 0x12 + 2;
                texDepth = 0x1E;
                goto render;
            }
        }

        // Next byte
        data++;
        ch = *data;
        continue;

render:
        // 0x004552b6: Set atlas coords and queue sprite
        g_TextureDepth = texDepth;
        g_TextureVramY = row * 14;
        g_TextureVramX = (ch % 0x12) * 8;
        AddTintSprite((TextureDesc*)&g_texPrintState, brightness);

        // 0x004552da: Advance X
        g_TexturePrintX += 8;
        data++;
        ch = *data;
    }
}

// ============================================================================
// Pending sprite queue (filled by display_texture / AddTintSprite,
// rendered by FrameRateGovernor)
// ============================================================================
#define MAX_PENDING_SPRITES 300

struct PendingSprite {
    float x, y, w, h;
    float u0, v0, u1, v1;
    DWORD color;
    ID3D11ShaderResourceView* srv;
    BOOL valid;
};
static PendingSprite g_pendingSprites[MAX_PENDING_SPRITES];
static int g_pendingSpriteCount = 0;

// ============================================================================
// PrintText8x14 (0x00455520)
// Renders text from PRINT_TEXT_BUFFER using 8x14 font via AddTintSprite.
// Characters are laid out 18 per row in the font texture atlas.
// color: upper nibble = brightness (2-30), lower nibble = CLUT tint index.
// flags: 0 = no shadow, non-zero = shadow pass.
// ============================================================================
void PrintText8x14(short x, short y, unsigned char color, char flags)
{
    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (pD3D == NULL) return;

    if (pD3D->m_pFontSRV == NULL || pD3D->m_FontTexWidth <= 0 || pD3D->m_FontTexHeight <= 0) {
        OutputDebugStringA("[TEXT] Font not loaded yet, skipping\n");
        return;
    }

    // 0x00455520: Compute brightness
    unsigned char brightness;
    if ((color & 0x80) == 0) {
        brightness = color >> 4;
        if (brightness == 0) brightness = 2;
    } else {
        brightness = 30;
    }

    // 0x00455530: Set texture_buffer flags
    g_TextureBuffer = ((flags != 0) ? 0x40000000U : 0U) + 0x40;

    // 0x00455550: Set font character size (vramAreaX/Y store position, vramWidth/Height store size)
    g_TextureVramX = 8;
    g_TextureVramY = 14;
    g_texPrintState.vramWidth = 8;
    g_texPrintState.vramHeight = 14;
    g_TextureDepth = 0x1E;

    // 0x00455567: Set print position
    g_TexturePrintX = x - g_ScreenOffsetX;
    g_TexturePrintY = y - g_ScreenOffsetY;

    // 0x0045557b: Set tint defaults
    g_PrintTintR = 0x80;
    g_PrintTintG = 0x80;
    g_PrintTintB = 0x80;
    g_PrintTintA = 0x100;
    g_PrintTintMode = 0;
    g_PrintTintFlagA = 0;
    g_PrintTintFlagB = 0;

    // 0x004555b0: CLUT tint from lower nibble
    g_PrintClutTint = (color & 0xF) + 0x1E0;

    // 0x004555c3: Shadow pass
    if (flags != 0) {
        g_TexturePrintX = (x - g_ScreenOffsetX) + 1;
        g_TexturePrintY = (y - g_ScreenOffsetY) + 1;
        g_PrintTintR = 0;
        g_PrintTintG = 0;
        g_PrintTintB = 0;

        for (int i = 0; PRINT_TEXT_BUFFER[i] != '\0'; i++) {
            unsigned char ch = (unsigned char)PRINT_TEXT_BUFFER[i];
            if (ch == ' ') continue;

            if (ch == '(')      { g_TextureVramX = 56;  g_TextureVramY = 224; }
            else if (ch == ')') { g_TextureVramX = 70;  g_TextureVramY = 224; }
            else                { g_TextureVramX = (ch % 18) * 8; g_TextureVramY = (ch / 18) * 14; }

            AddTintSprite((TextureDesc*)&g_texPrintState, 10);
            g_TexturePrintX += 8;
        }
    }

    // Reset for main pass
    g_TexturePrintX = x - g_ScreenOffsetX;
    g_TexturePrintY = y - g_ScreenOffsetY;
    g_PrintTintR = 0x80;
    g_PrintTintG = 0x80;
    g_PrintTintB = 0x80;

    // 0x004555c7: Main text pass — iterate through PRINT_TEXT_BUFFER
    for (int i = 0; PRINT_TEXT_BUFFER[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)PRINT_TEXT_BUFFER[i];

        // 0x004555d0: Skip space characters
        if (ch == ' ') {
            g_TexturePrintX += 8;
            continue;
        }

        // 0x004555e0: Compute VRAM position in font atlas (18 chars per row, 8x14 each)
        if (ch == '(')      { g_TextureVramX = 56;  g_TextureVramY = 224; }
        else if (ch == ')') { g_TextureVramX = 70;  g_TextureVramY = 224; }
        else                { g_TextureVramX = (ch % 18) * 8; g_TextureVramY = (ch / 18) * 14; }

        // 0x00455630: Darkness override for specific room
        unsigned char finalBrightness = brightness;
        if ((g_STAGE_ID == 3) && (g_ROOM_ID == 17) && (g_roomCamera_id == 4)) {
            finalBrightness = 0;
        }

        // 0x00455649: Queue tinted sprite
        AddTintSprite((TextureDesc*)&g_texPrintState, finalBrightness);

        // 0x00455660: Advance X position by 8 pixels
        g_TexturePrintX += 8;
    }
}

// ============================================================================
// AddTintSprite (0x0046e0a0)
// Builds a color-tinted sprite from the texture print state globals
// and queues it for rendering in game_frame_present via the pending sprite queue.
// ============================================================================
int AddTintSprite(TextureDesc* texture, unsigned short brightness)
{
    if (g_pendingSpriteCount >= MAX_PENDING_SPRITES) return 0;

    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (pD3D == NULL || pD3D->m_pFontSRV == NULL) return 0;
    if (pD3D->m_FontTexWidth <= 0 || pD3D->m_FontTexHeight <= 0) return 0;

    // Screen position: TEXTURE_PRINT_POS was set as (x - offset), add offset back
    // 0x0046e1a0: sVar5 = printPosX + g_ScreenOffsetX
    // 0x0046e1b0: y0 = sVar5 - tintMode  (tintMode = 0 for font)
    float gameX = (float)(g_TexturePrintX + g_ScreenOffsetX);
    float gameY = (float)(g_TexturePrintY + g_ScreenOffsetY);

    // Scale from game coords (320x240) to screen coords
    float scaleX = (float)pD3D->m_width / 320.0f;
    float scaleY = (float)pD3D->m_height / 240.0f;

    float screenX = gameX * scaleX;
    float screenY = gameY * scaleY;
    float charW = (float)g_texPrintState.vramWidth * scaleX;
    float charH = (float)g_texPrintState.vramHeight * scaleY;

    // UV from font texture atlas: vramAreaX/Y are byte values
    // 0x0046e2d0: u0 = (ushort)*(byte *)((int)pak + 0xe)
    // 0x0046e2e0: v0 = (ushort)*(byte *)((int)pak + 0xf)
    float texW = (float)pD3D->m_FontTexWidth;
    float texH = (float)pD3D->m_FontTexHeight;
    float u0 = (float)g_TextureVramX / texW;
    float v0 = (float)g_TextureVramY / texH;

    // 0x0046e2f0: u1 = u0 + vramWidth - 1, v1 = v0 + vramHeight - 1
    float u1 = (float)(g_TextureVramX + g_texPrintState.vramWidth) / texW;
    float v1 = (float)(g_TextureVramY + g_texPrintState.vramHeight) / texH;

    // Tint color values are PS1 7-bit (0-128), scale to D3D11 8-bit (0-255)
    unsigned char a = (unsigned char)((unsigned int)brightness * 255 / 30);
    if (brightness <= 2) a = 17;
    unsigned char r = (unsigned char)((g_PrintTintR & 0xFF) * 2); if (r > 255) r = 255;
    unsigned char g = (unsigned char)((g_PrintTintG & 0xFF) * 2); if (g > 255) g = 255;
    unsigned char b = (unsigned char)((g_PrintTintB & 0xFF) * 2); if (b > 255) b = 255;
    DWORD color = (a << 24) | (r << 16) | (g << 8) | b;

    PendingSprite* spr = &g_pendingSprites[g_pendingSpriteCount];
    spr->x = screenX;
    spr->y = screenY;
    spr->w = charW;
    spr->h = charH;
    spr->u0 = u0;
    spr->v0 = v0;
    spr->u1 = u1;
    spr->v1 = v1;
    spr->color = color;
    spr->srv = pD3D->m_pFontSRV;
    spr->valid = TRUE;

    g_pendingSpriteCount++;
    return 1;
}

// ============================================================================
// draw_rect (0x00470350)
// Draws a solid-color or textured rectangle via the sprite command buffer.
// Queues a pending sprite for rendering during game_frame_present.
//
// rect:  rectangle descriptor (position, size, color, optional textureId)
// blend: opacity fade value (0 = fully opaque, 255 = fully transparent)
// flags: 0 = additive depth sort (fade+450), non-0 = layered (fade*16+500)
// ============================================================================
void draw_rect(RectDrawDesc* rect, int blend, int flags)
{
    if (rect == NULL) return;
    if (g_pendingSpriteCount >= MAX_PENDING_SPRITES) return;

    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (pD3D == NULL) return;

    // 0x00470350: Compute game-space position (rect coords + screen offset)
    float gameX = (float)(rect->x + g_ScreenOffsetX);
    float gameY = (float)(rect->y + g_ScreenOffsetY);
    float gameW = (float)rect->w;
    float gameH = (float)rect->h;

    // 0x004704a0: Scale from game coords (320x240) to screen coords
    float scaleX = (float)pD3D->m_width / 320.0f;
    float scaleY = (float)pD3D->m_height / 240.0f;

    float screenX = gameX * scaleX;
    float screenY = gameY * scaleY;
    float screenW = gameW * scaleX;
    float screenH = gameH * scaleY;

    // 0x004703b0: Color from rect descriptor
    unsigned char r = (unsigned char)(rect->r & 0xFF);
    unsigned char g = (unsigned char)(rect->g & 0xFF);
    unsigned char b = (unsigned char)(rect->b & 0xFF);

    // 0x00470350: fade controls transparency: 0 = opaque, higher = more transparent
    int alpha = 255 - blend;
    if (alpha < 0) alpha = 0;
    if (alpha > 255) alpha = 255;
    unsigned char a = (unsigned char)alpha;

    DWORD color = (a << 24) | (r << 16) | (g << 8) | b;

    // 0x004704b0: textureId == 0 means solid color (use white 1x1 texture as fill)
    ID3D11ShaderResourceView* srv = NULL;
    if (rect->textureId == 0) {
        srv = pD3D->m_pWhiteSRV;
    } else {
        // Look up texture page SRV for non-zero textureId
        int shiftedSlot = (rect->textureId & 0xFF) + 0xF;
        if (shiftedSlot >= 0 && shiftedSlot < 256) {
            srv = g_TexturePageSRV[shiftedSlot];
        }
    }

    // 0x004706e0: Queue sprite for rendering
    PendingSprite* spr = &g_pendingSprites[g_pendingSpriteCount];
    spr->x = screenX;
    spr->y = screenY;
    spr->w = screenW;
    spr->h = screenH;
    spr->u0 = 0.0f;
    spr->v0 = 0.0f;
    spr->u1 = 1.0f;
    spr->v1 = 1.0f;
    spr->color = color;
    spr->srv = srv;
    spr->valid = TRUE;

    g_pendingSpriteCount++;

    (void)flags;
}

// ============================================================================
// FrameRateGovernor (0x004973d0)
//
// Adaptive frame rate governor. Measures frame time deltas in a 4-entry ring
// buffer, computes a running average, and only presents a frame when the
// accumulated time budget reaches the target.  Also manages screen/render
// access state and resets the sprite queue after each present.
//
// The frame target is clamped to [100, 800] units (relative to a 100-unit
// per-call accumulator), giving an effective range of ~60 FPS to ~8 FPS.
// ============================================================================

// Title image/buffer resources
static BYTE g_displayImageBuffer[320 * 240 * 2];
static ID3D11ShaderResourceView* g_displayImageSRV = NULL;

// Title text atlas sprite heights
static float g_titleCurrentSprH = 60.0f;

void FrameRateGovernor(void)
{
    // 0x004973d4: Increment frame counter
    g_numFramesRendered++;

    // 0x004973e0: Record frame time delta
    DWORD currentTime = timeGetTime();
    int frameDelta = 0;

    if (g_LastFrameTime_ms != 0) {
        frameDelta = (int)(currentTime - g_LastFrameTime_ms);

        // 0x00497410: Only record if delta < 300ms (ignore huge gaps)
        if (frameDelta < 300) {
            if (g_bFrameRateUnlocked) {
                // Unlocked: store 2 entries per sample
                g_frameTimeIndex += 2;
                g_frameTimeBuffer[g_frameTimeIndex & 3] = frameDelta;
                g_frameTimeBuffer[(g_frameTimeIndex - 1) & 3] = frameDelta;
            } else {
                g_frameTimeIndex++;
                g_frameTimeBuffer[g_frameTimeIndex & 3] = frameDelta;
            }

            // 0x00497450: Wrap index at 4
            if (g_frameTimeIndex > 3) g_frameTimeIndex = 0;
        }
    }

    // 0x00497460: Compute average frame time every 4 samples
    if (g_frameTimeIndex == 0) {
        int frameTimeSum = 0;
        for (int i = 0; i < 4; i++) {
            frameTimeSum += g_frameTimeBuffer[i];
        }

        // 0x00497490: Average with different formulas
        if (!g_bFrameRateUnlocked) {
            // Normal: sum * 100 / 64 (≈ sum * 1.5625)
            g_frameTargetTime = (int)((frameTimeSum * 100 + (frameTimeSum * 100 >> 31 & 0x3FU)) >> 6);
        } else {
            // Unlocked: sum * 100 / 132 (≈ sum * 0.7576)
            g_frameTargetTime = (frameTimeSum * 100) / 132;
        }

        // 0x004974c0: Sound-based adjustment (stubbed – no sound system yet)
        // Original adjusts g_frameTargetTime based on BGM sound bank status

        // 0x00497510: Clamp target to [100, 800]
        if (g_frameTargetTime < 100) g_frameTargetTime = 100;
        g_bFrameSkipDetected = (g_frameTargetTime > 100);
        if (g_frameTargetTime > 800) g_frameTargetTime = 800;
    }

    // 0x0049753d: Accumulate time budget
    g_frameTimeAccumulator += 100;

    if (g_frameTimeAccumulator < g_frameTargetTime) {
        // 0x00497550: Not enough budget — skip present, clear stale sprites
        g_LastFrameTime_ms = 0;
        g_pendingSpriteCount = 0;
        SpriteQueue_Reset();
    } else {
        // 0x00497563: Budget met — present frame
        if (g_ScreenAccessReady && g_RenderAccessReady) {
            // 0x00497580: vtable[3] = Clear
            MarniClear();

            // 0x00497590: FUN_0040a8f0 — inserts background prim into ordering table
            FUN_0040a8f0(NULL);

            // Render all pending sprites (queued during task execution)
            for (int i = 0; i < g_pendingSpriteCount; i++) {
                if (g_pendingSprites[i].valid) {
                    MarniDrawSprite(
                        g_pendingSprites[i].x, g_pendingSprites[i].y,
                        g_pendingSprites[i].w, g_pendingSprites[i].h,
                        g_pendingSprites[i].u0, g_pendingSprites[i].v0,
                        g_pendingSprites[i].u1, g_pendingSprites[i].v1,
                        g_pendingSprites[i].color,
                        g_pendingSprites[i].srv);
                }
            }

            // Flush TextureDraw command buffer (filled by display_texture etc.)
            FlushSpriteCommands();

            // 0x004975b0: vtable[4] = Present (if pad not disabled)
            if (!g_DisablePad) {
                MarniPresent();
            }
            g_numFramesPresented++;

            // 0x0049763e: Reset sprite queue after present
            ResetSpriteQueue();
        }

        // 0x004975d0: Subtract target from accumulator
        g_frameTimeAccumulator -= g_frameTargetTime;
        if (g_frameTimeAccumulator < 0) g_frameTimeAccumulator = 0;

        // 0x004975f0: Record present timestamp
        g_LastFrameTime_ms = currentTime;

        // If accumulator exceeded target by more than target, reset
        if (g_frameTimeAccumulator > g_frameTargetTime) {
            g_frameTimeAccumulator = 0;
        }
    }

    // 0x00497610: Screen/render access countdown (ready at 0, counts down from initial)
    if (g_ScreenAccessCheck != 0) {
        g_ScreenAccessCheck--;
        if (g_ScreenAccessCheck == 0) g_ScreenAccessReady = 1;
    }
    if (g_RenderAccessCheck != 0) {
        g_RenderAccessCheck--;
        if (g_RenderAccessCheck == 0) g_RenderAccessReady = 1;
    }

    // 0x00497630: vtable[11] = ResetTextures
    // 0x00497635: field792_0x324 = 3  (render state reset)
}

// ============================================================================
// FUN_0040a8f0 (0x0040a8f0)
// Sets up a full-screen background primitive and inserts it into the ordering
// table at depth 0xFFF.  Called every frame between Clear and Present.
// ============================================================================
void FUN_0040a8f0(void* param)
{
    g_titlePrimType = 1;
    g_primFlag2 = 2;
    g_primParam = g_sceneRenderParam;
    OT_InsertPrimitive(&g_titlePrimType, 0xFFF);
}

// ============================================================================
// OT_InsertPrimitive (0x004402f0)
// Inserts a primitive descriptor into the ordering table at the given depth.
// Original: linked-list insert into PSYQ ordering table. D3D11 port: if the
// prim at depth 0xFFF references the display image, queues it as a full-screen
// background sprite.
// ============================================================================
void OT_InsertPrimitive(void* prim, unsigned int depth)
{
    if (depth != 0xFFF) return;

    DWORD* p = (DWORD*)prim;
    if (p[0] != 1) return;

    if (g_displayImageSRV == NULL) return;
    if ((g_main_state_flags & 0x40000000) != 0) return;
    if (g_pendingSpriteCount >= MAX_PENDING_SPRITES) return;

    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;

    for (int i = g_pendingSpriteCount; i > 0; i--) {
        g_pendingSprites[i] = g_pendingSprites[i - 1];
    }
    g_pendingSprites[0].x = 0;
    g_pendingSprites[0].y = 0;
    g_pendingSprites[0].w = (float)(pD3D ? pD3D->m_width : 640);
    g_pendingSprites[0].h = (float)(pD3D ? pD3D->m_height : 480);
    g_pendingSprites[0].u0 = 0;
    g_pendingSprites[0].v0 = 0;
    g_pendingSprites[0].u1 = 1;
    g_pendingSprites[0].v1 = 1;
    g_pendingSprites[0].color = 0xFFFFFFFF;
    g_pendingSprites[0].srv = g_displayImageSRV;
    g_pendingSprites[0].valid = TRUE;
    g_pendingSpriteCount++;
}
void ResetSpriteQueue(void)                                  { g_pendingSpriteCount = 0; SpriteQueue_Reset(); }
void ShowVideoModeDebugText(void) { /* stub */ }

// ============================================================================
// display_texture (0x0046e8d0)
// Queues a textured sprite for rendering based on the texture descriptor.
// Writes directly to g_SpriteCommandBuffer (TextureDraw format).
// ============================================================================
void display_texture(TextureDesc* texture, unsigned short depth, int slot, int pageCount)
{
    if (g_SpriteQueueCount >= MAX_SPRITE_COMMANDS - 1) return;
    if ((g_main_state_flags & 0x40000000) != 0) return;

    // Verify the texture page slot exists
    int shiftedSlot = slot + 0xF;
    if (shiftedSlot < 0 || shiftedSlot >= 256) return;
    if (g_TexturePageSRV[shiftedSlot] == NULL) return;

    TextureDraw* cmd = &g_SpriteCommandBuffer[g_SpriteQueueCount];
    cmd->type = 10;

    // Render flags from texture flags
    unsigned int flags;
    BuildSpriteRenderFlags(texture->flags, &flags);
    int variant = GetTextureVariant(texture->flags);
    cmd->unk1c = (float)((variant != 0) ? (flags | 8) : flags);

    // Color from descriptor
    cmd->r = (float)texture->colorMulR * g_ColorScaleFactor;
    cmd->g = (float)texture->colorMulG * g_ColorScaleFactor;
    cmd->b = (float)texture->colorMulB * g_ColorScaleFactor;

    cmd->texturePage = 0;

    // Position (screen coordinates + offset, minus pivot)
    short sx = texture->screenX + g_ScreenOffsetX;
    short sy = texture->screenY + g_ScreenOffsetY;
    cmd->x0 = sx - texture->pivotX;
    cmd->y0 = sy - texture->pivotY;
    cmd->x1 = (texture->width - texture->pivotX) + sx - 1;
    cmd->y1 = (texture->height - texture->pivotY) + sy - 1;

    // Depth sort
    cmd->depthSort = (unsigned int)depth * 16 + 500;

    // UV coordinates (direct texel positions within the page)
    cmd->u0 = (unsigned short)texture->texU;
    cmd->v0 = (unsigned short)texture->texV;
    cmd->u1 = cmd->u0 + texture->width - 1;
    cmd->v1 = cmd->v0 + texture->height - 1;

    // Store the texture page slot index for rendering
    cmd->extraFlags = shiftedSlot;

    g_SpriteQueueCount++;
}

// ============================================================================
// Title state function implementations
// ============================================================================
// display_image (0x00470770)
// Loads raw 16-bit PS1 pixel data and creates a D3D11 texture + SRV.
// The SRV is stored globally; FrameRateGovernor queues it as a full-screen
// background sprite each frame when set (replaces the original's persistent
// PSYQ display list auto-rendering).
// ============================================================================
void display_image(int slot, void* buffer, int width, int height)
{
    g_DisplayImageWidth = width;
    g_DisplayImageHeight = height;

    if (g_displayImageSRV != NULL) {
        g_displayImageSRV->Release();
        g_displayImageSRV = NULL;
    }

    unsigned short* src = (unsigned short*)buffer;
    int pixelCount = width * height;
    unsigned int* rgba = (unsigned int*)malloc(pixelCount * 4);
    if (rgba == NULL) return;

    for (int i = 0; i < pixelCount; i++) {
        unsigned short px = src[i];
        unsigned char r = ((px >> 0)  & 0x1F) * 255 / 31;
        unsigned char g = ((px >> 5)  & 0x1F) * 255 / 31;
        unsigned char b = ((px >> 10) & 0x1F) * 255 / 31;
        unsigned char a = (px & 0x8000) ? 0x80 : 0xFF;
        rgba[i] = (a << 24) | (b << 16) | (g << 8) | r;
    }

    ID3D11Texture2D* tex = NULL;
    MarniCreateTexture(width, height, 32, rgba, &tex, &g_displayImageSRV);
    free(rgba);
    if (tex != NULL) tex->Release();
}

// set_display_resolution (0x00401000)
void set_display_resolution(int w, int h, int mode)
{
    g_displayWidth = w;
    g_displayHeight = h;
    g_displayMode = mode;
}

// title_reset_display_list (0x00470960)
void title_reset_display_list(int slot)
{
    // Empty in original
}

// check_save_files_exist (0x00494190)
// Returns 1 if any save files exist, 0 otherwise.
int check_save_files_exist(void)
{
    char path[260];
    for (int i = 1; i <= 8; i++) {
        sprintf(path, ".\\savedat%d.dat", i);
        FILE* f = fopen(path, "rb");
        if (f != NULL) {
            fclose(f);
            return 1;
        }
    }
    return 0;
}

// title_setup_texture_pages (0x00470970)
// Creates texture pages for button prompt images.
void title_setup_texture_pages(int slot, int mode)
{
    // Destroy existing textures for this slot
    int slotBase = slot * 0x37C;
    int pageCount = g_VideoDriverArray_814[slotBase / 4];
    if (pageCount != 0) {
        DWORD* pageTable = (DWORD*)((BYTE*)&g_TexturePageTable_DAT + slotBase);
        for (int i = 0; i < pageCount; i++) {
            if (pageTable[i] != 0) {
                destroy_texture_page(pageTable[i]);
                pageTable[i] = 0;
            }
        }
    }

    // Create texture pages (up to 2)
    BYTE* pageData = (BYTE*)&g_VideoDriverArray_4d0 + slot * 0x37C;
    for (int i = 0; i < 2; i++) {
        int handle = create_texture_page(pageData, (mode != 0) ? 26 : 10);
        if (handle == 0) break;
        pageData += 0x68;
    }

    g_titleTextureSlotId = slot;
}

// init_title_screen (0x004306e0)
// Loads title.pix, button prompt textures, and initializes title screen state.
void init_title_screen(void)
{
    g_StackPointer = 0;   // status_flags_pointer = 0
    set_display_resolution(320, 240, 0);

    g_roomCamera_id = 0;

    load_file(".\\usa\\data\\title.pix", g_displayImageBuffer, 0x20);
    display_image(0, g_displayImageBuffer, 320, 240);

    title_setup_texture_pages(0, 1);

    title_reset_display_list(0);

    const char* buttonTexPath;
    if (!g_bIsSideWinderConnected) {
        buttonTexPath = ".\\usa\\data\\t_press.tim";
    } else {
        buttonTexPath = ".\\usa\\data\\t_start.tim";
    }
    load_file(buttonTexPath, g_displayImageBuffer, 0x20);

    g_titleTextureDepthData[0] = 8;   // 0x004306e0: _DAT_00d22778 = 8
    g_titleTextureDepthData[1] = 8;   // 0x004306e0: _DAT_00d2277a = DAT_00bebcc4 (copied from texture load)
    g_titleTextureDepthData[2] = 8;   // 0x004306e0: _DAT_00d2277c = _DAT_00d2277a (copied from [1])
    g_titleTextureDepthData[4] = 0x1A; // 0x004306e0: _DAT_00d22780 = 26 (texture page param for other data)
    g_titleTextureDepthData[5] = 0x1A; // 0x004306e0: _DAT_00d22782 = DAT_00bebcc5 (copied from texture load)
    g_titleTextureDepthData[6] = 0x1A; // 0x004306e0: _DAT_00d22784 = _DAT_00d22782 (copied from [5])
    g_TextureBankID = 8;
    LoadTexturePage(g_displayImageBuffer, 8, 0, 12, 4, 0, 0, 0);

    {
        char dbg[256];
        sprintf(dbg, "[INIT] LoadTexturePage done, SRV[27]=%p\n", g_TexturePageSRV[27]);
        OutputDebugStringA(dbg);
    }

    g_titleLoopFlag = 1;

    if (check_save_files_exist()) {
        g_titleSelectionId = 2;
        g_main_state_flags &= ~0x40000000;
        return;
    }
    g_titleSelectionId = 1;
    g_main_state_flags &= ~0x40000000;
}

// title_init_render_state (0x0040abb0)
// Initializes render state for title screen rendering.
void title_init_render_state(void* ptr, int a, int b, int c) { /* stub */ }

// set_title_render_param (0x0040a8e0)
// Sets a global render parameter used during title screen.
void set_title_render_param(int value)
{
    g_sceneRenderParam = value;
}

// cleanup_texture_slot (0x0046cfd0)
// Clears texture page entries for a given texture slot.
void cleanup_texture_slot(int slot) { /* stub */ }

// title_select_sfx (0x0047eb80)
// Sound effect played when player confirms a title menu selection.
void title_select_sfx(void) { /* stub */ }

// reset_title_pad_state (0x00497c10)
// Resets title screen input/pad state.
void reset_title_pad_state(int value) { /* stub */ }

// sounds_reset (0x0047eb70)
// Resets the sound system state.
void sounds_reset(void) { /* stub */ }

// load_sfx (0x0047ed30)
// Loads a sound effect bank into a buffer.
void load_sfx(int sound_id, void* buffer) { /* stub */ }

// fade_update (0x0047b950)
// Updates fade state based on fade counter.
// When counter is positive (fading in progress): g_fading_state = 0
// When counter is zero or negative (fade complete): g_fading_state = 0x7FFF
void fade_update(void)
{
    if ((short)g_fading_state <= 0 && g_fading_counter != 0) {
        if ((short)g_fading_counter <= 0) {
            g_fading_state = 0x7FFF;
        } else {
            g_fading_state = 0;
        }
    }
}

// read_sidewinder_pad (0x00497e30)
// Returns SideWinder gamepad raw input state.
int read_sidewinder_pad(void)
{
    return g_PadRawP2;
}

// play_sfx (0x0047fb10)
// Triggers a sound effect. bank=0 room, 1=sfx, 2=enemy, 3=character, 4=special
void play_sfx(int bank, int soundId)
{
    // Sound system not yet implemented
}

// title_exit_loop (0x00430e10)
// Exits the title menu loop by clearing the loop flag.
void title_exit_loop(void)
{
    g_titleLoopFlag = 0;
    g_main_state_flags = (g_main_state_flags & 0x3FFFFFFF) | 0x40000000;
}

struct TitleTextPosData {
    int vramY;
    int sprHeight;
    int screenY;
};

// Atlas layout determined from TIM file (t_press.tim, 256x256 4-bit CLUT):
static const TitleTextPosData g_titleTextPosTable[3] = {
    // vramY, sprHeight, screenY, 
    { 0,        54,        24 },  // index 0: "PRESS ANY BUTTON"
    { 83,       70,         8 },  // index 1: "NEW GAME"
    { 175,      70,         8 },  // index 2: "LOAD GAME"
};

// UpdateTitleTextSprite (0x00430d40)
// Sets up a TextureDesc for rendering title menu text from the TIM texture atlas.
// param_1 controls color fade (0 = transparent, 0x80 = fully visible).
// selection_id picks the text sprite area: 0 = "PRESS ANY BUTTON", 1 = "NEW GAME", 2 = "LOAD GAME"
void UpdateTitleTextSprite(unsigned char brightness, unsigned char selectionId)
{
    TextureDesc* td = (TextureDesc*)&g_texPrintState;

    td->flags = 0x10000000;
    if (brightness != 0x80) {
        td->flags = 0x40000000;
    }

    const TitleTextPosData* entry = &g_titleTextPosTable[selectionId];

    td->texU = 0;
    td->screenX = -130;
    td->depth = g_titleTextureDepthData[selectionId];
    td->width = 256;
    td->texV = (unsigned char)entry->vramY;
    td->height = entry->sprHeight;
    td->screenY = entry->screenY + 38;
    g_titleCurrentSprH = (float)entry->sprHeight;

    td->colorMulR = brightness;
    td->unk10 = 0;
    td->colorMulG = brightness;
    td->pivotX = 0;
    td->pivotY = 0;
    td->colorMulB = brightness;

    td->printClutTint = 0x1E0;

    display_texture(td, 2, 12, 1);
}

// update_title_options (0x00430810)
// Handles title screen input, menu navigation, fade transitions,
// and text rendering for both attract mode and main menu mode.
void update_title_options(void)
{
    // Sidewinder edge detection (original uses _g_PlayerPadHeldPrev)
    DWORD sidewinderPress = 0;
    DWORD sidewinderState = 0;
    if (g_bIsSideWinderConnected) {
        sidewinderState = read_sidewinder_pad();
        sidewinderPress = sidewinderState & 0x10000 & ~g_PlayerPadHeldPrev;
    }
    g_PlayerPadHeldPrev = sidewinderState;

    // Keyboard edge detection — only new presses (not held across frames)
    static DWORD prevPad = 0;
    DWORD justPressed = g_PlayerPadPressed & ~prevPad;
    prevPad = g_PlayerPadPressed;

    // ============================================
    // MAIN MENU MODE (g_titleMode == 1)
    // New Game / Load Game selection
    // ============================================
    if (g_titleMode != 0) {
        if (g_titleMode != 1) return;

        switch (g_titleOptionsFading) {
        case 0:
            g_titleOptionsFading = 1;
            g_fade_type_id = 2;
            g_fading_counter = (short)0xFC00;
            g_main_state_flags = (g_main_state_flags & 0x3FFFFFFF) | 0x80000000;
            fade_update();
            break;

        case 1:
            if (g_fading_state < 0) {
                g_titleOptionsFading = 2;
                g_titleDemoTime = 0x708;
            }
            UpdateTitleTextSprite(128, g_titleSelectionId);
            break;

        case 2:
            UpdateTitleTextSprite(128, g_titleSelectionId);

            if ((justPressed & 0xEFF) || sidewinderPress) {
                play_sfx(1, 0);
                play_sfx(1, 1);
                g_titleOptionsFading = 6;
                g_fade_type_id = 1;
                g_fading_counter = 0x7F00;
                fade_update();
                g_StackPointer = 2;
                break;
            }

            if (justPressed & 0x5100) {
                if (!(justPressed & 0x1100)) {
                    if (g_titleSelectionId == 2) g_titleSelectionId = 0;
                    g_titleSelectionId++;
                } else {
                    g_titleSelectionId--;
                    if (g_titleSelectionId == 0) {
                        g_titleSelectionId = 2;
                        g_titleDemoTime = 0x708;
                        goto demo_reset;
                    }
                }
                g_titleDemoTime = 0x708;
            }
        demo_reset:
            g_titleDemoTime--;
            if (g_titleDemoTime != 0) break;
            g_titleOptionsFading = 3;
            g_fade_type_id = 2;
            g_fading_counter = 0x400;
            fade_update();
            break;

        case 3:
            if (g_fading_state < 0) {
                g_titleSelectionId = 0;
                title_exit_loop();
                break;
            }
            UpdateTitleTextSprite(0x80, g_titleSelectionId);
            if ((g_PlayerPadPressed & 0xEFF) == 0) break;
            g_titleOptionsFading = 0;
            g_fading_counter = (short)0xF000;
            break;

        case 4:
            if (g_fading_state < 0) {
                title_exit_loop();
                g_main_state_flags &= 0xC0000000;
                break;
            }
            UpdateTitleTextSprite(0x80, g_titleSelectionId);

        case 6:
            if (g_fading_state < 0) {
                g_titleOptionsFading = 7;
                g_fade_type_id = 1;
                g_fading_counter = (short)0xC000;
                fade_update();
                UpdateTitleTextSprite(0x80, g_titleSelectionId);
                break;
            }

        case 8:
            if (g_fading_state < 0) {
                g_titleOptionsFading = 9;
                g_fade_type_id = 1;
                g_fading_counter = (short)0xF800;
                fade_update();
                UpdateTitleTextSprite(0x80, g_titleSelectionId);
                break;
            }
            break;

        case 7:
            if (g_fading_state < 0) {
                g_titleOptionsFading = 8;
                g_fade_type_id = 1;
                g_fading_counter = (short)0x8000;
                fade_update();
                UpdateTitleTextSprite(0x80, g_titleSelectionId);
            }
            break;

        case 9:
            if (g_fading_state < 0) {
                g_titleOptionsFading = 4;
                g_fade_type_id = 2;
                g_fading_counter = 0x270;
                fade_update();
                UpdateTitleTextSprite(0x80, g_titleSelectionId);
            }
            break;
        }

        if (g_titleOptionsFading == 4 || g_titleOptionsFading >= 6) {
            UpdateTitleTextSprite(0x80, g_titleSelectionId);
        }
        return;
    }

    // ============================================
    // ATTRACT MODE (g_titleMode == 0)
    // "Press any button" prompt with slide-in animation and demo timeout
    // ============================================
    switch (g_titleOptionsFading) {
    case 0:
        // 0x004309b0: Start fade-in, falls through to case 1
        g_titleOptionsFading = 1;
        g_titleDemoTime = 0x80;
    case 1:
        // 0x004309b8: Sliding fade-in from top — demoTime counts down 128→0
        g_titleDemoTime -= 4;
        UpdateTitleTextSprite(-0x80 - (char)g_titleDemoTime, 0);
        if (g_titleDemoTime == 0) {
            g_titleOptionsFading = 2;
            g_titleDemoTime = 0x708;
        }
        if ((justPressed & 0xEFF) || sidewinderPress) {
            g_titleOptionsFading = 2;
            g_titleDemoTime = 0x708;
        }
        break;

    case 2:
        // 0x00430a0b: "PRESS ANY BUTTON" idle wait with demo timer
        g_titleDemoTime--;
        UpdateTitleTextSprite(0x80, 0);
        if (g_titleDemoTime == 0) {
            g_titleOptionsFading = 3;
            g_fade_type_id = 2;
            g_fading_counter = 0x400;
            fade_update();
        }
        if ((justPressed & 0xEFF) || sidewinderPress) {
            // 0x00430a70: Button pressed — switch to main menu mode
            g_titleMode = 1;
            g_titleOptionsFading = 2;
            g_titleDemoTime = 0x708;
        }
        break;

    case 3:
        // 0x00430ade: Fade-to-black animation (demo timeout)
        if (0x7B80 < g_fading_state) {
            g_fading_state = 0x7FFF;
            g_fading_counter = 0;
            g_titleSelectionId = 0;
            g_main_state_flags = (g_main_state_flags & 0x3FFFFFFF) | 0x40000000;
            title_exit_loop();
        }
        if ((justPressed & 0xEFF) || sidewinderPress) {
            g_titleOptionsFading = 2;
            g_fading_state = -1;
            g_titleDemoTime = 0x708;
        }
        UpdateTitleTextSprite(0x80, 0);
        break;

    case 4:
        // 0x00430bb0: Resume from fade-out
        UpdateTitleTextSprite(0x80, 0);
        if (0x7B80 < g_fading_state) {
            g_titleMode = 1;
            g_titleOptionsFading = 0;
            g_fading_state = 0x7FFF;
            g_fading_counter = 0;
            g_main_state_flags = (g_main_state_flags & 0x3FFFFFFF) | 0x40000000;
        }
        break;
    }
}

// characterSelectionScreen (0x00492340)
// Character selection screen shown when loading a game.
void characterSelectionScreen(void) {
    OutputDebugStringA("[CharSelection] Character selection screen\n");
}

// game_start (0x00480710)
// Entry point for starting a new game after title screen selection.
void game_start(void) {
    OutputDebugStringA("[GAME] Game start\n");
}

// save_load_game_state (0x00493310)
// Save/load game state menu.
void save_load_game_state(int a, int b, int c, int d, int e) { /* stub */ }

