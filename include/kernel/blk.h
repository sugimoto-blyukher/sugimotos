#pragma once

#include "common.h"

int blk_init(void);
int blk_read(uint32_t sector, void *buf);
