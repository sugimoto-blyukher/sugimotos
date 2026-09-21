#pragma once

#include "common.h"

int ext4_mount(void);
int ext4_lookup(const char *name);
int ext4_read_file(int file_index, uint32_t offset, void *buf, uint32_t len);
int ext4_listdir(char *buf, uint32_t len);
