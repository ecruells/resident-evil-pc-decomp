// WeaponDamage.cpp — Real-time weapon hit detection and damage application
// apply_weapon_damage @ 0x0043c020
// Called each frame when the player fires a weapon. Iterates all active
// enemies, calls per-weapon hit detection callbacks (knife cone, gun cone,
// projectile distance) to find the closest target in range+FOV, then
// subtracts damage from the enemy's health and sets the hit reaction state.
#include "../Globals.h"

// ---- Global scratch variable (set by apply_weapon_damage before hit detection) ----
extern int g_scaled_down_dist;  // holds weapon_id - 1 during hit detection

// ---- Forward declarations ----
// room_check_sight_blocked (0x0047db90) comes from Globals.h.

// ============================================================================
// check_weapon_line_of_sight @ 0x0048a530
// Multi-layer room obstruction check for weapon hits. Computes the vector
// from the player to the hit position, then checks 4 room partition layers
// (indices 3 down to 0) via room_check_sight_blocked. Returns 0 only if
// all layers are clear — the shot has an unobstructed path.
// ============================================================================
static unsigned char check_weapon_line_of_sight(VECTOR* hitPos)
{
    unsigned char blocked = 0;
    VECTOR dir;
    dir.x = hitPos->x - (int)g_playerEntityPointer.scaMatrixData.localMatrix.t[0];
    dir.y = hitPos->y - (int)g_playerEntityPointer.scaMatrixData.localMatrix.t[1];
    dir.z = hitPos->z - (int)g_playerEntityPointer.scaMatrixData.localMatrix.t[2];

    for (unsigned char layer = 3; layer != 0xFF; layer--) {
        blocked |= (unsigned char)room_check_sight_blocked(&dir, layer);
    }
    return blocked;
}
extern void MovePlayerXZ(int angle, SVECTOR* offset, SVECTOR* out);  // movement helper

// ---- 2D cross product helper ----
static int compute_2d_cross_product(int x1, int z1, int x2, int z2)
{
    return z2 * x1 - x2 * z1;
}

// ---- Weapon damage data tables ----
extern void* PTR_weapons_hit_detection_functions[10];
extern void* PTR_post_hit_callbacks[10];
extern unsigned int weapons_ranges[20];
extern short weapons_damage_table[30];
extern unsigned char weapon_hit_state_tbl_easy[30];
extern unsigned char weapon_hit_state_tbl_normal[30];
extern short weapon_damage_tbl_normal[30];

// ============================================================================
// checkEntityInRangeCone @ 0x0043d590
// Checks if an entity lies within a triangular aim cone in front of the
// player. Uses 2D cross products to test if the entity position falls
// between the left and right cone boundaries, and within the far edge.
// Keeps track of the closest entity via g_playerDisplacement.
// ============================================================================
static unsigned char checkEntityInRangeCone(short* leftBound, short* rightBound, Entity* enemy)
{
    int dist_x = *(int*)&enemy->scaMatrixData.localMatrix.t[0]
               - (int)g_playerEntityPointer.scaMatrixData.localMatrix.t[0];
    int dist_z = *(int*)&enemy->scaMatrixData.localMatrix.t[2]
               - (int)g_playerEntityPointer.scaMatrixData.localMatrix.t[2];

    // Check entity is to the LEFT of the right boundary
    int cross = compute_2d_cross_product(
        (int)g_svecScratch.x, (int)g_svecScratch.z,
        dist_x - *leftBound, dist_z - leftBound[2]);
    if (cross > 0) return 0;

    // Check entity is to the RIGHT of the left boundary
    cross = compute_2d_cross_product(
        (int)g_svecScratch.x, (int)g_svecScratch.z,
        dist_x - *rightBound, dist_z - rightBound[2]);
    if (cross < 0) return 0;

    // Check entity is BEFORE the far edge
    cross = compute_2d_cross_product(
        (int)*leftBound - (int)*rightBound, (int)leftBound[2] - (int)rightBound[2],
        dist_x - (int)g_svecScratch.x, dist_z - (int)g_svecScratch.z);
    if (cross > 0) return 0;

    // Entity is inside the cone — keep closest
    unsigned int distance = SquareRoot0(dist_z * dist_z + dist_x * dist_x);
    if (distance < g_playerDisplacement) {
        g_playerDisplacement = distance;
        return 1;
    }
    return 0;
}

// ============================================================================
// weapon_hit_detect_knife @ 0x0043d690
// Knife hit detection. Computes knife reach from the player's weapon joint,
// adjusts range per enemy type, and checks Euclidean distance. Returns 1
// if the entity is the closest within knife reach.
// ============================================================================
static unsigned char weapon_hit_detect_knife(short range, Entity* enemy)
{
    short offsets[6] = { 0x99, 0, 0, -0x17C, 0, 0 };

    g_matrixScratch = g_identityMatrixData;

    unsigned int pid = (unsigned int)(g_playerEntityPointer.id & 1);
    g_matrixScratch.t[0] = (int)offsets[pid * 3];
    g_matrixScratch.t[1] = (int)offsets[pid * 3 + 1];
    g_matrixScratch.t[2] = (int)offsets[pid * 3 + 2];

    SVECTOR knifePos;
    ApplyLVAndMul0Matrix(&g_playerEntityPointer.jointsStructs[0xE].world,
                         &g_matrixScratch, (SVECTOR*)&knifePos);

    int dist_x = *(int*)&enemy->scaMatrixData.localMatrix.t[0] - (int)knifePos.x;
    int dist_z = *(int*)&enemy->scaMatrixData.localMatrix.t[2] - (int)knifePos.z;

    unsigned int effectiveRange = (unsigned int)*(short*)(*(int*)((char*)enemy + 4) + 10) + range;
    unsigned char enemyId = *(unsigned char*)((char*)enemy + 1);

    // Zombies — skip if player aiming down
    if ((enemyId == 0 || enemyId == 1 || enemyId == 0x11)
        && (g_playerEntityPointer.flags & 0x80) != 0)
        return 0;

    // Cerberus / Web Spinner — skip if player aiming up and enemy on ground
    if (((enemyId == 2 && *(int*)&enemy->scaMatrixData.localMatrix.t[1] > -400) || enemyId == 3)
        && (g_playerEntityPointer.flags & 0x40) != 0)
        return 0;

    // Crow / Bee / Chimera on ceiling
    if ((enemyId == 5 || enemyId == 7 || enemyId == 9)
        && *(int*)&enemy->scaMatrixData.localMatrix.t[1] < -4800)
        return 0;

    // Per-enemy range adjustments
    if (enemyId == 4)              effectiveRange -= 800;   // black tiger
    if (enemyId == 8)              effectiveRange += 2000;  // plant 42
    if (enemyId == 7 || enemyId == 10) effectiveRange += 100;  // bee / adder

    unsigned int distance = SquareRoot0(dist_z * dist_z + dist_x * dist_x);
    if (distance < effectiveRange && distance < g_playerDisplacement) {
        g_playerDisplacement = distance;
        return 1;
    }
    return 0;
}

// ============================================================================
// weapon_hit_detect_gun @ 0x0043d410
// Gun hit detection for handgun, shotgun, magnum, and grenade launcher.
// Creates a triangular aim cone in front of the player. The cone height
// shifts up (offset 0 vs 50) when aiming up (player flags & 0x20).
// Two cone tiers: near (50 depth) and far (200 to range depth).
// Calls checkEntityInRangeCone to test if the enemy is inside the cone.
// ============================================================================
static unsigned char weapon_hit_detect_gun(short range, Entity* enemy)
{
    short enemyRadius = *(short*)(*(int*)((char*)enemy + 4) + 10);
    unsigned char enemyId = *(unsigned char*)((char*)enemy + 1);

    // Zombies — skip if aiming down and NOT shotgun (weapon index 2)
    if ((enemyId == 0 || enemyId == 1 || enemyId == 0x11)
        && (g_playerEntityPointer.flags & 0x80) != 0
        && g_scaled_down_dist != 2)
        return 0;

    // Per-enemy range adjustments
    if (enemyId == 4)  range -= 1000;   // black tiger
    if (enemyId == 8)  range += 2000;   // plant 42

    // Cone height: 50 normal, 0 when aiming up (headshot cone)
    g_svecScratch.x = 50;
    if ((g_playerEntityPointer.flags & 0x20) != 0)
        g_svecScratch.x = 0;

    // Near cone boundaries
    g_svecScratch.z = enemyRadius + 200;
    g_svecScratch.y = 0;
    SVECTOR nearLeft, nearRight;
    MovePlayerXZ(g_playerEntityPointer.directionAngle, &g_svecScratch, &nearLeft);

    g_svecScratch.z = -200 - enemyRadius;
    MovePlayerXZ(g_playerEntityPointer.directionAngle, &g_svecScratch, &nearRight);

    // Far cone boundaries
    g_svecScratch.x = 0x28A;
    g_svecScratch.z = enemyRadius + range;
    SVECTOR farLeft, farRight;
    MovePlayerXZ(g_playerEntityPointer.directionAngle, &g_svecScratch, &farLeft);

    g_svecScratch.z = -(enemyRadius + range);
    MovePlayerXZ(g_playerEntityPointer.directionAngle, &g_svecScratch, &farRight);

    g_svecScratch.z = 0;
    MovePlayerXZ(g_playerEntityPointer.directionAngle, &g_svecScratch, &g_svecScratch);

    // Check near cone tier
    if (checkEntityInRangeCone((short*)&nearLeft, (short*)&nearRight, enemy))
        return 1;

    // Check far cone tier
    if (checkEntityInRangeCone((short*)&farLeft, (short*)&farRight, enemy))
        return 1;

    return 0;
}

// ============================================================================
// weapon_hit_detect_projectile @ 0x0043d810
// Projectile hit detection for grenade launcher and heavy weapons.
// Simple Euclidean distance check with per-enemy range adjustments.
// ============================================================================
static unsigned char weapon_hit_detect_projectile(short range, Entity* enemy)
{
    int dist_x = *(int*)&enemy->scaMatrixData.localMatrix.t[0]
               - (int)g_playerEntityPointer.scaMatrixData.localMatrix.t[0];
    int dist_z = *(int*)&enemy->scaMatrixData.localMatrix.t[2]
               - (int)g_playerEntityPointer.scaMatrixData.localMatrix.t[2];

    unsigned int effectiveRange = (unsigned int)*(short*)(*(int*)((char*)enemy + 4) + 10) + range;

    if (*(unsigned char*)((char*)enemy + 1) == 4)   // black tiger
        effectiveRange -= 1000;

    unsigned int distance = SquareRoot0(dist_z * dist_z + dist_x * dist_x);
    if (distance < effectiveRange && distance < g_playerDisplacement) {
        g_playerDisplacement = distance;
        return 1;
    }
    return 0;
}

// ============================================================================
// apply_weapon_damage @ 0x0043c020
// Real-time weapon hit detection and damage application. Iterates all
// active enemies, calls per-weapon hit detection to find the closest
// target in range+FOV, then subtracts weapon damage from health and
// transitions the enemy to damaged (state 2) or dead (state 3).
// ============================================================================
unsigned char apply_weapon_damage(unsigned int weapon_id)
{
    unsigned char activeIdx[32];
    unsigned char enemyCount = g_enemy_count;

    if (enemyCount == 0) return 0;

    // First pass: collect indices of all active enemies (reverse order)
    {
        Entity* ent = g_EnemiesList;
        unsigned char slot = 0;
        unsigned char remaining = enemyCount;
        do {
            if (ent->status_flags != 0) {
                activeIdx[--remaining] = slot;
            }
            if (remaining == 0) break;
            ent = (Entity*)((char*)ent + sizeof(Entity));
            slot++;
        } while (slot < 30);
    }

    g_playerDisplacement = 0x7FFFFFFF;

    unsigned char weaponAdj = (unsigned char)(weapon_id - 1);
    g_scaled_down_dist = weaponAdj;  // set global so hit detection callbacks can read it
    unsigned int wpnRange = weapons_ranges[
        (unsigned int)weaponAdj + (unsigned int)(g_playerEntityPointer.id & 1) * 10];

    Entity* enemy = NULL;

    // Second pass: check each active enemy for weapon hit
    unsigned char idx = enemyCount;
    while (idx != 0) {
        idx--;
        Entity* candidate = &g_EnemiesList[activeIdx[0]];

        if ((candidate->status_flags & g_playerEntityPointer.flags & 0xE0) != 0
            && candidate->hit_state == 0)
        {
            typedef unsigned char (*hitDetectFn)(short range, Entity* ent);
            hitDetectFn detector = (hitDetectFn)PTR_weapons_hit_detection_functions[weaponAdj];
            if (detector((short)wpnRange, candidate) != 0) {
                enemy = candidate;
            }
        }
        activeIdx[0] = activeIdx[idx];
    }

    if (enemy == NULL || (check_weapon_line_of_sight((VECTOR*)enemy->scaMatrixData.localMatrix.t) && weaponAdj < 5)) {
        return 0;
    }

    unsigned char enemyType = enemy->id;
    if (enemyType >= 0x14) return 0;

    unsigned int tableIdx = (unsigned int)weaponAdj + (unsigned int)enemyType * 10;

    unsigned char hitState;
    short damage;
    if (Flg_ck((int)g_PlayerFlags, 0x7b) == 0) {
        hitState = weapon_hit_state_tbl_easy[tableIdx];
        damage = weapons_damage_table[tableIdx];
    } else {
        hitState = weapon_hit_state_tbl_normal[tableIdx];
        damage = weapon_damage_tbl_normal[tableIdx];
    }

    enemy->health -= damage;

    if ((g_playerEntityPointer.flags & 0xE0) != 0x20) {
        hitState += (g_playerEntityPointer.flags >> 5);
    }
    hitState |= (unsigned char)(weapon_id << 3);
    enemy->hit_state = hitState;

    typedef void (*postHitFn)(Entity* ent);
    postHitFn postHit = (postHitFn)PTR_post_hit_callbacks[weaponAdj];
    postHit(enemy);

    enemy->state = 3;
    enemy->ignore_player_flag = 0;
    enemy->action_behavior = 0;
    enemy->action_state = 0;

    if (enemy->health >= 0) {
        enemy->state = 2;
        enemy->ignore_player_flag = 0;
        enemy->action_behavior = 0;
        enemy->action_state = 0;
    }

    return 1;
}

// ---- Stub data tables ----
void* PTR_weapons_hit_detection_functions[10] = {};
void* PTR_post_hit_callbacks[10] = {};
unsigned int weapons_ranges[20] = {};
short weapons_damage_table[30] = {};
unsigned char weapon_hit_state_tbl_easy[30] = {};
unsigned char weapon_hit_state_tbl_normal[30] = {};
short weapon_damage_tbl_normal[30] = {};

// ============================================================================
// MovePlayerXZ (0x0041b350)
// Rotate `offset` by a yaw angle about Y and write it to `out`.
//
// This was an empty stub with no address recorded, and it is the single reason
// door and item action zones never fired on approach: update_player_position
// builds its 600-unit reach probe by calling this with the SAME buffer as both
// input and output, so with the stub in place the probe came back unrotated as
// (600, 0, 0). Chris then had to be walked far enough that a due-east probe
// happened to land in the zone. The real address was found from the call site at
// 0x0041c087, not from any comment.
//
// The original builds a rotation SVECTOR with only .y set (.x and .z zeroed),
// hands it to RotMatrix, then routes the result through the g_playerPosScratch
// VECTOR before narrowing to shorts:
//
//   0041b358: MOV word ptr [ESP+0x2],CX      ; local.y = angle
//   0041b373: CALL 0x00409df0                ; RotMatrix(&local, &g_matrixScratch)
//   0041b38a: CALL 0x00409cd0                ; ApplyMatrix(&g_matrixScratch, offset, 0x00be11b0)
//   0041b396: MOV EDX,dword ptr [0x00be11b0] ; out->x = (short)result.x  (etc)
//
// Using g_playerPosScratch as the intermediate is faithful and safe: the caller
// overwrites it with the final probe immediately afterwards. The angle is read as
// a 16-bit value in the original (`MOV CX, word ptr [ESP+4]`) even though callers
// push a dword, so it is truncated here.
// ============================================================================
void MovePlayerXZ(int angle, SVECTOR* offset, SVECTOR* out)
{
    SVECTOR rot;
    rot.x = 0;
    rot.y = (short)angle;
    rot.z = 0;

    RotMatrix(&rot, &g_matrixScratch);
    ApplyMatrix(&g_matrixScratch, offset, &g_playerPosScratch);

    out->x = (short)g_playerPosScratch.x;
    out->y = (short)g_playerPosScratch.y;
    out->z = (short)g_playerPosScratch.z;
}

// ---- Stub functions ----
