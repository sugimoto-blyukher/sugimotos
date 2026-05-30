#include "user_app.h"

char g_line[SHELL_MAX_LINE];
char g_iobuf[1024];
char g_textbuf[WM_TEXT_MAX];
uint8_t g_img_buf[IMG_BUF_MAX];
uint32_t g_img_pixels[IMG_PIX_MAX];
int g_main_win;
struct shell_history g_hist;
struct file_manager g_fm;
char g_file_db[FILE_DB_MAX][FM_NAME_MAX];
int g_file_db_count;
char g_term_view[WM_TEXT_MAX];
int g_term_view_len;

char g_input_q[16];
int g_input_q_head;
int g_input_q_tail;
int g_input_shift;

static void input_q_push(char ch)
{
    int next = (g_input_q_tail + 1) % (int) sizeof(g_input_q);
    if (next == g_input_q_head) return;
    g_input_q[g_input_q_tail] = ch;
    g_input_q_tail = next;
}

static int input_q_pop(void)
{
    if (g_input_q_head == g_input_q_tail) return -1;
    int ch = (unsigned char) g_input_q[g_input_q_head];
    g_input_q_head = (g_input_q_head + 1) % (int) sizeof(g_input_q);
    return ch;
}

static char map_key_ascii(uint16_t code, int shift)
{
    switch (code) {
        case VI_KEY_A: return shift ? 'A' : 'a';
        case VI_KEY_B: return shift ? 'B' : 'b';
        case VI_KEY_C: return shift ? 'C' : 'c';
        case VI_KEY_D: return shift ? 'D' : 'd';
        case VI_KEY_E: return shift ? 'E' : 'e';
        case VI_KEY_F: return shift ? 'F' : 'f';
        case VI_KEY_G: return shift ? 'G' : 'g';
        case VI_KEY_H: return shift ? 'H' : 'h';
        case VI_KEY_I: return shift ? 'I' : 'i';
        case VI_KEY_J: return shift ? 'J' : 'j';
        case VI_KEY_K: return shift ? 'K' : 'k';
        case VI_KEY_L: return shift ? 'L' : 'l';
        case VI_KEY_M: return shift ? 'M' : 'm';
        case VI_KEY_N: return shift ? 'N' : 'n';
        case VI_KEY_O: return shift ? 'O' : 'o';
        case VI_KEY_P: return shift ? 'P' : 'p';
        case VI_KEY_Q: return shift ? 'Q' : 'q';
        case VI_KEY_R: return shift ? 'R' : 'r';
        case VI_KEY_S: return shift ? 'S' : 's';
        case VI_KEY_T: return shift ? 'T' : 't';
        case VI_KEY_U: return shift ? 'U' : 'u';
        case VI_KEY_V: return shift ? 'V' : 'v';
        case VI_KEY_W: return shift ? 'W' : 'w';
        case VI_KEY_X: return shift ? 'X' : 'x';
        case VI_KEY_Y: return shift ? 'Y' : 'y';
        case VI_KEY_Z: return shift ? 'Z' : 'z';
        case VI_KEY_1: return shift ? '!' : '1';
        case VI_KEY_2: return shift ? '@' : '2';
        case VI_KEY_3: return shift ? '#' : '3';
        case VI_KEY_4: return shift ? '$' : '4';
        case VI_KEY_5: return shift ? '%' : '5';
        case VI_KEY_6: return shift ? '^' : '6';
        case VI_KEY_7: return shift ? '&' : '7';
        case VI_KEY_8: return shift ? '*' : '8';
        case VI_KEY_9: return shift ? '(' : '9';
        case VI_KEY_0: return shift ? ')' : '0';
        case VI_KEY_MINUS: return shift ? '_' : '-';
        case VI_KEY_EQUAL: return shift ? '+' : '=';
        case VI_KEY_SPACE: return ' ';
        case VI_KEY_TAB: return '\t';
        case VI_KEY_ENTER: return '\n';
        case VI_KEY_BACKSPACE: return '\b';
        case VI_KEY_COMMA: return shift ? '<' : ',';
        case VI_KEY_DOT: return shift ? '>' : '.';
        case VI_KEY_SLASH: return shift ? '?' : '/';
        case VI_KEY_SEMICOLON: return shift ? ':' : ';';
        case VI_KEY_APOSTROPHE: return shift ? '"' : '\'';
        case VI_KEY_LEFTBRACE: return shift ? '{' : '[';
        case VI_KEY_RIGHTBRACE: return shift ? '}' : ']';
        case VI_KEY_BACKSLASH: return shift ? '|' : '\\';
        case VI_KEY_GRAVE: return shift ? '~' : '`';
        default: return 0;
    }
}

int u_getchar(void)
{
    int ch = input_q_pop();
    if (ch >= 0) return ch;

    for (int i = 0; i < 32; i++) {
        struct k_event kev;
        int rc = kevent_pop(&kev);
        if (rc <= 0) break;
        if (kev.type != KEVENT_TYPE_INPUT) continue;

        struct virtio_input_event ev = {
            .type = (uint16_t) kev.a,
            .code = (uint16_t) kev.b,
            .value = kev.c,
        };
        if (ev.type != VI_EV_KEY) continue;
        if (ev.code == VI_KEY_LEFTSHIFT || ev.code == VI_KEY_RIGHTSHIFT) {
            g_input_shift = (ev.value != 0) ? 1 : 0;
            continue;
        }
        if (ev.value == 0) continue;

        if (ev.code == VI_KEY_UP) {
            input_q_push(27); input_q_push('['); input_q_push('A');
            continue;
        }
        if (ev.code == VI_KEY_DOWN) {
            input_q_push(27); input_q_push('['); input_q_push('B');
            continue;
        }
        char a = map_key_ascii(ev.code, g_input_shift);
        if (a) input_q_push(a);
    }
    ch = input_q_pop();
    if (ch >= 0) return ch;
    return getchar();
}

int u_event_poll(struct sys_event *ev)
{
    struct k_event kev;
    int rc = kevent_pop(&kev);
    if (rc <= 0) return rc;
    ev->type = kev.type;
    ev->a = kev.a; ev->b = kev.b; ev->c = kev.c; ev->d = kev.d;
    ev->seq = kev.seq;
    return 1;
}

void u_puts(const char *s) { while (*s) u_putchar(*s++); }

static void term_view_sync(void) { if (g_main_win > 0) u_wm_set_text(g_main_win, g_term_view); }
static void term_view_append_char(char c) {
    if (g_term_view_len + 1 >= WM_TEXT_MAX) return;
    g_term_view[g_term_view_len++] = c;
    g_term_view[g_term_view_len] = '\0';
}
static void term_view_append(const char *s) { if (!s) return; while (*s) term_view_append_char(*s++); }
static void term_view_backspace(void) {
    if (g_term_view_len <= 0) return;
    if (g_term_view[g_term_view_len - 1] == '\n') return;
    g_term_view_len--; g_term_view[g_term_view_len] = '\0';
}
#if USER_INIT_AUTOSTART_GUI
static void term_view_reset(void) {
    g_term_view_len = 0; g_term_view[0] = '\0';
    term_view_append("kernel:shell integrated (S-mode)\n");
    term_view_append("k> ");
    term_view_sync();
}
#endif

void shell_help(void) {
    u_puts("commands: help, ls, cat, touch, write, rm, gui, img, fm, shutdown\n");
}

void shell_touch(const char *path) {
    int fd = u_open(path, O_CREAT | O_RDWR);
    if (fd < 0) {
        u_puts("touch: fail\n");
        return;
    }
    u_close(fd);
    filedb_add(path);
}

void shell_ls(void) {
    int n = u_listdir(g_iobuf, sizeof(g_iobuf));
    if (n < 0) {
        u_puts("ls: fail\n");
        return;
    }

    for (int i = 0; i < n; i++) {
        u_putchar(g_iobuf[i]);
    }
}

void shell_cat(const char *path) {
    int fd = u_open(path, O_RDONLY);
    if (fd < 0) { u_puts("cat: fail\n"); return; }
    while (1) {
        int n = u_read(fd, g_iobuf, sizeof(g_iobuf));
        if (n <= 0) break;
        for (int i = 0; i < n; i++) u_putchar(g_iobuf[i]);
    }
    u_close(fd);
    putchar('\n');
}

void shell_rm(const char *path) {
    if (u_unlink(path) < 0) { u_puts("rm: fail\n"); return; }
    filedb_remove(path);
}

void shell_write(const char *path, int argc, char **argv) {
    int fd = u_open(path, O_CREAT | O_TRUNC | O_WRONLY);
    if (fd < 0) { u_puts("write: fail\n"); return; }
    for (int i = 2; i < argc; i++) {
        if (i > 2)
            u_write(fd, " ", 1);
        u_write(fd, argv[i], str_len(argv[i]));
    }
    u_close(fd);
    filedb_add(path);
}

void shell_fm(void) {
    fm_run();
}

void shell_img(const char *path) {
    if (img_open_path(path) < 0)
        u_puts("img: fail\n");
}

void user_init_entry(void)
{
    u_puts("kernel:starting shell...\n");
    g_input_q_head = 0; g_input_q_tail = 0; g_input_shift = 0;
    printf("shell: calling wm_init...\n");
    wm_init();
    printf("shell: calling wm_input_init...\n");
    wm_input_init();
    g_term_view_len = 0; g_term_view[0] = '\0';
    history_init(&g_hist);
    g_file_db_count = 0;
    //filedb_add("/ext_hello.txt");
    //filedb_add("/ext_note.txt");

#if USER_INIT_AUTOSTART_GUI
    printf("shell: creating terminal window...\n");
    g_main_win = u_wm_create("terminal", 520, 300);
    if (g_main_win > 0) {
        term_view_reset();
        u_wm_focus(g_main_win);
    }
    printf("shell: performing initial render...\n");
    u_wm_render();
#endif

    u_puts("kernel> ");
    int len = 0;
    while (1) {
        int ch = u_getchar();
        if (ch < 0) { u_yield(); continue; }
        if (ch == '\r' || ch == '\n') {
            u_putchar('\n'); term_view_append("\n");
            g_line[len] = '\0';
            char *argv[SHELL_MAX_ARGS];
            int argc = split_args(g_line, argv, SHELL_MAX_ARGS);
            if (argc > 0) {
                if (str_eq(argv[0], "help")) shell_help();
                else if (str_eq(argv[0], "ls")) shell_ls();
                else if (str_eq(argv[0], "cat")) { if (argc > 1) shell_cat(argv[1]); else u_puts("usage: cat <path>\n"); }
                else if (str_eq(argv[0], "touch")) { if (argc > 1) shell_touch(argv[1]); else u_puts("usage: touch <path>\n"); }
                else if (str_eq(argv[0], "gui")) {
                    if (g_main_win <= 0) g_main_win = u_wm_create("terminal", 520, 300);
                    u_wm_render();
                }
                else if (str_eq(argv[0], "write")) {
                    if (argc > 2) shell_write(argv[1], argc, argv);
                    else u_puts("usage: write <path> <text>\n");
                }
                else if (str_eq(argv[0], "img")) { if (argc > 1) shell_img(argv[1]); else u_puts("usage: img <path>\n"); }
                else if (str_eq(argv[0], "fm")) shell_fm();
                else if (str_eq(argv[0], "rm")) { if (argc > 1) shell_rm(argv[1]); else u_puts("usage: rm <path>\n"); }
                else if (str_eq(argv[0], "shutdown")) u_shutdown();
                else u_puts("unknown command\n");
            }
            len = 0; u_puts("k> "); term_view_append("k> "); term_view_sync();
            continue;
        }
        if (ch == 8 || ch == 127) {
            if (len > 0) { len--; u_puts("\b \b"); term_view_backspace(); term_view_sync(); }
            continue;
        }
        if (ch >= 32 && ch <= 126) {
            if (len + 1 < SHELL_MAX_LINE) {
                g_line[len++] = (char) ch; u_putchar((char) ch);
                term_view_append_char((char) ch); term_view_sync();
            }
        }
    }
}
