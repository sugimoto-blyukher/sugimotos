#include "kernel/image_decode.h"
#include "kernel/syscall.h"
#include "kernel/wm.h"
#include "kernel/wm_apps.h"

extern char __bss[], __bss_end[];

#ifndef SHELL_START_IN_GUI
#define SHELL_START_IN_GUI 0
#endif

static int syscall0(int nr)
{
    register uint32_t a0 __asm__("a0");
    register uint32_t a7 __asm__("a7") = (uint32_t) nr;
    __asm__ __volatile__("ecall" : "=r"(a0) : "r"(a7) : "memory");
    return (int) a0;
}

static int syscall1(int nr, uint32_t arg0)
{
    register uint32_t a0 __asm__("a0") = arg0;
    register uint32_t a7 __asm__("a7") = (uint32_t) nr;
    __asm__ __volatile__("ecall" : "+r"(a0) : "r"(a7) : "memory");
    return (int) a0;
}

static int syscall2(int nr, uint32_t arg0, uint32_t arg1)
{
    register uint32_t a0 __asm__("a0") = arg0;
    register uint32_t a1 __asm__("a1") = arg1;
    register uint32_t a7 __asm__("a7") = (uint32_t) nr;
    __asm__ __volatile__("ecall" : "+r"(a0) : "r"(a1), "r"(a7) : "memory");
    return (int) a0;
}

static int syscall3(int nr, uint32_t arg0, uint32_t arg1, uint32_t arg2)
{
    register uint32_t a0 __asm__("a0") = arg0;
    register uint32_t a1 __asm__("a1") = arg1;
    register uint32_t a2 __asm__("a2") = arg2;
    register uint32_t a7 __asm__("a7") = (uint32_t) nr;
    __asm__ __volatile__("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
    return (int) a0;
}

static void u_putchar(char c)
{
    (void) syscall1(SYS_PUTCHAR, (uint32_t) c);
}

static void u_puts(const char *s)
{
    while (*s)
        u_putchar(*s++);
}

static void u_yield(void)
{
    (void) syscall0(SYS_YIELD);
}

static int u_fork(void)
{
    return syscall0(SYS_FORK);
}

static void u_exit(int status)
{
    (void) syscall1(SYS_EXIT, (uint32_t) status);
    while (1) {}
}

static int u_waitpid(int pid, int *status_ptr)
{
    return syscall3(SYS_WAITPID, (uint32_t) pid, (uint32_t) status_ptr, 0);
}

static int u_waitpid_nohang(int pid, int *status_ptr)
{
    return syscall3(SYS_WAITPID, (uint32_t) pid, (uint32_t) status_ptr, WNOHANG);
}

static int u_getchar(void)
{
    return syscall0(SYS_GETCHAR);
}

static int u_exec(uint32_t entry_pc, char **argv)
{
    return syscall2(SYS_EXEC, entry_pc, (uint32_t) argv);
}

static int u_open(const char *path, int flags)
{
    return syscall2(SYS_OPEN, (uint32_t) path, (uint32_t) flags);
}

static int u_close(int fd)
{
    return syscall1(SYS_CLOSE, (uint32_t) fd);
}

static int u_read(int fd, void *buf, uint32_t len)
{
    return syscall3(SYS_READ, (uint32_t) fd, (uint32_t) buf, len);
}

static int u_write(int fd, const void *buf, uint32_t len)
{
    return syscall3(SYS_WRITE, (uint32_t) fd, (uint32_t) buf, len);
}

static int u_unlink(const char *path)
{
    return syscall1(SYS_UNLINK, (uint32_t) path);
}

static int u_listdir(char *buf, uint32_t len)
{
    return syscall2(SYS_LISTDIR, (uint32_t) buf, len);
}

static int str_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b)
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int str_to_int(const char *s, int *out)
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

static int split_args(char *line, char **argv, int max_args)
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

static uint32_t str_len(const char *s)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
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

static int str_ends_with(const char *s, const char *suffix)
{
    int sl = (int) str_len(s);
    int tl = (int) str_len(suffix);
    if (tl > sl)
        return 0;
    for (int i = 0; i < tl; i++) {
        if (s[sl - tl + i] != suffix[i])
            return 0;
    }
    return 1;
}

struct shell_io;
static void io_putc(struct shell_io *io, char c);
static void io_puts(struct shell_io *io, const char *s);
static void io_put_dec(struct shell_io *io, int value);

#define IMG_BUF_MAX 65536
#define IMG_PREVIEW_W 46
#define IMG_PREVIEW_H 12

static int read_file_all(const char *path, uint8_t *buf, int cap)
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

static int shell_img_command(struct shell_io *io, char **argv, int argc, int in_gui)
{
    if (argc < 2) {
        io_puts(io, "img: usage: img <path>\n");
        return 2;
    }
    static uint8_t img_buf[IMG_BUF_MAX];
    int n = read_file_all(argv[1], img_buf, sizeof(img_buf));
    if (n <= 0) {
        io_puts(io, "img: open/read failed\n");
        return 1;
    }

    int w = 0, h = 0;
    int is_ppm = (n >= 2 && img_buf[0] == 'P' && (img_buf[1] == '6' || img_buf[1] == '3'));
    int is_jpeg = (n >= 3 && img_buf[0] == 0xff && img_buf[1] == 0xd8);
    int is_png = (n >= 4 && img_buf[0] == 0x89 && img_buf[1] == 'P' && img_buf[2] == 'N' && img_buf[3] == 'G');
    char preview[WM_TEXT_MAX];
    preview[0] = '\0';
    int has_pixel_preview = 0;
    int jpeg_thumb_w = 0;
    int jpeg_thumb_h = 0;

    if (is_ppm) {
        if (image_ppm_to_ascii_preview(img_buf,
                                       n,
                                       preview,
                                       sizeof(preview),
                                       IMG_PREVIEW_W,
                                       IMG_PREVIEW_H,
                                       &w,
                                       &h) < 0) {
            io_puts(io, "img: ppm parse failed\n");
            return 1;
        }
        has_pixel_preview = 1;
    } else if (is_jpeg || str_ends_with(argv[1], ".jpg") || str_ends_with(argv[1], ".jpeg")) {
        if (image_jpeg_read_size(img_buf, n, &w, &h) < 0) {
            io_puts(io, "img: jpeg parse failed\n");
            return 1;
        }
        static uint32_t jpeg_thumb_pixels[256 * 256];
        if (image_jpeg_decode_jfif_thumb_xrgb8888(img_buf,
                                                  n,
                                                  jpeg_thumb_pixels,
                                                  (int) (sizeof(jpeg_thumb_pixels) / sizeof(jpeg_thumb_pixels[0])),
                                                  &jpeg_thumb_w,
                                                  &jpeg_thumb_h) == 0)
            has_pixel_preview = 1;
        int tw = 0;
        int th = 0;
        if (image_jpeg_decode_jfif_thumb_ascii(img_buf,
                                               n,
                                               preview,
                                               sizeof(preview),
                                               IMG_PREVIEW_W,
                                               IMG_PREVIEW_H,
                                               &tw,
                                               &th) == 0)
            has_pixel_preview = 1;
        if (jpeg_thumb_w == 0)
            jpeg_thumb_w = tw;
        if (jpeg_thumb_h == 0)
            jpeg_thumb_h = th;
    } else if (is_png || str_ends_with(argv[1], ".png")) {
        if (image_png_read_size(img_buf, n, &w, &h) < 0) {
            io_puts(io, "img: png parse failed\n");
            return 1;
        }
    } else {
        io_puts(io, "img: supported formats are ppm/png/jpeg\n");
        return 2;
    }

    if (!in_gui) {
        io_puts(io, "img: ");
        io_puts(io, argv[1]);
        io_puts(io, " ");
        io_put_dec(io, w);
        io_puts(io, "x");
        io_put_dec(io, h);
        io_putc(io, '\n');
        return 0;
    }

    int id = wm_create("image-viewer", 58, 18);
    if (id < 0) {
        io_puts(io, "img: no window slot\n");
        return 1;
    }
    char text[WM_TEXT_MAX];
    int p = 0;
    const char *hdr = "Image Viewer\nfile: ";
    for (int i = 0; hdr[i] && p + 1 < (int) sizeof(text); i++)
        text[p++] = hdr[i];
    for (int i = 0; argv[1][i] && p + 1 < (int) sizeof(text); i++)
        text[p++] = argv[1][i];
    const char *mid = "\nsize: ";
    for (int i = 0; mid[i] && p + 1 < (int) sizeof(text); i++)
        text[p++] = mid[i];
    char num[16];
    int np = 0;
    int tw = w;
    if (tw == 0) num[np++] = '0';
    while (tw > 0 && np < (int) sizeof(num)) { num[np++] = (char) ('0' + (tw % 10)); tw /= 10; }
    while (np > 0 && p + 1 < (int) sizeof(text)) text[p++] = num[--np];
    if (p + 1 < (int) sizeof(text)) text[p++] = 'x';
    np = 0;
    int th = h;
    if (th == 0) num[np++] = '0';
    while (th > 0 && np < (int) sizeof(num)) { num[np++] = (char) ('0' + (th % 10)); th /= 10; }
    while (np > 0 && p + 1 < (int) sizeof(text)) text[p++] = num[--np];
    const char *kind = is_ppm ? "\nformat: ppm\n" : (is_png ? "\nformat: png\n" : "\nformat: jpeg\n");
    for (int i = 0; kind[i] && p + 1 < (int) sizeof(text); i++)
        text[p++] = kind[i];
    if (is_jpeg && jpeg_thumb_w > 0 && jpeg_thumb_h > 0) {
        const char *label = "thumb: ";
        for (int i = 0; label[i] && p + 1 < (int) sizeof(text); i++)
            text[p++] = label[i];
        np = 0;
        tw = jpeg_thumb_w;
        if (tw == 0) num[np++] = '0';
        while (tw > 0 && np < (int) sizeof(num)) { num[np++] = (char) ('0' + (tw % 10)); tw /= 10; }
        while (np > 0 && p + 1 < (int) sizeof(text)) text[p++] = num[--np];
        if (p + 1 < (int) sizeof(text)) text[p++] = 'x';
        np = 0;
        th = jpeg_thumb_h;
        if (th == 0) num[np++] = '0';
        while (th > 0 && np < (int) sizeof(num)) { num[np++] = (char) ('0' + (th % 10)); th /= 10; }
        while (np > 0 && p + 1 < (int) sizeof(text)) text[p++] = num[--np];
        if (p + 1 < (int) sizeof(text)) text[p++] = '\n';
    }
    if (has_pixel_preview) {
        if (preview[0]) {
            for (int i = 0; preview[i] && p + 1 < (int) sizeof(text); i++)
                text[p++] = preview[i];
        } else if (is_jpeg) {
            const char *msg = "preview: thumbnail pixels decoded (ascii preview unavailable)\n";
            for (int i = 0; msg[i] && p + 1 < (int) sizeof(text); i++)
                text[p++] = msg[i];
        }
    } else if (is_png) {
        const char *msg = "preview: metadata only (png decode not implemented)\n";
        for (int i = 0; msg[i] && p + 1 < (int) sizeof(text); i++)
            text[p++] = msg[i];
    } else {
        const char *msg = "preview: metadata only (jpeg thumbnail not found)\n";
        for (int i = 0; msg[i] && p + 1 < (int) sizeof(text); i++)
            text[p++] = msg[i];
    }
    text[p] = '\0';
    wm_set_text(id, text);
    wm_focus(id);
    wm_render();
    return 0;
}

static int join_args(char **argv, int start, int argc, char *out, int out_sz)
{
    int pos = 0;
    if (out_sz <= 0)
        return -1;
    for (int i = start; i < argc; i++) {
        const char *s = argv[i];
        if (i > start) {
            if (pos + 1 >= out_sz)
                return -1;
            out[pos++] = ' ';
        }
        for (int j = 0; s[j]; j++) {
            if (pos + 1 >= out_sz)
                return -1;
            out[pos++] = s[j];
        }
    }
    out[pos] = '\0';
    return 0;
}

struct shell_io {
    void (*putc)(void *ctx, char c);
    void *ctx;
};

static void serial_putc(void *ctx, char c)
{
    (void) ctx;
    u_putchar(c);
}

static void io_putc(struct shell_io *io, char c)
{
    if (io && io->putc)
        io->putc(io->ctx, c);
}

static void io_puts(struct shell_io *io, const char *s)
{
    while (*s)
        io_putc(io, *s++);
}

static void io_put_dec(struct shell_io *io, int value)
{
    char buf[12];
    int i = 0;
    unsigned magnitude;

    if (value < 0) {
        io_putc(io, '-');
        magnitude = (unsigned) (-value);
    } else {
        magnitude = (unsigned) value;
    }

    do {
        buf[i++] = (char) ('0' + (magnitude % 10));
        magnitude /= 10;
    } while (magnitude > 0 && i < (int) sizeof(buf));

    while (i > 0)
        io_putc(io, buf[--i]);
}

struct shell_state {
    char cwd[16];
    int last_status;
};

enum shell_action {
    SHELL_ACT_CONTINUE = 0,
    SHELL_ACT_EXIT = 1,
    SHELL_ACT_ENTER_GUI = 2,
    SHELL_ACT_LEAVE_GUI = 3,
};

static void shell_help(struct shell_io *io)
{
    io_puts(io, "commands:\n");
    io_puts(io, "  :                   POSIX no-op\n");
    io_puts(io, "  true|false          POSIX status commands\n");
    io_puts(io, "  echo [-n] ...       print text\n");
    io_puts(io, "  pwd                 print current directory\n");
    io_puts(io, "  cd [/]              change current directory (limited)\n");
    io_puts(io, "  wait [pid]          wait for child\n");
    io_puts(io, "  ls                  list files\n");
    io_puts(io, "  cat <file>          show file\n");
    io_puts(io, "  touch <file>        create file\n");
    io_puts(io, "  write <f> <text>    truncate and write\n");
    io_puts(io, "  append <f> <text>   append text\n");
    io_puts(io, "  rm <file>           remove file\n");
    io_puts(io, "  img <file>          inspect image, open viewer in GUI\n");
    io_puts(io, "  wm ...              text window system commands\n");
    io_puts(io, "  gui                 switch to desktop GUI mode\n");
    io_puts(io, "  tui                 leave GUI mode and return to TUI shell\n");
    io_puts(io, "  exit [n]            terminate shell process\n");
    io_puts(io, "  forktest            fork+waitpid+exec test\n");
    io_puts(io, "  help                show this message\n");
}

static char child_arg0[] = "child_exec";
static char child_arg1[] = "42";
static char *child_argv[] = {child_arg0, child_arg1, NULL};

static void child_exec_entry(int argc, char **argv)
{
    u_puts("child:exec_image\n");
    u_puts("child:argc=");
    u_putchar((char) ('0' + (argc % 10)));
    u_putchar('\n');
    if (argc > 1 && argv && argv[1]) {
        u_puts("child:arg1=");
        u_puts(argv[1]);
        u_putchar('\n');
    }
    for (int i = 0; i < 5; i++) {
        u_putchar('E');
        u_yield();
    }
    u_puts("\nchild:exec_exit\n");
    u_exit(99);
}

static void run_forktest(struct shell_io *io)
{
    int pid = u_fork();
    if (pid < 0) {
        io_puts(io, "fork:fail\n");
        return;
    }

    if (pid == 0) {
        u_puts("child:exec\n");
        if (u_exec((uint32_t) child_exec_entry, child_argv) < 0)
            u_exit(2);
        u_exit(3);
    }

    int status = -1;
    int nohang = u_waitpid_nohang(pid, &status);
    if (nohang == 0)
        io_puts(io, "parent:waitpid_wnohang\n");

    int waited = u_waitpid(pid, &status);
    if (waited == pid) {
        io_puts(io, "parent:waitpid_ok\n");
        io_puts(io, "parent:status=");
        io_put_dec(io, status);
        io_putc(io, '\n');
    } else {
        io_puts(io, "parent:waitpid_fail\n");
    }
}

static void shell_ls(struct shell_io *io)
{
    char buf[512];
    int n = u_listdir(buf, sizeof(buf));
    if (n < 0) {
        io_puts(io, "ls: failed\n");
        return;
    }
    if (n == 0) {
        io_puts(io, "(empty)\n");
        return;
    }
    for (int i = 0; i < n; i++)
        io_putc(io, buf[i]);
}

static void shell_cat(struct shell_io *io, const char *path)
{
    int fd = u_open(path, O_RDONLY);
    if (fd < 0) {
        io_puts(io, "cat: open failed\n");
        return;
    }

    char buf[128];
    while (1) {
        int n = u_read(fd, buf, sizeof(buf));
        if (n <= 0)
            break;
        for (int i = 0; i < n; i++)
            io_putc(io, buf[i]);
    }

    u_close(fd);
}

static int shell_write_file(struct shell_io *io, const char *path, char **argv, int argc, int append)
{
    int flags = O_WRONLY | O_CREAT;
    if (append)
        flags |= O_APPEND;
    else
        flags |= O_TRUNC;

    int fd = u_open(path, flags);
    if (fd < 0) {
        io_puts(io, "write: open failed\n");
        return 1;
    }

    for (int i = 2; i < argc; i++) {
        if (i > 2)
            u_write(fd, " ", 1);
        u_write(fd, argv[i], str_len(argv[i]));
    }
    u_write(fd, "\n", 1);
    u_close(fd);
    return 0;
}

static int shell_wm_command(struct shell_io *io, char **argv, int argc)
{
    if (argc < 2 || str_eq(argv[1], "help")) {
        io_puts(io, "wm commands:\n");
        io_puts(io, "  wm init\n");
        io_puts(io, "  wm new <title> [w h]\n");
        io_puts(io, "  wm text <id> <text...>\n");
        io_puts(io, "  wm move <id> <x> <y>\n");
        io_puts(io, "  wm resize <id> <w> <h>\n");
        io_puts(io, "  wm focus <id>\n");
        io_puts(io, "  wm raise <id>\n");
        io_puts(io, "  wm visible <id> <0|1>\n");
        io_puts(io, "  wm close <id>\n");
        io_puts(io, "  wm capture <id|off>\n");
        io_puts(io, "  wm ev <id> [n]\n");
        io_puts(io, "  wm state\n");
        io_puts(io, "  wm launch <about|notes|monitor>\n");
        io_puts(io, "  wm tile\n");
        io_puts(io, "  wm ls\n");
        io_puts(io, "  wm draw\n");
        return 0;
    }

    if (str_eq(argv[1], "init")) {
        wm_init();
        io_puts(io, "wm: reset\n");
        return 0;
    }
    if (str_eq(argv[1], "new")) {
        if (argc < 3) {
            io_puts(io, "wm new: title required\n");
            return 2;
        }
        int w = 30;
        int h = 8;
        if (argc >= 5) {
            if (str_to_int(argv[3], &w) < 0 || str_to_int(argv[4], &h) < 0) {
                io_puts(io, "wm new: invalid size\n");
                return 2;
            }
        }
        int id = wm_create(argv[2], w, h);
        if (id < 0) {
            io_puts(io, "wm new: no slots\n");
            return 1;
        }
        io_puts(io, "wm new id=");
        io_put_dec(io, id);
        io_putc(io, '\n');
        wm_render();
        return 0;
    }
    if (str_eq(argv[1], "text")) {
        if (argc < 4) {
            io_puts(io, "wm text: id and text required\n");
            return 2;
        }
        int id;
        if (str_to_int(argv[2], &id) < 0) {
            io_puts(io, "wm text: invalid id\n");
            return 2;
        }
        char text[WM_TEXT_MAX];
        if (join_args(argv, 3, argc, text, sizeof(text)) < 0) {
            io_puts(io, "wm text: too long\n");
            return 2;
        }
        if (wm_set_text(id, text) < 0) {
            io_puts(io, "wm text: not found\n");
            return 1;
        }
        wm_render();
        return 0;
    }
    if (str_eq(argv[1], "move")) {
        if (argc < 5) {
            io_puts(io, "wm move: id x y required\n");
            return 2;
        }
        int id, x, y;
        if (str_to_int(argv[2], &id) < 0 ||
            str_to_int(argv[3], &x) < 0 ||
            str_to_int(argv[4], &y) < 0) {
            io_puts(io, "wm move: invalid args\n");
            return 2;
        }
        if (wm_move(id, x, y) < 0) {
            io_puts(io, "wm move: not found\n");
            return 1;
        }
        wm_render();
        return 0;
    }
    if (str_eq(argv[1], "resize")) {
        if (argc < 5) {
            io_puts(io, "wm resize: id w h required\n");
            return 2;
        }
        int id, w, h;
        if (str_to_int(argv[2], &id) < 0 ||
            str_to_int(argv[3], &w) < 0 ||
            str_to_int(argv[4], &h) < 0) {
            io_puts(io, "wm resize: invalid args\n");
            return 2;
        }
        if (wm_resize(id, w, h) < 0) {
            io_puts(io, "wm resize: not found\n");
            return 1;
        }
        wm_render();
        return 0;
    }
    if (str_eq(argv[1], "focus")) {
        if (argc < 3) {
            io_puts(io, "wm focus: id required\n");
            return 2;
        }
        int id;
        if (str_to_int(argv[2], &id) < 0) {
            io_puts(io, "wm focus: invalid id\n");
            return 2;
        }
        if (wm_focus(id) < 0) {
            io_puts(io, "wm focus: not found\n");
            return 1;
        }
        wm_render();
        return 0;
    }
    if (str_eq(argv[1], "raise")) {
        if (argc < 3) {
            io_puts(io, "wm raise: id required\n");
            return 2;
        }
        int id;
        if (str_to_int(argv[2], &id) < 0) {
            io_puts(io, "wm raise: invalid id\n");
            return 2;
        }
        if (wm_raise(id) < 0) {
            io_puts(io, "wm raise: not found\n");
            return 1;
        }
        wm_render();
        return 0;
    }
    if (str_eq(argv[1], "visible")) {
        if (argc < 4) {
            io_puts(io, "wm visible: id 0|1 required\n");
            return 2;
        }
        int id;
        int vis;
        if (str_to_int(argv[2], &id) < 0 || str_to_int(argv[3], &vis) < 0) {
            io_puts(io, "wm visible: invalid args\n");
            return 2;
        }
        if (wm_set_visible(id, vis) < 0) {
            io_puts(io, "wm visible: not found\n");
            return 1;
        }
        wm_render();
        return 0;
    }
    if (str_eq(argv[1], "close")) {
        if (argc < 3) {
            io_puts(io, "wm close: id required\n");
            return 2;
        }
        int id;
        if (str_to_int(argv[2], &id) < 0) {
            io_puts(io, "wm close: invalid id\n");
            return 2;
        }
        if (wm_close(id) < 0) {
            io_puts(io, "wm close: not found\n");
            return 1;
        }
        wm_render();
        return 0;
    }
    if (str_eq(argv[1], "capture")) {
        if (argc < 3) {
            io_puts(io, "wm capture: id|off required\n");
            return 2;
        }
        if (str_eq(argv[2], "off")) {
            (void) wm_set_capture(0, 0);
            io_puts(io, "wm capture: off\n");
            return 0;
        }
        int id;
        if (str_to_int(argv[2], &id) < 0) {
            io_puts(io, "wm capture: invalid id\n");
            return 2;
        }
        if (wm_set_capture(id, 1) < 0) {
            io_puts(io, "wm capture: not found\n");
            return 1;
        }
        io_puts(io, "wm capture=");
        io_put_dec(io, wm_get_capture());
        io_putc(io, '\n');
        return 0;
    }
    if (str_eq(argv[1], "ev")) {
        if (argc < 3) {
            io_puts(io, "wm ev: id required\n");
            return 2;
        }
        int id;
        if (str_to_int(argv[2], &id) < 0) {
            io_puts(io, "wm ev: invalid id\n");
            return 2;
        }
        int maxn = 16;
        if (argc >= 4 && str_to_int(argv[3], &maxn) < 0) {
            io_puts(io, "wm ev: invalid n\n");
            return 2;
        }
        if (maxn < 1)
            maxn = 1;
        if (maxn > 64)
            maxn = 64;
        for (int i = 0; i < maxn; i++) {
            struct wm_event ev;
            int rc = wm_poll_event(id, &ev);
            if (rc < 0) {
                io_puts(io, "wm ev: not found\n");
                return 1;
            }
            if (rc == 0)
                break;
            io_puts(io, "ev type=");
            io_put_dec(io, (int) ev.type);
            io_puts(io, " win=");
            io_put_dec(io, (int) ev.window_id);
            io_puts(io, " ts=");
            io_put_dec(io, (int) (ev.timestamp_ms & 0xffffffffu));
            io_puts(io, " a=");
            io_put_dec(io, (int) ev.a);
            io_puts(io, " b=");
            io_put_dec(io, (int) ev.b);
            io_puts(io, " c=");
            io_put_dec(io, (int) ev.c);
            io_puts(io, " d=");
            io_put_dec(io, (int) ev.d);
            io_putc(io, '\n');
        }
        return 0;
    }
    if (str_eq(argv[1], "state")) {
        wm_dump_state();
        return 0;
    }
    if (str_eq(argv[1], "launch")) {
        if (argc < 3) {
            io_puts(io, "wm launch: app required\n");
            return 2;
        }
        int id = wm_launch_builtin(argv[2]);
        if (id == -2) {
            io_puts(io, "wm launch: unknown app\n");
            return 2;
        }
        if (id < 0) {
            io_puts(io, "wm launch: no slots\n");
            return 1;
        }
        io_puts(io, "wm launch id=");
        io_put_dec(io, id);
        io_putc(io, '\n');
        wm_render();
        return 0;
    }
    if (str_eq(argv[1], "tile")) {
        wm_tile();
        wm_render();
        return 0;
    }
    if (str_eq(argv[1], "ls")) {
        wm_list();
        return 0;
    }
    if (str_eq(argv[1], "draw")) {
        wm_render();
        return 0;
    }

    io_puts(io, "wm: unknown subcommand\n");
    return 2;
}

#define GUI_INPUT_MAX 120

struct gui_term {
    int win_id;
    char log[WM_TEXT_MAX];
    int log_len;
    char input[GUI_INPUT_MAX];
    int input_len;
};

static void gui_log_append_char(struct gui_term *gt, char c)
{
    if (gt->log_len + 1 >= (int) sizeof(gt->log)) {
        int cut = 0;
        while (cut < gt->log_len && gt->log[cut] != '\n')
            cut++;
        if (cut < gt->log_len)
            cut++;
        if (cut <= 0)
            cut = gt->log_len / 2;
        int remain = gt->log_len - cut;
        for (int i = 0; i < remain; i++)
            gt->log[i] = gt->log[cut + i];
        gt->log_len = remain;
        if (gt->log_len < 0)
            gt->log_len = 0;
    }
    gt->log[gt->log_len++] = c;
    gt->log[gt->log_len] = '\0';
}

static void gui_putc_cb(void *ctx, char c)
{
    struct gui_term *gt = (struct gui_term *) ctx;
    gui_log_append_char(gt, c);
}

static void gui_log_append_str(struct gui_term *gt, const char *s)
{
    for (int i = 0; s[i]; i++)
        gui_log_append_char(gt, s[i]);
}

static void gui_refresh(struct gui_term *gt)
{
    char text[WM_TEXT_MAX];
    int pos = 0;

    for (int i = 0; i < gt->log_len && pos + 1 < (int) sizeof(text); i++)
        text[pos++] = gt->log[i];

    if (pos + 3 < (int) sizeof(text)) {
        text[pos++] = '\n';
        text[pos++] = '>';
        text[pos++] = ' ';
    }
    for (int i = 0; i < gt->input_len && pos + 1 < (int) sizeof(text); i++)
        text[pos++] = gt->input[i];
    text[pos] = '\0';

    wm_set_text(gt->win_id, text);
    wm_render();
}

static enum shell_action shell_execute_command(struct shell_state *st,
                                               struct shell_io *io,
                                               char **argv,
                                               int argc,
                                               int in_gui,
                                               int *exit_code)
{
    if (str_eq(argv[0], ":")) {
        st->last_status = 0;
    } else if (str_eq(argv[0], "true")) {
        st->last_status = 0;
    } else if (str_eq(argv[0], "false")) {
        st->last_status = 1;
    } else if (str_eq(argv[0], "help")) {
        shell_help(io);
        st->last_status = 0;
    } else if (str_eq(argv[0], "echo")) {
        int start = 1;
        if (argc > 1 && str_eq(argv[1], "-n"))
            start = 2;
        for (int i = start; i < argc; i++) {
            io_puts(io, argv[i]);
            if (i + 1 < argc)
                io_putc(io, ' ');
        }
        if (start == 1)
            io_putc(io, '\n');
        st->last_status = 0;
    } else if (str_eq(argv[0], "pwd")) {
        io_puts(io, st->cwd);
        io_putc(io, '\n');
        st->last_status = 0;
    } else if (str_eq(argv[0], "cd")) {
        const char *target = (argc > 1) ? argv[1] : "/";
        if (str_eq(target, "/") || str_eq(target, ".")) {
            st->cwd[0] = '/';
            st->cwd[1] = '\0';
            st->last_status = 0;
        } else {
            io_puts(io, "cd: unsupported path: ");
            io_puts(io, target);
            io_putc(io, '\n');
            st->last_status = 1;
        }
    } else if (str_eq(argv[0], "wait")) {
        int status = -1;
        int waited = -1;
        if (argc == 1) {
            waited = u_waitpid(-1, &status);
        } else {
            int pid;
            if (str_to_int(argv[1], &pid) < 0) {
                io_puts(io, "wait: invalid pid\n");
                st->last_status = 2;
                return SHELL_ACT_CONTINUE;
            }
            waited = u_waitpid(pid, &status);
        }
        if (waited > 0)
            st->last_status = status;
        else {
            io_puts(io, "wait: no child\n");
            st->last_status = 127;
        }
    } else if (str_eq(argv[0], "ls")) {
        shell_ls(io);
        st->last_status = 0;
    } else if (str_eq(argv[0], "cat")) {
        if (argc < 2) {
            io_puts(io, "cat: file required\n");
            st->last_status = 2;
        } else {
            shell_cat(io, argv[1]);
            st->last_status = 0;
        }
    } else if (str_eq(argv[0], "touch")) {
        if (argc < 2) {
            io_puts(io, "touch: file required\n");
            st->last_status = 2;
        } else {
            int fd = u_open(argv[1], O_CREAT | O_RDWR);
            st->last_status = (fd >= 0) ? 0 : 1;
            if (fd >= 0)
                u_close(fd);
        }
    } else if (str_eq(argv[0], "write")) {
        if (argc < 3) {
            io_puts(io, "write: file and text required\n");
            st->last_status = 2;
        } else {
            st->last_status = shell_write_file(io, argv[1], argv, argc, 0);
        }
    } else if (str_eq(argv[0], "append")) {
        if (argc < 3) {
            io_puts(io, "append: file and text required\n");
            st->last_status = 2;
        } else {
            st->last_status = shell_write_file(io, argv[1], argv, argc, 1);
        }
    } else if (str_eq(argv[0], "rm")) {
        if (argc < 2) {
            io_puts(io, "rm: file required\n");
            st->last_status = 2;
        } else {
            st->last_status = (u_unlink(argv[1]) == 0) ? 0 : 1;
            if (st->last_status != 0)
                io_puts(io, "rm: failed\n");
        }
    } else if (str_eq(argv[0], "img")) {
        st->last_status = shell_img_command(io, argv, argc, in_gui);
    } else if (str_eq(argv[0], "wm")) {
        st->last_status = shell_wm_command(io, argv, argc);
    } else if (str_eq(argv[0], "yield")) {
        u_yield();
        st->last_status = 0;
    } else if (str_eq(argv[0], "forktest")) {
        run_forktest(io);
        st->last_status = 0;
    } else if (str_eq(argv[0], "gui")) {
        if (in_gui) {
            io_puts(io, "gui: already in GUI mode\n");
            st->last_status = 0;
        } else {
            st->last_status = 0;
            return SHELL_ACT_ENTER_GUI;
        }
    } else if (str_eq(argv[0], "tui")) {
        if (in_gui) {
            st->last_status = 0;
            return SHELL_ACT_LEAVE_GUI;
        }
        io_puts(io, "tui: already in TUI mode\n");
        st->last_status = 0;
    } else if (str_eq(argv[0], "exit")) {
        int code = st->last_status;
        if (argc > 1 && str_to_int(argv[1], &code) < 0) {
            io_puts(io, "exit: numeric argument required\n");
            code = 2;
        }
        *exit_code = code;
        return SHELL_ACT_EXIT;
    } else {
        io_puts(io, "unknown command: ");
        io_puts(io, argv[0]);
        io_putc(io, '\n');
        st->last_status = 127;
    }

    return SHELL_ACT_CONTINUE;
}

static int gui_loop(struct shell_state *st, int *exit_code)
{
    struct gui_term gt;
    memset(&gt, 0, sizeof(gt));

    wm_init();
    int desktop = wm_create("desktop", 80, 25);
    int term = wm_create("terminal", 76, 20);
    if (desktop < 0 || term < 0) {
        u_puts("gui: failed to create windows\n");
        return 0;
    }
    wm_move(desktop, 0, 0);
    wm_move(term, 2, 3);
    wm_focus(term);
    wm_set_desktop_id(desktop);

    wm_set_text(desktop,
                "mini desktop | mouse: virtio-input (physical) or Ctrl-W/A/S/D, Ctrl-F, Ctrl-G | "
                "commands: help, img, wm, tui, exit");

    gt.win_id = term;
    gui_log_append_str(&gt, "GUI shell\n");
    gui_log_append_str(&gt, "Ctrl-W/A/S/D: mouse move\n");
    gui_log_append_str(&gt, "Ctrl-F: click (focus/drag)  Ctrl-G: release drag\n");
    gui_log_append_str(&gt, "type 'help' or 'tui'\n");

    struct shell_io io = {.putc = gui_putc_cb, .ctx = &gt};
    gui_refresh(&gt);

    while (1) {
        if (wm_poll_mouse_input())
            gui_refresh(&gt);

        int ch = u_getchar();
        if (ch < 0) {
            u_yield();
            continue;
        }

        if (ch == 23) { // Ctrl-W
            wm_cursor_move(0, -1);
            gui_refresh(&gt);
            continue;
        }
        if (ch == 1) { // Ctrl-A
            wm_cursor_move(-1, 0);
            gui_refresh(&gt);
            continue;
        }
        if (ch == 19) { // Ctrl-S
            wm_cursor_move(0, 1);
            gui_refresh(&gt);
            continue;
        }
        if (ch == 4) { // Ctrl-D
            wm_cursor_move(1, 0);
            gui_refresh(&gt);
            continue;
        }
        if (ch == 6) { // Ctrl-F
            wm_drag_begin_from_cursor();
            gui_refresh(&gt);
            continue;
        }
        if (ch == 7) { // Ctrl-G
            wm_drag_end();
            gui_refresh(&gt);
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            gui_log_append_char(&gt, '>');
            gui_log_append_char(&gt, ' ');
            for (int i = 0; i < gt.input_len; i++)
                gui_log_append_char(&gt, gt.input[i]);
            gui_log_append_char(&gt, '\n');

            gt.input[gt.input_len] = '\0';
            char line[GUI_INPUT_MAX];
            str_copy_lim(line, gt.input, sizeof(line));
            gt.input_len = 0;
            gt.input[0] = '\0';

            char *argv[8];
            int argc = split_args(line, argv, 8);
            if (argc > 0) {
                enum shell_action act = shell_execute_command(st, &io, argv, argc, 1, exit_code);
                if (act == SHELL_ACT_EXIT)
                    return 1;
                if (act == SHELL_ACT_LEAVE_GUI)
                    return 0;
            }
            gui_refresh(&gt);
            continue;
        }

        if (ch == 8 || ch == 127) {
            if (gt.input_len > 0)
                gt.input[--gt.input_len] = '\0';
            gui_refresh(&gt);
            continue;
        }

        if (ch < 32 || ch > 126)
            continue;

        if (gt.input_len + 1 < (int) sizeof(gt.input)) {
            gt.input[gt.input_len++] = (char) ch;
            gt.input[gt.input_len] = '\0';
        }
        gui_refresh(&gt);
        u_yield();
    }
}

static void shell_loop(void)
{
    char line[128];
    struct shell_state st;
    int len = 0;
    int exit_code = 0;
    struct shell_io io = {.putc = serial_putc, .ctx = NULL};

    st.cwd[0] = '/';
    st.cwd[1] = '\0';
    st.last_status = 0;

    if (SHELL_START_IN_GUI) {
        int gui_exit = gui_loop(&st, &exit_code);
        if (gui_exit) {
            u_puts("shell exiting\n");
            u_exit(exit_code);
        }
    }

    u_puts("mini-shell ready. type 'help'\n");
    u_puts("> ");
    while (1) {
        int ch = u_getchar();
        if (ch < 0) {
            u_yield();
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            u_putchar('\n');
            line[len] = '\0';

            char *argv[8];
            int argc = split_args(line, argv, 8);
            if (argc == 0) {
                u_puts("> ");
                len = 0;
                continue;
            }

            enum shell_action act = shell_execute_command(&st, &io, argv, argc, 0, &exit_code);
            if (act == SHELL_ACT_EXIT) {
                u_puts("shell exiting\n");
                u_exit(exit_code);
            }
            if (act == SHELL_ACT_ENTER_GUI) {
                int gui_exit = gui_loop(&st, &exit_code);
                if (gui_exit) {
                    u_puts("shell exiting\n");
                    u_exit(exit_code);
                }
                u_puts("\nreturned to TUI shell\n");
            }
            if (act == SHELL_ACT_LEAVE_GUI)
                u_puts("tui: already in TUI mode\n");

            len = 0;
            u_puts("> ");
            continue;
        }

        if (ch == 8 || ch == 127) {
            if (len > 0) {
                len--;
                u_puts("\b \b");
            }
            continue;
        }

        if (ch < 32 || ch > 126)
            continue;

        if (len + 1 < (int) sizeof(line)) {
            line[len++] = (char) ch;
            u_putchar((char) ch);
        }

        u_yield();
    }
}

static void user_init_entry(void)
{
    u_puts("user:init\n");
    shell_loop();
}

static void idle_entry(void)
{
    while (1) {
        __asm__ __volatile__("wfi");
        yield();
    }
}

void kernel_main(void)
{
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);
    WRITE_CSR(stvec, (uint32_t) kernel_entry);
    fs_init();
    (void) wm_input_init();

    idle_proc = create_process((uint32_t) idle_entry);
    idle_proc->pid = 0;
    current_proc = idle_proc;

    (void) create_user_process((uint32_t) user_init_entry);

    while (1)
        yield();
}
