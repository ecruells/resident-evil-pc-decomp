#include "ZombieModeInternal.h"
#include "ZombieNet.h"
#include "../../DebugPrint.h"
#include "../../system/ConfigFile.h"
#include "../../platform/platform.h"
#include "../FileLoader.h"
#include "../SFXIds.h"
#include "../../system/AssetPath.h"
#include "../OptionsMenu.h"            // options_render_entity
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

// ============================================================================
// ZombieLobby.cpp - the zombie mod's NEW GAME screen (port-added): single
// player against the AI, host a game as the director (or against the AI
// director, ZombieDirectorAI.cpp, at the level picked on that row), join one
// as a survivor, or rejoin the last match joined (its saved match id finds
// the seat). Chained from the title screen's NEW GAME when [Mods]
// PlayInfested is on. Single player goes on to the normal character select;
// a multiplayer game skips it and starts Chris's scenario for everyone
// (lobby_start_game) - the survivors' picks are the models they wear.
//
// A session's screens, every choice an item of a menu (Action picks):
//   1. the join screen (lobby_room): who is in; the host advances to the map.
//   2. the map review (lobby_map_review): the arrows browse it, Action opens
//      its menu. Against a human director each side may veto once and the
//      director starts the game (GO); against the AI everyone just looks.
//   3. the character select (lobby_character_select), then
//   4. the pregame lobby (lobby_pregame): the survivors and their characters,
//      CHANGE CHARACTER and VIEW THE MAP. Against a human director it counts
//      down to the survivors' spawn; against the AI each survivor marks
//      itself ready and the host starts.
//
// Drawn over the generated CRT backdrop with inset text, redrawn every frame. Read off the raw PSX pad word the
// title screen uses (g_PlayerPadPressed): 0x1000 up, 0x4000 down, 0x8000 left,
// 0x2000 right, 0x80 confirm (Enter / cross), 0x40 cancel (Esc / circle).
// ============================================================================

extern void title_state(void);                  // TitleScreen.cpp
extern void characterSelectionScreen(void);     // CharacterSelectionScreen.cpp
extern void game_start(void);                   // GameStart.cpp

#define PAD_UP      0x1000
#define PAD_DOWN    0x4000
#define PAD_LEFT    0x8000
#define PAD_RIGHT   0x2000
#define PAD_CONFIRM 0x0080
#define PAD_CANCEL  0x0040
#define PAD_OPTIONS 0x0900

enum {
    ROW_SINGLE = 0,
    ROW_HOST,
    ROW_HOST_AI,
    ROW_JOIN,
    ROW_REJOIN,
    ROW_ADDRESS,
    ROW_PORT,
    ROW_BACK,
    ROW_COUNT,
};

// The address is edited as a fixed "ddd.ddd.ddd.ddd" field and the port as
// five digits, one digit at a time with the d-pad (the game has no text input).
static char s_addr[16];
static char s_port[6];

static void lobby_load_fields(void)
{
    unsigned int a = 127, b = 0, c = 0, d = 1;
    sscanf(g_zmNetAddress, "%u.%u.%u.%u", &a, &b, &c, &d);
    snprintf(s_addr, sizeof(s_addr), "%03u.%03u.%03u.%03u", a & 0xFF, b & 0xFF, c & 0xFF, d & 0xFF);
    snprintf(s_port, sizeof(s_port), "%05u", (unsigned)g_zmNetPort);
}

static void lobby_store_fields(void)
{
    unsigned int a = 0, b = 0, c = 0, d = 0;
    sscanf(s_addr, "%u.%u.%u.%u", &a, &b, &c, &d);
    snprintf(g_zmNetAddress, sizeof(g_zmNetAddress), "%u.%u.%u.%u",
             a > 255 ? 255 : a, b > 255 ? 255 : b, c > 255 ? 255 : c, d > 255 ? 255 : d);
    unsigned int port = (unsigned int)atoi(s_port);
    if (port == 0 || port > 65535) port = 27960;
    g_zmNetPort = (unsigned short)port;
}

// Step the digit under the cursor; the cursor skips the dots.
static void lobby_edit(char* field, int length, int* cursor, unsigned int pressed)
{
    int oldCursor = *cursor;
    char oldDigit = field[*cursor];
    if (pressed & PAD_LEFT)  { do { (*cursor)--; } while (*cursor >= 0 && field[*cursor] == '.'); }
    if (pressed & PAD_RIGHT) { do { (*cursor)++; } while (*cursor < length && field[*cursor] == '.'); }
    if (*cursor < 0) *cursor = 0;
    if (*cursor >= length) *cursor = length - 1;
    char* ch = &field[*cursor];
    if (pressed & PAD_UP)   *ch = (char)(*ch == '9' ? '0' : *ch + 1);
    if (pressed & PAD_DOWN) *ch = (char)(*ch == '0' ? '9' : *ch - 1);
    if (*cursor != oldCursor || *ch != oldDigit) play_sfx(SFX_UI_BANK, SFX_UI_CURSOR);
}

void zm_setup_text(int x, int y, unsigned char color, const char* text)
{
    snprintf(PRINT_TEXT_BUFFER, sizeof(PRINT_TEXT_BUFFER), "%s", text);
    zm_text_encode(PRINT_TEXT_BUFFER);
    PrintText8x14Scaled((short)zm_setup_x(x), (short)zm_setup_y(y), color, 0, 0.675f);
}

static void lobby_print(int x, int y, unsigned char color, const char* text)
{
    zm_setup_text(x, y, color, text);
}

// A field with the digit under the cursor marked: printed whole, then a
// caret line under it.
static void lobby_print_field(int x, int y, const char* label, const char* field, int cursor, bool editing)
{
    char line[64];
    snprintf(line, sizeof(line), "%s%s", label, field);
    lobby_print(x, y, editing ? 0x8F : 0x7F, line);
    if (editing) {
        char caret[64];
        int at = (int)strlen(label) + cursor;
        memset(caret, ' ', sizeof(caret));
        caret[at] = '-';                // under the digit (zm_text_encode: a dash)
        caret[at + 1] = '\0';
        lobby_print(x, y + 12, 0x8F, caret);
    }
}

// Into a multiplayer game: what the character select's last state does
// (CharacterSelectionScreen.cpp case 6), for Chris's scenario, then
// DisplayIntroAndStartGame without the opening movie.
static void lobby_start_game(void)
{
    zm_guide_background(false);
    lobby_store_fields();
    ConfigFile_Save();
    g_SelectedCharactedId = 0;                       // Chris's scenario, whoever plays
    g_main_state_flags &= ~MSF_CHAR_VARIANT;
    g_bGameActive = 2;
    nullsub_0047eb80();
    // Let the confirmation beep play before game_start replaces the sound banks.
    Task_sleep(8);
    sounds_reset();
    Task_sleep(1);
    Task_chain((void*)game_start);
}

// The players in the session: the director, then every survivor seat in
// order (against the AI the host's own seat 0 is the first survivor).
// Characters show once the map is up - before that they are only the
// host's placeholders; against the AI each survivor's READY too.
static void lobby_draw_players(int x, int y)
{
    char line[80];
    int self = zm_net_self();
    int ai = zm_net_ai_level();
    bool picking = zm_net_lobby_started() || zm_net_lobby_phase() != ZM_LOBBY_JOIN;
    lobby_print(x, y, 0x8F, "PLAYERS");
    y += 12;
    if (ai > 0) snprintf(line, sizeof(line), "DIRECTOR    AI - %s", zm_ai_level_name(ai));
    else snprintf(line, sizeof(line), "DIRECTOR    %s", self == 0 ? "YOU" : "HOST");
    lobby_print(x, y, 0x7F, line);
    int n = 0;
    for (int i = ai > 0 ? 0 : 1; i < zm_net_seat_limit(); i++) {
        y += 12;
        int c = zm_net_char(i);
        n++;
        if (c < 0) {
            snprintf(line, sizeof(line), "SURVIVOR %d  OPEN", n);
            lobby_print(x, y, 0x2F, line);
            continue;
        }
        const char* state = "";
        if (ai > 0 && picking) state = (i == 0 || zm_net_vote(i) == ZM_VOTE_ACCEPT) ? "  READY" : "  NOT READY";
        snprintf(line, sizeof(line), "SURVIVOR %d  %s%s%s%s", n, picking ? zm_char_name(c) : "JOINED",
                 i == 0 && ai > 0 ? "  HOST" : "", state, i == self ? "  - YOU" : "");
        lobby_print(x, y, 0x7F, line);
    }
}

// A screen's menu: Up / Down move, Action picks an available item. Items
// carry an id (MI_*) so a screen can build its list from what applies.
enum {
    MI_GENERATE, MI_TO_MAP, MI_AI_LEVEL, MI_TIMEOUT, MI_LEAVE, MI_START, MI_VETO,
    MI_ACCEPT, MI_BACK_LOBBY, MI_CONTINUE, MI_CLOSE, MI_READY, MI_CHANGE_CHAR, MI_VIEW_MAP,
};
#define LOBBY_MENU_MAX 8

struct LobbyMenu {
    int  count;
    int  id[LOBBY_MENU_MAX];
    bool on[LOBBY_MENU_MAX];
    char text[LOBBY_MENU_MAX][48];
    const char* ptr[LOBBY_MENU_MAX];

    LobbyMenu() : count(0) {}
    void add(int item, bool enabled, const char* fmt, ...)
    {
        if (count >= LOBBY_MENU_MAX) return;
        va_list args;
        va_start(args, fmt);
        vsnprintf(text[count], sizeof(text[count]), fmt, args);
        va_end(args);
        id[count] = item;
        on[count] = enabled;
        ptr[count] = text[count];
        count++;
    }
    int indexOf(int item) const
    {
        for (int i = 0; i < count; i++) if (id[i] == item) return i;
        return -1;
    }
};

// Move the cursor; returns the picked item's id, or -1.
static int lobby_menu_input(const LobbyMenu& m, int* sel, unsigned int pressed)
{
    if (m.count == 0) return -1;
    if (*sel >= m.count) *sel = m.count - 1;
    if (*sel < 0) *sel = 0;
    int old = *sel;
    if (pressed & PAD_UP)   *sel = (*sel + m.count - 1) % m.count;
    if (pressed & PAD_DOWN) *sel = (*sel + 1) % m.count;
    if (*sel != old) play_sfx(SFX_UI_BANK, SFX_UI_CURSOR);
    if (!(pressed & PAD_CONFIRM)) return -1;
    if (!m.on[*sel]) {
        play_sfx(SFX_UI_BANK, SFX_UI_CANCEL);
        return -1;
    }
    play_sfx(SFX_UI_BANK, SFX_UI_DECIDE);
    return m.id[*sel];
}

static void lobby_menu_draw(const LobbyMenu& m, int sel, int x, int y)
{
    char line[56];
    for (int i = 0; i < m.count; i++) {
        snprintf(line, sizeof(line), "%s%s", i == sel ? "> " : "  ", m.text[i]);
        lobby_print(x, y + i * 14, !m.on[i] ? 0x2F : i == sel ? 0x8F : 0x7F, line);
    }
}

// ---------------------------------------------------------------------------
// The character select screen (a survivor in the lobby presses action)
//
// The list of characters, two rows of three across the top; the highlighted one's full name,
// toughness, description, starting items and perk under it; its model on the
// right, turning in place. The model is drawn the way the options menu draws
// the player (options_menu 0x004761b0 / options_render_entity 0x004775b0):
// three lights, the camera at (0, 0, 5000) on the origin, the character at
// (-2400, 2100, -700) - right of centre, feet low - and its weapon file's
// standing animation (1). No game is loaded yet, so the player entity is
// free to borrow: SetupCharacterData loads the highlighted character's model
// (zombie_mode_preview_skin), and InitializeGame loads the real one later.
// The screen runs in the "rebuild" mode the 3D menus use, over a plain
// backdrop image.
// ---------------------------------------------------------------------------
static int            s_csLights[3][4];

static unsigned short s_guideBackdrop[320 * 240];
static bool s_guideBackdropBuilt = false;
static bool s_guideBackdropActive = false;
static unsigned int s_setupPreviousScreenMode;

// Distance inside the rounded monitor shell, in screen pixels.
static float guide_shell_depth(float x, float y)
{
    float qx = fabsf(x - 159.5f) - 147.5f;
    float qy = fabsf(y - 119.5f) - 107.5f;
    float ox = qx > 0.0f ? qx : 0.0f;
    float oy = qy > 0.0f ? qy : 0.0f;
    float inner = qx > qy ? qx : qy;
    if (inner > 0.0f) inner = 0.0f;
    return 12.0f - sqrtf(ox * ox + oy * oy) - inner;
}

// A raised plastic face with a rounded outer shoulder and a sloped inner
// lip. Its height gradient supplies surface normals for actual lighting.
static float guide_shell_height(float depth)
{
    if (depth <= 0.0f || depth >= 8.0f) return 0.0f;
    if (depth < 2.5f) {
        float t = depth / 2.5f;
        return 3.5f * sqrtf(t * (2.0f - t));
    }
    if (depth < 5.0f) return 3.5f + 0.15f * sinf((depth - 2.5f) * 1.256637f);
    float t = (depth - 5.0f) / 3.0f;
    return 3.5f * (1.0f - t * t);
}

static int guide_color(float value)
{
    return value < 0.0f ? 0 : value > 255.0f ? 255 : (int)value;
}

// Render the beige casing as a lit relief, rather than flat border stripes.
// The light comes from above/right, matching the reflection on the glass.
static void guide_build_backdrop(void)
{
    if (s_guideBackdropBuilt) return;
    for (int y = 0; y < 240; ++y) {
        for (int x = 0; x < 320; ++x) {
            float depth = guide_shell_depth((float)x, (float)y);
            float r, g, b;
            unsigned int noise = (unsigned int)(x + y * 320) * 1664525u + 1013904223u;
            noise ^= noise >> 13;
            float grain = (float)(noise & 15) * 0.32f - 2.4f;
            if (depth < 0.0f) {
                r = 12; g = 11; b = 9; // rounded casing silhouette
            } else if (depth < 8.0f) {
                float nx = guide_shell_height(guide_shell_depth(x - 0.5f, (float)y)) -
                           guide_shell_height(guide_shell_depth(x + 0.5f, (float)y));
                float ny = guide_shell_height(guide_shell_depth((float)x, y - 0.5f)) -
                           guide_shell_height(guide_shell_depth((float)x, y + 0.5f));
                float norm = 1.0f / sqrtf(nx * nx + ny * ny + 1.0f);
                nx *= norm; ny *= norm;
                float diffuse = nx * 0.45f - ny * 0.60f + norm * 0.66f;
                if (diffuse < 0.0f) diffuse = 0.0f;
                float shine = nx * 0.247f - ny * 0.329f + norm * 0.911f;
                if (shine < 0.0f) shine = 0.0f;
                shine *= shine; shine *= shine; shine *= shine; shine *= shine;
                float light = 0.46f + diffuse * 0.63f;
                // Dirt gathers in the inner seam; broad mottling gives the
                // aged plastic a little variation without obscuring its form.
                float seam = depth > 7.0f ? (depth - 7.0f) * 48.0f : 0.0f;
                float wear = sinf(x * 0.073f + y * 0.041f) * 2.0f +
                             sinf(x * 0.019f - y * 0.057f) * 2.0f;
                r = 195 * light + shine * 28 + grain + wear - seam;
                g = 183 * light + shine * 27 + grain + wear - seam;
                b = 156 * light + shine * 24 + grain + wear - seam;
            } else {
                float nx = (x - 160) / 152.0f;
                float ny = (y - 120) / 112.0f;
                float glow = 1.0f - (nx * nx + ny * ny) * 0.4f;
                if (glow < 0.0f) glow = 0.0f;
                float scan = (y & 1) ? -2.5f : 1.0f;
                float dx = (x - 292) / 68.0f;
                float dy = (y - 22) / 52.0f;
                float reflection = 1.0f - dx * dx - dy * dy;
                float glare = reflection > 0.0f ? reflection * reflection * 24 : 0;
                // Soft contact shadow makes the glass sit behind the lip.
                float shadow = depth < 13.0f ? (13.0f - depth) / 5.0f : 0;
                float shade = 1.0f - shadow * 0.65f;
                r = (8 + glow * 5 + scan + glare) * shade;
                g = (14 + glow * 7 + scan + glare) * shade;
                b = (17 + glow * 9 + scan + glare) * shade;
            }
            // Ordered dithering preserves the gradual shading in PSX RGB555.
            static const int dither[4][4] = {{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
            float d = (dither[y & 3][x & 3] - 7.5f) * 0.5f;
            int red = guide_color(r + d), green = guide_color(g + d), blue = guide_color(b + d);
            s_guideBackdrop[y * 320 + x] = (unsigned short)(0x8000 |
                ((red * 31 / 255) << 0) | ((green * 31 / 255) << 5) | ((blue * 31 / 255) << 10));
        }
    }
    s_guideBackdropBuilt = true;
}

void zm_guide_background(bool open)
{
    if (open) {
        if (!s_guideBackdropActive) s_setupPreviousScreenMode = g_main_state_flags & MSF_SCREEN_MODE_MASK;
        // STANDALONE suppresses AddBackgroundQuad, even with an image loaded.
        // Reapply after leaving a 3D preview for the lobby or map review.
        g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_REBUILD;
    }
    if (open == s_guideBackdropActive) return;
    s_guideBackdropActive = open;
    if (open) {
        guide_build_backdrop();
        display_image(8, s_guideBackdrop, 320, 240);
        title_setup_texture_pages(8, 1);
    } else {
        // display_image has ONE shared background texture, not one per slot.
        // Selecting slot zero alone cannot bring back the title's eye image.
        LoadFile(GAME_DATA_ROOT "data\\title.pix", g_TimImageBuffer__bitmap, 0x20);
        display_image(0, g_TimImageBuffer__bitmap, 320, 240);
        title_setup_texture_pages(0, 1);
        g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | s_setupPreviousScreenMode;
    }
}

static void cs_scene_begin(void)
{
    static const int dir[3][3] = { { 100, 0x50, -780 }, { -100, 0x50, -780 }, { -100, 0, -780 } };
    for (int i = 0; i < 3; i++) {
        s_csLights[i][0] = dir[i][0];
        s_csLights[i][1] = dir[i][1];
        s_csLights[i][2] = dir[i][2];
        unsigned char* c = (unsigned char*)&s_csLights[i][3];
        c[0] = c[1] = c[2] = 0x80;
        FUN_0040ac80(i, s_csLights[i]);
    }
    setBackColor(409, 409, 409);

    guide_build_backdrop();
    display_image(8, s_guideBackdrop, 320, 240);
    title_setup_texture_pages(8, 1);
    g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_REBUILD;

    set_scene_render_param(0xcf);
    MATRIX cam;
    memset(&cam, 0, sizeof(cam));
    cam.m[1][1] = 5000;                     // read as from (0, 0, 5000), to the origin
    MatrixToCamera(&cam);
    SetSubpixelOffset(0xa0, 0x78);
}

static void cs_scene_end(void)
{
    zombie_mode_preview_skin(-1);
    g_spriteAnimR = 0;
    g_spriteAnimG = 0;
    g_spriteAnimB = 0;
    g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;
}

static void cs_load_model(int ch)
{
    ZmPerkInfo info;
    zm_perk_describe(ch, &info);
    zombie_mode_preview_skin(ch);
    // SetupCharacterData without a weapon (equipped slot 0), then the
    // character's own first weapon - its file carries the standing animation.
    unsigned char savedEquipped = g_EquippedItemId;
    g_EquippedItemId = 0;
    g_playerEntity.id = 0;                  // Chris's scenario, as in the game
    SetupCharacterData();
    g_EquippedItemId = savedEquipped;
    LoadEquippedWeaponAnimation(info.weapon, 0xe, (unsigned int)g_animationBuffer,
                                (unsigned int)g_animObjectBuffer);
    g_playerEntity.equippedWeaponId = info.weapon;

    ENTITY = (Entity*)&g_playerEntity;
    g_playerEntity.unk_8c = 0;
    g_playerEntity.attackAnim = 1;
    g_playerEntity.animation_frame_id = 0;
    g_playerEntity.unk_bf = 0;
    // Looking from positive Z reverses screen X: negative world X is right.
    g_playerEntity.scaMatrixData.localMatrix.t[0] = -0x960;
    g_playerEntity.scaMatrixData.localMatrix.t[1] = 0x834;
    g_playerEntity.scaMatrixData.localMatrix.t[2] = -700;
    g_playerEntity.position.pad = 0;
    g_playerEntity.directionAngle = 0xce4;
    g_playerEntity.speed.x = 0;
}

static void cs_draw_model(void)
{
    g_playerEntity.zoneFlags = 1;           // options_render_entity's unconditional path
    ENTITY = (Entity*)&g_playerEntity;
    g_playerEntity.attackAnim = 1;          // standing
    g_playerEntity.directionAngle = (short)((g_playerEntity.directionAngle + 12) & 0xFFF);
    Joint_move(0, g_playerEntity.jointMoveData0, g_playerEntity.jointMoveData1, 0x400);
    EntityComputeJointWorldMatrices(g_playerEntity.unk_ca);
    EntityApplyLookAtRotation();
    options_render_entity((int)&g_playerEntity);
}

// Word-wrapped text from (x, y), `width` characters a line; returns the y
// under the last line.
static int cs_print_wrapped(int x, int y, unsigned char color, const char* text, int width)
{
    char line[48];
    const char* p = text;
    while (*p != '\0') {
        int len = (int)strlen(p);
        int take = len <= width ? len : width;
        if (len > width) {
            int cut = take;
            while (cut > 0 && p[cut] != ' ') cut--;
            if (cut > 0) take = cut;
        }
        memcpy(line, p, (size_t)take);
        line[take] = '\0';
        lobby_print(x, y, color, line);
        y += 11;
        p += take;
        while (*p == ' ') p++;
    }
    return y;
}

static void cs_draw(int sel, int mine)
{
    lobby_print(8, 6, 0x8F, "CHOOSE YOUR SURVIVOR");
    // Two rows of three; the last column may overlap the preview at the top.
    for (int c = 0; c < ZM_CHAR_COUNT; c++) {
        bool other = zm_net_char_taken(c) && c != mine;
        char word[24];
        snprintf(word, sizeof(word), "%s%s%s", c == sel ? ">" : " ", zm_char_name(c), c == mine ? " +" : "");
        lobby_print(8 + (c % 3) * 96, 24 + (c / 3) * 12, c == sel ? 0x8F : (other ? 0x2F : 0x7F), word);
    }

    ZmPerkInfo info;
    zm_perk_describe(sel, &info);
    char line[64];
    int y = 54;
    lobby_print(8, y, 0x8F, info.fullName);
    y += 11;
    snprintf(line, sizeof(line), "TOUGHNESS %d - %d HP", info.toughness, info.health);
    lobby_print(8, y, 0x7F, line);
    y += 11;
    y = cs_print_wrapped(8, y, 0x7F, info.description, 33) + 2;
    lobby_print(8, y, 0x8F, "ITEMS");
    y += 11;
    for (int i = 0; i < info.itemCount; i++) {
        if (info.itemCounts[i] > 1) {
            snprintf(line, sizeof(line), " %s X%d", info.items[i], info.itemCounts[i]);
        } else {
            snprintf(line, sizeof(line), " %s", info.items[i]);
        }
        lobby_print(8, y, 0x7F, line);
        y += 11;
    }
    y += 2;
    cs_print_wrapped(8, y, 0x8F, info.perk, 33);

    bool other = zm_net_char_taken(sel) && sel != mine;
    lobby_print(8, 228, 0x7F, other ? "TAKEN BY ANOTHER SURVIVOR  CANCEL: BACK" : "ACTION: PICK  CANCEL: BACK");
}


// Read-only title profiles: no network picks and no monster AI updates.
// Reload into one private arena, so paging never consumes room memory or
// advances the room/mod texture allocators.
extern void Object_DeleteAll(int);
extern void LoadEntityEMD(Entity*, unsigned char);
extern void Entity_SetJoints(Entity*, unsigned int);
extern void InitAnimStructure(void*);
extern unsigned int SetupJointStructures(unsigned int);
extern void ResetJointTransforms(void);
static unsigned int s_guideModelArena[256 * 1024];
static Entity s_guideMonster;
static int s_guideProfile = -1;
static unsigned int s_guideScreenMode;
static int s_guideRenderParam;
struct MonsterProfile {
    unsigned char id, idle;
    const char* name;
    const char* description;
    const char* abilities;
};
// Attack values follow Zombie.cpp, Cerberus.cpp, Hunter.cpp, Chimera.cpp
// and tyrant_damage_player; paired values are normal / second playthrough.
static const MonsterProfile kGuideMonsters[] = {
    {0x00, 0, "WHITE COAT ZOMBIE", "A CLOSE RANGE AMBUSHER. PLAY DEAD TO CATCH SURVIVORS OFF GUARD.",
     "ACTION: GRAB AND BITE. 10 / 12 HP EVERY 19 FRAMES. AIM: LIE DOWN OR RISE. FLOOR BITE: 6 / 9 HP. RUN: FASTER SHAMBLE. OTHER MONSTERS: SWIPE 30 HP; ZOMBIE BITE 15 HP PER TICK."},
    {0x11, 0, "GREEN ZOMBIE", "A SHAMBLING ATTACKER WITH A RANGED VOMIT ATTACK.",
     "ACTION: BITE, 10 / 12 HP EVERY 19 FRAMES. AIM: VOMIT. VOMIT: 5 / 15 HP PER HIT. RUN: FASTER SHAMBLE. OTHER MONSTERS: SWIPE 30 HP; ZOMBIE BITE 15 HP PER TICK."},
    {0x01, 0, "NAKED ZOMBIE", "A TOUGHER ZOMBIE FOR SUSTAINED PRESSURE. HAS 25 PERCENT EXTRA HEALTH.",
     "ACTION: BITE, 10 / 12 HP EVERY 19 FRAMES. AIM: VOMIT, 5 / 15 HP PER HIT. RUN: FASTER SHAMBLE. OTHER MONSTERS: SWIPE 30 HP; ZOMBIE BITE 15 HP PER TICK."},
    {0x02, 0, "CERBERUS", "A FAST PURSUER. CLOSE THE GAP WITH A LEAP OR SNAP AT NEARBY TARGETS.",
     "ACTION: RUNNING LEAP. AIM AND ACTION: CLOSE BITE. BOTH DEAL 12 HP PER HIT. RUN: FAST CHASE."},
    {0x06, 0x15, "HUNTER", "AN AGILE CLAW FIGHTER. LEAP INTO RANGE, THEN KEEP PRESSURE WITH SWIPES.",
     "ACTION: CLAW, 10 / 13 HP. AIM OR AIM AND ACTION: JUMPING CLAW, 15 / 20 HP. LEAP COOLDOWN: 5 SECONDS. RUN: FAST CHASE."},
    {0x09, 0, "CHIMERA", "A QUICK FLOOR FIGHTER WITH TWO CLAW ATTACKS. CONTROL BRINGS IT OFF THE CEILING.",
     "ACTION: DOUBLE CLAW, 20 HP. AIM AND ACTION: GRAB CLAW, 10 / 30 HP. CONTACT: 5 / 10 HP. IN PVP THE GRAB BECOMES A HIT. RUN: FAST CHASE."},
    {0x10, 0, "TYRANT", "A 600 HP HEAVY HITTER. USE ITS SPRINT TO CLOSE IN AND SWEEP THE ROOM.",
     "ACTION: SWIPE, 50 HP. AIM AND ACTION: SLASH, 40 HP. RUN AND FORWARD: SPRINT. ADD ACTION: WIDE SWEEP, 60 HP. A FATAL SLASH CAN CHAIN INTO AN IMPALE."}
};
void zm_guide_profile(int survivor, int monster)
{
    int profile = survivor >= 0 ? survivor : monster >= 0 ? ZM_CHAR_COUNT + monster : -1;
    if (profile != s_guideProfile) {
        if (s_guideProfile >= 0) {
            cs_scene_end();
            g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | s_guideScreenMode;
            set_scene_render_param(s_guideRenderParam);
        }
        s_guideProfile = profile;
        if (profile >= 0) {
            s_guideScreenMode = g_main_state_flags & MSF_SCREEN_MODE_MASK;
            s_guideRenderParam = g_sceneRenderParam;
            cs_scene_begin();
            if (survivor >= 0) cs_load_model(survivor);
            else {
                const MonsterProfile& info = kGuideMonsters[monster];
                Entity* savedEntity = ENTITY;
                void* savedDest = g_loadDataDestPointer;
                unsigned char savedBank = g_TextureBankID, savedRow = g_TextureCurrentPage;
                int savedQueue = DAT_00ae9f04;
                Object_DeleteAll(0);
                // As with the room extension loader, exclude these CLUT rows
                // from the original death-fade queue.
                DAT_00ae9f04 = 4;
                memset(&s_guideMonster, 0, sizeof(s_guideMonster));
                Entity* e = &s_guideMonster;
                e->id = info.id;
                e->has_enter_switch_zone = 1;
                e->texBank = g_TextureBankID = 0x20;
                e->attacking_direction = g_TextureCurrentPage = 0x40;
                ENTITY = e;
                g_loadDataDestPointer = s_guideModelArena;
                LoadEntityEMD(e, (unsigned char)(info.id + 4));
                Entity_SetJoints(e, 0x7c);
                InitAnimStructure((void*)e->modelLoadBuffer);
                SetupJointStructures((unsigned int)g_loadDataDestPointer);
                ResetJointTransforms();
                e->scaMatrixData.localMatrix = g_identityMatrixData;
                e->scaMatrixData.localMatrix.t[0] = -2400;
                e->scaMatrixData.localMatrix.t[1] = info.id == 0x02 ? 1600 : 2100;
                e->scaMatrixData.localMatrix.t[2] = info.id == 0x10 ? -2000 : -700;
                e->animationId = info.idle;
                e->angle = 0xce4;
                g_loadDataDestPointer = savedDest;
                g_TextureBankID = savedBank; g_TextureCurrentPage = savedRow;
                DAT_00ae9f04 = savedQueue;
                ENTITY = savedEntity;
            }
        }
    }
    if (profile < 0) return;
    Entity* savedEntity = ENTITY;
    if (survivor >= 0) {
        cs_draw_model();
        ZmPerkInfo info;
        zm_perk_describe(survivor, &info);
        lobby_print(8, 16, 0x8F, info.fullName);
        char line[64];
        snprintf(line, sizeof(line), "TOUGHNESS %d - %d HP - %d SLOTS", info.toughness,
                 info.health, survivor == ZM_CHAR_JILL ? 8 : 6);
        lobby_print(8, 35, 0x7F, line);
        int y = cs_print_wrapped(8, 49, 0x7F, info.description, 32) + 2;
        lobby_print(8, y, 0x8F, "STARTING KIT"); y += 11;
        for (int i = 0; i < info.itemCount; ++i) {
            if (info.itemCounts[i] > 1)
                snprintf(line, sizeof(line), "%s X%d", info.items[i], info.itemCounts[i]);
            else snprintf(line, sizeof(line), "%s", info.items[i]);
            lobby_print(8, y, 0x7F, line); y += 11;
        }
        cs_print_wrapped(8, y + 2, 0x8F, info.perk, 32);
    } else {
        const MonsterProfile& info = kGuideMonsters[monster];
        ENTITY = &s_guideMonster;
        s_guideMonster.angle = (short)((s_guideMonster.angle + 12) & 0xfff);
        Joint_move(0, s_guideMonster.animHeader, s_guideMonster.animBase, 0x400);
        EntityComputeJointWorldMatrices(0);
        options_render_entity((int)&s_guideMonster);
        lobby_print(8, 16, 0x8F, info.name);
        char buying[64];
        int unlock = zm_econ_unlock_bosses(info.id);
        if (unlock >= 2) snprintf(buying, sizeof(buying), "%d POINTS - AFTER BOTH YAWNS", zm_econ_cost(info.id));
        else if (unlock) snprintf(buying, sizeof(buying), "%d POINTS - AFTER THE FIRST YAWN", zm_econ_cost(info.id));
        else snprintf(buying, sizeof(buying), "%d POINTS - AVAILABLE AT START", zm_econ_cost(info.id));
        lobby_print(8, 32, 0x7F, buying);
        int y = cs_print_wrapped(8, 46, 0x7F, info.description, 32) + 3;
        lobby_print(8, y, 0x8F, "DIRECTOR ABILITIES");
        y = cs_print_wrapped(8, y + 12, 0x7F, info.abilities, 32);
        if (monster < 6) lobby_print(8, y + 3, 0x2F, "HP LOST: NORMAL / HARD");
    }
    ENTITY = savedEntity;
}

// Survivors ready in an AI director's lobby (the host's seat always is).
static void lobby_ready_count(int* seated, int* readied)
{
    *seated = *readied = 0;
    for (int i = 0; i < ZM_NET_MAX_PLAYERS; i++) {
        if (zm_net_char(i) < 0) continue;
        (*seated)++;
        if (i == 0 || zm_net_vote(i) == ZM_VOTE_ACCEPT) (*readied)++;
    }
}

// A survivor once the map is chosen: two minutes to pick a character, then
// into the game (the director is already playing).
#ifdef QUICK_DEBUG
#define ZM_PICK_MS 15000
#else
#define ZM_PICK_MS 120000
#endif

// Should a pregame screen give up? The session ended; with a deadline (a
// human director's game after GO) once it has passed; without one (an AI
// director's lobby, before GO) once the host started the game or took the
// lobby back from the map.
static bool lobby_screen_over(unsigned int deadlineMs)
{
    bool host = zm_net_role() == ZM_NET_ZOMBIE;
    if (zm_net_status() != (host ? ZM_NET_HOSTING : ZM_NET_CONNECTED)) return true;
    if (deadlineMs != 0) return (int)(deadlineMs - plat_time_ms()) <= 0;
    if (!host && zm_net_lobby_started()) return true;
    return zm_net_lobby_phase() != ZM_LOBBY_MAP;
}

// The countdown to the survivors' spawn, or (against the AI) who is ready.
static void lobby_timer_text(unsigned int deadlineMs, char* out, int len)
{
    if (deadlineMs != 0) {
        int ms = (int)(deadlineMs - plat_time_ms());
        unsigned int left = ms > 0 ? ((unsigned int)ms + 999) / 1000 : 0;
        snprintf(out, len, "ENTERING IN %u:%02u", left / 60, left % 60);
    } else {
        int seated, readied;
        lobby_ready_count(&seated, &readied);
        snprintf(out, len, "AI %s  READY %d OF %d", zm_ai_level_name(zm_net_ai_level()), readied, seated);
    }
}

// ---------------------------------------------------------------------------
// The character select screen
//
// Action picks the highlighted character (if nobody else wears it) and goes
// back; Cancel goes back keeping the current one. Every seat always holds a
// character (the host hands out a free one on joining), so backing out is
// always allowed.
// ---------------------------------------------------------------------------
static void lobby_character_select(unsigned int deadlineMs)
{
    int mine = zm_net_char(zm_net_self());
    int sel = mine >= 0 ? mine : 0;
    int loaded = -1;
    bool first = true;          // the press that opened the screen is not a pick
    char timer[40];
    cs_scene_begin();
    for (;;) {
        zm_net_poll();
        if (lobby_screen_over(deadlineMs)) break;
        mine = zm_net_char(zm_net_self());
        unsigned int pressed = first ? 0u : (unsigned int)g_PlayerPadPressed;
        first = false;
        int oldSel = sel;
        if (pressed & PAD_UP)    sel = (sel + 3) % ZM_CHAR_COUNT;
        if (pressed & PAD_DOWN)  sel = (sel + 3) % ZM_CHAR_COUNT;
        if (pressed & PAD_LEFT)  sel = (sel / 3) * 3 + (sel % 3 + 2) % 3;
        if (pressed & PAD_RIGHT) sel = (sel / 3) * 3 + (sel % 3 + 1) % 3;
        if (sel != oldSel) play_sfx(SFX_UI_BANK, SFX_UI_CURSOR);
        if (pressed & PAD_CANCEL) {
            play_sfx(SFX_UI_BANK, SFX_UI_CANCEL);
            break;
        }
        bool other = zm_net_char_taken(sel) && sel != mine;
        if (pressed & PAD_CONFIRM) {
            if (other) {
                play_sfx(SFX_UI_BANK, SFX_UI_CANCEL);
            } else {
                play_sfx(SFX_UI_BANK, SFX_UI_DECIDE);
                zm_net_set_char(sel);
                break;
            }
        }
        if (sel != loaded) {
            cs_load_model(sel);
            loaded = sel;
        }
        cs_draw(sel, mine);
        lobby_timer_text(deadlineMs, timer, sizeof(timer));
        lobby_print(320 - 8 - (int)strlen(timer) * 6, 6, 0x8F, timer);
        cs_draw_model();
        Task_sleep(1);
    }
    cs_scene_end();
}

// The route map to look at from the pregame lobby; Action or Cancel goes back.
static void lobby_view_map(unsigned int deadlineMs)
{
    zm_map_set_setup(true);
    if (!zm_map_is_open()) zm_map_toggle();
    unsigned int shownSeed = 0;
    bool first = true;
    char timer[40];
    for (;;) {
        zm_net_poll();
        if (lobby_screen_over(deadlineMs)) break;
        unsigned int pressed = first ? 0u : (unsigned int)g_PlayerPadPressed;
        first = false;
        if (pressed & (PAD_CONFIRM | PAD_CANCEL | PAD_OPTIONS)) {
            play_sfx(SFX_UI_BANK, SFX_UI_CANCEL);
            break;
        }
        unsigned int seed = zm_net_seed();
        if (seed != shownSeed) {
            zm_random_build(seed);
            shownSeed = seed;
        }
        lobby_timer_text(deadlineMs, timer, sizeof(timer));
        zm_map_set_route(ZM_ROUTE_KEYS, false, false, timer, "ARROWS: ROOMS  ACTION: BACK");
        zm_map_input(pressed & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT));
        zm_map_draw();
        Task_sleep(1);
    }
    if (zm_map_is_open()) zm_map_toggle();
    zm_map_set_read_only(false);
    zm_map_set_setup(false);
}

// ---------------------------------------------------------------------------
// The pregame lobby: after a survivor has picked its character. Lists the
// survivors and their characters, with CHANGE CHARACTER and VIEW MAP.
//
// A human director's game (`deadlineMs`, after GO): the countdown to the
// survivors' spawn; at the deadline everyone goes in with what they hold.
// An AI director's game (no deadline, before GO): each survivor marks
// itself READY / NOT READY and the host starts - with someone not ready,
// START asks once more first. Returns true when the game starts (the host
// sent GO already when it is the one starting), false when the lobby went
// back to the join screen or the session ended.
// ---------------------------------------------------------------------------
static bool lobby_pregame(unsigned int deadlineMs)
{
    bool host = zm_net_role() == ZM_NET_ZOMBIE;
    int sel = 0;
    bool first = true;
    bool confirmStart = false;          // START pressed with someone not ready
    char timer[40];
    for (;;) {
        zm_net_poll();
        if (zm_net_status() != (host ? ZM_NET_HOSTING : ZM_NET_CONNECTED)) return false;
        if (deadlineMs != 0 && (int)(deadlineMs - plat_time_ms()) <= 0) return true;
        if (deadlineMs == 0 && !host && zm_net_lobby_started()) return true;
        if (deadlineMs == 0 && zm_net_lobby_phase() != ZM_LOBBY_MAP) return false;
        unsigned int pressed = first ? 0u : (unsigned int)g_PlayerPadPressed;
        first = false;

        int seated, readied;
        lobby_ready_count(&seated, &readied);
        bool ready = !host && zm_net_vote(zm_net_self()) == ZM_VOTE_ACCEPT;
        if (readied >= seated) confirmStart = false;

        LobbyMenu m;
        if (deadlineMs == 0 && host)
            m.add(MI_START, true, confirmStart ? "START ANYWAY - NOT ALL READY" : "START THE GAME");
        if (deadlineMs == 0 && !host) m.add(MI_READY, true, ready ? "READY - SELECT TO UNREADY" : "NOT READY - SELECT TO READY");
        m.add(MI_CHANGE_CHAR, true, "CHANGE CHARACTER");
        m.add(MI_VIEW_MAP, true, "VIEW THE MAP");
        if (deadlineMs == 0) m.add(host ? MI_BACK_LOBBY : MI_LEAVE, true, host ? "BACK TO THE LOBBY" : "LEAVE");

        int pick = lobby_menu_input(m, &sel, pressed);
        if (pick != MI_START) {
            if (pick >= 0) confirmStart = false;
        }
        switch (pick) {
        case MI_START:
            if (readied < seated && !confirmStart) {
                confirmStart = true;
                break;
            }
            zm_random_build(zm_net_seed());
            zm_net_lobby_go();
            return true;
        case MI_READY:
            zm_net_set_vote(ready ? ZM_VOTE_NONE : ZM_VOTE_ACCEPT);
            break;
        case MI_CHANGE_CHAR:
            lobby_character_select(deadlineMs);
            first = true;
            break;
        case MI_VIEW_MAP:
            lobby_view_map(deadlineMs);
            first = true;
            break;
        case MI_LEAVE:
            zm_net_stop();
            return false;
        case MI_BACK_LOBBY:
            zm_net_set_phase(ZM_LOBBY_JOIN);
            return false;
        }

        zm_guide_background(true);
        int x = 40, y = 20;
        lobby_print(x, y, 0x8F, ZM_GAME_TITLE);
        lobby_timer_text(deadlineMs, timer, sizeof(timer));
        lobby_print(320 - 24 - (int)strlen(timer) * 6, y, 0x8F, timer);
        y += 22;
        lobby_print(x, y, 0x8F, deadlineMs != 0 ? "GET READY - THE DIRECTOR IS SETTING UP" : "LOBBY - THE AI IS THE DIRECTOR");
        lobby_draw_players(x, y + 18);
        lobby_menu_draw(m, sel, x, 150);
        Task_sleep(1);
    }
}

// ---------------------------------------------------------------------------
// The map review (after the host advances the lobby)
//
// Every copy builds this game's scenario (ZombieRandom.cpp) from the seed the
// host hands out and shows it on the director's map, look-only: the rooms
// with key-locked doors and, for the picked room, which key opens each door
// and where it leads. The director's map also shows where the keys, crests
// and new pickups lie. The arrows browse the rooms; Action opens the menu.
//
// A human director's game: each side may veto one map. The director's veto
// is its own; the survivors' needs every survivor's vote. A veto draws a new
// map (a new seed) and clears the votes. The director starts once every
// survivor has accepted - or after ZM_REVIEW_MAX_MS, so a quiet survivor
// cannot hold the game. An AI director's game has no votes: everyone looks
// and continues to the character select on their own.
// ---------------------------------------------------------------------------
#define ZM_REVIEW_MAX_MS 90000

enum { REVIEW_ENDED, REVIEW_GO, REVIEW_CONTINUE };

static int lobby_map_review(void)
{
    zm_map_set_setup(true);
    bool host = zm_net_role() == ZM_NET_ZOMBIE;
    bool ai = zm_net_ai_level() > 0;
    if (!zm_map_is_open()) zm_map_toggle();
    bool first = true;              // the press that opened it
    bool menu = false;
    int sel = 0;
    unsigned int shownSeed = 0, shownMs = plat_time_ms();
    int result = REVIEW_ENDED;
    char status[64];
    for (;;) {
        zm_net_poll();
        int st = zm_net_status();
        if (st != ZM_NET_CONNECTED && st != ZM_NET_HOSTING) break;
        unsigned int seed = zm_net_seed();
        if (seed != shownSeed) {
            zm_random_build(seed);
            shownSeed = seed;
            shownMs = plat_time_ms();
            menu = false;
        }
        if (!host && zm_net_lobby_started()) { result = REVIEW_GO; break; }
        if (zm_net_lobby_phase() != ZM_LOBBY_MAP) break;
        unsigned int pressed = first ? 0u : (unsigned int)g_PlayerPadPressed;
        first = false;

        // Survivors seated, and how they voted.
        int seated = 0, accepted = 0, vetoes = 0;
        for (int i = 1; i < ZM_NET_MAX_PLAYERS; i++) {
            if (zm_net_char(i) < 0) continue;
            seated++;
            int v = zm_net_vote(i);
            if (v == ZM_VOTE_ACCEPT) accepted++;
            if (v == ZM_VOTE_VETO) vetoes++;
        }
        int used = zm_net_veto_used();
        bool timedOut = plat_time_ms() - shownMs > ZM_REVIEW_MAX_MS;
        if (host && !ai && seated > 0 && vetoes == seated && (used & ZM_VETO_SURVIVORS) == 0) {
            zm_net_reroll(ZM_VETO_SURVIVORS);
        }

        LobbyMenu m;
        if (ai) {
            m.add(MI_CONTINUE, true, "CHOOSE YOUR CHARACTER");
            m.add(host ? MI_BACK_LOBBY : MI_LEAVE, true, host ? "BACK TO THE LOBBY" : "LEAVE");
            snprintf(status, sizeof(status), "THE MAP - AI DIRECTOR: %s", zm_ai_level_name(zm_net_ai_level()));
        } else if (host) {
            m.add(MI_START, seated > 0 && (accepted == seated || timedOut),
                  (accepted == seated || timedOut) ? "START THE GAME" : "START - WAITING FOR OK");
            m.add(MI_VETO, (used & ZM_VETO_DIRECTOR) == 0, (used & ZM_VETO_DIRECTOR) ? "VETO USED" : "VETO THIS MAP");
            m.add(MI_BACK_LOBBY, true, "BACK TO THE LOBBY");
            snprintf(status, sizeof(status), "SURVIVORS %d OK %d VETO OF %d  YOUR VETO %s",
                     accepted, vetoes, seated, (used & ZM_VETO_DIRECTOR) ? "USED" : "LEFT");
        } else {
            int mine = zm_net_vote(zm_net_self());
            bool canVeto = (used & ZM_VETO_SURVIVORS) == 0;
            m.add(MI_ACCEPT, mine != ZM_VOTE_ACCEPT, mine == ZM_VOTE_ACCEPT ? "MAP ACCEPTED" : "ACCEPT THIS MAP");
            m.add(MI_VETO, canVeto && mine != ZM_VOTE_VETO,
                  !canVeto ? "VETO USED" : mine == ZM_VOTE_VETO ? "VETO VOTED - ALL MUST" : "VOTE TO VETO - ALL MUST");
            m.add(MI_LEAVE, true, "LEAVE");
            snprintf(status, sizeof(status), "YOU: %s  TEAM: %d OK %d VETO OF %d",
                     mine == ZM_VOTE_ACCEPT ? "OK" : mine == ZM_VOTE_VETO ? "VETO" : "-",
                     accepted, vetoes, seated);
        }
        m.add(MI_CLOSE, true, "CLOSE THE MENU");

        int pick = -1;
        if (menu) {
            if (pressed & PAD_CANCEL) {
                play_sfx(SFX_UI_BANK, SFX_UI_CANCEL);
                menu = false;
            } else {
                pick = lobby_menu_input(m, &sel, pressed);
            }
            pressed = 0;                        // the arrows are the menu's
        } else if (pressed & PAD_CONFIRM) {
            play_sfx(SFX_UI_BANK, SFX_UI_DECIDE);
            menu = true;
            sel = 0;
            pressed = 0;
        }
        bool done = false;
        switch (pick) {
        case MI_CONTINUE:   result = REVIEW_CONTINUE; done = true; break;
        case MI_START:      zm_net_lobby_go(); result = REVIEW_GO; done = true; break;
        case MI_VETO:
            if (host) zm_net_reroll(ZM_VETO_DIRECTOR);
            else zm_net_set_vote(ZM_VOTE_VETO);
            menu = false;
            break;
        case MI_ACCEPT:     zm_net_set_vote(ZM_VOTE_ACCEPT); menu = false; break;
        case MI_BACK_LOBBY: zm_net_set_phase(ZM_LOBBY_JOIN); done = true; break;
        case MI_LEAVE:      zm_net_stop(); done = true; break;
        case MI_CLOSE:      menu = false; break;
        }
        if (done) break;

        zm_map_set_route(host && !ai ? ZM_ROUTE_ALL : ZM_ROUTE_KEYS, false, false, status,
                         menu ? "UP-DOWN: CHOOSE  ACTION: SELECT  CANCEL: CLOSE" : "ARROWS: ROOMS  ACTION: MENU");
        if (menu) zm_map_set_menu(m.ptr, m.on, m.count, sel);
        zm_map_input(pressed & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT));
        zm_map_draw();
        Task_sleep(1);
    }
    if (zm_map_is_open()) zm_map_toggle();
    zm_map_set_read_only(false);
    zm_map_set_setup(false);
    return result;
}

// ---------------------------------------------------------------------------
// The join screen: who is in, and the host's settings. The host advances to
// the map (a human director once someone has joined; against the AI at any
// time - it may play alone). A survivor waits here until the map is up.
// ---------------------------------------------------------------------------

// The AI director's level for HOST - AI DIRECTOR: config's AiDirector, else normal.
static int s_aiLevel = 0;

static void lobby_room(void)
{
    int sel = 0;
    bool first = true;
    for (;;) {
        zm_net_poll();
        int status = zm_net_status();
        int role = zm_net_role();
        bool host = role == ZM_NET_ZOMBIE && status == ZM_NET_HOSTING;
        bool joined = role == ZM_NET_SURVIVOR && status == ZM_NET_CONNECTED;
        bool joining = role == ZM_NET_SURVIVOR && status == ZM_NET_JOINING;
        if (!host && !joined && !joining) return;
        if (joined && (zm_net_lobby_started() || zm_net_lobby_phase() != ZM_LOBBY_JOIN)) return;
        unsigned int pressed = first ? 0u : (unsigned int)g_PlayerPadPressed;
        first = false;

        int ai = zm_net_ai_level();
        LobbyMenu m;
        if (host && ai > 0) {
            m.add(MI_TO_MAP, true, "CONTINUE TO THE MAP");
            m.add(MI_AI_LEVEL, true, "AI DIRECTOR: %s", zm_ai_level_name(ai));
        } else if (host) {
            m.add(MI_GENERATE, zm_net_survivor_count() > 0,
                  zm_net_survivor_count() > 0 ? "GENERATE THE MAP" : "GENERATE THE MAP - NOBODY YET");
        }
        if (host) m.add(MI_TIMEOUT, true, "SURVIVOR TIMEOUT: %s", zm_net_timeout_enabled() ? "ON" : "OFF");
        m.add(MI_LEAVE, true, "LEAVE");

        // The AI level steps with left / right as well as Action.
        if (host && ai > 0 && sel == m.indexOf(MI_AI_LEVEL) && (pressed & (PAD_LEFT | PAD_RIGHT))) {
            play_sfx(SFX_UI_BANK, SFX_UI_CURSOR);
            s_aiLevel = ai + ((pressed & PAD_RIGHT) ? 1 : -1);
            if (s_aiLevel < 1) s_aiLevel = 5;
            if (s_aiLevel > 5) s_aiLevel = 1;
            zm_net_set_ai_level(s_aiLevel);
        }
        int pick = lobby_menu_input(m, &sel, pressed);
        if (pick < 0 && (pressed & PAD_CANCEL)) {
            play_sfx(SFX_UI_BANK, SFX_UI_CANCEL);
            pick = MI_LEAVE;
        }
        switch (pick) {
        case MI_GENERATE:
        case MI_TO_MAP:
            zm_net_set_phase(ZM_LOBBY_MAP);
            return;
        case MI_AI_LEVEL:
            s_aiLevel = ai >= 5 ? 1 : ai + 1;
            zm_net_set_ai_level(s_aiLevel);
            break;
        case MI_TIMEOUT:
            zm_net_set_timeout_enabled(!zm_net_timeout_enabled());
            break;
        case MI_LEAVE:
            zm_net_stop();
            return;
        }

        zm_guide_background(true);
        int x = 40, y = 20;
        lobby_print(x, y, 0x8F, ZM_GAME_TITLE);
        y += 24;
        lobby_print(x, y, 0x8F, !host ? "LOBBY"
                    : ai > 0 ? "LOBBY - THE AI IS THE DIRECTOR" : "LOBBY - YOU ARE THE DIRECTOR");
        y += 18;
        if (joining) {
            lobby_print(x, y, 0x7F, zm_net_status_text());
            lobby_print(x, y + 16, 0x7F, "WAITING FOR THE HOST...");
        } else {
            lobby_draw_players(x, y);
        }
        if (joined) {
            char option[48];
            snprintf(option, sizeof(option), "SURVIVOR TIMEOUT: %s", zm_net_timeout_enabled() ? "ON" : "OFF");
            lobby_print(x, 136, 0x7F, option);
            lobby_print(x, 200, 0x7F, "WAITING FOR THE HOST TO SHOW THE MAP");
        }
        lobby_menu_draw(m, sel, x, host ? 136 : 160);
        Task_sleep(1);
    }
}

// A survivor once the host said go: a rejoin restores its survivor; a human
// director's game gives two minutes to pick a character in the pregame
// lobby (the director is already playing); an AI director's game was picked
// before GO.
static void lobby_survivor_go(void)
{
    if (zm_net_rejoining()) {
        while (!zm_net_rejoin_downloaded() && zm_net_status() == ZM_NET_CONNECTED) {
            zm_net_poll();
            zm_guide_background(true);
            lobby_print(40, 88, 0x8F, "RESTORING YOUR SURVIVOR");
            lobby_print(40, 110, 0x7F, "WAITING FOR THE HOST CHECKPOINT");
            Task_sleep(1);
        }
        if (!zm_net_rejoin_downloaded()) return;
        lobby_start_game();
        return;
    }
    if (zm_net_ai_level() == 0) {
        unsigned int deadline = plat_time_ms() + ZM_PICK_MS;
        lobby_character_select(deadline);
        if (!lobby_pregame(deadline)) return;
    }
    lobby_start_game();
}

// The session, from hosting or joining until the game starts or it ends.
// Returns when it ended (back to the NEW GAME menu); does not return once
// the game starts.
static void lobby_session(void)
{
    for (;;) {
        zm_net_poll();
        int status = zm_net_status();
        int role = zm_net_role();
        bool hosting = role == ZM_NET_ZOMBIE && status == ZM_NET_HOSTING;
        bool joined = role == ZM_NET_SURVIVOR && status == ZM_NET_CONNECTED;
        bool joining = role == ZM_NET_SURVIVOR && status == ZM_NET_JOINING;
        if (!hosting && !joined && !joining) return;
        if (joined && zm_net_lobby_started()) {
            lobby_survivor_go();
        } else if (joining || zm_net_lobby_phase() == ZM_LOBBY_JOIN) {
            lobby_room();
        } else {
            int review = lobby_map_review();
            if (review == REVIEW_GO && hosting) {
                lobby_start_game();             // the director plays at once
                return;
            }
            if (review == REVIEW_CONTINUE) {
                // Against the AI: the character, then the ready-up lobby.
                lobby_character_select(0);
                if (lobby_pregame(0)) {
                    lobby_start_game();
                    return;
                }
            }
        }
        // The press that closed a screen must not reach the next one.
        Task_sleep(1);
    }
}

void zombie_lobby_state(void)
{
    int row = ROW_SINGLE;
    if (s_aiLevel == 0) s_aiLevel = (g_zmAiDirector >= 1 && g_zmAiDirector <= 5) ? g_zmAiDirector : 2;
    bool editing = false;
    int cursor = 0;
    lobby_load_fields();
    zm_net_stop();
    zm_guide_background(true);

    for (;;) {
        zm_net_poll();
        unsigned int pressed = (unsigned int)g_PlayerPadPressed;
        int status = zm_net_status();
        if (status == ZM_NET_HOSTING || status == ZM_NET_JOINING || status == ZM_NET_CONNECTED) {
            lobby_session();
            Task_sleep(1);
            continue;
        }

        // ---- input ----
        if (editing) {
            if (row == ROW_ADDRESS) lobby_edit(s_addr, 15, &cursor, pressed);
            if (row == ROW_PORT)    lobby_edit(s_port, 5, &cursor, pressed);
            if (pressed & (PAD_CONFIRM | PAD_CANCEL)) {
                play_sfx(SFX_UI_BANK, pressed & PAD_CONFIRM ? SFX_UI_DECIDE : SFX_UI_CANCEL);
                editing = false;
                lobby_store_fields();
            }
        } else {
            int oldRow = row;
            if (pressed & PAD_UP)   row = (row + ROW_COUNT - 1) % ROW_COUNT;
            if (pressed & PAD_DOWN) row = (row + 1) % ROW_COUNT;
            if (row != oldRow) play_sfx(SFX_UI_BANK, SFX_UI_CURSOR);
            if (row == ROW_HOST_AI && (pressed & (PAD_LEFT | PAD_RIGHT))) {
                play_sfx(SFX_UI_BANK, SFX_UI_CURSOR);
                s_aiLevel += (pressed & PAD_RIGHT) ? 1 : -1;
                if (s_aiLevel < 1) s_aiLevel = 5;
                if (s_aiLevel > 5) s_aiLevel = 1;
            }
            if (pressed & PAD_CANCEL) {
                play_sfx(SFX_UI_BANK, SFX_UI_CANCEL);
                zm_guide_background(false);
                Task_sleep(8);
                Task_chain((void*)title_state);
                return;
            }
            if (pressed & PAD_CONFIRM) {
                play_sfx(SFX_UI_BANK, SFX_UI_DECIDE);
                switch (row) {
                case ROW_SINGLE:
                    zm_net_stop();
                    zm_guide_background(false);
                    Task_sleep(8);
                    Task_chain((void*)characterSelectionScreen);
                    return;
                case ROW_HOST:
                    lobby_store_fields();
                    zm_net_host();
                    break;
                case ROW_HOST_AI:
                    lobby_store_fields();
                    zm_net_host();
                    zm_net_set_ai_level(s_aiLevel);
                    break;
                case ROW_JOIN:
                    lobby_store_fields();
                    zm_net_join(g_zmNetAddress, g_zmNetPort, -1);   // the character comes after the map
                    break;
                case ROW_REJOIN:
                    zm_net_rejoin_saved();
                    break;
                case ROW_ADDRESS:
                case ROW_PORT:
                    editing = true;
                    cursor = 0;
                    break;
                case ROW_BACK:
                    zm_guide_background(false);
                    Task_sleep(8);
                    Task_chain((void*)title_state);
                    return;
                }
            }
        }

        // The CRT background persists across the lobby and map review.
        zm_guide_background(true);
        int x = 40, y = 20;
        lobby_print(x, y, 0x8F, ZM_GAME_TITLE);
        y += 24;

        static const char* const labels[ROW_COUNT] = {
            "SINGLE PLAYER - AI SURVIVOR",
            "HOST - PLAY THE DIRECTOR",
            "",               // host vs the AI director, with its level
            "JOIN - PLAY A SURVIVOR",
            "REJOIN THE LAST MATCH",
            "",               // address
            "",               // port
            "BACK",
        };
        for (int r = 0; r < ROW_COUNT; r++) {
            char line[80];
            const char* mark = (r == row) ? "> " : "  ";
            int yy = y + r * 15;
            if (r == ROW_ADDRESS) {
                lobby_print(x, yy, 0x7F, mark);
                lobby_print_field(x + 12, yy, "  HOST ADDRESS ", s_addr, cursor,
                                  editing && row == ROW_ADDRESS);
            } else if (r == ROW_HOST_AI) {
                snprintf(line, sizeof(line), "%sHOST - AI DIRECTOR  < %s >", mark, zm_ai_level_name(s_aiLevel));
                lobby_print(x, yy, r == row ? 0x8F : 0x7F, line);
            } else if (r == ROW_PORT) {
                lobby_print(x, yy, 0x7F, mark);
                lobby_print_field(x + 12, yy, "  PORT ", s_port, cursor,
                                  editing && row == ROW_PORT);
            } else {
                snprintf(line, sizeof(line), "%s%s", mark, labels[r]);
                lobby_print(x, yy, r == row ? 0x8F : 0x7F, line);
            }
        }

        y += ROW_COUNT * 15 + 12;
        const char* st = zm_net_status_text();
        if (st[0] != '\0') lobby_print(x, y, 0x8F, st);
        if (editing) lobby_print(x, y + 16, 0x7F, "ARROWS EDIT  ENTER DONE");
        else {
            const char* hint = zm_net_status_hint();
            if (hint[0] != '\0') lobby_print(x, y + 16, 0x7F, hint);
        }

        Task_sleep(1);
    }
}
