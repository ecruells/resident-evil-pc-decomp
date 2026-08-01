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
static unsigned char  DAT_00ae9f10;       // 0x00ae9f10

// Menu state (0=init, 1=status/nav, 2=message wait, 3=itembox, 4=model view,
//             5=map anim, 6=model view2, 7=exit anim, 8=exit wait)
static char           g_MainMenuState;       // 0x00ae9f11

static unsigned char  DAT_00ae9f12;       // 0x00ae9f12
static unsigned char  DAT_00ae9f13;       // 0x00ae9f13

// 0x00ae9f14
// Frame part data pointer (used by load_main_menu_frame_part_tex_area)
static unsigned short* g_CurrentMenuFramesDataPtr;      

static char           DAT_00ae9f18;       // 0x00ae9f18

// Character type flags (bit 0 = chris/jill, bit 1 = rebecca/barry)
static unsigned char  DAT_00ae9f19;       // 0x00ae9f19

// Max inventory slots (6 for chris, 8 for jill)
static unsigned char  g_totalInventorySlots;       // 0x00ae9f1a

// Selected item ID for 3D model display
static unsigned char  DAT_00ae9f1b;       // 0x00ae9f1b

// Item model load direction (-1 or 1)
static char           DAT_00ae9f1c;       // 0x00ae9f1c

// Model loaded flag (0x80 = loaded, lower bits = image type)
static unsigned char  DAT_00ae9f1e;       // 0x00ae9f1e

// Map available flag (1 = player has map)
static unsigned char  DAT_00ae9f1f;       // 0x00ae9f1f

// Item box submenu state (0=init, 1=cursor, 2=item selected, 3=scrolling)
static unsigned char  DAT_00ae9f20;       // 0x00ae9f20

static unsigned char  DAT_00ae9f21;       // 0x00ae9f21

// Cursor grid position (0-7 for items, 0-4 for menu tabs)
static unsigned char  DAT_00ae9f23;       // 0x00ae9f23

// Item box scroll position
static unsigned char  DAT_00ae9f24;       // 0x00ae9f24

static unsigned char  DAT_00ae9f26;       // 0x00ae9f26
static unsigned char  DAT_00ae9f27;       // 0x00ae9f27
static unsigned char  DAT_00ae9f28;       // 0x00ae9f28

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
static unsigned char  DAT_00ae9f49;       // 0x00ae9f49
static unsigned char  DAT_00ae9f4a;       // 0x00ae9f4a

// Item box submenu scroll position (0-15)
static unsigned char  SUBMENU_STATE_ID;   // 0x00ae9f1d

// Item model path buffer (for loading TIM files)
static char           DAT_008e1cb0[128];  // 0x008e1cb0

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

// External function stubs (pending decompilation)
extern void empty_00470a20(void);
extern void FUN_004844b0(void);
extern void FUN_00470a40(void);
extern void FUN_0047d0e0(void);
extern void FUN_00462940(void);
extern void FUN_0040ac80(int idx, void* lightData);
extern void FUN_00484420(void* src, void* dst);
extern void FUN_00420b80(void);
extern void FUN_00420bb0(void);
extern void FUN_00420a70(void);
extern void FUN_00454fd0(int itemId, int mode, short x, short y);
extern int draw_texture(TextureDesc* tex, unsigned short depth);
extern int   FUN_0044e1b0(void);
extern void  FUN_00470c60(void* prim, int depth);
extern void  FUN_00438800(int a, int b, int c);


extern const char DAT_004c29a0[];
extern const char DAT_004c29a8[];
extern const char DAT_004bd348[];
extern const char DAT_004bd5a0[];
extern const char DAT_004bd5a8[];
extern const unsigned char DAT_004b92c8[];
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
extern const unsigned char DAT_004d31f0[];
extern const unsigned char DAT_004d3200[];
extern const unsigned char DAT_004d3206[];

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
                bool cond1 = (((g_PlayerPadPressed >> 8) & 0x80) == 0) || (DAT_00ae9f20 != 1);
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
    FUN_00462940();
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
        pbVar2 = (unsigned char*)g_ItemSlotsPointer + (g_EquippedItemId * 2 - 2);
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
        iVar5 = (int)((unsigned int)g_EquippedItemId * 2 + (unsigned int)(uintptr_t)g_ItemSlotsPointer);
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
        pbVar2 = (unsigned char*)g_ItemSlotsPointer + uVar7 * 2;
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
        pbVar2 = (unsigned char*)g_ItemSlotsPointer + uVar7 * 2;
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

// (0x00464770) - Load item 3D model TIM for menu display
static void menu_load_item_model(void) { }

// (0x00420880) - Handle menu cursor input (D-pad navigation, selection)
static void menu_handle_input(void) { }

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
            draw_texture(&g_TextureDesc, 10);
            break;
        case 4:
            bVar2 = FUN_004387e0((unsigned char*)&DAT_004b92e0);
            if (DAT_004b92f8[bVar2] != 0) {
                g_TextureDesc.texV = DAT_004b92f8[bVar2] * 8 + 0x40;
                goto LAB_004382cb;
            }
        }

        // Draw primary EKG line (calls stub FUN_00470c60 - not yet implemented)
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
            EKG_P_Y1 = (short)(unsigned char)pcVar9[2] + 0xa2;
            EKG_P_Y0 = ((EKG_P_X0 - (short)(unsigned char)*pcVar9) + -0x54) *
                       (short)(char)pcVar9[3] + (short)(unsigned char)pcVar9[2] + 0xa2;
            EKG_P_X1 = (short)(unsigned char)*pcVar9 + 0x54;
            if (sVar8 < EKG_P_X1) {
                FUN_00470c60(g_EkgPrimaryLine, 10);
                if (*pcVar9 == 0) goto LAB_0043849d;
                cVar6 = pcVar9[4];
                pcVar9 = pcVar9 + 4;
                while ((int)sVar8 < cVar6 + 0x54) {
                    EKG_P_X0 = (short)(unsigned char)pcVar9[1] + 0x54;
                    EKG_P_X1 = (short)(unsigned char)*pcVar9 + 0x54;
                    EKG_P_Y0 = (short)(unsigned char)pcVar9[-2] + 0xa2;
                    EKG_P_Y1 = (short)(unsigned char)pcVar9[2] + 0xa2;
                    FUN_00470c60(g_EkgPrimaryLine, 10);
                    cVar6 = pcVar9[4];
                    pcVar9 = pcVar9 + 4;
                }
                EKG_P_X0 = (short)(unsigned char)pcVar9[1] + 0x54;
                EKG_P_Y0 = (short)(unsigned char)pcVar9[-2] + 0xa2;
                EKG_P_Y1 = ((sVar8 - (short)(unsigned char)*pcVar9) + -0x54) *
                           (short)(char)pcVar9[3] + (short)(unsigned char)pcVar9[2] + 0xa2;
            }
            EKG_P_X1 = sVar8;
            FUN_00470c60(g_EkgPrimaryLine, 10);
        }
LAB_0043849d:
        // Draw secondary EKG line with gradient (calls stub FUN_00438800)
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
            EKG_S_Y1 = (short)(unsigned char)pcVar9[2] + 0xa2;
            EKG_S_Y0 = ((EKG_S_X0 - (short)(unsigned char)*pcVar9) + -0x54) *
                       (short)(char)pcVar9[3] + (short)(unsigned char)pcVar9[2] + 0xa2;
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
                        EKG_S_Y0 = (short)(unsigned char)pcVar9[-2] + 0xa2;
                        EKG_S_Y1 = (short)(unsigned char)pcVar9[2] + 0xa2;
                        FUN_00438800(bVar7, bVar2, bVar5);
                        cVar6 = pcVar9[4];
                        pcVar9 = pcVar9 + 4;
                    }
                    EKG_S_X0 = (short)(unsigned char)pcVar9[1] + 0x54;
                    EKG_S_Y0 = (short)(unsigned char)pcVar9[-2] + 0xa2;
                    EKG_S_Y1 = ((sVar8 - (short)(unsigned char)*pcVar9) + -0x54) *
                               (short)(char)pcVar9[3] + (short)(unsigned char)pcVar9[2] + 0xa2;
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

// (0x00488160) - Initialize map screen with room data
static void menu_init_map_screen(void) { }

// (0x004828f0) - Initialize status screen display
static void menu_init_status_screen(void) { }

// (0x00482910) - Update status screen (returns non-zero when complete)
static int menu_update_status_screen(void) { return 0; }

// (0x00482b80) - Reset status screen state
static void menu_reset_status_state(void) { }

// (0x004886b0) - Update map screen animation (returns non-zero when complete)
static int menu_update_map_animation(void) { return 0; }

// (0x00488880) - Initialize map display
static void menu_init_map_display(void) { }

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
