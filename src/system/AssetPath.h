// AssetPath.h - Asset path resolution for development and retail builds
//
// In the original game, all asset paths are relative starting with ".\usa\".
// In this decomp project, assets are stored under "assets\USA\".
// The path resolver maps original paths to their correct location based on build config.
#pragma once

#ifdef _DEBUG
// Debug: assets are under ".\assets\USA\"
#define USE_ASSET_PATH_REMAP 1
#else
// Release: assets follow original layout ".\usa\"
#define USE_ASSET_PATH_REMAP 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ResolveAssetPath - Map an original game path to the actual file location
//   originalPath: e.g. ".\usa\data\fontus.tim" or "./usa/data/fontus.tim"
//   outPath:      resolved path buffer
//   outSize:      size of outPath buffer
// Returns: outPath on success, NULL on error
const char* ResolveAssetPath(const char* originalPath, char* outPath, size_t outSize);

#ifdef __cplusplus
}
#endif
