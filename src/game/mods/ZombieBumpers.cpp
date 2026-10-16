#include "ZombieBumpers.h"
#include "ZombieModeInternal.h"
#include "ZombieNet.h"
#include "../../platform/platform.h"
#include "../../marni/MarniDX.h"
#include "../../marni/MarniSound.h"
#include "../../system/AssetPath.h"
#include "../../DebugPrint.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>

namespace {
const char* const kCharacters[] = { "chris", "jill", "barry", "rebecca", "richard", "enrico" };
struct Panel {
    PlatVideoClip* clip = NULL;
    MarniHandle texture = MARNI_NULL_HANDLE;
    int width = 0, height = 0;
    bool finished = false;
};
Panel s_panels[ZM_NET_MAX_PLAYERS];
int s_roster[ZM_NET_MAX_PLAYERS], s_rosterCount = 0, s_count = 0;
int s_pendingEnd = -1;
// survive.wav, played as the intro releases the match ("SURVIVE!") and left to
// play out into it; freed when it falls silent (zm_bumpers_frame), stopped by
// a reset or leaving.
int s_introTail = 0;
// helicopter.wav under the opening narration. Not freed by zm_bumpers_reset:
// the new game's second reset can come after the narration has started.
int s_heliBank = 0;
// The slices' slide in (sound/sliding.wav) and their crash together
// (sound/ending07.wav), from the game's own sounds; each freed once silent.
int s_slideBank = 0, s_crashBank = 0;
bool s_introDone = false, s_draw = false, s_recovered = false, s_have = false;
unsigned int s_started = 0;
// Sliding in, on their first frames; s_started is the crash.
bool s_sliding = false;
unsigned int s_slideAt = 0;

// The clips play over the live room, input held, instead of blocking
// game_loop: the intro once the room has faded in, the ending before the
// results screen.
// Wesker's all-dead clip is not a slice: the room fades to white, the FMV
// player plays it full screen with its sound, fading in from white, and its
// last frame stays under the results screen.
enum Live { LIVE_OFF, LIVE_INTRO_ROOM, LIVE_INTRO_PLAY, LIVE_INTRO_RELEASE, LIVE_END_PLAY,
            LIVE_WESKER_WHITE, LIVE_WESKER_PLAY };
Live s_live = LIVE_OFF;
unsigned int s_liveAt = 0, s_waitAt = 0, s_flashAt = 0, s_fadeAt = 0, s_backdropAt = 0;
bool s_backdrop = false;   // Wesker's frozen last frame, under the results
bool s_blood = false;      // the intro's slices wear a bloody border
// The room's music waits for the intro: BGM channel starts (g_SndBank 0-2)
// are recorded here until it releases the match, then started from the top.
void tail_free()
{
    if (s_introTail) { setSndStop(s_introTail); destroySndBank(s_introTail); s_introTail = 0; }
}
void heli_free()
{
    if (s_heliBank) { setSndStop(s_heliBank); destroySndBank(s_heliBank); s_heliBank = 0; }
}
void bank_free(int& bank)
{
    if (bank) { setSndStop(bank); destroySndBank(bank); bank = 0; }
}
bool asset_path(const char* relative, char* path, size_t size);
// A one-shot, centred at the sound effects' volume.
int play_sound_file(const char* path, const char* label)
{
    int bank = loadSndBankFromWav(path);
    if (!bank) { dbg_printf("[bumper] %s missing or unsupported\n", label); return 0; }
    pan_set(bank, 0);
    set_volume(bank, g_SfxVolume);
    playSnd(bank, 0);
    return bank;
}
// From infested/.
int play_asset_sound(const char* relative)
{
    char path[1200];
    if (!asset_path(relative, path, sizeof(path))) return 0;
    return play_sound_file(path, relative);
}
bool s_introReleased = false;
unsigned int s_heldBgm = 0;
// Wesker's ending: from its fade to white on, no music comes back.
bool s_silenced = false;
// "SURVIVE!" in the middle of the screen for a second once the intro is over.
unsigned int s_surviveAt = 0;
const unsigned int kSurviveMs = 1000, kSurvivePopMs = 120;
const float kSurviveScale = 4.0f, kSurvivePopScale = 5.0f;
// Wesker's movie through the FMV player: its file, its size (for the fit),
// requested / playing, and the last frame shown.
char s_moviePath[1200];
int s_movieW = 0, s_movieH = 0, s_movieLastMs = 0;
bool s_moviePending = false, s_moviePlaying = false;
unsigned int s_movieAt = 0;

// Layout, as fractions of the screen: 10% margins above and below, each
// slice 20% of the width, its sides leaning 15 degrees from vertical, 5
// pixels apart at 480 lines (scaled with the screen's height).
const float kMargin = 0.10f, kSliceWidth = 0.20f, kSliceGapPx480 = 5.0f;
const float kTan15 = 0.2679491924f;
const unsigned int kFlashMs = 300, kIntroMs = 3000, kEndMaxMs = 30000;
// The slices slide in from off screen (sliding.wav's length) before the flash.
const unsigned int kSlideMs = 550;
const unsigned int kBarrierMs = 150000, kRoomWaitMs = 2000;
const unsigned int kReleaseNoticeMs = 1000;   // before a start wait says so
const unsigned int kWeskerFadeMs = 1200, kDimMs = 600;   // into white, and out of it into the movie
const float kBackdropDim = 0.5f;

void flash() { unsigned int now = plat_time_ms(); s_flashAt = now ? now : 1; }

bool music_held()
{
    if (s_silenced) return true;
    return zombie_mode_armed() && !s_introReleased && !s_recovered && !zm_net_rejoining();
}
int bgm_channel(int bank)
{
    for (int i = 0; i < 3; ++i)
        if (bank != 0 && g_SndBank[i].handle == bank) return i;
    return -1;
}
// The intro is over (played, skipped or never due): the clock starts, and the
// music held back starts now. `fanfare`: "SURVIVE!" and its sound.
void intro_released(bool fanfare = true)
{
    zm_bumper_intro_clock_release();
    if (s_introReleased) return;
    s_introReleased = true;
    unsigned int now = plat_time_ms();
    if (fanfare) s_surviveAt = now ? now : 1;
    unsigned int held = s_heldBgm;
    s_heldBgm = 0;
    for (int i = 0; i < 3; ++i) {
        if ((held & (1u << i)) == 0 || g_SndBank[i].handle == 0) continue;
        SetSndSlot(g_SndBank[i].handle, g_SndBank[i].slot);
    }
    if (held) dbg_printf("[bumper] room music released (channels %X)\n", held);
    if (!fanfare) return;
    tail_free();
    s_introTail = play_asset_sound("audio/survive.wav");
}

// A human director's copy waits for nobody: it plays from the moment its
// room is up (setting up while the survivors pick, watch the narration and
// the intro). The host still runs the survivors' barrier as their reports
// come in (ZombieNet.cpp, PKT_BUMPER).
bool director_plays_at_once()
{
    return g_bPlayAsZombie && zm_net_role() == ZM_NET_ZOMBIE && zm_net_ai_level() == 0 && zm_net_active();
}

void close_panels()
{
    s_draw = false;
    s_backdrop = false;
    s_blood = false;
    MarniDX* dx = Marni_DX();
    for (int i = 0; i < ZM_NET_MAX_PLAYERS; ++i) {
        plat_video_clip_close(s_panels[i].clip);
        if (dx && s_panels[i].texture) dx->DestroyTexture(s_panels[i].texture);
        s_panels[i] = Panel();
    }
    s_count = 0;
}
bool asset_path(const char* relative, char* path, size_t size)
{
    char dir[1024], raw[1200];
    if (!plat_exe_dir(dir, sizeof(dir))) return false;
    snprintf(raw, sizeof(raw), "%s/infested/%s", dir, relative);
    const char* normalized = plat_normalize_path(raw, path, size);
    if (normalized != path) snprintf(path, size, "%s", normalized);
    return true;
}
void capture_roster()
{
    s_rosterCount = 0;
    if (zm_game_role() == ZM_NET_OFF) {
        s_roster[s_rosterCount++] = (g_playerEntity.id & 1) ? ZM_CHAR_JILL : ZM_CHAR_CHRIS;
    } else {
        for (int i = 0; i < ZM_NET_MAX_PLAYERS; ++i) {
            int ch = zm_net_char(i);
            if (ch >= 0 && ch < ZM_CHAR_COUNT) s_roster[s_rosterCount++] = ch;
        }
    }
}
bool open_panels(const char* folder, bool wesker)
{
    close_panels();
    if (!s_rosterCount) capture_roster();
    s_count = wesker ? 1 : s_rosterCount;
    bool& have = s_have;
    have = false;
    for (int i = 0; i < s_count; ++i) {
        char relative[128], path[1200];
        if (wesker) snprintf(relative, sizeof(relative), "video/wesker_victory.mp4");
        else snprintf(relative, sizeof(relative), "video/%s/%s.mp4", folder, kCharacters[s_roster[i]]);
        if (asset_path(relative, path, sizeof(path))) s_panels[i].clip = plat_video_clip_open(path);
        s_panels[i].finished = !s_panels[i].clip;
        dbg_printf("[bumper] %s: %s\n", relative, s_panels[i].clip ? "loaded" : "missing or unsupported; panel skipped");
        have |= s_panels[i].clip != NULL;
    }
    return have;
}
// The clips are silent: the intro's sound is survive.wav, on its release.
// The slices slide in on their first frames first (slide()), then crash
// together with the flash and play.
void start()
{
    s_started = plat_time_ms();
    s_draw = s_have;
    s_sliding = s_draw;
    if (!s_sliding) return;
    s_slideAt = s_started;
    bank_free(s_slideBank);
    s_slideBank = play_sound_file(GAME_DATA_ROOT "sound\\sliding.wav", "sound/sliding.wav");
}
bool update();
// While sliding in: keep the first frames up; TRUE until the crash, which
// flashes and starts the clips' clock.
bool slide()
{
    if (!s_sliding) return false;
    unsigned int now = plat_time_ms();
    if (now - s_slideAt < kSlideMs) {
        s_started = now;   // update() at 0 ms: the first frame
        update();
        return true;
    }
    s_sliding = false;
    s_started = now;
    flash();
    bank_free(s_crashBank);
    s_crashBank = play_sound_file(GAME_DATA_ROOT "sound\\ending07.wav", "sound/ending07.wav");
    return false;
}
bool update()
{
    unsigned int elapsed = plat_time_ms() - s_started;
    bool done = true;
    MarniDX* dx = Marni_DX();
    for (int i = 0; i < s_count; ++i) {
        Panel& p = s_panels[i];
        if (!p.clip || p.finished) continue;
        const unsigned char* rgba = NULL;
        p.finished = !plat_video_clip_frame(p.clip, (int)elapsed, &rgba, &p.width, &p.height);
        done &= p.finished;
        if (!rgba || !dx) continue;
        if (!p.texture) {
            int tw, th;
            p.texture = dx->CreateTexture(p.width, p.height, 32, rgba, &tw, &th);
        } else if (!s_sliding) dx->UpdateTexturePixels(p.texture, rgba, p.width, p.height, 32);
    }
    return done;
}
} // namespace

void zm_bumpers_reset(void)
{
    close_panels();
    s_introDone = false; s_pendingEnd = -1; s_rosterCount = 0;
    s_live = LIVE_OFF; s_flashAt = 0; s_fadeAt = 0; s_have = false; s_sliding = false;
    s_introReleased = false; s_heldBgm = 0; s_surviveAt = 0; s_silenced = false;
    tail_free();
    s_moviePending = s_moviePlaying = false;
    s_recovered = zm_net_rejoining();
}

// game_loop, while the opening narration is up: keep reporting in (the host's
// replies carry the roster), so it is current when the clips are loaded.
void zombie_mode_bumper_roster_poll(void)
{
    if (!zombie_mode_armed() || s_introDone || s_recovered || zm_net_rejoining()) return;
    if (zm_game_role() != ZM_NET_OFF) zm_net_bumper_phase(0);
}

// game_loop, before its first frame: load the clips. Nothing waits here -
// every copy plays its intro as soon as its room is on screen
// (zm_bumpers_frame) and the copies meet only at its end, before "SURVIVE!".
void zombie_mode_bumper_intro(void)
{
    if (!zombie_mode_armed() || s_introDone) return;
    s_introDone = true;
    // A recovered match is already running, including its original roster/clock.
    if (s_recovered || zm_net_rejoining()) { capture_roster(); s_introReleased = true; return; }
    if (director_plays_at_once()) {
        capture_roster();
        intro_released(false);
        dbg_printf("[bumper] director: no intro, playing at once\n");
        return;
    }
    s_waitAt = plat_time_ms();
    // The roster as this copy knows it now; refreshed from the host's frozen
    // one at the release, for the ending's slices.
    capture_roster();
    open_panels("intro", false);
    s_blood = true;
    s_live = LIVE_INTRO_ROOM;
    s_liveAt = 0;
}

void zm_bumpers_match_end(int reason)
{
    if (reason == ZM_END_ESCAPE || reason == ZM_END_ALL_DEAD) s_pendingEnd = reason;
}

// Every game frame (zombie_mode_net_frame, after the link is polled).
void zm_bumpers_frame(void)
{
    if (s_introTail && getSndStat(s_introTail) == 0) {
        destroySndBank(s_introTail);
        s_introTail = 0;
        dbg_printf("[bumper] survive sound finished\n");
    }
    if (s_slideBank && getSndStat(s_slideBank) == 0) { destroySndBank(s_slideBank); s_slideBank = 0; }
    if (s_crashBank && getSndStat(s_crashBank) == 0) { destroySndBank(s_crashBank); s_crashBank = 0; }
    if (s_heliBank && getSndStat(s_heliBank) == 0) {
        destroySndBank(s_heliBank);
        s_heliBank = 0;
        dbg_printf("[bumper] narration sound finished\n");
    }
    if (!zombie_mode_armed()) return;
    unsigned int now = plat_time_ms();
    bool network = zm_game_role() != ZM_NET_OFF;
    if (s_pendingEnd >= 0 && (s_live == LIVE_OFF || s_live == LIVE_INTRO_RELEASE)) {
        if (s_live == LIVE_INTRO_RELEASE) intro_released();
        int reason = s_pendingEnd; s_pendingEnd = -1;
        s_live = LIVE_OFF;
        if (reason == ZM_END_ALL_DEAD) {
            // Probe the movie (and take its size) before fading to white.
            unsigned char* rgba = NULL;
            close_panels();
            s_movieW = s_movieH = 0;
            bool ok = asset_path("video/wesker_victory.mp4", s_moviePath, sizeof(s_moviePath)) &&
                      plat_video_read_frame(s_moviePath, 0, &rgba, &s_movieW, &s_movieH);
            free(rgba);
            dbg_printf("[bumper] video/wesker_victory.mp4: %s\n", ok ? "loaded" : "missing or unsupported; skipped");
            if (!ok) return;
            s_fadeAt = now ? now : 1;
            s_live = LIVE_WESKER_WHITE;
            s_silenced = true;                  // the room's music never comes back
            return;
        }
        if (!open_panels("victory", false)) { close_panels(); return; }
        start();
        s_live = LIVE_END_PLAY;
        dbg_printf("[bumper] ending started: reason %d\n", reason);
        return;
    }
    switch (s_live) {
    case LIVE_OFF:
        return;
    case LIVE_INTRO_ROOM:
        // Wait out the room's fade-in, so the slices land on the room.
        if (!s_liveAt) s_liveAt = now ? now : 1;
        if (((g_main_state_flags & MSF_FADE_ACTIVE) == 0 && now - s_liveAt >= 100) ||
            now - s_liveAt >= kRoomWaitMs) {
            start();
            s_live = LIVE_INTRO_PLAY;
        }
        return;
    case LIVE_INTRO_PLAY:
        // Intro assets are three seconds / 72 frames at 24 fps. Every copy waits
        // the same duration even if it has no local assets; short clips hold last frame.
        if (slide()) return;
        update();
        if (now - s_started >= kIntroMs) {
            if (s_draw) flash();
            s_draw = false;
            s_live = LIVE_INTRO_RELEASE;
            s_liveAt = 0;
        }
        return;
    case LIVE_INTRO_RELEASE: {
        // The one sync point: held on the room until every copy has finished.
        if (!s_liveAt) s_liveAt = now ? now : 1;
        int phase = network ? zm_net_bumper_phase(2) : 3;
        if (phase < 3 && now - s_waitAt < kBarrierMs && zm_net_status() != ZM_NET_EXPIRED) return;
        close_panels();
        s_live = LIVE_OFF;
        s_rosterCount = 0;
        capture_roster();
        intro_released();
        dbg_printf("[bumper] intro complete: %d survivor panels\n", s_rosterCount);
        return;
    }
    case LIVE_WESKER_WHITE:
        // White: hand the movie to the FMV player, as cmd_fmv_set does. The
        // game (this task too) stands still until it is over.
        if (now - s_fadeAt < kWeskerFadeMs) return;
        s_moviePending = true;
        s_movieLastMs = 0;
        s_movieAt = 0;
        g_selectedFmvId = 0xFF;                  // no table entry: no cut, subtitles or credits
        g_main_state_flags |= MSF_FMV_REQUEST;
        s_live = LIVE_WESKER_PLAY;
        return;
    case LIVE_WESKER_PLAY: {
        if ((g_main_state_flags & MSF_FMV_REQUEST) != 0 || g_bMCINotifyEnabled) return;
        // Over: its last frame stays, the results screen comes up over it.
        s_moviePending = s_moviePlaying = false;
        s_live = LIVE_OFF;
        unsigned char* rgba = NULL;
        int fw = 0, fh = 0;
        MarniDX* dx = Marni_DX();
        if (dx && plat_video_read_frame(s_moviePath, s_movieLastMs, &rgba, &fw, &fh)) {
            int tw, th;
            Panel& p = s_panels[0];
            p.texture = dx->CreateTexture(fw, fh, 32, rgba, &tw, &th);
            p.width = fw; p.height = fh;
            s_backdrop = p.texture != MARNI_NULL_HANDLE;
            s_backdropAt = now;
        }
        free(rgba);
        zm_bumper_end_clock_release();
        dbg_printf("[bumper] ending complete: wesker %s at %d ms\n",
                   s_backdrop ? "frozen under the results" : "frame unavailable", s_movieLastMs);
        return;
    }
    case LIVE_END_PLAY:
        // End clips play through EOF. Keep shorter panels on their final frame.
        // Bound malformed/accidentally long assets so the results remain reachable.
        if (slide()) return;
        if (!update() && now - s_started < kEndMaxMs) return;
        flash();
        close_panels();
        s_live = LIVE_OFF;
        zm_bumper_end_clock_release();
        dbg_printf("[bumper] ending complete\n");
        return;
    }
}

bool zm_bumpers_hold_input(void)
{
    return zombie_mode_armed() && s_live != LIVE_OFF;
}

// SoundApi.cpp's SetSndSlot / playSnd: a music channel starting before the
// intro has released the match is held (TRUE: do not start it).
bool zombie_mode_bgm_hold(int bank)
{
    if (!music_held()) return false;
    int ch = bgm_channel(bank);
    if (ch < 0) return false;
    if ((s_heldBgm & (1u << ch)) == 0) dbg_printf("[bumper] room music channel %d held for the intro\n", ch);
    s_heldBgm |= 1u << ch;
    return true;
}
// setSndStop / destroySndBank: a held channel stopped or freed is not started later.
void zombie_mode_bgm_stopped(int bank)
{
    int ch = bgm_channel(bank);
    if (ch >= 0) s_heldBgm &= ~(1u << ch);
}

// zombie_mode_draw_overlay: the "SURVIVE!" banner, the game's 8x14 font
// scaled up, red, popping in from a little larger.
void zm_bumpers_draw_hud(void)
{
    if (!zombie_mode_armed()) return;
    // Waiting at the intro's end for slower copies: say so after a moment.
    if (s_live == LIVE_INTRO_RELEASE && s_liveAt && plat_time_ms() - s_liveAt >= kReleaseNoticeMs) {
        zm_draw_centered("WAITING FOR SURVIVORS", 110, 0x7F);
        return;
    }
    if (!s_surviveAt) return;
    unsigned int t = plat_time_ms() - s_surviveAt;
    if (t >= kSurviveMs) { s_surviveAt = 0; return; }
    float scale = kSurviveScale;
    if (t < kSurvivePopMs)
        scale = kSurvivePopScale + (kSurviveScale - kSurvivePopScale) * t / kSurvivePopMs;
    const char* text = "SURVIVE!";
    float width = (float)strlen(text) * 8.0f * scale, height = 14.0f * scale;
    snprintf(PRINT_TEXT_BUFFER, sizeof(PRINT_TEXT_BUFFER), "%s", text);
    zm_text_encode(PRINT_TEXT_BUFFER);
    PrintText8x14Scaled((short)((320.0f - width) * 0.5f), (short)((240.0f - height) * 0.5f), 2, 1, scale);
}

bool zm_bumpers_intro_playing(void)
{
    return zombie_mode_armed() &&
           (s_live == LIVE_INTRO_ROOM || s_live == LIVE_INTRO_PLAY || s_live == LIVE_INTRO_RELEASE);
}

bool zm_bumpers_end_playing(void)
{
    return s_live == LIVE_END_PLAY || s_live == LIVE_WESKER_WHITE || s_live == LIVE_WESKER_PLAY ||
           s_pendingEnd >= 0;
}

bool zombie_mode_fmv_path(char* out, unsigned int size)
{
    if (!s_moviePending || !out || !size) return false;
    s_moviePending = false;
    s_moviePlaying = true;
    snprintf(out, size, "%s", s_moviePath);
    return true;
}

void zombie_mode_fmv_clear(void)
{
    MarniDX* dx = Marni_DX();
    if (dx) dx->Clear(1, 1, 1, 1);
}

bool zombie_mode_fmv_playing(void)
{
    return s_moviePlaying;
}

// VideoPlayback.cpp's cleanup, after Wesker's movie, in place of
// ResumeGameSoundsAsync: the music it paused stays off.
void zombie_mode_fmv_silence(void)
{
    if (g_BgmSoundBank != 0) setSndStop(g_BgmSoundBank);
    g_BgmPaused = 0;
    for (int i = 0; i < 3; i++) {
        if (g_SndBank[i].handle != 0) setSndStop(g_SndBank[i].handle);
        g_SndBank[i].paused = 0;
    }
    dbg_printf("[bumper] wesker: room music left off\n");
}

bool zm_bumpers_silenced(void)
{
    return s_silenced;
}

bool zm_bumpers_backdrop(void)
{
    return s_backdrop && s_panels[0].texture;
}

void zombie_mode_bumper_leave(void)
{
    close_panels();
    s_live = LIVE_OFF; s_pendingEnd = -1; s_flashAt = 0; s_sliding = false;
    bank_free(s_slideBank);
    bank_free(s_crashBank);
    s_moviePending = s_moviePlaying = false;
    s_silenced = false; s_heldBgm = 0;
    tail_free();
    heli_free();
}

// display_game_loading_message, before the new game's opening narration: the
// roster is settled here, so nothing waits between the narration and the
// intro. Every copy reports in (bumper progress 0) once its survivor has
// picked; the host's phase 1 says all have, and its replies carry the roster.
// game_loop waits on this (zombie_mode_narration_held) as on the narration.
static bool s_narrationHeld = false;
void zombie_mode_narration_wait(void)
{
    if (!g_bPlayAsZombie || zm_net_rejoining() || !zm_net_active()) return;
    if (director_plays_at_once()) return;
    s_narrationHeld = true;
    unsigned int begin = plat_time_ms();
    while (zm_net_bumper_phase(0) < 1 && plat_time_ms() - begin < kBarrierMs &&
           zm_net_status() != ZM_NET_EXPIRED) {
        if (plat_time_ms() - begin >= kReleaseNoticeMs)
            zm_draw_centered("WAITING FOR SURVIVORS", 110, 0x7F);
        zm_net_poll();
        Task_sleep(1);
    }
    dbg_printf("[bumper] roster settled after %u ms\n", plat_time_ms() - begin);
    s_narrationHeld = false;
}
bool zombie_mode_narration_skip(void)
{
    return director_plays_at_once();
}
bool zombie_mode_narration_held(void)
{
    return s_narrationHeld;
}

// display_game_loading_message, as the new game's opening narration (0x5B)
// starts: the helicopter under the text. Called before zombie_mode_new_game(1)
// arms the mode, so it asks for the mode itself. It plays out on its own.
void zombie_mode_narration_sound(void)
{
    if (!g_bPlayAsZombie || zm_net_rejoining()) return;
    heli_free();
    s_heliBank = play_asset_sound("audio/helicopter.wav");
}

namespace {
// The panel's clip on black, fitted whole to the screen (letterboxed, never
// cropped or stretched), tinted by `level`.
void draw_fullscreen(MarniDX* dx, MarniHandle tex, int tw, int th, float w, float h, float level)
{
    dx->DrawSprite(0, 0, w, h, 0, 0, 1, 1, 0xFF000000u, MARNI_NULL_HANDLE,
                   MARNI_SAMPLER_POINT, MARNI_BLEND_DISABLE);
    if (!tex || tw <= 0 || th <= 0) return;
    float scale = w / tw;
    if (h / th < scale) scale = h / th;
    float iw = tw * scale, ih = th * scale;
    DWORD c = (DWORD)(level * 255.0f + 0.5f);
    dx->DrawSprite((w - iw) * 0.5f, (h - ih) * 0.5f, iw, ih, 0, 0, 1, 1,
                   0xFF000000u | c << 16 | c << 8 | c, tex, MARNI_SAMPLER_LINEAR, MARNI_BLEND_DISABLE);
}
void draw_white(MarniDX* dx, float w, float h, float a)
{
    if (a <= 0) return;
    if (a > 1) a = 1;
    dx->DrawSprite(0, 0, w, h, 0, 0, 1, 1, ((DWORD)(a * 255.0f) << 24) | 0x00FFFFFFu, MARNI_NULL_HANDLE,
                   MARNI_SAMPLER_POINT, MARNI_BLEND_ALPHA);
}
} // namespace

// The intro slices' bloody border, drawn procedurally: a thin ragged outline
// round each slice that bleeds inward over the clip. Untextured triangles, a
// dark pass under a red one; every slice's shape comes from its own seed, the
// same on every copy and frame.
namespace {
const float kBloodDark[4] = { 0.20f, 0.00f, 0.00f, 1.0f };
const float kBloodRed[4]  = { 0.55f, 0.02f, 0.02f, 1.0f };

float blood_noise(int seed, int k)
{
    unsigned int x = (unsigned int)seed * 0x9E3779B1u ^ (unsigned int)k * 0x85EBCA77u;
    x ^= x >> 15; x *= 0x2C1B3C6Du; x ^= x >> 12; x *= 0x297A2D39u; x ^= x >> 15;
    return (x & 0xFFFF) / 65535.0f;
}

struct BloodBatch {
    enum { MAX_TRIS = 1020 };
    MarniDX* dx;
    float v[MAX_TRIS * 3 * 8];
    int tris;
    void flush()
    {
        if (tris) dx->DrawTriangles(v, tris, MARNI_NULL_HANDLE, MARNI_SAMPLER_LINEAR, MARNI_BLEND_ALPHA);
        tris = 0;
    }
    void tri(float x0, float y0, float x1, float y1, float x2, float y2, const float* c)
    {
        if (tris == MAX_TRIS) flush();
        float* o = v + tris * 24;
        const float xy[6] = { x0, y0, x1, y1, x2, y2 };
        for (int i = 0; i < 3; ++i, o += 8) {
            o[0] = xy[i*2]; o[1] = xy[i*2+1]; o[2] = o[3] = 0;
            o[4] = c[0]; o[5] = c[1]; o[6] = c[2]; o[7] = c[3];
        }
        ++tris;
    }
};
BloodBatch s_bloodBatch;

// One side's ragged band, a to b (clockwise, so the outward normal is
// (dy, -dx) on a y-down screen), running a little past both corners. Mostly
// inside the edge: it bleeds onto the clip, barely past the slice.
void blood_edge(BloodBatch& b, float ax, float ay, float bx, float by, float base, float grow,
                int seed, const float* c)
{
    float dx = bx - ax, dy = by - ay, len = sqrtf(dx*dx + dy*dy);
    if (len < 1) return;
    dx /= len; dy /= len;
    float nx = dy, ny = -dx, ext = base * 0.8f;
    ax -= dx * ext; ay -= dy * ext; len += ext * 2;
    int n = (int)(len / (base * 2.4f));
    if (n < 8) n = 8;
    float px0 = 0, py0 = 0, ix0 = 0, iy0 = 0;
    for (int k = 0; k <= n; ++k) {
        float t = len * k / n, x = ax + dx * t, y = ay + dy * t;
        float jag = 0.5f * blood_noise(seed, k) + 0.25f * blood_noise(seed, k - 1) + 0.25f * blood_noise(seed, k + 1);
        float bleed = 0.5f * blood_noise(seed + 7, k) + 0.25f * blood_noise(seed + 7, k - 1) +
                      0.25f * blood_noise(seed + 7, k + 1);
        float out = base * grow * (0.15f + 0.35f * jag), in = base * grow * (0.4f + 1.4f * bleed * bleed);
        float px = x + nx * out, py = y + ny * out, ix = x - nx * in, iy = y - ny * in;
        if (k) {
            b.tri(ix0, iy0, px0, py0, px, py, c);
            b.tri(ix0, iy0, px, py, ix, iy, c);
        }
        px0 = px; py0 = py; ix0 = ix; iy0 = iy;
    }
}

void draw_blood(MarniDX* dx, const float corners[][4][2], int count, float h)
{
    BloodBatch& b = s_bloodBatch;
    b.dx = dx; b.tris = 0;
    float base = h * 0.006f;
    for (int pass = 0; pass < 2; ++pass) {
        const float* c = pass ? kBloodRed : kBloodDark;
        float grow = pass ? 1.0f : 1.3f;
        for (int i = 0; i < count; ++i) {
            const float (*q)[2] = corners[i];
            int seed = 0x5EED + i * 101;
            for (int e = 0; e < 4; ++e)
                blood_edge(b, q[e][0], q[e][1], q[(e+1)&3][0], q[(e+1)&3][1], base, grow, seed + e * 13, c);
        }
    }
    b.flush();
}
} // namespace

// FrameRateGovernor, after the 3D and before the 2D sprites: Wesker's frozen
// frame, so the results screen draws over it.
void zombie_mode_bumper_draw_backdrop(void)
{
    if (!zombie_mode_armed() || !zm_bumpers_backdrop()) return;
    MarniDX* dx = Marni_DX();
    if (!dx) return;
    DWORD bw, bh; dx->GetBackBufferSize(&bw, &bh);
    if (!bw || !bh) return;
    unsigned int t = plat_time_ms() - s_backdropAt;
    float dim = t >= kDimMs ? 1.0f : (float)t / kDimMs;
    const Panel& p = s_panels[0];
    draw_fullscreen(dx, p.texture, p.width, p.height, (float)bw, (float)bh, 1.0f - (1.0f - kBackdropDim) * dim);
}

// The FMV player's overlay callback, each presented frame of Wesker's movie:
// fitted on black, fading in from white.
bool zombie_mode_fmv_frame(unsigned int frameTexture, int frameMs)
{
    if (!s_moviePlaying) return false;
    MarniDX* dx = Marni_DX();
    if (!dx) return false;
    DWORD bw, bh; dx->GetBackBufferSize(&bw, &bh);
    if (!bw || !bh) return false;
    unsigned int now = plat_time_ms();
    if (!s_movieAt) s_movieAt = now ? now : 1;
    if (frameMs > s_movieLastMs) s_movieLastMs = frameMs;
    float w = (float)bw, h = (float)bh;
    draw_fullscreen(dx, (MarniHandle)frameTexture, s_movieW, s_movieH, w, h, 1.0f);
    draw_white(dx, w, h, 1.0f - (float)(now - s_movieAt) / kWeskerFadeMs);
    return true;
}

bool zombie_mode_bumper_draw(void)
{
    MarniDX* dx = Marni_DX();
    if (!dx) return false;
    DWORD bw, bh; dx->GetBackBufferSize(&bw, &bh);
    if (!bw || !bh) return false;
    float w = (float)bw, h = (float)bh;
    bool drew = false;
    if (s_live == LIVE_WESKER_WHITE) {
        draw_white(dx, w, h, (float)(plat_time_ms() - s_fadeAt) / kWeskerFadeMs);
        return true;
    }
    if (s_live == LIVE_WESKER_PLAY) {           // the FMV player is about to take over
        draw_white(dx, w, h, 1.0f);
        return true;
    }
    if (s_draw && s_count) {
        // Parallelograms over the room: top and bottom horizontal, sides 15
        // degrees from vertical, the group centred on the screen. Narrowed
        // only if three would not fit an unusually narrow window.
        float top = h * kMargin, bottom = h * (1.0f - kMargin), sliceH = bottom - top;
        float slant = sliceH * kTan15;
        float sliceW = w * kSliceWidth, gap = kSliceGapPx480 * h / 480.0f;
        float room = w * 0.96f - slant - (s_count - 1) * gap;
        if (sliceW * s_count > room) sliceW = room / s_count;
        if (sliceW < 1) sliceW = 1;
        float total = s_count * sliceW + (s_count - 1) * gap + slant;
        float x0 = (w - total) * 0.5f;
        float all[ZM_NET_MAX_PLAYERS][4][2];
        // Sliding in, speeding up until they crash together: the left half
        // from the left, the right half from the right, an odd count's
        // middle one (a lone slice too) from the top.
        float rest = 0;
        if (s_sliding) {
            float t = (float)(plat_time_ms() - s_slideAt) / kSlideMs;
            if (t > 1) t = 1;
            rest = 1.0f - t * t;
        }
        for (int i = 0; i < s_count; ++i) {
            Panel& p = s_panels[i];
            float bl = x0 + i * (sliceW + gap), br = bl + sliceW;
            float sx = 0, sy = 0;
            if (rest > 0) {
                if (s_count % 2 && i == s_count / 2) sy = -(bottom + 2) * rest;
                else if (i < s_count / 2) sx = -(br + slant + 2) * rest;
                else sx = (w - bl + 2) * rest;
            }
            bl += sx; br += sx;
            float top = h * kMargin + sy, bottom = h * (1.0f - kMargin) + sy;
            float tl = bl + slant, tr = br + slant;
            float (&corners)[4][2] = all[i];
            corners[0][0] = tl; corners[0][1] = top; corners[1][0] = tr; corners[1][1] = top;
            corners[2][0] = br; corners[2][1] = bottom; corners[3][0] = bl; corners[3][1] = bottom;
            const int indices[6] = { 0,1,2, 0,2,3 };
            // Crop-to-fill inside the slice's bounding rectangle; never stretch
            // or rotate the video. UVs follow screen position, not slanted edges.
            bool video = p.texture && p.width && p.height;
            float minX = bl, maxX = tr, boxW = maxX - minX;
            float scale = video ? sliceH / p.height : 1;
            if (video && boxW / p.width > scale) scale = boxW / p.width;
            float imageW = video ? p.width * scale : boxW, imageH = video ? p.height * scale : sliceH;
            float originX = (minX + maxX - imageW) * 0.5f, originY = (top + bottom - imageH) * 0.5f;
            float verts[48];
            for (int v = 0; v < 6; ++v) {
                float x = corners[indices[v]][0], y = corners[indices[v]][1];
                float* out = verts + v*8;
                out[0]=x; out[1]=y; out[2]=(x-originX)/imageW; out[3]=(y-originY)/imageH;
                // A missing clip leaves its slice black.
                out[4]=out[5]=out[6]= video ? 1.0f : 0.0f; out[7]=1;
            }
            dx->DrawTriangles(verts, 2, video ? p.texture : MARNI_NULL_HANDLE,
                              MARNI_SAMPLER_LINEAR, MARNI_BLEND_DISABLE);
        }
        // After every clip, so a band reaching into the gap is not covered by
        // the next slice.
        if (s_blood) draw_blood(dx, all, s_count, h);
        drew = true;
    }
    // The white flash as the slices come and go.
    if (s_flashAt) {
        unsigned int t = plat_time_ms() - s_flashAt;
        if (t >= kFlashMs) s_flashAt = 0;
        else {
            float a = 1.0f - (float)t / kFlashMs;
            draw_white(dx, w, h, a * a * 0.9f);
            drew = true;
        }
    }
    return drew;
}
