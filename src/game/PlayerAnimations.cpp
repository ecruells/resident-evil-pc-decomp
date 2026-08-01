// PlayerAnimations.cpp - Player animation state machines (decompiled from Ghidra)
#include "../Globals.h"
#include "../marni/MarniSystem.h"
#include <cstdio>
#include "../DebugPrint.h"

// ============================================================================
// Player animation function stubs (populated into g_playerAnimFunctions by set_player_animations_functions)
// These are dispatched by FUN_00495290 based on g_playerEntity.animFrameId
// Full implementations to be decompiled from Ghidra.
// ============================================================================
// 0x00437a80
void player_anim_attack_recoil(void) {
    char cVar1;
    switch ((unsigned int)g_playerEntity.action_state) {
    case 0:
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf = 0;
        g_playerEntity.unk_8c = 0;
        g_playerEntity.action_state = 1;
        g_playerEntity.scaMatrixData.localMatrix.t[0] = (int)*(unsigned short*)((char*)ENTITY + 0xC6);
        g_playerEntity.scaMatrixData.localMatrix.t[2] = (int)*(unsigned short*)((char*)ENTITY + 0xC8);
        Play3DSnd(3, 0, 0, (int)&g_playerEntity.scaMatrixData.localMatrix.t);
        g_playerEntity.flags |= 2;
    case 1:
        entity_apply_anim_vertex((Entity*)&g_playerEntity, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2);
        cVar1 = Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400);
        if (cVar1 != 0) {
            g_playerEntity.action_state = 2;
            g_playerEntity.unk_bf = 0;
            g_playerEntity.attackAnim++;
            return;
        }
        break;
    case 2:
        entity_apply_anim_vertex((Entity*)&g_playerEntity, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2);
        Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400);
        return;
    case 3:
        g_playerEntity.attackAnim++;
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf = 0;
        g_playerEntity.action_state = 4;
    case 4:
        entity_apply_anim_vertex((Entity*)&g_playerEntity, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2);
        cVar1 = Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400);
        if (cVar1 != 0) {
            g_playerEntity.animationId = 1;
            g_playerEntity.animFrameId = 0;
            g_playerEntity.action_behavior = 0;
            g_playerEntity.action_state = 0;
            g_playerEntity.flags &= 0xfd;
            if ((g_playerEntity.attackAnim == 2) || (g_playerEntity.attackAnim == 8)) {
                g_playerEntity.directionAngle += 0x800;
            }
            g_playerEntity.isBeingAttackedFlag = 0;
            if (g_playerEntity.attackTimer == 0) {
                g_playerEntity.attackTimer = 0x96;
            }
        }
    }
}
// 0x0049abb0
void player_anim_simple_recovery(void) {
    char cVar1;
    if (g_playerEntity.action_state > 1) return;
    if (g_playerEntity.action_state == 0) {
        g_playerEntity.action_state = 1;
        g_playerEntity.move_speed_current = 0;
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf = 0;
        g_playerEntity.unk_8c = 0;
        g_playerEntity.attackAnim = 2;
        Play3DSnd(3, 2, 0, (int)&g_playerEntity.scaMatrixData.localMatrix.t);
        return;
    }
    if (g_playerEntity.animation_frame_id == 0xf) {
        PlayEntitySnd(2);
    }
    if (g_playerEntity.health >= 0) {
        entity_apply_anim_vertex((Entity*)&g_playerEntity, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2);
    }
    cVar1 = Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x200);
    if (cVar1 != 0) {
        g_playerEntity.directionAngle += 0x800;
        g_playerEntity.action_state++;
    }
}
// 0x00430130
void player_anim_multi_attack(void) {
    char cVar1;
    switch ((unsigned int)g_playerEntity.action_state) {
    case 0:
        g_playerEntity.action_state = 1;
        g_playerEntity.unk_8c = 3;
        g_playerEntity.move_speed_current = 0;
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf = 0;
        g_playerEntity.attackAnim = 0;
    case 1:
        cVar1 = Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400);
        g_playerEntity.action_state += cVar1;
        break;
    case 2:
        g_playerEntity.attackAnim = 1;
        g_playerEntity.action_state = 3;
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf = 0;
    case 3:
        Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400);
        break;
    case 4:
        g_playerEntity.attackAnim = 2;
        g_playerEntity.action_state = 5;
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf = 0;
    case 5:
        cVar1 = Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400);
        if (cVar1 != 0) {
            g_playerEntity.animationId = 1;
            g_playerEntity.animFrameId = 0;
            g_playerEntity.action_behavior = 0;
            g_playerEntity.action_state = 0;
            g_playerEntity.isBeingAttackedFlag = 0;
        }
    }
    if ((int)(unsigned int)g_playerEntity.animation_frame_id <= (int)((unsigned int)(g_playerEntity.id & 1) * -4 + 10)) {
        EntityUpdateWeaponJoint(0);
        return;
    }
    EntityUpdateWeaponJoint(1);
}
// 0x004196d0 — Death animation with billboards (5 states)
void player_anim_dispatch_4c2ac8(void) {
    JointStruct* pJVar1;
    char cVar2;
    switch (g_playerEntity.action_state) {
    case 0:
        g_playerEntity.action_state = 1;
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf = 0;
        g_playerEntity.attackAnim = g_playerEntity.isBeingAttackedFlag - 1;
        g_playerEntity.unk_bc = 0xb4;
        g_playerEntity.unk_8c = 3;
        g_message_flags &= 0xffbf;
        g_playerEntity.jointsStructs[1].flags |= 8;
        pJVar1 = g_playerEntity.jointsStructs;
        {
            int* deadData = (int*)g_deadMoveValue;
            g_playerPosScratch.x = deadData[5];
            g_playerPosScratch.y = deadData[6];
            g_playerPosScratch.z = deadData[7];
            g_playerPosScratch.pad = deadData[8];
        }
        Effect_CreateBillboard(0, 3, 0, &pJVar1[1].world, &g_playerPosScratch, 0);
        Effect_CreateBillboard(0, 3, 0, (void*)g_deadMoveValue, &pJVar1[1].world.t, 0);
        g_playerEntity.health = -1;
        // fall through
    case 1:
        pJVar1 = g_playerEntity.jointsStructs;
        if (ENTITY->animation_frame_id < 10) {
            int* deadData = (int*)g_deadMoveValue;
            g_playerPosScratch.x = deadData[5];
            g_playerPosScratch.z = deadData[7];
            g_playerPosScratch.pad = deadData[8];
            g_playerPosScratch.y = -0x898;
            Effect_CreateBillboard(0, 0, 0, (void*)((char*)ENTITY + 0x20), &g_playerPosScratch, 0);
        }
        if (ENTITY->animation_frame_id == 3) {
            JointApplyColorTint(g_playerEntity.jointsStructs, 0x30, 0x80820, &DAT_00606060);
            JointApplyColorTint(pJVar1 + 9, 0x30, 0x80820, &DAT_00606060);
            JointApplyColorTint(pJVar1 + 12, 0x30, 0x80820, &DAT_00606060);
        }
        if (ENTITY->animation_frame_id == 0x2a) {
            PlayEntitySnd(2);
        }
        cVar2 = Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400);
        g_playerEntity.action_state += cVar2;
        EntityUpdateWeaponJoint(0);
        break;
    case 2: {
        g_svecScratch.y = 0;
        g_svecScratch.z = 0;
        g_svecScratch.x = -900;
        {
            int* src = (int*)g_deadMoveValue;
            int* dst = (int*)&g_matrixScratch;
            for (int i = 0; i < 8; i++) {
                dst[i] = src[i];
            }
        }
        RotMatrixY((int)(unsigned short)g_playerEntity.directionAngle, &g_matrixScratch);
        ApplyMatrixSV(&g_matrixScratch, &g_svecScratch, &g_svecScratch);
        ENTITY->pushVelocity.x = ENTITY->pushVelocity.x + g_svecScratch.x;
        ENTITY->pushVelocity.y = ENTITY->pushVelocity.y + g_svecScratch.y;
        ENTITY->pushVelocity.z = ENTITY->pushVelocity.z + g_svecScratch.z;
        BillboardSetColor(&ENTITY->pushVelocity, 1, 2, 0x00ffff50);
        BillboardAdjSize(&ENTITY->pushVelocity, 0xffffff38, 0xffffff38);
        g_playerEntity.action_state = 3;
        g_playerEntity.isBeingAttackedFlag = 0x80;
        return;
    }
    case 3:
        BillboardAdjSize(&ENTITY->pushVelocity, 0x10, 0x10);
        if (g_playerEntity.unk_bc == 0xa0) {
            g_fade_type_id = 1;
            g_fading_counter = 0x100;
            fade_update();
        }
        g_playerEntity.unk_bc--;
        if (g_playerEntity.unk_bc == 0x20) {
            g_playerEntity.animationId = 4;
            return;
        }
        break;
    case 4:
        g_playerEntity.animationId = 4;
        return;
    }
}
// 0x0048f060
void player_anim_crawling(void) {
    char cVar1;
    if (g_playerEntity.action_state == 0) {
        g_playerEntity.action_state = 1;
        g_playerEntity.unk_8c = 3;
        g_playerEntity.move_speed_current = 0;
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf = 0;
        g_playerEntity.attackAnim = 0;
    } else if (g_playerEntity.action_state != 1) {
        return;
    }
    entity_apply_anim_vertex((Entity*)&g_playerEntity, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2);
    cVar1 = Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400);
    if (cVar1 != 0) {
        g_playerEntity.isBeingAttackedFlag = 0;
        g_playerEntity.animationId = 1;
        g_playerEntity.animFrameId = 0;
        g_playerEntity.action_behavior = 0;
        g_playerEntity.action_state = 0;
    }
}
void player_anim_set_attacked_flag(void) {    // 0x00469400 - dispatch via DAT_004c2ac8[action_behavior]
    extern void* DAT_004c2ac8[];
    void (*func)(void) = (void(*)(void))DAT_004c2ac8[g_playerEntity.action_behavior];
    if (func) func();
}
// 0x00468e10 — Limb physics with bouncing (7 states)
void player_anim_dispatch_4ba360(void) {
    JointStruct* pJVar5;
    char cVar6;
    short sVar7;
    short sVar8;

    pJVar5 = g_playerEntity.jointsStructs;
    switch (g_playerEntity.action_state) {
    case 0:
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf = 0;
        g_playerEntity.action_state = 1;
        g_playerEntity.attackAnim = 2;
        g_playerEntity.unk_8c = 3;
        pJVar5[0].velX = 0;
        pJVar5[0].velY = 0;
        pJVar5[0].velZ = 0;
        pJVar5[2].velX = 0;
        pJVar5[2].velY = 0;
        pJVar5[2].velZ = 0;
        pJVar5[2].rotation.x = 0;
        pJVar5[2].rotation.y = 0;
        pJVar5[2].rotation.z = 0;
        JointApplyColorTint(pJVar5, 0x30, 0x80820, &DAT_00606060);
        JointApplyColorTint(pJVar5 + 2, 0x30, 0x80820, &DAT_00606060);
        JointApplyColorTint(pJVar5 + 1, 0x30, 0x80820, &DAT_00606060);
        {
            int* deadData = (int*)g_deadMoveValue;
            g_playerPosScratch.x = *(int*)(deadData + 5);
            g_playerPosScratch.y = *(int*)(deadData + 6);
            g_playerPosScratch.z = *(int*)(deadData + 7);
            g_playerPosScratch.pad = *(int*)(deadData + 8);
        }
        Effect_CreateBillboard(0, 3, 0, &pJVar5[0].world, &g_playerPosScratch, 0);
        Effect_CreateBillboard(0, 3, 0, &pJVar5[2].world, &g_playerPosScratch, 0);
        Effect_CreateBillboard(0, 3, 0, &pJVar5[1].world, &g_playerPosScratch, 0);
        Play3DSnd(3, 3, 0, (int)&g_playerEntity.scaMatrixData.localMatrix.t);
        Play3DSnd(4, 0, 0, (int)&g_playerEntity.scaMatrixData.localMatrix.t);
        g_playerEntity.flags |= 4;
        g_playerEntity.attackDirection = 0xf;
        // fall through
    case 1:
        if (g_playerEntity.attackDirection != 0) {
            g_playerEntity.attackDirection--;
            Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400);
        }
        pJVar5 = g_playerEntity.jointsStructs;
        sVar8 = pJVar5[0].rotation.z;
        if (-0x400 < sVar8) {
            pJVar5[0].rotation.z = sVar8 - pJVar5[0].velX;
            pJVar5[0].velX = pJVar5[0].velX + 8;
            RotMatrix(&pJVar5[0].rotation, &pJVar5[0].transform);
        }
        sVar8 = pJVar5[2].rotation.y;
        if ((sVar8 < 0x100) && ((((unsigned char*)&pJVar5[2].velX)[1] & 0x80) == 0)) {
            pJVar5[2].rotation.y = sVar8 + 0x20;
            pJVar5[2].rotation.x = pJVar5[2].rotation.x - pJVar5[2].velX;
            pJVar5[2].velX = pJVar5[2].velX + 2;
            RotMatrix(&pJVar5[2].rotation, &pJVar5[2].transform);
            MulMatrixInPlace(&pJVar5[2].transform, &pJVar5[2].world);
            if (0xff < pJVar5[2].rotation.y) {
                pJVar5[2].velX = (short)0x8002;
                {
                    int* deadData = (int*)g_deadMoveValue;
                    g_playerPosScratch.x = deadData[5];
                    g_playerPosScratch.y = deadData[6];
                    g_playerPosScratch.z = deadData[7];
                    g_playerPosScratch.pad = deadData[8];
                }
                Effect_CreateBillboard(0, 3, 0, &pJVar5[2].world, &g_playerPosScratch, 0);
            }
        }
        break;
    case 2:
        pJVar5 = g_playerEntity.jointsStructs;
        pJVar5[0].velX = (short)0x8002;
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf = 0;
        g_playerEntity.attackAnim = 0;
        g_playerEntity.action_state = 3;
        g_playerEntity.unk_8c = 0xf;
        {
            int* deadData = (int*)g_deadMoveValue;
            g_playerPosScratch.x = deadData[5];
            g_playerPosScratch.y = deadData[6];
            g_playerPosScratch.z = deadData[7];
            g_playerPosScratch.pad = deadData[8];
        }
        Effect_CreateBillboard(0, 3, 0, &pJVar5[2].world, &g_playerPosScratch, 0);
        g_playerPosScratch.y = 500;
        Effect_CreateBillboard(0, 3, 0, &pJVar5[0].world, &g_playerPosScratch, 0);
        // fall through
    case 3:
        Joint_move(0, g_playerEntity.animHeader, g_playerEntity.animBase, 0x100);
        break;
    case 4:
        g_playerEntity.action_state = 5;
        g_playerEntity.unk_8c = 3;
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf = 0;
        // fall through
    case 5:
        cVar6 = Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400);
        if (cVar6 != 0) {
            g_playerEntity.unk_bf = 0;
            g_playerEntity.attackAnim = 0;
            g_playerEntity.action_state = 6;
            g_playerEntity.unk_8c = 3;
        }
        break;
    case 6:
        Joint_move(0, g_playerEntity.animHeader, g_playerEntity.animBase, 0x400);
        g_deathAnimationFlag = 1;
        break;
    }

    // Post-switch: physics for bouncing joints (runs every frame)
    pJVar5 = g_playerEntity.jointsStructs;

    // Joint 0 bounce check (when in-air flag set and position.y < 700)
    if (((((unsigned char*)&pJVar5[0].velX)[1] & 0x80) != 0) &&
        (g_playerEntity.scaMatrixData.localMatrix.t[1] < 700)) {
        g_playerEntity.health = -1;
        g_playerEntity.scaMatrixData.localMatrix.t[1] = g_playerEntity.scaMatrixData.localMatrix.t[1] + pJVar5[0].velY;
        sVar8 = pJVar5[0].velY;
        sVar7 = sVar8 + 2;
        pJVar5[0].velY = sVar7;
        if (0x20 < sVar7) {
            pJVar5[0].velY = sVar8 + 12;
        }
        if ((700 < g_playerEntity.scaMatrixData.localMatrix.t[1]) && ((pJVar5[0].velX & 7) != 0)) {
            g_playerEntity.scaMatrixData.localMatrix.t[1] = 0x28a;
            sVar8 = -pJVar5[0].velY;
            pJVar5[0].velY = sVar8;
            pJVar5[0].velY = (short)((int)((int)sVar8 + ((int)sVar8 >> 31 & 7U)) >> 3);
            pJVar5[0].velX--;
            g_playerEntity.action_state = 4;
            {
                int* deadData = (int*)g_deadMoveValue;
                g_playerPosScratch.x = deadData[5];
                g_playerPosScratch.z = deadData[7];
                g_playerPosScratch.pad = deadData[8];
                g_playerPosScratch.y = 500;
            }
            Effect_CreateBillboard(0, 3, 0, &pJVar5[0].world, &g_playerPosScratch, 0);
        }
        if ((((unsigned char)pJVar5[0].velX) & 7) != 2) {
            g_playerEntity.move_speed_current = 30;
            Add_speedXZ(0x800);
        }
        sVar8 = pJVar5[0].rotation.z;
        if (sVar8 < -0x3ff) {
            g_playerEntity.position.pad = 0;
            g_playerEntity.directionAngle = 0;
            g_playerEntity.speed.x = 0;
            pJVar5[0].rotation.x = 0;
            pJVar5[0].rotation.y = 0;
            pJVar5[0].rotation.z = -0x400;
        } else {
            pJVar5[0].rotation.z = sVar8 - pJVar5[0].velZ;
            pJVar5[0].velZ = pJVar5[0].velZ + 1;
            RotMatrix(&pJVar5[0].rotation, &pJVar5[0].transform);
        }
    }

    // Joint 2 bounce check (when in-air flag set and world.t[1] < -400)
    if (((((unsigned char*)&pJVar5[2].velX)[1] & 0x80) != 0) &&
        (pJVar5[2].world.t[1] < -400)) {
        pJVar5[2].world.t[1] = pJVar5[2].velY + pJVar5[2].world.t[1];
        sVar8 = pJVar5[2].velY;
        sVar7 = sVar8 + 2;
        pJVar5[2].velY = sVar7;
        if (0x20 < sVar7) {
            pJVar5[2].velY = sVar8 + 8;
            pJVar5[2].rotation.x = 0;
            pJVar5[2].rotation.y = 0;
        }
        if ((-400 < pJVar5[2].world.t[1]) && ((pJVar5[2].velX & 7) != 0)) {
            pJVar5[2].world.t[1] = -0x1c2;
            sVar8 = -pJVar5[2].velY;
            pJVar5[2].velY = sVar8;
            pJVar5[2].velY = (short)((int)((int)sVar8 + ((int)sVar8 >> 31 & 7U)) >> 3);
            pJVar5[2].velX--;
            {
                int* deadData = (int*)g_deadMoveValue;
                g_playerPosScratch.x = deadData[5];
                g_playerPosScratch.y = deadData[6];
                g_playerPosScratch.z = deadData[7];
                g_playerPosScratch.pad = deadData[8];
            }
            Effect_CreateBillboard(0, 3, 0, &pJVar5[2].world, &g_playerPosScratch, 0);
        }
        if (-0x400 < pJVar5[2].rotation.z) {
            pJVar5[2].rotation.x = 0;
            pJVar5[2].rotation.y = 0;
            pJVar5[2].rotation.z = pJVar5[2].rotation.z - pJVar5[2].velZ;
            pJVar5[2].velZ = pJVar5[2].velZ + 1;
            RotMatrix(&pJVar5[2].rotation, &pJVar5[2].world);
            return;
        }
        pJVar5[2].rotation.z = -0x400;
    }
}
// 0x0043b980 — 10-state crawl/death handler
void player_anim_dispatch_4c10b0(void) {
    char cVar1;
    short sVar2;
    switch (g_playerEntity.action_state) {
    case 0:
        g_playerEntity.action_state = 1;
        g_playerEntity.attackAnim = 2;
        g_playerEntity.unk_8c = 3;
        g_playerEntity.move_speed_current = 200;
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf = 0;
        // fall through
    case 1:
        if (g_playerEntity.animation_frame_id < 0x24) {
            entity_apply_anim_vertex((Entity*)&g_playerEntity, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2);
        } else {
            Add_speedXZ(0);
        }
        cVar1 = Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400);
        if (cVar1 != 0) {
            g_playerEntity.action_state = 2;
            return;
        }
        break;
    case 2:
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf = 0;
        g_playerEntity.isBeingAttackedFlag = 2;
        g_playerEntity.action_state = 3;
        g_playerEntity.attackAnim = 3;
        g_playerEntity.unk_8c = 3;
        // fall through
    case 3:
        if ((1 < g_playerEntity.isBeingAttackedFlag) && (3 < g_playerEntity.animation_frame_id)) {
            Play3DSnd(3, 2, 0, (int)&g_playerEntity.scaMatrixData.localMatrix.t);
            PlayEntitySnd(2);
            g_playerEntity.isBeingAttackedFlag = 1;
        }
        if ((g_playerEntity.health < 0) && (10 < g_playerEntity.animation_frame_id)) {
            PlayEntitySnd(2);
            g_playerEntity.action_state = 8;
            return;
        }
        sVar2 = GetPlayerInputMasked();
        g_playerEntity.attackDirection = g_playerEntity.attackDirection + (unsigned short)(sVar2 != 0) * (unsigned short)-3;
        if ((g_playerEntity.attackDirection < 0) &&
            ((cVar1 = Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400)), cVar1 != 0)) {
            g_playerEntity.action_state = 4;
            return;
        }
        cVar1 = Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400);
        if (cVar1 != 0) {
            g_playerEntity.action_state = 4;
            return;
        }
        break;
    case 4:
        g_playerEntity.unk_8c = 3;
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf = 0;
        g_playerEntity.action_state = 5;
        g_playerEntity.attackAnim = 5;
        // fall through
    case 5:
        sVar2 = GetPlayerInputMasked();
        g_playerEntity.attackDirection = g_playerEntity.attackDirection + (unsigned short)(sVar2 != 0) * (unsigned short)-3;
        if ((g_playerEntity.attackDirection < 0) &&
            ((cVar1 = Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400)), cVar1 != 0)) {
            g_playerEntity.action_state = 6;
            return;
        }
        cVar1 = Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400);
        if (cVar1 != 0) {
            g_playerEntity.action_state = 6;
            return;
        }
        break;
    case 6:
        g_playerEntity.action_state = 7;
        g_playerEntity.attackAnim = 4;
        g_playerEntity.unk_8c = 3;
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf = 0;
        // fall through
    case 7:
        cVar1 = Joint_move(1, g_playerEntity.jointMoveData0, g_playerEntity.jointMoveData1, 0x400);
        if (cVar1 != 0) {
            if (-1 < g_playerEntity.health) {
                g_playerEntity.action_behavior = 0;
                g_playerEntity.action_state = 0;
                g_playerEntity.isBeingAttackedFlag = 0;
                g_playerEntity.animationId = 1;
                g_playerEntity.animFrameId = 0;
                return;
            }
            g_playerEntity.action_state = 8;
            return;
        }
        break;
    case 8:
        Play3DSnd(3, 3, 0, (int)&g_playerEntity.scaMatrixData.localMatrix.t);
        g_playerEntity.action_state = 9;
        g_playerEntity.attackDirection = 0x5a;
        BillboardSetColor(&g_playerEntity.pushVelocity, 1, 2, 0x00ffff50);
        BillboardSetSize(&g_playerEntity.pushVelocity, 0, 0);
        // fall through
    case 9:
        BillboardAdjSize(&g_playerEntity.pushVelocity, 0x14, 0x14);
        sVar2 = g_playerEntity.attackDirection;
        g_playerEntity.attackDirection = g_playerEntity.attackDirection - 1;
        if (sVar2 == 0) {
            g_playerEntity.action_state = 10;
        }
        break;
    }
}
// 0x004401c0
void player_anim_poison_death(void) {
    if (g_playerEntity.action_state == 0) {
        g_playerEntity.action_state = 1;
        g_playerEntity.attackAnim = 0;
        g_playerEntity.move_speed_current = 0;
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf = 0;
        g_playerEntity.unk_8c = 3;
        g_playerEntity.isBeingAttackedFlag = 1;
        Play3DSnd(3, 0, 0, (int)&g_playerEntity.scaMatrixData.localMatrix.t);
    }
    Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400);
}
// 0x00440230
void player_anim_death_billboard(void) {
    if (g_playerEntity.action_state == 0) {
        g_playerEntity.action_state = 1;
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf = 0;
        g_playerEntity.attackAnim = 3;
        g_playerEntity.isBeingAttackedFlag = 1;
        g_playerEntity.unk_8c = 0;
    } else if (g_playerEntity.action_state == 1) {
        Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400);
        if (g_playerEntity.animation_frame_id == 14) {
            g_playerEntity.animation_frame_id = 13;
        }
    } else if (g_playerEntity.action_state == 2) {
        g_playerEntity.unk_03 |= 0x80;
        // TODO: Apply RotMatrix / ApplyLVAndMul0Matrix transforms using g_EnemiesList[0] joint matrices
    }
}
void player_anim_limb_physics(void) {         // 0x00424fb0 - dispatch via DAT_004ba360[action_behavior]
    extern void* DAT_004ba360[];
    void (*func)(void) = (void(*)(void))DAT_004ba360[g_playerEntity.action_behavior];
    if (func) func();
}
// 0x00424de0
void player_anim_enemy_interact(void) {
    if (g_playerEntity.action_state == 0) {
        g_playerEntity.action_state = 1;
        g_playerEntity.attackAnim = 2;
        g_playerEntity.isBeingAttackedFlag = 0x80;
        g_playerEntity.flags |= 6;
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf = 0;
        g_playerEntity.unk_8c = 0;
    } else if (g_playerEntity.action_state == 1) {
        if (g_playerEntity.animation_frame_id == 8) {
            Play3DSnd(3, 3, 0, (int)&g_playerEntity.scaMatrixData.localMatrix.t);
            // TODO: Billboard effects on joints at frame 8/9 and >95
        }
        entity_apply_anim_vertex((Entity*)&g_playerEntity, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2);
        char cVar2 = Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400);
        g_playerEntity.action_state += cVar2;
    } else if (g_playerEntity.action_state == 2) {
        g_playerEntity.health = -1;
    }
}
void player_anim_death_alt(void) {            // 0x004088f0 - dispatch via DAT_004b1a90[action_behavior]
    extern void* DAT_004b1a90[];
    void (*func)(void) = (void(*)(void))DAT_004b1a90[g_playerEntity.action_behavior];
    if (func) func();
}
void player_anim_dispatch_4b1a90(void) {      // 0x0045c460 - dispatch via DAT_004c10b0[action_state]
    extern void* DAT_004c10b0[];
    void (*func)(void) = (void(*)(void))DAT_004c10b0[g_playerEntity.action_state];
    if (func) func();
}

// ============================================================================
// entity_extract_anim_vertex - Extract animation frame vertex position into gSVector
// 0x0048bb60
// Reads animation data from EMD scratch pointers, advances animation timing
// on the ENTITY global, and extracts a vertex position (X,Y,Z) into
// g_svecScratch (gSVector).
// ============================================================================
void entity_extract_anim_vertex(Entity* entity, unsigned int emdScratch1, unsigned int emdScratch2, char reverse)
{
    // emdScratch1 points to animation header: [+2] = vertex stride, [+6] = vertex count
    // emdScratch2 points to animation frame table (indexed by animationId)
    short headerStride = *(short*)(emdScratch1 + 2);
    short headerCount  = *(short*)(emdScratch1 + 6);

    unsigned char* frameIdPtr = &ENTITY->animation_frame_id;
    unsigned short* animSlot = (unsigned short*)(emdScratch2 + (unsigned int)entity->animationId * 4);

    // Save current frame id (will be restored at the end)
    g_animFrameIdSave = (unsigned int)*frameIdPtr;

    // Advance animation timing if timing_control > 1
    if (1 < (unsigned char)ENTITY->timing_control) {
        *frameIdPtr = *frameIdPtr - 1;
        if (ENTITY->animation_frame_id == 0xFF) {
            ENTITY->animation_frame_id = (char)*animSlot - 1;
        }
    }

    // Calculate base of frame data
    unsigned int frameDataBase = emdScratch2 + (animSlot[1] & 0xFFFFFFFC);

    // Get frame entry pointer based on direction
    unsigned short* frameEntry;
    if (reverse == 0) {
        frameEntry = (unsigned short*)(frameDataBase + (unsigned int)entity->animation_frame_id * 4);
    } else {
        frameEntry = (unsigned short*)(frameDataBase - 4 + ((unsigned int)*animSlot - (unsigned int)entity->animation_frame_id) * 4);
    }

    // Calculate vertex address in animation data
    // align4(headerStride) * 4 = round headerStride down to multiple of 4
    short aligned = (short)(((int)headerStride + ((int)headerStride >> 31 & 3)) >> 2);
    unsigned int vertexAddr = emdScratch1 + (int)aligned * 4 +
                              (unsigned int)*frameEntry * ((int)headerCount / 2 & 0xFFFF) * 2;

    // Extract vertex position (offsets +6, +8, +10 from vertex data)
    g_svecScratch.x = *(short*)(vertexAddr + 6);
    g_svecScratch.y = *(short*)(vertexAddr + 8);
    g_svecScratch.z = *(short*)(vertexAddr + 10);

    // Restore saved frame id
    ENTITY->animation_frame_id = (unsigned char)g_animFrameIdSave;
}

// ============================================================================
// entity_apply_anim_vertex - Update entity transform from animation vertex offset
// 0x00489fa0
// Extracts the current animation vertex, rotates it by the entity's Y angle,
// and sets the entity's transform translation (X, Z) from the result plus
// the entity's base position offsets.
// ============================================================================
void entity_apply_anim_vertex(Entity* entity, unsigned int emdScratch1, unsigned int emdScratch2)
{
    // 0x00489fbe - Extract animation vertex position into g_svecScratch
    entity_extract_anim_vertex(entity, emdScratch1, emdScratch2, 0);

    // 0x00489fc6 - Copy identity matrix to scratch matrix
    g_matrixScratch = g_identityMatrixData;

    // 0x00489fd7 - Rotate scratch matrix by entity Y angle
    RotMatrixY((int)(short)entity->angle, &g_matrixScratch);

    // 0x00489fee - Apply rotation to vertex position
    ApplyMatrixSV(&g_matrixScratch, &g_svecScratch, &g_svecScratch);

    // 0x00489fff - Set entity transform translation from base offset + rotated vertex
    entity->scaMatrixData.localMatrix.t[0] = (unsigned int)entity->unk_c6 + (int)g_svecScratch.x;
    entity->scaMatrixData.localMatrix.t[2] = (unsigned int)entity->unk_c8 + (int)g_svecScratch.z;
}

// ============================================================================
// JointSetColorTint (0x00485ac0)
// Sets vertex color tint on model data for a joint. Iterates through model
// vertex entries and applies an RGB color value scaled by a constant factor.
// ============================================================================
void JointSetColorTint(int modelObjPtr, unsigned int packedColor)
{
    unsigned char r = (unsigned char)(packedColor & 0xFF);
    unsigned char g = (unsigned char)((packedColor >> 8) & 0xFF);
    unsigned char b = (unsigned char)((packedColor >> 16) & 0xFF);

    if (*(int*)(modelObjPtr + 0x10) == 0) {
        int vertexDataPtr = *(int*)(modelObjPtr + 0x20);
        int vertexCount = *(int*)(vertexDataPtr + 0x4c0);
        if ((vertexCount & 0x7FFFFFFF) == 0) return;

        int entry = vertexDataPtr + 0x4d0;
        unsigned int idx = 0;
        do {
            *(unsigned int*)(entry + 0x80) |= 2;
            *(float*)(entry + 0x5c) = (float)r * (1.0f / 255.0f);
            *(float*)(entry + 0x60) = (float)g * (1.0f / 255.0f);
            *(float*)(entry + 0x64) = (float)b * (1.0f / 255.0f);
            *(float*)(entry + 0x6c) = *(float*)(entry + 0x5c);
            *(int*)(entry + 0x70) = *(int*)(entry + 0x60);
            *(int*)(entry + 0x74) = *(int*)(entry + 0x64);
            *(int*)(entry + 0x78) = *(int*)(entry + 0x68);
            entry += 0x84;
            idx++;
        } while (idx < (unsigned int)(vertexCount * 2));
    }
}

// ============================================================================
// JointApplyColorTint (0x0048a190)
// Applies color tinting to a joint's model data and optionally to its paired
// weapon joint. Sets the 0x80 flag on processed joints and updates
// g_playerDisplacement from animation slot data.
// ============================================================================
void JointApplyColorTint(JointStruct* joint, int param2, int param3, void* data)
{
    joint->flags |= 0x80;
    g_playerDisplacement = *(int*)(joint->anim_slot_ptr + 0x14) * 2;
    JointSetColorTint((int)joint->anim_object, (unsigned int)param2);

    if (((unsigned char)g_main_state_flags & 1) != 0) {
        int offset = ENTITY->weaponJointsPtr - (int)ENTITY->jointsStructs;
        JointStruct* weaponJoint = (JointStruct*)((unsigned char*)joint + offset);
        g_tempVar = weaponJoint;
        weaponJoint->flags |= 0x80;
        g_playerDisplacement = *(int*)(weaponJoint->anim_slot_ptr + 0x14) * 2;
        JointSetColorTint((int)weaponJoint->anim_object, (unsigned int)param2);
    }
}

// ============================================================================
// Animation/effect helper function stubs (called by player_anim_* functions)
// ============================================================================
// ============================================================================
// Joint_move (0x0048b700)
// Core joint animation playback. Advances animation timing, reads joint
// rotation data from EMD animation frames, and applies either direct or
// blended rotations to each joint. Returns 1 when animation loops, 0 otherwise.
// ============================================================================
unsigned int Joint_move(char reverse, unsigned int animHeader, unsigned int animBase, short blendStep)
{
    // Wait if timing hasn't expired
    unsigned char* timingPtr = &ENTITY->timing_control;
    if (1 < *timingPtr) {
        *timingPtr = *timingPtr - 1;
        return 0;
    }

    // Calculate vertex count per frame from animHeader
    g_playerDisplacement = (int)(*(short*)(animHeader + 6) / 2);

    // Get animation slot for current animationId
    unsigned short* animSlot = (unsigned short*)(animBase + (unsigned int)ENTITY->animationId * 4);

    // Get frame entry pointer
    unsigned short* frameEntry = (unsigned short*)((animSlot[1] & 0xFFFFFFFC) +
        (unsigned int)ENTITY->animation_frame_id * 4 + animBase);
    if (reverse != 0) {
        frameEntry = frameEntry + ((unsigned int)*animSlot + (unsigned int)ENTITY->animation_frame_id * (unsigned int)-2) * 2 + (unsigned int)-2;
    }

    // Calculate base of animation vertex data for this frame
    short headerStride = *(short*)(animHeader + 2);
    short aligned = (short)(((int)headerStride + ((int)headerStride >> 31 & 3)) >> 2);
    short* animData = (short*)(animHeader + (int)aligned * 4 +
        (unsigned int)*frameEntry * g_playerDisplacement * 2);

    JointStruct* joint = ENTITY->jointsStructs;
    unsigned char blendCounter = ENTITY->blend_counter;
    char jointCount = ENTITY->jointCount;

    // Set root joint translation from first 3 shorts of animation data
    joint->transform.t[0] = (int)animData[0];

    if (blendCounter == 0) {
        // Direct mode: apply rotations without blending
        joint->transform.t[1] = (int)animData[1];
        joint->transform.t[2] = (int)animData[2];
        short* rotData = animData + 6;

        while (jointCount != 0) {
            jointCount--;
            if ((joint->flags & 0x10) == 0) {
                joint->rotation.x = rotData[0];
                joint->rotation.y = rotData[1];
                joint->rotation.z = rotData[2];
                RotMatrix(&joint->rotation, &joint->transform);
            }
            rotData += 3;
            joint++;
        }
    } else {
        // Blending mode: interpolate rotations
        int step = (int)blendStep;
        unsigned int blend = (unsigned int)blendCounter;
        int invBlend = (int)(0x1000 / (long long)step) - blend;
        short* rotData = animData + 6;

        // Interpolate root Y translation
        int ty = (int)animData[1] * invBlend * step;
        int cy = joint->transform.t[1] * blend * step;
        joint->transform.t[1] = ((ty + (ty >> 31 & 0xFFF)) >> 12) +
                                ((cy + (cy >> 31 & 0xFFF)) >> 12);
        joint->transform.t[2] = (int)animData[2];

        while (jointCount != 0) {
            jointCount--;
            if ((joint->flags & 0x10) == 0) {
                g_svecScratch.x = rotData[0];
                g_svecScratch.y = rotData[1];
                g_svecScratch.z = rotData[2];

                // Angle wrapping fix when invBlend == 1 (last blend step)
                if (invBlend == 1) {
                    for (int i = 2; i >= 0; i--) {
                        short* curRot = &joint->rotation.x + i;
                        short curVal = *curRot;
                        unsigned short diff = ((&g_svecScratch.x)[i] - curVal) + 0x800;
                        if (0x1000 < diff) {
                            *curRot = (unsigned short)(((diff & 0x8000) == 0) * 0x2000) + curVal + (short)-0x1000;
                        }
                    }
                }

                fp_lerp(&joint->rotation, &g_svecScratch, blend * step, invBlend * step, &joint->rotation);
                RotMatrix(&joint->rotation, &joint->transform);
            }
            rotData += 3;
            joint++;
        }
        ENTITY->blend_counter = ENTITY->blend_counter - 1;
    }

    // Update timing and advance frame
    ENTITY->timing_control = (char)frameEntry[1];
    ENTITY->animation_frame_id = ENTITY->animation_frame_id + 1;

    // Check for animation loop
    if ((int)(*animSlot - 1) < (int)(unsigned int)ENTITY->animation_frame_id) {
        ENTITY->animation_frame_id = 0;
        return 1;
    }
    return 0;
}

// ============================================================================
// Effect_CreateBillboard (0x0047be30)
// Allocates one or more effect slots from the 64-slot pool and initializes
// a billboard sprite effect. When the animation has multiple frames, allocates
// one slot per frame (each gets a progressively earlier frame).
// Returns: slot index (0-63) on success, 0xFF on failure.
// ============================================================================
unsigned char Effect_CreateBillboard(
    unsigned char type, unsigned char depthGroup, short yaw,
    void* spriteInfo, void* pos, char lightFactor)
{
    if (g_freeEffectSlots == 0) return type;

    bool needAnimLookup = true;
    unsigned int* frameArrayPtr = NULL;
    unsigned int frameCount = 0;

    while (true) {
        // Search for a free slot (from 63 down to 0)
        bool noFreeSlot = true;
        unsigned char slotIdx = 64;
        do {
            slotIdx--;
            if (g_effectPool[slotIdx].animId == 0) {
                noFreeSlot = false;
                g_freeEffectSlots--;
            }
        } while (slotIdx != 0 && noFreeSlot);

        if (noFreeSlot) break;

        Effect* eff = &g_effectPool[slotIdx];
        VECTOR* vPos = (VECTOR*)pos;

        // Clear velocity, position, transform, and sprite offset fields
        eff->rotSpeedZ = 0;
        eff->rotSpeedY = 0;
        eff->rotSpeedX = 0;
        eff->posZ = 0;
        eff->posY = 0;
        eff->posX = 0;
        for (int i = 0; i < 9; i++) eff->transform[i] = 0;
        eff->spriteOffsetZ = 0;
        eff->spriteOffsetY = 0;
        eff->spriteOffsetX = 0;
        eff->depthScaled = 0;
        eff->projDepth = 0;

        // Set spawn parameters
        eff->effectType = type;
        eff->depthGroup = depthGroup;
        eff->localOffsetX = (short)vPos->x;
        eff->localOffsetY = (short)vPos->y;
        eff->localOffsetZ = (short)vPos->z;
        eff->spriteInfo = (int)spriteInfo;

        // Copy full-precision spawn position (VECTOR with pad)
        eff->spawnPosX = vPos->x;
        eff->spawnPosY = vPos->y;
        eff->spawnPosZ = vPos->z;
        eff->spawnPosW = vPos->pad;

        // Set up texture pointers from sprite info table
        unsigned int typeIdx = (unsigned int)type;
        DWORD* spriteInfoBase = (DWORD*)g_effectSpriteInfo[typeIdx];

        // g_effectSpriteInfo is populated per-room by load_effect_sprite_data from
        // the RDT's effect-animation index table, so only the effect types the
        // CURRENT room declares are valid. An unregistered type leaves a 0 here and
        // the `+2` read below faults at address 0x00000002 - which is what
        // room_transition_load hit, running the destination room's init SCD before
        // that room's RDT effect table had been loaded.
        //
        // The original does not guard this either; it would fault the same way. The
        // guard is port-only so a data-ordering bug reports instead of crashing.
        if (spriteInfoBase == NULL || (DWORD)spriteInfoBase == 0xFFFFFFFF) {
            dbg_printf("[effect] g_effectSpriteInfo[%u] not loaded for this room"
                       " - billboard skipped (depthGroup=%u)\n",
                       typeIdx, (unsigned int)depthGroup);
            return 0;
        }

        eff->clutInfo = (int)spriteInfoBase;
        eff->vramInfo = (int)(spriteInfoBase + 2);      // +8 bytes
        eff->vramInfoBackup = (int)(spriteInfoBase + 2);

        // UV data starts after sprite entries: base + 8 + count * 4
        unsigned short uvCount = *(unsigned short*)((char*)spriteInfoBase + 2);
        int uvAddr = (int)((char*)spriteInfoBase + 8 + uvCount * 4);
        eff->uvData = uvAddr;
        eff->uvDataBackup = uvAddr;

        // Read initial frame delay and index from VRAM info
        unsigned char* vramPtr = (unsigned char*)eff->vramInfo;
        eff->frameDelay = vramPtr[1];
        eff->frameIndex = vramPtr[0];

        // Look up animation data (only on first iteration)
        if (needAnimLookup) {
            needAnimLookup = false;
            unsigned char* animBase = (unsigned char*)g_effectAnimData[typeIdx];
            unsigned char depthIdx = depthGroup & 0x07;
            unsigned int tableIndex = (unsigned int)animBase[depthIdx];
            frameArrayPtr = (unsigned int*)(animBase + tableIndex * 4);
            frameCount = *frameArrayPtr;
        }

        // Skip to the correct animation frame (skip frameCount-1 frames)
        unsigned int* pFrame = frameArrayPtr + 1;
        if (frameCount > 1) {
            unsigned int remaining = frameCount - 1;
            do {
                unsigned int entryCount = *pFrame;
                pFrame = pFrame + entryCount * 6 + 1;
                remaining--;
            } while (remaining != 0);
        }

        // Set animation frame data pointers (past the 4-byte header)
        eff->animDataFrame = (int)(pFrame + 1);
        eff->animDataBase = (int)(pFrame + 1);

        // Copy 24-byte animation header into the slot (6 DWORDs)
        unsigned int* src = (unsigned int*)(pFrame + 1);
        unsigned int* dst = (unsigned int*)&eff->animId;
        for (int i = 0; i < 6; i++) dst[i] = src[i];

        // Set light factor if provided
        if (lightFactor != 0) {
            eff->lightFactor = lightFactor;
        }

        // Decrement frame count, apply yaw, mark active
        frameCount--;
        eff->yaw += yaw;
        eff->type = 1;

        // Use identity matrix if no sprite info was provided
        if (spriteInfo == NULL) {
            eff->spriteInfo = (int)&g_identityMatrixData;
        }

        // If no more frames remain, return this slot
        if (frameCount == 0) return slotIdx;
    }

    return 0xFF;
}

// ============================================================================
// BillboardSetColor (0x00456710)
// Sets tpage and vertex color on a billboard quad structure.
// ============================================================================
void BillboardSetColor(void* quad, int unused1, int unused2, unsigned int color)
{
    unsigned short tpage = GteTpageBuild(1, 2, 384, 256);
    *(unsigned short*)((char*)quad + 0x1E) = tpage;
    *(unsigned short*)((char*)quad + 0x46) = tpage;
    unsigned int c0 = *(unsigned int*)((char*)quad + 0x0C);
    *(unsigned int*)((char*)quad + 0x0C) = (c0 & 0xFF000000) | (color & 0x00FFFFFF);
    unsigned int c1 = *(unsigned int*)((char*)quad + 0x34);
    *(unsigned int*)((char*)quad + 0x34) = (c1 & 0xFF000000) | (color & 0x00FFFFFF);
}

// ============================================================================
// BillboardAdjSize (0x00456760)
// Adjusts billboard quad vertex positions by the given half-extents.
// ============================================================================
void BillboardAdjSize(void* quad, short halfW, short halfH)
{
    *(short*)((char*)quad + 0x58) -= halfW;
    *(short*)((char*)quad + 0x5C) += halfH;
    *(short*)((char*)quad + 0x60) += halfW;
    *(short*)((char*)quad + 0x64) += halfH;
    *(short*)((char*)quad + 0x68) -= halfW;
    *(short*)((char*)quad + 0x6C) -= halfH;
    *(short*)((char*)quad + 0x70) += halfW;
    *(short*)((char*)quad + 0x74) -= halfH;
}

// ============================================================================
// BillboardSetSize (0x00456790)
// Sets billboard quad vertex positions from half-extents (4 corners).
// ============================================================================
void BillboardSetSize(void* quad, short halfW, short halfH)
{
    *(short*)((char*)quad + 0x58) = -halfW;
    *(short*)((char*)quad + 0x5C) =  halfH;
    *(short*)((char*)quad + 0x60) =  halfW;
    *(short*)((char*)quad + 0x64) =  halfH;
    *(short*)((char*)quad + 0x68) = -halfW;
    *(short*)((char*)quad + 0x6C) = -halfH;
    *(short*)((char*)quad + 0x70) =  halfW;
    *(short*)((char*)quad + 0x74) = -halfH;
}

// ============================================================================
// BillboardSetRect (0x004567d0)
// The asymmetric sibling of BillboardSetSize: instead of one half-extent per
// axis it takes all four edges, so the quad can be off-centre. Used by the door
// sequence, where the shadow has to reach further forward than back.
//
// `left` and `back` are stored negated, which is what makes the argument order
// read oddly at the call sites: the original pushes (right, left, front, back)
// and writes -left / -back. The four corners land at quad+0x58/0x60/0x68/0x70
// with x at +0 and z at +4 - the same layout BillboardAdjSize patches and
// FUN_004565f0 builds.
// ============================================================================
void BillboardSetRect(void* quad, short right, short left, short front, short back)
{
    *(short*)((char*)quad + 0x58) = -left;
    *(short*)((char*)quad + 0x5C) =  front;
    *(short*)((char*)quad + 0x60) =  right;
    *(short*)((char*)quad + 0x64) =  front;
    *(short*)((char*)quad + 0x68) = -left;
    *(short*)((char*)quad + 0x6C) = -back;
    *(short*)((char*)quad + 0x70) =  right;
    *(short*)((char*)quad + 0x74) = -back;
}

// ============================================================================
// GetPlayerInputMasked (0x0048a030)
// Returns player pad held state masked to d-pad + face buttons (0xF0F0).
// ============================================================================
short GetPlayerInputMasked(void)
{
    return (short)(g_PlayerPadHeld & 0xF0F0);
}

// ============================================================================
// EntityUpdateWeaponJoint (0x00459de0)
// Calculates weapon joint world position by composing joint transforms and
// adjusting the entity's translation to match.
// ============================================================================
void EntityUpdateWeaponJoint(int weaponIdx)
{
    JointStruct* joints = ENTITY->jointsStructs;

    // Build rotation from entity's facing angle
    RotMatrix((SVECTOR*)&ENTITY->position.pad, &ENTITY->scaMatrixData.localMatrix);

    // Compose: scratch = entity_transform * joint[0].transform
    ApplyLVAndMul0Matrix(&ENTITY->scaMatrixData.localMatrix, &joints[0].transform, &g_matrixScratch);

    // Compose: scratch = scratch * joint[2].transform
    ApplyLVAndMulMatrix(&g_matrixScratch, &joints[2].transform);

    // Compose through 3 weapon chain joints
    unsigned int idx = (unsigned int)weaponIdx;
    for (int i = 3; i > 0; i--) {
        ApplyLVAndMulMatrix(&g_matrixScratch, &joints[idx * 3 + (6 - i)].transform);
    }

    // Extract translation offset from last weapon joint's world matrix
    g_matrixScratch.t[1] = 0;
    g_matrixScratch.t[0] -= joints[idx * 3 + 5].world.t[0];
    g_matrixScratch.t[2] -= joints[idx * 3 + 5].world.t[2];

    // Adjust entity translation
    ENTITY->scaMatrixData.localMatrix.t[0] -= g_matrixScratch.t[0];
    ENTITY->scaMatrixData.localMatrix.t[2] -= g_matrixScratch.t[2];
}

// ============================================================================
// MulMatrixInPlace (0x0040a170)
// In-place matrix multiply: m1 = m0 * m1
// ============================================================================
MATRIX* MulMatrixInPlace(MATRIX* m0, MATRIX* m1)
{
    MulMatrix0(m0, m1, m1);
    return m1;
}

// ============================================================================
// Add_speedXZ (0x0048a590)
// Adds movement speed to entity transform in the entity's facing direction
// plus an angular offset. Moves the entity by its move_speed_current speed value.
// ============================================================================
void Add_speedXZ(int angleOffset)
{
    // Set speed vector from entity's base speed value
    g_svecScratch.x = ENTITY->move_speed_current;
    g_svecScratch.y = 0;
    g_svecScratch.z = 0;

    // Copy identity matrix to scratch
    g_matrixScratch = g_identityMatrixData;

    // Rotate by entity facing + offset
    RotMatrixY((int)(short)ENTITY->angle + (int)(short)angleOffset, &g_matrixScratch);

    // Transform speed vector by rotation
    ApplyMatrixSV(&g_matrixScratch, &g_svecScratch, &ENTITY->speed);

    // Apply speed to entity transform translation
    ENTITY->scaMatrixData.localMatrix.t[0] += (int)ENTITY->speed.x;
    ENTITY->scaMatrixData.localMatrix.t[1] += (int)ENTITY->speed.y;
    ENTITY->scaMatrixData.localMatrix.t[2] += (int)ENTITY->speed.z;
}

// ============================================================================
// ApplyLVAndMul0Matrix (0x0040a0b0)
// Full matrix composition: m_out = m0 * m1 (rotation + translation)
// ============================================================================
void ApplyLVAndMul0Matrix(void* m0, void* m1, void* mOut)
{
    CompMatrix((MATRIX*)m0, (MATRIX*)m1, (MATRIX*)mOut);
}

// RotMatrix is implemented in GteMatrix.cpp. Its address is 0x00409df0 -
// 0x004406a0 is GteRotationMatrixCalc, the helper it calls.

// ============================================================================
// Player state machine — update_player_anim and the 0x004d4550 dispatch table
//
// update_player_anim is the per-frame entry point for the player, called from
// game_loop (0x00480ebd). It dispatches on g_playerEntity.animationId through a
// 16-entry table of function pointers statically initialized in .data at
// 0x004d4550. That table had no counterpart in the port at all, and
// update_player_anim itself was an empty stub — so the player never animated,
// never moved and never posed its skeleton, which is why no character model
// appeared and cutscene scripts waiting on player animation stalled forever.
//
// The table extent is 16 entries: 0x004d4590 onward is a different jumptable
// (the action_behavior switch inside state 1), whose targets match that switch's
// cases exactly. Entries 9 and 15 are NULL in the original.
// ============================================================================

extern void SetEntityScaHitData(Entity* ent);                                     // 0x0041b2c0
extern unsigned int HandleEnemyPlayerCollisions(void);                            // 0x00489e10
extern unsigned char check_room_collision(VECTOR* pos, short radius);             // 0x0047d310
extern void FUN_004565f0(SVECTOR* a, SVECTOR* b, int c, int d);                   // 0x004565f0
extern unsigned char FUN_0048bd00(void* light, unsigned char param2, int param3); // 0x0048bd00
extern void FUN_0048bda0(void);                                                   // 0x0048bda0
extern void ClearAnimTiming(void);                                                // 0x00429d30
extern void MovePlayerXZ(int angle, SVECTOR* offset, SVECTOR* out);
extern int is_entity_in_switch_zone(VECTOR* pos, void* zoneData);                 // 0x00462d90

// ----------------------------------------------------------------------------
// EntityUpdateLookAtAngles (0x00459eb0) — DEFERRED, 1058 bytes.
// Slews the tracking joint's yaw/pitch toward the look-at target, clamped to a
// +/-0x2C8 yaw and +/-0x138 pitch cone. Cosmetic head tracking, and gated on
// ENTITY->lookAtFlags & 0x10, which is zero for a freshly initialized player —
// so a no-op matches the original until an SCD look-at opcode enables it.
// ----------------------------------------------------------------------------
void EntityUpdateLookAtAngles(void)
{
}

// ----------------------------------------------------------------------------
// FUN_00456a10 (0x00456a10) — DEFERRED, 785 bytes.
// Builds the player ground shadow / fade sprite. Blocked on RotAverage4
// (0x0040ab00), a PSX GTE routine with no counterpart in the port yet.
// Cosmetic only: nothing reads back the state it produces.
// ----------------------------------------------------------------------------
static void player_update_shadow_sprite(int posPtr, int sprPtr, int height, int angle)
{
    (void)posPtr; (void)sprPtr; (void)height; (void)angle;
}

// ----------------------------------------------------------------------------
// FUN_00429d50 (0x00429d50) — DEFERRED body, faithful gate.
// Applies falling physics to joint 15 and draws it through the function table at
// 0x004ba950 (indexed by that joint's rotDeltaX). The whole body is gated on
// jointsStructs[15].velZ != 0, which is zero for a normally initialized player,
// so this is behaviourally identical to the original until a limb detaches.
// The 0x004ba950 table has not been extracted yet.
// ----------------------------------------------------------------------------
static void player_update_detached_joint(void)
{
    JointStruct* joints = g_playerEntity.jointsStructs;
    if (joints == NULL) return;
    if (((g_playerEntity.unk_03 & 0x7f) != 0) && (joints[0xf].velZ != 0)) {
        static bool reported = false;
        if (!reported) {
            reported = true;
            dbg_printf("[player] detached-joint physics hit (FUN_00429d50 body missing)\n");
        }
    }
}

// ----------------------------------------------------------------------------
// Player state 0 (FUN_00494eb0) — spawn / re-init.
// Runs for exactly one frame: poses the skeleton from the animation data via
// Joint_move, syncs position from the transform matrix, clears combat state,
// then hands over to state 1.
// ----------------------------------------------------------------------------
static void player_state_init(void) // 0x00494eb0
{
    g_playerEntity.animationId     = 1;
    g_playerEntity.animFrameId     = 0;
    g_playerEntity.action_behavior = 0;
    g_playerEntity.action_state    = 0;

    // 0x00494ec6: the original stores the constant 0x00808080 here. Ghidra
    // renders it as &DAT_00808080 because the value looks like an address; it is
    // a packed grey triple, not a pointer.
    g_animFrameIdSave = 0x00808080;

    g_PlayerDpadHeld = 0;

    g_svecScratch.x = 0;
    g_svecScratch.y = 0;
    g_svecScratch.z = 0;
    FUN_004565f0(&g_svecScratch, &g_playerEntity.pushVelocity, 500, 700);

    // 0x00494f0b: written through the global ENTITY pointer in the original,
    // which update_player_anim has already aimed at g_playerEntity.
    g_playerEntity.unk_8c             = 0;
    g_playerEntity.attackAnim         = 0;
    g_playerEntity.animation_frame_id = 0;
    g_playerEntity.unk_bf             = 0;

    Joint_move(0, g_playerEntity.animHeader, g_playerEntity.animBase, 0x400);

    // 0x00494f60: reset the hit box; the width comes from the SCA info block
    *(unsigned short*)(g_playerEntity.pSca_hit_data + 0) = 0;
    *(unsigned short*)(g_playerEntity.pSca_hit_data + 4) = 0;
    *(unsigned short*)(g_playerEntity.pSca_hit_data + 2) =
        *(unsigned short*)(g_playerEntity.Sca_info + 4);

    g_playerEntity.position.y = (short)g_playerEntity.scaMatrixData.localMatrix.t[1];
    g_playerEntity.position.x = (short)g_playerEntity.scaMatrixData.localMatrix.t[0];
    g_playerEntity.position.z = (short)g_playerEntity.scaMatrixData.localMatrix.t[2];

    g_playerEntity.unk_03 = 0;
    g_playerEntity.healthStatusFlags |= 0x10;
    g_playerEntity.attackTimer = 0;
    g_playerEntity.isBeingAttackedFlag = 0;

    ClearAnimTiming();
}

// ----------------------------------------------------------------------------
// Player states 5, 6 and 7 (0x00495290, 0x004952d0, 0x00495310).
// Each forwards to g_playerAnimFunctions[animFrameId + window], where the three
// windows are 0, 0x13 and 0x26 across that 52-entry table.
//
// PORT NOTE: the original g_playerAnimFunctions is fully populated —
// set_player_animations_functions overwrites only 14 of its entries and the rest
// come from static .data that has not been extracted yet. Dispatching a NULL
// entry would fault with no clue which index was missing, so these log the index
// and return instead. Drop the guard once the table is complete.
// ----------------------------------------------------------------------------
static void player_dispatch_anim_fn(unsigned int index)
{
    if (index >= 52 || g_playerAnimFunctions[index] == NULL) {
        static int lastReported = -1;
        if ((int)index != lastReported) {
            lastReported = (int)index;
            dbg_printf("[player] g_playerAnimFunctions[%u] is NULL (animFrameId=%u)\n",
                   index, (unsigned int)g_playerEntity.animFrameId);
        }
        return;
    }
    ((void(*)(void))g_playerAnimFunctions[index])();
}

static void player_state_anim_window0(void) // 0x00495290
{
    if (g_playerEntity.action_state == 0) {
        if (((g_main_state_flags & 0x40) != 0) ||
            ((g_playerEntity.healthStatusFlags & 0x80) != 0)) {
            g_message_flags = (unsigned short)(g_message_flags | 0x40);
        }
        g_playerEntity.healthStatusFlags &= 0x7f;
    }
    player_dispatch_anim_fn(g_playerEntity.animFrameId);
}

static void player_state_anim_window1(void) // 0x004952d0
{
    if (g_playerEntity.action_state == 0) {
        if (((g_main_state_flags & 0x40) != 0) ||
            ((g_playerEntity.healthStatusFlags & 0x80) != 0)) {
            g_message_flags = (unsigned short)(g_message_flags | 0x40);
        }
        g_playerEntity.healthStatusFlags &= 0x7f;
    }
    player_dispatch_anim_fn((unsigned int)g_playerEntity.animFrameId + 0x13);
}

static void player_state_anim_window2(void) // 0x00495310
{
    player_dispatch_anim_fn((unsigned int)g_playerEntity.animFrameId + 0x26);
}

// ----------------------------------------------------------------------------
// Player state 4 (0x00495280) — suppress all message/input flags.
// ----------------------------------------------------------------------------
static void player_state_block_input(void) // 0x00495280
{
    g_message_flags = 0;
}

// ----------------------------------------------------------------------------
// Unimplemented states, reached by animationId values not yet transcribed.
// Logging the index rather than leaving a NULL entry that faults is what tells
// us which state a stalled cutscene is asking for.
// ----------------------------------------------------------------------------
static void player_state_report_missing(const char* addr)
{
    static const char* lastAddr = NULL;
    if (addr != lastAddr) {
        lastAddr = addr;
        dbg_printf("[player] unimplemented state animationId=%u -> %s\n",
               (unsigned int)g_playerEntity.animationId, addr);
    }
}

static void player_state_02(void)         { player_state_report_missing("0x00495250"); }
static void player_state_03(void)         { player_state_report_missing("0x00495270"); }
static void player_state_null(void)       { player_state_report_missing("NULL in original"); }

// ============================================================================
// Player state 1 — normal player control (0x00495180)
//
// This is the state the intro cutscene hands back to, and while it was a stub the
// player froze: no input mapping, no idle animation, and no route to the door
// transition. See docs/SCD_WORK_PLAN.md.
//
// Ghidra's decompilation of this function is NOT usable - it reports
// "Sanity check requires truncation of jumptable" and "Could not find normalized
// switch variable", and it folds the animFrameId=0 handler into the outer switch,
// producing a bogus "case 0 falls through to case 2". The disassembly is
// unambiguous:
//
//   0049523f: MOV AL,[0x00be6369]                 ; animFrameId (entity+0x85)
//   00495244: JMP dword ptr [EAX*0x4 + 0x4d4578]  ; jump table, NO bounds check
//
// so animFrameId selects a handler directly out of a five-entry table that begins
// at 0x004d4578 - immediately after g_playerStateFunctions ends at 0x004d4577.
// ============================================================================

// Forward declarations for the animFrameId handlers.
static void player_ctrl_frame0(void);   // 0x00495320
static void player_ctrl_frame1(void);   // 0x00495520
static void player_ctrl_frame2(void);   // 0x00495330
static void player_ctrl_frame3(void);   // 0x00495530
static void player_ctrl_frame4(void);   // 0x004955e0

// 0x004d4578 — indexed by animFrameId (entity+0x85). The original applies no mask
// and no bound; animFrameId is only ever 0-4 on this path, and the port's bound
// check below is additive so an out-of-range value reports instead of jumping into
// whatever follows the table.
static void* const g_playerCtrlFrameFunctions[5] = {
    (void*)player_ctrl_frame0,   // 0x00495320
    (void*)player_ctrl_frame1,   // 0x00495520
    (void*)player_ctrl_frame2,   // 0x00495330
    (void*)player_ctrl_frame3,   // 0x00495530
    (void*)player_ctrl_frame4,   // 0x004955e0
};

static void player_state_01_control(void)
{
    // 0x00495180: dead - fall into the death animation and stop.
    if ((short)g_playerEntity.health < 0) {
        // 0x0049518a: attackDirection == 0x7FFF flips the facing 180 degrees, so a
        // back-shot death plays turned around.
        if (g_playerEntity.attackDirection == 0x7FFF) {
            g_playerEntity.directionAngle += 0x800;
        }
        // 0x0049519e writes a dword over animationId..action_state at once, then
        // overrides action_behavior with the death index.
        g_playerEntity.animationId    = 3;
        g_playerEntity.animFrameId    = 0;
        g_playerEntity.action_state   = 0;
        g_playerEntity.action_behavior = 200;
        g_playerEntity.isBeingAttackedFlag = 1;
        // 0x004951b4: msf2 bit 0 is the "cannot die" debug/scripted guard.
        if ((g_main_state_flags2 & 1) != 0) {
            g_playerEntity.health = 1;
        }
        return;
    }

    // 0x004951cc: taking a hit pre-empts control and runs the damage state.
    if ((g_playerEntity.isBeingAttackedFlag & 0x3f) != 0) {
        g_playerEntity.action_state = 0;
        g_playerEntity.animationId  = 2;
        g_playerEntity.animFrameId  = 0;
        return;
    }

    // 0x004951e6: poison / crimson-head style status drain. The timer byte at
    // entity+0x174 is read BEFORE it is decremented, so the tick fires on the frame
    // the old value was already 0 (the decrement having wrapped it to 0xFF).
    if ((g_playerEntity.healthStatusFlags & 0x62) != 0) {
        unsigned char prev = g_playerEntity.pad_174;
        g_playerEntity.pad_174--;
        if (prev == 0) {
            unsigned char fast = (unsigned char)(g_playerEntity.healthStatusFlags & 0x40);
            g_playerEntity.pad_174 = fast ? 7 : 120;
            g_playerEntity.health -= 2;
            // Only the 0x40 variant is allowed to drive health negative (that is
            // the one that actually kills); everything else floors at 1.
            if ((short)g_playerEntity.health < 0 && fast == 0) {
                g_playerEntity.health = 1;
            }
        }
    }

    // 0x0049522c
    if (g_playerEntity.attackTimer != 0) {
        g_playerEntity.attackTimer--;
    }

    // 0x0049523d: dispatch on animFrameId.
    unsigned char frame = g_playerEntity.animFrameId;
    if (frame >= 5) {
        player_state_report_missing("animFrameId out of range for 0x004d4578");
        return;
    }
    ((void(*)(void))g_playerCtrlFrameFunctions[frame])();
}

// ----------------------------------------------------------------------------
// animFrameId handlers 1-4. Still to transcribe; Ghidra has no function defined at
// any of these addresses yet. Named for what selects them rather than for the
// animationId they were previously mislabelled with.
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Helpers the control state reaches for. Each is a real function in the original
// and still to be transcribed; they report by address so the log names whichever
// one the player is actually asking for.
//
// CORRECTION: 0x00474930 is NOT a door check, despite feeding action_behavior 10.
// It walks g_itemboxes_covers_table through ChkPlReachEntity and an angle window,
// so behaviour 10 is "climb over / push object". Doors do not come through here at
// all - check_door sets unk_03 |= 0x20 and the branch on that bit in
// player_input_to_behavior selects action_behavior 0x11 instead.
//
// Still to transcribe; it needs ChkPlReachEntity, which the port does not have yet.
// ----------------------------------------------------------------------------
static int player_check_climb_object(void)       // 0x00474930
{
    player_state_report_missing("0x00474930 check climb/push object");
    return 0;
}

static int player_check_action_object(void)      // 0x0041c150
{
    player_state_report_missing("0x0041c150 check action object");
    return 0;
}

static void player_door_transition_update(void)  // 0x00495d70
{
    player_state_report_missing("0x00495d70 door transition update");
}

// ----------------------------------------------------------------------------
// Footstep sounds.
//
// NOTE: the original calls PlayEntitySnd with TWO arguments - (0, 0) and
// (0, 0xFFFFFFFC) - but the port declares it as PlayEntitySnd(unsigned char).
// The second argument selects a footstep variant, so the port currently plays the
// same sound for both. Fixing that means widening the signature at 0x0047fbf0 and
// auditing its existing call sites; deliberately left alone here rather than
// silently dropping the parameter. Cosmetic only - it does not affect movement.
// ----------------------------------------------------------------------------
static void player_footstep_snd(int variant)
{
    (void)variant;
    PlayEntitySnd(0);
}

// ============================================================================
// player_ctrl_behavior_walk (0x00495a70)
// action_behavior 1/2/3 — walking forward. Three-step state machine: start, run,
// then release back to idle when the forward bit is let go.
//
// The 8-byte local table is per-character (Chris/Jill, `id & 1` picks the half):
//   +0 / +1  animation-frame windows that trigger a speed reduction
//   +2 / +3  how much speed to subtract in each window
// This is what makes the walk cycle slow down on footfalls.
// ============================================================================
static void player_ctrl_behavior_walk(void)
{
    static const unsigned char kWalkSpeedTable[8] = {
        0x15, 0x17, 0x0D, 0x0E,   // character 0
        0x14, 0x16, 0x0F, 0x0F,   // character 1
    };

    // 0x00495a8a: forward released -> go to the release step.
    if ((g_PlayerDpadHeld & 1) == 0) {
        g_playerEntity.action_state = 2;
    }

    if (g_playerEntity.action_state == 0) {
        g_playerEntity.attackAnim         = 2;
        g_playerEntity.action_state       = 1;
        g_playerEntity.unk_8c             = 3;
        g_playerEntity.attackDirection    = 0;
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf             = 0;
        if ((g_main_state_flags2 & 1) != 0) {
            player_footstep_snd(0);
        }
    } else if (g_playerEntity.action_state != 1) {
        if (g_playerEntity.action_state == 2) {
            g_playerEntity.action_behavior = 0;
            g_playerEntity.action_state    = 0;
            player_footstep_snd(0);
        }
        Add_speedXZ(0);
        return;
    }

    // 0x00495b0a: dpad bit 9 (0x200) upgrades the walk into the aim-walk behaviour.
    if ((g_PlayerDpadHeld & 0x200) != 0) {
        g_playerEntity.animFrameId     = 1;
        g_playerEntity.action_behavior = 0xd;
        g_playerEntity.action_state    = 0;
    }

    unsigned char frame = g_playerEntity.animation_frame_id;
    if (g_playerEntity.attackDirection == 0) {
        if (frame == 0x08) { player_footstep_snd(0); }
        if (frame == 0x16) { player_footstep_snd(-4); }
    }

    short prevCounter = (short)g_playerEntity.attackDirection;
    unsigned int t = (unsigned int)(g_playerEntity.id & 1) * 4;

    g_playerEntity.move_speed_current = 0x5d;
    if ((unsigned char)(frame - kWalkSpeedTable[t]) < 7) {
        g_playerEntity.move_speed_current = (unsigned short)(0x5d - kWalkSpeedTable[t + 2]);
    }
    if ((unsigned char)(frame - 7) < 7) {
        g_playerEntity.move_speed_current -= kWalkSpeedTable[t + 2];
    }
    if ((unsigned char)(frame - kWalkSpeedTable[t + 1]) < 3) {
        g_playerEntity.move_speed_current -= kWalkSpeedTable[t + 3];
    }
    if ((unsigned char)(frame - 9) < 3) {
        g_playerEntity.move_speed_current -= kWalkSpeedTable[t + 3];
    }

    // msf2 bit 0 halves the speed and advances the animation only every other
    // frame - the slow-motion variant.
    if ((g_main_state_flags2 & 1) == 0) {
        Joint_move(0, g_playerEntity.jointMoveData0, g_playerEntity.jointMoveData1, 0x400);
        g_playerEntity.attackDirection = 0;
    } else {
        g_playerEntity.move_speed_current /= 2;
        g_playerEntity.attackDirection--;
        if (prevCounter == 0) {
            Joint_move(0, g_playerEntity.jointMoveData0, g_playerEntity.jointMoveData1, 0x400);
            g_playerEntity.attackDirection = 1;
        }
    }

    Add_speedXZ(0);
}

// ============================================================================
// player_ctrl_behavior_back (0x00495c90)
// action_behavior 4/5 — turning in place. The rotation itself is applied by the
// caller (player_ctrl_frame0 adds +/-0x60 to directionAngle); this drives the
// animation and, critically, releases the behaviour when the input stops.
//
// The 0x0A mask is both trigger bits at once (0x02 and 0x08), because one handler
// serves behaviours 4 and 5.
// ============================================================================
static void player_ctrl_behavior_back(void)
{
    // 0x00495c90: neither turn direction held -> release.
    if ((g_PlayerDpadHeld & 0x0A) == 0) {
        g_playerEntity.action_state = 2;
    }

    if (g_playerEntity.action_state == 0) {
        g_playerEntity.animation_frame_id  = 0;
        g_playerEntity.unk_bf              = 0;
        g_playerEntity.move_speed_current  = 1;
        g_playerEntity.action_state        = 1;
        g_playerEntity.attackAnim          = 2;
        g_playerEntity.unk_8c              = 3;
        g_playerEntity.isBeingAttackedFlag = 0;
        if ((g_main_state_flags2 & 1) != 0) {
            player_footstep_snd(0);
        }
    } else if (g_playerEntity.action_state != 1) {
        if (g_playerEntity.action_state != 2) {
            return;
        }
        g_playerEntity.action_behavior = 0;
        g_playerEntity.action_state    = 0;
        player_footstep_snd(0);
        return;
    }

    if (g_playerEntity.animation_frame_id == 0x08) { player_footstep_snd(0); }
    if (g_playerEntity.animation_frame_id == 0x16) { player_footstep_snd(-4); }

    Joint_move(0, g_playerEntity.jointMoveData0, g_playerEntity.jointMoveData1, 0x400);
}

// Declared the same way in entities/EntityCommon.h, which this file does not include.
extern unsigned char checkAngularViewAndDistance(short fovHalfAngle, short maxDistance,
                                                VECTOR* targetPos);

// ============================================================================
// player_ctrl_behavior_run (0x00495ed0)
// action_behavior 6/7/8 — running. Same three-step shape, plus an enemy-proximity
// scan: with any live, non-suppressed enemy inside a 0x200 half-angle and 8000
// units, the player keeps the guarded walk animation (attackAnim 2) instead of the
// full run (attackAnim 3).
// ============================================================================
static void player_ctrl_behavior_run(void)
{
    if ((g_PlayerDpadHeld & 4) == 0) {
        g_playerEntity.action_state = 2;
    }

    if (g_playerEntity.action_state == 0) {
        g_playerEntity.action_state        = 1;
        g_playerEntity.unk_8c             = 3;
        g_playerEntity.attackDirection    = 0;
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf             = 0;
        g_playerEntity.isBeingAttackedFlag = 0;
        if ((g_main_state_flags2 & 1) != 0) {
            player_footstep_snd(0);
        }
    } else if (g_playerEntity.action_state != 1) {
        if (g_playerEntity.action_state == 2) {
            g_playerEntity.action_behavior = 0;
            g_playerEntity.action_state    = 0;
            player_footstep_snd(0);
        }
        Add_speedXZ(0x800);
        return;
    }

    // 0x00495f1e: is any enemy in view? The original walks g_EnemiesList and only
    // decrements its remaining-count when the slot is active, so inactive slots do
    // not consume an iteration.
    // The original (0x00495f37) has NO upper bound on the walk: it advances the
    // pointer on every slot but only decrements the counter on *active* ones, so it
    // relies on g_enemy_count never exceeding the number of slots with
    // status_flags bit 0 set. If that invariant does not hold in the port the loop
    // runs off the end of the 30-slot array and never terminates. Bounded to the
    // real array size, and it reports when the bound is what stopped it.
    unsigned char enemyInView = 0;
    {
        Entity* e = g_EnemiesList;
        char remaining = (char)g_enemy_count;
        int slot = 0;
        while (remaining != 0 && slot < 30) {
            if ((e->status_flags & 1) != 0) {
                if ((short)e->health >= 0 && (e->behavior_flags & 0x80) == 0) {
                    enemyInView |= checkAngularViewAndDistance(
                        0x200, 8000, (VECTOR*)e->scaMatrixData.localMatrix.t);
                }
                remaining--;
            }
            e++;
            slot++;
        }
        if (remaining != 0) {
            static int reported = 0;
            if (reported == 0) {
                reported = 1;
                dbg_printf("[prun] enemy scan hit the 30-slot bound with %d left of"
                           " g_enemy_count=%d - the original would have looped here\n",
                           (int)remaining, (int)g_enemy_count);
            }
        }
    }

    unsigned char frame = g_playerEntity.animation_frame_id;
    if (enemyInView == 0) {
        if (g_playerEntity.attackAnim != 3) {
            g_playerEntity.attackAnim         = 3;
            g_playerEntity.animation_frame_id = 0;
            g_playerEntity.unk_bf             = 0;
            g_playerEntity.unk_8c             = 3;
        }
        if (g_playerEntity.attackDirection == 0 && (frame == 0x08 || frame == 0x16)) {
            player_footstep_snd(0);
        }
        // NOTE: the original's second test is `(frame > 4) || (frame < 8)`, which is
        // always true, so move_speed_current is unconditionally 0x40 here. Kept as
        // written rather than "corrected" - see the always-true-condition entries in
        // docs/SCD_WORK_PLAN.md.
        g_playerEntity.move_speed_current = 0x3c;
        if (frame > 4 || frame < 8) {
            g_playerEntity.move_speed_current = 0x40;
        }
    } else {
        if (g_playerEntity.attackAnim != 2) {
            g_playerEntity.attackAnim         = 2;
            g_playerEntity.unk_8c             = 3;
            g_playerEntity.animation_frame_id = 0;
            g_playerEntity.unk_bf             = 0;
        }
        if ((frame == 0x07 || frame == 0x1b) && g_playerEntity.unk_bf == 1 &&
            g_playerEntity.attackDirection == 0) {
            player_footstep_snd(-4);
        }
        g_playerEntity.move_speed_current = 0x3c;
    }

    short prevCounter = (short)g_playerEntity.attackDirection;
    if ((g_main_state_flags2 & 1) == 0) {
        Joint_move(0, g_playerEntity.animHeader, g_playerEntity.animBase, 0x400);
    } else {
        g_playerEntity.move_speed_current /= 2;
        g_playerEntity.attackDirection--;
        if (prevCounter == 0) {
            Joint_move(0, g_playerEntity.animHeader, g_playerEntity.animBase, 0x400);
            g_playerEntity.attackDirection = 1;
        }
    }

    Add_speedXZ(0x800);
}

// ============================================================================
// player_door_open_sequence (0x00457390)
// action_behavior 10 / 0x11 - open a door and carry the player through it. This
// is the last link in the door chain: cmd_door_set registers the zone,
// update_player_position fires check_door, check_door raises unk_03 bit 0x20,
// player_input_to_behavior turns that into action_behavior 0x11, and this runs
// the animation and the warp. While it was a stub the player reached the door and
// simply stood in the zone forever.
//
// Four steps on action_state (entity+0x87). The original selects them with
// `CMP EAX,3 / JA default`, so the range really is 0-3 with no table:
//
//   0  turn to face the door, then advance once the residual angle is inside
//      0x3e0
//   1  pick the open animation and size the shadow quad, then FALL THROUGH to 2
//   2  advance the opening animation, firing the door SFX at fixed frames; the
//      Joint_move completion return is what advances action_state
//   3  teleport the player through the doorway, raise g_message_flags bit 0x40
//      for the room-change path, and reset to normal control
//
// Faithfulness notes, all checked against the disassembly rather than Ghidra's C:
//
//  - Case 1 falls through into case 2 (there is no jump at 0x00457504).
//  - The turn step is `(angle & 0x3fc) >> 2`, a proportional ease-in rather than a
//    fixed rate, and every angle access is 16-bit.
//  - Case 0's second turn block writes ENTITY->angle through the *global* ENTITY
//    pointer (0x00bebcd4), not &g_playerEntity. update_player_anim points ENTITY
//    at the player before dispatching so they are the same object here; written
//    the original's way rather than "corrected" onto the named field.
//  - move_speed_current (0xC2) is reused as the SFX step counter in cases 2 and 3,
//    not as a speed. The frame tests are `15*counter - animFrame == -0xc` and
//    `9*counter - animFrame == 1`, and the first is computed once from the
//    pre-increment value, before the 0x80 branch.
//  - `g_main_state_flags & 0x80` distinguishes the climb/vault entry (behaviour 10,
//    set by player_input_to_behavior) from a plain door (0x11); it selects SFX
//    0x23 over 0x2d and a different, negated displacement.
//  - unk_03 bit 0x10 is check_door's "door swings the other way" flag: it picks
//    animation 0x35 over 0x33 and mirrors every Z displacement.
//  - g_message_flags |= 0x40 is a byte OR in the original.
//  - Case 3's reset is one dword store to 0x00be6368, covering animationId /
//    animFrameId / action_behavior / action_state together.
// ============================================================================
extern int player_distance_z;    // 0x00be0de4 - scratch, shared with the zombie code
extern int g_scaled_down_dist;   // 0x00be0de8 - scratch, shared with the zombie code

static void player_door_open_sequence(void)      // 0x00457390
{
    // 0x00457399: latched at entry, before the switch, and still the entry value
    // when case 1 falls through into case 2.
    JointStruct* joints = g_playerEntity.jointsStructs;

    // DIAGNOSTIC - remove once the door animation is confirmed. Reports each step
    // change plus the inputs that select the variant, so a machine stuck in one
    // state is obvious and names the reason.
    {
        static int lastState = -1;
        if ((int)g_playerEntity.action_state != lastState) {
            lastState = (int)g_playerEntity.action_state;
            dbg_printf("[dooranim] st=%d beh=%u unk03=%02X angle=%04X msf=%08X msf2=%08X\n",
                       lastState, (unsigned int)g_playerEntity.action_behavior,
                       (unsigned int)g_playerEntity.unk_03,
                       (unsigned int)(unsigned short)g_playerEntity.directionAngle,
                       (unsigned int)g_main_state_flags,
                       (unsigned int)g_main_state_flags2);
        }
    }

    switch (g_playerEntity.action_state) {
    case 0: {
        g_playerEntity.move_speed_current = 0;
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf             = 0;
        g_playerEntity.attackAnim         = 2;
        g_playerEntity.unk_8c             = 3;
        Joint_move(0, g_playerEntity.jointMoveData0, g_playerEntity.jointMoveData1, 0x400);

        // 0x004573ed: the climb/vault entry turns the player using his own facing.
        if ((g_main_state_flags & 0x80) != 0) {
            unsigned short a    = (unsigned short)g_playerEntity.directionAngle;
            unsigned short step = (unsigned short)((a & 0x3fc) >> 2);
            if ((a & 0x200) != 0) {
                g_playerEntity.directionAngle = (short)(unsigned short)(a + step);
            } else {
                g_playerEntity.directionAngle = (short)(unsigned short)(a - step);
            }
            g_main_state_flags2 |= 0x1000000;
        }

        // 0x0045742f: check_door set 0x400000; consume it to pick the turn
        // direction. Bit 0x40 of unk_03 and bit 0x400 of the angle together decide
        // whether to add or subtract - the two branches are exact mirrors.
        if ((g_main_state_flags2 & 0x400000) != 0) {
            short*         pAngle = (short*)((unsigned char*)ENTITY + 0x74);
            unsigned short a      = (unsigned short)*pAngle;
            unsigned short step   = (unsigned short)((a & 0x3fc) >> 2);
            bool turnUp;
            if ((g_playerEntity.unk_03 & 0x40) != 0) {
                turnUp = ((a & 0x400) == 0);
            } else {
                turnUp = ((a & 0x400) != 0);
            }
            *pAngle = (short)(unsigned short)(turnUp ? (a + step) : (a - step));
        }

        // 0x00457493: still turning - hold here until the angle settles.
        if ((g_playerEntity.directionAngle & 0x3e0) != 0) {
            return;
        }
        g_playerEntity.action_state = 1;
        return;
    }

    case 1:
        // 0x004574ac
        g_playerEntity.attackAnim         = 0x33;
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf             = 0;
        if ((g_playerEntity.unk_03 & 0x10) != 0) {
            g_playerEntity.attackAnim = 0x35;
        }
        g_playerEntity.action_state       = 2;
        g_playerEntity.unk_8c             = 3;
        g_playerEntity.move_speed_current = 0;
        BillboardSetRect(&g_playerEntity.pushVelocity, 800, 700, 700, 700);
        // fall through

    case 2: {
        // 0x00457507: computed once, from move_speed_current before any increment.
        int frameDelta = (int)(short)g_playerEntity.move_speed_current * 15
                       - (int)g_playerEntity.animation_frame_id;

        if ((g_main_state_flags & 0x80) == 0) {
            // 0x004577f6: plain door - one latch sound.
            if (frameDelta == -0xc) {
                Play3DSnd(2, 0x2d, 0, (int)&g_playerEntity.scaMatrixData.localMatrix.t);
                g_playerEntity.move_speed_current++;
            }
        } else {
            // 0x00457521: climb/vault - a longer cue sequence.
            if (frameDelta == -0xc) {
                Play3DSnd(2, 0x23, 0, (int)&g_playerEntity.scaMatrixData.localMatrix.t);
                g_playerEntity.move_speed_current++;
            }
            if (g_playerEntity.move_speed_current == 3) {
                g_playerEntity.move_speed_current = 7;
            }
            if ((g_playerEntity.move_speed_current == 7) &&
                (g_playerEntity.animation_frame_id == 0x35)) {
                Play3DSnd(2, 0x23, 0, (int)&g_playerEntity.scaMatrixData.localMatrix.t);
            }
            if (((g_playerEntity.unk_03 & 0x10) != 0) &&
                (g_playerEntity.move_speed_current == 2)) {
                g_playerEntity.move_speed_current = 5;
            }
            // 0x0045759f: signed compare in the original (CMP word,4 / JLE).
            if ((short)g_playerEntity.move_speed_current > 4) {
                int stepDelta = (int)(short)g_playerEntity.move_speed_current * 9
                              - (int)g_playerEntity.animation_frame_id;
                if (stepDelta == 1) {
                    g_playerEntity.move_speed_current = 6;
                    PlayEntitySnd(0);
                }
            }
        }

        // 0x0045781f: shared tail. g_svecScratch.y is the per-frame vertical creep
        // that walks the player through the doorway as the door swings.
        g_svecScratch.x = 0;
        g_svecScratch.y = -0x29;
        g_svecScratch.z = 0;
        if ((g_playerEntity.unk_03 & 0x10) != 0) {
            g_svecScratch.y = 0x29;
        }
        if ((g_roomId == 0xe) || (g_roomId == 5)) {
            g_svecScratch.y = -0x24;
            if ((g_playerEntity.unk_03 & 0x10) != 0) {
                g_svecScratch.y = 0x24;
            }
        }
        if ((g_main_state_flags & 0x80) != 0) {
            g_playerEntity.unk_e0 |= 0x40;
        }

        // 0x00457889: both subtractions are 16-bit in the original.
        g_playerEntity.pushVelocity.x =
            (short)((short)joints->world.t[0] -
                    (short)g_playerEntity.scaMatrixData.localMatrix.t[0]);
        g_playerEntity.unk_8e = (unsigned short)(g_playerEntity.unk_8e + g_svecScratch.y);
        g_playerEntity.pushVelocity.z =
            (short)((short)joints->world.t[2] -
                    (short)g_playerEntity.scaMatrixData.localMatrix.t[2]);

        // 0x004578cd: ADD byte ptr [action_state],AL - the animation's own
        // completion return is what moves the machine to case 3.
        g_playerEntity.action_state = (unsigned char)
            (g_playerEntity.action_state +
             (unsigned char)Joint_move(0, g_playerEntity.jointMoveData2,
                                       g_playerEntity.jointMoveData3, 0x400));
        break;
    }

    case 3: {
        // 0x004575e4: the warp.
        g_playerEntity.move_speed_current = 0;
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf             = 0;
        g_playerEntity.unk_8c             = 0;
        g_playerEntity.attackAnim++;
        Joint_move(0, g_playerEntity.jointMoveData2, g_playerEntity.jointMoveData3, 0x400);

        // check_door stored the approach side here as +1 / -1 (signed 16-bit).
        int            dir      = (int)(short)g_playerEntity.attackDirection;
        unsigned short sideways = (unsigned short)(g_playerEntity.directionAngle & 0x400);
        unsigned char  otherWay = (unsigned char)(g_playerEntity.unk_03 & 0x10);

        // Angle bit 0x400 means the doorway runs along Z rather than X, so the
        // displacement swaps axes.
        g_scaled_down_dist   = 0;
        g_playerDisplacement = dir * 0x10fe;
        if (sideways != 0) {
            g_playerDisplacement = 0;
            g_scaled_down_dist   = dir * 0x10fe;
        }
        player_distance_z = -0xb45;
        if (otherWay != 0) {
            player_distance_z = 0xb45;
        }

        // Rooms 5 and 0xE have shallower doorways.
        if ((g_roomId == 0xe) || (g_roomId == 5)) {
            g_scaled_down_dist   = 0;
            g_playerDisplacement = dir * 0x842;
            if (sideways != 0) {
                g_playerDisplacement = 0;
                g_scaled_down_dist   = dir * 0x842;
            }
            player_distance_z = -0x57a;
            if (otherWay != 0) {
                player_distance_z = 0x57a;
            }
        }

        // 0x004576d4: the climb/vault case. Note the X displacement is negated
        // while the Z one is not - that asymmetry is in the original.
        if ((g_main_state_flags & 0x80) != 0) {
            g_playerDisplacement = -(dir * 0x73a);
            g_scaled_down_dist   = 0;
            if (sideways != 0) {
                g_playerDisplacement = 0;
                g_scaled_down_dist   = dir * 0x73a;
            }
            player_distance_z = -0x708;
            if (otherWay != 0) {
                g_main_state_flags   &= 0xffffff7f;
                g_playerEntity.unk_e0 = (unsigned short)(g_playerEntity.unk_e0 & 0xffbf);
                player_distance_z     = 0x708;
            }
            g_main_state_flags2 &= 0xfeffffff;
        }

        g_playerEntity.scaMatrixData.localMatrix.t[0] += g_playerDisplacement;
        g_playerEntity.position.x = (short)g_playerEntity.scaMatrixData.localMatrix.t[0];
        g_playerEntity.scaMatrixData.localMatrix.t[1] += player_distance_z;
        g_playerEntity.position.y = (short)g_playerEntity.scaMatrixData.localMatrix.t[1];
        g_playerEntity.scaMatrixData.localMatrix.t[2] += g_scaled_down_dist;
        g_playerEntity.position.z = (short)g_playerEntity.scaMatrixData.localMatrix.t[2];

        BillboardSetRect(&g_playerEntity.pushVelocity, 500, 500, 700, 700);

        // 0x004577b7: byte OR. Bit 0x40 is what door_try_enter / the room-change
        // path waits for, so this is the handoff out of the door animation.
        ((unsigned char*)&g_message_flags)[0] |= 0x40;

        g_playerEntity.pushVelocity.x = 0;
        g_playerEntity.unk_8e =
            (unsigned short)(short)g_playerEntity.scaMatrixData.localMatrix.t[1];
        g_main_state_flags2 &= 0xffbfffff;   // consume check_door's 0x400000
        g_playerEntity.pushVelocity.z = 0;

        // 0x004577e5: one dword store back to normal control.
        g_playerEntity.animationId         = 1;
        g_playerEntity.animFrameId         = 0;
        g_playerEntity.action_behavior     = 0;
        g_playerEntity.action_state        = 0;
        g_playerEntity.isBeingAttackedFlag = 0;
        return;
    }

    default:
        break;
    }
}

// ============================================================================
// player_input_to_behavior (0x004956a0)
// Reads the D-pad and picks the next action_behavior. This is the function that
// starts the door transition: with the action button down and a door in reach it
// sets action_behavior = 10, which player_ctrl_frame0 routes to the door animation.
//
// D-pad bit layout used here: 0x80 = action/confirm, 0x100 = aim, low nibble =
// direction. The 0xC0 test accepts either 0x80 or 0xC0, i.e. action pressed with or
// without the second modifier bit.
// ============================================================================
static void player_input_to_behavior(void)
{
    unsigned int held = (unsigned int)g_PlayerDpadHeld;

    // 0x004956a0: action button newly pressed
    if ((((held & 0xc0) == 0x80) || ((held & 0xc0) == 0xc0)) &&
        ((g_PlayerDpadPressed & 0x80) != 0)) {

        // 0x004956c9: is there a door in front of the player? Sets msf bit 7,
        // which the door animation reads to pick its variant.
        if (player_check_climb_object() != 0) {
            g_main_state_flags |= 0x80;
            g_message_flags &= 0xffbf;
            g_playerEntity.action_behavior = 10;
            g_playerEntity.action_state    = 0;
            g_playerEntity.isBeingAttackedFlag = 0x80;
            g_playerEntity.animFrameId     = 1;
            return;
        }

        // 0x004956f8: an examinable/usable object instead
        if (player_check_action_object() != 0) {
            g_playerEntity.healthStatusFlags |= 0x80;
            g_message_flags &= 0xffbf;
            g_playerEntity.action_behavior = 0xc;
            g_playerEntity.action_state    = 0;
            g_playerEntity.animFrameId     = 1;
            return;
        }

        // 0x00495727: standing in a stairs/ladder zone
        if ((g_playerEntity.unk_03 & 0x20) != 0) {
            g_playerEntity.isBeingAttackedFlag = 0x80;
            g_playerEntity.animFrameId = 1;
            g_message_flags &= 0xffbf;
            if ((g_main_state_flags & 0x10) == 0) {
                g_playerEntity.action_behavior = 0x11;
                g_playerEntity.action_state    = 0;
                g_playerEntity.isBeingAttackedFlag = 0x80;
                return;
            }
            g_playerEntity.action_behavior = 0xb;
            g_playerEntity.action_state    = 0;
            return;
        }
    }

    // 0x0049578d: already inside a door transition
    if ((g_main_state_flags & 0x80) != 0) {
        player_door_transition_update();
        return;
    }

    // 0x0049579d: msf bit 6 forces the climb/vault behaviour
    if ((g_main_state_flags & 0x40) != 0) {
        g_playerEntity.animFrameId = 1;
        g_message_flags &= 0xffbf;
        g_playerEntity.action_behavior = 0x10;
        g_playerEntity.action_state    = 0;
        return;
    }

    if ((g_playerEntity.unk_03 & 0x20) != 0) {
        g_playerEntity.isBeingAttackedFlag = 0x80;
        g_playerEntity.animFrameId = 1;
        g_message_flags &= 0xffbf;
        g_playerEntity.action_behavior = 0x11;
        g_playerEntity.action_state    = 0;
        if ((g_main_state_flags & 0x10) != 0) {
            g_playerEntity.action_behavior = 0xb;
            g_playerEntity.action_state    = 0;
        }
        return;
    }

    // 0x004957c6: aim button. Two ranges of equippedWeaponId select the same
    // aim behaviour; the knife (id 1) uses a different animFrameId.
    if ((held & 0x100) != 0 &&
        (g_playerEntity.equippedWeaponId > 0x6e ||
         (g_playerEntity.equippedWeaponId != 0 && g_playerEntity.equippedWeaponId < 0xb))) {
        g_playerEntity.animFrameId = (g_playerEntity.equippedWeaponId == 1) ? 4 : 3;
        g_playerEntity.action_behavior = 0x12;
        g_playerEntity.action_state    = 0;
        return;
    }

    // 0x00495849: direction. The `prev` test makes a fresh press reset
    // action_state while a held direction keeps the current animation running.
    unsigned char prevLow = (unsigned char)g_PlayerDpadHeldPrev;
    switch (held & 0xf) {
    case 1:   // forward
        g_playerEntity.action_behavior = 1;
        if ((prevLow & 1) == 0) { g_playerEntity.action_state = 0; }
        break;
    case 2:   // back
        if (g_playerEntity.action_behavior != 4) {
            g_playerEntity.action_behavior = 0;
            g_playerEntity.action_state    = 0;
        }
        g_playerEntity.action_behavior = 4;
        break;
    case 3:   // forward + left
        g_playerEntity.action_behavior = 2;
        if ((prevLow & 1) == 0) { g_playerEntity.action_state = 0; }
        break;
    case 4:   // right
        g_playerEntity.action_behavior = 8;
        if ((prevLow & 4) == 0) { g_playerEntity.action_state = 0; }
        break;
    case 6:
        g_playerEntity.action_behavior = 6;
        if ((prevLow & 4) == 0) { g_playerEntity.action_state = 0; }
        break;
    case 8:   // left
        if (g_playerEntity.action_behavior != 5) {
            g_playerEntity.action_behavior = 0;
            g_playerEntity.action_state    = 0;
        }
        g_playerEntity.action_behavior = 5;
        break;
    case 9:
        g_playerEntity.action_behavior = 3;
        if ((prevLow & 1) == 0) { g_playerEntity.action_state = 0; }
        break;
    case 0xc:
        g_playerEntity.action_behavior = 7;
        if ((prevLow & 4) == 0) { g_playerEntity.action_state = 0; }
        break;
    default:
        break;
    }
}

// ============================================================================
// player_behavior_00_idle (0x00495960)
// action_behavior 0 — standing idle. A four-step sequence on action_state: settle
// into the idle pose, hold it for 100 frames, then blend into the looping breathe
// animation. This is what makes a standing Chris look alive rather than frozen.
//
// attackDirection is reused here as the countdown, matching the original.
// ============================================================================
static void player_behavior_00_idle(void)
{
    switch (g_playerEntity.action_state) {
    case 0:
        g_playerEntity.action_state++;
        g_playerEntity.attackDirection      = 100;
        g_playerEntity.animation_frame_id   = 0;
        g_playerEntity.unk_bf               = 0;
        g_playerEntity.move_speed_current   = 0;
        g_playerEntity.attackAnim           = 0;
        g_playerEntity.unk_8c               = 3;
        g_playerEntity.isBeingAttackedFlag  = 0;
        // fall through
    case 1:
        Joint_move(0, g_playerEntity.animHeader, g_playerEntity.animBase, 0x400);
        g_playerEntity.attackDirection--;
        // g_message_flags bit 8 (byte 1 bit 0) gates the transition into the
        // breathe loop, so it does not start mid-message.
        if (g_playerEntity.attackDirection == 0 &&
            ((((unsigned char*)&g_message_flags)[1] & 1) != 0)) {
            g_playerEntity.action_state++;
            g_playerEntity.animation_frame_id = 0;
            g_playerEntity.unk_bf             = 0;
            g_playerEntity.attackAnim         = 0;
            g_playerEntity.unk_8c             = 3;
        }
        break;
    case 2:
        if (Joint_move(0, g_playerEntity.jointMoveData0,
                       g_playerEntity.jointMoveData1, 0x400) != 0) {
            g_playerEntity.action_state++;
            g_playerEntity.animation_frame_id = 0;
            g_playerEntity.attackAnim         = 1;
            g_playerEntity.unk_8c             = 3;
            g_playerEntity.unk_bf             = 0;
        }
        break;
    case 3:
        Joint_move(0, g_playerEntity.jointMoveData0,
                   g_playerEntity.jointMoveData1, 0x400);
        break;
    default:
        break;
    }
}

// ============================================================================
// player_ctrl_frame0 (0x00495320) — animFrameId 0
// Read input, then run the handler for the resulting action_behavior. Behaviours
// 10 and 0x11 are the door transition; the rest are locomotion and are still to
// be transcribed.
// ============================================================================
static void player_ctrl_frame0(void)
{
    player_input_to_behavior();

    switch (g_playerEntity.action_behavior) {
    case 0:
        player_behavior_00_idle();
        return;
    case 1:
        player_ctrl_behavior_walk();
        return;
    case 2:
        g_playerEntity.directionAngle = (g_playerEntity.directionAngle + 0x28) & 0xfff;
        player_ctrl_behavior_walk();
        return;
    case 3:
        g_playerEntity.directionAngle = (g_playerEntity.directionAngle - 0x28) & 0xfff;
        player_ctrl_behavior_walk();
        return;
    case 4:
        g_playerEntity.directionAngle = (g_playerEntity.directionAngle + 0x60) & 0xfff;
        player_ctrl_behavior_back();
        return;
    case 5:
        g_playerEntity.directionAngle = (g_playerEntity.directionAngle - 0x60) & 0xfff;
        player_ctrl_behavior_back();
        return;
    case 6:
        g_playerEntity.directionAngle = (g_playerEntity.directionAngle + 0x28) & 0xfff;
        player_ctrl_behavior_run();
        return;
    case 7:
        g_playerEntity.directionAngle = (g_playerEntity.directionAngle - 0x28) & 0xfff;
        player_ctrl_behavior_run();
        return;
    case 8:
        player_ctrl_behavior_run();
        return;
    case 10:      // door transition
    case 0x11:
        player_door_open_sequence();
        return;
    default:
        player_state_report_missing("action_behavior under animFrameId 0");
        return;
    }
}

// ============================================================================
// player_behavior_0d_run (0x00496110)
// action_behavior 0x0d — the real run, reached by holding the run modifier (D-pad
// 0x200) while walking. player_ctrl_behavior_walk hands over to it by setting
// animFrameId = 1, so it runs under player_ctrl_frame1 rather than frame0 and does
// its own input reading.
//
// Steering here is a single expression rather than the caller-applied angle tweak
// frame0 uses: 0x02 turns right by 0x30, 0x08 turns left by 0x30.
//
// action_state 3 is the exit: after four frames of the stopping animation it puts
// animFrameId and action_behavior back to 0, returning to normal control. Without
// this handler the player was stuck in animFrameId 1 forever, and because input is
// only read under frame0 that presented as a total freeze.
// ============================================================================
static void player_behavior_0d_run(void)
{
    // 0x00496110: the climb/vault flag wins over running.
    if ((g_main_state_flags & 0x40) != 0) {
        g_playerEntity.animFrameId     = 1;
        g_playerEntity.action_behavior = 0x10;
        g_playerEntity.action_state    = 0;
        return;
    }

    // 0x00496134: the action button still works while running.
    if ((g_PlayerDpadPressed & 0x80) != 0) {
        if (player_check_climb_object() != 0) {
            g_playerEntity.isBeingAttackedFlag = 0x80;
            g_main_state_flags |= 0x80;
            g_message_flags &= 0xffbf;
            g_playerEntity.action_behavior = 10;
            g_playerEntity.action_state    = 0;
            g_playerEntity.animFrameId     = 1;
            return;
        }
        if (player_check_action_object() != 0) {
            g_playerEntity.animFrameId     = 1;
            g_message_flags &= 0xffbf;
            g_playerEntity.action_behavior = 0xc;
            g_playerEntity.action_state    = 0;
            return;
        }
        if ((g_playerEntity.unk_03 & 0x20) != 0) {
            g_playerEntity.isBeingAttackedFlag = 0x80;
            g_playerEntity.animFrameId = 1;
            g_message_flags &= 0xffbf;
            if ((g_main_state_flags & 0x10) == 0) {
                g_playerEntity.action_behavior = 0x11;
                g_playerEntity.action_state    = 0;
                g_playerEntity.isBeingAttackedFlag = 0x80;
                return;
            }
            g_playerEntity.action_behavior = 0xb;
            g_playerEntity.action_state    = 0;
            return;
        }
    }

    // 0x004961e6: forward released, or aiming with a weapon equipped, begins the stop.
    if (g_playerEntity.action_state < 2) {
        if ((g_PlayerDpadHeld & 1) == 0) {
            g_playerEntity.action_state = 2;
        }
        if ((g_PlayerDpadHeld & 0x100) != 0 && g_playerEntity.equippedWeaponId != 0) {
            g_playerEntity.action_state = 2;
        }
    }

    // 0x00496215: steering. Note the intermediate mask, which the original applies
    // between the two turn terms.
    {
        unsigned int a = ((unsigned int)(g_PlayerDpadHeld & 2) * 0x18
                          + (unsigned int)g_playerEntity.directionAngle) & 0xfff;
        a = (a + (unsigned int)((int)(g_PlayerDpadHeld & 8) * -6)) & 0xfff;
        g_playerEntity.directionAngle = (short)a;
    }

    switch (g_playerEntity.action_state) {
    case 0:
        g_playerEntity.unk_bf              = 1;
        g_playerEntity.attackAnim          = 3;
        g_playerEntity.action_state        = 1;
        g_playerEntity.unk_8c              = 3;
        g_playerEntity.isBeingAttackedFlag = 0;
        g_playerEntity.move_speed_current  = 0xd2;
        // The original parks the old frame id in the g_playerDisplacement scratch
        // global; kept as-is because it is a shared temp.
        g_playerDisplacement = (int)g_playerEntity.animation_frame_id;
        g_playerEntity.animation_frame_id = 1;
        if (g_playerDisplacement > 9 && g_playerDisplacement < 0x19) {
            g_playerEntity.animation_frame_id = 0xc;
        }
        g_playerEntity.attackDirection = 0;
        if ((g_main_state_flags2 & 1) != 0) {
            PlayEntitySnd(1);
        }
        // fall through
    case 1: {
        if (g_playerEntity.attackDirection == 0) {
            if (g_playerEntity.animation_frame_id == 0x00) { PlayEntitySnd(1); }
            if (g_playerEntity.animation_frame_id == 0x0a) { PlayEntitySnd(1); }
        }
        short prevCounter = (short)g_playerEntity.attackDirection;
        g_playerEntity.move_speed_current = 0xd2;
        if ((g_main_state_flags2 & 1) == 0) {
            Joint_move(0, g_playerEntity.jointMoveData0, g_playerEntity.jointMoveData1, 0x400);
            g_playerEntity.attackDirection = 0;
        } else {
            g_playerEntity.move_speed_current = 0x69;
            g_playerEntity.attackDirection--;
            if (prevCounter == 0) {
                Joint_move(0, g_playerEntity.jointMoveData0, g_playerEntity.jointMoveData1, 0x400);
                g_playerEntity.attackDirection = 1;
            }
        }
        // 0x004962fa: run modifier released -> hand back to the walk behaviour under
        // frame0, picking the walk frame that continues this stride.
        unsigned char frameBefore = g_playerEntity.animation_frame_id;
        if ((g_PlayerDpadHeld & 0x200) == 0) {
            g_playerEntity.animFrameId     = 0;
            g_playerEntity.action_behavior = 1;
            g_playerEntity.action_state    = 1;
            g_playerDisplacement = (int)g_playerEntity.animation_frame_id;
            g_playerEntity.animation_frame_id = 10;
            if (g_playerDisplacement != 0 && frameBefore < 0xc) {
                g_playerEntity.animation_frame_id = 0x19;
            }
            g_playerEntity.attackAnim = 2;
            g_playerEntity.unk_8c     = 3;
        }
        break;
    }
    case 2:
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf             = 0;
        g_playerEntity.attackAnim         = 0;
        g_playerEntity.action_state       = 3;
        g_playerEntity.unk_8c             = 3;
        g_playerEntity.unk_bc             = 0;
        // fall through
    case 3:
        Joint_move(0, g_playerEntity.animHeader, g_playerEntity.animBase, 0x400);
        g_playerEntity.unk_bc++;
        if (g_playerEntity.unk_bc > 3) {
            g_playerEntity.action_behavior    = 0;
            g_playerEntity.action_state       = 0;
            g_playerEntity.move_speed_current = 0;
            g_playerEntity.animFrameId        = 0;   // back to normal control
            PlayEntitySnd(1);
        }
        g_playerEntity.move_speed_current -= 0x1e;
        break;
    default:
        break;
    }

    Add_speedXZ(0);
}

// ============================================================================
// player_ctrl_frame1 (0x00495520) — animFrameId 1
// A bare table dispatch on action_behavior:
//   00495522: MOV AL,[0x00be636a]                  ; action_behavior
//   00495527: JMP dword ptr [EAX*0x4 + 0x4d456c]
// Unlike frame0 there is no input read and no caller-applied angle tweak - these
// are the locked-in actions, and each handler reads input itself if it needs to.
// ============================================================================
static void player_ctrl_frame1(void)
{
    switch (g_playerEntity.action_behavior) {
    case 0x0d:                       // 0x004d45a0 -> 0x00496110
        player_behavior_0d_run();
        return;
    case 10:                         // 0x004d4594 -> 0x00457390
    case 0x11:                       // 0x004d45b0 -> 0x00457390
        player_door_open_sequence();
        return;
    default:
        player_state_report_missing("action_behavior under animFrameId 1 (0x004d456c)");
        return;
    }
}
static void player_ctrl_frame2(void) { player_state_report_missing("animFrameId 2 -> 0x00495330"); }
static void player_ctrl_frame3(void) { player_state_report_missing("animFrameId 3 -> 0x00495530"); }
static void player_ctrl_frame4(void) { player_state_report_missing("animFrameId 4 -> 0x004955e0"); }

// ============================================================================
// Player state 8 (0x0044cf30) — the SCD-driven animation state.
//
// This is how a cutscene animates the player. Event opcodes 0x83/0x84/0x85 write
// state 8 plus an action_behavior straight into the entity (see
// scd_event_state1_anim), and this state runs the matching handler from the table
// at 0x004beca0 until the animation completes and the script moves on. With state
// 8 unimplemented the player froze in whatever pose the script had just set and
// the event VM waited on an animation that never advanced.
//
// Handlers 0, 1, 5, 7, 8 and 9 are transcribed. 2, 3, 4 and 6 are not yet, and
// log their address rather than sitting NULL.
// ============================================================================

extern void Flg_on(int baseAddr, unsigned int bitIndex);                          // 0x00473ef0
extern void entity_rotate_toward_target(VECTOR* pos, unsigned short angleStep);    // 0x004899b0
extern int  turn_toward_target(VECTOR* pos, short angleStep);                       // 0x00489b?0
extern void entity_apply_walk_speed(short speed);                                   // 0x0047a4f0

// Shorthand for the recurring Joint_move first argument. The original computes it
// with a CONCAT31/AND 0xffffff01 dance that Ghidra cannot fold; all it amounts to
// is bit 0 of unk_e0 (the SCD "mirror this animation" flag).
static inline int player_joint_mirror(void)
{
    return (int)(g_playerEntity.unk_e0 & 1);
}

// 0x0044cf80 — behavior 0: plain animation playback. action_state 2-5 pick which
// of the four joint-data pairs to finish on, reload the equipped weapon's
// animation and drop into the terminal state 6.
static void player_scd_behavior_00(void)
{
    switch (g_playerEntity.action_state) {
    case 0:
        g_playerEntity.action_state       = 1;
        g_playerEntity.unk_8c             = 3;
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf             = 0;
        g_playerEntity.attackAnim         = 0;
        // fall through
    case 1:
        Joint_move(player_joint_mirror(), g_playerEntity.animHeader,
                   g_playerEntity.animBase, 0x400);
        return;
    case 2:
        LoadEquippedWeaponAnimation(g_playerEntity.equippedWeaponId, 0xe,
                                    (unsigned int)(unsigned int*)g_animationBuffer,
                                    (unsigned int)(unsigned int*)g_animObjectBuffer);
        g_playerEntity.action_state = 6;
        Joint_move(0, g_playerEntity.animHeader, g_playerEntity.animBase, 0x400);
        return;
    case 3:
        LoadEquippedWeaponAnimation(g_playerEntity.equippedWeaponId, 0xe,
                                    (unsigned int)(unsigned int*)g_animationBuffer,
                                    (unsigned int)(unsigned int*)g_animObjectBuffer);
        g_playerEntity.action_state = 6;
        Joint_move(0, g_playerEntity.jointMoveData0, g_playerEntity.jointMoveData1, 0x400);
        return;
    case 4:
        LoadEquippedWeaponAnimation(g_playerEntity.equippedWeaponId, 0xe,
                                    (unsigned int)(unsigned int*)g_animationBuffer,
                                    (unsigned int)(unsigned int*)g_animObjectBuffer);
        g_playerEntity.action_state = 6;
        Joint_move(0, g_playerEntity.jointMoveData2, g_playerEntity.jointMoveData3, 0x400);
        return;
    case 5:
        LoadEquippedWeaponAnimation(g_playerEntity.equippedWeaponId, 0xe,
                                    (unsigned int)(unsigned int*)g_animationBuffer,
                                    (unsigned int)(unsigned int*)g_animObjectBuffer);
        g_playerEntity.action_state = 6;
        Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400);
        return;
    default:
        return;
    }
}

// 0x0044d100 — behavior 1: remapped animation playback. attackAnim carries the
// script's animation index; below 0x10 it goes through g_ScdAnimRemap, 0x3e and up
// selects the fourth joint-data pair. States 1-4 each advance a different pair and
// converge on 5, which raises the script's completion flag.
static void player_scd_behavior_01(void)
{
    short entryDir = (short)g_playerEntity.attackDirection;
    char done;

    switch (g_playerEntity.action_state) {
    case 0:
        g_playerEntity.unk_8c             = 7;
        g_playerEntity.move_speed_current = 0;
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf             = 0;
        if ((g_playerEntity.unk_e0 & 0x20) != 0) {
            g_playerEntity.unk_8c = 0;
        }
        if (g_playerEntity.attackAnim < 0x3e) {
            if (g_playerEntity.attackAnim < 0x10) {
                unsigned int idx = g_playerEntity.attackAnim;
                g_playerEntity.attackAnim   = g_ScdAnimRemap[idx * 2 + 1];
                g_playerEntity.action_state = g_ScdAnimRemap[idx * 2] + 1;
            } else {
                g_playerEntity.action_state = 3;
            }
        } else {
            g_playerEntity.action_state = 4;
            g_playerEntity.attackAnim   = g_playerEntity.attackAnim - 0x3e;
        }
        g_playerEntity.attackDirection = 1;
        return;

    case 1:
        // unk_e0 bit 7 makes the animation hold for one extra frame per step
        if (((g_playerEntity.unk_e0 & 0x80) != 0) &&
            (g_playerEntity.attackDirection--, entryDir == 0)) {
            g_playerEntity.attackDirection = 1;
            return;
        }
        done = Joint_move(player_joint_mirror(), g_playerEntity.animHeader,
                          g_playerEntity.animBase, 0x200);
        if (done != 0) {
            g_playerEntity.action_state = 5;
            g_playerEntity.directionAngle += (short)g_playerEntity.unk_de;
        }
        return;

    case 2:
        if (((g_playerEntity.unk_e0 & 0x80) != 0) &&
            (g_playerEntity.attackDirection--, entryDir == 0)) {
            g_playerEntity.attackDirection = 1;
            return;
        }
        done = Joint_move(player_joint_mirror(), g_playerEntity.jointMoveData0,
                          g_playerEntity.jointMoveData1, 0x200);
        if (done != 0) {
            g_playerEntity.action_state = 5;
            g_playerEntity.attackAnim   = g_playerEntity.attackAnim + 5;
            g_playerEntity.directionAngle += (short)g_playerEntity.unk_de;
        }
        return;

    case 3:
        if (((g_playerEntity.unk_e0 & 0x80) != 0) &&
            (g_playerEntity.attackDirection--, entryDir == 0)) {
            g_playerEntity.attackDirection = 1;
            return;
        }
        done = Joint_move(player_joint_mirror(), g_playerEntity.jointMoveData2,
                          g_playerEntity.jointMoveData3, 0x200);
        if (done != 0) {
            g_playerEntity.action_state = 5;
            g_playerEntity.directionAngle += (short)g_playerEntity.unk_de;
        }
        return;

    case 4:
        if (((g_playerEntity.unk_e0 & 0x80) != 0) &&
            (g_playerEntity.attackDirection--, entryDir == 0)) {
            g_playerEntity.attackDirection = 1;
            return;
        }
        done = Joint_move(player_joint_mirror(), g_playerEntity.emdScratchPtr1,
                          g_playerEntity.emdScratchPtr2, 0x200);
        if (done != 0) {
            g_playerEntity.action_state = 5;
            g_playerEntity.attackAnim   = g_playerEntity.attackAnim + 0x3e;
            g_playerEntity.directionAngle += (short)g_playerEntity.unk_de;
        }
        return;

    case 5:
        // Raise the completion flag the script is waiting on, then loop back to
        // state 0 when unk_e0 bit 4 asks for a repeat.
        Flg_on((int)g_SysFlags, g_playerEntity.unk_db);
        g_playerEntity.unk_de = 0;
        if ((g_playerEntity.unk_e0 & 0x10) != 0) {
            g_playerEntity.action_state = 0;
        }
        return;

    default:
        return;
    }
}

// 0x0044dac0 — behavior 5: walk to the scripted destination in unk_c6/unk_c8.
// Faces 180 degrees away while calling entity_rotate_toward_target (the helper
// steers toward a point, so the angle is flipped either side of the call to make
// it walk backwards-facing), then stops once within 100 units.
static void player_scd_behavior_05(void)
{
    if (g_playerEntity.action_state == 0) {
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf             = 0;
        g_playerEntity.action_state       = 1;
        g_playerEntity.unk_8c             = 3;
        g_playerEntity.attackAnim         = 2;
        g_playerEntity.move_speed_current = 0x1d;
    }
    else if (g_playerEntity.action_state != 1) {
        return;
    }

    // Footstep sound on the two contact frames
    if (((g_playerEntity.animation_frame_id == 7) ||
         (g_playerEntity.animation_frame_id == 0x1b)) &&
        (g_playerEntity.unk_bf == 2)) {
        // PlayEntitySnd takes ONE parameter (0x0047fbf0). The call sites push a
        // second dword (0 or -4) that the callee never reads - a dead push, the
        // same pattern as rotate_entity's fourth argument.
        PlayEntitySnd(0);
    }

    g_playerPosScratch.x = (int)g_playerEntity.unk_c6;
    g_playerPosScratch.z = (int)g_playerEntity.unk_c8;
    g_playerPosScratch.y = 0;

    g_playerEntity.directionAngle = (short)((g_playerEntity.directionAngle + 0x800) & 0xfff);
    entity_rotate_toward_target(&g_playerPosScratch, g_playerEntity.unk_de);
    g_playerEntity.directionAngle = (short)(g_playerEntity.directionAngle - 0x800);

    Joint_move(player_joint_mirror(), g_playerEntity.animHeader,
               g_playerEntity.animBase, 0x400);
    Add_speedXZ(0x800);

    int dz = g_playerEntity.scaMatrixData.localMatrix.t[2] - (int)g_playerEntity.unk_c8;
    int dx = g_playerEntity.scaMatrixData.localMatrix.t[0] - (int)g_playerEntity.unk_c6;
    if (SquareRoot0(dz * dz + dx * dx) < 100) {
        Flg_on((int)g_SysFlags, g_playerEntity.unk_db);
        if ((g_playerEntity.healthStatusFlags & 0x80) == 0) {
            // 0x0044dc35: MOV dword ptr [EAX+0x84],1 - back to state 1 with
            // animFrameId, action_behavior and action_state all cleared
            g_playerEntity.animationId     = 1;
            g_playerEntity.animFrameId     = 0;
            g_playerEntity.action_behavior = 0;
            g_playerEntity.action_state    = 0;
        }
    }
}

// 0x0044dd60 — behavior 7: play the second joint-data pair once, applying the
// per-frame turn from unk_de every frame including after completion.
static void player_scd_behavior_07(void)
{
    unsigned char st = g_playerEntity.action_state;
    if (st == 0) {
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf             = 0;
        g_playerEntity.attackAnim         = g_playerEntity.attackAnim - 5;
        g_playerEntity.action_state       = 1;
        g_playerEntity.unk_8c             = 3;
        st = 1;
    }
    if (st == 1) {
        g_playerEntity.action_state += (unsigned char)Joint_move(
            player_joint_mirror(), g_playerEntity.jointMoveData0,
            g_playerEntity.jointMoveData1, 0x400);
    }
    else if (st == 2) {
        Flg_on((int)g_SysFlags, g_playerEntity.unk_db);
    }
    g_playerEntity.directionAngle += (short)g_playerEntity.unk_de;
}

// 0x0044de10 — behavior 8: same as 7 but never mirrored and with no turn.
static void player_scd_behavior_08(void)
{
    unsigned char st = g_playerEntity.action_state;
    if (st == 0) {
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf             = 0;
        g_playerEntity.attackAnim         = g_playerEntity.attackAnim - 5;
        g_playerEntity.action_state       = 1;
        g_playerEntity.unk_8c             = 3;
    }
    else if (st != 1) {
        if (st == 2) {
            Flg_on((int)g_SysFlags, g_playerEntity.unk_db);
        }
        return;
    }
    g_playerEntity.action_state += (unsigned char)Joint_move(
        0, g_playerEntity.jointMoveData0, g_playerEntity.jointMoveData1, 0x400);
}

// 0x0044deb0 — behavior 9: as 8 but always mirrored, and on completion it hands
// the player back to state 1.
static void player_scd_behavior_09(void)
{
    unsigned char st = g_playerEntity.action_state;
    if (st == 0) {
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf             = 0;
        g_playerEntity.attackAnim         = g_playerEntity.attackAnim - 5;
        g_playerEntity.action_state       = 1;
        g_playerEntity.unk_8c             = 3;
    }
    else if (st != 1) {
        if (st != 2) {
            return;
        }
        g_playerEntity.animationId     = 1;
        g_playerEntity.animFrameId     = 0;
        g_playerEntity.action_behavior = 0;
        g_playerEntity.action_state    = 0;
        Flg_on((int)g_SysFlags, g_playerEntity.unk_db);
        return;
    }
    g_playerEntity.action_state += (unsigned char)Joint_move(
        1, g_playerEntity.jointMoveData0, g_playerEntity.jointMoveData1, 0x400);
}

static void player_scd_report(const char* addr)
{
    static const char* lastReported = NULL;
    if (addr != lastReported) {
        lastReported = addr;
        dbg_printf("[player] unimplemented SCD behavior %s (attackAnim=%u frame=%u action=%u)\n",
               addr, (unsigned int)g_playerEntity.attackAnim,
               (unsigned int)g_playerEntity.animation_frame_id,
               (unsigned int)g_playerEntity.action_state);
    }
}

// 0x0044d380 - behaviour 2: turn toward the scripted target, then walk to it.
// The player twin of npc_scd_02: state 1 turns on the spot until aligned within
// 0x16a, state 3 walks (anim 2, footsteps on frames 8 and 0x16) and finishes
// within 150 units.
//
// Note the completion ordering: the original evaluates
// `dist < 0x96 && (Flg_on(...), (healthStatusFlags & 0x80) == 0)`, so Flg_on fires
// on every frame the player is within range, while the return to state 1 happens
// only when the health flag is clear. Reproduced with the same sequencing.
static void player_scd_behavior_02(void)
{
    switch (g_playerEntity.action_state) {
    case 0:
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf             = 0;
        g_playerEntity.attackAnim         = 2;
        g_playerEntity.action_state       = 1;
        g_playerEntity.unk_8c             = 3;
        // fall through
    case 1:
        g_playerPosScratch.x = (int)g_playerEntity.unk_c6;
        g_playerPosScratch.z = (int)g_playerEntity.unk_c8;
        g_playerPosScratch.y = 0;
        g_playerEntity.directionAngle += (short)turn_toward_target(
            &g_playerPosScratch, (short)g_playerEntity.unk_de);
        Joint_move(player_joint_mirror(), g_playerEntity.jointMoveData0,
                   g_playerEntity.jointMoveData1, 0x400);
        if ((short)turn_toward_target(&g_playerPosScratch, 0x16a) == 0) {
            g_playerEntity.action_state = 2;
        }
        break;

    case 2:
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf             = 0;
        g_playerEntity.attackAnim         = 2;
        g_playerEntity.action_state       = 3;
        g_playerEntity.unk_8c             = 3;
        // fall through
    case 3:
        // PlayEntitySnd takes ONE parameter (0x0047fbf0). The call sites push a
        // second dword (0 or -4) that the callee never reads - a dead push, the
        // same pattern as rotate_entity's fourth argument.
        if (g_playerEntity.animation_frame_id == 8)    PlayEntitySnd(0);
        if (g_playerEntity.animation_frame_id == 0x16) PlayEntitySnd(0);

        entity_apply_walk_speed(0x5d);

        g_playerPosScratch.x = (int)g_playerEntity.unk_c6;
        g_playerPosScratch.z = (int)g_playerEntity.unk_c8;
        g_playerPosScratch.y = 0;
        entity_rotate_toward_target(&g_playerPosScratch, g_playerEntity.unk_de);
        Joint_move(player_joint_mirror(), g_playerEntity.jointMoveData0,
                   g_playerEntity.jointMoveData1, 0x400);
        Add_speedXZ(0);
        {
            int dz = g_playerEntity.scaMatrixData.localMatrix.t[2] - (int)g_playerEntity.unk_c8;
            int dx = g_playerEntity.scaMatrixData.localMatrix.t[0] - (int)g_playerEntity.unk_c6;
            if (SquareRoot0(dz * dz + dx * dx) < 0x96) {
                Flg_on((int)g_SysFlags, g_playerEntity.unk_db);
                if ((g_playerEntity.healthStatusFlags & 0x80) == 0) {
                    g_playerEntity.animationId     = 1;
                    g_playerEntity.animFrameId     = 0;
                    g_playerEntity.action_behavior = 0;
                    g_playerEntity.action_state    = 0;
                }
            }
        }
        break;
    }
}
// 0x0044d5e0 - behaviour 3: turn toward the scripted destination, run to it, then
// decelerate to a stop. This is the "Chris runs forward" beat of the intro.
//   state 1  turn on the spot until aligned within 0x16a
//   state 3  run (anim 3, speed 0xd2), footstep on frames 0 and 10, until within
//            250 units of the target
//   state 5  four frames of anim 0 shedding 0x1e speed each, then
//   state 6  hand the player back to state 1 and raise the script's flag
// healthStatusFlags bit 7 short-circuits the deceleration and finishes at once.
static void player_scd_behavior_03(void)
{
    switch (g_playerEntity.action_state) {
    case 0:
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf             = 0;
        g_playerEntity.attackAnim         = 2;
        g_playerEntity.action_state       = 1;
        g_playerEntity.unk_8c             = 3;
        // fall through
    case 1:
        g_playerPosScratch.x = (int)g_playerEntity.unk_c6;
        g_playerPosScratch.z = (int)g_playerEntity.unk_c8;
        g_playerPosScratch.y = 0;
        g_playerEntity.directionAngle += (short)turn_toward_target(
            &g_playerPosScratch, (short)g_playerEntity.unk_de);
        Joint_move(player_joint_mirror(), g_playerEntity.jointMoveData0,
                   g_playerEntity.jointMoveData1, 0x400);
        if ((short)turn_toward_target(&g_playerPosScratch, 0x16a) == 0) {
            g_playerEntity.action_state = 2;
        }
        break;

    case 2:
        g_playerEntity.move_speed_current = 0xd2;
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf             = 0;
        g_playerEntity.attackAnim         = 3;
        g_playerEntity.action_state       = 3;
        g_playerEntity.unk_8c             = 3;
        // fall through
    case 3:
        // PlayEntitySnd takes ONE parameter (0x0047fbf0). The call sites push a
        // second dword (0 or -4) that the callee never reads - a dead push, the
        // same pattern as rotate_entity's fourth argument.
        if (g_playerEntity.animation_frame_id == 0)  PlayEntitySnd(1);
        if (g_playerEntity.animation_frame_id == 10) PlayEntitySnd(1);

        g_playerPosScratch.x = (int)g_playerEntity.unk_c6;
        g_playerPosScratch.z = (int)g_playerEntity.unk_c8;
        g_playerPosScratch.y = 0;
        entity_rotate_toward_target(&g_playerPosScratch, g_playerEntity.unk_de);
        Joint_move(player_joint_mirror(), g_playerEntity.jointMoveData0,
                   g_playerEntity.jointMoveData1, 0x400);
        Add_speedXZ(0);
        {
            int dz = g_playerEntity.scaMatrixData.localMatrix.t[2] - (int)g_playerEntity.unk_c8;
            int dx = g_playerEntity.scaMatrixData.localMatrix.t[0] - (int)g_playerEntity.unk_c6;
            int dist = SquareRoot0(dz * dz + dx * dx);

            // DIAGNOSTIC: the run target and the closing distance. If dist stops
            // shrinking, or the target is not a sane room coordinate, the run
            // overshoots and the player leaves the room. Remove once verified.
            {
                static int lastDist = -1;
                if (lastDist < 0 || dist > lastDist || (lastDist - dist) > 64) {
                    dbg_printf("[run] pos=%d,%d target=%u,%u dist=%d speed=%d ang=%d\n",
                               g_playerEntity.scaMatrixData.localMatrix.t[0],
                               g_playerEntity.scaMatrixData.localMatrix.t[2],
                               (unsigned int)g_playerEntity.unk_c6,
                               (unsigned int)g_playerEntity.unk_c8,
                               dist, (int)(short)g_playerEntity.move_speed_current,
                               (int)g_playerEntity.directionAngle);
                }
                lastDist = dist;
            }

            if (dist < 0xfa) {
                if ((g_playerEntity.healthStatusFlags & 0x80) == 0) {
                    g_playerEntity.action_state = 4;
                } else {
                    Flg_on((int)g_SysFlags, g_playerEntity.unk_db);
                }
                return;
            }
        }
        break;

    case 4:
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf             = 0;
        g_playerEntity.attackAnim         = 0;
        g_playerEntity.action_state       = 5;
        g_playerEntity.unk_8c             = 3;
        g_playerEntity.attackDirection    = 0;   // +0xc4, the frame counter
        // fall through
    case 5:
        Joint_move(player_joint_mirror(), g_playerEntity.animHeader,
                   g_playerEntity.animBase, 0x400);
        g_playerEntity.attackDirection = (unsigned short)(g_playerEntity.attackDirection + 1);
        if ((short)g_playerEntity.attackDirection > 3) {
            g_playerEntity.action_state = 6;
        }
        *(short*)&g_playerEntity.move_speed_current -= 0x1e;
        Add_speedXZ(0);
        break;

    case 6:
        // 0x0044d8??: MOV dword ptr [.. + 0x84],1 - back to state 1 with
        // animFrameId, action_behavior and action_state cleared
        g_playerEntity.animationId     = 1;
        g_playerEntity.animFrameId     = 0;
        g_playerEntity.action_behavior = 0;
        g_playerEntity.action_state    = 0;
        Flg_on((int)g_SysFlags, g_playerEntity.unk_db);
        return;
    }
}
static void player_scd_behavior_04(void) { player_scd_report("0x0044d930"); }
// 0x0044dc50 - behaviour 6: turn in place toward the scripted target. Rotates at
// a fixed 0x38 per frame and finishes when turn_toward_target reports the
// remaining angle closed at the script's own step (unk_de), then hands the player
// back to state 1.
static void player_scd_behavior_06(void)
{
    unsigned char st = g_playerEntity.action_state;
    if (st == 0) {
        g_playerEntity.animation_frame_id = 0;
        g_playerEntity.unk_bf             = 0;
        g_playerEntity.attackAnim         = 2;
        g_playerEntity.action_state       = 1;
        g_playerEntity.unk_8c             = 3;
    }
    else if (st != 1) {
        if (st != 2) {
            return;
        }
        g_playerEntity.animationId     = 1;
        g_playerEntity.animFrameId     = 0;
        g_playerEntity.action_behavior = 0;
        g_playerEntity.action_state    = 0;
        Flg_on((int)g_SysFlags, g_playerEntity.unk_db);
        return;
    }

    g_playerPosScratch.x = (int)g_playerEntity.unk_c6;
    g_playerPosScratch.z = (int)g_playerEntity.unk_c8;
    g_playerPosScratch.y = 0;
    entity_rotate_toward_target(&g_playerPosScratch, 0x38);
    Joint_move(player_joint_mirror(), g_playerEntity.jointMoveData0,
               g_playerEntity.jointMoveData1, 0x400);
    if ((short)turn_toward_target(&g_playerPosScratch,
                                  (short)g_playerEntity.unk_de) == 0) {
        g_playerEntity.action_state = 2;
    }
}

// 0x004beca0 — 10 entries, indexed by action_behavior. The run of pointers ends
// at 0x004becc8 where string data begins.
static void* const g_playerScdBehaviors[10] = {
    (void*)player_scd_behavior_00,  // 0x0044cf80
    (void*)player_scd_behavior_01,  // 0x0044d100
    (void*)player_scd_behavior_02,  // 0x0044d380
    (void*)player_scd_behavior_03,  // 0x0044d5e0
    (void*)player_scd_behavior_04,  // 0x0044d930
    (void*)player_scd_behavior_05,  // 0x0044dac0
    (void*)player_scd_behavior_06,  // 0x0044dc50
    (void*)player_scd_behavior_07,  // 0x0044dd60
    (void*)player_scd_behavior_08,  // 0x0044de10
    (void*)player_scd_behavior_09,  // 0x0044deb0
};

// 0x0044cf30 — player state 8. Mirrors npc_state8_action_update exactly: run the
// handler, run it a second time when unk_e0 bit 1 asks for a double step, then
// refresh the held weapon's joint when bit 2 is set (bit 3 picks the hand).
// Note the handler is re-read from action_behavior on the second call - a handler
// that changes action_behavior redirects its own repeat.
static void player_state_08(void)
{
    unsigned char behavior = g_playerEntity.action_behavior;
    if (behavior >= 10) {
        player_scd_report("action_behavior out of range");
        return;
    }

    ((void(*)(void))g_playerScdBehaviors[behavior])();
    if ((g_playerEntity.unk_e0 & 2) != 0) {
        ((void(*)(void))g_playerScdBehaviors[g_playerEntity.action_behavior % 10])();
    }
    if ((g_playerEntity.unk_e0 & 4) != 0) {
        EntityUpdateWeaponJoint((g_playerEntity.unk_e0 & 8) >> 3);
    }
}

// 0x004d4550 — indexed by g_playerEntity.animationId.
//
// TEN entries, not sixteen. The table ends at 0x004d4577, because the animFrameId
// jump table used by player_state_01_control begins at 0x004d4578 (see
// g_playerCtrlFrameFunctions). The previous 16-entry declaration ran past the end
// and listed five animFrameId handlers as `player_state_10..14`; with the old
// `animationId & 0x0f` mask an animationId of 10-15 would have called an
// animFrameId handler as though it were a player state.
//
// The original applies neither a mask nor a bound:
//   00494da5: MOV AL,[0x00be6368]                  ; animationId
//   00494daa: CALL dword ptr [EAX*0x4 + 0x4d4550]
// animationId only ever reaches 8 in practice, so the bound added at the call site
// is port-only safety rather than a behaviour change.
static void* const g_playerStateFunctions[10] = {
    /* 0x0 */ (void*)player_state_init,           // 0x00494eb0
    /* 0x1 */ (void*)player_state_01_control,     // 0x00495180
    /* 0x2 */ (void*)player_state_02,             // 0x00495250
    /* 0x3 */ (void*)player_state_03,             // 0x00495270 -> FUN_00459be0
    /* 0x4 */ (void*)player_state_block_input,    // 0x00495280
    /* 0x5 */ (void*)player_state_anim_window0,   // 0x00495290
    /* 0x6 */ (void*)player_state_anim_window1,   // 0x004952d0
    /* 0x7 */ (void*)player_state_anim_window2,   // 0x00495310
    /* 0x8 */ (void*)player_state_08,             // 0x0044cf30
    /* 0x9 */ (void*)player_state_null,           // NULL in the original
};

// ============================================================================
// update_player_anim (0x00494d90)
// Per-frame player update: run the current state, resolve collisions, refresh
// the camera-zone membership flag, then the lighting and effect passes.
// ============================================================================
void update_player_anim(void)
{
    // 0x00494d95: aim the global current-entity pointer at the player
    ENTITY = (Entity*)&g_playerEntity;

    // 0x00494da1: only run the state machine when input/messages allow it
    if ((g_message_flags & 1) != 0) {
        // The original is an unmasked, unbounded CALL through the table. Bound it
        // to the real 10 entries instead of masking with 0x0f, which used to fold
        // 10-15 onto 0-5 and would now read past the array.
        if (g_playerEntity.animationId < 10) {
            ((void(*)(void))g_playerStateFunctions[g_playerEntity.animationId])();
        } else {
            player_state_report_missing("animationId >= 10, past 0x004d4550");
        }
        EntityUpdateLookAtAngles();
    }

    SetEntityScaHitData((Entity*)&g_playerEntity);

    // 0x00494dc4: skip collision in state 5 or behavior 0x11
    if ((g_playerEntity.animationId != 5) && (g_playerEntity.action_behavior != 0x11)) {
        HandleEnemyPlayerCollisions();
        check_room_collision((VECTOR*)g_playerEntity.scaMatrixData.localMatrix.t,
                             *(short*)(g_playerEntity.Sca_info + 10));
    }

    g_playerEntity.scaMatrixData.field_00 = 0;

    // 0x00494e11: bit 0 of unk_03 = player is inside the current camera zone
    g_playerEntity.unk_03 &= 0xfe;
    g_playerEntity.unk_03 |= (unsigned char)is_entity_in_switch_zone(
        (VECTOR*)g_playerEntity.scaMatrixData.localMatrix.t, g_CurrentRdtDataTypePtr);

    if ((((g_playerEntity.unk_03 & 0x7f) != 0) &&
         (((unsigned char)g_playerEntity.unk_e0 & 0x40) == 0)) ||
        ((g_stageId == 3) && (g_roomId == 0xd))) {
        player_update_shadow_sprite((int)g_playerEntity.scaMatrixData.localMatrix.t,
                                    (int)&g_playerEntity.pushVelocity,
                                    (int)g_playerEntity.unk_8e,
                                    (int)g_playerEntity.directionAngle);
    }

    // 0x00494e73: recompute lighting when the joint-animation flag is set
    if ((g_main_state_flags & 1) != 0) {
        unsigned char lit = FUN_0048bd00(
            (void*)((int)g_RdtPointer[1].lights + (unsigned int)g_roomCameraId * 0x2c - 4),
            (unsigned char)((g_main_state_flags >> 1) & 1),
            (int)g_playerEntity.scaMatrixData.localMatrix.t);
        if (lit != 0) {
            FUN_0048bda0();
        }
    }

    player_update_detached_joint();
}

// ============================================================================
// FUN_0041b3c0 (0x0041b3c0)
// Is `pos` inside the axis-aligned box at `zone`? The zone is four uint16s:
// x, z, width, depth. The original compares UNSIGNED, so a coordinate left of or
// above the box wraps to a huge value and fails the <= test — that is what makes
// one comparison per axis sufficient. Reproduced deliberately.
// ============================================================================
static int is_point_in_action_zone(VECTOR* pos, unsigned short* zone) // 0x0041b3c0
{
    if (((unsigned int)(pos->x - (unsigned int)zone[0]) <= (unsigned int)zone[2]) &&
        ((unsigned int)(pos->z - (unsigned int)zone[1]) <= (unsigned int)zone[3])) {
        return 1;
    }
    return 0;
}

// ============================================================================
// no_room_action (0x0041c050) — room_check_actions[0]
// Literally `return 0`. It exists so slot 0 is a valid, inert handler rather than a
// null entry the original would have jumped through.
// ============================================================================
int no_room_action(unsigned char* entry)
{
    (void)entry;
    return 0;
}

// ============================================================================
// door_try_enter (0x0041b400) — room_check_actions[1]
//
// Ghidra called this use_mansion_key, after one of the messages it emits (0xc3).
// It is really the whole door interaction: reject, transition, or unlock.
//
// Called every frame while the player's reach probe is inside the door's action
// zone - there is NO action-button check. Confirmed by the two call sites of
// update_player_position: game_loop passes mask 1 and update_sounds passes mask 4,
// so the entry flag byte is a mask *selector*, and a door with bit 0 set is tested
// every frame. Walking into the zone is the trigger.
//
// The door record is at entry+8. Its byte +0xC is the lock descriptor: bit 0x80 =
// locked, bit 0x40 = restricted to one character, low 6 bits = the lock's flag
// index in g_LocksFlags. Byte +0x16 is the item id required to unlock it.
// ============================================================================
extern int          get_item_slot(unsigned char itemId);
extern unsigned int Flg_ck(int baseAddr, unsigned int bitIndex);   // 0x00473f40
extern void         Flg_on(int baseAddr, unsigned int bitIndex);   // 0x00473ef0

// 0x0041b575: "it's locked" family. The index is biased by 200 into the message
// table, and a lock click plays first.
static void door_locked_message(unsigned int msgIndex)
{
    play_sfx(2, 0x14, 0);
    set_message_display(msgIndex + 200, 0xff);
}

// 0x0041b59c: the transition. An instant full-screen black rect, one frame of
// sleep, then StMask - not a fade. This is why the in-game door cut is instant.
static void door_begin_transition(unsigned char* record)
{
    // DIAGNOSTIC - remove once the door transition is confirmed. This is the exact
    // moment the room change is committed; if this never prints, the door was never
    // accepted, and if it prints but no room loads the fault is downstream in
    // room_transition_load / the g_openMenuFlag handoff.
    dbg_printf("[door] BEGIN TRANSITION rec=%p dest=%02X cam=%02X msf=%08X openMenu=%d\n",
               record, (unsigned int)record[0x0d],
               (unsigned int)(record[0x0b] & 0x3f),
               (unsigned int)g_main_state_flags, (int)g_openMenuFlag);

    g_pendingDoorRecord = (int)record;          // the room the transition will load
    g_main_state_flags |= 0x2000000;
    g_message_flags = 0;

    g_rect.textureId = 0;
    g_rect.x = -160;
    g_rect.y = -120;
    g_rect.w = 320;
    g_rect.h = 240;
    g_rect.r = 0;
    g_rect.g = 0;
    g_rect.b = 0;

    // NOTE: the original writes a byte at 0x00be961b, which Ghidra labels
    // g_openMenuFlag. The port declares g_openMenuFlag at 0x00d22760 instead, so one
    // of the two addresses is wrong. Writing the port's symbol because that is the
    // menu/transition state machine the port actually implements - but if the screen
    // goes black and nothing advances, this is the first thing to check.
    g_openMenuFlag = 1;

    draw_rect(&g_rect, 0, 0);
    Task_sleep(1);
    StMask(0, 0);
}

int door_try_enter(unsigned char* entry)
{
    // 0x0041b400: already mid-transition (climb/door), do nothing.
    if ((g_main_state_flags & 0x80) != 0) {
        // DIAGNOSTIC - remove once the door transition is confirmed.
        static int reported = 0;
        if (!reported) {
            reported = 1;
            dbg_printf("[door] try_enter bailed: msf&0x80 set (mid climb/door), msf=%08X\n",
                       (unsigned int)g_main_state_flags);
        }
        return 0;
    }

    unsigned char* record = *(unsigned char**)(entry + 8);
    unsigned char  lock   = record[0xc];

    // DIAGNOSTIC - remove once the door transition is confirmed. Reports the lock
    // descriptor once per distinct record so the log shows whether this door is
    // treated as open, character-barred, or needing an item.
    {
        static unsigned char* lastRecord = nullptr;
        if (record != lastRecord) {
            lastRecord = record;
            dbg_printf("[door] try_enter rec=%p lock=%02X need=%02X dest=%02X flags0B=%02X\n",
                       record, (unsigned int)lock, (unsigned int)record[0x16],
                       (unsigned int)record[0x0d], (unsigned int)record[0x0b]);
        }
    }

    // 0x0041b41a: some doors are barred for one of the two characters.
    if ((lock & 0x40) != 0 && (g_playerEntity.id & 3) == 3) {
        set_message_display(0xd6, 0xff);
        return 0;
    }

    // 0x0041b441: unlocked outright, or its lock flag is already raised.
    if ((lock & 0x80) == 0 || Flg_ck((int)g_LocksFlags, lock & 0x3f) != 0) {
        door_begin_transition(record);
        return 0;
    }

    // Locked: work out whether the player can open it.
    unsigned int need = record[0x16];

    if (need == 0x33 && (g_playerEntity.id & 3) == 1) {
        // 0x0041b474: Jill substitutes the lockpick for this key, but only once
        // she has it (g_PlayerFlags bit 0x7c).
        if (Flg_ck((int)&g_PlayerFlags, 0x7c) == 0) {
            door_locked_message(0xd);
            return 0;
        }
        g_selectedItemId = 0x31;
    } else if (need == 0xfe) {
        // 0x0041b4a5: opens only from the other side.
        set_message_display(0xd4, 0xff);
        play_sfx(2, 0x21, 0);
        Flg_on((int)g_LocksFlags, record[0xc] & 0x3f);
        return 0;
    } else if (need == 0xff) {
        door_locked_message(0xb);
        return 0;
    } else {
        // 0x0041b4d7: needs a specific item.
        if (get_item_slot(need) < 0) {
            unsigned int m = need - 0x33;
            if (m > 9) {
                m = 10;
            }
            door_locked_message(m);
            return 0;
        }
        g_eventItemUsedFlag = 1;
        g_selectedItemId = (unsigned char)need;
    }

    // 0x0041b4f5: the key turns. Note this only UNLOCKS - it does not transition.
    // The next frame's zone hit finds the lock flag raised and walks through.
    set_message_display(0xc3, 0xff);

    int sfxId;
    if (g_stageId == 4 && g_roomId == 5) {
        play_sfx(2, 0x17, 0);
        play_sfx(2, 0x18, 0);
        sfxId = 0x19;
    } else {
        sfxId = 0x22;
    }
    play_sfx(2, sfxId, 0);
    Flg_on((int)g_LocksFlags, record[0xc] & 0x3f);
    return 0;
}

// ============================================================================
// check_door (0x0041b6d0) — room_check_actions[5]
//
// Called from update_player_position when the player enters a door's action zone.
// It does not open anything itself: it records which side the player approached
// from, then raises the two flags the rest of the transition watches -
// has_enter_switch_zone bit 0x20 (which player_input_to_behavior turns into
// action_behavior 0x11) and g_main_state_flags2 bit 0x400000 (which the door
// animation consumes to pick its turn direction).
//
// The entry's +8 field is the 24-byte door record cmd_door_set stored. Its first
// u16 is the zone origin and its third is the zone width; comparing the player's X
// against origin + width/2 is what picks the approach side. The angle test bails out
// entirely when the player is facing the wrong way, leaving only the 0x20 flag set.
//
// Returns 0. cmd_room_action discards this, but update_player_position propagates
// it as its own return value.
// ============================================================================
int check_door(unsigned char* entry)
{
    // The original writes entity offsets 0x85 and 0xC4 through the generic ENTITY
    // pointer. On PlayerEntity those are animFrameId and attackDirection, but the
    // Entity struct names them differently, so they are addressed by offset rather
    // than retargeted onto a same-offset field with an unrelated name.
    unsigned char* ent   = (unsigned char*)ENTITY;
    short*         appr  = (short*)(ent + 0xC4);   // PlayerEntity::attackDirection
    unsigned char* afid  = ent + 0x85;             // PlayerEntity::animFrameId

    unsigned short* record = *(unsigned short**)(entry + 8);
    int playerX = ENTITY->scaMatrixData.localMatrix.t[0];
    short angle = (short)ENTITY->angle;

    bool sideResolved = false;

    if ((playerX - (int)record[0]) < (int)(unsigned int)(record[2] >> 1)) {
        if ((((int)angle + 0x400) & 0x800) == 0) {
            *appr = 1;
            sideResolved = true;
        }
    } else {
        if ((((int)angle - 0x400) & 0x800) == 0) {
            *appr = -1;
            sideResolved = true;
        }
    }

    if (sideResolved) {
        *afid = 0;                   // route into the frame-0/1 handlers
        g_message_flags &= 0xffbf;
        ENTITY->has_enter_switch_zone &= 0xef;
        // Bit 0x10 records that the door swings the opposite way, which the door
        // animation reads to pick attackAnim 0x35 instead of 0x33.
        if (((*appr >> 1) ^ *(unsigned short*)(entry + 2)) & 1) {
            ENTITY->has_enter_switch_zone |= 0x10;
        }
    }

    ENTITY->has_enter_switch_zone |= 0x20;
    g_main_state_flags2 |= 0x400000;
    return 0;
}

// ============================================================================
// update_player_position (0x0041c060)
// Despite the name this does not move the player: it tests the player against
// every entry of the room item/door event table and fires the matching
// room_check_actions handler. This is the door / item / examine interaction
// layer.
//
// Two probe points are used. Entries without flag 0x40 are tested against a
// point 600 units in front of the player (the reach probe, g_playerPosScratch);
// entries with 0x40 are tested against the player's actual position. Flag 0x80
// disables an entry. `mask` gates which entries participate this frame.
// ============================================================================
// Calls since the last action-zone hit; drives the [zone] diagnostic's rate limit.
static int g_zoneHitGap = 1000;

void update_player_position(PlayerEntity* ent, int mask)
{
    int actionResult = 0;
    g_zoneHitGap++;

    // 0x0041c060: build the reach probe 600 units ahead of the facing direction
    g_svecScratch.x = 600;
    g_svecScratch.z = 0;
    MovePlayerXZ(g_playerEntity.directionAngle, &g_svecScratch, &g_svecScratch);
    g_playerPosScratch.x = g_svecScratch.x + g_playerEntity.scaMatrixData.localMatrix.t[0];
    g_playerPosScratch.z = g_svecScratch.z + g_playerEntity.scaMatrixData.localMatrix.t[2];

    // 0x0041c0a8: clear the action-available bit
    ENTITY->has_enter_switch_zone &= 0xdf;

    // 0x0041c0b1: bail out when the event table is empty. The original compares
    // the head pointer against g_RoomItemEventTable - 1.
    unsigned char* head = (unsigned char*)g_RoomItemEventHead;
    if (head == NULL || head < g_RoomItemEventTable) {
        return;
    }

    unsigned char* entry = g_RoomItemEventTable;
    char index = 0;
    do {
        unsigned char flags = entry[1];
        if ((*entry != 0) && ((mask & flags) != 0) && ((~flags & 0x80) != 0)) {
            bool hit = false;
            if ((flags & 0x40) == 0) {
                if (is_point_in_action_zone((VECTOR*)&g_playerPosScratch,
                                            *(unsigned short**)(entry + 8)) != 0) {
                    DAT_00be9830 = (unsigned char)(index + 1);
                    hit = true;
                }
            } else {
                if (is_point_in_action_zone((VECTOR*)ent->scaMatrixData.localMatrix.t,
                                            *(unsigned short**)(entry + 8)) != 0) {
                    DAT_00be9831 = (unsigned char)(index + 1);
                    hit = true;
                }
            }

            // 0x0041c125: dispatch the room action handler
            if (hit) {
                // DIAGNOSTIC: report every zone hit, not just the missing-handler
                // case, and re-report when the entry changes. Pressing action at a
                // door does nothing if EITHER the probe never reaches the zone or
                // the handler is absent, and those need telling apart. Rate-limited
                // to one line per (entry, handler-present) transition.
                // Reported on a gap rather than on a key: the previous version keyed
                // on (entry, act), which never changes for a given door, so it fired
                // once and then suppressed every later hit - making continuous hits
                // look like a single one. g_zoneHitGap counts calls since the last
                // hit, so re-entering the zone reports again without spamming while
                // the player stands in it.
                {
                    if (g_zoneHitGap > 30) {
                        dbg_printf("[zone] HIT entry=%d act=%u flg=%02X handler=%s"
                                   " probe=%s pl=(%d,%d)\n",
                                   (int)index, (unsigned int)*entry,
                                   (unsigned int)flags,
                                   room_check_actions[*entry] ? "present" : "NULL",
                                   (flags & 0x40) ? "position" : "reach",
                                   g_playerPosScratch.x, g_playerPosScratch.z);
                    }
                    g_zoneHitGap = 0;
                }

                void* handler = room_check_actions[*entry];
                if (handler != NULL) {
                    actionResult = ((int(*)(unsigned char*))handler)(entry);
                }
            } else {
                // DIAGNOSTIC - remove once placement is confirmed. Reports how far the
                // tested point is from the zone, per axis, so "Chris stops short of the
                // door" becomes a number instead of a guess. Both comparisons in
                // is_point_in_action_zone are UNSIGNED, so a point below the origin
                // wraps to a huge value - the signed deltas printed here are what
                // actually tell you which side you are on.
                unsigned short* z = *(unsigned short**)(entry + 8);
                if (z != NULL) {
                    int px = (flags & 0x40) ? ent->scaMatrixData.localMatrix.t[0]
                                            : g_playerPosScratch.x;
                    int pz = (flags & 0x40) ? ent->scaMatrixData.localMatrix.t[2]
                                            : g_playerPosScratch.z;
                    int dx = px - (int)z[0];      // <0 = before the zone, >w = past it
                    int dz = pz - (int)z[1];
                    static int lastKey = -1;
                    static int missGap = 0;
                    int key = (int)index * 4 + ((dx >= 0 && dx <= (int)z[2]) ? 1 : 0)
                                             + ((dz >= 0 && dz <= (int)z[3]) ? 2 : 0);
                    if (++missGap > 90 || key != lastKey) {
                        missGap = 0;
                        lastKey = key;
                        dbg_printf("[zone] MISS entry=%d act=%u probe=%s pt=(%d,%d)"
                                   " zone=(%u..%u, %u..%u) dx=%d%s dz=%d%s\n",
                                   (int)index, (unsigned int)*entry,
                                   (flags & 0x40) ? "position" : "reach", px, pz,
                                   (unsigned int)z[0],
                                   (unsigned int)(z[0] + z[2]),
                                   (unsigned int)z[1],
                                   (unsigned int)(z[1] + z[3]),
                                   dx, (dx >= 0 && dx <= (int)z[2]) ? "(in)" : "(OUT)",
                                   dz, (dz >= 0 && dz <= (int)z[3]) ? "(in)" : "(OUT)");
                    }
                }
            }
        }
        entry += 12;
        index++;
    } while (entry <= head);

    (void)actionResult;
}
