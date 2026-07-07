// EntityModelLoader.cpp - Entity/player model and animation loading (decompiled)
#include "../Globals.h"
#include "../marni/MarniSystem.h"
#include "../marni/PSXTexture.h"
#include "FileLoader.h"
#include <cstdio>

// ============================================================================
// Extern data declarations (not yet extracted to Globals.h)
// ============================================================================
extern BYTE  g_entityModelBuffer[0xCC00];      // 0x00bf11c0
extern DWORD g_entityModelBuffer2[0xB4];       // 0x00bfddc0
extern DWORD g_animObjectBuffer[0x680];        // 0x00c133c0
extern DWORD DAT_004d2bd8;                     // 0x004d2bd8 - special model flag
extern DWORD DAT_004d2bf4;                     // 0x004d2bf4 - TMD processing flag
extern DWORD g_tmdAsyncData;                   // 0x008fc424 - TMD async data
extern DWORD DAT_004c1a2c;                     // 0x004c1a2c
extern DWORD DAT_00ae9f04;                     // 0x00ae9f04
extern BYTE  g_textureQueueData[40];           // 0x00d22740
extern DWORD g_animSlotIndex;                  // 0x008f8c78
extern DWORD g_tmdTextureAllocated[23];        // 0x00922a40
extern BYTE  g_psxTextureArray[23 * 0x1b60];  // 0x00a75168 - PSXTexture array
extern DWORD g_textureBankRedirect[23];        // 0x00aae2b0
extern DWORD DAT_004d6444;                     // 0x004d6444

// Player/weapon angle globals
extern int g_weaponAngle_Special;              // 0x004c2028
extern int g_weaponAngle_PrimX;                // 0x004c202c
extern int g_weaponAngle_PrimY;                // 0x004c2030
extern int g_weaponAngle_PrimZ;                // 0x004c2034
extern int g_weaponAngle_Sec1X;                // 0x004c2038
extern int g_weaponAngle_Sec1Y;                // 0x004c203c
extern int g_weaponAngle_Sec1Z;                // 0x004c2040
extern int g_weaponAngle_Sec2X;                // 0x004c2044
extern int g_weaponAngle_Sec2Y;                // 0x004c2048
extern int g_weaponAngle_Sec2Z;                // 0x004c204c

extern DWORD g_scaDataTable[4];                // SCA collision data table

// Forward declarations for functions in other files
void InitPlayerEntity(void);
void Object_DeleteAll(int a);
void ComplexTmdObjectSetup(int* param_1);
unsigned int AsyncCreateTmdObject(unsigned int param1, unsigned int param2, unsigned int param3);
int __stdcall VideoDriver_ClearState348(void* obj, void* context);

// ============================================================================
// EMD model path table (0x004c1320)
// 0x35 entries per character block, each entry is 0x11 (17) bytes
// Block 0: Chris (characterId & 1 == 0), Block 1: Jill/Rebecca (characterId & 1 == 1)
// Indexed by: base + ((characterId & 1) * 0x35 + entity_id) * 0x11
// ============================================================================
static const char g_emdPathTable[][0x35][17] = {
    {   // Chris block (characterId & 1 == 0)
        "enemy/char10.emd",
        "enemy/char11.emd",
        "enemy/char12.emd",
        "enemy/char13.emd",
        "enemy/em1000.emd",
        "enemy/em1001.emd",
        "enemy/em1002.emd",
        "enemy/em1003.emd",
        "enemy/em1004.emd",
        "enemy/em1005.emd",
        "enemy/em1006.emd",
        "enemy/em1007.emd",
        "enemy/em1008.emd",
        "enemy/em1009.emd",
        "enemy/em100a.emd",
        "enemy/em100b.emd",
        "enemy/em100c.emd",
        "enemy/em100d.emd",
        "enemy/em100e.emd",
        "enemy/em100f.emd",
        "enemy/em1010.emd",
        "enemy/em1011.emd",
        "enemy/em1012.emd",
        "enemy/em1013.emd",
        "enemy/em1014.emd",
        "enemy/em1015.emd",
        "enemy/em100a.emd",
        "enemy/em100a.emd",
        "enemy/em100a.emd",
        "enemy/em100a.emd",
        "enemy/em100a.emd",
        "enemy/em100a.emd",
        "enemy/em100a.emd",
        "enemy/em100a.emd",
        "enemy/em100a.emd",
        "enemy/em1020.emd",
        "enemy/em1021.emd",
        "enemy/em1022.emd",
        "enemy/em1023.emd",
        "enemy/em1024.emd",
        "enemy/em1025.emd",
        "enemy/em1026.emd",
        "enemy/em1027.emd",
        "enemy/em1028.emd",
        "enemy/em1029.emd",
        "enemy/em102a.emd",
        "enemy/em102b.emd",
        "enemy/em102c.emd",
        "enemy/em102d.emd",
        "enemy/em102e.emd",
        "enemy/em1030.emd",
        "enemy/em1032.emd",
    },
    {   // Jill/Rebecca block (characterId & 1 == 1)
        "enemy/char10.emd",
        "enemy/char11.emd",
        "enemy/char12.emd",
        "enemy/char13.emd",
        "enemy/em1100.emd",
        "enemy/em1101.emd",
        "enemy/em1102.emd",
        "enemy/em1103.emd",
        "enemy/em1104.emd",
        "enemy/em1105.emd",
        "enemy/em1106.emd",
        "enemy/em1107.emd",
        "enemy/em1108.emd",
        "enemy/em1109.emd",
        "enemy/em110a.emd",
        "enemy/em110b.emd",
        "enemy/em110c.emd",
        "enemy/em110d.emd",
        "enemy/em110e.emd",
        "enemy/em110f.emd",
        "enemy/em1110.emd",
        "enemy/em1111.emd",
        "enemy/em1112.emd",
        "enemy/em1113.emd",
        "enemy/em1114.emd",
        "enemy/em1115.emd",
        "enemy/em110a.emd",
        "enemy/em110a.emd",
        "enemy/em110a.emd",
        "enemy/em110a.emd",
        "enemy/em110a.emd",
        "enemy/em110a.emd",
        "enemy/em110a.emd",
        "enemy/em110a.emd",
        "enemy/em110a.emd",
        "enemy/em1020.emd",
        "enemy/em1021.emd",
        "enemy/em1022.emd",
        "enemy/em1023.emd",
        "enemy/em1024.emd",
        "enemy/em1025.emd",
        "enemy/em1026.emd",
        "enemy/em1027.emd",
        "enemy/em1028.emd",
        "enemy/em1029.emd",
        "enemy/em102a.emd",
        "enemy/em102b.emd",
        "enemy/em102c.emd",
        "enemy/em102d.emd",
        "enemy/em102e.emd",
        "enemy/em1031.emd",
        "enemy/em1033.emd",
    }
};

// ============================================================================
// Weapon animation path table (0x004c1ca8)
// 0xe entries per character block, each entry is 0x10 (16) bytes
// Block 0: Chris, Block 1: Jill, Block 2: ?, Block 3: Rebecca
// Indexed by: base + (weapon_id + (characterId & 3) * 0xe) * 0x10
// ============================================================================
static const char g_weaponPathTable[][0xe][16] = {
    {   // Chris block (characterId & 3 == 0)
        "players/w00.emw",
        "players/w01.emw",
        "players/w02.emw",
        "players/w03.emw",
        "players/w04.emw",
        "players/w04.emw",
        "players/w05.emw",
        "players/w06.emw",
        "players/w06.emw",
        "players/w06.emw",
        "players/w07.emw",
        "players/w0b.emw",
        "players/w18.emw",
        "players/w08.emw",
    },
    {   // Jill block (characterId & 3 == 1)
        "players/w10.emw",
        "players/w11.emw",
        "players/w12.emw",
        "players/w13.emw",
        "players/w14.emw",
        "players/w14.emw",
        "players/w15.emw",
        "players/w16.emw",
        "players/w16.emw",
        "players/w16.emw",
        "players/w17.emw",
        "players/w1b.emw",
        "players/w18.emw",
        "players/w08.emw",
    },
    {   // characterId & 3 == 2 (unused in RE1)
        "players/w00.emw",
        "players/w01.emw",
        "players/w02.emw",
        "players/w03.emw",
        "players/w04.emw",
        "players/w04.emw",
        "players/w05.emw",
        "players/w06.emw",
        "players/w06.emw",
        "players/w06.emw",
        "players/w07.emw",
        "players/w0b.emw",
        "players/w18.emw",
        "players/w08.emw",
    },
    {   // Rebecca block (characterId & 3 == 3)
        "players/w30.emw",
        "players/w11.emw",
        "players/w32.emw",
        "players/w13.emw",
        "players/w14.emw",
        "players/w14.emw",
        "players/w15.emw",
        "players/w16.emw",
        "players/w16.emw",
        "players/w16.emw",
        "players/w17.emw",
        "players/w1b.emw",
        "players/w18.emw",
        "players/w08.emw",
    }
};

// TMD texture header struct — defined in TmdAnimation.cpp, declared here for extern visibility
#pragma pack(push, 1)
struct TmdTextureHeader {
    int   count;       short field_04;    short field_06;
    short field_08;    short field_0A;    int   dataPtr;
    short field_10;    short field_12;    short field_14;
    short field_16;    int   ptr_10;
};
#pragma pack(pop)

// ============================================================================
// TMD animation functions — defined in TmdAnimation.cpp
// ============================================================================
extern void ResolveAnimPointers(unsigned char* data);
extern void SetAnimSlot(AnimSlot* slots, int slotPtr, int index);
extern unsigned int FindMinClutDepth(AnimSlot* slot);
extern unsigned int* CreateAnimObject(int slotPtr, unsigned int* param2);
extern unsigned int ProcessTmdTextures(char param1, unsigned int* param2, int param3, int param4);
extern void ClearTmdProcessingFlag(void);
extern void SetSpriteBufferFlag(void);
extern unsigned char QueueTextureForProcessing(char param1, unsigned char param2);
extern void ParseTmdTextureHeader(void* data, TmdTextureHeader* header);
extern void TmdProcessingCallback(void);
extern void ProcessTmdAsync(unsigned int param1);

// ============================================================================
// FUN_00462790 (0x00462790) - Adjust weapon animation positions
// Modifies weapon animation vertex positions based on character offsets.
// param1: weapon type index (0 = basic weapons, 1 = special)
// ============================================================================
void AdjustWeaponAnimationPositions(int param1)
{
    unsigned int animBuffer = g_playerEntity.jointMoveData0;
    unsigned int animEnd = g_playerEntity.jointMoveData1;
    int local_8 = 5;

    do {
        unsigned int uVar5 = *(unsigned int*)(animEnd + local_8 * 4);
        unsigned int* puVar6 = (unsigned int*)(((uVar5 >> 16) & 0xFFFFFFFC) + animEnd);
        for (unsigned int count = uVar5 & 0xFFFF; count != 0; count--) {
            unsigned int uVar4 = *puVar6;
            puVar6++;
            int offset = ((*(short*)(animBuffer + 2) >> 16) * (uVar4 & 0xFFFF) + (*(short*)animBuffer >> 16)) & 0xFFFFFFFC;
            if (param1 == 0) {
                short* psVar1;
                psVar1 = (short*)(animBuffer + offset + 0x42);
                *psVar1 = *psVar1 + (short)((g_weaponAngle_PrimX << 12) / 360);
                psVar1 = (short*)(animBuffer + offset + 0x44);
                *psVar1 = *psVar1 + (short)((g_weaponAngle_PrimY << 12) / 360);
                psVar1 = (short*)(animBuffer + offset + 0x46);
                *psVar1 = *psVar1 + (short)((g_weaponAngle_PrimZ << 12) / 360);
                if (local_8 == 6) {
                    psVar1 = (short*)(animBuffer + offset + 0x48);
                    *psVar1 = *psVar1 + (short)((g_weaponAngle_Sec2X << 12) / 360);
                    psVar1 = (short*)(animBuffer + offset + 0x4a);
                    *psVar1 = *psVar1 + (short)((g_weaponAngle_Sec2Y << 12) / 360);
                    psVar1 = (short*)(animBuffer + offset + 0x4c);
                    *psVar1 = *psVar1 + (short)((g_weaponAngle_Sec2Z << 12) / 360);
                } else {
                    psVar1 = (short*)(animBuffer + offset + 0x48);
                    *psVar1 = *psVar1 + (short)((g_weaponAngle_Sec1X << 12) / 360);
                    psVar1 = (short*)(animBuffer + offset + 0x4a);
                    *psVar1 = *psVar1 + (short)((g_weaponAngle_Sec1Y << 12) / 360);
                    psVar1 = (short*)(animBuffer + offset + 0x4c);
                    *psVar1 = *psVar1 + (short)((g_weaponAngle_Sec1Z << 12) / 360);
                }
            } else {
                short* psVar1 = (short*)(animBuffer + offset + 100);
                *psVar1 = *psVar1 + (short)((g_weaponAngle_Special << 12) / 360);
            }
        }
        local_8++;
    } while (local_8 < 8);
}

// ============================================================================
// FUN_0048bc60 (0x0048bc60) - Set joint count and joint structs pointer
// Reads the joint count from the animation header (+4 byte) and sets up
// the joints_structs pointer at the current g_loadDataDestPointer.
// ============================================================================
void Entity_SetJoints(Entity* em, unsigned int param2)
{
    unsigned char jointCount = *(unsigned char*)(em->animHeader + 4);
    em->jointCount = jointCount;
    em->jointsStructs = (JointStruct*)g_loadDataDestPointer;
    g_loadDataDestPointer = (void*)((unsigned int)g_loadDataDestPointer + (param2 & 0xFFFC) * jointCount);
}

// ============================================================================
// FUN_0048b6b0 (0x0048b6b0) - Initialize animation structure
// Resolves pointers in animation data and sets up initial animation state.
// ============================================================================
void InitAnimStructure(void* animHeaderValue)
{
    AnimDataHeader* header = (AnimDataHeader*)animHeaderValue;
    ResolveAnimPointers(&header->resolved);
    SetAnimSlot(header->slots, (int)&ENTITY->unk_0c, 0);
    *(DWORD*)&ENTITY->unk_10 = (DWORD)&ENTITY->scaMatrixData;
    ENTITY->blend_counter = 0;
}

// ============================================================================
// FUN_0048b9e0 (0x0048b9e0) - Set up joint structures
// Iterates through all joints and initializes them with animation data.
// Returns the next available pointer after animation object data.
// ============================================================================
unsigned int SetupJointStructures(unsigned int param1)
{
    unsigned char local_1 = 0;
    JointStruct* joint = ENTITY->jointsStructs;
    unsigned int uVar2 =     ENTITY->modelLoadBuffer;
    unsigned char jointCount = ENTITY->jointCount;

    if (jointCount == 0) {
        return param1;
    }

    do {
        SetAnimSlot((AnimSlot*)uVar2, (int)&joint->anim_field, local_1);
        joint->index = local_1;
        joint->flags = 3;
        joint->data_ptr = &joint->scale_flag;
        joint->field_1c = 0;
        joint->scale_flag = 1;
        joint->anim_object = 0;
        joint->field_02 = 0;
        joint->anim_field = 0;
        param1 = (unsigned int)CreateAnimObject((int)&joint->anim_field, (unsigned int*)param1);

        if (ENTITY->id == 0x29) {
            switch (local_1) {
                case 4:
                case 5:
                case 7:
                case 8:
                    joint->flags = 0;
                    break;
            }
        }

        joint++;
        local_1++;
    } while (local_1 < jointCount);

    return param1;
}

// ============================================================================
// FUN_0048bad0 (0x0048bad0) - Reset joint transforms to identity
// Sets all joint transforms to identity matrix and initial joint positions.
// ============================================================================
void ResetJointTransforms(void)
{
    unsigned char jointCount = ENTITY->jointCount;
    short* psVar2 = (short*)(ENTITY->animHeader + 8);
    JointStruct* joint = ENTITY->jointsStructs;

    ENTITY->jointResetFlag = 1;

    for (; jointCount != 0; jointCount--) {
        joint->transform = g_identityMatrixData;
        joint->transform.t[0] = (int)*psVar2;
        joint->transform.t[1] = (int)psVar2[1];
        joint->transform.t[2] = (int)psVar2[2];
        joint->rotation.x = 0;
        joint->rotation.y = 0;
        joint->rotation.z = 0;
        joint->rotDeltaX = 0;
        joint->rotDeltaY = 0;
        joint->rotDeltaZ = 0;
        psVar2 += 3;
        joint++;
    }
}

// ============================================================================
// InitScaMatrix (0x00483520) - Initialize SCA collision matrix data
// Sets up a 3x3 SCA collision response matrix with identity values.
// param1: owner pointer (set as back-reference at scaMatrixData->owner if non-zero)
// param2: destination ScaMatrixData buffer (scaMatrixData field of Entity/PlayerEntity)
// ============================================================================
void InitScaMatrix(int param1, ScaMatrixData* scaData)
{
    scaData->field_00 = 0;
    scaData->owner = (unsigned int)param1;
    if (param1 != 0) {
        *(unsigned int**)(param1 + 0x4c) = (unsigned int*)scaData;
    }
    scaData->field_4c = 0;

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            unsigned short val = (unsigned short)(-(i == j) & 0x1000);
            scaData->localMatrix.m[i][j] = val;
            scaData->worldMatrix.m[i][j] = val;
        }
        scaData->localMatrix.t[i] = 0;
        scaData->worldMatrix.t[i] = 0;
    }
}

// ============================================================================
// LoadEntityEMD (0x00462370) - Load entity EMD model file
// Loads the EMD 3D model for the player or enemy entity.
// Sets up animation data, texture references, and model geometry pointers.
// ============================================================================
void LoadEntityEMD(Entity* em, unsigned char entity_id)
{
    unsigned char bVar5 = g_TextureBankID;
    unsigned char bVar6 = g_TextureDepthByte;

    if ((g_main_state_flags2 & 0x4000000) != 0 && entity_id < 2) {
        entity_id = (unsigned char)DAT_004d6444 + 0x33;
    }

    sprintf(FILE_PATH, "%s%s",
            ".\\usa\\",
            g_emdPathTable[g_playerEntity.id & 1][entity_id]);
    SetSpriteBufferFlag();

    unsigned int fileSize = LoadFile(FILE_PATH, g_loadDataDestPointer, 32);
    int data_pointer = (int)g_loadDataDestPointer;

    if ((g_main_state_flags2 & 0x4000000) != 0 && (unsigned int)DAT_004d6444 - entity_id == -51) {
        entity_id = g_playerEntity.id & 1;
    }

    unsigned int* puVar2 = (unsigned int*)(((fileSize & 0xFFFFFFFC) - 0x14) + data_pointer);
    g_loadDataDestPointer = (void*)(data_pointer + (*(unsigned int*)(((fileSize & 0xFFFFFFFC) - 4) + data_pointer) & 0xFFFFFFFC));

    unsigned char entityType = ENTITY->id;
    if (entityType > 0x1f) {
        unsigned short uVar7 = (unsigned short)g_TextureDepthByte;
        ENTITY->attacking_direction = (char)uVar7;
        ENTITY->dir_control_flags = (char)(uVar7 >> 8);
        *(unsigned short*)&ENTITY->texBank = (unsigned short)g_TextureBankID;
    }

    DAT_004d2bd8 = 0;
    switch (entity_id) {
        case 7:
        case 8:
        case 11:
        case 13:
        case 23:
            ClearTmdProcessingFlag();
            break;
        case 18:
            DAT_004d2bd8 = 1;
            break;
    }

    ProcessTmdAsync((unsigned int)g_loadDataDestPointer);

    if ((DAT_004c1a2c >> (entityType & 0x1f) & 1) != 0) {
        QueueTextureForProcessing(bVar6, entityType);
    }

    int texDataPtr = (puVar2[3] & 0xFFFFFFFC) + data_pointer;
    em->modelLoadBuffer = texDataPtr;
    ProcessTmdTextures(2, (unsigned int*)texDataPtr, bVar5, bVar6);
    em->animBase = (puVar2[2] & 0xFFFFFFFC) + data_pointer;
    em->animHeader = (puVar2[1] & 0xFFFFFFFC) + data_pointer;

    if (*puVar2 != 0) {
        g_playerEntity.emdScratchPtr1 = data_pointer;
        g_playerEntity.emdScratchPtr2 = (*puVar2 & 0xFFFFFFFC) + data_pointer;
    }
}

// ============================================================================
// LoadEntityModel (0x0048b630) - Load entity 3D model
// Loads the EMD model for the current entity, sets up joint data,
// animation structures, and resets transforms.
// ============================================================================
void LoadEntityModel(void)
{
    void* data_pointer_bkp = g_loadDataDestPointer;

    g_playerEntity.modelLoadBuffer = (DWORD)&g_entityModelBuffer;
    g_loadDataDestPointer = &g_entityModelBuffer;

    LoadEntityEMD(ENTITY, g_playerEntity.id & 3);

    Entity_SetJoints(ENTITY, sizeof(JointStruct));

    g_loadDataDestPointer = data_pointer_bkp;

    InitAnimStructure((void*)g_playerEntity.modelLoadBuffer);

    g_playerEntity.jointCount++;

    SetupJointStructures((unsigned int)&g_entityModelBuffer2);

    g_playerEntity.jointCount--;

    ResetJointTransforms();
}

// ============================================================================
// LoadEquippedWeaponAnimation (0x00462620)
// Loads the animation file for the currently equipped weapon.
// Sets up weapon model geometry, texture, and animation data.
// ============================================================================
void LoadEquippedWeaponAnimation(unsigned char weapon_id, unsigned char param_2, unsigned int anim_buffer, unsigned int param_4)
{
    JointStruct* joint = &g_playerEntity.jointsStructs[param_2];

    if (weapon_id > 0x6e) {
        weapon_id = 0xd - (weapon_id == 0x6f);
    }

    sprintf(FILE_PATH, "%s%s",
            ".\\usa\\",
            g_weaponPathTable[g_playerEntity.id & 3][weapon_id]);
    SetSpriteBufferFlag();

    unsigned int fileSize = LoadFile(FILE_PATH, (void*)anim_buffer, 32);
    g_playerEntity.jointMoveData0 = anim_buffer;

    unsigned int* puVar1 = (unsigned int*)(((fileSize & 0xFFFFFFFC) - 8) + (int)anim_buffer);
    g_playerEntity.jointMoveData1 = (*puVar1 & 0xFFFFFFFC) + (int)anim_buffer;

    if (weapon_id > 0xb) {
        AdjustWeaponAnimationPositions(weapon_id - 0xc);
    }

    if (weapon_id == 0) {
        joint->anim_slot_ptr = g_playerEntity.weaponPartAnimSlot;
        joint->anim_object = (void*)g_playerEntity.weaponPartAnimObject;
    } else {
        joint->anim_slot_ptr = (puVar1[1] & 0xFFFFFFFC) + (int)anim_buffer;
        unsigned char prevDepth = g_TextureDepthByte;
        unsigned char prevBank = g_TextureBankID;
        g_TextureDepthByte = 7;
        g_TextureBankID = 0x16;
        ProcessTmdTextures(2, (unsigned int*)joint->anim_slot_ptr, 0x16, 7);
        g_TextureBankID = prevBank;
        g_TextureDepthByte = prevDepth;
        joint->anim_slot_ptr += 0xc;
        joint->anim_object = (void*)param_4;
    }

    CreateAnimObject((int)&joint->anim_field, (unsigned int*)joint->anim_object);
}

// ============================================================================
// FUN_00459da0 (0x00459da0) - Set body part pointers for current weapon
// Sets the weaponPartAnimSlot/weaponPartAnimObject body part data from the joints at the given index.
// param1: joint index (0xe for player, selects which body part handles weapon)
// ============================================================================
void SetWeaponBodyParts(unsigned char param1)
{
    JointStruct* joint = &ENTITY->jointsStructs[param1];
    ENTITY->weaponPartAnimSlot = joint->anim_slot_ptr;
    ENTITY->weaponPartAnimObject = (unsigned int)joint->anim_object;
}

// ============================================================================
// FUN_00429d30 (0x00429d30) - Clear animation timing values
// Clears two 16-bit values at 0x7b8 and 0x7ba within the joint data buffer.
// ============================================================================
void ClearAnimTiming(void)
{
    JointStruct* joints = g_playerEntity.jointsStructs;
    joints[15].velZ = 0;
    joints[15].rotDeltaX = 0;
}

// ============================================================================
// SetupCharacterData (0x00494fc0)
// Sets up the player character: loads the player model, weapon animation,
// initializes position from last safe position, and sets up SCA collision data.
// Called from InitializeGame() after loading bio_card.dat and item images.
// ============================================================================
void SetupCharacterData(void)
{
    ENTITY = reinterpret_cast<Entity*>(&g_playerEntity);
    InitPlayerEntity();
    g_TextureDepthByte = 7;
    g_TextureBankID = 0x16;
    Object_DeleteAll(0);
    LoadEntityModel();
    InitScaMatrix(0, &g_playerEntity.scaMatrixData);
    SetWeaponBodyParts(0xe);
    g_playerEntity.equippedWeaponId = 0;
    if (g_equippedItemId != 0) {
        g_playerEntity.equippedWeaponId = g_ItemsSlots[(unsigned int)g_equippedItemId].Id;
    }
    LoadEquippedWeaponAnimation(
        g_playerEntity.equippedWeaponId, 0xe, (unsigned int)g_animationBuffer, (unsigned int)&g_animObjectBuffer);
    g_playerEntity.unk_8e = 0;
    g_playerEntity.jointsStructs[1].rotDeltaX = 0;
    g_playerEntity.jointsStructs[1].rotDeltaY = 0;
    g_playerEntity.jointsStructs[1].rotDeltaZ = 0x10;
    g_playerEntity.Sca_info = g_scaDataTable[(g_playerEntity.id & 1) * 2];
    ClearAnimTiming();
}

// ============================================================================
// set_player_animations_functions (0x00409a00)
// Populates the player animation function pointer table at g_playerAnimFunctions
// These function pointers are indexed by g_playerEntity.animFrameId * 4
// and dispatched by FUN_00495290 during gameplay.
// ============================================================================
void set_player_animations_functions(void)
{
    g_playerAnimFunctions[0]   = (void*)player_anim_attack_recoil;       // 0x00437a80 @ offset 0x00
    g_playerAnimFunctions[40]  = (void*)player_anim_simple_recovery;     // 0x0049abb0 @ offset 0xA0
    g_playerAnimFunctions[24]  = (void*)player_anim_multi_attack;        // 0x00430130 @ offset 0x60
    g_playerAnimFunctions[44]  = (void*)player_anim_dispatch_4c2ac8;     // 0x004196d0 @ offset 0xB0
    g_playerAnimFunctions[7]   = (void*)player_anim_crawling;            // 0x0048f060 @ offset 0x1C
    g_playerAnimFunctions[27]  = (void*)player_anim_set_attacked_flag;   // 0x00469400 @ offset 0x6C
    g_playerAnimFunctions[46]  = (void*)player_anim_dispatch_4ba360;     // 0x00468e10 @ offset 0xB8
    g_playerAnimFunctions[28]  = (void*)player_anim_dispatch_4c10b0;     // 0x0043b980 @ offset 0x70
    g_playerAnimFunctions[30]  = (void*)player_anim_poison_death;        // 0x004401c0 @ offset 0x78
    g_playerAnimFunctions[49]  = (void*)player_anim_death_billboard;     // 0x00440230 @ offset 0xC4
    g_playerAnimFunctions[31]  = (void*)player_anim_limb_physics;        // 0x00424fb0 @ offset 0x7C
    g_playerAnimFunctions[50]  = (void*)player_anim_enemy_interact;      // 0x00424de0 @ offset 0xC8
    g_playerAnimFunctions[51]  = (void*)player_anim_death_alt;           // 0x004088f0 @ offset 0xCC
    g_playerAnimFunctions[34]  = (void*)player_anim_dispatch_4b1a90;     // 0x0045c460 @ offset 0x88
    Task_sleep(1);
}
