// Standalone CPU checks; never initializes or launches the game.
// g++ -std=c++17 tests/test_zombie_portraits.cpp src/game/mods/ZombiePortraitImage.cpp -o test_portraits
// test_portraits <USA directory> [preview.ppm]
#include "../src/game/mods/ZombiePortraitImage.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

static std::vector<unsigned char> read(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    assert(size > 0);
    rewind(f);
    std::vector<unsigned char> data((size_t)size);
    assert(fread(data.data(), 1, data.size(), f) == data.size());
    fclose(f);
    return data;
}

int main(int argc, char** argv)
{
    assert(argc >= 2);
    unsigned char face[30 * 30 * 4], preview[90 * 30 * 4];
    unsigned char solid[4] = { 43, 91, 137, 255 };
    assert(zm_portrait_crop(solid, 1, 1, 0, 0, 1, 1, face));
    for (int i = 0; i < 900; i++) assert(memcmp(face + i * 4, solid, 4) == 0);
    assert(!zm_portrait_crop(solid, 1, 1, -1, 0, 1, 1, face));
    assert(!zm_portrait_crop(solid, 1, 1, 1, 0, 1, 1, face));
    assert(!zm_portrait_crop(solid, 1, 1, 0, 0, 0, 1, face));
    assert(!zm_portrait_model(NULL, 0, face));

    const char* models[] = { "/Enemy/char12.emd", "/Enemy/em1027.emd", "/Enemy/em1028.emd" };
    for (int model = 0; model < 3; model++) {
        std::vector<unsigned char> data = read(std::string(argv[1]) + models[model]);
        assert(zm_portrait_model(data.data(), (unsigned int)data.size(), face));
        int foreground = 0;
        for (int i = 0; i < 900; i++) {
            assert(face[i * 4 + 3] == 255);
            if (face[i * 4] != 8 || face[i * 4 + 1] != 8 || face[i * 4 + 2] != 48) foreground++;
        }
        assert(foreground > 250 && foreground < 850);
        for (int y = 0; y < 30; y++) memcpy(preview + (y * 90 + model * 30) * 4, face + y * 30 * 4, 30 * 4);
        // Truncation and corrupt relative pointers must fail cleanly.
        for (unsigned int size = 0; size < 100; size++) assert(!zm_portrait_model(data.data(), size, face));
        std::vector<unsigned char> bad = data;
        memset(bad.data() + bad.size() - 8, 0xff, 4);
        assert(!zm_portrait_model(bad.data(), (unsigned int)bad.size(), face));
        unsigned int off = data[data.size() - 8] | (data[data.size() - 7] << 8) |
                           (data[data.size() - 6] << 16) | ((unsigned int)data[data.size() - 5] << 24);
        bad = data;
        memset(bad.data() + off + 44, 0xff, 4); // head vertex count
        assert(!zm_portrait_model(bad.data(), (unsigned int)bad.size(), face));
        bad = data;
        memset(bad.data() + bad.size() - 4, 0xff, 4); // texture offset
        assert(!zm_portrait_model(bad.data(), (unsigned int)bad.size(), face));
    }
    std::vector<unsigned char> sheet = read(std::string(argv[1]) + "/Data/Statface.tim");
    unsigned char original[64 * 64 * 4];
    assert(zm_portrait_sheet(sheet.data(), (unsigned int)sheet.size(), original));
    assert(!zm_portrait_sheet(sheet.data(), 32, original));
    if (argc >= 3) {
        FILE* f = fopen(argv[2], "wb");
        assert(f);
        fputs("P6\n90 30\n255\n", f);
        for (int i = 0; i < 90 * 30; i++) fwrite(preview + i * 4, 1, 3, f);
        fclose(f);
    }
    puts("Portrait checks passed: model heads, alpha, crop, sheet, invalid/truncated assets.");
    return 0;
}
