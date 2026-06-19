// RoomStubs.cpp - Room initialization functions (decompiled from Ghidra)
#include "../Globals.h"
#include "FileLoader.h"
#include "SpriteRenderer.h"
#include <cstdio>
#include <cstring>

// Forward declarations for functions defined in other files
extern void SetAnimSlot(AnimSlot* slots, int slotPtr, int index);
extern unsigned int* CreateAnimObject(int slotPtr, unsigned int* param2);
extern void SetSpriteBufferFlag(void);

// TMD texture header struct (output of ParseTmdTextureHeader)
// Packed struct matching the original byte layout (28 bytes = 0x1C)
#pragma pack(push, 1)
struct TmdTextureHeader {
    int   count;       // offset 0x00: (*data & 0xF) sign-extended
    short field_04;    // offset 0x04: first short from aligned sub-section
    short field_06;    // offset 0x06: second short
    short field_08;    // offset 0x08: third short (used for page count calc)
    short field_0A;    // offset 0x0A: fourth short
    int   dataPtr;     // offset 0x0C: pointer into sub-section data
    short field_10;    // offset 0x10: first short from data+8 (CLUT descriptor low)
    short field_12;    // offset 0x12: second short from data+8
    short field_14;    // offset 0x14: third short from data+8
    short field_16;    // offset 0x16: fourth short from data+8 (depth increment)
    int   ptr_10;      // offset 0x18: data + 0x10 pointer
};
#pragma pack(pop)

// Forward declaration for ParseTmdTextureHeader (defined in EntityModelLoader.cpp)
extern void ParseTmdTextureHeader(void* data, TmdTextureHeader* header);

// ============================================================================
// Room effect sprite relation table (0x004c48b8)
// Indexed by (stageId * 32 + roomId) * 4 + slotIndex.
// Each group of 4 bytes: effect sprite indices for texture pages 0-3.
// 0xFF = no effect sprite for that slot.
// ============================================================================
static const unsigned char g_RoomEffectSpriteTable[5 * 32 * 4] = {
    // Stage 0 (32 rooms x 4 bytes)
    0x00,0x02,0xFF,0xFF, 0x00,0x03,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x04,0xFF,0xFF,
    0x00,0x04,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x04,0xFF,0xFF,
    0x00,0x05,0x06,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x03,0xFF,0xFF,
    0x00,0x07,0x08,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x04,0xFF,0xFF,
    0x00,0x09,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0x0A,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x09,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF,
    // Stage 1 (32 rooms x 4 bytes)
    0x00,0x01,0xFF,0xFF, 0x00,0x03,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x04,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x02,0xFF,0xFF, 0x00,0x03,0xFF,0xFF,
    0x00,0x04,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x0B,0xFF,0xFF, 0x00,0x04,0x0C,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x03,0xFF,0xFF, 0x00,0x0D,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0x0A,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x09,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    // Stage 2 (32 rooms x 4 bytes)
    0x00,0x0E,0x0F,0x10, 0x00,0x04,0x11,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x04,0xFF,0xFF,
    0x00,0x04,0xFF,0xFF, 0x00,0x04,0x12,0xFF, 0x00,0x13,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x14,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x13,0xFF,0xFF,
    // Stage 3 (32 rooms x 4 bytes)
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x15,0x16,0xFF, 0x00,0x18,0xFF,0xFF,
    0x00,0x18,0xFF,0xFF, 0x00,0x18,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x19,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x19,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    // Stage 4 (32 rooms x 4 bytes)
    0x00,0x03,0x1A,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x03,0x1A,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x19,0xFF,0xFF, 0x00,0x04,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x19,0xFF,0xFF, 0x00,0x1B,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x04,0x1C,0xFF, 0x00,0x04,0x1C,0xFF, 0x00,0x04,0x1C,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0x1D,0x1E, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
    0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF, 0x00,0x01,0xFF,0xFF,
};

// ============================================================================
// Effect sprite name table (0x004c4c38)
// 8 bytes per entry (padded name). Index from g_RoomEffectSpriteTable values.
// ============================================================================
static const char g_EffectSpriteNames[][8] = {
    "esp000", "esp001", "esp200", "esp201", "esp202", "esp203",
    "esp204", "esp205", "esp206", "esp207", "esp208", "esp209",
    "esp210", "esp211", "esp212", "esp213", "esp214", "esp215",
    "esp216", "esp217", "esp218", "esp219", "esp220", "esp221",
    "esp222", "esp223", "esp225", "esp226", "esp227", "esp228",
    "esp229", "esp230",
};

// ============================================================================
// Effect sprite texture config table (0x004c4f50)
// 8 bytes per entry: [mode (4 bytes)] [data pointer (4 bytes)]
// ============================================================================
struct EffSpriteTexConfig {
    int   mode;
    void* dataPtr;
};

static const EffSpriteTexConfig g_EffectSpriteTexConfig[] = {
    { 4, (void*)0x004C4D38 }, { 4, (void*)0x004C4D48 }, { 5, (void*)0x004C4D58 },
    { 7, (void*)0x004C4D70 }, { 6, (void*)0x004C4D90 }, { 7, (void*)0x004C4DA8 },
    { 2, (void*)0x004C4DC8 }, { 5, (void*)0x004C4DD0 }, { 1, (void*)0x004C4DE4 },
    { 5, (void*)0x004C4DE8 }, { 1, (void*)0x004C4DFC }, { 5, (void*)0x004C4E00 },
    { 1, (void*)0x004C4E14 }, { 7, (void*)0x004C4E18 }, { 6, (void*)0x004C4E38 },
    { 1, (void*)0x004C4E50 }, { 1, (void*)0x004C4E54 }, { 3, (void*)0x004C4E58 },
    { 1, (void*)0x004C4E64 }, { 5, (void*)0x004C4E68 }, { 5, (void*)0x004C4E80 },
    { 5, (void*)0x004C4E98 }, { 1, (void*)0x004C4EAC }, { 5, (void*)0x004C4EB0 },
    { 6, (void*)0x004C4EC8 }, { 6, (void*)0x004C4EE0 }, { 2, (void*)0x004C4EF8 },
    { 7, (void*)0x004C4F00 }, { 2, (void*)0x004C4F20 }, { 1, (void*)0x004C4F28 },
    { 6, (void*)0x004C4F30 }, { 1, (void*)0x004C4F48 },
};

// ============================================================================
// FUN_0047bbe0 (0x0047bbe0) - Load effect sprite data from RDT
// Iterates through effect animation index table, resolves sprite info and
// animation data pointers relative to the RDT base.
// Returns the index of the last valid entry (or 8 if all valid).
// ============================================================================
static unsigned char load_effect_sprite_data(unsigned char* effectAnimIndex, unsigned char* effectAnimData, void* rdtBase, unsigned char startSlot)
{
    unsigned char lastValid = 8;
    unsigned char i = 0;
    do {
        unsigned int idx = (unsigned int)i;
        unsigned char spriteIdx = effectAnimIndex[idx];
        g_abEffSpriteIndexTable[startSlot + idx] = spriteIdx;
        if (spriteIdx == 0xFF) {
            lastValid = i;
            i = 8;
        } else {
            i = i + 1;
            unsigned int si = (unsigned int)spriteIdx;
            int dataOffset = *(int*)(effectAnimData - idx * 4);
            g_effectSpriteInfo[si] = (DWORD)rdtBase + dataOffset;
            g_effectAnimData[si] = (DWORD)rdtBase + dataOffset;
            unsigned char* spriteInfo = (unsigned char*)g_effectSpriteInfo[si];
            g_effectAnimData[si] = g_effectAnimData[si] +
                ((unsigned int)spriteInfo[2] + (unsigned int)spriteInfo[0]) * 4 + 8;
        }
    } while (i < 8);
    return lastValid;
}

// ============================================================================
// FUN_0047bc80 (0x0047bc80) - Set up effect sprite texture pages
// Assigns VRAM positions for each effect sprite's texture data.
// ============================================================================
static void setup_effect_sprite_textures(unsigned char startSlot)
{
    unsigned char texY;
    short texX;
    short pageRow;
    unsigned short curU;
    unsigned short curV;

    if (startSlot == 8) {
        texY = DAT_00bf0a38;
        texX = DAT_00bf0a3c;
        pageRow = DAT_00bf0a40;
    } else {
        texY = 0x18;
        texX = 0;
        DAT_00bf0a38 = 0x18;
        DAT_00bf0a3e = 0;
        pageRow = 0;
        DAT_00bf0a42 = 4;
    }

    unsigned char slot = 0;
    curV = DAT_00bf0a42;
    curU = DAT_00bf0a3e;
    DAT_00bf0a3c = texX;
    DAT_00bf0a40 = pageRow;

    do {
        unsigned char spriteIdx = g_abEffSpriteIndexTable[slot + startSlot];
        if (spriteIdx == 0xFF) break;

        TmdTextureHeader header;
        ParseTmdTextureHeader((void*)(DAT_00ac9cd0[slot] + 4), &header);

        unsigned short texW = header.field_0A;
        unsigned short texH = header.field_16;

        if ((unsigned int)texW + (unsigned int)curU > 0x100) {
            curU = 3;
            texY = texY + 1;
            texX = texX + 0x40;
        }
        if ((unsigned int)texH + (unsigned int)curV > 0x1F) {
            curV = 4;
            pageRow = pageRow + 1;
        }

        empty_0047b950(0);

        unsigned short* spriteInfo = (unsigned short*)g_effectSpriteInfo[spriteIdx];
        spriteInfo[2] = curV * 0x40 + pageRow + 0x7810;
        *((unsigned char*)(spriteInfo + 3)) = texY;

        unsigned short* uvPtr = spriteInfo + spriteInfo[1] * 2 + 4;
        unsigned char count = 0;
        do {
            count = count + 1;
            *((char*)uvPtr + 1) = *((char*)uvPtr + 1) + (char)curU;
            uvPtr = uvPtr + 2;
        } while ((unsigned short)count < spriteInfo[0]);

        curU = curU + texW;
        slot = slot + 1;
        curV = curV + (texH & 0xFF);
    } while (slot < 8);

    if (startSlot == 0) {
        DAT_00bf0a38 = texY;
        DAT_00bf0a3c = texX;
        DAT_00bf0a40 = pageRow;
        DAT_00bf0a3e = curU;
        DAT_00bf0a42 = curV;
    }
}

// ============================================================================
// load_effect_sprites (0x0047d020) - Load room effect sprite TIM files
// Looks up effect sprites for the current room and loads their TIM textures.
// ============================================================================
static void load_effect_sprites(void)
{
    char pathBuf[256];

    for (int i = 0; i < 4; i++) {
        TexturePage_DeleteSet(i);
    }

    for (int i = 0; i < 4; i++) {
        unsigned int relIdx = (unsigned int)g_RoomEffectSpriteTable[
            ((unsigned int)g_stageId * 32 + (unsigned int)g_roomId) * 4 + i];
        if (relIdx != 0xFF) {
            sprintf(pathBuf, ".\\usa\\effspr\\%s.tim", g_EffectSpriteNames[relIdx]);
            LoadFile(pathBuf, g_displayImageBuffer, 0x20);
            TexturePage_Load(i, g_displayImageBuffer);
        }
    }

    STAGE_ID_00ac9cf0 = (unsigned int)g_stageId;
    ROOM_ID_00ac9cf4 = (unsigned int)g_roomId;
}

// ============================================================================
// InitRoomEffSprite (0x0047b9b0)
// Initializes room effect sprites: clears the effect pool, loads effect
// animation data from the RDT, sets up texture pages, and loads sprite TIMs.
// ============================================================================
void InitRoomEffSprite(void)
{
    // 0x0047b9b0: Reset effect pool
    g_freeEffectSlots = 64;
    memset(g_effectPool, 0, sizeof(g_effectPool));

    // 0x0047b9c3: Clear shoot direction effect sprite entries (indices 0-7)
    unsigned char i = 0;
    do {
        unsigned int idx = (unsigned int)i;
        unsigned char spriteIdx = g_abEffSpriteIndexTable[idx];
        if (spriteIdx == 0xFF) {
            i = 8;
        } else {
            i = i + 1;
            g_effectSpriteInfo[spriteIdx] = 0xFFFFFFFF;
            g_effectAnimData[spriteIdx] = 0xFFFFFFFF;
            g_abEffSpriteIndexTable[idx] = 0xFF;
        }
    } while (i < 8);

    // 0x0047ba1d: Load room effect animation data from RDT
    load_effect_sprite_data(g_RdtPointer->effect_anim_index, g_RdtPointer->effect_anim_data, g_RdtPointer, 8);

    // 0x0047ba41: Compute effect sprite image data pointers
    RDT* pRdt = g_RdtPointer;
    i = 0;
    unsigned char* spriteImBase = pRdt->effect_anim_sprite;
    do {
        unsigned int idx = (unsigned int)i;
        i = i + 1;
        DAT_00ac9cd0[idx] = (int)pRdt->unknown_03 + *(int*)(spriteImBase - idx * 4) - 3;
    } while (i < 8);

    // 0x0047ba5c: Set up effect sprite texture positions
    setup_effect_sprite_textures(8);

    // 0x0047ba64: Load room effect sprite TIM files
    load_effect_sprites();
}

// ============================================================================
// FUN_0048bea0 (0x0048bea0) - Reverse animation frame data order
// Swaps animation entries to reverse the playback order.
// param_1: pointer to joint anim_field (offset 0x0C within JointStruct)
// ============================================================================
static void FUN_0048bea0(int param_1)
{
    AnimSlot* slot = *(AnimSlot**)(param_1 + 8);
    unsigned short count = slot->entryCount;
    int baseAddr = count * 0x1c + (int)slot->data2;

    short* pRot = (short*)(baseAddr - 0x14);
    int* pTiming = (int*)(baseAddr - 8);

    do {
        short tmpRot = pRot[0];
        pRot[0] = pRot[2];
        pRot[2] = tmpRot;

        int tmpTiming = pTiming[0];
        pTiming[0] = pTiming[1];
        pTiming[1] = tmpTiming;

        count = count - 1;
        pRot = (short*)((int)pRot - 0x1c);
        pTiming = (int*)((int)pTiming - 0x1c);
    } while (count != 0);
}

// ============================================================================
// SetupEntityJointAnimation (0x0048bef0) - Entity joint animation copy and setup
// Copies entity joint data to the load buffer, resolves animation pointers,
// and creates animation objects for each joint.
// ============================================================================
void SetupEntityJointAnimation(void)
{
    // 0x0048bef0: Save load data pointer to entity weapon joints ptr
    ENTITY->weaponJointsPtr = (unsigned int)g_loadDataDestPointer;
    int jointBase = (int)g_loadDataDestPointer;

    // 0x0048bf05: Advance load pointer past joint data
    unsigned char jointCount = ENTITY->jointCount;
    g_loadDataDestPointer = (char*)g_loadDataDestPointer + (unsigned int)jointCount * 0x7c;

    // 0x0048bf1e: Copy animation slot data
    JointStruct* joints = ENTITY->jointsStructs;
    int* animSlotSrc = (int*)joints->anim_slot_ptr;
    int animEnd = *animSlotSrc;
    memcpy(g_loadDataDestPointer, animSlotSrc, animEnd - (int)animSlotSrc);

    // 0x0048bf37: Copy joint structs
    memcpy((void*)jointBase, joints, (unsigned int)jointCount * 0x7c);

    // 0x0048bf4d: Set up new animation slot base
    DAT_00be0e00 = (int)g_loadDataDestPointer;
    *(int*)(jointBase + 0x14) = (int)g_loadDataDestPointer;
    g_loadDataDestPointer = (char*)g_loadDataDestPointer + (animEnd - (int)animSlotSrc & 0xFFFFFFFCU);

    // 0x0048bf6c: Save new and original anim slot pointers for delta fixup
    int newAnimSlotPtr = *(int*)(jointBase + 0x14);
    unsigned int origAnimSlotPtr = (unsigned int)joints->anim_slot_ptr;

    // 0x0048bf7e: Process each joint
    unsigned char j = 0;
    if (jointCount != 0) {
        unsigned char nextJ;
        do {
            int animFieldAddr = jointBase + 0x0c;
            nextJ = j + 1;

            SetAnimSlot((AnimSlot*)DAT_00be0e00, animFieldAddr, j);

            // Point data_ptr to &scale_flag
            *(int*)(jointBase + 0x10) = jointBase + 0x20;

            // Fix up animation data pointer with relocation delta
            int* fixupPtr = (int*)(*(int*)(jointBase + 0x14) + 0x10);
            *fixupPtr = *fixupPtr + (newAnimSlotPtr - (int)origAnimSlotPtr);

            FUN_0048bea0(animFieldAddr);

            g_loadDataDestPointer = CreateAnimObject(animFieldAddr, (unsigned int*)g_loadDataDestPointer);

            jointBase = jointBase + 0x7c;
            j = nextJ;
        } while (nextJ < jointCount);
    }
}

// ============================================================================
// SetupTextureBankData (0x00473a30) - Process texture queue bank data
// Sets up texture bank pointers and copies initial texture state when the
// texture queue has entries. Called during room initialization.
// param_1: texture bank ID (short, typically _g_TextureBankID >> 8)
// ============================================================================
void SetupTextureBankData(short param_1)
{
    // 0x00473a30: Skip if no texture queue entries
    if (DAT_00ae9f04 == 0) return;

    // 0x00473a3e: Calculate bank count and pointers
    DAT_00ae9f06 = (DWORD)(param_1 - 10);
    DAT_00ae9f00 = (DWORD)g_loadDataDestPointer;
    DAT_00ae9efc = (DWORD)DAT_00ae9f06 * 0x200 + (DWORD)g_loadDataDestPointer;

    // 0x00473a6d: Advance load pointer
    g_loadDataDestPointer = (char*)g_loadDataDestPointer + (DWORD)DAT_00ae9f06 * 0x400;

    // 0x00473a7e: Process pending texture operations
    empty_0047b950(0);

    // 0x00473a86: Copy texture data to secondary buffer
    unsigned short idx = 0;
    if (DAT_00ae9f06 != 0) {
        do {
            unsigned int i = (unsigned int)idx;
            idx = idx + 1;
            *(DWORD*)(DAT_00ae9efc + i * 4) = *(DWORD*)(DAT_00ae9f00 + i * 4);
        } while ((unsigned int)idx < (DWORD)DAT_00ae9f06 * 0x80);
    }
}

// ============================================================================
// load_slides_images (0x00478110)
// Loads the projector slide TIM image and creates a texture page from it.
// Called during room_set for stage 4, room 4 (the lab projector room).
// ============================================================================
void load_slides_images(void)
{
    // 0x00478110: Load slide TIM file into display image buffer
    LoadFile("./usa/data/slide.tim", g_displayImageBuffer, 0x20);
    // 0x00478124: Create texture page from loaded TIM data
    TexturePage_LoadImage(g_displayImageBuffer, 9, 0xd);
}

// ============================================================================
// LZW Decompression (unpack_pakfile_ at 0x00425ab0)
// Helper functions and main decompression routine for PAK files.
// ============================================================================

// FUN_00425a70 - Reset LZW decompression dictionary
static void pak_decomp_reset(void)
{
    // 0x00425a70: Clear all dictionary entries (set prefix to -1)
    for (int i = 0; i < 8192; i++) {
        g_pakDictPrefix[i] = -1;
    }
    g_pakDecompNextCode = 0x103;
    g_pakDecompCodeSize = 9;
    g_pakDecompMaxCode = 0x1ff;
}

// FUN_00425a00 - Read a code of 'codeSize' bits from the input bitstream
static unsigned int pak_decomp_read_code(void* src, unsigned int codeSize)
{
    unsigned int result = 0;
    unsigned int bit = 1 << (codeSize - 1);

    while (bit != 0) {
        // 0x00425a10: Refill bit buffer when empty
        if (g_pakDecompBitMask == 0x80) {
            g_pakDecompCurByte = ((unsigned char*)src)[g_pakDecompInputPos];
            g_pakDecompInputPos++;
        }
        // 0x00425a30: Test current bit
        if ((g_pakDecompCurByte & g_pakDecompBitMask) != 0) {
            result |= bit;
        }
        // 0x00425a48: Advance to next bit
        g_pakDecompBitMask >>= 1;
        bit >>= 1;
        if (g_pakDecompBitMask == 0) {
            g_pakDecompBitMask = 0x80;
        }
    }
    return result;
}

// FUN_00425bc0 - Decode a string from the LZW dictionary into g_pakStringBuf
// Returns the count of characters written (starting from param_1)
static int pak_decomp_decode_string(int startPos, unsigned int code)
{
    if (code > 0xFF) {
        // Multi-character: walk the chain
        int pos = startPos;
        do {
            int idx = code;
            int dictOfs = code * 12;
            code = *(int*)((char*)g_pakDictPrefix + dictOfs - 4); // prefix at entry+4
            g_pakStringBuf[pos] = g_pakDictChar[idx];
            pos++;
        } while (code > 0xFF);
        g_pakStringBuf[pos] = (char)code;
        return pos + 1;
    }
    // Single character
    g_pakStringBuf[startPos] = (char)code;
    return startPos + 1;
}

// unpack_pakfile_ (0x00425ab0) - LZW decompression of a PAK file
// src: pointer to compressed PAK data
// dst: pointer to output buffer for decompressed data
// Returns: number of bytes written to dst
int unpack_pakfile_(void* src, void* dst)
{
    int outPos = 0;
    g_pakDecompInputPos = 0;
    g_pakDecompBitMask = 0x80;
    g_pakDecompCurByte = 0;

    do {
        // 0x00425abf: Reset dictionary
        pak_decomp_reset();

        // 0x00425ac4: Read first code
        unsigned int curCode = pak_decomp_read_code(src, g_pakDecompCodeSize);
        if (curCode == 0x100) {
            return outPos;
        }

        // 0x00425ade: Output first character
        ((unsigned char*)dst)[outPos] = (unsigned char)curCode;
        outPos++;
        unsigned int prevCode = curCode;

        // 0x00425aee: Main decompression loop
        while (true) {
            curCode = pak_decomp_read_code(src, g_pakDecompCodeSize);

            // 0x100 = end of data
            if (curCode == 0x100) {
                return outPos;
            }
            // 0x102 = reset dictionary (restart outer loop)
            if (curCode == 0x102) {
                break;
            }
            // 0x101 = increase code size
            if (curCode == 0x101) {
                g_pakDecompCodeSize++;
                continue;
            }

            unsigned int lookupCode = curCode;
            bool special = (g_pakDecompNextCode <= curCode);
            if (special) {
                // Special case: code not yet in table
                g_pakStringBuf[0] = (char)prevCode; // store first char of prev
                lookupCode = prevCode;
            }

            // 0x00425b3d: Decode string
            int charCount = pak_decomp_decode_string(special ? 1 : 0, lookupCode);

            // 0x00425b50: Get first character of decoded string
            unsigned char firstChar;
            if (special) {
                firstChar = (unsigned char)g_pakStringBuf[0];
            } else {
                firstChar = (unsigned char)g_pakStringBuf[charCount - 1];
            }

            // 0x00425b64: Output decoded string in correct order
            int outputCount = charCount;
            if (special) {
                // Output from stringBuf[charCount-1] down to stringBuf[1], then stringBuf[0]
                for (int i = charCount - 1; i >= 1; i--) {
                    ((unsigned char*)dst)[outPos] = (unsigned char)g_pakStringBuf[i];
                    outPos++;
                }
                ((unsigned char*)dst)[outPos] = (unsigned char)g_pakStringBuf[0];
                outPos++;
            } else {
                for (int i = charCount - 1; i >= 0; i--) {
                    ((unsigned char*)dst)[outPos] = (unsigned char)g_pakStringBuf[i];
                    outPos++;
                }
            }

            // 0x00425b98: Update state for next iteration
            prevCode = curCode;

            // 0x00425b90: Add new dictionary entry
            unsigned int newIdx = g_pakDecompNextCode;
            g_pakDecompNextCode++;
            g_pakDictPrefix[newIdx] = (int)prevCode;
            g_pakDictChar[newIdx] = (char)firstChar;
        }
    } while (true);
}
