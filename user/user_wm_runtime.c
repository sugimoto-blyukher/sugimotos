#include "user_app.h"

// Export as userland WM symbols to avoid colliding with kernel wm.c symbols.
#define wm_screen_width uwm_screen_width
#define wm_screen_height uwm_screen_height
#define wm_input_init uwm_input_init
#define wm_poll_mouse_input uwm_poll_mouse_input
#define wm_set_desktop_id uwm_set_desktop_id
#define wm_init uwm_init
#define wm_create uwm_create
#define wm_move uwm_move
#define wm_resize uwm_resize
#define wm_focus uwm_focus
#define wm_raise uwm_raise
#define wm_close uwm_close
#define wm_set_visible uwm_set_visible
#define wm_set_text uwm_set_text
#define wm_set_image uwm_set_image
#define wm_get_rect uwm_get_rect
#define wm_render uwm_render
#define wm_list uwm_list
#define wm_tile uwm_tile
#define wm_cursor_move uwm_cursor_move
#define wm_drag_end uwm_drag_end
#define wm_drag_begin_from_cursor uwm_drag_begin_from_cursor
#define wm_set_capture uwm_set_capture
#define wm_get_focus uwm_get_focus
#define wm_get_capture uwm_get_capture
#define wm_poll_event uwm_poll_event
#define wm_dump_state uwm_dump_state

static struct sys_gpu_info u_gpu_info;
static uint32_t *u_gpu_backing;
static uint32_t u_gpu_backing_bytes;
static int u_gpu_inited;
static int u_input_inited;

static int u_gpu_init_local(void)
{
    if (u_gpu_inited)
        return (u_gpu_info.ready != 0) ? 0 : -1;
    if (u_syscall0(SYS_GPU_INIT) < 0)
        return -1;
    if (u_syscall1(SYS_GPU_INFO, (uint32_t) &u_gpu_info) < 0)
        return -1;
    if (!u_gpu_info.ready || u_gpu_info.width == 0 || u_gpu_info.height == 0)
        return -1;
    uint64_t bytes64 = (uint64_t) u_gpu_info.width * (uint64_t) u_gpu_info.height * sizeof(uint32_t);
    if (bytes64 == 0 || bytes64 > 0xffffffffu)
        return -1;
    u_gpu_backing_bytes = (uint32_t) bytes64;
    int addr = u_mmap(0, u_gpu_backing_bytes, VMA_PROT_R | VMA_PROT_W, 0);
    if (addr < 0)
        return -1;
    u_gpu_backing = (uint32_t *) (uint32_t) addr;
    // Keep allocation lazy; first render/present will fault in needed pages.
    u_gpu_inited = 1;
    return 0;
}

__attribute__((unused)) static int u_gpu_is_ready_local(void)
{
    if (!u_gpu_inited)
        return 0;
    return u_gpu_info.ready ? 1 : 0;
}

static int u_gpu_last_error_local(void)
{
    return (int) u_gpu_info.last_error;
}

static int u_gpu_width_local(void)
{
    return (int) u_gpu_info.width;
}

static int u_gpu_height_local(void)
{
    return (int) u_gpu_info.height;
}

__attribute__((unused)) static int u_gpu_pitch_local(void)
{
    return (int) u_gpu_info.pitch;
}

static uint32_t *u_gpu_backbuffer_local(void)
{
    return u_gpu_backing;
}

static void u_gpu_present_local(void)
{
    if (!u_gpu_backing || u_gpu_backing_bytes == 0)
        return;
    (void) u_syscall2(SYS_GPU_PRESENT, (uint32_t) u_gpu_backing, u_gpu_backing_bytes);
}

static int u_input_init_local(void)
{
    if (u_input_inited)
        return 0;
    if (u_syscall0(SYS_INPUT_INIT) < 0)
        return -1;
    u_input_inited = 1;
    return 0;
}

static int u_input_next_event_local(struct virtio_input_event *ev)
{
    return u_syscall1(SYS_INPUT_NEXT_EVENT, (uint32_t) ev);
}

#define virtio_gpu_init u_gpu_init_local
#define virtio_gpu_is_ready u_gpu_is_ready_local
#define virtio_gpu_last_error u_gpu_last_error_local
#define virtio_gpu_width u_gpu_width_local
#define virtio_gpu_height u_gpu_height_local
#define virtio_gpu_pitch u_gpu_pitch_local
#define virtio_gpu_backbuffer u_gpu_backbuffer_local
#define virtio_gpu_present u_gpu_present_local
#define virtio_input_init u_input_init_local
#define virtio_input_next_event u_input_next_event_local

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
static struct wm_cell wm_fb[WM_TEXT_SCREEN_H][WM_TEXT_SCREEN_W];
static int wm_screen_w = WM_TEXT_SCREEN_W;
static int wm_screen_h = WM_TEXT_SCREEN_H;
static int wm_cursor_x;
static int wm_cursor_y;
static int wm_cursor_visible;
static int wm_drag_id;
static int wm_drag_mode;
static int wm_drag_off_x;
static int wm_drag_off_y;
static int wm_drag_start_w;
static int wm_drag_start_h;
static int wm_desktop_id;
static int wm_focus_id;
static int wm_capture_id;
static uint64_t wm_event_tick;
static int wm_gpu_ready;
static int vi_ready;
static int vi_rel_dx;
static int vi_rel_dy;
static int vi_wheel;
static int vi_btn_left_down;
static int vi_shift_down;
static int vi_ctrl_down;
static int wm_input_debug = 1;
static int wm_motion_debug_budget = 64;
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
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > wm_screen_w)
        x1 = wm_screen_w;
    if (y1 > wm_screen_h)
        y1 = wm_screen_h;
    if (x0 >= x1 || y0 >= y1)
        return;
    if (!wm_dirty_valid) {
        wm_dirty_valid = 1;
        wm_dirty_x0 = x0;
        wm_dirty_y0 = y0;
        wm_dirty_x1 = x1;
        wm_dirty_y1 = y1;
        return;
    }
    if (x0 < wm_dirty_x0)
        wm_dirty_x0 = x0;
    if (y0 < wm_dirty_y0)
        wm_dirty_y0 = y0;
    if (x1 > wm_dirty_x1)
        wm_dirty_x1 = x1;
    if (y1 > wm_dirty_y1)
        wm_dirty_y1 = y1;
}

static void wm_mark_dirty_window_box(int x, int y, int w, int h)
{
    wm_mark_dirty_rect(x - 4, y - 4, w + 8, h + 8);
}

static void wm_mark_dirty_window(const struct wm_window *w)
{
    if (!w || !w->used)
        return;
    wm_mark_dirty_window_box(w->x, w->y, w->w, w->h);
}

static void wm_mark_dirty_taskbar(void)
{
    int y0 = wm_usable_h();
    if (y0 < 0)
        y0 = 0;
    wm_mark_dirty_rect(0, y0, wm_screen_w, wm_screen_h - y0);
}

static void wm_mark_dirty_cursor_xy(int x, int y)
{
    wm_mark_dirty_rect(x - 1, y - 1, 18, 18);
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
    if (!win)
        return;
    if (win->image_pixels && win->image_bytes > 0)
        (void) u_munmap((uint32_t) win->image_pixels, win->image_bytes);
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

static int wm_gpu_border_for_window(const struct wm_window *w)
{
    int ww = w->w;
    int wh = w->h;
    return (ww > 160 || wh > 120) ? 3 : 2;
}

static int wm_gpu_title_h_for_window(const struct wm_window *w, int border)
{
    int wh = w->h;
    int title_h = wh / 8;
    if (title_h < 18)
        title_h = 18;
    if (title_h > 34)
        title_h = 34;
    if (title_h >= wh - border * 2)
        title_h = wh - border * 2 - 1;
    if (title_h < 6)
        title_h = 6;
    return title_h;
}

static int wm_gpu_close_button_rect(const struct wm_window *w, int *x, int *y, int *size)
{
    int border = wm_gpu_border_for_window(w);
    int title_h = wm_gpu_title_h_for_window(w, border);
    int btn_size = title_h - 6;
    if (btn_size < 10)
        btn_size = 10;
    if (btn_size > 18)
        btn_size = 18;
    int bx = w->x + w->w - border - 4 - btn_size;
    int by = w->y + border + (title_h - btn_size) / 2;
    if (bx <= w->x + border + 2 || by < w->y + border)
        return 0;
    if (bx + btn_size >= w->x + w->w - border)
        return 0;
    if (by + btn_size > w->y + border + title_h)
        return 0;
    *x = bx;
    *y = by;
    *size = btn_size;
    return 1;
}

static int wm_close_button_hit(const struct wm_window *w, int px, int py)
{
    if (!w || !w->used || !w->visible)
        return 0;
    if (px < w->x || py < w->y || px >= w->x + w->w || py >= w->y + w->h)
        return 0;

    if (wm_gpu_ready) {
        int bx, by, bs;
        if (!wm_gpu_close_button_rect(w, &bx, &by, &bs))
            return 0;
        return (px >= bx && px < bx + bs && py >= by && py < by + bs) ? 1 : 0;
    }

    if (py != w->y || w->w < 7)
        return 0;
    int bx0 = w->x + w->w - 4;
    int bx1 = w->x + w->w - 2;
    return (px >= bx0 && px <= bx1) ? 1 : 0;
}

int wm_screen_width(void)
{
    return wm_screen_w;
}

int wm_screen_height(void)
{
    return wm_screen_h;
}

static int vi_u32_to_s32(uint32_t v)
{
    if (v & 0x80000000u)
        return -((int) ((~v) + 1u));
    return (int) v;
}

static void wm_u_putchar(char c)
{
    register uint32_t a0 __asm__("a0") = (uint32_t) c;
    register uint32_t a7 __asm__("a7") = (uint32_t) SYS_PUTCHAR;
    __asm__ __volatile__("ecall" : "+r"(a0) : "r"(a7) : "memory");
}

static void wm_u_puts(const char *s)
{
    while (*s)
        wm_u_putchar(*s++);
}

static void wm_u_put_dec(int value)
{
    char buf[12];
    int i = 0;
    unsigned magnitude;

    if (value < 0) {
        wm_u_putchar('-');
        magnitude = (unsigned) (-value);
    } else {
        magnitude = (unsigned) value;
    }

    do {
        buf[i++] = (char) ('0' + (magnitude % 10));
        magnitude /= 10;
    } while (magnitude > 0 && i < (int) sizeof(buf));

    while (i > 0)
        wm_u_putchar(buf[--i]);
}

static char wm_apply_shift_letter(char base, int shift)
{
    if (base >= 'a' && base <= 'z')
        return shift ? (char) (base - ('a' - 'A')) : base;
    return base;
}

static char wm_apply_shift_digit(char base, int shift)
{
    if (!shift)
        return base;
    switch (base) {
        case '1': return '!';
        case '2': return '@';
        case '3': return '#';
        case '4': return '$';
        case '5': return '%';
        case '6': return '^';
        case '7': return '&';
        case '8': return '*';
        case '9': return '(';
        case '0': return ')';
        default:
            return base;
    }
}

static int wm_vi_key_to_ascii(uint16_t code, int shift)
{
    char c = 0;
    switch (code) {
        case VI_KEY_A: c = 'a'; break;
        case VI_KEY_B: c = 'b'; break;
        case VI_KEY_C: c = 'c'; break;
        case VI_KEY_D: c = 'd'; break;
        case VI_KEY_E: c = 'e'; break;
        case VI_KEY_F: c = 'f'; break;
        case VI_KEY_G: c = 'g'; break;
        case VI_KEY_H: c = 'h'; break;
        case VI_KEY_I: c = 'i'; break;
        case VI_KEY_J: c = 'j'; break;
        case VI_KEY_K: c = 'k'; break;
        case VI_KEY_L: c = 'l'; break;
        case VI_KEY_M: c = 'm'; break;
        case VI_KEY_N: c = 'n'; break;
        case VI_KEY_O: c = 'o'; break;
        case VI_KEY_P: c = 'p'; break;
        case VI_KEY_Q: c = 'q'; break;
        case VI_KEY_R: c = 'r'; break;
        case VI_KEY_S: c = 's'; break;
        case VI_KEY_T: c = 't'; break;
        case VI_KEY_U: c = 'u'; break;
        case VI_KEY_V: c = 'v'; break;
        case VI_KEY_W: c = 'w'; break;
        case VI_KEY_X: c = 'x'; break;
        case VI_KEY_Y: c = 'y'; break;
        case VI_KEY_Z: c = 'z'; break;
        case VI_KEY_1: c = '1'; break;
        case VI_KEY_2: c = '2'; break;
        case VI_KEY_3: c = '3'; break;
        case VI_KEY_4: c = '4'; break;
        case VI_KEY_5: c = '5'; break;
        case VI_KEY_6: c = '6'; break;
        case VI_KEY_7: c = '7'; break;
        case VI_KEY_8: c = '8'; break;
        case VI_KEY_9: c = '9'; break;
        case VI_KEY_0: c = '0'; break;
        case VI_KEY_SPACE: return ' ';
        case VI_KEY_ENTER: return '\n';
        case VI_KEY_TAB: return '\t';
        case VI_KEY_BACKSPACE: return '\b';
        case VI_KEY_MINUS: return shift ? '_' : '-';
        case VI_KEY_EQUAL: return shift ? '+' : '=';
        case VI_KEY_LEFTBRACE: return shift ? '{' : '[';
        case VI_KEY_RIGHTBRACE: return shift ? '}' : ']';
        case VI_KEY_BACKSLASH: return shift ? '|' : '\\';
        case VI_KEY_SEMICOLON: return shift ? ':' : ';';
        case VI_KEY_APOSTROPHE: return shift ? '"' : '\'';
        case VI_KEY_GRAVE: return shift ? '~' : '`';
        case VI_KEY_COMMA: return shift ? '<' : ',';
        case VI_KEY_DOT: return shift ? '>' : '.';
        case VI_KEY_SLASH: return shift ? '?' : '/';
        default:
            return 0;
    }
    if (c >= 'a' && c <= 'z')
        return wm_apply_shift_letter(c, shift);
    if (c >= '0' && c <= '9')
        return wm_apply_shift_digit(c, shift);
    return c;
}

static void wm_str_copy_lim(char *dst, const char *src, int max)
{
    int i = 0;
    if (max <= 0)
        return;
    while (src[i] && i + 1 < max) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void wm_clear_fb(void)
{
    for (int y = 0; y < WM_TEXT_SCREEN_H; y++) {
        for (int x = 0; x < WM_TEXT_SCREEN_W; x++) {
            wm_fb[y][x].ch = ' ';
            wm_fb[y][x].fg = WM_FG_LIGHT;
            wm_fb[y][x].bg = WM_BG_DESKTOP;
        }
    }
}

static void wm_plot_color(int x, int y, char c, int fg, int bg)
{
    if (x < 0 || y < 0 || x >= WM_TEXT_SCREEN_W || y >= WM_TEXT_SCREEN_H)
        return;
    wm_fb[y][x].ch = c;
    wm_fb[y][x].fg = (unsigned char) fg;
    wm_fb[y][x].bg = (unsigned char) bg;
}

static struct wm_window *wm_find(int id)
{
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (wm_windows[i].used && wm_windows[i].id == id)
            return &wm_windows[i];
    }
    return NULL;
}

static int wm_event_count(const struct wm_window *w)
{
    return (w->ev_tail - w->ev_head + WM_EVENT_RING_SIZE) % WM_EVENT_RING_SIZE;
}

static void wm_queue_event(struct wm_window *w,
                           uint32_t type,
                           uint32_t window_id,
                           uint32_t a,
                           uint32_t b,
                           uint32_t c,
                           uint32_t d)
{
    if (!w || !w->used)
        return;
    if (w->ev_head != w->ev_tail) {
        int last = (w->ev_tail + WM_EVENT_RING_SIZE - 1) % WM_EVENT_RING_SIZE;
        struct wm_event *prev = &w->ev_ring[last];
        if (type == WM_EV_POINTER_MOVE && prev->type == WM_EV_POINTER_MOVE && prev->window_id == window_id) {
            prev->timestamp_ms = wm_event_tick++;
            prev->a = a;
            prev->b = b;
            prev->c = c;
            prev->d = d;
            return;
        }
        if (type == WM_EV_WHEEL && prev->type == WM_EV_WHEEL && prev->window_id == window_id) {
            prev->timestamp_ms = wm_event_tick++;
            prev->a += a;
            prev->b = b;
            prev->c = c;
            prev->d = d;
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
            w->ev_ring[w->ev_tail].a = 0;
            w->ev_ring[w->ev_tail].b = 0;
            w->ev_ring[w->ev_tail].c = 0;
            w->ev_ring[w->ev_tail].d = 0;
            w->ev_tail = next;
            if (w->ev_tail == w->ev_head)
                w->ev_head = (w->ev_head + 1) % WM_EVENT_RING_SIZE;
        }
        return;
    }
    w->ev_ring[w->ev_tail].type = type;
    w->ev_ring[w->ev_tail].window_id = window_id;
    w->ev_ring[w->ev_tail].timestamp_ms = wm_event_tick++;
    w->ev_ring[w->ev_tail].a = a;
    w->ev_ring[w->ev_tail].b = b;
    w->ev_ring[w->ev_tail].c = c;
    w->ev_ring[w->ev_tail].d = d;
    w->ev_tail = next;
}

static int wm_set_focus_internal(int id)
{
    if (wm_focus_id == id)
        return 0;
    struct wm_window *oldw = wm_find(wm_focus_id);
    struct wm_window *neww = wm_find(id);
    if (id > 0 && (!neww || !neww->focusable || !neww->visible))
        return -1;
    wm_focus_id = id;
    if (oldw)
        wm_queue_event(oldw, WM_EV_FOCUS_OUT, (uint32_t) oldw->id, 0, 0, 0, 0);
    if (neww)
        wm_queue_event(neww, WM_EV_FOCUS_IN, (uint32_t) neww->id, 0, 0, 0, 0);
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
        if (!w->used || !w->visible || w->id == ignore_id)
            continue;
        if (x < w->x || y < w->y || x >= w->x + w->w || y >= w->y + w->h)
            continue;
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
        if (wm_windows[i].used)
            order[n++] = i;
    }
    for (int i = 0; i < n; i++) {
        int best = i;
        for (int j = i + 1; j < n; j++) {
            int bi = order[best];
            int ji = order[j];
            if (wm_windows[ji].z < wm_windows[bi].z ||
                (wm_windows[ji].z == wm_windows[bi].z && wm_windows[ji].id < wm_windows[bi].id)) {
                best = j;
            }
        }
        if (best != i) {
            int tmp = order[i];
            order[i] = order[best];
            order[best] = tmp;
        }
    }
    for (int i = 0; i < n; i++)
        wm_windows[order[i]].z = i + 1;
    wm_next_z = n + 1;
}

void wm_init(void)
{
    memset(wm_windows, 0, sizeof(wm_windows));
    wm_next_id = 1;
    wm_next_z = 1;
    wm_gpu_ready = (virtio_gpu_init() == 0) ? 1 : 0;
    if (wm_gpu_ready) {
        wm_screen_w = virtio_gpu_width();
        wm_screen_h = virtio_gpu_height();
        if (wm_screen_w <= 0)
            wm_screen_w = 640;
        if (wm_screen_h <= 0)
            wm_screen_h = 480;
        printf("wm: gpu mode %dx%d\n", wm_screen_w, wm_screen_h);
    } else {
        wm_screen_w = WM_TEXT_SCREEN_W;
        wm_screen_h = WM_TEXT_SCREEN_H;
        printf("wm: text mode fallback (virtio_gpu_init err=%d)\n", virtio_gpu_last_error());
    }
    wm_cursor_x = wm_screen_w / 2;
    wm_cursor_y = wm_usable_h() / 2;
    wm_cursor_visible = 1;
    wm_drag_id = 0;
    wm_drag_mode = WM_DRAG_NONE;
    wm_desktop_id = 0;
    wm_focus_id = 0;
    wm_capture_id = 0;
    wm_event_tick = 1;
    vi_rel_dx = 0;
    vi_rel_dy = 0;
    vi_wheel = 0;
    vi_btn_left_down = 0;
    vi_shift_down = 0;
    vi_ctrl_down = 0;
    wm_dirty_valid = 0;
    wm_mark_dirty_full();
}

int wm_create(const char *title, int w, int h)
{
    int min_w = wm_gpu_ready ? WM_MIN_WIN_W_PX : 10;
    int min_h = wm_gpu_ready ? WM_MIN_WIN_H_PX : 5;
    if (w < min_w)
        w = min_w;
    if (h < min_h)
        h = min_h;
    if (w > wm_screen_w)
        w = wm_screen_w;
    if (h > wm_usable_h())
        h = wm_usable_h();

    int slot = -1;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (!wm_windows[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return -1;

    struct wm_window *win = &wm_windows[slot];
    memset(win, 0, sizeof(*win));
    win->used = 1;
    win->id = wm_next_id++;
    win->visible = 1;
    win->focusable = 1;
    win->w = w;
    win->h = h;
    win->x = (win->id * 37) % (wm_screen_w - w + 1);
    win->y = (win->id * 2) % (wm_usable_h() - h + 1);
    win->z = wm_next_z++;
    wm_str_copy_lim(win->title, title ? title : "window", WM_TITLE_MAX);
    wm_str_copy_lim(win->text, "(empty)", WM_TEXT_MAX);
    wm_mark_dirty_window(win);
    wm_mark_dirty_taskbar();
    return win->id;
}

int wm_move(int id, int x, int y)
{
    struct wm_window *win = wm_find(id);
    if (!win)
        return -1;
    int old_x = win->x;
    int old_y = win->y;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x + win->w > wm_screen_w)
        x = wm_screen_w - win->w;
    if (y + win->h > wm_usable_h())
        y = wm_usable_h() - win->h;
    wm_mark_dirty_window_box(old_x, old_y, win->w, win->h);
    win->x = x;
    win->y = y;
    wm_mark_dirty_window(win);
    return 0;
}

int wm_resize(int id, int w, int h)
{
    struct wm_window *win = wm_find(id);
    if (!win)
        return -1;
    int old_w = win->w;
    int old_h = win->h;
    int min_w = wm_gpu_ready ? WM_MIN_WIN_W_PX : 10;
    int min_h = wm_gpu_ready ? WM_MIN_WIN_H_PX : 5;
    if (w < min_w)
        w = min_w;
    if (h < min_h)
        h = min_h;
    if (w > wm_screen_w)
        w = wm_screen_w;
    if (h > wm_usable_h())
        h = wm_usable_h();
    wm_mark_dirty_window_box(win->x, win->y, old_w, old_h);
    win->w = w;
    win->h = h;
    wm_mark_dirty_window(win);
    return wm_move(id, win->x, win->y);
}

int wm_focus(int id)
{
    struct wm_window *win = wm_find(id);
    if (!win)
        return -1;
    if (!win->visible || !win->focusable)
        return -1;
    win->z = wm_next_z++;
    (void) wm_set_focus_internal(id);
    return 0;
}

int wm_raise(int id)
{
    struct wm_window *win = wm_find(id);
    if (!win)
        return -1;
    win->z = wm_next_z++;
    wm_mark_dirty_full();
    return 0;
}

int wm_close(int id)
{
    struct wm_window *win = wm_find(id);
    if (!win)
        return -1;
    wm_mark_dirty_window(win);
    wm_window_free_image(win);
    if (wm_focus_id == id)
        (void) wm_set_focus_internal(0);
    if (wm_capture_id == id)
        wm_capture_id = 0;
    if (wm_drag_id == id)
        wm_drag_end();
    memset(win, 0, sizeof(*win));
    wm_mark_dirty_taskbar();
    return 0;
}

int wm_set_visible(int id, int visible)
{
    struct wm_window *win = wm_find(id);
    if (!win)
        return -1;
    wm_mark_dirty_window(win);
    win->visible = visible ? 1 : 0;
    if (!win->visible) {
        if (wm_focus_id == id)
            (void) wm_set_focus_internal(0);
        if (wm_capture_id == id)
            wm_capture_id = 0;
        if (wm_drag_id == id)
            wm_drag_end();
    }
    wm_mark_dirty_taskbar();
    return 0;
}

int wm_set_text(int id, const char *text)
{
    struct wm_window *win = wm_find(id);
    if (!win)
        return -1;
    wm_str_copy_lim(win->text, text, WM_TEXT_MAX);
    wm_mark_dirty_window(win);
    return 0;
}

int wm_set_image(int id, const uint32_t *pixels, int w, int h)
{
    struct wm_window *win = wm_find(id);
    if (!win)
        return -1;
    if (!pixels || w <= 0 || h <= 0) {
        wm_window_free_image(win);
        wm_mark_dirty_window(win);
        return 0;
    }

    int dst_w = w;
    int dst_h = h;
    if (dst_w > WM_IMAGE_MAX_W || dst_h > WM_IMAGE_MAX_H) {
        uint32_t rw = (uint32_t) (WM_IMAGE_MAX_W * 1024) / (uint32_t) dst_w;
        uint32_t rh = (uint32_t) (WM_IMAGE_MAX_H * 1024) / (uint32_t) dst_h;
        uint32_t ratio = (rw < rh) ? rw : rh;
        if (ratio < 1)
            ratio = 1;
        dst_w = (int) (((uint32_t) dst_w * ratio) / 1024u);
        dst_h = (int) (((uint32_t) dst_h * ratio) / 1024u);
        if (dst_w < 1)
            dst_w = 1;
        if (dst_h < 1)
            dst_h = 1;
        if (dst_w > WM_IMAGE_MAX_W)
            dst_w = WM_IMAGE_MAX_W;
        if (dst_h > WM_IMAGE_MAX_H)
            dst_h = WM_IMAGE_MAX_H;
    }

    uint32_t need_bytes = (uint32_t) dst_w * (uint32_t) dst_h * (uint32_t) sizeof(uint32_t);
    if (!win->image_pixels || win->image_bytes < need_bytes) {
        wm_window_free_image(win);
        int addr = u_mmap(0, need_bytes, VMA_PROT_R | VMA_PROT_W, 0);
        if (addr < 0)
            return -1;
        win->image_pixels = (uint32_t *) (uint32_t) addr;
        win->image_bytes = need_bytes;
    }

    for (int y = 0; y < dst_h; y++) {
        int sy = (y * h) / dst_h;
        for (int x = 0; x < dst_w; x++) {
            int sx = (x * w) / dst_w;
            win->image_pixels[y * dst_w + x] = pixels[sy * w + sx];
        }
    }
    win->image_w = dst_w;
    win->image_h = dst_h;
    win->has_image = 1;
    wm_mark_dirty_window(win);
    return 0;
}

int wm_get_rect(int id, int *x, int *y, int *w, int *h)
{
    struct wm_window *win = wm_find(id);
    if (!win)
        return -1;
    if (x)
        *x = win->x;
    if (y)
        *y = win->y;
    if (w)
        *w = win->w;
    if (h)
        *h = win->h;
    return 0;
}

static void wm_draw_taskbar(void)
{
    int y = WM_TEXT_SCREEN_H - 1;
    for (int x = 0; x < WM_TEXT_SCREEN_W; x++)
        wm_plot_color(x, y, ' ', WM_FG_DARK, WM_BG_TASKBAR);

    int top_id = 0;
    int top_z = -1;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (wm_windows[i].used && wm_windows[i].z > top_z) {
            top_z = wm_windows[i].z;
            top_id = wm_windows[i].id;
        }
    }

    int pos = 0;
    const char *prefix = "[miniwm] active:";
    for (int i = 0; prefix[i] && pos < WM_TEXT_SCREEN_W; i++)
        wm_plot_color(pos++, y, prefix[i], WM_FG_DARK, WM_BG_TASKBAR);

    if (top_id > 0 && pos < WM_TEXT_SCREEN_W - 1) {
        char idbuf[16];
        int n = 0;
        int v = top_id;
        char rev[16];
        int rn = 0;
        while (v > 0 && rn < (int) sizeof(rev)) {
            rev[rn++] = (char) ('0' + (v % 10));
            v /= 10;
        }
        if (rn == 0)
            rev[rn++] = '0';
        while (rn > 0)
            idbuf[n++] = rev[--rn];
        for (int i = 0; i < n && pos < WM_TEXT_SCREEN_W; i++)
            wm_plot_color(pos++, y, idbuf[i], WM_FG_DARK, WM_BG_TASKBAR);
    }
}

static void wm_apply_cursor_overlay(void)
{
    if (!wm_cursor_visible)
        return;
    if (wm_cursor_x < 0 || wm_cursor_y < 0 || wm_cursor_x >= WM_TEXT_SCREEN_W || wm_cursor_y >= WM_TEXT_SCREEN_H)
        return;
    wm_plot_color(wm_cursor_x, wm_cursor_y, '@', 15, 41);
}

static void wm_draw_window(const struct wm_window *w, int active)
{
    int x0 = w->x;
    int y0 = w->y;
    int x1 = w->x + w->w - 1;
    int y1 = w->y + w->h - 1;
    int title_bg = active ? WM_BG_TITLE_ACTIVE : WM_BG_TITLE;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++)
            wm_plot_color(x, y, ' ', WM_FG_DARK, WM_BG_WINDOW);
    }
    for (int x = x0; x <= x1; x++) {
        wm_plot_color(x, y0, '=', WM_FG_DARK, title_bg);
        wm_plot_color(x, y1, '_', WM_FG_DARK, WM_BG_WINDOW);
    }
    for (int y = y0; y <= y1; y++) {
        wm_plot_color(x0, y, '|', WM_FG_DARK, WM_BG_WINDOW);
        wm_plot_color(x1, y, '|', WM_FG_DARK, WM_BG_WINDOW);
    }
    wm_plot_color(x0, y0, '+', WM_FG_DARK, title_bg);
    wm_plot_color(x1, y0, '+', WM_FG_DARK, title_bg);
    wm_plot_color(x0, y1, '+', WM_FG_DARK, WM_BG_WINDOW);
    wm_plot_color(x1, y1, '+', WM_FG_DARK, WM_BG_WINDOW);

    int tx = x0 + 2;
    for (int i = 0; w->title[i] && tx < x1 - 1; i++, tx++)
        wm_plot_color(tx, y0, w->title[i], WM_FG_DARK, title_bg);
    if (w->w >= 7) {
        int bx0 = x1 - 3;
        wm_plot_color(bx0, y0, '[', WM_FG_DARK, title_bg);
        wm_plot_color(bx0 + 1, y0, 'X', WM_FG_DARK, title_bg);
        wm_plot_color(bx0 + 2, y0, ']', WM_FG_DARK, title_bg);
    }

    int cx = x0 + 1;
    int cy = y0 + 1;
    for (int i = 0; w->text[i] && cy < y1; i++) {
        char c = w->text[i];
        if (c == '\n' || cx >= x1) {
            cx = x0 + 1;
            cy++;
            if (cy >= y1)
                break;
            if (c == '\n')
                continue;
        }
        if (c >= 32 && c <= 126)
            wm_plot_color(cx, cy, c, WM_FG_DARK, WM_BG_WINDOW);
        cx++;
    }
}

static void wm_set_ansi_color(int fg, int bg)
{
    wm_u_puts("\x1b[");
    wm_u_put_dec(fg);
    wm_u_putchar(';');
    wm_u_put_dec(bg);
    wm_u_puts("m");
}

static void wm_gpu_fill_rect(uint32_t *fb,
                             int fb_w,
                             int fb_h,
                             int x,
                             int y,
                             int w,
                             int h,
                             uint32_t color)
{
    if (!fb || w <= 0 || h <= 0)
        return;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x >= fb_w || y >= fb_h)
        return;
    if (x + w > fb_w)
        w = fb_w - x;
    if (y + h > fb_h)
        h = fb_h - y;
    if (wm_clip_enabled) {
        if (x < wm_clip_x0) {
            w -= (wm_clip_x0 - x);
            x = wm_clip_x0;
        }
        if (y < wm_clip_y0) {
            h -= (wm_clip_y0 - y);
            y = wm_clip_y0;
        }
        if (x + w > wm_clip_x1)
            w = wm_clip_x1 - x;
        if (y + h > wm_clip_y1)
            h = wm_clip_y1 - y;
    }
    if (w <= 0 || h <= 0)
        return;
    for (int yy = 0; yy < h; yy++) {
        uint32_t *row = fb + (y + yy) * fb_w + x;
        for (int xx = 0; xx < w; xx++)
            row[xx] = 0xff000000u | color;
    }
}

static void wm_gpu_plot_pixel(uint32_t *fb, int fb_w, int fb_h, int x, int y, uint32_t color)
{
    if (!fb)
        return;
    if (x < 0 || y < 0 || x >= fb_w || y >= fb_h)
        return;
    if (wm_clip_enabled && (x < wm_clip_x0 || y < wm_clip_y0 || x >= wm_clip_x1 || y >= wm_clip_y1))
        return;
    fb[y * fb_w + x] = 0xff000000u | color;
}

static void wm_gpu_draw_rect_outline(uint32_t *fb,
                                     int fb_w,
                                     int fb_h,
                                     int x,
                                     int y,
                                     int w,
                                     int h,
                                     uint32_t color)
{
    if (w < 2 || h < 2)
        return;
    for (int xx = x; xx < x + w; xx++) {
        wm_gpu_plot_pixel(fb, fb_w, fb_h, xx, y, color);
        wm_gpu_plot_pixel(fb, fb_w, fb_h, xx, y + h - 1, color);
    }
    for (int yy = y; yy < y + h; yy++) {
        wm_gpu_plot_pixel(fb, fb_w, fb_h, x, yy, color);
        wm_gpu_plot_pixel(fb, fb_w, fb_h, x + w - 1, yy, color);
    }
}

static void wm_gpu_draw_drag_feedback(uint32_t *fb, int fb_w, int fb_h)
{
    if (wm_drag_mode == WM_DRAG_NONE || wm_drag_id <= 0)
        return;
    struct wm_window *w = wm_find(wm_drag_id);
    if (!w || !w->used || !w->visible)
        return;

    int x = w->x;
    int y = w->y;
    int ww = w->w;
    int hh = w->h;
    if (wm_drag_mode == WM_DRAG_MOVE) {
        x -= 2;
        y -= 2;
        ww += 4;
        hh += 4;
    }
    if (y + hh > wm_usable_h())
        hh = wm_usable_h() - y;
    if (ww < 2 || hh < 2)
        return;

    wm_gpu_draw_rect_outline(fb, fb_w, fb_h, x, y, ww, hh, 0x00ffd24au);
}

struct wm_font_glyph {
    char ch;
    uint8_t rows[7];
};

static const struct wm_font_glyph wm_font_5x7[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {'!', {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}},
    {'"', {0x0a, 0x0a, 0x0a, 0x00, 0x00, 0x00, 0x00}},
    {'\'', {0x04, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00}},
    {'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}},
    {')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}},
    {'*', {0x00, 0x0a, 0x04, 0x1f, 0x04, 0x0a, 0x00}},
    {'+', {0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00}},
    {',', {0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0x08}},
    {'-', {0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06}},
    {'/', {0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00}},
    {':', {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00}},
    {';', {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x08}},
    {'<', {0x01, 0x02, 0x04, 0x08, 0x04, 0x02, 0x01}},
    {'=', {0x00, 0x00, 0x1f, 0x00, 0x1f, 0x00, 0x00}},
    {'>', {0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10}},
    {'?', {0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},
    {'[', {0x0e, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0e}},
    {'\\', {0x10, 0x08, 0x04, 0x02, 0x01, 0x00, 0x00}},
    {']', {0x0e, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0e}},
    {'_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f}},
    {'0', {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e}},
    {'1', {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x1f}},
    {'2', {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f}},
    {'3', {0x1e, 0x01, 0x01, 0x06, 0x01, 0x01, 0x1e}},
    {'4', {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02}},
    {'5', {0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e}},
    {'6', {0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e}},
    {'7', {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e}},
    {'9', {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x1c}},
    {'A', {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}},
    {'B', {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e}},
    {'C', {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e}},
    {'D', {0x1c, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1c}},
    {'E', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f}},
    {'F', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10}},
    {'G', {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0e}},
    {'H', {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}},
    {'I', {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e}},
    {'J', {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0e}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f}},
    {'M', {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},
    {'O', {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}},
    {'P', {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10}},
    {'Q', {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d}},
    {'R', {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11}},
    {'S', {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e}},
    {'T', {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x1b, 0x11}},
    {'X', {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04}},
    {'Z', {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f}},
    {'a', {0x00, 0x00, 0x0e, 0x01, 0x0f, 0x11, 0x0f}},
    {'b', {0x10, 0x10, 0x1e, 0x11, 0x11, 0x11, 0x1e}},
    {'c', {0x00, 0x00, 0x0f, 0x10, 0x10, 0x10, 0x0f}},
    {'d', {0x01, 0x01, 0x0f, 0x11, 0x11, 0x11, 0x0f}},
    {'e', {0x00, 0x00, 0x0e, 0x11, 0x1f, 0x10, 0x0f}},
    {'f', {0x03, 0x04, 0x0e, 0x04, 0x04, 0x04, 0x04}},
    {'g', {0x00, 0x00, 0x0f, 0x11, 0x11, 0x0f, 0x01}},
    {'h', {0x10, 0x10, 0x1e, 0x11, 0x11, 0x11, 0x11}},
    {'i', {0x04, 0x00, 0x0c, 0x04, 0x04, 0x04, 0x0e}},
    {'j', {0x02, 0x00, 0x06, 0x02, 0x02, 0x12, 0x0c}},
    {'k', {0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12}},
    {'l', {0x0c, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e}},
    {'m', {0x00, 0x00, 0x1a, 0x15, 0x15, 0x15, 0x15}},
    {'n', {0x00, 0x00, 0x1e, 0x11, 0x11, 0x11, 0x11}},
    {'o', {0x00, 0x00, 0x0e, 0x11, 0x11, 0x11, 0x0e}},
    {'p', {0x00, 0x00, 0x1e, 0x11, 0x11, 0x1e, 0x10}},
    {'q', {0x00, 0x00, 0x0f, 0x11, 0x11, 0x0f, 0x01}},
    {'r', {0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10}},
    {'s', {0x00, 0x00, 0x0f, 0x10, 0x0e, 0x01, 0x1e}},
    {'t', {0x04, 0x04, 0x0e, 0x04, 0x04, 0x04, 0x03}},
    {'u', {0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0d}},
    {'v', {0x00, 0x00, 0x11, 0x11, 0x11, 0x0a, 0x04}},
    {'w', {0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0a}},
    {'x', {0x00, 0x00, 0x11, 0x0a, 0x04, 0x0a, 0x11}},
    {'y', {0x00, 0x00, 0x11, 0x11, 0x11, 0x0f, 0x01}},
    {'z', {0x00, 0x00, 0x1f, 0x02, 0x04, 0x08, 0x1f}},
};

static const uint8_t *wm_font_rows_5x7(char c)
{
    static const uint8_t fallback[7] = {0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04};
    for (unsigned i = 0; i < (unsigned) (sizeof(wm_font_5x7) / sizeof(wm_font_5x7[0])); i++) {
        if (wm_font_5x7[i].ch == c)
            return wm_font_5x7[i].rows;
    }
    return fallback;
}

static void wm_gpu_draw_glyph_5x7(uint32_t *fb,
                                  int fb_w,
                                  int fb_h,
                                  int x,
                                  int y,
                                  int char_w,
                                  int char_h,
                                  char c,
                                  uint32_t color)
{
    if (char_w <= 0 || char_h <= 0)
        return;
    if (c == ' ')
        return;
    const uint8_t *rows = wm_font_rows_5x7(c);
    int px = char_w / 5;
    int py = char_h / 7;
    if (px < 1)
        px = 1;
    if (py < 1)
        py = 1;
    int gw = px * 5;
    int gh = py * 7;
    int ox = x + (char_w - gw) / 2;
    int oy = y + (char_h - gh) / 2;
    for (int ry = 0; ry < 7; ry++) {
        uint8_t bits = rows[ry];
        for (int rx = 0; rx < 5; rx++) {
            if (!(bits & (uint8_t) (1u << (4 - rx))))
                continue;
            wm_gpu_fill_rect(fb, fb_w, fb_h, ox + rx * px, oy + ry * py, px, py, color);
        }
    }
}

static void wm_gpu_draw_window_rect(uint32_t *fb,
                                    int fb_w,
                                    int fb_h,
                                    const struct wm_window *w,
                                    int active)
{
    int x0 = w->x;
    int y0 = w->y;
    int x1 = w->x + w->w;
    int y1 = w->y + w->h;
    int taskbar_y = wm_usable_h();
    if (y1 > taskbar_y)
        y1 = taskbar_y;
    if (x1 <= x0 + 4 || y1 <= y0 + 4)
        return;

    int ww = x1 - x0;
    int wh = y1 - y0;
    int border = wm_gpu_border_for_window(w);
    int title_h = wm_gpu_title_h_for_window(w, border);

    uint32_t border_color = active ? 0x00f0c040u : 0x007a8594u;
    uint32_t title_color = active ? 0x001f5f9du : 0x003b4d63u;
    uint32_t body_color = active ? 0x00d9e2eeu : 0x00c8d0d8u;
    uint32_t text_color = 0x00141b24u;

    wm_gpu_fill_rect(fb, fb_w, fb_h, x0, y0, ww, wh, border_color);
    wm_gpu_fill_rect(fb,
                     fb_w,
                     fb_h,
                     x0 + border,
                     y0 + border,
                     ww - border * 2,
                     title_h,
                     title_color);
    wm_gpu_fill_rect(fb,
                     fb_w,
                     fb_h,
                     x0 + border,
                     y0 + border + title_h,
                     ww - border * 2,
                     wh - title_h - border * 2,
                     body_color);

    int title_cw = ww / WM_TITLE_MAX;
    if (title_cw < 6)
        title_cw = 6;
    int title_ch = title_h - 4;
    if (title_ch < 7)
        title_ch = 7;
    int tx = x0 + border + 4;
    int ty = y0 + border + 2;
    for (int i = 0; w->title[i] && tx + title_cw < x1 - border; i++, tx += title_cw) {
        wm_gpu_draw_glyph_5x7(fb, fb_w, fb_h, tx, ty, title_cw - 1, title_ch - 1, w->title[i], 0x00e8edf4u);
    }
    int bx, by, bs;
    if (wm_gpu_close_button_rect(w, &bx, &by, &bs)) {
        uint32_t close_bg = active ? 0x00c84343u : 0x009a6a6au;
        wm_gpu_fill_rect(fb, fb_w, fb_h, bx, by, bs, bs, close_bg);
        wm_gpu_draw_rect_outline(fb, fb_w, fb_h, bx, by, bs, bs, 0x00f4e9e9u);
        int pad = bs / 4;
        if (pad < 2)
            pad = 2;
        for (int i = pad; i < bs - pad; i++) {
            wm_gpu_plot_pixel(fb, fb_w, fb_h, bx + i, by + i, 0x00fff6f6u);
            wm_gpu_plot_pixel(fb, fb_w, fb_h, bx + (bs - 1 - i), by + i, 0x00fff6f6u);
        }
    }

    int text_x0 = x0 + border + 4;
    int text_y0 = y0 + border + title_h + 4;
    int text_x1 = x1 - border - 4;
    int text_y1 = y1 - border - 4;
    if (text_x1 <= text_x0 || text_y1 <= text_y0)
        return;

    if (w->has_image && w->image_pixels && w->image_w > 0 && w->image_h > 0) {
        int avail_w = text_x1 - text_x0;
        int avail_h = text_y1 - text_y0;
        if (avail_w > 0 && avail_h > 0) {
            int draw_w = avail_w;
            int draw_h = (draw_w * w->image_h) / w->image_w;
            if (draw_h > avail_h) {
                draw_h = avail_h;
                draw_w = (draw_h * w->image_w) / w->image_h;
            }
            if (draw_w < 1)
                draw_w = 1;
            if (draw_h < 1)
                draw_h = 1;
            int dx0 = text_x0 + (avail_w - draw_w) / 2;
            int dy0 = text_y0 + (avail_h - draw_h) / 2;
            for (int dy = 0; dy < draw_h; dy++) {
                int sy = (dy * w->image_h) / draw_h;
                for (int dx = 0; dx < draw_w; dx++) {
                    int sx = (dx * w->image_w) / draw_w;
                    uint32_t pix = w->image_pixels[sy * w->image_w + sx];
                    wm_gpu_plot_pixel(fb, fb_w, fb_h, dx0 + dx, dy0 + dy, pix & 0x00ffffffu);
                }
            }
        }
        return;
    }

    int char_w = 8;
    int char_h = 12;
    int cx = text_x0;
    int cy = text_y0;
    for (int i = 0; w->text[i] && cy + char_h <= text_y1; i++) {
        char c = w->text[i];
        if (c == '\n') {
            cx = text_x0;
            cy += char_h;
            continue;
        }
        if (cx + char_w > text_x1) {
            cx = text_x0;
            cy += char_h;
            if (cy + char_h > text_y1)
                break;
        }
        if (c >= 32 && c <= 126)
            wm_gpu_draw_glyph_5x7(fb, fb_w, fb_h, cx, cy, char_w, char_h, c, text_color);
        cx += char_w;
    }
}

static void wm_gpu_draw_taskbar(uint32_t *fb, int fb_w, int fb_h)
{
    int y0 = wm_usable_h();
    if (y0 < 0)
        y0 = 0;
    int h = fb_h - y0;
    if (h <= 0)
        return;
    wm_gpu_fill_rect(fb, fb_w, fb_h, 0, y0, fb_w, h, 0x00242a33u);

    int top_id = 0;
    int top_z = -1;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (wm_windows[i].used && wm_windows[i].visible && wm_windows[i].z > top_z) {
            top_z = wm_windows[i].z;
            top_id = wm_windows[i].id;
        }
    }

    char label[64];
    int pos = 0;
    const char *prefix = "miniwm active:";
    for (int i = 0; prefix[i] && pos + 1 < (int) sizeof(label); i++)
        label[pos++] = prefix[i];
    if (top_id > 0 && pos + 2 < (int) sizeof(label)) {
        label[pos++] = ' ';
        char rev[16];
        int rn = 0;
        int v = top_id;
        while (v > 0 && rn < (int) sizeof(rev)) {
            rev[rn++] = (char) ('0' + (v % 10));
            v /= 10;
        }
        if (rn == 0)
            rev[rn++] = '0';
        while (rn > 0 && pos + 1 < (int) sizeof(label))
            label[pos++] = rev[--rn];
    }
    label[pos] = '\0';

    int char_w = 8;
    int char_h = h - 4;
    if (char_w < 4)
        char_w = 4;
    if (char_w > 12)
        char_w = 12;
    if (char_h < 6)
        char_h = 6;
    if (char_h > 18)
        char_h = 18;
    int x = 6;
    int y = y0 + (h - char_h) / 2;
    for (int i = 0; label[i] && x + char_w < fb_w - 4; i++, x += char_w) {
        wm_gpu_draw_glyph_5x7(fb, fb_w, fb_h, x, y, char_w, char_h, label[i], 0x00ccd3dd);
    }
}

static void wm_gpu_draw_cursor(uint32_t *fb, int fb_w, int fb_h)
{
    if (!wm_cursor_visible)
        return;
    // 16x16 arrow cursor. Bit set = draw pixel. Hotspot is (0,0).
    static const uint16_t cursor_fill[16] = {
        0x8000, 0xc000, 0xe000, 0xf000,
        0xf800, 0xfc00, 0xfe00, 0xff00,
        0xf800, 0xd800, 0x8c00, 0x0c00,
        0x0600, 0x0600, 0x0300, 0x0000,
    };
    static const uint16_t cursor_edge[16] = {
        0xc000, 0xe000, 0xf000, 0xf800,
        0xfc00, 0xfe00, 0xff00, 0xff80,
        0xffc0, 0xfcc0, 0xde00, 0x0f00,
        0x0780, 0x07c0, 0x03e0, 0x0300,
    };
    const uint32_t edge_color = 0x00101010u;
    const uint32_t fill_color = 0x00f6f7fb;
    const uint32_t shadow_color = 0x00304050u;

    int cx = wm_cursor_x;
    int cy = wm_cursor_y;

    for (int y = 0; y < 16; y++) {
        uint16_t edge = cursor_edge[y];
        uint16_t fill = cursor_fill[y];
        for (int x = 0; x < 16; x++) {
            uint16_t mask = (uint16_t) (0x8000u >> x);
            if (!(edge & mask) && !(fill & mask))
                continue;
            if (edge & mask)
                wm_gpu_plot_pixel(fb, fb_w, fb_h, cx + x + 1, cy + y + 1, shadow_color);
            if (edge & mask)
                wm_gpu_plot_pixel(fb, fb_w, fb_h, cx + x, cy + y, edge_color);
            if (fill & mask)
                wm_gpu_plot_pixel(fb, fb_w, fb_h, cx + x, cy + y, fill_color);
        }
    }
}

static void wm_draw_drag_feedback_text(void)
{
    if (wm_drag_mode == WM_DRAG_NONE || wm_drag_id <= 0)
        return;
    struct wm_window *w = wm_find(wm_drag_id);
    if (!w || !w->used || !w->visible)
        return;

    int x0 = w->x;
    int y0 = w->y;
    int x1 = w->x + w->w - 1;
    int y1 = w->y + w->h - 1;
    if (wm_drag_mode == WM_DRAG_MOVE) {
        x0--;
        y0--;
        x1++;
        y1++;
    }
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 >= WM_TEXT_SCREEN_W)
        x1 = WM_TEXT_SCREEN_W - 1;
    if (y1 >= wm_usable_h())
        y1 = wm_usable_h() - 1;
    if (x1 <= x0 || y1 <= y0)
        return;

    for (int x = x0; x <= x1; x++) {
        wm_plot_color(x, y0, '#', WM_FG_LIGHT, 41);
        wm_plot_color(x, y1, '#', WM_FG_LIGHT, 41);
    }
    for (int y = y0; y <= y1; y++) {
        wm_plot_color(x0, y, '#', WM_FG_LIGHT, 41);
        wm_plot_color(x1, y, '#', WM_FG_LIGHT, 41);
    }
}

static void wm_render_gpu(void)
{
    uint32_t *fb = virtio_gpu_backbuffer();
    if (!fb)
        return;
    int fb_w = virtio_gpu_width();
    int fb_h = virtio_gpu_height();
    if (fb_w <= 0 || fb_h <= 0)
        return;
    if (!wm_dirty_valid)
        return;

    wm_clip_enabled = 1;
    wm_clip_x0 = wm_dirty_x0;
    wm_clip_y0 = wm_dirty_y0;
    wm_clip_x1 = wm_dirty_x1;
    wm_clip_y1 = wm_dirty_y1;

    int top_h = (fb_h * 3) / 5;
    wm_gpu_fill_rect(fb, fb_w, fb_h, 0, 0, fb_w, top_h, 0x00162236u);
    wm_gpu_fill_rect(fb, fb_w, fb_h, 0, top_h, fb_w, fb_h - top_h, 0x00101a2au);
    wm_gpu_fill_rect(fb, fb_w, fb_h, 0, top_h - 1, fb_w, 2, 0x00223650u);

    int drawn[WM_MAX_WINDOWS];
    memset(drawn, 0, sizeof(drawn));
    int active_idx = -1;
    int active_z = -1;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (wm_windows[i].used && wm_windows[i].visible && wm_windows[i].z > active_z) {
            active_z = wm_windows[i].z;
            active_idx = i;
        }
    }
    for (int pass = 0; pass < WM_MAX_WINDOWS; pass++) {
        int best = -1;
        int best_z = 0x7fffffff;
        for (int i = 0; i < WM_MAX_WINDOWS; i++) {
            if (!wm_windows[i].used || !wm_windows[i].visible || drawn[i])
                continue;
            if (wm_windows[i].z < best_z) {
                best_z = wm_windows[i].z;
                best = i;
            }
        }
        if (best < 0)
            break;
        wm_gpu_draw_window_rect(fb, fb_w, fb_h, &wm_windows[best], best == active_idx);
        drawn[best] = 1;
    }

    wm_gpu_draw_drag_feedback(fb, fb_w, fb_h);
    wm_gpu_draw_taskbar(fb, fb_w, fb_h);
    wm_gpu_draw_cursor(fb, fb_w, fb_h);

    wm_clip_enabled = 0;
    wm_dirty_valid = 0;
    virtio_gpu_present();
}

void wm_render(void)
{
    wm_repack_z();
    if (!wm_dirty_valid)
        return;

    if (wm_gpu_ready) {
        wm_render_gpu();
        return;
    }

    wm_clear_fb();
    int drawn[WM_MAX_WINDOWS];
    memset(drawn, 0, sizeof(drawn));
    int active_idx = -1;
    int active_z = -1;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (wm_windows[i].used && wm_windows[i].visible && wm_windows[i].z > active_z) {
            active_z = wm_windows[i].z;
            active_idx = i;
        }
    }
    for (int pass = 0; pass < WM_MAX_WINDOWS; pass++) {
        int best = -1;
        int best_z = 0x7fffffff;
        for (int i = 0; i < WM_MAX_WINDOWS; i++) {
            if (!wm_windows[i].used || !wm_windows[i].visible || drawn[i])
                continue;
            if (wm_windows[i].z < best_z) {
                best_z = wm_windows[i].z;
                best = i;
            }
        }
        if (best < 0)
            break;
        wm_draw_window(&wm_windows[best], best == active_idx);
        drawn[best] = 1;
    }
    wm_draw_drag_feedback_text();
    wm_draw_taskbar();
    wm_apply_cursor_overlay();

    wm_u_puts("\x1b[2J\x1b[H\x1b[?25l");
    int cur_fg = -1;
    int cur_bg = -1;
    for (int y = 0; y < WM_TEXT_SCREEN_H; y++) {
        for (int x = 0; x < WM_TEXT_SCREEN_W; x++) {
            int fg = wm_fb[y][x].fg;
            int bg = wm_fb[y][x].bg;
            if (fg != cur_fg || bg != cur_bg) {
                wm_set_ansi_color(fg, bg);
                cur_fg = fg;
                cur_bg = bg;
            }
            wm_u_putchar(wm_fb[y][x].ch);
        }
        wm_u_puts("\x1b[0m");
        cur_fg = -1;
        cur_bg = -1;
        wm_u_putchar('\n');
    }
    wm_dirty_valid = 0;
}

void wm_cursor_move(int dx, int dy)
{
    int old_x = wm_cursor_x;
    int old_y = wm_cursor_y;
    wm_cursor_x += dx;
    wm_cursor_y += dy;
    if (wm_cursor_x < 0)
        wm_cursor_x = 0;
    if (wm_cursor_y < 0)
        wm_cursor_y = 0;
    if (wm_cursor_x >= wm_screen_w)
        wm_cursor_x = wm_screen_w - 1;
    if (wm_cursor_y >= wm_usable_h())
        wm_cursor_y = wm_usable_h() - 1;

    if (wm_drag_mode == WM_DRAG_MOVE && wm_drag_id > 0) {
        struct wm_window *w = wm_find(wm_drag_id);
        if (w)
            wm_move(w->id, wm_cursor_x - wm_drag_off_x, wm_cursor_y - wm_drag_off_y);
    } else if (wm_drag_mode == WM_DRAG_RESIZE && wm_drag_id > 0) {
        struct wm_window *w = wm_find(wm_drag_id);
        if (w) {
            int nw = wm_drag_start_w + (wm_cursor_x - wm_drag_off_x);
            int nh = wm_drag_start_h + (wm_cursor_y - wm_drag_off_y);
            wm_resize(w->id, nw, nh);
        }
    }
    wm_mark_dirty_cursor_xy(old_x, old_y);
    wm_mark_dirty_cursor_xy(wm_cursor_x, wm_cursor_y);
}

void wm_drag_end(void)
{
    wm_mark_dirty_full();
    wm_drag_id = 0;
    wm_drag_mode = WM_DRAG_NONE;
}

void wm_drag_begin_from_cursor(void)
{
    struct wm_window *w = wm_top_at(wm_cursor_x, wm_cursor_y, wm_desktop_id);
    if (!w) {
        wm_drag_end();
        return;
    }
    if (wm_close_button_hit(w, wm_cursor_x, wm_cursor_y)) {
        int id = w->id;
        wm_drag_end();
        wm_capture_id = 0;
        wm_close(id);
        return;
    }

    wm_focus(w->id);
    wm_capture_id = w->id;
    int title_h = wm_gpu_ready ? (w->h / 8) : 1;
    if (title_h < 1)
        title_h = 1;
    if (title_h > 34)
        title_h = 34;
    int corner = wm_gpu_ready ? 14 : 2;
    int on_corner = (wm_cursor_x >= w->x + w->w - corner && wm_cursor_y >= w->y + w->h - corner);
    if (on_corner) {
        wm_drag_mode = WM_DRAG_RESIZE;
        wm_drag_id = w->id;
        wm_drag_off_x = wm_cursor_x;
        wm_drag_off_y = wm_cursor_y;
        wm_drag_start_w = w->w;
        wm_drag_start_h = w->h;
        return;
    }
    wm_drag_mode = WM_DRAG_MOVE;
    wm_drag_id = w->id;
    wm_drag_off_x = wm_cursor_x - w->x;
    wm_drag_off_y = wm_cursor_y - w->y;
    wm_mark_dirty_window(w);
}

void wm_set_desktop_id(int id)
{
    wm_desktop_id = id;
}

int wm_poll_mouse_input(void)
{
    if (!vi_ready)
        return 0;

    int changed = 0;
    struct virtio_input_event ev;
    while (virtio_input_next_event(&ev)) {
        if (ev.type == VI_EV_REL) {
            if (wm_input_debug && wm_motion_debug_budget > 0) {
                printf("wm:rel code=%d val=%d\n", (int) ev.code, (int) vi_u32_to_s32(ev.value));
                wm_motion_debug_budget--;
            }
            if (ev.code == VI_REL_X)
                vi_rel_dx += vi_u32_to_s32(ev.value);
            else if (ev.code == VI_REL_Y)
                vi_rel_dy += vi_u32_to_s32(ev.value);
            else if (ev.code == VI_REL_WHEEL)
                vi_wheel += vi_u32_to_s32(ev.value);
            if (vi_rel_dx != 0 || vi_rel_dy != 0) {
                wm_cursor_move(vi_rel_dx, vi_rel_dy);
                vi_rel_dx = 0;
                vi_rel_dy = 0;
                changed = 1;
            }
        } else if (ev.type == VI_EV_ABS) {
            if (wm_input_debug && wm_motion_debug_budget > 0) {
                printf("wm:abs code=%d raw=%u\n", (int) ev.code, (unsigned) ev.value);
                wm_motion_debug_budget--;
            }
            if (ev.code == VI_ABS_X) {
                uint32_t raw = (ev.value > 32767u) ? 32767u : ev.value;
                int nx = (int) ((raw * (uint32_t) (wm_screen_w - 1)) / 32767u);
                int dx = nx - wm_cursor_x;
                if (dx != 0)
                    vi_rel_dx += dx;
            } else if (ev.code == VI_ABS_Y) {
                uint32_t raw = (ev.value > 32767u) ? 32767u : ev.value;
                int ny = (int) ((raw * (uint32_t) (wm_usable_h() - 1)) / 32767u);
                int dy = ny - wm_cursor_y;
                if (dy != 0)
                    vi_rel_dy += dy;
            }
            if (vi_rel_dx != 0 || vi_rel_dy != 0) {
                wm_cursor_move(vi_rel_dx, vi_rel_dy);
                vi_rel_dx = 0;
                vi_rel_dy = 0;
                changed = 1;
            }
        } else if (ev.type == VI_EV_KEY) {
            int down = ev.value ? 1 : 0;
            if (wm_input_debug && down) {
                printf("wm:key down code=%d ctrl=%d shift=%d focus=%d capture=%d\n",
                       (int) ev.code,
                       vi_ctrl_down,
                       vi_shift_down,
                       wm_focus_id,
                       wm_capture_id);
            }
            if (ev.code == VI_KEY_LEFTSHIFT || ev.code == VI_KEY_RIGHTSHIFT) {
                vi_shift_down = down;
                continue;
            }
            if (ev.code == VI_KEY_LEFTCTRL) {
                vi_ctrl_down = down;
                continue;
            }
            if (ev.code == VI_BTN_LEFT) {
                int target = wm_capture_id;
                if (target == 0) {
                    struct wm_window *top = wm_top_at(wm_cursor_x, wm_cursor_y, wm_desktop_id);
                    if (top)
                        target = top->id;
                }
                if (target > 0) {
                    struct wm_window *tw = wm_find(target);
                    if (tw) {
                        wm_queue_event(tw,
                                       WM_EV_POINTER_BUTTON,
                                       (uint32_t) tw->id,
                                       (uint32_t) (down ? 1 : 0),
                                       (uint32_t) wm_cursor_x,
                                       (uint32_t) wm_cursor_y,
                                       0);
                    }
                }
                if (down && !vi_btn_left_down) {
                    wm_drag_begin_from_cursor();
                    changed = 1;
                }
                if (!down && vi_btn_left_down) {
                    wm_drag_end();
                    wm_capture_id = 0;
                    changed = 1;
                }
                vi_btn_left_down = down;
                continue;
            }

            if (!down)
                continue;

            if (vi_ctrl_down && ev.code == VI_KEY_S) {
                int rc = kbd_enqueue_char(19); // Ctrl+S
                if (wm_input_debug)
                    printf("wm:ctrl+s enqueue rc=%d\n", rc);
                continue;
            }
            if (vi_ctrl_down && ev.code == VI_KEY_Q) {
                int rc = kbd_enqueue_char(17); // Ctrl+Q
                if (wm_input_debug)
                    printf("wm:ctrl+q enqueue rc=%d\n", rc);
                continue;
            }
            if (!vi_ctrl_down) {
                if (ev.code == VI_KEY_UP) {
                    (void) kbd_enqueue_char(27);
                    (void) kbd_enqueue_char('[');
                    (void) kbd_enqueue_char('A');
                } else if (ev.code == VI_KEY_DOWN) {
                    (void) kbd_enqueue_char(27);
                    (void) kbd_enqueue_char('[');
                    (void) kbd_enqueue_char('B');
                } else {
                    int kc = wm_vi_key_to_ascii(ev.code, vi_shift_down);
                    if (kc > 0)
                        (void) kbd_enqueue_char((char) kc);
                }
            }

            int target = wm_focus_id;
            if (target <= 0 && wm_capture_id > 0)
                target = wm_capture_id;
            if (target > 0) {
                int ascii = wm_vi_key_to_ascii(ev.code, vi_shift_down);
                struct wm_window *tw = wm_find(target);
                if (tw) {
                    wm_queue_event(tw,
                                   WM_EV_KEY,
                                   (uint32_t) tw->id,
                                   (uint32_t) (ascii > 0 ? ascii : 0),
                                   (uint32_t) ev.code,
                                   (uint32_t) (vi_shift_down ? 1 : 0),
                                   0);
                }
            }
        } else if (ev.type == VI_EV_SYN && ev.code == VI_SYN_REPORT) {
            int target = wm_capture_id;
            if (target == 0) {
                struct wm_window *top = wm_top_at(wm_cursor_x, wm_cursor_y, wm_desktop_id);
                if (top)
                    target = top->id;
            }
            if (vi_rel_dx != 0 || vi_rel_dy != 0) {
                wm_cursor_move(vi_rel_dx, vi_rel_dy);
                if (target > 0) {
                    struct wm_window *tw = wm_find(target);
                    if (tw) {
                        wm_queue_event(tw,
                                       WM_EV_POINTER_MOVE,
                                       (uint32_t) tw->id,
                                       (uint32_t) wm_cursor_x,
                                       (uint32_t) wm_cursor_y,
                                       (uint32_t) vi_rel_dx,
                                       (uint32_t) vi_rel_dy);
                    }
                }
                vi_rel_dx = 0;
                vi_rel_dy = 0;
                changed = 1;
            }
            if (vi_wheel != 0 && target > 0) {
                struct wm_window *tw = wm_find(target);
                if (tw) {
                    wm_queue_event(tw,
                                   WM_EV_WHEEL,
                                   (uint32_t) tw->id,
                                   (uint32_t) vi_wheel,
                                   (uint32_t) wm_cursor_x,
                                   (uint32_t) wm_cursor_y,
                                   0);
                }
                vi_wheel = 0;
                changed = 1;
            }
        }
    }
    return changed;
}

void wm_list(void)
{
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (!wm_windows[i].used)
            continue;
        wm_u_puts("id=");
        wm_u_put_dec(wm_windows[i].id);
        wm_u_puts(" pos=(");
        wm_u_put_dec(wm_windows[i].x);
        wm_u_puts(",");
        wm_u_put_dec(wm_windows[i].y);
        wm_u_puts(") size=(");
        wm_u_put_dec(wm_windows[i].w);
        wm_u_puts(",");
        wm_u_put_dec(wm_windows[i].h);
        wm_u_puts(") title=");
        wm_u_puts(wm_windows[i].title);
        wm_u_puts(" visible=");
        wm_u_put_dec(wm_windows[i].visible);
        wm_u_puts(" focusable=");
        wm_u_put_dec(wm_windows[i].focusable);
        wm_u_puts(" evq=");
        wm_u_put_dec(wm_event_count(&wm_windows[i]));
        wm_u_putchar('\n');
    }
}

void wm_tile(void)
{
    int used = 0;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (wm_windows[i].used)
            used++;
    }
    if (used <= 0)
        return;

    int cols = 1;
    while (cols * cols < used)
        cols++;
    int rows = (used + cols - 1) / cols;
    if (rows < 1)
        rows = 1;

    int cell_w = wm_screen_w / cols;
    int cell_h = wm_usable_h() / rows;
    int min_w = wm_gpu_ready ? WM_MIN_WIN_W_PX : 10;
    int min_h = wm_gpu_ready ? WM_MIN_WIN_H_PX : 5;
    if (cell_w < min_w)
        cell_w = min_w;
    if (cell_h < min_h)
        cell_h = min_h;

    int idx = 0;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (!wm_windows[i].used)
            continue;
        int row = idx / cols;
        int col = idx % cols;
        int x = col * cell_w;
        int y = row * cell_h;
        int w = (col == cols - 1) ? (wm_screen_w - x) : cell_w;
        int h = (row == rows - 1) ? (wm_usable_h() - y) : cell_h;
        wm_resize(wm_windows[i].id, w, h);
        wm_move(wm_windows[i].id, x, y);
        idx++;
    }
}

int wm_input_init(void)
{
    vi_ready = (virtio_input_init() == 0) ? 1 : 0;
    printf("wm: input_ready=%d\n", vi_ready);
    return vi_ready;
}

int wm_set_capture(int id, int on)
{
    if (!on) {
        wm_capture_id = 0;
        return 0;
    }
    struct wm_window *w = wm_find(id);
    if (!w || !w->visible)
        return -1;
    wm_capture_id = id;
    return 0;
}

int wm_get_focus(void)
{
    return wm_focus_id;
}

int wm_get_capture(void)
{
    return wm_capture_id;
}

int wm_poll_event(int window_id, struct wm_event *ev)
{
    struct wm_window *w = wm_find(window_id);
    if (!w || !ev)
        return -1;
    if (w->ev_head == w->ev_tail)
        return 0;
    *ev = w->ev_ring[w->ev_head];
    w->ev_head = (w->ev_head + 1) % WM_EVENT_RING_SIZE;
    if (ev->type == WM_EV_OVERFLOW)
        w->ev_overflow_latched = 0;
    return 1;
}

void wm_dump_state(void)
{
    wm_u_puts("wm state:\n");
    wm_u_puts("  focus=");
    wm_u_put_dec(wm_focus_id);
    wm_u_puts(" capture=");
    wm_u_put_dec(wm_capture_id);
    wm_u_puts(" desktop=");
    wm_u_put_dec(wm_desktop_id);
    wm_u_puts(" input_ready=");
    wm_u_put_dec(vi_ready);
    wm_u_puts(" gpu_ready=");
    wm_u_put_dec(wm_gpu_ready);
    wm_u_putchar('\n');
    wm_list();
}
