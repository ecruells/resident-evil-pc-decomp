#pragma once
#include "Types.h"
#include "Entities.h"

// ============================================================================
// Bio Card packed layout (0x00be9620 - 0x00BE9A3C, 1052 bytes)
// bio_card.dat is loaded and memcpy'd here by InitializeGame.
// ALL fields MUST be in the correct order, size, and position.
// ============================================================================
#pragma pack(push, 1)
struct BioCardLayout {
    BYTE          prefix[0x200];             // 0x000 - raw bio_card prefix data
    unsigned char stageId;                   // 0x200
    unsigned char roomId;                    // 0x201
    unsigned char roomCameraId;              // 0x202
    unsigned char attractMode_RoomCameraId;  // 0x203
    unsigned char cutId;                     // 0x204
    unsigned char menu_choice_id;            // 0x205
    unsigned char selectedItemId;            // 0x206
    unsigned char totalHeldItems;            // 0x207
    unsigned char specialRoomLightR;         // 0x208 (1 byte, NOT short)
    unsigned char characterModelId;          // 0x209
    unsigned char dat_0x2a;                  // 0x20A
    unsigned char dat_0x2b;                  // 0x20B
    unsigned char dat_0x2c;                  // 0x20C
    unsigned char dat_0x2d;                  // 0x20D
    unsigned char dat_0x2e;                  // 0x20E 
    unsigned char dat_0x20f;                 // 0x20F
    unsigned char dat_0x210;                 // 0x210 (DAT_00be9830, room state byte)
    unsigned char dat_0x211;                 // 0x211 (DAT_00be9831, room state byte)
    unsigned char usedItemId;                // 0x212
    unsigned char dat_0x33;                  // 0x213
    short         fadingState;               // 0x214
    short         specialRoomLightState;     // 0x216
    short         specialRoomLightDelta;     // 0x218
    short         randSeed;                  // 0x21A
    short         countdownTimer;            // 0x21C
    short         playerHealthCopy;          // 0x21E
    short         playerDpadHeld;            // 0x220
    short         playerDpadPressed;         // 0x222
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

    // flags
    unsigned char playerFlags3[32];          // 0x234
    unsigned char locksFlags[8];             // 0x254
    unsigned char roomEventFlags[32];        // 0x25C
    unsigned char roomItemsFlags[32];        // 0x27C
    unsigned char gameFlags_bc[4];           // 0x29C
    unsigned char playerFlags[16];           // 0x2A0
    unsigned char roomFlags[20];             // 0x2B0

    // items slots area
    ItemSlot    itemboxSlots[48];            // 0x2C4
    ItemSlot    itemsSlots[6];               // 0x324
    ItemSlot    rebeccaItemsSlots[6];        // 0x330

    // g_roomBgmState
    unsigned char roomBgmState[224];         // 0x33C
};
static_assert(sizeof(BioCardLayout) == 0x41C, "BioCardLayout size mismatch");
#pragma pack(pop)

// ============================================================================
// Bio Card macro aliases (must match BioCardLayout field order)
// These macros reference fields of the global g_BioCard instance.
// ============================================================================
#define g_BioCardData             ((BYTE*)&g_BioCard)                           // 0x00be9620
#define g_stageId                 (g_BioCard.stageId)                           // BYTE 0x00be9820
#define g_roomId                  (g_BioCard.roomId)                            // BYTE 0x00be9821
#define g_roomCameraId            (g_BioCard.roomCameraId)                      // BYTE 0x00be9822
#define g_AttractMode_RoomCameraId  (g_BioCard.attractMode_RoomCameraId)      // BYTE 0x00be9823
#define g_cutId                   (g_BioCard.cutId)                             // BYTE 0x00be9824
#define g_menu_choice_id          (g_BioCard.menu_choice_id)                  // BYTE 0x00be9825
#define g_selectedItemId          (g_BioCard.selectedItemId)                    // BYTE 0x00be9826
#define g_TotalHeldItems          (g_BioCard.totalHeldItems)                    // BYTE 0x00be9827
#define g_SpecialRoomLightR       (g_BioCard.specialRoomLightR)                 // BYTE 0x00be9828
#define g_CharacterModelId        (g_BioCard.characterModelId)                  // BYTE 0x00be9829
#define DAT_00be982a              (g_BioCard.dat_0x2a)                          // BYTE 0x00be982a
#define DAT_00be982b              (g_BioCard.dat_0x2b)                          // BYTE 0x00be982b
#define DAT_00be982c              (g_BioCard.dat_0x2c)                          // BYTE 0x00be982c
#define DAT_00be982d              (g_BioCard.dat_0x2d)                          // BYTE 0x00be982d
#define DAT_00be982e              (g_BioCard.dat_0x2e)                          // BYTE 0x00be982e
#define DAT_00be982f              (g_BioCard.dat_0x20f)                         // BYTE 0x00be982f
#define DAT_00be9830              (g_BioCard.dat_0x210)                         // BYTE 0x00be9830
#define DAT_00be9831              (g_BioCard.dat_0x211)                         // BYTE 0x00be9831
#define g_usedItemId              (g_BioCard.usedItemId)                        // BYTE 0x00be9832
#define DAT_00be9833              (g_BioCard.dat_0x33)                          // BYTE 0x00be9833
#define g_fading_state            (g_BioCard.fadingState)                       // SHORT 0x00be9834
#define g_SpecialRoomLightState   (g_BioCard.specialRoomLightState)             // SHORT 0x00be9836
#define g_SpecialRoomLightDelta   (g_BioCard.specialRoomLightDelta)             // SHORT 0x00be9838
#define g_RandSeed                (g_BioCard.randSeed)                          // SHORT 0x00be983A
#define g_CountdownTimer          (g_BioCard.countdownTimer)                    // SHORT 0x00be983C
#define g_PlayerHealthCopy        (g_BioCard.playerHealthCopy)                  // SHORT 0x00be983E
#define g_PlayerDpadHeld          (g_BioCard.playerDpadHeld)                    // SHORT 0x00be9840
#define g_PlayerDpadPressed       (g_BioCard.playerDpadPressed)                 // SHORT 0x00be9842
#define g_gameTimerSnapshot_bio   (g_BioCard.gameTimerSnapshot)                 // DWORD 0x00be9844
#define g_SavesCounter            (g_BioCard.savesCounter)                      // BYTE 0x00be9848
#define g_EquippedItemId          (g_BioCard.equippedItemId)                    // BYTE 0x00be9849
#define g_RoomItemBackup          (g_BioCard.roomItemBackup)                    // BYTE 0x00be984A
#define g_SelectedCharactedId     (g_BioCard.selectedCharactedId)               // BYTE 0x00be984B
#define g_PlayerPosXCopy          (g_BioCard.playerPosXCopy)                    // SHORT 0x00be984C
#define g_PlayerPosZCopy          (g_BioCard.playerPosZCopy)                    // SHORT 0x00be984E
#define g_PlayerDirAngleCopy      (g_BioCard.playerDirAngleCopy)                // SHORT 0x00be9850
#define g_PlayerHealthStatusCopy  (g_BioCard.playerHealthStatusCopy)            // BYTE 0x00be9852

#define g_PlayerFlags3            (g_BioCard.playerFlags3)                      // BYTE[32] 0x00be9854
#define g_LocksFlags              (g_BioCard.locksFlags)                        // BYTE[8] 0x00be9874
#define g_RoomEventFlags          (g_BioCard.roomEventFlags)                    // BYTE[32] 0x00be987c
#define g_roomItemsFlags          (g_BioCard.roomItemsFlags)                    // BYTE[32] 0x00be989c
#define g_gameFlags_bc            (g_BioCard.gameFlags_bc)                      // BYTE[4] 0x00be98bc
#define g_PlayerFlags             (g_BioCard.playerFlags)                       // BYTE[16] 0x00be98c0
#define g_RoomFlags               (g_BioCard.roomFlags)                         // BYTE[20] 0x00be98d0

#define g_itemboxSlots            (g_BioCard.itemboxSlots)                      // ItemSlot[48] 0x00be98e4
#define g_ItemsSlots              (g_BioCard.itemsSlots)                        // ItemSlot[6] 0x00be9944
#define g_RebeccaItemSlots        (g_BioCard.rebeccaItemsSlots)                 // ItemSlot[6] 0x00be9950

#define g_roomBgmState            (g_BioCard.roomBgmState)                      // BYTE[224] 0x00be995c


