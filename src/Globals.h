#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <cstdio>
#include <d3d11.h>
#include "marni/MarniBits.h"
#include "marni/Marni3DObject.h"
#include "marni/MarniInput.h"

struct TextureDesc;

// PS1 GTE matrix type (0x00ac93b0 layout, 32 bytes)
struct MATRIX {
    short m[3][3];  // 0x00: Rotation matrix (9 × short = 18 bytes)
    short _pad;     // 0x12: Padding for int alignment
    int t[3];       // 0x14: Translation vector (3 × int = 12 bytes)
};

// PS1 GTE vector type
struct SVECTOR {
    short x, y, z;
    short pad;
};

#define REGKEY_PATH "Software\\CAPCOM\\RESIDENT EVIL"
#define MAX_DISPLAY_MODES 100
#define MAX_DRIVES 26

// PS1 digital controller button bit constants (used by g_RawPadPressed / g_PlayerPadPressed)
#define PAD_SELECT      0x0001
#define PAD_L3          0x0002
#define PAD_R3          0x0004
#define PAD_START       0x0008
#define PAD_UP          0x0010
#define PAD_RIGHT       0x0020
#define PAD_DOWN        0x0040
#define PAD_LEFT        0x0080
#define PAD_L2          0x0100
#define PAD_R2          0x0200
#define PAD_L1          0x0400
#define PAD_R1          0x0800
#define PAD_TRIANGLE    0x1000
#define PAD_CIRCLE      0x2000
#define PAD_CROSS       0x4000
#define PAD_SQUARE      0x8000

// Composite masks for common groups
#define PAD_ANY         0xFFFF      // any button
#define PAD_DPAD        (PAD_UP|PAD_DOWN|PAD_LEFT|PAD_RIGHT)       // 0x00F0
#define PAD_SHOULDER    (PAD_L1|PAD_L2|PAD_R1|PAD_R2)               // 0x0F00
#define PAD_FACE        (PAD_TRIANGLE|PAD_CIRCLE|PAD_CROSS|PAD_SQUARE) // 0xF000
#define PAD_MENU_CONFIRM PAD_CROSS   // confirm/select in menus
#define PAD_MENU_BACK   PAD_SQUARE   // cancel/back in menus
#define PAD_MENU_UP     PAD_TRIANGLE // navigate up in menus
#define PAD_MENU_DOWN   PAD_CROSS    // navigate down in menus
#define PAD_CONFIRM     (PAD_CROSS|PAD_START)  // start/confirm (Enter/Space maps to both)

// Title screen: "any button except menu navigation/face buttons"
// Excludes L2, TRIANGLE, CIRCLE, CROSS, SQUARE (menu nav + face buttons)
#define PAD_TITLE_ANY   (PAD_SELECT|PAD_L3|PAD_R3|PAD_START|PAD_UP|PAD_RIGHT|PAD_DOWN|PAD_LEFT|PAD_R2|PAD_L1|PAD_R1)  // 0x0EFF

struct DisplayModeInfo {
    DWORD dwWidth;         // 0x00 - Screen width
    DWORD dwHeight;        // 0x04 - Screen height
    DWORD dwBPP;           // 0x08 - Bits per pixel
    DWORD dwRefreshRate;   // 0x0C - Refresh rate (Hz)
    DWORD dwFlags;         // 0x10 - Mode flags
};

struct RectDrawDesc {
    unsigned int textureId;  // 0x00 - Texture ID
    short x;                 // 0x04 - X position
    short y;                 // 0x06 - Y position
    short w;                 // 0x08 - Width
    short h;                 // 0x0a - Height
    unsigned char r;         // 0x0c - Red
    unsigned char g;         // 0x0d - Green
    unsigned char b;         // 0x1e - Blue
};

struct TaskControlBlock {
    short state;           // 0x00 - State/flags
    short sleepCounter;    // 0x02 - Sleep counter
    BYTE  reserved[0x78];
};

struct D3DRendererInfo {
    char name[256];
    DWORD flags;
};

// --- Global Variables ---

// Window system (0x007e0xxx, 0x00d91xxx range)
extern HWND          g_hWnd;                           // 0x00bcb2c0
extern HINSTANCE     g_hInstance;                      // 0x00bcb2c4
extern BOOL          g_bIsSoftwareRendering;           // 0x004bcb2c (?)
extern BOOL          g_isGameCursorHiddenFlag;         // 0x00be0e29 (?)
extern BOOL          g_bHasFinalizedSettings;          // 0x004bcb78
extern HANDLE        g_hMutex;                         // 0x00bcb2c8

// Display / Adapter (0x007d9xxx range)
extern DWORD         g_dwSelectedDisplayAdapterID;     // 0x007d9148
extern DWORD         g_dwSelectedDisplayModeID;        // 0x007d914c
extern DWORD         g_dwScreenWidth;                  // 0x007d9150
extern DWORD         g_dwScreenHeight;                 // 0x007d9154
extern BOOL          g_bFullScreen;                    // 0x007d9158
extern int           g_dwBitDepth;                     // 0x004d642c
extern DWORD         g_dwPlayCount;                    // 0x004d6430
extern DWORD         g_dwClearCount;                   // 0x004d6434
extern DWORD         g_GPU_VENDOR_ID;                  // 0x004bcb64

// Display mode storage (0x007d8f28)
extern DisplayModeInfo g_DisplayModeBuffer[MAX_DISPLAY_MODES];
extern int           g_NumDisplayModes;                // 0x007d8f24

// D3D Renderer info (0x007e0e10)
extern D3DRendererInfo g_D3DRenderers[8];
extern int           g_NumD3DRenderersAvailable;       // 0x007e0e08

extern int           g_SelectedPlayerID;               // 0x008f879c

// Drive types (0x008f87c4)
extern UINT          g_DriveTypes[MAX_DRIVES];
extern char          g_DriveLetterBuffer[256];

// Installation path
extern char          g_szInstallPath[MAX_PATH];        // 0x00d91bd0
extern char          g_szCreateDir[260];

// Registry loaded data
extern BYTE          g_keyBindingData[32];
extern BYTE          g_joystickBindingData[128];
extern BOOL          g_bIsSideWinderConnected;         // 0x004d1f50
extern BYTE          g_InstallFlagData;

// Shared memory (for inter-process communication with setup)
extern HANDLE        g_hFileMapping;                   // DAT_007dfd20 (= DAT_004bcca4 for SW render path)
extern BYTE*         g_pSharedMemory;                  // sharedMemoryPtr

// Window rect for drawing
extern RectDrawDesc  g_window_rect;                    // 0x00d227b0

// Marni System objects
extern void*         g_pMarniDirect3D;                 // 0x00ac4028
extern MasterInputState* g_pMasterInputState;          // input state pointer

// Main state flags
extern DWORD         g_main_state_flags;               // 0x00be41c0 (?)

extern int           g_playerHealth;                   // 0x00be636c (?)

// Game state
extern int           DAT_00d91bc8;                     // 0x00d91bc8 (?)
extern int           g_currentFMVID;                   // FMV_ID
extern int           g_CurrentFMVID;                   // 0x00d91bcc (?)

extern unsigned char g_stageId;                        // 0x00be9820
extern unsigned char g_roomId;                         // 0x00be9821
extern unsigned char g_roomCameraId;                   // 0x00be9822

// Screen pos
extern int           g_ScreenOffsetX;                  // 0x00ac3ff8
extern int           g_ScreenOffsetY;                  // 0x00ac3ffc
extern signed char   g_ScreenShakeOffsetX;             // 0x00bca0d8
extern signed char   g_ScreenShakeOffsetY;             // 0x00bca0d9

// fading
extern short         g_fading_state;                   // 0x00be9834
extern short         g_fading_counter;                 // 0x00bebcca
extern unsigned char g_fade_type_id;                   // 0x00bf0a2f
extern BYTE          g_bGameActive;                    // 0x00be41dc

// Task system globals
extern DWORD         g_StackPointer;                   // _g_StackPointer 0x007e0cc8
extern TaskControlBlock g_TasksTable[3];               // 0x00d1fde4
extern void*         g_CurrentTask;                     // 0x00bf09ec
extern DWORD         g_TasksESP[3];                    // 0x00d91a70
extern DWORD         g_TasksEIP[3];                    // 0x00d91a80
extern DWORD         g_CurrentTaskID;                  // 0x00d91a7c
extern void*         g_CurrentTaskPtr;                 // 0x00d91a68
extern DWORD         g_SchedulerESP;                   // 0x00d91a8c
extern DWORD         g_SchedulerRunningFlag;           // 0x004ba0b8
extern void*         g_AsyncRpcCallback;               // 0x00d91a90

// Input state
extern DWORD g_lastScanCodeOrMsgID; // 0x00bcb2e0 - last keyboard scan code or dialog message ID
extern DWORD g_InputFlags; // 0x00bcb2e4
extern DWORD g_RawPadPressed; // 0x00be05b4 - raw pad state (edge-detected WORD in original)
extern DWORD g_PlayerPadPressed; // 0x00bf0a08 (edge-detected: pressed this frame only)
extern DWORD g_PlayerPadHeld; // 0x00bf0a10 (currently held buttons)
extern DWORD g_button_pressed_id; // 0x00bf0a0c
extern DWORD g_PlayerPadHeldPrev; // 0x004bae30
extern WORD g_RawPadState; // 0x00bf0a12 (raw pad state snapshot)
extern DWORD g_PadRawP2; // SideWinder raw pad state
extern BOOL g_DisablePad; // 0x004bcb3c

// Menu / dialog flags
extern int           g_menu_choice_id;                 // 0x00be0e28 (?)
extern BOOL          g_displayReturnToTitleScreen_Flag;// 0x004bcb50
extern BOOL          g_displayExitGameScreen_flag;     // 0x004bcb54
extern int           g_demoTimer;                      // 0x004bcb5c (?)

// Sound system
extern int           g_SndFadeType;                    // 0x00bf0a2d
extern int           g_SndRampFramesLeft;              // 0x00ac9908
extern int           g_BgmSoundBank;                   // BGM sound bank ID
extern int           g_SfxBanks[64];                   // SFX bank handles + metadata
extern int           g_RoomSfxBanks[64];               // Room SFX bank handles + metadata
extern int           g_CharacterSfxBanks[64];          // Character SFX bank handles + metadata
extern int           g_emSndBanks[64];                 // Enemy SFX bank handles + metadata
extern int           g_SndBank[64];                    // Generic sound bank handles + metadata
extern int           g_SfxVolume;                      // Master SFX volume
extern char          g_BgmPaused;                      // BGM paused flag
extern int           g_SndRampDirection;               // Sound ramp direction
extern int           g_SndRampCurrentVolume;           // Current ramp volume
extern int           g_SndRampBankIndex;               // Bank index for ramp
extern int           g_SndDistSteps;                   // Sound distance steps
extern void*         g_SoundManager;                   // Sound manager object pointer (legacy, for non-class code)
extern class DirectSound* g_pDirectSound;               // DirectSound class instance
extern DWORD         g_CachedWaveOutVolume;            // Cached wave out volume
extern int           g_WaitForMusicTimer;              // Music wait timer
extern unsigned char g_BGM_STATE;                      // BGM state byte
extern HWND          g_MainWindowHandle;               // Main window handle for sound
extern int           g_setVolResult;                   // getSndStat result
extern int           g_CurBank;                        // Current bank for getSndStat
extern unsigned char g_snd_slot_00ac99d5;              // Sound slot index
extern int           g_SoundPanVol;                    // Sound pan/volume parameter
extern int           g_SndPanSet_result;               // Pan set result flag
extern char*         g_wavName;                        // WAV filename pointer for async load
extern int           g_sndload_bank_index;             // Loaded bank index result
extern short         g_CurSlot;                        // Current sound slot
extern int           g_SndFadeStepTbl[64];              // Per-bank fade step counters

// Game init
extern int           init_game_flag;                   // 0x00bcb2e8

// Timing
extern DWORD         g_dwSystemTimer1;                 // 0x007e0df4 (?)
extern DWORD         g_dwGameTimer1;                   // 0x007e0df8 (?)
extern DWORD         g_GameInitTime;                   // 0x004d46c4
extern DWORD         g_gameTimerSnapshot;              // 0x004d46cc
extern DWORD         Game_timer;                       // 0x004d46d0
extern DWORD         g_LastFrameTime_ms;               // 0x004d45fc

// Frame rate governor globals (0x004973d0)
extern int           g_frameTimeIndex;                // 0x004d45f8 - ring buffer index
extern int           g_frameTimeBuffer[4];            // 0x00ac4000 - last 4 frame delta times (ms)
extern int           g_frameTimeAccumulator;          // 0x004d45f4 - running time budget accumulator
extern int           g_frameTargetTime;               // 0x004d45ec - computed target frame interval
extern int           g_ScreenAccessReady;             // 0x004d4658 - screen ready for present
extern int           g_ScreenAccessCountdown;         // 0x004d4684 - StMask countdown (frames until re-enable)
extern int           g_RenderAccessReady;             // 0x004d4688 - render state ready
extern int           g_MarniScreenReady;              // 0x00497340 - screen present enable (marni field_0x2ec)
extern DWORD         g_MarniScreenColor;              // 0x00497360 - packed RGB debug color override

// MCIVideo 
extern int           g_mciVideoDeviceID;               // 0x004bcb44
extern BOOL          g_bMCIVideoEvent;                 // 0x004bcb48

// Misc flags
extern BOOL          g_bIsPaused;                      // DAT_004bcb2c
extern BOOL          g_bWindowActive;                  // DAT_004bcb30
extern BOOL          g_bQuitFlag;                      // DAT_004bcb40
extern BOOL          g_bUseFrameSkip;             // DAT_004bcb48
extern BOOL          g_bFrameSkipDetected;             // DAT_004d46dc
extern int           g_ScreenAccessCheck;              // 0x004d2290
extern int           g_RenderAccessCheck;              // DAT_004d468c

// ============================================================================
// TexturePrintState - packed struct matching original Ghidra memory layout
// Used by AddTintSprite / AddSprite for offset-based field access (0x00be116c area)
// ============================================================================
#pragma pack(push, 1)
struct TexturePrintState {
    DWORD  flags;          // +0x00  _texture_buffer (flags for BuildSpriteRenderFlags)
    short  printPosX;      // +0x04  TEXTURE_PRINT_POS_X
    short  printPosY;      // +0x06  TEXTURE_PRINT_POS_Y
    short  vramWidth;      // +0x08  VRAM area width (8 for font)
    short  vramHeight;     // +0x0A  VRAM area height (14 for font)
    short  textureDepth;   // +0x0C  TEXTURE_DEPTH (0x1E = font bank)
    byte   vramAreaX;      // +0x0E  VRAM_AREA_X
    byte   vramAreaY;      // +0x0F  VRAM_AREA_Y
    short  printTintA;     // +0x10  _DAT_00be1170 / PrintTintA (0x100)
    short  textureDepth2;  // +0x12  TEXTURE_DEPTH dup (variant lookup)
    byte   tintR;          // +0x14  DAT_00be1174 / PrintTintR
    byte   tintG;          // +0x15  DAT_00be1175 / PrintTintG
    byte   tintB;          // +0x16  DAT_00be1176 / PrintTintB
    byte   pad_17;         // +0x17
    short  tintMode;       // +0x18  _DAT_00be1178 (x0 anchor)
    short  tintFlagA;      // +0x1A  _DAT_00be117a (y0 anchor, subtracted from sprite y)
    short  pad_1C;         // +0x1C
    short  pad_1E;         // +0x1E
    short  tintFlagB;      // +0x20  _DAT_00be1180
};
#pragma pack(pop)
static_assert(sizeof(TexturePrintState) == 0x22, "TexturePrintState size mismatch");

// Global texture print state instance (address 0x00be116c area)
extern TexturePrintState g_texPrintState;

// Macros to maintain backward compatibility with existing code
#define g_TextureBuffer     g_texPrintState.flags
#define g_TexturePrintX     g_texPrintState.printPosX
#define g_TexturePrintY     g_texPrintState.printPosY
#define g_TextureVramX      g_texPrintState.vramAreaX
#define g_TextureVramY      g_texPrintState.vramAreaY
#define g_TextureDepth      g_texPrintState.textureDepth
#define g_PrintTintR        g_texPrintState.tintR
#define g_PrintTintG        g_texPrintState.tintG
#define g_PrintTintB        g_texPrintState.tintB
#define g_PrintTintA        g_texPrintState.printTintA
#define g_PrintTintMode     g_texPrintState.tintMode
#define g_PrintTintFlagA    g_texPrintState.tintFlagA
#define g_PrintTintFlagB    g_texPrintState.tintFlagB

// Other print globals (not part of the packed struct)
extern char          PRINT_TEXT_BUFFER[256];           // 0x00be0e20
extern int           g_PrintClutTint;                  // CLUT tint index for text

// Image buffer for asset loading
extern void*         g_image_buffer;                   // _image_buffer
extern void*         g_ITEMS_IMAGES_BUFFER;            // ITEMS_IMAGES_BUFFER

// Asset loading globals
extern int           g_InstallFlagDataLoaded;          // DAT_004b3998
extern int           g_SpriteBufferFlag;               // DAT_004b399c
extern int           g_SpriteAsyncFlag;                // DAT_00d91bc8
extern int           g_FileOpenCount;                  // DAT_00d91bcc
extern int           g_FileRetryFlag;                  // DAT_004d4690
extern int           g_playingGameFlag;                // 0x004d4674
extern int           g_loadSaveStateFlag;              // 0x004d4678
extern int           g_demoIdleTimer1;                 // DAT_004c44d8
extern void*         g_loadDataDestPointer;            // 0x00bebcdc
extern unsigned char g_selectedFmvId;                  // 0x00bf07fb
extern void*         g_fmvDataPointer;                 // 0x00bf07fc
extern int           g_fmvPlayCount;                   // 0x004bae34

// Texture page table and video driver arrays
extern void*         g_TexturePageTable;               // vtable-style pointer
extern DWORD         g_TexturePageTable_DAT[256];      // populated by load_texture
extern ID3D11ShaderResourceView* g_TexturePageSRV[256]; // D3D11 SRV for each page table entry
extern int           g_TexturePageWidth[256];           // texture width per slot
extern int           g_TexturePageHeight[256];          // texture height per slot

// Player input data fields
extern int           g_PlayerDpadHeld;                 // 0x00be9a3c area
extern int           g_PlayerInputConfig_3c;           // DAT_00be9a3c
extern int           g_PlayerInputConfig_3e;           // DAT_00be9a3e
extern int           g_PlayerInputConfig_40;           // DAT_00be9a40
extern int           g_PlayerInputConfig_42;           // DAT_00be9a42
extern int           g_PlayerInputConfig_44;           // DAT_00be9a44
extern int           g_PlayerInputConfig_46;           // DAT_00be9a46
extern int           g_PlayerInputConfig_48;           // DAT_00be9a48
extern int           g_PlayerInputConfig_4a;           // DAT_00be9a4a
extern int           g_PlayerInputConfig_4c;           // DAT_00be9a4c
extern int           g_PlayerInputConfig_4e;           // DAT_00be9a4e
extern int           g_PlayerInputConfig_50;           // DAT_00be9a50
extern int           g_PlayerInputConfig_52;           // DAT_00be9a52
extern int           g_PlayerInputConfig_54;           // DAT_00be9a54
extern int           g_PlayerInputConfig_56;           // DAT_00be9a56
extern int           g_PlayerInputConfig_58;           // DAT_00be9a58
extern int           g_PlayerInputConfig_5a;           // DAT_00be9a5a

// Task data arrays initialized in init_and_start_game
extern DWORD         g_TaskDataArray_ba750[16];       // DAT_004ba750 area
extern DWORD         g_TaskDataArray_ba780[16];       // DAT_004ba780 area

// Image processing/status variables
extern unsigned char g_TextureBankID;                  // 0x00bebcc4
extern unsigned char g_TextureDepthByte;               // 0x00bebcc5
extern short         g_SpecialRoomLightR;              // DAT_00be9828
extern short         g_SpecialRoomLightState;          // DAT_00be9836
extern short         g_SpecialRoomLightDelta;          // DAT_00be9838

// Key binding data area
extern BYTE          g_KeyBindingVectors[32];          // DAT_00ac4030 area (0x004d4730 area)

// Marni video driver arrays
extern DWORD         g_VideoDriverArray_D0[64];        // DAT_008ed030 area
extern DWORD         g_VideoDriverArray_03c[64];       // DAT_008ed03c area
extern DWORD         g_VideoDriverArray_04c[64];       // DAT_008ed04c area
extern DWORD         g_VideoDriverArray_068[64];       // DAT_008ed068 area
extern DWORD         g_VideoDriverArray_06c[64];       // DAT_008ed06c area
extern DWORD         g_VideoDriverArray_4d0[1024];     // DAT_008ed4d0 area
extern DWORD         g_VideoDriverArray_4fa[256];      // DAT_008ed4fa area
extern DWORD         g_VideoDriverArray_4fc[256];      // DAT_008ed4fc area
extern DWORD         g_VideoDriverArray_500[256];      // DAT_008ed500 area
extern DWORD         g_VideoDriverArray_810[256];      // DAT_008ed810 area
extern DWORD         g_VideoDriverArray_814[256];      // DAT_008ed814 area
extern DWORD         g_VideoDriverArray_838[2048];     // DAT_008ed838 area
extern DWORD         g_VideoDriverArray_520[2048];     // DAT_008ed520 area (page flags)
extern short         g_VideoDriverArray_FA[256];       // DAT_008ed4fa area (byte access)

// Input key binding configuration
extern DWORD         g_KeyBindingConfig[32];           // DAT_004d4730 area
extern BYTE          g_MasterInputState[256];          // g_pMasterInputState points here

// Counters
extern int           g_numFramesRendered;              // DAT_004d4694
extern int           g_numFramesPresented;             // DAT_004d469c

// Screen info
extern BOOL          g_bAccessibilityAnimations;       // DAT_007d9144

// MCI notification  
extern BOOL          g_bMCINotifyEnabled;              // 0x004bcb58
extern BOOL          g_bMCINotifyFlag;                 // 0x004bcb5c

// Game loop
int main_loop(void);

// Frame counter
extern int           g_loopCounter;                    // local counter in main_loop

// Print text
void PrintText8x8(short x, short y, unsigned char color, char shadow);     // 0x00455420
void PrintText8x14(short x, short y, unsigned char color, char flags);     // 0x00455520
void PrintFormattedText(short x, short y, unsigned char color, unsigned char* data); // 0x00455190
void draw_rect(RectDrawDesc* rect, int blend, int flags);

// Task scheduler functions
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

// Input functions
void InputUpdate(void);
void PlayerPad_Update(void);

// Sound functions
void PauseSounds(void);
void ResumePausedSounds(void);
void UpdateSoundFadeState(void);
void UpdateSoundDecay(void);
void empty_0047b950(int param);
void UpdateMusicWaitState(void);
void sounds_reset(void);
void LoadSoundBank(int sound_bank_id, void* buffer);
void PauseGameSoundsAsync(void);
void ResumeGameSoundsAsync(void);
void DestroySoundManagerAsync(void);
int  getSndStat(int bank);

// Video functions
void UpdateVideoPlayback(void);
void ClearScreen(void);

// Marni System functions
BOOL IsGraphicsSystemReadyForOperation(void);
void InitializeMarniSystem(void);
void EnumerateDisplayModes(void);
void EnumerateD3DRenderers(void);
void InitJoysticks(void);
int  IsSideWinderPadConnected(void);
void CreateLights(int count);

// Screen effects
void ResetScreenPanning(void);
void SetScreenOffset(int x, int y);   // 0x00483600
void ApplyScreenShake(void);
void FUN_004557b0(void);
void FUN_00401020(int param);
void FUN_0045ab60(void);
void FUN_00497360(int r, int g, int b);
void FUN_00497340(int param);
void FUN_004973a0(int param);
void FUN_00470a90(void);              // 0x00470a90 - build title background sprites
void FrameRateGovernor(void);  // 0x004973d0 - frame timing + present governor
void FUN_0040a8f0(void* param);
void ResetSpriteQueue(void);     // 0x0046d990 - reset sprite queue after present
void OT_InsertPrimitive(void* prim, unsigned int depth); // 0x004402f0 - insert prim into ordering table
void AsyncCreateTexturePage(void);       // FUN_0046c130
void AsyncDestroyTexturePage(void);      // destroy_texture_page_callback
void AsyncCreateObject(void);            // FUN_0046c210
void AsyncDeleteObject(void);            // FUN_0046c260
int  FUN_0046c230(void* data);          // create execute buffer object
int  FUN_0046c280(int id);              // release execute buffer object
// FUN_00426600 -> CMarniPolyhedra::CopyFrom declared in marni/Marni3DObject.h
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

// Debug clear color (controlled by test tasks)
extern float         g_debugClearR;
extern float         g_debugClearG;
extern float         g_debugClearB;
extern int           g_debugTaskFrame;

// Sprite/clear color (set by setSomeColor)
extern float         g_color_r;               // 0x004c336c
extern float         g_color_g;               // 0x004c3370
extern float         g_color_b;               // 0x004c3374
extern float         g_spriteColorScale;       // 0x004af2ac

// Title screen display image SRV (shared with Rendering.cpp OT_InsertPrimitive)
extern ID3D11ShaderResourceView*  g_displayImageSRV;

// Title state globals
extern unsigned char g_titleLoopFlag;                  // 0x00d22777
extern unsigned char g_titleMode;                      // 0x00d22775
extern unsigned char g_titleOptionsFading;             // 0x00d22776
extern unsigned char g_titleSelectionId;               // 0x00d22774
extern short         g_titleDemoTime;                  // 0x00d22788 - demo countdown
extern short         g_titleTextureDepthData[8];       // 0x00d22778 - texture depth array
extern int           g_sceneRenderParam;               // 0x004d6300 - global render param (title, effects)
extern DWORD          g_titlePrimType;                  // 0x004d6398 - OT prim type (0x0040a8f0)
extern DWORD          g_primParam;                      // 0x004d63e0 - OT prim param (set to g_sceneRenderParam)
extern DWORD          g_primFlag2;                      // 0x004d63e4 - OT prim flag
extern int           g_displayWidth;                   // 0x00bf09f8
extern int           g_displayHeight;                  // 0x00bf09fc
extern int           g_displayMode;                    // 0x00bf09f4
extern int           g_DisplayImageWidth;              // 0x004c3364
extern int           g_DisplayImageHeight;             // 0x004c3368
extern int           g_titleTextureSlotId;             // 0x004c331c
extern int           g_texturePageMode;                // DAT_008ec9c0
extern int           g_texturePageHandle;              // global handle returned by create_texture_page
extern int           g_AsyncResult;                    // async operation result
extern int           g_ExecuteBufferHandle;            // DAT_008e1d58 - execute buffer handle
extern int           g_SpriteQueueCount;               // sprite queue count
extern int           g_OTIndex;                        // ordering table index
extern DWORD         g_SpriteQueueIndex;               // DAT_004c2d10 - sprite queue index
extern CMarniBits    g_MarniBitsWorkBuffer;            // DAT_008ed478 - work buffer for texture creation
extern DWORD         g_MarniBitsOutput;                // DAT_008ed470 - output from CreateTextureHandle
extern DWORD         g_ObjectWorkBuffer[64];            // DAT_008ec9c8 - work buffer for object creation

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
int  AddTintSprite(TextureDesc* texture, unsigned short brightness);  // 0x0046e0a0
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
void save_load_game_state(int, int, int, int, int);

// Object cleanup globals (Object_DeleteAll / ObjectCleanupCallback)
extern int           g_objectDeleteFlag;          // 0x004d2bfc
extern int           g_objectCountArray[32];      // 0x008ffc40 - 0x008ffcbc
extern int           g_objectDeleteCounter;       // 0x00aabd68
extern int*          g_objectDeletePtr;           // 0x004d2bf8
extern int           g_objectListCleanupFlag;     // 0x004d2fb4
extern int           g_objectListCleanupCount;    // 0x004d2fb0
extern DWORD         g_objectListPtrArray[512];   // 0x008fc430 area
extern char          g_tmdObjectBuffer[250 * 0x1594]; // 0x00923b50 area (array of CMarniDirect3DTMD, 0x1594 stride)

// Global render-state objects at fixed addresses in the original binary
// Used by FUN_00484e70 (cleanup wrapper) and related functions
extern char          g_renderStateTex[0x36c];    // 0x00aad6f0 — PSXTexture + aux data (clear via ClearState348)
extern char          g_renderStateTMD[0x1594];    // 0x00aac158 — specific CMarniDirect3DTMD instance

// PS1 GTE fixed-point pipe matrix globals
extern int g_fixedPointPipe_matrix_m00;  // 0x004c3790
extern int g_fixedPointPipe_matrix_m01;  // 0x004c3794
extern int g_fixedPointPipe_matrix_m02;  // 0x004c3798
extern int g_fixedPointPipe_matrix_m10;  // 0x004c379c
extern int g_fixedPointPipe_matrix_m11;  // 0x004c37a0
extern int g_fixedPointPipe_matrix_m12;  // 0x004c37a4
extern int g_fixedPointPipe_matrix_m20;  // 0x004c37a8
extern int g_fixedPointPipe_matrix_m21;  // 0x004c37ac
extern int g_fixedPointPipe_matrix_m22;  // 0x004c37b0
extern int matrix_t0;                    // 0x004c37b8
extern int matrix_t1;                    // 0x004c37bc
extern int matrix_t2;                    // 0x004c37c0

// PS1 GTE trig lookups (12-bit fixed-point angle → sin/cos * 4096)
int GteSin(int angle);   // 0x00440a10
int GteCos(int angle);   // 0x004409f0

// PS1 GTE matrix functions
MATRIX* RotMatrix(SVECTOR* r, MATRIX* m);      // 0x004406a0
void MatrixSetTranslation(MATRIX* m, int* translation); // 0x0040ab80
void SetGlobalScaledRotationMatrix(MATRIX* m);  // 0x0040a9e0
void GetMatrixTranslation(MATRIX* m);           // 0x0040a9c0

// PS1 sprite primitive helpers
void GteSpriteHeaderInit(SVECTOR* header);      // 0x0040abc0
int  GteClutBuild(int param_1, short param_2);  // 0x0040ac00
int  GteTpageBuild(unsigned short param_1, unsigned short param_2, int param_3, int param_4); // 0x0040ac30
