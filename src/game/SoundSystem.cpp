// SoundSystem.cpp - Sound system implementation
// All functions decompiled from Ghidra with original addresses
#include "../Globals.h"
#include "../marni/MarniSound.h"
#include "Entities.h"
#include "../DebugPrint.h"
#include <cmath>
#include <cstdio>
#include "../system/AssetPath.h"

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
    // 48 records, i.e. 96 ints - the array is record-strided, not int-strided
    for (int i = 0; i < 96; i += 2) {
        if (g_emSndBanks[i] != 0) {
            destroySndBank(g_emSndBanks[i]);
            g_emSndBanks[i] = 0;
        }
        g_emSndBanks[i + 1] = 0;
    }
    // Original: stride 8, bound 0x00ac99e8 -> 3 records; clears handle, field_04,
    // slot and (uniquely for this array) paused.
    for (int i = 0; i < 3; i++) {
        if (g_SndBank[i].handle != 0) {
            destroySndBank(g_SndBank[i].handle);
        }
        g_SndBank[i].handle   = 0;
        g_SndBank[i].field_04 = 0;
        g_SndBank[i].slot     = 0;
        g_SndBank[i].paused   = 0;
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
            sprintf(path, GAME_DATA_ROOT "sound\\%s.wav", filename);

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
        if ((g_main_state_flags2 & 0x200000) != 0) {
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
        if (soundId < 48 && g_SndBank[0].handle != 0) {
            SetSndSlot(g_SndBank[0].handle, g_SndBank[0].slot);
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

    for (int i = 0; i < 3; i++) {
        if (g_SndBank[i].handle != 0) {
            if (getSndStat(g_SndBank[i].handle) == 1) {
                setSndStop(g_SndBank[i].handle);
                g_SndBank[i].paused = 1;
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

    for (int i = 0; i < 3; i++) {
        if (g_SndBank[i].handle != 0 && g_SndBank[i].paused == 1) {
            playSnd(g_SndBank[i].handle, (int)g_SndBank[i].slot);
            g_SndBank[i].paused = 0;
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
            for (int i = 0; i < 3; i++) {
                if (g_SndBank[i].handle != 0) {
                    setSndStop(g_SndBank[i].handle);
                    destroySndBank(g_SndBank[i].handle);
                    g_SndBank[i].handle = 0;
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
            for (int i = 0; i < 3; i++) {
                if (g_SndBank[i].handle != 0) {
                    setSndStop(g_SndBank[i].handle);
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
    // Original: MOV EAX,dword ptr [EAX*0x8 + 0xac99d0] - a record index, not *3.
    int bank = g_SndBank[g_SndRampBankIndex].handle;
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
    PauseSounds();
}

// ============================================================================
// ResumeGameSoundsAsync (0x0047...)
// Resumes all game sounds (after FMV).
// ============================================================================
void ResumeGameSoundsAsync(void)
{
    ResumePausedSounds();
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
    if (g_pDirectSound == NULL || bank == 0) return 0;
    return g_pDirectSound->GetStatus(bank);
}

// ============================================================================
// title_select_sfx (0x0047eb80)
// Sound effect played on title menu confirmation. Empty in original.
// ============================================================================
// load_room_sfx (0x0047eba0)
// Loads room-specific sound effects for the current stage.
// Original addresses: 0x0047eba0
// Destroys any previously loaded room SFX banks, then loads up to 2
// room sound effect WAVs from ./usa/sound/<name>.wav into g_RoomSfxBanks.
// ============================================================================

// --- Room sound effects sub-tables (0x004d01d0 area, 2 entries each) ---
// Each sub-table contains up to 2 WAV filenames (without extension).
// Entry layout: [0] = primary sound, [1] = alternate sound (or NULL).

static const char* g_roomSfx_00[2] = { "Dr_wd01",  "Dr_wd02"  };   // 0x004d01d0
static const char* g_roomSfx_01[2] = { "Dr_mtl01", "Dr_mtl02" };   // 0x004d01d8
static const char* g_roomSfx_02[2] = { NULL,       "Dr_brk01" };   // 0x004d01e0
static const char* g_roomSfx_03[2] = { "Dr_reb01", "Dr_reb02" };   // 0x004d01e8
static const char* g_roomSfx_04[2] = { "St_wcp01", NULL       };   // 0x004d0208
static const char* g_roomSfx_05[2] = { "St_wd01",  NULL       };   // 0x004d0210
static const char* g_roomSfx_06[2] = { "Ev_mv",    "Ev_mv"    };   // 0x004d01f0
static const char* g_roomSfx_07[2] = { "Ev_new01", "Ev_new02" };   // 0x004d01f8
static const char* g_roomSfx_08[2] = { "Dr_gat01", "Dr_gat02" };   // 0x004d0220
static const char* g_roomSfx_09[2] = { "St_mtl01", NULL       };   // 0x004d0218
static const char* g_roomSfx_10[2] = { "Ev_old01", "Ev_old02" };   // 0x004d0200
static const char* g_roomSfx_11[2] = { "Ladder01", NULL       };   // 0x004d0228
static const char* g_roomSfx_12[2] = { "Dr_air01", "Dr_air02" };   // 0x004d0230
static const char* g_roomSfx_13[2] = { "Ev_lab01", NULL       };   // 0x004d0238
static const char* g_roomSfx_14[2] = { "Ev_ftn01", "Ev_ftn02" };   // 0x004d0240

// --- room_sound_effects table (0x004d0248) ---
// Indexed by soundTableIndex. Terminated by NULL sentinel.
static const char** g_roomSoundEffectsTable[] = {
    g_roomSfx_00,  // 0x004d0248: index  0 - Dr_wd01/02 (mansion door open/close)
    g_roomSfx_01,  // 0x004d024c: index  1 - Dr_mtl01/02 (metal door open/close)
    g_roomSfx_02,  // 0x004d0250: index  2 - null, Dr_brk01 (trap room falling and closing the door)
    g_roomSfx_03,  // 0x004d0254: index  3 - Dr_reb01/02 (Rebecca saying "Chris!")
    g_roomSfx_04,  // 0x004d0258: index  4 - St_wcp01 (wooden floor footstep 1)
    g_roomSfx_05,  // 0x004d025c: index  5 - St_wd01 (wooden floor footstep 2)
    g_roomSfx_06,  // 0x004d0260: index  6 - Ev_mv (small elevator)
    g_roomSfx_07,  // 0x004d0264: index  7 - Ev_new01/02 (desk open/close?)
    g_roomSfx_08,  // 0x004d0268: index  8 - Dr_gat01/02 (exterior gate open/close)
    g_roomSfx_09,  // 0x004d026c: index  9 - St_mtl01 (metal floor footstep)
    g_roomSfx_10,  // 0x004d0270: index 10 - Ev_old01/02 (elevator doors open/close)
    g_roomSfx_11,  // 0x004d0274: index 11 - Ladder01 (ladder)
    g_roomSfx_12,  // 0x004d0278: index 12 - Dr_air01/02 (Lab gates open/close)
    g_roomSfx_13,  // 0x004d027c: index 13 - Ev_lab01 (Lab elevator door open)
    g_roomSfx_14,  // 0x004d0280: index 14 - Ev_ftn01/02 (Fountain elevetor door open/close)
    NULL,          // 0x004d0284: terminator
};

void load_room_sfx(unsigned char soundTableIndex)
{
    int iVar6 = 0;
    int* piVar7 = g_RoomSfxBanks;

    do {
        // destroy existing bank if loaded
        if (*piVar7 != 0) {
            destroySndBank(*piVar7);
        }
        *piVar7 = 0;
        *((unsigned char*)&piVar7[1]) = 0;

        // get sub-table pointer for this sound table index
        const char** puVar2 = g_roomSoundEffectsTable[soundTableIndex];
        *((unsigned char*)&piVar7[1] + 1) = 0;

        if (puVar2 != NULL) {
            const char* filename = *(const char**)((BYTE*)puVar2 + iVar6);
            if (filename != NULL) {
                char path[260];
                sprintf(path, GAME_DATA_ROOT "sound\\%s.wav", filename);
                findAndOpenFile(path);

                int bank = loadSndBankFromWav(path);
                *piVar7 = bank;
                if (bank != 0) {
                    pan_set(bank, 0);
                    set_volume(bank, g_roomSfxVolume);
                }
            }
        }

        iVar6 += 4;
        piVar7 += 2;
    } while (piVar7 <= (int*)&g_RoomSfxBanks[2]);
}

// ============================================================================
// load_character_sfx (0x0047f070)
// Loads character-specific sound effects (footsteps, voice, etc.).
// Original addresses: 0x0047f070
// Destroys any previously loaded character SFX banks, then loads up to 2
// character sound effect WAVs from ./usa/sound/<name>.wav into g_CharacterSfxBanks.
// ============================================================================

// --- Character sound effects sub-tables (0x004d0288 area, 16 entries each, 0x40 stride) ---
// Shared entries across all characters: [4]=cursor, [5]=cancel, [6]=decide,
// [7]=Chris08, [8]=Chris10, [9]=Chris09, [10]=Mapled, [11-15]=NULL

static const char* g_charSfx_00[16] = {  // 0x004d0288 - Chris base
    "Chris01",  "Chris02",  "Chris03",  "Chris04",
    "cursor",   "cancel",   "decide",   "Chris08",
    "Chris10",  "Chris09",  "Mapled",   NULL,
    NULL,       NULL,       NULL,       NULL,
};
static const char* g_charSfx_01[16] = {  // 0x004d0308 - Jill base
    "Jill01",   "Jill02",   "Jill03",   "Jill04",
    "cursor",   "cancel",   "decide",   "Chris08",
    "Chris10",  "Chris09",  "Mapled",   NULL,
    NULL,       NULL,       NULL,       NULL,
};
static const char* g_charSfx_02[16] = {  // 0x004d0388 - Rebecca base
    "Reb01",    "Reb02",    "Reb03",    "Reb04",
    "cursor",   "cancel",   "decide",   "Chris08",
    "Chris10",  "Chris09",  "Mapled",   NULL,
    NULL,       NULL,       NULL,       NULL,
};
static const char* g_charSfx_03[16] = {  // 0x004d0388 dup - Rebecca base
    "Reb01",    "Reb02",    "Reb03",    "Reb04",
    "cursor",   "cancel",   "decide",   "Chris08",
    "Chris10",  "Chris09",  "Mapled",   NULL,
    NULL,       NULL,       NULL,       NULL,
};
static const char* g_charSfx_04[16] = {  // 0x004d02c8 - Chris (echoed)
    "Ch_ef01",  "Ch_ef02",  "Ch_ef03",  "Ch_ef04",
    "cursor",   "cancel",   "decide",   "Chris08",
    "Chris10",  "Chris09",  "Mapled",   NULL,
    NULL,       NULL,       NULL,       NULL,
};
static const char* g_charSfx_05[16] = {  // 0x004d0348 - Jill (echoed)
    "Jill_ef01","Jill_ef02","Jill_ef03","Jill_ef04",
    "cursor",   "cancel",   "decide",   "Chris08",
    "Chris10",  "Chris09",  "Mapled",   NULL,
    NULL,       NULL,       NULL,       NULL,
};
static const char* g_charSfx_06[16] = {  // 0x004d03c8 - Rebecca (echoed)
    "Reb_ef01", "Reb_ef02", "Reb_ef03", "Reb_ef04",
    "cursor",   "cancel",   "decide",   "Chris08",
    "Chris10",  "Chris09",  "Mapled",   NULL,
    NULL,       NULL,       NULL,       NULL,
};
static const char* g_charSfx_07[16] = {  // 0x004d03c8 dup - Rebecca (echoed)
    "Reb_ef01", "Reb_ef02", "Reb_ef03", "Reb_ef04",
    "cursor",   "cancel",   "decide",   "Chris08",
    "Chris10",  "Chris09",  "Mapled",   NULL,
    NULL,       NULL,       NULL,       NULL,
};

// --- characters_sfx_table (0x004d0408) ---
// Indexed by char_id. 8 entries total.
static const char** g_charactersSfxTable[] = {
    g_charSfx_00,  // 0x004d0408: index 0 - Chris base
    g_charSfx_01,  // 0x004d040c: index 1 - Jill base
    g_charSfx_02,  // 0x004d0410: index 2 - Rebecca base
    g_charSfx_03,  // 0x004d0414: index 3 - Rebecca base (dup)
    g_charSfx_04,  // 0x004d0418: index 4 - Chris alternate
    g_charSfx_05,  // 0x004d041c: index 5 - Jill alternate
    g_charSfx_06,  // 0x004d0420: index 6 - Rebecca alternate
    g_charSfx_07,  // 0x004d0424: index 7 - Rebecca alternate (dup)
};

void load_character_sfx(unsigned char charId)
{
    int iVar6 = 0;
    int* piVar7 = g_CharacterSfxBanks;

    do {
        // destroy existing bank if loaded
        if (*piVar7 != 0) {
            destroySndBank(*piVar7);
        }
        *piVar7 = 0;
        *((unsigned char*)&piVar7[1]) = 0;

        // get sub-table pointer for this character
        const char** puVar2 = g_charactersSfxTable[charId];
        *((unsigned char*)&piVar7[1] + 1) = 0;

        if (puVar2 != NULL) {
            const char* filename = *(const char**)((BYTE*)puVar2 + iVar6);
            if (filename != NULL) {
                char path[260];
                sprintf(path, GAME_DATA_ROOT "sound\\%s.wav", filename);
                findAndOpenFile(path);

                int bank = loadSndBankFromWav(path);
                *piVar7 = bank;
                if (bank != 0) {
                    pan_set(bank, 0);
                    set_volume(bank, g_charSfxVolume);
                }
            }
        }

        iVar6 += 4;
        piVar7 += 2;
        // This bound is the authoritative size of g_CharacterSfxBanks: stride 8,
        // last record at base+120, i.e. 16 records spanning 0x00ac9950-0x00ac99cf
        // (ending exactly where g_SndBank begins). Original: `ptr <= 0xac99cf`.
        // Note sounds_reset / DestroyAllSoundBanks / UpdateSoundFade stop at
        // 0xac9998 in the original and so only cover the first 9 - that is a
        // Capcom bug (records 9-15 leak); see docs/SCD_SCRIPT_SYSTEM.md 6g.
    } while (piVar7 <= (int*)&g_CharacterSfxBanks[30]);
}

// ============================================================================
// bgm_fade_out_all (0x004800e0)
// Fades out and destroys all BGM and secondary sound banks.
// ============================================================================
static void bgm_fade_out_all(void)
{
    // mute all secondary sound banks
    for (int i = 0; i < 3; i++) {
        if (g_SndBank[i].handle != 0) {
            set_volume(g_SndBank[i].handle, 0xffffd8f1);
        }
    }
    Task_sleep(1);

    // stop all secondary sound banks
    for (int i = 0; i < 3; i++) {
        if (g_SndBank[i].handle != 0) {
            setSndStop(g_SndBank[i].handle);
        }
    }

    // wait 4 frames
    for (int i = 0; i < 4; i++) {
        Task_sleep(1);
    }

    // stop and destroy main BGM bank
    if (g_BgmSoundBank != 0) {
        setSndStop(g_BgmSoundBank);
        destroySndBank(g_BgmSoundBank);
        g_BgmSoundBank = 0;
    }

    Task_sleep(1);

    // destroy all secondary banks
    for (int i = 0; i < 3; i++) {
        if (g_SndBank[i].handle != 0) {
            destroySndBank(g_SndBank[i].handle);
            g_SndBank[i].handle = 0;
        }
    }
}

// ============================================================================
// bgm_load_and_start (0x0047f200)
// Loads and starts secondary BGM sound banks based on the target BGM state.
// TODO: Extract data tables from original binary:
//   - BGM name table at 0x004d07b8
//   - BGM pan table at 0x004d0980
//   - BGM data table at 0x004d0c30 (pointed to by g_bgmDataTable)
// ============================================================================
static void bgm_load_and_start(unsigned char bgmState)
{
    int slotCount = 3;

    // special case: stage 2, room 7, Jill with certain flags
    if (g_stageId == 2 && g_roomId == 7) {
        if ((g_playerEntity.id & 3) == 1) {
            if (Flg_ck((int)&g_PlayerFlags, 0x5c) || Flg_ck((int)&g_PlayerFlags, 0x48) || !Flg_ck((int)&g_PlayerFlags, 0x55)) {
                // pass - continue with slotCount=3
            } else {
                slotCount = 2;
                if (g_SndBank[2].handle != 0) {
                    destroySndBank(g_SndBank[2].handle);
                }
                g_SndBank[2].handle = 0;
                g_SndBank[2].slot   = 0;
            }
        }
    }

    unsigned char bgmIndex = bgmState & 7;

    for (int idx = 0; idx < slotCount; idx++) {
        SndBankSlot* ch = &g_SndBank[idx];
        if (ch->handle != 0) {
            destroySndBank(ch->handle);
        }
        ch->handle   = 0;
        ch->field_04 = 0;
        ch->slot     = 0;
        ch->paused   = 0;

        // TODO: Read BGM type byte from g_bgmDataTable[g_stageId][g_roomId] at offset bgmIndex
        // const unsigned char* bgmData = (unsigned char*)g_bgmDataTable + (g_stageId * 0x20 + g_roomId) * 4;
        // unsigned char bgmType = bgmData[bgmIndex];
        // if (bgmType == 0xFF) continue;
        // const char* filename = g_bgmNameTable[bgmType]->entries[idx]; // from 0x004d07b8
        // if (filename == NULL) continue;
        // ... load and start sound bank
    }
}

// ============================================================================
// bgm_start_secondary_slots (0x0047f560)
// Starts playing secondary sound slots if their bits are set in the target BGM state.
// ============================================================================
static void bgm_start_secondary_slots(void)
{
    if (g_targetBgmState == 0xFF) return;

    if (g_targetBgmState & 8) {
        if (g_SndBank[0].handle != 0) {
            SetSndSlot(g_SndBank[0].handle, g_SndBank[0].slot);
        }
        g_BGM_STATE |= 8;
    }
    if (g_targetBgmState & 0x10) {
        if (g_SndBank[1].handle != 0) {
            SetSndSlot(g_SndBank[1].handle, g_SndBank[1].slot);
        }
        g_BGM_STATE |= 0x10;
    }
    if (g_targetBgmState & 0x20) {
        if (g_SndBank[2].handle != 0) {
            SetSndSlot(g_SndBank[2].handle, g_SndBank[2].slot);
        }
        g_BGM_STATE |= 0x20;
    }
}

// ============================================================================
// update_room_bgm (0x0047f620)
// Manages BGM state transitions when entering/loading a room.
// Reads the target BGM state from g_RoomBgmStatePtr[g_roomId], compares with
// the current g_BGM_STATE, and fades/starts BGM tracks as needed.
// ============================================================================
void update_room_bgm(void)
{
    g_targetBgmState = g_RoomBgmStatePtr[g_roomId];
    g_prevBgmState = (unsigned char)g_BGM_STATE;

    if (g_targetBgmState == 0xFF) {
        if ((unsigned char)g_BGM_STATE != 0xFF) {
            bgm_fade_out_all();
            g_BGM_STATE = 0xFF;
        }
        g_main_state_flags2 &= 0xFF7FFFFF;
        return;
    }

    // compare BGM category byte from data table
    // TODO: Extract g_bgmDataTable (points to 0x004d0c30) which maps (stageId, roomId) to BGM category
    // For now, the category check is stubbed - will always trigger BGM reload on room change
    const unsigned char* bgmData = (unsigned char*)g_bgmDataTable;
    if (bgmData != NULL) {
        unsigned int newIdx = (g_targetBgmState & 7) + (g_stageId * 0x20 + g_roomId) * 4;
        unsigned int oldIdx = ((unsigned char)g_BGM_STATE & 7) + (g_stageId * 0x20 + g_AttractMode_RoomCameraId) * 4;

        if ((unsigned char)g_BGM_STATE != 0xFF && bgmData[newIdx] != bgmData[oldIdx] && (g_BGM_STATE & 0x38) != 0) {
            bgm_fade_out_all();
            g_main_state_flags2 &= 0xFF7FFFFF;
            bgm_load_and_start(g_targetBgmState);
        }
    }

    unsigned char bgmType = g_targetBgmState >> 6;
    if (bgmType == 0) {
        if (g_prevBgmState == 0xFF) {
            g_main_state_flags2 &= 0xFF7FFFFF;
            bgm_load_and_start(g_targetBgmState);
        } else {
            // compare category bytes
            const unsigned char* data = (unsigned char*)g_bgmDataTable;
            if (data != NULL) {
                unsigned int newIdx = (g_targetBgmState & 7) + (g_stageId * 0x20 + g_roomId) * 4;
                unsigned int oldIdx = (g_prevBgmState & 7) + (g_stageId * 0x20 + g_AttractMode_RoomCameraId) * 4;
                if (data[newIdx] == data[oldIdx]) {
                    g_main_state_flags2 &= 0xFF7FFFFF;
                    // bit-level sound slot toggling
                    if ((g_targetBgmState ^ g_prevBgmState) & 8) {
                        if (!(g_targetBgmState & 8)) {
                            if (g_SndBank[0].handle != 0) setSndStop(g_SndBank[0].handle);
                        } else {
                            if (g_SndBank[0].handle != 0) SetSndSlot(g_SndBank[0].handle, g_SndBank[0].slot);
                        }
                    }
                    if ((g_targetBgmState ^ g_prevBgmState) & 0x10) {
                        if (!(g_targetBgmState & 0x10)) {
                            if (g_SndBank[1].handle != 0) setSndStop(g_SndBank[1].handle);
                        } else {
                            if (g_SndBank[1].handle != 0) SetSndSlot(g_SndBank[1].handle, g_SndBank[1].slot);
                        }
                    }
                    if ((g_targetBgmState ^ g_prevBgmState) & 0x20) {
                        if (!(g_targetBgmState & 0x20)) {
                            if (g_SndBank[2].handle != 0) setSndStop(g_SndBank[2].handle);
                        } else {
                            if (g_SndBank[2].handle != 0) SetSndSlot(g_SndBank[2].handle, g_SndBank[2].slot);
                        }
                    }
                    goto done;
                }
            }
            bgm_fade_out_all();
            g_main_state_flags2 &= 0xFF7FFFFF;
            bgm_load_and_start(g_targetBgmState);
        }
    } else if (bgmType == 1) {
        bgm_fade_out_all();
        g_main_state_flags2 &= 0xFF7FFFFF;
        bgm_load_and_start(g_targetBgmState);
    } else if (bgmType == 2) {
        g_targetBgmState &= 0x7F;
        bgm_fade_out_all();
        g_main_state_flags2 &= 0xFF7FFFFF;
        bgm_load_and_start(g_targetBgmState);
        if ((g_targetBgmState & 0xC0) == 0) {
            bgm_start_secondary_slots();
        }
        goto done;
    }

    if (bgmType != 2) {
        bgm_start_secondary_slots();
    }

done:
    g_BGM_STATE = g_targetBgmState;
    g_main_state_flags2 &= 0xFF7FFFFF;
}

// ============================================================================
// SquareRoot0 (0x0040a500)
// PS1 GTE square root function. Returns integer square root of the value.
// ============================================================================
int SquareRoot0(int val)
{
    if (val <= 0) return 0;
    return (int)sqrt((double)val);
}

// ============================================================================
// GetAngleQuadrantValue (0x0040a930)
// Maps a fixed-point slope (delta_z * 4096 / delta_x) to an angle value
// in the game's 12-bit angle system (0-4095 = full circle).
// Returns atan(slope / 4096) scaled to game angle units.
// ============================================================================
int GetAngleQuadrantValue(int slope)
{
    if (slope == 0) return 0;
    double radians = atan((double)slope / 4096.0);
    return (int)(radians * (2048.0 / 3.14159265358979323846));
}

// ============================================================================
// CalculateAngleBetweenPointsXZ (0x004603e0)
// Calculates the 2D direction angle from point 1 to point 2 on the XZ plane.
// Returns a 12-bit angle (0-4095) representing a full circle.
// ============================================================================
unsigned short CalculateAngleBetweenPointsXZ(int pos1_x, int pos1_z, int pos2_x, int pos2_z)
{
    short dx = (short)pos2_x - (short)pos1_x;
    if (dx != 0) {
        short dz = (short)pos2_z - (short)pos1_z;
        int slope = ((int)dz << 12) / (int)dx;
        short angle = (short)GetAngleQuadrantValue(slope);
        return (unsigned short)(-((unsigned short)(dx < 0) * 0x800 + angle)) & 0xFFF;
    }
    short dz = (short)pos2_z - (short)pos1_z;
    return (unsigned short)((0 < dz) * 0x800 + 0x400);
}

// ============================================================================
// Calc3DSndPan (0x0047fd20)
// Computes 3D spatial audio panning based on the sound source position
// relative to the current camera. Sets g_snd_pan_right (right) and
// g_snd_pan_left (left) pan values (0x1E-0x7F range).
// Distance attenuates both channels. Angle determines stereo balance.
// ============================================================================
void Calc3DSndPan(VECTOR* pos) // 0x0047fd20
{
    RDT_Camera* cameras = (RDT_Camera*)((char*)g_RdtPointer + 0x94);
    RDT_Camera* cam = &cameras[g_roomCameraId];

    short dx = (short)cam->cam_from_x - (short)pos->x;
    short dz = (short)cam->cam_from_z - (short)pos->z;

    int horizDistSq = (int)dx * (int)dx + (int)dz * (int)dz;
    int dy = *(int*)((char*)&MATRIX_00d22680 + 4) - (int)pos->y;

    int horizDist = SquareRoot0(horizDistSq);
    int distSq3D = horizDist * horizDist + dy * dy;
    int dist3D = SquareRoot0(distSq3D);

    unsigned short angleToSound = CalculateAngleBetweenPointsXZ(
        (short)cam->cam_from_x, (short)cam->cam_from_z,
        (short)pos->x, (short)pos->z);

    unsigned short angleToTarget = CalculateAngleBetweenPointsXZ(
        (short)cam->cam_from_x, (short)cam->cam_from_z,
        cam->cam_to_x, cam->cam_to_z);

    unsigned short angleDiff = (angleToSound - angleToTarget) & 0xFFF;

    if (angleDiff != 0 && angleDiff != 0x1000 && angleDiff != 0x800) {
        bool isRightSide = angleDiff < 0x801;
        unsigned short absAngle = angleDiff;
        if (!isRightSide) {
            absAngle = (~absAngle) & 0x7FF;
        }
        if (absAngle > 0x400) {
            absAngle = ~absAngle;
        }

        if ((absAngle & 0x7FF) > 0x40) {
            short panOffset = (short)((int)(short)(absAngle & 0x7FF) / (dist3D / 2000 + 0x18));
            short rightPan = panOffset + 0x7F;
            short leftPan = 0x7F - panOffset;

            // Clamp right pan to [0x1E, 0x7F]
            if (rightPan > 0x7F) rightPan = 0x7F;
            if (rightPan < 0) rightPan = 0;

            // Clamp left pan to [0x1E, 0x7F]
            if (leftPan < 0x1E) leftPan = 0x1E;

            if (isRightSide) {
                g_snd_pan_right = (unsigned short)rightPan;
                g_snd_pan_left = (unsigned short)leftPan;
            } else {
                g_snd_pan_right = (unsigned short)leftPan;
                g_snd_pan_left = (unsigned short)rightPan;
            }
            goto applyDistance;
        }
    }

    g_snd_pan_right = 0x7F;
    g_snd_pan_left = 0x7F;

applyDistance:
    // Attenuate both channels by distance
    g_snd_pan_left = g_snd_pan_left + (unsigned short)(dist3D / -500);
    g_snd_pan_right = g_snd_pan_right + (unsigned short)(dist3D / -500);

    if (g_snd_pan_left < 0x1E) {
        g_snd_pan_left = 0x1E;
    }
    if (g_snd_pan_right < 0x1E) {
        g_snd_pan_right = 0x1E;
    }

    g_snd_pan_left = g_snd_pan_left & 0x7F;
    g_snd_pan_right = g_snd_pan_right & 0x7F;
}

// ============================================================================
// CalcPanVolume (0x00480610)
// Converts 3D sound pan values to a DirectSound volume (hundredths of dB).
// The average of both pan channels determines the volume level.
// Range: -10000 (silence) to ~0 (full volume).
// ============================================================================
int CalcPanVolume(int panL, int panR) // 0x00480610
{
    int avg = (panL + panR) / 2;
    if (avg < 0x20) {
        return avg * 0x103 - 10000;
    }
    return avg * 0x12 - 0x8E4;
}

// ============================================================================
// Play3DSnd (0x0047f9c0)
// Plays a sound effect with 3D spatial audio positioning.
// Computes stereo panning and volume attenuation based on the sound source's
// position relative to the current camera.
//
// bank: 0=room, 1=sfx, 2=enemy, 3=character, 4=special/BGM
// soundId: index into the selected bank array
// vol: unused (reserved)
// pos: pointer to a VECTOR containing the sound source world position
//      (typically entity.transform.t)
// ============================================================================
void Play3DSnd(int bank, int soundId, int vol, int pos) // 0x0047f9c0
{
    VECTOR* soundPos = (VECTOR*)pos;
    int handle = 0;

    switch (bank) {
    case 0:
        if (soundId > 1) return;
        handle = g_RoomSfxBanks[soundId * 2];
        break;

    case 1:
        if ((g_main_state_flags2 & 0x200000) == 0) {
            if (soundId > 0x2F) return;
        } else {
            if (soundId > 0x0F) return;
        }
        handle = g_SfxBanks[soundId * 2];
        break;

    case 2:
        if (soundId > 0x2F) return;
        handle = g_emSndBanks[soundId * 2];
        break;

    case 3:
        if (soundId > 0x0F) return;
        handle = g_CharacterSfxBanks[soundId * 2];
        break;

    case 4:
        if (soundId < 0x30) {
            Calc3DSndPan(soundPos);
            if (g_SndBank[0].handle != 0) {
                pan_set(g_SndBank[0].handle, (g_snd_pan_right - g_snd_pan_left) * 0x4E);
                SetSndSlot(g_SndBank[0].handle, (int)g_SndBank[0].slot);
            }
        }
        return;

    default:
        return;
    }

    Calc3DSndPan(soundPos);
    if (handle != 0) {
        int dsVol = CalcPanVolume(g_snd_pan_right, g_snd_pan_left);
        set_volume(handle, dsVol);
        SetSndSlot(handle, 0);
    }
}

// ============================================================================
// SCD voice playback (opcode 0x1E, cmd_sfx_set)
//
// Cutscene dialogue is one WAV per line under .\usa\voice\, loaded into
// g_BgmSoundBank. The handshake with the script matters: cmd_sfx_set SETS
// g_main_state_flags bit 17 (0x20000) after asking for a voice, and
// play_sound_and_voice_effect type 2 CLEARS it. Event-VM opcode 0xF7 waits on
// that bit, so the voice is what gates a cutscene line advancing. While
// play_sound_and_voice_effect was an empty stub the bit was only ever cleared as
// a side effect of UpdateMusicWaitState finding no BGM playing - which is why
// scripted lines advanced silently instead of hanging.
//
// Voice state block at 0x00ae9ec8-0x00ae9ee0. None of these existed in the port.
// ============================================================================
static unsigned char g_voiceActive       = 0;  // 0x00ae9ec8
static unsigned char g_voiceExtraCount   = 0;  // 0x00ae9ec9
static int           g_voiceChannelCount = 0;  // 0x00ae9ecc
static int           g_voiceBankBase     = 0;  // 0x00ae9ec4
static int           g_voiceCursor       = 0;  // 0x00ae9ec0
static int           g_voiceBankLimit    = 0;  // 0x00ae9ed8
static int           g_voiceFinished     = 0;  // 0x00ae9ed4
static unsigned char g_voicePanMode      = 0;  // 0x00ae9ee0
static unsigned char g_voicePan[4]       = {0};// 0x00ae9edc-0x00ae9edf
static int           g_voiceMixerReset   = 0;  // 0x008f87ac
static int           g_voiceVolume       = 0;  // 0x00ac9900 - 0 in the image (no attenuation)

// ============================================================================
// voice_set_pan (0x00475640)
// Fills the four per-channel pan bytes. Mode 1 puts the raw value on channels 0
// and 2 with 0 between; any other mode halves it across all four.
// ============================================================================
static void voice_set_pan(unsigned char value)
{
    if (g_voicePanMode == 1) {
        g_voicePan[0] = value;
        g_voicePan[1] = 0;
        g_voicePan[2] = value;
        g_voicePan[3] = 0;
        return;
    }
    g_voicePan[0] = (unsigned char)(value >> 1);
    g_voicePan[1] = g_voicePan[0];
    g_voicePan[2] = g_voicePan[0];
    g_voicePan[3] = g_voicePan[0];
}

// ============================================================================
// voice_mixer_reset (0x004756b0)
// One store. The original is called as FUN_004756b0(9,0,0) but takes no
// parameters - the three pushes are dead, the same pattern as rotate_entity's
// fourth argument and PlayEntitySnd's second.
// ============================================================================
static void voice_mixer_reset(void)
{
    g_voiceMixerReset = 0;
}

// ============================================================================
// voice_load_and_play (0x004753c0)
// Loads the WAV for voice line `id` and starts it.
//
// The long inlined strcpy/strlen chains in the decompilation are the compiler
// expanding three strcats: voice directory, then the 9-byte name from
// g_StageVoiceNamesTable[stageId] + id*9, then ".wav".
//
// g_StageDataPtr is walked as 16-bit entries; an entry with bit 15 set consumes
// an extra word and bumps a counter. That counter and the two bank offsets it
// feeds are stored but nothing on this path reads them back - FUN_004753b0, whose
// return value seeds them, is an empty function in the original. Kept for
// fidelity, marked as inert.
// ============================================================================
static void voice_load_and_play(unsigned int id)
{
    int extra = 0;
    unsigned short* p = (unsigned short*)g_StageDataPtr;

    for (unsigned int i = 0; i < id; i++) {
        if ((*p & 0x8000) != 0) {
            extra++;
            p++;
        }
        p++;
    }

    g_voiceExtraCount = (unsigned char)extra;
    g_voiceActive     = 1;

    // 0x004753b0 is an empty function; these three stay inert.
    g_voiceBankLimit = 0;
    g_voiceBankBase  = g_voiceBankLimit + (*p & 0x7fff) * 0x10 + extra;
    g_voiceCursor    = 0;
    g_voiceBankLimit = g_voiceBankLimit + (p[1] & 0x7fff) * 0x10 + extra;

    // Wait for the previous voice to finish, then release its bank.
    if (g_BgmSoundBank != 0) {
        // PORT-ONLY BOUND: the original spins here with no yield, which is safe
        // in its cooperative scheduler only because getSndStat eventually stops
        // returning 1. Bounding it turns a whole-game hang into a diagnostic.
        int spin = 0;
        while (getSndStat(g_BgmSoundBank) == 1) {
            if (++spin > 100000) {
                dbg_printf("[voice] bank %d never reported finished - abandoning wait\n",
                           g_BgmSoundBank);
                break;
            }
        }
        destroySndBank(g_BgmSoundBank);
        g_BgmSoundBank = 0;
    }

    if (g_stageId >= 8 || g_StageVoiceNamesTable[g_stageId] == NULL) {
        return;
    }

    const char* name = g_StageVoiceNamesTable[g_stageId] + id * 9;
    if (name[0] == '\0') {
        return;
    }

    char path[260];
    sprintf(path, "%s%s%s", GAME_DATA_ROOT "voice\\", name, ".wav");

    if (findAndOpenFile(path) == 0) {
        dbg_printf("[voice] could not open file: %s\n", path);
        return;
    }

    g_BgmSoundBank = loadSndBankFromWav(path);
    if (g_BgmSoundBank != 0) {
        pan_set(g_BgmSoundBank, 0);
        int volume = g_voiceVolume;
        // 0x0047558?: one line in stage 0 is mixed well down
        if ((id == 0x33) && (g_stageId == 0)) {
            volume = -300;
        }
        set_volume(g_BgmSoundBank, volume);
        SetSndSlot(g_BgmSoundBank, 0);
    }
}

// ============================================================================
// play_sound_and_voice_effect (0x00475340)
// SCD opcode 0x1E dispatches here. Type 1 starts a voice line; type 2 ends the
// current one, resets the mixer and clears the 0x20000 wait flag the script is
// blocked on. Any other type is ignored.
// ============================================================================
void play_sound_and_voice_effect(int type, int id)
{
    if (type == 1) {
        voice_load_and_play((unsigned int)id);
        return;
    }
    if (type != 2) {
        return;
    }

    for (int i = g_voiceChannelCount; i != 0; i--) {
        voice_set_pan((unsigned char)i);
    }
    voice_set_pan(0);
    voice_mixer_reset();
    g_voiceFinished = 0;
    g_main_state_flags &= 0xFFFDFFFF;
}

// ============================================================================
// LookupFootstepZone (0x00460480)
// Scans RDT footstep_sound_zones table for the zone containing (posX, posZ).
// Returns packed byte: high = zone type, low = sound offset for that zone.
// ============================================================================
unsigned short LookupFootstepZone(short posX, short posZ) // 0x00460480
{
    // footstep_sound_zones starts with a 2-byte header, then 5-usht entries:
    //   [0]=baseX [1]=baseZ [2]=width [3]=height [4]=soundData
    unsigned short* entry = (unsigned short*)(g_RdtPointer->footstep_sound_zones + 2);

    // Scan until position falls within zone bounds.
    //
    // The original has no terminator check either - it relies on the last zone in
    // every room's table being a catch-all that always matches, so the loop is
    // guaranteed to stop. That holds only while the entity is somewhere inside the
    // room. An entity that walks out of bounds makes this scan run off the end of
    // the RDT and fault (seen crashing at 0x0140F000 with the player overshooting
    // his scripted run target).
    //
    // PORT-ONLY GUARD, no behavioural change: in correct operation the catch-all
    // matches long before the cap, so this never fires. It exists so an
    // out-of-bounds entity produces a diagnostic instead of an access violation,
    // which would otherwise mask whatever actually moved the entity out of the
    // room. Remove it only once out-of-bounds movement is impossible.
    int guard = 0;
    while ((unsigned int)entry[2] <= (int)posX - (unsigned int)entry[0] ||
           (unsigned int)entry[3] <= (int)posZ - (unsigned int)entry[1]) {
        entry += 5;
        if (++guard > 256) {
            dbg_printf("[footstep] no zone contains (%d,%d) after %d entries - "
                       "entity is outside the room\n", (int)posX, (int)posZ, guard);
            return 0;
        }
    }

    // Pack high byte of height field and low byte of sound data
    return (unsigned short)(((entry[3] >> 8) & 0xFF) << 8 | (entry[4] & 0xFF));
}

// ============================================================================
// PlayEntitySnd (0x0047fbf0)
// Plays a 3D positioned entity sound effect. The base sound type (0-2) is
// offset by the current room's footstep zone data. If a special state flag
// is set, uses a fixed offset instead. Sound is spatially positioned using
// Calc3DSndPan.
// ============================================================================
void PlayEntitySnd(unsigned char soundType) // 0x0047fbf0
{
    if (soundType >= 3) return;

    if ((((unsigned char)g_main_state_flags & 0x80) == 0) ||
        (g_playerEntity.unk_8e != (unsigned short)0xF8F8)) {
        // Normal path: look up zone-based sound offset
        unsigned short zoneData = LookupFootstepZone(
            (short)ENTITY->scaMatrixData.localMatrix.t[0], (short)ENTITY->scaMatrixData.localMatrix.t[2]);
        char zoneOffset = (char)(zoneData & 0xFF);

        // Add input modifier: if g_main_state_flags2 bit 0 is set, add 0xFD (suppresses/wraps sound)
        unsigned int inputMod = (((g_main_state_flags2 & 1) == 0) - 1U) & 0xFD;
        soundType = (unsigned char)((int)soundType + (int)zoneOffset + (int)inputMod);
    } else {
        // Special state: fixed offset 0x23
        soundType = soundType + 0x23;
    }

    if (soundType >= 0x30) {
        dbg_printf("[entsnd] type=%u out of range (>=0x30) - no sound\n",
                   (unsigned int)soundType);
        return;
    }

    Calc3DSndPan((VECTOR*)ENTITY->scaMatrixData.localMatrix.t);

    int handle = g_emSndBanks[(unsigned int)soundType * 2];

    if (handle != 0) {
        int volume = CalcPanVolume(g_snd_pan_right, g_snd_pan_left);
        set_volume(handle, volume);
        SetSndSlot(handle, 0);
    }
}

// ============================================================================
// Snd_em (0x0047fca0)
// Plays a 3D positioned enemy sound effect using the current entity's
// translation vector. Enemy sound IDs are offset by the entity's
// sound group (bits 4-6 of field_0x161) * 10.
// ============================================================================
void Snd_em(unsigned char em_snd_id) // 0x0047fca0
{
    if (em_snd_id >= 10) return;

    em_snd_id = em_snd_id + ((ENTITY->unk_10 & 0x70) >> 4) * 10;
    if (em_snd_id >= 48) return;

    Calc3DSndPan((VECTOR*)ENTITY->scaMatrixData.localMatrix.t);

    int handle = g_emSndBanks[em_snd_id * 2];
    if (handle != 0) {
        pan_set(handle, (g_snd_pan_right - g_snd_pan_left) * 0x4E);
        SetSndSlot(g_emSndBanks[em_snd_id * 2], 0);
    }
}

// collision_flag_set / collision_push_rect / collision_push_circle and
// Room_SetupCollisionCallbacks now live in RoomCollision.cpp, with the rest of
// the room boundary system. They were only ever here because the original's
// callback table sits next to the sound bank globals.

// ============================================================================
// Room_LoadEnemySoundBanks (0x0047eed0)
// Loads per-room enemy sound banks. Iterates through g_emSndBanks, destroys
// existing banks, and loads WAV files from ./usa/sound/<name>.wav using the
// g_RoomSoundNameTable lookup table (indexed by stageId * 29 + roomId).
// ============================================================================
void Room_LoadEnemySoundBanks(void) {
    int iVar6 = 0;
    int* piVar7 = g_emSndBanks;

    do {
        // Destroy existing bank if loaded
        if (*piVar7 != 0) {
            destroySndBank(*piVar7);
        }
        *piVar7 = 0;
        *((unsigned char*)(piVar7 + 1)) = 0;
        *((unsigned char*)(piVar7 + 1) + 1) = 0;

        // Look up per-room sound name table
        const char** soundTable = g_RoomSoundNameTable[g_stageId * 29 + g_roomId];

        if (soundTable != NULL) {
            const char* filename = soundTable[iVar6 / 4];
            if (filename != NULL) {
                char path[260];
                sprintf(path, GAME_DATA_ROOT "sound\\%s.wav", filename);
                findAndOpenFile(path);

                int bank = loadSndBankFromWav(path);
                *piVar7 = bank;
                if (bank != 0) {
                    pan_set(bank, 0);
                    set_volume(bank, g_EnemySndVolume);
                }
            }
        }

        iVar6 += 4;
        piVar7 += 2;
        // The original bounds this with an absolute address:
        //   piVar7 walks from g_emSndBanks (0x00ac99f0) in 8-byte steps and the
        //   loop ends once piVar7 > 0xac9b6f, which is records 0 through 47.
        // &g_emSndBanks[48] is only base+192 bytes - element 48 of an int array,
        // not record 48 - so this stopped after 25 records and left every enemy
        // and footstep sound from id 25 up with a null bank. That is why no SFX
        // played: PlayEntitySnd looks up g_emSndBanks[soundType * 2], finds 0 and
        // silently returns.
    } while (piVar7 < (int*)&g_emSndBanks[96]);
}

// (0x0047f870) - 3-param play_sfx overload (mode parameter)
void play_sfx(int bank, int soundId, int mode) { play_sfx(bank, soundId); }

