#pragma once

#include "common.h"

#define GPU_FORMAT_BGRA8888 1u

struct gpu_info {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t format;
    uint32_t framebuffer;
};
