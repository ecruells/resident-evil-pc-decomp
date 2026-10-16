# Offline infestation route evaluation

`tools/evaluate_zombie_routes.py` measures the **shortest escape route** for
each production seed and searches for the layout with the longest such route.
The first cost model is one unit per door crossing. This establishes a travel
baseline without changing generation or starting the game.

## Run

Python 3 and a C++17 compiler are required. Windows automatically discovers
Visual Studio's x86 C++ tools. Linux uses `g++ -m32`; pass `--compiler clang++`
to select another compiler with 32-bit support. No game build is required.

From the repository root:

```sh
# Reproduce the checked-in full-range sample (sampling seed defaults to 0).
python tools/evaluate_zombie_routes.py --assets bin/Release/USA --random-seeds --count 1000

# Search a larger sample, keeping twenty hardest layouts and their routes.
python tools/evaluate_zombie_routes.py --assets /path/to/USA --random-seeds --count 10000 --top 20

# Inspect a specific seed, or search a consecutive range.
python tools/evaluate_zombie_routes.py --seed 0x4F6FA986 --output tools/route_single.json
python tools/evaluate_zombie_routes.py --start 1 --count 1000 --output tools/route_consecutive.json

# Solver regressions plus native production replay using real assets.
python tests/test_zombie_routes.py --assets bin/Release/USA
python tests/check_platform_boundary.py
```

`--assets` names the USA tree itself, containing `stage1`, `stage6` and
`stage7`. If omitted, the tool looks in `assets/USA`, `bin/Release/USA`, then
`build/linux/USA`. Stage/file names are case-insensitive. Missing files or
compiler errors stop the run; four-byte stub rooms remain valid absent rooms.
The report defaults to `tools/zombie_route_report.json` and is overwritten on
each run. A run containing failed generation or unsolved layouts writes its
report and **exits with status 1**, keeping the problem visible.

## What is evaluated

The adapter compiles source extracted from the current `ZombieRandom.cpp`:
the scanner, destination decoder, xorshift generator, extra-pickup allocation,
assumed fill, and reachability check. It uses the current opcode widths and
generated spawn table. Extra ammo allocation is included because it consumes
random numbers before progression placement. The roofed passage reads the
first-visit RDT, matching the game. Seed zero aliases `0x5EED1234`.

The adapter reproduces the progression portion of `zm_random_build`, including
its 200-attempt limit. Weapon placement does not move pool keys or crests;
the export excludes the non-pool original keys/crests that weapon placement
replaces with shells. It exports those removed items separately for diagnostics.
Compiler assertions reject changed room/item identifiers; extraction rejects
missing source boundaries. The report fingerprints the compiled adapter source
and relevant asset RDT contents so results can be associated with their inputs.

Breadth-first search operates on `(room, collected key/crest mask)` states.
Directed door edges enforce the seed's key assignments and the four-crest gate.
Entering a room collects its progression at zero cost. The search starts in the
main hall and ends at a stage-changing door from the storeroom, following
`zm_is_last_door`. Other exits from the mansion graph are excluded. Unnecessary
keys need not be collected. The reported minimum and replayable route are exact
**within this model**; tie-breaking selects one optimal route.

Each solved layout also reports:

- **Keyless crossings:** the optimal route to the same crests with key locks
  removed, retaining the crest gate.
- **Key-gate extra crossings:** the difference from that baseline. Both routes
  are optimized independently; this isolates the travel cost of key gating.
- **Repeated room entries:** visits after the first, excluding the final exit.
- **Progression spheres:** batches of items reachable with the ownership from
  previous batches. This measures staged availability, rather than elapsed time.
- **Sample percentile:** the fraction of solved sampled seeds whose minimum
  crossing count is at most this layout's. Tied scores share a percentile.

The report retains the hardest layouts, their complete routes, the crossing
histogram, percentile summaries, all failure seeds, and three detailed failure
examples. Route labels use one-indexed stages and hexadecimal room IDs; exported
raw layout arrays use the engine's zero-indexed stages. `collect` lists items
gained at that route step.

## Measurement before and after the solvability fix

The checked-in report samples 1,000 distinct nonzero seeds uniformly from the
32-bit range using sampling seed 0. Before the fix, 910 had an escape in the
final pickup graph and 90 did not. After the fix, **all 1,000 generated layouts
have an escape in the model**, with no generation failures. The report now
contains results for the latest generator; the table below records the original
solvability fix before the later shotgun restoration.

| Statistic | Before (910 solved) | After (1,000 solved) |
| --- | ---: | ---: |
| Minimum | 16 | 12 |
| Median | 33 | 33 |
| 90th percentile | 42 | 42 |
| 95th percentile | 45 | 45 |
| 99th percentile | 49 | 50 |
| Hardest found | 54 | 55 |

The post-fix hardest found is **`0x5BCA47BF`**: 55 crossings versus 28 with
key locks removed, and 24 repeated room entries. One optimal route collects:

| Crossing | Stage:room | Pickup |
| ---: | --- | --- |
| 3 | 7:12 | Sun crest |
| 11 | 6:08 | Shield key |
| 17 | 7:09 | Sword key |
| 26 | 7:19 | Armor key |
| 33 | 6:0C | Wind crest |
| 36 | 6:02 | Helmet key |
| 39 | 7:15 | Moon crest |
| 46 | 7:10 | Star crest |
| 55 | Storeroom to courtyard | Escape |

Before the fix, all 90 unsolved layouts became solvable in the model when the
removed original keys/crests were restored. Consecutive seed **11** reproduced
the discrepancy: its generation proof counted original non-pool progression
that weapon placement replaced with ammo. `rnd_reach` now collects only pool
progression, so both assumed fill and the final proof use the surviving pickup
set. Seed 11 is covered by the production replay regression and now has an
escape without restoring removed items. Network version **45** separates the
changed seeded placements from older builds; everyone in a match must update.
Runtime physical/script availability and user play-testing remain outstanding.
The subsequent [puzzle-access audit](INFESTATION_PUZZLE_ACCESS.md) identifies
additional script/furniture gates. Infestation now removes the piano alcove's
sliding wall and initializes Yawn's floor opening and the kitchen elevator in
their native ready states. Library access still needs in-room validation.

After restoring the shotgun rooms and reserving two keyless puzzle tools,
version **47** reruns the same 1,000 seeds with **1,000 modeled escapes**.
Minimum 15, median 33, P90 41, P95 44, P99 50, maximum observed 56 crossings
(`0xDF3277FE`, 31 with key gates removed). The current JSON report contains
these results. Shotgun rooms cannot receive keys/crests; the optional ceiling
timer, rescue, tools and reward collection are not part of the escape-distance
score. All players need version 47.

## Script-gated doors and furniture rooms (version 54)

The generator's proof (`rnd_reach`) and the evaluator now share three rules
from the [puzzle-access audit](INFESTATION_PUZZLE_ACCESS.md#gated-doors-and-unverified-rooms):
door triggers that scripts switch off follow the reviewed `kDoorGates` table
(four are closed: the wardrobe's costume-closet door, the 2F left stairs to
the rough passage, a degenerate front-of-attic record and the large library's
bookcase door to the lookout). A room counts only if the main hall
can be reached again from it. Keys, crests and puzzle tools stay out of the
`kUnverifiedRooms` furniture rooms. The adapter exports only usable doors as
edges, lists the gated ones (`gated_doors` in the report), and reports a seed
as `unreviewed_door_gates` if a gated door is missing from the table.

The same 1,000 seeds, report model v2: **1,000 modeled escapes**, no
generation failures. Seeded layouts differ from version 53, so every player
needs version 54.

| Statistic | v53 | v54 |
| --- | ---: | ---: |
| Minimum | 15 | 12 |
| Median | 33 | 34 |
| 90th percentile | 41 | 44 |
| 95th percentile | 44 | 46 |
| 99th percentile | 50 | 50 |
| Hardest found | 56 | 54 (`0x3D792FA2`, 23 with key gates removed) |

## Leaf rooms and puzzle weapons (version 55)

Keys, crests, the shotgun tools, the sheet music and the chemical now go to
leaf rooms (one neighbouring room), one each. The five puzzles hand out the
heavy weapons by route cost (see `AGENTS.md`, "Heavy weapons"). The adapter
exports `puzzles` (`[stage, room, cost, weapon]`), `leaves` and the four
key items in `tools`.

Same 1,000 seeds: **1,000 modeled escapes**, no generation failures, every
key item in a leaf room. Dead-end placement makes routes longer: minimum 28,
median 46, P90 54, P95 56, P99 59, hardest found 65 (`0xBB42E0B3`) crossings,
against 12 / 34 / 44 / 46 / 50 / 54 in version 54. The broken shotgun and
the pick axe fall in the 5..9 crossing band in about 93% of seeds; the rest
take any free leaf room.

## Time estimate

`tools/estimate_zombie_route_time.py` turns layouts into seconds. In each
room the runner goes straight from where it came in (the door record's
arrival point, +0x0E/+0x12) to the next door's zone or pickup, times a
detour factor (default 1.35), at run speed (0xD2 units a 33 ms tick, about
6,300 units/s). Each crossing adds the door time (default 0.75 s, the mode's
fixed door length), each pickup 2 s, and the crest door 16 s. The solo
estimate is a shortest-time search; the team estimate shares each round's
reachable key items among three survivors, who meet in the main hall between
rounds. No combat, healing, menus or mistakes.

```sh
python tools/estimate_zombie_route_time.py --assets build/linux/USA --random-seeds --count 1000 [--door 2.5]
```

Version 55, 1,000 seeds, 0.75 s doors: solo median 3.1, P90 3.7, P99 4.0,
worst 4.6 minutes (`0x0361524D`); team median 2.8, worst 4.2. With 2.5 s
doors the solo worst is 6.4 minutes. The run speed and detour are
uncalibrated; time one known route in play to scale them.

## Limits and next steps

This is an informed solo runner with unlimited progression inventory, retained
keys, zero-cost pickups/crest placement, and no character perks. It excludes
walking distance, door animation durations, turning, inventory/box trips,
combat, director decisions, and communication. Script-gated door triggers
follow the reviewed `kDoorGates` table, with puzzle gates closed. Collision,
furniture clearance and event-only doors (records built inside events) remain
unverified. Therefore the result is an
idealized route cost, not a match-duration estimate or multiplayer difficulty.

**55 is the hardest found, not a proven absolute maximum.** Sampling is a
reproducible seed search, not exhaustive optimization over every legal layout.
Use the sampled distribution as an initial calibration and keep its valid-seed
population explicit; do not make the current record a permanent 100-point ceiling.

The final pickup-set discrepancy is addressed. Next extend the evaluator with door/pickup positions and
collision-aware walking costs, followed by inventory/crest delivery rules,
character perks and coordinated survivor routing. Assess early supplies and
mandatory chokepoints separately from travel. Only then choose acceptable
generation ranges, and verify candidate layouts through user play-testing.
