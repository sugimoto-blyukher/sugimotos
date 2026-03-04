#include "kernel/virtio_input.h"

#define VIRTIO_MMIO_BASE 0x10001000
#define VIRTIO_MMIO_STEP 0x1000
#define VIRTIO_MMIO_SLOTS 8
#define VI_MMIO_MAGIC_VALUE 0x000
#define VI_MMIO_VERSION 0x004
#define VI_MMIO_DEVICE_ID 0x008
#define VI_MMIO_STATUS 0x070
#define VI_MMIO_CONFIG_SEL 0x0fc
#define VI_MMIO_CONFIG_SUBSEL 0x100
#define VI_MMIO_CONFIG_SIZE 0x104
#define VI_MMIO_CONFIG_DATA 0x108
#define VI_MMIO_DEVICE_FEATURES_SEL 0x014
#define VI_MMIO_DRIVER_FEATURES_SEL 0x024
#define VI_MMIO_DRIVER_FEATURES 0x020
#define VI_MMIO_QUEUE_SEL 0x030
#define VI_MMIO_QUEUE_NUM_MAX 0x034
#define VI_MMIO_QUEUE_NUM 0x038
#define VI_MMIO_QUEUE_READY 0x044
#define VI_MMIO_QUEUE_NOTIFY 0x050
#define VI_MMIO_QUEUE_DESC_LOW 0x080
#define VI_MMIO_QUEUE_DESC_HIGH 0x084
#define VI_MMIO_QUEUE_DRIVER_LOW 0x090
#define VI_MMIO_QUEUE_DRIVER_HIGH 0x094
#define VI_MMIO_QUEUE_DEVICE_LOW 0x0a0
#define VI_MMIO_QUEUE_DEVICE_HIGH 0x0a4

#define VI_STATUS_ACKNOWLEDGE 1
#define VI_STATUS_DRIVER 2
#define VI_STATUS_DRIVER_OK 4
#define VI_STATUS_FEATURES_OK 8

#define VI_DESC_F_WRITE 2
#define VI_QNUM 32

struct vi_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct vi_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VI_QNUM];
} __attribute__((packed));

struct vi_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct vi_used {
    uint16_t flags;
    uint16_t idx;
    struct vi_used_elem ring[VI_QNUM];
} __attribute__((packed));

static struct vi_desc vi_desc[VI_QNUM] __attribute__((aligned(16)));
static struct vi_avail vi_avail __attribute__((aligned(2)));
static volatile struct vi_used vi_used __attribute__((aligned(4)));
static struct virtio_input_event vi_events[VI_QNUM];
static uint32_t vi_base;
static uint16_t vi_last_used;
static int vi_ready;

static inline void vi_mmio_write(uint32_t off, uint32_t val)
{
    *(volatile uint32_t *) (vi_base + off) = val;
}

static inline uint32_t vi_mmio_read(uint32_t off)
{
    return *(volatile uint32_t *) (vi_base + off);
}

static void vi_queue_push(uint16_t id)
{
    vi_avail.ring[vi_avail.idx % VI_QNUM] = id;
    __sync_synchronize();
    vi_avail.idx++;
}

static int vi_char_is_letter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static char vi_to_lower(char c)
{
    if (c >= 'A' && c <= 'Z')
        return (char) (c - 'A' + 'a');
    return c;
}

static int vi_name_contains_word(const char *name, const char *word)
{
    for (int i = 0; name[i]; i++) {
        int k = i;
        int j = 0;
        while (word[j] && name[k] && vi_to_lower(name[k]) == word[j]) {
            j++;
            k++;
        }
        if (!word[j]) {
            char prev = (i > 0) ? name[i - 1] : ' ';
            char next = name[k];
            int prev_ok = !vi_char_is_letter(prev);
            int next_ok = !vi_char_is_letter(next);
            if (prev_ok && next_ok)
                return 1;
        }
    }
    return 0;
}

static int vi_read_device_name(uint32_t base, char *name, int name_sz)
{
    if (name_sz <= 0)
        return -1;
    for (int i = 0; i < name_sz; i++)
        name[i] = '\0';

    *(volatile uint32_t *) (base + VI_MMIO_CONFIG_SEL) = 0x01;    // VIRTIO_INPUT_CFG_ID_NAME
    *(volatile uint32_t *) (base + VI_MMIO_CONFIG_SUBSEL) = 0x00;
    uint32_t size = *(volatile uint32_t *) (base + VI_MMIO_CONFIG_SIZE);
    if (size == 0)
        return -1;
    int n = (int) size;
    if (n >= name_sz)
        n = name_sz - 1;
    volatile uint8_t *data = (volatile uint8_t *) (base + VI_MMIO_CONFIG_DATA);
    for (int i = 0; i < n; i++) {
        char c = (char) data[i];
        if (c == '\0')
            break;
        name[i] = c;
        name[i + 1] = '\0';
    }
    return 0;
}

int virtio_input_init(void)
{
    vi_base = 0;
    uint32_t fallback_base = 0;
    for (int i = 0; i < VIRTIO_MMIO_SLOTS; i++) {
        uint32_t base = VIRTIO_MMIO_BASE + i * VIRTIO_MMIO_STEP;
        uint32_t magic = *(volatile uint32_t *) (base + VI_MMIO_MAGIC_VALUE);
        uint32_t version = *(volatile uint32_t *) (base + VI_MMIO_VERSION);
        uint32_t dev = *(volatile uint32_t *) (base + VI_MMIO_DEVICE_ID);
        if (magic == 0x74726976 && (version == 1 || version == 2) && dev == 18) {
            if (!fallback_base)
                fallback_base = base;
            char name[64];
            if (vi_read_device_name(base, name, sizeof(name)) == 0) {
                if (vi_name_contains_word(name, "mouse") || vi_name_contains_word(name, "tablet")) {
                    vi_base = base;
                    break;
                }
            }
        }
    }
    if (!vi_base)
        vi_base = fallback_base;
    if (!vi_base)
        return -1;

    vi_mmio_write(VI_MMIO_STATUS, 0);
    vi_mmio_write(VI_MMIO_STATUS, VI_STATUS_ACKNOWLEDGE);
    vi_mmio_write(VI_MMIO_STATUS, VI_STATUS_ACKNOWLEDGE | VI_STATUS_DRIVER);
    vi_mmio_write(VI_MMIO_DEVICE_FEATURES_SEL, 0);
    vi_mmio_write(VI_MMIO_DRIVER_FEATURES_SEL, 0);
    vi_mmio_write(VI_MMIO_DRIVER_FEATURES, 0);
    vi_mmio_write(VI_MMIO_STATUS, VI_STATUS_ACKNOWLEDGE | VI_STATUS_DRIVER | VI_STATUS_FEATURES_OK);

    vi_mmio_write(VI_MMIO_QUEUE_SEL, 0);
    if (vi_mmio_read(VI_MMIO_QUEUE_NUM_MAX) < VI_QNUM)
        return -1;
    vi_mmio_write(VI_MMIO_QUEUE_NUM, VI_QNUM);

    memset(vi_desc, 0, sizeof(vi_desc));
    memset(&vi_avail, 0, sizeof(vi_avail));
    memset((void *) &vi_used, 0, sizeof(vi_used));
    memset(vi_events, 0, sizeof(vi_events));

    for (int i = 0; i < VI_QNUM; i++) {
        vi_desc[i].addr = (uint64_t) (uint32_t) &vi_events[i];
        vi_desc[i].len = sizeof(struct virtio_input_event);
        vi_desc[i].flags = VI_DESC_F_WRITE;
        vi_desc[i].next = 0;
        vi_queue_push((uint16_t) i);
    }
    __sync_synchronize();

    vi_mmio_write(VI_MMIO_QUEUE_DESC_LOW, (uint32_t) (uint64_t) vi_desc);
    vi_mmio_write(VI_MMIO_QUEUE_DESC_HIGH, 0);
    vi_mmio_write(VI_MMIO_QUEUE_DRIVER_LOW, (uint32_t) (uint64_t) &vi_avail);
    vi_mmio_write(VI_MMIO_QUEUE_DRIVER_HIGH, 0);
    vi_mmio_write(VI_MMIO_QUEUE_DEVICE_LOW, (uint32_t) (uint64_t) &vi_used);
    vi_mmio_write(VI_MMIO_QUEUE_DEVICE_HIGH, 0);
    vi_mmio_write(VI_MMIO_QUEUE_READY, 1);
    vi_mmio_write(VI_MMIO_QUEUE_NOTIFY, 0);

    vi_mmio_write(VI_MMIO_STATUS,
                  VI_STATUS_ACKNOWLEDGE | VI_STATUS_DRIVER | VI_STATUS_FEATURES_OK | VI_STATUS_DRIVER_OK);
    vi_last_used = 0;
    vi_ready = 1;
    return 0;
}

int virtio_input_next_event(struct virtio_input_event *ev)
{
    if (!vi_ready || !ev)
        return 0;
    if (vi_last_used == vi_used.idx)
        return 0;

    uint16_t slot = vi_last_used % VI_QNUM;
    uint32_t id = vi_used.ring[slot].id;
    vi_last_used++;
    if (id >= VI_QNUM)
        return 0;

    *ev = vi_events[id];
    vi_queue_push((uint16_t) id);
    __sync_synchronize();
    vi_mmio_write(VI_MMIO_QUEUE_NOTIFY, 0);
    return 1;
}
