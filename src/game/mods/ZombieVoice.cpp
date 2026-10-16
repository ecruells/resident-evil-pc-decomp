// ZombieVoice.cpp - the survivors' voice lines (zombie mod).
//
// A survivor holds MOD1 (pad LB, keyboard Shift - PAD_MOD1, InputSystem.cpp)
// and presses a direction or a face button:
//   up HELP, down WATCH OUT, left GUARD ME, right LEAD THE WAY;
//   aim (pad X) FOLLOW ME, inventory (Y) STAY HERE, action (A) USE THAT,
//   run (B) RUN AWAY.
// MOD2 (RB / Tab): up JOKE, down GOOD JOB, left I NEED AMMO,
// right I NEED HEALTH (or hurt/dying), aim (X) TAKE THIS, action (A) THANKS,
// run (B) the taunt, inventory (Y) WE'RE SCREWED.
// Jokes cycle in order.
// The face buttons are read as the functions they are bound to (the raw pad
// word has no physical buttons), so the keyboard's aim / menu / action / run
// keys say the same lines. While MOD1 is held the directions are kept from
// gameplay (zombie_mode_pad_filter, called from PlayerPad_Update), so the
// survivor stands still instead of walking; so are those four - except one
// already held when MOD1 went down (aiming with RT), which keeps working.
//
// Each character has its own recordings, any number of takes a line:
//   <exe dir>/infested/voices/<character>/<line>_<n>.wav   (n from 1; .ogg too)
// <character> is chris, jill, barry, rebecca, richard or enrico.
// <line> categories are listed in kLineFile below. Joke takes play in order,
// wrapping back to 1.
// Takes are counted on first use
// (1, 2, ... up to the first missing one). The speaker picks the take and
// sends it, so every copy plays the same recording. A missing file plays
// nothing; the caption still shows.
//
// ZM_EV_VOICE { line, take, character, stage | room << 8, x, y, z, 0 },
// survivor -> all. Who hears it is each copy's own call, by the room it has
// loaded (a spectator's is the watched survivor's):
//   - the speaker's room: the recording as is, panned from the speaker;
//   - a room with a door to the speaker's: muffled (AudioFile_Muffle, the
//     shotgun trap's filter) and quieter, panned from that door;
//   - anywhere else: nothing.
// The director's copy hears by the same rule - it can listen in.
//
// The music ducks under a line this copy hears (10 dB in the room, 5 dB
// through a door) - the cutscenes, which the voices were recorded for, play
// with little or no music. The room's (g_BgmSoundBank, g_SndBank[0..2]), the
// Yawn fight's and the piano's; voice_duck_frame below.
//
// One sound bank per speaker, loaded when its line starts and freed when it
// ends or the next one cuts it: the Windows backend has 80 banks for the
// whole game, so the recordings are never preloaded.
#include "../../Globals.h"
#include "../../platform/platform.h"
#include "../../marni/MarniSound.h"
#include "../../system/AudioFile.h"
#include "ZombieMode.h"
#include "ZombieModeInternal.h"
#include "ZombieNet.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ZM_VOICE_LINES       19
#define ZM_VOICE_JOKE        8
#define ZM_VOICE_HURT        14
#define ZM_VOICE_DYING       15
#define ZM_VOICE_MAX_TAKES   16
#define ZM_VOICE_COOLDOWN_MS 1500     // between two lines of one survivor
// Through a door the low-pass already takes most of the voice's energy;
// 18 dB on top of it (the shotgun trap's figure) left the line inaudible.
#define ZM_VOICE_MUFFLE_DB   600      // hundredths of a dB off through a door
#define ZM_VOICE_DUCK        1000     // the music under a line in the room
#define ZM_VOICE_DUCK_MUFFLED 800     // ... and under one through a door
#define ZM_VOICE_DUCK_DOWN   250      // a frame: down in 4 frames
#define ZM_VOICE_DUCK_UP     70       // a frame: back up in about half a second
#define ZM_VOICE_DUCK_BANKS  6
#define ZM_VOICE_UNSET       0x7FFFFFFF
// The raw pad word's directions (g_button_pressed_id), as the lobby reads them.
#define VOICE_UP    0x1000
#define VOICE_DOWN  0x4000
#define VOICE_LEFT  0x8000
#define VOICE_RIGHT 0x2000
#define VOICE_DIRS  (VOICE_UP | VOICE_DOWN | VOICE_LEFT | VOICE_RIGHT)
// ... and its functions (the default pad buttons: X, Y, A, B).
#define VOICE_AIM       0x0008
#define VOICE_INVENTORY 0x0800      // OPTIONS is 0x0900: not this alone
#define VOICE_OPTIONS_BIT 0x0100
#define VOICE_ACTION    0x0080
#define VOICE_RUN       0x0040
#define VOICE_FACE  (VOICE_AIM | VOICE_INVENTORY | VOICE_ACTION | VOICE_RUN)

// Slot 5 is retired; keep the remaining network line IDs stable.
static const char* const kLineFile[ZM_VOICE_LINES] = {
    "help", "takethis", "needammo", "needhealth",
    "followme", NULL, "watchout", "taunt", "joke",
    "goodjob", "guardme", "leadtheway", "runaway", "stayhere",
    "hurt", "dying", "thanks", "usethat", "werescrewed",
};
static const char* const kLineText[ZM_VOICE_LINES] = {
    "HELP!", "TAKE THIS!", "I NEED AMMO!", "I NEED HEALTH!",
    "FOLLOW ME!", NULL, "WATCH OUT!", "COME AND GET ME!", "JOKE",
    "GOOD JOB!", "GUARD ME!", "LEAD THE WAY!", "RUN AWAY!", "STAY HERE!",
    "I'M HURT!", "I'M DYING!", "THANKS!", "USE THAT!", "WE'RE SCREWED!",
};

static int voice_health_line(void)
{
    // Same red/Danger band as menu_draw_health_bar: bucket zero in
    // (health - 1) / (maxHealth >> 2), including zero HP.
    int health = g_playerEntity.health;
    int maximum = g_playerEntity.maxHealth;
    if (maximum > 0 && health <= (maximum >> 2)) return ZM_VOICE_DYING;
    if (health < maximum) return ZM_VOICE_HURT;
    return 3; // full health: the existing needhealth category
}

static signed char s_takes[ZM_CHAR_COUNT][ZM_VOICE_LINES];   // -1: not counted yet
static unsigned char s_nextJoke[ZM_CHAR_COUNT];             // last joke take played
static int          s_bank[ZM_NET_MAX_PLAYERS];              // the speaker's playing line
static unsigned int s_bankStart[ZM_NET_MAX_PLAYERS];
static bool         s_bankMuffled[ZM_NET_MAX_PLAYERS];
// A music bank under the duck: the volume its owner wants (`base`) and the
// one this file last set (`written`). A volume that is not ours is the
// owner's new one - a fade, a new track - and becomes the base.
struct VoiceDuck { int handle, base, written; };
static VoiceDuck    s_duck[ZM_VOICE_DUCK_BANKS];
static int          s_duckNow;                               // hundredths of a dB
static int          s_pendingLine = -1;                      // pressed, not yet said
static unsigned int s_lastSaid;
static bool         s_saidOnce;
static unsigned int s_rand;
static bool         s_modWas;                                // MOD1 held last frame
static DWORD        s_passHeld;                              // face buttons held before MOD1

static void voice_char_dir(int ch, char* out, int size)
{
    const char* name = zm_char_name(ch);
    int i = 0;
    for (; name[i] != '\0' && i < size - 1; i++) {
        char c = name[i];
        out[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    out[i] = '\0';
}

static bool voice_path(int ch, int line, int take, char* out, int size)
{
    if (line < 0 || line >= ZM_VOICE_LINES || kLineFile[line] == NULL) return false;
    char exeDir[1024], name[16];
    if (!plat_exe_dir(exeDir, sizeof(exeDir))) return false;
    voice_char_dir(ch, name, sizeof(name));
    snprintf(out, size, "%s/infested/voices/%s/%s_%d.wav", exeDir, name, kLineFile[line], take);
    return true;
}

static int voice_take_count(int ch, int line)
{
    if (ch < 0 || ch >= ZM_CHAR_COUNT) return 0;
    if (s_takes[ch][line] < 0) {
        int n = 0;
        char path[1200], found[1200];
        while (n < ZM_VOICE_MAX_TAKES && voice_path(ch, line, n + 1, path, sizeof(path)) &&
               AudioFile_Find(path, found, sizeof(found))) n++;
        s_takes[ch][line] = (signed char)n;
        dbg_printf("[voice] %s %s: %d take(s)\n", zm_char_name(ch), kLineFile[line], n);
    }
    return s_takes[ch][line];
}

static void voice_stop(int player)
{
    if (s_bank[player] == 0) return;
    setSndStop(s_bank[player]);
    destroySndBank(s_bank[player]);
    s_bank[player] = 0;
}

void zm_voice_reset(void)
{
    memset(s_takes, -1, sizeof(s_takes));
    memset(s_nextJoke, 0, sizeof(s_nextJoke));
    for (int p = 0; p < ZM_NET_MAX_PLAYERS; p++) voice_stop(p);
    s_pendingLine = -1;
    s_saidOnce = false;
    s_modWas = false;
    s_passHeld = 0;
    for (int i = 0; i < ZM_VOICE_DUCK_BANKS; i++) {
        VoiceDuck& d = s_duck[i];
        if (d.handle != 0 && d.written != ZM_VOICE_UNSET && getSndVol(d.handle) == d.written)
            set_volume(d.handle, d.base);
        d.handle = 0;
        d.written = ZM_VOICE_UNSET;
    }
    s_duckNow = 0;
}

// The door of this copy's room that leads to (stage, room), nearest the
// speaker's spot there; its middle in this room's coordinates. Failing that,
// a door of the speaker's room into this one: where it brings a character in.
static bool voice_door_between(unsigned char stage, unsigned char room, int sx, int sz, VECTOR* at)
{
    const ZmDoor* doors;
    int n = zm_room_doors(g_stageId, g_roomId, &doors);
    int best = -1, bestDist = 0x7FFFFFFF;
    for (int i = 0; i < n; i++) {
        if (doors[i].flags0B & 0x80) continue;          // camera-only
        unsigned char ds, dr;
        zm_decode_dest(doors[i].dest, g_stageId, &ds, &dr);
        if (ds != stage || dr != room) continue;
        int d = abs(doors[i].arriveX - sx) + abs(doors[i].arriveZ - sz);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    if (best >= 0) {
        at->x = doors[best].zoneX + doors[best].zoneW / 2;
        at->z = doors[best].zoneZ + doors[best].zoneD / 2;
        return true;
    }
    n = zm_room_doors(stage, room, &doors);
    for (int i = 0; i < n; i++) {
        if (doors[i].flags0B & 0x80) continue;
        unsigned char ds, dr;
        zm_decode_dest(doors[i].dest, stage, &ds, &dr);
        if (ds != g_stageId || dr != g_roomId) continue;
        int d = abs(doors[i].zoneX + doors[i].zoneW / 2 - sx) + abs(doors[i].zoneZ + doors[i].zoneD / 2 - sz);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    if (best < 0) return false;
    at->x = doors[best].arriveX;
    at->z = doors[best].arriveZ;
    return true;
}

// Play `player`'s line on this copy, if it is in earshot.
static void voice_play(int player, int ch, int line, int take, short where, int x, int y, int z)
{
    if (player < 0 || player >= ZM_NET_MAX_PLAYERS || line < 0 || line >= ZM_VOICE_LINES) return;
    if (kLineFile[line] == NULL) return;
    unsigned char stage = (unsigned char)(where & 0xFF), room = (unsigned char)((unsigned short)where >> 8);
    VECTOR at;
    at.x = x; at.y = y; at.z = z;
    bool here = stage == g_stageId && room == g_roomId;
    bool muffled = false;
    if (!here) {
        if (!voice_door_between(stage, room, x, z, &at)) {
            dbg_printf("[voice] p%d in %d:%02X not heard here (%d:%02X): no door between\n",
                       player, stage + 1, room, g_stageId + 1, g_roomId);
            return;
        }
        muffled = true;
        dbg_printf("[voice] p%d in %d:%02X heard through the door at (%d, %d)\n",
                   player, stage + 1, room, (int)at.x, (int)at.z);
    }
    char caption[64];
    snprintf(caption, sizeof(caption), muffled ? "%s - NEXT ROOM: %s" : "%s: %s",
             zm_char_name(ch), kLineText[line]);
    zm_note(caption);
    dbg_printf("[voice] p%d %s %s take %d%s\n", player, zm_char_name(ch), kLineFile[line], take,
               muffled ? " (muffled)" : "");

    voice_stop(player);                                 // the new line cuts the last
    char path[1200];
    if (take < 1 || ch < 0 || ch >= ZM_CHAR_COUNT || !voice_path(ch, line, take, path, sizeof(path))) return;
    int bank = loadSndBankFromWav(path, muffled);
    if (bank == 0) {
        dbg_printf("[voice] %s failed to load\n", path);
        return;
    }
    Calc3DSndPan(&at);
    pan_set(bank, ((int)g_snd_pan_right - (int)g_snd_pan_left) * 0x4E);
    set_volume(bank, g_SfxVolume - (muffled ? ZM_VOICE_MUFFLE_DB : 0));
    SetSndSlot(bank, 0);
    s_bank[player] = bank;
    s_bankStart[player] = zm_game_time_ms();
    s_bankMuffled[player] = muffled;
}

// Every frame, after the music's owners set theirs: ease the duck toward
// what the lines playing want and put it on every music bank.
static void voice_duck_frame(void)
{
    int target = 0;
    for (int p = 0; p < ZM_NET_MAX_PLAYERS; p++) {
        if (s_bank[p] == 0) continue;
        int want = s_bankMuffled[p] ? ZM_VOICE_DUCK_MUFFLED : ZM_VOICE_DUCK;
        if (want > target) target = want;
    }
    if (s_duckNow < target) {
        s_duckNow += ZM_VOICE_DUCK_DOWN;
        if (s_duckNow > target) s_duckNow = target;
    } else if (s_duckNow > target) {
        s_duckNow -= ZM_VOICE_DUCK_UP;
        if (s_duckNow < target) s_duckNow = target;
    }
    int handles[ZM_VOICE_DUCK_BANKS] = {
        g_BgmSoundBank, g_SndBank[0].handle, g_SndBank[1].handle, g_SndBank[2].handle,
        zm_yawn_music_bank(), zm_piano_music_bank(),
    };
    for (int i = 0; i < ZM_VOICE_DUCK_BANKS; i++) {
        VoiceDuck& d = s_duck[i];
        int h = handles[i];
        for (int j = 0; j < i; j++) if (handles[j] == h) h = 0;    // one bank, one duck
        if (h != d.handle) {                    // another bank there now: its volume is its own
            d.handle = h;
            d.written = ZM_VOICE_UNSET;
        }
        if (h == 0) continue;
        int v = getSndVol(h);
        if (v > 0) continue;                    // 2000: no volume set yet (apply_vol_delta)
        if (v != d.written) d.base = v;
        if (s_duckNow == 0) {
            if (d.written != ZM_VOICE_UNSET) {
                if (v == d.written) set_volume(h, d.base);
                d.written = ZM_VOICE_UNSET;     // left alone until the next line
            }
            continue;
        }
        set_volume(h, d.base - s_duckNow);
        d.written = getSndVol(h);               // as the backend keeps it (clamped)
    }
}

// PlayerPad_Update, after its edge detection (InputSystem.cpp): while MOD1 is
// held on a living survivor's copy, the directions are this file's - a fresh
// press picks a line - and are taken out of the pad. Returns the raw bits to
// drop (0 when the pad is left alone).
DWORD zombie_mode_pad_filter(DWORD rawHeld, DWORD rawPressed)
{
    bool mod = (rawHeld & PAD_MOD1) != 0;
    bool social = (rawHeld & PAD_MOD2) != 0;
    bool modDown = (mod || social) && !s_modWas;
    s_modWas = mod || social;
    if (zm_bumpers_hold_input()) return 0xFFFFFFFFu;   // a match bumper is playing
    if (!zombie_mode_armed() || zm_game_role() != ZM_NET_SURVIVOR) return 0;
    if (!mod && !social) return 0;
    // What was already held when MOD1 went down stays the game's until it
    // is let go.
    if (modDown) s_passHeld = rawHeld & VOICE_FACE;
    s_passHeld &= rawHeld;
    if (zm_map_is_open() || g_playerEntity.health < 0 || zombie_mode_match_over()) return 0;
    DWORD fresh = rawPressed & ~s_passHeld;
    if (social) {
        if (s_pendingLine < 0) {
            if ((fresh & VOICE_INVENTORY) && (rawHeld & VOICE_OPTIONS_BIT) == 0)
                s_pendingLine = 18;
            else if (fresh & VOICE_UP) s_pendingLine = ZM_VOICE_JOKE;
            else if (fresh & VOICE_DOWN) s_pendingLine = 9;
            else if (fresh & VOICE_LEFT) s_pendingLine = 2;
            else if (fresh & VOICE_RIGHT) s_pendingLine = 3;
            else if (fresh & VOICE_AIM) s_pendingLine = 1;
            else if (fresh & VOICE_ACTION) s_pendingLine = 16;
            else if (fresh & VOICE_RUN) s_pendingLine = 7;
        }
        return VOICE_DIRS | (VOICE_FACE & ~s_passHeld);
    }
    if (s_pendingLine < 0) {
        if (fresh & VOICE_UP)               s_pendingLine = 0;
        else if (fresh & VOICE_DOWN)        s_pendingLine = 6;
        else if (fresh & VOICE_LEFT)        s_pendingLine = 10;
        else if (fresh & VOICE_RIGHT)       s_pendingLine = 11;
        else if (fresh & VOICE_AIM)         s_pendingLine = 4;
        else if ((fresh & VOICE_INVENTORY) && (rawHeld & VOICE_OPTIONS_BIT) == 0) s_pendingLine = 13;
        else if (fresh & VOICE_ACTION)      s_pendingLine = 17;
        else if (fresh & VOICE_RUN)         s_pendingLine = 12;
    }
    return VOICE_DIRS | (VOICE_FACE & ~s_passHeld);
}

// zombie_mode_net_frame: say a picked line, free the banks whose lines ended.
void zm_voice_frame(void)
{
    unsigned int now = zm_game_time_ms();
    for (int p = 0; p < ZM_NET_MAX_PLAYERS; p++) {
        if (s_bank[p] != 0 && now - s_bankStart[p] > 300 && getSndStat(s_bank[p]) != 1) voice_stop(p);
    }
    voice_duck_frame();
    int line = s_pendingLine;
    s_pendingLine = -1;
    if (line < 0) return;
    if (s_saidOnce && now - s_lastSaid < ZM_VOICE_COOLDOWN_MS) return;
    int self = zm_net_self();
    int ch = zm_net_char(self);
    if (self < 0 || ch < 0) return;
    // Resolve on the speaker, then send the chosen subcategory and take so
    // listeners do not depend on a delayed health snapshot.
    if (line == 3) line = voice_health_line();
    s_saidOnce = true;
    s_lastSaid = now;
    int takes = voice_take_count(ch, line);
    int take = 0;
    if (takes > 0) {
        if (line == ZM_VOICE_JOKE) {
            take = 1 + s_nextJoke[ch] % takes;
            s_nextJoke[ch] = (unsigned char)take;
        } else {
            s_rand = s_rand * 1103515245u + 12345u + now;    // our own: the game's rand is shared
            take = 1 + (int)((s_rand >> 16) % (unsigned int)takes);
        }
    }
    const MATRIX& m = g_playerEntity.scaMatrixData.localMatrix;
    short where = (short)(g_stageId | (g_roomId << 8));
    voice_play(self, ch, line, take, where, m.t[0], m.t[1], m.t[2]);
    if (zm_net_active()) {
        zm_net_send_event8(ZM_EV_VOICE, (short)line, (short)take, (short)ch, where,
                           (short)m.t[0], (short)m.t[1], (short)m.t[2], 0);
    }
}

void zm_voice_take(const short* a, int src)
{
    if (src == zm_net_self()) return;
    voice_play(src, a[2], a[0], a[1], a[3], a[4], a[5], a[6]);
}

static void voice_hint_text(int cx, int y, const char* text, unsigned char color)
{
    snprintf(PRINT_TEXT_BUFFER, sizeof(PRINT_TEXT_BUFFER), "%s", text);
    zm_text_encode(PRINT_TEXT_BUFFER);
    PrintText8x14((short)(cx - (int)strlen(text) * 4), (short)y, color, 0);
}

static void voice_hint_rect(int x, int y, int w, int h, unsigned char level, int depth, unsigned int texture = 0)
{
    RectDrawDesc r;
    memset(&r, 0, sizeof(r));
    r.textureId = texture;
    r.x = (short)(x - g_ScreenOffsetX);
    r.y = (short)(y - g_ScreenOffsetY);
    r.w = (short)w;
    r.h = (short)h;
    r.r = r.g = r.b = level;
    draw_rect(&r, depth, 0);
}

static void voice_hint_round_rect(int x, int y, int w, int h, unsigned char level,
                                  int depth, unsigned int texture = 0)
{
    // Small rounded corners built from the same native rectangles as the HUD.
    voice_hint_rect(x + 4, y, w - 8, 1, level, depth, texture);
    voice_hint_rect(x + 2, y + 1, w - 4, 2, level, depth, texture);
    voice_hint_rect(x + 1, y + 3, w - 2, 1, level, depth, texture);
    voice_hint_rect(x, y + 4, w, h - 8, level, depth, texture);
    voice_hint_rect(x + 1, y + h - 4, w - 2, 1, level, depth, texture);
    voice_hint_rect(x + 2, y + h - 3, w - 4, 2, level, depth, texture);
    voice_hint_rect(x + 4, y + h - 1, w - 8, 1, level, depth, texture);
}

static bool voice_hint_held(DWORD rawBit)
{
    // OPTIONS includes the inventory bit (0x0900). It is not the Y shortcut.
    if (rawBit == VOICE_INVENTORY && (g_RawPadHeld & VOICE_OPTIONS_BIT) != 0)
        return false;
    return (g_RawPadHeld & rawBit) != 0;
}

static void voice_hint_card(int x, int y, int width, const char* key,
                            DWORD rawBit, const char* label)
{
    bool held = voice_hint_held(rawBit);
    voice_hint_round_rect(x, y, width, 24, held ? 64 : 32, 38);
    voice_hint_round_rect(x + 4, y + 3, 24, 18, held ? 160 : 64, 36);
    voice_hint_text(x + 16, y + 5, key, held ? 4 : 0);
    voice_hint_text(x + 30 + (width - 30) / 2, y + 5, label, 0);
}

// Native font and primitives: evenly spaced rows with rounded key badges.
// Read the unfiltered pad, since voice chords are removed from play.
void zm_voice_draw(void)
{
    if (!zombie_mode_armed() ||
        (zm_game_role() == ZM_NET_SURVIVOR && g_playerEntity.health < 0) ||
        zombie_mode_match_over() || zm_map_is_open() ||
        (g_main_state_flags & (MSF_MENU_ACTIVE | MSF_DOOR_TRANSITION)) != 0 || g_openMenuFlag != 0) return;
    bool lb = (g_RawPadHeld & PAD_MOD1) != 0;
    bool rb = (g_RawPadHeld & PAD_MOD2) != 0;
    if (!lb && !rb) return;

    if (zm_game_role() != ZM_NET_SURVIVOR) {
        if (!lb || g_zombieModeEntity == NULL) return;
        voice_hint_round_rect(48, 44, 224, 144, 176, 40, 0x60000000u);
        voice_hint_text(160, 50, "LB - SPAWN", 0);
        voice_hint_card(56, 70, 208, "U", VOICE_UP, "ZOMBIE");
        voice_hint_card(56, 98, 208, "L", VOICE_LEFT, "HUNTER");
        voice_hint_card(56, 126, 208, "R", VOICE_RIGHT, "CHIMERA");
        voice_hint_card(56, 154, 208, "D", VOICE_DOWN, "DOG");
        return;
    }

    voice_hint_round_rect(8, 44, 304, 144, 176, 40, 0x60000000u);
    voice_hint_text(160, 50, rb ? "RB - SUPPLIES / SOCIAL" : "LB - TEAMWORK", 0);
    int healthLine = voice_health_line();
    const char* health = healthLine == ZM_VOICE_DYING ? "I'M DYING" :
                         healthLine == ZM_VOICE_HURT ? "I'M HURT" : "NEED HEALTH";
    voice_hint_card(16, 70, 140, "U", VOICE_UP, rb ? "JOKE" : "HELP");
    voice_hint_card(16, 98, 140, "L", VOICE_LEFT, rb ? "NEED AMMO" : "GUARD ME");
    voice_hint_card(16, 126, 140, "R", VOICE_RIGHT, rb ? health : "LEAD THE WAY");
    voice_hint_card(16, 154, 140, "D", VOICE_DOWN, rb ? "GOOD JOB" : "WATCH OUT");
    voice_hint_card(164, 70, 140, "Y", VOICE_INVENTORY, rb ? "WE'RE SCREWED" : "STAY HERE");
    voice_hint_card(164, 98, 140, "X", VOICE_AIM, rb ? "TAKE THIS" : "FOLLOW ME");
    voice_hint_card(164, 126, 140, "B", VOICE_RUN, rb ? "TAUNT" : "RUN AWAY");
    voice_hint_card(164, 154, 140, "A", VOICE_ACTION, rb ? "THANKS" : "USE THAT");
}
