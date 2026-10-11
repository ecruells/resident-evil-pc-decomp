// Addon.cpp - add-on hook registry (see Addon.h).
#include "Addon.h"
#include "../system/AssetPath.h"
#include <string.h>
#include <ctype.h>

#define ADDON_MAX_HOOKS 32

namespace {

struct Entry {
    AddonHookFn fn;
    char        addon[32];
};

// Function-local static: add-on files register from static initializers, whose
// order across translation units is unspecified, so the table must exist the
// first time any of them runs.
struct Table {
    Entry entries[ADDON_HOOK_COUNT][ADDON_MAX_HOOKS];
    int   count[ADDON_HOOK_COUNT];
    bool  initDone;
};

Table& GetTable()
{
    static Table t;     // zero-initialized (static storage)
    return t;
}

bool IEqual(const char* a, const char* b)
{
    for (;; ++a, ++b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        if (*a == '\0') return true;
    }
}

void Run(AddonHook hook)
{
    Table& t = GetTable();
    for (int i = 0; i < t.count[hook]; i++) {
        if (Addon_IsEnabled(t.entries[hook][i].addon)) {
            t.entries[hook][i].fn();
        }
    }
}

} // namespace

void Addon_Register(AddonHook hook, AddonHookFn fn, const char* addonName)
{
    if (hook < 0 || hook >= ADDON_HOOK_COUNT || fn == NULL || addonName == NULL) return;
    Table& t = GetTable();
    if (t.count[hook] >= ADDON_MAX_HOOKS) return;
    Entry& e = t.entries[hook][t.count[hook]++];
    e.fn = fn;
    strncpy(e.addon, addonName, sizeof(e.addon) - 1);
    e.addon[sizeof(e.addon) - 1] = '\0';
}

int Addon_IsEnabled(const char* addonName)
{
    if (addonName == NULL) return 0;
    for (int i = 0; i < GetAddonCount(); i++) {
        if (IEqual(GetAddonName(i), addonName)) return 1;
    }
    return 0;
}

void Addon_RunInit(void)
{
    Table& t = GetTable();
    if (t.initDone || GetAddonCount() == 0) return;   // wait until add-ons are configured
    t.initDone = true;
    Run(ADDON_HOOK_INIT);
}

void Addon_RunGameStart(void) { Run(ADDON_HOOK_GAME_START); }
void Addon_RunRoomLoad(void)  { Run(ADDON_HOOK_ROOM_LOAD); }
void Addon_RunFrame(void)     { Run(ADDON_HOOK_FRAME); }
