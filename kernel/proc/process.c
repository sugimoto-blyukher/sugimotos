#include "kernel/kernel.h"
#include "kernel/syscall.h"

#define USER_STACK_PAGES 2

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

static void init_switch_context(struct process *proc, uint32_t ra)
{
    uint32_t *sp = (uint32_t *) &proc->stack[sizeof(proc->stack)];
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = ra;
    proc->sp = (uint32_t) sp;
}

static void release_process_resources(struct process *proc)
{
    if (proc->is_user && proc->user_stack_pages > 0)
        free_pages((paddr_t) proc->user_stack_base, proc->user_stack_pages);
}

static void mark_process_unused(struct process *proc)
{
    memset(proc, 0, sizeof(*proc));
    proc->state = PROC_UNUSED;
}

static struct process *find_process_by_pid(int pid)
{
    for (int i = 0; i < PROC_MAX; i++) {
        if (procs[i].state != PROC_UNUSED && procs[i].pid == pid)
            return &procs[i];
    }
    return NULL;
}

__attribute__((naked)) static void resume_from_trap(void)
{
    __asm__ __volatile__(
        "la a6, current_proc\n"
        "lw a6, 0(a6)\n"

        "li t0, %[sepc_offset]\n"
        "add t0, a6, t0\n"
        "lw t1, 0(t0)\n"
        "csrw sepc, t1\n"

        "csrr t0, sstatus\n"
        "li t1, -257\n"
        "and t0, t0, t1\n"
        "li t1, %[is_user_offset]\n"
        "add t1, a6, t1\n"
        "lw t1, 0(t1)\n"
        "bnez t1, 1f\n"
        "ori t0, t0, 0x100\n"
        "1:\n"
        "csrw sstatus, t0\n"

        "li t0, %[tf_offset]\n"
        "add a6, a6, t0\n"

        "lw ra,  %[ra](a6)\n"
        "lw gp,  %[gp](a6)\n"
        "lw tp,  %[tp](a6)\n"
        "lw t0,  %[t0](a6)\n"
        "lw t1,  %[t1](a6)\n"
        "lw t2,  %[t2](a6)\n"
        "lw t3,  %[t3](a6)\n"
        "lw t4,  %[t4](a6)\n"
        "lw t5,  %[t5](a6)\n"
        "lw t6,  %[t6](a6)\n"
        "lw a0,  %[a0](a6)\n"
        "lw a1,  %[a1](a6)\n"
        "lw a2,  %[a2](a6)\n"
        "lw a3,  %[a3](a6)\n"
        "lw a4,  %[a4](a6)\n"
        "lw a5,  %[a5](a6)\n"
        "lw a7,  %[a7](a6)\n"
        "lw s0,  %[s0](a6)\n"
        "lw s1,  %[s1](a6)\n"
        "lw s2,  %[s2](a6)\n"
        "lw s3,  %[s3](a6)\n"
        "lw s4,  %[s4](a6)\n"
        "lw s5,  %[s5](a6)\n"
        "lw s6,  %[s6](a6)\n"
        "lw s7,  %[s7](a6)\n"
        "lw s8,  %[s8](a6)\n"
        "lw s9,  %[s9](a6)\n"
        "lw s10, %[s10](a6)\n"
        "lw s11, %[s11](a6)\n"
        "lw sp,  %[sp](a6)\n"
        "lw a6,  %[a6](a6)\n"
        "sret\n"
        :
        : [sepc_offset] "i" (offsetof(struct process, sepc)),
          [is_user_offset] "i" (offsetof(struct process, is_user)),
          [tf_offset] "i" (offsetof(struct process, trap_frame)),
          [ra] "i" (offsetof(struct trap_frame, ra)),
          [gp] "i" (offsetof(struct trap_frame, gp)),
          [tp] "i" (offsetof(struct trap_frame, tp)),
          [t0] "i" (offsetof(struct trap_frame, t0)),
          [t1] "i" (offsetof(struct trap_frame, t1)),
          [t2] "i" (offsetof(struct trap_frame, t2)),
          [t3] "i" (offsetof(struct trap_frame, t3)),
          [t4] "i" (offsetof(struct trap_frame, t4)),
          [t5] "i" (offsetof(struct trap_frame, t5)),
          [t6] "i" (offsetof(struct trap_frame, t6)),
          [a0] "i" (offsetof(struct trap_frame, a0)),
          [a1] "i" (offsetof(struct trap_frame, a1)),
          [a2] "i" (offsetof(struct trap_frame, a2)),
          [a3] "i" (offsetof(struct trap_frame, a3)),
          [a4] "i" (offsetof(struct trap_frame, a4)),
          [a5] "i" (offsetof(struct trap_frame, a5)),
          [a6] "i" (offsetof(struct trap_frame, a6)),
          [a7] "i" (offsetof(struct trap_frame, a7)),
          [s0] "i" (offsetof(struct trap_frame, s0)),
          [s1] "i" (offsetof(struct trap_frame, s1)),
          [s2] "i" (offsetof(struct trap_frame, s2)),
          [s3] "i" (offsetof(struct trap_frame, s3)),
          [s4] "i" (offsetof(struct trap_frame, s4)),
          [s5] "i" (offsetof(struct trap_frame, s5)),
          [s6] "i" (offsetof(struct trap_frame, s6)),
          [s7] "i" (offsetof(struct trap_frame, s7)),
          [s8] "i" (offsetof(struct trap_frame, s8)),
          [s9] "i" (offsetof(struct trap_frame, s9)),
          [s10] "i" (offsetof(struct trap_frame, s10)),
          [s11] "i" (offsetof(struct trap_frame, s11)),
          [sp] "i" (offsetof(struct trap_frame, sp))
        : "memory");
}

struct process *create_process(uint32_t pc)
{
    struct process *proc = alloc_process_slot();
    if (!proc)
        PANIC("no free process slots");

    int index = (int) (proc - procs);
    memset(proc, 0, sizeof(*proc));
    proc->pid = index + 1;
    proc->state = PROC_RUNNABLE;
    proc->parent_pid = 0;
    proc->exit_status = 0;
    proc->is_user = false;
    proc->user_stack_base = 0;
    proc->user_stack_pages = 0;
    proc->has_trap_frame = false;
    proc->sepc = 0;
    memset(&proc->trap_frame, 0, sizeof(proc->trap_frame));
    init_switch_context(proc, pc);
    return proc;
}

struct process *create_user_process(uint32_t entry_pc)
{
    struct process *proc = alloc_process_slot();
    if (!proc)
        PANIC("no free process slots");

    int index = (int) (proc - procs);
    memset(proc, 0, sizeof(*proc));
    proc->pid = index + 1;
    proc->state = PROC_RUNNABLE;
    proc->parent_pid = 0;
    proc->exit_status = 0;
    proc->is_user = true;
    proc->user_stack_pages = USER_STACK_PAGES;
    proc->user_stack_base = alloc_pages(proc->user_stack_pages);
    proc->has_trap_frame = true;
    proc->sepc = entry_pc;
    memset(&proc->trap_frame, 0, sizeof(proc->trap_frame));
    proc->trap_frame.sp = proc->user_stack_base + proc->user_stack_pages * PAGE_SIZE;
    init_switch_context(proc, (uint32_t) resume_from_trap);
    return proc;
}

int proc_fork(struct trap_frame *f, uint32_t user_pc)
{
    struct process *parent = current_proc;
    if (!parent->is_user)
        return -1;

    struct process *child = alloc_process_slot();
    if (!child)
        return -1;

    int child_index = (int) (child - procs);
    memset(child, 0, sizeof(*child));
    child->pid = child_index + 1;
    child->state = PROC_RUNNABLE;
    child->parent_pid = parent->pid;
    child->exit_status = 0;
    child->is_user = true;
    child->user_stack_pages = parent->user_stack_pages;
    child->user_stack_base = alloc_pages(child->user_stack_pages);
    child->has_trap_frame = true;
    memcpy(child->fds, parent->fds, sizeof(child->fds));

    memcpy((void *) child->user_stack_base, (const void *) parent->user_stack_base,
           child->user_stack_pages * PAGE_SIZE);

    child->trap_frame = *f;
    child->trap_frame.a0 = 0;

    uint32_t parent_stack_base = parent->user_stack_base;
    uint32_t parent_stack_end = parent_stack_base + parent->user_stack_pages * PAGE_SIZE;
    uint32_t parent_sp = f->sp;
    if (parent_sp < parent_stack_base || parent_sp > parent_stack_end) {
        release_process_resources(child);
        mark_process_unused(child);
        return -1;
    }

    uint32_t sp_offset = parent_sp - parent_stack_base;
    child->trap_frame.sp = child->user_stack_base + sp_offset;
    child->sepc = user_pc + 4;
    init_switch_context(child, (uint32_t) resume_from_trap);

    return child->pid;
}

static int count_argv(char *const *argv)
{
    if (!argv)
        return 0;

    int argc = 0;
    while (argv[argc]) {
        argc++;
        if (argc > 32)
            return -1;
    }
    return argc;
}

int proc_exec(struct trap_frame *f, uint32_t entry_pc, uint32_t argv)
{
    if (!current_proc->is_user)
        return -1;
    if (entry_pc == 0)
        return -1;

    int argc = count_argv((char *const *) argv);
    if (argc < 0)
        return -1;

    current_proc->sepc = entry_pc;
    memset(f, 0, sizeof(*f));
    f->sp = current_proc->user_stack_base + current_proc->user_stack_pages * PAGE_SIZE;
    f->a0 = (uint32_t) argc;
    f->a1 = argv;
    current_proc->trap_frame = *f;
    current_proc->trap_frame.sp =
        current_proc->user_stack_base + current_proc->user_stack_pages * PAGE_SIZE;
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

    yield();
    PANIC("proc_exit returned pid=%d", current_proc->pid);
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

            int pid = proc->pid;
            if (status_ptr)
                *status_ptr = proc->exit_status;

            release_process_resources(proc);
            mark_process_unused(proc);
            return pid;
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
