#!/usr/bin/env bash
# Host checks for the PS2 rasterizer and the real movie conversion/decoder.
set -euo pipefail
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_dir"
out="$repo_dir/build/ps2-tests"
mkdir -p "$out"
host_cxx="${HOST_CXX:-g++}"
flags=(-std=c++17 -O1 -g -Wall -Wextra -Isrc -fsanitize=address,undefined -fno-omit-frame-pointer)
"$host_cxx" "${flags[@]}" tests/ps2_rasterizer_test.cpp -o "$out/rasterizer-test"
"$out/rasterizer-test"
"$host_cxx" "${flags[@]}" tests/ps2_movie_test.cpp -o "$out/movie-test"
fixture="$(mktemp -d "$out/movies.XXXXXX")"
trap 'rm -rf -- "$fixture"' EXIT
ffmpeg -nostdin -v error -f lavfi -i testsrc2=size=320x240:rate=10 \
    -f lavfi -i sine=frequency=440:sample_rate=22050 -t 1 \
    -c:v mpeg4 -c:a pcm_s16le "$fixture/test.avi"
python3 tools/prepare_ps2_movies.py "$fixture"
"$out/movie-test" "$fixture/test.ps2.mpg"
python3 tools/prepare_ps2_movies.py "$fixture"
python3 tests/check_platform_boundary.py
