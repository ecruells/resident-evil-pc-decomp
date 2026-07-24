// Cleanup.cpp - Game cleanup and shutdown functions
// Functions: CleanupSharedMemory (0x00442230), UpdateGameStatus (0x00442350),
//            CleanupVideoConfigAndSaveAllSettings (0x00497ea0),
//            DestroyAllSoundBanks (0x004801c0), CleanupAsyncTasks (0x0041d0b0)
// Adapted from Ghidra decompilation
#include "Globals.h"
#include "marni/MarniSystem.h"

// ============================================================================
// CleanupSharedMemory - Release shared memory (0x00442230)
// ============================================================================
void CleanupSharedMemory(void)
{
    // 0x00442230
    if (g_pSharedMemory != NULL) {
        // Signal cleanup
        *g_pSharedMemory = 0;
        
        // Wait for acknowledgment
        while (g_pSharedMemory[1] == 1) {
            // Busy wait
        }
        
        Sleep(1000);
        UnmapViewOfFile(g_pSharedMemory);
    }
    
    if (g_hFileMapping != NULL && g_hFileMapping != INVALID_HANDLE_VALUE) {
        CloseHandle(g_hFileMapping);
    }
    
    g_pSharedMemory = NULL;
    g_hFileMapping = NULL;
}

// ============================================================================
// UpdateGameStatus - Update game status in shared memory (0x00442350)
// ============================================================================
void UpdateGameStatus(void)
{
    // 0x00442350
    if (g_pSharedMemory != NULL) {
        *(g_pSharedMemory + 5) = 1;
    }
}

// ============================================================================
// CleanupVideoConfigAndSaveAllSettings - Final video cleanup (0x00497ea0)
// ============================================================================
void CleanupVideoConfigAndSaveAllSettings(void)
{
    OutputDebugStringA("[CLEANUP] CleanupVideoConfigAndSaveAllSettings called\n");
    // 0x00497ea0: Guard flag prevents double cleanup
    if (g_bHasFinalizedSettings) {
        return;
    }
    
    g_bHasFinalizedSettings = TRUE;
    
    // Save settings to registry
    SaveGameSettingsToRegistry();
    
    // Cleanup Marni Direct3D
    if (g_pMarniDirect3D != NULL) {
        CVideoSystem_Cleanup(g_pMarniDirect3D);
        // Original: free(videoConfig) - in Ghidra this is a direct free
        // In our implementation, the constructor used operator_new so we match
        operator_delete(g_pMarniDirect3D);
    }
    
    g_pMarniDirect3D = NULL;
    OutputDebugStringA("[CLEANUP] g_pMarniDirect3D set to NULL\n");
}

// ============================================================================
// DestroyAllSoundBanks - Release all sound resources (0x004801c0)
// ============================================================================
void DestroyAllSoundBanks(void)
{
    // 0x004801c0: The original has 6 categories of sound banks:
    // BGM, SFX, Room SFX, Character SFX, Enemy SFX, General
    
    // In modern implementation, this frees XAudio2 resources
    // Stub: actual XAudio2 cleanup to be implemented
}

// ============================================================================
// CleanupAsyncTasks - Initiate async cleanup (0x0041d0b0)
// ============================================================================
void CleanupAsyncTasks(void)
{
    // 0x0041d0b0: Execute async callback to destroy sound manager
    ExecAsync((void*)DestroySoundManagerAsync);
}

// ============================================================================
// SaveGameSettingsToRegistry - Save current settings (stub)
// ============================================================================
void SaveGameSettingsToRegistry(void)
{
    // Original saves all display settings to registry
    HKEY hKey;
    LSTATUS result = RegCreateKeyExA(HKEY_CURRENT_USER, REGKEY_PATH, 0, NULL,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
    
    if (result == ERROR_SUCCESS) {
        DWORD dwValue;
        
        dwValue = g_dwScreenWidth;
        RegSetValueExA(hKey, "X Size", 0, REG_DWORD, (BYTE*)&dwValue, sizeof(DWORD));
        
        dwValue = g_dwScreenHeight;
        RegSetValueExA(hKey, "Y Size", 0, REG_DWORD, (BYTE*)&dwValue, sizeof(DWORD));
        
        dwValue = g_dwBitDepth;
        RegSetValueExA(hKey, "Bit Depth", 0, REG_DWORD, (BYTE*)&dwValue, sizeof(DWORD));
        
        dwValue = g_bFullScreen;
        RegSetValueExA(hKey, "FullScreen?", 0, REG_DWORD, (BYTE*)&dwValue, sizeof(DWORD));
        
        dwValue = g_dwPlayCount;
        RegSetValueExA(hKey, "Play Number", 0, REG_DWORD, (BYTE*)&dwValue, sizeof(DWORD));
        
        dwValue = g_dwClearCount;
        RegSetValueExA(hKey, "Clear Number", 0, REG_DWORD, (BYTE*)&dwValue, sizeof(DWORD));
        
        dwValue = g_dwSelectedDisplayAdapterID;
        RegSetValueExA(hKey, "Display Driver", 0, REG_DWORD, (BYTE*)&dwValue, sizeof(DWORD));
        
        dwValue = g_dwSelectedDisplayModeID;
        RegSetValueExA(hKey, "Display Mode", 0, REG_DWORD, (BYTE*)&dwValue, sizeof(DWORD));
        
        RegCloseKey(hKey);
    }
}

// ============================================================================
// CVideoSystem_Cleanup - Cleanup Marni Direct3D (stub, calls destructor)
// ============================================================================
void CVideoSystem_Cleanup(void* ptr)
{
    // 0x0044b940: Destructor for CMarniDirect3D
    // In the modern port, delegates to MarniDX which owns all D3D11 state.
    if (ptr != NULL) {
        CMarniDirect3D* pD3D = (CMarniDirect3D*)ptr;

        // Release all D3D11 resources — everything lives in MarniDX now.
        if (pD3D->m_pDX) {
            pD3D->m_pDX->Destroy();
        }

        pD3D->m_isInitialized = FALSE;
    }
}

// ============================================================================
// DestroySoundManagerAsync - Async callback for sound cleanup
// ============================================================================
void DestroySoundManagerAsync(void)
{
    // Stub: cleanup XAudio2 engine asynchronously
}
