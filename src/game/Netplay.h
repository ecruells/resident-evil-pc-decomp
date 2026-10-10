// Netplay.h - co-op over the network (port addition, not in the original)
//
// Lockstep: both machines run the whole game, and every tick (one main_loop,
// one PlayerPad_Update) both use the same two pad words - player 1's from the
// host, player 2's from the joiner. Each sends its own word a few ticks ahead
// (the input delay) and waits for the other's before the tick runs, so the two
// simulations stay identical as long as they started identical: same binary,
// same game data, same settings (checked at the handshake), same start.
//
// config.ini [Net]: Mode = off / host / join, Peer = the host's name or address
// (join only; a Tailscale machine name works), Port, Delay (ticks of input
// delay), LocalInput = both / keyboard / pad (which of this machine's devices
// make its player's word).
//
// Every NETPLAY_CHECK_TICKS both hash the state that matters (players,
// enemies, the random state, the room, the timer) and swap the hashes; the
// first mismatch is a desync, written to netplay.log with the parts.
#pragma once
#include "../Globals.h"

extern int  g_netMode;          // NET_MODE_*
extern char g_netPeer[128];
extern int  g_netPort;
extern int  g_netDelay;
extern int  g_netLocalInput;    // NET_INPUT_*

enum { NET_MODE_OFF = 0, NET_MODE_HOST = 1, NET_MODE_JOIN = 2 };
enum { NET_INPUT_BOTH = 0, NET_INPUT_KEYBOARD = 1, NET_INPUT_PAD = 2 };

// Configured for netplay (host or join)?
int  Netplay_Configured(void);
// Connected and running in lockstep?
int  Netplay_IsActive(void);

// Platform main loop, before the first tick: one step of the handshake.
// Returns 1 once connected, 0 while still waiting, -1 when it cannot start
// (socket, peer address, settings mismatch - netplay.log says which).
int  Netplay_Connect(void);

// ReadPadBoth, once per tick: send this machine's word (localWord) and wait
// for the other's; returns both words for this tick. If the other player
// leaves or stops answering, the session ends and this game closes too.
void Netplay_ExchangeInputs(DWORD localWord, DWORD* p1Word, DWORD* p2Word);

// Platform main loop, on exit: tell the other game this one is leaving.
void Netplay_Shutdown(void);
