#include "arch/csr.h"
#include "arch/process.h"
#include "kernel/proc.h"

#define SSTATUS_SIE (1u << 1)
#define SSTATUS_SPIE (1u << 5)
#define SSTATUS_SUM (1u << 18)

void arch_init_switch_context(struct process *proc, uint32_t entry)
{
    // Match switch_context: ra, s0-s11, sstatus, then alignment padding.
    uint32_t *sp = (uint32_t *) &proc->stack[sizeof(proc->stack)] - 16;
    memset(sp, 0, 16 * sizeof(uint32_t));
    sp[0] = entry;
    uint32_t status = READ_CSR(sstatus) & ~(SSTATUS_SIE | SSTATUS_SUM);
    sp[13] = proc->is_user ? status : status | SSTATUS_SIE;
    proc->sp = (uint32_t) sp;
}

void arch_init_user_context(struct process *proc, uint32_t entry, uint32_t stack_top,
                            uint32_t argc, uint32_t argv)
{
    memset(&proc->trap_frame, 0, sizeof(proc->trap_frame));
    proc->trap_frame.sp = stack_top;
    proc->trap_frame.a0 = argc;
    proc->trap_frame.a1 = argv;
    proc->trap_frame.sepc = entry;
    proc->trap_frame.sstatus = SSTATUS_SPIE;
    proc->sepc = entry;
    proc->has_trap_frame = true;
}

uint32_t arch_user_stack_pointer(const struct process *proc)
{
    return proc->trap_frame.sp;
}

void arch_clone_user_context(struct process *child, const struct process *parent,
                             uint32_t child_sp)
{
    child->trap_frame = parent->trap_frame;
    child->trap_frame.a0 = 0;
    child->trap_frame.sp = child_sp;
    child->sepc = child->trap_frame.sepc;
    child->has_trap_frame = true;
}
