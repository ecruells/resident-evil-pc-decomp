#include "ZombieModeInternal.h"
#include "ZombieNet.h"
#include "../PrintText.h"
#include "../FileLoader.h"
#include "../../marni/MarniSound.h"
#include "../../system/AssetPath.h"
#include "../../DebugPrint.h"
#include <cstring>
#include <cstdio>
#include <cmath>

// ============================================================================
// ZombiePiano.cpp - port-added: the bar's piano puzzle (multiplayer survivors).
//
// The piano bar's alcove (ROOM60F0, stage 6 room 0x0F) holds one of the
// puzzles' heavy weapons (ZombieRandom.cpp kPuzzles) behind its sliding wall.
// Jill, Rebecca or Richard USE the sheet music at the piano and play for
// ZM_PIANO_MS; the director is told (ZM_EV_PIANO) and its monsters can stop
// them: a hit, a grab, moving, aiming or leaving the room ends the playing
// and the next try starts from zero. The sheet music is kept until the tune
// is finished; opening the inventory or the map stops it too. Then the host
// opens the wall for every copy.
//
// The open wall is the original's own state: ScenarioFlags2 bit 0xA2 makes the
// room's init put the wall away (events 0x0C / 0x0E) on every entry, the
// story-flag sync and the reconnect snapshot (g_BioCard) carry it, and a copy
// in the bar when it opens plays event 9 - the wall sinking, with its sound.
// The tune plays on every copy while anyone plays, wherever it is.
// The original's voiced Rebecca scenes (events 1, 2, 0x11) never start: the
// item menu hands the sheet music to zm_piano_use instead.
// ============================================================================

extern void Flg_on(int baseAddr, unsigned int bitIndex);
extern void ScdEventEntry_Create(unsigned int slot, int scriptIndex);   // RoomEvents.cpp

#define ZM_PIANO_MS        15000u
#define ZM_PIANO_OPEN_FLAG 0xA2           // ScenarioFlags2: the alcove's wall is down
#define ZM_PIANO_WALL_EVENT 9             // ROOM60F0: the wall sinks into the floor
// The tune: the first visit's bar music group 0x0E has the whole piece on
// channel 1 - 24.5 s with its ending (bgm_11 / bgm_1f are the 15.4 s practice
// that breaks off). The return mansion's bar has no music, so the piano loads
// it itself and every copy plays it, wherever it is. It ends with the
// playing: faded out over ZM_PIANO_FADE_MS when the tune is finished, cut
// off when the pianist is stopped.
#define ZM_PIANO_MUSIC_WAV GAME_DATA_ROOT "sound\\bgm_2b.wav"
// The room's music is off while the tune plays, and comes back this long
// after it stops (unless someone starts again first).
#define ZM_PIANO_RESUME_MS 2000
#define ZM_PIANO_FADE_MS   1000
#define ZM_PIANO_FADE_TO   (-4000)        // set_volume's attenuation at the end of the fade
#define ZM_PIANO_MOVE      800            // units the pianist may drift (a shove) before it stops
// Jill's bar, ROOM60F1 event 6, where she plays: she walks to (9950, 8100),
// faces the keys at (14000, 8100) and plays her room's player animations
// (s1_pose_anim, completion flag SysFlags 0x20). The mode loads Chris's rooms
// (ROOM60F0), whose player only fumbles with one hand, so every pianist
// borrows Jill's animation data from her room file. Without it, Chris's own
// scene (ROOM60F0 event 1) is the fallback. Placed straight there, facing
// the keys, ZM_PIANO_RIGHT (about two feet) to the right of the event's spot
// and ZM_PIANO_FORWARD (about six inches) closer to the piano.
#define ZM_PIANO_JILL_RDT  GAME_DATA_ROOT "stage6\\room60f1.rdt"
#define ZM_PIANO_SEAT_X    9950
#define ZM_PIANO_SEAT_Z    8100
#define ZM_PIANO_FACE_X    14000
#define ZM_PIANO_FACE_Z    8100
#define ZM_PIANO_CHRIS_SEAT_X 9600
#define ZM_PIANO_CHRIS_SEAT_Z 7200
#define ZM_PIANO_CHRIS_FACE_X 12000
#define ZM_PIANO_CHRIS_FACE_Z 7500
#define ZM_PIANO_RIGHT     600
#define ZM_PIANO_FORWARD   150
// The spot is within a body's reach of the piano's boundary box (x 10127 on),
// which would push the pianist back out (to about 9690 - Chris 422, Jill 372
// radius). Bit 4 of the player's flags - set by room scripts for scripted
// moves - skips the room collision (check_room_collision) while it plays.
#define ZM_PIANO_NO_COLLISION 0x04
#define ZM_PIANO_ANIM_SIT  0x37
#define ZM_PIANO_ANIM_A    0x38
#define ZM_PIANO_ANIM_B    0x39
#define ZM_PIANO_ANIM_DONE 0x20

enum { PIANO_START = 1, PIANO_STOP = 2, PIANO_DONE = 3, PIANO_OPEN = 4, PIANO_FINISHED = 5 };

static constexpr auto s_notPianist = STR("You can't play the piano.");
static constexpr auto s_notHere = STR("Use this at the piano.");
static constexpr auto s_alreadyOpen = STR("The way is already open.");

static bool         s_playing;
static bool         s_pending;            // USE accepted: playing starts once the menu is closed
static bool         s_noCollision;        // the piano set ZM_PIANO_NO_COLLISION
static int          s_pose;               // the animation playing
static unsigned int s_playStart;
static int          s_startX, s_startZ;
static short        s_startHealth;
static bool         s_music;              // the tune is playing on this copy
static int          s_musicBank;          // its sound bank, loaded on first use
static unsigned int s_musicStart;
static unsigned int s_fadeAt;             // a finished tune is fading out since (0: not)
static bool         s_bgmHeld;            // the room's music was stopped for the tune
static unsigned int s_resumeAt;           // when it comes back (0: not counting)
static unsigned char s_jillRdt[700 * 1024]; // ROOM60F1 (480 KB), for its player animations
static int          s_jillState;          // 0 not tried, 1 loaded, -1 unavailable
static unsigned int s_jillHeader, s_jillBase;

// Jill's room's player animation pair (RDT +0x6C / +0x70, offsets from the
// file's start - the room loader adds the base the same way). False: none.
static bool piano_jill_anims(void)
{
    if (s_jillState == 0) {
        s_jillState = -1;
        size_t size = LoadFile(ZM_PIANO_JILL_RDT, s_jillRdt, 1);
        if (size != (size_t)-1 && size >= 0x100 && size <= sizeof(s_jillRdt)) {
            unsigned int header = *(const unsigned int*)(s_jillRdt + 0x6C);
            unsigned int base = *(const unsigned int*)(s_jillRdt + 0x70);
            if (header != 0 && base != 0 && header < size && base < size) {
                s_jillHeader = (unsigned int)(s_jillRdt + header);
                s_jillBase = (unsigned int)(s_jillRdt + base);
                s_jillState = 1;
            }
        }
        if (s_jillState < 0) dbg_printf("[piano] %s unavailable: Chris's piano animations\n", ZM_PIANO_JILL_RDT);
    }
    return s_jillState > 0;
}

// The room's own player animations back (RoomInit's assignment).
static void piano_room_anims(void)
{
    if (g_RdtPointer == NULL) return;
    g_playerEntity.jointMoveData2 = (unsigned int)g_RdtPointer->player_anim_header;
    g_playerEntity.jointMoveData3 = (unsigned int)g_RdtPointer->player_anim_base;
}
static int          s_listeners;          // survivors playing, as other copies hear them (bit per player)
static bool         s_wallDown;           // this room load has the wall down (or is not the bar)
static int          s_alertPlayer = -1;   // the director's copy: who is playing
static unsigned int s_alertStart;

// The tune's sound bank while loaded (0 none): ducked under voice lines.
int zm_piano_music_bank(void) { return s_musicBank; }

bool zm_piano_open(void)
{
    return Flg_ck((int)g_ScenarioFlags2, ZM_PIANO_OPEN_FLAG) != 0;
}

static bool piano_in_bar(void)
{
    return g_stageId == STAGE_MANSION_RETURN_1F && g_roomId == ROOM_MANSION_BAR;
}

// ROOM60F0's piano zone (room actions 0x0E / 0x0F, where the original makes
// the sheet music usable, and slot 2's look at the piano), with room around it.
#define ZM_PIANO_MARGIN 800
static bool piano_near(int x, int z)
{
    return x >= 10200 - ZM_PIANO_MARGIN && x <= 10800 + ZM_PIANO_MARGIN &&
           z >= 7300 - ZM_PIANO_MARGIN && z <= 8800 + ZM_PIANO_MARGIN;
}

static void piano_message(const unsigned char* text)
{
    if (set_message_display(0xFB, 0) == 0) g_MessagePtr = (unsigned char*)text;
}

static int piano_slot(void)
{
    if (!g_ItemSlotsPointer) return -1;
    const unsigned char* slots = (const unsigned char*)g_ItemSlotsPointer;
    for (int i = 0; i < 8; i++) if (slots[i * 2] == ITEM_MUSIC_NOTES) return i;
    return -1;
}

// The room's music off while the tune plays: whatever is playing - after a
// room change or a pause screen's resume, too - is stopped and marked paused,
// for ResumePausedSounds to bring back (SoundSystem.cpp's own pause).
static void piano_hold_bgm(void)
{
    if (g_BgmSoundBank != 0 && getSndStat(g_BgmSoundBank) == 1) {
        setSndStop(g_BgmSoundBank);
        g_BgmPaused = 1;
    }
    for (int i = 0; i < 3; i++) {
        if (g_SndBank[i].handle != 0 && getSndStat(g_SndBank[i].handle) == 1) {
            setSndStop(g_SndBank[i].handle);
            g_SndBank[i].paused = 1;
        }
    }
    s_bgmHeld = true;
    s_resumeAt = 0;
}

static void piano_music(bool on);

// Every frame: hold the room's music under the tune, then bring it back
// ZM_PIANO_RESUME_MS after the tune stops or runs out.
static void piano_bgm_frame(void)
{
    unsigned int now = zm_game_time_ms();
    if (s_music && s_musicBank != 0 && now - s_musicStart > 1000 && getSndStat(s_musicBank) != 1) {
        s_music = false;                    // the recording ran out
        s_fadeAt = 0;
    }
    if (s_music && s_fadeAt != 0) {
        unsigned int gone = now - s_fadeAt;
        if (gone >= ZM_PIANO_FADE_MS) {
            piano_music(false);             // faded out: stopped
        } else if (s_musicBank != 0) {
            int from = g_bgmDefaultVolume;
            set_volume(s_musicBank, from + (int)((long long)(ZM_PIANO_FADE_TO - from) * (int)gone / ZM_PIANO_FADE_MS));
        }
    }
    if (s_music && s_musicBank != 0) {
        piano_hold_bgm();
        return;
    }
    if (!s_bgmHeld) return;
    if (s_resumeAt == 0) {
        s_resumeAt = now + ZM_PIANO_RESUME_MS;
        if (s_resumeAt == 0) s_resumeAt = 1;
    } else if ((int)(now - s_resumeAt) >= 0) {
        ResumePausedSounds();
        s_bgmHeld = false;
        s_resumeAt = 0;
    }
}

// The tune from the start, or stopped.
static void piano_music(bool on)
{
    if (s_fadeAt != 0) {                    // stopped, or started again, mid-fade
        s_fadeAt = 0;
        if (on && s_music && s_musicBank != 0) setSndStop(s_musicBank);
        if (on) s_music = false;
    }
    if (on == s_music) return;
    s_music = on;
    if (on && s_musicBank == 0) {
        s_musicBank = loadSndBankFromWav(ZM_PIANO_MUSIC_WAV);
        if (s_musicBank == 0) dbg_printf("[piano] %s failed to load\n", ZM_PIANO_MUSIC_WAV);
    }
    if (s_musicBank == 0) return;
    if (on) {
        piano_hold_bgm();
        s_musicStart = zm_game_time_ms();
        pan_set(s_musicBank, 0);
        set_volume(s_musicBank, g_bgmDefaultVolume);
        SetSndSlot(s_musicBank, 0);         // once, not looped
    } else {
        setSndStop(s_musicBank);
    }
}

// A finished tune fades out (piano_bgm_frame), then stops.
static void piano_fade(void)
{
    if (s_music && s_fadeAt == 0) {
        s_fadeAt = zm_game_time_ms();
        if (s_fadeAt == 0) s_fadeAt = 1;
    }
}

// The pianist's pose, as the event VM's 0x84 puts it on the player: state 8
// (scripted), behavior 1 playing a room animation - Jill's, when her room
// file loaded; SysFlags ZM_PIANO_ANIM_DONE is raised when it is done.
static void piano_pose(int anim)
{
    g_playerEntity.animationId     = 8;
    g_playerEntity.animFrameId     = 0;
    g_playerEntity.action_behavior = 1;
    g_playerEntity.action_state    = 0;
    g_playerEntity.attackAnim      = (unsigned char)anim;
    g_playerEntity.scd_anim_param  = ZM_PIANO_ANIM_DONE;
    g_playerEntity.unk_de          = 0;
    g_playerEntity.unk_e0          = 0;
    if (piano_jill_anims()) {
        g_playerEntity.jointMoveData2 = s_jillHeader;
        g_playerEntity.jointMoveData3 = s_jillBase;
    }
    s_pose = anim;
}

// Where the pianist sits and which way it faces: the scene's spot, moved
// ZM_PIANO_RIGHT to its right and ZM_PIANO_FORWARD toward the keys. Angles
// are 0x1000 a turn and the player moves along (cos a, -sin a) (Add_speedXZ),
// so +x is 0 and the right is (-sin a, -cos a).
static void piano_seat(int* x, int* z, short* angle)
{
    bool jill = piano_jill_anims();
    int sx = jill ? ZM_PIANO_SEAT_X : ZM_PIANO_CHRIS_SEAT_X;
    int sz = jill ? ZM_PIANO_SEAT_Z : ZM_PIANO_CHRIS_SEAT_Z;
    double dx = (jill ? ZM_PIANO_FACE_X : ZM_PIANO_CHRIS_FACE_X) - sx;
    double dz = (jill ? ZM_PIANO_FACE_Z : ZM_PIANO_CHRIS_FACE_Z) - sz;
    double a = atan2(-dz, dx);
    *x = sx + (int)lround(-sin(a) * ZM_PIANO_RIGHT + cos(a) * ZM_PIANO_FORWARD);
    *z = sz + (int)lround(-cos(a) * ZM_PIANO_RIGHT - sin(a) * ZM_PIANO_FORWARD);
    *angle = (short)((int)lround(a * 2048.0 / 3.14159265358979323846) & 0xFFF);
}

// Back on its feet, unless something else (a grab, a hit) has the player.
static void piano_release(void)
{
    piano_room_anims();
    if (s_noCollision) {
        g_playerEntity.flags &= (unsigned char)~ZM_PIANO_NO_COLLISION;
        s_noCollision = false;
    }
    if (g_playerEntity.animationId != 8 || g_playerEntity.action_behavior != 1) return;
    g_playerEntity.animationId     = 1;
    g_playerEntity.animFrameId     = 0;
    g_playerEntity.action_behavior = 0;
    g_playerEntity.action_state    = 0;
}

static void piano_send(int dst, int op)
{
    unsigned int seed = zm_net_seed();
    zm_net_send_event_to(dst, ZM_EV_PIANO, (short)op, (short)zm_net_self(), (short)g_stageId,
                         (short)g_roomId, 0, 0, (short)seed, (short)(seed >> 16));
}

// The wall goes down: the flag for every later entry. A copy in the bar sees
// it sink (zm_piano_frame), however the flag reached it (OPEN or STORY).
static void piano_open_here(void)
{
    if (zm_piano_open()) return;
    Flg_on((int)g_ScenarioFlags2, ZM_PIANO_OPEN_FLAG);
    dbg_printf("[piano] the alcove's wall is open\n");
}

// After room init (zm_random_room_loaded's turn): an open wall is already put
// away by the room's own init.
void zm_piano_room(void)
{
    s_wallDown = zm_piano_open();
}

void zm_piano_reset(void)
{
    s_playing = false;
    s_pending = false;
    if (s_noCollision) g_playerEntity.flags &= (unsigned char)~ZM_PIANO_NO_COLLISION;
    s_noCollision = false;
    if (s_musicBank != 0) {
        setSndStop(s_musicBank);
        destroySndBank(s_musicBank);
    }
    s_musicBank = 0;
    s_music = false;
    s_fadeAt = 0;
    s_bgmHeld = false;
    s_resumeAt = 0;
    s_listeners = 0;
    s_alertPlayer = -1;
}

// The item menu's USE of the sheet music. True: accepted - the menu closes
// (zm_piano_menu_finished) and the playing starts once it has; false:
// refused, with the reason on screen.
bool zm_piano_use(void)
{
    int x = g_playerEntity.scaMatrixData.localMatrix.t[0], z = g_playerEntity.scaMatrixData.localMatrix.t[2];
    int ch = zm_game_role() == ZM_NET_SURVIVOR ? zm_net_char(zm_net_self()) : -1;
    const unsigned char* refusal = NULL;
    if (ch != ZM_CHAR_JILL && ch != ZM_CHAR_REBECCA && ch != ZM_CHAR_RICHARD) refusal = s_notPianist.bytes;
    else if (!piano_in_bar() || !piano_near(x, z)) refusal = s_notHere.bytes;
    else if (zm_piano_open()) refusal = s_alreadyOpen.bytes;
    if (refusal != NULL) {
        dbg_printf("[piano] refused: character %d at (%d, %d), stage %d room %02X\n",
                   ch, x, z, (int)g_stageId + 1, (int)g_roomId);
        piano_message(refusal);
        return false;
    }
    if (g_playerEntity.health < 0 || g_playerEntity.isBeingAttackedFlag || piano_slot() < 0) return false;
    s_pending = true;
    return true;
}

// The item menu (MainMenu.cpp): an accepted USE closes it.
bool zm_piano_menu_finished(void)
{
    return zombie_mode_armed() && s_pending;
}

// The menu is closed: placed at the piano, facing the keys, sitting down.
static void piano_begin(void)
{
    s_pending = false;
    if (!piano_in_bar() || zm_piano_open() || g_playerEntity.health < 0 ||
        g_playerEntity.isBeingAttackedFlag || piano_slot() < 0) return;
    int x, z;
    short angle;
    piano_seat(&x, &z, &angle);
    s_playing = true;
    s_playStart = zm_game_time_ms();
    s_startX = x;
    s_startZ = z;
    g_playerEntity.scaMatrixData.localMatrix.t[0] = x;
    g_playerEntity.scaMatrixData.localMatrix.t[2] = z;
    g_playerEntity.position.x = (short)x;
    g_playerEntity.position.z = (short)z;
    g_playerEntity.directionAngle = angle;
    if ((g_playerEntity.flags & ZM_PIANO_NO_COLLISION) == 0) {
        g_playerEntity.flags |= ZM_PIANO_NO_COLLISION;
        s_noCollision = true;
    }
    piano_pose(ZM_PIANO_ANIM_SIT);
    s_startHealth = g_playerEntity.health;
    piano_music(true);
    piano_send(ZM_NET_ALL, PIANO_START);
    dbg_printf("[piano] %s starts playing at (%d, %d) facing %03X\n",
               zm_char_name(zm_net_char(zm_net_self())), x, z, (unsigned)angle);
}

static void piano_stop(const char* why)
{
    s_playing = false;
    piano_release();
    if (s_listeners == 0) piano_music(false);
    piano_send(ZM_NET_ALL, PIANO_STOP);
    dbg_printf("[piano] stopped: %s\n", why);
}

// Every frame (zombie_mode_net_frame): the pianist's own checks.
void zm_piano_frame(void)
{
    piano_bgm_frame();
    if (!s_wallDown && zm_piano_open()) {
        s_wallDown = true;
        if (piano_in_bar()) ScdEventEntry_Create(9, ZM_PIANO_WALL_EVENT);
    }
    if (s_pending && !g_openMenuFlag && !(g_main_state_flags & (MSF_MENU_ACTIVE | MSF_ROOM_TRANSITION)) &&
        !g_roomTransitionBusy) piano_begin();
    if (!s_playing) {
        if (s_music && s_listeners == 0 && s_fadeAt == 0) piano_music(false);
        return;
    }
    int x = g_playerEntity.scaMatrixData.localMatrix.t[0], z = g_playerEntity.scaMatrixData.localMatrix.t[2];
    int dx = x - s_startX, dz = z - s_startZ;
    if (!piano_in_bar()) { piano_stop("left the bar"); return; }
    // The inventory or the map: stood back up, so the menu's idle
    // (player_menu_idle, which runs only for a player in control) shows on
    // every copy instead of a frozen piano pose.
    if (g_openMenuFlag == 1 || (g_main_state_flags & MSF_MENU_ACTIVE) || zm_map_is_open()) {
        piano_stop("menu opened");
        return;
    }
    if (g_playerEntity.health < 0 || g_playerEntity.health < s_startHealth ||
        g_playerEntity.isBeingAttackedFlag) { piano_stop("hurt"); return; }
    if ((g_PlayerDpadHeld & (ZM_PAD_FORWARD | ZM_PAD_BACK | ZM_PAD_TURN_A | ZM_PAD_TURN_B |
                             ZM_PAD_AIM | ZM_PAD_RUN)) != 0 ||
        dx * dx + dz * dz > ZM_PIANO_MOVE * ZM_PIANO_MOVE) { piano_stop("moved"); return; }
    if (zm_piano_open()) { piano_stop("opened by another survivor"); return; }
    if (piano_slot() < 0) { piano_stop("no sheet music"); return; }
    // The pose: the sit-down, then the two playing animations in turn.
    if (g_playerEntity.animationId != 8 || g_playerEntity.action_behavior != 1) {
        piano_stop("interrupted");
        return;
    } else if (g_playerEntity.action_state == 5) {
        piano_pose(s_pose == ZM_PIANO_ANIM_A ? ZM_PIANO_ANIM_B : ZM_PIANO_ANIM_A);
    }
    if (zm_game_time_ms() - s_playStart < ZM_PIANO_MS) return;

    // Finished: the sheet music is used up, and the host opens the wall. The
    // tune plays on to its end on every copy.
    s_playing = false;
    piano_release();
    piano_fade();
    int slot = piano_slot();
    unsigned char* item = (unsigned char*)g_ItemSlotsPointer + slot * 2;
    if (g_EquippedItemId == slot + 1) g_EquippedItemId = 0;
    item[0] = item[1] = 0;
    rearrange_item_slots();
    piano_send(ZM_NET_DIRECTOR, PIANO_DONE);
    piano_send(ZM_NET_ALL, PIANO_FINISHED);
    zm_note("THE TUNE IS DONE");
    dbg_printf("[piano] finished the tune\n");
}

// ZM_EV_PIANO { op, player, stage, room, 0, 0, seed low, seed high }: 1 start,
// 2 stop, 5 finished (survivor -> all), 3 done (survivor -> host), 4 open (host -> all).
void zm_piano_take(const short* a, int src)
{
    unsigned int seed = zm_net_seed();
    if ((unsigned short)a[6] != (unsigned short)seed || (unsigned short)a[7] != (unsigned short)(seed >> 16)) return;
    int player = a[1];
    if (player < 0 || player >= ZM_NET_MAX_PLAYERS) return;
    switch (a[0]) {
    case PIANO_START:
        s_listeners |= 1 << player;
        piano_music(true);
        if (zm_game_role() != ZM_NET_SURVIVOR) {
            s_alertPlayer = player;
            s_alertStart = zm_game_time_ms();
        }
        break;
    case PIANO_STOP:
        s_listeners &= ~(1 << player);
        if (s_alertPlayer == player) s_alertPlayer = -1;
        if (s_listeners == 0 && !s_playing) piano_music(false);
        break;
    case PIANO_FINISHED:
        s_listeners &= ~(1 << player);
        if (s_alertPlayer == player) s_alertPlayer = -1;
        if (s_listeners == 0) piano_fade();
        break;
    case PIANO_DONE:
        if (!zm_match_authority() || src != player) break;
        s_listeners &= ~(1 << player);
        if (s_alertPlayer == player) s_alertPlayer = -1;
        if (!zm_piano_open()) {
            piano_open_here();
            unsigned int s = zm_net_seed();
            zm_net_send_event_to(ZM_NET_ALL, ZM_EV_PIANO, PIANO_OPEN, (short)player, 0, 0, 0, 0,
                                 (short)s, (short)(s >> 16));
            zm_note("THE PIANO OPENED THE ALCOVE IN THE BAR");
        }
        break;
    case PIANO_OPEN:
        piano_open_here();
        break;
    }
}

// zombie_mode_draw_overlay: the pianist's timer, the director's alert.
void zm_piano_draw(void)
{
    char line[48];
    if (s_playing) {
        unsigned int gone = zm_game_time_ms() - s_playStart;
        unsigned int left = gone >= ZM_PIANO_MS ? 0 : (ZM_PIANO_MS - gone + 999) / 1000;
        snprintf(line, sizeof(line), "PLAYING THE PIANO %u", left);
        zm_draw_centered(line, 190, 0x8F);
        zm_draw_centered("MOVE TO STOP", 206, 0x7F);
        return;
    }
    if (s_alertPlayer >= 0 && zm_game_role() != ZM_NET_SURVIVOR) {
        unsigned int gone = zm_game_time_ms() - s_alertStart;
        if (gone > ZM_PIANO_MS + 5000) { s_alertPlayer = -1; return; }
        unsigned int left = gone >= ZM_PIANO_MS ? 0 : (ZM_PIANO_MS - gone + 999) / 1000;
        snprintf(line, sizeof(line), "PIANO BEING PLAYED IN THE BAR %u", left);
        zm_draw_centered(line, 190, 2);
    }
}
