// TitleScreen.cpp - Title screen rendering and state management
// All functions decompiled from Ghidra with original addresses
#include "../Globals.h"
#include "../marni/MarniSystem.h"
#include "../marni/PSXTexture.h"
#include "FileLoader.h"
#include "SpriteRenderer.h"
#include "InfestedTitleImage.h"
#include "GuideTitleImage.h"
#include "SFXIds.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>          // memcpy, for the DC title sheet assembly
#include "../system/AssetPath.h"
#include "../system/AudioFile.h"
#include "../platform/platform.h"
#include "../marni/MarniSound.h"
#include "mods/ZombieMode.h"
#include "mods/ZombieModeInternal.h"
#include "mods/ZombieNet.h"

extern void logos_state(void);
extern void zombie_lobby_state(void);   // mods/ZombieLobby.cpp (port-added)

// Preload the suffix so NEW GAME never waits for file I/O or audio completion
// before starting the original flash/fade sequence.
static int s_infestedBank = 0;
static int s_infestedVoicePhase = 0;
static unsigned int s_infestedStartMs = 0;
static unsigned int s_infestedDelayMs = 0;

static void title_load_infested(void)
{
    if (s_infestedBank) destroySndBank(s_infestedBank);
    s_infestedBank = 0;
    s_infestedVoicePhase = 0;
    s_infestedDelayMs = 0;
    char titlePath[1200], rooted[1200];
    const char* name = g_SoundBanksTable[GetAssetVersion() != 0 ? BANK_BIO : BANK_TITLE][0];
    snprintf(titlePath, sizeof(titlePath), GAME_DATA_ROOT "sound/%s.wav", name);
    AudioFileData titleAudio = {};
    if (AudioFile_Load(ResolveAssetRoot(titlePath, rooted, sizeof(rooted)), &titleAudio)) {
        unsigned int frameBytes = titleAudio.channels * (titleAudio.bitsPerSample / 8);
        if (frameBytes && titleAudio.sampleRate > 0) {
            unsigned int frames = titleAudio.pcmSize / frameBytes;
            // Split the calculation to keep it within 32-bit arithmetic.
            unsigned int durationMs = (frames / titleAudio.sampleRate) * 1000u +
                (frames % titleAudio.sampleRate) * 1000u / titleAudio.sampleRate;
            s_infestedDelayMs = durationMs > 1000u ? durationMs - 1000u : 0;
        }
        free(titleAudio.buffer);
    }
    char exeDir[1024], path[1200];
    if (!plat_exe_dir(exeDir, sizeof(exeDir))) return;
    snprintf(path, sizeof(path), "%s/infested/infested.wav", exeDir);
    s_infestedBank = loadSndBankFromWav(path);
    if (!s_infestedBank) return;
    set_volume(s_infestedBank, g_SfxVolume);
    pan_set(s_infestedBank, 0);
}

static void title_infested_voice_frame(void)
{
    if (s_infestedVoicePhase == 1 &&
        (plat_time_ms() - s_infestedStartMs >= s_infestedDelayMs ||
         !getSndStat(g_SfxBanks[SFX_TITLE_EVIL01 * 2]))) {
        playSnd(s_infestedBank, 0);
        s_infestedVoicePhase = 2;
    } else if (s_infestedVoicePhase == 2 && !getSndStat(s_infestedBank)) {
        s_infestedVoicePhase = 0;
    }
}


// Port-added PvP guide. Kept with the title so both platform builds share it.
extern void zm_text_encode(char* s);
static bool s_guideOpen = false;
static int s_guideSection = 0;
static int s_guidePage = -1;
// Eight spacious rows: blank rows separate short, single-topic paragraphs.
struct GuidePage { const char* heading; const char* lines[8]; };
static const GuidePage kGeneral[] = {
    { "WELCOME TO THE MANSION", {
        "BRING A FRIEND, OR A WHOLE TEAM. ONE DIRECTOR",
        "FACES 1-3 SURVIVORS IN THE RETURN MANSION.",
        "",
        "SURVIVORS EXPLORE TOGETHER. THE DIRECTOR FILLS",
        "THEIR PATH WITH MONSTERS." } },
    { "A FAMILIAR PLACE, A NEW ROUTE", {
        "KEYS, CRESTS AND SUPPLIES MOVE AROUND EACH MATCH.",
        "KNOWING THE MANSION HELPS, BUT THERE IS ALWAYS",
        "MORE TO DISCOVER.",
        "",
        "STORY SCENES ARE SKIPPED. EVERY MONSTER YOU MEET",
        "WAS BOUGHT BY THE DIRECTOR." } },
    { "WHAT YOU ARE WORKING TOWARD", {
        "SURVIVORS: GATHER THE FOUR CRESTS AND OPEN THE",
        "STOREROOM EXIT TO THE COURTYARD. ONE ESCAPE WINS",
        "FOR YOUR WHOLE TEAM.",
        "",
        "DIRECTOR: STOP THEM UNTIL THE 20 MINUTE CLOCK",
        "RUNS OUT, OR KILL EVERY SURVIVOR." } },
    { "GETTING EVERYONE TOGETHER", {
        "THE DIRECTOR CHOOSES NEW GAME, THEN HOST.",
        "SURVIVORS CHOOSE JOIN AND ENTER THE HOST ADDRESS.",
        "",
        "ONCE EVERYONE IS CONNECTED, THE HOST PRESSES",
        "ENTER TO REVIEW THE MAP." } },
    { "RETURNING AFTER CONNECTION LOSS", {
        "A LOST CONNECTION PAUSES THE MATCH FOR UP TO",
        "30 SECONDS. KEEP THE GAME OPEN TO RECONNECT.",
        "",
        "AFTER A CRASH, CHOOSE NEW GAME AND REJOIN YOUR",
        "SURVIVOR NUMBER ON THE SAME COMPUTER. RETURN",
        "BEFORE THE COUNTDOWN ENDS. IF THE HOST CLOSES",
        "ITS GAME, THE MATCH CANNOT BE RECOVERED." } },
    { "TAKE A LOOK AT THE MAP", {
        "TAKE A MOMENT TO PLAN YOUR ROUTE. ACTION ACCEPTS",
        "THE MAP. AIM VOTES TO VETO IT AND DRAW A NEW ONE.",
        "",
        "EACH TEAM GETS ONE VETO. SURVIVORS MUST ALL",
        "AGREE. THE HOST PRESSES ENTER TO START AFTER",
        "ALL ACCEPT, OR CAN PROCEED AFTER 90 SECONDS." } },
    { "BEFORE THE CLOCK STARTS", {
        "THE DIRECTOR GETS TWO MINUTES TO PREPARE.",
        "SURVIVORS CHOOSE DIFFERENT CHARACTERS AND CAN",
        "CHANGE THEIR PICKS UNTIL THEY SPAWN.",
        "",
        "EVERYONE ARRIVES IN THE MAIN HALL. THE CLOCK",
        "STARTS ONCE ALL ARE IN; THE HALL STAYS PROTECTED",
        "FOR 60 SECONDS." } },
};
static const GuidePage kDirector[] = {
    { "YOUR PART IN THE MATCH", {
        "YOU SET THE PACE OF THE DANGER. SPEND POINTS ON",
        "MONSTERS, THEN TAKE CONTROL OF ONE TO HUNT THE",
        "SURVIVORS YOURSELF.",
        "",
        "YOU CAN WIN BY WEARING DOWN THE TEAM OR KEEPING",
        "THE EXIT OUT OF REACH UNTIL TIME RUNS OUT." } },
    { "TAKING CONTROL", {
        "ARROWS MOVE AND TURN YOUR MONSTER. RUN MOVES",
        "FASTER. ACTION ATTACKS; AIM + ACTION USES A",
        "SECOND ATTACK.",
        "",
        "START SWITCHES BODIES. THE MONSTER PROFILES THAT",
        "FOLLOW EXPLAIN EACH SET OF ATTACKS." } },
    { "MOVING AROUND THE MANSION", {
        "OPTIONS OPENS OR CLOSES YOUR MAP. TO TRAVEL ON",
        "FOOT, PRESS ACTION AT A DOOR. SURVIVOR KEY LOCKS",
        "DO NOT STOP YOU.",
        "",
        "ON THE MAP, SELECT A ROOM AND PRESS ACTION TO",
        "JUMP TO A CONTROLLABLE MONSTER THERE." } },
    { "BUYING YOUR FIRST MONSTERS", {
        "ON THE MAP, ARROWS SELECT A ROOM. RUN OPENS THE",
        "MONSTER AND TRAP LIST; UP AND DOWN CHOOSE A TYPE.",
        "",
        "ACTION OR RUN RETURNS TO THE MAP. AIM BUYS AND",
        "PLACES YOUR SELECTION. CHECK ITS PRICE AND THE",
        "ROOM LIMIT FIRST." } },
    { "ROOM LIMITS", {
        "HUNTERS AND CHIMERAS USE TWO ROOM SLOTS.",
        "TYRANTS USE THREE. OTHER MONSTERS USE ONE.",
        "",
        "A TYRANT CAN FIT AN EMPTY ROOM EVEN WHEN ITS",
        "LIMIT IS BELOW THREE. LATER PURCHASES MUST FIT",
        "THE ROOM LIMIT." } },
    { "GIVING SURVIVORS SOME SPACE", {
        "OCCUPIED ROOMS ALLOW ONE DOOR REINFORCEMENT PER",
        "MINUTE: A ZOMBIE OR UNLOCKED HUNTER, NORMAL COST.",
        "",
        "SAFE ROOMS ARE ALWAYS OFF LIMITS: NO ENTRY, MAP",
        "JUMPS, MONSTERS OR TRAPS. THE HALL IS CLOSED",
        "DURING ITS PROTECTION TIMER." } },
    { "INTERCEPTING A SURVIVOR", {
        "BUY A ZOMBIE OR HUNTER IN AN OCCUPIED ROOM.",
        "IT USES THE CLOSEST SAFE DOOR OTHER THAN A",
        "SURVIVOR'S ENTRANCE. ROOM LIMITS STILL APPLY.",
        "",
        "SURVIVORS HEAR A ONE SECOND WARNING. IF NO DOOR",
        "IS SAFE, YOU SPEND NO POINTS OR COOLDOWN.",
        "YOU CAN STILL JUMP TO MONSTERS OR USE TRAPS." } },
    { "BUILDING YOUR BUDGET", {
        "YOU START WITH 500, 800 OR 1000 POINTS FOR 1, 2",
        "OR 3 SURVIVORS.",
        "",
        "WITH 1, 2 OR 3 SURVIVORS ALIVE, YOU EARN 1, 2 OR",
        "4 POINTS EACH SECOND. YOU DO NOT NEED TO RUSH",
        "EVERY PURCHASE." } },
    { "MAKING YOUR MONSTERS COUNT", {
        "A HIT FROM YOUR CONTROLLED MONSTER EARNS 50",
        "POINTS. ANOTHER MONSTER EARNS 25 FOR A HIT. BOTH",
        "BONUSES HAVE SHORT COOLDOWNS.",
        "",
        "WHEN A MONSTER DIES, YOU GET 25 PERCENT OF ITS",
        "PRICE BACK. ROOM LIMITS GROW AT 6 AND 10 MINUTES." } },
    { "MORE TOOLS AS TIME PASSES", {
        "STRONGER MONSTERS UNLOCK AS THE MATCH GOES ON.",
        "THEIR PROFILES LIST THE TIMES, COUNTED FROM WHEN",
        "ALL SURVIVORS ARRIVE.",
        "",
        "LOCK DOORS COSTS 100 POINTS. IT BLOCKS SURVIVOR",
        "ENTRANCES TO ONE ROOM FOR 10 SECONDS, WITH A",
        "ONE MINUTE COOLDOWN." } },
};
static const GuidePage kSurvivors[] = {
    { "YOU ARE IN THIS TOGETHER", {
        "EXPLORE, SHARE WHAT YOU FIND AND HELP EACH OTHER",
        "REACH THE COURTYARD. JUST ONE SURVIVOR NEEDS TO",
        "OPEN THE EXIT FOR THE TEAM TO WIN.",
        "",
        "YOU HAVE 20 MINUTES. COLLECT THE FOUR CRESTS AND",
        "KEEP AN EYE ON THE CLOCK AS YOU PLAN YOUR ROUTE." } },
    { "FINDING YOUR FEET", {
        "USE YOUR CONFIGURED GAME CONTROLS. ARROWS MOVE",
        "AND TURN; RUN RUNS. AIM + ACTION USES YOUR",
        "EQUIPPED WEAPON.",
        "",
        "ACTION OPENS DOORS, TAKES ITEMS AND WORKS",
        "PUZZLES. YOUR INVENTORY LETS YOU EQUIP, USE AND",
        "DROP ITEMS." } },
    { "PLANNING YOUR NEXT MOVE", {
        "OPTIONS OPENS YOUR ROUTE MAP. IT SHOWS KEY DOORS,",
        "KEYS, CRESTS AND THE ROOMS YOUR TEAMMATES ARE IN.",
        "",
        "WAIT FIVE SECONDS BEFORE RETURNING TO THE ROOM",
        "YOU JUST LEFT. OTHER EXITS REMAIN AVAILABLE.",
        "NEARBY MONSTERS ARE STUNNED WHEN YOU ARRIVE.",
        "STUN LASTS FIVE SECONDS. THEN THEY ARE IMMUNE",
        "TO ENTRY STUNS FOR TEN SECONDS." } },
    { "A PLACE TO CATCH YOUR BREATH", {
        "SAFE ROOMS KEEP THE DIRECTOR OUT. USE THE BREAK",
        "TO SORT YOUR SUPPLIES AND THINK ABOUT WHERE TO GO",
        "NEXT.",
        "",
        "EVERY ITEM BOX SHARES THE SAME TEAM STORAGE. IT",
        "STARTS EMPTY; LEAVE SOMETHING THERE FOR A",
        "TEAMMATE WHO NEEDS IT." } },
    { "SHARING WHAT YOU CARRY", {
        "IN THE INVENTORY COMMAND MENU, CHOOSE DROP TO",
        "LEAVE AN ITEM AT YOUR FEET FOR SOMEONE ELSE TO",
        "TAKE.",
        "",
        "DROPPED ITEMS STAY FOR THE MATCH. IF YOU DIE,",
        "YOUR INVENTORY FALLS AROUND YOUR BODY SO THE TEAM",
        "CAN RECOVER IT." } },
    { "BRINGING A TEAMMATE BACK", {
        "CARRY A FIRST AID SPRAY, STAND CLOSE TO THEIR",
        "BODY AND HOLD ACTION FOR FIVE SECONDS TO REVIVE",
        "THEM.",
        "",
        "KEEP CLOSE AND COVER EACH OTHER. RELEASING",
        "ACTION, GETTING HURT OR BEING GRABBED CANCELS",
        "YOUR ATTEMPT." } },
    { "THE CHANCE TO RETURN", {
        "THERE IS NO DEADLINE TO REVIVE A TEAMMATE.",
        "EACH SURVIVOR CAN COME BACK ONCE PER",
        "MATCH, AT 25 PERCENT HEALTH.",
        "",
        "REBECCA REVIVES FASTER, CAN USE A GREEN HERB AND",
        "BRINGS TEAMMATES BACK WITH MORE HEALTH. SEE HER",
        "PROFILE FOR DETAILS." } },
    { "STAYING WITH YOUR TEAM", {
        "AFTER DEATH, YOU STAY IN THE MATCH. AFTER FOUR",
        "SECONDS YOU WATCH A LIVING TEAMMATE; LEFT AND",
        "RIGHT CHANGE WHO YOU FOLLOW.",
        "",
        "A REVIVE BRINGS YOU BACK TO YOUR BODY. IF",
        "EVERYONE DIES, THE DIRECTOR WINS, SO HELP EACH",
        "OTHER WHILE YOU STILL CAN." } },
};
static void guide_text(int x, int y, unsigned char color, const char* text)
{
    zm_setup_text(x, y, color, text);
}
static int guide_general_count(void)
{
    return s_guideSection == 0 ? (int)(sizeof(kGeneral) / sizeof(kGeneral[0])) :
           s_guideSection == 1 ? (int)(sizeof(kDirector) / sizeof(kDirector[0])) :
                                (int)(sizeof(kSurvivors) / sizeof(kSurvivors[0]));
}
static int guide_page_count(void)
{
    return guide_general_count() + (s_guideSection == 1 ? 7 : s_guideSection == 2 ? ZM_CHAR_COUNT : 0);
}
static void title_guide_update(void)
{
    unsigned int pad = g_PlayerPadPressed;
    int oldSection = s_guideSection, oldPage = s_guidePage;
    if (pad & 0x40) {
        play_sfx(SFX_UI_BANK, SFX_UI_CANCEL);
        zm_guide_profile(-1, -1);
        if (s_guidePage >= 0) s_guidePage = -1;
        else { s_guideOpen = false; zm_guide_background(false); }
        g_titleDemoTime = 0x708;
        return;
    }
    const GuidePage* pages = s_guideSection == 0 ? kGeneral :
                             s_guideSection == 1 ? kDirector : kSurvivors;
    int count = guide_page_count();
    if (s_guidePage < 0) {
        if (pad & 0x1000) s_guideSection = (s_guideSection + 3) % 4;
        if (pad & 0x4000) s_guideSection = (s_guideSection + 1) % 4;
        if (pad & 0x80) {
            play_sfx(SFX_UI_BANK, SFX_UI_DECIDE);
            if (s_guideSection == 3) { zm_guide_profile(-1, -1); zm_guide_background(false); s_guideOpen = false; return; }
            s_guidePage = 0;
        }
    } else {
        if (pad & 0x8000) s_guidePage = (s_guidePage + count - 1) % count;
        if (pad & (0x2000 | 0x80)) s_guidePage = (s_guidePage + 1) % count;
    }
    if (oldSection != s_guideSection || oldPage != s_guidePage) {
        if (pad & 0x80) {
            if (oldPage >= 0) play_sfx(SFX_UI_BANK, SFX_UI_DECIDE);
        } else play_sfx(SFX_UI_BANK, SFX_UI_CURSOR);
    }
    zm_guide_background(true);
    bool profile = s_guidePage >= guide_general_count();
    if (!profile) zm_guide_profile(-1, -1);
    if (s_guidePage < 0) {
        static const char* labels[] = { "GENERAL / STARTING A MATCH", "THE DIRECTOR", "THE SURVIVORS", "BACK TO MAIN MENU" };
        guide_text(24, 20, 0x8F, ZM_MODE_TITLE " GUIDE");
        for (int i = 0; i < 4; ++i) {
            guide_text(24, 65 + i * 21, i == s_guideSection ? 0x8F : 0x7F, labels[i]);
            if (i == s_guideSection) guide_text(8, 65 + i * 21, 0x8F, ">");
        }
    } else {
        // Re-evaluate after entering a section in this frame.
        pages = s_guideSection == 0 ? kGeneral : s_guideSection == 1 ? kDirector : kSurvivors;
        count = guide_page_count();
        if (profile) {
            int index = s_guidePage - guide_general_count();
            zm_guide_profile(s_guideSection == 2 ? index : -1,
                             s_guideSection == 1 ? index : -1);
        } else {
            guide_text(8, 16, 0x8F, pages[s_guidePage].heading);
            for (int i = 0; i < 8; ++i)
                if (pages[s_guidePage].lines[i]) guide_text(8, 44 + i * 18, 0x7F, pages[s_guidePage].lines[i]);
        }
        char footer[64];
        snprintf(footer, sizeof(footer), "PAGE %d/%d  LEFT/RIGHT: PAGE", s_guidePage + 1, count);
        guide_text(8, 202, 0x7F, footer);
    }
    guide_text(8, 232 - 14, 0x7F, s_guidePage < 0 ? "UP/DOWN: SELECT ACTION: OPEN RUN: BACK" : "ACTION: NEXT  RUN / ESC: BACK");
}

// Generated artwork matches the original menu sheet's bold capitals.
// Embedded TIM keeps both platform builds independent of loose image files.
static void title_build_guide_label(void)
{
    LoadTexturePage(kGuideTitleTim, 10, 0, 14, 0, 0, 0, 0);
}

// Archived INFESTATION alternative (the previous renderer and placement).
// To restore it: uncomment this block and change the title initialization/draw
// calls below to title_build_infestation / title_draw_infestation.
/*
// PvP INFESTATION wordmark: a dedicated procedural 4-bit TIM renderer.
// The silhouette is hand-lettered; a seeded cellular field shades its tissue.
// Generated once per title entry, then submitted as one sprite per frame.
static void title_build_infestation(void)
{
    // Individual brush paths, not a font grid. Coordinates describe just
    // this image; each letter has its own lean, height and baseline.
    struct Stroke { unsigned char letter; float x0, y0, x1, y1; };
    static const Stroke strokes[] = {
        {0,2,1,13,0},{0,8,0,5,18},{0,0,18,11,19}, // I
        {1,0,19,2,0},{1,2,0,15,18},{1,15,18,16,0}, // N
        {2,2,19,3,0},{2,3,0,16,1},{2,2,9,13,8}, // F
        {3,3,1,0,19},{3,3,1,15,0},{3,2,9,12,10},{3,0,19,15,17}, // E
        {4,16,1,6,0},{4,6,0,1,5},{4,1,5,5,9},{4,5,9,13,10},
        {4,13,10,16,14},{4,16,14,12,18},{4,12,18,1,17}, // S
        {5,0,1,17,0},{5,9,0,6,19}, // T
        {6,0,19,8,0},{6,8,0,17,18},{6,4,11,13,10}, // A
        {7,0,0,17,2},{7,9,1,7,19}, // T
        {8,2,1,13,0},{8,8,0,5,18},{8,0,18,11,19}, // I
        {9,8,0,2,3},{9,2,3,0,13},{9,0,13,5,18},
        {9,5,18,13,17},{9,13,17,17,10},{9,17,10,15,3},{9,15,3,8,0}, // O
        {10,0,18,2,0},{10,2,0,15,19},{10,15,19,17,1} // N
    };
    const int width = 224, height = 32;
    static unsigned char mask[width * height];
    memset(mask, 0, sizeof(mask));
    unsigned int rng = 0xE91DE11Cu;
    auto random = [&rng]() { rng = rng * 1664525u + 1013904223u; return rng >> 16; };
    auto dab = [&](float cx, float cy, float radius) {
        for (int y = (int)(cy - radius - 1); y <= (int)(cy + radius + 1); ++y)
            for (int x = (int)(cx - radius - 1); x <= (int)(cx + radius + 1); ++x) {
                float dx = x - cx, dy = y - cy;
                if (x > 0 && x < width - 1 && y > 0 && y < height - 1 &&
                    dx * dx + dy * dy < radius * radius) mask[y * width + x] = 1;
            }
    };
    static const float baseline[11] = {4,2,5,3,4,1,4,2,5,2,3};
    static const float stretch[11] = {0.82f,0.91f,0.74f,0.86f,0.78f,0.96f,0.83f,0.88f,0.74f,0.91f,0.84f};
    for (const Stroke& stroke : strokes) {
        int g = stroke.letter;
        for (int t = 0; t <= 32; ++t) {
            float u = t / 32.0f;
            float y = stroke.y0 + (stroke.y1 - stroke.y0) * u;
            float x = stroke.x0 + (stroke.x1 - stroke.x0) * u;
            float wobble = ((int)(random() % 101) - 50) / 140.0f;
            dab(7 + g * 19 + x * 0.87f + y * (g % 2 ? -0.10f : 0.13f),
                baseline[g] + y * stretch[g] + wobble,
                1.35f + (random() % 100) / 120.0f);
        }
    }
    // A few short, attached beads rather than a fringe under every letter.
    // Keep the trails inside the existing image, above the menu text.
    static const int dripX[] = {47, 86, 127, 183};
    for (int i = 0; i < 4; ++i) {
        int x = dripX[i];
        for (int y = 23; y >= 12; --y) if (mask[y * width + x]) {
            int length = 3 + i % 3;
            for (int d = 1; d <= length; ++d)
                dab((float)x, (float)(y + d), d == 1 ? 1.1f : 0.75f);
            dab((float)x, (float)(y + length), 1.2f);
            break;
        }
    }
    struct Cell { int x, y; };
    Cell cells[160];
    for (int i = 0; i < 160; ++i) {
        cells[i].x = (i % 32) * 7 + random() % 6;
        cells[i].y = (i / 32) * 7 + random() % 6;
    }
    alignas(4) unsigned char tim[64 + width * height / 2] = {};
    unsigned int* header = (unsigned int*)tim;
    header[0] = 0x10; header[1] = 8; header[2] = 44;
    unsigned short* clut = (unsigned short*)(tim + 12);
    clut[0] = 0; clut[1] = 0x1E0; clut[2] = 16; clut[3] = 1;
    unsigned short* palette = (unsigned short*)(tim + 20);
    for (int i = 1; i < 16; ++i) {
        int r = 4 + i * 27 / 15;
        int g = i > 12 ? (i - 12) : 0;
        int b = i > 12 ? (i - 12) : 0;
        palette[i] = (unsigned short)(r | g << 5 | b << 10);
    }
    *(unsigned int*)(tim + 52) = 12 + width * height / 2;
    unsigned short* image = (unsigned short*)(tim + 56);
    image[2] = width / 4; image[3] = height;
    for (int y = 1; y < height - 1; ++y) for (int x = 1; x < width - 1; ++x) {
        int shade = 0;
        if (mask[y * width + x]) {
            int first = 100000, second = 100000;
            for (int i = 0; i < 160; ++i) {
                int dx = x - cells[i].x, dy = y - cells[i].y;
                int distance = dx * dx + dy * dy;
                if (distance < first) { second = first; first = distance; }
                else if (distance < second) second = distance;
            }
            // Shade the continuous brush volume rather than dithering each
            // pixel. Cellular boundaries remain subtle beneath the wet face.
            int edgeDistance = 16;
            for (int dy = -3; dy <= 3; ++dy) for (int dx = -3; dx <= 3; ++dx) {
                int nx = x + dx, ny = y + dy;
                int distance = dx * dx + dy * dy;
                if (nx < 0 || nx >= width || ny < 0 || ny >= height ||
                    !mask[ny * width + nx])
                    if (distance < edgeDistance) edgeDistance = distance;
            }
            shade = edgeDistance <= 1 ? 6 : edgeDistance <= 4 ? 10 : 12;
            if (second - first < 4 && shade > 7) shade -= 2;
            if (first <= 1 && shade > 8) shade -= 2;
            // Sparse highlights on the upper-left rim, a dark lower rim.
            if (!mask[(y - 1) * width + x] && mask[y * width + x + 1]) shade = 13;
            if (!mask[(y + 1) * width + x]) shade = 4;
        } else if (mask[y * width + x - 1] && mask[(y - 1) * width + x]) shade = 1;
        tim[64 + y * (width / 2) + x / 2] |= (unsigned char)(shade << ((x & 1) * 4));
    }
    LoadTexturePage(tim, 11, 0, 15, 0, 0, 0, 0);
}

static void title_draw_infestation(unsigned char brightness)
{
    TextureDesc word = {};
    word.flags = brightness == 128 ? 0x10000000 : 0x40000000;
    word.texturePage = 11;
    word.clutY = 0x1E0;
    // Centered screen coordinates: right of the original logo's center,
    // below its lower edge, clear of PRESS START and the menu.
    word.screenX = -72; word.screenY = 37;
    word.width = 224; word.height = 32;
    word.colorMulR = word.colorMulG = word.colorMulB = brightness;
    // Two translucent black silhouettes feather the edge without a panel.
    for (int layer = 0; layer < 2; ++layer) {
        TextureDesc shadow = word;
        shadow.texturePage = 12 + layer;
        int first = g_SpriteQueueCount;
        display_texture(&shadow, 2, 16 + layer, 1);
        for (int i = first; i < g_SpriteQueueCount; ++i)
            g_SpriteCommandBuffer[i].alpha = (layer == 0 ? 0.12f : 0.22f) * brightness / 128.0f;
    }
    display_texture(&word, 2, 15, 1);
}
*/

// PvP wordmark: infested/images/infested.png converted to a 4-bit TIM by
// tools/gen_infested_title.py and baked into InfestedTitleImage.h.
static void title_build_infested(void)
{
    LoadTexturePage(kInfestedTitleTim, 11, 0, 15, 0, 0, 0, 0);
    LoadTexturePage(kInfestedShadowTim0, 12, 0, 16, 0, 0, 0, 0);
    LoadTexturePage(kInfestedShadowTim1, 13, 0, 17, 0, 0, 0, 0);
}

static void title_draw_infested(unsigned char brightness)
{
    TextureDesc word = {};
    word.flags = brightness == 128 ? 0x10000000 : 0x40000000;
    word.texturePage = 11;
    word.clutY = 0x1E0;
    // Centered screen coordinates: centred across the screen, raised a
    // quarter of its height above y 49, where the code-drawn wordmark sat.
    word.width = kInfestedTitleWidth; word.height = kInfestedTitleHeight;
    word.screenX = -kInfestedTitleWidth / 2;
    word.screenY = 49 - kInfestedTitleHeight / 2 - kInfestedTitleHeight / 4;
    word.colorMulR = word.colorMulG = word.colorMulB = brightness;
    // The 320x240 screen is scaled to the window per axis, so a wide window
    // stretches sprites sideways. Narrow the quad (the UVs still cover the
    // whole texture) so the artwork keeps its own aspect on any window.
    float scaleX = 1.0f, scaleY = 1.0f;
    MarniGetRenderScale(&scaleX, &scaleY);
    int drawnWidth = kInfestedTitleWidth;
    if (scaleX > 0.0f && scaleY > 0.0f)
        drawnWidth = (int)(kInfestedTitleWidth * scaleY / scaleX + 0.5f);
    auto fit = [drawnWidth](int first) {
        for (int i = first; i < g_SpriteQueueCount; ++i) {
            TextureDraw& cmd = g_SpriteCommandBuffer[i];
            int center = cmd.x0 + kInfestedTitleWidth / 2;
            cmd.x0 = (short)(center - drawnWidth / 2);
            cmd.x1 = (short)(cmd.x0 + drawnWidth - 1);
        }
    };
    // Two translucent black silhouettes feather the edge without a panel.
    for (int layer = 0; layer < 2; ++layer) {
        TextureDesc shadow = word;
        shadow.texturePage = 12 + layer;
        int first = g_SpriteQueueCount;
        display_texture(&shadow, 2, 16 + layer, 1);
        fit(first);
        for (int i = first; i < g_SpriteQueueCount; ++i)
            g_SpriteCommandBuffer[i].alpha = (layer == 0 ? 0.12f : 0.22f) * brightness / 128.0f;
    }
    int first = g_SpriteQueueCount;
    display_texture(&word, 2, 15, 1);
    fit(first);
}

// ============================================================================
// set_display_resolution (0x00401000)
// ============================================================================
void set_display_resolution(int w, int h, int mode)
{
    g_displayWidth = w;
    g_displayHeight = h;
    g_displayMode = mode;
}

// ============================================================================
// check_save_files_exist (0x00494190)
// Returns 1 if any save files exist, 0 otherwise.
// ============================================================================
int check_save_files_exist(void)
{
    char path[260];
    for (int i = 1; i <= 8; i++) {
        sprintf(path, "%ssavedat%d.dat", GetSaveRoot(), i);
        FILE* f = fopen(path, "rb");
        if (f != NULL) {
            fclose(f);
            return 1;
        }
    }
    return 0;
}

// ============================================================================
// title_setup_texture_pages (0x00470970)
// Creates texture pages for button prompt images.
// ============================================================================
void title_setup_texture_pages(int slot, int mode)
{
    // Same legacy-descriptor caveat as TextureLoader: these tables are indexed
    // by slot * 0x37C and the port's stand-ins are a few KB, so the room-load
    // calls (load_room_bg passes the camera index, 0..7) run off the end. The
    // DX11 path takes page state from the g_TexturePage* arrays, so reads that
    // fall out of range can yield 0 - but the destroy loop's writes must not
    // happen at all.
    int slotBase = slot * 0x37C;
    const bool cntOk   = (size_t)slotBase / sizeof(DWORD)
                         < sizeof(g_VideoDriverArray_814) / sizeof(DWORD);
    const bool tableOk = (size_t)slotBase + 8 * sizeof(DWORD) <= sizeof(g_TexturePageTable_DAT);
    const bool dataOk  = (size_t)slotBase + 2 * 0x68 <= sizeof(g_VideoDriverArray_4d0);

    int pageCount = cntOk ? g_VideoDriverArray_814[slotBase / 4] : 0;
    if (pageCount != 0 && tableOk) {
        DWORD* pageTable = (DWORD*)((BYTE*)&g_TexturePageTable_DAT + slotBase);
        for (int i = 0; i < pageCount; i++) {
            if ((size_t)slotBase + (size_t)(i + 1) * sizeof(DWORD)
                > sizeof(g_TexturePageTable_DAT)) {
                break;
            }
            if (pageTable[i] != 0) {
                destroy_texture_page(pageTable[i]);
                pageTable[i] = 0;
            }
        }
    }

    if (dataOk) {
        BYTE* pageData = (BYTE*)&g_VideoDriverArray_4d0 + slotBase;
        for (int i = 0; i < 2; i++) {
            int handle = create_texture_page(pageData, (mode != 0) ? 26 : 10);
            if (handle == 0) break;
            pageData += 0x68;
        }
    }

    g_titleTextureSlotId = slot;
}

// ============================================================================
// init_title_screen (0x004306e0)

// ============================================================================
// Director's Cut title option sheets, assembled from the disc's own
// DATA/BT367OAB.TIM.
//
// The DC keeps its seven 256x80 option cells in one file, 0x2840 bytes each:
//
//   +0x00  u32 0x10, u32 8            magic, then 4bpp-with-CLUT
//   +0x08  u32 44, s16 x, s16 y,      CLUT block: 16 entries, one row
//          u16 16, u16 1, 16 x u16
//   +0x34  u32 10252, s16 x, s16 y,   image block: width is in 16-bit units,
//          u16 64, u16 80             so 64 means 256 pixels
//   +0x40  10240 bytes                4bpp pixels, two per byte, low nibble
//                                     is the left pixel
//
// 7 x 80 = 560 rows cannot live on one texture page - TextureDesc.texV is a
// byte, so a page tops out at 256 rows. The cells are therefore split across
// two pages. That split used to happen offline, in tools/port_dc_assets.py,
// which wrote t_dc.tim and t_dc2.tim; doing it here instead means the DC asset
// tree holds the file the player's own disc has rather than two invented ones.
//
//   page A (slot 12)  cells 0,1,2 at full height  -> 256x240, the main menu
//   page B (slot 13)  cells 3,4,5,6 cropped to 64 -> 256x256, the difficulty
//                                                    submenu
//
// The crop keeps each cell's row 0, so drawing every row at screenY 0 (plus the
// port's +38) lands where the PS1 draws it. Cell 6 is the ADVANCED* frame: the
// same art as cell 5 but green, and since the port keeps ONE palette per page,
// its colour 1 is remapped onto the shared palette's unused entry 5 rather than
// carried as a second CLUT row.
// ============================================================================
#define DC_TITLE_CHUNK      0x2840
#define DC_TITLE_CELL_W     256
#define DC_TITLE_CELL_H     80
#define DC_TITLE_ROW_BYTES  (DC_TITLE_CELL_W / 2)   /* 4bpp: 128 B per row */
#define DC_TITLE_CLUT_OFF   0x14
#define DC_TITLE_PIX_OFF    0x40
#define DC_TITLE_GREEN_SLOT 5

// Build one 4bpp TIM page into `out`. `cells` lists the source cell indices,
// `rows` how many rows of each to take, `palFrom` which of them supplies the
// page palette, and `greenFrom` which (an index INTO cells, or -1) needs the
// colour-1 -> colour-5 remap described above. Returns the bytes written.
static unsigned int dc_title_build_page(const unsigned char* file,
                                        const int* cells, int count, int rows,
                                        int palFrom, int greenFrom,
                                        unsigned char* out)
{
    const int h = rows * count;
    const unsigned int pixBytes = (unsigned int)(DC_TITLE_CELL_W * h / 2);
    unsigned char* p = out;

    *(unsigned int*)p = 0x10;      p += 4;
    *(unsigned int*)p = 8;         p += 4;      // 4bpp + CLUT
    *(unsigned int*)p = 12 + 32;   p += 4;      // CLUT block length
    *(short*)p = 0;                p += 2;
    *(short*)p = 0x1E0;            p += 2;      // the PS1's own clutY
    *(unsigned short*)p = 16;      p += 2;
    *(unsigned short*)p = 1;       p += 2;
    memcpy(p, file + cells[palFrom] * DC_TITLE_CHUNK + DC_TITLE_CLUT_OFF, 32);
    if (greenFrom >= 0) {
        // the ADVANCED* cell's own colour 1 - pure green - into the spare slot
        *(unsigned short*)(p + DC_TITLE_GREEN_SLOT * 2) =
            *(const unsigned short*)(file + cells[greenFrom] * DC_TITLE_CHUNK
                                     + DC_TITLE_CLUT_OFF + 2);
    }
    p += 32;

    *(unsigned int*)p = 12 + pixBytes;                     p += 4;
    *(short*)p = 0;                                        p += 2;
    *(short*)p = 0;                                        p += 2;
    *(unsigned short*)p = (unsigned short)(DC_TITLE_CELL_W / 4); p += 2;
    *(unsigned short*)p = (unsigned short)h;               p += 2;

    for (int i = 0; i < count; i++) {
        const unsigned char* cell =
            file + cells[i] * DC_TITLE_CHUNK + DC_TITLE_PIX_OFF;
        unsigned int n = (unsigned int)(rows * DC_TITLE_ROW_BYTES);
        memcpy(p, cell, n);
        if (i == greenFrom) {
            for (unsigned int k = 0; k < n; k++) {
                unsigned char lo = (unsigned char)(p[k] & 0xF);
                unsigned char hi = (unsigned char)(p[k] >> 4);
                if (lo == 1) lo = DC_TITLE_GREEN_SLOT;
                if (hi == 1) hi = DC_TITLE_GREEN_SLOT;
                p[k] = (unsigned char)(lo | (hi << 4));
            }
        }
        p += n;
    }
    return (unsigned int)(p - out);
}

// ============================================================================
void init_title_screen(void)
{
    s_guideOpen = false;
    s_guidePage = -1;
    if (g_bPlayAsZombie && !g_bDcMode) {
        title_build_guide_label();
        title_build_infested();
    }
    g_bGameActive = 0;
    set_display_resolution(320, 240, 0);

    g_roomCameraId = 0;

    // The Director's Cut's own title art ("DIRECTOR'S CUT") is its overlay's
    // title.pix, so this stays one unconditional load - the base tree's file is
    // never touched and OG still gets today's screen.
    LoadFile(GAME_DATA_ROOT "data\\title.pix", g_TimImageBuffer__bitmap, 0x20);
    display_image(0, g_TimImageBuffer__bitmap, 320, 240);

    title_setup_texture_pages(0, 1);

    //empty_00470960(0);

    // The DC's cells come from one file; stage it whole, because page B below
    // needs it again. Everything else loads a ready-made sheet.
    if (g_bDcMode) {
        static const int kTitlePageA[3] = { 0, 1, 2 };
        LoadFile(GAME_DATA_ROOT "data\\bt367oab.tim", g_bgPakLoadBuffer, 0x20);
        dc_title_build_page(g_bgPakLoadBuffer, kTitlePageA, 3, DC_TITLE_CELL_H,
                            1, -1, g_TimImageBuffer__bitmap);
    } else {
        const char* buttonTexPath = !g_bPadConnected
            ? GAME_DATA_ROOT "data\\t_press.tim"
            : GAME_DATA_ROOT "data\\t_start.tim";
        LoadFile(buttonTexPath, g_TimImageBuffer__bitmap, 0x20);
    }

    g_titleTexturePageData[4] = 26;
    g_titleTexturePageData[0] = 8;
    g_TextureCurrentPage = 26;
    g_TextureBankID = 8;
    LoadTexturePage(g_TimImageBuffer__bitmap, 8, 0, 12, 4, 0, 0, 0);

    g_titleTexturePageData[1] = g_TextureBankID;
    g_titleLoopFlag = 1;
    g_titleTexturePageData[5] = g_TextureCurrentPage;
    g_titleTexturePageData[2] = g_titleTexturePageData[1];
    g_titleTexturePageData[6] = g_titleTexturePageData[5];

    // The DC submenu sheet is a second texture page: slot 13 -> SRV 28, beside
    // the main sheet's slot 12 -> SRV 27. Rows 3-6 draw from it (see the table).
    if (g_bDcMode) {
        static const int kTitlePageB[4] = { 3, 4, 5, 6 };
        dc_title_build_page(g_bgPakLoadBuffer, kTitlePageB, 4, 64, 0, 3,
                            g_TimImageBuffer__bitmap);
        LoadTexturePage(g_TimImageBuffer__bitmap, 9, 0, 13, 0, 0, 0, 0);
        g_titleTexturePageData[3] = 9;
        g_titleTexturePageData[4] = 9;
        g_titleTexturePageData[5] = 9;
        g_titleTexturePageData[6] = 9;
    }

    {
        char dbg[256];
        sprintf(dbg, "[INIT] LoadTexturePage done, SRV[27]=%p\n", g_TexturePageSRV[27]);
        OutputDebugStringA(dbg);
    }


    if (!(g_bPlayAsZombie && !g_bDcMode) && check_save_files_exist()) {
        g_titleSelectionId = 2;
        g_main_state_flags &= ~MSF_SCREEN_MODE_MASK;
        return;
    }
    g_titleSelectionId = 1;
    g_main_state_flags &= ~MSF_SCREEN_MODE_MASK;
}

// ============================================================================
// set_scene_render_param (0x0040a8e0)
// ============================================================================
void set_scene_render_param(int value)
{
    g_sceneRenderParam = value;
}

// ============================================================================
// title_exit_loop (0x00430e10)
// ============================================================================
void title_exit_loop(void)
{
    g_titleLoopFlag = 0;
    g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;
}

// ============================================================================
// fade_update (0x0047b950)
// ============================================================================
void fade_update(void)
{
    if (g_fading_state <= 0 && g_fading_counter != 0) {
        if (g_fading_counter <= 0) {
            g_fading_state = 0x7FFF;
        } else {
            g_fading_state = 0;
        }
    }
}

// ============================================================================
// read_sidewinder_pad (0x00497e30)
// Original: return g_pMasterInputState.field466_0x200 (joystick[0].currPress)
// ============================================================================
int read_sidewinder_pad(void)
{
    return g_pMasterInputState.joysticks[0].currPress;
}

// ============================================================================
// TitleTextPosData and UpdateTitleTextSprite
// ============================================================================
struct TitleTextPosData {
    int vramY;
    int sprHeight;
    int screenY;
    int slot;       // display_texture SRV slot (12 = the main sheet, 13 = the DC submenu sheet)
};

// USA sheet (data\t_press.tim / data\t_start.tim, 256x256): the PC build
// re-cropped the three frames out of the PS1's 80-row cells, hence the per-row
// screenY and the shorter heights.
static const TitleTextPosData g_titleTextPosTableUsa[3] = {
    { 0,  54, 24, 12 },  // index 0: "PRESS ANY BUTTON" / "PRESS START BUTTON"
    { 83, 70,  8, 12 },  // index 1: "NEW GAME"
    { 175,70,  8, 12 },  // index 2: "LOAD GAME"
};

// Director's Cut (data\t_dc.tim + data\t_dc2.tim, built by
// tools/port_dc_assets.py). The PS1 draws every option as a whole 256x80 cell at
// a fixed screenX -130 / screenY 38 (title_draw_option, TITLE.EXE 0x800e14d8),
// so screenY is 0 here and the +38 in UpdateTitleTextSprite supplies it; the
// cells keep their internal offsets. Rows 0-2 are the main menu (t_dc.tim);
// 3-6 are the STANDARD/TRAINING/ADVANCED submenu (t_dc2.tim, cropped to 64 rows
// per cell) - row 6 is ADVANCED held, drawn with the green palette entry.
static const TitleTextPosData g_titleTextPosTableDc[7] = {
    {   0, 80, 0, 12 },  // 0: PRESS ANY BUTTON
    {  80, 80, 0, 12 },  // 1: NEW GAME
    { 160, 80, 0, 12 },  // 2: LOAD GAME
    {   0, 64, 0, 13 },  // 3: STANDARD
    {  64, 64, 0, 13 },  // 4: TRAINING
    { 128, 64, 0, 13 },  // 5: ADVANCED
    { 192, 64, 0, 13 },  // 6: ADVANCED, confirm held
};

// ============================================================================
// UpdateTitleTextSprite (0x00430d40)
// ============================================================================
void UpdateTitleTextSprite(unsigned char brightness, unsigned char selectionId)
{
    if (g_bPlayAsZombie && !g_bDcMode) title_draw_infested(brightness);
    const bool guideSelected = selectionId == 2;
    if (g_bPlayAsZombie && !g_bDcMode && selectionId != 0) selectionId = 1;
    TextureDesc* td = &g_TextureDesc;

    td->flags = 0x10000000;
    if (brightness != 0x80) {
        td->flags = 0x40000000;
    }

    const TitleTextPosData* entry;
    if (g_bDcMode) {
        if (selectionId > 6) selectionId = 0;
        entry = &g_titleTextPosTableDc[selectionId];
    } else {
        if (selectionId > 2) selectionId = 0;
        entry = &g_titleTextPosTableUsa[selectionId];
    }

    td->texU = 0;
    td->screenX = -130;
    td->texturePage = g_titleTexturePageData[selectionId];
    td->width = 256;
    td->texV = (unsigned char)entry->vramY;
    td->height = entry->sprHeight;
    td->screenY = entry->screenY + 38;
    // g_titleCurrentSprH = (float)entry->sprHeight;

    td->colorMulR = brightness;
    td->clutX = 0;
    td->colorMulG = brightness;
    td->pivotX = 0;
    td->pivotY = 0;
    td->colorMulB = brightness;

    td->clutY = 0x1E0;

    // Port-added mod: draw NEW GAME and our generated GUIDE label, but
    // the sheet's NEW GAME cell (rows 83-152 of t_press.tim / t_start.tim)
    // carries the whole menu: NEW GAME lit (rows 84-94), LOAD GAME dimmed
    // (102-112), the copyright (131-151). Drawn in two pieces, it leaves the
    // LOAD GAME line out.
    if (g_bPlayAsZombie && !g_bDcMode && selectionId == 1) {
        const int cellTop = entry->vramY;
        // Keep the PvP menu below INFESTED; copyright stays in place.
        const int menuOffsetY = 12;
        td->screenY += menuOffsetY;
        td->texV = (unsigned char)cellTop;
        td->height = 98 - cellTop;                      // NEW GAME
        td->colorMulR = td->colorMulG = td->colorMulB = guideSelected ? brightness / 2 : brightness;
        display_texture(td, 2, entry->slot, 1);
        td->colorMulR = td->colorMulG = td->colorMulB = brightness;
        td->texV = 120;
        td->height = cellTop + entry->sprHeight - 120;  // the copyright
        td->screenY = entry->screenY + 38 + (120 - cellTop);
        display_texture(td, 2, entry->slot, 1);
        td->texV = 0; td->height = 16; td->screenY = 65 + menuOffsetY;
        td->texturePage = 10;
        td->colorMulR = td->colorMulG = td->colorMulB = guideSelected ? brightness : brightness / 2;
        display_texture(td, 2, 14, 1);
        return;
    }

    display_texture(td, 2, entry->slot, 1);
}

// ============================================================================
// update_title_options (0x00430810)
// ============================================================================
void update_title_options(void)
{
    title_infested_voice_frame();
	DWORD sidewinderPress = 0;
	DWORD sidewinderState = 0;
	if (g_bPadConnected) {
		sidewinderState = read_sidewinder_pad();
		sidewinderPress = sidewinderState & 0x10000 & ~g_PlayerPadHeldPrev;
	}
	g_PlayerPadHeldPrev = sidewinderState;

    if (s_guideOpen) { title_guide_update(); return; }

    if (g_titleMode != 0) {
        if (g_titleMode != 1) return;

        switch (g_titleOptionsFading) {
        case 10:
            // DC difficulty submenu (PS1 TITLE.EXE g_titleState 10). Up/down cycle
            // STANDARD(3) / TRAINING(4) / ADVANCED(5); holding confirm on
            // ADVANCED shows the green ADVANCED*(6) cell; confirm writes
            // g_DcDifficulty and then runs the original new-game exit.
            if (g_PlayerPadPressed & 0x5100) {
                if (!(g_PlayerPadPressed & 0x1000)) {
                    if (g_titleSelectionId < 5) g_titleSelectionId++;
                    else g_titleSelectionId = 3;
                } else {
                    if (g_titleSelectionId < 4) g_titleSelectionId = 5;
                    else g_titleSelectionId--;
                }
                g_titleHoldTimer = 0x5A;
                g_titleDemoTime = 0x708;
            }

            if ((g_PlayerPadPressed & 0xeff) || sidewinderPress) {
                play_sfx(SFX_BANKS, SFX_TITLE_EVIL01);
                play_sfx(SFX_BANKS, 1); // null sfx
                // The PS1 keys ADVANCED* off the hold timer, not the choice (it
                // drops the choice back to 5 after drawing the green cell).
                if (g_titleSelectionId == 5) {
                    g_DcDifficulty = (g_titleHoldTimer == 0) ? DC_DIFFICULTY_ADVANCED_HOLD
                                                            : DC_DIFFICULTY_ADVANCED;
                } else {
                    g_DcDifficulty = g_titleSelectionId - 3;   // 3 -> STANDARD, 4 -> TRAINING
                }
                // Keep the submenu choice: the PS1 leaves g_titleMenuChoice at
                // 3..5, so the exit fade-out keeps drawing the difficulty cell.
                // The exit switch maps 3..6 to the new-game action.
                g_titleOptionsFading = 6;
                g_fade_type_id = 1;
                g_fading_counter = 0x7F00;
                fade_update();
                g_bGameActive = 2;
                return;
            }

            // Hold Right on ADVANCED to reach the green ADVANCED* cell; the PS1
            // draws the green cell once and drops back to 5 so the mode stays
            // ADVANCED until confirm. The DC reads the *held* pad word
            // (0x800cf844); g_PlayerPadHeld is the edge-detected word here, so
            // use g_button_pressed_id (== g_RawPadHeld, continuous) with the raw
            // d-pad layout: 0x1000 up, 0x2000 right, 0x4000 down, 0x8000 left.
            if (g_titleSelectionId == 5 && (g_button_pressed_id & 0x2000)) {
                if (g_titleHoldTimer != 0) g_titleHoldTimer--;
            }
            if (g_titleHoldTimer == 0) g_titleSelectionId = 6;

            UpdateTitleTextSprite(128, g_titleSelectionId);
            if (g_titleSelectionId == 6) g_titleSelectionId = 5;

            g_titleDemoTime--;
            if (g_titleDemoTime == 0) {
                g_titleOptionsFading = 3;
                g_fade_type_id = 2;
                g_fading_counter = 0x400;
                fade_update();
            }
            return;

        case 0:
            g_titleOptionsFading = 1;
            g_fade_type_id = 2;
            g_fading_counter = 0xFC00;
            g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_REBUILD;
            fade_update();
            return;

        case 1:
            if (g_fading_state < 0) {
                g_titleOptionsFading = 2;
                g_titleDemoTime = 0x708;
            }
            // PvP has no saves; start on NEW GAME, with GUIDE below it.
            if (g_bPlayAsZombie && !g_bDcMode) g_titleSelectionId = 1;
            UpdateTitleTextSprite(128, g_titleSelectionId);
            return;

	case 2:
        if (g_bPlayAsZombie && !g_bDcMode) {
            if (g_PlayerPadPressed & 0x5000) g_titleSelectionId = g_titleSelectionId == 1 ? 2 : 1;
            if (g_titleSelectionId == 2 && ((g_PlayerPadPressed & 0x80) || sidewinderPress)) {
                s_guideOpen = true; s_guideSection = 0; s_guidePage = -1;
                return;
            }
        }
		UpdateTitleTextSprite(128, g_titleSelectionId);

		// DC: confirming NEW GAME opens the STANDARD/TRAINING/ADVANCED submenu
		// (state 10 below) instead of starting the game. The PS1 is silent here:
		// the title sfx plays on the *submenu* confirm, or on LOAD GAME, which
		// falls through to the original path below.
		if (g_bDcMode && ((g_PlayerPadPressed & 0xeff) || sidewinderPress) &&
		    g_titleSelectionId == 1) {
			g_titleOptionsFading = 10;
			g_titleSelectionId = 3;          // STANDARD
			g_titleDemoTime = 0x708;
			g_titleHoldTimer = 0x5A;
			return;
		}

		if ((g_PlayerPadPressed & ((g_bPlayAsZombie && !g_bDcMode) ? 0x80 : 0xeff)) || sidewinderPress) {
			play_sfx(SFX_BANKS, SFX_TITLE_EVIL01);
			play_sfx(SFX_BANKS, 1); // null sfx
			if (g_bPlayAsZombie && !g_bDcMode && g_titleSelectionId == 1) {
				s_infestedStartMs = plat_time_ms();
				s_infestedVoicePhase = s_infestedBank ? 1 : 0;
			}
			g_titleOptionsFading = 6;
			g_fade_type_id = 1;
			g_fading_counter = 0x7F00;
			fade_update();
			g_bGameActive = 2;
			return;
		}

		if ((g_PlayerPadPressed & 0x5100) && !(g_bPlayAsZombie && !g_bDcMode)) {
			if (!(g_PlayerPadPressed & 0x1100)) {
				if (g_titleSelectionId == 2) g_titleSelectionId = 0;
				g_titleSelectionId++;
			} else {
				g_titleSelectionId--;
				if (g_titleSelectionId == 0) {
					g_titleSelectionId = 2;
					g_titleDemoTime = 0x708;
					goto demo_reset;
				}
			}
			g_titleDemoTime = 0x708;
		}

	demo_reset:
            // Infestation stays on the menu indefinitely instead of entering attract mode.
            if (g_bPlayAsZombie && !g_bDcMode) break;
            g_titleDemoTime--;
            if (g_titleDemoTime != 0) break;

            g_titleOptionsFading = 3;
            g_fade_type_id = 2;
            g_fading_counter = 0x400;
            fade_update();
            return;

        case 3:
            if (g_fading_state < 0) {
                g_titleSelectionId = 0;
                title_exit_loop();
                return;
            }
            UpdateTitleTextSprite(128, g_titleSelectionId);
            if ((g_PlayerPadPressed & 0xeff) == 0) {
                return;
            }
            g_titleOptionsFading = 0;
            g_fading_counter = 0xF000;
            return;

        case 4:
            if (g_fading_state < 0) {
                title_exit_loop();
                g_main_state_flags &= MSF_SCREEN_MODE_MASK;
                return;
            }
            UpdateTitleTextSprite(128, g_titleSelectionId);

        case 6:
            if (g_fading_state < 0) {
                g_titleOptionsFading = 7;
                g_fade_type_id = 1;
                g_fading_counter = 0xC000;
                fade_update();
                UpdateTitleTextSprite(128, g_titleSelectionId);
                return;
            }

        case 8:
            if (g_fading_state < 0) {
                g_titleOptionsFading = 9;
                g_fade_type_id = 1;
                g_fading_counter = 0xF800;
                fade_update();
                UpdateTitleTextSprite(128, g_titleSelectionId);
                return;
            }
            break;

        case 7:
            if (g_fading_state < 0) {
                g_titleOptionsFading = 8;
                g_fade_type_id = 1;
                g_fading_counter = 0x8000;
                fade_update();
                UpdateTitleTextSprite(0x80, g_titleSelectionId);
                return;
            }

        case 9:
            if (g_fading_state < 0) {
                // Hold the title after its flashes until both lines finish.
                // Waiting after the final fade lets the standalone renderer
                // draw the eye again while the suffix is still playing.
                if (s_infestedVoicePhase != 0) {
                    UpdateTitleTextSprite(0x80, g_titleSelectionId);
                    return;
                }
                g_titleOptionsFading = 4;
                g_fade_type_id = 2;
                g_fading_counter = 0x270;
                fade_update();
                UpdateTitleTextSprite(0x80, g_titleSelectionId);
                return;
            }

        default:
            break;
        }

        UpdateTitleTextSprite(0x80, g_titleSelectionId);
        return;
    }

    switch (g_titleOptionsFading) {
        case 0:
            g_titleOptionsFading = 1;
            g_titleDemoTime = 0x80;
            goto option_selected;
        case 1:
    option_selected:
            g_titleDemoTime -= 4;
            UpdateTitleTextSprite(-0x80 - (char)g_titleDemoTime, 0);
            if (g_titleDemoTime == 0) {
                g_titleOptionsFading = 2;
                g_titleDemoTime = 0x708;
            }
            if ((g_PlayerPadPressed & 0xeff) || sidewinderPress) {
                g_titleOptionsFading = 2;
                g_titleDemoTime = 0x708;
            }
            break;

        case 2:
            // Keep PRESS START responsive, but do not time out into a demo.
            if (!(g_bPlayAsZombie && !g_bDcMode)) g_titleDemoTime--;
            UpdateTitleTextSprite(0x80, 0);
            if (g_titleDemoTime == 0) {
                g_titleOptionsFading = 3;
                g_fade_type_id = 2;
                g_fading_counter = 0x400;
                fade_update();
            }
            if ((g_PlayerPadPressed & 0xeff) || sidewinderPress) {
                g_titleMode = 1;
                g_titleOptionsFading = 2;
            }
            break;

        case 3:
            if (g_fading_state > 0x7B80) {
                g_fading_state = 0x7FFF;
                g_fading_counter = 0;
                g_titleSelectionId = 0;
                g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;
                title_exit_loop();
            }
            if ((g_PlayerPadPressed & 0xeff) || sidewinderPress) {
                g_titleOptionsFading = 2;
                g_fading_state = -1;
                g_titleDemoTime = 0x708;
            }
            UpdateTitleTextSprite(0x80, 0);
            break;

        case 4:
            UpdateTitleTextSprite(0x80, 0);
            if (g_fading_state > 0x7B80) {
                g_titleMode = 1;
                g_titleOptionsFading = 0;
                g_fading_state = 0x7FFF;
                g_fading_counter = 0;
                g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;
            }
            break;
    }
}

// ============================================================================
// title_state (0x00430470)
// Title screen state: displays title, waits for player selection,
// then chains to game_start, logos_state, or characterSelectionScreen.
// ============================================================================
void title_state(void)
{
    int i;

	g_main_state_flags &= ~MSF_INTENSITY_RAMP;
	g_PlayerPadHeldPrev = 0;
	g_PlayerPadHeld = 0;
	g_RawPadHeld = 0;
	g_PlayerPadPressed = 0;
	g_playingGameFlag = 0;
	g_menu_choice_id = 0;

    setMenuScreenOffset(320, 240, 0, 0, 0);
    CenterScreenOrigin();
    clear_textures();
    set_scene_render_param(0xc0);
    sounds_reset();

    g_loadDataDestPointer = g_DataBuffer;
    // The USA title_state (0x00430470) loads bank 12 (EVIL01 - the "Resident
    // Evil" voice), the Japanese one (FUN_0048dc20, 0x0048dc20 in
    // Biohazard.exe) loads bank 11 (BIO01 - the "Bio Hazard" voice). Both banks
    // have slot 0 as the logo voice and 13/14/15 as Cancel/Type01/Type02, so the
    // play_sfx ids in update_title_options are unchanged; only the bank differs.
    LoadSoundBank(GetAssetVersion() != 0 ? BANK_BIO : BANK_TITLE, g_DataBuffer);
    if (g_bPlayAsZombie && !g_bDcMode) title_load_infested();
    if (g_bPlayAsZombie && !g_bDcMode) load_character_sfx(0);

    g_fading_state = -1;
    g_titleLoopFlag = 0;
    g_SpecialRoomLightState = 0xFFFF;
    g_titleMode = 0;
    g_titleOptionsFading = 0;

    init_title_screen();

    // empty_00497c10(0);
    // empty_0040abb0((void*)0, 0, 0, 0);

    g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;
    // 0x0047b950 call site: the original calls 0x00483510 here, a stub that
    // just returns 0 - call dropped

    setMenuScreenOffset(320, 240, 0, 0, 1);
    Task_sleep(1);

    if (g_fmvPlayCount < 1) {
        g_fmvPlayCount = 0x10;
        // Port-added mod: no opening movie (OU.avi) in play-as-zombie.
        if (!zombie_mode_skip_intro()) {
            g_selectedFmvId = 0;
            g_fmvDataPointer = g_loadDataDestPointer;
            g_main_state_flags = g_main_state_flags | MSF_FMV_REQUEST;
            Task_sleep(1);
        }
    }

    g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_REBUILD;
    Task_sleep(1);

    do {
        update_title_options();
        Task_sleep(1);
    } while (g_titleLoopFlag != 0);

    // legacy gpu wait
    if (g_GPU_VENDOR_ID == 1) {
        for (i = 180; i != 0; i--) {
            Task_sleep(1);
        }
    }

    cleanup_texture_slot(12);
    if (s_infestedBank) destroySndBank(s_infestedBank);
    s_infestedBank = 0;
    s_infestedVoicePhase = 0;
    if (g_bPlayAsZombie && !g_bDcMode) {
        cleanup_texture_slot(14);
        cleanup_texture_slot(15);
        cleanup_texture_slot(16);
        cleanup_texture_slot(17);
    }

    // DC: the difficulty submenu leaves the choice at 3..5 (and 6 for ADVANCED*
    // while the green cell is drawn). The PS1's title_state groups 1 and 3..6
    // together - chain the character select and let it start the game, exactly
    // like NEW GAME. Without this the choice 3 would fall into the USA build's
    // load-game case below.
    if (g_bDcMode && g_titleSelectionId >= 3) {
        nullsub_0047eb80();
        Task_chain((void*)characterSelectionScreen);
        return;
    }

    switch (g_titleSelectionId) {
    case 0:
        nullsub_0047eb80();
        g_main_state_flags2 |= MSF2_ATTRACT_DEMO;
        Task_chain((void*)game_start);
        Task_chain((void*)logos_state);
        return;

    case 1:
        nullsub_0047eb80();
        // Port-added mod: NEW GAME goes through the zombie mod's lobby
        // (single player / host / join) first; it chains the character select.
        if (g_bPlayAsZombie && !g_bDcMode) {
            Task_chain((void*)zombie_lobby_state);
            return;
        }
        Task_chain((void*)characterSelectionScreen);

        // The character select starts the game itself (CharacterSelectionScreen
        // chains game_start once the player confirms a character), so the DC
        // path stops here. Falling into the load screen below would show it for
        // a NEW GAME, and that screen's exit option chains back to title_state -
        // which is what made the DC difficulty confirm bounce back to the
        // NEW GAME / LOAD GAME menu.
        //
        // On the PS1 the new-game branch chains the SELECT overlay and then
        // calls its save-state loader; that loader is NOT the interactive
        // screen, but the port only has the interactive one, so it must not run
        // for a new game.
        if (g_bDcMode) {
            return;
        }

    case 2:
    case 3:
        g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;
        LoadSaveGameState(1, 0x80180000, 0, 1, 0);
        g_loadSaveStateFlag = 0;
        Game_timer = g_gameTimerSnapshot;
        g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;
        nullsub_0047eb80();
        Task_chain((void*)game_start);

    default:
        return;
    }
}

// nullsub_0047eb80 - empty no-op in the original PC build.
// likely a PS1 version function stripped during the PC port.
void nullsub_0047eb80(void) { }
