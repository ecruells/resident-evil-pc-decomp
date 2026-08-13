// GameLoop.cpp - Main gameplay loop (game_loop at 0x00480b30)
// Decompiled from Ghidra with original addresses.
//
// This is the core gameplay loop that runs during active gameplay. It handles:
//   - Per-frame entity/camera/room updates
//   - Menu open/close state machine
//   - SCD script execution and room event processing
//   - Player death state machine with timed fade-out
//   - Countdown timer display (self-destruct sequences)
//   - Debug save menu access
//   - Attract demo transition
//
// Returns: 0 = game completed (chains to ending_state), 1 = died/quit (chains to title_state)
// Called by: game_start (0x00480710)
// ============================================================================
#include "../Globals.h"
#include "../marni/MarniInput.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include "../DebugPrint.h"

// Forward declarations for functions only used within game_loop
extern void FUN_00473f10(int* baseAddr, unsigned int bitIndex);
extern void run_command_functions(unsigned short* scd_opcodes);
extern void room_events_check(void);
extern void room_state_reset(void);

// Debug: F2 save-screen request state machine (0 = idle, 1 = fade out,
// 2 = save screen open, 3 = fade back in). Port-added, debug builds only.
#ifdef _DEBUG
static int g_debugSaveScreenState = 0;
#endif

// ============================================================================
// game_loop (0x00480b30)
// Main gameplay loop: entities, cameras, rooms, menus, combat.
// Returns: 0 = game completed -> ending, 1 = died/quit -> title
// ============================================================================
int game_loop(void)
{
    // 0x00480b30-0x00480b51: Wait for any pending menu/message to close
    while ((g_menu_choice_id & 0x80) != 0) {
        Task_sleep(1);
    }

    // 0x00480b53: Mark gameplay active
    g_main_state_flags |= 0x2000000;

    // 0x00480b66: Initialize message backup and saved light state
    g_short_message_flags = 0xFFFF;
    g_int_008f8898 = -1;

    // 0x00480b70: Seed RNG for attract mode demo playback
    if ((g_main_state_flags2 & 0x10000000) != 0) {
        srand(0);
    }

    // 0x00480b83: Initialize screen transition mask
    StMask(0, 6);

    // ========================================================================
    // Outer loop: runs each time we re-enter gameplay from a menu
    // ========================================================================
    do {
        // 0x00480b8f-0x00480bd4: Set up fade-in for room
        g_fade_type_id = 2;
        g_main_state_flags = (g_main_state_flags & 0x3FFFFFFF) | 0x80000000;

        int hasFlag = Flg_ck((int)g_PlayerFlags, 0x7d);
        g_fading_counter = 0xFF5D;
        if (hasFlag == 0) {
            g_fading_counter = 0xE800;
        }

        fade_update();

        g_SpecialRoomLightState = (short)g_int_008f8898;
        Task_sleep(1);

        // 0x00480bf2-0x00480c2e: Set message flags and reset input/menu state
        g_message_flags = 0xFD3F;
        if ((g_main_state_flags & 0x2000000) == 0) {
            g_message_flags = 1;
        }

        g_PlayerDpadPressed = 0;
        g_openMenuFlag = 2;
        g_main_state_flags &= 0xF5FFFFFF;
        g_PlayerDpadHeld = 0;

        // ====================================================================
        // Inner frame loop: runs once per game frame during gameplay
        // ====================================================================
LAB_00480c33:
        do {
            // 0x00480c33-0x00480c4a: Per-frame random seed and reset
            g_RandSeed = (short)rand();
            DAT_00d213a0[0] = 0;
            DAT_00d213a0[1] = 0;
            g_PlayerHealthCopy = g_playerEntity.health;

            // 0x00480c56-0x00480c65: Check interactive object states
            check_desk_state();
            check_itembox_state();
            check_typewriter_state();
            check_event_item_usage();

            // ---- Debug: F2 opens the save screen directly (no ink ribbon) ----
            // Mirrors check_typewriter_state's fade-out -> LoadSaveGameState ->
            // fade-back-in sequence, with useInkRibbon = 0 so nothing is consumed.
#ifdef _DEBUG
            if (g_debugOpenSaveScreenFlag != 0) {
                g_debugOpenSaveScreenFlag = 0;
                if ((g_openMenuFlag == 0) && (g_loadSaveStateFlag == 0) &&
                    ((g_main_state_flags & 0x8000) == 0)) {
                    g_debugSaveScreenState = 1;
                }
            }
            switch (g_debugSaveScreenState) {
            case 1: // fade out to black
                g_fade_type_id = 2;
                g_fading_counter = 0x1000;
                fade_update();
                g_debugSaveScreenState = 2;
                break;
            case 2: // black reached: open the save screen (blocking)
                if ((short)g_fading_state < 0) {
                    LoadSaveGameState(0, (int)g_loadDataDestPointer, 0, 2, 0);
                    g_loadSaveStateFlag = 0;
                    cut_set();
                    g_main_state_flags = (g_main_state_flags & 0x3fffffff) | 0x80000000;
                    StMask(1, 0);
                    g_fade_type_id = 2;
                    g_fading_counter = 0xf000;
                    fade_update();
                    g_debugSaveScreenState = 3;
                }
                break;
            case 3: // wait for fade back in
                if ((short)g_fading_state < 0) {
                    g_debugSaveScreenState = 0;
                    ((unsigned char*)&g_message_flags)[0] |= 0x45;
                }
                break;
            }
#endif

            // 0x00480c6a-0x00480ca0: Countdown timer management (self-destruct)
            if ((DAT_004d2294 > 29) || (g_CountdownTimer == 0x7FFF)) {
                DAT_004d2294 = 0;
                if (g_CountdownTimer < 0x7FFE) {
                    if ((g_main_state_flags & 0x1000000) == 0) {
                        g_CountdownTimer = g_CountdownTimer + 1;
                    }
                } else {
                    g_CountdownTimer = 0;
                }
            }

            // 0x00480ca7: Check if countdown timer active (self-destruct sequence)
            if ((g_main_state_flags2 & 0x8000000) == 0) {
                // ----- Normal gameplay path -----
LAB_00480d7c:
                // 0x00480d7c-0x00480d85: Increment frame counter (unless dead)
                if ((g_main_state_flags & 0x1000000) == 0) {
                    DAT_004d2294 = DAT_004d2294 + 1;
                }

                // 0x00480d85-0x00480d9d: Execute SCD room scripts, then step the
                // event VM and clear the per-frame room state. The original
                // calls these three back to back (0x00473f60, 0x0041d6a0,
                // 0x00475700); room_events_check and room_state_reset used to be
                // commented out here, which meant no room event script ever
                // advanced during gameplay.
                run_command_functions((unsigned short*)g_RoomScdOpcodes);
                room_events_check();
                room_state_reset();

                // 0x00480d98: Check interactive screen display
                check_and_display_interactive_screen();

                // 0x00480d9d-0x00480dcd: Handle game reset request (F9 key)
                if (g_resetGameFlag != 0) {
                    g_resetGameFlag = 0;
                    StMask(0, 3);
                    g_main_state_flags2 &= 0x20080000;
                    g_main_state_flags = (g_main_state_flags & 0x2FFFFFFF) | 0x40000000;
                    Task_chain((void*)title_state);
                }

                // 0x00480dcd-0x00480de8: Camera zone switching
                if ((g_main_state_flags & 0x20) == 0) {
                    check_camera_switch(0);
                } else {
                    g_main_state_flags &= ~0x20;
                    display_room_camera_bg();
                }

                // 0x00480de8-0x00480e89: Menu button detection
                WORD savedMsgFlags = g_message_flags;

                // Debug: F3 opens the item box directly. Sets menu mode 2
                // (0x1000, consumed by main_menu's mode scan) and requests the
                // menu through the same g_openMenuFlag path as the START button.
                // The mode bits are cleared when the menu closes
                // (menu_restore_game_state: g_main_state_flags &= 0xFFFF00FF).
#ifdef _DEBUG
                if (g_debugOpenItemboxFlag != 0) {
                    g_debugOpenItemboxFlag = 0;
                    if ((g_openMenuFlag == 0) && (g_loadSaveStateFlag == 0) &&
                        ((g_main_state_flags & 0x8000) == 0)) {
                        g_main_state_flags |= 0x1000;
                        g_openMenuFlag = 1;
                        g_message_flags &= 0xFF7A;
                        g_short_message_flags = savedMsgFlags;
                    }
                }

                // Debug: F6 toggles the in-game texture viewer overlay. The
                // overlay draws through the pending-sprite queue each frame
                // (FrameRateGovernor flushes it with the rest of the frame)
                // and freezes the player by blanking the pad state.
                if (g_debugTextureViewerFlag != 0) {
                    g_debugTextureViewerFlag = 0;
                    g_debugTextureViewerOpen = 1;
                }
                if (g_debugTextureViewerOpen) {
                    if (texture_viewer_overlay() == 0) {
                        g_debugTextureViewerOpen = 0;
                    }
                }
#endif

                if (((g_playerEntity.isBeingAttackedFlag == 0) &&
                     ((g_message_flags & 0x100) != 0) &&
                     ((g_message_flags & 0x40) != 0) &&
                     ((g_main_state_flags & 0x8000) == 0)))
                {
                    // Check START+bit8 combo (Option Mode)
                    if ((((g_button_pressed_id >> 8) & 0xFF) & 9) == 9 &&
                        ((g_main_state_flags & 0x7F00) == 0))
                    {
                        // Enable Option Mode menu
                        g_main_state_flags |= 0x400000;
                    }
                    else if (((g_PlayerPadHeld >> 8) & 8) == 0 &&
                             ((g_main_state_flags & 0x7F00) == 0))
                    {
                        goto LAB_00480e89;
                    }

                    // Menu requested
                    g_openMenuFlag = 1;
                    g_message_flags &= 0xFF7A;
                    g_short_message_flags = savedMsgFlags;
                }

LAB_00480e89:
                // 0x00480e89-0x00480ebd: Update entities and player
                update_entities();

                if (((g_playerEntity.unk_03 & 0x20) != 0) ||
                    ((g_message_flags & 0x100) == 0))
                {
                    g_PlayerDpadHeld &= 0xC000;
                    g_PlayerDpadPressed &= 0xC000;
                }

                // 0x00480ebd-0x00480ecf: Player animation and position update
                update_player_anim();
                g_main_state_flags2 &= ~1;
                update_player_position(&g_playerEntity, 1);

                // 0x00480ecf-0x00480ed4: Screen effects, then the room 3D-object
                // pass (collision + the walk-into-it push driver).
                DrawFadeSpr();
                update_room_objects();

                // 0x00480ed4: Camera/lighting update
                if (g_dwCameraLightingEnabled != 0) {
                    room_camera_and_lighting_update();
                }

                // 0x00480ee5-0x00480f52: Entity rendering loop (enemies).
                // EntityComputeJointWorldMatrices, EntityApplyLookAtRotation and
                // calc_entity_lighting all operate on the GLOBAL ENTITY pointer,
                // so the loop has to advance that global — walking a local copy
                // leaves every helper transforming whichever entity was set last.
                ENTITY = g_EnemiesList;
                int entCount = g_enemy_count;
                while (entCount != 0) {
                    if ((ENTITY->status_flags & 0x01) != 0) {
                        entCount = entCount - 1;
                        EntityComputeJointWorldMatrices(*(unsigned short*)&ENTITY->pad_ca);
                        EntityApplyLookAtRotation();
                        if (g_dwEntityRenderEnabled != 0) {
                            calc_entity_lighting(ENTITY);
                        }
                    }
                    ENTITY++;
                }

                // 0x00480f54-0x00480f89: Player entity rendering
                ENTITY = (Entity*)&g_playerEntity;
                EntityComputeJointWorldMatrices(g_playerEntity.unk_ca);
                EntityApplyLookAtRotation();
                if (g_dwEntityRenderEnabled != 0) {
                    calc_entity_lighting((Entity*)&g_playerEntity);
                }

                // 0x00480f6e-0x00480f70: 2D effects and room sprites
                update_2d_effects();
                DrawRoomSpr();

                // 0x00480f70-0x00480f90: Debug save menu
                if (g_displayDebugSaveMenu == 0) {
                    g_debugSaveMenuFlag = 1;
                } else {
                    g_displayDebugSaveMenu = 0;
                    if (g_debugSaveMenuFlag != 0) {
                        g_debugSaveMenuFlag = 0;
                        DebugSaveMenu();
                    }
                }

                // 0x00480f90-0x00480fae: Check player death
                if (((g_main_state_flags2 & 0x10000000) == 0) &&
                    (g_playerEntity.health < 0))
                {
                    g_main_state_flags |= 0x1000000;
                }
            }
            else {
                // ----- Countdown timer active (self-destruct sequence) -----
                if (g_CountdownTimer != 180) {
                    // 0x00480cb4-0x00480d7c: Display countdown timer
                    sprintf(PRINT_TEXT_BUFFER, "%2d:%02d:%d%d",
                            2 - g_CountdownTimer / 60,
                            59 - (unsigned int)g_CountdownTimer % 60,
                            DAT_004d2294 / -3 + 9,
                            (DAT_004d2294 + 1) % 10);
                    PrintText8x14(0x84, 0x20, (0x77 < g_CountdownTimer) + 1, 0);
                    goto LAB_00480d7c;
                }
                // 0x00480cbd-0x00480cee: Timer expired - trigger explosion FMV
                g_fmvDataPointer = g_loadDataDestPointer;
                g_playerEntity.flags = 0;
                g_main_state_flags |= 0x1040000;
                g_main_state_flags2 &= ~0x8000000;
                g_selectedFmvId = 2;
            }

            // ====================================================================
            // Death state machine (0x00480ff4)
            // ====================================================================
            if ((g_main_state_flags & 0x1000000) != 0) {
                switch (DAT_00be9614) {
                case 0:
                    // 0x00480ff4-0x004810c5: Death trigger - check room-specific behavior
                    if (((g_main_state_flags2 & 0x90000000) == 0) &&
                        (g_CountdownTimer != 180))
                    {
                        if ((g_stageId == 1) && (g_roomId == 0x10)) {
                            // Stage 1, Room 16: immediate death fade
                            TimeoutDeathFadeOut();
                            DAT_00be9614 = 3;
                        }
                        else if ((g_stageId == 6) && (g_roomId == 0x0C)) {
                            // Stage 6, Room 12: immediate death fade
                            TimeoutDeathFadeOut();
                            DAT_00be9614 = 3;
                        }
                        else if ((g_stageId == 3) && (g_roomId == 0x0E)) {
                            // Stage 3, Room 14: immediate death fade
                            TimeoutDeathFadeOut();
                            DAT_00be9614 = 3;
                        }
                        else {
                            // Other rooms: 90-frame delay before death
                            DAT_004d2288 = 0x5A;
                            DAT_00be9614 = 1;
                            goto switchD_00480ff4_caseD_1;
                        }
                    }
                    else {
                        // Countdown expired or attract mode: immediate death fade
                        TimeoutDeathFadeOut();
                        DAT_00be9614 = 3;
                    }
                    break;

                case 1:
switchD_00480ff4_caseD_1:
                    // 0x004810c5-0x004810e5: Death delay countdown
                    if (DAT_004d2288 == 0) {
                        DAT_00be9614 = 2;
                        goto switchD_00480ff4_caseD_2;
                    }
                    DAT_004d2288 = DAT_004d2288 - 1;
                    break;

                case 2:
switchD_00480ff4_caseD_2:
                    // 0x004810e5-0x004810f4: Delay expired - start fade out
                    TimeoutDeathFadeOut();
                    DAT_00be9614 = 3;
                    // fall through

                case 3:
                    // 0x004810f4-0x00481118: Wait for fade to complete, then die
                    if ((short)g_fading_state < 0) {
                        die_state();
                        return (g_main_state_flags2 & 0x80000000) == 0;
                    }
                    break;
                }
            }

            // 0x00481118-0x0048112e: Frame timing and attract demo check
            Task_sleep(1);
            Game_timer = Game_timer + 1;
            StartAttractDemo();

            // 0x00481133-0x00481176: Menu flag state machine
            if (g_openMenuFlag != 1) {
                if (g_openMenuFlag == 2) {
                    // State 2 -> 3: menu initialization complete
                    g_openMenuFlag = 3;
                    g_message_flags = 0;
                }
                else if ((g_openMenuFlag == 3) && ((short)g_fading_state < 1)) {
                    // State 3 -> 0: menu closing, restore flags
                    g_message_flags = g_short_message_flags | 0x200;
                    g_openMenuFlag = 0;
                    FUN_00473f10((int*)g_PlayerFlags, 0x7d);
                }
                goto LAB_00480c33;
            }

            // g_openMenuFlag == 1: menu requested, check message state
        } while ((g_menu_choice_id & 0x80) != 0);

        // ====================================================================
        // Menu is now open (0x00481176-0x0048123d)
        // ====================================================================

        // 0x00481176-0x004811a5: Save light state and start menu fade
        if ((g_main_state_flags & 0x2000000) == 0) {
            g_int_008f8898 = (int)(short)g_SpecialRoomLightState;
            set_fading(2, 0xC00);
        } else {
            set_fading(2, 0x600);
            g_int_008f8898 = -1;
        }

        // 0x004811a5-0x004811e3: Set up black overlay rect for menu background
        g_rect.textureId = 0;
        g_SpecialRoomLightState = 0xFFFF;
        g_rect.r = 0;
        g_rect.g = 0;
        g_rect.b = 0;
        g_rect.x = -0xA0;
        g_rect.y = -0x78;
        g_rect.w = 0x140;
        g_rect.h = 0xF0;

        // 0x004811e3-0x004811ee: Transition to menu rendering mode
        g_main_state_flags = (g_main_state_flags & 0x3FFFFFFF) | 0x40000000;
        StMask(0, 1);

        // 0x004811ee-0x0048123d: Execute appropriate menu handler
        if ((g_main_state_flags & 0x2000000) == 0) {
            // Normal gameplay: open in-game menu (status/inventory/map)
            check_menus_state();
        } else {
            // First load or room transition: restore room state
            room_transition_load();
            g_short_message_flags = 0xFFFF;
        }

    } while (true);
}

// ============================================================================
// check_menus_state (0x004815f0)
// Opens and processes the in-game menu system. Called from game_loop when the
// player requests a menu (START button). Dispatches to either the main menu
// (status/inventory/map) or the options menu (key bindings/display/sound)
// depending on whether the Option Mode flag (0x400000) was set.
//
// Flow:
//   1. Wait one frame for message system to settle
//   2. Set DAT_004d228c (menu processing flag)
//   3. If option mode flag set: clear it, set g_loadSaveStateFlag, run options_menu
//      Else: run main_menu
//   4. Set menu active flag (0x8000) in g_main_state_flags
//   5. Wait one frame, clear processing flag, cleanup
// ============================================================================
void check_menus_state(void)
{
    // 0x004815f0: Wait one frame
    Task_sleep(1);

    // 0x004815f7: Set menu processing flag
    DAT_004d228c = 1;

    // 0x00481604-0x00481626: Determine which menu to open
    if ((g_main_state_flags & 0x400000) != 0) {
        // Option Mode requested (START+bit8 combo detected in game_loop)
        g_main_state_flags &= ~0x400000;
        g_loadSaveStateFlag = 1;
        // 0x00481621: PUSH 0x4761b0 (options_menu)
        Task_execute(1, (void*)options_menu);
    } else {
        // 0x00481628: PUSH 0x463710 (main_menu)
        Task_execute(1, (void*)main_menu);
    }

    // 0x00481637: Set menu active flag
    g_main_state_flags |= 0x8000;

    // 0x00481641: Wait for menu task to complete
    Task_sleep(1);

    // 0x00481648: Clear menu processing flag
    DAT_004d228c = 0;

    // 0x00481655: Cleanup (tail call to empty_00412380)
    empty_00412380();
}

// UpdateDemoTimer (0x00429ce0) - increments demo idle timer and resets when threshold reached
void UpdateDemoTimer(void) {
  if ((g_main_state_flags2 & 0x10000000) != 0 &&
      (g_message_flags & 0x200) != 0 &&
      g_DemoTimerCur != 0) {
    g_DemoTimerCur++;
    if ((int)(g_DemoTimerMax - 1) <= (int)(unsigned short)g_DemoTimerCur) {
      g_DemoTimerCur = 0;
    }
  }
}

