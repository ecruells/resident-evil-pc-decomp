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
extern void FUN_0048bda0(void);                                                   // 0x0048bda0

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

// (0x00494d90) - Update player animation state machine
void update_player_anim(void) { }

// (0x0041c060) - Update player position from speed/angle
void update_player_position(PlayerEntity* ent, int a) { }

// (0x00456d30) - Draw screen fade sprite overlay
void DrawFadeSpr(void) { }

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
// light. Lights with zero2 == 0 are point lights with radial falloff
// (direction = light->entity, color attenuated by distance); the others are
// used as-is (directional).
void update_entity_lighting(VECTOR* entityPos)
{
    if (g_RdtPointer == NULL) return;

    for (int i = 0; i < 3; i++) {
        RDT_Light* light = &g_RdtPointer->lights[i];
        if (light->zero2 == 0) {
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

// (0x004813c0) - Restore room state after menu close
void FUN_004813c0(void) { }

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

    // FUN_0040a5c0: normalize to 12-bit fixed point
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

// (0x0044e1b0) - 3D model viewer animation (returns non-zero when exit requested)
int  FUN_0044e1b0(void) { return 0; }

// (0x00484420) - Process texture for menu item model
void FUN_00484420(void* src, void* dst) { }

// (0x00420b80) - Update selected menu item cursor
void FUN_00420b80(void) { }

// (0x00420bb0) - Handle menu item action
void FUN_00420bb0(void) { }

// (0x00420a70) - Draw menu cursor sprite
void FUN_00420a70(void) { }

// (0x00454fd0) - Draw item name text at position
void FUN_00454fd0(int itemId, int mode, short x, short y) { }

// FUN_004387e0 is now a static function in MainMenu.cpp

// (0x00470c60) - Draw gradient line primitive
void FUN_00470c60(void* prim, int depth) { }

// (0x00438800) - Draw health bar gradient line
void FUN_00438800(int a, int b, int c) { }

// (0x00488430) - Map screen room flag check
void FUN_00488430(void* mapData) { }

// (0x00488950) - Ending state check for map
void FUN_00488950(void) { }

// (0x00488660) - Set map room highlight
void FUN_00488660(int roomId) { }

// (0x00482c50) - Status screen texture setup
void FUN_00482c50(void) { }

// (0x00482800) - Status screen input check
int  FUN_00482800(int param) { return 0; }

// (0x00482250) - Status screen update sub
void FUN_00482250(void* data) { }

// (0x00482be0) - Status screen init sub
void FUN_00482be0(void* data) { }

// (0x00487200) - Map animation sub-function
void FUN_00487200(void* data) { }

// (0x00487640) - Map room display sub-function
void FUN_00487640(void* data) { }

// (0x00443040) - Load item box item image
void FUN_00443040(int imgType, int slot) { }

// (0x00452c50) - options_key_config_handler now in OptionsMenu.cpp
// (0x00451960) - options_display_config_handler now in OptionsMenu.cpp
// (0x00453a80) - options_joystick_config_handler now in OptionsMenu.cpp
// (0x00451900) - options_input_repeat now in OptionsMenu.cpp
// (0x00476b00) - options_menu_exit now in OptionsMenu.cpp
// (0x00476b40) - options_menu_render now in OptionsMenu.cpp
// (0x00477230) - options_init_keybind_display now in OptionsMenu.cpp

// (0x00488160) - Map screen init
void menu_init_map_screen(void) { }

// (0x00488880) - Map display init
void menu_init_map_display(void) { }

// (0x004886b0) - Map animation update
int  menu_update_map_animation(void) { return 0; }

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

// (0x00462d90) - Check if entity position is within a camera switch zone
// Returns 1 if position is inside the quadrilateral defined by the zone,
// or if zoneData is NULL (no zones = always visible).
int is_entity_in_switch_zone(VECTOR* pos, void* zoneData)
{
    // If no zone data, treat as always visible (e.g. options menu has no RDT zones)
    if (zoneData == NULL) return 1;

    // Original checks a single quadrilateral zone
    // For now return 1 to allow rendering (proper zone iteration TBD)
    return 1;
}

// (0x00497de0) - Keyboard scancode read (async)
unsigned char FUN_00497de0(void) { return 0; }

// (0x00486df0) - Sprite rendering mode (used when spriteData[4]==1)
void FUN_00486df0(void* spriteData) { }




