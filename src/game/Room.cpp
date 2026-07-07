// Room.cpp - Room sprite, camera, and background management
// Decompiled from Ghidra
#include "../Globals.h"
#include "FileLoader.h"
#include "SpriteRenderer.h"
#include <cstdio>

extern void SetSpriteBufferFlag(void);

// ============================================================================
// cut_set (0x004628c0)
// Handles room cutscene/camera transition when entering a new room context.
// Restores texture bank, rebuilds sprite entries, sets up camera, loads
// background image, and processes sprite visibility flags.
// ============================================================================
void cut_set(void) // 0x004628c0
{
    printf("cut set  start\n");
    if ((unsigned char)g_SavedTextureBankID != 0) {
        int maskFrames;
        if ((g_stageId == 5) && (g_roomId == 0x13)) {
            maskFrames = 3;
        } else {
            maskFrames = 2;
        }
        StMask(0, maskFrames);

        *(unsigned short*)&g_TextureBankID = g_SavedTextureBankID;

        Room_LoadCameraSprites();

        if ((g_main_state_flags & 0x04000000) != 0) {
            return;
        }

        Room_SetupCamera();
        load_room_bg_image();
        Room_ApplySpriteFlags();
    }
    printf("cut set  end\n");
}

// ============================================================================
// Room_LoadCameraSprites (0x004757c0)
// Populate room sprite entries (g_RoomSprEntries) from RDT camera sprite
// overlay data. Called by cut_set during camera transitions.
// ============================================================================
void Room_LoadCameraSprites(void) // 0x004757c0
{
    unsigned short sprIndex = 0;
    unsigned char totalCount = 0;

    RDT_Camera* cameras = (RDT_Camera*)((char*)g_RdtPointer + sizeof(RDT));
    int* spriteGroupBase = (int*)cameras[g_roomCameraId].mask_pointer;
    int groupCount = spriteGroupBase[0];
    unsigned short* groupHeaders = (unsigned short*)(spriteGroupBase + 1);
    unsigned short* spriteData = groupHeaders + groupCount * 4;

    if (groupCount != 0) {
        load_room_masks(g_roomCameraId);
    }

    unsigned int grpIdx = 0;
    if (*spriteGroupBase != 0) {
        do {
            unsigned int sprCount = 0;
            if (groupHeaders[0] != 0) {
                unsigned short depthByte = (unsigned short)g_TextureDepthByte;
                unsigned short bankID = (unsigned short)g_TextureBankID;
                unsigned short* sprPtr = spriteData;
                do {
                    RoomSprEntry* entry = &g_RoomSprEntries[sprIndex];

                    entry->active = 1;
                    entry->id = (unsigned char)grpIdx + 1;
                    entry->texDesc.unk10 = (groupHeaders[1] & 0x3f) << 4;
                    entry->texDesc.printClutTint = (short)(depthByte + 0x1e0);
                    entry->texDesc.texU = (unsigned char)sprPtr[0];
                    entry->texDesc.texV = *((unsigned char*)sprPtr + 1);
                    entry->texDesc.screenX = (short)((unsigned short)(unsigned char)sprPtr[1] + groupHeaders[2] - 0xa0);
                    entry->texDesc.screenY = (short)(*((unsigned char*)sprPtr + 3) + groupHeaders[3] - 0x78);
                    entry->posData = sprPtr[2];

                    unsigned short flags = sprPtr[3];
                    entry->texDesc.depth = (short)((flags & 0x1f) + bankID);

                    if ((flags & 0xf000) == 0) {
                        spriteData = sprPtr + 6;
                        entry->texDesc.width = sprPtr[4];
                        entry->texDesc.height = sprPtr[5];
                    } else {
                        spriteData = sprPtr + 4;
                        unsigned short sz = (flags & 0xf1ff) >> 9;
                        entry->texDesc.width = sz;
                        entry->texDesc.height = sz;
                    }

                    entry->texDesc.flags = 0x40;
                    if ((flags & 0xc00) == 0) {
                        entry->texDesc.flags = 0x8000040;
                    }

                    sprCount++;
                    unsigned int transMode = (unsigned int)((flags & 0x180) >> 7) << 0x18;
                    sprIndex++;
                    entry->texDesc.flags |= transMode;
                    entry->texDesc.flags |= (unsigned int)((flags & 0x60) >> 5) << 0x1c;

                    sprPtr = spriteData;
                } while (sprCount < groupHeaders[0]);
            }
            totalCount = (unsigned char)sprIndex;
            groupHeaders += 4;
            grpIdx++;
        } while (grpIdx < (unsigned int)(*spriteGroupBase));
    }
    g_RdtPointer->sprites_count = totalCount;
}

// ============================================================================
// Room_SetupCamera (0x00462970)
// Set camera rendering parameters from RDT camera data.
// Sets the scene render param from camera FOV and computes the camera matrix.
// ============================================================================
void Room_SetupCamera(void) // 0x00462970
{
    RDT_Camera* cameras = (RDT_Camera*)((char*)g_RdtPointer + sizeof(RDT));
    set_title_render_param(cameras[g_roomCameraId].fov);
    MatrixToCamera((MATRIX*)&cameras[g_roomCameraId].cam_from_x);
}

// ============================================================================
// Room_ApplySpriteFlags (0x00432220)
// Process sprite visibility flags from DAT_00d22770.
// For each bit set in DAT_00d22770, disables (active=0) all room sprites
// whose id matches that bit index. Then clears DAT_00d22770.
// ============================================================================
int Room_ApplySpriteFlags(void) // 0x00432220
{
    unsigned int bitIdx = 0;
    do {
        if ((DAT_00d22770 & 1) != 0) {
            unsigned int sprIdx = 0;
            if (g_RdtPointer->sprites_count != 0) {
                RoomSprEntry* entry = g_RoomSprEntries;
                do {
                    if (entry->id == bitIdx) {
                        entry->active = 0;
                    }
                    entry++;
                    sprIdx++;
                } while (sprIdx < g_RdtPointer->sprites_count);
            }
        }
        DAT_00d22770 = (int)DAT_00d22770 >> 1;
        bitIdx++;
    } while (bitIdx < 0x10);
    DAT_00d22770 = 0;
    return 1;
}

// ============================================================================
// RoomSpr_SetActive (0x00476170) - Enable room sprite by ID
// Iterates through the room sprite table and sets active=1 for all entries
// whose id matches the given parameter. Called by SCD command 0x25.
// ============================================================================
void RoomSpr_SetActive(char id) // 0x00476170
{
    unsigned int i = 0;
    if (g_RdtPointer->sprites_count != 0) {
        RoomSprEntry* entry = g_RoomSprEntries;
        do {
            if (entry->id == id) {
                entry->active = 1;
            }
            entry++;
            i++;
        } while (i < g_RdtPointer->sprites_count);
    }
}

// ============================================================================
// RoomSpr_SetInactive (0x00476130) - Disable room sprite by ID
// Iterates through the room sprite table and sets active=0 for all entries
// whose id matches the given parameter. Called by SCD command 0x25.
// ============================================================================
void RoomSpr_SetInactive(char id) // 0x00476130
{
    unsigned int i = 0;
    if (g_RdtPointer->sprites_count != 0) {
        RoomSprEntry* entry = g_RoomSprEntries;
        do {
            if (entry->id == id) {
                entry->active = 0;
            }
            entry++;
            i++;
        } while (i < g_RdtPointer->sprites_count);
    }
}

// ============================================================================
// load_room_masks (0x00475a90)
// Load sprite mask/texture data for the specified camera.
// If the camera has sprite groups, loads the corresponding PAK file and
// sets up texture pages. Otherwise, deletes texture slot 4.
// ============================================================================
void load_room_masks(int param_1) // 0x00475a90
{
    RDT_Camera* cameras = (RDT_Camera*)((char*)g_RdtPointer + sizeof(RDT));
    int* spriteGroupPtr = (int*)cameras[param_1].mask_pointer;

    if (*spriteGroupPtr != 0) {
        g_maskPathTemplate[0x11] = (char)(g_stageId + 0x30);
        if (g_stageId > 4) {
            g_maskPathTemplate[0x11] = (char)(g_stageId + 0x2b);
        }
        g_maskPathTemplate[0x12] = (char)(g_roomId / 10 + 0x30);
        g_maskPathTemplate[0x14] = (char)(param_1 + '0');
        g_maskPathTemplate[0x13] = (char)(g_roomId % 10 + 0x30);

        void* pakData;
        if ((g_stageId == 2) && (g_roomId == 0x11)) {
            LoadFile(g_maskPathTemplate, &g_bgPakLoadBuffer, 0x20);
            pakData = &g_bgPakLoadBuffer;
        } else {
            pakData = &g_bgMaskDataBuffer[g_bgMaskOffsets[param_1]];
        }
        unpack_pakfile_(pakData, &g_TimImageBuffer__bitmap);
        TexturePage_SetupFull(&g_TimImageBuffer__bitmap, g_TextureBankID, g_TextureDepthByte, 0);
    } else {
        TexturePage_DeleteSet(4);
    }
}

// ============================================================================
// load_room_bg (0x00462b00) - Load room background images for all cameras
// For each camera in the room, loads the background PAK file, decompresses it,
// and either displays it immediately (mode 0) or caches it (mode non-zero).
// ============================================================================
void load_room_bg(void) // 0x00462b00
{
    if (g_bgCacheMode == 0) {
        int cameraIdx = 0;
        if (g_RdtPointer->cameras_count != 0) {
            do {
                DAT_004c2090 = g_hexCharTable[cameraIdx];
                g_bgPathTemplate[15] = g_hexCharTable[g_stageId + 1];
                g_bgPathTemplate[16] = g_hexCharTable[g_roomId >> 4];
                g_bgPathTemplate[17] = g_hexCharTable[g_roomId & 0xf];
                if (g_stageId > 4) {
                    g_bgPathTemplate[15] = g_bgPathTemplate[17] - 5;
                }
                g_bgPathTemplate[11] = g_bgPathTemplate[15];

                SetSpriteBufferFlag();
                LoadFile(g_bgPathTemplate, g_bgPakLoadBuffer, 2);
                unpack_pakfile_(g_bgPakLoadBuffer, g_TimImageBuffer);

                int width, height;
                if ((g_main_state_flags2 & 4) == 0) {
                    width = 320;
                    height = 240;
                } else {
                    width = 316;
                    height = 236;
                }

                display_image(cameraIdx, g_TimImageBuffer__bitmap, width, height);
                title_setup_texture_pages(cameraIdx, 1);
                cameraIdx++;
            } while (cameraIdx < (int)(unsigned int)g_RdtPointer->cameras_count);
        }
    } else {
        int totalSize = 0;
        int camCounter = 0;
        if (g_RdtPointer->cameras_count != 0) {
            do {
                g_bgPathTemplate[18] = g_hexCharTable[camCounter];
                g_bgPathTemplate[15] = g_hexCharTable[g_stageId + 1];
                g_bgPathTemplate[16] = g_hexCharTable[g_roomId >> 4];
                g_bgPathTemplate[17] = g_hexCharTable[g_roomId & 0xf];
                if (g_stageId > 4) {
                    g_bgPathTemplate[15] = g_bgPathTemplate[15] - 5;
                }
                camCounter++;
                g_bgPathTemplate[11] = g_bgPathTemplate[15];

                SetSpriteBufferFlag();
                g_bgCameraOffsets[camCounter] = totalSize;
                int fileSize = (int)LoadFile(g_bgPathTemplate, &g_bgCacheBuffer[totalSize], 2);
                totalSize += fileSize;
            } while (camCounter < (int)(unsigned int)g_RdtPointer->cameras_count);
        }
    }
}

// ============================================================================
// load_room_bg_image (0x004629c0) - Load a specific camera's background image
// Called during camera switches. In mode 0, just activates the cached texture.
// In cache mode or for special rooms, loads/decompresses the specific camera bg.
// ============================================================================
void load_room_bg_image(void) // 0x004629c0
{
    if (g_bgCacheMode == 0 && (g_stageId != 2 || g_roomId != 0x11)) {
        empty_00470960(g_roomCameraId);
        return;
    }

    void* pakData;
    if (g_stageId == 2 && g_roomId == 0x11) {
        g_bgPathTemplate[16] = g_hexCharTable[1];
        g_bgPathTemplate[17] = g_hexCharTable[1];
        g_bgPathTemplate[18] = g_hexCharTable[g_roomCameraId];
        g_bgPathTemplate[15] = g_hexCharTable[3];
        g_bgPathTemplate[11] = g_bgPathTemplate[15];
        LoadFile(g_bgPathTemplate, g_bgPakLoadBuffer, 2);
        pakData = g_bgPakLoadBuffer;
    } else {
        pakData = &g_bgCacheBuffer[g_bgCameraOffsets[g_roomCameraId]];
    }

    unpack_pakfile_(pakData, g_TimImageBuffer);

    int width, height;
    if ((g_main_state_flags2 & 4) == 0) {
        width = 320;
        height = 240;
    } else {
        width = 316;
        height = 236;
    }

    display_image(8, g_TimImageBuffer__bitmap, width, height);
    title_setup_texture_pages(8, 1);
    empty_00470960(8);
}

// ============================================================================
// load_room_bg_masks (0x004759d0) - Load room background masks for all cameras
// Masks are depth/occlusion data used for sprite rendering against the
// pre-rendered backgrounds.
// ============================================================================
void load_room_bg_masks(void) // 0x004759d0
{
    int totalSize = 0;
    int camCounter = 0;
    int camOffset = 0;
    int* offsetPtr = g_bgMaskOffsets;

    if (g_RdtPointer->cameras_count != 0) {
        do {
            RDT* pRdt = g_RdtPointer;
            *offsetPtr = totalSize;

            RDT_Camera* cameras = (RDT_Camera*)((char*)pRdt + sizeof(RDT));
            int* maskPointer = (int*)cameras[camCounter].mask_pointer;

            int fileSize;
            if (*maskPointer == 0) {
                fileSize = 0;
            } else {
                g_maskPathTemplate[0x11] = (char)(g_stageId + 0x30);
                if (g_stageId > 4) {
                    g_maskPathTemplate[0x11] = (char)(g_stageId + 0x2b);
                }
                g_maskPathTemplate[0x12] = (char)(g_roomId / 10 + 0x30);
                g_maskPathTemplate[0x13] = (char)(g_roomId % 10 + 0x30);
                g_maskPathTemplate[0x14] = (char)(camCounter + '0');

                fileSize = (int)LoadFile(g_maskPathTemplate, &g_bgMaskDataBuffer[totalSize], 0x20);
            }

            totalSize += fileSize;
            camOffset += 0x2C;
            offsetPtr++;
            camCounter++;
        } while (camCounter < (int)(unsigned int)g_RdtPointer->cameras_count);
    }
}
