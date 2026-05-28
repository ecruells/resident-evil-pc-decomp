// FileLoader.cpp - Asset file loading implementation
// Original function: load_file at 0x00411e10 (Ghidra)
// Handles fopen/fread with retry logic and path resolution
#include "../Globals.h"
#include "../system/AssetPath.h"
#include "FileLoader.h"
#include <stdio.h>

// ============================================================================
// load_file - Load a game asset file into memory (0x00411e10)
//
// The original function has retry logic:
//   1. If flag bit 5 is set: prepend install directory to path (skipped in dev)
//   2. Try fopen(path, "rb")
//   3. If fails, retry up to 2 more times (sprite buffer flag / Task_sleep)
//   4. On success: fseek to end, ftell for size, fseek back, fread into buffer
//   5. On failure after retries: show error message, return -1
// ============================================================================
size_t load_file(const char* path, void* buffer, unsigned char flags)
{
    char resolvedPath[MAX_PATH];
    const char* filePath;

    // Resolve the asset path (debug: remap .\usa\ → .\assets\USA\)
    if (ResolveAssetPath(path, resolvedPath, sizeof(resolvedPath)) != NULL) {
        filePath = resolvedPath;
    } else {
        filePath = path;
    }

    // Original: if (DAT_004b3998 & flags) → prepend install path
    // In dev mode, we skip this since assets are relative
#if !USE_ASSET_PATH_REMAP
    char fullInstallPath[MAX_PATH];
    if ((flags & 0x20) != 0) {
        // Prepend install directory to path (skipping first 8 chars: ".\usa\d")
        sprintf(fullInstallPath, "%s%s", g_szInstallPath, filePath + 8);
        filePath = fullInstallPath;
    }
#endif

    int retryCount = 0;
    BOOL showError = FALSE;

    do {
        // Try to open the file
        FILE* fp = fopen(filePath, "rb");
        if (fp != NULL) {
            // Get file size
            fseek(fp, 0, SEEK_END);
            size_t fileSize = ftell(fp);
            fseek(fp, 0, SEEK_SET);

            // Read into buffer
            size_t bytesRead = fread(buffer, 1, fileSize, fp);
            fclose(fp);

            if (bytesRead == fileSize) {
                return fileSize;
            }

            // Partial read - treat as failure
            return (size_t)-1;
        }

        // File open failed
        if (showError) {
            // Original: sprintf + display error message
            char errorMsg[256];
            sprintf(errorMsg, "Could not open file: %s", filePath + 2);
            // Original calls FUN_00497e90() which shows a dialog
            // In dev mode, just OutputDebugString for simplicity
            OutputDebugStringA("[LOAD] FAILED: ");
            OutputDebugStringA(filePath);
            OutputDebugStringA("\n");
            return (size_t)-1;
        }

        // Retry: the original sets some global flag and tries again
        // DAT_004d4690 = 0xFF - some retry indicator
        showError = TRUE;
        retryCount++;

    } while (retryCount < 2);

    return (size_t)-1;
}
