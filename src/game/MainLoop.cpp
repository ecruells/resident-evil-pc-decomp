// main_loop.cpp - Main game loop
// Original function: main_loop at 0x00428eb0 (Ghidra)
// Adapted from Ghidra decompilation
#include "Globals.h"

// Sprite animation/screen tint state (original addresses 0x00be41d0-0x00be41d4)
// These are global in the original binary; game_start resets g_spriteAnimIntensity
// to prevent stale white-flash overlays from the character-select fade transition.
int g_spriteAnimActive;             // 0x00be41d0
int g_spriteAnimR;                  // 0x00be41d1
int g_spriteAnimG;                  // 0x00be41d2
int g_spriteAnimB;                  // 0x00be41d3
short g_spriteAnimIntensity;        // 0x00be41d4

static int g_pressF9Flag;           // 0x004ba718
static int g_fading_007d9048;       // 0x007d9048
static int g_fading_007d904c;       // 0x007d904c
static int g_MaxHealthDisplayFlag;  // 0x00d227c0
static RectDrawDesc g_FadingRect = { 0x60000000, -160, -120, 320, 240, 0, 0, 0 };  // 0x004ba720
static RectDrawDesc g_ColorRect  = { 0x60000000, -164, -130, 328,  38, 0, 0, 0 };  // 0x004ba730
static RectDrawDesc g_ColorRect2 = { 0x60000000, -164,   92, 328,  38, 0, 0, 0 };  // 0x004ba740

// ============================================================================
// main_loop - Main game update/rendering (0x00428eb0)
// Called once per frame by the outer Win32 message loop. No internal loop.
// ============================================================================
int main_loop(void)
{
    SetFrameRateMode(g_bGameActive != 0);

    // 0x00428ed1: Initialize game on first run
    if ((char)init_game_flag == 0) {
        init_and_start_game();
        init_game_flag = 1;
    }

    // 0x00428ee0: Frame startup - input update
    InputUpdate();
    PlayerPad_Update();

    // 0x00428eff: Check for special key combination (F9/F10/F11 scan codes)
    if (g_RawPadPressed != 0) {
        if (g_lastScanCodeOrMsgID == 0x5B || g_lastScanCodeOrMsgID == 0x5C || g_lastScanCodeOrMsgID == 0x5D) {
            DAT_00d91bc8 = 1;
            g_menu_choice_id = 0;
        }
    }

    if ((g_main_state_flags2 & 0x10000000) == 0) {
        if (g_isSideWinderConnected) {
            g_PlayerPadHeld |= 0x800;
            g_button_pressed_id |= 0x800;
            g_isSideWinderConnected = FALSE;
        }
        if (g_isPaused) {
            WORD savedButtons = (WORD)g_button_pressed_id;
            g_button_pressed_id |= 0x900;
            g_isPaused = FALSE;
            g_PlayerPadHeld = savedButtons | 0x900;
        }
    } else {
        g_isSideWinderConnected = FALSE;
        g_isPaused = FALSE;
    }

    if ((g_main_state_flags & 0x40000) != 0) {
        g_window_rect.h = 240;
        g_main_state_flags &= ~0x40000;
        g_window_rect.w = 320;
        g_CurrentFMVID = (int)g_selectedFmvId;
        g_bMCINotifyEnabled = TRUE;
        g_window_rect.textureId = 0;
        g_window_rect.r = 0;
        g_FmvCharacterId = (int)g_SelectedCharactedId;
        g_window_rect.g = 0;
        g_window_rect.b = 0;
        g_window_rect.x = -g_ScreenOffsetX;
        g_window_rect.y = -g_ScreenOffsetY;
        draw_rect(&g_window_rect, 0, 0);
        g_bMCINotifyFlag = TRUE;
        StMask(1, 0);
        goto _post;
    }

    if ((g_AttractModeIdleTimer == 0) && ((g_main_state_flags & 0x20000) == 0) &&
        (g_displayReturnToTitleScreen_Flag != 0)) {

        sprintf(PRINT_TEXT_BUFFER, "Press F9 to abort game and return to");
        PrintText8x14(16, 100, 128, 1);

        sprintf(PRINT_TEXT_BUFFER, "title screen.");
        PRINT_TEXT_BUFFER[0x0C] = 0x9d;
        PrintText8x14(16, 116, 128, 1);

        sprintf(PRINT_TEXT_BUFFER, "Or any other key to continue game.");
        PRINT_TEXT_BUFFER[0x21] = 0x9d;
        PrintText8x14(16, 150, 128, 1);

        g_window_rect.w = 320;
        g_window_rect.textureId = 0;
        g_window_rect.r = 0;
        g_window_rect.g = 0;
        g_window_rect.b = 0;
        g_window_rect.x = -g_ScreenOffsetX;
        g_window_rect.h = 240;
        g_window_rect.y = -g_ScreenOffsetY;
        draw_rect(&g_window_rect, 100, 1);

        if (g_pressF9Flag == 0) {
            PauseSounds();
        }
        g_pressF9Flag = 1;

        if (g_PlayerPadHeld != 0) {
            g_displayReturnToTitleScreen_Flag = FALSE;
            ResumePausedSounds();
        }
        goto _post;
    }

    if (g_displayExitGameScreen_flag != 0) {
        sprintf(PRINT_TEXT_BUFFER, "Press F9 to exit game and return");
        PrintText8x14(16, 100, 128, 1);

        sprintf(PRINT_TEXT_BUFFER, "to desktop.");
        PRINT_TEXT_BUFFER[0x0A] = 0x9d;
        PrintText8x14(16, 116, 128, 1);

        sprintf(PRINT_TEXT_BUFFER, "Or any other key to return to");
        PrintText8x14(16, 150, 128, 1);

        sprintf(PRINT_TEXT_BUFFER, "title screen.");
        PRINT_TEXT_BUFFER[0x0C] = 0x9d;
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

        if (g_PlayerPadHeld != 0) {
            g_displayExitGameScreen_flag = FALSE;
        }
        goto _post;
    }

    if (g_pressF9Flag != 0) {
        g_pressF9Flag = 0;
        ResumePausedSounds();
    }

    UpdateDemoTimer();

    if ((g_main_state_flags & 0x80000) != 0) {
        ResetScreenPanning();
    }
    if (g_SndFadeType != 0) {
        UpdateSoundFadeState();
    }
    if (g_SndRampFramesLeft != 0) {
        UpdateSoundDecay();
    }

    if ((g_main_state_flags & 0x20000000) != 0) {
        // Fade transition in progress
        if (g_fading_state < 0) {
            g_fading_007d9048 = 0;
            g_FadingRect.textureId = (unsigned int)(g_fade_type_id | 4) << 28;
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

        draw_rect(&g_FadingRect, 0, 0);

        TaskScheduler_Update();

        g_fading_state += g_fading_counter;

        if (g_fading_state < 0) {
            g_fading_counter = 0;
            g_main_state_flags &= ~0x20000000;
            g_fading_state = 0;
            if (g_fading_007d904c == 0) {
                Task_Resume(0);
            }
            goto _fade_done;
        }
    } else {
_fade_done:
        g_spriteAnimActive ^= 1;
        g_MaxHealthDisplayFlag = 0x7FFFFFFF;

        TaskScheduler_Update();

        if ((g_menu_choice_id & 0x80) != 0) {
            FUN_004557b0();
        }

        if (g_fading_state >= 0) {
            g_FadingRect.textureId = (unsigned int)(g_fade_type_id | 4) << 28;
            g_FadingRect.r = (BYTE)(g_fading_state >> 7);
            g_fading_state += g_fading_counter;

            int blend;
            if ((g_playerEntity.health < 0) && (g_MaxHealthDisplayFlag != 0x7FFFFFFF)) {
                blend = 100;
            } else {
                blend = 0;
            }

            g_FadingRect.g = g_FadingRect.r;
            g_FadingRect.b = g_FadingRect.r;

            draw_rect(&g_FadingRect, blend, 0);
        }
    }

    // 0x004297e0: Sprite animation intensity
    if ((g_main_state_flags & 0x10000) != 0) {
        if ((unsigned __int8)g_spriteAnimIntensity < 0xF0) {
            g_spriteAnimIntensity += 16;
        }
    } else if ((unsigned __int8)g_spriteAnimIntensity > 0x0F) {
        g_spriteAnimIntensity -= 16;
    }

    BYTE intensity = (BYTE)g_spriteAnimIntensity;
    g_ColorRect.r = intensity;
    g_ColorRect.g = intensity;
    g_ColorRect.b = intensity;
    g_ColorRect2.r = intensity;
    g_ColorRect2.g = intensity;
    g_ColorRect2.b = intensity;

    // Original PS1 GPU POLY_F4 screen-tint primitives (dead code in D3D11 port).
    // In the original binary these wrote to the PS1 ordering table for screen
    // color tinting. The modern port uses draw_rect() with g_ColorRect instead.
    // Preserved here for reference; writing to the array crashes on modern
    // Windows due to linker section placement.
    // Poly_F4_ARRAY_004ba750[g_spriteAnimActive].r0 = intensity;
    // Poly_F4_ARRAY_004ba750[g_spriteAnimActive].g0 = intensity;
    // Poly_F4_ARRAY_004ba750[g_spriteAnimActive].b0 = intensity;
    // Poly_F4_ARRAY_004ba750[g_spriteAnimActive + 2].r0 = intensity;
    // Poly_F4_ARRAY_004ba750[g_spriteAnimActive + 2].g0 = intensity;
    // Poly_F4_ARRAY_004ba750[g_spriteAnimActive + 2].b0 = intensity;

    if (g_spriteAnimIntensity == 0xF0) {
        g_ColorRect.r = 0xFF;
        g_ColorRect.g = 0xFF;
        g_ColorRect.b = 0xFF;
        g_ColorRect2.r = 0xFF;
        g_ColorRect2.g = 0xFF;
        g_ColorRect2.b = 0xFF;
    }

    if ((g_main_state_flags & 0x4008000) == 0) {
        if (g_spriteAnimIntensity != 0) {
            if ((g_stageId == 4) && (g_roomId == 0x13) && (g_roomCameraId == 5)) {
                draw_rect(&g_ColorRect, 0, 0);
                draw_rect(&g_ColorRect2, 0, 0);
            } else {
                draw_rect(&g_ColorRect, 0x28, 0);
                draw_rect(&g_ColorRect2, 0x28, 0);
            }
        }

        if (g_SpecialRoomLightState >= 0) {
            g_FadingRect.textureId = (unsigned int)(g_SpecialRoomLightR | 4) << 28;

            BYTE lightMask = (BYTE)(g_SpecialRoomLightState >> 7);
            g_FadingRect.r = g_SpecialR1 & lightMask;
            g_FadingRect.g = g_SpecialG1 & lightMask;
            g_FadingRect.b = lightMask & g_SpecialB1;

            // Stage 2, Room 5, Camera 3: double color components
            if ((g_stageId == 2) && (g_roomId == 5) && (g_roomCameraId == 3)) {
                if ((g_FadingRect.r & 0x80) == 0) {
                    g_FadingRect.r = g_FadingRect.r << 1;
                } else {
                    g_FadingRect.r = 0xFF;
                }
                if ((g_FadingRect.g & 0x80) == 0) {
                    g_FadingRect.g = g_FadingRect.g << 1;
                } else {
                    g_FadingRect.g = 0xFF;
                }
                if ((g_FadingRect.b & 0x80) == 0) {
                    g_FadingRect.b = g_FadingRect.b << 1;
                } else {
                    g_FadingRect.b = 0xFF;
                }
            }

            g_SpecialRoomLightState += g_SpecialRoomLightDelta;

            if ((g_stageId == 3) && (g_roomId == 0x11)) {
                draw_rect(&g_FadingRect, 0x14, 0);
                int blend2;
                if ((g_roomCameraId == 4) || (g_roomCameraId == 3)) {
                    blend2 = 0x14;
                } else {
                    blend2 = 0x31;
                }
                draw_rect(&g_FadingRect, blend2, 0);
            } else {
                bool useSpecialFade = true;

                if ((g_stageId == 2) && (useSpecialFade = (g_roomId != 0), g_roomId == 1)) {
                    useSpecialFade = false;
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
                    if ((g_roomId == 0xF) && (g_roomCameraId == 1)) {
                        useSpecialFade = false;
                    }
                    if ((g_roomId == 0x10) && (g_roomCameraId == 0)) {
                        useSpecialFade = false;
                    }
                }
                if ((g_stageId == 4) && (g_roomId == 0)) {
                    useSpecialFade = false;
                }

                if (useSpecialFade) {
                    draw_rect(&g_FadingRect, 0x31, 0);
                }
            }
        }
    }

    // 0x00429dc0: Screen state flags
    if ((g_main_state_flags & 0x40000000) != 0) {
        g_window_rect.w = 320;
        g_window_rect.textureId = 0;
        g_window_rect.r = g_spriteAnimR;
        g_window_rect.x = -g_ScreenOffsetX;
        g_window_rect.g = g_spriteAnimG;
        g_window_rect.h = 240;
        g_window_rect.y = -g_ScreenOffsetY;
        g_window_rect.b = g_spriteAnimB;
        SetScreenReadyWithDebugColor(g_spriteAnimR, g_spriteAnimG, g_spriteAnimB);
    } else if ((g_main_state_flags & 0x80000000) != 0) {
        if ((g_main_state_flags2 & 0x04) == 0) {
            ResetScreenAndRebuildSprites(g_spriteAnimActive == 0 ? 0xF0 : 0);
        } else {
            ApplyShakeAndRebuildSprites();
        }
    }

    if (((g_main_state_flags2 & 0x02) != 0) && ((g_main_state_flags >> 8) == 0)) {
        ApplyScreenShake();
    }

    if (g_ScreenAccessCheck != 0) {
        SetScreenReady(1);
    }

    empty_483510();
    UpdateMusicWaitState();

_post:
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
    DAT_004d46d4 = Game_timer;

    return 1;
}
