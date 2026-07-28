// GameInit.cpp - Game initialization and startup
// All functions decompiled from Ghidra with original addresses
#include "../Globals.h"
#include "../marni/MarniSystem.h"
#include "FileLoader.h"
#include <cstdlib>

// Forward declaration for task function registered from init_and_start_game
extern void load_global_assets(void);


// ---------------------------------------------------------------------------
// setPolyF4 (0x0040abe0)
// ---------------------------------------------------------------------------
static void setPolyF4(POLY_F4* entry)
{
    entry->code = 0x28;
    entry->tag = entry->tag & 0xffffff | 0x5000000;
}

// ---------------------------------------------------------------------------
// ClearGameStateFlags (0x004756c0)
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
// Copies key binding configuration data to the master input state's keyMap
// ---------------------------------------------------------------------------
void InitInputKeyBindings(void)
{
    for (int i = 0; i < 32; i++) {
        g_pMasterInputState.keyMap[i] = g_keyBindingData[i];
    }
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

    g_bGameActive = 2; // 0x00be41dc

    // Initialize sprite animation slot table (6 entries x 0x14 bytes)
    // Entries accessed by g_spriteAnimActive in rendering functions
    g_spriteAnimSlots[2].count = 10;                     // 0x00be9a88
    g_spriteAnimSlots[3].count = 10;                     // 0x00be9a9c
    g_spriteAnimSlots[4].count = 4;                      // 0x00be9ab0
    g_spriteAnimSlots[3].dataPtr = g_entityLightData_bad8; // 0x00be9aa0
    g_spriteAnimSlots[4].dataPtr = g_entityLightData_bad8; // 0x00be9ab4
    g_spriteAnimSlots[5].count = 4;                      // 0x00be9ac4
    g_spriteAnimSlots[5].dataPtr = g_entityLightData_bb58; // 0x00be9ac8
    g_spriteAnimSlots[0].count = 4;                      // 0x00be9a60
    g_spriteAnimSlots[0].dataPtr = g_entityLightData_bb58; // 0x00be9a64
    g_spriteAnimSlots[1].count = 4;                      // 0x00be9a74
    g_spriteAnimSlots[2].dataPtr = g_entityLightData_9ad8; // 0x00be9a8c
    g_spriteAnimSlots[1].dataPtr = g_playerAnimFunctions;  // 0x00be9a78

    ClearGameStateFlags();
    InitSoundAndFadeState();

    g_imageBufferPtr = g_imageBufferDataA;               // 0x00bebce8
    g_imageBufferPtr2 = g_imageBufferDataB;              // 0x00bee268

    g_main_state_flags = (g_main_state_flags & 0x3FFFFFFF) | 0x40000000;

    TaskScheduler_Reset();

    InitPlayerInputData();

    POLY_F4* poly = Poly_F4_ARRAY_004ba750;
    do {
        setPolyF4(poly);
        setPolyF4(poly + 2);
        poly++;
    } while (poly < Poly_F4_ARRAY_004ba750 + 2);

    Task_execute(0, (void*)load_global_assets);
}
