// MainMenu.cpp - Main in-game menu (0x00463710)
// Decompiled from Ghidra with original addresses.
//
// The main menu is the in-game pause menu system. It handles:
//   - Status screen (character model, health bar, EKG)
//   - Inventory management (equip weapons, use items)
//   - Map screen (floor plan with room markers)
//   - Item box interaction (store/retrieve items)
//   - Options submenu access
//
// State machine: DAT_00ae9f11 controls the current submenu (0-8)
// Menu mode: DAT_00ae9f10 determines initial submenu on open
// ============================================================================
#include "../Globals.h"
#include "../system/AssetPath.h"
#include "../DebugPrint.h"
#include "FileLoader.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

// ============================================================================
// Main menu state block (0x00ae9f08 - 0x00ae9f4b)
// These globals are used exclusively by the menu system.
// ============================================================================

// Inventory draw depth layer (Z-order for render sorting)
static unsigned short g_invDepthLayer;    // 0x00ae9f08

// Menu mode (0=status, 1=use item, 2=itembox, 3=desk locked, 4=desk take,
//            5=map, 6=item model viewer)
unsigned char  DAT_00ae9f10;       // 0x00ae9f10

// Menu state (0=init, 1=status/nav, 2=message wait, 3=itembox, 4=model view,
//             5=map anim, 6=model view2, 7=exit anim, 8=exit wait)
static char           g_MainMenuState;       // 0x00ae9f11

static unsigned char  DAT_00ae9f12;       // 0x00ae9f12
unsigned char  DAT_00ae9f13;       // 0x00ae9f13

// 0x00ae9f14
// Frame part data pointer (used by load_main_menu_frame_part_tex_area)
unsigned short* g_CurrentMenuFramesDataPtr;      

static char           DAT_00ae9f18;       // 0x00ae9f18

// Character type flags (bit 0 = chris/jill, bit 1 = rebecca/barry)
static unsigned char  DAT_00ae9f19;       // 0x00ae9f19

// Max inventory slots (6 for chris, 8 for jill)
unsigned char  g_totalInventorySlots;       // 0x00ae9f1a

// Selected item ID for 3D model display
unsigned char  DAT_00ae9f1b;       // 0x00ae9f1b

// Item model load direction (-1 or 1)
static char           DAT_00ae9f1c;       // 0x00ae9f1c

// Model loaded flag (0x80 = loaded, lower bits = image type)
static unsigned char  DAT_00ae9f1e;       // 0x00ae9f1e

// Map available flag (1 = player has map)
static unsigned char  DAT_00ae9f1f;       // 0x00ae9f1f

// Item box submenu state (0=init, 1=cursor, 2=item selected, 3=scrolling)
static unsigned char  DAT_00ae9f20;       // 0x00ae9f20

// Item action submenu state (0=init, 1=action confirm, 2=use/view confirm,
// 3=return, 4=item-mix message wait)
static unsigned char  DAT_00ae9f22;       // 0x00ae9f22

static unsigned char  DAT_00ae9f21;       // 0x00ae9f21

// Cursor grid position (0-7 for items, 0-4 for menu tabs)
unsigned char  DAT_00ae9f23;       // 0x00ae9f23

// Item box scroll position
static unsigned char  DAT_00ae9f24;       // 0x00ae9f24

// Item move: original cursor position
static unsigned char  DAT_00ae9f25;       // 0x00ae9f25

static unsigned char  DAT_00ae9f26;       // 0x00ae9f26
static unsigned char  DAT_00ae9f27;       // 0x00ae9f27
static unsigned char  DAT_00ae9f28;       // 0x00ae9f28

// Item move: slide animation counters
static unsigned char  DAT_00ae9f29;       // 0x00ae9f29
static unsigned char  DAT_00ae9f2a;       // 0x00ae9f2a

// Health bar animation state (0=init, 1=scrolling in, 2=init out, 3=scrolling out)
static unsigned char  DAT_00ae9f2e;       // 0x00ae9f2e

// Health status (0=danger, 1=caution, 2-3=good, 4=poisoned)
static unsigned char  DAT_00ae9f2f;       // 0x00ae9f2f

// EKG line colors (RGB)
static unsigned char  DAT_00ae9f30;       // 0x00ae9f30
static unsigned char  DAT_00ae9f31;       // 0x00ae9f31
static unsigned char  DAT_00ae9f32;       // 0x00ae9f32

// EKG line colors (secondary)
static unsigned char  DAT_00ae9f33;       // 0x00ae9f33
static unsigned char  DAT_00ae9f34;       // 0x00ae9f34
static unsigned char  DAT_00ae9f35;       // 0x00ae9f35

// EKG scroll positions
static short          DAT_00ae9f36;       // 0x00ae9f36
static short          DAT_00ae9f38;       // 0x00ae9f38
static short          DAT_00ae9f3a;       // 0x00ae9f3a
static short          DAT_00ae9f3c;       // 0x00ae9f3c

// EKG wave data pointers
static char*          DAT_00ae9f40;       // 0x00ae9f40
static char*          DAT_00ae9f44;       // 0x00ae9f44

// Health bar face animation index (bits 0-1 = face, bits 2-3 = health status)
static unsigned char  DAT_00ae9f48;       // 0x00ae9f48
unsigned char  DAT_00ae9f49;       // 0x00ae9f49
unsigned char  DAT_00ae9f4a;       // 0x00ae9f4a

// Item box submenu scroll position (0-15)
static unsigned char  SUBMENU_STATE_ID;   // 0x00ae9f1d

// Item model path buffer (for loading TIM files)
static char           DAT_008e1cb0[128];  // 0x008e1cb0

// External function stubs (pending decompilation)
extern void empty_00470a20(void);
extern void FUN_004844b0(void);
extern void FUN_00470a40(void);
extern void FUN_0047d0e0(void);
extern void FUN_0040ac80(int idx, void* lightData);
extern void FUN_00484420(void* src, void* dst);
extern int draw_texture(TextureDesc* tex, unsigned short depth);
extern int   FUN_0044e1b0(void);
extern int   FUN_00470c60(void* prim, int depth);
extern int   FUN_00470e60(void* prim, int depth);
extern void  FUN_00438800(int a, int b, int c);
extern void FUN_00473f10(int* baseAddr, unsigned int bitIndex); // 0x00473f10
extern void Flg_on(int baseAddr, unsigned int bitIndex);         // 0x00473ef0
extern void FUN_00481ab0(void* fileState);                       // file dialog init
extern void FUN_00482250(void* fileState);                       // file dialog update
extern unsigned char* message_item_name_lookup(unsigned char itemId); // 0x00455140
void FUN_00454fd0(int itemId, int mode, short x, short y);            // 0x00454fd0

// g_ItemSlotsPointer is void* in the port; all slot access is byte-based.
#define ITEM_SLOTS  ((unsigned char*)g_ItemSlotsPointer)


extern const char DAT_004c29a0[];
extern const char DAT_004c29a8[];
extern const char DAT_004bd348[];
extern const char DAT_004bd5a0[];
extern const char DAT_004bd5a8[];
extern const unsigned char DAT_004b92c8[];
extern const unsigned char* g_ItemNamePointers[77];
extern const unsigned char* g_UnknownItemNamePointers[16];
extern const char* PTR_DAT_004b9278[];
extern const unsigned char DAT_004b92d8[];
extern const unsigned char DAT_004b92e0[];
extern const unsigned char DAT_004b92f0[];
extern const unsigned char DAT_004b92f8[];
extern void* DAT_004ba1e0[];
extern const unsigned short DAT_004d446c[];
extern const unsigned short DAT_004d4494[];
extern const unsigned short DAT_004d44c0[];
extern const char DAT_004d44e0[];
extern const char DAT_004d4500[];
extern const unsigned char DAT_004d2a08[];

// ============================================================================
// Input helpers
// The original read the D-pad from byte 1 of g_PlayerPadHeld (0x00bf0a11)
// and the action/cancel buttons from byte 1 of the remapped g_PlayerDpadPressed
// (0x00be9843). The port's pad words keep the same bit layout, so the byte-1
// accesses translate directly to (word >> 8) & 0xFF.
// ============================================================================
static unsigned char pad_held_byte1(void)
{
    return (unsigned char)(g_PlayerPadHeld >> 8);
}

static unsigned char dpad_pressed_byte1(void)
{
    return (unsigned char)(g_PlayerDpadPressed >> 8);
}

// Forward declarations for menu sub-functions
static void menu_exit_cleanup(void);              // 0x00463da0
static void menu_restore_game_state(void);        // 0x00463e10
void menu_update_equipped_weapon(void);    // 0x00463ec0
static void menu_draw_inventory(void);            // 0x00463f20
static void menu_load_item_model(void);           // 0x00464770
static void menu_update_fading_rect(void);        // 0x00429cb0
static void menu_handle_input(void);              // 0x00420880
static void menu_draw_health_bar(void);           // 0x00437fd0
static void menu_init_map_screen(void);           // 0x00488160
static void menu_init_status_screen(void);        // 0x004828f0
static int  menu_update_status_screen(void);      // 0x00482910
static void menu_reset_status_state(void);        // 0x00482b80
static int  menu_update_map_animation(void);      // 0x004886b0
static void menu_init_map_display(void);          // 0x00488880
static int  menu_itembox_interaction(void);       // 0x004941f0
static void load_main_menu_frame_part_tex_area(void);
static void draw_itembox_menu(void);              // 0x004947c0
static void loadMenuAssets(void);                 // 0x00494730
static void display_item_qty(unsigned char itemId, unsigned char qty, int depth); // 0x004645b0
static int  menu_item_use_item(void);             // 0x00401070
static int  menu_item_view_model(void);           // 0x004013a0
static int  menu_item_move_item(void);            // 0x00401470
static void menu_item_toggle_equip(void);         // 0x00401050
static int  menu_item_check_combine(void);        // 0x00401960
static void menu_item_apply_combine(void);        // 0x00401b00
static void menu_item_combine_refresh(unsigned char slot1, unsigned char slot2,
                                      unsigned char new1, unsigned char new2); // 0x00401df0
static int  menu_item_use_heal(unsigned char slot); // 0x00401260
static int  menu_item_use_item_none(unsigned char slot);   // 0x00401180/0x00401190
static int  menu_item_use_if_flag(unsigned char slot);     // 0x004011a0/0x004011e0
static int  menu_item_use_desk_key(unsigned char slot);    // 0x00401220
static int  menu_item_use_always(unsigned char slot);      // 0x00401380
static int  menu_item_use_none_return0(unsigned char slot);// 0x00401390
static void menu_use_ammo_combine_a(unsigned char slotA, unsigned char slotB); // 0x00401bf0
static void menu_use_ammo_combine_b(unsigned char slotA, unsigned char slotB); // 0x00401c60
static void menu_use_qty_merge(unsigned char slotA, unsigned char slotB);      // 0x00401cd0
static void menu_use_set_flag(unsigned char slotA, unsigned char slotB);       // 0x00401d20
static void menu_use_set_mode(unsigned char slotA, unsigned char slotB);       // 0x00401d40
static void menu_use_ammo_transfer_a(unsigned char slotA, unsigned char slotB);// 0x00401d50
static void menu_use_ammo_transfer_b(unsigned char slotA, unsigned char slotB);// 0x00401da0
static void menu_item_combine_noop(unsigned char a, unsigned char b); // 0x00401390
static void menu_tab_map(void);                   // 0x00420fd0
static void menu_tab_file(void);                  // 0x00420ff0
static void menu_tab_radio(void);                 // 0x00421010
static void menu_tab_exit(void);                  // 0x00421060
static int  menu_map_state_machine(void);         // 0x00487100
static int  menu_file_state_machine(void);        // 0x00481980
static void map_tab_update(unsigned char* state);       // 0x00487200
static void map_display_update(unsigned char* state);   // 0x00487640

// Menu tab action table (0x004ba1e0), indexed by (cursor position >> 1)
static void (*const g_MenuTabActions[4])(void) = {
    menu_tab_map,    // tab 0: map
    menu_tab_file,   // tab 2: file (save/load)
    menu_tab_radio,  // tab 4: radio
    menu_tab_exit    // tab 6: exit
};

// Item action submenu table (0x004ba1f0), indexed by submenu option
static int (*const g_ItemActionFunctions[3])(void) = {
    menu_item_use_item,     // option 0: use / equip
    menu_item_view_model,   // option 1: view item model
    menu_item_move_item     // option 2: move / combine
};

// Item use dispatch (0x004b1090), indexed by item use category
static int (*const g_ItemUseFunctions[12])(unsigned char) = {
    menu_item_use_item_none,     // 0: weapons (cannot use)
    menu_item_use_item_none,     // 1: ammo (cannot use)
    menu_item_use_item_none,     // 2: empty bottle (cannot use)
    menu_item_use_if_flag,       // 3: chemicals (use if flag set)
    menu_item_use_if_flag,       // 4: special items (use if flag set)
    menu_item_use_if_flag,       // 5: keys (use if flag set)
    menu_item_use_desk_key,      // 6: desk key
    menu_item_use_heal,          // 7: healables (spray, herbs)
    menu_item_use_always,        // 8: always usable (consumed)
    menu_item_use_none_return0   // 9: unusable
};

// Combine effect dispatch (0x004b10b4), indexed by record effect byte
static void (*const g_CombineEffectFunctions[8])(unsigned char, unsigned char) = {
    menu_item_combine_noop,          // 0: no effect
    menu_use_ammo_combine_a,         // 1
    menu_use_ammo_combine_b,         // 2
    menu_use_qty_merge,              // 3
    menu_use_set_flag,               // 4
    menu_use_set_mode,               // 5
    menu_use_ammo_transfer_a,        // 6
    menu_use_ammo_transfer_b         // 7
};

// ============================================================================
// main_menu (0x00463710)
// Main in-game menu: status screen, inventory, map, item box.
// Called as Task_execute(1, main_menu) from check_menus_state.
// ============================================================================
void main_menu(void)
{
    OutputDebugStringA("[MENU] main_menu: entered\n");

    // 0x00463710-0x0046373e: Suspend gameplay task and set up menu environment
    g_bGameActive = 0;
    Task_suspend(0);
    empty_00470a20();
    TexturePage_ClearAll();
    setMenuScreenOffset(320, 240, 4, 0, 0);
    SetSubpixelOffset(112, 76);
    menu_update_fading_rect();

    // 0x0046373e-0x00463753: Initialize sprite animation color
    g_spriteAnimR = 0;
    g_spriteAnimG = 0;
    g_spriteAnimB = 0;

    // 0x00463753-0x00463768: Fade in
    g_fade_type_id = 2;
    g_fading_counter = 0xE800;
    fade_update();

    // 0x00463768-0x0046377d: Check if opening for message display (bit 8)
    if ((g_main_state_flags & 0x100) != 0) {
        g_MainMenuState = 8;
        goto LAB_00463966;
    }

    // 0x0046377d-0x004637ae: Determine initial menu mode
    DAT_00ae9f10 = 5;
    do {
        if ((g_main_state_flags & (0x4000U >> (DAT_00ae9f10 & 0x1f))) != 0) break;
        DAT_00ae9f10 = DAT_00ae9f10 - 1;
    } while (DAT_00ae9f10 != 0);

    // 0x004637ae-0x0046381c: Initialize menu state variables
    DAT_00ae9f23 = 8;
    g_MainMenuState = 0;
    DAT_00ae9f49 = 0;
    DAT_00ae9f4a = 0;
    SUBMENU_STATE_ID = 0;
    DAT_00ae9f1e = 0;

    // 0x004637d4-0x0046381c: Load mode-specific assets
    switch (DAT_00ae9f10) {
    case 1:
        // Use item mode: check if it's a map item
        if (0x32 < g_selectedItemId) {
            DAT_00ae9f10 = 6;
            DAT_00ae9f1b = g_selectedItemId;
            goto LAB_0046381c;
        }
        break;
    case 2:
        // Item box mode: load item box textures
        loadMenuAssets();
        break;
    case 3:
    case 4:
        // Desk mode: get item from room event
        DAT_00ae9f1b = *(unsigned char*)(*(int*)((int)g_room_event_index + 8) + 8);
LAB_0046381c:
        menu_load_item_model();
    }

    // 0x0046381c-0x00463880: Set character display flags
    unsigned char charBits = ((g_playerEntity.id + 1) & 2);
    g_totalInventorySlots = charBits + 6;
    DAT_00ae9f19 = charBits | (g_playerEntity.id & 1);

    // 0x00463880-0x004638e3: Check if map is available
    if ((g_playerEntity.id & 3) == 3) {
        DAT_00ae9f1f = 0;
        int hasFlag = Flg_ck((int)g_PlayerFlags, 0x7f);
        if ((hasFlag == 0) &&
            (hasFlag = Flg_ck((int)g_PlayerFlags3, 0x38), hasFlag != 0) &&
            (hasFlag = Flg_ck((int)g_PlayerFlags3, 0x22), hasFlag == 0))
        {
            DAT_00ae9f1f = 1;
        }
    } else {
        DAT_00ae9f1f = Flg_ck((int)g_PlayerFlags, 0x7f);
    }

    // 0x004638e3-0x00463966: Set up camera and lighting for menu
    DAT_00ae9f12 = 0;
    DAT_00ae9f13 = 0;
    set_title_render_param(0xC0);

    MATRIX_00d22680.m[0][0] = 15000;
    MATRIX_00d22680.m[0][1] = 0;
    MATRIX_00d22680.m[0][2] = 0;
    MATRIX_00d22680.m[1][0] = 0;
    MATRIX_00d22680.m[1][1] = 0;
    MATRIX_00d22680.m[1][2] = 0;
    MATRIX_00d22680.m[2][0] = 0;
    MATRIX_00d22680.m[2][1] = 0;
    MATRIX_00d22680.m[2][2] = 0;
    MATRIX_00d22680.t[0] = 0;
    MATRIX_00d22680.t[1] = 0;
    MATRIX_00d22680.t[2] = 0;
    MatrixToCamera(&MATRIX_00d22680);

    setBackColor(0x199, 0x199, 0x199);
    empty_40ae40(0);

LAB_00463966:
    StMask(0, 1);
    OutputDebugStringA("[MENU] Entering main loop\n");

    // ====================================================================
    // Main menu frame loop
    // ====================================================================
    do {
        Game_timer = Game_timer + 1;

        // 0x00463973-0x0046398a: Handle game reset
        if (g_resetGameFlag != 0) {
            menu_restore_game_state();
            Task_exit();
            return;
        }

        // 0x0046398a-0x004639b5: Set texture descriptor defaults
        g_TextureDesc.colorMulR = 0x80;
        g_TextureDesc.pivotX = 0;
        unk_00be1180 = 0;
        g_TextureDesc.pivotY = 0;
        g_TextureDesc.colorMulG = 0x80;
        g_TextureDesc.colorMulB = 0x80;

        // 0x004639b5: Menu state machine
        switch (g_MainMenuState) {
        case 0:
            // Wait for fade-in to complete, then enter appropriate submenu
            if (-1 < (short)g_fading_state) break;
            switch (DAT_00ae9f10) {
            case 0:
                g_MainMenuState = 1;
                DAT_00ae9f20 = 0;
                menu_init_map_screen();
                menu_init_status_screen();
                break;
            case 1:
                g_MainMenuState = 2;
                set_message_display(0xC4, 0);
                break;
            case 2:
                g_MainMenuState = 3;
                DAT_00ae9f20 = 0;
                break;
            case 3:
            case 4:
                g_MainMenuState = 4;
                goto LAB_00463a53;
            case 5:
                g_MainMenuState = 5;
                menu_init_map_display();
                break;
            case 6:
                g_MainMenuState = 6;
                menu_init_map_screen();
                menu_init_status_screen();
LAB_00463a53:
                DAT_00ae9f49 = 0;
            }
            DAT_00ae9f48 = 0xFF;
            DAT_00ae9f2e = 0;
            break;

        case 1:
            // Status screen + inventory navigation
            {
                // Original: (g_PlayerDpadPressed._1_1_ & 0x80) = REMAPPED cancel
                // (Square/action=0x4000, Cross/cancel=0x8000 in the remapped
                // word). The raw word's byte 1 bit 0x80 is the D-pad LEFT,
                // which must NOT close the menu.
                bool cond1 = (((g_PlayerDpadPressed >> 8) & 0x80) == 0) || (DAT_00ae9f20 != 1);
                bool cond2 = (((g_PlayerPadHeld >> 8) & 8) == 0) || (DAT_00ae9f4a != 0) || (DAT_00ae9f12 != 0);
                if (cond1 && cond2) {
                    menu_handle_input();
                    if ((DAT_00ae9f13 & 0x41) == 0) {
                        if (g_MainMenuState != 7) {
                            menu_draw_health_bar();
                        }
                        if ((DAT_00ae9f13 & 0x41) == 0) break;
                    }
                }
            }
            menu_exit_cleanup();
            play_sfx(3, 5);
            g_menu_choice_id = 0;
            break;

        case 2:
            // Message wait (use item prompt)
            if ((g_menu_choice_id & 0x80) == 0) {
                menu_exit_cleanup();
            } else {
                menu_draw_health_bar();
            }
            break;

        case 3:
            // Item box submenu
            if (menu_itembox_interaction() == 0) {
                menu_draw_health_bar();
            } else {
                menu_exit_cleanup();
            }
            break;

        case 4:
            // 3D model viewer (desk item)
            if (FUN_0044e1b0() == 0) {
                menu_draw_health_bar();
            } else {
                menu_exit_cleanup();
            }
            break;

        case 5:
            // Map screen
            if (menu_update_map_animation() == 0) {
                menu_draw_health_bar();
            } else {
                menu_exit_cleanup();
            }
            break;

        case 6:
            // Item model viewer (map/key items)
            if (FUN_0044e1b0() != 0) {
                DAT_00ae9f10 = 0;
                DAT_00ae9f20 = 0;
                g_MainMenuState = 1;
            }
            menu_draw_health_bar();
            break;

        case 7:
            // Exit animation: restore player state
            if ((DAT_00ae9f1e & 0x80) != 0) {
                ENTITY = (Entity*)&g_playerEntity;
                SetupJointStructures(&g_entityModelBuffer2);
            }

            g_playerEntity.unk_8c = 0;
            g_playerEntity.animation_frame_id = 0;
            g_playerEntity.unk_bf = 0;
            g_playerEntity.isBeingAttackedFlag = 0;

            if ((((DAT_00ae9f13 & 0x80) == 0) || (g_usedItemId < 0x1D)) || (0x1E < g_usedItemId)) {
                g_playerEntity.attackAnim = 0;
                g_playerEntity.animationId = 1;
                g_playerEntity.animFrameId = 0;
                g_playerEntity.action_behavior = 0;
                g_playerEntity.action_state = 0;
                // Joint_move(0, g_playerEntity.animHeader, g_playerEntity.animBase, 0x400);
            } else {
                g_playerEntity.animationId = 8;
                g_playerEntity.animFrameId = 0;
                g_playerEntity.action_behavior = 1;
                g_playerEntity.action_state = 0;
                g_playerEntity.attackAnim = 0x37;
                // Joint_move(0, g_playerEntity.animHeader, g_playerEntity.animBase, 0x400);
            }

            // 0x00463c35: Handle weapon equip/unequip
            if (DAT_00ae9f10 == 0) {
                if (((DAT_00ae9f13 & 0x80) != 0) && (0x1C < g_usedItemId)) {
                    if (g_usedItemId < 0x1F) {
                        DAT_00ae9f1e = 0xFF;
                        g_playerEntity.equippedWeaponId = 0;
                    } else if (g_usedItemId == 0x4D) {
                        g_playerEntity.equippedWeaponId = 0x0B;
                        DAT_00ae9f1e = 0xFF;
                    }
                }
            } else if (DAT_00ae9f10 == 1 || DAT_00ae9f10 > 2) {
                // Skip weapon update
            } else {
                menu_update_equipped_weapon();
            }

            if (DAT_00ae9f1e != 0) {
                // 0x00463c8d: (weaponId, 0xE, g_animationBuffer, g_animObjectBuffer)
                LoadEquippedWeaponAnimation(g_playerEntity.equippedWeaponId, 0xE,
                                            (unsigned int)g_animationBuffer,
                                            (unsigned int)g_animObjectBuffer);
            }

            if ((g_main_state_flags & 1) != 0) {
                FUN_0048c020(0xE);
            }

            // Wait for fade transitions
            while (((g_main_state_flags & 0x20000000) != 0) || (g_spriteAnimActive != 0)) {
                Task_sleep(1);
            }

            menu_restore_game_state();
            Task_exit();
            return;

        case 8:
            // Exit wait (message display mode)
            menu_reset_status_state();
            while (menu_update_status_screen() == 0) {
                Task_sleep(1);
            }
            if (g_spriteAnimActive != 0) {
                Task_sleep(1);
            }
            menu_restore_game_state();
            Task_exit();
            return;
        }

        // 0x00463d33-0x00463d70: Draw inventory and item box
        if ((g_MainMenuState != 7) && (menu_draw_inventory(), DAT_00ae9f10 == 2)) {
                draw_itembox_menu();
        }

        Task_sleep(1);

    } while (true);
}

// ============================================================================
// Stub implementations for menu sub-functions
// These need full decompilation from Ghidra.
// ============================================================================

// (0x00463da0) - Menu exit: fade out, cleanup textures
static void menu_exit_cleanup(void)
{
    FUN_004844b0();
    g_MainMenuState = 7;
    set_fading(2, 0xC00);
    g_spriteAnimR = 0;
    g_spriteAnimG = 0;
    g_spriteAnimB = 0;
    for (int i = 0; i < 3; i++) cleanup_texture_slot(i + 0xC);
    for (int i = 0; i < 8; i++) cleanup_texture_slot(i + 0xF);
    FUN_00470a40();
    FUN_0047d0e0();
}

// (0x00463e10) - Restore game state after menu close
static void menu_restore_game_state(void)
{
    setMenuScreenOffset(320, 240, 0, 0, 0);
    CenterScreenOrigin();
    menu_update_fading_rect();
    RestoreRoomCamera();
    // 0x00463e4a: FUN_0040ac80(i, g_RdtPointer->lights + i). The RDT light array
    // is at +0x0C with a 0x14 stride (Ghidra RDT: LIGHT[3] at offset 12,
    // sizeof(LIGHT) == 20). An earlier revision used base +0x30 and stride 0x2C
    // - the camera record's stride - so restoring the room after a menu close
    // reloaded the D3D lights from the middle of the light array and past its end.
    for (unsigned char i = 3; i > 0; i--) {
        FUN_0040ac80((int)(i - 1), &g_RdtPointer->lights[i - 1]);
    }
    // ambient_light is three shorts; do not narrow to a byte (see cmd_light_set)
    setBackColor((unsigned short)g_RdtPointer->ambient_light_r,
                 (unsigned short)g_RdtPointer->ambient_light_g,
                 (unsigned short)g_RdtPointer->ambient_light_b);
    empty_40ae40(0);
    g_main_state_flags &= 0xFFFF00FF;
    g_bGameActive = 2;
    Task_Resume(0);
    StMask(0, 2);
}

// (0x00463ec0) - Update equipped weapon based on menu selection
void menu_update_equipped_weapon(void)
{
    if (g_EquippedItemId == 0) {
        if (g_playerEntity.equippedWeaponId != 0) {
            g_playerEntity.equippedWeaponId = 0;
            DAT_00ae9f1e = 0xFF;
        }
    } else {
        unsigned char* slotPtr = (unsigned char*)g_ItemSlotsPointer;
        unsigned char itemId = slotPtr[(g_EquippedItemId - 1) * 2];
        if (g_playerEntity.equippedWeaponId != itemId) {
            g_playerEntity.equippedWeaponId = itemId;
            LoadSoundBank(itemId, &g_TimImageBuffer__bitmap);
            DAT_00ae9f1e = 0xFF;
        }
    }
}

// (0x00429cb0) - Update fading rect based on screen offset
static void menu_update_fading_rect(void)
{
    extern RectDrawDesc g_FadingRect;
    if (g_ScreenOffsetX == 160) {
        g_FadingRect.x = -160;
        g_FadingRect.y = -120;
    } else {
        g_FadingRect.x = 0;
        g_FadingRect.y = 0;
    }
}

// (0x00463f20) - Draw inventory HUD (items, equipped weapon, character portrait)
// 0x00463f20
static void menu_draw_inventory(void)
{
    unsigned char* pbVar2;
    unsigned short* puVar3;
    int iVar5;
    unsigned char bVar6;
    unsigned int uVar7;
    unsigned short uVar8;
    unsigned short uVar9;
    int uVar10;
    int uVar11;
    unsigned char local_2;
    char local_1;

    // 0x00463f20-0x00463f80: Draw equipped weapon or no equipped empty slot
    g_TextureDesc.flags = 0x01000040;
    g_TextureDesc.unk10 = 0;
    g_TextureDesc.depth = 0x1c;
    g_TextureDesc.screenX = 0xa0;
    g_TextureDesc.screenY = 0x92;
    g_TextureDesc.width = 0x28;
    g_TextureDesc.height = 0x1e;
    g_invDepthLayer = 10;

    if (g_EquippedItemId == 0) {
        uVar11 = 1;
        uVar10 = 10;
        g_TextureDesc.texU = 0;
        g_TextureDesc.printClutTint = 0x1e0;
        g_TextureDesc.texV = 0;
    } else {
        g_TextureDesc.texU = 0x58;
        g_TextureDesc.printClutTint = 0x1e4;
        g_TextureDesc.depth = 0x1d;
        g_TextureDesc.texV = DAT_00d21ccf[g_EquippedItemId] << 5;
        pbVar2 = (unsigned char*)ITEM_SLOTS + (g_EquippedItemId * 2 - 2);
        if (*pbVar2 < 0x6f) {
            uVar11 = 8;
            uVar10 = 1;
        } else {
            g_TextureDesc.texU = 0;
            uVar11 = 1;
            uVar10 = 0x1e;
            g_TextureDesc.printClutTint = 0x1e0;
            g_TextureDesc.depth = 0;
            g_TextureDesc.texV = *pbVar2 * 0x1e - 2;
        }
    }
    display_texture(&g_TextureDesc, 10, uVar10, uVar11);

    // 0x00464050: Draw equipped item quantity
    g_invDepthLayer = 9;
    if (g_EquippedItemId != 0) {
        iVar5 = (int)((unsigned int)g_EquippedItemId * 2 + (unsigned int)(uintptr_t)ITEM_SLOTS);
        g_TextureDesc.depth = 0x1c;
        display_item_qty(*(unsigned char*)(iVar5 - 2), *(unsigned char*)(iVar5 - 1), 9);
    }

    // 0x00464090: Calculate starting position index for inventory items
    local_2 = g_TotalHeldItems * 2 + 8;
    if ((DAT_00ae9f19 & 2) == 0) {
        local_2 = g_TotalHeldItems * 2 + 0xc;
    }

    // 0x004640d0: Draw each held inventory item
    g_invDepthLayer = 9;
    bVar6 = g_TotalHeldItems;
    while (bVar6 != 0) {
        g_TextureDesc.depth = 0x1d;
        g_TextureDesc.width = 40;
        bVar6 = bVar6 - 1;
        g_TextureDesc.height = 30;
        uVar7 = (unsigned int)bVar6;
        g_TextureDesc.printClutTint = 0x1e4;
        g_TextureDesc.texU = 88;
        g_TextureDesc.screenY = *(short*)((int)g_inventorySlotsPos + (unsigned int)(unsigned char)(local_2 - 1) * 2);
        local_2 = local_2 - 2;
        g_TextureDesc.screenX = *(short*)((int)g_inventorySlotsPos + (unsigned int)local_2 * 2);
        g_TextureDesc.texV = g_ItemSlotsIndexes[uVar7] << 5;
        pbVar2 = (unsigned char*)ITEM_SLOTS + uVar7 * 2;
        if (*pbVar2 < 0x6f) {
            uVar11 = 8;
            uVar10 = 1;
        } else {
            uVar11 = 1;
            g_TextureDesc.texU = 0;
            uVar10 = 0x1e;
            g_TextureDesc.depth = 0;
            g_TextureDesc.printClutTint = 0x1e0;
            g_TextureDesc.texV = *pbVar2 * 0x1e - 2;
        }
        display_texture(&g_TextureDesc, (unsigned short)g_invDepthLayer + 1, uVar10, uVar11);

        g_TextureDesc.depth = 0x1c;
        pbVar2 = (unsigned char*)ITEM_SLOTS + uVar7 * 2;
        display_item_qty(*pbVar2, *(pbVar2 + 1), g_invDepthLayer);
    }

    // 0x00464200: Draw empty inventory slots
    g_TextureDesc.texU = 0;
    g_TextureDesc.texV = 0;
    bVar6 = 24;
    g_TextureDesc.width = 40;
    g_invDepthLayer = 10;
    g_TextureDesc.height = 30;
    g_TextureDesc.printClutTint = 0x1e0;
    for (char cVar4 = g_totalInventorySlots - g_TotalHeldItems; cVar4 != 0; cVar4 = cVar4 - 1) {
        g_TextureDesc.screenY = *(short*)((int)g_inventorySlotsPos + (unsigned int)(unsigned char)(bVar6 - 1) * 2);
        bVar6 = bVar6 - 2;
        g_TextureDesc.screenX = *(short*)((int)g_inventorySlotsPos + (unsigned int)bVar6 * 2);
        display_texture(&g_TextureDesc, (unsigned short)g_invDepthLayer, 10, 1);
    }

    // 0x00464290: Draw character portrait
    g_TextureDesc.screenX = 0x16;
    g_TextureDesc.screenY = 0x92;
    g_TextureDesc.width = 30;
    g_TextureDesc.height = 30;
    g_TextureDesc.printClutTint = 0x1e0;
    g_invDepthLayer = 10;
    g_TextureDesc.texU = (g_playerEntity.id & 1) << 5;
    g_TextureDesc.texV = (g_playerEntity.id & 2) << 4;
    bVar6 = 8;
    display_texture(&g_TextureDesc, 10, 9, 1);

    // 0x00464310: Draw top options menu (Map, File, Radio, Exit)
    g_CurrentMenuFramesDataPtr = (unsigned short*)g_MainMenuTopOptionsPos;
    g_invDepthLayer = 20;
    g_TextureDesc.printClutTint = 0x1e4;
    do {
        bVar6 = bVar6 - 2;
        load_main_menu_frame_part_tex_area();
        g_TextureDesc.flags = 0x1000000;
        if (DAT_00ae9f10 == 2) {
            if ((DAT_00ae9f23 | 2) == bVar6) {
LAB_004642e7:
                g_TextureDesc.flags = 0x01000040;
            }
        } else if (((g_MainMenuState == 1) && (DAT_00ae9f23 == bVar6)) && (DAT_00ae9f20 < 2)) {
            goto LAB_004642e7;
        }
        if ((bVar6 == 4) && (DAT_00ae9f1f == 0)) {
            g_TextureDesc.texV = 0x40;
        }
        display_texture(&g_TextureDesc, (unsigned short)g_invDepthLayer, 0, 1);

        // 0x00464370: Draw frames
        if (bVar6 == 0) {
            g_TextureDesc.flags = 0x01000040;

            // main frame (left-top-right borders)
            // Health frame
            // equipped weapon frame
            // bottom frames
            // options menu top and bottom borders
            // inventory outer frame bottom border
            g_CurrentMenuFramesDataPtr = (unsigned short*)g_MainMenuFrames2Pos;
            g_invDepthLayer = 0x15;
            do {
                load_main_menu_frame_part_tex_area();
                display_texture(&g_TextureDesc, (unsigned short)g_invDepthLayer, 0, 1);
            } while ((unsigned short*)g_MainMenuFramesPos < g_CurrentMenuFramesDataPtr);

            // Main frame bottom border
            // Inventory bottom inner border
            // Top options menu left and right borders
            g_CurrentMenuFramesDataPtr = (unsigned short*)g_MainMenuFrames3Pos;
            g_invDepthLayer = 10;
            do {
                puVar3 = g_CurrentMenuFramesDataPtr;
                g_CurrentMenuFramesDataPtr = g_CurrentMenuFramesDataPtr - 1;
                g_TextureDesc.flags = (*g_CurrentMenuFramesDataPtr & 0xc0) << 0x10 | 0x1000040;
                bVar6 = *(unsigned char*)((int)puVar3 - 1);
                load_main_menu_frame_part_tex_area();
                if ((bVar6 & 0x80) == 0) {
                    uVar8 = 0;
                    uVar9 = g_TextureDesc.width;
                } else {
                    uVar8 = g_TextureDesc.height;
                    uVar9 = 0;
                }
                bVar6 = bVar6 & 0x7f;
                do {
                    display_texture(&g_TextureDesc, (unsigned short)bVar6 + (unsigned short)g_invDepthLayer, 0, 1);
                    g_TextureDesc.screenX = g_TextureDesc.screenX + uVar9;
                    g_TextureDesc.screenY = g_TextureDesc.screenY + uVar8;
                    bVar6 = bVar6 - 1;
                } while (bVar6 != 0);
            } while ((unsigned short*)g_MainMenuTopOptionsPos < g_CurrentMenuFramesDataPtr);

            // Invntory top, left and right borders
            g_CurrentMenuFramesDataPtr = (unsigned short*)g_MainMenuFrames4Pos;
            if ((DAT_00ae9f19 & 2) == 0) {
                g_CurrentMenuFramesDataPtr = (unsigned short*)g_MainMenuFrames3Pos;
            }
            g_CurrentMenuFramesDataPtr = (unsigned short*)((int)g_CurrentMenuFramesDataPtr + 0x7e);
            local_1 = 9;
            g_invDepthLayer = 0x14;
            do {
                puVar3 = g_CurrentMenuFramesDataPtr;
                g_CurrentMenuFramesDataPtr = g_CurrentMenuFramesDataPtr - 1;
                local_1 = local_1 - 1;
                g_TextureDesc.flags = (*g_CurrentMenuFramesDataPtr & 0xc0) << 0x10 | 0x1000040;
                bVar6 = *(unsigned char*)((int)puVar3 - 1);
                load_main_menu_frame_part_tex_area();
                if ((bVar6 & 0x80) == 0) {
                    uVar8 = 0;
                    uVar9 = g_TextureDesc.width;
                } else {
                    uVar8 = g_TextureDesc.height;
                    uVar9 = 0;
                }
                bVar6 = bVar6 & 0x7f;
                do {
                    display_texture(&g_TextureDesc, (unsigned short)bVar6 + (unsigned short)g_invDepthLayer, 0, 1);
                    g_TextureDesc.screenX = g_TextureDesc.screenX + uVar9;
                    g_TextureDesc.screenY = g_TextureDesc.screenY + uVar8;
                    bVar6 = bVar6 - 1;
                } while (bVar6 != 0);
            } while (local_1 != 0);

            // 0x00464510: Draw background black rect
            g_CurrentMenuFramesDataPtr = (unsigned short*)g_inventorySlotsPos;
            g_rect.textureId = 0;
            g_rect.r = 0;
            g_rect.g = 0;
            g_invDepthLayer = 0x1e;
            g_rect.b = 0;
            do {
                g_rect.h = *(short*)((int)g_CurrentMenuFramesDataPtr - 2);
                g_rect.w = *(short*)((int)g_CurrentMenuFramesDataPtr - 4);
                g_rect.y = *(short*)((int)g_CurrentMenuFramesDataPtr - 6);
                g_rect.x = *(short*)((int)g_CurrentMenuFramesDataPtr - 8);
                draw_rect(&g_rect, (unsigned short)g_invDepthLayer, 1);
                g_CurrentMenuFramesDataPtr = (unsigned short*)((int)g_CurrentMenuFramesDataPtr - 8);
            } while ((unsigned short*)DAT_004c2940 < g_CurrentMenuFramesDataPtr);
            return;
        }
    } while (true);
}

// ============================================================================
// display_item_qty (0x004645b0)
// Draw item quantity text (or infinite symbol for special items).
// Reads g_TextureDesc for screenX/Y and modifies it for subsequent draws.
// ============================================================================
static void display_item_qty(unsigned char itemId, unsigned char qty, int depth)
{
    // Only draw for quantifiable items:
    //   Weapons and ammo (2-18, excluding knife), ink ribbon (0x2F),
    //   or high-numbered special items (> ITEM_ID_MAX).
    if (!(((itemId < ITEM_EMPTY_BOTTLE && itemId != ITEM_KNIFE) ||
           itemId == ITEM_INK_RIBBONS) ||
           itemId > ITEM_ID_MAX))
        return;

    g_TextureDesc.printClutTint = 0x1E4;
    g_TextureDesc.screenY = g_TextureDesc.screenY + 0x14;
    g_TextureDesc.height = 8;

    int hasFlag = Flg_ck((int)g_PlayerFlags, 0x7e);

    // Normal numeric quantity display
    // (flag 0x7E grants infinite quantity for certain items like the rocket launcher)
    if (((hasFlag == 0 && itemId <= ITEM_ID_MAX) ||
         (itemId != ITEM_ROCKET_LAUNCHER && itemId <= ITEM_ID_MAX)))
    {
        // Clip quantity display for certain weapon types
        if (itemId != ITEM_FLAMETHROWER && itemId < ITEM_CLIP)
            qty = qty & 0x7F;

        sprintf(PRINT_TEXT_BUFFER, "%03d", (unsigned int)qty);

        // Position text relative to the item icon
        if (itemId < ITEM_CLIP)
            g_TextureDesc.screenX = g_TextureDesc.screenX + 4;
        else
            g_TextureDesc.screenX = g_TextureDesc.screenX + 0xE;

        g_TextureDesc.width = 8;

        // Select digit font column based on item type.
        // Different columns in STATUS.tim hold different glyph variants.
        switch (itemId)
        {
        case ITEM_COLT_PYTHON_DUM:
        case ITEM_BAZOOKA_ACID:
        case ITEM_DUM_DUM_ROUNDS:
        case ITEM_ACID_ROUNDS:
            g_TextureDesc.texU = 0x88;
            break;
        case ITEM_BAZOOKA_FLAME:
        case ITEM_FLAME_ROUNDS:
            g_TextureDesc.texU = 0xC0;
            break;
        default:
            g_TextureDesc.texU = 0x80;
            break;
        }

        char* pch = PRINT_TEXT_BUFFER;
        bool drawnAny = false;
        int remaining = 3;

        do {
            remaining--;
            unsigned char digit = (unsigned char)(*pch & 0xF);  // ASCII '0'-'9' → 0-9

            // Draw digit if: non-zero, OR already drawn one, OR it's the last position
            if (digit != 0 || drawnAny || remaining == 0)
            {
                g_TextureDesc.texV = digit << 3;  // digit × 8 pixels
                display_texture(&g_TextureDesc, (unsigned short)depth, 0, 1);
                drawnAny = true;
            }

            // Advance cursor: always for items beyond weapons (> ITEM_ROCKET_LAUNCHER),
            // otherwise only after first drawn digit (suppresses leading-zero spacing)
            if (drawnAny || itemId > ITEM_ROCKET_LAUNCHER)
                g_TextureDesc.screenX = g_TextureDesc.screenX + 8;

            pch++;
        } while (remaining != 0);
    }
    else
    {
        // Draw infinite (∞) symbol for unlimited-quantity items
        g_TextureDesc.texU = 152;
        g_TextureDesc.texV = 112;
        g_TextureDesc.screenX = g_TextureDesc.screenX + 6;
        g_TextureDesc.width = 10;
        display_texture(&g_TextureDesc, (unsigned short)depth, 0, 1);
    }
}

// ============================================================================
// Menu input, cursor and item action functions
// ============================================================================

// ============================================================================
// Map state block (0x00ac9890 - 0x00ac98a7)
// Shared by the map tab browser (menu_map_state_machine) and the full map
// display (main menu mode 5). Byte layout:
//   +0   state (0-8 open/display/close cycle)
//   +1   sub-state of map_tab_update / map_display_update (0 = init, 1 = run)
//   +2   "you are here" dot blink animation state (0-2)
//   +3   map zoom animation state (0-9)              [DAT_00ac9893]
//   +4   current map layout (0-3)
//   +5   area group (stageId % 5, or 5 for the lab map display)
//   +6   current area id (0-10)
//   +7   current room id (0-0x1f)
//   +8..+0xb  room index per layout (the room whose area matches +6)
//   +0xc move flags (bit 0 = next room exists, bit 1 = previous room)
//   +0xd layout availability bitmask
//   +0xe floor-transition arrow animation counter
//   +0xf map mode/variant (0/1/2/3/4)                [DAT_00ac989f]
//   +0x10 area highlight animation state
//   +0x11 highlight frame counter
//   +0x12 display cycle countdown                    [DAT_00ac98a2]
//   +0x14 map exit flag                              [DAT_00ac98a4]
//   +0x15 highlight frame
//   +0x16 ushort: area-known bitmask (11 areas)      [DAT_00ac98a6]
// ============================================================================
static unsigned char DAT_00ac9890[0x18];            // 0x00ac9890
#define MAP_STATE       (DAT_00ac9890[0x00])
#define MAP_SUBSTATE    (DAT_00ac9890[0x01])
#define MAP_BLINK_STATE (DAT_00ac9890[0x02])
#define MAP_ZOOM_STATE  (DAT_00ac9890[0x03])        // DAT_00ac9893
#define MAP_LAYOUT      (DAT_00ac9890[0x04])        // DAT_00ac9894
#define MAP_GROUP       (DAT_00ac9890[0x05])        // DAT_00ac9895
#define MAP_AREA        (DAT_00ac9890[0x06])        // DAT_00ac9896
#define MAP_ROOM        (DAT_00ac9890[0x07])        // DAT_00ac9897
#define MAP_ROOM_IDX    (&DAT_00ac9890[0x08])       // DAT_00ac9898..0x9b (4)
#define MAP_MOVE_FLAGS  (DAT_00ac9890[0x0c])        // DAT_00ac989c
#define MAP_LAYOUT_MASK (DAT_00ac9890[0x0d])        // DAT_00ac989d
#define MAP_ARROW_ANIM  (DAT_00ac9890[0x0e])        // DAT_00ac989e
#define MAP_MODE        (DAT_00ac9890[0x0f])        // DAT_00ac989f
#define MAP_HL_STATE    (DAT_00ac9890[0x10])        // DAT_00ac98a0
#define MAP_HL_COUNT    (DAT_00ac9890[0x11])        // DAT_00ac98a1
#define MAP_TIMER       (DAT_00ac9890[0x12])        // DAT_00ac98a2
#define MAP_EXIT_FLAG   (DAT_00ac9890[0x14])        // DAT_00ac98a4
#define MAP_HL_FRAME    (DAT_00ac9890[0x15])        // DAT_00ac98a5
#define MAP_AREA_MASK   (*(unsigned short*)&DAT_00ac9890[0x16])  // DAT_00ac98a6

// Map display position values (0x00ac98a8 - 0x00ac98af, 4 shorts).
// Written by the map display functions; nothing in the original reads them
// back (dead stores kept for fidelity).
static short DAT_00ac98a8[4];                       // 0x00ac98a8

// Scratch copy of the zoom sprite used for the flashing "you are here" dot
// (0x00ac3668, 36 bytes). The dot's color multiplier (byte +0x14) is
// overwritten every frame with the blink phase.
static unsigned int DAT_00ac3668[9];                // 0x00ac3668

// "You are here" dot blink phase (0x00ac368c - 0x00ac3694)
static unsigned char  DAT_00ac368c;                 // 0x00ac368c
static unsigned short DAT_00ac3690;                 // 0x00ac3690
static unsigned char  DAT_00ac3694;                 // 0x00ac3694

// File tab (save/load dialog) state block (0x00ac98b0)
static unsigned char DAT_00ac98b0[0x20]; // 0x00ac98b0
#define FILE_STATE      DAT_00ac98b0[0x00]
#define FILE_SUBSTATE   DAT_00ac98b0[0x01]
#define FILE_REQUEST    DAT_00ac98b0[0x02]
#define FILE_MODE       DAT_00ac98b0[0x03]
#define FILE_SLOT       DAT_00ac98b0[0x05]
#define FILE_EXITFLAG   DAT_00ac98b0[0x08]
#define FILE_BUSY       DAT_00ac98b0[0x09]
#define FILE_SAVEFLAG   DAT_00ac98b0[0x10]

// (0x00420b80) - Update selected item id from the slot under the cursor
static void menu_update_selected_item(void)
{
    unsigned char bVar1 = (DAT_00ae9f23 >> 1) - 4;
    if (bVar1 < g_TotalHeldItems) {
        DAT_00ae9f1b = *(unsigned char*)(ITEM_SLOTS + (unsigned int)bVar1 * 2);
        return;
    }
    DAT_00ae9f1b = 0;
}

// (0x00401050) - Equip / unequip the item under the cursor
static void menu_item_toggle_equip(void)
{
    char cVar1 = (char)((DAT_00ae9f23 >> 1) - 3);
    if (cVar1 != g_EquippedItemId) {
        g_EquippedItemId = cVar1;
        return;
    }
    g_EquippedItemId = 0;
}

// (0x00401180 / 0x00401190 / 0x00401390) - Item cannot be used
static int menu_item_use_item_none(unsigned char slot) { return 0; }
static int menu_item_use_none_return0(unsigned char slot) { return 0; }
static void menu_item_combine_noop(unsigned char a, unsigned char b) { }

// (0x00401380) - Item always usable (consumed)
static int menu_item_use_always(unsigned char slot) { return 1; }

// (0x004011a0 / 0x004011e0) - Use item if its flag in DAT_00d213a0 is set
static int menu_item_use_if_flag(unsigned char slot)
{
    if (Flg_ck((int)DAT_00d213a0, (unsigned int)(DAT_00ae9f1b - 0x1b)) != 0) {
        DAT_00ae9f13 = DAT_00ae9f13 | 0x80;
        g_usedItemId = DAT_00ae9f1b;
        return 1;
    }
    return 0;
}

// (0x00401220) - Desk key use check
static int menu_item_use_desk_key(unsigned char slot)
{
    if (DAT_00ae9f1b == 0x3e) {
        if (Flg_ck((int)DAT_00d213a0, 0x23) != 0) {
            DAT_00ae9f13 = DAT_00ae9f13 | 0x80;
            g_usedItemId = DAT_00ae9f1b;
            return 1;
        }
    }
    return 0;
}

// (0x00401260) - Heal / status-cure item use (spray, herbs, serum)
static int menu_item_use_heal(unsigned char slot)
{
    unsigned char bVar1;
    unsigned short uVar2;
    int used = 0;

    bVar1 = g_ItemHealTable[DAT_00ae9f1b];
    if (bVar1 != 0) {
        used = 0;
        if ((bVar1 & 0xf) != 0) {
            unsigned short maxHealth = (unsigned short)g_playerEntity.maxHealth;
            if ((int)g_playerEntity.health < (int)maxHealth) {
                used = 1;
                DAT_00ae9f13 = DAT_00ae9f13 | 0x20;
                unsigned char amount = bVar1 & 0xf;
                if (amount == 1) {
                    g_playerEntity.health = g_playerEntity.health + g_playerEntity.maxHealth / 3;
                    uVar2 = g_playerEntity.health;
                } else if (amount == 2) {
                    g_playerEntity.health = g_playerEntity.health + (short)(((unsigned long long)maxHealth * 2) / 3);
                    uVar2 = g_playerEntity.health;
                } else {
                    uVar2 = maxHealth;
                    if (amount != 3) {
                        uVar2 = g_playerEntity.health;
                    }
                }
                g_playerEntity.health = uVar2;
                if ((int)maxHealth < (int)g_playerEntity.health) {
                    g_playerEntity.health = maxHealth;
                }
            }
        }
        if (((bVar1 & 0xf0) != 0) && ((g_playerEntity.healthStatusFlags & 0x22) != 0)) {
            if ((bVar1 & 0xf0) == 0x10) {
                if ((g_playerEntity.healthStatusFlags & 0x20) != 0) {
                    FUN_00473f10((int*)g_PlayerFlags3, 0x43);
                    g_playerEntity.healthStatusFlags = g_playerEntity.healthStatusFlags & 0xdf;
                }
            } else {
                if (((bVar1 & 0xf0) != 0x20) || ((g_playerEntity.healthStatusFlags & 2) == 0)) {
                    goto heal_done;
                }
                g_playerEntity.healthStatusFlags = g_playerEntity.healthStatusFlags & 0xfd;
            }
            DAT_00ae9f13 = DAT_00ae9f13 | 0x10;
            used = 1;
        }
    }
heal_done:
    if (used != 0) {
        DAT_00ae9f2e = 2;
        DAT_00ae9f12 = 0xff;
    }
    return used;
}

// (0x00401bf0) - Combine effect 1: transfer ammo quantity (cursor slot wins)
static void menu_use_ammo_combine_a(unsigned char slotA, unsigned char slotB)
{
    unsigned char* pbVar4 = (unsigned char*)(ITEM_SLOTS + (unsigned int)slotA * 2);
    unsigned char* pbVar1 = pbVar4 + 1;
    unsigned char* pbOther = (unsigned char*)(ITEM_SLOTS + 1 + (unsigned int)slotB * 2);
    unsigned short uVar6 = (unsigned short)*pbOther + (*pbVar1 & 0x7f);
    unsigned char bVar2 = g_ItemMaxQty[(unsigned int)*pbVar4 * 4];
    unsigned char bVar5 = (unsigned char)uVar6;
    if (bVar2 < uVar6) {
        *pbVar1 = bVar2;
        *pbOther = (unsigned char)(bVar5 - bVar2);
        return;
    }
    *pbVar1 = bVar5;
    *(unsigned char*)(ITEM_SLOTS + (unsigned int)slotB * 2) = 0;
}

// (0x00401c60) - Combine effect 2: transfer ammo quantity (other slot wins)
static void menu_use_ammo_combine_b(unsigned char slotA, unsigned char slotB)
{
    unsigned char* pbVar4 = (unsigned char*)(ITEM_SLOTS + (unsigned int)slotB * 2);
    unsigned char* pbVar1 = pbVar4 + 1;
    unsigned char* pbOther = (unsigned char*)(ITEM_SLOTS + 1 + (unsigned int)slotA * 2);
    unsigned short uVar6 = (unsigned short)*pbOther + (*pbVar1 & 0x7f);
    unsigned char bVar2 = g_ItemMaxQty[(unsigned int)*pbVar4 * 4];
    unsigned char bVar5 = (unsigned char)uVar6;
    if (bVar2 < uVar6) {
        *pbVar1 = bVar2;
        *pbOther = (unsigned char)(bVar5 - bVar2);
        return;
    }
    *pbVar1 = bVar5;
    *(unsigned char*)(ITEM_SLOTS + (unsigned int)slotA * 2) = 0;
}

// (0x00401cd0) - Combine effect 3: merge quantities (cap 0xFA)
static void menu_use_qty_merge(unsigned char slotA, unsigned char slotB)
{
    unsigned char* pbVar2 = (unsigned char*)(ITEM_SLOTS + 1 + (unsigned int)slotA * 2);
    unsigned short uVar4 = (unsigned short)*(unsigned char*)(ITEM_SLOTS + 1 + (unsigned int)slotB * 2)
                           + (unsigned short)*pbVar2;
    unsigned char bVar3 = (unsigned char)uVar4;
    if (0xfa < uVar4) {
        *pbVar2 = 0xfa;
        *(unsigned char*)(ITEM_SLOTS + 1 + (unsigned int)slotB * 2) = (unsigned char)(bVar3 + 6);
        return;
    }
    *pbVar2 = bVar3;
    *(unsigned char*)(ITEM_SLOTS + (unsigned int)slotB * 2) = 0;
}

// (0x00401d20) - Combine effect 4: set chemical flag
static void menu_use_set_flag(unsigned char slotA, unsigned char slotB)
{
    DAT_00ae9f12 = 2;
    Flg_on((int)g_PlayerFlags, 0x16);
}

// (0x00401d40) - Combine effect 5: set mode
static void menu_use_set_mode(unsigned char slotA, unsigned char slotB)
{
    DAT_00ae9f12 = 3;
}

// (0x00401d50) - Combine effect 6: ammo transfer A
static void menu_use_ammo_transfer_a(unsigned char slotA, unsigned char slotB)
{
    unsigned char bVar4 = *(unsigned char*)(ITEM_SLOTS + 1 + (unsigned int)slotB * 2);
    unsigned char* pbVar1 = (unsigned char*)(ITEM_SLOTS + (unsigned int)slotA * 2);
    unsigned char bVar5 = g_ItemMaxQty[(unsigned int)*pbVar1 * 4];
    if (bVar5 < bVar4) {
        pbVar1[1] = bVar5;
        *(unsigned char*)(ITEM_SLOTS + 1 + (unsigned int)slotB * 2) =
            *(unsigned char*)(ITEM_SLOTS + 1 + (unsigned int)slotB * 2) - bVar5;
        return;
    }
    pbVar1[1] = bVar4;
    *(unsigned char*)(ITEM_SLOTS + (unsigned int)slotB * 2) = 0;
}

// (0x00401da0) - Combine effect 7: ammo transfer B
static void menu_use_ammo_transfer_b(unsigned char slotA, unsigned char slotB)
{
    unsigned char bVar4 = *(unsigned char*)(ITEM_SLOTS + 1 + (unsigned int)slotA * 2);
    unsigned char* pbVar1 = (unsigned char*)(ITEM_SLOTS + (unsigned int)slotB * 2);
    unsigned char bVar5 = g_ItemMaxQty[(unsigned int)*pbVar1 * 4];
    if (bVar5 < bVar4) {
        pbVar1[1] = bVar5;
        *(unsigned char*)(ITEM_SLOTS + 1 + (unsigned int)slotA * 2) =
            *(unsigned char*)(ITEM_SLOTS + 1 + (unsigned int)slotA * 2) - bVar5;
        return;
    }
    pbVar1[1] = bVar4;
    *(unsigned char*)(ITEM_SLOTS + (unsigned int)slotA * 2) = 0;
}

// (0x00401df0) - Refresh item images after a combine
static void menu_item_combine_refresh(unsigned char slot1, unsigned char slot2,
                                      unsigned char new1, unsigned char new2)
{
    unsigned char bVar1;
    unsigned char refresh1 = 0;
    unsigned char refresh2 = 0;

    bVar1 = *(unsigned char*)(ITEM_SLOTS + (unsigned int)slot1 * 2);
    if ((bVar1 != 0) && (bVar1 != new1)) {
        refresh1 = g_ItemImageTypeTable[g_ItemImageLookupTable[(unsigned int)bVar1 * 4 + 1]];
    }
    bVar1 = *(unsigned char*)(ITEM_SLOTS + (unsigned int)slot2 * 2);
    if ((bVar1 != 0) && (bVar1 != new2)) {
        refresh2 = g_ItemImageTypeTable[g_ItemImageLookupTable[(unsigned int)bVar1 * 4 + 1]];
    }
    if (((((char)DAT_00ae9f1e + 1) & 0x7f) != 0) && ((refresh2 != 0) || (refresh1 != 0))) {
        DAT_00ae9f1e = DAT_00ae9f1e | 0x7f;
        LoadFile((char*)g_ItemMixPixPath, g_TimImageBuffer__bitmap, 0x20);
    }
    if (refresh1 != 0) {
        LoadItemImage((int)refresh1 - 1, (int)g_ItemSlotsIndexes[slot1], (int)g_TimImageBuffer__bitmap);
    }
    if (refresh2 != 0) {
        LoadItemImage((int)refresh2 - 1, (int)g_ItemSlotsIndexes[slot2], (int)g_TimImageBuffer__bitmap);
    }
}

// (0x00401960) - Item combine check. Returns:
//   0 = cannot combine, 1 = requires lab room, 2 = herb combo, 3 = herb+herb,
//   4 = combine applied
static int menu_item_check_combine(void)
{
    unsigned char bVar3 = (DAT_00ae9f23 >> 1) - 4;
    unsigned char* pbVar7 = (unsigned char*)(ITEM_SLOTS + (unsigned int)bVar3 * 2);
    unsigned char bVar1 = *pbVar7;
    unsigned char bVar4 = (DAT_00ae9f25 >> 1) - 4;
    unsigned char* pbVar5 = (unsigned char*)(ITEM_SLOTS + (unsigned int)bVar4 * 2);
    unsigned char bVar2 = *pbVar5;

    const unsigned char* pTable = g_ItemCombinePtrs[(char)g_ItemImageLookupTable[(unsigned int)DAT_00ae9f1b * 4 + 1]];
    const unsigned char* pRec = pTable + 1;
    unsigned char cVar6 = *pTable;
    for (; cVar6 != 0; cVar6--) {
        if (*pRec == bVar2) {
            if (5 < pRec[3]) {
                if (pRec[3] == 6) {
                    if (pbVar7[1] != 0) return 0;
                } else if (pbVar5[1] != 0) {
                    return 0;
                }
            }
            play_sfx(3, 6, 0);
            if ((bVar1 < 0x13) || (0x1a < bVar1)) {
                if ((0x42 < bVar1) && (bVar1 < 0x4c)) return 2;
            } else if ((g_roomId != 9) || (g_stageId != 3)) {
                return 1;
            }
            *(unsigned char*)(ITEM_SLOTS + (unsigned int)bVar3 * 2) = pRec[1];
            *(unsigned char*)(ITEM_SLOTS + (unsigned int)bVar4 * 2) = pRec[2];
            if (pRec[3] != 0) {
                g_CombineEffectFunctions[pRec[3] & 7](bVar3, bVar4);
            }
            menu_item_combine_refresh(bVar3, bVar4, pRec[1], pRec[2]);
            rearrange_item_slots();
            return 4;
        }
        pRec += 4;
    }
    if ((((0x42 < bVar1) && (bVar1 < 0x4c)) && (0x42 < bVar2)) && (bVar2 < 0x4c)) {
        return 3;
    }
    return 0;
}

// (0x00401b00) - Apply a confirmed combine (called from the move confirmation)
static void menu_item_apply_combine(void)
{
    unsigned char bVar5;
    unsigned char bVar4;
    unsigned char cVar1;
    unsigned char cVar2;
    const unsigned char* pRec;

    if (g_menu_choice_id == 0) {
        play_sfx(3, 6, 0);
        DAT_00ae9f22 = 3;
        bVar5 = (DAT_00ae9f23 >> 1) - 4;
        unsigned char* pcVar6 = (unsigned char*)((unsigned int)bVar5 * 2 + ITEM_SLOTS);
        DAT_00ae9f13 = DAT_00ae9f13 | 8;
        cVar1 = *pcVar6;
        bVar4 = (DAT_00ae9f25 >> 1) - 4;
        cVar2 = *(unsigned char*)(ITEM_SLOTS + (unsigned int)bVar4 * 2);

        pRec = g_ItemCombinePtrs[(char)g_ItemImageLookupTable[(unsigned int)DAT_00ae9f1b * 4 + 1]] + 1;
        unsigned char cVar3 = *pRec;
        while (cVar3 != cVar2) {
            pRec += 4;
            cVar3 = *pRec;
        }
        *pcVar6 = pRec[1];
        *(unsigned char*)(ITEM_SLOTS + (unsigned int)bVar4 * 2) = pRec[2];
        menu_item_combine_refresh(bVar5, bVar4, cVar1, cVar2);
        rearrange_item_slots();
        menu_update_selected_item();
        return;
    }
    play_sfx(3, 5, 0);
    DAT_00ae9f22 = 2;
}

// (0x00401070) - Item action: use / equip the selected item
static int menu_item_use_item(void)
{
    unsigned char bVar5;
    unsigned char bVar6;
    unsigned char bVar2;

    if (DAT_00ae9f22 != 0) {
        if ((g_menu_choice_id & 0x80) == 0) {
            DAT_00ae9f13 = DAT_00ae9f13 | 4;
        }
        return 0;
    }
    bVar5 = 9;
    bVar6 = (DAT_00ae9f23 >> 1) - 4;
    bVar2 = g_ItemImageLookupTable[0x144];
    while (DAT_00ae9f1b < bVar2) {
        bVar5 = bVar5 - 1;
        bVar2 = g_ItemImageLookupTable[bVar5 + 0x13b];
    }
    if (g_ItemUseFunctions[bVar5](bVar6) == 0) {
        DAT_00ae9f22 = 1;
        set_message_display((unsigned short)(bVar5 + 0xf7), 0);
        DAT_00ae9f13 = DAT_00ae9f13 & 0xfb;
        return 0;
    }
    play_sfx(3, 6, 0);
    unsigned char* pcVar1 = (unsigned char*)(ITEM_SLOTS + 1 + (unsigned int)bVar6 * 2);
    unsigned char cVar3 = *pcVar1;
    if (cVar3 != 0) {
        *pcVar1 = cVar3 - 1;
        if (*(unsigned char*)(ITEM_SLOTS + (unsigned int)bVar6 * 2 + 1) == 0) {
            *(unsigned char*)(ITEM_SLOTS + (unsigned int)bVar6 * 2) = 0;
            rearrange_item_slots();
            menu_update_selected_item();
        }
    }
    DAT_00ae9f13 = DAT_00ae9f13 | 2;
    FUN_00454fd0(DAT_00ae9f1b, 0, 0x30 - g_ScreenOffsetX, 0xba - g_ScreenOffsetY);
    return 0;
}

// (0x004013a0) - Item action: view the selected item's 3D model
static int menu_item_view_model(void)
{
    switch (DAT_00ae9f22) {
    case 0:
        DAT_00ae9f22 = 1;
        menu_load_item_model();
        // fall through
    case 1:
        break;
    case 2:
        goto view_case2;
    case 3:
        goto view_case3;
    default:
        return 0;
    }
    DAT_00ae9f28 = DAT_00ae9f28 + 1;
    if ((DAT_00ae9f28 & 8) == 0) {
        FUN_00454fd0(DAT_00ae9f1b, 0, 0x30 - g_ScreenOffsetX, 0xba - g_ScreenOffsetY);
        return 0;
    }
    DAT_00ae9f22 = 2;
    DAT_00ae9f49 = 0;
view_case2:
    if (FUN_0044e1b0() != 0) {
        DAT_00ae9f22 = 3;
view_case3:
        DAT_00ae9f28 = DAT_00ae9f28 - 1;
        if (DAT_00ae9f28 == 0) {
            DAT_00ae9f13 = DAT_00ae9f13 | 4;
        }
        FUN_00454fd0(DAT_00ae9f1b, 0, 0x30 - g_ScreenOffsetX, 0xba - g_ScreenOffsetY);
    }
    return 0;
}

// (0x00401470) - Item action: move / combine the selected item
static int menu_item_move_item(void)
{
    unsigned char bVar2;
    unsigned char bVar3;

    bVar2 = DAT_00ae9f23;
    if (DAT_00ae9f22 != 0) {
        bVar2 = DAT_00ae9f25;
    }
    bVar2 = (bVar2 >> 1) - 4;

    switch (DAT_00ae9f22) {
    case 0:
        DAT_00ae9f22 = 1;
        DAT_00ae9f26 = 0xff;
        DAT_00ae9f25 = DAT_00ae9f23;
        DAT_00ae9f29 = 0;
        DAT_00ae9f2a = 0;
        break;

    case 1:
        DAT_00ae9f29 = DAT_00ae9f29 + 4;
        DAT_00ae9f2a = DAT_00ae9f2a + 3;
        if (DAT_00ae9f29 == 0x10) {
            DAT_00ae9f22 = 2;
            DAT_00ae9f13 = DAT_00ae9f13 & 0xf7;
            goto move_case2;
        }
        break;

    case 2:
move_case2:
        if ((dpad_pressed_byte1() & 0x80) != 0) {
            DAT_00ae9f22 = 3;
            DAT_00ae9f26 = 0;
            play_sfx(3, 5, 0);
            goto move_case3;
        }
        if ((dpad_pressed_byte1() & 0x40) == 0) {
            if ((pad_held_byte1() & 0xf0) != 0) {
                if ((pad_held_byte1() & 0xa0) != 0) {
                    DAT_00ae9f25 = DAT_00ae9f25 ^ 2;
                }
                if ((pad_held_byte1() & 0x10) == 0) {
                    if (((pad_held_byte1() & 0x40) != 0) &&
                        (DAT_00ae9f25 = DAT_00ae9f25 + 4,
                         (unsigned int)g_totalInventorySlots * 2 + 6 < (unsigned int)DAT_00ae9f25)) {
                        DAT_00ae9f25 = DAT_00ae9f25 & 2;
                        DAT_00ae9f25 = DAT_00ae9f25 | 8;
                    }
                } else {
                    DAT_00ae9f25 = DAT_00ae9f25 - 4;
                    if ((DAT_00ae9f25 & 0xf8) == 0) {
                        DAT_00ae9f25 = DAT_00ae9f25 & 2;
                        DAT_00ae9f25 = DAT_00ae9f25 | g_totalInventorySlots * 2 + 4;
                    }
                }
                DAT_00ae9f26 = 0;
                play_sfx(3, 4, 0);
            }
        } else if (((DAT_00ae9f23 != DAT_00ae9f25) && (bVar2 < g_TotalHeldItems)) &&
                   (-1 < (char)g_ItemImageLookupTable[(unsigned int)DAT_00ae9f1b * 4 + 1])) {
            unsigned char uVar1 = (unsigned char)menu_item_check_combine();
            switch (uVar1) {
            case 1:
                DAT_00ae9f22 = 4;
                DAT_00ae9f12 = 1;
                set_message_display(0xf2, 0);
                break;
            case 2:
                DAT_00ae9f22 = 4;
                DAT_00ae9f12 = 4;
                set_message_display(0xf5, 0);
                break;
            case 3:
                DAT_00ae9f22 = 4;
                DAT_00ae9f12 = 5;
                set_message_display(0xf6, 0);
                break;
            case 4:
                DAT_00ae9f22 = 3;
                DAT_00ae9f13 = DAT_00ae9f13 | 8;
                menu_update_selected_item();
            }
            DAT_00ae9f26 = 0;
            break;
        }
        DAT_00ae9f26 = DAT_00ae9f26 - 1;
        break;

    case 3:
move_case3:
        DAT_00ae9f29 = DAT_00ae9f29 - 4;
        DAT_00ae9f2a = DAT_00ae9f2a - 3;
        if (DAT_00ae9f29 == 0) {
            if (DAT_00ae9f12 == 0) {
                if ((DAT_00ae9f13 & 8) == 0) {
                    DAT_00ae9f13 = DAT_00ae9f13 | 4;
                } else {
                    DAT_00ae9f13 = DAT_00ae9f13 | 2;
                }
            } else {
                DAT_00ae9f22 = 4;
                set_message_display((unsigned short)(DAT_00ae9f12 + 0xf1), 0);
            }
        }
        break;

    case 4:
        if ((g_menu_choice_id & 0x80) == 0) {
            switch (DAT_00ae9f12 - 1) {
            case 0:
                DAT_00ae9f22 = 3;
                break;
            case 1:
                DAT_00ae9f13 = DAT_00ae9f13 | 0x40;
                return 0;
            case 2:
                DAT_00ae9f13 = DAT_00ae9f13 | 2;
                break;
            case 3:
                menu_item_apply_combine();
                break;
            case 4:
                DAT_00ae9f22 = 2;
            }
            DAT_00ae9f12 = 0;
        }
    }

    if (DAT_00ae9f12 == 0) {
        bVar3 = DAT_00ae9f1b;
        if (DAT_00ae9f22 != 3) {
            if (g_TotalHeldItems <= bVar2) goto move_skip_name;
            bVar3 = *(unsigned char*)(ITEM_SLOTS + (unsigned int)bVar2 * 2);
        }
        FUN_00454fd0(bVar3, 0, 0x30 - g_ScreenOffsetX, 0xba - g_ScreenOffsetY);
    }
move_skip_name:
    if ((DAT_00ae9f22 != 4) || (DAT_00ae9f29 != 0)) {
        // Draw the moving item cursor (4-part slide icon)
        g_TextureDesc.flags = 0x01000040;
        g_TextureDesc.texU = 0x6c;
        g_TextureDesc.depth = 0x1c;
        g_TextureDesc.texV = 0x60;
        g_TextureDesc.unk10 = 0;
        g_TextureDesc.printClutTint = 0x1e4;
        if ((DAT_00ae9f26 & 0x20) == 0) {
            g_TextureDesc.texU = 0x74;
        }
        g_TextureDesc.width = 4;
        g_TextureDesc.height = 4;
        g_TextureDesc.screenX = *(short*)((int)g_inventorySlotsPos + DAT_00ae9f25 * 2) + 0x12;
        g_TextureDesc.screenY = (*(short*)((int)g_inventorySlotsPos + DAT_00ae9f25 * 2 + 2) - (unsigned short)DAT_00ae9f2a) + 0xd;
        if ((DAT_00ae9f19 & 2) == 0) {
            g_TextureDesc.screenY = (*(short*)((int)g_inventorySlotsPos + DAT_00ae9f25 * 2 + 2) - (unsigned short)DAT_00ae9f2a) + 0x2b;
        }
        display_texture(&g_TextureDesc, 5, 0, 1);
        g_TextureDesc.texV = g_TextureDesc.texV + 8;
        g_TextureDesc.screenY = g_TextureDesc.screenY + (unsigned short)DAT_00ae9f2a * 2;
        display_texture(&g_TextureDesc, 5, 0, 1);
        g_TextureDesc.screenX = g_TextureDesc.screenX - (unsigned short)DAT_00ae9f29;
        g_TextureDesc.texV = g_TextureDesc.texV + 8;
        g_TextureDesc.screenY = *(short*)((int)g_inventorySlotsPos + DAT_00ae9f25 * 2 + 2) + 0xd;
        if ((DAT_00ae9f19 & 2) == 0) {
            g_TextureDesc.screenY = *(short*)((int)g_inventorySlotsPos + DAT_00ae9f25 * 2 + 2) + 0x2b;
        }
        display_texture(&g_TextureDesc, 5, 0, 1);
        g_TextureDesc.texV = g_TextureDesc.texV + 8;
        g_TextureDesc.screenX = g_TextureDesc.screenX + (unsigned short)DAT_00ae9f29 * 2;
        display_texture(&g_TextureDesc, 5, 0, 1);
    }
    return 0;
}

// (0x00420a70) - Draw the menu cursor (item slot or top tab)
static void menu_draw_cursor(void)
{
    g_TextureDesc.flags = 0x01000040;
    g_TextureDesc.depth = 0x1c;
    g_TextureDesc.unk10 = 0;
    g_TextureDesc.printClutTint = 0x1e4;
    g_TextureDesc.screenX = *(short*)((int)g_inventorySlotsPos + DAT_00ae9f23 * 2);
    g_TextureDesc.screenY = *(short*)((int)g_inventorySlotsPos + DAT_00ae9f23 * 2 + 2);

    if ((DAT_00ae9f23 & 0xf8) != 0) {
        // Item slot cursor (40x30)
        g_TextureDesc.width = 0x28;
        g_TextureDesc.texU = 0x80;
        g_TextureDesc.texV = 0xe0;
        g_TextureDesc.height = 0x1e;
        if ((DAT_00ae9f18 & 0x20) == 0) {
            g_TextureDesc.texU = 0xa8;
        }
        g_rect.x = 0xce;
        if ((DAT_00ae9f19 & 2) == 0) {
            g_TextureDesc.screenY = g_TextureDesc.screenY + 0x1e;
        }
        display_texture(&g_TextureDesc, 5, 0, 1);
        g_TextureDesc.texV = 0x90;
        return;
    }

    // Top tab cursor (48x16)
    g_TextureDesc.width = 0x30;
    g_TextureDesc.texU = 0;
    g_TextureDesc.texV = 0x50;
    g_TextureDesc.height = 0x10;
    if (g_MainMenuState == 3) {
        g_TextureDesc.screenX = *(short*)((int)g_inventorySlotsPos + (DAT_00ae9f23 | 2) * 2);
    }
    display_texture(&g_TextureDesc, 0x19, 0, 1);
    g_TextureDesc.texV = 0x98;
}

// (0x00454fd0) - Draw the item name text at the given position
void FUN_00454fd0(int itemId, int mode, short x, short y)
{
    unsigned char bVar1;
    unsigned char* pbVar2;
    unsigned char* pbVar4;
    unsigned char bVar3;
    unsigned char item = (unsigned char)itemId;
    unsigned char modeB = (unsigned char)mode;

    if (item == 0) return;

    if ((modeB & 0x80) == 0) {
        bVar3 = modeB >> 4;
        if (bVar3 == 0) bVar3 = 2;
    } else {
        bVar3 = 0x1e;
    }

    g_TextureDesc.flags = 0x40;
    g_TextureDesc.height = 0xe;
    g_TextureDesc.unk10 = 0x100;
    g_TextureDesc.printClutTint = (modeB & 0xf) + 0x1e0;
    g_TextureDesc.screenX = x;
    g_TextureDesc.screenY = y;
    g_TextureDesc.width = 8;
    g_TextureDesc.colorMulR = 0x80;
    g_TextureDesc.colorMulG = 0x80;
    g_TextureDesc.colorMulB = 0x80;
    g_TextureDesc.pivotX = 0;
    unk_00be1180 = 0;
    g_TextureDesc.pivotY = 0;

    pbVar2 = message_item_name_lookup(item);
    if (*pbVar2 == 7) return;

    do {
        bVar1 = *pbVar2;
        if (bVar1 == 0xf8) {
            g_TextureDesc.depth = 0x1e;
            pbVar2 = pbVar2 + 1;
            bVar1 = *pbVar2 / 0x12 + 0xf;
        } else if (bVar1 == 0xf9) {
            pbVar2 = pbVar2 + 1;
            g_TextureDesc.depth = 0x1f;
            bVar1 = *pbVar2 / 0x12;
        } else if (bVar1 == 0xfa) {
            pbVar2 = pbVar2 + 1;
            g_TextureDesc.depth = 0x1f;
            bVar1 = *pbVar2 / 0x12 + 0xe;
        } else {
            g_TextureDesc.depth = 0x1e;
            bVar1 = *pbVar2 / 0x12 + 2;
        }
        pbVar4 = pbVar2 + 1;
        g_TextureDesc.texV = bVar1 * 0xe;
        g_TextureDesc.texU = *pbVar2 % 0x12 << 3;
        AddTintSprite(&g_TextureDesc, (unsigned short)bVar3);
        g_TextureDesc.screenX = g_TextureDesc.screenX + 8;
        pbVar2 = pbVar4;
    } while (*pbVar4 != 7);
}

// (0x00420bb0) - Item action submenu: use/view/move options, animation and
// item name box display
static void menu_item_submenu(void)
{
    unsigned char bVar1;
    unsigned short uVar2;
    unsigned char bVar5;

    switch (DAT_00ae9f21) {
    case 0:
        DAT_00ae9f21 = 1;
        DAT_00ae9f27 = 0x80;
        DAT_00ae9f28 = 0;
        // fall through
    case 1:
        DAT_00ae9f27 = DAT_00ae9f27 + 1;
        if ((DAT_00ae9f27 & 8) != 0) {
            DAT_00ae9f21 = 2;
            DAT_00ae9f13 = DAT_00ae9f13 & 0xfd;
        }
        break;

    case 2:
        if ((dpad_pressed_byte1() & 0x80) == 0) {
            FUN_00454fd0(DAT_00ae9f1b, 0, 0x30 - g_ScreenOffsetX, 0xba - g_ScreenOffsetY);
            if ((dpad_pressed_byte1() & 0x40) != 0) {
                if (DAT_00ae9f24 == 0) {
                    if ((DAT_00ae9f1b < 0xb) || (0x6e < DAT_00ae9f1b)) {
                        menu_item_toggle_equip();
                        DAT_00ae9f21 = 3;
                        DAT_00ae9f27 = DAT_00ae9f27 & 0x7f;
                        play_sfx(3, 6, 0);
                    } else {
                        DAT_00ae9f21 = 4;
                        DAT_00ae9f22 = 0;
                    }
                } else if ((DAT_00ae9f24 != 0) && (DAT_00ae9f24 < 3)) {
                    DAT_00ae9f21 = 4;
                    DAT_00ae9f22 = 0;
                    DAT_00ae9f13 = DAT_00ae9f13 & 0xfb;
                    play_sfx(3, 6, 0);
                }
                goto submenu_default;
            }
            if ((pad_held_byte1() & 0x50) != 0) {
                if ((pad_held_byte1() & 0x10) == 0) {
                    if (DAT_00ae9f24 < 2) {
                        DAT_00ae9f24 = DAT_00ae9f24 + 1;
                    }
                } else if (DAT_00ae9f24 != 0) {
                    DAT_00ae9f24 = DAT_00ae9f24 - 1;
                }
                play_sfx(3, 4, 0);
            }
            if ((DAT_00ae9f13 & 2) == 0) goto submenu_default;
            DAT_00ae9f27 = DAT_00ae9f27 & 0x7f;
        }
        DAT_00ae9f21 = 3;
        play_sfx(3, 5, 0);

    case 3:
        DAT_00ae9f27 = DAT_00ae9f27 - 1;
        if ((DAT_00ae9f27 & 0x7f) == 0) {
            DAT_00ae9f20 = 1;
            menu_update_selected_item();
            if ((DAT_00ae9f13 & 0x80) != 0) {
                DAT_00ae9f13 = DAT_00ae9f13 | 1;
                return;
            }
        }
        break;

    case 4:
        g_ItemActionFunctions[DAT_00ae9f24]();
        if ((DAT_00ae9f13 & 0x40) != 0) {
            return;
        }
        if ((DAT_00ae9f13 & 2) == 0) {
            if ((DAT_00ae9f13 & 4) != 0) {
                DAT_00ae9f21 = 2;
            }
        } else {
            DAT_00ae9f21 = 3;
            DAT_00ae9f27 = DAT_00ae9f27 & 0x7f;
        }
        // 0x00420d70 - case 4 falls into `default`, which JUMPS PAST the item
        // name draw below. While an item action owns the screen (the examine
        // description, a use prompt) the 0xba line belongs to the message, so
        // drawing the name here painted it on top of the description text.
    default:
        goto submenu_default;
    }

    FUN_00454fd0(DAT_00ae9f1b, 0, 0x30 - g_ScreenOffsetX, 0xba - g_ScreenOffsetY);
submenu_default:
    // Draw the item action submenu box with the selected option highlighted
    if ((DAT_00ae9f27 != 0) && ((DAT_00ae9f28 & 8) == 0)) {
        g_TextureDesc.flags = 0x01000040;
        g_TextureDesc.depth = 0x1c;
        g_TextureDesc.unk10 = 0;
        g_TextureDesc.printClutTint = 0x1e4;
        g_TextureDesc.screenX = 0x90;
        if (((DAT_00ae9f21 & 1) == 0) && (DAT_00ae9f28 == 0)) {
            g_TextureDesc.texV = 0x60;
            g_TextureDesc.height = 0x18;
            g_TextureDesc.screenY = (unsigned short)DAT_00ae9f24 * 0x18 + 0x39;
            g_TextureDesc.width = 0x30;
            g_TextureDesc.texU = 0x30;
            display_texture(&g_TextureDesc, 0x23, 0, 1);
        }
        g_TextureDesc.screenY = 0x39;
        g_TextureDesc.texU = 0x30;
        g_TextureDesc.texV = 0;
        if (DAT_00ae9f1c == 0) {
            g_TextureDesc.texV = 0x18;
        }
        if ((DAT_00ae9f27 & 0x80) == 0) {
            g_TextureDesc.screenX = g_TextureDesc.screenX + (8 - (unsigned short)DAT_00ae9f27) * 6;
            g_TextureDesc.screenY = (0x1b - (unsigned short)DAT_00ae9f27) * 3;
            g_TextureDesc.texU = (0x10 - DAT_00ae9f27) * 6;
            g_TextureDesc.texV = g_TextureDesc.texV + (8 - DAT_00ae9f27) * 3;
        }
        g_TextureDesc.printClutTint = 0x1e4;
        uVar2 = 0;
        g_TextureDesc.width = (DAT_00ae9f27 & 0x7f) * 6;
        g_TextureDesc.height = (DAT_00ae9f27 & 0x7f) * 3;
        do {
            if (((DAT_00ae9f27 & 0x80) != 0) && (DAT_00ae9f28 != 0)) {
                if ((short)((DAT_00ae9f28 - uVar2) * 8) < 0) {
                    g_TextureDesc.screenX = 0x90;
                } else {
                    g_TextureDesc.screenX = (DAT_00ae9f28 - uVar2) * 8 + 0x90;
                }
            }
            display_texture(&g_TextureDesc, 0x23, 0, 1);
            bVar1 = g_TextureDesc.texV;
            g_TextureDesc.texV = g_TextureDesc.texV + 0x18;
            if (((char)uVar2 == 0) && (DAT_00ae9f1c != 0)) {
                g_TextureDesc.texV = bVar1 + 0x30;
            }
            g_TextureDesc.screenY = g_TextureDesc.screenY + 0x18;
            bVar5 = (unsigned char)uVar2 + 1;
            uVar2 = (unsigned short)bVar5;
        } while (bVar5 < 3);
    }
}

// (0x00420880) - Main menu input: cursor navigation, tab and item selection
static void menu_handle_input(void)
{
    if (DAT_00ae9f20 == 0) {
        DAT_00ae9f20 = 1;
        DAT_00ae9f18 = 0;
        menu_update_selected_item();
    } else if (DAT_00ae9f20 == 2) {
        if ((DAT_00ae9f23 & 0xf8) == 0) {
            // Cursor on a top tab: run the tab action
            g_MenuTabActions[DAT_00ae9f23 >> 1]();
        } else {
            menu_item_submenu();
        }
        if ((DAT_00ae9f13 & 0x41) != 0) {
            return;
        }
        if (g_MainMenuState == 7) {
            return;
        }
        // 0x00420a2b - the submenu path draws the cursor and returns; it does
        // NOT fall into the item-name draw at input_draw. Once a submenu owns
        // the screen the 0xba line belongs to whatever it puts there (the
        // examine description, a use prompt), and menu_item_submenu itself
        // draws the name in the states that still want it.
        menu_draw_cursor();
        return;
    } else if (DAT_00ae9f20 != 1) {
        return;
    }

    if ((dpad_pressed_byte1() & 0x40) == 0) {
        // Navigation with the D-pad (held for repeat)
        if ((pad_held_byte1() & 0xf0) != 0) {
            if ((pad_held_byte1() & 0xa0) != 0) {
                DAT_00ae9f23 = DAT_00ae9f23 ^ 2;
            }
            if ((pad_held_byte1() & 0x10) == 0) {
                if (((pad_held_byte1() & 0x40) != 0) &&
                    (DAT_00ae9f23 = DAT_00ae9f23 + 4,
                     (unsigned int)g_totalInventorySlots * 2 + 6 < (unsigned int)DAT_00ae9f23)) {
                    DAT_00ae9f23 = DAT_00ae9f23 & 2;
                }
            } else if ((DAT_00ae9f23 & 0xfc) == 0) {
                DAT_00ae9f23 = DAT_00ae9f23 + g_totalInventorySlots * 2 + 4;
            } else {
                DAT_00ae9f23 = DAT_00ae9f23 - 4;
            }
            menu_update_selected_item();
            DAT_00ae9f18 = 0;
            play_sfx(3, 4, 0);
            goto input_draw;
        }
        DAT_00ae9f18 = DAT_00ae9f18 - 1;
    } else {
        // Confirm pressed
        if ((DAT_00ae9f23 & 0xf8) == 0) {
            if (DAT_00ae9f23 == 4) {
                // Radio tab pre-check: without the radio item, show it as a model
                if (DAT_00ae9f1f == 0) goto input_blink;
                if (Flg_ck((int)DAT_00d213a0, 0x3f) == 0) {
                    DAT_00ae9f49 = 0;
                    DAT_00ae9f1b = 0x4d;
                    menu_load_item_model();
                }
            }
        } else {
            if (DAT_00ae9f1b == 0) goto input_blink;
            if ((DAT_00ae9f1b < 0xb) || (0x6e < DAT_00ae9f1b)) {
                DAT_00ae9f1c = 1;
            } else {
                DAT_00ae9f1c = 0;
            }
        }
        DAT_00ae9f20 = 2;
        DAT_00ae9f21 = 0;
        DAT_00ae9f24 = 0;
        DAT_00ae9f18 = 0;
        play_sfx(3, 6, 0);
    }
input_draw:
    menu_draw_cursor();
    FUN_00454fd0(DAT_00ae9f1b, 0, 0x30 - g_ScreenOffsetX, 0xba - g_ScreenOffsetY);
    return;

input_blink:
    DAT_00ae9f18 = DAT_00ae9f18 - 1;
    goto input_draw;
}

// (0x00487100) - Map tab state machine (returns non-zero when exiting)
static int menu_map_state_machine(void)
{
    // Cancel during the shrink/close (zoom 7+) drops back to the browser
    if (((dpad_pressed_byte1() & 0x80) != 0) && (7 <= MAP_ZOOM_STATE)) {
        MAP_STATE = 0;
    }
    switch (MAP_STATE) {
    case 0:
        // Browser: load the map0d..g textures and navigate
        if ((dpad_pressed_byte1() & 0x80) != 0) {
            *(unsigned int*)&DAT_00ac9890[0] = 0;
            play_sfx(3, 5, 0);
            return -1;
        }
        map_tab_update(&DAT_00ac9890[0]);
        if ((dpad_pressed_byte1() & 0x40) != 0) {
            // Cross: enter the full display (substate 0 re-inits the load)
            *(unsigned int*)&DAT_00ac9890[0] = 1;
            return 0;
        }
        break;
    case 1:
        // Full map display
        map_display_update(&DAT_00ac9890[0]);
        if (((dpad_pressed_byte1() & 0x40) != 0) && ((MAP_ZOOM_STATE == 0) || (6 < MAP_ZOOM_STATE))) {
            // Re-zoom from the shrink states: 8 -> 3, 9 -> 2
            if (MAP_ZOOM_STATE == 8) MAP_ZOOM_STATE = 3;
            if (MAP_ZOOM_STATE == 9) MAP_ZOOM_STATE = 2;
            play_sfx(3, 9, 0);
        }
        if ((dpad_pressed_byte1() & 0x80) != 0) {
            // Cancel: start the shrink (zoom state 7) and leave the tab
            MAP_ZOOM_STATE = 7;
            play_sfx(3, 5, 0);
            return -1;
        }
        break;
    default:
        // Other states (e.g. the display's close-path state 6): stay
        break;
    }
    return 0;
}

// (0x00420fd0) - Top tab: Map
static void menu_tab_map(void)
{
    if (menu_map_state_machine() != 0) {
        DAT_00ae9f20 = 1;
        DAT_00ae9f1e = DAT_00ae9f1e & 0x80;
    }
}

// (0x00481980) - File tab state machine (returns non-zero when exiting)
static int menu_file_state_machine(void)
{
    unsigned int uVar1 = 0;

    if (FILE_STATE == 0) {
        if ((FILE_BUSY == 0) && ((dpad_pressed_byte1() & 0x80) != 0)) {
            if ((FILE_MODE != 1) && (FILE_MODE != 2)) {
                play_sfx(3, 5, 0);
            }
            FILE_REQUEST = 2;
            FILE_BUSY = 1;
        }
        FUN_00481ab0(&DAT_00ac98b0[0]);
        if ((((FILE_BUSY != 0) || ((dpad_pressed_byte1() & 0x40) == 0)) ||
             (FILE_REQUEST != 1)) || (FILE_SLOT == 0xff)) {
            goto file_end;
        }
        FILE_MODE = 1;
        uVar1 = 6;
    } else {
        if ((FILE_STATE != 1) || (FUN_00482250(&DAT_00ac98b0[0]), FILE_BUSY != 0)) {
            goto file_end;
        }
        if ((dpad_pressed_byte1() & 0x80) != 0) {
            FILE_REQUEST = 0xd;
            FILE_BUSY = 1;
            play_sfx(3, 5, 0);
        }
        if (((dpad_pressed_byte1() & 0x40) == 0) || (FILE_SAVEFLAG != 1)) {
            goto file_end;
        }
        FILE_REQUEST = 0xd;
        uVar1 = 5;
    }
    FILE_BUSY = 1;
    play_sfx(3, uVar1, 0);
file_end:
    if (FILE_EXITFLAG == 0) {
        // The save/load dialog subsystem (FUN_00481ab0) is still pending; the
        // cancel request alone closes the file tab so the menu cannot lock.
        if (FILE_BUSY != 0) {
            FILE_BUSY = 0;
            FILE_STATE = 0;
            FILE_REQUEST = 0;
            return -1;
        }
        return 0;
    }
    FILE_EXITFLAG = 0;
    return -1;
}

// (0x00420ff0) - Top tab: File (save / load)
static void menu_tab_file(void)
{
    if (menu_file_state_machine() != 0) {
        DAT_00ae9f20 = 1;
        DAT_00ae9f1e = DAT_00ae9f1e & 0x80;
    }
}

// (0x00421010) - Top tab: Radio
static void menu_tab_radio(void)
{
    if (Flg_ck((int)DAT_00d213a0, 0x3f) != 0) {
        DAT_00ae9f20 = 1;
        g_usedItemId = 0x4d;
        DAT_00ae9f13 = DAT_00ae9f13 | 0x81;
        play_sfx(3, 7, 0);
        return;
    }
    if (FUN_0044e1b0() != 0) {
        DAT_00ae9f20 = 1;
        DAT_00ae9f1b = 0;
    }
}

// (0x00421060) - Top tab: Exit
static void menu_tab_exit(void)
{
    DAT_00ae9f20 = 1;
    menu_exit_cleanup();
}

// (0x00464770) - Load the item's 3D model TIM for the menu display
static void menu_load_item_model(void)
{
    unsigned char bVar2;
    unsigned int uVar3;
    int iVar4;
    unsigned int uVar5;
    char* pcVar6;
    char* pcVar7;
    char* pcVar8;

    if (DAT_00ae9f1b == SUBMENU_STATE_ID) {
        return;
    }
    SUBMENU_STATE_ID = DAT_00ae9f1b;
    if (DAT_00ae9f1b < 0x6f) {
        bVar2 = g_ItemImageLookupTable[(unsigned int)DAT_00ae9f1b * 4];
        if (((short)(char)DAT_00ae9f1e & 0xff7f) ==
            (unsigned short)(unsigned char)g_ItemImageLookupTable[(unsigned int)DAT_00ae9f1b * 4]) {
            SUBMENU_STATE_ID = DAT_00ae9f1b;
            return;
        }
    } else {
        DAT_00ae9f1e = 0;
        bVar2 = DAT_00ae9f1e;
    }
    DAT_00ae9f1e = bVar2;

    // Build the path: data root + "item_m2/" + file name + ".ivm"
    // (the original's "./usa/item_m2/" strings are rooted at the game data
    // directory, which in this port expands to GAME_DATA_ROOT).
    strcpy(DAT_008e1cb0, GAME_DATA_ROOT);
    strcat(DAT_008e1cb0, "item_m2/");
    if (DAT_00ae9f1b == 0x6f) {
        pcVar6 = (char*)g_ItemModelFileNameING;
    } else if (DAT_00ae9f1b == 0x70) {
        pcVar6 = (char*)g_ItemModelFileNameMINI;
    } else {
        pcVar6 = (char*)g_ItemModelFileNames + (char)DAT_00ae9f1e * 8;
    }
    strcat(DAT_008e1cb0, pcVar6);
    strcat(DAT_008e1cb0, (const char*)g_ItemModelExtIVM);

    LoadFile(DAT_008e1cb0, g_TimImageBuffer__bitmap, 0x20);
    g_TextureDepthByte = 0x1c;
    g_TextureBankID = 0x15;
    FUN_00484420(g_TimImageBuffer__bitmap, (void*)0xd024cc);
    DAT_00ae9f1e = DAT_00ae9f1e | 0x80;
}

// (0x004387e0) - Get EKG wave data index
// Scans EKG wave data table counting entries <= current EKG scroll position (DAT_00ae9f38 - 0x35)
static char FUN_004387e0(const unsigned char* data)
{
    char index = 0;
    unsigned char threshold = (unsigned char)((char)DAT_00ae9f38 - 0x35);
    while (*data <= threshold) {
        index++;
        data++;
    }
    return index;
}

// (0x00437fd0) - Draw health bar with EKG animation
// 0x00437fd0
static void menu_draw_health_bar(void)
{
    unsigned char bVar2;
    unsigned int uVar3;
    int iVar4;
    unsigned char bVar5;
    unsigned char bVar7;
    short sVar8;
    char* pcVar1;
    char* pcVar9;

    // EKG primary line data accessors (g_EkgPrimaryLine at 0x00be1198)
    #define EKG_P_FLAGS  (*(unsigned int*)&g_EkgPrimaryLine[0])
    #define EKG_P_X0     (*(short*)&g_EkgPrimaryLine[4])
    #define EKG_P_Y0     (*(short*)&g_EkgPrimaryLine[6])
    #define EKG_P_X1     (*(short*)&g_EkgPrimaryLine[8])
    #define EKG_P_Y1     (*(short*)&g_EkgPrimaryLine[10])
    #define EKG_P_R      (g_EkgPrimaryLine[12])
    #define EKG_P_G      (g_EkgPrimaryLine[13])
    #define EKG_P_B      (g_EkgPrimaryLine[14])

    // EKG secondary line data accessors (g_EkgSecondaryLine at 0x00be1184)
    #define EKG_S_X0     (*(short*)&g_EkgSecondaryLine[4])
    #define EKG_S_Y0     (*(short*)&g_EkgSecondaryLine[6])
    #define EKG_S_X1     (*(short*)&g_EkgSecondaryLine[8])
    #define EKG_S_Y1     (*(short*)&g_EkgSecondaryLine[10])
    #define EKG_S_R      (g_EkgSecondaryLine[12])
    #define EKG_S_G      (g_EkgSecondaryLine[13])
    #define EKG_S_B      (g_EkgSecondaryLine[14])
    #define EKG_S_R2     (g_EkgSecondaryLine[15])
    #define EKG_S_G2     (g_EkgSecondaryLine[16])
    #define EKG_S_B2     (g_EkgSecondaryLine[17])

    switch (DAT_00ae9f2e) {
    case 0:
        DAT_00ae9f2e = 1;
        DAT_00ae9f38 = 0x84;
        DAT_00ae9f3c = 0x84;
        // fall through
    case 1:
        DAT_00ae9f38 = DAT_00ae9f38 + 1;
        *(unsigned int*)&g_EkgPrimaryLine[0] = 0;
        unk_00be1180 = 0;
        if (0x83 < DAT_00ae9f38) {
            DAT_00ae9f38 = 0x35;
            if (g_playerEntity.health == 0) {
                DAT_00ae9f2f = 0;
            } else {
                DAT_00ae9f2f = (unsigned char)((g_playerEntity.health - 1) /
                    (int)(unsigned int)(g_playerEntity.maxHealth >> 2));
            }
            uVar3 = (unsigned int)DAT_00ae9f2f;
            DAT_00ae9f30 = DAT_004b92c8[uVar3 * 3];
            DAT_00ae9f31 = DAT_004b92c8[uVar3 * 3 + 1];
            DAT_00ae9f32 = DAT_004b92c8[uVar3 * 3 + 2];
            if (((g_playerEntity.healthStatusFlags & 0x22) != 0) &&
               (uVar3 = rand(), (uVar3 & 1) != 0)) {
                DAT_00ae9f2f = 4;
            }
            iVar4 = rand();
            bVar2 = (unsigned char)iVar4 & 3;
            if ((DAT_00ae9f48 & 3) == bVar2) {
                bVar2 = (bVar2 + 1) & 3;
            }
            DAT_00ae9f48 = bVar2 | DAT_00ae9f2f << 2;
            DAT_00ae9f40 = (char*)PTR_DAT_004b9278[DAT_00ae9f48];
        }
        DAT_00ae9f36 = DAT_00ae9f38 + 0xf;
        DAT_00ae9f3c = DAT_00ae9f3c + 1;
        if (0x83 < DAT_00ae9f3c) {
            DAT_00ae9f3c = DAT_00ae9f38 - 0x1f;
            DAT_00ae9f44 = DAT_00ae9f40;
            DAT_00ae9f33 = DAT_00ae9f30;
            DAT_00ae9f34 = DAT_00ae9f31;
            DAT_00ae9f35 = DAT_00ae9f32;
        }
        DAT_00ae9f3a = DAT_00ae9f3c + 0x1f;
        if ((g_playerEntity.healthStatusFlags & 0x22) != 0) {
            DAT_00ae9f2f = 4;
        }

        // Set up texture for health bar face icon
        g_TextureDesc.flags = 0x01000040;
        g_TextureDesc.texU = 0x60;
        g_TextureDesc.screenX = 100;
        g_TextureDesc.screenY = 0xa8;
        g_TextureDesc.width = 0x20;
        g_TextureDesc.height = 8;
        g_TextureDesc.unk10 = 0;
        g_TextureDesc.printClutTint = 0x1e4;
        g_TextureDesc.depth = 0x1c;

        // Draw face icon based on health status
        switch (DAT_00ae9f2f) {
        case 0:
            bVar2 = FUN_004387e0((unsigned char*)&DAT_004b92e0);
            if (DAT_004b92f8[bVar2] != 0) {
                g_TextureDesc.texV = DAT_004b92f8[bVar2] * 8 + 0x30;
                goto LAB_004382cb;
            }
            break;
        case 1:
            bVar2 = FUN_004387e0((unsigned char*)&DAT_004b92d8);
            if (DAT_004b92f0[bVar2] != 0) {
                g_TextureDesc.texV = DAT_004b92f0[bVar2] * 8 + 0x18;
                goto LAB_004382cb;
            }
            break;
        case 2:
        case 3:
            g_TextureDesc.texV = 0;
LAB_004382cb:
            display_texture(&g_TextureDesc, 10, 0, 1);
            break;
        case 4:
            bVar2 = FUN_004387e0((unsigned char*)&DAT_004b92e0);
            if (DAT_004b92f8[bVar2] != 0) {
                g_TextureDesc.texV = DAT_004b92f8[bVar2] * 8 + 0x40;
                goto LAB_004382cb;
            }
        }

        // Draw primary EKG line (FUN_00470c60, EngineStubs.cpp)
        if ((0x53 < DAT_00ae9f36) && (DAT_00ae9f38 < 0x84)) {
            EKG_P_R = DAT_00ae9f30;
            EKG_P_G = DAT_00ae9f31;
            EKG_P_B = DAT_00ae9f32;
            EKG_P_X0 = DAT_00ae9f36;
            if (0x83 < DAT_00ae9f36) {
                EKG_P_X0 = 0x83;
            }
            sVar8 = DAT_00ae9f38;
            if (DAT_00ae9f38 < 0x54) {
                sVar8 = 0x54;
            }
            // Walk wave data to find segment at current position
            pcVar9 = DAT_00ae9f40;
            char cVar6 = *pcVar9;
            while ((int)EKG_P_X0 < cVar6 + 0x54) {
                pcVar1 = pcVar9 + 4;
                pcVar9 = pcVar9 + 4;
                cVar6 = *pcVar1;
            }
            EKG_P_Y1 = (short)(char)pcVar9[2] + 0xa2;
            EKG_P_Y0 = ((EKG_P_X0 - (short)(unsigned char)*pcVar9) + -0x54) *
                       (short)(char)pcVar9[3] + (short)(char)pcVar9[2] + 0xa2;
            EKG_P_X1 = (short)(unsigned char)*pcVar9 + 0x54;
            if (sVar8 < EKG_P_X1) {
                FUN_00470c60(g_EkgPrimaryLine, 10);
                if (*pcVar9 == 0) goto LAB_0043849d;
                cVar6 = pcVar9[4];
                pcVar9 = pcVar9 + 4;
                while ((int)sVar8 < cVar6 + 0x54) {
                    EKG_P_X0 = (short)(unsigned char)pcVar9[1] + 0x54;
                    EKG_P_X1 = (short)(unsigned char)*pcVar9 + 0x54;
                    EKG_P_Y0 = (short)(char)pcVar9[-2] + 0xa2;
                    EKG_P_Y1 = (short)(char)pcVar9[2] + 0xa2;
                    FUN_00470c60(g_EkgPrimaryLine, 10);
                    cVar6 = pcVar9[4];
                    pcVar9 = pcVar9 + 4;
                }
                EKG_P_X0 = (short)(unsigned char)pcVar9[1] + 0x54;
                EKG_P_Y0 = (short)(char)pcVar9[-2] + 0xa2;
                EKG_P_Y1 = ((sVar8 - (short)(unsigned char)*pcVar9) + -0x54) *
                           (short)(char)pcVar9[3] + (short)(char)pcVar9[2] + 0xa2;
            }
            EKG_P_X1 = sVar8;
            FUN_00470c60(g_EkgPrimaryLine, 10);
        }
LAB_0043849d:
        // Draw secondary EKG line with gradient (FUN_00438800, EngineStubs.cpp)
        if ((0x53 < DAT_00ae9f3a) && (DAT_00ae9f3c < 0x84)) {
            bVar7 = DAT_00ae9f33 >> 5;
            bVar2 = DAT_00ae9f34 >> 5;
            bVar5 = DAT_00ae9f35 >> 5;
            if (DAT_00ae9f3a < 0x84) {
                EKG_S_R = DAT_00ae9f33;
                EKG_S_G = DAT_00ae9f34;
                EKG_S_B = DAT_00ae9f35;
                EKG_S_X0 = DAT_00ae9f3a;
            } else {
                char cFade = (char)(-0x7d - (char)DAT_00ae9f3a);
                EKG_S_R = DAT_00ae9f33 + bVar7 * cFade;
                EKG_S_G = DAT_00ae9f34 + bVar2 * cFade;
                EKG_S_B = DAT_00ae9f35 + bVar5 * cFade;
                EKG_S_X0 = 0x83;
            }
            sVar8 = DAT_00ae9f3c;
            if (DAT_00ae9f3c < 0x54) {
                sVar8 = 0x54;
            }
            pcVar9 = DAT_00ae9f44;
            char cVar6 = *pcVar9;
            while ((int)EKG_S_X0 < cVar6 + 0x54) {
                pcVar1 = pcVar9 + 4;
                pcVar9 = pcVar9 + 4;
                cVar6 = *pcVar1;
            }
            EKG_S_Y1 = (short)(char)pcVar9[2] + 0xa2;
            EKG_S_Y0 = ((EKG_S_X0 - (short)(unsigned char)*pcVar9) + -0x54) *
                       (short)(char)pcVar9[3] + (short)(char)pcVar9[2] + 0xa2;
            EKG_S_X1 = (short)(unsigned char)*pcVar9 + 0x54;
            if (EKG_S_X1 <= sVar8) {
                EKG_S_X1 = sVar8;
                FUN_00438800(bVar7, bVar2, bVar5);
            } else {
                FUN_00438800(bVar7, bVar2, bVar5);
                if (*pcVar9 != 0) {
                    cVar6 = pcVar9[4];
                    pcVar9 = pcVar9 + 4;
                    while ((int)sVar8 < cVar6 + 0x54) {
                        EKG_S_X0 = (short)(unsigned char)pcVar9[1] + 0x54;
                        EKG_S_X1 = (short)(unsigned char)*pcVar9 + 0x54;
                        EKG_S_Y0 = (short)(char)pcVar9[-2] + 0xa2;
                        EKG_S_Y1 = (short)(char)pcVar9[2] + 0xa2;
                        FUN_00438800(bVar7, bVar2, bVar5);
                        cVar6 = pcVar9[4];
                        pcVar9 = pcVar9 + 4;
                    }
                    EKG_S_X0 = (short)(unsigned char)pcVar9[1] + 0x54;
                    EKG_S_Y0 = (short)(char)pcVar9[-2] + 0xa2;
                    EKG_S_Y1 = ((sVar8 - (short)(unsigned char)*pcVar9) + -0x54) *
                               (short)(char)pcVar9[3] + (short)(char)pcVar9[2] + 0xa2;
                    EKG_S_X1 = sVar8;
                    FUN_00438800(bVar7, bVar2, bVar5);
                }
            }
        }
        break;

    case 2:
        DAT_00ae9f2e = 3;
        DAT_00ae9f36 = 0xaf;
        // fall through
    case 3:
        EKG_P_X1 = 0x83;
        EKG_S_R = 0;
        EKG_S_G = 0;
        EKG_P_X0 = 0x54;
        EKG_P_FLAGS = 0x50000000;
        EKG_S_B = 0;
        if (0x83 < DAT_00ae9f36) {
            char cCount;
            if (DAT_00ae9f36 < 0x92) {
                EKG_P_Y0 = 0x92;
                cCount = 0xf - (-0x6e - (char)DAT_00ae9f36);
                DAT_00ae9f2f = (-0x6e - (char)DAT_00ae9f36) * -0x10 - 1;
            } else {
                cCount = 0xf;
                DAT_00ae9f2f = 0xff;
                EKG_P_Y0 = DAT_00ae9f36;
            }
            if (((DAT_00ae9f13 & 0x20) == 0) || (EKG_S_G = DAT_00ae9f2f, (DAT_00ae9f13 & 0x10) != 0)) {
                EKG_S_B = DAT_00ae9f2f;
                EKG_S_R = EKG_S_G;
            }
            do {
                EKG_P_Y1 = EKG_P_Y0;
                FUN_00470c60(g_EkgPrimaryLine, 10);
                if (EKG_S_R != 0) {
                    EKG_S_R = EKG_S_R - 0x10;
                }
                if (EKG_S_G != 0) {
                    EKG_S_G = EKG_S_G - 0x10;
                }
                if (EKG_S_B != 0) {
                    EKG_S_B = EKG_S_B - 0x10;
                }
                EKG_P_Y0 = EKG_P_Y0 + 1;
                cCount = cCount - 1;
            } while ((EKG_P_Y0 < 0xb0) && (cCount != 0));
            DAT_00ae9f36 = DAT_00ae9f36 - 2;
            return;
        }
        DAT_00ae9f13 = DAT_00ae9f13 & 0xcf;
        DAT_00ae9f2e = 0;
        DAT_00ae9f12 = 0;
    }

    #undef EKG_P_FLAGS
    #undef EKG_P_X0
    #undef EKG_P_Y0
    #undef EKG_P_X1
    #undef EKG_P_Y1
    #undef EKG_P_R
    #undef EKG_P_G
    #undef EKG_P_B
    #undef EKG_S_X0
    #undef EKG_S_Y0
    #undef EKG_S_X1
    #undef EKG_S_Y1
    #undef EKG_S_R
    #undef EKG_S_G
    #undef EKG_S_B
    #undef EKG_S_R2
    #undef EKG_S_G2
    #undef EKG_S_B2
}

// ============================================================================
// Map screen
// ============================================================================
// The map submenu has two faces:
//   - Map tab (from the inventory menu): menu_map_state_machine runs a
//     browser showing the map0d..g layouts with floor/room navigation
//     (map_tab_update), then switches to the full map display.
//   - Map display (menu opened with the map item, mode 5): the menu cycle
//     menu_update_map_animation runs the zoom-in animation over the base
//     map, then the map01..09 textures with per-room marker tinting, the
//     flashing "you are here" dot and the area highlight.
// The state block at 0x00ac9890 is shared by both faces.
// ============================================================================

// --- Map data tables (original .data) ---

// Map tab layout file names (0x004d2fc0, 4 x 0x24). The original
// ".\usa\item_m2\map0d.tim" strings are rooted at GAME_DATA_ROOT here.
static const char* const g_MapTabFileNames[4] = {
    "item_m2/map0d.tim",    // layout 0 Mansion only
    "item_m2/map0c.tim",    // layout 1 Mansion and Courtyard
    "item_m2/map00.tim",    // layout 2 Mansion, Courtyard and Guardhouse
    "item_m2/map0e.tim"     // layout 3 Mansion, Courtyard, Guardhouse and Fountain (Lab entrance)
};

// Map display layout file names (0x004d3050, 11 x 0x24), indexed by area.
static const char* const g_MapFileNames[11] = {
    "item_m2/map01.tim", // Mansion 1F
    "item_m2/map02.tim", // Mansion 2F
    "item_m2/map03.tim", // Mansion B1
    "item_m2/map04.tim", // Courtyard
    "item_m2/map05.tim", // Underground
    "item_m2/map06.tim", // Laboratory B1
    "item_m2/map07.tim", // Laboratory B2
    "item_m2/map08.tim", // Laboratory B3
    "item_m2/map09.tim", // Laboratory B4
    "item_m2/map0a.tim", // Guardhouse 1F
    "item_m2/map0b.tim"  // Guardhouse B1
};

// Map background image (0x004d349c)
static const char* const g_MapBlueFileName = "item_m2/Map_blue.tim";

// Room count per area group (0x004d31e8), groups 0-5
static const unsigned char g_MapRoomCounts[6] = { 32, 31, 19, 18, 24, 0 };

// Area per (layout, room) (0x004d31f0); 0xFF = no room. The left/right
// adjacency reads (DAT_004d31ef / DAT_004d31f1) are this table shifted by
// one byte, i.e. the previous/next room's area.
static const unsigned char g_MapAreaTable[16] = {
    2, 0, 1, 0xFF,
    4, 3, 0xFF, 0xFF,
    6, 5, 0xFF, 0xFF,
    10, 9, 8, 7
};

// Area -> layout (0x004d3200). Entry 6 is the original DAT_004d3206 (2),
// the layout the map display falls back to when the map item grants the lab.
static const unsigned char g_MapAreaLayouts[16] = {
    0, 0, 0, 1, 1, 2, 2, 3, 3, 3, 3, 0, 0, 0, 0, 0
};

// Area -> area group (0x004d3210)
static const unsigned char g_MapAreaGroups[16] = {
    0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 4, 4, 0, 3, 2, 2
};

// Per-layout room count (0x004d321c). For layout 1 this is the index of the
// trailing 0xFF "exit" room, so the last real room resolves through the
// 0xFF path in map_update_layout_state.
static const unsigned char g_MapLayoutRoomCount[4] = { 0, 3, 2, 2 };

// Per-layout floor-number offset (0x004d3220)
static const unsigned char g_MapLayoutOffset[4] = { 4, 4, 4, 4 };

// --- Map sprite descriptors (original .data) ---

// 0x004d3228 - the base map image (tab browser + display backdrop)
static TextureDesc g_MapBaseDesc = {
    0x01000000, 33, 32, 160, 72, 0x15, 0x00, 0x00, 0, 0x1fc,
    0x80, 0x80, 0x80, 0, 0, 0, 0, 0
};

// 0x004d324c - room markers (one per layout, bright/dim by layout)
static TextureDesc g_MapSprites[8] = {
    { 0x01000000, 121, 32, 56, 64, 0x15, 0xa0, 0x00, 0, 0x1fc, 0, 0, 0, 0, 0, 0, 0, 0 },  // 0: marker 0
    { 0x01000000, 73, 32, 80, 64, 0x15, 0x00, 0x48, 0, 0x1fc, 0, 0, 0, 0, 0, 0, 0, 0 },   // 1: marker 1
    { 0x01000000, 49, 32, 40, 64, 0x15, 0x50, 0x48, 0, 0x1fc, 0, 0, 0, 0, 0, 0, 0, 0 },   // 2: marker 2
    { 0x01000000, 121, 32, 28, 64, 0x15, 0xc8, 0x48, 0, 0x1fc, 0, 0, 0, 0, 0, 0, 0, 0 },  // 3: marker 3
    { 0x01000000, 57, 104, 80, 16, 0x15, 0x78, 0x48, 0, 0x1fc, 0x80, 0x80, 0x80, 0, 0, 0, 0, 0 },  // 4: 0x004d32dc "you are here" dot
    { 0x01000000, 137, 104, 24, 16, 0x15, 0xe8, 0x60, 0, 0x1fc, 0x80, 0x80, 0x80, 0, 0, 0, 0, 0 }, // 5: 0x004d3300 floor glyph
    { 0x01000000, 145, 96, 8, 8, 0x15, 0xa0, 0x40, 0, 0x1fc, 0x80, 0x80, 0x80, 0, 0, 0, 0, 0 },   // 6: 0x004d3324 left arrow
    { 0x01000000, 145, 120, 8, 8, 0x15, 0xb0, 0x40, 0, 0x1fc, 0x80, 0x80, 0x80, 0, 0, 0, 0, 0 }   // 7: 0x004d3348 right arrow
};

// 0x004d3370 / 0x004d3394 / 0x004d33b8 - zoom sprites (scaleX/Y fix16.12,
// animated by map_display_animate). The map01..09 TIMs are 128x128 (8bpp
// width field is in 16-bit words), so the original 0x80/0x50/0x50 widths and
// 0x50 pivots are correct for the PC textures.
static TextureDesc g_MapZoomDesc[3] = {
    { 0x51000000, 16, 0, 128, 100, 0x15, 0x00, 0x88, 0, 0x1ff, 0x80, 0x80, 0x80, 0, 80, 50, 0x1000, 0x1000 },
    { 0x41000000, 0, 0, 80, 104, 0x15, 0x00, 0x88, 0, 0x1ff, 0x80, 0x80, 0x80, 0, 80, 52, 0x1000, 0x1000 },
    { 0x41000000, 0, 0, 80, 104, 0x15, 0x00, 0x88, 0, 0x1ff, 0x80, 0x80, 0x80, 0, 0, 52, 0x1000, 0x1000 }
};

// 0x004d33e0 - floor-transition animation sprites (drawn at zoom state 6)
static TextureDesc g_MapFloorAnim[4] = {
    { 0x51000000, 49, 24, 128, 1, 0x15, 0x00, 0x80, 0, 0x1ff, 0x80, 0x80, 0x80, 0, 0, 0, 0, 0 },
    { 0x01000000, 33, 24, 80, 1, 0x15, 0x00, 0x80, 0, 0x1ff, 0x80, 0x80, 0x80, 0, 0, 0, 0, 0 },
    { 0x01000000, 113, 24, 80, 1, 0x15, 0x00, 0x80, 0, 0x1ff, 0x80, 0x80, 0x80, 0, 0, 0, 0, 0 },
    { 0x51000000, 49, 24, 128, 1, 0x15, 0x00, 0x80, 0, 0x1ff, 0x80, 0x80, 0x80, 0, 0, 0, 0, 0 }
};

// 0x004d3470 - area name label
static TextureDesc g_MapLabelDesc = {
    0x01000000, 0, 0, 8, 8, 0x15, 0x00, 0xf0, 0, 0x1ff, 0x80, 0x80, 0x80, 0, 0, 0, 0, 0
};

// 0x004d3494 - "you are here" dot blink phase (rewritten every frame)
static unsigned char g_MapDotBlink;

// (0x004885c0) - Is the map area known (visited/cleared)?
// Area flag mapping: 0,1->0x7c; 3->0x7e; 4->0x7f; 5,6->0x80; 8,9->0x81.
// Areas 2, 7, 10 and out-of-range values (the 0xFF "no room" table entries)
// are never known - the original's out-of-range path reads uninitialized
// stack and returns 0 in practice.
static int map_area_known(unsigned char area)
{
    int flag;
    switch (area) {
    case 0:
    case 1: flag = 0x7c; break;
    case 2: return 0;
    case 3: flag = 0x7e; break;
    case 4: flag = 0x7f; break;
    case 5:
    case 6: flag = 0x80; break;
    case 7: return 0;
    case 8:
    case 9: flag = 0x81; break;
    default: return 0;
    }
    return Flg_ck((int)g_RoomFlags, flag);
}

// (0x00487540) - Map tab: draw the base map, markers, dot and floor glyph
static void map_draw_browser(unsigned char* state)
{
    display_texture(&g_MapBaseDesc, 0x32, 0xc, 1);
    for (int i = 0; i < 4; i++) {
        if ((state[0xd] & (1 << i)) != 0) {
            display_texture(&g_MapSprites[i], 0x2d, 0xc, 1);
        }
    }
    g_MapSprites[4].texV = (unsigned char)(state[4] * 0x10 + 0x48);
    display_texture(&g_MapSprites[4], 0x2d, 0xc, 1);
    g_MapSprites[5].texV =
        (unsigned char)((6 - state[8 + state[4]] - g_MapLayoutOffset[state[4]]) * 0x10);
    display_texture(&g_MapSprites[5], 0x2d, 0xc, 1);

    unsigned char c = state[0x11] + 1;
    state[0x11] = c;
    bool blink = (c & 0x30) != 0;
    if ((state[0xc] & 1) != 0) {
        g_MapSprites[6].texU = (unsigned char)(blink * 8 - 0x60);
        display_texture(&g_MapSprites[6], 0x2d, 0xc, 1);
    }
    if ((state[0xc] & 2) != 0) {
        g_MapSprites[7].texU = (unsigned char)(blink * 8 - 0x50);
        display_texture(&g_MapSprites[7], 0x2d, 0xc, 1);
    }
}

// (0x00487430) - Map tab: update the current room/area state and colors
static void map_update_layout_state(unsigned char* state)
{
    int layout = (int)(char)state[4];
    unsigned int room = (unsigned int)state[8 + layout];
    state[0xc] = 0;
    state[6] = g_MapAreaTable[layout * 4 + room];

    // Right room exists: not the last room of the layout, and the next
    // room's area is known (DAT_004d31f1 = area table shifted by one)
    if (g_MapLayoutRoomCount[layout] - room != 1) {
        unsigned char nextArea = g_MapAreaTable[layout * 4 + room + 1];
        if ((1 << (nextArea & 0x1f) & MAP_AREA_MASK) == 0) {
            if (map_area_known(nextArea) == 0) goto no_right_room;
        }
        state[0xc] = state[0xc] | 1;
    }
no_right_room:
    // Left room exists (DAT_004d31ef = area table shifted back by one)
    if (room != 0) {
        unsigned char prevArea = g_MapAreaTable[layout * 4 + room - 1];
        if ((1 << (prevArea & 0x1f) & MAP_AREA_MASK) == 0) {
            if (map_area_known(prevArea) == 0) goto no_left_room;
        }
        state[0xc] = state[0xc] | 2;
    }
no_left_room:
    // Marker colors: the current layout's marker stays bright, the rest dim
    for (int i = 0; i < 4; i++) {
        unsigned char v = (state[4] == i) ? 0x80 : 0x40;
        g_MapSprites[i].colorMulR = v;
        g_MapSprites[i].colorMulG = v;
        g_MapSprites[i].colorMulB = v;
    }
    g_MapBaseDesc.colorMulR = 0x80;
    g_MapBaseDesc.colorMulG = 0x80;
    g_MapBaseDesc.colorMulB = 0x80;
    // Dot, floor glyph and arrow sprites
    for (int i = 4; i < 8; i++) {
        g_MapSprites[i].colorMulR = 0x80;
        g_MapSprites[i].colorMulG = 0x80;
        g_MapSprites[i].colorMulB = 0x80;
    }
    map_draw_browser(state);
}

// (0x00487230) - Map tab: load the map0d..g texture for the current layout
static void map_tab_load_map(unsigned char* state)
{
    // Highest set layout bit (0-3) of the available-layout mask
    unsigned int uVar1 = 0;
    unsigned int uVar2 = 0;
    do {
        if ((state[0xd] & (1 << uVar1)) != 0) uVar2 = uVar1;
        uVar1++;
    } while (uVar1 < 4);

    if ((int)SUBMENU_STATE_ID - (int)uVar2 != 0x80) {
        SUBMENU_STATE_ID = (unsigned char)(uVar2 + 0x80);
        strcpy(DAT_008e1cb0, GAME_DATA_ROOT);
        strcat(DAT_008e1cb0, g_MapTabFileNames[uVar2]);
        LoadFile(DAT_008e1cb0, g_TimImageBuffer__bitmap, 0x20);
        DAT_00ac98a8[0] = 0x140;    // dead stores, see declaration
        DAT_00ac98a8[1] = 0x0001;
        DAT_00ac98a8[2] = 0x0080;
        DAT_00ac98a8[3] = 0x0088;
        LoadTexturePage(g_TimImageBuffer__bitmap, 0x15, 0x1c, 0xc, 0, 0, 0, 0);
    }
    map_update_layout_state(state);
}

// (0x004872d0) - Map tab: navigate rooms (up/down) and layouts (left/right)
static void map_tab_navigate(unsigned char* state)
{
    if ((state[0xf] & 1) == 0) {
        // Up: next room in the layout path (gated by the move flags)
        if (((pad_held_byte1() & 0x10) != 0) && ((state[0xc] & 1) != 0)) {
            state[8 + state[4]] = state[8 + state[4]] + 1;
            play_sfx(3, 4, 0);
        }
        // Down: previous room
        if (((pad_held_byte1() & 0x40) != 0) && ((state[0xc] & 2) != 0)) {
            state[8 + state[4]] = state[8 + state[4]] - 1;
            play_sfx(3, 4, 0);
        }
        // Right: next layout (mansion 4-layout wrap: 0<->3, 1<->2)
        if ((pad_held_byte1() & 0x80) != 0) {
            if ((state[0xd] & 8) == 0) {
                unsigned char next = state[4] + 1;
                if ((state[4] == 2) || ((state[0xd] & (1 << (next & 0x1f))) == 0))
                    goto tab_layout_done;
                state[4] = next;
            } else {
                switch (state[4]) {
                case 0: state[4] = 3; break;
                case 1: state[4] = 2; break;
                case 2: state[4] = 2; break;
                case 3: state[4] = 1; break;
                }
            }
            play_sfx(3, 4, 0);
        }
tab_layout_done:
        // Left: previous layout
        if ((pad_held_byte1() & 0x20) != 0) {
            if ((state[0xd] & 8) != 0) {
                switch (state[4]) {
                case 0:
                case 3: state[4] = 0; break;
                case 1: state[4] = 3; break;
                case 2: state[4] = 1; break;
                }
                play_sfx(3, 4, 0);
                map_update_layout_state(state);
                return;
            }
            unsigned char prev = state[4] - 1;
            if ((state[4] != 0) && ((state[0xd] & (1 << (prev & 0x1f))) != 0)) {
                state[4] = prev;
                play_sfx(3, 4, 0);
            }
        }
    }
    map_update_layout_state(state);
}

// (0x00487200) - Map tab update dispatcher (sub-state machine)
static void map_tab_update(unsigned char* state)
{
    if (state[1] == 0) {
        map_tab_load_map(state);
        state[1] = state[1] + 1;
        return;
    }
    if (state[1] == 1) {
        map_tab_navigate(state);
    }
}

// (0x00487ec0) - Map display: draw the zoom sprites, the flashing dot, the
// floor-transition arrows and the area highlight
static void map_display_draw(unsigned char* state)
{
    // TEMP DEBUG: report the zoom sprite draw results once
    static int s_dbgOnce = 1;
    if (s_dbgOnce) {
        s_dbgOnce = 0;
        dbg_printf("[MAP] zoom0 slot0xd r=%d | zoom1 slot0xf r=%d | zoom2 slot0xf r=%d\n",
            AddSprite_Ex(&g_MapZoomDesc[0], 0x28, 0xd, 1),
            AddSprite_Ex(&g_MapZoomDesc[1], 0x2a, 0xf, 1),
            AddSprite_Ex(&g_MapZoomDesc[2], 0x2a, 0xf, 1));
        for (int s = 0x1B; s <= 0x1F; s++) {
            dbg_printf("[MAP] page %02X srv=%p w=%d h=%d bpp=%d ox=%d oy=%d depth=%d clut=%d\n",
                s, g_TexturePageSRV[s], g_TexturePageWidth[s], g_TexturePageHeight[s],
                g_TexturePageBpp[s], g_TexturePageOriginX[s], g_TexturePageOriginY[s],
                g_TexturePageDepth[s], g_TexturePageClutBase[s]);
        }
        dbg_printf("[MAP] zoom0: flags=%08X xy=(%d,%d) wh=(%d,%d) texUV=(%d,%d) depth=%d clut=%d\n",
            g_MapZoomDesc[0].flags, g_MapZoomDesc[0].screenX, g_MapZoomDesc[0].screenY,
            g_MapZoomDesc[0].width, g_MapZoomDesc[0].height,
            g_MapZoomDesc[0].texU, g_MapZoomDesc[0].texV, g_MapZoomDesc[0].depth,
            g_MapZoomDesc[0].printClutTint);
    } else {
        AddSprite_Ex(&g_MapZoomDesc[0], 0x28, 0xd, 1);
        AddSprite_Ex(&g_MapZoomDesc[1], 0x2a, 0xf, 1);
        AddSprite_Ex(&g_MapZoomDesc[2], 0x2a, 0xf, 1);
    }

    // Flashing "you are here" dot: copy the zoom sprite and tint it with the
    // blink phase (the original copies 36 bytes including the padding; the
    // 9th dword is zero here and never read)
    for (int i = 0; i < 8; i++) ((unsigned int*)DAT_00ac3668)[i] = ((unsigned int*)&g_MapZoomDesc[0])[i];
    DAT_00ac3668[8] = 0;
    unsigned char blink = (unsigned char)(g_MapDotBlink << 2);
    ((unsigned char*)DAT_00ac3668)[0x14] = blink;   // colorMulR
    AddSprite_Ex((TextureDesc*)DAT_00ac3668, 0x26, 0xe, 1);

    // Floor-transition arrows while the zoom is parked at state 6
    if (state[3] == 6) {
        if (state[0xe] < 0xd0) {
            unsigned char a = state[0xe] >> 1;
            g_MapFloorAnim[0].screenY = (short)(a + 0x18);
            g_MapFloorAnim[0].height = 1;
            g_MapFloorAnim[0].texV = (unsigned char)(a - 0x7c);
            g_MapFloorAnim[1].screenY = (short)(a + 0x18);
            g_MapFloorAnim[1].height = 1;
            g_MapFloorAnim[1].texV = (unsigned char)(a - 0x78);
            g_MapFloorAnim[2].screenY = (short)(a + 0x18);
            g_MapFloorAnim[2].height = 1;
            g_MapFloorAnim[2].texV = (unsigned char)(a - 0x78);
            if (g_MapFloorAnim[0].texV > 0x8d) display_texture(&g_MapFloorAnim[0], 0x23, 0xd, 1);
            if (g_MapFloorAnim[1].texV > 0x8d) display_texture(&g_MapFloorAnim[1], 0x24, 0xf, 1);
            if (g_MapFloorAnim[2].texV > 0x8d) display_texture(&g_MapFloorAnim[2], 0x25, 0xf, 1);
            g_MapFloorAnim[3].screenY = (short)(a + 0x18);
            g_MapFloorAnim[3].colorMulR = blink;
            g_MapFloorAnim[3].height = 1;
            g_MapFloorAnim[3].texV = (unsigned char)(a - 0x7c);
            if (g_MapFloorAnim[3].texV > 0x8d) display_texture(&g_MapFloorAnim[3], 0x22, 0xe, 1);
        }
        state[0xe] = state[0xe] + 1;
        if (0x118 < state[0xe]) state[0xe] = 0;
    }

    // Area highlight animation (label sprite)
    if ((state[0x10] > 1) && (state[0xf] != 0)) {
        unsigned char frame = (state[0x15] / 3) & 7;
        switch (state[0x10]) {
        case 2:
            play_sfx(3, 10, 0);
            state[0x10] = 3;
            // fall through
        case 3:
            state[0x15] = state[0x15] + 2;
            if (0x17 <= state[0x15]) {
                state[0x10] = 4;
                state[0x15] = 0x17;
            }
            break;
        case 4:
            state[0x15] = state[0x15] - 1;
            if (state[0x15] == 0) {
                state[0x10] = 5;
                state[0x15] = 0x14;
            }
            break;
        case 5:
            frame = 0;
            state[0x15] = state[0x15] - 1;
            if (state[0x15] == 0) {
                state[0x10] = 2;
                state[0x15] = 0;
            }
            break;
        }
        if (state[0xf] / 3 == 0) {
            g_MapLabelDesc.screenX = 0x38;
            g_MapLabelDesc.screenY = 0x34;
        } else {
            g_MapLabelDesc.screenX = 0x86;
            g_MapLabelDesc.screenY = 0x43;
        }
        g_MapLabelDesc.texU = (unsigned char)(frame << 3);
        display_texture(&g_MapLabelDesc, 0x21, 0xd, 1);
    }
}

// (0x00487a30) - Map display: blink the dot and run the zoom animation
static void map_display_animate(unsigned char* state)
{
    // "You are here" dot blink (only when the area group matches)
    if (g_MapAreaGroups[state[6]] == state[5]) {
        switch (state[2]) {
        case 0:
            DAT_00ac368c = 0x50;
            state[2] = 1;
            break;
        case 1:
            DAT_00ac368c = DAT_00ac368c + 2;
            if (0xa0 < DAT_00ac368c) state[2] = 2;
            break;
        case 2:
            DAT_00ac368c = DAT_00ac368c - 2;
            if (DAT_00ac368c < 0x50) state[2] = 1;
            break;
        }
        DAT_00ac3690 = (unsigned short)((0x80 << 8) | (DAT_00ac368c >> 3));
        g_MapDotBlink = (unsigned char)(DAT_00ac3690 & 0x1f);
        DAT_00ac98a8[0] = (short)(state[7] * 6 + 0x10);     // dead stores
        DAT_00ac98a8[1] = 0x1ff;
        DAT_00ac98a8[2] = 1;
        DAT_00ac98a8[3] = 1;
    }

    // Map zoom state machine (fix16.12 scale, pivot-relative)
    switch (state[3]) {
    case 0:
        for (int i = 0; i < 3; i++) {
            g_MapZoomDesc[i].scaleX = 1;
            g_MapZoomDesc[i].scaleY = 200;
        }
        g_MapZoomDesc[0].flags = g_MapZoomDesc[0].flags & 0xefffffff;
        state[3] = 1;
        play_sfx(3, 9, 0);
        // fall through
    case 1:
        g_MapZoomDesc[0].screenX = 0x10;
        g_MapZoomDesc[0].screenY = 2;
        g_MapZoomDesc[0].width = 0x80;
        g_MapZoomDesc[1].screenX = 0;
        g_MapZoomDesc[1].screenY = 0;
        g_MapZoomDesc[1].width = 0x51;
        g_MapZoomDesc[2].screenX = 0;
        g_MapZoomDesc[2].screenY = 0;
        g_MapZoomDesc[2].width = 0x51;
        state[3] = 2;
        map_display_draw(state);
        return;
    case 2:
        for (int i = 0; i < 3; i++) {
            g_MapZoomDesc[i].scaleX = (short)(g_MapZoomDesc[i].scaleX * 3);
        }
        if (g_MapZoomDesc[0].scaleX < 0xfff) break;
        for (int i = 0; i < 3; i++) g_MapZoomDesc[i].scaleX = 0xfff;
        state[3] = 3;
        map_display_draw(state);
        return;
    case 3:
        for (int i = 0; i < 3; i++) {
            g_MapZoomDesc[i].scaleY = (short)(g_MapZoomDesc[i].scaleY + 900);
        }
        if (g_MapZoomDesc[0].scaleY < 0x15dc) break;
        for (int i = 0; i < 3; i++) g_MapZoomDesc[i].scaleY = 0x15dc;
        state[3] = 4;
        map_display_draw(state);
        return;
    case 4:
        for (int i = 0; i < 3; i++) {
            g_MapZoomDesc[i].scaleY = (short)(g_MapZoomDesc[i].scaleY - 300);
        }
        if (g_MapZoomDesc[0].scaleY > 0x1000) break;
        for (int i = 0; i < 3; i++) {
            g_MapZoomDesc[i].scaleX = 0x1000;
            g_MapZoomDesc[i].scaleY = 0x1000;
        }
        g_MapZoomDesc[0].screenX = 0x80;
        g_MapZoomDesc[0].screenY = 0x4e;
        g_MapZoomDesc[0].width = 0x80;
        g_MapZoomDesc[1].screenX = 0x70;
        g_MapZoomDesc[1].screenY = 0x4c;
        g_MapZoomDesc[1].width = 0x50;
        g_MapZoomDesc[2].screenX = 0x70;
        g_MapZoomDesc[2].screenY = 0x4c;
        g_MapZoomDesc[2].width = 0x50;
        DAT_00ac3694 = 8;
        state[3] = 5;
        map_display_draw(state);
        return;
    case 5:
        DAT_00ac3694 = DAT_00ac3694 - 1;
        if (DAT_00ac3694 == 0) {
            g_MapZoomDesc[0].flags = g_MapZoomDesc[0].flags | 0x10000000;
            state[3] = 6;
            DAT_00ac3694 = 10;
            map_display_draw(state);
            return;
        }
        break;
    case 6:
        // Wait for the area highlight to start (substate 1 -> 2)
        if (state[0x10] == 1) {
            DAT_00ac3694 = DAT_00ac3694 - 1;
            if (DAT_00ac3694 == 0) {
                state[0x10] = 2;
                map_display_draw(state);
                return;
            }
        }
        break;
    case 7:
        g_MapZoomDesc[0].flags = g_MapZoomDesc[0].flags & 0xefffffff;
        g_MapZoomDesc[0].screenX = 0x10;
        g_MapZoomDesc[0].screenY = 2;
        g_MapZoomDesc[0].width = 0x80;
        g_MapZoomDesc[1].screenX = 0;
        g_MapZoomDesc[1].screenY = 0;
        g_MapZoomDesc[1].width = 0x51;
        g_MapZoomDesc[2].screenX = 0;
        g_MapZoomDesc[2].screenY = 0;
        g_MapZoomDesc[2].width = 0x51;
        for (int i = 0; i < 3; i++) g_MapZoomDesc[i].scaleX = 0xfff;
        state[0xe] = 0;
        if (state[0x10] != 0) state[0x10] = 1;
        state[3] = 8;
        // fall through
    case 8:
        for (int i = 0; i < 3; i++) {
            g_MapZoomDesc[i].scaleY = (short)(g_MapZoomDesc[i].scaleY - 0x28a);
        }
        if (g_MapZoomDesc[0].scaleY > 1) break;
        for (int i = 0; i < 3; i++) g_MapZoomDesc[i].scaleY = 200;
        state[3] = 9;
        // fall through
    case 9:
        for (int i = 0; i < 3; i++) {
            g_MapZoomDesc[i].scaleX = (short)(g_MapZoomDesc[i].scaleX / 2);
        }
        if (g_MapZoomDesc[0].scaleX > 1) break;
        for (int i = 0; i < 3; i++) g_MapZoomDesc[i].scaleX = 1;
        if ((state[0xf] & 1) != 0) {
            *(unsigned int*)&state[0] = 0x106;   // close path: state 6, substate 1
        } else {
            *(unsigned int*)&state[0] = 0x100;   // re-open: state 0
        }
        map_display_draw(state);
        return;
    }
    map_display_draw(state);
}

// (0x004876b0) - Map display: load the layout map and tint the room markers
static void map_display_load_textures(unsigned char* state)
{
    if ((int)(char)state[6] - (int)SUBMENU_STATE_ID == -0x84) {
        // Texture already loaded for this area
    } else {
        SUBMENU_STATE_ID = (unsigned char)(state[6] + 0x84);
        strcpy(DAT_008e1cb0, GAME_DATA_ROOT);
        strcat(DAT_008e1cb0, g_MapFileNames[state[6]]);
        size_t fileSize = LoadFile(DAT_008e1cb0, g_TimImageBuffer__bitmap, 0x20);

        unsigned int group = g_MapAreaGroups[state[6]];
        unsigned int roomCount = g_MapRoomCounts[group];

        // The PS1 map TIMs carried a room-marker patch section (12-byte
        // entries at +0x1002e, colour table at +0x10014, index table at
        // +0x10220, count at +0x10214) that the PC repacks lack - the GOG
        // map files are ~0x4200 bytes with the markers already baked in.
        // Only patch when the section actually exists: reading the count
        // field unconditionally (as the original does) picks up stale
        // buffer data, and the marker-clear loop then runs for billions of
        // iterations - a hard freeze.
        bool hasMarkerSection = (fileSize != (size_t)-1) && (fileSize > 0x10220);
        if (hasMarkerSection) {
            int known = map_area_known(state[6]);
            if (known == 0) {
                // 12-byte marker header block at +0x10022 (just before the
                // first room entry at +0x1002e)
                unsigned int* pFirst = (unsigned int*)((unsigned char*)g_TimImageBuffer__bitmap + 0x10022);
                pFirst[0] = 0; pFirst[1] = 0; pFirst[2] = 0;
            }
            // Tint the room markers in the loaded TIM buffer: visited rooms
            // keep their colour, unvisited go transparent, the current room
            // is blanked (its marker is shown by the highlight page).
            unsigned int* pEntry = (unsigned int*)((unsigned char*)g_TimImageBuffer__bitmap + 0x1002e);
            for (unsigned int room = 0; room < roomCount; room++) {
                if (Flg_ck((int)g_RoomFlags, g_StageRoomFlagOffset[group] + room) == 0) {
                    if (known == 0) {
                        pEntry[0] = 0; pEntry[1] = 0; pEntry[2] = 0;
                    } else {
                        *(unsigned int*)((unsigned char*)pEntry + 2) = 0;
                        *(unsigned short*)((unsigned char*)pEntry + 6) = 0;
                    }
                } else if ((group == state[5]) && (state[7] == room)) {
                    *(unsigned int*)((unsigned char*)pEntry + 2) = 0;
                    *(unsigned short*)((unsigned char*)pEntry + 6) = 0;
                }
                pEntry += 3;
            }
            // Drop dead marker indices (their colour-table entry is zero)
            int count = *(int*)((unsigned char*)g_TimImageBuffer__bitmap + 0x10214) - 0xc;
            if (count > (int)(fileSize - 0x10220)) count = (int)(fileSize - 0x10220);
            for (int i = 0; i < count; i++) {
                unsigned char* idx = (unsigned char*)g_TimImageBuffer__bitmap + 0x10220 + i;
                if (*(short*)((unsigned char*)g_TimImageBuffer__bitmap + (unsigned int)*idx * 2 + 0x10014) == 0) {
                    *idx = 0;
                }
            }
        }
        DAT_00ac98a8[0] = 0x140;    // dead stores
        DAT_00ac98a8[1] = 0x188;
        DAT_00ac98a8[2] = 0x80;
        DAT_00ac98a8[3] = 0x70;
        LoadTexturePage(g_TimImageBuffer__bitmap, 0x15, 0x1f, 0xd, 4, 0, 0x88, 0);

        // Reload: the highlight page shows only the current room's marker
        LoadFile(DAT_008e1cb0, g_TimImageBuffer__bitmap, 0x20);
        if (hasMarkerSection) {
            int known = map_area_known(state[6]);
            if (known == 0) {
                unsigned int* pFirst = (unsigned int*)((unsigned char*)g_TimImageBuffer__bitmap + 0x10022);
                pFirst[0] = 0; pFirst[1] = 0; pFirst[2] = 0;
            }
            unsigned int* pEntry = (unsigned int*)((unsigned char*)g_TimImageBuffer__bitmap + 0x1002e);
            for (unsigned int room = 0; room < roomCount; room++) {
                if ((Flg_ck((int)g_RoomFlags, g_StageRoomFlagOffset[group] + room) == 0) ||
                    (group != state[5]) || (state[7] != room)) {
                    pEntry[0] = 0; pEntry[1] = 0; pEntry[2] = 0;
                } else {
                    *(unsigned int*)((unsigned char*)pEntry + 2) = 0x1f001f;
                    *(unsigned short*)((unsigned char*)pEntry + 6) = 0x1f;
                }
                pEntry += 3;
            }
            int count = *(int*)((unsigned char*)g_TimImageBuffer__bitmap + 0x10214) - 0xc;
            if (count > (int)(fileSize - 0x10220)) count = (int)(fileSize - 0x10220);
            for (int i = 0; i < count; i++) {
                unsigned char* idx = (unsigned char*)g_TimImageBuffer__bitmap + 0x10220 + i;
                if (*(short*)((unsigned char*)g_TimImageBuffer__bitmap + (unsigned int)*idx * 2 + 0x10014) == 0) {
                    *idx = 0;
                }
            }
        }
        DAT_00ac98a8[0] = 0x140;
        DAT_00ac98a8[1] = 0x188;
        DAT_00ac98a8[2] = 0x80;
        DAT_00ac98a8[3] = 0x70;
        LoadTexturePage(g_TimImageBuffer__bitmap, 0x15, 0x1f, 0xe, 4, 0, 0x88, 0);

        strcpy(DAT_008e1cb0, GAME_DATA_ROOT);
        strcat(DAT_008e1cb0, g_MapBlueFileName);
        LoadFile(DAT_008e1cb0, g_TimImageBuffer__bitmap, 0x20);
        LoadTexturePage(g_TimImageBuffer__bitmap, 0x15, 0x1f, 0xf, 4, 0, 0x88, 0);
    }

    // Start the area highlight for the map variants that show it
    if ((state[0xf] == 2) && (state[6] == 0)) state[0x10] = 1;
    else if ((state[0xf] == 4) && (state[6] == 6)) state[0x10] = 1;
    else state[0x10] = 0;
    map_display_animate(state);
}

// (0x00487640) - Map display update dispatcher + dim the browser sprites
static void map_display_update(unsigned char* state)
{
    if (state[1] == 0) {
        map_display_load_textures(state);
        state[1] = state[1] + 1;
    } else if (state[1] == 1) {
        map_display_animate(state);
    }
    // Dim everything for the display view (base map 0x40, sprites 0x30)
    g_MapBaseDesc.colorMulR = 0x40;
    g_MapBaseDesc.colorMulG = 0x40;
    g_MapBaseDesc.colorMulB = 0x40;
    for (int i = 0; i < 8; i++) {
        g_MapSprites[i].colorMulR = 0x30;
        g_MapSprites[i].colorMulG = 0x30;
        g_MapSprites[i].colorMulB = 0x30;
    }
    map_draw_browser(state);
}

// (0x00488430) - Build the area-known bitmask and the layout availability mask
static void map_build_area_mask(unsigned char* state)
{
    MAP_AREA_MASK = 0;
    // Areas reached through visited rooms (per area group and room range)
    for (unsigned int group = 0; group < 5; group++) {
        for (unsigned int room = 0; room < g_MapRoomCounts[group]; room++) {
            if (Flg_ck((int)g_RoomFlags, g_StageRoomFlagOffset[group] + room) == 0) continue;
            switch (group) {
            case 0: MAP_AREA_MASK |= 1; break;
            case 1:
                if ((room < 0x1a) || (room == 0x1d)) MAP_AREA_MASK |= 2;
                else MAP_AREA_MASK |= 4;
                break;
            case 2:
                if ((room < 6) || (room == 0x10)) MAP_AREA_MASK |= 8;
                else MAP_AREA_MASK |= 0x10;
                break;
            case 3:
                if (room < 0xd) MAP_AREA_MASK |= 0x20;
                else MAP_AREA_MASK |= 0x40;
                break;
            case 4:
                if ((room < 2) || (room == 0x16)) MAP_AREA_MASK |= 0x80;
                else if (room < 5) MAP_AREA_MASK |= 0x100;
                else if (room < 0x13) MAP_AREA_MASK |= 0x200;
                else MAP_AREA_MASK |= 0x400;
                break;
            }
        }
    }
    // Areas granted by the map-clear flags (0x7c-0x81)
    for (unsigned int area = 0; area < 0xb; area++) {
        if (map_area_known(area) != 0) MAP_AREA_MASK |= 1 << area;
    }
    // Layout availability mask
    MAP_LAYOUT_MASK = 0;
    for (unsigned int area = 0; area < 0xb; area++) {
        if ((MAP_AREA_MASK & (1 << area)) != 0) {
            MAP_LAYOUT_MASK |= 1 << (g_MapAreaLayouts[area] & 0x1f);
        }
    }
}

// (0x00488950) - Update the map variant flag (MAP_MODE)
static void map_update_variant(void)
{
    if ((g_main_state_flags & 0x00800000) != 0) {
        if (Flg_ck((int)g_PlayerFlags3, 0x23) != 0) {
            if (Flg_ck((int)g_PlayerFlags, 0x49) == 0) { MAP_MODE = 1; return; }
        }
        MAP_MODE = 0;
        return;
    }
    if (Flg_ck((int)g_PlayerFlags3, 0x2d) != 0) {
        if (Flg_ck((int)g_PlayerFlags, 0x49) == 0) { MAP_MODE = 1; return; }
    }
    if (Flg_ck((int)g_PlayerFlags3, 0x2e) != 0) {
        if (Flg_ck((int)g_PlayerFlags, 0x4a) == 0) {
            if ((g_stageId != 0) && (g_stageId != 1)) {
                if (Flg_ck((int)g_PlayerFlags3, 0x38) != 0) { MAP_MODE = 1; return; }
                MAP_MODE = 0;
                return;
            }
            MAP_MODE = 1;
            return;
        }
    }
    if (Flg_ck((int)g_PlayerFlags3, 0x38) != 0) {
        if (Flg_ck((int)g_PlayerFlags, 0x48) == 0) { MAP_MODE = 3; return; }
    }
    MAP_MODE = 0;
}

// (0x00488660) - Mark a room as explored (flag 0x82 + roomId)
static void map_set_room_flag(int roomId)
{
    Flg_on((int)g_RoomFlags, roomId + 0x82);
}

// (0x00488160) - Initialize map screen with room data (menu open, modes 0/6)
static void menu_init_map_screen(void)
{
    MAP_ROOM_IDX[0] = 1;
    MAP_ROOM_IDX[1] = 1;
    MAP_ROOM_IDX[2] = 1;
    MAP_ROOM_IDX[3] = 3;
    *(unsigned int*)&DAT_00ac9890[0] = 0;   // state, substate, blink, zoom
    MAP_ROOM = g_roomId;
    MAP_GROUP = g_stageId % 5;
    switch (MAP_GROUP) {
    case 0:
        if (((g_roomId == 7) && (2 < g_roomCameraId)) && (g_roomCameraId < 7)) {
            MAP_AREA = 0;
            MAP_ROOM = 0x1d;
        } else if ((g_roomId == 0xf) && ((g_roomCameraId == 3) || (g_roomCameraId == 4))) {
            MAP_AREA = 0;
            MAP_ROOM = 0x1e;
        } else if ((g_roomId == 0x10) && (g_roomCameraId == 0)) {
            MAP_AREA = 2;
            MAP_ROOM = 0x1e;
            MAP_GROUP = 1;
        } else {
            MAP_AREA = 0;
        }
        break;
    case 1:
        if ((g_roomId == 0xf) && ((g_roomCameraId == 2) || (g_roomCameraId == 5))) {
            MAP_AREA = 1;
            MAP_ROOM = 0x1d;
        } else if ((g_roomId == 0xc) && (3 < g_roomCameraId) && (g_roomCameraId < 8)) {
            MAP_AREA = 0;
            MAP_ROOM = 0x1f;
            MAP_GROUP = 0;
        } else if (g_roomId < 0x1a) {
            MAP_AREA = 1;
        } else {
            MAP_AREA = 2;
        }
        break;
    case 2:
        if (((g_roomId == 0xb) && (4 < g_roomCameraId)) && (g_roomCameraId < 7)) {
            MAP_AREA = 4;
            MAP_ROOM = 0x11;
        } else if ((g_roomId == 0xf) && (g_roomCameraId == 6)) {
            MAP_AREA = 4;
            MAP_ROOM = 0x12;
        } else if (g_roomId < 6) {
            MAP_AREA = 3;
        } else {
            MAP_AREA = 4;
        }
        break;
    case 3:
        if (g_roomId < 0xd) MAP_AREA = 5;
        else MAP_AREA = 6;
        break;
    case 4:
        if ((g_roomId == 7) && (g_roomCameraId < 4)) {
            MAP_AREA = 9;
            MAP_ROOM = 0x16;
        } else if (g_roomId < 2) {
            MAP_AREA = 7;
        } else if (g_roomId < 5) {
            MAP_AREA = 8;
        } else {
            MAP_AREA = 9;
            if (0x12 < g_roomId) MAP_AREA = 10;
        }
        break;
    }
    map_build_area_mask(&DAT_00ac9890[0]);
    // Room index per layout: the room whose area matches the current one
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (g_MapAreaTable[i * 4 + j] == MAP_AREA) {
                MAP_ROOM_IDX[i] = (unsigned char)j;
            }
        }
    }
    map_update_variant();
    if (MAP_MODE != 0) MAP_MODE = MAP_MODE + 1;
    MAP_LAYOUT = g_MapAreaLayouts[MAP_AREA];
    MAP_HL_STATE = 0;
}

// (0x004886b0) - Update map screen animation (returns non-zero when complete)
static int menu_update_map_animation(void)
{
    unsigned char timer = MAP_TIMER;
    switch (MAP_STATE) {
    case 0:
        MAP_TIMER = 10;
        MAP_STATE = MAP_STATE + 1;
        // fall through
    case 1:
        timer = MAP_TIMER;
        MAP_TIMER = MAP_TIMER - 1;
        if (timer == 0) {
            MAP_STATE = MAP_STATE + 1;
            MAP_TIMER = 5;
            play_sfx(3, 6, 0);
        }
        break;
    case 2:
        // Map tab browser phase (loads the map0d..g texture)
        map_tab_update(&DAT_00ac9890[0]);
        timer = MAP_TIMER;
        MAP_TIMER = MAP_TIMER - 1;
        if (timer == 0) {
            MAP_STATE = MAP_STATE + 1;
            MAP_TIMER = 0x14;
            MAP_ZOOM_STATE = 0;
            MAP_BLINK_STATE = 0;
            MAP_SUBSTATE = 0;
        }
        break;
    case 3:
        // Full map display: zoom in, then park at room 6
        map_display_update(&DAT_00ac9890[0]);
        timer = MAP_TIMER;
        if ((MAP_ZOOM_STATE == 6) && (MAP_TIMER = MAP_TIMER - 1, timer == 0)) {
            MAP_STATE = MAP_STATE + 1;
            MAP_HL_STATE = 2;
        }
        break;
    case 4:
        map_display_update(&DAT_00ac9890[0]);
        if (((dpad_pressed_byte1() & 0x80) != 0) || ((pad_held_byte1() & 8) != 0)) {
            MAP_ZOOM_STATE = 7;
            MAP_STATE = MAP_STATE + 1;
            play_sfx(3, 5, 0);
        }
        break;
    case 5:
        map_display_update(&DAT_00ac9890[0]);
        break;
    case 6:
        MAP_TIMER = 5;
        MAP_STATE = MAP_STATE + 1;
        play_sfx(3, 5, 0);
        // fall through
    case 7:
        // Close animation returns to the map tab browser
        map_tab_update(&DAT_00ac9890[0]);
        timer = MAP_TIMER;
        MAP_TIMER = MAP_TIMER - 1;
        if (timer == 0) {
            MAP_STATE = MAP_STATE + 1;
            MAP_TIMER = 5;
        }
        break;
    case 8:
        MAP_TIMER = MAP_TIMER - 1;
        if (timer == 0) {
            MAP_EXIT_FLAG = 1;
            play_sfx(3, 5, 0);
        }
        break;
    }
    return (MAP_EXIT_FLAG == 0) - 1;
}

// (0x00488880) - Initialize map display (menu mode 5 - map item)
static void menu_init_map_display(void)
{
    map_update_variant();
    *(unsigned int*)&DAT_00ac9890[0] = 0;
    if (MAP_MODE != 1) {
        MAP_LAYOUT = g_MapAreaLayouts[6];   // DAT_004d3206: layout 2 (lab map)
        MAP_GROUP = 5;
        MAP_AREA = 6;
    } else {
        MAP_LAYOUT = g_MapAreaLayouts[0];   // layout 0 (mansion 1F)
        MAP_GROUP = 1;
        MAP_AREA = 0;
    }
    MAP_ROOM_IDX[2] = (MAP_MODE == 1);
    MAP_ROOM_IDX[1] = 1;
    MAP_ROOM_IDX[0] = 1;
    MAP_ROOM = 0;
    MAP_ROOM_IDX[3] = 3;
    MAP_MOVE_FLAGS = 0;
    MAP_LAYOUT_MASK = 0;
    MAP_ARROW_ANIM = 0;
    MAP_HL_STATE = 0;
    MAP_HL_COUNT = 0;
    MAP_TIMER = 0;
    DAT_00ac9890[0x13] = 0;
    MAP_EXIT_FLAG = 0;
    MAP_HL_FRAME = 0;
    MAP_AREA_MASK = 0;
    map_build_area_mask(&DAT_00ac9890[0]);
}

// (0x004828f0) - Initialize status screen display
static void menu_init_status_screen(void) { }

// (0x00482910) - Update status screen (returns non-zero when complete)
static int menu_update_status_screen(void) { return 0; }

// (0x00482b80) - Reset status screen state
static void menu_reset_status_state(void) { }

// (0x004941f0) - Item box interaction (browse, swap, store items)
static int menu_itembox_interaction(void) { return 0; }

// (0x00494730) - Load item box menu textures
static void loadMenuAssets(void) { }

// (0x004947c0) - Draw item box menu overlay
static void draw_itembox_menu(void) { }

// (0x00464560) - Load frame part texture area from g_CurrentMenuFramesDataPtr table pointer.
// Each call reads a 12-byte entry backwards: screenX(short), screenY(short),
// width(ushort), height(ushort), texU(byte), texV(byte), then advances the pointer.
static void load_main_menu_frame_part_tex_area(void)
{
    int base = (int)g_CurrentMenuFramesDataPtr;
    g_TextureDesc.texV    = *(unsigned char*)(base - 2);
    g_TextureDesc.texU    = *(unsigned char*)(base - 4);
    g_TextureDesc.height  = *(unsigned short*)(base - 6);
    g_TextureDesc.width   = *(unsigned short*)(base - 8);
    g_TextureDesc.screenY = *(short*)(base - 10);
    g_TextureDesc.screenX = *(short*)(base - 12);
    g_CurrentMenuFramesDataPtr = (unsigned short*)(base - 12);
}
