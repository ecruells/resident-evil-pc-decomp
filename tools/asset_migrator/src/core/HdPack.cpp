#include "core/HdPack.h"

#include "core/BackgroundWebp.h"
#include "core/Pak.h"
#include "core/Process.h"
#include "core/HdFonts.h"
#include "core/Util.h"

// The game's own texture naming, so the files written here are found by it.
#include "HdTextureHash.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <system_error>

namespace re1 {

namespace {

// ModelPalette0/1: the pack file is Classic REbirth's upload of an .EMD's
// texture in that CLUT row (the whole TIM in that palette); `asset` is the
// .EMD. The port draws a model from one picture named after the file - the
// left half in row 0, the right half in row 1 - so two rows make one file.
// ModelWhole: a one-row skin narrower than the page (the crow, EM1005; the
// plant's vines) that the pack has stretched over its whole square page, the
// way it hands the doors over: installed as it is.
//
// File: a texture the game builds straight from one file, named after it -
// Data/ screens and pages (HD/<TREE>/Data/STATUS.webp), Item_m2 maps and item
// views (.ivm), Item_m1 doors (.dor) and Effspr pages (the weapon-effect
// bands are cut out of the HD page in the game). Classic REbirth hands a
// texture smaller than 256x256 over in one of two layouts, and the generator
// tells them apart by picture: File, the texture in the top-left corner of a
// 256x256 page (a map, ITEMBOXn), which is cut out; FileWhole, the whole
// texture stretched over the square page (every Item_m1 door), which goes in
// as it is - the game maps an HD picture over the texture's logical size,
// whatever its shape. Cell: a part of a file the game uploads on its own and
// names "<file>/<NN>" - the slide projector's six 192x128 slides, cut out of
// Data/SLIDE.TIM ("Data/SLIDE/03"), each the top-left corner of a pack page,
// and the USA file reader's 256x256 pages, one per page of an
// Item_m2/FILEM_*.PIX container ("Item_m2/FILEM_N0/04").
// A File row whose file the tree does not have is skipped: the table covers
// both trees (the Japanese reader's Item_m2/FILEI backdrops are JPN only).
// Named: a texture the game names by something other than a file - a room
// TIM's CRC ("RoomModels/667bc53f") or, for one whose palette the game edits
// before the upload, the upload's own hash ("82152f2276f2176d") - paired in
// game by the user; the pack picture goes in as it is, under HD/<TREE>/ or,
// for a hash, HD/.
// Backdrop: a USA file reader page with a picture ("Item_m2/FILEM_P0/01"),
// which no pack has; the row names the pack picture of the Japanese reader's
// backdrop of the same picture, and the importer builds the HD page from the
// page and that backdrop (composeBackdropPage).
// DcBackground / DcMask: the pack's bio1dc set
// (its Director's Cut camera angles), paired with the DC overlay's own files
// and installed under HD/DC/ - a background as Stage8/RC8000, a mask kept in
// the room file as Stage8/ROOM8000_MASK3 (src/game/RoomBackgroundWebp.h).
enum class HdPackKind { Background, Mask, ModelPalette0, ModelPalette1, File, FileWhole,
                        DcBackground, DcMask, ModelWhole, Cell, Named,
                        Backdrop };

// The slide projector's cells (src/game/SpriteRenderer.cpp, TexturePage_LoadImage).
const int kSlideCellWidth = 192;
const int kSlideCellHeight = 128;
// A page of the USA file reader (src/game/MainMenu.cpp, pickup_unpack_list).
const int kFilePageSize = 256;

struct HdPackEntry {
    const char* packName;   // file name in the pack, without extension
    HdPackKind kind;
    const char* asset;      // game file relative to the tree, without extension
};

const HdPackEntry kTable[] = {
#include "core/HdPackTable.inc"
};

// Director's Cut models that wear a base model's skin under another name. The
// game looks a skin up by the model's file (HD/DC/Enemy/EM1116.webp), and no
// pack names one after these, so the base skin is written there too, as a
// file of its own. The zombie Forest (DC entity id 0x16: EM1016 in Chris's
// game, EM1116 in Jill's, one TIM) is the Forest corpse EM1026: the same
// 256x256 skin and UV layout, with about 11% of the texels retouched (wound
// edges, a badge).
struct DcModelSkinCopy {
    const char* base;   // the pack table's asset ("Enemy/EM1026")
    const char* dc;     // the DC file, under HD/DC/Enemy/
};
const DcModelSkinCopy kDcModelSkinCopies[] = {
    {"Enemy/EM1026", "EM1016"},
    {"Enemy/EM1026", "EM1116"},
};

// The entry of `dir` named `name`, compared case-insensitively ("" when none).
std::string childCI(const std::string& dir, const std::string& name, bool wantDir) {
    const std::string low = toLower(name);
    for (const auto& e : listDirectory(dir)) {
        if (toLower(e) != low) continue;
        const std::string path = joinPath(dir, e);
        if (wantDir ? isDirectory(path) : isRegularFile(path)) return e;
    }
    return std::string();
}

// A room background as display_image / display_image_rgba hand it to the
// renderer: 320x240, every pixel opaque. From the .pak each 5-bit channel is
// expanded v * 255 / 31; from a .webp the pixels are the decoder's (the same
// decoder the game uses), which is why a lossy tree names its backgrounds
// differently.
bool backgroundRgba(const std::string& dir, const std::string& pakName,
                    std::vector<uint8_t>* rgba, std::string* why) {
    const std::string file = findBackgroundFile(dir, pakName);
    if (file.empty()) {
        *why = "not in the tree";
        return false;
    }
    std::vector<uint8_t> data;
    if (!readFile(joinPath(dir, file), &data)) {
        *why = "cannot read " + file;
        return false;
    }
    if (extensionOf(file) == ".webp") {
        int w = 0, h = 0;
        if (!decodeWebp(data, rgba, &w, &h)) {
            *why = file + " does not decode";
            return false;
        }
        if (w != kBackgroundWidth || h != kBackgroundHeight) {
            // Already somebody's replacement: the game shows it as it is and
            // never looks it up in HD/.
            *why = file + " is not 320x240 (already a replacement)";
            return false;
        }
        for (size_t i = 3; i < rgba->size(); i += 4) (*rgba)[i] = 0xFF;
        return true;
    }
    std::vector<uint8_t> rgb;
    if (!backgroundPakToRgb(data, &rgb, why)) return false;
    rgba->resize((size_t)kBackgroundWidth * kBackgroundHeight * 4);
    for (size_t i = 0, n = rgb.size() / 3; i < n; ++i) {
        (*rgba)[i * 4 + 0] = rgb[i * 3 + 0];
        (*rgba)[i * 4 + 1] = rgb[i * 3 + 1];
        (*rgba)[i * 4 + 2] = rgb[i * 3 + 2];
        (*rgba)[i * 4 + 3] = 0xFF;
    }
    return true;
}

// A 4/8 bpp TIM with a CLUT in its first CLUT row, v * 255 / 31, alpha 0 for
// a texel whose colour is 0x0000 exactly.
bool timRgba(const std::vector<uint8_t>& tim, std::vector<uint8_t>* rgba, int* width,
             int* height, std::string* why) {
    const size_t n = tim.size();
    const uint32_t flags = readU32(tim.data(), n, 4);
    if (n < 32 || readU32(tim.data(), n, 0) != 0x10 || !(flags & 8) || (flags & 3) > 1) {
        *why = "not a 4/8 bpp TIM with a CLUT";
        return false;
    }
    const int bpp = (flags & 3) == 0 ? 4 : 8;
    const int cw = readU16(tim.data(), n, 16);
    const int ch = readU16(tim.data(), n, 18);
    const size_t clutOff = 20;
    const size_t img = clutOff + (size_t)cw * ch * 2;
    const int words = readU16(tim.data(), n, img + 8);
    const int h = readU16(tim.data(), n, img + 10);
    const int w = words * 16 / bpp;
    const size_t pix = img + 12;
    const int colours = bpp == 4 ? 16 : 256;
    if (cw * ch < colours || w <= 0 || h <= 0 || pix + (size_t)words * 2 * h > n) {
        *why = "truncated TIM";
        return false;
    }
    uint8_t pal[256][4];
    for (int c = 0; c < colours; ++c) {
        const uint16_t clr = readU16(tim.data(), n, clutOff + (size_t)c * 2);
        pal[c][0] = (uint8_t)((clr & 0x1F) * 255 / 31);
        pal[c][1] = (uint8_t)(((clr >> 5) & 0x1F) * 255 / 31);
        pal[c][2] = (uint8_t)(((clr >> 10) & 0x1F) * 255 / 31);
        pal[c][3] = clr == 0 ? 0x00 : 0xFF;
    }
    rgba->resize((size_t)w * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int idx;
            if (bpp == 8) {
                idx = tim[pix + (size_t)y * w + x];
            } else {
                const uint8_t b = tim[pix + (size_t)y * (w / 2) + x / 2];
                idx = (x & 1) ? (b >> 4) : (b & 0xF);
            }
            std::memcpy(&(*rgba)[((size_t)y * w + x) * 4], pal[idx], 4);
        }
    }
    *width = w;
    *height = h;
    return true;
}

// A room mask page as LoadEffectTextureSheet (src/game/TextureLoader.cpp)
// builds it from Objspr/osp*.pak (timRgba's colours and key).
bool maskRgba(const std::string& path, std::vector<uint8_t>* rgba, int* width,
              int* height, std::string* why) {
    std::vector<uint8_t> pak, tim;
    if (!readFile(path, &pak)) {
        *why = "cannot read the .pak";
        return false;
    }
    std::string err;
    if (!lzwUnpack(pak.data(), pak.size(), &tim, &err)) {
        *why = "LZW: " + err;
        return false;
    }
    return timRgba(tim, rgba, width, height, why);
}


// ---------------------------------------------------------------------------
// Matching by picture, against the game's own texture dump.
//
// The table above covers what can be rebuilt from the game's files without
// running it. Everything else - model skins, menu and item art, effect sheets,
// and the backgrounds and masks the table leaves out - is uploaded by loaders
// this tool does not replicate. For those the game itself is the reference:
// with [Textures] DumpTextures=1 it writes every texture it uploads to
// HD/dump/<name>.png, and a pack picture that shows the same thing as a dumped
// texture is that texture's replacement.
// ---------------------------------------------------------------------------

// Thumbnails are compared as kThumb x kThumb premultiplied RGBA.
constexpr int kThumb = 48;
constexpr size_t kThumbBytes = (size_t)kThumb * kThumb * 4;

// Largest mean absolute difference (0-255) a pair may have, per pack folder.
// Masks are the strict ones: a wrong mask draws another room's cut-outs.
double maxError(const std::string& kind) {
    if (kind == "mask") return 4.0;
    if (kind == "bgd") return 9.0;
    return 6.0;
}

// Thumbnail of the top-left `rw` x `rh` part of a `w`-wide image.
void makeThumb(const uint8_t* rgba, int w, int rw, int rh, uint8_t* out) {
    for (int ty = 0; ty < kThumb; ++ty) {
        const int y0 = ty * rh / kThumb;
        const int y1 = std::max(y0 + 1, (ty + 1) * rh / kThumb);
        for (int tx = 0; tx < kThumb; ++tx) {
            const int x0 = tx * rw / kThumb;
            const int x1 = std::max(x0 + 1, (tx + 1) * rw / kThumb);
            uint64_t sum[4] = {0, 0, 0, 0};
            for (int y = y0; y < y1 && y < rh; ++y) {
                const uint8_t* p = rgba + ((size_t)y * w + x0) * 4;
                for (int x = x0; x < x1 && x < rw; ++x, p += 4) {
                    const uint32_t a = p[3];
                    sum[0] += p[0] * a;
                    sum[1] += p[1] * a;
                    sum[2] += p[2] * a;
                    sum[3] += a;
                }
            }
            const uint64_t n = (uint64_t)(y1 - y0) * (x1 - x0);
            uint8_t* o = out + ((size_t)ty * kThumb + tx) * 4;
            o[0] = (uint8_t)(sum[0] / (n * 255));
            o[1] = (uint8_t)(sum[1] / (n * 255));
            o[2] = (uint8_t)(sum[2] / (n * 255));
            o[3] = (uint8_t)(sum[3] / n);
        }
    }
}

double thumbError(const uint8_t* a, const uint8_t* b) {
    uint64_t sum = 0;
    for (size_t i = 0; i < kThumbBytes; ++i) sum += (uint64_t)std::abs((int)a[i] - (int)b[i]);
    return (double)sum / (double)kThumbBytes;
}

// The game's dump format (src/system/PngImage.cpp PngEncodeRGBA): 8-bit RGBA,
// filter 0, one zlib stream of stored blocks. Anything else is not a dump.
bool readDumpPng(const std::vector<uint8_t>& d, std::vector<uint8_t>* rgba, int* w, int* h) {
    static const uint8_t kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (d.size() < 33 || std::memcmp(d.data(), kSig, 8) != 0) return false;
    auto be32 = [&](size_t o) {
        return ((uint32_t)d[o] << 24) | ((uint32_t)d[o + 1] << 16) | ((uint32_t)d[o + 2] << 8) | d[o + 3];
    };
    const uint32_t width = be32(16), height = be32(20);
    if (d[24] != 8 || d[25] != 6 || width == 0 || height == 0 || width > 4096 || height > 4096)
        return false;
    std::vector<uint8_t> z;
    for (size_t pos = 8; pos + 12 <= d.size();) {
        const uint32_t len = be32(pos);
        if (pos + 12 + (size_t)len > d.size()) return false;
        if (std::memcmp(&d[pos + 4], "IDAT", 4) == 0)
            z.insert(z.end(), d.begin() + pos + 8, d.begin() + pos + 8 + len);
        pos += 12 + (size_t)len;
    }
    const size_t stride = (size_t)width * 4 + 1;
    std::vector<uint8_t> raw;
    raw.reserve(stride * height);
    for (size_t pos = 2; pos + 5 <= z.size();) {
        const bool last = (z[pos] & 1) != 0;
        if ((z[pos] & 6) != 0) return false;            // not a stored block
        const size_t n = z[pos + 1] | ((size_t)z[pos + 2] << 8);
        if (pos + 5 + n > z.size()) return false;
        raw.insert(raw.end(), z.begin() + pos + 5, z.begin() + pos + 5 + n);
        pos += 5 + n;
        if (last) break;
    }
    if (raw.size() != stride * height) return false;
    rgba->resize((size_t)width * height * 4);
    for (uint32_t y = 0; y < height; ++y) {
        if (raw[y * stride] != 0) return false;         // filter 0 only
        std::memcpy(&(*rgba)[(size_t)y * width * 4], &raw[y * stride + 1], (size_t)width * 4);
    }
    *w = (int)width;
    *h = (int)height;
    return true;
}

struct DumpTexture {
    std::string name;       // 16 hex digits
    int w = 0, h = 0;
    std::vector<uint8_t> thumb;
    std::string crName;     // what Classic REbirth would call these pixels
    bool taken = false;
    // The names the game also dumped these exact pixels under
    // (HD/dump/<TREE>/<path>.png): a texture it can name by its file.
    std::vector<std::string> namedKeys;
};

// A path as a set key: lower case, '/' separators, no extension.
std::string pathKey(std::string p) {
    std::replace(p.begin(), p.end(), '\\', '/');
    const size_t dot = p.rfind('.');
    if (dot != std::string::npos && p.find('/', dot) == std::string::npos) p.erase(dot);
    return toLower(p);
}

struct PackPicture {
    std::string kind;       // the pack folder it is in
    std::string path;
    int w = 0, h = 0;
    std::vector<uint8_t> thumb;
    // Thumbnails of its top-left part, for the case below, keyed by the
    // original texture size the part would stand for.
    std::map<std::pair<int, int>, std::vector<uint8_t>> partThumb;
    // Already installed for something else (2b skips it; the room model pass
    // may use it again: the same art can stand for a file and a room model).
    bool used = false;
};

// How alike two thumbnails are in SHAPE, whatever their colours: the
// correlation of their grey levels (1 = the same picture up to brightness and
// contrast). A repainted picture keeps its shape and loses its colours, which
// is what the room model pass needs (thumbError alone sends a recoloured
// bookshelf to a statue).
double thumbShape(const uint8_t* a, const uint8_t* b) {
    const size_t n = (size_t)kThumb * kThumb;
    std::vector<double> ga(n), gb(n);
    double ma = 0, mb = 0;
    for (size_t i = 0; i < n; ++i) {
        ga[i] = 0.299 * a[i * 4] + 0.587 * a[i * 4 + 1] + 0.114 * a[i * 4 + 2];
        gb[i] = 0.299 * b[i * 4] + 0.587 * b[i * 4 + 1] + 0.114 * b[i * 4 + 2];
        ma += ga[i];
        mb += gb[i];
    }
    ma /= (double)n;
    mb /= (double)n;
    double ab = 0, aa = 0, bb = 0;
    for (size_t i = 0; i < n; ++i) {
        ab += (ga[i] - ma) * (gb[i] - mb);
        aa += (ga[i] - ma) * (ga[i] - ma);
        bb += (gb[i] - mb) * (gb[i] - mb);
    }
    return (aa > 0 && bb > 0) ? ab / std::sqrt(aa * bb) : 0.0;
}

// Classic REbirth hands the game's textures over as pages of 256x256, so a
// pack picture for a smaller texture - a 128x256 door, a 256x133 mask strip -
// is a square image with the texture in its top-left corner and padding
// beside it. This game uploads the texture at its own size, so only that
// corner is the replacement. True when `pic` can be such a page for a
// `dw` x `dh` texture; the corner's size in the pack image goes to rw/rh.
bool pagePart(const PackPicture& pic, int dw, int dh, int* rw, int* rh) {
    if (pic.w != pic.h || dw > 256 || dh > 256 || (dw == 256 && dh == 256)) return false;
    if (pic.w % 256 != 0) return false;
    *rw = pic.w / 256 * dw;
    *rh = pic.h / 256 * dh;
    return *rw > 0 && *rh > 0;
}

// The name Classic REbirth gives a texture (docs/HD_TEXTURES.md): CRC32 over
// BGRA, each channel's five bits shifted left by three, alpha 0 where the
// colour is black. It holds for the textures the game builds straight from a
// file, which is where a pack picture can be identified exactly even when it
// was repainted beyond what a comparison of pictures accepts.
std::string classicRebirthName(const std::vector<uint8_t>& rgba) {
    static uint32_t table[256];
    static bool built = false;
    if (!built) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        built = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i + 4 <= rgba.size(); i += 4) {
        const uint8_t r = (uint8_t)(((rgba[i] * 31 + 127) / 255) << 3);
        const uint8_t g = (uint8_t)(((rgba[i + 1] * 31 + 127) / 255) << 3);
        const uint8_t b = (uint8_t)(((rgba[i + 2] * 31 + 127) / 255) << 3);
        const uint8_t px[4] = {b, g, r, (uint8_t)((r | g | b) ? 0xFF : 0x00)};
        for (int k = 0; k < 4; ++k) crc = table[(crc ^ px[k]) & 0xFF] ^ (crc >> 8);
    }
    char name[16];
    std::snprintf(name, sizeof(name), "%08X", (unsigned)~crc);
    return name;
}

// The top-left `rw` x `rh` of a pack picture as its own WebP, through ffmpeg.
// `lossless` for a mask, whose cut-out edges a lossy encode would soften.
bool cropWebp(const std::string& ffmpeg, const std::string& in, int rw, int rh,
              const std::string& tmp, std::vector<uint8_t>* out, bool lossless = false) {
    const std::string crop = "crop=" + std::to_string(rw) + ":" + std::to_string(rh) + ":0:0";
    std::vector<std::string> args = {"-hide_banner", "-loglevel", "error", "-y", "-i", in,
                                     "-vf", crop, "-c:v", "libwebp"};
    if (lossless) args.insert(args.end(), {"-lossless", "1"});
    else args.insert(args.end(), {"-quality", "92"});
    args.insert(args.end(), {"-compression_level", "6", tmp});
    int code = -1;
    std::string so, se, err;
    if (!ChildProcess::run(ffmpeg, args, false, &code, &so, &se, &err) ||
        code != 0)
        return false;
    const bool ok = readFile(tmp, out) && !out->empty();
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    return ok;
}

// The texture at the end of an .EMD: its size and how many CLUT rows it has.
// The directory is the file's last five dwords; [4] is the TIM's offset.
bool emdTextureShape(const std::vector<uint8_t>& emd, int* w, int* h, int* rows) {
    const size_t end = emd.size() & ~(size_t)3;
    if (end < 0x14 + 32) return false;
    const size_t tim = readU32(emd.data(), emd.size(), end - 0x14 + 16) & ~3u;
    const size_t n = emd.size();
    if (tim + 32 > n || readU32(emd.data(), n, tim) != 0x10) return false;
    const uint32_t flags = readU32(emd.data(), n, tim + 4);
    if (!(flags & 8) || (flags & 7) > 1) return false;
    const size_t img = tim + 8 + readU32(emd.data(), n, tim + 8);
    if (img + 12 > n) return false;
    const int words = readU16(emd.data(), n, img + 8);
    *w = words * ((flags & 7) == 0 ? 4 : 2);
    *h = readU16(emd.data(), n, img + 10);
    *rows = readU16(emd.data(), n, tim + 18);
    return *w > 0 && *h > 0 && *w <= 256 && *h <= 256 && *rows > 0;
}

// The port's picture for a model (src/marni/MarniSystem.cpp, HdModelComposite)
// from the pack's per-palette pictures: each pack picture is a 256x256 page at
// its own scale with the texture in the top-left `w` x `h`; row 0's picture
// gives the left half, row 1's the right one (a one-row texture is all row 0).
// Encoded losslessly, so the pack's pixels pass through unchanged.
bool composeSkin(const std::string& ffmpeg, const std::string& pal0, const std::string& pal1,
                 int w, int h, const std::string& tmp, std::vector<uint8_t>* out,
                 std::string* why) {
    std::vector<uint8_t> file, rgba;
    int pw = 0, ph = 0;
    if (!readFile(pal0, &file) || !decodeWebp(file, &rgba, &pw, &ph)) {
        *why = pal0 + " does not decode";
        return false;
    }
    const int tw = pw * w / 256, th = ph * h / 256;   // the texture in the picture
    std::vector<std::string> args = {"-hide_banner", "-loglevel", "error", "-y", "-i", pal0};
    std::string filter;
    if (pal1.empty()) {
        if (w == 256 && h == 256) {
            *out = file;                                   // already the whole picture
            return true;
        }
        filter = "crop=" + std::to_string(tw) + ":" + std::to_string(th) + ":0:0";
    } else {
        int qw = 0, qh = 0;
        std::vector<uint8_t> file1, rgba1;
        if (!readFile(pal1, &file1) || !decodeWebp(file1, &rgba1, &qw, &qh)) {
            *why = pal1 + " does not decode";
            return false;
        }
        args.insert(args.end(), {"-i", pal1});
        const std::string half = std::to_string(tw / 2), hh = std::to_string(th);
        // The second picture is brought to the first one's size, in case the
        // two rows came from packs of different resolutions.
        filter = "[0:v]crop=" + half + ":" + hh + ":0:0[l];[1:v]scale=" + std::to_string(pw) +
                 ":" + std::to_string(ph) + ":flags=lanczos,crop=" + std::to_string(tw - tw / 2) +
                 ":" + hh + ":" + half + ":0[r];[l][r]hstack=inputs=2";
    }
    args.insert(args.end(), {pal1.empty() ? "-vf" : "-filter_complex", filter, "-pix_fmt", "bgra",
                             "-c:v", "libwebp", "-lossless", "1", "-compression_level", "4", tmp});
    int code = -1;
    std::string so, se, err;
    if (!ChildProcess::run(ffmpeg, args, false, &code, &so, &se, &err) || code != 0) {
        *why = "ffmpeg could not build it (it needs libwebp)" + (se.empty() ? "" : ": " + se);
        return false;
    }
    const bool ok = readFile(tmp, out) && !out->empty();
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    if (!ok) *why = "ffmpeg wrote nothing";
    return ok;
}

// Whether `d` is a file reader page container (Item_m2/FILEM_*.PIX: sixteen
// dword offsets to LZW-packed 256x256 TIMs, the last one the file's end) with
// a page `page` ("04").
bool isFilePageContainer(const std::vector<uint8_t>& d, const std::string& page) {
    const size_t n = d.size();
    if (n < 64 || readU32(d.data(), n, 0) != 64 || page.empty()) return false;
    char* end = nullptr;
    const long p = std::strtol(page.c_str(), &end, 10);
    if (*end != '\0' || p < 0 || p >= 16) return false;
    const uint32_t off = readU32(d.data(), n, (size_t)p * 4);
    return off >= 64 && off < n;
}

// The size of the image in a TIM at `off` (4, 8 or 16 bpp).
bool timShape(const std::vector<uint8_t>& d, size_t off, int* w, int* h) {
    const size_t n = d.size();
    if (off + 20 > n || readU32(d.data(), n, off) != 0x10) return false;
    const uint32_t flags = readU32(d.data(), n, off + 4);
    if ((flags & 3) == 3) return false;
    size_t img = off + 8;
    if (flags & 8) img += readU32(d.data(), n, off + 8);
    if (img + 12 > n) return false;
    const int words = readU16(d.data(), n, img + 8);
    *w = words * ((flags & 3) == 0 ? 4 : (flags & 3) == 1 ? 2 : 1);
    *h = readU16(d.data(), n, img + 10);
    return *w > 0 && *h > 0;
}

// The pack picture for a `w` x `h` texture, as the game wants it: as it is
// when it has the texture's shape, else the top-left corner of the 256x256
// page Classic REbirth hands smaller textures over in.
bool texturePicture(const std::string& ffmpeg, const std::string& pic, int w, int h,
                    const std::string& tmp, std::vector<uint8_t>* out, std::string* why) {
    std::vector<uint8_t> file, rgba;
    int pw = 0, ph = 0;
    if (!readFile(pic, &file) || !decodeWebp(file, &rgba, &pw, &ph)) {
        *why = pic + " does not decode";
        return false;
    }
    if ((int64_t)pw * h == (int64_t)ph * w) {
        *out = file;
        return true;
    }
    if (pw != ph || w > 256 || h > 256) {
        *why = "the pack picture has another shape";
        return false;
    }
    return composeSkin(ffmpeg, pic, std::string(), w, h, tmp, out, why);
}

// The pack picture for a texture it stretches over a whole square page: the
// file as it is, once it is known to decode.
bool wholePicture(const std::string& pic, std::vector<uint8_t>* out, std::string* why) {
    std::vector<uint8_t> file, rgba;
    int pw = 0, ph = 0;
    if (!readFile(pic, &file) || !decodeWebp(file, &rgba, &pw, &ph)) {
        *why = pic + " does not decode";
        return false;
    }
    out->swap(file);
    return true;
}

// A USA file reader page that shows a picture (the pass number, Barry's
// photo, the three pass codes) has no page in any pack: Classic REbirth runs
// the Japanese game, which draws the same picture as a 256x120 backdrop
// (Item_m2/FILEI*) and the text on a page of its own. The USA page holds that
// backdrop's pixels at (24, 24), scale 1 (all five, within 2-9 levels a
// channel), so the HD page is built from it: the page scaled up to the pack's
// scale, with the pack's backdrop over the texels that show the picture. Only
// those texels: the backdrop's own frame scraps (top right) are not on the
// USA page. Whatever of the picture lies outside the backdrop (the bottom of
// Barry's photo) stays the scaled-up page.
const int kBackdropX = 24;
const int kBackdropY = 24;
const int kBackdropWidth = 256;
const int kBackdropHeight = 120;

bool filePageRgba(const std::vector<uint8_t>& pix, int page, std::vector<uint8_t>* rgba,
                  int* w, int* h, std::string* why) {
    const size_t n = pix.size();
    const uint32_t off = page >= 0 && page < 16 ? readU32(pix.data(), n, (size_t)page * 4) : 0;
    if (n < 64 || off < 64 || off >= n) {
        *why = "no such page";
        return false;
    }
    std::vector<uint8_t> tim;
    std::string err;
    if (!lzwUnpack(pix.data() + off, n - off, &tim, &err)) {
        *why = "LZW: " + err;
        return false;
    }
    return timRgba(tim, rgba, w, h, why);
}

// Lossless WebP of a W x H RGBA8 image through ffmpeg, as the rest of the import.
bool encodeRgbaWebp(const std::string& ffmpeg, const std::vector<uint8_t>& rgba, int W, int H,
                    const std::string& tmp, std::vector<uint8_t>* out, std::string* why) {
    const std::string raw = tmp + ".rgba";
    if (!writeFile(raw, rgba)) {
        *why = "cannot write " + raw;
        return false;
    }
    const std::vector<std::string> args = {
        "-hide_banner", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgba",
        "-s", std::to_string(W) + "x" + std::to_string(H), "-i", raw, "-c:v", "libwebp",
        "-lossless", "1", "-compression_level", "4", tmp};
    int code = -1;
    std::string so, se, err;
    const bool ran = ChildProcess::run(ffmpeg, args, false, &code, &so, &se, &err);
    std::error_code ec;
    std::filesystem::remove(raw, ec);
    if (!ran || code != 0 || !readFile(tmp, out)) {
        *why = "ffmpeg could not build it (it needs libwebp)" + (se.empty() ? "" : ": " + se);
        return false;
    }
    std::filesystem::remove(tmp, ec);
    return true;
}

bool composeBackdropPage(const std::string& ffmpeg, const std::vector<uint8_t>& pageRgba,
                         int pw, int ph, const std::string& pic, const std::string& tmp,
                         std::vector<uint8_t>* out, std::string* why) {
    std::vector<uint8_t> file, hd;
    int hw = 0, hh = 0;
    if (!readFile(pic, &file) || !decodeWebp(file, &hd, &hw, &hh)) {
        *why = pic + " does not decode";
        return false;
    }
    if (hw != hh || hw < 256 || pw != 256 || ph != 256) {
        *why = "the pack picture is not a square page";
        return false;
    }
    const double s = hw / 256.0;           // pack pixels per texel
    const int W = hw, H = (int)std::lround(ph * s);

    // Which texels show the picture: opaque, not black, inside the backdrop;
    // grown by two texels so the pack's edge pixels come along.
    std::vector<float> mask((size_t)pw * ph, 0.0f);
    for (int y = kBackdropY; y < std::min(ph, kBackdropY + kBackdropHeight); ++y)
        for (int x = kBackdropX; x < std::min(pw, kBackdropX + kBackdropWidth); ++x) {
            const uint8_t* p = &pageRgba[((size_t)y * pw + x) * 4];
            if (p[3] && std::max(p[0], std::max(p[1], p[2])) > 24) mask[(size_t)y * pw + x] = 1.0f;
        }
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<float> grown = mask;
        for (int y = kBackdropY; y < std::min(ph, kBackdropY + kBackdropHeight); ++y)
            for (int x = kBackdropX; x < std::min(pw, kBackdropX + kBackdropWidth); ++x)
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int sx = x + dx, sy = y + dy;
                        if (sx >= 0 && sy >= 0 && sx < pw && sy < ph && mask[(size_t)sy * pw + sx] > 0)
                            grown[(size_t)y * pw + x] = 1.0f;
                    }
        mask.swap(grown);
    }

    // The pack repainted the picture (Barry's photo is warmer and brighter):
    // the part of it the backdrop does not reach is brought to the pack's
    // colours, a gain and offset per channel fitted on the texels both show
    // (each texel against the mean of its pack block). And the backdrop fades
    // out over its last rows instead of ending on a hard line.
    const int S = std::max(1, (int)std::lround(s));
    double gain[3] = {1, 1, 1}, bias[3] = {0, 0, 0};
    {
        double n = 0, sx[3] = {0}, sy[3] = {0}, sxx[3] = {0}, sxy[3] = {0};
        for (int y = kBackdropY; y < std::min(ph, kBackdropY + kBackdropHeight); ++y)
            for (int x = kBackdropX; x < std::min(pw, kBackdropX + kBackdropWidth); ++x) {
                const uint8_t* p = &pageRgba[((size_t)y * pw + x) * 4];
                if (!p[3] || std::max(p[0], std::max(p[1], p[2])) <= 24) continue;
                double q[3] = {0, 0, 0};
                const int qx = (x - kBackdropX) * S, qy = (y - kBackdropY) * S;
                if (qx + S > hw || qy + S > hh) continue;
                for (int by = 0; by < S; ++by)
                    for (int bx = 0; bx < S; ++bx)
                        for (int k = 0; k < 3; ++k)
                            q[k] += hd[((size_t)(qy + by) * hw + qx + bx) * 4 + k];
                ++n;
                for (int k = 0; k < 3; ++k) {
                    q[k] /= S * S;
                    sx[k] += p[k]; sy[k] += q[k]; sxx[k] += p[k] * p[k]; sxy[k] += p[k] * q[k];
                }
            }
        for (int k = 0; k < 3 && n > 100; ++k) {
            const double var = sxx[k] - sx[k] * sx[k] / n;
            if (var > 1e-6) {
                gain[k] = (sxy[k] - sx[k] * sy[k] / n) / var;
                bias[k] = (sy[k] - gain[k] * sx[k]) / n;
            }
        }
    }
    // Texels of the picture the backdrop does not cover get the fitted colours.
    std::vector<uint8_t> page = pageRgba;
    const int fade = 6;   // texels over which the backdrop fades out at its bottom
    for (int y = 0; y < ph; ++y)
        for (int x = 0; x < pw; ++x) {
            uint8_t* p = &page[((size_t)y * pw + x) * 4];
            const bool inside = x >= kBackdropX && x < kBackdropX + kBackdropWidth &&
                                y >= kBackdropY && y < kBackdropY + kBackdropHeight;
            if (inside || !p[3] || std::max(p[0], std::max(p[1], p[2])) <= 24) continue;
            // Only below the backdrop and next to the picture it covers.
            if (y < kBackdropY + kBackdropHeight || x < kBackdropX) continue;
            bool near = false;
            for (int yy = y - 1; yy >= kBackdropY + kBackdropHeight - 1 && !near; --yy) {
                const uint8_t* a = &pageRgba[((size_t)yy * pw + x) * 4];
                if (!a[3] || std::max(a[0], std::max(a[1], a[2])) <= 24) break;
                near = yy == kBackdropY + kBackdropHeight - 1;
            }
            if (!near) continue;
            for (int k = 0; k < 3; ++k)
                p[k] = (uint8_t)std::max(0.0, std::min(255.0, gain[k] * p[k] + bias[k] + 0.5));
        }
    for (int y = kBackdropY + kBackdropHeight - fade; y < kBackdropY + kBackdropHeight; ++y) {
        if (y < 0 || y >= ph) continue;
        // Only where the picture goes on below the backdrop.
        const float w = (float)(kBackdropY + kBackdropHeight - y) / (fade + 1);
        for (int x = kBackdropX; x < std::min(pw, kBackdropX + kBackdropWidth); ++x) {
            const int below = kBackdropY + kBackdropHeight;
            if (below >= ph) continue;
            const uint8_t* a = &page[((size_t)below * pw + x) * 4];
            if (a[3] && std::max(a[0], std::max(a[1], a[2])) > 24)
                mask[(size_t)y * pw + x] *= w;
        }
    }

    // Bilinear sample of the page (each channel, and the mask) at texel (u, v).
    auto sample = [&](double u, double v, float* c, float* m) {
        u = std::max(0.0, std::min(pw - 1.0, u));
        v = std::max(0.0, std::min(ph - 1.0, v));
        const int x0 = (int)u, y0 = (int)v;
        const int x1 = std::min(pw - 1, x0 + 1), y1 = std::min(ph - 1, y0 + 1);
        const double fx = u - x0, fy = v - y0;
        const double wt[4] = {(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy};
        const int xs[4] = {x0, x1, x0, x1}, ys[4] = {y0, y0, y1, y1};
        for (int k = 0; k < 4; ++k) c[k] = 0.0f;
        *m = 0.0f;
        for (int i = 0; i < 4; ++i) {
            const size_t t = (size_t)ys[i] * pw + xs[i];
            for (int k = 0; k < 4; ++k) c[k] += (float)(wt[i] * page[t * 4 + k]);
            *m += (float)(wt[i] * mask[t]);
        }
    };

    std::vector<uint8_t> rgba((size_t)W * H * 4);
    const int ox = (int)std::lround(kBackdropX * s), oy = (int)std::lround(kBackdropY * s);
    const int bh = (int)std::lround(kBackdropHeight * s);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            float c[4], m;
            sample((x + 0.5) / s - 0.5, (y + 0.5) / s - 0.5, c, &m);
            const int px = x - ox, py = y - oy;
            if (m > 0.0f && px >= 0 && py >= 0 && px < hw && py < bh) {
                const uint8_t* q = &hd[((size_t)py * hw + px) * 4];
                const float mq = m * q[3] / 255.0f;   // nothing from a pack texel it keys out
                for (int k = 0; k < 3; ++k) c[k] = c[k] * (1 - mq) + q[k] * mq;
            }
            // The page's own transparency, scaled up: the pack's picture
            // keeps stray key colours (magenta, blue) in the pass codes'
            // punched holes, which the page has transparent (0x0000). Colour
            // under transparency goes to black - the reader's background -
            // so filtering cannot bleed it in.
            for (int k = 0; k < 3; ++k) c[k] *= c[3] / 255.0f;
            for (int k = 0; k < 4; ++k)
                rgba[((size_t)y * W + x) * 4 + k] =
                    (uint8_t)std::max(0.0f, std::min(255.0f, c[k] + 0.5f));
        }

    return encodeRgbaWebp(ffmpeg, rgba, W, H, tmp, out, why);
}

// The pack picture for a texture the game keys (texels of colour 0x0000
// transparent), given the cut-out it lacks. Classic REbirth's pictures are
// opaque - its renderer keys their black itself - while this game takes a
// texture's holes from the picture's alpha, so an opaque picture fills them:
// the file book page's black margin (Item_m2/FILE000/001) covered the book
// change arrows drawn under it. The game's transparency, scaled up
// (bilinear), goes on a picture with none of its own; colour under it goes to
// black so filtering cannot bleed it in. `file` is left as it is when the
// picture has alpha already or the texture keys nothing.
bool keyLikeTexture(const std::string& ffmpeg, const std::vector<uint8_t>& tex, int tw, int th,
                    const std::string& tmp, std::vector<uint8_t>* file, std::string* why) {
    if (tw <= 0 || th <= 0 || tex.size() < (size_t)tw * th * 4) return true;
    bool keyed = false;
    for (size_t i = 0; i < (size_t)tw * th && !keyed; ++i) keyed = tex[i * 4 + 3] == 0;
    if (!keyed) return true;
    std::vector<uint8_t> rgba;
    int W = 0, H = 0;
    if (!decodeWebp(*file, &rgba, &W, &H) || W <= 0 || H <= 0) return true;
    for (size_t i = 0; i < (size_t)W * H; ++i)
        if (rgba[i * 4 + 3] != 0xFF) return true;   // the picture brings its own
    const double sx = (double)tw / W, sy = (double)th / H;
    for (int y = 0; y < H; ++y) {
        const double v = std::max(0.0, std::min(th - 1.0, (y + 0.5) * sy - 0.5));
        const int y0 = (int)v, y1 = std::min(th - 1, y0 + 1);
        const double fy = v - y0;
        for (int x = 0; x < W; ++x) {
            const double u = std::max(0.0, std::min(tw - 1.0, (x + 0.5) * sx - 0.5));
            const int x0 = (int)u, x1 = std::min(tw - 1, x0 + 1);
            const double fx = u - x0;
            auto a = [&](int xx, int yy) { return (double)tex[((size_t)yy * tw + xx) * 4 + 3]; };
            const double al = a(x0, y0) * (1 - fx) * (1 - fy) + a(x1, y0) * fx * (1 - fy) +
                              a(x0, y1) * (1 - fx) * fy + a(x1, y1) * fx * fy;
            uint8_t* p = &rgba[((size_t)y * W + x) * 4];
            for (int k = 0; k < 3; ++k) p[k] = (uint8_t)std::lround(p[k] * al / 255.0);
            p[3] = (uint8_t)std::lround(al);
        }
    }
    std::vector<uint8_t> keyedFile;
    if (!encodeRgbaWebp(ffmpeg, rgba, W, H, tmp, &keyedFile, why)) return false;
    file->swap(keyedFile);
    return true;
}

// Installs `file` as HD/<name>.webp - where the game looks a texture up by its
// hash - and retires a copy of the same texture in a pack-named folder
// (HD/<kind>/), where an earlier version of this importer grouped them. The
// game takes one file per name, so a stale copy there must not survive.
bool install(const std::string& hd, const std::string& kind, const std::string& name,
             const std::vector<uint8_t>& file, const std::vector<std::string>& kinds,
             std::string* error) {
    (void)kind;
    const std::string out = joinPath(hd, name + ".webp");
    if (!makeDirs(hd) || !writeFile(out, file)) {
        if (error) *error = "cannot write " + out;
        return false;
    }
    std::error_code ec;
    for (const auto& k : kinds) std::filesystem::remove(joinPath(joinPath(hd, k), name + ".webp"), ec);
    return true;
}

// The textures of the models a room file carries, as the game uploads them
// and names them (src/game/RoomBackgroundWebp.h room_hd_model_key): the RDT
// header's object model table (+0x50) and item model table (+0x54) each hold
// {TMD, TIM} file offsets, counted by header bytes 2 and 3. Every palette row
// of a TIM is uploaded as the whole image in that row - index 0 transparent,
// a 0x0000 entry elsewhere drawn as 0x0001 and 0x8000 as 0x8001, which is how
// the game hands them over (checked texel for texel against its dump) - and
// named after the TIM itself, <TREE>/RoomModels/<crc> (hdtex::RoomTimName),
// with _P<row> when there is more than one row: the same TIM is copied into
// many rooms, and they all share the one file. `treeName` is "USA" / "JPN".
//
// Each texture is also returned in `rendered` (by hash) as if the game had
// dumped it, so it is paired with the pack without the room being played.
void roomModelTextures(const std::vector<uint8_t>& rdt, const std::string& treeName,
                       std::map<std::string, std::vector<std::string>>* byHash,
                       std::map<std::string, DumpTexture>* rendered) {
    auto u32 = [&](size_t o) -> uint32_t {
        return (o + 4 <= rdt.size()) ? (uint32_t)rdt[o] | (uint32_t)rdt[o + 1] << 8 |
                                           (uint32_t)rdt[o + 2] << 16 | (uint32_t)rdt[o + 3] << 24
                                     : 0;
    };
    auto u16 = [&](size_t o) -> uint32_t {
        return (o + 2 <= rdt.size()) ? (uint32_t)rdt[o] | (uint32_t)rdt[o + 1] << 8 : 0;
    };
    if (rdt.size() < 0x94) return;                       // the stub rooms are 4 bytes
    const struct { const char* kind; uint32_t count, table; } tables[2] = {
        {"OBJ", rdt[2], u32(0x50)}, {"ITEM", rdt[3], u32(0x54)}};
    for (const auto& t : tables) {
        for (uint32_t slot = 0; slot < t.count; ++slot) {
            const uint32_t tim = u32(t.table + slot * 8 + 4);
            if (tim == 0 || u32(tim) != 0x10) continue;
            const uint32_t flags = u32(tim + 4);
            const int bpp = (flags & 3) == 0 ? 4 : (flags & 3) == 1 ? 8 : 0;
            if (bpp == 0 || !(flags & 8)) continue;
            const uint32_t clutW = u16(tim + 16), clutH = u16(tim + 18);
            const size_t clut = tim + 20;
            const size_t img = tim + 8 + u32(tim + 8);
            const uint32_t imgW = u16(img + 8), imgH = u16(img + 10);
            const int w = (int)imgW * (bpp == 4 ? 4 : 2), h = (int)imgH;
            const size_t pix = img + 12;
            if (clutW == 0 || clutH == 0 || clutH > 8 || w == 0 || h == 0 ||
                clut + clutW * clutH * 2 > rdt.size() || pix + (size_t)imgW * 2 * h > rdt.size())
                continue;
            char timName[9];
            if (!hdtex::RoomTimName(rdt.data() + tim, (unsigned)(rdt.size() - tim), timName)) continue;
            const std::string key = treeName + "/RoomModels/" + timName;
            std::vector<uint8_t> rgba((size_t)w * h * 4);
            for (uint32_t row = 0; row < clutH; ++row) {
              const std::string k = clutH > 1 ? key + "_P" + std::to_string(row) : key;
              // The palette fix-ups (0x8000 -> 0x8001, 0x0000 -> 0x0001) are
              // not applied to every upload (the ROOM5130 gun parts keep
              // their black, the ROOM1051 objects do not), so each of the
              // four readings is hashed and linked to the name; the first,
              // with both, is the picture the pack is compared with.
              for (int variant = 0; variant < 4; ++variant) {
                const bool fixStp = (variant & 1) == 0, fixBlack = (variant & 2) == 0;
                bool differs = false;
                for (int y = 0; y < h; ++y) {
                    for (int x = 0; x < w; ++x) {
                        const uint8_t b = rdt[pix + (size_t)y * imgW * 2 + (bpp == 4 ? x / 2 : x)];
                        const uint32_t idx = bpp == 8 ? b : (x & 1) ? (b >> 4) : (b & 15);
                        uint32_t c = idx < clutW ? u16(clut + (row * clutW + idx) * 2) : 0;
                        if (c == 0x8000) { differs = differs || !fixStp; if (fixStp) c = 0x8001; }
                        if (c == 0 && idx != 0) { differs = differs || !fixBlack; if (fixBlack) c = 1; }
                        uint8_t* o = &rgba[((size_t)y * w + x) * 4];
                        o[0] = (uint8_t)((c & 31) * 255 / 31);
                        o[1] = (uint8_t)(((c >> 5) & 31) * 255 / 31);
                        o[2] = (uint8_t)(((c >> 10) & 31) * 255 / 31);
                        o[3] = idx == 0 ? 0 : 255;
                    }
                }
                if (variant != 0 && !differs) continue;   // the same picture again
                char name[17];
                hdtex::HashName(w, h, rgba.data(), name);
                if (variant == 0 && !rendered->count(name)) {
                    DumpTexture& d = (*rendered)[name];
                    d.name = name;
                    d.w = w;
                    d.h = h;
                    d.thumb.resize(kThumbBytes);
                    makeThumb(rgba.data(), w, w, h, d.thumb.data());
                    d.crName = classicRebirthName(rgba);
                }
                std::vector<std::string>& keys = (*byHash)[name];
                if (std::find(keys.begin(), keys.end(), k) == keys.end()) keys.push_back(k);
              }
            }
        }
    }
}

// Installs a texture matched against the dump. When the game also dumped those
// pixels under a file name, it goes there - HD/<TREE>/<path>.webp, the name
// the game looks it up by first - unless this import already gave that name a
// picture from its table (`named`); the hash name is then retired. Otherwise
// it goes in by hash (install).
bool installDump(const std::string& hd, const std::string& kind, const DumpTexture& t,
                 const std::vector<uint8_t>& file, const std::vector<std::string>& kinds,
                 std::set<std::string>& named, std::string* error) {
    bool any = false;
    // One texture can stand in many rooms (an ink ribbon lies in over a
    // hundred item slots): the first name is written, the others are hard
    // links to it where the file system has them, copies where it does not.
    std::string first;
    for (const auto& key : t.namedKeys) {
        const std::string out = joinPath(hd, key + ".webp");
        any = true;
        if (named.count(pathKey(out))) continue;
        const std::string dir = out.substr(0, out.find_last_of("/\\"));
        if (!makeDirs(dir)) {
            if (error) *error = "cannot write " + out;
            return false;
        }
        // Removed first: writing into a link an earlier import made would
        // change every name linked to it.
        std::error_code ec;
        std::filesystem::remove(out, ec);
        bool linked = false;
        if (!first.empty()) {
            std::filesystem::create_hard_link(first, out, ec);
            linked = !ec;
        }
        if (!linked && !writeFile(out, file)) {
            if (error) *error = "cannot write " + out;
            return false;
        }
        if (first.empty()) first = out;
        named.insert(pathKey(out));
    }
    if (!any) return install(hd, kind, t.name, file, kinds, error);
    std::error_code ec;
    std::filesystem::remove(joinPath(hd, t.name + ".webp"), ec);
    for (const auto& k : kinds) std::filesystem::remove(joinPath(joinPath(hd, k), t.name + ".webp"), ec);
    return true;
}

// An earlier version of this importer named a room's model textures after the
// room file and slot (HD/<TREE>/Stage1/ROOM1091_ITEM0.webp, ...), one copy per
// room; they are named by the TIM now (HD/<TREE>/RoomModels/). Removes those.
void removeRoomSlotNames(const std::string& hdTree) {
    std::error_code ec;
    if (!isDirectory(hdTree)) return;
    for (const auto& stage : listDirectory(hdTree)) {
        const std::string dir = joinPath(hdTree, stage);
        if (toLower(stage).rfind("stage", 0) != 0 || !isDirectory(dir)) continue;
        for (const auto& f : listDirectory(dir)) {
            const std::string l = toLower(f);
            if (l.rfind("room", 0) == 0 && extensionOf(l) == ".webp" &&
                (l.find("_obj") != std::string::npos || l.find("_item") != std::string::npos))
                std::filesystem::remove(joinPath(dir, f), ec);
        }
    }
}

// An earlier version of this importer grouped what it matched by hash into
// the pack's folders (HD/bgd, HD/misc, ...). Moves those files up into HD/,
// where they go now, so this import replaces or retires them like any other;
// a name HD/ already holds keeps that copy. A folder left empty is removed.
void flattenKindFolders(const std::string& hd) {
    static const char* const kKinds[] = {"bgd", "mask", "misc", "skin", "skin2"};
    std::error_code ec;
    for (const char* k : kKinds) {
        const std::string dir = joinPath(hd, k);
        if (!isDirectory(dir)) continue;
        for (const auto& f : listDirectory(dir)) {
            if (f.size() < 5 || f.compare(f.size() - 5, 5, ".webp") != 0) continue;
            const std::string from = joinPath(dir, f), to = joinPath(hd, f);
            if (isRegularFile(to)) std::filesystem::remove(from, ec);
            else std::filesystem::rename(from, to, ec);
        }
        if (std::filesystem::is_empty(dir, ec)) std::filesystem::remove(dir, ec);
    }
}

}  // namespace

bool importHdPack(const HdPackOptions& opts, const Progress& progress,
                  std::string* error) {
    progress.info("HD pack import");
    if (opts.packDir.empty() || !isDirectory(opts.packDir)) {
        if (error) *error = "no HD pack folder selected";
        return false;
    }
    if (opts.targetRoot.empty()) {
        if (error) *error = "no target game folder selected";
        return false;
    }
    // The selected folder is a pack's hires folder: it holds the texture-type
    // folders, any of these. What goes in by hash is written to HD/ itself,
    // the folder the game looks a hash up in; the grouping is not kept.
    static const char* const kPackKinds[] = {"bgd", "mask", "misc", "skin", "skin2"};
    const std::string hires = opts.packDir;
    std::vector<std::string> kinds;
    std::map<std::string, std::map<std::string, std::string>> pack;   // kind -> stem -> path
    size_t packTotal = 0;
    for (const char* k : kPackKinds) {
        const std::string d = childCI(hires, k, true);
        if (d.empty()) continue;
        const std::string dir = joinPath(hires, d);
        for (const auto& f : listDirectory(dir)) {
            if (extensionOf(f) != ".webp") continue;
            pack[k][toUpper(f.substr(0, f.size() - 5))] = joinPath(dir, f);
            ++packTotal;
        }
        if (pack.count(k)) kinds.push_back(k);
    }
    if (packTotal == 0) {
        if (error)
            *error = hires + " is not an HD pack's hires folder: it should hold one or "
                     "more of the folders bgd, mask, misc, skin and skin2, with .webp "
                     "files in them";
        return false;
    }

    const bool dcPackGame = opts.game == HdPackGame::DirectorsCut;
    // Original: names come from the base tree. Director's Cut: from the DC
    // overlay the Director's Cut tab builds.
    const std::string tree = joinPath(opts.targetRoot, versionName(opts.tree));
    const std::string dcTree = joinPath(opts.targetRoot, "DC");
    if (!isDirectory(dcPackGame ? dcTree : tree)) {
        if (error)
            *error = dcPackGame
                ? dcTree + " does not exist; migrate the Director's Cut first - the "
                           "names come from its own files"
                : tree + " does not exist; migrate the PC assets first - the names "
                         "are computed from the game's own files";
        return false;
    }
    const std::string hd = joinPath(opts.targetRoot, "HD");
    if (!makeDirs(hd)) {
        if (error) *error = "cannot create " + hd;
        return false;
    }
    progress.info(std::string("pack (") + (dcPackGame ? "Director's Cut" : "original game") +
                  "): " + hires);
    progress.info("names from: " + (dcPackGame ? dcTree : tree));
    progress.info("target: " + hd);
    // Not from the pack: the HD fonts (Archivo Black, Archivo Narrow), which
    // the game draws its HD text in (core/HdFonts.h).
    if (!dcPackGame && !writeHdFonts(opts.targetRoot, opts.tree, progress, error))
        return false;
    flattenKindFolders(hd);
    if (!dcPackGame) removeRoomSlotNames(joinPath(hd, versionName(opts.tree)));

    std::map<std::string, size_t> done;        // kind -> files installed
    std::set<std::string> usedNames;           // texture names already given a file
    std::set<std::string> usedPack;            // pack paths already installed
    std::set<std::string> namedWritten;        // pathKey of every named file written

    // ---- 1. the table: backgrounds and masks, from the tree's own files ----
    const int total = (int)(sizeof(kTable) / sizeof(kTable[0]));
    size_t noOriginal = 0;
    for (int i = 0; i < total; ++i) {
        const HdPackEntry& e = kTable[i];
        if (progress.isCancelled()) {
            if (error) *error = "cancelled";
            return false;
        }
        if (dcPackGame || (e.kind != HdPackKind::Background && e.kind != HdPackKind::Mask)) continue;
        const bool bg = e.kind == HdPackKind::Background;
        const std::string kind = bg ? "bgd" : "mask";
        const auto folderIt = pack.find(kind);
        if (folderIt == pack.end()) continue;
        const auto it = folderIt->second.find(e.packName);
        if (it == folderIt->second.end()) continue;   // this pack does not carry it
        if ((i & 15) == 0) progress.phase(i, total, "backgrounds and masks");

        // "Stage2/RC2016" -> folder + file, both resolved without regard to case.
        const std::string asset = e.asset;
        const size_t slash = asset.find('/');
        const std::string folder = childCI(tree, asset.substr(0, slash), true);
        const std::string stem = asset.substr(slash + 1);
        const std::string dir = folder.empty() ? std::string() : joinPath(tree, folder);
        // The file as the tree spells it: a background may be the .pak or the
        // .webp the migrator made of it.
        const std::string source = folder.empty() ? std::string()
                                 : bg ? findBackgroundFile(dir, stem + ".pak")
                                      : childCI(dir, stem + ".pak", false);
        if (source.empty()) {
            progress.info("  " + std::string(e.packName) + " (" + asset + "): " +
                          (folder.empty() ? "no " + asset.substr(0, slash) + " folder"
                                          : std::string("not in the tree")));
            ++noOriginal;
            continue;
        }

        // Only what the game can read goes in.
        std::vector<uint8_t> file, probe;
        int pw = 0, ph = 0;
        if (!readFile(it->second, &file) || !decodeWebp(file, &probe, &pw, &ph)) {
            progress.info("  " + std::string(e.packName) + ".webp does not decode - skipped");
            ++noOriginal;
            continue;
        }

        // The texture as the game builds it: its size, and the hash that
        // retires the copy an earlier import wrote under it and keeps the
        // dump matching below off it.
        std::vector<uint8_t> rgba;
        int w = kBackgroundWidth, h = kBackgroundHeight;
        std::string why;
        const bool haveRgba = bg ? backgroundRgba(dir, stem + ".pak", &rgba, &why)
                                 : maskRgba(joinPath(dir, source), &rgba, &w, &h, &why);

        // A pack mask is a whole 256x256 VRAM page (Classic REbirth hashes the
        // page); a smaller Objspr page, 128x128, is its top-left part, so that
        // part is cut out (losslessly: it is a cut-out).
        if (!bg && haveRgba && (w < 256 || h < 256)) {
            std::vector<uint8_t> part;
            if (!cropWebp(opts.ffmpegPath, it->second, pw * w / 256, ph * h / 256,
                          joinPath(hd, "re1am_mask_tmp.webp"), &part, true)) {
                progress.info("  " + std::string(e.packName) + " (" + asset +
                              "): cannot cut the mask out of the page - is ffmpeg "
                              "there, with libwebp?");
                ++noOriginal;
                continue;
            }
            file.swap(part);
        }

        // The game finds it by the file's own path in the tree it reads it
        // from: HD/<USA|JPN>/Stage2/RC2016.webp, HD/<USA|JPN>/Objspr/OSP01104.webp
        // (src/game/RoomBackgroundWebp.h). That does not depend on the pixels
        // the tree's files decode to, so it survives a change of background
        // format.
        const std::string outDir = joinPath(joinPath(hd, versionName(opts.tree)), folder);
        const std::string outFile = joinPath(outDir, source.substr(0, source.rfind('.')) + ".webp");
        if (!makeDirs(outDir) || !writeFile(outFile, file)) {
            if (error) *error = "cannot write " + outFile;
            return false;
        }
        namedWritten.insert(pathKey(outFile));

        if (haveRgba) {
            char name[17];
            hdtex::HashName(w, h, rgba.data(), name);
            usedNames.insert(name);
            std::error_code ec;
            std::filesystem::remove(joinPath(hd, std::string(name) + ".webp"), ec);
            for (const auto& k : kinds)
                std::filesystem::remove(joinPath(joinPath(hd, k), std::string(name) + ".webp"), ec);
        }
        usedPack.insert(it->second);
        ++done[kind];
    }
    if (!dcPackGame) {
        progress.phase(total, total, "backgrounds and masks");
        progress.info("  from the match table: " + std::to_string(done["bgd"]) +
                      " background(s), " + std::to_string(done["mask"]) + " mask(s) -> " +
                      joinPath(hd, versionName(opts.tree)));
        if (noOriginal)
            progress.info("  " + std::to_string(noOriginal) + " skipped (listed above)");
    }

    // ---- 1b. model skins: one picture per .EMD, named after the file ----
    // The game finds these by path (HD/<TREE>/ENEMY/EM1010.webp, docs/
    // HD_TEXTURES.md "Model skins by file"), so no hash and no dump is needed.
    struct ModelSources {
        std::string pal[2];
        bool whole = false;    // ModelWhole: pal[0] goes in as it is
    };
    std::map<std::string, ModelSources> models;      // asset -> pack picture per row
    for (int i = 0; i < total; ++i) {
        const HdPackEntry& e = kTable[i];
        if (dcPackGame || (e.kind != HdPackKind::ModelPalette0 && e.kind != HdPackKind::ModelPalette1 &&
                           e.kind != HdPackKind::ModelWhole))
            continue;
        for (const auto& k : kinds) {
            const auto it = pack[k].find(e.packName);
            if (it == pack[k].end()) continue;
            models[e.asset].pal[e.kind == HdPackKind::ModelPalette1 ? 1 : 0] = it->second;
            if (e.kind == HdPackKind::ModelWhole) models[e.asset].whole = true;
            break;
        }
    }
    size_t modelsDone = 0, modelsHalf = 0, modelsFailed = 0;
    std::map<std::string, std::vector<uint8_t>> built;   // same pictures -> same file
    const std::string treeName = versionName(opts.tree);
    const std::string tmpModel = joinPath(hd, "re1am_skin_tmp.webp");
    int m = 0;
    for (const auto& entry : models) {
        if (progress.isCancelled()) {
            if (error) *error = "cancelled";
            return false;
        }
        progress.phase(m++, (int)models.size(), "model skins");
        const std::string& asset = entry.first;
        const ModelSources& src = entry.second;
        const size_t slash = asset.find('/');
        const std::string folder = childCI(tree, asset.substr(0, slash), true);
        const std::string emd = folder.empty() ? std::string()
                                : childCI(joinPath(tree, folder), asset.substr(slash + 1) + ".emd", false);
        std::vector<uint8_t> data;
        int w = 0, h = 0, rows = 0;
        if (emd.empty() || !readFile(joinPath(joinPath(tree, folder), emd), &data) ||
            !emdTextureShape(data, &w, &h, &rows)) {
            progress.info("  " + asset + ": not in the tree");
            ++modelsFailed;
            continue;
        }
        const bool two = rows >= 2;
        if (src.pal[0].empty() || (two && src.pal[1].empty())) {
            // Half a model would be drawn with the other half's palette.
            ++modelsHalf;
            continue;
        }
        const std::string key = src.pal[0] + "|" + (two ? src.pal[1] : "") + "|" +
                                std::to_string(w) + "x" + std::to_string(h);
        auto b = built.find(key);
        if (b == built.end()) {
            std::vector<uint8_t> file;
            std::string why;
            if (src.whole && !two) {
                if (!readFile(src.pal[0], &file)) {
                    progress.info("  " + asset + ": cannot read " + src.pal[0]);
                    ++modelsFailed;
                    continue;
                }
            } else if (!composeSkin(opts.ffmpegPath, src.pal[0], two ? src.pal[1] : std::string(), w, h,
                             tmpModel, &file, &why)) {
                progress.info("  " + asset + ": " + why);
                ++modelsFailed;
                continue;
            }
            b = built.emplace(key, std::move(file)).first;
        }
        // HD/<TREE>/<folder>/<file>.webp with the tree's own spelling of both.
        const std::string dir = joinPath(joinPath(hd, treeName), folder);
        const std::string out = joinPath(dir, emd.substr(0, emd.size() - 4) + ".webp");
        if (!makeDirs(dir) || !writeFile(out, b->second)) {
            if (error) *error = "cannot write " + out;
            return false;
        }
        namedWritten.insert(pathKey(out));
        // The same skin under the Director's Cut's names for the model, each
        // its own file (HD/DC/...), so the two can be retouched apart.
        for (const auto& copy : kDcModelSkinCopies) {
            if (toLower(asset) != toLower(copy.base)) continue;
            const std::string dcDir = joinPath(joinPath(hd, "DC"), "Enemy");
            const std::string dcOut = joinPath(dcDir, std::string(copy.dc) + ".webp");
            if (!makeDirs(dcDir) || !writeFile(dcOut, b->second)) {
                if (error) *error = "cannot write " + dcOut;
                return false;
            }
            namedWritten.insert(pathKey(dcOut));
        }
        usedPack.insert(src.pal[0]);
        if (two) usedPack.insert(src.pal[1]);
        ++modelsDone;
    }
    progress.phase((int)models.size(), (int)models.size(), "model skins");
    if (!models.empty()) {
        progress.info("  model skins: " + std::to_string(modelsDone) + " -> " +
                      joinPath(hd, treeName));
        if (modelsHalf)
            progress.info("  " + std::to_string(modelsHalf) + " model(s) left out: the pack "
                          "has a picture for only one of their two palettes");
        if (modelsFailed)
            progress.info("  " + std::to_string(modelsFailed) + " model(s) failed (listed above)");
    }

    // ---- 1c. Data/ files (original game), or the Director's Cut set ----
    // A Director's Cut pack (the Seamless HD Project's bio1dc/hires) is paired
    // with the DC overlay's own files and goes to HD/DC/.
    size_t dataDone = 0, dcBgDone = 0, dcMaskDone = 0, namedFailed = 0;
    const std::string tmpNamed = joinPath(hd, "re1am_named_tmp.webp");
    for (int i = 0; i < total; ++i) {
        const HdPackEntry& e = kTable[i];
        const bool dc = e.kind == HdPackKind::DcBackground || e.kind == HdPackKind::DcMask;
        const bool whole = e.kind == HdPackKind::FileWhole;
        const bool cell = e.kind == HdPackKind::Cell;
        if (dc != dcPackGame || (!dc && e.kind != HdPackKind::File && !whole && !cell)) continue;
        if (progress.isCancelled()) {
            if (error) *error = "cancelled";
            return false;
        }
        if ((i & 15) == 0) progress.phase(i, total, dc ? "Director's Cut rooms" : "file textures");
        // The pack picture: a DC row names its folder, a Data/ row may be in any.
        std::string pic;
        if (dc) {
            const auto k = pack.find(e.kind == HdPackKind::DcMask ? "mask" : "bgd");
            if (k == pack.end()) continue;
            const auto it = k->second.find(e.packName);
            if (it == k->second.end()) continue;
            pic = it->second;
        } else {
            for (const auto& k : kinds) {
                const auto it = pack[k].find(e.packName);
                if (it != pack[k].end()) { pic = it->second; break; }
            }
            if (pic.empty()) continue;
        }
        const std::string treeDir = dc ? dcTree : tree;
        const std::string asset = e.asset;
        const size_t slash = asset.find('/');
        const std::string folder = childCI(treeDir, asset.substr(0, slash), true);
        std::string stem = asset.substr(slash + 1);
        // A cell ("SLIDE/03") is written into a folder named after its file.
        std::string cellName;
        if (cell) {
            const size_t c = stem.find('/');
            if (c == std::string::npos) continue;
            cellName = stem.substr(c + 1);
            stem = stem.substr(0, c);
        }
        const std::string dir = folder.empty() ? std::string() : joinPath(treeDir, folder);
        std::string outStem, outFolder = folder, why;
        int w = 0, h = 0;
        size_t timOff = (size_t)-1;
        std::vector<uint8_t> texRgba;
        if (!dir.empty() && e.kind == HdPackKind::DcBackground) {
            const std::string file = findBackgroundFile(dir, stem + ".pak");
            if (!file.empty()) {
                outStem = file.substr(0, file.rfind('.'));
                w = kBackgroundWidth;
                h = kBackgroundHeight;
            }
        } else if (!dir.empty() && e.kind == HdPackKind::DcMask) {
            // "ROOM8000_MASK3": the room file and the camera.
            const size_t m = stem.rfind("_MASK");
            const std::string room = stem.substr(0, m);
            const std::string file = childCI(dir, room + ".rdt", false);
            std::vector<uint8_t> rdt;
            const int cam = (int)std::strtol(stem.substr(m + 5).c_str(), nullptr, 16);
            if (!file.empty() && readFile(joinPath(dir, file), &rdt) && rdt.size() > 0x94 &&
                cam < rdt[1]) {
                const size_t tim = readU32(rdt.data(), rdt.size(), 0x94 + (size_t)cam * 0x2C + 4);
                if (timShape(rdt, tim, &w, &h))
                    outStem = file.substr(0, file.rfind('.')) + "_MASK" + stem.substr(m + 5);
            }
        } else if (!dir.empty()) {
            // <folder>/<stem>.tim, .pix, .ivm (an item view: its TIM first) or
            // .dor (a door: its TIM at the offset in header dword 2).
            for (const auto& f : listDirectory(dir)) {
                const std::string ext = extensionOf(f);
                if ((ext != ".tim" && ext != ".pix" && ext != ".ivm" && ext != ".dor") ||
                    toLower(f.substr(0, f.size() - 4)) != toLower(stem))
                    continue;
                std::vector<uint8_t> d;
                if (!readFile(joinPath(dir, f), &d)) break;
                if (cell) {
                    // The cell's shape is the loader's, whatever the file's.
                    if (ext == ".tim" && timShape(d, 0, &w, &h) && w >= kSlideCellWidth &&
                        h >= kSlideCellHeight) {
                        w = kSlideCellWidth;
                        h = kSlideCellHeight;
                        outFolder = joinPath(folder, f.substr(0, f.size() - 4));
                    } else if (ext == ".pix" && isFilePageContainer(d, cellName)) {
                        // A page of the file reader (pickup_unpack_list): a
                        // 256x256 TIM, the whole pack page.
                        w = h = kFilePageSize;
                        outFolder = joinPath(folder, f.substr(0, f.size() - 4));
                    } else {
                        w = 0;
                    }
                    if (w > 0) outStem = cellName;
                    break;
                }
                if (ext == ".pix" && d.size() == (size_t)kBackgroundWidth * kBackgroundHeight * 2) {
                    w = kBackgroundWidth;
                    h = kBackgroundHeight;
                } else if (ext == ".tim" || ext == ".ivm") {
                    timShape(d, 0, &w, &h);
                    timOff = 0;
                } else if (ext == ".dor" && d.size() >= 12) {
                    timOff = readU32(d.data(), d.size(), 8);
                    timShape(d, timOff, &w, &h);
                }
                if (w > 0) outStem = f.substr(0, f.size() - 4);
                // The game's own texels, for the cut-out (keyLikeTexture).
                if (w > 0 && e.kind == HdPackKind::File && timOff < d.size()) {
                    const std::vector<uint8_t> tim(d.begin() + (std::ptrdiff_t)timOff, d.end());
                    int kw = 0, kh = 0;
                    std::string ignored;
                    if (!timRgba(tim, &texRgba, &kw, &kh, &ignored) || kw != w || kh != h)
                        texRgba.clear();
                }
                break;
            }
        }
        if (outStem.empty()) {
            // The table covers both trees: a file this tree does not have
            // (the Japanese FILEI backdrops in a USA tree) is not a failure.
            if (!dc) continue;
            progress.info("  " + std::string(e.packName) + " (DC/" + asset + "): not in the tree");
            ++namedFailed;
            continue;
        }
        std::vector<uint8_t> file;
        if (whole ? !wholePicture(pic, &file, &why)
                  : !texturePicture(opts.ffmpegPath, pic, w, h, tmpNamed, &file, &why)) {
            progress.info("  " + std::string(e.packName) + " (" + asset + "): " + why);
            ++namedFailed;
            continue;
        }
        if (!texRgba.empty() && !keyLikeTexture(opts.ffmpegPath, texRgba, w, h, tmpNamed, &file, &why)) {
            progress.info("  " + std::string(e.packName) + " (" + asset + "): " + why);
            ++namedFailed;
            continue;
        }
        const std::string outDir = joinPath(joinPath(hd, dc ? std::string("DC") : treeName), outFolder);
        const std::string out = joinPath(outDir, outStem + ".webp");
        if (!makeDirs(outDir) || !writeFile(out, file)) {
            if (error) *error = "cannot write " + out;
            return false;
        }
        namedWritten.insert(pathKey(out));
        usedPack.insert(pic);
        if (e.kind == HdPackKind::File || whole || cell) ++dataDone;
        else if (e.kind == HdPackKind::DcBackground) ++dcBgDone;
        else ++dcMaskDone;
    }
    if (dataDone)
        progress.info("  Data/, Item_m1/, Item_m2/ and Effspr/ textures: " +
                      std::to_string(dataDone) + " -> " + joinPath(hd, treeName));
    if (dcBgDone || dcMaskDone)
        progress.info("  Director's Cut: " + std::to_string(dcBgDone) + " background(s), " +
                      std::to_string(dcMaskDone) + " mask(s) -> " + joinPath(hd, "DC"));
    if (namedFailed)
        progress.info("  " + std::to_string(namedFailed) + " skipped (listed above)");

    // ---- 1c'. textures named by a room TIM's CRC or by their hash ----
    // Paired in game (HdPackKind::Named); each pack picture goes in as it is.
    // A hash name is kept away from the dump pass below (usedNames); a tree
    // name from the room-model pass (namedWritten).
    size_t namedDone = 0;
    for (int i = 0; i < total && !dcPackGame; ++i) {
        const HdPackEntry& e = kTable[i];
        if (e.kind != HdPackKind::Named) continue;
        std::string pic;
        for (const auto& k : kinds) {
            const auto it = pack[k].find(e.packName);
            if (it != pack[k].end()) { pic = it->second; break; }
        }
        if (pic.empty()) continue;
        std::vector<uint8_t> file;
        std::string why;
        if (!wholePicture(pic, &file, &why)) {
            progress.info("  " + std::string(e.packName) + " (" + e.asset + "): " + why);
            continue;
        }
        const std::string asset = e.asset;
        if (asset.find('/') == std::string::npos) {
            if (!install(hd, std::string(), toLower(asset), file, kinds, error)) return false;
            usedNames.insert(toLower(asset));
        } else {
            const std::string out = joinPath(joinPath(hd, treeName), asset + ".webp");
            const std::string dir = out.substr(0, out.find_last_of("/\\"));
            if (!makeDirs(dir) || !writeFile(out, file)) {
                if (error) *error = "cannot write " + out;
                return false;
            }
            namedWritten.insert(pathKey(out));
        }
        usedPack.insert(pic);
        ++namedDone;
    }
    if (namedDone)
        progress.info("  textures paired in game: " + std::to_string(namedDone));

    // ---- 1c''. the USA file reader's picture pages, built from a backdrop ----
    size_t backdropDone = 0;
    const std::string tmpPage = joinPath(hd, "re1am_page_tmp.webp");
    for (int i = 0; i < total && !dcPackGame; ++i) {
        const HdPackEntry& e = kTable[i];
        if (e.kind != HdPackKind::Backdrop) continue;
        std::string pic;
        for (const auto& k : kinds) {
            const auto it = pack[k].find(e.packName);
            if (it != pack[k].end()) { pic = it->second; break; }
        }
        if (pic.empty()) continue;
        // "<folder>/<container>/<page>"
        const std::string asset = e.asset;
        const size_t a = asset.find('/'), b = asset.rfind('/');
        if (a == std::string::npos || a == b) continue;
        const std::string folder = childCI(tree, asset.substr(0, a), true);
        const std::string stem = asset.substr(a + 1, b - a - 1);
        const std::string page = asset.substr(b + 1);
        const std::string pix = folder.empty() ? std::string()
                                : childCI(joinPath(tree, folder), stem + ".pix", false);
        std::vector<uint8_t> d, pageRgba, file;
        int pw = 0, ph = 0;
        std::string why;
        if (pix.empty() || !readFile(joinPath(joinPath(tree, folder), pix), &d) ||
            !isFilePageContainer(d, page))
            continue;   // a tree without the USA reader (JPN) is not a failure
        if (!filePageRgba(d, std::atoi(page.c_str()), &pageRgba, &pw, &ph, &why) ||
            !composeBackdropPage(opts.ffmpegPath, pageRgba, pw, ph, pic, tmpPage, &file, &why)) {
            progress.info("  " + std::string(e.packName) + " (" + asset + "): " + why);
            continue;
        }
        const std::string outDir = joinPath(joinPath(joinPath(hd, treeName), folder),
                                            pix.substr(0, pix.size() - 4));
        const std::string out = joinPath(outDir, page + ".webp");
        if (!makeDirs(outDir) || !writeFile(out, file)) {
            if (error) *error = "cannot write " + out;
            return false;
        }
        namedWritten.insert(pathKey(out));
        ++backdropDone;
    }
    if (backdropDone)
        progress.info("  file pages built from a backdrop: " + std::to_string(backdropDone));

    // ---- 1d. the inventory icons, by Classic REbirth's name ----
    // The original game uploads each item's icon on its own: the 40x30 icon
    // from item_all.pix (1200 bytes per item, 8 bpp) in STATUS.TIM's third
    // palette row, in the top-left corner of a 64x64 texture. That is what the
    // pack names (71 of the 72 USA icons by CRC), as a 4x picture with the
    // icon in its top-left 160x120. The port names each icon after the file
    // and the item (src/game/TextureLoader.cpp, HD item icons), so the corner
    // goes in as HD/<TREE>/Data/ITEM_ALL/<NN>.webp.
    // A combine reloads the new item's icon from item_mix.pix instead
    // (menu_item_combine_refresh), named HD/<TREE>/Data/ITEM_MIX/<NN>.webp.
    // Each of its rows is a byte-for-byte copy of an item_all.pix row, so the
    // pack has the same picture under the same name.
    size_t iconsDone = 0;
    for (const char* sheet : {"item_all.pix", "item_mix.pix"}) {
        if (dcPackGame) break;
        const std::string data = childCI(tree, "data", true);
        const std::string pixName = data.empty() ? "" : childCI(joinPath(tree, data), sheet, false);
        const std::string timName = data.empty() ? "" : childCI(joinPath(tree, data), "status.tim", false);
        std::vector<uint8_t> pix, tim;
        if (!pixName.empty() && !timName.empty() &&
            readFile(joinPath(joinPath(tree, data), pixName), &pix) &&
            readFile(joinPath(joinPath(tree, data), timName), &tim) && tim.size() >= 20 + 3 * 512) {
            const uint32_t clutW = tim[16] | tim[17] << 8, clutH = tim[18] | tim[19] << 8;
            const std::string outDir = joinPath(joinPath(joinPath(hd, treeName), data),
                                                pixName.substr(0, pixName.rfind('.')));
            const std::string tmpIcon = joinPath(hd, "re1am_icon_tmp.webp");
            if (clutW * clutH >= 3 * 256) {
                const uint8_t* clut = tim.data() + 20 + 2 * 256 * 2;     // row 2
                for (size_t item = 0; (item + 1) * 1200 <= pix.size(); ++item) {
                    std::vector<uint8_t> rgba(64 * 64 * 4, 0);
                    for (int y = 0; y < 30; ++y) {
                        for (int x = 0; x < 40; ++x) {
                            const uint8_t idx = pix[item * 1200 + y * 40 + x];
                            if (idx == 0) continue;
                            const uint32_t c = clut[idx * 2] | clut[idx * 2 + 1] << 8;
                            uint8_t* o = &rgba[((size_t)y * 64 + x) * 4];
                            o[0] = (uint8_t)((c & 31) * 255 / 31);
                            o[1] = (uint8_t)(((c >> 5) & 31) * 255 / 31);
                            o[2] = (uint8_t)(((c >> 10) & 31) * 255 / 31);
                            o[3] = 255;
                        }
                    }
                    const std::string name = classicRebirthName(rgba);
                    std::string pic;
                    for (const auto& k : kinds) {
                        const auto it = pack[k].find(name);
                        if (it != pack[k].end()) { pic = it->second; break; }
                    }
                    if (pic.empty()) continue;
                    std::vector<uint8_t> file, probe;
                    int pw = 0, ph = 0;
                    if (!readFile(pic, &file) || !decodeWebp(file, &probe, &pw, &ph) ||
                        !cropWebp(opts.ffmpegPath, pic, pw * 40 / 64, ph * 30 / 64, tmpIcon, &file, true))
                        continue;
                    char stem[8];
                    std::snprintf(stem, sizeof(stem), "%02u", (unsigned)item);
                    const std::string out = joinPath(outDir, std::string(stem) + ".webp");
                    if (!makeDirs(outDir) || !writeFile(out, file)) {
                        if (error) *error = "cannot write " + out;
                        return false;
                    }
                    namedWritten.insert(pathKey(out));
                    usedPack.insert(pic);
                    ++iconsDone;
                }
            }
            std::error_code ec;
            std::filesystem::remove(tmpIcon, ec);
        }
    }
    if (iconsDone)
        progress.info("  inventory icons: " + std::to_string(iconsDone) + " -> " +
                      joinPath(joinPath(hd, treeName), "Data/ITEM_ALL, Data/ITEM_MIX"));

    // ---- 2. everything else: against the game's own dump (original game) ----
    const std::string dumpDir = joinPath(hd, "dump");
    std::vector<DumpTexture> dumps;
    // What the game dumped under a file name (HD/dump/<TREE>/<path>.png), by
    // the hash of its pixels: a hash-named dump with the same pixels is that
    // file's texture.
    std::map<std::string, std::vector<std::string>> namedByHash;
    // The models inside the room files, named without having to be dumped:
    // the tree's own rooms, rendered as the game uploads them.
    std::map<std::string, DumpTexture> roomRendered;
    if (!dcPackGame) {
        std::error_code ec;
        for (const auto& stage : listDirectory(tree)) {
            if (toLower(stage).rfind("stage", 0) != 0 || !isDirectory(joinPath(tree, stage))) continue;
            for (const auto& f : listDirectory(joinPath(tree, stage))) {
                if (toLower(f).rfind("room", 0) != 0 || toLower(extensionOf(f)) != ".rdt") continue;
                std::vector<uint8_t> rdt;
                if (!readFile(joinPath(joinPath(tree, stage), f), &rdt)) continue;
                roomModelTextures(rdt, treeName, &namedByHash, &roomRendered);
            }
        }
    }
    if (!dcPackGame && isDirectory(dumpDir)) {
        std::error_code ec;
        for (const auto& e : std::filesystem::recursive_directory_iterator(dumpDir, ec)) {
            if (!e.is_regular_file(ec)) continue;
            const std::string rel = e.path().lexically_relative(dumpDir).generic_string();
            if (rel.find('/') == std::string::npos || toLower(extensionOf(rel)) != ".png") continue;
            // A room model dumped by an earlier build under its room and slot
            // (stage1/room1051_OBJ0): the game names those by the TIM now.
            const std::string lrel = toLower(rel);
            if (lrel.find("/room") != std::string::npos &&
                (lrel.find("_obj") != std::string::npos || lrel.find("_item") != std::string::npos))
                continue;
            std::vector<uint8_t> data, rgba;
            int w = 0, h = 0;
            if (!readFile(e.path().string(), &data) || !readDumpPng(data, &rgba, &w, &h)) continue;
            char name[17];
            hdtex::HashName(w, h, rgba.data(), name);
            std::vector<std::string>& keys = namedByHash[name];
            const std::string key = rel.substr(0, rel.size() - 4);
            bool known = false;      // the room pass may have named it already
            for (const auto& k : keys) known = known || pathKey(k) == pathKey(key);
            if (!known) keys.push_back(key);
        }
    }
    if (!dcPackGame && isDirectory(dumpDir)) {
        for (const auto& f : listDirectory(dumpDir)) {
            if (extensionOf(f) != ".png" || f.size() != 20) continue;
            const std::string name = toLower(f.substr(0, 16));
            if (usedNames.count(name)) continue;
            std::vector<uint8_t> data, rgba;
            DumpTexture t;
            if (!readFile(joinPath(dumpDir, f), &data) || !readDumpPng(data, &rgba, &t.w, &t.h))
                continue;
            t.name = name;
            t.thumb.resize(kThumbBytes);
            makeThumb(rgba.data(), t.w, t.w, t.h, t.thumb.data());
            t.crName = classicRebirthName(rgba);
            const auto nk = namedByHash.find(name);
            if (nk != namedByHash.end()) t.namedKeys = nk->second;
            dumps.push_back(std::move(t));
        }
    }
    // A dump the game cannot name that is all but the same picture as a room
    // model - an older build's knock-out twin of it (its blending texels
    // cleared), or a palette fix-up reading not hashed above - is that room
    // model: it gets its name rather than keeping a hash and competing with
    // it for the pack's picture.
    std::set<std::string> roomCovered;
    for (auto& t : dumps) {
        if (namedByHash.count(t.name) && roomRendered.count(t.name)) roomCovered.insert(t.name);
        if (!t.namedKeys.empty()) continue;
        for (const auto& rm : roomRendered) {
            if (rm.second.w != t.w || rm.second.h != t.h ||
                thumbError(rm.second.thumb.data(), t.thumb.data()) > 0.5)
                continue;
            t.namedKeys = namedByHash[rm.first];
            roomCovered.insert(rm.first);
            break;
        }
    }
    // Every room model the game has not dumped joins in as if it had.
    for (auto& rm : roomRendered) {
        if (usedNames.count(rm.first) || roomCovered.count(rm.first)) continue;
        rm.second.namedKeys = namedByHash[rm.first];
        dumps.push_back(std::move(rm.second));
    }
    size_t leftOver = packTotal - usedPack.size();
    if (dcPackGame) {
        // A Director's Cut pack holds rooms only, and the table covers them.
    } else if (dumps.empty()) {
        progress.info("  no texture dump in " + dumpDir + ": the other " +
                      std::to_string(leftOver) + " file(s) of the pack (models, "
                      "menus, items, effects) were not imported. Set [Textures] "
                      "DumpTextures=1, play through what you want replaced, and "
                      "run this import again.");
    } else {
        progress.info("  matching the rest of the pack against " +
                      std::to_string(dumps.size()) + " dumped texture(s)...");

        // 2a. By Classic REbirth's own name, where the dumped pixels give it.
        size_t exact = 0;
        for (auto& t : dumps) {
            for (const auto& k : kinds) {
                const auto it = pack[k].find(t.crName);
                if (it == pack[k].end() || usedPack.count(it->second)) continue;
                std::vector<uint8_t> file, probe;
                int pw = 0, ph = 0;
                if (!readFile(it->second, &file) || !decodeWebp(file, &probe, &pw, &ph)) continue;
                if (!installDump(hd, k, t, file, kinds, namedWritten, error)) return false;
                usedPack.insert(it->second);
                t.taken = true;
                ++done[k];
                ++exact;
                break;
            }
        }
        if (exact) progress.info("  " + std::to_string(exact) + " identified by name");

        // 2b. By picture. The sizes a page part may stand for:
        std::set<std::pair<int, int>> partSizes;
        for (const auto& t : dumps) {
            if (!t.taken && t.w <= 256 && t.h <= 256 && !(t.w == 256 && t.h == 256))
                partSizes.insert({t.w, t.h});
        }
        std::vector<PackPicture> pictures;
        size_t seen = 0;
        leftOver = packTotal - usedPack.size();
        for (const auto& k : kinds) {
            for (const auto& entry : pack[k]) {
                const bool used = usedPack.count(entry.second) != 0;
                // A used background or mask is never a room model's picture.
                if (used && (k == "bgd" || k == "mask")) continue;
                if (progress.isCancelled()) {
                    if (error) *error = "cancelled";
                    return false;
                }
                if ((seen++ & 7) == 0) progress.phase((int)seen, (int)leftOver, "reading the pack");
                std::vector<uint8_t> file, rgba;
                PackPicture pic;
                if (!readFile(entry.second, &file) || !decodeWebp(file, &rgba, &pic.w, &pic.h))
                    continue;
                pic.kind = k;
                pic.path = entry.second;
                pic.used = used;
                pic.thumb.resize(kThumbBytes);
                makeThumb(rgba.data(), pic.w, pic.w, pic.h, pic.thumb.data());
                for (const auto& size : partSizes) {
                    int rw = 0, rh = 0;
                    if (!pagePart(pic, size.first, size.second, &rw, &rh)) continue;
                    std::vector<uint8_t>& part = pic.partThumb[size];
                    part.resize(kThumbBytes);
                    makeThumb(rgba.data(), pic.w, rw, rh, part.data());
                }
                pictures.push_back(std::move(pic));
            }
        }
        // A pair is kept when each is the other's closest picture and the
        // difference is under the folder's limit. A pack picture is compared
        // whole when it has the texture's shape, and when it can be a 256x256
        // page holding the texture both whole (the texture stretched over the
        // page, as Classic REbirth hands the doors over) and by its top-left
        // part. A whole picture goes in as it is: the game maps it over the
        // texture's logical size.
        std::vector<int> bestDump(pictures.size(), -1), bestPic(dumps.size(), -1);
        std::vector<double> bestDumpErr(pictures.size(), 1e9), bestPicErr(dumps.size(), 1e9);
        std::vector<char> bestIsPart(pictures.size(), 0);
        for (size_t i = 0; i < pictures.size(); ++i) {
            if (pictures[i].used) continue;
            for (size_t j = 0; j < dumps.size(); ++j) {
                if (dumps[j].taken) continue;
                double e = 1e9;
                bool part = false;
                const auto pt = pictures[i].partThumb.find({dumps[j].w, dumps[j].h});
                if ((int64_t)pictures[i].w * dumps[j].h == (int64_t)pictures[i].h * dumps[j].w ||
                    pt != pictures[i].partThumb.end())
                    e = thumbError(pictures[i].thumb.data(), dumps[j].thumb.data());
                if (pt != pictures[i].partThumb.end()) {
                    const double pe = thumbError(pt->second.data(), dumps[j].thumb.data());
                    if (pe < e) { e = pe; part = true; }
                }
                if (e >= 1e9) continue;
                if (e < bestDumpErr[i]) { bestDumpErr[i] = e; bestDump[i] = (int)j; bestIsPart[i] = part; }
                if (e < bestPicErr[j]) { bestPicErr[j] = e; bestPic[j] = (int)i; }
            }
        }
        size_t cropped = 0, cropFailed = 0;
        const std::string tmp = joinPath(hd, "re1am_crop_tmp.webp");
        for (size_t i = 0; i < pictures.size(); ++i) {
            const int j = bestDump[i];
            if (j < 0 || bestPic[j] != (int)i || bestDumpErr[i] > maxError(pictures[i].kind))
                continue;
            std::vector<uint8_t> file;
            if (bestIsPart[i]) {
                int rw = 0, rh = 0;
                pagePart(pictures[i], dumps[j].w, dumps[j].h, &rw, &rh);
                if (!cropWebp(opts.ffmpegPath, pictures[i].path, rw, rh, tmp, &file)) {
                    ++cropFailed;
                    continue;
                }
                ++cropped;
            } else if (!readFile(pictures[i].path, &file)) {
                continue;
            }
            if (!installDump(hd, pictures[i].kind, dumps[j], file, kinds, namedWritten, error))
                return false;
            usedPack.insert(pictures[i].path);
            pictures[i].used = true;
            dumps[j].taken = true;
            ++done[pictures[i].kind];
        }

        // 2c. Room models by shape. What 2b left of the models inside the room
        // files - most were repainted, so their colours are too far off for
        // 2b - goes by the shape of the picture: the pack picture whose grey
        // levels correlate best, when that is at least 0.85, clearly ahead
        // (0.1) of the next different picture, and not too far off in colour
        // (30). Two pack pictures that are themselves the same picture count
        // as one. Every picture of the pack but the backgrounds and masks
        // takes part, used or not.
        size_t byShape = 0;
        for (size_t j = 0; j < dumps.size(); ++j) {
            const DumpTexture& t = dumps[j];
            bool room = false;
            for (const auto& k : t.namedKeys) room = room || k.find("/RoomModels/") != std::string::npos;
            if (t.taken || !room) continue;
            struct Cand { double shape, err; int pic; bool part; };
            std::vector<Cand> cands;
            for (size_t i = 0; i < pictures.size(); ++i) {
                const PackPicture& pic = pictures[i];
                if (pic.kind == "bgd" || pic.kind == "mask") continue;
                const auto pt = pic.partThumb.find({t.w, t.h});
                if ((int64_t)pic.w * t.h == (int64_t)pic.h * t.w || pt != pic.partThumb.end())
                    cands.push_back({thumbShape(pic.thumb.data(), t.thumb.data()),
                                     thumbError(pic.thumb.data(), t.thumb.data()), (int)i, false});
                if (pt != pic.partThumb.end())
                    cands.push_back({thumbShape(pt->second.data(), t.thumb.data()),
                                     thumbError(pt->second.data(), t.thumb.data()), (int)i, true});
            }
            if (cands.empty()) continue;
            std::sort(cands.begin(), cands.end(),
                      [](const Cand& a, const Cand& b) { return a.shape > b.shape; });
            const Cand& best = cands[0];
            const uint8_t* bestThumb = best.part
                ? pictures[best.pic].partThumb.at({t.w, t.h}).data() : pictures[best.pic].thumb.data();
            double next = -1.0;
            for (size_t c = 1; c < cands.size(); ++c) {
                if (cands[c].pic == best.pic) continue;
                const PackPicture& o = pictures[cands[c].pic];
                const uint8_t* ot = cands[c].part ? o.partThumb.at({t.w, t.h}).data() : o.thumb.data();
                if (thumbError(ot, bestThumb) < 3.0) continue;      // the same picture twice
                next = cands[c].shape;
                break;
            }
            if (best.shape < 0.85 || best.err > 30.0 || best.shape - next < 0.1) continue;
            std::vector<uint8_t> file;
            if (best.part) {
                int rw = 0, rh = 0;
                pagePart(pictures[best.pic], t.w, t.h, &rw, &rh);
                if (!cropWebp(opts.ffmpegPath, pictures[best.pic].path, rw, rh, tmp, &file)) {
                    ++cropFailed;
                    continue;
                }
            } else if (!readFile(pictures[best.pic].path, &file)) {
                continue;
            }
            if (!installDump(hd, pictures[best.pic].kind, t, file, kinds, namedWritten, error))
                return false;
            usedPack.insert(pictures[best.pic].path);
            dumps[j].taken = true;
            ++byShape;
        }
        if (byShape)
            progress.info("  " + std::to_string(byShape) + " room model(s) matched by shape");
        if (cropped)
            progress.info("  " + std::to_string(cropped) + " cut out of a 256x256 page picture");
        if (cropFailed)
            progress.info("  " + std::to_string(cropFailed) + " page part(s) could not be cut "
                          "out: that needs ffmpeg with libwebp");
        progress.phase((int)leftOver, (int)leftOver, "reading the pack");
    }

    // Per pack folder, the files that went in - as a texture of their own, or
    // as one palette half of a model skin.
    for (const auto& k : kinds) {
        size_t used = 0;
        for (const auto& f : pack[k]) used += usedPack.count(f.second);
        progress.info("  " + k + ": " + std::to_string(used) + " of " +
                      std::to_string(pack[k].size()) + " used");
    }
    if (usedPack.empty()) {
        if (error) *error = "nothing in this pack could be matched";
        return false;
    }
    if (packTotal > usedPack.size())
        progress.info("  " + std::to_string(packTotal - usedPack.size()) + " file(s) had no "
                      "confident match and were left out." +
                      std::string(dcPackGame ? "" : " Textures the game has not dumped yet "
                                                    "cannot be matched: dump more and run again."));
    if (dcPackGame) {
        progress.info("HD textures are on by default ([Textures] HdTextures=1). The "
                      "Director's Cut rooms are named after the DC folder's files.");
    } else {
        progress.info("HD textures are on by default ([Textures] HdTextures=1). The "
                      "backgrounds, masks, model skins and Data/ textures are named "
                      "after their files; what was matched against the dump is named "
                      "by its pixels, so run the import again after changing the "
                      "tree's background format.");
    }
    return true;
}

namespace {

// One PC FMV: the pack's stem (the Japanese release's name) and the stems the
// two FMV tables name (src/video/VideoPlayback.cpp g_FMVTableUSA/JPN).
struct HdMovieName {
    const char* pack;
    const char* usa;
    const char* jpn;
};

const HdMovieName kHdMovies[] = {
    {"OJ", "OU", "OJ"},         {"PJ", "PU", "PJ"},
    {"DMF", "DMF", "DMF"},      {"DM3", "DM3", "DM3"},
    {"DM4", "DM4", "DM4"},      {"DM1", "DM1", "DM1"},
    {"DM6", "DM6", "DM6"},      {"DM7", "DM7", "DM7"},
    {"DM8", "DM8", "DM8"},      {"DM2", "DM2", "DM2"},
    {"DMB", "DMB", "DMB"},      {"DMC", "DMC", "DMC"},
    {"DMD", "DMD", "DMD"},      {"DME", "DME", "DME"},
    {"ED1", "ED1", "ED1"},      {"ED2", "ED2", "ED2"},
    {"ED3", "ED3", "ED3"},      {"ED4", "EU4", "ED4"},
    {"ED5", "EU5", "ED5"},      {"ED6", "ED6", "ED6"},
    {"ED7", "ED7", "ED7"},      {"ED8", "ED8", "ED8"},
    {"CAPCOM", "CAPCOM", "CAPCOM"},
    {"STFC_B", "STFC_R", "STFC_B"}, {"STFJ_B", "STFJ_R", "STFJ_B"},
    {"STFZ_B", "STFZ_R", "STFZ_B"}, {"STAF_B", "STAF_R", "STAF_B"},
    {"VLOGO", "VLOGO", "VLOGO"},
};

// `<dir>/<clips>/<cut>/<file>`, every part matched case-insensitively ("" when
// the pack does not have it).
std::string hdMovieClip(const std::string& dir, const char* clips, const char* cut,
                        const std::string& file) {
    const std::string c = childCI(dir, clips, true);
    if (c.empty()) return std::string();
    const std::string cd = joinPath(dir, c);
    const std::string k = childCI(cd, cut, true);
    if (k.empty()) return std::string();
    const std::string f = childCI(joinPath(cd, k), file, false);
    return f.empty() ? std::string() : joinPath(joinPath(cd, k), f);
}

}  // namespace

bool importHdMovies(const HdMovieOptions& opts, const Progress& progress,
                    std::string* error) {
    namespace fs = std::filesystem;
    progress.info("HD movie import: " + opts.movieDir);
    if (opts.movieDir.empty() || !isDirectory(opts.movieDir)) {
        if (error) *error = "no HD movie folder selected";
        return false;
    }
    if (opts.targetRoot.empty()) {
        if (error) *error = "no target game folder selected";
        return false;
    }
    // HD/<TREE>/Movie: the game looks a movie up as HD/<TREE>/MOVIE/<name>.mp4
    // before the tree's own (src/video/VideoPlayback.cpp, CheckVideoFileExists).
    const std::string hdTree = joinPath(joinPath(opts.targetRoot, "HD"), versionName(opts.tree));
    std::string movieName = isDirectory(hdTree) ? childCI(hdTree, "Movie", true) : std::string();
    if (movieName.empty()) movieName = "Movie";
    const std::string movieDir = joinPath(hdTree, movieName);
    if (!makeDirs(movieDir)) {
        if (error) *error = "cannot create " + movieDir;
        return false;
    }
    const std::vector<std::string> hdFiles = listDirectory(movieDir);

    // The pack's .mp4 files, by upper-case stem.
    std::map<std::string, std::string> pack;
    for (const auto& f : listDirectory(opts.movieDir)) {
        const std::string path = joinPath(opts.movieDir, f);
        if (extensionOf(f) != ".mp4" || !isRegularFile(path)) continue;
        pack[toUpper(fs::path(f).stem().string())] = path;
    }
    // The chosen cuts of the two movies that come in more than one.
    if (opts.capcom != HdCapcomIntro::PackDefault) {
        const char* cut = opts.capcom == HdCapcomIntro::Cubes ? "Cubes" : "Original";
        const std::string f = hdMovieClip(opts.movieDir, "Capcom clips", cut, "Capcom.mp4");
        if (f.empty()) progress.info("  warning: no Capcom clips/" + std::string(cut) +
                                     "/Capcom.mp4; using the main folder's");
        else pack["CAPCOM"] = f;
    }
    if (opts.dm7 != HdDm7Clip::PackDefault) {
        const char* cut = opts.dm7 == HdDm7Clip::JillScene ? "Jill-scene addition" : "Original";
        const std::string f = hdMovieClip(opts.movieDir, "DM7 clips", cut, "Dm7.mp4");
        if (f.empty()) progress.info("  warning: no DM7 clips/" + std::string(cut) +
                                     "/Dm7.mp4; using the main folder's");
        else pack["DM7"] = f;
    }

    const int total = (int)(sizeof(kHdMovies) / sizeof(kHdMovies[0]));
    size_t copied = 0, replaced = 0;
    std::set<std::string> used;
    for (int i = 0; i < total; ++i) {
        if (progress.isCancelled()) {
            if (error) *error = "cancelled";
            return false;
        }
        const HdMovieName& m = kHdMovies[i];
        progress.phase(i, total, "movies");
        const auto it = pack.find(m.pack);
        if (it == pack.end()) continue;
        used.insert(m.pack);
        const std::string stem = opts.tree == AssetVersion::USA ? m.usa : m.jpn;
        // An earlier import's spelling, else the FMV table's name in upper case.
        std::string out;
        for (const auto& f : hdFiles)
            if (toLower(f) == toLower(stem) + ".mp4") out = f;
        const bool existed = !out.empty();
        if (out.empty()) out = stem + ".mp4";
        std::error_code ec;
        fs::copy_file(fs::path(it->second), fs::path(joinPath(movieDir, out)),
                      fs::copy_options::overwrite_existing, ec);
        if (ec) {
            if (error) *error = "cannot write " + joinPath(movieDir, out) + ": " + ec.message();
            return false;
        }
        progress.info("  " + baseName(it->second) + " -> HD/" + versionName(opts.tree) + "/" +
                      movieName + "/" + out + (existed ? " (replaced)" : ""));
        ++copied;
        if (existed) ++replaced;
    }
    progress.phase(total, total, "movies");
    for (const auto& p : pack)
        if (!used.count(p.first))
            progress.info("  " + baseName(p.second) + ": not a game movie, left out");
    if (copied == 0) {
        if (error) *error = "no game movie found in this folder";
        return false;
    }
    progress.info("  " + std::to_string(copied) + " movie(s) copied to " + movieDir +
                  (replaced ? " (" + std::to_string(replaced) + " replaced an earlier import)"
                            : std::string()) +
                  ". The game plays them in place of the tree's movies while HD "
                  "textures are on ([Textures] HdTextures=1, the default).");
    return true;
}

}  // namespace re1
