# Survivor inventory portraits

The zombie mod's inventory uses the local survivor's selected character,
independent of Chris's scenario. The original game's selection stays unchanged
when the mod is off.

Portraits are prepared during `load_global_assets` and kept in the global
inventory texture for the session. They require no new distributed artwork,
disk cache or external executable at runtime.

| Character | Source |
| --- | --- |
| Chris | `Data/Statface.tim`, top left |
| Jill | `Data/Statface.tim`, top right |
| Rebecca | `Data/Statface.tim`, bottom right |
| Barry | Base USA `Movie/PU.mp4` or `PU.avi`, 208700 ms |
| Richard | Upper-body mesh and embedded texture from `Enemy/em1027.emd` |
| Enrico | Upper-body mesh and embedded texture from `Enemy/em1028.emd` |

Barry's crop is `(94, 10, 174, 174)` in the original 320x240 frame, resized to
30x30. Converted movies use the same crop scaled to their dimensions. The USA
timestamp is not applied to Japanese or Director's Cut edits. If the USA movie
is unavailable or cannot be decoded, Barry uses `Enemy/char12.emd` instead.

`ZombiePortraitImage.cpp` renders the shipped PC EMD torso and head (objects 0 and 1) from +X,
turned fifteen degrees, with the camera looking down eight degrees,
and framed as a headshot with just the shoulders,
with its embedded 8-bit TIM palette and texture. A CPU depth buffer resolves
the triangles at 120x120, then a box filter produces the 30x30 portrait. It
does not load an entity, change animation/camera state, consume model/texture
bank slots or touch the live player's joints. Unsupported or invalid model
data falls back to Chris's portrait, with a `[portrait]` debug trace.

The original 64x64 portrait atlas becomes 64x96 in the mod, retaining its
logical sprite metadata and the existing inventory draw path. Barry replaces
the unused radio tile; Richard and Enrico occupy the extra row.

`plat_video_read_frame` uses the existing FFmpeg video setup/seek/convert path
on Linux and Media Foundation for MP4 on Windows. Windows AVI extraction uses
VFW's installed AVI/Cinepak codec, since MCI playback does not expose pixels.
Frame extraction starts no audio and presents no frame.

Run the CPU checks with a 32-bit C++17 compiler, pointing at the shipped USA
tree (the game is not launched):

```sh
g++ -m32 -std=c++17 tests/test_zombie_portraits.cpp \
    src/game/mods/ZombiePortraitImage.cpp -o obj/test_zombie_portraits
obj/test_zombie_portraits bin/Release/USA obj/model-portraits.ppm
python3 tests/check_platform_boundary.py
```

`RE1_DEBUGLOG=1` records which source each generated portrait used. The
inventory appearance still needs user play-testing.
