#pragma once

#include "common.h"

#define WM_TEXT_MAX 4096

struct wm_event {
    uint32_t type;
    uint32_t window_id;
    uint64_t timestamp_ms;
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
};

#define WM_EV_FOCUS_IN 1
#define WM_EV_FOCUS_OUT 2
#define WM_EV_POINTER_MOVE 3
#define WM_EV_POINTER_BUTTON 4
#define WM_EV_OVERFLOW 5
#define WM_EV_WHEEL 6
#define WM_EV_KEY 7

int wm_input_init(void);
int wm_poll_mouse_input(void);
void wm_set_desktop_id(int id);

void wm_init(void);
int wm_screen_width(void);
int wm_screen_height(void);
int wm_create(const char *title, int w, int h);
int wm_move(int id, int x, int y);
int wm_resize(int id, int w, int h);
int wm_focus(int id);
int wm_raise(int id);
int wm_close(int id);
int wm_set_visible(int id, int visible);
int wm_set_text(int id, const char *text);
int wm_set_image(int id, const uint32_t *pixels, int w, int h);
int wm_get_rect(int id, int *x, int *y, int *w, int *h);
void wm_render(void);
void wm_list(void);
void wm_tile(void);
void wm_dump_state(void);

void wm_cursor_move(int dx, int dy);
void wm_drag_begin_from_cursor(void);
void wm_drag_end(void);

int wm_set_capture(int id, int on);
int wm_get_focus(void);
int wm_get_capture(void);
int wm_poll_event(int window_id, struct wm_event *ev);
