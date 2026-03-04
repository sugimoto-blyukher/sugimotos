#include "kernel/syscall.h"
#include "kernel/wm.h"

#define USER_PATH_MAX 96
#define SSTATUS_SUM (1u << 18)

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
            PANIC("unreachable after SYS_EXIT");
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
        case SYS_SHUTDOWN:
            sbi_shutdown();
            PANIC("unreachable after SYS_SHUTDOWN");
            break;
        case SYS_WMCTL: {
            uint32_t op = f->a0;
            if (op == WMCTL_CREATE) {
                if (!proc_user_cstr_ok(f->a1, 64)) {
                    f->a0 = (uint32_t) -1;
                    break;
                }
                f->a0 = (uint32_t) wm_create((const char *) f->a1, (int) f->a2, (int) f->a3);
                break;
            }
            if (op == WMCTL_SET_TEXT) {
                if (!proc_user_cstr_ok(f->a2, WM_TEXT_MAX)) {
                    f->a0 = (uint32_t) -1;
                    break;
                }
                f->a0 = (uint32_t) wm_set_text((int) f->a1, (const char *) f->a2);
                break;
            }
            if (op == WMCTL_FOCUS) {
                f->a0 = (uint32_t) wm_focus((int) f->a1);
                break;
            }
            if (op == WMCTL_RENDER) {
                wm_render();
                f->a0 = 0;
                break;
            }
            if (op == WMCTL_POLL_MOUSE) {
                f->a0 = (uint32_t) wm_poll_mouse_input();
                break;
            }
            if (op == WMCTL_POLL_EVENT) {
                if (!proc_user_writable_ok(f->a2, sizeof(struct wm_event))) {
                    f->a0 = (uint32_t) -1;
                    break;
                }
                f->a0 = (uint32_t) wm_poll_event((int) f->a1, (struct wm_event *) f->a2);
                break;
            }
            if (op == WMCTL_CURSOR_MOVE) {
                wm_cursor_move((int) f->a1, (int) f->a2);
                f->a0 = 0;
                break;
            }
            if (op == WMCTL_DRAG_BEGIN) {
                wm_drag_begin_from_cursor();
                f->a0 = 0;
                break;
            }
            if (op == WMCTL_DRAG_END) {
                wm_drag_end();
                f->a0 = 0;
                break;
            }
            if (op == WMCTL_CLOSE) {
                f->a0 = (uint32_t) wm_close((int) f->a1);
                break;
            }
            if (op == WMCTL_SET_IMAGE) {
                int id = (int) f->a1;
                uint32_t pixels = f->a2;
                int w = (int) f->a3;
                int h = (int) f->a4;
                if (pixels == 0 || w <= 0 || h <= 0) {
                    f->a0 = (uint32_t) wm_set_image(id, NULL, 0, 0);
                    break;
                }
                uint64_t px_count = (uint64_t) (uint32_t) w * (uint64_t) (uint32_t) h;
                if (px_count == 0 || px_count > (uint64_t) 320u * 240u) {
                    f->a0 = (uint32_t) -1;
                    break;
                }
                uint32_t bytes = (uint32_t) (px_count * sizeof(uint32_t));
                if (!proc_user_writable_ok(pixels, bytes)) {
                    f->a0 = (uint32_t) -1;
                    break;
                }
                f->a0 = (uint32_t) wm_set_image(id, (const uint32_t *) pixels, w, h);
                break;
            }
            f->a0 = (uint32_t) -1;
            break;
        }
        default:
            PANIC("unknown syscall: a7=%x sepc=%x", f->a7, user_pc);
    }

    WRITE_CSR(sstatus, sstatus_saved);
    WRITE_CSR(sepc, next_pc);
}
