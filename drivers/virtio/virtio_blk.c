#include "kernel/blk.h"
#include "kernel/lock.h"
#include "kernel/proc.h"
#include "kernel/waitq.h"

#define VIRTIO_MMIO_BASE 0x10001000
#define VIRTIO_MMIO_STEP 0x1000
#define VIRTIO_MMIO_SLOTS 8

#define MMIO_MAGIC_VALUE 0x000
#define MMIO_VERSION 0x004
#define MMIO_DEVICE_ID 0x008
#define MMIO_VENDOR_ID 0x00c
#define MMIO_DEVICE_FEATURES 0x010
#define MMIO_DEVICE_FEATURES_SEL 0x014
#define MMIO_DRIVER_FEATURES 0x020
#define MMIO_DRIVER_FEATURES_SEL 0x024
#define MMIO_QUEUE_SEL 0x030
#define MMIO_QUEUE_NUM_MAX 0x034
#define MMIO_QUEUE_NUM 0x038
#define MMIO_QUEUE_READY 0x044
#define MMIO_QUEUE_NOTIFY 0x050
#define MMIO_INTERRUPT_STATUS 0x060
#define MMIO_INTERRUPT_ACK 0x064
#define MMIO_STATUS 0x070
#define MMIO_QUEUE_DESC_LOW 0x080
#define MMIO_QUEUE_DESC_HIGH 0x084
#define MMIO_QUEUE_DRIVER_LOW 0x090
#define MMIO_QUEUE_DRIVER_HIGH 0x094
#define MMIO_QUEUE_DEVICE_LOW 0x0a0
#define MMIO_QUEUE_DEVICE_HIGH 0x0a4

#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER 2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_STATUS_FAILED 128

#define VIRTQ_DESC_F_NEXT 1
#define VIRTQ_DESC_F_WRITE 2

#define VIRTIO_BLK_T_IN 0
#define VIRTQ_NUM 32
#define BLK_REQ_MAX 8

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VIRTQ_NUM];
} __attribute__((packed));

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[VIRTQ_NUM];
} __attribute__((packed));

struct virtio_blk_req {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __attribute__((packed));

static struct virtq_desc desc[VIRTQ_NUM] __attribute__((aligned(16)));
static struct virtq_avail avail __attribute__((aligned(2)));
static volatile struct virtq_used used __attribute__((aligned(4)));

struct blk_slot {
    struct virtio_blk_req req;
    volatile uint8_t status;
    uint8_t in_use;
    uint8_t done;
};

static struct blk_slot blk_slots[BLK_REQ_MAX];
static uint16_t used_idx;
static bool blk_ready;
static uint32_t virtio_base;
static struct waitq blk_waitq;
static struct spinlock blk_lock;

static inline void mmio_write(uint32_t off, uint32_t val)
{
    *(volatile uint32_t *) (virtio_base + off) = val;
}

static inline uint32_t mmio_read(uint32_t off)
{
    return *(volatile uint32_t *) (virtio_base + off);
}

static uint16_t blk_head_for_slot(int slot_idx)
{
    return (uint16_t) (slot_idx * 3);
}

static void blk_process_used_locked(void)
{
    __sync_synchronize();
    while (used_idx != used.idx) {
        uint16_t ring_idx = used_idx % VIRTQ_NUM;
        uint32_t head = used.ring[ring_idx].id;
        used_idx++;
        if ((head % 3u) != 0)
            continue;
        uint32_t slot_idx = head / 3u;
        if (slot_idx >= BLK_REQ_MAX)
            continue;
        if (!blk_slots[slot_idx].in_use)
            continue;
        blk_slots[slot_idx].done = 1;
    }
}

int blk_init(void)
{
    virtio_base = 0;
    for (int i = 0; i < VIRTIO_MMIO_SLOTS; i++) {
        uint32_t base = VIRTIO_MMIO_BASE + i * VIRTIO_MMIO_STEP;
        uint32_t magic = *(volatile uint32_t *) (base + MMIO_MAGIC_VALUE);
        uint32_t version = *(volatile uint32_t *) (base + MMIO_VERSION);
        uint32_t device = *(volatile uint32_t *) (base + MMIO_DEVICE_ID);
        if (magic == 0x74726976 && (version == 1 || version == 2) && device == 2) {
            virtio_base = base;
            printf("virtio-blk: found device at %x (irq %d)\n", base, i + 1);
            break;
        }
    }
    if (!virtio_base) {
        printf("virtio-blk: device not found\n");
        return -1;
    }

    mmio_write(MMIO_STATUS, 0);
    mmio_write(MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
    mmio_write(MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    mmio_write(MMIO_DRIVER_FEATURES_SEL, 0);
    mmio_write(MMIO_DRIVER_FEATURES, 0);
    mmio_write(MMIO_DRIVER_FEATURES_SEL, 1);
    mmio_write(MMIO_DRIVER_FEATURES, 0);

    mmio_write(MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                                VIRTIO_STATUS_FEATURES_OK);

    if (!(mmio_read(MMIO_STATUS) & VIRTIO_STATUS_FEATURES_OK)) {
        printf("virtio-blk: features not ok\n");
        return -1;
    }

    mmio_write(MMIO_QUEUE_SEL, 0);
    if (mmio_read(MMIO_QUEUE_NUM_MAX) < VIRTQ_NUM)
        return -1;
    if (VIRTQ_NUM < (BLK_REQ_MAX * 3))
        return -1;
    mmio_write(MMIO_QUEUE_NUM, VIRTQ_NUM);

    memset(desc, 0, sizeof(desc));
    memset(&avail, 0, sizeof(avail));
    memset((void *) &used, 0, sizeof(used));

    mmio_write(MMIO_QUEUE_DESC_LOW, (uint32_t) (uint64_t) desc);
    mmio_write(MMIO_QUEUE_DESC_HIGH, 0);
    mmio_write(MMIO_QUEUE_DRIVER_LOW, (uint32_t) (uint64_t) &avail);
    mmio_write(MMIO_QUEUE_DRIVER_HIGH, 0);
    mmio_write(MMIO_QUEUE_DEVICE_LOW, (uint32_t) (uint64_t) &used);
    mmio_write(MMIO_QUEUE_DEVICE_HIGH, 0);
    mmio_write(MMIO_QUEUE_READY, 1);

    mmio_write(MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                                VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);

    used_idx = 0;
    blk_ready = true;
    memset(blk_slots, 0, sizeof(blk_slots));
    waitq_init(&blk_waitq);
    printf("virtio-blk: init ok\n");
    return 0;
}

int blk_read(uint32_t sector, void *buf)
{
    if (!blk_ready || !buf)
        return -1;

    int slot_idx = -1;
    while (slot_idx < 0) {
        uint32_t s = spin_lock_irqsave(&blk_lock);
        blk_process_used_locked();
        for (int i = 0; i < BLK_REQ_MAX; i++) {
            if (!blk_slots[i].in_use) {
                slot_idx = i;
                blk_slots[i].in_use = 1;
                blk_slots[i].done = 0;
                blk_slots[i].status = 0xff;
                break;
            }
        }
        spin_unlock_irqrestore(&blk_lock, s);
        if (slot_idx >= 0)
            break;
        if (current_proc)
            waitq_sleep(&blk_waitq);
        else
            __asm__ __volatile__("wfi");
    }

    struct blk_slot *slot = &blk_slots[slot_idx];
    slot->req.type = VIRTIO_BLK_T_IN;
    slot->req.reserved = 0;
    slot->req.sector = sector;

    uint16_t head = blk_head_for_slot(slot_idx);
    uint16_t d0 = head;
    uint16_t d1 = (uint16_t) (head + 1);
    uint16_t d2 = (uint16_t) (head + 2);

    uint32_t s = spin_lock_irqsave(&blk_lock);
    desc[d0].addr = (uint64_t) (uint32_t) &slot->req;
    desc[d0].len = sizeof(slot->req);
    desc[d0].flags = VIRTQ_DESC_F_NEXT;
    desc[d0].next = d1;

    desc[d1].addr = (uint64_t) (uint32_t) buf;
    desc[d1].len = 512;
    desc[d1].flags = VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE;
    desc[d1].next = d2;

    desc[d2].addr = (uint64_t) (uint32_t) &slot->status;
    desc[d2].len = 1;
    desc[d2].flags = VIRTQ_DESC_F_WRITE;
    desc[d2].next = 0;

    avail.ring[avail.idx % VIRTQ_NUM] = head;
    __sync_synchronize();
    avail.idx++;
    __sync_synchronize();
    mmio_write(MMIO_QUEUE_NOTIFY, 0);
    spin_unlock_irqrestore(&blk_lock, s);

    while (1) {
        s = spin_lock_irqsave(&blk_lock);
        blk_process_used_locked();
        if (slot->done) {
            int ok = (slot->status == 0);
            slot->done = 0;
            slot->in_use = 0;
            spin_unlock_irqrestore(&blk_lock, s);
            waitq_wake_all(&blk_waitq);
            return ok ? 0 : -1;
        }
        spin_unlock_irqrestore(&blk_lock, s);
        if (current_proc)
            waitq_sleep(&blk_waitq);
        else
            __asm__ __volatile__("wfi");
    }
}

void blk_handle_irq(void)
{
    if (!virtio_base)
        return;
    uint32_t st = mmio_read(MMIO_INTERRUPT_STATUS);
    if (!st)
        return;
    mmio_write(MMIO_INTERRUPT_ACK, st);
    uint32_t s = spin_lock_irqsave(&blk_lock);
    blk_process_used_locked();
    spin_unlock_irqrestore(&blk_lock, s);
    waitq_wake_all(&blk_waitq);
}
