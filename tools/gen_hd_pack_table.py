# gen_hd_pack_table.py - build the asset migrator's HD pack match table.
#
# The Classic REbirth texture packs for the Japanese PC release (Seamless HD
# Project) name every file after a CRC of the picture it replaces.
# Their backgrounds and masks are hashed from a low-res base set this project
# does not have, so the names cannot be recomputed from the game's own files
# (the rules, read out of Classic REbirth's ddraw.dll, are in
# docs/HD_TEXTURES.md). This tool pairs the pack's pictures with the game's by
# what they SHOW instead, and emits the pairs the migrator's HD pack importer
# (tools/asset_migrator/src/core/HdPack.cpp) uses:
#
#   python tools/gen_hd_pack_table.py <game USA dir> <pack hires dir> [more packs]
#          [--out tools/asset_migrator/src/core/HdPackTable.inc] [--report FILE]
#          [--dc <DC overlay dir>] [--jpn <game JPN dir>]
#
# <game USA dir> must hold the ORIGINAL Stage*/RC*.pak and Objspr/*.pak (a GOG
# install; not a tree migrated to WebP). Needs numpy and ffmpeg on PATH.
#
# It also pairs the packs' model skins (skin/, skin2/) with the .EMD files
# under Enemy/, one row per (pack picture, palette row, .EMD); see
# match_models below. The importer joins a model's two rows into the one
# picture the port names after the .EMD.
#
# Only confident pairs are written. A pair is kept when it is MUTUAL - the pack
# picture's closest game picture also has that pack picture as its closest -
# and the difference is under a threshold. That drops what cannot be told
# apart by picture: the pack's non-room screens, the "_XX" suffixed variants of
# one camera, and the nearly empty mask pages several rooms share. A wrong mask
# draws the wrong cut-outs over a room, so the mask threshold is the tighter
# one.
import argparse
import glob
import os
import struct
import subprocess
import sys
import zlib

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pak_view  # noqa: E402

BG_SIZE = (80, 60)      # thumbnail the pictures are compared at
MASK_SIZE = (128, 128)
BG_MAX_ERR = 9.0        # mean absolute difference, 0-255
# The 316x236 backgrounds (the bordered-screen rooms: the caves, RC2000,
# RC50D0, RC5150) were re-lit far harder in the Seamless HD Project: all 28
# mutual pairs are right (checked side by side) at 5-17.
BORDER_BG_MAX_ERR = 18.0
MASK_MAX_ERR = 4.0

# Pairs checked by eye (side by side with the game's picture), kept although
# they are over their limit: each is the MUTUAL closest pair, the pack's art
# is just retouched further from the original than the limits allow (the
# Seamless HD Project re-lit these rooms and repainted these doors). Raising
# the limits instead would let in the pairs that are wrong.
VERIFIED = {
    ('FFB94AC0', 'Stage1/RC1094'), ('07078ECC', 'Stage1/RC1095'),
    ('E134714E', 'Stage1/RC1097'), ('936E93EB', 'Stage5/RC5065'),
    ('1BBF276F', 'Objspr/OSP00060'), ('B0C8CE83', 'Objspr/OSP00092'),
    ('51E26C5F', 'Objspr/OSP00095'), ('67F32AFF', 'Objspr/OSP00250'),
    ('69B3DD51', 'Objspr/OSP02061'), ('CBE5E9A7', 'Objspr/OSP03127'),
    ('43CB5D47', 'Objspr/OSP04161'), ('3824801F', 'Objspr/OSP04165'),
    # Pages with the white filler block (game_pictures), repainted past the
    # mask limit: 5.3-6.9.
    ('07FE3925', 'Objspr/OSP00031'), ('332E8821', 'Objspr/OSP00093'),
    ('D7B7AB5B', 'Objspr/OSP00094'), ('CDED1F98', 'Objspr/OSP01282'),
    ('BD5ACB5B', 'Item_m1/DOOR02'), ('E296E4A9', 'Item_m1/DOOR04'),
    ('A73566CF', 'Item_m1/DOOR14'),
    # The slides' body text was re-typeset in the pack.
    ('7D8E82DB', 'Data/slide/01'), ('9881E5CD', 'Data/slide/02'),
    ('93FE1F7F', 'Data/slide/03'),
    # The pack page's frame scraps (top right) differ; the book is the same.
    ('48D95E9E', 'Item_m2/FILEI17'),
    # The arrange 2F terrace (room 212) camera 0's mask: the pack re-lit the
    # pillars, 4.17 against the 4.0 limit. One picture, six room files.
    ('2DCFFEEC', 'Stage2/ROOM2120_MASK0+Stage2/ROOM2121_MASK0+Stage9/ROOM9120_MASK0+'
                 'Stage9/ROOM9121_MASK0+StageE/ROOME120_MASK0+StageE/ROOME121_MASK0'),
}

# Pairs the user identified in game (2026-11-01), each checked side by side
# with the game's dump. They are written whether or not the matcher finds
# them: the room 304 corridor (RC3044) is lit differently in the pack; the
# others are file textures the matcher leaves out (the save screen's
# typewriter, the all-maps selector, the ending report and three of the
# Japanese reader's backdrops).
USER_BG_PAIRS = [
    ('73FF3601', 'Stage3/RC3044'),
]
USER_FILE_PAIRS = [
    ('D1D144A8', 'Data/TYPE00'),
    ('C9831239', 'Data/EN07'),
    ('4D64B07B', 'Item_m2/MAP0E'),
    ('2D82D6C8', 'Item_m2/FILEI07'),     # fax and orders
    ('2CFF1A06', 'Item_m2/FILEI11'),     # security system
    ('0F20AFFA', 'Item_m2/FILEI16'),     # plant 42 report
]
# Neptune: a one-row 128-wide skin, the left half of the pack page (EM100B and
# EM110B carry the same TIM).
USER_MODEL_PAIRS = [
    ('CAD6E2E5', 0, 'Enemy/EM100B'),
    ('CAD6E2E5', 0, 'Enemy/EM110B'),
]
# Textures the game names only by a room TIM's CRC or by the hash of the
# upload (HdPackKind::Named, installed as they are: each 128x256 picture is
# stretched over the square pack page). The courtyard map (room 300's item)
# has its palette darkened before the upload (CmdFunctions.cpp), so the game
# looks it up by the upload's hash. The guardhouse map (room 406) and the
# shelf (room 1F0's object 1) are named by their TIM; the hash dumps of them
# (ef3eccc3a0cb92b8, d2dc6e521a59ab2d) are an older build's.
USER_NAMED_PAIRS = [
    ('558DBD58', '82152f2276f2176d'),             # courtyard map, darkened
    ('ABEA20A8', 'RoomModels/667bc53f'),          # guardhouse map
    ('B5CE7F7A', 'RoomModels/99817e6a'),          # shelf
]


def area(img, w, h):
    hh, ww = img.shape[:2]
    return img[:hh // h * h, :ww // w * w].reshape(h, hh // h, w, ww // w, -1).mean((1, 3))


def thumb(img, w, h):
    """area(), for a picture whose size is not a multiple of the thumbnail's
    (a 316x236 background): box-filtered per channel."""
    hh, ww = img.shape[:2]
    if hh % h == 0 and ww % w == 0:
        return area(img, w, h)
    from PIL import Image
    return np.stack([np.asarray(Image.fromarray(np.ascontiguousarray(img[..., c]), 'F')
                                .resize((w, h), getattr(Image, "Resampling", Image).BOX), np.float32)
                     for c in range(img.shape[2])], -1)


def row_jump(words):
    """How much a picture changes from one row to the next (green channel)."""
    g = ((words >> 5) & 31).astype(np.float32)
    return float(np.abs(g[1:] - g[:-1]).mean())


def premultiply(rgba):
    a = rgba[..., 3:] / 255.0
    return np.concatenate([rgba[..., :3] * a, rgba[..., 3:]], -1)


def game_pictures(root, kind):
    """-> ([asset name without extension], features, [page (w, h)])"""
    names, feats, pages = [], [], []
    pattern = '/Stage*/[Rr][Cc]*.[Pp][Aa][Kk]' if kind == 'bgd' else '/Objspr/*.[Pp][Aa][Kk]'
    size = BG_SIZE if kind == 'bgd' else MASK_SIZE
    for path in sorted(glob.glob(root + pattern)):
        tim = bytes(pak_view.PakDecoder(open(path, 'rb').read()).run())
        if kind == 'bgd':
            # The bordered-screen rooms (MSF2_SCREEN_BORDER) show a 316x236
            # picture. Stage3/RC3062 says so in its header; RC3070-RC3074
            # carry it under a 320x240 header, which the game reads 316 wide
            # (display_image), so read 320 wide the rows shear. The reading
            # that changes less from row to row is the picture.
            w, h = struct.unpack_from('<HH', tim, 16)
            if (w, h) not in ((320, 240), (316, 236)):
                continue
            words = np.frombuffer(tim, '<u2', w * h, 20).reshape(h, w)
            if (w, h) == (320, 240):
                narrow = np.frombuffer(tim, '<u2', 316 * 236, 20).reshape(236, 316)
                if row_jump(narrow) < row_jump(words):
                    w, h, words = 316, 236, narrow
            alpha = np.full(words.shape, 255.0, np.float32)
        else:
            flags = struct.unpack_from('<I', tim, 4)[0]
            if (flags & 0xB) != 9:
                continue                      # every shipped mask is 8 bpp with a CLUT
            _, _, _, cw, ch = struct.unpack_from('<IHHHH', tim, 8)
            clut = np.frombuffer(tim, '<u2', cw * ch, 20)
            off = 20 + cw * ch * 2
            _, _, _, w, h = struct.unpack_from('<IHHHH', tim, off)
            idx = np.frombuffer(tim, np.uint8, w * 2 * h, off + 12).reshape(h, w * 2)
            words = clut[:256][idx]
            w *= 2                            # 8 bpp: two texels per halfword
            alpha = np.where(words == 0, 0.0, 255.0).astype(np.float32)
            # Seven pages (OSP00031, 00093, 00094, 01184, 04101, 04105 ...)
            # fill the part of the page no sprite uses with index 255 =
            # 0x7FFF, opaque white; the pack leaves it transparent. Compared
            # as it is, that white block outweighs the picture and the page
            # matched nothing. A 16x16 block of nothing but that filler is
            # left out (a lone white texel in the picture is kept).
            filler = (idx == 255) & (words == 0x7FFF)
            blocks = filler.reshape(h // 16, 16, w // 16, 16).all((1, 3))
            alpha[np.repeat(np.repeat(blocks, 16, 0), 16, 1)] = 0.0
        rgba = np.stack([(words & 31) * 8.0, ((words >> 5) & 31) * 8.0,
                         ((words >> 10) & 31) * 8.0, alpha], -1).astype(np.float32)
        rel = os.path.relpath(path, root).replace('\\', '/')
        names.append(os.path.splitext(rel)[0])
        feats.append(thumb(premultiply(rgba), size[0], size[1]))
        pages.append((w, h))
    return names, np.stack(feats), pages


def pack_pictures(packs, kind, part=(1, 1)):
    """The pack's pictures, or with part=(fw, fh) only their top-left 1/fw x
    1/fh: a pack mask is always a whole 256x256 page, and a 128x128 game mask
    is the top-left quarter of it."""
    size = BG_SIZE if kind == 'bgd' else MASK_SIZE
    crop = '' if part == (1, 1) else 'crop=iw/%d:ih/%d:0:0,' % part
    files = {}
    for pack in packs:
        folder = os.path.join(pack, kind)
        if not os.path.isdir(folder):
            continue
        for f in os.listdir(folder):
            stem, ext = os.path.splitext(f)
            if ext.lower() == '.webp':
                files.setdefault(stem.upper(), os.path.join(folder, f))
    names, feats = [], []
    for stem in sorted(files):
        raw = subprocess.run(['ffmpeg', '-v', 'error', '-i', files[stem], '-vf',
                              crop + 'scale=%d:%d:flags=area' % size, '-f', 'rawvideo',
                              '-pix_fmt', 'rgba', '-'], capture_output=True).stdout
        if len(raw) != size[0] * size[1] * 4:
            continue
        img = np.frombuffer(raw, np.uint8).reshape(size[1], size[0], 4).astype(np.float32)
        names.append(stem)
        feats.append(premultiply(img))
    return names, np.stack(feats)


def match(kind, game_root, packs, report):
    gnames, gfeats, gpages = game_pictures(game_root, kind)
    pnames, pfeats = pack_pictures(packs, kind)
    limit = BG_MAX_ERR if kind == 'bgd' else MASK_MAX_ERR
    if kind == 'bgd':
        limit = [BORDER_BG_MAX_ERR if page == (316, 236) else BG_MAX_ERR for page in gpages]
    err = np.empty((len(pnames), len(gnames)), np.float32)
    for i in range(len(pnames)):
        err[i] = np.abs(gfeats - pfeats[i][None]).mean((1, 2, 3))
    if kind == 'mask':
        # Classic REbirth hashes a mask as its whole 256x256 VRAM page, so a
        # pack mask always shows that page. A smaller Objspr page (128x128,
        # 155 of the 436) is its top-left part: compare it with that part,
        # and the importer cuts that part out (HdPack.cpp).
        for part in sorted(set((256 // w, 256 // h) for w, h in gpages) - {(1, 1)}):
            cols = [j for j, (w, h) in enumerate(gpages) if (256 // w, 256 // h) == part]
            pnames_p, pfeats_p = pack_pictures(packs, kind, part)
            assert pnames_p == pnames
            for i in range(len(pnames)):
                err[i, cols] = np.abs(gfeats[cols] - pfeats_p[i][None]).mean((1, 2, 3))
    pairs = mutual_pairs(kind, gnames, pnames, err, limit, report)
    if kind == 'mask':
        # A second round over what is left: a pack page whose closest game
        # page already has a far closer pack page (D7B7AB5B is 4.98 from
        # OSP04054, which took 819B6A20 at 0.48) is still the closest to its
        # own page (OSP00094, 6.87) once the paired ones are out.
        pairs += leftover_pairs(kind, gnames, pnames, err, limit, pairs, report)
    return pairs + same_picture_pairs(kind, gnames, gfeats, gpages, pairs, report)


def leftover_pairs(kind, gnames, pnames, err, limit, pairs, report):
    gpaired = set(g for _, g, _ in pairs)
    ppaired = set(p for p, _, _ in pairs)
    gi = [j for j, n in enumerate(gnames) if n not in gpaired]
    pi = [i for i, n in enumerate(pnames) if n not in ppaired]
    if not gi or not pi:
        return []
    lim = [limit[j] for j in gi] if isinstance(limit, list) else limit
    return mutual_pairs(kind + ' (leftovers)', [gnames[j] for j in gi], [pnames[i] for i in pi],
                        err[np.ix_(pi, gi)], lim, report)


# Two room files can show the same picture: the main hall's 2F cameras
# (Stage2/RC2030-RC2034) are Stage1/RC1220-RC1224 again. The pack has the
# picture once, and the mutual match gives it to one of them, so the other
# room was left without its HD background. A game picture that is the SAME
# picture as a paired one - not just a close one: under SAME_PICTURE_ERR (the
# main hall pairs differ by 0.5-1.3, compression noise and one small object;
# two different cameras by 20 and more), at the same size - takes that pair's
# pack picture too. Masks keep the tight limit: nearly empty mask pages are
# close to each other, and a wrong mask draws another room's cut-outs.
SAME_PICTURE_ERR = 1.5
SAME_MASK_ERR = 0.5


def same_picture_pairs(kind, gnames, gfeats, gpages, pairs, report):
    index = {n: i for i, n in enumerate(gnames)}
    paired = set(g for _, g, _ in pairs)
    limit = SAME_MASK_ERR if kind == 'mask' else SAME_PICTURE_ERR
    extra = []
    for name, g, e in pairs:
        i = index[g]
        diff = np.abs(gfeats - gfeats[i][None]).mean((1, 2, 3))
        for j in np.nonzero(diff < limit)[0]:
            g2 = gnames[j]
            if g2 in paired or gpages[j] != gpages[i]:
                continue
            paired.add(g2)
            extra.append((name, g2, e))
            if report:
                report.write('%s,%s,%s,%.2f,same picture as %s\n' % (kind, name, g2, e, g))
    if extra:
        print('%s: %d more game pictures that are the same picture as a paired one'
              % (kind, len(extra)))
    return extra


def mutual_pairs(kind, gnames, pnames, err, limit, report):
    """err[pack, game] -> [(pack name, game name, error)] for the pairs that
    are each other's closest picture and differ by less than `limit` (one
    value, or one per game picture)."""
    best_game = err.argmin(1)       # per pack picture
    best_pack = err.argmin(0)       # per game picture
    pairs, dropped = [], []
    for i, name in enumerate(pnames):
        g = int(best_game[i])
        e = float(err[i, g])
        lim = limit[g] if isinstance(limit, list) else limit
        if best_pack[g] == i and (e <= lim or (name, gnames[g]) in VERIFIED):
            pairs.append((name, gnames[g], e))
        else:
            dropped.append((name, gnames[g], e, 'not mutual' if best_pack[g] != i else 'too different'))
    print('%s: %d game pictures, %d pack pictures, %d confident pairs, %d dropped'
          % (kind, len(gnames), len(pnames), len(pairs), len(dropped)))
    if report:
        for name, asset, e in pairs:
            report.write('%s,%s,%s,%.2f,kept\n' % (kind, name, asset, e))
        for name, asset, e, why in dropped:
            report.write('%s,%s,%s,%.2f,%s\n' % (kind, name, asset, e, why))
    return pairs


# ---------------------------------------------------------------------------
# Director's Cut rooms
#
# The Seamless HD Project ships a second set beside hires/: bio1dc/hires/{bgd,
# mask}, the TeamX mod's Director's Cut camera angles. In this port those are
# the DC overlay's own files, so they are paired with the DC tree (the asset
# migrator's DC/ folder) and installed under HD/DC/:
#
#   - backgrounds: DC/Stage<n>/RC*.pak, like the base set;
#   - masks: the arrange rooms read theirs from the room file, not from
#     Objspr (load_room_masks), so a mask is named after the room file and the
#     camera, "Stage8/ROOM8000_MASK3" (src/game/RoomBackgroundWebp.h). Every
#     room file's masks are candidates - a pack mask of a base-stage room must
#     not be pushed onto an arrange room - but only the arrange stages (8-E),
#     the ones the game names, are written. The two character files of a room
#     usually carry the same masks; identical pictures are matched once.
# ---------------------------------------------------------------------------
ARRANGE_STAGE_DIGITS = '89ABCDE'


def dc_pack_dirs(packs):
    """The bio1dc/hires folder beside each pack's hires folder."""
    out = []
    for pack in packs:
        root = os.path.normpath(pack)
        if os.path.basename(root).lower() == 'hires':
            root = os.path.dirname(root)
        for d in os.listdir(root):
            if d.lower() == 'bio1dc':
                hires = next((h for h in os.listdir(os.path.join(root, d)) if h.lower() == 'hires'), None)
                if hires:
                    out.append(os.path.join(root, d, hires))
    return out


def dc_room_masks(dc_root):
    """-> [{'names': [...], 'w', 'h', 'rgba'}], one per distinct mask picture."""
    groups = {}
    for stage in sorted(os.listdir(dc_root)):
        if not stage.lower().startswith('stage'):
            continue
        for f in sorted(os.listdir(os.path.join(dc_root, stage))):
            if not f.lower().endswith('.rdt'):
                continue
            d = open(os.path.join(dc_root, stage, f), 'rb').read()
            for cam in range(d[1] if len(d) > 1 else 0):
                base = 0x94 + cam * 0x2C
                if base + 8 > len(d):
                    break
                tp = struct.unpack_from('<i', d, base + 4)[0]
                if tp <= 0 or tp + 20 > len(d) or struct.unpack_from('<I', d, tp)[0] != 0x10:
                    continue
                flags = struct.unpack_from('<I', d, tp + 4)[0]
                if (flags & 0xB) != 9:
                    continue
                clen, _, _, cw, ch = struct.unpack_from('<IHHHH', d, tp + 8)
                clut = np.frombuffer(d, '<u2', cw * ch, tp + 20)
                off = tp + 8 + clen
                _, _, _, w, h = struct.unpack_from('<IHHHH', d, off)
                w *= 2
                if off + 12 + w * h > len(d) or w > 256 or h > 256:
                    continue
                idx = np.frombuffer(d, np.uint8, w * h, off + 12).reshape(h, w)
                words = clut[:256][idx]
                key = (w, h, words.tobytes())
                g = groups.setdefault(key, {'names': [], 'w': w, 'h': h, 'words': words})
                g['names'].append('%s/%s_MASK%X' % (stage, os.path.splitext(f)[0], cam))
    for g in groups.values():
        words = g.pop('words')
        alpha = np.where(words == 0, 0.0, 255.0).astype(np.float32)
        g['rgba'] = np.stack([(words & 31) * 8.0, ((words >> 5) & 31) * 8.0,
                              ((words >> 10) & 31) * 8.0, alpha], -1).astype(np.float32)
    return list(groups.values())


def is_arrange(asset):
    stage = asset.split('/')[0]
    return len(stage) == 6 and stage[-1].upper() in ARRANGE_STAGE_DIGITS


def match_dc(dc_root, packs, report):
    dc_packs = dc_pack_dirs(packs)
    if not dc_packs:
        print('dc: no bio1dc/hires folder beside the packs - skipped')
        return [], []
    bgd = match('bgd', dc_root, dc_packs, report)

    # Masks: each pack picture is a 256x256 page at its own scale with the
    # texture in its top-left corner, so only that part is compared.
    rooms = dc_room_masks(dc_root)
    files = {}
    for pack in dc_packs:
        folder = os.path.join(pack, 'mask')
        if os.path.isdir(folder):
            for f in sorted(os.listdir(folder)):
                stem, ext = os.path.splitext(f)
                if ext.lower() == '.webp':
                    files.setdefault(stem.upper(), os.path.join(folder, f))
    pnames = sorted(files)
    regions = sorted(set((0, 0, g['w'], g['h']) for g in rooms))
    feats = {r: [] for r in regions}
    keep = []
    for n in pnames:
        pics = [picture(files[n], r) for r in regions]
        if any(x is None for x in pics):
            continue
        keep.append(n)
        for r, x in zip(regions, pics):
            feats[r].append(x)
    pnames = keep
    masks = []
    if pnames and rooms:
        feats = {r: np.stack(v) for r, v in feats.items()}
        err = np.empty((len(pnames), len(rooms)), np.float32)
        for j, g in enumerate(rooms):
            region = (0, 0, g['w'], g['h'])
            size = region_size(region)
            err[:, j] = np.abs(feats[region] - area(premultiply(g['rgba']), size[0], size[1])[None]).mean((1, 2, 3))
        pairs = mutual_pairs('dc mask', ['+'.join(g['names']) for g in rooms], pnames, err,
                             MASK_MAX_ERR, report)
        for name, joined, e in pairs:
            for asset in joined.split('+'):
                if is_arrange(asset):
                    masks.append((name, asset, e))
    print('dc mask: %d rows written (arrange rooms only)' % len(masks))
    return bgd, masks


# ---------------------------------------------------------------------------
# File textures: Data/, Item_m2/ (maps, item views), Item_m1/ (doors), Effspr/
#
# The game names a texture it builds straight from one file after the file
# (HdBufferScope / HdTextures_SetOwnerKey / HdRegionScope in
# src/system/HdTextures.h): HD/<TREE>/Data/STATUS.webp, .../item_m2/i00v.webp,
# .../item_m1/door00.webp, .../effspr/esp001.webp (the whole page; the game
# cuts its weapon-effect bands out of it). The Players/ weapons carry no
# texture of their own - they are drawn with the character's skin.
# Each file is rendered as its loader uploads it - a 4/8 bpp TIM in its first
# palette row, a 16 bpp TIM or a 320x240 .pix as it is - and paired with the
# pack by Classic REbirth's name of that picture first, then by picture
# (mutual closest under DATA_MAX_ERR). Classic REbirth hands a texture smaller
# than 256x256 over as a square page in one of two layouts: the texture in the
# top-left corner (the maps, ITEMBOXn), or the whole texture stretched over the
# page (every Item_m1 door: a 128x256 door fills a 1024x1024 picture). Both
# views are compared, and the row records which one holds - HdPackKind::File
# (the importer cuts the corner out) or HdPackKind::FileWhole (it takes the
# picture as it is). Files the game cuts up or assembles into pages are not
# named and are left to the dump route.
# ---------------------------------------------------------------------------
DATA_KINDS = ('misc', 'skin', 'skin2', 'bgd')
DATA_MAX_ERR = 6.5
# A repainted or recoloured picture (the ending stills, the select-screen
# backdrop) is still taken when it is the same picture by structure: mutual,
# under DATA_LOOSE_ERR, and the best-correlated candidate by a clear margin
# (normalized cross-correlation of the grey thumbnails, which colour grading
# does not change). Checked by eye.
DATA_LOOSE_ERR = 30.0
DATA_MIN_NCC = 0.75
DATA_NCC_MARGIN = 0.25
# The least correlation any picture match needs (checked by eye: the doors
# and ELE02 pass at 0.33-0.85, the wrong pairs sat at 0.00).
DATA_MIN_STRUCTURE = 0.3


def structure(a, b):
    """Normalized cross-correlation of two premultiplied thumbnails' grey
    levels, 0 when either is close to flat (nothing to correlate)."""
    ga = a[..., :3].mean(-1)
    gb = b[..., :3].mean(-1)
    ga = ga - ga.mean()
    gb = gb - gb.mean()
    na = float(np.sqrt((ga * ga).sum()))
    nb = float(np.sqrt((gb * gb).sum()))
    if na < 4.0 * np.sqrt(ga.size) or nb < 4.0 * np.sqrt(gb.size):
        return 0.0
    return float((ga * gb).sum() / (na * nb))
# Cut up or assembled by their loaders (not named), or never loaded by the
# port (the original's renderer-select logos, CONFIG, SIDEKEY3).
DATA_NOT_NAMED = {'item_all.pix', 'item_mix.pix', 'medal.pix', 'slide.tim', 'bt367oab.tim',
                  '3dfx.tim', 'matrox.tim', 'rend.tim', 'virgin.tim', 'config.tim',
                  'sidekey3.tim'}


# The folders whose files the game names, and the files in them it reads a
# texture from. A .ivm item view starts with its TIM; a .dor door file holds
# its TIM at the offset in its third header dword.
FILE_FOLDERS = {
    'data': ('.tim', '.pix'),
    'item_m2': ('.tim', '.pix', '.ivm'),
    'item_m1': ('.dor',),
    'effspr': ('.tim',),
}


def tim_words(d, base, row=0):
    """The image of the TIM at `base` as 15-bit words (in palette row `row`), or None."""
    if base < 0 or len(d) < base + 20 or struct.unpack_from('<I', d, base)[0] != 0x10:
        return None
    flags = struct.unpack_from('<I', d, base + 4)[0]
    off = base + 8
    clut = None
    if flags & 8:
        clen, _, _, cw, ch = struct.unpack_from('<IHHHH', d, base + 8)
        if row >= ch:
            return None
        clut = np.frombuffer(d, '<u2', cw, base + 20 + row * cw * 2)
        off = base + 8 + clen
    if off + 12 > len(d):
        return None
    _, _, _, iw, ih = struct.unpack_from('<IHHHH', d, off)
    bpp = (4, 8, 16)[flags & 3] if (flags & 3) < 3 else 0
    n = iw * 2 * ih
    if not bpp or off + 12 + n > len(d):
        return None
    raw = np.frombuffer(d, np.uint8, n, off + 12)
    if bpp == 16:
        return raw.view('<u2').reshape(ih, iw)
    if clut is None:
        return None
    if bpp == 8:
        return clut[raw.astype(np.int32)].reshape(ih, iw * 2)
    idx = np.stack([raw & 15, raw >> 4], -1).reshape(ih, iw * 4)
    return clut[idx.astype(np.int32)]


# The slide projector (src/game/SpriteRenderer.cpp, TexturePage_LoadImage)
# uploads Data/SLIDE.TIM as six 192x128 cells, cell n at ((n >> 1) * 192,
# (n & 1) * 128) in palette row n, and names cell n HD/<TREE>/Data/SLIDE/<nn>.
# Classic REbirth hands each over as the top-left corner of a 256x256 page.
SLIDE_CELL = (192, 128)


def slide_cells(root):
    """-> [(asset, words)] for the slide projector's cells, [] without SLIDE.TIM."""
    for folder in os.listdir(root):
        if folder.lower() != 'data' or not os.path.isdir(os.path.join(root, folder)):
            continue
        for f in os.listdir(os.path.join(root, folder)):
            if f.lower() != 'slide.tim':
                continue
            d = open(os.path.join(root, folder, f), 'rb').read()
            rows = struct.unpack_from('<H', d, 18)[0] if len(d) >= 20 else 0
            out = []
            cw, ch = SLIDE_CELL
            for n in range(min(rows, 8)):
                words = tim_words(d, 0, n)
                if words is None:
                    break
                x, y = (n >> 1) * cw, (n & 1) * ch
                if x + cw > words.shape[1] or y + ch > words.shape[0]:
                    break
                out.append(('%s/%s/%02d' % (folder, os.path.splitext(f)[0], n),
                            words[y:y + ch, x:x + cw]))
            return out
    return []


# The USA file reader's pages (src/game/MainMenu.cpp, pickup_unpack_list): each
# Item_m2/FILEM_*.PIX is an LZW pak container (a dword offset per page, the
# last one the file's end) of 256x256 4 bpp TIMs, one per page, and the game
# names page n HD/<TREE>/Item_m2/FILEM_X0/<nn>. Classic REbirth's English
# pages are these pages pixel for pixel, so most pair by name.
def filem_pages(root):
    """-> [(asset, words)] for every page of every FILEM_*.PIX."""
    out = []
    for folder in os.listdir(root):
        if folder.lower() != 'item_m2' or not os.path.isdir(os.path.join(root, folder)):
            continue
        for f in sorted(os.listdir(os.path.join(root, folder))):
            low = f.lower()
            if not (low.startswith('filem_') and low.endswith('.pix')):
                continue
            d = open(os.path.join(root, folder, f), 'rb').read()
            if len(d) < 64:
                continue
            offs = struct.unpack_from('<16I', d, 0)
            for n in range(16):
                if offs[n] == 0 or offs[n] >= len(d):
                    break
                words = tim_words(bytes(pak_view.PakDecoder(d[offs[n]:]).run()), 0)
                if words is not None:
                    out.append(('%s/%s/%02d' % (folder, os.path.splitext(f)[0], n), words))
    return out


# A USA page that shows a picture (the pass number, Barry's photo, the three
# pass codes) has no page in any pack: Classic REbirth runs the Japanese game,
# whose reader draws the picture as a 256x120 backdrop (Item_m2/FILEI*). The
# USA page holds the backdrop's pixels at BACKDROP_AT, scale 1, so the
# importer builds the HD page from the backdrop's pack picture
# (HdPackKind::Backdrop, composeBackdropPage in HdPack.cpp).
BACKDROP_AT = (24, 24)
BACKDROP_MAX_ERR = 12.0       # the five pages sit at 2.0-8.8
BACKDROP_MIN_TEXELS = 2000


def backdrop_rows(game_root, jpn_root, data):
    """-> [(pack name, page asset)] for the FILEM pages no pack has, from the
    backdrop rows in `data` whose picture the page shows."""
    if not jpn_root:
        return []
    paired = set(a.lower() for _, a, _, _ in data)
    pack_of = dict((a.lower(), n) for n, a, _, _ in data)
    backdrops = [(a, words_rgba(w)) for a, w in file_textures(jpn_root)
                 if os.path.basename(a).lower().startswith('filei') and a.lower() in pack_of]
    out = []
    bx, by = BACKDROP_AT
    for asset, words in filem_pages(game_root):
        if asset.lower() in paired:
            continue
        page = words_rgba(words)
        best = None
        for b, img in backdrops:
            bh, bw = img.shape[:2]
            h, w = min(bh, page.shape[0] - by), min(bw, page.shape[1] - bx)
            p = page[by:by + h, bx:bx + w]
            q = img[:h, :w]
            m = (p[..., 3] > 0) & (q[..., 3] > 0) & (p[..., :3].sum(-1) > 40)
            if m.sum() < BACKDROP_MIN_TEXELS:
                continue
            e = float(np.abs(p[..., :3] - q[..., :3])[m].mean())
            if best is None or e < best[0]:
                best = (e, b)
        if best is not None and best[0] <= BACKDROP_MAX_ERR:
            out.append((pack_of[best[1].lower()], asset))
            print('backdrop: %s <- %s (%.2f)' % (asset, best[1], best[0]))
    return out


def file_textures(root):
    """-> [(asset, words HxW uint16 array)] for the files the game names."""
    out = slide_cells(root) + filem_pages(root)
    for folder in sorted(os.listdir(root)):
        exts = FILE_FOLDERS.get(folder.lower())
        if exts is None or not os.path.isdir(os.path.join(root, folder)):
            continue
        for f in sorted(os.listdir(os.path.join(root, folder))):
            low = f.lower()
            ext = os.path.splitext(low)[1]
            if low in DATA_NOT_NAMED or ext not in exts:
                continue
            d = open(os.path.join(root, folder, f), 'rb').read()
            words = None
            if ext == '.pix':
                if len(d) == 320 * 240 * 2:
                    words = np.frombuffer(d, '<u2').reshape(240, 320)
            elif ext == '.dor':
                if len(d) >= 12:
                    words = tim_words(d, struct.unpack_from('<I', d, 8)[0])
            else:
                words = tim_words(d, 0)
            if words is not None:
                out.append(('%s/%s' % (folder, os.path.splitext(f)[0]), words))
    return out


def pack_page(path):
    """-> (picture premultiplied at page scale, square?): a square page to
    256x256, a 4:3 screen to 320 wide; None when it does not decode."""
    probe = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_entries',
                            'stream=width,height', '-of', 'csv=p=0', path],
                           capture_output=True, text=True).stdout.strip().split(',')
    if len(probe) != 2:
        return None
    pw, ph = int(probe[0]), int(probe[1])
    square = pw == ph
    sw, sh = (256, 256) if square else (320, 320 * ph // pw)
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-i', path, '-vf',
                          'scale=%d:%d:flags=area' % (sw, sh), '-f', 'rawvideo',
                          '-pix_fmt', 'rgba', '-'], capture_output=True).stdout
    if len(raw) != sw * sh * 4:
        return None
    return premultiply(np.frombuffer(raw, np.uint8).reshape(sh, sw, 4).astype(np.float32)), square


def page_views(img, square, w, h):
    """The views of a pack picture that can stand for a w x h texture, as
    (view, stretched?): whole when it has the texture's shape; for a square
    page holding a smaller texture, the page stretched over the texture and
    its top-left corner."""
    size = region_size((0, 0, w, h))
    ph, pw = img.shape[:2]
    page = square and w <= 256 and h <= 256 and (w, h) != (256, 256)
    views = []
    if pw * h == ph * w:
        views.append((area(img, size[0], size[1]), False))
    elif page:
        views.append((area(img, size[0], size[1]), True))
    if page:
        views.append((area(img[:h, :w], size[0], size[1]), False))
    return views


def view_error(view, game):
    """Mean difference of a pack view and a game picture (premultiplied
    thumbnails). A pack picture with no transparency at all draws the game's
    transparent texels black: the reader's FILEI backdrops and the slides key
    their black, and the pack page is opaque, so comparing the alpha would
    make the same picture look 38 apart. Only for a texture with something in
    it (the least filled backdrop is 21% opaque): a mostly empty one (the 32x32
    ARROR, 6%) would then be close to the black corner of any page."""
    a = game[..., 3]
    if (float(view[..., 3].min()) >= 254.0 and float(a.min()) < 254.0 and
            float(a.mean()) >= 0.15 * 255.0):
        game = game.copy()
        game[..., 3] = 255.0
    return float(np.abs(view - game).mean())


def match_data(game_root, packs, report, jpn_root=None):
    textures = file_textures(game_root)
    if jpn_root:
        # The files only the Japanese tree has (the reader's FILEI backdrops and
        # TEXTM pages): a row is installed into whichever tree has the file.
        have = set(a.lower() for a, _ in textures)
        textures += [t for t in file_textures(jpn_root) if t[0].lower() not in have]
    files = {}
    for pack in packs:
        for kind in DATA_KINDS:
            folder = os.path.join(pack, kind)
            if not os.path.isdir(folder):
                continue
            for f in sorted(os.listdir(folder)):
                stem, ext = os.path.splitext(f)
                if ext.lower() == '.webp':
                    files.setdefault(stem.upper(), os.path.join(folder, f))

    # rows: (pack name, asset, error, stretched over the whole page?)
    rows, todo = [], []
    for asset, words in textures:
        name = classic_rebirth_name(words)
        if name in files:
            # The name says which picture, not how the texture sits in it.
            stretched = False
            pic = pack_page(files[name])
            if pic is not None:
                w, h = words.shape[1], words.shape[0]
                size = region_size((0, 0, w, h))
                g = area(premultiply(words_rgba(words)), size[0], size[1])
                views = page_views(pic[0], pic[1], w, h)
                if views:
                    stretched = min(views, key=lambda v: view_error(v[0], g))[1]
            rows.append((name, asset, 0.0, stretched))
        else:
            todo.append((asset, words))
    exact = len(rows)
    used = set(r[0] for r in rows)
    names = [n for n in sorted(files) if n not in used]

    # The views of each pack picture per texture shape (page_views). Each
    # picture is decoded once, at page scale.
    shapes = sorted(set((w.shape[1], w.shape[0]) for _, w in todo))
    keep = []
    for n in names:
        pic = pack_page(files[n])
        if pic is None:
            continue
        keep.append((n, [page_views(pic[0], pic[1], w, h) for (w, h) in shapes]))
    if todo and keep:
        err = np.full((len(keep), len(todo)), 1e9, np.float32)
        ncc = np.zeros((len(keep), len(todo)), np.float32)
        stretch = np.zeros((len(keep), len(todo)), bool)
        for j, (asset, words) in enumerate(todo):
            w, h = words.shape[1], words.shape[0]
            k = shapes.index((w, h))
            size = region_size((0, 0, w, h))
            g = area(premultiply(words_rgba(words)), size[0], size[1])
            for i, (n, per) in enumerate(keep):
                for x, stretched in per[k]:
                    e = view_error(x, g)
                    if e < err[i, j]:
                        err[i, j] = e
                        stretch[i, j] = stretched
                    ncc[i, j] = max(ncc[i, j], structure(x, g))
        best_pack = err.argmin(0)        # per game texture
        # Per pack picture, among the textures that share some structure with
        # it: a small, mostly empty texture (the reader's 32x32 ARROR) is
        # close to the black corner of any page and would win every one.
        best_game = np.where(ncc >= DATA_MIN_STRUCTURE, err, 1e9).argmin(1)
        for j, (asset, _) in enumerate(todo):
            i = int(best_pack[j])
            e = float(err[i, j])
            second = float(np.partition(err[:, j], 1)[1]) if len(keep) > 1 else 1e9
            c = float(ncc[i, j])
            c2 = float(np.partition(-ncc[:, j], 1)[1] * -1) if len(keep) > 1 else 0.0
            mutual = int(best_game[i]) == j
            # Close enough; or recoloured but the same picture: by far the
            # best correlated of every candidate.
            # Every picture match also has to share some structure: a mostly
            # dark or empty texture (a ladder strip, an esp page with a few
            # sparks) is close in colour to any dark picture.
            ok = mutual and ((keep[i][0], asset) in VERIFIED or c >= DATA_MIN_STRUCTURE and (
                e <= DATA_MAX_ERR or
                (e <= DATA_LOOSE_ERR and c >= DATA_MIN_NCC and
                 c == float(ncc[:, j].max()) and c - c2 >= DATA_NCC_MARGIN)))
            if ok:
                rows.append((keep[i][0], asset, e, bool(stretch[i, j])))
            if report:
                report.write('data,%s,%s,%.2f,%s (next %.2f; ncc %.3f, next %.3f)\n' % (
                    keep[i][0], asset, e,
                    'kept' if ok else ('not mutual' if not mutual else 'too different'),
                    second, c, c2))
    # The pairs identified in game, in the layout the pack picture shows them.
    by_asset = dict((a.lower(), w) for a, w in textures)
    have = set(r[1].lower() for r in rows)
    for name, asset in USER_FILE_PAIRS:
        words = by_asset.get(asset.lower())
        if words is None or asset.lower() in have or name not in files:
            continue
        pic = pack_page(files[name])
        if pic is None:
            continue
        w, h = words.shape[1], words.shape[0]
        size = region_size((0, 0, w, h))
        g = area(premultiply(words_rgba(words)), size[0], size[1])
        views = page_views(pic[0], pic[1], w, h)
        if not views:
            continue
        view = min(views, key=lambda v: view_error(v[0], g))
        rows.append((name, asset, view_error(view[0], g), view[1]))
        have.add(asset.lower())
        if report:
            report.write('data,%s,%s,%.2f,identified in game\n' % (name, asset, rows[-1][2]))
    if report:
        for name, asset, e, stretched in rows[:exact]:
            report.write('data,%s,%s,0.00,name%s\n' % (name, asset,
                                                     ' (whole page)' if stretched else ''))
    print('files: %d textures, %d by name, %d by picture' % (len(textures), exact, len(rows) - exact))
    return rows


# ---------------------------------------------------------------------------
# Model skins
#
# An .EMD ends in one TIM, normally 256x256 8 bpp with two CLUT rows. The game
# Classic REbirth runs on uploads it once per row - the whole TIM in that
# palette - and names each upload with its CRC (classic_rebirth_name), so a
# pack carries one picture per palette row. This port draws a model from ONE
# picture instead: the left half in row 0, the right half in row 1 (docs/
# HD_TEXTURES.md, "Model skins by file"), named after the .EMD. So each pack
# picture is paired with an (.EMD, palette row) and the importer puts the two
# halves together.
#
# Pairing: Classic REbirth's own name first - exact, and it holds for most
# models - then by picture, mutual closest under MODEL_MAX_ERR, for the rest.
# Models whose textures are identical (EM1010 / EM1110, CHAR10 / EM1020) are
# one picture here, or they would tie each other out of the mutual match.
# ---------------------------------------------------------------------------
MODEL_KINDS = ('skin', 'skin2')
MODEL_MAX_ERR = 6.0
MODEL_LOOSE_ERR = 15.0
WHOLE_MIN_SHAPE = 0.85
WHOLE_MARGIN = 0.1
WHOLE_MIN_OUTLINE = 0.7
WHOLE_OUTLINE_MARGIN = 0.3
WHOLE_MIN_INSIDE = 0.5


def unit(a):
    """A flattened array with zero mean and unit length."""
    v = np.asarray(a, np.float64).ravel()
    v = v - v.mean()
    n = np.linalg.norm(v)
    return v / n if n > 0 else v


def inside_shape(a, b):
    """Grey-level correlation of two premultiplied 64x64 pictures over only
    the texels both have opaque."""
    both = (a[..., 3] > 127) & (b[..., 3] > 127)
    if both.sum() < 50:
        return 0.0
    ga = (a[..., :3] @ np.array([0.299, 0.587, 0.114]))[both]
    gb = (b[..., :3] @ np.array([0.299, 0.587, 0.114]))[both]
    return float(unit(ga) @ unit(gb))


def shape_vector(img, square):
    """Grey levels of a premultiplied RGBA picture stretched to `square` x
    `square`, zero mean and unit length: the dot product of two is their
    correlation, whatever their colours."""
    img = thumb(np.ascontiguousarray(img, np.float32), square, square)
    g = img[..., 0] * 0.299 + img[..., 1] * 0.587 + img[..., 2] * 0.114
    v = g.ravel().astype(np.float64)
    v -= v.mean()
    n = np.linalg.norm(v)
    return v / n if n > 0 else v


def emd_tim(data):
    end = len(data) & ~3
    tim = struct.unpack_from('<5I', data, end - 0x14)[4] & ~3
    return data[tim:]


def tim_index_and_cluts(tim):
    """-> (index image, [CLUT row arrays]) for a 4/8 bpp TIM, or None."""
    if len(tim) < 32 or struct.unpack_from('<I', tim, 0)[0] != 0x10:
        return None
    flags = struct.unpack_from('<I', tim, 4)[0]
    if not flags & 8 or (flags & 7) > 1:
        return None
    clen, _, _, cw, ch = struct.unpack_from('<IHHHH', tim, 8)
    clut = np.frombuffer(tim, '<u2', cw * ch, 20).reshape(ch, cw).copy()
    off = 8 + clen
    _, _, _, iw, ih = struct.unpack_from('<IHHHH', tim, off)
    raw = np.frombuffer(tim, np.uint8, iw * 2 * ih, off + 12).reshape(ih, iw * 2)
    if flags & 7:
        idx = raw.astype(np.int32)
    else:
        idx = np.stack([raw & 15, raw >> 4], -1).reshape(ih, iw * 4).astype(np.int32)
    # FixClutVertexData (src/game/TmdAnimation.cpp): the game turns 0x8000 into
    # 0x8001 in the first row before the upload, so that is what was hashed.
    clut[0][clut[0] == 0x8000] = 0x8001
    return idx, [clut[r] for r in range(ch)]


def classic_rebirth_name(words):
    """CRC32 over BGRA, channels << 3, alpha 0 where the colour is black."""
    r = ((words & 31) << 3).astype(np.uint8)
    g = (((words >> 5) & 31) << 3).astype(np.uint8)
    b = (((words >> 10) & 31) << 3).astype(np.uint8)
    a = np.where((r | g | b) != 0, 255, 0).astype(np.uint8)
    return '%08X' % (zlib.crc32(np.stack([b, g, r, a], -1).tobytes()) & 0xFFFFFFFF)


def words_rgba(words):
    r = (words & 31) * 8.0
    g = ((words >> 5) & 31) * 8.0
    b = ((words >> 10) & 31) * 8.0
    a = np.where((words & 0x7FFF) != 0, 255.0, 0.0)
    return np.stack([r, g, b, a], -1).astype(np.float32)


def game_models(root):
    """-> {texture key: {'assets': [...], 'w', 'h', 'renders': [words per row]}}"""
    folder = next((d for d in os.listdir(root) if d.lower() == 'enemy'), None)
    models = {}
    if folder is None:
        return models
    for f in sorted(os.listdir(os.path.join(root, folder))):
        if not f.lower().endswith('.emd'):
            continue
        data = open(os.path.join(root, folder, f), 'rb').read()
        parsed = tim_index_and_cluts(emd_tim(data))
        if parsed is None:
            continue
        idx, cluts = parsed
        renders = [c[idx] for c in cluts[:2]]
        key = b''.join(r.tobytes() for r in renders)
        m = models.setdefault(key, {'assets': [], 'w': idx.shape[1], 'h': idx.shape[0],
                                    'renders': renders})
        m['assets'].append('%s/%s' % (folder, os.path.splitext(f)[0]))
    return models


def pack_model_pictures(packs):
    """-> {pack name: path}, first pack wins (same name = same original)."""
    files = {}
    for pack in packs:
        for kind in MODEL_KINDS:
            folder = os.path.join(pack, kind)
            if not os.path.isdir(folder):
                continue
            for f in sorted(os.listdir(folder)):
                stem, ext = os.path.splitext(f)
                if ext.lower() == '.webp':
                    files.setdefault(stem.upper(), os.path.join(folder, f))
    return files


def model_region(m, row):
    """The texels of the 256x256 page a palette row is drawn from:
    (x0, y0, x1, y1). Row 0 owns the left half of a two-row TIM, row 1 the
    right; a one-row TIM is used whole. Only that part is compared - the pack
    artists often repainted (or blacked out) the half a palette never shows."""
    if len(m['renders']) >= 2:
        half = m['w'] // 2
        return (row * half, 0, (row + 1) * half, m['h'])
    return (0, 0, m['w'], m['h'])


def region_size(region):
    return (max(16, (region[2] - region[0]) // 4), max(16, (region[3] - region[1]) // 4))


def picture(path, region):
    """The part of a pack picture that stands for `region` of the 256x256
    page, scaled to region_size(region)."""
    x0, y0, x1, y1 = region
    size = region_size(region)
    crop = 'crop=iw*%g:ih*%g:iw*%g:ih*%g,' % ((x1 - x0) / 256.0, (y1 - y0) / 256.0,
                                              x0 / 256.0, y0 / 256.0)
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-i', path, '-vf',
                          crop + 'scale=%d:%d:flags=area' % size, '-f', 'rawvideo',
                          '-pix_fmt', 'rgba', '-'], capture_output=True).stdout
    if len(raw) != size[0] * size[1] * 4:
        return None
    return premultiply(np.frombuffer(raw, np.uint8).reshape(size[1], size[0], 4).astype(np.float32))


def match_models(game_root, packs, report):
    models = game_models(game_root)
    pack = pack_model_pictures(packs)
    # (texture key, row) -> pack name
    found = {}
    for key, m in models.items():
        for row, words in enumerate(m['renders']):
            name = classic_rebirth_name(words)
            if name in pack:
                found[(key, row)] = (name, 0.0, 'name')
    exact = len(found)

    # The rest by picture, over the part of the page the row is drawn from (a
    # texture narrower than 256 is the top-left part of Classic REbirth's
    # 256x256 page).
    used = set(n for n, _, _ in found.values())
    # One entry per distinct picture of a region: two models whose right
    # halves are the same (EM1025 / EM1029) want the same pack picture and
    # would otherwise tie each other out of the mutual match.
    units = {}
    for key, m in models.items():
        for row in range(len(m['renders'])):
            if (key, row) in found:
                continue
            x0, y0, x1, y1 = region = model_region(m, row)
            pixels = m['renders'][row][y0:y1, x0:x1].tobytes()
            units.setdefault((region, pixels), []).append((key, row))
    todo = list(units.items())
    regions = sorted(set(region for (region, _), _ in todo))
    names = [n for n in sorted(pack) if n not in used]
    feats = {r: [] for r in regions}
    keep = []
    for n in names:
        pics = [picture(pack[n], r) for r in regions]
        if any(x is None for x in pics):
            continue
        keep.append(n)
        for r, x in zip(regions, pics):
            feats[r].append(x)
    names = keep
    if todo and names:
        feats = {r: np.stack(v) for r, v in feats.items()}
        err = np.empty((len(todo), len(names)), np.float32)
        for i, ((region, _), members) in enumerate(todo):
            key, row = members[0]
            x0, y0, x1, y1 = region
            rgba = words_rgba(models[key]['renders'][row])[y0:y1, x0:x1]
            size = region_size(region)
            g = area(premultiply(rgba), size[0], size[1])
            err[i] = np.abs(feats[region] - g[None]).mean((1, 2, 3))
        best_pack = err.argmin(1)
        best_game = err.argmin(0)
        for i, (_, members) in enumerate(todo):
            j = int(best_pack[i])
            e = float(err[i, j])
            second = float(np.partition(err[i], 1)[1]) if len(names) > 1 else 1e9
            # Close enough, or - for a face the artist redrew - still the
            # clear winner: under the loose limit and at most half the error
            # of the next candidate.
            ok = best_game[j] == i and (e <= MODEL_MAX_ERR or
                                        (e <= MODEL_LOOSE_ERR and e * 2 <= second))
            for t in members:
                if ok:
                    found[t] = (names[j], e, 'picture' if e <= MODEL_MAX_ERR
                                else 'picture, clear winner (next %.2f)' % second)
                elif report:
                    report.write('model,%s,%s,%.2f,%s (next %.2f)\n' % (
                        names[j], '+'.join(models[t[0]]['assets']) + ':%d' % t[1], e,
                        'not mutual' if best_game[j] != i else 'too different', second))

    # One-row skins narrower than the page that nothing above found: the pack
    # may have the texture stretched over its whole square page (the crow
    # EM1005, the plant's vines EM1113), as it does the doors. Repainted too,
    # so compared by SHAPE: the correlation of grey levels of the texture
    # stretched to a square against each whole pack picture, kept when at
    # least WHOLE_MIN_SHAPE, WHOLE_MARGIN ahead of the next picture, and the
    # pack picture's best texture by shape as well. A texture that is nearly
    # all black (the crow) has no grey levels to speak of, and a block of
    # texels the artist cleared sinks the grey score (0.32 for the crow's
    # E6D7E5CD): those are kept by their OUTLINE instead - the alpha
    # channels correlating at least WHOLE_MIN_OUTLINE, WHOLE_OUTLINE_MARGIN
    # ahead of the next picture, mutual, and the grey levels agreeing where
    # both are opaque (WHOLE_MIN_INSIDE).
    whole = [(key, m) for key, m in models.items()
             if len(m['renders']) == 1 and m['w'] < 256 and (key, 0) not in found]
    if whole:
        used = set(n for n, _, _ in found.values())
        names = [n for n in sorted(pack) if n not in used]
        pics, outl, imgs, keep = [], [], [], []
        for n in names:
            x = picture(pack[n], (0, 0, 256, 256))
            if x is not None:
                pics.append(shape_vector(x, 64))
                outl.append(unit(x[..., 3]))
                imgs.append(x)
                keep.append(n)
        if pics:
            P = np.stack(pics)
            PA = np.stack(outl)
            texs = [thumb(np.ascontiguousarray(premultiply(words_rgba(m['renders'][0])), np.float32),
                          64, 64) for _, m in whole]
            T = np.stack([shape_vector(t, 64) for t in texs])
            TA = np.stack([unit(t[..., 3]) for t in texs])
            score = T @ P.T                       # [texture, picture]
            ascore = TA @ PA.T
            best_pic = score.argmax(1)
            best_tex = score.argmax(0)
            abest_pic = ascore.argmax(1)
            abest_tex = ascore.argmax(0)
            for i, (key, m) in enumerate(whole):
                j = int(best_pic[i])
                s0 = float(score[i, j])
                s1 = float(np.partition(score[i], -2)[-2]) if len(keep) > 1 else -1.0
                ok = s0 >= WHOLE_MIN_SHAPE and s0 - s1 >= WHOLE_MARGIN and best_tex[j] == i
                if not ok:
                    ja = int(abest_pic[i])
                    a0 = float(ascore[i, ja])
                    a1 = float(np.partition(ascore[i], -2)[-2]) if len(keep) > 1 else -1.0
                    inside = inside_shape(texs[i], imgs[ja])
                    if (a0 >= WHOLE_MIN_OUTLINE and a0 - a1 >= WHOLE_OUTLINE_MARGIN and
                            abest_tex[ja] == i and inside >= WHOLE_MIN_INSIDE):
                        found[(key, 'whole')] = (keep[ja], 1.0 - a0,
                                                 'stretched page, outline %.2f (next %.2f), '
                                                 'inside %.2f' % (a0, a1, inside))
                        continue
                if ok:
                    found[(key, 'whole')] = (keep[j], 1.0 - s0,
                                             'stretched page, shape %.2f (next %.2f)' % (s0, s1))
                elif report:
                    report.write('model,%s,%s:whole,%.2f,shape %.2f next %.2f%s\n' % (
                        keep[j], '+'.join(m['assets']), 1.0 - s0, s0, s1,
                        '' if best_tex[j] == i else ' not mutual'))

    rows = []
    for (key, row), (name, e, how) in sorted(found.items(), key=lambda kv: models[kv[0][0]]['assets'][0]):
        for asset in models[key]['assets']:
            rows.append((name, row, asset))
            if report:
                report.write('model,%s,%s:%s,%.2f,%s\n' % (name, asset, row, e, how))
    textures = sum(len(m['renders']) for m in models.values())
    print('models: %d EMD textures (%d palette rows), %d by name, %d by picture, %d rows written'
          % (len(models), textures, exact, len(found) - exact, len(rows)))
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('game')
    ap.add_argument('packs', nargs='+')
    ap.add_argument('--out', default=os.path.join(
        os.path.dirname(os.path.abspath(__file__)), 'asset_migrator', 'src', 'core', 'HdPackTable.inc'))
    ap.add_argument('--report')
    ap.add_argument('--dc', help='the DC overlay tree (the asset migrator\'s DC folder), '
                                 'for the packs\' bio1dc set')
    ap.add_argument('--jpn', help='the Japanese tree, for the file textures only it has '
                                  '(Item_m2/FILEI*, TEXTM*)')
    args = ap.parse_args()

    report = open(args.report, 'w') if args.report else None
    bgd = match('bgd', args.game, args.packs, report)
    mask = match('mask', args.game, args.packs, report)
    models = match_models(args.game, args.packs, report)
    data = match_data(args.game, args.packs, report, args.jpn)
    dc_bgd, dc_mask = match_dc(args.dc, args.packs, report) if args.dc else ([], [])
    # The pairs identified in game (USER_*_PAIRS), unless the matcher already
    # has a picture for that asset.
    named_in_packs = set()
    for pack in args.packs:
        for kind in DATA_KINDS:
            folder = os.path.join(pack, kind)
            if os.path.isdir(folder):
                named_in_packs.update(os.path.splitext(f)[0].upper() for f in os.listdir(folder))
    have_bg = set(a.lower() for _, a, _ in bgd)
    for name, asset in USER_BG_PAIRS:
        if name in named_in_packs and asset.lower() not in have_bg:
            bgd.append((name, asset, -1.0))
    have_model = set((a.lower(), r) for _, r, a in models)
    for name, row, asset in USER_MODEL_PAIRS:
        if name in named_in_packs and (asset.lower(), row) not in have_model:
            models.append((name, row, asset))
    named = [(n, a) for n, a in USER_NAMED_PAIRS if n in named_in_packs]
    backdrops = backdrop_rows(args.game, args.jpn, data)
    with open(args.out, 'w', newline='\n') as out:
        out.write('// Generated by tools/gen_hd_pack_table.py - do not edit by hand.\n')
        out.write('// { pack file name (no extension), kind, game asset (no extension) }\n')
        out.write('// %d backgrounds, %d masks, %d model palette rows, %d Data/ textures, '
                  '%d named, %d file pages from a backdrop; DC: %d backgrounds, %d masks.\n'
                  % (len(bgd), len(mask), len(models), len(data), len(named), len(backdrops),
                     len(dc_bgd), len(dc_mask)))
        for name, asset in named:
            out.write('{"%s", HdPackKind::Named, "%s"},\n' % (name, asset))
        for name, asset in backdrops:
            out.write('{"%s", HdPackKind::Backdrop, "%s"},\n' % (name, asset))
        for name, asset, _, stretched in data:
            kind = 'FileWhole' if stretched else 'File'
            if asset.count('/') == 2:
                # A cell a loader cuts out of the file (the slides): the
                # importer takes the page's corner.
                if stretched:
                    continue
                kind = 'Cell'
            out.write('{"%s", HdPackKind::%s, "%s"},\n' % (name, kind, asset))
        for name, asset, _ in dc_bgd:
            out.write('{"%s", HdPackKind::DcBackground, "%s"},\n' % (name, asset))
        for name, asset, _ in dc_mask:
            out.write('{"%s", HdPackKind::DcMask, "%s"},\n' % (name, asset))
        for name, asset, _ in bgd:
            out.write('{"%s", HdPackKind::Background, "%s"},\n' % (name, asset))
        for name, asset, _ in mask:
            out.write('{"%s", HdPackKind::Mask, "%s"},\n' % (name, asset))
        for name, row, asset in models:
            if row == 'whole':
                out.write('{"%s", HdPackKind::ModelWhole, "%s"},\n' % (name, asset))
            else:
                out.write('{"%s", HdPackKind::ModelPalette%d, "%s"},\n' % (name, row, asset))
    print('wrote', args.out)


if __name__ == '__main__':
    main()
