# Convert the INFESTED subtitle artwork (infested/images/infested.png) into the
# game's texture format: a 4-bit PSX TIM (16-colour CLUT at 0,0x1E0, index 0
# transparent) written to infested/images/infested.tim, and the same bytes
# baked into src/game/InfestedTitleImage.h with two dilated black masks the
# title draws as a feathered shadow. The letters get a solid black outline
# (OUTLINE pixels, palette index 1) so they read over the title background.
# Run from the repository root. Add --preview to write tools/infested-title-preview.png.
# No external Python packages required.
from pathlib import Path
import argparse, random, struct, zlib
parser = argparse.ArgumentParser(description="Convert the subtitle PNG into the title's TIM textures.")
parser.add_argument("--src", default="infested/images/infested.png", help="RGBA artwork")
parser.add_argument("--invert", action="store_true", help="invert the artwork's colours (outline and shadow stay black)")
parser.add_argument("--preview", action="store_true", help="also write an enlarged PNG preview")
args = parser.parse_args()

# The artwork's letters sit in a transparent band between two opaque black
# bars; only that band is searched for them.
BAND = (212, 812)
CONTENT_H = 36             # letter height on the 320x240 screen, as the old wordmark
OUTLINE = 1                # black outline width, in TIM pixels
WIDEN = 2                  # letters drawn this many times wider than the artwork's aspect
MARGIN_Y = OUTLINE + 3     # room for the outline and the shadow (radius 3)
MARGIN_X = MARGIN_Y * WIDEN

def load_png(path):
    d = Path(path).read_bytes()
    assert d[:8] == b'\x89PNG\r\n\x1a\n', "not a PNG"
    p, idat = 8, b''
    while p < len(d):
        n, t = struct.unpack('>I4s', d[p:p + 8]); c = d[p + 8:p + 8 + n]; p += 12 + n
        if t == b'IHDR': w, h, depth, ctype, _, _, interlace = struct.unpack('>IIBBBBB', c)
        elif t == b'IDAT': idat += c
    assert depth == 8 and ctype == 6 and interlace == 0, "expects 8-bit RGBA, not interlaced"
    raw = zlib.decompress(idat); stride = w * 4
    out = bytearray(h * stride); prev = bytearray(stride); i = 0
    for y in range(h):
        f = raw[i]; line = bytearray(raw[i + 1:i + 1 + stride]); i += 1 + stride
        for x in range(stride):
            a = line[x - 4] if x >= 4 else 0; b = prev[x]; c = prev[x - 4] if x >= 4 else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out[y * stride:(y + 1) * stride] = line; prev = line
    return w, h, out

sw, sh, src = load_png(args.src)
x0, x1, y0, y1 = sw, 0, sh, 0
for y in range(BAND[0], min(BAND[1], sh)):
    for x in range(sw):
        if src[(y * sw + x) * 4 + 3] > 128:
            x0 = min(x0, x); x1 = max(x1, x); y0 = min(y0, y); y1 = max(y1, y)
cw, ch = x1 - x0 + 1, y1 - y0 + 1
content_w = round(cw * CONTENT_H * WIDEN / ch)
W = (content_w + 2 * MARGIN_X + 3) & ~3  # 4 pixels per VRAM halfword
H = CONTENT_H + 2 * MARGIN_Y
ox = (W - content_w) // 2

# Area-average each target pixel (alpha-weighted colour, coverage = alpha).
rgb = [None] * (W * H)
for ty in range(CONTENT_H):
    sy0 = y0 + ty * ch // CONTENT_H; sy1 = max(sy0 + 1, y0 + (ty + 1) * ch // CONTENT_H)
    for tx in range(content_w):
        sx0 = x0 + tx * cw // content_w; sx1 = max(sx0 + 1, x0 + (tx + 1) * cw // content_w)
        r = g = b = a = n = 0
        for yy in range(sy0, sy1):
            row = yy * sw
            for xx in range(sx0, sx1):
                q = (row + xx) * 4; al = src[q + 3]
                r += src[q] * al; g += src[q + 1] * al; b += src[q + 2] * al; a += al; n += 1
        if a >= n * 128:
            c = (r / a, g / a, b / a)
            if args.invert: c = tuple(255 - v for v in c)
            rgb[(ty + MARGIN_Y) * W + tx + ox] = c

# 14 colours by k-means over the opaque pixels (index 0 stays transparent,
# index 1 is the outline's black).
opaque = [c for c in rgb if c]
rng = random.Random(1997)
centers = sorted(rng.sample(opaque, 14))
for _ in range(30):
    sums = [[0, 0, 0, 0] for _ in centers]
    for c in opaque:
        k = min(range(len(centers)), key=lambda i: sum((c[j] - centers[i][j]) ** 2 for j in range(3)))
        s = sums[k]; s[0] += c[0]; s[1] += c[1]; s[2] += c[2]; s[3] += 1
    centers = [(s[0] / s[3], s[1] / s[3], s[2] / s[3]) if s[3] else centers[i] for i, s in enumerate(sums)]
centers.sort(key=lambda c: c[0] + c[1] + c[2])
def to555(c):
    v = tuple(min(31, int(round(ch_ * 31 / 255))) for ch_ in c)
    v = v[0] | v[1] << 5 | v[2] << 10
    return v or 0x8000  # pure black needs the STP bit, or the GPU skips it
pal = [0, 0x8000] + [to555(c) for c in centers]
indices = [0 if c is None else 2 + min(range(14), key=lambda i: sum((c[j] - centers[i][j]) ** 2 for j in range(3)))
           for c in rgb]

# Outline: transparent pixels within OUTLINE (square) of a letter pixel.
letters = indices[:]
for y in range(H):
    for x in range(W):
        if not letters[y * W + x] and any(0 <= x + dx < W and 0 <= y + dy < H and letters[(y + dy) * W + x + dx]
                                          for dy in range(-OUTLINE, OUTLINE + 1) for dx in range(-OUTLINE * WIDEN, OUTLINE * WIDEN + 1)):
            indices[y * W + x] = 1

def tim4(palette, idx):
    pixels = bytes(idx[i] | idx[i + 1] << 4 for i in range(0, len(idx), 2))
    return (struct.pack('<IIIHHHH', 16, 8, 44, 0, 480, 16, 1) + struct.pack('<16H', *palette)
            + struct.pack('<IHHHH', 12 + len(pixels), 0, 0, W // 4, H) + pixels)
def c_array(name, data):
    return ('alignas(4) static unsigned char ' + name + '[] = {\n'
            + ''.join('    ' + ','.join(str(b) for b in data[i:i + 24]) + ',\n' for i in range(0, len(data), 24)) + '};\n')

tim = tim4(pal, indices)
Path('infested/images/infested.tim').write_bytes(tim)
text = ('// Generated by tools/gen_infested_title.py from infested/images/infested.png; do not edit by hand.\n'
        '#pragma once\n'
        'enum { kInfestedTitleWidth = %d, kInfestedTitleHeight = %d };\n' % (W, H) + c_array('kInfestedTitleTim', tim))
# Two dilation masks make a faint feathered black halo at runtime.
for layer, radius in enumerate((3, 1)):
    mask = [1 if any(0 <= x + dx < W and 0 <= y + dy < H and indices[(y + dy) * W + x + dx]
                     for dy in range(-radius, radius + 1) for dx in range(-radius * WIDEN, radius * WIDEN + 1)
                     if dx * dx + dy * dy * WIDEN * WIDEN <= radius * radius * WIDEN * WIDEN) else 0
            for y in range(H) for x in range(W)]
    text += '\n' + c_array('kInfestedShadowTim' + str(layer), tim4([0, 0x8000] + [0] * 14, mask))
Path('src/game/InfestedTitleImage.h').write_text(text)
print('crop %dx%d at (%d,%d) -> %dx%d TIM (letters %dx%d)' % (cw, ch, x0, y0, W, H, content_w, CONTENT_H))

def png(path, w, h, pixels):
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d))
    raw = b''.join(b'\0' + bytes(pixels[y * w * 3:(y + 1) * w * 3]) for y in range(h))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))
if args.preview:
    colors = [((p & 31) * 255 // 31, (p >> 5 & 31) * 255 // 31, (p >> 10 & 31) * 255 // 31) for p in pal]
    preview = []
    for y in range(H * 4):
        for x in range(W * 4):
            i = indices[(y // 4) * W + x // 4]; preview.extend(colors[i] if i else (5, 7, 14))
    png(Path('tools/infested-title-preview.png'), W * 4, H * 4, preview)
