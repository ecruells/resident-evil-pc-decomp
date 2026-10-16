#include "ZombieModeInternal.h"
#include "ZombieNet.h"
#include "ZombieReconnect.h"
#include "../entities/EntityCommon.h"
#include "../../DebugPrint.h"
#include "../../platform/platform.h"
#include <cstdio>
#include <cstring>

// ============================================================================
// ZombieStats.cpp - the end of a match (port-added).
//
// Every way a match ends - a survivor out through the back exit, the clock
// running out, every survivor dead - ends on the same screen on every copy:
// the picture fades to white, the result (YOU WIN! / YOU LOSE / GAME OVER)
// and how it came about, then what each player did:
//   * the director: points spent and earned, monsters bought, traps set,
//     monsters lost, hits its monsters landed on survivors (ZombieEconomy.cpp);
//   * each survivor: when it died (or that it escaped / lived), its hits and
//     kills, and the damage it took.
// Each copy keeps its own player's numbers - the director's copy the
// director's, a survivor's copy that survivor's (single player: both, the AI
// survivor's in the player entity) - and sends them once the match is over
// (ZM_EV_STATS); everyone shows what has come in. A survivor that left the
// game shows as such.
// ============================================================================

struct ZmSurvivorStats {
    bool have;
    int  deathSec;     // match time of its death, -1 alive
    int  hits, kills, damage;
};

static ZmSurvivorStats s_surv[ZM_NET_MAX_PLAYERS];   // by player index (single player: 1)
static ZmEconStats     s_director;
static bool            s_directorHave = false;
static bool            s_over = false;
static int             s_reason = ZM_END_ESCAPE;
static int             s_winner = -1;                // the survivor that escaped
static int             s_matchSec = 0;

// This copy's survivor (a survivor's copy, or single player's AI).
static int   s_myHits = 0, s_myKills = 0, s_myDamage = 0, s_myDeathSec = -1;
static short s_myLastHealth = 0;
static bool  s_myLastHealthHave = false;

void zm_stats_reconnect_export(int out[4])
{
    out[0] = s_myHits; out[1] = s_myKills; out[2] = s_myDamage; out[3] = s_myDeathSec;
}
void zm_stats_reconnect_import(const int in[4])
{
    s_myHits = in[0]; s_myKills = in[1]; s_myDamage = in[2]; s_myDeathSec = in[3];
    s_myLastHealthHave = false;
}

#define ZM_STATS_SINGLE_SURVIVOR 1

static bool zm_stats_has_survivor(void)
{
    return zm_game_role() != ZM_NET_ZOMBIE;
}

// This copy keeps the director's account: its copy, single player's, or the
// host of an AI director's game.
static bool zm_stats_has_director(void)
{
    return zm_game_role() != ZM_NET_SURVIVOR || zm_ai_hosted();
}

static int zm_stats_self(void)
{
    return zm_game_role() == ZM_NET_OFF ? ZM_STATS_SINGLE_SURVIVOR : zm_net_self();
}

void zm_stats_new_game(void)
{
    memset(s_surv, 0, sizeof(s_surv));
    memset(&s_director, 0, sizeof(s_director));
    s_directorHave = false;
    s_over = false;
    s_reason = ZM_END_ESCAPE;
    s_winner = -1;
    s_matchSec = 0;
    s_myHits = s_myKills = s_myDamage = 0;
    s_myDeathSec = -1;
    s_myLastHealthHave = false;
}

void zm_stats_revived(void)
{
    s_myDeathSec = -1;
    s_myLastHealthHave = false;
}

// Once a frame (zombie_mode_net_frame): the damage this copy's survivor took
// and when it died. `elapsedMs`: the match clock, 0 until it starts.
void zm_stats_frame(unsigned int elapsedMs)
{
    if (!zm_stats_has_survivor() || s_over) return;
    short h = g_playerEntity.health;
    if (elapsedMs == 0) {
        // Before the clock starts the perks set the health: no damage.
        s_myLastHealthHave = false;
        return;
    }
    if (s_myLastHealthHave && h < s_myLastHealth && s_myLastHealth >= 0) {
        // Down to the death, not past it.
        s_myDamage += s_myLastHealth - (h < 0 ? 0 : h);
    }
    if (h < 0 && s_myDeathSec < 0) {
        s_myDeathSec = (int)(elapsedMs / 1000);
        dbg_printf("[stats] this survivor died at %d s\n", s_myDeathSec);
    }
    s_myLastHealth = h;
    s_myLastHealthHave = true;
}

// apply_weapon_damage, after a hit on `enemy` (WeaponDamage.cpp): this copy's
// survivor landed it; a kill when it took the monster's health below zero.
void zombie_mode_on_weapon_hit(const Entity* enemy, short healthBefore)
{
    if (!zombie_mode_armed() || !zm_stats_has_survivor() || s_over || enemy == NULL) return;
    s_myHits++;
    if (healthBefore >= 0 && enemy->health < 0) s_myKills++;
}

static void zm_stats_fill_mine(void)
{
    if (zm_stats_has_survivor()) {
        ZmSurvivorStats& m = s_surv[zm_stats_self()];
        m.have = true;
        m.deathSec = s_myDeathSec;
        m.hits = s_myHits;
        m.kills = s_myKills;
        m.damage = s_myDamage;
    }
    if (zm_stats_has_director()) {
        zm_econ_stats(&s_director);
        s_directorHave = true;
    }
}

// The match is over (every copy, once): this copy's numbers, kept and sent.
void zm_stats_match_over(int reason, int winner, unsigned int elapsedMs)
{
    if (s_over) return;
    s_over = true;
    s_reason = reason;
    s_winner = winner;
    s_matchSec = (int)(elapsedMs / 1000);
    zm_stats_fill_mine();
    if (zm_game_role() == ZM_NET_SURVIVOR) {
        const ZmSurvivorStats& m = s_surv[zm_net_self()];
        zm_net_send_event8(ZM_EV_STATS, 0, (short)m.deathSec, (short)m.hits, (short)m.kills,
                           (short)m.damage, 0, 0, 0);
    }
    if (zm_game_role() != ZM_NET_OFF && zm_stats_has_director()) {
        const ZmEconStats& d = s_director;
        zm_net_send_event8(ZM_EV_STATS, 1, (short)(unsigned short)d.spent, (short)(unsigned short)d.earned,
                           (short)d.placed, (short)d.trapsSet, (short)d.lost, (short)d.survivorHits, 0);
    }
    dbg_printf("[stats] match over: reason %d, winner %d, %d s\n", reason, winner, s_matchSec);
}

// ZM_EV_STATS from another copy: { 0 survivor, death s, hits, kills, damage }
// or { 1 director, spent, earned, placed, traps, lost, survivor hits }.
void zm_stats_take(const short* a, int src)
{
    if (a[0] == 1) {
        s_director.spent = (unsigned short)a[1];
        s_director.earned = (unsigned short)a[2];
        s_director.placed = a[3];
        s_director.trapsSet = a[4];
        s_director.lost = a[5];
        s_director.survivorHits = a[6];
        s_directorHave = true;
    } else if (src >= 0 && src < ZM_NET_MAX_PLAYERS && zm_net_char(src) >= 0) {
        ZmSurvivorStats& m = s_surv[src];
        m.have = true;
        m.deathSec = a[1];
        m.hits = a[2];
        m.kills = a[3];
        m.damage = a[4];
    }
    dbg_printf("[stats] from player %d: %s\n", src, a[0] == 1 ? "director" : "survivor");
}

// ---------------------------------------------------------------------------
// The screen
// ---------------------------------------------------------------------------
#define ZM_END_FADE_MS   1500      // to white
#define ZM_END_TEXT_MS   1200      // the result, once the white is nearly full
#define ZM_END_PROMPT_MS 4000      // "PRESS ACTION", and the press counts

// Everything below sits in the last overlay pass (depth under 500, far to
// near): the white at 490, the stats panel at 486, the text (PrintText8x14 at
// default brightness, 482) over both.
static void zm_end_rect(int x, int y, int w, int h, unsigned int textureId, unsigned char level, int blend)
{
    RectDrawDesc r;
    memset(&r, 0, sizeof(r));
    r.textureId = textureId;
    r.x = (short)(x - g_ScreenOffsetX);
    r.y = (short)(y - g_ScreenOffsetY);
    r.w = (short)w;
    r.h = (short)h;
    r.r = r.g = r.b = level;
    draw_rect(&r, blend, 0);
}

// Text colours (the font's CLUT tints): 0 white, 1 green, 2 red, 3 grey, 4 yellow.
static void zm_end_text(int x, int y, unsigned char color, const char* text)
{
    snprintf(PRINT_TEXT_BUFFER, sizeof(PRINT_TEXT_BUFFER), "%s", text);
    zm_text_encode(PRINT_TEXT_BUFFER);
    PrintText8x14((short)x, (short)y, color, 0);
}

static void zm_end_centered(int y, unsigned char color, const char* text)
{
    zm_end_text((320 - (int)strlen(text) * 8) / 2, y, color, text);
}

static void zm_mmss(char* out, int len, int sec)
{
    if (sec < 0) sec = 0;
    snprintf(out, len, "%d:%02d", sec / 60, sec % 60);
}

// Did this copy's side win?
static bool zm_end_won(void)
{
    bool survivorsWon = s_reason == ZM_END_ESCAPE;
    return zm_game_role() == ZM_NET_SURVIVOR ? survivorsWon : !survivorsWon;
}

bool zm_stats_prompt_up(unsigned int sinceMs)
{
    return sinceMs >= ZM_END_PROMPT_MS;
}

void zm_stats_draw(unsigned int sinceMs)
{
    // The fade to white, then held.
    unsigned int level = sinceMs >= ZM_END_FADE_MS ? 255 : sinceMs * 255 / ZM_END_FADE_MS;
    if (level == 0) level = 1;
    // Over Wesker's frozen frame (ZombieBumpers.cpp) there is no white.
    if (!zm_bumpers_backdrop()) zm_end_rect(0, 0, 320, 240, 0x50000000u, (unsigned char)level, 40);
    if (sinceMs < ZM_END_TEXT_MS) return;

    char line[48], t[16];
    bool won = zm_end_won();
    zm_end_centered(14, 2, won ? "YOU WIN!" : zm_game_role() == ZM_NET_SURVIVOR ? "GAME OVER" : "YOU LOSE");
    if (s_reason == ZM_END_ESCAPE) {
        int ch = s_winner >= 0 ? zm_net_char(s_winner) : -1;
        if (zm_game_role() == ZM_NET_OFF) ch = -1;
        snprintf(line, sizeof(line), "%s ESCAPED THE MANSION", ch >= 0 ? zm_char_name(ch) : "A SURVIVOR");
    } else if (s_reason == ZM_END_TIME) {
        snprintf(line, sizeof(line), "TIME IS UP - THE DIRECTOR WINS");
    } else if (s_reason == ZM_END_UNSOLVABLE) {
        snprintf(line, sizeof(line), "ESCAPE IMPOSSIBLE - KEY ITEMS LOST");
    } else if (s_reason == ZM_END_CONNECTION) {
        snprintf(line, sizeof(line), "CONNECTION LOST - RECONNECT TIME EXPIRED");
    } else {
        snprintf(line, sizeof(line), "ALL SURVIVORS ARE DEAD");
    }
    zm_end_centered(32, 2, line);

    // The stats on a dark panel.
    zm_end_rect(12, 52, 296, 166, 0x60000000u, 210, 36);
    zm_mmss(t, sizeof(t), s_matchSec);
    snprintf(line, sizeof(line), "MATCH TIME %s", t);
    zm_end_text(20, 58, 4, line);

    zm_end_text(20, 78, 4, "DIRECTOR");
    if (s_directorHave) {
        snprintf(line, sizeof(line), "POINTS SPENT %d  EARNED %d", s_director.spent, s_director.earned);
        zm_end_text(28, 94, 0, line);
        snprintf(line, sizeof(line), "MONSTERS %d  LOST %d  TRAPS %d", s_director.placed, s_director.lost,
                 s_director.trapsSet);
        zm_end_text(28, 108, 0, line);
        snprintf(line, sizeof(line), "HITS ON SURVIVORS %d", s_director.survivorHits);
        zm_end_text(28, 122, 0, line);
    } else {
        zm_end_text(28, 94, 3, "WAITING FOR THE DIRECTOR...");
    }

    zm_end_text(20, 142, 4, "SURVIVOR  FATE        HITS KILLS DMG");
    int y = 158;
    for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
        int ch;
        if (zm_game_role() == ZM_NET_OFF) {
            if (i != ZM_STATS_SINGLE_SURVIVOR) continue;
            ZmSurvivorInfo surv[1];
            ch = zm_survivor_list(surv, 1) > 0 ? surv[0].character : ZM_CHAR_CHRIS;
        } else {
            ch = zm_net_char(i);
            if (ch < 0) continue;
        }
        const ZmSurvivorStats& m = s_surv[i];
        const char* name = zm_char_name(ch);
        if (!m.have) {
            bool here = i == zm_net_self() || zm_net_player(i) != NULL;
            snprintf(line, sizeof(line), "%-9s %s", name, here ? "..." : "LEFT THE GAME");
            zm_end_text(20, y, 3, line);
        } else {
            char fate[16];
            unsigned char color = 0;
            if (s_reason == ZM_END_ESCAPE && i == s_winner) {
                snprintf(fate, sizeof(fate), "ESCAPED");
                color = 1;
            } else if (m.deathSec >= 0) {
                zm_mmss(t, sizeof(t), m.deathSec);
                snprintf(fate, sizeof(fate), "DIED %s", t);
                color = 2;
            } else {
                snprintf(fate, sizeof(fate), "ALIVE");
            }
            snprintf(line, sizeof(line), "%-9s %-11s %4d %5d %3d", name, fate, m.hits, m.kills, m.damage);
            zm_end_text(20, y, color, line);
        }
        y += 16;
    }

    if (zm_stats_prompt_up(sinceMs)) zm_end_centered(222, 2, "PRESS ACTION TO CONTINUE");
}
