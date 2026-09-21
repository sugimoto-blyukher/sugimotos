#include "kernel/syscall.h"
#include "uapi/syscall.h"
#include "syscall_internal.h"

struct syscall_result syscall_dispatch(uint32_t number, const uint32_t args[6])
{
    int value;

    switch (number) {
        case SYS_PUTCHAR:
            value = sys_putchar((int) args[0]);
            break;
        case SYS_GETCHAR:
            value = sys_getchar();
            break;
        case SYS_YIELD:
            value = sys_yield();
            break;
        case SYS_EXIT:
            value = sys_exit((int) args[0]);
            break;
        case SYS_FORK:
            value = sys_fork();
            break;
        case SYS_WAIT:
            value = sys_wait(args[0]);
            break;
        case SYS_WAITPID:
            value = sys_waitpid((int) args[0], args[1], (int) args[2]);
            break;
        case SYS_OPEN:
            value = sys_open(args[0], (int) args[1]);
            break;
        case SYS_CLOSE:
            value = sys_close((int) args[0]);
            break;
        case SYS_READ:
            value = sys_read((int) args[0], args[1], args[2]);
            break;
        case SYS_WRITE:
            value = sys_write((int) args[0], args[1], args[2]);
            break;
        case SYS_UNLINK:
            value = sys_unlink(args[0]);
            break;
        case SYS_RENAME:
            value = sys_rename(args[0], args[1]);
            break;
        case SYS_LISTDIR:
            value = sys_listdir(args[0], args[1]);
            break;
        case SYS_MMAP:
            value = sys_mmap(args[0], args[1], args[2], args[3]);
            break;
        case SYS_MUNMAP:
            value = sys_munmap(args[0], args[1]);
            break;
        case SYS_GPU_INFO:
            value = sys_gpu_info(args[0]);
            break;
        case SYS_GPU_FLUSH:
            value = sys_gpu_flush();
            break;
        case SYS_GET_EVENT:
            value = sys_get_event(args[0]);
            break;
        case SYS_WMCTL:
            value = sys_wmctl((int) args[0], args[1]);
            break;
        case SYS_EXEC:
            value = sys_exec(args[0], args[1]);
            return (struct syscall_result){value, value == 0};
        default:
            value = -1;
            break;
    }
    return (struct syscall_result){value, false};
}
