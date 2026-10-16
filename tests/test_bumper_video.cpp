// Silent decoder integration check; a console test, never starts the game.
// Pass a short H.264/yuv420p/24fps MP4 fixture. Link with the platform backend
// and the game objects (on Windows use a console entry point).
#include "../src/platform/platform.h"
#include "../src/game/mods/ZombieBumperBarrier.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(_WIN32)
#include <objbase.h>
#endif
static void require(bool ok, const char* message)
{
    if (!ok) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}
int main(int argc, char** argv)
{
    signed char ready[4] = { 0, -1, -1, -1 };
    require(zm_bumper_barrier_phase(0, ready, 3) == 0, "wait for every seat to arrive");
    ready[1] = 0;
    require(zm_bumper_barrier_phase(0, ready, 3) == 1, "all arrived begins preloading");
    ready[0] = 1;
    require(zm_bumper_barrier_phase(1, ready, 3) == 1, "one loaded copy cannot begin playback");
    ready[1] = 1;
    require(zm_bumper_barrier_phase(1, ready, 3) == 2, "all loaded begins playback");
    ready[0] = 2;
    require(zm_bumper_barrier_phase(2, ready, 3) == 2, "one finished copy cannot begin gameplay");
    ready[1] = 2;
    require(zm_bumper_barrier_phase(2, ready, 3) == 3, "all finished releases gameplay");
    ready[0] = ready[1] = 0;
    require(zm_bumper_barrier_phase(3, ready, 3) == 3, "late packets cannot rewind release");
    ready[0] = 2; ready[1] = -1;
    require(zm_bumper_barrier_phase(2, ready, 1) == 3, "removed seats do not hold gameplay");
    ready[0] = ready[1] = ready[2] = 1; ready[3] = 0;
    require(zm_bumper_barrier_phase(1, ready, 15) == 1, "AI host and three survivors all participate");
    ready[3] = 1;
    require(zm_bumper_barrier_phase(1, ready, 15) == 2, "four loaded seats start together");
    puts("Bumper start barrier checks passed.");
    require(argc == 2, "provide a short silent H.264 MP4 fixture");
#if defined(_WIN32)
    require(SUCCEEDED(CoInitializeEx(NULL, COINIT_MULTITHREADED)), "initialize COM");
#endif
    PlatVideoClip* clips[3];
    for (int i = 0; i < 3; ++i) {
        clips[i] = plat_video_clip_open(argv[1]);
        require(clips[i] != NULL, "open simultaneous readers");
    }
    require(!plat_video_clip_open("this-bumper-does-not-exist.mp4"), "missing asset fails cleanly");
    const unsigned char* rgba = NULL; int w = 0, h = 0;
    unsigned char* reference = NULL;
    // Read one picture through the existing FMV/portrait path while clips are
    // open. Independent silent decoders must not own that global state.
    require(plat_video_read_frame(argv[1], 0, &reference, &w, &h), "legacy frame extraction stays available");
    for (int ms = 0; ms <= 200; ms += 25) {
        unsigned char* expected = NULL;
        for (int i = 0; i < 3; ++i) {
            int cw, ch;
            require(plat_video_clip_frame(clips[i], ms, &rgba, &cw, &ch), "decode 24 fps without early EOF");
            require(rgba && cw == w && ch == h, "valid frame dimensions");
            size_t bytes = (size_t)w*h*4;
            if (ms == 0) require(memcmp(reference, rgba, bytes) == 0, "orientation and colors match legacy decoder");
            if (i == 0) {
                expected = (unsigned char*)malloc(bytes);
                require(expected != NULL, "allocate reference");
                memcpy(expected, rgba, bytes);
            } else require(memcmp(expected, rgba, bytes) == 0, "independent readers agree on shared clock");
        }
        free(expected);
    }
    free(reference);
    for (int i = 0; i < 3; ++i) {
        require(!plat_video_clip_frame(clips[i], 10000, &rgba, &w, &h), "EOF reached");
        require(rgba != NULL, "final picture retained for shorter panels");
        unsigned char first = rgba[0];
        require(!plat_video_clip_frame(clips[i], 12000, &rgba, &w, &h) && rgba[0] == first,
                "last frame stays stable after EOF");
        plat_video_clip_close(clips[i]);
    }
    plat_video_clip_close(NULL);
    PlatVideoClip* reopened = plat_video_clip_open(argv[1]);
    require(reopened != NULL, "reopen after cleanup");
    plat_video_clip_close(reopened);
    puts("Silent bumper decoder checks passed (3 simultaneous readers, 24 fps, EOF, missing asset, legacy isolation).");
#if defined(_WIN32)
    CoUninitialize();
#endif
    return 0;
}
