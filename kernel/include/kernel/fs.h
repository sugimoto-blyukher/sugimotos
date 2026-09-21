#pragma once

#include "common.h"

void fs_init(void);
int fs_open(const char *path, int flags);
int fs_close(int fd);
int fs_read(int fd, void *buf, uint32_t len);
int fs_write(int fd, const void *buf, uint32_t len);
int fs_unlink(const char *path);
int fs_rename(const char *old_path, const char *new_path);
int fs_listdir(char *buf, uint32_t len);
