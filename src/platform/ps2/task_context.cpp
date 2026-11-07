// Port-added: no x86 assembly/ucontext on the R5900.
#include "task_context.h"
#include "../platform.h"
#include <kernel.h>
extern void Task_exit(void);

static int Semaphore()
{
    ee_sema_t s = {};
    s.init_count = 0;
    s.max_count = 1;
    int id = CreateSema(&s);
    if (id < 0)
        plat_fatal("PS2 task semaphore allocation failed");
    return id;
}
static void Entry(void *arg)
{
    auto *c = static_cast<Re1TaskContext *>(arg);
    c->entry();
    Task_exit();
    plat_fatal("Task_exit returned");
}
void re1_task_start(Re1TaskContext &c, void (*fn)(void), void *stack, size_t bytes)
{
    // A chained/reused slot's old thread is blocked in WaitSema. Terminate it
    // before reusing its stack. No C++ objects may survive Task_chain/exit,
    // the same contract as the original's stack switch.
    if (c.thread >= 0)
    {
        TerminateThread(c.thread);
        DeleteThread(c.thread);
    }
    if (c.resume >= 0)
        DeleteSema(c.resume);
    if (c.yielded >= 0)
        DeleteSema(c.yielded);
    c.resume = Semaphore();
    c.yielded = Semaphore();
    c.entry = fn;
    ee_thread_status_t current = {};
    if (ReferThreadStatus(GetThreadId(), &current) < 0)
        plat_fatal("Cannot query EE thread priority");
    ee_thread_t t = {};
    t.func = reinterpret_cast<void *>(Entry);
    t.stack = stack;
    t.stack_size = bytes;
    t.gp_reg = &_gp;
    t.initial_priority = current.current_priority;
    c.thread = CreateThread(&t);
    if (c.thread < 0 || StartThread(c.thread, &c) < 0)
        plat_fatal("PS2 task thread allocation failed");
    WaitSema(c.yielded);
    re1_check_stack_guards();
}
void re1_task_resume(Re1TaskContext &c)
{
    SignalSema(c.resume);
    WaitSema(c.yielded);
    re1_check_stack_guards();
}
void re1_task_yield(Re1TaskContext &c)
{
    re1_check_stack_guards();
    SignalSema(c.yielded);
    WaitSema(c.resume);
}
