// GameStubs.cpp - Stub functions and implementations from Ghidra decompilation
// All functions decompiled from Ghidra with original addresses
#include "../Globals.h"
#include "../marni/MarniSystem.h"
#include "../marni/PSXTexture.h"

// ============================================================================
// ObjectList_Cleanup (0x00487040)
// Cleans up the object list: releases object handles and calls destructors
// ============================================================================
static void ObjectList_Cleanup(void)
{
    if (g_objectListCleanupFlag == 1) {
        DWORD* basePtr = g_objectListPtrArray;
        DWORD* countPtr = (DWORD*)&g_objectCountArray[32];  // points past the end (DAT_008ffcc0)
        int i = 0;
        if (0 < g_objectListCleanupCount) {
            do {
                i = i + 1;

                // Call vtable[9] (DeleteObjectHandle) on countPtr[0x15]
                void** d3dVtable = *(void***)g_pMarniDirect3D;
                ((void(*)(void*))d3dVtable[9])((void*)countPtr[0x15]);
                countPtr[0x15] = 0;

                // Call function pointer from basePtr
                void** funcPtr = (void**)*basePtr;
                if (funcPtr) {
                    ((void(*)())*funcPtr)();
                }

                basePtr = basePtr + 0xe;     // advance by 14 DWORDs (0x38 bytes)
                countPtr = countPtr + 0x21;  // advance by 33 DWORDs (0x84 bytes)
            } while (i < g_objectListCleanupCount);
        }
        g_objectListCleanupCount = 0;
        g_objectListCleanupFlag = 0;
    }
}

// ============================================================================
// VideoDriver_ClearState348 (0x004211b0)
// Clears texture state at this+0x348: releases handles then zeros the array
// ============================================================================
static int __stdcall VideoDriver_ClearState348(void* obj, void* context)
{
    // VideoDriver_ReleaseResources (0x00421150)
    // Release 8 texture handles stored at obj+0x34c
    DWORD* handleArray = (DWORD*)((DWORD*)obj + 0x34c / 4);
    void** ctxVtable = *(void***)context;
    for (int i = 0; i < 8; i++) {
        ((void(*)(DWORD))ctxVtable[8])(handleArray[i]);
        handleArray[i] = 0;
    }
    *(DWORD*)((DWORD*)obj + 0x348 / 4) = 0;

    // VideoDriver_ClearArrayD0 — call PSXTexture::ClearCLUTEntries on obj
    ((PSXTexture*)obj)->ClearCLUTEntries();

    // Zero 8 DWORDs at obj+0x34c (already done above) and obj+0x348
    for (int i = 0; i < 8; i++) {
        handleArray[i] = 0;
    }

    return 1;
}

// ============================================================================
// ObjectCleanupCallback (0x00483e00)
// Async callback for Object_DeleteAll: iterates object arrays and cleans up
// ============================================================================
static void ObjectCleanupCallback(void)
{
    g_objectDeleteFlag = 0;

    // First pass: iterate g_objectCountArray and clear associated objects
    for (int i = 0; i < 32; i++) {
        if (i == 22) {
            g_objectCountArray[22] = 1;
        } else {
            int count = g_objectCountArray[i];
            char* basePtr = (char*)&g_tmdObjectBuffer[0] + i * 0x1b60;
            for (int j = 0; j < count; j++) {
                VideoDriver_ClearState348(basePtr + j * 0x36c, g_pMarniDirect3D);
            }
            g_objectCountArray[i] = 0;
        }
    }

    // Second pass: zero memory and cleanup TMD objects
    g_objectDeleteCounter = 0;
    char* tmdBase = (char*)&g_tmdObjectBuffer[0];
    for (int i = 0; i < 250; i++) {
        if (g_objectDeletePtr) {
            g_objectDeletePtr[i] = 0;
        }

        // Call VideoDriver_CleanupObjects on each CMarniDirect3DTMD
        // Equivalent to CMarniDirect3DTMD::CleanupObjects
        CMarniDirect3DTMD* tmd = (CMarniDirect3DTMD*)(tmdBase + i * 0x1594);
        tmd->CleanupObjects(g_pMarniDirect3D);
    }

    ObjectList_Cleanup();
}

// ============================================================================
// FUN_00484e70 (0x00484e70)
// Called by CleanupWrapper: clears specific render-state objects
// ============================================================================
static void FUN_00484e70(void)
{
    // Clear a specific PSXTexture+aux object at g_renderStateTex
    VideoDriver_ClearState348(g_renderStateTex, g_pMarniDirect3D);

    // Cleanup a specific TMD object at g_renderStateTMD
    CMarniDirect3DTMD* tmd = (CMarniDirect3DTMD*)g_renderStateTMD;
    tmd->CleanupObjects(g_pMarniDirect3D);
}

// ============================================================================
// CleanupWrapper (0x00484ea0)
// Wrapper that schedules FUN_00484e70 asynchronously
// ============================================================================
static void CleanupWrapper(void)
{
    ExecAsync((void*)FUN_00484e70);
}

// ============================================================================
// Object_DeleteAll (0x00483680)
// Schedules cleanup callbacks, then jumps to CleanupWrapper
// ============================================================================
void Object_DeleteAll(int a)
{
    ExecAsync((void*)ObjectCleanupCallback);
    CleanupWrapper();
}

// ============================================================================
// setSomeColor (0x00470a50)
// Sets the global color values used by sprite/effect rendering
// ============================================================================
void setSomeColor(int r, int g, int b)
{
    g_color_r = (float)r * 0.0078125;
    g_color_g = (float)g * 0.0078125;
    g_color_b = (float)b * 0.0078125;
}

// ---------------------------------------------------------------------------
// Texture/video stubs (called from logos_state / title_state in GameState.cpp)
// ---------------------------------------------------------------------------

void FUN_00470a30(void) { }
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
// FUN_00401020, FUN_0045ab60, FUN_00497360, FUN_00497340, StMask
// are implemented in Rendering.cpp
// ---------------------------------------------------------------------------
void FUN_004973a0(int param)                                 { /* stub */ }

void UpdateDemoTimer(void) {

}
// ============================================================================
// empty function (0x0047b950 area)
// ============================================================================
void empty_0047b950(int param)
{
}


void CreateTimestampedLogFile(void)                           { /* stub */ }
void ShowVideoModeDebugText(void)                            { /* stub */ }

// ---------------------------------------------------------------------------
// Title/input stubs
// ---------------------------------------------------------------------------
void empty_0040abb0(void* ptr, int a, int b, int c) { /* stub */ }
void cleanup_texture_slot(int slot)                           { /* stub */ }
void empty_00497c10(int value)                         { /* stub */ }

// ---------------------------------------------------------------------------
// Save/load stubs
// ---------------------------------------------------------------------------
void save_load_game_state(int a, int b, int c, int d, int e) { /* stub */ }


void empty_00470960(int slot) { }


