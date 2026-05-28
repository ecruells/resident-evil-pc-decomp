// FileLoader.h - Asset file loading (replaces original load_file at 0x00411e10)
// Handles path resolution and file I/O for game assets
#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

// load_file - Load a game asset file into memory
//   path:     The game path, e.g. ".\usa\data\fontus.tim"
//   buffer:   Destination buffer for file data
//   flags:    Load flags (bit 5 = use install directory prefix)
// Returns: File size on success, -1 (0xFFFFFFFF) on failure
// Original address: 0x00411e10
size_t load_file(const char* path, void* buffer, unsigned char flags);

#ifdef __cplusplus
}
#endif
