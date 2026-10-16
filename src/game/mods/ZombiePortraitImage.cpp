// Runtime portraits from the player's own assets, without borrowing live entities.
#include "ZombiePortraitImage.h"
#include <cmath>
#include <cstring>
#include <vector>
#include <algorithm>

namespace {
struct Bytes {
    const unsigned char* p;
    unsigned int size;
    bool has(unsigned int off, unsigned int count) const {
        return off <= size && count <= size - off;
    }
    unsigned int u16(unsigned int off) const { return p[off] | (p[off + 1] << 8); }
    int s16(unsigned int off) const { return (short)u16(off); }
    unsigned int u32(unsigned int off) const {
        return u16(off) | (u16(off + 2) << 16);
    }
};
struct Tim {
    Bytes data;
    unsigned int palette, pixels;
    int cx, cy, cw, ch, x, y, width, height;
    bool open(Bytes bytes, unsigned int off) {
        data = bytes;
        if (!data.has(off, 20) || data.u32(off) != 16 || data.u32(off + 4) != 9) return false;
        unsigned int len = data.u32(off + 8);
        if (len < 12 || !data.has(off + 8, len)) return false;
        palette = off + 20;
        cx = data.u16(off + 12); cy = data.u16(off + 14);
        cw = data.u16(off + 16); ch = data.u16(off + 18);
        if (!cw || !ch || (unsigned int)cw * (unsigned int)ch > (len - 12) / 2) return false;
        off += 8 + len;
        if (!data.has(off, 12)) return false;
        len = data.u32(off);
        if (len < 12 || !data.has(off, len)) return false;
        pixels = off + 12;
        x = data.u16(off + 4); y = data.u16(off + 6);
        width = data.u16(off + 8) * 2; height = data.u16(off + 10);
        if (!width || !height || (unsigned int)width > (len - 12) / (unsigned int)height) return false;
        return true;
    }
    bool sample(int u, int v, unsigned int clut, unsigned char* rgba) const {
        int px = (int)(clut & 63) * 16 - cx;
        int py = (int)(clut >> 6) - cy;
        if (u < 0 || u >= width || v < 0 || v >= height || py < 0 || py >= ch) return false;
        int index = data.p[pixels + v * width + u];
        if (px < 0 || px + index >= cw) return false;
        unsigned int col = data.u16(palette + (py * cw + px + index) * 2);
        rgba[0] = (unsigned char)((col & 31) * 255 / 31);
        rgba[1] = (unsigned char)(((col >> 5) & 31) * 255 / 31);
        rgba[2] = (unsigned char)(((col >> 10) & 31) * 255 / 31);
        rgba[3] = (col & 0x7fff) ? 255 : 0;
        return true;
    }
};
struct Vertex { float x, y, z, u, v; };
}

bool zm_portrait_sheet(const unsigned char* data, unsigned int size, unsigned char* rgba)
{
    if (!data || !rgba) return false;
    Tim tim;
    if (!tim.open({data, size}, 0) || tim.width != 64 || tim.height != 64) return false;
    unsigned int clut = (unsigned int)(tim.cy * 64 + tim.cx / 16);
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++)
        if (!tim.sample(x, y, clut, rgba + (y * 64 + x) * 4)) return false;
    return true;
}

bool zm_portrait_crop(const unsigned char* src, int width, int height,
                      int x, int y, int cropWidth, int cropHeight, unsigned char* dst)
{
    if (!src || !dst || width <= 0 || height <= 0 || x < 0 || y < 0 ||
        cropWidth <= 0 || cropHeight <= 0 || x > width || y > height ||
        cropWidth > width - x || cropHeight > height - y) return false;
    // Box filter gives a useful face at the original inventory's 30x30 size.
    for (int oy = 0; oy < 30; oy++) for (int ox = 0; ox < 30; ox++) {
        int left = x + ox * cropWidth / 30, top = y + oy * cropHeight / 30;
        int right = x + (ox + 1) * cropWidth / 30, bottom = y + (oy + 1) * cropHeight / 30;
        right = std::max(right, left + 1); bottom = std::max(bottom, top + 1);
        unsigned int sum[4] = {};
        for (int sy = top; sy < bottom; sy++) for (int sx = left; sx < right; sx++)
            for (int c = 0; c < 4; c++) sum[c] += src[(sy * width + sx) * 4 + c];
        unsigned int count = (right - left) * (bottom - top);
        for (int c = 0; c < 4; c++) dst[(oy * 30 + ox) * 4 + c] = (unsigned char)(sum[c] / count);
    }
    return true;
}

bool zm_portrait_model(const unsigned char* data, unsigned int size, unsigned char* rgba)
{
    if (!data || !rgba || size < 20) return false;
    Bytes d = {data, size};
    unsigned int model = d.u32(size - 8), texture = d.u32(size - 4);
    // EMD's model has a size word before the ordinary TMD flags/object count.
    if (!d.has(model, 68) || d.u32(model + 4) != 0 || d.u32(model + 8) < 2) return false;
    unsigned int base = model + 12;
    Tim tim;
    if (!tim.open(d, texture) || !d.has(8, 12)) return false;
    struct Mesh { unsigned int vertices, count, packets, packetCount, first; };
    Mesh meshes[2];
    std::vector<Vertex> vertices;
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    float headTop = 1e9f, headBottom = -1e9f;
    const int side = 120;
    const float angle = 15.0f * 3.14159265f / 180.0f;
    const float sn = std::sin(angle), cs = std::cos(angle);
    // Camera slightly above the face, looking down eight degrees.
    const float pitch = 8.0f * 3.14159265f / 180.0f;
    const float sp = std::sin(pitch), cp = std::cos(pitch);
    for (int mesh = 0; mesh < 2; mesh++) {
        unsigned int obj = base + mesh * 28;
        Mesh& m = meshes[mesh];
        m.vertices = d.u32(obj); m.count = d.u32(obj + 4);
        m.packets = d.u32(obj + 16); m.packetCount = d.u32(obj + 20);
        if (!m.count || m.count > 4096 || !m.packetCount || m.packetCount > 4096 ||
            m.vertices > size - base || m.packets > size - base) return false;
        m.vertices += base; m.packets += base;
        if (!d.has(m.vertices, m.count * 8)) return false;
        m.first = (unsigned int)vertices.size();
        // Rest-pose armature: torso joint 0, head joint 1 is its child.
        int tx = d.s16(8), ty = d.s16(10), tz = d.s16(12);
        if (mesh == 1) { tx += d.s16(14); ty += d.s16(16); tz += d.s16(18); }
        for (unsigned int i = 0; i < m.count; i++) {
            float x = (float)(d.s16(m.vertices + i * 8) + tx);
            float y = (float)(d.s16(m.vertices + i * 8 + 2) + ty);
            float z = (float)(d.s16(m.vertices + i * 8 + 4) + tz);
            float viewDepth = x * cs + z * sn;
            Vertex v = { -z * cs + x * sn, y * cp + viewDepth * sp,
                         viewDepth * cp - y * sp, 0, 0 };
            vertices.push_back(v);
            minX = std::min(minX, v.x); maxX = std::max(maxX, v.x);
            if (mesh == 1) { headTop = std::min(headTop, v.y); headBottom = std::max(headBottom, v.y); }
        }
    }
    // Head-and-shoulders framing: crop the torso's width rather than shrinking
    // the face to fit it, leaving just the shoulders beneath the chin.
    minY = headTop;
    maxY = headTop + (headBottom - headTop) * 1.3f;
    float span = maxY - minY;
    if (span < 1) return false;
    float scale = (side - 12) / span;
    for (unsigned int i = 0; i < vertices.size(); i++) {
        vertices[i].x = (vertices[i].x - (minX + maxX) * .5f) * scale + side * .5f;
        vertices[i].y = (vertices[i].y - (minY + maxY) * .5f) * scale + side * .5f;
    }
    std::vector<unsigned char> pixels(side * side * 4);
    std::vector<float> depth(side * side, -1e9f);
    for (int i = 0; i < side * side; i++) {
        pixels[i * 4] = 8; pixels[i * 4 + 1] = 8;
        pixels[i * 4 + 2] = 48; pixels[i * 4 + 3] = 255;
    }
    int drawn = 0;
    for (int mesh = 0; mesh < 2; mesh++) {
        unsigned int po = meshes[mesh].packets;
        for (unsigned int p = 0; p < meshes[mesh].packetCount; p++) {
            // The shipped torso/head meshes use textured Gouraud triangles (GT3).
            // Validate this explicitly rather than interpreting another packet layout.
            if (!d.has(po, 28) || d.u32(po) != 0x34000609) return false;
            unsigned int clut = d.u16(po + 6), tpage = d.u16(po + 10);
            if (((tpage >> 7) & 3) != 1) return false;
            Vertex v[3];
            for (int i = 0; i < 3; i++) {
                unsigned int index = d.u16(po + 18 + i * 4);
                if (index >= meshes[mesh].count) return false;
                v[i] = vertices[meshes[mesh].first + index];
                v[i].u = (float)d.p[po + 4 + i * 4] + ((int)(tpage & 15) * 64 - tim.x) * 2;
                v[i].v = (float)d.p[po + 5 + i * 4] + (int)(tpage & 16) * 16 - tim.y;
            }
            po += 28;
            float den = (v[1].y - v[2].y) * (v[0].x - v[2].x) + (v[2].x - v[1].x) * (v[0].y - v[2].y);
            if (std::fabs(den) < .00001f) continue;
            int left = std::max(0, (int)std::floor(std::min(v[0].x, std::min(v[1].x, v[2].x))));
            int right = std::min(side - 1, (int)std::ceil(std::max(v[0].x, std::max(v[1].x, v[2].x))));
            int top = std::max(0, (int)std::floor(std::min(v[0].y, std::min(v[1].y, v[2].y))));
            int bottom = std::min(side - 1, (int)std::ceil(std::max(v[0].y, std::max(v[1].y, v[2].y))));
            for (int y = top; y <= bottom; y++) for (int x = left; x <= right; x++) {
                float w[3];
                w[0] = ((v[1].y - v[2].y) * (x + .5f - v[2].x) + (v[2].x - v[1].x) * (y + .5f - v[2].y)) / den;
                w[1] = ((v[2].y - v[0].y) * (x + .5f - v[2].x) + (v[0].x - v[2].x) * (y + .5f - v[2].y)) / den;
                w[2] = 1 - w[0] - w[1];
                if (w[0] < 0 || w[1] < 0 || w[2] < 0) continue;
                float z = 0, u = 0, tv = 0;
                for (int i = 0; i < 3; i++) { z += w[i] * v[i].z; u += w[i] * v[i].u; tv += w[i] * v[i].v; }
                int dest = y * side + x;
                if (z < depth[dest]) continue;
                unsigned char color[4];
                if (!tim.sample((int)u, (int)tv, clut, color)) return false;
                if (!color[3]) continue;
                memcpy(&pixels[dest * 4], color, 4);
                depth[dest] = z;
                drawn++;
            }
        }
    }
    return drawn > 0 && zm_portrait_crop(pixels.data(), side, side, 0, 0, side, side, rgba);
}
