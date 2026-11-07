#!/usr/bin/env bash
set -euo pipefail
: "${PS2DEV:?Set PS2DEV to your ps2dev installation}"
export PS2SDK="${PS2SDK:-$PS2DEV/ps2sdk}"
export GSKIT="${GSKIT:-$PS2DEV/gsKit}"
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cmake_bin="${CMAKE:-cmake}"
"$cmake_bin" -S "$repo_dir" -B "$repo_dir/build/ps2" \
    -DCMAKE_TOOLCHAIN_FILE="$repo_dir/cmake/ps2-toolchain.cmake" -DCMAKE_BUILD_TYPE=Release
"$cmake_bin" --build "$repo_dir/build/ps2" --parallel "${JOBS:-4}"
"$PS2DEV/ee/bin/mips64r5900el-ps2-elf-size" "$repo_dir/build/ps2/residentevil.elf"
