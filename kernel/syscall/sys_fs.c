#include "common.h"
#include "kernel/fs.h"
#include "kernel/proc.h"
#include "kernel/usercopy.h"
#include "syscall_internal.h"

#define USER_PATH_MAX 256

int sys_open(uint32_t user_path, int flags) {
    char kpath[USER_PATH_MAX];

    if (copy_string_from_user(kpath, user_path, USER_PATH_MAX) < 0) {
        return -1;
    }

    return fs_open(kpath, flags);
}

int sys_close(int fd) {
    return fs_close(fd);
}

int sys_read(int fd, uint32_t user_buf, uint32_t len) {
    if (len != 0 && !proc_user_writable_ok(user_buf, len)) {
        return -1;
    }

    return fs_read(fd, (void *)user_buf, len);
}

int sys_write(int fd, uint32_t user_buf, uint32_t len) {
    if (len != 0 && !proc_user_readable_ok(user_buf, len)) {
        return -1;
    }

    return fs_write(fd, (const void *)user_buf, len);
}

int sys_unlink(uint32_t user_path) {
    char kpath[USER_PATH_MAX];

    if (copy_string_from_user(kpath, user_path, USER_PATH_MAX) < 0) {
        return -1;
    }

    return fs_unlink(kpath);
}

int sys_rename(uint32_t user_old_path, uint32_t user_new_path) {
    char kold_path[USER_PATH_MAX];
    char knew_path[USER_PATH_MAX];

    if (copy_string_from_user(kold_path, user_old_path, USER_PATH_MAX) < 0 ||
        copy_string_from_user(knew_path, user_new_path, USER_PATH_MAX) < 0) {
        return -1;
    }

    return fs_rename(kold_path, knew_path);
}

int sys_listdir(uint32_t user_buf, uint32_t len) {
    if (len != 0 && !proc_user_writable_ok(user_buf, len)) {
        return -1;
    }

    return fs_listdir((void *)user_buf, len);
}
