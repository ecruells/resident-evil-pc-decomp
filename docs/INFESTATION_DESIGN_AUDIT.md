# Infestation rules, balance and player-experience audit

Date: 2026-10-05. Scope: the current source checkout, primarily `src/game/mods/`, the title guide, and pickup/menu integration. No game was launched, no multiplayer session was run, and this audit changed no gameplay rules. Game assets are absent from this checkout, so seed distributions and physical route times could not be measured here. `QUICK_DEBUG` was enabled at the initial read and was commented out externally during the audit; that edit was preserved. Existing binaries still require a rebuild to reflect it.

This is a design and implementation review, not a claim that a particular side wins a measured percentage of matches. **Verified** means the relevant rule or code path is present; **risk** means the likely consequence needs a controlled play-test. Proposed numbers below are experiments, not established balance targets.

Follow-up, 2026-10-05: finding 2 has an implementation fix awaiting user play-testing. Chris now receives +50% world ammo, rounded down, and dropped/death-drop ammo is excluded from the bonus in both pickup awards and inventory-space checks. CPU regression checks and the Win32 Release build passed. The audit below describes the original reviewed rules; references to doubling are historical.

Further follow-up: finding 6 now has host-confirmed results awaiting user play-testing. Escape attempts are private requests; received escapes precede timeout/all-dead in the same host update, and a confirmed outcome is immutable. All copies use the host's outcome/duration, with scenario-seed checks and retention during game initialization. Protocol version is 41. CPU race/regression checks passed.

Pickup follow-up: finding 3's floor/drop pickup races now have host reservations and confirmed commits, awaiting user play-testing. Competing attempts award once; cancelled, disconnected and abandoned uncommitted claims release the item. Critical pickup/drop events bypass the effects inbox. The live world continues during confirmation, and dropped-ammo identification now uses the opcode pickup record rather than its model pointer. Protocol version is 42. CPU conservation/race checks and the Win32 Release build passed. The shared item box's separate last-writer race remains open.

Shared-box follow-up: finding 3's remaining box race now has host-approved atomic swaps. Conflicting withdrawals/deposits leave the losing survivor's inventory unchanged and refresh the selected slot. Transfer receipts reconcile committed changes during reconnect or timeout inventory recovery. Revisioned host snapshots repair missed updates and reject stale contents. Protocol version is 44. CPU competition/conservation/retry/reconnect checks passed; multiplayer play-testing is still required.

## Overall assessment

The foundation is promising: a clear team objective, a director who combines planning with possession, predictable placement restrictions, persistent monsters, character identity, and useful downtime in safe rooms. The biggest concerns are interactions between individually sensible rules.

Before adjusting monster prices, address item conservation, door protection, match configuration, and repeatable testing. Otherwise an apparent balance result can actually be debug settings, duplicated ammunition, network races, or a radically easier random route.

The main design tension is **a mapped objective race versus an escalating attrition game**. Survivors know where progression items are, can divide the work, and need only one escape. The director cannot reinforce their current rooms and unlocks several marquee monsters very late. Experienced survivors may bypass much of the intended escalation; inexperienced survivors may spend the clock navigating menus and logistics instead. Both are plausible under the same rules.

## Implemented rulebook

These are the normal rules with `QUICK_DEBUG` disabled. It was enabled at the initial read and disabled in the header by the end of the audit.

| Rule | Implementation |
|---|---|
| Players | One host/director, one to three survivors; unique character choices |
| Objective | Collect four crests, open the passage to the storeroom, attempt its courtyard exit; one survivor wins for the team |
| Director victory | Twenty-minute timeout or all present survivors dead |
| Map | Return mansion; randomized keys, key-door requirements, crests, weapons and supplies; puzzle/one-way locks generally opened |
| Information | Survivors can see key doors, key/crest locations and teammate rooms; Richard adds monster counts; director sees survivors and objectives |
| Setup | Map review, host starts after acceptance or after 90 seconds permits bypassing missing acceptances; survivors then wait through two-minute character selection while director prepares |
| Hall protection | Both hall floors protected until 60 seconds after all survivor states arrive; arrival wait has a 150-second fallback |
| Placement | Ordinary purchases exclude occupied rooms; once per minute, a Zombie/Hunter reinforcement can appear at the nearest valid non-entry door. Room caps use weighted slots and grow at six and ten minutes |
| Safe rooms | Item-box rooms prohibit director entry, map jumps, placement and traps |
| Door protection | Monsters within 2200 units stunned for five seconds on arrival, followed by ten seconds of immunity; immunity still resets on room reload |
| Returning through doors | Five-second minimum applies to returning to the previous room, not every exit |
| Economy | Starting points 500/800/1000; income 1/2/4 points per second with 1/2/3 living survivors; possessed hit +50, AI hit +25; death refunds 25% |
| Trap | Lock either side of a room's doors for ten seconds, costs 100, global sixty-second cooldown |
| Supplies | Shared finite pickups; additional ammo spots; tier-three weapon placements scale with survivor count, with extra composition handling for Barry |
| Death | Inventory drops, spectating after four seconds; one revive per survivor with no rescue deadline, unless match already ended |
| Revive | Five-second hold and spray, 25% HP; Rebecca two seconds, spray or plain green herb, 50% HP |
| Menus/maps | Shared world continues; survivor map holds the character still; enemy updates continue through inventory menus |
| Shared box | Host-approved atomic swaps; competing transfers preserve the losing player's inventory, and receipts support reconnect recovery |
| Disconnects | Thirty-second pause and survivor seat reservation after detection; same-client reconnect or saved-seat rejoin; expired survivors drop carried supplies. A host crash ends the match |

Sources: [mode](../src/game/mods/ZombieMode.cpp), [economy](../src/game/mods/ZombieEconomy.cpp), [randomizer](../src/game/mods/ZombieRandom.cpp), [lobby](../src/game/mods/ZombieLobby.cpp), [perks](../src/game/mods/ZombiePerks.cpp), [spectating/revival](../src/game/mods/ZombieSpectate.cpp), [traps](../src/game/mods/ZombieTraps.cpp).

### Monster costs and availability

| Monster | Cost | Unlock after survivor arrival |
|---|---:|---:|
| Zombie | 100 | Start |
| Green zombie | 125 | Start |
| Naked zombie | 150 | Start |
| Web spinner | 150 | Start |
| Cerberus | 200 | 3 minutes |
| Hunter | 500 | 10 minutes |
| Chimera | 400 | 12 minutes |
| Tyrant | 1800 | 15 minutes |

Prices above match `ZombieEconomy.cpp`; AGENTS.md now agrees. The current network protocol is 44. The rulebook incorporates implemented follow-ups; historical findings below describe the original problems where explicitly noted. The developer quick-debug documentation now correctly describes bypassed unlock waits and hall protection.

## Findings requiring attention before balance tuning

### 1. Debug mode fundamentally changes the match — critical, verified

At the initial read, `ZombieModeInternal.h:8` defined `QUICK_DEBUG`; it was commented out externally during this audit. When enabled, it grants 20,000 starting director points, bypasses monster unlocks and hall protection, shortens character selection to fifteen seconds, and adds loaded guns and all four crests to the hall. This bypasses the central progression objective and intended opening pressure. Rebuild every copy before treating that header edit as a change to the playable rules.

**Follow-up decision:** quick debug remains a developer-only testing switch; no player-facing debug ruleset or lobby distinction is planned. Use normal rules for balance experiments. Network version equality still does not prove identical asset tables or scenario generation; a scenario checksum remains a separate proposal.

### 2. Chris can multiply dropped ammunition — critical, verified code path; runtime reproduction pending

`ZombiePerks.cpp:zombie_mode_pickup_quantity` doubles ammunition without identifying whether it came from the world or a player. `ZombieDrops.cpp:zm_drop_place` turns dropped ammunition into a standard pickup carrying its existing quantity. `RoomEvents.cpp:room_event_item_pickup` applies the doubling hook.

A plausible loop is Chris drops 15 rounds, picks up 30, drops 30, picks up 60, and repeats. The 250-unit cap limits an individual pickup, not repeatable resource creation. Ammo transferred by teammates can also gain the bonus, including recovered death drops.

**Action:** apply scavenging only to original/generated world supply, once per world pickup. Dropping and reclaiming any stack must conserve its total. Add a focused regression check for 15 → drop → pickup remaining 15, and an ordinary world pickup still awarding 30 to Chris.

### 3. Shared item transactions are not exclusive — critical, verified

The drops source explicitly states two survivors taking the same drop within one round trip both receive it. Ordinary world pickup flags are merged after local awards rather than granted before them, so simultaneous original-item pickup also merits a reproduction test.

The box uses last-arriving slot writes. Two clients can withdraw the same locally visible item before the empty-slot update arrives. Two clients depositing different items into the same empty slot can overwrite one deposit. Reliable delivery does not make the transfer atomic.

**Why this matters:** this can happen through ordinary cooperative play. It breaks scarcity and can lose a progression item, making the team blame each other or the randomizer.

**Action:** host grants a pickup/withdrawal/deposit against an item identity or slot revision before inventory is changed, following the existing revive reservation pattern. Cover duplicate requests, rejected grants, disconnects and retries. Prioritize conservation of keys, crests, weapons, ammo and healing.

Sources: [drops](../src/game/mods/ZombieDrops.cpp), [world flags and box](../src/game/mods/ZombieWorld.cpp), [pickup award](../src/game/RoomEvents.cpp).

### 4. Door stun has no recovery interval — high, verified; exploit effectiveness unmeasured

`ZM_STUN_MS` and `ZM_STUN_COOLDOWN_MS` are both 5000. Immunity is counted from stun start, so a monster becomes eligible for another stun as soon as the first ends. Several survivors staggering arrivals can extend suppression. Room resets also clear the slot timers.

The five-second return restriction does not solve this: another survivor can enter, and a player may leave through a different door. In a small room, 2200 units can cover a substantial combat area. Free shots against the possessed monster during repeated entry stuns may make the director feel unable to act.

**Action:** preserve safe entry, but test a shorter entry stun with a longer recovery window, such as two seconds of stun followed by eight seconds of immunity. Store immunity by persistent monster uid where ownership/re-entry requires it. Compare with protecting the entering survivor briefly instead of disabling monsters for everyone. Do not allow protection to become a free shooting window without counterplay.

### 5. Late joins, disconnects and progression-item custody need an explicit rule — high, verified mechanisms; loss scenario needs testing

Implemented, pending multiplayer play-testing: a gameplay disconnect now reserves the survivor's seat and pauses simulation and timers for 30 seconds after detection. The original client reconnects automatically; a relaunched client can use its saved seat credential to restore the host's last received player checkpoint and shared world snapshot. On expiry the host drops the missing survivor's carried supplies at its last known position, within the existing drop capacity. New strangers still cannot join after GO.

**Remaining limits:** restoration uses the last received checkpoint rather than the exact crash instruction; attack/menu animations restart. A disconnected director can return while its host process remains running. A crashed host cannot reconstruct the match: survivors wait through the grace period and then show an explicit connection-loss ending. Exercise crash/relaunch, packet loss during restoration, expiry with a carried key, and death/rescue transitions before treating recovery as verified in play.

### 6. Final result arbitration can race — medium/high, verified ordering; runtime consequence unmeasured

`zombie_mode_net_frame` checks the host clock/end condition before polling incoming events. A survivor also begins its escape result locally when it sends WIN. Around the deadline, the host can choose timeout before processing an already-sent escape, while the survivor has chosen escape. Match-end handling accepts its first outcome and returns for later ones.

**Action:** make the host publish one final outcome with a match id and explicit precedence. Clients can show an escape pending state until confirmation. Define how escape, last death, and a revive commit at the boundary resolve. This is a fairness issue even in trusted groups.

## Balance and fun risks

### 7. Fast objective runs may outrun the interesting monster roster — high-confidence design risk

The generator guarantees eventual graph reachability, not a minimum challenge or completion time. Progression items are distributed to different rooms, but there is no route-length, key-depth, or arrival-time target. Full objective-location information reduces searching. Only one survivor must escape, and the four-crested exit opens shared progression.

Hunters, Chimeras and Tyrant arrive at ten, twelve and fifteen minutes. A successful coordinated route below ten minutes excludes most advanced tools. Moving those unlocks earlier without addressing novice route time could make slower groups miserable.

**Action:** first measure undefended completion time across seeds for one, two and three survivors. Decide whether the target is a fast tactical race or a twenty-minute escalation. Then schedule some director variety by progression milestones as well as time. Avoid forcing players to wait just to make late content appear.

### 8. Split-up play is strongly rewarded; cooperative play can be punished — high-confidence design risk

Splitting covers more objectives and blocks purchases in more rooms. Grouping concentrates firepower and enables Rebecca's heal/revive, but also concentrates director pressure and does less parallel collection. The rules do not require extraction as a group. A strong player can do the final run while others are expendable scouts or spectators.

**Action:** keep one escape as the accessible victory condition initially. Add positive cooperative reasons to stay within support distance: clear teammate requests, resource sharing that works safely, useful rescue opportunities, and meaningful completion credit. Measure group versus split routes before adding mandatory group interactions; forced coordination could make the mode worse for two-person sessions.

### 9. Director information encourages objective camping — high-confidence design risk

The director knows progression-item rooms and living survivor rooms. Spending on unavoidable objectives or the final approach is safer than spreading a small budget over optional rooms. The final escape itself has no distinct hold/extraction phase once the door is usable. This can compress a whole match into a few choke rooms.

**Action:** test deliberate camping of the first reachable key, each crest, and the roofed passage. Check alternate approaches and whether stun simply reverses the imbalance. If camping dominates, improve approach choices or warning cues before adding arbitrary anti-camping penalties. Clear objective progress could support reactive director decisions without perfect advance item knowledge.

### 10. Director reinforcement restrictions can create stretches with little agency — medium/high risk

No placing into occupied rooms is good protection against materializing enemies on top of survivors. Its interaction with low early budgets, blocked safe rooms/hall, and survivors clearing rooms can leave the director waiting or setting threats far ahead. A possessed monster also stands idle while its director uses the map, and can be shot during that planning.

The current code does recover from a possessed monster dying: it tries another room and otherwise opens the map to place a replacement. AGENTS.md's old claim that this necessarily ends the director's copy should not be treated as a current verified defect.

**Action:** measure time without a controllable body and time spent planning under attack. Consider a safe return-to-AI/unpossess command while browsing, and delayed, telegraphed reinforcement at valid entrances with survivor proximity restrictions. Preserve prediction as a director skill.

### 11. Hit income creates a strong positive feedback loop — medium/high risk

A normal zombie costs 100 and refunds 25 when dead. Two qualifying possessed-hit credits yield 100, so that zombie's purchase-plus-death-refund can be profitable. Three qualifying AI credits yield 75, covering its net cost. Credits count qualifying hit events, not proportional damage, and should be audited for grabs/multi-hit attacks.

The director needs rewards for playing well. However, players already losing health also funding additional threats can make one early mistake snowball, particularly in a one-survivor match where passive income is low.

**Action:** log damage, credited hits and net income by monster uid. Test smaller hit awards or damage-proportional awards with per-victim limits. Keep a dependable passive budget so a struggling director still has choices, without requiring them to land hits first.

### 12. One, two and three survivors are substantially different games — verified scaling; balance effect unmeasured

The objective and twenty-minute timer remain the same. Extra survivors add parallel objective work, initial ammunition and character perks; tier-three weapons also scale. Most world supply does not scale directly in proportion to the party. Director starting points and income scale differently: three survivors generate twice the income of two, while adding one player.

Assuming normal rules, two minutes of preparation at the fallback income of one point/second, every survivor stays alive, and no purchases, hits or refunds:

| Match minute | 1 survivor budget | 2 survivor budget | 3 survivor budget |
|---:|---:|---:|---:|
| 3 | 800 | 1280 | 1840 |
| 10 | 1220 | 2120 | 3520 |
| 12 | 1340 | 2360 | 4000 |
| 15 | 1520 | 2720 | 4720 |
| 20 | 1820 | 3320 | 5920 |

These are cumulative available funds before spending, not expected balances. Setup timing can change the extra 120. A solo opponent cannot passively afford a 1800-point Tyrant at its fifteen-minute unlock even after saving everything; three opponents permit it comfortably, subject to earlier spending. Late content should not be assumed equally available in every party size.

**Action:** define separate target pacing for each survivor count. Prioritize 1v1: it needs only one friend and is the most practical first real test. Do not derive 1v3 balance from the AI mode.

### 13. Character power is uneven and team composition changes the economy — medium/high risk

| Character | Implemented benefits | Main design concern |
|---|---|---|
| Chris | 140 HP, +50% world ammo rounded down, handgun/30 spare rounds, lighter, six slots | Team can funnel world ammo through him; dropped/death-drop ammo receives no bonus |
| Jill | 96 HP, eight slots, sword-lock bypass, handgun/30 spare rounds, lockpick | Strong progression/logistics role; bypass value varies with randomized lock placement |
| Barry | 184 HP, penetrating gunshots, loaded six-round magnum, six slots | Nearly twice 96-HP characters' health; brings a heavy weapon in addition to the scenario's non-magnum tier-three placements |
| Rebecca | 96 HP, spray plus handgun kit, area spray healing, much better revives | Valuable when grouped; low personal resilience; after medic dies the rescue advantage disappears |
| Enrico | 140 HP, 10% movement, handgun kit, six slots | Speed helps avoid fights and finish objectives, potentially the best solo runner |
| Richard | 96 HP, global monster-count map, handgun kit, six slots | Information is valuable but no direct survival/resource advantage; count is not enemy type/HP and can lag until room capture |

Barry does not just replace a generated magnum. The generator skips magnum when selecting its party-sized set, so his starting magnum increases the team's total heavy-weapon count. Three survivors with Barry can have his magnum plus all three other heavy types. Treat this as deliberate composition power rather than an equal-strength starting kit.

**Action:** test distinct roles rather than flattening all characters. Compare Jill/Chris/Rebecca support compositions with Barry/Enrico runners. If Richard underperforms, improve usable intel or communication before increasing HP. Include difficulty and weapon effectiveness when assessing health; raw HP alone is not a ranking.

### 14. Early death can remove someone from most of the evening's match — high-confidence fun risk

Spectating is useful, and death drops preserve team supplies if the player remains connected. The ninety-second rescue deadline has been removed. A required resource, travel distance, director pressure and the retained one-revive limit can still turn an early mistake into many minutes of watching. A non-medic revive gives only 24 HP to a 96-HP survivor, 35 to Chris/Enrico, or 46 to Barry; inventory is on the floor, so rescue can immediately become an awkward pickup/menu sequence under attack.

All-dead victory is immediate. In 1v1 there is nobody to revive the survivor, so Rebecca's rescue benefit cannot help after death. This matters when using 1v1 to recruit testers.

**Action:** retain the rescue tension, but test brief post-revive protection and a clear corpse/resource/revive timer on the team map. Consider a casual ruleset with a longer rescue window or limited second chance for 1v1. Spectators should have useful team information or pings; avoid incentives to die deliberately for superior scouting.

### 15. Menus and item logistics may consume too much of the tension budget — medium/high risk

All but Jill have six inventory slots. Several kits already occupy four slots including knife and utility/healing, leaving two for keys, crests or loot. Progression is shared through unlocked doors, but carried keys/crests still consume personal slots. Constant dropping, storing and swapping can be the central workload for novices.

The world continues through inventory and map use, which supports multiplayer tension but changes a familiar RE1 expectation. Grab handling forces the survivor out of menus. Long pickup/viewer interactions also need checking for how their gates affect vulnerability. Ink ribbons remain in the randomized supply table even though they do not advance the match objective.

**Action:** make the live-world rule prominent. Time pickup, equip, heal and transfer operations under pressure. Consider a team progression pouch or automatic recording of collected keys/crests, while leaving combat resources in limited inventory. Remove or repurpose dead-end supply rewards if saving has no meaningful match use. Do not retain inventory friction solely because the single-player game had it.

### 16. Safe rooms are useful but can produce predictable resets — medium risk

Protected boxes make coordination possible. The director can still pressure adjacent unsafe rooms, so safe rooms are not automatically an infinite advantage; the clock also makes indefinite hiding a survivor loss. The risk is repeatedly disengaging to sort supplies while the director waits outside, rather than interesting pursuit.

**Action:** keep safe rooms until testing establishes a problem. Measure dwell time and repeat trips. Check camping just outside each safe exit and whether team supplies are easy enough to manage there. Resist adding punishment timers that remove the only place novices can learn the interface.

### 17. Monster caps measure count rather than encounter danger — medium risk

Weighted caps are implemented: Hunters/Chimeras use two slots, Tyrant three, and other monsters one. An empty room permits a Tyrant even if its cap is below three; later purchases must fit. Area alone still does not describe door geometry, firing lanes, stairs, or the number of viable paths. Monsters carried through doors can bypass purchase caps. The six/ten-minute increases can be dramatic in a small room. Generated monster and pickup anchors also share limited space and deserve physical clearance testing.

**Action:** identify the worst small choke rooms and validate each supported monster there. If needed, use threat-weight caps or per-type room exclusions. Decide whether carried monsters are deliberately allowed to exceed the room cap. The director needs accurate counts when remote kills have happened but roster capture has not yet updated.

### 18. Randomization proves solvability, not equal match quality — verified limitation

**Offline evaluation follow-up:** [Route evaluator and initial results](INFESTATION_ROUTE_EVALUATION.md)
now measure exact minimum door crossings in an idealized solo room graph using
the production generator. The initial 1,000-seed full-range sample found 90
final pickup graphs without an escape because the proof counted original
non-pool keys/crests later replaced with ammo. `rnd_reach` now excludes those
items from both assumed fill and the final proof. Repeating the same sample
found escapes for all 1,000 layouts, with a hardest-found route of 55 crossings
(`0x5BCA47BF`) and a median of 33. This is not a proven maximum or runtime
availability proof. Network version 45 prevents older seeded placement logic
from joining the same match. User play-testing remains required.
The subsequent shotgun restoration (version 47) changes the seeded pool and
reserves two keyless tools. The same 1,000-seed sample still has 1,000 modeled
escapes, now with maximum observed 56 crossings (`0xDF3277FE`), median 33.
Keys and crests are excluded from both shotgun trap rooms.
The [puzzle-access audit](INFESTATION_PUZZLE_ACCESS.md) also records internal
furniture gates and disabled door triggers that the proof does not model.
Infestation now keeps the piano alcove open by deactivating its sliding wall,
and prepares the native broken-floor and powered-elevator flags before room
init. Those routes require user confirmation; library access still needs
review. The shotgun trap room remains unchanged at the user's direction.

`rnd_reach` operates on room/door connectivity and assumed item ownership. It does not budget walking, door cinematics, inventory trips, combat, communication, actual pickup interaction, or physical obstructions. Door records inside script conditions can be read by the scanner without proving their runtime conditions. The weapon placer prefers key-gated rooms but eventually falls back to any free spot. The generator can fail after 200 attempts and leave the mansion unrandomized; lobby review does not check its return value.

**Action:** fail visibly before GO if generation or asset loading fails. Add a final scenario checksum, and verify actual pickup availability. Measure progression spheres, route cost and early weapon/healing access over many seeds. Reject extreme seeds only after defining acceptable variation. A veto does not substitute for a fair generator, especially before players understand the routes.

### 19. Setup adds avoidable waiting and is not fully synchronized — medium risk

Normal map review can last ninety seconds per seed and rerolls restart that interval. Character selection then waits out two minutes regardless of an early choice. Deadlines are set on each survivor's local receipt of GO, not a single host spawn deadline. Loaded-room arrival ultimately starts the clock, so a slow copy can extend preparation and briefly allow earlier copies to move before final weapons are locked.

Documentation follow-up: the guide now explicitly requires host confirmation after all accept or the ninety-second timeout, and correctly describes the five-second delay as a return restriction. Current monster prices and protocol information have been corrected. Shared preparation deadlines and an all-ready shortcut remain proposals; no setup timing was changed.

**Action:** use character selection before map generation, lock party composition, and show a common host-controlled preparation countdown. Let all-ready shorten waiting while retaining a maximum preparation time. Review the guide against actual normal/debug rules. State clearly whether the intended delay is anti-door-ping-pong or minimum room commitment.

### 20. Multiplayer setup and learning are a recruitment obstacle — medium risk

The lobby uses a host address and UDP port, rather than discovery/invitations. Detected version mismatches now show GAME VERSION MISMATCH and an instruction to update all players to the same build. If an older host silently drops incompatible packets, the client cannot distinguish incompatibility from an unreachable host; timeout guidance asks players to check address, port and game version. New players must understand tank controls, live menus, map controls, crest progression and a director economy. The title guide is a good start, but several rules only become obvious after a failed action.

**Action:** give the lobby a concise connectivity/status explanation and obvious mismatch error. Provide a playable survivor practice route and director practice encounter using the same normal rules. In-game feedback should expose entry protection, reason a purchase failed, corpse recovery, team objectives and a replacement-body action. No communication/ping system was found in the reviewed mod paths; teammate-room markers alone do not express intentions.

## What should be preserved

- One survivor's escape wins for the team: easy to understand and avoids forcing a doomed trailing player to finish.
- No instant monster placement into occupied rooms: makes danger readable and rewards director planning.
- Safe rooms: a necessary place for novices to plan, share, and understand inventory.
- Death drops and spectator continuity: a good starting point for keeping deaths part of the match rather than immediate ejection.
- Character-specific team roles: worth developing rather than reducing to cosmetic skins.
- Seeded progression and a pre-match map: support reproducible tests and strategy; tune information deliberately rather than removing it reflexively.
- Director body recovery: current implementation already provides a path back into the match after a monster dies.

## Testing without a full group

### What the existing AI mode can establish

It can exercise possession, monster movement, attacks, camera behavior, door traversal and broad director usability when the user runs it. It cannot establish the multiplayer objective race. The current AI walks a room graph, avoids locked doors, and does not manage progression inventory; the reviewed escape trigger is for the multiplayer survivor role. Offscreen behavior is not the same as a human survivor fighting persistent room monsters.

Do not use beating this AI as evidence that a director price or character matchup is balanced.

### Useful next offline work

1. **Item-conservation tests.** Replay pickup/drop/box operations with two and three simulated clients and delayed messages. Assert exactly one owner of each unique item and no ammo creation except an intentional once-only world bonus. Include Chris, death drops, partial stacks, and reconnects.
2. **Extract a pure scenario-validation harness.** Run the production randomizer on real assets across at least 1000 seeds. Avoid maintaining an approximate second randomizer. Record generation failure, reachable objectives, actual item spots, progression layers, key/crest room spread, heavy-weapon eligibility, healing access, and candidate route distance.
3. **Budget replay.** Feed hypothetical completion times, casualty times, hits, refunds and purchases into the economy. Compare 1/2/3 survivor modes and reveal when each monster is realistically affordable. This can rule out bad settings but cannot model hit probability accurately.
4. **Door/ownership event replay.** Script staggered entry at 0/5/10 seconds, owner exits, room reload and a director jump. Check stun continuity and one consistent monster HP/death state. Include repeated safe-room crossings and the last-death/escape timeout boundary.
5. **Asset/rules agreement.** Hash the final scenario and active normal/debug configuration. Reject incompatible copies before the match, rather than discovering disagreement through impossible pickups.

Those tests do not require launching the game. Actual route times, controls, combat feel and animation fairness still require user-run sessions.

### Smallest useful human test

One friend is enough for 1v1. A single user running host and survivor copies can reproduce item and networking behavior, but is not a credible combat-feel experiment. Do not require a 1v3 group before learning anything.

Use one fixed normal-rules seed, then swap director/survivor roles:

| Session | Question |
|---|---|
| Undefended route | How long do collection, travel, menus and crest placement take? |
| Early zombie pressure | Is there counterplay without doorway immunity abuse? |
| Door camping | Does arrival protection prevent unfair hits without granting free kills? |
| Objective camping | Is any first key or final approach effectively unavoidable? |
| Inventory under pressure | Can the survivor equip/heal/share without losing control or an item? |
| Death and disconnect | Does the match end clearly, and can progression items be recovered? |

Then recruit a third player for 1v2. It is enough to test Chris transfers, shared box races, Rebecca healing/revival, splitting, staggered stuns and ownership changes. The fourth player primarily validates scaling; it should not be the entry requirement for testing.

### Telemetry that answers balance questions

Current logs and end stats capture useful combat/economy information, but should also record:

- Seed, asset/rules hash, difficulty, party composition and final result.
- Objective pickup/insertion times, door opens, and identity/location of each progression item.
- Survivor route, safe-room dwell, menu/map time, and damage while menus are open.
- Director purchases/placement refusal reasons, idle time without a body, hit-income source and refunds by uid.
- Monster encounter duration, stun start/end, first action after stun, attacks and damage per purchase.
- Revive attempts/cancellations, item grants/refusals, disconnects and ownership transfers.

Start with completion-time spread, director active time, meaningful encounters, early spectator minutes, and conservation failures. Win rate alone cannot distinguish a tense match from twenty minutes of frustrating navigation.

## Recommended order of work

1. Establish a visible normal-rules test configuration and align the rulebook.
2. Fix Chris drop doubling and make shared item transfers exclusive.
3. Define disconnect custody and a single host-confirmed match outcome.
4. Test door protection and a recovery interval, preserving safe entry.
5. Add offline scenario validation and progression telemetry.
6. Measure undefended routes and a few 1v1 normal matches before changing economy or unlocks.
7. Improve director downtime, survivor logistics and rescue usability based on those measurements.
8. Tune compositions and 1v2/1v3 scaling separately.

The immediate objective should be a reliable, comprehensible 1v1 match with both players making meaningful decisions. That creates a practical base for recruiting more testers and makes later balance conclusions much more trustworthy.
