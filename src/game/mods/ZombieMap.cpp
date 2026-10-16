#include "ZombieModeInternal.h"
#include "ZombieNet.h"
#include "../FileLoader.h"
#include "../SFXIds.h"
#include "../../system/AssetPath.h"
#include "../../DebugPrint.h"
#include <cstdio>
#include <cstring>

// ============================================================================
// ZombieMap.cpp - the director's map (port-added).
//
// The game's own map screen lives in the inventory menu task, which suspends
// gameplay - fine for one player, not for a director whose world has to keep
// running while the survivor plays. So this is an overlay drawn during play,
// built from the same data the map screen uses.
//
// Each floor's map (item_m2/mapXX.tim, the files the map screen loads -
// MainMenu.cpp g_MapFileNames) is an 8bpp image whose palette gives every
// room six entries of its own: room r is drawn with indices 13 + 6r ..
// 18 + 6r (map_display_load_textures recolours exactly those entries to hide
// unvisited rooms and highlight the current one). So a room's shape on the
// map is simply its pixels. They are sampled on a 64x64 grid and merged into
// rectangles, which draw_rect puts on screen.
// ============================================================================

// MainMenu.cpp g_MapFileNames (0x004d3050), indexed by map area.
static const char* const kMapFiles[11] = {
    "item_m2/map01.tim", "item_m2/map02.tim", "item_m2/map03.tim", "item_m2/map04.tim",
    "item_m2/map05.tim", "item_m2/map06.tim", "item_m2/map07.tim", "item_m2/map08.tim",
    "item_m2/map09.tim", "item_m2/map0a.tim", "item_m2/map0b.tim",
};
static const char* const kAreaNames[11] = {
    "MANSION 1F", "MANSION 2F", "MANSION B1", "COURTYARD", "UNDERGROUND",
    "LABORATORY B1", "LABORATORY B2", "LABORATORY B3", "LABORATORY B4",
    "GUARDHOUSE 1F", "GUARDHOUSE B1",
};
// Sheet -> room group (stage % 5). Read off the sheets themselves - which
// rooms each one's palette has entries for - since menu_init_map_screen's area
// numbers go through the layout tables before they pick a file.
static const unsigned char kAreaGroup[11] = { 0, 1, 1, 2, 2, 4, 4, 4, 4, 3, 3 };

// The sheet a room is drawn on, by the same reading: 2F rooms from 0x1A on
// are the B1 sheet (stage 7 only), courtyard rooms from the item chamber on
// are underground, guardhouse rooms from the water tank entry on are B1, and
// the lab's floors split at the ladder room, the B3 passage and rooms
// 0x13/0x14 (B4). Split sections are resolved separately below.
int zm_map_area_of(unsigned char stage, unsigned char room)
{
    switch (stage % 5) {
    case STAGE_MANSION_1F:  return 0;
    case STAGE_MANSION_2F:  return (room < ROOM_MANSION_B1_PASSAGE_1) ? 1 : 2;
    case STAGE_COURTYARD:   return (room < ROOM_ITEM_CHAMBER) ? 3 : 4;
    case STAGE_GUARDHOUSE:  return (room < ROOM_WATER_TANK_ENTRY) ? 9 : 10;
    default:  // laboratory
        if (room < ROOM_LAB_LADDER_ROOM) return 5;
        if (room < ROOM_LAB_B3_O_PASSAGE) return 6;
        return (room == 0x13 || room == 0x14) ? 8 : 7;
    }
}

// ---------------------------------------------------------------------------
// The sheets: rooms as rectangles on a 64x64 grid
//
// The director's map is the mansion the survivors cross on their way to the
// back exit (the storeroom's door to the courtyard, ZombieMode.cpp): its 1F
// and 2F sheets side by side and the basement (B1) under 1F, every room with
// a room file selectable.
// ---------------------------------------------------------------------------
#define MAP_GRID      64
#define MAP_MAX_RECTS 360
#define MAP_SHEETS    3
static const int kSheetArea[MAP_SHEETS] = { 0, 1, 2 };  // MANSION 1F, 2F, B1
#define MAP_SHEET_B1_ID 2

// A palette marker is a drawn section, not necessarily an RDT room. Keep
// its drawing sheet separate from the actual room's primary sheet.
struct MapRect { unsigned char sheet, homeSheet, room, x, y, w, h; };

static void map_section_room(int sheet, unsigned char marker, unsigned char* homeSheet,
                             unsigned char* room)
{
    *homeSheet = (unsigned char)sheet;
    *room = marker;
    // Inverse of MainMenu.cpp menu_init_map_screen (0x00488160).
    // These extra markers describe camera regions inside existing rooms.
    static const struct {
        unsigned char sheet, marker, homeSheet, room;
    } sections[] = {
        { 0, 0x1D, 0, ROOM_GALLERY },
        { 0, 0x1E, 0, ROOM_MANSION_BAR },
        { 0, 0x1F, 1, ROOM_LESSON_ROOM },
        { 1, 0x1D, 1, ROOM_SMALL_DINING },
        { 2, 0x1E, 0, ROOM_MANSION_1F_ELEVATOR_FRONT },
    };
    for (unsigned int i = 0; i < sizeof(sections) / sizeof(sections[0]); i++) {
        if (sections[i].sheet == sheet && sections[i].marker == marker) {
            *homeSheet = sections[i].homeSheet;
            *room = sections[i].room;
            return;
        }
    }
}

static MapRect s_rects[MAP_MAX_RECTS * MAP_SHEETS];
static int     s_rectCount = 0;
static bool    s_built = false;
static unsigned char s_tim[64 * 1024];

static void map_build_sheet(int sheet)
{
    int area = kSheetArea[sheet];
    char path[96];
    snprintf(path, sizeof(path), "%s%s", GAME_DATA_ROOT, kMapFiles[area]);
    size_t size = LoadFile(path, s_tim, 0x20);
    if (size == (size_t)-1 || size < 0x40 || size > sizeof(s_tim)) return;

    // TIM: id, flags, then the CLUT block (its length first), then the image
    // block: length, x, y, width in 16-bit units, height, pixels (8bpp).
    unsigned int clutLen = *(unsigned int*)(s_tim + 8);
    unsigned int img = 8 + clutLen;
    if (img + 12 > size) return;
    int width = *(unsigned short*)(s_tim + img + 8) * 2;
    int height = *(unsigned short*)(s_tim + img + 10);
    const unsigned char* px = s_tim + img + 12;
    if (width <= 0 || height <= 0 || img + 12 + (size_t)(width * height) > size) return;

    // Owner of each grid cell: the room whose palette entries its sample uses.
    static unsigned char owner[MAP_GRID][MAP_GRID];
    for (int gy = 0; gy < MAP_GRID; gy++) {
        for (int gx = 0; gx < MAP_GRID; gx++) {
            int sx = gx * width / MAP_GRID, sy = gy * height / MAP_GRID;
            unsigned char v = px[sy * width + sx];
            owner[gy][gx] = (v >= 13) ? (unsigned char)((v - 13) / 6) : 0xFF;
        }
    }
    // Greedy merge: a run along a row, grown downward while the rows below
    // carry the same run.
    static bool used[MAP_GRID][MAP_GRID];
    memset(used, 0, sizeof(used));
    int first = s_rectCount;
    for (int y = 0; y < MAP_GRID; y++) {
        for (int x = 0; x < MAP_GRID; x++) {
            unsigned char r = owner[y][x];
            if (r == 0xFF || used[y][x]) continue;
            int w = 1;
            while (x + w < MAP_GRID && owner[y][x + w] == r && !used[y][x + w]) w++;
            int h = 1;
            for (;;) {
                if (y + h >= MAP_GRID) break;
                bool same = true;
                for (int i = 0; i < w && same; i++) {
                    same = owner[y + h][x + i] == r && !used[y + h][x + i];
                }
                if (!same) break;
                h++;
            }
            for (int yy = 0; yy < h; yy++) for (int xx = 0; xx < w; xx++) used[y + yy][x + xx] = true;
            if (s_rectCount < MAP_MAX_RECTS * MAP_SHEETS) {
                MapRect& m = s_rects[s_rectCount++];
                m.sheet = (unsigned char)sheet;
                map_section_room(sheet, r, &m.homeSheet, &m.room);
                m.x = (unsigned char)x; m.y = (unsigned char)y;
                m.w = (unsigned char)w; m.h = (unsigned char)h;
            }
        }
    }
    dbg_printf("[map] %s: %d rects\n", kMapFiles[area], s_rectCount - first);
}

// ---------------------------------------------------------------------------
// The overlay
// ---------------------------------------------------------------------------
static bool s_open = false;
static int  s_selSheet = 0;          // selected room: its sheet...
static int  s_sel = -1;              // ...and room id, -1 none
static bool s_jumpReady = false;
static unsigned char s_jumpStage = 0, s_jumpRoom = 0;
static bool s_placeReady = false;
static unsigned char s_placeStage = 0, s_placeRoom = 0;

// What the director can place (entity ids, EntityModelLoader's table): the
// zombies and the roaming monsters. Bosses, plants and the water / web set
// pieces need their rooms' own scripting and are left out - but for the lab
// rooftop Tyrant (id 0x10): the mod skips its scripted entrance and final
// battle health latch so it can roam, sprint, fight and die in any room.
// Crows, adders and wasps are left out too: they did not work well placed.
// Prices and unlock times: ZombieEconomy.cpp.
static const struct { unsigned char id; const char* name; } kMonsters[] = {
    { 0x00, "ZOMBIE" },
    { 0x11, "GREEN ZOMBIE" },
    { 0x01, "NAKED ZOMBIE" },
    { 0x02, "CERBERUS" },
    { 0x06, "HUNTER" },
    { 0x09, "CHIMERA" },
    { 0x10, "TYRANT" },
    { 0x03, "WEB SPINNER" },
    // Traps (ZombieTraps.cpp): set on the room the same way.
    { ZM_TRAP_LOCK_DOORS, "LOCK DOORS" },
};
#define MAP_MONSTER_TYPES ((int)(sizeof(kMonsters) / sizeof(kMonsters[0])))
static int s_monster = 0;
// Run opens the list of monster types over the map (up/down, action or run
// to keep the choice).
static bool s_typeMenu = false;
// Richard's radio (ZombiePerks.cpp): the same map, to look at only - the
// arrows pick a room to read its monster count; nothing is placed or jumped to.
static bool s_readOnly = false;
// The route map (the lobby's, before a game): look-only too, and it shows the
// randomized scenario's key-locked doors (ZombieRandom.cpp) instead of the
// monsters and the players.
static bool s_route = false;
static bool s_setup = false;
void zm_map_set_setup(bool on) { s_setup = on; }

static int         s_routeLevel = 0;           // ZM_ROUTE_*: what of the scenario shows
static bool        s_routeMonsters = false;    // Richard's radio: monster counts too
static bool        s_routePlayers = false;     // in the game: this survivor's room, the others'
static const char* s_routeStatus = "";
static const char* s_routeHelp = "";
// The pregame lobby's popup over the route map (zm_map_set_menu).
static const char* const* s_menuItems = NULL;
static const bool* s_menuEnabled = NULL;
static int         s_menuCount = 0, s_menuSel = 0;

void zm_map_set_read_only(bool on) { s_readOnly = on; s_route = false; }
void zm_map_set_route(int level, bool monsters, bool players, const char* status, const char* help)
{
    s_readOnly = true;
    s_route = true;
    s_routeLevel = level;
    s_routeMonsters = monsters;
    s_routePlayers = players;
    s_routeStatus = status != NULL ? status : "";
    s_routeHelp = help != NULL ? help : "";
    s_menuCount = 0;
}

void zm_map_set_menu(const char* const* items, const bool* enabled, int count, int sel)
{
    s_menuItems = items;
    s_menuEnabled = enabled;
    s_menuCount = items != NULL ? count : 0;
    s_menuSel = sel;
}

static bool map_is_key_item(unsigned char id)
{
    return (id >= ITEM_SWORD_KEY && id <= ITEM_HELMET_KEY) || id == ITEM_WIND_CREST ||
           id == ITEM_MOON_CREST || id == ITEM_STAR_CREST || id == ITEM_SUN_CREST ||
           id == ITEM_BATTERY || id == ITEM_ZM_PASS_NOTE;     // the back area's ways in
}

static const char* map_item_name(unsigned char id)
{
    switch (id) {
    case ITEM_SWORD_KEY:       return "SWORD KEY";
    case ITEM_ARMOR_KEY:       return "ARMOR KEY";
    case ITEM_SHIELD_KEY:      return "SHIELD KEY";
    case ITEM_HELMET_KEY:      return "HELMET KEY";
    case ITEM_WIND_CREST:      return "WIND CREST";
    case ITEM_MOON_CREST:      return "MOON CREST";
    case ITEM_STAR_CREST:      return "STAR CREST";
    case ITEM_SUN_CREST:       return "SUN CREST";
    case ITEM_BATTERY:         return "BATTERY";
    case ITEM_ZM_PASS_NOTE:    return "PASS NUMBER NOTE";
    case ITEM_BERETTA:         return "HANDGUN";
    case ITEM_SHOTGUN:         return "SHOTGUN";
    case ITEM_COLT_PYTHON_MAG: return "MAGNUM";
    case ITEM_FLAMETHROWER:    return "FLAMETHROWER";
    case ITEM_BAZOOKA_EXPLOSIVE:
    case ITEM_BAZOOKA_ACID:
    case ITEM_BAZOOKA_FLAME:   return "GRENADE LAUNCHER";
    case ITEM_ROCKET_LAUNCHER: return "ROCKET LAUNCHER";
    case ITEM_INGRAM:          return "INGRAM";
    case ITEM_CLIP:            return "CLIP";
    case ITEM_SHELLS:          return "SHELLS";
    case ITEM_MAGNUM_ROUNDS:   return "MAGNUM ROUNDS";
    case ITEM_FUEL:            return "FUEL";
    case ITEM_EXPLOSIVE_ROUNDS: return "EXPLOSIVE ROUNDS";
    case ITEM_ACID_ROUNDS:     return "ACID ROUNDS";
    case ITEM_FLAME_ROUNDS:    return "FLAME ROUNDS";
    case ITEM_GREEN_HERB:      return "GREEN HERB";
    case ITEM_RED_HERB:        return "RED HERB";
    case ITEM_BLUE_HERB:       return "BLUE HERB";
    case ITEM_FIRST_AID_SPRAY: return "SPRAY";
    case ITEM_INK_RIBBONS:     return "INK RIBBON";
    default:                   return "SUPPLIES";
    }
}

static const char* map_key_short(unsigned char key)
{
    switch (key) {
    case ITEM_SWORD_KEY:  return "SWORD";
    case ITEM_ARMOR_KEY:  return "ARMOR";
    case ITEM_SHIELD_KEY: return "SHIELD";
    case ITEM_HELMET_KEY: return "HELMET";
    default:              return "KEY";
    }
}

// Rooms by primary sheet and actual RDT room id, shared by all drawn sections.
#define MAP_ROOMS 40
static signed char s_zombies[MAP_SHEETS][MAP_ROOMS];   // monsters; -1 unknown, -2 not scanned yet
static bool s_onSheet[MAP_SHEETS][MAP_ROOMS];
static bool s_noRdt[MAP_SHEETS][MAP_ROOMS];             // drawn, but no room file (no way in)

// The stage a sheet's rooms are loaded as: the area's group, moved to the
// return-mansion variant (5/6) once the scenario has switched to it - what
// room_transition_load makes of a door into that stage.
static unsigned char map_stage_of_sheet(int sheet)
{
    unsigned char st = kAreaGroup[kSheetArea[sheet]];
    // The zombie mode always plays the return mansion (zombie_mode_new_game),
    // so its map reads those stages even before the game has set the flag.
    if (st < 2 && (g_bPlayAsZombie || Flg_ck((int)&g_ScenarioFlags, SCENARIO_FLAG_STAGE_VARIANT) != 0)) {
        st = (unsigned char)(st + 5);
    }
    return st;
}

static void map_build_all(void)
{
    if (s_built) return;
    s_built = true;
    s_rectCount = 0;
    for (int s = 0; s < MAP_SHEETS; s++) map_build_sheet(s);
    for (int s = 0; s < MAP_SHEETS; s++) {
        for (int i = 0; i < MAP_ROOMS; i++) { s_zombies[s][i] = -2; s_onSheet[s][i] = false; s_noRdt[s][i] = false; }
    }
    for (int i = 0; i < s_rectCount; i++) {
        if (s_rects[i].room < MAP_ROOMS) s_onSheet[s_rects[i].homeSheet][s_rects[i].room] = true;
    }
}

// Every room of the mansion's sheets can be picked (a room id a door record
// can name).
// The basement (B1) is only built in the return to the mansion: the first
// mansion's stage 2 carries its rooms (0x1A-0x1C) as 4-byte stub RDTs, so it
// is left off the map until the scenario has switched to stages 6/7.
static bool map_sheet_shown(int sheet)
{
    return sheet != MAP_SHEET_B1_ID || map_stage_of_sheet(sheet) >= 5;
}

static bool map_selectable(int sheet, int r)
{
    return sheet >= 0 && sheet < MAP_SHEETS && map_sheet_shown(sheet) && r >= 0 && r < MAP_ROOMS &&
           r < 0x20 && s_onSheet[sheet][r] && !s_noRdt[sheet][r];
}

// Monster counts for the rooms. RDTs not read yet are read one per frame, so
// opening the map never stalls on sixty file loads.
static void map_update_counts(void)
{
    bool loaded = false;
    for (int s = 0; s < MAP_SHEETS; s++) {
        unsigned char st = map_stage_of_sheet(s);
        for (int r = 0; r < MAP_ROOMS; r++) {
            if (!map_selectable(s, r)) { s_zombies[s][r] = -1; continue; }
            bool cached = (st == g_stageId && r == g_roomId) || zm_room_cached(st, (unsigned char)r);
            if (!cached) {
                if (loaded) continue;
                loaded = true;
            }
            int n = zm_world_room_zombies(st, (unsigned char)r, NULL);
            s_zombies[s][r] = (signed char)(n >= 0 ? (n > 99 ? 99 : n) : -1);
            if (n < 0) s_noRdt[s][r] = true;     // a drawn marker with no actual room file
        }
    }
}

// ---------------------------------------------------------------------------
// Layout, at one scale: 1F (left) and 2F (right) side by side on top, B1
// under 1F with 1F's horizontal origin - the sheets share one frame, so the
// basement sits under the part of 1F it is under. The space under 2F holds
// the survivor list. Each room's centre on screen is what the arrows move
// between.
// ---------------------------------------------------------------------------
#define MAP_SHEET_1F 0
#define MAP_SHEET_2F 1
#define MAP_SHEET_B1 2
struct MapLayout {
    int cell;
    int ox[MAP_SHEETS], oy[MAP_SHEETS];
    int labelX[MAP_SHEETS], labelY[MAP_SHEETS];
    int listX, listY;               // the survivor list, under 2F
    int colW;                       // a column's width (1F's, 2F's)
};

static void map_layout(MapLayout* L)
{
    const int areaX = 8, areaY = 26, areaW = 304, areaBottom = 196, colGap = 12, rowGap = 16;
    const int colW = (areaW - colGap) / 2;
    int bx0[MAP_SHEETS], by0[MAP_SHEETS], bx1[MAP_SHEETS], by1[MAP_SHEETS];
    for (int s = 0; s < MAP_SHEETS; s++) { bx0[s] = by0[s] = MAP_GRID; bx1[s] = by1[s] = 0; }
    for (int i = 0; i < s_rectCount; i++) {
        const MapRect& m = s_rects[i];
        int s = m.sheet;
        if (m.x < bx0[s]) bx0[s] = m.x;
        if (m.y < by0[s]) by0[s] = m.y;
        if (m.x + m.w > bx1[s]) bx1[s] = m.x + m.w;
        if (m.y + m.h > by1[s]) by1[s] = m.y + m.h;
    }
    for (int s = 0; s < MAP_SHEETS; s++) {
        if (bx1[s] <= bx0[s] || by1[s] <= by0[s]) { bx0[s] = by0[s] = 0; bx1[s] = by1[s] = MAP_GRID; }
    }
    // 1F's column carries B1 under it (aligned with 1F, so 1F's x extent
    // covers it); the top row is as tall as the taller of 1F and 2F.
    int x0 = bx0[MAP_SHEET_1F] < bx0[MAP_SHEET_B1] ? bx0[MAP_SHEET_1F] : bx0[MAP_SHEET_B1];
    int x1 = bx1[MAP_SHEET_1F] > bx1[MAP_SHEET_B1] ? bx1[MAP_SHEET_1F] : bx1[MAP_SHEET_B1];
    int topH = by1[MAP_SHEET_1F] - by0[MAP_SHEET_1F];
    if (by1[MAP_SHEET_2F] - by0[MAP_SHEET_2F] > topH) topH = by1[MAP_SHEET_2F] - by0[MAP_SHEET_2F];
    int b1H = by1[MAP_SHEET_B1] - by0[MAP_SHEET_B1];
    int cell = 6;
    int c = colW / (x1 - x0);
    if (c < cell) cell = c;
    c = colW / (bx1[MAP_SHEET_2F] - bx0[MAP_SHEET_2F]);
    if (c < cell) cell = c;
    c = (areaBottom - areaY - rowGap) / (topH + b1H);
    if (c < cell) cell = c;
    if (cell < 1) cell = 1;
    L->cell = cell;

    int left1F = areaX, left2F = areaX + colW + colGap;
    L->ox[MAP_SHEET_1F] = left1F + (colW - (x1 - x0) * cell) / 2 - x0 * cell;
    L->ox[MAP_SHEET_B1] = L->ox[MAP_SHEET_1F];
    L->ox[MAP_SHEET_2F] = left2F + (colW - (bx1[MAP_SHEET_2F] - bx0[MAP_SHEET_2F]) * cell) / 2 -
                          bx0[MAP_SHEET_2F] * cell;
    L->oy[MAP_SHEET_1F] = areaY - by0[MAP_SHEET_1F] * cell;
    L->oy[MAP_SHEET_2F] = areaY - by0[MAP_SHEET_2F] * cell;
    int row2 = areaY + topH * cell + rowGap;
    L->oy[MAP_SHEET_B1] = row2 - by0[MAP_SHEET_B1] * cell;
    L->labelX[MAP_SHEET_1F] = left1F + colW / 2;
    L->labelX[MAP_SHEET_2F] = left2F + colW / 2;
    L->labelX[MAP_SHEET_B1] = L->ox[MAP_SHEET_B1] + (bx0[MAP_SHEET_B1] + bx1[MAP_SHEET_B1]) * cell / 2;
    L->labelY[MAP_SHEET_1F] = 10;
    L->labelY[MAP_SHEET_2F] = 10;
    L->labelY[MAP_SHEET_B1] = row2 - 14;
    L->listX = left2F;
    L->listY = row2 - 14;
    L->colW = colW;
}

// A room's centre on screen (area-weighted over all its drawn sections), x2.
static bool map_room_centre(const MapLayout& L, int sheet, int room, int* cx, int* cy)
{
    long long sx = 0, sy = 0, sa = 0;
    for (int i = 0; i < s_rectCount; i++) {
        const MapRect& m = s_rects[i];
        if (m.homeSheet != sheet || m.room != room || !map_sheet_shown(m.sheet)) continue;
        long long a = (long long)m.w * m.h;
        sx += a * (2 * (L.ox[m.sheet] + m.x * L.cell) + m.w * L.cell);
        sy += a * (2 * (L.oy[m.sheet] + m.y * L.cell) + m.h * L.cell);
        sa += a;
    }
    if (sa == 0) return false;
    *cx = (int)(sx / sa);
    *cy = (int)(sy / sa);
    return true;
}

// The arrows: the nearest room in that direction, measured on the drawn map -
// distance along the direction plus twice the sideways offset, so a room
// straight ahead beats a closer one off to the side. Off the right edge of
// 1F lies 2F, and back.
static void map_move(int dx, int dy, bool audible = true)
{
    MapLayout L;
    map_layout(&L);
    int fx, fy;
    if (s_sel < 0 || !map_room_centre(L, s_selSheet, s_sel, &fx, &fy)) {
        fx = 320; fy = 172;     // nothing picked yet: from the middle
    }
    int bestSheet = -1, bestRoom = -1;
    long long bestScore = 0;
    for (int s = 0; s < MAP_SHEETS; s++) {
        for (int r = 0; r < MAP_ROOMS; r++) {
            if (!map_selectable(s, r) || (s == s_selSheet && r == s_sel)) continue;
            int cx, cy;
            if (!map_room_centre(L, s, r, &cx, &cy)) continue;
            int along = (cx - fx) * dx + (cy - fy) * dy;
            int side = (cx - fx) * dy + (cy - fy) * dx;
            if (side < 0) side = -side;
            if (along <= 0) continue;
            long long score = (long long)along + 2LL * side;
            if (bestRoom < 0 || score < bestScore) {
                bestScore = score;
                bestSheet = s;
                bestRoom = r;
            }
        }
    }
    if (bestRoom >= 0) {
        if (s_setup && audible) play_sfx(SFX_UI_BANK, SFX_UI_CURSOR);
        s_selSheet = bestSheet;
        s_sel = bestRoom;
    }
}

bool zm_map_is_open(void) { return s_open; }

// Pick (stage, room) if it is on the map; false if not.
static bool map_select(unsigned char stage, unsigned char room)
{
    for (int s = 0; s < MAP_SHEETS; s++) {
        if (map_stage_of_sheet(s) == stage && map_selectable(s, room) &&
            zm_map_area_of(stage, room) == kSheetArea[s]) {
            s_selSheet = s;
            s_sel = room;
            return true;
        }
    }
    return false;
}

void zm_map_toggle(void)
{
    s_open = !s_open;
    s_jumpReady = false;
    s_placeReady = false;
    s_typeMenu = false;
    if (s_open) {
        map_build_all();
        // The director's own room, else the first survivor's, else the first
        // room on the map.
        ZmSurvivorInfo list[ZM_NET_MAX_PLAYERS];
        int n = zm_survivor_list(list, ZM_NET_MAX_PLAYERS);
        if (!map_select(g_stageId, g_roomId) && !(n > 0 && map_select(list[0].stage, list[0].room))) {
            s_sel = -1;
            map_move(1, 0, false);
        }
    }
}

// Raw pad presses while open (the game's pad word: the arrows, action 0x80,
// run 0x40, aim 0x08, START 0x800; OPTIONS 0x900 closes the map, from
// ZombieMode.cpp):
//   arrows   the nearest room that way
//   action   go there, into its first monster (refused in a room without one)
//   aim      place the chosen monster type there
//   run      the monster type list: up/down pick, action or run keeps it
//   LB / RB  (PAD_MOD1 / PAD_MOD2, keyboard Shift / Tab) the previous / next
//            monster type, list open or not
void zm_map_input(unsigned int rawPressed)
{
    if (!s_open) return;
    if (s_readOnly) {
        if (rawPressed & 0x1000) map_move(0, -1);
        if (rawPressed & 0x4000) map_move(0, 1);
        if (rawPressed & 0x8000) map_move(-1, 0);
        if (rawPressed & 0x2000) map_move(1, 0);
        return;
    }
    if (rawPressed & PAD_MOD1) s_monster = (s_monster + MAP_MONSTER_TYPES - 1) % MAP_MONSTER_TYPES;
    if (rawPressed & PAD_MOD2) s_monster = (s_monster + 1) % MAP_MONSTER_TYPES;
    if (s_typeMenu) {
        if (rawPressed & 0x1000) s_monster = (s_monster + MAP_MONSTER_TYPES - 1) % MAP_MONSTER_TYPES;
        if (rawPressed & 0x4000) s_monster = (s_monster + 1) % MAP_MONSTER_TYPES;
        if (rawPressed & (0x0080 | 0x0040 | 0x0800)) s_typeMenu = false;
        return;
    }
    if (rawPressed & 0x0040) {
        s_typeMenu = true;
        return;
    }
    if (rawPressed & 0x1000) map_move(0, -1);
    if (rawPressed & 0x4000) map_move(0, 1);
    if (rawPressed & 0x8000) map_move(-1, 0);
    if (rawPressed & 0x2000) map_move(1, 0);
    if ((rawPressed & 0x0008) && map_selectable(s_selSheet, s_sel)) {
        s_placeReady = true;
        s_placeStage = map_stage_of_sheet(s_selSheet);
        s_placeRoom = (unsigned char)s_sel;
    }
    if ((rawPressed & 0x0080) && map_selectable(s_selSheet, s_sel)) {
        unsigned char st = map_stage_of_sheet(s_selSheet);
        if (!(st == g_stageId && s_sel == g_roomId)) {
            s_jumpReady = true;
            s_jumpStage = st;
            s_jumpRoom = (unsigned char)s_sel;
            // Left open: ZombieMode.cpp closes the map once the jump is under
            // way, and a refused one (no monster there) notes why over it.
        }
    }
}

bool zm_map_take_place(unsigned char* stage, unsigned char* room, unsigned char* id, const char** name)
{
    if (!s_placeReady) return false;
    s_placeReady = false;
    *stage = s_placeStage;
    *room = s_placeRoom;
    *id = kMonsters[s_monster].id;
    *name = kMonsters[s_monster].name;
    // Its count on the map is stale now: read it again.
    for (int s = 0; s < MAP_SHEETS; s++) {
        if (map_stage_of_sheet(s) == s_placeStage && s_placeRoom < MAP_ROOMS) s_zombies[s][s_placeRoom] = -2;
    }
    return true;
}

bool zm_map_take_jump(unsigned char* stage, unsigned char* room)
{
    if (!s_jumpReady) return false;
    s_jumpReady = false;
    *stage = s_jumpStage;
    *room = s_jumpRoom;
    return true;
}

// Depth: draw_rect(flags 1) sorts at blend*16 + 500. Below 0x400 it is an
// overlay - in front of the 3D pass - and above PrintText8x14's glyphs
// (brightness*16 + 500: 612 for colour 0x7F, 980 for 0x8F), so the text
// stays on top. Equal depths sort unstably, hence one level per layer.
#define MAP_DEPTH_BACKDROP 32    // 1012
#define MAP_DEPTH_ROOMS    31    // 996

static void map_fill(int x, int y, int w, int h, int r, int g, int b, int depth)
{
    if (s_setup) {
        w = zm_setup_x(x + w) - zm_setup_x(x);
        h = zm_setup_y(y + h) - zm_setup_y(y);
        x = zm_setup_x(x); y = zm_setup_y(y);
    }
    RectDrawDesc rect;
    memset(&rect, 0, sizeof(rect));
    rect.textureId = 0;          // variant 0: opaque, literal colour
    rect.x = (short)(x - g_ScreenOffsetX);
    rect.y = (short)(y - g_ScreenOffsetY);
    rect.w = (short)w;
    rect.h = (short)h;
    rect.r = (unsigned char)r; rect.g = (unsigned char)g; rect.b = (unsigned char)b;
    draw_rect(&rect, depth, 1);
}

// Room conditions share the fill instead of hiding one another. Draw clipped
// horizontal runs so the same diagonal bands work in both map layouts.
struct MapRoomColors {
    unsigned char rgb[8][3];
    int count;

    MapRoomColors() : count(0) {}
    void add(int r, int g, int b) {
        for (int i = 0; i < count; i++)
            if (rgb[i][0] == r && rgb[i][1] == g && rgb[i][2] == b) return;
        if (count >= 8) return;
        rgb[count][0] = (unsigned char)r;
        rgb[count][1] = (unsigned char)g;
        rgb[count++][2] = (unsigned char)b;
    }
};

static void map_room_fill(int x, int y, int w, int h, const MapRoomColors& colors)
{
    if (colors.count == 1) {
        map_fill(x, y, w, h, colors.rgb[0][0], colors.rgb[0][1], colors.rgb[0][2], MAP_DEPTH_ROOMS);
        return;
    }
    const int stripeWidth = 4;
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; ) {
            int diagonal = x + col + y + row;
            int band = diagonal / stripeWidth;
            int index = ((band % colors.count) + colors.count) % colors.count;
            int run = stripeWidth - ((diagonal % stripeWidth) + stripeWidth) % stripeWidth;
            if (run > w - col) run = w - col;
            map_fill(x + col, y + row, run, 1,
                     colors.rgb[index][0], colors.rgb[index][1], colors.rgb[index][2], MAP_DEPTH_ROOMS);
            col += run;
        }
    }
}

static void map_text(int x, int y, unsigned char color, const char* text)
{
    if (s_setup) { zm_setup_text(x, y, color, text); return; }
    snprintf(PRINT_TEXT_BUFFER, sizeof(PRINT_TEXT_BUFFER), "%s", text);
    zm_text_encode(PRINT_TEXT_BUFFER);
    PrintText8x14Scaled((short)x, (short)y, color, 0, 0.75f);
}

static const char* map_room_name(unsigned char stage, unsigned char room, char* buf, int len)
{
    const char* name = DebugRoom_Name(stage, room);
    if (name != NULL) return name;
    snprintf(buf, len, "ROOM %X%02X", (unsigned)(stage + 1) & 0xF, (unsigned)room);
    return buf;
}

// The monster type list, a panel over the right of the map: one line per
// type, the current one with the arrow, then its price - or, while it is
// locked, the Yawn that unlocks it ("YAWN 2": both beaten). The panel sits one layer in front of
// the rooms and behind the text (dim text sorts at 612, the panel at 980).
#define MAP_DEPTH_PANEL 30       // 980
// The lobby's popup: a panel in the middle of the map, one item a line,
// the highlighted one with the arrow, unavailable ones dim.
static void map_draw_popup(void)
{
    const int lineH = 12, w = 176, x = (320 - w) / 2, y = 70;
    const int h = 10 + s_menuCount * lineH;
    map_fill(x, y, w, h, 0x18, 0x18, 0x28, MAP_DEPTH_PANEL);
    for (int i = 0; i < s_menuCount; i++) {
        char line[48];
        bool on = s_menuEnabled == NULL || s_menuEnabled[i];
        snprintf(line, sizeof(line), "%s%s", i == s_menuSel ? "> " : "  ", s_menuItems[i]);
        map_text(x + 8, y + 5 + i * lineH, !on ? 0x2F : i == s_menuSel ? 0x71 : 0x7F, line);
    }
}

static void map_draw_type_menu(void)
{
    const int x = 128, y = 24, w = 184, lineH = 11;
    const int h = 22 + MAP_MONSTER_TYPES * lineH;
    map_fill(x, y, w, h, 0x18, 0x18, 0x28, MAP_DEPTH_PANEL);
    map_text(x + 8, y + 4, 0x7F, "MONSTER TYPE     COST");
    for (int i = 0; i < MAP_MONSTER_TYPES; i++) {
        char line[40], price[12];
        int locked = zm_econ_locked(kMonsters[i].id);
        int left = zm_trap_cooldown_ms(kMonsters[i].id);                // a trap cooling down
        if (locked > 0) {
            snprintf(price, sizeof(price), "YAWN %d", zm_econ_unlock_bosses(kMonsters[i].id));
        } else if (left > 0) {
            int sec = (left + 999) / 1000;
            snprintf(price, sizeof(price), "%d:%02d", sec / 60, sec % 60);
        } else {
            snprintf(price, sizeof(price), "%d", zm_econ_cost(kMonsters[i].id));
        }
        snprintf(line, sizeof(line), "%s%-12s %5s", i == s_monster ? "> " : "  ", kMonsters[i].name, price);
        map_text(x + 8, y + 20 + i * lineH, i == s_monster ? 0x71 : 0x7F, line);
    }
}

void zm_map_draw(void)
{
    if (!s_open) return;
    if (s_setup) zm_guide_background(true);
    map_build_all();
    map_update_counts();
    if (s_sel >= 0 && !map_selectable(s_selSheet, s_sel)) s_sel = -1;

    ZmSurvivorInfo surv[ZM_NET_MAX_PLAYERS];
    int survCount = zm_survivor_list(surv, ZM_NET_MAX_PLAYERS);

    // A black backdrop under the map (the scene stays behind it).
    if (!s_setup) map_fill(0, 0, 320, 240, 0, 0, 0, MAP_DEPTH_BACKDROP);

    MapLayout L;
    map_layout(&L);
    for (int i = 0; i < s_rectCount; i++) {
        const MapRect& m = s_rects[i];
        if (!map_sheet_shown(m.sheet)) continue;
        unsigned char stage = map_stage_of_sheet(m.homeSheet);
        int group = stage % 5;
        // Selection stays solid white so it remains distinct from room conditions.
        if (m.homeSheet == s_selSheet && m.room == s_sel) {
            map_fill(L.ox[m.sheet] + m.x * L.cell, L.oy[m.sheet] + m.y * L.cell,
                     m.w * L.cell, m.h * L.cell, 0xF0, 0xF0, 0xF0, MAP_DEPTH_ROOMS);
            continue;
        }
        int r = 0x38, g = 0x38, b = 0x48;     // a room
        bool visited = Flg_ck((int)g_RoomFlags, g_StageRoomFlagOffset[group] + m.room) != 0;
        if (visited) { r = 0x60; g = 0x60; b = 0x78; }
        MapRoomColors colors;
        if (s_route) {
            // The route map shows all applicable conditions: key-locked doors
            // blue; monsters orange (Richard's radio); other new pickups teal
            // (the director's); keys and crests purple; this survivor's room
            // green and the others' red (in the game); the selection white.
            unsigned char to[4], key[4];
            if (zm_random_room_locks(stage, m.room, to, key, 4) > 0) { colors.add(0x30, 0x50, 0xC0); }
            if (s_routeMonsters && m.room < MAP_ROOMS && s_zombies[m.homeSheet][m.room] > 0) {
                colors.add(0xA0, 0x58, 0x18);
            }
            if (s_routeLevel >= ZM_ROUTE_KEYS) {
                unsigned char ids[8], qtys[8];
                bool isNew[8];
                int n = zm_random_room_items(stage, m.room, ids, qtys, isNew, 8);
                bool keyHere = false, pickupHere = false;
                for (int k = 0; k < n; k++) {
                    if (map_is_key_item(ids[k])) keyHere = true;
                    else if (isNew[k] && s_routeLevel >= ZM_ROUTE_ALL) pickupHere = true;
                }
                if (pickupHere) { colors.add(0x30, 0x90, 0x90); }
                if (keyHere) { colors.add(0x90, 0x40, 0xC0); }
            }
            if (s_routePlayers) {
                if (m.room == g_roomId && stage == g_stageId) {
                    colors.add(0x20, 0xA0, 0x30);
                }
                for (int k = 0; k < survCount; k++) {
                    const ZmSurvivorInfo& v = surv[k];
                    if (!v.dead && m.room == v.room && stage == v.stage) {
                        colors.add(0xE0, 0x20, 0x20);
                    }
                }
            }
            if (colors.count == 0) colors.add(r, g, b);
            map_room_fill(L.ox[m.sheet] + m.x * L.cell, L.oy[m.sheet] + m.y * L.cell,
                          m.w * L.cell, m.h * L.cell, colors);
            continue;
        }
        // The director's: safe rooms dark (closed to it), monsters orange,
        // keys and crests still lying there purple (as on the route map).
        if (!s_readOnly && (zm_random_room_safe(stage, m.room) || zm_yawn_director_closed(stage, m.room))) {
            colors.add(0x1C, 0x1C, 0x24);
        }
        if (m.room < MAP_ROOMS && s_zombies[m.homeSheet][m.room] > 0) { colors.add(0xA0, 0x58, 0x18); }
        if (!s_readOnly && zm_random_room_key_left(stage, m.room)) { colors.add(0x90, 0x40, 0xC0); }
        if (m.room == g_roomId && stage == g_stageId) {
            colors.add(0x20, 0xA0, 0x30);
        }
        // Survivors add red to the room conditions.
        for (int k = 0; k < survCount; k++) {
            const ZmSurvivorInfo& v = surv[k];
            if (!v.dead && m.room == v.room && stage == v.stage) {
                colors.add(0xE0, 0x20, 0x20);
            }
        }
        if (colors.count == 0) colors.add(r, g, b);
        map_room_fill(L.ox[m.sheet] + m.x * L.cell, L.oy[m.sheet] + m.y * L.cell,
                      m.w * L.cell, m.h * L.cell, colors);
    }

    char line[96], nameBuf[24];
    for (int s = 0; s < MAP_SHEETS; s++) {
        if (!map_sheet_shown(s)) continue;
        const char* label = kAreaNames[kSheetArea[s]];
        // 1F and 2F from their column's left edge: the director's points
        // (ZombieEconomy.cpp) sit at the top right.
        int lx = L.labelX[s] - (int)strlen(label) * 3;
        if (s != MAP_SHEET_B1 && zm_econ_on()) lx = L.labelX[s] - L.colW / 2;
        map_text(lx, L.labelY[s], 0x8F, label);
    }

    if (s_route) {
        // The selected room, under 2F: its key doors (the key, then where the
        // door leads) and - the director's - what lies there. At most four
        // lines (the column ends above the status line).
        int ty = L.listY;
        int lines = 0;
        if (s_sel >= 0) {
            unsigned char stage = map_stage_of_sheet(s_selSheet);
            unsigned char to[4], key[4];
            int n = zm_random_room_locks(stage, (unsigned char)s_sel, to, key, 4);
            for (int i = 0; i < n && lines < 4; i++, lines++) {
                snprintf(line, sizeof(line), "%s > %s", map_key_short(key[i]),
                         map_room_name(stage, to[i], nameBuf, sizeof(nameBuf)));
                line[19] = '\0';
                map_text(L.listX, ty, 0x7F, line);
                ty += 11;
            }
            if (s_routeMonsters && s_sel < MAP_ROOMS && s_zombies[s_selSheet][s_sel] > 0 && lines < 4) {
                snprintf(line, sizeof(line), "MONSTERS: %d", (int)s_zombies[s_selSheet][s_sel]);
                map_text(L.listX, ty, 0x8F, line);
                ty += 11;
                lines++;
            }
            if (s_routeLevel >= ZM_ROUTE_KEYS) {
                unsigned char ids[8], qtys[8];
                bool isNew[8];
                int m = zm_random_room_items(stage, (unsigned char)s_sel, ids, qtys, isNew, 8);
                for (int i = 0; i < m && lines < 4; i++) {
                    if (!map_is_key_item(ids[i]) && s_routeLevel < ZM_ROUTE_ALL) continue;
                    snprintf(line, sizeof(line), "%s%s", isNew[i] ? "+" : " ", map_item_name(ids[i]));
                    map_text(L.listX, ty, 0x8F, line);
                    ty += 11;
                    lines++;
                }
            }
            if (lines == 0) map_text(L.listX, ty, 0x7F, "NOTHING HERE");
            snprintf(line, sizeof(line), "ROOM: %s", map_room_name(stage, (unsigned char)s_sel, nameBuf, sizeof(nameBuf)));
            map_text(8, 213, 0x8F, line);
        }
        // The survivors' timeout (ZombieTimeout.cpp), right of the room name.
        const char* timeout = s_routePlayers ? zm_timeout_map_line() : NULL;
        if (timeout != NULL) map_text(312 - (int)strlen(timeout) * 6, 213, 0x8F, timeout);
        map_text(8, 200, 0x7F, s_routeStatus);
        map_text(8, 226, 0x7F, s_routeHelp);
        // The survivor's files: the keypad's pass number, once its note is read.
        const char* files = s_routePlayers ? zm_access_files_line() : NULL;
        if (files != NULL) map_text(312 - (int)strlen(files) * 6, 226, 0x8F, files);
        if (s_menuCount > 0) map_draw_popup();
        return;
    }

    // Who is where, under 2F.
    int ty = L.listY;
    for (int k = 0; k < survCount; k++) {
        snprintf(line, sizeof(line), "%s: %s", zm_char_name(surv[k].character),
                 surv[k].dead ? "DEAD" : map_room_name(surv[k].stage, surv[k].room, nameBuf, sizeof(nameBuf)));
        map_text(L.listX, ty, 0x7F, line);
        ty += 11;
    }
    if (survCount == 0) {
        map_text(L.listX, ty, 0x7F, "SURVIVORS: UNKNOWN");
        ty += 11;
    }
    snprintf(line, sizeof(line), "YOU: %s", map_room_name(g_stageId, g_roomId, nameBuf, sizeof(nameBuf)));
    map_text(L.listX, ty, 0x7F, line);
    if (s_sel >= 0) {
        int n = s_zombies[s_selSheet][s_sel];
        if (zm_econ_on()) {
            // The director's: every monster there against the room's cap.
            unsigned char st = map_stage_of_sheet(s_selSheet);
            snprintf(line, sizeof(line), "ROOM: %s - %d OF %d SLOTS",
                     map_room_name(st, (unsigned char)s_sel, nameBuf, sizeof(nameBuf)),
                     zm_room_monster_slots(st, (unsigned char)s_sel), zm_econ_room_cap(st, (unsigned char)s_sel));
        } else if (n >= 0) {
            snprintf(line, sizeof(line), "ROOM: %s - %d MONSTER%s",
                     map_room_name(map_stage_of_sheet(s_selSheet), (unsigned char)s_sel, nameBuf, sizeof(nameBuf)),
                     n, n == 1 ? "" : "S");
        } else {
            snprintf(line, sizeof(line), "ROOM: %s",
                     map_room_name(map_stage_of_sheet(s_selSheet), (unsigned char)s_sel, nameBuf, sizeof(nameBuf)));
        }
        // The white selection hides a survivor's red: name who is in there.
        unsigned char selStage = map_stage_of_sheet(s_selSheet);
        if (!s_readOnly) {
            size_t len = strlen(line);
            if (zm_random_room_safe(selStage, (unsigned char)s_sel)) {
                snprintf(line + len, sizeof(line) - len, " - SAFE");
            } else if (zm_yawn_director_closed(selStage, (unsigned char)s_sel)) {
                snprintf(line + len, sizeof(line) - len, " - BOSS");
            } else if (zm_random_room_key_left(selStage, (unsigned char)s_sel)) {
                snprintf(line + len, sizeof(line) - len, " - KEY");
            }
        }
        for (int k = 0; k < survCount; k++) {
            if (!surv[k].dead && surv[k].stage == selStage && surv[k].room == s_sel) {
                size_t len = strlen(line);
                snprintf(line + len, sizeof(line) - len, " - %s", zm_char_name(surv[k].character));
            }
        }
        map_text(8, 200, 0x8F, line);
    } else {
        map_text(8, 200, 0x7F, "ARROWS: PICK A ROOM");
    }
    if (s_readOnly) {
        map_text(8, 213, 0x7F, "RADIO: ORANGE ROOMS HAVE MONSTERS");
        map_text(8, 226, 0x7F, "ARROWS: ROOM  OPTIONS: CLOSE");
        return;
    }
    if (s_typeMenu) {
        map_draw_type_menu();
        map_text(8, 213, 0x7F, "UP-DOWN OR LB-RB: MONSTER TYPE");
        map_text(8, 226, 0x7F, "ACTION OR RUN: DONE");
        return;
    }
    snprintf(line, sizeof(line), "AIM: %s %s  RUN: LIST  LB-RB: TYPE",
             zm_is_trap_id(kMonsters[s_monster].id) ? "SET" : "PLACE", kMonsters[s_monster].name);
    map_text(8, 213, 0x7F, line);
    map_text(8, 226, 0x7F, "ARROWS: ROOM  ACTION: GO  OPTIONS: CLOSE");
}
