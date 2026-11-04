# WebP backgrounds and high-resolution textures

Port-only. The original reads its room backgrounds from `.pak` files and builds
every other texture from PlayStation TIM data; it has no notion of a replacement
image. The port adds two ways to supply one, both as **WebP** (lossy or
lossless, with or without alpha; animated files are not supported):

| | Named background | HD texture |
|---|---|---|
| What it replaces | a room camera's background | any texture the game uploads |
| File | `<tree>/Stage<n>/RC<stage><room><cam>.webp` | `<assets>/HD/[<folder>/]<hash>.webp` |
| Found by | name, beside (or instead of) the `.pak` | a hash of the original's pixels |
| Resolution | any | any, same aspect ratio as the original |
| Switch | none - the file is the switch | `[Textures] HdTextures` (default 1) |

A third route needs no hash: a texture that comes from one known file can be
replaced by that file's own path under `HD/<TREE>/`, the way the mode overlays
name their files. It covers room backgrounds (`HD/USA/Stage1/RC1060.webp`),
room masks (`HD/USA/Objspr/OSP01060.webp`) and character / enemy skins
(`HD/USA/Enemy/EM1010.webp`); see [Replacement by file
path](#replacement-by-file-path) and [Model skins by file](#model-skins-by-file).
This is what the HD pack importer writes for everything its table covers.

The decoder is the vendored `src/third_party/simplewebp.h` (BSD-3-Clause),
wrapped by `src/system/WebpImage.{h,cpp}`. Images are limited to 8192 pixels a
side and 4096 x 4096 in total: the process is 32-bit.

## Named backgrounds

The game asks for `stage1\rc1060.pak` - an LZW stream holding a 320x240 16-bit
TIM. If `rc1060.webp` exists it is used instead
(`src/game/RoomBackgroundWebp.cpp`, hooked into the three reads in
`load_room_bg` / `load_room_bg_image`). The search order across the trees is:

1. the mode overlay's `.webp` (`DC/`, `SATURN/`)
2. the mode overlay's `.pak`
3. the base tree's `.webp` (`USA/`, `JPN/`)
4. the base tree's `.pak`

so a mode's own background always outranks the base tree's, whatever format
either is in. Within one folder the `.webp` wins; keep only one of the two.

The picture can be any size - the background is drawn as one full-screen quad -
but should be 4:3. Alpha is ignored: a background has nothing behind it.

The **asset migrator** writes these: its *Room backgrounds* option converts
every `Stage*/RC*.pak` of the tree it migrates to WebP, lossless (the same
pixels, about 20 % smaller) or lossy. See `tools/asset_migrator/README.md`.
A lossless file made from a `.pak` decodes to exactly the pixels the game builds
from that `.pak`, so it also hashes the same (below).

Room **masks** (`Objspr/osp*.pak`) are not named files in the tree; replace them
by file path under `HD/` (below) or by hash. The mask TIMs inside the arrange
and Saturn rooms have no file of their own: hash only.

## Replacement by file path

`HD/<TREE>/<path of the file without its extension>.webp`, where `<TREE>` is
the folder the game actually reads the file from: the mode overlay (`DC`,
`SATURN`) when it carries it (for a background, as `.pak` or
`.webp`), otherwise `USA` or `JPN`. Case does not matter.

```
HD/USA/Stage1/RC1065.webp        background, stage 1 room 06 camera 5
HD/USA/Objspr/OSP00065.webp      that camera's mask page
HD/DC/Stage8/RC8000.webp         a background the DC overlay supplies
HD/DC/Stage8/ROOM8000_MASK3.webp a mask kept inside a room file (camera 3)
HD/USA/Data/STATUS.webp          a texture built straight from a Data/ file
HD/USA/Item_m2/MAP01.webp        a map page (Item_m2 TIMs go through the same loaders)
HD/USA/Item_m2/I00V.webp         an item view's texture (the TIM inside the .ivm)
HD/USA/Item_m1/DOOR00.webp       a door's texture (the TIM inside the .dor)
HD/USA/Effspr/ESP001.webp        a whole effect page; the game cuts its bands out
HD/USA/Data/ITEM_ALL/05.webp     an inventory icon (item 5 of item_all.pix)
```

**Doors and item views.** A `.dor` / `.ivm` holds one TIM, stored into a
texture page the way a model's TIM is: `DoorCreateTexturePage` and the item
viewer (`FUN_004841f0`) register the page with the file's key
(`HdTextures_SetOwnerKey`), and `VTable_CreateTextureHandle` finds it as it
does a model skin. The item key is taken from the buffer in `FUN_00484420`, so
the ending's `i73v.ivm` is named as well. The item viewer's glass copy (its
translucent texels knocked out) is not named and keeps its own picture.

**Effspr pages.** `BakeWeaponSheetFromPage` cuts the weapon-effect sheets as
horizontal bands out of a 256x256 `esp*.tim` page (esp000 for every room, the
room's esp001/esp2xx). Each band upload is an `HdRegionScope`: the HD file is
the whole page, and `CreateTextureHd` uploads the matching rows of it. The
room's own effect sprites (the glass bursts, the Tyrant tube's cracks) are TIMs
inside the room file, which `load_effect_sprites` composites into up to four
pages; page `p` is the room's Effspr file `room_effect_page_entry()[p]`, and
its sprites are that file's art at the same place (ESP204/208/213/214/220/227/
228 compared texel for texel against a dump). So each page upload (and each
palette-variant row) is an `HdPageScope` on that file: the HD page is used,
except where the room draws a sprite in another palette row than the file
(ESP203, ESP229, the greenhouse fountain's water in its tint row): there the HD
picture is recoloured, each channel scaled by the ratio of the room's colour
to the file's for the texel under it, so it stays HD in the room's colours. A
page with a texel the file has transparent is not named and stays on the hash
route.

**Inventory icons.** `LoadItemImage` copies one item's 40x30 icon out of
`item_all.pix` into the item page at (88, slot x 32), and `LoadImage` uploads
the whole 256x256 page again - its contents are whatever the player carries,
so its hash never names the same page twice. Each icon is named instead, after
the file in the buffer and the item: `HD/<TREE>/Data/ITEM_ALL/<NN>.webp`
(`LoadImage_SetHdIconKey`, `src/game/TextureLoader.cpp`). The page remembers
which named icon is where; when one has an HD picture the page is uploaded at
HD size - the page scaled up, each icon's picture over its slot - and the
callers keep the logical 256x256. The original game uploaded each icon on its
own, as a 64x64 texture with the icon in the top-left corner in STATUS.TIM's
third palette row: the importer computes that texture's Classic REbirth name
for every item (71 of the 72 USA icons are in the Seamless HD Project; not the
oil can, 46) and cuts the icon's corner (160x120 of the 4x picture) out.
A combine reloads the new item's icon from `item_mix.pix` instead
(`menu_item_combine_refresh`), named `HD/<TREE>/Data/ITEM_MIX/<NN>.webp`; each
of its rows is a byte-for-byte copy of an `item_all.pix` row, so the importer
finds it in the pack the same way.
A mode's sheet (`DC/data/item_all`) never borrows the base icon of the same
**number**: the DC's rows 45/46 are the moon crest halves where the PC has the
lock pick and the oil can. `HdItemIconKey` (`src/game/TextureLoader.cpp`)
names a mode row after the base row that shows the same picture (same row
first, then any row of either base sheet; colours through STATUS.TIM row 2
within 2 steps a channel), and leaves it on its own name with the fallback
refused when there is none. Every named icon is also dumped on its own with
`DumpTextures`, e.g. `HD/dump/DC/data/item_all/45.png`; its HD picture goes
in `HD/DC/data/item_all/45.webp`.

**Projector slides.** `load_slides_images` loads `Data/SLIDE.TIM` (640x256,
8 bpp, six palette rows) and the port uploads it as six 192x128 cells, cell
`n` at `((n >> 1) * 192, (n & 1) * 128)` in palette row `n`
(`TexturePage_LoadImage`, `src/game/SpriteRenderer.cpp`). Each is named after
the file and the slide, `HD/<TREE>/Data/SLIDE/<NN>.webp` (`00` the Umbrella
title .. `05` the staff photo). The packs hand each slide over as the top-left
192x128 of a 256x256 page; the importer's `Cell` rows cut that corner out.

**File reader pages.** The USA file reader reads each document from an
`Item_m2/FILEM_*.PIX` container: sixteen dword offsets, then one LZW-packed
256x256 4 bpp TIM per page (the last offset is the file's end).
`pickup_unpack_list` (`src/game/MainMenu.cpp`) unpacks one page into the TIM
buffer and uploads it, so the upload is not the file `LoadFile` read; it is
named after the container and the page, `HD/<TREE>/Item_m2/FILEM_N0/<NN>.webp`
(`04` the fifth page). Classic REbirth's English pages are these pages pixel
for pixel: 78 of the 84 have a file under Classic REbirth's own name in the
MulderLoad pack (the RE-ENHANCE text pages), which the importer installs as
`Cell` rows. A 79th, `FILEM_W0/03` (Barry's letter to his daughters), pairs
by picture (2.15, correlation 0.94): the pack's page corrects the original's
typos ("Poly", "will watching"), so its name differs. No pack has the five
pages with a picture (`FILEM_P0/01` the pass number, `W0/01` Barry's photo,
`X0/01`, `Y0/01`, `Z0/01` the pass codes): Classic REbirth runs the Japanese
game, which draws the picture as a separate 256x120 backdrop (`FILEI12`, `15`,
`10`, `13`, `14`). The USA page holds that backdrop's pixels at (24, 24),
scale 1 (2-9 levels a channel apart), so the importer builds the HD page
(row kind `Backdrop`, `composeBackdropPage`): the page scaled up to the
pack's scale, with the pack's backdrop over the texels that show the picture
(not over the rest: the backdrop's frame scraps are not on the USA page).
The transparency is the page's own, scaled up, and colour under it is black:
the pack keeps stray key colours (magenta, blue) in the pass codes' punched
holes, which the page has transparent, and copying the pack's pixels there
drew them.
Barry's photo goes on 32 texels below the backdrop; that part stays the
scaled-up page, brought to the pack's colours (a gain and offset per channel
fitted where both show the photo) with the backdrop fading out over its last
six rows.

**Players/.** The `.EMW` weapons and the cutscene `WS*.TMD` carry no texture:
they are drawn from the character's texture page (bank 0x16), so the
character's named skin covers them.

**Masks inside a room file.** The Director's Cut arrange rooms (and Saturn
mode) read a camera's mask from the `.RDT` itself, so it is named after the
room file the game loaded and the camera, in hex: `ROOM8000_MASK3`
(`room_hd_note_room` in `RoomInit.cpp`, used by `load_room_masks`).

In Saturn mode, a mask with no picture under that name takes the PC page's.
The Saturn room file is the PS1 one, and its mask TIM is the PC `Objspr` page
of the same room and camera cut down to the rows and columns it uses: all
1,274 Saturn masks (every `ROOM*.RDT`, the Battle Game arenas against their
campaign room, the Laboratory Tyrant room included) are the PC page's top-left
part within 2 steps a channel - the palettes are ordered differently, the
colours are the same. `room_hd_saturn_mask_page` (`RoomBackgroundWebp.cpp`)
checks that per camera against the page itself, and the upload is then named
as that part of the page (`HdRegionScope` on `HD/USA/Objspr/OSP01065.webp`),
the way the weapon-effect bands are cut out of an Effspr page. A mask that is
not the page (another disc) keeps its own name.

One room is left out, backgrounds and masks alike: the Laboratory Tyrant room
(`Stage5/RC5130`-`RC5137`, also Battle Game arena 15), which the Saturn re-drew
(the vats emptied, the second Tyrant's capsule added; the asset migrator's
`kRoomBgs`). Its masks match the PC pages, but the PC's HD art shows the PC's
room, so its background keys refuse the base fallback
(`HdTextures_SetBaseFallback`, `room_hd_note_bg`) and its masks take no PC
page: with no picture under `HD/SATURN/` the room keeps the Saturn's own art.
Every other Saturn background (with "all rooms" migrated, a palette-shifted
copy of the PC picture) borrows the USA picture of the same name.

**Models inside a room file.** A room object (the clocks, statues and crests
`omodel_set` places) and an item model (the keys, jewels and ink ribbons
lying in the room, `item_set`) carry their TIM inside the `.RDT`, and the same
TIM is copied into many rooms (an ink ribbon's into over a hundred item
slots). So they share one folder, the way the room effect pages share the
`Effspr` files, and are named by the TIM: `HD/USA/RoomModels/<crc>.webp`,
a CRC-32 of the TIM's palette and pixels (`hdtex::RoomTimName` in
`HdTextureHash.h`, shared with the importer; `room_hd_model_key`, set around
`ProcessTmdAsync` in `CmdFunctions.cpp`). The tree is the one the room file
was read from. Their palette rows are not the EMD layout - a room TIM's rows
are colour variants or belong to different objects - so each row is its own
picture, exactly as uploaded: `_P<row>` is added when the TIM has more than
one (`HdTextures_SetOwnerKeyPerPalette`). A palette the room code edits before the upload
(the greenhouse objects, the 2F lesson room, the two map items) leaves the
upload unnamed: a picture of the file's own colours would undo the edit.

**Data/ files.** `LoadFile` records which asset it read into which buffer,
with a checksum of the bytes (`HdTextures_NoteLoad`). The loaders that build a
texture straight from a buffer - `LoadTexturePage`, `display_image`,
`ProcessTextureImage` (the font), `LoadEffectTextureSheet`,
`LoadShadowMaskTexture` - name the upload after that file (`HdBufferScope`)
when the buffer still holds it; a pointer into the middle of a file adds
`@<hex offset>`. A buffer the game has since unpacked into or assembled a page
in fails the checksum and names nothing, so it can never pick up a wrong
picture. The item sheets (`ITEM_ALL`, `ITEM_MIX`, `MEDAL`) and the DC
title's `BT367OAB` pages are cut up or assembled by their loaders and stay on
the hash route, except for the parts named above (the inventory icons, the
slides). Named uploads seen in a short run:
`fontus`, `Font03t`, `Optkey03`, `status`, `statface`, `blue`, `staitem`,
`kage`, `title`, `t_press`, `type00`.

How it works (`src/game/Room.cpp`, `src/game/RoomBackgroundWebp.cpp`):

- `load_room_bg` and `load_room_bg_masks` note each camera's file key
  (`room_hd_note_bg` / `room_hd_note_mask`) while the room loads;
- when a camera is shown, the background upload (320x240 only) and the mask
  page upload run inside an `HdSourceScope` naming that key, and
  `CreateTextureHd` looks for `HD/<key>.webp` before it hashes anything;
- `room_hd_prefetch` hands the named files to the decode worker
  (`HdTextures_PrefetchNamed`), so camera cuts stay as fast as with the hash
  route.

A named replacement outranks a hash-named one. A large `.webp` placed in the
tree itself (Named backgrounds, above) is already the picture and is not
looked up in `HD/`. With `DumpTextures=1` each of these is also dumped under
its key, `HD/dump/USA/stage1/rc1065.png`.

## HD textures

Every texture - model skins, item and menu art, fonts, effect sheets, room
masks, the backgrounds themselves - ends up as an RGBA8 image handed to the
renderer in one place (`CreateTextureHd` in `src/marni/MarniSystem.cpp`). There
the image is hashed, and if `<assets>/HD/<hash>.webp` exists that file is
uploaded instead (`src/system/HdTextures.{h,cpp}`).

`<assets>` is the folder holding `USA/` (config.ini `[Assets] Path`; the
executable's folder by default), so `HD/` sits beside `USA/`, `DC/` and
`SATURN/`. One folder serves every mode and region: the name depends only on
the pixels.

A replacement may sit in `HD/` itself or one folder down, so a pack can group
its files by type (`HD/bgd/`, `HD/mask/`, `HD/skin/` ...) or however it likes;
only the file name matters. The folder is indexed once, at the first texture
upload. `HD/dump/` is the game's own output and is never read as a source.

### Making a pack

1. Set `[Textures] DumpTextures=1` and play through what you want to replace.
   Each image is written once to `HD/dump/<hash>.png`, exactly as the game
   uploads it.
2. Upscale or repaint the PNG. Keep the layout and the aspect ratio: the game
   still addresses the texture by its original size and normalises its
   coordinates against that, so a 256x256 page can become 1024x1024 but not
   1024x512.
3. Save it as `HD/<hash>.webp` - the same 16 hex digits - or in a folder of
   your choosing below `HD/`.
4. Set `DumpTextures=0` again. The dump is uncompressed and slows room loads.

Replacement is on by default and costs nothing while `HD/` does not exist. The
folder is read once, at the first texture upload, so files added while the
game is running are picked up on the next start.

### Decode cost: the cache and the room prefetch

The game uploads a room's background and its mask page again on every camera
cut, and decoding a 1280x960 background plus a 1024x1024 mask takes far longer
than a cut has. Two things keep the cuts instant:

- a decoded replacement is kept in memory (up to 160 MB, least recently used
  out first), so a camera seen before costs one hash and one upload. Only what
  a prefetch asked for is kept - the backgrounds and masks the cuts upload
  again. A model skin, a room model or an effect page is uploaded once per
  room and dropped after its upload: kept, those (a 4x skin is 8 MB) pushed
  the backgrounds out of the cache, and the next cut decoded them again;
- when a room is loaded, `room_hd_prefetch` (`src/game/RoomBackgroundWebp.cpp`)
  builds the texture each of its cameras will upload and hands it to
  `HdTextures_Prefetch`, and a worker thread decodes the replacements in the
  background. A cut that arrives before the worker got to its image decodes it
  on the spot, as before.

A room load asks for a dozen 4x pictures at once - its model textures, its
Effspr pages, its doors - and the room's init script uploads them one after
the other, so decoding each on the spot stalled the load (and the save-load
message) for a second or two. `room_hd_prefetch_early` queues the room's
model textures (`RoomModels/<crc>`) and Effspr pages as soon as the room file
is in memory, before the init script runs, and `door_set` queues each door's
picture (`Door_HdPrefetch`), so it is ready when the door opens. Three workers
decode in parallel.

That alone still left the load waiting on whatever was not decoded yet. On
the PlayStation the door animation and the load message were the screens the
CD read hid behind, so here they are what the decoding hides behind: while a
room loads (`HdDeferScope` around `room_transition_load`, `room_set`, the
door's file load and the game start's player/effect loads) a lookup does not
wait (`HdTextures_TryNamed` / `TryHash`). A picture still decoding is
uploaded as the original and remembered with what turns it into the final
texture - the whole picture, an Effspr band, a recoloured effect page, and a
model's knock-out twin (`MarniSystem.cpp`, deferred HD pictures). `main_loop`
swaps each in under the same handle once it is decoded
(`MarniHd_PumpDeferred`, `MarniDX::ReplaceTexture`; a slot serial drops one
whose texture was released first). The door cannot be skipped while any are
still on their way, and when its animation ends first it holds on black until
they are in. A model skin's name is only known at its upload, so it is
deferred there like the rest.

The workers only decode; every lookup, upload and log line stays on the game
thread. A `[hd] ... replaces its texture` line in the log is a decode on the
game thread; for a background or mask that means a cut waited on it.

A model's knock-out twin (its blending texels cleared, for the two-pass
draw) is cut from the model's HD picture, named or by hash, and is never
looked up or dumped on its own. A Debug build decodes several times slower than Release.

### What to keep in mind

- **Alpha is the cut-out.** The game marks transparent texels with alpha 0
  (the PlayStation's colour key) and draws everything else opaque. Keep the
  dump's alpha channel when you upscale, or the texture loses its holes. Use
  lossless WebP, or a lossy one with a clean alpha edge, for anything keyed.
  Classic REbirth's pack pictures are opaque (its renderer keys black
  itself), so the importer gives a table row's picture that has no alpha the
  game TIM's own transparency, scaled up (`keyLikeTexture` in
  `core/HdPack.cpp`). Without it the file book page's black margin
  (`Item_m2/FILE000/001`) covered the book-change arrows drawn under it.
- **A page is hashed as uploaded.** Several model textures are assembled into
  one 256x256 page before the upload, so the hash names the finished page, not
  one source file. A room with a different set of enemies builds a different
  page and needs its own replacement. Character and enemy skins avoid this
  through the by-file route below.
- **Palette variants are separate textures.** An effect sheet or a slide drawn
  with four palettes is uploaded four times and dumps four images.
- **Sampling is unchanged.** A replacement is drawn with the filter the
  original was drawn with.
- **Backgrounds.** A background can be replaced either way: by hash (of the
  320x240 image the `.pak`, or a native-size `.webp`, decodes to) or by a named
  `.webp`. A named file that is not 320x240 is used as it is and is neither
  hashed nor dumped.
- FMV frames are not textures in this sense and are never hashed.

### The hash

`hdtex::HashName` (`src/system/HdTextureHash.h`, header-only and shared with
the asset migrator): two MurmurHash3 x86_32 passes, seeds `0x52453148`
("RE1H") and `0x48445458` ("HDTX"). Each pass consumes the width, the height
and then every pixel as a little-endian dword (`R | G<<8 | B<<16 | A<<24`), and
is finalised with the byte length `(width*height + 2) * 4`. The name is the two
results as 8 hex digits each, first pass first.

## Model skins by file

A character or enemy model's texture can be replaced by the name of the `.EMD`
it is loaded from, the way the mode overlays name their files:

```
<assets>/HD/<TREE>/<path of the .EMD without its extension>.webp

HD/USA/ENEMY/EM1010.webp         the USA tree's zombie
HD/DC/ENEMY/EM1032.webp          a model the Director's Cut overlay supplies
HD/SATURN/ENEMY/EM1016.webp      the Saturn's Tick
```

`<TREE>` is the folder the model was **actually read from**: the mode's
overlay (`DC`, `SATURN`, `SATURN_JPN`) when it carries the file, otherwise
`USA` or `JPN`. A DC session that loads a zombie the DC overlay does not
replace reads `USA/ENEMY/...`, so one picture serves every mode that uses the
same file, and a mode's own model can never be drawn with another tree's art.
Folder and file name case does not matter.

A model the overlay does carry, but with no picture in its own HD tree,
borrows the base tree's skin of the same name only when the two TIMs show the
same picture (`EmdTimsLookAlike`, `src/game/EntityModelLoader.cpp`): colours
through each file's palette, at most 10% of texels more than 2 steps apart.
The DC's CHAR10/CHAR11/EM1030/EM1031 are the PC skins with the gaps between
UV islands repainted (about 2% differ) and borrow; its outfits EM1032/EM1033
(57-70%) do not.

With `[Textures] DumpTextures=1` each model is also written once to
`HD/dump/<TREE>/<path>.png`, e.g. `HD/dump/USA/enemy/em1033.png`. That PNG is the
starting point: upscale it, keep its layout, aspect ratio and alpha, save it as
WebP under the name above. A named skin outranks a hash-named replacement of
the same texture; with no named file the hash route works as before.

What the picture is. An `.EMD` ends in one TIM, normally 256x256 at 8 bpp with
two palette rows, and the renderer makes one texture per palette row, each
the whole TIM in that palette. Every shipped model keeps the rows on separate
halves: polygons using row 0 sample only the left 128 texels, polygons using
row 1 only the right 128 (checked on CHAR10, EM1000, EM1020; a single-row TIM
uses its row everywhere). So the dump, and the replacement, is **one** picture:
the left half in palette 0 and the right half in palette 1, which is what the
model actually looks like. It stands in for every palette's texture.

How it works (`src/game/EntityModelLoader.cpp`, `src/game/TmdAnimation.cpp`,
`src/marni/MarniSystem.cpp`):

- `LoadEntityEMD` takes the file's key with `AssetSourceKey` right after the
  load, with the overlay the load used still set (a bonus outfit or the
  borrowed DC outfit is read from a tree of its own). For a cutscene NPC
  wearing a bonus outfit the key is the outfit's file, whose texture it is.
- `TmdProcessingCallback` stores the TIM into a PSXTexture bank and registers
  the bank with that key and its pixel pointer (`HdTextures_SetOwnerKey`);
  `SetupTextureBank`'s copy of a bank inherits it. A bank reused for an
  unnamed TIM forgets the key.
- `VTable_CreateTextureHandle` traces the material it is asked for back to its
  bank and uses `HD/<key>.webp` if there is one. Its STP knock-out twin is then
  the HD picture with each texel cleared where the original texel under it is
  STP, so which texels blend still comes from the TIM.

Not covered: a model drawn gold in the Saturn Battle Game's arena 15 (its
palette is rewritten before the upload, so the named skin is skipped and the
hash route applies); weapon models, which do not load through
`LoadEntityEMD` (room objects and item models are named after the room file,
above). The knock-out twin path has not been seen in
game for a named skin.

## Importing a Classic REbirth HD pack

The Seamless HD Project and RE-ENHANCE packs for the Classic Rebirth PC patch release are
WebP already and are laid out as `hires/{bgd,mask,misc,skin,skin2}/<CRC>.webp`;
the Seamless HD Project adds `bio1dc/hires/{bgd,mask}` for the Director's Cut
camera angles. The asset migrator's **HD Pack** tab (`core/HdPack.cpp`,
`re1am_selftest hdpack <hires folder> <target> [USA|JPN] [--dc] [--ffmpeg <path>]`)
imports one hires folder at a time into `HD/` under this port's names; the
user says which game it is for (*Pack is for*: original game or Director's
Cut, `--dc`).

It does not use the packs' names to find anything. Those are Classic REbirth's
own hash, read out of its `ddraw.dll`:

- CRC32 over the texture as the original game builds it: BGRA bytes, each
  5-bit channel shifted left by 3, alpha 0 where the colour is black;
- a background is hashed from its left 256x240 page only, after
  `display_image` (`0x00470770`) has turned every `0x0000` pixel into `0x0001`,
  with alpha forced to 255;
- a mask page from at most 256x256, with the colour of transparent texels
  zeroed.

That reproduces the names of the menu, effect and some model textures from the
game's files, but not the backgrounds and masks: Classic REbirth first looks
for a replacement low-res picture (`BG\ROOM_<stage><room>_<cam>.png` and
`..._mask.png`, supplied by the TeamX HD mod those packs are built on) and
hashes that instead. Of the 675 stock backgrounds exactly one hashes to a pack
name.

So the importer finds each pack file's texture two other ways. What its table
covers is written under the game file's own path, `HD/<USA|JPN>/...` (see
[Replacement by file path](#replacement-by-file-path)); what is matched against
the dump is written as `HD/<hash>.webp`, the name the game looks it up by. The
pack's folders (`bgd`, `mask`, `misc`, `skin`, `skin2`) are not kept: what an
earlier import left in `HD/<pack folder>/` is moved up into `HD/` before the
import starts (where `HD/` has the name already, its copy wins), so this import
replaces it like any other file, and the emptied folder is removed.

**1. A table, for room backgrounds and masks.** `core/HdPackTable.inc`,
generated by `tools/gen_hd_pack_table.py`: every pack background and mask is
compared, as a picture, with every one of the game's, and a pair is kept when
each is the other's closest match and they differ by less than a threshold.
That gives 605 of 676 backgrounds and 429 of 431 pack masks; a second round
over the masks left unpaired adds 2 more (`D7B7AB5B` is closest to
`OSP04054`, which already took a pack page at 0.48, and next closest to its
own `OSP00094`). Two room files can
show the same picture (the main hall's 2F cameras `Stage2/RC2030-RC2034` are
`Stage1/RC1220-RC1224` again, at 0.5-1.3: compression noise and one small
object), and the pack has it once; a game picture that is the same as a paired
one (under 1.5, at the same size; masks under 0.5) takes that pack picture too
- 17 more backgrounds and 2 masks, 622 and 433 in all. Three things about
the pictures matter:

- A pack mask is always a whole 256x256 page (Classic REbirth hashes the VRAM
  page). 163 of the 436 `Objspr` pages are 128x128, which is the page's
  top-left quarter: the generator compares them with that quarter of each pack
  picture, and the importer cuts that quarter out losslessly (ffmpeg). Compared
  whole, only 8 of them matched, and those 8 went to the wrong rooms.
- Seven `Objspr` pages (`OSP00031`, `00093`, `00094`, `01184`, `01234`,
  `04101`, `04105` ...) fill the part no sprite uses with index 255 =
  `0x7FFF`, opaque white; the pack leaves it transparent. The generator leaves
  out every 16x16 block of nothing but that filler, or the white block
  outweighs the picture and the page matches nothing (room 109's masks had no
  HD page for that reason). `OSP02062`, `OSP03065` and `OSP03071` have no
  pack page.
- The bordered-screen rooms (`MSF2_SCREEN_BORDER`: the caves, `RC2000`,
  `RC50D0`, `RC5150`) hold a 316x236 picture. `RC3062` says so in its header;
  the others carry it under a 320x240 header and the game reads it 316 wide,
  so the generator picks the reading that is smoother row to row. The game
  names the upload at the size it is made (`room_hd_bg_width/height`). The
  Seamless HD Project re-lit these rooms hard, so they have a looser limit, and
  a short list of pairs over their limit was checked by eye (`VERIFIED`).

Each is written as
`HD/<USA|JPN>/Stage<n>/RC....webp` / `HD/<USA|JPN>/Objspr/OSP....webp`, named
after the tree's own file, so this part needs no dump and does not depend on
the tree's background format. A copy an earlier version of the importer wrote
under the texture's hash is removed (the hash is still computed from the
tree for that, and to keep the dump matching off these textures).

- the masks are the base game's `Objspr` pages. The Director's Cut arrange
  rooms and Saturn mode read their masks from the room file, which are
  different pictures; those come in through the dump, below.

**1b. The same table, for character and enemy skins.** Classic REbirth's game
uploads an `.EMD`'s TIM once per palette row (the whole TIM in that palette)
and names each upload with its CRC, so a pack has one picture per row. The
generator pairs each with an (`.EMD`, row):

- by Classic REbirth's name first: the CRC of the TIM in that palette, with
  the `0x8000` -> `0x8001` fix `FixClutVertexData` applies to the first row
  before the upload (without it EM1004's row 0 does not match). 27 of the 63
  distinct rows are found this way;
- the rest by picture, over **only the half the row is drawn from** (row 0
  the left 128 texels, row 1 the right): the artists often repainted or
  blacked out the half a palette never shows, which sinks a whole-picture
  comparison. Mutual closest match under 6.0, or under 15.0 when it is at most
  half the next candidate's error (faces repainted from scratch: Barry, Jill,
  EM1031). Rows whose region pixels are identical (EM1025 / EM1029's right
  half, EM1010 / EM1110) are matched as one. 25 more rows; every accepted
  pair was checked by eye;
- a one-row skin narrower than the page that neither found (EM100A/E/F,
  EM1013, EM1114/15 and their EM11xx twins) by the shape of the WHOLE pack
  page: the pack has these stretched over the square page, as it has the
  doors. Grey-level correlation of the texture stretched to a square against
  each whole picture, at least 0.85, 0.1 ahead of the next and mutual (row
  kind `ModelWhole`, installed as it is). A texture that is nearly all black
  has no grey levels to compare, and texels the artist cleared sink the grey
  score: the crow (EM1005, the Seamless HD Project's `E6D7E5CD`) scores 0.32.
  Those go by their outline - the alpha channels correlating at least 0.7,
  0.3 ahead of the next picture (the crow: 0.79 against 0.21), mutual, with
  the grey levels agreeing where both are opaque (0.5). 12 more rows.

The importer joins row 0's left half and row 1's right half (ffmpeg, lossless
WebP; the second picture is scaled to the first's size if the packs differ)
into the picture described in [Model skins by file](#model-skins-by-file), and
writes it as `HD/<USA|JPN>/Enemy/<EMD name>.webp`. No hash and no dump is
involved. Seamless HD Project yields 49 models, RE-ENHANCE 34. A two-row model
the pack has only one row of is left out (half a model would be drawn with the
other half's palette). Not found in either pack: the 128-texel-wide one-row
TIMs (EM1005, EM100A/B/E/F, EM1013-15 and their EM11xx twins), EM1030, and
EM1029's right half (two pack pictures tie).

**1c. The same table, for file textures and the Director's Cut set.**

- Files: `Data/` (18), `Item_m2/` maps and item views (65), `Item_m1/` doors,
  ladders and lifts (30) and `Effspr/` pages (31) - 144 of 195. Each file the game names (see
  [Replacement by file path](#replacement-by-file-path)) is rendered as its
  loader uploads it - a 4/8 bpp TIM in palette row 0, a 16 bpp TIM or 320x240
  `.pix` as it is; the TIM inside a `.ivm` / `.dor` - and paired by Classic
  REbirth's name of that picture (73), else by picture (55): mutual closest
  under 6.5, or, for a repainted or recoloured picture, mutual, under 30,
  with a normalized cross-correlation of the grey thumbnails of at least 0.75
  and 0.25 ahead of the next candidate; every picture match also needs a
  correlation of at least 0.3, which keeps a mostly dark texture (a ladder
  strip, an esp page with a few sparks) off an unrelated dark picture. The
  weakest of each folder were checked by eye. A square pack page standing for
  a smaller texture comes in one of two layouts, and both are compared: the
  texture in the page's top-left corner (a 256x160 map, `ITEMBOXn`; row kind
  `File`, cut out), or the whole texture stretched over the page (every
  `Item_m1` door, ladder and lift: a 128x256 door fills a 1024x1024 picture;
  row kind `FileWhole`, installed as it is - the game maps an HD picture over
  the texture's logical size whatever its shape). Cutting the corner out of a
  stretched door installed its left half, drawn stretched over the whole
  door. Written as `HD/<USA|JPN>/<folder>/<FILE>.webp`.

  Two more sets: the six projector slides, rendered as their cells and
  written as `HD/<TREE>/Data/SLIDE/<NN>.webp` (row kind `Cell`, the page's
  corner; all six, three of them in `VERIFIED` because the pack re-typeset
  their text), and, with `--jpn <JPN tree>`, the files only the Japanese
  tree has - the file reader's `Item_m2/FILEI*` backdrops (11 of the 14 it
  reads) and `TEXTM*` pages (none: text on black has nothing to correlate).
  A row is installed into whichever tree has the file; a tree without it
  skips the row quietly. Those backdrops and the slides key their black
  (`0x0000`) as transparent while the pack page is opaque, so a texture at
  least 15% opaque is compared with its transparent texels as opaque black
  when the pack view has no transparency (`view_error`); compared with alpha,
  the same picture scored 38. Emptier textures keep the plain comparison, and
  a pack picture's own best match is taken only among textures that share
  structure with it (correlation 0.3) - otherwise the 32x32 `ARROR`, nearly
  all transparent, was every black page corner's best match. That also paired
  `DOOR08` and the three `ELE01` lifts (checked by eye). In all: 163 rows.
- Pairs identified in game (`USER_*_PAIRS` in the generator), written
  whether or not the matcher finds them: the room 304 corridor
  (`Stage3/RC3044`, re-lit in the Seamless HD Project), the save screen's
  typewriter (`Data/TYPE00`), the all-maps selector (`Item_m2/MAP0E`), the
  ending report (`Data/EN07`, MulderLoad's English page), the Japanese
  reader's fax, security system and plant 42 report backdrops
  (`FILEI07/11/16`), Neptune's skin (`EM100B` / `EM110B`, one row: the pack
  page's left half), and three room-model textures in row kind `Named`,
  installed as they are under the name the game looks them up by: the
  guardhouse map and room 1F0's shelf by their TIM
  (`HD/<TREE>/RoomModels/667bc53f`, `99817e6a`), and the courtyard map by the
  upload's hash (`HD/82152f2276f2176d`): the game darkens that map's palette
  before the upload, so it has no file name.
- Director's Cut: the Seamless HD Project's `bio1dc/hires` (the TeamX DC
  camera angles) is paired with the DC overlay tree, which the generator takes
  as `--dc <DC folder>`: 91 of 91 backgrounds, and 67 of 74 masks against the
  masks kept inside the DC room files (187 rows, one per arrange-room file that
  carries the picture). Installed as `HD/DC/Stage<n>/RC....webp` and
  `HD/DC/Stage<n>/ROOM...._MASK<camera>.webp` when that folder is imported as
  *Director's Cut*; the import refuses to run when the target has no `DC/`
  folder. There is no HD pack for the Saturn release.

**2. The game's own dump, for everything else** - models, doors, menus, items,
effects, and the backgrounds and masks the table lacks. With
`[Textures] DumpTextures=1` the game writes each texture it uploads to
`HD/dump/<name>.png`; the importer pairs those with the rest of the pack:

- by Classic REbirth's name, when the dumped pixels give a CRC the pack has a
  file for. This is exact, and catches pictures that were repainted too far to
  be matched by eye (the status screen);
- otherwise by picture: mutual closest match under a per-folder threshold. A
  pack picture is compared whole when it has the texture's shape, and by its
  top-left part when it is a 256x256 page holding a smaller texture (Classic
  REbirth hands textures over as 256x256 pages, so a 256x160 map is a square
  picture with the map at the top), and also whole when it is such a page
  (the texture stretched over it, as the doors are). A part is cut out with
  ffmpeg and written as its own file; a whole page goes in as it is.

A matched texture the game also dumped under a file name - the same pixels in
`HD/dump/<TREE>/<path>.png` - is written under that name,
`HD/<TREE>/<path>.webp`, instead of its hash (unless the table has already
given that name a picture in this import), and a copy under the hash is
removed. The models inside the room files need no dump for this: the importer
reads every `Stage*/ROOM*.RDT` of the tree, renders each object and item
model TIM row as the game uploads it (index 0 transparent, a `0x0000` entry
elsewhere as `0x0001`, `0x8000` as `0x8001` - checked texel for texel against
the game's dump) and names it `RoomModels/<crc>` (`_P<row>`), one file for
every room that carries the TIM. Per-room names an earlier version wrote
(`Stage1/ROOM1091_ITEM0.webp`) are removed. Where one picture still has
several names, the others are hard links to the first file (copies where the
file system has no links). Only what the game cannot name stays
`HD/<hash>.webp`.

Those room renders also take part in the matching as if the game had dumped
them, so a room never played is paired too. Most were repainted, so after the
colour comparison above a third pass pairs what is left of them by SHAPE: the
pack picture whose grey levels correlate best with the texture (whole, or as
the top-left part of a page), when that is at least 0.85, 0.1 ahead of the
next different picture and within 30 in colour; two pack pictures that are the
same picture count as one, and a picture the file table already used may be
used again (the Umbrella computer screens are a `Data/` file and a room model).
Colour alone sent a recoloured bookshelf to a statue. A dump that is all but
the same picture as a room render (an older build's knock-out twin of it, or a
palette reading the game did not fix up) is named after that render, and the
fix-ups themselves are hashed with and without (`ROOM5130`'s gun parts keep
their black). From one session's dump: 106 room-model textures of 122.

Only what the game has dumped can be matched this way, so the coverage grows
with the dump: play, run the import, repeat. From a 304-texture dump of a
short session the Seamless HD Project gave 2 more backgrounds, 22 more masks
and 31 `misc` / `skin` / `skin2` files on top of the table.

What stays out cannot be told apart with confidence - the `_XX` suffixed
variants of one camera, nearly empty mask pages several rooms share - or has
no counterpart here: this port uploads some model textures in a different
layout than Classic REbirth's pages. A wrong mask draws another room's
cut-outs, so the mask threshold is the strictest.

## HD movies

A movie is looked up by name too: `CheckVideoFileExists`
(`src/video/VideoPlayback.cpp`) keys the FMV table's path the same way
(`AssetSourceKey`, `.\usa\MOVIE\OU.avi` -> `USA/MOVIE/OU`) and plays
`HD/USA/MOVIE/OU.mp4` when it exists (`HdTextures_NamedFile`), ahead of the
tree's own `.mp4` / `.avi`. Only the PS1 ending credits come first. It follows
`HdTextures` like everything else in `HD/`.

The migrator's HD Pack tab fills it from the Seamless HD Project's `movie`
folder. The pack is named after the Japanese release's movies; for the USA
tree `OJ`/`PJ`/`Ed4`/`Ed5` become `OU`/`PU`/`EU4`/`EU5` and the `*_B` staff
rolls `*_R`. The prologue's Jill cut is picked by the file's duration, and the
pack's `PJ` is the uncensored PC length (226.0 s).

## HD fonts (text drawn, not loaded)

With HD textures on, the game draws its text in real **TrueType fonts** from
the HD folder, each named after the base tree's font file it stands in for
(like any file replacement; the base tree's name serves every mode, the
Director's Cut and Saturn overlays included):

| File | Font | Draws |
|---|---|---|
| `HD/<USA\|JPN>/Data/TITLEFNT.TTF` | Archivo Black | the title menu, PRESS START / ANY BUTTON, the USA sheet's NEW GAME / LOAD GAME, the Director's Cut's STANDARD / TRAINING / ADVANCED |
| `HD/USA/Data/FONTUS.TTF`, `HD/JPN/Data/FONT.TTF` | Archivo Narrow SemiBold | the 8x8 font (`PrintText8x8`: the OPTIONS pages, the debug menu, the lab terminal, ...) and the title's copyright lines |
| `HD/USA/Data/FONTUS_14.TTF` | Libertinus Serif SemiBold (subset) | the 8x14 game text: messages, item names, files, the save screen |
| `HD/JPN/Data/FONT_14.TTF` | M PLUS 2 SemiBold (subset) | the same on the Japanese sheet |

Both fonts are by Omnibus-Type under the SIL Open Font License 1.1, rasterised
by the vendored `src/third_party/stb_truetype.h` (`src/game/HdText.cpp`).
Archivo Narrow is the upstream static SemiBold: stb_truetype draws only a
variable font's default instance, which is Regular. Any TrueType font works in
either place; one without an H to size letters by, or a file stb_truetype
cannot open, leaves that text in the pixel font. Without HD textures, or
without the files, every pixel font is as before. `Font03t.tim` (the
key-config panel's arrows and digits) and the sheet's controller-button glyphs
are not replaced.

Text is drawn at `HdText_Scale()` = `ceil(desktop height / 240)` HD pixels per
game pixel (2 to 8; 5 on a 1080p desktop). The sprite pass point-samples, so
that keeps one texel to about one screen pixel.

### Title menu

The title menu's lines (NEW GAME / LOAD GAME / EXTRA / OPTIONS, BATTLE GAME /
BACK) are a 256x256 4bpp sheet the game builds from `Data/TITLEFNT.BIN`
(`title_font_build_strips`, `src/game/TitleMenuFont.cpp`), so no
picture can be named after a file, and a hash-named one would go stale with
every change to the item list. `title_font_load_strips` draws the same sheet
in `TITLEFNT.TTF` and hands it to the upload as an `HdPictureScope`:
`CreateTextureHd` uploads it for any texture whose pixels are exactly the 4bpp
sheet's, after the hash lookup finds no file. A `HD/<hash>.webp` made from a
dump of the sheet therefore still wins.

Archivo Black is the closest real typeface to the original option text. Its
letters are scaled so the H is the bitmap's 11 rows tall, and each one is
centred on the bitmap letter's cell (`hd_layout`): Archivo Black's own spacing
is far tighter than the original's 3-pixel letter gaps and 15-pixel word gaps,
so the HD lines keep the bitmap's rhythm. The whole line is then drawn at
`TITLE_HD_TEXT_SIZE` (0.8) of the bitmap's size about its centre - letters,
gaps and width alike, the capitals centred on the bitmap's 11 rows - because a
smooth font at the pixel font's full height reads too big on an HD screen. A
line whose round letters would overshoot its band drops down within it.
`tools/gen_title_font_outlines.py` still writes the earlier hand-drawn
letters as `assets/fonts/title_font.ttf`, which can be copied over
`TITLEFNT.TTF`.

### The release's title sheets

PRESS START BUTTON, the copyright lines, the USA sheet's NEW GAME / LOAD GAME
frames and the Director's Cut's difficulty cells are pictures in the release's
sheets (`data\t_start.tim` / `t_press.tim`; the Director's Cut's pages A and B
built from `bt367oab.tim`). `title_font_load_sheet` gives each sheet an HD
picture the same way: the sheet's own texels enlarged, with every text line of
its table (`kSheetUsaStart` ...) drawn again over a band cleared to the line's
colour - the option lines in the title font exactly as the menu draws its own,
centred where the art centres them, in the art's most common colour; the
copyright lines in the 8x8 HD font, at the font's own advances and kerning
plus one even tracking that makes the ink span the art's, the whole line then
drawn at `TITLE_SHEET_COPY_SIZE` (0.7) about its centre. The title shows the
copyright with PRESS START / ANY BUTTON only; the menu draws no copyright strip
(`TitleScreen.cpp`), with or without HD. A sheet gets the picture only when its ink is where the
table says (every inked row inside a listed line), so a release whose art is
laid out differently keeps its own; a line whose font is missing keeps its art.

### The 8x8 font

`PrintText8x8` hands its text to `HdText_Print8x8` once it has set up the
tint, shadow and brightness, and draws from `fontus.tim` only when that
returns 0. The glyphs are rasterised once into an atlas texture (1024 wide, in
the 8x8 sheet's own glyph colour, which `TextureLoader.cpp` records as the
font page goes up) and every character is a sprite of its own
(`AddTintSpriteRect`, `Rendering.cpp`: AddTintSprite's colour, tint and depth
at a fractional rectangle).

- **Size:** capitals 6.5 game pixels tall (`HD8_CAP`), centred on the sheet's
  7-row capitals; descenders hang below the 8-pixel cell.
- **Layout:** callers lay text out in 8-pixel cells - right-aligned values
  (`288 - strlen * 8`), one character per call in a colour of its own, the lab
  terminal's caret on a cell - so a proportional layout would break them.
  Each word keeps its cells' span instead: its ink starts 1 pixel in from the
  first cell and ends 1 pixel short of the last, the gaps between letters
  even (at least 0.6). A lone character sits in the middle of its cell, and
  spaces stay a cell wide. Archivo Narrow is far narrower than the 8x8 font,
  so the letters come out widely spaced.
- **Smaller text:** `HdText_SetPrint8x8Size(size, anchor)` draws what follows
  at a fraction of that size - letters, gaps and width alike, each size from
  an atlas of its own - scaled about the string's left end, centre or right
  end (`x + strlen * 8`), whichever the caller aligned it by. The OPTIONS
  pages (`GameOptions.cpp`) use 0.8 (`OPT_HD_TEXT_SIZE`): labels anchored
  left, the right-aligned values right, the BONUS page's centred names centre.
- **Characters:** the sheet's cells are ASCII from 0x20, with its own glyphs
  at 0x40 (a copyright sign, U+00A9) and 0x5C (a yen sign, U+00A5); 0x60 is
  blank. A character the font lacks, or a byte outside 0x21-0x7E, is drawn
  from the sheet in its own cell (the rest of that word then sits in cells
  too).

### The 8x14 game text

Every 8x14 renderer (`PrintText8x14`, `PrintFormattedText`, the message box,
item names, the save screen, the ending) ends in `AddTintSprite` on a 14-row
cell of the font sheet. `AddTintSprite` asks `HdText_Glyph14` whether that
cell is a character the HD font has - by the USA sheet's table (`kUsa14`, rows
from v 28, 8 wide) or the Japanese one (`JpnFontTable.h` inverted: left page
from v 28, the kanji page from v 0, 14 wide) - and queues an atlas sprite
instead; `hd14_layout_pending` (Rendering.cpp) places them while the queue is
still in drawing order, before the depth sort. The controller buttons and the
interrobang (missing from M PLUS 2) stay the sheet's.

- **Look:** each glyph has the sheet's fill and a dark outline around it in the
  sheet's own two colours, found as the font page goes up (the outline is the
  commonest colour next to a clear texel, the fill the commonest inside one:
  `(222,222,205)` and `(49,49,74)` on `fontus.tim`). The outline is the glyph's
  coverage grown by 0.8 game pixels; tints and fades multiply both, as on the
  sheet.
- **Latin:** capitals 7.2 game pixels tall on the sheet's 10. Cells that follow
  each other, or are one blank cell apart, are one run, set at the font's own
  advances, kerning and space from where its first cell starts; a gap of two
  or more cells (`Yes  No`, columns) starts the next run at its own cell. A
  blank cell that another glyph fills does not join (the save screen draws
  `CHRIS` and the count over a template whose `\` sits between them). A run
  with no letter in it - dashes, numbers, slashes - keeps one glyph centred per
  cell, so the empty save slots line up with the filled ones. A letter's place
  depends only on those before it, so the typing message box never moves what
  is already typed.
- **Japanese:** one glyph per 14-pixel cell, centred on its advance, em 10.
  Not seen on screen yet (no Japanese tree was at hand).

The text fonts are subsets `tools/gen_hd_fonts.py --subset` makes from
google/fonts' `LibertinusSerif-SemiBold.ttf` (Latin-1 and the sheet's
punctuation) and `MPLUS2[wght].ttf` (a variable font whose
default instance is Thin: instanced at wght 600, cut to the Japanese
sheet's 596 characters plus ASCII). Neither licence names a Reserved Font Name.

### Migrator

The migrator carries the fonts (`tools/asset_migrator/fonts/`, embedded as
`core/HdFont*.inc` by `tools/gen_hd_fonts.py`; each file's name table holds its
licence) and writes the tree's three. Its HD Pack
tab writes them (`core/HdFonts.h`) with every original-game import and its PC
tab refreshes them when `HD/` already exists (it never creates `HD/`, which
would switch HD textures on); both delete the `TITLEFNT.VEC` outlines an
earlier version wrote there. `re1am_selftest hdfonts TARGET [jpn]` writes them
into `TARGET/HD/<USA|JPN>/Data` on their own.

## config.ini

```ini
[Textures]
; 1 = use the replacements in HD/ (default). 0 = ignore the folder.
HdTextures=1
; 1 = write every uploaded texture to HD/dump/<hash>.png, once each, and each
; model skin to HD/dump/<TREE>/<path>.png.
DumpTextures=0
```

Named backgrounds have no key.
