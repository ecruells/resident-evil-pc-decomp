// Offline adapter; tools/evaluate_zombie_routes.py inserts production code at
// the markers below. Never links or starts the game.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <string>
#include <fstream>
#include <filesystem>
#include <stdexcept>
namespace fs = std::filesystem;
static fs::path assetRoot;
#define GAME_DATA_ROOT ""
// @production-definitions
static_assert(sizeof(unsigned int) == 4 && sizeof(short) == 2, "RE1 data widths");
static size_t LoadFile(const char* name, unsigned char* dst, int) {
    std::string relative(name);
    for (char& c : relative) if (c == '\\') c = '/';
    fs::path path = assetRoot;
    for (const auto& part : fs::path(relative)) {
        std::string wanted = part.string();
        for (char& c : wanted) c = (char)std::tolower((unsigned char)c);
        bool found = false;
        for (const auto& entry : fs::directory_iterator(path)) {
            std::string actual = entry.path().filename().string();
            for (char& c : actual) c = (char)std::tolower((unsigned char)c);
            if (actual == wanted) { path = entry.path(); found = true; break; }
        }
        if (!found) throw std::runtime_error("Missing asset: " + relative);
    }
    const auto size = fs::file_size(path);
    if (size > 700 * 1024) throw std::runtime_error("Oversized RDT: " + relative);
    std::ifstream file(path, std::ios::binary);
    if (!file.read((char*)dst, (std::streamsize)size))
        throw std::runtime_error("Unreadable RDT: " + relative);
    return (size_t)size;
}
// @production-generator
static_assert(ROOM_MAIN_HALL == 0x06 && ROOM_STOREROOM == 0x1B &&
              RND_STAGE_1F == 5 && RND_STAGE_2F == 6 && RND_ROOMS == 0x1D,
              "Update the Python route endpoints for changed production constants");
static_assert(ITEM_SWORD_KEY == 0x33 && ITEM_ARMOR_KEY == 0x34 &&
              ITEM_SHIELD_KEY == 0x35 && ITEM_HELMET_KEY == 0x36 &&
              ITEM_WIND_CREST == 0x29 && ITEM_MOON_CREST == 0x2C &&
              ITEM_STAR_CREST == 0x2D && ITEM_SUN_CREST == 0x2E,
              "Update Python item identifiers for changed production constants");
static void generate(unsigned int seed) {
    if (!seed) seed = 0x5EED1234;
    s_doorCount = s_spotCount = s_lockCount = s_unreviewedGates = 0;
    memset(s_roomInfo, 0, sizeof(s_roomInfo));
    memset(s_usedFlags, 0, sizeof(s_usedFlags));
    for (int st = RND_STAGE_1F; st <= RND_STAGE_2F; ++st)
        for (int r = 0; r < RND_ROOMS; ++r) rnd_read_room(st, r);
    s_curInfo = NULL;
    for (int i = 0; i < s_doorCount; ++i) {
        const RndDoor& d = s_doors[i];
        unsigned char flag = d.lock & 0x3F;
        if (rnd_door_key_lock(d) && rnd_lock_index(flag) < 0 && s_lockCount < RND_MAX_LOCKS)
            s_lockFlag[s_lockCount++] = flag;
    }
    if (!s_roomInfo[0][ROOM_MAIN_HALL].exists || !s_roomInfo[0][ROOM_STOREROOM].exists || !s_lockCount)
        throw std::runtime_error("Incomplete mansion graph");
    s_rng = seed;
    rnd_add_new_spots();
    bool ok = false;
    int attempts = 0;
    while (attempts < 200 && !ok) { ++attempts; ok = rnd_generate_once(); }
    printf("{\"seed\":%u,\"generated\":%s,\"attempts\":%d,\"unreviewed_gates\":%d",
           seed, ok ? "true" : "false", attempts, s_unreviewedGates);
    printf(",\"gated_doors\":[");
    for (int i = 0, n = 0; i < s_doorCount; ++i) {
        const RndDoor& d = s_doors[i];
        if (!d.gated) continue;
        printf("%s[%d,%d,%d,%d,%d,%s]", n++ ? "," : "", d.fromStage, d.fromRoom, d.toStage, d.toRoom,
               d.slot, d.usable ? "true" : "false");
    }
    printf("]");
    if (ok) {
        printf(",\"doors\":[");
        for (int i = 0, n = 0; i < s_doorCount; ++i) {
            const RndDoor& d = s_doors[i];
            // A dead or puzzle-gated trigger is no edge.
            if (!d.usable) continue;
            int requirement = 0;
            if ((d.lock & 0x80) && (d.lock & 0x3F) == RND_CREST_LOCK) requirement = 0xF0;
            else if (rnd_door_key_lock(d)) {
                int li = rnd_lock_index(d.lock & 0x3F);
                requirement = rnd_item_bit(li >= 0 ? s_lockKey[li] : d.need);
            }
            requirement |= (int)d.access;     // the battery's elevator, the note's keypad door
            printf("%s[%d,%d,%d,%d,%d,%d]", n++ ? "," : "", d.fromStage, d.fromRoom, d.toStage, d.toRoom,
                   requirement, d.slot);
        }
        printf("],\"locks\":[");
        for (int i = 0; i < s_lockCount; ++i)
            printf("%s[%d,%d]", i ? "," : "", s_lockFlag[i], s_lockKey[i]);
        printf("],\"tools\":[");
        bool firstTool = true;
        for (int i = 0; i < s_spotCount; ++i) {
            const RndSpot& s = s_spots[i];
            if (!s.pool || (s.id != ITEM_BROKEN_SHOTGUN && s.id != ITEM_PICK_AXE &&
                            s.id != ITEM_MUSIC_NOTES && s.id != ITEM_CHEMICAL)) continue;
            printf("%s[%d,%d,%d,%d,%d,%d]", firstTool ? "" : ",", s.stage, s.room, s.id, s.flag, s.x, s.z);
            firstTool = false;
        }
        printf("],\"puzzles\":[");
        for (int p = 0; p < RND_PUZZLES; ++p)
            printf("%s[%d,%d,%d,%d]", p ? "," : "", kPuzzles[p].stage, kPuzzles[p].room,
                   s_puzzleCost[p], s_puzzleWeapon[p]);
        printf("],\"leaves\":[");
        for (int r = 0, n = 0; r < RND_ROOMS * 2; ++r)
            if (rnd_key_item_room(r < RND_ROOMS ? RND_STAGE_1F : RND_STAGE_2F, r % RND_ROOMS))
                printf("%s[%d,%d]", n++ ? "," : "", r < RND_ROOMS ? RND_STAGE_1F : RND_STAGE_2F, r % RND_ROOMS);
        printf("],\"items\":[");
        bool first = true;
        for (int i = 0; i < s_spotCount; ++i) {
            const RndSpot& s = s_spots[i];
            // rnd_place_weapons replaces non-pool original keys/crests with
            // supplies. Only pool progression belongs to the final scenario.
            if (!s.pool || !rnd_item_bit(s.id)) continue;
            printf("%s[%d,%d,%d,%d,%d,%d]", first ? "" : ",", s.stage, s.room, s.id, s.flag, s.x, s.z);
            first = false;
        }
        printf("],\"proof_only_items\":[");
        first = true;
        for (int i = 0; i < s_spotCount; ++i) {
            const RndSpot& s = s_spots[i];
            if (s.pool || !rnd_item_bit(s.id)) continue;
            printf("%s[%d,%d,%d,%d]", first ? "" : ",", s.stage, s.room, s.id, s.flag);
            first = false;
        }
        printf("]");
    }
    puts("}");
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Expected USA asset directory");
        assetRoot = fs::absolute(argv[1]);
        unsigned int seed;
        while (scanf("%u", &seed) == 1) generate(seed);
        return 0;
    } catch (const std::exception& e) {
        fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
