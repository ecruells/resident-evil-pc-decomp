"""Convert a folder's WAVs to RE1 voice audio: mono, 22050 Hz, 16-bit PCM.

Usage: python tools/convert_voice_wavs.py "C:/recordings"
Requires ffmpeg on PATH, or --ffmpeg /path/to/ffmpeg.
Writes to <input>/converted by default, preserving filenames and originals.
Use --recursive to include character subfolders, preserving their layout.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
import wave


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path)
    parser.add_argument("--output", type=Path, help="Output folder (default: input/converted)")
    parser.add_argument("--recursive", action="store_true")
    parser.add_argument("--ffmpeg", default="ffmpeg", help="ffmpeg executable path")
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
    candidates = source.rglob("*") if args.recursive else source.iterdir()
    files = sorted(p for p in candidates if p.is_file() and p.suffix.lower() == ".wav"
                   and output not in p.parents)
    if not files:
        print("No WAV files found.")
        return 0
    converted = skipped = failed = 0
    for original in files:
        target = output / original.relative_to(source)
        if target.exists():
            print(f"SKIP (already exists): {target}")
            skipped += 1
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        # Convert and validate a temporary file before publishing the output.
        with tempfile.TemporaryDirectory(prefix="voice-convert-", dir=target.parent) as temp:
            pending = Path(temp) / "converted.wav"
            result = subprocess.run([
                ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin", "-n",
                "-i", str(original), "-map", "0:a:0", "-vn", "-ac", "1",
                "-ar", "22050", "-c:a", "pcm_s16le", "-map_metadata", "-1",
                "-f", "wav", str(pending),
            ], capture_output=True, text=True)
            try:
                if result.returncode:
                    raise ValueError(result.stderr.strip() or "ffmpeg failed")
                with wave.open(str(pending), "rb") as audio:
                    if (audio.getnchannels(), audio.getframerate(), audio.getsampwidth(),
                            audio.getcomptype()) != (1, 22050, 2, "NONE") or audio.getnframes() == 0:
                        raise ValueError("Output is not a nonempty mono 22050 Hz 16-bit PCM WAV")
                # Exclusive creation also protects an output made during conversion.
                with target.open("xb") as dest, pending.open("rb") as src:
                    shutil.copyfileobj(src, dest)
            except (OSError, ValueError, wave.Error) as error:
                print(f"FAIL: {original}: {error}")
                failed += 1
                continue
        print(f"OK: {original.name} -> {target}")
        converted += 1
    print(f"Converted {converted}; skipped {skipped}; failed {failed}. Originals preserved.")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
