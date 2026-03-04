#include "kernel/syscall.h"

#define SCAUSE_ECALL_FROM_UMODE 8

void handle_trap(struct trap_frame *f)
{
    uint32_t scause = READ_CSR(scause);
    uint32_t stval = READ_CSR(stval);
    uint32_t user_pc = READ_CSR(sepc);

    if (scause == SCAUSE_ECALL_FROM_UMODE) {
        handle_syscall(f, user_pc);
        return;
    }

    PANIC("unexpected trap scause=%x, stval=%x, sepc=%x", scause, stval, user_pc);
}
