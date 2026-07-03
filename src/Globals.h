#pragma once

// Core types and structures
#include "game/Types.h"
#include "game/Entities.h"
#include "game/Items.h"

// ============================================================================
// Global Variables
// ============================================================================

// Window system
extern HWND          g_hWnd;                           // 0x00bcb2c0
extern HINSTANCE     g_hInstance;                      // 0x00bcb2c4
extern BOOL          g_bIsSoftwareRendering;           // 0x004bcb2c
extern BOOL          g_isGameCursorHiddenFlag;         // 0x00be0e29
extern BOOL          g_bHasFinalizedSettings;          // 0x004bcb78
extern HANDLE        g_hMutex;                         // 0x00bcb2c8

// Display / Adapter
extern DWORD         g_dwSelectedDisplayAdapterID;     // 0x007d9148
extern DWORD         g_dwSelectedDisplayModeID;        // 0x007d914c
extern DWORD         g_dwScreenWidth;                  // 0x007d9150
extern DWORD         g_dwScreenHeight;                 // 0x007d9154
extern BOOL          g_bFullScreen;                    // 0x007d9158
extern int           g_dwBitDepth;                     // 0x004d642c
extern DWORD         g_dwPlayCount;                    // 0x004d6430
extern DWORD         g_dwClearCount;                   // 0x004d6434
extern DWORD         g_GPU_VENDOR_ID;                  // 0x004bcb64

// Display mode storage
extern DisplayModeInfo g_DisplayModeBuffer[MAX_DISPLAY_MODES];
extern int           g_NumDisplayModes;                // 0x007d8f24

// D3D Renderer info
extern D3DRendererInfo g_D3DRenderers[8];
extern int           g_NumD3DRenderersAvailable;       // 0x007e0e08

extern int           g_SelectedPlayerID;               // 0x008f879c

// Drive types
extern UINT          g_DriveTypes[MAX_DRIVES];
extern char          g_DriveLetterBuffer[256];

// Installation path
extern char          g_szInstallPath[MAX_PATH];        // 0x00d91bd0
extern char          g_szCreateDir[260];

// Registry loaded data
extern BYTE          g_keyBindingData[32];
extern BYTE          g_joystickBindingData[128];
extern BOOL          g_isSideWinderConnected;         // 0x004d1f50
extern BYTE          g_InstallFlagData;

// Shared memory
extern HANDLE        g_hFileMapping;                   // 0x007dfd20
extern BYTE*         g_pSharedMemory;

// Window rect for drawing
extern RectDrawDesc  g_window_rect;                    // 0x00d227b0

// Marni System objects
extern void*         g_pMarniDirect3D;                 // 0x00ac4028

// 0x00ac4030 - MasterInputState (keyboard + joystick states)
extern MasterInputState g_pMasterInputState;

int __stdcall VideoDriver_ClearState348(void* obj, void* context);

// ============================================================================
// g_main_state_flags - Global game state bit flags (0x00be41c0)
//
// This 32-bit flag word controls global game state, rendering behavior, and
// scene transitions. Bits are tested/set across the entire codebase.
//
// Bit   Mask          Description
// ----  ------------  ----------------------------------------------------------
//   0   0x00000001    Entity joint animation flag (tested in room_set, PlayerAnimations)
//   7   0x00000080    Special entity sound state (tested in SoundSystem)
//   8   0x00000100    Message display Y-position (byte 1, test in set_message_display)
//   9   0x00000200    Room events wait condition (byte 1 bit 1, RoomEvents)
//  10   0x00000400    "Got item" active (set by cmd_got_item)
//  11   0x00000800    "Got item" toggle flag (toggled by cmd_got_item)
//  13   0x00002000    Key item depleted / drop message (SaveLoadScreen)
//  16   0x00010000    Screen intensity animation direction (main_loop)
//  17   0x00020000    SFX playback active / music wait done (sfx_set, SoundSystem)
//  18   0x00040000    FMV playback active (logos_state, cmd_fmv_set, main_loop)
//  19   0x00080000    Screen panning reset / ending state (ResetScreenPanning, ending_state)
//  20   0x00100000    Camera changes disabled during cutscene (cut_set, cmd_cut_toogle)
//  23   0x00800000    Room RDT variant: 0=Chris, 1=Jill/Rebecca (LoadRoomRdt, char select)
//  25   0x02000000    Game loop active flag (game_loop)
//  26   0x04000000    Game initialized / loading complete (InitializeGame, room_set)
//  27   0x08000000    (tested alongside bit 26 in main_loop 0x4008000 combo)
//  28   0x10000000    New game (0) vs continue/load game (1) (InitializeGame)
//  29   0x20000000    Screen fade transition in progress (main_loop, title, char select)
//  30   0x40000000    Debug overlay mode / exit-to-title transition (rendering, main_loop)
//  31   0x80000000    Save/load screen active / title screen overlay (title, save/load)
//
// The lower 2 bits (0-1) are also used as a 2-bit SCD script parameter
// (cmd_entities_0x0f writes g_ScdOpcodes[1] into bits 0-1).
//
// The lower nibble is cleared by room_set: g_main_state_flags &= 0xfffffff0.
//
// Byte 3 bit 0x10 is tested in display_game_loading_message for input check.
// Byte 7 bit 0x10 is tested in the same function.
// ============================================================================
// Main state flags
extern DWORD         g_main_state_flags;               // 0x00be41c0
extern DWORD         g_main_state_flags2;              // 0x00be41c4
extern WORD          g_message_flags;                  // 0x00bebcc0

// Message display system
extern unsigned short g_messageFlagsBackup;            // 0x00bebcc2
extern unsigned short g_PauseGameInMsgFlag;            // 0x00bf0a18
extern unsigned char* g_MessagePtr;                    // 0x00bf0a1c
extern unsigned char  g_MessageStateCounter;            // 0x00bf0a16
extern short          g_MessageScreenY;                 // 0x00bf0a1a
extern unsigned char  g_MessageSpeedUpFlag;             // 0x00bf0a17
extern RDT*           g_RdtPointer;                     // 0x00bebcd0
extern unsigned char* global_messages[64];             // 0x004bfc58
extern unsigned char* g_MessageCurrentPtr;             // 0x00bf0a20 - current position in message text
extern unsigned char* g_MessageSavedPtr;               // 0x00bf0a24 - saved ptr for item name returns
extern unsigned char  g_MessageCharDelay;              // 0x00bf0a29 - base delay between characters
extern unsigned char  g_MessageCharTimer;              // 0x00bf0a2a - current timer countdown
extern unsigned char  g_MessageClutBase;               // 0x00bf0a2b - CLUT color base
extern unsigned char  g_MessageClutCopy;               // 0x00bf0a2c - copy of CLUT base
extern int            g_MessageLineCounter;            // 0x008e1c64 - line counter for newlines

// Game state
extern int           DAT_00d91bc8;                     // 0x00d91bc8

extern int           g_CurrentFMVID;                   // 0x008f8790

// Screen pos
extern short         g_ScreenOffsetX;                  // 0x004BCAC8
extern short         g_ScreenOffsetY;                  // 0x004BCACA
extern signed char   g_ScreenShakeOffsetX;             // 0x00bca0d8
extern signed char   g_ScreenShakeOffsetY;             // 0x00bca0d9

// fading
extern short         g_fading_counter;                 // 0x00bebcca
extern unsigned char g_fade_type_id;                   // 0x00bf0a2f
extern BYTE          g_bGameActive;                    // 0x00be41dc

// Sprite animation/screen tint state (0x00be41d0-0x00be41d4)
extern int           g_spriteAnimActive;               // 0x00be41d0
extern int           g_spriteAnimR;                    // 0x00be41d1
extern int           g_spriteAnimG;                    // 0x00be41d2
extern int           g_spriteAnimB;                    // 0x00be41d3
extern short         g_spriteAnimIntensity;            // 0x00be41d4

// Task system globals
extern DWORD         g_StackPointer;                   // 0x007e0cc8
extern TaskControlBlock g_TasksTable[3];               // 0x00d1fde4
extern TaskControlBlock*         g_CurrentTask;                    // 0x00bf09ec
extern DWORD         g_TasksESP[3];                    // 0x00d91a70
extern DWORD         g_TasksEIP[3];                    // 0x00d91a80
extern DWORD         g_CurrentTaskID;                  // 0x00d91a7c
extern TaskControlBlock*         g_CurrentTaskPtr;                 // 0x00d91a68
extern DWORD         g_SchedulerESP;                   // 0x00d91a8c
extern DWORD         g_SchedulerRunningFlag;           // 0x004ba0b8
extern void*         g_AsyncRpcCallback;               // 0x00d91a90

// Input state
extern DWORD g_lastScanCodeOrMsgID;                    // 0x00bcb2e0

// Entity / Player globals
extern PlayerEntity  g_playerEntity;                   // 0x00be62e4 - main player entity (0x180 bytes)
extern Entity        g_EnemiesList[30];                // 0x00be6464 - enemy entity array (30 x 0x18C bytes)
extern int           g_enemy_count;                    // number of active enemies
extern Entity*       ENTITY;                           // 0x00bebcd4 - current entity pointer
extern unsigned char g_PlayerMaxHealth;                // 0x00be6459
extern unsigned char g_controllerConfig;               // 0x00be9a5c
extern DWORD         g_AttractModeIdleTimer;           // 0x004c44d8
#define g_demoIdleTimer1 g_AttractModeIdleTimer
extern DWORD         g_gameSessionInitFlag;            // 0x00d213b0
extern DWORD         g_RoomCameraData;                 // 0x004bca88
extern DWORD         g_RoomCameraDataCopy;             // 0x00d1fdd4
extern DWORD         g_deadMoveValue;                  // 0x00d1fdd0
extern DWORD         g_lightMatrixPtr;                 // 0x00d1fdcc
extern MATRIX        g_identityMatrixData;             // 0x004bca68
extern MATRIX        g_lightMatrix;                    // 0x004bcaa8
extern DWORD         g_scaDataTable[4];                // 0x004d4540
extern DWORD         g_scaChrisData[4];                // 0x004d4510
extern DWORD         g_scaData2[4];                    // 0x004d4520
extern DWORD         g_scaJillData[4];                 // 0x004d4530
extern BYTE          g_entityDataBlock[0x200];         // 0x00d211d0
extern DWORD         g_scaPoolPtr;                     // 0x00d21354
extern DWORD         g_scaPoolBase;                     // 0x00d21358

extern DWORD g_RawPadHeld;                             // 0x00bf0a04 - raw held state (input to edge detect)
extern DWORD g_PlayerPadPressed;                       // 0x00bf0a08
extern DWORD g_button_pressed_id;                      // 0x00bf0a0c
extern DWORD g_PlayerPadHeld;                          // 0x00bf0a10 - edge-detected held state (output)
extern DWORD g_PlayerPadHeldPrev;                      // 0x004bae30

extern WORD  g_RawPadState;                            // 0x00be05b2
extern WORD  g_padEdgeDetectedWord;                    // 0x00be05b4 - edge-detected raw pad word

// Joystick/controller globals (used by ReadPadBoth / JoyToPSX)
// NOTE: g_PadActiveP1 (0x00ac422c), g_PadActiveP2 (0x00ac4404),
// g_PadRawP1 (0x00ac4058), g_PadRawP2 (0x00ac4230) are aliases for
// g_pMasterInputState fields and have been merged into the struct.
extern DWORD g_PadBtnWord;                             // 0x00ac4018
extern int   g_NumControllers;                         // 0x00ac7b58
extern int   g_JoyWarnPrinted;                         // 0x004b1958
extern const DWORD g_JoyRemapTbl[2][32];              // 0x004b1858 - PC joystick → PSX button remap
extern BOOL g_DisablePad;                              // 0x004bcb3c

// Pad remap tables and dpad globals (used by PlayerPad_Update)
extern const WORD* g_padRemapTable[4];                // 0x004bf300 - pointers to remap sub-tables
extern WORD g_padRemapSubTable3[16];                  // 0x00be9a3c - runtime configurable remap table
extern WORD g_PlayerDpadHeldPrev;                      // 0x00bf0a14 - previous dpad held state
// g_PlayerDpadPressed is now a macro to g_BioCard.playerDpadPressed (see Items.h)
extern WORD g_demoPadData[512];                       // 0x00d21d10 - attract demo input data

// Bio Card
extern BioCardLayout g_BioCard;                        // 0x00be9620

// Menu / dialog flags
extern BOOL          g_displayReturnToTitleScreen_Flag;// 0x004d466c
extern BOOL          g_displayExitGameScreen_flag;     // 0x004D4668
extern int           g_demoTimer;                      // 0x004bcb5c
extern short         g_DemoTimerCur;                   // 0x00d21cee
extern short         g_DemoTimerMax;                   // 0x00d21cf0

// F9 key handling state (used by OnKeyDown)
extern DWORD         g_lastF9PressTime;                 // 0x004d46e0 - timeGetTime() of last F9 press
extern int           g_blockF9Flag;                    // 0x004b3870 - blocks F9 processing when set
extern int           g_F1DebugMode;                    // 0x004d4654 - cycles 0-3 on F1 press
extern int           g_pressF9Flag;                    // 0x004ba718 - set when F9 triggers game reset
extern int           g_resetGameFlag;                  // 0x004d4670 - triggers game state reset

// Sound system
extern int           g_SndFadeType;                    // 0x00BF0A2D
extern int           g_SndRampFramesLeft;              // 0x00AC9908 
extern int           g_BgmSoundBank;
extern int           g_SfxBanks[64];
extern int           g_RoomSfxBanks[64];
extern int           g_CharacterSfxBanks[64];
extern int           g_emSndBanks[64];
extern int           g_SndBank[64];
extern int           g_SfxVolume;
extern char          g_BgmPaused;
extern int           g_SndRampDirection;
extern int           g_SndRampCurrentVolume;
extern int           g_SndRampBankIndex;
extern int           g_SndDistSteps;
extern int           g_EnemySndVolume;                // 0x00ac98d0 - enemy sound volume
extern unsigned int  g_SoundSystemFlags;              // 0x004b3998 - sound system flags (bit 3 = alt path)
extern char          g_SoundAltPathPrefix[256];       // 0x00d91bd0 - alternate sound path prefix

// Sound callback function pointers (set by Room_SetupCollisionCallbacks)
typedef void (*SoundCallbackRect)(short*, int*, short*);
typedef unsigned int (*SoundCallbackCircle)(unsigned short*, int*);
typedef void (*SoundCallbackFlag)(void);
extern SoundCallbackRect   g_SoundCallbackRect;       // 0x00ac9c04
extern SoundCallbackRect   g_SoundCallbackRect2;      // 0x00ac9c14
extern SoundCallbackCircle g_SoundCallbackCircle;     // 0x00ac9c0c
extern SoundCallbackFlag   g_SoundCallbackFlag;       // 0x00ac9c10

// Per-room enemy sound name table (indexed by stageId * 29 + roomId)
// Each entry points to an array of 4 sound name strings (or NULL)
extern const char** g_RoomSoundNameTable[145];
extern void*         g_SoundManager;
extern class DirectSound* g_pDirectSound;
extern DWORD         g_CachedWaveOutVolume;
extern int           g_WaitForMusicTimer;
extern unsigned char g_BGM_STATE;
extern HWND          g_MainWindowHandle;
extern int           g_setVolResult;
extern int           g_CurBank;
extern unsigned char g_snd_slot_00ac99d5;
extern int           g_SoundPanVol;
extern int           g_SndPanSet_result;
extern char*         g_wavName;
extern int           g_sndload_bank_index;
extern short         g_CurSlot;
extern int           g_SndFadeStepTbl[64];


extern POLY_F4      Poly_F4_ARRAY_004ba750[4];          // 0x004ba750

// Game init
extern int           init_game_flag;                   // 0x004ba7b0

// Timing
extern DWORD         g_dwSystemTimer1;                 // 0x007e0df4
extern DWORD         g_dwGameTimer1;                   // 0x007e0df8
extern DWORD         g_GameInitTime;                   // 0x004d46c4
extern DWORD         g_gameTimerSnapshot;              // 0x00be9844
extern DWORD         Game_timer;                       // 0x00d22730
extern DWORD         DAT_004d46d4;                     // 0x004d46d4
extern DWORD         g_LastFrameTime_ms;               // 0x004d45fc

// Frame rate governor
extern int           g_frameTimeIndex;                 // 0x004d45f8
extern int           g_frameTimeBuffer[4];             // 0x00ac4000
extern int           g_frameTimeAccumulator;           // 0x004d45f4
extern int           g_frameTargetTime;                // 0x004d45ec
extern int           g_ScreenAccessReady;              // 0x004d4658
extern int           g_ScreenAccessCountdown;          // 0x004d4684
extern int           g_RenderAccessReady;              // 0x004d4688
extern int           g_MarniScreenReady;               // 0x00497340
extern DWORD         g_MarniScreenColor;               // 0x00497360

// MCIVideo
extern int           g_mciVideoDeviceID;               // 0x004bcb44
extern BOOL          g_bMCIVideoEvent;                 // 0x004bcb48

// Misc flags
extern BOOL          g_isPaused;
extern BOOL          g_bWindowActive;                  // 0x004bcb30
extern BOOL          g_bQuitFlag;                      // 0x004bcb40
extern BOOL          g_bUseFrameSkip;
extern BOOL          g_bFrameSkipDetected;             // 0x004d46dc
extern int           g_ScreenAccessCheck;              // 0x004d2290
extern int           g_RenderAccessCheck;

extern RectDrawDesc  g_rect;                           // 0x00be1150
extern TextureDesc   g_TextureDesc;                    // 0x00be1160
extern int           unk_00be1180;                     // 0x00be1180

// Print text
extern char          PRINT_TEXT_BUFFER[256];           // 0x00be0e20
extern int           g_PrintClutTint;

// Image buffer for asset loading
extern void*         g_image_buffer;
extern void*         g_ITEMS_IMAGES_BUFFER;

// Asset loading globals
extern int           g_InstallFlagDataLoaded;
extern int           g_SpriteBufferFlag;
extern int           g_SpriteAsyncFlag;
extern int           g_FileOpenCount;
extern int           g_FileRetryFlag;
extern int           g_playingGameFlag;                // 0x004d4674
extern int           g_loadSaveStateFlag;              // 0x004d4678
extern void*         g_loadDataDestPointer;            // 0x00bebcdc
extern unsigned char g_selectedFmvId;                  // 0x00bf07fb
extern void*         g_fmvDataPointer;                 // 0x00bf07fc
extern int           g_fmvPlayCount;                   // 0x004bae34

// Texture page table and video driver arrays
extern void*         g_TexturePageTable;
extern DWORD         g_TexturePageTable_DAT[256];
extern ID3D11ShaderResourceView* g_TexturePageSRV[256];
extern int           g_TexturePageWidth[256];
extern int           g_TexturePageHeight[256];

// Player input data fields
extern int           g_PlayerInputConfig_3c;
extern int           g_PlayerInputConfig_3e;
extern int           g_PlayerInputConfig_40;
extern int           g_PlayerInputConfig_42;
extern int           g_PlayerInputConfig_44;
extern int           g_PlayerInputConfig_46;
extern int           g_PlayerInputConfig_48;
extern int           g_PlayerInputConfig_4a;
extern int           g_PlayerInputConfig_4c;
extern int           g_PlayerInputConfig_4e;
extern int           g_PlayerInputConfig_50;
extern int           g_PlayerInputConfig_52;
extern int           g_PlayerInputConfig_54;
extern int           g_PlayerInputConfig_56;
extern int           g_PlayerInputConfig_58;
extern int           g_PlayerInputConfig_5a;

// Task data arrays
extern DWORD         g_TaskDataArray_ba750[16];
extern DWORD         g_TaskDataArray_ba780[16];

// Image processing/status variables
extern unsigned char g_TextureBankID;                  // 0x00bebcc4
extern unsigned char g_TextureDepthByte;               // 0x00bebcc5
extern unsigned short g_SavedTextureBankID;              // 0x00bebcc6 - saved room texture bank ID (restored after cutscenes)

// Key binding data area
extern BYTE          g_KeyBindingVectors[32];

// Marni video driver arrays
extern DWORD         g_VideoDriverArray_D0[64];
extern DWORD         g_VideoDriverArray_03c[64];
extern DWORD         g_VideoDriverArray_04c[64];
extern DWORD         g_VideoDriverArray_068[64];
extern DWORD         g_VideoDriverArray_06c[64];
extern DWORD         g_VideoDriverArray_4d0[1024];
extern DWORD         g_VideoDriverArray_4fa[256];
extern DWORD         g_VideoDriverArray_4fc[256];
extern DWORD         g_VideoDriverArray_500[256];
extern DWORD         g_VideoDriverArray_810[256];
extern DWORD         g_VideoDriverArray_814[256];
extern DWORD         g_VideoDriverArray_838[2048];
extern DWORD         g_VideoDriverArray_520[2048];
extern short         g_VideoDriverArray_FA[256];

// Input key binding configuration
extern DWORD         g_KeyBindingConfig[32];
extern BYTE          g_MasterInputState[256];

// Counters
extern int           g_numFramesRendered;
extern int           g_numFramesPresented;

// Screen info
extern BOOL          g_bAccessibilityAnimations;

// MCI notification
extern BOOL          g_bMCINotifyEnabled;              // 0x004bcb58
extern BOOL          g_bMCINotifyFlag;                 // 0x004bcb5c

// Game loop
int main_loop(void);

// Frame counter
extern int           g_loopCounter;

// Debug clear color
extern float         g_debugClearR;
extern float         g_debugClearG;
extern float         g_debugClearB;
extern int           g_debugTaskFrame;

// Sprite/clear color
extern float         g_color_r;                        // 0x004c336c
extern float         g_color_g;                        // 0x004c3370
extern float         g_color_b;                        // 0x004c3374
extern float         g_spriteColorScale;                // 0x004af2ac

// Model/animation buffers
extern BYTE          g_entityModelBuffer[0xCC00];      // 0x00bf11c0
extern DWORD         g_entityModelBuffer2[0xB4];       // 0x00bfddc0
extern DWORD         g_animObjectBuffer[0x680];        // 0x00c133c0
extern DWORD         g_animSlotIndex;                  // 0x008f8c78

// TMD processing flags
extern DWORD         DAT_004d2bd8;
extern DWORD         DAT_004d2bf4;
extern DWORD         DAT_004d2bdc;
extern int           DAT_004d2be0;
extern DWORD         g_tmdAsyncData;                   // 0x008fc424
extern DWORD         DAT_004c1a2c;
extern DWORD         DAT_00ae9f04;
extern DWORD         DAT_00ae9f06;
extern DWORD         DAT_00ae9f00;
extern DWORD         DAT_00ae9efc;
extern DWORD         DAT_004d6444;

// TMD async creation params
extern DWORD         g_asyncTmdDepth;
extern DWORD         g_asyncTmdDataPtr;
extern DWORD         g_asyncTmdObjectPtr;
extern DWORD         g_asyncTmdResult;

// Complex TMD object tracking
extern DWORD         g_complexTmdObjectArray[256];
extern int           g_complexTmdObjectIds[256];

// Face normal buffer
extern BYTE          g_faceNormalBuffer[250 * 8];

// Texture/bank arrays
extern BYTE          g_textureQueueData[40];           // 0x00d22740
extern DWORD         g_tmdTextureAllocated[23];        // 0x00922a40
extern BYTE          g_psxTextureArray[23 * 0x1b60];   // 0x00a75168
extern DWORD         g_textureBankRedirect[23];        // 0x00aae2b0

// Weapon animation angles
extern int           g_weaponAngle_Special;
extern int           g_weaponAngle_PrimX;
extern int           g_weaponAngle_PrimY;
extern int           g_weaponAngle_PrimZ;
extern int           g_weaponAngle_Sec1X;
extern int           g_weaponAngle_Sec1Y;
extern int           g_weaponAngle_Sec1Z;
extern int           g_weaponAngle_Sec2X;
extern int           g_weaponAngle_Sec2Y;
extern int           g_weaponAngle_Sec2Z;

// Animation dispatch jump tables
extern void*         DAT_004c2ac8[];
extern void*         DAT_004ba360[];
extern void*         DAT_004b1a90[];
extern void*         DAT_004c10b0[];

// Animation data constants
extern DWORD         DAT_00606060;
extern DWORD         DAT_00ffff50;

// Scratch globals used by animation functions
extern VECTOR        g_playerPosScratch;               // 0x00be11b0
extern SVECTOR       g_svecScratch;                    // 0x00be11a8 (gSVector in Ghidra)
extern MATRIX        g_matrixScratch;                  // 0x00be11c0 (MATRIX_00be11c0 in Ghidra)
extern unsigned int  g_deathAnimationFlag;             // 0x00be0dd8
extern unsigned int  g_animFrameIdSave;                // 0x00be0dfc - temp save for animation_frame_id
extern int           g_playerDisplacement;             // 0x00be0de0 - joint displacement for animation
extern void*         g_tempVar;                        // 0x00be0df8 - temp pointer for joint processing
extern void*         g_playerAnimFunctions[52];        // 0x00bebbd8

// Title screen display image SRV
extern ID3D11ShaderResourceView* g_displayImageSRV;

// Title state globals
extern unsigned char g_titleLoopFlag;                  // 0x00d22777
extern unsigned char g_titleMode;                      // 0x00d22775
extern unsigned char g_titleOptionsFading;             // 0x00d22776
extern unsigned char g_titleSelectionId;               // 0x00d22774
extern short         g_titleDemoTime;                  // 0x00d22788
extern short         g_titleTextureDepthData[8];       // 0x00d22778
extern int           g_sceneRenderParam;               // 0x004d6300
extern DWORD         g_titlePrimType;                  // 0x004d6398
extern DWORD         g_primParam;                      // 0x004d63e0
extern DWORD         g_primFlag2;                      // 0x004d63e4
extern int           g_displayWidth;                   // 0x00bf09f8
extern int           g_displayHeight;                  // 0x00bf09fc
extern int           g_displayMode;                    // 0x00bf09f4
extern int           g_DisplayImageWidth;              // 0x004c3364
extern int           g_DisplayImageHeight;             // 0x004c3368
extern int           g_titleTextureSlotId;             // 0x004c331c
extern int           g_texturePageMode;
extern int           g_texturePageHandle;
extern int           g_AsyncResult;
extern int           g_ExecuteBufferHandle;
extern int           g_SpriteQueueCount;
extern int           g_OTIndex;
extern DWORD         g_SpriteQueueIndex;
extern CMarniBits    g_MarniBitsWorkBuffer;
extern DWORD         g_MarniBitsOutput;
extern DWORD         g_ObjectWorkBuffer[64];

// Save/Load game state globals
extern int           g_healthStatus;                   // 0x00be6370
extern int           g_playerAngle;                    // 0x00be6368
extern short         g_playerBkpPosX;                  // 0x00be6380
extern short         g_playerBkpPosZ;                  // 0x00be6382
extern int           g_playerBkpHealthStat;            // 0x00be6384
extern short         g_playerBkpAngle;                 // 0x00be6388
extern int           g_playerPosX;                     // 0x00be6350
extern int           g_playerPosZ;                     // 0x00be6358
extern unsigned char g_equippedItemId;                 // 0x00be0e32
extern unsigned char* g_firstItemSlotPointer;          // 0x00be63a0
extern int           g_totalHeldItems;                 // 0x00be63a4
extern DWORD         g_heItemsX2Less1;                 // 0x00be63a8
extern unsigned char g_itemSlotIndices[8];             // 0x00be63b0
extern char          g_saveFileName[260];              // 0x004d42d8
extern char          g_saveDirPrefix[260];
extern char          g_saveSlotTextBuf[80];
extern BYTE          g_saveFileBuffer[0x1000];
extern BYTE          g_BackgroundImageBuffer[320 * 240 * 2 + 20]; // 0x00cf2298
extern char          g_characterNameTable[4][16];
extern char          g_locationNameTable[7][40];

// Object cleanup globals
extern int           g_objectDeleteFlag;
extern int           g_objectCountArray[32];
extern int           g_objectDeleteCounter;
extern int*          g_objectDeletePtr;
extern int           g_objectListCleanupFlag;
extern int           g_objectListCleanupCount;
extern DWORD         g_objectListPtrArray[512];
extern char          g_tmdObjectBuffer[250 * 0x1594];

// Global render-state objects
extern char          g_renderStateTex[0x36c];          // 0x00aad6f0
extern char          g_renderStateTMD[0x1594];         // 0x00aac158

// PS1 GTE fixed-point pipe matrix globals
extern int g_fixedPointPipe_matrix_m00;
extern int g_fixedPointPipe_matrix_m01;
extern int g_fixedPointPipe_matrix_m02;
extern int g_fixedPointPipe_matrix_m10;
extern int g_fixedPointPipe_matrix_m11;
extern int g_fixedPointPipe_matrix_m12;
extern int g_fixedPointPipe_matrix_m20;
extern int g_fixedPointPipe_matrix_m21;
extern int g_fixedPointPipe_matrix_m22;
extern int matrix_t0;
extern int matrix_t1;
extern int matrix_t2;

// ============================================================================
// Function declarations
// ============================================================================

// Print text
void PrintText8x8(short x, short y, unsigned char color, char shadow);
void PrintText8x14(short x, short y, unsigned char color, char flags);
void PrintTextFormatted(short x, short y, unsigned char color, const unsigned char* data);
void draw_rect(RectDrawDesc* rect, int blend, int flags);

// Task scheduler
void TaskScheduler_Init(void);
void TaskScheduler_Update(void);
void TaskScheduler_Reset(void);
void Task_execute(int id, void* func);
void Task_suspend(int id);
void Task_Resume(int id);
void Task_sleep(int frames);
void Task_exit(void);
void Task_chain(void* func);

// Async execution
void ExecAsync(void* callback);
void SetFrameRateMode(int status_flags);

// Input
void InputUpdate(void);
DWORD PlayerPad_Update(void);
DWORD ReadPadBoth(void);
DWORD JoyToPSX(DWORD pcMask, int player);

// Sound
void PauseSounds(void);
void ResumePausedSounds(void);
void UpdateSoundFadeState(void);
void UpdateSoundDecay(void);
void empty_0047b950(int param);
int  empty_483510(void);
void UpdateMusicWaitState(void);
void sounds_reset(void);
void LoadSoundBank(int sound_bank_id, void* buffer);
void PauseGameSoundsAsync(void);
void ResumeGameSoundsAsync(void);
void DestroySoundManagerAsync(void);
int  getSndStat(int bank);

// Video
void UpdateVideoPlayback(void);
void ClearScreen(void);

// Marni System
BOOL IsGraphicsSystemReadyForOperation(void);
void InitializeMarniSystem(void);
void EnumerateDisplayModes(void);
void EnumerateD3DRenderers(void);
void InitJoysticks(void);
int  IsSideWinderPadConnected(void);
void CreateLights(int count);

// Screen effects
void ResetScreenPanning(void);
void SetScreenOffset(int x, int y);
void ApplyScreenShake(void);
void UpdateMessageDisplay(void);
void ResetScreenAndRebuildSprites(int param);
void ApplyShakeAndRebuildSprites(void);
void SetScreenReadyWithDebugColor(int r, int g, int b);
void SetScreenReady(int param);
void FUN_004973a0(int param);
void FUN_00470a90(void);
void FrameRateGovernor(void);
void FUN_0040a8f0(void* param);
void ResetSpriteQueue(void);
void OT_InsertPrimitive(void* prim, unsigned int depth);
void AsyncCreateTexturePage(void);
void AsyncDestroyTexturePage(void);
void AsyncCreateObject(void);
void AsyncDeleteObject(void);
int  FUN_0046c230(void* data);
int  FUN_0046c280(int id);
void FUN_0046ccd0(void* buf, int a, int b);
void LoadShadowMaskTexture(void* imageBuffer, int slotBase);
void SetupTexturePageHandles(int slotIndex, int pageIndex);
void FUN_0046fb50(int a, int b, int* data);
void CreateTexturedQuad(int viewportSlot, int texturePageId, int* vertexData);
void ProcessTextureImage(void* imageBuffer, short textureBankID, short pageOffset, int slotIndex);
void LoadTexturePage(void* imageBuffer, short texId, short pageOffset, int slotIndex,
                     int unused, short posX, short posY, unsigned int flags);

// Game functions
void init_and_start_game(void);
void load_global_assets(void);
void logos_state(void);
void input_test_state(void);
void title_state(void);
void debug_state(void);
void UpdateDemoTimer(void);
void StMask(int param, int param2);
void clear_textures(void);
void setMenuScreenOffset(int w, int h, int x, int y, int mode);
void CenterScreenOrigin(void);
void SetSubpixelOffset(int x, int y);
void CreateTimestampedLogFile(void);
void ShowVideoModeDebugText(void);

// System check functions
DWORD GetFreeDiskSpaceMB(LPCSTR lpPath);
BOOL  EnumerateDriveTypes(void);
int   ShowMessageBox(HWND hWnd, LPCSTR lpMsg, LPCSTR lpCaption, UINT uType);

// Installation
BOOL IsGameInstalled(void);
BOOL LoadInstallationConfiguration(BYTE* pInstallPath);

// Display config
int   CheckVideoCapabilities(void);
INT_PTR EnumerateAndSelectDisplayMode(void);
int   GetDisplayModeCount(void);
void  GetDisplayModeRect(int modeIndex, DWORD* pRect);

// Cleanup
void CleanupSharedMemory(void);
void UpdateGameStatus(void);
void CleanupVideoConfigAndSaveAllSettings(void);
void DestroyAllSoundBanks(void);
void CleanupAsyncTasks(void);

// Sound system helpers
void SaveGameSettingsToRegistry(void);
void CVideoSystem_Cleanup(void* ptr);
void ProbeWaveOutDevicesAndCacheVolume(void);
void StartSoundSystemAsync(HWND hwnd);
void RestoreWaveOutVolume(void);

// Key handling
void OnKeyDown(HWND hwnd, WPARAM wparam);

// Window proc
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

// Marni Constructor
void* CMarniDirect3D_Constructor(void* self, HWND hWnd, int width, int height, int modeID, int adapterID);

// Memory
void* operator_new(size_t size);
void  operator_delete(void* ptr);

// Title state functions
void set_display_resolution(int w, int h, int mode);
void display_image(int slot, void* buffer, int width, int height);
void title_setup_texture_pages(int slot, int mode);
void empty_00470960(int slot);
int  check_save_files_exist(void);
int  create_texture_page(void* data, int mode);
void destroy_texture_page(int handle);
void UpdateTitleTextSprite(unsigned char brightness, unsigned char selectionId);
void display_texture(TextureDesc* texture, unsigned short depth, int slot, int pageCount);
int  AddTintSprite(TextureDesc* texture, unsigned short brightness);
void title_exit_loop(void);
void fade_update(void);
void play_sfx(int bank, int soundId);
int  read_sidewinder_pad(void);

void init_title_screen(void);
void title_select_sfx(void);
void empty_0040abb0(void* ptr, int a, int b, int c);
void set_title_render_param(int value);
void cleanup_texture_slot(int slot);
void empty_00497c10(int value);
void update_title_options(void);
void game_start(void);
void characterSelectionScreen(void);
void LoadSaveGameState(int mode, int flags, int useInkRibbon, int exitMode, int cutsceneReset);
void cut_set(void);
void Room_LoadCameraSprites(void);
void Room_SetupCamera(void);
void load_room_masks(int param_1);
int  Room_ApplySpriteFlags(void);
void FUN_004403c0(int param_1, int param_2, int param_3, int* param_4);
int  MatrixToCamera(MATRIX* m);
void InitInputKeyBindings(void);
unsigned int Flg_ck(int baseAddr, unsigned int bitIndex);
void use_room_action_item(void);
void rearrange_item_slots(void);
int  FileWrite(const char* name, void* buf, int len);
int  ReadSaveFile(const char* path, void* buffer);
void EnsureDirectoryExists(const char* path);
int  GetSaveLocationIndex(int stageId, int roomId);
void DrawSaveCursor(short x, short y, int mode);

// Save/Load
extern int           g_savesCounter;                   // 0x004d467c

// PS1 GTE trig lookups
int GteSin(int angle);
int GteCos(int angle);

// PS1 GTE matrix functions
MATRIX* RotMatrix(SVECTOR* r, MATRIX* m);
void MatrixSetTranslation(MATRIX* m, int* translation);
void SetGlobalScaledRotationMatrix(MATRIX* m);
void GetMatrixTranslation(MATRIX* m);

// PS1 sprite primitive helpers
void GteSpriteHeaderInit(SVECTOR* header);
int  GteClutBuild(int param_1, short param_2);
int  GteTpageBuild(unsigned short param_1, unsigned short param_2, int param_3, int param_4);

// Player animation function declarations
void player_anim_attack_recoil(void);
void player_anim_simple_recovery(void);
void player_anim_multi_attack(void);
void player_anim_dispatch_4c2ac8(void);
void player_anim_crawling(void);
void player_anim_set_attacked_flag(void);
void player_anim_dispatch_4ba360(void);
void player_anim_dispatch_4c10b0(void);
void player_anim_poison_death(void);
void player_anim_death_billboard(void);
void player_anim_limb_physics(void);
void player_anim_enemy_interact(void);
void player_anim_death_alt(void);
void player_anim_dispatch_4b1a90(void);

// Animation helper functions
unsigned int Joint_move(char reverse, unsigned int animHeader, unsigned int animBase, short blendStep);
void  entity_apply_anim_vertex(Entity* entity, unsigned int emdScratch1, unsigned int emdScratch2);
void  entity_extract_anim_vertex(Entity* entity, unsigned int emdScratch1, unsigned int emdScratch2, char reverse);
void  PlayEntitySnd(unsigned char soundType);
unsigned char Effect_CreateBillboard(unsigned char type, unsigned char depthGroup, short yaw, void* spriteInfo, void* pos, char lightFactor);
void  JointApplyColorTint(JointStruct* joint, int param2, int param3, void* data);
void  JointSetColorTint(int modelObjPtr, unsigned int packedColor);
MATRIX* RotMatrixY(int angle, MATRIX* m);
void  ApplyMatrix(MATRIX* m, SVECTOR* src, SVECTOR* dst);
void  ApplyMatrixSV(MATRIX* m, SVECTOR* src, SVECTOR* dst);
void  fp_lerp(SVECTOR* current, SVECTOR* target, int weightCurrent, int weightTarget, SVECTOR* out);
void  BillboardSetColor(void* quad, int unused1, int unused2, unsigned int color);
void  BillboardAdjSize(void* quad, short halfW, short halfH);
void  BillboardSetSize(void* quad, short halfW, short halfH);
short GetPlayerInputMasked(void);
void  EntityUpdateWeaponJoint(int weaponIdx);
MATRIX* MulMatrixInPlace(MATRIX* m0, MATRIX* m1);
void  Add_speedXZ(int angleOffset);
void  set_player_animations_functions(void);
void  ApplyLVAndMul0Matrix(void* a, void* b, void* c);

MATRIX* MulMatrix0(MATRIX* m0, MATRIX* m1, MATRIX* m2);
VECTOR* ApplyMatrixLV(MATRIX* m, VECTOR* v0, VECTOR* v1);
MATRIX* CompMatrix(MATRIX* m0, MATRIX* m1, MATRIX* m2);
void    ApplyLVAndMulMatrix(MATRIX* m0, MATRIX* m1);
void    ApplyLVAndMul0Matrix(void* m0, void* m1, void* mOut);

// Room initialization
void init_room(void);
void room_set(void);
void LoadRoomRdt(void);
void update_room_bgm(void);
void LoadHeldItemsImages(void* buf);
void load_room_bg(void);
void load_room_bg_image(void);
void load_room_bg_masks(void);
int  unpack_pakfile_(void* src, void* dst);

// Game state / character setup
void SetupCharacterData(void);
void display_game_loading_message(void);
void LoadAttractModePlayerData(void);
void empty_0047eb90(int param);
void load_shoot_direction_data(void);
void load_room_sfx(unsigned char soundTableIndex);
void load_character_sfx(unsigned char charId);
void LoadItemImage(int imageType, int index, void* buf);
void PrintFormattedText(short x, short y, unsigned char color, const unsigned char* data);

// Room sprite visibility control
void RoomSpr_SetActive(char id);    // 0x00476170
void RoomSpr_SetInactive(char id);  // 0x00476130

// Animation buffer globals
extern void*  ANIMATION_BUFFER;                       // 0x00be6440
extern DWORD  ANIMATION_BUFFER_END;                   // 0x00be6444
extern BYTE   g_shootDirEspBuffer[0x10000];           // 0x00c14dc0
extern char   FILE_PATH[260];

void  Play3DSnd(int bank, int soundId, int vol, int pos);
void  PlayEntitySnd(unsigned char soundType);
unsigned short LookupFootstepZone(short posX, short posZ);
void  Snd_em(unsigned char em_snd_id);
void  Calc3DSndPan(VECTOR* pos);
int   CalcPanVolume(int panL, int panR);
int   SquareRoot0(int val);
int   GetAngleQuadrantValue(int slope);
unsigned short CalculateAngleBetweenPointsXZ(int pos1_x, int pos1_z, int pos2_x, int pos2_z);

// Additional sound globals
extern int           g_snd_bank_00ac99d8;
extern unsigned char g_snd_slot_00ac99dd;
extern int           g_snd_bank_00ac99e0;
extern unsigned char g_snd_slot_00ac99e5;
extern int           g_bgmDefaultVolume;
extern unsigned char g_prevBgmState;
extern unsigned char g_targetBgmState;
extern unsigned char* g_RoomBgmStatePtr;
extern void*         g_bgmDataTable;
extern void*         g_StageDataPtr;
extern const unsigned short* g_StageVoiceOffsetTable[];
extern const char*   g_StageVoiceNamesTable[];
extern int           g_roomSfxVolume;
extern int           g_charSfxVolume;

// 3D sound panning globals (0x00ac98c8 / 0x00ac98cc)
extern unsigned short g_snd_pan_left;                  // 0x00ac98c8 - 3D sound left channel pan
extern unsigned short g_snd_pan_right;                 // 0x00ac98cc - 3D sound right channel pan

// Camera view matrix used for 3D sound Y position (0x00d22680)
extern MATRIX        MATRIX_00d22680;                   // 0x00d22680

// Game state
extern void*         g_RdtLoadDataBackup;
extern int           end_game_status;


extern const unsigned char g_StageRoomFlagOffset[5]; // 0x004d31e0 - per-stage room flag base offsets
extern unsigned char g_ItemSlotsIndexes;
extern DWORD         g_ItemSlotsBitmask;
extern unsigned char g_defaultItemSlot;
extern unsigned char DAT_00be41e1;
extern unsigned char DAT_00be9614;
extern const unsigned char g_ItemImageLookupTable[0x50 * 4];

// Room state (reset by room_state_reset / room_set)
extern DWORD         g_SysFlags[2];                 // 0x00be41c8 - SCD flag bank 4 (system flags, case 4 in cmd_bit_test)
// DAT_00be9830 is now a macro to g_BioCard.dat_0x210 (see Items.h)

// Room item event table (24 entries x 12 bytes, used by item/door commands)
extern unsigned char g_RoomItemEventTable[288];   // 0x00d91aa0
extern void*         g_RoomItemEventHead;         // 0x00d91bc0

// Lab slides state (reset by lab_slides_reset)
extern unsigned char g_labSlidesFuncIndex;        // 0x00d22790
extern unsigned char g_labSlidesAnimState;        // 0x00d22791 - animation phase (0-3)
extern int           g_labSlidesScrollX;          // 0x00d22794 - slide scroll X position
extern unsigned int  g_labSlidesScrollY;          // 0x00d22798
extern unsigned char g_labSlidesSlideIndex;       // 0x00d227a0 - current slide frame
extern unsigned char g_labSlidesLoopDone;         // 0x00d227a1 - loop completion flag
extern unsigned char g_labSlidesMsgId;            // 0x00d227a2 - current slide message id
extern unsigned char g_labSlidesCountdown;        // 0x00d227a3 - delay countdown timer
extern int           g_labSlidesState;            // 0x007d9120

// Room event index (SCD event pointer for current room entity)
extern void*         g_room_event_index;          // 0x00d226a4

// Effect system (billboard/sprite effect pool)
extern Effect        g_effectPool[MAX_EFFECTS];           // 0x00be41e4 - 64 slots x 0x84 bytes
extern unsigned char g_freeEffectSlots;                   // 0x00bf07ee - free slot counter (starts at 64)
extern DWORD         g_effectSpriteInfo[50];              // 0x00bf0a54 - per-type sprite header pointers
extern DWORD         g_effectAnimData[50];                // 0x00bf0b1c - per-type animation data pointers

// Effect sprite texture management state
extern unsigned char DAT_00bf0a38;                  // 0x00bf0a38 - effect tex Y position
extern short         DAT_00bf0a3c;                  // 0x00bf0a3c - effect tex X offset
extern unsigned short DAT_00bf0a3e;                 // 0x00bf0a3e - effect tex U offset
extern short         DAT_00bf0a40;                  // 0x00bf0a40 - effect tex page row
extern unsigned short DAT_00bf0a42;                 // 0x00bf0a42 - effect tex page col
extern unsigned char g_abEffSpriteIndexTable[16];              // 0x00bf0a44 - effect sprite index table (first 8 = shoot dir, second 8 = room eff)

// Effect sprite per-slot image data pointers (computed from RDT by InitRoomEffSprite)
extern int           DAT_00ac9cd0[8];               // 0x00ac9cd0 - effect sprite image pointers

// Entity joint animation copy base (set by SetupEntityJointAnimation)
extern int           DAT_00be0e00;                  // 0x00be0e00

// Effect sprite stage/room cache (set by load_effect_sprites)
extern unsigned int  STAGE_ID_00ac9cf0;             // 0x00ac9cf0
extern unsigned int  ROOM_ID_00ac9cf4;              // 0x00ac9cf4

// Display image buffer (TIM slide data loaded here)
// In original binary at 0x00cf22ac (g_BackgroundImageBuffer + 0x14)
extern BYTE          g_displayImageBuffer[0x30000];

// Background loading mode flag (0 = per-camera load/display, non-zero = cache all cameras)
extern int           g_bgCacheMode;                    // 0x004d46b4

// Hex character lookup table for path construction
extern char          g_hexCharTable[17];               // 0x004c2060 "0123456789abcdef"

// Path template for room background PAK files (mutated at runtime)
extern char          g_bgPathTemplate[28];             // 0x004c2078 ".\usa\stageS\rcSRRC.pak"

// Camera hex char (stored after path template, set by load_room_bg)
extern char          DAT_004c2090;                     // 0x004c2090

// Per-camera background load buffer (one PAK file worth)
extern BYTE          g_bgPakLoadBuffer[0x20000];       // 0x00aea0d0

// Cached all-camera background buffer
extern BYTE          g_bgCacheBuffer[0x20000];         // 0x00b0a0d0

// Per-camera offset into g_bgCacheBuffer (index 0 unused, 1..N = cameras)
extern int           g_bgCameraOffsets[16];            // 0x00aea08c (base at +4)

// Per-camera mask offset into g_bgMaskDataBuffer
extern int           g_bgMaskOffsets[16];              // 0x00ae9e80

// Mask data buffer (PAK mask data loaded here)
extern BYTE          g_bgMaskDataBuffer[0x20000];      // 0x00ac9e80

// Path template for mask PAK files (mutated at runtime)
extern char          g_maskPathTemplate[28];           // 0x004c3bc8 "./usa/objspr/osp0SRRC.pak"

// LZW decompression state (unpack_pakfile_)
extern unsigned int  g_pakDecompInputPos;              // 0x00d2b0a4
extern unsigned int  g_pakDecompBitMask;               // 0x00d91a64
extern unsigned int  g_pakDecompCurByte;               // 0x00d227c4
extern unsigned int  g_pakDecompCodeSize;              // 0x00d2b0a8
extern unsigned int  g_pakDecompNextCode;              // 0x00d227c8
extern unsigned int  g_pakDecompMaxCode;               // 0x00d2b0a0

// LZW dictionary (12 bytes per entry: 4 unused + 4 prefix + 4 char)
// Max ~35000 entries from 0x00d2b0b0 to 0x00d91a60
extern int           g_pakDictPrefix[8192];            // 0x00d2b0b4 (offset +4 per 12-byte entry)
extern char          g_pakDictChar[8192];              // 0x00d2b0b8 (offset +8 per 12-byte entry)

// LZW string output buffer (for building decoded strings)
extern char          g_pakStringBuf[512];              // 0x00d227d0

// Character switch backup globals (used by room_set when switching between Jill/Chris and Rebecca)
extern short          HEALTH_BKP;                      // 0x008f87b0 - backup of player health
extern unsigned short HEALTH_STATUS_BKP;               // 0x008f87b4 - backup of health status flags

// Active character item slots pointer (points to g_ItemsSlots or g_RebeccaItemSlots)
extern void*          g_ItemSlotsPointer;              // 0x00d22768

// Room event flags (Flg_on/Flg_ck bitfield, SCD flag bank 3)

// Sound bank pointer tables (populated by room_set from RDT vab_sound_file data)
extern void*          g_itemboxes_covers_table[8];     // 0x00d226b0
extern void*          g_desks_pointers_table[8];       // 0x00d21360

// Enemy model loading state (used by room_set and cmd_omodel_set)
extern int            g_omodelCount;                   // 0x00ae9ef4 - object model count (cmd_omodel_set)
extern unsigned char  g_LastEnemyModelId;              // 0x00bebcc9 - last enemy model ID (model reuse cache)
extern int            g_ItemModelCount;                // 0x00ae9eec - item/obstacle model count (cmd_item_model_set)

// SCD script pointer for room initialization (set by LoadRoomRdt from RDT initialization_scd)
extern void*          g_RoomInitScd;                   // 0x00d213bc

// Current RDT data type pointer (set to RDT cam_switch_zones by room_set)
extern void*          g_CurrentRdtDataTypePtr;         // 0x00bebccc

// Data pointer saved for stage 2 room 3 (pointer into load buffer at room init)
extern void*          DAT_00d213c0;                    // 0x00d213c0

// Room sprite entries table (populated from RDT by FUN_004757c0)
// Each entry is 0x24 bytes: TextureDesc (0x20) + active flag + id + posData
// Entry count is g_RdtPointer->sprites_count
extern RoomSprEntry   g_RoomSprEntries[128];           // 0x00d213d0

// SCD event execution context table (8 entries x 0x34 bytes each)
// Each entry tracks a running room event script executed by room_events_check.
extern ScdEventEntry  g_ScdEventTable[8];               // 0x00bf084c

// SCD event system globals
extern ScdEventEntry* g_pScdEventCurrent;               // 0x00bf0848 - current event being processed
extern unsigned char* g_ScdOpcodes;                     // 0x00bf0800 - current SCD opcode pointer
extern unsigned int*  g_CmdOpcodesPointer;              // 0x00bf0804 - SCD call stack pointer
extern unsigned char  g_ScriptContinueFlag;             // 0x00bf07fa - SCD call depth counter
extern unsigned char* g_EvtScripts;                     // 0x00d213b4 - event script table pointer
extern unsigned char* g_RoomScdOpcodes;                  // 0x00d213b8 - room SCD opcodes pointer
extern void*          script_command_funcs_table[256];   // 0x004c1110 - SCD command dispatch table

// SCD flag bank 9 (misc flags)
extern unsigned int   DAT_00d213a0[2];                  // 0x00d213a0

// Player entity pointer alias (used by decompiler-generated names)
#define g_playerEntityPointer  g_playerEntity

// Misc globals used by SCD command functions
extern BOOL          g_bFullScreenFlag_68;
extern int           g_FmvCharacterId;

// Desks/locks flags (SCD flag bank 2)
extern unsigned int   g_desks_locks_flags[16];          // 0x00be9874

// Player entity fields used by cmd functions
extern unsigned short DAT_00d211c4;                     // 0x00d211c4
extern unsigned short DAT_00d21350;                     // 0x00d21350
extern unsigned short DAT_00d2276c;                     // 0x00d2276c
extern unsigned int   DAT_00d22770;                     // 0x00d22770

// Sound system BGM state
extern unsigned char  DAT_00bf07ef;                     // 0x00bf07ef
extern int            DAT_00bf07f0;                     // 0x00bf07f0

// Room BGM state table (separate from g_roomBgmState which is per-stage)
extern unsigned char  g_abRoomBgmState[224];            // 0x00ac98e8

// Screen effect parameter storage
extern unsigned int   DAT_00ac98e0[4];                  // 0x00ac98e0
extern unsigned int   DAT_00ac98e4[4];                  // 0x00ac98e4

// Special room lighting globals (accessed by cmd_0x1c)
// Note: These are defined in MainLoop.cpp as static - need to make extern
extern int            g_SpecialR1;                       // 0x00be961d
extern int            g_SpecialG1;                       // 0x00be961e
extern int            g_SpecialB1;                       // 0x00be961f

// TMD model caching state (used by cmd_item_model_set / cmd_omodel_set)
extern int*           DAT_00bca0d0;                     // 0x00bca0d0
extern int*           DAT_00bca0d4;                     // 0x00bca0d4
extern unsigned char  DAT_008e1c78;                     // 0x008e1c78
extern unsigned char  DAT_008e1c70;                     // 0x008e1c70
extern unsigned char  DAT_008e1c7c;                     // 0x008e1c7c
extern unsigned char  DAT_008e1c74;                     // 0x008e1c74
extern DWORD          DAT_004d2bdc;                     // 0x004d2bdc
extern int            DAT_004d2be0;                     // 0x004d2be0
// DAT_004d6444 already declared at line 365

// Bullet effect parent sprite info pointer (set by cmd_bullet_0x3d)
extern int            DAT_00bf0a34;                     // 0x00bf0a34
