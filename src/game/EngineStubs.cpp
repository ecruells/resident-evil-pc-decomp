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


// ---------------------------------------------------------------------------
// Texture/video stubs (called from logos_state / title_state in GameState.cpp)
// ---------------------------------------------------------------------------

// FUN_00470a30 (0x00470a30) - stub
void FUN_00470a30(void) { }

// QueueVideoPlayback - stub
void QueueVideoPlayback(int id, int b)                { /* stub */ }

// VideoDriver_ClearArrayD0 - stub
void VideoDriver_ClearArrayD0(void)                   { /* stub */ }

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

// (0x00494050) - Debug save menu (F5 key)
void DebugSaveMenu(void) { }

// (0x004818b0) - Start attract mode demo playback
void StartAttractDemo(void) { }

// (0x0047eb60) - Post-death cleanup
// Genuinely empty in the original (single RET at 0x0047eb60).
void FUN_0047eb60(void) { }

// ---------------------------------------------------------------------------
// MainMenu.cpp dependency stubs (pending full decompilation)
// ---------------------------------------------------------------------------

// (0x004844b0) - Menu cleanup sub-function
void FUN_004844b0(void) { }

// (0x00470a40) - Texture cleanup sub-function
void FUN_00470a40(void) { }

// (0x00470a20) - Empty function (menu init placeholder)
void empty_00470a20(void) { }


// (0x00482c50) - Status screen texture setup
void FUN_00482c50(void) { }

// (0x00482800) - Status screen input check
int  FUN_00482800(int param) { return 0; }

// (0x00482be0) - Status screen init sub
void FUN_00482be0(void* data) { }

// (0x00443040) - Load item box item image
void FUN_00443040(int imgType, int slot) { }

// --- Entity render system stubs (entity animation/rendering) ---

// (0x0048a210) - Entity path animation step
void FUN_0048a210(void* joint) { }

// (0x00497de0) - Keyboard scancode read (async)
unsigned char FUN_00497de0(void) { return 0; }

