// SaveLoadScreen.cpp - Save/Load game state screen
// Decompiled from Ghidra at 0x00493310
// Dependencies: FileWrite(0x004122a0), ReadSaveFile(0x004120c0),
//               EnsureDirectoryExists(0x0040b700), GetSaveLocationIndex(0x00494000),
//               DrawSaveCursor(0x00493fa0), InitInputKeyBindings(0x00497c20),
//               Flg_ck(0x00473f40), use_room_action_item(0x004631f0),
//               rearrange_item_slots(0x00451510), cut_set(0x004628c0),
//               StMask(0x00497690), Task_sleep(0x004201e0), Task_chain(0x00420210)
#include "../Globals.h"
#include "FileLoader.h"
#include "SpriteRenderer.h"
#include "SFXIds.h"
#include "PrintText.h"
#include <cstdio>
#include <cstring>
#include "../system/AssetPath.h"

extern void logos_state(void);
extern void title_state(void);
extern void game_start(void);

// ============================================================================
// Save slot info — matches the original's 20-byte stack entries.
// Field order is load order: charId(+0), count(+4), stage(+8), room(+0xc),
// hasData(+0x10). The byte offsets are into the save file (the file's first
// 0x800 bytes are the g_BioCard block, 1:1 with memory).
// ============================================================================
typedef struct SaveSlotInfo
{
    uint32_t characterId;   // +0x00  file[0x22B]  (g_BioCard.selectedCharactedId)
    uint32_t savesCount;    // +0x04  file[0x228]  (g_BioCard.savesCounter)
    uint32_t stageId;       // +0x08  file[0x200]  (g_BioCard.stageId)
    uint32_t roomId;        // +0x0C  file[0x201]  (g_BioCard.roomId)
    uint32_t hasData;       // +0x10  file exists flag
} SaveSlotInfo;

typedef enum {
    STATE_INIT = 0,
    STATE_IDLE = 1,
    STATE_INPUT_DELAY = 2,
    STATE_SAVE_SLOT_SELECTED = 3,   // save menu: slot chosen
    STATE_LOAD_SLOT_SELECTED = 4,   // load menu: slot chosen
    STATE_CONFIRM_OVERWRITE = 5,    // confirm overwrite dialog
    STATE_PERFORM_SAVE = 6,         // write save file
    STATE_SAVE_ANIM_STEP1 = 7,
    STATE_SAVE_ANIM_STEP2 = 8,
    STATE_ERROR_MSG = 9             // "NOT ENOUGH FREE SPACE"
} MenuState;

// Save file constants (the file is 0xA82 = 2690 bytes)
#define SAVE_SLOT_COUNT      8
#define SAVE_FILE_SIZE       0xA82
// The original's 0x800 "block" is one contiguous region (0xbe9620..0xbe9e20)
// holding g_BioCard plus the input-config globals that follow it. The port
// models those as separate globals, so the copies below are sized per global
// instead of one 0x800 memcpy (g_BioCard is only sizeof(BioCardLayout)).
#define SAVE_BLOCK_SIZE      0x800   // original block region size (for gating)
#define OFFSET_STAGE_ID      0x200
#define OFFSET_ROOM_ID       0x201
#define OFFSET_SAVES_COUNT   0x228
#define OFFSET_CHARACTER_ID  0x22B
#define OFFSET_PAD_REMAP     0x41C   // 32 bytes  (g_padRemapSubTable3)
#define OFFSET_CONTROLLER_CFG 0x43C  // 1 byte   (g_controllerConfig)
#define OFFSET_KEY_BINDINGS  0x800   // 32 bytes  (g_keyBindingData)
#define OFFSET_JOY_REMAP     0x820   // 256 bytes (g_JoyRemapTbl)
#define OFFSET_JOY_BACKUP    0x8A0   // 128 bytes (g_joyRemapBackupJoy)
#define OFFSET_ROOM_BGM      0x920   // 224 bytes (g_roomBgmState)
#define OFFSET_SIDEWINDER    0xA00   // 1 byte
#define OFFSET_LANG_BYTE     0xA01   // 1 byte  (DAT_004d6444)
#define OFFSET_KEY_BACKUP    0xA02   // 128 bytes (g_joyRemapBackupKey)

// ============================================================================
// PrintFormattedText encoded data — byte-identical to the original tables.
// Decode with: python tools/decode_re1.py <address>
// Encoding: 0xFB = no-op spacer, 0x00 = space, 0x01 = end, 'A' = 0x1D,
//           '0' = 0x0C, '\\' = 0x38, '-' = 0x3B.
// The name entries are 11 bytes ("CHRIS" with a 0xFB after every glyph) and
// the location entries are 40 bytes (text + 0x00 space padding) because the
// save animation copies fixed 10/39-byte slices straight out of them.
// ============================================================================

// --- Character names (indexed by characterId & 3) ---

static const unsigned char s_pftChrisName[16] = {   // DAT_004d40f8
    0x1F, 0xFB, 0x24, 0xFB, 0x2E, 0xFB, 0x25, 0xFB, 0x2F, 0xFB, 0x01
};
static const unsigned char s_pftJillName[16] = {    // DAT_004d4108
    0x26, 0xFB, 0x25, 0xFB, 0x28, 0xFB, 0x28, 0xFB, 0x00, 0xFB, 0x01
};
static const unsigned char* s_pftCharNameTable[] = {
    s_pftChrisName, s_pftJillName
};

// --- Location names (indexed by GetSaveLocationIndex) ---

static const unsigned char s_pftLocRoom1F[40] = {     // DAT_004d41a0 " M.Room 1F"
    0x00, 0xFB, 0x00, 0xFB, 0x29, 0xFB, 0x79, 0xFB, 0x2E, 0xFB, 0x4B, 0xFB,
    0x4B, 0xFB, 0x49, 0xFB, 0x00, 0xFB, 0x0D, 0xFB, 0x22, 0xFB, 0x00, 0xFB,
    0x00, 0xFB, 0x00, 0xFB, 0x00, 0xFB, 0x00, 0xFB, 0x00, 0xFB, 0x00, 0xFB,
    0x00, 0xFB, 0x01, 0x00
};
static const unsigned char s_pftLocHall1F[40] = {     // DAT_004d41c8 " M.Hall 1F"
    0x00, 0xFB, 0x00, 0xFB, 0x29, 0xFB, 0x79, 0xFB, 0x24, 0xFB, 0x3D, 0xFB,
    0x48, 0xFB, 0x48, 0xFB, 0x00, 0xFB, 0x0D, 0xFB, 0x22, 0xFB, 0x00, 0xFB,
    0x00, 0xFB, 0x00, 0xFB, 0x00, 0xFB, 0x00, 0xFB, 0x00, 0xFB, 0x00, 0xFB,
    0x00, 0xFB, 0x01, 0x00
};
static const unsigned char s_pftLocCourtyard[40] = {  // DAT_004d41f0 " Courtyard Room B1"
    0x00, 0xFB, 0x00, 0xFB, 0x1F, 0xFB, 0x4B, 0xFB, 0x51, 0xFB, 0x4E, 0xFB,
    0x50, 0xFB, 0x55, 0xFB, 0x3D, 0xFB, 0x4E, 0xFB, 0x40, 0xFB, 0x00, 0xFB,
    0x2E, 0xFB, 0x4B, 0xFB, 0x4B, 0xFB, 0x49, 0xFB, 0x00, 0xFB, 0x1E, 0xFB,
    0x0D, 0xFB, 0x01, 0x00
};
static const unsigned char s_pftLocGuardhouse[40] = { // DAT_004d4218 " Guardhouse 1F"
    0x00, 0xFB, 0x00, 0xFB, 0x23, 0xFB, 0x51, 0xFB, 0x3D, 0xFB, 0x4E, 0xFB,
    0x40, 0xFB, 0x44, 0xFB, 0x4B, 0xFB, 0x51, 0xFB, 0x4F, 0xFB, 0x41, 0xFB,
    0x00, 0xFB, 0x0D, 0xFB, 0x22, 0xFB, 0x00, 0xFB, 0x00, 0xFB, 0x00, 0xFB,
    0x00, 0xFB, 0x01, 0x00
};
static const unsigned char s_pftLocLaboratory[40] = { // DAT_004d4240 " Laboratory B3"
    0x00, 0xFB, 0x00, 0xFB, 0x28, 0xFB, 0x3D, 0xFB, 0x3E, 0xFB, 0x4B, 0xFB,
    0x4E, 0xFB, 0x3D, 0xFB, 0x50, 0xFB, 0x4B, 0xFB, 0x4E, 0xFB, 0x55, 0xFB,
    0x00, 0xFB, 0x1E, 0xFB, 0x0F, 0xFB, 0x00, 0xFB, 0x00, 0xFB, 0x00, 0xFB,
    0x00, 0xFB, 0x01, 0x00
};
static const unsigned char s_pftLocStoreroom[40] = {  // DAT_004d4268 " M.Storeroom 1F"
    0x00, 0xFB, 0x00, 0xFB, 0x29, 0xFB, 0x79, 0xFB, 0x2F, 0xFB, 0x50, 0xFB,
    0x4B, 0xFB, 0x4E, 0xFB, 0x41, 0xFB, 0x4E, 0xFB, 0x4B, 0xFB, 0x4B, 0xFB,
    0x49, 0xFB, 0x00, 0xFB, 0x0D, 0x22, 0xFB, 0xFB, 0x00, 0xFB, 0x00, 0xFB,
    0x00, 0xFB, 0x01, 0x00
};
static const unsigned char s_pftLocCourtyard2[40] = { // DAT_004d4290 " Courtyard Path B1"
    0x00, 0xFB, 0x00, 0xFB, 0x1F, 0xFB, 0x4B, 0xFB, 0x51, 0xFB, 0x4E, 0xFB,
    0x50, 0xFB, 0x55, 0xFB, 0x3D, 0xFB, 0x4E, 0xFB, 0x40, 0xFB, 0x00, 0xFB,
    0x2C, 0xFB, 0x3D, 0xFB, 0x50, 0xFB, 0x44, 0xFB, 0x00, 0xFB, 0x1E, 0xFB,
    0x0D, 0xFB, 0x01, 0x00
};
// Pointer table at 0x004d42b8
static const unsigned char* s_pftLocNameTable[] = {
    s_pftLocRoom1F, s_pftLocHall1F, s_pftLocCourtyard,
    s_pftLocGuardhouse, s_pftLocLaboratory, s_pftLocStoreroom,
    s_pftLocCourtyard2
};

// --- Slot templates ---

// "     \  \" (date separators)
static constexpr auto s_pftFilledSlot = STR("     \\  \\");           // DAT_004d4058
// "-----\--\  -------------------" (empty slot dashes)
static constexpr auto s_pftEmptySlot  = STR("-----\\--\\  -------------------"); // DAT_004d4070

// --- Mode title strings (drawn as two overlapping parts per original) ---
// Bottom: "DO NOT" + "      SAVE"/"      LOAD" → "DO NOT SAVE"/"DO NOT LOAD"
// Header: "SAVE"/"LOAD" + "     GAME" → "SAVE GAME"/"LOAD GAME"

static constexpr auto s_pftDoNot     = STR("DO NOT");           // DAT_004d40b8
static constexpr auto s_pftExitSave  = STR("       SAVE");      // DAT_004d40d8
static constexpr auto s_pftExitLoad  = STR("       LOAD");      // DAT_004d40c0
static const unsigned char* s_pftExitTable[] = {
    s_pftExitSave, s_pftExitLoad
};
static constexpr auto s_pftSave      = STR("SAVE");             // DAT_004d40a8
static constexpr auto s_pftLoad      = STR("LOAD");             // DAT_004d40a0
static const unsigned char* s_pftHeaderTable[] = {
    s_pftSave, s_pftLoad
};
static constexpr auto s_pftGame      = STR("     GAME");        // DAT_004d4090

// --- Confirmation dialog (state 5) ---

static constexpr auto s_pftOverwritePrompt = STR("OK TO OVERWRITE THE DATA?"); // DAT_004d4170
static constexpr auto s_pftYesNo           = STR(" YES  NO ");  // DAT_004d4190

// --- Error messages (state 9) ---

static constexpr auto s_pftNoFreeSpace  = STR("NOT ENOUGH FREE SPACE");   // DAT_004d4120
static constexpr auto s_pftOnHardDrive  = STR("               ON HARD DRIVE."); // DAT_004d4148

// ============================================================================
// FileWrite (0x004122a0)
// Write a buffer to a file. Returns bytes written or -1 on failure.
// ============================================================================
int FileWrite(const char* name, void* buf, int len)
{
    FILE* fp = fopen(name, "wb");
    if (fp == NULL) return -1;
    size_t written = fwrite(buf, 1, len, fp);
    fclose(fp);
    if ((int)written != len) return -1;
    return len;
}

// ============================================================================
// ReadSaveFile (wraps FUN_004120c0)
// Read a save file into a buffer. Returns file size or -1 on failure.
// Original: 0x004120c0 — reads file with install path fallback and async support.
// ============================================================================
int ReadSaveFile(const char* path, void* buffer)
{
    // Try the direct path first
    FILE* fp = fopen(path, "rb");
    if (fp == NULL) {
        // Try with install path prefix
        char fullPath[260];
        sprintf(fullPath, "%s%s", g_szInstallPath, path + 8);
        fp = fopen(fullPath, "rb");
        if (fp == NULL) return -1;
    }

    fseek(fp, 0, SEEK_END);
    int fileSize = (int)ftell(fp);
    fseek(fp, 0, SEEK_SET);
    size_t bytesRead = fread(buffer, 1, fileSize, fp);
    fclose(fp);

    if ((int)bytesRead != fileSize) return -1;
    return fileSize;
}

// ============================================================================
// EnsureDirectoryExists (0x0040b700)
// Ensures the directory portion of a file path exists.
// ============================================================================
void EnsureDirectoryExists(const char* path)
{
    char dirPath[260];
    sprintf(dirPath, "%s", path);
    char* lastSlash = strrchr(dirPath, '\\');
    if (lastSlash != NULL) {
        *lastSlash = '\0';
        SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, FALSE };
        CreateDirectoryA(dirPath, &sa);
    }
}

// ============================================================================
// GetSaveLocationIndex (0x00494000)
// Maps stageId/roomId to a location name table index (0-6).
// ============================================================================
int GetSaveLocationIndex(int stageId, int roomId)
{
    int locIdx = stageId % 5;
    if (locIdx == 0) {
        if (roomId == 6) locIdx = 1;
        if (locIdx == 0 && roomId == 0x18) locIdx = 5;
    }
    if (locIdx == 2 && roomId == 7) locIdx = 6;
    return locIdx;
}

// ============================================================================
// DrawSaveCursor (0x00493fa0)
// Draws a blinking cursor/highlight sprite at the given position.
// mode==0 draws the cursor, mode!=0 draws nothing.
// Uses g_TextureDesc — only overwrites geometry/UV fields, inherits
// flags/colorMul/pivot from the previous PrintFormattedText call.
// ============================================================================
void DrawSaveCursor(short x, short y, int mode)
{
    if (mode == 0) {
        g_TextureDesc.width = 8;
        g_TextureDesc.height = 14;
        g_TextureDesc.depth = 0x1E;
        g_TextureDesc.texU = 16;
        g_TextureDesc.texV = 28;
        g_TextureDesc.unk10 = 0x100;
        g_TextureDesc.printClutTint = 0x1E0;
        g_TextureDesc.screenX = x - g_ScreenOffsetX;
        g_TextureDesc.screenY = y - g_ScreenOffsetY;
        AddTintSprite(&g_TextureDesc, 2);
    }
}

// ============================================================================
// Flg_ck (0x00473f40)
// Checks a bit flag in a flag array.
// baseAddr: base address of the flag array
// bitIndex: bit position to check
// Returns non-zero if the bit is set, 0 otherwise.
// ============================================================================
unsigned int Flg_ck(int baseAddr, unsigned int bitIndex)
{
    // Original: *(uint*)(((bitIndex & 0xffffffe7) >> 3) + baseAddr) & (0x80000000 >> (bitIndex & 0x1f))
    unsigned int wordIndex = (bitIndex & 0xFFFFFFE7) >> 3;
    unsigned int bitMask = 0x80000000 >> (bitIndex & 0x1F);
    unsigned int* flagWord = (unsigned int*)((unsigned char*)baseAddr + wordIndex);
    return *flagWord & bitMask;
}

// ============================================================================
// use_room_action_item (0x004631f0)
// Uses the currently selected item (e.g., ink ribbon, key, weapon).
// Consumes the item from inventory and handles related game state.
// ============================================================================
// NOTE: the original reads/writes g_ItemSlotsPointer (0x00d22768) directly and
// has no NULL guard. The port previously used g_firstItemSlotPointer (a .gwipe
// save-overlay global that is never assigned) and bailed when it was NULL -
// which silently made door-key consumption a no-op. Fixed to the original's
// pointer; the guard was the dead-pointer crutch, not part of the original.
void use_room_action_item(void)
{
    unsigned char* slots = (unsigned char*)g_ItemSlotsPointer;

    g_usedItemId = g_selectedItemId;

    // Lockpick doesn't consume
    if (g_selectedItemId == 0x31) return;

    // Find the item in inventory
    unsigned char index = 0;
    unsigned char itemId = slots[0];
    while (itemId != g_selectedItemId) {
        index++;
        itemId = slots[index * 2];
    }

    // If item is a weapon (ID < 0x0B): unequip and remove
    if (g_selectedItemId < 0x0B) {
        slots[index * 2] = 0;
        if ((unsigned int)g_EquippedItemId - (unsigned int)index == 1) {
            g_EquippedItemId = 0;
        }
        rearrange_item_slots();
        return;
    }

    // For consumable items: decrement quantity
    unsigned char quantity = slots[index * 2 + 1];
    if (quantity != 0) {
        slots[index * 2 + 1] = quantity - 1;
        if (slots[index * 2 + 1] == 0) {
            // Item depleted
            if (g_selectedItemId > 0x32 && g_selectedItemId < 0x3D) {
                // Key item depleted — display drop message
                g_main_state_flags |= 0x2000;
                return;
            }
            slots[index * 2] = 0;
            rearrange_item_slots();
        }
    }
}

// ============================================================================
// rearrange_item_slots (0x00451510)
// Compacts the item inventory by removing empty gaps left by consumed items.
// The original reads/writes g_ItemSlotsPointer, g_TotalHeldItems,
// g_ItemSlotsBitmask and g_ItemSlotIndices directly (no NULL guard). An
// earlier port used g_firstItemSlotPointer / g_totalHeldItems /
// g_heItemsX2Less1 / g_itemSlotIndices - .gwipe overlay globals that are
// never assigned - which made this function a silent no-op and left the
// slot->sheet-row mapping (g_ItemSlotIndices) stale after consumption.
// ============================================================================
void rearrange_item_slots(void)
{
    unsigned char equippedSlotIdx = g_EquippedItemId - 1;
    unsigned char readIdx = 0;
    unsigned char writeIdx = 0;
    // Chris (0) has 6 slots, Jill (1) has 8 slots
    int maxSlots = (4 - (((g_playerEntity.id & 3) != 1) ? 1 : 0)) * 2;
    int remaining = maxSlots;

    do {
        unsigned char itemId = ((unsigned char*)g_ItemSlotsPointer)[readIdx * 2];
        if (itemId == 0) {
            // Empty slot — skip, clear held-items bit
            if (readIdx < g_TotalHeldItems) {
                g_ItemSlotsBitmask &= ~(1u << (g_ItemSlotIndices[readIdx] & 0x1F));
            }
        } else {
            // Has item — compact
            if (writeIdx != readIdx) {
                ((unsigned char*)g_ItemSlotsPointer)[writeIdx * 2] = itemId;
                ((unsigned char*)g_ItemSlotsPointer)[writeIdx * 2 + 1] =
                    ((unsigned char*)g_ItemSlotsPointer)[readIdx * 2 + 1];
                g_ItemSlotIndices[writeIdx] = g_ItemSlotIndices[readIdx];
                if (equippedSlotIdx == readIdx) {
                    equippedSlotIdx = writeIdx;
                }
            }
            writeIdx++;
        }
        readIdx++;
        remaining--;
    } while (remaining != 0);

    g_EquippedItemId = equippedSlotIdx + 1;
    g_TotalHeldItems = writeIdx;

    // Zero remaining empty slots
    for (int i = maxSlots - writeIdx; i > 0; i--) {
        ((unsigned char*)g_ItemSlotsPointer)[writeIdx * 2] = 0;
        ((unsigned char*)g_ItemSlotsPointer)[writeIdx * 2 + 1] = 0;
        writeIdx++;
    }
}

// ============================================================================
// LoadSaveGameState (0x00493310)
//
// Main save/load screen state machine.
// Parameters:
//   mode          — 0 = save mode, 1 = load mode. Controls the exit behavior
//                   (0 returns in-game, anything else chains to title_state)
//                   and the slot-selection state (state = mode + 3).
//   flags         — unused by the screen itself; passed through to the
//                   slot-select play_sfx bank (always a non-bank value, so
//                   that call is silent in the original).
//   useInkRibbon  — non-zero to consume ink ribbon when saving; also the
//                   play_sfx bank for the cursor-move sounds.
//   exitMode      — play_sfx bank for the confirm/cancel/animation sounds
//                   (2 from the typewriter, 1 from the title screen).
//   cutsceneReset — controls cut_set/StMask on exit (0 = do reset).
//
// Called from:
//   title_state:        LoadSaveGameState(1, 0x80180000, 0, 1, 0)  — Load Game
//   check_typewriter:   LoadSaveGameState(0, flags, ribbon+1, 2, 0) — Save Game
// ============================================================================
void LoadSaveGameState(int mode, int flags, int useInkRibbon, int exitMode, int cutsceneReset)
{
    // Save slot info table (9 entries: 8 file slots + 1 exit option)
    SaveSlotInfo save_slots[SAVE_SLOT_COUNT + 1];
    MenuState state = STATE_INIT;
    int selected_slot = 0;              // current cursor position (0..7 = slots, 8 = exit)
    int confirm_choice = 0;             // 0 = YES, 1 = NO (confirmation dialog)
    int blink_timer = 5;
    int blink_state = 0;                // 0 = cursor visible, 1 = invisible
    int input_delay = 0;
    int anim_counter = 0;               // reveal length counter
    int anim_timer = 5;                 // reveal pause between steps

    char saveBuffer[SAVE_FILE_SIZE + 8];    // slot-scan / load buffer
    char fileBuffer[SAVE_FILE_SIZE + 8];    // save-assembly buffer
    char displayStr[64];                    // save-animation reveal string (57 bytes)
    char animBuf[64];                       // reveal buffer (terminated at anim_counter)

    // Set save/load active flag
    g_loadSaveStateFlag = 1;

    LoadFile(GAME_DATA_ROOT "data\\type00.tim", g_TimImageBuffer, 0x20);
    g_main_state_flags = (g_main_state_flags & 0x3FFFFFFF) | 0x80000000;
    display_image(8, g_TimImageBuffer__bitmap, 320, 240);
    title_setup_texture_pages(8, 1);
    empty_00470960(8);

    // Main loop
    do {
        switch (state) {

        // ================================================================
        // State 0: Scan save files — build slot info table
        // ================================================================
        case STATE_INIT:
        {
            state = STATE_IDLE;

            for (int slotIndex = 0; slotIndex < 9; slotIndex++)
            {
                sprintf(g_saveFileName, "%ssavedat%d.dat", GAME_SAVE_ROOT, slotIndex + 1);
                FILE* fp = fopen(g_saveFileName, "r");
                if (fp == NULL) {
                    save_slots[slotIndex].hasData = 0;
                } else {
                    ReadSaveFile(g_saveFileName, saveBuffer);

                    save_slots[slotIndex].hasData       = 1;
                    save_slots[slotIndex].characterId   = saveBuffer[OFFSET_CHARACTER_ID];
                    save_slots[slotIndex].savesCount    = saveBuffer[OFFSET_SAVES_COUNT];
                    save_slots[slotIndex].stageId       = saveBuffer[OFFSET_STAGE_ID];
                    save_slots[slotIndex].roomId        = saveBuffer[OFFSET_ROOM_ID];

                    fclose(fp);
                }
            }
            // Fall through to state 1
        }

        // ================================================================
        // State 1: Main navigation — cursor movement and selection
        // ================================================================
        case STATE_IDLE:
        {
            // Blink cursor handling (matches original: test old value before decrement)
            if (blink_timer == 0) {
                blink_state = !blink_state;
                blink_timer = 5;
            } else {
                blink_timer--;
            }

            // UP Pressed (use g_RawPadHeld for continuous held detection)
            if ((g_RawPadHeld & 0x1000) != 0) {
                blink_state = 0;
                blink_timer = 5;
                input_delay = 6;
                if (--selected_slot < 0) selected_slot = 8;
                state = STATE_INPUT_DELAY;
                play_sfx(useInkRibbon, 30);
            }

            // Down pressed (use g_RawPadHeld for continuous held detection)
            if ((g_RawPadHeld & 0x4000) != 0) {
                blink_state = 0;
                blink_timer = 5;
                input_delay = 6;
                if (++selected_slot > 8) selected_slot = 0;
                state = STATE_INPUT_DELAY;
                play_sfx(useInkRibbon, 30);
            }

            // SideWinder pad check
            DWORD sidewinderBtn = 0;
            if (g_isSideWinderConnected) {
                sidewinderBtn = read_sidewinder_pad() & 0x10000;
            }

            // PAD_CROSS pressed (confirm) or SideWinder start
            if (((g_PlayerDpadPressed & 0x4000) != 0) || (sidewinderBtn != 0)) {
                if (selected_slot == SAVE_SLOT_COUNT) {
                    // Exit option selected
                    play_sfx(exitMode, 29);
                    if (mode == 0) {
                        // In-game: return to gameplay
                        if (!cutsceneReset) {
                            cut_set();
                            g_main_state_flags = (g_main_state_flags & 0x3FFFFFFF) | 0x80000000;
                            StMask(1, 0);
                        }
                        g_loadSaveStateFlag = 0;
                        return;
                    }
                    // From title: chain back to title_state
                    Task_sleep(4);
                    Task_chain((void*)title_state);
                } else {
                    // Non-exit slot selected → go to mode-specific state
                    // (original: state = mode + 3 → 3 = save, 4 = load)
                    play_sfx(exitMode, 31);
                    state = (MenuState)(mode + 3);
                }
            }

            // (cancel/back)
            if ((g_PlayerDpadPressed & 0x8000) != 0) {

                play_sfx(exitMode, 29);

                if (mode == 0) {
                    // In-game: return to gameplay
                    if (!cutsceneReset) {
                        cut_set();
                        g_main_state_flags = (g_main_state_flags & 0x3FFFFFFF) | 0x80000000;
                        StMask(1, 0);
                    }
                    g_loadSaveStateFlag = 0;
                    return;
                } else {
                    // From title: chain back to title_state
                    Task_sleep(4);
                    Task_chain((void*)title_state);
                }
            }

            break;
        }

        // ================================================================
        // State 2: Input repeat delay — prevents rapid cursor movement
        // ================================================================
        case STATE_INPUT_DELAY:
        {
            // no direction held (use g_RawPadHeld for continuous held detection)
            if ((g_RawPadHeld & (0x1000 | 0x4000)) == 0) {
                input_delay = 0;
            }

            // Original tests the old value, then decrements; on old==0 → idle
            if (input_delay == 0) {
                state = STATE_IDLE;
            } else {
                input_delay--;
            }

            // still blink (matches original: test old value before decrement)
            if (blink_timer == 0) {
                blink_state = !blink_state;
                blink_timer = 5;
            } else {
                blink_timer--;
            }
            break;
        }

        // ================================================================
        // State 3: Save mode — check if slot already has data
        // ================================================================
        case STATE_SAVE_SLOT_SELECTED:
        {
            if (save_slots[selected_slot].hasData) {
                // Slot hasData -> confirmation dialog
                blink_state = 1;
                input_delay = 0;
                state = STATE_CONFIRM_OVERWRITE;
            } else {
                // Empty slot -> save immediately
                state = STATE_PERFORM_SAVE;
            }
            break;
        }

        // ================================================================
        // State 4: Load mode — load game from save slot
        // ================================================================
        case STATE_LOAD_SLOT_SELECTED:
        {
            if (save_slots[selected_slot].hasData) {
                sprintf(g_saveFileName, "%ssavedat%d.dat", GAME_SAVE_ROOT, selected_slot + 1);
                EnsureDirectoryExists(GAME_SAVE_ROOT);
                int fileSize = ReadSaveFile(g_saveFileName, fileBuffer);

                // The block restore always runs; the extra areas past 0x800 are
                // size-gated so older (smaller) save files still load. The
                // original restores the block with one 0x800 memcpy; the port
                // models that region as g_BioCard + the input-config globals,
                // so each part is copied into its own global (the unmodeled
                // tail 0x43D..0x800 is discarded).
                memcpy(g_BioCardData, fileBuffer, sizeof(BioCardLayout));
                memcpy(g_padRemapSubTable3, fileBuffer + OFFSET_PAD_REMAP,
                       sizeof(g_padRemapSubTable3));
                g_controllerConfig = fileBuffer[OFFSET_CONTROLLER_CFG];
                if (fileSize > SAVE_BLOCK_SIZE) {
                    memcpy(g_keyBindingData, fileBuffer + OFFSET_KEY_BINDINGS, 0x20);
                    memcpy(g_JoyRemapTbl, fileBuffer + OFFSET_JOY_REMAP, 0x100);
                    InitInputKeyBindings();
                    if (fileSize > 0x920) {
                        memcpy(g_roomBgmState, fileBuffer + OFFSET_ROOM_BGM, 0xE0);
                        if (fileSize > 0xA00) {
                            // file[0xA00] holds the saved sidewinder flag; the
                            // original loads it into a dead stack local — the
                            // language selection below re-checks the live flag.
                            memcpy(&DAT_004d6444, fileBuffer + OFFSET_LANG_BYTE, 1);
                            if (fileSize > 0xA02) {
                                memcpy(g_joyRemapBackupKey, fileBuffer + OFFSET_KEY_BACKUP, 0x80);
                                memcpy(g_joyRemapBackupJoy, fileBuffer + OFFSET_JOY_BACKUP, 0x80);
                                memcpy(g_JoyRemapTbl[1],
                                       g_isSideWinderConnected
                                           ? g_joyRemapBackupJoy : g_joyRemapBackupKey,
                                       0x80);
                            }
                        }
                    }
                }

                g_main_state_flags |= 0x10000000;
                g_playerEntityPointer.id = g_SelectedCharactedId;
                if ((g_SelectedCharactedId & 3) != 0) {
                    g_main_state_flags |= 0x800000;
                }
                g_loadSaveStateFlag = 0;
                return;
            }
            // Empty slot – go back
            state = STATE_IDLE;
            break;
        }

        // ================================================================
        // State 5: Confirmation dialog (Yes/No for overwrite)
        // ================================================================
        case STATE_CONFIRM_OVERWRITE:
        {
            PrintFormattedText(49, 193, 1, s_pftOverwritePrompt);
            PrintFormattedText(118, 209, 0, s_pftYesNo);

            // no direction held → reset delay
            if ((g_RawPadHeld & 0x5000) == 0) {
                input_delay = 0;
            }

            if (input_delay == 0) {
                // blink cursor
                if (blink_timer == 0) {
                    blink_state = !blink_state;
                    blink_timer = 5;
                } else {
                    blink_timer--;
                }

                // Left arrow → YES (0), right arrow → NO (1)
                if ((g_RawPadHeld & 0x8000) != 0) {
                    blink_state = 0;
                    blink_timer = 5;
                    input_delay = 6;
                    confirm_choice = 0;
                }
                if ((g_RawPadHeld & 0x2000) != 0) {
                    blink_state = 0;
                    blink_timer = 5;
                    input_delay = 6;
                    confirm_choice = 1;
                }
                // Confirm: YES → perform save, NO → back to navigation
                if ((g_PlayerDpadPressed & 0x4000) != 0) {
                    state = (confirm_choice == 1) ? STATE_IDLE : STATE_PERFORM_SAVE;
                }
                // Cancel → back to navigation
                if ((g_PlayerDpadPressed & 0x8000) != 0) {
                    state = STATE_IDLE;
                }
            } else {
                input_delay--;
            }
            break;
        }

        // ================================================================
        // State 6: Execute save — build save data and write file
        // ================================================================
        case STATE_PERFORM_SAVE:
        {
            EnsureDirectoryExists(GAME_SAVE_ROOT);
            // (The original seeds the display string with a copy of the save
            // directory and truncates at a backslash — dead, the reveal
            // string is fully rebuilt below.)

            // Consume the ink ribbon. Jill needs the ribbon flag (bit 0x7B),
            // Chris always spends one when the typewriter offers it.
            if ((useInkRibbon != 0) &&
                (((g_playerEntityPointer.id & 3) != 1) ||
                 (Flg_ck((int)g_PlayerFlags, 0x7b) != 0))) {
                g_selectedItemId = 0x2F;   // ink ribbon
                use_room_action_item();
            }

            // Snapshot the current player state into the save block
            // (g_BioCard +0x22B..0x232).
            g_PlayerPosXCopy         = (short)g_playerEntityPointer.scaMatrixData.localMatrix.t[0];
            g_PlayerPosZCopy         = (short)g_playerEntityPointer.scaMatrixData.localMatrix.t[2];
            g_SelectedCharactedId    = g_playerEntityPointer.id;
            g_PlayerHealthStatusCopy = g_playerEntityPointer.healthStatusFlags;
            g_PlayerDirAngleCopy     = g_playerEntityPointer.directionAngle;

            sprintf(g_saveFileName, "%ssavedat%d.dat", GAME_SAVE_ROOT, selected_slot + 1);

            // Refresh the joystick-remap backup; the file stores both tables,
            // and the sidewinder-dependent one receives the live bindings.
            memcpy(g_isSideWinderConnected ? g_joyRemapBackupJoy : g_joyRemapBackupKey,
                   g_JoyRemapTbl[1], 0x80);

            // Assemble the save file (2690 bytes). The 0x820..0x91F region is
            // written twice on purpose (joy remap, then the joy backup over
            // its upper half) — same order as the original. The buffer is
            // zeroed first so the block tail the port does not model
            // (0x43D..0x800) stays deterministic instead of stack garbage.
            memset(fileBuffer, 0, SAVE_FILE_SIZE);
            memcpy(fileBuffer + 0x000, g_BioCardData, sizeof(BioCardLayout));
            memcpy(fileBuffer + OFFSET_PAD_REMAP, g_padRemapSubTable3,
                   sizeof(g_padRemapSubTable3));
            fileBuffer[OFFSET_CONTROLLER_CFG] = g_controllerConfig;
            memcpy(fileBuffer + OFFSET_KEY_BINDINGS, g_keyBindingData, 0x20);
            memcpy(fileBuffer + OFFSET_JOY_REMAP, g_JoyRemapTbl, 0x100);
            memcpy(fileBuffer + OFFSET_ROOM_BGM, g_roomBgmState, 0xE0);
            fileBuffer[OFFSET_SIDEWINDER] = (char)g_isSideWinderConnected;
            fileBuffer[OFFSET_LANG_BYTE]   = (char)DAT_004d6444;
            memcpy(fileBuffer + OFFSET_JOY_BACKUP, g_joyRemapBackupJoy, 0x80);
            memcpy(fileBuffer + OFFSET_KEY_BACKUP, g_joyRemapBackupKey, 0x80);
            FileWrite(g_saveFileName, fileBuffer, SAVE_FILE_SIZE);

            // Build the save-animation reveal string:
            //   name (10) + "\" + count(2) + "\" (8) + location (39) = 57 bytes
            const unsigned char* nameStr = s_pftCharNameTable[g_SelectedCharactedId & 3];
            for (int i = 0; i < 10; i++) {
                displayStr[i] = (char)nameStr[i];
            }
            displayStr[0x0A] = (char)0x38;   // '\'
            displayStr[0x0B] = (char)0xFB;
            displayStr[0x0C] = (char)((g_SavesCounter / 10) + 0x0C);
            displayStr[0x0D] = (char)0xFB;
            displayStr[0x0E] = (char)((g_SavesCounter % 10) + 0x0C);
            displayStr[0x0F] = (char)0xFB;
            displayStr[0x10] = (char)0x38;   // '\'
            displayStr[0x11] = (char)0xFB;
            int locIdx = GetSaveLocationIndex(g_stageId, g_roomId);
            const unsigned char* locStr = s_pftLocNameTable[locIdx];
            for (int i = 0x12; i < 0x39; i++) {
                displayStr[i] = (char)locStr[i - 0x12];
            }

            g_SavesCounter = (g_SavesCounter + 1 >= 100) ? 99 : (unsigned char)(g_SavesCounter + 1);

            // Start save animation
            anim_counter = 2;
            state = STATE_SAVE_ANIM_STEP1;
            break;
        }

        // ================================================================
        // State 7: Save animation — text reveal effect
        // Reveals the save slot text (char name + count + location) character
        // by character using PrintFormattedText with the reveal buffer.
        // ================================================================
        case STATE_SAVE_ANIM_STEP1:
        {
            state = STATE_SAVE_ANIM_STEP2;
            anim_timer = 5;

            // Copy the reveal prefix into the anim buffer
            int copyLen = (anim_counter > 0) ? anim_counter : 0;
            if (copyLen > 0) {
                memcpy(animBuf, displayStr, copyLen);
            }
            animBuf[copyLen] = 1;           // STR terminator
            anim_counter += 2;

            if (anim_counter > 58) {
                // Animation finished
                if (!cutsceneReset) {
                    cut_set();
                    g_main_state_flags = (g_main_state_flags & 0x3FFFFFFF) | 0x80000000;
                    StMask(1, 0);
                }
                g_loadSaveStateFlag = 0;
                return;
            }

            PrintFormattedText(55, (short)(16 * selected_slot + 45), 0, (unsigned char*)animBuf);

            // "Typewriter" tick — plays for every revealed non-space char
            // (the char two back from the copy end; spaces are 0x00).
            if (animBuf[copyLen - 2] != 0) {
                play_sfx(exitMode, 31);
            }
            break;
        }

        // ================================================================
        // State 8: Save animation delay — pause between text reveals
        // ================================================================
        case STATE_SAVE_ANIM_STEP2:
        {
            PrintFormattedText(55, (short)(16 * selected_slot + 45), 0, (unsigned char*)animBuf);

            // Original tests the old value, then decrements; on old<=0 → step 1
            if (anim_timer <= 0) {
                state = STATE_SAVE_ANIM_STEP1;
            } else {
                anim_timer--;
            }
            break;
        }

        // ================================================================
        // State 9: Error messages — "NOT ENOUGH FREE SPACE" / "ON HARD DRIVE."
        // Shown when save fails. Waits for any button to return to navigation.
        // ================================================================
        case STATE_ERROR_MSG:
        {
            PrintFormattedText(49, 209, 0, s_pftNoFreeSpace);
            PrintFormattedText(49, 225, 0, s_pftOnHardDrive);
            if (((g_RawPadHeld & 0xF000) != 0) ||
                ((g_PlayerDpadPressed & 0xC000) != 0)) {
                g_RawPadHeld = 0;
                g_PlayerDpadPressed = 0;
                state = STATE_IDLE;
            }
            break;
        }

        } // end switch

        // ================================================================
        // Render save slots using PrintFormattedText (STR encoding)
        // Positions from disassembly: slot X=55, save count X=103, location X=127
        // ================================================================
        int y = 45;
        for (int i = 0; i < 8; i++, y += 16) {
            if ((state == STATE_SAVE_ANIM_STEP1 || state == STATE_SAVE_ANIM_STEP2) && selected_slot == i)
                continue; // handled separately during animation

            const SaveSlotInfo* slot = &save_slots[i];

            if (slot->hasData) {
                // Filled slot: draw template dashes, then overlay character name
                PrintFormattedText(55, (short)y, 0, s_pftFilledSlot);

                // Character name overlay at same X=55 (from PTR_DAT_004d4118)
                unsigned char charIdx = (unsigned char)(save_slots[i].characterId & 3);
                PrintFormattedText(55, (short)y, 0, s_pftCharNameTable[charIdx]);

                // Save count at X=103 (sprintf + PrintText8x14 from assembly)
                int saveNum = save_slots[i].savesCount % 100;
                sprintf(PRINT_TEXT_BUFFER, "%02d", saveNum);
                PrintText8x14(103, (short)y, 0, 0);

                // Location name at X=127 (from PTR_DAT_004d42b8)
                int locIdx = GetSaveLocationIndex(save_slots[i].stageId, save_slots[i].roomId);
                PrintFormattedText(127, (short)y, 0, s_pftLocNameTable[locIdx]);
            } else {
                // Empty slot: full dash template
                PrintFormattedText(55, (short)y, 0, s_pftEmptySlot);
            }
        }

        // Print exit option — assembly draws two parts at same position:
        // 1. "      SAVE"/"      LOAD" (spaces + word)
        // 2. "DO NOT" (overwrites the leading spaces)
        // Result: "DO NOT SAVE" / "DO NOT LOAD"
        {
            int y = 45 + SAVE_SLOT_COUNT * 16;
            PrintFormattedText(55, (short)y, 0, s_pftExitTable[mode]);
            PrintFormattedText(55, (short)y, 0, s_pftDoNot);
        }

        // Print header — assembly draws two parts at same position:
        // 1. "SAVE"/"LOAD" at (124,13)
        // 2. "     GAME" at (124,13) — spaces don't overwrite, "GAME" follows
        // Result: "SAVE GAME" / "LOAD GAME"
        PrintFormattedText(124, 13, 0, s_pftHeaderTable[mode]);
        PrintFormattedText(124, 13, 0, s_pftGame);

        // Draw cursor arrow(s) — matching assembly at 0x00493ca8
        // blinkToggle: 0=cursor visible, 1=cursor hidden (toggles every 5 frames)
        // confirmChoice: 0=YES position, 1=NO position (state 5 only)
        {
            if (state == STATE_CONFIRM_OVERWRITE) {
                // State 5: draw slot cursor (always visible) + confirm cursor (blinks)
                DrawSaveCursor(47, (short)(45 + selected_slot * 16), 0);
                int confirmX = confirm_choice * 40 + 118;
                DrawSaveCursor((short)confirmX, 209, blink_state);
            } else {
                // States 1-4,9: cursor at slot position, blinks with blinkToggle
                DrawSaveCursor(47, (short)(45 + selected_slot * 16), blink_state);
            }
        }

        Task_sleep(1);

    } while (true);
}
