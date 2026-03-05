#include "kernel/virtio_gpu.h"
#include "kernel/event.h"
#include "kernel/kernel.h"
#include "kernel/waitq.h"

#define VIRTIO_MMIO_BASE 0x10001000
#define VIRTIO_MMIO_STEP 0x1000
#define VIRTIO_MMIO_SLOTS 32

#define VG_MMIO_MAGIC_VALUE 0x000
#define VG_MMIO_VERSION 0x004
#define VG_MMIO_DEVICE_ID 0x008
#define VG_MMIO_STATUS 0x070
#define VG_MMIO_DEVICE_FEATURES 0x010
#define VG_MMIO_DEVICE_FEATURES_SEL 0x014
#define VG_MMIO_DRIVER_FEATURES_SEL 0x024
#define VG_MMIO_DRIVER_FEATURES 0x020
#define VG_MMIO_QUEUE_SEL 0x030
#define VG_MMIO_QUEUE_NUM_MAX 0x034
#define VG_MMIO_QUEUE_NUM 0x038
#define VG_MMIO_QUEUE_READY 0x044
#define VG_MMIO_QUEUE_NOTIFY 0x050
#define VG_MMIO_QUEUE_DESC_LOW 0x080
#define VG_MMIO_QUEUE_DESC_HIGH 0x084
#define VG_MMIO_QUEUE_DRIVER_LOW 0x090
#define VG_MMIO_QUEUE_DRIVER_HIGH 0x094
#define VG_MMIO_QUEUE_DEVICE_LOW 0x0a0
#define VG_MMIO_QUEUE_DEVICE_HIGH 0x0a4
#define VG_MMIO_INTERRUPT_STATUS 0x060
#define VG_MMIO_INTERRUPT_ACK 0x064

#define VG_STATUS_ACKNOWLEDGE 1
#define VG_STATUS_DRIVER 2
#define VG_STATUS_DRIVER_OK 4
#define VG_STATUS_FEATURES_OK 8
#define VG_FEATURE_VERSION_1 32

#define VG_DESC_F_NEXT 1
#define VG_DESC_F_WRITE 2

#define VG_QNUM 32
#define VG_FB_MAX_W 1024
#define VG_FB_MAX_H 768

#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO 0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D 0x0101
#define VIRTIO_GPU_CMD_RESOURCE_UNREF 0x0102
#define VIRTIO_GPU_CMD_SET_SCANOUT 0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH 0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D 0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106

#define VIRTIO_GPU_RESP_OK_NODATA 0x1100
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO 0x1101

#define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM 1

struct vg_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct vg_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VG_QNUM];
} __attribute__((packed));

struct vg_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct vg_used {
    uint16_t flags;
    uint16_t idx;
    struct vg_used_elem ring[VG_QNUM];
} __attribute__((packed));

struct virtio_gpu_ctrl_hdr {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

struct virtio_gpu_display_one {
    struct virtio_gpu_rect rect;
    uint32_t enabled;
    uint32_t flags;
} __attribute__((packed));

struct virtio_gpu_resp_display_info {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_display_one pmodes[16];
} __attribute__((packed));

struct virtio_gpu_resource_create_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

struct virtio_gpu_mem_entry {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_resource_attach_backing {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
    struct virtio_gpu_mem_entry entry;
} __attribute__((packed));

struct virtio_gpu_set_scanout {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect rect;
    uint32_t scanout_id;
    uint32_t resource_id;
} __attribute__((packed));

struct virtio_gpu_transfer_to_host_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect rect;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_resource_flush {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect rect;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_resource_unref {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

static struct vg_desc vg_desc[VG_QNUM] __attribute__((aligned(16)));
static struct vg_avail vg_avail __attribute__((aligned(2)));
static volatile struct vg_used vg_used __attribute__((aligned(4)));

static uint32_t vg_base;
static int vg_ready;
static uint16_t vg_last_used;
static int vg_last_error;

static uint32_t vg_width_px;
static uint32_t vg_height_px;
static uint32_t vg_pitch_px;
static uint32_t vg_resource_id;
static uint32_t *vg_backbuffer;
static uint32_t vg_backbuffer_pages;
static struct waitq vg_waitq;

static inline void vg_mmio_write(uint32_t off, uint32_t val)
{
    *(volatile uint32_t *) (vg_base + off) = val;
}

static inline uint32_t vg_mmio_read(uint32_t off)
{
    return *(volatile uint32_t *) (vg_base + off);
}

static int vg_submit(const void *req, uint32_t req_len, void *resp, uint32_t resp_len)
{
    if (!vg_ready || !req || req_len == 0 || !resp || resp_len == 0)
        return -1;

    vg_desc[0].addr = (uint64_t) (uint32_t) req;
    vg_desc[0].len = req_len;
    vg_desc[0].flags = VG_DESC_F_NEXT;
    vg_desc[0].next = 1;

    vg_desc[1].addr = (uint64_t) (uint32_t) resp;
    vg_desc[1].len = resp_len;
    vg_desc[1].flags = VG_DESC_F_WRITE;
    vg_desc[1].next = 0;

    uint16_t head = 0;
    vg_avail.ring[vg_avail.idx % VG_QNUM] = head;
    __sync_synchronize();
    vg_avail.idx++;
    __sync_synchronize();
    vg_mmio_write(VG_MMIO_QUEUE_NOTIFY, 0);

    while (vg_last_used == vg_used.idx) {
        if (current_proc)
            waitq_sleep(&vg_waitq);
        else
            __asm__ __volatile__("wfi");
    }
    uint16_t slot = vg_last_used % VG_QNUM;
    if (vg_used.ring[slot].id != head)
        return -1;
    vg_last_used++;
    return 0;
}

static int vg_cmd_get_display_info(struct virtio_gpu_resp_display_info *out)
{
    struct virtio_gpu_ctrl_hdr req;
    memset(&req, 0, sizeof(req));
    req.type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;
    if (vg_submit(&req, sizeof(req), out, sizeof(*out)) < 0)
        return -1;
    if (out->hdr.type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO)
        return -1;
    return 0;
}

static int vg_cmd_resource_create_2d(uint32_t resource_id, uint32_t width, uint32_t height)
{
    struct virtio_gpu_resource_create_2d req;
    struct virtio_gpu_ctrl_hdr resp;
    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));
    req.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    req.resource_id = resource_id;
    req.format = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
    req.width = width;
    req.height = height;
    if (vg_submit(&req, sizeof(req), &resp, sizeof(resp)) < 0)
        return -1;
    return (resp.type == VIRTIO_GPU_RESP_OK_NODATA) ? 0 : -1;
}

static int vg_cmd_resource_attach_backing(uint32_t resource_id, void *addr, uint32_t len)
{
    struct virtio_gpu_resource_attach_backing req;
    struct virtio_gpu_ctrl_hdr resp;
    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));
    req.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    req.resource_id = resource_id;
    req.nr_entries = 1;
    req.entry.addr = (uint64_t) (uint32_t) addr;
    req.entry.length = len;
    if (vg_submit(&req, sizeof(req), &resp, sizeof(resp)) < 0)
        return -1;
    return (resp.type == VIRTIO_GPU_RESP_OK_NODATA) ? 0 : -1;
}

static int vg_cmd_set_scanout(uint32_t resource_id, uint32_t width, uint32_t height)
{
    struct virtio_gpu_set_scanout req;
    struct virtio_gpu_ctrl_hdr resp;
    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));
    req.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    req.rect.x = 0;
    req.rect.y = 0;
    req.rect.width = width;
    req.rect.height = height;
    req.scanout_id = 0;
    req.resource_id = resource_id;
    if (vg_submit(&req, sizeof(req), &resp, sizeof(resp)) < 0)
        return -1;
    return (resp.type == VIRTIO_GPU_RESP_OK_NODATA) ? 0 : -1;
}

static int vg_cmd_transfer_to_host_2d(uint32_t resource_id, uint32_t width, uint32_t height)
{
    struct virtio_gpu_transfer_to_host_2d req;
    struct virtio_gpu_ctrl_hdr resp;
    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));
    req.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    req.rect.x = 0;
    req.rect.y = 0;
    req.rect.width = width;
    req.rect.height = height;
    req.offset = 0;
    req.resource_id = resource_id;
    if (vg_submit(&req, sizeof(req), &resp, sizeof(resp)) < 0)
        return -1;
    return (resp.type == VIRTIO_GPU_RESP_OK_NODATA) ? 0 : -1;
}

static int vg_cmd_resource_flush(uint32_t resource_id, uint32_t width, uint32_t height)
{
    struct virtio_gpu_resource_flush req;
    struct virtio_gpu_ctrl_hdr resp;
    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));
    req.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    req.rect.x = 0;
    req.rect.y = 0;
    req.rect.width = width;
    req.rect.height = height;
    req.resource_id = resource_id;
    if (vg_submit(&req, sizeof(req), &resp, sizeof(resp)) < 0)
        return -1;
    return (resp.type == VIRTIO_GPU_RESP_OK_NODATA) ? 0 : -1;
}

static void vg_cmd_resource_unref(uint32_t resource_id)
{
    struct virtio_gpu_resource_unref req;
    struct virtio_gpu_ctrl_hdr resp;
    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));
    req.hdr.type = VIRTIO_GPU_CMD_RESOURCE_UNREF;
    req.resource_id = resource_id;
    (void) vg_submit(&req, sizeof(req), &resp, sizeof(resp));
}

static void vg_release_backbuffer(void)
{
    if (!vg_backbuffer || vg_backbuffer_pages == 0)
        return;
    free_pages((paddr_t) (uint32_t) vg_backbuffer, vg_backbuffer_pages);
    vg_backbuffer = NULL;
    vg_backbuffer_pages = 0;
}

int virtio_gpu_init(void)
{
    if (vg_ready)
        return 0;

    vg_last_error = 0;
    vg_resource_id = 0;
    vg_release_backbuffer();
    vg_base = 0;
    for (int i = 0; i < VIRTIO_MMIO_SLOTS; i++) {
        uint32_t base = VIRTIO_MMIO_BASE + i * VIRTIO_MMIO_STEP;
        uint32_t magic = *(volatile uint32_t *) (base + VG_MMIO_MAGIC_VALUE);
        uint32_t version = *(volatile uint32_t *) (base + VG_MMIO_VERSION);
        uint32_t dev = *(volatile uint32_t *) (base + VG_MMIO_DEVICE_ID);
        if (magic == 0x74726976 && (version == 1 || version == 2) && dev == 16) {
            vg_base = base;
            break;
        }
    }
    if (!vg_base) {
        vg_last_error = 1;
        return -1;
    }

    vg_mmio_write(VG_MMIO_STATUS, 0);
    vg_mmio_write(VG_MMIO_STATUS, VG_STATUS_ACKNOWLEDGE);
    vg_mmio_write(VG_MMIO_STATUS, VG_STATUS_ACKNOWLEDGE | VG_STATUS_DRIVER);

    uint64_t dev_features = 0;
    vg_mmio_write(VG_MMIO_DEVICE_FEATURES_SEL, 0);
    dev_features |= (uint64_t) vg_mmio_read(VG_MMIO_DEVICE_FEATURES);
    vg_mmio_write(VG_MMIO_DEVICE_FEATURES_SEL, 1);
    dev_features |= ((uint64_t) vg_mmio_read(VG_MMIO_DEVICE_FEATURES)) << 32;

    uint64_t drv_features = 0;
    if (dev_features & (1ull << VG_FEATURE_VERSION_1))
        drv_features |= (1ull << VG_FEATURE_VERSION_1);
    else {
        // We run with virtio-mmio.force-legacy=false and require virtio 1.0 mode.
        vg_last_error = 7;
        return -1;
    }

    vg_mmio_write(VG_MMIO_DRIVER_FEATURES_SEL, 0);
    vg_mmio_write(VG_MMIO_DRIVER_FEATURES, (uint32_t) drv_features);
    vg_mmio_write(VG_MMIO_DRIVER_FEATURES_SEL, 1);
    vg_mmio_write(VG_MMIO_DRIVER_FEATURES, (uint32_t) (drv_features >> 32));

    vg_mmio_write(VG_MMIO_STATUS, VG_STATUS_ACKNOWLEDGE | VG_STATUS_DRIVER | VG_STATUS_FEATURES_OK);
    uint32_t status = vg_mmio_read(VG_MMIO_STATUS);
    if ((status & VG_STATUS_FEATURES_OK) == 0) {
        vg_last_error = 8;
        return -1;
    }

    vg_mmio_write(VG_MMIO_QUEUE_SEL, 0);
    if (vg_mmio_read(VG_MMIO_QUEUE_NUM_MAX) < VG_QNUM) {
        vg_last_error = 2;
        return -1;
    }
    vg_mmio_write(VG_MMIO_QUEUE_NUM, VG_QNUM);

    memset(vg_desc, 0, sizeof(vg_desc));
    memset(&vg_avail, 0, sizeof(vg_avail));
    memset((void *) &vg_used, 0, sizeof(vg_used));
    vg_last_used = 0;

    vg_mmio_write(VG_MMIO_QUEUE_DESC_LOW, (uint32_t) (uint64_t) vg_desc);
    vg_mmio_write(VG_MMIO_QUEUE_DESC_HIGH, 0);
    vg_mmio_write(VG_MMIO_QUEUE_DRIVER_LOW, (uint32_t) (uint64_t) &vg_avail);
    vg_mmio_write(VG_MMIO_QUEUE_DRIVER_HIGH, 0);
    vg_mmio_write(VG_MMIO_QUEUE_DEVICE_LOW, (uint32_t) (uint64_t) &vg_used);
    vg_mmio_write(VG_MMIO_QUEUE_DEVICE_HIGH, 0);
    vg_mmio_write(VG_MMIO_QUEUE_READY, 1);

    vg_mmio_write(VG_MMIO_STATUS,
                  VG_STATUS_ACKNOWLEDGE | VG_STATUS_DRIVER | VG_STATUS_FEATURES_OK | VG_STATUS_DRIVER_OK);
    vg_ready = 1;
    waitq_init(&vg_waitq);

    struct virtio_gpu_resp_display_info dinfo;
    memset(&dinfo, 0, sizeof(dinfo));
    if (vg_cmd_get_display_info(&dinfo) < 0) {
        vg_ready = 0;
        vg_last_error = 3;
        return -1;
    }

    uint32_t w = 0;
    uint32_t h = 0;
    for (int i = 0; i < 16; i++) {
        if (dinfo.pmodes[i].enabled && dinfo.pmodes[i].rect.width > 0 && dinfo.pmodes[i].rect.height > 0) {
            w = dinfo.pmodes[i].rect.width;
            h = dinfo.pmodes[i].rect.height;
            break;
        }
    }
    if (w == 0 || h == 0) {
        w = 640;
        h = 480;
    }
    if (w > VG_FB_MAX_W)
        w = VG_FB_MAX_W;
    if (h > VG_FB_MAX_H)
        h = VG_FB_MAX_H;

    vg_width_px = w;
    vg_height_px = h;
    vg_pitch_px = w * 4;
    vg_resource_id = 1;

    uint32_t bytes = vg_pitch_px * vg_height_px;
    vg_backbuffer_pages = (bytes + PAGE_SIZE - 1u) / PAGE_SIZE;
    paddr_t bb_pa = alloc_pages_try(vg_backbuffer_pages);
    if (!bb_pa) {
        vg_ready = 0;
        vg_last_error = 9;
        return -1;
    }
    vg_backbuffer = (uint32_t *) bb_pa;

    if (vg_cmd_resource_create_2d(vg_resource_id, vg_width_px, vg_height_px) < 0) {
        vg_release_backbuffer();
        vg_ready = 0;
        vg_last_error = 4;
        return -1;
    }
    if (vg_cmd_resource_attach_backing(vg_resource_id, vg_backbuffer, bytes) < 0) {
        vg_cmd_resource_unref(vg_resource_id);
        vg_release_backbuffer();
        vg_ready = 0;
        vg_last_error = 5;
        return -1;
    }
    if (vg_cmd_set_scanout(vg_resource_id, vg_width_px, vg_height_px) < 0) {
        vg_cmd_resource_unref(vg_resource_id);
        vg_release_backbuffer();
        vg_ready = 0;
        vg_last_error = 6;
        return -1;
    }

    memset(vg_backbuffer, 0, bytes);
    virtio_gpu_present();
    vg_last_error = 0;
    return 0;
}

int virtio_gpu_is_ready(void)
{
    return vg_ready;
}

int virtio_gpu_last_error(void)
{
    return vg_last_error;
}

int virtio_gpu_width(void)
{
    return (int) vg_width_px;
}

int virtio_gpu_height(void)
{
    return (int) vg_height_px;
}

int virtio_gpu_pitch(void)
{
    return (int) vg_pitch_px;
}

uint32_t *virtio_gpu_backbuffer(void)
{
    if (!vg_ready)
        return NULL;
    return vg_backbuffer;
}

void virtio_gpu_present(void)
{
    if (!vg_ready)
        return;
    if (vg_cmd_transfer_to_host_2d(vg_resource_id, vg_width_px, vg_height_px) < 0)
        return;
    (void) vg_cmd_resource_flush(vg_resource_id, vg_width_px, vg_height_px);
}

void virtio_gpu_handle_irq(void)
{
    if (!vg_base)
        return;
    uint32_t st = vg_mmio_read(VG_MMIO_INTERRUPT_STATUS);
    if (!st)
        return;
    vg_mmio_write(VG_MMIO_INTERRUPT_ACK, st);
    waitq_wake_all(&vg_waitq);
    kevent_push(KEVENT_TYPE_GPU_IRQ, st, 0, 0, 0);
}
