// Coop.cpp - two-player co-op mod (port addition, not in the original binary)
#include "Coop.h"

int   g_coopEnabled = 0;

DWORD g_CoopPadBtnWord = 0;
DWORD g_CoopRawPadHeld = 0;
DWORD g_CoopPadPressed = 0;
WORD  g_CoopDpadHeld = 0;
WORD  g_CoopDpadHeldPrev = 0;
WORD  g_CoopDpadPressed = 0;

// Mirrors parts 2-6 of PlayerPad_Update (0x0044e000) for the second pad.
void CoopPad_Update(void)
{
	if (!g_coopEnabled) {
		return;
	}

	const WORD* pRemapTable = g_padRemapTable[g_controllerConfig & 3];
	DWORD prevRaw = g_CoopRawPadHeld;

	// Attract demo plays back player 1 only; player 2 stays idle.
	DWORD newRaw = ((g_main_state_flags2 & MSF2_ATTRACT_DEMO) == 0) ? g_CoopPadBtnWord : 0;

	g_CoopDpadHeldPrev = g_CoopDpadHeld;
	g_CoopRawPadHeld = newRaw;
	g_CoopPadPressed = ~prevRaw & newRaw;

	WORD held = 0;
	WORD bit = 0x8000;
	for (int i = 16; i != 0; ) {
		i--;
		if ((newRaw & pRemapTable[i]) != 0) {
			held |= bit;
		}
		bit >>= 1;
	}
	if ((held & 1) != 0) {
		held &= 0xFFFB;
	}
	if ((held & 2) != 0) {
		held &= 0xFFF7;
	}
	g_CoopDpadHeld = held;
	g_CoopDpadPressed = ~g_CoopDpadHeldPrev & held;

	if (g_CoopDpadHeld != g_CoopDpadHeldPrev) {
		dbg_printf("[COOP] P2 dpad held 0x%04X (raw 0x%04X)\n", g_CoopDpadHeld, g_CoopRawPadHeld);
	}
}
