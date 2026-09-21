#pragma once

#include "common.h"

/*
 * Internal syscall handlers.
 *
 * Shared by the dispatcher and adapters inside kernel/syscall/.
 * Do not expose this header to userland.
 */

/* console */
int sys_putchar(int ch);
int sys_getchar(void);

/* process */
int sys_yield(void);
int sys_exit(int status);
int sys_fork(void);
int sys_exec(uint32_t entry_pc, uint32_t argv);
int sys_wait(uint32_t status_ptr);
int sys_waitpid(int pid, uint32_t status_ptr, int options);

/* filesystem */
int sys_open(uint32_t user_path, int flags);
int sys_close(int fd);
int sys_read(int fd, uint32_t user_buf, uint32_t len);
int sys_write(int fd, uint32_t user_buf, uint32_t len);
int sys_unlink(uint32_t user_path);
int sys_rename(uint32_t user_old_path, uint32_t user_new_path);
int sys_listdir(uint32_t user_buf, uint32_t len);

/* virtual memory */
int sys_mmap(uint32_t user_addr, uint32_t length, uint32_t prot, uint32_t flags);
int sys_munmap(uint32_t user_addr, uint32_t length);

/* graphics / input / window manager */
int sys_gpu_info(uint32_t user_info);
int sys_gpu_flush(void);
int sys_get_event(uint32_t user_event);
int sys_wmctl(int cmd, uint32_t user_arg);
