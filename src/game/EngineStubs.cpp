// EngineStubs.cpp - General engine and video stubs (pending decompilation)
// All functions decompiled from Ghidra with original addresses
#include "../Globals.h"
#include "../marni/MarniSystem.h"
#include "../marni/PSXTexture.h"

// Forward declarations for TmdAnimation.cpp
extern void SetAnimSlot(AnimSlot* slots, int slotPtr, int index);
extern unsigned int* CreateAnimObject(int slotPtr, unsigned int* param2);

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

// CreateTimestampedLogFile - stub
void CreateTimestampedLogFile(void)                           { /* stub */ }

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
void update_entities(void) { }

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
void some_camera_transform_fun_0048c190(int ca) { }

// (0x0045a2e0) - Entity matrix update for rendering
void entity_matrix_update_0045a2e0(void) { }

// (0x0048c350) - Calculate entity lighting for rendering
void calc_entity_lighting(Entity* ent) { }

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

// (0x004761b0) - Options/configuration menu (key bindings, display, sound)
void options_menu(void) { }

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

// (0x00462620) - Load equipped weapon animation data
void LoadEquippedWeaponAnimation(int weaponId, int slot, void* animBuf, void* objBuf) { }

// (0x004844b0) - Menu cleanup sub-function
void FUN_004844b0(void) { }

// (0x00470a40) - Texture cleanup sub-function
void FUN_00470a40(void) { }

// (0x0047d0e0) - Effect cleanup sub-function
void FUN_0047d0e0(void) { }

// (0x00462940) - Room camera restore after menu
void FUN_00462940(void) { }

// (0x0040ac80) - Set light data by index
void FUN_0040ac80(int idx, void* lightData) { }

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

// (0x00452c50) - Options key config sub-menu
int  FUN_00452c50(void) { return 0; }

// (0x00451960) - Options display config sub-menu
int  FUN_00451960(void) { return 0; }

// (0x00453a80) - Options sound config sub-menu
int  FUN_00453a80(void) { return 0; }

// (0x00451900) - Options input handler
int  FUN_00451900(void* a, void* b, int c, int d) { return 0; }

// (0x00476b00) - Options exit handler
void FUN_00476b00(void) { }

// (0x00476b40) - Options render handler
void FUN_00476b40(void) { }

// (0x00477230) - Options init sub
void FUN_00477230(void) { }

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



