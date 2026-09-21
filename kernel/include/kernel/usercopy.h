#pragma once

#include "common.h"

int copy_from_user(void *dst, uint32_t src, uint32_t len);
int copy_to_user(uint32_t dst, const void *src, uint32_t len);
int copy_string_from_user(char *dst, uint32_t src, uint32_t capacity);
