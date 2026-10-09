// Coop.cpp - two-player co-op mod (port addition, not in the original binary)
#include "Coop.h"
#include "BioCard.h"
#include "entities/EntityCommon.h"

extern void Flg_on(int baseAddr, unsigned int bitIndex);   // CmdFunctions.cpp - 0x00473ef0

int   g_coopEnabled = 0;
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
	e->behavior_flags = 0;
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
	int x = g_coopPartner->scaMatrixData.localMatrix.t[0];
	int z = g_coopPartner->scaMatrixData.localMatrix.t[2];
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
