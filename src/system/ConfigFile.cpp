// ConfigFile.cpp - config.ini as the single settings store (both builds).
// See ConfigFile.h for the contract.
#include "ConfigFile.h"

#include "../Globals.h"
#include "../DebugPrint.h"
#include "../platform/platform.h"
#include "AssetPath.h"   // SetAssetBase / SetSaveRoot / SetAssetVersion

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONFIG_NAME "config.ini"
#define MAX_LINE    1024
#define MAX_LINES   512

#if defined(_WIN32)
#define kPathSep '\\'
#else
#define kPathSep '/'
#endif

namespace {

char s_path[260];
BOOL s_pathResolved = FALSE;

// The keys the game owns and rewrites on exit; everything else in the file is
// the player's and is left alone.
struct OwnedKey {
    const char* section;
    const char* key;
};

const OwnedKey kOwnedKeys[] = {
    { "Display", "FullScreen" },
    { "Display", "Width" },
    { "Display", "Height" },
    { "Display", "BitDepth" },
    { "Display", "VSync" },
    { "Player",  "PlayCount" },
    { "Player",  "ClearCount" },
    { "Input",   "KeyDef" },
    { "Input",   "SideDef" },
};
const int kOwnedKeyCount = (int)(sizeof(kOwnedKeys) / sizeof(kOwnedKeys[0]));

BOOL FileExists(const char* path)
{
    FILE* f = fopen(path, "r");
    if (f == NULL) return FALSE;
    fclose(f);
    return TRUE;
}

void HexEncode(const BYTE* src, int count, char* out)
{
    static const char* kDigits = "0123456789ABCDEF";
    for (int i = 0; i < count; ++i) {
        out[i * 2]     = kDigits[src[i] >> 4];
        out[i * 2 + 1] = kDigits[src[i] & 0x0F];
    }
    out[count * 2] = '\0';
}

int HexNibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void HexDecode(const char* src, BYTE* dst, int count)
{
    for (int i = 0; i < count; ++i) {
        const int hi = HexNibble(src[i * 2]);
        const int lo = HexNibble(src[i * 2 + 1]);
        if (hi < 0 || lo < 0) return;   // leave the rest at their current value
        dst[i] = (BYTE)((hi << 4) | lo);
    }
}

// Trim leading/trailing whitespace in place; returns the trimmed start.
char* Trim(char* s)
{
    while (*s == ' ' || *s == '\t') ++s;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' ||
                     s[n - 1] == ' '  || s[n - 1] == '\t')) {
        s[--n] = '\0';
    }
    return s;
}

// Read `key` from `section`. Sections are tracked by the caller's scan.
BOOL ReadValue(const char* path, const char* section, const char* key,
               char* out, size_t outSize)
{
    FILE* f = fopen(path, "r");
    if (f == NULL) return FALSE;

    char line[MAX_LINE];
    char current[128] = "";
    BOOL found = FALSE;

    while (fgets(line, sizeof(line), f) != NULL) {
        char* p = Trim(line);
        if (*p == ';' || *p == '#' || *p == '\0') continue;

        if (*p == '[') {
            char* end = strchr(p, ']');
            if (end != NULL) {
                size_t n = (size_t)(end - p - 1);
                if (n >= sizeof(current)) n = sizeof(current) - 1;
                memcpy(current, p + 1, n);
                current[n] = '\0';
            }
            continue;
        }

        if (strcmp(current, section) != 0) continue;

        char* eq = strchr(p, '=');
        if (eq == NULL) continue;
        *eq = '\0';
        if (strcmp(Trim(p), key) != 0) continue;

        char* v = Trim(eq + 1);
        strncpy(out, v, outSize - 1);
        out[outSize - 1] = '\0';
        found = TRUE;
        break;
    }
    fclose(f);
    return found;
}

int ReadInt(const char* path, const char* section, const char* key, int fallback)
{
    char buf[64];
    if (!ReadValue(path, section, key, buf, sizeof(buf))) return fallback;
    return atoi(buf);
}

// Resolve a configured folder against the executable's directory. Absolute
// paths - including Windows drive and UNC forms, so a config.ini written on one
// machine still reads sensibly on the other platform - are used as written;
// everything else is joined onto `exeDir`. With no anchor the value is left
// relative to the working directory.
const char* ResolveConfiguredPath(const char* exeDir, const char* value,
                                  char* out, size_t outSize)
{
    if (!plat_path_is_absolute(value) && exeDir != NULL && exeDir[0] != '\0') {
        snprintf(out, outSize, "%s%c%s", exeDir, kPathSep, value);
        return out;
    }
    strncpy(out, value, outSize - 1);
    out[outSize - 1] = '\0';
    return out;
}

// Static line pool for keys that have to be inserted rather than replaced.
char s_newLine[16][700];
int  s_newLineUsed = 0;

void SetValue(const char* section, const char* key, const char* value,
              char** lines, int* count)
{
    char current[128] = "";
    int sectionHeader = -1;     // index of our section's [header]
    int sectionEnd = -1;        // index of the next section's header, if any

    for (int i = 0; i < *count; ++i) {
        char* p = Trim(lines[i]);
        if (*p == '[') {
            char* end = strchr(p, ']');
            if (end != NULL) {
                size_t n = (size_t)(end - p - 1);
                if (n >= sizeof(current)) n = sizeof(current) - 1;
                memcpy(current, p + 1, n);
                current[n] = '\0';
            }
            if (strcmp(current, section) == 0) {
                sectionHeader = i;
            } else if (sectionHeader >= 0 && sectionEnd < 0) {
                sectionEnd = i;
            }
            continue;
        }

        if (strcmp(current, section) != 0) continue;
        char* eq = strchr(p, '=');
        if (eq == NULL) continue;

        // Compare the key WITHOUT touching the line: an earlier version wrote
        // '\0' over the '=' while scanning, which silently ate the '=value'
        // part of every comment line and of the keys it passed over.
        char* keyEnd = eq;
        while (keyEnd > p && (keyEnd[-1] == ' ' || keyEnd[-1] == '\t')) --keyEnd;
        const size_t keyLen = strlen(key);
        if ((size_t)(keyEnd - p) != keyLen || strncmp(p, key, keyLen) != 0) continue;

        // Replace in place.
        if (s_newLineUsed < 16) {
            snprintf(s_newLine[s_newLineUsed], sizeof(s_newLine[0]), "%s=%s", key, value);
            lines[i] = s_newLine[s_newLineUsed++];
        }
        return;
    }

    if (*count + 3 >= MAX_LINES || s_newLineUsed + 3 > 16) return;

    if (sectionHeader < 0) {
        // Unknown section: append it whole.
        snprintf(s_newLine[s_newLineUsed], sizeof(s_newLine[0]), "%s", "");
        lines[(*count)++] = s_newLine[s_newLineUsed++];
        snprintf(s_newLine[s_newLineUsed], sizeof(s_newLine[0]), "[%s]", section);
        lines[(*count)++] = s_newLine[s_newLineUsed++];
        snprintf(s_newLine[s_newLineUsed], sizeof(s_newLine[0]), "%s=%s", key, value);
        lines[(*count)++] = s_newLine[s_newLineUsed++];
        return;
    }

    // Insert the key at the end of its section.
    const int at = (sectionEnd >= 0) ? sectionEnd : *count;
    snprintf(s_newLine[s_newLineUsed], sizeof(s_newLine[0]), "%s=%s", key, value);
    char* newLine = s_newLine[s_newLineUsed++];
    for (int i = *count; i > at; --i) lines[i] = lines[i - 1];
    lines[at] = newLine;
    (*count)++;
}

}  // namespace

// ---------------------------------------------------------------------------
const char* ConfigFile_Find(void)
{
    if (s_pathResolved) return s_path;

    // Next to the executable first - that is the folder the asset and save
    // paths default to, so the settings file belongs there too. The
    // working-directory candidates stay as a fallback for launching the
    // development build from the repository root.
    char exeDir[240] = "";
    char exePath[300] = "";
    if (plat_exe_dir(exeDir, sizeof(exeDir))) {
        snprintf(exePath, sizeof(exePath), "%s%c%s", exeDir, kPathSep, CONFIG_NAME);
        if (FileExists(exePath)) {
            strncpy(s_path, exePath, sizeof(s_path) - 1);
            s_path[sizeof(s_path) - 1] = '\0';
            s_pathResolved = TRUE;
            return s_path;
        }
    }

    static const char* kCandidates[] = {
        "./" CONFIG_NAME, CONFIG_NAME, "../" CONFIG_NAME, "../../" CONFIG_NAME,
    };

    for (int i = 0; i < 4; ++i) {
        if (FileExists(kCandidates[i])) {
            strncpy(s_path, kCandidates[i], sizeof(s_path) - 1);
            s_path[sizeof(s_path) - 1] = '\0';
            s_pathResolved = TRUE;
            return s_path;
        }
    }

    // Nothing to read: a fresh file is created next to the executable when we
    // know where that is, otherwise in the working directory.
    const char* target = (exePath[0] != '\0') ? exePath : "./" CONFIG_NAME;
    strncpy(s_path, target, sizeof(s_path) - 1);
    s_path[sizeof(s_path) - 1] = '\0';
    s_pathResolved = TRUE;
    return s_path;
}

// ---------------------------------------------------------------------------
void ConfigFile_EnsureExists(void)
{
    const char* path = ConfigFile_Find();
    if (FileExists(path)) return;

    FILE* f = fopen(path, "w");
    if (f == NULL) {
        dbg_printf("[CONFIG] could not create %s\n", path);
        return;
    }

    char keyHex[32 * 2 + 1];
    char sideHex[128 * 2 + 1];
    HexEncode(g_keyBindingData, (int)sizeof(g_keyBindingData), keyHex);
    HexEncode(g_joystickBindingData, (int)sizeof(g_joystickBindingData), sideHex);

    fprintf(f,
        "; config.ini - Resident Evil PC decomp configuration.\n"
        ";\n"
        "; The [Display], [Player] and [Input] values below are rewritten by the\n"
        "; game on exit. Everything else is yours to edit; comments are kept.\n"
        "\n"
        "[Display]\n"
        "; 0 = windowed, 1 = fullscreen\n"
        "FullScreen=%d\n"
        "; Resolution width\n"
        "Width=%u\n"
        "; Resolution height\n"
        "Height=%u\n"
        "; Bits per pixel (16 or 32)\n"
        "BitDepth=%u\n"
        "; Wait for vblank on present. 0 = off (default, and what the original did\n"
        "; in a window): the engine paces itself to 33 ms per tick in software.\n"
        "VSync=%d\n"
        "\n"
        "[Assets]\n"
        "; Folder that holds the USA/ and JPN/ data trees. Relative paths are\n"
        "; resolved from the game binary's own directory. Leave it empty to use\n"
        "; that directory itself, i.e. put USA/ next to the executable.\n"
        "Path=\n"
        "; Which tree to run. USA = North American/GOG, JPN = Japanese PC\n"
        "; (Biohazard). Selects the subfolder of Path that every asset uses.\n"
        "Version=%s\n"
        "\n"
        "[Save]\n"
        "; Folder holding savedat*.dat, relative to the binary's directory unless\n"
        "; written absolute. Leave it empty for <Assets Path>/SAVE: with the\n"
        "; default [Assets] Path that is SAVE/ beside the executable, the layout\n"
        "; the original used, and an existing save/ or Save/ is found as well.\n"
        "Path=\n"
        "\n"
        "[Debug]\n"
        "; Master switch for the port-added debug features: F1 debug menu, F6\n"
        "; texture viewer, F8 collision overlay. 0 = off, 1 = on.\n"
        "EnableDebug=%d\n"
        "\n"
        "[Player]\n"
        "PlayCount=%u\n"
        "ClearCount=%u\n"
        "\n"
        "[Input]\n"
        "; 32 keyboard virtual-key codes, then 128 bytes of pad bindings, as hex.\n"
        "KeyDef=%s\n"
        "SideDef=%s\n",
        g_bFullScreen ? 1 : 0,
        (unsigned)g_dwScreenWidth, (unsigned)g_dwScreenHeight,
        (unsigned)g_dwBitDepth, g_bVSync ? 1 : 0,
        (GetAssetVersion() == 1) ? "JPN" : "USA",
#ifdef _DEBUG
        1,
#else
        0,
#endif
        (unsigned)g_dwPlayCount, (unsigned)g_dwClearCount,
        keyHex, sideHex);

    fclose(f);
    dbg_printf("[CONFIG] created %s\n", path);
}

// ---------------------------------------------------------------------------
BOOL ConfigFile_Load(void)
{
    // Anchor everything to the executable's own folder before looking at the
    // file: with no [Assets]/[Save] path configured that directory IS the base,
    // so the game no longer depends on the working directory. See the note in
    // AssetPath.h for why the compile-time roots still exist.
    char exeDir[240] = "";
    plat_exe_dir(exeDir, sizeof(exeDir));
    SetAssetBase(exeDir);
    SetSaveRoot(NULL);

    const char* path = ConfigFile_Find();
    if (!FileExists(path)) return FALSE;

    g_bFullScreen    = ReadInt(path, "Display", "FullScreen", g_bFullScreen ? 1 : 0) ? TRUE : FALSE;
    g_dwScreenWidth  = (DWORD)ReadInt(path, "Display", "Width", (int)g_dwScreenWidth);
    g_dwScreenHeight = (DWORD)ReadInt(path, "Display", "Height", (int)g_dwScreenHeight);
    g_dwBitDepth     = (DWORD)ReadInt(path, "Display", "BitDepth", (int)g_dwBitDepth);
    g_bVSync         = ReadInt(path, "Display", "VSync", g_bVSync ? 1 : 0) ? TRUE : FALSE;

    // Same clamps the Windows build applied.
    if (g_dwScreenWidth < 320) g_dwScreenWidth = 640;
    if (g_dwScreenHeight < 240) g_dwScreenHeight = 480;
    if (g_dwScreenWidth > 3840) g_dwScreenWidth = 3840;
    if (g_dwScreenHeight > 2160) g_dwScreenHeight = 2160;
    if (g_dwBitDepth != 16 && g_dwBitDepth != 32) g_dwBitDepth = 32;

    g_dwPlayCount  = (DWORD)ReadInt(path, "Player", "PlayCount", (int)g_dwPlayCount);
    g_dwClearCount = (DWORD)ReadInt(path, "Player", "ClearCount", (int)g_dwClearCount);

    char buf[128 * 2 + 1];
    if (ReadValue(path, "Input", "KeyDef", buf, sizeof(buf))) {
        HexDecode(buf, g_keyBindingData, (int)sizeof(g_keyBindingData));
    }
    if (ReadValue(path, "Input", "SideDef", buf, sizeof(buf))) {
        HexDecode(buf, g_joystickBindingData, (int)sizeof(g_joystickBindingData));
    }

    // Asset and save folders. [Assets] Path is the folder that holds the USA/
    // and JPN/ trees; [Save] Path is where savedat*.dat goes. Both are relative
    // to the executable's directory unless written absolute.
    char value[240];
    char resolved[320];
    if (ReadValue(path, "Assets", "Path", value, sizeof(value)) && value[0] != '\0') {
        SetAssetBase(ResolveConfiguredPath(exeDir, value, resolved, sizeof(resolved)));
    }
    if (ReadValue(path, "Save", "Path", value, sizeof(value)) && value[0] != '\0') {
        SetSaveRoot(ResolveConfiguredPath(exeDir, value, resolved, sizeof(resolved)));
    }

    char version[16];
    if (ReadValue(path, "Assets", "Version", version, sizeof(version))) {
        SetAssetVersion(version);
    }

    dbg_printf("[CONFIG] assets=%s save=%s\n", GetAssetRoot(), GetSaveRoot());

    g_debugFeaturesEnabled = ReadInt(path, "Debug", "EnableDebug",
#ifdef _DEBUG
                                     1
#else
                                     0
#endif
                                     );

    // The port always renders in hardware; the original's adapter picker is gone.
    g_dwSelectedDisplayAdapterID = 1;
    g_dwSelectedDisplayModeID = 0;

    dbg_printf("[CONFIG] loaded %s (%ux%u, %s)\n", path,
               (unsigned)g_dwScreenWidth, (unsigned)g_dwScreenHeight,
               g_bFullScreen ? "fullscreen" : "windowed");
    return TRUE;
}

// ---------------------------------------------------------------------------
void ConfigFile_Save(void)
{
    const char* path = ConfigFile_Find();

    // Slurp the file into lines so unknown keys, comments and section order
    // survive; only the keys we own are replaced. Inserted lines come from the
    // static pool, so only the ones recorded here may be freed.
    char* lines[MAX_LINES];
    char* malloced[MAX_LINES];
    int mallocedCount = 0;
    int count = 0;
    s_newLineUsed = 0;

    FILE* f = fopen(path, "r");
    if (f != NULL) {
        char buf[MAX_LINE];
        while (count < MAX_LINES && fgets(buf, sizeof(buf), f) != NULL) {
            size_t n = strlen(buf);
            while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
            lines[count] = (char*)malloc(n + 1);
            if (lines[count] == NULL) break;
            memcpy(lines[count], buf, n + 1);
            malloced[mallocedCount++] = lines[count];
            count++;
        }
        fclose(f);
    }

    char values[kOwnedKeyCount][600];
    char keyHex[32 * 2 + 1];
    char sideHex[128 * 2 + 1];
    HexEncode(g_keyBindingData, (int)sizeof(g_keyBindingData), keyHex);
    HexEncode(g_joystickBindingData, (int)sizeof(g_joystickBindingData), sideHex);

    snprintf(values[0], sizeof(values[0]), "%d", g_bFullScreen ? 1 : 0);
    snprintf(values[1], sizeof(values[1]), "%u", (unsigned)g_dwScreenWidth);
    snprintf(values[2], sizeof(values[2]), "%u", (unsigned)g_dwScreenHeight);
    snprintf(values[3], sizeof(values[3]), "%u", (unsigned)g_dwBitDepth);
    snprintf(values[4], sizeof(values[4]), "%d", g_bVSync ? 1 : 0);
    snprintf(values[5], sizeof(values[5]), "%u", (unsigned)g_dwPlayCount);
    snprintf(values[6], sizeof(values[6]), "%u", (unsigned)g_dwClearCount);
    snprintf(values[7], sizeof(values[7]), "%s", keyHex);
    snprintf(values[8], sizeof(values[8]), "%s", sideHex);

    for (int i = 0; i < kOwnedKeyCount; ++i) {
        SetValue(kOwnedKeys[i].section, kOwnedKeys[i].key, values[i], lines, &count);
    }

    f = fopen(path, "w");
    if (f == NULL) {
        dbg_printf("[CONFIG] could not write %s\n", path);
    } else {
        for (int i = 0; i < count; ++i) {
            fprintf(f, "%s\n", lines[i]);
        }
        fclose(f);
    }

    for (int i = 0; i < mallocedCount; ++i) {
        free(malloced[i]);
    }
}
