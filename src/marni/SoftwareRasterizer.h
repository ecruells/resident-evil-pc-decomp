// Port-added CPU rasterizer for platforms without desktop OpenGL.
// RGBA8 storage, top-left screen origin, normalized LESS_EQUAL depth.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace re1sw
{
struct Color
{
    float r, g, b, a;
};
struct Vertex
{
    float x, y, z, w, u, v;
    Color color;
};
struct Texture
{
    int width = 0, height = 0;
    std::vector<uint8_t> pixels;
    Color at(int x, int y) const
    {
        x = std::max(0, std::min(width - 1, x));
        y = std::max(0, std::min(height - 1, y));
        const uint8_t *p = &pixels[(y * width + x) * 4];
        return {p[0] / 255.f, p[1] / 255.f, p[2] / 255.f, p[3] / 255.f};
    }
    Color sample(float u, float v, bool linear) const
    {
        if (pixels.empty())
            return {1, 1, 1, 1};
        u = std::max(0.f, std::min(1.f, u));
        v = std::max(0.f, std::min(1.f, v));
        if (!linear)
            return at(int(u * width), int(v * height));
        float x = u * width - .5f, y = v * height - .5f;
        int ix = int(std::floor(x)), iy = int(std::floor(y));
        float fx = x - ix, fy = y - iy;
        Color c[4] = {at(ix, iy), at(ix + 1, iy), at(ix, iy + 1), at(ix + 1, iy + 1)};
        float weights[4] = {(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy};
        Color out = {};
        for (int i = 0; i < 4; ++i)
        {
            out.r += c[i].r * weights[i];
            out.g += c[i].g * weights[i];
            out.b += c[i].b * weights[i];
            out.a += c[i].a * weights[i];
        }
        return out;
    }
};
inline uint8_t byte(float f)
{
    return uint8_t(std::max(0.f, std::min(255.f, f * 255.f + .5f)));
}

class Surface
{
  public:
    int width, height;
    std::vector<uint8_t> pixels;
    std::vector<float> depth;
    Surface(int w, int h) : width(w), height(h), pixels(w * h * 4), depth(w * h, 1.f) {}
    void clear(Color c)
    {
        for (size_t i = 0; i < pixels.size(); i += 4)
        {
            pixels[i] = byte(c.r);
            pixels[i + 1] = byte(c.g);
            pixels[i + 2] = byte(c.b);
            pixels[i + 3] = byte(c.a);
        }
        std::fill(depth.begin(), depth.end(), 1.f);
    }
    // Blend values match MarniBlend; alpha uses the same separate blend as GL.
    void fragment(int x, int y, float z, Color c, int blend, bool test, bool write)
    {
        size_t index = y * width + x;
        if (test && (z < 0.f || z > 1.f || z > depth[index]))
            return;
        uint8_t *p = &pixels[index * 4];
        float src = 1.f, dst = 0.f;
        if (blend == 0)
        {
            src = c.a;
            dst = 1.f - c.a;
        }
        else if (blend == 1)
            dst = 1.f;
        else if (blend == 3)
        {
            src = c.a;
            dst = 1.f;
        }
        p[0] = byte(c.r * src + p[0] / 255.f * dst);
        p[1] = byte(c.g * src + p[1] / 255.f * dst);
        p[2] = byte(c.b * src + p[2] / 255.f * dst);
        p[3] = byte(blend == 2 ? c.a : c.a + p[3] / 255.f * (blend == 1 ? 1.f : 1.f - c.a));
        if (write)
            depth[index] = z;
    }
    void sprite(float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                Color tint, const Texture &tex, bool linear, int blend)
    {
        if (!(w > 0 && h > 0))
            return;
        int l = int(std::max(0.f, std::min(float(width), std::ceil(x - .5f))));
        int r = int(std::max(0.f, std::min(float(width), std::ceil(x + w - .5f))));
        int t = int(std::max(0.f, std::min(float(height), std::ceil(y - .5f))));
        int b = int(std::max(0.f, std::min(float(height), std::ceil(y + h - .5f))));
        for (int py = t; py < b; ++py)
            for (int px = l; px < r; ++px)
            {
                Color c = tex.sample(u0 + (u1 - u0) * (px + .5f - x) / w,
                                     v0 + (v1 - v0) * (py + .5f - y) / h, linear);
                c.r *= tint.r;
                c.g *= tint.g;
                c.b *= tint.b;
                c.a *= tint.a;
                fragment(px, py, 0, c, blend, false, false);
            }
    }
    static float edge(const Vertex &a, const Vertex &b, float x, float y)
    {
        return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
    }
    static bool topLeft(const Vertex &a, const Vertex &b)
    {
        return b.y < a.y || (b.y == a.y && b.x > a.x);
    }
    void triangle(Vertex a, Vertex b, Vertex c, const Texture &tex, bool linear, int blend,
                  bool perspective, bool test, bool write)
    {
        float area = edge(a, b, c.x, c.y);
        if (!std::isfinite(area) || std::fabs(area) < 1e-8f)
            return;
        if (area < 0)
        {
            std::swap(b, c);
            area = -area;
        }
        int l = int(std::max(0.f, std::min(float(width), std::floor(std::min({a.x, b.x, c.x})))));
        int r = int(std::max(0.f, std::min(float(width), std::ceil(std::max({a.x, b.x, c.x})))));
        int t = int(std::max(0.f, std::min(float(height), std::floor(std::min({a.y, b.y, c.y})))));
        int bottom =
            int(std::max(0.f, std::min(float(height), std::ceil(std::max({a.y, b.y, c.y})))));
        const float qa = 1.f / std::max(a.w, 1e-4f), qb = 1.f / std::max(b.w, 1e-4f),
                    qc = 1.f / std::max(c.w, 1e-4f);
        for (int y = t; y < bottom; ++y)
            for (int x = l; x < r; ++x)
            {
                float ea = edge(b, c, x + .5f, y + .5f), eb = edge(c, a, x + .5f, y + .5f),
                      ec = edge(a, b, x + .5f, y + .5f);
                if (ea < 0 || (ea == 0 && !topLeft(b, c)) || eb < 0 ||
                    (eb == 0 && !topLeft(c, a)) || ec < 0 || (ec == 0 && !topLeft(a, b)))
                    continue;
                float wa = ea / area, wb = eb / area, wc = ec / area;
                float z = wa * a.z + wb * b.z + wc * c.z;
                if (perspective)
                {
                    wa *= qa;
                    wb *= qb;
                    wc *= qc;
                    float sum = wa + wb + wc;
                    wa /= sum;
                    wb /= sum;
                    wc /= sum;
                }
                Color color = tex.sample(wa * a.u + wb * b.u + wc * c.u,
                                         wa * a.v + wb * b.v + wc * c.v, linear);
                color.r *= wa * a.color.r + wb * b.color.r + wc * c.color.r;
                color.g *= wa * a.color.g + wb * b.color.g + wc * c.color.g;
                color.b *= wa * a.color.b + wb * b.color.b + wc * c.color.b;
                color.a *= wa * a.color.a + wb * b.color.a + wc * c.color.a;
                fragment(x, y, z, color, blend, test, write);
            }
    }
};
} // namespace re1sw
