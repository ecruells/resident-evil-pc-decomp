#include "ZombieModeInternal.h"
#include "ZombieNet.h"
#include "../../DebugPrint.h"
#include "../../platform/platform.h"
#include <cstdio>
#include <cstring>

// ============================================================================
// ZombiePerks.cpp - port-added: each survivor's own strengths (multiplayer).
//
// Every survivor plays Chris's scenario in the model it picked; the character
// decides only these. All of them read this copy's own player: a perk is the
// local survivor's, and the other copies see its effects through the usual
// sync (health in STATE, hits through ZM_EV_HIT, a heal through ZM_EV_HEAL).
//
//   toughness   max health on a 0-10 scale, 22 HP a point: the game's own
//               Chris (140) is 8 and Jill (96) is 6. Barry 10 (184), Chris,
//               Enrico and Richard 8, Jill and Rebecca 6. The EKG and the
//               healing items read max health, so they scale with it.
//   piano       Jill, Rebecca and Richard can play the bar's piano with the
//               sheet music; Richard starts with it
//   Chris       50% more world ammo per pick-up (rounded down), not drops
//   Jill        eight inventory slots; her lockpick opens the sword-key doors
//   Barry       his gun shots go through every enemy in the line of fire
//   Rebecca     her first aid spray heals the survivors around her too;
//               revives in 2 seconds with spray or green herb, at 50% health
//   Enrico      runs and walks 10% faster, double knife damage,
//               zombie bite holds last half as long
//   Richard     his radio: the survivors' map (OPTIONS) also shows which
//               rooms have monsters in them
// ============================================================================

// The local survivor's character, -1 when this copy is not one.
static int zm_perk_char(void)
{
    if (!zombie_mode_armed() || zm_game_role() != ZM_NET_SURVIVOR) return -1;
    return zm_net_char(zm_net_self());
}

// Inventory limits follow the survivor pick, independently of Chris's scenario.
// Keep each original caller's limit when the mod is off or this is the director.
int zombie_mode_inventory_slots(int originalSlots)
{
    int ch = zm_perk_char();
    if (ch < 0) return originalSlots;
    return ch == ZM_CHAR_JILL ? 8 : 6;
}

static int zm_perk_toughness(int ch)
{
    switch (ch) {
    case ZM_CHAR_BARRY:   return 10;
    case ZM_CHAR_CHRIS:
    case ZM_CHAR_ENRICO:
    case ZM_CHAR_RICHARD: return 8;
    default:              return 6;     // Jill, Rebecca
    }
}

// InitializeGame, after its own health / max health (GameStart.cpp).
void zombie_mode_player_stats(void)
{
    int ch = zm_perk_char();
    if (ch < 0) return;
    int hp = 140 + (zm_perk_toughness(ch) - 8) * 22;
    g_playerEntity.maxHealth = (unsigned char)hp;
    g_playerEntity.health = (short)hp;
    g_PlayerHealthCopy = g_playerEntity.health;
    dbg_printf("[perks] %s: toughness %d, %d HP\n", zm_char_name(ch), zm_perk_toughness(ch), hp);
}

// TEST ONLY: Chris starts with the grenade launcher and Jill with the
// flamethrower (and their ammo), to try the tier 3 weapons on the other
// body. Set to 0 to undo.
#define ZM_TEST_HEAVY_WEAPONS 0

// Each character's starting kit: (item, quantity) pairs into `kit`, at most
// `max`; returns the count.
static int zm_perk_kit_of(int ch, unsigned char (*kit)[2], int max)
{
    int n = 0;
    struct { unsigned char id, qty; } items[6];
#if ZM_TEST_HEAVY_WEAPONS
    if (ch == ZM_CHAR_CHRIS) {
        items[n].id = ITEM_BAZOOKA_EXPLOSIVE; items[n++].qty = 6;
        items[n].id = ITEM_EXPLOSIVE_ROUNDS;  items[n++].qty = 6;
    } else if (ch == ZM_CHAR_JILL) {
        items[n].id = ITEM_FLAMETHROWER; items[n++].qty = 240;
        items[n].id = ITEM_FUEL;         items[n++].qty = 120;   // to try the refill
    }
#endif
    switch (ch) {
    case ZM_CHAR_BARRY:
        items[n].id = ITEM_COLT_PYTHON_MAG; items[n++].qty = 6;     // full cylinder
        break;
    case ZM_CHAR_JILL:
        items[n].id = ITEM_BERETTA; items[n++].qty = 15;
        items[n].id = ITEM_CLIP;    items[n++].qty = 30;    // two clips, one slot
        items[n].id = ITEM_LOCK_PICK; items[n++].qty = 1;
        break;
    case ZM_CHAR_CHRIS:
        items[n].id = ITEM_BERETTA; items[n++].qty = 15;
        items[n].id = ITEM_CLIP;    items[n++].qty = 30;    // two clips, one slot
        items[n].id = ITEM_LIGHTER; items[n++].qty = 1;
        break;
    case ZM_CHAR_REBECCA:
        items[n].id = ITEM_BERETTA; items[n++].qty = 15;
        items[n].id = ITEM_CLIP;    items[n++].qty = 30;    // two clips, one slot
        items[n].id = ITEM_FIRST_AID_SPRAY; items[n++].qty = 1;
        break;
    case ZM_CHAR_RICHARD:
        items[n].id = ITEM_BERETTA; items[n++].qty = 15;
        items[n].id = ITEM_CLIP;    items[n++].qty = 30;    // two clips, one slot
        items[n].id = ITEM_MUSIC_NOTES; items[n++].qty = 1;
        break;
    default:                        // Enrico: the handgun
        items[n].id = ITEM_BERETTA; items[n++].qty = 15;
        items[n].id = ITEM_CLIP;    items[n++].qty = 30;    // two clips, one slot
        break;
    }
    // Everyone keeps the knife, last.
    items[n].id = ITEM_KNIFE; items[n++].qty = 0;
    int count = 0;
    for (int i = 0; i < n && count < max; i++) {
        kit[count][0] = items[i].id;
        kit[count][1] = items[i].qty;
        count++;
    }
    return count;
}

// The starting kit (ZombieSurvivor.cpp zs_give_kit): the local survivor's,
// or -1 for the default kit.
int zm_perk_kit(unsigned char (*kit)[2], int max)
{
    int ch = zm_perk_char();
    if (ch < 0) return -1;
    return zm_perk_kit_of(ch, kit, max);
}

// The character select screen's text (ZombieLobby.cpp).
static const char* zm_perk_item_name(unsigned char id)
{
    switch (id) {
    case ITEM_BERETTA:          return "BERETTA HANDGUN";
    case ITEM_CLIP:             return "HANDGUN CLIP: 30 ROUNDS";
    case ITEM_COLT_PYTHON_MAG:  return "COLT PYTHON: 6 ROUNDS";
    case ITEM_LOCK_PICK:        return "LOCKPICK";
    case ITEM_LIGHTER:          return "LIGHTER";
    case ITEM_FIRST_AID_SPRAY:  return "FIRST AID SPRAY";
    case ITEM_KNIFE:            return "COMBAT KNIFE";
    case ITEM_MUSIC_NOTES:      return "SHEET MUSIC";
    default:                    return "ITEM";
    }
}

void zm_perk_describe(int ch, ZmPerkInfo* out)
{
    memset(out, 0, sizeof(*out));
    switch (ch) {
    case ZM_CHAR_CHRIS:
        out->fullName = "CHRIS REDFIELD";
        out->description = "S.T.A.R.S. ALPHA TEAM POINT MAN. A FORMER AIR FORCE PILOT AND A CRACK SHOT.";
        out->perk = "SCAVENGER: FINDS 50 PERCENT MORE AMMO. DROPPED AMMO GETS NO BONUS.";
        break;
    case ZM_CHAR_JILL:
        out->fullName = "JILL VALENTINE";
        out->description = "S.T.A.R.S. ALPHA TEAM REAR SECURITY. TRAINED IN BOMB DISPOSAL AND LOCKS.";
        out->perk = "MASTER OF UNLOCKING: HER LOCKPICK OPENS SWORD KEY DOORS. CARRIES 8 INVENTORY SLOTS. PLAYS THE PIANO.";
        break;
    case ZM_CHAR_BARRY:
        out->fullName = "BARRY BURTON";
        out->description = "S.T.A.R.S. ALPHA TEAM WEAPONS EXPERT. A SWAT VETERAN WITH A LOADED COLT PYTHON.";
        out->perk = "HEAVY HITTER: HIS SHOTS GO THROUGH EVERY ENEMY IN LINE.";
        break;
    case ZM_CHAR_REBECCA:
        out->fullName = "REBECCA CHAMBERS";
        out->description = "S.T.A.R.S. BRAVO TEAM MEDIC. THE YOUNGEST MEMBER AND A GIFTED CHEMIST.";
        out->perk = "FIELD MEDIC: SPRAY HEALS NEARBY ALLIES. REVIVES IN 2 SECONDS WITH SPRAY OR GREEN HERB, AT HALF HEALTH. PLAYS THE PIANO.";
        break;
    case ZM_CHAR_RICHARD:
        out->fullName = "RICHARD AIKEN";
        out->description = "S.T.A.R.S. BRAVO TEAM COMMUNICATIONS EXPERT. HIS RADIO IS NEVER FAR.";
        out->perk = "RADIO: HIS MAP ALSO SHOWS THE MONSTERS IN EVERY ROOM. CARRIES THE SHEET MUSIC AND PLAYS THE PIANO.";
        break;
    default:
        out->fullName = "ENRICO MARINI";
        out->description = "S.T.A.R.S. BRAVO TEAM CAPTAIN. A SEASONED LEADER, QUICK ON HIS FEET.";
        out->perk = "QUICK: MOVES 10 PERCENT FASTER. DOUBLE KNIFE DAMAGE. ZOMBIE GRABS LAST HALF AS LONG.";
        break;
    }
    out->toughness = zm_perk_toughness(ch);
    out->health = 140 + (out->toughness - 8) * 22;
    unsigned char kit[6][2];
    int n = zm_perk_kit_of(ch, kit, 6);
    out->weapon = kit[0][0];
    // Repeated items are folded into one line with a count.
    for (int i = 0; i < n && out->itemCount < 6; i++) {
        const char* name = zm_perk_item_name(kit[i][0]);
        bool merged = false;
        for (int k = 0; k < out->itemCount && !merged; k++) {
            if (out->itemIds[k] == kit[i][0]) { out->itemCounts[k]++; merged = true; }
        }
        if (merged) continue;
        out->itemIds[out->itemCount] = kit[i][0];
        out->itemCounts[out->itemCount] = 1;
        out->items[out->itemCount++] = name;
    }
}

// Chris: 50% more world ammo (rounded down; the same ammo ids the DC covers,
// less ink ribbons). Shared by the award and the "no room" check. Transfers
// and death drops must conserve ammunition, including Chris's own reclaims.
unsigned char zombie_mode_pickup_quantity(unsigned char itemId, unsigned char quantity,
                                         const unsigned char* record)
{
    if (zm_perk_char() != ZM_CHAR_CHRIS) return quantity;
    if (zm_drop_is_pickup(record)) return quantity;
    if (ITEM_ROCKET_LAUNCHER < itemId && itemId < ITEM_EMPTY_BOTTLE) {
        int q = quantity + quantity / 2;
        return (unsigned char)(q > 250 ? 250 : q);
    }
    return quantity;
}

// Jill: the lockpick in her inventory stands in for the sword key.
bool zombie_mode_has_lockpick(void)
{
    if (zm_perk_char() != ZM_CHAR_JILL) return false;
    for (int i = 0; i < 8; i++) {
        if (g_ItemsSlots[i].Id == ITEM_LOCK_PICK) return true;
    }
    return false;
}

// Barry: shots go through.
bool zombie_mode_shots_penetrate(void)
{
    return zm_perk_char() == ZM_CHAR_BARRY;
}

// Enrico: the player's own movement (Add_speedXZ) is 10% longer.
int zombie_mode_player_move_speed(int speed)
{
    if (zm_perk_char() != ZM_CHAR_ENRICO) return speed;
    return speed + speed / 10;
}

// Weapon damage is chosen from the original enemy/difficulty table first.
int zombie_mode_player_weapon_damage(unsigned char weaponId, int damage)
{
    if (zm_perk_char() == ZM_CHAR_ENRICO && weaponId == ITEM_KNIFE)
        return damage * 2;
    return damage;
}

// Zombie grabs run on the victim's copy, including network puppet grabs.
// Keep bite damage per tick intact; the shorter hold delivers fewer bites.
int zombie_mode_player_zombie_grab_time(int frames)
{
    if (zm_perk_char() == ZM_CHAR_ENRICO) return frames / 2;
    return frames;
}

// ---------------------------------------------------------------------------
// Rebecca's spray
//
// Using it heals her as usual and sends ZM_EV_HEAL { x, z, stage | room << 8 }
// to everyone; a survivor's copy whose player is in that room within
// ZM_HEAL_RANGE heals to full. With her own health full the spray still goes
// (the menu would otherwise refuse it), so she can patch up the others.
// ---------------------------------------------------------------------------
#define ZM_HEAL_RANGE 3000

int zombie_mode_heal_item_used(unsigned char itemId, int used)
{
    if (zm_perk_char() != ZM_CHAR_REBECCA || itemId != ITEM_FIRST_AID_SPRAY) return used;
    zm_net_send_event(ZM_EV_HEAL, (short)g_playerEntity.scaMatrixData.localMatrix.t[0],
                      (short)g_playerEntity.scaMatrixData.localMatrix.t[2],
                      (short)(g_stageId | (g_roomId << 8)), 0);
    dbg_printf("[perks] Rebecca's spray: healing the survivors nearby\n");
    return 1;
}

void zm_perk_take_heal(const short* a)
{
    if (zm_game_role() != ZM_NET_SURVIVOR) return;
    if (a[2] != (short)(g_stageId | (g_roomId << 8)) || g_playerEntity.health < 0) return;
    long long dx = (long long)g_playerEntity.scaMatrixData.localMatrix.t[0] - (unsigned short)a[0];
    long long dz = (long long)g_playerEntity.scaMatrixData.localMatrix.t[2] - (unsigned short)a[1];
    if (dx * dx + dz * dz > (long long)ZM_HEAL_RANGE * ZM_HEAL_RANGE) return;
    g_playerEntity.health = (short)g_playerEntity.maxHealth;
    dbg_printf("[perks] healed by Rebecca to %d\n", (int)g_playerEntity.health);
}

// ---------------------------------------------------------------------------
// The survivors' map - and Richard's radio
//
// OPTIONS (raw 0x0900) opens and closes the director's map (ZombieMap.cpp)
// for every survivor, look-only, as the route map of this game's scenario:
// the key-locked doors and which key opens each, where the keys and crests
// lie, this survivor's room and the others'. Richard's radio adds the monster
// counts (the roster every copy keeps). While it is up the pad is the map's -
// the survivor stands still - and neither the inventory nor the options
// screen opens: OPTIONS is the map's for the survivors.
// ---------------------------------------------------------------------------
static unsigned int s_radioRawWas = 0;

static bool zm_perk_map_user(void)
{
    return zm_perk_char() >= 0;     // any multiplayer survivor
}

void zombie_mode_survivor_input(void)
{
    if (zombie_mode_pickup_waiting()) {
        g_PlayerDpadHeld = g_PlayerDpadPressed = 0;
        g_PlayerPadHeld = g_button_pressed_id = 0;
        return;
    }
    if (!zm_perk_map_user()) return;
    if (zm_spec_input()) {          // dead: the pad picks whom to watch
        s_radioRawWas = g_button_pressed_id;
        return;
    }
    if (zm_revive_input()) return;
    if (zm_yawn_prompt_input()) return;     // a boss room's entry vote (ZombieYawn.cpp)
    if (zm_access_input()) return;  // the keypad (ZombieKeypad.cpp)
    unsigned int raw = g_button_pressed_id;
    unsigned int edge = raw & ~s_radioRawWas;
    s_radioRawWas = raw;
    bool optionsPressed = (raw & 0x0900) == 0x0900 && (edge & 0x0900) != 0;
    bool free = (g_message_flags & 0x0101) == 0x0101 && (g_main_state_flags & MSF_MENU_ACTIVE) == 0 &&
                g_playerEntity.health >= 0 && g_playerEntity.isBeingAttackedFlag == 0;
    bool radio = zm_perk_char() == ZM_CHAR_RICHARD;
    if (optionsPressed && (free || zm_map_is_open())) {
        zm_map_set_route(ZM_ROUTE_KEYS, radio, true,
                         radio ? "PURPLE KEYS BLUE DOORS ORANGE MONSTERS" : "PURPLE KEYS  BLUE KEY DOORS",
                         "ARROWS: ROOM  OPTIONS: CLOSE");
        zm_map_toggle();
        return;
    }
    if (zm_map_is_open()) {
        if (!free) {                    // grabbed, hurt or dead: the map drops
            zm_map_toggle();
            return;
        }
        zm_map_input(edge);
        zm_timeout_map_input(edge);     // Run: the survivors' timeout (ZombieTimeout.cpp)
        g_PlayerDpadHeld = 0;
        g_PlayerDpadPressed = 0;
    }
}

bool zm_perk_radio_blocks_menu(void)
{
    if (!zm_perk_map_user()) return false;
    return zm_map_is_open() || (g_button_pressed_id & 0x0900) == 0x0900;
}
