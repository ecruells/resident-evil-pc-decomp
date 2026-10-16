"""Convert a folder's videos to Infested bumper MP4s (docs/INFESTED_BUMPERS.md).

Usage: python tools/convert_bumper_videos.py "C:/renders" [--output infested/video]
Requires ffmpeg and ffprobe on PATH, or --ffmpeg /path/to/ffmpeg.
Output: H.264, 8-bit yuv420p, constant 24 fps, .mp4 (faststart), silent.
Writes to <input>/converted by default, preserving the folder layout and names
(lower-cased, so Chris.mov becomes chris.mp4); originals are left alone.

Length: a file under a folder named "intro" (or every file, with --intro) is
made exactly 3 seconds, 72 frames: trimmed, or held on its last frame. Others
are cut to 30 seconds, the longest an end clip plays. --size WxH resizes and
centre-crops to fill that size; by default the aspect is kept and only clips
larger than 1280x720 are scaled down to fit it.

Sound: the survivor slices play silent (the intro's sounds are audio/helicopter.wav and audio/survive.wav),
so their audio is dropped. wesker_victory plays through the game's FMV player
with its own soundtrack, so it keeps its audio (AAC stereo 44100 Hz); so does
any clip with --keep-audio.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

VIDEO_EXTENSIONS = {".mp4", ".mov", ".mkv", ".avi", ".webm", ".m4v", ".wmv", ".mpg", ".mpeg", ".gif"}
FPS = 24
INTRO_FRAMES = 72           # 3 s at 24 fps
END_MAX_FRAMES = 30 * FPS   # end clips play at most 30 s
FIT_W, FIT_H = 1280, 720


def video_filter(intro, size):
    steps = [f"fps={FPS}"]
    if size:
        w, h = size
        steps += [f"scale={w}:{h}:force_original_aspect_ratio=increase", f"crop={w}:{h}"]
    else:
        steps.append(f"scale='min({FIT_W},iw)':'min({FIT_H},ih)':force_original_aspect_ratio=decrease")
        # yuv420p needs even dimensions.
        steps.append("scale=trunc(iw/2)*2:trunc(ih/2)*2")
    steps.append("setsar=1")
    if intro:
        # Hold the last frame long enough that a short clip still reaches 72 frames.
        steps.append(f"tpad=stop_mode=clone:stop={INTRO_FRAMES}")
    steps.append("format=yuv420p")
    return ",".join(steps)


def probe(ffprobe, path):
    result = subprocess.run([
        ffprobe, "-v", "error", "-count_frames", "-show_entries",
        "stream=codec_type,codec_name,pix_fmt,r_frame_rate,nb_read_frames,width,height",
        "-of", "default=nw=1", str(path),
    ], capture_output=True, text=True)
    if result.returncode:
        raise ValueError(result.stderr.strip() or "ffprobe failed")
    streams, current = [], None
    for line in result.stdout.splitlines():
        key, _, value = line.partition("=")
        if key == "codec_name":
            current = {}
            streams.append(current)
        if current is not None:
            current[key] = value
    return streams


def parse_size(text):
    try:
        w, h = (int(v) for v in text.lower().split("x"))
    except ValueError:
        raise argparse.ArgumentTypeError("size must look like 1280x720")
    if w <= 0 or h <= 0 or w % 2 or h % 2:
        raise argparse.ArgumentTypeError("size must be positive and even")
    return w, h


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("folder", type=Path)
    parser.add_argument("--output", type=Path, help="Output folder (default: input/converted)")
    parser.add_argument("--recursive", action="store_true", help="Include subfolders (intro/, victory/)")
    parser.add_argument("--intro", action="store_true", help="Make every clip a 3-second intro")
    parser.add_argument("--size", type=parse_size, help="Fill exactly WxH, centre-cropped (e.g. 1280x720)")
    parser.add_argument("--crf", type=int, default=20, help="x264 quality, lower is better (default 20)")
    parser.add_argument("--overwrite", action="store_true", help="Replace existing outputs")
    parser.add_argument("--keep-audio", action="store_true",
                        help="Keep every clip's soundtrack (wesker_victory always keeps its own)")
    parser.add_argument("--ffmpeg", default="ffmpeg", help="ffmpeg executable path (ffprobe beside it)")
    args = parser.parse_args()
    source = args.folder.resolve()
    if not source.is_dir():
        parser.error(f"Input folder does not exist: {source}")
    output = (args.output or source / "converted").resolve()
    if output == source or output in source.parents:
        parser.error("Output must be a separate folder, outside or below the input folder.")
    ffmpeg = shutil.which(args.ffmpeg)
    if not ffmpeg:
        parser.error("ffmpeg was not found. Add it to PATH or pass --ffmpeg with its full path.")
    ffprobe = shutil.which(str(Path(ffmpeg).with_name("ffprobe"))) or shutil.which("ffprobe")
    if not ffprobe:
        parser.error("ffprobe was not found beside ffmpeg or on PATH.")
    candidates = source.rglob("*") if args.recursive else source.iterdir()
    files = sorted(p for p in candidates if p.is_file() and p.suffix.lower() in VIDEO_EXTENSIONS
                   and output not in p.parents)
    if not files:
        print("No video files found.")
        return 0
    converted = skipped = failed = 0
    for original in files:
        relative = original.relative_to(source)
        target = output / relative.parent / (original.stem.lower() + ".mp4")
        if target.exists() and not args.overwrite:
            print(f"SKIP (already exists): {target}")
            skipped += 1
            continue
        folders = (source.name,) + relative.parts[:-1]
        intro = args.intro or any(part.lower() == "intro" for part in folders)
        frames = INTRO_FRAMES if intro else END_MAX_FRAMES
        keep_audio = args.keep_audio or original.stem.lower() == "wesker_victory"
        audio_args = (["-map", "0:a:0?", "-c:a", "aac", "-b:a", "192k", "-ac", "2", "-ar", "44100", "-shortest"]
                      if keep_audio else ["-an"])
        target.parent.mkdir(parents=True, exist_ok=True)
        # Convert and validate a temporary file before publishing the output.
        with tempfile.TemporaryDirectory(prefix="bumper-convert-", dir=target.parent) as temp:
            pending = Path(temp) / "converted.mp4"
            result = subprocess.run([
                ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin", "-y",
                "-i", str(original), "-map", "0:v:0", *audio_args, "-sn", "-dn",
                "-vf", video_filter(intro, args.size), "-fps_mode", "cfr", "-r", str(FPS),
                "-frames:v", str(frames),
                "-c:v", "libx264", "-preset", "slow", "-crf", str(args.crf),
                "-profile:v", "high", "-pix_fmt", "yuv420p",
                "-map_metadata", "-1", "-movflags", "+faststart", str(pending),
            ], capture_output=True, text=True)
            try:
                if result.returncode:
                    raise ValueError(result.stderr.strip() or "ffmpeg failed")
                streams = probe(ffprobe, pending)
                video = [s for s in streams if s.get("codec_type") == "video"]
                audio = [s for s in streams if s.get("codec_type") == "audio"]
                if len(video) != 1 or len(streams) != 1 + len(audio) or len(audio) > (1 if keep_audio else 0):
                    raise ValueError("expected one video stream" + ("" if keep_audio else " and no audio"))
                v = video[0]
                count = int(v.get("nb_read_frames", 0) or 0)
                if (v.get("codec_name"), v.get("pix_fmt"), v.get("r_frame_rate")) != ("h264", "yuv420p", f"{FPS}/1"):
                    raise ValueError(f"unexpected format {v}")
                if count == 0 or (intro and count != INTRO_FRAMES):
                    raise ValueError(f"unexpected frame count {count}")
                if target.exists():
                    target.unlink()
                pending.replace(target)
            except (OSError, ValueError) as error:
                print(f"FAIL: {original}: {error}")
                failed += 1
                continue
        kind = ("intro" if intro else "clip") + (", with audio" if audio else "")
        print(f"OK ({kind}, {v['width']}x{v['height']}, {count} frames = {count / FPS:.2f} s): {target}")
        converted += 1
    print(f"Converted {converted}, skipped {skipped}, failed {failed}.")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
