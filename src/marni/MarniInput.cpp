// MarniInput.cpp - Marni System DirectInput wrapper implementation
// Wraps Win32 GetAsyncKeyState + WinMM joyGetPosEx for PS1 controller emulation
// Original class: MarniSystem::DirectInput (debug string at 0x004ba154)

#include "MarniInput.h"
#include <cstdio>
#include <cstring>

// ============================================================================
// UpdateKeyboardInputState (0x004202f0)
// Reads 32 virtual key codes from keyMap, calls GetAsyncKeyState for each,
// and builds a bitmask in keyboardCurr where bit i = 1 if keyMap[i] is held.
// ============================================================================
void CMarniDirectInput::UpdateKeyboardInputState(MasterInputState* pState)
{
    // 0x004202f0: Zero the keyboardCurr DWORD at +0x24
    pState->keyboardCurr = 0;

    // 0x004202f8-0x0042056d: Check all 32 mapped keys
    for (int i = 0; i < 32; i++) {
        SHORT keyState = GetAsyncKeyState(pState->keyMap[i]);
        if (keyState & 0x8000) {
            pState->keyboardCurr |= (1 << i);
        }
    }
}

// ============================================================================
// SetDefaultKeyMapping (0x00420720)
// Writes the default keyboard layout to keyMap:
//   [0]=E, [1]=X, [2]=S, [3]=D (confirm/cancel/square/triangle)
//   [4-7]=arrow keys (dpad directions)
//   [8-17]='0'-'9' (number keys for menu/map/etc.)
// Remaining [18-31] are left as zero.
// ============================================================================
void CMarniDirectInput::SetDefaultKeyMapping(BYTE* keyMap)
{
    // 0x00420720: *keyMap = 0x45 (E) - action/confirm
    keyMap[0] = 0x45;

    // 0x00420727: keyMap[1] = 0x58 (X) - cross/cancel
    keyMap[1] = 0x58;

    // 0x0042072f: keyMap[2] = 0x53 (S) - square
    keyMap[2] = 0x53;

    // 0x00420736: keyMap[3] = 0x44 (D) - triangle
    keyMap[3] = 0x44;

    // 0x0042073e-0x00420757: D-pad arrows
    keyMap[4] = VK_UP;     // 0x26
    keyMap[5] = VK_DOWN;   // 0x28
    keyMap[6] = VK_LEFT;   // 0x25
    keyMap[7] = VK_RIGHT;  // 0x27

    // 0x0042075f-0x00420763: Number keys '0' through '9' (0x30-0x39)
    for (int i = 0; i < 10; i++) {
        keyMap[8 + i] = '0' + i;
    }

    // Indices 18-31 (0x12-0x1F) remain unset (zero) in the original decompilation
}

// ============================================================================
// UpdateAllInputStates (0x00420570)
// Main input update: polls keyboard + all connected joysticks.
// Computes current/previous/new-press bitmasks for both keyboard and joysticks.
// ============================================================================
void CMarniDirectInput::UpdateAllInputStates(MasterInputState* pState)
{
    // 0x0042057a: Update keyboard - writes keyboardCurr at +0x24
    UpdateKeyboardInputState(pState);

    // 0x0042057f: uVar4 = keyboardPrev (old value from last frame)
    DWORD oldPrev = pState->keyboardPrev;

    // 0x00420586: keyboardRepeat = old keyboardPrev
    // 0x0042058c: keyboardPrev = keyboardCurr (advance frame)
    // 0x00420597: frameFlag = 1
    // 0x004206e7: keyboardNewPress = ~oldPrev & keyboardCurr (newly pressed keys)
    pState->keyboardRepeat  = oldPrev;
    pState->keyboardPrev    = pState->keyboardCurr;
    pState->frameFlag       = 1;
    pState->keyboardNewPress = (~oldPrev) & pState->keyboardCurr;

    // 0x004205b8-0x004206f4: Process each joystick (indices 1 to joystickCount-1, max 31)
    int maxJoy = (int)pState->joystickCount;
    if (maxJoy > 32) maxJoy = 32;

    for (int i = 1; i < maxJoy; i++) {
        JoystickEntry* pJoy = &pState->joysticks[i];

        // 0x004205c3: If joystick is disabled, zero all press states
        if (!pJoy->enabled) {
            pJoy->prevPress = 0;
            pJoy->currPress = 0;
            pJoy->newPress  = 0;
            continue;
        }

        // 0x004205ec-0x00420612: Clear JOYINFOEX and set up for joyGetPosEx
        memset(&pJoy->info, 0, sizeof(JOYINFOEX));
        pJoy->info.dwSize  = sizeof(JOYINFOEX);
        pJoy->info.dwFlags = JOY_RETURNALL;

        // 0x00420617: Poll the joystick
        MMRESULT result = joyGetPosEx(i - 1, &pJoy->info);

        // 0x0042061f-0x00420637: Error handling
        const char* pErrorMsg = NULL;
        if (result == 6) {                       // MMSYSERR_NODRIVER
            pErrorMsg = "joyGetPosEx Error!!";
        } else if (result == JOYERR_PARMS) {     // 0xA5
            pErrorMsg = "Joy Param Error!!";
        } else if (result == JOYERR_UNPLUGGED) { // 0xA7
            pErrorMsg = "Joy Unplug Error!!";
        }

        if (pErrorMsg != NULL) {
            // 0x00420633: Print error and skip this joystick
            printf("%s [%s]\n", pErrorMsg, "MarniSystem DirectInput Class");
            continue;
        }

        // 0x0042063c-0x00420685: Build directional flags from axes
        DWORD dirFlags = 0;

        // X axis: joystick right (>0xC000) or left (<0x3000)
        if (pJoy->info.dwXpos > 0xC000) dirFlags |= 8;   // RIGHT bit
        if (pJoy->info.dwXpos < 0x3000) dirFlags |= 4;   // LEFT bit

        // Y axis: joystick down (>0xC000) or up (<0x3000)
        if (pJoy->info.dwYpos > 0xC000) dirFlags |= 2;   // DOWN bit
        if (pJoy->info.dwYpos < 0x3000) dirFlags |= 1;   // UP bit

        // 0x00420659-0x004206d5: POV hat to directional mapping
        // The POV hat is divided into 8 cardinal/diagonal zones:
        //   CENTERED (0xFFFF) = no input
        //   0x0000-0x1187 = North/UP
        //   0x1188-0x230F = NorthEast/UP+RIGHT
        //   0x2310-0x3496 = East/RIGHT
        //   0x3497-0x461E = SouthEast/DOWN+RIGHT
        //   0x461F-0x57A5 = South/DOWN
        //   0x57A6-0x692D = SouthWest/DOWN+LEFT
        //   0x692E-0x7AB4 = West/LEFT
        //   0x7AB5-0x8C3C = NorthWest/UP+LEFT
        //   >0x8C3C = wraps back to North (unhandled in original decomp)
        if ((pJoy->povFlags & 0x10) && pJoy->info.dwPOV != 0xFFFFFFFF) {
            DWORD povValue = pJoy->info.dwPOV;
            if      (povValue < 0x1187) dirFlags |= 0x10;  // UP
            else if (povValue < 0x230F) dirFlags |= 0x90;  // UP + RIGHT
            else if (povValue < 0x3496) dirFlags |= 0x80;  // RIGHT
            else if (povValue < 0x461E) dirFlags |= 0xA0;  // DOWN + RIGHT
            else if (povValue < 0x57A5) dirFlags |= 0x20;  // DOWN
            else if (povValue < 0x692D) dirFlags |= 0x60;  // DOWN + LEFT
            else if (povValue < 0x7AB4) dirFlags |= 0x40;  // LEFT
            else if (povValue < 0x8C3C) dirFlags |= 0x50;  // UP + LEFT
        }

        // 0x004206d7: Combine with button state (shifted left 8 bits)
        dirFlags |= (pJoy->info.dwButtons << 8);

        // 0x004206da-0x004206e4: Compute press transitions
        // prevPress = last frame's currPress
        // currPress = combined axis + POV + button flags
        // newPress  = buttons newly pressed this frame
        pJoy->prevPress = pJoy->currPress;
        pJoy->currPress = dirFlags;
        pJoy->newPress  = (~pJoy->prevPress) & pJoy->currPress;
    }
}

// ============================================================================
// InitJoysticks (0x00420770)
// Enumerates all joystick devices via WinMM, validates each with
// joyGetDevCapsA + joyGetPosEx, and marks valid ones as enabled.
// ============================================================================
void CMarniDirectInput::InitJoysticks(MasterInputState* pState)
{
    // 0x0042077c: Get number of joystick devices
    UINT numDevs = joyGetNumDevs();
    pState->joystickCount = numDevs + 1;

    // 0x00420788-0x0042079d: If > 32 joysticks, print warning and return
    if ((int)(numDevs + 1) > 0x1F) {
        printf("DirectInput::WM_Create: too many joysticks (%d) [%s]\n",
               pState->joystickCount, "MarniSystem DirectInput Class");
        return;
    }

    // 0x004207a5-0x004207b5: Zero from +0x28 through +0x3B28
    // This covers keyboardPrev/keyboardNewPress/keyboardRepeat, pad area,
    // frameFlag, and all joystick entries (0xEC0 DWORDs = 0x3B00 bytes)
    DWORD* pZero = (DWORD*)((BYTE*)pState + 0x28);
    for (int i = 0xEC0; i != 0; i--) {
        *pZero++ = 0;
    }

    // 0x004207c0-0x0042087f: Validate each joystick device
    for (int i = 1; i < (int)pState->joystickCount; i++) {
        JoystickEntry* pJoy = &pState->joysticks[i];

        // 0x004207cb: Tentatively mark as enabled
        pJoy->enabled = 1;

        // 0x004207d3: Query device capabilities
        JOYCAPSA joyCaps;
        memset(&joyCaps, 0, sizeof(joyCaps));
        joyGetDevCapsA(i - 1, &joyCaps, sizeof(JOYCAPSA));

        // 0x004207dd-0x004207ed: Clear JOYINFOEX and set up for validation
        memset(&pJoy->info, 0, sizeof(JOYINFOEX));
        pJoy->info.dwSize  = sizeof(JOYINFOEX);
        pJoy->info.dwFlags = JOY_RETURNALL;

        // 0x004207f2: Validate by polling the device
        MMRESULT result = joyGetPosEx(i - 1, &pJoy->info);

        // 0x004207fa-0x00420810: Disable if device is not available
        if (result == 6 || result == JOYERR_PARMS || result == JOYERR_UNPLUGGED) {
            pJoy->enabled = 0;
            continue;
        }

        // 0x0042081a-0x0042085a: Print joystick identification info
        char joyInfo[256];
        sprintf(joyInfo,
                "Joy[%d] manID:%04X prodID:%04X caps:%08X",
                i, joyCaps.wMid, joyCaps.wPid, joyCaps.wCaps);
        printf("%s [%s]\n", joyInfo, "MarniSystem DirectInput Class");
    }
}
