// MarniInput.h - Marni System DirectInput wrapper
// Wraps Win32 GetAsyncKeyState + WinMM joyGetPosEx for PS1 controller emulation
// Original class: MarniSystem::DirectInput (debug string at 0x004ba154)
#pragma once
#include <windows.h>
#include <mmsystem.h>

// Maximum joysticks supported
#define MAX_JOYSTICKS 32

// Joystick entry (stride 0x1D8 = 472 bytes)
// Fields at offsets relative to start of joystick array at +0x200 in MasterInputState
struct JoystickEntry {
    JOYINFOEX  info;              // +0x00 - JOYINFOEX (0x34 bytes)
    BYTE       pad1[0x18];        // +0x34
    DWORD      xPos;              // +0x4C - puVar6[-8] scaled to 0x0000-0xFFFF
    DWORD      yPos;              // +0x50 - puVar6[-7]
    BYTE       pad2[0x18];        // +0x54
    DWORD      prevPress;         // +0x6C - puVar6[-0xB] previous button state
    DWORD      currPress;         // +0x70 - puVar6[-0xD] current button state
    DWORD      newPress;          // +0x74 - puVar6[-0xC] newly pressed buttons
    BYTE       pad3[0x8];         // +0x78
    DWORD      extraFlags;        // +0x80 - puVar6[-2] flags shifted left 8
    BYTE       pad4[0x1C];        // +0x84
    DWORD      povValue;          // +0xA0 - puVar6[0x00] POV hat value
    BYTE       pad5[0x64];        // +0xA4
    DWORD      povFlags;          // +0x108 - puVar6[0x1B] bit 0x10 = POV enabled
    BYTE       pad6[0xC8];        // +0x10C - padding to reach 0x1D4
    DWORD      enabled;           // +0x1D4 - puVar6[0x68] 0=disabled, 1=enabled
};

// Master input state structure (object at g_pMasterInputState)
// Size: 0x3B2C+ bytes
struct MasterInputState {
    // Keyboard mapping (offset 0x00 - 0x1F)
    BYTE       keyMap[32];        // 0x00 - virtual key codes for 32 PS1 buttons

    // Padding (0x20 - 0x23)
    DWORD      pad_20;

    // Keyboard state (0x24 - 0x33)
    DWORD      keyboardCurr;      // 0x24 - current frame pressed keys (bitmask)
    DWORD      keyboardPrev;      // 0x28 - previous frame pressed keys
    DWORD      keyboardNewPress;  // 0x2C - newly pressed this frame (~prev & curr)
    DWORD      keyboardRepeat;    // 0x30 - repeat latch (prev before overwrite)

    // Padding (0x34 - 0x1FB)
    BYTE       pad_34[0x1C8];

    // Frame flag (0x1FC)
    DWORD      frameFlag;         // 0x1FC - set to 1 each frame

    // Joystick array (0x200 - 0x3B27)
    // 32 entries × 0x1D8 bytes each = 0x3B00 bytes
    JoystickEntry joysticks[MAX_JOYSTICKS];  // 0x200

    // Joystick count (0x3B28)
    DWORD      joystickCount;     // 0x3B28 - number of joysticks + 1
};

// Global pointer to the master input state
extern MasterInputState* g_pMasterInputState;

// Key binding data (persistent, saved to registry)
extern BYTE g_keyBindingData[32];

// DirectInput class methods
class CMarniDirectInput {
public:
    // Update keyboard state from GetAsyncKeyState (0x004202f0)
    static void UpdateKeyboardInputState(MasterInputState* pState);

    // Update all input states - keyboard + all joysticks (0x00420570)
    static void UpdateAllInputStates(MasterInputState* pState);

    // Set default keyboard mapping (0x00420720)
    static void SetDefaultKeyMapping(BYTE* keyMap);

    // Initialize joystick devices (0x00420770)
    static void InitJoysticks(MasterInputState* pState);
};
