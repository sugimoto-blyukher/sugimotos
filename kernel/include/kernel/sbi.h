#pragma once

#include "common.h"

struct sbiret {
    long error;
    long value;
};

struct sbiret sbi_call(long arg0, long arg1, long arg2, long arg3, long arg4,
                       long arg5, long fid, long eid);
void putchar(char ch);
int getchar(void);
int kbd_enqueue_char(char ch);
void sbi_shutdown(void);
