// Original addresses from Ghidra marked for each global variable
#include "Globals.h"
#include "game/PrintText.h"

// ============================================================================
// Task scheduler state section
// ----------------------------------------------------------------------------
// The scheduler state globals MUST live outside the memory range that
// InitializeGame()'s memclr(&g_defaultItemSlot, g_BioCardData) wipes. In the
// original binary these globals (0x00d91a68..0x00d91a90, plus g_TasksTable at
// 0x00d1fde4 and g_StackPointer at 0x007e0cc8) are placed by the linker far
// away from the wiped game-state block (0x00be41e0..0x00be9620), so memclr
// never touches them. Our decompilation's linker happened to place them inside
// the wiped range, which zeroed g_SchedulerESP mid-task and crashed TaskYield.
//
// To preserve the original layout intent without disturbing the bio card /
// game-state globals (whose relative order and sizes are load-bearing), we
// isolate the scheduler state into its own .sched section. The relative order
// within this block matches the original's 0x00d91a68 cluster ordering.
// ============================================================================
#pragma section(".sched", read, write)
__declspec(allocate(".sched"))

// --- Window system ---
// 0x00bcb2c0
HWND g_hWnd = NULL;
// 0x00bcb2c4
HINSTANCE g_hInstance = NULL;
// 0x004bcb2c
BOOL g_bIsSoftwareRendering = FALSE;
// 0x00be0e29
BOOL g_isGameCursorHiddenFlag = FALSE;
// 0x004bcb78
BOOL g_bHasFinalizedSettings = FALSE;
// 0x00bcb2c8
HANDLE g_hMutex = NULL;

// --- Display / Adapter ---
// 0x007d9148
DWORD g_dwSelectedDisplayAdapterID = 0;
// 0x007d914c
DWORD g_dwSelectedDisplayModeID = 0;
// 0x007d9150
DWORD g_dwScreenWidth = 640;
// 0x007d9154
DWORD g_dwScreenHeight = 480;
// 0x007d9158
BOOL g_bFullScreen = FALSE;
// 0x004d642c
int g_dwBitDepth = 16;
// 0x004d6430
DWORD g_dwPlayCount = 0;
// 0x004d6434
DWORD g_dwClearCount = 0;
// 0x004bcb64
DWORD g_GPU_VENDOR_ID = 0;

// --- Display mode storage ---
// 0x007d8f28
DisplayModeInfo g_DisplayModeBuffer[MAX_DISPLAY_MODES] = {};
// 0x007d8f24
int g_NumDisplayModes = 0;

// --- D3D Renderer info ---
// 0x007e0e10
D3DRendererInfo g_D3DRenderers[8] = {};
// 0x007e0e08
int g_NumD3DRenderersAvailable = 0;


// 0x008f879c (ID to choose FMVs exclusives for the selected character)
int g_FmvCharacterId = 0;

// --- Drive types ---
// 0x008f87c4
UINT g_DriveTypes[MAX_DRIVES] = {};
// Drive letter buffer
char g_DriveLetterBuffer[256] = {};

// --- Installation path ---
// 0x00d91bd0
char g_szInstallPath[MAX_PATH] = {};
char g_szCreateDir[260] = {};

// 0x004d4730
BYTE g_keyBindingData[32] = {
    VK_UP,       // [0]  → 0x26
    VK_DOWN,     // [1]  → 0x28
    VK_LEFT,     // [2]  → 0x25
    VK_RIGHT,    // [3]  → 0x27
    0,           // [4]
    'V',         // [5]  → 0x56 run/cancel
    0,           // [6]
    0,           // [7]
    0,           // [8]
    0,           // [9]
    'X',         // [10] → 0x58 aim
    'C',         // [11] → 0x43 action/confirm
    0,           // [12]
    'Z',         // [13] → 0x5a open menu
    0,           // [14]
    0,           // [15]
    0,           // [16]
    0,           // [17]
    0,           // [18]
    0,           // [19]
    0,           // [20]
    0,           // [21]
    0,           // [22]
    0,           // [23]
    0,           // [24]
    0,           // [25]
    0,           // [26]
    'A',         // [27] → 0x41 open options
    VK_CONTROL,  // [28] → 0x11 run/cancel
    VK_RETURN,   // [29] → 0x0d action/confirm
    VK_SPACE,    // [30] → 0x20 action/confirm
    VK_ESCAPE    // [31] → 0x1b run/cancel
};

BYTE g_joystickBindingData[128] = {};

BOOL g_isPaused = FALSE;                   // 0_004d46ac
BOOL g_isSideWinderConnected = FALSE;      // 0x004d46b0

BYTE g_InstallFlagData = 0;

// --- Shared memory ---
// DAT_007dfd20
HANDLE g_hFileMapping = NULL;
BYTE* g_pSharedMemory = NULL;

// --- Window rect for drawing ---
// 0xd227b0
RectDrawDesc g_window_rect = {320, 0, 0, 0, 0, 0, 240, 0};

// --- Marni System objects ---
// 0x00d227b0
void* g_pMarniDirect3D = NULL;

// Input state master 0x00ac4030
MasterInputState g_pMasterInputState = {};

// --- Main state flags ---
// 0x00be41c0
DWORD g_main_state_flags = 0;

// 0x00be41c4
DWORD g_main_state_flags2 = 0;

// 0x00bebcca
short g_fading_counter = 0;
// 0x00bf0a2f
unsigned char g_fade_type_id = 0;


// --- Game state ---
// 0x00d91bc8
int DAT_00d91bc8 = 0;

// 0x008f8790
int g_CurrentFMVID = 0;


// --- Screen pos ---
// 0x004bcac8
short g_ScreenOffsetX = 0;
// 0x04bcaca
short g_ScreenOffsetY = 0;


// 0x00aea0d0 - Per-camera background PAK load buffer (128KB)
BYTE           g_bgPakLoadBuffer[0x20000] = {};

// 0x00b0a0d0 - All-camera cached background buffer (768KB)
// Ghidra detects 786440 bytes (0xC0008) until the next global
BYTE           g_bgCacheBuffer[0xC0008] = {};

// 0x00bca0d8
signed char g_ScreenShakeOffsetX = 0;
// 0x00bca0d9
signed char g_ScreenShakeOffsetY = 0;

// 0x00be41dc
BYTE g_bGameActive = 0;

// --- Task system globals ---
// NOTE: These live in the dedicated .sched section (see top of file) so that
// InitializeGame's memclr(&g_defaultItemSlot, g_BioCardData) cannot wipe them.
// Relative order mirrors the original binary's 0x00d91a68 cluster.
// _g_StackPointer 0x007e0cc8
__declspec(allocate(".sched")) DWORD g_StackPointer = 0;
// 0x00d1fde4
__declspec(allocate(".sched")) TaskControlBlock g_TasksTable[3] = {};
// 0x00bf09ec
__declspec(allocate(".sched")) TaskControlBlock* g_CurrentTask = NULL;
// 0x00d91a68
__declspec(allocate(".sched")) TaskControlBlock* g_CurrentTaskPtr = NULL;
// 0x00d91a70
__declspec(allocate(".sched")) DWORD g_TasksESP[3] = {};
// 0x00d91a7c
__declspec(allocate(".sched")) DWORD g_CurrentTaskID = 0;
// 0x00d91a80
__declspec(allocate(".sched")) DWORD g_TasksEIP[3] = {};
// 0x00d91a8c
__declspec(allocate(".sched")) DWORD g_SchedulerESP = 0;
// 0x00d91a90
__declspec(allocate(".sched")) void* g_AsyncRpcCallback = NULL;
// 0x004ba0b8
__declspec(allocate(".sched")) DWORD g_SchedulerRunningFlag = 0;

// --- Input state ---
// 0x00bcb2e0 - last keyboard scan code or dialog message ID
DWORD g_lastScanCodeOrMsgID = 0;

// 0x00be05b4 - edge-detected raw pad word (~prev & current)
WORD g_padEdgeDetectedWord = 0;
// 0x00bf0a04 - raw held state (input to edge detection, used by menu code for held checks)
DWORD g_RawPadHeld = 0;
// 0x00bf0a08 (edge-detected: pressed this frame only)
DWORD g_PlayerPadPressed = 0;
// 0x00bf0a0c
DWORD g_button_pressed_id = 0;
// 0x00bf0a10 - edge-detected held state (output of PlayerPad_Update)
DWORD g_PlayerPadHeld = 0;
// 0x004bae30
DWORD g_PlayerPadHeldPrev = 0;

// 0x00bcb430
#pragma section(".items", read, write)
__declspec(allocate(".items"))
BYTE g_ItemsImageBuffer[86400] = {};

// 0x00be05b2 (raw pad state snapshot)
WORD g_RawPadState = 0;
// 0x004bcb3c
BOOL g_DisablePad = FALSE;

// Joystick/controller globals (used by ReadPadBoth / JoyToPSX)
// NOTE: g_PadActiveP1/P2, g_PadRawP1/P2 are aliases for g_pMasterInputState
// fields. See MarniInput.h for the struct layout:
//   g_PadActiveP1 (0x00ac422c) = g_pMasterInputState.frameFlag
//   g_PadRawP1   (0x00ac4058) = g_pMasterInputState.keyboardPrev
//   g_PadRawP2   (0x00ac4230) = g_pMasterInputState.joysticks[0].currPress
//   g_PadActiveP2 (0x00ac4404) = g_pMasterInputState.joysticks[0].enabled
// 0x00ac4018 - Combined PSX button word from both controllers
DWORD g_PadBtnWord = 0;
// 0x00ac7b58 - Number of connected controllers
int   g_NumControllers = 1;
// 0x004b1958 - JoyToPSX overflow warning flag
int   g_JoyWarnPrinted = 0;
// 0x004b1858 - PC joystick bit → PSX button remap table (2 players x 32 entries)
// Maps 32 keyboard/joystick button bits to PSX controller button codes.
// Verified against original binary at 0x004b1858.
const DWORD g_JoyRemapTbl[2][32] = {
    { // Player 1
        0x00001000, 0x00004000, 0x00008000, 0x00002000,
        0x00000020, 0x00000040, 0x00000002, 0x00000010,
        0x00000004, 0x00000001, 0x00000008, 0x00000080,
        0x00000100, 0x00000800, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000900,
        0x00000040, 0x00000080, 0x00000080, 0x00000040
    },
    { // Player 2
        0x00001000, 0x00004000, 0x00008000, 0x00002000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000080, 0x00000040, 0x00000002, 0x00000010,
        0x00000020, 0x00000001, 0x00000004, 0x00000008,
        0x00000100, 0x00000800, 0x00000000, 0x00000000, 
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000
    }
};

// Pad remap sub-tables (ROM data from 0x004bf2a0, 0x004bf2c0, 0x004bf2e0)
// Each entry maps a PS1 button bitmask to a dpad output position
// Sub-table index 0 (controller config 0)
static const WORD g_padRemapSubTable0[16] = {
    0x1000, 0x2000, 0x4000, 0x8000,
    0x1000, 0x4000, 0x0080, 0x0080,
    0x0008, 0x0040, 0x0008, 0x0004,
    0x0002, 0x0001, 0x0080, 0x0040
};
// Sub-table index 1 (controller config 1)
static const WORD g_padRemapSubTable1[16] = {
    0x1000, 0x2000, 0x4000, 0x8000,
    0x1000, 0x4000, 0x0020, 0x0020,
    0x0008, 0x0040, 0x0008, 0x0004,
    0x0002, 0x0001, 0x0080, 0x0040
};
// Sub-table index 2 (controller config 2)
static const WORD g_padRemapSubTable2[16] = {
    0x1000, 0x0008, 0x4000, 0x0004,
    0x1000, 0x4000, 0x0020, 0x0020,
    0x0002, 0x0040, 0x0008, 0x0004,
    0x0002, 0x0001, 0x0020, 0x0040
};
// Sub-table index 3 (0x00be9a3c) - runtime configurable
WORD g_padRemapSubTable3[16] = {};

// 0x004bf300 - Array of pointers to remap sub-tables
const WORD* g_padRemapTable[4] = {
    g_padRemapSubTable0,
    g_padRemapSubTable1,
    g_padRemapSubTable2,
    g_padRemapSubTable3
};

// 0x00bf0a14 - Previous dpad held state
WORD g_PlayerDpadHeldPrev = 0;
// 0x00bf0a0e - placeholder for high WORD of g_button_pressed_id (use (WORD)(g_button_pressed_id >> 16))
// 0x00bf0a12 - placeholder for high WORD of g_PlayerPadHeld (== g_RawPadState)
// 0x00be9842 - g_PlayerDpadPressed is now a macro to g_BioCard.playerDpadPressed (see Items.h)
// 0x00d21d10 - Attract demo input data (512 WORDs = 1024 bytes)
WORD g_demoPadData[512] = {};

// --- Menu / dialog flags ---
// 0x00be9825 - g_menu_choice_id is now a macro to g_BioCard.menu_choice_id (see Items.h)
// 0x004D4668
BOOL g_displayExitGameScreen_flag = FALSE;
// 0x004d466c
BOOL g_displayReturnToTitleScreen_Flag = FALSE;

// F9 key handling state (used by OnKeyDown)
// 0x004d46e0 - timeGetTime() of last F9 press for debounce
DWORD g_lastF9PressTime = 0;
// 0x004b3870 - blocks F9 processing when set
int   g_blockF9Flag = 0;
// 0x004d4654 - debug mode counter, cycles 0-3 on F1 press
int   g_F1DebugMode = 0;
// 0x004ba718 - set when F9 triggers game reset
int   g_pressF9Flag = 0;
// 0x004d4670 - triggers game state reset
int   g_resetGameFlag = 0;

// --- Sound system ---
// 0x00BF0A2D
int g_SndFadeType = 0;
// 0x00ac9908
int g_SndRampFramesLeft = 0;
int g_BgmSoundBank = 0;
int g_SfxBanks[64] = {};
int g_RoomSfxBanks[64] = {};
int g_CharacterSfxBanks[64] = {};
int g_emSndBanks[64] = {};
int g_SndBank[64] = {};
int g_SfxVolume = -1;
char g_BgmPaused = 0;
int g_SndRampDirection = 0;
int g_SndRampCurrentVolume = 0;
int g_SndRampBankIndex = 0;
int g_SndDistSteps = 0;
int g_EnemySndVolume = -1;              // 0x00ac98d0 - enemy sound volume
unsigned int g_SoundSystemFlags = 0;    // 0x004b3998 - sound system flags
char g_SoundAltPathPrefix[256] = {};    // 0x00d91bd0

// Sound callback function pointers (set by Room_SetupCollisionCallbacks)
SoundCallbackRect   g_SoundCallbackRect = NULL;    // 0x00ac9c04
SoundCallbackRect   g_SoundCallbackRect2 = NULL;   // 0x00ac9c14
SoundCallbackCircle g_SoundCallbackCircle = NULL;  // 0x00ac9c0c
SoundCallbackFlag   g_SoundCallbackFlag = NULL;    // 0x00ac9c10

void* g_SoundManager = NULL;
DirectSound* g_pDirectSound = NULL;
DWORD g_CachedWaveOutVolume = 0;
int g_WaitForMusicTimer = 0;
unsigned char g_BGM_STATE = 0xFF;
HWND g_MainWindowHandle = NULL;
int g_setVolResult = 0;
int g_CurBank = 0;
unsigned char g_snd_slot_00ac99d5 = 0;
int           g_snd_bank_00ac99d8 = 0;          // 0x00ac99d8 - secondary BGM bank 1
unsigned char g_snd_slot_00ac99dd = 0;          // 0x00ac99dd - secondary BGM slot 1
int           g_snd_bank_00ac99e0 = 0;          // 0x00ac99e0 - secondary BGM bank 2
unsigned char g_snd_slot_00ac99e5 = 0;          // 0x00ac99e5 - secondary BGM slot 2
int           g_bgmDefaultVolume = -1;           // 0x00ac98c4
unsigned short g_snd_pan_left  = 0x7F;        // 0x00ac98c8 - 3D sound left channel pan
unsigned short g_snd_pan_right = 0x7F;        // 0x00ac98cc - 3D sound right channel pan
MATRIX        MATRIX_00d22680 = {};               // 0x00d22680 - camera view matrix for 3D sound
unsigned char g_prevBgmState = 0;               // 0x00bf07f1
unsigned char g_targetBgmState = 0;             // 0x00bf07f0
unsigned char* g_RoomBgmStatePtr = NULL;              // 0x00bf0a30 - pointer to current stage's room BGM state data
void*         g_bgmDataTable = NULL;             // 0x00d1fdc4 → 0x004d0c30
void*         g_StageDataPtr = NULL;             // 0x00d1fdc8 - stage-specific data pointer

int g_SoundPanVol = 0;
int g_SndPanSet_result = 0;
char* g_wavName = NULL;
int g_sndload_bank_index = 0;
short g_CurSlot = 0;
int g_SndFadeStepTbl[64] = {};
int g_roomSfxVolume = -1;              // 0x00ac9b70
int g_charSfxVolume = -1;              // 0x00ac98fc

// --- Game init ---

// 0x00be9a60 - Sprite animation slot table (6 entries x 0x14 bytes)
// Indexed by g_spriteAnimActive in room_camera_and_lighting_update / calc_entity_lighting
SpriteAnimSlot g_spriteAnimSlots[6] = {};

// Sprite animation data buffers (pointed to by g_spriteAnimSlots entries)
BYTE g_entityLightData_9ad8[0x1000] = {};  // 0x00be9ad8 - entry[2] target
BYTE g_entityLightData_bad8[0x80] = {};    // 0x00bebad8 - entry[3]/[4] target
BYTE g_entityLightData_bb58[0x80] = {};    // 0x00bebb58 - entry[0]/[5] target

// Image buffer data (pointed to by g_imageBufferPtr / g_imageBufferPtr2)
BYTE g_imageBufferDataA[0x2580] = {};      // 0x00bebce8 - primary image buffer data
BYTE g_imageBufferDataB[0x1000] = {};      // 0x00bee268 - secondary image buffer data

// Image buffer pointers (set by init_and_start_game, swapped by display_die_screen)
void* g_imageBufferPtr = NULL;             // 0x00d213a8 - primary image buffer pointer
void* g_imageBufferPtr2 = NULL;            // 0x00d213ac - secondary image buffer pointer

// 0x004ba750
POLY_F4 Poly_F4_ARRAY_004ba750[4] = {};

// 0x004ba7b0
int init_game_flag = 0;

// --- Timing ---
// 0x007e0df4
DWORD g_dwSystemTimer1 = 0;
// 0x007e0df8
DWORD g_dwGameTimer1 = 0;
// 0x004d46c4
DWORD g_GameInitTime = 0;
// 0x00d22730
DWORD Game_timer = 0;
// 0x004d46d4
DWORD DAT_004d46d4 = 0;
// 0x004d45fc
DWORD g_LastFrameTime_ms = 0;

// Frame rate governor globals (0x004973d0)
int g_frameTimeIndex = 0;          // 0x004d45f8
int g_frameTimeBuffer[4] = {};     // 0x00ac4000
int g_frameTimeAccumulator = 0;   // 0x004d45f4
int g_frameTargetTime = 100;      // 0x004d45ec (start at 100 for first-frame present)
int g_ScreenAccessReady = 1;      // 0x004d4658 (start ready so first frame presents)
int g_ScreenAccessCountdown = 0;  // 0x004d4684 - StMask countdown (frames until re-enable)
int g_RenderAccessReady = 1;      // 0x004d4688
int g_MarniScreenReady = 0;       // FUN_00497340 - screen present enable (marni field_0x2ec)
DWORD g_MarniScreenColor = 0;     // FUN_00497360 - packed RGB debug color override

// --- MCIVideo ---
// 0x004bcb44
int g_mciVideoDeviceID = 0;
// 0x004bcb48
BOOL g_bMCIVideoEvent = FALSE;

// --- Misc flags ---

BOOL g_bWindowActive = TRUE;    // DAT_004bcb30 (start active so game loop runs)
BOOL g_bQuitFlag = FALSE;       // DAT_004bcb40
BOOL g_bUseFrameSkip = FALSE; // DAT_004bcb48
BOOL g_bFrameSkipDetected = TRUE;  // DAT_004d46dc (allow frame timing check)

// --- Print text buffer ---
// 0x00be0e20
char PRINT_TEXT_BUFFER[256] = {};

// --- Texture descriptor for text rendering ---
// 0x00be1150
RectDrawDesc g_rect = {};
// 0x00be1160
TextureDesc g_TextureDesc = {};
// 0x00be1180
int unk_00be1180 = 0;

// EKG line drawing data (primary line at 0x00be1198, secondary at 0x00be1184)
unsigned char g_EkgPrimaryLine[16] = {};               // 0x00be1198
unsigned char g_EkgSecondaryLine[24] = {};             // 0x00be1184

// Item image texture V lookup table (indexed by item ID)
unsigned char DAT_00d21ccf[256] = {};                   // 0x00d21ccf

// --- Counters ---
int g_numFramesRendered = 0;    // DAT_004d4694
int g_numFramesPresented = 0;   // DAT_004d469c

// --- Screen info ---
BOOL g_bAccessibilityAnimations = FALSE; // DAT_007d9144

// --- MCI notification ---
BOOL g_bMCINotifyEnabled = FALSE;  // 0x004bcb58
BOOL g_bMCINotifyFlag = FALSE;     // 0x004bcb5c

// --- Frame counter ---
int g_loopCounter = 0;

// Debug clear color
float g_debugClearR = 0.05f;
float g_debugClearG = 0.05f;
float g_debugClearB = 0.08f;
int   g_debugTaskFrame = 0;

// Sprite/clear color (set by setSomeColor)
float g_color_r = 0.0f;              // 0x004c336c
float g_color_g = 0.0f;              // 0x004c3370
float g_color_b = 0.0f;              // 0x004c3374
float g_spriteColorScale = 1.0f;     // 0x004af2ac (read-only multiplier constant)

// Title state globals
unsigned char g_titleLoopFlag = 0;        // 0x00d22777
unsigned char g_titleMode = 0;            // 0x00d22775
unsigned char g_titleOptionsFading = 0;   // 0x00d22776
unsigned char g_titleSelectionId = 0;     // 0x00d22774
short         g_titleDemoTime = 0;        // 0x00d22788 - demo countdown
short         g_titleTextureDepthData[8] = {}; // 0x00d22778 - texture depth array
int           g_sceneRenderParam = 0;     // 0x004d6300
DWORD          g_titlePrimType = 0;        // 0x004d6398
DWORD          g_primParam = 0;            // 0x004d63e0
DWORD          g_primFlag2 = 0;            // 0x004d63e4
int           g_displayWidth = 0;         // 0x00bf09f8
int           g_displayHeight = 0;        // 0x00bf09fc
int           g_displayMode = 0;          // 0x00bf09f4
int           g_DisplayImageWidth = 0;    // 0x004c3364
int           g_DisplayImageHeight = 0;   // 0x004c3368
int           g_titleTextureSlotId = 0;   // 0x004c331c
int           g_texturePageMode = 0;      // DAT_008ec9c0
int           g_texturePageHandle = 0;    // global handle
int           g_AsyncResult = 0;          // async operation result
int           g_ExecuteBufferHandle = 0;  // DAT_008e1d58
int           g_SpriteQueueCount = 0;     // sprite queue count
int           g_OTIndex = 0;              // OT index
DWORD         g_SpriteQueueIndex = 0;     // DAT_004c2d10
CMarniBits    g_MarniBitsWorkBuffer;      // DAT_008ed478
DWORD         g_MarniBitsOutput = 0;      // DAT_008ed470
DWORD         g_ObjectWorkBuffer[64] = {};// DAT_008ec9c8


int   g_InstallFlagDataLoaded = 0;      // DAT_004b3998
int   g_SpriteBufferFlag = 0;           // DAT_004b399c
int   g_SpriteAsyncFlag = 0;            // DAT_00d91bc8
int   g_FileOpenCount = 0;              // DAT_00d91bcc
int   g_FileRetryFlag = 0;              // DAT_004d4690
int   g_playingGameFlag = 0;            // 0x004d4674
int   g_loadSaveStateFlag = 0;          // 0x004d4678
// g_demoIdleTimer1 is now a #define alias for g_AttractModeIdleTimer
void* g_loadDataDestPointer = NULL;     // 0x00bebcdc
void* g_RdtLoadDataBackup = NULL;       // 0x00bebce0 - backup of g_loadDataDestPointer before room_set
unsigned char g_selectedFmvId = 0;          // 0x00bf07fb
void* g_fmvDataPointer = NULL;          // 0x00bf07fc
int   g_fmvPlayCount = 0;               // 0x004bae34

// Texture page table
void* g_TexturePageTable = NULL;
DWORD g_TexturePageTable_DAT[256] = {};
ID3D11ShaderResourceView* g_TexturePageSRV[256] = {};
int   g_TexturePageWidth[256] = {};
int   g_TexturePageHeight[256] = {};
int   g_TexturePageBpp[256] = {};

// Player input data
int   g_PlayerInputConfig_3c = 0;
int   g_PlayerInputConfig_3e = 0;
int   g_PlayerInputConfig_40 = 0;
int   g_PlayerInputConfig_42 = 0;
int   g_PlayerInputConfig_44 = 0;
int   g_PlayerInputConfig_46 = 0;
int   g_PlayerInputConfig_48 = 0;
int   g_PlayerInputConfig_4a = 0;
int   g_PlayerInputConfig_4c = 0;
int   g_PlayerInputConfig_4e = 0;
int   g_PlayerInputConfig_50 = 0;
int   g_PlayerInputConfig_52 = 0;
int   g_PlayerInputConfig_54 = 0;
int   g_PlayerInputConfig_56 = 0;
int   g_PlayerInputConfig_58 = 0;
int   g_PlayerInputConfig_5a = 0;

// Player animation function pointer table (0x00bebbd8, 52 entries)
// Populated by set_player_animations_functions (0x00409a00)
// Indexed by g_playerEntityPointer.animFrameId * 4
void* g_playerAnimFunctions[52] = {};     // 0x00bebbd8

// Player animation jump tables (populated from binary data)
// TODO: Extract actual table contents from binary
void* DAT_004c2ac8[32] = {};   // jump table for player_anim_set_attacked_flag dispatch (anim_86)
void* DAT_004ba360[32] = {};   // jump table for player_anim_limb_physics dispatch (anim_86)
void* DAT_004b1a90[32] = {};   // jump table for player_anim_death_alt dispatch (anim_86)
void* DAT_004c10b0[32] = {};   // jump table for player_anim_dispatch_4b1a90 dispatch (anim_87)

// Texture/room state
unsigned char g_TextureBankID = 0;       // 0x00bebcc4
unsigned char g_TextureDepthByte = 0;    // 0x00bebcc5
unsigned short g_SavedTextureBankID = 0; // 0x00bebcc6


// Model data buffers
BYTE   g_entityModelBuffer[0xCC00] = {};// 0x00bf11c0
DWORD  g_entityModelBuffer2[0xB4] = {};// 0x00bfddc0


// File path construction buffer
char   FILE_PATH[260] = {};             // global file path buffer

// Texture/model loading flags
DWORD  DAT_004d2bd8 = 0;                // 0x004d2bd8 - special model flag
DWORD  DAT_004d2bdc = 0;                // 0x004d2bdc - TMD rendering disabled flag
int    DAT_004d2be0 = -1;               // 0x004d2be0 - blend mode override value
unsigned char g_red_color   = 0x34;   // 0x004d2be4 - back color red component
unsigned char g_green_color = 0x34;   // 0x004d2be5 - back color green component
unsigned char g_blue_color  = 0x00;   // 0x004d2be6 - back color blue component
DWORD  DAT_004d2bf4 = 0;                // 0x004d2bf4 - TMD processing flag
DWORD  g_tmdAsyncData = 0;               // 0x008fc424 - TMD async processing data pointer
DWORD  DAT_004c1a2c = 0;                // 0x004c1a2c

// Weapon animation angle adjustment constants (0x004c2028–0x004c204c)
int    g_weaponAngle_Special = 0x12;     // 0x004c2028 - special weapon angle
int    g_weaponAngle_PrimX   = 0x14;     // 0x004c202c - primary X rotation
int    g_weaponAngle_PrimY   = 0x05;     // 0x004c2030 - primary Y rotation
int    g_weaponAngle_PrimZ   = 0x1E;     // 0x004c2034 - primary Z rotation
int    g_weaponAngle_Sec1X   = -20;      // 0x004c2038 - secondary set 1 X rotation
int    g_weaponAngle_Sec1Y   = 0x10;     // 0x004c203c - secondary set 1 Y rotation
int    g_weaponAngle_Sec1Z   = -50;      // 0x004c2040 - secondary set 1 Z rotation
int    g_weaponAngle_Sec2X   = 0x05;     // 0x004c2044 - secondary set 2 X rotation
int    g_weaponAngle_Sec2Y   = 0x05;     // 0x004c2048 - secondary set 2 Y rotation
int    g_weaponAngle_Sec2Z   = -48;      // 0x004c204c - secondary set 2 Z rotation
DWORD  DAT_00ae9f04 = 0;                // 0x00ae9f04
DWORD  DAT_00ae9f06 = 0;                // 0x00ae9f06
DWORD  DAT_00ae9f00 = 0;                // 0x00ae9f00
DWORD  DAT_00ae9efc = 0;                // 0x00ae9efc
BYTE   g_textureQueueData[40] = {};     // 0x00d22740

// Additional globals
DWORD  DAT_004d6444 = 0;                // 0x004d6444

// Key binding vectors
BYTE  g_KeyBindingVectors[32] = {};

// Marni video driver arrays (large BSS allocations)
DWORD g_VideoDriverArray_D0[64] = {};
DWORD g_VideoDriverArray_03c[64] = {};
DWORD g_VideoDriverArray_04c[64] = {};
DWORD g_VideoDriverArray_068[64] = {};
DWORD g_VideoDriverArray_06c[64] = {};
DWORD g_VideoDriverArray_4d0[1024] = {};
DWORD g_VideoDriverArray_4fa[256] = {};
DWORD g_VideoDriverArray_4fc[256] = {};
DWORD g_VideoDriverArray_500[256] = {};
DWORD g_VideoDriverArray_810[256] = {};
DWORD g_VideoDriverArray_814[256] = {};
DWORD g_VideoDriverArray_838[2048] = {};
DWORD g_VideoDriverArray_520[2048] = {};
short g_VideoDriverArray_FA[256] = {};
DWORD g_animSlotIndex = 0;               // 0x008f8c78

// --- Other state vars ---
int g_ScreenAccessCheck = 1;    // 0x004d2290 (start enabled so rendering happens)
int g_RenderAccessCheck = 1;    // DAT_004d468c (start enabled so rendering happens)
int g_demoTimer = 0;            // DEMO timer pattern field
short g_DemoTimerCur = 0;      // 0x00d21cee - demo idle timer current value
short g_DemoTimerMax = 0;      // 0x00d21cf0 - demo idle timer threshold
BOOL g_bFullScreenFlag_68 = FALSE;  // used for cursor hiding logic

// Object cleanup globals (Object_DeleteAll / ObjectCleanupCallback / ObjectList_Cleanup)
int    g_objectDeleteFlag = 0;                // 0x004d2bfc
int    g_objectCountArray[32] = {};           // 0x008ffc40
int    g_objectDeleteCounter = 0;             // 0x00aabd68
int*   g_objectDeletePtr = NULL;              // 0x004d2bf8
int    g_objectListCleanupFlag = 0;           // 0x004d2fb4
int    g_objectListCleanupCount = 0;          // 0x004d2fb0
DWORD  g_objectListPtrArray[512] = {};        // 0x008fc430 area
char   g_tmdObjectBuffer[250 * 0x1594] = {};  // 0x00923b50 area (array of CMarniDirect3DTMD, 0x1594 stride)
DWORD  g_tmdTextureAllocated[23] = {};         // 0x00922a40
BYTE   g_psxTextureArray[23 * 0x1b60] = {};   // 0x00a75168 - PSXTexture array
DWORD  g_textureBankRedirect[23] = {};        // 0x00aae2b0

// Async TMD object creation globals (FUN_00483cc0)
DWORD  g_asyncTmdDepth = 0;                   // 0x008fc42c
DWORD  g_asyncTmdDataPtr = 0;                 // 0x00aae330
DWORD  g_asyncTmdObjectPtr = 0;               // 0x008f88a0
DWORD  g_asyncTmdResult = 0;                  // 0x008ffc30

// Complex TMD object setup arrays (FUN_00486990)
DWORD  g_complexTmdObjectArray[256] = {};      // 0x00922b00
int    g_complexTmdObjectIds[256] = {};        // 0x00923748
BYTE   g_faceNormalBuffer[250 * 8] = {};       // 0x008fb8b0
char   g_renderStateTex[0x36c] = {};           // 0x00aad6f0 — PSXTexture + aux data
char   g_renderStateTMD[0x1594] = {};          // 0x00aac158 — specific CMarniDirect3DTMD instance

// --- Save/Load game state globals ---
int           g_healthStatus = 0;              // 0x00be6370
int           g_playerAngle = 0;               // 0x00be6368
short         g_playerBkpPosX = 0;             // 0x00be6380
short         g_playerBkpPosZ = 0;             // 0x00be6382
int           g_playerBkpHealthStat = 0;       // 0x00be6384
short         g_playerBkpAngle = 0;            // 0x00be6388
int           g_playerPosX = 0;                // 0x00be6350
int           g_playerPosZ = 0;                // 0x00be6358
int           g_savesCounter = 0;              // 0x004d467c
unsigned char* g_firstItemSlotPointer = NULL;  // 0x00be63a0
int           g_totalHeldItems = 0;            // 0x00be63a4

DWORD         g_heItemsX2Less1 = 0;            // 0x00be63a8
unsigned char g_itemSlotIndices[8] = {};        // 0x00be63b0
char          g_saveFileName[260] = {};         // 0x004d42d8


// --- Game start / game loop globals ---
// 0x00bebcc0
unsigned short g_message_flags = 0;

// Message display system
unsigned short g_messageFlagsBackup = 0;            // 0x00bebcc2
unsigned short g_PauseGameInMsgFlag = 0;            // 0x00bf0a18
unsigned char* g_MessagePtr = NULL;                 // 0x00bf0a1c
unsigned char  g_MessageStateCounter = 0;            // 0x00bf0a16
short          g_MessageScreenY = 0;                 // 0x00bf0a1a
unsigned char  g_MessageSpeedUpFlag = 0;             // 0x00bf0a17
RDT*           g_RdtPointer = NULL;                  // 0x00bebcd0
unsigned char* g_MessageCurrentPtr = NULL;           // 0x00bf0a20
unsigned char* g_MessageSavedPtr = NULL;             // 0x00bf0a24
unsigned char  g_MessageCharDelay = 0;               // 0x00bf0a29
unsigned char  g_MessageCharTimer = 0;               // 0x00bf0a2a
unsigned char  g_MessageClutBase = 0;                // 0x00bf0a2b
unsigned char  g_MessageClutCopy = 0;                // 0x00bf0a2c
int            g_MessageLineCounter = 0;             // 0x008e1c64
// 0x008f8894
int end_game_status = 0;

// ============================================================================
// Entity / Player globals (non-bio_card addresses)
// ============================================================================

// 0x00be62e4 - Main player entity structure (0x180 bytes)
PlayerEntity g_playerEntity = {};

// 0x00be6464 - Enemy entity array (30 x 0x18C bytes)
Entity g_EnemiesList[30] = {};

int g_enemy_count = 0;

// ============================================================================
// Effect system globals (billboard/sprite effect pool)
// ============================================================================

// 0x00be41e4 - Effect pool: 64 slots x 0x84 bytes each (ends at 0x00be62e4)
Effect g_effectPool[MAX_EFFECTS] = {};

// 0x00bf07ee - Free effect slot counter (initialized to 64 by InitRoomEffSprite)
unsigned char g_freeEffectSlots = 0;

// 0x00bf0a54 - Per-type sprite header pointers (populated by InitRoomEffSprite/load_shoot_direction_data)
DWORD g_effectSpriteInfo[50] = {};

// 0x00bf0b1c - Per-type animation data pointers (populated by InitRoomEffSprite/load_shoot_direction_data)
DWORD g_effectAnimData[50] = {};

// 0x00bebcd4 - Pointer to current active entity (points to g_playerEntity during gameplay)
Entity* ENTITY = NULL;

// 0x00be9a5c - Controller configuration byte
unsigned char g_controllerConfig = 0;

// 0x004c44d8 - Attract mode idle timer
DWORD         g_AttractModeIdleTimer = 0;

// 0x00d213b0 - Zeroed on new game init (InitializeGame non-continue path, +0x1E0 in entity data struct)
DWORD         g_gameSessionInitFlag = 0;

// 0x004bca88 - Room camera data (original address)
DWORD         g_RoomCameraData = 0;

// 0x00d1fdd4 - Camera data copy pointer (holds → 0x4bca88)
DWORD         g_RoomCameraDataCopy = 0;

// 0x00d1fdd0 - Dead player move matrix pointer (holds → 0x4bca68, identity matrix)
DWORD         g_deadMoveValue = 0;

// 0x00d1fdcc - Light direction matrix pointer (holds → g_lightMatrix)
DWORD         g_lightMatrixPtr = 0;

// 0x004bcaa8 - Light direction matrix (3 light source direction vectors for GTE lighting)
MATRIX        g_lightMatrix = {};

// 0x004bca68 - Identity matrix data constant (PS1 GTE: 0x1000 = 1.0 on diagonal)
MATRIX        g_identityMatrixData = {{{4096,0,0}, {0,4096,0}, {0,0,4096}}, 0, {0,0,0}};

// 0x00be11b0 - Player position scratch VECTOR for billboard effects
VECTOR        g_playerPosScratch = {};

// 0x00be11a8 - Scratch SVECTOR for velocity calculations
SVECTOR       g_svecScratch = {};

// 0x00be11c0 - Scratch MATRIX for matrix operations
MATRIX        g_matrixScratch = {};

// 0x00be0dd8 - Death animation state flag
unsigned int  g_deathAnimationFlag = 0;

// 0x00be0dfc - Temp save for ENTITY->animation_frame_id during animation extraction
unsigned int  g_animFrameIdSave = 0;

// 0x00be0de0 - Joint displacement value for animation color tinting
int           g_playerDisplacement = 0;

// 0x00be0df8 - Temp pointer for joint processing (second joint in weapon pair)
void*         g_tempVar = NULL;

// 0x004d4510 - Chris SCA collision data (16 bytes)
DWORD         g_scaChrisData[4] = {
    0x00008000,  // first short negative = terminator, rest zero
    0x0000FA06,
    0x01A605FA,
    0x00000000
};

// 0x004d4520 - SCA collision data table 2 (16 bytes, unknown character)
DWORD         g_scaData2[4] = {
    0x00008000,
    0x00000190,
    0x00000000,
    0x00000000
};

// 0x004d4530 - Jill SCA collision data (16 bytes)
DWORD         g_scaJillData[4] = {
    0x00008000,
    0x0000FABA,
    0x01740546,
    0x00000000
};

// 0x004d4540 - SCA data pointer table (index = (characterId & 1) * 2)
// [0]: Chris, [1]: table2, [2]: Jill, [3]: NULL sentinel
DWORD         g_scaDataTable[4] = {
    (DWORD)g_scaChrisData,
    (DWORD)g_scaData2,
    (DWORD)g_scaJillData,
    0x00000000
};

// 0x00d211d0 - Entity/game state data block (enemy SCA pool, image buffer ptrs, SCD ptrs)
BYTE          g_entityDataBlock[0x200] = {};

// 0x00d213d0 - Room sprite entries table (populated from RDT data by FUN_004757c0)
// 128 entries max, each 0x24 bytes. Count stored in g_RdtPointer->sprites_count.
RoomSprEntry  g_RoomSprEntries[128] = {};

// 0x00d21354 - Enemy SCA hit data pool allocator (current position, starts at g_entityDataBlock+6)
DWORD         g_scaPoolPtr = 0;

// 0x00d21358 - Enemy SCA hit data pool base (reset value = g_entityDataBlock+6)
DWORD         g_scaPoolBase = 0;

// 0x00be995c - Per-room BGM state table (7 stages x 32 rooms, 224 bytes)
// Each byte is a BGM state descriptor: bits 0-2 = track index, bits 3-5 = secondary slot enables,
// bits 6-7 = transition mode (0=normal, 1=always reload, 2=force restart), 0xFF = no BGM.
// Modified at runtime by SCD opcode cmd_0x37 and preserved in save files.
// 0x00be995c to 0x00be9a3c - g_roomBgmState is a macro to g_BioCard.roomBgmState (see Items.h)

// 0x00be98d0 - Room flags bitfield (set by Flg_on via room_set_visited_flag)
// g_RoomFlags is now a macro to g_BioCard.roomFlags (see Items.h)

// 0x004d31e0 - Stage room-flag base offset table (indexed by stageId % 5)
const unsigned char g_StageRoomFlagOffset[5] = { 0, 32, 63, 82, 100 };

// 0x00be41c8 - SCD flag bank 4: system flags used by room scripts (case 4 in cmd_bit_test/cmd_bit_op)
// DWORD array, index 0 = flags 0-31, index 1 = flags 32-63. Cleared on room change.
DWORD         g_SysFlags[2] = {};
// DAT_00be9830 is now a macro to g_BioCard.dat_0x210 (see Items.h)

// 0x00d91aa0 - Room item event table (24 entries x 12 bytes = 288 bytes)
// Used by item_set, door_set, item_model_set commands and player position updates
unsigned char g_RoomItemEventTable[288] = {};
// 0x00d91bc0 - Pointer to current room item event entry (reset to g_RoomItemEventTable)
void*         g_RoomItemEventHead = NULL;

// 0x00d22790 - Lab slides state block (reset by lab_slides_reset)
unsigned char g_labSlidesFuncIndex = 0;
unsigned char g_labSlidesAnimState = 0;     // 0x00d22791
int           g_labSlidesScrollX = 0;       // 0x00d22794
unsigned int  g_labSlidesScrollY = 0;       // 0x00d22798
unsigned char g_labSlidesSlideIndex = 0;    // 0x00d227a0
unsigned char g_labSlidesLoopDone = 0;      // 0x00d227a1
unsigned char g_labSlidesMsgId = 0;         // 0x00d227a2
unsigned char g_labSlidesCountdown = 0;     // 0x00d227a3
// 0x007d9120 - Lab slides state (reset by lab_slides_reset)
int           g_labSlidesState = 0;

// 0x00d226a4 - Room event index (SCD event pointer for current room entity)
void*         g_room_event_index = NULL;

// 0x00be9a3c - Item slot index table (first byte AFTER bio_card range)
unsigned char g_ItemSlotsIndexes[16] = {};

// 0x00d22734 - Bitmask of held items (1 << count) - 1
DWORD         g_ItemSlotsBitmask = 0;

// 0x00be41e0 - Default/reset item slot (byte)
unsigned char g_defaultItemSlot = 0;

// 0x00be41e1 - Zeroed on game init (adjacent byte to g_defaultItemSlot)
unsigned char DAT_00be41e1 = 0;

// 0x00be9614 - Death/timeout state machine byte (game_loop switch)
unsigned char DAT_00be9614 = 0;

// 0x004bd81d - Item image lookup table (4 bytes per item, byte 0 = image type index)
// Indexed by itemId * 4. Each entry is 4 bytes. First byte is the image type for LoadItemImage.
// 0x004bd81d - Item image lookup table (459 bytes from Ghidra)
const unsigned char g_ItemImageLookupTable[459] = {
    0x00,0x00,0x00,0x00,
    0x01,0x80,0x80,0x0F,
    0x02,0x00,0x80,0x07,
    0x03,0x01,0x80,0x06,
    0x04,0x02,0x80,0x06,
    0x04,0x03,0x80,0xF0,
    0x05,0x04,0x80,0x06,
    0x06,0x05,0x80,0x06,
    0x06,0x06,0x80,0x06,
    0x06,0x07,0x80,0x04,
    0x07,0x80,0x80,0x0F,
    0x08,0x08,0x80,0x07,
    0x09,0x09,0x80,0x06,
    0x0A,0x0A,0x80,0x06,
    0x0B,0x0B,0x80,0xF0,
    0x0C,0x0C,0x80,0x06,
    0x0D,0x0D,0x80,0x06,
    0x0E,0x0E,0x80,0x06,
    0x0F,0x0F,0x80,0x01,
    0x10,0x10,0x80,0x01,
    0x11,0x11,0x80,0x01,
    0x12,0x12,0x80,0x01,
    0x13,0x13,0x80,0x01,
    0x14,0x14,0x80,0x01,
    0x15,0x15,0x80,0x01,
    0x16,0x16,0x80,0x01,
    0x17,0x17,0x80,0x01,
    0x18,0x18,0x80,0x01,
    0x19,0x80,0x80,0x00,
    0x1A,0x80,0x00,0x00,
    0x1B,0x80,0x01,0x01,
    0x1C,0x80,0x80,0x01,
    0x1D,0x80,0x80,0x01,
    0x1E,0x80,0x80,0x01,
    0x1F,0x80,0x80,0x01,
    0x20,0x80,0x80,0x01,
    0x21,0x80,0x80,0x01,
    0x22,0x80,0x80,0x01,
    0x23,0x80,0x02,0x01,
    0x24,0x80,0x80,0x01,
    0x25,0x80,0x80,0x01,
    0x26,0x80,0x80,0x01,
    0x27,0x80,0x80,0x01,
    0x28,0x80,0x80,0x01,
    0x29,0x80,0x80,0x01,
    0x2A,0x80,0x80,0x01,
    0x2B,0x80,0x80,0x03,
    0x2C,0x19,0x80,0x00,
    0x2D,0x80,0x80,0x00,
    0x2E,0x80,0x80,0x02,
    0x2F,0x80,0x80,0x00,
    0x30,0x80,0x03,0x00,
    0x31,0x80,0x04,0x00,
    0x32,0x80,0x05,0x00,
    0x33,0x80,0x06,0x00,
    0x34,0x80,0x07,0x00,
    0x35,0x80,0x08,0x00,
    0x36,0x80,0x09,0x00,
    0x37,0x80,0x0A,0x00,
    0x38,0x80,0x80,0x00,
    0x39,0x80,0x0B,0x01,
    0x3A,0x80,0x0C,0x01,
    0x3B,0x80,0x0D,0x01,
    0x3C,0x80,0x0E,0x01,
    0x3D,0x80,0x0F,0x01,
    0x3E,0x80,0x80,0x01,
    0x3F,0x80,0x80,0x01,
    0x40,0x1A,0x80,0x01,
    0x41,0x1B,0x80,0x01,
    0x42,0x1C,0x80,0x01,
    0x43,0x1D,0x80,0x01,
    0x44,0x1E,0x80,0x01,
    0x45,0x1F,0x80,0x01,
    0x46,0x20,0x80,0x01,
    0x47,0x21,0x80,0x01,
    0x48,0x22,0x80,0x00,
    0x49,0x80,0x80,0x00,
    0x4A,0x80,0x80,0x00,
    0x00,0x00,0x00, // padding after item entries (75 items * 4 = 300, + these = 303)
    0x0B,0x13,0x14,0x1B,0x33,0x3D,0x3E,0x41,0x4C,0x4C,
    0x00,0x00,0x00,0x00,0x00,0x00,
    0x03,0x10,0x00,0x01,0x20,0x03,0x02,0x21,0x23,0x03,0x22,0x00,0x00,0x00,0x00,0x00,
    0xEB,0xEC,0xEF,0xE1,0xE2,0xE3,0xE4,0xE9,0xEA,0xE5,0xE6,0xE8,0xE7,0xED,0xEE,0xEE,
    0x00,0x00,0x00,0x00,
    0x2C,0x01,0x00,0x0C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x30,0x02,0x00,0x00,0x30,0x02,0x00,0x08,0x00,0x00,0x00,0x00,
    0x30,0x02,0x00,0x00,0x30,0x02,0x00,0x00,0x00,0x00,0x00,0x00,
    0x90,0x01,0x00,0x04,0x00,0x00,0x00,0x00,
    0x10,0x10,0x00,0x11,0x11,0x11,0x11,0x00,0x00,0x11,0x11,0x21,0x00,0x13,0x13,0x13,
    0x00,0x80,0x44,0xFD,0x00,0x00,0x00,0x00,0x00,0x00,0xF4,0x01,0x00,0x00,0x00,0x00,
    0x00,0x80,0x0C,0xFE,0x00,0x00,0x00,0x00,0x00,0x00,0xF4,0x01,0x00,0x00,0x00,0x00
};

// ============================================================================
// Bio Card data zone globals (0x00be9620 - 0x00be9A3B, 1052 bytes)
// These globals overlay the bio_card.dat buffer. The memcpy in InitializeGame
// copies 1052 bytes to g_BioCard, populating all fields at once.
// ORDER AND POSITION ARE CRITICAL - they must match the bio_card.dat layout.
// All individual bio_card globals are now #define macros to g_BioCard fields.
// ============================================================================
BioCardLayout g_BioCard = {};  // 0x00be9620

// ============================================================================
// Global Messages Table (0x004bfc58)
// Decoded from original ResidentEvil.exe using tools/decode_re1.py
// These messages are displayed by set_message_display() when msg_id bit 6 is set.
// Index = msg_id & 0x3F. The STR() macro encodes ASCII to RE1 font bytes.
//
// Control characters embedded via STR_* macros:
//   STR_START (0x02) = line break/newline
//   STR_L2    (0x03) = page break / section separator
//   STR_R2    (0x04) = line spacing / formatting marker
//   STR_L1    (0x05) = dynamic item name placeholder
//   STR_CIR   (0x08) = Circle button glyph
//   STR_SQR   (0x0A) = Square button glyph
// ============================================================================

// [0] 0x004BF3D8 - Item pickup prompt
static constexpr auto s_gm00 = STR("Will you take\\nthe \\i");
// [1] 0x004BF3F7 - Item obtained
static constexpr auto s_gm01 = STR("You got the \\i");
// [2] 0x004BF40C - Inventory full
static constexpr auto s_gm02 = STR("You can't carry any more\\nitems.");
// [3] 0x004BF42D - Item used
static constexpr auto s_gm03 = STR("You have used\\nthe \\i");
// [4] 0x004BF448 - Item discard prompt
static constexpr auto s_gm04 = STR("Will you put down the\\n\\i");
// [5] 0x004BF46B - Item use prompt
static constexpr auto s_gm05 = STR("Will you use\\nthe \\i");
// [6] 0x004BF489 - Dynamic item name only
static constexpr auto s_gm06 = STR("\\i");
// [7] 0x004BF4A3 - Key discard confirmation
static constexpr auto s_gm07 = STR("This key is useless now.\\nDiscard?\\c\\n\\q\\n");
// [8] 0x004BF4CA - Door locked, sword carving
static constexpr auto s_gm08 = STR("\\nIt's locked.\\p\\nA carving of a sword.");
// [9] 0x004BF4F1 - Door locked, armor carving
static constexpr auto s_gm09 = STR("\\nIt's locked.\\p\\nA carving of armor.");
// [10] 0x004BF516 - Door locked, shield carving
static constexpr auto s_gm10 = STR("\\nIt's locked.\\p\\nA carving of a shield.");
// [11] 0x004BF53E - Door locked, helmet carving
static constexpr auto s_gm11 = STR("\\nIt's locked.\\p\\nA carving of a helmet.");
// [12] 0x004BF566 - Door tightly locked with plate
static constexpr auto s_gm12 = STR("The door is tightly\\nlocked.\\pThere's a plate on right\\nhand side.");
// [13] 0x004BF5A8 - Door locked, says Closet
static constexpr auto s_gm13 = STR("\\nIt's locked.\\p\\nThe door says \"Closet\".");
// [14] 0x004BF5D1 - Door locked, plate says 002
static constexpr auto s_gm14 = STR("\\nIt's locked.\\p\\nThe plate says 002.");
// [15] 0x004BF5F6 - Door locked, plate says 003
static constexpr auto s_gm15 = STR("\\nIt's locked.\\p\\nThe plate says 003.");
// [16] 0x004BF61B - Door locked, says Control Room
static constexpr auto s_gm16 = STR("\\nIt's locked.\\pThe door says\\n\"Control Room\".");
// [17] 0x004BF649 - Power Room door locked
static constexpr auto s_gm17 = STR("\\n\"Power Room\"\\pThe door is tightly\\nlocked.");
// [18] 0x004BF675 - Generic locked
static constexpr auto s_gm18 = STR("\\nIt's locked.");
// [19] 0x004BF684 - Locked from inside
static constexpr auto s_gm19 = STR("\\nIt's locked from inside.");
// [20] 0x004BF69F - Unlocked
static constexpr auto s_gm20 = STR("\\nYou unlocked it.");
// [21] 0x004BF6B2 - Locked, use lockpick
static constexpr auto s_gm21 = STR("\\nIt's locked.\\pUse the lockpick to open\\nthe door.");
// [22] 0x004BF6E5 - Hurry message
static constexpr auto s_gm22 = STR("\\nI've got to hurry!");
// [23] 0x004BF6FA - No time to check
static constexpr auto s_gm23 = STR("There is no time to check\\nit.");
// [24] 0x004BF719 - Desk locked
static constexpr auto s_gm24 = STR("\\nThe desk is locked.");
// [25] 0x004BF72F - Desk locked, use item prompt
static constexpr auto s_gm25 = STR("\\nThe desk is locked.\\pWill you use\\nthe \\i");
// [26] 0x004BF763 - Opening narration: Chris
static constexpr auto s_gm26 = STR("\\n\\s           Chris...\\s4\\pT\\n \\s          Chris...\\s\\n");
// [27] 0x004BF795 - Opening narration: escaped into mansion
static constexpr auto s_gm27 = STR("\\s\\nThey have escaped\\ninto the mansion\\pTwhere they thought\\nit was safe.\\pTYet...");
// [28] 0x004BF7E4 - Opening narration: survival horror
static constexpr auto s_gm28 = STR("\\s\\nYou have once\\n\\s\\nagain entered\\pT\\s\\nthe world of\\n\\s\\nsurvival horror.\\pT\\s\\nGood luck!");
// [29] 0x004BF836 - Opening narration: be smart
static constexpr auto s_gm29 = STR("\\s\\nBe smart!\\n\\s\\nFighting foes\\pT\\s\\nisn't the only\\n\\s\\nway to survive\\pT\\s\\nthis horror.");
// [30] 0x004BF886 - Typewriter, no ink ribbon
static constexpr auto s_gm30 = STR("\\nIt's an old typewriter.\\p If I had an \\i");
// [31] 0x004BF8D9 - Typewriter, use ink ribbon prompt
static constexpr auto s_gm31 = STR("You can save your progress\\nwith this.\\pWill you use\\nthe \\i");
// [32] 0x004BF924 - Typewriter, save confirmation
static constexpr auto s_gm32 = STR("You can save your progress\\nwith this.\\pWill you save your progress?\\c ");
// [33] 0x004BF4DA - Carving of a sword (examined)
static constexpr auto s_gm33 = STR("A carving of a sword.");
// [34] 0x004BF501 - Carving of armor (examined)
static constexpr auto s_gm34 = STR("A carving of armor.");
// [35] 0x004BF526 - Carving of a shield (examined)
static constexpr auto s_gm35 = STR("A carving of a shield.");
// [36] 0x004BF54E - Carving of a helmet (examined)
static constexpr auto s_gm36 = STR("A carving of a helmet.");
// [37] 0x004BF96B - Number 002 engraved
static constexpr auto s_gm37 = STR("The number 002 is engraved.");
// [38] 0x004BF988 - Number 003 engraved
static constexpr auto s_gm38 = STR("The number 003 is engraved.");
// [39] 0x004BF9A5 - A desk key
static constexpr auto s_gm39 = STR("A desk key.");
// [40] 0x004BF9B2 - Strange mark carved
static constexpr auto s_gm40 = STR("A strange mark is carved\\nhere.");
// [41] 0x004BF9D2 - Specially coated, looks important
static constexpr auto s_gm41 = STR("It's specially coated and\\nlooks important...");
// [42] 0x004BFA00 - Must be a closet somewhere
static constexpr auto s_gm42 = STR("There must be a closet\\nsomewhere.");
// [43] 0x004BFA23 - Square-shaped end (Crank Square)
static constexpr auto s_gm43 = STR("Its end is square-shaped.");
// [44] 0x004BFA3E - Hex-shaped end (Crank Hex)
static constexpr auto s_gm44 = STR("Its end is hex.-shaped.");
// [45] 0x004BFA57 - All pages blank
static constexpr auto s_gm45 = STR("All pages are blank.\\nWhat's it for...?");
// [46] 0x004BFA7F - Medal in the book
static constexpr auto s_gm46 = STR("There was a medal in the\\nbook.");
// [47] 0x004BFA9F - Chemical to kill weeds
static constexpr auto s_gm47 = STR("A chemical to kill the\\nweeds.");
// [48] 0x004BFABE - Dynamic item name (alternate)
static constexpr auto s_gm48 = STR("\\i");
// [49] 0x004BFACE - Don't need this any more
static constexpr auto s_gm49 = STR("I don't need this any\\nmore.");
// [50] 0x004BFAEB - Too dangerous to mix here
static constexpr auto s_gm50 = STR("Too dangerous to mix here.");
// [51] 0x004BFB07 - Poison gas enveloped
static constexpr auto s_gm51 = STR("The poison gas enveloped.");
// [52] 0x004BFB22 - Mixing failed
static constexpr auto s_gm52 = STR("The mixing seems to have\\nfailed.  It disappeared.");
// [53] 0x004BFB55 - Mix herbs confirmation
static constexpr auto s_gm53 = STR("Will you mix the herbs?\\c ");
// [54] 0x004BFB70 - Mixing doesn't work
static constexpr auto s_gm54 = STR("Mixing these does not seem\\nto work.");
// [55] 0x004BFB95 - Can't use alone
static constexpr auto s_gm55 = STR("You can't use this alone.");
// [56] 0x004BFBE2 - Can't use it here
static constexpr auto s_gm56 = STR("You can't use it here.");
// [57] 0x004BFBB0 - Chemical can be mixed
static constexpr auto s_gm57 = STR("This chemical can be mixed\\nwith other chemicals.");
// [58] 0x004BFBE2 - Can't use it here (dup of 56)
static constexpr auto s_gm58 = STR("You can't use it here.");
// [59] 0x004BFBE2 - Can't use it here (dup of 56)
static constexpr auto s_gm59 = STR("You can't use it here.");
// [60] 0x004BFBE2 - Can't use it here (dup of 56)
static constexpr auto s_gm60 = STR("You can't use it here.");
// [61] 0x004BFBE2 - Can't use it here (dup of 56)
static constexpr auto s_gm61 = STR("You can't use it here.");
// [62] 0x004BFC27 - Don't need to use at the moment
static constexpr auto s_gm62 = STR("You don't need to use this\\nitem at the moment.");
// [63] - NULL (pointer 0x73755C2E is out of bounds)

unsigned char* global_messages[64] = {
    (unsigned char*)s_gm00.bytes,  // [0]  Will you take the [L1]
    (unsigned char*)s_gm01.bytes,  // [1]  You got the [L1]
    (unsigned char*)s_gm02.bytes,  // [2]  You can't carry any more items.
    (unsigned char*)s_gm03.bytes,  // [3]  You have used the [L1]
    (unsigned char*)s_gm04.bytes,  // [4]  Will you put down the [L1]
    (unsigned char*)s_gm05.bytes,  // [5]  Will you use the [L1]
    (unsigned char*)s_gm06.bytes,  // [6]  [L1] (item name)
    (unsigned char*)s_gm07.bytes,  // [7]  Key discard confirmation
    (unsigned char*)s_gm08.bytes,  // [8]  Locked - sword carving
    (unsigned char*)s_gm09.bytes,  // [9]  Locked - armor carving
    (unsigned char*)s_gm10.bytes,  // [10] Locked - shield carving
    (unsigned char*)s_gm11.bytes,  // [11] Locked - helmet carving
    (unsigned char*)s_gm12.bytes,  // [12] Door tightly locked with plate
    (unsigned char*)s_gm13.bytes,  // [13] Locked - Closet
    (unsigned char*)s_gm14.bytes,  // [14] Locked - plate 002
    (unsigned char*)s_gm15.bytes,  // [15] Locked - plate 003
    (unsigned char*)s_gm16.bytes,  // [16] Locked - Control Room
    (unsigned char*)s_gm17.bytes,  // [17] Power Room locked
    (unsigned char*)s_gm18.bytes,  // [18] It's locked.
    (unsigned char*)s_gm19.bytes,  // [19] Locked from inside
    (unsigned char*)s_gm20.bytes,  // [20] You unlocked it.
    (unsigned char*)s_gm21.bytes,  // [21] Locked - use lockpick
    (unsigned char*)s_gm22.bytes,  // [22] I've got to hurry!
    (unsigned char*)s_gm23.bytes,  // [23] No time to check it.
    (unsigned char*)s_gm24.bytes,  // [24] Desk is locked.
    (unsigned char*)s_gm25.bytes,  // [25] Desk locked - use item?
    (unsigned char*)s_gm26.bytes,  // [26] Opening narration: Chris
    (unsigned char*)s_gm27.bytes,  // [27] Opening: escaped into mansion
    (unsigned char*)s_gm28.bytes,  // [28] Opening: survival horror
    (unsigned char*)s_gm29.bytes,  // [29] Opening: be smart
    (unsigned char*)s_gm30.bytes,  // [30] Typewriter - no ink ribbon
    (unsigned char*)s_gm31.bytes,  // [31] Typewriter - use ink ribbon?
    (unsigned char*)s_gm32.bytes,  // [32] Typewriter - save confirmation
    (unsigned char*)s_gm33.bytes,  // [33] A carving of a sword.
    (unsigned char*)s_gm34.bytes,  // [34] A carving of armor.
    (unsigned char*)s_gm35.bytes,  // [35] A carving of a shield.
    (unsigned char*)s_gm36.bytes,  // [36] A carving of a helmet.
    (unsigned char*)s_gm37.bytes,  // [37] Number 002 engraved
    (unsigned char*)s_gm38.bytes,  // [38] Number 003 engraved
    (unsigned char*)s_gm39.bytes,  // [39] A desk key.
    (unsigned char*)s_gm40.bytes,  // [40] Strange mark carved here.
    (unsigned char*)s_gm41.bytes,  // [41] Specially coated, looks important.
    (unsigned char*)s_gm42.bytes,  // [42] Must be a closet somewhere.
    (unsigned char*)s_gm43.bytes,  // [43] Square-shaped end.
    (unsigned char*)s_gm44.bytes,  // [44] Hex-shaped end.
    (unsigned char*)s_gm45.bytes,  // [45] All pages blank.
    (unsigned char*)s_gm46.bytes,  // [46] Medal in the book.
    (unsigned char*)s_gm47.bytes,  // [47] Chemical to kill weeds.
    (unsigned char*)s_gm48.bytes,  // [48] [L1] (item name)
    (unsigned char*)s_gm49.bytes,  // [49] Don't need this any more.
    (unsigned char*)s_gm50.bytes,  // [50] Too dangerous to mix here.
    (unsigned char*)s_gm51.bytes,  // [51] Poison gas enveloped.
    (unsigned char*)s_gm52.bytes,  // [52] Mixing failed.
    (unsigned char*)s_gm53.bytes,  // [53] Mix herbs?
    (unsigned char*)s_gm54.bytes,  // [54] Mixing doesn't work.
    (unsigned char*)s_gm55.bytes,  // [55] Can't use this alone.
    (unsigned char*)s_gm56.bytes,  // [56] Can't use it here.
    (unsigned char*)s_gm57.bytes,  // [57] Chemical can be mixed.
    (unsigned char*)s_gm58.bytes,  // [58] Can't use it here.
    (unsigned char*)s_gm59.bytes,  // [59] Can't use it here.
    (unsigned char*)s_gm60.bytes,  // [60] Can't use it here.
    (unsigned char*)s_gm61.bytes,  // [61] Can't use it here.
    (unsigned char*)s_gm62.bytes,  // [62] Don't need to use at the moment.
    NULL,                            // [63] (out of bounds pointer)
};

// ============================================================================
// External data constants at fixed addresses in original binary
// These are placeholder definitions for ported code.
// ============================================================================
// 0x00606060 — joint animation constant data (referenced by JointApplyColorTint)
DWORD DAT_00606060 = 0;

// 0x00ffff50 — velocity decay reference data (referenced by BillboardSetColor)
DWORD DAT_00ffff50 = 0;

// Effect sprite texture management state (FUN_0047bc80 / InitRoomEffSprite)
unsigned char  DAT_00bf0a38 = 0;               // 0x00bf0a38
short          DAT_00bf0a3c = 0;               // 0x00bf0a3c
unsigned short DAT_00bf0a3e = 0;               // 0x00bf0a3e
short          DAT_00bf0a40 = 0;               // 0x00bf0a40
unsigned short DAT_00bf0a42 = 0;               // 0x00bf0a42
unsigned char  g_abEffSpriteIndexTable[16] = {};          // 0x00bf0a44

// Effect sprite per-slot image data pointers
int            DAT_00ac9cd0[8] = {};           // 0x00ac9cd0

// Entity joint animation copy base pointer
int            DAT_00be0e00 = 0;               // 0x00be0e00

// Effect sprite stage/room cache
unsigned int   STAGE_ID_00ac9cf0 = 0;          // 0x00ac9cf0
unsigned int   ROOM_ID_00ac9cf4 = 0;           // 0x00ac9cf4


// 0x00c0b9c0
BYTE    g_animationBuffer[37888] = {};

// 0x00c133c0
DWORD   g_animObjectBuffer[0x680] = {};

// Shoot direction ESP data buffer (loaded from core00.esp)
// 0x00c14dc0 - ESP effect data
BYTE   g_shootDirEspBuffer[73728] = {};

// 0x00c26dc0 - General purpose data buffer (832728 bytes)
BYTE    g_DataBuffer[832728];

// Display image buffer for TIM slide loading (at 0x00cf22ac in original)
BYTE    g_TimImageBuffer[187160+20] = {};

// ============================================================================
// Background loading globals
// ============================================================================

// 0x004d46b4 - Background loading mode flag
// 0 = per-camera: load PAK, unpack, display each camera individually
// non-zero = cache: load all camera PAKs into cache buffer with offset table
int            g_bgCacheMode = 1;             // 0x004d46b4 — original value is 1 (cache mode)

// 0x004c2060 - Hex character lookup table for path construction
char           g_hexCharTable[17] = "0123456789abcdef";

// 0x004c2078 - Path template for room background PAK files (mutated at runtime)
// Format: ".\usa\stageS\rcSRRC.pak"
// Positions 0x0B, 0x0F = stage digit, 0x10 = room high nibble, 0x11 = room low nibble
// Position 0x12 = camera digit (cache mode only)
char           g_bgPathTemplate[28] = {
    '.','\\','u','s','a','\\','s','t','a','g','e','S','\\','r','c','S','R','R','C','.','p','a','k','\0',
    0, 0, 0, 0
};

// 0x004c2090 - Camera hex char (stored after path template)
char           DAT_004c2090 = 0;

// ============================================================================
// Critical small globals - declared BEFORE large buffers to protect them from
// buffer overflows. In the original binary g_ItemSlotsPointer (0x00d22768) is
// ~2MB after g_bgCacheBuffer (0x00b0a0d0), so forward overflow never reaches it.
// In the recompiled binary the linker may place them adjacent, so we force them
// to lower addresses by declaring them first.
// ============================================================================

// 0x008f87b0 - Backup of player health during character switch
short          HEALTH_BKP = 0;

// 0x008f87b4 - Backup of health status flags during character switch
unsigned short HEALTH_STATUS_BKP = 0;

// 0x00d22768 - Pointer to active character's item slots (g_ItemsSlots or g_RebeccaItemSlots)
void*          g_ItemSlotsPointer = NULL;

// 0x00aea08c/0x00aea090 - Per-camera offset into g_bgCacheBuffer
int            g_bgCameraOffsets[16] = {};

// 0x00ae9e80 - Per-camera mask offset into g_bgMaskDataBuffer
int            g_bgMaskOffsets[16] = {};

// 0x00ac9e80 - Mask data buffer (128KB)
BYTE           g_bgMaskDataBuffer[0x20000] = {};

// 0x004c3bc8 - Path template for mask PAK files (mutated at runtime)
// Format: "./usa/objspr/osp0SRRC.pak"
// Position 0x11 = stageId+'0', 0x12/0x13 = roomId decimal digits, 0x14 = camera index+'0'
char           g_maskPathTemplate[28] = {
    '.','/','u','s','a','/','o','b','j','s','p','r','/','o','s','p','0','S','R','R','C','.','p','a','k','\0',
    0, 0
};

// ============================================================================
// LZW decompression state (unpack_pakfile_ at 0x00425ab0)
// ============================================================================

// 0x00d2b0a4 - Current byte offset into input data
unsigned int   g_pakDecompInputPos = 0;

// 0x00d91a64 - Current bit mask (starts at 0x80, shifts right)
unsigned int   g_pakDecompBitMask = 0x80;

// 0x00d227c4 - Current byte buffer from input stream
unsigned int   g_pakDecompCurByte = 0;

// 0x00d2b0a8 - Current code size in bits (starts at 9)
unsigned int   g_pakDecompCodeSize = 9;

// 0x00d227c8 - Next free dictionary code (starts at 0x103)
unsigned int   g_pakDecompNextCode = 0x103;

// 0x00d2b0a0 - Maximum code value for current code size
unsigned int   g_pakDecompMaxCode = 0x1ff;

// 0x00d2b0b4 - LZW dictionary prefix entries (8192 entries, 4 bytes each)
// In original: 12 bytes per dict entry, prefix at offset +4
int            g_pakDictPrefix[8192] = {};

// 0x00d2b0b8 - LZW dictionary character entries (8192 entries, 4 bytes each)
// In original: 12 bytes per dict entry, char at offset +8
char           g_pakDictChar[8192] = {};

// 0x00d227d0 - String output buffer for building decoded strings
char           g_pakStringBuf[512] = {};

// ============================================================================
// Room data globals (populated by room_set from RDT file data)
// ============================================================================

// 0x00be987c - SCD flag bank 3: room event flags (case 3 in cmd_bit_test/cmd_bit_op)
// Used by SCD scripts to track triggered events, doors, cutscenes, enemy deaths.
// g_RoomEventFlags is now a macro to g_BioCard.roomEventFlags (see Items.h)

// 0x00d226b0 - Sound bank cover pointer table (populated from RDT vab_sound_file)
void*          g_itemboxes_covers_table[8] = {};

// 0x00d21360 - Desk data pointer table (populated from RDT unknown_03 data)
void*          g_desks_pointers_table[8] = {};

// 0x00ae9ef4 - Count of object models loaded by cmd_omodel_set in current room
int            g_omodelCount = 0;

// 0x00bebcc9 - Last enemy model ID loaded during room_set enemy loop (for model reuse caching)
unsigned char  g_LastEnemyModelId = 0xff;

// 0x00ae9eec - Count of item/obstacle models loaded by cmd_item_model_set in current room
int            g_ItemModelCount = 0;

// 0x00d213bc - SCD opcodes pointer for room initialization script (set by LoadRoomRdt)
void*          g_RoomInitScd = NULL;

// 0x00bebccc - Current RDT data type pointer (set to cam_switch_zones by room_set)
void*          g_CurrentRdtDataTypePtr = NULL;

// 0x00d213c0 - Data pointer saved for stage 2 room 3 (into g_loadDataDestPointer)
void*          DAT_00d213c0 = NULL;

// 0x00bf084c - SCD event execution context table (8 entries x 0x34 bytes)
// Each entry tracks a running room event script executed by room_events_check.
ScdEventEntry  g_ScdEventTable[8] = {};

// SCD event system globals
ScdEventEntry* g_pScdEventCurrent = NULL;               // 0x00bf0848
unsigned char* g_ScdOpcodes = NULL;                     // 0x00bf0800
unsigned int*  g_CmdOpcodesPointer = NULL;              // 0x00bf0804
unsigned char  g_ScriptContinueFlag = 0;                // 0x00bf07fa
unsigned char* g_EvtScripts = NULL;                     // 0x00d213b4
unsigned char* g_RoomScdOpcodes = NULL;                  // 0x00d213b8

// SCD flag bank 9
unsigned int   DAT_00d213a0[2] = {};                    // 0x00d213a0

// Desks/locks flags (SCD flag bank 2)
unsigned int   g_desks_locks_flags[16] = {};            // 0x00be9840

// Player entity fields used by SCD command functions
unsigned short DAT_00d211c4 = 0;                        // 0x00d211c4
unsigned short DAT_00d21350 = 0;                        // 0x00d21350
unsigned short DAT_00d2276c = 0;                        // 0x00d2276c
unsigned int   DAT_00d22770 = 0;                        // 0x00d22770

// Sound system BGM state
unsigned char  DAT_00bf07ef = 0;                        // 0x00bf07ef
int            DAT_00bf07f0 = -1;                       // 0x00bf07f0

// Room BGM state table
unsigned char  g_abRoomBgmState[224] = {};              // 0x00ac98e8

// Screen effect parameter storage
unsigned int   DAT_00ac98e0[4] = {};                    // 0x00ac98e0
unsigned int   DAT_00ac98e4[4] = {};                    // 0x00ac98e4

// Special room lighting globals (also used by MainLoop.cpp)
int            g_SpecialR1 = 0;                         // 0x00be961d
int            g_SpecialG1 = 0;                         // 0x00be961e
int            g_SpecialB1 = 0;                         // 0x00be961f

// TMD model caching state
int*           DAT_00bca0d0 = NULL;                     // 0x00bca0d0
int*           DAT_00bca0d4 = NULL;                     // 0x00bca0d4
unsigned char  DAT_008e1c78 = 0;                        // 0x008e1c78
unsigned char  DAT_008e1c70 = 0;                        // 0x008e1c70
unsigned char  DAT_008e1c7c = 0;                        // 0x008e1c7c
unsigned char  DAT_008e1c74 = 0;                        // 0x008e1c74
// Note: DAT_004d2bdc and DAT_004d2be0 are already defined above
// Note: DAT_004d6444 is already defined at line 445

// Bullet effect parent sprite info pointer
int            DAT_00bf0a34 = 0;                        // 0x00bf0a34

// ============================================================================
// game_loop globals (0x00480b30)
// ============================================================================

// 0x00d22760 - Menu open state machine: 0=none, 1=open requested, 2=initializing, 3=closing
int            g_openMenuFlag = 0;

// 0x00bebcc2 - Backup of g_message_flags when menu is requested
unsigned short g_short_message_flags = 0xFFFF;

// 0x008f8898 - Saved special room light state across room transitions
int            g_int_008f8898 = -1;

// 0x004d2294 - Countdown frame counter (counts 0-29, then increments countdown timer)
int            DAT_004d2294 = 0;

// 0x004d2288 - Death delay countdown (set to 90 frames before triggering fade)
int            DAT_004d2288 = 0;

// 0x004d46a4 - Camera/lighting update enable flag (set by room_set)
int            DAT_004d46a4 = 0;

// 0x004d4680 - Debug save menu display trigger (toggled by debug key)
int            g_displayDebugSaveMenu = 0;

// 0x004d4684 - Debug save menu state flag (prevents re-trigger until re-armed)
int            g_debugSaveMenuFlag = 0;

// 0x004d228c - Menu processing active flag (set during menu open/close)
int            DAT_004d228c = 0;
