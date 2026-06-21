// CmdFunctions.cpp - SCD command dispatch functions (decompiled from Ghidra)
// All functions in this file are entries in the script_command_funcs_table[81].
// Each function takes no parameters and returns int (0 = stop, 1 = continue).
#include "../Globals.h"
#include <cstdio>
#include <cstring>

// Forward declarations for functions defined in other files
extern unsigned int set_message_display(unsigned short msg_id, unsigned short pause_game);
extern void cut_set(void);
extern void Play3DSnd(int bank, int id, int vol, int pos);
extern void play_sfx(int bank, int soundId);
extern void play_sound_and_voice_effect(int type, int id);
extern void SetSndSlot(int bank, int slot);
extern void setSndStop(int bank);
extern void BuildSndFadeTbl(char fadeType, int maxVol);
extern void set_volume(int bank, int vol);
extern unsigned char Effect_CreateBillboard(unsigned char type, unsigned char param, unsigned short flags, MATRIX* spriteInfo, int* pos, char mode);
extern int get_item_slot(unsigned char itemId);
extern void rearrange_item_slots(void);
extern unsigned int Flg_ck(int baseAddr, unsigned int bitIndex);
extern void FUN_00473f10(int* baseAddr, unsigned int bitIndex);
extern int SquareRoot0(int val);
extern void FUN_004805d0(short param1, unsigned int param2, unsigned int param3, unsigned int param4);
extern void FUN_004804a0(short param1, unsigned int param2, short param3, unsigned int param4);
extern void FUN_0047cf80(int param1, unsigned int param2, unsigned int param3, unsigned int param4, MATRIX* param5);
extern void FUN_00473ea0(int param1, void* param2, ScaMatrixData* param3);
extern void FUN_00473b10(unsigned char p1, unsigned short p2, unsigned short p3, unsigned char p4, unsigned char p5, char p6);
extern void FUN_00473d10(unsigned char p1, unsigned short p2, unsigned short p3, unsigned char p4, unsigned char p5, char p6);
extern void FUN_00473d60(char p1, unsigned char p2, unsigned char p3);
extern void RoomSpr_SetInactive(char id);  // 0x00476130
extern void RoomSpr_SetActive(char id);    // 0x00476170
extern void setBackColor(unsigned char r, unsigned char g, unsigned char b);
extern void FUN_0048bfe0(void);
extern void FUN_0048c020(int param);
extern void FUN_004870d0(int param);
extern void FUN_00484d90(int param1, unsigned char param2, unsigned char param3);
extern void FUN_00484e40(int param1, unsigned char param2, unsigned char param3);
extern void FUN_00473e40(int param);
extern int FUN_0048f330(unsigned char param);
extern void ProcessTmdAsync(unsigned int tmdData);
extern unsigned int ProcessTmdTextures(char mode, unsigned int* tmdBase, int bank, int depth);
extern void ClearTmdProcessingFlag(void);
extern void InitScaMatrix(int parentPtr, ScaMatrixData* matrix);
extern unsigned char QueueTextureForProcessing(char bank, unsigned char depth);
extern void SetupEntityJointAnimation(void);
extern void ScdEventEntry_Create(unsigned int slot, int scriptIndex);
extern void cmd_room_action(void);
extern void empty_00470960(int param);

// Externs for globals used by cmd functions
extern void*          g_RoomInitScd;
// g_message_flags already declared in Globals.h
extern DWORD          g_main_state_flags;
extern int            g_menu_choice_id;
extern DWORD          DAT_00be9830;
extern unsigned int   DAT_00d213a0[2];

// Item event table pointer (used for bounds checking)
extern void*          g_RoomItemEventHead;         // 0x00d91bc0

// ============================================================================
// Helper: read 16-bit value from SCD opcode stream
// ============================================================================
static inline unsigned short scd_read_u16(int offset) {
    return *(unsigned short*)(g_ScdOpcodes + offset);
}

static inline short scd_read_s16(int offset) {
    return *(short*)(g_ScdOpcodes + offset);
}

// ============================================================================
// 0x00 - cmd_nop (0x004604d0)
// ============================================================================
int cmd_nop(void)
{
    g_ScriptContinueFlag = 0;
    return 0;
}

// ============================================================================
// 0x01 - cmd_if (0x004604e0)
// Push current position + offset onto the call stack for conditional.
// ============================================================================
int cmd_if(void)
{
    unsigned short val = scd_read_u16(0);
    g_ScdOpcodes += 2;
    *g_CmdOpcodesPointer = (unsigned int)(val >> 8) + (unsigned int)g_ScdOpcodes;
    g_CmdOpcodesPointer++;
    g_ScriptContinueFlag++;
    return 1;
}

// ============================================================================
// 0x02 - cmd_else (0x00460520)
// Pop call stack and jump to else branch.
// ============================================================================
int cmd_else(void)
{
    g_CmdOpcodesPointer--;
    g_ScriptContinueFlag--;
    g_ScdOpcodes = g_ScdOpcodes + (unsigned int)g_ScdOpcodes[1];
    return 1;
}

// ============================================================================
// 0x03 - cmd_end_if (0x00460550)
// End of if/else block - pop call stack.
// ============================================================================
int cmd_end_if(void)
{
    g_ScriptContinueFlag--;
    g_CmdOpcodesPointer--;
    g_ScdOpcodes += 2;
    return 1;
}

// ============================================================================
// 0x04 - cmd_bit_test (0x00460570)
// Test a bit in a flag bank. Returns 1 if condition matches, 0 otherwise.
// Flag banks: 0=PlayerFlags, 1=PlayerFlags3, 2=desks_locks, 3=RoomEventFlags,
//             4=SysFlags, 5=main_state_flags, 6=message_flags,
//             7=PlayerFlags2, 8=RoomFlags, 9=DAT_00d213a0
// ============================================================================
int cmd_bit_test(void)
{
    unsigned int* flagBank;
    unsigned short op1 = scd_read_u16(0);
    g_ScdOpcodes += 2;

    switch (op1 >> 8) {
    case 0: flagBank = (unsigned int*)&g_PlayerFlags; break;
    case 1: flagBank = (unsigned int*)&g_PlayerFlags3; break;
    case 2: flagBank = (unsigned int*)&g_desks_locks_flags; break;
    case 3: flagBank = (unsigned int*)g_RoomEventFlags; break;
    case 4: flagBank = (unsigned int*)g_SysFlags; break;
    case 5: flagBank = (unsigned int*)&g_main_state_flags; break;
    case 6: flagBank = (unsigned int*)&g_message_flags; break;
    case 7: flagBank = (unsigned int*)&g_PlayerFlags2; break;
    case 8: flagBank = (unsigned int*)&g_RoomFlags; break;
    case 9: flagBank = (unsigned int*)&DAT_00d213a0; break;
    default: return 0;
    }

    unsigned short op2 = scd_read_u16(0);
    g_ScdOpcodes += 2;
    unsigned int bitOffset = (op2 & 0xe0) >> 3;
    unsigned int bitIndex = op2 & 0x1f;
    int* target = (int*)((char*)flagBank + bitOffset);
    unsigned int condition = op2 >> 8;

    return ((*target << bitIndex) < 0) ^ condition;
}

// ============================================================================
// 0x05 - cmd_bit_op (0x00460650)
// Set/clear/toggle a bit in a flag bank.
// Operation: 0=OR(set), 1=AND(clear), 2=XOR(toggle)
// ============================================================================
int cmd_bit_op(void)
{
    unsigned int* flagBank;
    unsigned short op1 = scd_read_u16(0);
    g_ScdOpcodes += 4;

    switch (op1 >> 8) {
    case 0: flagBank = (unsigned int*)&g_PlayerFlags; break;
    case 1: flagBank = (unsigned int*)&g_PlayerFlags3; break;
    case 2: flagBank = (unsigned int*)&g_desks_locks_flags; break;
    case 3: flagBank = (unsigned int*)g_RoomEventFlags; break;
    case 4: flagBank = (unsigned int*)g_SysFlags; break;
    case 5: flagBank = (unsigned int*)&g_main_state_flags; break;
    case 6: flagBank = (unsigned int*)&g_message_flags; break;
    case 7: flagBank = (unsigned int*)&g_PlayerFlags2; break;
    case 8: flagBank = (unsigned int*)&g_RoomFlags; break;
    case 9: flagBank = (unsigned int*)&DAT_00d213a0; break;
    default: return 0;
    }

    unsigned short op2 = scd_read_u16(-2);
    unsigned int operation = op2 >> 8;
    unsigned int bitOffset = (op2 & 0xe0) >> 3;
    unsigned int bitIndex = op2 & 0x1f;
    unsigned int* target = (unsigned int*)((char*)flagBank + bitOffset);
    unsigned int mask = 0x80000000U >> bitIndex;

    if (operation == 0) {
        *target |= mask;
    } else if (operation == 1) {
        *target &= ~mask;
    } else if (operation == 2) {
        *target ^= mask;
    } else {
        return 0;
    }
    return 1;
}

// ============================================================================
// 0x06 - cmd_obj06_test (0x00460760)
// Compare a byte value from the game state against a constant.
// ============================================================================
int cmd_obj06_test(void)
{
    unsigned short op1 = scd_read_u16(0);
    unsigned short op2 = scd_read_u16(2);
    g_ScdOpcodes += 4;

    unsigned char stateVal = ((unsigned char*)&g_stageId)[op1 >> 8];
    unsigned short compareVal = op2 >> 8;
    unsigned char mode = (unsigned char)op2;

    switch (mode) {
    case 0: return compareVal == stateVal;
    case 1: return compareVal < stateVal;
    case 2: return compareVal <= stateVal;
    case 3: return stateVal < compareVal;
    case 4: return stateVal <= compareVal;
    case 5: return compareVal != stateVal;
    default: return 0;
    }
}

// ============================================================================
// 0x07 - cmd_obj07_test (0x00460800)
// Compare a fading state value against a constant.
// ============================================================================
int cmd_obj07_test(void)
{
    unsigned int op1 = *(unsigned int*)g_ScdOpcodes;
    g_ScdOpcodes += 6;

    unsigned short stateVal = ((short*)&g_fading_state)[(op1 & 0xff0000) >> 0x10];
    unsigned short compareVal = (unsigned short)*(unsigned int*)(g_ScdOpcodes - 4);
    unsigned char mode = (unsigned char)(op1 >> 0x18);

    switch (mode) {
    case 0: return compareVal == stateVal;
    case 1: return stateVal > compareVal;
    case 2: return stateVal >= compareVal;
    case 3: return compareVal > stateVal;
    case 4: return compareVal >= stateVal;
    case 5: return compareVal != stateVal;
    default: return 0;
    }
}

// ============================================================================
// 0x08 - cmd_room_cam_set (0x004608a0)
// Set a byte in the g_stageId/g_roomId byte array.
// ============================================================================
int cmd_room_cam_set(void)
{
    unsigned short op1 = scd_read_u16(0);
    unsigned short op2 = scd_read_u16(2);
    g_ScdOpcodes += 4;
    (&g_stageId)[op1 >> 8] = (unsigned char)op2;
    return 1;
}

// ============================================================================
// 0x09 - cmd_cut_set_0x09 (0x00460920)
// Set camera cut and disable camera changes.
// ============================================================================
int cmd_cut_set_0x09(void)
{
    g_ScdOpcodes++;
    g_cutId = g_roomCameraId;
    g_roomCameraId = *g_ScdOpcodes;

    // Walk cam_switch_zones to find matching camera
    unsigned short camId = *(unsigned short*)((char*)g_RdtPointer->cam_switch_zones + 2);
    unsigned int zonePtr = (unsigned int)g_RdtPointer->cam_switch_zones;
    while (camId != g_roomCameraId) {
        g_CurrentRdtDataTypePtr = (void*)(zonePtr + 0x14);
        camId = *(unsigned short*)(zonePtr + 0x16);
        zonePtr = (unsigned int)g_CurrentRdtDataTypePtr;
    }
    g_CurrentRdtDataTypePtr = (void*)zonePtr;
    cut_set();
    g_ScdOpcodes++;
    g_main_state_flags |= 0x100000;
    return 1;
}

// ============================================================================
// 0x0A - cmd_current_cut_set (0x00460990)
// Restore camera to previous cut and enable camera changes.
// ============================================================================
int cmd_current_cut_set(void)
{
    g_roomCameraId = g_cutId;
    unsigned short camId = *(unsigned short*)((char*)g_RdtPointer->cam_switch_zones + 2);
    unsigned int zonePtr = (unsigned int)g_RdtPointer->cam_switch_zones;
    while (camId != g_cutId) {
        g_CurrentRdtDataTypePtr = (void*)(zonePtr + 0x14);
        camId = *(unsigned short*)(zonePtr + 0x16);
        zonePtr = (unsigned int)g_CurrentRdtDataTypePtr;
    }
    g_CurrentRdtDataTypePtr = (void*)zonePtr;
    cut_set();
    g_main_state_flags &= ~0x100000;
    g_ScdOpcodes += 2;
    return 1;
}

// ============================================================================
// 0x0B - cmd_message_set (0x004609f0)
// Display a message with optional pause.
// ============================================================================
int cmd_message_set(void)
{
    unsigned short msgId = scd_read_u16(0);
    g_ScdOpcodes += 2;
    set_message_display(msgId >> 8, *g_ScdOpcodes);
    g_ScdOpcodes++;
    return 1;
}

// ============================================================================
// 0x0C - cmd_door_set (0x004611b0)
// Set up a door interaction in the room item event table.
// ============================================================================
int cmd_door_set(void)
{
    printf("DOOR_AT_SET START %s\n", "door_at_set");
    unsigned char doorNumber = g_ScdOpcodes[1];
    int tableOffset = (unsigned int)doorNumber * 0xc;
    unsigned char* entry = &g_RoomItemEventTable[tableOffset];
    if (entry > (unsigned char*)g_RoomItemEventHead) {
        g_RoomItemEventHead = entry;
    }
    entry[0] = 1;
    entry[1] = g_ScdOpcodes[0x19];
    *(unsigned short*)(entry + 2) = (unsigned short)doorNumber;
    *(unsigned int*)(entry + 8) = (unsigned int)(g_ScdOpcodes + 2);
    g_ScdOpcodes += 0x1a;
    printf("DOOR_AT_SET END %s\n", "door_at_set");
    return 1;
}

// ============================================================================
// 0x0D - cmd_item_set (0x00461130)
// Set up an item pickup in the room item event table.
// ============================================================================
int cmd_item_set(void)
{
    unsigned char itemSlot = g_ScdOpcodes[1];
    int tableOffset = (unsigned int)itemSlot * 0xc;
    unsigned char* entry = &g_RoomItemEventTable[tableOffset];
    if (entry > (unsigned char*)g_RoomItemEventHead) {
        g_RoomItemEventHead = entry;
    }
    entry[0] = g_ScdOpcodes[10];
    entry[1] = g_ScdOpcodes[0xb];
    *(unsigned short*)(entry + 2) = *(unsigned short*)(g_ScdOpcodes + 0xc);
    *(unsigned short*)(entry + 4) = *(unsigned short*)(g_ScdOpcodes + 0xe);
    *(unsigned short*)(entry + 6) = *(unsigned short*)(g_ScdOpcodes + 0x10);
    *(unsigned int*)(entry + 8) = (unsigned int)(g_ScdOpcodes + 2);
    g_ScdOpcodes += 0x12;
    return 1;
}

// ============================================================================
// 0x0E - cmd_skip_2bytes_opcode (0x00460900)
// Skip 2 bytes in the opcode stream.
// ============================================================================
int cmd_skip_2bytes_opcode(void)
{
    g_ScdOpcodes += 2;
    return 1;
}

// ============================================================================
// 0x0F - cmd_entities_0x0f (0x004610b0)
// Set up player entity state and joint animation.
// ============================================================================
int cmd_entities_0x0f(void)
{
    Entity* entityBkp = ENTITY;
    g_main_state_flags = (g_main_state_flags & ~3u) | g_ScdOpcodes[1];
    *(unsigned short*)&DAT_00d211c4 = *(unsigned short*)(g_ScdOpcodes + 2);
    *(unsigned short*)&DAT_00d21350 = *(unsigned short*)(g_ScdOpcodes + 4);
    *(unsigned short*)&DAT_00d2276c = *(unsigned short*)(g_ScdOpcodes + 6);
    ENTITY = (Entity*)&g_playerEntity;
    SetupEntityJointAnimation();
    FUN_0048bfe0();
    FUN_0048c020(0xe);
    g_ScdOpcodes += 8;
    ENTITY = entityBkp;
    return 1;
}

// ============================================================================
// 0x10 - cmd_obj10_test (0x00460f30)
// Test if the used item ID matches a value.
// ============================================================================
int cmd_obj10_test(void)
{
    unsigned char testVal = g_ScdOpcodes[1];
    g_ScdOpcodes += 2;
    return testVal == g_usedItemId;
}

// ============================================================================
// 0x11 - cmd_obj11_test (0x00460f10)
// Test if DAT_00be9833 matches a value.
// ============================================================================
int cmd_obj11_test(void)
{
    unsigned char testVal = g_ScdOpcodes[1];
    g_ScdOpcodes += 2;
    return testVal == DAT_00be9833;
}

// ============================================================================
// 0x12 - cmd_item_flag_0x12 (0x00460fc0)
// Update item event table entry flags.
// ============================================================================
int cmd_item_flag_0x12(void)
{
    int base = (unsigned int)g_ScdOpcodes[1] * 0xc;
    g_ScdOpcodes += 10;
    g_RoomItemEventTable[base]     = g_ScdOpcodes[-8]; // param at +2 from original
    g_RoomItemEventTable[base + 1] = g_ScdOpcodes[-7]; // param at +3
    *(unsigned short*)(&g_RoomItemEventTable[base + 2]) = *(unsigned short*)(g_ScdOpcodes - 6);
    *(unsigned short*)(&g_RoomItemEventTable[base + 4]) = *(unsigned short*)(g_ScdOpcodes - 4);
    *(unsigned short*)(&g_RoomItemEventTable[base + 6]) = *(unsigned short*)(g_ScdOpcodes - 2);
    return 1;
}

// ============================================================================
// 0x13 - cmd_0x13 (0x00461010)
// Update item event table entry (simpler version).
// ============================================================================
int cmd_0x13(void)
{
    int base = (unsigned int)g_ScdOpcodes[1] * 0xc;
    g_ScdOpcodes += 4;
    g_RoomItemEventTable[base]     = g_ScdOpcodes[-2];
    g_RoomItemEventTable[base + 1] = g_ScdOpcodes[-1];
    return 1;
}

// ============================================================================
// 0x14 - cmd_0x14 (0x00461040)
// Create a new SCD event from the command stream.
// ============================================================================
int cmd_0x14(void)
{
    g_ScdOpcodes += 2;
    unsigned char slot = *g_ScdOpcodes & 0xff;
    unsigned char scriptIndex = *g_ScdOpcodes >> 8;
    ScdEventEntry_Create(slot, scriptIndex);
    g_ScdOpcodes += 2;
    return 1;
}

// ============================================================================
// 0x15 - cmd_bgm_0x15 (0x00460a80)
// Start a BGM sound track.
// ============================================================================
int cmd_bgm_0x15(void)
{
    unsigned short val = scd_read_u16(0);
    unsigned int bankIdx = (val & 0x1f) >> 5;
    g_ScdOpcodes += 2;
    int bank = ((int*)&g_SndBank)[bankIdx];
    if (bank != 0) {
        SetSndSlot(bank, (int)(char)(&g_snd_slot_00ac99d5)[bankIdx]);
    }
    g_BGM_STATE |= 1 << ((char)(val >> 8) + 3);
    return 1;
}

// ============================================================================
// 0x16 - cmd_volume_set (0x00460c70)
// Stop and reset volume for a sound bank.
// ============================================================================
int cmd_volume_set(void)
{
    unsigned short val = scd_read_u16(0);
    g_ScdOpcodes += 2;
    unsigned int bit = 1 << ((char)(val >> 8) + 3);
    if ((g_BGM_STATE & bit) != 0) {
        int* bankPtr = &((int*)&g_SndBank)[(val & 0x1f) >> 5];
        int bank = *bankPtr;
        if (bank != 0) {
            setSndStop(bank);
        }
        g_BGM_STATE &= ~bit;
        set_volume(*bankPtr, -1);
    }
    return 1;
}

// ============================================================================
// 0x17 - cmd_player_pos_0x17 (0x00460d80)
// Play a 3D sound effect at various positions.
// ============================================================================
int cmd_player_pos_0x17(void)
{
    unsigned short op1 = scd_read_u16(0);
    unsigned short op2 = scd_read_u16(2);
    unsigned short op3 = scd_read_u16(4);
    g_ScdOpcodes += 6;

    unsigned char sndType = (unsigned char)(op1 >> 8);
    unsigned char sndId = (unsigned char)op2;
    char vol = (char)(op2 >> 8);
    unsigned char posType = (unsigned char)op3;

    switch (posType) {
    case 0: {
        int posX = (int)(short)scd_read_s16(0);
        int posZ = (int)(short)scd_read_s16(2);
        g_ScdOpcodes += 4;
        SVECTOR pos;
        pos.x = (short)posX;
        pos.y = 0;
        pos.z = (short)posZ;
        Play3DSnd(sndType, sndId, (int)vol, (unsigned int)&pos);
        break;
    }
    case 1:
        g_ScdOpcodes += 4;
        Play3DSnd(sndType, sndId, (int)vol, (unsigned int)&g_playerEntity.position);
        break;
    case 2:
        g_ScdOpcodes += 4;
        Play3DSnd(sndType, sndId, (int)vol,
            (unsigned int)(op3 >> 8) * 0x18c + (unsigned int)&g_EnemiesList[0].position);
        break;
    case 3:
        g_ScdOpcodes += 4;
        play_sfx(sndType, sndType);
        break;
    }
    return 1;
}

// ============================================================================
// 0x18 - cmd_item_model_set (0x00461220)
// Set up an interactive item model (desks, obstacles, etc.).
// ============================================================================
int cmd_item_model_set(void)
{
    printf("ITEM MODEL START %s\n", "imodel_set");

    // Check for '/' character special case with Jill
    if ((char)g_ScdOpcodes[10] == '/' && (g_playerEntity.id & 3) == 1) {
        if (Flg_ck((int)&g_PlayerFlags, 0x7b) == 0) {
            FUN_00473f10((int*)&g_PlayerFlags2, g_ScdOpcodes[0x16]);
            g_ScdOpcodes += 0x1a;
            return 1;
        }
    }

    unsigned int slotIdx = (unsigned int)(g_ScdOpcodes[1] & 0x7f);
    int tableOffset = slotIdx * 0xc;
    unsigned char* entry = &g_RoomItemEventTable[tableOffset];
    unsigned char itemType = g_ScdOpcodes[10];

    // Determine entry visibility based on flag check and item type
    unsigned char visFlag;
    if (itemType < 0x54) {
        if (itemType < 0x4e) {
            visFlag = (Flg_ck((int)&g_PlayerFlags2, g_ScdOpcodes[0x16]) == 0) - 1;
            visFlag &= 4;
        } else {
            visFlag = (Flg_ck((int)&g_PlayerFlags2, g_ScdOpcodes[0x16]) == 0) - 1;
            visFlag &= 0xf;
        }
    } else {
        visFlag = (Flg_ck((int)&g_PlayerFlags2, g_ScdOpcodes[0x16]) == 0) - 1;
        visFlag &= 0xd;
    }
    entry[0] = visFlag;

    unsigned short flags18 = scd_read_u16(0x18);
    entry[1] = g_ScdOpcodes[0x17];
    *(unsigned short*)(entry + 2) = flags18;
    *(unsigned short*)(entry + 4) = (unsigned short)g_ScdOpcodes[0xc];
    *(unsigned short*)(entry + 6) = (unsigned short)g_ScdOpcodes[0x16];
    *(unsigned int*)(entry + 8) = (unsigned int)(g_ScdOpcodes + 2);
    if (entry > (unsigned char*)g_RoomItemEventHead) {
        g_RoomItemEventHead = entry;
    }

    char* deskPtr = (char*)g_desks_pointers_table[g_ScdOpcodes[0xc]];
    int* obstacleData = (int*)((char*)g_RdtPointer->obstacles_models + (unsigned int)g_ScdOpcodes[0xc] * 8);

    if (*obstacleData == 0) {
        deskPtr[0x14] = 0; deskPtr[0x15] = 0;
        deskPtr[0x16] = 0; deskPtr[0x17] = 0;
    } else {
        if (((char)g_ItemModelCount == 0 || DAT_00bca0d0[1] != obstacleData[1]) && obstacleData[1] != 0) {
            DAT_008e1c78 = g_TextureBankID;
            DAT_008e1c70 = g_TextureDepthByte;
            ClearTmdProcessingFlag();
            ProcessTmdAsync((unsigned int)obstacleData[1]);
        }
        if ((char)g_ItemModelCount == 0 || *obstacleData != *DAT_00bca0d0) {
            ProcessTmdTextures(2, (unsigned int*)(unsigned int)*obstacleData, DAT_008e1c78, DAT_008e1c70);
        }
        FUN_00473ea0(*obstacleData, deskPtr + 0xc, (ScaMatrixData*)(deskPtr + 0x1c));
        if ((char)g_ScdOpcodes[10] == 0x1e) {
            FUN_004870d0(*(int*)(deskPtr + 0x18));
        }

        unsigned char parentType = g_ScdOpcodes[0xd];
        if (parentType == 0xff) {
            deskPtr[100] = 0; deskPtr[0x65] = 0;
            deskPtr[0x66] = 0; deskPtr[0x67] = 0;
        } else if (parentType == 0xfe) {
            *(int*)(deskPtr + 100) = (int)&g_playerEntity + 0x1c;
        } else {
            *(int*)(deskPtr + 100) = (int)g_itemboxes_covers_table[parentType] + 0x1c;
        }
        InitScaMatrix(*(int*)(deskPtr + 100), (ScaMatrixData*)(deskPtr + 0x1c));
    }

    deskPtr[0x86] = 0;
    deskPtr[0x87] = 0;

    int flagResult = Flg_ck((int)&g_PlayerFlags2, g_ScdOpcodes[0x16]);
    if (flagResult != 0 && (flags18 & 0x8000) != 0) {
        unsigned int animType = (unsigned int)(flags18 & 0xf00);
        unsigned char effectId;
        if (animType < 0x101) {
            effectId = (animType == 0x100) ? 0x0b : ((flags18 & 0xf00) == 0 ? 0x03 : 0x14);
        } else if (animType < 0x301) {
            effectId = (animType == 0x300) ? 0x1b : (animType == 0x200 ? 0x13 : 0x14);
        } else if (animType < 0x501) {
            effectId = (animType == 0x500) ? 0x0c : (animType == 0x400 ? 0x04 : 0x14);
        } else {
            effectId = (animType == 0x600) ? 0x14 : (animType == 0x700 ? 0x1c : 0x14);
        }

        SVECTOR effectPos;
        if ((char)g_ScdOpcodes[0xd] == -1) {
            effectPos.x = 0;
            effectPos.z = 0;
            effectPos.y = (short)((int)(flags18 & 0xf0) * -2);
        } else {
            effectPos.x = scd_read_s16(0xe);
            effectPos.y = (short)(scd_read_s16(0x10) + (int)(flags18 & 0xf0) * -2);
            effectPos.z = scd_read_s16(0x12);
        }
        MATRIX* spriteInfo = (MATRIX*)(deskPtr + 0x20);
        unsigned char effResult = Effect_CreateBillboard(0x0b, effectId, 0, spriteInfo, (int*)&effectPos, 0);
        *(short*)(deskPtr + 0x86) = (short)(char)effResult;
    }

    flagResult = Flg_ck((int)&g_PlayerFlags2, g_ScdOpcodes[0x16]);
    entry[0] = 1 - (flagResult == 0);

    if ((char)g_ScdOpcodes[10] == '/' && (g_playerEntity.id & 3) == 1) {
        if (Flg_ck((int)&g_PlayerFlags, 0x7b) == 0) {
            entry[0] = 0;
        }
    }

    deskPtr[1] = (char)g_ItemModelCount;
    deskPtr[0x72] = 0; deskPtr[0x73] = 0;
    *(unsigned short*)(deskPtr + 0x74) = scd_read_u16(0x14);
    deskPtr[0x76] = 0; deskPtr[0x77] = 0;
    deskPtr[0xc] = 0; deskPtr[0xd] = 0;
    deskPtr[0xe] = 0; deskPtr[0xf] = 0x40;

    if (g_ScdOpcodes[1] & 0x80) {
        deskPtr[0xc] = 0x40; deskPtr[0xd] = 0;
        deskPtr[0xe] = 0;   deskPtr[0xf] = 0x40;
    }

    *(int*)(deskPtr + 0x34) = (int)scd_read_s16(0xe);
    *(int*)(deskPtr + 0x38) = (int)scd_read_s16(0x10);
    *(int*)(deskPtr + 0x3c) = (int)scd_read_s16(0x12);

    if (g_stageId == 6 && g_roomId == 0x15 && (deskPtr[1] & 0x3f) == 1) {
        *(int*)(deskPtr + 0x3c) = scd_read_s16(0x12) - 0x96;
    }

    *(char*)&g_ItemModelCount = (char)g_ItemModelCount + 1;
    g_ScdOpcodes += 0x1a;
    DAT_00bca0d0 = obstacleData;
    printf("ITEM MODEL END %s\n", "imodel_set");
    return 1;
}

// ============================================================================
// 0x19 - cmd_obj19_set (0x00460f50)
// Set a byte on a desk/obstacle pointer.
// ============================================================================
int cmd_obj19_set(void)
{
    unsigned char deskIdx = g_ScdOpcodes[1];
    unsigned char value = g_ScdOpcodes[2];
    g_ScdOpcodes += 4;
    *(unsigned char*)g_desks_pointers_table[deskIdx] = value;
    return 1;
}

// ============================================================================
// 0x1A - cmd_item_search (0x00460f80)
// Search player inventory for an item. Returns 1 if found.
// ============================================================================
int cmd_item_search(void)
{
    unsigned char searchId = g_ScdOpcodes[1];
    g_ScdOpcodes += 2;
    if (g_TotalHeldItems != 0) {
        unsigned char* slots = (unsigned char*)g_ItemSlotsPointer;
        for (unsigned int i = 0; i < g_TotalHeldItems; i++) {
            if (*slots == searchId) return 1;
            slots += 2;
        }
    }
    return 0;
}

// ============================================================================
// 0x1B - cmd_em_set (0x004617d0)
// Set up an enemy entity in the room.
// ============================================================================
int cmd_em_set(void)
{
    printf("ENEMY SET START %s\n", "enemy_set");

    if ((char)g_ScdOpcodes[3] != -1) {
        if (Flg_ck((int)g_RoomEventFlags, (char)g_ScdOpcodes[3]) != 0) {
            g_ScdOpcodes += 0x16;
            printf("ENEMY SET END %s\n", "enemy_set");
            return 1;
        }
    }

    unsigned char enemySlot = g_ScdOpcodes[0x12] & 0xf;
    ENTITY = &g_EnemiesList[enemySlot];
    g_EnemiesList[enemySlot].scaMatrixData.localMatrix.t[1] = (int)scd_read_s16(0xe);
    g_EnemiesList[enemySlot].pad_15d[4] = g_ScdOpcodes[0x12] & 0xf;
    g_EnemiesList[enemySlot].pad_15d[4] |= (char)g_ScdOpcodes[0x15] << 4;

    if ((char)g_ScdOpcodes[4] != 0) {
        ENTITY->pad_15d[4] |= 0x80;
    }

    bool shouldInit = true;
    if ((char)g_ScdOpcodes[4] == 0) {
        if (FUN_0048f330(g_ScdOpcodes[0x12]) != 0) shouldInit = false;
    }

    if (shouldInit) {
        ENTITY->status_flags = 1;
        ENTITY->behavior_flags = g_ScdOpcodes[2];
        *(unsigned short*)&ENTITY->angle = scd_read_u16(8);
        ENTITY->scaMatrixData.localMatrix.t[0] = (unsigned int)scd_read_u16(0xc);
        ENTITY->scaMatrixData.localMatrix.t[2] = (unsigned int)scd_read_u16(0x10);
        ENTITY->position.x = (short)ENTITY->scaMatrixData.localMatrix.t[0];
        ENTITY->position.y = (short)ENTITY->scaMatrixData.localMatrix.t[1];
        ENTITY->position.z = (short)ENTITY->scaMatrixData.localMatrix.t[2];
        ENTITY->animationId = g_ScdOpcodes[0x13];
        ENTITY->animation_frame_id = g_ScdOpcodes[0x14];
        ENTITY->timing_control = 1;
    }

    if (ENTITY->status_flags & 1) {
        ENTITY->state = 0;
        ENTITY->ignore_player_flag = 0;
        ENTITY->action_behavior = 0;
        ENTITY->action_state = 0;
        ENTITY->id = g_ScdOpcodes[1];
        ENTITY->pad_15d[7] = g_ScdOpcodes[3];
        ENTITY->position.pad = scd_read_s16(6);
        *((unsigned short*)&ENTITY->angle + 1) = scd_read_u16(10);
        ENTITY->field_0x8a = 0;
        *(unsigned short*)&ENTITY->pad_ca[0] = 0;
        ENTITY->collisionFlags = 0;
        ENTITY->pad_bc = 0;
        ENTITY->Sca_info = (unsigned int)g_scaDataTable;
        ENTITY->pSca_hit_data = g_scaPoolPtr;
        g_enemy_count++;
        g_scaPoolPtr += (unsigned int)g_ScdOpcodes[5] * 6;
    }

    g_ScdOpcodes += 0x16;
    printf("ENEMY SET END %s\n", "enemy_set");
    return 1;
}

// ============================================================================
// 0x1C - cmd_0x1c (0x00462210)
// Set up special room lighting effects.
// ============================================================================
int cmd_0x1c(void)
{
    unsigned short op1 = scd_read_u16(0);
    g_ScdOpcodes += 2;
    g_SpecialRoomLightR = (char)(op1 >> 8);
    g_SpecialRoomLightDelta = scd_read_s16(0);
    g_ScdOpcodes += 2;
    unsigned short flags = scd_read_u16(0);

    g_SpecialB1 = 0;
    g_SpecialG1 = 0;
    g_SpecialR1 = 0;
    if (flags & 1) g_SpecialB1 = 0xff;
    if (flags & 2) g_SpecialG1 = 0xff;
    if (flags & 4) g_SpecialR1 = 0xff;

    g_ScdOpcodes += 2;
    if (g_SpecialRoomLightDelta != 0) {
        g_SpecialRoomLightState = 0;
        if (g_SpecialRoomLightDelta < 1) {
            g_SpecialRoomLightState = 0x7fff;
        }
    }
    return 1;
}

// ============================================================================
// 0x1D - cmd_weapon_set (0x00460ee0)
// Test if the equipped weapon matches a value.
// ============================================================================
int cmd_weapon_set(void)
{
    unsigned char testVal = g_ScdOpcodes[1];
    g_ScdOpcodes += 2;
    return ((unsigned char*)g_ItemSlotsPointer)[-2 + (unsigned int)g_EquippedItemId * 2] == testVal;
}

// ============================================================================
// 0x1E - cmd_sfx_set (0x00461a80)
// Play a sound/voice effect.
// ============================================================================
int cmd_sfx_set(void)
{
    unsigned short sndId = scd_read_u16(0);
    g_ScdOpcodes += 2;
    unsigned short param = scd_read_u16(0);
    g_ScdOpcodes += 2;
    play_sound_and_voice_effect(sndId >> 8, param);
    g_main_state_flags |= 0x20000;
    return 1;
}

// ============================================================================
// 0x1F - cmd_omodel_set (0x00461ac0)
// Set up an object model (furniture, decorations, etc.).
// This is a very complex function with many stage/room-specific overrides.
// ============================================================================
int cmd_omodel_set(void)
{
    printf("OMODEL SET START %s\n", "omodel_set");

    unsigned int slotIdx = (unsigned int)(g_ScdOpcodes[1] & 0x3f);
    char* objPtr = (char*)g_itemboxes_covers_table[slotIdx];
    int* modelData = (int*)((char*)g_RdtPointer->items_models + slotIdx * 8);

    if (*modelData == 0) {
        *(int*)(objPtr + 0x14) = 0;
        goto setupObject;
    }

    // Model TMD processing
    if (((char)g_omodelCount == 0 || DAT_00bca0d4[1] != modelData[1]) && modelData[1] != 0) {
        DAT_008e1c7c = g_TextureBankID;
        DAT_008e1c74 = g_TextureDepthByte;

        // Stage 4 room-specific texture bank overrides
        if (g_stageId == 4) {
            if (g_roomId == 4) {
                if (g_TextureBankID == 9) {
                    g_TextureBankID = 0x0e;
                    g_TextureDepthByte = 0x13;
                }
            } else if (g_roomId == 6) {
                if (g_TextureBankID == 7) {
                    g_TextureBankID = 9;
                    g_TextureDepthByte++;
                } else if (g_TextureBankID == 9) {
                    g_TextureBankID = 0x0b;
                    g_TextureDepthByte++;
                } else if (g_TextureBankID == 0x0b) {
                    g_TextureBankID = 0x0d;
                    g_TextureDepthByte++;
                }
            }
        } else {
            // Stage 0 room 12: adjust TMD colors
            if (g_stageId == 0 && g_roomId == 12 && (g_ScdOpcodes[1] & 0x3f) == 0) {
                unsigned short* colorPtr = (unsigned short*)(modelData[1] + 0x14);
                for (int i = 0; i < 256; i++) {
                    unsigned short c = *colorPtr;
                    unsigned short r = (c & 0x1f) + 3;
                    unsigned short g = ((c & 0x3e0) >> 5) + 2;
                    unsigned short b = ((c & 0x7c00) >> 10) + 2;
                    if (r > 0x1f) r = 0x1f;
                    if (g > 0x1f) g = 0x1f;
                    if (b > 0x1f) b = 0x1f;
                    *colorPtr = (g << 5) | (b << 10) | r | (c & 0x8000);
                    colorPtr++;
                }
            }

            if (!(g_stageId == 6 && g_roomId == 0x16 && (g_ScdOpcodes[1] & 0x3f) == 0)) {
                ClearTmdProcessingFlag();
            }

            // Stage 1 room 11: fix transparent colors
            if ((g_stageId + 1) % 5 == 2 && g_roomId == 0x0b && (g_ScdOpcodes[1] & 0x3f) == 0) {
                unsigned short* colorPtr = (unsigned short*)(modelData[1] + 0x14);
                for (int i = 0; i < 256; i++) {
                    if ((*colorPtr & 0x7fff) == 0x7fff) *colorPtr = 0x4e73;
                    colorPtr++;
                }
            }
            ProcessTmdAsync((unsigned int)modelData[1]);
        }
    }

    // TMD texture processing
    if ((char)g_omodelCount == 0 || *DAT_00bca0d4 != *modelData) {
        unsigned int texResult = ProcessTmdTextures(2, (unsigned int*)(unsigned int)*modelData, DAT_008e1c7c, DAT_008e1c74);
        if (g_ScdOpcodes[1] & 0x80) {
            QueueTextureForProcessing((char)DAT_008e1c74,
                (unsigned char)(((unsigned int)texResult & 0xFFFFFF00) | (g_ScdOpcodes[1] & 0xBF)));
        }
        if (g_stageId == 2 && g_roomId == 3 && (g_ScdOpcodes[1] & 0x3f) < 5) {
            FUN_00473e40(*modelData);
        }
    }

    // Stage-specific adjustments
    if ((g_stageId + 1) % 5 == 2 && g_roomId == 10 && (g_ScdOpcodes[1] & 0x3f) == 1) {
        DAT_004d2be0 = 0x30;
    }
    if (g_stageId == 0 && g_roomId == 13 && (g_ScdOpcodes[1] & 0x3f) == 1) {
        FUN_00473e40(*modelData);
    }
    if ((g_stageId + 1) % 5 == 2 && g_roomId == 10 && (g_ScdOpcodes[1] & 0x3f) == 0) {
        FUN_00484d90(modelData[1], DAT_008e1c7c, DAT_008e1c74);
        FUN_00484e40(*modelData, DAT_008e1c7c, DAT_008e1c74);
    }

    FUN_00473ea0(*modelData, objPtr + 0xc, (ScaMatrixData*)(objPtr + 0x1c));
    DAT_004d2bdc = 0;
    DAT_004d2be0 = -1;

    // Set up SCA parent reference
    unsigned char parentByte = g_ScdOpcodes[3];
    unsigned int parentIdx = (unsigned int)parentByte;
    if (parentIdx == 0xfe) {
        *(int*)(objPtr + 100) = (int)&g_playerEntity + 0x1c;
    } else if (parentIdx == 0xff) {
        *(int*)(objPtr + 100) = 0;
    } else if (parentByte < 0x80) {
        *(int*)(objPtr + 100) = (int)g_itemboxes_covers_table[parentIdx] + 0x1c;
    } else {
        *(unsigned int*)(objPtr + 100) = (unsigned int)(parentByte & 0x7f) * 0x18c + 0xbe6480;
    }
    InitScaMatrix(*(int*)(objPtr + 100), (ScaMatrixData*)(objPtr + 0x1c));

setupObject:
    *(int*)(objPtr + 0xc) = 0x40000000;
    if (g_ScdOpcodes[2] & 0x10) {
        *(int*)(objPtr + 0xc) = 0x40000040;
    }

    objPtr[0] = g_ScdOpcodes[2];
    objPtr[1] = g_ScdOpcodes[1] & 0x7f;
    *(unsigned short*)(objPtr + 0x72) = 0;
    unsigned short flags0a = scd_read_u16(0xa);
    *(unsigned short*)(objPtr + 0x7e) = flags0a;
    *(unsigned short*)(objPtr + 0x74) = flags0a;
    *(unsigned short*)(objPtr + 0x76) = 0;

    // Set up entry data pointer
    *(unsigned int*)(objPtr + 4) = (unsigned int)(objPtr + 0x88);
    *(unsigned short*)(objPtr + 0x88) = 0x8000;
    *(unsigned short*)(objPtr + 0x8a) = scd_read_u16(0x18);
    *(unsigned short*)(objPtr + 0x8c) = scd_read_u16(0x16);
    *(unsigned short*)(objPtr + 0x8e) = scd_read_u16(0x1a);
    *(unsigned short*)(objPtr + 0x90) = scd_read_u16(0x16);
    *(unsigned short*)(objPtr + 0x92) = scd_read_u16(0x14);
    *(unsigned short*)(objPtr + 0x94) = scd_read_u16(0xc);
    *(unsigned short*)(objPtr + 0x98) = scd_read_u16(0xe);
    *(unsigned short*)(objPtr + 0x9c) = scd_read_u16(0x10);
    *(unsigned short*)(objPtr + 0xa0) = scd_read_u16(0x12);

    // Set position
    short posX = scd_read_s16(4);
    *(short*)(objPtr + 0x6c) = posX;
    *(int*)(objPtr + 0x34) = (int)posX;
    short posY = scd_read_s16(6);
    *(short*)(objPtr + 0x6e) = posY;
    *(int*)(objPtr + 0x38) = (int)posY;
    short posZ = scd_read_s16(8);
    *(short*)(objPtr + 0x70) = posZ;
    *(int*)(objPtr + 0x3c) = (int)posZ;

    // Stage/room-specific position adjustments
    if (g_stageId == 0 && g_roomId == 5 && g_playerEntity.id == 1 && (g_ScdOpcodes[1] & 0x3f) == 1) {
        *(short*)(objPtr + 0x6e) = -5;
        *(int*)(objPtr + 0x38) = -5;
    }
    if ((g_stageId + 1) % 5 == 2 && g_roomId == 0x0b) {
        if ((g_ScdOpcodes[1] & 0x3f) == 0) {
            short adjZ = scd_read_s16(8) + 10;
            *(short*)(objPtr + 0x70) = adjZ;
            *(int*)(objPtr + 0x3c) = (int)adjZ;
        }
        if ((g_ScdOpcodes[1] & 0x3f) == 1) {
            short adjZ = scd_read_s16(8) - 0x28;
            *(short*)(objPtr + 0x70) = adjZ;
            *(int*)(objPtr + 0x3c) = (int)adjZ;
        }
    }
    if (g_stageId == 3) {
        if (g_roomId == 3 && (g_ScdOpcodes[1] & 0x3f) == 0) {
            *(short*)(objPtr + 0x6c) = scd_read_s16(4) + 0x1e;
            *(int*)(objPtr + 0x34) = (int)*(short*)(objPtr + 0x6c);
            *(short*)(objPtr + 0x6e) = scd_read_s16(6) - 10;
            *(int*)(objPtr + 0x38) = (int)*(short*)(objPtr + 0x6e);
            *(short*)(objPtr + 0x70) = scd_read_s16(8) + 0x82;
            *(int*)(objPtr + 0x3c) = (int)*(short*)(objPtr + 0x70);
        }
        if (g_roomId == 0x0f && (g_ScdOpcodes[1] & 0x3f) == 0) {
            *(short*)(objPtr + 0x6e) = scd_read_s16(6) - 0x15e;
            *(int*)(objPtr + 0x38) = (int)*(short*)(objPtr + 0x6e);
        }
    }

    *(char*)&g_omodelCount = (char)g_omodelCount + 1;
    g_ScdOpcodes += 0x1c;
    DAT_00bca0d4 = modelData;
    printf("OMODEL SET END %s\n", "omodel_set");
    return 1;
}

// ============================================================================
// 0x20 - cmd_player_pos_set (0x00430f60)
// Set the player's position, rotation, and speed from SCD data.
// ============================================================================
int cmd_player_pos_set(void)
{
    g_playerEntity.unk_e0 &= ~8u;
    g_playerEntity.position.pad = scd_read_s16(2);
    g_playerEntity.directionAngle = scd_read_s16(4);
    g_playerEntity.speed.x = scd_read_s16(6);
    g_playerEntity.position.x = scd_read_s16(8);
    g_playerEntity.scaMatrixData.localMatrix.t[0] = (int)scd_read_s16(8);
    g_playerEntity.position.y = scd_read_s16(10);
    g_playerEntity.scaMatrixData.localMatrix.t[1] = (int)scd_read_s16(10);
    short posZ = scd_read_s16(0xc);
    g_ScdOpcodes += 0xe;
    g_playerEntity.position.z = posZ;
    g_playerEntity.scaMatrixData.localMatrix.t[2] = (int)posZ;
    return 1;
}

// ============================================================================
// 0x21 - cmd_enemy_pos_set (0x00430fe0)
// Set an enemy's position, rotation from SCD data.
// ============================================================================
int cmd_enemy_pos_set(void)
{
    g_ScdOpcodes += 7;
    unsigned char enemyIdx = (unsigned char)(scd_read_u16(-5) >> 8);
    *(unsigned short*)&g_EnemiesList[enemyIdx].pad_ec[0x70] &= ~8u;
    g_EnemiesList[enemyIdx].position.pad = scd_read_s16(-3);
    *(short*)&g_EnemiesList[enemyIdx].angle = scd_read_s16(-1);
    *((short*)&g_EnemiesList[enemyIdx].angle + 1) = scd_read_s16(1);
    short posX = scd_read_s16(3);
    g_EnemiesList[enemyIdx].position.x = posX;
    g_EnemiesList[enemyIdx].scaMatrixData.localMatrix.t[0] = (int)posX;
    short posY = scd_read_s16(5);
    g_EnemiesList[enemyIdx].position.y = posY;
    g_EnemiesList[enemyIdx].scaMatrixData.localMatrix.t[1] = (int)posY;
    short posZ = scd_read_s16(7);
    g_EnemiesList[enemyIdx].position.z = posZ;
    g_EnemiesList[enemyIdx].scaMatrixData.localMatrix.t[2] = (int)posZ;
    return 1;
}

// ============================================================================
// 0x22 - cmd_item_cmd_0x22 (0x00431100)
// Complex item inventory search and comparison.
// ============================================================================
int cmd_item_cmd_0x22(void)
{
    unsigned short op1 = scd_read_u16(0);
    int count = 0;
    g_ScdOpcodes += 4;
    unsigned int searchId = (unsigned int)(op1 >> 8);
    unsigned int totalVal = 0;

    if (g_TotalHeldItems != 0) {
        unsigned char* slots = (unsigned char*)g_ItemSlotsPointer;
        for (unsigned int i = 0; i < (unsigned int)g_TotalHeldItems; i++) {
            unsigned char itemId = slots[0];
            if (searchId == itemId || (unsigned char)(itemId - searchId) == (unsigned char)-0xb) {
                totalVal = (unsigned int)slots[1];
            }
            switch (searchId) {
            case 10:
                totalVal = (unsigned int)slots[1];
                count++;
                break;
            case 0xb:
                if (itemId == 2) { totalVal += slots[1]; count++; }
                break;
            case 0xc:
                if (itemId == 3) { totalVal += slots[1]; count++; }
                break;
            case 0xd:
                if (itemId == 5 || itemId == 4) { totalVal += slots[1]; count++; }
                break;
            case 0xf:
                if (itemId == 6) { totalVal += slots[1]; count++; }
                break;
            case 0x10: case 0x11: case 0x12:
                if (itemId == 7 || itemId == 8 || itemId == 9) { totalVal += slots[1]; count++; }
                break;
            }
            slots += 2;
        }
    }

    unsigned short op2 = scd_read_u16(-2);
    unsigned int compareVal = (unsigned int)(op2 >> 8);
    if (count == 0) return 0;

    switch ((unsigned char)op2) {
    case 0: return totalVal == compareVal;
    case 1: return compareVal < totalVal;
    case 2: return compareVal <= totalVal;
    case 3: return totalVal < compareVal;
    case 4: return totalVal <= compareVal;
    case 5: return totalVal != compareVal;
    default: return 0;
    }
}

// ============================================================================
// 0x23 - cmd_cut_toogle (0x00431280)
// Toggle camera change enable/disable.
// ============================================================================
int cmd_cut_toogle(void)
{
    if ((char)g_ScdOpcodes[1] == 0) {
        g_main_state_flags &= ~0x100000;
    } else {
        g_main_state_flags |= 0x100000;
    }
    g_ScdOpcodes += 2;
    return 1;
}

// ============================================================================
// 0x24 - cmd_room_action (0x004312b0)
// Execute a room action callback from the room_check_actions table.
// ============================================================================
int cmd_room_action_impl(void)
{
    unsigned char slotIdx = g_ScdOpcodes[1];
    unsigned char actionIdx = g_ScdOpcodes[2];
    g_ScdOpcodes += 4;
    extern void* room_check_actions[];
    typedef void (*RoomActionFunc)(void*);
    ((RoomActionFunc)room_check_actions[actionIdx])(
        &g_RoomItemEventTable[(unsigned int)slotIdx * 0xc]);
    return 1;
}

// ============================================================================
// 0x25 - cmd_rdt_0x25 (0x004621d0)
// Enable or disable a room sprite by ID (SCD room object visibility command).
// If high byte of opcode word is 0, enables the sprite; otherwise disables it.
// ============================================================================
int cmd_rdt_0x25(void)
{
    g_ScdOpcodes += 2;
    if ((*g_ScdOpcodes & 0xff00) == 0) {
        RoomSpr_SetActive((char)(*g_ScdOpcodes & 0xff));
    } else {
        RoomSpr_SetInactive((char)(*g_ScdOpcodes & 0xff));
    }
    g_ScdOpcodes += 2;
    return 1;
}

// ============================================================================
// 0x26 - cmd_nop_0x26 (0x00460ce0)
// ============================================================================
int cmd_nop_0x26(void)
{
    return 1;
}

// ============================================================================
// 0x27 - cmd_snd_fade_set (0x00460cf0)
// Set up sound fade.
// ============================================================================
int cmd_snd_fade_set(void)
{
    BuildSndFadeTbl((char)(scd_read_u16(0) >> 8), 0x7f);
    g_ScdOpcodes += 2;
    return 1;
}

// ============================================================================
// 0x28 - cmd_enemy_0x28 (0x004312f0)
// Modify enemy entity properties (behavior, state, flags, etc.).
// ============================================================================
int cmd_enemy_0x28(void)
{
    unsigned short op1 = scd_read_u16(2);
    unsigned int enemyIdx = op1 & 0xff;
    Entity* ent = &g_EnemiesList[enemyIdx];
    unsigned char subCmd = (unsigned char)(op1 >> 8);
    unsigned short param1 = scd_read_u16(4);

    switch (subCmd) {
    case 0:
        ent->behavior_flags = (unsigned char)param1;
        g_ScdOpcodes += 6;
        return 1;
    case 1:
        ent->state = 2;
        ent->ignore_player_flag = 0;
        ent->action_behavior = 0;
        ent->action_state = 0;
        ent->health = param1;
        ent->field_0x8a = g_ScdOpcodes[6];
        g_ScdOpcodes += 8;
        return 1;
    case 2:
        ent->action_behavior = (unsigned char)param1;
        ent->action_state = 0;
        g_ScdOpcodes += 6;
        return 1;
    case 3: {
        char mode = (char)(param1 >> 8);
        unsigned char flagVal = (unsigned char)param1;
        if (mode == 0) ent->status_flags = flagVal;
        else if (mode == 1) ent->status_flags |= flagVal;
        else if (mode == 2) ent->status_flags ^= flagVal;
        g_ScdOpcodes += 6;
        return 1;
    }
    case 5:
        *(unsigned short*)&ent->angle = param1;
        g_ScdOpcodes += 6;
        return 1;
    case 6:
        ent->blend_counter = 0;
        g_ScdOpcodes += 4;
        return 1;
    case 8:
        ent->state = 9;
        ent->ignore_player_flag = 0;
        ent->action_behavior = 0;
        ent->action_state = 0;
        g_ScdOpcodes += 4;
        return 1;
    case 9: {
        JointStruct* joints = ent->jointsStructs;
        for (unsigned short bits = param1; bits != 0; bits >>= 1) {
            joints->flags ^= (unsigned char)bits & 1;
            joints++;
        }
        g_ScdOpcodes += 6;
        return 1;
    }
    case 10:
        ent->action_state = (unsigned char)param1;
        g_ScdOpcodes += 6;
        return 1;
    default:
        return 1;
    }
}

// ============================================================================
// 0x29 - cmd_fmv_set (0x00461a40)
// Set up FMV (full motion video) playback.
// ============================================================================
int cmd_fmv_set(void)
{
    g_main_state_flags |= 0x40000;
    *(unsigned char*)&g_selectedFmvId = (unsigned char)(scd_read_u16(0) >> 8);
    g_fmvDataPointer = g_loadDataDestPointer;
    g_ScdOpcodes += 2;
    return 1;
}

// ============================================================================
// 0x2A - cmd_effect_spawn (0x004316c0)
// Spawn a billboard effect at a position.
// ============================================================================
int cmd_effect_spawn(void)
{
    unsigned short typeParam = scd_read_u16(0);
    unsigned short parentParam = scd_read_u16(2);
    int posX = (int)scd_read_s16(4);
    int posY = (int)scd_read_s16(6);
    int posZ = (int)scd_read_s16(8);
    unsigned short effectFlags = scd_read_u16(10);
    g_ScdOpcodes += 12;

    MATRIX* spriteInfo;
    unsigned int parentType = (unsigned int)(parentParam >> 8);
    if (parentType == 0) {
        spriteInfo = &g_identityMatrixData;
    } else if (parentType == 1) {
        spriteInfo = &g_playerEntity.scaMatrixData.localMatrix;
    } else if ((parentParam & 0x8000) == 0) {
        spriteInfo = (MATRIX*)(g_effectPool[parentType * 3 + 0x3d].animDataBase + 0x10);
    } else {
        spriteInfo = (MATRIX*)((int)g_itemboxes_covers_table[(parentParam & 0x7f00) >> 8] + 0x20);
    }

    Effect_CreateBillboard(
        (unsigned char)(typeParam >> 8),
        (unsigned char)parentParam,
        effectFlags,
        spriteInfo,
        &posX,
        0);
    return 1;
}

// ============================================================================
// 0x2B - cmd_player_anim_0x2b (0x00431990)
// Set player animation state for scripted actions.
// ============================================================================
int cmd_player_anim_0x2b(void)
{
    short val = scd_read_s16(0);
    unsigned short animParam = scd_read_u16(2);
    g_ScdOpcodes += 4;

    unsigned short adj = (unsigned short)(val + 0x200) & 0xff00;
    g_playerEntity.attackAnim = (unsigned char)animParam;
    g_playerEntity.anim_86 = (unsigned char)adj;
    g_playerEntity.anim_87 = (unsigned char)(adj >> 8);
    g_playerEntity.unk_be = (unsigned char)(animParam >> 8);
    g_playerEntity.unk_bf = 0;
    g_playerEntity.unk_8c = 0;
    g_playerEntity.animationId = 8;
    return 1;
}

// ============================================================================
// 0x2C - cmd_item_remove (0x004319e0)
// Remove an item from the player's inventory.
// ============================================================================
int cmd_item_remove(void)
{
    unsigned char itemId = g_ScdOpcodes[1];
    g_ScdOpcodes += 2;
    int slot = get_item_slot(itemId);
    if (slot >= 0) {
        ((unsigned char*)g_ItemSlotsPointer)[slot * 2] = 0;
        rearrange_item_slots();
        return 1;
    }
    return 0;
}

// ============================================================================
// 0x2D - cmd_got_item (0x00431a20)
// Trigger the "got item" room action and menu.
// ============================================================================
int cmd_got_item(void)
{
    cmd_room_action();
    g_main_state_flags |= 0x400;
    g_main_state_flags ^= 0x800;
    return 0;
}

// ============================================================================
// 0x2E - cmd_nop_0x2e (0x00460a70)
// ============================================================================
int cmd_nop_0x2e(void)
{
    return 1;
}

// ============================================================================
// 0x2F - cmd_0x2f (0x00460c00)
// Set up screen effect parameters.
// ============================================================================
int cmd_0x2f(void)
{
    unsigned short op1 = scd_read_u16(0);
    g_ScdOpcodes += 2;
    unsigned short op2 = scd_read_u16(0);
    g_ScdOpcodes += 2;
    unsigned int idx = (op1 & 0x1f) >> 5;
    FUN_004805d0((short)(unsigned char)DAT_00bf07ef, op1 >> 8, op2 & 0xff, (unsigned int)(op2 >> 8));
    *(unsigned int*)((char*)DAT_00ac98e0 + idx) = op2 & 0xff;
    *(unsigned int*)((char*)DAT_00ac98e4 + idx) = (unsigned int)(op2 >> 8);
    return 1;
}

// ============================================================================
// 0x30 - cmd_boundaries_0x30 (0x00431a40)
// Modify collision boundary data.
// ============================================================================
int cmd_boundaries_0x30(void)
{
    unsigned short op1 = scd_read_u16(0);
    g_ScdOpcodes += 2;
    unsigned short op2 = scd_read_u16(0);
    g_ScdOpcodes += 2;

    unsigned short* boundary = (unsigned short*)(
        (op2 & 0xff) * 0xc +
        *(int*)(((op1 >> 6) & 0xfffffffc) + 4 + (unsigned int)g_RdtPointer->boundaries));

    if ((op2 & 0xff00) != 0) {
        unsigned short flags = boundary[5];
        boundary[5] = (op2 & 0xf00) | (flags & 0xf0ff);
    }

    boundary[2] = scd_read_u16(0); g_ScdOpcodes += 2;
    boundary[3] = scd_read_u16(0); g_ScdOpcodes += 2;
    boundary[0] = scd_read_u16(0); g_ScdOpcodes += 2;
    boundary[1] = scd_read_u16(0); g_ScdOpcodes += 2;
    return 1;
}

// ============================================================================
// 0x31 - cmd_0x31 (0x004608d0)
// Set a fading state value.
// ============================================================================
int cmd_0x31(void)
{
    unsigned short op1 = scd_read_u16(0);
    unsigned short value = scd_read_u16(2);
    g_ScdOpcodes += 4;
    ((short*)&g_fading_state)[(op1 >> 7) & 0xfffe] = value;
    return 1;
}

// ============================================================================
// 0x32 - cmd_skip_4bytes (0x00431b00)
// Skip 4 bytes in the opcode stream.
// ============================================================================
int cmd_skip_4bytes(void)
{
    g_ScdOpcodes += 4;
    return 1;
}

// ============================================================================
// 0x33 - cmd_damage_set (0x004314b0)
// Modify player damage/combat state.
// ============================================================================
int cmd_damage_set(void)
{
    unsigned short* params = (unsigned short*)(g_ScdOpcodes + 2);
    unsigned char subCmd = (unsigned char)(scd_read_u16(0) >> 8);

    switch (subCmd) {
    case 0:
        g_EquippedItemId = 0;
        g_playerEntity.equippedWeaponId = 0;
        g_ScdOpcodes += 2;
        return 1;
    case 1:
        g_playerEntity.isBeingAttackedFlag = (unsigned char)*params;
        g_playerEntity.unk_be = 0;
        g_playerEntity.unk_bf = 0;
        g_playerEntity.attackDirection = 100;
        g_ScdOpcodes += 4;
        g_playerEntity.unk_c2 = 0;
        g_playerEntity.attackAnim = 0;
        *(unsigned int*)&g_playerEntity.animationId = 0x01000001;
        g_playerEntity.unk_8c = 3;
        return 1;
    case 3: {
        char mode = (char)(*params >> 8);
        unsigned char val = (unsigned char)*params;
        if (mode == 0) g_playerEntity.flags = val;
        else if (mode == 1) g_playerEntity.flags |= val;
        else if (mode == 2) g_playerEntity.flags ^= val;
        g_ScdOpcodes += 4;
        return 1;
    }
    case 4:
        g_playerEntity.anim_86 = 1;
        g_playerEntity.anim_87 = 6;
        g_ScdOpcodes += 2;
        return 1;
    case 5:
        g_playerEntity.directionAngle = *params;
        g_ScdOpcodes += 4;
        return 1;
    case 6:
        g_playerEntity.unk_8c = 0;
        g_ScdOpcodes += 2;
        return 1;
    case 7:
        g_playerEntity.animationId = 1;
        g_playerEntity.animFrameId = 0;
        g_playerEntity.anim_86 = 0;
        g_playerEntity.anim_87 = 2;
        g_playerEntity.isBeingAttackedFlag = 0;
        g_playerEntity.unk_be = 0;
        g_playerEntity.unk_bf = 0;
        g_playerEntity.attackAnim = 0;
        g_playerEntity.unk_8c = 3;
        g_ScdOpcodes += 2;
        return 1;
    case 8: {
        char mode = (char)(*params >> 8);
        unsigned char val = (unsigned char)*params;
        if (mode == 0) g_playerEntity.healthStatusFlags = val;
        else if (mode == 1) g_playerEntity.healthStatusFlags |= val;
        else if (mode == 2) g_playerEntity.healthStatusFlags ^= val;
        g_ScdOpcodes += 4;
        return 1;
    }
    case 9: {
        JointStruct* joints = g_playerEntity.jointsStructs;
        for (unsigned short bits = *params; bits != 0; bits >>= 1) {
            joints->flags ^= (unsigned char)bits & 1;
            joints++;
        }
        g_ScdOpcodes += 4;
        return 1;
    }
    case 10:
        if ((*params & 0xff00) == 0) {
            *(unsigned short*)&g_playerEntity.unk_e0 &= ~0x40u;
        } else {
            *(unsigned short*)&g_playerEntity.unk_e0 |= 0x40;
        }
        g_ScdOpcodes += 4;
        return 1;
    default:
        return 1;
    }
}

// ============================================================================
// 0x34 - cmd_0x34 (0x00431b10)
// Modify lighting/texture parameters.
// ============================================================================
int cmd_0x34(void)
{
    g_ScdOpcodes++;
    char param1 = (char)*g_ScdOpcodes; g_ScdOpcodes++;
    char param2 = (char)*g_ScdOpcodes - 0x80; g_ScdOpcodes++;
    unsigned char p3 = *g_ScdOpcodes; g_ScdOpcodes++;
    unsigned char p4 = *g_ScdOpcodes; g_ScdOpcodes++;
    unsigned char p5 = *g_ScdOpcodes; g_ScdOpcodes++;
    unsigned short p6 = *g_ScdOpcodes; g_ScdOpcodes++;
    unsigned short p7 = *g_ScdOpcodes; g_ScdOpcodes++;

    if (param1 == 0) {
        FUN_00473b10(p5, p6, p7, p3, p4, param2);
    } else if (param1 == 1) {
        FUN_00473d10(p5, p6, p7, p3, p4, param2);
    } else if (param1 == 2) {
        FUN_00473d60(param2, p3, p4);
    }
    return 1;
}

// ============================================================================
// 0x35 - cmd_0x35 (0x00431bf0)
// Modify object entity flags/state.
// ============================================================================
int cmd_0x35(void)
{
    unsigned short op1 = scd_read_u16(0);
    g_ScdOpcodes += 2;
    unsigned short op2 = scd_read_u16(0);
    g_ScdOpcodes += 2;

    // Special case: stage 3 room 13 object 5
    if (g_stageId == 3 && g_roomId == 13 && ((unsigned char)op2 & 0x3f) == 5) {
        *(unsigned char*)g_itemboxes_covers_table[op2 & 0xff] = 0;
        return 1;
    }

    unsigned char value = (unsigned char)(op2 >> 8);
    if (op1 >> 8 == 0) {
        *(unsigned char*)g_itemboxes_covers_table[op2 & 0xff] = value;
    } else if (op1 >> 8 == 1) {
        *(unsigned char*)g_desks_pointers_table[op2 & 0xff] = value;
    }
    return 1;
}

// ============================================================================
// 0x36 - cmd_0x36 (0x00431c90)
// Compare an object entity field against a value.
// ============================================================================
int cmd_0x36(void)
{
    unsigned short op1 = scd_read_u16(0);
    g_ScdOpcodes += 2;
    unsigned short op2 = scd_read_u16(0);
    g_ScdOpcodes += 2;

    unsigned short* objField = (unsigned short*)(
        *(int*)((int)&g_itemboxes_covers_table + ((op1 >> 6) & 0xfffffffc)) + 0x86);
    unsigned short fieldVal = *objField;
    unsigned short compareVal = op2 >> 8;

    switch (op2 & 0xff) {
    case 0: return compareVal == fieldVal;
    case 1: return fieldVal > compareVal;
    case 2: return fieldVal >= compareVal;
    case 3: return compareVal > fieldVal;
    case 4: return compareVal >= fieldVal;
    case 5: return compareVal != fieldVal;
    default: return 0;
    }
}

// ============================================================================
// 0x37 - cmd_0x37 (0x00460a30)
// Set room BGM state data.
// ============================================================================
int cmd_0x37(void)
{
    unsigned short op1 = scd_read_u16(0);
    g_ScdOpcodes += 2;
    unsigned short op2 = scd_read_u16(0);
    g_ScdOpcodes += 2;
    unsigned int idx = ((op1 & 0x07) >> 3) + (op2 & 0xff);
    g_RoomBgmStateData[idx] = (unsigned char)(op2 >> 8);
    return 1;
}

// ============================================================================
// 0x38 - cmd_0x38 (0x00431dc0)
// Test player D-pad held state.
// ============================================================================
int cmd_0x38(void)
{
    unsigned short op1 = scd_read_u16(0);
    g_ScdOpcodes += 2;
    unsigned short op2 = scd_read_u16(0);
    g_ScdOpcodes += 2;

    if ((op1 & 0xff00) != 0) {
        return ((g_PlayerDpadHeld & op2) == 0);
    }
    return g_PlayerDpadHeld & op2;
}

// ============================================================================
// 0x39 - cmd_0x39 (0x00431e10)
// Read enemy behavior_flags into DAT_00be982a.
// ============================================================================
int cmd_0x39(void)
{
    unsigned short op1 = scd_read_u16(0);
    g_ScdOpcodes += 2;
    DAT_00be982a = g_EnemiesList[op1 >> 8].behavior_flags;
    return 1;
}

// ============================================================================
// 0x3A - cmd_cut_0x3a (0x00431e50)
// Modify camera switch zone entries.
// ============================================================================
int cmd_cut_0x3a(void)
{
    unsigned char zoneIdx = g_ScdOpcodes[1];
    *(unsigned short*)((unsigned int)zoneIdx * 0x14 + 2 + (unsigned int)g_RdtPointer->cam_switch_zones) =
        (unsigned short)g_ScdOpcodes[2];
    *(unsigned short*)((unsigned int)zoneIdx * 0x14 + (unsigned int)g_RdtPointer->cam_switch_zones) =
        (unsigned short)g_ScdOpcodes[3];
    g_ScdOpcodes += 4;
    return 1;
}

// ============================================================================
// 0x3B - cmd_0x3b (0x00431ea0)
// Set object animation parameters.
// ============================================================================
int cmd_0x3b(void)
{
    unsigned short op1 = scd_read_u16(0);
    g_ScdOpcodes += 2;
    char* objPtr;
    if (op1 < 0x8000) {
        objPtr = (char*)g_desks_pointers_table[(op1 >> 6) & 0x3f];
    } else {
        objPtr = (char*)g_itemboxes_covers_table[(op1 & 0x7f00) >> 8];
    }
    if (*objPtr != 0) {
        *(unsigned short*)(objPtr + 0x72) = scd_read_u16(0);
        g_ScdOpcodes += 2;
        *(unsigned short*)(objPtr + 0x76) = scd_read_u16(0);
        g_ScdOpcodes += 2;
    } else {
        g_ScdOpcodes += 4;
    }
    return 1;
}

// ============================================================================
// 0x3C - cmd_0x3c (0x00431f20)
// Test distance between player and an entity/object.
// ============================================================================
int cmd_0x3c(void)
{
    g_ScdOpcodes += 6;
    unsigned short targetSpec = scd_read_u16(-4);
    unsigned short maxDist = scd_read_u16(-2);

    int* targetPos;
    if ((targetSpec & 0xff) == 0) {
        targetPos = g_EnemiesList[targetSpec >> 8].scaMatrixData.localMatrix.t;
    } else if ((targetSpec & 0xff) == 1) {
        targetPos = (int*)(*(int*)((int)&g_itemboxes_covers_table + ((targetSpec >> 6) & 0xfffffffc)) + 0x34);
    } else if ((targetSpec & 0xff) == 2) {
        targetPos = (int*)(*(int*)((int)&g_desks_pointers_table + ((targetSpec >> 6) & 0xfffffffc)) + 0x34);
    } else {
        return 0;
    }

    int dx = g_playerEntity.scaMatrixData.localMatrix.t[0] - targetPos[0];
    int dz = g_playerEntity.scaMatrixData.localMatrix.t[2] - targetPos[2];
    unsigned int dist = SquareRoot0(dz * dz + dx * dx);
    return dist <= (unsigned int)maxDist;
}

// ============================================================================
// 0x3D - cmd_bullet_0x3d (0x00431770)
// Spawn a bullet/hit effect.
// ============================================================================
int cmd_bullet_0x3d(void)
{
    unsigned short typeParam = scd_read_u16(0);
    unsigned short parentParam = scd_read_u16(2);
    int posX = (int)scd_read_s16(4);
    int posY = (int)scd_read_s16(6);
    int posZ = (int)scd_read_s16(8);
    unsigned short effectFlags = scd_read_u16(10);
    g_ScdOpcodes += 12;

    MATRIX* spriteInfo;
    if ((parentParam >> 8) == 0) {
        spriteInfo = &g_identityMatrixData;
    } else if ((parentParam >> 8) == 1) {
        spriteInfo = &g_playerEntity.scaMatrixData.localMatrix;
    } else if ((parentParam & 0x8000) == 0) {
        spriteInfo = (MATRIX*)(g_effectPool[((unsigned int)(typeParam >> 8) >> 8) * 3 + 0x3d].animDataBase + 0x10);
    } else {
        spriteInfo = (MATRIX*)(*(int*)((int)&g_itemboxes_covers_table + ((typeParam >> 6) & 0xfffffffc)) + 0x20);
    }

    unsigned char effectType = (unsigned char)(typeParam >> 8);
    Effect_CreateBillboard(effectType, (unsigned char)parentParam, effectFlags, spriteInfo, &posX, 0);
    DAT_00be982b = effectType;
    DAT_00bf0a34 = (int)spriteInfo;
    return 1;
}

// ============================================================================
// 0x3E - cmd_0x3f (0x00431840)
// Trigger a previously set up bullet effect.
// ============================================================================
int cmd_0x3f(void)
{
    FUN_0047cf80(9, DAT_00be982b, 0, 0, (MATRIX*)DAT_00bf0a34);
    g_ScdOpcodes += 2;
    return 1;
}

// ============================================================================
// 0x3F - cmd_player_dir_set (0x00431fd0)
// Test if player direction is within a range.
// ============================================================================
int cmd_player_dir_set(void)
{
    unsigned short minAngle = scd_read_u16(2);
    unsigned short maxAngle = scd_read_u16(4);
    g_ScdOpcodes += 6;
    unsigned short playerDir = g_playerEntity.directionAngle;
    return (int)(unsigned short)(playerDir - minAngle) <= (int)((unsigned int)maxAngle - (unsigned int)minAngle);
}

// ============================================================================
// 0x40 - cmd_lights_0x41 (0x00432010)
// Modify a light source in the RDT.
// ============================================================================
int cmd_lights_0x41(void)
{
    short* params = (short*)g_ScdOpcodes;
    g_ScdOpcodes += 16;
    int lightIdx = params[0] >> 8;
    int* light = (int*)((char*)&g_RdtPointer[1].lights + lightIdx * 0x2c - 4);
    light[0] = (int)params[1];
    light[1] = (int)params[2];
    light[2] = (int)params[3];
    light[3] = (int)params[4];
    light[4] = (int)params[5];
    light[5] = (int)params[6];
    light[8] = (int)params[7];
    return 1;
}

// ============================================================================
// 0x41 - cmd_0x42 (0x00432090)
// Set an entity unk_8e field.
// ============================================================================
int cmd_0x42(void)
{
    unsigned short op1 = scd_read_u16(0);
    g_ScdOpcodes += 2;
    unsigned short value = scd_read_u16(0);
    g_ScdOpcodes += 2;
    if ((op1 & 0xff00) == 0) {
        *(unsigned short*)&g_playerEntity.unk_8e = value;
    } else {
        *(unsigned short*)((char*)&g_playerEntity + (unsigned int)(op1 >> 8) * 0x18c + 0x82) = value;
    }
    return 1;
}

// ============================================================================
// 0x42 - cmd_0x43 (0x00431870)
// Trigger effect type 3.
// ============================================================================
int cmd_0x43(void)
{
    unsigned short type = scd_read_u16(0);
    unsigned short param = scd_read_u16(2);
    g_ScdOpcodes += 4;
    FUN_0047cf80(3, type >> 8, param, 0, 0);
    return 1;
}

// ============================================================================
// 0x43 - cmd_0x44 (0x00460d20)
// Modify BGM sound parameters if track is playing.
// ============================================================================
int cmd_0x44(void)
{
    unsigned short op1 = scd_read_u16(0);
    g_ScdOpcodes += 2;
    unsigned short op2 = scd_read_u16(0);
    g_ScdOpcodes += 2;
    unsigned int bit = 1 << ((char)(op1 >> 8) + 3);
    if ((g_BGM_STATE & bit) != 0) {
        FUN_004804a0((short)(unsigned char)DAT_00bf07ef, op1 >> 8, (short)(char)op2, op2 >> 8);
    }
    return 1;
}

// ============================================================================
// 0x44 - cmd_0x45 (0x00461080)
// Deactivate a specific SCD event slot.
// ============================================================================
int cmd_0x45(void)
{
    g_ScdEventTable[scd_read_u16(0) >> 8].active = 0;
    g_ScdOpcodes += 2;
    return 1;
}

// ============================================================================
// 0x45 - cmd_0x46 (0x004320f0)
// Add to player unk_8e field.
// ============================================================================
int cmd_0x46(void)
{
    unsigned short val = scd_read_u16(0);
    g_ScdOpcodes += 2;
    *(short*)&g_playerEntity.unk_8e += (char)(val >> 8);
    return 1;
}

// ============================================================================
// 0x46 - cmd_light_set_0x47 (0x00432110)
// Set all room lights from SCD data.
// ============================================================================
int cmd_light_set_0x47(void)
{
    g_ScdOpcodes += 4;
    for (unsigned int offset = 0; offset < 0x3c; offset += 0x14) {
        int* light = (int*)((char*)&g_RdtPointer->lights[0].pos_x + offset);
        light[0] = (int)scd_read_s16(0);
        light[1] = (int)scd_read_s16(2);
        light[2] = (int)scd_read_s16(4);
        ((unsigned char*)light)[12] = g_ScdOpcodes[6];
        ((unsigned char*)light)[13] = g_ScdOpcodes[7];
        ((unsigned char*)light)[14] = g_ScdOpcodes[8];
        *(unsigned short*)((char*)light + 16) = (unsigned short)g_ScdOpcodes[9];
        *(short*)((char*)light + 20) = scd_read_s16(10);
        g_ScdOpcodes += 12;
    }
    for (unsigned int i = 0; i < 6; i += 2) {
        *(short*)(g_RdtPointer->unknown_03 + i + 3) = scd_read_s16(0);
        g_ScdOpcodes += 2;
    }
    setBackColor((unsigned char)g_RdtPointer->ambient_light_r, (unsigned char)g_RdtPointer->ambient_light_g, (unsigned char)g_RdtPointer->ambient_light_b);
    return 1;
}

// ============================================================================
// 0x47 - cmd_0x48 (0x00431080)
// Set object position and rotation offsets.
// ============================================================================
int cmd_0x48(void)
{
    unsigned short slot = scd_read_u16(0);
    g_ScdOpcodes += 14;
    int objBase = (int)g_itemboxes_covers_table[slot >> 8];
    *(short*)(objBase + 0x72) = scd_read_s16(-12);
    *(short*)(objBase + 0x74) = scd_read_s16(-10);
    *(short*)(objBase + 0x76) = scd_read_s16(-8);
    short posX = scd_read_s16(-6);
    *(short*)(objBase + 0x6c) = posX;
    *(int*)(objBase + 0x34) = (int)posX;
    short posY = scd_read_s16(-4);
    *(short*)(objBase + 0x6e) = posY;
    *(int*)(objBase + 0x38) = (int)posY;
    short posZ = scd_read_s16(-2);
    *(short*)(objBase + 0x70) = posZ;
    *(int*)(objBase + 0x3c) = (int)posZ;
    return 1;
}

// ============================================================================
// 0x48 - cmd_0x49 (0x004318a0)
// Clear all effect pool entries.
// ============================================================================
int cmd_0x49(void)
{
    g_freeEffectSlots = 0;
    g_ScdOpcodes += 4;
    do {
        g_effectPool[g_freeEffectSlots].updateId = 0;
        g_effectPool[g_freeEffectSlots].animId = g_effectPool[g_freeEffectSlots].updateId;
        g_freeEffectSlots++;
    } while (g_freeEffectSlots < 0x40);
    return 1;
}

// ============================================================================
// 0x49 - cmd_0x4a (0x00432290)
// Set or clear DAT_00d22770 flags.
// ============================================================================
int cmd_0x4a(void)
{
    unsigned short val = scd_read_u16(0);
    g_ScdOpcodes += 2;
    if ((val >> 8) == 0xff) {
        DAT_00d22770 = 0;
    } else {
        DAT_00d22770 |= 1 << ((unsigned char)(val >> 8) & 0x1f);
    }
    return 1;
}

// ============================================================================
// 0x4A - cmd_snd_set0x4b (0x00460ae0)
// Restore and play sound slots.
// ============================================================================
int cmd_snd_set0x4b(void)
{
    g_ScdOpcodes += 4;
    if ((int)DAT_00bf07f0 != -1) {
        g_BGM_STATE = g_BGM_STATE >> 8;
        if ((g_BGM_STATE & 8) != 0 && g_SndBank[0] != 0) {
            SetSndSlot(g_SndBank[0], (int)g_snd_slot_00ac99d5);
        }
        if ((g_BGM_STATE & 0x10) != 0 && g_snd_bank_00ac99d8 != 0) {
            SetSndSlot(g_snd_bank_00ac99d8, (int)g_snd_slot_00ac99dd);
        }
        if ((g_BGM_STATE & 0x20) != 0 && g_snd_bank_00ac99e0 != 0) {
            SetSndSlot(g_snd_bank_00ac99e0, (int)g_snd_slot_00ac99e5);
        }
    }
    return 1;
}

// ============================================================================
// 0x4B - cmd_0x4c (0x00460b80)
// Stop all sound banks and shift BGM state.
// ============================================================================
int cmd_0x4c(void)
{
    g_ScdOpcodes += 4;
    if ((int)DAT_00bf07f0 != -1) {
        if (g_SndBank[0] != 0) setSndStop(g_SndBank[0]);
        if (g_snd_bank_00ac99d8 != 0) setSndStop(g_snd_bank_00ac99d8);
        if (g_snd_bank_00ac99e0 != 0) setSndStop(g_snd_bank_00ac99e0);
        if (g_BgmSoundBank != 0) setSndStop(g_BgmSoundBank);
        g_BGM_STATE = g_BGM_STATE << 8;
    }
    return 1;
}

// ============================================================================
// 0x4C - cmd_0x4d (0x004322d0)
// Item slot data transfer operations.
// ============================================================================
int cmd_0x4d(void)
{
    unsigned short op1 = scd_read_u16(0);
    unsigned short op2 = scd_read_u16(2);
    g_ScdOpcodes += 4;

    unsigned int mode = op1 >> 8;
    unsigned int slotIdx = op2 & 0xff;
    unsigned int fieldIdx = op2 >> 8;

    if (mode == 0) {
        (&g_stageId)[fieldIdx] = ((unsigned char*)g_RoomItemEventTable + slotIdx * 0xc + 8)[1];
    } else if (mode == 1) {
        ((unsigned char*)g_RoomItemEventTable + slotIdx * 0xc + 8)[1] = (&g_stageId)[fieldIdx];
    } else if (mode == 2) {
        int itemSlot = get_item_slot(((unsigned char*)g_RoomItemEventTable + slotIdx * 0xc + 8)[0]);
        if (itemSlot >= 0) {
            unsigned char val = ((unsigned char*)g_ItemSlotsPointer)[itemSlot * 2 + 1];
            (&g_stageId)[fieldIdx] = val;
            ((unsigned char*)g_RoomItemEventTable + slotIdx * 0xc + 8)[1] = val;
        }
    }
    return 1;
}

// ============================================================================
// 0x4D - cmd_0x4e (0x004323a0)
// Reset player entity lighting/palette to default grey.
// ============================================================================
int cmd_0x4e(void)
{
    // This function calls FUN_0048a190 multiple times with different entity
    // sub-structure offsets to reset lighting colors to 0x606060 (medium grey).
    // The entity offsets are: 0x00, 0x7c, 0xf8, 0x45c, 0x5d0, 0x174, 0x1f0,
    //                        0x26c, 0x2e8, 0x364, 0x3e0, 0x4d8, 0x554, 0x64c
    // Each call: FUN_0048a190(offset_ptr, 0x30, 0x00080820, 0x00606060)
    extern void FUN_0048a190(void* ptr, int size, int flags, int color);
    int* entity = (int*)&g_playerEntity;
    FUN_0048a190(entity, 0x30, 0x00080820, 0x00606060);
    FUN_0048a190((char*)entity + 0x7c, 0x30, 0x00080820, 0x00606060);
    FUN_0048a190((char*)entity + 0xf8, 0x30, 0x00080820, 0x00606060);
    FUN_0048a190((char*)entity + 0x45c, 0x30, 0x00080820, 0x00606060);
    FUN_0048a190((char*)entity + 0x5d0, 0x30, 0x00080820, 0x00606060);
    FUN_0048a190((char*)entity + 0x174, 0x30, 0x00080820, 0x00606060);
    FUN_0048a190((char*)entity + 0x1f0, 0x30, 0x00080820, 0x00606060);
    FUN_0048a190((char*)entity + 0x26c, 0x30, 0x00080820, 0x00606060);
    FUN_0048a190((char*)entity + 0x2e8, 0x30, 0x00080820, 0x00606060);
    FUN_0048a190((char*)entity + 0x364, 0x30, 0x00080820, 0x00606060);
    FUN_0048a190((char*)entity + 0x3e0, 0x30, 0x00080820, 0x00606060);
    FUN_0048a190((char*)entity + 0x4d8, 0x30, 0x00080820, 0x00606060);
    FUN_0048a190((char*)entity + 0x554, 0x30, 0x00080820, 0x00606060);
    FUN_0048a190((char*)entity + 0x64c, 0x30, 0x00080820, 0x00606060);
    g_ScdOpcodes += 2;
    return 1;
}

// ============================================================================
// 0x4E - cmd_0x4f (0x00431910)
// Modify flags on all active effect pool entries.
// ============================================================================
int cmd_0x4f(void)
{
    unsigned short op1 = scd_read_u16(0);
    g_ScdOpcodes += 2;
    unsigned short op2 = scd_read_u16(0);
    g_ScdOpcodes += 2;
    unsigned int mode = op1 >> 8;

    unsigned char* ptr = (unsigned char*)&g_effectPool[0].animId + 2;
    while (ptr < (unsigned char*)&g_playerEntity.unk_0e) {
        Effect* eff = (Effect*)(ptr - 2);
        if (eff->animId != 0 || eff->updateId != 0) {
            unsigned short* flagsPtr = (unsigned short*)ptr;
            if (mode == 0) *flagsPtr |= op2;
            else if (mode == 1) *flagsPtr &= ~op2;
            else if (mode == 2) *flagsPtr ^= op2;
        }
        ptr += 0x84;
    }
    return 1;
}

// ============================================================================
// 0x4F - cmd_0x50 (0x004622b0)
// Call FUN_0040c560 with parameter.
// ============================================================================
int cmd_0x50(void)
{
    extern void FUN_0040c560(int param);
    FUN_0040c560(scd_read_u16(0) >> 8);
    g_ScdOpcodes += 2;
    return 1;
}

// ============================================================================
// 0x50 - cmd_0x51 (0x004622e0)
// Call cmd_0x51_inner with 2-byte parameter.
// ============================================================================
int cmd_0x51(void)
{
    g_ScdOpcodes += 2;
    return (int)DAT_004d6444;
}

// ============================================================================
// script_command_funcs_table (0x004c1110)
// SCD command dispatch table - 81 entries (opcodes 0x00-0x50).
// ============================================================================
typedef int (*ScdCmdFunc)(void);

void* script_command_funcs_table[256] = {
    /* 0x00 */ (void*)cmd_nop,               // 0x004604d0
    /* 0x01 */ (void*)cmd_if,                // 0x004604e0
    /* 0x02 */ (void*)cmd_else,              // 0x00460520
    /* 0x03 */ (void*)cmd_end_if,            // 0x00460550
    /* 0x04 */ (void*)cmd_bit_test,          // 0x00460570
    /* 0x05 */ (void*)cmd_bit_op,            // 0x00460650
    /* 0x06 */ (void*)cmd_obj06_test,        // 0x00460760
    /* 0x07 */ (void*)cmd_obj07_test,        // 0x00460800
    /* 0x08 */ (void*)cmd_room_cam_set,      // 0x004608a0
    /* 0x09 */ (void*)cmd_cut_set_0x09,      // 0x00460920
    /* 0x0A */ (void*)cmd_current_cut_set,   // 0x00460990
    /* 0x0B */ (void*)cmd_message_set,       // 0x004609f0
    /* 0x0C */ (void*)cmd_door_set,          // 0x004611b0
    /* 0x0D */ (void*)cmd_item_set,          // 0x00461130
    /* 0x0E */ (void*)cmd_skip_2bytes_opcode,// 0x00460900
    /* 0x0F */ (void*)cmd_entities_0x0f,     // 0x004610b0
    /* 0x10 */ (void*)cmd_obj10_test,        // 0x00460f30
    /* 0x11 */ (void*)cmd_obj11_test,        // 0x00460f10
    /* 0x12 */ (void*)cmd_item_flag_0x12,    // 0x00460fc0
    /* 0x13 */ (void*)cmd_0x13,              // 0x00461010
    /* 0x14 */ (void*)cmd_0x14,              // 0x00461040
    /* 0x15 */ (void*)cmd_bgm_0x15,          // 0x00460a80
    /* 0x16 */ (void*)cmd_volume_set,        // 0x00460c70
    /* 0x17 */ (void*)cmd_player_pos_0x17,   // 0x00460d80
    /* 0x18 */ (void*)cmd_item_model_set,    // 0x00461220
    /* 0x19 */ (void*)cmd_obj19_set,         // 0x00460f50
    /* 0x1A */ (void*)cmd_item_search,       // 0x00460f80
    /* 0x1B */ (void*)cmd_em_set,            // 0x004617d0
    /* 0x1C */ (void*)cmd_0x1c,              // 0x00462210
    /* 0x1D */ (void*)cmd_weapon_set,        // 0x00460ee0
    /* 0x1E */ (void*)cmd_sfx_set,           // 0x00461a80
    /* 0x1F */ (void*)cmd_omodel_set,        // 0x00461ac0
    /* 0x20 */ (void*)cmd_player_pos_set,    // 0x00430f60
    /* 0x21 */ (void*)cmd_enemy_pos_set,     // 0x00430fe0
    /* 0x22 */ (void*)cmd_item_cmd_0x22,     // 0x00431100
    /* 0x23 */ (void*)cmd_cut_toogle,        // 0x00431280
    /* 0x24 */ (void*)cmd_room_action_impl,  // 0x004312b0
    /* 0x25 */ (void*)cmd_rdt_0x25,          // 0x004621d0
    /* 0x26 */ (void*)cmd_nop_0x26,          // 0x00460ce0
    /* 0x27 */ (void*)cmd_snd_fade_set,      // 0x00460cf0
    /* 0x28 */ (void*)cmd_enemy_0x28,        // 0x004312f0
    /* 0x29 */ (void*)cmd_fmv_set,           // 0x00461a40
    /* 0x2A */ (void*)cmd_effect_spawn,      // 0x004316c0
    /* 0x2B */ (void*)cmd_player_anim_0x2b,  // 0x00431990
    /* 0x2C */ (void*)cmd_item_remove,       // 0x004319e0
    /* 0x2D */ (void*)cmd_got_item,          // 0x00431a20
    /* 0x2E */ (void*)cmd_nop_0x2e,          // 0x00460a70
    /* 0x2F */ (void*)cmd_0x2f,              // 0x00460c00
    /* 0x30 */ (void*)cmd_boundaries_0x30,   // 0x00431a40
    /* 0x31 */ (void*)cmd_0x31,              // 0x004608d0
    /* 0x32 */ (void*)cmd_skip_4bytes,       // 0x00431b00
    /* 0x33 */ (void*)cmd_damage_set,        // 0x004314b0
    /* 0x34 */ (void*)cmd_0x34,              // 0x00431b10
    /* 0x35 */ (void*)cmd_0x35,              // 0x00431bf0
    /* 0x36 */ (void*)cmd_0x36,              // 0x00431c90
    /* 0x37 */ (void*)cmd_0x37,              // 0x00460a30
    /* 0x38 */ (void*)cmd_0x38,              // 0x00431dc0
    /* 0x39 */ (void*)cmd_0x39,              // 0x00431e10
    /* 0x3A */ (void*)cmd_cut_0x3a,          // 0x00431e50
    /* 0x3B */ (void*)cmd_0x3b,              // 0x00431ea0
    /* 0x3C */ (void*)cmd_0x3c,              // 0x00431f20
    /* 0x3D */ (void*)cmd_bullet_0x3d,       // 0x00431770
    /* 0x3E */ (void*)cmd_0x3f,              // 0x00431840
    /* 0x3F */ (void*)cmd_player_dir_set,    // 0x00431fd0
    /* 0x40 */ (void*)cmd_lights_0x41,       // 0x00432010
    /* 0x41 */ (void*)cmd_0x42,              // 0x00432090
    /* 0x42 */ (void*)cmd_0x43,              // 0x00431870
    /* 0x43 */ (void*)cmd_0x44,              // 0x00460d20
    /* 0x44 */ (void*)cmd_0x45,              // 0x00461080
    /* 0x45 */ (void*)cmd_0x46,              // 0x004320f0
    /* 0x46 */ (void*)cmd_light_set_0x47,    // 0x00432110
    /* 0x47 */ (void*)cmd_0x48,              // 0x00431080
    /* 0x48 */ (void*)cmd_0x49,              // 0x004318a0
    /* 0x49 */ (void*)cmd_0x4a,              // 0x00432290
    /* 0x4A */ (void*)cmd_snd_set0x4b,       // 0x00460ae0
    /* 0x4B */ (void*)cmd_0x4c,              // 0x00460b80
    /* 0x4C */ (void*)cmd_0x4d,              // 0x004322d0
    /* 0x4D */ (void*)cmd_0x4e,              // 0x004323a0
    /* 0x4E */ (void*)cmd_0x4f,              // 0x00431910
    /* 0x4F */ (void*)cmd_0x50,              // 0x004622b0
    /* 0x50 */ (void*)cmd_0x51,              // 0x004622e0
    // Opcodes 0x51-0xFF: fill with cmd_nop as safe default
    // (entries 0x51-0xF5 may be accessed; 0xF6-0xFF are handled by room_events_check)
};
