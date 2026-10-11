#!/usr/bin/env python3
"""Build DATA/MENUFONT.FNT from the Director's Cut title-option sheet.

  python tools/make_menu_font.py BT367OAB.TIM menufont.fnt [--preview out.png]

The sheet (7 cells of 256x80, 4bpp) is the only place the DC keeps its menu
lettering.  Every bright (colour 1) glyph in it is cut out and re-packed on a
256-wide 4bpp texture page as 16x16 cells in ASCII order, so the game can draw
arbitrary menu text with it.  Only the big menu letters the sheet really contains
exist: A B C D E G I L M N O P R S T U V W Y. F H J K Q X Z, 0-9, '-' and '.'
are hand-drawn in the same style (SYNTH_ART below); the sheet's own small
copyright-line glyphs are a different size and are not used.  Missing characters are drawn by the game's normal text routine.

File layout (little endian), read by src/game/MenuFont.cpp:
  +0x00 'MFNT'
  +0x04 u8 cellW(16) u8 cellH(16) u8 first(32) u8 count(96)
  +0x08 u8 height of the tallest glyph, u8 space width, u8 pad, u8 pad
  +0x0C u8 advance[count]       0 = glyph absent
  +0x6C  TIM, 4bpp + CLUT, 256 x 96  (colour 0 transparent, 1 = text)
"""
import struct, sys
from collections import deque

CHUNK, W, H = 0x2840, 256, 80
CW = CH = 16
FIRST, COUNT = 32, 96
# bright text of each cell, in reading order, per line (None = line to skip)
LINES = {
    0: ["PRESS ANY BUTTON", "(C)CAPCOM CO.,LTD.1996,1997", "(C)CAPCOM U.S.A.,INC.1996,1997 ALL RIGHTS RESERVED."],
    1: ["NEW GAME"], 2: ["LOAD GAME"],
    3: ["STANDARD"], 4: ["TRAINING"], 5: ["ADVANCED"],
}

# Letters the sheet never prints (F H J K Q X Z, digits, - and .) hand-drawn on the
# same grid: 11 rows, 3-pixel stems, to match the extracted menu letters.
SYNTH_ART = r'''
F:
#########
#########
###......
###......
########.
########.
###......
###......
###......
###......
###......
H:
###....###
###....###
###....###
###....###
##########
##########
###....###
###....###
###....###
###....###
###....###
J:
.....###
.....###
.....###
.....###
.....###
.....###
.....###
###..###
###..###
.#######
..#####.
K:
###....###
###...###.
###..###..
###.###...
######....
#####.....
######....
###.###...
###..###..
###...###.
###....###
Q:
...#####...
.#########.
.##.....##.
###.....###
###.....###
###.....###
###...#.###
###....####
.##....###.
.#########.
...#######.
X:
###.....###
###.....###
.###...###.
..###.###..
...#####...
....###....
...#####...
..###.###..
.###...###.
###.....###
###.....###
Z:
#########
#########
......###
.....###.
....###..
...###...
..###....
.###.....
###......
#########
#########
0:
..######..
.########.
###....###
###....###
###....###
###....###
###....###
###....###
###....###
.########.
..######..
1:
..###
.####
#####
..###
..###
..###
..###
..###
..###
..###
..###
2:
.########.
##########
###....###
.......###
.....####.
...####...
.####.....
###.......
###.......
##########
##########
3:
.########.
##########
###....###
.......###
..######..
..######..
.......###
###....###
###....###
##########
.########.
4:
......###.
.....####.
....#####.
...######.
..###.###.
.###..###.
###...###.
##########
##########
......###.
......###.
5:
##########
##########
###.......
###.......
########..
##########
.......###
.......###
###....###
##########
.########.
6:
..#######.
.#########
###.......
###.......
########..
##########
###....###
###....###
###....###
.#########
..#######.
7:
#########
#########
......###
.....###.
....###..
....###..
...###...
...###...
...###...
...###...
...###...
8:
.########.
##########
###....###
###....###
.########.
.########.
###....###
###....###
###....###
##########
.########.
9:
.########.
##########
###....###
###....###
##########
.#########
.......###
.......###
......####
##########
.#######..
-:
.......
.......
.......
.......
#######
#######
.......
.......
.......
.......
.......
.:
...
...
...
...
...
...
...
...
...
###
###
'''

def synth_glyphs():
    out, cur = {}, None
    for line in SYNTH_ART.strip().splitlines():
        if line.endswith(':') and len(line) == 2:
            cur = line[0]; out[cur] = []
        else:
            out[cur].append([1 if c == '#' else 0 for c in line])
    return {c: (rows, len(rows[0]), len(rows), True) for c, rows in out.items()}

def cell_pixels(d, c):
    b = c * CHUNK + 0x40
    px = [[0] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            v = d[b + y * 128 + x // 2]
            px[y][x] = (v >> 4) if x & 1 else v & 15
    return px

def segments(mask, axis_len, other_len, get):
    """runs of non-empty lines along one axis"""
    runs, start = [], None
    for i in range(axis_len):
        has = any(get(i, j) for j in range(other_len))
        if has and start is None: start = i
        if not has and start is not None: runs.append((start, i)); start = None
    if start is not None: runs.append((start, axis_len))
    return runs

def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    try:
        d = open(sys.argv[1], 'rb').read()
    except OSError as e:
        sys.exit('cannot open %s: %s' % (sys.argv[1], e))
    if len(d) != 7 * CHUNK:
        sys.exit('%s is %d bytes, but the Director\'s Cut menu sheet is %d bytes (7 cells of 256x80).\n'
                 'This is a different file - use BT367OAB.TIM from the DC disc, or use the ready-made MENUFONT.FNT.'
                 % (sys.argv[1], len(d), 7 * CHUNK))
    glyphs = {}   # char -> (bitmap rows list, width)
    for c, lines in LINES.items():
        px = cell_pixels(d, c)
        on = lambda y, x: px[y][x] == 1
        bands = segments(None, H, W, lambda y, x: on(y, x))
        if len(bands) != len(lines):
            print("cell %d: found %d text lines, expected %d - skipped" % (c, len(bands), len(lines)))
            continue
        for (y0, y1), text in zip(bands, lines):
            cols = segments(None, W, y1 - y0, lambda x, j: on(y0 + j, x))
            chars = [ch for ch in text if ch != ' ']
            # '(C)' is one (c) glyph, keep as 0xA9; multi-char tokens collapsed
            chars = []
            i = 0
            while i < len(text):
                if text.startswith("(C)", i): chars.append('\xa9'); i += 3
                elif text[i] == ' ': i += 1
                else: chars.append(text[i]); i += 1
            clean = [True] * len(cols)
            while len(cols) < len(chars):
                # glyphs that touch form one blob: cut the widest one where it is thinnest
                k = max(range(len(cols)), key=lambda i: cols[i][1] - cols[i][0])
                x0, x1 = cols[k]
                lo, hi = x0 + (x1 - x0) * 3 // 10, x0 + (x1 - x0) * 7 // 10
                cut = min(range(lo, max(hi, lo + 1)),
                          key=lambda x: sum(on(y, x) for y in range(y0, y1)))
                cols[k:k + 1] = [(x0, cut), (cut, x1)]
                clean[k:k + 1] = [False, False]
            if len(cols) != len(chars):
                print("cell %d line %r: %d blobs vs %d chars - skipped" % (c, text, len(cols), len(chars)))
                continue
            for (x0, x1), ch, cl in zip(cols, chars, clean):
                old = glyphs.get(ch)
                if old and not ((y1 - y0) > old[2] or ((y1 - y0) == old[2] and cl and not old[3])):
                    continue    # prefer an uncut glyph, then the tallest
                rows = [[1 if on(y, x) else 0 for x in range(x0, x1)] for y in range(y0, y1)]
                glyphs[ch] = (rows, x1 - x0, y1 - y0, cl)
    # the page may only hold glyphs <= 16 wide; the small copyright-line glyphs and
    # the big menu letters share chars (C, A, P, O, M ...): the tallest wins above.
    tall = max(g[2] for g in glyphs.values())
    for ch, g in synth_glyphs().items():
        if ch not in glyphs or glyphs[ch][2] < tall:
            glyphs[ch] = g
    page = [[0] * 256 for _ in range(96)]
    adv = [0] * COUNT
    for ch, (rows, w, h, _cl) in glyphs.items():
        code = 0xA9 if ch == '\xa9' else ord(ch)
        if not (FIRST <= code < FIRST + COUNT) or w > CW - 1 or h > CH:
            continue
        if h * 10 < tall * 9:
            continue      # small copyright-line glyphs would look wrong beside the big letters
        idx = code - FIRST
        cx, cy = (idx % 16) * CW, (idx // 16) * CH
        for y, row in enumerate(rows):
            for x, v in enumerate(row):
                if v: page[cy + y][cx + x] = 1
        adv[idx] = w + 2
    space = 8
    adv[0] = space
    # 4bpp TIM, 256x96
    pix = bytearray()
    for y in range(96):
        for x in range(0, 256, 2):
            pix.append(page[y][x] | (page[y][x + 1] << 4))
    pal = struct.pack('<16H', 0, 0xB9CE, *([0] * 14))
    tim = struct.pack('<II', 0x10, 8)
    tim += struct.pack('<IHHHH', 12 + 32, 0, 0x1E0, 16, 1) + pal
    tim += struct.pack('<IHHHH', 12 + len(pix), 0, 0, 64, 96) + bytes(pix)
    out = b'MFNT' + bytes([CW, CH, FIRST, COUNT, tall, space, 0, 0]) + bytes(adv) + tim
    open(sys.argv[2], 'wb').write(out)
    have = ''.join(chr(FIRST + i) if FIRST + i < 127 else '(c)' for i in range(COUNT) if adv[i] and i)
    print("wrote %s: %d glyphs: %s" % (sys.argv[2], len([a for a in adv if a]) - 1, have))
    if '--preview' in sys.argv:
        from PIL import Image
        im = Image.new('L', (256, 96))
        im.putdata([255 if page[y][x] else 40 for y in range(96) for x in range(256)])
        im.resize((768, 288), Image.NEAREST).save(sys.argv[sys.argv.index('--preview') + 1])

if __name__ == '__main__':
    main()
