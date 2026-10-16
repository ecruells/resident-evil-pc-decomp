#include "ZombieModeInternal.h"
#include "../../DebugPrint.h"
#include <cstring>

// ============================================================================
// ZombieNav.cpp - a walk grid for the loaded room, for the zombie mod's AI
// survivor. Port-added; nothing like it exists in the original, whose NPCs
// steer on the coarse RDT+0x58 zone ring (zone_path_find) and slide along
// walls.
//
// Built once per room load, straight from the room's collision: the room's
// extent is the bounding box of its boundary records, cut into square cells.
// A cell is walkable when its centre and four body-radius points are clear of
// every fully blocking boundary (room_collision_check_0047da50 returns 1), and
// two neighbouring cells are connected when the segment between their centres
// is clear too, so thin walls between two clear cells still separate them.
// Routes are a breadth-first search over those links (8 directions).
// ============================================================================

#define NAV_MAX        160          // cells per axis at most
#define NAV_MIN_CELL   250          // world units
#define NAV_BODY       260          // clearance probed around a cell centre
#define NAV_MARGIN     1000         // grid extends this far past the boundaries

static bool          s_navReady = false;
static unsigned char s_navStage = 0xFF, s_navRoom = 0xFF;
static int           s_navOX, s_navOZ, s_navCell, s_navW, s_navH;
static unsigned char s_walk[NAV_MAX * NAV_MAX];
static unsigned char s_links[NAV_MAX * NAV_MAX];   // bit d: can step in direction d
static short         s_parent[NAV_MAX * NAV_MAX];
static short         s_queue[NAV_MAX * NAV_MAX];

static const int kDirX[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
static const int kDirZ[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };

// One point against the room's collision. The query takes an absolute
// position and an SVECTOR offset from it, as the original's own callers pass
// it (CharacterNpc / EntityCommon: g_playerPosScratch + g_svecScratch); it
// writes g_playerPosScratch, which the callers below save and restore.
static bool nav_point_blocked(int x, int z)
{
    VECTOR pos = { x, 0, z, 0 };
    SVECTOR off = { 0, 0, 0, 0 };
    return room_collision_check_0047da50(&pos, (VECTOR*)&off) == 1;
}

static bool nav_body_clear(int x, int z)
{
    static const int k[5][2] = { {0,0}, {NAV_BODY,0}, {-NAV_BODY,0}, {0,NAV_BODY}, {0,-NAV_BODY} };
    for (int i = 0; i < 5; i++) {
        if (nav_point_blocked(x + k[i][0], z + k[i][1])) return false;
    }
    return true;
}

// Clear segment, sampled every ~100 units (thinner than any wall record).
static bool nav_segment_clear_raw(int ax, int az, int bx, int bz)
{
    int dx = bx - ax, dz = bz - az;
    int len = SquareRoot0(dx * dx + dz * dz);
    int steps = len / 100 + 1;
    for (int i = 1; i <= steps; i++) {
        if (nav_point_blocked(ax + dx * i / steps, az + dz * i / steps)) return false;
    }
    return true;
}

static int nav_cx(int cell) { return s_navOX + (cell % s_navW) * s_navCell + s_navCell / 2; }
static int nav_cz(int cell) { return s_navOZ + (cell / s_navW) * s_navCell + s_navCell / 2; }

void zm_nav_build(void)
{
    if (s_navReady && s_navStage == g_stageId && s_navRoom == g_roomId) {
        return;     // a camera-only door: same room, same grid
    }
    s_navReady = false;
    s_navStage = g_stageId;
    s_navRoom = g_roomId;
    if (g_RdtPointer == NULL || g_RdtPointer->boundaries == NULL) return;

    // The room's extent from its boundary records (all five quadrant groups
    // are one contiguous run, [group[0], group[4])).
    RDT_BoundaryHeader* hdr = (RDT_BoundaryHeader*)g_RdtPointer->boundaries;
    int minX = 0x7FFFFFFF, minZ = 0x7FFFFFFF, maxX = -0x7FFFFFFF, maxZ = -0x7FFFFFFF;
    for (RDT_Boundary* r = hdr->group[0]; r < hdr->group[4]; r++) {
        if ((int)r->xMin < minX) minX = r->xMin;
        if ((int)r->zMin < minZ) minZ = r->zMin;
        if ((int)r->xMax > maxX) maxX = r->xMax;
        if ((int)r->zMax > maxZ) maxZ = r->zMax;
    }
    if (minX > maxX || minZ > maxZ) return;
    minX -= NAV_MARGIN; minZ -= NAV_MARGIN; maxX += NAV_MARGIN; maxZ += NAV_MARGIN;

    int extent = (maxX - minX > maxZ - minZ) ? maxX - minX : maxZ - minZ;
    s_navCell = extent / NAV_MAX + 1;
    if (s_navCell < NAV_MIN_CELL) s_navCell = NAV_MIN_CELL;
    s_navOX = minX;
    s_navOZ = minZ;
    s_navW = (maxX - minX) / s_navCell + 1;
    s_navH = (maxZ - minZ) / s_navCell + 1;
    if (s_navW > NAV_MAX) s_navW = NAV_MAX;
    if (s_navH > NAV_MAX) s_navH = NAV_MAX;

    VECTOR savedScratch = g_playerPosScratch;
    int cells = s_navW * s_navH, walkable = 0;
    for (int c = 0; c < cells; c++) {
        s_walk[c] = nav_body_clear(nav_cx(c), nav_cz(c)) ? 1 : 0;
        walkable += s_walk[c];
        s_links[c] = 0;
    }
    // Links, each pair tested once (directions 0-3) and mirrored (d + 4).
    for (int c = 0; c < cells; c++) {
        if (!s_walk[c]) continue;
        int cx = c % s_navW, cz = c / s_navW;
        for (int d = 0; d < 4; d++) {
            int nx = cx + kDirX[d], nz = cz + kDirZ[d];
            if (nx < 0 || nz < 0 || nx >= s_navW || nz >= s_navH) continue;
            int n = nz * s_navW + nx;
            if (!s_walk[n]) continue;
            if (nav_segment_clear_raw(nav_cx(c), nav_cz(c), nav_cx(n), nav_cz(n))) {
                s_links[c] |= (unsigned char)(1u << d);
                s_links[n] |= (unsigned char)(1u << (d + 4));
            }
        }
    }
    g_playerPosScratch = savedScratch;
    s_navReady = true;
    dbg_printf("[nav] stage %d room %02X: %dx%d cells of %d, %d walkable\n",
               (int)g_stageId, (int)g_roomId, s_navW, s_navH, s_navCell, walkable);
}

bool zm_nav_ready(void)
{
    return s_navReady && s_navStage == g_stageId && s_navRoom == g_roomId;
}

bool zm_nav_segment_clear(int ax, int az, int bx, int bz)
{
    VECTOR savedScratch = g_playerPosScratch;
    bool clear = nav_segment_clear_raw(ax, az, bx, bz);
    g_playerPosScratch = savedScratch;
    return clear;
}

// The walkable cell nearest (x, z), searching outward ring by ring, or -1.
static int nav_nearest_walkable(int x, int z, int maxRings)
{
    int gx = (x - s_navOX) / s_navCell;
    int gz = (z - s_navOZ) / s_navCell;
    for (int ring = 0; ring <= maxRings; ring++) {
        int best = -1, bestD = 0x7FFFFFFF;
        for (int dz = -ring; dz <= ring; dz++) {
            for (int dx = -ring; dx <= ring; dx++) {
                if (dx != -ring && dx != ring && dz != -ring && dz != ring) continue;
                int nx = gx + dx, nz = gz + dz;
                if (nx < 0 || nz < 0 || nx >= s_navW || nz >= s_navH) continue;
                int c = nz * s_navW + nx;
                if (!s_walk[c]) continue;
                int ddx = nav_cx(c) - x, ddz = nav_cz(c) - z;
                int d = ddx * ddx + ddz * ddz;
                if (d < bestD) { bestD = d; best = c; }
            }
        }
        if (best >= 0) return best;
    }
    return -1;
}

int zm_nav_path(int fromX, int fromZ, int toX, int toZ, int goalRadius,
                int* outX, int* outZ, int maxPoints)
{
    if (!zm_nav_ready() || maxPoints < 2) return 0;
    int start = nav_nearest_walkable(fromX, fromZ, 6);
    if (start < 0) return 0;

    int cells = s_navW * s_navH;
    for (int c = 0; c < cells; c++) s_parent[c] = -1;
    long long goalR2 = (long long)goalRadius * goalRadius;

    int head = 0, tail = 0, goal = -1;
    s_queue[tail++] = (short)start;
    s_parent[start] = (short)start;
    while (head < tail) {
        int c = s_queue[head++];
        long long gx = nav_cx(c) - toX, gz = nav_cz(c) - toZ;
        if (gx * gx + gz * gz <= goalR2) { goal = c; break; }
        int cx = c % s_navW, cz = c / s_navW;
        for (int d = 0; d < 8; d++) {
            if ((s_links[c] & (1u << d)) == 0) continue;
            int n = (cz + kDirZ[d]) * s_navW + (cx + kDirX[d]);
            if (s_parent[n] >= 0) continue;
            s_parent[n] = (short)c;
            s_queue[tail++] = (short)n;
        }
    }
    if (goal < 0) return 0;

    // Measure the route, then fill it start -> goal. A route longer than the
    // buffer keeps its start (where the walker is) and is cut at the far end.
    int length = 1;
    for (int c = goal; c != start; c = s_parent[c]) length++;
    int count = length < maxPoints ? length : maxPoints;
    int i = length - 1;
    for (int c = goal; ; c = s_parent[c], i--) {
        if (i < count) {
            outX[i] = nav_cx(c);
            outZ[i] = nav_cz(c);
        }
        if (c == start) break;
    }
    return count;
}
