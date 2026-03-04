#include "kernel/blk.h"
#include "kernel/kernel.h"

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

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[8];
} __attribute__((packed));

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[8];
} __attribute__((packed));

struct virtio_blk_req {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __attribute__((packed));

static struct virtq_desc desc[8] __attribute__((aligned(16)));
static struct virtq_avail avail __attribute__((aligned(2)));
static volatile struct virtq_used used __attribute__((aligned(4)));

static struct virtio_blk_req req;
static volatile uint8_t req_status;
static uint16_t used_idx;
static bool blk_ready;
static uint32_t virtio_base;

static inline void mmio_write(uint32_t off, uint32_t val)
{
    *(volatile uint32_t *) (virtio_base + off) = val;
}

static inline uint32_t mmio_read(uint32_t off)
{
    return *(volatile uint32_t *) (virtio_base + off);
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
            break;
        }
    }
    if (!virtio_base)
        return -1;

    mmio_write(MMIO_STATUS, 0);
    mmio_write(MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
    mmio_write(MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    mmio_write(MMIO_DRIVER_FEATURES_SEL, 0);
    mmio_write(MMIO_DRIVER_FEATURES, 0);
    mmio_write(MMIO_DRIVER_FEATURES_SEL, 1);
    mmio_write(MMIO_DRIVER_FEATURES, 0);

    mmio_write(MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                                VIRTIO_STATUS_FEATURES_OK);

    if (!(mmio_read(MMIO_STATUS) & VIRTIO_STATUS_FEATURES_OK))
        return -1;

    mmio_write(MMIO_QUEUE_SEL, 0);
    if (mmio_read(MMIO_QUEUE_NUM_MAX) < 8)
        return -1;
    mmio_write(MMIO_QUEUE_NUM, 8);

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
    return 0;
}

int blk_read(uint32_t sector, void *buf)
{
    if (!blk_ready || !buf)
        return -1;

    req.type = VIRTIO_BLK_T_IN;
    req.reserved = 0;
    req.sector = sector;
    req_status = 0xff;

    desc[0].addr = (uint64_t) (uint32_t) &req;
    desc[0].len = sizeof(req);
    desc[0].flags = VIRTQ_DESC_F_NEXT;
    desc[0].next = 1;

    desc[1].addr = (uint64_t) (uint32_t) buf;
    desc[1].len = 512;
    desc[1].flags = VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE;
    desc[1].next = 2;

    desc[2].addr = (uint64_t) (uint32_t) &req_status;
    desc[2].len = 1;
    desc[2].flags = VIRTQ_DESC_F_WRITE;
    desc[2].next = 0;

    avail.ring[avail.idx % 8] = 0;
    __sync_synchronize();
    avail.idx++;
    __sync_synchronize();

    mmio_write(MMIO_QUEUE_NOTIFY, 0);

    uint32_t spin = 0;
    while (used_idx == used.idx) {
        spin++;
        if (spin > 100000000)
            return -1;
    }
    used_idx = used.idx;

    mmio_write(MMIO_INTERRUPT_ACK, mmio_read(MMIO_INTERRUPT_STATUS));

    return (req_status == 0) ? 0 : -1;
}
