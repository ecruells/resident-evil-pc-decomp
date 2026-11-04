#include "core/SaturnEmd.h"

#include "core/SaturnLz77.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace re1 {

namespace {

constexpr int kPage = 256;

uint32_t be32(const std::vector<uint8_t>& d, size_t o) {
    if (o + 4 > d.size()) return 0;
    return (uint32_t)d[o] << 24 | (uint32_t)d[o + 1] << 16 |
           (uint32_t)d[o + 2] << 8 | d[o + 3];
}
int32_t be32s(const std::vector<uint8_t>& d, size_t o) { return (int32_t)be32(d, o); }
uint16_t be16(const std::vector<uint8_t>& d, size_t o) {
    if (o + 2 > d.size()) return 0;
    return (uint16_t)(d[o] << 8 | d[o + 1]);
}
uint32_t le32(const std::vector<uint8_t>& d, size_t o) {
    return (uint32_t)d[o] | (uint32_t)d[o + 1] << 8 | (uint32_t)d[o + 2] << 16 |
           (uint32_t)d[o + 3] << 24;
}
int16_t le16s(const std::vector<uint8_t>& d, size_t o) {
    return (int16_t)(d[o] | d[o + 1] << 8);
}

void put16(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back((uint8_t)v);
    b.push_back((uint8_t)(v >> 8));
}
void put32(std::vector<uint8_t>& b, uint32_t v) {
    put16(b, v & 0xFFFF);
    put16(b, v >> 16);
}
void pad4(std::vector<uint8_t>& b) {
    while (b.size() % 4) b.push_back(0);
}

// Python's round(): halves go to even. nearbyint does the same in the
// default FE_TONEAREST mode, which is what makes the output byte-identical
// to tools/saturn_emd.py.
int roundEven(double v) { return (int)std::nearbyint(v); }

struct Poly {
    uint16_t v[4];
    uint16_t tex, atrb, pal, dir;
};
struct Part {
    uint32_t offset = 0;
    uint32_t polyTable = 0;
    std::vector<double> pts;  // x,y,z * n, PS1 units
    std::vector<Poly> polys;
};
struct Texture {
    int w, h;
    uint32_t off;
};

// One OBJECT of the skeleton tree: the mesh it draws, the joint id it carries,
// its offset from its parent (PS1 units) and its scale. `cum` is the scale the
// mesh is drawn at: the node's own times every ancestor's, the root's left out.
struct Node {
    int part;
    int jid;
    int parent;  // index into Model::nodes, -1 for a root
    double pos[3];
    double scale[3];
    double cum[3];
    bool root;
};

struct Model {
    std::vector<uint8_t> ps1, mesh, pix;
    std::vector<Node> nodes;
    std::vector<std::vector<uint16_t>> palettes;
    std::vector<Texture> textures;
    std::vector<Part> parts;
    std::map<int, int> jointOf;  // part -> joint id

    int texel(int t, int x, int y) const {
        const Texture& tx = textures[t];
        const uint8_t b = pix[tx.off + (size_t)(y * tx.w + x) / 2];
        return (x & 1) == 0 ? (b >> 4) : (b & 15);
    }
};

bool walk(Model& m, uint32_t o, int depth, int parent, std::string* error) {
    while (o) {
        if (depth > 64 || o + 48 > m.mesh.size()) {
            if (error) *error = "Saturn EMD: bad OBJECT tree";
            return false;
        }
        const uint32_t pat = be32(m.mesh, o);
        const int jid = be16(m.mesh, o + 4);
        const uint32_t child = be32(m.mesh, o + 40);
        const uint32_t next = be32(m.mesh, o + 44);
        int part = -1;
        for (size_t i = 0; i < m.parts.size(); ++i)
            if (m.parts[i].offset == pat) part = (int)i;
        if (part < 0) {
            if (error) *error = "Saturn EMD: OBJECT points at no mesh";
            return false;
        }
        Node n;
        n.part = part;
        n.jid = jid;
        n.parent = parent;
        n.root = depth == 0;
        for (int a = 0; a < 3; ++a) {
            n.pos[a] = be32s(m.mesh, o + 8 + 4 * a) / 65536.0 * 4;
            n.scale[a] = be32s(m.mesh, o + 28 + 4 * a) / 65536.0;
            // SGL scales a node's mesh, its children's offsets and everything
            // below it. The root's scale is the whole creature's size, which is
            // the game's business, not the model's: the Black Tiger (EM1004)
            // carries 0.1 there over a mesh that is the PS1's size as it is.
            n.cum[a] = n.root ? 1.0 : m.nodes[parent].cum[a] * n.scale[a];
        }
        const int self = (int)m.nodes.size();
        m.nodes.push_back(n);
        if (child && !walk(m, child, depth + 1, self, error)) return false;
        o = next;
    }
    return true;
}

// The PS1 skeleton the engine animates with (directory dword 1; 0 on the NPC
// and player files, whose skeleton opens the file): u16 joint count at +4, s16
// x,y,z offset per joint at +8.
bool ps1Skeleton(const std::vector<uint8_t>& ps1, size_t* sk, int* count) {
    if (ps1.size() < 20) return false;
    *sk = le32(ps1, ps1.size() - 20 + 4);
    if (*sk + 8 > ps1.size()) return false;
    *count = (uint16_t)le16s(ps1, *sk + 4);
    return *sk + 8 + 6 * (size_t)*count <= ps1.size();
}

// Each joint's parent in the PS1 armature (u16 offset to it at +0 of the
// skeleton: per joint a u16 child count and a u16 offset to that many child
// ids); -1 for the root.
std::vector<int> ps1Parents(const std::vector<uint8_t>& ps1, size_t sk, int count) {
    std::vector<int> parent(count, -1);
    const size_t arm = sk + (uint16_t)le16s(ps1, sk);
    for (int i = 0; i < count && arm + 4 * (size_t)count <= ps1.size(); ++i) {
        const size_t n = (uint16_t)le16s(ps1, arm + 4 * (size_t)i);
        const size_t at = arm + (uint16_t)le16s(ps1, arm + 4 * (size_t)i + 2);
        for (size_t c = 0; c < n && at + c < ps1.size(); ++c)
            if (ps1[at + c] < count && ps1[at + c] != i) parent[ps1[at + c]] = i;
    }
    return parent;
}

// Three models label their OBJECT nodes with joint ids that are not the PS1's:
//   - the Neptune (EM100B / EM110B) repeats one: its nodes for PS1 joints 2 and
//     5 are labelled 5 and 6, so 6 comes twice and 2 never;
//   - the wasp (EM1007) rotates them: node id j is PS1 joint j - 1, and node 1
//     is joint 9 (each mesh has the bounding box of that PS1 object);
//   - the adder (EM100A) swaps 0 and 1: its tree is rooted at PS1 joint 1, with
//     joint 0 hanging off it.
// In all three the tree itself is right - every node rests where a PS1 joint
// does - so the ids are taken from that. A model whose labels fit, or
// whose offsets fit no PS1 joint at all (the Saturn re-cut several skeletons:
// Barry, Rebecca, Zombie Wesker), keeps its labels.
bool relabelJoints(Model& m, std::string* error) {
    std::set<int> seen;
    bool repeated = false;
    for (const Node& n : m.nodes)
        if (!seen.insert(n.jid).second) repeated = true;

    size_t sk = 0;
    int count = 0;
    if (!ps1Skeleton(m.ps1, &sk, &count)) {
        if (!repeated) return true;
        if (error) *error = "Saturn EMD: PS1 skeleton out of range";
        return false;
    }
    auto miss = [&](const Node& n, int j) {
        double e = 0;
        for (int a = 0; a < 3; ++a)
            e += std::fabs(n.pos[a] - le16s(m.ps1, sk + 8 + 6 * (size_t)j + 2 * a));
        return e;
    };
    // The Saturn's offsets are the PS1's / 4, so a right label is within a few
    // units; the Neptune's two wrong ones are off by 800 and 1100.
    const double kTolerance = 64.0;
    if (!repeated) {
        // Rest position of every PS1 joint (from the PS1 root) and of every
        // node (from the Saturn root).
        const std::vector<int> psParent = ps1Parents(m.ps1, sk, count);
        std::vector<double> P(count * 3, 0.0);
        for (int pass = 0; pass < count; ++pass)
            for (int j = 0; j < count; ++j)
                if (psParent[j] >= 0)
                    for (int a = 0; a < 3; ++a)
                        P[j * 3 + a] = P[psParent[j] * 3 + a] +
                                       le16s(m.ps1, sk + 8 + 6 * (size_t)j + 2 * a);
        std::vector<double> S(m.nodes.size() * 3, 0.0);
        int rootNode = -1;
        for (size_t i = 0; i < m.nodes.size(); ++i) {
            const Node& n = m.nodes[i];
            if (n.jid >= count) return true;
            if (n.root) {
                if (rootNode >= 0) return true;  // two roots: leave it alone
                rootNode = (int)i;
                continue;
            }
            for (int a = 0; a < 3; ++a) S[i * 3 + a] = S[(size_t)n.parent * 3 + a] + n.pos[a];
        }
        if (rootNode < 0) return true;
        // Node i against joint j, with the Saturn root resting on joint r.
        auto gap = [&](size_t i, int j, int r) {
            double e = 0;
            for (int a = 0; a < 3; ++a)
                e += std::fabs(S[i * 3 + a] + P[r * 3 + a] - P[j * 3 + a]);
            return e;
        };
        const int rootLabel = m.nodes[rootNode].jid;
        // Labels that do not fit closely: take the rest positions' word, but
        // only when every node then lands on a joint of its own, closely - a
        // re-cut skeleton (Barry, Rebecca) fits no assignment and keeps its
        // labels. A node keeps its label whenever that fits (two joints can
        // rest on one point).
        const double kTight = 16.0;
        bool off = false;
        for (size_t i = 0; i < m.nodes.size(); ++i)
            if (gap(i, m.nodes[i].jid, rootLabel) > kTight) off = true;
        if (!off) return true;
        for (int t = 0; t < count; ++t) {
            const int r = t == 0 ? rootLabel : (t <= rootLabel ? t - 1 : t);
            std::vector<int> owner(count, -1), pick(m.nodes.size(), -1);
            owner[r] = rootNode;
            pick[rootNode] = r;
            bool fits = true;
            for (size_t i = 0; i < m.nodes.size(); ++i)
                if ((int)i != rootNode && owner[m.nodes[i].jid] < 0 &&
                    gap(i, m.nodes[i].jid, r) <= kTight) {
                    owner[m.nodes[i].jid] = (int)i;
                    pick[i] = m.nodes[i].jid;
                }
            for (size_t i = 0; i < m.nodes.size() && fits; ++i) {
                if (pick[i] >= 0) continue;
                int best = -1;
                for (int j = 0; j < count; ++j)
                    if (owner[j] < 0 && gap(i, j, r) <= kTight &&
                        (best < 0 || gap(i, j, r) < gap(i, best, r)))
                        best = j;
                if (best < 0) {
                    fits = false;
                    break;
                }
                owner[best] = (int)i;
                pick[i] = best;
            }
            if (!fits) continue;
            for (size_t i = 0; i < m.nodes.size(); ++i) m.nodes[i].jid = pick[i];
            return true;
        }
        return true;
    }

    std::vector<int> owner(count, -1);
    std::vector<size_t> loose;
    for (size_t i = 0; i < m.nodes.size(); ++i) {
        const Node& n = m.nodes[i];
        if (n.jid < count && (n.root || miss(n, n.jid) <= kTolerance)) {
            if (owner[n.jid] >= 0) {
                if (error) *error = "Saturn EMD: two meshes on one joint";
                return false;
            }
            owner[n.jid] = (int)i;
        } else {
            loose.push_back(i);
        }
    }
    for (size_t i : loose) {
        int best = -1;
        for (int j = 0; j < count; ++j)
            if (owner[j] < 0 && miss(m.nodes[i], j) <= kTolerance &&
                (best < 0 || miss(m.nodes[i], j) < miss(m.nodes[i], best)))
                best = j;
        if (best < 0) {
            if (error) *error = "Saturn EMD: a mesh fits no PS1 joint";
            return false;
        }
        owner[best] = (int)i;
        m.nodes[i].jid = best;
    }
    return true;
}

// The PS1 section's own texture: an 8 bpp TIM, 128 or 256 pixels wide - one or
// two 128-pixel texture pages side by side - with one CLUT row per page.
struct Ps1Tim {
    int w = 0, h = 0;
    std::vector<std::vector<uint16_t>> rows;
    size_t pix = 0;  // offset of the pixels in the PS1 section
};

bool parsePs1Tim(const std::vector<uint8_t>& ps1, Ps1Tim* t) {
    if (ps1.size() < 20) return false;
    const size_t tim = le32(ps1, ps1.size() - 20 + 16);
    if (tim + 20 > ps1.size() || le32(ps1, tim) != 0x10 || le32(ps1, tim + 4) != 9)
        return false;
    const size_t clen = le32(ps1, tim + 8);
    const int cw = (uint16_t)le16s(ps1, tim + 16), ch = (uint16_t)le16s(ps1, tim + 18);
    if (cw != 256 || ch < 1 || ch > 4 || tim + 20 + 512 * (size_t)ch > ps1.size()) return false;
    for (int r = 0; r < ch; ++r) {
        std::vector<uint16_t> row(256);
        for (int c = 0; c < 256; ++c)
            row[c] = (uint16_t)le16s(ps1, tim + 20 + 512 * (size_t)r + 2 * c);
        t->rows.push_back(row);
    }
    const size_t img = tim + 8 + clen;
    if (img + 12 > ps1.size()) return false;
    t->w = (uint16_t)le16s(ps1, img + 8) * 2;
    t->h = (uint16_t)le16s(ps1, img + 10);
    t->pix = img + 12;
    return t->w > 0 && t->h > 0 && t->pix + (size_t)t->w * t->h <= ps1.size();
}

// Reduce a set of RGB555 colours to at most `limit`, merging the pair whose
// merge costs least (Ward's criterion) until it fits. `slotOf[i]` is the group
// colour i ends in, `out` the groups' colours.
void quantize(const std::vector<std::pair<uint16_t, uint32_t>>& colours, size_t limit,
              std::vector<int>* slotOf, std::vector<uint16_t>* out) {
    struct G {
        double r, g, b, n;
        bool stp;
        bool live;
    };
    std::vector<G> gs;
    std::vector<int> parent(colours.size());
    for (size_t i = 0; i < colours.size(); ++i) {
        const uint16_t c = colours[i].first;
        gs.push_back({(double)(c & 31), (double)(c >> 5 & 31), (double)(c >> 10 & 31),
                      (double)colours[i].second, (c & 0x8000) != 0, true});
        parent[i] = (int)i;
    }
    size_t live = gs.size();
    while (live > limit) {
        int ba = -1, bb = -1;
        double best = 0;
        for (size_t a = 0; a < gs.size(); ++a) {
            if (!gs[a].live) continue;
            for (size_t b = a + 1; b < gs.size(); ++b) {
                if (!gs[b].live) continue;
                const double dr = gs[a].r - gs[b].r, dg = gs[a].g - gs[b].g, db = gs[a].b - gs[b].b;
                const double cost = (dr * dr + dg * dg + db * db) * gs[a].n * gs[b].n /
                                    (gs[a].n + gs[b].n);
                if (ba < 0 || cost < best) {
                    best = cost;
                    ba = (int)a;
                    bb = (int)b;
                }
            }
        }
        G &A = gs[ba], &B = gs[bb];
        const double n = A.n + B.n;
        A.r = (A.r * A.n + B.r * B.n) / n;
        A.g = (A.g * A.n + B.g * B.n) / n;
        A.b = (A.b * A.n + B.b * B.n) / n;
        A.stp = A.stp || B.stp;
        A.n = n;
        B.live = false;
        parent[bb] = ba;
        --live;
    }
    std::vector<int> slot(gs.size(), -1);
    for (size_t i = 0; i < gs.size(); ++i) {
        if (!gs[i].live) continue;
        slot[i] = (int)out->size();
        uint16_t c = (uint16_t)(roundEven(gs[i].r) | roundEven(gs[i].g) << 5 |
                                roundEven(gs[i].b) << 10 | (gs[i].stp ? 0x8000 : 0));
        if (c == 0) c = 1;  // colour 0 is the PS1's transparent key
        out->push_back(c);
    }
    slotOf->resize(colours.size());
    for (size_t i = 0; i < colours.size(); ++i) {
        int g = (int)i;
        while (parent[g] != g) g = parent[g];
        (*slotOf)[i] = slot[g];
    }
}

bool parse(const std::vector<uint8_t>& data, Model* m, std::string* error) {
    if (!isSaturnEmd(data) || data.size() < 16) {
        if (error) *error = "not a Saturn .EMD (no 00000010 tag)";
        return false;
    }
    const uint32_t s1 = be32(data, 4), s2 = be32(data, 8), s3 = be32(data, 12);
    if (!(0x10 < s1 && s1 < s2 && s2 < s3 && s3 <= data.size())) {
        if (error) *error = "Saturn EMD: bad section offsets";
        return false;
    }
    m->ps1.assign(data.begin() + 0x10, data.begin() + s1);
    std::string err;
    if (!saturnLz77Decode(data.data() + s1, s2 - s1, &m->mesh, nullptr, &err) ||
        !saturnLz77Decode(data.data() + s2, s3 - s2, &m->pix, nullptr, &err)) {
        if (error) *error = "Saturn EMD: " + err;
        return false;
    }
    for (size_t p = 0; p + 32 <= data.size() - s3; p += 32) {
        std::vector<uint16_t> lut(16);
        for (int i = 0; i < 16; ++i)
            lut[i] = (uint16_t)(data[s3 + p + 2 * i] << 8 | data[s3 + p + 2 * i + 1]);
        m->palettes.push_back(lut);
    }
    const auto& M = m->mesh;
    const uint32_t nobj = be32(M, 0), npal = be32(M, 4), root = be32(M, 8),
                   tex = be32(M, 12);
    if (npal != m->palettes.size()) {
        if (error) *error = "Saturn EMD: palette count does not match section 3";
        return false;
    }
    for (uint32_t o = tex; o + 8 <= M.size(); o += 8) {
        const uint16_t w = be16(M, o);
        if (w == 0xFFFF) break;
        m->textures.push_back({(int)w, (int)be16(M, o + 2), (uint32_t)be16(M, o + 4) * 8u});
    }
    for (uint32_t i = 0; i < nobj; ++i) {
        Part part;
        part.offset = be32(M, 16 + 4 * i);
        const uint32_t pt = be32(M, part.offset), npt = be32(M, part.offset + 4);
        const uint32_t pl = be32(M, part.offset + 8), npl = be32(M, part.offset + 12);
        const uint32_t at = be32(M, part.offset + 16);
        part.polyTable = pl;
        for (uint32_t k = 0; k < npt; ++k)
            for (int a = 0; a < 3; ++a)
                part.pts.push_back(be32s(M, pt + 12 * k + 4 * a) / 65536.0 * 4);
        for (uint32_t k = 0; k < npl; ++k) {
            Poly q;
            for (int a = 0; a < 4; ++a) q.v[a] = be16(M, pl + 20 * k + 12 + 2 * a);
            q.tex = be16(M, at + 12 * k + 2);
            q.atrb = be16(M, at + 12 * k + 4);
            q.pal = (uint16_t)(be16(M, at + 12 * k + 6) / 4);
            q.dir = be16(M, at + 12 * k + 10);
            if (q.tex >= m->textures.size() || q.pal >= m->palettes.size()) {
                if (error) *error = "Saturn EMD: polygon texture/palette out of range";
                return false;
            }
            for (int a = 0; a < 4; ++a)
                if (q.v[a] >= npt) {
                    if (error) *error = "Saturn EMD: polygon vertex out of range";
                    return false;
                }
            part.polys.push_back(q);
        }
        m->parts.push_back(std::move(part));
    }
    for (const Texture& t : m->textures)
        if (t.off + (size_t)t.w * t.h / 2 > m->pix.size()) {
            if (error) *error = "Saturn EMD: texture data out of range";
            return false;
        }
    if (!walk(*m, root, 0, -1, error)) return false;
    if (!relabelJoints(*m, error)) return false;
    for (const Node& n : m->nodes) m->jointOf[n.part] = n.jid;
    // The creatures' skeletons cover every mesh. The player models (CHAR1x and
    // the EM103x outfits) carry extra meshes the OBJECT tree never reaches - 23
    // meshes against the PS1 skeleton's 15 joints - so only the reached ones are
    // converted; the joint ids must still be 0..N-1 (checked in the convert).
    if (m->jointOf.empty() || m->jointOf.size() > nobj) {
        if (error) *error = "Saturn EMD: skeleton does not cover any mesh";
        return false;
    }
    return true;
}

struct Place {
    int page, x, y;
};

}  // namespace

bool isSaturnEmd(const std::vector<uint8_t>& data) {
    return data.size() >= 4 && data[0] == 0 && data[1] == 0 && data[2] == 0 &&
           data[3] == 0x10;
}

bool convertSaturnEmd(const std::vector<uint8_t>& saturn,
                      std::vector<uint8_t>* out, SaturnEmdInfo* info,
                      std::string* error, const SaturnEmdOptions& opts) {
    Model m;
    if (!parse(saturn, &m, error)) return false;
    if (m.ps1.size() < 20) {
        if (error) *error = "Saturn EMD: no PS1 section";
        return false;
    }
    if (1 + 16 * m.palettes.size() > 256) {
        if (error) *error = "Saturn EMD: palettes do not fit one 256-colour CLUT";
        return false;
    }

    // --- pack the used textures into 256x256 pages (pack_pages) ---
    // Only the meshes the skeleton reaches are converted; the others' textures
    // must not take page space.
    std::vector<const Part*> reached;
    for (size_t i = 0; i < m.parts.size(); ++i)
        if (m.jointOf.count((int)i)) reached.push_back(&m.parts[i]);
    std::set<int> usedSet;
    for (const Part* p : reached)
        for (const Poly& q : p->polys) usedSet.insert(q.tex);
    std::vector<int> used(usedSet.begin(), usedSet.end());
    std::stable_sort(used.begin(), used.end(), [&](int a, int b) {
        const Texture &A = m.textures[a], &B = m.textures[b];
        if (A.h != B.h) return A.h > B.h;
        return A.w > B.w;
    });
    // --- what is kept of the PS1 section's own model ---
    // Two things the Saturn mesh does not replace still sample the PS1 texture:
    //   - a weapon. A Players/W??.EMW (the player's) or WS2??.TMD (an NPC's)
    //     has no texture of its own; its packets (the gun and the hand holding
    //     it) read the left 128x256 page of the holder's TIM through CLUT row 0;
    //   - a PS1 object past the Saturn skeleton's joints, which the Saturn .EMD
    //     has no mesh for: the Tyrant's heart (object 15), Plant 42's bulb and
    //     vines (objects 16, 17). They keep the PS1 geometry.
    // For either, page 0 of the output TIM is the PS1 page (its texels moved to
    // the CLUT entries the Saturn palettes leave free) and the Saturn textures
    // start at page 1. The 8-vertex, 12-primitive box the player and NPC models
    // carry as object 15 is not kept: it stays an empty slot, as before.
    std::map<int, int> partOfJoint;
    for (const auto& kv : m.jointOf) partOfJoint[kv.second] = kv.first;
    const int joints = partOfJoint.empty() ? 0 : partOfJoint.rbegin()->first + 1;
    if (joints > 64) {
        if (error) *error = "Saturn EMD: joint id out of range";
        return false;
    }
    uint32_t psTmd = le32(m.ps1, m.ps1.size() - 20 + 12), psObjs = 0;
    if ((size_t)psTmd + 12 <= m.ps1.size()) psObjs = le32(m.ps1, psTmd + 8);
    // Object k's table entry and data, bounds-checked.
    struct PsObj {
        size_t verts, norms, prims;
        uint32_t nv, nn, np;
    };
    auto psObject = [&](uint32_t k, PsObj* o) {
        const size_t e = psTmd + 12 + 28 * (size_t)k, base = psTmd + 12;
        if (k >= psObjs || e + 28 > m.ps1.size()) return false;
        o->verts = base + le32(m.ps1, e);
        o->nv = le32(m.ps1, e + 4);
        o->norms = base + le32(m.ps1, e + 8);
        o->nn = le32(m.ps1, e + 12);
        o->prims = base + le32(m.ps1, e + 16);
        o->np = le32(m.ps1, e + 20);
        return o->verts + 8 * (size_t)o->nv <= m.ps1.size() &&
               o->norms + 8 * (size_t)o->nn <= m.ps1.size();
    };
    Ps1Tim psTim;
    const bool havePsTim = parsePs1Tim(m.ps1, &psTim) && psTim.h >= kPage &&
                           1 + 16 * m.palettes.size() < 256;
    std::set<uint32_t> kept;  // PS1 objects that keep their geometry
    // PS1 texels wanted on page 0: [y][x], x across both 128-pixel pages; and
    // the CLUT row each page is read through.
    std::vector<uint8_t> want((size_t)kPage * kPage, 0);
    int rowOf[2] = {-1, -1};
    if (havePsTim) {
        for (uint32_t k = (uint32_t)joints; k < psObjs && k < 64; ++k) {
            PsObj o;
            if (!psObject(k, &o) || o.np == 0 || (o.nv == 8 && o.np == 12)) continue;
            if (o.prims + 28 * (size_t)o.np > m.ps1.size()) continue;
            bool plain = true;
            for (uint32_t i = 0; i < o.np && plain; ++i) {
                const size_t pk = o.prims + 28 * (size_t)i;
                const int tp = m.ps1[pk + 10] & 0x0F;
                plain = le32(m.ps1, pk) == 0x34000609 && tp < 2 && 128 * (tp + 1) <= psTim.w;
            }
            if (!plain) continue;
            kept.insert(k);
            for (uint32_t i = 0; i < o.np; ++i) {
                const size_t pk = o.prims + 28 * (size_t)i;
                const int tp = m.ps1[pk + 10] & 0x0F;
                int row = ((uint16_t)le16s(m.ps1, pk + 6) >> 6 & 0x1FF) - 0x1E0;
                if (row < 0 || row >= (int)psTim.rows.size()) row = 0;
                if (rowOf[tp] < 0) rowOf[tp] = row;
                int u0 = 255, u1 = 0, v0 = 255, v1 = 0;
                for (int c = 0; c < 3; ++c) {
                    u0 = std::min<int>(u0, m.ps1[pk + 4 + 4 * c]);
                    u1 = std::max<int>(u1, m.ps1[pk + 4 + 4 * c]);
                    v0 = std::min<int>(v0, m.ps1[pk + 5 + 4 * c]);
                    v1 = std::max<int>(v1, m.ps1[pk + 5 + 4 * c]);
                }
                for (int yy = v0; yy <= v1; ++yy)
                    for (int xx = u0; xx <= std::min(u1, 127); ++xx)
                        want[(size_t)yy * kPage + 128 * tp + xx] = 1;
            }
        }
        if (opts.weaponPage) {
            if (rowOf[0] < 0) rowOf[0] = 0;
            for (int yy = 0; yy < kPage; ++yy)
                for (int xx = 0; xx < 128; ++xx) want[(size_t)yy * kPage + xx] = 1;
        }
    }
    // The donor's page for the right half (SaturnEmdOptions::donorEmd).
    std::vector<uint8_t> donorPs1;
    Ps1Tim donorTim;
    bool donorRight = false;
    if (havePsTim && opts.weaponPage && rowOf[1] < 0 && opts.donorEmd && opts.donorMask &&
        opts.donorMask->size() == (size_t)128 * kPage && isSaturnEmd(*opts.donorEmd) &&
        opts.donorEmd->size() >= 16) {
        const uint32_t s1 = be32(*opts.donorEmd, 4);
        if (s1 > 0x10 && s1 <= opts.donorEmd->size()) {
            donorPs1.assign(opts.donorEmd->begin() + 0x10, opts.donorEmd->begin() + s1);
            if (parsePs1Tim(donorPs1, &donorTim) && donorTim.w >= 128 && donorTim.h >= kPage) {
                donorRight = true;
                for (int yy = 0; yy < kPage; ++yy)
                    for (int xx = 0; xx < 128; ++xx)
                        if ((*opts.donorMask)[(size_t)yy * 128 + xx])
                            want[(size_t)yy * kPage + 128 + xx] = 1;
            }
        }
    }
    // The left-page patch (SaturnEmdOptions::leftMask), inside the page the
    // weapon models read.
    const bool leftPatch = havePsTim && opts.weaponPage && opts.leftMask && opts.leftColours &&
                           opts.leftMask->size() == (size_t)128 * kPage &&
                           opts.leftColours->size() == (size_t)128 * kPage;
    const bool psPage = rowOf[0] >= 0 || rowOf[1] >= 0 || donorRight;

    std::map<int, Place> place;
    int page = psPage ? 1 : 0, x = 0, y = 0, shelf = 0;
    for (int t : used) {
        const int w = m.textures[t].w, h = m.textures[t].h;
        if (w > kPage || h > kPage) {
            if (error) *error = "Saturn EMD: a texture is larger than a page";
            return false;
        }
        if (x + w > kPage) {
            x = 0;
            y = y + shelf + 1;
            shelf = 0;
        }
        if (y + h > kPage) {
            page += 1;
            x = y = shelf = 0;
        }
        place[t] = {page, x, y};
        x += w + 1;
        shelf = std::max(shelf, h);
    }
    const int npages = page + 1;

    // --- TIM (build_tim) ---
    std::vector<uint16_t> clut(256, 0);
    for (size_t k = 0; k < m.palettes.size(); ++k)
        for (int c = 0; c < 16; ++c) clut[1 + 16 * k + c] = m.palettes[k][c] & 0x7FFF;
    int usedRows = psPage ? kPage : 0;
    for (const auto& kv : place)
        usedRows = std::max(usedRows, kv.second.page * kPage + kv.second.y +
                                          m.textures[kv.first].h);
    const int H = npages == 1 ? kPage : (usedRows + 15) / 16 * 16;
    std::vector<uint8_t> img((size_t)kPage * H, 0);
    std::map<int, std::pair<int, int>> tpal;  // tex -> (pal, atrb), first user
    for (const Part* p : reached)
        for (const Poly& q : p->polys)
            if (!tpal.count(q.tex)) tpal[q.tex] = {q.pal, q.atrb};
    for (const auto& kv : place) {
        const int t = kv.first;
        const Place& pl = kv.second;
        const int pal = tpal[t].first;
        const bool keyed = !(tpal[t].second & 0x40);  // SPD clear: texel 0 = hole
        for (int yy = 0; yy < m.textures[t].h; ++yy) {
            const size_t row = (size_t)(pl.page * kPage + pl.y + yy) * kPage + pl.x;
            for (int xx = 0; xx < m.textures[t].w; ++xx) {
                const int i = m.texel(t, xx, yy);
                img[row + xx] = (uint8_t)((keyed && i == 0) ? 0 : 1 + 16 * pal + i);
            }
        }
    }
    if (psPage) {
        // The PS1 page, read through its CLUT row, onto the free CLUT entries.
        std::map<uint16_t, uint32_t> hist;
        auto colourAt = [&](int xx, int yy) -> uint16_t {
            if (xx >= 128 && donorRight) {
                const uint8_t di = donorPs1[donorTim.pix + (size_t)yy * donorTim.w + (xx - 128)];
                return di == 0 ? 0 : donorTim.rows[0][di];
            }
            if (xx < 128 && leftPatch && (*opts.leftMask)[(size_t)yy * 128 + xx])
                return (*opts.leftColours)[(size_t)yy * 128 + xx];
            const uint8_t i = m.ps1[psTim.pix + (size_t)yy * psTim.w + xx];
            // Index 0 is the PC's transparent key, colour 0 the PS1's.
            return i == 0 ? 0 : psTim.rows[rowOf[xx / 128]][i];
        };
        for (int yy = 0; yy < kPage; ++yy)
            for (int xx = 0; xx < kPage; ++xx)
                if (want[(size_t)yy * kPage + xx])
                    if (const uint16_t c = colourAt(xx, yy)) ++hist[c];
        const std::vector<std::pair<uint16_t, uint32_t>> colours(hist.begin(), hist.end());
        const size_t first = 1 + 16 * m.palettes.size();
        std::vector<int> slotOf;
        std::vector<uint16_t> slots;
        quantize(colours, 256 - first, &slotOf, &slots);
        std::map<uint16_t, uint8_t> indexOf;
        for (size_t i = 0; i < colours.size(); ++i)
            indexOf[colours[i].first] = (uint8_t)(first + slotOf[i]);
        for (size_t i = 0; i < slots.size(); ++i) clut[first + i] = slots[i];
        for (int yy = 0; yy < kPage; ++yy)
            for (int xx = 0; xx < kPage; ++xx)
                if (want[(size_t)yy * kPage + xx])
                    if (const uint16_t c = colourAt(xx, yy))
                        img[(size_t)yy * kPage + xx] = indexOf[c];
    }
    std::vector<uint8_t> tim;
    put32(tim, 0x10);
    put32(tim, 9);
    put32(tim, 12 + 512);
    put16(tim, 0);
    put16(tim, 0x1E0);
    put16(tim, 256);
    put16(tim, 1);
    for (uint16_t c : clut) put16(tim, c);
    put32(tim, (uint32_t)(12 + img.size()));
    put16(tim, 0x280);
    put16(tim, 0);
    put16(tim, kPage / 2);
    put16(tim, (uint32_t)H);
    tim.insert(tim.end(), img.begin(), img.end());

    // --- TMD (build_tmd) ---
    // Node scales, by joint: what the joint's mesh is drawn at, and what its
    // offset from its parent is drawn at (the parent's). 1.0 on every model but
    // two: the Neptune's dorsal fin is stored at half size under a x2 node, and
    // Enrico (EM1028) has his torso and legs at x0.935. The node angles a few
    // models carry (CHAR10's feet, the Yawn's joints 2 and 4) are not applied:
    // the animation sets every joint's rotation.
    std::map<int, const Node*> nodeOfJoint;
    for (const Node& n : m.nodes) nodeOfJoint[n.jid] = &n;
    auto unit = [](const double* v) { return v[0] == 1.0 && v[1] == 1.0 && v[2] == 1.0; };
    struct Obj {
        std::vector<uint8_t> verts, norms, prims;
        uint32_t nv = 0, nn = 0, np = 0;
    };
    std::vector<Obj> objs;
    // Object k of the PS1 section's own TMD: its vertices and normals, and
    // either its primitives (moved onto page 0, see above) or none of them.
    // False when the PS1 TMD has no such object.
    auto ps1Object = [&](uint32_t k, Obj* o) {
        PsObj po;
        if (!psObject(k, &po)) return false;
        o->verts.assign(m.ps1.begin() + po.verts, m.ps1.begin() + po.verts + 8 * (size_t)po.nv);
        o->norms.assign(m.ps1.begin() + po.norms, m.ps1.begin() + po.norms + 8 * (size_t)po.nn);
        o->nv = po.nv;
        o->nn = po.nn;
        if (kept.count(k)) {
            for (uint32_t i = 0; i < po.np; ++i) {
                uint8_t pk[28];
                std::copy(m.ps1.begin() + po.prims + 28 * (size_t)i,
                          m.ps1.begin() + po.prims + 28 * (size_t)i + 28, pk);
                const int tp = pk[10] & 0x0F;
                for (int c = 0; c < 3; ++c) pk[4 + 4 * c] = (uint8_t)(pk[4 + 4 * c] + 128 * tp);
                pk[6] = 0x00; pk[7] = 0x78;    // CBA: CLUT row 0
                pk[10] = 0x80; pk[11] = 0x00;  // TSB: page 0, 8 bpp
                pk[14] = pk[15] = 0;           // 256-line page 0
                o->prims.insert(o->prims.end(), pk, pk + 28);
            }
            o->np = po.np;
            return true;
        }
        // PSXObject::Store refuses an object with no primitives (its kind
        // enumeration returns 0), so the placeholder carries one degenerate
        // triangle - all three corners on vertex 0 - of the one kind the
        // other objects use. It rasterises to nothing.
        if (po.nv > 0 && po.nn > 0) {
            put32(o->prims, 0x34000609);
            for (int c = 0; c < 3; ++c) {
                o->prims.push_back(0);
                o->prims.push_back(0);
                put16(o->prims, c == 0 ? 0x7800 : c == 1 ? 0x0080 : 0);
            }
            for (int c = 0; c < 6; ++c) put16(o->prims, 0);
            o->np = 1;
        }
        return true;
    };

    // The Saturn's skeleton is its own: each OBJECT node's offset is where that
    // joint's mesh was modelled to hang, and on several models it is not the
    // PS1's (Barry's chest and shoulders, Rebecca, Zombie Wesker over the
    // zombie's skeleton, the spider web's all-zero nodes). The offsets go into
    // the PS1 skeleton below. One case needs more than that - Rebecca's root
    // mesh is centred on its own origin, 451 units above the PS1's waist origin
    // the animation's root height is measured from - so when the root's
    // children agree on one vertical shift and the model's rest-pose floor
    // moved by the same amount, the root mesh and its children take it back.
    size_t psSk = 0;
    int psJoints = 0;
    const bool haveSk = ps1Skeleton(m.ps1, &psSk, &psJoints);
    auto psRel = [&](int j, int a) { return (double)le16s(m.ps1, psSk + 8 + 6 * (size_t)j + 2 * a); };
    double rootShiftY = 0.0;
    if (haveSk) {
        std::vector<double> deltas;
        for (const Node& n : m.nodes)
            if (!n.root && m.nodes[n.parent].root && n.jid < psJoints)
                deltas.push_back(psRel(n.jid, 1) - n.pos[1]);
        std::sort(deltas.begin(), deltas.end());
        if (!deltas.empty()) {
            const double mid = deltas[deltas.size() / 2];
            size_t agree = 0;
            for (double v : deltas)
                if (std::fabs(v - mid) <= 16.0) ++agree;
            // Rest-pose floor (largest y: y grows downward) of both models.
            std::vector<double> oSat(m.nodes.size(), 0.0), oPs(m.nodes.size(), 0.0);
            double floorSat = -1e9, floorPs = -1e9;
            bool ok = true;
            for (size_t i = 0; i < m.nodes.size() && ok; ++i) {
                const Node& n = m.nodes[i];
                if (n.jid >= psJoints) { ok = false; break; }
                if (!n.root) {
                    oSat[i] = oSat[n.parent] + n.pos[1];
                    oPs[i] = oPs[n.parent] + psRel(n.jid, 1);
                }
                const Part& part = m.parts[n.part];
                for (size_t k = 1; k < part.pts.size(); k += 3)
                    floorSat = std::max(floorSat, oSat[i] + part.pts[k]);
                PsObj po;
                if (!psObject((uint32_t)n.jid, &po)) { ok = false; break; }
                for (uint32_t k = 0; k < po.nv; ++k)
                    floorPs = std::max(floorPs, oPs[i] + le16s(m.ps1, po.verts + 8 * (size_t)k + 2));
            }
            if (ok && std::fabs(mid) >= 64.0 && 2 * agree >= deltas.size() &&
                std::fabs((floorPs - floorSat) - mid) <= 32.0)
                rootShiftY = mid;
        }
    }
    // Plant 42's roots (EM100E) are one mesh, and only the half of the root ball
    // the room's camera sees is modelled - facing +X on the PS1 (x 0..2700,
    // z -2700..2700) and -Z on the Saturn (x -2560..2560, z -2560..10). Placed
    // with the PS1's entity angle the Saturn half stands edge-on to the camera.
    // A single-mesh model is therefore turned about Y, in quarter turns, to
    // whichever of the four fits the PS1 mesh's footprint best.
    int quarterTurns = 0;
    if (joints == 1 && partOfJoint.count(0)) {
        PsObj po;
        const Part& part = m.parts[partOfJoint[0]];
        if (psObject(0, &po) && po.nv > 0 && !part.pts.empty()) {
            double pb[4] = {1e9, -1e9, 1e9, -1e9};  // PS1 x min/max, z min/max
            for (uint32_t k = 0; k < po.nv; ++k) {
                const double x = le16s(m.ps1, po.verts + 8 * (size_t)k);
                const double z = le16s(m.ps1, po.verts + 8 * (size_t)k + 4);
                pb[0] = std::min(pb[0], x); pb[1] = std::max(pb[1], x);
                pb[2] = std::min(pb[2], z); pb[3] = std::max(pb[3], z);
            }
            double best = 0;
            for (int q = 0; q < 4; ++q) {
                double sb[4] = {1e9, -1e9, 1e9, -1e9};
                for (size_t k = 0; k + 2 < part.pts.size(); k += 3) {
                    double x = part.pts[k], z = part.pts[k + 2];
                    for (int t = 0; t < q; ++t) {
                        const double nx = -z;
                        z = x;
                        x = nx;
                    }
                    sb[0] = std::min(sb[0], x); sb[1] = std::max(sb[1], x);
                    sb[2] = std::min(sb[2], z); sb[3] = std::max(sb[3], z);
                }
                double e = 0;
                for (int a = 0; a < 4; ++a) e += std::fabs(sb[a] - pb[a]);
                // A turn has to be clearly better than none.
                if (q == 0) best = e;
                else if (e < best * 0.5) { best = e; quarterTurns = q; }
            }
        }
    }
    // (x, z) -> (-z, x), `quarterTurns` times: a rotation, so the winding holds.
    auto turn = [&](double* v) {
        for (int t = 0; t < quarterTurns; ++t) {
            const double nx = -v[2];
            v[2] = v[0];
            v[0] = nx;
        }
    };
    for (int j = 0; j < joints; ++j) {
        // A joint the Saturn gives no mesh (EM1029, a body without its lower
        // legs: joints 4, 5, 7 and 8, which are 3-vertex dummies on the PS1
        // too) still needs its slot, or every later joint would animate as the
        // one before it.
        if (!partOfJoint.count(j)) {
            Obj o;
            if (!ps1Object((uint32_t)j, &o)) {
                if (error) *error = "Saturn EMD: joint ids are not 0..N-1";
                return false;
            }
            objs.push_back(std::move(o));
            continue;
        }
        const Part& part = m.parts[partOfJoint[j]];
        const double* cum = nodeOfJoint[j]->cum;
        const double shift[3] = {0.0, nodeOfJoint[j]->root ? rootShiftY : 0.0, 0.0};
        Obj o;
        const size_t nverts = part.pts.size() / 3;
        std::vector<double> acc(nverts * 3, 0.0);
        for (size_t k = 0; k < part.polys.size(); ++k) {
            double fn[3];
            for (int a = 0; a < 3; ++a)
                fn[a] = be32s(m.mesh, part.polyTable + 20 * k + 4 * a) / 65536.0;
            std::set<int> vs(part.polys[k].v, part.polys[k].v + 4);
            for (int vi : vs)
                for (int a = 0; a < 3; ++a) acc[vi * 3 + a] += fn[a];
        }
        for (size_t k = 0; k < nverts; ++k) {
            double pos[3];
            for (int a = 0; a < 3; ++a)
                pos[a] = (unit(cum) ? part.pts[k * 3 + a] : part.pts[k * 3 + a] * cum[a]) + shift[a];
            turn(pos);
            for (int a = 0; a < 3; ++a) put16(o.verts, (uint16_t)(int16_t)roundEven(pos[a]));
            put16(o.verts, 0);
            turn(&acc[k * 3]);
            const double* n = &acc[k * 3];
            double l = std::sqrt(0.0 + n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (l == 0.0) l = 1.0;
            for (int a = 0; a < 3; ++a) put16(o.norms, (uint16_t)(int16_t)roundEven(n[a] / l * 4096));
            put16(o.norms, 0);
        }
        o.nv = o.nn = (uint32_t)nverts;
        for (const Poly& q : part.polys) {
            const Texture& tx = m.textures[q.tex];
            const Place& pl = place[q.tex];
            int cx[4] = {0, tx.w, tx.w, 0}, cy[4] = {0, 0, tx.h, tx.h};
            for (int k = 0; k < 4; ++k) {
                if (q.dir & 0x10) cx[k] = tx.w - cx[k];
                if (q.dir & 0x20) cy[k] = tx.h - cy[k];
            }
            int u[4], v[4];
            for (int k = 0; k < 4; ++k) {
                u[k] = pl.x + std::min(cx[k], tx.w - 1);
                v[k] = pl.y + std::min(cy[k], tx.h - 1);
            }
            const bool tri = q.v[3] == q.v[2];
            const int tris[2][3] = {{0, 1, 2}, {0, 2, 3}};
            for (int t = 0; t < (tri ? 1 : 2); ++t) {
                // Saturn winding is the reverse of the PS1's.
                const int ia = tris[t][0], ib = tris[t][2], ic = tris[t][1];
                put32(o.prims, 0x34000609);
                // CBA / TSB as the PC EMDs carry them: CLUT at VRAM y 0x1E0,
                // tpage colour mode 1 (8 bpp). EnumKind rejects anything else.
                o.prims.push_back((uint8_t)u[ia]); o.prims.push_back((uint8_t)v[ia]); put16(o.prims, 0x7800);
                o.prims.push_back((uint8_t)u[ib]); o.prims.push_back((uint8_t)v[ib]); put16(o.prims, 0x0080);
                o.prims.push_back((uint8_t)u[ic]); o.prims.push_back((uint8_t)v[ic]); put16(o.prims, (uint32_t)pl.page);
                for (int k : {ia, ib, ic}) {
                    put16(o.prims, q.v[k]);
                    put16(o.prims, q.v[k]);
                }
                ++o.np;
            }
        }
        objs.push_back(std::move(o));
    }
    // The PS1 section's own TMD can have more objects than the Saturn skeleton has
    // joints. The engine sizes the joint array from the TMD and the animation
    // data from the PS1 skeleton, so each of them keeps its slot (a 15-object
    // player TMD leaves joint 15 reading past its slots: crash in
    // FindMinClutDepth) - with the PS1 geometry when it is a real part of the
    // model, empty when it is the player / NPC models' box (see `kept`).
    for (uint32_t k = (uint32_t)objs.size(); k < psObjs && k < 64; ++k) {
        Obj o;
        if (!ps1Object(k, &o)) break;
        objs.push_back(std::move(o));
    }
    const uint32_t table = 12 + 28 * (uint32_t)objs.size();
    // Body layout = the stock EMDs': ALL primitives (objects in order), then all
    // vertices, then all normals. It is not cosmetic. SetupEntityJointAnimation
    // (the room mirror's second copy of an entity) copies the object table from
    // entry 0 up to the address its first word - object 0's VERTEX pointer -
    // holds, and relocates every entry's primitive pointer into that copy. In the
    // stock layout that span is the table plus every primitive; with the
    // per-object (verts, norms, prims) interleave this converter used to write it
    // was the table alone, so the mirror's primitives pointed past the copy and
    // the entity left no reflection.
    std::vector<uint8_t> body, entries;
    std::vector<uint32_t> ptOf(objs.size()), vtOf(objs.size()), ntOf(objs.size());
    for (size_t k = 0; k < objs.size(); ++k) {
        ptOf[k] = table - 12 + (uint32_t)body.size();
        body.insert(body.end(), objs[k].prims.begin(), objs[k].prims.end());
    }
    for (size_t k = 0; k < objs.size(); ++k) {
        vtOf[k] = table - 12 + (uint32_t)body.size();
        body.insert(body.end(), objs[k].verts.begin(), objs[k].verts.end());
    }
    for (size_t k = 0; k < objs.size(); ++k) {
        ntOf[k] = table - 12 + (uint32_t)body.size();
        body.insert(body.end(), objs[k].norms.begin(), objs[k].norms.end());
    }
    for (size_t k = 0; k < objs.size(); ++k)
        for (uint32_t w : {vtOf[k], objs[k].nv, ntOf[k], objs[k].nn, ptOf[k], objs[k].np, 0u})
            put32(entries, w);
    std::vector<uint8_t> tmd;
    put32(tmd, table + (uint32_t)body.size());
    put32(tmd, 0);
    put32(tmd, (uint32_t)objs.size());
    tmd.insert(tmd.end(), entries.begin(), entries.end());
    tmd.insert(tmd.end(), body.begin(), body.end());

    // --- reassemble (build_pc_emd) ---
    uint32_t d[5];
    for (int i = 0; i < 5; ++i) d[i] = le32(m.ps1, m.ps1.size() - 20 + 4 * i);
    if (d[3] > m.ps1.size()) {
        if (error) *error = "Saturn EMD: PS1 section directory is corrupt";
        return false;
    }
    out->assign(m.ps1.begin(), m.ps1.begin() + d[3]);
    // The skeleton the engine animates with takes the Saturn's joint offsets
    // (see rootShiftY above). An offset is measured from the joint's parent in
    // the PS1 armature (u16 offset to it at +0 of the skeleton: per joint a u16
    // child count and a u16 offset to that many child ids, ps1Parents), which is the
    // Saturn tree's parent on every model but the adder, whose tree is rooted
    // at PS1 joint 1 - so it is taken as the difference of the two joints' rest
    // positions. A scaled node moves its
    // children with it, so a rest position also takes its parent's scale.
    // Offsets within the Saturn's own rounding (it stores them / 4) are left as
    // the PS1 has them.
    if (haveSk && psSk + 8 + 6 * (size_t)psJoints <= out->size()) {
        const std::vector<int> psParent = ps1Parents(m.ps1, psSk, psJoints);
        // Rest position of every node, from the root's origin.
        std::vector<double> rest(m.nodes.size() * 3, 0.0);
        std::map<int, size_t> nodeIndex;
        for (size_t i = 0; i < m.nodes.size(); ++i) {
            const Node& n = m.nodes[i];
            nodeIndex[n.jid] = i;
            if (n.root) continue;
            const Node& par = m.nodes[n.parent];
            for (int a = 0; a < 3; ++a)
                rest[i * 3 + a] = rest[(size_t)n.parent * 3 + a] +
                                  (n.pos[a] + (a == 1 && par.root ? rootShiftY : 0.0)) * par.cum[a];
        }
        for (size_t i = 0; i < m.nodes.size(); ++i) {
            const Node& n = m.nodes[i];
            if (n.jid >= psJoints) continue;
            // The PS1 parent's node; the PS1 root, and a joint whose PS1 parent
            // has no Saturn node, keep their PS1 offset.
            const int pp = psParent[n.jid];
            if (pp < 0 || !nodeIndex.count(pp)) continue;
            const size_t pi = nodeIndex[pp];
            const size_t at = psSk + 8 + 6 * (size_t)n.jid;
            int v[3];
            bool differs = !n.root && !unit(m.nodes[n.parent].cum);
            for (int a = 0; a < 3; ++a) {
                v[a] = roundEven(rest[i * 3 + a] - rest[pi * 3 + a]);
                if (std::abs(v[a] - le16s(*out, at + 2 * a)) > 2) differs = true;
            }
            if (!differs) continue;
            for (int a = 0; a < 3; ++a) {
                (*out)[at + 2 * a] = (uint8_t)v[a];
                (*out)[at + 2 * a + 1] = (uint8_t)(v[a] >> 8);
            }
        }
    }
    pad4(*out);
    d[3] = (uint32_t)out->size();
    out->insert(out->end(), tmd.begin(), tmd.end());
    pad4(*out);
    d[4] = (uint32_t)out->size();
    out->insert(out->end(), tim.begin(), tim.end());
    pad4(*out);
    for (uint32_t w : d) put32(*out, w);

    if (info) {
        info->parts = (int)m.parts.size();
        info->palettes = (int)m.palettes.size();
        info->textures = (int)m.textures.size();
        info->pages = npages;
        info->timHeight = H;
    }
    return true;
}

}  // namespace re1
