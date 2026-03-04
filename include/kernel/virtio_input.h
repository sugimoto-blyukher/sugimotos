#pragma once

#include "common.h"

struct virtio_input_event {
    uint16_t type;
    uint16_t code;
    uint32_t value;
};

int virtio_input_init(void);
int virtio_input_next_event(struct virtio_input_event *ev);

#define VI_EV_SYN 0
#define VI_EV_KEY 1
#define VI_EV_REL 2
#define VI_EV_ABS 3

#define VI_SYN_REPORT 0
#define VI_REL_X 0
#define VI_REL_Y 1
#define VI_REL_WHEEL 8
#define VI_ABS_X 0
#define VI_ABS_Y 1
#define VI_BTN_LEFT 272
