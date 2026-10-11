// Difficulty.cpp - loads difficulty.ini and serves the values to the game code.
//
// File search order (first hit wins):
//   1. <asset base>/<mode overlay>/difficulty.ini   (e.g. assets/DC/)
//   2. <asset base>/difficulty.ini
//   3. <asset root>/difficulty.ini                  (e.g. assets/USA/)
//   4. <exe dir>/difficulty.ini, then ./difficulty.ini
// If nothing is found, or no valid [ModeN] section exists, the built-in
// STANDARD / TRAINING / ADVANCED trio is used.
#include "Difficulty.h"
#include "../Globals.h"
#include "../platform/platform.h"
#include "../system/AssetPath.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define DIFF_INI_NAME "difficulty.ini"

int g_DifficultyId = 0;

namespace {

DifficultyMode s_modes[DIFF_MAX_MODES];
int  s_count      = 0;
int  s_menuOn     = 1;
int  s_defaultId  = 0;
bool s_loaded     = false;

int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void SetMode(DifficultyMode* m, const char* name, int dc, int hc, int hj)
{
    memset(m, 0, sizeof(*m));
    strncpy(m->name, name, DIFF_NAME_LEN - 1);
    m->dcDifficulty    = dc;
    m->healthChris     = hc;
    m->healthJill      = hj;
    m->weaponDamagePct = 100;
    m->enemyDamagePct  = 100;
    m->ammoPct         = 100;
}

// Same trio and health values the Director's Cut uses.
void LoadBuiltIn(void)
{
    s_count = 3;
    SetMode(&s_modes[0], "STANDARD", 0, 0,   0);
    SetMode(&s_modes[1], "TRAINING", 1, 180, 150);
    SetMode(&s_modes[2], "ADVANCED", 2, 100, 70);
    s_menuOn    = 1;
    s_defaultId = 0;
}

bool IEqualN(const char* a, const char* b, int n)
{
    for (int i = 0; i < n; ++i) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
        if (!a[i]) return true;
    }
    return true;
}

bool IEqual(const char* a, const char* b)
{
    for (;; ++a, ++b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        if (!*a) return true;
    }
}

char* Trim(char* s)
{
    while (*s && isspace((unsigned char)*s)) ++s;
    char* e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    return s;
}

bool TryOpenPath(const char* raw, FILE** out)
{
    char buf[512];
    const char* p = plat_normalize_path(raw, buf, sizeof(buf));
    FILE* f = fopen(p ? p : raw, "r");
    if (!f) return false;
    *out = f;
    return true;
}

FILE* OpenIni(void)
{
    FILE* f = NULL;
    char cand[512];
    const char* base = GetAssetBase();
    const char* mode = GetAssetModeName();

    if (base && base[0]) {
        if (mode && mode[0]) {
            snprintf(cand, sizeof(cand), "%s/%s/%s", base, mode, DIFF_INI_NAME);
            if (TryOpenPath(cand, &f)) return f;
        }
        snprintf(cand, sizeof(cand), "%s/%s", base, DIFF_INI_NAME);
        if (TryOpenPath(cand, &f)) return f;
    }

    const char* root = GetAssetRoot();
    if (root && root[0]) {
        snprintf(cand, sizeof(cand), "%s%s", root, DIFF_INI_NAME);
        if (TryOpenPath(cand, &f)) return f;
    }

    char exeDir[240] = "";
    if (plat_exe_dir(exeDir, sizeof(exeDir)) && exeDir[0]) {
        snprintf(cand, sizeof(cand), "%s/%s", exeDir, DIFF_INI_NAME);
        if (TryOpenPath(cand, &f)) return f;
    }
    if (TryOpenPath("./" DIFF_INI_NAME, &f)) return f;
    return NULL;
}

// Returns mode index for "ModeN" (N = 0..DIFF_MAX_MODES-1), -1 otherwise.
int ParseModeSection(const char* sec)
{
    if (!IEqualN(sec, "Mode", 4)) return -1;
    const char* d = sec + 4;
    if (!*d) return -1;
    for (const char* p = d; *p; ++p)
        if (!isdigit((unsigned char)*p)) return -1;
    int n = atoi(d);
    return (n >= 0 && n < DIFF_MAX_MODES) ? n : -1;
}

} // namespace

void Difficulty_Reload(void)
{
    s_loaded = true;
    FILE* f = OpenIni();
    if (!f) { LoadBuiltIn(); return; }

    DifficultyMode tmp[DIFF_MAX_MODES];
    bool           have[DIFF_MAX_MODES];
    for (int i = 0; i < DIFF_MAX_MODES; ++i) { SetMode(&tmp[i], "", 0, 0, 0); have[i] = false; }

    int menuOn = 1, defId = 0;
    int section = -2;   // -2 unknown/ignored, -1 [Menu], 0..N mode index
    char line[256];

    while (fgets(line, sizeof(line), f)) {
        char* p = Trim(line);
        if (!*p || *p == ';' || *p == '#') continue;

        if (*p == '[') {
            char* e = strchr(p, ']');
            if (!e) { section = -2; continue; }
            *e = '\0';
            char* name = Trim(p + 1);
            if (IEqual(name, "Menu")) section = -1;
            else { int m = ParseModeSection(name); section = (m >= 0) ? m : -2; }
            continue;
        }
        if (section == -2) continue;

        char* eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = Trim(p);
        char* val = Trim(eq + 1);
        int   num = atoi(val);

        if (section == -1) {
            if (IEqual(key, "Enabled")) menuOn = num ? 1 : 0;
            else if (IEqual(key, "Default")) defId = num;
            continue;
        }

        DifficultyMode* m = &tmp[section];
        if (IEqual(key, "Name")) {
            memset(m->name, 0, sizeof(m->name));
            for (int i = 0; i < DIFF_NAME_LEN - 1 && val[i]; ++i)
                m->name[i] = (char)toupper((unsigned char)val[i]);
            have[section] = m->name[0] != '\0';
        }
        else if (IEqual(key, "Dc"))           m->dcDifficulty    = Clamp(num, 0, 2);
        else if (IEqual(key, "HealthChris"))  m->healthChris     = Clamp(num, 0, 255);
        else if (IEqual(key, "HealthJill"))   m->healthJill      = Clamp(num, 0, 255);
        else if (IEqual(key, "WeaponDamage")) m->weaponDamagePct = Clamp(num, 1, 1000);
        else if (IEqual(key, "EnemyDamage"))  m->enemyDamagePct  = Clamp(num, 1, 1000);
        else if (IEqual(key, "Ammo"))         m->ammoPct         = Clamp(num, 0, 1000);
    }
    fclose(f);

    s_count = 0;
    for (int i = 0; i < DIFF_MAX_MODES; ++i)
        if (have[i]) s_modes[s_count++] = tmp[i];

    if (s_count == 0) { LoadBuiltIn(); return; }

    s_menuOn    = menuOn;
    s_defaultId = Clamp(defId, 0, s_count - 1);
}

static void EnsureLoaded(void)
{
    if (!s_loaded) Difficulty_Reload();
}

int Difficulty_MenuEnabled(void)
{
    EnsureLoaded();
    return s_menuOn && s_count > 1;
}

int Difficulty_Count(void)
{
    EnsureLoaded();
    return s_count;
}

const DifficultyMode* Difficulty_Get(int id)
{
    EnsureLoaded();
    return (id >= 0 && id < s_count) ? &s_modes[id] : NULL;
}

const DifficultyMode* Difficulty_Active(void)
{
    EnsureLoaded();
    const DifficultyMode* m = Difficulty_Get(g_DifficultyId);
    return m ? m : &s_modes[0];
}

int Difficulty_DefaultId(void)
{
    EnsureLoaded();
    return s_defaultId;
}

int Difficulty_StartHealth(int charId)
{
    const DifficultyMode* m = Difficulty_Active();
    return charId == 0 ? m->healthChris : m->healthJill;
}

int Difficulty_ScaleWeaponDamage(int dmg)
{
    const int pct = Difficulty_Active()->weaponDamagePct;
    if (pct == 100 || dmg <= 0) return dmg;
    int r = (dmg * pct + 50) / 100;
    return r < 1 ? 1 : r;
}

int Difficulty_ScaleAmmo(int qty)
{
    const int pct = Difficulty_Active()->ammoPct;
    if (pct == 100 || qty <= 0) return qty;
    int r = (qty * pct + 50) / 100;
    if (pct > 0 && r < 1) r = 1;      // a non-zero setting never rounds to empty
    return r > 255 ? 255 : r;         // item quantities are bytes
}

int Difficulty_EnemyDamagePct(void)
{
    return Difficulty_Active()->enemyDamagePct;
}

// BioCardLayout's DC mode byte (g_DcGameMode) inside a save image; the same
// offset as OFFSET_DC_MODE in SaveLoadScreen.cpp.
#define DIFF_SAVE_DC_MODE_OFFSET 0x233

void Difficulty_WriteSave(char* fileBuffer)
{
    fileBuffer[DIFF_SAVE_OFFSET + 0] = 'D';
    fileBuffer[DIFF_SAVE_OFFSET + 1] = 'I';
    fileBuffer[DIFF_SAVE_OFFSET + 2] = 'F';
    fileBuffer[DIFF_SAVE_OFFSET + 3] = (char)g_DifficultyId;
}

void Difficulty_ReadSave(const char* fileBuffer, int fileSize)
{
    g_DifficultyId = 0;
    if (fileSize <= DIFF_SAVE_OFFSET + 3) return;

    if (fileBuffer[DIFF_SAVE_OFFSET + 0] == 'D' &&
        fileBuffer[DIFF_SAVE_OFFSET + 1] == 'I' &&
        fileBuffer[DIFF_SAVE_OFFSET + 2] == 'F') {
        const int id = (unsigned char)fileBuffer[DIFF_SAVE_OFFSET + 3];
        if (id < Difficulty_Count()) g_DifficultyId = id;
        return;
    }

    if (g_bDcMode) {
        const int dc   = (unsigned char)fileBuffer[DIFF_SAVE_DC_MODE_OFFSET] & 3;
        const int want = (dc > 2) ? 2 : dc;          // ADVANCED* -> ADVANCED
        for (int i = 0; i < Difficulty_Count(); i++) {
            if (Difficulty_Get(i)->dcDifficulty == want) { g_DifficultyId = i; break; }
        }
    }
}
