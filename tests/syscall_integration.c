#include "arch/trap.h"
#include "kernel/event.h"
#include "kernel/fs.h"
#include "kernel/proc.h"
#include "kernel/sbi.h"
#include "kernel/vm.h"
#include "uapi/fs.h"
#include "uapi/mman.h"
#include "uapi/process.h"
#include "uapi/syscall.h"

#define USER_CODE __attribute__((section(".user.text"), noinline))

static inline __attribute__((always_inline)) int call(uint32_t number, uint32_t arg0,
                                                     uint32_t arg1, uint32_t arg2, uint32_t arg3)
{
    register uint32_t a0 __asm__("a0") = arg0;
    register uint32_t a1 __asm__("a1") = arg1;
    register uint32_t a2 __asm__("a2") = arg2;
    register uint32_t a3 __asm__("a3") = arg3;
    register uint32_t a7 __asm__("a7") = number;
    __asm__ __volatile__("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a3), "r"(a7) : "memory");
    return (int) a0;
}

static USER_CODE __attribute__((noreturn)) void finish(int status)
{
    call(SYS_EXIT, (uint32_t) status, 0, 0, 0);
    for (;;)
        ;
}

// Exit status identifies the failing check in QEMU output.
#define CHECK(condition, status) do { if (!(condition)) finish(status); } while (0)

static USER_CODE void exec_entry(int argc, const char **argv)
{
    CHECK(argc == 1 && argv && argv[0] && argv[0][0] == 'X', 40);
    finish(37);
}

static USER_CODE void user_test(void)
{
    register uint32_t t0 __asm__("t0") = 0x12345678;
    register uint32_t t1 __asm__("t1") = 0x87654321;
    register uint32_t a0 __asm__("a0") = 0;
    register uint32_t a7 __asm__("a7") = 0xffffffffu;
    __asm__ __volatile__("ecall" : "+r"(a0), "+r"(t0), "+r"(t1) : "r"(a7) : "memory");
    CHECK((int) a0 == -1 && t0 == 0x12345678 && t1 == 0x87654321, 1);
    CHECK(call(SYS_YIELD, 0, 0, 0, 0) == 0, 2);
    CHECK(call(SYS_OPEN, 0, O_RDONLY, 0, 0) == -1, 3);
    CHECK(call(SYS_EXEC, 0, 0, 0, 0) == -1, 4);

    char path[] = {'t', 'r', 'a', 'p', 0};
    char renamed[] = {'m', 'o', 'v', 'e', 'd', 0};
    char data[] = {'h', 'e', 'l', 'l', 'o'};
    int fd = call(SYS_OPEN, (uint32_t) path, O_CREAT | O_RDWR, 0, 0);
    CHECK(fd >= 0, 5);
    CHECK(call(SYS_WRITE, (uint32_t) fd, (uint32_t) data, sizeof(data), 0) == 5, 6);
    CHECK(call(SYS_CLOSE, (uint32_t) fd, 0, 0, 0) == 0, 7);
    CHECK(call(SYS_RENAME, (uint32_t) path, (uint32_t) renamed, 0, 0) == 0, 8);
    fd = call(SYS_OPEN, (uint32_t) renamed, O_RDONLY, 0, 0);
    CHECK(fd >= 0, 9);

    // The first read faults inside the kernel while copying into a lazy user page.
    int mapped = call(SYS_MMAP, 0x21000000u, PAGE_SIZE,
                      PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANON);
    CHECK(mapped == 0x21000000, 10);
    CHECK(call(SYS_READ, (uint32_t) fd, (uint32_t) mapped, 5, 0) == 5, 11);
    const char *readback = (const char *) (uint32_t) mapped;
    CHECK(readback[0] == 'h' && readback[4] == 'o', 12);
    CHECK(call(SYS_CLOSE, (uint32_t) fd, 0, 0, 0) == 0, 13);
    CHECK(call(SYS_MUNMAP, (uint32_t) mapped, PAGE_SIZE, 0, 0) == 0, 14);
    CHECK(call(SYS_UNLINK, (uint32_t) renamed, 0, 0, 0) == 0, 15);

    int child = call(SYS_FORK, 0, 0, 0, 0);
    CHECK(child >= 0, 20);
    if (child == 0) {
        char arg[] = {'X', 0};
        const char *argv[] = {arg, NULL};
        call(SYS_EXEC, (uint32_t) exec_entry, (uint32_t) argv, 0, 0);
        finish(21); // A successful exec must never return here.
    }
    int status = -1;
    CHECK(call(SYS_WAITPID, (uint32_t) child, (uint32_t) &status, WNOHANG, 0) == 0, 22);
    CHECK(call(SYS_WAITPID, (uint32_t) child, (uint32_t) &status, 0, 0) == child, 23);
    CHECK(status == 37, 24);
    CHECK(call(SYS_WAIT, 0, 0, 0, 0) == -1, 25);
    finish(0);
}

extern char __bss[], __bss_end[];

static void idle(void)
{
    for (;;)
        yield();
}

void kernel_main(void)
{
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);
    arch_trap_init();
    kevent_init();
    if (vm_init() < 0) {
        printf("FAIL: vm_init\n");
        sbi_shutdown();
    }
    idle_proc = create_process((uint32_t) idle);
    current_proc = idle_proc;
    fs_init();
    struct process *test = create_user_process((uint32_t) user_test);
    if (!test) {
        printf("FAIL: create_user_process\n");
        sbi_shutdown();
    }
    printf("syscall integration: entering U-mode\n");
    while (test->state != PROC_ZOMBIE)
        yield();
    if (test->exit_status == 0)
        printf("PASS: syscall integration\n");
    else
        printf("FAIL: syscall integration status=%d\n", test->exit_status);
    sbi_shutdown();
}
