#pragma once

#include "common.h"

#define EVENT_NONE 0
#define EVENT_INPUT 1
#define EVENT_GPU_IRQ 2

struct sys_event {
    uint32_t type;
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint64_t seq;
};
