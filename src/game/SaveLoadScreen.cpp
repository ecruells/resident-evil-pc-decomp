// SaveLoadScreen.cpp - Save/Load game state screen
// Decompiled from Ghidra at 0x00493310
// Dependencies: FileWrite(0x004122a0), FUN_004120c0, FUN_0040b700,
//               FUN_00494000, FUN_00493fa0, InitInputKeyBindings(0x00497c20),
//               Flg_ck(0x00473f40), use_room_action_item(0x004631f0),
//               rearrange_item_slots(0x00451510)
#include "../Globals.h"
#include "FileLoader.h"
#include "SpriteRenderer.h"
#include "SFXIds.h"
#include "PrintText.h"
#include <cstdio>
#include <cstring>

extern void logos_state(void);
extern void title_state(void);
extern void game_start(void);

// // Save slot info structure — matches the 20-byte entries on the original stack
// struct SaveSlotInfo {
//     int previewF7b;     // +0x00 byte from save file at offset 0xF7B
//     int previewF78;     // +0x04 byte from save file at offset 0xF78
//     int stageId;        // +0x08 byte from save file at offset 0xF50
//     int roomId;         // +0x0C byte from save file at offset 0xF51
//     int hasData;        // +0x10 flag: 1 = file exists, 0 = no file
// };

enum SaveMenuMode
{
    MENU_LOAD = 0,
    MENU_SAVE = 2
};

typedef struct SaveSlotInfo
{
    uint32_t characterId;
    uint32_t stageId;
    uint32_t savesCount;
    uint32_t roomId;
    uint32_t hasData;
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
    STATE_ERROR_MSG = 9             // unused? default state
} MenuState;

// Save file constants
#define SAVE_SLOT_COUNT         8

// ============================================================================
// PrintFormattedText encoded data — compile-time encoded via STR() macro
// Decode with: python tools/decode_re1.py <address>
// Encoding: see PrintText.h for supported characters
// ============================================================================

// --- Character names (indexed by characterId & 3) ---

static constexpr auto s_pftChrisName   = STR("CHRIS");       // DAT_004d40f8
static constexpr auto s_pftJillName    = STR("JILL");         // DAT_004d4108
static const unsigned char* s_pftCharNameTable[] = {
    s_pftChrisName, s_pftJillName
};

// --- Location names (indexed by GetSaveLocationIndex) ---

static constexpr auto s_pftLocRoom1F      = STR(" M.Room 1F ");    // DAT_004d41a0
static constexpr auto s_pftLocHall1F      = STR(" M.Hall 1F ");    // DAT_004d41c8
static constexpr auto s_pftLocCourtyard   = STR(" Courtyard ");    // DAT_004d41f0
static constexpr auto s_pftLocGuardhouse  = STR(" Guardhouse");    // DAT_004d4218
static constexpr auto s_pftLocLaboratory  = STR(" Laboratory");    // DAT_004d4240
static constexpr auto s_pftLocStoreroom   = STR(" M.Storeroom ");  // DAT_004d4268
static constexpr auto s_pftLocCourtyard2  = STR(" Courtyard ");    // DAT_004d4290
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
// InitInputKeyBindings (0x00497c20)
// Copies key binding configuration from config area to active binding vectors.
// ============================================================================
void InitInputKeyBindings(void)
{
    // Copy key binding configuration to active input state
    // Original: copies 31 bytes from g_KeyBindingConfig (0x004d4730) area
    //           to g_KeyBindingVectors (0x00ac4030) area, then sets
    //           g_pMasterInputState = g_keyBindingData
    g_pMasterInputState = (MasterInputState*)g_keyBindingData;
    memcpy(g_KeyBindingVectors + 1, g_KeyBindingConfig, 31);
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
void use_room_action_item(void)
{
    if (g_firstItemSlotPointer == NULL) return;

    g_usedItemId = g_selectedItemId;

    // Lockpick doesn't consume
    if (g_selectedItemId == 0x31) return;

    // Find the item in inventory
    unsigned char index = 0;
    unsigned char itemId = g_firstItemSlotPointer[0];
    while (itemId != g_selectedItemId) {
        index++;
        itemId = g_firstItemSlotPointer[index * 2];
    }

    // If item is a weapon (ID < 0x0B): unequip and remove
    if (g_selectedItemId < 0x0B) {
        g_firstItemSlotPointer[index * 2] = 0;
        if ((unsigned int)g_EquippedItemId - (unsigned int)index == 1) {
            g_EquippedItemId = 0;
        }
        rearrange_item_slots();
        return;
    }

    // For consumable items: decrement quantity
    unsigned char quantity = g_firstItemSlotPointer[index * 2 + 1];
    if (quantity != 0) {
        g_firstItemSlotPointer[index * 2 + 1] = quantity - 1;
        if (g_firstItemSlotPointer[index * 2 + 1] == 0) {
            // Item depleted
            if (g_selectedItemId > 0x32 && g_selectedItemId < 0x3D) {
                // Key item depleted — display drop message
                g_main_state_flags |= 0x2000;
                return;
            }
            g_firstItemSlotPointer[index * 2] = 0;
            rearrange_item_slots();
        }
    }
}

// ============================================================================
// rearrange_item_slots (0x00451510)
// Compacts the item inventory by removing empty gaps left by consumed items.
// ============================================================================
void rearrange_item_slots(void)
{
    if (g_firstItemSlotPointer == NULL) return;

    unsigned char equippedSlotIdx = g_EquippedItemId - 1;
    unsigned char readIdx = 0;
    unsigned char writeIdx = 0;
    // Chris (0) has 6 slots, Jill (1) has 8 slots
    int maxSlots = (4 - (((g_playerEntity.id & 3) != 1) ? 1 : 0)) * 2;
    int remaining = maxSlots;

    do {
        unsigned char itemId = g_firstItemSlotPointer[readIdx * 2];
        if (itemId == 0) {
            // Empty slot — skip, clear held-items bit
            if (readIdx < (unsigned char)g_totalHeldItems) {
                g_heItemsX2Less1 &= ~(1 << (g_itemSlotIndices[readIdx] & 0x1F));
            }
        } else {
            // Has item — compact
            if (writeIdx != readIdx) {
                g_firstItemSlotPointer[writeIdx * 2] = itemId;
                g_firstItemSlotPointer[writeIdx * 2 + 1] = g_firstItemSlotPointer[readIdx * 2 + 1];
                g_itemSlotIndices[writeIdx] = g_itemSlotIndices[readIdx];
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
    g_totalHeldItems = writeIdx;

    // Zero remaining empty slots
    for (int i = maxSlots - writeIdx; i > 0; i--) {
        g_firstItemSlotPointer[writeIdx * 2] = 0;
        g_firstItemSlotPointer[writeIdx * 2 + 1] = 0;
        writeIdx++;
    }
}

// ============================================================================
// LoadSaveGameState (0x00493310)
//
// Main save/load screen state machine.
// Parameters:
//   mode          — 0 = save mode, 1 = load mode
//   flags         — display/behavior flags (0x80180000 for load from title)
//   useInkRibbon  — non-zero to consume ink ribbon when saving
//   exitMode      — controls exit behavior (0 = in-game, 1 = from title)
//   cutsceneReset — controls cut_set/StMask on exit (0 = do reset)
//
// Called from:
//   title_state:  LoadSaveGameState(1, 0x80180000, 0, 1, 0)  — Load Game
//   in-game save: LoadSaveGameState(0, 0x80180000, 1, 0, 0)  — Save Game
// ============================================================================
void LoadSaveGameState(int mode, int flags, int useInkRibbon, int exitMode, int cutsceneReset)
{
    // Save slot info table (9 entries: 8 file slots + 1 exit option)
    SaveSlotInfo save_slots[SAVE_SLOT_COUNT + 1];
    MenuState state = STATE_INIT;
    int selected_slot = 0;              // current cursor position (0..7 = slots, 8 = exit)
    int confirm_choice = 0;             // 0 = yes, 1 = no (for confirmation dialog)
    int blink_timer = 5;
    int blink_state = 0;                // 0 = invisible, 1 = visible
    int input_delay = 0;
    int anim_counter = 0;               // v39
    int anim_timer = 5;                 // v41

    char saveBuffer[2692];

    // Set save/load active flag
    g_loadSaveStateFlag = 1;

    LoadFile("./usa/data/type00.tim", g_BackgroundImageBuffer, 0x20);
    g_main_state_flags = (g_main_state_flags & 0x3FFFFFFF) | 0x80000000;
    display_image(8, g_BackgroundImageBuffer + 0x14, 320, 240);
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
                sprintf(g_saveFileName, ".\\savedat%d.dat", slotIndex + 1);
                FILE* fp = fopen(g_saveFileName, "r");
                if (fp == NULL) {
                    save_slots[slotIndex].hasData = 0;
                } else {
                    ReadSaveFile(g_saveFileName, saveBuffer);

                    save_slots[slotIndex].hasData       = 1;
                    save_slots[slotIndex].savesCount    = saveBuffer[512];
                    save_slots[slotIndex].roomId        = saveBuffer[513];
                    save_slots[slotIndex].stageId       = saveBuffer[552];
                    save_slots[slotIndex].characterId   = saveBuffer[555];

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

            // UP Pressed
            if ((g_PlayerPadHeld & PAD_UP) != 0) {
                blink_state = 0;
                blink_timer = 5;
                input_delay = 6;
                if (--selected_slot < 0) selected_slot = 8;
                state = STATE_INPUT_DELAY;
                play_sfx(SFX_BANKS, 30);
            }

            // Down pressed
            if ((g_PlayerPadHeld & PAD_DOWN) != 0) {
                blink_state = 0;
                blink_timer = 5;
                input_delay = 6;
                if (++selected_slot > 8) selected_slot = 0;
                state = STATE_INPUT_DELAY;
                play_sfx(SFX_BANKS, 30);
            }

            // SideWinder pad check
            DWORD sidewinderBtn = 0;
            if (g_isSideWinderConnected) {
                sidewinderBtn = read_sidewinder_pad() & 0x10000;
            }

            // PAD_CROSS pressed (confirm) or SideWinder start
            if ((g_PlayerPadPressed & PAD_CROSS) != 0 || sidewinderBtn != 0) {
                if (selected_slot == SAVE_SLOT_COUNT) {
                    // Exit option selected
                    play_sfx(SFX_BANKS, 29);
                    if (!exitMode) {
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
                    play_sfx(SFX_BANKS, 31);
                    state = (exitMode == 0) ? STATE_SAVE_SLOT_SELECTED : STATE_LOAD_SLOT_SELECTED;
                }
            }

            // PAD_SQUARE pressed (cancel/back)
            if (g_PlayerPadPressed < 0) {

                play_sfx(SFX_BANKS, 29);

                if (!exitMode) {
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
            // no direction held
            if ((g_PlayerPadHeld & (PAD_UP | PAD_DOWN)) == 0) {
                input_delay = 0;
            }

            if (--input_delay <= 0)
                state = STATE_IDLE;

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
            // if (save_slots[selected_slot].exists) {
            //     // Load the game
            //     sprintf(savedat_path, "%ssavedat%d.dat", aSave, selected_slot + 1);
            //     sub_40B700(aSave);
            //     int file_size = sub_4120C0(savedat_path, file_buffer);
            //     if (file_size <= 2048) {
            //         sub_412340(byte_BE9620, file_buffer, 2048);
            //     } else {
            //         sub_412340(byte_BE9620, file_buffer, 2048);
            //         sub_412340(asc_4D4730, small_buf1, 32);
            //         sub_412340(byte_4B1858, small_buf2, 256);
            //         sub_497C20();
            //         if (file_size > 2336) {
            //             sub_412340(byte_BE995C, small_buf4, 224);
            //             if (file_size > 2560) {
            //                 sub_412340(&byte_AC8BB8, &byte1_buf, 1);
            //                 sub_412340(&byte_4D6444, &byte2_buf, 1);
            //                 if (file_size > 2562) {
            //                     sub_412340(byte_4D3F58, small_buf5, 128);
            //                     sub_412340(byte_4D3FD8, small_buf3, 128);
            //                     const char* selected_lang = byte_AC8BB8 ? byte_4D3FD8 : byte_4D3F58;
            //                     sub_412340(&dword_4B18D8, selected_lang, 128);
            //                 }
            //             }
            //         }
            //     }
            //     g_main_state_flags |= 0x10000000;
            //     byte_BE62E5 = byte_BE984B;
            //     if (byte_BE984B & 3)
            //         g_main_state_flags |= 0x800000;
            //     dword_4D4678 = 0;
            //     return;
            // } else {
            //     state = STATE_IDLE;  // empty slot – go back
            // }
            break;
        }

        // ================================================================
        // State 5: Confirmation dialog (Yes/No for overwrite)
        // ================================================================
        case STATE_CONFIRM_OVERWRITE:
        {
            // sub_455190(49, 193, 1, unk_4D4170);  // "Overwrite?"
            // sub_455190(118, 209, 0, unk_4D4190); // "Yes No"
            // if (!(dword_BF0A04 & 0x5000))
            //     input_delay = 0;
            // if (input_delay) {
            //     --input_delay;
            // } else {
            //     if (--blink_timer <= 0) {
            //         blink_timer = 5;
            //         blink_state = !blink_state;
            //     }
            //     if (dword_BF0A04 & 0x8000) {   // Move cursor left/right?
            //         blink_state = 0;
            //         blink_timer = 5;
            //         input_delay = 6;
            //         confirm_choice = 0;
            //     }
            //     if (dword_BF0A04 & 0x2000) {
            //         blink_state = 0;
            //         blink_timer = 5;
            //         input_delay = 6;
            //         confirm_choice = 1;
            //     }
            //     if (word_BE9842 & 0x4000) {    // Confirm
            //         state = (confirm_choice == 0) ? STATE_PERFORM_SAVE : STATE_IDLE;
            //     }
            //     if ((int16_t)word_BE9842 < 0) { // Cancel
            //         state = STATE_IDLE;
            //     }
            // }
            break;
        }

        // ================================================================
        // State 6: Execute save — build save data and write file
        // ================================================================
        case STATE_PERFORM_SAVE:
        {
            // {
            //     sub_40B700(aSave);
            //     strcpy(backup_path, aSave);
            //     char* sep = strchr(backup_path, '\\');
            //     if (sep) *sep = '\0';
            //     // Fill save data into file_buffer (encrypt/compress)
            //     // ... (original complex data preparation)
            //     // After preparation:
            //     sub_412340(file_buffer, byte_BE9620, 2048);
            //     sub_412340(small_buf1, asc_4D4730, 32);
            //     sub_412340(small_buf2, byte_4B1858, 256);
            //     sub_412340(small_buf4, byte_BE995C, 224);
            //     sub_412340(&byte1_buf, &byte_AC8BB8, 1);
            //     sub_412340(&byte2_buf, &byte_4D6444, 1);
            //     sub_412340(small_buf3, byte_4D3FD8, 128);
            //     sub_412340(small_buf5, byte_4D3F58, 128);
            //     // Add save slot specific data (date, difficulty, etc.)
            //     {
            //         int idx = 0;
            //         const char* diff_str = off_4D4118[byte_BE984B & 3];
            //         do {
            //             backup_path[idx] = diff_str[idx];
            //             idx++;
            //         } while (idx < 10);
            //         backup_path[10] = 56;
            //         backup_path[11] = -5;
            //         backup_path[13] = -5;
            //         backup_path[12] = (byte_BE9848 / 10) + 12;
            //         backup_path[15] = -5;
            //         backup_path[16] = 56;
            //         backup_path[17] = -5;
            //         backup_path[14] = (byte_BE9848 % 10) + 12;
            //         unsigned int time_index = (uint8_t)word_BE9820 % 5;
            //         if (!time_index) {
            //             if (HIBYTE(word_BE9820) == 6) time_index = 1;
            //             if (!time_index && HIBYTE(word_BE9820) == 24) time_index = 5;
            //         }
            //         if (time_index == 2 && HIBYTE(word_BE9820) == 7) time_index = 6;
            //         int k = 18;
            //         const char* time_str = off_4D42B8[time_index];
            //         do {
            //             backup_path[k - 1] = time_str[k - 18];
            //             k++;
            //         } while (k < 57);
            //         if (++byte_BE9848 >= 100) byte_BE9848 = 99;
            //     }
            //     // Write the save file
            //     sprintf(savedat_path, "%ssavedat%d.dat", aSave, selected_slot + 1);
            //     sub_4122A0(savedat_path, file_buffer, 0xA82);
            //     // Start save animation
            //     anim_counter = 2;
            //     state = STATE_SAVE_ANIM_STEP1;
            // }
            break;
        }

        // ================================================================
        // State 7: Save animation — text reveal effect
        // Reveals the save slot text (char name + count + location) character
        // by character using PrintFormattedText with the reveal buffer.
        // ================================================================
        case STATE_SAVE_ANIM_STEP1:
        {
            // state = STATE_SAVE_ANIM_STEP2;
            // {
            //     int len = (anim_counter > 0) ? anim_counter : 0;
            //     memcpy(save_anim_str, backup_path, len);
            //     save_anim_str[len] = 1;
            //     anim_counter += 2;
            //     if (anim_counter <= 58) {
            //         sub_455190(55, 16 * selected_slot + 45, 0, save_anim_str);
            //         // The original played a sound if certain condition met.
            //     }
            //     // Fall through to drawing loop below
            // }
            break;
        }

        // ================================================================
        // State 8: Save animation delay — pause between text reveals
        // ================================================================
        case STATE_SAVE_ANIM_STEP2:
        {
            // sub_455190(55, 16 * selected_slot + 45, 0, save_anim_str);
            // if (--anim_timer <= 0)
            //     state = STATE_SAVE_ANIM_STEP1;
            // if (anim_counter > 58) {
            //     // Animation finished
            //     if (!skip_cleanup) {
            //         sub_4628C0();
            //         g_main_state_flags = (g_main_state_flags & 0x3FFFFFFF) | 0x80000000;
            //         sub_497690(1, 0);
            //     }
            //     dword_4D4678 = 0;
            //     return;
            // }
            // // Fall through to drawing loop
            break;
        }

        // ================================================================
        // State 9: Error messages — "NOT ENOUGH FREE SPACE" / "ON HARD DRIVE."
        // Shown when save fails. Waits for any button to return to navigation.
        // ================================================================
        case 9:
        {
            PrintFormattedText(49, 209, 0, s_pftNoFreeSpace);
            PrintFormattedText(49, 225, 0, s_pftOnHardDrive);
            if (((g_PlayerPadHeld & PAD_FACE) != 0) ||
                ((g_PlayerPadPressed & (PAD_CROSS | PAD_SQUARE)) != 0)) {
                g_PlayerPadHeld = 0;
                g_PlayerPadPressed = 0;
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

        // Confirmation dialog (state 5) — "OK TO OVERWRITE THE DATA?" + "YES NO"
        if (state == 5) {
            // Assembly: PUSH 0x4d4170, color=1, x=49, y=193
            PrintFormattedText(49, 193, 1, s_pftOverwritePrompt);
            // Assembly: PUSH 0x4d4190, color=0, x=118, y=209
            PrintFormattedText(118, 209, 0, s_pftYesNo);
        }

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
