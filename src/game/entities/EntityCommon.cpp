// EntityCommon.cpp - Entity-generic runtime shared by every entity type.
//
// These functions were originally all decompiled into Zombie.cpp because the
// zombie was the first entity ported, but none of them are zombie logic: they
// operate on whatever ENTITY currently points at. The monster update functions
// (ids 0-21), the shared human-character driver (ids 22-47, CharacterNpc.cpp)
// and the player code in PlayerAnimations.cpp all call into them.
//
// Anything that reads a zombie table or dispatches to a zombie behaviour stays
// in Zombie.cpp - including zombie_update_player_distance (0x00434330), which
// was named entity_* until its switch over the zombie move behaviour table
// (0x004bb280) gave it away.
//
// All original addresses from Ghidra.
// ============================================================================
#include "EntityCommon.h"
#include <cstring>

// ============================================================================
// Shared scratch globals (0x00be0de4 onward)
// Also written by RoomCollision.cpp, PlayerAnimations.cpp and WeaponDamage.cpp,
// which each carry their own extern for them. Declared in address order.
// ============================================================================
int          player_distance_z = 0;   // 0x00be0de4
int          g_scaled_down_dist = 0;  // 0x00be0de8
unsigned int g_entity_bkp = 0;        // 0x00be0df4
void*        _ENTITY_SAVE = NULL;

// ============================================================================
// enemies_update_functions_tbl @ 0x004d3c90
// Per-entity-type update function dispatch table, indexed by entity->id.
//
// **48 entries**, not 32. Ids 0-21 are the monsters; ids 22-47 all point at
// 0x0046acf0, the shared human-character driver (character_npc_update in
// CharacterNpc.cpp). Verified by reading 0x004d3c90: the 0x0046acf0 run ends at
// 0x004d3d50, which is 48 dwords in, and the next dword is 0.
//
// Declaring only 32 was not a harmless under-count. cmd_em_set gives cutscene
// actors ids from 32 up (Chris 32, Jill 33, Barry 34, Rebecca 35, Wesker 36), so
// update_entities indexed one to sixteen entries past the end - straight into
// zombie_states_table, which used to follow in the same translation unit. Jill
// read zombie_state_check and ran zombie logic; nothing ever ran her character
// init, so her skeleton was never posed and she never appeared.
// ============================================================================
void* enemies_update_functions_tbl[48] = {
    (void*)zombie_update,  // [0]  zombie (white coat)
    (void*)zombie_update,  // [1]  zombie (naked)
    NULL,                  // [2]  enemy type 2  (0x00497fb0)
    NULL,                  // [3]  enemy type 3  (0x00478310)
    NULL,                  // [4]  enemy type 4  (0x0044f300)
    NULL,                  // [5]  enemy type 5  (0x0042e520)
    NULL,                  // [6]  enemy type 6  (0x004161f0)
    NULL,                  // [7]  enemy type 7  (0x0048daf0)
    NULL,                  // [8]  enemy type 8  (0x00464d10)
    NULL,                  // [9]  enemy type 9  (0x00438a70)
    NULL,                  // [10] enemy type 10 (0x004727f0)
    NULL,                  // [11] enemy type 11 (0x0043d8d0)
    NULL,                  // [12] enemy type 12 (0x00421990)
    NULL,                  // [13] enemy type 13 (0x004051e0)
    NULL,                  // [14] enemy type 14 (0x0047e1c0)
    NULL,                  // [15] enemy type 15 (0x0045abb0)
    NULL,                  // [16] enemy type 16 (0x00421990)
    (void*)zombie_update,  // [17] zombie variant 3
    NULL,                  // [18] enemy type 18 (0x004051e0)
    NULL,                  // [19] enemy type 19 (0x00443640)
    NULL,                  // [20] enemy type 20 (0x00427330)
    NULL,                  // [21] enemy type 21 (0x0040b760)
    (void*)character_npc_update,  // [22]
    (void*)character_npc_update,  // [23]
    (void*)character_npc_update,  // [24]
    (void*)character_npc_update,  // [25]
    (void*)character_npc_update,  // [26]
    (void*)character_npc_update,  // [27]
    (void*)character_npc_update,  // [28]
    (void*)character_npc_update,  // [29]
    (void*)character_npc_update,  // [30]
    (void*)character_npc_update,  // [31]
    (void*)character_npc_update,  // [32] chris (cutscene actor)
    (void*)character_npc_update,  // [33] jill
    (void*)character_npc_update,  // [34] barry
    (void*)character_npc_update,  // [35] rebecca
    (void*)character_npc_update,  // [36] wesker
    (void*)character_npc_update,  // [37] Kenneth's corpse
    (void*)character_npc_update,  // [38] Forest's corpse
    (void*)character_npc_update,  // [39] richard
    (void*)character_npc_update,  // [40] enrico
    (void*)character_npc_update,  // [41]
    (void*)character_npc_update,  // [42]
    (void*)character_npc_update,  // [43]
    (void*)character_npc_update,  // [44]
    (void*)character_npc_update,  // [45]
    (void*)character_npc_update,  // [46]
    (void*)character_npc_update   // [47]
};

// ============================================================================
// getAngleTowardsTarget @ 0x00460450
// Returns the PS1 angle (0-0xFFF, 4096 = 360 deg) from the current entity's
// position to the target (px, pz). Wraps CalculateAngleBetweenPointsXZ.
// ============================================================================
unsigned short getAngleTowardsTarget(int px, int pz)
{
    return CalculateAngleBetweenPointsXZ(
        *(int*)&ENTITY->scaMatrixData.localMatrix.t[0],  // entity pos X
        *(int*)&ENTITY->scaMatrixData.localMatrix.t[2],  // entity pos Z
        px, pz);
}

// ============================================================================
// turn_toward_target @ 0x00489960
// Returns an angular step (+step, -step, or 0) to rotate the entity toward
// the target position. Uses getAngleTowardsTarget to compute the desired
// angle, then returns the shortest signed step to reduce the delta.
// If already facing the target within angle_step*2, returns 0.
// ============================================================================
int turn_toward_target(VECTOR* target_pos, short angle_step)
{
    unsigned short targetAngle = getAngleTowardsTarget(target_pos->x, target_pos->z);
    int delta = ((short)targetAngle - (short)(unsigned short)ENTITY->angle) + angle_step;
    delta &= 0xFFF;

    if (delta < (int)(unsigned short)(angle_step * 2)) {
        return 0;                    // already facing target
    }
    if (delta < ANGLE_HALF_CIRCLE + 1) {
        return angle_step;           // turn clockwise (shorter path right)
    }
    return -angle_step;              // turn counter-clockwise (shorter path left)
}

// ============================================================================
// entity_rotate_toward_target @ 0x004899b0
// Smoothly rotates the entity toward or away from a target position by
// angular steps. Bit 15 of angleStep flips the direction (face away).
// Computes desired angle via getAngleTowardsTarget, then adjusts entity
// angle by +angleStep (toward) or -angleStep + 180 deg (away).
// ============================================================================
void entity_rotate_toward_target(VECTOR* pos, unsigned short angleStep)
{
    short targetAngle = getAngleTowardsTarget(pos->x, pos->z);
    unsigned short baseAngle = (unsigned short)targetAngle;

    if ((angleStep & 0x8000) != 0) {
        angleStep = -angleStep;
        baseAngle = (baseAngle + 0x800) & 0xFFF;  // +180 deg
    }

    unsigned short delta = ((unsigned short)(angleStep - ENTITY->angle) + baseAngle) & 0xFFF;

    // The original widens the step to int BEFORE doubling: (short)param_2 * 2.
    // Casting the product back to short instead would wrap a step above 0x3FFF
    // negative and make the "close enough, snap to target" test never fire.
    if ((int)delta < (int)(short)angleStep * 2) {
        ENTITY->angle = (short)baseAngle;
        return;
    }

    ENTITY->angle = ENTITY->angle - (short)angleStep;
    if (delta < ANGLE_HALF_CIRCLE + 1) {
        ENTITY->angle = ENTITY->angle + (short)(angleStep * 2);
    }
}

// ============================================================================
// check_line_of_sight @ 0x0048a4b0
// Is the entity's view of `targetPos` blocked? Picks the boundary quadrant the
// target falls in with ChkOutsideCell, then hands the entity->target delta to
// room_check_sight_blocked. Returns 0 if the path is clear, 1 if blocked.
//
// targetPos is a VECTOR, not the SVECTOR Ghidra's stack-arg guess says: the
// original reads three ints out of it, at +0, +4 and +8.
// ============================================================================
unsigned char check_line_of_sight(VECTOR* targetPos)
{
    g_svecScratch.z = 0;
    g_svecScratch.y = 0;
    g_svecScratch.x = 0;

    RDT_BoundaryHeader* hdr = (RDT_BoundaryHeader*)g_RdtPointer->boundaries;
    unsigned int cell = ChkOutsideCell(targetPos, &g_svecScratch,
                                       hdr->cellX, hdr->cellZ);

    VECTOR delta;
    delta.x = targetPos->x - ENTITY->scaMatrixData.localMatrix.t[0];
    delta.y = targetPos->y - ENTITY->scaMatrixData.localMatrix.t[1];
    delta.z = targetPos->z - ENTITY->scaMatrixData.localMatrix.t[2];

    unsigned int result = room_check_sight_blocked(&delta, (unsigned char)cell);
    player_distance_z = result & 0xFF;
    return (unsigned char)(result & 0xFF);
}

// ============================================================================
// entity_check_angular_los @ 0x00489c60
// Checks if the target position is within the entity's angular FOV (half-angle
// param_1) AND has a clear line of sight. Returns 0 if path is clear, non-zero
// if blocked or outside the angular wedge.
// ============================================================================
unsigned int entity_check_angular_los(short fovHalfAngle, VECTOR* targetPos)
{
    short targetAngle = getAngleTowardsTarget(targetPos->x, targetPos->z);
    unsigned short delta = ((unsigned short)(fovHalfAngle - ENTITY->angle) + (unsigned short)targetAngle) & 0xFFF;

    // 0x00489c7?: param_1 * 2 is a signed int compare, not truncated to 16 bits
    if (fovHalfAngle * 2 < (int)delta)
        return 1;  // outside angular FOV

    VECTOR dir;
    dir.x = targetPos->x - *(int*)&ENTITY->scaMatrixData.localMatrix.t[0];
    dir.z = targetPos->z - *(int*)&ENTITY->scaMatrixData.localMatrix.t[2];
    dir.y = 0;

    // entity+0x164 is entity_pathfind_update's counter byte, passed through raw.
    // room_check_sight_blocked masks it to 5 bits and only 0-3 index a real
    // boundary quadrant; the caller only reaches here while the counter is <= 3.
    unsigned char boundaryIndex = *(unsigned char*)((char*)ENTITY + 0x164);
    return room_check_sight_blocked(&dir, boundaryIndex);
}

// ============================================================================
// checkAngularViewAndDistance @ 0x00489cf0
// Checks whether a target position is within the entity's angular field of
// view (a wedge defined by fovHalfAngle) and within maxDistance. Creates two
// edge vectors from entity angle +/- fovHalfAngle, then uses 2D cross products
// to test if the direction to target lies between them. Returns true if the
// target is both within range and within the FOV wedge.
// ============================================================================
unsigned char checkAngularViewAndDistance(short fovHalfAngle, short maxDistance, VECTOR* targetPos)
{
    VECTOR referenceForward = { 2000, 0, 0, 0 };

    VECTOR dirToTarget;
    dirToTarget.x = targetPos->x - *(int*)&ENTITY->scaMatrixData.localMatrix.t[0];
    dirToTarget.z = targetPos->z - *(int*)&ENTITY->scaMatrixData.localMatrix.t[2];
    dirToTarget.y = 0;

    int absDx = (dirToTarget.x ^ (dirToTarget.x >> 31)) - (dirToTarget.x >> 31);
    int absDz = (dirToTarget.z ^ (dirToTarget.z >> 31)) - (dirToTarget.z >> 31);
    if ((int)(unsigned short)maxDistance < absDx - (dirToTarget.x >> 31) + absDz)
        return 0;

    MATRIX local_20;
    local_20 = g_identityMatrixData;

    VECTOR leftEdge, rightEdge;
    RotMatrixY(ENTITY->angle - (int)fovHalfAngle, &local_20);
    ApplyMatrixLV(&local_20, &referenceForward, &leftEdge);

    RotMatrixY(fovHalfAngle * 2, &local_20);
    ApplyMatrixLV(&local_20, &referenceForward, &rightEdge);

    vectorMul3(&leftEdge, &dirToTarget, &leftEdge);
    vectorMul3(&rightEdge, &dirToTarget, &rightEdge);

    return (unsigned char)((leftEdge.y & 0x80000000U) < (rightEdge.y & 0x80000000U));
}

// ============================================================================
// entity_check_visual_range @ 0x0043bfa0
// Computes Euclidean distance from entity to player via SquareRoot0.
// If distance < range, sets status_flags bit 5 (0x20) - "player in visual range".
// ============================================================================
void entity_check_visual_range(unsigned int range)
{
    int dx = (int)g_playerEntity.scaMatrixData.localMatrix.t[0]
           - (int)ENTITY->scaMatrixData.localMatrix.t[0];
    int dz = (int)g_playerEntity.scaMatrixData.localMatrix.t[2]
           - (int)ENTITY->scaMatrixData.localMatrix.t[2];
    unsigned int distance = SquareRoot0(dx * dx + dz * dz);

    if (distance < range) {
        ENTITY->status_flags |= ENTITY_STATUS_PLAYER_ABOVE;
    }
}

// ============================================================================
// entity_check_alert_range @ 0x0043bfe0
// Computes Euclidean distance from entity to player via SquareRoot0.
// If distance < range, sets status_flags bit 7 (0x80) - "player in alert range".
// ============================================================================
void entity_check_alert_range(unsigned int range)
{
    int dx = (int)g_playerEntity.scaMatrixData.localMatrix.t[0]
           - (int)ENTITY->scaMatrixData.localMatrix.t[0];
    int dz = (int)g_playerEntity.scaMatrixData.localMatrix.t[2]
           - (int)ENTITY->scaMatrixData.localMatrix.t[2];
    unsigned int distance = SquareRoot0(dx * dx + dz * dz);

    if (distance < range) {
        ENTITY->status_flags |= ENTITY_STATUS_PLAYER_BELOW;
    }
}

// ============================================================================
// entity_pathfind_update @ 0x0048ad10
// Obstacle-detection pathfinding state machine. Uses entity+0x164 as a
// 3-bit counter (bits 0-4, clamped to 15) + direction flag (bit 5).
// Returns: 0 = no target, 1 = target acquired/path clear, 2 = waiting.
// When counter == 3, stores player position as new movement target.
// ============================================================================
unsigned int entity_pathfind_update(void)
{
    unsigned char* state = (unsigned char*)ENTITY + 0x164;  // pathfind_state
    unsigned char val = *state;
    unsigned char counter = val & 0x1F;

    if (counter > 3) {
        *state = counter + 1;
        if ((*state & 0x1F) > 0x0F)
            *state &= 0xC0;  // clamp counter
        return 2;
    }

    char result = entity_check_angular_los(1512, (VECTOR*)g_playerEntityPointer.scaMatrixData.localMatrix.t);
    *state = (result << 5) | val;

    val = *state;
    counter = val & 0x1F;

    if (counter == 3) {
        if ((val & 0x20) == 0) {
            // 16-bit stores - see the waypoint note in Entities.h.
            ENTITY->player_pos_x = (short)g_playerEntityPointer.scaMatrixData.localMatrix.t[0];
            ENTITY->player_pos_z = (short)g_playerEntityPointer.scaMatrixData.localMatrix.t[2];
            *state = counter + 1;
            *state &= ~0x20;
            return 1;
        }
        *state = counter + 1;
        *state &= ~0x20;
        return 0;
    }

    *state = counter + 1;
    return 2;
}

// ============================================================================
// entity_update_wander_turn @ 0x00489800
// Controls randomized wandering turns when the entity gets stuck. If movement
// distance falls below a threshold derived from the entity's speed divider,
// a turn counter increments. When it exceeds turn_limit, a random turn
// (direction from g_RandSeed bit 6) activates. Also applies angular rotation
// toward the current waypoint using getAngleTowardsTarget.
// ============================================================================
unsigned int entity_update_wander_turn(unsigned int movement_dist, unsigned char* control_flags, unsigned char* turn_counter, unsigned short angle_step, unsigned char turn_limit)
{
    unsigned short* entity_angle = (unsigned short*)&ENTITY->angle;
    unsigned char* speedDiv = (unsigned char*)ENTITY + 0x61;  // speed_divider (inside scaMatrixData - struct layout gap)

    if ((*control_flags & 0x80) != 0) {
        *entity_angle = *entity_angle
            + (1 - (unsigned short)((*control_flags & 0x40) >> 5)) * angle_step;

        int speedDiv3 = *speedDiv * 3;
        int threshold = (speedDiv3 + (speedDiv3 >> 31 & 3)) >> 2;

        if ((unsigned int)threshold < movement_dist) {
            unsigned char newCount = *turn_counter - 1;
            *turn_counter = newCount;
            if (newCount == 0) {
                *control_flags = 0;
                *turn_counter = 0;
            }
        }
        return 1;
    }

    if (movement_dist < (unsigned int)((*speedDiv * 2) / 3)) {
        unsigned char newCount = *turn_counter + 1;
        *turn_counter = newCount;
        if (turn_limit < newCount) {
            unsigned char flags = *control_flags;
            *control_flags = flags | 0x80;
            *control_flags = ((unsigned char)g_RandSeed & 0x40) | flags | 0x80;
            *turn_counter = turn_limit / 6;
        }
    } else {
        *control_flags = 0;
        *turn_counter = 0;
    }

    short waypointAngle = getAngleTowardsTarget(
        (int)*(unsigned char*)((char*)ENTITY + 0xB3),
        (int)*(unsigned char*)((char*)ENTITY + 0xB4));
    unsigned short targetAngle = (unsigned short)waypointAngle;

    if ((angle_step & 0x8000) != 0) {
        angle_step = -angle_step;
        targetAngle = (targetAngle + 0x800) & 0xFFF;
    }

    unsigned short delta = ((unsigned short)(angle_step - *entity_angle) + targetAngle) & 0xFFF;

    if ((int)delta < (short)(angle_step * 2)) {
        *entity_angle = targetAngle;
        return 0;
    }

    *entity_angle = *entity_angle - angle_step;
    if (delta < 0x801) {
        *entity_angle = *entity_angle + angle_step * 2;
    }
    return 0;
}

// ============================================================================
// SetEntityScaHitData @ 0x0041b2c0
// Converts the entity's local SCA collision points into world-space hit
// coordinates by rotating them around the Y-axis using the entity's angle.
// Iterates the SCA volume list (6 shorts per entry, terminated by negative
// first short), rotating each point so the collision/hit-check system can
// operate in world space.
// ============================================================================
void SetEntityScaHitData(Entity* ent)
{
    short* srcVol = *(short**)((char*)ent + 4);
    short* dstVol = *(short**)((char*)ent + 8);

    g_svecScratch.y = ent->angle;
    g_svecScratch.z = 0;
    g_svecScratch.x = 0;

    RotMatrix(&g_svecScratch, &g_matrixScratch);

    while (*srcVol >= 0) {
        SVECTOR localVertex;
        localVertex.x = srcVol[1];
        localVertex.z = srcVol[3];
        localVertex.y = srcVol[2];

        // ApplyMatrix writes three ints (see its note in GteMatrix.cpp); the
        // destination has to be a VECTOR, and the truncation to short happens here.
        VECTOR worldVertex;
        ApplyMatrix(&g_matrixScratch, &localVertex, &worldVertex);

        dstVol[0] = (short)worldVertex.x;
        dstVol[1] = srcVol[2];
        dstVol[2] = (short)worldVertex.z;

        dstVol += 3;
        srcVol += 6;
    }
}

// ============================================================================
// ResolveEntityScaCollision @ 0x0041b0a0
// Resolves SCA (Sphere/Cylinder Area) collision between two entities.
// Iterates both entities' SCA volume lists, checks each pair for overlap
// using Euclidean distance + SquareRoot0, and pushes the second entity
// away from the first by the penetration depth. Returns 1 if collision
// occurred, 0 otherwise.
// ============================================================================
unsigned int ResolveEntityScaCollision(Entity* entA, Entity* entB)
{
    if (entB->state == 4) return 0;       // eating/headless state - skip collision
    if ((entA->status_flags | entB->status_flags) & 2) return 0;  // one is deactivated

    short* volAStart = *(short**)((char*)entA + 4);   // SCA volume list start
    short* volAEnd   = *(short**)((char*)entA + 8);   // SCA volume list end
    unsigned char hitFlag = 0;

    while (*volAStart >= 0) {                          // terminate on negative first short
        short* volBStart = *(short**)((char*)entB + 4);
        short* volBEnd   = *(short**)((char*)entB + 8);

        while (*volBStart >= 0) {
            int dx = ((int)volBStart[0] - (int)volAStart[0])
                   - *(int*)&entA->scaMatrixData.localMatrix.t[0]
                   + *(int*)&entB->scaMatrixData.localMatrix.t[0];
            int dz = ((int)volBStart[2] - (int)volAStart[2])
                   - *(int*)&entA->scaMatrixData.localMatrix.t[2]
                   + *(int*)&entB->scaMatrixData.localMatrix.t[2];

            unsigned short radiusA = volAStart[5];  // cylinder radius
            unsigned short radiusB = volBStart[5];
            unsigned short heightA = volAStart[4];  // cylinder half-height
            unsigned short heightB = volBStart[4];

            int dist = SquareRoot0(dz * dz + dx * dx);
            int penetration = (unsigned int)(radiusA + radiusB) - (dist + 1);

            if (penetration > 0) {
                int dy = (int)volBStart[1]
                       + (*(int*)&entA->scaMatrixData.localMatrix.t[1] - (int)volAStart[1])
                       - *(int*)&entB->scaMatrixData.localMatrix.t[1];

                int maxHeight = (unsigned int)(unsigned short)heightA
                              + (unsigned int)(unsigned short)heightB;

                if (-maxHeight < dy && dy < maxHeight) {
                    int pushX = (penetration * dx) / (dist + 1);
                    int pushZ = (penetration * dz) / (dist + 1);

                    int dy2 = (int)volBStart[1]
                            + ((int)entA->position.y - (int)volAStart[1])
                            - *(int*)&entB->scaMatrixData.localMatrix.t[1];

                    if (dy2 <= -maxHeight || maxHeight <= dy2) {
                        int posXA = *(int*)&entA->scaMatrixData.localMatrix.t[0];
                        if ((entB->position.x < posXA && posXA < *(int*)&entB->scaMatrixData.localMatrix.t[0])
                         || (posXA < entB->position.x && *(int*)&entB->scaMatrixData.localMatrix.t[0] < posXA)) {
                            if (-pushX < 1)
                                pushX = -(-pushX + (unsigned int)(unsigned short)radiusA * 2);
                            else
                                pushX = (unsigned int)(unsigned short)radiusA * 2 + pushX;
                        }
                        int posZA = *(int*)&entA->scaMatrixData.localMatrix.t[2];
                        if ((entB->position.z < posZA && posZA < *(int*)&entB->scaMatrixData.localMatrix.t[2])
                         || (posZA < entB->position.z && *(int*)&entB->scaMatrixData.localMatrix.t[2] < posZA)) {
                            if (-pushZ < 1)
                                pushZ = -(-pushZ + (unsigned int)(unsigned short)radiusA * 2);
                            else
                                pushZ = (unsigned int)(unsigned short)radiusA * 2 + pushZ;
                        }
                    }

                    *(int*)&entB->scaMatrixData.localMatrix.t[0] += pushX;
                    *(int*)&entB->scaMatrixData.localMatrix.t[2] += pushZ;
                    hitFlag = 1;
                }
            }

            volBStart += 3;       // next volume: advance 3 shorts (6 bytes)
        }

        volAStart += 3;           // next volume: advance 3 shorts (6 bytes)
    }

    return hitFlag;
}

// ============================================================================
// HandleEnemyPlayerCollisions @ 0x00489e10
// Resolves SCA collisions between all active enemies and the current ENTITY,
// computing a combined hit flag. Also handles Yawn-specific player pushback:
// if player moved >450 units and Yawn (ID 13/18) is active, pushes the player
// back by 1/4 of the displacement.
// ============================================================================
unsigned int HandleEnemyPlayerCollisions(void)
{
    unsigned char hitFlag = 0;
    Entity* enemies = g_EnemiesList;
    signed char count = g_enemy_count;

    while (count != 0) {
        if (enemies->status_flags != 0 && enemies != ENTITY) {
            hitFlag |= (unsigned char)ResolveEntityScaCollision(enemies, ENTITY);
        }
        count--;
        enemies = (Entity*)((char*)enemies + sizeof(Entity));
    }

    int dx = (int)g_playerEntityPointer.scaMatrixData.localMatrix.t[0]
           - (int)g_playerEntityPointer.position.x;
    int dz = (int)g_playerEntityPointer.scaMatrixData.localMatrix.t[2]
           - (int)g_playerEntityPointer.position.z;

    int absDx = (dx ^ (dx >> 31)) - (dx >> 31);
    int absDz = (dz ^ (dz >> 31)) - (dz >> 31);
    g_playerDisplacement = absDx - (dz >> 31) + absDz;

    if (g_playerDisplacement > 450
        && (g_playerEntityPointer.flags & 2) == 0
        && (g_EnemiesList[0].id == 13 || g_EnemiesList[0].id == 18))
    {
        g_playerDisplacement = dx >> 4;
        player_distance_z = dz >> 4;
        g_playerEntityPointer.scaMatrixData.localMatrix.t[0] =
            g_playerEntityPointer.position.x + g_playerDisplacement;
        g_playerEntityPointer.scaMatrixData.localMatrix.t[2] =
            g_playerEntityPointer.position.z + player_distance_z;
    }

    return hitFlag;
}

// ============================================================================
// blood_splatter_physics @ 0x00437d20
// Blood drop physics after a hit. Moves the blood joint downward with
// gravity, checks room collision for wall/floor hits, creates blood
// billboards at impact points, plays impact SFX, and decrements the
// speed parameter. Called from zombie_update for the hand joint.
// ============================================================================
void blood_splatter_physics(int jointData, short gravityStep)
{
    if (*(int*)(jointData + 0x5C) >= -100 && (*(unsigned char*)(jointData + 3) & 0x1F) >= 6)
        return;

    unsigned char jointFlag = *(unsigned char*)(jointData + 3);
    unsigned char animFrame = jointFlag & 0x1F;

    SVECTOR splatterDir;
    splatterDir.z = 0x40;
    splatterDir.y = (6 - animFrame) * 0x10;
    splatterDir.x = (6 - animFrame) * 8;

    RotMatrix(&splatterDir, &g_matrixScratch);
    MulMatrix((MATRIX*)(jointData + 0x44), &g_matrixScratch);

    g_matrixScratch = g_identityMatrixData;
    RotMatrixY(ENTITY->angle, &g_matrixScratch);

    VECTOR* jointPos = (VECTOR*)(jointData + 0x58);
    SVECTOR local_18;
    ApplyMatrixSV(&g_matrixScratch, (SVECTOR*)(jointData + 4), &local_18);

    int savedX = jointPos->x;
    int savedZ = *(int*)(jointData + 0x60);

    jointPos->x += (1 - (unsigned int)((jointFlag & 0x40) >> 5)) * (int)local_18.x;
    *(int*)(jointData + 0x60) += (1 - (unsigned int)((jointFlag & 0xBF) >> 6)) * (int)local_18.z;

    g_svecScratch.z = 0; g_svecScratch.y = 0; g_svecScratch.x = 0;
    short collision = room_collision_check_0047da50(jointPos, (VECTOR*)&g_svecScratch);

    if (collision != 0) {
        jointFlag ^= 0x40;
        jointPos->x = savedX;
        *(int*)(jointData + 0x60) = savedZ;
        *(unsigned char*)(jointData + 3) = jointFlag;

        jointPos->x = (1 - (unsigned int)((jointFlag & 0x40) >> 5)) * (int)local_18.x + savedX;
        *(int*)(jointData + 0x60) = (1 - (unsigned int)((jointFlag & 0xBF) >> 6)) * (int)local_18.z + savedZ;

        collision = room_collision_check_0047da50(jointPos, (VECTOR*)&g_svecScratch);
        if (collision != 0) {
            *(unsigned char*)(jointData + 3) ^= 0xC0;
        }

        jointPos->x = savedX;
        *(int*)(jointData + 0x60) = savedZ;
        *(short*)(jointData + 4) >>= 1;

        VECTOR zero = { 0, 0, 0, 0 };
        Effect_CreateBillboard(0, 0, 0, (void*)(jointData + 0x44), &zero, 0);
    }

    short accel = *(short*)(jointData + 6) - (unsigned short)*(unsigned char*)(jointData + 2) * gravityStep;
    *(short*)(jointData + 6) = accel;
    *(int*)(jointData + 0x5C) -= (int)accel;

    if (*(int*)(jointData + 0x5C) > -0x65) {
        *(int*)(jointData + 0x5C) = -0x63;  // -99
        *(unsigned char*)(jointData + 2) = 0;
        *(short*)(jointData + 6) = -accel;
        *(unsigned char*)(jointData + 3) += 1;
        *(short*)(jointData + 4) += 0x28;
        *(short*)(jointData + 6) = -accel >> 2;

        VECTOR zero = { 0, 0, 0, 0 };
        Effect_CreateBillboard(0, 0, 0, (void*)(jointData + 0x44), &zero, 0);
        Snd_em(8);
    }

    if (*(short*)(jointData + 4) > 0)
        *(short*)(jointData + 4) = 0;

    *(unsigned char*)(jointData + 2) += 1;
}

// ============================================================================
// snap_player_to_grab_position @ 0x00489ee0
// Positions the player at the entity's grab point by extracting the current
// animation vertex for the entity's attacking joint, rotating it to world
// space, and computing the target position. Sets the player's grab-target
// coordinates so the player model snaps to the bite/grab spot.
// ============================================================================
void snap_player_to_grab_position(void* player)
{
    entity_extract_anim_vertex(ENTITY, ENTITY->animHeader, ENTITY->animBase, 0);

    g_matrixScratch = g_identityMatrixData;
    RotMatrixY(ENTITY->angle, &g_matrixScratch);
    ApplyMatrixSV(&g_matrixScratch, &g_svecScratch, &g_svecScratch);

    *(short*)((char*)ENTITY + 0xC6) =
        (short)(*(int*)&ENTITY->scaMatrixData.localMatrix.t[0]) - g_svecScratch.x;
    *(short*)((char*)ENTITY + 0xC8) =
        (short)(*(int*)&ENTITY->scaMatrixData.localMatrix.t[2]) - g_svecScratch.z;

    *(short*)((char*)player + 0xC6) = *(short*)((char*)ENTITY + 0xC6);
    *(short*)((char*)player + 0xC8) = *(short*)((char*)ENTITY + 0xC8);
}

// ============================================================================
// joint_setup_attack_effect @ 0x0048a070
// Configures a joint for an attack special effect (blood, bite mark, etc.).
// Sets size parameters (0x28 standard or 0x30 for alt costumes), a timer
// at +0x70, effect type at +3, and frame match at +0x72. Also applies to
// the weapon-part joint if g_main_state_flags has bit 0 set.
// ============================================================================
void joint_setup_attack_effect(int joint, unsigned char effectType, unsigned short timer, unsigned short frameMatch)
{
    if ((*(unsigned char*)(joint + 2) & 0x80) != 0) return;

    int sizeVal = 0x28;
    unsigned char sizeB = 0x60;
    unsigned char sizeC = 0x28;

    // Larger effect size for alternate costumes (entity ID 3 or 4)
    if (*(char*)((char*)ENTITY + 1) == 3 || *(char*)((char*)ENTITY + 1) == 4) {
        sizeVal = 0x30;
        sizeB = 0x18;
        sizeC = 0x18;
    }

    // Apply effect to the main joint
    joint_enable_special_effect(joint, sizeB, sizeVal, sizeC);
    *(unsigned short*)(joint + 0x70) = timer;
    *(unsigned char*)(joint + 3) = effectType;
    *(unsigned short*)(joint + 0x72) = frameMatch;

    // Also apply to weapon-part joint if active
    if (((unsigned char)g_main_state_flags & 1) != 0) {
        int weaponJoint = (*(int*)((char*)ENTITY + 0xAC) - *(int*)&ENTITY->jointsStructs) + joint;
        joint_enable_special_effect(weaponJoint, sizeB, sizeVal, sizeC);
        *(unsigned char*)(weaponJoint + 3) = effectType;
        *(unsigned short*)(weaponJoint + 0x70) = timer;
        *(unsigned short*)(weaponJoint + 0x72) = frameMatch;
    }
}

// ============================================================================
// FUN_004565f0 (0x004565f0)
// Builds an entity's ground-shadow billboard. `pos` is the world offset (from
// g_svecScratch at every call site) and `quad` is the entity's sprite block at
// entity+0xE4; halfW/halfH are the shadow's half-extents.
//
// Despite the old "SCA init helper" label this is the shadow quad builder, and it
// was an empty stub - so every character's shadow block stayed all zeros and there
// was nothing for the sprite pass to draw.
//
// Layout, from the disassembly at 0x004565f0 (SVECTOR = 8 bytes):
//   quad[0]        world offset, copied wholesale from `pos`
//   quad[1]        primitive header; .z/.pad take the packed 0xRRGGBB tint that
//                  the caller left in scratch global 0x00be0dfc as ONE dword
//   quad[2].pad    CLUT      (GteClutBuild(0, 487))
//   quad[3].pad    tpage     (GteTpageBuild(1, 2, 384, 256))
//   quad[2..5].z   the four UV pairs: (0x51,0xC8) (0x6B,0xC8) (0x51,0xE5) (0x6B,0xE5)
//   quad[0xB..0xE] the four corners in the XZ plane, y = 0:
//                    (-halfW, 0, +halfH) (+halfW, 0, +halfH)
//                    (-halfW, 0, -halfH) (+halfW, 0, -halfH)
//                  Byte offsets 0x58/0x60/0x68/0x70 - exactly the offsets
//                  BillboardAdjSize and BillboardSetSize patch.
//   quad[6..0xA]   a 40-byte copy of quad[1..5]: the second half of the quad.
//
// One deliberate difference: the original pairs each corner's .z with an
// uninitialised stack word, so .pad receives garbage. Nothing reads it; zero is
// written here instead of reproducing indeterminate values.
// ============================================================================
void FUN_004565f0(SVECTOR* pos, SVECTOR* quad, int halfW, int halfH)
{
    // 0x004565f3: single dword store of the tint scratch into quad[1].z/.pad
    *(unsigned int*)&quad[1].z = g_animFrameIdSave;

    GteSpriteHeaderInit(&quad[1]);

    quad[3].pad = (short)GteTpageBuild(1, 2, 384, 256);
    quad[2].pad = (short)GteClutBuild(0, 487);

    unsigned char* uv = (unsigned char*)quad;
    uv[0x14] = 0x51;  uv[0x15] = 0xC8;   // quad[2].z
    uv[0x1C] = 0x6B;  uv[0x1D] = 0xC8;   // quad[3].z
    uv[0x24] = 0x51;  uv[0x25] = 0xE5;   // quad[4].z
    uv[0x2C] = 0x6B;  uv[0x2D] = 0xE5;   // quad[5].z

    short w = (short)halfW;
    short h = (short)halfH;

    quad[0xB].x = (short)-w;  quad[0xB].y = 0;  quad[0xB].z =  h;  quad[0xB].pad = 0;
    quad[0xC].x =         w;  quad[0xC].y = 0;  quad[0xC].z =  h;  quad[0xC].pad = 0;
    quad[0xD].x = (short)-w;  quad[0xD].y = 0;  quad[0xD].z = (short)-h; quad[0xD].pad = 0;
    quad[0xE].x =         w;  quad[0xE].y = 0;  quad[0xE].z = (short)-h; quad[0xE].pad = 0;

    quad[0] = *pos;

    memcpy(&quad[6], &quad[1], 40);
}

// ============================================================================
// Remaining engine dependencies (pending full decompilation)
//
// Every stub below MUST keep the signature declared in EntityCommon.h. A stub
// whose parameter list differs from the real implementation elsewhere does not
// collide at link time - it becomes a distinct OVERLOAD, and every call site
// that includes this header silently binds to the do-nothing one. That is what
// happened to BillboardSetColor (now in PlayerAnimations.cpp) and to
// entity_add_fade_sprite (now in FadeSprite.cpp).
// ============================================================================
unsigned char FUN_0048ae00(int joint, VECTOR* pos, int radius, int playerPtr) { return 0; }
void FUN_0040a380(VECTOR* v0, VECTOR* v1) { }
void FUN_0045f970(int px, int pz, int* a, int* b) { }
unsigned int is_facing_toward_entity(void* player) { return 0; }
void entity_apply_anim_vertex(void* entity, unsigned int animHeader, unsigned int animBase) { }
void joint_enable_special_effect(int joint, unsigned char a, int b, unsigned char c) { }
char reduce_attack_time_by_btn_press(void) { return 0; }

// FUN_0047d6f0 @ 0x0047d6f0 - STUB. The two-point boundary push a prone entity
// needs: it runs boundary_classify + g_CollisionShapeHandlers at both ends of the
// body (see the SVECTOR pair in zombie_update) and rolls position and angle back
// from the backups at entity+0x6c/0x70/0x7e if either end stays stuck. While it
// returns 0, a zombie on the floor has no body-length collision at all.
unsigned char FUN_0047d6f0(SVECTOR* endA, SVECTOR* endB) { return 0; }

// set_next_entity_data_buffer @ 0x00488f90 - stub
void set_next_entity_data_buffer(int count) { }

// FUN_0048bd00 @ 0x0048bd00 - lighting check stub
unsigned char FUN_0048bd00(void* light, unsigned char param2, int param3) { return 0; }

// FUN_0048bda0 @ 0x0048bda0 - lighting response stub
void FUN_0048bda0(void) { }

// FUN_0048c0d0 @ 0x0048c0d0 - pre-flip setup stub
void FUN_0048c0d0(void) { }

// FlipSprite @ 0x00460610 - stub
void FlipSprite(int light, MATRIX* out, unsigned char param3, int param4) { }

// Matrix_MulMatrix @ 0x0040a2e0 - stub (may already exist in GteMatrix.cpp)
void Matrix_MulMatrix(MATRIX* a, MATRIX* b) { }

// ---------------------------------------------------------------------------
// Implemented elsewhere, listed here so the split stays legible:
//   check_room_collision             @ 0x0047d310 - RoomCollision.cpp
//   ChkOutsideCell                   @ 0x0047d270 - RoomCollision.cpp
//   room_collision_check_0047da50    @ 0x0047da50 - RoomCollision.cpp
//   room_check_sight_blocked         @ 0x0047db90 - RoomCollision.cpp
//   vectorMul3                       @ 0x0040a550 - GteMatrix.cpp
//   VectorNormal                     @ 0x0040a5c0 - GteMatrix.cpp
//   entity_add_fade_sprite           @ 0x00456810 - FadeSprite.cpp
//   BillboardSetColor                @ 0x00456710 - PlayerAnimations.cpp
// ---------------------------------------------------------------------------
