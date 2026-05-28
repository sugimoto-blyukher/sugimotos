#include "../include/kernel/event.h"
#include "../include/kernel/kernel.h"
#include "../include/kernel/lock.h"

#define KEVENT_Q_CAP 128

static struct k_event g_q[KEVENT_Q_CAP];
static uint32_t g_head;
static uint32_t g_tail;
static uint64_t g_seq;
static struct spinlock g_lock;

void kevent_init(void)
{
    spin_lock(&g_lock);
    memset(g_q, 0, sizeof(g_q));
    g_head = 0;
    g_tail = 0;
    g_seq = 0;
    spin_unlock(&g_lock);
}

int kevent_push(uint32_t type, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    uint32_t s = spin_lock_irqsave(&g_lock);
    uint32_t next = (g_tail + 1) % KEVENT_Q_CAP;
    if (next == g_head) {
        // Drop oldest to keep forward progress under interrupt bursts.
        g_head = (g_head + 1) % KEVENT_Q_CAP;
    }
    g_q[g_tail].type = type;
    g_q[g_tail].a = a;
    g_q[g_tail].b = b;
    g_q[g_tail].c = c;
    g_q[g_tail].d = d;
    g_q[g_tail].seq = g_seq++;
    g_tail = next;
    spin_unlock_irqrestore(&g_lock, s);
    return 0;
}

int kevent_pop(struct k_event *out)
{
    if (!out)
        return -1;
    uint32_t s = spin_lock_irqsave(&g_lock);
    if (g_head == g_tail)
    {
        spin_unlock_irqrestore(&g_lock, s);
        return 0;
    }
    *out = g_q[g_head];
    g_head = (g_head + 1) % KEVENT_Q_CAP;
    spin_unlock_irqrestore(&g_lock, s);
    return 1;
}
