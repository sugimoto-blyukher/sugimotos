#include "kernel/fs.h"
#include "kernel/page_alloc.h"
#include "kernel/proc.h"
#include "kernel/syscall.h"
#include "kernel/vm.h"

#define USER_STACK_PAGES 2
#define USER_STACK_TOP 0x40000000u
#define USER_MMAP_BASE 0x20000000u
#define USER_MMAP_END 0x3f000000u
#define PTE_W_BIT (1u << 2)
#define PROC_PRIORITY_MIN 1
#define PROC_PRIORITY_DEF 4

struct process procs[PROC_MAX];
struct process *current_proc;
struct process *idle_proc;

static int proc_add_vma(struct process *proc, uint32_t start, uint32_t end, uint32_t prot)
{
    if (!proc || start >= end)
        return -1;
    if (!is_aligned(start, PAGE_SIZE) || !is_aligned(end, PAGE_SIZE))
        return -1;

    for (int i = 0; i < PROC_VMA_MAX; i++) {
        if (!proc->vmas[i].used)
            continue;
        uint32_t vs = proc->vmas[i].start;
        uint32_t ve = proc->vmas[i].end;
        if (!(end <= vs || start >= ve))
            return -1;
    }

    for (int i = 0; i < PROC_VMA_MAX; i++) {
        if (proc->vmas[i].used)
            continue;
        proc->vmas[i].used = 1;
        proc->vmas[i].start = start;
        proc->vmas[i].end = end;
        proc->vmas[i].prot = prot;
        return 0;
    }
    return -1;
}

static int proc_find_vma_index(const struct process *proc, uint32_t addr)
{
    for (int i = 0; i < PROC_VMA_MAX; i++) {
        if (!proc->vmas[i].used)
            continue;
        if (addr >= proc->vmas[i].start && addr < proc->vmas[i].end)
            return i;
    }
    return -1;
}

static int proc_vma_range_ok(const struct process *proc, uint32_t addr, uint32_t len, uint32_t prot_need)
{
    if (!proc || !proc->is_user)
        return 0;
    if (len == 0)
        return 0;

    uint32_t end = addr + len;
    if (end < addr)
        return 0;

    uint32_t cur = addr;
    while (cur < end) {
        int idx = proc_find_vma_index(proc, cur);
        if (idx < 0)
            return 0;
        const struct user_vma *v = &proc->vmas[idx];
        if ((v->prot & prot_need) != prot_need)
            return 0;
        if (end <= v->end)
            return 1;
        cur = v->end;
    }
    return 1;
}

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

static void init_switch_context(struct process *proc, uint32_t ra)
{
    // Use the end of the process's own stack buffer
    uint32_t *sp = (uint32_t *) &proc->stack[sizeof(proc->stack)];
    
    // Clear the stack frame for safety
    for (int i = 0; i < 13; i++) {
        *--sp = 0;
    }
    
    // Set initial RA at the correct offset (0*4 from sp in switch_context)
    sp[0] = ra;
    
    proc->sp = (uint32_t) sp;
}

static void release_process_resources(struct process *proc)
{
    if (!proc || !proc->is_user)
        return;

    for (int i = 0; i < PROC_VMA_MAX; i++) {
        if (!proc->vmas[i].used)
            continue;
        uint32_t vs = proc->vmas[i].start;
        uint32_t ve = proc->vmas[i].end;
        if (vs < USER_MMAP_BASE || ve > USER_MMAP_END)
            continue;
        for (uint32_t va = vs; va < ve; va += PAGE_SIZE) {
            paddr_t pa = 0;
            if (vm_query_user_page(proc->satp, va, &pa, NULL) <= 0)
                continue;
            if (vm_unmap_user_page(proc->satp, va) == 0)
                free_pages(pa, 1);
        }
    }

    if (proc->user_stack_pages > 0)
        free_pages(proc->user_stack_paddr, proc->user_stack_pages);
}

static void mark_process_unused(struct process *proc)
{
    memset(proc, 0, sizeof(*proc));
    proc->state = PROC_UNUSED;
}

static void reset_process_with_released_resources(struct process *proc)
{
    release_process_resources(proc);
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

extern char __user_text_start[], __user_text_end[];
extern char __user_rodata_start[], __user_rodata_end[];
extern char __user_data_start[], __user_data_end[];
extern char __user_bss_start[], __user_bss_end[];

static int user_readable_ok(uint32_t addr, uint32_t len)
{
    return proc_vma_range_ok(current_proc, addr, len, VMA_PROT_R);
}

static int user_writable_ok(uint32_t addr, uint32_t len)
{
    return proc_vma_range_ok(current_proc, addr, len, VMA_PROT_W);
}

int proc_user_writable_ok(uint32_t addr, uint32_t len)
{
    if (!current_proc || !current_proc->is_user)
        return 1;
    return user_writable_ok(addr, len);
}

int proc_user_readable_ok(uint32_t addr, uint32_t len)
{
    if (!current_proc || !current_proc->is_user)
        return 1;
    return user_readable_ok(addr, len);
}

int proc_user_cstr_ok(uint32_t addr, uint32_t max_len)
{
    if (!current_proc || !current_proc->is_user)
        return 1;
    if (max_len == 0)
        return 0;
    if (!user_readable_ok(addr, 1))
        return 0;
    for (uint32_t i = 0; i < max_len; i++) {
        if (!user_readable_ok(addr + i, 1))
            return 0;
        if (*(const char *) (addr + i) == '\0')
            return 1;
    }
    return 0;
}

int proc_user_exec_argv_ok(uint32_t argv_ptr, int *argc_out)
{
    if (argc_out)
        *argc_out = 0;

    if (!current_proc || !current_proc->is_user)
        return -1;

    if (argv_ptr == 0) {
        if (argc_out)
            *argc_out = 0;
        return 0;
    }

    const int max_args = 32;
    for (int i = 0; i < max_args; i++) {
        uint32_t slot = argv_ptr + (uint32_t) i * sizeof(uint32_t);
        if (!user_readable_ok(slot, sizeof(uint32_t)))
            return -1;
        uint32_t argp = *(const uint32_t *) slot;
        if (argp == 0) {
            if (argc_out)
                *argc_out = i;
            return 0;
        }
        if (!proc_user_cstr_ok(argp, 128))
            return -1;
    }
    return -1;
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
        "beqz t1, 4f\n"
        "li t2, %[stack_top_offset]\n"
        "add t2, a6, t2\n"
        "csrw sscratch, t2\n"
        "j 5f\n"
        "4:\n"
        "ori t0, t0, 0x100\n"
        "csrw sscratch, zero\n"
        "5:\n"
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
        [stack_top_offset] "i" (offsetof(struct process, stack) + 32768),
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

static uint32_t page_floor(uint32_t v)
{
    return v & ~(PAGE_SIZE - 1u);
}

static uint32_t page_ceil(uint32_t v)
{
    return (v + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u);
}

static uint32_t proc_user_stack_end(const struct process *proc)
{
    return proc->user_stack_base + proc->user_stack_pages * PAGE_SIZE;
}

static int proc_init_user_vmas(struct process *proc)
{
    uint32_t ut0 = page_floor((uint32_t) __user_text_start);
    uint32_t ut1 = page_ceil((uint32_t) __user_text_end);
    uint32_t ur0 = page_floor((uint32_t) __user_rodata_start);
    uint32_t ur1 = page_ceil((uint32_t) __user_rodata_end);
    uint32_t ud0 = page_floor((uint32_t) __user_data_start);
    uint32_t ud1 = page_ceil((uint32_t) __user_data_end);
    uint32_t ub0 = page_floor((uint32_t) __user_bss_start);
    uint32_t ub1 = page_ceil((uint32_t) __user_bss_end);
    uint32_t st0 = proc->user_stack_base;
    uint32_t st1 = proc->user_stack_base + proc->user_stack_pages * PAGE_SIZE;

    memset(proc->vmas, 0, sizeof(proc->vmas));
    if (ut1 > ut0 && proc_add_vma(proc, ut0, ut1, VMA_PROT_R | VMA_PROT_X) < 0)
        return -1;
    if (ur1 > ur0 && proc_add_vma(proc, ur0, ur1, VMA_PROT_R) < 0)
        return -1;
    if (ud1 > ud0 && proc_add_vma(proc, ud0, ud1, VMA_PROT_R | VMA_PROT_W) < 0)
        return -1;
    if (ub1 > ub0 && proc_add_vma(proc, ub0, ub1, VMA_PROT_R | VMA_PROT_W) < 0)
        return -1;
    if (st1 > st0 && proc_add_vma(proc, st0, st1, VMA_PROT_R | VMA_PROT_W) < 0)
        return -1;
    return 0;
}

static int proc_sp_within_user_stack(const struct process *proc, uint32_t sp)
{
    uint32_t stack_base = proc->user_stack_base;
    uint32_t stack_end = proc_user_stack_end(proc);
    return sp >= stack_base && sp <= stack_end;
}

static int proc_apply_cow_for_writable_vmas(struct process *parent, struct process *child)
{
    for (int i = 0; i < PROC_VMA_MAX; i++) {
        if (!parent->vmas[i].used || !(parent->vmas[i].prot & VMA_PROT_W))
            continue;

        uint32_t cow_prot = parent->vmas[i].prot & ~VMA_PROT_W;
        for (uint32_t va = parent->vmas[i].start; va < parent->vmas[i].end; va += PAGE_SIZE) {
            paddr_t parent_pa = 0;
            if (vm_query_user_page(parent->satp, va, &parent_pa, NULL) <= 0)
                continue;

            paddr_t old_child_pa = 0;
            int child_had_mapping = (vm_query_user_page(child->satp, va, &old_child_pa, NULL) > 0);
            if (vm_map_user_page(parent->satp, va, parent_pa, cow_prot) < 0)
                return -1;

            page_inc_ref(parent_pa);
            if (vm_map_user_page(child->satp, va, parent_pa, cow_prot) < 0) {
                page_dec_ref(parent_pa);
                return -1;
            }

            if (child_had_mapping && old_child_pa != parent_pa)
                free_pages(old_child_pa, 1);
        }
    }

    return 0;
}

static void proc_drop_dynamic_mappings(struct process *proc)
{
    for (int i = 0; i < PROC_VMA_MAX; i++) {
        if (!proc->vmas[i].used)
            continue;
        if (proc->vmas[i].start >= USER_MMAP_BASE && proc->vmas[i].end <= USER_MMAP_END) {
            for (uint32_t va = proc->vmas[i].start; va < proc->vmas[i].end; va += PAGE_SIZE) {
                paddr_t pa = 0;
                if (vm_query_user_page(proc->satp, va, &pa, NULL) <= 0)
                    continue;
                if (vm_unmap_user_page(proc->satp, va) == 0)
                    free_pages(pa, 1);
            }
            memset(&proc->vmas[i], 0, sizeof(proc->vmas[i]));
        }
    }
}

struct process *create_process(uint32_t pc)
{
    struct process *proc = alloc_process_slot();
    if (!proc)
        return NULL;

    int index = (int) (proc - procs);
    init_process_common(proc, index + 1, 0, false, PROC_PRIORITY_MIN);
    proc->satp = vm_kernel_satp();
    init_switch_context(proc, pc);
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
    proc->has_trap_frame = true;
    proc->sepc = entry_pc;
    proc->satp = vm_build_user_satp(proc->user_stack_base, proc->user_stack_paddr, proc->user_stack_pages);
    if (!proc->satp) {
        reset_process_with_released_resources(proc);
        return NULL;
    }
    if (proc_init_user_vmas(proc) < 0) {
        reset_process_with_released_resources(proc);
        return NULL;
    }
    proc->trap_frame.sp = proc_user_stack_end(proc);
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
    init_process_common(child, child_index + 1, parent->pid, true, parent->priority);
    child->user_stack_pages = parent->user_stack_pages;
    child->user_stack_paddr = alloc_pages_try(child->user_stack_pages);
    if (!child->user_stack_paddr) {
        mark_process_unused(child);
        return -1;
    }
    child->user_stack_base = parent->user_stack_base;
    child->has_trap_frame = true;
    child->satp = vm_build_user_satp(child->user_stack_base, child->user_stack_paddr, child->user_stack_pages);
    if (!child->satp) {
        reset_process_with_released_resources(child);
        return -1;
    }
    memcpy(child->vmas, parent->vmas, sizeof(child->vmas));
    memcpy(child->fds, parent->fds, sizeof(child->fds));

    child->trap_frame = *f;
    child->trap_frame.a0 = 0;

    uint32_t parent_sp = f->sp;
    if (!proc_sp_within_user_stack(parent, parent_sp)) {
        reset_process_with_released_resources(child);
        return -1;
    }

    uint32_t sp_offset = parent_sp - parent->user_stack_base;
    child->trap_frame.sp = child->user_stack_base + sp_offset;
    child->sepc = user_pc + 4;

    if (proc_apply_cow_for_writable_vmas(parent, child) < 0) {
        reset_process_with_released_resources(child);
        return -1;
    }

    // Stack pages are now shared COW with parent.
    child->user_stack_paddr = parent->user_stack_paddr;

    init_switch_context(child, (uint32_t) resume_from_trap);

    return child->pid;
}

int proc_exec(struct trap_frame *f, uint32_t entry_pc, uint32_t argv)
{
    if (!current_proc->is_user)
        return -1;
    if (entry_pc == 0)
        return -1;

    int argc = 0;
    if (proc_user_exec_argv_ok(argv, &argc) < 0)
        return -1;

    proc_drop_dynamic_mappings(current_proc);
    if (proc_init_user_vmas(current_proc) < 0)
        return -1;

    current_proc->sepc = entry_pc;
    memset(f, 0, sizeof(*f));
    f->sp = proc_user_stack_end(current_proc);
    f->a0 = (uint32_t) argc;
    f->a1 = argv;
    current_proc->trap_frame = *f;
    current_proc->trap_frame.sp = proc_user_stack_end(current_proc);
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
        release_process_resources(proc);
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

            release_process_resources(proc);
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

static int prot_to_query_bits(uint32_t prot)
{
    uint32_t bits = 0;
    if (prot & VMA_PROT_R)
        bits |= (1u << 1);
    if (prot & VMA_PROT_W)
        bits |= (1u << 2);
    if (prot & VMA_PROT_X)
        bits |= (1u << 3);
    return (int) bits;
}

static int proc_find_free_mmap_range(struct process *proc, uint32_t len, uint32_t *addr_out)
{
    uint32_t addr = USER_MMAP_BASE;
    while (addr + len >= addr && addr + len <= USER_MMAP_END) {
        bool overlap = false;
        for (int i = 0; i < PROC_VMA_MAX; i++) {
            if (!proc->vmas[i].used)
                continue;
            uint32_t vs = proc->vmas[i].start;
            uint32_t ve = proc->vmas[i].end;
            if (addr < ve && addr + len > vs) {
                addr = page_ceil(ve);
                overlap = true;
                break;
            }
        }
        if (!overlap) {
            *addr_out = addr;
            return 0;
        }
    }
    return -1;
}

static int proc_range_overlaps_any_vma(const struct process *proc, uint32_t start, uint32_t end)
{
    for (int i = 0; i < PROC_VMA_MAX; i++) {
        if (!proc->vmas[i].used)
            continue;
        uint32_t vs = proc->vmas[i].start;
        uint32_t ve = proc->vmas[i].end;
        if (start < ve && end > vs)
            return 1;
    }
    return 0;
}

static int proc_find_free_vma_slot(struct process *proc)
{
    for (int i = 0; i < PROC_VMA_MAX; i++) {
        if (!proc->vmas[i].used)
            return i;
    }
    return -1;
}

static int proc_try_cow_fault(uint32_t va, const struct user_vma *v)
{
    if (!v || !(v->prot & VMA_PROT_W))
        return -1;

    paddr_t old_pa = 0;
    uint32_t mapped = 0;
    if (vm_query_user_page(current_proc->satp, va, &old_pa, &mapped) <= 0)
        return -1;
    if (mapped & PTE_W_BIT)
        return -1;

    paddr_t new_pa = alloc_pages_try(1);
    if (!new_pa)
        return -1;
    memcpy((void *) new_pa, (const void *) old_pa, PAGE_SIZE);
    if (vm_map_user_page(current_proc->satp, va, new_pa, v->prot) < 0) {
        free_pages(new_pa, 1);
        return -1;
    }
    page_dec_ref(old_pa);
    return 0;
}

int proc_mmap(uint32_t addr_hint, uint32_t len, uint32_t prot, uint32_t flags)
{
    if (!current_proc || !current_proc->is_user)
        return -1;
    if (len == 0)
        return -1;
    if ((prot & ~(VMA_PROT_R | VMA_PROT_W | VMA_PROT_X)) != 0 || prot == 0)
        return -1;

    len = page_ceil(len);
    uint32_t addr = 0;

    if (flags & MAP_FIXED) {
        if (!is_aligned(addr_hint, PAGE_SIZE))
            return -1;
        addr = addr_hint;
        if (addr < USER_MMAP_BASE || addr + len > USER_MMAP_END || addr + len < addr)
            return -1;
        if (proc_munmap(addr, len) < 0)
            return -1;
    } else {
        if (addr_hint != 0 && is_aligned(addr_hint, PAGE_SIZE)) {
            uint32_t hint_end = addr_hint + len;
            if (addr_hint >= USER_MMAP_BASE && hint_end <= USER_MMAP_END && hint_end >= addr_hint &&
                !proc_range_overlaps_any_vma(current_proc, addr_hint, hint_end)) {
                addr = addr_hint;
            }
        }
        if (addr == 0 && proc_find_free_mmap_range(current_proc, len, &addr) < 0)
            return -1;
    }

    if (proc_add_vma(current_proc, addr, addr + len, prot) < 0)
        return -1;
    return (int) addr;
}

int proc_munmap(uint32_t addr, uint32_t len)
{
    if (!current_proc || !current_proc->is_user)
        return -1;
    if (!is_aligned(addr, PAGE_SIZE))
        return -1;
    if (len == 0)
        return -1;
    len = page_ceil(len);

    uint32_t end = addr + len;
    if (end < addr)
        return -1;

    if (addr < USER_MMAP_BASE || end > USER_MMAP_END)
        return -1;

    int changed = 0;
    for (int i = 0; i < PROC_VMA_MAX; i++) {
        if (!current_proc->vmas[i].used)
            continue;

        uint32_t vs = current_proc->vmas[i].start;
        uint32_t ve = current_proc->vmas[i].end;
        if (ve <= addr || vs >= end)
            continue;

        uint32_t cut_s = (addr > vs) ? addr : vs;
        uint32_t cut_e = (end < ve) ? end : ve;
        if (cut_s >= cut_e)
            continue;

        changed = 1;
        if (cut_s == vs && cut_e == ve) {
            memset(&current_proc->vmas[i], 0, sizeof(current_proc->vmas[i]));
            continue;
        }
        if (cut_s == vs) {
            current_proc->vmas[i].start = cut_e;
            continue;
        }
        if (cut_e == ve) {
            current_proc->vmas[i].end = cut_s;
            continue;
        }

        int free_idx = proc_find_free_vma_slot(current_proc);
        if (free_idx < 0)
            return -1;
        current_proc->vmas[free_idx].used = 1;
        current_proc->vmas[free_idx].start = cut_e;
        current_proc->vmas[free_idx].end = ve;
        current_proc->vmas[free_idx].prot = current_proc->vmas[i].prot;
        current_proc->vmas[i].end = cut_s;
    }

    if (!changed)
        return 0;

    for (uint32_t va = addr; va < end; va += PAGE_SIZE) {
        paddr_t pa = 0;
        if (vm_query_user_page(current_proc->satp, va, &pa, NULL) <= 0)
            continue;
        if (vm_unmap_user_page(current_proc->satp, va) == 0)
            free_pages(pa, 1);
    }

    return 0;
}

int proc_handle_user_page_fault(uint32_t fault_addr, uint32_t scause)
{
    if (!current_proc || !current_proc->is_user)
        return -1;

    uint32_t need = 0;
    if (scause == 12)
        need = VMA_PROT_X;
    else if (scause == 13)
        need = VMA_PROT_R;
    else if (scause == 15)
        need = VMA_PROT_W;
    else
        return -1;

    uint32_t va = page_floor(fault_addr);
    int vma_idx = proc_find_vma_index(current_proc, va);
    if (vma_idx < 0)
        return -1;

    const struct user_vma *v = &current_proc->vmas[vma_idx];
    if ((v->prot & need) != need)
        return -1;

    uint32_t mapped_prot = 0;
    if (vm_query_user_page(current_proc->satp, va, NULL, &mapped_prot) > 0) {
        int need_bits = prot_to_query_bits(need);
        if ((mapped_prot & (uint32_t) need_bits) == (uint32_t) need_bits)
            return 0;
        if (scause == 15 && proc_try_cow_fault(va, v) == 0)
            return 0;
        return -1;
    }

    paddr_t pa = alloc_pages_try(1);
    if (!pa)
        return -1;
    if (vm_map_user_page(current_proc->satp, va, pa, v->prot) < 0) {
        free_pages(pa, 1);
        return -1;
    }
    return 0;
}
