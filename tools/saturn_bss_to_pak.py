#!/usr/bin/env python3
"""Saturn room backgrounds -> PC .pak.

The Saturn's ``STAGEn/ROOMxyy.BSS`` is a container of 320x240 **8 bpp** camera
frames with a per-frame 256-entry RGB555 palette (see ``saturn_room.py``: a
big-endian u32 offset table whose first entry is its own size, the first half
the palettes, the second half LZ77 frames stored as 8x8 cells).  The PC release
has no palettes: a background is one 320x240 16 bpp TIM wrapped in the game's
own LZW (``unpack_pakfile_``, 0x00425ab0) - see ``docs/PSX_BSS_AND_PC_PAK.md``.

So the conversion is: read the frame's palette, index it into RGB888, and hand
the result to the PC writer that ``bss_to_pak.py`` already uses for the DC's
arrange stages (that one starts from an MDEC bitstream and decodes to the same
320x240 RGB buffer, so the tail of the pipeline is shared verbatim).

This matters because the Saturn **re-drew some room art**: the Laboratory Tyrant
room (STAGE5/ROOM513.BSS, PC ``ROOM5130``) is redrawn on all eight cameras - the
tank vats are emptied, because the Saturn cannot do the PC's translucent
glass - and Chris' second Tyrant comes out of a second capsule in it.  The
overlay therefore has to carry that room's backgrounds, or Saturn mode would
show the PC's art.

Usage:
    python tools/saturn_bss_to_pak.py FILE.BSS OUTDIR --stage 5 --room 13
    python tools/saturn_bss_to_pak.py --tyrant-room     # -> assets/SATURN/Stage5
    python tools/saturn_bss_to_pak.py --verify          # which frames differ?
    python tools/saturn_bss_to_pak.py --bordered OUTDIR # the bordered cameras, from the disc

Bordered cameras.  The cave rooms, the two lab elevators and the mansion
elevator are shown on the bordered screen (MSF2_SCREEN_BORDER, set by the room
script).  The game then reads the background as a PACKED 316x236 bitmap - 316
pixels a row, rows back to back - out of the 320x240 buffer, and the PC's paks
for those cameras are stored that way.  The Saturn frame is a plain 320x240
picture whose top-left 316x236 is the PC's picture, so those frames are packed
before they are written (``BORDERED``, the same table as the migrator's
``kBorderedBgs`` in ``core/SaturnOverlay.cpp``).  Written unpacked they show as
diagonal streaks, four pixels of shear a row.

``--verify`` decodes every Saturn frame and the PC pak for the same room and
camera (through the game's own LZW) and reports the ones whose luma differs by
more than 16 on more than 5% of pixels.  Rooms the PC redrew and rooms where the
Saturn adds camera angles the PC has no pak for both show up there; the rest
agree to about one 8-bit step, which is the Saturn's slightly different palette.
"""

import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bss_to_pak import make_tim, lzw_pack, pak_name  # noqa: E402
from saturn_room import bss_backgrounds  # noqa: E402
from pak_view import PakDecoder, find_tims, parse_tim  # noqa: E402

SATURN_ROOT = os.path.join("assets", "SATURN_ISO_CONTENT")
PC_ROOT = os.path.join("assets", "USA")
WIDTH, HEIGHT = 320, 240

# The rooms the port overlays, as (stage dir, stage digit, room id).  Only rooms
# whose art the Saturn actually changed belong here: everything else falls back
# to the base tree, which is the whole point of the sparse overlay.
OVERLAY_ROOMS = [
    # STAGE5 room 0x13 - Laboratory B4, the Tyrant room. All eight cameras are
    # redrawn (emptied vats, extra floor tanks) and Chris' second Tyrant bursts
    # out of the second capsule at the back of the room.
    ("STAGE5", 5, 0x13),
]


# (stage digit, room id) -> camera bit mask.  Every PC picture that is packed
# (29 of the USA tree's 679, by row continuity: smooth at stride 316, not 320).
# Stage 7 room 0x00, the mansion elevator, reads stage 2's RC2000.
BORDERED = {
    (2, 0x00): 0x01,
    (3, 0x06): 0x0F, (3, 0x07): 0x7F, (3, 0x0B): 0xFF, (3, 0x0F): 0xFF,
    (5, 0x0D): 0x01, (5, 0x15): 0x01,
}
BORDER_W, BORDER_H = 316, 236


def frame_rgb(rgb, pix):
    """(256 palette triples, linear 8 bpp indices) -> 320x240x3 uint8 RGB."""
    pal = np.frombuffer(b"".join(rgb), dtype=np.uint8).reshape(256, 3)
    return pal[np.frombuffer(pix, dtype=np.uint8)]


def pack_bordered(img):
    """The top-left 316x236 of a 320x240x3 frame, rows back to back."""
    img = np.asarray(img, dtype=np.uint8).reshape(HEIGHT, WIDTH, 3)
    flat = np.zeros(WIDTH * HEIGHT * 3, dtype=np.uint8)
    flat[:BORDER_W * BORDER_H * 3] = img[:BORDER_H, :BORDER_W].reshape(-1)
    return flat.reshape(WIDTH * HEIGHT, 3)


def background_rgb(stage, room, k, rgb, pix):
    img = frame_rgb(rgb, pix)
    if (BORDERED.get((stage, room), 0) >> k) & 1:
        return pack_bordered(img)
    return img


def convert_room(bss_path, out_dir, stage_digit, room_id, dry=False):
    data = open(bss_path, "rb").read()
    frames = list(bss_backgrounds(data))
    if not frames:
        raise ValueError("%s: no camera frames" % bss_path)
    os.makedirs(out_dir, exist_ok=True)
    out = []
    for k, rgb, pix in frames:
        name = pak_name(str(stage_digit), room_id, k)
        pak = lzw_pack(make_tim(background_rgb(stage_digit, room_id, k, rgb, pix)))
        if not dry:
            with open(os.path.join(out_dir, name), "wb") as fh:
                fh.write(pak)
        out.append((name, len(pak)))
        print("  %-16s %7d B pak (camera %d)" % (name, len(pak), k))
    return out


def pak_luma(path):
    """Decode a PC background pak through the game's own LZW -> luma float32."""
    raw = PakDecoder(open(path, "rb").read()).run()
    offs = find_tims(raw)
    if not offs:
        return None
    pix, w, h, _info, _next = parse_tim(raw, offs[0])
    if (w, h) != (WIDTH, HEIGHT):
        return None
    a = np.array([p[:3] for p in pix[:WIDTH * HEIGHT]], dtype=np.float32)
    if a.shape[0] < WIDTH * HEIGHT:  # the length field counts pixels only
        a = np.concatenate([a, np.repeat(a[-1:], WIDTH * HEIGHT - a.shape[0], axis=0)])
    return a @ np.array([0.299, 0.587, 0.114], dtype=np.float32)


def cmd_verify():
    differ, missing, total = [], [], 0
    for stage_dir, stage, room in OVERLAY_ROOMS + _all_saturn_rooms():
        path = os.path.join(SATURN_ROOT, stage_dir, "ROOM%d%02X.BSS" % (stage, room))
        if not os.path.isfile(path):
            continue
        try:
            frames = list(bss_backgrounds(open(path, "rb").read()))
        except ValueError as e:                       # an unconverted PS1 MDEC frame
            print("%s: skipped (%s)" % (os.path.basename(path), e))
            continue
        for k, rgb, pix in frames:
            total += 1
            sat = frame_rgb(rgb, pix)
            sat = sat @ np.array([0.299, 0.587, 0.114], dtype=np.float32)
            pak = os.path.join(PC_ROOT, "Stage%d" % stage,
                               pak_name(str(stage), room, k).replace(".pak", ".PAK"))
            if not os.path.isfile(pak):
                missing.append((stage_dir, room, k))
                continue
            pc = pak_luma(pak)
            if pc is None:
                missing.append((stage_dir, room, k))
                continue
            pct = (np.abs(pc - sat) > 16).mean() * 100
            if pct > 5:
                differ.append((stage_dir, room, k, pct))
                print("DIFFER %s room %02X camera %d: %.1f%% of pixels off by >16"
                      % (stage_dir, room, k, pct))
    print("\n%d Saturn camera frames, %d differ from the PC pak, %d have no PC pak"
          % (total, len(differ), len(missing)))
    for stage_dir, room, k in missing:
        print("   Saturn-only: %s room %02X camera %d" % (stage_dir, room, k))
    return 1


def _all_saturn_rooms():
    out = []
    for d in sorted(os.listdir(SATURN_ROOT)):
        if not d.startswith("STAGE"):
            continue
        stage = int(d[-1])
        if stage > 5:
            continue
        sd = os.path.join(SATURN_ROOT, d)
        if not os.path.isdir(sd):
            continue
        for name in os.listdir(sd):
            if name.endswith(".BSS"):
                out.append((d, stage, int(name[5:7], 16)))
    return out


def cmd_tyrant_room(dry=False):
    out_dir = os.path.join("assets", "SATURN", "Stage5")
    for stage_dir, stage, room in OVERLAY_ROOMS:
        path = os.path.join(SATURN_ROOT, stage_dir, "ROOM%d%02X.BSS" % (stage, room))
        print("%s -> %s" % (path, out_dir))
        convert_room(path, out_dir, stage, room, dry)
    print("\nThese paks are the Saturn's own art for the room. The engine picks "
          "them up through the sparse SATURN/ overlay, so Mode=OG and Mode=DC "
          "keep the PC art.")


def cmd_bordered(out_root):
    """Write the bordered cameras' paks, reading the .BSS from the disc image
    (the extracted SATURN_ISO_CONTENT copy is incomplete)."""
    import saturn_iso
    iso = saturn_iso.Iso()
    for (stage, room), mask in sorted(BORDERED.items()):
        data = iso.get("/STAGE%d/ROOM%d%02X.BSS" % (stage, stage, room))
        out_dir = os.path.join(out_root, "Stage%d" % stage)
        os.makedirs(out_dir, exist_ok=True)
        for k, rgb, pix in bss_backgrounds(data):
            if not (mask >> k) & 1:
                continue
            name = pak_name(str(stage), room, k)
            with open(os.path.join(out_dir, name), "wb") as fh:
                fh.write(lzw_pack(make_tim(background_rgb(stage, room, k, rgb, pix))))
            print("  %s (packed 316x236)" % os.path.join(out_dir, name))


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 2
    if argv[0] == "--verify":
        return cmd_verify()
    if argv[0] == "--bordered" and len(argv) > 1:
        cmd_bordered(argv[1])
        return 0
    if argv[0] == "--tyrant-room":
        cmd_tyrant_room("--dry" in argv)
        return 0
    if len(argv) < 2:
        print(__doc__)
        return 2
    stage = room = None
    for i, a in enumerate(argv):
        if a == "--stage":
            stage = int(argv[i + 1])
        if a == "--room":
            room = int(argv[i + 1], 16)
    convert_room(argv[0], argv[1], stage, room)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
