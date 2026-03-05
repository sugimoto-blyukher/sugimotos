#include "kernel/kernel.h"

#define KBDQ_CAP 64

static char kbdq_buf[KBDQ_CAP];
static int kbdq_head;
static int kbdq_tail;
static int kbdq_debug = 1;

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

void putchar(char ch)
{
    sbi_call(ch, 0, 0, 0, 0, 0, 0, 1);
}

int kbd_enqueue_char(char ch)
{
    int next = (kbdq_tail + 1) % KBDQ_CAP;
    if (next == kbdq_head)
        return -1;
    kbdq_buf[kbdq_tail] = ch;
    kbdq_tail = next;
    return 0;
}

int getchar(void)
{
    if (kbdq_head != kbdq_tail) {
        int ch = (unsigned char) kbdq_buf[kbdq_head];
        kbdq_head = (kbdq_head + 1) % KBDQ_CAP;
        if (kbdq_debug)
            printf("kbdq:deq ch=%d\n", ch);
        return ch;
    }
    struct sbiret ret = sbi_call(0, 0, 0, 0, 0, 0, 0, 2);
    return (int) ret.error;
}

void sbi_shutdown(void)
{
    // SBI v0.2+ SRST extension: fid=0(system reset), arg0=0(shutdown), arg1=0(no reason)
    (void) sbi_call(0, 0, 0, 0, 0, 0, 0, 0x53525354);
    while (1)
        __asm__ __volatile__("wfi");
}
