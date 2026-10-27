#include "core/SaturnSound.h"

#include "core/SaturnCdp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>

// Addresses in comments are SDDRVS.PCK (68000, loaded at 0) unless a module is
// named. The SCSP model follows MAME src/devices/sound/scsp.cpp and
// scspdsp.cpp (BSD-3-Clause, copyright ElSemi, R. Belmont); the constants and
// the order of operations there are what this reproduces. See
// tools/asset_migrator/THIRD_PARTY_NOTICES.txt for the required notice.

namespace re1 {
namespace saturn {
namespace {

constexpr uint32_t kSoundRamBase = 0x25A00000u;
constexpr uint32_t kSoundRamSize = 0x80000u;

// Driver image layout (Ver-2.10 -L).
constexpr size_t kVersionAt = 0x1020;       // "Ver-2.10 -L"
constexpr size_t kOctTableAt = 0x40A4;      // 256 B: OCT << 4 | semitone, index key - base + 0x60
constexpr size_t kFnsTableAt = 0x4164;      // 0x600 B: FNS low byte, index semitone*128 + frac/2
constexpr size_t kExbAt = 0x6860;           // smpldsp.EXB, copied to sound RAM 0xB000
constexpr size_t kExbSize = 0x540;

// The driver runs its sequencer from a 500 Hz interrupt: the step increment
// 0x7D000 / (us per tick) is in 8.8 steps per call (0x04F42), which is 2 ms.
constexpr double kTicksPerSecond = 500.0;
constexpr int kRate = 44100;

// ---------------------------------------------------------------------------
// SCSP (MAME scsp.cpp)
// ---------------------------------------------------------------------------
constexpr int SHIFT = 12;
constexpr int LFO_SHIFT = 8;
constexpr int EG_SHIFT = 16;

const double kARTimes[64] = {100000, 100000, 8100.0, 6900.0, 6000.0, 4800.0, 4000.0, 3400.0, 3000.0, 2400.0,
    2000.0, 1700.0, 1500.0, 1200.0, 1000.0, 860.0, 760.0, 600.0, 500.0, 430.0, 380.0, 300.0, 250.0, 220.0,
    190.0, 150.0, 130.0, 110.0, 95.0, 76.0, 63.0, 55.0, 47.0, 38.0, 31.0, 27.0, 24.0, 19.0, 15.0, 13.0, 12.0,
    9.4, 7.9, 6.8, 6.0, 4.7, 3.8, 3.4, 3.0, 2.4, 2.0, 1.8, 1.6, 1.3, 1.1, 0.93, 0.85, 0.65, 0.53, 0.44, 0.40,
    0.35, 0.0, 0.0};
const double kDRTimes[64] = {100000, 100000, 118200.0, 101300.0, 88600.0, 70900.0, 59100.0, 50700.0, 44300.0,
    35500.0, 29600.0, 25300.0, 22200.0, 17700.0, 14800.0, 12700.0, 11100.0, 8900.0, 7400.0, 6300.0, 5500.0,
    4400.0, 3700.0, 3200.0, 2800.0, 2200.0, 1800.0, 1600.0, 1400.0, 1100.0, 920.0, 790.0, 690.0, 550.0, 460.0,
    390.0, 340.0, 270.0, 230.0, 200.0, 170.0, 140.0, 110.0, 98.0, 85.0, 68.0, 57.0, 49.0, 43.0, 34.0, 28.0,
    25.0, 22.0, 18.0, 14.0, 12.0, 11.0, 8.5, 7.1, 6.1, 5.4, 4.3, 3.6, 3.1};
const float kSDLT[8] = {-1000000.0f, -36.0f, -30.0f, -24.0f, -18.0f, -12.0f, -6.0f, 0.0f};
const float kLFOFreq[32] = {0.17f, 0.19f, 0.23f, 0.27f, 0.34f, 0.39f, 0.45f, 0.55f, 0.68f, 0.78f, 0.92f,
    1.10f, 1.39f, 1.60f, 1.87f, 2.27f, 2.87f, 3.31f, 3.92f, 4.79f, 6.15f, 7.18f, 8.60f, 10.8f, 14.4f, 17.2f,
    21.5f, 28.7f, 43.1f, 57.4f, 86.1f, 172.3f};
const float kASCALE[8] = {0.0f, 0.4f, 0.8f, 1.5f, 3.0f, 6.0f, 12.0f, 24.0f};
const float kPSCALE[8] = {0.0f, 7.0f, 13.5f, 27.0f, 55.0f, 112.0f, 230.0f, 494.0f};

enum EgState { kAttack, kDecay1, kDecay2, kRelease };

struct Tables {
    int32_t egTable[0x400];
    int32_t lpan[0x10000];
    int32_t rpan[0x10000];
    int32_t arTable[64];
    int32_t drTable[64];
    int alfo[4][256];
    int plfo[4][256];
    int32_t pscales[8][256];
    int32_t ascales[8][256];

    Tables() {
        for (int i = 0; i < 0x400; ++i) {
            const float envDB = (float)(3 * (i - 0x3ff)) / 32.0f;
            egTable[i] = (int32_t)(std::pow(10.0f, envDB / 20.0f) * (float)(1 << SHIFT));
        }
        for (int i = 0; i < 0x10000; ++i) {
            const int iTL = i & 0xff, iPAN = (i >> 8) & 0x1f, iSDL = (i >> 13) & 7;
            float db = 0.0f;
            if (iTL & 0x01) db -= 0.4f;
            if (iTL & 0x02) db -= 0.8f;
            if (iTL & 0x04) db -= 1.5f;
            if (iTL & 0x08) db -= 3.0f;
            if (iTL & 0x10) db -= 6.0f;
            if (iTL & 0x20) db -= 12.0f;
            if (iTL & 0x40) db -= 24.0f;
            if (iTL & 0x80) db -= 48.0f;
            const float tl = std::pow(10.0f, db / 20.0f);
            db = 0.0f;
            if (iPAN & 1) db -= 3.0f;
            if (iPAN & 2) db -= 6.0f;
            if (iPAN & 4) db -= 12.0f;
            if (iPAN & 8) db -= 24.0f;
            const float pan = (iPAN & 0xf) == 0xf ? 0.0f : std::pow(10.0f, db / 20.0f);
            const float lp = iPAN < 0x10 ? pan : 1.0f;
            const float rp = iPAN < 0x10 ? 1.0f : pan;
            const float sdl = iSDL ? std::pow(10.0f, kSDLT[iSDL] / 20.0f) : 0.0f;
            lpan[i] = (int32_t)(uint32_t)((float)(1 << SHIFT) * (4.0f * lp * tl * sdl));
            rpan[i] = (int32_t)(uint32_t)((float)(1 << SHIFT) * (4.0f * rp * tl * sdl));
        }
        arTable[0] = drTable[0] = arTable[1] = drTable[1] = 0;
        for (int i = 2; i < 64; ++i) {
            const double scale = (double)(1 << EG_SHIFT);
            double t = kARTimes[i];
            arTable[i] = t != 0.0 ? (int32_t)((1023 * 1000.0) / (44100.0 * t) * scale) : (1024 << EG_SHIFT);
            t = kDRTimes[i];
            drTable[i] = (int32_t)((1023 * 1000.0) / (44100.0 * t) * scale);
        }
        uint32_t noise = 0x12345678u;  // MAME uses machine().rand(); any fixed LCG
        for (int i = 0; i < 256; ++i) {
            // saw, square, triangle, noise - MAME LFO_Init
            alfo[0][i] = 255 - i;
            plfo[0][i] = i < 128 ? i : i - 256;
            alfo[1][i] = i < 128 ? 255 : 0;
            plfo[1][i] = i < 128 ? 127 : -128;
            alfo[2][i] = i < 128 ? 255 - i * 2 : i * 2 - 256;
            plfo[2][i] = i < 64 ? i * 2 : i < 128 ? 255 - i * 2 : i < 192 ? 256 - i * 2 : i * 2 - 511;
            noise = noise * 1103515245u + 12345u;
            const int a = (int)((noise >> 16) & 0xff);
            alfo[3][i] = a;
            plfo[3][i] = 128 - a;
        }
        auto lfix = [](float v) { return (int32_t)(uint32_t)((float)(1 << LFO_SHIFT) * v); };
        for (int s = 0; s < 8; ++s) {
            for (int i = -128; i < 128; ++i)
                pscales[s][i + 128] = lfix(std::pow(2.0f, (kPSCALE[s] * (float)i / 128.0f) / 1200.0f));
            for (int i = 0; i < 256; ++i)
                ascales[s][i] = lfix(std::pow(10.0f, (-kASCALE[s] * (float)i / 256.0f) / 20.0f));
        }
    }
};

const Tables& tables() {
    static const Tables t;
    return t;
}

struct Lfo {
    uint32_t phase = 0;
    uint32_t step = 0;
    const int* table = nullptr;
    const int32_t* scale = nullptr;
};

struct Slot {
    uint16_t r[16] = {};  // word registers 0x00..0x1E
    bool active = false;
    int egState = kRelease;
    int32_t egVolume = 0;
    int32_t egAR = 0, egD1R = 0, egD2R = 0, egRR = 0;
    int egDL = 0;
    bool egHold = false;
    uint32_t curAddr = 0, nxtAddr = 0, step = 0;
    bool backwards = false;
    Lfo plfo, alfo;

    // MAME's register macros, by name.
    int SBCTL() const { return (r[0] >> 9) & 3; }
    int SSCTL() const { return (r[0] >> 7) & 3; }
    int LPCTL() const { return (r[0] >> 5) & 3; }
    bool PCM8B() const { return (r[0] & 0x10) != 0; }
    uint32_t SA() const { return ((uint32_t)(r[0] & 0xF) << 16) | r[1]; }
    uint32_t LSA() const { return r[2]; }
    uint32_t LEA() const { return r[3]; }
    int D2R() const { return (r[4] >> 11) & 0x1F; }
    int D1R() const { return (r[4] >> 6) & 0x1F; }
    bool EGHOLD() const { return (r[4] & 0x20) != 0; }
    int AR() const { return r[4] & 0x1F; }
    bool LPSLNK() const { return (r[5] & 0x4000) != 0; }
    int KRS() const { return (r[5] >> 10) & 0xF; }
    int DL() const { return (r[5] >> 5) & 0x1F; }
    int RR() const { return r[5] & 0x1F; }
    bool STWINH() const { return (r[6] & 0x200) != 0; }
    bool SDIR() const { return (r[6] & 0x100) != 0; }
    int TL() const { return r[6] & 0xFF; }
    int OCT() const { return (r[8] >> 11) & 0xF; }
    int FNS() const { return r[8] & 0x3FF; }
    int LFOF() const { return (r[9] >> 10) & 0x1F; }
    int PLFOWS() const { return (r[9] >> 8) & 3; }
    int PLFOS() const { return (r[9] >> 5) & 7; }
    int ALFOWS() const { return (r[9] >> 3) & 3; }
    int ALFOS() const { return r[9] & 7; }
    int ISEL() const { return (r[10] >> 3) & 0xF; }
    int IMXL() const { return r[10] & 7; }
    int DISDL() const { return (r[11] >> 13) & 7; }
    int DIPAN() const { return (r[11] >> 8) & 0x1F; }
    int EFSDL() const { return (r[11] >> 5) & 7; }
    int EFPAN() const { return r[11] & 0x1F; }
};

// scspdsp.cpp
uint16_t dspPack(int32_t val) {
    const int sign = (val >> 23) & 1;
    uint32_t temp = (uint32_t)(val ^ (val << 1)) & 0xFFFFFF;
    int exponent = 0;
    for (int k = 0; k < 12; k++) {
        if (temp & 0x800000) break;
        temp <<= 1;
        exponent += 1;
    }
    if (exponent < 12)
        val = (val << exponent) & 0x3FFFFF;
    else
        val <<= 11;
    val >>= 11;
    val &= 0x7FF;
    val |= sign << 15;
    val |= exponent << 11;
    return (uint16_t)val;
}

int32_t dspUnpack(uint16_t val) {
    const int sign = (val >> 15) & 1;
    int exponent = (val >> 11) & 0xF;
    const int mantissa = val & 0x7FF;
    int32_t uval = mantissa << 11;
    if (exponent > 11) {
        exponent = 11;
        uval |= sign << 22;
    } else {
        uval |= (sign ^ 1) << 22;
    }
    uval |= sign << 23;
    uval = (int32_t)((uint32_t)uval << 8) >> 8;
    uval >>= exponent;
    return uval;
}

int32_t sext(int32_t v, int bits) { return (int32_t)((uint32_t)v << (32 - bits)) >> (32 - bits); }

class Scsp {
public:
    std::vector<uint8_t> ram = std::vector<uint8_t>(kSoundRamSize, 0);
    Slot slots[32];

    // DSP
    uint16_t coef[64] = {};
    uint16_t madrs[32] = {};
    uint16_t mpro[128 * 4] = {};
    int32_t temp[128] = {};
    int32_t mems[32] = {};
    int32_t mixs[16] = {};
    int16_t efreg[16] = {};  // 16-bit in the SCSP (and in MAME): the sum wraps
    uint32_t dec = 0;
    uint32_t rbl = 8 * 1024;  // words
    uint32_t rbp = 0;         // 4K-word units
    int lastStep = 0;
    bool dspRunning = false;

    void startDsp() {
        int i;
        for (i = 127; i >= 0; --i) {
            const uint16_t* p = mpro + i * 4;
            if (p[0] || p[1] || p[2] || p[3]) break;
        }
        lastStep = i + 1;
        dspRunning = lastStep > 0;
    }

    uint16_t rd16(uint32_t a) const {
        a &= kSoundRamSize - 2;
        return (uint16_t)((ram[a] << 8) | ram[a + 1]);
    }
    void wr16(uint32_t a, uint16_t v) {
        a &= kSoundRamSize - 2;
        ram[a] = (uint8_t)(v >> 8);
        ram[a + 1] = (uint8_t)v;
    }
    int8_t rd8(uint32_t a) const { return (int8_t)ram[a & (kSoundRamSize - 1)]; }

    static int getRate(const int32_t* table, int base, int r) {
        return table[std::clamp(base + (r << 1), 0, 63)];
    }

    void computeLfo(Slot& s) {
        const Tables& t = tables();
        const float step = kLFOFreq[s.LFOF()] * 256.0f / 44100.0f;
        const uint32_t ps = (uint32_t)((float)(1 << LFO_SHIFT) * step);
        if (s.PLFOS()) {
            s.plfo.step = ps;
            s.plfo.table = t.plfo[s.PLFOWS()];
            s.plfo.scale = t.pscales[s.PLFOS()];
        }
        if (s.ALFOS()) {
            s.alfo.step = ps;
            s.alfo.table = t.alfo[s.ALFOWS()];
            s.alfo.scale = t.ascales[s.ALFOS()];
        }
    }

    // StartSlot (KYONB set, KYONEX, slot in RELEASE).
    void keyOn(Slot& s) {
        const Tables& t = tables();
        s.active = true;
        s.curAddr = 0;
        s.nxtAddr = 1 << SHIFT;
        int octave = ((s.OCT() ^ 8) - 8) + SHIFT - 10;
        uint32_t fn = (uint32_t)s.FNS() + (1 << 10);
        s.step = octave >= 0 ? fn << octave : fn >> -octave;
        // Compute_EG
        const int oct = (s.OCT() ^ 8) - 8;
        const int rate = s.KRS() != 0xf ? oct + 2 * s.KRS() + ((s.FNS() >> 9) & 1) : 0;
        s.egAR = getRate(t.arTable, rate, s.AR());
        s.egD1R = getRate(t.drTable, rate, s.D1R());
        s.egD2R = getRate(t.drTable, rate, s.D2R());
        s.egRR = getRate(t.drTable, rate, s.RR());
        s.egDL = 0x1f - s.DL();
        s.egHold = s.EGHOLD();
        s.egState = kAttack;
        s.egVolume = 0x17F << EG_SHIFT;
        s.backwards = false;
        s.plfo = Lfo();
        s.alfo = Lfo();
        computeLfo(s);  // the driver keys on with LFORE set and clears it after
    }

    void keyOff(Slot& s) {
        if (s.active) s.egState = kRelease;
    }

    int egUpdate(Slot& s) {
        switch (s.egState) {
        case kAttack:
            s.egVolume += s.egAR;
            if (s.egVolume >= (0x3ff << EG_SHIFT)) {
                if (!s.LPSLNK()) {
                    s.egState = kDecay1;
                    if (s.egD1R >= (1024 << EG_SHIFT)) s.egState = kDecay2;
                }
                s.egVolume = 0x3ff << EG_SHIFT;
            }
            if (s.egHold) return 0x3ff << (SHIFT - 10);
            break;
        case kDecay1:
            s.egVolume -= s.egD1R;
            if (s.egVolume <= 0) s.egVolume = 0;
            if ((s.egVolume >> (EG_SHIFT + 5)) <= s.egDL) s.egState = kDecay2;
            break;
        case kDecay2:
            if (s.D2R() == 0) return (s.egVolume >> EG_SHIFT) << (SHIFT - 10);
            s.egVolume -= s.egD2R;
            if (s.egVolume <= 0) s.egVolume = 0;
            break;
        case kRelease:
            s.egVolume -= s.egRR;
            if (s.egVolume <= 0) {
                s.egVolume = 0;
                s.active = false;
            }
            break;
        default:
            return 1 << SHIFT;
        }
        return (s.egVolume >> EG_SHIFT) << (SHIFT - 10);
    }

    static int32_t lfoStep(Lfo& l, bool amplitude) {
        l.phase += l.step;
        l.phase &= (1u << (LFO_SHIFT + 8)) - 1;
        int p = l.table[l.phase >> LFO_SHIFT];
        p = l.scale[amplitude ? p : p + 128];
        return p << (SHIFT - LFO_SHIFT);
    }

    // UpdateSlot, without FM (no layer in the game's banks uses MDL/MDXSL/MDYSL;
    // the renderer refuses such a voice before it gets here).
    int32_t updateSlot(Slot& s) {
        if (s.SSCTL() == 3) return 0;
        int32_t sample = 0;
        uint32_t step = s.step;
        if (s.PLFOS() && s.plfo.table) step = (uint32_t)((int64_t)step * lfoStep(s.plfo, false) >> SHIFT);

        uint32_t addr1, addr2;
        if (s.PCM8B()) {
            addr1 = s.curAddr >> SHIFT;
            addr2 = s.nxtAddr >> SHIFT;
        } else {
            addr1 = (s.curAddr >> (SHIFT - 1)) & ~1u;
            addr2 = (s.nxtAddr >> (SHIFT - 1)) & ~1u;
        }
        if (s.SSCTL() == 0) {
            const int32_t fpart = (int32_t)(s.curAddr & ((1 << SHIFT) - 1));
            if (s.PCM8B()) {
                const int32_t p1 = rd8(s.SA() + addr1) * 256, p2 = rd8(s.SA() + addr2) * 256;
                sample = (p1 * ((1 << SHIFT) - fpart) + p2 * fpart) >> SHIFT;
            } else {
                const int32_t p1 = (int16_t)rd16(s.SA() + addr1), p2 = (int16_t)rd16(s.SA() + addr2);
                sample = (p1 * ((1 << SHIFT) - fpart) + p2 * fpart) >> SHIFT;
            }
        }
        if (s.SBCTL() & 1) sample ^= 0x7FFF;
        if (s.SBCTL() & 2) sample = (int16_t)(sample ^ 0x8000);

        if (s.backwards)
            s.curAddr -= step;
        else
            s.curAddr += step;
        s.nxtAddr = s.curAddr + (1 << SHIFT);

        uint32_t a[2] = {s.curAddr >> SHIFT, s.nxtAddr >> SHIFT};
        uint32_t* sa[2] = {&s.curAddr, &s.nxtAddr};
        if (a[0] >= s.LSA() && !s.backwards && s.LPSLNK() && s.egState == kAttack) s.egState = kDecay1;
        for (int k = 0; k < 2; ++k) {
            int32_t rem;
            switch (s.LPCTL()) {
            case 0:
                if (a[k] >= s.LSA() && a[k] >= s.LEA()) s.active = false;
                break;
            case 1:
                if (a[k] >= s.LEA()) {
                    rem = (int32_t)(*sa[k] - (s.LEA() << SHIFT));
                    *sa[k] = (s.LSA() << SHIFT) + rem;
                }
                break;
            case 2:
                if (a[k] >= s.LSA() && !s.backwards) {
                    rem = (int32_t)(*sa[k] - (s.LSA() << SHIFT));
                    *sa[k] = (s.LEA() << SHIFT) - rem;
                    s.backwards = true;
                } else if ((a[k] < s.LSA() || (*sa[k] & 0x80000000u)) && s.backwards) {
                    rem = (int32_t)((s.LSA() << SHIFT) - *sa[k]);
                    *sa[k] = (s.LEA() << SHIFT) - rem;
                }
                break;
            case 3:
                if (a[k] >= s.LEA()) {
                    rem = (int32_t)(*sa[k] - (s.LEA() << SHIFT));
                    *sa[k] = (s.LEA() << SHIFT) - rem;
                    s.backwards = true;
                } else if ((a[k] < s.LSA() || (*sa[k] & 0x80000000u)) && s.backwards) {
                    rem = (int32_t)((s.LSA() << SHIFT) - *sa[k]);
                    *sa[k] = (s.LSA() << SHIFT) + rem;
                    s.backwards = false;
                }
                break;
            }
        }

        if (!s.SDIR()) {
            if (s.ALFOS() && s.alfo.table) sample = (sample * lfoStep(s.alfo, true)) >> SHIFT;
            if (s.egState == kAttack)
                sample = (sample * egUpdate(s)) >> SHIFT;
            else
                sample = (sample * tables().egTable[egUpdate(s) >> (SHIFT - 10)]) >> SHIFT;
        }
        return sample;
    }

    void dspStep() {
        if (!dspRunning) return;
        std::fill(std::begin(efreg), std::end(efreg), 0);
        int32_t acc = 0, memval = 0, frcReg = 0, yReg = 0;
        uint32_t adrsReg = 0;
        for (int step = 0; step < lastStep; ++step) {
            const uint16_t* ip = mpro + step * 4;
            const uint32_t TRA = (ip[0] >> 8) & 0x7f, TWT = (ip[0] >> 7) & 1, TWA = ip[0] & 0x7f;
            const uint32_t XSEL = (ip[1] >> 15) & 1, YSEL = (ip[1] >> 13) & 3, IRA = (ip[1] >> 6) & 0x3f,
                           IWT = (ip[1] >> 5) & 1, IWA = ip[1] & 0x1f;
            const uint32_t TABLE = (ip[2] >> 15) & 1, MWT = (ip[2] >> 14) & 1, MRD = (ip[2] >> 13) & 1,
                           EWT = (ip[2] >> 12) & 1, EWA = (ip[2] >> 8) & 0xf, ADRL = (ip[2] >> 7) & 1,
                           FRCL = (ip[2] >> 6) & 1, SHIFTV = (ip[2] >> 4) & 3, YRL = (ip[2] >> 3) & 1,
                           NEGB = (ip[2] >> 2) & 1, ZERO = (ip[2] >> 1) & 1, BSEL = ip[2] & 1;
            const uint32_t NOFL = (ip[3] >> 15) & 1, COEF = (ip[3] >> 9) & 0x3f, MASA = (ip[3] >> 2) & 0x1f,
                           ADREB = (ip[3] >> 1) & 1, NXADR = ip[3] & 1;

            int32_t inputs;
            if (IRA <= 0x1f)
                inputs = mems[IRA];
            else if (IRA <= 0x2f)
                inputs = mixs[IRA - 0x20] << 4;
            else if (IRA <= 0x31)
                inputs = 0;  // EXTS: CD audio, silent here
            else
                return;
            inputs = sext(inputs, 24);
            if (IWT) {
                mems[IWA] = memval;
                if (IRA == IWA) inputs = memval;
            }
            int32_t b;
            if (!ZERO) {
                b = BSEL ? acc : sext(temp[(TRA + dec) & 0x7f], 24);
                if (NEGB) b = 0 - b;
            } else {
                b = 0;
            }
            const int32_t x = XSEL ? inputs : sext(temp[(TRA + dec) & 0x7f], 24);
            int32_t y = 0;
            if (YSEL == 0)
                y = frcReg;
            else if (YSEL == 1)
                y = (int16_t)coef[COEF] >> 3;
            else if (YSEL == 2)
                y = (yReg >> 11) & 0x1fff;
            else
                y = (yReg >> 4) & 0x0fff;
            if (YRL) yReg = inputs;

            int32_t shifted;
            if (SHIFTV == 0)
                shifted = std::clamp<int32_t>(acc, -0x00800000, 0x007fffff);
            else if (SHIFTV == 1)
                shifted = std::clamp<int32_t>(acc * 2, -0x00800000, 0x007fffff);
            else if (SHIFTV == 2)
                shifted = sext(acc * 2, 24);
            else
                shifted = sext(acc, 24);

            y = sext(y, 13);
            const int64_t v = ((int64_t)x * (int64_t)y) >> 12;
            acc = (int32_t)(v + b);

            if (TWT) temp[(TWA + dec) & 0x7f] = shifted;
            if (FRCL) frcReg = SHIFTV == 3 ? (shifted & 0x0fff) : ((shifted >> 11) & 0x1fff);

            if (MRD || MWT) {
                uint32_t addr = madrs[MASA];
                if (!TABLE) addr += dec;
                if (ADREB) addr += adrsReg & 0x0FFF;
                if (NXADR) addr++;
                if (!TABLE)
                    addr &= rbl - 1;
                else
                    addr &= 0xffff;
                addr += rbp << 12;
                addr <<= 1;
                if (MRD && (step & 1)) memval = NOFL ? (int32_t)((uint32_t)rd16(addr) << 8) : dspUnpack(rd16(addr));
                if (MWT && (step & 1)) wr16(addr, NOFL ? (uint16_t)(shifted >> 8) : dspPack(shifted));
            }
            if (ADRL) adrsReg = SHIFTV == 3 ? ((shifted >> 12) & 0xfff) : (uint32_t)(inputs >> 16);
            if (EWT) efreg[EWA] = (int16_t)(efreg[EWA] + (shifted >> 8));
        }
        --dec;
        std::fill(std::begin(mixs), std::end(mixs), 0);
    }

    // DoMasterSamples, one frame.
    void sample(int32_t* outL, int32_t* outR) {
        const Tables& t = tables();
        int32_t l = 0, r = 0;
        for (Slot& s : slots) {
            if (!s.active) continue;
            const int32_t smp = updateSlot(s);
            const int tl = s.SDIR() ? 0 : s.TL();
            uint32_t enc = (uint32_t)tl | ((uint32_t)s.IMXL() << 13);
            mixs[s.ISEL()] += (smp * t.lpan[enc]) >> (SHIFT - 2);
            enc = (uint32_t)tl | ((uint32_t)s.DIPAN() << 8) | ((uint32_t)s.DISDL() << 13);
            l += (smp * t.lpan[enc]) >> SHIFT;
            r += (smp * t.rpan[enc]) >> SHIFT;
        }
        dspStep();
        for (int i = 0; i < 16; ++i) {
            const Slot& s = slots[i];
            if (!s.EFSDL()) continue;
            const uint32_t enc = ((uint32_t)s.EFPAN() << 8) | ((uint32_t)s.EFSDL() << 13);
            l += ((int32_t)efreg[i] * t.lpan[enc]) >> SHIFT;
            r += ((int32_t)efreg[i] * t.rpan[enc]) >> SHIFT;
        }
        *outL = l >> 2;
        *outR = r >> 2;
    }

    bool silent() const {
        for (const Slot& s : slots)
            if (s.active) return false;
        return true;
    }
};

// ---------------------------------------------------------------------------
// Driver model
// ---------------------------------------------------------------------------

uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

void setError(std::string* e, const std::string& m) {
    if (e) *e = m;
}

// One MIDI-style message as the sequencer queues it (0x056A8).
struct Msg {
    uint8_t type;  // 0x80 note off, 0x90 note on, 0xB0 CC, 0xC0 program, 0xE0 bend, ...
    uint8_t ch;    // 0..31; 16..31 are rejected by the message processor (0x02EB0)
    uint8_t d1, d2;
};

struct ChannelState {  // 0x20-byte block at 0x7000 + song*0x200 + ch*0x20
    uint8_t program = 0;
    uint8_t pan = 0;         // +1: bit 7 = CC10 set it, low 5 bits = SCSP pan
    bool sustain = false;    // +3 bit 6
    uint32_t bank = 0;       // +4: tone bank address in sound RAM, 0 = none
    int16_t bend = 0;        // +8: 1/256 semitone
    uint16_t level = 0x7F;   // +0x10 = volume * expression >> 7 (read live: 7F before any CC)
    uint16_t volume = 0x7F;  // +0x12
    uint16_t expression = 0x7F;  // +0x14
    int8_t bias = 0;         // +0xE: the voice's volume bias, copied at key-on
};

struct SlotState {  // the driver's per-slot block at 0x8000 + slot*0x40
    bool held = false;   // +0x3A bit 3
    bool used = false;   // ever keyed
    int ch = -1, key = -1;
    uint16_t base = 0;   // +0x36: velocity curve x (255 - TL + bias) >> 8
    bool velPath = false;  // +0x05 bit 0: TL follows velocity and channel level
    uint16_t age = 0;    // +0x3E
};

struct GateTimer {  // 32 x 0x0C at 0x052AC
    bool active = false;
    uint8_t ch = 0, key = 0, vel = 0;
    int32_t counter = 0;
};

class Driver {
public:
    Driver(const SoundDriver& drv, Scsp& scsp, const RenderOptions& opts)
        : m_drv(drv.image), m_scsp(scsp), m_opts(opts) {}

    int notes = 0;
    int dropped = 0;
    std::string fatal;

    // Where the pack's tone bank sits in sound RAM. CC32 (0x01A98) resolves a
    // bank id through the area map; every song of all 57 music packs selects
    // id 4, which the game's map puts wherever that pack's tone bank loads.
    uint32_t toneBank = 0;

    void process(const Msg& m) {
        if (m.ch >= 16) return;  // 0x02EB0: track index >= 16 is an error
        ChannelState& c = m_ch[m.ch];
        switch (m.type) {
        case 0x80: noteOff(m.ch, m.d1); break;
        case 0x90:
            if (m.d2 == 0)
                noteOff(m.ch, m.d1);
            else
                noteOn(m.ch, m.d1, m.d2);
            break;
        case 0xB0: control(m.ch, m.d1, m.d2); break;
        case 0xC0:  // 0x01B14
            if (c.bank && 8u + 2u * (m.d1 & 0x7F) < be16(ram(c.bank))) {
                c.program = m.d1 & 0x7F;
                c.bend = 0;
                if (!(c.pan & 0x40)) c.pan &= 0x7F;
            }
            break;
        case 0xE0: {  // 0x03480: scaled by the voice's bend range, applied at the next key-on
            int v = (((m.d2 & 0x7F) << 7) | (m.d1 & 0x7F)) - 0x2000;
            int range = 0;
            if (c.bank) {
                const uint32_t vo = voiceOffset(c);
                if (vo) range = ram(vo)[0] & 0xF;
            }
            // 0x034B6: range r -> v / 32 * r (the jump table's shifts and adds)
            c.bend = (int16_t)((v * range) / 32);
            break;
        }
        default: break;  // 2/5/7: poly pressure, channel pressure - flags only
        }
    }

private:
    const std::vector<uint8_t>& m_drv;
    Scsp& m_scsp;
    const RenderOptions& m_opts;
    ChannelState m_ch[16];
    SlotState m_slot[32];
    uint16_t m_keyCount = 0;

    const uint8_t* ram(uint32_t a) const { return m_scsp.ram.data() + (a & (kSoundRamSize - 1)); }

    uint32_t voiceOffset(const ChannelState& c) const {
        const uint8_t* b = ram(c.bank);
        return c.bank + be16(b + 8 + 2 * c.program);
    }

    // Channel level x base -> TL (0x03A74 / 0x0321E), with the song volume.
    uint8_t tlFor(uint16_t base, const ChannelState& c) const {
        int v = (int)((base * c.level) >> 6);
        const int sv = (int8_t)(uint8_t)((m_opts.songVolume & 0xFF) ^ 0x80);
        if (((m_opts.songVolume & 0xFF) ^ 0x80) != 0) {
            v += sv;
            if (v < 0 || v >= 0x100) v = v < 0 ? 0 : 0xFF;
        }
        return (uint8_t)~(uint8_t)v;
    }

    void control(int ch, int num, int val) {
        ChannelState& c = m_ch[ch];
        switch (num) {
        case 7:   // 0x031D4
        case 2:
        case 11:  // 0x031C0
            if (num == 7)
                c.volume = (uint16_t)(val & 0x7F);
            else
                c.expression = (uint16_t)(val & 0x7F);
            c.level = (uint16_t)((c.expression * c.volume) >> 7);
            for (int i = 0; i < 32; ++i) {
                SlotState& s = m_slot[i];
                if (s.ch != ch || !s.velPath || !m_scsp.slots[i].active) continue;
                m_scsp.slots[i].r[6] = (uint16_t)((m_scsp.slots[i].r[6] & 0xFF00) | tlFor(s.base, c));
            }
            break;
        case 10: {  // 0x03264
            uint8_t p = (uint8_t)((val & 0x7F) >> 2);
            if (p >= 0x10)
                p = (uint8_t)(p - 0x10);
            else
                p = (uint8_t)((uint8_t)~(uint8_t)(p - 0x10) + 0x10);
            c.pan = (uint8_t)(0x80 | (p & 0x1F));
            for (int i = 0; i < 32; ++i) {
                if (m_slot[i].ch != ch || !m_scsp.slots[i].active) continue;
                Slot& s = m_scsp.slots[i];
                s.r[11] = (uint16_t)((s.r[11] & 0xE0FF) | ((p & 0x1F) << 8));
            }
            break;
        }
        case 16: {  // 0x01938: mixer table n of the channel's bank -> slots 0..15 EFSDL/EFPAN
            if (!c.bank) break;
            const uint8_t* b = ram(c.bank);
            const uint32_t at = c.bank + be16(b) + (uint32_t)(val & 0x7F) * 18;
            if (at + 16 > c.bank + be16(b + 2)) break;
            for (int i = 0; i < 16; ++i) setEffectReturn(i, ram(at)[i]);
            break;
        }
        case 17:
        case 70: {  // 0x030C0: EFPAN of effect return `ch`
            uint8_t p = (uint8_t)((val & 0x7F) >> 2);
            if (p >= 0x10)
                p = (uint8_t)(p - 0x10);
            else
                p = (uint8_t)((uint8_t)~(uint8_t)(p - 0x10) + 0x10);
            Slot& s = m_scsp.slots[ch];
            s.r[11] = (uint16_t)((s.r[11] & 0xFFE0) | (p & 0x1F));
            break;
        }
        case 71: {  // 0x030F8: EFSDL of effect return `ch` (< 0x12)
            Slot& s = m_scsp.slots[ch];
            s.r[11] = (uint16_t)((s.r[11] & 0xFF1F) | (((val & 0x70) << 1) & 0xE0));
            break;
        }
        case 32:  // 0x01A98: bank select through the area map
            c.bank = toneBank;
            break;
        case 64:  // 0x032F0: sustain
            if (val & 0x40) {
                c.sustain = true;
            } else {
                c.sustain = false;
                for (int i = 0; i < 32; ++i)
                    if (m_slot[i].ch == ch && !m_slot[i].held) m_scsp.keyOff(m_scsp.slots[i]);
            }
            break;
        default: break;  // 1: LFO on/off (no layer has LFO depth), 80-83: 3D, 91: DSP change
        }
    }

public:
    void setEffectReturn(int i, uint8_t v) {
        Slot& s = m_scsp.slots[i];
        s.r[11] = (uint16_t)((s.r[11] & 0xFF00) | v);
    }

private:
    // 0x03CAC
    void noteOff(int ch, int key) {
        for (int i = 0; i < 32; ++i) {
            SlotState& s = m_slot[i];
            if (!s.held || s.ch != ch || s.key != key) continue;
            if (!m_ch[ch].sustain) m_scsp.keyOff(m_scsp.slots[i]);
            s.held = false;
        }
    }

    // 0x0378A: an idle slot (lowest first), else the last releasing one, else
    // the last held one of this song, else the oldest.
    int allocSlot(const bool* taken) {
        int released = -1, held = -1;
        for (int i = 0; i < 32; ++i) {
            if (taken[i]) continue;
            if (!m_scsp.slots[i].active) return i;
            if (!m_slot[i].held)
                released = i;
            else
                held = i;
        }
        if (released >= 0) return released;
        if (held >= 0) return held;
        int best = -1, bestAge = -1;
        for (int i = 0; i < 32; ++i) {
            if (taken[i]) continue;
            const int age = (uint16_t)(m_keyCount - m_slot[i].age);
            if (age > bestAge) best = i, bestAge = age;
        }
        return best;
    }

    // 0x03574 (non-FM path) + 0x038B2 per slot.
    void noteOn(int ch, int key, int vel) {
        ChannelState& c = m_ch[ch];
        if (!c.bank) {
            ++dropped;
            return;
        }
        const uint32_t voice = voiceOffset(c);
        const uint8_t* v = ram(voice);
        if ((int8_t)v[2] < 0 || (v[0] & 0xF0) != 0) {
            ++dropped;  // no layers / a play mode the driver rejects (0x03C78)
            return;
        }
        uint32_t layers[32];
        int n = 0;
        for (int i = 0; i <= (v[2] & 0x7F) && n < 32; ++i) {
            const uint32_t lo = voice + 4 + 0x20 * (uint32_t)i;
            const uint8_t* L = ram(lo);
            if (key >= L[0] && key <= L[1]) layers[n++] = lo;
        }
        if (n == 0) {
            ++dropped;
            return;
        }
        c.bias = (int8_t)v[3];
        ++m_keyCount;
        ++notes;
        bool taken[32] = {};
        for (int k = 0; k < n; ++k) {
            const uint8_t* L = ram(layers[k]);
            if (be16(L + 0x10) != 0 || (L[0x19] & 0x80)) {
                if (fatal.empty()) fatal = "voice uses FM modulation (driver path 0x03692), not rendered";
                continue;
            }
            const int si = allocSlot(taken);
            if (si < 0) break;
            taken[si] = true;
            keyOnSlot(si, ch, key, vel, L);
        }
    }

    void keyOnSlot(int si, int ch, int key, int vel, const uint8_t* L) {
        ChannelState& c = m_ch[ch];
        Slot& s = m_scsp.slots[si];
        SlotState& st = m_slot[si];
        m_scsp.keyOff(s);  // 0x038BC: KYONB=0 + KYONEX first
        st.ch = ch;
        st.key = key;
        st.held = true;
        st.used = true;
        st.age = m_keyCount;
        // 0x038F2..: the layer's register image
        const uint32_t sa = c.bank + be32(L + 2);
        s.r[0] = (uint16_t)((s.r[0] & 0xE000) | ((sa >> 16) & 0x07FF));
        s.r[1] = (uint16_t)sa;
        s.r[2] = be16(L + 6);
        s.r[3] = be16(L + 8);
        s.r[4] = be16(L + 0x0A);
        s.r[5] = be16(L + 0x0C);
        s.r[7] = be16(L + 0x10);
        uint16_t lfo = be16(L + 0x14);
        if (L[0x0E] & 0x80) lfo &= 0xFF18;  // modulation-wheel controlled: depths off
        s.r[9] = lfo;
        s.r[10] = be16(L + 0x16);
        uint8_t dd = L[0x18];
        if (c.pan & 0xC0) dd = (uint8_t)((dd & 0xE0) | (c.pan & 0x1F));
        s.r[11] = (uint16_t)((s.r[11] & 0x00FF) | (dd << 8));

        // 0x0395E: TL. Velocity path when the layer is heard at all.
        uint8_t tl;
        const bool velPath = (L[2] & 0x20) || (L[0x17] & 7) || (L[0x18] & 0xE0);
        st.velPath = velPath;
        if (!velPath) {
            tl = L[0x0F];
        } else {
            const uint8_t* vl = ram(c.bank + be16(ram(c.bank) + 2) + 10u * L[0x1D]);
            // 0x039B2: rate / point / level segments
            int d1 = 0, d2 = 0, d3 = vl[0];
            int w = vel;
            const uint8_t* p = vl + 1;
            for (int seg = 0; seg < 3; ++seg) {
                if (w <= p[0]) break;
                d1 = p[0];
                d2 = p[1];
                d3 = p[2];
                p += 3;
            }
            int x = (uint8_t)(w - d1);
            const int shift = (d3 >> 4) & 0xF;
            const bool boost = (d3 & 8) != 0;  // the carry out of LSR #4
            int r8 = 0;
            switch (d3 & 7) {
            case 0:
            case 4: r8 = d2; break;
            case 1:
                r8 = boost ? (((x & 0x7F) * 12 << shift) >> 3) + d2 : ((x << shift) + d2);
                break;
            case 2: r8 = x + d2; break;
            case 3: r8 = (x >> shift) + d2; break;
            case 5: r8 = d2 - (x >> shift); break;
            case 6: r8 = d2 - x; break;
            case 7:
                r8 = boost ? d2 - ((((x & 0x7F) * 12) << shift) >> 3) : d2 - (x << shift);
                break;
            }
            uint8_t b = (uint8_t)r8;
            if (b & 0x80) b = (b & 0x40) ? 0 : 0x7F;  // 0x03A3E: clamp
            b &= 0x7F;
            if (b == 0) b = 1;
            int lvl = (uint8_t)~L[0x0F];
            if (c.bias != 0) {
                lvl += c.bias;
                if (lvl < 0 || lvl >= 0x100) {
                    ++dropped;  // 0x03C5E: the driver rejects the note
                    st.held = false;
                    return;
                }
            }
            st.base = (uint16_t)((b * lvl) >> 8);
            tl = tlFor(st.base, c);
        }
        s.r[6] = (uint16_t)((L[0x0E] << 8) | tl);
        s.r[8] = pitch(key, L[0x19], (int8_t)L[0x1A], c.bend);
        s.r[0] = (uint16_t)((s.r[0] & 0x07FF) | 0x1800);  // KYONEX | KYONB (0x0390A)
        m_scsp.keyOn(s);
        if (m_opts.onKeyOn) m_opts.onKeyOn(si, s.r);
    }

public:
    uint16_t pitch(int key, int base, int fine, int bend) const {
        // 0x03AF6
        const uint8_t* oct = m_drv.data() + kOctTableAt;
        const uint8_t* fns = m_drv.data() + kFnsTableAt;
        uint16_t d5 = (uint16_t)(bend + fine);
        int idx = ((d5 >> 8) + key + 0x60 - (base & 0x7F)) & 0xFF;
        const uint16_t t = (uint16_t)(oct[idx] << 7);
        const uint16_t octBits = t & 0x7800;
        uint16_t d2 = (uint16_t)(((uint16_t)(t << 1) & 0xFF00) | (d5 & 0xFF));
        d2 = (uint16_t)((d2 & 0xFFE) >> 1);
        int f = fns[d2];
        if (d2 >= 0x1EF) f += 0x100;
        if (d2 >= 0x383) f += 0x100;
        if (d2 >= 0x4D9) f += 0x100;
        return (uint16_t)(octBits | f);
    }
};

// One song's reader and timers (the 0x60-byte song slot).
class Song {
public:
    Song(const uint8_t* bank, size_t size, size_t songOff) : m_b(bank), m_size(size), m_off(songOff) {}

    bool start(std::string* error) {
        if (m_off + 16 > m_size) return fail(error, "song header outside the bank");
        m_ppqn = be16(m_b + m_off);
        if (m_ppqn < 0x18 || m_ppqn >= 0x3C1) return fail(error, "song resolution out of range");  // 0x04EDC
        const uint16_t tempoLoop = be16(m_b + m_off + 6);
        m_base = m_off + be16(m_b + m_off + 4);
        m_tempoPtr = m_off + 8;
        m_tempoLoop = tempoLoop ? m_off + tempoLoop : 0;
        if (!readTempo(error)) return false;
        m_ptr = m_base;
        Ev e;
        const int r = readEvent(&e);
        if (r == 1) {
            m_ended = true;
            return true;
        }
        if (r != 0) return fail(error, "bad first event");
        m_pending = e;
        m_step = (int32_t)(e.step << 8);
        return true;
    }

    // One 2 ms interrupt: gates first (0x04D4C), then the song (0x0506E).
    // `emit` receives the messages in the driver's queue order.
    template <class F>
    bool tick(F&& emit, std::string* error) {
        for (GateTimer& g : m_gates) {
            if (!g.active) continue;
            g.counter -= (int32_t)m_inc;
            if (g.counter > 0) continue;
            emit(Msg{0x80, g.ch, g.key, g.vel});
            g.active = false;
            --m_activeGates;
            if (m_activeGates == 0 && m_ended) m_stopped = true;
        }
        if (m_stopped) return true;
        // tempo track
        m_tempoCounter -= (int32_t)m_inc;
        if (m_tempoCounter < 0 && !readTempo(error)) return false;
        if (m_ended) return true;
        m_step -= (int32_t)m_inc;
        if (m_step > 0) return true;
        for (;;) {
            fire(emit);
            Ev e;
            const int r = readEvent(&e);
            if (r == 1) {
                m_ended = true;
                if (m_activeGates == 0) m_stopped = true;
                return true;
            }
            if (r != 0) {
                // 0x05290: malformed stream, stop active gates but report a
                // failed render rather than emitting a silently truncated WAV.
                for (GateTimer& g : m_gates)
                    if (g.active) emit(Msg{0x80, g.ch, g.key, g.vel}), g.active = false;
                m_activeGates = 0;
                m_ended = m_stopped = true;
                setError(error, "invalid sequence event");
                return false;
            }
            m_pending = e;
            m_step += (int32_t)(e.step << 8);
            if (m_step > 0) return true;
        }
    }

    bool stopped() const { return m_stopped; }
    // Number of times the first event after the loop point has been emitted,
    // and the tick of each.
    const std::vector<long>& loopHeads() const { return m_loopHeads; }
    bool hasLoop() const { return m_loopPtr != 0; }
    long ticks = 0;

private:
    struct Ev {
        Msg msg{};
        uint32_t step = 0;
        uint32_t gate = 0;
        bool loopHead = false;
    };

    const uint8_t* m_b;
    size_t m_size, m_off;
    uint16_t m_ppqn = 0;
    size_t m_base = 0, m_ptr = 0, m_loopPtr = 0, m_ret = 0;
    int m_callCount = 0;
    size_t m_tempoPtr = 0, m_tempoLoop = 0;
    int32_t m_tempoCounter = 0;
    uint32_t m_inc = 0;
    int32_t m_step = 0;
    Ev m_pending;
    bool m_ended = false, m_stopped = false;
    bool m_nextIsLoopHead = false;
    GateTimer m_gates[32];
    int m_activeGates = 0;
    std::vector<long> m_loopHeads;

    static bool fail(std::string* e, const char* m) {
        setError(e, m);
        return false;
    }

    uint8_t byte() { return m_ptr < m_size ? m_b[m_ptr++] : 0x83; }

    // 0x05B30 / 0x05B58: the next (steps, us) tempo event.
    bool readTempo(std::string* error) {
        if (m_tempoPtr + 8 > m_base) {
            if (!m_tempoLoop) return true;  // keep the current tempo
            m_tempoPtr = m_tempoLoop;
        }
        if (m_tempoPtr + 8 > m_size) return fail(error, "tempo track outside the bank");
        const uint32_t steps = be32(m_b + m_tempoPtr), us = be32(m_b + m_tempoPtr + 4);
        m_tempoPtr += 8;
        if (us < 0x30D40 || us >= 0x16E361) {  // 0x04F1A
            m_ended = m_stopped = true;
            return fail(error, "tempo out of the driver's range");
        }
        m_tempoCounter = (int32_t)(steps << 8);
        const uint32_t perTick = (us / m_ppqn) & 0xFFFF;         // divu.w
        m_inc = perTick ? (0x7D000u / perTick) & 0xFFFF : 0xFFFF;  // divu.w
        return true;
    }

    // 0x0579A with D0 = 0: the next event, with the extension bytes folded in.
    // 0 = event, 1 = end (0x83), 2 = error.
    int readEvent(Ev* e) {
        uint32_t step = 0, gate = 0;
        for (int guard = 0; guard < 100000; ++guard) {
            const size_t at = m_ptr;
            const uint8_t st = byte();
            if (st <= 0x7F) {  // 0x057C0: note
                const uint8_t key = byte(), vel = byte(), g = byte(), s = byte();
                gate += g | ((st & 0x40) << 2);
                step += s | ((st & 0x20) << 3);
                e->msg = Msg{0x90, (uint8_t)(st & 0x1F), key, vel};
                return finish(e, step, gate);
            }
            switch (st) {
            case 0x81: {  // 0x05A38: play `count` events at stream + offset
                const uint16_t off = (uint16_t)(byte() << 8);
                const uint16_t o = (uint16_t)(off | byte());
                m_callCount = byte();
                m_ret = m_ptr;
                m_ptr = m_base + o;
                continue;
            }
            case 0x82:  // 0x05A54: loop point, set the first time, taken after
                step += byte();
                if (!m_loopPtr) {
                    m_loopPtr = m_ptr;
                } else {
                    m_ptr = m_loopPtr;
                }
                m_nextIsLoopHead = true;
                continue;
            case 0x83: return 1;
            case 0x88: gate += 0x200; continue;
            case 0x89: gate += 0x800; continue;
            case 0x8A: gate += 0x1000; continue;
            case 0x8B: gate += 0x2000; continue;
            case 0x8C: step += 0x100; continue;
            case 0x8D: step += 0x200; continue;
            case 0x8E: step += 0x800; continue;
            case 0x8F: step += 0x1000; continue;
            default: break;
            }
            const uint8_t hi = st & 0xF0;
            if (hi >= 0xA0 && hi <= 0xB0) {  // 0x05A92: two data bytes
                uint8_t d1 = byte(), d2 = byte();
                const bool high = (d1 & 0x80) != 0;
                d1 &= 0x7F;
                step += byte();
                e->msg = Msg{hi, (uint8_t)((st & 0xF) | (high ? 0x10 : 0)), d1, d2};
                return finish(e, step, gate);
            }
            if (hi == 0xC0 || hi == 0xD0) {  // 0x05A7C
                uint8_t d1 = byte();
                const bool high = (d1 & 0x80) != 0;
                d1 &= 0x7F;
                step += byte();
                e->msg = Msg{hi, (uint8_t)((st & 0xF) | (high ? 0x10 : 0)), d1, 0};
                return finish(e, step, gate);
            }
            if (hi == 0xE0) {  // 0x05A86
                uint8_t d2 = byte();
                const bool high = (d2 & 0x80) != 0;
                d2 &= 0x7F;
                step += byte();
                e->msg = Msg{hi, (uint8_t)((st & 0xF) | (high ? 0x10 : 0)), 0, d2};
                return finish(e, step, gate);
            }
            (void)at;
            return 2;  // 0x05AFE: 80, 84-87, 90-9F, F0-FF
        }
        return 2;
    }

    // 0x057E2: count down a reference and return from it.
    int finish(Ev* e, uint32_t step, uint32_t gate) {
        e->step = step;
        e->gate = gate;
        e->loopHead = m_nextIsLoopHead;
        m_nextIsLoopHead = false;
        if (m_callCount) {
            if (--m_callCount == 0) m_ptr = m_ret;
        }
        return 0;
    }

    template <class F>
    void fire(F&& emit) {
        const Ev& e = m_pending;
        if (e.loopHead) m_loopHeads.push_back(ticks);
        emit(e.msg);
        if (e.msg.type == 0x90) {
            // 0x0565E: the note-off timer; gate from now, the remainder included
            for (GateTimer& g : m_gates) {
                if (g.active) continue;
                g.active = true;
                g.ch = e.msg.ch;
                g.key = e.msg.d1;
                g.vel = e.msg.d2;
                g.counter = (int32_t)(e.gate << 8) + m_step;
                ++m_activeGates;
                break;
            }
        }
    }
};

struct PackParts {
    std::vector<std::pair<uint32_t, const uint8_t*>> ramSegments;  // address, bytes
    std::vector<uint32_t> ramSizes;
    const uint8_t* seq = nullptr;
    size_t seqSize = 0;
    uint32_t toneAddr = 0;
};

bool splitPack(const std::vector<uint8_t>& cdp, PackParts* out, std::string* error) {
    SaturnCdpPack pack;
    if (!parseSaturnCdpPack(cdp, &pack, error)) return false;
    std::vector<const SaturnCdpSegment*> segs;
    const SaturnCdpSegment* map = nullptr;
    for (const SaturnCdpSegment& s : pack.segments) {
        if (s.loadAddress < kSoundRamBase || s.loadAddress >= kSoundRamBase + kSoundRamSize)
            continue;  // SH-2 side data (SE packs carry a table for 0x002E8430)
        if (s.loadAddress - kSoundRamBase == 0x500) {
            map = &s;  // a sound area map
            continue;
        }
        if (s.loadAddress - kSoundRamBase < 0x1000) continue;
        segs.push_back(&s);
    }
    if (segs.size() < 2) {
        setError(error, "not a music pack (no tone + sequence bank)");
        return false;
    }
    // The sequence bank is the last segment, the tone bank the one before
    // (every music pack on the disc: tone for 0x55000 + sequence for 0x7A000).
    const SaturnCdpSegment* seq = segs.back();
    const SaturnCdpSegment* tone = segs[segs.size() - 2];
    const uint32_t seqAddr = seq->loadAddress - kSoundRamBase;
    uint32_t toneAddr = tone->loadAddress - kSoundRamBase;
    if (seqAddr + seq->sizeBytes > kSoundRamSize) {
        setError(error, "sequence bank does not fit in sound RAM");
        return false;
    }
    // Six BGM packs also carry an area map, and in three of them (BGM30, 33,
    // 35) the tone bank is too big for its 0x55000 destination: the map puts
    // bank 4 lower (0x3F840 for BGM33). Load it there. The address does not
    // change the sound: layer sample addresses are relative to the bank.
    if (toneAddr + tone->sizeBytes > seqAddr && map) {
        const uint8_t* m = cdp.data() + map->sourceOffset;
        for (uint32_t o = 0; o + 8 <= map->sizeBytes && m[o] != 0xFF; o += 8)
            if (m[o] == 0x04) toneAddr = ((uint32_t)m[o + 1] << 16) | ((uint32_t)m[o + 2] << 8) | m[o + 3];
    }
    if (toneAddr < 0x14040 || toneAddr + tone->sizeBytes > seqAddr) {
        setError(error, "tone bank does not fit below the sequence bank");
        return false;
    }
    out->ramSegments.push_back({toneAddr, cdp.data() + tone->sourceOffset});
    out->ramSizes.push_back(tone->sizeBytes);
    out->ramSegments.push_back({seqAddr, cdp.data() + seq->sourceOffset});
    out->ramSizes.push_back(seq->sizeBytes);
    out->seq = cdp.data() + seq->sourceOffset;
    out->seqSize = seq->sizeBytes;
    out->toneAddr = toneAddr;
    return true;
}

}  // namespace

bool SoundDriver::load(std::vector<uint8_t> pck, std::string* error) {
    image = std::move(pck);
    if (image.size() < kExbAt + kExbSize ||
        std::memcmp(image.data() + kVersionAt, "Ver-2.10", 8) != 0) {
        setError(error, "SDDRVS.PCK is not the Ver-2.10 sound driver (wrong or truncated file)");
        return false;
    }
    if (std::memcmp(image.data() + kExbAt, "smpldsp.EXB", 11) != 0) {
        setError(error, "SDDRVS.PCK has no smpldsp.EXB at +0x6860");
        return false;
    }
    // The OCT table puts key == base note at octave 0, semitone 0.
    if (image[kOctTableAt + 0x60] != 0x00 || image[kOctTableAt + 0x54] != 0xF0) {
        setError(error, "SDDRVS.PCK pitch table not where Ver-2.10 has it");
        return false;
    }
    return true;
}

int saturnSongCount(const std::vector<uint8_t>& cdp, std::string* error) {
    PackParts parts;
    if (!splitPack(cdp, &parts, error)) return -1;
    if (parts.seqSize < 2) return -1;
    return be16(parts.seq);
}

uint16_t saturnPitchWord(const SoundDriver& driver, int key, int baseNote, int fine, int bend) {
    Scsp dummy;
    RenderOptions o;
    Driver d(driver, dummy, o);
    return d.pitch(key, baseNote, fine, bend);
}

bool renderSaturnSong(const SoundDriver& driver, const std::vector<uint8_t>& cdp, int song,
                      const RenderOptions& opts, RenderedSong* out, std::string* error) {
    PackParts parts;
    if (!splitPack(cdp, &parts, error)) return false;
    const uint16_t songCount = parts.seqSize >= 2 ? be16(parts.seq) : 0;
    if (song < 0 || song >= songCount || 2 + 4 * (size_t)songCount > parts.seqSize) {
        setError(error, "song index out of range");
        return false;
    }
    const size_t songOff = be32(parts.seq + 2 + 4 * song);

    auto scsp = std::make_unique<Scsp>();
    for (size_t i = 0; i < parts.ramSegments.size(); ++i)
        std::memcpy(scsp->ram.data() + parts.ramSegments[i].first, parts.ramSegments[i].second,
                    parts.ramSizes[i]);

    // DSP: the driver's smpldsp.EXB (name, header, COEF x64, MADRS x32, MPRO
    // x128 x4). Byte +0x20 is RBL: the EFFECT_CHANGE handler (0x033A0) needs a
    // work area of 0x4000 << RBL + 0x40 bytes, which is exactly the area map's
    // type-3 entry (0xC000, 0x8040). RBP is that entry's address / 0x2000.
    const uint8_t* exb = driver.image.data() + kExbAt;
    for (int i = 0; i < 64; ++i) scsp->coef[i] = be16(exb + 0x40 + 2 * i);
    for (int i = 0; i < 32; ++i) scsp->madrs[i] = be16(exb + 0xC0 + 2 * i);
    for (int i = 0; i < 128 * 4; ++i) scsp->mpro[i] = be16(exb + 0x100 + 2 * i);
    scsp->rbl = (8u * 1024u) << (exb[0x20] & 3);
    // The DSP work area: the in-game area map's type-3 entry (read back live
    // at 0x500, and the same in every map a pack carries).
    const uint32_t work = 0xC000;
    scsp->rbp = work >> 13;
    std::fill(scsp->ram.begin() + work, scsp->ram.begin() + work + 0x8040, 0);
    scsp->startDsp();

    Driver drv(driver, *scsp, opts);
    drv.toneBank = parts.toneAddr;
    for (int i = 0; i < 16; ++i) drv.setEffectReturn(i, opts.mixer[i]);

    Song seq(parts.seq, parts.seqSize, songOff);
    if (!seq.start(error)) return false;

    std::vector<int32_t> pcm;
    const double samplesPerTick = kRate / kTicksPerSecond;
    double nextTick = 0.0;
    long frame = 0;
    const long maxFrames = (long)(opts.maxSeconds * kRate);
    long tailFrom = -1;
    int quiet = 0;
    std::vector<long> headFrames;
    size_t headsSeen = 0;
    for (;;) {
        while (frame >= (long)nextTick) {
            if (!seq.stopped()) {
                std::string tickError;
                if (!seq.tick([&](const Msg& m) { drv.process(m); }, &tickError)) {
                    setError(error, tickError);
                    return false;
                }
                ++seq.ticks;
                while (headsSeen < seq.loopHeads().size()) {
                    headFrames.push_back(frame);
                    ++headsSeen;
                }
            }
            nextTick += samplesPerTick;
        }
        if (!drv.fatal.empty()) {
            setError(error, drv.fatal);
            return false;
        }
        if (headFrames.size() >= 3) break;  // intro, pass 1, pass 2 done
        if (seq.stopped() && tailFrom < 0) tailFrom = frame;
        int32_t l, r;
        scsp->sample(&l, &r);
        pcm.push_back(l);
        pcm.push_back(r);
        ++frame;
        if (tailFrom >= 0) {
            quiet = (std::abs(l) < 2 && std::abs(r) < 2 && scsp->silent()) ? quiet + 1 : 0;
            if (quiet > kRate / 10 || frame - tailFrom > kRate * 8) break;
        }
        if (frame >= maxFrames) {
            setError(error, "song did not end or loop within the length cap");
            return false;
        }
    }

    out->rate = kRate;
    out->notes = drv.notes;
    out->droppedNotes = drv.dropped;
    out->mix.clear();
    if (headFrames.size() >= 3) {
        // intro + pass 1 + pass 2, looping over pass 2 (see renderSepTrack in
        // Spu.cpp): pass 1 is kept whole so the first time through has no
        // tails from the loop's end over its start.
        const long l2 = headFrames[1], l3 = headFrames[2];
        out->mix.assign(pcm.begin(), pcm.begin() + l3 * 2);
        out->loopStart = l2;
        out->loopEnd = l3;
    } else {
        out->mix = std::move(pcm);
        out->loopStart = out->loopEnd = -1;
    }
    return true;
}

}  // namespace saturn
}  // namespace re1
