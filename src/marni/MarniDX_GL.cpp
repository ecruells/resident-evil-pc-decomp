// MarniDX_GL.cpp - OpenGL implementation of the MarniDX backend interface.
//
// This is the Linux counterpart of MarniDX.cpp (Direct3D 11). It defines the
// same class, the same globals and the same opaque MarniHandle table, so every
// caller in src/game/ works unchanged. See docs/LINUX_PORT.md sections 3 and 6.
//
// Written against the GLES-3-compatible subset: core-profile VAOs/VBOs, no
// fixed-function state, no desktop-only entry points, so the body can be
// repointed at GLES for Android/Switch later.
//
// Phase 2 scope: context lifecycle, Clear/Present, texture management and the
// 2D quad/triangle path. DrawTrianglesPersp/DrawTriangles3D currently route to
// the affine 2D path; real depth and perspective-correct interpolation land in
// Phase 4 with the TMD renderer.
#include "MarniDX.h"
#include "MarniGLFuncs.h"

#include <SDL2/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// DisplayModeInfo / D3DRendererInfo live with the rest of the shared types.
#include "../game/Types.h"

#define MARNI_MAX_TEXTURES 2048

// ---------------------------------------------------------------------------
// Backend state
// ---------------------------------------------------------------------------

struct MarniDX::Impl {
    SDL_Window*   window = nullptr;
    SDL_GLContext ctx    = nullptr;
    int  width  = 0;
    int  height = 0;
    bool ready  = false;

    GLuint program = 0;
    GLuint vao     = 0;
    GLuint vbo     = 0;
    GLint  uMVP    = -1;
    GLint  uTex    = -1;
    GLint  uPersp  = -1;

    MarniHandle whiteHandle = MARNI_NULL_HANDLE;
    MarniHandle fontHandle  = MARNI_NULL_HANDLE;
    int fontW = 0, fontH = 0;

    struct TexSlot {
        GLuint tex = 0;
        int w = 0, h = 0;
    } slots[MARNI_MAX_TEXTURES];

    void ReleaseSlot(MarniHandle h) {
        if (h == MARNI_NULL_HANDLE || h >= MARNI_MAX_TEXTURES) return;
        if (slots[h].tex != 0) glDeleteTextures(1, &slots[h].tex);
        slots[h].tex = 0;
        slots[h].w = slots[h].h = 0;
    }

    void ReleaseAll() {
        for (MarniHandle h = 1; h < MARNI_MAX_TEXTURES; ++h) ReleaseSlot(h);
        if (vbo) { glDeleteBuffers(1, &vbo); vbo = 0; }
        if (vao) { glDeleteVertexArrays(1, &vao); vao = 0; }
        if (program) { glDeleteProgram(program); program = 0; }
        whiteHandle = MARNI_NULL_HANDLE;
        fontHandle  = MARNI_NULL_HANDLE;
    }
};

static MarniDX* g_pDX = nullptr;

MarniDX* Marni_DX() { return g_pDX; }

// ---------------------------------------------------------------------------
// Shader / pipeline setup
// ---------------------------------------------------------------------------

static const char* kVertexSrc =
    "#version 330 core\n"
    "layout(location = 0) in vec2 aPos;\n"
    "layout(location = 1) in vec2 aUV;\n"
    "layout(location = 2) in vec4 aCol;\n"
    "layout(location = 3) in vec2 aZW;\n"      // x = depth [0,1], y = view-space w
    "uniform mat4 uMVP;\n"
    "uniform int  uPersp;\n"
    "out vec2 vUV;\n"
    "out vec4 vCol;\n"
    "void main() {\n"
    // GL's NDC z is [-1,1]; the callers pass normalised [0,1] depth.
    "    float z = aZW.x * 2.0 - 1.0;\n"
    "    vec4 p = uMVP * vec4(aPos, z, 1.0);\n"
    // Perspective-correct path: premultiply by w so the rasteriser's divide
    // restores the same screen position while interpolating UV/colour with 1/w.
    // Clamped like the Windows VS (max(input.pos.w, 1e-4)) so a degenerate
    // vertex cannot produce a NaN.
    "    if (uPersp != 0) {\n"
    "        float w = max(aZW.y, 1e-4);\n"
    "        p = vec4(p.xy * w, p.z * w, w);\n"
    "    }\n"
    "    gl_Position = p;\n"
    "    vUV = aUV;\n"
    "    vCol = aCol;\n"
    "}\n";

static const char* kFragmentSrc =
    "#version 330 core\n"
    "in vec2 vUV;\n"
    "in vec4 vCol;\n"
    "uniform sampler2D uTex;\n"
    "out vec4 oCol;\n"
    "void main() {\n"
    "    oCol = texture(uTex, vUV) * vCol;\n"
    "}\n";

static GLuint CompileShader(GLenum type, const char* src)
{
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);

    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        GLsizei n = 0;
        glGetShaderInfoLog(sh, sizeof(log) - 1, &n, log);
        log[n] = '\0';
        fprintf(stderr, "[GL] shader compile failed: %s\n", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static bool BuildPipeline(MarniDX::Impl* p)
{
    GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertexSrc);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragmentSrc);
    if (!vs || !fs) return false;

    p->program = glCreateProgram();
    glAttachShader(p->program, vs);
    glAttachShader(p->program, fs);
    glLinkProgram(p->program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(p->program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        GLsizei n = 0;
        glGetProgramInfoLog(p->program, sizeof(log) - 1, &n, log);
        log[n] = '\0';
        fprintf(stderr, "[GL] program link failed: %s\n", log);
        glDeleteProgram(p->program);
        p->program = 0;
        return false;
    }

    p->uMVP = glGetUniformLocation(p->program, "uMVP");
    p->uTex = glGetUniformLocation(p->program, "uTex");
    p->uPersp = glGetUniformLocation(p->program, "uPersp");

    glGenVertexArrays(1, &p->vao);
    glBindVertexArray(p->vao);
    glGenBuffers(1, &p->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, p->vbo);

    // Internal vertex layout: x, y, z, w, u, v, r, g, b, a (10 floats).
    const GLsizei stride = 10 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));

    glBindVertexArray(0);
    return true;
}

// ---------------------------------------------------------------------------
// Internal draw
// ---------------------------------------------------------------------------

static void ApplyBlend(MarniBlend blend)
{
    switch (blend) {
    case MARNI_BLEND_ADD:
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        break;
    case MARNI_BLEND_DISABLE:
        glDisable(GL_BLEND);
        break;
    case MARNI_BLEND_ALPHA:
    default:
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        break;
    }
}

static void ApplySampler(MarniSampler sampler)
{
    GLint filter = (sampler == MARNI_SAMPLER_POINT) ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
}

static void DrawBatch(MarniDX::Impl* p, const float* verts, int vertexCount,
                      MarniHandle tex, MarniSampler sampler, MarniBlend blend,
                      bool depthTest, bool depthWrite, bool persp)
{
    if (p == nullptr || !p->ready || p->program == 0 || vertexCount <= 0) return;

    // Y-down ortho: (0,0) top-left -> NDC (-1,+1).
    float mvp[16] = {0};
    mvp[0]  = 2.0f / (float)p->width;
    mvp[5]  = -2.0f / (float)p->height;
    mvp[10] = 1.0f;
    mvp[12] = -1.0f;
    mvp[13] = 1.0f;
    mvp[15] = 1.0f;

    GLuint texture = p->whiteHandle != MARNI_NULL_HANDLE
                   ? p->slots[p->whiteHandle].tex : 0;
    if (tex != MARNI_NULL_HANDLE && tex < MARNI_MAX_TEXTURES && p->slots[tex].tex != 0) {
        texture = p->slots[tex].tex;
    }

    glUseProgram(p->program);
    glUniformMatrix4fv(p->uMVP, 1, GL_FALSE, mvp);
    glUniform1i(p->uTex, 0);
    glUniform1i(p->uPersp, persp ? 1 : 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    ApplySampler(sampler);

    ApplyBlend(blend);

    if (depthTest) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(depthWrite ? GL_TRUE : GL_FALSE);
    } else {
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
    }

    glBindVertexArray(p->vao);
    glBindBuffer(GL_ARRAY_BUFFER, p->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(vertexCount * 10 * sizeof(float)),
                 verts, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    glBindVertexArray(0);
}

// Expand a caller vertex into the internal 10-float layout
// (x, y, z, w, u, v, r, g, b, a). `src` points at one vertex in the caller's
// layout; `hasZ`/`hasW` say which of z/w the caller already provides.
static inline void ExpandVertex(float* dst, const float* src, bool hasZ, bool hasW)
{
    if (hasZ && hasW) {
        // Already { x, y, z, w, u, v, r, g, b, a } - DrawTrianglesPersp.
        for (int i = 0; i < 10; ++i) dst[i] = src[i];
        return;
    }

    dst[0] = src[0];   // x
    dst[1] = src[1];   // y
    dst[2] = hasZ ? src[2] : 0.0f;   // z
    dst[3] = 1.0f;                   // w

    const float* rest = src + (hasZ ? 3 : 2);   // u, v, r, g, b, a
    for (int i = 0; i < 6; ++i) dst[4 + i] = rest[i];
}

// Scratch buffer for the expansion, reused across calls (single-threaded).
static float* ExpandBatch(const float* src, int vertexCount, bool hasZ, bool hasW)
{
    static float* s_buf = nullptr;
    static int s_cap = 0;
    int need = vertexCount * 10;
    if (need > s_cap) {
        float* grown = (float*)realloc(s_buf, (size_t)need * sizeof(float));
        if (grown == nullptr) return nullptr;
        s_buf = grown;
        s_cap = need;
    }
    int inStride = hasZ && hasW ? 10 : (hasZ ? 9 : 8);
    for (int v = 0; v < vertexCount; ++v) {
        ExpandVertex(s_buf + v * 10, src + v * inStride, hasZ, hasW);
    }
    return s_buf;
}

// ---------------------------------------------------------------------------
// MarniDX lifecycle
// ---------------------------------------------------------------------------

MarniDX::MarniDX() : m_pImpl(new Impl()) {}
MarniDX::~MarniDX() { Destroy(); delete m_pImpl; m_pImpl = nullptr; }

BOOL MarniDX::Create(HWND hWnd, int width, int height, BOOL fullScreen,
                     int* outWidth, int* outHeight)
{
    Impl* p = m_pImpl;
    if (p == nullptr) return FALSE;

    // The window and its GL context are created by the platform entry point
    // (main.cpp); this binds the backend to them.
    p->window = (SDL_Window*)hWnd;
    p->ctx = SDL_GL_GetCurrentContext();
    if (p->window == nullptr || p->ctx == nullptr) {
        fprintf(stderr, "[GL] Create: no window or GL context\n");
        return FALSE;
    }

    int dw = 0, dh = 0;
    SDL_GL_GetDrawableSize(p->window, &dw, &dh);
    p->width  = (dw > 0) ? dw : width;
    p->height = (dh > 0) ? dh : height;

    if (!BuildPipeline(p)) return FALSE;

    // 1x1 opaque white, the fallback for solid-colour quads.
    const DWORD white = 0xFFFFFFFFu;
    p->whiteHandle = CreateTexture(1, 1, 32, &white, nullptr, nullptr);
    if (p->whiteHandle == MARNI_NULL_HANDLE) return FALSE;

    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    p->ready = true;
    if (outWidth)  *outWidth  = p->width;
    if (outHeight) *outHeight = p->height;
    return TRUE;
}

void MarniDX::Destroy()
{
    Impl* p = m_pImpl;
    if (p == nullptr) return;
    p->ReleaseAll();
    p->ready = false;
    p->window = nullptr;
    p->ctx = nullptr;
}

BOOL MarniDX::IsReady() const
{
    return (m_pImpl != nullptr && m_pImpl->ready) ? TRUE : FALSE;
}

void MarniDX::GetBackBufferSize(DWORD* outWidth, DWORD* outHeight) const
{
    Impl* p = m_pImpl;
    if (outWidth)  *outWidth  = (p != nullptr) ? (DWORD)p->width : 0;
    if (outHeight) *outHeight = (p != nullptr) ? (DWORD)p->height : 0;
}

int MarniDX::ChangeDisplayMode(DWORD newWidth, DWORD newHeight, BOOL fullScreen)
{
    Impl* p = m_pImpl;
    if (p == nullptr || p->window == nullptr) return 0;

    SDL_SetWindowSize(p->window, (int)newWidth, (int)newHeight);
    if (fullScreen) {
        SDL_SetWindowFullscreen(p->window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    } else {
        SDL_SetWindowFullscreen(p->window, 0);
    }

    int dw = 0, dh = 0;
    SDL_GL_GetDrawableSize(p->window, &dw, &dh);
    if (dw > 0) p->width = dw;
    if (dh > 0) p->height = dh;
    return 1;
}

int MarniDX::HandleWindowMessage(HWND, UINT msg, WPARAM, LPARAM)
{
    // SDL owns the event loop on this platform; the only message that matters
    // to the backend is a resize.
    if (msg == WM_SIZE && m_pImpl != nullptr && m_pImpl->window != nullptr) {
        int dw = 0, dh = 0;
        SDL_GL_GetDrawableSize(m_pImpl->window, &dw, &dh);
        if (dw > 0) m_pImpl->width = dw;
        if (dh > 0) m_pImpl->height = dh;
    }
    return 1;
}

unsigned int MarniDX::QueryVideoMemory(BOOL) const
{
    return 128u * 1024u * 1024u;
}

void MarniDX::EnumerateDisplayModes(DisplayModeInfo* outModes, int maxModes,
                                    int* outCount)
{
    int count = 0;
    int display = 0;
    int numModes = SDL_GetNumDisplayModes(display);

    for (int i = 0; i < numModes && count < maxModes; ++i) {
        SDL_DisplayMode mode;
        if (SDL_GetDisplayMode(display, i, &mode) != 0) continue;
        if (outModes != nullptr) {
            outModes[count].dwWidth       = (DWORD)mode.w;
            outModes[count].dwHeight      = (DWORD)mode.h;
            outModes[count].dwBPP         = 32;
            outModes[count].dwRefreshRate = (DWORD)mode.refresh_rate;
            outModes[count].dwFlags       = 0;
        }
        ++count;
    }
    if (outCount) *outCount = count;
}

void MarniDX::EnumerateAdapters(D3DRendererInfo* outRenderers, int max,
                                int* outCount)
{
    int count = 0;
    if (outRenderers != nullptr && max > 0) {
        const char* name = (const char*)glGetString(GL_RENDERER);
        strncpy(outRenderers[0].name, name ? name : "OpenGL", sizeof(outRenderers[0].name) - 1);
        outRenderers[0].name[sizeof(outRenderers[0].name) - 1] = '\0';
        outRenderers[0].flags = 0;
        count = 1;
    }
    if (outCount) *outCount = count;
}

void MarniDX::Clear(float r, float g, float b, float a)
{
    Impl* p = m_pImpl;
    if (p == nullptr || !p->ready) return;
    glViewport(0, 0, p->width, p->height);
    glClearColor(r, g, b, a);
    // glClear is masked by the depth write mask. The 2D path leaves it GL_FALSE
    // (DrawBatch disables it for every non-depth draw), so without forcing it
    // back on the depth buffer was never cleared at all - each frame's 3D pass
    // tested against the PREVIOUS frame's depths. Static geometry survived
    // (same depths every frame), which is why it went unnoticed; the item
    // examine screen, whose model rotates, lost everything except the few
    // fragments that happened to be no farther than last frame's surface.
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void MarniDX::Present()
{
    Impl* p = m_pImpl;
    if (p == nullptr || !p->ready || p->window == nullptr) return;
    SDL_GL_SwapWindow(p->window);
}

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------

MarniHandle MarniDX::CreateTexture(int width, int height, int bpp,
                                   const void* pixelData,
                                   int* outWidth, int* outHeight)
{
    Impl* p = m_pImpl;
    if (p == nullptr || width <= 0 || height <= 0 || pixelData == nullptr) {
        return MARNI_NULL_HANDLE;
    }

    // Only the layouts the game actually uploads are supported here: 32-bit
    // RGBA (r in the lowest byte) and 24-bit BGR. Everything else is converted
    // by the caller before reaching this point.
    const unsigned char* src = (const unsigned char*)pixelData;
    const unsigned char* upload = src;
    unsigned char* converted = nullptr;

    if (bpp == 24) {
        converted = (unsigned char*)malloc((size_t)width * height * 4);
        if (converted == nullptr) return MARNI_NULL_HANDLE;
        for (int i = 0; i < width * height; ++i) {
            converted[i * 4 + 0] = src[i * 3 + 2];  // R
            converted[i * 4 + 1] = src[i * 3 + 1];  // G
            converted[i * 4 + 2] = src[i * 3 + 0];  // B
            converted[i * 4 + 3] = 0xFF;
        }
        upload = converted;
    } else if (bpp != 32) {
        return MARNI_NULL_HANDLE;
    }

    MarniHandle handle = MARNI_NULL_HANDLE;
    for (MarniHandle h = 1; h < MARNI_MAX_TEXTURES; ++h) {
        if (p->slots[h].tex == 0) { handle = h; break; }
    }
    if (handle == MARNI_NULL_HANDLE) {
        free(converted);
        return MARNI_NULL_HANDLE;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, upload);
    glBindTexture(GL_TEXTURE_2D, 0);

    free(converted);

    p->slots[handle].tex = tex;
    p->slots[handle].w = width;
    p->slots[handle].h = height;

    if (outWidth)  *outWidth  = width;
    if (outHeight) *outHeight = height;
    return handle;
}

MarniHandle MarniDX::CreateTextureFromBits(CMarniBits* bits)
{
    if (bits == nullptr || bits->m_pPixelData == nullptr) return MARNI_NULL_HANDLE;
    return CreateTexture((int)bits->m_width, (int)bits->m_height,
                         (int)bits->m_bitDepth, bits->m_pPixelData, nullptr, nullptr);
}

BOOL MarniDX::UpdateTexturePixels(MarniHandle tex, const void* pixelData,
                                  int width, int height, int bpp)
{
    Impl* p = m_pImpl;
    if (p == nullptr || tex == MARNI_NULL_HANDLE || tex >= MARNI_MAX_TEXTURES) return FALSE;
    if (p->slots[tex].tex == 0 || pixelData == nullptr) return FALSE;
    if (width != p->slots[tex].w || height != p->slots[tex].h) return FALSE;
    if (bpp != 32) return FALSE;

    glBindTexture(GL_TEXTURE_2D, p->slots[tex].tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                    GL_RGBA, GL_UNSIGNED_BYTE, pixelData);
    glBindTexture(GL_TEXTURE_2D, 0);
    return TRUE;
}

void MarniDX::GetTextureSize(MarniHandle tex, int* outWidth, int* outHeight) const
{
    Impl* p = m_pImpl;
    if (outWidth)  *outWidth  = 0;
    if (outHeight) *outHeight = 0;
    if (p == nullptr || tex == MARNI_NULL_HANDLE || tex >= MARNI_MAX_TEXTURES) return;
    if (outWidth)  *outWidth  = p->slots[tex].w;
    if (outHeight) *outHeight = p->slots[tex].h;
}

void MarniDX::DestroyTexture(MarniHandle tex)
{
    if (m_pImpl != nullptr) m_pImpl->ReleaseSlot(tex);
}

MarniHandle MarniDX::WhiteTexture() const
{
    return (m_pImpl != nullptr) ? m_pImpl->whiteHandle : MARNI_NULL_HANDLE;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void MarniDX::DrawSprite(float x, float y, float w, float h,
                         float u0, float v0, float u1, float v1,
                         DWORD color, MarniHandle tex,
                         MarniSampler sampler, MarniBlend blend)
{
    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8)  & 0xFF) / 255.0f;
    float b = ( color        & 0xFF) / 255.0f;
    float a = ((color >> 24) & 0xFF) / 255.0f;

    float x1 = x + w, y1 = y + h;
    const float verts[6 * 8] = {
        x,  y,  u0, v0, r, g, b, a,
        x1, y,  u1, v0, r, g, b, a,
        x,  y1, u0, v1, r, g, b, a,
        x,  y1, u0, v1, r, g, b, a,
        x1, y,  u1, v0, r, g, b, a,
        x1, y1, u1, v1, r, g, b, a,
    };
    const float* expanded = ExpandBatch(verts, 6, false, false);
    if (expanded != nullptr) {
        DrawBatch(m_pImpl, expanded, 6, tex, sampler, blend, false, false, false);
    }
}

void MarniDX::DrawRect(int x, int y, int w, int h, DWORD color)
{
    DrawSprite((float)x, (float)y, (float)w, (float)h,
               0.0f, 0.0f, 1.0f, 1.0f, color,
               (m_pImpl != nullptr) ? m_pImpl->whiteHandle : MARNI_NULL_HANDLE,
               MARNI_SAMPLER_POINT, MARNI_BLEND_ALPHA);
}

void MarniDX::DrawLine(float x0, float y0, float x1, float y1,
                       float thickness, DWORD color)
{
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if (len <= 0.0f) return;

    // Perpendicular offset of half the thickness.
    float nx = -dy / len * (thickness * 0.5f);
    float ny =  dx / len * (thickness * 0.5f);

    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8)  & 0xFF) / 255.0f;
    float b = ( color        & 0xFF) / 255.0f;
    float a = ((color >> 24) & 0xFF) / 255.0f;

    float ax = x0 + nx, ay = y0 + ny;
    float bx = x1 + nx, by = y1 + ny;
    float cx = x1 - nx, cy = y1 - ny;
    float ex = x0 - nx, ey = y0 - ny;

    const float verts[6 * 8] = {
        ax, ay, 0.0f, 0.0f, r, g, b, a,
        bx, by, 1.0f, 0.0f, r, g, b, a,
        ex, ey, 0.0f, 1.0f, r, g, b, a,
        ex, ey, 0.0f, 1.0f, r, g, b, a,
        bx, by, 1.0f, 0.0f, r, g, b, a,
        cx, cy, 1.0f, 1.0f, r, g, b, a,
    };
    const float* expanded = ExpandBatch(verts, 6, false, false);
    if (expanded != nullptr) {
        DrawBatch(m_pImpl, expanded, 6,
                  (m_pImpl != nullptr) ? m_pImpl->whiteHandle : MARNI_NULL_HANDLE,
                  MARNI_SAMPLER_POINT, MARNI_BLEND_ALPHA,
                  false, false, false);
    }
}

void MarniDX::DrawTriangles(const float* verts, int triCount, MarniHandle tex,
                            MarniSampler sampler, MarniBlend blend)
{
    if (verts == nullptr || triCount <= 0) return;
    const float* expanded = ExpandBatch(verts, triCount * 3, false, false);
    if (expanded != nullptr) {
        DrawBatch(m_pImpl, expanded, triCount * 3, tex, sampler, blend,
                  false, false, false);
    }
}

void MarniDX::DrawTrianglesPersp(const float* verts, int triCount, MarniHandle tex,
                                 MarniSampler sampler, MarniBlend blend,
                                 bool depthTest)
{
    // Input is already { x, y, z, w, u, v, r, g, b, a }.
    if (verts == nullptr || triCount <= 0) return;
    const float* expanded = ExpandBatch(verts, triCount * 3, true, true);
    if (expanded != nullptr) {
        // Test only, never write: translucent geometry must not hide the
        // depth-tested fade polys behind it.
        DrawBatch(m_pImpl, expanded, triCount * 3, tex, sampler, blend,
                  depthTest, false, true);
    }
}

void MarniDX::DrawTriangles3D(const float* verts, int triCount, MarniHandle tex,
                              MarniSampler sampler, MarniBlend blend,
                              bool depthWrite)
{
    // Input is { x, y, z (NDC [0,1]), w (view-space Z), u, v, r, g, b, a } -
    // same layout as DrawTrianglesPersp. The header's "9 floats" comment is
    // stale: TmdRenderer's TMD_VERT_FLOATS is 10 and the Windows backend
    // memcpy's sizeof(Model3DVertex) (10 floats) per vertex. Both 3D paths
    // run the perspective vertex shader; only the depth state differs.
    if (verts == nullptr || triCount <= 0) return;
    const float* expanded = ExpandBatch(verts, triCount * 3, true, true);
    if (expanded != nullptr) {
        DrawBatch(m_pImpl, expanded, triCount * 3, tex, sampler, blend,
                  true, depthWrite, true);
    }
}

// ---------------------------------------------------------------------------
// Readback
// ---------------------------------------------------------------------------

BOOL MarniDX::CaptureBackbufferToRGBA(void** outPixels, DWORD* outWidth, DWORD* outHeight)
{
    Impl* p = m_pImpl;
    if (p == nullptr || !p->ready || outPixels == nullptr) return FALSE;

    size_t size = (size_t)p->width * p->height * 4;
    unsigned char* buf = (unsigned char*)malloc(size);
    if (buf == nullptr) return FALSE;

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, p->width, p->height, GL_RGBA, GL_UNSIGNED_BYTE, buf);

    // GL reads bottom-up; the callers expect top-down.
    unsigned char* row = (unsigned char*)malloc((size_t)p->width * 4);
    if (row != nullptr) {
        for (int y = 0; y < p->height / 2; ++y) {
            unsigned char* a = buf + (size_t)y * p->width * 4;
            unsigned char* b = buf + (size_t)(p->height - 1 - y) * p->width * 4;
            memcpy(row, a, (size_t)p->width * 4);
            memcpy(a, b, (size_t)p->width * 4);
            memcpy(b, row, (size_t)p->width * 4);
        }
        free(row);
    }

    *outPixels = buf;
    if (outWidth)  *outWidth  = (DWORD)p->width;
    if (outHeight) *outHeight = (DWORD)p->height;
    return TRUE;
}

BOOL MarniDX::ReadTextureRGBA(MarniHandle, void**, int*, int*)
{
    // Phase 8: needs a framebuffer-object round trip (glGetTexImage is not in
    // the GLES subset). Only the character-select card mask uses this.
    return FALSE;
}

// ---------------------------------------------------------------------------
// Font texture
// ---------------------------------------------------------------------------

void MarniDX::SetFontTexture(MarniHandle tex, int width, int height)
{
    if (m_pImpl == nullptr) return;
    m_pImpl->fontHandle = tex;
    m_pImpl->fontW = width;
    m_pImpl->fontH = height;
}

MarniHandle MarniDX::FontTexture() const
{
    return (m_pImpl != nullptr) ? m_pImpl->fontHandle : MARNI_NULL_HANDLE;
}

int MarniDX::FontTextureWidth() const
{
    return (m_pImpl != nullptr) ? m_pImpl->fontW : 0;
}

int MarniDX::FontTextureHeight() const
{
    return (m_pImpl != nullptr) ? m_pImpl->fontH : 0;
}

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

MarniDX* MarniDX_Create()
{
    if (g_pDX != nullptr) return g_pDX;
    g_pDX = new MarniDX();
    return g_pDX;
}

void MarniDX_DestroyGlobal()
{
    if (g_pDX != nullptr) {
        g_pDX->Destroy();
        delete g_pDX;
        g_pDX = nullptr;
    }
}
