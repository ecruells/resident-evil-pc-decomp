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
#include "../system/AssetPath.h"

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
        sprintf(path, "%ssavedat%d.dat", GetSaveRoot(), i);
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
    // Same legacy-descriptor caveat as TextureLoader: these tables are indexed
    // by slot * 0x37C and the port's stand-ins are a few KB, so the room-load
    // calls (load_room_bg passes the camera index, 0..7) run off the end. The
    // DX11 path takes page state from the g_TexturePage* arrays, so reads that
    // fall out of range can yield 0 - but the destroy loop's writes must not
    // happen at all.
    int slotBase = slot * 0x37C;
    const bool cntOk   = (size_t)slotBase / sizeof(DWORD)
                         < sizeof(g_VideoDriverArray_814) / sizeof(DWORD);
    const bool tableOk = (size_t)slotBase + 8 * sizeof(DWORD) <= sizeof(g_TexturePageTable_DAT);
    const bool dataOk  = (size_t)slotBase + 2 * 0x68 <= sizeof(g_VideoDriverArray_4d0);

    int pageCount = cntOk ? g_VideoDriverArray_814[slotBase / 4] : 0;
    if (pageCount != 0 && tableOk) {
        DWORD* pageTable = (DWORD*)((BYTE*)&g_TexturePageTable_DAT + slotBase);
        for (int i = 0; i < pageCount; i++) {
            if ((size_t)slotBase + (size_t)(i + 1) * sizeof(DWORD)
                > sizeof(g_TexturePageTable_DAT)) {
                break;
            }
            if (pageTable[i] != 0) {
                destroy_texture_page(pageTable[i]);
                pageTable[i] = 0;
            }
        }
    }

    if (dataOk) {
        BYTE* pageData = (BYTE*)&g_VideoDriverArray_4d0 + slotBase;
        for (int i = 0; i < 2; i++) {
            int handle = create_texture_page(pageData, (mode != 0) ? 26 : 10);
            if (handle == 0) break;
            pageData += 0x68;
        }
    }

    g_titleTextureSlotId = slot;
}

// ============================================================================
// init_title_screen (0x004306e0)
// ============================================================================
void init_title_screen(void)
{
    g_bGameActive = 0;
    set_display_resolution(320, 240, 0);

    g_roomCameraId = 0;

    LoadFile(GAME_DATA_ROOT "data\\title.pix", g_TimImageBuffer__bitmap, 0x20);
    display_image(0, g_TimImageBuffer__bitmap, 320, 240);

    title_setup_texture_pages(0, 1);

    //empty_00470960(0);

    const char* buttonTexPath;
    if (!g_bPadConnected) {
        buttonTexPath = GAME_DATA_ROOT "data\\t_press.tim";
    } else {
        buttonTexPath = GAME_DATA_ROOT "data\\t_start.tim";
    }
    LoadFile(buttonTexPath, g_TimImageBuffer__bitmap, 0x20);

    g_titleTexturePageData[4] = 26;
    g_titleTexturePageData[0] = 8;
    g_TextureCurrentPage = 26;
    g_TextureBankID = 8;
    LoadTexturePage(g_TimImageBuffer__bitmap, 8, 0, 12, 4, 0, 0, 0);

    g_titleTexturePageData[1] = g_TextureBankID;
    g_titleLoopFlag = 1;
    g_titleTexturePageData[5] = g_TextureCurrentPage;
    g_titleTexturePageData[2] = g_titleTexturePageData[1];
    g_titleTexturePageData[6] = g_titleTexturePageData[5];

    {
        char dbg[256];
        sprintf(dbg, "[INIT] LoadTexturePage done, SRV[27]=%p\n", g_TexturePageSRV[27]);
        OutputDebugStringA(dbg);
    }


    if (check_save_files_exist()) {
        g_titleSelectionId = 2;
        g_main_state_flags &= ~MSF_SCREEN_MODE_MASK;
        return;
    }
    g_titleSelectionId = 1;
    g_main_state_flags &= ~MSF_SCREEN_MODE_MASK;
}

// ============================================================================
// set_scene_render_param (0x0040a8e0)
// ============================================================================
void set_scene_render_param(int value)
{
    g_sceneRenderParam = value;
}

// ============================================================================
// title_exit_loop (0x00430e10)
// ============================================================================
void title_exit_loop(void)
{
    g_titleLoopFlag = 0;
    g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;
}

// ============================================================================
// fade_update (0x0047b950)
// ============================================================================
void fade_update(void)
{
    if (g_fading_state <= 0 && g_fading_counter != 0) {
        if (g_fading_counter <= 0) {
            g_fading_state = 0x7FFF;
        } else {
            g_fading_state = 0;
        }
    }
}

// ============================================================================
// read_sidewinder_pad (0x00497e30)
// Original: return g_pMasterInputState.field466_0x200 (joystick[0].currPress)
// ============================================================================
int read_sidewinder_pad(void)
{
    return g_pMasterInputState.joysticks[0].currPress;
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
    TextureDesc* td = &g_TextureDesc;

    td->flags = 0x10000000;
    if (brightness != 0x80) {
        td->flags = 0x40000000;
    }

    const TitleTextPosData* entry = &g_titleTextPosTable[selectionId];

    td->texU = 0;
    td->screenX = -130;
    td->texturePage = g_titleTexturePageData[selectionId];
    td->width = 256;
    td->texV = (unsigned char)entry->vramY;
    td->height = entry->sprHeight;
    td->screenY = entry->screenY + 38;
    // g_titleCurrentSprH = (float)entry->sprHeight;

    td->colorMulR = brightness;
    td->clutX = 0;
    td->colorMulG = brightness;
    td->pivotX = 0;
    td->pivotY = 0;
    td->colorMulB = brightness;

    td->clutY = 0x1E0;

    display_texture(td, 2, 12, 1);
}

// ============================================================================
// update_title_options (0x00430810)
// ============================================================================
void update_title_options(void)
{
	DWORD sidewinderPress = 0;
	DWORD sidewinderState = 0;
	if (g_bPadConnected) {
		sidewinderState = read_sidewinder_pad();
		sidewinderPress = sidewinderState & 0x10000 & ~g_PlayerPadHeldPrev;
	}
	g_PlayerPadHeldPrev = sidewinderState;

    if (g_titleMode != 0) {
        if (g_titleMode != 1) return;

        switch (g_titleOptionsFading) {
        case 0:
            g_titleOptionsFading = 1;
            g_fade_type_id = 2;
            g_fading_counter = 0xFC00;
            g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_REBUILD;
            fade_update();
            return;

        case 1:
            if (g_fading_state < 0) {
                g_titleOptionsFading = 2;
                g_titleDemoTime = 0x708;
            }
            UpdateTitleTextSprite(128, g_titleSelectionId);
            return;

	case 2:
		UpdateTitleTextSprite(128, g_titleSelectionId);

		if ((g_PlayerPadPressed & 0xeff) || sidewinderPress) {
			play_sfx(SFX_BANKS, SFX_TITLE_EVIL01);
			play_sfx(SFX_BANKS, 1); // null sfx
			g_titleOptionsFading = 6;
			g_fade_type_id = 1;
			g_fading_counter = 0x7F00;
			fade_update();
			g_bGameActive = 2;
			return;
		}

		if (g_PlayerPadPressed & 0x5100) {
			if (!(g_PlayerPadPressed & 0x1100)) {
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
            return;

        case 3:
            if (g_fading_state < 0) {
                g_titleSelectionId = 0;
                title_exit_loop();
                return;
            }
            UpdateTitleTextSprite(128, g_titleSelectionId);
            if ((g_PlayerPadPressed & 0xeff) == 0) {
                return;
            }
            g_titleOptionsFading = 0;
            g_fading_counter = 0xF000;
            return;

        case 4:
            if (g_fading_state < 0) {
                title_exit_loop();
                g_main_state_flags &= MSF_SCREEN_MODE_MASK;
                return;
            }
            UpdateTitleTextSprite(128, g_titleSelectionId);

        case 6:
            if (g_fading_state < 0) {
                g_titleOptionsFading = 7;
                g_fade_type_id = 1;
                g_fading_counter = 0xC000;
                fade_update();
                UpdateTitleTextSprite(128, g_titleSelectionId);
                return;
            }

        case 8:
            if (g_fading_state < 0) {
                g_titleOptionsFading = 9;
                g_fade_type_id = 1;
                g_fading_counter = 0xF800;
                fade_update();
                UpdateTitleTextSprite(128, g_titleSelectionId);
                return;
            }
            break;

        case 7:
            if (g_fading_state < 0) {
                g_titleOptionsFading = 8;
                g_fade_type_id = 1;
                g_fading_counter = 0x8000;
                fade_update();
                UpdateTitleTextSprite(0x80, g_titleSelectionId);
                return;
            }

        case 9:
            if (g_fading_state < 0) {
                g_titleOptionsFading = 4;
                g_fade_type_id = 2;
                g_fading_counter = 0x270;
                fade_update();
                UpdateTitleTextSprite(0x80, g_titleSelectionId);
                return;
            }

        default:
            break;
        }

        UpdateTitleTextSprite(0x80, g_titleSelectionId);
        return;
    }

    switch (g_titleOptionsFading) {
        case 0:
            g_titleOptionsFading = 1;
            g_titleDemoTime = 0x80;
            goto option_selected;
        case 1:
    option_selected:
            g_titleDemoTime -= 4;
            UpdateTitleTextSprite(-0x80 - (char)g_titleDemoTime, 0);
            if (g_titleDemoTime == 0) {
                g_titleOptionsFading = 2;
                g_titleDemoTime = 0x708;
            }
            if ((g_PlayerPadPressed & 0xeff) || sidewinderPress) {
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
            if ((g_PlayerPadPressed & 0xeff) || sidewinderPress) {
                g_titleMode = 1;
                g_titleOptionsFading = 2;
            }
            break;

        case 3:
            if (g_fading_state > 0x7B80) {
                g_fading_state = 0x7FFF;
                g_fading_counter = 0;
                g_titleSelectionId = 0;
                g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;
                title_exit_loop();
            }
            if ((g_PlayerPadPressed & 0xeff) || sidewinderPress) {
                g_titleOptionsFading = 2;
                g_fading_state = -1;
                g_titleDemoTime = 0x708;
            }
            UpdateTitleTextSprite(0x80, 0);
            break;

        case 4:
            UpdateTitleTextSprite(0x80, 0);
            if (g_fading_state > 0x7B80) {
                g_titleMode = 1;
                g_titleOptionsFading = 0;
                g_fading_state = 0x7FFF;
                g_fading_counter = 0;
                g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;
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

	g_main_state_flags &= ~MSF_INTENSITY_RAMP;
	g_PlayerPadHeldPrev = 0;
	g_PlayerPadHeld = 0;
	g_RawPadHeld = 0;
	g_PlayerPadPressed = 0;
	g_playingGameFlag = 0;
	g_menu_choice_id = 0;

    setMenuScreenOffset(320, 240, 0, 0, 0);
    CenterScreenOrigin();
    clear_textures();
    set_scene_render_param(0xc0);
    sounds_reset();

    g_loadDataDestPointer = g_DataBuffer;
    LoadSoundBank(BANK_TITLE, g_DataBuffer);

    g_fading_state = -1;
    g_titleLoopFlag = 0;
    g_SpecialRoomLightState = 0xFFFF;
    g_titleMode = 0;
    g_titleOptionsFading = 0;

    init_title_screen();

    // empty_00497c10(0);
    // empty_0040abb0((void*)0, 0, 0, 0);

    g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;
    // 0x0047b950 call site: the original calls 0x00483510 here, a stub that
    // just returns 0 - call dropped

    setMenuScreenOffset(320, 240, 0, 0, 1);
    Task_sleep(1);

    if (g_fmvPlayCount < 1) {
        g_selectedFmvId = 0;
        g_fmvDataPointer = g_loadDataDestPointer;
        g_fmvPlayCount = 0x10;
        g_main_state_flags = g_main_state_flags | MSF_FMV_REQUEST;
        Task_sleep(1);
    }

    g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_REBUILD;
    Task_sleep(1);

    do {
        update_title_options();
        Task_sleep(1);
    } while (g_titleLoopFlag != 0);

    // legacy gpu wait
    if (g_GPU_VENDOR_ID == 1) {
        for (i = 180; i != 0; i--) {
            Task_sleep(1);
        }
    }

    cleanup_texture_slot(12);

    switch (g_titleSelectionId) {
    case 0:
        nullsub_0047eb80();
        g_main_state_flags2 |= MSF2_ATTRACT_DEMO;
        Task_chain((void*)game_start);
        Task_chain((void*)logos_state);
        return;

    case 1:
        nullsub_0047eb80();
        Task_chain((void*)characterSelectionScreen);

    case 2:
    case 3:
        g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;
        LoadSaveGameState(1, 0x80180000, 0, 1, 0);
        g_loadSaveStateFlag = 0;
        Game_timer = g_gameTimerSnapshot;
        g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;
        nullsub_0047eb80();
        Task_chain((void*)game_start);

    default:
        return;
    }
}

// nullsub_0047eb80 - empty no-op in the original PC build.
// likely a PS1 version function stripped during the PC port.
void nullsub_0047eb80(void) { }
