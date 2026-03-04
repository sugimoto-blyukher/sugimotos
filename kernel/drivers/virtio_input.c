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
#define VI_MAX_DEVICES 4

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

struct vi_candidate {
    uint32_t base;
    int score;
};

static struct vi_desc vi_desc[VI_MAX_DEVICES][VI_QNUM] __attribute__((aligned(16)));
static struct vi_avail vi_avail[VI_MAX_DEVICES] __attribute__((aligned(2)));
static volatile struct vi_used vi_used[VI_MAX_DEVICES] __attribute__((aligned(4)));
static struct virtio_input_event vi_events[VI_MAX_DEVICES][VI_QNUM];

static uint32_t vi_bases[VI_MAX_DEVICES];
static uint16_t vi_last_used[VI_MAX_DEVICES];
static int vi_dev_count;
static int vi_rr_next;

static inline void vi_mmio_write(uint32_t base, uint32_t off, uint32_t val)
{
    *(volatile uint32_t *) (base + off) = val;
}

static inline uint32_t vi_mmio_read(uint32_t base, uint32_t off)
{
    return *(volatile uint32_t *) (base + off);
}

static void vi_queue_push(int dev, uint16_t id)
{
    vi_avail[dev].ring[vi_avail[dev].idx % VI_QNUM] = id;
    __sync_synchronize();
    vi_avail[dev].idx++;
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

    vi_mmio_write(base, VI_MMIO_CONFIG_SEL, 0x01); // VIRTIO_INPUT_CFG_ID_NAME
    vi_mmio_write(base, VI_MMIO_CONFIG_SUBSEL, 0x00);
    uint32_t size = vi_mmio_read(base, VI_MMIO_CONFIG_SIZE);
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

static int vi_init_one_device(int dev, uint32_t base)
{
    vi_mmio_write(base, VI_MMIO_STATUS, 0);
    vi_mmio_write(base, VI_MMIO_STATUS, VI_STATUS_ACKNOWLEDGE);
    vi_mmio_write(base, VI_MMIO_STATUS, VI_STATUS_ACKNOWLEDGE | VI_STATUS_DRIVER);
    vi_mmio_write(base, VI_MMIO_DEVICE_FEATURES_SEL, 0);
    vi_mmio_write(base, VI_MMIO_DRIVER_FEATURES_SEL, 0);
    vi_mmio_write(base, VI_MMIO_DRIVER_FEATURES, 0);
    vi_mmio_write(base, VI_MMIO_STATUS, VI_STATUS_ACKNOWLEDGE | VI_STATUS_DRIVER | VI_STATUS_FEATURES_OK);

    vi_mmio_write(base, VI_MMIO_QUEUE_SEL, 0);
    if (vi_mmio_read(base, VI_MMIO_QUEUE_NUM_MAX) < VI_QNUM)
        return -1;
    vi_mmio_write(base, VI_MMIO_QUEUE_NUM, VI_QNUM);

    memset(vi_desc[dev], 0, sizeof(vi_desc[dev]));
    memset(&vi_avail[dev], 0, sizeof(vi_avail[dev]));
    memset((void *) &vi_used[dev], 0, sizeof(vi_used[dev]));
    memset(vi_events[dev], 0, sizeof(vi_events[dev]));

    for (int i = 0; i < VI_QNUM; i++) {
        vi_desc[dev][i].addr = (uint64_t) (uint32_t) &vi_events[dev][i];
        vi_desc[dev][i].len = sizeof(struct virtio_input_event);
        vi_desc[dev][i].flags = VI_DESC_F_WRITE;
        vi_desc[dev][i].next = 0;
        vi_queue_push(dev, (uint16_t) i);
    }
    __sync_synchronize();

    vi_mmio_write(base, VI_MMIO_QUEUE_DESC_LOW, (uint32_t) (uint64_t) vi_desc[dev]);
    vi_mmio_write(base, VI_MMIO_QUEUE_DESC_HIGH, 0);
    vi_mmio_write(base, VI_MMIO_QUEUE_DRIVER_LOW, (uint32_t) (uint64_t) &vi_avail[dev]);
    vi_mmio_write(base, VI_MMIO_QUEUE_DRIVER_HIGH, 0);
    vi_mmio_write(base, VI_MMIO_QUEUE_DEVICE_LOW, (uint32_t) (uint64_t) &vi_used[dev]);
    vi_mmio_write(base, VI_MMIO_QUEUE_DEVICE_HIGH, 0);
    vi_mmio_write(base, VI_MMIO_QUEUE_READY, 1);
    vi_mmio_write(base, VI_MMIO_QUEUE_NOTIFY, 0);

    vi_mmio_write(base,
                  VI_MMIO_STATUS,
                  VI_STATUS_ACKNOWLEDGE | VI_STATUS_DRIVER | VI_STATUS_FEATURES_OK | VI_STATUS_DRIVER_OK);
    vi_last_used[dev] = 0;
    vi_bases[dev] = base;
    return 0;
}

int virtio_input_init(void)
{
    struct vi_candidate cand[VI_MAX_DEVICES];
    int cand_count = 0;

    memset(vi_bases, 0, sizeof(vi_bases));
    memset(vi_last_used, 0, sizeof(vi_last_used));
    vi_dev_count = 0;
    vi_rr_next = 0;

    for (int i = 0; i < VIRTIO_MMIO_SLOTS; i++) {
        uint32_t base = VIRTIO_MMIO_BASE + i * VIRTIO_MMIO_STEP;
        uint32_t magic = vi_mmio_read(base, VI_MMIO_MAGIC_VALUE);
        uint32_t version = vi_mmio_read(base, VI_MMIO_VERSION);
        uint32_t dev = vi_mmio_read(base, VI_MMIO_DEVICE_ID);
        if (!(magic == 0x74726976 && (version == 1 || version == 2) && dev == 18))
            continue;

        int score = 2;
        char name[64];
        if (vi_read_device_name(base, name, sizeof(name)) == 0) {
            if (vi_name_contains_word(name, "mouse") || vi_name_contains_word(name, "tablet"))
                score = 0;
            else if (vi_name_contains_word(name, "keyboard"))
                score = 1;
        }

        if (cand_count < VI_MAX_DEVICES) {
            cand[cand_count].base = base;
            cand[cand_count].score = score;
            cand_count++;
        }
    }

    for (int i = 0; i < cand_count; i++) {
        int best = i;
        for (int j = i + 1; j < cand_count; j++) {
            if (cand[j].score < cand[best].score)
                best = j;
        }
        if (best != i) {
            struct vi_candidate tmp = cand[i];
            cand[i] = cand[best];
            cand[best] = tmp;
        }
    }

    for (int i = 0; i < cand_count && vi_dev_count < VI_MAX_DEVICES; i++) {
        if (vi_init_one_device(vi_dev_count, cand[i].base) == 0)
            vi_dev_count++;
    }

    return (vi_dev_count > 0) ? 0 : -1;
}

int virtio_input_next_event(struct virtio_input_event *ev)
{
    if (!ev || vi_dev_count <= 0)
        return 0;

    for (int n = 0; n < vi_dev_count; n++) {
        int dev = (vi_rr_next + n) % vi_dev_count;
        if (vi_last_used[dev] == vi_used[dev].idx)
            continue;

        uint16_t slot = vi_last_used[dev] % VI_QNUM;
        uint32_t id = vi_used[dev].ring[slot].id;
        vi_last_used[dev]++;
        if (id >= VI_QNUM)
            continue;

        *ev = vi_events[dev][id];
        vi_queue_push(dev, (uint16_t) id);
        __sync_synchronize();
        vi_mmio_write(vi_bases[dev], VI_MMIO_QUEUE_NOTIFY, 0);
        vi_rr_next = (dev + 1) % vi_dev_count;
        return 1;
    }

    return 0;
}
