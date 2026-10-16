# Match bumpers

Optional files live beside the executable, independent of the configured game
asset directory. Windows builds and the Linux packager copy these files from
this repository's `infested` folder when present.

```
infested/
  audio/helicopter.wav
  audio/survive.wav
  video/
    intro/chris.mp4
    intro/jill.mp4
    intro/barry.mp4
    intro/rebecca.mp4
    intro/richard.mp4
    intro/enrico.mp4
    victory/chris.mp4
    victory/jill.mp4
    victory/barry.mp4
    victory/rebecca.mp4
    victory/richard.mp4
    victory/enrico.mp4
    wesker_victory.mp4
```

Export MP4 files (silent, except `wesker_victory.mp4`, which keeps its
soundtrack) with
H.264, 8-bit `yuv420p`, constant 24 fps (30 fps also works). Recommended size:
1280 x 720. Intro files should be exactly three seconds (72 frames at 24 fps).
Export the two intro sounds as PCM WAV, ideally 16-bit stereo:
`helicopter.wav` plays once the new game's opening narration ("They have
escaped into the mansion...") starts and plays out on its own; `survive.wav`
plays when the intro releases the match, with the "SURVIVE!" banner, and plays
out into the match. The intro clips themselves are silent.

A human director's copy takes no part in any of this: it skips the opening
narration, the intro and both waits and plays from the moment its room is up,
so it can set up while the survivors pick their characters. The host still
runs the survivors' barrier, advancing it as their reports arrive; the match
clock and the director's unlock timers start only once every survivor is in
and released (`zm_net_bumper_released`). Against the AI director the host is
a survivor and waits like the others.

The roster settles before the opening narration: in multiplayer each copy
holds on the loading screen ("WAITING FOR SURVIVORS" after a second) until
every seated survivor has picked and reported in. Nothing waits after the
narration: each copy loads its clips, and once its room has faded in the intro
plays over it for three seconds, on that copy's own time. The copies meet
again only at the end: a copy that finishes first holds on the room (input
held, no slices; the same notice after a second) until every connected copy
has finished, and gameplay is released on all of them together. The roster
is refreshed from the host's at the release, for the ending's slices. Nobody moves while a clip plays: the pad, the
single-player AI survivor and the AI director are held. The mod's HUD (clock,
points, survivor HUD, room name, captions) stays hidden until the intro
releases the match; then "SURVIVE!" shows in the middle of the screen for a
second (the game's 8x14 font at 4x, red, popping in from 5x). The room's music
waits for the release too: a BGM channel (`g_SndBank`
0-2) started before then - by the room load or a room script - is only
recorded (SoundApi.cpp's `SetSndSlot` / `playSnd` ask
`zombie_mode_bgm_hold`), and starts from its beginning on the release unless
it was stopped or its bank freed meanwhile. Networking continues throughout. Rejoining an existing game does not replay its intro. The match
clock starts afterward.

Each survivor gets a slice over the live room - one survivor too - in seat
order, left to right. A slice is a parallelogram: top and bottom horizontal,
sides 15 degrees from vertical, 80% of the screen's height (10% margins above
and below), 20% of its width, 5 pixels apart at 480 lines. The group is centred
on the screen (narrowed only if three would not fit an unusually narrow
window). The slices start off screen and slide in over 0.55 s on their
clips' first frames, speeding up (the game's `sound/sliding.wav` as they
start): with an odd count the middle one comes down from the top (a lone slice
too), the ones left of it from the left edge and the ones right of it from the
right edge. As they crash together `sound/ending07.wav` plays, a white flash
goes off and the clips start; the intro's three seconds count from the crash.
A white flash also marks the slices leaving. The intro's slices wear
a bloody border, drawn procedurally (no art file): a thin ragged dark-red
outline round each slice that bleeds inward over the clip; each slice's shape
is fixed by its seat, the same on every copy. Victory slices stay plain. The AI-director
mode can include the host as a fourth survivor. Videos are cropped centrally to
fill each slice, never stretched. Keep faces/actions near the centre and leave
background around them for the narrow slices.

On a confirmed survivor escape, victory slices use the starting survivor roster,
including teammates who died. On all-survivors-dead, Wesker is not a slice:
the room fades to white (1.2 s), then the engine's own FMV player
(`VideoPlayback.cpp`, as `cmd_fmv_set` requests a movie) plays the clip full
screen with its own soundtrack, unskippable, fading in from white (1.2 s) (fitted
whole, letterboxed on black, never cropped). The game stands still meanwhile,
as under any movie. From the fade on the game stays silent: the movie's
cleanup leaves the room music it paused off instead of resuming it, no room
music can start again, and the Yawn fight's track is stopped. When it ends, its last frame (decoded again with
`plat_video_read_frame`) freezes - dimmed to half over 0.6 s - under the results screen,
which then draws over it instead of its white background. Time expiry, connection failure and impossible-escape outcomes retain
their existing results screen. Survivor end clips play over the room until the
longest finishes (maximum 30 seconds); shorter clips hold their last frame. The results
screen and its timers begin when the ending clip finishes. The intro's
clips are silent (`helicopter.wav` before them, `survive.wav` after); survivor end clips are silent; Wesker's sound is in its MP4.

Missing or unsupported videos leave their assigned slice black; if all videos
are absent, the visual is skipped. The intro still calls its sound and observes
the release barrier. Missing audio is silent. `[bumper]` debug log lines report
assets loaded/skipped and playback completion. All multiplayer copies must use
network protocol version 71 or newer compatible builds.

Manual verification: test one, two and three survivors; confirm character clips,
angles, centre cropping, shared sound and input lockout; then test a survivor
escape, all-survivors-dead, missing files and reconnecting mid-match. Run with
`RE1_DEBUGLOG=1` and retain `re1_debug.log` if playback or the barrier fails.

Decoder integration check: `tests/test_bumper_video.cpp` is a standalone console
test. Supply a short silent 24 fps H.264/yuv420p fixture; it checks three
simultaneous readers, shared-clock frames, EOF retention, cleanup, missing
assets, isolation from the existing frame reader, and start-barrier phases.
It does not initialize or launch the game.
