#pragma once

// Player-facing mode name. Use uppercase for the game's menu font.
// The hand-drawn title image is separate artwork (InfestedTitleImage.h).
#define ZM_MODE_TITLE "INFESTED"
#define ZM_GAME_TITLE "RESIDENT EVIL " ZM_MODE_TITLE
// Visual-only cue; called when a Tyrant arms its claw ribbon.
void zombie_mode_tyrant_trail_started(const struct Entity* e, unsigned short frames);
#include "../../Globals.h"
#include "ZombieBumpers.h"

// ============================================================================
// ZombieMode.h - port-added "play as a zombie" mod ([Mods] PlayInfested=1)
//
// Not part of the original game: nothing in here has an address in
// ResidentEvil.exe. With the key off every hook below is inert and the USA
// behaviour is unchanged.
//
// You play a zombie; the character you picked (Chris or Jill) is an AI
// "survivor" exploring the mansion, and you hunt it.
//
//   The zombie (ZombieMode.cpp)
//     Every room_set spawns it in a spare enemy slot at the player's arrival
//     point, right after the init SCD (so the model loop that follows loads
//     its EMD like any scripted enemy's). update_entities hands the slot to
//     zombie_mode_update(): the pad drives it. It walks, runs, crawls, opens
//     doors, grabs the survivor with the real zombie_attack (paired animation
//     and all), bites other zombies, and takes the survivor's bullets through
//     the real zombie_damaged / zombie_die. The camera follows it.
//
//   The survivor (ZombieSurvivor.cpp)
//     Only one room is ever loaded, so the survivor lives two lives:
//       - in the zombie's room it IS g_playerEntity: the real player state
//         machine, fed synthesized pad input by a small AI (walk to a door,
//         shoot the zombie, run when it gets close or ammo runs out);
//       - elsewhere it is a record (stage, room, position, timer) that moves
//         along the door graph read straight out of each room's RDT, while
//         g_playerEntity is parked outside the room where nothing sees it.
//     It walks into the zombie's room through a door, and leaves the same way.
// ============================================================================

// [Mods] PlayInfested - read by ConfigFile_Load.
extern bool g_bPlayAsZombie;

// The possessed zombie, or NULL when the mod is not driving anything this
// room. Cleared at the top of every room_set.
extern Entity* g_zombieModeEntity;

// InitializeGame: arm the mod for a new game (isNewGame = 1) and skip the
// main-hall intro, or disarm it (continued game / attract demo).
void zombie_mode_new_game(int isNewGame);

// Survivor inventory keeps the room and network running without player input.
bool zombie_mode_live_menu(void);
// A fatal bite leaves a living zombie feeding; restored after room/owner changes.
void zombie_mode_begin_feeding(Entity* e, bool restored = false);
bool zombie_mode_feeding_target(const Entity* e);
void zombie_mode_stand_feeding(Entity* e);
bool zombie_mode_feeding_update(Entity* e);
bool zombie_mode_menu_frame(void);
void zombie_mode_room_entry_sound(void);

// room_set: forget last room's zombie (before the init SCD).
void zombie_mode_room_reset(void);

// room_set, before init SCD: keep infestation's floor opening and elevator ready.
void zombie_mode_room_prepare(void);
bool zm_shotgun_use_broken(void);
bool zm_shotgun_use_shotgun(void);
bool zm_shotgun_can_return_shotgun(void);
bool zm_shotgun_menu_finished(void);
bool zombie_mode_shotgun_corpse(const Entity* e);
bool zm_shotgun_use_pickaxe(void);
// The item menu's USE of the sheet music at the bar's piano (ZombiePiano.cpp).
bool zm_piano_use(void);
bool zm_piano_menu_finished(void);
// The item menu's USE of the battery at the small elevator (ZombieKeypad.cpp).
bool zm_access_use_battery(void);
bool zm_access_menu_finished(void);
unsigned char* zombie_mode_pickaxe_name(unsigned char item);
unsigned char* zombie_mode_pickaxe_description(unsigned short description);
void zombie_mode_pickaxe_icon_init(const void* statusTim, unsigned int bytes);
const unsigned char* zombie_mode_pickaxe_icon(int imageRow);

// room_set: spawn the possessed zombie (after the init SCD, before the enemy
// model loading loop), and bring the survivor in if it is in this room.
void zombie_mode_room_spawn(void);

// room_transition_load, before BuildEnemySnap: take the zombie out of the
// outgoing room's enemy list so the snapshot only sees the room's own.
void zombie_mode_room_exit(void);

// room_transition_load, after the destination is in place: a camera-only
// transition keeps the room (and the zombie), so move it to the new spot.
void zombie_mode_after_transition(void);

// Synthetic spectator/director jumps load the room without the door sequence.
bool zombie_mode_skip_door_animation(const unsigned char* record);
// DoorAnimLoop: the frame every door animation ends on in the mode (no button
// skip), or -1 for the original's held-button skip.
int zombie_mode_door_frames(void);
// check_camera_switch: spectators use the watched copy's camera, not local zones.
bool zombie_mode_spectator_camera(void);
bool zombie_mode_spectator_player_frozen(void);

// update_entities: per-frame update of the possessed zombie (ENTITY is set).
void zombie_mode_update(void);

// game_loop: replaces update_player_anim / update_player_position while the
// mod is driving a room.
void zombie_mode_player_update(void);

// game_loop, around update_entities: while the zombie borrows the player
// entity for a room event, the enemies must not see it.
void zombie_mode_enemies_begin(void);
void zombie_mode_enemies_end(void);

// Two players, survivor's copy: the other player's zombie is an entity of the
// room driven by zombie_mode_puppet_update, and hidden while it is elsewhere.
bool zombie_mode_is_puppet(const Entity* e);
void zombie_mode_puppet_update(void);
bool zombie_mode_hide_entity(const Entity* e);
// Director's copy: a monster whose attack is running on the survivor's copy
// (update_entities runs zombie_mode_remote_grabbed_update for it instead).
bool zombie_mode_is_remote_grabbed(const Entity* e);
void zombie_mode_remote_grabbed_update(void);
// game_loop, after the player's turn: pump the two-player link.
void zombie_mode_net_frame(void);

// cmd_enemy_set, for a conditional spawn (script byte 4 == 0), after the
// record's own setup: false = the persistent roster has this monster dead,
// do not spawn it; otherwise ENTITY may now carry its saved position.
bool zombie_mode_enemy_spawn(Entity* e, unsigned char slot, unsigned char id);
// update_entities, after each entity's own update.
// update_entities, right before each active entity's own update (the monsters'
// target for it) - zombie_mode_after_entity_update follows it.
// False: skip its update this frame (a monster with no model to move).
bool zombie_mode_before_entity_update(Entity* e);
void zombie_mode_after_entity_update(Entity* e);

// Snd_em: every enemy sound cue (forwarded to the other copy for the
// possessed zombie).
void zombie_mode_on_snd_em(unsigned char id);

// The Capcom logo, the title's opening movie and the prologue after the
// character select: all skipped while [Mods] PlayInfested is on.
bool zombie_mode_skip_intro(void);
// A play-as-zombie game is running (a new game started with the key on).
bool zombie_mode_armed(void);
// The small dining room's Ingram (ZombieRandom.cpp, behind Chris's lighter):
// true for an item that counts its rounds down - in the mode the Ingram is
// not the PC unlock's endless one (PlayerAnimations.cpp weapon_autoaim_check
// / the hold-fire, MainMenu.cpp's count).
bool zombie_mode_ingram_finite(unsigned char itemId);
// A survivor's reload button (PlayerAnimations.cpp's gun aim hold): the
// cancel/run press while aiming plays the empty gun's reload (behavior 0x18)
// on a part-loaded one and tops it up, instead of switching targets.
bool zombie_mode_manual_reload(void);
// cmd_scd_event_create: true = this event script is a story scene (a voice
// line or a movie in it) or a monster's entrance (takes the control and drives
// an enemy); do not start it - its flag writes are made instead.
bool zombie_mode_skip_scd_event(int scriptIndex);
// create_room_event: the same for a walk-in zone's event (not action presses).
bool zombie_mode_skip_room_event(const unsigned char* entry);
// Messages that do not stop the game (ZombieMessages.cpp). set_message_display:
// _repeat - the same message just ended, do not start it again yet; _replace -
// a passive message is up and gives way to this different one (true = it was
// closed); _pause - after the text is chosen: the g_message_flags mask to
// clear instead of `pause` (0 for a message that asks nothing, only the
// control for a Yes/No prompt). UpdateMessageDisplay: _page_done stands for
// "Action/Cancel pressed" (a passive message's reading time instead),
// _is_passive picks it for the script-timed close too, _end - true = it was
// passive, do not restore the flags; _prompt_cancel - true = answer No now
// (grabbed or dead), _prompt_end - true = the bits it cleared are raised back,
// do not restore the whole backup. game_loop: _drop closes a passive message
// when a door or the menu is requested.
bool zombie_mode_message_repeat(unsigned short msgId, unsigned short pause);
bool zombie_mode_message_replace(unsigned short msgId);
unsigned short zombie_mode_message_pause(unsigned short msgId, unsigned short pause,
                                         const unsigned char* text);
bool zombie_mode_message_prompt_cancel(void);
bool zombie_mode_message_prompt_end(void);
bool zombie_mode_message_is_passive(void);
bool zombie_mode_message_page_done(bool pressed);
// How many characters a message types out a frame (the original's one; more
// in the mode).
int  zombie_mode_message_chars_per_frame(void);
// The new game's opening narration (0x5B, "They have escaped into the
// mansion..."): main_loop asks whether a press may skip it (never in the mode,
// so every copy shows it for the same time); UpdateMessageDisplay its timed
// pages' delay in frames (shortened in the mode).
bool zombie_mode_intro_unskippable(void);
unsigned char zombie_mode_message_page_delay(unsigned char frames);
// cmd_bit_op clearing g_message_flags bits `mask`: what it really clears. In
// the mode an SCD event's write keeps the player, monsters and effects running.
unsigned int zombie_mode_event_flag_clear(unsigned int mask);
// The event VM's state 1 about to run `op` on `entity` for event slot `slot`:
// the width to step over instead (a reveal leaves the player alone), or 0.
int  zombie_mode_event_pose_skip(int slot, const void* entity, const unsigned char* op);
// cmd_picked_item_test: true when `testId` was a randomized spot's original
// item and that spot's new item was just picked up (mods/ZombieRandom.cpp).
bool zombie_mode_picked_original(unsigned char testId);
// cmd_player_pos_set: true to leave the player where it is (a reveal's).
bool zombie_mode_player_pos_skip(void);
bool zombie_mode_message_end(void);
void zombie_mode_message_drop(void);
// set_message_display: the mod's own text for the message being set up (zm_message_show), NULL none.
const unsigned char* zombie_mode_message_custom(void);
// Examine events (an action press's close-up and message, ZombieMessages.cpp).
// RoomEvents.cpp: _event_init as an event slot (re)starts a script, _event_cmd
// around the SCD commands an event runs (slot, then -1). CmdFunctions.cpp:
// _cut_skip - true = drop this camera command (the close-up was already left),
// _cut_closeup after a cut_lock_set (the camera before it), _cut_restored after
// the event's own cut back. game_loop: _message_frame once a frame - a
// survivor moving or turning leaves the close-up.
void zombie_mode_event_init(int slot, int script);
void zombie_mode_event_cmd(int slot);
bool zombie_mode_cut_skip(void);
void zombie_mode_cut_closeup(unsigned char prevCam);
void zombie_mode_cut_restored(void);
void zombie_mode_message_frame(void);
// Door keys (ZombieMessages.cpp). door_try_enter, once the key turned: _key_turned
// uses it up at once instead of when its message closes. use_room_action_item and
// the message "use" action, a key's last use: _key_used_up(slots, index) discards
// it (true) instead of the inventory's "This key is useless now. Discard?".
void zombie_mode_key_turned(void);
bool zombie_mode_key_used_up(unsigned char* slots, int index);
// check_action_object, before an Action press runs its entry: true = refuse it
// (an examine/reveal still running, or its close-up still up). cmd_room_action:
// true = drop the call (an event cut short by a hit or grab no longer offers
// its pickup).
bool zombie_mode_action_busy(const unsigned char* entry);
// cmd_bit_op, a script turning the letterbox on (main-state MSF_INTENSITY_RAMP):
// false = drop it (an examine or a reveal - ZombieMessages.cpp).
bool zombie_mode_event_letterbox(void);
bool zombie_mode_event_room_action_skip(unsigned char actionIdx);
// cmd_room_action, before a handler runs on `entry`: true = drop the call (the
// attic's key or crest, held back while Yawn is in the room - ZombieYawn.cpp).
bool zombie_mode_yawn_guards(const unsigned char* entry, unsigned char handler);
// Yawn's bite (Yawn.cpp): true while a monster update's target is another
// copy's survivor swapped into the player entity - the bite's story flags
// (Yawn's poison) are that copy's, not this one's.
bool zombie_mode_target_remote(void);
// Yawn's two moves of the mode (Yawn.cpp actions 9 and 10, ZombieYawn.cpp):
// the selectors ask which to start (0 none, 9 the thrash, 10 the slam), and
// each strikes every survivor within `radius` of any of `points` (x, y, z) -
// the local one and, through PHURT, the other copies'.
int  zombie_mode_yawn_special(void);
// door_try_enter, an unlocked door about to be taken: true = held back (a
// boss room's door, the whole team gathered and asked first - ZombieYawn.cpp).
bool zombie_mode_boss_door_gate(const unsigned char* record);
bool zombie_mode_boss_prompt_answer(bool yes);
void zombie_mode_yawn_area_hit(const int (*points)[3], int count, int radius, int damage);
// The player model / weapon-animation block to load for character `base`
// (g_playerEntity.id & 3): a multiplayer survivor's pick (0 Chris, 1 Jill,
// 2 Barry), else `base`. EntityModelLoader.cpp.
int  zombie_mode_player_skin(int base);
// The EMD table index of the player's model, and the weapon-file block its
// in-hand weapons come from (Richard / Enrico: their NPC model, Chris's block).
int  zombie_mode_player_model_index(int base);
int  zombie_mode_player_weapon_block(int base);
// Which half of the engine's per-character player tables (`id & 1`: Chris 0,
// Jill 1) matches the body the player wears - its skeleton, animations and
// in-hand weapon files: Jill and Rebecca 1, everyone else 0. Outside the mode
// (or for a director) `id & 1` itself. PlayerAnimations.cpp, WeaponDamage.cpp.
int  zombie_mode_player_body(void);
int  zombie_mode_player_voice(int base);
// Enemy EMDs carry their own survivor reaction set. Keep it by monster type
// and latch it for a hit/grab; unrelated model loads must not replace it.
void zombie_mode_damage_model_loaded(const Entity* e, unsigned int header, unsigned int base);
bool zombie_mode_damage_anim_select(unsigned char enemyId);
// Paired attacks without a complete victim-side network handoff use a
// normal damage reaction in PvP. Original single-player attacks are retained.
bool zombie_mode_network_match(void);
bool zombie_mode_reconnect_wait(void);
unsigned int zm_game_time_ms(void);
// Floor/drop pickup: reserve and commit before the original inventory award.
bool zombie_mode_pickup_claim(unsigned char* evt, const unsigned char* record);
bool zombie_mode_pickup_waiting(void);
bool zombie_mode_box_shared(void);
bool zombie_mode_box_waiting(void);
bool zombie_mode_box_claim(unsigned int playerSlot, unsigned int boxSlot, unsigned char* item, unsigned char* quantity);
void zombie_mode_box_finished(void);
void zombie_mode_pickup_finished(void);
// Global inventory atlas: shipped Chris/Jill/Rebecca, movie Barry, model heads.
void zombie_mode_load_portraits(void);
int  zombie_mode_inventory_portrait(void);
// game_loop's death check: false lets a dead player end the game as usual;
// the mode keeps a dead survivor in the match and ends the game itself.
bool zombie_mode_hold_death(void);
// die_state: the match is over - the mode's own end screen ran, no death screen.
bool zombie_mode_match_over(void);
// PlayerPad_Update, after its edge detection: the raw pad bits a held
// modifier takes for itself this frame (the survivors' voice lines,
// ZombieVoice.cpp) - dropped before the remap. 0: the pad is left alone.
DWORD zombie_mode_pad_filter(DWORD rawHeld, DWORD rawPressed);
// apply_weapon_damage, after a hit (the end-of-match stats, ZombieStats.cpp).
void zombie_mode_on_weapon_hit(const Entity* enemy, short healthBefore);
// LoadEntityModel, after the player's EMD: a borrowed character takes Chris's
// animations.
void zombie_mode_player_model_loaded(void);
// LoadEquippedWeaponAnimation: the texture bank / page a borrowed character's
// weapon mesh is pointed at (Chris's sheet in this room). False: as usual.
bool zombie_mode_weapon_texture(int* bank, int* page);
// room_set, after its model loop (RoomInit.cpp).
void zombie_mode_room_models_loaded(void);
// room_set's model loop, for each active enemy: -1 load it as usual, else the
// EMD table index to load instead (another survivor's stand-in wears that
// player's model), or -2 to drop it (no texture banks / memory left).
int  zombie_mode_standin_model(const Entity* e);
// ...and around each model it loads: the mod's own entities load their
// textures on the mod's own page counter (true = switched; end it after).
bool zombie_mode_model_load_begin(const Entity* e);
void zombie_mode_model_load_end(void);
// Play3DSnd (SoundSystem.cpp): the local player's own sounds (bank 1 - shots,
// reloads, clicks) go to the other players in the room.
void zombie_mode_on_player_sound(int bank, int soundId, int vol, int pos);
// update_2d_effects runs the effects' behaviours (EffectSystem.cpp)...
void zombie_mode_effects_running(bool on);
// ...and apply_weapon_damage, from one of them: is it a replay of another
// copy's projectile, which finds its target but does no damage here?
bool zombie_mode_effect_damage_blocked(void);
void* zombie_mode_effect_replay_parent(void);
bool zombie_mode_effect_context_begin(void);
void zombie_mode_effect_context_end(void);
void zombie_mode_effect_created(unsigned char slot);
bool zombie_mode_weapon_fx_begin(void);
void zombie_mode_on_joint_tint(const JointStruct* joint, unsigned int color);

// game_loop's menu request: true while a zombie is being played (START is
// the possession key then, not the inventory).
bool zombie_mode_blocks_menu(void);

// Effect mirroring: game_loop marks the entity and player turns, where a
// fight's billboards come from; Effect_CreateBillboard reports each one.
void zombie_mode_fx_capture(bool on);
void zombie_mode_on_effect(unsigned char type, unsigned char depthGroup, short yaw,
                           const void* spriteInfo, const void* pos, char lightFactor, unsigned char slot);

// game_loop render pass: false while the player entity is parked.
bool zombie_mode_draw_player(void);

// game_loop, after the scene and room sprites: the mod's 2D overlay.
void zombie_mode_draw_overlay(void);

// check_camera_switch: the position camera zones are tested against (the
// zombie's), or NULL for the player's.
VECTOR* zombie_mode_camera_target(void);

// door_begin_transition (PlayerAnimations.cpp): true if the mod handled the
// door itself and the room must NOT change (the survivor walked out).
bool zombie_mode_door_begin(const unsigned char* record);
// door_try_enter: true if the director's LOCK DOORS trap holds this door shut
// for the local survivor (it has been told why; the caller clicks the lock).
bool zombie_mode_door_trapped(const unsigned char* record);
// door_try_enter: true if the director's possessed monster is the one at the
// door - it ignores the lock (and does not unlock it for anyone else).
bool zombie_mode_door_ignores_lock(const unsigned char* record);

// ---- The randomized scenario (ZombieRandom.cpp) ----
// cmd_item_model_set, before it reads the record: this game's item and
// quantity for the spot (op = the command's bytes, patched in place - the
// pickup reads the same bytes later).
void zombie_mode_item_spot(unsigned char* op);
void zombie_mode_item_action(unsigned char slot);
// door_try_enter: the key a key-locked door needs this game.
unsigned char zombie_mode_door_need(unsigned char lockFlag, unsigned char need);
// cmd_door_set: the door record (patched in place) with the mode's own key
// locks - the large gallery's door (ZombieRandom.cpp kAddedLocks).
void zombie_mode_door_record(unsigned char slot, unsigned char* record);
// cmd_item_model_set: the {TMD, TIM} model to show at this spot instead of the
// room's own (a randomized item's look), or NULL.
const int* zombie_mode_item_look(const unsigned char* op);

// ---- Survivor perks (ZombiePerks.cpp), the local multiplayer survivor's ----
// InitializeGame, after its own health and max health: the character's.
void zombie_mode_player_stats(void);
// Inventory display, pickups, item box and reloads: Jill has eight slots.
int zombie_mode_inventory_slots(int originalSlots);
// Pickup award and viewer's room check: Chris gets 50% more world ammo,
// rounded down. Dropped items (including death drops) keep their quantity.
unsigned char zombie_mode_pickup_quantity(unsigned char itemId, unsigned char quantity,
                                         const unsigned char* record);
// door_try_enter: Jill's lockpick opens the sword-key doors.
bool zombie_mode_has_lockpick(void);

// ---- The survivors' dropped items (ZombieDrops.cpp) ----
// The inventory's command box: the fourth row, DROP, for a survivor.
bool zombie_mode_can_drop(void);
// Drop the item (it leaves the inventory if this is true).
bool zombie_mode_drop_item(unsigned char id, unsigned char qty);
// The DROP row's word over its blank button.
void zombie_mode_drop_label(short x, short y);
// apply_weapon_damage: Barry's shots go through every enemy in the cone.
bool zombie_mode_shots_penetrate(void);
// Add_speedXZ on the player: Enrico moves 10% further.
int  zombie_mode_player_move_speed(int speed);
// Enrico: double knife damage, and half the zombie's bite-loop timer.
int zombie_mode_player_weapon_damage(unsigned char weaponId, int damage);
int zombie_mode_player_zombie_grab_time(int frames);
// game_loop, before its menu check: Richard's radio (OPTIONS opens the map).
void zombie_mode_survivor_input(void);
// menu_item_use_heal's result: Rebecca's spray heals the survivors near her
// too (and is used even with her own health full).
int  zombie_mode_heal_item_used(unsigned char itemId, int used);

// reduce_attack_time_by_btn_press: true and *reduce set while the mod decides
// how hard the grabbed survivor struggles (the real pad belongs to the zombie).
bool zombie_mode_grab_mash(char* reduce);

// zombie_attack_withdraw: a surviving victim pushes nearby zombies back too.
void zombie_mode_grab_push_off(void);

// Cinematic completion: start the match/results clocks after playback.
void zm_bumper_intro_clock_release(void);
void zm_bumper_end_clock_release(void);
