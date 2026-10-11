// Addon.h - code hooks for add-ons (addons/<Name>/src/*.cpp).
//
// An add-on's .cpp files are compiled into the game (CMake globs
// addons/*/src/*.cpp, Game.vcxproj does the same with a wildcard), so they can
// include any game header. They plug in by registering functions here with one
// line at file scope:
//
//     #include "game/Addon.h"
//     static void MyInit(void)  { ... }
//     static void MyFrame(void) { ... }
//     ADDON_ON_INIT("MyAddon", MyInit);
//     ADDON_ON_FRAME("MyAddon", MyFrame);
//
// A registered hook only RUNS when the add-on's name is listed in config.ini
// [Addon] Name= (case-insensitive), so an add-on can stay compiled in and be
// switched off by editing the config. The same name selects the asset folder
// <Assets Path>/Addons/<Name>/ (see AssetPath.h SetAddons).
//
// Hooks take no arguments; read the game's globals (Globals.h) for state.
#pragma once

enum AddonHook {
    ADDON_HOOK_INIT = 0,    // once, before the first gameplay session starts
    ADDON_HOOK_GAME_START,  // every time a session starts (new game, load, continue), after InitializeGame
    ADDON_HOOK_ROOM_LOAD,   // after each room's RDT has been loaded and set up
    ADDON_HOOK_FRAME,       // once per gameplay frame
    ADDON_HOOK_COUNT
};

typedef void (*AddonHookFn)(void);

// Register `fn` for `hook` on behalf of the add-on called `addonName`.
void Addon_Register(AddonHook hook, AddonHookFn fn, const char* addonName);

// 1 when `addonName` is listed in config.ini [Addon] Name=.
int  Addon_IsEnabled(const char* addonName);

// Called by the game at the matching points. Not for add-on code.
void Addon_RunInit(void);        // runs the INIT hooks the first time only
void Addon_RunGameStart(void);
void Addon_RunRoomLoad(void);
void Addon_RunFrame(void);

// Static registration helper, so a hook is one line at file scope.
struct AddonRegistrar {
    AddonRegistrar(AddonHook hook, AddonHookFn fn, const char* addonName)
    {
        Addon_Register(hook, fn, addonName);
    }
};

#define ADDON_CONCAT2(a, b) a##b
#define ADDON_CONCAT(a, b)  ADDON_CONCAT2(a, b)
#define ADDON_REGISTER(hook, addon, fn) \
    static AddonRegistrar ADDON_CONCAT(s_addonReg_, __COUNTER__)(hook, fn, addon)

#define ADDON_ON_INIT(addon, fn)       ADDON_REGISTER(ADDON_HOOK_INIT, addon, fn)
#define ADDON_ON_GAME_START(addon, fn) ADDON_REGISTER(ADDON_HOOK_GAME_START, addon, fn)
#define ADDON_ON_ROOM_LOAD(addon, fn)  ADDON_REGISTER(ADDON_HOOK_ROOM_LOAD, addon, fn)
#define ADDON_ON_FRAME(addon, fn)      ADDON_REGISTER(ADDON_HOOK_FRAME, addon, fn)
