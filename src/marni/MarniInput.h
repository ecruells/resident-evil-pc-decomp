// MarniInput.h - Marni System DirectInput wrapper
// Wraps Win32 GetAsyncKeyState + WinMM joyGetPosEx for PS1 controller emulation
// Original class: MarniSystem::DirectInput (debug string at 0x004ba154)
#pragma once
#include <windows.h>
#include <mmsystem.h>

// Maximum joysticks supported
#define MAX_JOYSTICKS 32

// Joystick entry (stride 0x1D8 = 472 bytes)
// Original layout (confirmed from UpdateAllInputStates ASM at 0x00420570):
//   puVar6 (uint*) starts at entry+0x34
//   puVar6[-0xD] = currPress (0x00), puVar6[-0xC] = newPress (0x04), puVar6[-0xB] = prevPress (0x08)
//   puVar6[-10]  = JOYINFOEX (0x0C), puVar6[-8] = dwXpos, puVar6[-7] = dwYpos
//   puVar6[0]    = dwPOV (0x34), puVar6[0x1B] = povFlags (0xA0), puVar6[0x68] = enabled (0x1D4)
struct JoystickEntry {
    DWORD      currPress;         // +0x00 - puVar6[-0xD] current button/axis state
    DWORD      newPress;          // +0x04 - puVar6[-0xC] newly pressed this frame
    DWORD      prevPress;         // +0x08 - puVar6[-0xB] previous frame state
    JOYINFOEX  info;              // +0x0C - JOYINFOEX (0x34 bytes), puVar6[-10]
    BYTE       pad_40[0x60];      // +0x40 - padding to povFlags at +0xA0
    DWORD      povFlags;          // +0xA0 - puVar6[0x1B] bit 0x10 = POV enabled
    BYTE       pad_A4[0x130];     // +0xA4 - padding to enabled at +0x1D4
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

// Global master input state instance (0x00ac4030)
extern MasterInputState g_pMasterInputState;

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
