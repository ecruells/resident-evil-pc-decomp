# Resident Evil 1 PC - Functions Reference

This document provides detailed documentation for all implemented functions in the decompilation project.

---

## Table of Contents

1. [WinMain.cpp Functions](#winmaincpp-functions)
2. [MarniSystem.cpp Functions](#marnisystemcpp-functions)
3. [GameLoop.cpp Functions](#gameloopcpp-functions)
4. [Cleanup.cpp Functions](#cleanupcpp-functions)
5. [SystemChecks.cpp Functions](#systemcheckscpp-functions)
6. [Installation.cpp Functions](#installationcpp-functions)
7. [DisplayConfig.cpp Functions](#displayconfigcpp-functions)
8. [WindowProc.cpp Functions](#windowproccpp-functions)
9. [VideoPlayback.cpp Functions](#videoplaybackcpp-functions)

---

## WinMain.cpp Functions

### WinMain

**Address:** `0x00441350`

**Purpose:** Main entry point for the game. Handles system initialization, installation verification, display configuration, window creation, and the main message loop.

**Signature:**
```cpp
int PASCAL WinMain(
    HINSTANCE hInstance,      // Application instance handle
    HINSTANCE hPrevInstance,  // Previous instance (always NULL on Win32)
    LPSTR lpCmdLine,          // Command line arguments
    int nCmdShow              // Window show state
);
```

**Return Value:** 
- `0` - Normal exit
- `1` - Color depth error
- `2` - No CD-ROM found
- `3` - Game already running
- `4` - Setup already running
- `5` - Uninstall already running
- `6` - User cancelled installation
- `7` - Setup completed, restart needed
- `9` - Software rendering memory error
- `10` - Software rendering map error

**Dependencies:**
- `GetFreeDiskSpaceMB()` - Check disk space
- `EnumerateDriveTypes()` - Find CD-ROM
- `ShowMessageBox()` - Display errors
- `IsGameInstalled()` - Check installation
- `LoadInstallationConfiguration()` - Load settings
- `CheckVideoCapabilities()` - Check video
- `EnumerateAndSelectDisplayMode()` - Display config
- `InitializeMarniSystem()` - Init graphics
- `main_loop()` - Main game loop
- `DestroyAllSoundBanks()` - Cleanup
- `CleanupAsyncTasks()` - Cleanup
- `CleanupVideoConfigAndSaveAllSettings()` - Cleanup
- `CleanupSharedMemory()` - Cleanup
- `WindowProc()` - Window procedure

**Phases:**
1. System checks (memory, color depth, CD-ROM)
2. Single instance check (mutexes)
3. Installation verification
4. Display configuration
5. Software rendering setup (if needed)
6. Window creation
7. Game initialization
8. Main game loop

---

## MarniSystem.cpp Functions

### IsGraphicsSystemReadyForOperation

**Address:** `0x00497060`

**Purpose:** Check if the Marni Direct3D graphics system is properly initialized and ready for rendering.

**Signature:**
```cpp
BOOL IsGraphicsSystemReadyForOperation(void);
```

**Return Value:**
- `TRUE` - Graphics system is ready
- `FALSE` - Graphics system not initialized or error

**Implementation:**
```cpp
BOOL IsGraphicsSystemReadyForOperation(void)
{
    if (g_pMarniDirect3D != NULL)
    {
        // Check initialization flag at offset 0x3C
        if (*(int*)((char*)g_pMarniDirect3D + 0x3C) != 0)
        {
            return TRUE;
        }
        printf(FORMAT_STR_VIDEO_SUBSYSTEM_ERROR, s_trans_cpp_004d4798);
        return FALSE;
    }
    
    if (g_hWnd == NULL)
    {
        return TRUE;
    }
    
    printf(FORMAT_STR_FALLBACK_SYSTEM_ERROR, s_trans_cpp_004d4798);
    return FALSE;
}
```

**Dependencies:** None

---

### InitializeMarniSystem

**Address:** `0x004970c0`

**Purpose:** Initialize the Marni graphics system, including Direct3D, input devices, and lighting.

**Signature:**
```cpp
void InitializeMarniSystem(
    HWND hWnd,           // Window handle
    int displayModeID,   // Selected display mode
    int adapterID        // Display adapter ID (unused)
);
```

**Parameters:**
- `hWnd` - Handle to the game window
- `displayModeID` - Index of selected display mode
- `adapterID` - Display adapter index (not used in original)

**Operations:**
1. Validate and set screen dimensions
2. Allocate CMarniDirect3D object (8676 bytes)
3. Call CMarniDirect3D constructor
4. Verify graphics system is ready
5. Initialize joysticks
6. Check for SideWinder gamepad
7. Create lights for 3D rendering
8. Hide cursor in fullscreen mode
9. Record initialization time

**Dependencies:**
- `CMarniDirect3D_Constructor()` - Create graphics object
- `IsGraphicsSystemReadyForOperation()` - Verify init
- `InitJoysticks()` - Initialize input
- `IsSideWinderPadConnected()` - Check gamepad
- `CreateLights()` - Create lighting
- `ShowMessageBox()` - Display errors

---

### EnumerateD3DRenderers

**Address:** `0x004977f0`

**Purpose:** Enumerate available Direct3D hardware renderers and store their information.

**Signature:**
```cpp
void EnumerateD3DRenderers(void);
```

**Operations:**
1. Check if Marni Direct3D is available
2. Get count of D3D renderers
3. Enumerate each renderer's name

**Dependencies:**
- `GetDirect3DDriverCount()` - Get renderer count
- `GetDirect3DDriverName()` - Get renderer name

---

## GameLoop.cpp Functions

### main_loop

**Address:** `0x00428eb0`

**Purpose:** Main game loop that handles input, game state updates, rendering, and state transitions.

**Signature:**
```cpp
int main_loop(void);
```

**Return Value:**
- `1` - Continue game loop
- `0` - Exit game (never reached in normal operation)

**Operations:**
1. Initialize game on first run
2. Update input state
3. Check for special key combinations
4. Handle input flags
5. Check FMV playback state
6. Handle menu dialogs (return to title, exit game)
7. Update demo timer
8. Reset screen panning
9. Update sound fade/decay
10. Update task scheduler
11. Handle screen fading effects
12. Apply room-specific lighting
13. Handle screenshot mode
14. Apply screen shake
15. Present frame

**Dependencies:**
- `init_and_start_game()` - Initialize game
- `InputUpdate()` - Update input
- `PlayerPad_Update()` - Update player input
- `TaskScheduler_Update()` - Update tasks
- `draw_rect()` - Draw rectangles
- `PrintText8x14()` - Print text
- `PauseSounds()` - Pause audio
- `ResumePausedSounds()` - Resume audio
- `Task_suspend()` - Suspend task
- `Task_Resume()` - Resume task
- Many more helper functions

**State Flags Checked:**
- `g_main_state_flags` - Various game states
- `g_displayReturnToTitleScreen_Flag` - Return dialog
- `g_displayExitGameScreen_flag` - Exit dialog
- `g_fading_state` - Screen fade state

---

## Cleanup.cpp Functions

### CleanupSharedMemory

**Address:** `0x00442230`

**Purpose:** Release shared memory used for inter-process communication with the setup program.

**Signature:**
```cpp
void CleanupSharedMemory(void);
```

**Operations:**
1. Signal cleanup by writing 0 to first byte
2. Wait for acknowledgment (byte 1 != 1)
3. Sleep for 1 second
4. Unmap shared memory view
5. Close file mapping handle

**Dependencies:** None

---

### UpdateGameStatus

**Address:** `0x00442350`

**Purpose:** Update game status flag in shared memory to indicate the game is running.

**Signature:**
```cpp
void UpdateGameStatus(void);
```

**Operations:**
- Set byte 5 of shared memory to 1

**Dependencies:** None

---

### CleanupVideoConfigAndSaveAllSettings

**Address:** `0x00497ea0`

**Purpose:** Perform final cleanup of video system and save all settings to registry.

**Signature:**
```cpp
void CleanupVideoConfigAndSaveAllSettings(void);
```

**Operations:**
1. Check guard flag to prevent double cleanup
2. Save settings to registry
3. Call CMarniDirect3D cleanup/destructor
4. Free allocated memory
5. Set global pointer to NULL

**Dependencies:**
- `SaveGameSettingsToRegistry()` - Save settings
- `CVideoSystem_Cleanup()` - Cleanup graphics

---

### DestroyAllSoundBanks

**Address:** `0x004801c0`

**Purpose:** Destroy all loaded sound banks and free associated resources.

**Signature:**
```cpp
void DestroyAllSoundBanks(void);
```

**Operations:**
1. Destroy BGM sound bank
2. Destroy all SFX banks
3. Destroy room SFX banks
4. Destroy character SFX banks
5. Destroy enemy SFX banks
6. Destroy general sound banks

**Dependencies:**
- `destroySndBank()` - Destroy individual bank

---

### CleanupAsyncTasks

**Address:** `0x0041d0b0`

**Purpose:** Initiate cleanup of asynchronous operations.

**Signature:**
```cpp
void CleanupAsyncTasks(void);
```

**Operations:**
- Execute async callback to destroy sound manager

**Dependencies:**
- `ExecAsync()` - Execute async callback
- `DestroySoundManagerAsync()` - Callback function

---

## SystemChecks.cpp Functions

### GetFreeDiskSpaceMB

**Address:** `0x0040c510`

**Purpose:** Calculate free disk space on the drive containing the specified path.

**Signature:**
```cpp
DWORD GetFreeDiskSpaceMB(
    LPCSTR lpPath    // Path to check (only first char used)
);
```

**Parameters:**
- `lpPath` - Path string; only the drive letter (first character) is used

**Return Value:** Free disk space in megabytes

**Implementation:**
```cpp
DWORD GetFreeDiskSpaceMB(LPCSTR lpPath)
{
    CHAR szRootPath[4];
    DWORD dwSectorsPerCluster, dwBytesPerSector;
    DWORD dwNumberOfFreeClusters, dwTotalNumberOfClusters;
    
    szRootPath[0] = lpPath[0];
    szRootPath[1] = ':';
    szRootPath[2] = '\\';
    szRootPath[3] = '\0';
    
    GetDiskFreeSpaceA(szRootPath, &dwSectorsPerCluster,
        &dwBytesPerSector, &dwNumberOfFreeClusters,
        &dwTotalNumberOfClusters);
    
    return (dwNumberOfFreeClusters * dwBytesPerSector * 
            dwSectorsPerCluster) >> 20;
}
```

**Dependencies:** None (Win32 API only)

---

### EnumerateDriveTypes

**Address:** `0x0047b830`

**Purpose:** Enumerate all logical drives and store their types for CD-ROM detection.

**Signature:**
```cpp
BOOL EnumerateDriveTypes(void);
```

**Return Value:**
- `TRUE` - At least one drive found
- `FALSE` - No drives found

**Operations:**
1. Clear drive type buffer
2. Get all logical drive strings
3. Parse each drive string
4. Get drive type using GetDriveTypeA
5. Store in global arrays

**Dependencies:** None (Win32 API only)

---

### ShowMessageBox

**Address:** `0x00497ee0`

**Purpose:** Display a message box with automatic cursor visibility handling.

**Signature:**
```cpp
int ShowMessageBox(
    HWND hWndOwner,       // Owner window
    LPCSTR lpMessageText, // Message text
    LPCSTR lpCaptionText, // Caption text
    UINT uMessageBoxType  // Message box style
);
```

**Parameters:**
- `hWndOwner` - Handle to owner window
- `lpMessageText` - Message to display
- `lpCaptionText` - Window caption
- `uMessageBoxType` - MB_ flags

**Return Value:** Result from MessageBoxA (IDOK, IDCANCEL, etc.)

**Operations:**
1. Show cursor if hidden
2. Display message box
3. Restore cursor state

**Dependencies:** None (Win32 API only)

---

## Installation.cpp Functions

### IsGameInstalled

**Address:** `0x0040ae50`

**Purpose:** Check if the game is properly installed by verifying registry key existence.

**Signature:**
```cpp
BOOL IsGameInstalled(void);
```

**Return Value:**
- `TRUE` - Game is installed (registry key exists)
- `FALSE` - Game is not installed

**Registry Key:** `HKEY_CURRENT_USER\Software\CAPCOM\RESIDENT EVIL`

**Dependencies:** None (Win32 API only)

---

### LoadInstallationConfiguration

**Address:** `0x0040aea0`

**Purpose:** Load all game settings from the Windows registry.

**Signature:**
```cpp
BOOL LoadInstallationConfiguration(
    BYTE* pInstallPath    // Buffer to receive install path
);
```

**Parameters:**
- `pInstallPath` - Buffer to receive the full installation path

**Return Value:**
- `TRUE` - Configuration loaded successfully
- `FALSE` - Error loading configuration

**Registry Values Read:**
| Value | Type | Variable |
|-------|------|----------|
| Install Path | REG_SZ | Path buffer |
| Create Directory | REG_SZ | Appended to path |
| X Size | REG_DWORD | `g_dwScreenWidth` |
| Y Size | REG_DWORD | `g_dwScreenHeight` |
| Bit Depth | REG_DWORD | `g_dwBitDepth` |
| FullScreen? | REG_DWORD | `g_bFullScreen` |
| Play Number | REG_DWORD | `g_dwPlayCount` |
| Clear Number | REG_DWORD | `g_dwClearCount` |
| Key Def | REG_BINARY | `g_keyBindingData` |
| Side Def | REG_BINARY | `g_joystickBindingData` |
| Joy Def | REG_BINARY | `g_joystickBindingData` |
| Display Driver | REG_DWORD | `g_dwSelectedDisplayAdapterID` |
| Install Flag | REG_DWORD | `g_InstallFlagData` |
| Display Mode | REG_DWORD | `g_dwSelectedDisplayModeID` |

**Dependencies:**
- `IsSideWinderPadConnected()` - Check for SideWinder

---

## DisplayConfig.cpp Functions

### EnumDisplayModesCallback

**Address:** `0x00442930`

**Purpose:** DirectDraw callback function for enumerating available display modes.

**Signature:**
```cpp
HRESULT CALLBACK EnumDisplayModesCallback(
    LPDDSURFACEDESC lpDDSurfaceDesc,  // Surface description
    LPVOID lpContext                   // User context
);
```

**Parameters:**
- `lpDDSurfaceDesc` - DirectDraw surface description for the mode
- `lpContext` - Pointer to EnumDisplayModesContext structure

**Return Value:** `DDENUMRET_OK` to continue enumeration

**Operations:**
1. Extract mode info from surface description
2. Store in display mode buffer
3. Increment mode count

**Dependencies:** None (DirectDraw API)

---

### EnumerateAndSelectDisplayMode

**Address:** `0x00442870`

**Purpose:** Enumerate display modes and show selection dialog.

**Signature:**
```cpp
INT_PTR EnumerateAndSelectDisplayMode(void);
```

**Return Value:**
- Dialog result from selection
- `-1` on failure

**Operations:**
1. Create DirectDraw object
2. Clear display mode buffer
3. Enumerate all display modes
4. Release DirectDraw
5. Display selection dialog

**Dependencies:**
- `EnumDisplayModesCallback()` - Enumeration callback
- `DisplayModeDialogProc()` - Dialog procedure

---

### EnumerateDisplayModes

**Address:** `0x004976c0`

**Purpose:** Enumerate display modes through the Marni Direct3D interface.

**Signature:**
```cpp
void EnumerateDisplayModes(void);
```

**Operations:**
1. Check if Marni Direct3D is available
2. Get display mode count
3. For each mode, get mode info and store

**Dependencies:**
- `GetDisplayModeCount()` - Get mode count
- `GetDisplayModeRect()` - Get mode info

---

### GetDisplayModeCount

**Address:** `0x00448770`

**Purpose:** Get the number of available display modes.

**Signature:**
```cpp
int GetDisplayModeCount(void);
```

**Return Value:** Number of display modes

**Status:** ⚠️ Stub implementation (returns 0)

---

### GetDisplayModeRect

**Address:** `0x004487a0`

**Purpose:** Get display mode information by index.

**Signature:**
```cpp
void GetDisplayModeRect(
    int modeIndex,    // Mode index
    DWORD* pRect      // Output buffer (5 DWORDs)
);
```

**Parameters:**
- `modeIndex` - Index of the display mode
- `pRect` - Buffer to receive: [width, height, bpp, refresh, flags]

**Status:** ⚠️ Stub implementation (returns 640x480 defaults)

---

## WindowProc.cpp Functions

### WindowProc

**Address:** `0x00441170`

**Purpose:** Main window procedure handling all window messages.

**Signature:**
```cpp
LRESULT CALLBACK WindowProc(
    HWND hWnd,      // Window handle
    UINT uMsg,      // Message ID
    WPARAM wParam,  // WPARAM
    LPARAM lParam   // LPARAM
);
```

**Parameters:**
- `hWnd` - Window handle
- `uMsg` - Message identifier
- `wParam` - Message-specific parameter
- `lParam` - Message-specific parameter

**Return Value:** Message-dependent result

**Messages Handled:**

| Message | Handler |
|---------|---------|
| `WM_CREATE` | Initialize sound system |
| `WM_DESTROY` | Cleanup and quit |
| `WM_ACTIVATE` | Handle activation state |
| `WM_KEYDOWN` | Process key input |
| `WM_KEYUP` | Check for Print Screen |
| `WM_PAINT` | Begin/End paint |
| `WM_SYSCOMMAND` | Handle restore for software mode |
| `MM_MCINOTIFY` | MCI video notification |

**Dependencies:**
- `ProbeWaveOutDevicesAndCacheVolume()` - Cache audio volume
- `StartSoundSystemAsync()` - Initialize sound
- `PauseSounds()` - Pause audio
- `ResumePausedSounds()` - Resume audio
- `CleanupVideoConfigAndSaveAllSettings()` - Cleanup
- `RestoreWaveOutVolume()` - Restore volume
- `OnKeyDown()` - Key handler
- `CreateTimestampedLogFile()` - Debug log

---

## VideoPlayback.cpp Functions

### UpdateVideoPlayback

**Address:** `0x00474e00`

**Purpose:** FMV playback state machine for both hardware and software rendering modes.

**Signature:**
```cpp
void UpdateVideoPlayback(void);
```

**State Machine:**

#### Hardware Rendering Path

| State | Operation |
|-------|-----------|
| 0 | Initialize: Clear screen, flip to GDI, open MCI device |
| 1 | Start playback: Begin video, init skip detection |
| 2 | Playing: Check for skip/end, handle events |
| 3 | Cleanup: Close MCI, restore surfaces, resume audio |

#### Software Rendering Path

| State | Operation |
|-------|-----------|
| 0 | Initialize: Clear screen, launch external player |
| 1 | Playing: Check for skip, wait for completion |
| 2 | Cleanup: Restore window, resume audio |

**Dependencies:**
- `ClearScreen()` - Clear display
- `OpenMCIAviVideo()` - Open MCI device
- `CheckVideoFileExists()` - Verify video file
- `setMenuScreenOffset()` - Set screen offset
- `CenterScreenOrigin()` - Center screen
- `InputUpdate()` - Update input
- `PlayerPad_Update()` - Get player input
- `PauseGameSoundsAsync()` - Pause audio
- `ResumeGameSoundsAsync()` - Resume audio
- `ShowMessageBox()` - Display errors
- `StMask()` - State mask function
- Many software player functions

---

## Function Index by Address

| Address | Function | File |
|---------|----------|------|
| `0x0040ae50` | `IsGameInstalled` | Installation.cpp |
| `0x0040aea0` | `LoadInstallationConfiguration` | Installation.cpp |
| `0x0040c510` | `GetFreeDiskSpaceMB` | SystemChecks.cpp |
| `0x0041d0b0` | `CleanupAsyncTasks` | Cleanup.cpp |
| `0x00428eb0` | `main_loop` | GameLoop.cpp |
| `0x00441170` | `WindowProc` | WindowProc.cpp |
| `0x00441350` | `WinMain` | WinMain.cpp |
| `0x00442230` | `CleanupSharedMemory` | Cleanup.cpp |
| `0x00442350` | `UpdateGameStatus` | Cleanup.cpp |
| `0x00442870` | `EnumerateAndSelectDisplayMode` | DisplayConfig.cpp |
| `0x00442930` | `EnumDisplayModesCallback` | DisplayConfig.cpp |
| `0x00448770` | `GetDisplayModeCount` | DisplayConfig.cpp |
| `0x004487a0` | `GetDisplayModeRect` | DisplayConfig.cpp |
| `0x00474e00` | `UpdateVideoPlayback` | VideoPlayback.cpp |
| `0x0047b830` | `EnumerateDriveTypes` | SystemChecks.cpp |
| `0x004801c0` | `DestroyAllSoundBanks` | Cleanup.cpp |
| `0x00497060` | `IsGraphicsSystemReadyForOperation` | MarniSystem.cpp |
| `0x004970c0` | `InitializeMarniSystem` | MarniSystem.cpp |
| `0x004976c0` | `EnumerateDisplayModes` | DisplayConfig.cpp |
| `0x004977f0` | `EnumerateD3DRenderers` | MarniSystem.cpp |
| `0x00497ea0` | `CleanupVideoConfigAndSaveAllSettings` | Cleanup.cpp |
| `0x00497ee0` | `ShowMessageBox` | SystemChecks.cpp |

---
