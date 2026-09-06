// AssetPath.cpp - Runtime asset-root selection
//
// The original hardcodes a data root in front of every asset path. That root
// differs by region: the North American/GOG version ships a "usa" tree and the
// Japanese PC (Biohazard) version a "jpn" tree. The path templates themselves
// are identical, so the only thing that changes is the root folder.
//
// GAME_DATA_ROOT (see AssetPath.h) is a compile-time macro that stands in for the
// original's GAME_DATA_ROOT; it is baked into string literals and the static
// path templates. That remains the USA default. This translation unit adds a
// runtime override selected from config.ini [Assets] Version so the same build
// can point its readers at the JPN tree without recompiling.
//
// Note on idempotence: the previous ResolveAssetPath rewrite scanned for a
// "\usa\" component and spliced in "\assets\USA\", which double-applied because
// the result still contained "\USA\". ResolveAssetRoot instead substitutes the
// recognized root FORM for the configured root, and the configured root is never
// itself a recognized form, so a second pass cannot match.

#include "AssetPath.h"
#include <stdio.h>
#include <string.h>

// The configured data root (folder + trailing separator). Defaults to the
// compile-time USA root so release behaviour is unchanged unless config.ini
// selects JPN.
#if defined(_DEBUG)
#define ROOT_USA ".\\assets\\USA\\"
#define ROOT_JPN ".\\assets\\JPN\\"
#else
#define ROOT_USA ".\\usa\\"
#define ROOT_JPN ".\\jpn\\"
#endif

const char* s_assetRoot = ROOT_USA;
// Version is tracked as an int, not by comparing s_assetRoot's pointer against
// a string literal. Debug builds compile without string pooling (/GF is off),
// so two ".\assets\JPN\" literals have different addresses and a pointer
// comparison would report USA even when JPN is selected. Content comparison is
// the safe alternative, but an explicit flag is unambiguous.
static int s_assetIsJpn = 0;

void SetAssetVersion(const char* version)
{
    if (version != NULL &&
        (version[0] == 'J' || version[0] == 'j') &&
        (version[1] == 'P' || version[1] == 'p')) {
        s_assetRoot = ROOT_JPN;
        s_assetIsJpn = 1;
    }
    else {
        s_assetRoot = ROOT_USA;
        s_assetIsJpn = 0;
    }
}

const char* GetAssetRoot(void)
{
    return s_assetRoot;
}

int GetAssetVersion(void)
{
    return s_assetIsJpn;
}

const char* ResolveAssetRoot(const char* path, char* out, size_t outSize)
{
    if (path == NULL) return NULL;
    if (out == NULL || outSize == 0) return path;

    // Recognized USA/retail root forms. Both are always matched so a path
    // compiled against either the debug ("assets") or retail root is remapped,
    // e.g. the FMV table which hardcodes the retail ".\usa\" form.
    static const char* kForms[] = { ".\\assets\\USA\\", ".\\usa\\" };

    const char* match = NULL;
    size_t matchLen = 0;
    for (int i = 0; i < 2; i++) {
        size_t len = strlen(kForms[i]);
        if (len > matchLen && strncmp(path, kForms[i], len) == 0) {
            matchLen = len;
            match = kForms[i];
        }
    }
    if (match == NULL) return path; // Not an asset-rooted path.

    int n = sprintf_s(out, outSize, "%s%s", s_assetRoot, path + matchLen);
    if (n < 0) return path;         // Would not fit: keep the original path.
    return out;
}
