#pragma once

#include "kernel/syscall.h"
#include "kernel/event.h"
#include "kernel/virtio_input.h"
#include "kernel/wm.h"
#include "kernel/kernel.h"
#include "../../include/common.h"
#include "kernel/virtio_gpu.h"

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

#define USER_INIT_AUTOSTART_GUI 1

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

// Direct kernel calls replaced the syscall wrappers
#define u_putchar putchar
int u_getchar(void);
#define u_yield yield
#define u_shutdown sbi_shutdown
#define u_exit proc_exit
#define u_open fs_open
#define u_close fs_close
#define u_read fs_read
#define u_write fs_write
#define u_unlink fs_unlink
#define u_listdir fs_listdir
#define u_rename fs_rename
#define u_mmap proc_mmap
#define u_munmap proc_munmap

// Window Manager direct calls
#define u_wm_create wm_create
#define u_wm_set_text wm_set_text
#define u_wm_focus wm_focus
#define u_wm_close wm_close
#define u_wm_set_image wm_set_image
#define u_wm_poll_mouse wm_poll_mouse_input
#define u_wm_poll_event wm_poll_event
#define u_wm_render wm_render

int u_event_poll(struct sys_event *ev);
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

void user_init_entry(void);
