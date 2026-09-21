#pragma once

#include "common.h"

#define KEVENT_TYPE_INPUT 1u
#define KEVENT_TYPE_GPU_IRQ 2u

struct k_event {
    uint32_t type;
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint64_t seq;
};

void kevent_init(void);
int kevent_push(uint32_t type, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
int kevent_pop(struct k_event *out);
