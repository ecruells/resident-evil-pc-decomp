// TitleScreen.cpp - Title screen rendering and state management
// All functions decompiled from Ghidra with original addresses
#include "../Globals.h"
#include "../marni/MarniSystem.h"
#include "../marni/PSXTexture.h"
#include "FileLoader.h"
#include "SpriteRenderer.h"
#include "SFXIds.h"
#include <cstdio>
#include <cstdlib>

// Title image/buffer resources
static BYTE g_displayImageBuffer[320 * 240 * 2];
// Title text atlas sprite heights
static float g_titleCurrentSprH = 60.0f;

extern void logos_state(void);

// ============================================================================
// set_display_resolution (0x00401000)
// ============================================================================
void set_display_resolution(int w, int h, int mode)
{
    g_displayWidth = w;
    g_displayHeight = h;
    g_displayMode = mode;
}

// ============================================================================
// check_save_files_exist (0x00494190)
// Returns 1 if any save files exist, 0 otherwise.
// ============================================================================
int check_save_files_exist(void)
{
    char path[260];
    for (int i = 1; i <= 8; i++) {
        sprintf(path, ".\\savedat%d.dat", i);
        FILE* f = fopen(path, "rb");
        if (f != NULL) {
            fclose(f);
            return 1;
        }
    }
    return 0;
}

// ============================================================================
// title_setup_texture_pages (0x00470970)
// Creates texture pages for button prompt images.
// ============================================================================
void title_setup_texture_pages(int slot, int mode)
{
    int slotBase = slot * 0x37C;
    int pageCount = g_VideoDriverArray_814[slotBase / 4];
    if (pageCount != 0) {
        DWORD* pageTable = (DWORD*)((BYTE*)&g_TexturePageTable_DAT + slotBase);
        for (int i = 0; i < pageCount; i++) {
            if (pageTable[i] != 0) {
                destroy_texture_page(pageTable[i]);
                pageTable[i] = 0;
            }
        }
    }

    BYTE* pageData = (BYTE*)&g_VideoDriverArray_4d0 + slot * 0x37C;
    for (int i = 0; i < 2; i++) {
        int handle = create_texture_page(pageData, (mode != 0) ? 26 : 10);
        if (handle == 0) break;
        pageData += 0x68;
    }

    g_titleTextureSlotId = slot;
}

// ============================================================================
// init_title_screen (0x004306e0)
// ============================================================================
void init_title_screen(void)
{
    g_StackPointer = 0;
    set_display_resolution(320, 240, 0);

    g_roomCamera_id = 0;

    load_file(".\\usa\\data\\title.pix", g_displayImageBuffer, 0x20);
    display_image(0, g_displayImageBuffer, 320, 240);

    title_setup_texture_pages(0, 1);

    title_reset_display_list(0);

    const char* buttonTexPath;
    if (!g_bIsSideWinderConnected) {
        buttonTexPath = ".\\usa\\data\\t_press.tim";
    } else {
        buttonTexPath = ".\\usa\\data\\t_start.tim";
    }
    load_file(buttonTexPath, g_displayImageBuffer, 0x20);

    g_titleTextureDepthData[0] = 8;
    g_titleTextureDepthData[1] = 8;
    g_titleTextureDepthData[2] = 8;
    g_titleTextureDepthData[4] = 0x1A;
    g_titleTextureDepthData[5] = 0x1A;
    g_titleTextureDepthData[6] = 0x1A;
    g_TextureBankID = 8;
    LoadTexturePage(g_displayImageBuffer, 8, 0, 12, 4, 0, 0, 0);

    {
        char dbg[256];
        sprintf(dbg, "[INIT] LoadTexturePage done, SRV[27]=%p\n", g_TexturePageSRV[27]);
        OutputDebugStringA(dbg);
    }

    g_titleLoopFlag = 1;

    if (check_save_files_exist()) {
        g_titleSelectionId = 2;
        g_main_state_flags &= ~0x40000000;
        return;
    }
    g_titleSelectionId = 1;
    g_main_state_flags &= ~0x40000000;
}

// ============================================================================
// set_title_render_param (0x0040a8e0)
// ============================================================================
void set_title_render_param(int value)
{
    g_sceneRenderParam = value;
}

// ============================================================================
// title_exit_loop (0x00430e10)
// ============================================================================
void title_exit_loop(void)
{
    g_titleLoopFlag = 0;
    g_main_state_flags = (g_main_state_flags & 0x3FFFFFFF) | 0x40000000;
}

// ============================================================================
// fade_update (0x0047b950)
// ============================================================================
void fade_update(void)
{
    if ((short)g_fading_state <= 0 && g_fading_counter != 0) {
        if ((short)g_fading_counter <= 0) {
            g_fading_state = 0x7FFF;
        } else {
            g_fading_state = 0;
        }
    }
}

// ============================================================================
// read_sidewinder_pad (0x00497e30)
// ============================================================================
int read_sidewinder_pad(void)
{
    return g_PadRawP2;
}

// ============================================================================
// TitleTextPosData and UpdateTitleTextSprite
// ============================================================================
struct TitleTextPosData {
    int vramY;
    int sprHeight;
    int screenY;
};

static const TitleTextPosData g_titleTextPosTable[3] = {
    { 0,  54, 24 },  // index 0: "PRESS ANY BUTTON"
    { 83, 70,  8 },  // index 1: "NEW GAME"
    { 175,70,  8 },  // index 2: "LOAD GAME"
};

// ============================================================================
// UpdateTitleTextSprite (0x00430d40)
// ============================================================================
void UpdateTitleTextSprite(unsigned char brightness, unsigned char selectionId)
{
    TextureDesc* td = (TextureDesc*)&g_texPrintState;

    td->flags = 0x10000000;
    if (brightness != 0x80) {
        td->flags = 0x40000000;
    }

    const TitleTextPosData* entry = &g_titleTextPosTable[selectionId];

    td->texU = 0;
    td->screenX = -130;
    td->depth = g_titleTextureDepthData[selectionId];
    td->width = 256;
    td->texV = (unsigned char)entry->vramY;
    td->height = entry->sprHeight;
    td->screenY = entry->screenY + 38;
    g_titleCurrentSprH = (float)entry->sprHeight;

    td->colorMulR = brightness;
    td->unk10 = 0;
    td->colorMulG = brightness;
    td->pivotX = 0;
    td->pivotY = 0;
    td->colorMulB = brightness;

    td->printClutTint = 0x1E0;

    display_texture(td, 2, 12, 1);
}

// ============================================================================
// update_title_options (0x00430810)
// ============================================================================
void update_title_options(void)
{
    DWORD sidewinderPress = 0;
    DWORD sidewinderState = 0;
    if (g_bIsSideWinderConnected) {
        sidewinderState = read_sidewinder_pad();
        sidewinderPress = sidewinderState & 0x10000 & ~g_PlayerPadHeldPrev;
    }
    g_PlayerPadHeldPrev = sidewinderState;

    static DWORD prevPad = 0;
    DWORD justPressed = g_PlayerPadPressed & ~prevPad;
    prevPad = g_PlayerPadPressed;

    if (g_titleMode != 0) {
        if (g_titleMode != 1) return;

        switch (g_titleOptionsFading) {
        case 0:
            g_titleOptionsFading = 1;
            g_fade_type_id = 2;
            g_fading_counter = (short)0xFC00;
            g_main_state_flags = (g_main_state_flags & 0x3FFFFFFF) | 0x80000000;
            fade_update();
            break;

        case 1:
            if (g_fading_state < 0) {
                g_titleOptionsFading = 2;
                g_titleDemoTime = 0x708;
            }
            UpdateTitleTextSprite(128, g_titleSelectionId);
            break;

        case 2:
            UpdateTitleTextSprite(128, g_titleSelectionId);

            if ((justPressed & 0xEFF) || sidewinderPress) {
                play_sfx(SFX_BANKS, SFX_TITLE_EVIL01);
                play_sfx(SFX_BANKS, 1);  // null sfx
                g_titleOptionsFading = 6;
                g_fade_type_id = 1;
                g_fading_counter = 0x7F00;
                fade_update();
                g_StackPointer = 2;
                break;
            }

            if (justPressed & 0x5100) {
                if (!(justPressed & 0x1100)) {
                    if (g_titleSelectionId == 2) g_titleSelectionId = 0;
                    g_titleSelectionId++;
                } else {
                    g_titleSelectionId--;
                    if (g_titleSelectionId == 0) {
                        g_titleSelectionId = 2;
                        g_titleDemoTime = 0x708;
                        goto demo_reset;
                    }
                }
                g_titleDemoTime = 0x708;
            }
        demo_reset:
            g_titleDemoTime--;
            if (g_titleDemoTime != 0) break;
            g_titleOptionsFading = 3;
            g_fade_type_id = 2;
            g_fading_counter = 0x400;
            fade_update();
            break;

        case 3:
            if (g_fading_state < 0) {
                g_titleSelectionId = 0;
                title_exit_loop();
                break;
            }
            UpdateTitleTextSprite(0x80, g_titleSelectionId);
            if ((g_PlayerPadPressed & 0xEFF) == 0) break;
            g_titleOptionsFading = 0;
            g_fading_counter = (short)0xF000;
            break;

        case 4:
            if (g_fading_state < 0) {
                title_exit_loop();
                g_main_state_flags &= 0xC0000000;
                break;
            }
            UpdateTitleTextSprite(0x80, g_titleSelectionId);

        case 6:
            if (g_fading_state < 0) {
                g_titleOptionsFading = 7;
                g_fade_type_id = 1;
                g_fading_counter = (short)0xC000;
                fade_update();
                UpdateTitleTextSprite(0x80, g_titleSelectionId);
                break;
            }

        case 8:
            if (g_fading_state < 0) {
                g_titleOptionsFading = 9;
                g_fade_type_id = 1;
                g_fading_counter = (short)0xF800;
                fade_update();
                UpdateTitleTextSprite(0x80, g_titleSelectionId);
                break;
            }
            break;

        case 7:
            if (g_fading_state < 0) {
                g_titleOptionsFading = 8;
                g_fade_type_id = 1;
                g_fading_counter = (short)0x8000;
                fade_update();
                UpdateTitleTextSprite(0x80, g_titleSelectionId);
            }
            break;

        case 9:
            if (g_fading_state < 0) {
                g_titleOptionsFading = 4;
                g_fade_type_id = 2;
                g_fading_counter = 0x270;
                fade_update();
                UpdateTitleTextSprite(0x80, g_titleSelectionId);
            }
            break;
        }

        if (g_titleOptionsFading == 4 || g_titleOptionsFading >= 6) {
            UpdateTitleTextSprite(0x80, g_titleSelectionId);
        }
        return;
    }

    switch (g_titleOptionsFading) {
    case 0:
        g_titleOptionsFading = 1;
        g_titleDemoTime = 0x80;
    case 1:
        g_titleDemoTime -= 4;
        UpdateTitleTextSprite(-0x80 - (char)g_titleDemoTime, 0);
        if (g_titleDemoTime == 0) {
            g_titleOptionsFading = 2;
            g_titleDemoTime = 0x708;
        }
        if ((justPressed & 0xEFF) || sidewinderPress) {
            g_titleOptionsFading = 2;
            g_titleDemoTime = 0x708;
        }
        break;

    case 2:
        g_titleDemoTime--;
        UpdateTitleTextSprite(0x80, 0);
        if (g_titleDemoTime == 0) {
            g_titleOptionsFading = 3;
            g_fade_type_id = 2;
            g_fading_counter = 0x400;
            fade_update();
        }
        if ((justPressed & 0xEFF) || sidewinderPress) {
            g_titleMode = 1;
            g_titleOptionsFading = 2;
            g_titleDemoTime = 0x708;
        }
        break;

    case 3:
        if (0x7B80 < g_fading_state) {
            g_fading_state = 0x7FFF;
            g_fading_counter = 0;
            g_titleSelectionId = 0;
            g_main_state_flags = (g_main_state_flags & 0x3FFFFFFF) | 0x40000000;
            title_exit_loop();
        }
        if ((justPressed & 0xEFF) || sidewinderPress) {
            g_titleOptionsFading = 2;
            g_fading_state = -1;
            g_titleDemoTime = 0x708;
        }
        UpdateTitleTextSprite(0x80, 0);
        break;

    case 4:
        UpdateTitleTextSprite(0x80, 0);
        if (0x7B80 < g_fading_state) {
            g_titleMode = 1;
            g_titleOptionsFading = 0;
            g_fading_state = 0x7FFF;
            g_fading_counter = 0;
            g_main_state_flags = (g_main_state_flags & 0x3FFFFFFF) | 0x40000000;
        }
        break;
    }
}

// ============================================================================
// title_state (0x00430470)
// Title screen state: displays title, waits for player selection,
// then chains to game_start, logos_state, or characterSelectionScreen.
// ============================================================================
void title_state(void)
{
    int i;

    g_main_state_flags &= ~0x10000;
    g_PlayerPadHeldPrev = 0;
    g_playingGameFlag = 0;
    g_menu_choice_id = 0;

    setMenuScreenOffset(320, 240, 0, 0, 0);
    CenterScreenOrigin();
    clear_textures();
    set_title_render_param(0x11000000);
    sounds_reset();

    g_loadDataDestPointer = g_image_buffer;
    LoadSoundBank(BANK_TITLE, g_image_buffer);

    g_fading_state = -1;
    g_titleLoopFlag = 0;
    g_SpecialRoomLightState = (short)0xFFFF;
    g_titleMode = 0;
    g_titleOptionsFading = 0;

    init_title_screen();
    reset_title_pad_state(0);
    title_init_render_state((void*)0, 0, 0, 0);

    g_main_state_flags = (g_main_state_flags & 0x3FFFFFFF) | 0x40000000;
    Sound_Dispatch(0);

    setMenuScreenOffset(320, 240, 0, 0, 1);
    Task_sleep(1);

    g_main_state_flags = (g_main_state_flags & 0x3FFFFFFF) | 0x80000000;
    Task_sleep(1);

    do {
        update_title_options();
        Task_sleep(1);
    } while (g_titleLoopFlag != 0);

    if (g_GPU_VENDOR_ID == 1) {
        for (i = 180; i != 0; i--) {
            Task_sleep(1);
        }
    }

    cleanup_texture_slot(12);

    switch (g_titleSelectionId) {
    case 0:
        title_select_sfx();
        g_InputFlags |= 0x10000000;
        Task_chain((void*)game_start);
        Task_chain((void*)logos_state);
        return;

    case 1:
        title_select_sfx();
        Task_chain((void*)characterSelectionScreen);

    case 2:
    case 3:
        g_main_state_flags = (g_main_state_flags & 0x3FFFFFFF) | 0x40000000;
        save_load_game_state(1, 0x80180000, 0, 1, 0);
        g_loadSaveStateFlag = 0;
        Game_timer = g_gameTimerSnapshot;
        g_main_state_flags = (g_main_state_flags & 0x3FFFFFFF) | 0x40000000;
        title_select_sfx();
        Task_chain((void*)game_start);

    default:
        return;
    }
}
