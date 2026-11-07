#include "marni/SoftwareRasterizer.h"
#include <cassert>
#include <cstdio>
using namespace re1sw;
static Vertex v(float x, float y, Color c, float z = .5f, float w = 1.f, float u = 0.f,
                float vv = 0.f)
{
    return {x, y, z, w, u, vv, c};
}
int main()
{
    Texture white;
    Surface s(8, 8);
    Color red{1, 0, 0, 1}, green{0, 1, 0, 1}, blue{0, 0, 1, 1};
    s.clear({0, 0, 0, 1});
    // Two triangles forming a rectangle must cover every pixel once.
    Color half{1, 0, 0, .5};
    s.triangle(v(0, 0, half), v(8, 0, half), v(0, 8, half), white, false, 0, false, false, false);
    s.triangle(v(0, 8, half), v(8, 0, half), v(8, 8, half), white, false, 0, false, false, false);
    for (size_t i = 0; i < s.pixels.size(); i += 4)
        assert(s.pixels[i] == 128);
    // Near geometry survives a later far draw; translucent test-only geometry
    // must not write depth and hide subsequent objects.
    s.clear({0, 0, 0, 1});
    s.triangle(v(-1, -1, red, .2), v(20, -1, red, .2), v(-1, 20, red, .2), white, false, 2, true,
               true, true);
    s.triangle(v(-1, -1, green, .8), v(20, -1, green, .8), v(-1, 20, green, .8), white, false, 2,
               true, true, true);
    assert(s.pixels[0] == 255 && s.pixels[1] == 0 && s.depth[0] == .2f);
    s.triangle(v(-1, -1, blue, .1), v(20, -1, blue, .1), v(-1, 20, blue, .1), white, false, 2, true,
               true, false);
    assert(s.pixels[2] == 255 && s.depth[0] == .2f);
    s.clear({0, 0, 0, 1});
    Texture tex;
    tex.width = 2;
    tex.height = 2;
    tex.pixels = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    Color c = tex.sample(.5, .5, true);
    assert(c.r == .5f && c.g == .5f && c.b == .5f);
    assert(tex.sample(-1, -1, false).r == 1 && tex.sample(2, 2, false).r == 1);
    // Vertex B's w=10 makes its screen-space UV contribute less.
    s.triangle(v(0, 0, {1, 1, 1, 1}, .5, 1, 0, 0), v(8, 0, {1, 1, 1, 1}, .5, 10, 1, 0),
               v(0, 8, {1, 1, 1, 1}, .5, 1, 0, 0), tex, false, 2, true, false, false);
    assert(s.pixels[4 * 4] == 255 && s.pixels[4 * 4 + 1] == 0);
    s.clear({0, 0, 0, 1});
    s.triangle(v(0, 0, {1, 1, 1, 1}, .5, 1, 0, 0), v(8, 0, {1, 1, 1, 1}, .5, 10, 1, 0),
               v(0, 8, {1, 1, 1, 1}, .5, 1, 0, 0), tex, false, 2, false, false, false);
    assert(s.pixels[4 * 4] == 0 && s.pixels[4 * 4 + 1] == 255);
    // Clipped sprites preserve UV origin and do not overwrite adjacent pixels.
    s.clear({0, 0, 0, 1});
    s.sprite(-4, 0, 8, 4, 0, 0, 1, 1, {1, 1, 1, 1}, tex, false, 2);
    assert(s.pixels[0] == 0 && s.pixels[1] == 255 && s.pixels[4 * 4] == 0);
    // Additive and alpha-additive are distinct, and saturate.
    s.fragment(0, 0, 0, {.5f, 0, 0, .5f}, 3, false, false);
    assert(s.pixels[0] == 64 && s.pixels[1] == 255);
    s.fragment(0, 0, 0, {1, 0, 0, .5f}, 1, false, false);
    assert(s.pixels[0] == 255);
    puts("PASS rasterizer: shared edges, depth, read-only depth, perspective, filtering, clipping, "
         "blending");
}
