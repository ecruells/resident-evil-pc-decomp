// main_loop.cpp - Main game loop
// Original function: main_loop at 0x00428eb0 (Ghidra)
// Adapted from Ghidra decompilation
#include "Globals.h"

// Temporary globals used in main_loop
static int g_spriteAnimActive;     // 0x00be41d0
static int g_spriteAnimR;           // 0x00be41d1
static int g_spriteAnimG;           // 0x00be41d2
static int g_spriteAnimB;           // 0x00be41d3
static int g_spriteAnimIntensity;   // 0x00be41d4
static int g_demoIdleTimer;         // 0x004c44d8
static int g_pressF9Flag;           // 0x004ba718
static int g_fading_007d9048;       // 0x007d9048
static int g_fading_007d904c;       // 0x007d904c
static int g_MaxHealthDisplayFlag;  // 0x00d227c0
static int g_screenStateVar;        // 0x004ba73c range
static int g_specialRoomLightR;     // 0x00be9828 (alias: g_SpecialRoomLightR BioCard macro)
static int g_specialRoomLightState; // 0x00be9836 (alias: g_SpecialRoomLightState BioCard macro)
static int g_specialRoomLightDelta; // 0x00be9838 (alias: g_SpecialRoomLightDelta BioCard macro)
// g_SpecialR1, g_SpecialG1, g_SpecialB1 moved to Globals.cpp (used by cmd_0x1c)
static RectDrawDesc g_FadingRect;   // 0x004ba720
static RectDrawDesc g_ColorRect;    // 0x004ba730
static RectDrawDesc g_ColorRect2;   // 0x004ba740
static int g_SpriteDrawY;           // 0x004ba754
static int g_SpriteDrawX;           // 0x004ba784
static int g_MenuChoiceSpecial;     // 0x00be0e2c

// ============================================================================
// main_loop - Main game update/rendering loop (0x00428eb0)
// ============================================================================
int main_loop(void)
{
    SetFrameRateMode(g_bGameActive != 0);
    
    // 0x00428ed1: Initialize game on first run
    if (init_game_flag == 0) {
        init_and_start_game();
        init_game_flag = 1;
    }

    int g_loopCounter = 0;

    do {
        // 0x00428ee0: Frame startup - input update
        InputUpdate();
        PlayerPad_Update();
        
        // 0x00428eff: Check for special key combination (F9/F10/F11 or similar)
        // Original: checks if word[0x00be05b4] != 0 (edge-detected pad press) AND
        //           DAT_00bcb2e0 (g_lastScanCodeOrMsgID) matches 0x5B/0x5C/0x5D (F9/F10/F11 scan codes)
        if (g_PlayerPadPressed != 0) {
            if (g_lastScanCodeOrMsgID == 0x5B || g_lastScanCodeOrMsgID == 0x5C || g_lastScanCodeOrMsgID == 0x5D) {
                // Debug menu trigger
                DAT_00d91bc8 = 1;
                g_menu_choice_id = 0;
            }
        }
        
        // 0x00428f48: Handle input flags for sidewinder/gamepad
        if ((g_InputFlags & 0x10) == 0) {
            // Process shortcut button flags
            if (g_bIsSideWinderConnected) {
                g_button_pressed_id |= 0x800;
                // Original: DAT_00bf0a0c |= 0x800
                g_bIsSideWinderConnected = FALSE;
            }
            
            DWORD inputState = g_button_pressed_id;
            if (g_bIsPaused) {
                g_button_pressed_id = inputState | 0x900;
                g_bIsPaused = FALSE;
            }
        } else {
            g_bIsSideWinderConnected = FALSE;
            g_bIsPaused = FALSE;
        }
        
        // 0x00429015: Check main state - screen fade transition
        if ((g_main_state_flags & 0x40000) == 0) {
            // Normal game state
            
            // 0x00429028: Return to title screen check
            if ((g_demoIdleTimer == 0) && ((g_main_state_flags & 0x20000) == 0) &&
                (g_displayReturnToTitleScreen_Flag != 0)) {
                
                // Draw "Press F9 to abort game and return" message
                sprintf(PRINT_TEXT_BUFFER, "Press F9 to abort game and return to");
                PrintText8x14(16, 100, 128, 1);

                sprintf(PRINT_TEXT_BUFFER, "title screen");
                // unk_00be0e2c = 0x9d;
                PrintText8x14(16, 116, 128, 1);
                
                sprintf(PRINT_TEXT_BUFFER, "Or any other key to continue game");
                // unk_00be0e41 = 0x9d;
                PrintText8x14(16, 150, 128, 1);
                
                // Draw background black rect
                g_window_rect.w = 320;
                g_window_rect.textureId = 0;
                g_window_rect.r = 0;
                g_window_rect.g = 0;
                g_window_rect.b = 0;
                g_window_rect.x = -g_ScreenOffsetX;
                g_window_rect.h = 240;
                g_window_rect.y = -g_ScreenOffsetY;
                draw_rect(&g_window_rect, 100, 1);
                
                if (!g_pressF9Flag) {
                    PauseSounds();
                }
                g_pressF9Flag = TRUE;
                
                if (g_button_pressed_id != 0) {
                    g_displayReturnToTitleScreen_Flag = FALSE;
                    ResumePausedSounds();
                }
            }
            // 0x004291b4: Exit game screen check
            else if (g_displayExitGameScreen_flag == 0) {
                if (g_pressF9Flag) {
                    g_pressF9Flag = FALSE;
                    ResumePausedSounds();
                }
                
                // 0x004291d5: Update demo timer
                UpdateDemoTimer();
                
                // 0x004291ed: Reset screen panning
                if ((g_main_state_flags & 0x80000) != 0) {
                    ResetScreenPanning();
                }
                
                // 0x00429209: Update sound fade
                if (g_SndFadeType != 0) {
                    UpdateSoundFadeState();
                }
                
                // 0x0042921b: Update sound decay
                if (g_SndRampFramesLeft != 0) {
                    UpdateSoundDecay();
                }
                
                // 0x004292a5: Check fade state for rendering
                if ((g_main_state_flags & 0x20000000) == 0) {
        fade_label:
                    // Normal rendering path
                    g_spriteAnimActive ^= 1;
                    g_MaxHealthDisplayFlag = 0x7FFFFFFF;
                    
                    // 0x0042935f: Update task scheduler
                    TaskScheduler_Update();
                    
                    // 0x00429374: Handle menu choice (sound/display)
                    if ((g_menu_choice_id & 0x80) != 0) {
                        FUN_004557b0();
                    }
                    
                    // 0x00429394: Fading effects
                    if (g_fading_state >= 0) {
                        // Calculate fade rect values
                        g_FadingRect.textureId = (g_fade_type_id | 4) << 28;
                        g_FadingRect.r = (BYTE)(g_fading_state >> 7);
                        g_fading_state += g_fading_counter;
                        
                        int drawBlend;
                        if ((g_playerHealth < 0) && (g_MaxHealthDisplayFlag != 0x7FFFFFFF)) {
                            drawBlend = 100;
                        } else {
                            drawBlend = 0;
                        }
                        
                        g_FadingRect.g = g_FadingRect.r;
                        g_FadingRect.b = g_FadingRect.r;
                        g_FadingRect.x = -g_ScreenOffsetX;
                        g_FadingRect.y = -g_ScreenOffsetY;
                        g_FadingRect.w = 320;
                        g_FadingRect.h = 240;
                        draw_rect(&g_FadingRect, drawBlend, 0);
                    }
                } else {
                    // 0x004294f0: Fade-in state (msb set)
                    if (g_fading_state < 0) {
                        g_fading_007d9048 = 0;
                        g_FadingRect.textureId = (g_fade_type_id | 4) << 28;
                        g_fading_state = 0;
                        if (g_fading_counter < 1) {
                            g_fading_state = 0x7FFF;
                        }
                        
                        g_fading_007d904c = 0;
                        if ((g_TasksTable[0].state & 0x40) == 0) {
                            Task_suspend(0);
                        } else {
                            g_fading_007d904c = g_TasksTable[0].state;
                        }
                    }
                    
                    g_FadingRect.b = (BYTE)(g_fading_state >> 7);
                    g_fading_007d9048 += g_fading_counter;
                    
                    if ((g_fading_007d9048 & 0xFC00) == 0x400) {
                        g_FadingRect.b = 8;
                    }
                    g_fading_007d9048 &= 0x2FF;
                    
                    g_FadingRect.r = g_FadingRect.b;
                    g_FadingRect.g = g_FadingRect.b;
                    g_FadingRect.x = -g_ScreenOffsetX;
                    g_FadingRect.y = -g_ScreenOffsetY;
                    g_FadingRect.w = 320;
                    g_FadingRect.h = 240;
                    draw_rect(&g_FadingRect, 0, 0);
                    
                    // Update tasks and fade state
                    TaskScheduler_Update();

                    g_fading_state += g_fading_counter;
                    
                    if (g_fading_state < 0) {
                        g_fading_counter = 0;
                        g_main_state_flags &= ~0x20000000;
                        g_fading_state = 0;
                        
                        if (g_fading_007d904c == 0) {
                            Task_Resume(0);
                        }
                        goto fade_label;
                    }
                }
                
                // 0x004297b0: Screen intensity animation
                if ((g_main_state_flags & 0x10000) == 0) {
                    if (g_spriteAnimIntensity > 0x0F) {
                        g_spriteAnimIntensity -= 0x10;
                    }
                } else {
                    if (g_spriteAnimIntensity < 0xF0) {
                        g_spriteAnimIntensity += 0x10;
                    }
                }
                
                // 0x004297e0: Apply color effects
                BYTE intensity = (BYTE)g_spriteAnimIntensity;
                g_ColorRect.b = intensity;
                g_ColorRect.g = intensity;
                g_ColorRect.r = intensity;

                g_ColorRect2.b = intensity;
                
                int colorOffset = g_spriteAnimActive * 0x18;

                g_ColorRect2.g = intensity;
                g_ColorRect2.r = intensity;
                
                // Apply color to screen rects
                // This section handles screen color tinting based on game state
                
                if (g_spriteAnimIntensity == 0xF0) {
                    // Full intensity - white flash
                    // Apply to various screen regions via explicit colorRect values
                    g_ColorRect.b = 0xff;
                    g_ColorRect.g = 0xff;
                    g_ColorRect.r = 0xff;
                    g_ColorRect2.b = 0xff;
                    g_ColorRect2.g = 0xff;
                    g_ColorRect2.r = 0xff;
                }
                
                // 0x004299d6: Room-specific lighting
                int fadeBlend = 0;
                if ((g_main_state_flags & 0x4008000) == 0) {
                    if (g_spriteAnimIntensity != 0) {
                        g_ColorRect.x = -g_ScreenOffsetX;
                        g_ColorRect.y = -g_ScreenOffsetY;
                        g_ColorRect.w = 320;
                        g_ColorRect.h = 240;
                        g_ColorRect2.x = -g_ScreenOffsetX;
                        g_ColorRect2.y = -g_ScreenOffsetY;
                        g_ColorRect2.w = 320;
                        g_ColorRect2.h = 240;
                        // Special room/camera logic for Stage 4, Room 0x13, Camera 5
                        if ((g_stageId == 4) && (g_stageId == 0x13) && (g_stageId == 5)) {
                            draw_rect(&g_ColorRect, 0, 0);
                            fadeBlend = 0;
                        } else {
                            draw_rect(&g_ColorRect, 0x28, 0);
                            fadeBlend = 0x28;
                        }
                        
                        draw_rect(&g_ColorRect2, fadeBlend, 0);
                    }
                    
                    // 0x00429a80: Additional special room lighting
                    if (g_specialRoomLightState >= 0) {
                        g_FadingRect.textureId = (g_specialRoomLightR | 4) << 28;
                        BYTE specComp = (BYTE)(g_specialRoomLightState >> 7);
                        
                        // Apply special room color filter
                        g_FadingRect.r = g_SpecialR1 & specComp;
                        g_FadingRect.g = g_SpecialG1 & specComp;
                        g_FadingRect.b = g_SpecialB1 & specComp;
                        g_FadingRect.x = -g_ScreenOffsetX;
                        g_FadingRect.y = -g_ScreenOffsetY;
                        g_FadingRect.w = 320;
                        g_FadingRect.h = 240;
                        
                        // Stage 2, Room 5, Camera 3 special handling
                        if (((g_stageId == 2) && (g_roomId == 5)) && (g_roomCameraId == 3)) {
                            if ((g_FadingRect.r & 0x80) == 0) {
                                g_FadingRect.r = g_FadingRect.r << 1;
                            }
                            else {
                                g_FadingRect.r = 0xff;
                            }
                            if ((g_FadingRect.g & 0x80) == 0) {
                                g_FadingRect.g = g_FadingRect.g << 1;
                            }
                            else {
                                g_FadingRect.g = 0xff;
                            }
                            if ((g_FadingRect.b & 0x80) == 0) {
                                g_FadingRect.b = g_FadingRect.b << 1;
                            }
                            else {
                                g_FadingRect.b = 0xff;
                            }
                        }
                                    
                        g_specialRoomLightState += g_specialRoomLightDelta;
                        
                        // Special room handling (Stage 3, Room 0x11, various cameras)
                        if ((g_stageId == 3) && (g_roomId == 0x11)) {
                            draw_rect(&g_FadingRect,0x14,0);
                            
                            if ((g_roomCameraId == 4) || (g_roomCameraId == 3)) {
                                fadeBlend = 0x14;
                            } else {
                    special_rooms_fading:
                                fadeBlend = 0x31;
                            }

                            draw_rect(&g_FadingRect, fadeBlend, 0);
                        } else {
                            BOOL useSpecialFade = TRUE;
                            
                            if (g_stageId == 2) {
                                useSpecialFade = g_roomId != 0;
                                if (g_roomId == 1) {
                                useSpecialFade = false;
                                }
                            }
                            if (g_stageId == 2) {
                                if (g_roomId == 2) {
                                useSpecialFade = false;
                                }
                                if ((g_roomId == 5) && (g_roomCameraId == 0)) {
                                useSpecialFade = false;
                                }
                            }
                            if (g_stageId == 2) {
                                if ((g_roomId == 0xf) && (g_roomCameraId == 1)) {
                                useSpecialFade = false;
                                }
                                if ((g_roomId == 0x10) && (g_roomCameraId == 0)) {
                                useSpecialFade = false;
                                }
                            }
                            if ((g_stageId == 4) && (g_roomId == 0)) {
                                useSpecialFade = false;
                            }
                            if (useSpecialFade) goto special_rooms_fading;
                        }
                    }
                }


                // 0x00429d10: Screenshot/debug overlay modes
                if ((g_main_state_flags & 0x40000000) == 0) {
                    if ((g_main_state_flags & 0x80000000) != 0) {
                        if ((g_InputFlags & 0x04) == 0) {
                            FUN_00401020(-(g_spriteAnimActive == 0) & 0xF0);
                        } else {
                            FUN_0045ab60();
                        }
                    }
                } else {
                    // Debug overlay mode - draw colored rect
                    g_window_rect.w = 320;
                    g_window_rect.textureId = 0;
                    g_window_rect.r = g_spriteAnimR;
                    g_window_rect.x = -g_ScreenOffsetX;
                    g_window_rect.g = g_spriteAnimG;
                    g_window_rect.h = 240;
                    g_window_rect.y = -g_ScreenOffsetY;
                    g_window_rect.b = g_spriteAnimB;
                    FUN_00497360(g_spriteAnimR, g_spriteAnimG, g_spriteAnimB);
                }
                
                // 0x00429dc0: Screen shake application
                if (((g_InputFlags & 0x02) != 0) && ((g_main_state_flags >> 8) == 0)) {
                    ApplyScreenShake();
                }
                
                // 0x00429de8: Present screen (debug check)
                if (g_ScreenAccessCheck != 0) {
                    FUN_00497340(1);
                }
                
                // 0x00429e01: Sound update
                empty_0047b950(0);
                UpdateMusicWaitState();

            } else {
                // 0x00429015 (alternate): Exit game screen
                sprintf(PRINT_TEXT_BUFFER, "Press F9 to exit game and return");
                PrintText8x14(16, 100, 128, 1);
                
                sprintf(PRINT_TEXT_BUFFER, "to desktop");
                // unk_00be0e2a = 0x9d;
                PrintText8x14(16, 116, 128, 1);
                
                sprintf(PRINT_TEXT_BUFFER, "Or any other key to return to");
                PrintText8x14(16, 150, 128, 1);
                
                sprintf(PRINT_TEXT_BUFFER, "title screen");
                // unk_00be0e2c = 0x9d;
                PrintText8x14(16, 166, 128, 1);
                
                g_window_rect.w = 320;
                g_window_rect.textureId = 0;
                g_window_rect.r = 0;
                g_window_rect.g = 0;
                g_window_rect.b = 0;
                g_window_rect.x = -g_ScreenOffsetX;
                g_window_rect.h = 240;
                g_window_rect.y = -g_ScreenOffsetY;
                draw_rect(&g_window_rect, 100, 1);
                
                if (g_button_pressed_id != 0) {
                    g_displayExitGameScreen_flag = FALSE;
                }
            } 
        } 
        else {
            g_window_rect.h = 240;
            g_main_state_flags &= ~0x40000;
            g_window_rect.w = 320;
            g_CurrentFMVID = g_currentFMVID;
            g_bMCINotifyEnabled = TRUE;
            g_window_rect.textureId = 0;
            g_window_rect.r = 0;
            g_FmvCharacterId = g_selectedFmvId;
            g_window_rect.g = 0;
            g_window_rect.b = 0;
            g_window_rect.x = -g_ScreenOffsetX;
            g_window_rect.y = -g_ScreenOffsetY;
            draw_rect(&g_window_rect, 0, 0);
            g_bMCINotifyFlag = TRUE;
            StMask(1, 0);
        }
        
        // 0x0042a041: Frame counter
        g_loopCounter++;
        if (g_loopCounter != 0) {
            // 0x0042a060: FMV cleanup on state change
            if ((g_main_state_flags & 0x40000) != 0) {
                FUN_004973a0(0);
                g_window_rect.x = -g_ScreenOffsetX;
                g_window_rect.textureId = 0;
                g_window_rect.r = 0;
                g_window_rect.g = 0;
                g_window_rect.b = 0;
                g_window_rect.y = -g_ScreenOffsetY;
                g_window_rect.w = 320;
                g_window_rect.h = 240;
                draw_rect(&g_window_rect, 0, 0);
                StMask(1, 1);
            }

            // 0x0042a0d0: Frame timing + present governor
            FrameRateGovernor();
            g_gameTimerSnapshot = Game_timer;
            Game_timer = Game_timer;

            return 1;
        }
    } while (1);
}
