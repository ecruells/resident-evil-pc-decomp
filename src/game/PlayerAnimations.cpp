// PlayerAnimations.cpp - Player animation state machines (decompiled from Ghidra)
#include "../Globals.h"
#include "../marni/MarniSystem.h"

// ============================================================================
// Player animation function stubs (populated into g_playerAnimFunctions by set_player_animations_functions)
// These are dispatched by FUN_00495290 based on g_playerEntity.animFrameId
// Full implementations to be decompiled from Ghidra.
// ============================================================================
// 0x00437a80
void player_anim_attack_recoil(void) {
    char cVar1;
    switch ((unsigned int)g_playerEntity.anim_87) {
    case 0:
        g_playerEntity.unk_be = 0;
        g_playerEntity.unk_bf = 0;
        g_playerEntity.unk_8c = 0;
        g_playerEntity.anim_87 = 1;
        g_playerEntity.transform.t[0] = (int)*(unsigned short*)((char*)ENTITY + 0xC6);
        g_playerEntity.transform.t[2] = (int)*(unsigned short*)((char*)ENTITY + 0xC8);
        Play3DSnd(3, 0, 0, (int)&g_playerEntity.transform.t);
        g_playerEntity.flags |= 2;
    case 1:
        entity_apply_anim_vertex((Entity*)&g_playerEntity, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2);
        cVar1 = Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400);
        if (cVar1 != 0) {
            g_playerEntity.anim_87 = 2;
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
        g_playerEntity.unk_be = 0;
        g_playerEntity.unk_bf = 0;
        g_playerEntity.anim_87 = 4;
    case 4:
        entity_apply_anim_vertex((Entity*)&g_playerEntity, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2);
        cVar1 = Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400);
        if (cVar1 != 0) {
            g_playerEntity.animationId = 1;
            g_playerEntity.animFrameId = 0;
            g_playerEntity.anim_86 = 0;
            g_playerEntity.anim_87 = 0;
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
    if (g_playerEntity.anim_87 > 1) return;
    if (g_playerEntity.anim_87 == 0) {
        g_playerEntity.anim_87 = 1;
        g_playerEntity.unk_c2 = 0;
        g_playerEntity.unk_be = 0;
        g_playerEntity.unk_bf = 0;
        g_playerEntity.unk_8c = 0;
        g_playerEntity.attackAnim = 2;
        Play3DSnd(3, 2, 0, (int)&g_playerEntity.transform.t);
        return;
    }
    if (g_playerEntity.unk_be == 0xf) {
        PlayEntitySnd(2);
    }
    if (g_playerEntity.health >= 0) {
        entity_apply_anim_vertex((Entity*)&g_playerEntity, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2);
    }
    cVar1 = Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x200);
    if (cVar1 != 0) {
        g_playerEntity.directionAngle += 0x800;
        g_playerEntity.anim_87++;
    }
}
// 0x00430130
void player_anim_multi_attack(void) {
    char cVar1;
    switch ((unsigned int)g_playerEntity.anim_87) {
    case 0:
        g_playerEntity.anim_87 = 1;
        g_playerEntity.unk_8c = 3;
        g_playerEntity.unk_c2 = 0;
        g_playerEntity.unk_be = 0;
        g_playerEntity.unk_bf = 0;
        g_playerEntity.attackAnim = 0;
    case 1:
        cVar1 = Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400);
        g_playerEntity.anim_87 += cVar1;
        break;
    case 2:
        g_playerEntity.attackAnim = 1;
        g_playerEntity.anim_87 = 3;
        g_playerEntity.unk_be = 0;
        g_playerEntity.unk_bf = 0;
    case 3:
        Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400);
        break;
    case 4:
        g_playerEntity.attackAnim = 2;
        g_playerEntity.anim_87 = 5;
        g_playerEntity.unk_be = 0;
        g_playerEntity.unk_bf = 0;
    case 5:
        cVar1 = Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400);
        if (cVar1 != 0) {
            g_playerEntity.animationId = 1;
            g_playerEntity.animFrameId = 0;
            g_playerEntity.anim_86 = 0;
            g_playerEntity.anim_87 = 0;
            g_playerEntity.isBeingAttackedFlag = 0;
        }
    }
    if ((int)(unsigned int)g_playerEntity.unk_be <= (int)((unsigned int)(g_playerEntity.id & 1) * -4 + 10)) {
        EntityUpdateWeaponJoint(0);
        return;
    }
    EntityUpdateWeaponJoint(1);
}
// 0x004196d0 — Death animation with billboards (5 states)
void player_anim_dispatch_4c2ac8(void) {
    JointStruct* pJVar1;
    char cVar2;
    switch (g_playerEntity.anim_87) {
    case 0:
        g_playerEntity.anim_87 = 1;
        g_playerEntity.unk_be = 0;
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
        g_playerEntity.anim_87 += cVar2;
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
        BillboardSetColor(&ENTITY->pushVelocity, 1, 2, DAT_00ffff50);
        BillboardAdjSize(&ENTITY->pushVelocity, 0xffffff38, 0xffffff38);
        g_playerEntity.anim_87 = 3;
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
    if (g_playerEntity.anim_87 == 0) {
        g_playerEntity.anim_87 = 1;
        g_playerEntity.unk_8c = 3;
        g_playerEntity.unk_c2 = 0;
        g_playerEntity.unk_be = 0;
        g_playerEntity.unk_bf = 0;
        g_playerEntity.attackAnim = 0;
    } else if (g_playerEntity.anim_87 != 1) {
        return;
    }
    entity_apply_anim_vertex((Entity*)&g_playerEntity, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2);
    cVar1 = Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400);
    if (cVar1 != 0) {
        g_playerEntity.isBeingAttackedFlag = 0;
        g_playerEntity.animationId = 1;
        g_playerEntity.animFrameId = 0;
        g_playerEntity.anim_86 = 0;
        g_playerEntity.anim_87 = 0;
    }
}
void player_anim_set_attacked_flag(void) {    // 0x00469400 - dispatch via DAT_004c2ac8[anim_86]
    extern void* DAT_004c2ac8[];
    void (*func)(void) = (void(*)(void))DAT_004c2ac8[g_playerEntity.anim_86];
    if (func) func();
}
// 0x00468e10 — Limb physics with bouncing (7 states)
void player_anim_dispatch_4ba360(void) {
    JointStruct* pJVar5;
    char cVar6;
    short sVar7;
    short sVar8;

    pJVar5 = g_playerEntity.jointsStructs;
    switch (g_playerEntity.anim_87) {
    case 0:
        g_playerEntity.unk_be = 0;
        g_playerEntity.unk_bf = 0;
        g_playerEntity.anim_87 = 1;
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
        Play3DSnd(3, 3, 0, (int)&g_playerEntity.transform.t);
        Play3DSnd(4, 0, 0, (int)&g_playerEntity.transform.t);
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
        g_playerEntity.unk_be = 0;
        g_playerEntity.unk_bf = 0;
        g_playerEntity.attackAnim = 0;
        g_playerEntity.anim_87 = 3;
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
        g_playerEntity.anim_87 = 5;
        g_playerEntity.unk_8c = 3;
        g_playerEntity.unk_be = 0;
        g_playerEntity.unk_bf = 0;
        // fall through
    case 5:
        cVar6 = Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400);
        if (cVar6 != 0) {
            g_playerEntity.unk_bf = 0;
            g_playerEntity.attackAnim = 0;
            g_playerEntity.anim_87 = 6;
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
        (g_playerEntity.transform.t[1] < 700)) {
        g_playerEntity.health = -1;
        g_playerEntity.transform.t[1] = g_playerEntity.transform.t[1] + pJVar5[0].velY;
        sVar8 = pJVar5[0].velY;
        sVar7 = sVar8 + 2;
        pJVar5[0].velY = sVar7;
        if (0x20 < sVar7) {
            pJVar5[0].velY = sVar8 + 12;
        }
        if ((700 < g_playerEntity.transform.t[1]) && ((pJVar5[0].velX & 7) != 0)) {
            g_playerEntity.transform.t[1] = 0x28a;
            sVar8 = -pJVar5[0].velY;
            pJVar5[0].velY = sVar8;
            pJVar5[0].velY = (short)((int)((int)sVar8 + ((int)sVar8 >> 31 & 7U)) >> 3);
            pJVar5[0].velX--;
            g_playerEntity.anim_87 = 4;
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
            g_playerEntity.unk_c2 = 30;
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
    switch (g_playerEntity.anim_87) {
    case 0:
        g_playerEntity.anim_87 = 1;
        g_playerEntity.attackAnim = 2;
        g_playerEntity.unk_8c = 3;
        g_playerEntity.unk_c2 = 200;
        g_playerEntity.unk_be = 0;
        g_playerEntity.unk_bf = 0;
        // fall through
    case 1:
        if (g_playerEntity.unk_be < 0x24) {
            entity_apply_anim_vertex((Entity*)&g_playerEntity, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2);
        } else {
            Add_speedXZ(0);
        }
        cVar1 = Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400);
        if (cVar1 != 0) {
            g_playerEntity.anim_87 = 2;
            return;
        }
        break;
    case 2:
        g_playerEntity.unk_be = 0;
        g_playerEntity.unk_bf = 0;
        g_playerEntity.isBeingAttackedFlag = 2;
        g_playerEntity.anim_87 = 3;
        g_playerEntity.attackAnim = 3;
        g_playerEntity.unk_8c = 3;
        // fall through
    case 3:
        if ((1 < g_playerEntity.isBeingAttackedFlag) && (3 < g_playerEntity.unk_be)) {
            Play3DSnd(3, 2, 0, (int)&g_playerEntity.transform.t);
            PlayEntitySnd(2);
            g_playerEntity.isBeingAttackedFlag = 1;
        }
        if ((g_playerEntity.health < 0) && (10 < g_playerEntity.unk_be)) {
            PlayEntitySnd(2);
            g_playerEntity.anim_87 = 8;
            return;
        }
        sVar2 = GetPlayerInputMasked();
        g_playerEntity.attackDirection = g_playerEntity.attackDirection + (unsigned short)(sVar2 != 0) * (unsigned short)-3;
        if ((g_playerEntity.attackDirection < 0) &&
            ((cVar1 = Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400)), cVar1 != 0)) {
            g_playerEntity.anim_87 = 4;
            return;
        }
        cVar1 = Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400);
        if (cVar1 != 0) {
            g_playerEntity.anim_87 = 4;
            return;
        }
        break;
    case 4:
        g_playerEntity.unk_8c = 3;
        g_playerEntity.unk_be = 0;
        g_playerEntity.unk_bf = 0;
        g_playerEntity.anim_87 = 5;
        g_playerEntity.attackAnim = 5;
        // fall through
    case 5:
        sVar2 = GetPlayerInputMasked();
        g_playerEntity.attackDirection = g_playerEntity.attackDirection + (unsigned short)(sVar2 != 0) * (unsigned short)-3;
        if ((g_playerEntity.attackDirection < 0) &&
            ((cVar1 = Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400)), cVar1 != 0)) {
            g_playerEntity.anim_87 = 6;
            return;
        }
        cVar1 = Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400);
        if (cVar1 != 0) {
            g_playerEntity.anim_87 = 6;
            return;
        }
        break;
    case 6:
        g_playerEntity.anim_87 = 7;
        g_playerEntity.attackAnim = 4;
        g_playerEntity.unk_8c = 3;
        g_playerEntity.unk_be = 0;
        g_playerEntity.unk_bf = 0;
        // fall through
    case 7:
        cVar1 = Joint_move(1, g_playerEntity.jointMoveData0, g_playerEntity.jointMoveData1, 0x400);
        if (cVar1 != 0) {
            if (-1 < g_playerEntity.health) {
                g_playerEntity.anim_86 = 0;
                g_playerEntity.anim_87 = 0;
                g_playerEntity.isBeingAttackedFlag = 0;
                g_playerEntity.animationId = 1;
                g_playerEntity.animFrameId = 0;
                return;
            }
            g_playerEntity.anim_87 = 8;
            return;
        }
        break;
    case 8:
        Play3DSnd(3, 3, 0, (int)&g_playerEntity.transform.t);
        g_playerEntity.anim_87 = 9;
        g_playerEntity.attackDirection = 0x5a;
        BillboardSetColor(&g_playerEntity.pushVelocity, 1, 2, DAT_00ffff50);
        BillboardSetSize(&g_playerEntity.pushVelocity, 0, 0);
        // fall through
    case 9:
        BillboardAdjSize(&g_playerEntity.pushVelocity, 0x14, 0x14);
        sVar2 = g_playerEntity.attackDirection;
        g_playerEntity.attackDirection = g_playerEntity.attackDirection - 1;
        if (sVar2 == 0) {
            g_playerEntity.anim_87 = 10;
        }
        break;
    }
}
// 0x004401c0
void player_anim_poison_death(void) {
    if (g_playerEntity.anim_87 == 0) {
        g_playerEntity.anim_87 = 1;
        g_playerEntity.attackAnim = 0;
        g_playerEntity.unk_c2 = 0;
        g_playerEntity.unk_be = 0;
        g_playerEntity.unk_bf = 0;
        g_playerEntity.unk_8c = 3;
        g_playerEntity.isBeingAttackedFlag = 1;
        Play3DSnd(3, 0, 0, (int)&g_playerEntity.transform.t);
    }
    Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400);
}
// 0x00440230
void player_anim_death_billboard(void) {
    if (g_playerEntity.anim_87 == 0) {
        g_playerEntity.anim_87 = 1;
        g_playerEntity.unk_be = 0;
        g_playerEntity.unk_bf = 0;
        g_playerEntity.attackAnim = 3;
        g_playerEntity.isBeingAttackedFlag = 1;
        g_playerEntity.unk_8c = 0;
    } else if (g_playerEntity.anim_87 == 1) {
        Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400);
        if (g_playerEntity.unk_be == 14) {
            g_playerEntity.unk_be = 13;
        }
    } else if (g_playerEntity.anim_87 == 2) {
        g_playerEntity.unk_03 |= 0x80;
        // TODO: Apply RotMatrix / ApplyLVAndMul0Matrix transforms using g_EnemiesList[0] joint matrices
    }
}
void player_anim_limb_physics(void) {         // 0x00424fb0 - dispatch via DAT_004ba360[anim_86]
    extern void* DAT_004ba360[];
    void (*func)(void) = (void(*)(void))DAT_004ba360[g_playerEntity.anim_86];
    if (func) func();
}
// 0x00424de0
void player_anim_enemy_interact(void) {
    if (g_playerEntity.anim_87 == 0) {
        g_playerEntity.anim_87 = 1;
        g_playerEntity.attackAnim = 2;
        g_playerEntity.isBeingAttackedFlag = 0x80;
        g_playerEntity.flags |= 6;
        g_playerEntity.unk_be = 0;
        g_playerEntity.unk_bf = 0;
        g_playerEntity.unk_8c = 0;
    } else if (g_playerEntity.anim_87 == 1) {
        if (g_playerEntity.unk_be == 8) {
            Play3DSnd(3, 3, 0, (int)&g_playerEntity.transform.t);
            // TODO: Billboard effects on joints at frame 8/9 and >95
        }
        entity_apply_anim_vertex((Entity*)&g_playerEntity, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2);
        char cVar2 = Joint_move(0, g_playerEntity.emdScratchPtr1, g_playerEntity.emdScratchPtr2, 0x400);
        g_playerEntity.anim_87 += cVar2;
    } else if (g_playerEntity.anim_87 == 2) {
        g_playerEntity.health = -1;
    }
}
void player_anim_death_alt(void) {            // 0x004088f0 - dispatch via DAT_004b1a90[anim_86]
    extern void* DAT_004b1a90[];
    void (*func)(void) = (void(*)(void))DAT_004b1a90[g_playerEntity.anim_86];
    if (func) func();
}
void player_anim_dispatch_4b1a90(void) {      // 0x0045c460 - dispatch via DAT_004c10b0[anim_87]
    extern void* DAT_004c10b0[];
    void (*func)(void) = (void(*)(void))DAT_004c10b0[g_playerEntity.anim_87];
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
    entity->transform.t[0] = (unsigned int)entity->unk_c6 + (int)g_svecScratch.x;
    entity->transform.t[2] = (unsigned int)entity->unk_c8 + (int)g_svecScratch.z;
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
    RotMatrix((SVECTOR*)&ENTITY->position.pad, &ENTITY->transform);

    // Compose: scratch = entity_transform * joint[0].transform
    ApplyLVAndMul0Matrix(&ENTITY->transform, &joints[0].transform, &g_matrixScratch);

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
    ENTITY->transform.t[0] -= g_matrixScratch.t[0];
    ENTITY->transform.t[2] -= g_matrixScratch.t[2];
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
// plus an angular offset. Moves the entity by its unk_c2 speed value.
// ============================================================================
void Add_speedXZ(int angleOffset)
{
    // Set speed vector from entity's base speed value
    g_svecScratch.x = ENTITY->unk_c2;
    g_svecScratch.y = 0;
    g_svecScratch.z = 0;

    // Copy identity matrix to scratch
    g_matrixScratch = g_identityMatrixData;

    // Rotate by entity facing + offset
    RotMatrixY((int)(short)ENTITY->angle + (int)(short)angleOffset, &g_matrixScratch);

    // Transform speed vector by rotation
    ApplyMatrixSV(&g_matrixScratch, &g_svecScratch, &ENTITY->speed);

    // Apply speed to entity transform translation
    ENTITY->transform.t[0] += (int)ENTITY->speed.x;
    ENTITY->transform.t[1] += (int)ENTITY->speed.y;
    ENTITY->transform.t[2] += (int)ENTITY->speed.z;
}

// ============================================================================
// ApplyLVAndMul0Matrix (0x0040a0b0)
// Full matrix composition: m_out = m0 * m1 (rotation + translation)
// ============================================================================
void ApplyLVAndMul0Matrix(void* m0, void* m1, void* mOut)
{
    CompMatrix((MATRIX*)m0, (MATRIX*)m1, (MATRIX*)mOut);
}

// RotMatrix is implemented in GteMatrix.cpp (0x004406a0)
