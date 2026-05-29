// Original addresses from Ghidra marked for each global variable
#include "Globals.h"

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

// --- Drive types ---
// 0x008f87c4
UINT g_DriveTypes[MAX_DRIVES] = {};
// Drive letter buffer
char g_DriveLetterBuffer[256] = {};

// --- Installation path ---
// 0x00d91bd0
char g_szInstallPath[MAX_PATH] = {};
char g_szCreateDir[260] = {};

// --- Registry loaded data ---
BYTE g_keyBindingData[32] = {};
BYTE g_joystickBindingData[128] = {};
// 0x004d1f50
BOOL g_bIsSideWinderConnected = FALSE;
BYTE g_InstallFlagData = 0;

// --- Shared memory ---
// DAT_007dfd20
HANDLE g_hFileMapping = NULL;
BYTE* g_pSharedMemory = NULL;

// --- Window rect for drawing ---
// 0x004ba720
RectDrawDesc g_window_rect = {320, 0, 0, 0, 0, 0, 240, 0};

// --- Marni System objects ---
// 0x00ac4028
void* g_pMarniDirect3D = NULL;
// Input state pointer
MasterInputState* g_pMasterInputState = NULL;

// --- Main state flags ---
// 0x004ba744
DWORD g_main_state_flags = 0;
// 0x004ba758
int g_fading_state = 0;
// 0x004ba75c
int g_fading_counter = 0;
// 0x004ba760
int g_fade_type_id = 0;
// Player health
int g_playerHealth = 0;

// --- Game state ---
int g_selectedPlayerID = 0;
// 0x00d91bc8
int g_SelectedPlayerID = 0;
int g_currentFMVID = 0;
// 0x00d91bcc
int g_CurrentFMVID = 0;
int g_stageId = 0;
// 0x00d91bd0
int g_STAGE_ID = 0;
int g_roomId = 0;
int g_roomCameraId = 0;
// 0x00d91bd4
int g_ROOM_ID = 0;
// 0x00d91bd8
int g_roomCamera_id = 0;

// --- Screen pos ---
// 0x00ac3ff8
int g_ScreenOffsetX = 0;
// 0x00ac3ffc
int g_ScreenOffsetY = 0;

// --- Task system globals ---
// _g_StackPointer 0x007e0cc8
DWORD g_StackPointer = 0;
// 0x00d1fde4
TaskControlBlock g_TasksTable[3] = {};
// 0x00bf09ec
void* g_CurrentTask = NULL;
// 0x00d91a70
DWORD g_TasksESP[3] = {};
// 0x00d91a80
DWORD g_TasksEIP[3] = {};
// 0x00d91a7c
DWORD g_CurrentTaskID = 0;
// 0x00d91a68
void* g_CurrentTaskPtr = NULL;
// 0x00d91a8c
DWORD g_SchedulerESP = 0;
// 0x004ba0b8
DWORD g_SchedulerRunningFlag = 0;
// 0x00d91a90
void* g_AsyncRpcCallback = NULL;

// --- Input state ---
// 0x00bcb2e0
DWORD g_RawPadPressed = 0;
// 0x00bcb2e4
DWORD g_InputFlags = 0;
// 0x00bf0a08
DWORD g_PlayerPadPressed = 0;
// 0x00bf0a0c
DWORD button_pressed_id = 0;
// 0x004bae30
DWORD g_PlayerPadHeldPrev = 0;
// SideWinder raw pad state
DWORD g_PadRawP2 = 0;
// 0x004bcb3c
BOOL g_DisablePad = FALSE;

// --- Menu / dialog flags ---
// 0x00be0e28
int g_menu_choice_id = 0;
// 0x004bcb50
BOOL g_displayReturnToTitleScreen_Flag = FALSE;
// 0x004bcb54
BOOL g_displayExitGameScreen_flag = FALSE;

// --- Sound system ---
// 0x00be15e0
int g_SndFadeType = 0;
// 0x00be15e4
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
void* g_SoundManager = NULL;
DirectSound* g_pDirectSound = NULL;
DWORD g_CachedWaveOutVolume = 0;
int g_WaitForMusicTimer = 0;
unsigned char g_BGM_STATE = 0xFF;
HWND g_MainWindowHandle = NULL;
int g_setVolResult = 0;
int g_CurBank = 0;
unsigned char g_snd_slot_00ac99d5 = 0;
int g_SoundPanVol = 0;
int g_SndPanSet_result = 0;
char* g_wavName = NULL;
int g_sndload_bank_index = 0;
short g_CurSlot = 0;
int g_SndFadeStepTbl[64] = {};

// --- Game init ---
// 0x00bcb2e8
int init_game_flag = 0;

// --- Timing ---
// 0x007e0df4
DWORD g_dwSystemTimer1 = 0;
// 0x007e0df8
DWORD g_dwGameTimer1 = 0;
// 0x004d46c4
DWORD g_GameInitTime = 0;
// 0x004d46cc
DWORD g_gameTimerSnapshot = 0;
// 0x004d46d0
DWORD Game_timer = 0;
// 0x004d45fc
DWORD g_LastFrameTime_ms = 0;

// Frame rate governor globals (0x004973d0)
int g_frameTimeIndex = 0;          // 0x004d45f8
int g_frameTimeBuffer[4] = {};     // 0x00ac4000
int g_frameTimeAccumulator = 0;   // 0x004d45f4
int g_frameTargetTime = 100;      // 0x004d45ec (start at 100 for first-frame present)
int g_ScreenAccessReady = 1;      // 0x004d4658 (start ready so first frame presents)
int g_RenderAccessReady = 1;      // 0x004d4688

// --- MCIVideo ---
// 0x004bcb44
int g_mciVideoDeviceID = 0;
// 0x004bcb48
BOOL g_bMCIVideoEvent = FALSE;

// --- Misc flags ---
BOOL g_bIsPaused = FALSE;       // DAT_004bcb2c
BOOL g_bWindowActive = TRUE;    // DAT_004bcb30 (start active so game loop runs)
BOOL g_bQuitFlag = FALSE;       // DAT_004bcb40
BOOL g_bFrameRateUnlocked = FALSE; // DAT_004bcb48
BOOL g_bFrameSkipDetected = TRUE;  // DAT_004d46dc (allow frame timing check)

// --- Print text buffer ---
// 0x00be0e2c
char PRINT_TEXT_BUFFER[256] = {};

// Print text globals (0x00be1170 area)
TexturePrintState g_texPrintState = {};
int   g_PrintClutTint = 0;

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

// Asset loading globals
void* g_image_buffer = NULL;
void* g_ITEMS_IMAGES_BUFFER = NULL;
int   g_InstallFlagDataLoaded = 0;      // DAT_004b3998
int   g_SpriteBufferFlag = 0;           // DAT_004b399c
int   g_SpriteAsyncFlag = 0;            // DAT_00d91bc8
int   g_FileOpenCount = 0;              // DAT_00d91bcc
int   g_FileRetryFlag = 0;              // DAT_004d4690
int   g_playingGameFlag = 0;            // 0x004d4674
int   g_loadSaveStateFlag = 0;          // 0x004d4678
int   g_demoIdleTimer1 = 0;             // DAT_004c44d8
void* g_loadDataDestPointer = NULL;     // 0x00bebcdc
void* g_fmvDataPointer = NULL;          // 0x00bf07fc
int   g_fmvPlayCount = 0;               // 0x004bae34

// Texture page table
void* g_TexturePageTable = NULL;
DWORD g_TexturePageTable_DAT[256] = {};
ID3D11ShaderResourceView* g_TexturePageSRV[256] = {};
int   g_TexturePageWidth[256] = {};
int   g_TexturePageHeight[256] = {};

// Player input data
int   g_PlayerDpadHeld = 0;
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

// Task data arrays
DWORD g_TaskDataArray_ba750[16] = {};
DWORD g_TaskDataArray_ba780[16] = {};

// Texture/room state
short g_TextureBankID = 0;             // _DAT_00bebcc4
short g_SpecialRoomLightR = 0;
short g_SpecialRoomLightState = 0;
short g_SpecialRoomLightDelta = 0;

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

// Input key binding config
DWORD g_KeyBindingConfig[32] = {};
BYTE  g_MasterInputState[256] = {};

// --- Other state vars ---
int g_ScreenAccessCheck = 1;    // DAT_004d4684 (start enabled so rendering happens)
int g_RenderAccessCheck = 1;    // DAT_004d468c (start enabled so rendering happens)
int g_demoTimer = 0;            // DEMO timer pattern field
BOOL g_bFullScreenFlag_68 = FALSE;  // used for cursor hiding logic
