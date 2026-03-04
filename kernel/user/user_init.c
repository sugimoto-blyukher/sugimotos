#include "user_app.h"

#define U_FN static U_TEXT

U_BSS char g_line[SHELL_MAX_LINE];
U_BSS char g_iobuf[1024];
U_BSS char g_textbuf[WM_TEXT_MAX];
U_BSS uint8_t g_img_buf[IMG_BUF_MAX];
U_BSS uint32_t g_img_pixels[IMG_PIX_MAX];
U_BSS int g_main_win;
U_BSS struct shell_history g_hist;
U_BSS struct file_manager g_fm;
U_BSS char g_file_db[FILE_DB_MAX][FM_NAME_MAX];
U_BSS int g_file_db_count;

int u_syscall0(int nr)
{
    int ret;
    __asm__ __volatile__(
        "mv a7, %1\n"
        "li a0, 0\n"
        "ecall\n"
        "mv %0, a0\n"
        : "=r"(ret)
        : "r"(nr)
        : "a0", "a7", "memory");
    return ret;
}

int u_syscall1(int nr, uint32_t arg0)
{
    int ret;
    __asm__ __volatile__(
        "mv a7, %2\n"
        "mv a0, %1\n"
        "ecall\n"
        "mv %0, a0\n"
        : "=r"(ret)
        : "r"(arg0), "r"(nr)
        : "a0", "a7", "memory");
    return ret;
}

int u_syscall2(int nr, uint32_t arg0, uint32_t arg1)
{
    int ret;
    __asm__ __volatile__(
        "mv a7, %3\n"
        "mv a0, %1\n"
        "mv a1, %2\n"
        "ecall\n"
        "mv %0, a0\n"
        : "=r"(ret)
        : "r"(arg0), "r"(arg1), "r"(nr)
        : "a0", "a1", "a7", "memory");
    return ret;
}

int u_syscall3(int nr, uint32_t arg0, uint32_t arg1, uint32_t arg2)
{
    int ret;
    __asm__ __volatile__(
        "mv a7, %4\n"
        "mv a0, %1\n"
        "mv a1, %2\n"
        "mv a2, %3\n"
        "ecall\n"
        "mv %0, a0\n"
        : "=r"(ret)
        : "r"(arg0), "r"(arg1), "r"(arg2), "r"(nr)
        : "a0", "a1", "a2", "a7", "memory");
    return ret;
}

int u_syscall4(int nr, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3)
{
    int ret;
    __asm__ __volatile__(
        "mv a7, %5\n"
        "mv a0, %1\n"
        "mv a1, %2\n"
        "mv a2, %3\n"
        "mv a3, %4\n"
        "ecall\n"
        "mv %0, a0\n"
        : "=r"(ret)
        : "r"(arg0), "r"(arg1), "r"(arg2), "r"(arg3), "r"(nr)
        : "a0", "a1", "a2", "a3", "a7", "memory");
    return ret;
}

int u_syscall5(int nr, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    int ret;
    __asm__ __volatile__(
        "mv a7, %6\n"
        "mv a0, %1\n"
        "mv a1, %2\n"
        "mv a2, %3\n"
        "mv a3, %4\n"
        "mv a4, %5\n"
        "ecall\n"
        "mv %0, a0\n"
        : "=r"(ret)
        : "r"(arg0), "r"(arg1), "r"(arg2), "r"(arg3), "r"(arg4), "r"(nr)
        : "a0", "a1", "a2", "a3", "a4", "a7", "memory");
    return ret;
}

void u_putchar(char c) { (void) u_syscall1(SYS_PUTCHAR, (uint32_t) c); }
int u_getchar(void) { return u_syscall0(SYS_GETCHAR); }
void u_yield(void) { (void) u_syscall0(SYS_YIELD); }
void u_shutdown(void) { (void) u_syscall0(SYS_SHUTDOWN); }
void u_exit(int code)
{
    (void) u_syscall1(SYS_EXIT, (uint32_t) code);
    while (1) {}
}

int u_open(const char *path, int flags) { return u_syscall2(SYS_OPEN, (uint32_t) path, (uint32_t) flags); }
int u_close(int fd) { return u_syscall1(SYS_CLOSE, (uint32_t) fd); }
int u_read(int fd, void *buf, uint32_t len) { return u_syscall3(SYS_READ, (uint32_t) fd, (uint32_t) buf, len); }
int u_write(int fd, const void *buf, uint32_t len) { return u_syscall3(SYS_WRITE, (uint32_t) fd, (uint32_t) buf, len); }
int u_unlink(const char *path) { return u_syscall1(SYS_UNLINK, (uint32_t) path); }
int u_rename(const char *old_path, const char *new_path)
{
    return u_syscall2(SYS_RENAME, (uint32_t) old_path, (uint32_t) new_path);
}

int u_wm_create(const char *title, int w, int h)
{
    return u_syscall4(SYS_WMCTL, WMCTL_CREATE, (uint32_t) title, (uint32_t) w, (uint32_t) h);
}
int u_wm_set_text(int id, const char *text)
{
    return u_syscall4(SYS_WMCTL, WMCTL_SET_TEXT, (uint32_t) id, (uint32_t) text, 0);
}
int u_wm_focus(int id)
{
    return u_syscall4(SYS_WMCTL, WMCTL_FOCUS, (uint32_t) id, 0, 0);
}
int u_wm_close(int id)
{
    return u_syscall4(SYS_WMCTL, WMCTL_CLOSE, (uint32_t) id, 0, 0);
}
int u_wm_set_image(int id, const uint32_t *pixels, int w, int h)
{
    return u_syscall5(SYS_WMCTL, WMCTL_SET_IMAGE, (uint32_t) id, (uint32_t) pixels, (uint32_t) w, (uint32_t) h);
}
int u_wm_poll_mouse(void)
{
    return u_syscall4(SYS_WMCTL, WMCTL_POLL_MOUSE, 0, 0, 0);
}
int u_wm_poll_event(int id, struct wm_event *ev)
{
    return u_syscall4(SYS_WMCTL, WMCTL_POLL_EVENT, (uint32_t) id, (uint32_t) ev, 0);
}
void u_wm_render(void)
{
    (void) u_syscall4(SYS_WMCTL, WMCTL_RENDER, 0, 0, 0);
}

void u_puts(const char *s)
{
    while (*s)
        u_putchar(*s++);
}

int str_len(const char *s)
{
    int n = 0;
    while (s && s[n])
        n++;
    return n;
}

void str_copy_lim(char *dst, const char *src, int max)
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

int str_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b)
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

char ascii_lower(char c)
{
    if (c >= 'A' && c <= 'Z')
        return (char) (c + ('a' - 'A'));
    return c;
}

int str_ends_with_ci(const char *s, const char *suffix)
{
    int sl = str_len(s);
    int tl = str_len(suffix);
    if (tl > sl)
        return 0;
    for (int i = 0; i < tl; i++) {
        if (ascii_lower(s[sl - tl + i]) != ascii_lower(suffix[i]))
            return 0;
    }
    return 1;
}

int str_to_int(const char *s, int *out)
{
    int sign = 1;
    int v = 0;
    if (!s || !*s)
        return -1;
    if (*s == '-') {
        sign = -1;
        s++;
    }
    if (!*s)
        return -1;
    while (*s) {
        if (*s < '0' || *s > '9')
            return -1;
        v = v * 10 + (*s - '0');
        s++;
    }
    *out = sign * v;
    return 0;
}

int split_args(char *line, char **argv, int max_args)
{
    int argc = 0;
    char *p = line;
    while (*p && argc < max_args) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t')
            p++;
        if (!*p)
            break;
        *p++ = '\0';
    }
    return argc;
}

void put_dec(int v)
{
    char tmp[16];
    int n = 0;
    if (v == 0) {
        u_putchar('0');
        return;
    }
    if (v < 0) {
        u_putchar('-');
        v = -v;
    }
    while (v > 0 && n < (int) sizeof(tmp)) {
        tmp[n++] = (char) ('0' + (v % 10));
        v /= 10;
    }
    while (n > 0)
        u_putchar(tmp[--n]);
}

void history_init(struct shell_history *h)
{
    for (int i = 0; i < HIST_MAX; i++)
        h->entries[i][0] = '\0';
    h->count = 0;
    h->next = 0;
    h->browse_pos = -1;
    h->scratch[0] = '\0';
    h->scratch_valid = 0;
}

void history_cancel(struct shell_history *h)
{
    h->browse_pos = -1;
    h->scratch_valid = 0;
    h->scratch[0] = '\0';
}

U_FN const char *history_latest(const struct shell_history *h)
{
    if (h->count <= 0)
        return NULL;
    int idx = (h->next - 1 + HIST_MAX) % HIST_MAX;
    return h->entries[idx];
}

void history_push(struct shell_history *h, const char *line)
{
    if (!line || !line[0]) {
        history_cancel(h);
        return;
    }
    const char *last = history_latest(h);
    if (last && str_eq(last, line)) {
        history_cancel(h);
        return;
    }
    str_copy_lim(h->entries[h->next], line, HIST_LINE_MAX);
    h->next = (h->next + 1) % HIST_MAX;
    if (h->count < HIST_MAX)
        h->count++;
    history_cancel(h);
}

const char *history_prev(struct shell_history *h, const char *current)
{
    if (h->count <= 0)
        return NULL;
    if (h->browse_pos < 0) {
        h->browse_pos = h->count;
        str_copy_lim(h->scratch, current ? current : "", HIST_LINE_MAX);
        h->scratch_valid = 1;
    }
    if (h->browse_pos > 0)
        h->browse_pos--;
    int idx = (h->next - h->count + h->browse_pos + HIST_MAX * 2) % HIST_MAX;
    return h->entries[idx];
}

const char *history_next(struct shell_history *h)
{
    if (h->count <= 0 || h->browse_pos < 0)
        return NULL;
    if (h->browse_pos + 1 >= h->count) {
        h->browse_pos = -1;
        return h->scratch_valid ? h->scratch : "";
    }
    h->browse_pos++;
    int idx = (h->next - h->count + h->browse_pos + HIST_MAX * 2) % HIST_MAX;
    return h->entries[idx];
}

int read_file_all(const char *path, uint8_t *buf, int cap)
{
    int fd = u_open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    int total = 0;
    while (total < cap) {
        int n = u_read(fd, buf + total, (uint32_t) (cap - total));
        if (n <= 0)
            break;
        total += n;
    }
    u_close(fd);
    return total;
}

void append_char(char c, int *pos)
{
    if (*pos + 1 >= WM_TEXT_MAX)
        return;
    g_textbuf[(*pos)++] = c;
    g_textbuf[*pos] = '\0';
}

void append_str(const char *s, int *pos)
{
    for (int i = 0; s[i]; i++)
        append_char(s[i], pos);
}

void append_dec(int v, int *pos)
{
    char tmp[16];
    int n = 0;
    if (v == 0) {
        append_char('0', pos);
        return;
    }
    if (v < 0) {
        append_char('-', pos);
        v = -v;
    }
    while (v > 0 && n < (int) sizeof(tmp)) {
        tmp[n++] = (char) ('0' + (v % 10));
        v /= 10;
    }
    while (n > 0)
        append_char(tmp[--n], pos);
}

U_FN void shell_help(void)
{
    u_puts("commands:\n");
    u_puts("  help                  show this message\n");
    u_puts("  ls                    list files\n");
    u_puts("  cat <file>            show file contents\n");
    u_puts("  touch <file>          create empty file\n");
    u_puts("  write <f> <text...>   truncate and write\n");
    u_puts("  append <f> <text...>  append text\n");
    u_puts("  rm <file>             remove file\n");
    u_puts("  rename <o> <n>        rename file\n");
    u_puts("  gui                   open main gui window\n");
    u_puts("  img <path>            open image viewer\n");
    u_puts("  fm                    open file manager (GUI)\n");
    u_puts("  close                 close main gui window\n");
    u_puts("  shutdown              power off\n");
    u_puts("  exit                  exit process\n");
    u_puts("  Up/Down               command history prev/next\n");
}

U_FN void shell_ls(void)
{
    if (g_file_db_count <= 0) {
        u_puts("(empty)\n");
        return;
    }
    for (int i = 0; i < g_file_db_count; i++) {
        u_puts(g_file_db[i]);
        u_putchar('\n');
    }
}

U_FN void shell_cat(const char *path)
{
    int fd = u_open(path, O_RDONLY);
    if (fd < 0) {
        u_puts("cat: open failed\n");
        return;
    }
    while (1) {
        int n = u_read(fd, g_iobuf, sizeof(g_iobuf));
        if (n <= 0)
            break;
        for (int i = 0; i < n; i++)
            u_putchar(g_iobuf[i]);
    }
    u_close(fd);
}

U_FN int shell_write_file(const char *path, char **argv, int argc, int append)
{
    int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
    int fd = u_open(path, flags);
    if (fd < 0) {
        u_puts("write: open failed\n");
        return -1;
    }
    for (int i = 2; i < argc; i++) {
        if (i > 2)
            (void) u_write(fd, " ", 1);
        (void) u_write(fd, argv[i], (uint32_t) str_len(argv[i]));
    }
    (void) u_write(fd, "\n", 1);
    u_close(fd);
    return 0;
}

U_RO const char u_banner[] = "user:init apps in userspace (sv32)\n";
U_RO const char u_prompt[] = "u> ";
U_RO const char u_bs[] = "\b \b";
U_RO const char u_gui_title[] = "user-gui";
U_RO const char u_gui_text[] =
    "User-space shell GUI window\n"
    "apps: img/fm, file ops, shutdown\n"
    "all app logic runs in user space\n";
U_RO const char u_cmd_help[] = "help";
U_RO const char u_cmd_ls[] = "ls";
U_RO const char u_cmd_cat[] = "cat";
U_RO const char u_cmd_touch[] = "touch";
U_RO const char u_cmd_write[] = "write";
U_RO const char u_cmd_append[] = "append";
U_RO const char u_cmd_rm[] = "rm";
U_RO const char u_cmd_rename[] = "rename";
U_RO const char u_cmd_img[] = "img";
U_RO const char u_cmd_fm[] = "fm";
U_RO const char u_cmd_gui[] = "gui";
U_RO const char u_cmd_close[] = "close";
U_RO const char u_cmd_shutdown[] = "shutdown";
U_RO const char u_cmd_exit[] = "exit";
U_RO const char u_cmd_status[] = "status";
U_RO const char u_cmd_int[] = "int";

U_TEXT void user_init_entry(void)
{
    u_puts(u_banner);
    history_init(&g_hist);
    g_file_db_count = 0;
    filedb_add("/ext_hello.txt");
    filedb_add("/ext_note.txt");
    filedb_add("/test.png");

#if USER_INIT_AUTOSTART_GUI
    if (g_main_win <= 0)
        g_main_win = u_wm_create(u_gui_title, 520, 300);
    if (g_main_win > 0) {
        (void) u_wm_set_text(g_main_win, u_gui_text);
        (void) u_wm_focus(g_main_win);
        u_wm_render();
    }
#endif

    u_puts(u_prompt);
    int len = 0;

    while (1) {
        int ch = u_getchar();
        if (ch < 0) {
            u_yield();
            continue;
        }

        if (ch == 27) {
            int n1 = u_getchar();
            int n2 = u_getchar();
            if (n1 == '[' && n2 == 'A')
                ch = -2;
            else if (n1 == '[' && n2 == 'B')
                ch = -3;
            else
                continue;
        }

        if (ch == -2) {
            g_line[len] = '\0';
            const char *entry = history_prev(&g_hist, g_line);
            if (entry) {
                while (len > 0) {
                    u_puts(u_bs);
                    len--;
                }
                int i = 0;
                while (entry[i] && i + 1 < SHELL_MAX_LINE) {
                    g_line[i] = entry[i];
                    u_putchar(entry[i]);
                    i++;
                }
                g_line[i] = '\0';
                len = i;
            }
            continue;
        }

        if (ch == -3) {
            const char *entry = history_next(&g_hist);
            if (entry) {
                while (len > 0) {
                    u_puts(u_bs);
                    len--;
                }
                int i = 0;
                while (entry[i] && i + 1 < SHELL_MAX_LINE) {
                    g_line[i] = entry[i];
                    u_putchar(entry[i]);
                    i++;
                }
                g_line[i] = '\0';
                len = i;
            }
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            u_putchar('\n');
            g_line[len] = '\0';
            history_cancel(&g_hist);

            char *argv[SHELL_MAX_ARGS];
            int argc = split_args(g_line, argv, SHELL_MAX_ARGS);
            if (argc > 0) {
                history_push(&g_hist, g_line);

                if (str_eq(argv[0], u_cmd_help)) {
                    shell_help();
                } else if (str_eq(argv[0], u_cmd_ls)) {
                    shell_ls();
                } else if (str_eq(argv[0], u_cmd_cat)) {
                    if (argc < 2) u_puts("cat: usage: cat <file>\n");
                    else shell_cat(argv[1]);
                } else if (str_eq(argv[0], u_cmd_touch)) {
                    if (argc < 2) {
                        u_puts("touch: usage: touch <file>\n");
                    } else {
                        int fd = u_open(argv[1], O_WRONLY | O_CREAT);
                        if (fd < 0) u_puts("touch: failed\n");
                        else {
                            u_close(fd);
                            filedb_add(argv[1]);
                        }
                    }
                } else if (str_eq(argv[0], u_cmd_write)) {
                    if (argc < 3) u_puts("write: usage: write <file> <text...>\n");
                    else if (shell_write_file(argv[1], argv, argc, 0) == 0) filedb_add(argv[1]);
                } else if (str_eq(argv[0], u_cmd_append)) {
                    if (argc < 3) u_puts("append: usage: append <file> <text...>\n");
                    else if (shell_write_file(argv[1], argv, argc, 1) == 0) filedb_add(argv[1]);
                } else if (str_eq(argv[0], u_cmd_rm)) {
                    if (argc < 2) u_puts("rm: usage: rm <file>\n");
                    else if (u_unlink(argv[1]) < 0) u_puts("rm: failed\n");
                    else filedb_remove(argv[1]);
                } else if (str_eq(argv[0], u_cmd_rename)) {
                    if (argc < 3) u_puts("rename: usage: rename <old> <new>\n");
                    else if (u_rename(argv[1], argv[2]) < 0) u_puts("rename: failed\n");
                    else filedb_rename(argv[1], argv[2]);
                } else if (str_eq(argv[0], u_cmd_img)) {
                    if (argc < 2) u_puts("img: usage: img <path>\n");
                    else (void) img_open_path(argv[1]);
                } else if (str_eq(argv[0], u_cmd_fm)) {
                    fm_run();
                } else if (str_eq(argv[0], u_cmd_gui)) {
                    if (g_main_win <= 0)
                        g_main_win = u_wm_create(u_gui_title, 520, 300);
                    if (g_main_win > 0) {
                        (void) u_wm_set_text(g_main_win, u_gui_text);
                        (void) u_wm_focus(g_main_win);
                        u_wm_render();
                        u_puts("gui: opened\n");
                    } else {
                        u_puts("gui: failed\n");
                    }
                } else if (str_eq(argv[0], u_cmd_close)) {
                    if (g_main_win > 0 && u_wm_close(g_main_win) == 0) {
                        g_main_win = 0;
                        u_wm_render();
                        u_puts("close: ok\n");
                    } else {
                        u_puts("close: no window\n");
                    }
                } else if (str_eq(argv[0], u_cmd_shutdown)) {
                    u_shutdown();
                } else if (str_eq(argv[0], u_cmd_exit)) {
                    u_exit(0);
                } else if (str_eq(argv[0], u_cmd_status)) {
                    u_puts("pid/user shell alive\n");
                } else if (str_eq(argv[0], u_cmd_int)) {
                    if (argc < 2) u_puts("int: usage int <n>\n");
                    else {
                        int v;
                        if (str_to_int(argv[1], &v) == 0) {
                            put_dec(v);
                            u_putchar('\n');
                        } else {
                            u_puts("int: invalid\n");
                        }
                    }
                } else {
                    u_puts("unknown\n");
                }
            }

            len = 0;
            u_puts(u_prompt);
            continue;
        }

        if (ch == 8 || ch == 127) {
            if (len > 0) {
                len--;
                u_puts(u_bs);
            }
            history_cancel(&g_hist);
            continue;
        }

        if (ch < 32 || ch > 126)
            continue;
        if (len + 1 < SHELL_MAX_LINE) {
            g_line[len++] = (char) ch;
            u_putchar((char) ch);
        }
        history_cancel(&g_hist);
    }
}
