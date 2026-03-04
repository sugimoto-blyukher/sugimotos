#include "kernel/kernel.h"

void yield(void)
{
    struct process *next = idle_proc;
    int current_index = (int) (current_proc - procs);
    for (int i = 1; i <= PROC_MAX; i++) {
        struct process *proc = &procs[(current_index + i) % PROC_MAX];
        if (proc->state == PROC_RUNNABLE && proc->pid > 0) {
            next = proc;
            break;
        }
    }

    if (next == current_proc)
        return;

    __asm__ __volatile__(
        "csrw sscratch, %[sscratch]\n"
        :
        : [sscratch] "r" ((uint32_t) &next->stack[sizeof(next->stack)]));

    struct process *prev = current_proc;
    current_proc = next;
    switch_context(&prev->sp, &next->sp);
}
