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
#include "../platform/types.h"   // sprintf_s
#include "../platform/platform.h" // plat_normalize_path
#include <stdio.h>
#include <string.h>

// The configured data root (folder + trailing separator). Defaults to the
// compile-time USA root so release behaviour is unchanged unless config.ini
// selects JPN.
#if !defined(_WIN32)
#define ROOT_USA "./assets/USA/"
#define ROOT_JPN "./assets/JPN/"
#elif defined(_DEBUG)
#define ROOT_USA ".\\assets\\USA\\"
#define ROOT_JPN ".\\assets\\JPN\\"
#else
#define ROOT_USA ".\\usa\\"
#define ROOT_JPN ".\\jpn\\"
#endif

#if defined(_WIN32)
#define PATH_SEP '\\'
#else
#define PATH_SEP '/'
#endif

// Folder holding the USA/ and JPN/ trees (config.ini [Assets] Path). Empty
// means "not configured": the compile-time roots above stay in force.
static char s_base[240] = "";
// Composed root ("<base>/<region>/") used when a base is configured.
static char s_assetRootBuf[260] = "";
const char* s_assetRoot = ROOT_USA;
// Explicit save folder (config.ini [Save] Path). Empty means "<base>/SAVE/".
static char s_saveRoot[260] = "";

// Version is tracked as an int, not by comparing s_assetRoot's pointer against
// a string literal. Debug builds compile without string pooling (/GF is off),
// so two ".\assets\JPN\" literals have different addresses and a pointer
// comparison would report USA even when JPN is selected. Content comparison is
// the safe alternative, but an explicit flag is unambiguous.
static int s_assetIsJpn = 0;

// Rebuild s_assetRoot from the configured base + region. With no base the
// compile-time root is kept, which also keeps every static path template (they
// are compiled against it and patched by character index) valid.
static void ComposeAssetRoot(void)
{
    if (s_base[0] == '\0') {
        s_assetRoot = s_assetIsJpn ? ROOT_JPN : ROOT_USA;
        return;
    }
    sprintf_s(s_assetRootBuf, sizeof(s_assetRootBuf), "%s%c%s%c",
              s_base, PATH_SEP, s_assetIsJpn ? "JPN" : "USA", PATH_SEP);
    s_assetRoot = s_assetRootBuf;
}

void SetAssetBase(const char* base)
{
    if (base == NULL || base[0] == '\0') {
        s_base[0] = '\0';
        ComposeAssetRoot();
        return;
    }

    strncpy(s_base, base, sizeof(s_base) - 1);
    s_base[sizeof(s_base) - 1] = '\0';
    // Drop a trailing separator so composition is uniform.
    size_t n = strlen(s_base);
    while (n > 1 && (s_base[n - 1] == '/' || s_base[n - 1] == '\\')) {
        s_base[--n] = '\0';
    }
    ComposeAssetRoot();
}

const char* GetAssetBase(void)
{
    return s_base;
}

void SetSaveRoot(const char* path)
{
    if (path == NULL || path[0] == '\0') {
        s_saveRoot[0] = '\0';
        return;
    }

    strncpy(s_saveRoot, path, sizeof(s_saveRoot) - 1);
    s_saveRoot[sizeof(s_saveRoot) - 1] = '\0';
    size_t n = strlen(s_saveRoot);
    if (n + 1 < sizeof(s_saveRoot) &&
        s_saveRoot[n - 1] != '/' && s_saveRoot[n - 1] != '\\') {
        s_saveRoot[n] = PATH_SEP;
        s_saveRoot[n + 1] = '\0';
    }
}

const char* GetSaveRoot(void)
{
    // The folder is "SAVE" (what the original kept next to the executable), but
    // the path is run through the platform layer so an existing "save" or
    // "Save" directory is found as well - save reads are plain fopen, not
    // LoadFile, so without this a copied Windows SAVE folder was invisible on a
    // case-sensitive filesystem while writes (plat_file_write, which does
    // resolve) went somewhere else. Returned buffer is a shared static, like the
    // default it replaces: callers build a name with it immediately.
    static char s_resolved[260];
    const char* raw;

    if (s_saveRoot[0] != '\0') {
        raw = s_saveRoot;
    } else if (s_base[0] == '\0') {
        raw = GAME_SAVE_ROOT;   // nothing configured
    } else {
        static char s_defaultSave[260];
        sprintf_s(s_defaultSave, sizeof(s_defaultSave), "%s%cSAVE%c",
                  s_base, PATH_SEP, PATH_SEP);
        raw = s_defaultSave;
    }

    plat_normalize_path(raw, s_resolved, sizeof(s_resolved));
    return s_resolved;
}

void SetAssetVersion(const char* version)
{
    if (version != NULL &&
        (version[0] == 'J' || version[0] == 'j') &&
        (version[1] == 'P' || version[1] == 'p')) {
        s_assetIsJpn = 1;
    }
    else {
        s_assetIsJpn = 0;
    }
    ComposeAssetRoot();
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

    // Recognized root forms. Both compile-time variants are always matched so
    // a path built against either the debug ("assets") or retail root is
    // remapped, e.g. the FMV table which hardcodes the retail ".\usa\" form.
    // On non-Windows the configured roots are themselves in the list; the
    // substitution is idempotent for them (root -> same root).
#if !defined(_WIN32)
    static const char* kForms[] = { "./assets/USA/", "./assets/JPN/",
                                    ".\\usa\\", ".\\jpn\\" };
    const int kFormCount = 4;
#else
    static const char* kForms[] = { ".\\assets\\USA\\", ".\\usa\\" };
    const int kFormCount = 2;
#endif

    const char* match = NULL;
    size_t matchLen = 0;
    for (int i = 0; i < kFormCount; i++) {
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
