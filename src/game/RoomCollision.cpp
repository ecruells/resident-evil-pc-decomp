// RoomCollision.cpp - room boundary collision (decompiled from Ghidra)
//
// The room's collision geometry lives in the RDT at +0x4C (the ".blk" block).
// It is a 0x18-byte header followed by an array of 0x0C-byte boundary records:
//
//   header  short cellX, cellZ                 quadrant split point
//           int   count[5]                     per-quadrant record counts
//   record  short xMax, zMax, xMin, zMin       AABB, MAX corner FIRST
//           u16   type                         low byte = shape index
//           u16   flags
//
// Room_SetupCollisionCallbacks rewrites count[] in place into five absolute
// pointers, so quadrant q afterwards spans [group[q], group[q+1]).
// ChkOutsideCell picks the quadrant from the position's side of (cellX, cellZ);
// a record that straddles the split is duplicated into every quadrant it
// touches, so one quadrant's list is all a point ever needs to test.
//
// NOTE ON THE RECORD LAYOUT: the first coordinate pair is the box MAXIMUM and
// the second is the minimum - not (x, z, width, depth) as the RE2-era notes on
// this format claim. Verified two ways: boundary_point_outside accepts a point
// only when xMin-r <= px <= xMax+r, which is an empty interval for any box
// wider than 2r under the width reading; and across all 320 shipped RDTs every
// record's [pair2 .. pair1] span agrees with the set of quadrants that record
// was duplicated into. Only shapes 1, 3, 4 and 5 occur in the shipped data.
#include "../Globals.h"
#include "Types.h"
#include "Entities.h"

extern int SquareRoot0(int val);

// ---------------------------------------------------------------------------
// The 0x00be0de0-0x00be0df4 block is general-purpose scratch that the original
// shares between the collision push, the zombie AI and the player animations
// (see the declarations in Zombie.cpp / PlayerAnimations.cpp). The push writes
// its intermediate depths there; nothing in the collision path reads them back,
// but the clobber is part of the original's observable behaviour.
// ---------------------------------------------------------------------------
extern int          player_distance_z;   // 0x00be0de4
extern int          g_scaled_down_dist;  // 0x00be0de8
extern unsigned int g_entity_bkp;        // 0x00be0df4

int g_collPushDepthZHi = 0;              // 0x00be0dec
int g_collPushDepthZLo = 0;              // 0x00be0df0

// 0x00ac9c00 - boundary shape handler table, indexed by (record->type & 0xff).
// Slots 1/3/4/5 are installed by Room_SetupCollisionCallbacks; slots 0 and 2
// are never written by the original and never selected by the shipped data.
CollisionShapeHandler g_CollisionShapeHandlers[6] = {};

// ===========================================================================
// ChkOutsideCell (0x0047d270)
// Which of the four boundary quadrants does `position + offset` fall in?
// bit 0 = the point is on the low side of cellX, bit 1 = low side of cellZ.
// ===========================================================================
unsigned int ChkOutsideCell(VECTOR* position, SVECTOR* offset, int cellX, int cellZ)
{
    unsigned int bitX = (unsigned int)((offset->x - cellX) + position->x) >> 31;
    unsigned int bitZ = ((unsigned int)((offset->z + position->z) - cellZ) & 0xbfffffffu) >> 30;
    return bitZ | bitX;
}

// ===========================================================================
// boundary_point_outside (0x0047d2a0)
// Is g_playerPosScratch + *offset outside the box [xLo..xHi] x [zLo..zHi]?
//
// The original folds the four sign bits into bits 28-31 of one word through a
// shift-and-or chain (the low mask bits are shifted back out again by the end);
// only "is the result zero" is ever tested.
// ===========================================================================
static unsigned int boundary_point_outside(SVECTOR* offset,
                                           int xHi, int zHi, int xLo, int zLo)
{
    int ox = offset->x;
    int oz = offset->z;

    unsigned int a = (unsigned int)((g_playerPosScratch.x - xLo) + ox) & 0x80000007u;
    unsigned int b = (unsigned int)((xHi - ox) - g_playerPosScratch.x) & 0x80000003u;
    unsigned int c = (unsigned int)((g_playerPosScratch.z - zLo) + oz) & 0x80000001u;
    unsigned int d = (unsigned int)((zHi - oz) - g_playerPosScratch.z) & 0x80000000u;

    return ((((((a >> 1) | b) >> 1) | c) >> 1) | d);
}

// ===========================================================================
// boundary_classify (0x0047d1b0)
// Test one boundary record against g_playerPosScratch, grown by `radius`.
// Returns the record's shape index when the point is inside it, 0xffff if not.
// ===========================================================================
static unsigned short boundary_classify(SVECTOR* offset, RDT_Boundary* rec,
                                        unsigned int radius)
{
    int r = (int)(radius & 0xffff);

    // The original zero-extends each coordinate from 16 bits, then does the
    // grow/shrink as a signed 32-bit add - xMin - r may legitimately go < 0.
    unsigned int outside = boundary_point_outside(
        offset,
        (int)rec->xMax + r,
        (int)rec->zMax + r,
        (int)rec->xMin - r,
        (int)rec->zMin - r);

    if (outside != 0) return 0xffff;
    return (unsigned short)(rec->type & 0xff);
}

// ===========================================================================
// boundary_classify_flags (0x0047d210)
// Same test as boundary_classify, but returns the record's flag bits
// (flags & 0xff00) in place instead of the shape index.
// ===========================================================================
static unsigned short boundary_classify_flags(SVECTOR* offset, RDT_Boundary* rec,
                                              unsigned int radius)
{
    int r = (int)(radius & 0xffff);

    unsigned int outside = boundary_point_outside(
        offset,
        (int)rec->xMax + r,
        (int)rec->zMax + r,
        (int)rec->xMin - r,
        (int)rec->zMin - r);

    if (outside != 0) return 0xffff;
    return (unsigned short)(rec->flags & 0xff00);
}

// ===========================================================================
// collision_flag_set (0x0047e1b0)
// Shape 4: a soft zone. No push - it only raises the "touched a boundary" bit.
// ===========================================================================
static void collision_flag_set(short* bounds, int* pos, short* prevPos)
{
    (void)bounds; (void)pos; (void)prevPos;
    ENTITY->collisionFlags |= 0x08;
}

// ===========================================================================
// collision_push_rect (0x0047df10)
// Shapes 1 and 5: push the entity out of a rectangular obstacle.
//
// Computes the four exit depths (entity radius plus an 18-unit skin), keeps the
// shallower one per axis, then picks the axis to resolve on by comparing the
// sign of this frame's movement against the sign of the push: an axis whose
// push opposes the movement is the face the entity came in through.
// ===========================================================================
static void collision_push_rect(short* bounds, int* pos, short* prevPos)
{
    short radius = *(short*)(ENTITY->Sca_info + 10);

    // The original does all four subtractions in 16 bits, against only the low
    // word of the 32-bit position. `bounds` is signed here even though the
    // coordinates are unsigned (see RDT_Boundary): the adds are modular and the
    // result is truncated to a short, so the two readings give the same answer -
    // which is why the original can get away with 16-bit registers throughout.
    short posX = (short)pos[0];
    short posZ = (short)pos[2];

    short pushXHi = (short)((radius - posX) + bounds[0] + 0x12);    // exit via +x
    g_playerDisplacement = pushXHi;
    short pushXLo = (short)(((bounds[2] - radius) - posX) - 0x12);  // exit via -x
    player_distance_z = pushXLo;

    short pushX = pushXHi;
    if (-player_distance_z < g_playerDisplacement) {
        pushX = pushXLo;                       // -x face is the nearer exit
        g_scaled_down_dist = player_distance_z;
    }

    short pushZHi = (short)(((radius + bounds[1]) - posZ) + 0x12);  // exit via +z
    g_collPushDepthZHi = pushZHi;
    short pushZLo = (short)(((bounds[3] - radius) - posZ) - 0x12);  // exit via -z
    g_collPushDepthZLo = pushZLo;

    short pushZ = pushZHi;
    if (-g_collPushDepthZLo < g_collPushDepthZHi) {
        pushZ = pushZLo;
        g_entity_bkp = (unsigned int)g_collPushDepthZLo;
    }

    int prevX = prevPos[0];
    int prevZ = prevPos[2];

    // Selector bit 0: movement and push disagree in sign on X; bit 1: on Z.
    unsigned char sel = (unsigned char)(
        (((unsigned char)((pos[2] - prevZ) >> 14) ^ (unsigned char)(pushZ >> 14)) & 2) |
        (((unsigned char)((pos[0] - prevX) >> 15) ^ (unsigned char)(pushX >> 15)) & 1));

    if (prevX == pos[0] && prevZ == pos[2]) {
        g_animFrameIdSave = 0;
    }

    switch (sel) {
    case 0:
        // Both axes say the push would shove the entity further in, so it did
        // not enter through either face this frame: undo the whole move.
        pos[0] = prevX;
        pos[2] = prevZ;
        return;

    case 1:
        if ((unsigned short)(pushX + 0x190) <= 0x320) {
            ENTITY->scaMatrixData.localMatrix.t[0] += pushX;
            ENTITY->status_flags &= 0xef;
            return;
        }
        break;

    case 2:
        if ((unsigned short)(pushZ + 0x190) <= 0x320) {
            ENTITY->scaMatrixData.localMatrix.t[2] += pushZ;
            ENTITY->status_flags |= 0x10;
            return;
        }
        break;

    case 3:
        break;

    default:
        return;
    }

    // Ambiguous, or a single-axis push deeper than 400 units: resolve on
    // whichever axis needs the smaller correction.
    int absX = pushX < 0 ? -pushX : pushX;
    int absZ = pushZ < 0 ? -pushZ : pushZ;
    if (absZ > absX) {
        ENTITY->scaMatrixData.localMatrix.t[0] += pushX;
        ENTITY->status_flags &= 0xef;
    } else {
        ENTITY->scaMatrixData.localMatrix.t[2] += pushZ;
        ENTITY->status_flags |= 0x10;
    }
}

// ===========================================================================
// collision_push_circle (0x0047e0e0)
// Shape 3: push the entity out of a circular obstacle. The circle's radius
// comes from the record's X extent alone; its centre is the box centre.
// The original ignores prevPos; it is present only to match the table type.
// ===========================================================================
static void collision_push_circle(short* bounds, int* pos, short* prevPos)
{
    (void)prevPos;
    unsigned short* b = (unsigned short*)bounds;
    unsigned int radius = (unsigned int)*(unsigned short*)(ENTITY->Sca_info + 10);

    // (width / 2) + entity radius, then the offsets from the circle centre.
    int reach = (int)(((unsigned int)b[0] - (unsigned int)b[2]) + radius * 2) / 2;
    int dz = ((pos[2] - (int)(unsigned int)b[3]) - reach) + (int)radius;
    int dx = ((pos[0] - (int)(unsigned int)b[2]) - reach) + (int)radius;

    int dist = SquareRoot0(dx * dx + dz * dz);
    if (dist < 0) dist = -dist;

    int penetration = reach - dist;
    if (penetration < 1) return;

    // The original divides unguarded, so dead centre would fault. Push along
    // +x instead, which is where any degenerate approach direction lands.
    if (dist == 0) {
        ENTITY->scaMatrixData.localMatrix.t[0] += penetration;
        ENTITY->status_flags &= 0xef;
        return;
    }

    int pushX = (penetration * dx) / dist;
    int pushZ = (penetration * dz) / dist;

    ENTITY->scaMatrixData.localMatrix.t[2] += pushZ;
    ENTITY->scaMatrixData.localMatrix.t[0] += pushX;

    int absX = pushX < 0 ? -pushX : pushX;
    int absZ = pushZ < 0 ? -pushZ : pushZ;
    if (absX < absZ) {
        ENTITY->status_flags |= 0x10;
    } else {
        ENTITY->status_flags &= 0xef;
    }
}

// ===========================================================================
// Room_SetupCollisionCallbacks (0x0047d140)
// Turn the RDT's per-quadrant record counts into absolute range pointers and
// install the boundary shape handlers. Called once per room from room_set,
// immediately after LoadRoomRdt - the rewrite is in place and not idempotent.
// ===========================================================================
void Room_SetupCollisionCallbacks(void)
{
    RDT_BoundaryHeader* hdr = (RDT_BoundaryHeader*)g_RdtPointer->boundaries;
    RDT_Boundary* base = (RDT_Boundary*)((char*)hdr + 0x18);

    int running = (int)(size_t)hdr->group[0];   // still the quadrant-0 count
    hdr->group[0] = base;

    g_playerDisplacement = 0;
    do {
        int count = (int)(size_t)hdr->group[g_playerDisplacement + 1];
        hdr->group[g_playerDisplacement + 1] = base + running;
        g_playerDisplacement++;
        running += count;
    } while (g_playerDisplacement < 4);

    g_CollisionShapeHandlers[1] = collision_push_rect;
    g_CollisionShapeHandlers[5] = collision_push_rect;
    g_CollisionShapeHandlers[3] = collision_push_circle;
    g_CollisionShapeHandlers[4] = collision_flag_set;
}

// ---------------------------------------------------------------------------
// Shared tail of check_room_collision: accept the current position, record how
// far the entity actually travelled, and advance the rollback point.
// ---------------------------------------------------------------------------
static unsigned char collision_accept(short floorStep, unsigned char clearResult)
{
    int* t = ENTITY->scaMatrixData.localMatrix.t;
    int dx = t[0] - (int)ENTITY->position.x;
    int dz = t[2] - (int)ENTITY->position.z;

    g_tempVar = (void*)(size_t)(unsigned int)SquareRoot0(dz * dz + dx * dx);

    ENTITY->position.x = (short)t[0];
    ENTITY->position.y = (short)t[1];
    ENTITY->position.z = (short)t[2];

    if (floorStep != 0) {
        g_animFrameIdSave = (unsigned int)(int)floorStep;
        return 3;
    }
    return clearResult;
}

// ===========================================================================
// check_room_collision (0x0047d310)
// Resolve ENTITY against the room boundaries.
//
// Pass 1 tests the incoming position and lets each hit record's handler push
// the entity out. Pass 2 re-tests the pushed position: if it is still inside a
// blocking record the frame's movement is rolled back to `position`
// (entity+0x6C), otherwise the rollback point advances to here.
//
// Returns 0 = clear, 1 = pushed out and now clear, 2 = still stuck (position
// reverted), 3 = a floor/step zone was crossed, with its step value left in
// g_animFrameIdSave. g_tempVar receives the distance actually travelled.
// ===========================================================================
unsigned char check_room_collision(VECTOR* position, short radius)
{
    SVECTOR offset;
    unsigned short hitBits = 0;
    short floorStep = 0;

    offset.x = 0;
    offset.y = 0;
    offset.z = 0;
    offset.pad = 0;

    ENTITY->collisionFlags &= 0xf7;
    if ((ENTITY->status_flags & 4) != 0) return 0;

    // The original dereferences unconditionally; room_set always installs the
    // boundary pointers before anything can run, but the port reaches this from
    // menu/attract paths where no room is loaded.
    if (g_RdtPointer == NULL || g_RdtPointer->boundaries == NULL) return 0;

    RDT_BoundaryHeader* hdr = (RDT_BoundaryHeader*)g_RdtPointer->boundaries;
    short cell = (short)ChkOutsideCell(position, &offset, hdr->cellX, hdr->cellZ);
    RDT_Boundary* first = hdr->group[cell];
    RDT_Boundary* last  = hdr->group[cell + 1];

    g_svecScratch.x = 0;
    g_svecScratch.y = 0;
    g_svecScratch.z = 0;
    g_playerPosScratch = *position;
    g_animFrameIdSave = 0;

    // ---- pass 1: push out of everything the incoming position is inside ----
    for (RDT_Boundary* rec = first; rec < last; rec++) {
        unsigned char cf = ENTITY->collisionFlags;

        if ((cf & 4) != 0 && (rec->flags & 0x100) == 0) {
            // Floor / step zone: the record carries a height step instead of a
            // shape, reported to the caller through g_animFrameIdSave.
            short step = (short)((((rec->type & 0x7f00) >> 8) * 10
                                  + (rec->flags & 0xff)) * 100);
            floorStep = (short)(((rec->type & 0x8000) ? -(int)step : 0) | 1);
            continue;
        }

        if ((rec->type & 0xff) == 5 && (cf & 0x10) != 0) continue;

        unsigned short shape = boundary_classify(&g_svecScratch, rec,
                                                (unsigned short)radius);
        if (shape == 0xffff) continue;

        if (shape < 6 && g_CollisionShapeHandlers[shape] != NULL) {
            g_CollisionShapeHandlers[shape]((short*)rec,
                                            ENTITY->scaMatrixData.localMatrix.t,
                                            &ENTITY->position.x);
        }
        hitBits |= (unsigned short)((rec->flags & 0x300) >> 8);
    }

    if (hitBits == 0) {
        return collision_accept(floorStep, 0);
    }

    // ---- pass 2: is the pushed position clear? ----
    // The original copies four dwords from entity+0x34, so `pad` picks up the
    // first word of worldMatrix - it is never read, only overwritten.
    g_playerPosScratch.x = ENTITY->scaMatrixData.localMatrix.t[0];
    g_playerPosScratch.y = ENTITY->scaMatrixData.localMatrix.t[1];
    g_playerPosScratch.z = ENTITY->scaMatrixData.localMatrix.t[2];
    hitBits = 0;
    g_animFrameIdSave = 0;

    for (RDT_Boundary* rec = first; rec < last; rec++) {
        unsigned short t = (unsigned short)(rec->type & 0xff);
        if (t == 4) continue;
        if (t == 5 && (ENTITY->collisionFlags & 0x10) != 0) continue;
        if ((ENTITY->collisionFlags & 4) != 0 && (rec->flags & 0x100) == 0) continue;

        unsigned short shape = boundary_classify(&g_svecScratch, rec,
                                                (unsigned short)radius);
        if ((shape & 0x8000) != 0) continue;

        unsigned short bits = (unsigned short)(rec->flags & 0x300);
        hitBits |= (unsigned short)(bits >> 8);
        if (shape == 3 && bits != 0) {
            g_animFrameIdSave = 1;
        }
    }

    if (g_animFrameIdSave != 0) {
        // Wedged against a circular obstacle - accept the push as it stands.
        return collision_accept(floorStep, 0);
    }
    if (hitBits == 0) {
        return collision_accept(floorStep, 1);
    }

    // Still inside something: roll this frame's movement back.
    g_tempVar = (void*)0;
    ENTITY->scaMatrixData.localMatrix.t[0] = (int)ENTITY->position.x;
    ENTITY->position.y = (short)ENTITY->scaMatrixData.localMatrix.t[1];
    ENTITY->scaMatrixData.localMatrix.t[2] = (int)ENTITY->position.z;

    if (floorStep != 0) {
        g_animFrameIdSave = (unsigned int)(int)floorStep;
        return 3;
    }
    return 2;
}

// ===========================================================================
// room_collision_check_0047da50 (0x0047da50)
// Point query with no push: which boundary flag bits does `position + offset`
// land in? Returns 1 as soon as a fully-blocking record (flags 0x300) contains
// the point, otherwise the OR of the flag bits of every record it is inside.
// ===========================================================================
short room_collision_check_0047da50(VECTOR* position, VECTOR* offset)
{
    unsigned short bits = 0;

    if (g_RdtPointer == NULL || g_RdtPointer->boundaries == NULL) return 0;

    RDT_BoundaryHeader* hdr = (RDT_BoundaryHeader*)g_RdtPointer->boundaries;
    short cell = (short)ChkOutsideCell(position, (SVECTOR*)offset,
                                       hdr->cellX, hdr->cellZ);
    g_playerPosScratch = *position;

    RDT_Boundary* first = hdr->group[cell];
    RDT_Boundary* last  = hdr->group[cell + 1];

    for (RDT_Boundary* rec = first; rec < last; rec++) {
        unsigned short f = boundary_classify_flags((SVECTOR*)offset, rec, 0);
        if ((short)f == -1) continue;
        if ((unsigned char)((f & 0x300) >> 8) == 3) return 1;
        bits |= (unsigned short)(f & 0x300);
    }

    return (short)bits;
}

// ---------------------------------------------------------------------------
// 0x00ac9cc8 - the group start pointer room_check_sight_blocked was last called
// with. The original stores it on every call; the only xref to the address is
// that write, so nothing ever reads it back. Kept so the store is not silently
// dropped from the port.
// ---------------------------------------------------------------------------
static RDT_Boundary* g_lastSightGroup = NULL;

// ===========================================================================
// One segment-vs-segment test, factored out of room_check_sight_blocked: does
// the ray (ent -> ent + dir) cross the box diagonal A -> B?
//
// Both halves are the standard 2D straddle test on the sign of the XZ cross
// product (vectorMul3's .y): A and B must fall on opposite sides of the ray,
// and the ray's two ends must fall on opposite sides of the diagonal.
//
// `normalizeA` reproduces the lone VectorNormal call the original makes, in the
// first of its two diagonal tests only (0x0047dda2). Scaling a vector to length
// 4096 cannot change a cross product's sign, so the asymmetry does not make the
// two tests disagree - it is reproduced rather than dropped because it is a real
// call in the original's control flow.
//
// Note the second half crosses against the UNDIVIDED `dir`, while its other
// operand is in /18 space. Mixed scale is harmless for a sign test.
// ===========================================================================
static int ray_crosses_diagonal(VECTOR* dir, int entX, int entZ, int dirX, int dirZ,
                                int ax, int az, int bx, int bz, int normalizeA)
{
    VECTOR p0, p1;

    // ---- do A and B straddle the ray? ----
    // g_playerPosScratch (0x00be11b0) is the original's scratch for the edge.
    g_playerPosScratch.x = bx - ax;
    g_playerPosScratch.y = 0;
    g_playerPosScratch.z = bz - az;

    p1.x = (entX + dirX) - ax;  p1.y = 0;  p1.z = (entZ + dirZ) - az;
    p0.x = entX - ax;           p0.y = 0;  p0.z = entZ - az;

    vectorMul3(&g_playerPosScratch, &p1, &p1);
    vectorMul3(&g_playerPosScratch, &p0, &p0);

    if ((((unsigned int)p0.y ^ (unsigned int)p1.y) & 0x80000000u) == 0) return 0;

    // ---- ...and do the ray's ends straddle the diagonal? ----
    p1.x = bx - entX;  p1.y = 0;  p1.z = bz - entZ;
    vectorMul3(dir, &p1, &p1);

    p0.x = ax - entX;  p0.y = 0;  p0.z = az - entZ;
    if (normalizeA) VectorNormal(&p0, &p0);
    vectorMul3(dir, &p0, &p0);

    return (((unsigned int)p0.y ^ (unsigned int)p1.y) & 0x80000000u) != 0;
}

// ===========================================================================
// room_check_sight_blocked (0x0047db90)
// Does the straight line from ENTITY to ENTITY + `delta` cross a sight-blocking
// boundary record? `cell` is the quadrant index, so only one quadrant's list is
// walked - the same trick ChkOutsideCell sets up for the push path.
//
// Callers: check_line_of_sight (0x0048a4b0), check_weapon_line_of_sight
// (0x0048a530) and entity_check_angular_los (0x00489c60).
//
// The per-record test is the cheap "does the segment cross the box" trick: a
// chord that enters and leaves an axis-aligned box must cross at least one of
// its two diagonals, so instead of clipping against four edges the original runs
// two segment-vs-segment tests, one per diagonal:
//
//     (xMax, zMin) -> (xMin, zMax)        diagonal 1 (with the VectorNormal)
//     (xMin, zMin) -> (xMax, zMax)        diagonal 2
//
// EVERY coordinate is scaled down by 18 first, and that is not cosmetic:
// boundary coordinates are unsigned and reach 35677 in the shipped data (see the
// RDT_Boundary note in Types.h), so a cross product of two raw spans would
// overflow a signed 32-bit int. /18 keeps the products under ~1.4e7.
// ===========================================================================
unsigned int room_check_sight_blocked(VECTOR* delta, unsigned char cell)
{
    cell &= 0x1f;

    // The original dereferences unconditionally, as the other two entry points
    // in this file used to; the port reaches the entity code from paths with no
    // room loaded.
    if (g_RdtPointer == NULL || g_RdtPointer->boundaries == NULL) return 0;

    // NOT bounds-checked, matching the original. group[cell] / group[cell+1]
    // only bound a quadrant for cell 0-3, yet the mask above admits 0-31 and
    // entity_check_angular_los passes the pathfind counter at entity+0x164
    // straight through. What keeps it in range is an invariant, not a test:
    // entity_pathfind_update only calls that path while the counter is <= 3,
    // check_line_of_sight passes ChkOutsideCell's 0-3, and
    // check_weapon_line_of_sight loops 3 down to 0. Break any of those and both
    // the original and this port read a wild `first`/`last` pair out of the
    // record array and walk it - so if this ever crashes here, the bug is in the
    // caller's counter, not in the missing check.
    RDT_BoundaryHeader* hdr = (RDT_BoundaryHeader*)g_RdtPointer->boundaries;
    RDT_Boundary* first = hdr->group[cell];
    RDT_Boundary* last  = hdr->group[cell + 1];

    g_lastSightGroup = first;

    // The ray, in /18 space: entity -> entity + delta. These divides are signed
    // (IDIV at 0x0047dbc9); the record's are not.
    int entX = ENTITY->scaMatrixData.localMatrix.t[0] / 18;
    int entZ = ENTITY->scaMatrixData.localMatrix.t[2] / 18;
    int dirX = delta->x / 18;
    int dirZ = delta->z / 18;

    for (RDT_Boundary* rec = first; rec < last; rec++) {
        // 16-bit UNSIGNED divides (DIV at 0x0047dc0f, not IDIV). That is what
        // keeps the 222 shipped records with a coordinate above 32767 from
        // inverting into a negative box.
        int xMax = (int)(rec->xMax / 18u);
        int zMax = (int)(rec->zMax / 18u);
        int xMin = (int)(rec->xMin / 18u);
        int zMin = (int)(rec->zMin / 18u);

        // The original masks the record's flags down to the two blocking bits
        // and WRITES THE RESULT BACK (0x0047dc4a), discarding the low byte's
        // floor/step fine value for the rest of the room's life. Reproduced:
        // check_room_collision reads that low byte, so the clobber is
        // observable, and it is the sort of thing a "tidier" port would drop.
        unsigned short blocking = (unsigned short)(rec->flags & 0x300);
        rec->flags = blocking;

        // Only fully-blocking records occlude sight - except for Yawn (ids 13
        // and 18), for which every record occludes. Same pair of ids that
        // HandleEnemyPlayerCollisions special-cases.
        if (blocking != 0x300 && ENTITY->id != 13 && ENTITY->id != 18) continue;

        // Compared as the FULL 16-bit type, not (type & 0xff) as everywhere else
        // in this file, so a shape-4 or shape-5 record that also carries a floor
        // step in its high bits is not skipped here. Quirk of the original.
        if (rec->type == 4 || rec->type == 5) continue;

        if (ray_crosses_diagonal(delta, entX, entZ, dirX, dirZ,
                                 xMax, zMin, xMin, zMax, 1)) {
            return 1;
        }
        if (ray_crosses_diagonal(delta, entX, entZ, dirX, dirZ,
                                 xMin, zMin, xMax, zMax, 0)) {
            return 1;
        }
    }

    return 0;
}
