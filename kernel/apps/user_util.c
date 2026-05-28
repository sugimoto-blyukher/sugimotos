#include "user_app.h"

int str_len(const char *s) {
    int len = 0;
    while (*s++) len++;
    return len;
}

void str_copy_lim(char *dst, const char *src, int max) {
    if (max <= 0) return;
    int i = 0;
    while (i < max - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

int str_eq(const char *a, const char *b) {
    if (!a || !b) return a == b;
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

char ascii_lower(char c) {
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

int str_ends_with_ci(const char *s, const char *suffix) {
    int slen = str_len(s);
    int sulen = str_len(suffix);
    if (sulen > slen) return 0;
    s += (slen - sulen);
    while (*s) {
        if (ascii_lower(*s) != ascii_lower(*suffix)) return 0;
        s++; suffix++;
    }
    return 1;
}

int str_to_int(const char *s, int *out) {
    int val = 0;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    if (!*s) return -1;
    while (*s) {
        if (*s < '0' || *s > '9') return -1;
        val = val * 10 + (*s - '0');
        s++;
    }
    *out = val * sign;
    return 0;
}

int split_args(char *line, char **argv, int max_args) {
    int argc = 0;
    while (*line && argc < max_args) {
        while (*line && (*line == ' ' || *line == '\t' || *line == '\n' || *line == '\r')) {
            *line++ = '\0';
        }
        if (!*line) break;
        argv[argc++] = line;
        while (*line && !(*line == ' ' || *line == '\t' || *line == '\n' || *line == '\r')) {
            line++;
        }
    }
    return argc;
}

void put_dec(int v) {
    if (v < 0) { u_putchar('-'); v = -v; }
    if (v == 0) { u_putchar('0'); return; }
    char buf[16];
    int i = 0;
    while (v > 0) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0) u_putchar(buf[--i]);
}

void history_init(struct shell_history *h) {
    h->count = 0;
    h->next = 0;
    h->browse_pos = -1;
    h->scratch_valid = 0;
}

void history_cancel(struct shell_history *h) {
    h->browse_pos = -1;
    h->scratch_valid = 0;
}

const char *history_prev(struct shell_history *h, const char *current) {
    if (h->count == 0) return NULL;
    if (h->browse_pos < 0) {
        str_copy_lim(h->scratch, current, HIST_LINE_MAX);
        h->scratch_valid = 1;
        h->browse_pos = (h->next + h->count - 1) % HIST_MAX;
    } else {
        int oldest = h->count == HIST_MAX ? h->next : 0;
        if (h->browse_pos == oldest) return h->entries[h->browse_pos];
        h->browse_pos = (h->browse_pos + HIST_MAX - 1) % HIST_MAX;
    }
    return h->entries[h->browse_pos];
}

const char *history_next(struct shell_history *h) {
    if (h->browse_pos < 0) return NULL;
    int last = (h->next + h->count - 1) % HIST_MAX;
    if (h->browse_pos == last) {
        h->browse_pos = -1;
        return h->scratch_valid ? h->scratch : "";
    }
    h->browse_pos = (h->browse_pos + 1) % HIST_MAX;
    return h->entries[h->browse_pos];
}

void history_push(struct shell_history *h, const char *line) {
    if (!line || !*line) return;
    int prev = (h->next + h->count - 1) % HIST_MAX;
    if (h->count > 0 && str_eq(h->entries[prev], line)) return;
    str_copy_lim(h->entries[h->next], line, HIST_LINE_MAX);
    h->next = (h->next + 1) % HIST_MAX;
    if (h->count < HIST_MAX) h->count++;
    h->browse_pos = -1;
    h->scratch_valid = 0;
}

int read_file_all(const char *path, uint8_t *buf, int cap) {
    int fd = u_open(path, O_RDONLY);
    if (fd < 0) return -1;
    int total = 0;
    while (total < cap) {
        int n = u_read(fd, (char *)buf + total, cap - total);
        if (n <= 0) break;
        total += n;
    }
    u_close(fd);
    return total;
}

void append_char(char c, int *pos) {
    if (*pos < WM_TEXT_MAX - 1) {
        g_textbuf[(*pos)++] = c;
        g_textbuf[*pos] = '\0';
    }
}

void append_str(const char *s, int *pos) {
    while (*s) append_char(*s++, pos);
}

void append_dec(int v, int *pos) {
    if (v < 0) { append_char('-', pos); v = -v; }
    if (v == 0) { append_char('0', pos); return; }
    char buf[16];
    int i = 0;
    unsigned int uv = (unsigned int)v;
    while (uv > 0) {
        buf[i++] = (char)('0' + (uv % 10));
        uv /= 10;
    }
    while (i > 0) append_char(buf[--i], pos);
}
