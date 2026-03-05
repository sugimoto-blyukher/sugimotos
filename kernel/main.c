#include "kernel/kernel.h"
#include "kernel/event.h"
#include "kernel/plic.h"

extern char __bss[], __bss_end[];
extern char __stack_top[];
extern void user_init_entry(void);
extern volatile uint32_t g_last_user_scause;
extern volatile uint32_t g_last_user_stval;
extern volatile uint32_t g_last_user_sepc;

static void halt_forever(void)
{
    for (;;)
        __asm__ __volatile__("wfi");
}

static void fatal_boot_error(const char *msg)
{
    printf("%s\n", msg);
    sbi_shutdown();
    halt_forever();
}

static void idle_entry(void)
{
    while (1) {
        __asm__ __volatile__("wfi");
        yield();
    }
}

static int has_live_user_proc(void)
{
    for (int i = 0; i < PROC_MAX; i++) {
        struct process *p = &procs[i];
        if (!p->is_user)
            continue;
        if (p->state == PROC_RUNNABLE || p->state == PROC_BLOCKED)
            return 1;
    }
    return 0;
}

void kernel_main(void)
{
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);
    WRITE_CSR(stvec, (uint32_t) kernel_entry);
    WRITE_CSR(sscratch, (uint32_t) __stack_top);
    WRITE_CSR(sie, 0);
    WRITE_CSR(sstatus, READ_CSR(sstatus) & ~0x2u);

    kevent_init();
    plic_init();
    if (vm_init() < 0)
        fatal_boot_error("kernel: vm_init failed");
    fs_init();
    WRITE_CSR(sie, (1u << 9)); // SEIE
    WRITE_CSR(sstatus, READ_CSR(sstatus) | 0x2u); // SIE

    idle_proc = create_process((uint32_t) idle_entry);
    if (!idle_proc)
        fatal_boot_error("kernel: failed to create idle process");
    idle_proc->pid = 0;
    current_proc = idle_proc;

    if (!create_user_process((uint32_t) user_init_entry))
        printf("create_user_process failed\n");

    while (1) {
        proc_reap_orphan_zombies();
        if (!has_live_user_proc()) {
            printf("kernel: respawn user init (last trap scause=%x stval=%x sepc=%x)\n",
                   g_last_user_scause,
                   g_last_user_stval,
                   g_last_user_sepc);
            (void) create_user_process((uint32_t) user_init_entry);
        }
        yield();
    }
}
