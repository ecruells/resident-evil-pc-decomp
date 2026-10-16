#pragma once
#include "../../Globals.h"

// ============================================================================
// ZombieNet.h - the zombie mod's multiplayer link (port-added).
//
// Up to four copies of the game, one per player: the director (who plays the
// monsters) hosts, and up to three survivors join. Every copy runs only its
// own player's room. Each sends its own character's state every frame - room,
// position, health and the full 15-joint pose - and shows the others as
// puppets when they share its room. Interactions (shots, grabs, sounds,
// effects, the monster roster) travel as reliable events addressed to one
// player or to everyone.
//
// Transport: one UDP socket per copy (platform.h plat_net_*), a star around
// the host. The host binds a known port and learns each survivor's address
// from its first packet; survivors bind any port and talk only to the host,
// which relays survivor-to-survivor traffic (states, monsters, events).
//
// Player indices: 0 is the director (the host); survivors are 1..3 in the
// order they joined.
// ============================================================================

enum ZmNetRole {
    ZM_NET_OFF = 0,        // single player: the AI plays the other character
    ZM_NET_ZOMBIE = 1,     // this copy is the director (the host)
    ZM_NET_SURVIVOR = 2,   // this copy is a survivor
};

enum ZmNetStatus {
    ZM_NET_IDLE = 0,
    ZM_NET_HOSTING,        // bound, taking survivors
    ZM_NET_JOINING,        // sending hellos to the host
    ZM_NET_CONNECTED,      // a survivor with a player index
    ZM_NET_LOST,           // was connected, nothing heard for too long
    ZM_NET_FAILED,         // could not open the socket / resolve the host
    ZM_NET_EXPIRED,        // reconnect grace elapsed; this survivor is out
};

#define ZM_NET_MAX_PLAYERS 4
#define ZM_NET_ALL         0xFF        // event destination: everyone else
#define ZM_NET_DIRECTOR    0

// Survivor characters (the model each survivor wears; the scenario is
// Chris's whatever they pick). One player per character: the host settles
// clashes. 0-3 are the player models (char10-char13); Richard and Enrico are
// Bravo team's NPC models (em1027 / em1028), moved with Chris's animations and
// holding Chris's weapons (ZombieMode.cpp, "Borrowed characters").
enum {
    ZM_CHAR_CHRIS = 0, ZM_CHAR_JILL = 1, ZM_CHAR_BARRY = 2, ZM_CHAR_REBECCA = 3,
    ZM_CHAR_RICHARD = 4, ZM_CHAR_ENRICO = 5,
    ZM_CHAR_COUNT = 6
};
const char* zm_char_name(int ch);

// [Net] Address / Port in config.ini - the last host joined, and the port.
extern char           g_zmNetAddress[64];
extern unsigned short g_zmNetPort;

// The role this copy plays in the current session (ZM_NET_OFF without one).
int  zm_net_role(void);
int  zm_net_status(void);
const char* zm_net_status_text(void);
const char* zm_net_status_hint(void);
// This copy's player index (0 director, 1..3 survivor), -1 without a session.
int  zm_net_self(void);
// A player's character (ZM_CHAR_*), -1 for a free seat; the director's own
// seat reads -1 too.
int  zm_net_char(int player);
int  zm_net_survivor_count(void);

// Session control (the lobby).
void zm_net_host(void);                                   // be the director
void zm_net_join(const char* address, unsigned short port, int character);
void zm_net_set_char(int character);                      // a survivor's pick, in the lobby
// Is `character` worn by a player other than this copy's?
bool zm_net_char_taken(int character);
void zm_net_lobby_go(void);                               // host: everyone into the game
bool zm_net_lobby_started(void);                          // the host said go
unsigned int zm_net_seed(void);                           // the game's scenario seed (the host's)

// The lobby's map review (ZombieLobby.cpp). The host owns the phase; each
// team may veto one map (the survivors only all together).
enum { ZM_LOBBY_JOIN = 0, ZM_LOBBY_MAP = 1 };
enum { ZM_VOTE_NONE = 0, ZM_VOTE_ACCEPT = 1, ZM_VOTE_VETO = 2 };
#define ZM_VETO_DIRECTOR  1
#define ZM_VETO_SURVIVORS 2
int  zm_net_lobby_phase(void);
int  zm_net_veto_used(void);                              // ZM_VETO_* bits
int  zm_net_vote(int player);                             // a survivor's ZM_VOTE_*
void zm_net_set_phase(int phase);                         // host
void zm_net_reroll(int vetoBit);                          // host: a new map, that team's veto spent
void zm_net_set_vote(int vote);                           // survivor
// The survivors' timeout (ZombieTimeout.cpp): the director's lobby switch.
bool zm_net_timeout_enabled(void);
void zm_net_set_timeout_enabled(bool on);                 // host
bool zm_net_survivor_ready(int player);                   // host: reported in (in the game)
// The AI director (ZombieDirectorAI.cpp): the host's level, 1 easy .. 5
// hell on earth, 0 for a human director. No map review then: each survivor picks
// a character in the lobby and votes ZM_VOTE_ACCEPT as ready.
int  zm_net_ai_level(void);
void zm_net_set_ai_level(int level);                      // host, before GO
// Seats 0..limit-1 exist: three survivors either way (against the AI the
// host's seat 0 is one of them).
int  zm_net_seat_limit(void);
void zm_net_stop(void);

// Pump the socket: receive, time out, resend. Call once per frame anywhere a
// session can be live (the lobby, the game loop).
void zm_net_poll(void);
bool zm_net_pause_active(void);       // a link down or the survivors' timeout
bool zm_net_link_pause_active(void);  // a link down only
int  zm_net_pause_seconds(void);
unsigned int zm_game_time_ms(void);
// Rejoin the last match this copy joined, by its saved match id.
bool zm_net_rejoin_saved(void);
bool zm_net_rejoining(void);
bool zm_net_rejoin_downloaded(void);
void zm_net_rejoin_ready(void);

// Game start: a barrier. Survivors report in; the host waits for all of them
// (or `timeoutMs`) and then releases everyone. False if this copy ends up
// alone (the game then runs single player).
bool zm_net_start_game(unsigned int timeoutMs);

// Intro barrier: local 0 entered, 1 loaded, 2 finished; host 0 wait, 1 load, 2 play, 3 release.
int zm_net_bumper_phase(int progress);
// Host: every survivor has finished its intro (true without a session).
bool zm_net_bumper_released(void);
