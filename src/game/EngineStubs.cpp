// EngineStubs.cpp - General engine and video stubs (pending decompilation)
// All functions decompiled from Ghidra with original addresses
#include "../Globals.h"
#include "../marni/MarniSystem.h"
#include "../marni/PSXTexture.h"
#include "../marni/MarniBits.h"
#include "../marni/Marni3DObject.h"
#include "SpriteRenderer.h"
#include "Items.h"
#include <math.h>

// Forward declarations for TmdAnimation.cpp
extern void SetAnimSlot(AnimSlot* slots, int slotPtr, int index);
extern unsigned int* CreateAnimObject(int slotPtr, unsigned int* param2);

// Forward declarations for rendering chain
extern unsigned int AsyncCreateTmdObject(unsigned int param1, unsigned int param2, unsigned int param3);
extern void FUN_00486df0(void* spriteData);  // Sprite rendering mode

// Forward declarations for functions defined later in this file
void FUN_00483250(int p0, int p1, int p2, int p3, int p4, int p5, void* p6);
void FUN_004896c0(void* joint, short p1, short p2, int p3);
void FUN_0048a210(void* joint);
void SetLightMatrix(MATRIX* m);
void SetRotAndTransMatrix(MATRIX* m);
int  is_entity_in_switch_zone(VECTOR* pos, void* zoneData);

// Forward declarations for update_entities dependencies
extern unsigned char FUN_0048bd00(void* light, unsigned char param2, int param3); // 0x0048bd00
extern void FUN_0048bda0(void);
extern void FUN_00454fd0(int itemId, int mode, short x, short y);   // MainMenu.cpp
extern unsigned short* g_CurrentMenuFramesDataPtr;      // MainMenu.cpp
extern unsigned char g_totalInventorySlots;             // MainMenu.cpp

static void FUN_0044ea50(void);
static void FUN_004631c0(void);
static int  FUN_0040a990(int y, int x);
static int  FUN_0040a530(int value);
static void FUN_0040a250(MATRIX* src, MATRIX* dst);
static int  FUN_0044ed40(void);
static int  FUN_0044ef60(short angle, int axisMask);
static void FUN_0044eca0(void);
static void FUN_0044eb30(void);
static void FUN_0044e660(void);
static void FUN_0044e820(void);
static void FUN_0044e8c0(void);
static void FUN_0044e920(void);
static void FUN_0044ea50(void);
static void FUN_004846d0(int slot);
static void FUN_004844c0(void);
static void FUN_00483580(int* joint, MATRIX* out);
static void FUN_004841f0(void);
extern void Flg_on(int baseAddr, unsigned int bitIndex);
extern void ResolveAnimPointers(unsigned char* data);
extern void InitScaMatrix(int parentPtr, ScaMatrixData* matrix);
extern unsigned int CheckTmdTransparency(int tmdData);
extern unsigned char DAT_00ae9f23;

                                                   // 0x0048bda0

// ---------------------------------------------------------------------------
// Texture/video stubs (called from logos_state / title_state in GameState.cpp)
// ---------------------------------------------------------------------------

// FUN_00470a30 (0x00470a30) - stub
void FUN_00470a30(void) { }

// QueueVideoPlayback - stub
void QueueVideoPlayback(int id, int b)                { /* stub */ }

// VideoDriver_ClearArrayD0 - stub
void VideoDriver_ClearArrayD0(void)                   { /* stub */ }

// LoadPSXImage - thin wrapper around PSXTexture::Store
void LoadPSXImage(PSXTexture* tex, void* buf, int mode)
{
    tex->Store((int*)buf, mode);
}

// FUN_0046c160 (0x0046c160) - stub
int  FUN_0046c160(void* data, int mode)               { return 0; }

// FUN_00427270 (0x00427270) - stub
void FUN_00427270(void)                               { /* stub */ }

// FUN_00427100 (0x00427100) - stub
void FUN_00427100(int a, int b, int c)                { /* stub */ }

// FUN_004271e0 (0x004271e0) - stub
int  FUN_004271e0(int a, int b)                       { return 0; }

// FUN_00426df0 (0x00426df0) - stub
void FUN_00426df0(int id, void* data)                 { /* stub */ }

// FUN_00426f70 (0x00426f70) - stub
void FUN_00426f70(int a, void* b)                     { /* stub */ }

// FUN_00427250 (0x00427250) - stub
void FUN_00427250(void)                               { /* stub */ }

// ---------------------------------------------------------------------------
// General game engine stubs (referenced from MainLoop.cpp and WindowProc.cpp)
// ---------------------------------------------------------------------------

// FUN_004973a0 (0x004973a0) - stub
void FUN_004973a0(int param)                                 { /* stub */ }

// UpdateDemoTimer (0x00429ce0) - increments demo idle timer and resets when threshold reached
void UpdateDemoTimer(void) {
  if ((g_main_state_flags2 & 0x10000000) != 0 &&
      (g_message_flags & 0x200) != 0 &&
      g_DemoTimerCur != 0) {
    g_DemoTimerCur++;
    if ((int)(g_DemoTimerMax - 1) <= (int)(unsigned short)g_DemoTimerCur) {
      g_DemoTimerCur = 0;
    }
  }
}

// empty_0047b950 (0x0047b950) - stub
void empty_0047b950(int param)
{
}

// ShowVideoModeDebugText - stub
void ShowVideoModeDebugText(void)                            { /* stub */ }

// empty_0040abb0 (0x0040abb0) - stub
void empty_0040abb0(void* ptr, int a, int b, int c) { /* stub */ }

// cleanup_texture_slot - stub
void cleanup_texture_slot(int slot)                           { /* stub */ }

// empty_00497c10 (0x00497c10) - stub
void empty_00497c10(int value)                         { /* stub */ }

// empty_00470960 (0x00470960) - stub
void empty_00470960(int slot) { }

// vram_clr - remanent of original PSX code
void vram_clr(int x, int y, int w, int h) {}

// empty_00412380 (0x00412380) - unknown init function
void empty_00412380(void) { }

// empty_483510 (0x00483510) - stub, returns 0
int  empty_483510(void) { return 0; }

// ---------------------------------------------------------------------------
// game_loop dependency stubs (pending full decompilation)
// These functions are called per-frame from game_loop (0x00480b30).
// ---------------------------------------------------------------------------

// (0x0048f0f0) - Update all enemy entities per-frame
// Iterates through g_EnemiesList, calls the per-type update function from
// enemies_update_functions_tbl for each active entity, and performs lighting
// checks when the entity is in a camera switch zone with joint animation active.
void update_entities(void)
{
    // 0x0048f0f0-0x0048f102: Set current entity pointer to start of list
    ENTITY = g_EnemiesList;
    int em_counter = 0;

    // 0x0048f104-0x0048f10f: Only process if there are active enemies
    if (g_enemy_count == 0) {
        return;
    }

    do {
        // 0x0048f10f: Safety guard - max 30 entities (array size)
        if (em_counter > 29) {
            return;
        }

        // 0x0048f114-0x0048f12b: Only update active entities (status_flags bit 0)
        if ((ENTITY->status_flags & 0x01) != 0) {
            // 0x0048f12b: Call per-type update function from dispatch table
            void* updateFunc = enemies_update_functions_tbl[ENTITY->id];
            if (updateFunc != NULL) {
                ((void(*)())updateFunc)();
            }

            // 0x0048f131-0x0048f197: Lighting check when joint animation is active
            // Original: if ((g_main_state_flags & 1) != 0 && FUN_0048bd00(...) != 0)
            // then recalculate entity lighting via FUN_0048bda0().
            // This handles dynamic lighting when the entity's weapon/hand joint
            // moves in front of a camera light source.
            if ((g_main_state_flags & 0x00000001) != 0) {
                unsigned char lightCheck = FUN_0048bd00(
                    (void*)((int)g_RdtPointer[1].lights + (unsigned int)g_roomCameraId * 44 - 4),
                    ((unsigned char)(g_main_state_flags >> 1)) & 1,
                    (int)ENTITY->scaMatrixData.localMatrix.t);
                if (lightCheck != 0) {
                    FUN_0048bda0();
                }
            }

            // 0x0048f197: Increment processed entity counter
            em_counter = em_counter + 1;
        }

        // 0x0048f19a: Advance to next entity (sizeof(Entity) = 0x18C)
        ENTITY = (Entity*)((char*)ENTITY + sizeof(Entity));

    } while (em_counter < (int)(unsigned int)g_enemy_count);
}

// update_player_anim (0x00494d90) and update_player_position (0x0041c060) are
// implemented in PlayerAnimations.cpp, together with the player state dispatch
// table from 0x004d4550. Both used to be empty stubs here, which is why the
// player never animated and no character model appeared.

// DrawFadeSpr (0x00456d30) and entity_add_fade_sprite (0x00456810) are
// implemented in FadeSprite.cpp, together with the queue they operate on.

// (0x00474090) - Update sound system state per-frame
void update_sounds(void) { }

// (0x00473ff0) - Update room camera and lighting per-frame
void room_camera_and_lighting_update(void) { }

// (0x0048c190) - Camera transform for entity rendering
// NOW IMPLEMENTED in GteMatrix.cpp

// (0x0045a2e0) - Entity matrix update for rendering
// NOW IMPLEMENTED in GteMatrix.cpp

// (0x00481660) - Update entity lighting from RDT point lights
// Recomputes the 3 D3D lights based on the entity's distance to each RDT
// light. Lights with lightType == 0 are point lights with radial falloff
// (direction = light->entity, color attenuated by distance); the others are
// used as-is (directional).
void update_entity_lighting(VECTOR* entityPos)
{
    if (g_RdtPointer == NULL) return;

    for (int i = 0; i < 3; i++) {
        RDT_Light* light = &g_RdtPointer->lights[i];
        if (light->lightType == 0) {
            struct { int x, y, z; unsigned char r, g, b; } pointLight;
            pointLight.x = entityPos->x - light->pos_x;
            pointLight.y = entityPos->y - light->pos_y;
            pointLight.z = entityPos->z - light->pos_z;

            int atten = (int)(unsigned short)light->radius -
                        SquareRoot0(pointLight.z * pointLight.z +
                                    pointLight.x * pointLight.x);
            if (atten < 0) atten = 0;

            if ((unsigned short)light->radius == 0) {
                pointLight.r = pointLight.g = pointLight.b = 0;
            }
            else {
                pointLight.r = (unsigned char)((light->red   * atten) / (int)(unsigned short)light->radius);
                pointLight.g = (unsigned char)((light->green * atten) / (int)(unsigned short)light->radius);
                pointLight.b = (unsigned char)((light->blue  * atten) / (int)(unsigned short)light->radius);
            }
            FUN_0040ac80(i, &pointLight);
        }
        else {
            FUN_0040ac80(i, light);
        }
    }
}

// (0x0048c350) - Render entity joints with lighting (in-game entity renderer)
// Per-joint loop: computes camera-space matrices, sets light/rot matrices,
// and queues each visible joint's TMD object for rendering. Skipped for
// entity types 0x0D/0x12 with sub-type 1 (they render elsewhere).
void calc_entity_lighting(Entity* ent)
{
    int param_1 = (int)ent;
    unsigned char* entBytes = (unsigned char*)ENTITY;

    if (((entBytes[1] == 0x0D) || (entBytes[1] == 0x12)) && (entBytes[2] == 1)) {
        return;
    }

    g_animFrameIdSave = (unsigned int)((*(unsigned char*)(param_1 + 3) & 0x7f) == 0);

    unsigned char jointIdx = *(char*)(param_1 + 0x8d) - 1;
    MATRIX* pJoint = (MATRIX*)((unsigned int)jointIdx * 0x7c + *(int*)(param_1 + 0x98));

    update_entity_lighting((VECTOR*)(param_1 + 0x34));

    do {
        short jointFlags = pJoint->m[0][0];

        if ((entBytes[1] == 0x0D) || (entBytes[1] == 0x12)) {
            update_entity_lighting((VECTOR*)(pJoint[2].t + 1));
        }

        if ((jointFlags & 4) != 0) {
            g_svecScratch.x = 0;
            g_svecScratch.z = 0;
            g_svecScratch.y = 0x1e;
            pJoint->m[0][2] = -0x14;
            pJoint->m[1][1] = 0;
            pJoint->m[1][0] = 200;
            FUN_004896c0(pJoint, (short)0xffdd, (short)0xff9c, 1);
        }

        if ((jointFlags & 1) == 0) {
            if ((jointFlags & 0x20) != 0) {
                FUN_0048a210(pJoint);
            }
        }
        else {
            MATRIX localMatrix;
            ApplyLVAndMul0Matrix(&g_RoomCameraData, pJoint[2].m[0] + 2, &localMatrix);

            // Copy g_lightMatrix to g_matrixScratch
            MATRIX* src = &g_lightMatrix;
            MATRIX* dst = &g_matrixScratch;
            for (int i = 8; i != 0; i--) {
                *(unsigned int*)dst->m[0] = *(unsigned int*)src->m[0];
                src = (MATRIX*)(src->m[0] + 2);
                dst = (MATRIX*)(dst->m[0] + 2);
            }

            if (g_animFrameIdSave == 0) {
                if ((jointFlags & 0x74) != 0) goto checkSwitchZone;
doRender:
                // Original skips a specific hunter in stage 6 / room 0xC / camera 3
                if (((entBytes[1] != 18) || (g_stageId != 6)) ||
                    ((g_roomId != 0x0C) || (g_roomCameraId != 3))) {
                    g_entityJointPosX = pJoint->t[0];
                    SetLightMatrix(&g_matrixScratch);
                    SetRotAndTransMatrix(&localMatrix);
                    FUN_00483250(0, 0, 0, pJoint->t[1], 0, 4,
                        (BYTE*)&g_spriteAnimSlots[2] + (unsigned int)g_spriteAnimActive * 0x14);
                }
            }
            else if ((jointFlags & 0x74) != 0) {
checkSwitchZone:
                if (is_entity_in_switch_zone((VECTOR*)(pJoint[2].t + 1), g_CurrentRdtDataTypePtr) != 0) {
                    goto doRender;
                }
            }
        }

        pJoint = (MATRIX*)(pJoint[-4].m[0] + 2);
        bool done = (jointIdx == 0);
        jointIdx--;
        if (!done) continue;
        return;
    } while (true);
}

// (0x0047c0c0) - Update 2D sprite effects (billboards, particles)
void update_2d_effects(void) { }

// (0x00475b80) - Draw room sprites (background overlays)
void DrawRoomSpr(void) { }

// (0x00494050) - Debug save menu (F5 key)
void DebugSaveMenu(void) { }

// (0x00481310) - Player death sequence state machine
void die_state(void) { }

// (0x00481250) - Death fade-out transition
void TimeoutDeathFadeOut(void) { }

// (0x004818b0) - Start attract mode demo playback
void StartAttractDemo(void) { }

// (0x004815f0) - check_menus_state now implemented in GameLoop.cpp

// (0x00463710) - main_menu now implemented in MainMenu.cpp

// (0x004761b0) - options_menu now implemented in OptionsMenu.cpp

// 0x004813c0 is now implemented as room_transition_load in GameState.cpp. The old
// stub described it as "restore room state after menu close", which was wrong - it
// reads g_pendingDoorRecord seven times and is the room/stage transition loader.

// (0x0047b980) - Set screen fade transition parameters
void set_fading(int type, int counter) { }

// (0x004...) - Display death/game-over screen
void display_die_screen(void) { }

// (0x0047eb60) - Post-death cleanup
void FUN_0047eb60(void) { }

// (0x0042a030) - Check and display interactive screen (item pickup, etc.)
void check_and_display_interactive_screen(void) { }

// (0x0041bc90) - Check desk/lock interaction state
void check_desk_state(void) { }

// (0x0041c240) - Check item box interaction state
void check_itembox_state(void) { }

// (0x0041c330) - Check typewriter (save point) interaction state
void check_typewriter_state(void) { }

// (0x0041c490) - Check event item usage state
void check_event_item_usage(void) { }

// ---------------------------------------------------------------------------
// MainMenu.cpp dependency stubs (pending full decompilation)
// ---------------------------------------------------------------------------

// (0x0048b9e0) - Set up joint animation structures for an entity
// Iterates through all joints, initializes animation slots, and creates
// animation objects. Used by main_menu before Joint_move to set up the
// player model's joint data from the EMD animation header.
void SetupJointStructures(void* buf)
{
    unsigned int* paramBuf = (unsigned int*)buf;
    unsigned char jointIdx = 0;
    JointStruct* joint = ENTITY->jointsStructs;
    void* modelLoadBuffer = (void*)ENTITY->modelLoadBuffer;
    unsigned char count = ENTITY->jointCount;

    if (count == 0) return;

    do {
        // Link animation slot data for this joint
        SetAnimSlot((AnimSlot*)modelLoadBuffer, (int)&joint->anim_field, jointIdx);

        // Initialize joint fields
        joint->index = jointIdx;
        joint->flags = 3;
        joint->data_ptr = &joint->scale_flag;
        joint->field_1c = 0;
        joint->scale_flag = 1;
        joint->anim_object = NULL;
        joint->field_02 = 0;
        joint->anim_field = 0;

        // Create animation object in the buffer
        paramBuf = CreateAnimObject((int)&joint->anim_field, paramBuf);

        // Special case: entity ID 0x29 disables certain joints
        if (ENTITY->id == 0x29) {
            switch (jointIdx) {
            case 4: case 5: case 7: case 8:
                joint->flags = 0;
            }
        }

        joint++;
        jointIdx++;
    } while (jointIdx < count);
}

// (0x00462620) - LoadEquippedWeaponAnimation is implemented in
// EntityModelLoader.cpp. An empty stub used to live here with a different
// parameter list; because both were visible it became an overload and every
// call that passed pointers (main_menu, options_menu) hit the stub instead of
// the real loader.

// (0x004844b0) - Menu cleanup sub-function
void FUN_004844b0(void) { }

// (0x00470a40) - Texture cleanup sub-function
void FUN_00470a40(void) { }

// (0x0047d0e0) - Effect cleanup sub-function
void FUN_0047d0e0(void) { }

// (0x00462940) - Room camera restore after menu
void FUN_00462940(void) { }

// (0x0040ac80) - Set light data by index
// Normalizes the light direction (light position) into g_lightMatrix row idx
// (12-bit fixed, Y negated) and stores the light color as 0..1 floats in
// g_d3dLightData[idx*12 + 6..8] (+ 1.0f at [9]). Input light data layout:
//   int x, y, z;  byte r, g, b  (offsets 0xC, 0xD, 0xE)
void FUN_0040ac80(int idx, void* lightData)
{
    int* p = (int*)lightData;
    double x = (double)p[0];
    double y = (double)p[1];
    double z = (double)p[2];

    // Same normalize-to-4096 as VectorNormal (0x0040a5c0, GteMatrix.cpp), inlined
    double len = sqrt(x * x + y * y + z * z);
    if (len < 1.0) len = 1.0;
    int nx = (int)(x / len * 4096.0);
    int ny = (int)(y / len * 4096.0);
    int nz = (int)(z / len * 4096.0);

    g_lightMatrix.m[idx][0] = (short)nx;
    g_lightMatrix.m[idx][1] = (short)-ny;
    g_lightMatrix.m[idx][2] = (short)nz;
    g_lightMatrix.t[idx] = 0;

    unsigned char* c = (unsigned char*)lightData;
    float* lightColor = (float*)&g_d3dLightData[idx * 12 + 6];
    for (int i = 0; i < 3; i++) {
        unsigned char v = c[0xC + i];
        if (v > 0x7F) v = 0x80;
        lightColor[i] = (float)v * 0.0078125f; // 1/128
    }
    g_d3dLightData[idx * 12 + 9] = 0x3F800000; // 1.0f
}

// (0x00470a20) - Empty function (menu init placeholder)
void empty_00470a20(void) { }

// ============================================================================
// Item 3D model viewer (0x0044e1b0) and item model loader (0x004841f0)
// ============================================================================

// Shared menu state used by the viewer (defined in MainMenu.cpp)
extern unsigned char DAT_00ae9f10;
extern unsigned char DAT_00ae9f13;
extern unsigned char DAT_00ae9f1b;
extern unsigned char DAT_00ae9f49;
extern unsigned char DAT_00ae9f4a;
extern unsigned char DAT_00ae9f48;

// Item model load state (FUN_00484420 / FUN_004841f0)
static int  g_itemModelSrc;            // DAT_00aafcdc - .ivm file buffer (TIM part)
static int  g_itemModelTmdBase;        // DAT_00aafcd8 - TMD header inside the .ivm
static int  g_itemModelBlendFlag;      // DAT_004d2c10 - 0x3f000000 when the model has transparency
static int  g_itemModelTmdCount;       // DAT_004d2c14 - processed object count
static int  g_itemModelTexCreated;     // DAT_008fc3f8 - item texture page flag
static int  g_itemSharedTmdCount;      // DAT_008f8c48 - shared transparent TMD slot count
static int  g_itemSharedTmdReady;      // DAT_008f8c50 - shared textures created flag
static int  g_itemRenderSlot;          // DAT_008f8d80 - render slot index (0-2)
static int  g_itemSharedDisplayFlag;   // DAT_009220b8 - shared model drawn flag
// The item model slot(s) must live inside g_tmdObjectBuffer: TmdQueueObject
// (the CMarniDirect3D vtable[10] adapter) only accepts object data that
// falls within that buffer and computes the slot index from the offset.
// The original stores EACH TMD object of a multi-object item into its own
// CMarniDirect3DTMD slot (0x008f8d88 + objIndex*0x1594) and the viewer draws
// the slot matching the render slot, so the port reserves three adjacent slots
// (247..249) and indexes them by the item object / render slot index. The
// entity allocator scans from the low end and never reaches them in practice.
#define ITEM_TMD_SLOT_INDEX 247
static BYTE* g_itemTmdSlots[3] = {
    &g_tmdObjectBuffer[(ITEM_TMD_SLOT_INDEX + 0) * 0x1594], // DAT_008fc0b0 (item object 0)
    &g_tmdObjectBuffer[(ITEM_TMD_SLOT_INDEX + 1) * 0x1594], // item object 1
    &g_tmdObjectBuffer[(ITEM_TMD_SLOT_INDEX + 2) * 0x1594], // item object 2
};
static BYTE g_itemSharedTmdSlot[0x1594]; // DAT_008f8908 - shared transparent slot
static DWORD g_itemSharedTmdHandles[16]; // DAT_008f8c54 - shared texture handles

// Viewer animation state
static short DAT_00ae9f4b;             // 0x00ae9f4b - item model object count

// 0x00ae9f4c - examine spin input vector (x,y,z); the 4th short is the
// SVECTOR pad RotMatrix reads past the end of.
static short g_viewerSpinInput[4];
#define DAT_00ae9f4c (g_viewerSpinInput[0])
#define DAT_00ae9f4e (g_viewerSpinInput[1])
#define DAT_00ae9f50 (g_viewerSpinInput[2])

// 0x00ae9f54 - per-object rotation vectors, 8-byte stride (0x00ae9f54 for
// object 0, 0x00ae9f5c for object 1). The viewer indexes this block by BYTE
// offset (index * 8), so the two vectors have to live in one contiguous
// array. Declaring the components as six separate shorts made
// `&DAT_00ae9f54 + 8` resolve to the base rotation vector below, so object 1
// was rotated by the base rotation as well as the root Sca - the base
// rotation got applied twice and the turntable spin came out as a compound
// tumble about a diagonal axis.
static short g_viewerObjRot[2][4];
#define DAT_00ae9f54 (g_viewerObjRot[0][0])
#define DAT_00ae9f56 (g_viewerObjRot[0][1])
#define DAT_00ae9f58 (g_viewerObjRot[0][2])
// 0x00ae9f5e - the examine spin accumulator IS object 1's yaw (0xae9f5c + 2)
#define DAT_00ae9f5e (g_viewerObjRot[1][1])

// 0x00ae9f64 - the viewer's base rotation: x = pitch, y = yaw (the turntable
// axis the intro spin and the left/right d-pad drive), z = roll.
static short g_viewerRot[4];
#define DAT_00ae9f64 (g_viewerRot[0])
#define DAT_00ae9f66 (g_viewerRot[1])
#define DAT_00ae9f68 (g_viewerRot[2])

static int   g_viewerJoints[3 * 0x50 / 4]; // 0x00ae9f6c - joint structs (parent/child at +0x48/+0x4c stay NULL)
static BYTE  g_viewerMatrices[3 * 0x50]; // 0x00ae9f94 - sub-object matrices (0x50 each)
static int   g_viewerScaMatrices[3 * 0x50 / 4]; // Sca[0] 0x9f94, Sca[1] 0x9fe4, Sca[2] 0xaea034

// 0x00aea04c/50/54 are Sca[2].localMatrix.t - the root Sca's translation, the
// "zoom" the intro animates from -0xaf00 up to the resting 0x1980. They must
// alias the Sca so the translation flows through the chain compose and the
// menu camera matrix (which turns the +X offset into view depth) exactly as
// in the original; standalone ints left the root translation at zero.
#define VIEWER_ROOT_SCA ((ScaMatrixData*)((BYTE*)g_viewerScaMatrices + 2 * 0x50))
#define DAT_00aea04c (VIEWER_ROOT_SCA->localMatrix.t[0])
#define DAT_00aea050 (VIEWER_ROOT_SCA->localMatrix.t[1])
#define DAT_00aea054 (VIEWER_ROOT_SCA->localMatrix.t[2])

static int   DAT_00aea084;             // 0x00aea084 - animation counter
static int   g_viewerPadWord;          // DAT_00be05b0 - pad read
static MATRIX g_viewerMatrixBe0f60;    // 0x00be0f60
static MATRIX g_viewerMatrixBe0f80;    // 0x00be0f80
static VECTOR g_viewerSvecBe0fa0;      // 0x00be0fa0 - ApplyMatrix writes 3 ints, not an SVECTOR
static SVECTOR g_viewerSvecBe0fb0;     // 0x00be0fb0
static int   g_viewerLightData[3][8];  // 0x00d22700 - viewer light data (3 lights)

// The base rotation matrix lives at 0x00aea038 (Sca[2].localMatrix); its
// third column (m[0][2], m[1][2], m[2][2]) is the model's up vector read by
// the angle recompute (DAT_00aea03c / 0x00aea042 / 0x00aea048).
#define VIEWER_BASE_MATRIX  ((short*)((BYTE*)g_viewerScaMatrices + 2 * 0x50 + 4))

// Viewer light setup (FUN_0040ac80) - the original built light records in
// DAT_00d22700..00d22730 and passed each 8-dword block to FUN_0040ac80.
// The directions are set ONCE by main_menu (0x004638e3: light0 (100,0x50,0x50),
// light1 (-100,0x50,0x50), light2 (0,-100,0x50)) and the intro/exit animation
// only rewrites the three colour bytes (+0xC/+0xD/+0xE) of each record with the
// fade value. A previous revision stored the fade value into ALL eight dwords,
// which corrupted the light directions and left every face in shadow.
static void viewer_setup_lights(int fadeValue)
{
    // NOTE: ambient comes from setBackColor via SetLightMatrix (called every
    // frame by the renderer), so nothing is written here - matching the
    // original, which only updates the light record colours.
    for (int i = 0; i < 3; i++) {
        int* p = &g_viewerLightData[i][0];
        if (p[0] == 0 && p[1] == 0 && p[2] == 0) {
            // Seed the directions once (main_menu's DAT_00d22700 setup)
            switch (i) {
            case 0: p[0] = 100;  p[1] = 0x50; p[2] = 0x50; break;
            case 1: p[0] = -100; p[1] = 0x50; p[2] = 0x50; break;
            case 2: p[0] = 0;    p[1] = -100; p[2] = 0x50; break;
            }
        }
        // The original writes the same byte to +0xC, +0xD and +0xE
        unsigned char* c = (unsigned char*)&g_viewerLightData[i][3];
        c[0] = (unsigned char)fadeValue;
        c[1] = (unsigned char)fadeValue;
        c[2] = (unsigned char)fadeValue;
        FUN_0040ac80(i, p);
    }
}

// (0x0044ef60) - Examine rotation combo check
// Advances the 12-byte combo record pointer and tests the current rotation
// angle against the record's target window. Returns 1 (match) or 0.
static int FUN_0044ef60(short angle, int axisMask)
{
    unsigned short* pRec = g_CurrentMenuFramesDataPtr;
    unsigned short target = pRec[0];
    if (target == 0) {
        g_CurrentMenuFramesDataPtr = (unsigned short*)((int)g_CurrentMenuFramesDataPtr + 4);
        return axisMask;
    }
    unsigned short tol = pRec[1];
    g_CurrentMenuFramesDataPtr = (unsigned short*)((int)g_CurrentMenuFramesDataPtr + 4);
    int diff = (int)((short)(angle + (short)target) & 0xfff) - (int)tol & 0xfff;
    if (diff <= target * 2) {
        return axisMask;
    }
    return 0;
}

// (0x0044ed40) - Item examine check. Returns 1 when the examine message has
// been queued (or the examine sequence is running), 0 when the item cannot be
// examined yet.
static int FUN_0044ed40(void)
{
    unsigned char flagIndex = g_ItemImageLookupTable[(unsigned int)DAT_00ae9f1b * 4 + 2];
    if ((flagIndex & 0x80) == 0) {
        if (0x6e < DAT_00ae9f1b) {
            return 0;
        }
        unsigned char exType = g_ItemExamineTypes[flagIndex];
        if ((exType & 0xf0) == 0) {
            DAT_00ae9f4a = 2;
            Flg_on((int)g_gameFlags_bc, (unsigned int)flagIndex);
            set_message_display(g_ItemHealTable[(unsigned int)flagIndex + 0x51], 0);
            return 1;
        }
        unsigned char tries = exType >> 4;
        int bVar3;
        g_CurrentMenuFramesDataPtr = (unsigned short*)(g_ItemExamineCombos + (unsigned int)(exType & 0xf) * 0xc);
        do {
            bVar3 = FUN_0044ef60(DAT_00ae9f64, 1);
            bVar3 |= FUN_0044ef60(DAT_00ae9f66, 2);
            bVar3 |= FUN_0044ef60(DAT_00ae9f68, 4);
            tries = tries - 1;
            if (tries == 0) break;
        } while (bVar3 != 7);
        if (bVar3 == 7) {
            if (DAT_00ae9f1b == 0x3e) {
                if (DAT_00ae9f5e == 0) {
                    DAT_00ae9f4a = 1;
                    DAT_00aea084 = 0;
                    return 1;
                }
            } else if ((0x3e < DAT_00ae9f1b) && (DAT_00ae9f1b < 0x41)) {
                DAT_00ae9f4a = 1;
                DAT_00aea084 = 0;
                return 1;
            }
            DAT_00ae9f4a = 2;
            Flg_on((int)g_gameFlags_bc, (unsigned int)flagIndex);
            set_message_display(g_ItemHealTable[(unsigned int)flagIndex + 0x51], 0);
            return 1;
        }
    } else {
        switch (DAT_00ae9f1b) {
        case 4:
        case 5:
        case 7:
        case 8:
        case 9:
            if (*(char*)((unsigned int)g_ItemSlotsPointer + 1 + (unsigned int)((DAT_00ae9f23 >> 1) - 4) * 2) != 0) {
                g_selectedItemId = DAT_00ae9f1b + 9;
                DAT_00ae9f4a = 2;
                set_message_display(0xf0, 0);
                return 1;
            }
            break;
        case 0x13:
            if ((Flg_ck((int)g_PlayerFlags, 0x13) != 0) || (Flg_ck((int)g_PlayerFlags, 0) != 0)) {
                DAT_00ae9f4a = 2;
                set_message_display(0xf1, 0);
                return 1;
            }
        }
    }
    return 0;
}

// Rotation control icon positions (0x004bf320 / 0x004bf328)
static const short g_itemViewerIconPos[4] = { 0x6D, 0xB6, 0x6D, 0x22 };
static const short g_itemViewerIconPosY[4] = { 0x1A, 0x49, 0x76, 0x49 };

// (0x0044eca0) - Draw the 4 rotation-control indicator arrows
static void FUN_0044eca0(void)
{
    g_TextureDesc.width = 9;
    g_TextureDesc.flags = 0x01000040;
    g_TextureDesc.texV = 0x7c;
    g_TextureDesc.depth = 0x1c;
    g_TextureDesc.unk10 = 0;
    g_TextureDesc.printClutTint = 0x1e4;
    g_TextureDesc.height = 9;
    unsigned short bitMask = 0x8000;
    unsigned char count = 4;
    do {
        count = count - 1;
        g_TextureDesc.texU = 0x80;
        g_TextureDesc.screenX = g_itemViewerIconPos[count];
        g_TextureDesc.screenY = g_itemViewerIconPosY[count];
        // 0x0044ecfb tests the raw held word at 0x00bf0a0c, i.e. the same four
        // face-button bits the rotation reads - not the edge-detected word.
        if (((unsigned short)g_button_pressed_id & bitMask) == 0) {
            g_TextureDesc.texU = 0x8c;
        }
        bitMask = bitMask >> 1;
        display_texture(&g_TextureDesc, 4, 0, 1);
        g_TextureDesc.texV = g_TextureDesc.texV - 0xc;
    } while (count != 0);
}

// (0x0044eb30) - Recompute the model view rotation angles from the spin input
static void FUN_0044eb30(void)
{
    // Zoom target
    g_viewerMatrixBe0f60.t[0] = DAT_00aea04c;
    g_viewerMatrixBe0f60.t[1] = DAT_00aea050;
    g_viewerMatrixBe0f60.t[2] = DAT_00aea054;

    RotMatrix((SVECTOR*)&DAT_00ae9f4c, (MATRIX*)&g_viewerMatrixBe0f60);
    // base = spin * base (the original wrote the product back into 0xaea038)
    MulMatrixInPlace(&g_viewerMatrixBe0f60, (MATRIX*)VIEWER_BASE_MATRIX);

    // Up vector = third column of the (spinned) base rotation matrix.
    // The original reads 0x00aea03c / 0x00aea042 / 0x00aea048, i.e. byte
    // offsets +4 / +0xa / +0x10 from the matrix base = short indices 2, 5, 8.
    short* baseM = VIEWER_BASE_MATRIX;
    int iVar1 = baseM[2];   // m[0][2]
    int iVar5 = baseM[8];   // m[2][2]
    int iVar4 = baseM[5];   // m[1][2]
    int iVar2 = iVar1 * iVar1 + iVar5 * iVar5 + iVar4 * iVar4;
    // The `U` suffix Ghidra puts on the sign-correction mask is an artifact of
    // how it renders the original's signed shift - it must NOT propagate into
    // the operands. Without the (int) cast the divisor is unsigned, so the
    // whole division goes unsigned: a negative m[0][2] (any yaw past 180 deg)
    // divided as ~4.29e9, which drove the extracted yaw to 90 deg and left the
    // model upside down for the rest of the turn. The original is an idiv.
    int denom = (int)(iVar2 + ((iVar2 >> 0x1f) & 0xfff)) >> 0xc;
    if (denom == 0) denom = 1;           // guard: never divide by zero
    iVar1 = (iVar1 << 0xc) / denom;

    DAT_00ae9f64 = FUN_0040a990(iVar4, iVar5);
    {
        int sq = (int)(iVar1 * iVar1 + ((iVar1 * iVar1 >> 0x1f) & 0xfff)) >> 0xc;
        int inv = 0x1000 - sq;
        if (inv < 0) inv = 0;
        int uVar3 = FUN_0040a530(inv);
        DAT_00ae9f66 = FUN_0040a990(iVar1, uVar3);
    }
    DAT_00ae9f68 = 0;

    g_viewerMatrixBe0f60.t[0] = 0;
    g_viewerMatrixBe0f60.t[1] = 0;
    g_viewerMatrixBe0f60.t[2] = 0;
    RotMatrix((SVECTOR*)&DAT_00ae9f64, (MATRIX*)&g_viewerMatrixBe0f60);
    FUN_0040a250(&g_viewerMatrixBe0f60, &g_viewerMatrixBe0f80);

    // First column of the base matrix: 0x00aea038 / 0x00aea03e / 0x00aea044,
    // i.e. byte offsets +0 / +6 / +0xc = short indices 0, 3, 6.
    g_viewerSvecBe0fb0.x = baseM[0];
    g_viewerSvecBe0fb0.y = -baseM[3];
    g_viewerSvecBe0fb0.z = baseM[6];
    ApplyMatrix((MATRIX*)&g_viewerMatrixBe0f80, (SVECTOR*)&g_viewerSvecBe0fb0, (VECTOR*)&g_viewerSvecBe0fa0);
    DAT_00ae9f68 = FUN_0040a990(g_viewerSvecBe0fa0.y, g_viewerSvecBe0fa0.x);
}

// (0x0044e660) - Viewer action 0: display mode (rotation + examine confirm)
static void FUN_0044e660(void)
{
    if ((g_PlayerDpadPressed >> 8) & 0x40) {
        if (FUN_0044ed40()) {
            if (DAT_00ae9f4a != 1) {
                return;
            }
            FUN_00454fd0(DAT_00ae9f1b, 0, 0x30 - g_ScreenOffsetX, 0xba - g_ScreenOffsetY);
            return;
        }
        DAT_00ae9f4a = 2;
        // 0x0044e6ce/db/e6 - the examine text comes from the item description
        // table (0x004c6160) through its own setter, NOT from set_message_display:
        // the index here is a raw table index, so feeding it to the room/global
        // message picker showed an unrelated RDT message instead.
        if (DAT_00ae9f1b != 0x6f) {
            if (DAT_00ae9f1b == 0x70) {
                set_item_description_message(0x4e, 0);
                return;
            }
            set_item_description_message(DAT_00ae9f1b - 1, 0);
            return;
        }
        set_item_description_message(0x4d, 0);
        return;
    }
    if ((g_PlayerDpadPressed >> 8) & 0x80) {
        DAT_00ae9f49 = 5;
        DAT_00aea084 = 0x40;
        DAT_00ae9f4c = 0;
        DAT_00ae9f4e = 0;
        DAT_00ae9f50 = 0;
        goto viewer_spin_input;
    }

    // Rotation is NOT applied by bumping the Euler angles directly: those are
    // model-space (RotMatrix builds Rx * Ry * Rz), so only r->y happens to line
    // up with a screen axis and r->x comes out as an in-screen roll. The
    // original instead builds a small incremental rotation from the spin input
    // vector and PRE-multiplies it onto the base matrix (FUN_0044eb30), so the
    // spin axes are always the view's:
    //   spin x (0x00ae9f4c) = roll, y (0x00ae9f4e) = yaw, z (0x00ae9f50) = pitch
    // FUN_0044eb30 then re-extracts 0x00ae9f64/66/68 from the product so the
    // examine combo check still has usable angles.
    {
        unsigned int rawHeld = (g_button_pressed_id >> 8) & 0xff;   // 0x00bf0a0d
        unsigned int dpad = g_PlayerDpadHeld;                       // 0x00be9840

        if (g_viewerPadWord != 0) {
            // 0x0044e722 - shift held: the d-pad drives zoom and roll instead
            if ((dpad & 0x5) != 0) {
                if ((dpad & 0x1) != 0) {
                    DAT_00aea04c = DAT_00aea04c + 0x64;
                    if (DAT_00aea04c > 0x2920) DAT_00aea04c = 0x2920;
                } else {
                    DAT_00aea04c = DAT_00aea04c - 0x64;
                    if (DAT_00aea04c < 0x1980) DAT_00aea04c = 0x1980;
                }
            }
            if ((dpad & 0xa) != 0) {
                DAT_00ae9f4c = ((dpad & 0x8) != 0) ? 0x20 : -0x20;
            } else {
                DAT_00ae9f4c = 0;
            }
            // The original leaves these alone because its recompute gate only
            // fires on the face buttons; the gate below keys off the spin
            // vector, so the other mode's axes have to be released here or a
            // stale value would keep the model turning by itself.
            DAT_00ae9f4e = 0;
            DAT_00ae9f50 = 0;
        } else {
            DAT_00ae9f4c = 0;
            // 0x0044e796 - the four face buttons (the on-screen arrow diamond).
            // Port addition: the d-pad drives the same two axes, so the model
            // can be rotated with the arrow keys as well.
            if ((rawHeld & 0xa0) != 0 || (dpad & 0xa) != 0) {
                DAT_00ae9f4e = ((rawHeld & 0x80) != 0 || (dpad & 0x8) != 0) ? 0x20 : -0x20;
            } else {
                DAT_00ae9f4e = 0;
            }
            if ((rawHeld & 0x50) != 0 || (dpad & 0x5) != 0) {
                DAT_00ae9f50 = ((rawHeld & 0x10) != 0 || (dpad & 0x1) != 0) ? -0x20 : 0x20;
            } else {
                DAT_00ae9f50 = 0;
            }
            // Aim button (raw 0x08) zooms without needing shift
            if ((g_PlayerPadHeld & 0x08) != 0) {
                DAT_00aea04c = DAT_00aea04c - 0x64;
                if (DAT_00aea04c < 0x1980) DAT_00aea04c = 0x1980;
            } else if ((g_PlayerPadHeld & 0x1000) != 0) {
                DAT_00aea04c = DAT_00aea04c + 0x64;
                if (DAT_00aea04c > 0x2920) DAT_00aea04c = 0x2920;
            }
        }
    }
    // 0x0044e7f8 - the original gates the recompute on the pad bits; gating on
    // the spin vector instead covers the d-pad additions above and skips the
    // extract/rebuild round trip on idle frames.
    if (DAT_00ae9f4c != 0 || DAT_00ae9f4e != 0 || DAT_00ae9f50 != 0) {
        FUN_0044eb30();
    }
viewer_spin_input:
    FUN_0044eca0();
}

// (0x0044e820) - Viewer action 1: examine spin-in animation
static void FUN_0044e820(void)
{
    DAT_00aea084 = DAT_00aea084 + 1;
    unsigned char step = DAT_00aea084 * 4;
    if (0x20 < step) {
        step = 0x20;
    }
    DAT_00ae9f5e = (DAT_00ae9f5e + step) & 0xfff;
    DAT_00ae9f56 = (DAT_00ae9f56 + (short)step * -2) & 0xfff;
    if (0x300 < DAT_00ae9f5e) {
        DAT_00ae9f4a = 2;
        set_message_display(g_ItemHealTable[(unsigned int)g_ItemImageLookupTable[(unsigned int)DAT_00ae9f1b * 4 + 2] + 0x51], 0);
        return;
    }
    FUN_00454fd0(0xf00 | DAT_00ae9f1b, 0, 0x30 - g_ScreenOffsetX, 0xba - g_ScreenOffsetY);
}

// (0x0044e8c0) - Viewer action 2: examine message wait
static void FUN_0044e8c0(void)
{
    if ((g_menu_choice_id & 0x80) == 0) {
        if (DAT_00ae9f1b == 0x3e) {
            if (DAT_00ae9f5e != 0) {
                Flg_on((int)g_gameFlags_bc, 0xd);
            }
        } else if (((0x3e < DAT_00ae9f1b) && (DAT_00ae9f1b < 0x41)) && (DAT_00ae9f5e != 0)) {
            DAT_00ae9f4a = 3;
            DAT_00aea084 = 0x40;
            return;
        }
        DAT_00ae9f4a = 0;
    }
}

// (0x0044e920) - Viewer action 3: reload the item image after examining
static void FUN_0044e920(void)
{
    if ((g_menu_choice_id & 0x80) == 0) {
        unsigned char imageType = g_ItemImageLookupTable[(unsigned int)DAT_00ae9f1b * 4];
        if (imageType != 0) {
            LoadItemImage((int)imageType - 1, 0, (int)g_TimImageBuffer__bitmap);
        }
        DAT_00ae9f4a = 0;
    }
}

// Viewer action dispatch table (PTR_FUN_004bf310)
static void (*const g_viewerActions[4])(void) = {
    FUN_0044e660, FUN_0044e820, FUN_0044e8c0, FUN_0044e920
};

// (0x0044e1b0) - Item 3D model viewer animation
// Returns non-zero when the viewer finishes and the menu should continue.
int FUN_0044e1b0(void)
{
    int iVar1;
    unsigned int uVar7;
    unsigned char bVar4;
    unsigned char bVar5;

    // 0x0044e1b5 - FUN_00497e40 is GetAsyncKeyState(VK_SHIFT) reduced to 0/1
    // (0x00497e00: push 0x10, call GetAsyncKeyState, store 1 if nonzero).
    // Shift switches the d-pad from rotate to zoom+roll, so this must be the
    // live key state - the previous constant 0x10 pinned it to "shift held".
    g_viewerPadWord = (GetAsyncKeyState(0x10) != 0) ? 1 : 0;
    switch (DAT_00ae9f49) {
    case 0:
        // Model setup: resolve the model pointers and initialise the joint chain
        if (g_itemModelTmdBase == 0) {
            return 0;   // the model file failed to load; keep the viewer inert
        }
        DAT_00ae9f49 = 1;
        ResolveAnimPointers((unsigned char*)(g_itemModelTmdBase + 4));
        DAT_00ae9f4b = *(unsigned char*)(g_itemModelTmdBase + 8);
        // Sca chain: Sca[2] (root) <- Sca[1] <- Sca[0]
        InitScaMatrix(0, (ScaMatrixData*)&g_viewerScaMatrices[2 * 0x50 / 4]);
        InitScaMatrix((int)&g_viewerScaMatrices[2 * 0x50 / 4], (ScaMatrixData*)&g_viewerScaMatrices[0x50 / 4]);
        InitScaMatrix((int)&g_viewerScaMatrices[0x50 / 4], (ScaMatrixData*)&g_viewerScaMatrices[0]);
        // Items have 1 or 2 objects; never trust the file. The do-while below
        // walks indices [2 - count, 1], so a count outside 1..2 would run off
        // both the joint array and g_viewerObjRot.
        bVar4 = (unsigned char)DAT_00ae9f4b;
        if (bVar4 < 1) bVar4 = 1;
        if (bVar4 > 2) bVar4 = 2;
        bVar5 = 2 - bVar4;
        do {
            bVar4 = bVar4 - 1;
            {
                int uVar6 = bVar5;
                bVar5 = bVar5 + 1;
                *(int*)((BYTE*)g_viewerJoints + uVar6 * 0x14) = 0;
                *(int**)((BYTE*)g_viewerJoints + 4 + uVar6 * 0x14) = (int*)((BYTE*)g_viewerScaMatrices + bVar4 * 0x50);
                *(short*)((BYTE*)&DAT_00ae9f54 + uVar6 * 8) = 0;
                *(short*)((BYTE*)&DAT_00ae9f56 + uVar6 * 8) = 0;
                *(short*)((BYTE*)&DAT_00ae9f58 + uVar6 * 8) = 0;
                *(int*)((BYTE*)g_viewerScaMatrices + 0x18 + uVar6 * 0x50) = 0;
                *(int*)((BYTE*)g_viewerScaMatrices + 0x1c + uVar6 * 0x50) = 0;
                *(int*)((BYTE*)g_viewerScaMatrices + 0x20 + uVar6 * 0x50) = 0;
            }
        } while (bVar4 != 0);
        DAT_00aea04c = -0xaf00; // 0x0044e2ac - start far away; the intro closes in
        DAT_00ae9f64 = 0;       // 0x0044e2b8
        DAT_00aea050 = 0;
        DAT_00ae9f66 = 0;       // 0x0044e2c3
        DAT_00aea054 = 0;
        // 0x0044e2ce - the resting pose is the identity rotation. The item is
        // framed by the menu camera main_menu installs (from (15000,0,0) toward
        // the origin), not by a baked rotation here.
        DAT_00ae9f68 = 0;
        DAT_00ae9f4c = 0;
        DAT_00ae9f4e = 0;
        DAT_00ae9f50 = 0;
        RotMatrix((SVECTOR*)&DAT_00ae9f64, (MATRIX*)VIEWER_BASE_MATRIX);
        DAT_00aea084 = 0x40;
        // fall through
    case 1:
        // Intro animation (0x0044e2ff-0x0044e31b): close in while spinning.
        // 0x40 frames x 0xc0 of yaw = 0x3000 = three full turns, and 0x40 x
        // 0x80 of roll = 0x2000 = two full turns, so both angles land back on
        // 0 and the model comes to rest in its identity pose.
        DAT_00aea04c = DAT_00aea04c + 0x322;
        DAT_00ae9f66 = DAT_00ae9f66 + 0xc0;
        DAT_00ae9f68 = DAT_00ae9f68 + 0x80;
        DAT_00aea084 = DAT_00aea084 - 1;
        if (DAT_00aea084 != 0) goto viewer_draw;
        switch (DAT_00ae9f10) {
        case 0:
            DAT_00ae9f49 = 2;
            DAT_00ae9f4a = 0;
            goto viewer_draw;   // no message for the plain status-screen viewer
        case 3:
            g_selectedItemId = DAT_00ae9f1b;
            DAT_00ae9f49 = 3;
            if (g_TotalHeldItems < g_totalInventorySlots) {
                uVar7 = 0xc0;
            } else {
                if (((10 < DAT_00ae9f1b) && (DAT_00ae9f1b < 0x13)) || (DAT_00ae9f1b == 0x2f)) {
                    bVar5 = 0;
                    bVar4 = g_totalInventorySlots;
                    do {
                        unsigned char* pbVar2 = (unsigned char*)((unsigned int)bVar5 * 2 + (unsigned int)g_ItemSlotsPointer);
                        if ((*pbVar2 == DAT_00ae9f1b) &&
                            ((unsigned short)((unsigned short)pbVar2[1] +
                             (unsigned short)*(unsigned char*)(*(int*)((int)g_room_event_index + 8) + 9)) < 0xfb)) {
                            set_message_display(0xc0, 0);
                            break;
                        }
                        bVar4 = bVar4 - 1;
                        bVar5 = bVar5 + 1;
                    } while (bVar4 != 0);
                }
                uVar7 = 0xc2;
            }
            break;
        case 4:
            uVar7 = 0xc1;
            g_selectedItemId = DAT_00ae9f1b;
            DAT_00ae9f49 = 3;
            break;
        case 6:
            DAT_00ae9f49 = 4;
            uVar7 = 199;
            break;
        default:
            goto viewer_draw;
        }
        set_message_display(uVar7, 0);
viewer_draw:
        g_spriteAnimB = 0x20 - (DAT_00aea084 >> 1);
        // 0x0044e538: intro colour fades in as 0xFF - (DAT_00aea084 << 2)
        viewer_setup_lights(0xff - (DAT_00aea084 << 2));
        break;

    case 2:
        // Display mode: rotation input + examine confirm (action table)
        g_viewerActions[DAT_00ae9f4a]();
        break;

    case 3:
        goto viewer_state3;
    case 4:
        DAT_00ae9f66 = DAT_00ae9f66 - 0x10;
viewer_state3:
        if ((g_menu_choice_id & 0x80) == 0) {
            DAT_00ae9f49 = 5;
            DAT_00aea084 = 0x40;
            if (DAT_00ae9f10 != 4) {
                if ((g_menu_choice_id & 1) == 0) {
                    uVar7 = 6;
                } else {
                    uVar7 = 5;
                }
            } else {
                FUN_004631c0();
                break;
            }
        } else {
            if ((DAT_00ae9f10 != 4) && (*g_MessageCurrentPtr != 8)) break;
            if (((g_PlayerPadHeld >> 8) & 0xa0) == 0) break;
            uVar7 = 4;
        }
        play_sfx(3, uVar7, 0);
        break;

    case 5:
        // Exit animation (0x0044e397-0x0044e3ba): mirrors the intro
        DAT_00aea084 = DAT_00aea084 - 1;
        if (DAT_00aea084 == 0) {
            return 1;
        }
        DAT_00aea04c = DAT_00aea04c - 0x322;
        DAT_00ae9f66 = DAT_00ae9f66 - 0xc0;
        DAT_00ae9f68 = DAT_00ae9f68 - 0x80;
        g_spriteAnimB = DAT_00aea084 >> 1;
        // 0x0044e3d0: exit colour fades out as (DAT_00aea084 << 2) - 1
        viewer_setup_lights((DAT_00aea084 << 2) - 1);
        break;
    }

    if (DAT_00ae9f49 != 0) {
        FUN_0044ea50();
    }
    if ((DAT_00ae9f10 == 0) && (DAT_00ae9f4a == 0)) {
        FUN_00454fd0(DAT_00ae9f1b, 0, 0x30 - g_ScreenOffsetX, 0xba - g_ScreenOffsetY);
    }
    return 0;
}

// (0x00484420) - Process texture for menu item model
// src: the loaded .ivm file buffer; dst: unused in the port (the original
// fixed buffers happened to sit exactly a TIM image apart). The TMD header
// inside the .ivm starts right after the TIM image, so the async processor
// receives the TMD base computed from the TIM header.
static void FUN_004841f0(void);
void FUN_00484420(void* src, void* dst)
{
    g_itemModelSrc = (int)src;
    unsigned char* p = (unsigned char*)src;
    g_itemModelTmdBase = 0;
    // Validate the .ivm TIM header before trusting its lengths
    if (p != 0 && *(int*)p == 0x10 && (*(int*)(p + 4) & 7) <= 2) {
        int clutLen = *(int*)(p + 8);
        int imgLen = *(int*)(p + 8 + clutLen);
        g_itemModelTmdBase = (int)(p + 8 + clutLen + imgLen);
    }
    ExecAsync((void*)FUN_004841f0);
}

// ============================================================================
// Item model processor (0x004841f0) and viewer renderer
// ============================================================================

// (0x0040a990) - GTE-style atan2, returning the angle in the game's
// 0x1000 = 360 deg units (the same scale RotMatrix consumes).
// Original: fpatan * (180/pi) * (2048/180); the two literals are the doubles
// at 0x004af050 (57.29577791868204) and 0x004af058 (11.377777777777778).
// A previous revision used 24.7555... there, which came out 2.18x too large -
// the angles the examine recompute extracts from the base matrix are fed
// straight back into RotMatrix, so any scale error compounds every frame.
static int FUN_0040a990(int y, int x)
{
    double rad = atan2((double)y, (double)x);
    return (int)(rad * 57.29577791868204 * 11.377777777777778);
}

// (0x0040a530) - GTE-style fixed-point square root: fmul by 1/4096 (0x004af028),
// fsqrt, fmul by 4096.0 (0x004af030). Input and output are both 20.12 fixed
// point, i.e. sqrt(x * 4096) - not the plain integer sqrt.
static int FUN_0040a530(int value)
{
    if (value < 0) return 0;
    return (int)(sqrt((double)value * 0.000244140625) * 4096.0);
}

// (0x0040a250) - Transpose a MATRIX (3x3 shorts) into a 9-short buffer
// The original reads the source in column order (6-byte strides) and writes
// the rows consecutively, i.e. dst = src^T.
static void FUN_0040a250(MATRIX* src, MATRIX* dst)
{
    for (int j = 0; j < 3; j++) {
        for (int i = 0; i < 3; i++) {
            ((short*)dst)[j * 3 + i] = src->m[i][j];
        }
    }
}

// (0x00483580) - Compose the viewer joint chain matrices into the output
// The "joint" argument is the ScaMatrixData chain root; the chain is linked
// through the OWNER back-pointers (ScaMatrixData.owner at +0x48, set by
// InitScaMatrix), with each parent's child back-ref at +0x4C. The walk
// follows the owners to the root and composes local -> world from the root
// down, exactly like the original.
static void FUN_00483580(int* joint, MATRIX* out)
{
    int* chain[0x14];
    int chainCount = 0;
    int* p = joint;
    while (p != 0 && chainCount < 0x14) {
        chain[chainCount++] = p;
        p = (int*)p[0x12];   // owner (parent)
    }

    // Compose from the deepest (root) down to the given joint
    for (int i = chainCount - 1; i >= 0; i--) {
        int* j = chain[i];
        if (j[0x12] == 0) {
            for (int k = 0; k < 8; k++) {
                j[9 + k] = j[1 + k];
            }
        } else {
            CompMatrix((MATRIX*)(j[0x12] + 0x24), (MATRIX*)(j + 1), (MATRIX*)(j + 9));
        }
    }
    // 0x004835ee - compose against the camera. main_menu (0x004638e3) installs
    // a menu camera at (15000,0,0) looking at the origin, so g_RoomCameraData
    // here is a 90 deg rotation about Y with t = (0,0,15000): it maps the root
    // Sca's +X translation ("zoom") onto view depth (15000 - zoom) and leaves
    // world Y on the screen vertical, which is what makes the base rotation's
    // y component read as a turntable.
    CompMatrix(&g_RoomCameraData, (MATRIX*)(joint + 9), out);
}

// (0x004846d0) - Queue the item model draw for the given render slot
static void FUN_004846d0(int slot)
{
    g_itemRenderSlot = slot;
    ExecAsync((void*)FUN_004844c0);
}

// (0x004844c0) - Async item model draw: build the transform matrix from the
// GTE buffer and call CMarniDirect3DTMD::Transform on the item model slot.
static void FUN_004844c0(void)
{
    int slot = g_itemRenderSlot;

    // Build the 4x4 float matrix from the GTE rotation/translation buffer
    // (same layout as the entity render path).
    float transformMatrix[16];
    float scale = 0.00024414063f; // 1/4096
    transformMatrix[0]  = (float)g_gteRotTransMatrix.m[0][0] * scale;
    transformMatrix[4]  = (float)g_gteRotTransMatrix.m[0][1] * scale;
    transformMatrix[8]  = (float)g_gteRotTransMatrix.m[0][2] * scale;
    transformMatrix[1]  = (float)g_gteRotTransMatrix.m[1][0] * scale;
    transformMatrix[5]  = (float)g_gteRotTransMatrix.m[1][1] * scale;
    transformMatrix[9]  = (float)g_gteRotTransMatrix.m[1][2] * scale;
    transformMatrix[2]  = (float)g_gteRotTransMatrix.m[2][0] * scale;
    transformMatrix[6]  = (float)g_gteRotTransMatrix.m[2][1] * scale;
    transformMatrix[10] = (float)g_gteRotTransMatrix.m[2][2] * scale;
    transformMatrix[12] = (float)g_gteRotTransMatrix.t[0];
    transformMatrix[13] = (float)g_gteRotTransMatrix.t[1];
    transformMatrix[14] = (float)g_gteRotTransMatrix.t[2];
    transformMatrix[3]  = 0.0f;
    transformMatrix[7]  = 0.0f;
    transformMatrix[11] = 0.0f;
    transformMatrix[15] = 1.0f;

    // The view distance already comes through t[2]: the menu camera contributes
    // t = (0,0,15000) and rotates the root Sca's +X "zoom" onto -Z, so the depth
    // is 15000 - zoom - 59800 at the start of the intro, 8472 at rest, and it
    // never crosses the camera plane.

    int depth = slot + 10;
    if (slot >= 3) slot = 0;
    int tr = ((CMarniDirect3DTMD*)g_itemTmdSlots[slot])->Transform(g_pMarniDirect3D, (void*)(size_t)depth, transformMatrix, 0);
    if (tr == 0) {
        OutputDebugStringA("[ITEM] Transform skipped (slot not initialized)\n");
    } else {
        char dbg[160];
        sprintf_s(dbg, sizeof(dbg), "[ITEM] transform ok tz=%f\n", transformMatrix[14]);
        OutputDebugStringA(dbg);
    }

    if ((slot == 0) && (g_itemSharedDisplayFlag == 1)) {
        ((CMarniDirect3DTMD*)g_itemSharedTmdSlot)->Transform(g_pMarniDirect3D, (void*)(size_t)10, transformMatrix, 0);
    }
}

// (0x0044ea50) - Viewer render: compute the model matrices and queue the draw
static void FUN_0044ea50(void)
{
    DAT_00ae9f64 = DAT_00ae9f64 & 0xfff;
    DAT_00ae9f66 = DAT_00ae9f66 & 0xfff;
    DAT_00ae9f68 = DAT_00ae9f68 & 0xfff;
    // Base (spin) rotation into Sca[2].localMatrix (0xaea038)
    RotMatrix((SVECTOR*)&DAT_00ae9f64, (MATRIX*)((BYTE*)g_viewerScaMatrices + 2 * 0x50 + 4));
    *(int*)((BYTE*)g_viewerScaMatrices + 2 * 0x50) = 0;

    unsigned char local_1 = (unsigned char)DAT_00ae9f4b;
    if (local_1 < 1) local_1 = 1;   // never trust the model object count
    if (local_1 > 2) local_1 = 2;
    unsigned char local_2 = 2;
    do {
        local_1 = local_1 - 1;
        local_2 = local_2 - 1;
        {
            int uVar1 = local_2;
            // Per-object rotation into Sca[uVar1].localMatrix
            RotMatrix((SVECTOR*)((BYTE*)&DAT_00ae9f54 + uVar1 * 8),
                      (MATRIX*)((BYTE*)g_viewerScaMatrices + 4 + uVar1 * 0x50));
            *(int*)((BYTE*)g_viewerScaMatrices + uVar1 * 0x50) = 0;
            // Compose the Sca chain from the joint's matrix
            int* joint = *(int**)((BYTE*)g_viewerJoints + 4 + uVar1 * 0x14);
            // The compose already carries the zoom into t[2] via the camera.
            FUN_00483580(joint, &g_viewerMatrixBe0f60);
            // 0x0044eaef - multAndSetLightMatrix(&g_RoomCameraData)
            MATRIX lightM;
            MulMatrix0(&g_lightMatrix, &g_RoomCameraData, &lightM);
            SetLightMatrix(&lightM);
            SetRotAndTransMatrix(&g_viewerMatrixBe0f60);
            FUN_004846d0(1 - uVar1);
        }
    } while (local_1 != 0);
}

// (0x004631c0) - Use the desk item (stub pending)
void FUN_004631c0(void) { }

// (0x004841f0) - Item model async processor
// Parses the .ivm file loaded by menu_load_item_model: creates the item
// texture page (bank 0x15) and the Direct3DTMD slot(s) from the TMD header.
static void FUN_004841f0(void)
{
    int local_10 = *(int*)(g_itemModelTmdBase + 8);   // TMD object count
    g_itemModelTmdCount = 0;
    if (g_itemModelTmdBase == 0) return;
    if (local_10 < 1 || local_10 >= 3) return;

    ResolveAnimPointers((unsigned char*)(g_itemModelTmdBase + 4));
    VideoDriver_ClearState348(g_pMarniDirect3D, g_pMarniDirect3D);
    VideoDriver_ClearState348(g_pMarniDirect3D, g_pMarniDirect3D);

    BYTE* itemPage = &g_psxTextureArray[0x15 * 0x1b60];

    if (g_itemModelTexCreated == 0) {
        LoadPSXImage((PSXTexture*)itemPage, (void*)g_itemModelSrc, 1);
        Direct3DTIM_Create(itemPage, g_pMarniDirect3D);
    }

    if (CheckTmdTransparency(g_itemModelTmdBase + 0xc) != 0) {
        g_itemModelBlendFlag = 0x3f000000;
        if (g_itemSharedTmdReady != 1) {
            LoadPSXImage((PSXTexture*)itemPage, (void*)g_itemModelSrc, 1);
            // Shared transparent TMD slots (DAT_008f8c48, set up by the map
            // screen): reload their texture handles. The count is 0 until the
            // map screen is ported, so this loop is inert.
            for (int i = 0; i < g_itemSharedTmdCount; i++) {
                ((CMarniDirect3DTMD*)g_itemSharedTmdSlot)->CleanupObjects(g_pMarniDirect3D);
            }
            void** vtable = *(void***)g_pMarniDirect3D;
            typedef DWORD (*CreateTexFn)(void*, void*, int, int);
            CreateTexFn createTex = (CreateTexFn)vtable[6];
            for (int i = 0; i < g_itemSharedTmdCount; i++) {
                g_itemSharedTmdHandles[i] = createTex(g_pMarniDirect3D, g_itemSharedTmdSlot, 0x21, 0);
            }
            g_itemSharedTmdReady = 1;
        }
    }

    for (int i = 0; i < local_10; i++) {
        // Each TMD object is stored into its own slot (0x008f8d88 + i*0x1594
        // in the original); a previous revision reused one slot for all
        // objects, so multi-object items only ever rendered their last object.
        CMarniDirect3DTMD* itemSlot = (CMarniDirect3DTMD*)g_itemTmdSlots[i];
        itemSlot->CleanupObjects(g_pMarniDirect3D);
        int st = PSXObject_Store(itemSlot, (int*)g_itemModelTmdBase, i, 0xffffffff, 0x100);
        int cr = itemSlot->Create(g_pMarniDirect3D, itemPage, (void*)1);
        char dbg[160];
        sprintf_s(dbg, sizeof(dbg), "[ITEM] store=%d create=%d tmd=%p count=%d obj=%d\n", st, cr, (void*)g_itemModelTmdBase, local_10, i);
        OutputDebugStringA(dbg);
    }
    g_itemModelTmdCount = local_10;

    // NOTE: no CleanupObjects here - it would reset the slot's m_initialized
    // flag and stop the viewer's Transform from queuing the model.
    if (g_itemSharedTmdReady == 1) {
        PSXObject_Store((CMarniDirect3DTMD*)g_itemSharedTmdSlot, (int*)g_itemModelTmdBase, 0, 0xffffffff, 0x100);
        ((CMarniDirect3DTMD*)g_itemSharedTmdSlot)->Create(g_pMarniDirect3D, itemPage, (void*)1);
    }
}

// (0x00420b80) - Update selected menu item cursor. NOW IMPLEMENTED in
// MainMenu.cpp (menu_update_selected_item) together with the rest of the
// main menu input handling (0x00420880 menu_handle_input, 0x00420bb0
// menu_item_submenu, 0x00420a70 menu_draw_cursor).

// (0x00454fd0) - Draw item name text at position. NOW IMPLEMENTED in
// MainMenu.cpp together with the rest of the main menu input handling.

// FUN_004387e0 is now a static function in MainMenu.cpp

// (0x00470c60) - Queue EKG line primitive (primary line)
// Builds a line primitive from the 16-byte EKG line struct and inserts it
// into the ordering table with OT depth `param_2`. The original wrote the
// primitive into the DAT_008e3e60 render buffer and called the
// CMarniDirect3D vtable[10] entry (SetTexture == OT_InsertPrimitive); the
// port submits the same line into the sprite command queue instead.
// Returns 1 when submitted, 0 when the per-frame primitive cap is reached.
int FUN_00470c60(void* prim, int depth)
{
    if (g_renderPrimCount >= 0x28) return 0;

    unsigned char* p = (unsigned char*)prim;
    unsigned short depthOut = (unsigned short)depth;

    // Software-renderer modes offset the OT depth by 0x28.
    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (pD3D && (pD3D->m_deviceType == 5 || pD3D->m_deviceType == 7)) {
        depthOut = depthOut + 0x28;
    }

    short x0 = *(short*)(p + 4);
    short y0 = *(short*)(p + 6);
    short x1 = *(short*)(p + 8);
    short y1 = *(short*)(p + 10);
    float r = (float)p[0xC] * 0.00390625f;
    float g = (float)p[0xD] * 0.00390625f;
    float b = (float)p[0xE] * 0.00390625f;

    if (g_nFadeInverted != 0) {
        if (g_MaxFadeValue < (int)depthOut) depthOut = (unsigned short)g_MaxFadeValue;
        depthOut = (unsigned short)(g_MaxFadeValue - (int)depthOut);
    }

    if ((g_RenderDisableFlags & 0x10) == 0) {
        SubmitLine(x0, y0, x1, y1, depthOut, r, g, b, 1.0f);
        g_renderPrimCount++;
    }
    return 1;
}

// (0x00470e60) - Queue EKG line primitive (secondary line with gradient)
// Identical to FUN_00470c60 but the line struct carries a second color
// endpoint at bytes 0xF-0x11 (the gradient target computed by FUN_00438800).
int FUN_00470e60(void* prim, int depth)
{
    if (g_renderPrimCount >= 0x28) return 0;

    unsigned char* p = (unsigned char*)prim;
    unsigned short depthOut = (unsigned short)depth;

    CMarniDirect3D* pD3D = (CMarniDirect3D*)g_pMarniDirect3D;
    if (pD3D && (pD3D->m_deviceType == 5 || pD3D->m_deviceType == 7)) {
        depthOut = depthOut + 0x28;
    }

    short x0 = *(short*)(p + 4);
    short y0 = *(short*)(p + 6);
    short x1 = *(short*)(p + 8);
    short y1 = *(short*)(p + 10);
    float r = (float)p[0xC] * 0.00390625f;
    float g = (float)p[0xD] * 0.00390625f;
    float b = (float)p[0xE] * 0.00390625f;

    if (g_nFadeInverted != 0) {
        if (g_MaxFadeValue < (int)depthOut) depthOut = (unsigned short)g_MaxFadeValue;
        depthOut = (unsigned short)(g_MaxFadeValue - (int)depthOut);
    }

    if ((g_RenderDisableFlags & 0x10) == 0) {
        SubmitLine(x0, y0, x1, y1, depthOut, r, g, b, 1.0f);
        g_renderPrimCount++;
    }
    return 1;
}

// (0x00438800) - EKG secondary line gradient update
// Steps the secondary line's gradient color toward the primary color and
// queues the line. cVar1 = (X0 - X1) from the EKG secondary line struct;
// each gradient channel is extrapolated by (X0 - X1) * param.
int FUN_00470e60(void* prim, int depth);
void FUN_00438800(int a, int b, int c)
{
    unsigned char* line = g_EkgSecondaryLine;   // 0x00be1184
    char x0 = *(char*)(line + 4);               // DAT_00be1188
    char x1 = *(char*)(line + 8);               // DAT_00be118c
    char delta = x0 - x1;
    line[0xF] = (unsigned char)(line[0xC] - delta * (char)a);   // DAT_00be1193
    line[0x10] = (unsigned char)(line[0xD] - delta * (char)b);  // DAT_00be1194
    line[0x11] = (unsigned char)(line[0xE] - delta * (char)c);  // DAT_00be1195
    FUN_00470e60(line, 10);
    line[0xC] = line[0xF];
    line[0xD] = line[0x10];
    line[0xE] = line[0x11];
}

// (0x00482c50) - Status screen texture setup
void FUN_00482c50(void) { }

// (0x00482800) - Status screen input check
int  FUN_00482800(int param) { return 0; }

// (0x00482250) - Status screen update sub
void FUN_00482250(void* data) { }

// (0x00482be0) - Status screen init sub
void FUN_00482be0(void* data) { }

// (0x00481ab0) - File (save/load) dialog sub-function
void FUN_00481ab0(void* data) { }

// (0x00443040) - Load item box item image
void FUN_00443040(int imgType, int slot) { }

// (0x00452c50) - options_key_config_handler now in OptionsMenu.cpp
// (0x00451960) - options_display_config_handler now in OptionsMenu.cpp
// (0x00453a80) - options_joystick_config_handler now in OptionsMenu.cpp
// (0x00451900) - options_input_repeat now in OptionsMenu.cpp
// (0x00476b00) - options_menu_exit now in OptionsMenu.cpp
// (0x00476b40) - options_menu_render now in OptionsMenu.cpp
// (0x00477230) - options_init_keybind_display now in OptionsMenu.cpp

// (0x004828f0) - Status screen init
void menu_init_status_screen(void) { }

// (0x00482910) - Status screen update
int  menu_update_status_screen(void) { return 0; }

// (0x00482b80) - Status screen reset
void menu_reset_status_state(void) { }

// (0x004941f0) - Item box interaction
int  menu_itembox_interaction(void) { return 0; }

// (0x00494730) - Load menu item box assets
void loadMenuAssets(void) { }

// (0x004947c0) - Draw item box menu
void draw_itembox_menu(void) { }

// --- Options menu dependency stubs (entity rendering system) ---

// (0x0047f870) - 3-param play_sfx overload (mode parameter)
void play_sfx(int bank, int soundId, int mode) { play_sfx(bank, soundId); }

// (0x00483250) - Entity sprite rendering helper
// Forwards joint sprite data and depth shift to the TMD renderer.
void FUN_00483250(int p0, int p1, int p2, int p3, int p4, int p5, void* p6)
{
    // Assembly: MOV EAX,[ESP+0x18]; MOV ECX,[ESP+0x10]; PUSH EAX; PUSH ECX; CALL FUN_00483080
    FUN_00483080((void*)p3, p5);
}

// (0x0048cc50) - Build view matrix from eye/target positions
static unsigned int FUN_0048cc50(float* eyeTarget, float* eyePos, float* outMatrix)
{
    float dx = eyePos[0] - eyeTarget[0];
    float dy = eyePos[1] - eyeTarget[1];
    float dz = eyePos[2] - eyeTarget[2];
    float len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (len == 0.0f) len = 1.0f;
    float invLen = 1.0f / len;
    float ny = -(dy * invLen);
    float horiz = sqrtf(1.0f - ny * ny);
    float nx, nz;
    if (horiz == 0.0f) {
        nx = 0.0f;
        nz = 1.0f;
    } else {
        nx = -((dx * invLen) / horiz);
        nz = (dz * invLen) / horiz;
    }
    outMatrix[0] = nz;      outMatrix[4] = 0.0f;  outMatrix[8]  = nx;
    outMatrix[1] = -(nx * ny); outMatrix[5] = horiz; outMatrix[9]  = nz * ny;
    outMatrix[2] = -(nx * horiz); outMatrix[6] = -ny; outMatrix[10] = nz * horiz;
    outMatrix[12] = outMatrix[0] * dx + outMatrix[8] * dz;
    outMatrix[13] = outMatrix[1] * dx + outMatrix[5] * dy + outMatrix[9] * dz;
    outMatrix[14] = outMatrix[2] * dx + outMatrix[6] * dy + outMatrix[10] * dz;
    outMatrix[3] = 0.0f; outMatrix[7] = 0.0f; outMatrix[11] = 0.0f; outMatrix[15] = 1.0f;
    return 1;
}

// (0x0048c730) - 4x4 matrix multiply (rotation part only, 3x3)
static void FUN_0048c730(float* a, float* b, float* out)
{
    out[0]  = a[0]*b[0] + a[1]*b[4] + a[2]*b[8];
    out[1]  = a[0]*b[1] + a[1]*b[5] + a[2]*b[9];
    out[2]  = a[0]*b[2] + a[1]*b[6] + a[2]*b[10];
    out[4]  = a[4]*b[0] + a[5]*b[4] + a[6]*b[8];
    out[5]  = a[4]*b[1] + a[5]*b[5] + a[6]*b[9];
    out[6]  = a[4]*b[2] + a[5]*b[6] + a[6]*b[10];
    out[8]  = a[8]*b[0] + a[9]*b[4] + a[10]*b[8];
    out[9]  = a[8]*b[1] + a[9]*b[5] + a[10]*b[9];
    out[10] = a[8]*b[2] + a[9]*b[6] + a[10]*b[10];
}

// (0x0048c820) - Transform translation vector by rotation matrix
static void FUN_0048c820(float* translation, float* rotMatrix)
{
    float x = translation[0], y = translation[1], z = translation[2];
    translation[0] = rotMatrix[0]*x + rotMatrix[4]*y + rotMatrix[8]*z;
    translation[1] = rotMatrix[1]*x + rotMatrix[5]*y + rotMatrix[9]*z;
    translation[2] = rotMatrix[2]*x + rotMatrix[6]*y + rotMatrix[10]*z;
}

// (0x00486190) - Camera/projection matrix setup
// Builds view matrix from camera parameters and composites with the model matrix.
// The original builds the two input vectors as
//   from = (0, 0, -g_sceneRenderParam)
//   to   = (0xA0 - subpixelX, subpixelY - 0x78, 0)
// so the direction handed to FUN_0048cc50 (to - from) has a POSITIVE Z of
// g_sceneRenderParam. An earlier revision folded -g_sceneRenderParam into `to`
// and left `from` at the origin, which negated the Z axis (an extra 180 degree
// yaw) and only happened to look right while the subpixel offset was exactly
// the screen centre.
static void FUN_00486190(float* modelMatrix)
{
    float from[3];
    from[0] = 0.0f;
    from[1] = 0.0f;
    from[2] = (float)-g_sceneRenderParam;

    float to[3];
    to[0] = (float)(0xA0 - g_SubpixelOffsetX);
    to[1] = (float)(g_SubpixelOffsetY + (-0x78));
    to[2] = 0.0f;

    float viewMatrix[16];
    FUN_0048cc50(from, to, viewMatrix);
    FUN_0048c730(modelMatrix, viewMatrix, modelMatrix);
    FUN_0048c820(modelMatrix + 12, viewMatrix);
}

// (0x00482fa0) - Copy light data to TMD render object and insert into ordering table
static void FUN_00482fa0(void* spriteData, int depthShift)
{
    if (spriteData == NULL || g_gteRotTransMatrix.t[2] < 0) return;

    int depth = g_gteRotTransMatrix.t[2] >> (depthShift & 0x1F);
    int* data = (int*)spriteData;
    float* lightDst = (float*)((unsigned char*)data + 0x24);
    DWORD* pLight = g_d3dLightData;

    for (int i = 0; i < 3; i++) {
        memcpy(lightDst, pLight, 12 * sizeof(DWORD));
        if (data[6] != 0) {
            lightDst[6] = (float)((data[6] & 0xFF0000) >> 16);
            lightDst[7] = (float)((data[6] >> 8) & 0xFF);
            lightDst[8] = (float)(data[6] & 0xFF);
        }
        OT_InsertPrimitive(lightDst, depth);
        pLight += 12;
        lightDst += 12;
    }
}

// (0x00483080) - Main TMD entity render function
// Reads GTE state buffers, creates TMD object, builds transform matrix, renders
void FUN_00483080(void* spriteData, int depthShift)
{
    int depthField = g_gteRotTransMatrix.t[2];
    if (spriteData == NULL || depthField < 0) {
        return;
    }

    int depth = depthField >> (depthShift & 0x1F);
    int* data = (int*)spriteData;

    FUN_00482fa0(spriteData, depthShift);

    // data[1] is the minimum CLUT depth of the animation slot (FindMinClutDepth)
    // and doubles as the texture bank id; zero means the object carries no
    // textured primitives and is not rendered.
    if (data[1] == 0) {
        return;
    }

    if (data[4] == 1) {
        FUN_00486df0(spriteData);
        return;
    }

    unsigned int tmdObj = AsyncCreateTmdObject(data[1], data[0], (unsigned int)spriteData);
    data[8] = tmdObj;
    if (tmdObj == 0) {
        return;
    }

    // Build 4x4 transform matrix from GTE rotation/translation buffer.
    // Column layout, matching the original store order at 0x004830ef:
    // GTE row 0 (m[0][0..2]) lands in M[0], M[4], M[8], so the consumer reads
    // a transformed X as M[0]*x + M[4]*y + M[8]*z. An earlier revision wrote
    // m[0][1] to M[1] etc., i.e. the transposed (inverse) rotation.
    float transformMatrix[16];
    float scale = 0.00024414063f; // 1/4096

    transformMatrix[0]  = (float)g_gteRotTransMatrix.m[0][0] * scale;
    transformMatrix[4]  = (float)g_gteRotTransMatrix.m[0][1] * scale;
    transformMatrix[8]  = (float)g_gteRotTransMatrix.m[0][2] * scale;
    transformMatrix[1]  = (float)g_gteRotTransMatrix.m[1][0] * scale;
    transformMatrix[5]  = (float)g_gteRotTransMatrix.m[1][1] * scale;
    transformMatrix[9]  = (float)g_gteRotTransMatrix.m[1][2] * scale;
    transformMatrix[2]  = (float)g_gteRotTransMatrix.m[2][0] * scale;
    transformMatrix[6]  = (float)g_gteRotTransMatrix.m[2][1] * scale;
    transformMatrix[10] = (float)g_gteRotTransMatrix.m[2][2] * scale;
    transformMatrix[12] = (float)g_gteRotTransMatrix.t[0];
    transformMatrix[13] = (float)g_gteRotTransMatrix.t[1];
    transformMatrix[14] = (float)depthField;
    transformMatrix[3]  = 0.0f;
    transformMatrix[7]  = 0.0f;
    transformMatrix[11] = 0.0f;
    transformMatrix[15] = 1.0f;

    FUN_00486190(transformMatrix);

    // Call CMarniDirect3DTMD::Transform(ctx, depth, matrix, doubleBuffer=0)
    // Original: Direct3DTMD_Transform(g_pMarniDirect3D, iVar2, &local_40, 0)
    // (ECX = [spriteData+0x20] = the TMD object handle; depth is the OT depth,
    // NOT the matrix — an earlier revision passed the matrix as arg 2, which
    // left every object with a garbage depth and no stored transform).
    CMarniDirect3DTMD* tmd = (CMarniDirect3DTMD*)(void*)tmdObj;
    tmd->Transform(g_pMarniDirect3D, (void*)(size_t)depth, transformMatrix, 0);
}

// (0x004896c0) - Entity motion/animation step
void FUN_004896c0(void* joint, short p1, short p2, int p3) { }

// (0x0048a210) - Entity path animation step
void FUN_0048a210(void* joint) { }

// (0x00482e20) - Set light matrix for entity rendering
// Transforms light direction vectors through camera matrix and stores
// in D3D light data buffer for the rendering pipeline.
void SetLightMatrix(MATRIX* m) {
    // The original copies 32 bytes (8 iterations × 4 bytes) from the input matrix
    // to a local buffer, then iterates 3 light directions from it.
    // The buffer must be at least 32 bytes (the loop writes 8 DWORDs).
    MATRIX srcCopy;
    memcpy(&srcCopy, m, sizeof(MATRIX));

    int lightIndex = 0;
    DWORD* pLight = g_d3dLightData;
    // The original iterates 3 times over SVECTORs at 6-byte offsets starting from srcCopy
    short* pVec = &srcCopy.m[0][0];

    for (int light = 0; light < 3; light++) {
        SVECTOR vec;
        vec.x = pVec[0];
        vec.y = pVec[1];
        vec.z = pVec[2];
        vec.pad = 0;

        ApplyMatrixSV((MATRIX*)&g_RoomCameraData, &vec, &vec);

        pLight[0] = 2;  // directional light type
        pLight[2] = lightIndex;
        lightIndex++;
        float* pF = (float*)&pLight[3];
        pF[0] = (float)(int)vec.x * 0.00024414063f;
        pF[1] = (float)(int)vec.y * 0.00024414063f;
        pF[2] = (float)(int)vec.z * 0.00024414063f;
        pLight[9] = 0x3f800000; // 1.0f
        pLight[10] = 0;
        pLight += 0xC;
        pVec += 3; // advance by 6 bytes (3 shorts)
    }

    g_d3dLightFlags |= 1;
    g_d3dAmbientColor = ((unsigned int)g_green_color << 8) |
                        ((unsigned int)g_red_color << 16) |
                        (unsigned int)g_blue_color;
}

// (0x00482df0) - Copy rotation+translation matrix to GTE state buffer
void SetRotAndTransMatrix(MATRIX* m) {
    memcpy(&g_gteRotTransMatrix, m, sizeof(MATRIX));
    g_gteRotTransMatrix.t[1] = -g_gteRotTransMatrix.t[1];
}

// is_entity_in_switch_zone (0x00462d90) is implemented in Room.cpp.
// The placeholder that used to live here unconditionally returned 1, which made
// every entity test as "inside every zone" — check_camera_switch would then have
// latched onto the first zone of the group instead of the one the player is in.

// (0x00497de0) - Keyboard scancode read (async)
unsigned char FUN_00497de0(void) { return 0; }

// (0x00486df0) - Sprite rendering mode (used when spriteData[4]==1)
void FUN_00486df0(void* spriteData) { }




