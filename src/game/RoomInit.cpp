// RoomInit.cpp - Room initialization (decompiled from Ghidra)
#include "../Globals.h"
#include "../marni/MarniSystem.h"
#include "../marni/PSXTexture.h"
#include "FileLoader.h"
#include "SpriteRenderer.h"
#include <cstdio>

// Forward declarations for helpers defined in other files
extern void SetupCharacterData(void);
extern void LoadSoundBank(int, void*);
extern void load_character_sfx(unsigned char);

extern void Object_DeleteAll(int);
extern void object_delete_00442170(int category);
extern void texture_queue_reset(void);
extern void Flg_on(int baseAddr, unsigned int bitIndex);

// Room sub-function stubs from RoomStubs.cpp
extern void InitRoomEffSprite(void);
extern void room_events_check(void);
extern void load_slides_images(void);
extern void load_room_bg(void);
extern void load_room_bg_masks(void);
extern void check_camera_switch(int param);
extern void display_room_camera_bg(void);
extern void Room_SetupCollisionCallbacks(void);
extern void Room_LoadEnemySoundBanks(void);
extern void SetupEntityJointAnimation(void);
extern void SetupTextureBankData(short param);

// Entity model loading functions from EntityModelLoader.cpp
extern void LoadEntityEMD(Entity* em, unsigned char entity_id);
extern void Entity_SetJoints(Entity* em, unsigned int param2);
extern void InitAnimStructure(void* animHeaderValue);
extern unsigned int SetupJointStructures(unsigned int param1);

// SCD script runner
extern void run_command_functions(unsigned short* scd_opcodes);

// Texture helpers defined elsewhere
extern void delete_texture_set_secondary(unsigned char);
extern void TexturePage_DeleteSet(unsigned char);

// ============================================================================
// set_message_display (0x00455670)
// Sets up message display state. Called when the game needs to show a text
// message (loading screens, item descriptions, door prompts, etc.).
//
// msg_id: Message identifier
//   - bit 7 (0x80): speed up flag
//   - bit 6 (0x40): if set, use global_messages table; if clear, use RDT messages
//   - bits 0-5: message index within the table
// pause_game: if non-zero, pause game flags during message display
//
// Returns: 0 = message display started, 1 = already displaying (rejected)
// ============================================================================
unsigned int set_message_display(unsigned short msg_id, unsigned short pause_game)
{
    short screenY;

    if ((g_menu_choice_id & 0x80) != 0) {
        return 1;
    }

    g_menu_choice_id = 0x80;

    g_messageFlagsBackup = g_message_flags;
    g_PauseGameInMsgFlag = pause_game;
    g_message_flags = g_message_flags & ~pause_game;

    if ((msg_id & 0x40) == 0) {
        // Room-specific message from RDT data
        // messages pointer at RDT+0x74 points to a message table:
        //   [offset_table: uint16[msg_count]] followed by [message_data...]
        if (g_RdtPointer != NULL && g_RdtPointer->messages != NULL) {
            unsigned char* msgBase = g_RdtPointer->messages;
            unsigned short offset = *(unsigned short*)(msgBase + (msg_id & 0x3F) * 2);
            g_MessagePtr = msgBase + offset;
        }
    } else {
        // Global message from the global_messages lookup table
        g_MessagePtr = global_messages[msg_id & 0x3F];
    }

    g_MessageStateCounter = 0;

    if ((*(((BYTE*)&g_main_state_flags) + 1) == 0)) {
        screenY = 181;
    } else {
        screenY = 186;
    }

    g_MessageScreenY = screenY - (short)g_ScreenOffsetY;
    g_lastScanCodeOrMsgID = (DWORD)msg_id;
    g_MessageSpeedUpFlag = (unsigned char)(msg_id & 0x80);

    return 0;
}

// ============================================================================
// display_game_loading_message (0x00481930)
// Task function: displays the appropriate loading message based on game state.
// Runs as a parallel task during InitializeGame.
//
// Message IDs (bit 6 set = global message table):
//   0x5B = "New game" / initial loading message
//   0x5C = "Loading game" / loading saved game message
//   0x5D = "Soft reset" / attract mode transition message
// ============================================================================
void display_game_loading_message(void)
{
    if ( (g_main_state_flags2 & 0x10000000) != 0 ) {
        set_message_display(0x5d, 0);
        Task_exit();
    }
    if ( (g_main_state_flags & 0x10000000) != 0 ) {
        set_message_display(0x5c, 0);
        Task_exit();
    }
    set_message_display(0x5b, 0);
    Task_exit();
}

// ============================================================================
// init_room (0x00409990)
// Initializes room state: sets up stage data pointer, room relations table,
// animation function pointers, and calls room_set for full room initialization.
// ============================================================================
void init_room(void)
{
    g_AttractMode_RoomCameraId = 0x1f;
    g_BGM_STATE = 0xFF;
    g_loadDataDestPointer = g_DataBuffer;
    g_StageDataPtr = (void*)g_StageVoiceOffsetTable[g_stageId];
    // Set pointer to current stage's 32-room BGM state block (used by update_room_bgm)
    g_RoomBgmStatePtr = &g_roomBgmState[g_stageId * 32];

    set_player_animations_functions();

    g_RdtLoadDataBackup = g_loadDataDestPointer;

    if ( (g_main_state_flags2 & 0x10000000) != 0 ){
        // 0x004099f6: Force silence for this room's BGM (0xFF = no BGM)
        g_RoomBgmStatePtr[g_roomId] = 0xFF;
    }
    room_set();
}

// ============================================================================
// room_item_event_table_reset (0x00477f80)
// Clears the room item event table (24 entries x 12 bytes) and resets the
// head pointer. Used by item_set, door_set, item_model_set commands.
// ============================================================================
void room_item_event_table_reset(void) {
    unsigned char* p = g_RoomItemEventTable;
    do {
        *p = 0;
        p += 12;
    } while (p < g_RoomItemEventTable + 288);
    g_RoomItemEventHead = g_RoomItemEventTable;
}

// ============================================================================
// room_set_visited_flag (0x00488570)
// Sets the visited flag for the current room in the room flags bitfield.
// Uses g_StageRoomFlagOffset[stageId % 5] + roomId as the bit index.
// ============================================================================
void room_set_visited_flag(unsigned char stageId, unsigned char roomId) {
    Flg_on((int)g_RoomFlags, (unsigned int)g_StageRoomFlagOffset[stageId % 5] + roomId);
}

// ============================================================================
// room_state_reset (0x00475700)
// Resets room state: clears SCD system flags word 1 and DAT_00be9830.
// Called from room_set and game_loop. Note: room_set also clears word 0.
// ============================================================================
void room_state_reset(void) {
    g_SysFlags[1] = 0;
    DAT_00be9830 = 0;
}

// ============================================================================
// lab_slides_reset (0x0042a020)
// Resets lab slides function index and state to zero.
// ============================================================================
void lab_slides_reset(void) {
    g_labSlidesFuncIndex = 0;
    g_labSlidesState = 0;
}

// ============================================================================
// room_set (0x00477720)
// Full room initialization: deletes old textures/objects, loads RDT, sets up
// enemies, cameras, collision, sound, BGM, events, and player state.
// ============================================================================
void room_set(void)
{
    Entity* pPrevEntity;
    unsigned int i;

    // 0x00477720: g_AttractModeIdleTimer = 1
    g_AttractModeIdleTimer = 1;

    printf("room_set start\n");

    // Delete secondary texture sets 0x17-0x1d
    delete_texture_set_secondary(0x1d);
    delete_texture_set_secondary(0x1c);
    delete_texture_set_secondary(0x1b);
    delete_texture_set_secondary(0x1a);
    delete_texture_set_secondary(0x19);
    delete_texture_set_secondary(0x18);
    delete_texture_set_secondary(0x17);

    // Delete main texture sets 0x30, 0x2e
    TexturePage_DeleteSet(0x30);
    TexturePage_DeleteSet(0x2e);

    printf("sprite delete end\n");

    // Delete objects (category 1)
    object_delete_00442170(1);
    Object_DeleteAll(1);

    printf("object delete end\n");

    // 0x0047780d: Clear lower nibble of main state flags and input flags
    g_main_state_flags &= 0xFFFFFFF0;
    g_main_state_flags2 &= 0xFFFFFFF0;

    // 0x0047782e: Restore g_loadDataDestPointer from backup
    g_loadDataDestPointer = g_RdtLoadDataBackup;

    // 0x00477833: Reset texture queue
    texture_queue_reset();

    // 0x00477838: Reset room item event table
    room_item_event_table_reset();

    // 0x0047783d: Zero SCD system flags (flag bank 4, both DWORDs)
    g_SysFlags[0] = 0;
    g_SysFlags[1] = 0;

    // 0x0047785b: Clear SCD event active flags (8 entries)
    for (int j = 0; j < 8; j++) {
        g_ScdEventTable[j].active = 0;
    }

    // 0x00477879: Clear bit 0x100000 of g_main_state_flags
    g_main_state_flags &= 0xffefffff;

    // 0x0047788d: Reset player entity fields
    g_playerEntity.unk_e0 = 0;
    g_playerEntity.healthStatusFlags &= 0xbf;

    // 0x004778a1: Set room visited flag
    room_set_visited_flag(g_stageId, g_roomId);

    // 0x004778b8: g_specialRoomLightState = 0xffff
    g_SpecialRoomLightState = 0xffff;

    // 0x004778bd: Reset room state counters
    room_state_reset();

    // 0x004778d2: Clear saved texture bank ID (low byte = validity flag)
    g_SavedTextureBankID &= 0xff00;

    // 0x004778d7: Reset lab slides state
    lab_slides_reset();

    printf("etc set end\n");

    // 0x00477904: Character model switching logic
    if (g_playerEntity.id != g_CharacterModelId) {
        if ((g_CharacterModelId & 8) == 0) {
            if ((g_CharacterModelId < 4) && (g_playerEntity.id < 4)) {
                g_playerEntity.id = g_CharacterModelId;
                if (g_CharacterModelId == 3) {
                    // 0x004778c6: Switch to Rebecca
                    HEALTH_STATUS_BKP = (unsigned short)g_playerEntity.healthStatusFlags;
                    HEALTH_BKP = g_playerEntity.health;
                    g_playerEntity.health = 88;
                    g_playerEntity.maxHealth = 88;
                    g_RoomItemBackup = g_EquippedItemId;
                    g_playerEntity.healthStatusFlags = 0;
                    g_EquippedItemId = 0;
                    g_ItemSlotsPointer = g_RebeccaItemSlots;
                } else {
                    // 0x00477914: Restore from backup
                    g_playerEntity.health = HEALTH_BKP;
                    g_playerEntity.healthStatusFlags = (unsigned char)HEALTH_STATUS_BKP;
                    g_EquippedItemId = g_RoomItemBackup;
                    g_playerEntity.maxHealth = 140;
                    g_ItemSlotsPointer = g_ItemsSlots;
                    g_RoomItemBackup = 0;
                }
            }
        } else {
            g_main_state_flags2 |= 0x4000000;
            g_CharacterModelId &= 7;
        }

        // 0x00477950: Common character setup (reached when bit3 set or ids < 4)
        if ((g_CharacterModelId & 8) != 0 || (g_CharacterModelId < 4 && g_playerEntity.id <= 3)) {
            LoadHeldItemsImages();
            g_playerEntity.animationId = 0;
            g_playerEntity.animFrameId = 0;
            g_playerEntity.action_behavior = 0;
            g_playerEntity.action_state = 0;
            SetupCharacterData();
            LoadSoundBank(g_playerEntity.equippedWeaponId, g_loadDataDestPointer);
        }

        // 0x00477982: Update player id and load character SFX
        g_playerEntity.id = g_CharacterModelId;
        load_character_sfx(g_CharacterModelId);
    }

    // 0x0047799c:
    printf("player set end\n");

    // 0x004779b4: object_delete_00442170(2)
    object_delete_00442170(2);

    // 0x004779c7: LoadRoomRdt()
    LoadRoomRdt();

    // 0x004779d6: object_delete_00442170(3)
    object_delete_00442170(3);

    // 0x004779f3: Set room event flags for room 0x13 camera 4
    if ((g_roomCameraId == 4) && (g_roomId == 0x13)) {
        Flg_on((int)&g_RoomEventFlags, 0x52);
        Flg_on((int)&g_RoomEventFlags, 0x53);
        Flg_on((int)&g_RoomEventFlags, 0x54);
    }

    printf("after of Room load\n");

    // 0x00477a37: Setup collision boundaries and 3D sound callbacks
    Room_SetupCollisionCallbacks();

    // 0x00477a45: object_delete_00442170(4)
    object_delete_00442170(4);

    // 0x00477a58: Load per-room enemy sound banks
    Room_LoadEnemySoundBanks();

    // 0x00477a66: object_delete_00442170(5)
    object_delete_00442170(5);

    printf("after of Sound rom set\n");

    // 0x00477a92: InitRoomEffSprite()
    InitRoomEffSprite();

    printf("after of Init eff sprite\n");

    // 0x00477ac5: Sound bank loading loop
    i = 0;
    object_delete_00442170(6);

    g_loadDataDestPointer = g_RdtPointer->vab_sound_file;
    if (g_RdtPointer->sound_banks_count != 0) {
        do {
            g_itemboxes_covers_table[i] = g_loadDataDestPointer;
            *(unsigned char*)g_loadDataDestPointer = 0;
            g_loadDataDestPointer = (char*)g_loadDataDestPointer + 0xa4;
            i++;
        } while (i < g_RdtPointer->sound_banks_count);
    }

    // Desk/unknown_03 processing
    i = 0;
    if (g_RdtPointer->unknown_03[0] != 0) {
        do {
            g_desks_pointers_table[i] = g_loadDataDestPointer;
            *(unsigned char*)g_loadDataDestPointer = 0;
            g_loadDataDestPointer = (char*)g_loadDataDestPointer + 0xa4;
            i++;
        } while (i < g_RdtPointer->unknown_03[0]);
    }

    // 0x00477bc8: Reset enemy count and model state
    i = 0;
    g_enemy_count = 0;
    g_omodelCount = 0;
    g_LastEnemyModelId = 0xff;
    g_TextureBankID = 0x06;
    g_TextureDepthByte = 0x0a;
    g_ItemModelCount = 0;

    object_delete_00442170(7);

    printf("after of model\n");

    // 0x00477c0f: run_command_functions(g_RoomInitScd)
    run_command_functions((unsigned short*)g_RoomInitScd);

    // 0x00477c21: g_message_flags |= 0x80
    g_message_flags |= 0x80;

    printf("after of Scenario check\n");

    // 0x00477c45: room_events_check()
    room_events_check();

    // 0x00477c54: g_message_flags &= ~0x80
    g_message_flags &= ~0x80;

    object_delete_00442170(8);

    printf("after of Event\n");

    object_delete_00442170(9);

    // 0x00477c84: Set player joint movement data from RDT
    g_playerEntity.jointMoveData2 = (unsigned int)g_RdtPointer->unknown_6c;
    g_playerEntity.jointMoveData3 = (unsigned int)g_RdtPointer->unknown_70;

    // 0x00477ca0: Reset enemy model cache and set up entity loop
    g_LastEnemyModelId = 0xff;

    // 0x00477cc3: Enemy entity loading loop
    ENTITY = g_EnemiesList;
    pPrevEntity = NULL;
    if (g_enemy_count != 0) {
        do {
            if ((ENTITY->status_flags & 1) != 0) {
                if (g_LastEnemyModelId == ENTITY->id) {
                    // 0x00477bf7: Reuse previous enemy model data
                    ENTITY->animHeader = pPrevEntity->animHeader;
                    ENTITY->animBase = pPrevEntity->animBase;
                    ENTITY->modelLoadBuffer = pPrevEntity->modelLoadBuffer - 0xc;
                } else {
                    // 0x00477bdc: Load new enemy model
                    g_LastEnemyModelId = ENTITY->id;
                    LoadEntityEMD(ENTITY, ENTITY->id + 4);
                }
                Entity_SetJoints(ENTITY, 0x7c);
                InitAnimStructure((void*)ENTITY->modelLoadBuffer);
                g_loadDataDestPointer = (void*)SetupJointStructures((unsigned int)g_loadDataDestPointer);
                pPrevEntity = ENTITY;
                if ((g_main_state_flags & 1) != 0) {
                    SetupEntityJointAnimation();
                }
                i++;
            }
            ENTITY++;
        } while (i < (unsigned int)g_enemy_count);
    }

    // 0x00477c89: Setup texture bank data
    SetupTextureBankData((short)g_TextureDepthByte);
    // 0x00477ca0: Save current texture bank ID for cutscene restoration
    g_SavedTextureBankID = *(unsigned short*)&g_TextureBankID;
    g_CurrentRdtDataTypePtr = g_RdtPointer->cam_switch_zones;

    object_delete_00442170(10);

    printf("after of model set\n");

    // 0x00477e7c: Special case for stage 4, room 4
    if ((g_stageId == 4) && (g_roomId == 4)) {
        load_slides_images();
    }

    // 0x00477e9f: load_room_bg()
    load_room_bg();

    printf("after of Bg room set\n");

    object_delete_00442170(0xb);

    // 0x00477ec8: load_room_bg_masks()
    load_room_bg_masks();

    object_delete_00442170(0xc);

    // 0x00477ef6: Camera setup
    if ((g_main_state_flags & 0x100000) == 0) {
        g_roomCameraId = 0;
        check_camera_switch(1);
    } else {
        display_room_camera_bg();
    }

    // 0x00477f26: Special case for stage 2, room 3
    if ((g_stageId == 2) && (g_roomId == 3)) {
        DAT_00d213c0 = g_loadDataDestPointer;
    }

    object_delete_00442170(0xd);

    g_AttractModeIdleTimer = 0;

    printf("room_set end\n");
}
