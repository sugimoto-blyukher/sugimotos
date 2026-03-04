#include "kernel/syscall.h"
#include "kernel/virtio_gpu.h"
#include "kernel/virtio_input.h"
#include "kernel/wm.h"

#define WM_SCREEN_W 80
#define WM_SCREEN_H 25
#define WM_MAX_WINDOWS 8
#define WM_TITLE_MAX 24
#define WM_FG_LIGHT 97
#define WM_FG_DARK 30
#define WM_BG_DESKTOP 44
#define WM_BG_WINDOW 47
#define WM_BG_TITLE 46
#define WM_BG_TITLE_ACTIVE 45
#define WM_BG_TASKBAR 100
#define WM_EVENT_RING_SIZE 64

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
    struct wm_event ev_ring[WM_EVENT_RING_SIZE];
    int ev_head;
    int ev_tail;
    int ev_overflow_latched;
};

static struct wm_window wm_windows[WM_MAX_WINDOWS];
static int wm_next_id = 1;
static int wm_next_z = 1;
static struct wm_cell wm_fb[WM_SCREEN_H][WM_SCREEN_W];
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

#define WM_DRAG_NONE 0
#define WM_DRAG_MOVE 1
#define WM_DRAG_RESIZE 2

static int wm_usable_h(void)
{
    return WM_SCREEN_H - 1;
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

static void str_copy_lim(char *dst, const char *src, int max)
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
    for (int y = 0; y < WM_SCREEN_H; y++) {
        for (int x = 0; x < WM_SCREEN_W; x++) {
            wm_fb[y][x].ch = ' ';
            wm_fb[y][x].fg = WM_FG_LIGHT;
            wm_fb[y][x].bg = WM_BG_DESKTOP;
        }
    }
}

static void wm_plot_color(int x, int y, char c, int fg, int bg)
{
    if (x < 0 || y < 0 || x >= WM_SCREEN_W || y >= WM_SCREEN_H)
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

void wm_init(void)
{
    memset(wm_windows, 0, sizeof(wm_windows));
    wm_next_id = 1;
    wm_next_z = 1;
    wm_cursor_x = WM_SCREEN_W / 2;
    wm_cursor_y = wm_usable_h() / 2;
    wm_cursor_visible = 1;
    wm_drag_id = 0;
    wm_drag_mode = WM_DRAG_NONE;
    wm_desktop_id = 0;
    wm_focus_id = 0;
    wm_capture_id = 0;
    wm_event_tick = 1;
    wm_gpu_ready = (virtio_gpu_init() == 0) ? 1 : 0;
    vi_rel_dx = 0;
    vi_rel_dy = 0;
    vi_wheel = 0;
    vi_btn_left_down = 0;
}

int wm_create(const char *title, int w, int h)
{
    if (w < 10)
        w = 10;
    if (h < 5)
        h = 5;
    if (w > WM_SCREEN_W)
        w = WM_SCREEN_W;
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
    win->x = (win->id * 3) % (WM_SCREEN_W - w + 1);
    win->y = (win->id * 2) % (wm_usable_h() - h + 1);
    win->z = wm_next_z++;
    str_copy_lim(win->title, title ? title : "window", WM_TITLE_MAX);
    str_copy_lim(win->text, "(empty)", WM_TEXT_MAX);
    return win->id;
}

int wm_move(int id, int x, int y)
{
    struct wm_window *win = wm_find(id);
    if (!win)
        return -1;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x + win->w > WM_SCREEN_W)
        x = WM_SCREEN_W - win->w;
    if (y + win->h > wm_usable_h())
        y = wm_usable_h() - win->h;
    win->x = x;
    win->y = y;
    return 0;
}

int wm_resize(int id, int w, int h)
{
    struct wm_window *win = wm_find(id);
    if (!win)
        return -1;
    if (w < 10)
        w = 10;
    if (h < 5)
        h = 5;
    if (w > WM_SCREEN_W)
        w = WM_SCREEN_W;
    if (h > wm_usable_h())
        h = wm_usable_h();
    win->w = w;
    win->h = h;
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
    return 0;
}

int wm_close(int id)
{
    struct wm_window *win = wm_find(id);
    if (!win)
        return -1;
    if (wm_focus_id == id)
        (void) wm_set_focus_internal(0);
    if (wm_capture_id == id)
        wm_capture_id = 0;
    if (wm_drag_id == id)
        wm_drag_end();
    memset(win, 0, sizeof(*win));
    return 0;
}

int wm_set_visible(int id, int visible)
{
    struct wm_window *win = wm_find(id);
    if (!win)
        return -1;
    win->visible = visible ? 1 : 0;
    if (!win->visible) {
        if (wm_focus_id == id)
            (void) wm_set_focus_internal(0);
        if (wm_capture_id == id)
            wm_capture_id = 0;
        if (wm_drag_id == id)
            wm_drag_end();
    }
    return 0;
}

int wm_set_text(int id, const char *text)
{
    struct wm_window *win = wm_find(id);
    if (!win)
        return -1;
    str_copy_lim(win->text, text, WM_TEXT_MAX);
    return 0;
}

static void wm_draw_taskbar(void)
{
    int y = WM_SCREEN_H - 1;
    for (int x = 0; x < WM_SCREEN_W; x++)
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
    for (int i = 0; prefix[i] && pos < WM_SCREEN_W; i++)
        wm_plot_color(pos++, y, prefix[i], WM_FG_DARK, WM_BG_TASKBAR);

    if (top_id > 0 && pos < WM_SCREEN_W - 1) {
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
        for (int i = 0; i < n && pos < WM_SCREEN_W; i++)
            wm_plot_color(pos++, y, idbuf[i], WM_FG_DARK, WM_BG_TASKBAR);
    }
}

static void wm_apply_cursor_overlay(void)
{
    if (!wm_cursor_visible)
        return;
    if (wm_cursor_x < 0 || wm_cursor_y < 0 || wm_cursor_x >= WM_SCREEN_W || wm_cursor_y >= WM_SCREEN_H)
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

static uint32_t wm_ansi_to_rgb(int ansi)
{
    switch (ansi) {
        case 30: return 0x00101010u;
        case 31: return 0x00cc3333u;
        case 32: return 0x0033cc33u;
        case 33: return 0x00cccc33u;
        case 34: return 0x003366ccu;
        case 35: return 0x00cc33ccu;
        case 36: return 0x0033ccccu;
        case 37: return 0x00e0e0e0u;
        case 40: return 0x00101010u;
        case 41: return 0x00cc3333u;
        case 42: return 0x0033cc33u;
        case 43: return 0x00cccc33u;
        case 44: return 0x002a4b8du;
        case 45: return 0x008d4b7au;
        case 46: return 0x004b8d8du;
        case 47: return 0x00e0e0e0u;
        case 97: return 0x00ffffffu;
        case 100: return 0x004a4a4au;
        default:
            return 0x00202020u;
    }
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
    if (w <= 0 || h <= 0)
        return;
    for (int yy = 0; yy < h; yy++) {
        uint32_t *row = fb + (y + yy) * fb_w + x;
        for (int xx = 0; xx < w; xx++)
            row[xx] = 0xff000000u | color;
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
    int cell_w = fb_w / WM_SCREEN_W;
    int cell_h = fb_h / WM_SCREEN_H;
    if (cell_w < 1)
        cell_w = 1;
    if (cell_h < 1)
        cell_h = 1;

    wm_gpu_fill_rect(fb, fb_w, fb_h, 0, 0, fb_w, fb_h, 0x00101010u);

    for (int y = 0; y < WM_SCREEN_H; y++) {
        for (int x = 0; x < WM_SCREEN_W; x++) {
            struct wm_cell *c = &wm_fb[y][x];
            int px = x * cell_w;
            int py = y * cell_h;
            uint32_t bg = wm_ansi_to_rgb(c->bg);
            wm_gpu_fill_rect(fb, fb_w, fb_h, px, py, cell_w, cell_h, bg);
            if (c->ch != ' ') {
                uint32_t fg = wm_ansi_to_rgb(c->fg);
                int gw = cell_w - 2;
                int gh = cell_h - 2;
                if (gw < 2)
                    gw = cell_w;
                if (gh < 2)
                    gh = cell_h;
                wm_gpu_fill_rect(fb, fb_w, fb_h, px + 1, py + 1, gw, gh, fg);
            }
        }
    }

    virtio_gpu_present();
}

void wm_render(void)
{
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
    int maxz = 1;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (wm_windows[i].used && wm_windows[i].visible && wm_windows[i].z > maxz)
            maxz = wm_windows[i].z;
    }
    wm_next_z = maxz + 1;
    wm_draw_taskbar();
    wm_apply_cursor_overlay();

    if (wm_gpu_ready) {
        wm_render_gpu();
        return;
    }

    wm_u_puts("\x1b[2J\x1b[H\x1b[?25l");
    int cur_fg = -1;
    int cur_bg = -1;
    for (int y = 0; y < WM_SCREEN_H; y++) {
        for (int x = 0; x < WM_SCREEN_W; x++) {
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
}

void wm_cursor_move(int dx, int dy)
{
    wm_cursor_x += dx;
    wm_cursor_y += dy;
    if (wm_cursor_x < 0)
        wm_cursor_x = 0;
    if (wm_cursor_y < 0)
        wm_cursor_y = 0;
    if (wm_cursor_x >= WM_SCREEN_W)
        wm_cursor_x = WM_SCREEN_W - 1;
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
}

void wm_drag_end(void)
{
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
    wm_focus(w->id);
    wm_capture_id = w->id;
    int on_title = (wm_cursor_y == w->y);
    int on_corner = (wm_cursor_x >= w->x + w->w - 2 && wm_cursor_y >= w->y + w->h - 2);
    if (on_corner) {
        wm_drag_mode = WM_DRAG_RESIZE;
        wm_drag_id = w->id;
        wm_drag_off_x = wm_cursor_x;
        wm_drag_off_y = wm_cursor_y;
        wm_drag_start_w = w->w;
        wm_drag_start_h = w->h;
        return;
    }
    if (on_title) {
        wm_drag_mode = WM_DRAG_MOVE;
        wm_drag_id = w->id;
        wm_drag_off_x = wm_cursor_x - w->x;
        wm_drag_off_y = wm_cursor_y - w->y;
        return;
    }
    wm_drag_end();
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
            if (ev.code == VI_REL_X)
                vi_rel_dx += vi_u32_to_s32(ev.value);
            else if (ev.code == VI_REL_Y)
                vi_rel_dy += vi_u32_to_s32(ev.value);
            else if (ev.code == VI_REL_WHEEL)
                vi_wheel += vi_u32_to_s32(ev.value);
        } else if (ev.type == VI_EV_ABS) {
            if (ev.code == VI_ABS_X) {
                int nx = (int) (((uint64_t) (ev.value > 32767 ? 32767 : ev.value)) * (WM_SCREEN_W - 1) / 32767);
                int dx = nx - wm_cursor_x;
                if (dx != 0)
                    vi_rel_dx += dx;
            } else if (ev.code == VI_ABS_Y) {
                int ny = (int) (((uint64_t) (ev.value > 32767 ? 32767 : ev.value)) * (wm_usable_h() - 1) / 32767);
                int dy = ny - wm_cursor_y;
                if (dy != 0)
                    vi_rel_dy += dy;
            }
        } else if (ev.type == VI_EV_KEY && ev.code == VI_BTN_LEFT) {
            int down = ev.value ? 1 : 0;
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

    int cell_w = WM_SCREEN_W / cols;
    int cell_h = wm_usable_h() / rows;
    if (cell_w < 10)
        cell_w = 10;
    if (cell_h < 5)
        cell_h = 5;

    int idx = 0;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (!wm_windows[i].used)
            continue;
        int row = idx / cols;
        int col = idx % cols;
        int x = col * cell_w;
        int y = row * cell_h;
        int w = (col == cols - 1) ? (WM_SCREEN_W - x) : cell_w;
        int h = (row == rows - 1) ? (wm_usable_h() - y) : cell_h;
        wm_resize(wm_windows[i].id, w, h);
        wm_move(wm_windows[i].id, x, y);
        idx++;
    }
}

int wm_input_init(void)
{
    vi_ready = (virtio_input_init() == 0) ? 1 : 0;
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
