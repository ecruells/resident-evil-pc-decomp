// WindowProc.cpp - Main window procedure
// Original function: WindowProc at 0x00441170 (Ghidra)
// Adapted from Ghidra decompilation
#include "Globals.h"

// ============================================================================
// WindowProc - Handle window messages (0x00441170)
// ============================================================================
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // 0x0044119c: Check if Marni Direct3D handles this message first
    if (g_pMarniDirect3D != NULL) {
        // Call vtable[5] -> HandleWindowMessage (0x00448b60)
        typedef int (*HandleMsgFunc)(void*, HWND, UINT, WPARAM, LPARAM);
        void** vtable = *(void***)g_pMarniDirect3D;
        HandleMsgFunc handler = (HandleMsgFunc)vtable[5];
        int result = handler(g_pMarniDirect3D, hwnd, msg, wParam, lParam);
        if (result == 0) {
            return 0;
        }
    }
    
    // 0x004411d0: Message dispatch
    switch (msg) {
        // --- WM_ACTIVATE ---
        case WM_ACTIVATE:
            g_isPaused = FALSE;
            if (LOWORD(wParam) == WA_INACTIVE) {
                // 0x004411f4: Window deactivated
                if (!g_bIsSoftwareRendering && g_mciVideoDeviceID == 1) {
                    g_bMCIVideoEvent = TRUE;
                }
                PauseSounds();
            } else {
                // Window activated
                g_isPaused = TRUE;
                ResumePausedSounds();
            }
            break;
        
        // --- WM_CREATE ---
        case WM_CREATE:
            // 0x00441226: Initialize sound system
            if (g_dwSelectedDisplayAdapterID == 0) {
                ProbeWaveOutDevicesAndCacheVolume();
            }
            StartSoundSystemAsync(hwnd);
            break;
        
        // --- WM_DESTROY ---
        case WM_DESTROY:
            // 0x00441250: Cleanup and quit
            CleanupVideoConfigAndSaveAllSettings();
            g_hWnd = NULL;
            if (g_dwSelectedDisplayAdapterID == 0) {
                RestoreWaveOutVolume();
            }
            g_bQuitFlag = TRUE;
            PostQuitMessage(0);
            break;
        
        // --- WM_PAINT ---
        case WM_PAINT:
            {
                PAINTSTRUCT ps;
                BeginPaint(hwnd, &ps);
                EndPaint(hwnd, &ps);
            }
            break;
        
        // --- WM_KEYDOWN ---
        case WM_KEYDOWN:
            // 0x004412c8
            OnKeyDown(hwnd, wParam);
            break;
        
        // --- WM_KEYUP ---
        case WM_KEYUP:
            // 0x004412d2: Check for PrintScreen
            if (wParam == VK_SNAPSHOT) {
                CreateTimestampedLogFile();
            }
            break;
        
        // --- WM_SYSCOMMAND ---
        case WM_SYSCOMMAND:
            // 0x00441300: Handle restore for software mode
            if (g_bIsSoftwareRendering && (wParam == SC_RESTORE)) {
                ShowWindow(hwnd, SW_HIDE);
                ShowWindow(hwnd, SW_SHOW);
                ShowWindow(hwnd, SW_SHOWDEFAULT);
                UpdateWindow(hwnd);
                SetForegroundWindow(hwnd);
            }
            break;
        
        // --- MM_MCINOTIFY (MCI video notification) ---
        case MM_MCINOTIFY:
            // 0x00441330
            if ((wParam == MCI_NOTIFY_SUCCESSFUL) && (g_mciVideoDeviceID == 1)) {
                g_bMCIVideoEvent = TRUE;
            }
            break;
    }
    
    // 0x00441340: Default processing
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}
