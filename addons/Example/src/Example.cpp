// Example add-on code. Compiled into the game, but its hooks only run when
// config.ini has [Addon] Name=Example. Copy this folder to start your own.
#include "game/Addon.h"
#include "DebugPrint.h"

static void ExampleInit(void)      { dbg_printf("[addon Example] init\n"); }
static void ExampleGameStart(void) { dbg_printf("[addon Example] game start\n"); }
static void ExampleRoomLoad(void)  { dbg_printf("[addon Example] room loaded\n"); }

ADDON_ON_INIT("Example", ExampleInit);
ADDON_ON_GAME_START("Example", ExampleGameStart);
ADDON_ON_ROOM_LOAD("Example", ExampleRoomLoad);
// ADDON_ON_FRAME("Example", fn) runs every gameplay frame; keep it cheap.
