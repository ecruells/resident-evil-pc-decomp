#include "ZombieModeInternal.h"
#include "ZombieNet.h"
#include "ZombieReconnect.h"
#include "ZombieMode.h"
#include "../../DebugPrint.h"
#include "../../platform/platform.h"
#include <cstdio>
#include <cstring>

// ============================================================================
// ZombieSpectate.cpp - a dead survivor watches the living ones (port-added).
//
// A survivor that dies stays in the match (zombie_mode_hold_death). Once its
// death has played out (ZM_SPEC_DELAY_MS) its copy follows one of the living
// survivors and shows that survivor's room through the camera that survivor
// sees: the room is loaded as a map jump loads one (a door record of our own,
// room_transition_load without a door animation), and STATE carries the
// watched survivor's active camera (zombie_mode_spectator_camera overrides
// the local player's camera-zone walk). Left/right
// picks the next survivor; a survivor that goes through a door is followed
// once its STATE has named the new room for ZM_SPEC_FOLLOW_MS.
//
// The body stays where it fell: from the moment the watching begins, this
// copy's STATE is frozen on the corpse (zm_net_freeze_state) - stage, room,
// spot, pose - with the spectating flag, so every other copy keeps showing it
// there. The flag also takes this player out of every room: it is never "here"
// for ownership (zm_player_here), the monsters do not go for it, and a copy
// that runs the room it watches sends that room's monsters for it as for a
// player there (STATE's view room, zm_spectated_here in ZombieMode.cpp).
//
// The copy itself counts as gone from a room once it has left it
// (zm_spec_room_exit): the corpse's room is captured to the roster on the way
// out if this copy still ran it, as on any door. Watching someone in the
// corpse's own room hands that room over at once when somebody else is there.
// While it watches, its pad is the spectator's (the hidden player entity is
// held still), and the story-flag watch is off: the room's scripts run for the
// watched player, whose own copy reports their changes.
// ============================================================================

#define ZM_SPEC_DELAY_MS   4000     // the death animation first
#define ZM_SPEC_FOLLOW_MS  400      // the watched survivor this long in a new room: follow

// Port-added revive tuning (no original addresses).
#define ZM_REVIVE_RANGE       800
#define ZM_REVIVE_HOLD_MS     5000
#define ZM_REVIVE_MEDIC_MS    2000
#define ZM_REVIVE_HEALTH_PCT    25
#define ZM_REVIVE_MEDIC_PCT     50
#define ZM_REVIVE_LIMIT          1

// Port-added state, reset explicitly at match start.
static bool s_deathSeen[ZM_NET_MAX_PLAYERS];
static int s_revives[ZM_NET_MAX_PLAYERS];

void zm_spec_reconnect_export(int out[4]) { memcpy(out, s_revives, sizeof(s_revives)); }
void zm_spec_reconnect_import(const int in[4]) { memcpy(s_revives, in, sizeof(s_revives)); }
static int s_reviveTarget = -1;
static unsigned int s_reviveAt = 0;
static int s_reviveHealth = 0;
static bool s_returning = false;
static ZmNetPeerState s_corpse;
static int s_claimOwner[ZM_NET_MAX_PLAYERS];
static bool s_claimDone[ZM_NET_MAX_PLAYERS];
static int s_requestTarget = -1;
static short s_requestHealth = 0;
static unsigned char s_reconnectSpentMask;
static short s_committedHealth[ZM_NET_MAX_PLAYERS];
static bool s_committedFree[ZM_NET_MAX_PLAYERS];

unsigned char zm_spec_reconnect_spent(void) { return s_reconnectSpentMask; }
void zm_spec_reconnect_restore_spent(unsigned char mask) { s_reconnectSpentMask = mask; }
void zm_spec_reconnect_reconcile(int player, ZmReconnectPlayer* p)
{
    if (p->health < 0 && s_revives[player] > p->revives[player] && s_committedHealth[player] > 0) {
        p->health = s_committedHealth[player]; p->healthFlags = 0; p->stats[3] = -1;
    }
    for (int target = 1; target < 4; target++) {
        if (!s_claimDone[target] || s_committedFree[target] || s_claimOwner[target] != player || s_revives[target] <= p->revives[target] ||
            (p->reviveSpentMask & (1 << target))) continue;
        for (int i = 0; i < p->card.totalHeldItems; i++) {
            int id = p->inventory[i * 2];
            if (id != ITEM_FIRST_AID_SPRAY && !(zm_net_char(player) == ZM_CHAR_REBECCA && id == ITEM_GREEN_HERB)) continue;
            p->inventoryMask &= ~(1u << p->indices[i]);
            if (p->card.equippedItemId == i + 1) p->card.equippedItemId = 0;
            else if (p->card.equippedItemId > i + 1) p->card.equippedItemId--;
            int n = --p->card.totalHeldItems;
            memmove(p->inventory + i * 2, p->inventory + (i + 1) * 2, (n - i) * 2);
            memmove(p->indices + i, p->indices + i + 1, n - i);
            p->inventory[n * 2] = p->inventory[n * 2 + 1] = 0;
            break;
        }
        p->reviveSpentMask |= (unsigned char)(1 << target);
    }
}

static bool          s_dead = false;        // this survivor has died this game
static unsigned int  s_deadMs = 0;
static bool          s_started = false;     // watching (the STATE is frozen)
static bool          s_away = false;        // no longer in any room for the others
static bool          s_jumping = false;     // a follow's room load is under way
static bool          s_leaveOnExit = false; // ...and that load leaves the corpse's room
static unsigned char s_jumpStage = 0, s_jumpRoom = 0;
static int           s_watch = -1;          // the player index watched, -1 none
static int           s_seenKey = -1;        // the watched survivor's room, when it changed
static unsigned int  s_seenMs = 0;
static VECTOR        s_camTarget;
static bool          s_camValid = false;
static unsigned int  s_rawWas = 0;
static unsigned char s_record[0x18];

bool zm_spec_jump_record(const unsigned char* record)
{
    return record == s_record;
}

void zm_spec_new_game(void)
{
    s_reconnectSpentMask = 0;
    memset(s_committedHealth, 0, sizeof(s_committedHealth));
    memset(s_committedFree, 0, sizeof(s_committedFree));
    memset(s_deathSeen, 0, sizeof(s_deathSeen));
    memset(s_revives, 0, sizeof(s_revives));
    s_reviveTarget = -1;
    s_reviveHealth = 0;
    s_returning = false;
    for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) s_claimOwner[i] = -1;
    memset(s_claimDone, 0, sizeof(s_claimDone));
    s_requestTarget = -1;
    s_dead = false;
    s_deadMs = 0;
    s_started = false;
    s_away = false;
    s_jumping = false;
    s_leaveOnExit = false;
    s_watch = -1;
    s_seenKey = -1;
    s_camValid = false;
    s_rawWas = 0;
    zm_net_freeze_state(false);
}

bool zm_spec_away(void) { return s_away; }
// Where this copy's dead survivor lies once it watches (its STATE says so).
const ZmNetPeerState* zm_spec_corpse(void) { return s_started ? &s_corpse : NULL; }

bool zm_spec_player_frozen(void)
{
    return s_started && zm_game_role() == ZM_NET_SURVIVOR && g_playerEntity.health < 0;
}

static bool zm_spec_death_finished(void)
{
    // State 3 includes the fall and blood-pool growth; only state 4 is done.
    return s_started || (s_dead && g_playerEntity.animationId == 4 &&
                         zm_game_time_ms() - s_deadMs >= ZM_SPEC_DELAY_MS);
}

static const ZmNetPeerState* zm_spec_watchable(int i)
{
    if (i < 0 || i >= ZM_NET_MAX_PLAYERS || i == zm_net_self() || zm_net_char(i) < 0) return NULL;
    const ZmNetPeerState* p = zm_net_player(i);
    if (p == NULL || p->dead || p->spectating) return NULL;
    return p;
}

// The next living survivor after `from` in direction `dir` (the first one
// for -1), -1 without one.
static int zm_spec_next(int from, int dir)
{
    int n = ZM_NET_MAX_PLAYERS;             // every seat; zm_spec_watchable skips the director's
    int at = (from >= 0) ? from : (dir > 0 ? n - 1 : 0);
    for (int k = 1; k <= n; k++) {
        int i = ((at + dir * k) % n + n) % n;
        if (zm_spec_watchable(i) != NULL) return i;
    }
    return -1;
}

// Someone other than this copy is in the loaded room: it runs the room when
// this one stops counting.
static bool zm_spec_others_here(void)
{
    for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
        if (i == zm_net_self()) continue;
        if (i != ZM_NET_DIRECTOR && zm_net_char(i) < 0) continue;
        const ZmNetPeerState* p = zm_net_player(i);
        if (p != NULL && !p->spectating && p->stage == g_stageId && p->room == g_roomId) return true;
    }
    return false;
}

// The final death state clears ALL message flags every frame. Observing
// uses neither its input permission nor its room-script camera lock.
// Only an actual menu/load/transition must finish before another jump.
static bool zm_spec_can_jump(void)
{
    return g_openMenuFlag == 0 && (g_main_state_flags & (MSF_MENU_ACTIVE | MSF_ROOM_TRANSITION)) == 0 &&
           g_roomTransitionBusy == 0;
}

bool zm_spec_camera_update(void)
{
    if (!s_started || s_returning || zm_game_role() != ZM_NET_SURVIVOR || zombie_mode_match_over())
        return false;
    // A corpse's position and local camera zones cannot choose the watched
    // player's view. Keep the last view until both copies have the same room.
    const ZmNetPeerState* p = zm_spec_watchable(s_watch);
    if (s_jumping || g_roomTransitionBusy || p == NULL || g_RdtPointer == NULL ||
        p->stage != g_stageId || p->room != g_roomId ||
        p->viewStage != g_stageId || p->viewRoom != g_roomId ||
        p->camera >= g_RdtPointer->cameras_count) return true;
    if (g_roomCameraId != p->camera) {
        g_roomCameraId = p->camera;
        g_main_state_flags &= ~MSF_CAMERA_REDRAW;
        display_room_camera_bg();
        dbg_printf("[spec] survivor %d camera %d stage %d room %02X\n",
                   s_watch, (int)p->camera, (int)g_stageId, (int)g_roomId);
    }
    return true;
}

// Into (stage, room) at the watched survivor's spot: a door record of our
// own, as the director's map jump (zm_jump_to) and door_begin_transition hand
// theirs to room_transition_load.
static void zm_spec_jump(const ZmNetPeerState* p)
{
    unsigned char stage = p->stage, room = p->room;
    if (room >= 0x20) return;
    unsigned char* rec = s_record;
    memset(rec, 0, sizeof(s_record));
    // +0x0D: the room alone within this stage, else (stage + 1) << 5; it adds
    // the return-mansion +5 itself.
    unsigned char base = (unsigned char)(stage >= 5 ? stage - 5 : stage);
    rec[0x0D] = (stage == g_stageId) ? room : (unsigned char)(((base + 1) << 5) | room);
    rec[0x0A] = 0;          // door00.dor
    rec[0x0B] = 0x40;       // camera 0 (corrected on arrival), full load, no door sound
    rec[0x16] = 0xFF;       // no key
    *(short*)(rec + 0x0E) = (short)p->x;
    *(short*)(rec + 0x10) = (short)p->y;
    *(short*)(rec + 0x12) = (short)p->z;
    *(short*)(rec + 0x14) = p->angle;

    s_jumping = true;
    s_jumpStage = stage;
    s_jumpRoom = room;
    dbg_printf("[spec] following survivor %d to stage %d room %02X\n", s_watch, (int)stage, (int)room);

    g_pendingDoorRecord = (int)rec;
    g_main_state_flags |= MSF_GAMEPLAY_ACTIVE;
    g_message_flags = 0;
    g_rect.textureId = 0;
    g_rect.x = -160;
    g_rect.y = -120;
    g_rect.w = 320;
    g_rect.h = 240;
    g_rect.r = 0;
    g_rect.g = 0;
    g_rect.b = 0;
    g_openMenuFlag = 1;
    draw_rect(&g_rect, 0, 0);
    Task_sleep(1);
    StMask(0, 0);
}

// zombie_mode_room_exit, after the room's capture: a follow out of the
// corpse's room takes this player out of every room from here on.
void zm_spec_room_exit(void)
{
    if (s_jumping && s_leaveOnExit) {
        s_away = true;
        s_leaveOnExit = false;
        dbg_printf("[spec] left the corpse's room\n");
    }
}

// The watching begins: the STATE stays the corpse's from now on.
static void zm_spec_start(void)
{
    s_corpse.stage = g_stageId;
    s_corpse.room = g_roomId;
    s_corpse.x = g_playerEntity.scaMatrixData.localMatrix.t[0];
    s_corpse.y = g_playerEntity.scaMatrixData.localMatrix.t[1];
    s_corpse.z = g_playerEntity.scaMatrixData.localMatrix.t[2];
    s_corpse.angle = g_playerEntity.directionAngle;
    s_started = true;
    zm_net_freeze_state(true);
    if (zm_map_is_open()) zm_map_toggle();
    dbg_printf("[spec] dead: watching survivor %d\n", s_watch);
}

void zm_spec_frame(void)
{
    s_camValid = false;
    if (zm_game_role() == ZM_NET_OFF || zombie_mode_match_over() || zombie_mode_pickup_waiting()) return;
    unsigned int now = zm_game_time_ms();
    for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
        if (zm_net_char(i) < 0) continue;
        const ZmNetPeerState* peer = zm_net_player(i);
        bool dead = i == zm_net_self() ? g_playerEntity.health < 0 : peer != NULL && peer->dead;
        if (dead && !s_deathSeen[i]) {
            s_deathSeen[i] = true;
        }
    }
    if (zm_game_role() != ZM_NET_SURVIVOR) return;
    if (s_reviveHealth > 0) {
        // Let an existing spectator transition finish before replacing its record.
        if (!zm_spec_can_jump()) return;
        // Reload even the same room: SetupCharacterData refreshes the weapon
        // and model after the inventory was dropped on death.
        if (!s_returning) {
            s_leaveOnExit = false;
            s_returning = true;
            zm_spec_jump(&s_corpse);
            return;
        }
        if (g_stageId != s_corpse.stage || g_roomId != s_corpse.room) return;
        // Undo player_state_01_control (0x00495180), death fall (0x00459be0),
        // and game_loop's death machine (0x00480ff4). Port-added recovery.
        g_playerEntity.health = (short)s_reviveHealth;
        // State 0 (0x00494eb0) restores the shadow, hit box, skeleton and
        // animation timing, then hands control to state 1 on its next update.
        g_playerEntity.animationId = 0;
        g_playerEntity.animFrameId = 0;
        g_playerEntity.action_state = 0;
        g_playerEntity.action_behavior = 0;
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.isBeingAttackedFlag = 0;
        g_playerEntity.attackDirection = 0;
        g_playerEntity.healthStatusFlags = 0;
        g_playerEntity.move_speed_current = 0;
        g_playerEntity.scaMatrixData.localMatrix.t[0] = s_corpse.x;
        g_playerEntity.scaMatrixData.localMatrix.t[1] = s_corpse.y;
        g_playerEntity.scaMatrixData.localMatrix.t[2] = s_corpse.z;
        g_playerEntity.directionAngle = s_corpse.angle;
        BillboardSetSize(&g_playerEntity.pushVelocity, 0, 0);
        g_main_state_flags &= ~MSF_PLAYER_DEAD;
        DAT_00be9614 = 0;
        DAT_004d2288 = 0;
        g_fading_state = (short)-1;
        g_fading_counter = 0;
        g_deathAnimationFlag = 0;
        g_message_flags |= 0x0144; // control, entity updates and shadow, cleared by death
        s_dead = s_started = s_away = s_jumping = s_leaveOnExit = false;
        s_camValid = false;
        s_watch = s_seenKey = -1;
        s_returning = false;
        s_reviveHealth = 0;
        zm_net_freeze_state(false);
        zm_world_story_rebase();
        zm_stats_revived();
        int t[3] = { s_corpse.x, s_corpse.y, s_corpse.z };
        zm_fix_camera_at(t, ZM_NET_MAX_PLAYERS * 10 + zm_net_self());
        dbg_printf("[revive] back at corpse: stage %d room %02X health %d\n",
                   (int)g_stageId, (int)g_roomId, (int)g_playerEntity.health);
        return;
    }
    if (!s_dead) {
        if (g_playerEntity.health >= 0) return;
        s_dead = true;
        s_deadMs = now;
    }
    if (!zm_spec_death_finished()) return;

    // Only finish after the loader and its menu-state restoration have run.
    if (s_jumping && g_stageId == s_jumpStage && g_roomId == s_jumpRoom && zm_spec_can_jump()) {
        s_jumping = false;
    }

    if (zm_spec_watchable(s_watch) == NULL) {
        int next = zm_spec_next(s_watch, 1);
        if (next != s_watch) {
            s_watch = next;
            s_seenKey = -1;
        }
    }
    const ZmNetPeerState* p = zm_spec_watchable(s_watch);
    if (p == NULL) return;                  // nobody to watch (yet): stay with the body

    bool sameRoom = p->stage == g_stageId && p->room == g_roomId;
    if (!s_started) {
        zm_spec_start();
        if (sameRoom) {
            // Watching from the corpse's own room: out of it at once, the
            // watched survivor runs it. (Alone, the room is left on the
            // follow's door, after this copy captured it.)
            s_away = true;
        }
    }
    if (s_jumping) return;

    if (!sameRoom) {
        int key = (int)p->stage | ((int)p->room << 8);
        if (key != s_seenKey) {
            s_seenKey = key;
            s_seenMs = now;
        }
        if (now - s_seenMs >= ZM_SPEC_FOLLOW_MS && zm_spec_can_jump()) {
            // Leaving a room this copy may still run: counted gone once the
            // room has been captured, unless somebody else is there to run it.
            if (!s_away) {
                if (zm_spec_others_here()) s_away = true;
                else s_leaveOnExit = true;
            }
            zm_spec_jump(p);
        }
        return;
    }

    s_seenKey = -1;
    s_camTarget.x = p->x;
    s_camTarget.y = p->y;
    s_camTarget.z = p->z;
    s_camValid = true;
    zm_spec_camera_update();
}

const VECTOR* zm_spec_camera_target(void)
{
    return (s_started && s_camValid) ? &s_camTarget : NULL;
}

// zombie_mode_survivor_input: while this survivor is dead the pad is the
// spectator's - left / right the next survivor - and the hidden player entity
// gets none of it.
bool zm_spec_input(void)
{
    if (!s_dead || zombie_mode_match_over()) return false;
    unsigned int raw = g_button_pressed_id;
    unsigned int edge = raw & ~s_rawWas;
    s_rawWas = raw;
    if (zm_map_is_open()) zm_map_toggle();
    if (s_started) {
        int dir = (edge & 0x8000) ? -1 : (edge & 0x2000) ? 1 : 0;
        if (dir != 0) {
            int next = zm_spec_next(s_watch, dir);
            if (next >= 0 && next != s_watch) {
                s_watch = next;
                s_seenKey = -1;
                dbg_printf("[spec] now watching survivor %d\n", s_watch);
            }
        }
    }
    g_PlayerPadHeld = 0;
    g_PlayerPadPressed = 0;
    g_PlayerDpadHeld = 0;
    g_PlayerDpadPressed = 0;
    return true;
}

static bool zm_revive_free(int target);   // with the revive below

bool zm_spec_draw(void)
{
    if (!s_dead) return false;
    // Suppress the fallback death banner while the full animation plays.
    if (!zm_spec_death_finished()) return true;
    char revive[48];
    if (zm_shotgun_crushed(zm_net_self()))
        snprintf(revive, sizeof(revive), "CRUSHED - REVIVE UNAVAILABLE");
    else if (s_revives[zm_net_self()] < ZM_REVIVE_LIMIT || zm_revive_free(zm_net_self()))
        snprintf(revive, sizeof(revive), "REVIVE AVAILABLE");
    else snprintf(revive, sizeof(revive), "REVIVE UNAVAILABLE");
    zm_draw_centered(revive, 188, 0);
    const ZmNetPeerState* p = zm_spec_watchable(s_watch);
    if (p == NULL) {
        zm_draw_centered("YOU ARE DEAD", 96, 2);
        zm_draw_centered("NO SURVIVOR LEFT TO WATCH", 116, 0);
        return true;
    }
    char line[48];
    snprintf(line, sizeof(line), "YOU ARE DEAD - WATCHING %s", zm_char_name(zm_net_char(s_watch)));
    zm_draw_centered(line, 8, 2);
    if (zm_spec_next(s_watch, 1) != s_watch) {
        zm_draw_centered("LEFT OR RIGHT: NEXT SURVIVOR", 208, 0);
    }
    return true;
}

// A corpse left by the boss fight during its lockdown (ZombieYawn.cpp): revived there
// without a healing item, however often it has been.
static bool zm_revive_free(int target)
{
    const ZmNetPeerState* p = zm_seat_state(target);
    return p != NULL && zm_yawn_free_revive(target, p->stage, p->room);
}

// A boss's lockdown over: every survivor's revives are counted afresh - those
// lying elsewhere too, to be reached and revived (with a healing item) again.
void zm_revive_reset_counts(void)
{
    memset(s_revives, 0, sizeof(s_revives));
    for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
        if (s_claimDone[i]) s_claimOwner[i] = -1;
        s_claimDone[i] = false;
    }
    dbg_printf("[revive] revive counts reset\n");
}

void zm_revive_cancel(void)
{
    if (s_requestTarget >= 0) {
        zm_net_send_event_to(ZM_NET_DIRECTOR, ZM_EV_REVIVE, (short)s_requestTarget, 0, 0, 0, 0, 0, 0, 0);
        s_requestTarget = -1;
    }
    s_reviveTarget = -1;
}

static bool zm_revive_eligible(int i)
{
    const ZmNetPeerState* p = zm_net_player(i);
    return i >= 0 && i < ZM_NET_MAX_PLAYERS && zm_net_char(i) >= 0 && p != NULL && p->dead && p->spectating &&
           !zm_shotgun_crushed(i) && s_deathSeen[i] && (s_revives[i] < ZM_REVIVE_LIMIT || zm_revive_free(i));
}

static int zm_revive_item(void)
{
    bool medic = zm_net_char(zm_net_self()) == ZM_CHAR_REBECCA;
    unsigned char* slots = (unsigned char*)g_ItemSlotsPointer;
    for (int i = 0; i < g_TotalHeldItems; i++) {
        if (slots[i * 2] == ITEM_FIRST_AID_SPRAY || (medic && slots[i * 2] == ITEM_GREEN_HERB)) return i;
    }
    return -1;
}

static int zm_revive_near(void)
{
    if (zm_game_role() != ZM_NET_SURVIVOR || g_playerEntity.health < 0 || zombie_mode_match_over() ||
        zm_map_is_open() || g_openMenuFlag != 0 || (g_main_state_flags & (MSF_MENU_ACTIVE | MSF_CAMERA_LOCK)) != 0 ||
        (g_message_flags & 0x0101) != 0x0101 || g_playerEntity.isBeingAttackedFlag != 0)
        return -1;
    bool item = zm_revive_item() >= 0;
    int best = -1, dist = ZM_REVIVE_RANGE * ZM_REVIVE_RANGE + 1;
    for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
        if (!zm_revive_eligible(i) || (!item && !zm_revive_free(i))) continue;
        const ZmNetPeerState* p = zm_net_player(i);
        if (p->stage != g_stageId || p->room != g_roomId) continue;
        int dx = p->x - g_playerEntity.scaMatrixData.localMatrix.t[0];
        int dz = p->z - g_playerEntity.scaMatrixData.localMatrix.t[2];
        int dy = p->y - g_playerEntity.scaMatrixData.localMatrix.t[1];
        // Bound each axis before squaring: strictly 32-bit arithmetic.
        if (dx < -ZM_REVIVE_RANGE || dx > ZM_REVIVE_RANGE || dz < -ZM_REVIVE_RANGE || dz > ZM_REVIVE_RANGE ||
            dy < -ZM_REVIVE_RANGE || dy > ZM_REVIVE_RANGE) continue;
        int d = dx * dx + dz * dz + dy * dy;
        if (d < dist) { best = i; dist = d; }
    }
    return best;
}

bool zm_revive_input(void)
{
    int target = zm_revive_near();
    if (target < 0 || (g_PlayerDpadHeld & ZM_PAD_ACTION) == 0) {
        zm_revive_cancel();
        return false;
    }
    unsigned int now = zm_game_time_ms();
    if (target != s_reviveTarget && s_requestTarget >= 0) zm_revive_cancel();
    if (target != s_reviveTarget) { s_reviveTarget = target; s_reviveAt = now; }
    bool medic = zm_net_char(zm_net_self()) == ZM_CHAR_REBECCA;
    unsigned int duration = medic ? ZM_REVIVE_MEDIC_MS : ZM_REVIVE_HOLD_MS;
    if (now - s_reviveAt >= duration && s_requestTarget < 0) {
        ZmPerkInfo info;
        zm_perk_describe(zm_net_char(target), &info);
        s_requestTarget = target;
        s_requestHealth = (short)(info.health * (medic ? ZM_REVIVE_MEDIC_PCT : ZM_REVIVE_HEALTH_PCT) / 100);
        zm_net_send_event_to(ZM_NET_DIRECTOR, ZM_EV_REVIVE, (short)target, (short)-s_requestHealth,
                             0, 0, 0, 0, 0, 0);
    }
    // Keep the hold from firing a weapon, taking a door or opening a pickup.
    g_PlayerPadHeld = g_PlayerPadPressed = g_PlayerDpadHeld = g_PlayerDpadPressed = 0;
    return true;
}

void zm_revive_take(const short* a, int src)
{
    int target = a[0];
    if (zm_shotgun_crushed(target)) return;
    if (src == ZM_NET_DIRECTOR && a[2] == 1 && zm_game_role() == ZM_NET_SURVIVOR) {
        // The host granted our completed hold. Consume only the winner's item
        // and commit; a refused or cancelled hold spends nothing.
        if (target != s_requestTarget) return;
        int slot = zm_revive_item();
        bool free = zm_revive_free(target);
        if (a[1] != s_requestHealth || (slot < 0 && !free) || zombie_mode_match_over() ||
            g_playerEntity.health < 0 || g_playerEntity.isBeingAttackedFlag != 0) {
            zm_revive_cancel();
            return;
        }
        if (!free) {
            unsigned char* item = (unsigned char*)g_ItemSlotsPointer + slot * 2;
            if (g_EquippedItemId == slot + 1) g_EquippedItemId = 0;
            item[0] = item[1] = 0;
            s_reconnectSpentMask |= (unsigned char)(1 << target);
            rearrange_item_slots();
        }
        s_requestTarget = -1;
        s_reviveTarget = -1;
        zm_net_send_event(ZM_EV_REVIVE, a[0], a[1], 0, 0);
        // The host reviving (an AI director's game): its commit does not come
        // back to it, so it takes it here as it would a survivor's.
        if (zm_net_role() == ZM_NET_ZOMBIE) {
            short commit[8] = { a[0], a[1], 0, 0, 0, 0, 0, 0 };
            if (zm_revive_host_claim(a[0], a[1], zm_net_self())) zm_revive_take(commit, zm_net_self());
        }
        return;
    }
    if (a[2] == 1) return;          // another survivor's grant
    if (zombie_mode_match_over() || src < 0 || src >= ZM_NET_MAX_PLAYERS || zm_net_char(src) < 0 ||
        target < 0 || target >= ZM_NET_MAX_PLAYERS || zm_net_char(target) < 0 || src == target ||
        (s_revives[target] >= ZM_REVIVE_LIMIT && !zm_revive_free(target)) || a[1] <= 0) return;
    ZmPerkInfo info;
    zm_perk_describe(zm_net_char(target), &info);
    int expected = info.health * (zm_net_char(src) == ZM_CHAR_REBECCA ? ZM_REVIVE_MEDIC_PCT : ZM_REVIVE_HEALTH_PCT) / 100;
    if (a[1] != expected) return;
    s_committedFree[target] = zm_revive_free(target);
    s_revives[target]++;
    zm_yawn_revived(target);
    dbg_printf("[revive] survivor %d revived %d: health %d\n", src, target, (int)a[1]);
    if (target == zm_net_self() && zm_game_role() == ZM_NET_SURVIVOR && s_dead) {
        s_reviveHealth = a[1];
        // No spectator started yet: capture the current body rather than an old match's spot.
        if (!s_started) zm_spec_start();
    }
}

// Port-added arbitration, called immediately while the host receives events,
// so two requests in the same packet poll cannot both win the reservation.
bool zm_revive_host_claim(int target, int health, int src)
{
    if (target < 0 || target >= ZM_NET_MAX_PLAYERS || src < 0 || src >= ZM_NET_MAX_PLAYERS || src == target ||
        zm_net_char(target) < 0 || zm_net_char(src) < 0)
        return false;
    if (health == 0) {
        if (s_claimOwner[target] == src && !s_claimDone[target]) s_claimOwner[target] = -1;
        return false;
    }
    // A corpse left by a boss fight gets a fresh claim even if this survivor
    // already used its ordinary revive before dying in the fight.
    bool free = zm_revive_free(target);
    if (free && health < 0 && s_claimDone[target]) {
        s_claimDone[target] = false;
        s_claimOwner[target] = -1;
    }
    if (zombie_mode_match_over() || zm_shotgun_crushed(target) || s_claimDone[target] ||
        (s_revives[target] >= ZM_REVIVE_LIMIT && !free)) return false;
    if (health > 0) {
        // The reliable stream delivers the grant before this commit.
        if (s_claimOwner[target] != src) return false;
        ZmPerkInfo info;
        zm_perk_describe(zm_net_char(target), &info);
        int expected = info.health * (zm_net_char(src) == ZM_CHAR_REBECCA ? ZM_REVIVE_MEDIC_PCT : ZM_REVIVE_HEALTH_PCT) / 100;
        if (health != expected) return false;
        s_claimDone[target] = true;
        s_committedHealth[target] = (short)health;
        return true;
    }
    // Do not expire a live grant: its winner may already have spent the item
    // while its reliable commit is being retransmitted. Explicit cancellation
    // releases it; a disconnected requester cannot later send a commit.
    if (s_claimOwner[target] >= 0 && zm_seat_state(s_claimOwner[target]) != NULL) return false;
    const ZmNetPeerState* body = zm_seat_state(target);
    const ZmNetPeerState* reviver = zm_seat_state(src);
    if (body == NULL || reviver == NULL || !body->dead || !body->spectating || reviver->dead || reviver->spectating || reviver->attacked ||
        body->stage != reviver->stage || body->room != reviver->room || !s_deathSeen[target]) return false;
    ZmPerkInfo info;
    zm_perk_describe(zm_net_char(target), &info);
    int expected = info.health * (zm_net_char(src) == ZM_CHAR_REBECCA ? ZM_REVIVE_MEDIC_PCT : ZM_REVIVE_HEALTH_PCT) / 100;
    if (-health != expected) return false;
    int dx = body->x - reviver->x, dy = body->y - reviver->y, dz = body->z - reviver->z;
    if (dx < -ZM_REVIVE_RANGE || dx > ZM_REVIVE_RANGE || dy < -ZM_REVIVE_RANGE || dy > ZM_REVIVE_RANGE ||
        dz < -ZM_REVIVE_RANGE || dz > ZM_REVIVE_RANGE || dx * dx + dy * dy + dz * dz > ZM_REVIVE_RANGE * ZM_REVIVE_RANGE)
        return false;
    s_claimOwner[target] = src;
    return true;
}

void zm_revive_draw(void)
{
    int target = zm_revive_near();
    if (target < 0) return;
    char line[64];
    if (target == s_reviveTarget) {
        unsigned int duration = zm_net_char(zm_net_self()) == ZM_CHAR_REBECCA ? ZM_REVIVE_MEDIC_MS : ZM_REVIVE_HOLD_MS;
        unsigned int elapsed = zm_game_time_ms() - s_reviveAt;
        snprintf(line, sizeof(line), "REVIVING %s %u S", zm_char_name(zm_net_char(target)),
                 elapsed < duration ? (duration - elapsed + 999) / 1000 : 0);
    } else snprintf(line, sizeof(line), "HOLD ACTION: REVIVE %s", zm_char_name(zm_net_char(target)));
    zm_draw_centered(line, 172, 0);
}
