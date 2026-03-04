#pragma once

#include "kernel/kernel.h"

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

#define WNOHANG 1

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0x100
#define O_TRUNC 0x200
#define O_APPEND 0x400

void handle_syscall(struct trap_frame *f, uint32_t user_pc);
