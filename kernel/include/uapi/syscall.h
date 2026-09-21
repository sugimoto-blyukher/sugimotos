#pragma once

#define SYS_PUTCHAR 1
#define SYS_GETCHAR 2
#define SYS_YIELD   3
#define SYS_EXIT    4
#define SYS_FORK    5
#define SYS_EXEC    6
#define SYS_WAIT    7
#define SYS_WAITPID 8

#define SYS_OPEN    20
#define SYS_CLOSE   21
#define SYS_READ    22
#define SYS_WRITE   23
#define SYS_UNLINK  24
#define SYS_RENAME  25
#define SYS_LISTDIR 26

#define SYS_MMAP    40
#define SYS_MUNMAP  41

#define SYS_GPU_INFO      60
#define SYS_GPU_FLUSH     61
#define SYS_GET_EVENT     62
#define SYS_WMCTL         63
