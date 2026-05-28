// InputStubs.cpp - Input handling stubs
// These functions will be properly implemented with XInput later
#include "../Globals.h"
#include <Xinput.h>

// ============================================================================
// InputUpdate - Update all input states (0x00497c00)
// ============================================================================
void InputUpdate(void)
{
    g_RawPadPressed = 0;
    
    // Map keyboard to PS1 controller layout:
    // byte 0 (bits 0-7): SELECT, L3, R3, START, UP, RIGHT, DOWN, LEFT
    // byte 1 (bits 8-15): L2, R2, L1, R1, TRIANGLE, CIRCLE, CROSS, SQUARE
    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) g_RawPadPressed |= 0x8000;  // SQUARE = exit
    if (GetAsyncKeyState(VK_RETURN) & 0x8000) g_RawPadPressed |= 0x4008;  // CROSS + START
    if (GetAsyncKeyState(VK_SPACE)  & 0x8000) g_RawPadPressed |= 0x4008;  // CROSS + START
    if (GetAsyncKeyState(VK_UP)     & 0x8000) g_RawPadPressed |= 0x1000;  // TRIANGLE (menu up)
    if (GetAsyncKeyState(VK_DOWN)   & 0x8000) g_RawPadPressed |= 0x4000;  // CROSS (menu down)
    if (GetAsyncKeyState(VK_LEFT)   & 0x8000) g_RawPadPressed |= 0x0080;  // LEFT
    if (GetAsyncKeyState(VK_RIGHT)  & 0x8000) g_RawPadPressed |= 0x0020;  // RIGHT
    
    XINPUT_STATE state;
    if (XInputGetState(0, &state) == ERROR_SUCCESS) {
        if (state.Gamepad.wButtons & XINPUT_GAMEPAD_A)              g_RawPadPressed |= 0x4008;
        if (state.Gamepad.wButtons & XINPUT_GAMEPAD_B)              g_RawPadPressed |= 0x8000;
        if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP)        g_RawPadPressed |= 0x1010;
        if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN)      g_RawPadPressed |= 0x4040;
        if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT)      g_RawPadPressed |= 0x0080;
        if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT)     g_RawPadPressed |= 0x0020;
    }
}

void PlayerPad_Update(void)
{
    button_pressed_id = g_RawPadPressed;
    g_PlayerPadPressed = g_RawPadPressed;
}

// ============================================================================
// OnKeyDown - Handle keyboard input (called from WindowProc)
// ============================================================================
void OnKeyDown(HWND hwnd, WPARAM wparam)
{
    switch (wparam) {
        case VK_F9:
            g_displayReturnToTitleScreen_Flag = !g_displayReturnToTitleScreen_Flag;
            break;
        case VK_ESCAPE:
            g_displayExitGameScreen_flag = TRUE;
            break;
        default:
            break;
    }
}
