// Port-added cooperative context backend. Kernel objects never enter game data.
#pragma once
#include <stddef.h>
struct Re1TaskContext
{
    int thread = -1, resume = -1, yielded = -1;
    void (*entry)(void) = nullptr;
};
void re1_task_start(Re1TaskContext &, void (*entry)(void), void *stack, size_t bytes);
void re1_task_resume(Re1TaskContext &);
void re1_task_yield(Re1TaskContext &);
void re1_check_stack_guards();
