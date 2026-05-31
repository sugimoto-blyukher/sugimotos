#pragma once

#include "kernel/lock.h"
#include "kernel/proc.h"

struct waitq {
    struct process *waiters[PROC_MAX];
    struct spinlock lock;
};

void waitq_init(struct waitq *q);
void waitq_sleep(struct waitq *q);
void waitq_wake_all(struct waitq *q);
