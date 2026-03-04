#include "kernel/kernel.h"
#include "kernel/virtio_input.h"
#include "kernel/wm.h"

extern char __bss[], __bss_end[];
extern void user_init_entry(void);

static void idle_entry(void)
{
    while (1) {
        __asm__ __volatile__("wfi");
        yield();
    }
}

void kernel_main(void)
{
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);
    WRITE_CSR(stvec, (uint32_t) kernel_entry);
    WRITE_CSR(sie, 0);
    WRITE_CSR(sstatus, READ_CSR(sstatus) & ~0x2u);

    vm_init();
    fs_init();
    wm_init();
    (void) wm_input_init();

    idle_proc = create_process((uint32_t) idle_entry);
    idle_proc->pid = 0;
    current_proc = idle_proc;

    if (!create_user_process((uint32_t) user_init_entry))
        printf("create_user_process failed\n");

    while (1)
        yield();
}
