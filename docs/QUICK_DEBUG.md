# Quick zombie-mod testing

`QUICK_DEBUG` is a compile-time switch in `src/game/mods/ZombieModeInternal.h`,
currently enabled. With zombie mode
enabled it shortens survivor character selection from two minutes to 15 seconds,
starts the director with 20,000 points, and adds loaded weapon pickups around the
main hall. It includes the handgun, shotgun, both magnums, flamethrower, all three
grenade-launcher loads, rocket launcher, Ingram and Minimi, plus one each of the
Wind, Moon, Star and Sun crests. The broken shotgun and the pick axe lie in
the trap passage, and the sheet music in the piano bar by the piano, whose
key door starts unlocked. Normal randomized pickups remain available,
and collected debug pickups stay collected on re-entry.

Comment out `#define QUICK_DEBUG` in that header to disable it; uncomment it
to enable it. Rebuild normally on Windows or Linux and use the same setting
on every multiplayer copy. Map review retains its usual rules; monster unlock
waits and the initial main-hall protection are bypassed. This is a developer
testing switch, not a player-facing ruleset.
