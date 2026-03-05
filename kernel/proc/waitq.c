#include "kernel/waitq.h"

void waitq_init(struct waitq *q)
{
    if (!q)
        return;
    q->lock.v = 0;
    for (int i = 0; i < PROC_MAX; i++)
        q->waiters[i] = NULL;
}

static int waitq_contains(struct waitq *q, struct process *p)
{
    for (int i = 0; i < PROC_MAX; i++) {
        if (q->waiters[i] == p)
            return 1;
    }
    return 0;
}

void waitq_sleep(struct waitq *q)
{
    if (!q || !current_proc || current_proc == idle_proc)
        return;

    spin_lock(&q->lock);
    if (!waitq_contains(q, current_proc)) {
        for (int i = 0; i < PROC_MAX; i++) {
            if (!q->waiters[i]) {
                q->waiters[i] = current_proc;
                break;
            }
        }
    }
    spin_unlock(&q->lock);

    current_proc->state = PROC_BLOCKED;
    yield();
    if (current_proc->state == PROC_BLOCKED)
        current_proc->state = PROC_RUNNABLE;
}

void waitq_wake_all(struct waitq *q)
{
    if (!q)
        return;

    spin_lock(&q->lock);
    for (int i = 0; i < PROC_MAX; i++) {
        struct process *p = q->waiters[i];
        if (!p)
            continue;
        if (p->state == PROC_BLOCKED)
            p->state = PROC_RUNNABLE;
        q->waiters[i] = NULL;
    }
    spin_unlock(&q->lock);
}
