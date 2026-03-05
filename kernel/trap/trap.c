#include "kernel/syscall.h"
#include "kernel/plic.h"
#include "kernel/blk.h"
#include "kernel/virtio_gpu.h"
#include "kernel/virtio_input.h"

#define SCAUSE_ECALL_FROM_UMODE 8
#define SCAUSE_ECALL_FROM_SMODE 9
#define SCAUSE_INTERRUPT_FLAG 0x80000000u
#define SCAUSE_SUPERVISOR_EXTERNAL_IRQ (SCAUSE_INTERRUPT_FLAG | 9u)
#define SCAUSE_INST_PAGE_FAULT 12
#define SCAUSE_LOAD_PAGE_FAULT 13
#define SCAUSE_STORE_PAGE_FAULT 15

static int g_user_trap_reporting;
volatile uint32_t g_last_user_scause;
volatile uint32_t g_last_user_stval;
volatile uint32_t g_last_user_sepc;

static void handle_external_interrupt(void)
{
    while (1) {
        uint32_t irq = plic_claim();
        if (irq == 0)
            break;

        // QEMU virt maps virtio-mmio interrupts over low source IDs.
        if (irq <= 8)
            virtio_input_handle_irq();
        if (irq <= 32)
            blk_handle_irq();
        if (irq <= 32)
            virtio_gpu_handle_irq();

        plic_complete(irq);
    }
}

void handle_trap(struct trap_frame *f)
{
    uint32_t scause = READ_CSR(scause);
    uint32_t stval = READ_CSR(stval);
    uint32_t user_pc = READ_CSR(sepc);
    if (scause == SCAUSE_SUPERVISOR_EXTERNAL_IRQ) {
        handle_external_interrupt();
        return;
    }
    if (scause == SCAUSE_ECALL_FROM_UMODE || scause == SCAUSE_ECALL_FROM_SMODE) {
        handle_syscall(f, user_pc);
        return;
    }

    if (current_proc && current_proc->is_user &&
        (scause == SCAUSE_INST_PAGE_FAULT || scause == SCAUSE_LOAD_PAGE_FAULT ||
         scause == SCAUSE_STORE_PAGE_FAULT)) {
        if (proc_handle_user_page_fault(stval, scause) == 0)
            return;
    }

    if (current_proc && current_proc->is_user) {
        g_last_user_scause = scause;
        g_last_user_stval = stval;
        g_last_user_sepc = user_pc;
        (void) g_user_trap_reporting;
        proc_exit(128);
        return;
    }

    printf("kernel: unexpected trap scause=%x stval=%x sepc=%x; shutting down\n",
           scause,
           stval,
           user_pc);
    sbi_shutdown();
    for (;;)
        __asm__ __volatile__("wfi");
}
