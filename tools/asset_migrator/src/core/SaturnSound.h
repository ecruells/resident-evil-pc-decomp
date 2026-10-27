#pragma once
// Sega Saturn music -> PCM. The Saturn plays its music as sequences: every
// /SND/*.CDP is a CD_PACK for Sega's 68000 sound driver ("Ver-2.10 -L", Jul 2
// 1996, /SND/SDDRVS.PCK) holding a tone bank (voices, key layers, raw PCM) and
// a sequence bank (one 16-channel event stream per song, a song per BGM
// channel). This renders one song the way that driver and the SCSP play it:
//
//  - the SEQUENCER is the driver's, read out of SDDRVS.PCK (68000, loaded at 0;
//    Ghidra "SDDRVS.PCK.0"): the 500 Hz tick and its 8.8 step counters, the
//    event reader 0x0579A and its opcode table 0x05938, the tempo track, the
//    gate timers 0x052AC, the per-tick order 0x04D48 (gates before songs);
//  - the KEY-ON is the driver's 0x03574 / 0x038B2: every layer whose key range
//    holds the note sounds, its register image goes to the slot as-is, TL is
//    the velocity curve x (255 - TL + bias) x channel level + song volume, the
//    pitch goes through the driver's own OCT and FNS tables (SDDRVS.PCK 0x40A4
//    and 0x4164, read from the file, not copied here), pan is DIPAN unless the
//    channel's CC10 overrides it;
//  - the SCSP (slots, envelope, LFO, the DSP running the driver's
//    smpldsp.EXB, the effect returns through slots 0-15's EFSDL/EFPAN) follows
//    MAME's scsp.cpp / scspdsp.cpp (BSD-3-Clause, ElSemi and R. Belmont),
//    which is the reference behaviour, not the driver's.
//
// What each piece was checked against is in docs/SATURN_VERSION_ANALYSIS.md 6a.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace re1 {
namespace saturn {

// The driver pieces the renderer needs, taken from /SND/SDDRVS.PCK.
struct SoundDriver {
    std::vector<uint8_t> image;  // the whole file, 68000 address 0
    // Validates the version string and the tables' positions.
    bool load(std::vector<uint8_t> pck, std::string* error);
};

struct RenderOptions {
    // Host sequence volume (SEQ_START / SEQ_VOLUME). The game's BGM start
    // helper (GAME5 0x0604E124) passes a per-song table value + 5; the running
    // game held 0x64 in the song slot's +0x48 (sound RAM 0x9948).
    int songVolume = 100;
    // EFSDL/EFPAN of slots 0..15 (the DSP effect returns). The game issues
    // MIXER_CHANGE(bank 0, mixer 0) (GAME5 0x0604D148 -> 0x0605D58C); tone bank
    // 0 is the player's CHR/JIL/REB.CDP, whose only mixer table is BF AF 00...
    // = EFREG0 left, EFREG1 right, both at EFSDL 5. Read back live from the
    // SCSP while the game ran (slot 0/1 reg 0x17).
    uint8_t mixer[16] = {0xBF, 0xAF};
    // Safety cap on the rendered length, seconds.
    double maxSeconds = 600.0;
    // Called with a slot's 16 register words right after each key-on
    // (diagnostics: compared against the live SCSP by the self-test).
    std::function<void(int slot, const uint16_t* regs)> onKeyOn;
};

// Interleaved stereo at 44.1 kHz, at the SCSP's output scale (MAME's >> 2
// 16-bit path) but NOT clamped, so the caller can level it. loopStart/loopEnd
// are frames ([start, end)) when the song loops, else -1. A looping song is
// rendered intro + pass 1 + pass 2 with the loop region on pass 2, so the loop
// carries the tails of the previous pass and repeats seamlessly, while the
// first time through plays clean.
struct RenderedSong {
    std::vector<int32_t> mix;
    int rate = 44100;
    long loopStart = -1;
    long loopEnd = -1;
    int notes = 0;
    int droppedNotes = 0;  // notes the driver itself would reject (no bank, bad program)
};

// Number of songs in a music CD_PACK, or -1 (error set) when it is not one.
int saturnSongCount(const std::vector<uint8_t>& cdp, std::string* error);

bool renderSaturnSong(const SoundDriver& driver, const std::vector<uint8_t>& cdp,
                      int song, const RenderOptions& opts, RenderedSong* out,
                      std::string* error);

// The key-on pitch word (SCSP reg 0x10: OCT << 11 | FNS) the driver computes
// for `key` on a layer with base note `baseNote` and fine tune `fine`, with
// channel bend `bend` in 1/256 semitone (0x03AF6). Exposed for the self-test.
uint16_t saturnPitchWord(const SoundDriver& driver, int key, int baseNote,
                         int fine, int bend);

}  // namespace saturn
}  // namespace re1
