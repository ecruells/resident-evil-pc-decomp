# Infestation puzzle-access audit

The solvability proof does not model every mansion puzzle. It follows room
doors, keys and crests; it does not execute scripts or simulate moving
furniture, trap states or collision geometry. Opening a lock flag is not the
same as enabling its door trigger, so the proof now honours a reviewed table
of script-gated door triggers and keeps progression out of rooms whose
furniture moves (see [Gated doors and unverified rooms](#gated-doors-and-unverified-rooms)).

`tools/audit_zombie_puzzle_access.py` statically inspects all 58 return-mansion
Chris-scenario rooms, using the first-visit roofed passage as the mode does.
It records conditional/inactive/event-created doors, script writes to door
action slots, item-use tests, boundary changes and moving room objects.
The evidence is in `tools/zombie_puzzle_access_report.json`.

```sh
python tools/audit_zombie_puzzle_access.py --assets bin/Release/USA
python tests/test_zombie_routes.py --assets bin/Release/USA
```

This is a static audit, not proof of runtime accessibility. The event
disassembler reports six undecoded instructions across six rooms; those
warnings are retained in the report. Furniture footprints, camera transitions
and interactions still need manual play-testing. The findings below separate
confirmed script/geometry mechanisms from possible progression consequences.

| Area | Evidence and progression implication |
| --- | --- |
| Piano bar, 6:0F | Object 0 is the sliding wall to the emblem alcove. Events 9–12 move it, and taking the gold emblem can close it. A generated pickup anchor at `(8300,0,17500)` lies in the alcove; seed 17 puts an armor key there. **Addressed in this change:** infestation room setup clears the wall's active bit, removing its rendering and collision. Its scripts do not reactivate it, so it remains absent across puzzle animations and room re-entry. The bar's exterior key lock remains randomized. |
| Large library / lookout, 7:16 → 7:18 | The room includes a movable bookcase (object 0) with positions selected by scenario bit `0x4F`, and a door trigger to the lookout. This is an internal furniture/access puzzle, not an additional mansion-key requirement. The graph cannot account for pushing the bookcase or verify clearance to the door and generated pickups. |
| Private library, 7:17 | Scenario bit `0x68` selects furniture positions; event 1 moves object 1 and updates that bit. A switch/furniture puzzle can change access within the room without changing the room graph. Actual pickup clearance has not been proved. |
| Yawn's second room / basement passage, 7:0C ↔ 7:1A | Door records deliberately have zero probe flags: their original down/up prompt events invoke the door actions explicitly. Infestation now sets scenario bits `0x27` (floor broken) and `0x28` (furniture moved aside) before init when entering either room. The native init selects the post-break collision boundary and displaced furniture; the original prompts remain. User travel/visual confirmation is pending. |
| Kitchen elevator, 7:1C → 6:10 | Init disables elevator door slot 2 while scenario bit `0x33` is **clear**; the frame script plays the power-restoration scene, sets that bit and restores the trigger. Infestation now sets `0x33` before kitchen init, keeping the elevator enabled without that scene. User travel confirmation is pending. |
| Shotgun trap rooms, 6:15 / 6:16 | First-visit assets are restored. The mod owns replacement/rescue and the 15-second ceiling timer; native fatal/story events are suppressed. Empty mounting plate locks the trap room's outer passage door. Keys/crests cannot spawn in either room; broken shotgun and pick axe are guaranteed keyless drops. User play-testing is pending. |
| Greenhouse, 6:0C | Six scripted vines (ID `0x0F`) are restored. Chemical use immediately shares completion, starting red water and vine death on loaded copies without taking player control. Completion prevents re-entry spawns for the match. The chemical is placed in a leaf room and is not included in escape progression ownership. No chemical-locked inter-room door was found here; physical pickup clearance still needs play-testing. |
| Tiger statue, 6:0D | Gem-driven original reward slots are re-armed by scripts, excluded from the pool, and their original crest/magnum become shells. Neither gem is required to collect a randomized progression item from those reward slots. New floor anchors are separate from those slots. |
| Armor room, 7:05 | The gas/statue puzzle re-arms its original crest slot, which is excluded from progression. New floor pickups do not use that reward slot. Moving statues, gas and their effect on safe movement are not simulated. |
| Gallery portraits, 6:17 | The portrait switches and related events remain. Its return-mansion crest record is an armed, top-level floor pickup; the room scanner treats it as an ordinary eligible spot. A portrait-puzzle interaction is not represented in the distance score. Check the randomized pickup's actual interaction in play-testing. |
| Study / trophy / dining statue | These retain switches, movable furniture, climbable objects and optional item interactions. The dining statue's blue gem and trophy room's red gem are not randomized progression items. Puzzle-controlled original pickup slots are filtered where the scanner sees their re-arming or variants; generated floor anchors still need physical clearance checks. These are candidates for in-room validation, rather than newly proved mandatory gem dependencies. |
| Roofed passage, 6:1A | Intentionally retained: all four crests must be collected and placed to open the storeroom door. The proof models crest ownership, but does not charge interaction or inventory-delivery costs. |

Infestation opens the piano wall and prepares the native open-hole and powered
elevator states. `zombie_mode_room_prepare` runs immediately before room init
SCD, guarded by the armed mode and exact return-mansion room IDs. Re-entry and
restoration reapply these states; the normal USA game keeps its original flags.
Desk rewards also open freely for every infestation survivor. The large
library (7:16) has a top-level magnum-ammo record whose pickup is driven by
`check_desk`, rather than an ordinary floor interaction. Randomized keys can
occupy that record. Infestation bypasses the desk-key and character rejection
checks while preserving its camera and take-item flow; ordinary USA desk
behavior remains intact. This also applies to other native desk interactions.
CPU regression checks cover all character IDs, locked/unlocked desks, missing
or present keys/lockpicks, and empty rewards. Collection still needs play-testing.

The battery slot (7:19, item flag `0x39`) and private-library MO disk slot
(7:17, item flag `0x4D`) now participate in weapon/supply randomization while
retaining their native pickup interactions. Both are marked `rewardOnly`:
keys and crests cannot be placed there. Neither original scenario item remains
in these slots. These extra reward candidates change seeded weapon layouts,
and the restored shotgun puzzle adds a rescue event and reconnect state,
so the network version is now **47**; all players need the updated build.
The remaining runtime access gaps (collision, furniture clearance,
inventory) mean that the evaluator's successful 1,000-seed result remains a
**room-graph** result.

Next, validate the opened basement route, library door clearance and enabled
elevator against actual infestation play. The generator should then
either model their prerequisites or deliberately normalize those mechanisms
at room setup. Extra pickup anchors should eventually be validated against
initial room state and dynamic furniture, in addition to the static floor.

For the current piano change, test seed 17 if available: reach the bar using
its randomized exterior key, walk into the alcove without music notes, collect
the extra armor key, take the original gold emblem, and leave/re-enter. Test
the ordinary USA piano puzzle separately to confirm its normal behavior.
For the hole/elevator changes, enter Yawn's second room before any encounter:
check the open-floor visuals and descent prompt, climb back from the basement,
and repeat after leaving/re-entering. Try the kitchen elevator before its
power-restoration scene and verify travel back as well. The shotgun trap room
now uses the restored two-solution puzzle described below.

## Gated doors and unverified rooms

`rnd_read_room` marks a door **gated** when its trigger is not plainly live:
the record is built inside an `if` block, is left unarmed for an event to
run (`room_action`, 0x24), has a zone one unit wide or deep, or an init or
per-frame script rewrites its slot with `room_action_arm` / `_reset`. Every
gated door must appear in `kDoorGates` (ZombieRandom.cpp), reviewed against
the mode's own flag state. A gated door the table does not name counts as
closed; the build logs it and the evaluator reports the seed as a failure.
The audit tool prints every gated door, and `tests/test_zombie_routes.py`
checks that the audit, the production scanner and the table agree.

| Door | State | Reason |
| --- | --- | --- |
| 6:05 dining room → main hall (slot 1) | open | Disarmed only while ScenarioFlags2 bit 2 is clear (before the hall's intro scene); `zombie_mode_new_game` sets it. Confirmed in play. |
| 6:06 main hall → dressing room (slot 3) | open | A second record for one camera; slot 1 is the same edge and lock. |
| 6:12 wardrobe → wardrobe closet (slot 1) | **closed** | The costume closet: built only while ScenarioFlags 0x7B (second playthrough) is set, a message otherwise. |
| 6:16 living room → trap room (slot 0) | open | `ZombieShotgun.cpp` governs it at run time; neither room takes progression. |
| 7:00 elevator car (slots 0, 1) | open | 1×1 zones its events run by slot: arriving from 7:13 rides to the kitchen, otherwise back up. |
| 7:01 2F left stairs → rough passage (slot 1) | open (keypad) | Init disarms it unconditionally; `ZombieKeypad.cpp` re-arms it once the pass number is keyed in. The route requires the note (`kAccessDoors`), as it requires the battery for the elevator doors 7:1C slot 0 and 7:13 slot 1. The rough passage's way back (slot 1) is built by the keypad and added to the route by `rnd_read_room`. |
| 7:08 deer room → 2F bedroom (slot 0) | open | Disarmed only while the stage-variant bit is clear (the first visit). |
| 7:0C ↔ 7:1A Yawn's hole | open | Unarmed records run by the down/up prompts; `zombie_mode_room_prepare` sets the open-hole state. |
| 7:0E front of attic → save room (slot 3) | **closed** | A 1×1 zone at the origin that no script runs. |
| 7:16 large library → lookout (slot 2) | **puzzle** (closed) | Re-armed live only on a frame with SysFlags 0x18 set (the bookcase's `flag_bank_set` zone), dead otherwise. Pushing it and door clearance are not modelled. |
| 7:1C kitchen → elevator (slot 2) | open | Disarmed while ScenarioFlags 0x33 is clear, which `zombie_mode_room_prepare` sets. |

`kUnverifiedRooms` lists the rooms where events move an object or a puzzle
flag picks an object's position: 6:05, 6:09, 6:13, 6:17, 6:1C, 7:02, 7:05,
7:0A, 7:16 and 7:17. Keys, crests, the broken shotgun and the pick axe are
not placed in them; weapons and supplies still are. The piano bar (wall
removed), the lesson room (hole prepared) and the shotgun rooms are handled
separately. A room can leave the list once its pickups are play-tested.

`rnd_reach` also counts a room only when the main hall can be reached again
from it. Doors can be one-way (the small library's door to the 2F right
stairs has no record back). Without this rule the proof could count an item
in a room it could not return from.

When reading conditions: `bit_test`'s third byte is the inverse of the
wanted bit - 0 passes when the bit is set, 1 when it is clear
(`cmd_bit_test` returns `bit ^ byte`). `mine_room_scd.py` prints it as
`cond=`.

## Optional puzzle reward candidates

Version 55 makes five of these the heavy-weapon sources (piano bar,
greenhouse, tiger statue, large gallery, armor room); see `AGENTS.md`.
The piano is restored as the mode's own interaction (`ZombiePiano.cpp`,
see `AGENTS.md`); the alcove's wall now stays closed until someone plays it.
The greenhouse's six original vines are restored. Chemical use sets shared
ScenarioFlags2 bit `0xA6` immediately and permanently for the match. The native
player/camera cutscene is suppressed. `ZombieGreenhouse.cpp` recreates the red
water effects and fountain tint on each copy while its local segmented vines
play their death animations; survivors retain normal control. Re-entry restores
the final water appearance and does not respawn vines.
The chemical requires a leaf-room pickup; `QUICK_DEBUG` adds another in the
greenhouse. Multiplayer death, re-entry and reconnect still need user testing.

This is a design inventory, not a claim that these puzzles have been converted
to cooperative mechanics. Existing room scripts do not establish a requirement
for simultaneous survivor actions. Keep escape progression independent of
these rewards until each interaction is verified in multiplayer and solo play.

| Location | Existing access mechanism | Reward policy / solo feasibility |
| --- | --- | --- |
| Armor room, 7:05 | Statues, floor switches and gas; native crest reward re-armed by scripts | Candidate for a strong weapon or substantial ammo. Verify one survivor can arrange statues and press the final switch before introducing cooperative changes. |
| Private library, 7:17 | Switch moves furniture; original MO disk on desk | Now an optional randomized weapon/supply slot. Retain the sequential switch interaction for solo play; verify pickup clearance. |
| Elevator room, 7:19 | Climb/scripted interaction invokes original battery pickup; its ordinary pickup rectangle is only 1 by 1 | Now an optional randomized weapon/supply slot. Verify the native climb and pickup prompt with replacement items. The courtyard battery has no role in escape. |
| Tiger statue, 6:0D | Blue/red gems reveal separate rewards | Candidate for gem-delivery rewards; currently outside the ordinary pool. Both gems and their pickup routes need validation. Solo can deliver them sequentially. |
| Gallery, 6:17 | Ordered portrait switches | Candidate for an optional reward after the sequence. First verify whether its return-mansion pickup is actually gated; it is currently eligible for progression. |
| Trophy room / dining statue | Move furniture, climb, push/drop statue to obtain gems | Candidate for rewards tied to completing the interaction, with persistent completed state so solo can perform each step in order. |
| Large library, 7:16 | Bookcase movement to lookout; locked desk separately | Bookcase route is a candidate for an optional reward. Desks now open freely in infestation and should not be scored as cooperative puzzles. |
| Shotgun trap, 6:15 / 6:16 | Bait/replacement and ceiling trap | Restored optional shotgun reward. A solo survivor brings the broken shotgun; a teammate outside can break the trapped door with the pick axe. |

For a future cooperative version, prefer persistent switches or movable props
over simultaneous button holds. A lone survivor can then complete the same
steps sequentially. If timed holds are desired, supply a solo latch or longer
timer based on the active survivor count, including after a disconnect/death.
Strong puzzle weapons should come from the existing tier-3 quota (currently
one per survivor), with compatible ammo, rather than silently adding extra
powerful guns. Guaranteed strong rewards and new cooperative mechanics are
not implemented by the two optional-slot changes.

## Restored shotgun puzzle

The return-mansion trap and living rooms load ROOM1150 / ROOM1160 instead of
ROOM6150 / ROOM6160, retaining stage 6 identities. Generator scanning uses the
same substitution. The first visit's shotgun (7 shells), mounting plate,
replacement model and ceiling are retained; its single-player rescue/fatal
events are replaced by `ZombieShotgun.cpp`.

One broken shotgun and one unused **Pick Axe** (`ITEM_PICK_AXE`, `0x4C`) are
placed in different ordinary pool rooms reachable without keys, each 3–8 door
crossings from the main hall. They survive party/Barry weapon refreshes. The
original broken-shotgun floor pickup becomes a supply; its mounting-plate
model remains a prop. Neither trap room nor scripted optional reward slots
can receive these tools or escape progression.

Take the shotgun, then USE a broken or working shotgun at its empty mounting
plate to prevent or stop the trap. Host confirmation consumes the inventory
item, preserves its ammunition and creates a pickup on the plate. Lifting that
pickup can start another cycle; pickup generations and placement receipts
prevent delayed transactions from duplicating a replacement.

If a living survivor is already in the trap room when the shotgun is lifted,
the host starts the ceiling immediately. Otherwise entering the empty-plate
trap room starts it. The occupant briefly looks up and the hall door locks
with a click. During the 15-second countdown the inner door stays usable in
both directions. Once the last living occupant leaves or dies, the ceiling
finishes immediately and both inner and outer doors become slab-blocked;
interacting displays "The door is blocked." The ceiling travels from -10280
to -4750 over the same 15 seconds, about four feet lower than the former
endpoint. After the lethal moment it keeps descending another 2250 units
over three seconds, with the chain loop continuing. Bodies are hidden during
the overhead view so they cannot clip through the moving slab. In the final 1.8 seconds every copy viewing the room uses native
crush camera 0 above the slab, retaining that view while blocked. Survivors
use their normal networked death scream; a completed slab also kills living
monsters, including the director's possessed monster. Monsters become silent
settled corpses; after one second of overhead view the director takes another
living monster through the normal handoff, or waits on the map if none remain. Returning either shotgun raises
the slab and resets the cycle. A completed slab cannot be broken with the axe.

During an active countdown, select PICKAXE then USE near either side of the
trap/hall door. Action at the door never uses it. Both the local copy and host
require no living monster within 1500 game units (approximately five feet)
of the survivor using the axe. Any living monster anywhere in the trap room
also prevents rescue, whether the axe is used inside or outside. Distant hallway monsters do not block use. A refusal asks the
player to clear the area. Success permanently breaks the outer door, consumes
one pickaxe and rescues every living survivor in both the trap room and shotgun
room, plus the rescuer, into separate arrival positions in the passage.
The rescuer arrives at (6500, 9400), with rescued survivors at (6500, 11000)
and (6500, 12600) in seat order. These positions clear the native corridor
boundaries with a 400-unit body radius; reconnect recovery uses the same
assignment. Inside use rescues all living occupants of both rooms as well.
The completed slab blocks both survivors and possessed monsters. After a
pickaxe rescue, both doors remain blocked and both trap/shotgun rooms reject
director entry, map jumps, placement and doorway reinforcements for the rest
of the match. Monsters left in the shotgun room die; the host issues the usual
25% refund once per roster UID. A possessed victim uses the normal monster/map
handoff. Before rescue, the director can still ignore ordinary locks while the
ceiling is moving, but cannot pass the completed slab.
When Barry uses the pickaxe and Jill is among the rescued survivors, copies in
the passage play the original Jill thanks and Barry sandwich reply after the
rescue fade (ROOM1091 event 0, V105_05 and V105_06), with a two-second pause
after Jill finishes speaking. Gameplay continues during
the recordings; leaving the passage or ending the match stops them. Other
character combinations and reconnect snapshots do not start the dialogue.
Every observer already in the hallway, including the director, gets a short
black fade during the break. The intact-door cover disappears to reveal the
original broken doorway. All roles in the hallway hear a quieter, low-pass
filtered copy of the native chain loop while the trap runs.

Expiry permanently kills trapped survivors on the host's decision. Their
inventories are destroyed without drops or revival; reconnect and timeout
loot cannot recover them. The host rechecks remaining progression after the
loss and declares a director win if escape is impossible. The proof respects
the current trap/slab state and reachable replacements or active-trap axe rescue.

Reconnect snapshots carry the phase, timer, replacement/ammunition, placement
receipts, breaker, rescue mask and permanent crush mask. Committed placement
and axe receipts reconcile older inventories once. A survivor who crashes
before an accepted rescue finishes restores into the passage, including when
its checkpoint was in the shotgun room. Retries do not consume another item
or replay the transition.

The unused pick axe has a suitable original PC model. Infestation supplies its
name, a CHECK hint about breaking doors, and a 40x30 indexed inventory icon
generated at application startup from the STATUS.TIM inventory palette. Its
uppercase PICKAXE name ends in native item-name return tag 7 so the pickup
question continues to the question mark and Yes/No prompt. Both
held inventory and shared-box icons use it. Director's Cut repurposes
its artwork as a radio; a dedicated replacement visual for that asset set is
not supplied by this change. Other available tools are square/hex cranks,
lighter and lockpick; no native crowbar item is defined. The pick axe is the
most direct original-game choice for breaking a door.

Play-test with all copies on protocol version 52. Check an occupant waiting
in the trap room before the shotgun is lifted, the head reaction/lock click,
and hallway sound. Leave the trap room for the shotgun room while it runs:
the ceiling should finish and both doors should report the blockage. Return
either shotgun, lift it again, and test inventory USE of the pickaxe from
both sides. Verify monster refusals, axe consumption, both rooms' occupants
arriving outside, and the hallway observer/director fade. Also test expiry,
no drops/revival, reconnect during placement/rescue and after crush, loss of
required crests versus keys whose doors are already open, and the PICKAXE
Yes/No pickup prompt. Visual/audio behavior and multiplayer play remain
unconfirmed until manual play-testing.

The runtime route proof starts from surviving players and reconciled inventories,
then collects only reachable unclaimed floor items, live drops and shared-box
contents. It respects already opened doors, individually inserted crests and
Jill's lockpick. Normal deaths remain recoverable; crushed inventories never
count. Missing network checkpoints postpone the decision instead of guessing.
Checks continue after a crush so a late committed pickup cannot hide key loss.
CPU regressions: `tests/test_zombie_shotgun.py`,
`tests/test_zombie_progression_loss.py`, `tests/test_zombie_shotgun_effects.py`, and the reconnect/result fixtures.

