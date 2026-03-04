#include "kernel.h"
#include "common.h"

extern char __bss[], __bss_end[], __stack_top[];
extern char __free_ram[], __free_ram_end[];

#define SCAUSE_BREAKPOINT 3

#define SYS_FORK 1

paddr_t alloc_pages(uint32_t n)
{
    static paddr_t next_paddr = (paddr_t) __free_ram;
    paddr_t paddr = next_paddr;
    next_paddr += n * PAGE_SIZE;

    if (next_paddr > (paddr_t)__free_ram_end)
        PANIC("out of memory");

    memset((void *)paddr, 0, n * PAGE_SIZE);
    return paddr;
}

struct sbiret sbi_call(long arg0, long arg1, long arg2, long arg3, long arg4,
                       long arg5, long fid, long eid)
{
    register long a0 __asm__("a0") = arg0;
    register long a1 __asm__("a1") = arg1;
    register long a2 __asm__("a2") = arg2;
    register long a3 __asm__("a3") = arg3;
    register long a4 __asm__("a4") = arg4;
    register long a5 __asm__("a5") = arg5;
    register long a6 __asm__("a6") = fid;
    register long a7 __asm__("a7") = eid;

    __asm__ __volatile__("ecall"
                         : "=r"(a0), "=r"(a1)
                         : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5),
                           "r"(a6), "r"(a7)
                         : "memory");

    return (struct sbiret){.error = a0, .value = a1};
}

struct process procs[PROC_MAX];

void putchar(char ch)
{
    sbi_call(ch, 0, 0, 0, 0, 0, 0, 1);
}

__attribute__((naked))
__attribute__((aligned(4))) void
kernel_entry(void)
{
    __asm__ __volatile__(
        // 実行中プロセスのカーネルスタックをsscratchから取り出す
        // tmp = sp; sp = sscratch; sscratch = tmp;
        "csrrw sp, sscratch, sp\n"
        "addi sp, sp, -4 * 31\n"
        "sw ra,  4 * 0(sp)\n"
        "sw gp,  4 * 1(sp)\n"
        "sw tp,  4 * 2(sp)\n"
        "sw t0,  4 * 3(sp)\n"
        "sw t1,  4 * 4(sp)\n"
        "sw t2,  4 * 5(sp)\n"
        "sw t3,  4 * 6(sp)\n"
        "sw t4,  4 * 7(sp)\n"
        "sw t5,  4 * 8(sp)\n"
        "sw t6,  4 * 9(sp)\n"
        "sw a0,  4 * 10(sp)\n"
        "sw a1,  4 * 11(sp)\n"
        "sw a2,  4 * 12(sp)\n"
        "sw a3,  4 * 13(sp)\n"
        "sw a4,  4 * 14(sp)\n"
        "sw a5,  4 * 15(sp)\n"
        "sw a6,  4 * 16(sp)\n"
        "sw a7,  4 * 17(sp)\n"
        "sw s0,  4 * 18(sp)\n"
        "sw s1,  4 * 19(sp)\n"
        "sw s2,  4 * 20(sp)\n"
        "sw s3,  4 * 21(sp)\n"
        "sw s4,  4 * 22(sp)\n"
        "sw s5,  4 * 23(sp)\n"
        "sw s6,  4 * 24(sp)\n"
        "sw s7,  4 * 25(sp)\n"
        "sw s8,  4 * 26(sp)\n"
        "sw s9,  4 * 27(sp)\n"
        "sw s10, 4 * 28(sp)\n"
        "sw s11, 4 * 29(sp)\n"

        // 例外発生時のspを取り出して保存
        "csrr a0, sscratch\n"
        "sw a0,  4 * 30(sp)\n"

        // カーネルスタックを設定し直す
        "addi a0, sp, 4 * 31\n"
        "csrw sscratch, a0\n"

        "mv a0, sp\n"
        "call handle_trap\n"

        "lw ra,  4 * 0(sp)\n"
        "lw gp,  4 * 1(sp)\n"
        "lw tp,  4 * 2(sp)\n"
        "lw t0,  4 * 3(sp)\n"
        "lw t1,  4 * 4(sp)\n"
        "lw t2,  4 * 5(sp)\n"
        "lw t3,  4 * 6(sp)\n"
        "lw t4,  4 * 7(sp)\n"
        "lw t5,  4 * 8(sp)\n"
        "lw t6,  4 * 9(sp)\n"
        "lw a0,  4 * 10(sp)\n"
        "lw a1,  4 * 11(sp)\n"
        "lw a2,  4 * 12(sp)\n"
        "lw a3,  4 * 13(sp)\n"
        "lw a4,  4 * 14(sp)\n"
        "lw a5,  4 * 15(sp)\n"
        "lw a6,  4 * 16(sp)\n"
        "lw a7,  4 * 17(sp)\n"
        "lw s0,  4 * 18(sp)\n"
        "lw s1,  4 * 19(sp)\n"
        "lw s2,  4 * 20(sp)\n"
        "lw s3,  4 * 21(sp)\n"
        "lw s4,  4 * 22(sp)\n"
        "lw s5,  4 * 23(sp)\n"
        "lw s6,  4 * 24(sp)\n"
        "lw s7,  4 * 25(sp)\n"
        "lw s8,  4 * 26(sp)\n"
        "lw s9,  4 * 27(sp)\n"
        "lw s10, 4 * 28(sp)\n"
        "lw s11, 4 * 29(sp)\n"
        "lw sp,  4 * 30(sp)\n"
        "sret\n");
}

__attribute__((naked)) void switch_context(uint32_t *prev_sp, uint32_t *next_sp)
{
    __asm__ __volatile__(
        // 実行中プロセスのスタックへレジスタを保存
        "addi sp, sp, -13 * 4\n"
        "sw ra,  0  * 4(sp)\n"
        "sw s0,  1  * 4(sp)\n"
        "sw s1,  2  * 4(sp)\n"
        "sw s2,  3  * 4(sp)\n"
        "sw s3,  4  * 4(sp)\n"
        "sw s4,  5  * 4(sp)\n"
        "sw s5,  6  * 4(sp)\n"
        "sw s6,  7  * 4(sp)\n"
        "sw s7,  8  * 4(sp)\n"
        "sw s8,  9  * 4(sp)\n"
        "sw s9,  10 * 4(sp)\n"
        "sw s10, 11 * 4(sp)\n"
        "sw s11, 12 * 4(sp)\n"

        // スタックポインタの切り替え
        "sw sp, (a0)\n"
        "lw sp, (a1)\n"

        // 次のプロセスのスタックからレジスタを復元
        "lw ra,  0  * 4(sp)\n"
        "lw s0,  1  * 4(sp)\n"
        "lw s1,  2  * 4(sp)\n"
        "lw s2,  3  * 4(sp)\n"
        "lw s3,  4  * 4(sp)\n"
        "lw s4,  5  * 4(sp)\n"
        "lw s5,  6  * 4(sp)\n"
        "lw s6,  7  * 4(sp)\n"
        "lw s7,  8  * 4(sp)\n"
        "lw s8,  9  * 4(sp)\n"
        "lw s9,  10 * 4(sp)\n"
        "lw s10, 11 * 4(sp)\n"
        "lw s11, 12 * 4(sp)\n"
        "addi sp, sp, 13 * 4\n"
        "ret\n");
}

struct process *current_proc;
struct process *idle_proc;
struct process *proc_a;
struct process *proc_b;

static struct process *alloc_process_slot(void)
{
    for (int i = 0; i < PROC_MAX; i++)
    {
        if (procs[i].state == PROC_UNUSED)
        {
            return &procs[i];
        }
    }
    return NULL;
}

static void init_switch_context(struct process *proc, uint32_t ra)
{
    uint32_t *sp = (uint32_t *) &proc->stack[sizeof(proc->stack)];
    *--sp = 0;             // s11
    *--sp = 0;             // s10
    *--sp = 0;             // s9
    *--sp = 0;             // s8
    *--sp = 0;             // s7
    *--sp = 0;             // s6
    *--sp = 0;             // s5
    *--sp = 0;             // s4
    *--sp = 0;             // s3
    *--sp = 0;             // s2
    *--sp = 0;             // s1
    *--sp = 0;             // s0
    *--sp = ra;            // ra
    proc->sp = (uint32_t) sp;
}

__attribute__((naked)) static void resume_from_trap(void)
{
    __asm__ __volatile__(
        "la a6, current_proc\n"
        "lw a6, 0(a6)\n"

        "li t0, %[sepc_offset]\n"
        "add t0, a6, t0\n"
        "lw t1, 0(t0)\n"
        "csrw sepc, t1\n"

        "li t0, %[tf_offset]\n"
        "add a6, a6, t0\n"
        "csrr t0, sstatus\n"
        "ori t0, t0, 0x100\n"
        "csrw sstatus, t0\n"

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
        : [sepc_offset] "i" (offsetof(struct process, sepc)),
          [tf_offset] "i" (offsetof(struct process, trap_frame)),
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

struct process *create_process(uint32_t pc)
{
    struct process *proc = alloc_process_slot();
    if (!proc)
        PANIC("no free process slots");

    int index = (int) (proc - procs);
    proc->pid = index + 1;
    proc->state = PROC_RUNNABLE;
    proc->has_trap_frame = false;
    proc->sepc = 0;
    memset(&proc->trap_frame, 0, sizeof(proc->trap_frame));
    init_switch_context(proc, pc);
    return proc;
}

static int sys_fork(struct trap_frame *f, uint32_t user_pc)
{
    struct process *parent = current_proc;
    struct process *child = alloc_process_slot();
    if (!child)
        return -1;

    int child_index = (int) (child - procs);
    child->pid = child_index + 1;
    child->state = PROC_RUNNABLE;
    child->has_trap_frame = true;

    memcpy(child->stack, parent->stack, sizeof(child->stack));

    child->trap_frame = *f;
    child->trap_frame.a0 = 0;

    uint32_t parent_stack_base = (uint32_t) &parent->stack[0];
    uint32_t child_stack_base = (uint32_t) &child->stack[0];
    uint32_t parent_sp = f->sp;
    if (parent_sp < parent_stack_base ||
        parent_sp >= parent_stack_base + sizeof(parent->stack))
    {
        child->state = PROC_UNUSED;
        return -1;
    }

    uint32_t sp_offset = parent_sp - parent_stack_base;
    child->trap_frame.sp = child_stack_base + sp_offset;
    child->sepc = user_pc + 4;

    init_switch_context(child, (uint32_t) resume_from_trap);

    return child->pid;
}

static void handle_syscall(struct trap_frame *f, uint32_t user_pc)
{
    int ret;

    switch (f->a7)
    {
        case SYS_FORK:
            ret = sys_fork(f, user_pc);
            f->a0 = ret;
            WRITE_CSR(sepc, user_pc + 4);
            break;
        default:
            PANIC("unknown syscall: a7=%x sepc=%x", f->a7, user_pc);
    }
}

void handle_trap(struct trap_frame *f)
{
    uint32_t scause = READ_CSR(scause);
    uint32_t stval = READ_CSR(stval);
    uint32_t user_pc = READ_CSR(sepc);

    if (scause == SCAUSE_BREAKPOINT)
    {
        handle_syscall(f, user_pc);
        return;
    }

    PANIC("unexpected trap scause=%x, stval=%x, sepc=%x", scause, stval, user_pc);
}

void delay(void) {
    for (int i = 0; i < 30000000; i++)
        __asm__ __volatile__("nop"); //何もしない命令
}

void yield(void) {
    //実行可能なプロセスを探す
    struct process *next = idle_proc;
    int current_index = (int) (current_proc - procs);
    for (int i = 1; i <= PROC_MAX; i++) {
        struct process *proc = &procs[(current_index + i) % PROC_MAX];
        if (proc->state == PROC_RUNNABLE && proc->pid > 0) {
            next = proc;
            break;
        }
    }

    //現在進行中のプロセス以外に、実行可能なプロセスがない、戻って作業を続行する
    if (next == current_proc)
        return;

    __asm__ __volatile__(
        "csrw sscratch, %[sscratch]\n"
        :
        : [sscratch] "r" ((uint32_t) &next->stack[sizeof(next->stack)])
    );

    //コンテキストスイッチ
    struct process *prev = current_proc;
    current_proc = next;
    switch_context(&prev->sp, &next->sp);
}

static int fork_syscall(void)
{
    register uint32_t a0 __asm__("a0");
    register uint32_t a7 __asm__("a7") = SYS_FORK;

    __asm__ __volatile__("ebreak"
                         : "=r" (a0)
                         : "r" (a7)
                         : "memory");

    return (int) a0;
}

void proc_a_entry(void) {
    printf("starting process A\n");

    int role = 0;
    int once = 0;

    while (1) {
        if (!once) {
            int pid = fork_syscall();
            if (pid < 0) {
                printf("fork failed\n");
            } else if (pid == 0) {
                role = 1;
                printf("fork child started\n");
            } else {
                printf("fork parent child_pid=%d\n", pid);
            }
            once = 1;
        }

        putchar(role ? 'C' : 'A');
        yield();
        delay();
    }
}

void proc_b_entry(void) {
    printf("starting process B\n");
    while (1) {
        putchar('B');
        yield();
        delay();
    }
}

void kernel_main(void)
{
    memset(__bss, 0, (size_t)__bss_end - (size_t)__bss);
    WRITE_CSR(stvec, (uint32_t)kernel_entry);

    idle_proc = create_process((uint32_t) kernel_entry);
    idle_proc->pid = 0; // idle
    current_proc = idle_proc;

    proc_a = create_process((uint32_t) proc_a_entry);
    proc_b = create_process((uint32_t) proc_b_entry);

    yield();
    PANIC("switch to idle process");

    for (;;)
    {
        __asm__ __volatile__("wfi");
    }

}

__attribute__((section(".text.boot")))
__attribute__((naked)) void
boot(void)
{
    __asm__ __volatile__(
        "mv sp, %[stack_top]\n"
        "j kernel_main\n"
        :
        : [stack_top] "r"(__stack_top));
}
