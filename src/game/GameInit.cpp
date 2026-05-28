// GameInit.cpp - Game initialization and startup
// All functions decompiled from Ghidra with original addresses
#include "../Globals.h"
#include "../marni/MarniSystem.h"
#include "FileLoader.h"
#include <cstdlib>

// Forward declaration for task function registered from init_and_start_game
extern void load_global_assets(void);

// ---------------------------------------------------------------------------
// Global asset loading buffer (original: DAT_00bebce8, 320x240 x 2 bytes = 153600)
// ---------------------------------------------------------------------------
static BYTE g_AssetLoadBuffer[256 * 1024];  // 256KB for texture loads
static BYTE g_ItemsImageBuffer[128 * 1024]; // 128KB for item images

// ---------------------------------------------------------------------------
// InitTaskDataEntry (0x0040abe0)
// Initializes a task data entry structure. Writes byte at offset +7 = 0x28
// and ORs the first dword with 0x05000000.
// ---------------------------------------------------------------------------
static void InitTaskDataEntry(DWORD* entry)
{
    if (entry == NULL) return;
    *((BYTE*)entry + 7) = 0x28;
    *entry = (*entry & 0x000000FF) | 0x05000000;
}

// ---------------------------------------------------------------------------
// ClearGameStateFlags (0x00429a...)
// Clears 7 dwords starting at g_main_state_flags, then clears g_menu_choice_id
// ---------------------------------------------------------------------------
static void ClearGameStateFlags(void)
{
    DWORD* p = &g_main_state_flags;
    for (int i = 0; i < 7; i++) {
        *p = 0;
        p++;
    }
    g_menu_choice_id = 0;
}

// ---------------------------------------------------------------------------
// InitSoundAndFadeState (0x00429a...)
// Initializes sound status and fade state variables
// ---------------------------------------------------------------------------
static void InitSoundAndFadeState(void)
{
    g_SndFadeType = 0;
    g_fading_state = -1;
    g_SpecialRoomLightState = (short)0xFFFF;
}

// ---------------------------------------------------------------------------
// InitInputKeyBindings (0x00497c20)
// Copies key binding configuration data from the config area to the master input state
// ---------------------------------------------------------------------------
static void InitInputKeyBindings(void)
{
    for (int i = 0; i < 32; i++) {
        g_KeyBindingVectors[i] = g_KeyBindingConfig[i] & 0xFF;
    }
    g_pMasterInputState = (MasterInputState*)g_keyBindingData;
}

// ---------------------------------------------------------------------------
// InitPlayerInputData (0x004...)
// Initializes player input configuration values
// ---------------------------------------------------------------------------
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
// init_and_start_game (0x00429920)
// Main game initialization. Sets up input, clears state, initializes task
// data entries, registers the asset loading task.
// ============================================================================
void init_and_start_game(void)
{
    InitInputKeyBindings();
    CenterScreenOrigin();

    g_demoIdleTimer1 = 10;

    ClearGameStateFlags();
    InitSoundAndFadeState();

    g_image_buffer = g_AssetLoadBuffer;
    g_ITEMS_IMAGES_BUFFER = g_ItemsImageBuffer;

    g_main_state_flags = (g_main_state_flags & 0x3FFFFFFF) | 0x40000000;

    TaskScheduler_Reset();

    InitPlayerInputData();

    DWORD* pEntry = g_TaskDataArray_ba750;
    for (int i = 0; i < 4; i++) {
        InitTaskDataEntry(pEntry);
        InitTaskDataEntry(pEntry + (0x30 / sizeof(DWORD)));
        pEntry += (0x18 / sizeof(DWORD));
    }

    Task_execute(0, (void*)load_global_assets);
}
