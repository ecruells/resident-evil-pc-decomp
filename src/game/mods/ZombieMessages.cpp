#include "ZombieModeInternal.h"
#include "../PrintText.h"
#include "../../DebugPrint.h"

extern void Flg_on(int baseAddr, unsigned int bitIndex);              // 0x00473ef0 CmdFunctions.cpp

// ============================================================================
// ZombieMessages.cpp - text that does not stop the game (port-added).
//
// The original freezes the room for most messages: set_message_display clears
// the g_message_flags bits its caller names (0xff for a locked door, a desk,
// "no ink ribbon", an examined object...), which stops the player's state
// machine (bit 0x01), the monsters (0x04), the effects (0x08), and waits for
// Action/Cancel to close the text. In a multiplayer match that is a free
// pause on the copy that runs the room.
//
// In the mode such a message is "passive": the flags are left as they are, so
// the survivor keeps full control and the room keeps running; the text still
// types out on the room's message line at the bottom of the screen, turns its
// pages and goes away by itself after a reading time for its length. The
// buttons do not touch it (Action is still the check button).
//
// A message that asks Yes/No (tag 8: take an item, use a key, save) needs the
// answer, so the survivor is held - but not the room: instead of the caller's
// mask only 0x0100 (the control: game_loop cuts the pad to the menu bits, as
// when a script takes the control) and 0x0040 (no other check, door or menu
// starts) are cleared. The player's state machine keeps running with no
// movement input, so the survivor settles into the idle pose, and its STATE
// keeps going out; monsters, effects and events carry on. A grab or death
// while the question is up answers No. Closing it raises back only those two
// bits, so nothing the room changed meanwhile is rolled back.
//
// Messages raised inside the menus, during a door transition, or with no
// pause asked for keep the original behaviour - except an examine event's.
//
// Examine events (zm_evt_is_examine: an action press on a painting, a statue,
// a shelf...) take the control themselves (message flag 0x100), cut to a
// close-up (cut_lock_set), show a message with no pause that waits for a
// press, then cut back to a fixed camera and give the control back. In the
// mode their message is passive too, and the control comes back as it starts.
// The close-up stays until the survivor moves or turns: then the camera goes
// back to the one before it (and on to the one for where the survivor stands,
// zm_fix_camera_at), and the event's own camera commands are dropped for the
// rest of its run - its fixed cut back would land on a stale camera.
//
// A passive message gives way to a different one at once, and to a door or
// the inventory opening (game_loop waits on g_menu_choice_id 0x80 before
// either). The same message is not restarted while it is up, nor for
// ZM_MSG_REPEAT_MS after: walk-in zones probe every frame. The key turning
// (door_try_enter's global 0x03, naming g_selectedItemId) is exempt: one id
// for every key, and each unlock is new - two doors opened in quick
// succession lost the second's message.
// ============================================================================

#define ZM_MSG_BASE_MS     700   // reading time per page: this
#define ZM_MSG_CHAR_MS      25   //   plus this per character,
#define ZM_MSG_MIN_MS     1200   //   within these bounds
#define ZM_MSG_MAX_MS     4000
#define ZM_MSG_REPEAT_MS  1000
#define ZM_MSG_KEY_TURNED 0x43      // global message 0x03 (msg id & 0x7F): "unlocked using the %item"
#define ZM_MSG_PROMPT_MASK 0x0140   // what a Yes/No prompt clears


static bool           s_passive = false;
static bool           s_prompt = false;    // a Yes/No prompt holding only the control
static bool           s_passiveEvent = false; // a passive message an event (holding the control) waits on
static unsigned short s_id = 0;
static unsigned int   s_waitStart = 0;     // when the shown page finished typing; 0 = not yet
static signed char    s_evtSlot = -1;      // event slot whose SCD command is running
static bool           s_examine[8];        // per event slot: an examine event (or a reveal)
static bool           s_reveal[8];         // per event slot: a reveal played as a close-up
static unsigned char  s_script[8];         // per event slot: the script it runs
static bool           s_cut[8];            // per event slot: cut short by a hit, grab or death

// Reveals: scenes that take the control, cut the camera and pose or place
// the player to show what a puzzle changed. In the mode they play like an
// examine's close-up - the survivor keeps the control and is neither posed
// nor moved, and moving, turning or being hit cuts back to it; the scene
// itself runs on (the case still opens). The events a reveal starts are
// reveals too (one group: a cut back drops all their cuts). Found by
// scanning the return mansion's events that survive the story filter for
// camera cuts with the control taken or the player placed. The pickup
// close-ups a monster could walk in on (the bathtub, the attic) are here too:
// moving cuts back, and the take prompt still holds the survivor while it
// asks. Room-changing scenes (the rope in ROOM70C0) are left out.
struct ZmReveal {
    unsigned char stage, room, script;
};
static const ZmReveal kReveals[] = {
    { STAGE_MANSION_RETURN_1F, ROOM_DINING_ROOM, 1 },          // ROOM6050: the object at the fireplace moves
    { STAGE_MANSION_RETURN_1F, ROOM_MANSION_BAR, 10 },         // ROOM60F0: the emblems' cabinet
    { STAGE_MANSION_RETURN_1F, ROOM_MANSION_BAR, 11 },
    { STAGE_MANSION_RETURN_1F, ROOM_MANSION_BATHROOM, 1 },     // ROOM6130: the bathtub drains
    { STAGE_MANSION_RETURN_1F, ROOM_MANSION_BATHROOM, 4 },     //   ...the item in it (pickup close-up)
    { STAGE_MANSION_RETURN_1F, ROOM_LARGE_GALLERY, 20 },       // ROOM6170: the last portrait, solved
    { STAGE_MANSION_RETURN_1F, ROOM_LARGE_GALLERY, 21 },       //   ...the panel slides away
    { STAGE_MANSION_RETURN_1F, ROOM_MANSION_1F_STUDY, 0 },     // ROOM6190: the switch
    { STAGE_MANSION_RETURN_1F, ROOM_ROOFED_PASSAGE, 3 },       // ROOM11A0: a crest set in the door
    { STAGE_MANSION_RETURN_1F, ROOM_ROOFED_PASSAGE, 4 },
    { STAGE_MANSION_RETURN_1F, ROOM_ROOFED_PASSAGE, 5 },
    { STAGE_MANSION_RETURN_1F, ROOM_ROOFED_PASSAGE, 6 },
    { STAGE_MANSION_RETURN_2F, ROOM_ARMOR_ROOM, 1 },           // ROOM7050: the display case slides open
    { STAGE_MANSION_RETURN_2F, ROOM_STUDY_2F, 1 },             // ROOM70A0: the shelf's switch
    { STAGE_MANSION_RETURN_2F, ROOM_FRONT_LESSON_ROOM, 0 },    // ROOM70B0: the stove
    { STAGE_MANSION_RETURN_2F, ROOM_TROPHY_ROOM, 0 },          // ROOM7150: the light switch
    { STAGE_MANSION_RETURN_2F, ROOM_TROPHY_ROOM, 4 },          //   ...the deer's eye
    { STAGE_MANSION_RETURN_2F, ROOM_PRIVATE_LIBRARY, 1 },      // ROOM7170: the statue in place
    { STAGE_MANSION_RETURN_2F, ROOM_ATTIC, 1 },                // ROOM7100: the item by the hole (pickup close-up)
};

static bool zm_is_reveal(int script)
{
    for (unsigned int i = 0; i < sizeof(kReveals) / sizeof(kReveals[0]); i++) {
        if (kReveals[i].stage == g_stageId && kReveals[i].room == g_roomId && kReveals[i].script == script) return true;
    }
    return false;
}
static int            s_closeSlot = -1;    // the event that cut to a close-up
static unsigned char  s_closeCam, s_prevCam, s_closeStage, s_closeRoom;
static bool           s_closeReleased = false;
static bool           s_closeUp = false;   // a close-up camera is on screen, not yet cut back from
static unsigned short s_lastId = 0xFFFF;   // the last passive message, and when it ended
static unsigned int   s_lastEnd = 0;

// Does the message ask Yes/No? Walks the encoding UpdateMessageDisplay reads:
// tag 1 n (n = 0 ends the text), tags 3/4/5/6 and the 0xf8-0xfa glyph pages
// take one byte, tag 8 is the prompt.
static bool zm_msg_has_prompt(const unsigned char* p)
{
    for (int n = 0; n < 2048; ++n) {
        unsigned char c = p[n];
        switch (c) {
        case 1:
            if (p[n + 1] == 0) return false;
            ++n;
            break;
        case 8:
            return true;
        case 3: case 4: case 5: case 6:
        case 0xf8: case 0xf9: case 0xfa:
            ++n;
            break;
        default:
            break;
        }
    }
    return false;
}

static unsigned int zm_msg_page_ms(void)
{
    int chars = (int)(g_MessageCurrentPtr - g_MessagePtr);
    if (chars < 0 || chars > 512) chars = 0;
    unsigned int ms = ZM_MSG_BASE_MS + (unsigned int)chars * ZM_MSG_CHAR_MS;
    if (ms < ZM_MSG_MIN_MS) ms = ZM_MSG_MIN_MS;
    if (ms > ZM_MSG_MAX_MS) ms = ZM_MSG_MAX_MS;
    return ms;
}

static void zm_msg_finish(void)
{
    s_passive = false;
    s_waitStart = 0;
    s_lastId = s_id;
    s_lastEnd = zm_game_time_ms();
}

bool zombie_mode_message_replace(unsigned short msgId)
{
    if (!s_passive) return false;
    if ((msgId & 0x7F) == (s_id & 0x7F) && (msgId & 0x7F) != ZM_MSG_KEY_TURNED) return false;
    g_menu_choice_id &= 0x7F;
    zm_msg_finish();
    return true;
}

bool zombie_mode_message_repeat(unsigned short msgId, unsigned short pause)
{
    if (!zombie_mode_armed() || pause == 0 || (msgId & 0x7F) == ZM_MSG_KEY_TURNED) return false;
    return (msgId & 0x7F) == (s_lastId & 0x7F) &&
           zm_game_time_ms() - s_lastEnd < ZM_MSG_REPEAT_MS;
}

static bool zm_in_examine(void)
{
    return zombie_mode_armed() && s_evtSlot >= 0 && s_evtSlot < 8 && s_examine[s_evtSlot];
}

// One close-up: an examine's own event, or any of the reveals running.
static bool zm_same_close(int a, int b)
{
    if (a < 0 || a >= 8 || b < 0 || b >= 8) return false;
    return a == b || (s_reveal[a] && s_reveal[b]);
}

static bool zm_close_running(void)
{
    if (g_ScdEventTable[s_closeSlot].active != 0) return true;
    if (!s_reveal[s_closeSlot]) return false;
    for (int i = 0; i < 8; i++) {
        if (s_reveal[i] && g_ScdEventTable[i].active != 0) return true;
    }
    return false;
}

void zombie_mode_event_init(int slot, int script)
{
    if (slot < 0 || slot >= 8) return;
    // Started (or re-inited) by a reveal's command: part of it.
    const bool inherit = zombie_mode_armed() && s_evtSlot >= 0 && s_evtSlot < 8 && s_reveal[s_evtSlot];
    s_reveal[slot] = inherit || (zombie_mode_armed() && zm_is_reveal(script));
    s_examine[slot] = s_reveal[slot] || (zombie_mode_armed() && zm_evt_is_examine(script));
    s_script[slot] = (unsigned char)script;
    s_cut[slot] = false;
    if (slot == s_closeSlot) s_closeSlot = -1;
    if (s_examine[slot]) dbg_printf("[msg] event %d (slot %d) is an %s event\n", script, slot,
                                    s_reveal[slot] ? "reveal" : "examine");
}

void zombie_mode_event_cmd(int slot)
{
    s_evtSlot = (signed char)slot;
}

bool zombie_mode_cut_skip(void)
{
    return zm_in_examine() && s_closeReleased && zm_same_close(s_closeSlot, s_evtSlot);
}

void zombie_mode_cut_closeup(unsigned char prevCam)
{
    if (!zm_in_examine()) return;
    // A scene cutting from one close-up to the next still returns to the
    // camera before the first.
    if (!zm_same_close(s_closeSlot, s_evtSlot)) {
        // Locked again from its own close-up, never cut back from (a scene
        // started over while it ran): the camera to go back to is still the
        // room's. Taking the close-up's own left the camera stuck on it.
        if (s_closeUp && prevCam == s_closeCam && g_stageId == s_closeStage && g_roomId == s_closeRoom) {
            g_cutId = s_prevCam;
            dbg_printf("[msg] close-up %d locked again: back to camera %d kept\n", (int)prevCam, (int)s_prevCam);
        } else {
            s_prevCam = prevCam;
        }
    }
    s_closeUp = true;
    s_closeSlot = s_evtSlot;
    s_closeCam = g_roomCameraId;
    s_closeStage = g_stageId;
    s_closeRoom = g_roomId;
    s_closeReleased = false;
}

void zombie_mode_cut_restored(void)
{
    s_closeUp = false;
    if (zm_in_examine() && zm_same_close(s_closeSlot, s_evtSlot) && !s_closeReleased) s_closeSlot = -1;
}

// The examine close-up back to the room's camera: the one before it, as the
// event's current_cut_set would, then the one for where the survivor stands.
static void zm_close_release(void)
{
    extern void cut_set(void);
    s_closeUp = false;
    g_main_state_flags &= ~MSF_CAMERA_LOCK;
    if (g_RdtPointer == NULL || g_RdtPointer->cam_switch_zones == NULL) return;
    const CAM_SWITCH_ZONE* zone = (const CAM_SWITCH_ZONE*)g_RdtPointer->cam_switch_zones;
    for (int i = 0; i < 1024; i++, zone++) {
        if ((unsigned short)zone->camFrom >= g_RdtPointer->cameras_count) return;
        if ((unsigned short)zone->camFrom != s_prevCam) continue;
        g_roomCameraId = s_prevCam;
        g_CurrentRdtDataTypePtr = (void*)zone;
        cut_set();
        break;
    }
    zm_fix_camera_at((const int*)g_playerEntity.scaMatrixData.localMatrix.t, -1);
    dbg_printf("[msg] close-up %d left for camera %d\n", (int)s_closeCam, (int)g_roomCameraId);
}

static void zm_letterbox_frame(void);
static void zm_key_discard_frame(void);
static unsigned char s_discardedKey = 0;   // a key used up, its message not shown yet

void zombie_mode_message_frame(void)
{
    if (!zombie_mode_armed()) {
        s_closeSlot = -1;
        s_closeUp = false;
        s_discardedKey = 0;
        return;
    }
    zm_letterbox_frame();
    zm_key_discard_frame();
    // Hit, grabbed or dead: every examine or reveal still running is cut
    // short - it plays on (a puzzle's scene still finishes) but offers no
    // pickup afterwards (zombie_mode_event_room_action_skip), and its
    // close-up gives the camera back below.
    const bool attacked = g_playerEntity.isBeingAttackedFlag != 0 || g_playerEntity.health < 0;
    if (attacked) {
        for (int i = 0; i < 8; i++) {
            if (s_cut[i] || !s_examine[i] || g_ScdEventTable[i].active == 0) continue;
            s_cut[i] = true;
            dbg_printf("[msg] event %d (slot %d) cut short: survivor attacked\n", (int)s_script[i], i);
        }
    }
    if (s_closeSlot < 0) return;
    if (g_stageId != s_closeStage || g_roomId != s_closeRoom || !zm_close_running()) {
        s_closeSlot = -1;
        s_closeUp = false;
        return;
    }
    if (s_closeReleased) return;
    // Someone else has the camera now: not ours to give back.
    if ((g_main_state_flags & MSF_CAMERA_LOCK) == 0 || g_roomCameraId != s_closeCam) {
        s_closeSlot = -1;
        s_closeUp = false;
        return;
    }
    if (!attacked) {
        if ((g_message_flags & 0x0101) != 0x0101) return;
        if ((g_PlayerDpadHeld & (ZM_PAD_FORWARD | ZM_PAD_BACK | ZM_PAD_TURN_A | ZM_PAD_TURN_B)) == 0) return;
    }
    s_closeReleased = true;
    zm_close_release();
}

// An Action press while a scene is playing. The original takes the control
// for these, so a press cannot reach them; in the mode an examine or a
// reveal leaves it with the survivor, and each press started its scene over
// in the same slot - the close-up locked again from itself (a stuck camera)
// and every run asked for the pickup once more. So a press does not start an
// examine or reveal that is still running, and while its close-up is on
// screen no other pickup (handler 4) or scene (handler 9) starts either.
bool zombie_mode_action_busy(const unsigned char* entry)
{
    if (!zombie_mode_armed() || entry == NULL) return false;
    const unsigned char handler = entry[0];
    // The attic's key or crest and its close-up, while Yawn is there.
    if (zombie_mode_yawn_guards(entry, handler)) return true;
    if (handler != 4 && handler != 9) return false;
    if (s_closeSlot >= 0 && s_closeUp && !s_closeReleased) {
        dbg_printf("[msg] action (handler %d) refused: close-up still up\n", (int)handler);
        return true;
    }
    if (handler != 9) return false;
    const int slot = entry[2], script = entry[4];
    for (int i = 0; i < 8; i++) {
        if (g_ScdEventTable[i].active == 0 || !s_examine[i]) continue;
        if (i != slot && s_script[i] != script) continue;
        dbg_printf("[msg] event %d refused: slot %d still runs event %d\n", script, i, (int)s_script[i]);
        return true;
    }
    return false;
}

// cmd_room_action from an event cut short by a hit: no take screen (4) or
// item box (8) opens afterwards. The item stays where it lies.
bool zombie_mode_event_room_action_skip(unsigned char actionIdx)
{
    if (!zombie_mode_armed() || s_evtSlot < 0 || s_evtSlot >= 8 || !s_cut[s_evtSlot]) return false;
    if (actionIdx != 4 && actionIdx != 8) return false;
    dbg_printf("[msg] event %d (slot %d): pickup dropped, cut short\n", (int)s_script[s_evtSlot], (int)s_evtSlot);
    return true;
}

unsigned short zombie_mode_message_pause(unsigned short msgId, unsigned short pause,
                                         const unsigned char* text)
{
    s_passive = false;
    s_prompt = false;
    s_passiveEvent = false;
    s_waitStart = 0;
    const bool examine = zm_in_examine();
    if (!zombie_mode_armed() || (pause == 0 && !examine) || text == NULL) return pause;
    if ((g_main_state_flags & (MSF_MENU_ACTIVE | MSF_DOOR_TRANSITION)) != 0) return pause;
    // g_openMenuFlag 0: game_loop's frame loop is running, no door or menu
    // pending. (MSF_GAMEPLAY_ACTIVE is no test: game_loop clears it once the
    // room is up and raises it only to request a room change.)
    if (g_openMenuFlag != 0) return pause;
    if (zm_msg_has_prompt(text)) {
        // A grab or a hit answers No either way (zombie_mode_message_prompt_cancel).
        s_prompt = true;
        if (pause == 0) return 0;    // an examine event's question: its script holds the control
        dbg_printf("[msg] 0x%02X Yes/No: only the control held\n", msgId);
        return ZM_MSG_PROMPT_MASK;
    }
    s_passive = true;
    s_id = msgId;
    // Shown from an event that holds the control (not an examine, which gives
    // it back): the survivor can do nothing else, so Action may turn it on.
    s_passiveEvent = s_evtSlot >= 0 && s_evtSlot < 8 && !examine;
    // An examine event took the control for its message: give it back now.
    if (examine && (g_message_flags & 0x0100) == 0) {
        g_message_flags |= 0x0100;
    }
    dbg_printf("[msg] 0x%02X shown without the pause\n", msgId);
    return 0;
}

bool zombie_mode_message_prompt_cancel(void)
{
    if (!s_prompt) return false;
    if (g_playerEntity.isBeingAttackedFlag == 0 && g_playerEntity.health >= 0) return false;
    g_menu_choice_id |= 1;   // the cursor on No
    dbg_printf("[msg] Yes/No prompt answered No: survivor grabbed or dead\n");
    return true;
}

bool zombie_mode_message_prompt_end(void)
{
    if (!s_prompt) return false;
    s_prompt = false;
    g_message_flags |= (unsigned short)(g_messageFlagsBackup & ZM_MSG_PROMPT_MASK);
    return true;
}

// Text is read in a race: in the mode it types out ZM_MSG_FAST_CHARS
// characters each time the original types one.
#define ZM_MSG_FAST_CHARS 4

int zombie_mode_message_chars_per_frame(void)
{
    return zombie_mode_armed() ? ZM_MSG_FAST_CHARS : 1;
}

// The opening narration (display_game_loading_message's 0x5B) turns its pages
// and closes on its own timer (\p / \d, 48 frames a page). In the original any
// press skips it (main_loop); in the mode it plays out on every copy, a little
// faster. game_loop waits for it before its first frame, pumping the link
// meanwhile (zombie_mode_reconnect_wait).
#define ZM_INTRO_MSG_ID       0x5B
#define ZM_INTRO_PAGE_PERCENT 70

static bool zm_intro_showing(void)
{
    return zombie_mode_armed() && g_lastScanCodeOrMsgID == ZM_INTRO_MSG_ID;
}

bool zombie_mode_intro_unskippable(void)
{
    return zm_intro_showing();
}

unsigned char zombie_mode_message_page_delay(unsigned char frames)
{
    if (!zm_intro_showing() || frames == 0) return frames;
    unsigned int shorter = (unsigned int)frames * ZM_INTRO_PAGE_PERCENT / 100;
    return (unsigned char)(shorter ? shorter : 1);
}

// An event's bit_op clearing message flags. The original's scenes stop the
// player's state machine (0x01), the monsters (0x04) and the effects (0x08)
// while they hold the control (0x100). In the mode the room runs on - a
// monster on another copy grabs and hits regardless, and its GRAB/PHURT wait
// on 0x04 - so only the control is taken: the survivor stands idle, as under
// a menu, and a grab or a hit reaches it (and closes the event's text). The
// skipped scenes' flag replay runs outside any event and keeps the original's.
#define ZM_MSG_WORLD_FLAGS 0x000D

unsigned int zombie_mode_event_flag_clear(unsigned int mask)
{
    if (!zombie_mode_armed() || s_evtSlot < 0 || s_evtSlot >= 8) return mask;
    // A reveal does not take the control either.
    if (s_reveal[s_evtSlot]) return mask & ~(unsigned int)(ZM_MSG_WORLD_FLAGS | 0x0100);
    return mask & ~(unsigned int)ZM_MSG_WORLD_FLAGS;
}

// The letterbox (main-state MSF_INTENSITY_RAMP, MainLoop.cpp's bars) of the
// scenes played as a close-up stays off. The mode runs them on without the
// control - cut short by a hit, their later cuts dropped, a reveal's group
// ending in an event that never comes - so the command that takes the bars
// away again (bit_op 05 0F 01) can be left unrun: the armor room's display
// case (ROOM7050 event 1), the large gallery's solve (ROOM6170 event 20, whose
// bars event 21 takes away) and the rest of kReveals and the examines.
// Other scenes keep theirs (zm_letterbox_frame is their net).
bool zombie_mode_event_letterbox(void)
{
    if (!zm_in_examine()) return true;
    dbg_printf("[msg] event %d (slot %d): letterbox left off\n", (int)s_script[s_evtSlot], (int)s_evtSlot);
    return false;
}

// Only scenes (SCD events) turn the letterbox on - no room's per-frame script
// does. Up with no event running for a second (a skipped scene's flag replay
// that turned it on and not off, a scene the mode ended early), it goes.
#define ZM_LETTERBOX_IDLE_FRAMES 30
static void zm_letterbox_frame(void)
{
    static int idle = 0;
    bool running = false;
    for (int i = 0; i < 8 && !running; i++) running = g_ScdEventTable[i].active != 0;
    if ((g_main_state_flags & MSF_INTENSITY_RAMP) == 0 || running) {
        idle = 0;
        return;
    }
    if (++idle < ZM_LETTERBOX_IDLE_FRAMES) return;
    idle = 0;
    g_main_state_flags &= ~MSF_INTENSITY_RAMP;
    dbg_printf("[msg] letterbox left up with no scene running: taken away\n");
}

// The event VM's state 1, a reveal's command on the player: the look-at
// (0x81 / 0x82), the pose (0x83 / 0x84 / 0x85), the stand-back (0x86), the
// flags (0x87), the timer (0x88) and the frame (0x89 / 0x8A) are not applied -
// the survivor keeps its own action. A pose's end flag (the system flag the
// player's animation raises when it finishes, which the scene waits on) is
// raised at once. The command's width to step over, 0 to run it.
int zombie_mode_event_pose_skip(int slot, const void* entity, const unsigned char* op)
{
    if (!zombie_mode_armed() || slot < 0 || slot >= 8 || !s_reveal[slot] || entity != (const void*)&g_playerEntity) {
        return 0;
    }
    unsigned char done = 0;
    int width;
    switch (op[0]) {
    case 0x81: width = (op[1] & 0x0F) != 0 ? 10 : 2; break;
    case 0x82: width = 1; break;
    case 0x83: done = op[6]; width = 7; break;
    case 0x84: done = op[2]; width = 4; break;
    case 0x85: done = op[3]; width = 4; break;
    case 0x86: width = 1; break;
    case 0x87: width = 4; break;
    case 0x88: width = 4; break;
    case 0x89: case 0x8A: width = 2; break;
    default: return 0;
    }
    if (done != 0) Flg_on((int)g_SysFlags, done);
    return width;
}

// player_pos_set from a reveal: the survivor stays where it is.
bool zombie_mode_player_pos_skip(void)
{
    return zombie_mode_armed() && s_evtSlot >= 0 && s_evtSlot < 8 && s_reveal[s_evtSlot];
}

bool zombie_mode_message_is_passive(void)
{
    return s_passive;
}

bool zombie_mode_message_page_done(bool pressed)
{
    if (!s_passive) return pressed;
    // A grab, a hit or death: out of the way at once, so the event waiting on
    // it moves on (its question, if any, is answered No).
    if (g_playerEntity.isBeingAttackedFlag != 0 || g_playerEntity.health < 0) {
        s_waitStart = 0;
        return true;
    }
    if (s_passiveEvent && pressed) {
        s_waitStart = 0;
        return true;
    }
    unsigned int now = zm_game_time_ms();
    if (s_waitStart == 0) {
        s_waitStart = now ? now : 1;
        return false;
    }
    if (now - s_waitStart < zm_msg_page_ms()) return false;
    s_waitStart = 0;
    return true;
}

bool zombie_mode_message_end(void)
{
    if (!s_passive) return false;
    zm_msg_finish();
    return true;
}

// The mod's own text. A pause is asked for in the room, so it is passive
// there like any room message - with none it would be the original's
// no-pause message, which waits for a button and is not given up to the
// inventory or a door (a frozen survivor). Under a menu or a door it is
// asked with none, as before.
static const unsigned char* s_customText = NULL;

const unsigned char* zombie_mode_message_custom(void)
{
    return s_customText;
}

void zm_message_show(const unsigned char* text)
{
    bool room = (g_main_state_flags & (MSF_MENU_ACTIVE | MSF_DOOR_TRANSITION)) == 0 && g_openMenuFlag == 0;
    s_customText = text;
    set_message_display(0xFB, room ? 0xFF : 0);
    s_customText = NULL;
}

// ---------------------------------------------------------------------------
// Door keys. The original takes the key out of the inventory only once the
// "You have used the key" message closes (check_event_item_usage), and on its
// last lock opens the inventory to ask "This key is useless now. Discard?".
// In the mode the message does not hold the survivor, who walks through the
// door the next frame - the question came up in the room behind it, with the
// game running. So the key is used up as it turns, and one with no lock left
// is simply discarded; a message says so once the survivor has the control
// with no other text up (in the next room, when it walked straight through).
// ---------------------------------------------------------------------------
static constexpr auto s_keyDiscarded = STR("The \\i is no\\nlonger useful.\\p You discarded it.");

void zombie_mode_key_turned(void)
{
    if (!zombie_mode_armed() || g_eventItemUsedFlag != 1) return;
    g_eventItemUsedFlag = 0;
    use_room_action_item();
}

bool zombie_mode_key_used_up(unsigned char* slots, int index)
{
    if (!zombie_mode_armed()) return false;
    s_discardedKey = slots[index * 2];
    slots[index * 2] = 0;
    rearrange_item_slots();
    dbg_printf("[msg] key 0x%02X used up: discarded\n", (int)s_discardedKey);
    return true;
}

static void zm_key_discard_frame(void)
{
    if (s_discardedKey == 0) return;
    if (g_playerEntity.health < 0) {
        s_discardedKey = 0;
        return;
    }
    if ((g_menu_choice_id & 0x80) != 0 || (g_message_flags & 0x0101) != 0x0101 ||
        (g_main_state_flags & (MSF_MENU_ACTIVE | MSF_DOOR_TRANSITION)) != 0 || g_openMenuFlag != 0) {
        return;
    }
    g_selectedItemId = s_discardedKey;     // the message's \i
    s_discardedKey = 0;
    zm_message_show(s_keyDiscarded);
}

void zombie_mode_message_drop(void)
{
    if (!s_passive || (g_menu_choice_id & 0x80) == 0) return;
    g_menu_choice_id &= 0x7F;
    zm_msg_finish();
}
