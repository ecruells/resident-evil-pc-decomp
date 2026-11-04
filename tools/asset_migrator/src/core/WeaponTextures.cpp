// Standalone gun textures for the weapon models; see WeaponTextures.h.
#include "core/WeaponTextures.h"

#include "core/Util.h"

#include <algorithm>
#include <cstdlib>

namespace re1 {

namespace {

constexpr int kPage = 256;

void put16(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back((uint8_t)v);
    b.push_back((uint8_t)(v >> 8));
}
void put32(std::vector<uint8_t>& b, uint32_t v) {
    put16(b, v & 0xFFFF);
    put16(b, v >> 16);
}

// The 8 bpp TIM a PC .EMD ends in (the directory's last dword): pixels,
// width and CLUT row 0.
struct EmdPage {
    const uint8_t* pix = nullptr;
    int w = 0, h = 0;
    std::vector<uint16_t> row0;
};

bool emdPage(const std::vector<uint8_t>& d, EmdPage* out) {
    const size_t n = d.size();
    if (n < 64 || n % 4) return false;
    const uint8_t* e = d.data();
    const size_t tim = readU32(e, n, n - 4);
    if (tim + 20 > n || readU32(e, n, tim) != 0x10 || readU32(e, n, tim + 4) != 9) return false;
    const size_t clen = readU32(e, n, tim + 8);
    if (readU16(e, n, tim + 16) != 256 || readU16(e, n, tim + 18) < 1 || tim + 20 + 512 > n)
        return false;
    const size_t img = tim + 8 + clen;
    if (img + 12 > n) return false;
    out->w = readU16(e, n, img + 8) * 2;
    out->h = readU16(e, n, img + 10);
    if (out->w < 128 || out->h < kPage || img + 12 + (size_t)out->w * out->h > n) return false;
    out->pix = e + img + 12;
    out->row0.resize(256);
    for (int c = 0; c < 256; ++c) out->row0[c] = readU16(e, n, tim + 20 + 2 * (size_t)c);
    return true;
}

// The colour a holder shows at a left-page texel; 0x8000 marks a hole (index 0),
// which no RGB555 colour can equal.
uint16_t colourAt(const EmdPage& p, int x, int y) {
    const uint8_t i = p.pix[(size_t)y * p.w + x];
    return i == 0 ? 0x8000 : (uint16_t)(p.row0[i] & 0x7FFF);
}

// Same picture: both holes, or every channel within 2 steps (re-saved art).
bool sameColour(uint16_t a, uint16_t b) {
    if (a == 0x8000 || b == 0x8000) return a == b;
    for (int s = 0; s < 15; s += 5)
        if (std::abs((int)((a >> s) & 31) - (int)((b >> s) & 31)) > 2) return false;
    return true;
}

// The footprint of a weapon .EMW (its TMD is one object of 0x34000609 packets,
// the file's last dword its offset): each packet's UV box, 128 x 256. False if
// the packets do not all read the left page through CLUT row 0.
bool footprint(const std::vector<uint8_t>& d, std::vector<uint8_t>* mask) {
    const size_t n = d.size();
    if (n < 64) return false;
    const uint8_t* e = d.data();
    const size_t tmd = readU32(e, n, n - 4) & ~3u;
    if (tmd + 12 + 28 > n || readU32(e, n, tmd + 8) != 1) return false;
    const size_t prims = tmd + 12 + readU32(e, n, tmd + 12 + 16);
    const size_t count = readU32(e, n, tmd + 12 + 20);
    if (count == 0 || prims + 28 * count > n) return false;
    mask->assign((size_t)128 * kPage, 0);
    for (size_t i = 0; i < count; ++i) {
        const size_t pk = prims + 28 * i;
        if (readU32(e, n, pk) != 0x34000609) return false;
        const int row = ((readU16(e, n, pk + 6) >> 6) & 0x1FF) - 0x1E0;
        if (row != 0 || (readU16(e, n, pk + 10) & 0x0F) != 0) return false;
        int u0 = 255, u1 = 0, v0 = 255, v1 = 0;
        for (int c = 0; c < 3; ++c) {
            const int u = e[pk + 4 + 4 * c], v = e[pk + 5 + 4 * c];
            if (u > 127) return false;
            u0 = std::min(u0, u);
            u1 = std::max(u1, u);
            v0 = std::min(v0, v);
            v1 = std::max(v1, v);
        }
        for (int y = v0; y <= v1; ++y)
            for (int x = u0; x <= u1; ++x) (*mask)[(size_t)y * 128 + x] = 1;
    }
    return true;
}

// <dir>/<file> without regard to case; "" when absent.
std::string findFile(const std::string& dir, const std::string& file) {
    for (const std::string& f : listDirectory(dir))
        if (toLower(f) == toLower(file)) return joinPath(dir, f);
    return std::string();
}
std::string findDir(const std::string& root, const std::string& dir) {
    for (const std::string& f : listDirectory(root))
        if (toLower(f) == toLower(dir) && isDirectory(joinPath(root, f))) return joinPath(root, f);
    return std::string();
}

struct Character {
    int block;
    const char* holder;
    std::vector<const char*> outfits;  // every other model this character wears
    std::vector<const char*> weapons;  // the weapon files of its block
};

// The weapon table's blocks (src/game/EntityModelLoader.cpp, g_weaponPathTable
// 0x004c1ca8), without the "no weapon" entry, plus the DC's custom Beretta.
// Outfits: the PC's own (EM1030..33), and the bonus copies the DC and Saturn
// tabs put into the base tree (EM1052/53, EM1061/1161); in the DC overlay
// EM1030..33 are the DC's own.
const Character kCharacters[] = {
    {0, "CHAR10.EMD", {"EM1030.EMD", "EM1032.EMD", "EM1052.EMD", "EM1061.EMD"},
     {"W01.EMW", "W02.EMW", "W03.EMW", "W04.EMW", "W05.EMW", "W06.EMW", "W07.EMW",
      "W0B.EMW", "W18.EMW", "W08.EMW", "W0F.EMW"}},
    {1, "CHAR11.EMD", {"EM1031.EMD", "EM1033.EMD", "EM1053.EMD", "EM1161.EMD"},
     {"W11.EMW", "W12.EMW", "W13.EMW", "W14.EMW", "W15.EMW", "W16.EMW", "W17.EMW",
      "W1B.EMW", "W18.EMW", "W08.EMW", "W1F.EMW"}},
};

}  // namespace

bool buildWeaponTexture(const std::vector<uint8_t>& emw, const std::vector<uint8_t>& holderEmd,
                        const std::vector<const std::vector<uint8_t>*>& outfits,
                        bool wholeFootprint, std::vector<uint8_t>* pix,
                        std::vector<uint16_t>* clut) {
    std::vector<uint8_t> foot;
    EmdPage holder;
    if (!footprint(emw, &foot) || !emdPage(holderEmd, &holder)) return false;
    std::vector<EmdPage> others;
    if (!wholeFootprint)
        for (const std::vector<uint8_t>* o : outfits) {
            EmdPage p;
            if (o && emdPage(*o, &p)) others.push_back(p);
        }
    pix->assign((size_t)kPage * kPage, 0);
    for (int y = 0; y < kPage; ++y)
        for (int x = 0; x < 128; ++x) {
            (*pix)[(size_t)y * kPage + x] = holder.pix[(size_t)y * holder.w + x];
            if (!foot[(size_t)y * 128 + x]) continue;
            const uint16_t c = colourAt(holder, x, y);
            bool gun = c != 0x8000;
            for (const EmdPage& o : others)
                if (gun && !sameColour(c, colourAt(o, x, y))) gun = false;
            (*pix)[(size_t)y * kPage + 128 + x] = gun ? 1 : 0;
        }
    *clut = holder.row0;
    return true;
}

int writeWeaponTextures(const std::string& tree, const Progress& progress) {
    const std::string players = findDir(tree, "Players");
    const std::string enemy = findDir(tree, "Enemy");
    if (players.empty() || enemy.empty()) return 0;
    int written = 0;
    for (const Character& ch : kCharacters) {
        for (const char* weapon : ch.weapons) {
            const std::string emwPath = findFile(players, weapon);
            if (emwPath.empty()) continue;
            // Jill's flamethrower is Chris's model: his page, whole footprint.
            const bool flame = ch.block == 1 && std::string(weapon) == "W15.EMW";
            const std::string holderPath = findFile(enemy, flame ? "CHAR10.EMD" : ch.holder);
            std::vector<uint8_t> emw, holder;
            if (holderPath.empty() || !readFile(emwPath, &emw) || !readFile(holderPath, &holder))
                continue;
            std::vector<std::vector<uint8_t>> outfitData;
            for (const char* o : ch.outfits) {
                const std::string p = findFile(enemy, o);
                std::vector<uint8_t> d;
                if (!p.empty() && readFile(p, &d)) outfitData.push_back(std::move(d));
            }
            std::vector<const std::vector<uint8_t>*> outfits;
            for (const auto& d : outfitData) outfits.push_back(&d);
            std::vector<uint8_t> pix;
            std::vector<uint16_t> clut;
            if (!buildWeaponTexture(emw, holder, outfits, flame, &pix, &clut)) {
                progress.info("  " + emwPath + ": not the expected weapon / holder shape - skipped");
                continue;
            }
            size_t gun = 0;
            for (int y = 0; y < kPage; ++y)
                for (int x = 0; x < 128; ++x) gun += pix[(size_t)y * kPage + 128 + x] != 0;

            std::vector<uint8_t> tim;
            put32(tim, 0x10);
            put32(tim, 9);
            put32(tim, 12 + 512);
            put16(tim, 0);
            put16(tim, 0x1E0);
            put16(tim, 256);
            put16(tim, 1);
            for (uint16_t c : clut) put16(tim, c);
            put32(tim, 12 + (uint32_t)pix.size());
            put16(tim, 0);
            put16(tim, 0);
            put16(tim, kPage / 2);
            put16(tim, kPage);
            tim.insert(tim.end(), pix.begin(), pix.end());

            std::string stem = toUpper(baseName(emwPath));
            stem = stem.substr(0, stem.size() - 4);
            const std::string out =
                joinPath(players, stem + "_" + std::to_string(ch.block) + ".TIM");
            if (!writeFile(out, tim)) {
                progress.info("  cannot write " + out);
                continue;
            }
            ++written;
            progress.info("  " + stem + " (" + (ch.block == 0 ? "Chris" : "Jill") + ", " +
                          baseName(holderPath) + ", " + std::to_string(outfits.size() * !flame) +
                          " outfit(s)): " + std::to_string(gun) + " gun texel(s) -> " +
                          baseName(out));
        }
    }
    return written;
}

}  // namespace re1
