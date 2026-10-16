#include "ZombieModeInternal.h"
#include "ZombieNet.h"
#include "../PrintText.h"
#include "../../platform/platform.h"
#include "../../DebugPrint.h"
#include <cstring>
#include <cstdio>

// ============================================================================
// ZombieTimeout.cpp - port-added: the survivors' timeout (multiplayer).
//
// Once a match, from ZM_TIMEOUT_AT_MS on the game clock, a living survivor
// may call a timeout from its map (OPTIONS, then Run twice) while every
// living survivor stands in a safe room (an item box room,
// zm_random_room_safe). The host checks the same from the survivors' STATE
// and, if it holds, stops the match for ZM_TIMEOUT_MS on every copy: the
// pause is the disconnect pause's (zm_net_pause_active), so the game clock,
// the director's points and every timer on zm_game_time_ms stand still and
// no copy simulates. The survivors' copies show the route map meanwhile
// (arrows pick a room); the director's says the match is paused.
//
// The director may turn it off in the lobby (zm_net_timeout_enabled, the
// lobby packet's option bits).
//
// ZM_EV_TIMEOUT {op, player, seed low, seed high, duration / 100 ms}:
//   1 request   survivor -> host
//   2 start     host -> all
//   3 refused   host -> the requester; args[1] the reason (TO_WHY_*)
//   4 end       host -> all
// Each copy times the pause from START's arrival; the host's END closes it
// (a survivor's copy also lets it go ZM_TIMEOUT_SLACK_MS after its own end).
// ============================================================================

#define ZM_TIMEOUT_AT_MS    (5 * 60000)
#define ZM_TIMEOUT_MS       60000u
#define ZM_TIMEOUT_SLACK_MS 5000u
#define ZM_TIMEOUT_CONFIRM_MS 3000u      // the second Run press, to call it
#define ZM_TIMEOUT_WHY_MS   3000u        // a refusal stays on the map

enum { TO_REQUEST = 1, TO_START = 2, TO_REFUSE = 3, TO_END = 4 };
enum { TO_WHY_OFF = 1, TO_WHY_USED, TO_WHY_EARLY, TO_WHY_UNSAFE, TO_WHY_BUSY };

static bool         s_used;           // spent this match (the host's word on the others)
static bool         s_active;
static unsigned int s_startMs;        // plat_time_ms of START (sent, or arrived)
static unsigned int s_durationMs;
static int          s_caller = -1;
static bool         s_requested;      // a survivor's: waiting for the host
static unsigned int s_requestMs;
static unsigned int s_confirmMs;      // a survivor's: first Run press
static int          s_why;            // a survivor's: the last refusal...
static unsigned int s_whyMs;          // ...and when it came
static bool         s_mapOpened;      // the timeout opened the survivor's map
static bool         s_mapWasOpen;
static unsigned int s_rawWas;
static bool         s_readyNoted;
static char         s_line[40];

void zm_timeout_reset(void)
{
    s_used = s_active = s_requested = false;
    s_caller = -1;
    s_confirmMs = 0;
    s_why = 0;
    s_mapOpened = s_mapWasOpen = false;
    s_readyNoted = false;
    s_rawWas = 0;
}

static bool to_on(void)
{
    return zombie_mode_armed() && zm_game_role() != ZM_NET_OFF && zm_net_timeout_enabled();
}

static unsigned int to_seed(void) { return zm_net_seed(); }
static bool to_seed_ok(const short* a)
{
    return (unsigned short)a[2] == (unsigned short)to_seed() &&
           (unsigned short)a[3] == (unsigned short)(to_seed() >> 16);
}

bool zm_timeout_active(unsigned int now)
{
    if (!s_active) return false;
    unsigned int limit = s_durationMs + (zm_net_role() == ZM_NET_ZOMBIE ? 0u : ZM_TIMEOUT_SLACK_MS);
    return now - s_startMs < limit;
}

static unsigned int to_left_ms(void)
{
    unsigned int gone = plat_time_ms() - s_startMs;
    return gone >= s_durationMs ? 0 : s_durationMs - gone;
}

// Every living survivor in a safe room - this copy's own too, when it plays
// one (a survivor's, or an AI director's host).
static bool to_all_safe(int)
{
    int living = 0;
    for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
        if (zm_net_char(i) < 0) continue;
        const ZmNetPeerState* p = zm_seat_state(i);
        if (p == NULL || p->dead || p->spectating) continue;
        if (p->transitioning || !zm_random_room_safe(p->stage, p->room)) return false;
        living++;
    }
    return living > 0;
}

// Why a timeout cannot be called now (0 if it can).
static int to_why_not(int self)
{
    if (!to_on()) return TO_WHY_OFF;
    if (s_used) return TO_WHY_USED;
    if (s_active || zombie_mode_match_over() || zm_net_pause_active()) return TO_WHY_BUSY;
    int elapsed = zm_net_role() == ZM_NET_ZOMBIE ? zm_survivors_in_ms() : (int)zm_match_elapsed_ms();
    if (elapsed < ZM_TIMEOUT_AT_MS) return TO_WHY_EARLY;
    if (!to_all_safe(self)) return TO_WHY_UNSAFE;
    return 0;
}

static void to_begin(int caller, unsigned int durationMs)
{
    s_used = true;
    s_active = true;
    s_requested = false;
    s_startMs = plat_time_ms();
    s_durationMs = durationMs;
    s_caller = caller;
    s_confirmMs = 0;
    (void)zm_game_time_ms();     // the clock stops from here
    dbg_printf("[timeout] called by survivor %d for %u ms\n", caller, durationMs);
}

void zm_timeout_take(const short* a, int src)
{
    if (!to_seed_ok(a)) return;
    if (a[0] == TO_REQUEST) {
        if (zm_net_role() != ZM_NET_ZOMBIE || src < 0 || src >= ZM_NET_MAX_PLAYERS || zm_net_char(src) < 0) return;
        int why = to_why_not(-1);
        const ZmNetPeerState* p = zm_seat_state(src);
        if (!why && (p == NULL || p->dead || p->spectating)) why = TO_WHY_UNSAFE;
        if (why) {
            zm_net_send_event_to(src, ZM_EV_TIMEOUT, TO_REFUSE, (short)why, (short)to_seed(),
                                 (short)(to_seed() >> 16), 0, 0, 0, 0);
            dbg_printf("[timeout] survivor %d refused (%d)\n", src, why);
            return;
        }
        to_begin(src, ZM_TIMEOUT_MS);
        zm_net_send_event_to(ZM_NET_ALL, ZM_EV_TIMEOUT, TO_START, (short)src, (short)to_seed(),
                             (short)(to_seed() >> 16), (short)(ZM_TIMEOUT_MS / 100), 0, 0, 0);
        return;
    }
    if (src != ZM_NET_DIRECTOR) return;
    switch (a[0]) {
    case TO_START:
        if (!s_active) to_begin(a[1], (unsigned int)(unsigned short)a[4] * 100u);
        break;
    case TO_REFUSE:
        s_requested = false;
        s_confirmMs = 0;
        s_why = a[1];
        s_whyMs = plat_time_ms();
        if (a[1] == TO_WHY_USED) s_used = true;
        break;
    case TO_END:
        s_used = true;
        if (s_active) dbg_printf("[timeout] ended by the host\n");
        s_active = false;
        break;
    }
}

// The survivors' route map (as ZombiePerks.cpp opens it), with `help`.
static void to_route_map(const char* help)
{
    bool radio = zm_net_char(zm_net_self()) == ZM_CHAR_RICHARD;
    zm_map_set_route(ZM_ROUTE_KEYS, radio, true,
                     radio ? "PURPLE KEYS BLUE DOORS ORANGE MONSTERS" : "PURPLE KEYS  BLUE KEY DOORS", help);
}

// Every frame (zombie_mode_reconnect_wait) and after the pause: the host ends
// it; a survivor's map goes back as it was.
void zm_timeout_poll(void)
{
    if (s_active && !zm_timeout_active(plat_time_ms())) {
        s_active = false;
        if (zm_net_role() == ZM_NET_ZOMBIE)
            zm_net_send_event_to(ZM_NET_ALL, ZM_EV_TIMEOUT, TO_END, 0, (short)to_seed(),
                                 (short)(to_seed() >> 16), 0, 0, 0, 0);
        dbg_printf("[timeout] over\n");
    }
    if (!s_active && s_mapOpened) {
        if (zm_map_is_open() && !s_mapWasOpen) zm_map_toggle();
        else to_route_map("ARROWS: ROOM  OPTIONS: CLOSE");   // the survivor's own map, as it was
        s_mapOpened = false;
    }
    if (s_requested && plat_time_ms() - s_requestMs > 10000u) s_requested = false;
}

// zombie_mode_reconnect_wait's paused frame while the timeout holds the match
// (and no link is down). False: not ours - the reconnect screen shows.
bool zm_timeout_pause_frame(void)
{
    if (!zm_timeout_active(plat_time_ms()) || zm_net_link_pause_active()) return false;
    unsigned int sec = (to_left_ms() + 999) / 1000;
    if (zm_game_role() == ZM_NET_SURVIVOR) {
        if (!s_mapOpened) {
            s_mapWasOpen = zm_map_is_open();
            s_mapOpened = true;
            to_route_map("TIMEOUT - ARROWS: ROOM");
            if (!s_mapWasOpen) zm_map_toggle();
            s_rawWas = g_button_pressed_id;
        }
        unsigned int raw = g_button_pressed_id;
        zm_map_input(raw & ~s_rawWas);
        s_rawWas = raw;
        zm_map_draw();
        return true;
    }
    zm_draw_centered("SURVIVORS TIMEOUT", 80, 0x7F);
    zm_draw_centered("THE SURVIVORS ARE PLANNING THEIR ROUTE", 102, 0x7F);
    char text[40];
    snprintf(text, sizeof(text), "THE MATCH RESUMES IN %u:%02u", sec / 60, sec % 60);
    zm_draw_centered(text, 124, 0x7F);
    return true;
}

// The survivors' map line (right of the room name): the timeout's state.
// NULL when there is nothing to say.
const char* zm_timeout_map_line(void)
{
    if (zm_game_role() != ZM_NET_SURVIVOR || !to_on()) return NULL;
    unsigned int now = plat_time_ms();
    if (zm_timeout_active(now)) {
        unsigned int sec = (to_left_ms() + 999) / 1000;
        snprintf(s_line, sizeof(s_line), "TIMEOUT %u:%02u LEFT", sec / 60, sec % 60);
        return s_line;
    }
    if (s_requested) return "TIMEOUT REQUESTED";
    if (s_why && now - s_whyMs < ZM_TIMEOUT_WHY_MS) {
        switch (s_why) {
        case TO_WHY_USED:   return "TIMEOUT ALREADY USED";
        case TO_WHY_EARLY:  return "TIMEOUT NOT YET READY";
        case TO_WHY_UNSAFE: return "ALL MUST BE IN SAFE ROOMS";
        case TO_WHY_OFF:    return "TIMEOUTS ARE OFF";
        default:            return "TIMEOUT REFUSED";
        }
    }
    if (g_playerEntity.health < 0) return NULL;
    switch (to_why_not(zm_net_self())) {
    case 0:
        return s_confirmMs && now - s_confirmMs < ZM_TIMEOUT_CONFIRM_MS
            ? "RUN AGAIN: CALL TIMEOUT" : "RUN: CALL TIMEOUT";
    case TO_WHY_USED: return "TIMEOUT USED";
    case TO_WHY_EARLY: {
        unsigned int at = ZM_TIMEOUT_AT_MS - zm_match_elapsed_ms();
        unsigned int sec = (at + 999) / 1000;
        snprintf(s_line, sizeof(s_line), "TIMEOUT IN %u:%02u", sec / 60, sec % 60);
        return s_line;
    }
    case TO_WHY_UNSAFE: return "TIMEOUT: ALL IN SAFE ROOMS";
    default: return NULL;
    }
}

// A survivor's map is up (zombie_mode_survivor_input): Run calls the
// timeout, a second press within ZM_TIMEOUT_CONFIRM_MS confirms it.
void zm_timeout_map_input(unsigned int edge)
{
    if (zm_game_role() != ZM_NET_SURVIVOR || (edge & 0x0040) == 0 || s_requested) return;
    if (g_playerEntity.health < 0) return;
    int why = to_why_not(zm_net_self());
    if (why) {
        s_why = why;
        s_whyMs = plat_time_ms();
        s_confirmMs = 0;
        return;
    }
    unsigned int now = plat_time_ms();
    if (!s_confirmMs || now - s_confirmMs >= ZM_TIMEOUT_CONFIRM_MS) {
        s_confirmMs = now;
        return;
    }
    s_confirmMs = 0;
    s_requested = true;
    s_requestMs = now;
    s_why = 0;
    zm_net_send_event_to(ZM_NET_DIRECTOR, ZM_EV_TIMEOUT, TO_REQUEST, (short)zm_net_self(),
                         (short)to_seed(), (short)(to_seed() >> 16), 0, 0, 0, 0);
    dbg_printf("[timeout] requested\n");
}

// A survivor's HUD: a one-time note when it comes, then a line while it is
// still to be had.
void zm_timeout_draw(void)
{
    if (zm_game_role() != ZM_NET_SURVIVOR || !to_on() || s_used || s_active) return;
    if (g_playerEntity.health < 0 || zm_match_elapsed_ms() < ZM_TIMEOUT_AT_MS) return;
    if (!s_readyNoted) {
        s_readyNoted = true;
        zm_note("TIMEOUT READY - SEE THE MAP");
    }
    static const char text[] = "TIMEOUT READY";
    snprintf(PRINT_TEXT_BUFFER, sizeof(PRINT_TEXT_BUFFER), "%s", text);
    zm_text_encode(PRINT_TEXT_BUFFER);
    PrintText8x14((short)(320 - 8 - (int)(sizeof(text) - 1) * 8), 208, 0x7F, 0);
}
