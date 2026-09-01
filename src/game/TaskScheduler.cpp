// TaskScheduler.cpp - Cooperative multitasking scheduler
// Exact replica of the original assembly-based stack switching:
//   - Tasks entered via JMP (no return address on task stack)
//   - Yield saves task ESP, restores scheduler ESP, RETs to scheduler
//   - Scheduler resumes by restoring task ESP, RETs into task
//   - No setjmp/longjmp — pure ESP save/restore like the original
//
// Memory Layout (matching original):
//   g_TasksTable[3] at 0x00D1FDE4 (0x7C bytes each)
//   g_TasksESP[3]   at 0x00D91A70
//   g_TasksEIP[3]   at 0x00D91A80
//   g_SchedulerESP  at 0x00D91A8C
//   g_CurrentTaskID at 0x00D91A7C
//   g_AsyncRpcCallback at 0x00D91A90
//
// Original entry point: TaskScheduler_Update at 0x004200E0
// Original helpers:
//   SwitchToTask at 0x00475740
//   Yield at 0x00475768
//   ReturnToScheduler at 0x00475788
//   ReturnToSchedulerAndKillTask at 0x004757a8
//   Task_execute at 0x004201C0
//   Task_sleep at 0x004201E0
//   Task_exit (FUN_00420210) at 0x00420210
//   Task_chain at 0x00420230
//
// IMPORTANT: /RTCs (Runtime Stack Check) MUST be disabled in this file.
// The manual ESP switching (g_SchedulerESP save/restore via naked asm and
// inline asm) is fundamentally incompatible with the compiler-inserted
// ESP verification that /RTCs adds after every CALL instruction. The
// original game was built with MSVC 4.x which did not have this feature.

// Disable "Run-Time Check Failure #0" ESP verification for this TU
#pragma runtime_checks("s", off)

// Disable frame pointer generation (/Oy). The task resume path only
// switches ESP; EBP remains the scheduler's value from PUSHAD.
// If the task functions use EBP-based stack frames, mov-esp-ebp
// in the epilogue would jump to the scheduler's frame, corrupting
// the resume. The original game was compiled with frame pointer omission.
#pragma optimize("y", on)

#include "../Globals.h"

#define TASK_STACK_SIZE   262144      // 256KB per task
#define TASK_MAX           3
#define TASK_SUSPENDED     0x40       // Bit 6: suspend flag
#define TASK_DEAD          0x00
#define TASK_SLEEPING      0x01
#define TASK_START         0x02
#define TASK_YIELD         0x04
#define TASK_ACTIVE        0x7F

#define TASK_SIZE          0x7C       // sizeof(TaskControlBlock)

// Task stacks: allocated with GUARD PAGES between each 256KB slot. A task
// that overflows its slot faults immediately on the guard page with the
// culprit's instruction pointer in crash.log, instead of silently corrupting
// a neighbouring task's saved registers (which killed standalone Release
// runs at room load as POPAD-restored garbage).
#define TASK_GUARD_SIZE   4096
static BYTE* g_TaskStackBase = NULL;   // start of slot 0's usable area

// Initial ESP for a task START dispatch. The original layout had contiguous
// stacks, so tasks were entered with ESP exactly at slot_top and a read at
// [slot_top] hit the next slot's memory. With the port's guard pages,
// slot_top IS the first PAGE_NOACCESS byte, and the MSVC Release stack-
// alignment prologue (push ebx / mov ebx,esp / and esp,-16 / mov ebp,[ebx+4])
// reads [entry_esp] — e.g. options_menu+0xD faulting on its movaps alignment
// prologue (0xC0000005 at options_menu+0xd, EBX=slot_top-4, ESP=slot_top-0x10).
// Enter tasks 16 bytes below the guard so [entry_esp] stays readable and the
// ESP stays 16-byte aligned like the original dispatch.
static DWORD TaskStackTop(int id)
{
    return (DWORD)(g_TaskStackBase + id * (TASK_STACK_SIZE + TASK_GUARD_SIZE)
                   + TASK_STACK_SIZE) - 16;
}

#pragma warning(disable: 4731)  // frame pointer register modified by inline asm

// ============================================================================
// Naked assembly: switch to task (initial dispatch)
// Original two-level structure:
//   0x0047575c (SwitchToTask):  PUSHAD; PUSHFD; CALL 0x00475740; POPFD; POPAD; RET
//   0x00475740 (inner core):    save sched ESP; load task ESP; JMP task EIP
// ============================================================================

// Inner core: actual stack switch (0x00475740)
__declspec(naked) static void SwitchToTask_Core(void) {
    __asm {
        mov  eax, [g_CurrentTaskID]
        mov  [g_SchedulerESP], esp      ; save scheduler ESP (points to wrapper return addr)
        mov  esp, [g_TasksESP + eax*4]  ; load task's ESP
        jmp  [g_TasksEIP + eax*4]       ; jump to task (no return address on task stack)
    }
}

// Outer wrapper: save registers/flags, call inner core, restore on yield (0x0047575c)
__declspec(naked) void SwitchToTask_Asm() {
    __asm {
        pushad                          ; save EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI
        pushfd                          ; save EFLAGS
        call SwitchToTask_Core          ; CALL pushes return addr → captured by g_SchedulerESP
        popfd                           ; restore EFLAGS (reached when task yields back)
        popad                           ; restore all registers
        ret                             ; return to TaskScheduler_Update
    }
}

// ============================================================================
// Naked assembly: resume task after sleep
// Original two-level structure:
//   0x0047579c (ReturnToScheduler): PUSHAD; PUSHFD; CALL 0x00475788; POPFD; POPAD; RET
//   0x00475788 (inner core):        save sched ESP; load task ESP; RET to task
// ============================================================================

// Inner core: actual stack switch (0x00475788)
// TaskYield pushed all task registers onto the task stack via PUSHAD.
// This function must POPAD them back before RETurning to the task.
__declspec(naked) static void ReturnToScheduler_Core(void) {
    __asm {
        mov  eax, [g_CurrentTaskID]
        mov  [g_SchedulerESP], esp      ; save scheduler ESP (points to wrapper return addr)
        mov  esp, [g_TasksESP + eax*4]  ; load task's saved ESP (points to PUSHAD block)
        popad                           ; restore task's EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI
        ret                             ; return to task (pops from task's restored stack)
    }
}

// Outer wrapper: save registers/flags, call inner core, restore on yield (0x0047579c)
__declspec(naked) void ReturnToScheduler_Resume() {
    __asm {
        pushad                          ; save EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI
        pushfd                          ; save EFLAGS
        call ReturnToScheduler_Core      ; CALL pushes return addr → captured by g_SchedulerESP
        popfd                           ; restore EFLAGS (reached when task yields back)
        popad                           ; restore all registers
        ret                             ; return to TaskScheduler_Update
    }
}

// ============================================================================
// TaskScheduler_Update (0x004200E0)
// ============================================================================
void TaskScheduler_Update(void)
{
    g_CurrentTask = g_TasksTable;
    g_CurrentTaskPtr = g_TasksTable;
    g_CurrentTaskID = 0;
    g_SchedulerRunningFlag = 1;

    TaskControlBlock* endPtr = &g_TasksTable[TASK_MAX];

    do {
        int state = g_CurrentTask->state;
        if (state == TASK_SLEEPING) {
            g_CurrentTask->sleepCounter -= 1;
            if (g_CurrentTask->sleepCounter == 0) {
_resume_task:
                g_CurrentTask->state = TASK_ACTIVE;
                ReturnToScheduler_Resume();
            }
        }
        else if (state == TASK_START) {
            g_TasksESP[g_CurrentTaskID] = TaskStackTop(g_CurrentTaskID);
            SwitchToTask_Asm();
        }
        else if (state == TASK_YIELD) {
            goto _resume_task;
        }

        if (g_AsyncRpcCallback == NULL) {
            ++g_CurrentTaskID;
            ++g_CurrentTask;

            if (g_CurrentTask >= endPtr) {
                g_SchedulerRunningFlag = 0;
                return;
            }
        }
        else {
            void (*callback)() = (void (*)())g_AsyncRpcCallback;
            g_AsyncRpcCallback = NULL;
            callback();
        }
    } while(1);
}

// ============================================================================
// Task_execute (0x004201C0)
// ============================================================================
void Task_execute(int id, void* func)
{
    if (id < 0 || id >= TASK_MAX) return;
    if (func == NULL) return;

    g_TasksEIP[id] = (DWORD)func;
    g_TasksTable[id].state = TASK_START;
    g_TasksTable[id].sleepCounter = 0;
}

// ============================================================================
// Naked assembly: TaskYield — saves task ESP+registers, returns to scheduler (0x00475768)
// PUSHAD saves all task registers (EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI) onto the
// task stack so that ReturnToScheduler_Core can POPAD them back on resume.
// Without this, the task's EBP/ESI/EDI/EBX get corrupted by scheduler values.
// ============================================================================
__declspec(naked) static void TaskYield(void) {
    __asm {
        pushad                              ; save all task registers to task stack
        mov  eax, [g_CurrentTaskID]
        mov  [g_TasksESP + eax*4], esp      ; save task ESP (points to PUSHAD block)
        mov  esp, [g_SchedulerESP]          ; restore scheduler's ESP
        ret                                 ; return to scheduler wrapper
    }
}

// ============================================================================
// Task_sleep (0x004201E0)
// Original: sets sleep counter + state, then calls Yield (naked function).
// The CALL pushes the return address; Yield/TaskYield captures ESP before any frame.
// ============================================================================
void Task_sleep(int frames)
{
    int id = g_CurrentTaskID;
    if (id < 0 || id >= TASK_MAX) return;

    g_TasksTable[id].sleepCounter = (short)frames;
    g_TasksTable[id].state = TASK_SLEEPING;

    TaskYield();  // Naked function saves correct ESP, switches to scheduler
}

// ============================================================================
// Task_chain (0x00420230)
// ============================================================================
void Task_chain(void* func)
{
    int id = g_CurrentTaskID;
    if (id < 0 || id >= TASK_MAX) return;

    g_TasksEIP[id] = (DWORD)func;
    g_TasksTable[id].state = TASK_START;
    g_TasksTable[id].sleepCounter = 0;

    // Restore scheduler ESP and RET to scheduler
    __asm {
        mov  esp, [g_SchedulerESP]
        ret
    }
}

// ============================================================================
// Task_exit (0x00420210)
// ============================================================================
void Task_exit(void)
{
    int id = g_CurrentTaskID;
    if (id < 0 || id >= TASK_MAX) return;

    g_TasksTable[id].state = TASK_DEAD;
    g_TasksTable[id].sleepCounter = 0;

    // Restore scheduler ESP and RET to scheduler
    __asm {
        mov  esp, [g_SchedulerESP]
        ret
    }
}

// ============================================================================
// Task_suspend (0x00420260)
// ============================================================================
void Task_suspend(int id)
{
    if (id < 0 || id >= TASK_MAX) return;
    g_TasksTable[id].state = (short)(g_TasksTable[id].state | TASK_SUSPENDED);
}

// ============================================================================
// Task_Resume (0x00420270)
// ============================================================================
void Task_Resume(int id)
{
    if (id < 0 || id >= TASK_MAX) return;
    g_TasksTable[id].state = (short)(g_TasksTable[id].state & ~TASK_SUSPENDED);
}

// ============================================================================
// ExecAsync (0x004202a0)
// Executes a callback asynchronously via the task scheduler.
// If the scheduler is not running, calls the callback directly.
// If running, waits for any pending async callback, registers the new one,
// yields via Task_sleep(1) so the scheduler fires it in the advance block,
// then clears it after the task wakes up.
// ============================================================================
void ExecAsync(void* callback)
{
    if (g_SchedulerRunningFlag == 0) {
        ((void(*)())callback)();
        return;
    }
    while (g_AsyncRpcCallback != NULL) {
        Task_sleep(1);
    }
    g_AsyncRpcCallback = callback;
    Task_sleep(1);
    g_AsyncRpcCallback = NULL;
}

// ============================================================================
// TaskScheduler_Reset (0x004200A0)
// ============================================================================
void TaskScheduler_Reset(void)
{
    for (int i = 0; i < TASK_MAX; i++) {
        g_TasksTable[i].state = TASK_DEAD;
        g_TasksTable[i].sleepCounter = 0;
        g_TasksESP[i] = 0;
        g_TasksEIP[i] = 0;
    }
    g_CurrentTaskID = 0;
    g_SchedulerESP = 0;
    g_SchedulerRunningFlag = 0;
    g_AsyncRpcCallback = NULL;
    g_CurrentTask = NULL;
    g_CurrentTaskPtr = NULL;
}

// ============================================================================
// TaskScheduler_Init
// ============================================================================
void TaskScheduler_Init(void)
{
    if (g_TaskStackBase == NULL) {
        SIZE_T total = (SIZE_T)TASK_MAX * (TASK_STACK_SIZE + TASK_GUARD_SIZE)
                       + TASK_GUARD_SIZE;
        BYTE* mem = (BYTE*)VirtualAlloc(NULL, total, MEM_RESERVE | MEM_COMMIT,
                                        PAGE_READWRITE);
        if (mem != NULL) {
            // Poison every guard page with PAGE_NOACCESS: [G0][S0][G1][S1][G2][S2][G3]
            for (int i = 0; i <= TASK_MAX; i++) {
                BYTE* guard = mem + i * (TASK_STACK_SIZE + TASK_GUARD_SIZE);
                DWORD oldProt = 0;
                VirtualProtect(guard, TASK_GUARD_SIZE, PAGE_NOACCESS, &oldProt);
            }
            g_TaskStackBase = mem + TASK_GUARD_SIZE;  // skip G0 -> start of S0
        } else {
            MessageBoxA(NULL, "Failed to allocate task stacks", "RESIDENT EVIL", MB_OK);
            ExitProcess(1);
        }
    }
    g_StackPointer = (DWORD)g_TaskStackBase;
    TaskScheduler_Reset();
}
