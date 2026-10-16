"""Check lesson-room pickups stay on the floor reachable from its entrance."""
from collections import deque
from pathlib import Path
import re
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import gen_spawn_spots as geometry
source = (ROOT / 'src/game/mods/ZombieRandom.cpp').read_text()
table = source.split('static const short kLessonPickupSpots[3][3] = {', 1)[1].split('};', 1)[0]
spots = [tuple(map(int, p)) for p in re.findall(r'\{\s*(\d+),\s*(\d+),\s*(\d+)\s*\}', table)]
assert len(spots) == 3
rdt = geometry.load_rdt(str(ROOT / 'bin/Release/USA'), 6, 12)
if rdt is None:
    raise RuntimeError('This geometry regression requires the shipped USA ROOM70C0.RDT.')
blocks, steps = geometry.boundaries(rdt)
zones = geometry.camera_zones(rdt)
# Use the logged main-floor arrival, rather than seeding every door's separate floor.
queue = deque([(10400 // 200, 18400 // 200)])
seen, reachable = set(), set()
while queue:
    cell = queue.popleft()
    if cell in seen:
        continue
    seen.add(cell)
    x,z = cell[0]*200+100,cell[1]*200+100
    if not (0 <= x <= 30000 and 0 <= z <= 30000):
        continue
    if geometry.blocked(x,z,blocks):
        continue
    reachable.add(cell)
    queue.extend((cell[0]+dx,cell[1]+dz) for dx,dz in [(1,0),(-1,0),(0,1),(0,-1)])
assert (23500//200,2300//200) not in reachable, 'The reported bad spot must reproduce the disconnected floor.'
for x,y,z in spots:
    assert y == 0 and geometry.in_camera(x,z,zones)
    assert not geometry.blocked(x,z,blocks)
    assert (x//200,z//200) in reachable, (x,z)
    # Native descent/piano interaction occupies x=16650..19050,z=2570..4650.
    assert x+500 < 16650 or x-500 > 19050 or z+500 < 2570 or z-500 > 4650
for i,a in enumerate(spots):
    for b in spots[i+1:]:
        assert abs(a[0]-b[0]) >= 1000 or abs(a[2]-b[2]) >= 1000
print('All three lesson-room pickup positions reachable from the main entrance; bad logged spot rejected.')
