// GameStart.cpp - Gameplay session bootstrap
// game_start, InitializeGame and all player/inventory initialization.
// All functions decompiled from Ghidra with original addresses
#include "../Globals.h"
#include "../marni/MarniSystem.h"
#include "FileLoader.h"
#include "SpriteRenderer.h"
#include <cstdio>
#include <cstring>
#include "../system/AssetPath.h"

extern void setSomeColor(int r, int g, int b);              // 0x00470a50
extern void empty_40ae40(int);                              // 0x0040ae40 RoomInit.cpp
extern void ScheduleInputFlush(void);                       // 0x00497e80 InputSystem.cpp
extern unsigned int Flg_ck(int baseAddr, unsigned int bitIndex);   // 0x00473f40 SaveLoadScreen.cpp
extern void Flg_on(int baseAddr, unsigned int bitIndex);    // 0x00473ef0 CmdFunctions.cpp
extern void title_state(void);                              // TitleScreen.cpp
extern void logos_state(void);                              // LogosScreen.cpp

// ending_state (0x00410820) lives in EndingScreen.cpp.
extern void ending_state(void);

// ---------------------------------------------------------------------------
// memclr (0x00475720)
// Zeros memory from start up to (but not including) end. Operates on DWORDs.
// ---------------------------------------------------------------------------
void memclr(void* start, void* end)
{
    unsigned int* p = (unsigned int*)start;
    unsigned int* e = (unsigned int*)end;
    while (p < e) {
        *p = 0;
        p++;
    }
}

// ---------------------------------------------------------------------------
// SetInitialItems (0x004513f0)
// Sets up the initial inventory based on selected character.
// ---------------------------------------------------------------------------
void SetInitialItems(void)
{
    unsigned char slot_index;
    unsigned char total_items_slots;
    unsigned char* item_slot;
    unsigned char item_qty;

    unsigned char initial_items[] = {
        // chris items
        ITEM_KNIFE,             0,
        ITEM_FIRST_AID_SPRAY,   1,
        ITEM_NONE,              0,
        ITEM_NONE,              0,
        // jill items
        ITEM_KNIFE,             0,
        ITEM_BERETTA,          15,
        ITEM_FIRST_AID_SPRAY,   1,
        ITEM_NONE,              0
    };

    // init room items flags (bit set = item not taken)
    {
        static const unsigned char roomItemsFlagsInit[32] = {
            0xff, 0xff, 0xff, 0xbf,
            0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xf7, 0xff,
            0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff
        };
        memcpy(g_roomItemsFlags, roomItemsFlagsInit, 32);
    }

    g_initUnusedA = 7;
    g_initUnusedB = 0xf0;
    g_initUnusedC = 0xf0;

    if ((g_playerEntity.id & 3) == CHAR_CHRIS) {
        // Chris: 6 slots, Rebecca gets Baretta with 15 bullets
        item_slot = initial_items;           // Chris items at offset 0
        total_items_slots = 6;
        g_RebeccaItemSlots[0].Id = ITEM_BERETTA;
        g_RebeccaItemSlots[0].qty = 15;
    } else {
        // Jill: 8 slots
        item_slot = initial_items + 8;       // Jill items at offset 8
        total_items_slots = 8;
    }

    // Copy items into inventory slots
    item_qty = *item_slot;
    slot_index = 0;
    while (item_qty != 0) {
        g_ItemsSlots[slot_index].Id = *item_slot;
        item_qty = item_slot[1];
        g_ItemSlotIndices[slot_index] = slot_index;
        g_ItemsSlots[slot_index].qty = item_qty;
        item_qty = item_slot[2];
        item_slot = item_slot + 2;
        slot_index = slot_index + 1;
    }
    g_ItemSlotsBitmask = (1 << (slot_index & 0x1f)) - 1;
    g_TotalHeldItems = slot_index;

    // Clear remaining slots
    for (; slot_index < total_items_slots; slot_index++) {
        g_ItemsSlots[slot_index].Id = 0;
        g_ItemsSlots[slot_index].qty = 0;
    }
}

// ---------------------------------------------------------------------------
// CountHeldItems (0x00451600)
// Counts non-empty item slots in the current character's inventory.
// Max slots: 8 for Jill (characterId & 3 == 1), 6 for Chris/Rebecca.
// ---------------------------------------------------------------------------
void CountHeldItems(void) // 0x00451600
{
    g_TotalHeldItems = 0;
    unsigned char itemSlot = *(unsigned char*)g_ItemSlotsPointer;
    while (itemSlot != 0 &&
           g_TotalHeldItems < (unsigned char)((4 - ((g_playerEntity.id & 3) != 1)) * 2)) {
        g_TotalHeldItems = g_TotalHeldItems + 1;
        itemSlot = ((unsigned char*)g_ItemSlotsPointer)[(unsigned int)g_TotalHeldItems * 2];
    }
}

// ---------------------------------------------------------------------------
// LoadHeldItemsImages (0x00451640)
// Loads inventory item images into the image buffer for HUD display.
// Counts held items, sets up slot bitmask and indices, then loads each
// item's image sprite via LoadItemImage using the item image lookup table.
// After loading, composites all items into a single D3D11 SRV at the
// texture slot the renderer expects.
// ---------------------------------------------------------------------------
void LoadHeldItemsImages(void) // 0x00451640
{
    unsigned char totalItems;
    unsigned int index;
    void* savedSlotPointer;

    CountHeldItems();
    g_ItemSlotsBitmask = (1 << (g_TotalHeldItems & 0x1f)) - 1;
    totalItems = g_TotalHeldItems;
    savedSlotPointer = g_ItemSlotsPointer;

    while (totalItems != 0) {
        totalItems = totalItems - 1;
        index = (unsigned int)totalItems;
        g_ItemSlotsPointer = savedSlotPointer;
        g_ItemSlotIndices[index] = totalItems;
        unsigned char itemId = ((unsigned char*)savedSlotPointer)[index * 2];
        unsigned char imageType = g_ItemImageLookupTable[itemId * 4];
        LoadItemImage(imageType - 1, (int)index, (int)g_ItemsImageBuffer);
        savedSlotPointer = g_ItemSlotsPointer;
    }
    g_ItemSlotsPointer = savedSlotPointer;
}

// ---------------------------------------------------------------------------
// get_item_slot (0x004516a0)
// Linear search of the player's inventory for itemId. On a hit, points
// g_pCurrentItemSlot (0x00d226f0) at the matched 2-byte slot and returns its
// index; on a miss, points it at g_defaultItemSlot and returns -1.
// (The address previously commented here, 0x0047ee20, is inside LoadSoundBank.)
// ---------------------------------------------------------------------------
int get_item_slot(unsigned char itemId)
{
    unsigned char* slot = (unsigned char*)g_ItemSlotsPointer;
    if (g_TotalHeldItems != 0) {
        unsigned int i = 0;
        do {
            if (*slot == itemId) {
                g_pCurrentItemSlot = (unsigned char*)g_ItemSlotsPointer + i * 2;
                return (int)i;
            }
            i++;
            slot += 2;
        } while (i < (unsigned int)g_TotalHeldItems);
    }
    g_pCurrentItemSlot = &g_defaultItemSlot;
    return -1;
}

// ---------------------------------------------------------------------------
// InitPlayerData (0x00481880)
// Initializes starting position, angle, and calls SetInitialItems.
// Starting position: (17000, 5000), angle: 3072 (about 270 degrees)
// ---------------------------------------------------------------------------
void InitPlayerData(void)
{
    SetInitialItems();
    g_playerEntity.position.x = 17000;
    g_main_state_flags2 = g_main_state_flags2 | MSF2_PLAYER_INITIALISED;
    g_playerEntity.position.z = 5000;
    g_playerEntity.directionAngle = 3072;
}

// ---------------------------------------------------------------------------
// InitPlayerEntity (0x004950b0)
// Zeroes out all entity state fields: flags, animation, position, etc.
// Called at the start of SetupCharacterData.
// ---------------------------------------------------------------------------
void InitPlayerEntity(void)
{
    g_playerEntity.unk_bc = 0;
    g_playerEntity.attackAnim = 0;
    g_playerEntity.animation_frame_id = 0;
    g_playerEntity.unk_bf = 0;
    g_playerEntity.unk_c0 = 0;
    g_playerEntity.flags = 1;
    g_playerEntity.move_speed_current = 0;
    g_playerEntity.zoneFlags = 1;
    g_playerEntity.speed.y = 0;
    g_playerEntity.animationId = 0;
    g_playerEntity.animFrameId = 0;
    g_playerEntity.action_behavior = 0;
    g_playerEntity.action_state = 0;
    g_playerEntity.speed.z = 0;
    g_playerEntity.unk_c1 = 0;
    g_playerEntity.speed.pad = 0;
    g_playerEntity.isBeingAttackedFlag = 0;
    g_playerEntity.position.pad = 0;
    g_playerEntity.unk_10 = 0;
    g_playerEntity.unk_11 = 99;
    g_playerEntity.unk_12 = 0xBE;
    g_playerEntity.unk_13 = 0;
    g_playerEntity.speed.x = 0;
    g_playerEntity.scaMatrixData.localMatrix.m[0][0] = 0x1000;
    g_playerEntity.scaMatrixData.localMatrix.m[0][1] = 0;
    g_playerEntity.scaMatrixData.localMatrix.m[0][2] = 0;
    g_playerEntity.scaMatrixData.localMatrix.m[1][0] = 0;
    g_playerEntity.scaMatrixData.localMatrix.m[1][1] = 0x1000;
    g_playerEntity.scaMatrixData.localMatrix.m[1][2] = 0;
    g_playerEntity.lookAtFlags = 0;   // disable head/aim tracking
    g_playerEntity.scaMatrixData.localMatrix.m[2][0] = 0;
    g_playerEntity.scaMatrixData.localMatrix.m[2][1] = 0;
    g_playerEntity.scaMatrixData.localMatrix.m[2][2] = 0x1000;
    g_playerEntity.unk_ca = 0;
}

// ===========================================================================
// InitializeGame (0x004807a0)
// Main game initialization. Loads bio_card.dat, sets up player entity, health,
// inventory, character data, room SFX, character SFX, and initializes the
// starting room.
// ===========================================================================
void InitializeGame(void)
{
    int has_alternate_outfit;

    g_AttractModeIdleTimer = 1;
    ScheduleInputFlush();
    // vram_clr(0, 0, 320, 480): PS1 leftover, returns immediately in this
    // build (0x00412370) - call dropped

    g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;

    Task_sleep(1);
    g_bGameActive = 2;

    g_main_state_flags = g_main_state_flags & MSF_GAMESTART_KEEP_MASK;
    g_main_state_flags = g_main_state_flags | MSF_ROOM_TRANSITION;

    // 0x00412380: empty in the original - call dropped

    memclr(&g_defaultItemSlot, g_BioCardData);

    g_loadDataDestPointer = g_DataBuffer;
    g_SpecialRoomLightDelta = 0;
    g_fading_counter = 0;

    Task_execute(1, (void*)display_game_loading_message);

    LoadFile(GAME_DATA_ROOT "data\\bio_card.dat", g_loadDataDestPointer, 32);

    if ((g_main_state_flags & MSF_CONTINUE_GAME) == 0) {
        g_gameSessionInitFlag = 0;
        Game_timer = 0;

        g_playerEntity.id = g_SelectedCharactedId;
        g_playerEntity.healthStatusFlags = 0x10;

        memcpy(&g_BioCardData[0], g_loadDataDestPointer, 1052);

        g_SpecialRoomLightState = (short)0xFFFF;
        g_CharacterModelId = g_playerEntity.id;

        if ((g_main_state_flags2 & MSF2_ATTRACT_DEMO) == 0) {
            InitPlayerData();
            /*
            * chris: 140hp
            * jill: 96hp
            */
            g_playerEntity.health = (short)((g_playerEntity.id & 1) * -44 + 140);
            g_PlayerHealthCopy = g_playerEntity.health;
        } else {
            LoadAttractModePlayerData();
        }
    } else {
        memcpy(&g_BioCardData[0], g_loadDataDestPointer, 0x200);
        empty_0047eb90((int)((~g_controllerConfig) >> 7));

        g_playerEntity.position.x = g_PlayerPosXCopy;
        g_playerEntity.position.z = g_PlayerPosZCopy;
        g_playerEntity.healthStatusFlags = g_PlayerHealthStatusCopy;
        g_playerEntity.directionAngle = g_PlayerDirAngleCopy;
        g_playerEntity.health = g_PlayerHealthCopy;
        g_playerEntity.id = g_SelectedCharactedId;
        g_CharacterModelId = g_SelectedCharactedId;

        /*  check alternative outfit flag */
        has_alternate_outfit = Flg_ck((int)g_ScenarioFlags, SCENARIO_FLAG_ALTERNATE_OUTFIT);
        if (has_alternate_outfit != 0) {
            g_CharacterModelId = g_CharacterModelId + 8;
        }

        if (g_SavesCounter == 0) {
            Game_timer = 0;
        }
        g_SavesCounter = g_SavesCounter + 1;
    }

    g_deadMoveValue = (DWORD)&g_identityMatrixData;
    g_RoomCameraDataCopy = (DWORD)&g_RoomCameraData;
    g_lightMatrixPtr = (DWORD)&g_lightMatrix;

    // 0x004809c5: g_ItemSlotsPointer = g_ItemsSlots
    g_ItemSlotsPointer = g_ItemsSlots;
    g_usedItemId = 0;
    g_pickedItemId = 0;
    DAT_00be41e1 = 0;
    g_defaultItemSlot = 0;
    DAT_00be9614 = 0;

    LoadHeldItemsImages();

    // 0x00480a0d: DAT_00d91bc0 = &g_RoomItemEventTable. Redundant in practice —
    // room_set -> room_item_event_table_reset sets the same head pointer — but
    // present in the original.
    g_RoomItemEventHead = g_RoomItemEventTable;

    g_playerEntity.pSca_hit_data = (DWORD)g_entityDataBlock;

    g_playerEntity.maxHealth = (unsigned char)((g_playerEntity.id & 1) * -44 + 140);

    g_playerEntity.Sca_info = (unsigned int)g_scaDataTable;

    g_scaPoolPtr = (DWORD)g_entityDataBlock + 6;
    g_scaPoolBase = (DWORD)g_entityDataBlock + 6;

    Task_sleep(1);

    SetupCharacterData();

    g_loadDataDestPointer = g_shootDirEspBuffer;
    load_shoot_direction_data();

    g_SndFadeType = 0;
    g_loadDataDestPointer = g_DataBuffer;
    g_BGM_STATE = 0xFF;

    load_room_sfx(0);
    load_character_sfx(g_playerEntity.id & 1);

    LoadSoundBank(g_playerEntity.equippedWeaponId, g_DataBuffer);

    g_main_state_flags = g_main_state_flags & ~MSF_ROOM_TRANSITION;

    init_room();

    g_AttractModeIdleTimer = 1;
    update_room_bgm();

    if ((g_playerEntity.id & 3) == CHAR_JILL) {
        // Second playthrough marker (0x7B, set by EndingScreen after clearing).
        // On a FIRST Jill playthrough, arm the first-run-only room item flag
        int is_second_playthrough = Flg_ck((int)g_ScenarioFlags, SCENARIO_FLAG_SECOND_PLAYTHROUGH);
        if (is_second_playthrough == 0) {
            Flg_on((int)g_roomItemsFlags, 0x34); // disable ink-ribbon from main hall
            Flg_on((int)g_ScenarioFlags2, SCENARIO2_FLAG_JILL_FIRST_RUN);
        }
    }

    g_AttractModeIdleTimer = 0;
    printf("end of game init\n");
}

// ============================================================================
// game_start (0x00480710)
// Entry point for gameplay. Initializes game, runs the main game loop,
// then chains to the appropriate next state based on how the game ended.
//
// State transitions:
//   end_game_status == 1 → title_state (player died or quit)
//   end_game_status == 0 → ending_state (game completed)
//   otherwise            → logos_state  (fallback)
// ============================================================================
void game_start(void)
{
    int end_game_status;
    g_playingGameFlag = 1;

    g_message_flags = g_message_flags & 0xfdff;

    InitializeGame();

    end_game_status = game_loop();

    g_main_state_flags = 0;

    if (end_game_status == 1) {
        g_main_state_flags2 = g_main_state_flags2 & MSF2_RESET_KEEP_MASK;
        Task_chain((void*)title_state);
    }

    g_main_state_flags2 = g_main_state_flags2 & MSF2_RESET_KEEP_MASK;

    if (end_game_status == 0) {
        g_gameTimerSnapshot = Game_timer;
        Task_chain((void*)ending_state);
    }

    Task_chain((void*)logos_state);
}

// ============================================================================
// Stub implementations for functions not yet decompiled
// ============================================================================

// 0x00443000 - LoadItemImage
// Wrapper around LoadImage for inventory item sprites.
// Loads a 20x30 16-bit item image from the buffer into a texture page slot.
void LoadItemImage(int item_id, int image_index, int img_buffer) // 0x00443000
{
    LoadImage(item_id * 1200 + img_buffer, 0, image_index + 1, 1, 108, (short)image_index << 5, 20, 30, 2);
}

// (0x00481750) - Load attract mode (demo) player save data
// Cycles through ./usa/data/pdemo0.dat..pdemo3.dat. The original loads the
// WHOLE 0x994-byte file at 0x00d21ce0, which overlaps three state areas:
//   +0x000 header          -> demo state block (g_AttractDemoData)
//   +0x030 input words     -> g_demoPadData (0x00d21d10)
//   +0x990 tail (4 bytes)  -> g_AttractMode_ControllerConfig / _PlayerHealth
void LoadAttractModePlayerData(void)
{
    // 0x00481753: wrap the demo index around after pdemo3.dat
    if (g_CurrentAttractModeId > 3) {
        g_CurrentAttractModeId = 0;
    }

    // 0x00481768-0x00481788: build the path from the "./usa/data/pdemo0.dat"
    // template (0x004d22e8), patching the digit at offset 16 with the index
    sprintf(FILE_PATH, GAME_DATA_ROOT "data\\pdemo%d.dat", g_CurrentAttractModeId);

    // 0x0048178d: LoadFile copies the entire file; stage it here and then
    // scatter the pieces onto the globals that overlap the original block.
    static BYTE pdemoFile[0x994];
    LoadFile(FILE_PATH, pdemoFile, 0x20);
    memcpy(&g_AttractDemoData, pdemoFile, sizeof(g_AttractDemoData));
    memcpy(g_demoPadData, pdemoFile + 0x30, sizeof(g_demoPadData));
    g_AttractMode_ControllerConfig = *(WORD*)(pdemoFile + 0x990);
    g_AttractMode_PlayerHealth     = *(short*)(pdemoFile + 0x992);

    // 0x00481792: advance to the next demo for the following cycle
    g_CurrentAttractModeId = g_CurrentAttractModeId + 1;

    // 0x0048179c: back up the current controller config (restored by
    // StartAttractDemo when the demo ends)
    g_AttractMode_ControllerConfig = (WORD)g_controllerConfig;

    // 0x004817ab-0x004817b7: switch to the recorded character
    g_playerEntity.id = g_AttractDemoData.characterId;
    g_SelectedCharactedId = g_AttractDemoData.characterId;
    g_controllerConfig = g_controllerConfig & 0xfc;
    g_CharacterModelId = g_AttractDemoData.characterId;
    if (g_AttractDemoData.characterId != 0) {
        g_main_state_flags = g_main_state_flags | MSF_CHAR_VARIANT;
    }

    // 0x004817d8-0x00481816: apply the recorded room / camera / items
    g_stageId = g_AttractDemoData.stageId;
    g_roomId = g_AttractDemoData.roomId;
    g_AttractMode_RoomCameraId = g_AttractDemoData.cameraId;
    g_EquippedItemId = g_AttractDemoData.equippedItemId;
    g_TotalHeldItems = g_AttractDemoData.totalHeldItems;
    memcpy(g_ItemsSlots, &g_AttractDemoData.itemsSlots, 24);

    // 0x00481832: restart demo playback input from frame 1
    g_DemoTimerCur = 1;

    // 0x00481821-0x00481863: restore the recorded player position / health
    g_playerEntity.position.x = g_AttractDemoData.playerPosX;
    g_playerEntity.position.z = g_AttractDemoData.playerPosZ;
    g_playerEntity.directionAngle = g_AttractDemoData.playerDirAngle;
    g_playerEntity.health = g_AttractMode_PlayerHealth;

    // 0x00481870: reload the sound bank for the recorded weapon
    LoadSoundBank(g_playerEntity.equippedWeaponId, g_DataBuffer);
}

// (0x0047eb90) - Restore game state from bio card on load
void empty_0047eb90(int param) { }
