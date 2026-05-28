// VideoPlayback.cpp - FMV playback state machine via MCI
// Original function: UpdateVideoPlayback at 0x00474e00
// Uses Windows MCI (Media Control Interface) with mciSendStringA for simplicity

#include "../Globals.h"
#include "../marni/MarniSystem.h"
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

// ============================================================================
// FMV path table - maps FMV IDs to AVI filenames
// Original table at 0x004c39d8 in Ghidra
// ============================================================================
struct FMVEntry {
    const char* filename;
    int         field_4;
};

static const FMVEntry g_FMVTable[] = {
    { ".\\usa\\MOVIE\\OU.avi",      0 },   // 0  - Opening / title movie
    { ".\\usa\\MOVIE\\PU.avi",      0 },   // 1
    { ".\\usa\\MOVIE\\DMF.avi",     0 },   // 2
    { ".\\usa\\MOVIE\\DM3.avi",     0 },   // 3
    { ".\\usa\\MOVIE\\DM4.avi",     0 },   // 4
    { ".\\usa\\MOVIE\\DM1.avi",     0 },   // 5
    { ".\\usa\\MOVIE\\DM6.avi",     0 },   // 6
    { ".\\usa\\MOVIE\\DM7.avi",     0 },   // 7
    { ".\\usa\\MOVIE\\DM8.avi",     0 },   // 8
    { ".\\usa\\MOVIE\\DM2.avi",     0 },   // 9
    { NULL,                         0 },   // 10 - null entry
    { ".\\usa\\MOVIE\\DMB.avi",     0 },   // 11
    { ".\\usa\\MOVIE\\DMC.avi",     0 },   // 12
    { ".\\usa\\MOVIE\\DMD.avi",     0 },   // 13
    { ".\\usa\\MOVIE\\DME.avi",     0 },   // 14
    { ".\\usa\\MOVIE\\ED1.avi",     0 },   // 15
    { ".\\usa\\MOVIE\\ED2.avi",     0 },   // 16
    { ".\\usa\\MOVIE\\ED3.avi",     0 },   // 17
    { ".\\usa\\MOVIE\\EU4.avi",     0 },   // 18
    { ".\\usa\\MOVIE\\EU5.avi",     0 },   // 19
    { ".\\usa\\MOVIE\\ED6.avi",     0 },   // 20
    { ".\\usa\\MOVIE\\ED7.avi",     0 },   // 21
    { ".\\usa\\MOVIE\\ED8.avi",     0 },   // 22
    { ".\\usa\\MOVIE\\capcom.avi",  0 },   // 23 - Capcom logo
    { ".\\usa\\MOVIE\\stfc_r.avi",  0 },   // 24
    { ".\\usa\\MOVIE\\stfj_r.avi",  0 },   // 25
    { ".\\usa\\MOVIE\\stfz_r.avi",  0 },   // 26
    { ".\\usa\\MOVIE\\staf_r.avi",  0 },   // 27
    { ".\\usa\\MOVIE\\vlogo.avi",   0 },   // 28 - Virgin logo
};

static const int g_FMVTableCount = sizeof(g_FMVTable) / sizeof(g_FMVTable[0]);

// ============================================================================
// Global video playback state (file-scope, persistent across UpdateVideoPlayback calls)
// ============================================================================
static int   g_FMVPlaybackState = 0;
static BOOL  g_bIsMCIVideoOpenSuccess = FALSE;
static BOOL  g_mciWindowCreated = FALSE;
static DWORD g_videoFlagA4 = 0;
static DWORD g_videoSkipInput = 0;
static int   g_videoSkipCounter = 0;
static BOOL  g_savedFullScreen = FALSE;
static char  g_videoFilePath[MAX_PATH] = {};

// ============================================================================
// ResolveVideoPath - Remap original .\usa\ paths to .\assets\USA\ in debug builds
// ============================================================================
static const char* ResolveVideoPath(const char* originalPath, char* outPath, size_t outSize)
{
    if (originalPath == NULL || outPath == NULL || outSize == 0) return NULL;

    const char* p = originalPath;
    while (*p) {
        if ((p[0] == '\\' || p[0] == '/') &&
            (p[1] == 'u' || p[1] == 'U') &&
            (p[2] == 's' || p[2] == 'S') &&
            (p[3] == 'a' || p[3] == 'A') &&
            (p[4] == '\\' || p[4] == '/')) {
            size_t prefixLen = (size_t)(p - originalPath);
            if (prefixLen >= outSize) return NULL;
            memcpy(outPath, originalPath, prefixLen);
            char sep = p[4];
            int written = sprintf_s(outPath + prefixLen, outSize - prefixLen,
                                    "%cassets%cUSA%c", sep, sep, sep);
            if (written < 0) return NULL;
            size_t remainingLen = strlen(p + 5);
            if (prefixLen + (size_t)written + remainingLen + 1 > outSize) return NULL;
            strcpy_s(outPath + prefixLen + written, outSize - prefixLen - written, p + 5);
            return outPath;
        }
        p++;
    }

    size_t len = strlen(originalPath);
    if (len + 1 > outSize) return NULL;
    strcpy_s(outPath, outSize, originalPath);
    return outPath;
}

// ============================================================================
// CheckVideoFileExists - Check if video file exists (0x00474d90)
// ============================================================================
BOOL CheckVideoFileExists(const char* filename)
{
    if (filename == NULL) return FALSE;

    char resolvedPath[MAX_PATH];
    const char* filePath = ResolveVideoPath(filename, resolvedPath, sizeof(resolvedPath));
    if (filePath == NULL) filePath = filename;

    DWORD attrib = GetFileAttributesA(filePath);
    if (attrib == INVALID_FILE_ATTRIBUTES) {
        char msg[256];
        sprintf_s(msg, "Could not open file: %s", filename + 2);
        OutputDebugStringA("[VIDEO] ");
        OutputDebugStringA(msg);
        OutputDebugStringA("\n");
        return FALSE;
    }

    // Store resolved path for later use
    strcpy_s(g_videoFilePath, filePath);
    return TRUE;
}

// ============================================================================
// MCISend - Helper to send MCI string command and optionally check error
// ============================================================================
static MCIERROR MCISend(const char* cmd, BOOL showError)
{
    char buf[256];
    MCIERROR err = mciSendStringA(cmd, buf, sizeof(buf), g_hWnd);
    if (err != 0 && showError) {
        mciGetErrorStringA(err, buf, sizeof(buf));
        OutputDebugStringA("[VIDEO] MCI error: ");
        OutputDebugStringA(buf);
        OutputDebugStringA("\n");
    }
    return err;
}

// ============================================================================
// MCI_CloseAll - Close the MCI AVI device entirely
// ============================================================================
static void MCI_CloseAll(void)
{
    MCISend("close all", FALSE);
    g_mciWindowCreated = FALSE;
    g_mciVideoDeviceID = 0;
}

// ============================================================================
// MCI_OpenAndPlay - Open video file and start playback
// ============================================================================
static BOOL MCI_OpenAndPlay(const char* filePath)
{
    // Close any existing video
    MCI_CloseAll();

    // Convert to absolute path (MCI requires it)
    char absPath[MAX_PATH];
    if (GetFullPathNameA(filePath, sizeof(absPath), absPath, NULL) == 0) {
        strcpy_s(absPath, filePath);
    }

    // Verify file exists
    DWORD attrib = GetFileAttributesA(absPath);
    if (attrib == INVALID_FILE_ATTRIBUTES) {
        OutputDebugStringA("[VIDEO] Video file not found: ");
        OutputDebugStringA(absPath);
        OutputDebugStringA("\n");
        return FALSE;
    }

    // Open the AVI file using MCI
    char cmd[512];
    sprintf_s(cmd, "open \"%s\" type avivideo alias movie", absPath);

    MCIERROR err = mciSendStringA(cmd, NULL, 0, g_hWnd);
    if (err != 0) {
        char errorBuf[256];
        mciGetErrorStringA(err, errorBuf, sizeof(errorBuf));
        OutputDebugStringA("[VIDEO] Failed to open video: ");
        OutputDebugStringA(absPath);
        OutputDebugStringA(" - ");
        OutputDebugStringA(errorBuf);
        OutputDebugStringA("\n");
        return FALSE;
    }

    // Set video window as child of our game window
    sprintf_s(cmd, "window movie handle %u", (UINT)(UINT_PTR)g_hWnd);
    mciSendStringA(cmd, NULL, 0, g_hWnd);

    // Show the video window
    mciSendStringA("window movie state show", NULL, 0, g_hWnd);

    g_mciWindowCreated = TRUE;

    // Position the video window
    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (pD3D != NULL) {
        RECT clientRect;
        GetClientRect(g_hWnd, &clientRect);

        int vidW = (int)pD3D->m_width;
        int vidH = (int)pD3D->m_height;

        int x = 0, y = 0;
        if (vidW < clientRect.right)
            x = (clientRect.right - vidW) / 2;
        if (vidH < clientRect.bottom)
            y = (clientRect.bottom - vidH) / 2;

        sprintf_s(cmd, "put movie destination at %d %d %d %d",
                  x, y, x + vidW - 1, y + vidH - 1);
        mciSendStringA(cmd, NULL, 0, g_hWnd);
    }

    // Play the video with notification
    sprintf_s(cmd, "play movie notify");
    err = mciSendStringA(cmd, NULL, 0, g_hWnd);
    if (err != 0) {
        OutputDebugStringA("[VIDEO] MCI play failed\n");
        g_mciVideoDeviceID = 0;
        return FALSE;
    }

    g_mciVideoDeviceID = 1;
    return TRUE;
}

// ============================================================================
// OpenMCIAviVideo - Open MCI video subsystem (0x00474af0)
// ============================================================================
void OpenMCIAviVideo(void)
{
    g_mciWindowCreated = FALSE;
    g_mciVideoDeviceID = 0;

    // Test if we can open the MCI AVI driver
    MCIERROR err = mciSendStringA("open avivideo alias _test_", NULL, 0, NULL);
    if (err == 0) {
        mciSendStringA("close _test_", NULL, 0, NULL);
        g_bIsMCIVideoOpenSuccess = TRUE;
    } else {
        g_bIsMCIVideoOpenSuccess = FALSE;
        OutputDebugStringA("[VIDEO] OpenMCIAviVideo failed - MCI AVI driver not available\n");
    }
}

// ============================================================================
// UpdateVideoPlayback - FMV playback state machine (0x00474e00)
// ============================================================================
void UpdateVideoPlayback(void)
{
    if (g_bIsSoftwareRendering) {
        g_bMCINotifyEnabled = FALSE;
        return;
    }

    switch (g_FMVPlaybackState) {
    case 0: // Initialize
        {
            MarniClear();
            MarniClear();

            // Present current frame before video overlay
            if (g_pMarniDirect3D != NULL) {
                MarniPresent();
            }

            g_videoFlagA4 = 1;
            g_demoIdleTimer1 = 1;

            // Get the filename for this FMV ID
            const char* videoFile = NULL;
            if (g_CurrentFMVID >= 0 && g_CurrentFMVID < g_FMVTableCount) {
                videoFile = g_FMVTable[g_CurrentFMVID].filename;
            }

            if (videoFile != NULL && CheckVideoFileExists(videoFile)) {
                PauseGameSoundsAsync();

                // Handle window style for video playback
                if (g_dwSelectedDisplayAdapterID == 5 || g_dwSelectedDisplayAdapterID == 7) {
                    g_savedFullScreen = g_bFullScreen;
                    LONG_PTR style = GetWindowLongA(g_hWnd, GWL_STYLE);
                    if (!g_bFullScreen) {
                        style = (style & 0xFFFAFFFF) | 0xC00000;
                    } else {
                        style = style & 0xFF3AFFFF;
                    }
                    SetWindowLongA(g_hWnd, GWL_STYLE, style);
                }

                OpenMCIAviVideo();
                if (!g_bIsMCIVideoOpenSuccess) {
                    OutputDebugStringA("[VIDEO] Failed to init video system, skipping FMV\n");
                    g_bMCINotifyEnabled = FALSE;
                    g_mciVideoDeviceID = 0;
                    setMenuScreenOffset(320, 240, 0, 0, 0);
                    CenterScreenOrigin();
                    return;
                }

                g_FMVPlaybackState = 1;
            } else {
                // No video file - skip FMV
                g_bMCINotifyEnabled = FALSE;
                g_mciVideoDeviceID = 0;
                setMenuScreenOffset(320, 240, 0, 0, 0);
                CenterScreenOrigin();
            }
        }
        break;

    case 1: // Open and start playing
        {
            if (!MCI_OpenAndPlay(g_videoFilePath)) {
                g_FMVPlaybackState = 3;
                break;
            }

            g_FMVPlaybackState = 2;

            InputUpdate();
            g_videoSkipInput = g_RawPadPressed;
            g_videoSkipCounter = 100;
        }
        break;

    case 2: // Playing - monitor for skip/completion
        {
            if (g_videoSkipCounter > 0) {
                g_videoSkipCounter--;
            }

            InputUpdate();
            DWORD currentInput = g_RawPadPressed;

            // Check for skip input
            if (((currentInput & ~g_videoSkipInput) != 0) && (g_videoSkipCounter == 0)) {
                // Skip requested - stop playback
                MCISend("stop movie", FALSE);
                g_mciVideoDeviceID = 0;
            }

            g_videoSkipInput = currentInput;

            // Check for MCI notification (MM_MCINOTIFY from WindowProc)
            if (g_bMCIVideoEvent) {
                g_mciVideoDeviceID = 0;
                g_bMCIVideoEvent = FALSE;
                g_videoFlagA4 = 0;
            }

            // Check if video has ended
            if (g_mciVideoDeviceID == 0) {
                g_FMVPlaybackState = 3;

                // Restore window style
                if (g_dwSelectedDisplayAdapterID == 5 || g_dwSelectedDisplayAdapterID == 7) {
                    LONG_PTR style = GetWindowLongA(g_hWnd, GWL_STYLE);
                    SetWindowLongA(g_hWnd, GWL_STYLE, style & 0xFFFAFFFF | 0xC00000);
                }

                setMenuScreenOffset(320, 240, 0, 0, 0);
                CenterScreenOrigin();
                MCI_CloseAll();
            }
        }
        break;

    case 3: // Cleanup
        {
            MCI_CloseAll();
            ResumeGameSoundsAsync();

            g_FMVPlaybackState = 0;
            g_bMCINotifyEnabled = FALSE;
            g_demoIdleTimer1 = 0;
            StMask(3, 0);
        }
        break;
    }
}

// ============================================================================
// StartVideoPlayback - Signal software video player to start (0x004423b0)
// ============================================================================
void StartVideoPlayback(void)
{
    if (g_pSharedMemory != NULL) {
        g_pSharedMemory[6] = 1;
    }
}

// ============================================================================
// IsVideoPlaybackComplete - Check if software video player finished (0x00442310)
// ============================================================================
bool IsVideoPlaybackComplete(void)
{
    if (g_pSharedMemory != NULL) {
        return (g_pSharedMemory[3] == 1);
    }
    return false;
}

// ============================================================================
// SignalVideoSkip - Signal software video player to skip (0x00442330)
// ============================================================================
void SignalVideoSkip(void)
{
    if (g_pSharedMemory != NULL) {
        g_pSharedMemory[2] = 1;
    }
}

// ============================================================================
// SetVideoResolution - Set video display resolution (0x00497f30)
// ============================================================================
void SetVideoResolution(int width, int height)
{
    if (g_pMarniDirect3D != NULL) {
        CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
        pD3D->m_width = (DWORD)width;
        pD3D->m_height = (DWORD)height;
    }
}
