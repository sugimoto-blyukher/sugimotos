#include "arch/csr.h"
#include "arch/trap.h"
#include "kernel/proc.h"
#include "kernel/syscall.h"

#define SSTATUS_SPP (1u << 8)
#define SSTATUS_SUM (1u << 18)

void arch_handle_ecall(struct trap_frame *f)
{
    uint32_t number = f->a7;
    uint32_t args[6] = {f->a0, f->a1, f->a2, f->a3, f->a4, f->a5};
    struct process *proc = current_proc;
    bool from_user = !(f->sstatus & SSTATUS_SPP);

    // ECALL is always four bytes. Fork inherits the PC after this instruction.
    f->sepc += 4;
    if (from_user && proc) {
        proc->trap_frame = *f;
        proc->sepc = f->sepc;
        proc->has_trap_frame = true;
        WRITE_CSR(sstatus, READ_CSR(sstatus) | SSTATUS_SUM);
    }

    struct syscall_result result = syscall_dispatch(number, args);
    if (result.context_replaced && from_user && proc) {
        *f = proc->trap_frame;
    } else {
        f->a0 = (uint32_t) result.value;
    }

    if (from_user && proc) {
        proc->trap_frame = *f;
        proc->sepc = f->sepc;
    }
    // entry.c restores the saved PC and privilege state, including SUM, on sret.
}
