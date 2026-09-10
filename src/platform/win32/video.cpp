// video.cpp - FMV backend: Windows MCI (avivideo).
//
// Moved out of src/video/VideoPlayback.cpp during Phase 7. The 4-state machine,
// the skip masks and the prologue scenario cut stay in that shared file; this
// is only the decoder/presenter it drives. MCI renders the movie directly into
// g_hWnd, so plat_video_tick() has nothing to do here - the window itself is
// the video surface, exactly as in the original.
#include "../platform.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>

#include "../../Globals.h"

#pragma comment(lib, "winmm.lib")

// MM_MCINOTIFY is delivered to the window procedure, which sets g_bMCIVideoEvent
// (see window_proc.cpp). MCI ownership of the device is tracked here.
static BOOL  s_windowCreated = FALSE;
static BOOL  s_savedFullScreen = FALSE;

// ============================================================================
// MCISend - Helper to send an MCI string command and optionally report failure
// ============================================================================
static MCIERROR MCISend(const char* cmd, BOOL showError)
{
    char buf[256];
    MCIERROR err = mciSendStringA(cmd, buf, sizeof(buf), g_hWnd);
    if (err != 0 && showError) {
        mciGetErrorStringA(err, buf, sizeof(buf));
        plat_debug_output("[VIDEO] MCI error: ");
        plat_debug_output(buf);
        plat_debug_output("\n");
    }
    return err;
}

// ============================================================================
// CloseAll - Close the MCI AVI device entirely.
// NOTE: the MCI AVI video renders directly into g_hWnd (see OpenAndPlay:
// "window movie handle <g_hWnd>"), so we must NOT send "window movie state
// hide" here - that would hide the entire game window. The stale backbuffer
// flash is handled by presenting a fresh black frame in state 3.
// ============================================================================
static void CloseAll(void)
{
    MCISend("close all", FALSE);
    s_windowCreated = FALSE;
    g_mciVideoDeviceID = 0;
}

BOOL plat_video_init(void)
{
    s_windowCreated = FALSE;
    g_mciVideoDeviceID = 0;

    // Test if we can open the MCI AVI driver.
    MCIERROR err = mciSendStringA("open avivideo alias _test_", NULL, 0, NULL);
    if (err == 0) {
        mciSendStringA("close _test_", NULL, 0, NULL);
        return TRUE;
    }
    plat_debug_output("[VIDEO] OpenMCIAviVideo failed - MCI AVI driver not available\n");
    return FALSE;
}

BOOL plat_video_open_and_play(const char* filePath, int playTo)
{
    CloseAll();

    // Convert to absolute path (MCI requires it).
    char absPath[MAX_PATH];
    if (GetFullPathNameA(filePath, sizeof(absPath), absPath, NULL) == 0) {
        strcpy_s(absPath, sizeof(absPath), filePath);
    }

    if (GetFileAttributesA(absPath) == INVALID_FILE_ATTRIBUTES) {
        plat_debug_output("[VIDEO] Video file not found: ");
        plat_debug_output(absPath);
        plat_debug_output("\n");
        return FALSE;
    }

    char cmd[512];
    sprintf_s(cmd, sizeof(cmd), "open \"%s\" type avivideo alias movie", absPath);

    MCIERROR err = mciSendStringA(cmd, NULL, 0, g_hWnd);
    if (err != 0) {
        char errorBuf[256];
        mciGetErrorStringA(err, errorBuf, sizeof(errorBuf));
        plat_debug_output("[VIDEO] Failed to open video: ");
        plat_debug_output(absPath);
        plat_debug_output(" - ");
        plat_debug_output(errorBuf);
        plat_debug_output("\n");
        return FALSE;
    }

    // Set the video window as a child of the game window, then show it.
    sprintf_s(cmd, sizeof(cmd), "window movie handle %u", (UINT)(UINT_PTR)g_hWnd);
    mciSendStringA(cmd, NULL, 0, g_hWnd);
    mciSendStringA("window movie state show", NULL, 0, g_hWnd);

    s_windowCreated = TRUE;

    // Fill the whole client area. The MCI AVI video is always 320x240; MCI
    // handles centering within the destination.
    RECT clientRect;
    GetClientRect(g_hWnd, &clientRect);
    sprintf_s(cmd, sizeof(cmd), "put movie destination at 0 0 %d %d",
              clientRect.right - 1, clientRect.bottom - 1);
    mciSendStringA(cmd, NULL, 0, g_hWnd);

    // The cut points are frame numbers; MCIAVI already defaults to the frames
    // time format, but pin it so a driver default cannot reinterpret them.
    mciSendStringA("set movie time format frames", NULL, 0, g_hWnd);

    if (playTo > 0) {
        sprintf_s(cmd, sizeof(cmd), "play movie from 0 to %d notify", playTo);
    } else {
        sprintf_s(cmd, sizeof(cmd), "play movie notify");
    }
    err = mciSendStringA(cmd, NULL, 0, g_hWnd);
    if (err != 0) {
        plat_debug_output("[VIDEO] MCI play failed\n");
        g_mciVideoDeviceID = 0;
        return FALSE;
    }

    g_mciVideoDeviceID = 1;
    return TRUE;
}

// ============================================================================
// PlayFrom - Resume the already-open movie at playFrom through to the end.
// Mirrors video_mci_window_helper(playFrom, 0): the device stays open, so this
// is a second MCI_PLAY on the same alias. Failure zeroes g_mciVideoDeviceID,
// which lets the state machine fall through to cleanup just as the original
// helper does.
// ============================================================================
void plat_video_play_from(int playFrom)
{
    char cmd[128];
    sprintf_s(cmd, sizeof(cmd), "play movie from %d notify", playFrom);
    if (mciSendStringA(cmd, NULL, 0, g_hWnd) != 0) {
        plat_debug_output("[VIDEO] MCI resume-play failed\n");
        g_mciVideoDeviceID = 0;
        return;
    }
    g_mciVideoDeviceID = 1;
}

void plat_video_stop(void)
{
    MCISend("stop movie", FALSE);
    g_mciVideoDeviceID = 0;
}

void plat_video_close(void)
{
    CloseAll();
}

BOOL plat_video_is_active(void)
{
    return (g_mciVideoDeviceID != 0);
}

BOOL plat_video_take_end_event(void)
{
    if (!g_bMCIVideoEvent) {
        return FALSE;
    }
    g_bMCIVideoEvent = FALSE;
    return TRUE;
}

void plat_video_tick(void)
{
    // MCI paints the window itself; nothing to present.
}

// ============================================================================
// SetWindowMode - the original widens/borders the window around the movie for
// the two adapter ids that use a GDI overlay (5 and 7), then restores it. MCI
// renders into the window, so the style matters here; on Linux it does not.
// ============================================================================
void plat_video_set_window_mode(BOOL forVideo)
{
    if (g_dwSelectedDisplayAdapterID != 5 && g_dwSelectedDisplayAdapterID != 7) {
        return;
    }
    if (forVideo) {
        s_savedFullScreen = g_bFullScreen;
        LONG_PTR style = GetWindowLongA(g_hWnd, GWL_STYLE);
        if (!g_bFullScreen) {
            style = (style & 0xFFFAFFFF) | 0xC00000;
        } else {
            style = style & 0xFF3AFFFF;
        }
        SetWindowLongA(g_hWnd, GWL_STYLE, style);
    } else {
        LONG_PTR style = GetWindowLongA(g_hWnd, GWL_STYLE);
        SetWindowLongA(g_hWnd, GWL_STYLE, style & 0xFFFAFFFF | 0xC00000);
    }
}
