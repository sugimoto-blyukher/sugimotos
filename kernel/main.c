#include "arch/csr.h"
#include "kernel/event.h"
#include "kernel/fs.h"
#include "kernel/proc.h"
#include "kernel/plic.h"
#include "kernel/sbi.h"
#include "kernel/trap.h"
#include "kernel/vm.h"

void kernel_main(void);

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
    while (1)
    {
        __asm__ __volatile__("wfi");
        yield();
    }
}

static int has_live_app_proc(void)
{
    for (int i = 0; i < PROC_MAX; i++)
    {
        struct process *p = &procs[i];
        if (p->pid == 0)
            continue;
        if (p->state == PROC_RUNNABLE || p->state == PROC_BLOCKED)
            return 1;
    }
    return 0;
}

void kernel_main(void)
{
    memset(__bss, 0, (size_t)__bss_end - (size_t)__bss);
    printf("  _____ _    _  _____ _____ __  __  ____ _______ ____   _____ \n");
    printf(" / ____| |  | |/ ____|_   _|  \\/  |/ __ \\__   __/ __ \\ / ____|\n");
    printf("| (___ | |  | | |  __  | | | \\  / | |  | | | | | |  | | (___  \n");
    printf(" \\___ \\| |  | | | |_ | | | | |\\/| | |  | | | | | |  | |\\___ \\ \n");
    printf(" ____) | |__| | |__| |_| |_| |  | | |__| | | | | |__| |____) |\n");
    printf("|_____/ \\____/ \\_____|_____|_|  |_|\\____/  |_|  \\____/|_____/ \n");

    WRITE_CSR(stvec, (uint32_t)kernel_entry);
    WRITE_CSR(sscratch, 0);
    WRITE_CSR(sie, 0);
    WRITE_CSR(sstatus, READ_CSR(sstatus) & ~0x2u);

    printf("kernel: initializing events...\n");
    kevent_init();
    printf("kernel: initializing plic...\n");
    plic_init();
    printf("kernel: initializing vm...\n");
    if (vm_init() < 0)
        fatal_boot_error("kernel: vm_init failed");

    printf("kernel: creating idle process...\n");
    idle_proc = create_process((uint32_t)idle_entry);
    if (!idle_proc)
        fatal_boot_error("kernel: failed to create idle process");
    current_proc = idle_proc;

    printf("kernel: enabling interrupts...\n");
    WRITE_CSR(sie, (1u << 9));                    // SEIE
    WRITE_CSR(sstatus, READ_CSR(sstatus) | 0x2u); // SIE

    printf("kernel: initializing fs...\n");
    fs_init();

    printf("kernel: creating shell process...\n");
    if (!create_process((uint32_t)user_init_entry))
        printf("create_process failed\n");

    printf("kernel: entering scheduler loop...\n");
    while (1)
    {
        proc_reap_orphan_zombies();
        if (!has_live_app_proc())
        {
            printf("kernel: respawn shell init (last trap scause=%x stval=%x sepc=%x)\n",
                   g_last_user_scause,
                   g_last_user_stval,
                   g_last_user_sepc);
            (void)create_process((uint32_t)user_init_entry);
        }
        yield();
    }
}
