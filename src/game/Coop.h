// Coop.h - two-player co-op mod (port addition, not in the original binary)
//
// Player 1 is the keyboard, player 2 is the first pad. The original ORs both
// into one pad word (ReadPadBoth); with [Coop] Enable=1 they are split so each
// player gets their own PSX button word, edge detection and remapped dpad word.
#pragma once
#include "../Globals.h"

extern int   g_coopEnabled;          // [Coop] Enable - 0 = original behaviour
extern int   g_coopFreezeEnemies;    // [Coop] FreezeEnemies - dev tool: enemies stop after their init
extern int   g_coopAnimBrowser;      // [Coop] AnimBrowser - dev tool: P2's pad steps her animations
extern int   g_coopSharedInput;      // >0 while both pads drive player 1's input (save screen)
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

// Doors need both players: returns 1 when there is no partner in the room, or
// when she stands inside the door's action zone (record[0..3] = x, z, width,
// depth, as cmd_door_set stores it) grown by a margin on every side.
int Coop_PartnerNearDoor(const unsigned short* zone);
// Is (x, z) inside the zone grown by the same margin?
int Coop_PointNearZone(int x, int z, const unsigned short* zone);

// PlayerAnimations.cpp - player 2 pressed action. The first action-press room
// entry her reach probe hits runs as check_action_object would run it for
// player 1: a door only when player 1 is at it too (door_try_enter, as player
// 1, so the transition is his); anything else as her (Coop_BeginP2Action), so a
// picked-up item lands in her slots.
void coop_partner_try_action(Entity* partner);

// ---- Partner inventory -------------------------------------------------------
// Player 2 has her own item slots (g_RebeccaItemSlots, which the save already
// carries) and opens the ordinary item menu from her pad. While her menu is open
// the game runs "as her": the player entity's id, health and equipped item are
// swapped for hers, the item menu reads her slots and her pad drives it, and
// everything is swapped back when the menu closes.
extern int g_coopMenuOwner;          // 2 while the game runs as player 2 (her menu / action), else 0

// Player 2's own status. Not in the save yet: a continued game resets it.
struct CoopPartnerState {
	int            valid;
	short          health;
	unsigned char  maxHealth;
	unsigned char  healthStatusFlags;
	unsigned char  equippedItemId;   // 1-based slot index, like g_EquippedItemId
	unsigned char  equippedWeaponId; // item id, like PlayerEntity::equippedWeaponId
};
extern CoopPartnerState g_coopP2;

// New game: Rebecca's starting status (her slots are set by SetInitialItems).
void Coop_ResetPartnerState(void);
// game_loop: player 2 pressed her inventory button this frame.
int  Coop_P2WantsMenu(void);
// game_loop: player 2 asked for the item menu (taken up when it opens).
void Coop_RequestP2Menu(void);
// main_menu entry: switch to player 2 if she asked for this menu.
void Coop_BeginP2Menu(void);
// menu_restore_game_state: switch back to player 1 as the menu closes.
void Coop_EndP2Menu(void);
// Player 2 pressed action at a room object (item, item box, typewriter...):
// run the game as her until the message / prompt / menu it starts is over.
void Coop_BeginP2Action(void);
// Room actions player 2 leaves to player 1 - scripted room events, and items
// that are his to find: returns 0 (and shows "Maybe Chris should check it
// out.") when the room action entry is one of them.
int  Coop_PartnerMayTake(const unsigned char* entry);
// Typewriter: the save itself always runs as player 1 (it records the main
// character), but the ink ribbon can be player 2's. coop_partner_try_action
// starts the typewriter with her ribbon (Coop_SetSaveRibbonFromP2); the save
// screen then spends it from her slots (Coop_UseSaveRibbon returns 1 when it
// did). check_typewriter clears the claim when player 1 starts one.
void Coop_SetSaveRibbonFromP2(int fromP2);
int  Coop_UseSaveRibbon(void);
// Save file: player 2's status in a 16-byte block of the save's unused gap
// ("CO" + version + fields). A save without it (older, or made with co-op
// off) resets her status to a new game's.
void Coop_WriteSaveBlock(unsigned char* block);
void Coop_ReadSaveBlock(const unsigned char* block);
// ---- Partner combat ----
// The gun she has equipped that she can use - Beretta, shotgun, Colt Python,
// flamethrower, rocket launcher - or 0.
int  Coop_PartnerGunId(void);
// Any weapon she can use - the knife or one of the guns - or 0.
int  Coop_PartnerWeaponId(void);
// Is a handgun equipped in her slots?
int  Coop_PartnerHasHandgun(void);
// Spend one round from her equipped weapon's slot; 0 when it is empty.
int  Coop_PartnerUseAmmo(void);
// Rounds left in her equipped weapon's slot.
int  Coop_PartnerAmmo(void);
// Does she carry ammo for her equipped gun (item weaponId + 9)?
int  Coop_PartnerHasAmmoFor(int weaponId);
// The reload's transfer: her gun from her fullest stack of its ammo, capped at
// the gun's maximum (fire_consume_ammo_stack, on her slots).
void Coop_PartnerReloadFromStack(int weaponId);
// apply_weapon_damage reads the shooter from g_playerEntity (position, facing,
// aim bits in flags - 0x40 ahead, 0x80 up, 0x20 down, as weaponAimFlags - and
// id for the range table). Lend it hers for one shot.
unsigned char Coop_PartnerApplyWeaponDamage(Entity* partner, unsigned int weaponId,
                                            unsigned char aimBits);
// SoundSystem.cpp - her own copy of a weapon's sound set, and playing from it.
void Coop_LoadP2WeaponSounds(int weaponId);
void Coop_PlayP2WeaponSnd(int soundId, int pos);
// ---- Enemies vs the partner (zombies first) ----
// Every enemy reads "the player" from g_playerEntity, so she gets a stand-in
// player record of her own (g_coopP2Shadow: her position, facing, health,
// collision volumes and skeleton). An enemy that targets her runs its update
// with the stand-in swapped into g_playerEntity (and her pad into the player's
// pad words); while an enemy holds her (grab, knock-back) the player's own
// reaction code runs on the stand-in, animating her skeleton.
extern PlayerEntity g_coopP2Shadow;
extern int          g_coopP2Down;   // she died: Chris goes on alone until a save

// update_entities, before the enemy loop: refresh the stand-in from her.
void Coop_PreEnemyUpdate(void);
// update_entities, per enemy: runs it against her when she is its target.
// Returns 1 when it ran the update, 0 to let the caller run it as usual.
int  Coop_EnemyUpdate(void* updateFunc);
// Partner controller: is an enemy holding her (the player code drives her)?
int  Coop_PartnerInReaction(void);
// PlayerAnimations.cpp - update_player_anim without the player-only extras.
void coop_update_shadow_anim(void);
// Partner controller: one frame of the player's reaction code on her stand-in.
void Coop_RunPartnerReaction(Entity* partner);
// Save screen: a save brings her back.
void Coop_OnSave(void);
// room_set, after the enemy models: her shotgun and knife hand meshes,
// textured. Her own sheet has no art for them, so they are Jill's (W13 / W11 on
// CHAR11's TIM, loaded into the next 2 free texture banks), or player 1's own on
// his sheet when that would leave the room's occlusion masks no banks below 16.
void Coop_LoadPartnerWeaponModels(void);
// Her weapon models beyond the handgun: shotgun, knife, Colt Python,
// flamethrower, rocket launcher. Mesh `index` (ready for
// JointStruct::anim_slot_ptr, or 0) and the item it is.
#define COOP_WEAPON_MODELS 5
unsigned int Coop_PartnerWeaponMesh(int index, unsigned char* itemId);
// Effects player 2 spawns (her flamethrower's flames) do their damage with
// "the player's" aim bits, which effect behaviour 40 latches from
// g_playerEntity.flags while they fly. Effect slots taken between Begin and End
// are hers, with her bits; Effect_CreateBillboard notes every slot it takes.
void Coop_BeginP2Effects(unsigned char aimFlags);
void Coop_EndP2Effects(void);
void Coop_NoteEffectSlot(int slot);
// The flags behaviour 40 should latch for effect `slot`.
unsigned char Coop_EffectFlags(int slot, unsigned char playerFlags);
// Does player 2 (Rebecca, in the room) carry this item?
int  Coop_PartnerHasItem(unsigned char itemId);
// game_loop, once per gameplay frame: ends her action when the game is free.
void Coop_FrameUpdate(void);

// CharacterNpc.cpp - runs in place of the NPC dispatch for g_coopPartner.
void npc_coop_partner_control(void);

// Called from InitializeGame on a new game (after InitPlayerData). With
// [Coop] Enable and SkipOpening set, starts in the dining room (at the spot the
// main hall's door arrives at) with the opening cutscene, the "go back to the
// dining room" scene and the Tea Room zombie encounter marked as already done.
void Coop_ApplyStartRoom(void);
