// Saturn overlay builder: <target>/SATURN from a Sega Saturn disc image, or
// <target>/SATURN_JPN from the Japanese "Bio Hazard" disc (core/SaturnRegion.h:
// same file names, Japanese text, the game reads it with [Assets] Version=JPN).
//
// Only what the Saturn release adds on top of the PC/PS1 content goes into the
// overlay; everything else is shared with the base tree:
//
//   Enemy/EM1016.EMD, EM1116.EMD   the Tick (Chris / Jill model table block)
//   Enemy/EM1017.EMD, EM1117.EMD   Zombie Wesker
//   Enemy/EM1060.EMD, EM1061.EMD   Chris' two Saturn alternate outfits (disc EM1030,
//   Enemy/EM1160.EMD, EM1161.EMD   EM1032) and Jill's (disc EM1031, EM1033). Renamed:
//                                  the disc's names are the OG's and the DC's own
//                                  alternate outfits, which these must not shadow
//                                  -> converted to PC .EMDs (core/SaturnEmd)
//   (all of the above, the arenas, the Tyrant room, the title font and the Battle
//   Game's sound are always written; only the switches below are optional)
//   Enemy/EM10xx.EMD, EM11xx.EMD,  [allModels] every other enemy, NPC and player
//   Enemy/CHAR10..13.EMD           model on the disc, converted the same way
//   Stage1..7/ROOM*.RDT, RC*.pak   [allRooms] every other campaign room and its
//                                  backgrounds, decoded
//   Sound/<PC BGM name>            [allBgm] every campaign BGM, rendered from
//                                  SND/BGM<group>.CDP
//   Stage8/ROOM8010..80F0.RDT      the 15 Battle Game arenas, LZ77-decoded;
//                                  decoded they are ordinary PS1 RDTs
//   Stage5/RC5130..5137.pak        the Laboratory Tyrant room's backgrounds,
//                                  which the Saturn re-drew on all eight
//                                  cameras (the tank vats are emptied because
//                                  the Saturn cannot do translucent glass, and
//                                  the second capsule is in this room)
//                                  -> decoded and re-packed (core/SaturnBss)
//   Stage1/ROOM11C0/1.RDT,         the wardrobe, decoded and patched so the
//   Stage6/ROOM61C0/1.RDT          Saturn's two outfits are offered by the room
//                                  script (core/SaturnWardrobe): on the Saturn
//                                  they are GAME5 code, not RDT data
//   (Data/TITLEFNT.BIN, the title menu's typeface, is the PC tab's now: it is
//   written into the base tree, core/Migrate.cpp)
//
// And into the BASE tree, beside the overlay:
//   <USA|JPN>/Enemy/EM1061.EMD,    copies of the Saturn-exclusive outfit, which
//   <USA|JPN>/Enemy/EM1161.EMD     the game offers as a bonus outfit in every
//                                  mode (src/game/Bonus.h)
//   Sound/Bgm_58, Bgm_59 (battle), Bgm_5a (ranking), Bgm_24
//                                  the Battle Game's own music, rendered from
//                                  SND/BATTLE.CDP and SND/NAME.CDP through the
//                                  Sega sound driver + SCSP model
//                                  (core/SaturnSound, core/SaturnMusic)
//
// Not migrated yet, and logged as such: the rest of the Saturn audio (the
// campaign's own BGM arrangement, the SE banks, the .PCK room PCM and the X68
// SSCONV .BIN banks), the MOVIE.CPK FMV, the title/ending BM60
// art, and the A_*.DAT room props (the extra lab Tyrant capsule), whose format
// is decoded by tools/saturn_mbn.py but has no PC writer yet. The 348 campaign
// RDTs decode byte-identical to the PS1's, and the arenas reuse campaign
// backgrounds, so neither needs a copy. Only the rooms listed in kRoomBgs are
// copied: of 655 Saturn camera frames, those are the ones whose art the Saturn
// changed beyond a palette shift (tools/saturn_bss_to_pak.py --verify).
// Reference: docs/SATURN_VERSION_ANALYSIS.md, docs/SATURN_PORT.md.

#include "core/DiscImage.h"
#include "core/Migrate.h"
#include "core/Pak.h"
#include "core/SaturnBss.h"
#include "core/SaturnEmd.h"
#include "core/SaturnLz77.h"
#include "core/SaturnMovie.h"
#include "core/SaturnMusic.h"
#include "core/SaturnObModel.h"
#include "core/SaturnWardrobe.h"
#include "core/TitleFont.h"
#include "core/Tim.h"
#include "core/Util.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

namespace re1 {

namespace {

// disc file -> overlay file. The creatures keep the disc's names (their model-
// table slots are the PC's placeholders, saturn/SaturnModels.h). The alternate
// outfits do NOT: the Saturn calls them EM1030..EM1033 - the very names the PC
// and the DC give their own alternate outfits - so the overlay files take names
// no release uses (EM106x Chris, EM116x Jill; the same 10xx/11xx split as the
// creatures), and the port selects them by model-table index, not by name. That
// is what keeps them from colliding with the OG and DC outfits.
struct Model {
    const char* disc;
    const char* out;
    const char* what;
    bool weapon;  // holds a weapon model: keeps the weapons' texture page
};

const Model kModels[] = {
    {"EM1016.EMD", "EM1016.EMD", "the Tick (Chris's game)", false},
    {"EM1116.EMD", "EM1116.EMD", "the Tick (Jill's game)", false},
    {"EM1017.EMD", "EM1017.EMD", "Zombie Wesker (Chris's game)", false},
    {"EM1117.EMD", "EM1117.EMD", "Zombie Wesker (Jill's game)", false},
    {"EM1030.EMD", "EM1060.EMD", "Chris, Saturn alternate outfit 1", true},
    {"EM1032.EMD", "EM1061.EMD", "Chris, Saturn alternate outfit 2", true},
    {"EM1031.EMD", "EM1160.EMD", "Jill, Saturn alternate outfit 1", true},
    {"EM1033.EMD", "EM1161.EMD", "Jill, Saturn alternate outfit 2", true},
};
constexpr int kModelCount = (int)(sizeof(kModels) / sizeof(kModels[0]));

// True for a disc model the mandatory pass above already wrote. The outfits
// matter most: their disc names (EM1030..EM1033) are the PC's own alternate
// outfits', so the "all models" pass must not write them under those names.
bool isMandatoryModel(const std::string& discName) {
    for (const Model& m : kModels)
        if (discName == m.disc) return true;
    return false;
}

const char* const kArenas[] = {"ROOM8010.RDT", "ROOM8020.RDT", "ROOM8030.RDT",
                               "ROOM8040.RDT", "ROOM8050.RDT", "ROOM8060.RDT",
                               "ROOM8070.RDT", "ROOM8080.RDT", "ROOM8090.RDT",
                               "ROOM80A0.RDT", "ROOM80B0.RDT", "ROOM80C0.RDT",
                               "ROOM80D0.RDT", "ROOM80E0.RDT", "ROOM80F0.RDT"};

// The rooms the Saturn re-drew: disc folder, .BSS file name, stage digit for
// the PC path, room id for the PC path. Everything else falls back to the base
// tree - see the header and tools/saturn_bss_to_pak.py.
struct RoomBg {
    const char* folder;
    const char* bss;
    char stageDigit;
    int roomId;
    const char* why;
};

const RoomBg kRoomBgs[] = {
    {"STAGE5", "ROOM513.BSS", '5', 0x13,
     "Laboratory B4, the Tyrant room - emptied vats, second capsule"},
};

// The camera pictures the game shows on the bordered screen (MSF2_SCREEN_BORDER,
// set by the room's own script): the cave rooms, the two lab elevators and the
// mansion elevator (stage 7 room 0x00, whose picture is stage 2's RC2000). For
// these the game reads the background as a PACKED 316x236 bitmap - 316 pixels
// per row, rows back to back - out of the 320x240 buffer (display_image(...,
// 316, 236), src/game/Room.cpp), and the PC's paks are stored that way. The
// Saturn's frame is a plain 320x240 picture whose top-left 316x236 is the PC's
// picture (offset 0,0, checked on all five rooms below), so written as it is it
// came out sheared four pixels a row: diagonal streaks.
//
// The list is every PC picture that is packed - all 29, found by row
// continuity in the USA tree (a packed picture is smooth only at stride 316);
// every other one of its 650 pictures is smooth only at stride 320. Packing is
// per picture, not per room: RC2001..RC2004 are plain.
struct BorderedBg {
    char stageDigit;
    int roomId;
    unsigned cameraMask;   // bit n: camera n
};

const BorderedBg kBorderedBgs[] = {
    {'2', 0x00, 0x01},   // mansion elevator (stage 7 room 0x00 reads RC2000)
    {'3', 0x06, 0x0F},   // caves
    {'3', 0x07, 0x7F},
    {'3', 0x0B, 0xFF},
    {'3', 0x0F, 0xFF},
    {'5', 0x0D, 0x01},   // lab elevators
    {'5', 0x15, 0x01},
};

constexpr int kBorderedW = 316;
constexpr int kBorderedH = 236;

bool isBorderedBg(char stageDigit, int roomId, int camera) {
    for (const BorderedBg& b : kBorderedBgs)
        if (b.stageDigit == stageDigit && b.roomId == roomId && camera >= 0 && camera < 32 &&
            (b.cameraMask >> camera) & 1u)
            return true;
    return false;
}

// The frame's top-left 316x236 with its rows back to back, as the PC stores a
// bordered picture; the 1,424 pixels left over at the end are black.
std::vector<uint8_t> packBorderedFrame(const std::vector<uint8_t>& rgb) {
    std::vector<uint8_t> out(rgb.size(), 0);
    for (int y = 0; y < kBorderedH; ++y)
        std::memcpy(&out[(size_t)y * kBorderedW * 3], &rgb[(size_t)y * kSaturnBgWidth * 3],
                    (size_t)kBorderedW * 3);
    return out;
}

// The pixels the PC background writer gets for camera `camera` of a room.
std::vector<uint8_t> backgroundPixels(char stageDigit, int roomId, const SaturnBssFrame& f) {
    return isBorderedBg(stageDigit, roomId, f.index) ? packBorderedFrame(f.rgb) : f.rgb;
}

// Campaign rooms whose *data* the Saturn also cut differently, and which are
// therefore overlaid as a whole .RDT rather than patched in code. Both ship two
// files per room (the two character variants, chosen by the last digit of
// room5S RRc.rdt) and the Saturn's disc has both too, so each is listed and read
// from its own file: copying the Chris room to the Jill name gave Jill his scripts.
struct RoomRdt {
    const char* folder;
    const char* rdt;
    char stageDigit;
    int roomId;
    const char* why;
};

// The wardrobe rooms (stage 1 and its revisit copy, stage 6), both character
// variants: written always, patched by patchSaturnWardrobeRdt. Without them the
// base tree's room would run the PC's one-shot costume script.
const char* const kWardrobeRdts[][2] = {
    {"STAGE1", "ROOM11C0.RDT"}, {"STAGE1", "ROOM11C1.RDT"},
    {"STAGE6", "ROOM61C0.RDT"}, {"STAGE6", "ROOM61C1.RDT"},
};
constexpr int kWardrobeCount = (int)(sizeof(kWardrobeRdts) / sizeof(kWardrobeRdts[0]));

#define LAB_TYRANT_WHY     "Laboratory B4: the PC's room has one item the Saturn's does not (the "     "init script is 28 bytes - one item_model_set - longer and item_icons "     "holds two 40x30 icons against one), so the Saturn's room is loaded "     "instead of the PC's"
const RoomRdt kRoomRdts[] = {
    {"STAGE5", "ROOM5130.RDT", '5', 0x13, LAB_TYRANT_WHY},
    {"STAGE5", "ROOM5131.RDT", '5', 0x13, LAB_TYRANT_WHY},
};
#undef LAB_TYRANT_WHY

// Jill's flamethrower (see SaturnEmdOptions::donorEmd). The disc's names of the
// models Jill is loaded from; Chris's CHAR10 is the one whose page has the texels.
bool isJillPlayerModel(const std::string& discName) {
    return discName == "CHAR11.EMD" || discName == "EM1031.EMD" || discName == "EM1033.EMD";
}

// <root>/<folder>/<file>, each matched without regard to case; "" when absent.
std::string findNoCase(const std::string& root, const std::string& folder,
                       const std::string& file) {
    for (const std::string& d : listDirectory(root))
        if (toLower(d) == toLower(folder))
            for (const std::string& f : listDirectory(joinPath(root, d)))
                if (toLower(f) == toLower(file)) return joinPath(joinPath(root, d), f);
    return std::string();
}

bool checkPcEmd(const std::vector<uint8_t>& d, std::string* why);

// A weapon .EMW ends in two dwords, the second the offset of its TMD: one
// object of 0x34000609 packets. Flags the texels the packets cover (128 x 256,
// each packet's UV bounding box) and returns where the packets are and how many;
// `row` is the CLUT row the first packet reads. False if it is not that shape.
bool weaponFootprint(const std::vector<uint8_t>& d, std::vector<uint8_t>* mask,
                     size_t* primsOut, size_t* countOut, int* row) {
    if (d.size() < 64) return false;
    const size_t tmd = readU32(d.data(), d.size(), d.size() - 4) & ~3u;
    if (tmd + 12 + 28 > d.size() || readU32(d.data(), d.size(), tmd + 8) != 1) return false;
    const size_t prims = tmd + 12 + readU32(d.data(), d.size(), tmd + 12 + 16);
    const size_t count = readU32(d.data(), d.size(), tmd + 12 + 20);
    if (count == 0 || prims + 28 * count > d.size()) return false;
    mask->assign((size_t)128 * 256, 0);
    for (size_t i = 0; i < count; ++i) {
        const size_t pk = prims + 28 * i;
        if (readU32(d.data(), d.size(), pk) != 0x34000609) return false;
        int u0 = 255, u1 = 0, v0 = 255, v1 = 0;
        for (int c = 0; c < 3; ++c) {
            if (d[pk + 4 + 4 * c] > 127) return false;
            u0 = std::min<int>(u0, d[pk + 4 + 4 * c]);
            u1 = std::max<int>(u1, d[pk + 4 + 4 * c]);
            v0 = std::min<int>(v0, d[pk + 5 + 4 * c]);
            v1 = std::max<int>(v1, d[pk + 5 + 4 * c]);
        }
        for (int y = v0; y <= v1; ++y)
            for (int x = u0; x <= u1; ++x) (*mask)[(size_t)y * 128 + x] = 1;
    }
    *primsOut = prims;
    *countOut = count;
    *row = ((readU16(d.data(), d.size(), prims + 6) >> 6) & 0x1FF) - 0x1E0;
    return true;
}

// The footprint as above, then every packet moved 128 texels right, onto the
// page half the donor texels are written to.
bool patchWeaponToRightHalf(std::vector<uint8_t>* emw, std::vector<uint8_t>* mask) {
    std::vector<uint8_t>& d = *emw;
    size_t prims = 0, count = 0;
    int row = 0;
    if (!weaponFootprint(d, mask, &prims, &count, &row)) return false;
    for (size_t i = 0; i < count; ++i)
        for (int c = 0; c < 3; ++c) d[prims + 28 * i + 4 + 4 * c] += 128;
    return true;
}

// The DC's custom Beretta (Players/W0F.EMW Chris, W1F.EMW Jill, DC overlay
// only). Like every weapon model it samples its holder's left page, but it
// reaches texels the plain Beretta does not (Jill's: u 0..7, v 66..87), and
// only the DC's own CHAR10/CHAR11 have the gun's art there. A Saturn outfit's
// page is the PS1 original's (yellow filler there on Jill's), so worn in the
// DC the gun came out yellow. Gives the colours the DC holder shows under the
// gun's footprint (128 x 256 RGB555, 0 = hole) and the mask; false when the
// DC overlay is not in the target folder.
bool dcCustomBerettaTexels(const std::string& targetRoot, const char* emwName,
                           const char* emdName, std::vector<uint8_t>* mask,
                           std::vector<uint16_t>* colours) {
    const std::string dc = joinPath(targetRoot, "DC");
    const std::string emwPath = findNoCase(dc, "Players", emwName);
    const std::string emdPath = findNoCase(dc, "Enemy", emdName);
    std::vector<uint8_t> emw, emd;
    std::string why;
    if (emwPath.empty() || emdPath.empty() || !readFile(emwPath, &emw) ||
        !readFile(emdPath, &emd) || !checkPcEmd(emd, &why))
        return false;
    size_t prims = 0, count = 0;
    int row = 0;
    if (!weaponFootprint(emw, mask, &prims, &count, &row)) return false;
    const uint8_t* e = emd.data();
    const size_t n = emd.size();
    const size_t tim = readU32(e, n, n - 4);
    const size_t clen = readU32(e, n, tim + 8);
    const int cw = readU16(e, n, tim + 16), ch = readU16(e, n, tim + 18);
    const size_t img = tim + 8 + clen;
    if (cw != 256 || row < 0 || row >= ch || img + 12 > n) return false;
    const int w = readU16(e, n, img + 8) * 2, h = readU16(e, n, img + 10);
    if (w < 128 || h < 256 || img + 12 + (size_t)w * h > n) return false;
    colours->assign((size_t)128 * 256, 0);
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 128; ++x) {
            const uint8_t i = e[img + 12 + (size_t)y * w + x];
            if (i)
                (*colours)[(size_t)y * 128 + x] =
                    readU16(e, n, tim + 20 + 512 * (size_t)row + 2 * (size_t)i);
        }
    return true;
}

// The disc's names of the models Chris is loaded from (see isJillPlayerModel).
bool isChrisPlayerModel(const std::string& discName) {
    return discName == "CHAR10.EMD" || discName == "EM1030.EMD" || discName == "EM1032.EMD";
}

bool readDiscFile(const DiscImage& img, const std::string& folder,
                  const std::string& name, std::vector<uint8_t>* out,
                  std::string* error) {
    const DiscEntry* e = img.findInFolder(folder, name);
    if (!e) {
        if (error) *error = folder + "/" + name + " is not on the disc";
        return false;
    }
    if (!img.readFile(*e, out)) {
        if (error) *error = "cannot read " + folder + "/" + name;
        return false;
    }
    return true;
}

// A PC .EMD ends in a 5-dword directory; its TMD and TIM must be where it says.
bool checkPcEmd(const std::vector<uint8_t>& d, std::string* why) {
    if (d.size() < 64 || d.size() % 4) {
        *why = "bad size";
        return false;
    }
    const uint32_t tmd = readU32(d.data(), d.size(), d.size() - 8);
    const uint32_t tim = readU32(d.data(), d.size(), d.size() - 4);
    if (tmd >= tim || tim >= d.size()) {
        *why = "directory out of range";
        return false;
    }
    if (readU32(d.data(), d.size(), tim) != 0x10 ||
        readU32(d.data(), d.size(), tim + 4) != 9) {
        *why = "no 8 bpp TIM at the directory's texture offset";
        return false;
    }
    const uint32_t tmdSize = readU32(d.data(), d.size(), tmd);
    if (tmd + tmdSize > tim) {
        *why = "TMD overruns the TIM";
        return false;
    }
    return true;
}

// A background pak must come back out of the game's own LZW as exactly the one
// TIM the reader expects: 16 bpp, VRAM (0,240), 320x240, image block length
// counting only the pixel bytes. That checks the encoder against the decoder
// instead of against itself. See docs/PSX_BSS_AND_PC_PAK.md.
bool checkBackgroundPak(const std::vector<uint8_t>& d, std::string* why) {
    std::vector<uint8_t> tim;
    if (!lzwUnpack(d.data(), d.size(), &tim, why)) return false;
    if (tim.size() != 20 + 320 * 240 * 2) {
        *why = "decodes to " + std::to_string(tim.size()) + " B, expected " +
               std::to_string(20 + 320 * 240 * 2);
        return false;
    }
    if (readU32(tim.data(), tim.size(), 0) != 0x10 ||
        readU32(tim.data(), tim.size(), 4) != 2 ||
        readU32(tim.data(), tim.size(), 8) != 320 * 240 * 2) {
        *why = "not a 16 bpp background TIM";
        return false;
    }
    if (readU16(tim.data(), tim.size(), 12) != 0 ||
        readU16(tim.data(), tim.size(), 14) != 240 ||
        readU16(tim.data(), tim.size(), 16) != 320 ||
        readU16(tim.data(), tim.size(), 18) != 240) {
        *why = "wrong VRAM position or size";
        return false;
    }
    return true;
}

}  // namespace

bool migrateSaturnAssets(const SaturnMigrationOptions& opts,
                         const Progress& progress, std::string* error) {
    progress.info("Sega Saturn migration");
    if (opts.imagePath.empty()) {
        if (error) *error = "no disc image selected";
        return false;
    }
    if (opts.targetRoot.empty()) {
        if (error) *error = "no target game folder selected";
        return false;
    }
    DiscImage img;
    std::string err;
    if (!img.open(opts.imagePath, &err)) {
        if (error) *error = err;
        return false;
    }
    if (!img.findInFolder("ENEMY", "EM1016.EMD") ||
        !img.findInFolder("STAGE8", "ROOM8010.RDT")) {
        if (error)
            *error = "this is not the Resident Evil / Bio Hazard Saturn disc (no "
                     "ENEMY/EM1016.EMD or STAGE8/ROOM8010.RDT)";
        return false;
    }
    // USA or Japan: the two discs share every file name, so the release is told
    // apart by its GAME5.PRG - by where the wardrobe questions sit, the very
    // texts the wardrobe patch below writes (see core/SaturnRegion.h).
    SaturnRegion region;
    {
        const DiscEntry* e = img.find("GAME5.PRG");
        std::vector<uint8_t> game5;
        if (!e || !img.readFile(*e, &game5)) {
            if (error) *error = "cannot read /GAME5.PRG";
            return false;
        }
        if (!saturnRegionFromGame5(game5, &region)) {
            if (error)
                *error = "GAME5.PRG is neither the USA nor the Japanese release's "
                         "(the wardrobe questions are not where either keeps them)";
            return false;
        }
    }
    progress.info(std::string("disc: ") + saturnRegionName(region));
    if (region == SaturnRegion::Jpn)
        progress.info("  Japanese text: the overlay goes to SATURN_JPN and is read with "
                      "config.ini [Assets] Version=JPN");
    const std::string overlay = joinPath(opts.targetRoot, saturnOverlayFolder(region));
    if (!makeDirs(overlay)) {
        if (error) *error = "cannot create " + overlay;
        return false;
    }
    progress.info("target: " + overlay);

    std::vector<std::string> written;
    // The Saturn disc's campaign rooms and backgrounds, for the optional pass.
    struct DiscRoom {
        const DiscEntry* rdt;     // STAGEn/ROOMsRRv.RDT
        const DiscEntry* bss;     // STAGEn/ROOMsRR.BSS, may be null
        std::string rdtName;      // ROOMsRRv.RDT
        std::string bssName;
        char stageDigit;
        int roomId;
    };
    std::vector<DiscRoom> allRoomList;
    if (opts.allRooms) {
        for (const DiscEntry& e : img.entries()) {
            if (e.directory) continue;
            const size_t slash = e.path.rfind('/');
            if (slash == std::string::npos || slash < 6) continue;
            const std::string folder = e.path.substr(slash - 6, 6);
            const std::string file = e.path.substr(slash + 1);
            // STAGE1..STAGE7: STAGE8 is the Battle Game's, written above.
            if (folder.compare(0, 5, "STAGE") != 0 || folder[5] < '1' || folder[5] > '7' ||
                file.size() != 12 || file.compare(0, 4, "ROOM") != 0 ||
                file.compare(8, 4, ".RDT") != 0)
                continue;
            DiscRoom r;
            r.rdt = &e;
            r.rdtName = file;
            r.stageDigit = folder[5];
            r.roomId = (int)std::strtol(file.substr(5, 2).c_str(), nullptr, 16);
            r.bssName = file.substr(0, 7) + ".BSS";
            r.bss = img.findInFolder(folder, r.bssName);
            allRoomList.push_back(std::move(r));
        }
    }

    // The rest of ENEMY/*.EMD, for the optional pass: every model the mandatory
    // set does not already cover.
    std::vector<const DiscEntry*> allModelList;
    if (opts.allModels) {
        for (const DiscEntry& e : img.entries()) {
            if (e.directory) continue;
            const size_t slash = e.path.rfind('/');
            if (slash == std::string::npos || slash < 5 ||
                e.path.compare(slash - 5, 5, "ENEMY") != 0)
                continue;
            const std::string file = e.path.substr(slash + 1);
            if (file.size() < 5 || file.compare(file.size() - 4, 4, ".EMD") != 0 ||
                isMandatoryModel(file))
                continue;
            allModelList.push_back(&e);
        }
    }

    // Progress units: the fixed work, plus a unit per room and per BGM when asked.
    const int fixedUnits = kModelCount + 15 +
                           (int)(sizeof(kRoomBgs) / sizeof(kRoomBgs[0])) +
                           (int)(sizeof(kRoomRdts) / sizeof(kRoomRdts[0])) +
                           kWardrobeCount + 2;
    const int total = fixedUnits + (int)allModelList.size() + (int)allRoomList.size() +
                      (opts.allBgm ? 1 : 0) + (opts.movies ? 1 : 0);
    int done = 0;

    // Jill's flamethrower: her models get the flamethrower's texels from Chris's
    // page, and (with all models, i.e. with her default model converted too)
    // the overlay gets a W15.EMW that reads them. The weapon file is the base
    // tree's - the Saturn disc has no .EMW - so without one nothing changes.
    std::vector<uint8_t> flameEmw, flameMask, chrisEmd;
    {
        const std::string base =
            joinPath(opts.targetRoot, region == SaturnRegion::Jpn ? "JPN" : "USA");
        const std::string path = findNoCase(base, "Players", "W15.EMW");
        if (path.empty() || !readFile(path, &flameEmw) ||
            !patchWeaponToRightHalf(&flameEmw, &flameMask) ||
            !readDiscFile(img, "ENEMY", "CHAR10.EMD", &chrisEmd, nullptr)) {
            flameEmw.clear();
            flameMask.clear();
            progress.info("no " + joinPath(joinPath(base, "Players"), "W15.EMW") +
                          ": Jill's flamethrower keeps the base tree's look");
        }
    }
    // The DC's custom Beretta: its texels from the DC overlay's own player
    // models, written into the Saturn players' left page (dcCustomBerettaTexels).
    std::vector<uint8_t> berettaMask[2];
    std::vector<uint16_t> berettaColours[2];
    {
        const char* const emw[2] = {"W0F.EMW", "W1F.EMW"};
        const char* const emd[2] = {"CHAR10.EMD", "CHAR11.EMD"};
        for (int c = 0; c < 2; ++c)
            if (!dcCustomBerettaTexels(opts.targetRoot, emw[c], emd[c], &berettaMask[c],
                                       &berettaColours[c])) {
                berettaMask[c].clear();
                berettaColours[c].clear();
            }
        if (berettaMask[0].empty() || berettaMask[1].empty())
            progress.info("no Director's Cut overlay in " + joinPath(opts.targetRoot, "DC") +
                          ": the DC's custom Beretta keeps the PS1 page's look on the "
                          "Saturn outfits (migrate the Director's Cut first, then this disc)");
    }
    auto setBeretta = [&](const std::string& disc, SaturnEmdOptions* eo) {
        const int c = isChrisPlayerModel(disc) ? 0 : isJillPlayerModel(disc) ? 1 : -1;
        if (c < 0 || berettaMask[c].empty()) return;
        eo->leftMask = &berettaMask[c];
        eo->leftColours = &berettaColours[c];
    };
    {
        progress.info("models: Saturn-only creatures and alternate outfits -> PC .EMD");
        for (const Model& m : kModels) {
            if (progress.isCancelled()) {
                if (error) *error = "cancelled";
                return false;
            }
            progress.phase(done++, total, m.disc);
            std::vector<uint8_t> src, pc;
            if (!readDiscFile(img, "ENEMY", m.disc, &src, error)) return false;
            SaturnEmdInfo info;
            SaturnEmdOptions eo;
            eo.weaponPage = m.weapon;
            if (!flameMask.empty() && isJillPlayerModel(m.disc)) {
                eo.donorEmd = &chrisEmd;
                eo.donorMask = &flameMask;
            }
            if (m.weapon) setBeretta(m.disc, &eo);
            if (!convertSaturnEmd(src, &pc, &info, &err, eo)) {
                if (error) *error = std::string(m.disc) + ": " + err;
                return false;
            }
            const std::string rel = std::string("Enemy/") + m.out;
            if (!writeFile(joinPath(overlay, rel), pc)) {
                if (error) *error = "cannot write " + joinPath(overlay, rel);
                return false;
            }
            written.push_back(rel);
            char line[256];
            snprintf(line, sizeof(line),
                     "  %s -> %s  %s: %d parts, %d palettes, %d textures -> %d page(s), "
                     "TIM 256x%d, %zu bytes",
                     m.disc, m.out, m.what, info.parts, info.palettes, info.textures,
                     info.pages, info.timHeight, pc.size());
            progress.info(line);
        }
    }

    if (opts.allModels) {
        progress.info(std::string("models: every other enemy, NPC and player model "
                                  "(optional) -> ") +
                      saturnOverlayFolder(region) + "/Enemy");
        size_t bytes = 0, largest = 0;
        for (const DiscEntry* e : allModelList) {
            if (progress.isCancelled()) {
                if (error) *error = "cancelled";
                return false;
            }
            const std::string name = e->path.substr(e->path.rfind('/') + 1);
            progress.phase(done++, total, name);
            std::vector<uint8_t> src, pc;
            if (!img.readFile(*e, &src)) {
                if (error) *error = "cannot read " + e->path;
                return false;
            }
            SaturnEmdInfo info;
            SaturnEmdOptions eo;
            // The models a weapon is hung on: CHAR10..13 (the player, Players/
            // W??.EMW) and the NPCs EM1020..EM102E (Players/WS2??.TMD).
            eo.weaponPage = name.compare(0, 4, "CHAR") == 0 || name.compare(0, 5, "EM102") == 0;
            if (!flameMask.empty() && isJillPlayerModel(name)) {
                eo.donorEmd = &chrisEmd;
                eo.donorMask = &flameMask;
            }
            if (eo.weaponPage) setBeretta(name, &eo);
            if (!convertSaturnEmd(src, &pc, &info, &err, eo)) {
                if (error) *error = name + ": " + err;
                return false;
            }
            const std::string rel = "Enemy/" + name;
            if (!writeFile(joinPath(overlay, rel), pc)) {
                if (error) *error = "cannot write " + joinPath(overlay, rel);
                return false;
            }
            written.push_back(rel);
            bytes += pc.size();
            largest = std::max(largest, pc.size());
            char line[200];
            snprintf(line, sizeof(line),
                     "  %s: %d parts, %d palettes, %d textures -> %d page(s), TIM 256x%d, "
                     "%zu bytes",
                     name.c_str(), info.parts, info.palettes, info.textures, info.pages,
                     info.timHeight, pc.size());
            progress.info(line);
        }
        if (!flameEmw.empty()) {
            const std::string rel = "Players/W15.EMW";
            if (!makeDirs(joinPath(overlay, "Players")) ||
                !writeFile(joinPath(overlay, rel), flameEmw)) {
                if (error) *error = "cannot write " + joinPath(overlay, rel);
                return false;
            }
            written.push_back(rel);
            progress.info("  " + rel + ": Jill's flamethrower, reading Chris's texels from the "
                                       "right half of her page");
        }
        progress.info("  " + std::to_string(allModelList.size()) + " model(s), " +
                      std::to_string(bytes / 1024) + " KB; largest " +
                      std::to_string(largest / 1024) + " KB");
    }

    {
        progress.info("rooms: Battle Game arenas (STAGE8) -> decoded RDTs");
        size_t relabelled = 0;
        for (const char* name : kArenas) {
            if (progress.isCancelled()) {
                if (error) *error = "cancelled";
                return false;
            }
            progress.phase(done++, total, name);
            std::vector<uint8_t> src, rdt;
            if (!readDiscFile(img, "STAGE8", name, &src, error)) return false;
            size_t used = 0;
            if (!saturnLz77Decode(src.data(), src.size(), &rdt, &used, &err)) {
                if (error) *error = std::string(name) + ": " + err;
                return false;
            }
            // PS1 art keys holes by colour 0x0000; the PC engine by index 0.
            int moved = 0;
            for (const auto& blk : iterTimBlocks(rdt))
                moved += foldTimTransparency(&rdt, blk);
            relabelled += moved;
            const std::string rel = std::string("Stage8/") + name;
            if (!writeFile(joinPath(overlay, rel), rdt)) {
                if (error) *error = "cannot write " + joinPath(overlay, rel);
                return false;
            }
            written.push_back(rel);
            progress.info("  " + std::string(name) + ": " +
                          std::to_string(src.size()) + " -> " +
                          std::to_string(rdt.size()) + " bytes");
        }
        progress.info("  " + std::to_string(relabelled) +
                      " texel(s) moved onto the index-0 transparency key");
    }

    {
        progress.info("rooms: the Laboratory Tyrant room's backgrounds -> PC paks");
        for (const RoomBg& bg : kRoomBgs) {
            if (progress.isCancelled()) {
                if (error) *error = "cancelled";
                return false;
            }
            progress.phase(done++, total, bg.bss);
            std::vector<uint8_t> src;
            if (!readDiscFile(img, bg.folder, bg.bss, &src, error)) return false;
            std::vector<SaturnBssFrame> frames;
            if (!saturnBssDecode(src.data(), src.size(), &frames, &err)) {
                if (error) *error = std::string(bg.bss) + ": " + err;
                return false;
            }
            const std::string dir =
                joinPath(overlay, std::string("Stage") + bg.stageDigit);
            if (!makeDirs(dir)) {
                if (error) *error = "cannot create " + dir;
                return false;
            }
            progress.info(std::string("  ") + bg.bss + ": " + bg.why);
            for (const SaturnBssFrame& f : frames) {
                const std::string name = pakName(bg.stageDigit, bg.roomId, f.index);
                // Same PC writer the DC's arrange backgrounds go through: a
                // 320x240 16 bpp TIM (vram 0,240) inside the game's LZW.
                const std::vector<uint8_t> tim =
                    makeBackgroundTim(backgroundPixels(bg.stageDigit, bg.roomId, f));
                const std::vector<uint8_t> pak = lzwPack(tim);
                const std::string rel =
                    std::string("Stage") + bg.stageDigit + "/" + name;
                if (!writeFile(joinPath(overlay, rel), pak)) {
                    if (error) *error = "cannot write " + joinPath(overlay, rel);
                    return false;
                }
                written.push_back(rel);
                progress.info("  " + name + ": " + std::to_string(src.size()) +
                              " B .BSS -> " + std::to_string(pak.size()) +
                              " B pak (camera " + std::to_string(f.index) + ")");
            }
        }
        for (const RoomRdt& r : kRoomRdts) {
            if (progress.isCancelled()) {
                if (error) *error = "cancelled";
                return false;
            }
            progress.phase(done++, total, r.rdt);
            std::vector<uint8_t> packed;
            if (!readDiscFile(img, r.folder, r.rdt, &packed, error)) return false;
            std::vector<uint8_t> rdt;
            std::string why;
            if (!saturnLz77Decode(packed.data(), packed.size(), &rdt, nullptr, &why)) {
                if (error) *error = std::string(r.rdt) + ": " + why;
                return false;
            }
            // A PC .RDT is a plain file - LoadFile reads it verbatim - so this is
            // the Saturn's room data with nothing to relabel, unlike the arenas.
            //
            // The Laboratory Tyrant room also gets the Saturn's model for omodel
            // slot 0, ENEMY/OB80.DAT - the dark cover over the second capsule.
            // The Saturn draws no room-file TMD at all: it loads OBnn.DAT per
            // slot (GAME4 table 0x060980F4, row for this room 50 00 00 00 12 5A
            // 03) and places each with a hardcoded transform (0x060681E8). OB80's
            // cases: position (-4, 1.5, 0) at 0x06068616, scale (24, 44, 64) at
            // 0x06068878, rotation (0, 0x2580, 0) at 0x06068A00 - the same values
            // read back from the running game. Slots 1-3 get no Saturn model at
            // all; the engine skips them (saturn_omodel_has_no_model).
            if (r.roomId == 0x13 && r.stageDigit == '5') {
                std::vector<uint8_t> ob, tmd;
                if (!readDiscFile(img, "ENEMY", "OB80.DAT", &ob, error)) return false;
                const SaturnObTransform xf = {{-4.0, 1.5, 0.0}, {0, 0x2580, 0}, {24.0, 44.0, 64.0}};
                SaturnObInfo oi;
                // cba 0x7800 / tsb 0x0080 are the room TMDs' own relative
                // addressing (CLUT row 0x1E0, page 0, 8 bpp); the engine adds the
                // texture bank the TIM lands in (ProcessTmdTextures).
                if (!convertSaturnObToTmd(ob, xf, 4, 4, 0x7800, 0x0080, &tmd, &oi, &why)) {
                    if (error) *error = "OB80.DAT: " + why;
                    return false;
                }
                const std::vector<uint8_t> tim = makeFlatTim8(oi.colour, 0, 480, 0, 0);
                if (!patchRdtOmodel(&rdt, 0, tmd, tim, &why)) {
                    if (error) *error = std::string(r.rdt) + ": " + why;
                    return false;
                }
                char line[200];
                std::snprintf(line, sizeof(line),
                              "  OB80.DAT -> omodel slot 0: %d points, %d polygons -> %d "
                              "triangles, colour %04X",
                              oi.points, oi.polygons, oi.triangles, oi.colour);
                progress.info(line);
            }
            const std::string dir =
                joinPath(overlay, std::string("Stage") + r.stageDigit);
            if (!makeDirs(dir)) {
                if (error) *error = "cannot create " + dir;
                return false;
            }
            progress.info(std::string("  ") + r.rdt + ": " + r.why);
            // One file per character variant, named as the disc names it.
            {
                const char* name = r.rdt;
                const std::string rel =
                    std::string("Stage") + r.stageDigit + "/" + name;
                if (!writeFile(joinPath(overlay, rel), rdt)) {
                    if (error) *error = "cannot write " + joinPath(overlay, rel);
                    return false;
                }
                written.push_back(rel);
                progress.info(std::string("  ") + name + ": " +
                              std::to_string(static_cast<size_t>(packed.size())) +
                              " B LZ77 -> " +
                              std::to_string(static_cast<size_t>(rdt.size())) + " B rdt");
            }
        }
    }
    {
        progress.info("rooms: the wardrobe (room 1C, stages 1 and 6) -> patched RDTs");
        for (const auto& w : kWardrobeRdts) {
            if (progress.isCancelled()) {
                if (error) *error = "cancelled";
                return false;
            }
            progress.phase(done++, total, w[1]);
            std::vector<uint8_t> packed, rdt;
            if (!readDiscFile(img, w[0], w[1], &packed, error)) return false;
            std::string why;
            if (!saturnLz77Decode(packed.data(), packed.size(), &rdt, nullptr, &why)) {
                if (error) *error = std::string(w[1]) + ": " + why;
                return false;
            }
            // The same PS1 colour-key relabel the "All rooms" pass gives every
            // other room, so the two passes write the same art.
            for (const auto& blk : iterTimBlocks(rdt)) foldTimTransparency(&rdt, blk);
            const size_t before = rdt.size();
            if (!patchSaturnWardrobeRdt(&rdt, region, &why)) {
                if (error) *error = std::string(w[1]) + ": " + why;
                return false;
            }
            const std::string stageRel = std::string("Stage") + w[0][5];
            const std::string dir = joinPath(overlay, stageRel);
            if (!makeDirs(dir)) {
                if (error) *error = "cannot create " + dir;
                return false;
            }
            const std::string rel = stageRel + "/" + w[1];
            if (!writeFile(joinPath(overlay, rel), rdt)) {
                if (error) *error = "cannot write " + joinPath(overlay, rel);
                return false;
            }
            written.push_back(rel);
            progress.info(std::string("  ") + w[1] + ": " + std::to_string(before) + " -> " +
                          std::to_string(rdt.size()) +
                          " B (area 4 = outfit 2, area 5 = outfit 1, Saturn texts as room "
                          "messages 3-5)");
        }
    }
    if (opts.allRooms) {
        progress.info(std::string("rooms: every campaign room and background (optional) -> ") +
                      saturnOverlayFolder(region) + "/Stage1..7");
        // The Laboratory Tyrant room (stage 5, room 0x13) was written above with
        // the Saturn's omodel patch and its own re-drawn backgrounds; the generic
        // copy must not overwrite it.
        std::set<std::string> bssDone;
        int rdtCount = 0, bgCount = 0;
        size_t relabelled = 0;
        for (const DiscRoom& r : allRoomList) {
            if (progress.isCancelled()) {
                if (error) *error = "cancelled";
                return false;
            }
            progress.phase(done++, total, r.rdtName);
            if (r.stageDigit == '5' && r.roomId == 0x13) continue;
            // The wardrobe was written above, patched; keep its backgrounds.
            const bool wardrobe = (r.stageDigit == '1' || r.stageDigit == '6') && r.roomId == 0x1C;
            const std::string stageRel = std::string("Stage") + r.stageDigit;
            const std::string dir = joinPath(overlay, stageRel);
            if (!makeDirs(dir)) {
                if (error) *error = "cannot create " + dir;
                return false;
            }
            // A PC .RDT is a plain file. The Saturn's is the PS1's, LZ77-packed;
            // decoded it is the PS1 room, and like the arenas it needs the PS1
            // colour-key -> PC index-0 transparency relabel (the PC release did the
            // same to its own copies).
            if (!wardrobe) {
                std::vector<uint8_t> packed, rdt;
                if (!img.readFile(*r.rdt, &packed)) {
                    if (error) *error = "cannot read " + r.rdt->path;
                    return false;
                }
                std::string why;
                if (!saturnLz77Decode(packed.data(), packed.size(), &rdt, nullptr, &why)) {
                    if (error) *error = r.rdtName + ": " + why;
                    return false;
                }
                for (const auto& blk : iterTimBlocks(rdt))
                    relabelled += foldTimTransparency(&rdt, blk);
                const std::string rel = stageRel + "/" + r.rdtName;
                if (!writeFile(joinPath(overlay, rel), rdt)) {
                    if (error) *error = "cannot write " + joinPath(overlay, rel);
                    return false;
                }
                written.push_back(rel);
                ++rdtCount;
            }

            // The room's backgrounds, once for both character variants.
            if (!r.bss || !bssDone.insert(r.bssName).second) continue;
            std::vector<uint8_t> bssData;
            if (!img.readFile(*r.bss, &bssData)) {
                if (error) *error = "cannot read " + r.bss->path;
                return false;
            }
            std::vector<SaturnBssFrame> frames;
            if (!saturnBssDecode(bssData.data(), bssData.size(), &frames, &err)) {
                // Stage 3 room 0x11 is the scrapped heliport: a 641-byte stub RDT
                // and a 22 KB placeholder .BSS that is not a container. It is the
                // only one on the disc; skip it, but say so, so a different file
                // failing the same way is noticed.
                progress.info("  " + r.bssName + ": " + err + " (" +
                              std::to_string(bssData.size()) + " B) - no background written");
                continue;
            }
            for (const SaturnBssFrame& f : frames) {
                const std::string name = pakName(r.stageDigit, r.roomId, f.index);
                const std::vector<uint8_t> pak =
                    lzwPack(makeBackgroundTim(backgroundPixels(r.stageDigit, r.roomId, f)));
                const std::string prel = stageRel + "/" + name;
                if (!writeFile(joinPath(overlay, prel), pak)) {
                    if (error) *error = "cannot write " + joinPath(overlay, prel);
                    return false;
                }
                written.push_back(prel);
                ++bgCount;
            }
        }
        progress.info("  " + std::to_string(rdtCount) + " room file(s), " +
                      std::to_string(bgCount) + " background(s); " +
                      std::to_string(relabelled) +
                      " texel(s) moved onto the index-0 transparency key");
    }
    // The Saturn-exclusive outfit as a bonus outfit: the game offers it in
    // every mode once it is unlocked (src/game/Bonus.h), so the converted model
    // goes into the base tree too. EM1061 / EM1161 are already names no other
    // release uses, so the copies keep them.
    progress.info("bonus outfits: the Saturn-exclusive player models -> the base tree");
    copyBonusOutfits(opts.targetRoot, joinPath(overlay, "Enemy"),
                     {{"EM1061.EMD", "EM1061.EMD", "Chris, Saturn outfit"},
                      {"EM1161.EMD", "EM1161.EMD", "Jill, Saturn outfit"}},
                     progress);
    {
        progress.info("music: the Battle Game's own tracks -> rendered from the sequences");
        // The Battle Game's results screens (RANKING.PRG 0x0602B1E4): their
        // background BMP, both fonts and palettes are in /BATTLE.BIN (X68
        // SSCONV sections at 0x0, 0x10000 BMP, 0x30000, 0x34000, 0x35000). The
        // USA module then reads /WAPON3.BIN over BATTLE.BIN's +0x35000 (the
        // Japanese item labels) and /SAVE2.BIN in place of +0x31EB0 (the
        // Japanese yes/no prompts); the Japanese disc has neither file and uses
        // BATTLE.BIN's own. The Japanese results also draw the "BIO HAZARD"
        // logo, an SGL model inside RANKING.PRG itself (0x0603BF54, see
        // tools/saturn_ranking_logo.py), so that module is copied for it. The
        // game reads all of these as they are (src/game/saturn/SaturnBattle.cpp).
        const char* const usaFiles[] = {"BATTLE.BIN", "WAPON3.BIN"};
        const char* const jpnFiles[] = {"BATTLE.BIN", "RANKING.PRG"};
        for (const char* name : region == SaturnRegion::Jpn ? jpnFiles : usaFiles) {
            const DiscEntry* e = img.find(name);
            std::vector<uint8_t> d;
            if (!e || !img.readFile(*e, &d)) {
                if (error) *error = std::string("cannot read /") + name;
                return false;
            }
            const std::string rel = std::string("Data/") + name;
            if (!writeFile(joinPath(overlay, rel), d)) {
                if (error) *error = "cannot write " + joinPath(overlay, rel);
                return false;
            }
            written.push_back(rel);
            progress.info("  " + rel + " (results screens, " + std::to_string(d.size()) + " bytes)");
        }
    }
    {
        progress.info(opts.allBgm
                          ? "audio: the Battle Game's tracks, the Tick's sound effects and "
                            "every campaign BGM -> rendered from the sequences"
                          : "audio: the Battle Game's tracks and the Tick's sound effects -> "
                            "rendered from the sequences");
        progress.phase(done, total, "audio");
        SaturnMusicOptions mo;
        mo.music = true;
        mo.tickSfx = true;
        mo.allBgm = opts.allBgm;
        mo.format = opts.musicFormat;
        mo.oggQuality = opts.oggQuality;
        mo.ffmpegPath = opts.ffmpegPath;
        std::vector<std::string> music;
        if (!migrateSaturnMusic(img, overlay, mo, progress, &music, error)) return false;
        written.insert(written.end(), music.begin(), music.end());
        done += 2 + (opts.allBgm ? 1 : 0);
    }
    if (opts.movies) {
        // The bar counts movies while they encode (minutes of ffmpeg).
        progress.phase(done, total, "movies");
        std::vector<std::string> movies;
        if (!migrateSaturnMovies(img, region, overlay, opts.ffmpegPath, progress, &movies,
                                 error))
            return false;
        written.insert(written.end(), movies.begin(), movies.end());
        ++done;
    }
    progress.phase(total, total, "done");

    progress.info("not migrated yet (no PC asset / no engine support):");
    progress.info(std::string("  ") +
                  (opts.allModels ? "" : "the other enemy / NPC / player models (optional), ") +
                  (opts.allBgm ? "" : "the campaign's Saturn BGM (optional), ") +
                  (opts.movies ? "" : "the MOVIE.CPK FMV (optional), ") +
                  "the Saturn SE banks (SND/*.CDP, *.PCK), X68 SSCONV .BIN banks, "
                  "TITLE.BIN/END.BIN art, ENEMY/A_*.DAT props");

    // Manifest: what this overlay holds, for people and for later passes.
    std::string manifest =
        "SATURN overlay written by the RE1 Asset Migrator from\n  " +
        opts.imagePath + "\n\nFiles:\n";
    for (const auto& w : written) manifest += "  " + w + "\n";
    manifest +=
        "\nEverything else falls back to the base tree. See "
        "docs/SATURN_PORT.md.\n";
    writeFile(joinPath(overlay, "SATURN.TXT"),
              (const uint8_t*)manifest.data(), manifest.size());

    if (opts.verify) {
        progress.info("verify:");
        int bad = 0;
        for (const auto& rel : written) {
            std::vector<uint8_t> d;
            std::string why;
            bool ok = readFile(joinPath(overlay, rel), &d);
            if (!ok) why = "cannot read back";
            else if (rel == "Data/TITLEFNT.BIN") {
                ok = checkTitleFont(d, nullptr);
                if (!ok) why = "not a consistent TITLEFNT.BIN";
            }
            else if (rel.rfind("Enemy/", 0) == 0) ok = checkPcEmd(d, &why);
            else if (rel.rfind("Players/", 0) == 0) {
                ok = d.size() == flameEmw.size();
                if (!ok) why = "not the weapon file that was written";
            }
            else if (rel.find(".pak") != std::string::npos)
                ok = checkBackgroundPak(d, &why);
            else if (rel.rfind("Movie/", 0) == 0) {
                ok = d.size() > 12 && std::memcmp(d.data() + 4, "ftyp", 4) == 0;
                if (!ok) why = "not an MP4 file";
            }
            else if (rel.rfind("Sound/", 0) == 0) {
                ok = d.size() > 44 && (std::memcmp(d.data(), "RIFF", 4) == 0 ||
                                       std::memcmp(d.data(), "OggS", 4) == 0);
                if (!ok) why = "not a WAV / Ogg file";
            }
            // A 4-byte RDT is a real thing: 28 unused rooms (4 in stage 1, 24 in
            // stage 2) are 4-byte placeholders on the PS1 and the PC alike, and
            // decode to exactly that here.
            else if (d.size() < 0x100 && d.size() != 4) { ok = false; why = "too small for an RDT"; }
            if (!ok) {
                ++bad;
                progress.info("  FAIL " + rel + ": " + why);
            }
        }
        if (bad) {
            if (error) *error = std::to_string(bad) + " file(s) failed verification";
            return false;
        }
        progress.info("  " + std::to_string(written.size()) + " file(s) OK");
    }

    // After the verify, which reads the .pak files back through the game's LZW.
    BackgroundFormatOptions bg;
    bg.format = opts.backgroundFormat;
    bg.quality = opts.webpQuality;
    bg.ffmpegPath = opts.ffmpegPath;
    if (!applyBackgroundFormat(overlay, bg, progress, error)) return false;

    progress.info("Saturn overlay complete: " + std::to_string(written.size()) +
                  " file(s) in " + overlay);
    return true;
}

}  // namespace re1
