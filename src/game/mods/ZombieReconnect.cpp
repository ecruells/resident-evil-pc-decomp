#include "ZombieReconnect.h"
#include "ZombieModeInternal.h"
#include "ZombieNet.h"
#include "../../platform/platform.h"
#include <cstring>
#include <cstdio>

static unsigned char s_restore[ZM_RECONNECT_BLOB_MAX];
static int s_restoreBytes;
static bool s_initialized, s_roomReady;
static bool s_readyReported;
static unsigned int s_checkpointSequence;

bool zm_reconnect_restore_pending(void) { return s_restoreBytes != 0; }
void zm_reconnect_forget(void) { s_restoreBytes = 0; s_initialized = s_roomReady = s_readyReported = false; }

void zm_reconnect_capture(ZmReconnectPlayer* out)
{
    memset(out, 0, sizeof(*out));
    out->sequence = ++s_checkpointSequence;
    out->card = g_BioCard;
    memcpy(out->inventory, g_ItemSlotsPointer, sizeof(out->inventory));
    memcpy(out->indices, g_ItemSlotIndices, sizeof(out->indices));
    out->inventoryMask = g_ItemSlotsBitmask;
    out->x = g_playerEntity.scaMatrixData.localMatrix.t[0];
    out->y = g_playerEntity.scaMatrixData.localMatrix.t[1];
    out->z = g_playerEntity.scaMatrixData.localMatrix.t[2];
    out->angle = g_playerEntity.directionAngle;
    out->health = g_playerEntity.health;
    out->maxHealth = g_playerEntity.maxHealth;
    out->healthFlags = g_playerEntity.healthStatusFlags;
    out->pickupToken = zm_pickups_reconnect_token();
    out->boxToken = zm_box_reconnect_token();
    out->dropSequence = zm_drops_reconnect_sequence();
    out->monsterSequence = zm_world_reconnect_sequence();
    out->shotgunReplacement = zm_shotgun_replacement_consumed();
    out->pickaxeSpent = zm_shotgun_pickaxe_consumed();
    zm_stats_reconnect_export(out->stats);
    zm_spec_reconnect_export(out->revives);
    out->reviveSpentMask = zm_spec_reconnect_spent();
}

int zm_reconnect_build(void* out, int capacity, const ZmReconnectPlayer* player)
{
    if (capacity < (int)sizeof(ZmReconnectWorld)) return 0;
    ZmReconnectWorld* h = (ZmReconnectWorld*)out;
    memset(h, 0, sizeof(*h));
    h->magic = 0x52454331;
    h->seed = zm_net_seed();
    h->elapsedMs = zm_survivors_in_ms();
    h->player = *player;
    h->shared = g_BioCard;
    zm_spec_reconnect_export(h->revives);
    zm_trap_reconnect_export(h->trapRemaining, h->trapRooms);
    zm_shotgun_export(h->shotgun);
    int used = sizeof(*h);
    h->rosterBytes = zm_world_reconnect_export((unsigned char*)out + used, capacity - used);
    if (h->rosterBytes <= 0) return 0;
    used += h->rosterBytes;
    h->dropBytes = zm_drops_reconnect_export((unsigned char*)out + used, capacity - used);
    if (h->dropBytes <= 0) return 0;
    used += h->dropBytes;
    h->bytes = used;
    return used;
}

bool zm_reconnect_import(const void* data, int size)
{
    if (size < (int)sizeof(ZmReconnectWorld) || size > ZM_RECONNECT_BLOB_MAX) return false;
    const ZmReconnectWorld* h = (const ZmReconnectWorld*)data;
    if (h->magic != 0x52454331 || h->seed != zm_net_seed() || h->bytes != (unsigned int)size ||
        h->rosterBytes <= 0 || h->dropBytes <= 0 || h->rosterBytes > size - (int)sizeof(*h) ||
        h->dropBytes != size - (int)sizeof(*h) - h->rosterBytes ||
        h->player.shotgunReplacement > 1023 || h->player.pickaxeSpent > 1 || h->player.card.totalHeldItems > 8 || h->player.card.equippedItemId > h->player.card.totalHeldItems ||
        h->player.card.stageId > 6 || h->player.card.roomId >= 58 ||
        h->elapsedMs < -1 || h->elapsedMs > 3600000 || !h->player.maxHealth ||   // an hour: boss fights hold and add to the clock
        (h->shotgun[0] & 3) > 2 || h->shotgun[0] > 1022 || h->shotgun[1] > 2 || h->shotgun[2] > 15001 ||
        h->shotgun[3] > 4 || (h->shotgun[4] & ~14u) || (h->shotgun[5] & ~30u) || h->shotgun[6] > 1023 || h->shotgun[7] > 4) return false;
    for (int i = 0; i < 4; i++)
        if (h->shotgun[8+i] > 1023 || (h->shotgun[12+i] != 0 && h->shotgun[12+i] != ITEM_SHOTGUN && h->shotgun[12+i] != ITEM_BROKEN_SHOTGUN)) return false;
    memcpy(s_restore, data, size);
    s_restoreBytes = size;
    s_initialized = s_roomReady = s_readyReported = false;
    return true;
}

void zm_reconnect_new_game(void)
{
    if (!s_restoreBytes) return;
    const ZmReconnectWorld& h = *(const ZmReconnectWorld*)s_restore;
    s_checkpointSequence = h.player.sequence;
    g_BioCard = h.player.card;
    g_PlayerDpadHeld = g_PlayerDpadPressed = 0;
    g_PlayerPosXCopy = (short)h.player.x;
    g_PlayerPosZCopy = (short)h.player.z;
    g_PlayerDirAngleCopy = h.player.angle;
    // InitializeGame binds this pointer only after zombie_mode_new_game.
    // A relaunched process still has a null pointer here; bind the same
    // save-card inventory storage that normal startup uses before restoring.
    g_ItemSlotsPointer = g_ItemsSlots;
    memcpy(g_ItemSlotsPointer, h.player.inventory, sizeof(h.player.inventory));
    memcpy(g_ItemSlotIndices, h.player.indices, sizeof(h.player.indices));
    g_ItemSlotsBitmask = h.player.inventoryMask;
    const unsigned char* roster = s_restore + sizeof(h);
    bool valid = zm_world_reconnect_import(roster, h.rosterBytes, h.player.monsterSequence) &&
                 zm_drops_reconnect_import(roster + h.rosterBytes, h.dropBytes, h.player.dropSequence);
    if (!valid) { zm_reconnect_failed(); return; }
    zm_world_reconnect_shared(&h.shared);
    zm_stats_reconnect_import(h.player.stats);
    zm_spec_reconnect_import(h.revives);
    zm_spec_reconnect_restore_spent(h.player.reviveSpentMask);
    zm_trap_reconnect_import(h.trapRemaining, h.trapRooms);
    zm_shotgun_import(h.shotgun);
    zm_pickups_reconnect_restore(h.player.pickupToken);
    zm_box_reconnect_restore(h.player.boxToken);
    zm_reconnect_match_clock(h.elapsedMs);
    s_initialized = true;
    g_playerEntity.position.x = (short)h.player.x;
    g_playerEntity.position.y = (short)h.player.y;
    g_playerEntity.position.z = (short)h.player.z;
    g_playerEntity.directionAngle = h.player.angle;
}

void zm_reconnect_player_ready(void)
{
    if (!s_initialized) return;
    const ZmReconnectPlayer& p = ((const ZmReconnectWorld*)s_restore)->player;
    g_playerEntity.health = p.health;
    g_playerEntity.maxHealth = p.maxHealth;
    g_playerEntity.healthStatusFlags = p.healthFlags;
    g_PlayerHealthCopy = p.health;
}

void zm_reconnect_room_ready(void)
{
    if (!s_initialized || s_roomReady) return;
    const ZmReconnectPlayer& p = ((const ZmReconnectWorld*)s_restore)->player;
    zm_reconnect_player_ready();
    g_playerEntity.position.x = (short)p.x;
    g_playerEntity.position.y = (short)p.y;
    g_playerEntity.position.z = (short)p.z;
    g_playerEntity.posY = (unsigned short)p.y;
    g_playerEntity.directionAngle = p.angle;
    g_playerEntity.scaMatrixData.localMatrix.t[0] = p.x;
    g_playerEntity.scaMatrixData.localMatrix.t[1] = p.y;
    g_playerEntity.scaMatrixData.localMatrix.t[2] = p.z;
    s_roomReady = true;
}

bool zombie_mode_reconnect_wait(void)
{
    if (!zombie_mode_armed() || zm_game_role() == ZM_NET_OFF || zombie_mode_match_over()) return false;
    if (s_roomReady && !s_readyReported) { zm_net_rejoin_ready(); s_readyReported = true; }
    zm_net_poll();
    zm_timeout_poll();
    bool waited = false;
    while (zm_net_pause_active() && zm_net_status() != ZM_NET_EXPIRED && !zombie_mode_match_over()) {
        waited = true;
        (void)zm_game_time_ms();        // the game clock notes the pause
        g_PlayerPadHeld = g_PlayerPadPressed = g_PlayerDpadHeld = g_PlayerDpadPressed = 0;
        // The survivors' timeout: their route map, the director's notice.
        if (zm_timeout_pause_frame()) {
            Task_sleep(1);
            zm_net_poll();
            continue;
        }
        zm_draw_centered("MATCH PAUSED", 80, 0x7F);
        zm_draw_centered(zm_net_status() == ZM_NET_LOST ? "CONNECTION LOST - RECONNECTING" :
                         "WAITING FOR A SURVIVOR TO RECONNECT", 102, 0x7F);
        char text[48]; snprintf(text, sizeof(text), "%d SECONDS REMAINING", zm_net_pause_seconds());
        zm_draw_centered(text, 124, 0x7F);
        Task_sleep(1);
        zm_net_poll();
    }
    zm_timeout_poll();
    if (zm_net_status() == ZM_NET_EXPIRED) zm_reconnect_failed();
    if (waited) g_PlayerPadHeld = g_PlayerPadPressed = g_PlayerDpadHeld = g_PlayerDpadPressed = 0;
    return waited;
}
