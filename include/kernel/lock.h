#pragma once

#include "common.h"

struct spinlock {
    volatile uint32_t v;
};

static inline void spin_lock(struct spinlock *lk)
{
    while (__sync_lock_test_and_set(&lk->v, 1)) {
        __asm__ __volatile__("nop");
    }
    __sync_synchronize();
}

static inline void spin_unlock(struct spinlock *lk)
{
    __sync_synchronize();
    __sync_lock_release(&lk->v);
}
