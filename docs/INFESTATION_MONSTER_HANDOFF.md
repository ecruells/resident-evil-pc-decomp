# Monster entry and possession audit

The first one-second patch did not cover the cross-copy door arrival: the
survivor still ran the room during the director's door animation, and could
start a bite once the timer expired. The director's local possession predicate
could not see that victim-side attack, and room-owner replacement could stop
its driver. CPU-only checks of the local predicate did not exercise this case.

The possession controller supports standing/naked/DC zombies, Cerberus,
Hunter, Chimera and rooftop Tyrant. The same entry and possession guards
cover each type; ordinary USA gameplay does not use these guards.

Fresh mod monsters initialize normally, then animate idle for one second
without running AI decisions, movement or attack hit tests. The delay begins
on their first update after initialization. This also covers the interval
between a reinforcement's roster announcement and its reliable entrance cue.
Director ownership is now a persistent `directorControlled` roster flag.
The carried body is announced with it in the first reliable ROSTER packet,
including its carried health. Map jumps reserve their destination uid before
loading, and possession switches claim the new body and explicitly release the
old body. A controlled monster on another room owner's copy keeps animating
idle indefinitely, with normal targetability, until the director can run it or
releases it. Survivor captures cannot change this flag, and UDP snapshots do
not override it. It follows the roster through slot changes and reconnects.

The entrance cue itself now holds for one second instead of 500 ms. Puppets
continue displaying their owner's poses, and damage/death playback can interrupt
the idle. Room reloads also give newly instantiated mod monsters this delay.

START switching and map-jump selection exclude monsters playing attacks or
reactions. The director cannot switch or jump away during those sequences.
Room ownership also waits for an existing pair to finish. An owning copy
advertises a `roomPairBusy` STATE flag during zombie bites, Tyrant impales,
local victim release and remote bite replay. That copy retains the room even
when the director arrives. While waiting, the director displays the current
owner's monster pose instead of running another controller. A local zombie
bite also remains pinned to its actual local victim rather than selecting a
new nearer survivor. The zombie's temporary DEAD/busy bit is no longer encoded
as a corpse during bites in ENEMIES or roster/reconnect capture.

An unsuccessful map arrival stays on the director map instead of overwriting
the busy monster or creating another director body. Feeding can still be
disturbed, and a dead possessed monster can still trigger automatic handoff.
Possession requests are not queued; press again after the attack finishes.

The non-zombie controller previously treated any nonzero `ignore_player_flag`
as an attack. That includes ordinary patrol/idle states, so possession could
run the original update and select an autonomous attack. It now uses each
type's existing attack-completion predicate. Reaction ownership also persists
until an ensuing attack completes, and completion is checked before another
original update can run a decision brain.

| Monster | Paired playback audited | Multiplayer behavior |
| --- | --- | --- |
| Zombie | `zombie_attack`, local director bite, victim-side grab replay | Both halves remain driven until release; remote reservations block possession. |
| Cerberus | Maul/contact behavior and attack states 3/6/7 | Existing network path substitutes an ordinary hit for the unsynchronized maul. |
| Hunter | Pounce bite/drag, hold behaviors and state-2 action layer | Existing network path substitutes an ordinary hit for the unsynchronized pounce drag. |
| Chimera | `chimera_behavior_grabhold` | Existing network path substitutes an ordinary hit for the unsynchronized grab. |
| Tyrant | Impale, including action-layer and victim-side replay | Existing paired synchronization remains intact; possession waits for completion. |

Network protocol is now 53: ROSTER argument 7 uses bit 15 for director
ownership (entity id remains in bits 8..14), and STATE flag 0x40 carries the
pair lease. Every player must update. `[control]` traces record claims,
releases and the director waiting for paired playback.

Door departure follow-up: the director publishes its carried body's departure
even while a survivor retains room ownership. Departed UIDs remain tombstones;
late ENEMIES, room captures and reconnect exports cannot revive them, and room
spawning/counting excludes them. This closes a path where the director could
restore an old body that survivor copies correctly rejected as departed.
The regression sends late poses and roster captures after departure and checks
that the destination body has an independent UID.

Rescue arrivals now form a triangle: the rescuer faces diagonally toward the
double doors and the two rescuees face back toward him. After the synthetic
door load completes, camera coverage is selected from the player's position.
The exact placement needs a visual play-test against the supplied arrows.

Trap camera follow-up: keep the overhead introduction while idle, then lock
camera 2 throughout descent and retraction for every local role. This index
matches ROOM1150 event 0's cut_lock_set[02] at script +0x88, immediately before
the ceiling movement. Camera 1 faces the passage door and was initially chosen
incorrectly; the user confirmed ceiling movement works but rejected that angle.
Disarming preserves the current ceiling depth and retracts at the descent
speed rather than resetting its height. Repeated inactive network updates do
not restart the return. CPU tests cover a partial descent returning over the
same elapsed time and switching away from camera 0; visual validation remains
required.

CPU regressions: `python3 tests/test_zombie_director_control.py` and
`python3 tests/test_zombie_possession.py`. It compiles the
production entry animator and possession predicates for a 32-bit target and
checks initialization, animated idle timing, message pauses, attack/reaction
exclusion, remote grabs, feeding and death handoff. The reinforcement regression
checks the full one-second entrance boundary and existing purchase arbitration.

User play-testing is still required: walk a possessed monster through a door
near a survivor; buy doorway reinforcements; press START and attempt map jumps
during bites/impales; then verify the survivor regains control. Repeat for all
supported monster types, including single player where the original paired
dog/Hunter/Chimera animations still run. Capture `re1_debug.log` with
`RE1_DEBUGLOG=1` if a handoff still sticks.
