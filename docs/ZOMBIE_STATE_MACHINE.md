# Zombie State Machine

> Resident Evil 1 PC — Enemy type 0/1/17  
> All addresses from Ghidra decompilation of `Biohazard.exe`

---

## Architecture Overview

The zombie enemy is driven by a **two-level state machine**:

1. **Top-level state** (`entity+0x84`, `ZombieState`) — dispatched from `zombie_states_table`
2. **Behavior sub-state** (`entity+0x86` `action_behavior` × `entity+0x87` `action_state`) — dispatched from `zombie_action_update` / `update_zombie_action`

```
zombie_update (0x004338c0)
  ├─ zombie_states_table[entity.state]()     ← top-level state dispatch
  │   ├─ zombie_init          (state 0)
  │   ├─ zombie_state_check   (state 1)      ← idle behavior → behavior table
  │   ├─ zombie_damaged       (state 2)      ← hit reactions
  │   ├─ zombie_die           (state 3)      ← death sequences
  │   ├─ zombie_dead_anim     (state 4)      ← no-op (handled by sprite system)
  │   ├─ zombie_attack        (state 5)      ← bite/grab/vomit on player
  │   ├─ (unused)             (state 6-7)
  │   ├─ zombie_action_update (state 8)      ← per-behavior action dispatch
  │   ├─ (unused)             (state 9)
  │   ├─ zombie_chase_player  (state 10-11)  ← chase + attack transition
  │   ├─ zombie_pushed_back   (state 12-13)  ← knockback recovery
  │   ├─ zombie_random_chase  (state 14)     ← staggered chase
  │   └─ zombie_eating        (state 15)     ← eating corpse
  ├─ Collision resolution (SetEntityScaHitData / ResolveEntityScaCollision / HandleEnemyPlayerCollisions)
  ├─ Room collision check
  ├─ Blood splatter physics (blood_splatter_physics)
  ├─ Camera switch zone check
  └─ Fade sprite rendering (entity_add_fade_sprite)
```

---

## Top-Level State Table

`zombie_states_table` @ **0x004bb2c8** — 16 entries indexed by `entity.state` (0x84).

| State | Handler | Address | Description |
|-------|---------|---------|-------------|
| 0 | `zombie_init` | 0x00433440 | One-time initialization: health, behavior, animation setup |
| 1 | `zombie_state_check` | 0x00433ae0 | Main idle dispatcher → routes to behavior handler by `behavior_flags & 0x0F` |
| 2 | `zombie_damaged` | 0x00433db0 | Hit reaction: stagger animation, falldown threshold, damage behavior |
| 3 | `zombie_die` | 0x004340e0 | Death sequence: fall backward, headshot, magnum pushback, lay-down |
| 4 | `zombie_dead_animation` | 0x004342d0 | No-op — corpse sprite managed by rendering system |
| 5 | `zombie_attack` | 0x004342e0→0x00435200 | 11-state attack FSM: bite, grab, vomit, head explosion, withdrawal |
| 6 | — | NULL | Unused |
| 7 | — | NULL | Unused |
| 8 | `zombie_action_update` | 0x00454ab0 | Behavior dispatch via jump table on `action_behavior` (0-7) |
| 9 | — | NULL | Unused |
| 10 | `zombie_chase_player` | 0x00433bb0 | Chase with attack transition when in FOV + range |
| 11 | `zombie_chase_player` | 0x00433bb0 | Mirror of state 10 |
| 12 | `zombie_pushed_back` | 0x00433c60 | Knockback recovery with attack opportunity |
| 13 | `zombie_pushed_back` | 0x00433c60 | Mirror of state 12 |
| 14 | `zombie_random_chase` | 0x00433cf0 | Staggered chase requiring collision push flag clear |
| 15 | `zombie_eating` | 0x00436690 | Eating corpse: loops animation until player approaches |

---

## Behavior Table

`zombie_behavior_tbl` @ **0x004bb280** — indexed by `behavior_flags & 0x0F`.

| Behavior | Handler | Address | Description |
|----------|---------|---------|-------------|
| 0 | `zombie_idle` | 0x004349d0 | Standing idle with random head turns (8 sub-states) |
| 1 | `zombie_slow_walk` | 0x00434cd0 | Slow walk to random target, footstep SFX, deceleration |
| 2 | `zombie_slow_walk` (laying) | 0x00434660 | Laying-down variant (stub) |
| 3 | `zombie_idling` | 0x00434730 | Simple idling dispatch |
| 4 | `zombie_walk2` | 0x00434750 | Walk with player awareness, transitions to chase |
| 5 | — | — | Laying eating (handled by state machine) |
| 6 | — | — | Dead — doing nothing |
| 7 | — | — | Laying eating (handled by state machine) |
| 8-10 | — | — | Other variants |

---

## Per-Frame Update Flow

### `zombie_update` @ 0x004338c0

```
┌─────────────────────────────────────────────────────┐
│ if g_message_flags & 0x04 (message system allows)  │
│   │                                                 │
│   ├─ zombie_states_table[entity.state]()            │
│   │                                                 │
│   ├─ Mirror state fields (for animation blending)   │
│   │  state_mirror = state                           │
│   │  ignore_player_mirror = ignore_player_flag      │
│   │  action_behavior_mirror = action_behavior       │
│   │  attack_behavior_mirror = action_state          │
│   │                                                 │
│   ├─ Decrement internal_timer                       │
│   │                                                 │
│   ├─ if state != ATTACK (5):                        │
│   │  ├─ SetEntityScaHitData(ENTITY)                 │
│   │  ├─ ResolveEntityScaCollision(player, ENTITY)   │
│   │  ├─ HandleEnemyPlayerCollisions()               │
│   │  ├─ room_collision_check() / floor check        │
│   │                                                 │
│   ├─ if splatter_flag:                              │
│   │  └─ blood_splatter_physics(hand_joint, 6)       │
│   │                                                 │
│   ├─ is_entity_in_switch_zone() → camera check      │
│   │                                                 │
│   └─ if in switch zone:                             │
│      ├─ entity_add_fade_sprite(pos, pushVel, 0, ang)│
│      └─ entity_add_fade_sprite(joint, sca_ptr, 0, …)│
└─────────────────────────────────────────────────────┘
```

---

## State Details

### 0. `zombie_init` (0x00433440)

One-time setup when the entity spawns:

1. **Health**: `zombie_health_tbl[rand & 0xF] - (rand % 22)` → 158–220 HP
2. **Behavior**: sets `behavior_flags` modifier from difficulty-dependent random table
3. **SCA data**: allocates 2 SCA data blocks from shared pool
4. **Animation**: initial `animationId` from `zombie_anim_id_tbl[behavior & 0xF]`
5. **Dead spawn**: if behavior == 6 → sets health=-1, enters die state
6. **Laying spawn**: if behavior bit 1 → sets scaMatrixData laying orientation, disables leg joints
7. **Vomiting spawn**: if behavior bit 6 → sets state=8 (action_update)
8. State transitions to **1** (state_check)

### 1. `zombie_state_check` (0x00433ae0)

Main idle behavior dispatcher:

1. Calls `zombie_behavior_tbl[behavior_flags & 0x0F]()` → behavior handler
2. Clears status_flags to lower 5 bits (`& 0x1F`)
3. `entity_check_alert_range(3000)` → sets `status_flags |= 0x80` if player within 3000
4. `entity_check_visual_range(4500)` → sets `status_flags |= 0x20` if player within 4500
5. Vertical check: if ΔY > ±100 → sets player above/below flags
6. Eating special case: `entity_check_visual_range(4000)` for eating zombies (behavior 5/7)

### 2. `zombie_damaged` (0x00433db0)

Hit reaction state machine:

```
on-damage event (hit_state != 0)
  │
  ├─ Hit counter: if hit_state & 0x78 == 0x08 → action_speed++
  │  └─ if action_speed >= hit_threshold → action_speed = 0x80 (falldown)
  │
  ├─ Stagger timer: if hit_state & 0x78 == 0x10 → stagger_timer--
  │  └─ if stagger_timer == 0 → action_speed = 0x80 (falldown)
  │
  ├─ Damage behavior table: zombie_damage_action_tbl[(hit_state & 7) + (laying? 3:0)] → action_behavior
  │
  └─ Dispatch damage behavior:
     [0] zombie_falldown     — fall to ground
     [1] short_push_back     — stagger backward
     [2] short_push_back     — stagger backward (mirror)
     [3] push_and_stagger    — strong pushback
     [4] push_and_stagger    — strong pushback (mirror)
```

### 3. `zombie_die` (0x004340e0)

Death sequence, 4 cases:

| `action_behavior` | Animation | Description |
|---|---|---|
| 0 | 8 (fall backward) | Standard death — falls back, plays SFX, corpse persists |
| 1 | 10 (headshot) | Headshot death — bonus speed at early frames, corpse persists |
| 2 | 16 (lay down) | Already on ground — simple death |
| 3 | magnum_shot_pushback | Magnum/explosive death — violent pushback animation |

Sets `Flg_on(roomItemsFlags, death_event_id)` to trigger SCD room event.

### 5. `zombie_attack` (0x004342e0 → 0x00435200)

11-state FSM for bite/grab/vomit attacks:

| State | Phase | Description |
|-------|-------|-------------|
| 0 | Init | Set roar SFX, determine attack type (facing/laying_front/laying_back), position player at grab point |
| 1 | Wind-up | Play wind-up animation via Joint_move |
| 2 | Start | Transition to damage loop, set attack timer (105 or 30) |
| 3 | Damage | Deal damage every 19 frames, blood billboard, button-mash timer reduction, detect player death |
| 4 | Release | Normal withdrawal or transition to head-bite (laying×facing) or vomiting (laying×facing_back) |
| 5 | Withdrawal | Finish animation, return to damaged state |
| 6 | Head bite | Keyframe-triggered head explosion: 5 billboards, joint tinting, SFX |
| 7 | Post-explosion | Billboard cleanup, enter dead-headless state (health=0xFFFF, death_timer=0x46) |
| 8 | Vomiting | Vomit on player, joint flagging, keyframe-triggered effects |
| 9 | Post-vomit | Cleanup, enter dead-headless state |
| 10 | Post-bite finish | Finish bite animation, transition to state 7 |

Attack types (entity+0x16C `attacking_direction`):
- **0**: Facing player — standard bite
- **1**: Laying down front — bite from ground
- **2**: Laying down back — vomit attack

Damage per tick: `zombie_damage_easy_tbl[behavior & 0xF]` (20–45) or `zombie_damage_normal_tbl[behavior & 0xF]` (35–45)

### 8. `zombie_action_update` (0x00454ab0)

Jump table dispatch on `action_behavior`:

| Behavior | Handler | Description |
|----------|---------|-------------|
| 0 | return | No action |
| 1 | (inline) | Chase walk — 4 sub-states: init, follow, decelerate, return to idle |
| 4 | `zombie_chase_player` | Chase with attack transition |
| 5 | (inline) | Attack follow-through — walk+SFX, falldown check, angle toward player |
| 6 | (inline) | Approach player — deals damage every 19 frames (lite attack) |
| 7 | `zombie_pushed_back` | Knockback recovery |

### 10/11. `zombie_chase_player` (0x00433bb0)

Chase with FOV-based attack detection:
1. `checkAngularViewAndDistance(700, 1500)` → player in view cone + within 1500?
2. `check_line_of_sight` → clear path to player?
3. If both true → transition to zombie_attack (state 5)
4. Otherwise → `entity_update_player_distance` → behavior dispatch

### 12/13. `zombie_pushed_back` (0x00433c60)

Knockback recovery:
1. `checkAngularViewAndDistance(512, 2200)` → wider FOV check
2. If player in range + clear LOS → transition to attack
3. Otherwise → `entity_update_player_distance` + `zombie_pushback_action`

### 14. `zombie_random_chase` (0x00433cf0)

Like chase, but requires `collisionFlags & 0x08 == 0` (no wall push active).

### 15. `zombie_eating` (0x00436690)

Eating corpse animation:
1. Loops animation 0x1D/0x1E randomly, creates blood billboard at frame 0x19
2. If player moves within 3000 (Manhattan distance) → stand up (state 3)
3. Standing animation 0x0D, clears laying flag → enters die state

---

## Key Data Tables

| Table | Address | Description |
|-------|---------|-------------|
| `enemies_update_functions_tbl` | 0x004d3c90 | Per-enemy-type update dispatch (32 entries). [0]=zombie, [6]=hunter, [10]=adder, etc. |
| `zombie_states_table` | 0x004bb2c8 | Top-level state dispatch (16 entries) |
| `zombie_behavior_tbl` | 0x004bb280 | Behavior handler dispatch (16 entries) |
| `zombie_health_tbl` | 0x004bb290 | Base health values (16 entries: 180–240) |
| `zombie_anim_id_tbl` | 0x004bb2a0 | Initial animation per behavior type (16 entries) |
| `zombie_behavior_mod_tbl` | 0x004bb2b0 | Behavior modifier per random seed (32 entries: 2–4) |
| `zombie_damage_action_tbl` | 0x004bb31f | Hit reaction behavior index (11 entries) |
| `zombie_recovery_timer_tbl` | 0x004bb400 | Falldown recovery multiplier (16 entries: 2–9 × 30 frames) |
| `zombie_attack_data_tbl` | 0x004bb3e8 | Falling attack speed/timer/angle (4 entries × 3 shorts) |
| `zombie_attack_anim_tbl` | — | Attack animation IDs per attack type (9 entries) |
| `zombie_damage_easy_tbl` | — | Damage per behavior (easy, 16 entries: 20–45) |
| `PTR_weapons_hit_detection_functions` | 0x004bb530 | Per-weapon hit detection callbacks (10 entries) |
| `PTR_post_hit_callbacks` | 0x004bb558 | Per-weapon post-hit effect callbacks (10 entries) |
| `weapons_ranges` | 0x004bb560 | Per-weapon-per-player range values (20 entries) |
| `weapons_damage_table` | 0x004bb698 | Per-weapon-per-enemy damage (easy, 30 entries) |
| `weapon_damage_tbl_normal` | 0x004bbffe | Per-weapon-per-enemy damage (normal, 30 entries) |

---

## Entity Field Reference

### State (0x84–0x8F)

| Offset | Field | Description |
|--------|-------|-------------|
| 0x84 | `state` | Top-level state (ZombieState enum) |
| 0x85 | `ignore_player_flag` | Disable player awareness temporarily |
| 0x86 | `action_behavior` | Behavior type → dispatches to handler |
| 0x87 | `action_state` | Sub-state counter within current behavior |
| 0x88 | `health` | Current HP (short, signed) |
| 0x8A | `hit_state` | Damage type (bits 0–2) + reaction phase (bits 3–6) |
| 0x8C | `blend_counter` | Animation blend countdown |
| 0x8D | `jointCount` | Number of active limbs/joints |

### Animation (0xBD–0xBF)

| Offset | Field | Description |
|--------|-------|-------------|
| 0xBD | `animationId` | Current animation ID (0x00–0x1E) |
| 0xBE | `animation_frame_id` | Current frame within animation |
| 0xBF | `timing_control` | Frame timing control byte |

### Movement / Combat (0x16C–0x18B)

| Offset | Field | Description |
|--------|-------|-------------|
| 0x16C | `attacking_direction` | Attack type (0=facing, 1=laying_front, 2=laying_back) |
| 0x16D | `dir_control_flags` | Direction control flags |
| 0x16F | `seq_counter` | Idle delay timer / attack phase step counter |
| 0x170 | `angle_turn_delta` | Angular turn step per frame |
| 0x171 | `move_timer` | Footstep/effect pacing countdown |
| 0x172 | `is_moving` | Movement active flag |
| 0x173 | `move_max_steps` | Movement phase threshold |
| 0x174 | `splatter_flag` | Blood splatter effect active |
| 0x177 | `reaction_timer` (high byte) | Hit reaction / damage recovery timer |
| 0x178 | `subpixel_pos_x` | 16.16 fixed-point X position accumulator |
| 0x17C | `action_speed` | Signed approach/retreat speed (×16); also hit counter |
| 0x17D | `hit_threshold` | Max hits before falldown |
| 0x17E | `behavior_step` | Sub-phase index (0–9) within behavior sequence |
| 0x17F | `action_counter` | Generic per-behavior flag/counter |
| 0x180 | `move_speed` | Default move/chase speed (45 for zombies) |
| 0x181 | `turn_speed` | Rotation speed (24 for zombies) |
| 0x182 | `internal_timer` | Internal cooldown timer |
| 0x188 | `stagger_timer` | Poise countdown — when zero, falldown triggers |

### Collision / SCA (0xDC–0xDF)

| Offset | Field | Description |
|--------|-------|-------------|
| 0xDC | `collisionFlags` | Collision callback flags (bit 3 = wall push active) |
| 0xDD | `Sca_info` | SCA configuration pointer |

### Room Events / Tracking (0x15C–0x16B)

| Offset | Field | Description |
|--------|-------|-------------|
| 0x15C | `sca_data_ptr` | SCA data pool allocation pointer |
| 0x163 | `death_event_id` | Room event flag index triggered on death |
| 0x166 | `player_pos_x` | Player last known X position |
| 0x168 | `player_pos_z` | Player last known Z position |

---

## Behavior Flags (`behavior_flags` @ 0x02)

| Bit | Mask | Name | Description |
|-----|------|------|-------------|
| 1 | 0x02 | `ZOMBIE_FLAG_LAYING_DOWN` | Zombie is on the ground |
| 2 | 0x04 | `ZOMBIE_FLAG_INACTIVE` | Disabled / dead on floor |
| 6 | 0x40 | `ZOMBIE_FLAG_VOMITING` | Vomiting state active |
| 7 | 0x80 | `ZOMBIE_FLAG_SCD_CONTROLLED` | SCD event is controlling zombie |

Behavior types (lower nibble of `behavior_flags`):
- 0: Normal zombie
- 2: Laying on ground
- 5: Laying eating corpse
- 6: Dead — doing nothing (spawned as corpse)
- 7: Laying eating corpse (variant)
- 10: Lying on floor

---

## Status Flags (`status_flags` @ 0x00)

| Bit | Mask | Description |
|-----|------|-------------|
| 0 | 0x01 | Entity is active/visible |
| 3 | 0x08 | Entity is dead |
| 5 | 0x20 | Player is above (vertical) / in visual range (distance) |
| 6 | 0x40 | Entity is aligned with player |
| 7 | 0x80 | Player is below (vertical) / in alert range (distance) |

---

## Dependency Functions

### Movement / Physics

| Function | Address | Description |
|----------|---------|-------------|
| `entity_update_wander_turn` | 0x00489800 | Random turn when stuck (wandering behavior) |
| `entity_update_player_distance` | 0x00434330 | Compute player distance, dispatch behavior |
| `entity_pathfind_update` | 0x0048ad10 | Obstacle-detection pathfinding state machine |
| `turn_toward_target` | 0x00489960 | Angular step toward/away from target position |
| `entity_rotate_toward_target` | 0x004899b0 | Alternative rotate toward target (with 180° toggle) |
| `getAngleTowardsTarget` | 0x00460450 | Compute PS1 angle to target position |
| `snap_player_to_grab_position` | 0x00489ee0 | Position player at enemy grab point for attack |
| `entity_add_fade_sprite` | 0x00456810 | Add entity to fade sprite render queue |

### Collision / Detection

| Function | Address | Description |
|----------|---------|-------------|
| `checkAngularViewAndDistance` | 0x00489cf0 | FOV wedge + Manhattan distance check |
| `check_line_of_sight` | 0x0048a4b0 | Room/obstacle line-of-sight check |
| `entity_check_angular_los` | 0x00489c60 | Angular FOV + room collision check |
| `entity_check_visual_range` | 0x0043bfa0 | Set status bit 0x20 if player within range |
| `entity_check_alert_range` | 0x0043bfe0 | Set status bit 0x80 if player within range |
| `ResolveEntityScaCollision` | 0x0041b0a0 | SCA volume collision between two entities |
| `HandleEnemyPlayerCollisions` | 0x00489e10 | All-entity collision + Yawn pushback |
| `SetEntityScaHitData` | 0x0041b2c0 | Set up SCA collision data for entity |

### Weapon Hit Detection (`src/game/WeaponDamage.cpp`)

| Function | Address | Weapons | Description |
|----------|---------|---------|-------------|
| `apply_weapon_damage` | 0x0043c020 | All | Master weapon damage dispatcher — iterates enemies, calls hit detection, applies damage |
| `weapon_hit_detect_knife` | 0x0043d690 | Knife (0) | Distance from weapon joint, per-enemy range offsets |
| `weapon_hit_detect_gun` | 0x0043d410 | HG/Shotgun/Python/GL (1–4) | 3D aim cone with near/far tiers, aim-up headshot cone |
| `weapon_hit_detect_projectile` | 0x0043d810 | Heavy (5–9) | Simple Euclidean distance check |
| `checkEntityInRangeCone` | 0x0043d590 | (gun helper) | 2D cross-product wedge test — entity inside aim cone? |
| `check_weapon_line_of_sight` | 0x0048a530 | (all weapons) | Multi-layer room obstruction check (4 partition layers, indices 3→0) |

### Damage / Effects

| Function | Address | Description |
|----------|---------|-------------|
| `blood_splatter_physics` | 0x00437d20 | Blood drop physics after hit |
| `joint_setup_attack_effect` | 0x0048a070 | Configure joint for attack special effect |
| `joint_enable_special_effect` | 0x0048a140 | Toggle joint special-effect flag |
| `magnum_shot_pushback` | 0x00436b70 | Magnum/explosive headshot death animation |
| `zombie_check_special_weapon` | 0x0043d8a0 | Clear hit_state on special weapon hit |

### Behavior Handlers

| Function | Address | Description |
|----------|---------|-------------|
| `zombie_idle` | 0x004349d0 | Idle with random head turns (8 sub-states) |
| `zombie_slow_walk` | 0x00434cd0 | Slow walk with footstep SFX |
| `zombie_idling` | 0x00434730 | Simple idling dispatch |
| `zombie_walk2` | 0x00434750 | Walk with player awareness / chase transition |
| `zombie_falldown` | 0x00436af0 | Fall down + recover (4 sub-states) |
| `short_push_back` | 0x00436c80 | Short stagger reaction |
| `push_and_stagger` | 0x00436f00 | Strong pushback stagger |
| `zombie_pushback_idle` | 0x00435d90 | Idle stagger animation |
| `zombie_pushback_stagger` | 0x004362a0 | Stagger backward walk (4 sub-states) |
| `zombie_pushback_action` | 0x00434310 | Pushback dispatcher (behavior=0 → idle, else → stagger) |
| `zombie_check_player_distance` | 0x004345b0 | Laying zombie stand-up check |
| `zombie_body_part_physics` | 0x00437c30 | Compute move_speed_current from joint displacement |
| `update_zombie_action` | 0x004342f0 | Master behavior dispatcher (behaviors 0-12) |

---

## SCD Command Integration

Zombie entities are spawned via the `cmd_omodel_set` SCD command in room scripts:

```
cmd_omodel_set:
  [enemy_slot] [enemy_type_id] [param3] [param4] [x] [z] [flags...]
```

Key parameters stored on the entity:
- `entity.id` = enemy type ID (0/1/17 for zombies)
- `entity+0x164[0]` = behavior flags from SCD opcodes
- `entity+0x167` = additional SCD parameter
- `entity+0x163` = `death_event_id` — room event flag to trigger on death

The entity update is driven from `update_entities` (0x0048f0f0) which iterates `g_EnemiesList` and calls `enemies_update_functions_tbl[entity.id]()`. For all zombie variants, this calls `zombie_update`.

---

## Weapon Damage System

> Source file: `src/game/WeaponDamage.cpp`

### Entry Point

`apply_weapon_damage(weapon_id)` @ **0x0043c020** is called each frame when the player fires. It:

1. **Collects active enemies** — scans `g_EnemiesList` (30 slots), records indices of entities with `status_flags != 0`
2. **Runs per-weapon hit detection** — calls `PTR_weapons_hit_detection_functions[weapon_id - 1]` for each candidate
3. **Keeps the closest hit** — `g_playerDisplacement` tracks the nearest distance
4. **Looks up damage** — indexes weapon damage tables by `(weapon_id - 1) + enemyType * 10`
5. **Applies damage** — `enemy->health -= damage`, sets `hit_state`, triggers post-hit callback
6. **Sets state** — transitions entity to state 2 (damaged) or state 3 (die)

```
Player fires weapon
  │
  ├─ apply_weapon_damage(weapon_id)
  │   │
  │   ├─ Collect all active enemies into activeIdx[]
  │   │
  │   ├─ For each candidate (status_flags & player.flags & 0xE0):
  │   │   ├─ PTR_weapons_hit_detection_functions[weapon_id - 1](range, entity)
  │   │   └─ Keep closest via g_playerDisplacement
  │   │
  │   ├─ check_weapon_line_of_sight(enemy_pos)
  │   │   └─ Tests 4 room partition layers (boundary indices 3→0)
  │   │      Returns 0 if ALL layers clear — shot is unobstructed.
  │   │      Weapons 0-4 blocked by walls; weapons 5+ (grenades) bypass.  │   │
  │   │
  │   ├─ Damage lookup: tableIdx = weaponAdj + enemyType * 10
  │   │   ├─ Easy:   weapons_damage_table[tableIdx] + weapon_hit_state_tbl_easy[tableIdx]
  │   │   └─ Normal: weapon_damage_tbl_normal[tableIdx] + weapon_hit_state_tbl_normal[tableIdx]
  │   │
  │   ├─ enemy->health -= damage
  │   ├─ enemy->hit_state = hitState | (weapon_id << 3)
  │   ├─ PTR_post_hit_callbacks[weapon_id - 1](enemy)  ← amputation, head explosion
  │   │
  │   └─ enemy->state = 3 (die) if health < 0, else 2 (damaged)
  │
  └─ Next frame: zombie_update → zombie_states_table[2] → zombie_damaged
```

### Per-Weapon Hit Detection Callbacks

`PTR_weapons_hit_detection_functions` @ **0x004bb530** — 10 entries:

| Index | Weapon | Function | Address | Method |
|-------|--------|----------|---------|--------|
| 0     | Knife   | `weapon_hit_detect_knife` | 0x0043d690 | Euclidean distance from weapon joint, per-enemy range offsets |
| 1–4   | Handgun, Shotgun, Python, GL | `weapon_hit_detect_gun` | 0x0043d410 | 3D aim cone with near/far tiers, aim-up headshot cone |
| 5–9   | Heavy weapons | `weapon_hit_detect_projectile` | 0x0043d810 | Simple Euclidean distance check |

### Aim Cone System (`weapon_hit_detect_gun`)

```
                    Player
                     ╱│╲
                    ╱ │ ╲  ← nearLeft / nearRight (50 depth)
                   ╱  │  ╲
                  ╱   │   ╲
                 ╱    │    ╲  ← farLeft / farRight (200+range depth)
                ╱     │     ╲
               ╱  cone vert  ╲
              ╱   shift=50    ╲
             ╱  (0 if aim↑)    ╲
            ╱                   ╲
           Enemy───────────────Enemy
              (head SCA vol)    (body SCA vol)
```

The cone is defined by two triangular wedges (near + far), each bounded by left/right vectors. The vertical offset (`g_svecScratch.x`) determines which SCA volume the cone intersects:

| Player State | `g_svecScratch.x` | Cone Position | Hits |
|---|---|---|---|
| Aim normal | 50 | Body height | Torso/leg SCA volumes |
| Aim up (`flags & 0x20`) | 0 | Head height | Head SCA volume |
| Aim down (`flags & 0x80`) | 50 | Body height | Skip zombie unless shotgun |

The cone test is performed by `checkEntityInRangeCone` (0x0043d590) which uses three 2D cross products to test if the entity position falls within the triangular wedge.

### Head Explosion Logic

The hit type (`hit_state`) determines which reaction the zombie uses:

| `hit_state & 7` | Body Part | Zombie Reaction |
|---|---|---|
| 0 | Torso | Standard stagger / fall backward |
| 1 | Back | Reverse stagger |
| 3 | Legs | Leg cut / crawl |
| 4 | Head | **Head explosion** / headless death |

Two paths produce a head hit:

1. **Damage table path** (`weapon_hit_state_tbl_normal[tableIdx]`): The Python (weapon index 3) has `hit_state=4` hardcoded for zombie enemies — **always headshots regardless of aim or distance**. The damage tables contain per-weapon×per-enemy-type hit_state values.

2. **Aim cone path** (`checkEntityInRangeCone`): When aiming up with any gun (handgun/shotgun), the cone shifts to head height. If `checkEntityInRangeCone` detects the entity inside the shifted cone AND the damage table has `hit_state=0` (body), the head hit is determined by which SCA volume (head vs torso) the cone intersects. The shotgun's short range + aim-up creates a tight cone that reliably hits the head SCA volume.

### Hit State Post-Processing

After the damage table lookup, `apply_weapon_damage` modifies `hit_state`:

```c
// Add aim-up modifier if player not aiming exactly horizontal
if ((player.flags & 0xE0) != 0x20)
    hitState += (player.flags >> 5);      // adds 0, 2, 4, or 6 depending on aim

// Add weapon type shift
hitState |= (weapon_id << 3);             // weapon identifier in bits 3-7

enemy->hit_state = hitState;
```

This composite `hit_state` is then read by `zombie_damaged`:
- Low 3 bits (0–7): hit direction/type → damage behavior table index
- Bits 3–6: reaction phase → determines stagger type
- Bit 7: from weapon modifier → used by `short_push_back` for joint severing logic

### Per-Weapon Post-Hit Callbacks

`PTR_post_hit_callbacks` @ **0x004bb558** — called after damage is applied for special effects:
- Amputation/limb severing (shotgun)
- Head explosion (Python, shotgun headshot)
- Push-back velocity (magnum)

