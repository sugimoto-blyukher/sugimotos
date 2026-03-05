#pragma once

#include "kernel/syscall.h"
#include "kernel/event.h"
#include "kernel/virtio_input.h"
#include "kernel/wm.h"

#pragma clang section text=".user.text"
#pragma clang section rodata=".user.rodata"
#pragma clang section data=".user.data"
#pragma clang section bss=".user.bss"

#define U_TEXT
#define U_RO
#define U_DATA
#define U_BSS

#ifndef USER_INIT_AUTOSTART_GUI
#define USER_INIT_AUTOSTART_GUI 1
#endif

#define SHELL_MAX_LINE 128
#define SHELL_MAX_ARGS 12
#define HIST_MAX 16
#define HIST_LINE_MAX SHELL_MAX_LINE

#define IMG_BUF_MAX 65536
#define IMG_PIX_MAX_W 320
#define IMG_PIX_MAX_H 240
#define IMG_PIX_MAX (IMG_PIX_MAX_W * IMG_PIX_MAX_H)

#define FM_MAX_ENTRIES 64
#define FM_NAME_MAX 64
#define FM_STATUS_MAX 96
#define FILE_DB_MAX 64

struct shell_history {
    int count;
    int next;
    int browse_pos;
    char entries[HIST_MAX][HIST_LINE_MAX];
    char scratch[HIST_LINE_MAX];
    int scratch_valid;
};

struct file_manager {
    int active;
    int win_id;
    int selected;
    int count;
    int rename_mode;
    int rename_len;
    char names[FM_MAX_ENTRIES][FM_NAME_MAX];
    char preview[384];
    char status[FM_STATUS_MAX];
    char rename_buf[FM_NAME_MAX];
};

extern char g_line[SHELL_MAX_LINE];
extern char g_iobuf[1024];
extern char g_textbuf[WM_TEXT_MAX];
extern uint8_t g_img_buf[IMG_BUF_MAX];
extern uint32_t g_img_pixels[IMG_PIX_MAX];
extern int g_main_win;
extern struct shell_history g_hist;
extern struct file_manager g_fm;
extern char g_file_db[FILE_DB_MAX][FM_NAME_MAX];
extern int g_file_db_count;

int u_syscall0(int nr);
int u_syscall1(int nr, uint32_t arg0);
int u_syscall2(int nr, uint32_t arg0, uint32_t arg1);
int u_syscall3(int nr, uint32_t arg0, uint32_t arg1, uint32_t arg2);
int u_syscall4(int nr, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3);
int u_syscall5(int nr, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);

void u_putchar(char c);
int u_getchar(void);
void u_yield(void);
void u_shutdown(void);
void u_exit(int code);
int u_open(const char *path, int flags);
int u_close(int fd);
int u_read(int fd, void *buf, uint32_t len);
int u_write(int fd, const void *buf, uint32_t len);
int u_unlink(const char *path);
int u_rename(const char *old_path, const char *new_path);
int u_mmap(uint32_t addr_hint, uint32_t len, uint32_t prot, uint32_t flags);
int u_munmap(uint32_t addr, uint32_t len);
int u_event_poll(struct sys_event *ev);
int u_wm_create(const char *title, int w, int h);
int u_wm_set_text(int id, const char *text);
int u_wm_focus(int id);
int u_wm_close(int id);
int u_wm_set_image(int id, const uint32_t *pixels, int w, int h);
int u_wm_poll_mouse(void);
int u_wm_poll_event(int id, struct wm_event *ev);
void u_wm_render(void);

int uwm_input_init(void);
int uwm_poll_mouse_input(void);
void uwm_init(void);
int uwm_create(const char *title, int w, int h);
int uwm_focus(int id);
int uwm_close(int id);
int uwm_set_text(int id, const char *text);
int uwm_set_image(int id, const uint32_t *pixels, int w, int h);
int uwm_poll_event(int window_id, struct wm_event *ev);
void uwm_cursor_move(int dx, int dy);
void uwm_drag_begin_from_cursor(void);
void uwm_drag_end(void);
void uwm_render(void);

void u_puts(const char *s);
int str_len(const char *s);
void str_copy_lim(char *dst, const char *src, int max);
int str_eq(const char *a, const char *b);
char ascii_lower(char c);
int str_ends_with_ci(const char *s, const char *suffix);
int str_to_int(const char *s, int *out);
int split_args(char *line, char **argv, int max_args);
void put_dec(int v);

void history_init(struct shell_history *h);
void history_cancel(struct shell_history *h);
const char *history_prev(struct shell_history *h, const char *current);
const char *history_next(struct shell_history *h);
void history_push(struct shell_history *h, const char *line);

int read_file_all(const char *path, uint8_t *buf, int cap);
void append_char(char c, int *pos);
void append_str(const char *s, int *pos);
void append_dec(int v, int *pos);

int img_open_path(const char *path);
void fm_run(void);
void filedb_add(const char *path);
void filedb_remove(const char *path);
void filedb_rename(const char *old_path, const char *new_path);
