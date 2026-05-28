// AssetPath.cpp - Asset path resolution implementation
// Original game paths: ".\usa\data\..." go through remapping in debug builds
#include "AssetPath.h"
#include <windows.h>
#include <string.h>
#include <stdio.h>

const char* ResolveAssetPath(const char* originalPath, char* outPath, size_t outSize)
{
    if (originalPath == NULL || outPath == NULL || outSize == 0) {
        return NULL;
    }

#if USE_ASSET_PATH_REMAP
    // In debug builds, remap ".\usa\" → ".\assets\USA\"
    // Also handle "./usa/" variant (forward slashes)
    const char* usaPos = NULL;

    // Try backslash pattern
    const char* p = originalPath;
    while (*p) {
        if ((p[0] == '\\' || p[0] == '/') &&
            (p[1] == 'u' || p[1] == 'U') &&
            (p[2] == 's' || p[2] == 'S') &&
            (p[3] == 'a' || p[3] == 'A') &&
            (p[4] == '\\' || p[4] == '/')) {
            usaPos = p;
            break;
        }
        p++;
    }

    if (usaPos != NULL) {
        // Copy prefix (e.g., ".\" or "./")
        size_t prefixLen = (size_t)(usaPos - originalPath);
        if (prefixLen >= outSize) return NULL;
        memcpy(outPath, originalPath, prefixLen);

        // Insert "\assets\USA\" or "/assets/USA/"
        char sep = usaPos[4];  // preserve original slash style
        int written = sprintf(outPath + prefixLen, "%cassets%cUSA%c", sep, sep, sep);
        if (written < 0) return NULL;

        // Append the remaining path (after "usa\")
        size_t remainingLen = strlen(usaPos + 5);
        if (prefixLen + (size_t)written + remainingLen + 1 > outSize) return NULL;
        strcpy(outPath + prefixLen + written, usaPos + 5);

        return outPath;
    }
#endif

    // No remap needed or release build: use path as-is
    size_t len = strlen(originalPath);
    if (len + 1 > outSize) return NULL;
    strcpy(outPath, originalPath);
    return outPath;
}
