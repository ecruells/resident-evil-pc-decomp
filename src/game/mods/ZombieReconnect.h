#pragma once
#include "../../Globals.h"

// Pointer-free checkpoint. Room assets and transient attack/menu animations
// are reconstructed on load; no process addresses travel over the network.
struct ZmReconnectPlayer {
    unsigned int sequence;
    BioCardLayout card;
    unsigned char inventory[16], indices[8];
    unsigned int inventoryMask;
    int x, y, z;
    short angle, health;
    unsigned char maxHealth, healthFlags;
    unsigned short pickupToken, boxToken, dropSequence, monsterSequence;
    int stats[4];
    int revives[4];
    unsigned char reviveSpentMask;
    unsigned short shotgunReplacement; // consumed placement serial
    unsigned char pickaxeSpent;
};

struct ZmReconnectWorld {
    unsigned int magic, seed, bytes;
    int elapsedMs;
    ZmReconnectPlayer player;
    BioCardLayout shared;
    int revives[4];
    unsigned int trapRemaining[8];
    unsigned char trapRooms[16];
    unsigned int shotgun[16];
    int rosterBytes, dropBytes;
};

#define ZM_RECONNECT_BLOB_MAX 65536
void zm_reconnect_capture(ZmReconnectPlayer* out);
void zm_shotgun_export(unsigned int out[16]);
void zm_shotgun_import(const unsigned int in[16]);
void zm_shotgun_reconcile(int player, ZmReconnectPlayer* checkpoint);
int zm_reconnect_build(void* out, int capacity, const ZmReconnectPlayer* player);
bool zm_reconnect_import(const void* data, int size);
void zm_reconnect_new_game(void);
void zm_reconnect_player_ready(void);
void zm_reconnect_room_ready(void);
bool zm_reconnect_restore_pending(void);
void zm_reconnect_forget(void);

int zm_world_reconnect_export(void* out, int capacity);
bool zm_world_reconnect_import(const void* data, int size, unsigned short nextUid);
void zm_world_reconnect_enemy(unsigned char stage, unsigned char room, unsigned short uid,
    unsigned char id, short health, bool alive, int x, int y, int z, short angle);
void zm_world_reconnect_shared(const BioCardLayout* card);
int zm_drops_reconnect_export(void* out, int capacity);
bool zm_drops_reconnect_import(const void* data, int size, unsigned short sequence);
unsigned short zm_drops_reconnect_sequence(void);
void zm_drops_reconnect_reconcile(int player, ZmReconnectPlayer* checkpoint);
void zm_drops_reconnect_abandon(const ZmReconnectPlayer* checkpoint);
unsigned short zm_world_reconnect_sequence(void);
void zm_stats_reconnect_export(int out[4]);
void zm_stats_reconnect_import(const int in[4]);
void zm_spec_reconnect_export(int out[4]);
void zm_spec_reconnect_import(const int in[4]);
unsigned char zm_spec_reconnect_spent(void);
void zm_spec_reconnect_restore_spent(unsigned char mask);
void zm_spec_reconnect_reconcile(int player, ZmReconnectPlayer* checkpoint);
void zm_trap_reconnect_export(unsigned int remaining[8], unsigned char rooms[16]);
void zm_trap_reconnect_import(const unsigned int remaining[8], const unsigned char rooms[16]);
unsigned short zm_pickups_reconnect_token(void);
void zm_pickups_reconnect_restore(unsigned short token);
void zm_pickups_reconnect_awarded(void);
void zm_pickups_reconnect_reconcile(int player, ZmReconnectPlayer* checkpoint);
unsigned short zm_box_reconnect_token(void);
void zm_box_reconnect_restore(unsigned short token);
void zm_box_reconnect_reconcile(int player, ZmReconnectPlayer* checkpoint);
void zm_reconnect_match_clock(int elapsedMs);
void zm_reconnect_failed(void);
void zm_reconnect_route_events(void);
