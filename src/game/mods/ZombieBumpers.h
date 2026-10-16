#pragma once
// Silent sliced match cinematics over the live room (Wesker's full screen). Presentation runs after scene rendering.
void zm_bumpers_reset(void);
void zombie_mode_bumper_roster_poll(void);
void zombie_mode_bumper_intro(void);
void zm_bumpers_match_end(int reason);
void zm_bumpers_frame(void);
// While a clip plays nobody moves: the pad, the AI survivor and the AI director are held.
bool zm_bumpers_hold_input(void);
// SoundApi.cpp: the room's music is held until the intro releases the match.
bool zombie_mode_bgm_hold(int bank);
void zombie_mode_bgm_stopped(int bank);
// The "SURVIVE!" banner for a second after the intro (zombie_mode_draw_overlay).
void zm_bumpers_draw_hud(void);
// From the room's fade-in to the intro's release: the HUD stays hidden.
bool zm_bumpers_intro_playing(void);
// The ending clip is up (or about to be): the results screen waits for it.
bool zm_bumpers_end_playing(void);
bool zombie_mode_bumper_draw(void);
// Wesker's frozen last frame, drawn under the 2D sprites so the results show over it.
bool zm_bumpers_backdrop(void);
void zombie_mode_bumper_draw_backdrop(void);
// die_state: the match's screen is left; free the clips.
void zombie_mode_bumper_leave(void);
// Before the opening narration: wait until every survivor has picked, so the
// intro's roster is settled; game_loop waits while this is in progress.
void zombie_mode_narration_wait(void);
// A human director's copy skips the new game's opening narration: it plays
// at once (display_game_loading_message leaves before showing it).
bool zombie_mode_narration_skip(void);
bool zombie_mode_narration_held(void);
// The opening narration starts: helicopter.wav under it.
void zombie_mode_narration_sound(void);

// Wesker's ending goes through the engine's FMV player (VideoPlayback.cpp), so
// it plays with its own soundtrack. TRUE from fmv_path once per request with
// the movie's file; fmv_clear starts it on white; fmv_frame draws each frame
// (fitted, fading in from white); fmv_playing makes it unskippable.
bool zombie_mode_fmv_path(char* out, unsigned int size);
void zombie_mode_fmv_clear(void);
bool zombie_mode_fmv_frame(unsigned int frameTexture, int frameMs);
bool zombie_mode_fmv_playing(void);
// The movie's cleanup leaves the music it paused off (instead of resuming it).
void zombie_mode_fmv_silence(void);
// From Wesker's fade to white until the game is left: no music (the room's is
// held by zombie_mode_bgm_hold, the Yawn fight's stopped).
bool zm_bumpers_silenced(void);
