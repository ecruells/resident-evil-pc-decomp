// Port-added PS2 entry point. SDL2main initializes the IOP and filesystem
// drivers. The main loop follows the Linux port's 33 ms tick and FMV gate.
#include "../../Globals.h"
#include "../../marni/MarniDX.h"
#include "../../marni/MarniSystem.h"
#include "../../system/AssetPath.h"
#include "../../system/ConfigFile.h"
#include "../../system/HdTextures.h"
#include "../platform.h"
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstring>
#include <kernel.h>
#include <string>

extern void re1_ps2_set_exe_path(const char *);
extern void CleanupSoundManagerResources(void);
extern bool re1_movie_audio_ready();
extern bool re1_movie_audio_push(const float *, unsigned);
namespace
{
bool displayPending = false, restartRequested = false;
int taskChecks = 0;
void ChainedTask()
{
    ++taskChecks;
    Task_exit();
}
void ProbeTask()
{
    volatile unsigned local[16];
    for (unsigned i = 0; i < 16; ++i)
        local[i] = 0x12340000 + i;
    for (int pass = 0; pass < 3; ++pass)
    {
        Task_sleep(1);
        for (unsigned i = 0; i < 16; ++i)
            if (local[i] != 0x12340000 + i)
                plat_fatal("Task stack damaged");
        ++taskChecks;
    }
    Task_chain(reinterpret_cast<void *>(ChainedTask));
}
// Small port-owned diagnostic font: rows, MSB on the left, A-Z then 0-9.
const unsigned char glyphs[][7] = {
    {14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30}, {14, 17, 16, 16, 16, 17, 14},
    {30, 17, 17, 17, 17, 17, 30}, {31, 16, 16, 30, 16, 16, 31}, {31, 16, 16, 30, 16, 16, 16},
    {14, 17, 16, 23, 17, 17, 15}, {17, 17, 17, 31, 17, 17, 17}, {31, 4, 4, 4, 4, 4, 31},
    {7, 2, 2, 2, 18, 18, 12},     {17, 18, 20, 24, 20, 18, 17}, {16, 16, 16, 16, 16, 16, 31},
    {17, 27, 21, 21, 17, 17, 17}, {17, 25, 21, 19, 17, 17, 17}, {14, 17, 17, 17, 17, 17, 14},
    {30, 17, 17, 30, 16, 16, 16}, {14, 17, 17, 17, 21, 18, 13}, {30, 17, 17, 30, 20, 18, 17},
    {15, 16, 16, 14, 1, 1, 30},   {31, 4, 4, 4, 4, 4, 4},       {17, 17, 17, 17, 17, 17, 14},
    {17, 17, 17, 17, 17, 10, 4},  {17, 17, 17, 21, 21, 21, 10}, {17, 17, 10, 4, 10, 17, 17},
    {17, 17, 10, 4, 4, 4, 4},     {31, 1, 2, 4, 8, 16, 31},     {14, 17, 19, 21, 25, 17, 14},
    {4, 12, 4, 4, 4, 4, 14},      {14, 17, 1, 2, 4, 8, 31},     {30, 1, 1, 14, 1, 1, 30},
    {2, 6, 10, 18, 31, 2, 2},     {31, 16, 16, 30, 1, 1, 30},   {14, 16, 16, 30, 17, 17, 14},
    {31, 1, 2, 4, 8, 8, 8},       {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 1, 14}};
void Text(int x, int y, const char *s, DWORD color = 0xffffffff)
{
    for (; *s; ++s, x += 6)
    {
        int index = *s >= 'A' && *s <= 'Z' ? *s - 'A' : *s >= '0' && *s <= '9' ? 26 + *s - '0' : -1;
        if (index < 0)
            continue;
        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < 5; ++col)
                if (glyphs[index][row] & (16 >> col))
                    Marni_DX()->DrawRect(x + col, y + row, 1, 1, color);
    }
}
int Diagnostic(bool timed)
{
    TaskScheduler_Reset();
    taskChecks = 0;
    Task_execute(0, reinterpret_cast<void *>(ProbeTask));
    for (int i = 0; i < 6; ++i)
        TaskScheduler_Update();
    bool scheduler = taskChecks == 4;
    fprintf(stderr, "[PS2 TEST] scheduler sleep/chain/stack: %s\n", scheduler ? "PASS" : "FAIL");
    TaskScheduler_Reset();
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    SDL_GameController *pad = nullptr;
    for (int i = 0; i < SDL_NumJoysticks() && !pad; ++i)
        if (SDL_IsGameController(i))
            pad = SDL_GameControllerOpen(i);
    float tone[4410 * 2];
    for (int i = 0; i < 4410; ++i)
        tone[i * 2] = tone[i * 2 + 1] = (i % 100 < 50 ? .1f : -.1f);
    bool audio = re1_movie_audio_ready() && re1_movie_audio_push(tone, 4410);
    bool running = true;
    for (int frame = 0; running && (!timed || frame < 180); ++frame)
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
            if (e.type == SDL_QUIT)
                running = false;
        bool pressed = pad && SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A);
        if (pad && SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START))
            running = false;
        auto *dx = Marni_DX();
        dx->Clear(.03f, .03f, .06f, 1);
        Text(40, 20, "RESIDENT EVIL PS2");
        Text(40, 36, "SDL2 DIAGNOSTIC");
        Text(16, 60, scheduler ? "TASK SLEEP AND CHAIN PASS" : "TASK TEST FAILED",
             scheduler ? 0xff40ff70 : 0xffff4040);
        Text(16, 76, audio ? "AUDIO DEVICE OPEN TONE SENT" : "AUDIO DEVICE FAILED");
        Text(16, 92, pad ? "CONTROLLER CONNECTED" : "CONTROLLER NOT FOUND");
        Text(16, 108, pressed ? "CROSS PRESSED" : "PRESS CROSS TO TEST INPUT");
        Text(16, 128, "NO GAMEPLAY VALIDATED BY THIS TEST");
        Text(16, 144, "GAME NEEDS USA OR JPN ASSETS");
        Text(16, 160, "START EXITS");
        dx->DrawRect(16, 186, 80, 24, 0xffff3030);
        dx->DrawRect(120, 186, 80, 24, 0xff30ff30);
        dx->DrawRect(224, 186, 80, 24, 0xff3030ff);
        dx->DrawRect(16 + (frame % 280), 220, 8, 4, 0xffffffff);
        dx->Present();
        SDL_Delay(16);
    }
    if (pad)
        SDL_GameControllerClose(pad);
    return scheduler && audio ? 0 : 1;
}
bool HasAssets()
{
    char rooted[1024], path[1024];
    std::string name =
        std::string(GAME_DATA_ROOT) + (GetAssetVersion() ? "data/font.tim" : "data/fontus.tim");
    const char *resolved = ResolveAssetRoot(name.c_str(), rooted, sizeof(rooted));
    FILE *file = fopen(plat_normalize_path(resolved, path, sizeof(path)), "rb");
    if (!file)
    {
        fprintf(stderr, "[PS2] missing asset: %s\n", path);
        return false;
    }
    fclose(file);
    return true;
}
} // namespace
void plat_display_request_apply(void)
{
    displayPending = true;
}
void plat_request_quit(void)
{
    g_bQuitFlag = TRUE;
}
void plat_request_restart(void)
{
    restartRequested = true;
    g_bQuitFlag = TRUE;
}
BOOL plat_display_desktop_size(int *w, int *h)
{
    if (w)
        *w = 320;
    if (h)
        *h = 240;
    return TRUE;
}

int main(int argc, char **argv)
{
    crashlog_install();
    re1_ps2_set_exe_path(argc ? argv[0] : nullptr);
    bool selftest = false;
    for (int i = 1; i < argc; ++i)
        if (!strcmp(argv[i], "--selftest"))
            selftest = true;
    ConfigFile_EnsureExists();
    ConfigFile_Load();
    // Fixed memory profile for the 32 MB EE. Original art and fonts remain.
    g_dwScreenWidth = 320;
    g_dwScreenHeight = 240;
    g_bFullScreen = TRUE;
    g_bVSync = FALSE;
    HdTextures_Configure(false, false);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0)
    {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *window = SDL_CreateWindow("RESIDENT EVIL", SDL_WINDOWPOS_UNDEFINED,
                                          SDL_WINDOWPOS_UNDEFINED, 320, 240, SDL_WINDOW_SHOWN);
    if (!window)
    {
        fprintf(stderr, "SDL window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    g_hWnd = window;
    TaskScheduler_Init();
    StartSoundSystemAsync(window);
    InitializeMarniSystem();
    if (!IsGraphicsSystemReadyForOperation())
    {
        fprintf(stderr, "PS2 graphics init failed\n");
        return 1;
    }
    int result = 0;
    if (selftest || !HasAssets())
        result = Diagnostic(selftest);
    else
    {
        g_bWindowFocused = g_bWindowActive = TRUE;
        g_bQuitFlag = FALSE;
        g_dwGameTimer1 = plat_time_ms();
        while (!g_bQuitFlag)
        {
            SDL_Event e;
            while (SDL_PollEvent(&e))
            {
                if (e.type == SDL_QUIT)
                    g_bQuitFlag = TRUE;
                if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP)
                    plat_key_event(e.key.keysym.scancode, e.type == SDL_KEYDOWN);
            }
            if (displayPending)
            {
                displayPending = false;
                g_dwScreenWidth = 320;
                g_dwScreenHeight = 240;
            }
            DWORD now = plat_time_ms();
            if (DWORD(now - g_dwGameTimer1) < 33)
            {
                SDL_Delay(1);
                continue;
            }
            g_dwGameTimer1 = now;
            if (g_bMCINotifyEnabled)
            {
                if (g_bMCINotifyFlag)
                {
                    UpdateVideoPlayback();
                    continue;
                }
                g_bMCINotifyFlag = TRUE;
            }
            if (main_loop() == 0)
                break;
            if (DWORD(now - g_dwSystemTimer1) > 1000)
                g_dwSystemTimer1 = now;
        }
    }
    CleanupVideoConfigAndSaveAllSettings();
    plat_video_close();
    CleanupSoundManagerResources();
    MarniDX_DestroyGlobal();
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (restartRequested && argc > 0 && argv[0])
        LoadExecPS2(argv[0], argc - 1, argv + 1);
    return result;
}
