// GameState.cpp - Game state task functions (logos, title, asset loading)
// All functions decompiled from Ghidra with original addresses
#include "../Globals.h"
#include "../marni/MarniSystem.h"
#include "../marni/MarniSound.h"
#include "../marni/PSXTexture.h"
#include "FileLoader.h"
#include <cstdio>

extern void FUN_00470a30(void);
extern void Object_DeleteAll(int a);
extern void SetVideoResolution(int w, int h);
extern void setSomeColor(int r, int g, int b);
extern void empty_00412380(void);
extern void SetSpriteBufferFlag(void);
extern void setBackColor(unsigned char r, unsigned char g, unsigned char b);
extern void empty_40ae40(int);
extern void vram_clr(int x, int y, int w, int h);

// Forward declarations
// ---------------------------------------------------------------------------
// LoadAllItemsTexture (0x004...)
// Loads item_all.pix into the items image buffer
// ---------------------------------------------------------------------------
static void LoadAllItemsTexture(void)
{
    LoadFile(".\\usa\\data\\item_all.pix", g_ITEMS_IMAGES_BUFFER, 0x20);
}

// ============================================================================
// load_global_assets (0x00429a40)
// Task function: loads all global textures/assets needed by the game.
// After loading, chains to logos_state.
// ============================================================================
void logos_state(void);

void load_global_assets(void)
{
    LoadAllItemsTexture();

    if (g_image_buffer == NULL) {
        OutputDebugStringA("[ASSET] FATAL: g_image_buffer is NULL in load_global_assets!\n");
        Task_exit();
        return;
    }

    LoadFile(".\\usa\\data\\fontus.tim", g_image_buffer, 0x20);
    g_TextureBankID = 30;
    ProcessTextureImage(g_image_buffer, 30, 0, 0);

    OutputDebugStringA("fontus.tim loaded\n");

    LoadFile(".\\usa\\data\\Font03t.tim", g_image_buffer, 0x20);
    g_TextureBankID = 1;
    ProcessTextureImage(g_image_buffer, 1, 0, 2);

    LoadFile(".\\usa\\data\\Optkey03.tim", g_image_buffer, 0x20);
    g_TextureBankID = 2;
    LoadTexturePage(g_image_buffer, 2, 0, 0xB, 0, 0, 0, 0);

    LoadFile(".\\usa\\data\\status.tim", g_image_buffer, 0x20);
    g_TextureBankID = 0x41C;
    LoadTexturePage(g_image_buffer, 0x1C, 4, 0, 0, 0, 1, 0);

    SetupTexturePageHandles(0, 1);

    LoadFile(".\\usa\\data\\statface.tim", g_image_buffer, 0x20);
    LoadTexturePage(g_image_buffer, g_TextureBankID & 0xFF, 0, 9, 0, 0, 0, 0);

    LoadFile(".\\usa\\data\\blue.tim", g_image_buffer, 0x20);
    LoadTexturePage(g_image_buffer, g_TextureBankID & 0xFF, 0, 10, 0, 0, 0, 0);

    LoadFile(".\\usa\\data\\staitem.tim", g_image_buffer, 0x20);
    LoadTexturePage(g_image_buffer, 0, 0, 0x1E, 0, 0, 0, 0);

    LoadFile(".\\usa\\data\\kage.tim", g_image_buffer, 0x20);
    LoadShadowMaskTexture(g_image_buffer, 0);

    int rectConfig[24] = {
        -400,     400,     -400,     400,
           0,       0,        0,       0,
         400,     400,     -400,    -400,
           0,    0x1A,        0,    0x1A,
           0,       0,     0x1D,    0x1D,
           0,       1,        0,       0,
    };
    CreateTexturedQuad(0, 0x2F, rectConfig);

    // FUN_0047b950(0);

    // Task_chain((void*)debug_state);
    Task_chain((void*)logos_state);
}

// ============================================================================
// logos_state (0x00442bb0)
// Logos/opening state: plays intro videos then chains to title_state.
// ============================================================================
void logos_state(void)
{
    g_playingGameFlag = 0;
    g_fmvPlayCount = 0;
    g_demoIdleTimer1 = 1;

    clear_textures();
    FUN_00470a30();
    Object_DeleteAll(1);

    SetVideoResolution(640, 480);
    g_demoIdleTimer1 = 0;
    SetVideoResolution(320, 240);

    setSomeColor(128, 128, 128);

    // if (g_bIsSoftwareRendering == FALSE) {
        // g_currentFMVID = 28;
        // g_FmvCharacterId = 0;
        // g_main_state_flags |= 0x40000;
    // } else {
    //     QueueVideoPlayback(29, 0);
    // }

    Task_sleep(3);

    // if (g_bIsSoftwareRendering == FALSE) {
        // g_currentFMVID = 23;
        // g_FmvCharacterId = 0;
        // g_main_state_flags |= 0x40000;
    // } else {
    //     QueueVideoPlayback(29, 0);
    // }

    Task_sleep(1);

    Task_chain((void*)title_state);
}

// ============================================================================
// debug_state — SFX Player Debug Menu
// Tests the MarniSound system interactively: load sound banks from
// g_SoundBanksTable, browse SFX entries, play/stop/loop individual sounds.
//
// Two-column layout showing 16 SFX slots per bank (0-7 left, 8-15 right).
// Uses direct GetAsyncKeyState for PC keyboard input with edge detection.
//
// Controls:
//   LEFT/RIGHT    : Change sound bank (0-15)
//   UP/DOWN       : Select SFX entry
//   ENTER         : Load selected bank
//   SPACE         : Play selected SFX (respects loop toggle)
//   S             : Stop selected SFX
//   L             : Toggle loop mode on/off
//   W/X           : Volume up/down
//   ESC           : Exit to logos_state
// ============================================================================
void debug_state(void)
{
    // Original address: 0x00429a40 continuation (debug_state inserted at chain point)
    static const char* bankNames[] = {
        "Knife",     "Knife2",   "Gun",      "Shotgun",
        "Magnum",    "Magnum2",  "Flame",    "Grenade",
        "Acid",      "Fire",     "Rocket",   "Bio",
        "Evil",      "Select",   "Ending",   "Win95"
    };

    static int curBank = 0;
    static int curEntry = 0;
    static int loadedBank = -1;
    static int sfxVolume = -1;     // -1 = max, -9999 = min
    static int loopMode = 0;       // 0 = one-shot, 1 = looping
    static int prevKeys = 0;

    OutputDebugStringA("[DEBUG] SFX Player - entering debug_state\n");

    while (1) {
        // --- Draw background (title screen pattern: blend=100, flags=1 → depth=2100 behind text) ---
        g_window_rect.w = 320;
        g_window_rect.textureId = 0;
        g_window_rect.r = 0;
        g_window_rect.g = 0;
        g_window_rect.b = 0;
        g_window_rect.x = -g_ScreenOffsetX;
        g_window_rect.h = 240;
        g_window_rect.y = -g_ScreenOffsetY;
        draw_rect(&g_window_rect, 100, 1);

        int y = 2;

        // Title
        sprintf(PRINT_TEXT_BUFFER, "=== SFX PLAYER ===");
        PrintText8x14(2, y, 0x8F, 0); y += 16;

        // Sound system status
        int dsReady = (g_pDirectSound != NULL && *(int*)((BYTE*)g_pDirectSound + 0x10));
        sprintf(PRINT_TEXT_BUFFER, "DS: %-7s  Vol: %-5d  Loop: %s",
                dsReady ? "READY" : "OFF", sfxVolume, loopMode ? "ON" : "OFF");
        PrintText8x14(2, y, 0x7F, 0); y += 16;

        // Bank selection
        sprintf(PRINT_TEXT_BUFFER, "Bank [%d]: %s  %s",
                curBank, bankNames[curBank],
                (curBank == loadedBank) ? "(loaded)" : "");
        PrintText8x14(2, y, 0x8F, 0); y += 18;

        // Separator
        sprintf(PRINT_TEXT_BUFFER, "----------------------------------------");
        PrintText8x14(2, y, 0x5F, 0); y += 16;

        // SFX entries — two columns: 0-7 on left, 8-15 on right
        if (loadedBank >= 0) {
            const char** tbl = g_SoundBanksTable[loadedBank];
            int startY = y;
            for (int i = 0; i < 8; i++) {
                int col2Idx = i + 8;

                // Left column (entries 0-7)
                {
                    const char* fn = tbl[i];
                    unsigned char col = (i == curEntry) ? 0x8F : 0x7F;
                    if (fn) {
                        int hnd = g_SfxBanks[i * 2];
                        sprintf(PRINT_TEXT_BUFFER, " %c[%2d] %-12s h=%d",
                                (i == curEntry) ? '>' : ' ', i, fn, hnd);
                        PrintText8x14(2, startY, col, 0);
                    } else {
                        sprintf(PRINT_TEXT_BUFFER, "  [%2d] ---", i);
                        PrintText8x14(2, startY, 0x5F, 0);
                    }
                }

                // Right column (entries 8-15)
                {
                    const char* fn = tbl[col2Idx];
                    unsigned char col = (col2Idx == curEntry) ? 0x8F : 0x7F;
                    if (fn) {
                        int hnd = g_SfxBanks[col2Idx * 2];
                        sprintf(PRINT_TEXT_BUFFER, " %c[%2d] %-12s h=%d",
                                (col2Idx == curEntry) ? '>' : ' ', col2Idx, fn, hnd);
                        PrintText8x14(160, startY, col, 0);
                    } else {
                        sprintf(PRINT_TEXT_BUFFER, "  [%2d] ---", col2Idx);
                        PrintText8x14(160, startY, 0x5F, 0);
                    }
                }

                startY += 14;
            }
            y = startY + 4;

            // Played entry status (no async calls — ExecAsync creates scheduler loop)
            int hnd = g_SfxBanks[curEntry * 2];
            sprintf(PRINT_TEXT_BUFFER, "Entry %d: handle=%d  %s",
                    curEntry, hnd,
                    (hnd != 0) ? "READY" : "(not loaded)");
            PrintText8x14(2, y, 0x8F, 0);
            y += 16;
        }

        // Controls legend at bottom
        y = 215;
        sprintf(PRINT_TEXT_BUFFER, "L/R:Bank ENT:Load UP/DN:Sel SPC:Play S:Stop L:Loop W/X:Vol ESC:Exit");
        PrintText8x14(2, y, 0x5F, 0);

        // --- Input handling with edge detection ---
        int keys = 0;
        if (GetAsyncKeyState(VK_UP)     & 0x8000) keys |= 0x001;
        if (GetAsyncKeyState(VK_DOWN)   & 0x8000) keys |= 0x002;
        if (GetAsyncKeyState(VK_LEFT)   & 0x8000) keys |= 0x004;
        if (GetAsyncKeyState(VK_RIGHT)  & 0x8000) keys |= 0x008;
        if (GetAsyncKeyState(VK_RETURN) & 0x8000) keys |= 0x010;
        if (GetAsyncKeyState(VK_SPACE)  & 0x8000) keys |= 0x020;
        if (GetAsyncKeyState('S')       & 0x8000) keys |= 0x040;
        if (GetAsyncKeyState('W')       & 0x8000) keys |= 0x080;
        if (GetAsyncKeyState('X')       & 0x8000) keys |= 0x100;
        if (GetAsyncKeyState('L')       & 0x8000) keys |= 0x200;
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) keys |= 0x400;

        int newKeys = keys & ~prevKeys;
        prevKeys = keys;

        if (newKeys & 0x001) { if (curEntry > 0)  curEntry--; }
        if (newKeys & 0x002) { if (curEntry < 15) curEntry++; }
        if (newKeys & 0x004) { if (curBank > 0)   { curBank--; loadedBank = -1; curEntry = 0; } }
        if (newKeys & 0x008) { if (curBank < 15)  { curBank++; loadedBank = -1; curEntry = 0; } }

        if (newKeys & 0x010) {  // ENTER — load bank
            if (dsReady) {
                char dbg[128];
                sprintf(dbg, "[DEBUG] SFX Player: loading bank %d (%s)\n", curBank, bankNames[curBank]);
                OutputDebugStringA(dbg);
                LoadSoundBank(curBank, NULL);
                loadedBank = curBank;
                g_SfxVolume = sfxVolume;
                curEntry = 0;
            }
        }

        if (newKeys & 0x020) {  // SPACE — play SFX
            if (loadedBank >= 0) {
                int hnd = g_SfxBanks[curEntry * 2];
                if (hnd != 0) {
                    char dbg[128];
                    sprintf(dbg, "[DEBUG] SFX Player: play entry %d (hnd=%d, loop=%d)\n",
                            curEntry, hnd, loopMode);
                    OutputDebugStringA(dbg);
                    set_volume(hnd, sfxVolume);
                    playSnd(hnd, loopMode ? 1 : 0);
                }
            }
        }

        if (newKeys & 0x040) {  // S — stop SFX
            if (loadedBank >= 0) {
                int hnd = g_SfxBanks[curEntry * 2];
                if (hnd != 0) {
                    OutputDebugStringA("[DEBUG] SFX Player: stop\n");
                    setSndStop(hnd);
                }
            }
        }

        if (newKeys & 0x080) {  // W — volume up
            sfxVolume += 500;
            if (sfxVolume > -1) sfxVolume = -1;
            g_SfxVolume = sfxVolume;
            if (loadedBank >= 0) {
                int hnd = g_SfxBanks[curEntry * 2];
                if (hnd != 0) set_volume(hnd, sfxVolume);
            }
        }

        if (newKeys & 0x100) {  // X — volume down
            sfxVolume -= 500;
            if (sfxVolume < -9999) sfxVolume = -9999;
            g_SfxVolume = sfxVolume;
            if (loadedBank >= 0) {
                int hnd = g_SfxBanks[curEntry * 2];
                if (hnd != 0) set_volume(hnd, sfxVolume);
            }
        }

        if (newKeys & 0x200) { loopMode ^= 1; }   // L — toggle loop

        if (newKeys & 0x400) { break; }           // ESC — exit

        Task_sleep(1);
    }

    // Cleanup: destroy loaded bank before leaving
    if (loadedBank >= 0) {
        for (int i = 0; i < 32; i += 2) {
            if (g_SfxBanks[i] != 0) {
                destroySndBank(g_SfxBanks[i]);
                g_SfxBanks[i] = 0;
            }
            g_SfxBanks[i + 1] = 0;
        }
        OutputDebugStringA("[DEBUG] SFX Player: banks cleaned up, exiting\n");
    }

    Task_chain((void*)logos_state);
}


// ---------------------------------------------------------------------------
// Flg_on (0x00473f60)
// Sets a bit flag in a flag array.
// baseAddr: base address of the flag array
// bitIndex: bit position to set
// ---------------------------------------------------------------------------
void Flg_on(int baseAddr, unsigned int bitIndex)
{
    unsigned int* flagWord = (unsigned int*)(((bitIndex & 0xffffffe7) >> 3) + baseAddr);
    *flagWord = *flagWord | (0x80000000U >> ((unsigned char)bitIndex & 0x1f));
}

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
// ResetGetAsyncKeyStateFlags (0x00497e60)
// Clears async key state tracking flags.
// ---------------------------------------------------------------------------
void ResetGetAsyncKeyStateFlags(void)
{
    // In the original, this resets per-frame key state tracking.
    // With GetAsyncKeyState-based input, this is effectively a no-op.
}

// ---------------------------------------------------------------------------
// ScheduleInputFlush (0x00497e80)
// Schedules an async call to ResetGetAsyncKeyStateFlags.
// ---------------------------------------------------------------------------
void ScheduleInputFlush(void)
{
    ExecAsync((void*)ResetGetAsyncKeyStateFlags);
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

    // Set player flags (secondary flags array at 0x00be989c)
    g_PlayerFlags2[0] = 0xbfffffff;    // first DWORD has cleared bit 30
    g_PlayerFlags2[1] = 0xffffffff;
    g_PlayerFlags2[2] = 0xffffffff;
    g_PlayerFlags2[3] = 0xffffffff;
    g_PlayerFlags2[4] = 0xffffffff;
    g_PlayerFlags2[5] = 0xfff7ffff;    // 5th DWORD has cleared bit 19
    g_PlayerFlags2[6] = 0xffffffff;
    g_PlayerFlags2[7] = 0xffffffff;

    // Set display values
    DAT_00be982c = 7;
    DAT_00be982d = 0xf0;
    DAT_00be982e = 0xf0;

    if ((g_playerEntity.id & 3) == 0) {
        // Chris: 6 slots, Rebecca gets Baretta + 15
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
        (&g_ItemSlotsIndexes)[slot_index] = slot_index;
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
    unsigned char itemSlot = *g_firstItemSlotPointer;
    while (itemSlot != 0 &&
           g_TotalHeldItems < (unsigned char)((4 - ((g_playerEntity.id & 3) != 1)) * 2)) {
        g_TotalHeldItems = g_TotalHeldItems + 1;
        itemSlot = g_firstItemSlotPointer[(unsigned int)g_TotalHeldItems * 2];
    }
}

// ---------------------------------------------------------------------------
// LoadHeldItemsImages (0x00451640)
// Loads inventory item images into the image buffer for HUD display.
// Counts held items, sets up slot bitmask and indices, then loads each
// item's image sprite via LoadItemImage using the item image lookup table.
// ---------------------------------------------------------------------------
void LoadHeldItemsImages(void* buf) // 0x00451640
{
    unsigned char totalItems;
    unsigned int index;

    CountHeldItems();
    g_ItemSlotsBitmask = (1 << (g_TotalHeldItems & 0x1f)) - 1;
    totalItems = g_TotalHeldItems;

    while (totalItems != 0) {
        totalItems = totalItems - 1;
        index = (unsigned int)totalItems;
        (&g_ItemSlotsIndexes)[index] = totalItems;
        unsigned char itemId = g_firstItemSlotPointer[index * 2];
        unsigned char imageType = g_ItemImageLookupTable[itemId * 4];
        LoadItemImage(imageType - 1, (int)index, buf);
    }
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
    g_main_state_flags2 = g_main_state_flags2 | 0x20000000;
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
    g_playerEntity.unk_be = 0;
    g_playerEntity.unk_bf = 0;
    g_playerEntity.unk_c0 = 0;
    g_playerEntity.flags = 1;
    g_playerEntity.unk_c2 = 0;
    g_playerEntity.unk_03 = 1;
    g_playerEntity.speed.y = 0;
    g_playerEntity.animationId = 0;
    g_playerEntity.animFrameId = 0;
    g_playerEntity.anim_86 = 0;
    g_playerEntity.anim_87 = 0;
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
    g_playerEntity.unk_d8 = 0;
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
    vram_clr(0, 0, 320, 480);

    g_main_state_flags = (g_main_state_flags & 0x3fffffff) | 0x40000000;

    Task_sleep(1);
    g_bGameActive = 2;

    g_main_state_flags = g_main_state_flags & 0xd4e900f0;
    g_main_state_flags = g_main_state_flags | 0x4000000;

    empty_00412380();

    memclr(&g_defaultItemSlot, g_BioCardData);

    g_loadDataDestPointer = g_image_buffer;
    g_SpecialRoomLightDelta = 0;
    g_fading_counter = 0;

    Task_execute(1, (void*)display_game_loading_message);

    LoadFile(".\\usa\\data\\bio_card.dat", g_loadDataDestPointer, 32);

    if ((g_main_state_flags & 0x10000000) == 0) {
        g_gameSessionInitFlag = 0;
        Game_timer = 0;

        g_playerEntity.id = g_SelectedCharactedId;
        g_playerEntity.healthStatusFlags = 0x10;

        memcpy(&g_BioCardData[0], g_loadDataDestPointer, 1052);

        g_SpecialRoomLightState = (short)0xFFFF;
        g_CharacterModelId = g_playerEntity.id;

        if ((g_main_state_flags2 & 0x10000000) == 0) {
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
        has_alternate_outfit = Flg_ck((int)g_PlayerFlags, 0x2a);
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

    g_firstItemSlotPointer = (unsigned char*)g_ItemsSlots;
    g_usedItemId = 0;
    DAT_00be9833 = 0;
    DAT_00be41e1 = 0;
    g_defaultItemSlot = 0;
    DAT_00be9614 = 0;

    LoadHeldItemsImages(g_image_buffer);

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
    g_loadDataDestPointer = g_image_buffer;
    g_BGM_STATE = 0xFF;

    load_room_sfx(0);
    load_character_sfx(g_playerEntity.id & 1);

    LoadSoundBank(g_playerEntity.equippedWeaponId, g_image_buffer);

    g_main_state_flags = g_main_state_flags & 0xfbffffff;

    init_room();

    g_AttractModeIdleTimer = 1;
    update_room_bgm();

    if ((g_playerEntity.id & 3) == CHAR_JILL) {
        has_alternate_outfit = Flg_ck((int)g_PlayerFlags, 0x7b);
        if (has_alternate_outfit == 0) {
            Flg_on((int)g_PlayerFlags2, 0x34);
            Flg_on((int)g_PlayerFlags3, 0x0b);
        }
    }

    g_AttractModeIdleTimer = 0;
    printf("end of game init\n");
}

// ============================================================================
// game_loop (0x00480b30)
// Main gameplay loop: entities, cameras, rooms, menus, combat.
// Returns: 1 = died/quit to title, 0 = game completed → ending.
//
// 0x00480b30: while ((g_menu_choice_id & 0x80) != 0) Task_sleep(1)
// 0x00480b45: g_main_state_flags |= 0x2000000
// 0x00480b6c: g_main_state_flags &= 0x3fffffff; g_main_state_flags |= 0x80000000
// 0x00480e02: do { ... Task_sleep(1); ... } while ((g_menu_choice_id & 0x80) != 0)
// 0x004813b9: do { ... } while (true) — main gameplay loop
// ============================================================================
int game_loop(void)
{
    // 0x00480b30: wait for the loading message (queued by
    // display_game_loading_message during InitializeGame) to be dismissed
    // before entering gameplay. The message is rendered each frame by
    // FUN_004557b0 in main_loop on the blanking (black) screen.
    while ((g_menu_choice_id & 0x80) != 0) {
        Task_sleep(1);
    }

    // 0x00480b45: mark gameplay active
    g_main_state_flags |= 0x2000000;

    // The full game loop is not yet implemented. The original (0x00480b6c)
    // clears 0x40000000 and sets 0x80000000 here, then arms a black fade-in
    // and renders the loaded room background. Until room rendering exists,
    // keep the screen in blanking mode (0x40000000, set by InitializeGame) so
    // the stale character-select background held in g_displayImageSRV is NOT
    // re-drawn (OT_InsertPrimitive/display_texture are gated by this flag) and
    // no white flashing occurs. Also drop any leftover fade state from the
    // character-select screen (g_fade_type_id=1 white flash) and hide the
    // transition for 6 frames (matches original 0x00480b5c StMask(0,6)).
    g_fading_state = -1;
    g_fading_counter = 0;
    StMask(0, 6);

    do {
        Task_sleep(1);
    } while (true);

    return 1;  // 1 = died, chains to title_state
}

// ============================================================================
// ending_state (0x00410820) — STUB
// Ending sequence: plays ending FMVs, credits, result screen.
// Sets up next-cycle save data and chains to title_state.
// ============================================================================
void ending_state(void)
{
    // 0x00410820
    OutputDebugStringA("[GAME] ending_state — stub\n");

    g_main_state_flags |= 0x80000;
    Task_sleep(1);

    sounds_reset();
    LoadSoundBank(0xe, g_image_buffer);

    // Stub: brief delay then return to title
    for (int i = 0; i < 90; i++) {
        Task_sleep(1);
    }

    g_dwClearCount++;
    Task_chain((void*)title_state);
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
        g_main_state_flags2 = g_main_state_flags2 & 0x20080000;
        Task_chain((void*)title_state);
    }

    g_main_state_flags2 = g_main_state_flags2 & 0x20080000;

    if (end_game_status == 0) {
        g_gameTimerSnapshot = Game_timer;
        Task_chain((void*)ending_state);
    }

    Task_chain((void*)logos_state);
}

// ============================================================================
// Stub implementations for functions not yet decompiled
// ============================================================================

// (0x0045fbb0) - Load item image into display buffer
void LoadItemImage(int imageType, int index, void* buf) { }

// (0x00481060) - Load attract mode (demo) player save data
void LoadAttractModePlayerData(void) { }

// (0x0047eb90) - Restore game state from bio card on load
void empty_0047eb90(int param) { }

// (0x0045fa80) - Load shoot direction effect sprite data
void load_shoot_direction_data(void) { }

// (0x0045a6d0) - Play title screen selection SFX
void title_select_sfx(void) { }

// (0x00462e90) - Check if player moved to different camera zone
void check_camera_switch(int param) { }

// (0x00477d90) - Load RDT file for current room
// Loads the Room Definition Table for the current stage/room, resolves internal
// relative pointers to absolute addresses, and sets up SCD script pointers.
void LoadRoomRdt(void)
{
    static const char hexDigits[] = "0123456789abcdef";

    // 0x00477d97: Set g_RdtPointer to the current load buffer
    g_RdtPointer = (RDT*)g_loadDataDestPointer;

    // 0x00477d9c: ESI = start of camera data (past RDT header)
    unsigned char* cameras = (unsigned char*)(g_RdtPointer + 1);

    // 0x00477da4-0x00477e02: Build RDT file path
    // Format: ./usa/stageX/roomXYYZ.rdt where X=stage, YY=room, Z=flag
    sprintf(FILE_PATH, ".\\usa\\stage%c\\room%c%c%c%c.rdt",
            hexDigits[g_stageId + 1],
            hexDigits[g_stageId + 1],
            hexDigits[g_roomId >> 4],
            hexDigits[g_roomId & 0xF],
            hexDigits[(g_main_state_flags & 0x800000) ? 1 : 0]);

    SetSpriteBufferFlag();

    LoadFile(FILE_PATH, g_RdtPointer, 1);

    // 0x00477e2a-0x00477e46: Resolve camera pointers
    // Each camera has 2 relative pointer fields (mask_pointer, tim_mask_pointer)
    // that need to be converted to absolute addresses.
    int cameraCount = g_RdtPointer->cameras_count;
    for (int i = 0; i < cameraCount; i++) {
        *(int*)(cameras) += (int)g_RdtPointer;
        *(int*)(cameras + 4) += (int)g_RdtPointer;
        cameras += 0x2C; // sizeof(RDT_Camera)
    }

    // 0x00477e48-0x00477e75: Resolve RDT pointer fields (offset 0x48 to 0x93)
    // These are relative offsets stored as ints, converted to absolute pointers.
    int* ptrField = (int*)((unsigned char*)g_RdtPointer + 0x48);
    int* ptrEnd = (int*)((unsigned char*)g_RdtPointer + 0x94);
    while (ptrField < ptrEnd) {
        *ptrField += (int)g_RdtPointer;
        ptrField++;
    }

    // 0x00477e7e-0x00477ec0: Resolve item model pointers
    // Iterates forward through entries, zeros table backward
    int* itemPtr = (int*)g_RdtPointer->items_models;
    int itemCount = g_RdtPointer->sound_banks_count;
    for (int i = itemCount; i > 0; i--) {
        // Zero out table entry (reverse order: table[count-1] down to table[0])
        ((int*)g_itemboxes_covers_table)[i - 1] = 0;
        if (itemPtr[0] != 0) itemPtr[0] += (int)g_RdtPointer;
        if (itemPtr[1] != 0) itemPtr[1] += (int)g_RdtPointer;
        itemPtr += 2;
    }

    // 0x00477ec9-0x00477f0b: Resolve obstacle model pointers
    // Same pattern: forward through entries, backward through table
    int* obstPtr = (int*)g_RdtPointer->obstacles_models;
    int obstCount = g_RdtPointer->unknown_03[0];
    for (int i = obstCount; i > 0; i--) {
        ((int*)g_desks_pointers_table)[i - 1] = 0;
        if (obstPtr[0] != 0) obstPtr[0] += (int)g_RdtPointer;
        if (obstPtr[1] != 0) obstPtr[1] += (int)g_RdtPointer;
        obstPtr += 2;
    }

    // 0x00477f12-0x00477f27: Set up SCD script pointers
    g_RoomInitScd = g_RdtPointer->initialization_scd;
    g_RoomScdOpcodes = g_RdtPointer->scd_opcodes;
    g_EvtScripts = g_RdtPointer->scd_opcodes2;

    // 0x00477f2d-0x00477f3f: Resolve EVT script relative offsets
    int* evtPtr = (int*)g_EvtScripts;
    while (*evtPtr != 0) {
        *evtPtr += (int)g_EvtScripts;
        evtPtr++;
    }

    // 0x00477f49-0x00477f64: Set back color from ambient light
    setBackColor(
        (unsigned char)g_RdtPointer->ambient_light_r,
        (unsigned char)g_RdtPointer->ambient_light_g,
        (unsigned char)g_RdtPointer->ambient_light_b);

    // 0x00477f6e: empty_40ae40(0)
    empty_40ae40(0);
}

// (0x00462990) - Display room camera background image
void display_room_camera_bg(void) { }

// (0x00487590) - SCD: Create a script event entry
void ScdEventEntry_Create(unsigned int slot, int scriptIndex) { }

// (0x0046a250) - SCD: Room action dispatch
void cmd_room_action(void) { }

// (0x0047f870) - Play 3D sound with voice effect
void play_sound_and_voice_effect(int type, int id) { }

// (0x00455260) - Set background clear color
void setBackColor(unsigned char r, unsigned char g, unsigned char b) { }

// (0x0040ae40) - Empty function called by LoadRoomRdt
void empty_40ae40(int param) { }

// (0x00473b10) - Texture bank setup variant
void FUN_00473b10(unsigned char p1, unsigned short p2, unsigned short p3, unsigned char p4, unsigned char p5, char p6) { }

// (0x00473d10) - Texture bank setup variant 2
void FUN_00473d10(unsigned char p1, unsigned short p2, unsigned short p3, unsigned char p4, unsigned char p5, char p6) { }

// (0x00473d60) - Entity animation trigger
void FUN_00473d60(char p1, unsigned char p2, unsigned char p3) { }

// (0x00473e40) - Texture page operation
void FUN_00473e40(int param) { }

// (0x00473ea0) - SCA matrix setup
void FUN_00473ea0(int param1, void* param2, ScaMatrixData* param3) { }

// (0x00473f10) - Flag set operation
void FUN_00473f10(int* baseAddr, unsigned int bitIndex) { }

// (0x0047cf80) - Sprite/billboard effect creation
void FUN_0047cf80(int param1, unsigned int param2, unsigned int param3, unsigned int param4, MATRIX* param5) { }

// (0x004804a0) - Sound fade control
void FUN_004804a0(short param1, unsigned int param2, short param3, unsigned int param4) { }

// (0x004805d0) - Sound parameter control
void FUN_004805d0(short param1, unsigned int param2, unsigned int param3, unsigned int param4) { }

// (0x00484d90) - Entity animation setup
void FUN_00484d90(int param1, unsigned char param2, unsigned char param3) { }

// (0x00484e40) - Entity animation setup variant
void FUN_00484e40(int param1, unsigned char param2, unsigned char param3) { }

// (0x004870d0) - Entity/sound operation
void FUN_004870d0(int param) { }

// (0x0048a190) - Entity effect setup
void FUN_0048a190(void* param1, int param2, int param3, int param4) { }

// (0x0048bfe0) - Joint animation processing
void FUN_0048bfe0(void) { }

// (0x0048c020) - Entity weapon setup
void FUN_0048c020(int param) { }

// (0x0048f330) - Get entity animation state
int FUN_0048f330(unsigned char param) { return 0; }

// (0x0047ee20) - Get item slot index
int get_item_slot(unsigned char itemId) { return -1; }

// (0x0047cf80) - Create billboard effect sprite
unsigned char Effect_CreateBillboard(unsigned char type, unsigned char param, unsigned short flags, MATRIX* spriteInfo, int* pos, char mode) { return 0; }

// (0x0047b410) - Build sound fade table
void BuildSndFadeTbl(char fadeType, int maxVol) { }

// (0x0040c560) - Camera/viewport operation
void FUN_0040c560(int param) { }

// Global stubs
unsigned long g_gameTimerSnapshot = 0;            // 0x00be9844
unsigned char g_equippedItemId = 0;               // 0x00be9849
extern const unsigned char DAT_004bec80[] = { 0 };  // 0x004bec80
void* room_check_actions[] = { nullptr };          // 0x004c1420
