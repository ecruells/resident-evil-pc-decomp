// Coop.cpp - two-player co-op mod (port addition, not in the original binary)
#include "Coop.h"
#include "BioCard.h"
#include "entities/EntityCommon.h"
#include "PrintText.h"

extern void Flg_on(int baseAddr, unsigned int bitIndex);   // CmdFunctions.cpp - 0x00473ef0

int   g_coopEnabled = 0;
int   g_coopSharedInput = 0;
int   g_coopAnimBrowser = 0;
int   g_coopFreezeEnemies = 0;
int   g_coopSkipOpening = 0;

DWORD g_CoopPadBtnWord = 0;
DWORD g_CoopRawPadHeld = 0;
DWORD g_CoopPadPressed = 0;
WORD  g_CoopDpadHeld = 0;
WORD  g_CoopDpadHeldPrev = 0;
WORD  g_CoopDpadPressed = 0;

Entity* g_coopPartner = nullptr;
int     g_coopPartnerPlaced = 0;

// Mirrors parts 2-6 of PlayerPad_Update (0x0044e000) for the second pad.
// The edge history always follows the real pad; while the game runs as her
// (her menu or action, g_coopMenuOwner == 2) her pad drives player 1's words
// instead, so the published words are blanked - without breaking the history,
// or a button still held when that ends would count as a fresh press.
void CoopPad_Update(void)
{
	static DWORD s_rawHeld = 0;
	static WORD  s_dpadHeld = 0;

	if (!g_coopEnabled) {
		return;
	}

	const WORD* pRemapTable = g_padRemapTable[g_controllerConfig & 3];

	// Attract demo plays back player 1 only; player 2 stays idle.
	DWORD newRaw = ((g_main_state_flags2 & MSF2_ATTRACT_DEMO) == 0) ? g_CoopPadBtnWord : 0;

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

	DWORD rawPressed = ~s_rawHeld & newRaw;
	WORD  dpadPressed = (WORD)(~s_dpadHeld & held);
	s_rawHeld = newRaw;
	s_dpadHeld = held;

	g_CoopDpadHeldPrev = g_CoopDpadHeld;
	if (g_coopMenuOwner == 2) {
		g_CoopRawPadHeld = 0;
		g_CoopPadPressed = 0;
		g_CoopDpadHeld = 0;
		g_CoopDpadPressed = 0;
	} else {
		g_CoopRawPadHeld = newRaw;
		g_CoopPadPressed = rawPressed;
		g_CoopDpadHeld = held;
		g_CoopDpadPressed = dpadPressed;
	}
}

// Spawns the partner the same way cmd_enemy_set (0x004617d0) spawns a script
// actor: fill a free g_EnemiesList slot and bump g_enemy_count, and room_set's
// model loop does the rest. Runs on every room load, so she follows player 1
// through doors; she is placed beside him on her first control frame.
void Coop_SpawnPartner(void)
{
	g_coopPartner = nullptr;
	g_coopPartnerPlaced = 0;

	if (!g_coopEnabled) {
		return;
	}
	// Only while player 1 is Chris or Jill (Rebecca is playable herself for a
	// stretch of Chris's game), and never during the attract-mode demo.
	if ((g_playerEntity.id & 3) >= 2 || (g_main_state_flags2 & MSF2_ATTRACT_DEMO) != 0) {
		return;
	}

	// Chris is paired with Rebecca, Jill with Barry.
	unsigned char partnerId = ((g_playerEntity.id & 1) != 0) ? NPC_BARRY : NPC_REBECCA;

	// A room script that already put this character in the room wins.
	for (int i = 0; i < 30; i++) {
		if ((g_EnemiesList[i].status_flags & 1) != 0 && g_EnemiesList[i].id == partnerId) {
			dbg_printf("[COOP] room already has character 0x%02X, no partner\n", partnerId);
			return;
		}
	}

	// Highest free slot: scripts fill the low ones, and an event that runs later
	// may still cmd_enemy_set into any of them.
	int slot = -1;
	for (int i = 29; i >= 0; i--) {
		if ((g_EnemiesList[i].status_flags & 1) == 0) {
			slot = i;
			break;
		}
	}
	if (slot < 0) {
		dbg_printf("[COOP] no free enemy slot for the partner\n");
		return;
	}

	Entity* e = &g_EnemiesList[slot];
	memset(e, 0, sizeof(*e));

	e->status_flags = 1;
	// behavior_flags picks the weapon model her init loads into her hand
	// (LoadCharacterWeaponTmd): 2 = the handgun, whose firing effects are the
	// NPC tables' weapon index 0.
	// Rebecca always gets it: the controller swaps the gun in and out of her
	// hand as she equips it (npc_coop_partner_control).
	e->behavior_flags = (partnerId == NPC_REBECCA) ? 2 : 0;
	if (Coop_PartnerHasHandgun()) {
		Coop_LoadP2WeaponSounds(ITEM_BERETTA);
	}
	e->scaMatrixData.localMatrix.t[0] = (unsigned int)(int)g_playerEntity.scaMatrixData.localMatrix.t[0];
	e->scaMatrixData.localMatrix.t[1] = 0;
	e->scaMatrixData.localMatrix.t[2] = (unsigned int)(int)g_playerEntity.scaMatrixData.localMatrix.t[2];
	e->position.x = (short)e->scaMatrixData.localMatrix.t[0];
	e->position.y = 0;
	e->position.z = (short)e->scaMatrixData.localMatrix.t[2];
	e->angle = g_playerEntity.directionAngle;
	e->animationId = 0;
	e->animation_frame_id = 0;
	e->timing_control = 1;

	e->state = 0;
	e->ignore_player_flag = 0;
	e->action_behavior = 0;
	e->action_state = 0;
	e->id = partnerId;
	e->death_event_id = 0xff;          // no kill flag
	e->hit_state = 0;
	e->collisionFlags = 0;
	e->lookAtFlags = 0;
	e->pad_160[1] = (unsigned char)(slot | 0x80);   // as cmd_enemy_set: slot + "spawned unconditionally"
	e->Sca_info = g_scaDataTable[0];
	e->pSca_hit_data = g_scaPoolPtr;
	g_scaPoolPtr += 6;
	g_enemy_count++;

	g_coopPartner = e;
	dbg_printf("[COOP] partner id 0x%02X spawned in enemy slot %d (stage %d room 0x%02X)\n",
	           partnerId, slot, (int)g_stageId, (int)g_roomId);
}

#define COOP_DOOR_MARGIN 3000

int Coop_PartnerNearDoor(const unsigned short* zone)
{
	if (g_coopPartner == nullptr) {
		return 1;
	}
	return Coop_PointNearZone(g_coopPartner->scaMatrixData.localMatrix.t[0],
	                          g_coopPartner->scaMatrixData.localMatrix.t[2], zone);
}

int Coop_PointNearZone(int x, int z, const unsigned short* zone)
{
	int x0 = (int)zone[0] - COOP_DOOR_MARGIN;
	int z0 = (int)zone[1] - COOP_DOOR_MARGIN;
	int x1 = (int)zone[0] + (int)zone[2] + COOP_DOOR_MARGIN;
	int z1 = (int)zone[1] + (int)zone[3] + COOP_DOOR_MARGIN;
	return (x >= x0 && x <= x1 && z >= z0 && z <= z1) ? 1 : 0;
}

// Scenario flags 2 (bank 1) bit numbers, read with tools/mine_room_scd.py and
// tools/evt_disasm.py:
//   0     ROOM1060 init: runs the opening cutscene while clear, then sets it
//   2     ROOM1040 event 2 (the zombie encounter) sets it; ROOM1060 plays its
//         "go back to the dining room" scene on every entry while it is clear
//   0xA7  set by the ROOM1060 opening block; ROOM1050 shows a table item for it
void Coop_ApplyStartRoom(void)
{
	if (!g_coopEnabled || !g_coopSkipOpening) {
		return;
	}
	// Arrival spot from ROOM1060 door_set slot 2 (dest 05): x=30900 z=8300 ang=0x800.
	g_stageId = STAGE_MANSION_1F;
	g_roomId = ROOM_DINING_ROOM;
	g_playerEntity.position.x = 30900;
	g_playerEntity.position.z = 8300;
	g_playerEntity.directionAngle = 0x800;

	Flg_on((int)g_ScenarioFlags2, 0);
	Flg_on((int)g_ScenarioFlags2, 2);
	Flg_on((int)g_ScenarioFlags2, 0xA7);
	dbg_printf("[COOP] new game starts in the dining room, opening skipped\n");
}

// ============================================================================
// Partner inventory
// ============================================================================
int g_coopMenuOwner = 0;
CoopPartnerState g_coopP2 = { 0, 0, 0, 0, 0, 0 };

// Player 1's state while player 2's menu is open.
static struct {
	unsigned char id;
	short         health;
	unsigned char maxHealth;
	unsigned char healthStatusFlags;
	unsigned char equippedItemId;
	unsigned char equippedWeaponId;
	void*         itemSlots;
} s_p1;

// Rebecca's numbers from room_set's character switch (RoomInit.cpp): 88 health,
// nothing equipped. Her slots get the Beretta and 15 rounds in SetInitialItems.
void Coop_ResetPartnerState(void)
{
	g_coopP2.valid = 1;
	g_coopP2.health = 88;
	g_coopP2.maxHealth = 88;
	g_coopP2.healthStatusFlags = 0;
	g_coopP2.equippedItemId = 0;
	g_coopP2.equippedWeaponId = 0;
}

int Coop_P2WantsMenu(void)
{
	// Rebecca only for now: Barry would get Jill's 8-slot menu over the 6-slot
	// array.
	if (!g_coopEnabled || g_coopPartner == nullptr || g_coopPartner->id != NPC_REBECCA) {
		return 0;
	}
	// 0x800 in the raw word is the inventory button, as game_loop tests for P1.
	return ((g_CoopPadPressed >> 8) & 8) != 0;
}

static int s_p2MenuRequested = 0;

void Coop_RequestP2Menu(void)
{
	s_p2MenuRequested = 1;
}

// What player 2 is doing while the game runs as her (g_coopMenuOwner == 2).
enum { P2_SESSION_NONE, P2_SESSION_MENU, P2_SESSION_ACTION };
static int s_p2Session = P2_SESSION_NONE;
static int s_p2ActionFrames = 0;     // frames since her action started
static int s_p2ActionBlocked = 0;    // a message / menu has taken over since

static void p2_swap_in(void)
{
	if (!g_coopP2.valid) {
		Coop_ResetPartnerState();
	}

	s_p1.id = g_playerEntity.id;
	s_p1.health = g_playerEntity.health;
	s_p1.maxHealth = g_playerEntity.maxHealth;
	s_p1.healthStatusFlags = g_playerEntity.healthStatusFlags;
	s_p1.equippedItemId = g_EquippedItemId;
	s_p1.equippedWeaponId = g_playerEntity.equippedWeaponId;
	s_p1.itemSlots = g_ItemSlotsPointer;

	g_playerEntity.id = 3;   // Rebecca: her portrait, 6 slots, her map rules
	g_playerEntity.health = g_coopP2.health;
	g_playerEntity.maxHealth = g_coopP2.maxHealth;
	g_playerEntity.healthStatusFlags = g_coopP2.healthStatusFlags;
	g_EquippedItemId = g_coopP2.equippedItemId;
	g_playerEntity.equippedWeaponId = g_coopP2.equippedWeaponId;
	g_ItemSlotsPointer = g_RebeccaItemSlots;
	LoadHeldItemsImages();   // her item icons, count and slot indices

	g_coopMenuOwner = 2;
}

static void p2_swap_out(void)
{
	g_coopMenuOwner = 0;

	unsigned char newWeapon = g_playerEntity.equippedWeaponId;
	g_coopP2.health = g_playerEntity.health;
	g_coopP2.maxHealth = g_playerEntity.maxHealth;
	g_coopP2.healthStatusFlags = g_playerEntity.healthStatusFlags;
	g_coopP2.equippedItemId = g_EquippedItemId;

	g_playerEntity.id = s_p1.id;
	g_playerEntity.health = s_p1.health;
	g_playerEntity.maxHealth = s_p1.maxHealth;
	g_playerEntity.healthStatusFlags = s_p1.healthStatusFlags;
	g_EquippedItemId = s_p1.equippedItemId;
	g_playerEntity.equippedWeaponId = s_p1.equippedWeaponId;
	g_ItemSlotsPointer = s_p1.itemSlots;
	LoadHeldItemsImages();

	// The menu loads a newly equipped weapon into the PLAYER's weapon model and
	// sound bank. If she changed weapon, put player 1's back.
	if (newWeapon != g_coopP2.equippedWeaponId) {
		g_coopP2.equippedWeaponId = newWeapon;
		if (Coop_PartnerHasHandgun()) {
			Coop_LoadP2WeaponSounds(ITEM_BERETTA);
		}
		LoadSoundBank(g_playerEntity.equippedWeaponId, g_TimImageBuffer__bitmap);
		LoadEquippedWeaponAnimation(g_playerEntity.equippedWeaponId, 0xE,
		                            (unsigned int)g_animationBuffer,
		                            (unsigned int)g_animObjectBuffer);
	}
	dbg_printf("[COOP] back to player 1 (P2: equipped item %d, weapon 0x%02X, health %d)\n",
	           g_coopP2.equippedItemId, g_coopP2.equippedWeaponId, g_coopP2.health);
}

void Coop_BeginP2Menu(void)
{
	if (!s_p2MenuRequested) {
		return;
	}
	s_p2MenuRequested = 0;
	if (s_p2Session != P2_SESSION_NONE) {
		return;
	}
	p2_swap_in();
	s_p2Session = P2_SESSION_MENU;
	dbg_printf("[COOP] player 2 opened her item menu\n");
}

void Coop_EndP2Menu(void)
{
	// A menu that opens during one of her actions (the item box, the pick-up
	// screen) closes back into that action; Coop_FrameUpdate ends it.
	if (s_p2Session != P2_SESSION_MENU) {
		return;
	}
	s_p2Session = P2_SESSION_NONE;
	p2_swap_out();
}

void Coop_BeginP2Action(void)
{
	if (s_p2Session != P2_SESSION_NONE) {
		return;
	}
	p2_swap_in();
	s_p2Session = P2_SESSION_ACTION;
	s_p2ActionFrames = 0;
	s_p2ActionBlocked = 0;
}

// Free = no message on screen (set_message_display raises g_menu_choice_id
// bit 0x80 for as long as one is up, even when it does not pause the game),
// player 1's input enabled (bit 0x100: no prompt or scripted event holds the
// game), and no menu pending or open.
static int gameplay_is_free(void)
{
	return (g_menu_choice_id & 0x80) == 0 &&
	       (g_message_flags & 0x100) != 0 &&
	       (g_main_state_flags & (MSF_MENU_ACTIVE | MSF_MENU_PENDING)) == 0 &&
	       g_openMenuFlag == 0;
}

// The game has to stay free this many frames in a row before her action ends:
// a multi-page message lets go for a frame or two between its pages.
#define P2_ACTION_SETTLE_FRAMES 10

void Coop_FrameUpdate(void)
{
	static int s_freeFrames = 0;

	if (s_p2Session != P2_SESSION_ACTION) {
		s_freeFrames = 0;
		return;
	}
	s_p2ActionFrames++;
	if (!gameplay_is_free()) {
		s_p2ActionBlocked = 1;
		s_freeFrames = 0;
		return;
	}
	s_freeFrames++;
	// Done once whatever her action started (message, prompt, item box) has
	// finished and stayed finished - or if it never started anything.
	if (s_freeFrames >= P2_ACTION_SETTLE_FRAMES) {
		s_freeFrames = 0;
		s_p2Session = P2_SESSION_NONE;
		p2_swap_out();
	}
}

int Coop_PartnerHasItem(unsigned char itemId)
{
	if (!g_coopEnabled || g_coopPartner == nullptr || g_coopPartner->id != NPC_REBECCA ||
	    g_coopMenuOwner == 2) {
		return 0;
	}
	for (int i = 0; i < 6; i++) {
		if (g_RebeccaItemSlots[i].Id == itemId) {
			return 1;
		}
	}
	return 0;
}

// Co-op's own line, shown through the global message table's spare last slot
// (index 63 is NULL in both the USA and the JPN table).
static constexpr auto s_msgChrisShouldCheck = STR("Maybe Chris should\\ncheck it out.");
#define COOP_MSG_SLOT 63

static void coop_show_message(const unsigned char* encoded)
{
	global_messages[COOP_MSG_SLOT] = (unsigned char*)encoded;
	global_messages_jpn[COOP_MSG_SLOT] = (unsigned char*)encoded;
	set_message_display(0x40 | COOP_MSG_SLOT, 0xff);
}

int Coop_PartnerMayTake(const unsigned char* entry)
{
	int slot = (int)(entry - g_RoomActionTable) / 12;
	const unsigned char* record = *(const unsigned char* const*)(entry + 8);
	unsigned char itemId = (record != nullptr) ? record[8] : 0;
	if (g_coopPartner == nullptr) {
		return 1;
	}
	// Scripted room events (room_check_actions 9, create_room_event) are written
	// for the player character - the scene would play on player 1 - so only he
	// starts them.
	if (entry[0] == 9) {
		coop_show_message(s_msgChrisShouldCheck.bytes);
		return 0;
	}
	// Jill's handgun in the main hall: ROOM1060 item_model_set slot 6 (Beretta,
	// 15 rounds). Its init script turns the slot into room event 0x0B - Chris
	// crouches, "It's Jill's gun", then the pick-up prompt - so it is matched by
	// slot, not by handler. It is his scene.
	if (g_coopPartner != nullptr && g_coopPartner->id == NPC_REBECCA &&
	    g_stageId == STAGE_MANSION_1F && g_roomId == ROOM_MAIN_HALL &&
	    slot == 6 && itemId == ITEM_BERETTA) {
		coop_show_message(s_msgChrisShouldCheck.bytes);
		return 0;
	}
	return 1;
}

static int s_saveRibbonFromP2 = 0;

void Coop_SetSaveRibbonFromP2(int fromP2)
{
	s_saveRibbonFromP2 = fromP2;
}

int Coop_UseSaveRibbon(void)
{
	if (!s_saveRibbonFromP2) {
		return 0;
	}
	s_saveRibbonFromP2 = 0;

	// use_room_action_item works on the active slots: point them at hers for
	// the one call, then back.
	void* p1Slots = g_ItemSlotsPointer;
	g_ItemSlotsPointer = g_RebeccaItemSlots;
	LoadHeldItemsImages();
	g_selectedItemId = ITEM_INK_RIBBONS;
	use_room_action_item();
	g_ItemSlotsPointer = p1Slots;
	LoadHeldItemsImages();
	dbg_printf("[COOP] save paid with player 2's ink ribbon\n");
	return 1;
}

#define COOP_SAVE_VERSION 1

void Coop_WriteSaveBlock(unsigned char* block)
{
	memset(block, 0, 16);
	if (!g_coopP2.valid) {
		return;
	}
	block[0] = 'C';
	block[1] = 'O';
	block[2] = COOP_SAVE_VERSION;
	block[3] = g_coopP2.maxHealth;
	block[4] = (unsigned char)(g_coopP2.health & 0xff);
	block[5] = (unsigned char)((unsigned short)g_coopP2.health >> 8);
	block[6] = g_coopP2.healthStatusFlags;
	block[7] = g_coopP2.equippedItemId;
	block[8] = g_coopP2.equippedWeaponId;
}

void Coop_ReadSaveBlock(const unsigned char* block)
{
	if (block[0] != 'C' || block[1] != 'O' || block[2] != COOP_SAVE_VERSION) {
		Coop_ResetPartnerState();
		return;
	}
	g_coopP2.valid = 1;
	g_coopP2.maxHealth = block[3];
	g_coopP2.health = (short)(block[4] | (block[5] << 8));
	g_coopP2.healthStatusFlags = block[6];
	g_coopP2.equippedItemId = block[7];
	g_coopP2.equippedWeaponId = block[8];
	dbg_printf("[COOP] save: player 2 health %d/%d, equipped item %d\n",
	           g_coopP2.health, g_coopP2.maxHealth, g_coopP2.equippedItemId);
}

// ============================================================================
// Partner combat (handgun prototype)
// ============================================================================
extern unsigned char apply_weapon_damage(unsigned int weapon_id);   // WeaponDamage.cpp 0x0043c020

int Coop_PartnerHasHandgun(void)
{
	return g_coopP2.valid && g_coopP2.equippedWeaponId == ITEM_BERETTA;
}

int Coop_PartnerUseAmmo(void)
{
	unsigned char slot = g_coopP2.equippedItemId;
	if (slot == 0 || slot > 6) {
		return 0;
	}
	ItemSlot* s = &g_RebeccaItemSlots[slot - 1];
	if (s->qty == 0) {
		return 0;
	}
	s->qty--;
	return 1;
}

unsigned char Coop_PartnerApplyWeaponDamage(Entity* partner, unsigned int weaponId)
{
	int  t0 = g_playerEntity.scaMatrixData.localMatrix.t[0];
	int  t1 = g_playerEntity.scaMatrixData.localMatrix.t[1];
	int  t2 = g_playerEntity.scaMatrixData.localMatrix.t[2];
	short angle = g_playerEntity.directionAngle;
	unsigned char flags = g_playerEntity.flags;
	unsigned char id = g_playerEntity.id;

	g_playerEntity.scaMatrixData.localMatrix.t[0] = partner->scaMatrixData.localMatrix.t[0];
	g_playerEntity.scaMatrixData.localMatrix.t[1] = partner->scaMatrixData.localMatrix.t[1];
	g_playerEntity.scaMatrixData.localMatrix.t[2] = partner->scaMatrixData.localMatrix.t[2];
	g_playerEntity.directionAngle = partner->angle;
	g_playerEntity.flags = (unsigned char)((flags & ~0xE0) | 0x40);   // aiming straight ahead
	g_playerEntity.id = 3;                                             // Rebecca's weapon ranges

	unsigned char hit = apply_weapon_damage(weaponId);

	g_playerEntity.scaMatrixData.localMatrix.t[0] = t0;
	g_playerEntity.scaMatrixData.localMatrix.t[1] = t1;
	g_playerEntity.scaMatrixData.localMatrix.t[2] = t2;
	g_playerEntity.directionAngle = angle;
	g_playerEntity.flags = flags;
	g_playerEntity.id = id;
	dbg_printf("[COOP] player 2 shot: result %d\n", hit);
	return hit;
}
