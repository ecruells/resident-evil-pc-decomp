// Headless self-test / verification driver for the asset migrator core.
// Console build (not WIN32) so it can be scripted and compared against the
// Python tools.
//
//   re1am_selftest info <image>
//   re1am_selftest dump <image> <path-in-image> <outfile>
//   re1am_selftest bss <image> <path-in-image> <outdir>
//   re1am_selftest strinfo <image> [filter]
//   re1am_selftest strconvert <image> <outdir> <ffmpeg> [filter] [limit]
//   re1am_selftest extract <image> <outdir> [folders-csv]
//   re1am_selftest jimaku <image> <outdir>
//   re1am_selftest pc <source|-> <target> <USA|JPN> [--image] [--movies]
//                     [--ps1 <image>] [--no-ps1-credits] [--no-ps1-subs]
//                     [--no-ps1-movies] [--ps1-replace]
//   re1am_selftest dc <image> <target> <USA|JPN> [--no-bg] [--no-movies]
//                     [--no-subs] [--no-verify]
//   re1am_selftest ps1audio <image> <tree> [--no-sfx] [--no-voices] [--no-bgm]
//                     [--ogg] [--ogg-q N] [--ffmpeg <path>]
//   re1am_selftest bgm <image> <BGM name> <out.wav>
//   re1am_selftest saturn <image> <target> [--all-models] [--all-rooms] [--all-bgm]
//                     [--movies]
//                     [--ogg] [--ogg-q N] [--ffmpeg <path>] [--no-verify]
//   re1am_selftest satcpk <image>   (MOVIE.CPK's FILM chain: offsets, frames)
//   re1am_selftest satcdp <BATTLE.CDP> [segment-outdir]
//   re1am_selftest satemd <saturn.EMD> <out.EMD> [weapon]
//                     (weapon: keep the PS1 page weapon models sample)
//   re1am_selftest satwardrobe <decoded ROOM11Cv.RDT> <out.RDT> [jpn]
//                     (the wardrobe patch alone; compare with
//                     tools/saturn_wardrobe_rdt.py patch)
//   re1am_selftest satsong <SDDRVS.PCK> <music.CDP> <song|all> <out.wav|outdir>
//                     [--dry]   (peak-normalised, for listening)
//   re1am_selftest satkeys <SDDRVS.PCK> <music.CDP> <song>   (key-on registers)
//   re1am_selftest audioatomic (synthetic WAV/Ogg replacement test)
//   re1am_selftest selftest     (asset-migrator core tests)

#include "core/Assets.h"
#include "core/AudioWriter.h"
#include "core/BackgroundWebp.h"
#include "core/HdFonts.h"
#include "core/HdPack.h"
#include "core/Bss.h"
#include "core/DiscImage.h"
#include "core/Jimaku.h"
#include "core/Migrate.h"
#include "core/Ps1Audio.h"
#include "core/SaturnCdp.h"
#include "core/SaturnEmd.h"
#include "core/SaturnMovie.h"
#include "core/SaturnSequenceBank.h"
#include "core/SaturnSound.h"
#include "core/SaturnToneBank.h"
#include "core/SaturnWardrobe.h"
#include "core/TitleFont.h"
#include "core/Util.h"
#include "core/Video.h"
#include "core/WeaponTextures.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace re1;
namespace fs = std::filesystem;

namespace {

re1::Progress consoleProgress() {
    re1::Progress p;
    p.log = [](const std::string& s) { std::printf("%s\n", s.c_str()); };
    p.step = [](int d, int t, const std::string& l) {
        if (!l.empty()) std::printf("  [%d/%d] %s\n", d, t, l.c_str());
        return true;
    };
    p.cancelled = [] { return false; };
    return p;
}

int cmdInfo(const std::string& image, const std::string& filter = "") {
    DiscImage img;
    std::string err;
    if (!img.open(image, &err)) {
        std::printf("ERROR: %s\n", err.c_str());
        return 1;
    }
    std::printf("path : %s\n", img.path().c_str());
    std::printf("raw  : %s\n", img.isRaw() ? "yes (2352)" : "no (2048)");
    std::printf("files: %zu\n", img.entries().size());
    int shown = 0;
    for (const auto& e : img.entries()) {
        if (!filter.empty() &&
            toUpper(e.path).find(toUpper(filter)) == std::string::npos)
            continue;
        if (shown++ >= 40) break;
        std::printf("  %-40s %s %8u\n", e.path.c_str(),
                    e.directory ? "DIR" : "   ", e.size);
    }
    return 0;
}

int cmdDump(const std::string& image, const std::string& path,
            const std::string& out) {
    DiscImage img;
    std::string err;
    if (!img.open(image, &err)) {
        std::printf("ERROR: %s\n", err.c_str());
        return 1;
    }
    const DiscEntry* e = img.find(path);
    if (!e) {
        std::printf("ERROR: %s not in the image\n", path.c_str());
        return 1;
    }
    std::vector<uint8_t> data;
    if (!img.readFile(*e, &data) || !writeFile(out, data)) {
        std::printf("ERROR: cannot write %s\n", out.c_str());
        return 1;
    }
    std::printf("wrote %s (%zu B)\n", out.c_str(), data.size());
    return 0;
}

int cmdBss(const std::string& image, const std::string& path,
           const std::string& outDir) {
    DiscImage img;
    std::string err;
    if (!img.open(image, &err)) {
        std::printf("ERROR: %s\n", err.c_str());
        return 1;
    }
    const DiscEntry* e = img.find(path);
    if (!e) {
        std::printf("ERROR: %s not in the image\n", path.c_str());
        return 1;
    }
    std::vector<uint8_t> data;
    if (!img.readFile(*e, &data)) return 1;
    const std::string stem = fs::path(e->path).stem().string();
    if (stem.size() < 7) {
        std::printf("ERROR: bad BSS name %s\n", stem.c_str());
        return 1;
    }
    auto hex = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    };
    const int hi = hex(stem[5]), lo = hex(stem[6]);
    if (hi < 0 || lo < 0) return 1;
    re1::Progress p = consoleProgress();
    std::vector<std::string> written;
    if (!convertBss(data, stem[4], hi * 16 + lo, outDir, p, &written, &err)) {
        std::printf("ERROR: %s\n", err.c_str());
        return 1;
    }
    for (const auto& w : written)
        std::printf("wrote %s\n", (fs::path(outDir) / w).string().c_str());
    return 0;
}

int cmdStrInfo(const std::string& image, const std::string& filter) {
    DiscImage img;
    std::string err;
    if (!img.open(image, &err)) {
        std::printf("ERROR: %s\n", err.c_str());
        return 1;
    }
    int n = 0;
    for (const auto& e : img.entries()) {
        if (e.directory || extensionOf(e.path) != ".str") continue;
        if (!filter.empty() &&
            toUpper(e.path).find(toUpper(filter)) == std::string::npos)
            continue;
        Movie mov;
        if (!strParseFromDisc(img, e, &mov, &err)) {
            std::printf("ERROR: %s\n", err.c_str());
            return 1;
        }
        std::printf("%-24s frames=%-5zu %dx%d v%d audio=%zu sectors=%zu video_end=%zu%s\n",
                    fs::path(e.path).stem().string().c_str(), mov.frames.size(),
                    mov.width, mov.height, mov.version, mov.audio.size(),
                    mov.sectorCount, mov.videoSectorEnd,
                    mov.truncatedAudio ? " TRUNCATED" : "");
        ++n;
    }
    std::printf("%d movie(s)\n", n);
    return 0;
}

int cmdStrConvert(const std::string& image, const std::string& outDir,
                  const std::string& ffmpeg, const std::string& filter,
                  int limit) {
    DiscImage img;
    std::string err;
    if (!img.open(image, &err)) {
        std::printf("ERROR: %s\n", err.c_str());
        return 1;
    }
    re1::Progress p = consoleProgress();
    int n = 0;
    for (const auto& e : img.entries()) {
        if (e.directory || extensionOf(e.path) != ".str") continue;
        if (!filter.empty() &&
            toUpper(e.path).find(toUpper(filter)) == std::string::npos)
            continue;
        if (limit > 0 && n >= limit) break;
        if (!convertStrMovie(img, e, outDir, ffmpeg, p, &err)) {
            std::printf("ERROR: %s\n", err.c_str());
            return 1;
        }
        ++n;
    }
    return 0;
}

int cmdExtract(const std::string& image, const std::string& outDir,
               const std::string& foldersCsv) {
    DiscImage img;
    std::string err;
    if (!img.open(image, &err)) {
        std::printf("ERROR: %s\n", err.c_str());
        return 1;
    }
    re1::Progress p = consoleProgress();
    if (foldersCsv.empty()) {
        return extractAssetsFromImage(img, outDir, p, &err) ? 0 : 1;
    }
    std::vector<std::string> folders;
    std::string cur;
    for (char c : foldersCsv) {
        if (c == ',') {
            if (!cur.empty()) folders.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) folders.push_back(cur);
    if (!extractFoldersFromImage(img, folders, outDir, p, &err)) {
        std::printf("ERROR: %s\n", err.c_str());
        return 1;
    }
    return 0;
}

int cmdJimaku(const std::string& image, const std::string& outDir) {
    DiscImage img;
    std::string err;
    if (!img.open(image, &err)) {
        std::printf("ERROR: %s\n", err.c_str());
        return 1;
    }
    re1::Progress p = consoleProgress();
    int written = 0;
    if (!makeDirs(outDir) ||
        !convertJimakuSubtitles(img, outDir, p, &written, &err)) {
        std::printf("ERROR: %s\n", err.empty() ? "cannot create the output "
                                               "directory" : err.c_str());
        return 1;
    }
    std::printf("%d subtitle plane(s)\n", written);
    return written > 0 ? 0 : 1;
}

// "--webp" (lossless) and "--webp-q N" (lossy at quality N): the format the
// migration leaves its room backgrounds in.
void parseBackgroundArg(int argc, char** argv, int* i, BackgroundFormat* format,
                        int* quality) {
    if (std::strcmp(argv[*i], "--webp") == 0) {
        *format = BackgroundFormat::WebpLossless;
    } else if (std::strcmp(argv[*i], "--webp-q") == 0 && *i + 1 < argc) {
        *format = BackgroundFormat::WebpLossy;
        *quality = std::atoi(argv[++*i]);
    }
}

// Convert the backgrounds of an existing tree in place, or with "check" only
// decode every RC*.webp in it and report the sizes.
int cmdBgWebp(int argc, char** argv) {
    const std::string tree = argv[2];
    re1::Progress p = consoleProgress();
    std::string err;
    if (argc >= 4 && std::strcmp(argv[3], "check") == 0) {
        int count = 0, bad = 0;
        for (const auto& d : listDirectory(tree)) {
            const std::string dir = joinPath(tree, d);
            if (toLower(d).rfind("stage", 0) != 0 || !isDirectory(dir)) continue;
            for (const auto& f : listDirectory(dir)) {
                if (extensionOf(f) != ".webp") continue;
                std::vector<uint8_t> file, rgba;
                int w = 0, h = 0;
                ++count;
                if (!readFile(joinPath(dir, f), &file) ||
                    !decodeWebp(file, &rgba, &w, &h)) {
                    std::printf("FAIL %s/%s\n", d.c_str(), f.c_str());
                    ++bad;
                } else if (w != kBackgroundWidth || h != kBackgroundHeight) {
                    std::printf("  %s/%s is %dx%d\n", d.c_str(), f.c_str(), w, h);
                }
            }
        }
        std::printf("%d .webp background(s), %d undecodable\n", count, bad);
        return bad ? 1 : 0;
    }
    BackgroundFormatOptions o;
    o.format = BackgroundFormat::WebpLossless;
    for (int i = 3; i < argc; ++i) {
        parseBackgroundArg(argc, argv, &i, &o.format, &o.quality);
        if (std::strcmp(argv[i], "--pak") == 0) o.format = BackgroundFormat::Pak;
        if (std::strcmp(argv[i], "--ffmpeg") == 0 && i + 1 < argc) o.ffmpegPath = argv[++i];
    }
    if (!applyBackgroundFormat(tree, o, p, &err)) {
        std::printf("ERROR: %s\n", err.c_str());
        return 1;
    }
    return 0;
}

// Import a Classic REbirth HD pack into <target>/HD.
int cmdHdPack(int argc, char** argv) {
    HdPackOptions o;
    o.packDir = argv[2];
    o.targetRoot = argv[3];
    o.tree = (argc >= 5 && std::string(argv[4]) == "JPN") ? AssetVersion::JPN
                                                          : AssetVersion::USA;
    for (int i = 4; i < argc; ++i) {
        if (std::strcmp(argv[i], "--ffmpeg") == 0 && i + 1 < argc) o.ffmpegPath = argv[++i];
        // The hires folder is the Director's Cut set (bio1dc\hires).
        if (std::strcmp(argv[i], "--dc") == 0) o.game = HdPackGame::DirectorsCut;
    }
    re1::Progress p = consoleProgress();
    p.step = [](int, int, const std::string&) { return true; };
    std::string err;
    if (!importHdPack(o, p, &err)) {
        std::printf("ERROR: %s\n", err.c_str());
        return 1;
    }
    return 0;
}

// Copy the Seamless HD Project's movie folder into <target>/<tree>/Movie.
int cmdHdMovies(int argc, char** argv) {
    HdMovieOptions o;
    o.movieDir = argv[2];
    o.targetRoot = argv[3];
    o.tree = (argc >= 5 && std::string(argv[4]) == "JPN") ? AssetVersion::JPN
                                                          : AssetVersion::USA;
    for (int i = 4; i + 1 < argc; ++i) {
        const std::string v = argv[i + 1];
        if (std::strcmp(argv[i], "--capcom") == 0)
            o.capcom = v == "cubes" ? HdCapcomIntro::Cubes : HdCapcomIntro::Original;
        if (std::strcmp(argv[i], "--dm7") == 0)
            o.dm7 = v == "jill" ? HdDm7Clip::JillScene : HdDm7Clip::Original;
    }
    re1::Progress p = consoleProgress();
    p.step = [](int, int, const std::string&) { return true; };
    std::string err;
    if (!importHdMovies(o, p, &err)) {
        std::printf("ERROR: %s\n", err.c_str());
        return 1;
    }
    return 0;
}

int cmdPc(int argc, char** argv) {
    PcMigrationOptions o;
    o.sourcePath = std::strcmp(argv[2], "-") == 0 ? "" : argv[2];
    o.targetRoot = argv[3];
    o.version = std::string(argv[4]) == "JPN" ? AssetVersion::JPN
                                              : AssetVersion::USA;
    for (int i = 5; i < argc; ++i) {
        if (std::strcmp(argv[i], "--image") == 0) o.sourceIsImage = true;
        if (std::strcmp(argv[i], "--movies") == 0) o.convertMovies = true;
        if (std::strcmp(argv[i], "--ps1") == 0 && i + 1 < argc)
            o.ps1ImagePath = argv[++i];
        if (std::strcmp(argv[i], "--no-ps1-credits") == 0) o.ps1Credits = false;
        if (std::strcmp(argv[i], "--no-ps1-subs") == 0)
            o.ps1FmvSubtitles = false;
        if (std::strcmp(argv[i], "--no-ps1-movies") == 0) o.ps1Movies = false;
        if (std::strcmp(argv[i], "--ps1-replace") == 0) o.ps1ReplaceMovies = true;
        if (std::strcmp(argv[i], "--ps1-audio") == 0) o.ps1Audio = true;
        if (std::strcmp(argv[i], "--ps1-ogg") == 0) o.ps1AudioOgg = true;
        if (std::strcmp(argv[i], "--ffmpeg") == 0 && i + 1 < argc) o.ffmpegPath = argv[++i];
        parseBackgroundArg(argc, argv, &i, &o.backgroundFormat, &o.webpQuality);
    }
    re1::Progress p = consoleProgress();
    std::string err;
    if (!migratePcAssets(o, p, &err)) {
        std::printf("ERROR: %s\n", err.c_str());
        return 1;
    }
    return 0;
}

int cmdDc(int argc, char** argv) {
    DcMigrationOptions o;
    o.imagePath = argv[2];
    o.targetRoot = argv[3];
    o.base = std::string(argv[4]) == "JPN" ? AssetVersion::JPN
                                           : AssetVersion::USA;
    for (int i = 5; i < argc; ++i) {
        if (std::strcmp(argv[i], "--no-bg") == 0)
            o.backgrounds = Backgrounds::None;
        if (std::strcmp(argv[i], "--no-movies") == 0) o.convertMovies = false;
        if (std::strcmp(argv[i], "--no-subs") == 0) o.fmvSubtitles = false;
        if (std::strcmp(argv[i], "--no-verify") == 0) o.verify = false;
        if (std::strcmp(argv[i], "--ffmpeg") == 0 && i + 1 < argc) o.ffmpegPath = argv[++i];
        parseBackgroundArg(argc, argv, &i, &o.backgroundFormat, &o.webpQuality);
    }
    re1::Progress p = consoleProgress();
    std::string err;
    if (!migrateDcAssets(o, p, &err)) {
        std::printf("ERROR: %s\n", err.c_str());
        return 1;
    }
    return 0;
}

int cmdSaturn(int argc, char** argv) {
    SaturnMigrationOptions o;
    o.imagePath = argv[2];
    o.targetRoot = argv[3];
    for (int i = 4; i < argc; ++i) {
        if (std::strcmp(argv[i], "--all-models") == 0) o.allModels = true;
        if (std::strcmp(argv[i], "--all-rooms") == 0) o.allRooms = true;
        if (std::strcmp(argv[i], "--all-bgm") == 0) o.allBgm = true;
        if (std::strcmp(argv[i], "--movies") == 0) o.movies = true;
        if (std::strcmp(argv[i], "--ogg") == 0) o.musicFormat = AudioFormat::Ogg;
        if (std::strcmp(argv[i], "--ogg-q") == 0 && i + 1 < argc) o.oggQuality = std::atoi(argv[++i]);
        if (std::strcmp(argv[i], "--ffmpeg") == 0 && i + 1 < argc) o.ffmpegPath = argv[++i];
        if (std::strcmp(argv[i], "--no-verify") == 0) o.verify = false;
        parseBackgroundArg(argc, argv, &i, &o.backgroundFormat, &o.webpQuality);
    }
    re1::Progress p = consoleProgress();
    p.step = [](int, int, const std::string&) { return true; };
    std::string err;
    if (!migrateSaturnAssets(o, p, &err)) {
        std::printf("ERROR: %s\n", err.c_str());
        return 1;
    }
    return 0;
}

// MOVIE.CPK's FILM chain, the way core/SaturnMovie.cpp reads it.
int cmdSatCpk(const char* image) {
    DiscImage img;
    std::string err;
    if (!img.open(image, &err)) {
        std::printf("ERROR: %s\n", err.c_str());
        return 1;
    }
    const DiscEntry* cpk = img.find("MOVIE.CPK");
    std::vector<SaturnCpkMovie> movies;
    if (!cpk || !parseSaturnCpk(img, *cpk, &movies, &err)) {
        std::printf("ERROR: %s\n", cpk ? err.c_str() : "no /MOVIE.CPK");
        return 1;
    }
    for (size_t i = 0; i < movies.size(); ++i) {
        const SaturnCpkMovie& m = movies[i];
        std::printf("%2zu  offset %10u  size %9u  %ux%u  base %3u  %5u frames (%7.3f s)  %4u audio\n",
                    i, m.offset, m.size, m.width, m.height, m.timeBase, m.videoFrames,
                    m.videoFrames / 15.0, m.audioChunks);
    }
    std::printf("%zu movies, MOVIE.CPK %u bytes\n", movies.size(), cpk->size);
    return 0;
}

// The wardrobe patch alone, to compare byte-for-byte with
// `python tools/saturn_wardrobe_rdt.py patch IN OUT`.
int cmdSatWardrobe(const char* in, const char* out, bool jpn) {
    std::vector<uint8_t> rdt;
    if (!readFile(in, &rdt)) {
        std::fprintf(stderr, "cannot read %s\n", in);
        return 1;
    }
    std::string why;
    if (!patchSaturnWardrobeRdt(&rdt, jpn ? SaturnRegion::Jpn : SaturnRegion::Usa, &why)) {
        std::fprintf(stderr, "%s: %s\n", in, why.c_str());
        return 1;
    }
    if (!writeFile(out, rdt)) {
        std::fprintf(stderr, "cannot write %s\n", out);
        return 1;
    }
    std::printf("%s -> %s (%zu bytes)\n", in, out, rdt.size());
    return 0;
}

// One Saturn .EMD -> PC .EMD, to compare byte-for-byte with
// `python tools/saturn_emd.py pc IN OUT`.
int cmdSatEmd(const char* in, const char* out, bool weaponPage) {
    std::vector<uint8_t> src, pc;
    if (!readFile(in, &src)) {
        std::printf("ERROR: cannot read %s\n", in);
        return 1;
    }
    SaturnEmdInfo info;
    SaturnEmdOptions eo;
    eo.weaponPage = weaponPage;
    std::string err;
    if (!convertSaturnEmd(src, &pc, &info, &err, eo)) {
        std::printf("ERROR: %s\n", err.c_str());
        return 1;
    }
    if (!writeFile(out, pc)) {
        std::printf("ERROR: cannot write %s\n", out);
        return 1;
    }
    std::printf("%s: %zu bytes, %d parts, %d palettes, %d textures, %d page(s), "
                "TIM 256x%d\n", out, pc.size(), info.parts, info.palettes,
                info.textures, info.pages, info.timHeight);
    return 0;
}

void putBe32(std::vector<uint8_t>* data, size_t offset, uint32_t value) {
    (*data)[offset] = (uint8_t)(value >> 24);
    (*data)[offset + 1] = (uint8_t)(value >> 16);
    (*data)[offset + 2] = (uint8_t)(value >> 8);
    (*data)[offset + 3] = (uint8_t)value;
}

void putBe16(std::vector<uint8_t>* data, size_t offset, uint16_t value) {
    (*data)[offset] = (uint8_t)(value >> 8);
    (*data)[offset + 1] = (uint8_t)value;
}

bool testSaturnCdpParser() {
    static const char magic[] = "CD_PACK Ver.1.0";
    std::vector<uint8_t> data(0x50, 0);
    std::memcpy(data.data(), magic, sizeof(magic));
    putBe32(&data, 0x10, 0);
    putBe32(&data, 0x14, 0x40);
    putBe32(&data, 0x18, 4);
    putBe32(&data, 0x1C, 0x25A55000);
    putBe32(&data, 0x20, 1);
    putBe32(&data, 0x24, 0x44);
    putBe32(&data, 0x28, 4);
    putBe32(&data, 0x2C, 0x25A7A000);
    putBe32(&data, 0x30, 0xFFFFFFFFu);

    SaturnCdpPack pack;
    std::string error;
    if (!parseSaturnCdpPack(data, &pack, &error) || pack.marker != 0 ||
        pack.segments.size() != 2 || pack.segments[0].sourceOffset != 0x40 ||
        pack.segments[0].sizeBytes != 4 ||
        pack.segments[0].loadAddress != 0x25A55000 ||
        pack.segments[0].continuation != 1 ||
        pack.segments[1].sourceOffset != 0x44 ||
        pack.segments[1].loadAddress != 0x25A7A000 ||
        pack.segments[1].continuation != 0xFFFFFFFFu) {
        return false;
    }

    std::vector<uint8_t> badRange = data;
    putBe32(&badRange, 0x18, 0xFFFFFFFFu);
    if (parseSaturnCdpPack(badRange, &pack, &error)) return false;

    std::vector<uint8_t> unterminated = data;
    putBe32(&unterminated, 0x30, 0);
    if (parseSaturnCdpPack(unterminated, &pack, &error)) return false;

    std::vector<uint8_t> badMagic = data;
    badMagic[0] = 'X';
    return !parseSaturnCdpPack(badMagic, &pack, &error);
}

bool testSaturnToneBankParser() {
    std::vector<uint8_t> data(0x50, 0);
    putBe16(&data, 0, 0x0A);
    putBe16(&data, 2, 0x40);
    putBe16(&data, 8, 0x10);
    data[0x10] = 2;
    data[0x11] = 3;
    data[0x12] = 0;
    data[0x13] = 0xFE;
    data[0x14] = 0;
    data[0x15] = 127;
    putBe32(&data, 0x16, 0x00206660);
    putBe16(&data, 0x1A, 0x1582);
    putBe16(&data, 0x1C, 0x162A);
    putBe32(&data, 0x1E, 0x001F000E);
    data[0x22] = 4;
    data[0x23] = 0x10;
    data[0x2D] = 69;

    SaturnToneBank bank;
    std::string error;
    if (!parseSaturnToneBank(data, &bank, &error) ||
        bank.mixerOffset != 0x0A || bank.velocityCurveOffset != 0x40 ||
        bank.voiceOffsets.size() != 1 || bank.voices.size() != 1 ||
        bank.voices[0].layers.size() != 1) {
        return false;
    }
    const SaturnToneLayer& layer = bank.voices[0].layers[0];
    SaturnToneSampleReference sample;
    if (!resolveSaturnToneSampleReference(layer, 0x55000, 0x800, 0x24196,
                                          &sample, &error)) {
        return false;
    }
    return layer.startKey == 0 && layer.endKey == 127 &&
           layer.sampleReference == 0x00206660 &&
           layer.loopStart == 0x1582 && layer.loopEnd == 0x162A &&
           layer.envelope == 0x001F000E && layer.totalLevel == 0x10 &&
           layer.baseNote == 69 && sample.scspRegisterAddress == 0x25B660 &&
           sample.scspSampleAddress == 0x5B660 && sample.bankOffset == 0x6660 &&
           sample.sourceFileOffset == 0x6E60 &&
           sample.sampleBytesThroughLoopEnd == 0x2C56 && !sample.pcm8Bit &&
           sample.loopMode == 1;
}

bool testSaturnSequenceBankParser() {
    std::vector<uint8_t> data(0x40, 0);
    putBe16(&data, 0, 1);
    putBe32(&data, 2, 6);
    putBe16(&data, 6, 480);
    putBe16(&data, 8, 1);
    putBe16(&data, 10, 0x10);
    putBe16(&data, 12, 0x08);
    putBe32(&data, 0x0E, 4);  // the first tempo event, song + 8
    putBe32(&data, 0x12, 500000);

    SaturnSequenceBank bank;
    std::string error;
    if (!parseSaturnSequenceBank(data, &bank, &error) || bank.songs.size() != 1)
        return false;
    const SaturnSequenceSongHeader& song = bank.songs[0];
    if (song.offset != 6 || song.resolution != 480 || song.tempoEventCount != 1 ||
        song.eventOffset != 0x10 || song.tempoLoopOffset != 0x08 ||
        song.tempos.size() != 1 || song.tempos[0].step != 4 ||
        song.tempos[0].microsecondsPerQuarter != 500000) {
        return false;
    }

    std::vector<uint8_t> badOffset = data;
    putBe32(&badOffset, 2, 0xFFFFFFFFu);
    return !parseSaturnSequenceBank(badOffset, &bank, &error);
}

bool testAudioWriterAtomic() {
    std::error_code ec;
    const fs::path root = fs::temp_directory_path(ec);
    if (ec) return false;
    const fs::path dir = root / ("re1am_audio_atomic_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir, ec);
    if (ec) return false;
    struct Cleanup {
        fs::path path;
        ~Cleanup() {
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }
    } cleanup{dir};

    const std::string wavPath = (dir / "battle.wav").string();
    const std::string oggPath = (dir / "battle.ogg").string();
    const std::vector<uint8_t> oldWav = { 'o', 'l', 'd', '-', 'w', 'a', 'v' };
    const std::vector<uint8_t> oldOgg = { 'o', 'l', 'd', '-', 'o', 'g', 'g' };
    if (!writeFile(wavPath, oldWav) || !writeFile(oggPath, oldOgg)) return false;

    Clip clip;
    clip.channels = 2;
    clip.rate = 44100;
    clip.pcm = { 0, 0, 1200, -1200, -800, 800, 0, 0 };
    clip.loopStart = 1;
    clip.loopEnd = 3;
    std::string error;
    if (writeAudioClipAtomically(clip, AudioFormat::Ogg,
                                 "re1am_missing_ffmpeg_for_atomic_test", 6,
                                 oggPath, wavPath, &error) || error.empty()) {
        return false;
    }
    std::vector<uint8_t> currentWav, currentOgg;
    if (!readFile(wavPath, &currentWav) || !readFile(oggPath, &currentOgg) ||
        currentWav != oldWav || currentOgg != oldOgg) {
        return false;
    }

    if (!writeAudioClipAtomically(clip, AudioFormat::Wav, "", 0,
                                  wavPath, oggPath, &error)) {
        return false;
    }
    if (!readFile(wavPath, &currentWav) || fs::exists(oggPath) ||
        currentWav.size() < 80 || std::memcmp(currentWav.data(), "RIFF", 4) != 0) {
        return false;
    }
    const auto containsTag = [&](const char* tag) {
        return std::search(currentWav.begin(), currentWav.end(), tag, tag + 4) !=
               currentWav.end();
    };
    return containsTag("WAVE") && containsTag("re1p") && containsTag("smpl");
}

int cmdAudioAtomic() {
    if (!testAudioWriterAtomic()) {
        std::printf("ERROR: audio writer atomic-replacement test failed\n");
        return 1;
    }
    std::printf("ok: audio conversion failure preserves old WAV/Ogg; successful WAV switch is marked and looped\n");
    return 0;
}

bool testTitleFont() {
    const std::vector<uint8_t> f = buildTitleFont();
    int glyphs = 0;
    // A-Z, all present, and a damaged copy must be refused.
    if (!checkTitleFont(f, &glyphs) || glyphs != 26) return false;
    std::vector<uint8_t> bad = f;
    bad.pop_back();
    if (checkTitleFont(bad, nullptr)) return false;
    bad = f;
    bad[0] = 'X';
    if (checkTitleFont(bad, nullptr)) return false;
    // The HD fonts (core/HdFonts.h): each a whole TrueType font, refused when
    // cut short or not TrueType.
    const std::vector<uint8_t> fonts[4] = { buildHdTitleFont(), buildHdTextFont(),
                                            buildHdText14Font(AssetVersion::USA),
                                            buildHdText14Font(AssetVersion::JPN) };
    for (int f = 0; f < 4; ++f) {
        const std::vector<uint8_t>& t = fonts[f];
        int tables = 0;
        if (!checkHdFont(t, &tables) || tables < 9) return false;
        bad = t;
        bad.resize(bad.size() / 2);
        if (checkHdFont(bad, nullptr)) return false;
        bad = t;
        bad[0] = 'X';
        if (checkHdFont(bad, nullptr)) return false;
    }
    return std::strcmp(hdTextFontName(AssetVersion::USA), "FONTUS.TTF") == 0 &&
           std::strcmp(hdTextFontName(AssetVersion::JPN), "FONT.TTF") == 0 &&
           std::strcmp(hdText14FontName(AssetVersion::USA), "FONTUS_14.TTF") == 0 &&
           std::strcmp(hdText14FontName(AssetVersion::JPN), "FONT_14.TTF") == 0;
}

int cmdSelfTest() {
    if (!testTitleFont() || !testSaturnCdpParser() || !testSaturnToneBankParser() ||
        !testSaturnSequenceBankParser() || !testAudioWriterAtomic()) {
        std::printf("ERROR: asset-migrator core self-test failed\n");
        return 1;
    }
    std::printf("ok: CD_PACK, tone-bank, sequence-bank and atomic-audio tests\n");
    return 0;
}

int cmdSatCdp(const std::string& path, const std::string& outDir) {
    if (!testSaturnCdpParser() || !testSaturnToneBankParser() ||
        !testSaturnSequenceBankParser()) {
        std::printf("ERROR: Saturn audio parser self-test failed\n");
        return 1;
    }
    std::vector<uint8_t> data;
    if (!readFile(path, &data)) {
        std::printf("ERROR: cannot read %s\n", path.c_str());
        return 1;
    }
    SaturnCdpPack pack;
    std::string error;
    if (!parseSaturnCdpPack(data, &pack, &error)) {
        std::printf("ERROR: %s\n", error.c_str());
        return 1;
    }
    std::printf("CD_PACK marker=%08X, %zu segment(s)\n", pack.marker,
                pack.segments.size());
    bool toneBankFound = false;
    bool sequenceBankFound = false;
    SaturnToneBank battleToneBank;
    SaturnSequenceBank battleSequenceBank;
    if (!outDir.empty() && !makeDirs(outDir)) {
        std::printf("ERROR: cannot create %s\n", outDir.c_str());
        return 1;
    }
    for (size_t i = 0; i < pack.segments.size(); ++i) {
        const SaturnCdpSegment& s = pack.segments[i];
        std::printf("  %02zu source=%08X size=%08X destination=%08X continuation=%08X\n",
                    i, s.sourceOffset, s.sizeBytes, s.loadAddress, s.continuation);
        const std::vector<uint8_t> payload(data.begin() + s.sourceOffset,
                                           data.begin() + s.sourceOffset + s.sizeBytes);
        SaturnToneBank toneBank;
        if (parseSaturnToneBank(payload, &toneBank, &error)) {
            toneBankFound = true;
            std::printf("     tone bank: %zu voice(s), mixer=%04X, velocity=%04X\n",
                        toneBank.voices.size(), toneBank.mixerOffset,
                        toneBank.velocityCurveOffset);
            if (s.loadAddress == 0x25A55000)
                battleToneBank = toneBank;
        }
        SaturnSequenceBank sequenceBank;
        if (parseSaturnSequenceBank(payload, &sequenceBank, &error)) {
            sequenceBankFound = true;
            std::printf("     sequence bank: %zu song(s)\n", sequenceBank.songs.size());
            for (size_t songIndex = 0; songIndex < sequenceBank.songs.size(); ++songIndex) {
                const SaturnSequenceSongHeader& song = sequenceBank.songs[songIndex];
                std::printf("       song %zu: offset=%04zX ppqn=%u tempo events=%u "
                            "events@%04X tempo loop@%04X\n",
                            songIndex, song.offset, song.resolution,
                            song.tempoEventCount, song.eventOffset,
                            song.tempoLoopOffset);
                for (size_t tempoIndex = 0; tempoIndex < song.tempos.size(); ++tempoIndex) {
                    const SaturnSequenceTempoRecord& tempo = song.tempos[tempoIndex];
                    std::printf("         tempo %zu: %u steps, %u us/qn\n",
                                tempoIndex, tempo.step, tempo.microsecondsPerQuarter);
                }
            }
            if (s.loadAddress == 0x25A7A000)
                battleSequenceBank = sequenceBank;
        }
        if (!outDir.empty()) {
            char name[32];
            std::snprintf(name, sizeof(name), "segment_%02zu.bin", i);
            const std::string output = joinPath(outDir, name);
            if (!writeFile(output, data.data() + s.sourceOffset, s.sizeBytes)) {
                std::printf("ERROR: cannot write %s\n", output.c_str());
                return 1;
            }
        }
    }
    if (pack.segments.size() == 2 && pack.segments[0].loadAddress == 0x25A55000 &&
        pack.segments[0].sizeBytes == 0x24196 &&
        pack.segments[1].loadAddress == 0x25A7A000 &&
        pack.segments[1].sizeBytes == 0x14E2) {
        if (!toneBankFound || !sequenceBankFound || battleToneBank.voices.size() != 9 ||
            battleSequenceBank.songs.size() != 2) {
            std::printf("ERROR: Battle tone/sequence bank layout was not recognized\n");
            return 1;
        }
        if (battleToneBank.mixerOffset != 0x1A ||
            battleToneBank.velocityCurveOffset != 0x2C ||
            battleToneBank.voiceOffsets.size() != 9 ||
            battleToneBank.voiceOffsets[1] != 0x68 ||
            battleToneBank.voices[1].layers.size() != 2) {
            std::printf("ERROR: Battle tone-bank directory differs from the recovered layout\n");
            return 1;
        }
        for (size_t layerIndex = 0; layerIndex < 2; ++layerIndex) {
            const SaturnToneLayer& layer = battleToneBank.voices[1].layers[layerIndex];
            const uint8_t expectedStart = layerIndex == 0 ? 0x00 : 0x29;
            const uint8_t expectedEnd = layerIndex == 0 ? 0x28 : 0x7F;
            const uint32_t expectedSample =
                layerIndex == 0 ? 0x00206660 : 0x002092BA;
            const uint16_t expectedLoopStart =
                layerIndex == 0 ? 0x1582 : 0x120D;
            const uint16_t expectedLoopEnd =
                layerIndex == 0 ? 0x162A : 0x1260;
            if (layer.startKey != expectedStart || layer.endKey != expectedEnd ||
                layer.sampleReference != expectedSample ||
                layer.loopStart != expectedLoopStart || layer.loopEnd != expectedLoopEnd) {
                std::printf("ERROR: Battle voice 1 layer %zu differs from the recovered tone zones\n",
                            layerIndex);
                return 1;
            }
            SaturnToneSampleReference sample;
            if (!resolveSaturnToneSampleReference(layer, 0x55000, 0x800, 0x24196,
                                                  &sample, &error)) {
                std::printf("ERROR: %s\n", error.c_str());
                return 1;
            }
            const uint32_t expectedAddress = layerIndex == 0 ? 0x5B660 : 0x5E2BA;
            const uint32_t expectedFileOffset = layerIndex == 0 ? 0x6E60 : 0x9ABA;
            const size_t expectedBytes = layerIndex == 0 ? 0x2C56 : 0x24C2;
            if (sample.scspSampleAddress != expectedAddress ||
                sample.sourceFileOffset != expectedFileOffset ||
                sample.sampleBytesThroughLoopEnd != expectedBytes ||
                sample.pcm8Bit || sample.loopMode != 1) {
                std::printf("ERROR: Battle SCSP sample address/format did not resolve\n");
                return 1;
            }
            std::printf("     Battle layer %zu: keys=%02X..%02X SA=%05X signed 16-bit PCM, "
                        "loop=%04X..%04X, file=%05X bytes-through-LEA=%05X\n",
                        layerIndex, layer.startKey, layer.endKey,
                        sample.scspSampleAddress, layer.loopStart, layer.loopEnd,
                        (unsigned)sample.sourceFileOffset,
                        (unsigned)sample.sampleBytesThroughLoopEnd);
        }
        const SaturnSequenceSongHeader& song0 = battleSequenceBank.songs[0];
        const SaturnSequenceSongHeader& song1 = battleSequenceBank.songs[1];
        if (song0.offset != 0x0A || song0.resolution != 0x180 ||
            song0.tempoEventCount != 2 || song0.eventOffset != 0x18 ||
            song0.tempoLoopOffset != 0x10 || song0.tempos.size() != 2 ||
            song0.tempos[0].step != 4 ||
            song0.tempos[0].microsecondsPerQuarter != 0x000C0BDE ||
            song0.tempos[1].step != 0x2FFC ||
            song0.tempos[1].microsecondsPerQuarter != 0x000C0BDE ||
            song1.offset != 0x0EAE || song1.resolution != 480 ||
            song1.tempoEventCount != 1 || song1.eventOffset != 0x10 ||
            song1.tempoLoopOffset != 0x08 || song1.tempos.size() != 1 ||
            song1.tempos[0].step != 0 ||
            song1.tempos[0].microsecondsPerQuarter != 0x00051615) {
            std::printf("ERROR: Battle sequence-bank headers differ from the recovered layout\n");
            return 1;
        }
        std::printf("     Battle tone/sequence headers match the recovered layout\n");
        constexpr uint32_t kScspRamBase = 0x25A00000;
        size_t layerTotal = 0;
        bool hasPcm8 = false;
        bool hasPcm16 = false;
        bool allSamplesMapped = true;
        for (size_t voiceIndex = 0; voiceIndex < battleToneBank.voices.size();
             ++voiceIndex) {
            const SaturnToneVoice& voice = battleToneBank.voices[voiceIndex];
            std::printf("     voice %02zu: %zu layer(s)\n", voiceIndex,
                        voice.layers.size());
            for (size_t layerIndex = 0; layerIndex < voice.layers.size();
                 ++layerIndex) {
                ++layerTotal;
                const SaturnToneLayer& layer = voice.layers[layerIndex];
                bool mapped = false;
                for (const SaturnCdpSegment& segment : pack.segments) {
                    if (segment.loadAddress < kScspRamBase) continue;
                    SaturnToneSampleReference sample;
                    if (!resolveSaturnToneSampleReference(
                            layer, segment.loadAddress - kScspRamBase,
                            segment.sourceOffset, segment.sizeBytes, &sample,
                            &error)) {
                        continue;
                    }
                    std::printf("       layer %02zu keys=%02X..%02X SA=%05X %u-bit, "
                                "loop=%04X..%04X file=%05X bytes=%05X\n",
                                layerIndex, layer.startKey, layer.endKey,
                                sample.scspSampleAddress,
                                sample.pcm8Bit ? 8u : 16u,
                                layer.loopStart, layer.loopEnd,
                                (unsigned)sample.sourceFileOffset,
                                (unsigned)sample.sampleBytesThroughLoopEnd);
                    hasPcm8 |= sample.pcm8Bit;
                    hasPcm16 |= !sample.pcm8Bit;
                    mapped = true;
                    break;
                }
                if (!mapped) {
                    allSamplesMapped = false;
                    std::printf("       layer %02zu keys=%02X..%02X sampleRef=%08X "
                                "(not in a CDP segment)\n",
                                layerIndex, layer.startKey, layer.endKey,
                                layer.sampleReference);
                }
            }
        }
        if (layerTotal != 15 || !hasPcm8 || !hasPcm16 || !allSamplesMapped) {
            std::printf("ERROR: Battle tone bank did not resolve all 15 8-/16-bit sample layers\n");
            return 1;
        }
        std::printf("     all %zu tone layers map to the CDP blocks (8- and 16-bit PCM)\n",
                    layerTotal);
    }
    return 0;
}

int cmdPs1Audio(int argc, char** argv) {
    DiscImage img;
    std::string err;
    if (!img.open(argv[2], &err)) {
        std::printf("ERROR: %s\n", err.c_str());
        return 1;
    }
    Ps1AudioOptions o;
    for (int i = 4; i < argc; ++i) {
        if (std::strcmp(argv[i], "--no-sfx") == 0) o.sfx = false;
        if (std::strcmp(argv[i], "--no-voices") == 0) o.voices = false;
        if (std::strcmp(argv[i], "--no-bgm") == 0) o.bgm = false;
        if (std::strcmp(argv[i], "--ogg") == 0) o.format = AudioFormat::Ogg;
        if (std::strcmp(argv[i], "--ogg-q") == 0 && i + 1 < argc) o.oggQuality = std::atoi(argv[++i]);
        if (std::strcmp(argv[i], "--ffmpeg") == 0 && i + 1 < argc) o.ffmpegPath = argv[++i];
    }
    re1::Progress p = consoleProgress();
    p.step = [](int, int, const std::string&) { return true; };
    if (!migratePs1Audio(img, argv[3], o, p, &err)) {
        std::printf("ERROR: %s\n", err.c_str());
        return 1;
    }
    return 0;
}

int cmdBgm(const std::string& image, const std::string& name,
           const std::string& out) {
    DiscImage img;
    std::string err;
    if (!img.open(image, &err) || !renderPs1Bgm(img, name, out, &err)) {
        std::printf("ERROR: %s\n", err.c_str());
        return 1;
    }
    return 0;
}

// A peak-normalised 16-bit stereo WAV with a `smpl` loop, for listening to a
// render. The migrator's own writer (SaturnAudio.cpp) levels to the PC mix.
bool writeListeningWav(const std::string& path, const saturn::RenderedSong& s) {
    int32_t peak = 1;
    for (int32_t v : s.mix) peak = std::max(peak, std::abs(v));
    const double gain = 29204.0 / peak;  // -1 dBFS
    std::vector<uint8_t> b;
    auto p16 = [&](uint32_t v) { b.push_back((uint8_t)v); b.push_back((uint8_t)(v >> 8)); };
    auto p32 = [&](uint32_t v) { p16(v & 0xFFFF); p16(v >> 16); };
    auto tag = [&](const char* t) { b.insert(b.end(), t, t + 4); };
    tag("RIFF"); p32(0); tag("WAVE");
    tag("fmt "); p32(16); p16(1); p16(2); p32(s.rate); p32(s.rate * 4); p16(4); p16(16);
    tag("data"); p32((uint32_t)(s.mix.size() * 2));
    for (int32_t v : s.mix) p16((uint16_t)(int16_t)std::lround(v * gain));
    if (s.loopStart >= 0) {
        tag("smpl"); p32(60);
        for (int i = 0; i < 7; ++i) p32(i == 2 ? 22676 : i == 3 ? 60 : 0);
        p32(1); p32(0); p32(0); p32(0);
        p32((uint32_t)s.loopStart); p32((uint32_t)(s.loopEnd - 1)); p32(0); p32(0);
    }
    const uint32_t riff = (uint32_t)(b.size() - 8);
    std::memcpy(b.data() + 4, &riff, 4);
    return writeFile(path, b);
}

int cmdSatSong(int argc, char** argv) {
    std::vector<uint8_t> drvBytes, cdp;
    if (!readFile(argv[2], &drvBytes) || !readFile(argv[3], &cdp)) {
        std::printf("ERROR: cannot read the driver or the pack\n");
        return 1;
    }
    saturn::SoundDriver drv;
    std::string err;
    if (!drv.load(std::move(drvBytes), &err)) {
        std::printf("ERROR: %s\n", err.c_str());
        return 1;
    }
    saturn::RenderOptions o;
    for (int i = 6; i < argc; ++i)
        if (std::strcmp(argv[i], "--dry") == 0)
            std::memset(o.mixer, 0, sizeof(o.mixer));  // no DSP effect returns
    const int count = saturn::saturnSongCount(cdp, &err);
    if (count < 0) {
        std::printf("ERROR: %s\n", err.c_str());
        return 1;
    }
    const bool all = std::strcmp(argv[4], "all") == 0;
    const int first = all ? 0 : std::atoi(argv[4]);
    const int last = all ? count - 1 : first;
    const std::string stem = fs::path(argv[3]).stem().string();
    for (int song = first; song <= last; ++song) {
        saturn::RenderedSong r;
        if (!saturn::renderSaturnSong(drv, cdp, song, o, &r, &err)) {
            std::printf("%s song %d: ERROR %s\n", stem.c_str(), song, err.c_str());
            if (!all) return 1;
            continue;
        }
        const std::string out = all ? (fs::path(argv[5]) / (stem + "_" + std::to_string(song) + ".wav")).string()
                                    : std::string(argv[5]);
        if (!writeListeningWav(out, r)) {
            std::printf("ERROR: cannot write %s\n", out.c_str());
            return 1;
        }
        std::printf("%s song %d: %.2f s, loop %ld..%ld, %d notes (%d dropped) -> %s\n", stem.c_str(),
                    song, r.mix.size() / 2.0 / r.rate, r.loopStart, r.loopEnd, r.notes,
                    r.droppedNotes, out.c_str());
    }
    return 0;
}

// Every distinct key-on register image of a song, one line of 16 hex words,
// for comparing with a live SCSP register dump.
int cmdSatKeys(char** argv) {
    std::vector<uint8_t> drvBytes, cdp;
    if (!readFile(argv[2], &drvBytes) || !readFile(argv[3], &cdp)) {
        std::printf("ERROR: cannot read the driver or the pack\n");
        return 1;
    }
    saturn::SoundDriver drv;
    std::string err;
    if (!drv.load(std::move(drvBytes), &err)) {
        std::printf("ERROR: %s\n", err.c_str());
        return 1;
    }
    std::vector<std::string> seen;
    saturn::RenderOptions o;
    o.onKeyOn = [&](int, const uint16_t* r) {
        char line[128];
        int n = 0;
        for (int i = 0; i < 16; ++i) n += std::snprintf(line + n, sizeof(line) - n, "%04X ", r[i]);
        if (std::find(seen.begin(), seen.end(), line) == seen.end()) seen.push_back(line);
    };
    saturn::RenderedSong r;
    if (!saturn::renderSaturnSong(drv, cdp, std::atoi(argv[4]), o, &r, &err)) {
        std::printf("ERROR: %s\n", err.c_str());
        return 1;
    }
    for (const std::string& l : seen) std::printf("%s\n", l.c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: re1am_selftest <command> ...\n");
        return 2;
    }
    const std::string cmd = argv[1];
    if (cmd == "info" && argc >= 3)
        return cmdInfo(argv[2], argc >= 4 ? argv[3] : "");
    if (cmd == "dump" && argc >= 5) return cmdDump(argv[2], argv[3], argv[4]);
    if (cmd == "bss" && argc >= 5) return cmdBss(argv[2], argv[3], argv[4]);
    if (cmd == "strinfo" && argc >= 3)
        return cmdStrInfo(argv[2], argc >= 4 ? argv[3] : "");
    if (cmd == "strconvert" && argc >= 5)
        return cmdStrConvert(argv[2], argv[3], argv[4],
                             argc >= 6 ? argv[5] : "MOVIE",
                             argc >= 7 ? std::atoi(argv[6]) : 0);
    if (cmd == "extract" && argc >= 4)
        return cmdExtract(argv[2], argv[3], argc >= 5 ? argv[4] : "");
    if (cmd == "jimaku" && argc >= 4) return cmdJimaku(argv[2], argv[3]);
    if (cmd == "pc" && argc >= 5) return cmdPc(argc, argv);
    if (cmd == "bgwebp" && argc >= 3) return cmdBgWebp(argc, argv);
    if (cmd == "hdpack" && argc >= 4) return cmdHdPack(argc, argv);
    if (cmd == "hdmovies" && argc >= 4) return cmdHdMovies(argc, argv);
    if (cmd == "dc" && argc >= 5) return cmdDc(argc, argv);
    if (cmd == "ps1audio" && argc >= 4) return cmdPs1Audio(argc, argv);
    if (cmd == "bgm" && argc >= 5) return cmdBgm(argv[2], argv[3], argv[4]);
    if (cmd == "saturn" && argc >= 4) return cmdSaturn(argc, argv);
    if (cmd == "satcpk" && argc >= 3) return cmdSatCpk(argv[2]);
    if (cmd == "satcdp" && argc >= 3)
        return cmdSatCdp(argv[2], argc >= 4 ? argv[3] : "");
    if (cmd == "audioatomic") return cmdAudioAtomic();
    if (cmd == "weapontex" && argc >= 3) {
        // re1am_selftest weapontex TREE - the weapons' gun textures, written into
        // TREE/Players as the PC / DC / Saturn tabs do (core/WeaponTextures.h).
        Progress p;
        p.log = [](const std::string& s) { std::printf("%s\n", s.c_str()); };
        const int n = writeWeaponTextures(argv[2], p);
        std::printf("%d texture(s)\n", n);
        return n > 0 ? 0 : 1;
    }
    if (cmd == "selftest") return cmdSelfTest();
    if (cmd == "titlefont" && argc >= 3) {
        // re1am_selftest titlefont OUT.BIN - what the PC tab writes as
        // <tree>/Data/TITLEFNT.BIN, for trying the port's title menu without a
        // migration.
        if (!testTitleFont() || !writeFile(argv[2], buildTitleFont())) return 1;
        std::printf("wrote %s\n", argv[2]);
        return 0;
    }
    if (cmd == "hdfonts" && argc >= 3) {
        // re1am_selftest hdfonts TARGET [jpn] - the HD fonts as the HD Pack tab
        // writes them, into TARGET/HD/<USA|JPN>/Data (core/HdFonts.h).
        const AssetVersion tree = (argc >= 4 && std::string(argv[3]) == "jpn")
                                      ? AssetVersion::JPN : AssetVersion::USA;
        std::string error;
        if (!testTitleFont() || !writeHdFonts(argv[2], tree, consoleProgress(), &error)) {
            std::printf("ERROR: %s\n", error.c_str());
            return 1;
        }
        return 0;
    }
    if (cmd == "satemd" && argc >= 4)
        return cmdSatEmd(argv[2], argv[3], argc >= 5 && std::string(argv[4]) == "weapon");
    if (cmd == "satwardrobe" && argc >= 4)
        return cmdSatWardrobe(argv[2], argv[3], argc >= 5 && std::string(argv[4]) == "jpn");
    if (cmd == "satsong" && argc >= 6) return cmdSatSong(argc, argv);
    if (cmd == "satkeys" && argc >= 5) return cmdSatKeys(argv);
    std::printf("unknown or incomplete command\n");
    return 2;
}
