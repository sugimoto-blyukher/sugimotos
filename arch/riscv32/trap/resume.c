#include "arch/process.h"
#include "kernel/proc.h"

__attribute__((naked)) void arch_resume_from_trap(void)
{
    __asm__ __volatile__(
        "la a6, current_proc\n"
        "lw a6, 0(a6)\n"

        "li t0, %[tf_offset]\n"
        "add a6, a6, t0\n"
        "lw t0, %[sepc](a6)\n"
        "csrw sepc, t0\n"
        "lw t0, %[sstatus](a6)\n"
        "csrw sstatus, t0\n"
        "la t0, current_proc\n"
        "lw t0, 0(t0)\n"
        "li t1, %[stack_top_offset]\n"
        "add t0, t0, t1\n"
        "csrw sscratch, t0\n"

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
        : [tf_offset] "i" (offsetof(struct process, trap_frame)),
          [stack_top_offset] "i" (offsetof(struct process, stack) + sizeof(((struct process *) 0)->stack)),
          [sepc] "i" (offsetof(struct trap_frame, sepc)),
          [sstatus] "i" (offsetof(struct trap_frame, sstatus)),
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

