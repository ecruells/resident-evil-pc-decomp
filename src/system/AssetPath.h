// AssetPath.h - Compile-time asset root
//
// The original hardcodes GAME_DATA_ROOT in front of every asset path. This project keeps
// the same tree under ".\assets\USA\" for development, so the only thing that
// differs is the root.
//
// That substitution is done at COMPILE TIME, with GAME_DATA_ROOT standing in for
// the original's GAME_DATA_ROOT literal. An earlier revision instead rewrote paths at
// runtime (ResolveAssetPath, which scanned for a "\usa\" component and spliced in
// "\assets\USA\"). That was worse in two ways:
//
//   1. It was a behavioural divergence with no counterpart in the original - a
//      string transform sitting between the game and the filesystem.
//   2. It was not idempotent, and nothing stopped it running twice. Once the sound
//      loaders resolved a path and handed the result to CreateSound, which resolved
//      again, ".\assets\USA\voice\V001_00.wav" matched its own "\USA\" component
//      and became ".\assets\assets\USA\voice\V001_00.wav".
//
// A macro cannot double-apply, and it keeps the path literals in the decompiled
// code reading the way the original's do.
#pragma once
#include <stddef.h>

#ifdef _DEBUG
#define GAME_DATA_ROOT      ".\\assets\\USA\\"
#else
#define GAME_DATA_ROOT      ".\\usa\\"
#endif

// Japanese (Biohazard) data root. Like GAME_DATA_ROOT it is substituted at
// compile time, so JPN path literals (e.g. the FMV table) initialized statically
// read as the JPN tree instead of the retail ".\usa\" form that ResolveAssetRoot
// would otherwise have to swap. Debug/Release lengths match GAME_DATA_ROOT.
#ifdef _DEBUG
#define GAME_DATA_ROOT_JPN  ".\\assets\\JPN\\"
#else
#define GAME_DATA_ROOT_JPN  ".\\jpn\\"
#endif

// Save directory root. The original builds save paths as "%ssavedat%d.dat"
// with the literal "SAVE\\" (CWD-relative, so "SAVE\savedat1.dat"). In
// development the save folder lives under the asset tree so saves travel
// with the project (assets/save, matching the original game's SAVE folder).
#ifdef _DEBUG
#define GAME_SAVE_ROOT      ".\\assets\\save\\"
#else
#define GAME_SAVE_ROOT      "SAVE\\"
#endif

// Length of the root, excluding the terminator.
#define GAME_DATA_ROOT_LEN  (sizeof(GAME_DATA_ROOT) - 1)

// Byte offset of a character in a path built as GAME_DATA_ROOT followed by the
// original's template body. Needed only by paths that are fixed-layout templates
// patched by character index (g_bgPathTemplate). `originalIndex` is the index the
// original used, which is relative to its own 6-character GAME_DATA_ROOT root.
#define GAME_DATA_PATH_IDX(originalIndex) (GAME_DATA_ROOT_LEN + (originalIndex) - 6)

#ifdef __cplusplus
extern "C" {
#endif

// Runtime asset-root selector. Compile-time GAME_DATA_ROOT stays the USA default
// so static initializers (g_bgPathTemplate, the door table, g_maskPathTemplate)
// are still valid; SetAssetVersion() swaps the root the readers use at runtime,
// letting a single build run USA *or* JPN assets per config.ini [Assets].
void         SetAssetVersion(const char* version);
const char*  GetAssetRoot(void);
// 1 when the JPN (Biohazard) asset tree is active, 0 for the USA default.
int          GetAssetVersion(void);

// Rewrites whichever known asset-root form a path was compiled with (debug
// ".\assets\USA\" or retail ".\usa\") to the current runtime root, writing the
// result into `out` (outSize bytes) and returning `out`. Returns `path`
// unchanged when no rewrite is needed (non-rooted path, or out of room). The
// rewrite is idempotent: the configured root never equals a recognized form, so
// running the resolved path through again is a no-op.
const char*  ResolveAssetRoot(const char* path, char* out, size_t outSize);

#ifdef __cplusplus
}
#endif
