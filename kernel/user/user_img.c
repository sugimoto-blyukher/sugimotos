#include "user_app.h"

#define U_FN static U_TEXT

U_FN int img_next_token(const uint8_t *buf, int len, int *pos, char *tok, int tok_sz)
{
    int p = *pos;
    while (p < len) {
        char c = (char) buf[p];
        if (c == '#') {
            while (p < len && (char) buf[p] != '\n')
                p++;
            continue;
        }
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            p++;
            continue;
        }
        break;
    }
    if (p >= len)
        return -1;

    int n = 0;
    while (p < len) {
        char c = (char) buf[p];
        if (c == '#' || c == ' ' || c == '\n' || c == '\r' || c == '\t')
            break;
        if (n + 1 < tok_sz)
            tok[n++] = c;
        p++;
    }
    tok[n] = '\0';
    *pos = p;
    return 0;
}

U_FN int ppm_decode(const uint8_t *buf, int len, uint32_t *out_pixels, int cap, int *w_out, int *h_out)
{
    int pos = 0;
    char t[32];
    if (img_next_token(buf, len, &pos, t, sizeof(t)) < 0)
        return -1;

    int p6 = (t[0] == 'P' && t[1] == '6' && t[2] == '\0');
    int p3 = (t[0] == 'P' && t[1] == '3' && t[2] == '\0');
    if (!p6 && !p3)
        return -1;

    int w = 0;
    int h = 0;
    int maxv = 0;
    if (img_next_token(buf, len, &pos, t, sizeof(t)) < 0 || str_to_int(t, &w) < 0 || w <= 0)
        return -1;
    if (img_next_token(buf, len, &pos, t, sizeof(t)) < 0 || str_to_int(t, &h) < 0 || h <= 0)
        return -1;
    if (img_next_token(buf, len, &pos, t, sizeof(t)) < 0 || str_to_int(t, &maxv) < 0 || maxv <= 0)
        return -1;

    int need = w * h;
    if (need <= 0 || need > cap)
        return -1;

    while (pos < len) {
        char c = (char) buf[pos];
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
            pos++;
        else
            break;
    }

    if (p6) {
        if (pos + need * 3 > len)
            return -1;
        for (int i = 0; i < need; i++) {
            int r = buf[pos + i * 3 + 0];
            int g = buf[pos + i * 3 + 1];
            int b = buf[pos + i * 3 + 2];
            if (maxv != 255) {
                r = (r * 255) / maxv;
                g = (g * 255) / maxv;
                b = (b * 255) / maxv;
            }
            out_pixels[i] = 0xff000000u | ((uint32_t) r << 16) | ((uint32_t) g << 8) | (uint32_t) b;
        }
    } else {
        for (int i = 0; i < need; i++) {
            int r = 0, g = 0, b = 0;
            if (img_next_token(buf, len, &pos, t, sizeof(t)) < 0 || str_to_int(t, &r) < 0)
                return -1;
            if (img_next_token(buf, len, &pos, t, sizeof(t)) < 0 || str_to_int(t, &g) < 0)
                return -1;
            if (img_next_token(buf, len, &pos, t, sizeof(t)) < 0 || str_to_int(t, &b) < 0)
                return -1;
            if (maxv != 255) {
                r = (r * 255) / maxv;
                g = (g * 255) / maxv;
                b = (b * 255) / maxv;
            }
            if (r < 0) r = 0;
            if (r > 255) r = 255;
            if (g < 0) g = 0;
            if (g > 255) g = 255;
            if (b < 0) b = 0;
            if (b > 255) b = 255;
            out_pixels[i] = 0xff000000u | ((uint32_t) r << 16) | ((uint32_t) g << 8) | (uint32_t) b;
        }
    }

    *w_out = w;
    *h_out = h;
    return 0;
}

U_FN int png_read_size(const uint8_t *buf, int len, int *w, int *h)
{
    if (len < 24)
        return -1;
    if (buf[0] != 0x89 || buf[1] != 'P' || buf[2] != 'N' || buf[3] != 'G')
        return -1;
    if (buf[12] != 'I' || buf[13] != 'H' || buf[14] != 'D' || buf[15] != 'R')
        return -1;
    *w = ((int) buf[16] << 24) | ((int) buf[17] << 16) | ((int) buf[18] << 8) | (int) buf[19];
    *h = ((int) buf[20] << 24) | ((int) buf[21] << 16) | ((int) buf[22] << 8) | (int) buf[23];
    return (*w > 0 && *h > 0) ? 0 : -1;
}

U_FN int jpeg_read_size(const uint8_t *buf, int len, int *w, int *h)
{
    if (len < 4 || buf[0] != 0xff || buf[1] != 0xd8)
        return -1;
    int i = 2;
    while (i + 3 < len) {
        while (i < len && buf[i] != 0xff)
            i++;
        while (i < len && buf[i] == 0xff)
            i++;
        if (i >= len)
            break;
        uint8_t marker = buf[i++];
        if (marker == 0xd8 || marker == 0xd9 || marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7))
            continue;
        if (i + 1 >= len)
            break;
        int seg_len = ((int) buf[i] << 8) | (int) buf[i + 1];
        i += 2;
        if (seg_len < 2 || i + seg_len - 2 > len)
            break;
        if ((marker >= 0xc0 && marker <= 0xc3) ||
            (marker >= 0xc5 && marker <= 0xc7) ||
            (marker >= 0xc9 && marker <= 0xcb) ||
            (marker >= 0xcd && marker <= 0xcf)) {
            if (seg_len >= 7) {
                *h = ((int) buf[i + 1] << 8) | (int) buf[i + 2];
                *w = ((int) buf[i + 3] << 8) | (int) buf[i + 4];
                return (*w > 0 && *h > 0) ? 0 : -1;
            }
        }
        i += seg_len - 2;
    }
    return -1;
}

int img_open_path(const char *path)
{
    int n = read_file_all(path, g_img_buf, sizeof(g_img_buf));
    if (n <= 0) {
        u_puts("img: open/read failed\n");
        return -1;
    }

    int is_ppm = (n >= 2 && g_img_buf[0] == 'P' && (g_img_buf[1] == '6' || g_img_buf[1] == '3'));
    int is_png = (n >= 4 && g_img_buf[0] == 0x89 && g_img_buf[1] == 'P' && g_img_buf[2] == 'N' && g_img_buf[3] == 'G');
    int is_jpg = (n >= 3 && g_img_buf[0] == 0xff && g_img_buf[1] == 0xd8);

    int w = 0;
    int h = 0;
    int pixel_ok = 0;
    if (is_ppm) {
        if (ppm_decode(g_img_buf, n, g_img_pixels, IMG_PIX_MAX, &w, &h) == 0)
            pixel_ok = 1;
    } else if (is_png || str_ends_with_ci(path, ".png")) {
        is_png = 1;
        if (png_read_size(g_img_buf, n, &w, &h) < 0) {
            u_puts("img: png parse failed\n");
            return -1;
        }
    } else if (is_jpg || str_ends_with_ci(path, ".jpg") || str_ends_with_ci(path, ".jpeg")) {
        is_jpg = 1;
        if (jpeg_read_size(g_img_buf, n, &w, &h) < 0) {
            u_puts("img: jpeg parse failed\n");
            return -1;
        }
    } else {
        u_puts("img: supported formats are ppm/png/jpeg\n");
        return -1;
    }

    int id = u_wm_create("image-viewer", 560, 360);
    if (id < 0) {
        u_puts("img: no window slot\n");
        return -1;
    }

    int p = 0;
    g_textbuf[0] = '\0';
    append_str("Image Viewer\nfile: ", &p);
    append_str(path, &p);
    append_str("\nsize: ", &p);
    append_dec(w, &p);
    append_char('x', &p);
    append_dec(h, &p);
    append_char('\n', &p);
    if (is_ppm)
        append_str("format: ppm\n", &p);
    else if (is_png)
        append_str("format: png\n", &p);
    else
        append_str("format: jpeg\n", &p);

    if (pixel_ok)
        append_str("preview: pixel image available\n", &p);
    else if (is_png)
        append_str("preview: metadata only (png decode not implemented in userspace)\n", &p);
    else
        append_str("preview: metadata only\n", &p);

    (void) u_wm_set_text(id, g_textbuf);
    if (pixel_ok)
        (void) u_wm_set_image(id, g_img_pixels, w, h);
    (void) u_wm_focus(id);
    u_wm_render();
    return 0;
}
