// InputSystem.cpp - Game input handling
// JoyToPSX (0x00404c90), ReadPadBoth (0x00497ba0), PlayerPad_Update (0x0044e000)
// InputUpdate (0x00497c00)
#include "../Globals.h"
#include "../marni/MarniInput.h"

// ============================================================================
// JoyToPSX - Convert PC joystick bitmask to PSX button word (0x00404c90)
//
// Maps 32 PC joystick button bits through g_JoyRemapTbl[player] to produce
// the corresponding PSX controller button word.
// ============================================================================
DWORD JoyToPSX(DWORD pcMask, int player)
{
	DWORD result;
	int i;
	const DWORD* pRemap;
	DWORD bitCheck;

	if (player > 2) {
		if (g_JoyWarnPrinted == 0) {
			// Original prints a warning; in port we just set the flag
		}
		g_JoyWarnPrinted = 1;
		return 0;
	}

	result = 0;
	bitCheck = 1;
	pRemap = g_JoyRemapTbl[player];
	for (i = 32; i != 0; i--) {
		if ((bitCheck & pcMask) != 0) {
			result |= *pRemap;
		}
		bitCheck *= 2;
		pRemap++;
	}
	return result;
}

// ============================================================================
// ReadPadBoth - Read both controllers and combine into PSX button word (0x00497ba0)
//
// Original code reads from g_pMasterInputState:
//   P1: g_pMasterInputState.keyboardPrev (offset 0x28) → JoyToPSX(0)
//   P2: g_pMasterInputState + 0x200 (joystick[0].currPress) → JoyToPSX(1)
//   Joystick only active if g_pMasterInputState + 0x3D4 (joystick[0].enabled) != 0
// ============================================================================
DWORD ReadPadBoth(void)
{
	DWORD tmp;

	if (g_pMasterInputState.frameFlag != 0) {
		g_PadBtnWord = JoyToPSX(g_pMasterInputState.keyboardPrev, 0);
	}
	if ((1 < g_NumControllers) && (g_pMasterInputState.joysticks[0].enabled != 0)) {
		tmp = JoyToPSX(g_pMasterInputState.joysticks[0].currPress, 1);
		g_PadBtnWord |= tmp;
	}
	if (g_DisablePad != 0) {
		return 0;
	}
	return g_PadBtnWord;
}

// ============================================================================
// InputUpdate - Poll all input states (0x00497c00)
//
// Original code:
//   MOV ECX, 0xac4030        ; ECX = &g_pMasterInputState (__fastcall arg1)
//   JMP  UpdateAllInputStates ; tail-call
// ============================================================================
void InputUpdate(void)
{
	CMarniDirectInput::UpdateAllInputStates(&g_pMasterInputState);
}

// ============================================================================
// PlayerPad_Update - Edge-detect pad input (0x0044e000)
//
// Polls input via InputUpdate()'s side effect (ReadPadBoth reads
// g_pMasterInputState.keyboardPrev), performs rising-edge detection on
// the raw button word and on the dpad, and publishes the results.
//
// Six-part function:
//
// Part 1: Compute previous-frame dpad state for use in part 6.
//   Iterates the 16-entry remap table selected by (g_controllerConfig & 3).
//   For each entry whose bit is set in g_button_pressed_id (the previous
//   frame's held word), sets the matching bit in prevDpadState counting
//   down from bit 15. Then clears bits 2 and 3 of prevDpadState.
//
// Part 2: Snapshot current frame's raw state for the NEXT call's edge
//   detection. g_RawPadHeld (raw, not edge-detected) is stored into
//   g_PlayerPadPressed so the rising-edge formula in part 4 is
//   (~previousRaw & newRaw), matching the Ghidra sequence.
//     g_PlayerPadPressed   = g_RawPadHeld
//     g_PlayerDpadHeldPrev = g_PlayerDpadHeld
//     g_PlayerPadHeldPrev  = g_RawPadState
//
// Part 3: Read new input.
//   Normal mode (g_main_state_flags2 bit 28 == 0):
//     newHeldRaw   = ReadPadBoth();
//     g_RawPadHeld = newHeldRaw;
//   Demo / attract mode (bit 28 set): overwrite g_RawPadHeld with the
//   demo playback word (g_demoPadData[g_DemoTimerCur]) or zero, then
//   still call ReadPadBoth() so newHeldRaw reflects the live pad for
//   g_RawPadState.
//
// Part 4: Edge detect on the raw button word.
//     g_RawPadState         = (WORD)newHeldRaw
//     g_padEdgeDetectedWord = (WORD)(~g_PlayerPadHeldPrev & (WORD)newHeldRaw)
//     g_button_pressed_id   = g_RawPadHeld        (== return value)
//     g_PlayerPadPressed    = ~previousRaw & g_RawPadHeld   (rising-edge pressed)
//
// Part 5: Remap current g_button_pressed_id to the dpad state word and
//   publish the edge-detected held state for gameplay code.
//     g_PlayerPadHeld  = g_PlayerPadPressed  (gameplay-facing edge-detected)
//     g_PlayerDpadHeld = remap(g_button_pressed_id, g_padRemapTable)
//   Bits 2 and 3 of g_PlayerDpadHeld are cleared (same as part 1).
//
// Part 6: Edge detect on the dpad.
//     g_PlayerDpadPressed = ~prevDpadState & g_PlayerDpadHeld
//
// Returns g_button_pressed_id = g_RawPadHeld (current held, full 32-bit
// but only the low 16 bits are used by FMV skip and most callers).
// ============================================================================
DWORD PlayerPad_Update(void)
{
	WORD prevDpadState;
	WORD bitShift;
	BYTE i;
	const WORD* pRemapTable;
	DWORD newHeldRaw;

	// Part 1: Remap previous g_button_pressed_id to get previous dpad state
	bitShift = 0x8000;
	prevDpadState = 0;
	pRemapTable = g_padRemapTable[g_controllerConfig & 3];

	for (i = 16; i != 0; ) {
		i--;
		if ((g_button_pressed_id & pRemapTable[i]) != 0) {
			prevDpadState |= bitShift;
		}
		bitShift >>= 1;
	}

	if ((prevDpadState & 1) != 0) {
		prevDpadState &= 0xFFFB;
	}
	if ((prevDpadState & 2) != 0) {
		prevDpadState &= 0xFFF7;
	}

	// Part 2: Save previous states
	// Save the PREVIOUS raw held (0x00bf0a04) to pressed, not the edge-detected
	// value at 0x00bf0a10. Edge detection must use the previous raw input to
	// correctly detect rising edges (~prevRaw & newRaw).
	g_PlayerPadPressed = g_RawPadHeld;
	g_PlayerDpadHeldPrev = g_PlayerDpadHeld;
	g_PlayerPadHeldPrev = g_RawPadState;

	// Part 3: Read new input into the raw held variable (0x00bf0a04)
	if ((g_main_state_flags2 & MSF2_ATTRACT_DEMO) == 0) {
		newHeldRaw = ReadPadBoth();
		g_RawPadHeld = newHeldRaw;
	} else {
		if (((g_message_flags & 0x0200) == 0) || (g_DemoTimerCur == 0)) {
			g_RawPadHeld = 0;
		} else {
			g_RawPadHeld = (DWORD)g_demoPadData[(WORD)g_DemoTimerCur];
		}
		newHeldRaw = ReadPadBoth();
	}

	// Part 4: Edge detection (uses g_RawPadHeld = previous frame's raw + new raw)
	g_RawPadState = (WORD)newHeldRaw;
	g_padEdgeDetectedWord = (WORD)(~g_PlayerPadHeldPrev & (WORD)newHeldRaw);
	g_button_pressed_id = g_RawPadHeld;
	g_PlayerPadPressed = ~g_PlayerPadPressed & g_RawPadHeld;

	// Part 5: Remap current g_button_pressed_id to compute g_PlayerDpadHeld
	g_PlayerDpadHeld = 0;
	bitShift = 0x8000;

	// 0x0044e14a: g_PlayerPadHeld := edge-detected value
	// Write the edge-detected result to g_PlayerPadHeld at 0x00bf0a10.
	// g_RawPadHeld at 0x00bf0a04 is NOT overwritten — menu code reads it
	// for held-button navigation (e.g. "hold R3+START to cancel").
	g_PlayerPadHeld = g_PlayerPadPressed;

	for (i = 16; i != 0; ) {
		i--;
		if ((g_button_pressed_id & pRemapTable[i]) != 0) {
			g_PlayerDpadHeld |= bitShift;
		}
		bitShift >>= 1;
	}

	if ((g_PlayerDpadHeld & 1) != 0) {
		g_PlayerDpadHeld &= 0xFFFB;
	}
	if ((g_PlayerDpadHeld & 2) != 0) {
		g_PlayerDpadHeld &= 0xFFF7;
	}

	// Part 6: Edge detect on DPad
	g_PlayerDpadPressed = ~prevDpadState & g_PlayerDpadHeld;

	// While the F1 debug menu is open, publish a blank pad to gameplay but
	// keep the internal state (g_button_pressed_id, g_RawPadHeld,
	// g_RawPadState, prevDpadState inputs) continuous. Blanking the published
	// values here - instead of after debug_menu_overlay in game_loop - is
	// what keeps that continuity: part 1 derives prevDpadState from
	// g_button_pressed_id, so zeroing that global mid-session would re-mint
	// every still-held key as a fresh rising edge on the frame the menu
	// closes, firing the door / typewriter / item action the player stands in
	// front of (ENTER/SPACE is the action key). The overlay itself samples
	// the keyboard directly and is unaffected. g_debugMenuOpen stays 0 while
	// debug features are disabled.
	if (g_debugMenuOpen != 0) {
		g_PlayerPadPressed = 0;
		g_PlayerPadHeld = 0;
		g_PlayerDpadHeld = 0;
		g_PlayerDpadPressed = 0;
	}

	return g_button_pressed_id;
}

// ---------------------------------------------------------------------------
// ResetGetAsyncKeyStateFlags (0x00497e60)
//
// Calls GetAsyncKeyState() for every possible virtual key (0-255) to reset
// the internal low-bit flag that indicates whether a key was pressed since
// the last query. This flushes stale/buffered keyboard input when
// transitioning between game states (menus, gameplay, cutscenes, pause,
// camera changes), preventing unintended actions from leftover key presses.
// This does NOT disable input or read gameplay input — it only resets the
// OS-level key press history so that only new key presses after this call
// will be detected.
// Called via ScheduleInputFlush to prevent FMV-skip button from immediately
// dismissing loading messages.
// ---------------------------------------------------------------------------
void ResetGetAsyncKeyStateFlags(void)
{
    BYTE vk;
    GetAsyncKeyState(0);
    for (vk = 1; vk != 0; vk++) {
        GetAsyncKeyState(vk);
    }
}

// ---------------------------------------------------------------------------
// ScheduleInputFlush (0x00497e80)
// Schedules an async call to ResetGetAsyncKeyStateFlags.
// ---------------------------------------------------------------------------
void ScheduleInputFlush(void)
{
    ExecAsync((void*)ResetGetAsyncKeyStateFlags);
}
