#include "core/Migrate.h"

#include "core/Assets.h"
#include "core/DcOverlay.h"
#include "core/DiscImage.h"
#include "core/Jimaku.h"
#include "core/Ps1Audio.h"
#include "core/HdFonts.h"
#include "core/TitleFont.h"
#include "core/Util.h"
#include "core/Video.h"
#include "core/WeaponTextures.h"

#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace re1 {
namespace {

bool ensureTarget(const std::string& targetRoot, std::string* error) {
    if (targetRoot.empty()) {
        if (error) *error = "no target game folder selected";
        return false;
    }
    if (!makeDirs(targetRoot)) {
        if (error) *error = "cannot create the target folder " + targetRoot;
        return false;
    }
    return true;
}

// The directory under `dir` whose name matches `name` case-insensitively, or
// `dir/name` when there is none yet.
std::string childDir(const std::string& dir, const std::string& name) {
    for (const auto& e : listDirectory(dir)) {
        if (toLower(e) == toLower(name) && isDirectory(joinPath(dir, e)))
            return joinPath(dir, e);
    }
    return joinPath(dir, name);
}

// The existing file in `dir` named `stem` + one of `exts`, compared
// case-insensitively; "" when none.
std::string findFile(const std::string& dir, const std::string& stem,
                     const std::vector<std::string>& exts) {
    for (const auto& e : listDirectory(dir)) {
        const std::string low = toLower(e);
        for (const auto& ext : exts) {
            if (low == toLower(stem) + ext && isRegularFile(joinPath(dir, e)))
                return e;
        }
    }
    return std::string();
}

// PS1 movie stem -> the stem the PC FMV table names (tools/str_to_video.py
// PC_ALIASES). The Japanese table already uses the PS1 names. STFC/STFJ keep
// theirs: the PS1 credits look them up as STFC/STFJ, while stfc_r/stfj_r are
// the PC staff rolls with the credits baked in.
std::string pcMovieStem(const std::string& ps1Stem, AssetVersion version) {
    const std::string key = toLower(ps1Stem);
    if (version == AssetVersion::USA) {
        if (key == "oj") return "OU";
        if (key == "pj") return "PU";
        if (key == "ed4") return "EU4";
        if (key == "ed5") return "EU5";
    }
    return toUpper(ps1Stem);
}

bool migratePs1Supplement(const PcMigrationOptions& opts,
                          const std::string& destRoot,
                          const Progress& progress, std::string* error) {
    progress.info("PS1 assets: " + opts.ps1ImagePath);
    DiscImage img;
    std::string err;
    if (!img.open(opts.ps1ImagePath, &err)) {
        if (error) *error = err;
        return false;
    }
    if (!img.findInFolder("DATA", "STAFF.STF") &&
        !img.findInFolder("MOVIE", "STFC.STR")) {
        if (error)
            *error = "the PS1 image has neither DATA/STAFF.STF nor "
                     "MOVIE/STFC.STR; is it a Resident Evil PS1 disc?";
        return false;
    }

    if (opts.ps1Credits) {
        progress.info("PS1 credits: STAFF.STF, STAFF2.STF, BIO.TIM -> Data");
        const std::string dataDir = childDir(destRoot, "Data");
        size_t copied = 0;
        for (const char* name : {"STAFF.STF", "STAFF2.STF", "BIO.TIM"}) {
            if (progress.isCancelled()) {
                if (error) *error = "cancelled";
                return false;
            }
            const DiscEntry* e = img.findInFolder("DATA", name);
            if (!e) {
                progress.info(std::string("  ") + name + " not on the disc, skipped");
                continue;
            }
            std::vector<uint8_t> data;
            if (!img.readFile(*e, &data)) {
                if (error) *error = std::string("cannot read ") + e->path;
                return false;
            }
            // Keep an existing file's spelling so a case-sensitive tree does
            // not end up with two copies.
            const std::string existing = findFile(dataDir, name, {""});
            const std::string out =
                joinPath(dataDir, existing.empty() ? std::string(name) : existing);
            if (!writeFile(out, data)) {
                if (error) *error = "cannot write " + out;
                return false;
            }
            progress.info("  wrote " + out);
            ++copied;
        }
        if (copied < 3)
            progress.info("  warning: the PS1 credits need all three files");
    }

    int planes = 0;
    if (opts.ps1FmvSubtitles) {
        progress.info("PS1 subtitles: JIMAKU*.RGB -> jimaku*.png");
        if (!convertJimakuSubtitles(img, childDir(destRoot, "Data"), progress,
                                    &planes, error))
            return false;
        if (planes > 0 && opts.version != AssetVersion::JPN)
            progress.info("  note: the game only reads the subtitle planes from "
                          "the JPN tree ([Assets] Version=JPN)");
    }

    if (opts.ps1Movies) {
        if (!img.isRaw())
            progress.info("warning: 2048-byte image; the movie audio will be "
                          "degraded, use a raw .bin/.cue");
        progress.info("PS1 movies: STR -> MP4");
        const std::string movieDir = childDir(destRoot, "Movie");
        makeDirs(movieDir);
        size_t converted = 0;
        size_t kept = 0;
        for (const auto& e : img.entries()) {
            if (e.directory) continue;
            if (extensionOf(e.path) != ".str") continue;
            if (toUpper(e.path).find("MOVIE") == std::string::npos) continue;
            if (e.size == 0 || e.size % 2048 != 0) continue;
            if (progress.isCancelled()) {
                if (error) *error = "cancelled";
                return false;
            }
            const std::string ps1Stem = fs::path(e.path).stem().string();
            std::string stem = pcMovieStem(ps1Stem, opts.version);
            const std::string existing =
                findFile(movieDir, stem, {".mp4", ".avi"});
            if (!existing.empty() && !opts.ps1ReplaceMovies) {
                progress.info("  " + ps1Stem + ": the tree has " + existing +
                              ", kept");
                ++kept;
                continue;
            }
            // Overwrite the tree's .mp4 under its own spelling.
            const std::string existingMp4 = findFile(movieDir, stem, {".mp4"});
            if (!existingMp4.empty())
                stem = fs::path(existingMp4).stem().string();
            if (!convertStrMovie(img, e, movieDir, opts.ffmpegPath, progress,
                                 error, stem))
                return false;
            ++converted;
        }
        progress.info("  " + std::to_string(converted) + " movie(s) converted, " +
                      std::to_string(kept) + " kept");
        if (findFile(movieDir, "STFC", {".mp4"}).empty() ||
            findFile(movieDir, "STFJ", {".mp4"}).empty())
            progress.info("  warning: STFC.mp4/STFJ.mp4 missing; the PS1 "
                          "credits need both");
    }

    if (opts.ps1Audio) {
        Ps1AudioOptions ao;
        ao.sfx = opts.ps1AudioSfx;
        ao.voices = opts.ps1AudioVoices;
        ao.bgm = opts.ps1AudioBgm;
        ao.format = opts.ps1AudioOgg ? AudioFormat::Ogg : AudioFormat::Wav;
        ao.oggQuality = opts.ps1AudioOggQuality;
        ao.ffmpegPath = opts.ffmpegPath;
        if (!migratePs1Audio(img, destRoot, ao, progress, error)) return false;
    }

    progress.info("PS1 credits in OG mode: set [Game] Ps1EndingCredits=1 in "
                  "config.ini");
    if (planes > 0)
        progress.info("PS1 FMV subtitles: set [Game] Ps1FmvSubtitles=1 in "
                      "config.ini");
    return true;
}

// The title menu's typeface (core/TitleFont.h) into <tree>/Data. The game
// draws NEW GAME / LOAD GAME / EXTRA / OPTIONS with it in every mode, so it
// belongs to the base tree, not to an overlay.
bool writeTitleFont(const std::string& tree, const Progress& progress,
                    std::string* error) {
    const std::string dataDir = childDir(tree, "Data");
    const std::string path = joinPath(dataDir, "TITLEFNT.BIN");
    const std::vector<uint8_t> font = buildTitleFont();
    if (!makeDirs(dataDir) || !writeFile(path, font)) {
        if (error) *error = "cannot write " + path;
        return false;
    }
    progress.info("title menu font -> " + path + " (" +
                  std::to_string(font.size()) + " bytes)");
    return true;
}

}  // namespace

int copyBonusOutfits(const std::string& targetRoot, const std::string& sourceEnemyDir,
                     const std::vector<BonusOutfitFile>& files,
                     const Progress& progress) {
    int written = 0;
    for (const char* region : {"USA", "JPN"}) {
        const std::string tree = joinPath(targetRoot, region);
        if (!isDirectory(tree)) continue;
        const std::string enemyDir = childDir(tree, "Enemy");
        for (const BonusOutfitFile& f : files) {
            // The source name is matched without regard to case; the
            // destination is written as given.
            std::string src;
            for (const auto& e : listDirectory(sourceEnemyDir)) {
                if (toLower(e) == toLower(f.source)) src = joinPath(sourceEnemyDir, e);
            }
            std::vector<uint8_t> data;
            if (src.empty() || !readFile(src, &data)) {
                progress.info("  bonus outfit " + f.source + " not found in " +
                              sourceEnemyDir + " - skipped");
                continue;
            }
            const std::string dst = joinPath(enemyDir, f.dest);
            if (!makeDirs(enemyDir) || !writeFile(dst, data)) {
                progress.info("  cannot write " + dst);
                continue;
            }
            progress.info("  " + f.source + " -> " + dst + "  (" + f.what + ")");
            ++written;
        }
    }
    return written;
}

bool migratePcAssets(const PcMigrationOptions& opts, const Progress& progress,
                     std::string* error) {
    progress.info("PC asset migration");
    const bool webp = opts.backgroundFormat != BackgroundFormat::Pak;
    if (opts.sourcePath.empty() && opts.ps1ImagePath.empty() && !webp) {
        if (error) *error = "no source selected";
        return false;
    }
    if (!ensureTarget(opts.targetRoot, error)) return false;

    const std::string destRoot =
        joinPath(opts.targetRoot, versionName(opts.version));
    progress.info("target: " + destRoot);
    if (!makeDirs(destRoot)) {
        if (error) *error = "cannot create " + destRoot;
        return false;
    }

    // A PC copy restores the PC release's sounds: first drop what an earlier
    // PS1 audio migration wrote (its .ogg files would otherwise still win over
    // the restored .wav), then the copy puts the originals back.
    if (!opts.sourcePath.empty()) removePs1Audio(destRoot, progress);

    if (opts.sourcePath.empty() && opts.ps1ImagePath.empty()) {
        progress.info("no source: converting the existing tree's backgrounds");
    } else if (opts.sourcePath.empty()) {
        progress.info("no PC source: adding the PS1 assets to the existing tree");
    } else if (opts.sourceIsImage) {
        progress.info("source: " + opts.sourcePath + " (disc image)");
        DiscImage img;
        std::string err;
        if (!img.open(opts.sourcePath, &err)) {
            if (error) *error = err;
            return false;
        }
        progress.info(img.isRaw() ? "image: raw 2352-byte sectors"
                                  : "image: 2048-byte ISO");
        if (!extractAssetsFromImage(img, destRoot, progress, error)) return false;
    } else {
        progress.info("source: " + opts.sourcePath + " (folder)");
        std::string root;
        if (!findAssetRootInDir(opts.sourcePath, &root, error)) return false;
        progress.info("asset root: " + root);
        if (!copyAssetsFromDir(root, destRoot, progress, error)) return false;
    }

    if (opts.convertMovies) {
        progress.info("movies: AVI -> MP4");
        if (!convertPcMovies(childDir(destRoot, "Movie"), opts.ffmpegPath,
                             opts.keepAvi, progress, error))
            return false;
    }

    // Carried by the tool, not read from any disc; written on every run so a
    // tree migrated before the font moved here gets it too.
    if (!writeTitleFont(destRoot, progress, error)) return false;
    // The weapons' gun textures (core/WeaponTextures.h), from the tree's own
    // player models; redone by the DC and Saturn tabs when they add outfits.
    progress.info("weapon textures: the guns' own art -> Players/W??_?.TIM");
    writeWeaponTextures(destRoot, progress);
    // The HD fonts belong to the HD folder; only refreshed here when there is
    // one already, since creating HD/ switches HD textures on.
    if (isDirectory(joinPath(opts.targetRoot, "HD")) &&
        !writeHdFonts(opts.targetRoot, opts.version, progress, error))
        return false;

    // After the AVI conversion, so the "tree already has it" check sees the
    // PC movies' .mp4 as well.
    if (!opts.ps1ImagePath.empty() &&
        !migratePs1Supplement(opts, destRoot, progress, error))
        return false;

    // Last, on the finished tree: a PC copy has just put every .pak back.
    BackgroundFormatOptions bg;
    bg.format = opts.backgroundFormat;
    bg.quality = opts.webpQuality;
    bg.ffmpegPath = opts.ffmpegPath;
    if (!applyBackgroundFormat(destRoot, bg, progress, error)) return false;

    progress.info("done: " + destRoot);
    return true;
}

bool migrateDcAssets(const DcMigrationOptions& opts, const Progress& progress,
                     std::string* error) {
    progress.info("Director's Cut migration");
    if (opts.imagePath.empty()) {
        if (error) *error = "no disc image selected";
        return false;
    }
    if (!ensureTarget(opts.targetRoot, error)) return false;

    DiscImage img;
    std::string err;
    if (!img.open(opts.imagePath, &err)) {
        if (error) *error = err;
        return false;
    }
    if (!img.isRaw())
        progress.info("warning: 2048-byte image; use a raw .bin/.cue so the "
                      "movie audio is intact");
    else
        progress.info("image: raw 2352-byte sectors (CD-XA audio intact)");

    const std::string overlay = joinPath(opts.targetRoot, "DC");
    const std::string baseTree =
        joinPath(opts.targetRoot, versionName(opts.base));

    std::error_code ec;
    const std::string tmp =
        joinPath(fs::temp_directory_path(ec).string(), "re1am_psxdc");
    fs::remove_all(tmp, ec);
    if (!makeDirs(tmp)) {
        if (error) *error = "cannot create " + tmp;
        return false;
    }

    auto cleanup = [&]() { fs::remove_all(tmp, ec); };

    std::vector<std::string> folders = {"DATA",   "ENEMY",   "ITEM_M1",
                                        "ITEM_M2", "PLAYERS"};
    for (char c : std::string("1234567")) folders.push_back(std::string("STAGE") + c);
    for (char c : std::string("89ABCDE")) folders.push_back(std::string("STAGE") + c);

    progress.info("extracting the disc's asset folders...");
    if (!extractFoldersFromImage(img, folders, tmp, progress, error)) {
        cleanup();
        return false;
    }

    DcOverlayOptions o;
    o.sourceDir = tmp;
    o.overlayDir = overlay;
    o.baseTreeDir = baseTree;
    o.baseName = versionName(opts.base);
    o.backgrounds = opts.backgrounds;

    if (!buildDcOverlay(o, progress, error)) {
        cleanup();
        return false;
    }

    // The arrange game's outfits as bonus outfits: the game offers them in
    // every mode once ADVANCED is cleared (src/game/Bonus.h), so it needs them
    // in the base tree. The DC calls them EM1032 / EM1033 - the PC's own
    // alternate outfits' names - hence the copies take names no release uses.
    progress.info("bonus outfits: the arrange game's player models -> the base tree");
    copyBonusOutfits(opts.targetRoot, childDir(overlay, "Enemy"),
                     {{"EM1032.EMD", "EM1052.EMD", "Chris, Director's Cut outfit"},
                      {"EM1033.EMD", "EM1053.EMD", "Jill, Director's Cut outfit"},
                      {"EM1035.EMD", "EM1055.EMD", "Rebecca, Director's Cut outfit"}},
                     progress);
    // The custom Beretta's texture from the DC's own player models, and the
    // base trees' textures again now that they hold the DC outfits too (an
    // outfit's repainted hand must not end up in a gun mask).
    progress.info("weapon textures: the guns' own art -> Players/W??_?.TIM");
    writeWeaponTextures(overlay, progress);
    for (const char* region : {"USA", "JPN"})
        if (isDirectory(joinPath(opts.targetRoot, region)))
            writeWeaponTextures(joinPath(opts.targetRoot, region), progress);

    int planes = 0;
    if (opts.fmvSubtitles) {
        // Into the JPN tree, not the overlay: the subtitle loader reads the
        // compile-time JPN root directly and never consults an overlay.
        const std::string jpnData =
            childDir(joinPath(opts.targetRoot, versionName(AssetVersion::JPN)),
                     "Data");
        progress.info("DC subtitles: JIMAKU*.RGB -> jimaku*.png");
        if (!convertJimakuSubtitles(img, jpnData, progress, &planes, error)) {
            cleanup();
            return false;
        }
        if (planes > 0) {
            progress.info("  the game reads these from the JPN tree's Data "
                          "folder, whatever the base tree is");
            if (opts.base != AssetVersion::JPN)
                progress.info("  note: [Assets] Version=JPN selects the "
                              "Japanese tree, which is where they landed");
        }
    }

    if (opts.convertMovies) {
        progress.info("movies: STR -> MP4");
        const std::string movieOut = joinPath(overlay, "Movie");
        makeDirs(movieOut);
        const std::vector<std::string> pcDirs = {
            joinPath(opts.targetRoot, "USA/Movie"),
            joinPath(opts.targetRoot, "JPN/Movie")};
        size_t converted = 0;
        for (const auto& e : img.entries()) {
            if (e.directory) continue;
            if (extensionOf(e.path) != ".str") continue;
            if (toUpper(e.path).find("MOVIE") == std::string::npos) continue;
            if (e.size == 0 || e.size % 2048 != 0) continue;
            if (progress.isCancelled()) {
                if (error) *error = "cancelled";
                cleanup();
                return false;
            }
            if (!convertStrMovie(img, e, movieOut, opts.ffmpegPath,
                                 progress, error)) {
                cleanup();
                return false;
            }
            ++converted;
        }
        progress.info("  " + std::to_string(converted) + " movie(s) converted");
    }

    if (opts.verify) {
        if (!verifyDcOverlay(o, progress, error)) {
            cleanup();
            return false;
        }
    }

    // After the verify, which reads the .pak files the build just wrote.
    BackgroundFormatOptions bg;
    bg.format = opts.backgroundFormat;
    bg.quality = opts.webpQuality;
    bg.ffmpegPath = opts.ffmpegPath;
    if (!applyBackgroundFormat(overlay, bg, progress, error)) {
        cleanup();
        return false;
    }

    cleanup();
    if (planes > 0)
        progress.info("PS1 FMV subtitles: set [Game] Ps1FmvSubtitles=1 in "
                      "config.ini");
    progress.info("done: " + overlay);
    return true;
}

}  // namespace re1
