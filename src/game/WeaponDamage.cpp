// WeaponDamage.cpp — Real-time weapon hit detection and damage application
// apply_weapon_damage @ 0x0043c020
// Called each frame when the player fires a weapon. Iterates all active
// enemies, calls per-weapon hit detection callbacks (knife cone, gun cone,
// projectile distance) to find the closest target in range+FOV, then
// subtracts damage from the enemy's health and sets the hit reaction state.
#include "../Globals.h"

// ---- Global scratch variable (set by apply_weapon_damage before hit detection) ----
extern int g_scaled_down_dist;  // holds weapon_id - 1 during hit detection
extern unsigned int g_entity_bkp;   // 0x00be0df4 - shared scratch (health snapshot)

// ---- Forward declarations (post-hit callbacks + reactions, defined at the end) ----
static void weapon_post_hit_knife(Entity* enemy);      // 0x0043c290
static void weapon_post_hit_reaction(Entity* enemy);   // 0x0043c350
static void weapon_post_hit_shotgun(Entity* enemy);    // 0x0043c370
static void weapon_post_hit_blood(Entity* enemy);      // 0x0043c3b0
static void weapon_post_hit_blood2(Entity* enemy);     // 0x0043c770
static void weapon_post_hit_sparks(Entity* enemy);     // 0x0043ca30
static void weapon_post_hit_blood3(Entity* enemy);     // 0x0043cc90
static void enemy_hit_reaction_zombie(Entity* enemy);  // 0x0043d060
static void enemy_hit_reaction_basic(Entity* enemy);   // 0x0043d2c0
static void enemy_hit_reaction_head(Entity* enemy);    // 0x0043d300
static void enemy_hit_reaction_blood(Entity* enemy);   // 0x0043d3a0
static void enemy_hit_reaction_none(Entity* enemy);    // 0x0043d400

// ---- Forward declarations ----
// room_check_sight_blocked (0x0047db90) comes from Globals.h.

// ============================================================================
// check_weapon_line_of_sight @ 0x0048a530
// Multi-layer room obstruction check for weapon hits. Computes the vector
// from the player to the hit position, then checks 4 room partition layers
// (indices 3 down to 0) via room_check_sight_blocked. Returns 0 only if
// all layers are clear — the shot has an unobstructed path.
// ============================================================================
unsigned char check_weapon_line_of_sight(VECTOR* hitPos)
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

// 0x004bb698 - easy difficulty hit records, 12 bytes each, indexed
// (weaponAdj + enemyType * 10) * 12. The knockback vector (kx/ky/kz) is used
// for BOTH difficulties; damage/hit-state come from these or the normal table
// below depending on Flg_ck(g_PlayerFlags, 0x7b).
typedef struct {
    short         kx;      // +0x00
    short         ky;      // +0x02
    short         kz;      // +0x04
    short         dmg;     // +0x06: easy damage
    unsigned char type;    // +0x08: hit type for the post-hit billboard
    unsigned char data;    // +0x09: hit data for the post-hit billboard
    unsigned char hit;     // +0x0A: easy hit state
    unsigned char pad;     // +0x0B
} WeaponHitRecord;

// 0x004bbffe - normal difficulty records, 12 bytes each. apply_weapon_damage
// reads the damage short at +0 and the hit-state byte at +4.
typedef struct {
    short         dmg;     // +0x00: normal damage
    short         unk_02;  // +0x02
    unsigned char hit;     // +0x04: normal hit state
    unsigned char unk_05;  // +0x05
    short         kx;      // +0x06
    short         ky;      // +0x08
    short         kz;      // +0x0A
} WeaponHitRecordNormal;

extern WeaponHitRecord       g_weaponHitRecordsEasy[200];
extern WeaponHitRecordNormal g_weaponHitRecordsNormal[200];

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
    if (enemyType >= 0x14) return enemy->id;   // matches the original (returns the enemy id)

    unsigned int tableIdx = (unsigned int)weaponAdj + (unsigned int)enemyType * 10;

    // 12-byte hit record lookup. The knockback vector (kx/ky/kz) and the
    // post-hit inputs (type/data) come from the easy records for BOTH
    // difficulties - only damage and hit-state differ (verified against the
    // original disassembly of 0x0043c020).
    WeaponHitRecord* rec = &g_weaponHitRecordsEasy[tableIdx];
    g_playerPosScratch.x = (int)rec->kx;                    // 0x00be11b0
    g_playerPosScratch.y = (int)rec->ky;
    g_playerPosScratch.z = (int)rec->kz;
    g_weaponHitEnemyType = enemyType;                       // 0x00be0de4
    g_collPushDepthZHi   = rec->type;                       // 0x00be0dec (shared scratch)
    g_collPushDepthZLo   = rec->data;                       // 0x00be0df0 (shared scratch)
    g_entity_bkp         = (unsigned int)(int)enemy->health;// 0x00be0df4 (shared scratch)

    unsigned char hitState;
    short damage;
    if (Flg_ck((int)g_PlayerFlags, 0x7b) == 0) {
        hitState = rec->hit;                                // easy hit-state @ +10
        damage = rec->dmg;                                  // easy damage @ +6
    } else {
        hitState = g_weaponHitRecordsNormal[tableIdx].hit;  // normal hit-state @ +4
        damage = g_weaponHitRecordsNormal[tableIdx].dmg;    // normal damage @ +0
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

// ============================================================================
// Weapon damage data tables (ROM .data, dumped byte-for-byte from the exe)
// ============================================================================

// 0x004bb530 - per-weapon hit detection callbacks (weaponAdj = weapon_id - 1)
void* PTR_weapons_hit_detection_functions[10] = {
    (void*)weapon_hit_detect_knife,     // 0x0043d690 - knife
    (void*)weapon_hit_detect_gun,       // 0x0043d410 - handgun / shotgun / python / magnum
    (void*)weapon_hit_detect_gun,
    (void*)weapon_hit_detect_gun,
    (void*)weapon_hit_detect_gun,
    (void*)weapon_hit_detect_projectile,// 0x0043d810 - heavy weapons
    (void*)weapon_hit_detect_projectile,
    (void*)weapon_hit_detect_projectile,
    (void*)weapon_hit_detect_projectile,
    (void*)weapon_hit_detect_projectile,
};

// 0x004bb558 - post-hit callbacks (called with the hit enemy, no null check)
void* PTR_post_hit_callbacks[10] = {
    (void*)weapon_post_hit_knife,       // 0x0043c290
    (void*)weapon_post_hit_reaction,    // 0x0043c350
    (void*)weapon_post_hit_shotgun,     // 0x0043c370
    (void*)weapon_post_hit_shotgun,
    (void*)weapon_post_hit_shotgun,
    (void*)weapon_post_hit_blood,       // 0x0043c3b0
    (void*)weapon_post_hit_blood2,      // 0x0043c770
    (void*)weapon_post_hit_sparks,      // 0x0043ca30
    (void*)weapon_post_hit_blood,       // 0x0043c3b0
    (void*)weapon_post_hit_blood3,      // 0x0043cc90
};

// 0x004bb580 - per-enemy-type hit reactions (indexed by enemy->id)
void* g_enemy_hit_reactions[20] = {
    (void*)enemy_hit_reaction_zombie,   // 0x0043d060
    (void*)enemy_hit_reaction_zombie,
    (void*)enemy_hit_reaction_zombie,
    (void*)enemy_hit_reaction_basic,    // 0x0043d2c0
    (void*)enemy_hit_reaction_basic,
    (void*)enemy_hit_reaction_basic,
    (void*)enemy_hit_reaction_head,     // 0x0043d300
    (void*)enemy_hit_reaction_basic,
    (void*)enemy_hit_reaction_blood,    // 0x0043d3a0
    (void*)enemy_hit_reaction_none,     // 0x0043d400
    (void*)enemy_hit_reaction_basic,
    (void*)enemy_hit_reaction_none,
    (void*)enemy_hit_reaction_none,
    (void*)enemy_hit_reaction_none,
    (void*)enemy_hit_reaction_none,
    (void*)enemy_hit_reaction_none,
    (void*)enemy_hit_reaction_none,
    (void*)enemy_hit_reaction_zombie,
    (void*)enemy_hit_reaction_none,
    (void*)enemy_hit_reaction_none,
};

// 0x004bb5d0 - per-enemy-type joint index lists for the blood-spurt effects
// (6 bytes each, read by weapon_post_hit_blood2)
unsigned char g_enemyHitJointLists[20][6] = {
    { 2, 3, 4, 5, 7, 8 },
    { 3, 4, 5, 6, 7, 8 },
    { 2, 3, 4, 5, 6, 8 },
    { 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0 },
    { 1, 2, 4, 5, 8, 9 },
    { 4, 5, 6, 7, 8, 9 },
    { 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0 },
    { 8, 4, 5, 6, 9, 10 },
    { 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0 },
    { 2, 4, 5, 6, 7, 8 },
    { 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0 },
};

// 0x004bb648 - per-weapon ranges, DWORDs. First 10 = Chris, second 10 = Jill
// (weaponAdj + (player.id & 1) * 10)
unsigned int weapons_ranges[20] = {
    400, 1200, 2600, 800, 800,
    400, 900, 900, 900, 1100,
    500, 1300, 2600, 800, 800,
    400, 900, 900, 900, 1100,
};

// 0x004bb698 - easy difficulty hit records (WeaponHitRecord, see top of file)
WeaponHitRecord g_weaponHitRecordsEasy[200] = {
    {   100,  -1800,   0,    8,   0,   0,   1, 0 }, {   100,  -2620,   0,    9,   0,   1,   1, 0 },
    {   100,  -2500,   0,   53,   4,   0,   2, 0 }, {   100,  -2620,   0,   50,   4,   0,   2, 0 },
    {   100,  -2620,   0,  130,   4,   0,   2, 0 }, {   150,  -1500,   0,   20,  14,   6,   1, 0 },
    {   150,  -1620,   0,  201,   0,   0,   2, 0 }, {   150,  -1520,   0,   95,   9,   0,   1, 0 },
    {   150,  -1500,   0,   95,  14,   6,   1, 0 }, {   150,  -1620,   0,  900,   0,   6,   2, 0 },
    {   100,  -1800,   0,    8,   0,   0,   1, 0 }, {   100,  -2620,   0,    9,   0,   1,   1, 0 },
    {   100,  -2500,   0,   53,   4,   0,   2, 0 }, {   100,  -2620,   0,   50,   4,   0,   2, 0 },
    {   100,  -2620,   0,  130,   4,   0,   2, 0 }, {   150,  -1500,   0,   20,  14,   6,   1, 0 },
    {   150,  -1620,   0,  201,   0,   0,   2, 0 }, {   150,  -1520,   0,   95,   9,   0,   1, 0 },
    {   150,  -1500,   0,   95,  14,   6,   1, 0 }, {   150,  -1620,   0,  900,   0,   6,   2, 0 },
    {   100,  -1200,   0,   30,   0,   0,   1, 0 }, {   100,  -1200,   0,   20,   0,   1,   1, 0 },
    {   100,  -1200,   0,   40,   4,   0,   2, 0 }, {   100,  -1200,   0,   60,   4,   0,   2, 0 },
    {   100,  -1200,   0,  130,   4,   0,   2, 0 }, {   100,      0,   0,   20,  14,   6,   1, 0 },
    {   100,   -100,   0,  200,   0,   0,   2, 0 }, {   100,      0,   0,  100,   9,   0,   1, 0 },
    {   100,      0,   0,  100,  14,   6,   1, 0 }, {   100,   -100,   0,  900,   0,   6,   2, 0 },
    {     0,  -1200,   0,   10,   0,   8,   1, 0 }, {     0,  -1220,   0,   20,   0,   9,   1, 0 },
    {     0,  -1100,   0,   40,   0,   8,   2, 0 }, {     0,  -1220,   0,   40,   0,   8,   2, 0 },
    {     0,  -1220,   0,  130,   0,   8,   2, 0 }, {     0,  -1100,   0,   20,  14,   7,   1, 0 },
    {     0,  -1120,   0,  100,   0,   0,   2, 0 }, {     0,  -1120,   0,  100,   9,   0,   1, 0 },
    {     0,  -1100,   0,  200,  14,   7,   1, 0 }, {     0,  -1120,   0,  900,   0,   7,   2, 0 },
    {     0,   -900,   0,   10,   0,   8,   1, 0 }, {     0,   -920,   0,   14,   0,   9,   1, 0 },
    {     0,   -800,   0,   40,   0,   8,   2, 0 }, {     0,   -920,   0,   50,   0,   8,   2, 0 },
    {     0,   -920,   0,   70,   0,   8,   2, 0 }, {     0,   -800,   0,   20,  14,   7,   1, 0 },
    {     0,   -820,   0,   60,   0,   0,   2, 0 }, {     0,   -820,   0,   60,   9,   0,   1, 0 },
    {     0,   -800,   0,  205,  14,   7,   1, 0 }, {     0,   -820,   0,  900,   0,   7,   2, 0 },
    {     0,      0,   0,   50,   0,   0,   1, 0 }, {     0,      0,   0,   26,   0,   0,   1, 0 },
    {     0,      0,   0,   50,   0,   0,   2, 0 }, {     0,      0,   0,   50,   0,   0,   2, 0 },
    {     0,      0,   0,  130,   0,   0,   2, 0 }, {     0,      0,   0,   20,  14,   5,   1, 0 },
    {     0,      0,   0,  200,   0,   0,   2, 0 }, {     0,      0,   0,   60,   9,   0,   1, 0 },
    {     0,      0,   0,   60,  14,   5,   1, 0 }, {     0,      0,   0,  900,   0,   5,   2, 0 },
    {     0,  -1500,   0,   16,   9,   6,   1, 0 }, {     0,  -1500,   0,   14,   9,   6,   1, 0 },
    {     0,  -1500,   0,   32,   9,   6,   2, 0 }, {     0,  -1500,   0,   40,   9,   6,   2, 0 },
    {     0,  -1500,   0,  130,   9,   6,   2, 0 }, {   150,  -1500,   0,   20,  14,   6,   1, 0 },
    {   150,  -1500,   0,  100,   0,   0,   2, 0 }, {   150,  -1500,   0,  200,   9,   0,   1, 0 },
    {   150,  -1500,   0,  100,  14,   6,   1, 0 }, {   150,  -1500,   0,  900,   0,   6,   2, 0 },
    {     0,      0,   0,   20,   0,  16,   1, 0 }, {     0,      0,   0,   30,   0,  17,   1, 0 },
    {     0,      0,   0,   60,   0,  16,   2, 0 }, {     0,      0,   0,   70,   0,  16,   2, 0 },
    {     0,      0,   0,  130,   0,  16,   2, 0 }, {     0,      0,   0,   20,  14,   4,   1, 0 },
    {     0,      0,   0,  200,   0,   0,   2, 0 }, {     0,      0,   0,   80,   9,   0,   1, 0 },
    {     0,      0,   0,   80,  14,   4,   1, 0 }, {     0,      0,   0,  900,   0,   4,   2, 0 },
    {     0,      0,   0,   15,   1,   0,   1, 0 }, {     0,   1000,   0,   15,   0,   0,   1, 0 },
    {     0,   1500,   0,   20,   0,   0,   2, 0 }, {     0,   1000,   0,   38,   0,   0,   2, 0 },
    {     0,   1000,   0,   74,   0,   0,   2, 0 }, {     0,   1500,   0,   20,  14,   6,   1, 0 },
    {     0,   1500,   0,   50,   2,   0,   2, 0 }, {     0,   1500,   0,   40,   9,   0,   1, 0 },
    {     0,   1500,   0,  150,  14,   6,   1, 0 }, {     0,   1500,   0,  900,   2,   6,   2, 0 },
    {     0,      0,   0,   17,   0,   0,   1, 0 }, {     0,      0,   0,   20,   1,   0,   1, 0 },
    {     0,      0,   0,   30,   1,   0,   2, 0 }, {     0,      0,   0,   40,   1,   0,   2, 0 },
    {     0,      0,   0,  130,   1,   0,   2, 0 }, {   150,  -1500,   0,   20,  14,   6,   1, 0 },
    {   150,  -1500,   0,  200,   0,   0,   2, 0 }, {   150,  -1500,   0,   60,   9,   0,   1, 0 },
    {   150,  -1500,   0,   60,  14,   6,   1, 0 }, {   150,  -1500,   0,  900,   0,   6,   2, 0 },
    {     0,      0,   0,   20,   0,   0,   1, 0 }, {     0,      0,   0,   20,   0,   0,   1, 0 },
    {     0,      0,   0,   40,   0,   0,   2, 0 }, {     0,      0,   0,   50,   0,   0,   2, 0 },
    {     0,      0,   0,  130,   0,   0,   2, 0 }, {     0,      0,   0,   30,  14,   3,   1, 0 },
    {     0,      0,   0,  200,   0,   0,   2, 0 }, {     0,      0,   0,   60,   9,   0,   1, 0 },
    {     0,      0,   0,   60,  14,   3,   1, 0 }, {     0,      0,   0,  900,   0,   3,   2, 0 },
    {     0,      0,   0,    0,   1,   0,   1, 0 }, {     0,      0,   0,    0,   1,   0,   1, 0 },
    {     0,      0,   0,    0,   1,   0,   1, 0 }, {     0,      0,   0,    0,   1,   0,   1, 0 },
    {     0,      0,   0,    0,   1,   0,   1, 0 }, {     0,  -1500,   0,    0,  14,   7,   1, 0 },
    {     0,  -1500,   0,    0,   0,   0,   2, 0 }, {     0,  -1500,   0,    0,   9,   0,   1, 0 },
    {     0,  -1500,   0,    0,  14,   7,   1, 0 }, {     0,  -1500,   0,    0,   0,   7,   2, 0 },
    {     0,      0,   0,   10,   0,   0,   1, 0 }, {     0,      0,   0,   20,   1,   0,   1, 0 },
    {     0,      0,   0,   30,   1,   0,   2, 0 }, {     0,      0,   0,   50,   1,   0,   2, 0 },
    {     0,      0,   0,   80,   1,   0,   2, 0 }, {   150,  -2000,   0,   20,   2,   6,   1, 0 },
    {   150,  -2000,   0,  100,   2,   0,   2, 0 }, {   150,  -2000,   0,  100,   2,   0,   1, 0 },
    {   150,  -2000,   0,  100,   2,   6,   1, 0 }, {   150,  -2000,   0,  900,   2,   6,   2, 0 },
    {     0,      0,   0,   20,   0,   8,   1, 0 }, {     0,      0,   0,   30,   1,   0,   1, 0 },
    {     0,      0,   0,   40,   1,   0,   2, 0 }, {     0,      0,   0,   40,   1,   0,   2, 0 },
    {     0,      0,   0,   80,   1,   0,   2, 0 }, {     0,      0,   0,   20,   2,   7,   1, 0 },
    {     0,      0,   0,   80,   2,   0,   2, 0 }, {     0,      0,   0,  130,   2,   0,   1, 0 },
    {     0,      0,   0,   50,   2,   7,   1, 0 }, {     0,      0,   0,  900,   2,   7,   2, 0 },
    {     0,      0,   0,    0,   1,   0,   0, 0 }, {     0,      0,   0,    0,   1,   0,   0, 0 },
    {     0,      0,   0,    0,   1,   0,   0, 0 }, {     0,      0,   0,    0,   1,   0,   0, 0 },
    {     0,      0,   0,    0,   1,   0,   0, 0 }, {     0,      0,   0,    0,   1,   0,   0, 0 },
    {     0,      0,   0,    0,   1,   0,   0, 0 }, {     0,      0,   0,    0,   1,   0,   0, 0 },
    {     0,      0,   0,    0,   1,   0,   0, 0 }, {     0,      0,   0,    0,   1,   0,   0, 0 },
    {     0,      0,   0,    0,   1,   0,   0, 0 }, {     0,      0,   0,    0,   1,   0,   0, 0 },
    {     0,      0,   0,    0,   1,   0,   0, 0 }, {     0,      0,   0,    0,   1,   0,   0, 0 },
    {     0,      0,   0,    0,   1,   0,   0, 0 }, {     0,      0,   0,    0,   1,   0,   0, 0 },
    {     0,      0,   0,    0,   1,   0,   0, 0 }, {     0,      0,   0,    0,   1,   0,   0, 0 },
    {     0,      0,   0,    0,   1,   0,   0, 0 }, {     0,      0,   0,    0,   1,   0,   0, 0 },
    {     0,      0,   0,   10,   0,   0,   1, 0 }, {     0,      0,   0,   20,   1,   0,   1, 0 },
    {     0,      0,   0,   30,   1,   0,   2, 0 }, {     0,      0,   0,   50,   1,   0,   2, 0 },
    {     0,      0,   0,   80,   1,   0,   2, 0 }, {   150,  -2000,   0,   20,   2,   6,   1, 0 },
    {   150,  -2000,   0,  100,   2,   0,   2, 0 }, {   150,  -2000,   0,  100,   2,   0,   1, 0 },
    {   150,  -2000,   0,  100,   2,   6,   1, 0 }, {   150,  -2000,   0,  900,   2,   6,   2, 0 },
    {   100,  -1800,   0,    8,   0,   0,   1, 0 }, {   100,  -2620,   0,    9,   0,   1,   1, 0 },
    {   100,  -1500,   0,   53,   4,   0,   2, 0 }, {   100,  -2620,   0,   50,   4,   0,   2, 0 },
    {   100,  -2620,   0,  130,   4,   0,   2, 0 }, {   150,  -2500,   0,   20,  14,   6,   1, 0 },
    {   150,  -1620,   0,  201,   0,   0,   2, 0 }, {   150,  -1520,   0,   95,   9,   0,   1, 0 },
    {   150,  -1500,   0,   95,  14,   6,   1, 0 }, {   150,  -1620,   0,  900,   0,   6,   2, 0 },
    {     0,      0,   0,   20,   0,   8,   1, 0 }, {     0,      0,   0,   30,   1,   0,   1, 0 },
    {     0,      0,   0,   40,   1,   0,   2, 0 }, {     0,      0,   0,   40,   1,   0,   2, 0 },
    {     0,      0,   0,   80,   1,   0,   2, 0 }, {     0,      0,   0,   20,   2,   7,   1, 0 },
    {     0,      0,   0,   80,   2,   0,   2, 0 }, {     0,      0,   0,  130,   2,   0,   1, 0 },
    {     0,      0,   0,   50,   2,   7,   1, 0 }, {     0,      0,   0,  900,   2,   7,   2, 0 },
    {     0,      0,   0,   10,   1,   0,   1, 0 }, {     0,      0,   0,    0,   1,   0,   1, 0 },
    {     0,      0,   0,    0,   1,   0,   2, 0 }, {     0,      0,   0,    0,   1,   0,   2, 0 },
    {     0,      0,   0,    0,   1,   0,   2, 0 }, {     0,      0,   0,    2,   2,   7,   1, 0 },
    {     0,      0,   0,   30,   2,   0,   2, 0 }, {     0,      0,   0,   30,   2,   0,   1, 0 },
    {     0,      0,   0,   30,   2,   7,   1, 0 }, {     0,      0,   0,  900,   2,   7,   2, 0 },
};

// 0x004bbffe - normal difficulty records (WeaponHitRecordNormal, top of file)
WeaponHitRecordNormal g_weaponHitRecordsNormal[200] = {
    {    8,    0,   1, 0,   100,  -2620,   0 }, {    9,  256,   1, 0,   100,  -2500,   0 },
    {   20,    4,   2, 0,   100,  -2620,   0 }, {   50,    4,   2, 0,   100,  -2620,   0 },
    {   60,    4,   2, 0,   150,  -1500,   0 }, {   20, 1550,   1, 0,   150,  -1620,   0 },
    {  201,    0,   2, 0,   150,  -1520,   0 }, {   70,    9,   1, 0,   150,  -1500,   0 },
    {   70, 1550,   1, 0,   150,  -1620,   0 }, {  900, 1536,   2, 0,   100,  -1800,   0 },
    {    8,    0,   1, 0,   100,  -2620,   0 }, {    9,  256,   1, 0,   100,  -2500,   0 },
    {   20,    4,   2, 0,   100,  -2620,   0 }, {   50,    4,   2, 0,   100,  -2620,   0 },
    {   60,    4,   2, 0,   150,  -1500,   0 }, {   20, 1550,   1, 0,   150,  -1620,   0 },
    {  201,    0,   2, 0,   150,  -1520,   0 }, {   70,    9,   1, 0,   150,  -1500,   0 },
    {   70, 1550,   1, 0,   150,  -1620,   0 }, {  900, 1536,   2, 0,   100,  -1200,   0 },
    {   30,    0,   1, 0,   100,  -1200,   0 }, {   20,  256,   1, 0,   100,  -1200,   0 },
    {   35,    4,   2, 0,   100,  -1200,   0 }, {   60,    4,   2, 0,   100,  -1200,   0 },
    {   60,    4,   2, 0,   100,      0,   0 }, {   20, 1550,   1, 0,   100,   -100,   0 },
    {  200,    0,   2, 0,   100,      0,   0 }, {   90,    9,   1, 0,   100,      0,   0 },
    {   90, 1550,   1, 0,   100,   -100,   0 }, {  900, 1536,   2, 0,     0,  -1200,   0 },
    {   10, 2048,   1, 0,     0,  -1220,   0 }, {   15, 2304,   1, 0,     0,  -1100,   0 },
    {   24, 2048,   2, 0,     0,  -1220,   0 }, {   30, 2048,   2, 0,     0,  -1220,   0 },
    {   40, 2048,   2, 0,     0,  -1100,   0 }, {   20, 1806,   1, 0,     0,  -1120,   0 },
    {  100,    0,   2, 0,     0,  -1120,   0 }, {  100,    9,   1, 0,     0,  -1100,   0 },
    {  200, 1806,   1, 0,     0,  -1120,   0 }, {  900, 1792,   2, 0,     0,   -900,   0 },
    {   10, 2048,   1, 0,     0,   -920,   0 }, {   12, 2304,   1, 0,     0,   -800,   0 },
    {   20, 2048,   2, 0,     0,   -920,   0 }, {   50, 2048,   2, 0,     0,   -920,   0 },
    {   70, 2048,   2, 0,     0,   -800,   0 }, {   20, 1806,   1, 0,     0,   -820,   0 },
    {   50,    0,   2, 0,     0,   -820,   0 }, {   50,    9,   1, 0,     0,   -800,   0 },
    {  103, 1806,   1, 0,     0,   -820,   0 }, {  900, 1792,   2, 0,     0,      0,   0 },
    {   50,    0,   1, 0,     0,      0,   0 }, {   26,    0,   1, 0,     0,      0,   0 },
    {   50,    0,   2, 0,     0,      0,   0 }, {   50,    0,   2, 0,     0,      0,   0 },
    {   50,    0,   2, 0,     0,      0,   0 }, {   20, 1294,   1, 0,     0,      0,   0 },
    {  200,    0,   2, 0,     0,      0,   0 }, {   60,    9,   1, 0,     0,      0,   0 },
    {   60, 1294,   1, 0,     0,      0,   0 }, {  900, 1280,   2, 0,     0,  -1500,   0 },
    {   16, 1545,   1, 0,     0,  -1500,   0 }, {   14, 1545,   1, 0,     0,  -1500,   0 },
    {   25, 1545,   2, 0,     0,  -1500,   0 }, {   40, 1545,   2, 0,     0,  -1500,   0 },
    {   80, 1545,   2, 0,   150,  -1500,   0 }, {   20, 1550,   1, 0,   150,  -1500,   0 },
    {   50,    0,   2, 0,   150,  -1500,   0 }, {   80,    9,   1, 0,   150,  -1500,   0 },
    {   50, 1550,   1, 0,   150,  -1500,   0 }, {  900, 1536,   2, 0,     0,      0,   0 },
    {   20, 4096,   1, 0,     0,      0,   0 }, {   30, 4352,   1, 0,     0,      0,   0 },
    {   60, 4096,   2, 0,     0,      0,   0 }, {   70, 4096,   2, 0,     0,      0,   0 },
    {   70, 4096,   2, 0,     0,      0,   0 }, {   20, 1038,   1, 0,     0,      0,   0 },
    {  200,    0,   2, 0,     0,      0,   0 }, {   80,    9,   1, 0,     0,      0,   0 },
    {   80, 1038,   1, 0,     0,      0,   0 }, {  900, 1024,   2, 0,     0,      0,   0 },
    {   15,    1,   1, 0,     0,   1000,   0 }, {   10,    0,   1, 0,     0,   1500,   0 },
    {   20,    0,   2, 0,     0,   1000,   0 }, {   38,    0,   2, 0,     0,   1000,   0 },
    {   20,    0,   2, 0,     0,   1500,   0 }, {   20, 1550,   1, 0,     0,   1500,   0 },
    {   40,    2,   2, 0,     0,   1500,   0 }, {   40,    9,   1, 0,     0,   1500,   0 },
    {  150, 1550,   1, 0,     0,   1500,   0 }, {  900, 1538,   2, 0,     0,      0,   0 },
    {   10,    0,   1, 0,     0,      0,   0 }, {   12,    1,   1, 0,     0,      0,   0 },
    {   20,    1,   2, 0,     0,      0,   0 }, {   40,    1,   2, 0,     0,      0,   0 },
    {   41,    1,   2, 0,   150,  -1500,   0 }, {   20, 1550,   1, 0,   150,  -1500,   0 },
    {   60,    0,   2, 0,   150,  -1500,   0 }, {   60,    9,   1, 0,   150,  -1500,   0 },
    {  100, 1550,   1, 0,   150,  -1500,   0 }, {  900, 1536,   2, 0,     0,      0,   0 },
    {   20,    0,   1, 0,     0,      0,   0 }, {   20,    0,   1, 0,     0,      0,   0 },
    {   40,    0,   2, 0,     0,      0,   0 }, {   50,    0,   2, 0,     0,      0,   0 },
    {   50,    0,   2, 0,     0,      0,   0 }, {   30,  782,   1, 0,     0,      0,   0 },
    {  200,    0,   2, 0,     0,      0,   0 }, {   60,    9,   1, 0,     0,      0,   0 },
    {   60,  782,   1, 0,     0,      0,   0 }, {  900,  768,   2, 0,     0,      0,   0 },
    {    0,    1,   1, 0,     0,      0,   0 }, {    0,    1,   1, 0,     0,      0,   0 },
    {    0,    1,   1, 0,     0,      0,   0 }, {    0,    1,   1, 0,     0,      0,   0 },
    {    0,    1,   1, 0,     0,  -1500,   0 }, {    0, 1806,   1, 0,     0,  -1500,   0 },
    {    0,    0,   2, 0,     0,  -1500,   0 }, {    0,    9,   1, 0,     0,  -1500,   0 },
    {    0, 1806,   1, 0,     0,  -1500,   0 }, {    0, 1792,   2, 0,     0,      0,   0 },
    {   10,    0,   1, 0,     0,      0,   0 }, {   15,    1,   1, 0,     0,      0,   0 },
    {   20,    1,   2, 0,     0,      0,   0 }, {   50,    1,   2, 0,     0,      0,   0 },
    {   40,    1,   2, 0,   150,  -2000,   0 }, {   20, 1538,   1, 0,   150,  -2000,   0 },
    {   35,    2,   2, 0,   150,  -2000,   0 }, {   35,    2,   1, 0,   150,  -2000,   0 },
    {   35, 1538,   1, 0,   150,  -2000,   0 }, {  900, 1538,   2, 0,     0,      0,   0 },
    {   15, 2048,   1, 0,     0,      0,   0 }, {   18,    1,   1, 0,     0,      0,   0 },
    {    5,    1,   2, 0,     0,      0,   0 }, {   40,    1,   2, 0,     0,      0,   0 },
    {   60,    1,   2, 0,     0,      0,   0 }, {   20, 1794,   1, 0,     0,      0,   0 },
    {   60,    2,   2, 0,     0,      0,   0 }, {  120,    2,   1, 0,     0,      0,   0 },
    {   60, 1794,   1, 0,     0,      0,   0 }, {  900, 1794,   2, 0,     0,      0,   0 },
    {    0,    1,   0, 0,     0,      0,   0 }, {    0,    1,   0, 0,     0,      0,   0 },
    {    0,    1,   0, 0,     0,      0,   0 }, {    0,    1,   0, 0,     0,      0,   0 },
    {    0,    1,   0, 0,     0,      0,   0 }, {    0,    1,   0, 0,     0,      0,   0 },
    {    0,    1,   0, 0,     0,      0,   0 }, {    0,    1,   0, 0,     0,      0,   0 },
    {    0,    1,   0, 0,     0,      0,   0 }, {    0,    1,   0, 0,     0,      0,   0 },
    {    0,    1,   0, 0,     0,      0,   0 }, {    0,    1,   0, 0,     0,      0,   0 },
    {    0,    1,   0, 0,     0,      0,   0 }, {    0,    1,   0, 0,     0,      0,   0 },
    {    0,    1,   0, 0,     0,      0,   0 }, {    0,    1,   0, 0,     0,      0,   0 },
    {    0,    1,   0, 0,     0,      0,   0 }, {    0,    1,   0, 0,     0,      0,   0 },
    {    0,    1,   0, 0,     0,      0,   0 }, {    0,    1,   0, 0,     0,      0,   0 },
    {   10,    0,   1, 0,     0,      0,   0 }, {   15,    1,   1, 0,     0,      0,   0 },
    {   20,    1,   2, 0,     0,      0,   0 }, {   50,    1,   2, 0,     0,      0,   0 },
    {   40,    1,   2, 0,   150,  -2000,   0 }, {   20, 1538,   1, 0,   150,  -2000,   0 },
    {   35,    2,   2, 0,   150,  -2000,   0 }, {   35,    2,   1, 0,   150,  -2000,   0 },
    {   35, 1538,   1, 0,   150,  -2000,   0 }, {  900, 1538,   2, 0,   100,  -1800,   0 },
    {    8,    0,   1, 0,   100,  -2620,   0 }, {    9,  256,   1, 0,   100,  -1500,   0 },
    {   20,    4,   2, 0,   100,  -2620,   0 }, {   50,    4,   2, 0,   100,  -2620,   0 },
    {   60,    4,   2, 0,   150,  -2500,   0 }, {   20, 1550,   1, 0,   150,  -1620,   0 },
    {  201,    0,   2, 0,   150,  -1520,   0 }, {   70,    9,   1, 0,   150,  -1500,   0 },
    {   70, 1550,   1, 0,   150,  -1620,   0 }, {  900, 1536,   2, 0,     0,      0,   0 },
    {   15, 2048,   1, 0,     0,      0,   0 }, {   18,    1,   1, 0,     0,      0,   0 },
    {   15,    1,   2, 0,     0,      0,   0 }, {   40,    1,   2, 0,     0,      0,   0 },
    {   60,    1,   2, 0,     0,      0,   0 }, {   20, 1794,   1, 0,     0,      0,   0 },
    {   60,    2,   2, 0,     0,      0,   0 }, {  120,    2,   1, 0,     0,      0,   0 },
    {   60, 1794,   1, 0,     0,      0,   0 }, {  900, 1794,   2, 0,     0,      0,   0 },
    {    6,    1,   1, 0,     0,      0,   0 }, {    0,    1,   1, 0,     0,      0,   0 },
    {    0,    1,   2, 0,     0,      0,   0 }, {    0,    1,   2, 0,     0,      0,   0 },
    {    0,    1,   2, 0,     0,      0,   0 }, {    2, 1794,   1, 0,     0,      0,   0 },
    {   10,    2,   2, 0,     0,      0,   0 }, {   20,    2,   1, 0,     0,      0,   0 },
    {   30, 1794,   1, 0,     0,      0,   0 }, {  900, 1794,   2, 0,     0,    500, -800 },
};

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

// ============================================================================
// Post-hit callbacks (0x004bb558) and per-enemy hit reactions (0x004bb580)
//
// apply_weapon_damage calls PTR_post_hit_callbacks[weaponAdj] with the hit
// enemy AFTER health/hit-state are set but BEFORE the enemy's state 3/2
// decision - so these can still change health (the shotgun callback adds
// health back; the zombie head-shot reaction kills outright). The reaction
// table is indexed by the enemy type snapshot in g_weaponHitEnemyType
// (0x00be0de4), and the per-weapon type/data bytes live in the shared
// 0x00be0dec / 0x00be0df0 scratch globals. ENTITY is saved/restored because
// joint_setup_attack_effect reads it for the costume size and weapon joint.
// ============================================================================

extern void Flg_on(int baseAddr, unsigned int bitIndex);                        // 0x00473ef0
extern void joint_setup_attack_effect(int joint, unsigned char effectType, unsigned short timer, unsigned short frameMatch);  // 0x0048a070

// Hit-billboard rotation: the angle between the enemy's facing and the
// player's facing, with the 0x800 offset the PSX angle convention uses.
static short hit_billboard_rot(Entity* enemy)
{
    return (short)(g_playerEntityPointer.directionAngle - enemy->angle + 0x800);
}

// Dispatch to the per-enemy-type reaction.
static void enemy_hit_reaction_dispatch(Entity* enemy)
{
    ((void(*)(Entity*))g_enemy_hit_reactions[g_weaponHitEnemyType])(enemy);
}

// 0x0043d400 - no reaction
static void enemy_hit_reaction_none(Entity* enemy) { }

// 0x0043d2c0 - single blood billboard
static void enemy_hit_reaction_basic(Entity* enemy)
{
    Effect_CreateBillboard((unsigned char)g_collPushDepthZHi,
                           (unsigned char)g_collPushDepthZLo, hit_billboard_rot(enemy),
                           &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
}

// 0x0043d300 - head blood: aim-up headshot lifts the blood to head height
static void enemy_hit_reaction_head(Entity* enemy)
{
    if ((g_playerEntityPointer.flags & 0x20) != 0
        && (g_playerPosScratch.y += 1000, -800 < ((JointStruct*)enemy->jointsStructs)[1].world.t[1])) {
        g_playerPosScratch.y = -300;
    }
    Effect_CreateBillboard(3, 8, hit_billboard_rot(enemy),
                           &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
    Effect_CreateBillboard((unsigned char)g_collPushDepthZHi,
                           (unsigned char)g_collPushDepthZLo, hit_billboard_rot(enemy),
                           &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
}

// 0x0043d3a0 - blood spray; long-range hits can restore health randomly
static void enemy_hit_reaction_blood(Entity* enemy)
{
    if (g_scaled_down_dist != 0) {
        if (7000 < g_playerDisplacement && (rand() & 1) != 0) {
            enemy->health = (short)g_entity_bkp;   // restore from the pre-shot snapshot
        }
        Effect_CreateBillboard(0, 0x18, hit_billboard_rot(enemy),
                               &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
    }
}

// 0x0043d060 - zombie reaction: shotgun point-blank / magnum hits blow the
// head off (instant kill + death event + head explosion effects)
static void enemy_hit_reaction_zombie(Entity* enemy)
{
    if (g_weaponHitEnemyType != 2) {
        if (((g_scaled_down_dist == 2 && g_playerDisplacement < 3000)
             && (g_playerEntityPointer.flags & 0xC0) != 0)
            || ((g_scaled_down_dist == 3 || g_scaled_down_dist == 4)
                && (g_playerEntityPointer.flags & 0x40) != 0)) {
            enemy->health = 0xfed4;    // -300: instant kill
            Flg_on((int)g_RoomEventFlags, enemy->death_event_id);
            joint_setup_attack_effect((int)((char*)enemy->jointsStructs + 0xf8), 30, 2, 3);
            Snd_em(6);                 // head-explosion sound
            g_playerPosScratch.x = 100;
            g_playerPosScratch.y = -600;
            g_playerPosScratch.z = 0;
            Effect_CreateBillboard(0, 3, 0, (void*)((char*)enemy->jointsStructs + 0x44),
                                   &g_playerPosScratch, 0);
            unsigned int idx = g_weaponHitEnemyType * 10 + g_scaled_down_dist;
            g_playerPosScratch.x = g_weaponHitRecordsEasy[idx].kx;
            g_playerPosScratch.y = g_weaponHitRecordsEasy[idx].ky - 0x78;
            g_playerPosScratch.z = g_weaponHitRecordsEasy[idx].kz;
            Effect_CreateBillboard(3, 0, hit_billboard_rot(enemy),
                                   &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
            g_playerPosScratch.y += 0x78;
        }
        if ((g_playerEntityPointer.flags & 0x20) != 0
            && (g_playerPosScratch.y = -600, (enemy->behavior_flags & 2) != 0)) {
            g_playerPosScratch.y = -500;
        }
    }

    // Jill + handgun: extra chip damage (cerberus takes a fixed -7)
    if ((g_playerEntityPointer.id & 1) != 0 && g_scaled_down_dist == 1) {
        short h = enemy->health;
        enemy->health = (short)(h - 3);
        if (g_weaponHitEnemyType == 2) {
            enemy->health = (short)(h - 7);
        }
    }

    if (g_collPushDepthZHi == 4) {
        if (g_playerDisplacement < 9000) {
            for (unsigned int i = 4; i != 0; i--) {
                Effect_CreateBillboard(4, (unsigned char)i,
                    (short)((i + 5) * 0x100 - enemy->angle + g_playerEntityPointer.directionAngle),
                    &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
            }
        }
        g_collPushDepthZHi = 0;
    }
    Effect_CreateBillboard((unsigned char)g_collPushDepthZHi,
                           (unsigned char)g_collPushDepthZLo, hit_billboard_rot(enemy),
                           &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
}

// 0x0043c290 - knife post-hit: stab sound, reaction, knife blood billboard
static void weapon_post_hit_knife(Entity* enemy)
{
    short offset[6] = { 153, 0, 0, -380, 0, 0 };   // per-character blood offset

    Play3DSnd(1, 1, 0, (int)&g_playerEntity.scaMatrixData.localMatrix.t);
    enemy_hit_reaction_dispatch(enemy);

    if (g_collPushDepthZHi != 1) {
        unsigned int pid = (unsigned int)(g_playerEntityPointer.id & 1);
        g_playerPosScratch.x = (int)offset[pid * 3];
        g_playerPosScratch.y = (int)offset[pid * 3 + 1];
        g_playerPosScratch.z = (int)offset[pid * 3 + 2];
        Effect_CreateBillboard((unsigned char)g_collPushDepthZHi,
                               (unsigned char)g_collPushDepthZLo, 0,
                               &g_playerEntityPointer.jointsStructs[0xe].world,
                               &g_playerPosScratch, 0);
    }
}

// 0x0043c350 - reaction only
static void weapon_post_hit_reaction(Entity* enemy)
{
    enemy_hit_reaction_dispatch(enemy);
}

// 0x0043c370 - reaction + long-range shotgun health chip
static void weapon_post_hit_shotgun(Entity* enemy)
{
    if (g_scaled_down_dist == 2 && 9000 < g_playerDisplacement) {
        enemy->health += 10;
        enemy->hit_state -= 1;   // +0x8A as a signed byte, per the original
    }
    enemy_hit_reaction_dispatch(enemy);
}

// 0x0043c3b0 - heavy blood FX: spurts + joint red tint
static void weapon_post_hit_blood(Entity* enemy)
{
    JointStruct* joints = enemy->jointsStructs;
    short rot = hit_billboard_rot(enemy);

    if (g_collPushDepthZHi != 1) {
        // Aim-up headshot: lift the blood to head height
        if (g_weaponHitRecordsEasy[g_weaponHitEnemyType * 10 + g_scaled_down_dist].kx == 0x96
            && (g_playerEntityPointer.flags & 0x20) != 0) {
            g_playerPosScratch.y += 1000;
            if (-1000 < joints[1].world.t[1]) {
                g_playerPosScratch.y = -300;
            }
        }
        Effect_CreateBillboard(0x0e, (unsigned char)g_collPushDepthZLo, rot,
                               &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
        Effect_CreateBillboard(0x09, 0x0d, rot,
                               &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
        if (g_weaponHitEnemyType != 7 && g_weaponHitEnemyType != 10 && g_weaponHitEnemyType != 8) {
            g_playerPosScratch.y -= 500;
            Effect_CreateBillboard(0x0e, 0x03, rot,
                                   &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
            Effect_CreateBillboard(0x09, 0x0d, rot,
                                   &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
        }
        if (enemy->health < 0 && g_collPushDepthZHi == 0x0e) {
            if (g_weaponHitEnemyType != 5 && g_weaponHitEnemyType != 7 && g_weaponHitEnemyType != 10) {
                g_playerPosScratch.y = 0;
                g_playerPosScratch.x = -100;
                g_playerPosScratch.z = -300;
                Effect_CreateBillboard(0x0e, 0x06, rot, &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
                Effect_CreateBillboard(0x09, 0x0d, rot, &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
                g_playerPosScratch.x = 100;
                g_playerPosScratch.z = 300;
                Effect_CreateBillboard(0x0e, 0x06, rot, &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
                Effect_CreateBillboard(0x09, 0x0d, rot, &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
                if (g_weaponHitRecordsEasy[g_weaponHitEnemyType * 10 + g_scaled_down_dist].kx == 0x96) {
                    g_playerPosScratch.y = -1000;
                    g_playerPosScratch.x = -100;
                    g_playerPosScratch.z = -300;
                    Effect_CreateBillboard(0x0e, 0x06, rot, &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
                    Effect_CreateBillboard(0x09, 0x0d, rot, &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
                    g_playerPosScratch.x = 100;
                    g_playerPosScratch.z = 300;
                    Effect_CreateBillboard(0x0e, 0x06, rot, &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
                    Effect_CreateBillboard(0x09, 0x0d, rot, &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
                }
            }
            // Red tint over every joint
            ENTITY = enemy;
            for (int i = enemy->jointCount; i != 0; i--) {
                JointApplyColorTint(joints + (i - 1), 0x202020, 0x101010, (void*)0x303030);
                if (g_weaponHitEnemyType == 0) {
                    JointApplyColorTint(joints + (i - 1), 0x202020, 0x101010, (void*)0x0a0a0a);
                }
            }
            ENTITY = enemy;
            g_animFrameIdSave = 0xffffffff;   // the original ends the tint loop by writing -1 to the 0x00be0dfc scratch
        }
    }
}

// 0x0043c770 - blood FX with per-enemy-type joint spurts
static void weapon_post_hit_blood2(Entity* enemy)
{
    JointStruct* joints = enemy->jointsStructs;
    Entity* savedEntity = ENTITY;
    short rot = hit_billboard_rot(enemy);

    if (g_collPushDepthZHi != 1) {
        if (g_weaponHitRecordsEasy[g_weaponHitEnemyType * 10 + g_scaled_down_dist].kx == 0x96
            && (g_playerEntityPointer.flags & 0x20) != 0) {
            g_playerPosScratch.y += 1000;
            if (-1000 < joints[1].world.t[1]) {
                g_playerPosScratch.y = -300;
            }
        }
        Effect_CreateBillboard(0x0e, 7, rot, &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
        Effect_CreateBillboard(0x09, 0x0d, rot, &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
        Effect_CreateBillboard(0x09, 0x0d, rot, &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
        if (g_weaponHitEnemyType != 8) {
            g_playerPosScratch.y -= 200;
            for (int i = 2; i != 0; i--) {
                Effect_CreateBillboard(0x09, 0x0d, rot, &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
            }
            if (enemy->health < 0 && g_collPushDepthZHi == 0
                && g_enemyHitJointLists[g_weaponHitEnemyType][0] != 0) {
                g_playerPosScratch.y = -500;
                for (int i = 2; i != 0; i--) {
                    Effect_CreateBillboard(0x09, 0x0d, rot, &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
                }
                if ((g_playerEntityPointer.flags & 0x20) == 0
                    || (g_weaponHitRecordsEasy[g_weaponHitEnemyType * 10 + g_scaled_down_dist].kx & 1)
                       * (enemy->behavior_flags & 2)) {
                    // spurts from the per-type joint list (6 joints)
                    ENTITY = enemy;
                    for (int i = 5; i >= 0; i--) {
                        joint_setup_attack_effect(
                            (int)((char*)joints + g_enemyHitJointLists[g_weaponHitEnemyType][i] * 0x7c),
                            0x1e, 2, 3);
                    }
                } else {
                    // spurts on the top 5 joints
                    ENTITY = enemy;
                    for (int i = enemy->jointCount - 1; (int)(enemy->jointCount - 5) <= i; i--) {
                        joint_setup_attack_effect((int)((char*)joints + i * 0x7c), 0x1e, 2, 3);
                    }
                }
            }
        }
    }
    ENTITY = savedEntity;
}

// 0x0043ca30 - spark FX + joint tint
static void weapon_post_hit_sparks(Entity* enemy)
{
    JointStruct* joints = enemy->jointsStructs;
    short rot = hit_billboard_rot(enemy);

    if (g_collPushDepthZHi != 1) {
        if (g_weaponHitRecordsEasy[g_weaponHitEnemyType * 10 + g_scaled_down_dist].kx == 0x96
            && (g_playerEntityPointer.flags & 0x20) != 0) {
            g_playerPosScratch.y += 1000;
            if (-1000 < joints[1].world.t[1]) {
                g_playerPosScratch.y = -300;
            }
        }
        for (int i = 2; i != 0; i--) {
            Effect_CreateBillboard(0x09, 0, rot, &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
        }
        if (enemy->health < 0 && g_collPushDepthZHi == 9) {
            if (g_weaponHitEnemyType != 5 && g_weaponHitEnemyType != 7
                && g_weaponHitEnemyType != 10 && g_weaponHitEnemyType != 8) {
                g_playerPosScratch.y = 0;
                g_playerPosScratch.x = -100;
                g_playerPosScratch.z = -300;
                Effect_CreateBillboard(0x09, 0, rot, &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
                Effect_CreateBillboard(0x09, 1, rot, &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
                g_playerPosScratch.x = 100;
                g_playerPosScratch.z = 300;
                Effect_CreateBillboard(0x09, 0, rot, &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
                Effect_CreateBillboard(0x09, 1, rot, &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
            }
            ENTITY = enemy;
            for (int i = enemy->jointCount; i != 0; i--) {
                JointApplyColorTint(joints + (i - 1), 0x4040, 0x1010, (void*)0x3030);
                if (g_weaponHitEnemyType == 0) {
                    JointApplyColorTint(joints + (i - 1), 0x2020, 0x1010, (void*)0x0a0a);
                }
            }
            ENTITY = enemy;
            g_animFrameIdSave = 0xffffffff;
        }
    }
}

// 0x0043cc90 - blood FX (health<0 && type != 2 variant) then the joint spurts
static void weapon_post_hit_blood3(Entity* enemy)
{
    JointStruct* joints = enemy->jointsStructs;
    short rot = hit_billboard_rot(enemy);

    if (g_collPushDepthZHi != 1) {
        if (g_weaponHitRecordsEasy[g_weaponHitEnemyType * 10 + g_scaled_down_dist].kx == 0x96
            && (g_playerEntityPointer.flags & 0x20) != 0) {
            g_playerPosScratch.y += 1000;
            if (-1000 < joints[1].world.t[1]) {
                g_playerPosScratch.y = -300;
            }
        }
        Effect_CreateBillboard(0x0e, (unsigned char)g_collPushDepthZLo, rot,
                               &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
        Effect_CreateBillboard(0x09, 0x0d, rot,
                               &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
        if (g_weaponHitEnemyType != 7 && g_weaponHitEnemyType != 10 && g_weaponHitEnemyType != 8) {
            g_playerPosScratch.y -= 500;
            Effect_CreateBillboard(0x0e, 0x03, rot,
                                   &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
            Effect_CreateBillboard(0x09, 0x0d, rot,
                                   &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
        }
        if (enemy->health < 0 && g_collPushDepthZHi != 2) {
            if (g_weaponHitEnemyType != 5 && g_weaponHitEnemyType != 7 && g_weaponHitEnemyType != 10) {
                g_playerPosScratch.y = 0;
                g_playerPosScratch.x = -100;
                g_playerPosScratch.z = -300;
                Effect_CreateBillboard(0x0e, 0x06, rot, &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
                Effect_CreateBillboard(0x09, 0x0d, rot, &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
                g_playerPosScratch.x = 100;
                g_playerPosScratch.z = 300;
                Effect_CreateBillboard(0x0e, 0x06, rot, &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
                Effect_CreateBillboard(0x09, 0x0d, rot, &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
                if (g_weaponHitRecordsEasy[g_weaponHitEnemyType * 10 + g_scaled_down_dist].kx == 0x96) {
                    g_playerPosScratch.y = -1000;
                    g_playerPosScratch.x = -100;
                    g_playerPosScratch.z = -300;
                    Effect_CreateBillboard(0x0e, 0x06, rot, &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
                    Effect_CreateBillboard(0x09, 0x0d, rot, &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
                    g_playerPosScratch.x = 100;
                    g_playerPosScratch.z = 300;
                    Effect_CreateBillboard(0x0e, 0x06, rot, &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
                    Effect_CreateBillboard(0x09, 0x0d, rot, &enemy->scaMatrixData.localMatrix, &g_playerPosScratch, 0);
                }
            }
            ENTITY = enemy;
            for (int i = enemy->jointCount; i != 0; i--) {
                JointApplyColorTint(joints + (i - 1), 0x202020, 0x101010, (void*)0x303030);
                if (g_weaponHitEnemyType == 0) {
                    JointApplyColorTint(joints + (i - 1), 0x202020, 0x101010, (void*)0x0a0a0a);
                }
            }
            ENTITY = enemy;
            g_animFrameIdSave = 0xffffffff;
        }
        weapon_post_hit_blood2(enemy);
    }
}
