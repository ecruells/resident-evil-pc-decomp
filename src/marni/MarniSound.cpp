// MarniSound.cpp - DirectSound class implementation
// All functions implemented from Ghidra decompilations.
#include "MarniSound.h"
#include "../Globals.h"
#include "../system/AssetPath.h"
#include <cstdio>
#include <cstring>
#include <xaudio2.h>        // NOLINT: XAudio2 (DirectSound replacement)
#include <memory>            // NOLINT: std::unique_ptr for XAudio2 cleanup

// XAudio2 engine globals (PSYQ DirectSound stub replacement)
static IXAudio2*         g_pXAudio2 = NULL;
static IXAudio2MasteringVoice* g_pMasterVoice = NULL;
static IXAudio2SourceVoice*    g_BankVoices[81];  // bank handles 1-80

// ============================================================================
// SFX filename sub-tables
// ============================================================================
#define SFX_SUBTABLE_SIZE 16

static const char* g_st0[SFX_SUBTABLE_SIZE] = {
    "Knife01","Knife02","Knife03",NULL,
    NULL,     NULL,     NULL,     NULL,
    NULL,     NULL,     NULL,     NULL,
    NULL,     NULL,     NULL,     NULL,
};
static const char* g_st2[SFX_SUBTABLE_SIZE] = {
    NULL,   NULL,   NULL,   NULL,
    NULL,   "Gun04",NULL,   "Gun01",
    NULL,   "Gun02","Gun03",NULL,
    NULL,   NULL,   NULL,   NULL,
};
static const char* g_st3[SFX_SUBTABLE_SIZE] = {
    NULL,   NULL,   NULL,   NULL,
    NULL,   NULL,   NULL,   "Shot01",
    NULL,   "Shot04","Shot02","Shot03",
    NULL,   NULL,   NULL,   NULL,
};
static const char* g_st4[SFX_SUBTABLE_SIZE] = {
    NULL,     NULL,     NULL,     NULL,
    NULL,     NULL,     NULL,     "Magnum01",
    NULL,     "Magnum02","Magnum03",NULL,
    NULL,     NULL,     NULL,     NULL,
};
static const char* g_st8[SFX_SUBTABLE_SIZE] = {
    NULL,     NULL,     NULL,     NULL,
    NULL,     "Gracid01",NULL,   "Gracid02",
    NULL,     "Gracid04",NULL,   NULL,
    NULL,     "Gracid03",NULL,   NULL,
};
static const char* g_st7[SFX_SUBTABLE_SIZE] = {
    NULL,     NULL,     NULL,     NULL,
    NULL,     "Grbomb01",NULL,   "Grbomb02",
    NULL,     "Grbomb04",NULL,   NULL,
    "Grbomb03",NULL,    NULL,    NULL,
};
static const char* g_st9[SFX_SUBTABLE_SIZE] = {
    NULL,     NULL,     NULL,     NULL,
    NULL,     "Grfire01",NULL,   "Grfire02",
    NULL,     "Grfire04",NULL,   NULL,
    NULL,     NULL,     "Grfire03",NULL,
};
static const char* g_st10[SFX_SUBTABLE_SIZE] = {
    NULL,     NULL,     NULL,     NULL,
    NULL,     NULL,     NULL,     "Rocket01",
    NULL,     "Rocket02",NULL,    NULL,
    NULL,     NULL,     NULL,     "Rocket03",
};
static const char* g_st11[SFX_SUBTABLE_SIZE] = {
    "Bio01",  NULL,     NULL,     NULL,
    NULL,     NULL,     NULL,     NULL,
    NULL,     NULL,     NULL,     NULL,
    NULL,     "cancel", "type01", "type02",
};
static const char* g_st12[SFX_SUBTABLE_SIZE] = {
    "Evil01", NULL,     NULL,     NULL,
    NULL,     NULL,     NULL,     NULL,
    NULL,     NULL,     NULL,     NULL,
    NULL,     "cancel", "type01", "type02",
};
static const char* g_st13[SFX_SUBTABLE_SIZE] = {
    "Select06","Select05",NULL,   NULL,
    NULL,     NULL,     NULL,     NULL,
    NULL,     NULL,     NULL,     NULL,
    NULL,     NULL,     NULL,     NULL,
};
static const char* g_st14[SFX_SUBTABLE_SIZE] = {
    "Ending07",NULL,   "Ending06",NULL,
    NULL,     NULL,     NULL,     NULL,
    NULL,     NULL,     NULL,     NULL,
    NULL,     "cancel", "type01", "type02",
};
static const char* g_st6[SFX_SUBTABLE_SIZE] = {
    NULL,     NULL,     NULL,     "Flame01",
    "Flame02",NULL,     NULL,     NULL,
    NULL,     "Flame03",NULL,     NULL,
    NULL,     NULL,     NULL,     NULL,
};
static const char* g_st15[SFX_SUBTABLE_SIZE] = {
    NULL,     NULL,     NULL,     NULL,
    NULL,     NULL,     NULL,     "Win95_mg",
    NULL,     NULL,     "A_mcn03",NULL,
    NULL,     NULL,     NULL,     NULL,
};

const char** g_SoundBanksTable[SFX_SUBTABLE_SIZE] = {
    g_st0,  g_st0,  g_st2,  g_st3,  g_st4,  g_st4,  g_st6,  g_st7,
    g_st8,  g_st9,  g_st10, g_st11, g_st12, g_st13, g_st14, g_st15,
};

// ------------------------------------------------------------------------
// Bank offset helpers
// ------------------------------------------------------------------------
#define BANK_BASE(bank)       ((BYTE*)this + (int)(bank) * 0xA2C)
#define BANK_WAV_PTR(bank)    (*(BYTE**)(BANK_BASE(bank) + 0x1C))
#define BANK_DATA_SZ(bank)    (*(DWORD*)(BANK_BASE(bank) + 0x20))
#define BANK_SR(bank)         (*(DWORD*)(BANK_BASE(bank) + 0x24))
#define BANK_CH(bank)         (*(WORD*)(BANK_BASE(bank) + 0x28))
#define BANK_BPS(bank)        (*(WORD*)(BANK_BASE(bank) + 0x2A))
#define BANK_PLAYSR(bank)     (*(int*)(BANK_BASE(bank) + 0x920))
#define BANK_PAN(bank)        (*(int*)(BANK_BASE(bank) + 0x924))
#define BANK_VOL(bank)        (*(int*)(BANK_BASE(bank) + 0x928))
#define BANK_SLOT(bank)       (*(int*)(BANK_BASE(bank) + 0x92C))
#define BANK_STATUS(bank)     (*(int*)(BANK_BASE(bank) + 0x930))
#define BANK_DSBUF(bank)      (*(int**)(BANK_BASE(bank) + 0x93C))
#define BANK_NAME(bank)       ((char*)(BANK_BASE(bank) + 0x940))
#define BANK_ACTIVE(bank)     (*(int*)(BANK_BASE(bank) + 0xA44))

// Internal sentinel to identify stubbed DS buffers (no real COM buffer created)
#define DS_STUB_BUF ((int*)0x00000001)

// ------------------------------------------------------------------------
// DirectSound constructor (0x0041f3b0)
// ------------------------------------------------------------------------
DirectSound::DirectSound(HWND hwnd)
{
    memset(this, 0, 0x32DDC);
    *(int**)((BYTE*)this + 0x10) = (int*)1;  // m_bInitialized = 1
    *(int*)((BYTE*)this + 0x18) = (int)hwnd;  // hwnd
}

// ============================================================================
// DirectSound::compact (0x0041e680) — compact/restore buffers
// ============================================================================
void DirectSound::compact(void)
{
    int* dev = *(int**)this;
    if (dev == NULL) return;
    int iVar1 = ((int(*)(int*, int, int))dev[6])(dev, *(int*)((BYTE*)this + 0x18), 3);
    if (iVar1 != 0) {
        printf("DirectSound::compact: set_cooperative_level error\n");
        return;
    }
    iVar1 = ((int(*)(int*))dev[7])(dev);
    if (iVar1 != 0) {
        printf("DirectSound::compact: compact error\n");
        return;
    }
    iVar1 = ((int(*)(int*, int, int))dev[6])(dev, *(int*)((BYTE*)this + 0x18), 2);
    if (iVar1 != 0) {
        printf("DirectSound::compact: restore_cooperative_level error\n");
    }
}

// ============================================================================
// DirectSound::GetStatus (0x0041e700)
// ============================================================================
int DirectSound::GetStatus(int bank)
{
    char dbg[256];
    sprintf(dbg, "[DS] GetStatus(bank=%d)\n", bank);
    OutputDebugStringA(dbg);

    if (*(int*)((BYTE*)this + 0x10) == 0) return 0;
    if (BANK_ACTIVE(bank) == 0) {
        sprintf(dbg, "[DS] GetStatus: bank %d not active\n", bank);
        OutputDebugStringA(dbg);
        return 0;
    }
    int* buf = BANK_DSBUF(bank);
    if (buf == NULL) {
        sprintf(dbg, "[DS] GetStatus: bank %d has no buffer\n", bank);
        OutputDebugStringA(dbg);
        return 0;
    }

    if (buf == DS_STUB_BUF) {
        if (g_BankVoices[bank] != NULL) {
            XAUDIO2_VOICE_STATE state = {};
            g_BankVoices[bank]->GetState(&state);
            if (state.BuffersQueued > 0) {
                BANK_STATUS(bank) = 1;
                return 1;
            }
        }
        BANK_STATUS(bank) = 0;
        return 0;
    }

    DWORD status = 0;
    int hr = ((int(*)(int*, DWORD*))buf[9])(buf, &status);
    *(int*)((BYTE*)this + 4) = hr;
    if (hr != 0) return 0;
    if (status != 0 && BANK_STATUS(bank) == 1) return 1;
    return 0;
}

// ============================================================================
// DirectSound::Release (0x0041e7c0)
// ============================================================================
void DirectSound::Release(void)
{
    if (*(int*)((BYTE*)this + 0x10) == 0) {
        printf("DirectSound::Release: not initialized\n");
        return;
    }
    if (*(int*)((BYTE*)this + 0x14) != 0) {
        printf("DirectSound::Release: already released\n");
        return;
    }
    *(int*)((BYTE*)this + 0x14) = 1;
    *(int*)((BYTE*)this + 0x10) = 0;

    // Stop primary buffer
    int* prim = *(int**)((BYTE*)this + 0x93C);
    if (prim != NULL) {
        ((void(*)(int*))prim[18])(prim);
        ((void(*)(int*))prim[2])(prim);
        *(int**)((BYTE*)this + 0x93C) = NULL;
    }

    // Destroy all bank buffers
    for (int bank = 1; bank <= 80; bank++) {
        int* buf = BANK_DSBUF(bank);
        if (BANK_ACTIVE(bank) != 0 && buf != NULL) {
            ((void(*)(int*))buf[2])(buf);
            BANK_DSBUF(bank) = NULL;
        }
    }

    // Release DirectSound device
    int* dev = *(int**)this;
    if (dev != NULL) {
        ((void(*)(int*))dev[2])(dev);
        *(int**)this = NULL;
    }
}

// ============================================================================
// DirectSound::Reload (0x0041e880) — re-init after Release
// ============================================================================
void DirectSound::Reload(void)
{
    if (*(int*)((BYTE*)this + 0x14) == 0) {
        printf("DirectSound::Reload: not released\n");
        return;
    }

    *(int*)((BYTE*)this + 0x10) = 1;

    // Re-activate all previously loaded banks
    for (int bank = 1; bank <= 80; bank++) {
        if (BANK_ACTIVE(bank) == 0) continue;
        BANK_ACTIVE(bank) = 1;
    }

    *(int*)((BYTE*)this + 0x14) = 0;
}

// ============================================================================
// DirectSound::DestroySound (0x0041eab0)
// ============================================================================
void DirectSound::DestroySound(int bank)
{
    char dbg[256];
    sprintf(dbg, "[DS] DestroySound(bank=%d)\n", bank);
    OutputDebugStringA(dbg);

    if (*(int*)((BYTE*)this + 0x10) == 0) {
        OutputDebugStringA("[DS] DestroySound: not initialized\n");
        return;
    }
    if (BANK_ACTIVE(bank) == 0) {
        sprintf(dbg, "[DS] DestroySound: bank %d already inactive\n", bank);
        OutputDebugStringA(dbg);
        return;
    }

    int* buf = BANK_DSBUF(bank);
    if (buf != NULL && buf != DS_STUB_BUF) {
        ((void(*)(int*))buf[2])(buf);
    }

    // Release XAudio2 voice if any
    if (g_BankVoices[bank] != NULL) {
        g_BankVoices[bank]->Stop(0);
        g_BankVoices[bank]->DestroyVoice();
        g_BankVoices[bank] = NULL;
    }

    BANK_DSBUF(bank) = NULL;

    BANK_ACTIVE(bank) = 0;
    BANK_STATUS(bank) = 0;
    m_bankSlots[bank - 1] = 0;

    BYTE* wavData = BANK_WAV_PTR(bank);
    if (wavData != NULL) {
        sprintf(dbg, "[DS]   freeing WAV data at %p (%lu bytes)\n", wavData, BANK_DATA_SZ(bank));
        OutputDebugStringA(dbg);
        free(wavData);
        BANK_WAV_PTR(bank) = NULL;
    }

    sprintf(dbg, "[DS] DestroySound OK: bank %d freed\n", bank);
    OutputDebugStringA(dbg);
}

// ============================================================================
// DirectSound::NewDirectSoundBuffer (0x0041eb50)
// Creates/registers a DirectSound secondary buffer slot for the given bank.
// The actual DirectX 5.0 COM buffer creation is stubbed; we set a sentinel
// so that PlaySound/StopSound/SetVol/SetPan can update bank state correctly.
// ============================================================================
int DirectSound::NewDirectSoundBuffer(DWORD* wavBlk)
{
    if (*(int*)((BYTE*)this + 0x10) == 0) {
        OutputDebugStringA("[DS] NewDirectSoundBuffer: not initialized\n");
        return 0;
    }

    char dbg[256];
    sprintf(dbg, "[DEBUG] NewDirectSoundBuffer: wavBlk=%p (bank offset=0x%X)\n",
            wavBlk, (DWORD)((BYTE*)wavBlk - (BYTE*)this));
    OutputDebugStringA(dbg);

    // Set a flag at bank+0x91C (original code writes to wavBlk[0x247])
    wavBlk[0x247] = 1;
    if ((wavBlk[1] & 4) == 0) wavBlk[0x247] = 0;

    // Store stub sentinel so PlaySound etc. know this bank has a "buffer"
    // In the stub mode, methods update bank state fields without real COM.
    int bank = ((BYTE*)wavBlk - (BYTE*)this) / 0xA2C;
    BANK_DSBUF(bank) = DS_STUB_BUF;

    sprintf(dbg, "[DEBUG] NewDirectSoundBuffer OK: bank=%d, DS_BUF=%p\n", bank, DS_STUB_BUF);
    OutputDebugStringA(dbg);

    return 1;
}

// ============================================================================
// DirectSound::StopSound (0x0041edb0)
// ============================================================================
void DirectSound::StopSound(int bank)
{
    char dbg[256];
    sprintf(dbg, "[DS] StopSound(bank=%d)\n", bank);
    OutputDebugStringA(dbg);

    if (*(int*)((BYTE*)this + 0x10) == 0) {
        OutputDebugStringA("[DS] StopSound: not initialized\n");
        return;
    }
    if (BANK_ACTIVE(bank) == 0) {
        sprintf(dbg, "[DS] StopSound: bank %d not active\n", bank);
        OutputDebugStringA(dbg);
        return;
    }
    int* buf = BANK_DSBUF(bank);
    if (buf == NULL) {
        sprintf(dbg, "[DS] StopSound: bank %d has no buffer\n", bank);
        OutputDebugStringA(dbg);
        return;
    }

    if (buf == DS_STUB_BUF) {
        if (g_BankVoices[bank] != NULL) {
            g_BankVoices[bank]->Stop(0);
            g_BankVoices[bank]->FlushSourceBuffers();
        }
        sprintf(dbg, "[XS] StopSound: bank=%d stopped\n", bank);
        OutputDebugStringA(dbg);
        BANK_STATUS(bank) = 0;
        return;
    }

    int hr = ((int(*)(int*))buf[18])(buf);
    *(int*)((BYTE*)this + 4) = hr;
    if (hr != 0) {
        sprintf(dbg, "[DS] StopSound: Stop failed (hr=0x%08X)\n", hr);
        OutputDebugStringA(dbg);
        ErrorRoutine(*(int*)((BYTE*)this + 4));
    } else {
        BANK_STATUS(bank) = 0;
    }
}

// ============================================================================
// DirectSound::PlaySound (0x0041ee60)
// ============================================================================
void DirectSound::PlaySound(int bank, unsigned int slot)
{
    char dbg[256];
    sprintf(dbg, "[DS] PlaySound(bank=%d, slot=%u)\n", bank, slot);
    OutputDebugStringA(dbg);

    if (*(int*)((BYTE*)this + 0x10) == 0) {
        OutputDebugStringA("[DS] PlaySound: not initialized\n");
        return;
    }
    if (BANK_ACTIVE(bank) == 0) {
        sprintf(dbg, "[DS] PlaySound: bank %d not active\n", bank);
        OutputDebugStringA(dbg);
        return;
    }

    BANK_SLOT(bank) = (int)slot;

    int* buf = BANK_DSBUF(bank);
    if (buf == NULL) {
        sprintf(dbg, "[DS] PlaySound: bank %d has no buffer (DS_BUF=NULL)\n", bank);
        OutputDebugStringA(dbg);
        return;
    }

    if (buf == DS_STUB_BUF) {
        // XAudio2 playback — DirectSound stub replacement
        if (g_pXAudio2 == NULL || g_pMasterVoice == NULL) {
            sprintf(dbg, "[DS] PlaySound: XAudio2 not available\n");
            OutputDebugStringA(dbg);
            BANK_STATUS(bank) = 0;
            return;
        }

        // Stop and destroy any existing voice for this bank
        if (g_BankVoices[bank] != NULL) {
            g_BankVoices[bank]->Stop(0);
            g_BankVoices[bank]->DestroyVoice();
            g_BankVoices[bank] = NULL;
        }

        // Build WAVEFORMATEX from bank metadata
        WAVEFORMATEX wfx = {};
        wfx.wFormatTag = WAVE_FORMAT_PCM;
        wfx.nChannels = BANK_CH(bank);
        wfx.nSamplesPerSec = BANK_SR(bank);
        wfx.wBitsPerSample = BANK_BPS(bank);
        wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
        wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
        wfx.cbSize = 0;

        // Skip WAV header (44 bytes) to get raw PCM data
        BYTE* wavData = BANK_WAV_PTR(bank);
        DWORD dataSize = BANK_DATA_SZ(bank);
        BYTE* pcmData = wavData;
        DWORD pcmSize = dataSize;

        // Find the "data" chunk in WAV file
        DWORD offs = 12;
        DWORD fileSize = dataSize + 44;  // approximate
        while (offs + 8 <= fileSize) {
            DWORD chunkId = *(DWORD*)(wavData + offs);
            DWORD chunkSz = *(DWORD*)(wavData + offs + 4);
            if (chunkId == 0x61746164) {  // "data"
                pcmData = wavData + offs + 8;
                pcmSize = chunkSz;
                if (pcmSize > dataSize) pcmSize = dataSize;
                break;
            }
            offs += 8 + chunkSz;
        }

        HRESULT hr = g_pXAudio2->CreateSourceVoice(&g_BankVoices[bank], &wfx, 0,
                                                      XAUDIO2_DEFAULT_FREQ_RATIO, NULL, NULL, NULL);
        if (FAILED(hr)) {
            sprintf(dbg, "[XA2] PlaySound: CreateSourceVoice failed bank=%d hr=0x%08X\n", bank, hr);
            OutputDebugStringA(dbg);
            BANK_STATUS(bank) = 0;
            return;
        }

        XAUDIO2_BUFFER xa2buf = {};
        xa2buf.Flags = 0;
        xa2buf.AudioBytes = pcmSize;
        xa2buf.pAudioData = pcmData;
        xa2buf.PlayBegin = 0;
        xa2buf.PlayLength = 0;  // entire buffer
        xa2buf.LoopBegin = 0;
        xa2buf.LoopLength = 0;
        xa2buf.LoopCount = (slot != 0) ? XAUDIO2_LOOP_INFINITE : 0;

        hr = g_BankVoices[bank]->SubmitSourceBuffer(&xa2buf, NULL);
        if (FAILED(hr)) {
            sprintf(dbg, "[XA2] PlaySound: SubmitSourceBuffer failed bank=%d hr=0x%08X\n", bank, hr);
            OutputDebugStringA(dbg);
            g_BankVoices[bank]->DestroyVoice();
            g_BankVoices[bank] = NULL;
            BANK_STATUS(bank) = 0;
            return;
        }

        hr = g_BankVoices[bank]->Start(0);
        if (FAILED(hr)) {
            sprintf(dbg, "[XA2] PlaySound: Start failed bank=%d hr=0x%08X\n", bank, hr);
            OutputDebugStringA(dbg);
            g_BankVoices[bank]->DestroyVoice();
            g_BankVoices[bank] = NULL;
            BANK_STATUS(bank) = 0;
            return;
        }

        // Apply current volume
        int vol = BANK_VOL(bank);
        float xa2vol = (vol <= -10000) ? 0.0f : 1.0f - ((float)(-vol) / 10000.0f);
        if (xa2vol > 1.0f) xa2vol = 1.0f;
        g_BankVoices[bank]->SetVolume(xa2vol);

        sprintf(dbg, "[XA2] PlaySound OK: bank=%d ch=%d sr=%lu bps=%d pcm=%lu bytes loop=%s\n",
                bank, wfx.nChannels, wfx.nSamplesPerSec, wfx.wBitsPerSample,
                pcmSize, (xa2buf.LoopCount == XAUDIO2_LOOP_INFINITE) ? "yes" : "no");
        OutputDebugStringA(dbg);
        BANK_STATUS(bank) = 1;
        return;
    }

    // Real COM path (not yet operational)
    DWORD status = 0;
    int hr = ((int(*)(int*, DWORD*))buf[9])(buf, &status);
    *(int*)((BYTE*)this + 4) = hr;
    if (hr != 0) {
        OutputDebugStringA("[DS] PlaySound: GetStatus failed\n");
        return;
    }

    if ((status & 1) != 0 && bank != 0) {
        hr = ((int(*)(int*, int))buf[13])(buf, 0);
        *(int*)((BYTE*)this + 4) = hr;
        if (hr != 0) {
            OutputDebugStringA("[DS] PlaySound: SetCurrentPosition failed\n");
            return;
        }
    }

    hr = ((int(*)(int*, int, int, int))buf[12])(buf, 0, 0, slot != 0);
    *(int*)((BYTE*)this + 4) = hr;
    if (hr != 0) {
        OutputDebugStringA("[DS] PlaySound: Play failed\n");
        return;
    }
    BANK_STATUS(bank) = 1;
}

// ============================================================================
// DirectSound::SetVol (0x0041efa0)
// ============================================================================
void DirectSound::SetVol(int bank, int vol)
{
    char dbg[256];
    sprintf(dbg, "[DS] SetVol(bank=%d, vol=%d)\n", bank, vol);
    OutputDebugStringA(dbg);

    if (*(int*)((BYTE*)this + 0x10) == 0) {
        OutputDebugStringA("[DS] SetVol: not initialized\n");
        return;
    }
    if (vol > 0 || vol < -10000) {
        sprintf(dbg, "[DS] SetVol: vol=%d out of range\n", vol);
        OutputDebugStringA(dbg);
        return;
    }
    if (BANK_ACTIVE(bank) == 0) {
        sprintf(dbg, "[DS] SetVol: bank %d not active\n", bank);
        OutputDebugStringA(dbg);
        return;
    }
    int* buf = BANK_DSBUF(bank);
    if (buf == NULL) {
        sprintf(dbg, "[DS] SetVol: bank %d has no buffer\n", bank);
        OutputDebugStringA(dbg);
        return;
    }

    if (buf == DS_STUB_BUF) {
        // MarniSound volume: -1 = max, -9999 = min, -10000 = mute
        float xa2vol;
        if (vol <= -10000) {
            xa2vol = 0.0f;
        } else {
            xa2vol = 1.0f - ((float)(-vol) / 10000.0f);
            if (xa2vol > 1.0f) xa2vol = 1.0f;
        }
        if (g_BankVoices[bank] != NULL) {
            g_BankVoices[bank]->SetVolume(xa2vol);
        }
        BANK_VOL(bank) = vol;
        return;
    }

    int hr = ((int(*)(int*, int))buf[15])(buf, vol);
    *(int*)((BYTE*)this + 4) = hr;
    if (hr != 0) {
        sprintf(dbg, "[DS] SetVol: SetVolume failed (hr=0x%08X)\n", hr);
        OutputDebugStringA(dbg);
        ErrorRoutine(*(int*)((BYTE*)this + 4));
    } else {
        BANK_VOL(bank) = vol;
    }
}

// ============================================================================
// DirectSound::SetPan (0x0041f080)
// ============================================================================
void DirectSound::SetPan(int bank, int pan)
{
    char dbg[256];
    sprintf(dbg, "[DS] SetPan(bank=%d, pan=%d)\n", bank, pan);
    OutputDebugStringA(dbg);

    if (*(int*)((BYTE*)this + 0x10) == 0) {
        OutputDebugStringA("[DS] SetPan: not initialized\n");
        return;
    }
    if (pan > 10000 || pan < -10000) {
        sprintf(dbg, "[DS] SetPan: pan=%d out of range\n", pan);
        OutputDebugStringA(dbg);
        return;
    }
    if (BANK_ACTIVE(bank) == 0) {
        sprintf(dbg, "[DS] SetPan: bank %d not active\n", bank);
        OutputDebugStringA(dbg);
        return;
    }
    int* buf = BANK_DSBUF(bank);
    if (buf == NULL) {
        sprintf(dbg, "[DS] SetPan: bank %d has no buffer\n", bank);
        OutputDebugStringA(dbg);
        return;
    }

    if (buf == DS_STUB_BUF) {
        sprintf(dbg, "[DS] SetPan: stub mode, PAN=%d\n", pan);
        OutputDebugStringA(dbg);
        BANK_PAN(bank) = pan;
        return;
    }

    int hr = ((int(*)(int*, int))buf[16])(buf, pan);
    *(int*)((BYTE*)this + 4) = hr;
    if (hr != 0) {
        sprintf(dbg, "[DS] SetPan: SetPan failed (hr=0x%08X)\n", hr);
        OutputDebugStringA(dbg);
        ErrorRoutine(*(int*)((BYTE*)this + 4));
    } else {
        BANK_PAN(bank) = pan;
    }
}

// ============================================================================
// DirectSound::GetVol (0x0041f160)
// ============================================================================
int DirectSound::GetVol(int bank)
{
    if (*(int*)((BYTE*)this + 0x10) == 0) {
        OutputDebugStringA("[DS] GetVol: not initialized\n");
        return 0;
    }
    if (BANK_ACTIVE(bank) == 0) {
        char dbg[256];
        sprintf(dbg, "[DS] GetVol: bank %d not active\n", bank);
        OutputDebugStringA(dbg);
        return 0;
    }
    int vol = BANK_VOL(bank);
    char dbg[256];
    sprintf(dbg, "[DS] GetVol(bank=%d) = %d\n", bank, vol);
    OutputDebugStringA(dbg);
    return vol;
}

// ============================================================================
// DirectSound::CreateSound (0x0041f1c0)
// ============================================================================
int DirectSound::CreateSound(const char* wavName)
{
    if (wavName == NULL || *(int*)((BYTE*)this + 0x10) == 0) {
        OutputDebugStringA("[DEBUG] CreateSound: not initialized or null name\n");
        return 0;
    }

    int bank;
    for (bank = 1; bank <= 80; bank++) {
        if (m_bankSlots[bank - 1] == 0) break;
    }
    if (bank > 80) {
        OutputDebugStringA("[DEBUG] CreateSound: no free bank slots\n");
        return 0;
    }

    char dbg[256];
    sprintf(dbg, "[DEBUG] CreateSound: '%s' -> bank %d\n", wavName, bank);
    OutputDebugStringA(dbg);

    char resolvedPath[MAX_PATH];
    const char* actualPath = wavName;
    if (ResolveAssetPath(wavName, resolvedPath, sizeof(resolvedPath)) != NULL) {
        actualPath = resolvedPath;
    }

    sprintf(dbg, "[DEBUG]   resolved path: '%s'\n", actualPath);
    OutputDebugStringA(dbg);

    HANDLE hFile = CreateFileA(actualPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        sprintf(dbg, "[DEBUG]   FAILED to open file (err=%d)\n", GetLastError());
        OutputDebugStringA(dbg);
        return 0;
    }

    DWORD fileSize = GetFileSize(hFile, NULL);
    sprintf(dbg, "[DEBUG]   fileSize=%lu bytes\n", fileSize);
    OutputDebugStringA(dbg);

    BYTE* wavData = (BYTE*)malloc(fileSize);
    if (wavData == NULL) { CloseHandle(hFile); return 0; }
    DWORD bytesRead;
    if (!ReadFile(hFile, wavData, fileSize, &bytesRead, NULL)) {
        free(wavData); CloseHandle(hFile); return 0;
    }
    CloseHandle(hFile);

    if (bytesRead < 44 || wavData[0] != 'R' || wavData[1] != 'I' ||
        wavData[2] != 'F' || wavData[3] != 'F') {
        sprintf(dbg, "[DEBUG]   invalid WAV header\n");
        OutputDebugStringA(dbg);
        free(wavData); return 0;
    }

    DWORD offs = 12;
    WORD channels = 1;
    DWORD sampleRate = 22050;
    WORD bitsPerSample = 8;
    DWORD dataSize = 0;
    while (offs + 8 <= bytesRead) {
        DWORD chunkSize = *(DWORD*)(wavData + offs + 4);
        if (*(DWORD*)(wavData + offs) == 0x20746D66) {
            if (chunkSize >= 16) {
                channels = *(WORD*)(wavData + offs + 10);
                sampleRate = *(DWORD*)(wavData + offs + 12);
                bitsPerSample = *(WORD*)(wavData + offs + 22);
            }
        } else if (*(DWORD*)(wavData + offs) == 0x61746164) {
            dataSize = chunkSize;
        }
        offs += 8 + chunkSize;
    }

    if (dataSize == 0) {
        sprintf(dbg, "[DEBUG]   no data chunk found\n");
        OutputDebugStringA(dbg);
        free(wavData); return 0;
    }

    sprintf(dbg, "[DEBUG]   WAV OK: %lu Hz, %d ch, %d bit, %lu bytes PCM\n",
            sampleRate, channels, bitsPerSample, dataSize);
    OutputDebugStringA(dbg);

    BANK_WAV_PTR(bank) = wavData;
    BANK_DATA_SZ(bank) = dataSize;
    BANK_SR(bank) = sampleRate;
    BANK_CH(bank) = channels;
    BANK_BPS(bank) = bitsPerSample;
    BANK_PLAYSR(bank) = (int)sampleRate;
    BANK_PAN(bank) = 400;
    BANK_VOL(bank) = 0;
    BANK_STATUS(bank) = 0;
    BANK_ACTIVE(bank) = 1;

    // Create the DirectSound buffer for this bank
    NewDirectSoundBuffer((DWORD*)BANK_BASE(bank));

    sprintf(dbg, "[DEBUG]   BANK_DSBUF[%d] = %p\n", bank, (void*)BANK_DSBUF(bank));
    OutputDebugStringA(dbg);

    char* nameDest = BANK_NAME(bank);
    size_t nameLen = strlen(wavName);
    if (nameLen > 255) nameLen = 255;
    memcpy(nameDest, wavName, nameLen);
    nameDest[nameLen] = '\0';

    m_bankSlots[bank - 1] = (int)BANK_BASE(bank);

    sprintf(dbg, "[DEBUG] CreateSound OK: bank=%d, slot=%p, wav=%p\n",
            bank, (void*)m_bankSlots[bank - 1], (void*)wavData);
    OutputDebugStringA(dbg);

    return bank;
}

// ============================================================================
// DirectSound::ErrorRoutine (0x0041f570)
// ============================================================================
void DirectSound::ErrorRoutine(int code)
{
    unsigned int c = (unsigned int)code;
    if (c < 0x80004002) {
        if (c == 0x80004001) { printf("DS: not implemented\n"); return; }
        if (c == 0) { printf("DS: OK\n"); return; }
        return;
    }
    if (c < 0x80040111) {
        if (c == 0x80040110) { printf("DS: buffer lost\n"); return; }
        if (c == 0x80004005) { printf("DS: E_FAIL\n"); return; }
        return;
    }
    if (c > 0x80070057) {
        switch (c) {
        case 0x8878000a: printf("DS: buffer too small\n"); return;
        case 0x8878001e: printf("DS: bad format\n"); return;
        case 0x88780032: printf("DS: buffer lost\n"); return;
        case 0x88780046: printf("DS: format not supported\n"); return;
        case 0x88780064: printf("DS: invalid call\n"); return;
        case 0x88780078: printf("DS: priority level high\n"); return;
        case 0x88780082: printf("DS: buffer already locked\n"); return;
        case 0x88780096: printf("DS: no hardware\n"); return;
        }
        return;
    }
    if (c == 0x80070057) { printf("DS: invalid parameter\n"); return; }
    if (c == 0x8007000e) { printf("DS: out of memory\n"); return; }
}

// ============================================================================
// Wrappers using g_pDirectSound
// ============================================================================
int loadSndBankFromWav(const char* path)
{
    char dbg[256];
    sprintf(dbg, "[DEBUG] loadSndBankFromWav: '%s' (sync, g_pDS=%p)\n", path, g_pDirectSound);
    OutputDebugStringA(dbg);

    if (g_pDirectSound != NULL && *(int*)((BYTE*)g_pDirectSound + 0x10) != 0) {
        // Call CreateSound synchronously — the original ExecAsync was an empty
        // stub (FUN_00420290). The async wrapper would overwrite the single
        // callback slot on each call, so batch loading via LoadSoundBank
        // would lose all but the last callback.
        g_sndload_bank_index = g_pDirectSound->CreateSound(path);
        sprintf(dbg, "[DEBUG] loadSndBankFromWav -> bank %d\n", g_sndload_bank_index);
        OutputDebugStringA(dbg);
    } else {
        OutputDebugStringA("[DEBUG] loadSndBankFromWav: g_pDirectSound NOT READY\n");
        g_sndload_bank_index = 0;
    }
    return g_sndload_bank_index;
}

void destroySndBank(int bank)
{
    g_CurBank = bank;
    if (g_pDirectSound != NULL && bank != 0) {
        ExecAsync((void*)destroyCurSndBank);
    }
}

void setSndStop(int bank)
{
    g_CurBank = bank;
    if (g_pDirectSound != NULL && bank != 0) {
        ExecAsync((void*)StopCurSnd);
    }
}

void playSnd(int bank, int slot)
{
    g_CurSlot = (short)slot;
    g_CurBank = bank;
    if (g_pDirectSound != NULL && bank != 0) {
        ExecAsync((void*)playCurSnd);
    }
}

void SetSndSlot(int bank, int slot)
{
    g_CurSlot = (short)slot;
    g_CurBank = bank;
    if (g_pDirectSound != NULL && bank != 0) {
        ExecAsync((void*)PlayCurSnd);
    }
}

void set_volume(int bank, int volume)
{
    if (g_pDirectSound == NULL || *(int*)((BYTE*)g_pDirectSound + 0x10) == 0) return;
    g_CurBank = bank;
    g_SoundPanVol = volume;
    if (volume > -1) g_SoundPanVol = -1;
    if (g_SoundPanVol < -9999) g_SoundPanVol = -9999;
    if (g_pDirectSound != NULL && bank != 0) {
        ExecAsync((void*)setCurSndVol);
    }
}

void pan_set(int bank, int pan)
{
    if (g_pDirectSound == NULL || *(int*)((BYTE*)g_pDirectSound + 0x10) == 0) return;
    g_CurBank = bank;
    g_SoundPanVol = pan;
    if (pan > 10000) g_SoundPanVol = 10000;
    if (g_SoundPanVol < -10000) g_SoundPanVol = -10000;
    if (g_pDirectSound != NULL && bank != 0) {
        ExecAsync((void*)setCurSndPan);
    }
}

int getSndVol(int bank)
{
    if (g_pDirectSound == NULL || *(int*)((BYTE*)g_pDirectSound + 0x10) == 0) return 0;
    g_CurBank = bank;
    if (g_pDirectSound != NULL && bank != 0) {
        ExecAsync((void*)getCurSndBankVol);
    }
    return g_SoundPanVol;
}

static void apply_vol_delta(int* bankPtr, int delta)
{
    if (bankPtr == NULL || *bankPtr == 0) return;
    int vol = getSndVol(*bankPtr);
    if (vol == 2000) vol = -9999;
    vol += delta;
    if (vol > -1) vol = -1;
    if (vol < -9999) vol = -9999;
    set_volume(*bankPtr, vol);
}

void UpdateSoundFade(int steps)
{
    if (g_BgmSoundBank != 0) apply_vol_delta(&g_BgmSoundBank, steps);
    for (int* p = g_SfxBanks; p < g_SfxBanks + 32; p += 2) apply_vol_delta(p, steps);
    for (int* p = g_RoomSfxBanks; p < &g_SndRampDirection; p += 2) apply_vol_delta(p, steps);
    for (int* p = g_CharacterSfxBanks; p < g_CharacterSfxBanks + 48; p += 2) apply_vol_delta(p, steps);
    for (int* p = g_emSndBanks; p < g_emSndBanks + 48; p += 2) apply_vol_delta(p, steps);
    int* fadeStep = g_SndFadeStepTbl;
    for (int* p = g_SndBank; p < g_SndBank + 48; p += 2) {
        if (*p != 0 && *fadeStep > 0) { apply_vol_delta(p, steps); (*fadeStep)--; }
        fadeStep++;
    }
}

// ============================================================================
// Async callbacks
// ============================================================================
void getCurSndStat(void)
{
    OutputDebugStringA("[DS:async] getCurSndStat\n");
    g_setVolResult = 1;
}

void setCurSndPan(void)
{
    OutputDebugStringA("[DS:async] setCurSndPan\n");
    g_SndPanSet_result = 0;
}

void setCurSndVol(void)
{
    OutputDebugStringA("[DS:async] setCurSndVol\n");
    g_setVolResult = 0;
}

void loadWav(void)
{
    char dbg[256];
    sprintf(dbg, "[DS:async] loadWav: path='%s'\n", g_wavName ? g_wavName : "NULL");
    OutputDebugStringA(dbg);

    if (g_pDirectSound != NULL && *(int*)((BYTE*)g_pDirectSound + 0x10) != 0) {
        g_sndload_bank_index = g_pDirectSound->CreateSound(g_wavName);
        sprintf(dbg, "[DS:async] loadWav -> bank index %d\n", g_sndload_bank_index);
        OutputDebugStringA(dbg);
    } else {
        OutputDebugStringA("[DS:async] loadWav: DirectSound not ready\n");
    }
}

void destroyCurSndBank(void)
{
    char dbg[256];
    sprintf(dbg, "[DS:async] destroyCurSndBank: bank=%d\n", g_CurBank);
    OutputDebugStringA(dbg);

    if (g_pDirectSound != NULL && *(int*)((BYTE*)g_pDirectSound + 0x10) != 0) {
        g_pDirectSound->DestroySound(g_CurBank);
    }
}

void StopCurSnd(void)
{
    char dbg[256];
    sprintf(dbg, "[DS:async] StopCurSnd: bank=%d\n", g_CurBank);
    OutputDebugStringA(dbg);

    if (g_pDirectSound != NULL && *(int*)((BYTE*)g_pDirectSound + 0x10) != 0) {
        g_pDirectSound->StopSound(g_CurBank);
    }
}

void playCurSnd(void)
{
    char dbg[256];
    sprintf(dbg, "[DS:async] playCurSnd: bank=%d slot=%d\n", g_CurBank, (int)g_CurSlot);
    OutputDebugStringA(dbg);

    if (g_pDirectSound != NULL && *(int*)((BYTE*)g_pDirectSound + 0x10) != 0) {
        g_pDirectSound->PlaySound(g_CurBank, (unsigned int)g_CurSlot);
    }
}

void PlayCurSnd(void)
{
    char dbg[256];
    sprintf(dbg, "[DS:async] PlayCurSnd: bank=%d slot=%d\n", g_CurBank, (int)g_CurSlot);
    OutputDebugStringA(dbg);

    if (g_pDirectSound != NULL && *(int*)((BYTE*)g_pDirectSound + 0x10) != 0) {
        g_pDirectSound->PlaySound(g_CurBank, (unsigned int)g_CurSlot);
    }
}

void getCurSndBankVol(void)
{
    char dbg[256];
    sprintf(dbg, "[DS:async] getCurSndBankVol: bank=%d\n", g_CurBank);
    OutputDebugStringA(dbg);

    if (g_pDirectSound != NULL && *(int*)((BYTE*)g_pDirectSound + 0x10) != 0) {
        g_SoundPanVol = g_pDirectSound->GetVol(g_CurBank);
    } else {
        g_SoundPanVol = 0;
    }
}

void InitializeSoundSystem(void)
{
    OutputDebugStringA("[DS] InitializeSoundSystem: creating DirectSound instance\n");
    DirectSound* mgr = new DirectSound(g_MainWindowHandle);
    g_pDirectSound = mgr;
    g_SoundManager = (void*)mgr;

    // Initialize XAudio2 as DirectSound replacement
    char dbg[128];
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE && hr != S_FALSE) {
        OutputDebugStringA("[XA2] CoInitializeEx failed\n");
    }

    hr = XAudio2Create(&g_pXAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr)) {
        sprintf(dbg, "[XA2] XAudio2Create failed: 0x%08X\n", hr);
        OutputDebugStringA(dbg);
        g_pXAudio2 = NULL;
    } else {
        hr = g_pXAudio2->CreateMasteringVoice(&g_pMasterVoice, 2, 22050, 0, NULL, NULL);
        if (FAILED(hr)) {
            sprintf(dbg, "[XA2] CreateMasteringVoice failed: 0x%08X\n", hr);
            OutputDebugStringA(dbg);
            g_pMasterVoice = NULL;
        } else {
            OutputDebugStringA("[XA2] XAudio2 engine ready\n");
        }
    }

    memset(g_BankVoices, 0, sizeof(g_BankVoices));
    OutputDebugStringA("[DS] InitializeSoundSystem OK\n");
}

void CleanupSoundManagerResources(void)
{
    OutputDebugStringA("[DS] CleanupSoundManagerResources\n");
    if (g_pDirectSound == NULL) return;
    for (int i = 1; i <= 79; i++) {
        g_pDirectSound->DestroySound(i);
    }

    // Shutdown XAudio2
    if (g_BankVoices) {
        for (int i = 1; i <= 80; i++) {
            if (g_BankVoices[i] != NULL) {
                g_BankVoices[i]->Stop(0);
                g_BankVoices[i]->DestroyVoice();
                g_BankVoices[i] = NULL;
            }
        }
    }
    if (g_pMasterVoice != NULL) {
        g_pMasterVoice->DestroyVoice();
        g_pMasterVoice = NULL;
    }
    if (g_pXAudio2 != NULL) {
        g_pXAudio2->Release();
        g_pXAudio2 = NULL;
    }
    CoUninitialize();
}

void PauseGameSoundsCallback(void) { PauseSounds(); }
void ResumeGameSoundsCallback(void) { ResumePausedSounds(); }

int findAndOpenFile(char* path)
{
    return (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) ? 1 : 0;
}
