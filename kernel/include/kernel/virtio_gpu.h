#pragma once

#include "common.h"

int virtio_gpu_init(void);
int virtio_gpu_is_ready(void);
int virtio_gpu_last_error(void);

int virtio_gpu_width(void);
int virtio_gpu_height(void);
int virtio_gpu_pitch(void);

uint32_t *virtio_gpu_backbuffer(void);
void virtio_gpu_present(void);
void virtio_gpu_handle_irq(void);
