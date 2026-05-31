#pragma once

#include "common.h"
#include "kernel/trap.h"

#define SYS_PUTCHAR 1
#define SYS_YIELD 2
#define SYS_FORK 3
#define SYS_EXIT 4
#define SYS_WAIT 5
#define SYS_EXEC 6
#define SYS_WAITPID 7
#define SYS_GETCHAR 8
#define SYS_OPEN 9
#define SYS_CLOSE 10
#define SYS_READ 11
#define SYS_WRITE 12
#define SYS_UNLINK 13
#define SYS_LISTDIR 14
#define SYS_SHUTDOWN 15
#define SYS_RENAME 16
#define SYS_WMCTL 17
#define SYS_MMAP 18
#define SYS_MUNMAP 19
#define SYS_GPU_INIT 20
#define SYS_GPU_INFO 21
#define SYS_GPU_PRESENT 22
#define SYS_INPUT_INIT 23
#define SYS_INPUT_NEXT_EVENT 24
#define SYS_EVENT_POLL 25

#define WMCTL_CREATE 1
#define WMCTL_SET_TEXT 2
#define WMCTL_FOCUS 3
#define WMCTL_RENDER 4
#define WMCTL_POLL_MOUSE 5
#define WMCTL_POLL_EVENT 6
#define WMCTL_CURSOR_MOVE 7
#define WMCTL_DRAG_BEGIN 8
#define WMCTL_DRAG_END 9
#define WMCTL_CLOSE 10
#define WMCTL_SET_IMAGE 11

#define WNOHANG 1

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0x100
#define O_TRUNC 0x200
#define O_APPEND 0x400

#define MAP_FIXED 1u

struct sys_gpu_info {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t ready;
    uint32_t last_error;
};

struct sys_event {
    uint32_t type;
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint64_t seq;
};

void handle_syscall(struct trap_frame *f, uint32_t user_pc);
