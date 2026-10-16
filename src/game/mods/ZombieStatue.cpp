#include "ZombieModeInternal.h"
#include "ZombieNet.h"
#include "../FileLoader.h"
#include "../../marni/MarniSound.h"
#include "../../system/AssetPath.h"
#include "../../DebugPrint.h"
#include <cstring>
#include <cstdio>

// ============================================================================
// ZombieStatue.cpp - port-added: the 2F dining room's statue falling into the
// 1F dining room, seen from below.
//
// Pushed off the 2F balcony (ROOM7020 event 0), the statue tips over and drops
// out of that room's view; the event sets ScenarioFlags bit 0x0B (bit_op 0).
// ROOM6050's init places the broken statue (omodel slot 3) and the jewel in it
// (item record 0x12) only once that bit is set, so a copy already standing in the
// 1F dining room when the bit arrives (STORY, ZombieWorld.cpp) saw nothing
// until it left and came back.
//
// Now a copy that loads the 1F dining room with the statue still up loads
// both pieces hidden (the broken statue and, without its sparkle, the jewel),
// the standing statue's own model out of ROOM7020 (object model 0) as an item
// record of our own (item records are drawn like room objects but are never
// solid), and the crash (sound/brk_stn.wav, the 2F room's BRK_stn cue). When
// the bit is set while it is here, the statue drops straight down onto the
// spot where the pieces lie, tipping over from the 2F event's lean until it
// lies flat along them - the broken model's footprint gives the direction
// and the place - then it is gone, the pieces and the jewel are there, the
// crash plays and dust puffs up along the wreck. Every copy in the room plays
// it for itself; nothing is sent.
//
// Textures go on the mod's own page counter (zm_ext_begin). If the standing
// statue cannot be loaded, the pieces appear at once with the crash.
// ============================================================================

extern int  cmd_omodel_set(void);                                       // 0x00461ac0 CmdFunctions.cpp
extern int  cmd_item_model_set(void);                                   // 0x00461220 CmdFunctions.cpp
extern void ProcessTmdAsync(unsigned int param1);                       // 0x004838e0
extern unsigned int ProcessTmdTextures(char mode, unsigned int* tmdBase, int bank, int depth);  // 0x00483560
extern void FUN_00473ea0(int param1, void* param2, ScaMatrixData* param3);
extern void InitScaMatrix(int parentPtr, ScaMatrixData* matrix);

#define ST_FLAG          0x0B               // ScenarioFlags: set once the statue has been pushed off the balcony
#define ST_SOURCE_RDT    GAME_DATA_ROOT "stage7\\room7020.rdt"
#define ST_SOURCE_SLOT   0                  // ROOM7020's object model 0: the standing statue
#define ST_SOUND_WAV     GAME_DATA_ROOT "sound\\brk_stn.wav"
#define ST_BROKEN_SLOT   3                  // ROOM6050's omodel slot: the broken statue
#define ST_JEWEL_SLOT    0x12               // ROOM6050's item_model_set slot: the jewel
#define ST_OMODEL_OP_LEN 0x1C
#define ST_ITEM_OP_LEN   0x1A
// The 2F statue tips for 10 frames before its fall begins (event 0); the
// bit is set as the event starts.
#define ST_DELAY_FRAMES  10
// Where the fall starts above the hall's floor (play-tested: the balcony's
// height, about 4200, plus 4 feet).
#define ST_DROP          5650
// The 2F event's fall: its per-frame drop starts near 30 and grows by about
// 19.5 a frame.
#define ST_SPEED0        30
#define ST_GRAVITY       20
// Its lean when the fall begins, of the quarter turn (1024) it lies at: 45
// degrees, as it goes over the balustrade (play-tested; the 2F event's own
// lean at that point is only 97).
#define ST_TILT0         512
#define ST_PUFFS         8
#define ST_TMD_MAX       0x2000
#define ST_TIM_MAX       0x10400

enum { ST_OFF, ST_STANDING, ST_DELAY, ST_FALLING, ST_DONE };

static int           s_state = ST_OFF;
static int           s_timer;
static bool          s_brokenReady;         // the pieces are loaded, hidden
static unsigned char s_brokenFlags;         // their record's flags, to show them
static bool          s_fallReady;           // the standing statue is loaded
static int           s_soundBank;

static unsigned char s_omodelOp[ST_OMODEL_OP_LEN];
static unsigned char s_jewelOp[ST_ITEM_OP_LEN];      // as the RDT has it
// The room action entry keeps a pointer into the record it was set from
// (the pickup reads the item there), so each run has a record of its own.
static unsigned char s_jewelPre[0x20];
static unsigned char s_jewelRun[0x20];

static unsigned char s_fallRec[0xA4];
static int           s_x, s_z, s_y, s_vy, s_yStart, s_yFinal;
static short         s_tilt[3];             // the lying pose's rotation (+0x72 x, y, z)
static int           s_centerX, s_centerZ;  // the pieces' footprint
static int           s_halfLen;
static bool          s_alongX;

static unsigned char s_srcRdt[640 * 1024];  // ROOM7020 (510 KB)
static int           s_srcState;            // 0 not tried, 1 loaded, -1 unavailable
static const unsigned char* s_srcTmd;
static unsigned int  s_srcTmdSize;
static const unsigned char* s_srcTim;
static unsigned int  s_srcTimSize;
static unsigned int  s_tmd[ST_TMD_MAX / 4];
static unsigned int  s_tim[ST_TIM_MAX / 4];

static bool st_here(void)
{
    return g_stageId == STAGE_MANSION_RETURN_1F && g_roomId == ROOM_DINING_ROOM;
}

// A command of the room's init script, by opcode and slot.
static unsigned char* st_find_init(unsigned char op, unsigned char slot, unsigned char mask)
{
    unsigned char* p = (unsigned char*)g_RoomInitScd;
    for (int block = 0; p != NULL && block < 16; block++) {
        unsigned short size = *(unsigned short*)p;
        if (size == 0) break;
        unsigned char* q = p + 2;
        unsigned char* end = p + size;
        while (q < end) {
            int w = zm_scd_width(q[0]);
            if (w < 0) break;
            if (q[0] == op && (q[1] & mask) == slot) return q;
            q += 1 + w;
        }
        p = end;
    }
    return NULL;
}

// A TMD's extent turned by `m` (all its objects, up to 16), as the engine
// turns it: y negated going in and coming out (ApplyMatrix; the renderer
// stores model vertices y-negated, TmdRenderer.cpp). A turn about y alone is
// the same either way; a tilt is not. A bound TMD's object table holds
// pointers instead of offsets from it (rnd_tmd_extent).
static void st_extent(const unsigned char* tmd, const MATRIX* m, int lo[3], int hi[3])
{
    int nobj = *(const int*)(tmd + 8);
    const unsigned char* objs = tmd + 0xC;
    bool resolved = (tmd[4] & 1) != 0;
    for (int a = 0; a < 3; a++) { lo[a] = 0x7FFFFFFF; hi[a] = -0x7FFFFFFF; }
    for (int o = 0; o < nobj && o < 16; o++) {
        unsigned int vt = *(const unsigned int*)(objs + o * 0x1C);
        int nv = *(const int*)(objs + o * 0x1C + 4);
        const short* v = resolved ? (const short*)vt : (const short*)(objs + vt);
        for (int i = 0; i < nv; i++, v += 4) {
            for (int a = 0; a < 3; a++) {
                int c = (m->m[a][0] * v[0] - m->m[a][1] * v[1] + m->m[a][2] * v[2]) >> 12;
                if (a == 1) c = -c;
                if (c < lo[a]) lo[a] = c;
                if (c > hi[a]) hi[a] = c;
            }
        }
    }
    for (int a = 0; a < 3; a++) if (lo[a] > hi[a]) lo[a] = hi[a] = 0;
}

static void st_matrix(short x, short y, short z, MATRIX* m)
{
    SVECTOR r;
    r.x = x; r.y = y; r.z = z; r.pad = 0;
    RotMatrix(&r, m);
}

// ROOM7020's standing statue: its TMD and TIM, as the file has them.
static bool st_load_source(void)
{
    if (s_srcState != 0) return s_srcState > 0;
    s_srcState = -1;
    size_t size = LoadFile(ST_SOURCE_RDT, s_srcRdt, 1);
    if (size == (size_t)-1 || size < 0x94 || size > sizeof(s_srcRdt)) {
        dbg_printf("[statue] %s unavailable\n", ST_SOURCE_RDT);
        return false;
    }
    const unsigned char* d = s_srcRdt;
    unsigned int omCount = d[2], itCount = d[3];
    unsigned int om = *(const unsigned int*)(d + 0x50);
    unsigned int it = *(const unsigned int*)(d + 0x54);
    if (omCount <= ST_SOURCE_SLOT || om + omCount * 8 > size || it + itCount * 8 > size) return false;
    unsigned int tmd = *(const unsigned int*)(d + om + ST_SOURCE_SLOT * 8);
    unsigned int tim = *(const unsigned int*)(d + om + ST_SOURCE_SLOT * 8 + 4);
    if (tmd == 0 || tim == 0 || tmd + 0x28 > size || tim + 0x14 > size) return false;
    // The TMD ends where the next block of the file starts.
    unsigned int end = (unsigned int)size;
    for (unsigned int i = 0; i < 19; i++) {
        unsigned int p = *(const unsigned int*)(d + 0x48 + i * 4);
        if (p > tmd && p < end) end = p;
    }
    for (unsigned int i = 0; i < omCount * 2; i++) {
        unsigned int p = *(const unsigned int*)(d + om + i * 4);
        if (p > tmd && p < end) end = p;
    }
    for (unsigned int i = 0; i < itCount * 2; i++) {
        unsigned int p = *(const unsigned int*)(d + it + i * 4);
        if (p > tmd && p < end) end = p;
    }
    // TIM: id, flags (bit 3: a CLUT block), then the blocks, each led by its length.
    unsigned int timSize = 8;
    if (d[tim + 4] & 8) timSize += *(const unsigned int*)(d + tim + timSize);
    if (tim + timSize + 4 > size) return false;
    timSize += *(const unsigned int*)(d + tim + timSize);
    if (end - tmd > ST_TMD_MAX || timSize > ST_TIM_MAX || tim + timSize > size) {
        dbg_printf("[statue] model too large (tmd %u, tim %u)\n", end - tmd, timSize);
        return false;
    }
    s_srcTmd = d + tmd; s_srcTmdSize = end - tmd;
    s_srcTim = d + tim; s_srcTimSize = timSize;
    s_srcState = 1;
    return true;
}

// The lying pose and the spot: the standing statue turned flat along the
// broken one's longest side, its top towards the end nearer the broken
// model's origin (as play-testing showed), centred on the pieces and resting
// on the floor.
static void st_orient(const unsigned char* statueTmd)
{
    const unsigned char* broken = (const unsigned char*)g_omodel_table[ST_BROKEN_SLOT];
    const int* model = (const int*)(g_RdtPointer->object_models + ST_BROKEN_SLOT * 8);
    int bx = *(const int*)(broken + 0x34), bz = *(const int*)(broken + 0x3c);
    short yaw = *(const short*)(broken + 0x74);
    MATRIX m;
    int lo[3], hi[3];
    st_matrix(*(const short*)(broken + 0x72), yaw, *(const short*)(broken + 0x76), &m);
    st_extent((const unsigned char*)model[0], &m, lo, hi);
    s_centerX = bx + (lo[0] + hi[0]) / 2;
    s_centerZ = bz + (lo[2] + hi[2]) / 2;
    s_alongX = hi[0] - lo[0] >= hi[2] - lo[2];
    int a = s_alongX ? 0 : 2;
    s_halfLen = (hi[a] - lo[a]) / 2;
    int dir = hi[a] >= -lo[a] ? -1 : 1;
    int dirX = s_alongX ? dir : 0, dirZ = s_alongX ? 0 : dir;

    // The statue's top (the TMD stands on y 0, up is -y).
    st_matrix(0, 0, 0, &m);
    st_extent(statueTmd, &m, lo, hi);
    int top = lo[1];
    static const short kTilt[4][2] = { { 0, 0x400 }, { 0, -0x400 }, { 0x400, 0 }, { -0x400, 0 } };
    int best = 0, bestScore = -0x7FFFFFFF;
    for (int i = 0; i < 4; i++) {
        st_matrix(kTilt[i][0], yaw, kTilt[i][1], &m);
        int tx = -(m.m[0][1] * top) >> 12, tz = -(m.m[2][1] * top) >> 12;   // as st_extent turns it
        int score = tx * dirX + tz * dirZ;
        if (score > bestScore) { bestScore = score; best = i; }
    }
    s_tilt[0] = kTilt[best][0];
    s_tilt[1] = yaw;
    s_tilt[2] = kTilt[best][1];
    st_matrix(s_tilt[0], s_tilt[1], s_tilt[2], &m);
    st_extent(statueTmd, &m, lo, hi);
    s_x = s_centerX - (lo[0] + hi[0]) / 2;
    s_z = s_centerZ - (lo[2] + hi[2]) / 2;
    s_yFinal = -hi[1];
    s_yStart = s_yFinal - ST_DROP;
    dbg_printf("[statue] pieces at %d,%d (%s, top %+d): statue lands at %d,%d,%d tilt %d/%d\n",
               s_centerX, s_centerZ, s_alongX ? "along x" : "along z", dir, s_x, s_yFinal, s_z,
               s_tilt[0], s_tilt[2]);
}

static void st_pose(void)
{
    unsigned char* rec = s_fallRec;
    int span = s_yFinal - s_yStart;
    int p = span > 0 ? (s_y - s_yStart) * 1024 / span : 1024;
    if (p < 0) p = 0;
    if (p > 1024) p = 1024;
    int f = ST_TILT0 + (1024 - ST_TILT0) * p / 1024;
    *(short*)(rec + 0x72) = (short)(s_tilt[0] * f / 1024);
    *(short*)(rec + 0x74) = s_tilt[1];
    *(short*)(rec + 0x76) = (short)(s_tilt[2] * f / 1024);
    *(int*)(rec + 0x34) = s_x;
    *(int*)(rec + 0x38) = s_y;
    *(int*)(rec + 0x3c) = s_z;
    *(short*)(rec + 0x6c) = (short)s_x;
    *(short*)(rec + 0x6e) = (short)s_y;
    *(short*)(rec + 0x70) = (short)s_z;
}

// The standing statue as an item record of our own after the room's
// (render_room_objects draws item_count records; nothing collides with them).
static bool st_fall_prepare(void)
{
    if (!st_load_source()) return false;
    int idx = g_RdtPointer->item_count;
    if (idx >= ROOM_ITEM_MODELS) return false;
    memcpy(s_tmd, s_srcTmd, s_srcTmdSize);
    memcpy(s_tim, s_srcTim, s_srcTimSize);
    st_orient((const unsigned char*)s_tmd);
    if (!zm_ext_begin(2)) {
        dbg_printf("[statue] no texture page left for the statue\n");
        return false;
    }
    int bank = g_TextureBankID, page = g_TextureCurrentPage;
    ProcessTmdAsync((unsigned int)s_tim);
    zm_ext_end();
    ProcessTmdTextures(2, s_tmd, bank, page);

    unsigned char* rec = s_fallRec;
    memset(rec, 0, sizeof(s_fallRec));
    g_item_model_table[idx] = rec;
    g_RdtPointer->item_count = (unsigned char)(idx + 1);
    FUN_00473ea0((int)s_tmd, rec + 0xc, (ScaMatrixData*)(rec + 0x1c));
    InitScaMatrix(0, (ScaMatrixData*)(rec + 0x1c));
    *(int*)(rec + 0xc) = 0x40000000;
    rec[0] = 0;                                     // hidden until it falls
    rec[1] = (unsigned char)idx;
    s_y = s_yStart;
    st_pose();
    return true;
}

static void st_run(int (*cmd)(void), unsigned char* op)
{
    unsigned char* saved = g_ScdOpcodes;
    g_ScdOpcodes = op;
    cmd();
    g_ScdOpcodes = saved;
}

void zm_statue_room_reset(void)
{
    s_state = ST_OFF;
    s_brokenReady = false;
    s_fallReady = false;
    if (s_soundBank != 0) {
        setSndStop(s_soundBank);
        destroySndBank(s_soundBank);
    }
    s_soundBank = 0;
}

// zombie_mode_room_spawn, after the room's init.
void zm_statue_room(void)
{
    zm_statue_room_reset();
    if (!zombie_mode_armed() || !st_here() || g_RdtPointer == NULL) return;
    if (Flg_ck((int)g_ScenarioFlags, ST_FLAG) != 0) return;    // already down: the init placed the pieces
    unsigned char* om = st_find_init(0x1F, ST_BROKEN_SLOT, 0x3F);
    unsigned char* im = st_find_init(0x18, ST_JEWEL_SLOT, 0x7F);
    if (om == NULL || im == NULL || g_RdtPointer->omodel_slot_count <= ST_BROKEN_SLOT ||
        g_omodel_table[ST_BROKEN_SLOT] == NULL) {
        dbg_printf("[statue] the room's broken statue records not found\n");
        return;
    }
    // Never a second time over what the init placed: g_omodelCount would run
    // past the room's objects (check_climb_object walks the table from it).
    // Room init zeroes only a record's first byte (its flags, which
    // omodel_set writes); the rest is whatever the last room left there.
    if (((unsigned char*)g_omodel_table[ST_BROKEN_SLOT])[0] != 0 ||
        (int)(unsigned char)g_omodelCount > ST_BROKEN_SLOT) {
        dbg_printf("[statue] the broken statue is already set (objects %d)\n", (int)(unsigned char)g_omodelCount);
        return;
    }
    memcpy(s_omodelOp, om, ST_OMODEL_OP_LEN);
    memcpy(s_jewelOp, im, ST_ITEM_OP_LEN);
    s_state = ST_STANDING;

    // The pieces, hidden: the broken statue as the init would set it, the
    // jewel without its sparkle (flags +0x18 bit 0x8000) - it gets that when
    // it is shown.
    if (zm_ext_begin(2)) {
        unsigned char op[ST_OMODEL_OP_LEN];
        memcpy(op, s_omodelOp, sizeof(op));
        st_run(cmd_omodel_set, op);
        memcpy(s_jewelPre, s_jewelOp, ST_ITEM_OP_LEN);
        *(unsigned short*)(s_jewelPre + 0x18) &= 0x7FFF;
        st_run(cmd_item_model_set, s_jewelPre);
        zm_ext_end();
        unsigned char* broken = (unsigned char*)g_omodel_table[ST_BROKEN_SLOT];
        s_brokenFlags = broken[0];
        broken[0] = 0;                              // neither drawn nor solid
        g_RoomActionTable[ST_JEWEL_SLOT * 12] = 0;  // not to be picked up
        unsigned char* jewel = (unsigned char*)g_item_model_table[s_jewelOp[0x0C]];
        if (jewel != NULL) jewel[0] = 0;
        s_brokenReady = true;
    } else {
        dbg_printf("[statue] no texture page left for the pieces\n");
    }
    if (s_brokenReady) s_fallReady = st_fall_prepare();
    s_soundBank = loadSndBankFromWav(ST_SOUND_WAV);
    if (s_soundBank == 0) dbg_printf("[statue] %s failed to load\n", ST_SOUND_WAV);
    dbg_printf("[statue] standing; pieces %s, falling statue %s\n",
               s_brokenReady ? "ready" : "missing", s_fallReady ? "ready" : "missing");
}

// Down: the pieces and the jewel, the crash and the dust.
static void st_impact(void)
{
    s_state = ST_DONE;
    if (s_fallReady) s_fallRec[0] = 0;
    if (s_brokenReady) {
        ((unsigned char*)g_omodel_table[ST_BROKEN_SLOT])[0] = s_brokenFlags;
    } else {
        // Not loaded at the room's start: set it now.
        bool ext = zm_ext_begin(2);
        unsigned char op[ST_OMODEL_OP_LEN];
        memcpy(op, s_omodelOp, sizeof(op));
        st_run(cmd_omodel_set, op);
        if (ext) zm_ext_end();
        s_centerX = *(short*)(s_omodelOp + 4);
        s_centerZ = *(short*)(s_omodelOp + 8);
        s_halfLen = 1500;
        s_alongX = true;
    }
    // The jewel as the room's init sets it, sparkle and all; its model and
    // texture are already bound, so nothing loads.
    memcpy(s_jewelRun, s_jewelOp, ST_ITEM_OP_LEN);
    bool ext = zm_ext_begin(2);
    st_run(cmd_item_model_set, s_jewelRun);
    if (ext) zm_ext_end();

    if (s_soundBank != 0) {
        pan_set(s_soundBank, 0);
        set_volume(s_soundBank, g_EnemySndVolume);
        SetSndSlot(s_soundBank, 0);                 // once, not looped
    }
    // Dust along the wreck: the shotgun's wall puff (type 9, animation 0).
    for (int k = 0; k < ST_PUFFS; k++) {
        int along = -s_halfLen + 2 * s_halfLen * k / (ST_PUFFS - 1);
        int side = (k & 1) ? 350 : -350;
        VECTOR v;
        v.x = s_centerX + (s_alongX ? along : side);
        v.z = s_centerZ + (s_alongX ? side : along);
        v.y = -150;
        v.pad = 0;
        Effect_CreateBillboard(9, 0, (short)((k * 0x555) & 0xFFF), NULL, &v, 0x14);
    }
    dbg_printf("[statue] down: pieces shown, crash, dust\n");
}

// Every frame (zombie_mode_net_frame).
void zm_statue_frame(void)
{
    if (s_state == ST_OFF || s_state == ST_DONE) return;
    if (!st_here()) { s_state = ST_OFF; return; }
    if (s_state == ST_STANDING) {
        if (Flg_ck((int)g_ScenarioFlags, ST_FLAG) == 0) return;
        if (!s_fallReady) { st_impact(); return; }
        s_state = ST_DELAY;
        s_timer = ST_DELAY_FRAMES;
        dbg_printf("[statue] pushed off the balcony\n");
        return;
    }
    if ((g_main_state_flags & (MSF_MENU_ACTIVE | MSF_ROOM_TRANSITION)) != 0) return;
    if (s_state == ST_DELAY) {
        if (--s_timer > 0) return;
        s_state = ST_FALLING;
        s_y = s_yStart;
        s_vy = ST_SPEED0;
        st_pose();
        s_fallRec[0] = 1;
        return;
    }
    s_y += s_vy;
    s_vy += ST_GRAVITY;
    if (s_y >= s_yFinal) {
        s_y = s_yFinal;
        st_pose();
        st_impact();
        return;
    }
    st_pose();
}
