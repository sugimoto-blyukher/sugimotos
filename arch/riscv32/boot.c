#include "kernel/kernel.h"

extern char __stack_top[];

__attribute__((section(".text.boot")))
__attribute__((naked)) void boot(void)
{
    __asm__ __volatile__(
        "mv sp, %[stack_top]\n"
        "j kernel_main\n"
        :
        : [stack_top] "r"(__stack_top));
}
