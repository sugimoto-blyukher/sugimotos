#include "kernel/syscall.h"
#include "kernel/event.h"
#include "kernel/virtio_gpu.h"
#include "kernel/virtio_input.h"

#define USER_PATH_MAX 96
#define SSTATUS_SUM (1u << 18)

static int g_gpu_init_done;
static int g_input_init_done;

static int ensure_gpu_init(void)
{
    if (!g_gpu_init_done)
        g_gpu_init_done = (virtio_gpu_init() == 0) ? 1 : -1;
    return (g_gpu_init_done > 0) ? 0 : -1;
}

static int ensure_input_init(void)
{
    if (!g_input_init_done)
        g_input_init_done = (virtio_input_init() == 0) ? 1 : -1;
    return (g_input_init_done > 0) ? 0 : -1;
}

void handle_syscall(struct trap_frame *f, uint32_t user_pc)
{
    uint32_t next_pc = user_pc + 4;
    uint32_t sstatus_saved = READ_CSR(sstatus);
    if (current_proc && current_proc->is_user)
        WRITE_CSR(sstatus, sstatus_saved | SSTATUS_SUM);

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
            f->a0 = 0;
            break;
        case SYS_WAIT:
            if (f->a0 != 0 && !proc_user_writable_ok(f->a0, sizeof(int))) {
                f->a0 = (uint32_t) -1;
                break;
            }
            f->a0 = proc_wait((int *) f->a0);
            break;
        case SYS_EXEC: {
            uint32_t entry_pc = f->a0;
            uint32_t argv = f->a1;
            if (argv != 0 && proc_user_exec_argv_ok(argv, NULL) < 0) {
                f->a0 = (uint32_t) -1;
                break;
            }
            int ret = proc_exec(f, entry_pc, argv);
            if (ret == 0) {
                next_pc = current_proc->sepc;
            } else {
                f->a0 = (uint32_t) ret;
            }
            break;
        }
        case SYS_WAITPID:
            if (f->a1 != 0 && !proc_user_writable_ok(f->a1, sizeof(int))) {
                f->a0 = (uint32_t) -1;
                break;
            }
            f->a0 = proc_waitpid((int) f->a0, (int *) f->a1, (int) f->a2);
            break;
        case SYS_GETCHAR:
            f->a0 = (uint32_t) getchar();
            break;
        case SYS_OPEN:
            if (!proc_user_cstr_ok(f->a0, USER_PATH_MAX)) {
                f->a0 = (uint32_t) -1;
                break;
            }
            f->a0 = (uint32_t) fs_open((const char *) f->a0, (int) f->a1);
            break;
        case SYS_CLOSE:
            f->a0 = (uint32_t) fs_close((int) f->a0);
            break;
        case SYS_READ:
            if (f->a2 != 0 && !proc_user_writable_ok(f->a1, f->a2)) {
                f->a0 = (uint32_t) -1;
                break;
            }
            f->a0 = (uint32_t) fs_read((int) f->a0, (void *) f->a1, f->a2);
            break;
        case SYS_WRITE:
            f->a0 = (uint32_t) fs_write((int) f->a0, (const void *) f->a1, f->a2);
            break;
        case SYS_UNLINK:
            if (!proc_user_cstr_ok(f->a0, USER_PATH_MAX)) {
                f->a0 = (uint32_t) -1;
                break;
            }
            f->a0 = (uint32_t) fs_unlink((const char *) f->a0);
            break;
        case SYS_LISTDIR:
            if (f->a1 != 0 && !proc_user_writable_ok((uint32_t) f->a0, f->a1)) {
                f->a0 = (uint32_t) -1;
                break;
            }
            f->a0 = (uint32_t) fs_listdir((char *) f->a0, f->a1);
            break;
        case SYS_RENAME:
            if (!proc_user_cstr_ok(f->a0, USER_PATH_MAX) ||
                !proc_user_cstr_ok(f->a1, USER_PATH_MAX)) {
                f->a0 = (uint32_t) -1;
                break;
            }
            f->a0 = (uint32_t) fs_rename((const char *) f->a0, (const char *) f->a1);
            break;
        case SYS_MMAP:
            f->a0 = (uint32_t) proc_mmap(f->a0, f->a1, f->a2, f->a3);
            break;
        case SYS_MUNMAP:
            f->a0 = (uint32_t) proc_munmap(f->a0, f->a1);
            break;
        case SYS_GPU_INIT:
            f->a0 = (uint32_t) ensure_gpu_init();
            break;
        case SYS_GPU_INFO:
            if (f->a0 == 0 || !proc_user_writable_ok(f->a0, sizeof(struct sys_gpu_info))) {
                f->a0 = (uint32_t) -1;
                break;
            }
            *(struct sys_gpu_info *) f->a0 = (struct sys_gpu_info){
                .width = (uint32_t) virtio_gpu_width(),
                .height = (uint32_t) virtio_gpu_height(),
                .pitch = (uint32_t) virtio_gpu_pitch(),
                .ready = (uint32_t) virtio_gpu_is_ready(),
                .last_error = (uint32_t) virtio_gpu_last_error(),
            };
            f->a0 = 0;
            break;
        case SYS_GPU_PRESENT: {
            if (ensure_gpu_init() < 0) {
                f->a0 = (uint32_t) -1;
                break;
            }
            uint32_t src = f->a0;
            uint32_t bytes = f->a1;
            uint32_t *dst = virtio_gpu_backbuffer();
            if (!dst || src == 0 || bytes == 0) {
                f->a0 = (uint32_t) -1;
                break;
            }
            uint64_t fb_bytes64 = (uint64_t) (uint32_t) virtio_gpu_width() *
                                  (uint64_t) (uint32_t) virtio_gpu_height() * sizeof(uint32_t);
            uint32_t fb_bytes = (fb_bytes64 > 0xffffffffu) ? 0xffffffffu : (uint32_t) fb_bytes64;
            if (bytes > fb_bytes)
                bytes = fb_bytes;
            if (!proc_user_readable_ok(src, bytes)) {
                f->a0 = (uint32_t) -1;
                break;
            }
            memcpy(dst, (const void *) src, bytes);
            virtio_gpu_present();
            f->a0 = 0;
            break;
        }
        case SYS_INPUT_INIT:
            f->a0 = (uint32_t) ensure_input_init();
            break;
        case SYS_INPUT_NEXT_EVENT:
            if (f->a0 == 0 || !proc_user_writable_ok(f->a0, sizeof(struct virtio_input_event))) {
                f->a0 = (uint32_t) -1;
                break;
            }
            if (ensure_input_init() < 0) {
                f->a0 = (uint32_t) -1;
                break;
            }
            f->a0 = (uint32_t) virtio_input_next_event((struct virtio_input_event *) f->a0);
            break;
        case SYS_EVENT_POLL: {
            if (f->a0 == 0 || !proc_user_writable_ok(f->a0, sizeof(struct sys_event))) {
                f->a0 = (uint32_t) -1;
                break;
            }
            struct k_event kev;
            int rc = kevent_pop(&kev);
            if (rc <= 0) {
                f->a0 = (uint32_t) rc;
                break;
            }
            *(struct sys_event *) f->a0 = (struct sys_event){
                .type = kev.type,
                .a = kev.a,
                .b = kev.b,
                .c = kev.c,
                .d = kev.d,
                .seq = kev.seq,
            };
            f->a0 = 1;
            break;
        }
        case SYS_SHUTDOWN:
            sbi_shutdown();
            for (;;)
                __asm__ __volatile__("wfi");
            break;
        case SYS_WMCTL:
            f->a0 = (uint32_t) -1;
            break;
        default:
            f->a0 = (uint32_t) -1;
            break;
    }

    WRITE_CSR(sstatus, sstatus_saved);
    WRITE_CSR(sepc, next_pc);
}
