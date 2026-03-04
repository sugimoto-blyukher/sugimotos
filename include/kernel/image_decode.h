#pragma once

#include "common.h"

int image_ppm_to_ascii_preview(const uint8_t *buf,
                               int len,
                               char *out,
                               int out_sz,
                               int preview_w,
                               int preview_h,
                               int *w_out,
                               int *h_out);

int image_jpeg_read_size(const uint8_t *buf, int len, int *w, int *h);

int image_jpeg_decode_jfif_thumb_ascii(const uint8_t *buf,
                                       int len,
                                       char *out,
                                       int out_sz,
                                       int preview_w,
                                       int preview_h,
                                       int *tw_out,
                                       int *th_out);

int image_jpeg_decode_jfif_thumb_xrgb8888(const uint8_t *buf,
                                          int len,
                                          uint32_t *out_pixels,
                                          int pixel_count,
                                          int *tw_out,
                                          int *th_out);

int image_png_read_size(const uint8_t *buf, int len, int *w, int *h);
