// Coop.h - two-player co-op mod (port addition, not in the original binary)
//
// Player 1 is the keyboard, player 2 is the first pad. The original ORs both
// into one pad word (ReadPadBoth); with [Coop] Enable=1 they are split so each
// player gets their own PSX button word, edge detection and remapped dpad word.
#pragma once
#include "../Globals.h"

extern int   g_coopEnabled;          // [Coop] Enable - 0 = original behaviour
extern int   g_coopSkipOpening;      // [Coop] SkipOpening - new games start in the dining room, opening done

// Player 2 input, same layout and meaning as the player 1 globals:
extern DWORD g_CoopPadBtnWord;       // raw PSX word from the pad (set by ReadPadBoth)
extern DWORD g_CoopRawPadHeld;       // raw held this frame
extern DWORD g_CoopPadPressed;       // rising edges of the raw word
extern WORD  g_CoopDpadHeld;         // remapped through g_padRemapTable (as g_PlayerDpadHeld)
extern WORD  g_CoopDpadHeldPrev;
extern WORD  g_CoopDpadPressed;      // rising edges of the remapped word

// Called once per frame at the end of PlayerPad_Update.
void CoopPad_Update(void);

// ---- Partner (player 2's character) -----------------------------------------
// Rebecca (Chris) / Barry (Jill) is spawned as an ordinary character entity in
// g_EnemiesList, so the room loader gives her a model and texture bank of her
// own. character_npc_update hands her to npc_coop_partner_control, which drives
// her from the player 2 pad instead of the SCD-driven state machine.
extern Entity* g_coopPartner;        // NULL when there is no partner in this room
extern int     g_coopPartnerPlaced;  // 0 until the first control frame positions her

// Called from room_set after the room script has run, before the enemy model
// loop. Resets g_coopPartner and spawns her if this room supports it.
void Coop_SpawnPartner(void);

// CharacterNpc.cpp - runs in place of the NPC dispatch for g_coopPartner.
void npc_coop_partner_control(void);

// Called from InitializeGame on a new game (after InitPlayerData). With
// [Coop] Enable and SkipOpening set, starts in the dining room (at the spot the
// main hall's door arrives at) with the opening cutscene, the "go back to the
// dining room" scene and the Tea Room zombie encounter marked as already done.
void Coop_ApplyStartRoom(void);
