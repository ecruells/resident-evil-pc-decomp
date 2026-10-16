# PvP creature damage-animation audit

The PC enemy EMDs contain a separate survivor EMR/EDD block before the
creature's own animations. The `em10xx` and `em11xx` blocks differ for every
creature available in PvP. Selecting a character's mesh alone does not select
its damage animations. Jill and Rebecca need the Jill block; Chris, Barry,
Richard and Enrico use the Chris block.

`LoadEntityEMD` now selects the body-compatible model table and registers each
creature's reaction pointers separately while the zombie mode is armed. Model
loads cannot replace a reaction already in progress. The local monster update,
received PHURT, accepted zombie GRAB, and received TYRANT_REACT select the
attacker's reaction set. Room reset clears the pointers before model storage
is rebuilt. Instances sharing an EMD share its registered reaction set.

The zombie crawling-grab branch and Hunter pounce health threshold now use
the target's body rather than the scenario's player id. When a monster is
updating against a remote survivor, the body helper uses that survivor's
character. Scenario flags and the underlying player id remain Chris's.

| Creature | Shipped survivor reaction slots | PvP handling |
| --- | ---: | --- |
| White, naked and green zombies | 12 each | Standing/crawling grabs retain the existing victim-side GRAB handoff. |
| Cerberus | 3 | Normal front/back bites. The unsynchronized fatal maul uses normal bite damage instead. |
| Web spinner | 3 | Front/back hits use its own reaction block; acid uses the ordinary player reaction. |
| Hunter | 3 | Swipe reactions include the Hunter's index offset. Pounce contact deals 15 damage and lands, without entering the unsynchronized bite/drag. |
| Chimera | 6 | Swipes retain front/back reactions. A grab attempt deals 10/30 damage (normal/hard), then returns to AI without the unsynchronized paired hold. |
| Rooftop Tyrant | 7 | Existing TYRANT_REACT handoff retains knockback/impale handling and selects the Tyrant's own block. |

The Cerberus, Hunter and Chimera replacements apply to multiplayer only.
Their original paired attacks remain in single player. Full PvP handoffs for
these attacks are still future work: they require victim reservation,
attacker/victim pose ownership, input forwarding where applicable, release,
death, interruption and room-exit handling. These changes do not claim to
implement those paired sequences.

The director's departing body is removed by the reliable roster departure
event on copies still in the previous room. This is independent of creature
type. Departure records reject delayed poses and roster captures that would
recreate the old uid. Removing an active zombie grab also releases its victim;
negative health then reaches normal death handling. Fatal ordinary PHURT
damage reaches the normal death check instead of waiting in a reaction pose.

## Validation

Run `python tools/verify_zm_damage_models.py <USA asset directory>`. It reads
the shipped models, checks all six survivor skeletons, checks both variants
of all eight PvP creature models, and validates every reaction sequence and
referenced frame against its EMR/EDD bounds. The local USA assets pass all
checks, and all creature reaction frames use a 15-joint survivor skeleton.

The Windows Release build and `tests/check_platform_boundary.py` pass. No game
was launched. Linux compilation and manual multiplayer behavior remain to
be verified in the user's Linux environment.

For play-testing, use `RE1_DEBUGLOG=1` on every copy:

- Test front/back hits on each character, especially Jill and Rebecca.
- Put different creature types in one room and alternate their attacks. Add
  another creature while a survivor is reacting to check that loading its
  model does not change the current reaction.
- Test standing/crawling zombie grabs and Tyrant knockbacks/impales on both
  the room owner and another survivor's copy.
- Test Hunter pounces and Chimera grab attempts, including fatal hits and
  hits while an inventory menu is open; verify normal recovery or spectating.
- Walk each possessable type through a door with survivors left behind,
  then return. Map jumps should still leave the previous body behind as AI;
  doors should carry it out. Repeat during damage/death and ownership changes.
- Run a normal USA game and single-player zombie mode to check their original
  paired attacks.

`[reaction]` reports a missing model reaction set and `[world] carried`
reports removal of an outgoing body. No packet layout changed, so no network
version bump is needed; all participants should nevertheless use this build
to obtain the corrected behavior.
