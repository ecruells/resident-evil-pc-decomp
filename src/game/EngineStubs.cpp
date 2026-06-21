// EngineStubs.cpp - General engine and video stubs (pending decompilation)
// All functions decompiled from Ghidra with original addresses
#include "../Globals.h"
#include "../marni/MarniSystem.h"
#include "../marni/PSXTexture.h"

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
