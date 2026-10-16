#include "ZombieModeInternal.h"
#include "ZombieNet.h"
#include "../../DebugPrint.h"
#include <cstring>

// ============================================================================
// ZombieRoomSync.cpp - port-added: a room's puzzle state shared by the
// survivors standing in it.
//
// Each copy runs its own room, so two kinds of puzzle state stayed on the copy
// whose survivor made it:
//   - pushed objects (the 2F dining room's statue, the armor room's two, the
//     2F study's, the libraries'): their positions live in the copy's own
//     g_omodel_table. update_room_objects runs the rooms' object-probe zones
//     (probe flag 0x04) against them every frame, so a copy that has the
//     statue where the pusher put it reaches the puzzle's result by itself -
//     the fall, the armor room's flags 0x20 / 0x21 and its reveal;
//   - the large gallery's portrait order (ROOM6170): system flags (bank 4,
//     0x12-0x1F) each press event tests and sets, so presses made on two
//     copies never chained.
// Both now travel as ZM_EV_ROOMSYNC between the survivors' copies:
//   { 1 object, stage | room << 8, slot, x, z, original x, original z }
//   { 2 flags,  stage | room << 8, byte, bits set, bits cleared }
//   { 3 reset,  stage | room << 8 }
// A survivor's own pushes are sent (at most every RS_SEND_MS while it pushes,
// and where the object stopped); objects moved by the copy's own events are
// not - every copy runs those. Every survivor copy keeps the latest state of
// each mansion room. Entering a room another survivor is in, a copy takes that
// state over (an object only where the room's init put it where the pusher's
// init did); entering an empty one, the room starts afresh as the original's
// does - its init puts the statues back and clears the system flags - and the
// copy tells the others to forget the old state (reset).
//
// The gallery solved on another copy: that copy plays the reveal (event 21,
// which also places its player); here the room is brought to the state the
// solved room's init would give - the panel aside (omodel 0 as the solved
// init's omodel_set), event 22 re-arming the portraits and freeing the reward's
// zone - so the reward can be taken without leaving the room.
//
// The director's copy takes no part: these scenes would turn its camera.
//
// The gallery's six portrait questions also change places each game
// (rs_gallery_shuffle): which painting asks which question comes from the
// scenario seed, so every copy agrees. The answers keep their order - it is
// the questions' own events (14-19) that test it.
// ============================================================================

extern void Flg_on(int baseAddr, unsigned int bitIndex);              // 0x00473ef0 CmdFunctions.cpp
extern void ScdEventEntry_Create(unsigned int slot, int scriptIndex); // 0x0041d650 RoomEvents.cpp

#define RS_OBJECTS   8          // g_omodel_table
#define RS_ROOMS     32
#define RS_FLAG_BYTES 8         // g_SysFlags
#define RS_SEND_MS   150

enum { RS_OP_OBJECT = 1, RS_OP_FLAGS = 2, RS_OP_RESET = 3 };

// The rooms whose system flags hold puzzle progress, and which bits.
struct RsFlagRoom {
    unsigned char stage, room, first, last;
};
static const RsFlagRoom kFlagRooms[] = {
    { STAGE_MANSION_RETURN_1F, ROOM_LARGE_GALLERY, 0x12, 0x1F },   // the portraits' order
};

// The large gallery (ROOM6170): solved is ScenarioFlags 0x03; the press that
// solves it (event 20) sets system flag 0x1F with it.
#define RS_GALLERY_SOLVED     0x03
#define RS_GALLERY_SOLVE_BIT  0x1F
#define RS_GALLERY_EVENT_ARM  22
#define RS_GALLERY_PANEL_X    3440      // the solved init's omodel_set 0
#define RS_GALLERY_PANEL_Y    300
#define RS_GALLERY_PANEL_Z    3310
#define RS_GALLERY_PANEL_FLAG 0x81

struct RsObject {
    bool  moved;
    short origX, origZ;           // where the room's init put it
    short x, z;
};
struct RsRoom {
    RsObject      obj[RS_OBJECTS];
    unsigned char flags[RS_FLAG_BYTES];
    bool          used;
};
static RsRoom s_rooms[2][RS_ROOMS];

// This room load.
static bool          s_loaded;
static unsigned char s_stage, s_room;
static int           s_objCount;
static short         s_initX[RS_OBJECTS], s_initZ[RS_OBJECTS];
static int           s_sentX[RS_OBJECTS], s_sentZ[RS_OBJECTS];
static bool          s_dirty[RS_OBJECTS];
static unsigned int  s_sentAt[RS_OBJECTS];
static const RsFlagRoom* s_flagRoom;
// Read through Flg_ck / Flg_on, which take them a dword at a time.
alignas(4) static unsigned char s_flagMask[RS_FLAG_BYTES];
alignas(4) static unsigned char s_flagSeen[RS_FLAG_BYTES];
static bool          s_localSolve, s_wasSolved;

static RsRoom* rs_room(unsigned char stage, unsigned char room)
{
    if (room >= RS_ROOMS) return NULL;
    if (stage == STAGE_MANSION_RETURN_1F) return &s_rooms[0][room];
    if (stage == STAGE_MANSION_RETURN_2F) return &s_rooms[1][room];
    return NULL;
}

static bool rs_on(void)
{
    return zombie_mode_armed() && zm_game_role() == ZM_NET_SURVIVOR;
}

static bool rs_here(unsigned char stage, unsigned char room)
{
    return s_loaded && s_stage == stage && s_room == room && g_stageId == stage && g_roomId == room;
}

static unsigned char* rs_obj(int slot)
{
    if (slot < 0 || slot >= s_objCount) return NULL;
    unsigned char* obj = (unsigned char*)g_omodel_table[slot];
    return (obj != NULL && (obj[0] & 1) != 0) ? obj : NULL;
}

static void rs_place(unsigned char* obj, int x, int z)
{
    *(int*)(obj + 0x34) = x;
    *(int*)(obj + 0x3c) = z;
    *(short*)(obj + 0x6c) = (short)x;
    *(short*)(obj + 0x70) = (short)z;
}

static bool rs_gallery_here(void)
{
    return g_stageId == STAGE_MANSION_RETURN_1F && g_roomId == ROOM_LARGE_GALLERY;
}

static bool rs_gallery_solved(void)
{
    return Flg_ck((int)g_ScenarioFlags, RS_GALLERY_SOLVED) != 0;
}

// Another living survivor whose copy has this room loaded.
static bool rs_someone_here(void)
{
    int self = zm_net_self();
    for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
        if (i == self || zm_net_char(i) < 0) continue;
        const ZmNetPeerState* p = zm_net_player(i);
        if (p == NULL || !p->valid || p->dead || p->spectating) continue;
        if (p->viewStage == g_stageId && p->viewRoom == g_roomId) return true;
    }
    return false;
}

void zm_roomsync_reset(void)
{
    memset(s_rooms, 0, sizeof(s_rooms));
    s_loaded = false;
}

// The unsolved gallery's portraits (room action slots 2-7) each start their
// question, events 6-11 (create_room_event, the event at entry +4). Which
// painting starts which is this game's: a permutation from the seed. The
// plaque (slot 1) and the last portrait under the reward (slot 8, event 12)
// stay. Once solved, event 22 turns the portraits into plain messages.
#define RS_GALLERY_FIRST_SLOT  2
#define RS_GALLERY_FIRST_EVENT 6
#define RS_GALLERY_PORTRAITS   6

static void rs_gallery_shuffle(void)
{
    unsigned char* slot[RS_GALLERY_PORTRAITS];
    for (int i = 0; i < RS_GALLERY_PORTRAITS; i++) {
        slot[i] = &g_RoomActionTable[(RS_GALLERY_FIRST_SLOT + i) * 12];
        // Only the room's own unsolved setup.
        if (slot[i][0] != 0x09 || slot[i][4] != RS_GALLERY_FIRST_EVENT + i) return;
    }
    unsigned char order[RS_GALLERY_PORTRAITS];
    for (int i = 0; i < RS_GALLERY_PORTRAITS; i++) order[i] = (unsigned char)i;
    unsigned int h = zm_random_seed() ^ 0x6A11E7u;
    for (int i = RS_GALLERY_PORTRAITS - 1; i > 0; i--) {
        h = h * 1103515245u + 12345u;
        int j = (int)((h >> 16) % (unsigned int)(i + 1));
        unsigned char t = order[i]; order[i] = order[j]; order[j] = t;
    }
    for (int i = 0; i < RS_GALLERY_PORTRAITS; i++) slot[i][4] = (unsigned char)(RS_GALLERY_FIRST_EVENT + order[i]);
    dbg_printf("[roomsync] gallery questions: %d %d %d %d %d %d\n",
               order[0], order[1], order[2], order[3], order[4], order[5]);
}

// After the room's init (zombie_mode_room_spawn).
void zm_roomsync_room(void)
{
    s_loaded = false;
    s_flagRoom = NULL;
    s_localSolve = false;
    if (zombie_mode_armed() && rs_gallery_here()) rs_gallery_shuffle();
    if (!rs_on() || g_RdtPointer == NULL) return;
    RsRoom* r = rs_room(g_stageId, g_roomId);
    bool adopt = r != NULL && rs_someone_here();
    if (r != NULL && !adopt && r->used) {
        // Nobody else is in it: it starts afresh, for every copy.
        memset(r, 0, sizeof(*r));
        zm_net_send_event8(ZM_EV_ROOMSYNC, RS_OP_RESET, (short)(g_stageId | (g_roomId << 8)), 0, 0, 0, 0, 0, 0);
    }

    s_objCount = (unsigned char)g_RdtPointer->omodel_slot_count;
    if (s_objCount > RS_OBJECTS) s_objCount = RS_OBJECTS;
    for (int i = 0; i < s_objCount; i++) {
        s_dirty[i] = false;
        s_sentAt[i] = 0;
        unsigned char* obj = rs_obj(i);
        if (obj == NULL) { s_initX[i] = s_initZ[i] = 0; s_sentX[i] = s_sentZ[i] = 0; continue; }
        s_initX[i] = *(short*)(obj + 0x6c);
        s_initZ[i] = *(short*)(obj + 0x70);
        if (adopt && r->obj[i].moved && r->obj[i].origX == s_initX[i] && r->obj[i].origZ == s_initZ[i]) {
            rs_place(obj, r->obj[i].x, r->obj[i].z);
            dbg_printf("[roomsync] object %d taken over at (%d,%d)\n", i, (int)r->obj[i].x, (int)r->obj[i].z);
        }
        s_sentX[i] = *(int*)(obj + 0x34);
        s_sentZ[i] = *(int*)(obj + 0x3c);
    }

    memset(s_flagMask, 0, sizeof(s_flagMask));
    for (unsigned int k = 0; k < sizeof(kFlagRooms) / sizeof(kFlagRooms[0]); k++) {
        if (kFlagRooms[k].stage != g_stageId || kFlagRooms[k].room != g_roomId) continue;
        s_flagRoom = &kFlagRooms[k];
        // Flg_on's own bit order builds the mask, as the story flags' do.
        for (unsigned int b = s_flagRoom->first; b <= s_flagRoom->last; b++) Flg_on((int)s_flagMask, b);
    }
    unsigned char* sys = (unsigned char*)g_SysFlags;
    if (s_flagRoom != NULL && adopt) {
        for (int b = 0; b < RS_FLAG_BYTES; b++) {
            sys[b] = (unsigned char)((sys[b] & ~s_flagMask[b]) | (r->flags[b] & s_flagMask[b]));
        }
        dbg_printf("[roomsync] puzzle flags taken over\n");
    }
    for (int b = 0; b < RS_FLAG_BYTES; b++) s_flagSeen[b] = (unsigned char)(sys[b] & s_flagMask[b]);

    s_wasSolved = rs_gallery_here() && rs_gallery_solved();
    s_stage = g_stageId;
    s_room = g_roomId;
    s_loaded = true;
}

// The large gallery solved on another copy, with this one in the room: the
// solved room's state, without the reveal's player placing.
static void rs_gallery_catch_up(void)
{
    unsigned char* panel = rs_obj(0);
    if (panel != NULL) {
        panel[0] = RS_GALLERY_PANEL_FLAG;
        rs_place(panel, RS_GALLERY_PANEL_X, RS_GALLERY_PANEL_Z);
        *(int*)(panel + 0x38) = RS_GALLERY_PANEL_Y;
        *(short*)(panel + 0x6e) = RS_GALLERY_PANEL_Y;
        s_sentX[0] = RS_GALLERY_PANEL_X;
        s_sentZ[0] = RS_GALLERY_PANEL_Z;
        s_dirty[0] = false;
    }
    ScdEventEntry_Create(9, RS_GALLERY_EVENT_ARM);
    dbg_printf("[roomsync] gallery solved by another survivor: reward freed here\n");
}

void zm_roomsync_frame(void)
{
    if (!s_loaded || !rs_on() || g_stageId != s_stage || g_roomId != s_room) return;
    if (g_roomTransitionBusy || (g_main_state_flags & MSF_ROOM_TRANSITION) != 0) return;
    RsRoom* r = rs_room(g_stageId, g_roomId);
    unsigned int now = zm_game_time_ms();
    short where = (short)(g_stageId | (g_roomId << 8));

    // Objects: this survivor's pushes go out; other moves (the copy's own
    // events, which take the control - message flag 0x100 - to play) are
    // every copy's own.
    bool pushing = ((g_main_state_flags & MSF_OBJECT_PUSH) != 0 || g_playerEntity.action_behavior == 0x10) &&
                   (g_message_flags & 0x0100) != 0;
    for (int i = 0; i < s_objCount; i++) {
        unsigned char* obj = rs_obj(i);
        if (obj == NULL) continue;
        int x = *(int*)(obj + 0x34), z = *(int*)(obj + 0x3c);
        if (x != s_sentX[i] || z != s_sentZ[i]) {
            if (pushing) {
                s_dirty[i] = true;
            } else if (!s_dirty[i]) {
                s_sentX[i] = x;
                s_sentZ[i] = z;
                continue;
            }
        }
        if (!s_dirty[i] || (pushing && now - s_sentAt[i] < RS_SEND_MS)) continue;
        s_dirty[i] = false;
        s_sentX[i] = x;
        s_sentZ[i] = z;
        s_sentAt[i] = now ? now : 1;
        zm_net_send_event8(ZM_EV_ROOMSYNC, RS_OP_OBJECT, where, (short)i, (short)x, (short)z,
                           s_initX[i], s_initZ[i], 0);
        if (r != NULL) {
            r->used = true;
            r->obj[i].moved = true;
            r->obj[i].origX = s_initX[i];
            r->obj[i].origZ = s_initZ[i];
            r->obj[i].x = (short)x;
            r->obj[i].z = (short)z;
        }
    }

    // Puzzle flags: what this copy's events changed.
    if (s_flagRoom != NULL) {
        const unsigned char* sys = (const unsigned char*)g_SysFlags;
        bool solveWas = Flg_ck((int)s_flagSeen, RS_GALLERY_SOLVE_BIT) != 0;
        for (int b = 0; b < RS_FLAG_BYTES; b++) {
            unsigned char cur = (unsigned char)(sys[b] & s_flagMask[b]);
            if (cur == s_flagSeen[b]) continue;
            unsigned char set = (unsigned char)(cur & ~s_flagSeen[b]);
            unsigned char clr = (unsigned char)(s_flagSeen[b] & ~cur);
            s_flagSeen[b] = cur;
            zm_net_send_event8(ZM_EV_ROOMSYNC, RS_OP_FLAGS, where, (short)b, set, clr, 0, 0, 0);
            if (r != NULL) {
                r->used = true;
                r->flags[b] = cur;
            }
        }
        if (!solveWas && Flg_ck((int)s_flagSeen, RS_GALLERY_SOLVE_BIT) != 0) s_localSolve = true;
    }

    if (rs_gallery_here() && !s_wasSolved && rs_gallery_solved()) {
        s_wasSolved = true;
        if (!s_localSolve) rs_gallery_catch_up();
    }
}

// ZM_EV_ROOMSYNC from another survivor's copy.
void zm_roomsync_take(const short* a, int src)
{
    if (!rs_on() || src == zm_net_self()) return;
    unsigned char stage = (unsigned char)(a[1] & 0xFF), room = (unsigned char)((unsigned short)a[1] >> 8);
    RsRoom* r = rs_room(stage, room);
    if (r == NULL) return;
    bool here = rs_here(stage, room);
    switch (a[0]) {
    case RS_OP_RESET:
        if (!here) memset(r, 0, sizeof(*r));
        break;
    case RS_OP_OBJECT: {
        int slot = a[2];
        if (slot < 0 || slot >= RS_OBJECTS) break;
        r->used = true;
        r->obj[slot].moved = true;
        r->obj[slot].x = a[3];
        r->obj[slot].z = a[4];
        r->obj[slot].origX = a[5];
        r->obj[slot].origZ = a[6];
        unsigned char* obj = here ? rs_obj(slot) : NULL;
        // Only the same object: the room's init put it where the sender's did.
        if (obj == NULL || s_initX[slot] != a[5] || s_initZ[slot] != a[6]) break;
        rs_place(obj, a[3], a[4]);
        s_sentX[slot] = a[3];
        s_sentZ[slot] = a[4];
        s_dirty[slot] = false;
        break;
    }
    case RS_OP_FLAGS: {
        int b = a[2];
        if (b < 0 || b >= RS_FLAG_BYTES) break;
        unsigned char set = (unsigned char)a[3], clr = (unsigned char)a[4];
        r->used = true;
        r->flags[b] = (unsigned char)((r->flags[b] | set) & ~clr);
        if (!here || s_flagRoom == NULL) break;
        unsigned char* sys = (unsigned char*)g_SysFlags;
        set &= s_flagMask[b];
        clr &= s_flagMask[b];
        sys[b] = (unsigned char)((sys[b] | set) & ~clr);
        s_flagSeen[b] = (unsigned char)(sys[b] & s_flagMask[b]);
        dbg_printf("[roomsync] puzzle flags from player %d: byte %d +%02X -%02X\n", src, b, set, clr);
        break;
    }
    }
}
