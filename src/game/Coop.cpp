// Coop.cpp - two-player co-op mod (port addition, not in the original binary)
#include "Coop.h"
#include "BioCard.h"
#include "entities/EntityCommon.h"
#include "PrintText.h"
#include "FileLoader.h"
#include "../system/AssetPath.h"

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

// Texture banks. room_set hands them out from 6 upward (enemy models in
// g_EnemiesList order, 2 banks each, a run of the same model sharing one load),
// and load_room_masks then puts the camera's occlusion masks into the next ones.
// Room textures have to stay below bank 16: 16 and up is the second VRAM row
// (TexturePage_SetupFull maps them to y = 256), where the fixed sheets live -
// the player's own is 0x16. Pushed up there, the masks stop occluding and
// everything behind the room's walls shows through. Keep this many banks free
// for them.
#define COOP_MODEL_BANKS 2
#define COOP_MASK_BANKS  2
#define COOP_ROOM_BANK_LIMIT 16

// Banks the enemy loop will have used once it has loaded every model now in
// g_EnemiesList, counting from where the room script left the counter.
static int room_banks_after_models(void)
{
	int bank = g_TextureBankID;
	int last = -1;
	for (int i = 0; i < 30; i++) {
		if ((g_EnemiesList[i].status_flags & 1) != 0) {
			if (g_EnemiesList[i].id != last) {
				bank += COOP_MODEL_BANKS;
			}
			last = g_EnemiesList[i].id;
		}
	}
	return bank;
}

// Bumped on every spawn: she lands in the same g_EnemiesList slot room after
// room, so the pointer alone cannot tell a stand-in built for the last room's
// skeleton (in that room's, since reused, load buffer) from a current one.
static int s_spawnSerial = 0;

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

	if (!g_coopEnabled || g_coopP2Down) {
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

	// Her model takes 2 texture banks of its own. In a room so full of models
	// that they would push the occlusion masks out of the room's banks, she
	// sits it out rather than break the walls for both of them.
	if (room_banks_after_models() + COOP_MODEL_BANKS + COOP_MASK_BANKS > COOP_ROOM_BANK_LIMIT) {
		dbg_printf("[COOP] no texture banks left for player 2 in this room (models up to %d)\n",
		           room_banks_after_models());
		return;
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
	// Rebecca always gets it, and char_init_rebecca adds the shotgun
	// (Coop_LoadPartnerWeaponModels): the controller swaps the bare hand, the
	// handgun and the shotgun as she equips them (npc_coop_partner_control).
	e->behavior_flags = (partnerId == NPC_REBECCA) ? 2 : 0;
	if (Coop_PartnerWeaponId() != 0) {
		Coop_LoadP2WeaponSounds(Coop_PartnerWeaponId());
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
	s_spawnSerial++;
	dbg_printf("[COOP] partner id 0x%02X spawned in enemy slot %d (stage %d room 0x%02X)\n",
	           partnerId, slot, (int)g_stageId, (int)g_roomId);
}

#define COOP_DOOR_MARGIN 3000

int Coop_PartnerNearDoor(const unsigned short* zone)
{
	if (g_coopPartner == nullptr || g_coopP2Down) {
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
	g_coopP2Down = 0;
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
	if (!g_coopEnabled || g_coopPartner == nullptr || g_coopPartner->id != NPC_REBECCA ||
	    g_coopP2Down) {
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
		if (Coop_PartnerWeaponId() != 0) {
			Coop_LoadP2WeaponSounds(Coop_PartnerWeaponId());
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

int Coop_PartnerGunId(void)
{
	if (!g_coopP2.valid) {
		return 0;
	}
	if (g_coopP2.equippedWeaponId == ITEM_BERETTA || g_coopP2.equippedWeaponId == ITEM_SHOTGUN) {
		return g_coopP2.equippedWeaponId;
	}
	return 0;
}

int Coop_PartnerWeaponId(void)
{
	if (!g_coopP2.valid) {
		return 0;
	}
	if (g_coopP2.equippedWeaponId == ITEM_KNIFE) {
		return ITEM_KNIFE;
	}
	return Coop_PartnerGunId();
}

int Coop_PartnerHasHandgun(void)
{
	return Coop_PartnerGunId() == ITEM_BERETTA;
}

int Coop_PartnerAmmo(void)
{
	unsigned char slot = g_coopP2.equippedItemId;
	if (slot == 0 || slot > 6) {
		return 0;
	}
	return g_RebeccaItemSlots[slot - 1].qty;
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

unsigned char Coop_PartnerApplyWeaponDamage(Entity* partner, unsigned int weaponId,
                                            unsigned char aimBits)
{
	int  t0 = g_playerEntity.scaMatrixData.localMatrix.t[0];
	int  t1 = g_playerEntity.scaMatrixData.localMatrix.t[1];
	int  t2 = g_playerEntity.scaMatrixData.localMatrix.t[2];
	short angle = g_playerEntity.directionAngle;
	unsigned char flags = g_playerEntity.flags;
	unsigned char id = g_playerEntity.id;
	JointStruct* joints = g_playerEntity.jointsStructs;

	g_playerEntity.scaMatrixData.localMatrix.t[0] = partner->scaMatrixData.localMatrix.t[0];
	g_playerEntity.scaMatrixData.localMatrix.t[1] = partner->scaMatrixData.localMatrix.t[1];
	g_playerEntity.scaMatrixData.localMatrix.t[2] = partner->scaMatrixData.localMatrix.t[2];
	g_playerEntity.directionAngle = partner->angle;
	g_playerEntity.flags = (unsigned char)((flags & ~0xE0) | aimBits);
	g_playerEntity.id = 3;                                             // Rebecca's weapon ranges
	g_playerEntity.jointsStructs = partner->jointsStructs;             // the knife's reach: her hand

	unsigned char hit = apply_weapon_damage(weaponId);

	g_playerEntity.scaMatrixData.localMatrix.t[0] = t0;
	g_playerEntity.scaMatrixData.localMatrix.t[1] = t1;
	g_playerEntity.scaMatrixData.localMatrix.t[2] = t2;
	g_playerEntity.directionAngle = angle;
	g_playerEntity.flags = flags;
	g_playerEntity.id = id;
	g_playerEntity.jointsStructs = joints;
	dbg_printf("[COOP] player 2 hit check (weapon %d): result %d\n", weaponId, hit);
	return hit;
}

// ============================================================================
// Enemies vs the partner (zombies first)
// ============================================================================
PlayerEntity g_coopP2Shadow;
int          g_coopP2Down = 0;

static unsigned char s_enemyTargetsP2[30];   // per enemy slot: 1 = after player 2
static short         s_deathHold[30];        // frames an enemy stays on her body
static Entity* s_shadowOwner = nullptr;   // partner entity the stand-in was built for
static int     s_shadowSerial = -1;       // ...and the spawn it was built for
static int     s_shadowSwapped = 0;
static DWORD   s_swapPadHeld;
static WORD    s_swapDpadHeld, s_swapDpadPressed;

static int enemy_is_zombie_id(unsigned char id)
{
	return id == ENEMY_ZOMBIE || id == ENEMY_ZOMBIE_NAKED || id == ENEMY_ZOMBIE_VARIANT;
}

// Enemies that can go after her: the regular ones. Each was checked for
// player-only assumptions (the player model's 16th joint, player 1's flags,
// his inventory); kill behaviours that never end on their own (the original's
// game is over by then) get a release in Coop_EnemyUpdate as they turn up, as
// the dog's maul did. The bosses - Black Tiger, Plant 42, Neptune, Yawn, the
// Tyrant - stay player 1's until each has had its own pass.
static int enemy_targets_partner(const Entity* e)
{
	switch (e->id) {
	case ENEMY_ZOMBIE:
	case ENEMY_ZOMBIE_NAKED:
	case ENEMY_ZOMBIE_VARIANT:
	case ENEMY_CERBERUS:
	case ENEMY_WEB_SPINNER:
	case ENEMY_CROW:
	case ENEMY_HUNTER:
	case ENEMY_WASP:
	case ENEMY_CHIMERA:
	case ENEMY_ADDER:
	case ENEMY_MONSTER_PLANT:
		return 1;
	default:
		return 0;
	}
}

// After she dies, the enemies that were on her finish with her stand-in for
// this many frames (a dog's maul, a zombie's feeding) instead of turning that
// on player 1 mid-animation.
#define P2_DEATH_HOLD_FRAMES 90   // 3 seconds

// Fresh stand-in for this room's partner: player 1's record (weapon and room
// animation pointers, flags), then her own body.
static void shadow_build(Entity* partner)
{
	memcpy(&g_coopP2Shadow, &g_playerEntity, sizeof(PlayerEntity));
	PlayerEntity* sh = &g_coopP2Shadow;
	sh->id = 3;
	sh->Sca_info = partner->Sca_info;
	sh->pSca_hit_data = partner->pSca_hit_data;
	sh->jointsStructs = partner->jointsStructs;
	sh->jointCount = partner->jointCount;
	sh->animHeader = partner->animHeader;
	sh->animBase = partner->animBase;
	sh->animationId = 1;          // player_state_01_control: free
	sh->animFrameId = 0;
	sh->action_behavior = 0;
	sh->action_state = 0;
	sh->isBeingAttackedFlag = 0;
	sh->attackTimer = 0;
	sh->lookAtFlags = 0;
	s_shadowOwner = partner;
	s_shadowSerial = s_spawnSerial;
	for (int i = 0; i < 30; i++) {
		s_enemyTargetsP2[i] = 0;
		s_deathHold[i] = 0;
	}
}

// Her free body -> the stand-in.
static void shadow_from_partner(Entity* partner)
{
	PlayerEntity* sh = &g_coopP2Shadow;
	memcpy(&sh->scaMatrixData, &partner->scaMatrixData, sizeof(sh->scaMatrixData));
	sh->position = partner->position;
	sh->directionAngle = partner->angle;
	sh->health = g_coopP2.health;
	sh->maxHealth = g_coopP2.maxHealth;
	sh->healthStatusFlags = g_coopP2.healthStatusFlags;
	sh->flags = (unsigned char)((g_playerEntity.flags & ~0xE0) | 0x40);
	// The room's enemy reaction set (grabs) follows the room.
	sh->emdScratchPtr1 = g_playerEntity.emdScratchPtr1;
	sh->emdScratchPtr2 = g_playerEntity.emdScratchPtr2;
	sh->jointMoveData2 = g_playerEntity.jointMoveData2;
	sh->jointMoveData3 = g_playerEntity.jointMoveData3;
}

// The stand-in -> her body, after an enemy or the reaction code moved it.
static void shadow_to_partner(Entity* partner)
{
	partner->scaMatrixData.localMatrix.t[0] = g_coopP2Shadow.scaMatrixData.localMatrix.t[0];
	partner->scaMatrixData.localMatrix.t[1] = g_coopP2Shadow.scaMatrixData.localMatrix.t[1];
	partner->scaMatrixData.localMatrix.t[2] = g_coopP2Shadow.scaMatrixData.localMatrix.t[2];
	partner->position.x = (short)partner->scaMatrixData.localMatrix.t[0];
	partner->position.z = (short)partner->scaMatrixData.localMatrix.t[2];
	partner->angle = g_coopP2Shadow.directionAngle;
	g_coopP2.health = g_coopP2Shadow.health;
	g_coopP2.healthStatusFlags = g_coopP2Shadow.healthStatusFlags;
}

// Swap the stand-in and her pad words into player 1's place (and back).
static void shadow_swap(void)
{
	PlayerEntity tmp;
	memcpy(&tmp, &g_playerEntity, sizeof(PlayerEntity));
	memcpy(&g_playerEntity, &g_coopP2Shadow, sizeof(PlayerEntity));
	memcpy(&g_coopP2Shadow, &tmp, sizeof(PlayerEntity));

	if (!s_shadowSwapped) {
		s_swapPadHeld = g_PlayerPadHeld;
		s_swapDpadHeld = g_PlayerDpadHeld;
		s_swapDpadPressed = g_PlayerDpadPressed;
		g_PlayerPadHeld = g_CoopPadPressed;     // edge-detected raw word, as P1's
		g_PlayerDpadHeld = g_CoopDpadHeld;
		g_PlayerDpadPressed = g_CoopDpadPressed;
		s_shadowSwapped = 1;
	} else {
		g_PlayerPadHeld = s_swapPadHeld;
		g_PlayerDpadHeld = s_swapDpadHeld;
		g_PlayerDpadPressed = s_swapDpadPressed;
		s_shadowSwapped = 0;
	}
}

static int partner_targetable(void)
{
	return g_coopEnabled && g_coopPartner != nullptr && !g_coopP2Down &&
	       g_coopPartnerPlaced && s_shadowOwner == g_coopPartner &&
	       s_shadowSerial == s_spawnSerial;
}

static int shadow_dead(void)
{
	return g_coopP2Shadow.health < 0 || g_coopP2Shadow.action_behavior == 200;
}

static void partner_dies(void)
{
	if (g_coopP2Down) {
		return;
	}
	g_coopP2Down = 1;
	g_coopP2.health = -1;   // stays "dead" to the enemy finishing with her
	for (int i = 0; i < 30; i++) {
		s_deathHold[i] = s_enemyTargetsP2[i] ? P2_DEATH_HOLD_FRAMES : 0;
	}
	dbg_printf("[COOP] player 2 is down until the next save\n");
}

void Coop_PreEnemyUpdate(void)
{
	if (!g_coopEnabled || g_coopPartner == nullptr || !g_coopPartnerPlaced) {
		return;
	}
	if (s_shadowOwner != g_coopPartner || s_shadowSerial != s_spawnSerial) {
		shadow_build(g_coopPartner);
		shadow_from_partner(g_coopPartner);
	} else if (!Coop_PartnerInReaction()) {
		shadow_from_partner(g_coopPartner);
	}
}

// Nearest living player, sticky while an attack holds one of them.

int Coop_EnemyUpdate(void* updateFunc)
{
	if (!enemy_targets_partner(ENTITY)) {
		return 0;
	}
	int slot = (int)(ENTITY - g_EnemiesList);
	if (slot < 0 || slot >= 30) {
		return 0;
	}

	// An enemy that was on her when she died stays on her body: a zombie for as
	// long as it feeds (ignore_player_flag, set by zombie_attack on the kill),
	// any of them for P2_DEATH_HOLD_FRAMES - its blood, tint and grab snap
	// belong on her, not on player 1.
	bool holdOnBody = s_deathHold[slot] > 0 ||
	                  (enemy_is_zombie_id(ENTITY->id) && ENTITY->ignore_player_flag != 0);
	if (g_coopP2Down && s_enemyTargetsP2[slot] && holdOnBody &&
	    g_coopPartner != nullptr && s_shadowOwner == g_coopPartner &&
	    s_shadowSerial == s_spawnSerial) {
		if (s_deathHold[slot] > 0) {
			s_deathHold[slot]--;
			if (s_deathHold[slot] == 0 && ENTITY->id == ENEMY_CERBERUS) {
				// The maul never ends on its own - the original's game is over by
				// then. Back to the chase (behaviour 2, Entity+0x85), and drop the
				// "busy" bit the kill set so it can be shot again.
				ENTITY->ignore_player_flag = 2;
				ENTITY->action_behavior = 0;
				ENTITY->action_state = 0;
				ENTITY->hit_state = 0;
				ENTITY->status_flags &= ~2;
				s_enemyTargetsP2[slot] = 0;
				dbg_printf("[COOP] dog in slot %d leaves player 2's body\n", slot);
				return 0;
			}
		}
		Entity* self = ENTITY;
		shadow_swap();
		((void(*)(void))updateFunc)();
		shadow_swap();
		ENTITY = self;

		// Busy with her body it carries status bit 2, which makes
		// ResolveEntityScaCollision skip it - player 1 would walk through it.
		// Keep it solid to him: one push with the bit dropped.
		unsigned char status = ENTITY->status_flags;
		ENTITY->status_flags &= ~2;
		ResolveEntityScaCollision(ENTITY, (Entity*)&g_playerEntity);
		ENTITY->status_flags = status;
		g_playerEntity.position.x = (short)g_playerEntity.scaMatrixData.localMatrix.t[0];
		g_playerEntity.position.z = (short)g_playerEntity.scaMatrixData.localMatrix.t[2];
		return 1;
	}
	if (!partner_targetable()) {
		s_enemyTargetsP2[slot] = 0;
		return 0;
	}

	unsigned char toP2 = s_enemyTargetsP2[slot];
	bool locked = toP2 ? g_coopP2Shadow.isBeingAttackedFlag != 0
	                   : g_playerEntity.isBeingAttackedFlag != 0;
	if (!locked) {
		int ex = ENTITY->scaMatrixData.localMatrix.t[0];
		int ez = ENTITY->scaMatrixData.localMatrix.t[2];
		double d1x = g_playerEntity.scaMatrixData.localMatrix.t[0] - ex;
		double d1z = g_playerEntity.scaMatrixData.localMatrix.t[2] - ez;
		double d2x = g_coopP2Shadow.scaMatrixData.localMatrix.t[0] - ex;
		double d2z = g_coopP2Shadow.scaMatrixData.localMatrix.t[2] - ez;
		toP2 = (d2x * d2x + d2z * d2z) < (d1x * d1x + d1z * d1z) ? 1 : 0;
		if (toP2 != s_enemyTargetsP2[slot]) {
			dbg_printf("[COOP] enemy %02X in slot %d now targets player %d\n", ENTITY->id, slot, toP2 + 1);
		}
		s_enemyTargetsP2[slot] = toP2;
	}
	if (!toP2) {
		return 0;
	}

	Entity* self = ENTITY;
	shadow_swap();
	((void(*)(void))updateFunc)();
	shadow_swap();
	ENTITY = self;

	shadow_to_partner(g_coopPartner);
	if (shadow_dead()) {
		partner_dies();
	}
	return 1;
}

int Coop_PartnerInReaction(void)
{
	if (s_shadowOwner == nullptr || s_shadowOwner != g_coopPartner ||
	    s_shadowSerial != s_spawnSerial) {
		return 0;
	}
	// Anything but free control (state 1, behaviour < 200): an enemy has her.
	return g_coopP2Shadow.animationId != 1 || g_coopP2Shadow.isBeingAttackedFlag != 0;
}

void Coop_RunPartnerReaction(Entity* partner)
{
	static int s_logFrames = 0;
	if (s_logFrames++ % 30 == 0) {
		dbg_printf("[COOP] player 2 held: state %d frame-fn %d behaviour %d action %d anim %d health %d\n",
		           g_coopP2Shadow.animationId, g_coopP2Shadow.animFrameId,
		           g_coopP2Shadow.action_behavior, g_coopP2Shadow.action_state,
		           g_coopP2Shadow.attackAnim, g_coopP2Shadow.health);
	}
	shadow_swap();
	coop_update_shadow_anim();   // sets ENTITY = &g_playerEntity (the stand-in)
	shadow_swap();
	ENTITY = partner;

	shadow_to_partner(partner);
	if (shadow_dead()) {
		partner_dies();
	}
}

void Coop_OnSave(void)
{
	if (g_coopP2Down) {
		g_coopP2Down = 0;
		g_coopP2.health = g_coopP2.maxHealth;
		dbg_printf("[COOP] the save brings player 2 back\n");
	}
}

// ============================================================================
// Partner weapon models
//
// Rebecca's sheet has no shotgun. Her NPC texture (EM1023.EMD's TIM) and
// playable Rebecca's (CHAR13.EMD's) are the same image, and her unreferenced
// W33.EMW shotgun mesh, drawn from it, comes out a solid red slab: the art was
// never made (the original never shows her holding one, and g_weaponPathTable
// sends her to Jill's W13). So she holds Jill's - W13's hand-and-shotgun mesh
// on Jill's sheet (CHAR11.EMD's TIM, 2 texture banks, loaded into the next free
// ones as LoadEntityEMD loads a model's) - or, when that would leave no room
// below bank 16 for the masks, player 1's own shotgun on his sheet (bank 0x16
// page 7, as LoadEquippedWeaponAnimation draws his).
// ============================================================================
extern void ProcessTmdAsync(unsigned int param1);                                         // TmdAnimation.cpp 0x004838e0
extern unsigned int ProcessTmdTextures(char mode, unsigned int* tmd, int bank, int page);  // TmdAnimation.cpp 0x00473da0
extern void SetSpriteBufferFlag(void);

static unsigned char s_p2SheetFile[0x20000];    // CHAR11.EMD (~104KB), for its TIM
static unsigned char s_p2ShotgunFile[0x8000];   // W13.EMW / W03.EMW (~28KB)
static unsigned char s_p2KnifeFile[0x8000];     // W11.EMW / W01.EMW (~21KB)
static unsigned int  s_p2ShotgunMesh = 0;
static unsigned int  s_p2KnifeMesh = 0;

// Load an .EMW and put its weapon mesh on texture (bank, page); 0 on failure.
static unsigned int emw_load_mesh(const char* file, unsigned char* buf, size_t cap,
                                  int bank, int page)
{
	sprintf(FILE_PATH, "%s%s", GAME_DATA_ROOT, file);
	size_t size = LoadFile(FILE_PATH, buf, 0x20);
	if (size < 0x20 || size > cap) {
		dbg_printf("[COOP] could not load %s\n", file);
		return 0;
	}
	unsigned int* mesh = (unsigned int*)(buf + (*(unsigned int*)(buf + (size & ~3u) - 4) & ~3u));
	ProcessTmdTextures(2, mesh, bank, page);
	return (unsigned int)mesh + 0xc;
}

// The weapon mesh of a loaded .EMW: the second of the two dwords at its end.
static unsigned int* emw_mesh(unsigned char* file, size_t size)
{
	return (unsigned int*)(file + (*(unsigned int*)(file + (size & ~3u) - 4) & ~3u));
}

void Coop_LoadPartnerWeaponModels(void)
{
	s_p2ShotgunMesh = 0;
	s_p2KnifeMesh = 0;
	if (g_coopPartner == nullptr || g_coopPartner->id != NPC_REBECCA) {
		return;
	}

	if ((int)g_TextureBankID + COOP_MODEL_BANKS + COOP_MASK_BANKS <= COOP_ROOM_BANK_LIMIT) {
		sprintf(FILE_PATH, "%s%s", GAME_DATA_ROOT, "enemy/char11.emd");
		SetSpriteBufferFlag();
		size_t sheetSize = LoadFile(FILE_PATH, s_p2SheetFile, 0x20);
		if (sheetSize >= 0x20 && sheetSize <= sizeof(s_p2SheetFile)) {
			// The bank/page the TIM lands in are the counters before
			// ProcessTmdAsync advances them; the TIM offset is the last dword.
			unsigned char bank = g_TextureBankID;
			unsigned char page = g_TextureCurrentPage;
			ProcessTmdAsync((unsigned int)s_p2SheetFile +
			                (*(unsigned int*)(s_p2SheetFile + (sheetSize & ~3u) - 4) & ~3u));
			s_p2ShotgunMesh = emw_load_mesh("players/w13.emw", s_p2ShotgunFile,
			                                sizeof(s_p2ShotgunFile), bank, page);
			s_p2KnifeMesh = emw_load_mesh("players/w11.emw", s_p2KnifeFile,
			                              sizeof(s_p2KnifeFile), bank, page);
			dbg_printf("[COOP] player 2's weapons: Jill's, sheet in bank %d page %d\n", bank, page);
			return;
		}
	}

	// Fallback: player 1's own, on his sheet.
	bool jill = (g_playerEntity.id & 1) != 0;
	s_p2ShotgunMesh = emw_load_mesh(jill ? "players/w13.emw" : "players/w03.emw",
	                                s_p2ShotgunFile, sizeof(s_p2ShotgunFile), 0x16, 7);
	s_p2KnifeMesh = emw_load_mesh(jill ? "players/w11.emw" : "players/w01.emw",
	                              s_p2KnifeFile, sizeof(s_p2KnifeFile), 0x16, 7);
	dbg_printf("[COOP] player 2's weapons: player 1's (no texture bank to spare, next %d)\n",
	           (int)g_TextureBankID);
}

unsigned int Coop_PartnerKnifeMesh(void)
{
	return s_p2KnifeMesh;
}

unsigned int Coop_PartnerShotgunMesh(void)
{
	return s_p2ShotgunMesh;
}

// ============================================================================
// Partner reload
// ============================================================================
int Coop_PartnerHasAmmoFor(int weaponId)
{
	unsigned char ammo = (unsigned char)(weaponId + 9);   // weapon_ammo_item_id
	for (int i = 0; i < 6; i++) {
		if (g_RebeccaItemSlots[i].Id == ammo && g_RebeccaItemSlots[i].qty != 0) {
			return 1;
		}
	}
	return 0;
}

// fire_consume_ammo_stack (PlayerAnimations.cpp, 0x0045a580) on her 6 slots.
void Coop_PartnerReloadFromStack(int weaponId)
{
	unsigned char ammo = (unsigned char)(weaponId + 9);
	unsigned char maxQty = g_ItemMaxQty[(unsigned int)ammo * 4];
	unsigned char gunSlot = g_coopP2.equippedItemId;
	if (gunSlot == 0 || gunSlot > 6) {
		return;
	}
	int best = -1;
	unsigned char bestQty = 0;
	for (int i = 0; i < 6; i++) {
		if (g_RebeccaItemSlots[i].Id == ammo && g_RebeccaItemSlots[i].qty > bestQty) {
			best = i;
			bestQty = g_RebeccaItemSlots[i].qty;
		}
	}
	if (best < 0) {
		return;
	}
	if (maxQty < bestQty) {
		g_RebeccaItemSlots[gunSlot - 1].qty = maxQty;
		g_RebeccaItemSlots[best].qty = (unsigned char)(bestQty - maxQty);
		return;
	}
	g_RebeccaItemSlots[gunSlot - 1].qty = bestQty;
	g_RebeccaItemSlots[best].Id = 0;
	g_RebeccaItemSlots[best].qty = 0;

	// rearrange_item_slots closes the gap in the active slots and keeps the
	// equipped index on its item: point it at hers for the one call.
	void* p1Slots = g_ItemSlotsPointer;
	unsigned char p1Equipped = g_EquippedItemId;
	g_ItemSlotsPointer = g_RebeccaItemSlots;
	g_EquippedItemId = g_coopP2.equippedItemId;
	LoadHeldItemsImages();
	rearrange_item_slots();
	g_coopP2.equippedItemId = g_EquippedItemId;
	g_ItemSlotsPointer = p1Slots;
	g_EquippedItemId = p1Equipped;
	LoadHeldItemsImages();
}
