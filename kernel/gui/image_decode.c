#include "kernel/image_decode.h"

static int image_parse_int(const char *s, int *out)
{
    if (!s || !*s)
        return -1;
    int sign = 1;
    int v = 0;
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

static int img_next_token(const uint8_t *buf, int len, int *pos, char *tok, int tok_sz)
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

int image_ppm_to_ascii_preview(const uint8_t *buf,
                               int len,
                               char *out,
                               int out_sz,
                               int preview_w,
                               int preview_h,
                               int *w_out,
                               int *h_out)
{
    int pos = 0;
    char t[32];
    if (img_next_token(buf, len, &pos, t, sizeof(t)) < 0)
        return -1;
    int p6 = (t[0] == 'P' && t[1] == '6' && t[2] == '\0');
    int p3 = (t[0] == 'P' && t[1] == '3' && t[2] == '\0');
    if (!p6 && !p3)
        return -1;
    if (img_next_token(buf, len, &pos, t, sizeof(t)) < 0 || image_parse_int(t, w_out) < 0)
        return -1;
    if (img_next_token(buf, len, &pos, t, sizeof(t)) < 0 || image_parse_int(t, h_out) < 0)
        return -1;
    int maxv = 0;
    if (img_next_token(buf, len, &pos, t, sizeof(t)) < 0 || image_parse_int(t, &maxv) < 0 || maxv <= 0)
        return -1;

    while (pos < len) {
        char c = (char) buf[pos];
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
            pos++;
        else
            break;
    }

    static const char *ramp = " .:-=+*#%@";
    int ramp_n = 10;
    int out_pos = 0;
    int pw = *w_out;
    int ph = *h_out;
    int sw = (pw < preview_w) ? pw : preview_w;
    int sh = (ph < preview_h) ? ph : preview_h;
    if (sw <= 0 || sh <= 0)
        return -1;

    for (int y = 0; y < sh; y++) {
        int sy = (y * ph) / sh;
        for (int x = 0; x < sw; x++) {
            int sx = (x * pw) / sw;
            int r = 0, g = 0, b = 0;
            if (p6) {
                int idx = pos + (sy * pw + sx) * 3;
                if (idx + 2 >= len)
                    return -1;
                r = buf[idx];
                g = buf[idx + 1];
                b = buf[idx + 2];
                if (maxv != 255) {
                    r = (r * 255) / maxv;
                    g = (g * 255) / maxv;
                    b = (b * 255) / maxv;
                }
            } else {
                int p = pos;
                char tk[32];
                int pix_i = (sy * pw + sx) * 3;
                for (int i = 0; i <= pix_i; i++) {
                    if (img_next_token(buf, len, &p, tk, sizeof(tk)) < 0)
                        return -1;
                }
                if (image_parse_int(tk, &r) < 0)
                    return -1;
                if (img_next_token(buf, len, &p, tk, sizeof(tk)) < 0 || image_parse_int(tk, &g) < 0)
                    return -1;
                if (img_next_token(buf, len, &p, tk, sizeof(tk)) < 0 || image_parse_int(tk, &b) < 0)
                    return -1;
                if (maxv != 255) {
                    r = (r * 255) / maxv;
                    g = (g * 255) / maxv;
                    b = (b * 255) / maxv;
                }
            }
            int luma = (r * 30 + g * 59 + b * 11) / 100;
            int ri = (luma * (ramp_n - 1)) / 255;
            if (out_pos + 2 >= out_sz)
                return -1;
            out[out_pos++] = ramp[ri];
        }
        if (out_pos + 2 >= out_sz)
            return -1;
        out[out_pos++] = '\n';
    }
    out[out_pos] = '\0';
    return 0;
}

int image_jpeg_read_size(const uint8_t *buf, int len, int *w, int *h)
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
        if ((marker >= 0xc0 && marker <= 0xc3) || (marker >= 0xc5 && marker <= 0xc7) ||
            (marker >= 0xc9 && marker <= 0xcb) || (marker >= 0xcd && marker <= 0xcf)) {
            if (seg_len >= 7) {
                *h = ((int) buf[i + 1] << 8) | (int) buf[i + 2];
                *w = ((int) buf[i + 3] << 8) | (int) buf[i + 4];
                return 0;
            }
            return -1;
        }
        i += seg_len - 2;
    }
    return -1;
}

static int image_jpeg_find_jfif_thumb(const uint8_t *buf, int len, const uint8_t **thumb_rgb, int *tw, int *th)
{
    if (!thumb_rgb || !tw || !th)
        return -1;
    *thumb_rgb = NULL;
    *tw = 0;
    *th = 0;
    if (len < 4 || buf[0] != 0xff || buf[1] != 0xd8)
        return -1;

    int i = 2;
    while (i + 4 < len) {
        while (i < len && buf[i] == 0xff)
            i++;
        if (i >= len)
            break;
        uint8_t marker = buf[i++];
        if (marker == 0xd9 || marker == 0xda)
            break;
        if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7))
            continue;
        if (i + 1 >= len)
            break;
        int seg_len = ((int) buf[i] << 8) | (int) buf[i + 1];
        i += 2;
        if (seg_len < 2 || i + seg_len - 2 > len)
            break;

        if (marker == 0xe0 && seg_len >= 16) {
            const uint8_t *p = &buf[i];
            int payload = seg_len - 2;
            if (payload >= 14 && p[0] == 'J' && p[1] == 'F' && p[2] == 'I' && p[3] == 'F' && p[4] == 0) {
                int w = p[12];
                int h = p[13];
                int thumb_bytes = w * h * 3;
                if (w > 0 && h > 0 && payload >= 14 + thumb_bytes) {
                    *thumb_rgb = p + 14;
                    *tw = w;
                    *th = h;
                    return 0;
                }
            }
        }
        i += seg_len - 2;
    }

    return -1;
}

int image_jpeg_decode_jfif_thumb_ascii(const uint8_t *buf,
                                       int len,
                                       char *out,
                                       int out_sz,
                                       int preview_w,
                                       int preview_h,
                                       int *tw_out,
                                       int *th_out)
{
    const uint8_t *thumb = NULL;
    int tw = 0;
    int th = 0;
    if (image_jpeg_find_jfif_thumb(buf, len, &thumb, &tw, &th) < 0)
        return -1;
    *tw_out = tw;
    *th_out = th;
    static const char *ramp = " .:-=+*#%@";
    int ramp_n = 10;
    int sw = (tw < preview_w) ? tw : preview_w;
    int sh = (th < preview_h) ? th : preview_h;
    int pos = 0;
    for (int y = 0; y < sh; y++) {
        int sy = (y * th) / sh;
        for (int x = 0; x < sw; x++) {
            int sx = (x * tw) / sw;
            int idx = (sy * tw + sx) * 3;
            int r = thumb[idx];
            int g = thumb[idx + 1];
            int b = thumb[idx + 2];
            int luma = (r * 30 + g * 59 + b * 11) / 100;
            int ri = (luma * (ramp_n - 1)) / 255;
            if (pos + 2 >= out_sz)
                return -1;
            out[pos++] = ramp[ri];
        }
        if (pos + 2 >= out_sz)
            return -1;
        out[pos++] = '\n';
    }
    out[pos] = '\0';
    return 0;
}

int image_jpeg_decode_jfif_thumb_xrgb8888(const uint8_t *buf,
                                          int len,
                                          uint32_t *out_pixels,
                                          int pixel_count,
                                          int *tw_out,
                                          int *th_out)
{
    const uint8_t *thumb = NULL;
    int tw = 0;
    int th = 0;
    if (image_jpeg_find_jfif_thumb(buf, len, &thumb, &tw, &th) < 0)
        return -1;
    int need = tw * th;
    if (!out_pixels || pixel_count < need)
        return -1;
    for (int i = 0; i < need; i++) {
        uint8_t r = thumb[i * 3 + 0];
        uint8_t g = thumb[i * 3 + 1];
        uint8_t b = thumb[i * 3 + 2];
        out_pixels[i] = (uint32_t) (0xff000000u | ((uint32_t) r << 16) | ((uint32_t) g << 8) | (uint32_t) b);
    }
    if (tw_out)
        *tw_out = tw;
    if (th_out)
        *th_out = th;
    return 0;
}

int image_png_read_size(const uint8_t *buf, int len, int *w, int *h)
{
    if (len < 24)
        return -1;
    if (buf[0] != 0x89 || buf[1] != 'P' || buf[2] != 'N' || buf[3] != 'G')
        return -1;
    *w = ((int) buf[16] << 24) | ((int) buf[17] << 16) | ((int) buf[18] << 8) | (int) buf[19];
    *h = ((int) buf[20] << 24) | ((int) buf[21] << 16) | ((int) buf[22] << 8) | (int) buf[23];
    return 0;
}
