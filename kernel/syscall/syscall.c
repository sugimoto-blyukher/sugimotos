#include "kernel/syscall.h"

void handle_syscall(struct trap_frame *f, uint32_t user_pc)
{
    uint32_t next_pc = user_pc + 4;

    switch (f->a7) {
        case SYS_PUTCHAR:
            putchar((char) f->a0);
            f->a0 = 0;
            break;
        case SYS_YIELD:
            yield();
            f->a0 = 0;
            break;
        case SYS_FORK:
            f->a0 = proc_fork(f, user_pc);
            break;
        case SYS_EXIT:
            proc_exit((int) f->a0);
            PANIC("unreachable after SYS_EXIT");
            break;
        case SYS_WAIT:
            f->a0 = proc_wait((int *) f->a0);
            break;
        case SYS_EXEC: {
            uint32_t entry_pc = f->a0;
            uint32_t argv = f->a1;
            int ret = proc_exec(f, entry_pc, argv);
            if (ret == 0) {
                next_pc = current_proc->sepc;
            } else {
                f->a0 = (uint32_t) ret;
            }
            break;
        }
        case SYS_WAITPID:
            f->a0 = proc_waitpid((int) f->a0, (int *) f->a1, (int) f->a2);
            break;
        case SYS_GETCHAR:
            f->a0 = (uint32_t) getchar();
            break;
        case SYS_OPEN:
            f->a0 = (uint32_t) fs_open((const char *) f->a0, (int) f->a1);
            break;
        case SYS_CLOSE:
            f->a0 = (uint32_t) fs_close((int) f->a0);
            break;
        case SYS_READ:
            f->a0 = (uint32_t) fs_read((int) f->a0, (void *) f->a1, f->a2);
            break;
        case SYS_WRITE:
            f->a0 = (uint32_t) fs_write((int) f->a0, (const void *) f->a1, f->a2);
            break;
        case SYS_UNLINK:
            f->a0 = (uint32_t) fs_unlink((const char *) f->a0);
            break;
        case SYS_LISTDIR:
            f->a0 = (uint32_t) fs_listdir((char *) f->a0, f->a1);
            break;
        default:
            PANIC("unknown syscall: a7=%x sepc=%x", f->a7, user_pc);
    }

    WRITE_CSR(sepc, next_pc);
}
