#!/usr/bin/env python3
"""Create MPEG1/MP2 PS2 sidecars beside the PC movies without replacing inputs.

Usage: python3 tools/prepare_ps2_movies.py path/to/USA/Movie
MP4 is preferred when both AVI and MP4 share a basename. Existing sidecars
are preserved; --overwrite explicitly regenerates them atomically.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile


def convert(source: Path, output: Path, ffmpeg: str) -> None:
    with tempfile.TemporaryDirectory(prefix=".ps2-movie-", dir=output.parent) as temp:
        staged = Path(temp) / "movie.mpg"
        subprocess.run([
            ffmpeg, "-nostdin", "-v", "error", "-i", str(source),
            "-map", "0:v:0", "-map", "0:a:0?",
            "-vf", "scale=320:240:force_original_aspect_ratio=decrease:force_divisible_by=2,pad=320:240:(ow-iw)/2:(oh-ih)/2,fps=30",
            "-c:v", "mpeg1video", "-pix_fmt", "yuv420p", "-q:v", "5", "-g", "15", "-bf", "0",
            "-c:a", "mp2", "-ar", "44100", "-ac", "2", "-b:a", "128k",
            "-f", "mpeg", str(staged),
        ], check=True)
        staged.replace(output)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--overwrite", action="store_true")
    args = parser.parse_args()
    if not args.directory.is_dir():
        parser.error("directory does not exist")
    inputs: dict[str, Path] = {}
    for file in sorted(args.directory.rglob("*")):
        if file.suffix.lower() in (".avi", ".mp4") and file.is_file():
            key = str(file.with_suffix("")).lower()
            if key not in inputs or file.suffix.lower() == ".mp4":
                inputs[key] = file
    if not inputs:
        parser.error("no AVI or MP4 movies found")
    for source in inputs.values():
        output = source.with_suffix(".ps2.mpg")
        if output.exists() and not args.overwrite:
            print(f"keep {output}")
            continue
        convert(source, output, args.ffmpeg)
        print(f"created {output}")


if __name__ == "__main__":
    main()
