#include "kernel/syscall.h"
#include "kernel/virtio_gpu.h"
#include "kernel/virtio_input.h"
#include "kernel/wm.h"
#include "kernel/event.h"
#include "kernel/page_alloc.h"

#define WM_TEXT_SCREEN_W 80
#define WM_TEXT_SCREEN_H 25
#define WM_MAX_WINDOWS 8
#define WM_TITLE_MAX 24
#define WM_FG_LIGHT 97
#define WM_FG_DARK 30
#define WM_BG_DESKTOP 44
#define WM_BG_WINDOW 47
#define WM_BG_TITLE 46
#define WM_BG_TITLE_ACTIVE 45
#define WM_BG_TASKBAR 100
#define WM_EVENT_RING_SIZE 256
#define WM_TASKBAR_PX 28
#define WM_MIN_WIN_W_PX 120
#define WM_MIN_WIN_H_PX 80
#define WM_IMAGE_MAX_W 320
#define WM_IMAGE_MAX_H 240
#define WM_IMAGE_MAX_PIXELS (WM_IMAGE_MAX_W * WM_IMAGE_MAX_H)

struct wm_cell {
    char ch;
    unsigned char fg;
    unsigned char bg;
};

struct wm_window {
    int used;
    int id;
    int visible;
    int focusable;
    int x;
    int y;
    int w;
    int h;
    int z;
    char title[WM_TITLE_MAX];
    char text[WM_TEXT_MAX];
    int has_image;
    int image_w;
    int image_h;
    uint32_t *image_pixels;
    uint32_t image_bytes;
    struct wm_event ev_ring[WM_EVENT_RING_SIZE];
    int ev_head;
    int ev_tail;
    int ev_overflow_latched;
};

static struct wm_window wm_windows[WM_MAX_WINDOWS];
static int wm_next_id = 1;
static int wm_next_z = 1;
static int wm_screen_w = WM_TEXT_SCREEN_W;
static int wm_screen_h = WM_TEXT_SCREEN_H;
static int wm_cursor_x;
static int wm_cursor_y;
static int wm_cursor_visible;
static int wm_focus_id;
static uint64_t wm_event_tick;
static int wm_gpu_ready;
static int wm_dirty_valid;
static int wm_dirty_x0;
static int wm_dirty_y0;
static int wm_dirty_x1;
static int wm_dirty_y1;
static int wm_clip_enabled;
static int wm_clip_x0;
static int wm_clip_y0;
static int wm_clip_x1;
static int wm_clip_y1;

#define WM_DRAG_NONE 0
#define WM_DRAG_MOVE 1
#define WM_DRAG_RESIZE 2

static int wm_usable_h(void);

static void wm_mark_dirty_rect(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;
    int x0 = x;
    int y0 = y;
    int x1 = x + w;
    int y1 = y + h;
    if (x1 <= 0 || y1 <= 0 || x0 >= wm_screen_w || y0 >= wm_screen_h)
        return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > wm_screen_w) x1 = wm_screen_w;
    if (y1 > wm_screen_h) y1 = wm_screen_h;
    if (x0 >= x1 || y0 >= y1) return;
    if (!wm_dirty_valid) {
        wm_dirty_valid = 1;
        wm_dirty_x0 = x0;
        wm_dirty_y0 = y0;
        wm_dirty_x1 = x1;
        wm_dirty_y1 = y1;
        return;
    }
    if (x0 < wm_dirty_x0) wm_dirty_x0 = x0;
    if (y0 < wm_dirty_y0) wm_dirty_y0 = y0;
    if (x1 > wm_dirty_x1) wm_dirty_x1 = x1;
    if (y1 > wm_dirty_y1) wm_dirty_y1 = y1;
}

static void wm_mark_dirty_window_box(int x, int y, int w, int h)
{
    wm_mark_dirty_rect(x - 4, y - 4, w + 8, h + 8);
}

static void wm_mark_dirty_window(const struct wm_window *w)
{
    if (!w || !w->used) return;
    wm_mark_dirty_window_box(w->x, w->y, w->w, w->h);
}

static void wm_mark_dirty_taskbar(void)
{
    int y0 = wm_usable_h();
    if (y0 < 0) y0 = 0;
    wm_mark_dirty_rect(0, y0, wm_screen_w, wm_screen_h - y0);
}

static void wm_mark_dirty_full(void)
{
    wm_dirty_valid = 1;
    wm_dirty_x0 = 0;
    wm_dirty_y0 = 0;
    wm_dirty_x1 = wm_screen_w;
    wm_dirty_y1 = wm_screen_h;
}

static void wm_window_free_image(struct wm_window *win)
{
    if (!win) return;
    if (win->image_pixels && win->image_bytes > 0) {
        // In monolithic mode, we use kernel free_pages or similar if it was alloc_pages.
        // But user_wm_runtime used u_munmap. We'll need a way to free image pixels.
        // For simplicity, if we used kmalloc or alloc_pages, we should free it here.
        // Let's assume we use alloc_pages for now.
        free_pages((paddr_t)(uint32_t)win->image_pixels, (win->image_bytes + PAGE_SIZE - 1) / PAGE_SIZE);
    }
    win->image_pixels = NULL;
    win->image_bytes = 0;
    win->has_image = 0;
    win->image_w = 0;
    win->image_h = 0;
}

static int wm_usable_h(void)
{
    if (wm_gpu_ready) {
        int h = wm_screen_h - WM_TASKBAR_PX;
        return (h > 1) ? h : 1;
    }
    return WM_TEXT_SCREEN_H - 1;
}

int wm_screen_width(void) { return wm_screen_w; }
int wm_screen_height(void) { return wm_screen_h; }

static int vi_u32_to_s32(uint32_t v)
{
    if (v & 0x80000000u) return -((int) ((~v) + 1u));
    return (int) v;
}

static struct wm_window *wm_find(int id)
{
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (wm_windows[i].used && wm_windows[i].id == id) return &wm_windows[i];
    }
    return NULL;
}

static void wm_queue_event(struct wm_window *w, uint32_t type, uint32_t window_id, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    if (!w || !w->used) return;
    if (w->ev_head != w->ev_tail) {
        int last = (w->ev_tail + WM_EVENT_RING_SIZE - 1) % WM_EVENT_RING_SIZE;
        struct wm_event *prev = &w->ev_ring[last];
        if (type == WM_EV_POINTER_MOVE && prev->type == WM_EV_POINTER_MOVE && prev->window_id == window_id) {
            prev->timestamp_ms = wm_event_tick++;
            prev->a = a; prev->b = b; prev->c = c; prev->d = d;
            return;
        }
        if (type == WM_EV_WHEEL && prev->type == WM_EV_WHEEL && prev->window_id == window_id) {
            prev->timestamp_ms = wm_event_tick++;
            prev->a += a; prev->b = b; prev->c = c; prev->d = d;
            return;
        }
    }
    int next = (w->ev_tail + 1) % WM_EVENT_RING_SIZE;
    if (next == w->ev_head) {
        if (!w->ev_overflow_latched) {
            w->ev_overflow_latched = 1;
            w->ev_ring[w->ev_tail].type = WM_EV_OVERFLOW;
            w->ev_ring[w->ev_tail].window_id = (uint32_t) w->id;
            w->ev_ring[w->ev_tail].timestamp_ms = wm_event_tick++;
            w->ev_tail = next;
            if (w->ev_tail == w->ev_head) w->ev_head = (w->ev_head + 1) % WM_EVENT_RING_SIZE;
        }
        return;
    }
    w->ev_ring[w->ev_tail].type = type;
    w->ev_ring[w->ev_tail].window_id = window_id;
    w->ev_ring[w->ev_tail].timestamp_ms = wm_event_tick++;
    w->ev_ring[w->ev_tail].a = a; w->ev_ring[w->ev_tail].b = b;
    w->ev_ring[w->ev_tail].c = c; w->ev_ring[w->ev_tail].d = d;
    w->ev_tail = next;
}

static int wm_set_focus_internal(int id)
{
    if (wm_focus_id == id) return 0;
    struct wm_window *oldw = wm_find(wm_focus_id);
    struct wm_window *neww = wm_find(id);
    if (id > 0 && (!neww || !neww->focusable || !neww->visible)) return -1;
    wm_focus_id = id;
    if (oldw) wm_queue_event(oldw, WM_EV_FOCUS_OUT, (uint32_t) oldw->id, 0, 0, 0, 0);
    if (neww) wm_queue_event(neww, WM_EV_FOCUS_IN, (uint32_t) neww->id, 0, 0, 0, 0);
    wm_mark_dirty_taskbar();
    wm_mark_dirty_window(oldw);
    wm_mark_dirty_window(neww);
    return 0;
}

static struct wm_window *wm_top_at(int x, int y, int ignore_id)
{
    struct wm_window *best = NULL;
    int best_z = -1;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        struct wm_window *w = &wm_windows[i];
        if (!w->used || !w->visible || w->id == ignore_id) continue;
        if (x < w->x || y < w->y || x >= w->x + w->w || y >= w->y + w->h) continue;
        if (w->z > best_z) {
            best = w;
            best_z = w->z;
        }
    }
    return best;
}

static void wm_repack_z(void)
{
    int order[WM_MAX_WINDOWS];
    int n = 0;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (wm_windows[i].used) order[n++] = i;
    }
    for (int i = 0; i < n; i++) {
        int best = i;
        for (int j = i + 1; j < n; j++) {
            if (wm_windows[order[j]].z < wm_windows[order[best]].z) best = j;
        }
        int tmp = order[i]; order[i] = order[best]; order[best] = tmp;
    }
    for (int i = 0; i < n; i++) wm_windows[order[i]].z = i + 1;
    wm_next_z = n + 1;
}

void wm_init(void)
{
    printf("wm: initializing windows...\n");
    memset(wm_windows, 0, sizeof(wm_windows));
    wm_next_id = 1; wm_next_z = 1;
    printf("wm: calling virtio_gpu_init...\n");
    int gpu_rc = virtio_gpu_init();
    printf("wm: virtio_gpu_init returned %d\n", gpu_rc);
    wm_gpu_ready = (gpu_rc == 0) ? 1 : 0;
    if (wm_gpu_ready) {
        printf("wm: gpu is ready, getting dims\n");
        wm_screen_w = virtio_gpu_width();
        wm_screen_h = virtio_gpu_height();
    } else {
        printf("wm: gpu NOT ready, using text mode\n");
        wm_screen_w = WM_TEXT_SCREEN_W;
        wm_screen_h = WM_TEXT_SCREEN_H;
    }
    printf("wm: setting up cursor...\n");
    wm_cursor_x = wm_screen_w / 2;
    wm_cursor_y = wm_usable_h() / 2;
    wm_cursor_visible = 1;
    wm_dirty_valid = 0;
    wm_mark_dirty_full();
    printf("wm: init complete (%dx%d)\n", wm_screen_w, wm_screen_h);
}

int wm_create(const char *title, int w, int h)
{
    int slot = -1;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (!wm_windows[i].used) { slot = i; break; }
    }
    if (slot < 0) return -1;
    struct wm_window *win = &wm_windows[slot];
    memset(win, 0, sizeof(*win));
    win->used = 1; win->id = wm_next_id++;
    win->visible = 1; win->focusable = 1;
    win->w = w; win->h = h;
    win->x = (win->id * 37) % (wm_screen_w - w + 1);
    win->y = (win->id * 2) % (wm_usable_h() - h + 1);
    win->z = wm_next_z++;
    strncpy(win->title, title ? title : "window", WM_TITLE_MAX);
    strncpy(win->text, "(empty)", WM_TEXT_MAX);
    wm_mark_dirty_window(win);
    wm_mark_dirty_taskbar();
    return win->id;
}

int wm_move(int id, int x, int y)
{
    struct wm_window *win = wm_find(id);
    if (!win) return -1;
    wm_mark_dirty_window(win);
    win->x = x; win->y = y;
    wm_mark_dirty_window(win);
    return 0;
}

int wm_resize(int id, int w, int h)
{
    struct wm_window *win = wm_find(id);
    if (!win) return -1;
    wm_mark_dirty_window(win);
    win->w = w; win->h = h;
    wm_mark_dirty_window(win);
    return 0;
}

int wm_focus(int id)
{
    struct wm_window *win = wm_find(id);
    if (!win) return -1;
    win->z = wm_next_z++;
    return wm_set_focus_internal(id);
}

int wm_close(int id)
{
    struct wm_window *win = wm_find(id);
    if (!win) return -1;
    wm_mark_dirty_window(win);
    wm_window_free_image(win);
    win->used = 0;
    if (wm_focus_id == id) wm_focus_id = 0;
    wm_mark_dirty_taskbar();
    return 0;
}

int wm_set_text(int id, const char *text)
{
    struct wm_window *win = wm_find(id);
    if (!win) return -1;
    strncpy(win->text, text, WM_TEXT_MAX);
    wm_mark_dirty_window(win);
    return 0;
}

int wm_set_image(int id, const uint32_t *pixels, int w, int h)
{
    struct wm_window *win = wm_find(id);
    if (!win) return -1;
    if (!pixels) { wm_window_free_image(win); return 0; }
    uint32_t bytes = w * h * 4;
    if (!win->image_pixels || win->image_bytes < bytes) {
        wm_window_free_image(win);
        paddr_t pa = alloc_pages((bytes + PAGE_SIZE - 1) / PAGE_SIZE);
        win->image_pixels = (uint32_t*)pa;
        win->image_bytes = bytes;
    }
    memcpy(win->image_pixels, pixels, bytes);
    win->image_w = w; win->image_h = h; win->has_image = 1;
    wm_mark_dirty_window(win);
    return 0;
}

static void wm_gpu_fill_rect(uint32_t *fb, int fb_w, int fb_h, int x, int y, int w, int h, uint32_t color)
{
    if (!fb) return;
    for (int yy = y; yy < y + h; yy++) {
        if (yy < 0 || yy >= fb_h) continue;
        for (int xx = x; xx < x + w; xx++) {
            if (xx < 0 || xx >= fb_w) continue;
            if (wm_clip_enabled && (xx < wm_clip_x0 || yy < wm_clip_y0 || xx >= wm_clip_x1 || yy >= wm_clip_y1)) continue;
            fb[yy * fb_w + xx] = 0xff000000u | color;
        }
    }
}

static void wm_render_gpu(void)
{
    uint32_t *fb = virtio_gpu_backbuffer();
    if (!fb || !wm_dirty_valid) return;
    int fb_w = virtio_gpu_width();
    int fb_h = virtio_gpu_height();
    wm_clip_enabled = 1;
    wm_clip_x0 = wm_dirty_x0; wm_clip_y0 = wm_dirty_y0;
    wm_clip_x1 = wm_dirty_x1; wm_clip_y1 = wm_dirty_y1;

    wm_gpu_fill_rect(fb, fb_w, fb_h, 0, 0, fb_w, fb_h, 0x00162236u);

    int order[WM_MAX_WINDOWS];
    int n = 0;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) if (wm_windows[i].used && wm_windows[i].visible) order[n++] = i;
    for (int i = 0; i < n; i++) {
        int best = i;
        for (int j = i + 1; j < n; j++) if (wm_windows[order[j]].z < wm_windows[order[best]].z) best = j;
        int tmp = order[i]; order[i] = order[best]; order[best] = tmp;
    }

    for (int i = 0; i < n; i++) {
        struct wm_window *w = &wm_windows[order[i]];
        uint32_t color = (w->id == wm_focus_id) ? 0x003b4d63u : 0x00242a33u;
        wm_gpu_fill_rect(fb, fb_w, fb_h, w->x, w->y, w->w, w->h, color);
        if (w->has_image && w->image_pixels) {
            // Very basic blit
            for (int yy = 0; yy < w->image_h && yy < w->h; yy++) {
                for (int xx = 0; xx < w->image_w && xx < w->w; xx++) {
                    int dx = w->x + xx; int dy = w->y + yy;
                    if (dx >= 0 && dx < fb_w && dy >= 0 && dy < fb_h) {
                        if (wm_clip_enabled && (dx < wm_clip_x0 || dy < wm_clip_y0 || dx >= wm_clip_x1 || dy >= wm_clip_y1)) continue;
                        fb[dy * fb_w + dx] = 0xff000000u | w->image_pixels[yy * w->image_w + xx];
                    }
                }
            }
        }
    }

    // Cursor
    wm_gpu_fill_rect(fb, fb_w, fb_h, wm_cursor_x, wm_cursor_y, 8, 8, 0x00ffffffu);

    wm_clip_enabled = 0; wm_dirty_valid = 0;
    virtio_gpu_present();
}

void wm_render(void)
{
    wm_repack_z();
    if (wm_gpu_ready) wm_render_gpu();
}

int wm_poll_mouse_input(void)
{
    struct virtio_input_event ev;
    int changed = 0;
    while (virtio_input_next_event(&ev)) {
        if (ev.type == VI_EV_REL) {
            if (ev.code == VI_REL_X) { wm_cursor_x += vi_u32_to_s32(ev.value); changed = 1; }
            if (ev.code == VI_REL_Y) { wm_cursor_y += vi_u32_to_s32(ev.value); changed = 1; }
        }
        if (ev.type == VI_EV_KEY && ev.code == VI_BTN_LEFT) {
            if (ev.value) {
                struct wm_window *w = wm_top_at(wm_cursor_x, wm_cursor_y, 0);
                if (w) wm_focus(w->id);
            }
            changed = 1;
        }
    }
    if (changed) wm_mark_dirty_full();
    return changed;
}

int wm_input_init(void) { return virtio_input_init(); }
int wm_poll_event(int window_id, struct wm_event *ev)
{
    struct wm_window *w = wm_find(window_id);
    if (!w || w->ev_head == w->ev_tail) return 0;
    *ev = w->ev_ring[w->ev_head];
    w->ev_head = (w->ev_head + 1) % WM_EVENT_RING_SIZE;
    return 1;
}
void wm_cursor_move(int dx, int dy) { wm_cursor_x += dx; wm_cursor_y += dy; wm_mark_dirty_full(); }
