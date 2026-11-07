#!/usr/bin/env bash
set -euo pipefail
: "${PS2DEV:?Set PS2DEV to your ps2dev installation}"
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_dir"
out=dist/ps2
mkdir -p "$out"
"$PS2DEV/ee/bin/mips64r5900el-ps2-elf-strip" --strip-debug \
    -o "$out/residentevil.elf" build/ps2/residentevil.elf
# Packaging must not replace a user's settings from a previous test.
if [[ ! -e "$out/config.ini" ]]; then cp config.ps2.ini "$out/config.ini"; fi
cp docs/PS2_PORT.md "$out/LEIA-ME.md"
cp LICENSE "$out/LICENSE"
cp src/third_party/pl_mpeg.LICENSE.txt "$out/PL_MPEG-LICENSE.txt"
ps2sdk_dir="${PS2SDK:-$PS2DEV/ps2sdk}"
mkdir -p "$out/licenses"
cp "$ps2sdk_dir/LICENSE" "$out/licenses/PS2SDK-AFL.txt"
cp "$ps2sdk_dir/ports/share/licenses/SDL2/LICENSE.txt" "$out/licenses/SDL2.txt"
python3 - <<'PY'
import hashlib
from pathlib import Path
import zipfile
out = Path('dist/ps2')
digest = hashlib.sha256((out / 'residentevil.elf').read_bytes()).hexdigest()
(out / 'SHA256SUMS').write_text(f'{digest}  residentevil.elf\n')
archive = Path('dist/resident-evil-ps2-experimental.zip')
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
    for name in ('residentevil.elf', 'config.ini', 'LEIA-ME.md', 'LICENSE', 'PL_MPEG-LICENSE.txt', 'SHA256SUMS'):
        z.write(out / name, name)
    for file in sorted((out / 'licenses').glob('*')):
        z.write(file, str(file.relative_to(out)))
    # Include the corresponding game sources and PS2 build/test scripts.
    # Generated files and commercial assets are deliberately outside these roots.
    for directory in ('src', 'cmake', 'tests'):
        for file in sorted(Path(directory).rglob('*')):
            if file.is_file() and '__pycache__' not in file.parts:
                z.write(file, f'source/{file}')
    for name in ('CMakeLists.txt', 'LICENSE', 'README.md', 'AGENTS.md', 'config.ps2.ini',
                 'docs/PS2_PORT.md', 'tools/build_ps2.sh', 'tools/package_ps2.sh',
                 'tools/prepare_ps2_movies.py'):
        z.write(name, f'source/{name}')
print(archive)
PY
