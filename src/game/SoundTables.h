#pragma once

// Voice offset data per stage (uint16 entries, indexed by voice clip ID)
// Bit 15 = extra entry flag (entry consumes 2 uint16s), bits 0-14 = offset index (*16)
extern const unsigned short* g_StageVoiceOffsetTable[8]; // 0x004b3288

// Voice filename data per stage (9-byte records: 7-8 char name + null + pad)
// Each stage's pointer points to an array of 9-byte records indexed by room ID
extern const char* g_StageVoiceNamesTable[8]; // 0x004b2d88

// Per-room enemy sound name table (indexed by stageId * 29 + roomId)
// Each entry points to an array of const char* sound name strings (or NULL)
// Up to 48 entries per room, accessed as soundTable[iVar6 / 4] where iVar6 = 0..96
extern const char** g_RoomSoundNameTable[145]; // 0x004cfae0
