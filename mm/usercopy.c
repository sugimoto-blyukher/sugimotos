#include "kernel/proc.h"
#include "kernel/usercopy.h"

int copy_from_user(void *dst, uint32_t src, uint32_t len)
{
    if (len == 0)
        return 0;
    if (!dst || !src || !proc_user_readable_ok(src, len))
        return -1;
    memcpy(dst, (const void *) src, len);
    return 0;
}

int copy_to_user(uint32_t dst, const void *src, uint32_t len)
{
    if (len == 0)
        return 0;
    if (!dst || !src || !proc_user_writable_ok(dst, len))
        return -1;
    memcpy((void *) dst, src, len);
    return 0;
}

int copy_string_from_user(char *dst, uint32_t src, uint32_t capacity)
{
    if (!dst || !src || capacity == 0)
        return -1;
    for (uint32_t i = 0; i < capacity; i++) {
        if (src + i < src || !proc_user_readable_ok(src + i, 1))
            return -1;
        dst[i] = *(const char *) (src + i);
        if (dst[i] == '\0')
            return 0;
    }
    return -1;
}
