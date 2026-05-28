// main_loop.cpp - Main game loop
// Original function: main_loop at 0x00428eb0 (Ghidra)
// Adapted from Ghidra decompilation
#include "Globals.h"

// Temporary globals used in main_loop
static int g_spriteAnimActive;     // DAT_00be41d0
static int g_spriteAnimR;           // DAT_00be41d1
static int g_spriteAnimG;           // DAT_00be41d2
static int g_spriteAnimB;           // DAT_00be41d3
static int g_spriteAnimIntensity;   // DAT_00be41d4
static int g_demoIdleTimer;         // DAT_004c44d8
static int g_pressF9Flag;           // DAT_004ba718
static int g_fadingSubVar_9048;     // DAT_007d9048
static int g_fadingSubVar_904c;     // DAT_007d904c
static int g_MaxHealthDisplayFlag;  // DAT_00d227c0
static int g_screenStateVar;        // DAT_004ba73c range
static int g_specialRoomLightR;     // DAT_00be9828
static int g_specialRoomLightState; // DAT_00be9836
static int g_specialRoomLightDelta; // DAT_00be9838
static int g_SpecialR1;             // DAT_00be961d
static int g_SpecialG1;             // DAT_00be961e
static int g_SpecialB1;             // DAT_00be961f
static int g_SpriteAnimFlag;        // DAT_004ba720 flags
static int g_DrawRectBlend;         // DAT_004ba75x usage
static int g_SpriteDrawY;           // DAT_004ba754 use
static int g_SpriteDrawX;           // DAT_004ba784 use
static int g_DebugInputStatus;      // DAT_00bcb2e0 special
static int g_MenuChoiceSpecial;     // DAT_00be0e2c use

// ============================================================================
// main_loop - Main game update/rendering loop (0x00428eb0)
// ============================================================================
int main_loop(void)
{
    // 0x00428ec0: Assert status flags
    FUN_00442150(g_StackPointer != 0);
    
    // 0x00428ed1: Initialize game on first run
    if (init_game_flag == 0) {
        init_and_start_game();
        init_game_flag = 1;
    }
    
    // 0x00428ee0: Frame startup - input update
    InputUpdate();
    PlayerPad_Update();
    
    // 0x00428eff: Check for special key combination (F9/F10/F11 or similar)
    if (g_RawPadPressed != 0) {
        // Check for magic keys: VK_F9, VK_F10, VK_F11 (scan codes 0x5b, 0x5c, 0x5d)
        // DAT_00bcb2e0 stores key scan codes in original
        int keyScan = (g_RawPadPressed & 0xFF);
        if (keyScan == 0x5B || keyScan == 0x5C || keyScan == 0x5D) {
            // Debug menu trigger
            g_SelectedPlayerID = 1;
            g_menu_choice_id = 0;
        }
    }
    
    // 0x00428f48: Handle input flags for sidewinder/gamepad
    if ((g_InputFlags & 0x10) == 0) {
        // Process shortcut button flags
        if (g_bIsSideWinderConnected) {
            button_pressed_id |= 0x800;
            // Original: DAT_00bf0a0c |= 0x800
            g_bIsSideWinderConnected = FALSE;
        }
        
        DWORD inputState = button_pressed_id;
        if (g_bIsPaused) {
            button_pressed_id = inputState | 0x900;
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
            sprintf(PRINT_TEXT_BUFFER, "Press F9 to abort game and return");
            PrintText8x14(16, 100, 128, 1);
            
            sprintf(PRINT_TEXT_BUFFER, "to title screen");
            g_menu_choice_id = 0x9D;
            PrintText8x14(16, 116, 128, 1);
            
            sprintf(PRINT_TEXT_BUFFER, "Or any other key to continue game");
            g_menu_choice_id = 0x9D;
            PrintText8x14(16, 150, 128, 1);
            
            // Draw background rect
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
            
            if (button_pressed_id != 0) {
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
                    int fadeBase = (g_fade_type_id | 4) << 28;
                    int fadeComp = (BYTE)(g_fading_state >> 7);
                    g_fading_state += g_fading_counter;
                    
                    int drawBlend;
                    if ((g_playerHealth < 0) && (g_MaxHealthDisplayFlag != 0x7FFFFFFF)) {
                        drawBlend = 100;
                    } else {
                        drawBlend = 0;
                    }
                    
                    // Build fade rect
                    RectDrawDesc fadeRect = {};
                    fadeRect.w = 320;
                    fadeRect.h = 240;
                    fadeRect.r = fadeComp;
                    fadeRect.g = fadeComp;
                    fadeRect.b = fadeComp;
                    fadeRect.x = -g_ScreenOffsetX;
                    fadeRect.y = -g_ScreenOffsetY;
                    draw_rect(&fadeRect, drawBlend, 0);
                }
            } else {
                // 0x004294f0: Fade-in state (msb set)
                if (g_fading_state < 0) {
                    g_fadingSubVar_9048 = 0;
                    int fadeBase2 = (g_fade_type_id | 4) << 28;
                    g_fading_state = 0;
                    if (g_fading_counter < 1) {
                        g_fading_state = 0x7FFF;
                    }
                    
                    g_fadingSubVar_904c = 0;
                    if ((g_TasksTable[0].state & 0x40) == 0) {
                        Task_suspend(0);
                    } else {
                        g_fadingSubVar_904c = g_TasksTable[0].state;
                    }
                }
                
                int fadeComp2 = (BYTE)(g_fading_state >> 7);
                g_fadingSubVar_9048 += g_fading_counter;
                
                if ((g_fadingSubVar_9048 & 0xFC00) == 0x400) {
                    fadeComp2 = 8;
                }
                g_fadingSubVar_9048 &= 0x2FF;
                
                // Build fade rect
                RectDrawDesc fadeRect2 = {};
                fadeRect2.w = 320;
                fadeRect2.h = 240;
                fadeRect2.r = fadeComp2;
                fadeRect2.g = fadeComp2;
                fadeRect2.b = fadeComp2;
                fadeRect2.x = -g_ScreenOffsetX;
                fadeRect2.y = -g_ScreenOffsetY;
                draw_rect(&fadeRect2, 0, 0);
                
                // Update tasks and fade state
                TaskScheduler_Update();
                g_fading_state += g_fading_counter;
                
                if (g_fading_state < 0) {
                    g_fading_counter = 0;
                    g_main_state_flags &= ~0x20000000;
                    g_fading_state = 0;
                    
                    if (g_fadingSubVar_904c == 0) {
                        Task_Resume(0);
                    }
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
            RectDrawDesc colorRect = {};
            colorRect.r = intensity;
            colorRect.g = intensity;
            colorRect.b = intensity;
            
            int colorOffset = g_spriteAnimActive * 0x18;
            
            // Apply color to screen rects
            // This section handles screen color tinting based on game state
            
            if (g_spriteAnimIntensity == 0xF0) {
                // Full intensity - white flash
                // Apply to various screen regions via explicit colorRect values
            }
            
            // 0x004299d6: Room-specific lighting
            int fadeBlend = 0;
            if ((g_main_state_flags & 0x4008000) == 0) {
                if (g_spriteAnimIntensity != 0) {
                    // Special room/camera logic for Stage 4, Room 0x13, Camera 5
                    if ((g_STAGE_ID == 4) && (g_ROOM_ID == 0x13) && (g_roomCamera_id == 5)) {
                        // Use special draw mode
                        RectDrawDesc darkRect = {};
                        draw_rect(&darkRect, 0, 0);
                    } else {
                        RectDrawDesc darkRect = {};
                        draw_rect(&darkRect, 0x28, 0);
                        fadeBlend = 0x28;
                    }
                    
                    RectDrawDesc darkRect2 = {};
                    draw_rect(&darkRect2, fadeBlend, 0);
                }
                
                // 0x00429a80: Additional special room lighting
                if (g_specialRoomLightState >= 0) {
                    int specLightBase = (g_specialRoomLightR | 4) << 28;
                    BYTE specComp = (BYTE)(g_specialRoomLightState >> 7);
                    
                    // Apply special room color filter
                    int r = g_SpecialR1 & specComp;
                    int g = g_SpecialG1 & specComp;
                    int b = g_SpecialB1 & specComp;
                    
                    // Stage 2, Room 5, Camera 3 special handling
                    if ((g_STAGE_ID == 2) && (g_ROOM_ID == 5) && (g_roomCamera_id == 3)) {
                        if ((r & 0x80) == 0) r <<= 1; else r = 0xFF;
                        if ((g & 0x80) == 0) g <<= 1; else g = 0xFF;
                        if ((b & 0x80) == 0) b <<= 1; else b = 0xFF;
                    }
                    
                    g_specialRoomLightState += g_specialRoomLightDelta;
                    
                    // Special room handling (Stage 3, Room 0x11, various cameras)
                    if ((g_STAGE_ID == 3) && (g_ROOM_ID == 0x11)) {
                        RectDrawDesc specRect = {};
                        specRect.r = r; specRect.g = g; specRect.b = b;
                        draw_rect(&specRect, 0x14, 0);
                        
                        if ((g_roomCamera_id == 4) || (g_roomCamera_id == 3)) {
                            fadeBlend = 0x14;
                        } else {
                            fadeBlend = 0x31;
                        }
                        
                        RectDrawDesc specRect2 = {};
                        specRect2.r = r; specRect2.g = g; specRect2.b = b;
                        draw_rect(&specRect2, fadeBlend, 0);
                    } else {
                        BOOL useSpecialFade = TRUE;
                        
                        // Stage 2 special room checks
                        if (g_STAGE_ID == 2) {
                            if (g_ROOM_ID == 0) useSpecialFade = TRUE;
                            if (g_ROOM_ID == 1) useSpecialFade = FALSE;
                            if (g_ROOM_ID == 2) useSpecialFade = FALSE;
                            if ((g_ROOM_ID == 5) && (g_roomCamera_id == 0)) useSpecialFade = FALSE;
                            if ((g_ROOM_ID == 0xF) && (g_roomCamera_id == 1)) useSpecialFade = FALSE;
                            if ((g_ROOM_ID == 0x10) && (g_roomCamera_id == 0)) useSpecialFade = FALSE;
                        }
                        
                        if ((g_STAGE_ID == 4) && (g_ROOM_ID == 0)) {
                            useSpecialFade = FALSE;
                        }
                        
                        if (!useSpecialFade) {
                            // No fade
                        } else {
                            RectDrawDesc specRect3 = {};
                            specRect3.r = r; specRect3.g = g; specRect3.b = b;
                            draw_rect(&specRect3, 0x31, 0);
                        }
                    }
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
        Sound_Dispatch(0);
        UpdateMusicWaitState();
        
    } else {
        // 0x00429015 (alternate): Exit game screen
        sprintf(PRINT_TEXT_BUFFER, "Press F9 to exit game and return");
        PrintText8x14(16, 100, 128, 1);
        
        sprintf(PRINT_TEXT_BUFFER, "to desktop");
        g_menu_choice_id = 0x9D;
        PrintText8x14(16, 116, 128, 1);
        
        sprintf(PRINT_TEXT_BUFFER, "Or any other key to return to");
        PrintText8x14(16, 150, 128, 1);
        
        sprintf(PRINT_TEXT_BUFFER, "title screen");
        g_menu_choice_id = 0x9D;
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
        
        if (button_pressed_id != 0) {
            g_displayExitGameScreen_flag = FALSE;
        }
    }
    
    // 0x00429f64 (alternate): FMV playback state
    if ((g_main_state_flags & 0x40000) != 0) {
        g_window_rect.h = 240;
        g_main_state_flags &= ~0x40000;
        g_window_rect.w = 320;
        g_CurrentFMVID = g_currentFMVID;
        g_bMCINotifyEnabled = TRUE;
        g_window_rect.textureId = 0;
        g_window_rect.r = 0;
        g_SelectedPlayerID = g_selectedPlayerID;
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
    if (g_loopCounter == 0) {
        // Overflow - this path loops back
    
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
    }
    
    // 0x0042a0d0: Frame timing + present governor
    FrameRateGovernor();
    g_gameTimerSnapshot = Game_timer;
    Game_timer = Game_timer;
    
    return 1; // Continue game loop
}
