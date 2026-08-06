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
