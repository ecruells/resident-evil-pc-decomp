// Coop.h - two-player co-op mod (port addition, not in the original binary)
//
// Player 1 is the keyboard, player 2 is the first pad. The original ORs both
// into one pad word (ReadPadBoth); with [Coop] Enable=1 they are split so each
// player gets their own PSX button word, edge detection and remapped dpad word.
#pragma once
#include "../Globals.h"

extern int   g_coopEnabled;          // [Coop] Enable - 0 = original behaviour

// Player 2 input, same layout and meaning as the player 1 globals:
extern DWORD g_CoopPadBtnWord;       // raw PSX word from the pad (set by ReadPadBoth)
extern DWORD g_CoopRawPadHeld;       // raw held this frame
extern DWORD g_CoopPadPressed;       // rising edges of the raw word
extern WORD  g_CoopDpadHeld;         // remapped through g_padRemapTable (as g_PlayerDpadHeld)
extern WORD  g_CoopDpadHeldPrev;
extern WORD  g_CoopDpadPressed;      // rising edges of the remapped word

// Called once per frame at the end of PlayerPad_Update.
void CoopPad_Update(void);
