// Port-added bounded MPEG1/MP2 streaming backend. Convert on the host with
// tools/prepare_ps2_movies.py; original assets are kept. No whole-movie buffers.
#include "../../marni/MarniDX.h"
#include "../platform.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <string>
#include <vector>
#define PL_MPEG_IMPLEMENTATION
#include "../../third_party/pl_mpeg.h"

extern bool re1_movie_audio_ready();
extern void re1_movie_audio_reset();
extern unsigned re1_movie_audio_played();
extern unsigned re1_movie_audio_queued();
extern bool re1_movie_audio_push(const float *, unsigned);
namespace
{
plm_t *decoder = nullptr;
PlatVideoOverlayCallback overlay = nullptr;
MarniHandle texture = 0;
std::vector<unsigned char> rgba;
bool active = false, ended = false, audio = false, frameReady = false, decodeError = false;
double baseTime = 0, stopTime = 0;
DWORD startTicks = 0;

std::string MoviePath(const char *path)
{
    std::string s = path ? path : "";
    if (s.size() < 8 || s.substr(s.size() - 8) != ".ps2.mpg")
    {
        auto dot = s.find_last_of('.');
        if (dot != std::string::npos)
            s.resize(dot);
        s += ".ps2.mpg";
    }
    char normalized[1024];
    plat_normalize_path(s.c_str(), normalized, sizeof(normalized));
    return normalized;
}
void Video(plm_t *, plm_frame_t *frame, void *)
{
    if (frame->width > 320 || frame->height > 240)
    {
        decodeError = true;
        return;
    }
    plm_frame_to_rgba(frame, rgba.data(), frame->width * 4);
    auto *dx = Marni_DX();
    if (!dx)
        return;
    if (!texture)
        texture = dx->CreateTexture(frame->width, frame->height, 32, rgba.data(), nullptr, nullptr);
    else
        dx->UpdateTexturePixels(texture, rgba.data(), frame->width, frame->height, 32);
    if (!texture)
        decodeError = true;
    frameReady = true;
}
void Audio(plm_t *, plm_samples_t *samples, void *)
{
    if (!re1_movie_audio_push(samples->interleaved, samples->count))
        decodeError = true;
}
void Finish()
{
    active = false;
    ended = true;
    re1_movie_audio_reset();
}
} // namespace
void plat_video_set_overlay_callback(PlatVideoOverlayCallback cb)
{
    overlay = cb;
}
BOOL plat_video_init(void)
{
    return TRUE;
}
void plat_video_close(void)
{
    re1_movie_audio_reset();
    if (decoder)
        plm_destroy(decoder);
    decoder = nullptr;
    if (texture && Marni_DX())
        Marni_DX()->DestroyTexture(texture);
    texture = 0;
    std::vector<unsigned char>().swap(rgba);
    active = ended = frameReady = decodeError = false;
}
BOOL plat_video_open_and_play(const char *path, int toMs)
{
    plat_video_close();
    const std::string name = MoviePath(path);
    decoder = plm_create_with_filename(name.c_str());
    if (!decoder)
        return FALSE;
    int w = plm_get_width(decoder), h = plm_get_height(decoder);
    if (w <= 0 || h <= 0 || w > 320 || h > 240 || !plm_has_headers(decoder))
    {
        fprintf(stderr, "[PS2] invalid MPEG1 sidecar: %s\n", name.c_str());
        plat_video_close();
        return FALSE;
    }
    audio = plm_get_num_audio_streams(decoder) > 0 && re1_movie_audio_ready();
    if (audio && plm_get_samplerate(decoder) != 44100)
    {
        fprintf(stderr, "[PS2] movie audio must be 44100 Hz\n");
        plat_video_close();
        return FALSE;
    }
    // PL_MPEG writes RGB only; leave every alpha byte opaque for overlays.
    rgba.assign(size_t(w) * h * 4, 255);
    plm_set_video_decode_callback(decoder, Video, nullptr);
    plm_set_audio_decode_callback(decoder, Audio, nullptr);
    plm_set_audio_enabled(decoder, audio);
    plm_set_audio_lead_time(decoder, .12);
    baseTime = 0;
    stopTime = toMs > 0 ? toMs / 1000.0 : 0;
    startTicks = SDL_GetTicks();
    active = true;
    plm_decode(decoder, 0); // prime audio before its sample cursor is our clock
    return TRUE;
}
int plat_video_probe_duration_ms(const char *path)
{
    plm_t *probe = plm_create_with_filename(MoviePath(path).c_str());
    if (!probe)
        return 0;
    int ms = int(plm_get_duration(probe) * 1000 + .5);
    plm_destroy(probe);
    return ms;
}
void plat_video_play_from(int fromMs)
{
    if (!decoder)
        return;
    re1_movie_audio_reset();
    ended = false;
    decodeError = false;
    baseTime = std::max(0, fromMs) / 1000.0;
    stopTime = 0;
    if (!plm_seek(decoder, baseTime, TRUE))
    {
        Finish();
        return;
    }
    startTicks = SDL_GetTicks();
    active = true;
    plm_decode(decoder, 0);
}
void plat_video_stop(void)
{
    Finish();
}
BOOL plat_video_is_active(void)
{
    return active;
}
BOOL plat_video_take_end_event(void)
{
    bool result = ended;
    ended = false;
    return result;
}
void plat_video_tick(void)
{
    if (!active || !decoder)
        return;
    double clock = baseTime + (audio ? re1_movie_audio_played() / 44100.0
                                     : (SDL_GetTicks() - startTicks) / 1000.0);
    if (stopTime > 0 && clock >= stopTime)
    {
        Finish();
        return;
    }
    double target = clock + 1.0 / 30.0;
    if (stopTime > 0)
        target = std::min(target, stopTime);
    double delta = target - plm_get_time(decoder);
    if (delta > 0)
        plm_decode(decoder, std::min(delta, .1));
    if (decodeError)
    {
        fprintf(stderr, "[PS2] movie decode/audio queue failed\n");
        Finish();
        return;
    }
    if (frameReady && Marni_DX())
    {
        auto *dx = Marni_DX();
        dx->Clear(0, 0, 0, 1);
        if (!overlay || !overlay(texture, int(clock * 1000)))
            dx->DrawSprite(0, 0, 320, 240, 0, 0, 1, 1, 0xffffffff, texture, MARNI_SAMPLER_LINEAR,
                           MARNI_BLEND_DISABLE);
        dx->Present();
        frameReady = false;
    }
    if (plm_has_ended(decoder) && (!audio || !re1_movie_audio_queued()))
        Finish();
}
void plat_video_set_window_mode(BOOL)
{ /* A single fixed framebuffer on PS2. */
}
