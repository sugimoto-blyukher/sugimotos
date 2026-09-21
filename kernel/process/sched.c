#include "kernel/proc.h"
#include "kernel/vm.h"

static int is_schedulable(const struct process *proc)
{
    return proc && proc->state == PROC_RUNNABLE && proc->pid > 0;
}

static int is_eligible_for_pick(const struct process *proc)
{
    return is_schedulable(proc) && proc->budget > 0;
}

static int pick_next_runnable_index(int start_index)
{
    int best_idx = -1;
    int best_budget = -1;

    for (int i = 1; i <= PROC_MAX; i++) {
        int idx = (start_index + i) % PROC_MAX;
        struct process *proc = &procs[idx];
        if (!is_eligible_for_pick(proc))
            continue;
        if (proc->budget > best_budget) {
            best_budget = proc->budget;
            best_idx = idx;
        }
    }

    return best_idx;
}

static void refill_budgets(void)
{
    for (int i = 0; i < PROC_MAX; i++) {
        struct process *proc = &procs[i];
        if (!is_schedulable(proc))
            continue;
        if (proc->priority < 1)
            proc->priority = 1;
        proc->budget = proc->priority;
    }
}

void yield(void)
{
    struct process *next = idle_proc;
    int current_index = (int) (current_proc - procs);

    if (is_eligible_for_pick(current_proc))
        current_proc->budget--;

    int next_idx = pick_next_runnable_index(current_index);
    if (next_idx < 0) {
        refill_budgets();
        next_idx = pick_next_runnable_index(current_index);
    }
    if (next_idx >= 0)
        next = &procs[next_idx];

    if (next == current_proc)
        return;

    vm_activate(next->satp);

    struct process *prev = current_proc;
    current_proc = next;
    switch_context(&prev->sp, &next->sp);
}
