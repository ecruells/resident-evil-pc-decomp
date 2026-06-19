#pragma once
#include "Types.h"
#include "Entities.h"

// ============================================================================
// Bio Card packed layout (0x00be9620 - 0x00be9A3B, 1052 bytes)
// bio_card.dat is loaded and memcpy'd here by InitializeGame.
// ALL fields MUST be in the correct order, size, and position.
// ============================================================================
#pragma pack(push, 1)
struct BioCardLayout {
    BYTE          prefix[0x200];             // 0x000 - raw bio_card prefix data
    unsigned char stageId;                   // 0x200
    unsigned char roomId;                    // 0x201
    unsigned char roomCameraId;              // 0x202
    unsigned char g_AttractMode_RoomCameraId;// 0x203
    unsigned char cutId;                     // 0x204
    unsigned char messageDisplayActive;      // 0x205
    unsigned char selectedItemId;            // 0x206
    unsigned char totalHeldItems;            // 0x207
    unsigned char specialRoomLightR;         // 0x208 (1 byte, NOT short)
    unsigned char characterModelId;          // 0x209
    unsigned char dat_0x2a;                  // 0x20A
    unsigned char dat_0x2b;                  // 0x20B
    unsigned char dat_0x2c;                  // 0x20C
    unsigned char dat_0x2d;                  // 0x20D
    unsigned char dat_0x2e;                  // 0x20E
    unsigned char pad_0x20f[3];              // 0x20F
    unsigned char usedItemId;                // 0x212
    unsigned char dat_0x33;                  // 0x213
    short         fadingState;               // 0x214
    short         specialRoomLightState;     // 0x216
    short         specialRoomLightDelta;     // 0x218
    short         randSeed;                  // 0x21A
    short         countdownTimer;            // 0x21C
    short         playerHealthCopy;          // 0x21E
    int           playerDpadHeld;            // 0x220
    DWORD         gameTimerSnapshot;         // 0x224
    unsigned char savesCounter;              // 0x228
    unsigned char equippedItemId;            // 0x229
    unsigned char roomItemBackup;            // 0x22A
    unsigned char selectedCharactedId;       // 0x22B
    short         playerPosXCopy;            // 0x22C
    short         playerPosZCopy;            // 0x22E
    short         playerDirAngleCopy;        // 0x230
    unsigned char playerHealthStatusCopy;    // 0x232
    unsigned char pad_0x233;                 // 0x233
    DWORD         playerFlags3[16];          // 0x234 (64 bytes)
    unsigned char pad_0x274[8];              // 0x274
    DWORD         playerFlags2[8];           // 0x27C (32 bytes)
    unsigned char pad_0x29c[4];              // 0x29C
    DWORD         playerFlags[16];           // 0x2A0 (64 bytes)
    unsigned char pad_0x2e0[0x44];           // 0x2E0
    BYTE          itemsArea[24];             // 0x324 (g_ItemsSlots + g_RebeccaItemSlots overlap)
    unsigned char pad_0x33c[0xE0];           // 0x33C
};
static_assert(sizeof(BioCardLayout) == 0x41C, "BioCardLayout size mismatch");
#pragma pack(pop)

// ============================================================================
// Bio Card macro aliases (must match BioCardLayout field order)
// These macros reference fields of the global g_BioCard instance.
// ============================================================================
#define g_BioCardData             ((BYTE*)&g_BioCard)
#define g_stageId                 (g_BioCard.stageId)
#define g_roomId                  (g_BioCard.roomId)
#define g_roomCameraId            (g_BioCard.roomCameraId)
#define g_AttractMode_RoomCameraId  (g_BioCard.g_AttractMode_RoomCameraId)
#define g_cutId                   (g_BioCard.cutId)
#define g_MessageDisplayActive    (g_BioCard.messageDisplayActive)
#define g_selectedItemId          (g_BioCard.selectedItemId)
#define g_TotalHeldItems          (g_BioCard.totalHeldItems)
#define g_SpecialRoomLightR       (g_BioCard.specialRoomLightR)
#define g_CharacterModelId        (g_BioCard.characterModelId)
#define DAT_00be982a              (g_BioCard.dat_0x2a)
#define DAT_00be982b              (g_BioCard.dat_0x2b)
#define DAT_00be982c              (g_BioCard.dat_0x2c)
#define DAT_00be982d              (g_BioCard.dat_0x2d)
#define DAT_00be982e              (g_BioCard.dat_0x2e)
#define g_usedItemId              (g_BioCard.usedItemId)
#define DAT_00be9833              (g_BioCard.dat_0x33)
#define g_fading_state            (g_BioCard.fadingState)
#define g_SpecialRoomLightState   (g_BioCard.specialRoomLightState)
#define g_SpecialRoomLightDelta   (g_BioCard.specialRoomLightDelta)
#define g_RandSeed                (g_BioCard.randSeed)
#define g_CountdownTimer          (g_BioCard.countdownTimer)
#define g_PlayerHealthCopy        (g_BioCard.playerHealthCopy)
#define g_PlayerDpadHeld          (g_BioCard.playerDpadHeld)
#define g_gameTimerSnapshot_bio   (g_BioCard.gameTimerSnapshot)
#define g_SavesCounter            (g_BioCard.savesCounter)
#define g_EquippedItemId          (g_BioCard.equippedItemId)
#define g_RoomItemBackup          (g_BioCard.roomItemBackup)
#define g_SelectedCharactedId     (g_BioCard.selectedCharactedId)
#define g_PlayerPosXCopy          (g_BioCard.playerPosXCopy)
#define g_PlayerPosZCopy          (g_BioCard.playerPosZCopy)
#define g_PlayerDirAngleCopy      (g_BioCard.playerDirAngleCopy)
#define g_PlayerHealthStatusCopy  (g_BioCard.playerHealthStatusCopy)
#define g_PlayerFlags3            (g_BioCard.playerFlags3)
#define g_PlayerFlags2            (g_BioCard.playerFlags2)
#define g_PlayerFlags             (g_BioCard.playerFlags)
#define g_ItemsSlots              ((ItemSlot*)&g_BioCard.itemsArea[0])
#define g_RebeccaItemSlots        ((ItemSlot*)&g_BioCard.itemsArea[12])
