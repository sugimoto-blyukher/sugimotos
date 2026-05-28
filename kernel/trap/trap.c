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

static int is_syscall_scause(uint32_t scause)
{
    return scause == SCAUSE_ECALL_FROM_UMODE || scause == SCAUSE_ECALL_FROM_SMODE;
}

static int is_user_page_fault_scause(uint32_t scause)
{
    return scause == SCAUSE_INST_PAGE_FAULT || scause == SCAUSE_LOAD_PAGE_FAULT ||
           scause == SCAUSE_STORE_PAGE_FAULT;
}

static const char *scause_name(uint32_t scause)
{
    switch (scause) {
        case SCAUSE_ECALL_FROM_UMODE:
            return "ecall-u";
        case SCAUSE_ECALL_FROM_SMODE:
            return "ecall-s";
        case SCAUSE_SUPERVISOR_EXTERNAL_IRQ:
            return "sext-irq";
        case SCAUSE_INST_PAGE_FAULT:
            return "inst-page-fault";
        case SCAUSE_LOAD_PAGE_FAULT:
            return "load-page-fault";
        case SCAUSE_STORE_PAGE_FAULT:
            return "store-page-fault";
        default:
            return "unknown";
    }
}

static void dump_trap_frame(const struct trap_frame *f)
{
    if (!f) {
        printf("trap tf: <null>\n");
        return;
    }
    printf("trap tf0: ra=%x sp=%x gp=%x tp=%x\n", f->ra, f->sp, f->gp, f->tp);
    printf("trap tf1: a0=%x a1=%x a2=%x a3=%x a4=%x a5=%x a6=%x a7=%x\n",
           f->a0,
           f->a1,
           f->a2,
           f->a3,
           f->a4,
           f->a5,
           f->a6,
           f->a7);
    printf("trap tf2: t0=%x t1=%x t2=%x t3=%x t4=%x t5=%x t6=%x\n",
           f->t0,
           f->t1,
           f->t2,
           f->t3,
           f->t4,
           f->t5,
           f->t6);
    printf("trap tf3: s0=%x s1=%x s2=%x s3=%x s4=%x s5=%x s6=%x s7=%x s8=%x s9=%x s10=%x s11=%x\n",
           f->s0,
           f->s1,
           f->s2,
           f->s3,
           f->s4,
           f->s5,
           f->s6,
           f->s7,
           f->s8,
           f->s9,
           f->s10,
           f->s11);
}

static void dump_trap_context(uint32_t scause, uint32_t stval, uint32_t sepc, const struct trap_frame *f)
{
    printf("trap: scause=%x(%s) stval=%x sepc=%x sstatus=%x satp=%x sip=%x sie=%x\n",
           scause,
           scause_name(scause),
           stval,
           sepc,
           (uint32_t) READ_CSR(sstatus),
           (uint32_t) READ_CSR(satp),
           (uint32_t) READ_CSR(sip),
           (uint32_t) READ_CSR(sie));
    if (current_proc) {
        printf("trap proc: pid=%d state=%d is_user=%d sp=%x proc_sepc=%x proc_satp=%x\n",
               current_proc->pid,
               current_proc->state,
               current_proc->is_user ? 1 : 0,
               (uint32_t) current_proc->sp,
               current_proc->sepc,
               current_proc->satp);
    } else {
        printf("trap proc: none\n");
    }
    dump_trap_frame(f);
}

static void shutdown_and_halt(void)
{
    sbi_shutdown();
    for (;;)
        __asm__ __volatile__("wfi");
}

static void handle_external_interrupt(void)
{
    while (1) {
        uint32_t irq = plic_claim();
        if (irq == 0)
            break;

        // if (irq != 0) printf("trap: ext-irq %d\n", (int)irq);

        // QEMU virt maps virtio-mmio interrupts over low source IDs.
        if (irq <= 64) {
            // We could add more specific checks here if we had base addresses,
            // but the handlers already check their respective device bases.
            virtio_input_handle_irq();
            blk_handle_irq();
            virtio_gpu_handle_irq();
        }

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
    if (is_syscall_scause(scause)) {
        handle_syscall(f, user_pc);
        return;
    }

    if (current_proc && current_proc->is_user && is_user_page_fault_scause(scause)) {
        if (proc_handle_user_page_fault(stval, scause) == 0)
            return;
    }

    if (current_proc && current_proc->is_user) {
        g_last_user_scause = scause;
        g_last_user_stval = stval;
        g_last_user_sepc = user_pc;
        (void) g_user_trap_reporting;
        dump_trap_context(scause, stval, user_pc, f);
        proc_exit(128);
        return;
    }

    printf("kernel: unexpected trap; shutting down\n");
    dump_trap_context(scause, stval, user_pc, f);
    shutdown_and_halt();
}
