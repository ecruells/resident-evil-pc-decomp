// GameStubs.cpp - Stub functions awaiting full decompilation
// All functions decompiled from Ghidra with original addresses
#include "../Globals.h"

// ---------------------------------------------------------------------------
// Texture/video stubs (called from logos_state / title_state in GameState.cpp)
// ---------------------------------------------------------------------------

void FUN_00470a30(void)                                      { /* stub */ }
void Object_DeleteAll(int a)                                 { /* stub */ }
void SetVideoResolution(int w, int h)                        { /* stub */ }
void setSomeColor(int r, int g, int b)                      { /* stub */ }
static void QueueVideoPlayback(int id, int b)                { /* stub */ }
static void VideoDriver_ClearArrayD0(void)                   { /* stub */ }
static void LoadPSXImage(void* buf, int mode)                { /* stub */ }
static int  FUN_0046c160(void* data, int mode)               { return 0; }
static void FUN_00427270(void)                               { /* stub */ }
static void FUN_00427100(int a, int b, int c)                { /* stub */ }
static int  FUN_004271e0(int a, int b)                       { return 0; }
static void FUN_00426df0(int id, void* data)                 { /* stub */ }
static void FUN_00426f70(int a, void* b)                     { /* stub */ }
static void FUN_00427250(void)                               { /* stub */ }

// ---------------------------------------------------------------------------
// General game engine stubs (referenced from GameLoop.cpp and WindowProc.cpp)
// ---------------------------------------------------------------------------
void FUN_00401020(int param)                                 { /* stub */ }
void FUN_0045ab60(void)                                      { /* stub */ }
void FUN_00497360(int r, int g, int b)                       { /* stub */ }
void FUN_00497340(int param)                                 { /* stub */ }
void FUN_004973a0(int param)                                 { /* stub */ }
void UpdateDemoTimer(void)                                   { /* stub */ }
void StMask(int param, int param2)                           { /* stub */ }


void CreateTimestampedLogFile(void)                           { /* stub */ }
void ShowVideoModeDebugText(void)                            { /* stub */ }

// ---------------------------------------------------------------------------
// Title/input stubs
// ---------------------------------------------------------------------------
void title_init_render_state(void* ptr, int a, int b, int c) { /* stub */ }
void cleanup_texture_slot(int slot)                           { /* stub */ }
void reset_title_pad_state(int value)                         { /* stub */ }

// ---------------------------------------------------------------------------
// Save/load stubs
// ---------------------------------------------------------------------------
void save_load_game_state(int a, int b, int c, int d, int e) { /* stub */ }

// ---------------------------------------------------------------------------
// Title screen display list stubs
// ---------------------------------------------------------------------------
void title_reset_display_list(int slot)                       { /* stub */ }
