#include <cstdio>
#define PL_MPEG_IMPLEMENTATION
#include "third_party/pl_mpeg.h"
#include <cassert>
static int video = 0, audio = 0;
static void onVideo(plm_t *, plm_frame_t *frame, void *)
{
    assert(frame->width == 320 && frame->height == 240);
    ++video;
}
static void onAudio(plm_t *, plm_samples_t *samples, void *)
{
    assert(samples->count > 0);
    ++audio;
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    plm_t *p = plm_create_with_filename(argv[1]);
    assert(p && plm_has_headers(p));
    assert(plm_get_samplerate(p) == 44100);
    plm_set_video_decode_callback(p, onVideo, nullptr);
    plm_set_audio_decode_callback(p, onAudio, nullptr);
    for (int i = 0; i < 180 && !plm_has_ended(p); ++i)
        plm_decode(p, 1.0 / 30.0);
    assert(plm_has_ended(p) && video >= 29 && audio > 0);
    assert(plm_seek(p, .5, TRUE));
    int before = video;
    plm_decode(p, .1);
    assert(video > before);
    plm_destroy(p);
    printf("PASS converted MPEG stream: %d video / %d audio frames, seek and resume\n", video,
           audio);
}
