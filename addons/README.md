# Add-ons

An add-on extends the game with extra characters, rooms, music, menu art and its
own C++ code, without editing the base asset trees or the original sources.

Switch one on in `config.ini`:

```ini
[Addon]
Name=MyAddon            ; several: Name=MyAddon,OtherAddon  (first listed wins)
```

## Files (assets)

Put them in `<Assets Path>/Addons/<Name>/`, laid out exactly like `USA/`:

```
Assets/
  USA/  JPN/  DC/
  Addons/
    MyAddon/
      Stage1/ROOM10000.RDT      new or replaced room
      Players/...               new or replaced character models
      Sound/...  Voice/...      music and sound
      Data/...                  menu art, fonts
```

For every file the game loads, the search order is: each add-on in the order
listed, then the `[Game] Mode` overlay (e.g. `DC/`), then the `USA/` or `JPN/`
tree. An add-on only carries what it adds or changes. The log (`RE1_DEBUGLOG=1`
on Linux, or the debug output on Windows) prints `[assets] add-on <Name>
supplies <name>/<folder>` the first time it provides a file from each folder.

## Code

Put `.cpp` files in `addons/<Name>/src/` (optional headers in
`addons/<Name>/include/`). They are built with the game: CMake and
`Game.vcxproj` pick up `addons/*/src/*.cpp` automatically (re-run CMake, or
reload the Visual Studio project, after adding the first file). They may include
any game header. Register functions with one line each:

```cpp
#include "game/Addon.h"
static void MyInit(void) { /* ... */ }
ADDON_ON_INIT("MyAddon", MyInit);
```

| Macro | Runs |
|---|---|
| `ADDON_ON_INIT` | once, before the first gameplay session |
| `ADDON_ON_GAME_START` | every session start (new game, load, continue) |
| `ADDON_ON_ROOM_LOAD` | after each room is loaded |
| `ADDON_ON_FRAME` | every gameplay frame |

A hook only runs while the add-on's name is in `[Addon] Name=`, so you can leave
the code compiled in and switch the add-on off from the config. Hooks take no
arguments; read the game state from the globals in `src/Globals.h`.

New characters and rooms usually need both parts: the files under
`Addons/<Name>/` and code (a hook, or edits to the tables) that makes the game
use them. `addons/Example/` shows the code side.
