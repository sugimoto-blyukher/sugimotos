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

static inline uint32_t spin_lock_irqsave(struct spinlock *lk)
{
    uint32_t s;
    __asm__ __volatile__("csrr %0, sstatus" : "=r"(s));
    __asm__ __volatile__("csrci sstatus, 2"); // Clear SIE
    spin_lock(lk);
    return s;
}

static inline void spin_unlock_irqrestore(struct spinlock *lk, uint32_t s)
{
    spin_unlock(lk);
    __asm__ __volatile__("csrw sstatus, %0" :: "r"(s));
}
