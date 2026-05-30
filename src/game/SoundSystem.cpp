// SoundSystem.cpp - Sound system implementation
// All functions decompiled from Ghidra with original addresses
#include "../Globals.h"
#include "../marni/MarniSound.h"

// ============================================================================
// sounds_reset (0x0047eb70)
// Destroys all sound banks and resets all sound state.
// ============================================================================
void sounds_reset(void)
{
    if (g_BgmSoundBank != 0) {
        destroySndBank(g_BgmSoundBank);
    }
    g_BgmSoundBank = 0;

    for (int i = 0; i < 64; i += 2) {
        if (g_SfxBanks[i] != 0) {
            destroySndBank(g_SfxBanks[i]);
            g_SfxBanks[i] = 0;
        }
        g_SfxBanks[i + 1] = 0;
    }
    for (int i = 0; i < 64; i += 2) {
        if (g_RoomSfxBanks[i] != 0) {
            destroySndBank(g_RoomSfxBanks[i]);
            g_RoomSfxBanks[i] = 0;
        }
        g_RoomSfxBanks[i + 1] = 0;
    }
    for (int i = 0; i < 64; i += 2) {
        if (g_CharacterSfxBanks[i] != 0) {
            destroySndBank(g_CharacterSfxBanks[i]);
            g_CharacterSfxBanks[i] = 0;
        }
        g_CharacterSfxBanks[i + 1] = 0;
    }
    for (int i = 0; i < 64; i += 2) {
        if (g_emSndBanks[i] != 0) {
            destroySndBank(g_emSndBanks[i]);
            g_emSndBanks[i] = 0;
        }
        g_emSndBanks[i + 1] = 0;
    }
    for (int i = 0; i < 64; i += 3) {
        if (g_SndBank[i] != 0) {
            destroySndBank(g_SndBank[i]);
            g_SndBank[i] = 0;
        }
        g_SndBank[i + 1] = 0;
        g_SndBank[i + 2] = 0;
    }

    g_SfxVolume = -1;
    g_BGM_STATE = 0xFF;
}

// ============================================================================
// LoadSoundBank (0x0047ed30)
// Loads a sound effects bank from file into memory.
// sound_bank_id: 0-110, selects a sub-table of up to 16 filenames.
// Each non-null entry in the sub-table is loaded into a g_SfxBanks slot.
// ============================================================================
void LoadSoundBank(int sound_bank_id, void* buffer)
{
    if (sound_bank_id > 110) {
        sound_bank_id = 15;
    }

    const char** subtable = NULL;
    if (sound_bank_id >= 0 && sound_bank_id < 16) {
        subtable = g_SoundBanksTable[sound_bank_id];
    }
    if (subtable == NULL) {
        OutputDebugStringA("[DEBUG] LoadSoundBank: subtable is NULL\n");
        return;
    }

    char dbg[256];
    sprintf(dbg, "[DEBUG] LoadSoundBank: bank_id=%d, g_pDirectSound=%p\n", sound_bank_id, g_pDirectSound);
    OutputDebugStringA(dbg);

    int iVar6 = 0;
    int* bankPtr = g_SfxBanks;
    int* endPtr = g_SfxBanks + 32;  // end of original g_SfxBanks area

    while (bankPtr < endPtr) {
        if (bankPtr[0] != 0) {
            destroySndBank(bankPtr[0]);
            bankPtr[0] = 0;
        }
        *((char*)&bankPtr[1]) = 0;
        *((char*)&bankPtr[1] + 1) = 0;

        const char* filename = *(const char**)((BYTE*)subtable + iVar6);
        if (filename != NULL) {
            char path[256];
            sprintf(path, ".\\usa\\sound\\%s.wav", filename);

            sprintf(dbg, "[DEBUG] LoadSoundBank: loading '%s' ...\n", path);
            OutputDebugStringA(dbg);

            int bank = loadSndBankFromWav(path);

            sprintf(dbg, "[DEBUG] LoadSoundBank: -> handle=%d\n", bank);
            OutputDebugStringA(dbg);

            bankPtr[0] = bank;
            if (bank != 0) {
                pan_set(bank, 0);
                set_volume(bank, g_SfxVolume);
            }
        } else {
            sprintf(dbg, "[DEBUG] LoadSoundBank: slot is NULL\n");
            OutputDebugStringA(dbg);
        }

        iVar6 += 4;
        bankPtr += 2;
    }
}

// ============================================================================
// play_sfx (0x0047fb10)
// Plays a sound effect from the specified bank type.
// bank: 0=room, 1=sfx, 2=enemy, 3=character, 4=special
// ============================================================================
void play_sfx(int bank, int soundId)
{
    int handle = 0;

    switch (bank) {
    case 0:
        if (soundId > 1) return;
        handle = g_RoomSfxBanks[soundId * 2];
        break;

    case 1:
        if ((g_InputFlags & 0x200000) != 0) {
            if (soundId > 15) return;
        } else {
            if (soundId > 31) return;
            if (soundId > 15) soundId -= 16;
        }
        handle = g_SfxBanks[soundId * 2];
        break;

    case 2:
        if (soundId > 47) return;
        handle = g_emSndBanks[soundId * 2];
        break;

    case 3:
        if (soundId > 15) return;
        handle = g_CharacterSfxBanks[soundId * 2];
        break;

    case 4:
        if (soundId < 48 && g_SndBank[0] != 0) {
            SetSndSlot(g_SndBank[0], g_snd_slot_00ac99d5);
        }
        return;

    default:
        return;
    }

    if (handle != 0) {
        SetSndSlot(handle, 0);
    }
}

// ============================================================================
// PauseSounds (0x0047b...)
// Pauses all currently playing sounds.
// ============================================================================
void PauseSounds(void)
{
    if (g_BgmSoundBank != 0) {
        if (getSndStat(g_BgmSoundBank) == 1) {
            setSndStop(g_BgmSoundBank);
            g_BgmPaused = 1;
        }
    }

    for (int i = 0; i < 64; i += 3) {
        if (g_SndBank[i] != 0) {
            if (getSndStat(g_SndBank[i]) == 1) {
                setSndStop(g_SndBank[i]);
                *(char*)&g_SndBank[i + 2] = 1;
            }
        }
    }
}

// ============================================================================
// ResumePausedSounds (0x0047b...)
// Resumes all paused sounds.
// ============================================================================
void ResumePausedSounds(void)
{
    if (g_BgmSoundBank != 0 && g_BgmPaused == 1) {
        playSnd(g_BgmSoundBank, 0);
        g_BgmPaused = 0;
    }

    for (int i = 0; i < 64; i += 3) {
        if (g_SndBank[i] != 0 && *(char*)&g_SndBank[i + 2] == 1) {
            playSnd(g_SndBank[i], *(char*)&g_SndBank[i + 1]);
            *(char*)&g_SndBank[i + 2] = 0;
        }
    }
}

// ============================================================================
// UpdateSoundFadeState (0x0047b...)
// Handles sound fade state machine.
// ============================================================================
void UpdateSoundFadeState(void)
{
    if (g_SndFadeType < 0) {
        g_SndFadeType++;
        if (g_SndFadeType == -1) {
            UpdateSoundFade(-1);
            g_SndFadeType = 0;
            return;
        }
        if (g_SndFadeType == -2) {
            for (int i = 0; i < 64; i += 3) {
                if (g_SndBank[i] != 0) {
                    setSndStop(g_SndBank[i]);
                    destroySndBank(g_SndBank[i]);
                    g_SndBank[i] = 0;
                }
            }
            return;
        }
        if (g_SndFadeType == -4) {
            if (g_BgmSoundBank != 0) {
                setSndStop(g_BgmSoundBank);
            }
            return;
        }
        if (g_SndFadeType == -29 && g_BGM_STATE != 0xFF) {
            for (int i = 0; i < 64; i += 3) {
                if (g_SndBank[i] != 0) {
                    setSndStop(g_SndBank[i]);
                }
            }
            return;
        }
    } else {
        UpdateSoundFade(g_SndDistSteps);
    }
}

// ============================================================================
// UpdateSoundDecay (0x0047b...)
// Updates sound volume ramp (decay/fade-out over time).
// ============================================================================
void UpdateSoundDecay(void)
{
    int bank = g_SndBank[g_SndRampBankIndex * 3];
    if (bank != 0) {
        int vol = getSndVol(bank);
        if (vol == 2000) {
            vol = -9999;
        }
        g_SndRampFramesLeft--;
        g_SndRampCurrentVolume = vol + (400 / (g_SndRampFramesLeft + 1) * g_SndRampDirection) / 100;
        set_volume(bank, g_SndRampCurrentVolume);

        if (g_SndRampCurrentVolume < -10000 || g_SndRampFramesLeft <= 0) {
            g_SndRampDirection = 0;
            g_SndRampFramesLeft = 0;
            g_SndRampCurrentVolume = 0;
            setSndStop(bank);
            set_volume(bank, -1);
        }
    }
}

// ============================================================================
// UpdateMusicWaitState (0x0047b...)
// Checks if BGM has finished playing; clears wait flag if done.
// ============================================================================
void UpdateMusicWaitState(void)
{
    if (g_BgmSoundBank != 0 && getSndStat(g_BgmSoundBank) == 1) {
        return;
    }
    g_main_state_flags &= 0xFFFDFFFF;
    g_WaitForMusicTimer = 0;
}

// ============================================================================
// PauseGameSoundsAsync (0x0047...)
// Queues async callback to pause all game sounds (used during FMV).
// ============================================================================
void PauseGameSoundsAsync(void)
{
    ExecAsync((void*)PauseGameSoundsCallback);
}

// ============================================================================
// ResumeGameSoundsAsync (0x0047...)
// Queues async callback to resume all game sounds (after FMV).
// ============================================================================
void ResumeGameSoundsAsync(void)
{
    ExecAsync((void*)ResumeGameSoundsCallback);
}

// ============================================================================
// ProbeWaveOutDevicesAndCacheVolume (0x0047...)
// Enumerates wave out devices and caches the current volume setting.
// ============================================================================
void ProbeWaveOutDevicesAndCacheVolume(void)
{
    UINT numDevs = waveOutGetNumDevs();
    HWAVEOUT hWaveOut = NULL;

    for (UINT devId = 0; devId < numDevs; devId++) {
        WAVEOUTCAPSA caps;
        if (waveOutGetDevCapsA(devId, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            WAVEFORMATEX wfx = {};
            wfx.wFormatTag = WAVE_FORMAT_PCM;
            wfx.wBitsPerSample = 8;

            if ((caps.dwFormats & WAVE_FORMAT_48S16) != 0) {
                wfx.nSamplesPerSec = 44100;
                wfx.nChannels = 2;
            } else {
                wfx.nSamplesPerSec = 22050;
                wfx.nChannels = caps.wChannels;
            }

            wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
            wfx.nAvgBytesPerSec = wfx.nBlockAlign * wfx.nSamplesPerSec;
            wfx.cbSize = 0;

            if (waveOutOpen(&hWaveOut, devId, &wfx, 0, 0, 0) == MMSYSERR_NOERROR) {
                waveOutGetVolume(hWaveOut, &g_CachedWaveOutVolume);
            }
        }
        if (hWaveOut != NULL) {
            waveOutClose(hWaveOut);
            hWaveOut = NULL;
        }
    }
}

// ============================================================================
// StartSoundSystemAsync (0x0047...)
// Sets the main window handle and queues async sound system initialization.
// ============================================================================
void StartSoundSystemAsync(HWND hwnd)
{
    g_MainWindowHandle = hwnd;
    OutputDebugStringA("[DEBUG] StartSoundSystemAsync: calling InitializeSoundSystem directly\n");
    // Original ExecAsync(InitializeSoundSystem) was a no-op (stub at 0x00420290).
    // TaskScheduler_Reset() would clear the callback anyway, so call it directly.
    InitializeSoundSystem();
}

// ============================================================================
// RestoreWaveOutVolume (0x0047...)
// Enumerates wave out devices and restores the cached volume.
// ============================================================================
void RestoreWaveOutVolume(void)
{
    UINT numDevs = waveOutGetNumDevs();
    HWAVEOUT hWaveOut = NULL;

    for (UINT devId = 0; devId < numDevs; devId++) {
        WAVEOUTCAPSA caps;
        if (waveOutGetDevCapsA(devId, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            WAVEFORMATEX wfx = {};
            wfx.wFormatTag = WAVE_FORMAT_PCM;
            wfx.wBitsPerSample = 8;

            if ((caps.dwFormats & WAVE_FORMAT_48S16) != 0) {
                wfx.nSamplesPerSec = 44100;
                wfx.nChannels = 2;
            } else {
                wfx.nSamplesPerSec = 22050;
                wfx.nChannels = caps.wChannels;
            }

            wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
            wfx.nAvgBytesPerSec = wfx.nBlockAlign * wfx.nSamplesPerSec;
            wfx.cbSize = 0;

            if (waveOutOpen(&hWaveOut, devId, &wfx, 0, 0, 0) == MMSYSERR_NOERROR) {
                waveOutSetVolume(hWaveOut, g_CachedWaveOutVolume);
            }
        }
        if (hWaveOut != NULL) {
            waveOutClose(hWaveOut);
            hWaveOut = NULL;
        }
    }
}

// ============================================================================
// getSndStat (0x0047...)
// Returns the status of a sound bank (0=stopped, 1=playing).
// ============================================================================
int getSndStat(int bank)
{
    g_setVolResult = 0;
    g_CurBank = bank;
    if (g_pDirectSound != NULL && bank != 0) {
        ExecAsync((void*)getCurSndStat);
    }
    return g_setVolResult;
}

// ============================================================================
// title_select_sfx (0x0047eb80)
// Sound effect played on title menu confirmation. Empty in original.
// ============================================================================
void title_select_sfx(void)
{
}
