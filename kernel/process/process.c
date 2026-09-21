#include "arch/process.h"
#include "kernel/fs.h"
#include "kernel/page_alloc.h"
#include "kernel/process_vm.h"
#include "kernel/proc.h"
#include "kernel/vm.h"
#include "uapi/process.h"

#define USER_STACK_PAGES 2
#define USER_STACK_TOP 0x40000000u
#define PROC_PRIORITY_MIN 1
#define PROC_PRIORITY_DEF 4

struct process procs[PROC_MAX];
struct process *current_proc;
struct process *idle_proc;

static struct process *alloc_process_slot(void)
{
    for (int i = 0; i < PROC_MAX; i++) {
        if (procs[i].state == PROC_UNUSED)
            return &procs[i];
    }
    return NULL;
}

static void init_process_common(struct process *proc, int pid, int parent_pid, bool is_user, int priority)
{
    memset(proc, 0, sizeof(*proc));
    proc->pid = pid;
    proc->state = PROC_RUNNABLE;
    proc->priority = priority;
    proc->budget = priority;
    proc->parent_pid = parent_pid;
    proc->is_user = is_user;
}

static void mark_process_unused(struct process *proc)
{
    memset(proc, 0, sizeof(*proc));
    proc->state = PROC_UNUSED;
}

static void reset_process_with_released_resources(struct process *proc)
{
    proc_vm_release(proc);
    mark_process_unused(proc);
}

static struct process *find_process_by_pid(int pid)
{
    for (int i = 0; i < PROC_MAX; i++) {
        if (procs[i].state != PROC_UNUSED && procs[i].pid == pid)
            return &procs[i];
    }
    return NULL;
}

static uint32_t proc_user_stack_end(const struct process *proc)
{
    return proc->user_stack_base + proc->user_stack_pages * PAGE_SIZE;
}

static int proc_sp_within_user_stack(const struct process *proc, uint32_t sp)
{
    uint32_t stack_base = proc->user_stack_base;
    uint32_t stack_end = proc_user_stack_end(proc);
    return sp >= stack_base && sp <= stack_end;
}

struct process *create_process(uint32_t pc)
{
    struct process *proc = alloc_process_slot();
    if (!proc)
        return NULL;

    int index = (int) (proc - procs);
    init_process_common(proc, index + 1, 0, false, PROC_PRIORITY_MIN);
    proc->satp = vm_kernel_satp();
    arch_init_switch_context(proc, pc);
    return proc;
}

struct process *create_user_process(uint32_t entry_pc)
{
    struct process *proc = alloc_process_slot();
    if (!proc)
        return NULL;

    int index = (int) (proc - procs);
    init_process_common(proc, index + 1, 0, true, PROC_PRIORITY_DEF);
    proc->user_stack_pages = USER_STACK_PAGES;
    proc->user_stack_paddr = alloc_pages_try(proc->user_stack_pages);
    if (!proc->user_stack_paddr) {
        mark_process_unused(proc);
        return NULL;
    }
    proc->user_stack_base = USER_STACK_TOP - proc->user_stack_pages * PAGE_SIZE;
    proc->satp = vm_build_user_satp(proc->user_stack_base, proc->user_stack_paddr, proc->user_stack_pages);
    if (!proc->satp) {
        reset_process_with_released_resources(proc);
        return NULL;
    }
    if (proc_vm_init(proc) < 0) {
        reset_process_with_released_resources(proc);
        return NULL;
    }
    arch_init_user_context(proc, entry_pc, proc_user_stack_end(proc), 0, 0);
    arch_init_switch_context(proc, (uint32_t) arch_resume_from_trap);
    return proc;
}

int proc_fork(void)
{
    struct process *parent = current_proc;
    if (!parent || !parent->is_user || !parent->has_trap_frame)
        return -1;

    struct process *child = alloc_process_slot();
    if (!child)
        return -1;

    int child_index = (int) (child - procs);
    init_process_common(child, child_index + 1, parent->pid, true, parent->priority);
    child->user_stack_pages = parent->user_stack_pages;
    child->user_stack_paddr = alloc_pages_try(child->user_stack_pages);
    if (!child->user_stack_paddr) {
        mark_process_unused(child);
        return -1;
    }
    child->user_stack_base = parent->user_stack_base;
    child->satp = vm_build_user_satp(child->user_stack_base, child->user_stack_paddr, child->user_stack_pages);
    if (!child->satp) {
        reset_process_with_released_resources(child);
        return -1;
    }
    memcpy(child->vmas, parent->vmas, sizeof(child->vmas));
    memcpy(child->fds, parent->fds, sizeof(child->fds));

    uint32_t parent_sp = arch_user_stack_pointer(parent);
    if (!proc_sp_within_user_stack(parent, parent_sp)) {
        reset_process_with_released_resources(child);
        return -1;
    }

    uint32_t sp_offset = parent_sp - parent->user_stack_base;
    arch_clone_user_context(child, parent, child->user_stack_base + sp_offset);

    if (proc_vm_clone(parent, child) < 0) {
        reset_process_with_released_resources(child);
        return -1;
    }

    // Stack pages are now shared COW with parent.
    child->user_stack_paddr = parent->user_stack_paddr;

    arch_init_switch_context(child, (uint32_t) arch_resume_from_trap);

    return child->pid;
}

int proc_exec(uint32_t entry_pc, uint32_t argv)
{
    if (!current_proc || !current_proc->is_user)
        return -1;
    if (entry_pc == 0)
        return -1;

    int argc = 0;
    if (proc_user_exec_argv_ok(argv, &argc) < 0)
        return -1;

    proc_vm_reset(current_proc);
    if (proc_vm_init(current_proc) < 0)
        return -1;

    arch_init_user_context(current_proc, entry_pc, proc_user_stack_end(current_proc),
                           (uint32_t) argc, argv);
    return 0;
}

void proc_exit(int status)
{
    for (int fd = 0; fd < FD_MAX; fd++) {
        if (current_proc->fds[fd].used)
            fs_close(fd);
    }

    current_proc->exit_status = status;
    current_proc->state = PROC_ZOMBIE;

    struct process *parent = find_process_by_pid(current_proc->parent_pid);
    if (parent && parent->state == PROC_BLOCKED)
        parent->state = PROC_RUNNABLE;

    for (;;)
        yield();
}

void proc_reap_orphan_zombies(void)
{
    for (int i = 0; i < PROC_MAX; i++) {
        struct process *proc = &procs[i];
        if (proc == idle_proc)
            continue;
        if (proc->state != PROC_ZOMBIE || !proc->is_user || proc->parent_pid != 0)
            continue;
        proc_vm_release(proc);
        mark_process_unused(proc);
    }
}

int proc_wait(int *status_ptr)
{
    return proc_waitpid(-1, status_ptr, 0);
}

int proc_waitpid(int pid, int *status_ptr, int options)
{
    if (options & ~WNOHANG)
        return -1;

    while (1) {
        bool has_child = false;

        for (int i = 0; i < PROC_MAX; i++) {
            struct process *proc = &procs[i];
            if (proc->state == PROC_UNUSED || proc->parent_pid != current_proc->pid)
                continue;
            if (pid > 0 && proc->pid != pid)
                continue;

            has_child = true;
            if (proc->state != PROC_ZOMBIE)
                continue;

            int reaped_pid = proc->pid;
            if (status_ptr)
                *status_ptr = proc->exit_status;

            proc_vm_release(proc);
            mark_process_unused(proc);
            return reaped_pid;
        }

        if (!has_child)
            return -1;
        if (options & WNOHANG)
            return 0;

        current_proc->state = PROC_BLOCKED;
        yield();
        current_proc->state = PROC_RUNNABLE;
    }
}
