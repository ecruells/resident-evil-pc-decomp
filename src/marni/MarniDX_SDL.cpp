// Port-added SDL2 presenter + CPU rasterizer. PS2's SDL2 driver presents the
// 320x240 framebuffer through gsKit; depth and perspective live on the EE.
#include "../game/Types.h"
#include "MarniDX.h"
#include "SoftwareRasterizer.h"
#include <SDL2/SDL.h>
#include <cstdlib>
#include <cstring>
#include <new>

struct MarniDX::Impl
{
    SDL_Renderer *renderer = nullptr;
    SDL_Texture *frame = nullptr;
    re1sw::Surface surface{320, 240};
    struct Slot
    {
        re1sw::Texture texture;
        unsigned serial = 0;
    } slots[2048];
    MarniHandle white = 0, font = 0;
    int fontW = 0, fontH = 0;
    size_t textureBytes = 0;
    static constexpr size_t budget = 8 * 1024 * 1024;
    bool valid(MarniHandle h) const
    {
        return h > 0 && h < 2048 && !slots[h].texture.pixels.empty();
    }
    const re1sw::Texture &texture(MarniHandle h) const
    {
        return slots[valid(h) ? h : white].texture;
    }
    bool upload(MarniHandle h, int w, int height, int bpp, const void *data)
    {
        if (h == 0 || h >= 2048 || !data || w <= 0 || height <= 0 || w > 1024 || height > 1024 ||
            (bpp != 24 && bpp != 32))
            return false;
        auto &t = slots[h].texture;
        size_t bytes = size_t(w) * height * 4, previous = t.pixels.size();
        if (textureBytes - previous + bytes > budget)
        {
            fprintf(stderr, "[PS2] texture budget exhausted (%dx%d)\n", w, height);
            return false;
        }
        if (bytes != previous)
        {
            try
            {
                std::vector<uint8_t> replacement(bytes);
                t.pixels.swap(replacement);
            }
            catch (const std::bad_alloc &)
            {
                fprintf(stderr, "[PS2] texture allocation failed (%zu bytes)\n", bytes);
                return false;
            }
        }
        textureBytes = textureBytes - previous + bytes;
        t.width = w;
        t.height = height;
        if (bpp == 32)
            memcpy(t.pixels.data(), data, bytes);
        else
        {
            const auto *s = static_cast<const unsigned char *>(data);
            for (size_t i = 0; i < bytes / 4; ++i)
            {
                t.pixels[i * 4] = s[i * 3 + 2];
                t.pixels[i * 4 + 1] = s[i * 3 + 1];
                t.pixels[i * 4 + 2] = s[i * 3];
                t.pixels[i * 4 + 3] = 255;
            }
        }
        return true;
    }
    void batch(const float *data, int count, MarniHandle h, MarniSampler sampler, MarniBlend blend,
               bool persp, bool test, bool write)
    {
        if (!renderer || !data || count <= 0)
            return;
        int stride = persp ? 10 : 8;
        for (int i = 0; i < count; ++i)
        {
            re1sw::Vertex v[3];
            for (int j = 0; j < 3; ++j)
            {
                const float *f = data + (i * 3 + j) * stride;
                const float *uv = f + (persp ? 4 : 2);
                v[j] = {f[0],  f[1],  persp ? f[2] : 0.f,          persp ? f[3] : 1.f,
                        uv[0], uv[1], {uv[2], uv[3], uv[4], uv[5]}};
            }
            surface.triangle(v[0], v[1], v[2], texture(h), sampler == MARNI_SAMPLER_LINEAR, blend,
                             persp, test, write);
        }
    }
};
static MarniDX *s_dx = nullptr;
static re1sw::Color Color(DWORD c)
{
    return {((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f,
            ((c >> 24) & 255) / 255.f};
}
MarniDX::MarniDX() : m_pImpl(new(std::nothrow) Impl) {}
MarniDX::~MarniDX()
{
    Destroy();
    delete m_pImpl;
}
MarniDX *Marni_DX()
{
    return s_dx;
}
MarniDX *MarniDX_Create()
{
    if (!s_dx)
        s_dx = new (std::nothrow) MarniDX;
    return s_dx;
}
void MarniDX_DestroyGlobal()
{
    delete s_dx;
    s_dx = nullptr;
}
BOOL MarniDX::Create(HWND window, int, int, BOOL, int *w, int *h)
{
    if (!m_pImpl || !window)
        return FALSE;
    Destroy();
    auto *p = m_pImpl;
    p->renderer = SDL_CreateRenderer(static_cast<SDL_Window *>(window), -1, 0);
    if (!p->renderer)
        return FALSE;
    p->frame = SDL_CreateTexture(p->renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING,
                                 320, 240);
    if (!p->frame)
    {
        Destroy();
        return FALSE;
    }
    SDL_SetTextureBlendMode(p->frame, SDL_BLENDMODE_NONE);
    SDL_RenderSetLogicalSize(p->renderer, 320, 240);
    unsigned char white[4] = {255, 255, 255, 255};
    p->white = CreateTexture(1, 1, 32, white, nullptr, nullptr);
    if (!p->white)
    {
        Destroy();
        return FALSE;
    }
    if (w)
        *w = 320;
    if (h)
        *h = 240;
    return TRUE;
}
void MarniDX::Destroy()
{
    if (!m_pImpl)
        return;
    for (unsigned i = 1; i < 2048; ++i)
        DestroyTexture(i);
    SDL_DestroyTexture(m_pImpl->frame);
    m_pImpl->frame = nullptr;
    SDL_DestroyRenderer(m_pImpl->renderer);
    m_pImpl->renderer = nullptr;
    m_pImpl->white = m_pImpl->font = 0;
    m_pImpl->fontW = m_pImpl->fontH = 0;
}
BOOL MarniDX::IsReady() const
{
    return m_pImpl && m_pImpl->frame;
}
void MarniDX::GetBackBufferSize(DWORD *w, DWORD *h) const
{
    if (w)
        *w = IsReady() ? 320 : 0;
    if (h)
        *h = IsReady() ? 240 : 0;
}
void MarniDX::SetAspectLock(BOOL)
{ /* Fixed 4:3 framebuffer on PS2. */
}
int MarniDX::ChangeDisplayMode(DWORD, DWORD, BOOL)
{
    return IsReady() ? 1 : 0;
}
int MarniDX::HandleWindowMessage(HWND, UINT, WPARAM, LPARAM)
{
    return 1;
}
unsigned MarniDX::QueryVideoMemory(BOOL) const
{
    return Impl::budget;
}
void MarniDX::EnumerateDisplayModes(DisplayModeInfo *modes, int max, int *count)
{
    if (count)
        *count = 0;
    if (modes && max > 0)
    {
        modes[0] = {320, 240, 32, 60, 0};
        if (count)
            *count = 1;
    }
}
void MarniDX::EnumerateAdapters(D3DRendererInfo *modes, int max, int *count)
{
    if (count)
        *count = 0;
    if (modes && max > 0)
    {
        memset(modes, 0, sizeof(*modes));
        strcpy(modes[0].name, "PS2 SDL2 / EE rasterizer");
        if (count)
            *count = 1;
    }
}
void MarniDX::Clear(float r, float g, float b, float a)
{
    if (IsReady())
        m_pImpl->surface.clear({r, g, b, a});
}
void MarniDX::Present()
{
    if (!IsReady())
        return;
    auto *p = m_pImpl;
    if (SDL_UpdateTexture(p->frame, nullptr, p->surface.pixels.data(), 320 * 4) < 0)
    {
        fprintf(stderr, "[PS2] frame upload: %s\n", SDL_GetError());
        return;
    }
    SDL_RenderClear(p->renderer);
    SDL_RenderCopy(p->renderer, p->frame, nullptr, nullptr);
    SDL_RenderPresent(p->renderer);
}
MarniHandle MarniDX::CreateTexture(int w, int h, int bpp, const void *data, int *ow, int *oh)
{
    if (!m_pImpl)
        return 0;
    for (unsigned i = 1; i < 2048; ++i)
        if (!m_pImpl->valid(i))
        {
            if (!m_pImpl->upload(i, w, h, bpp, data))
                return 0;
            if (ow)
                *ow = w;
            if (oh)
                *oh = h;
            return i;
        }
    return 0;
}
MarniHandle MarniDX::CreateTextureFromBits(CMarniBits *b)
{
    return b ? CreateTexture(b->m_width, b->m_height, b->m_bitDepth, b->m_pPixelData, nullptr,
                             nullptr)
             : 0;
}
BOOL MarniDX::UpdateTexturePixels(MarniHandle h, const void *data, int w, int height, int bpp)
{
    if (!m_pImpl || !m_pImpl->valid(h))
        return FALSE;
    const auto &t = m_pImpl->slots[h].texture;
    return t.width == w && t.height == height && m_pImpl->upload(h, w, height, bpp, data);
}
BOOL MarniDX::ReplaceTexture(MarniHandle h, int w, int height, const void *data)
{
    return m_pImpl && m_pImpl->valid(h) && m_pImpl->upload(h, w, height, 32, data);
}
unsigned MarniDX::TextureSerial(MarniHandle h) const
{
    return m_pImpl && h < 2048 ? m_pImpl->slots[h].serial : 0;
}
void MarniDX::GetTextureSize(MarniHandle h, int *w, int *height) const
{
    bool ok = m_pImpl && m_pImpl->valid(h);
    if (w)
        *w = ok ? m_pImpl->slots[h].texture.width : 0;
    if (height)
        *height = ok ? m_pImpl->slots[h].texture.height : 0;
}
void MarniDX::DestroyTexture(MarniHandle h)
{
    if (!m_pImpl || !h || h >= 2048)
        return;
    auto &slot = m_pImpl->slots[h];
    m_pImpl->textureBytes -= slot.texture.pixels.size();
    slot.texture = re1sw::Texture{};
    ++slot.serial;
}
MarniHandle MarniDX::WhiteTexture() const
{
    return m_pImpl ? m_pImpl->white : 0;
}
void MarniDX::DrawSprite(float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                         DWORD color, MarniHandle tex, MarniSampler sampler, MarniBlend blend)
{
    if (IsReady())
        m_pImpl->surface.sprite(x, y, w, h, u0, v0, u1, v1, Color(color), m_pImpl->texture(tex),
                                sampler == MARNI_SAMPLER_LINEAR, blend);
}
void MarniDX::DrawRect(int x, int y, int w, int h, DWORD c)
{
    DrawSprite(x, y, w, h, 0, 0, 1, 1, c, 0);
}
void MarniDX::DrawLine(float x0, float y0, float x1, float y1, float thickness, DWORD c)
{
    float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-5f || !IsReady())
        return;
    float nx = -dy / len * thickness * .5f, ny = dx / len * thickness * .5f;
    re1sw::Vertex a{x0 + nx, y0 + ny, 0, 1, 0, 0, Color(c)},
        b{x1 + nx, y1 + ny, 0, 1, 0, 0, Color(c)}, d{x0 - nx, y0 - ny, 0, 1, 0, 0, Color(c)},
        e{x1 - nx, y1 - ny, 0, 1, 0, 0, Color(c)};
    m_pImpl->surface.triangle(a, b, d, m_pImpl->texture(0), false, 0, false, false, false);
    m_pImpl->surface.triangle(d, b, e, m_pImpl->texture(0), false, 0, false, false, false);
}
void MarniDX::DrawTriangles(const float *v, int n, MarniHandle t, MarniSampler s, MarniBlend b)
{
    if (m_pImpl)
        m_pImpl->batch(v, n, t, s, b, false, false, false);
}
void MarniDX::DrawTrianglesPersp(const float *v, int n, MarniHandle t, MarniSampler s, MarniBlend b,
                                 bool test)
{
    if (m_pImpl)
        m_pImpl->batch(v, n, t, s, b, true, test, false);
}
void MarniDX::DrawTriangles3D(const float *v, int n, MarniHandle t, MarniSampler s, MarniBlend b,
                              bool write)
{
    if (m_pImpl)
        m_pImpl->batch(v, n, t, s, b, true, true, write);
}
BOOL MarniDX::CaptureBackbufferToRGBA(void **pixels, DWORD *w, DWORD *h)
{
    if (!pixels || !IsReady())
        return FALSE;
    *pixels = malloc(m_pImpl->surface.pixels.size());
    if (!*pixels)
        return FALSE;
    memcpy(*pixels, m_pImpl->surface.pixels.data(), m_pImpl->surface.pixels.size());
    GetBackBufferSize(w, h);
    return TRUE;
}
BOOL MarniDX::ReadTextureRGBA(MarniHandle handle, void **pixels, int *w, int *h)
{
    if (!pixels || !m_pImpl || !m_pImpl->valid(handle))
        return FALSE;
    const auto &t = m_pImpl->slots[handle].texture;
    *pixels = malloc(t.pixels.size());
    if (!*pixels)
        return FALSE;
    memcpy(*pixels, t.pixels.data(), t.pixels.size());
    GetTextureSize(handle, w, h);
    return TRUE;
}
void MarniDX::SetFontTexture(MarniHandle h, int w, int height)
{
    if (m_pImpl)
    {
        m_pImpl->font = h;
        m_pImpl->fontW = w;
        m_pImpl->fontH = height;
    }
}
MarniHandle MarniDX::FontTexture() const
{
    return m_pImpl ? m_pImpl->font : 0;
}
int MarniDX::FontTextureWidth() const
{
    return m_pImpl ? m_pImpl->fontW : 0;
}
int MarniDX::FontTextureHeight() const
{
    return m_pImpl ? m_pImpl->fontH : 0;
}
