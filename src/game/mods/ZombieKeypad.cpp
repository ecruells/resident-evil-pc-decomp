#include "ZombieModeInternal.h"
#include "ZombieNet.h"
#include "../PrintText.h"
#include "../SFXIds.h"
#include "../../DebugPrint.h"
#include <cstdio>
#include <cstring>

// ============================================================================
// ZombieKeypad.cpp - port-added: the back area's two ways in.
//
// The 2F back passage (ROOM7130) and the rooms off it - the rough passage,
// the large and private libraries, the shed - are reached only by the small
// elevator from the kitchen or by the 2F left stairs' keypad door into the
// rough passage. One crest always lies there (ZombieRandom.cpp), so the
// survivors must open one of them:
//   - the elevator runs once a survivor USEs the battery at either of its
//     doors (kitchen or back passage); until then those doors say it has no
//     power. The battery lies outside the back area;
//   - the keypad door opens on this game's pass number, four digits from the
//     seed (zm_random_pass_code). A note lying outside the back area tells
//     it: reading it shows the number and puts it on the survivor's route
//     map (OPTIONS). Pressing at the key panel (ROOM7010's slot 5, the
//     original's "A numeric key panel." event) brings up the keypad.
// Both stay open for the rest of the match, for everyone: they are story
// flags (ZM_FLAG2_*), carried by the STORY sync and the reconnect snapshot.
// The original's keypad door has no way back - the rough passage's side is
// only a "The door is locked." zone (slot 1) - so an open keypad builds a
// door there, to the stairs side's door.
// The director's monsters ignore both, as they ignore locks; they never use
// the key panel. The AI survivor treats both as shut until opened
// (zm_door_usable).
// ============================================================================

extern void Flg_on(int baseAddr, unsigned int bitIndex);              // 0x00473ef0 CmdFunctions.cpp
extern void FUN_00473f10(int* baseAddr, unsigned int bitIndex);        // clear flag
extern int  cmd_door_set(void);                                        // 0x0C CmdFunctions.cpp

#define KP_STAGE             STAGE_MANSION_RETURN_2F
#define KP_ROOM_STAIRS       0x01       // 2F left stairs (ROOM7010)
#define KP_ROOM_BACK         0x13       // 2F back passage (ROOM7130), the elevator's top
#define KP_ROOM_ROUGH        0x14       // rough passage (ROOM7140)
#define KP_ELEVATOR_CAR      0x00       // ROOM7000, the car both elevator doors lead to
#define KP_STAIRS_DOOR_SLOT  1          // ROOM7010's door_set to the rough passage (the init disarms it)
#define KP_STAIRS_PANEL_SLOT 5          // ROOM7010's key panel: create_room_event, event 2
#define KP_ROUGH_DOOR_SLOT   1          // ROOM7140's "The door is locked." zone
#define KP_DOOR_MARGIN       1500       // how far from an elevator door the battery can be put in
#define KP_WRONG_MS          1200

// The elevator car is a survivor-only transit room, even after power is restored.
bool zm_access_room_elevator(unsigned char stage, unsigned char room)
{
    return room == KP_ELEVATOR_CAR &&
        (stage == STAGE_MANSION_2F || stage == KP_STAGE);
}
#define KP_DIGITS            4

static bool          s_known;           // this copy's survivor has read the note
static bool          s_keypad;          // the keypad is up
static unsigned char s_entry[KP_DIGITS];
static int           s_len;
static int           s_cursor;          // 0-11: 1-9, then DEL, 0, END
static unsigned int  s_wrongAt;         // the last wrong number's time (0: none showing)
static unsigned int  s_rawWas;
static bool          s_batteryPending;  // USE accepted: put in once the menu is closed
static bool          s_wasPowered, s_wasOpen;
static bool          s_roughDoorBuilt;  // this room load has the rough passage's door
static char          s_filesLine[32];

bool zm_access_powered(void)
{
    return Flg_ck((int)g_ScenarioFlags2, ZM_FLAG2_ELEVATOR_POWER) != 0;
}

bool zm_access_keypad_open(void)
{
    return Flg_ck((int)g_ScenarioFlags2, ZM_FLAG2_KEYPAD_OPEN) != 0;
}

bool zm_access_keypad_up(void)
{
    return s_keypad;
}

// A passive message (ZombieMessages.cpp) from ASCII: '\n' breaks the line.
static unsigned char s_message[96];
static void kp_message(const char* text)
{
    int n = 0;
    for (const char* p = text; *p != '\0' && n < (int)sizeof(s_message) - 2; p++) {
        s_message[n++] = *p == '\n' ? 0x02 : pft_detail::encodeChar((unsigned char)*p);
    }
    s_message[n++] = 0x01;
    s_message[n] = 0x00;
    zm_message_show(s_message);
}

static bool kp_here(unsigned char room)
{
    return zombie_mode_armed() && g_stageId == KP_STAGE && g_roomId == room;
}

void zm_access_new_game(void)
{
#ifdef QUICK_DEBUG
    // Quick testing: the elevator powered and the keypad door open from the start.
    Flg_on((int)g_ScenarioFlags2, ZM_FLAG2_ELEVATOR_POWER);
    Flg_on((int)g_ScenarioFlags2, ZM_FLAG2_KEYPAD_OPEN);
#else
    FUN_00473f10((int*)g_ScenarioFlags2, ZM_FLAG2_ELEVATOR_POWER);
    FUN_00473f10((int*)g_ScenarioFlags2, ZM_FLAG2_KEYPAD_OPEN);
#endif
    s_known = false;
    s_keypad = false;
    s_len = 0;
    s_wrongAt = 0;
    s_batteryPending = false;
    s_wasPowered = s_wasOpen = false;
    s_roughDoorBuilt = false;
}

// ---------------------------------------------------------------------------
// The keypad door, open: the stairs side's door armed (and its key panel
// off), the rough passage's door back built over its locked-door zone.
// ---------------------------------------------------------------------------
static unsigned char s_roughDoor[0x1A];

static void kp_build_rough_door(void)
{
    unsigned char* op = s_roughDoor;
    memset(op, 0, sizeof(s_roughDoor));
    op[0] = 0x0C;                                   // door_set
    op[1] = KP_ROUGH_DOOR_SLOT;
    unsigned char* r = op + 2;
    // The zone: ROOM7140's locked-door zone (slot 1's room_action_set), in the
    // corridor's south wall - pressed from the corridor at z 27900.
    *(unsigned short*)(r + 0x00) = 10100;
    *(unsigned short*)(r + 0x02) = 25300;
    *(unsigned short*)(r + 0x04) = 1700;
    *(unsigned short*)(r + 0x06) = 2400;
    // The stairs side's door (ROOM7010 slot 1): its animation and sound.
    r[0x08] = 0x01;
    r[0x09] = 0x00;
    r[0x0A] = 0x00;
    r[0x0B] = 6;                                    // ROOM7010's camera 6 covers the arrival
    r[0x0C] = 0;                                    // unlocked
    r[0x0D] = KP_ROOM_STAIRS;
    // In the corridor below the stairs side's door (zone x 14700-16200, z
    // 25700-27600; floor to z 25800), facing away from it (-z: angle 0x400).
    *(short*)(r + 0x0E) = 15450;
    *(short*)(r + 0x10) = 0;
    *(short*)(r + 0x12) = 25000;
    *(short*)(r + 0x14) = 0x400;
    r[0x16] = 0;
    r[0x17] = 0x81;                                 // armed, action press
}

static void kp_apply_room(void)
{
    if (!zm_access_keypad_open()) return;
    if (kp_here(KP_ROOM_STAIRS)) {
        // The init's door_set left its record and probe flags; only the
        // handler was cleared (room_action_arm 01 00 81).
        g_RoomActionTable[KP_STAIRS_DOOR_SLOT * 12] = 1;     // door_try_enter
        g_RoomActionTable[KP_STAIRS_PANEL_SLOT * 12] = 0;
    } else if (kp_here(KP_ROOM_ROUGH) && !s_roughDoorBuilt) {
        kp_build_rough_door();
        unsigned char* saved = g_ScdOpcodes;
        g_ScdOpcodes = s_roughDoor;
        cmd_door_set();
        g_ScdOpcodes = saved;
        s_roughDoorBuilt = true;
        dbg_printf("[access] the rough passage's door to the stairs is built\n");
    }
}

// After the room's init (zombie_mode_room_spawn).
void zm_access_room(void)
{
    s_keypad = false;
    s_roughDoorBuilt = false;
    kp_apply_room();
}

// ---------------------------------------------------------------------------
// The elevator
// ---------------------------------------------------------------------------
// Near the elevator door of the kitchen (slot 0) or the back passage (slot 1)?
static bool kp_near_elevator(int x, int z)
{
    int zx, zz, zw, zd;
    if (kp_here(ROOM_MANSION_KITCHEN)) { zx = 17500; zz = 5300; zw = 2000; zd = 2900; }
    else if (kp_here(KP_ROOM_BACK))    { zx = 13700; zz = 5300; zw = 1700; zd = 3900; }
    else return false;
    return x >= zx - KP_DOOR_MARGIN && x <= zx + zw + KP_DOOR_MARGIN &&
           z >= zz - KP_DOOR_MARGIN && z <= zz + zd + KP_DOOR_MARGIN;
}

static int kp_battery_slot(void)
{
    if (!g_ItemSlotsPointer) return -1;
    const unsigned char* slots = (const unsigned char*)g_ItemSlotsPointer;
    for (int i = 0; i < 8; i++) if (slots[i * 2] == ITEM_BATTERY) return i;
    return -1;
}

// The item menu's USE of the battery. True: accepted - the menu closes
// (zm_access_menu_finished) and the battery goes in once it has.
bool zm_access_use_battery(void)
{
    int x = g_playerEntity.scaMatrixData.localMatrix.t[0], z = g_playerEntity.scaMatrixData.localMatrix.t[2];
    if (zm_game_role() != ZM_NET_SURVIVOR || !kp_near_elevator(x, z)) {
        kp_message("Use this at the elevator.");
        return false;
    }
    if (zm_access_powered()) {
        kp_message("The elevator already has power.");
        return false;
    }
    if (g_playerEntity.health < 0 || g_playerEntity.isBeingAttackedFlag || kp_battery_slot() < 0) return false;
    s_batteryPending = true;
    return true;
}

bool zm_access_menu_finished(void)
{
    return zombie_mode_armed() && s_batteryPending;
}

static void kp_battery_in(void)
{
    s_batteryPending = false;
    int slot = kp_battery_slot();
    if (slot < 0 || zm_access_powered() || g_playerEntity.health < 0) return;
    unsigned char* item = (unsigned char*)g_ItemSlotsPointer + slot * 2;
    if (g_EquippedItemId == slot + 1) g_EquippedItemId = 0;
    item[0] = item[1] = 0;
    rearrange_item_slots();
    Flg_on((int)g_ScenarioFlags2, ZM_FLAG2_ELEVATOR_POWER);
    play_sfx(2, 0x22, 0);
    kp_message("The elevator has power now.");
    dbg_printf("[access] the battery is in: the elevator runs\n");
}

// door_try_enter, a survivor's: an elevator door without power.
bool zm_access_door_refused(const unsigned char* record)
{
    if (zm_access_powered() || record[0x0D] != KP_ELEVATOR_CAR) return false;
    if (!kp_here(ROOM_MANSION_KITCHEN) && !kp_here(KP_ROOM_BACK)) return false;
    kp_message("The elevator has no power.");
    return true;
}

bool zm_access_door_closed(unsigned char stage, unsigned char room, unsigned char dest)
{
    if (!zombie_mode_armed() || stage != KP_STAGE) return false;
    if ((room == ROOM_MANSION_KITCHEN || room == KP_ROOM_BACK) && dest == KP_ELEVATOR_CAR) return !zm_access_powered();
    if (room == KP_ROOM_STAIRS && dest == KP_ROOM_ROUGH) return !zm_access_keypad_open();
    return false;
}

// ---------------------------------------------------------------------------
// The note and the key panel
// ---------------------------------------------------------------------------
const char* zm_access_files_line(void)
{
    if (!s_known) return NULL;
    snprintf(s_filesLine, sizeof(s_filesLine), "PASS NUMBER %04u", zm_random_pass_code());
    return s_filesLine;
}

static void kp_close(void)
{
    s_keypad = false;
    s_len = 0;
}

bool zm_access_room_event(const unsigned char* entry)
{
    if (!zombie_mode_armed() || entry < g_RoomActionTable) return false;
    unsigned char slot = (unsigned char)((entry - g_RoomActionTable) / 12);
    if (zm_random_note_slot(slot)) {
        if (zm_game_role() != ZM_NET_SURVIVOR) return true;
        char text[64];
        snprintf(text, sizeof(text), "A note. The pass number\nis %04u.", zm_random_pass_code());
        kp_message(text);
        if (!s_known) zm_note("THE PASS NUMBER IS ON YOUR MAP");
        s_known = true;
        dbg_printf("[access] read the note: pass number %04u\n", zm_random_pass_code());
        return true;
    }
    if (!kp_here(KP_ROOM_STAIRS) || slot != KP_STAIRS_PANEL_SLOT) return false;
    // The original's event 2 ("A numeric key panel." / "You don't know the
    // pass number.") never runs in the mode.
    if (zm_game_role() != ZM_NET_SURVIVOR || zm_access_keypad_open() || s_keypad) return true;
    if (g_playerEntity.health < 0 || g_playerEntity.isBeingAttackedFlag) return true;
    s_keypad = true;
    s_len = 0;
    s_cursor = 0;
    s_wrongAt = 0;
    s_rawWas = g_button_pressed_id;     // the press that opened it types nothing
    play_sfx(SFX_UI_BANK, SFX_UI_DECIDE, 0);
    return true;
}

static void kp_enter(void)
{
    unsigned int value = 0;
    for (int i = 0; i < KP_DIGITS; i++) value = value * 10 + s_entry[i];
    s_len = 0;
    if (value != zm_random_pass_code()) {
        s_wrongAt = zm_game_time_ms();
        if (s_wrongAt == 0) s_wrongAt = 1;
        play_sfx(SFX_UI_BANK, SFX_UI_CANCEL, 0);
        dbg_printf("[access] wrong pass number %04u\n", value);
        return;
    }
    Flg_on((int)g_ScenarioFlags2, ZM_FLAG2_KEYPAD_OPEN);
    kp_close();
    play_sfx(2, 0x22, 0);
    kp_message("The door is unlocked.");
    kp_apply_room();
    dbg_printf("[access] the keypad door is open\n");
}

// zombie_mode_survivor_input: the pad is the keypad's while it is up -
// arrows move, Action presses a key, Aim takes the last digit back, Run
// leaves. Four digits are tried at once.
bool zm_access_input(void)
{
    unsigned int raw = g_button_pressed_id;
    unsigned int edge = raw & ~s_rawWas;
    s_rawWas = raw;
    if (!s_keypad) return false;
    if (!kp_here(KP_ROOM_STAIRS) || zm_access_keypad_open() || g_playerEntity.health < 0 ||
        g_playerEntity.isBeingAttackedFlag || (g_main_state_flags & MSF_MENU_ACTIVE) != 0) {
        kp_close();
        return false;
    }
    int col = s_cursor % 3, row = s_cursor / 3;
    if (edge & 0x1000) row = (row + 3) % 4;
    if (edge & 0x4000) row = (row + 1) % 4;
    if (edge & 0x8000) col = (col + 2) % 3;
    if (edge & 0x2000) col = (col + 1) % 3;
    if (row * 3 + col != s_cursor) {
        s_cursor = row * 3 + col;
        play_sfx(SFX_UI_BANK, SFX_UI_CURSOR, 0);
    }
    if (edge & 0x0040) {                                   // run: leave
        kp_close();
        play_sfx(SFX_UI_BANK, SFX_UI_CANCEL, 0);
    } else if ((edge & 0x0008) || ((edge & 0x0080) && s_cursor == 9)) {   // aim / DEL
        if (s_len > 0) s_len--;
        play_sfx(SFX_UI_BANK, SFX_UI_CURSOR, 0);
    } else if ((edge & 0x0080) && s_cursor == 11) {         // END
        kp_close();
        play_sfx(SFX_UI_BANK, SFX_UI_CANCEL, 0);
    } else if (edge & 0x0080) {
        s_entry[s_len++] = (unsigned char)(s_cursor == 10 ? 0 : s_cursor + 1);
        s_wrongAt = 0;
        play_sfx(SFX_UI_BANK, SFX_UI_DECIDE, 0);
        if (s_len == KP_DIGITS) kp_enter();
    }
    g_PlayerDpadHeld = 0;
    g_PlayerDpadPressed = 0;
    return true;
}

// ---------------------------------------------------------------------------
// Every frame, and the keypad's look
// ---------------------------------------------------------------------------
void zm_access_frame(void)
{
    if (!zombie_mode_armed()) return;
    if (s_batteryPending && !g_openMenuFlag && !(g_main_state_flags & (MSF_MENU_ACTIVE | MSF_ROOM_TRANSITION)) &&
        !g_roomTransitionBusy) kp_battery_in();
    // However the flags came (this copy, STORY, a reconnect): the room's doors,
    // and a word for everyone.
    bool powered = zm_access_powered(), open = zm_access_keypad_open();
    if (powered && !s_wasPowered) zm_note("THE ELEVATOR HAS POWER");
    if (open && !s_wasOpen) {
        zm_note("THE KEYPAD DOOR IS OPEN");
        kp_apply_room();
    }
    s_wasPowered = powered;
    s_wasOpen = open;
}

// Overlay depth (draw_rect flags 1, as ZombieMap.cpp): behind the glyphs.
#define KP_DEPTH_PANEL 32
#define KP_DEPTH_KEY   31

static void kp_fill(int x, int y, int w, int h, int r, int g, int b, int depth)
{
    RectDrawDesc rect;
    memset(&rect, 0, sizeof(rect));
    rect.textureId = 0;
    rect.x = (short)(x - g_ScreenOffsetX);
    rect.y = (short)(y - g_ScreenOffsetY);
    rect.w = (short)w;
    rect.h = (short)h;
    rect.r = (unsigned char)r; rect.g = (unsigned char)g; rect.b = (unsigned char)b;
    draw_rect(&rect, depth, 1);
}

static void kp_text(int x, int y, unsigned char color, const char* text)
{
    snprintf(PRINT_TEXT_BUFFER, sizeof(PRINT_TEXT_BUFFER), "%s", text);
    zm_text_encode(PRINT_TEXT_BUFFER);
    PrintText8x14((short)x, (short)y, color, 0);
}

void zm_access_draw(void)
{
    if (!s_keypad) return;
    static const char* const kKeys[12] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "DEL", "0", "END" };
    const int panelX = 72, panelY = 36, panelW = 176, panelH = 172;
    kp_fill(panelX, panelY, panelW, panelH, 0x14, 0x14, 0x1C, KP_DEPTH_PANEL);
    zm_draw_centered("KEY PANEL", (short)(panelY + 8), 0x8F);
    char line[16];
    unsigned int now = zm_game_time_ms();
    if (s_wrongAt != 0 && now - s_wrongAt < KP_WRONG_MS) {
        zm_draw_centered("WRONG NUMBER", (short)(panelY + 28), 2);
    } else {
        int n = 0;
        for (int i = 0; i < KP_DIGITS; i++) {
            line[n++] = i < s_len ? (char)('0' + s_entry[i]) : '-';
            if (i + 1 < KP_DIGITS) line[n++] = ' ';
        }
        line[n] = '\0';
        zm_draw_centered(line, (short)(panelY + 28), 4);
    }
    for (int k = 0; k < 12; k++) {
        int cx = 160 + (k % 3 - 1) * 36, y = panelY + 52 + (k / 3) * 22;
        int w = (int)strlen(kKeys[k]) * 8;
        if (k == s_cursor) kp_fill(cx - 15, y - 3, 30, 20, 0x50, 0x50, 0x68, KP_DEPTH_KEY);
        kp_text(cx - w / 2, y, k == s_cursor ? 4 : 0x8F, kKeys[k]);
    }
    zm_draw_centered("ACTION: PRESS", (short)(panelY + 142), 0x7F);
    zm_draw_centered("AIM: BACK  RUN: LEAVE", (short)(panelY + 156), 0x7F);
}
