// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/filter/padded_f32.h"

#include <string.h>

static uint32_t region_pad(uint32_t size) {
    return size * 2u;
}

void jxl_filter_pad_params_compute(jxl_filter_pad_params *out,
                                   const jxl_restoration_filter *restoration,
                                   uint32_t frame_width, uint32_t frame_height,
                                   int32_t frame_left, int32_t frame_top) {
    uint32_t region_w;
    uint32_t region_h;
    int32_t region_left;
    int32_t region_top;
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));

    out->frame.frame_left = frame_left;
    out->frame.frame_top = frame_top;
    out->frame.frame_width = frame_width;
    out->frame.frame_height = frame_height;

    if (restoration == NULL) {
        out->buf_width = frame_width;
        out->buf_height = frame_height;
        return;
    }

    if (jxl_epf_enabled(restoration)) {
        if (restoration->epf.iters == 1) {
            out->epf_pad = 2;
        } else if (restoration->epf.iters == 2) {
            out->epf_pad = 5;
        } else if (restoration->epf.iters >= 3) {
            out->epf_pad = 6;
        }
    }
    if (jxl_gabor_enabled(restoration)) {
        out->gab_pad = 1;
    }

    region_w = frame_width;
    region_h = frame_height;
    region_left = frame_left;
    region_top = frame_top;

    if (out->epf_pad > 0) {
        uint32_t p = out->epf_pad;
        region_left = region_left > (int32_t)p ? region_left - (int32_t)p : 0;
        region_top = region_top > (int32_t)p ? region_top - (int32_t)p : 0;
        region_w += region_pad(p);
        region_h += region_pad(p);
    }
    if (out->gab_pad > 0) {
        uint32_t p = out->gab_pad;
        region_left = region_left > (int32_t)p ? region_left - (int32_t)p : 0;
        region_top = region_top > (int32_t)p ? region_top - (int32_t)p : 0;
        region_w += region_pad(p);
        region_h += region_pad(p);
    }

    if (jxl_epf_enabled(restoration)) {
        uint32_t add = 7u;
        uint32_t mask = ~add;
        region_w = (region_w + add) & mask;
        region_h = (region_h + add) & mask;
    }

    out->pad_left = frame_left >= 0 ? (uint32_t)frame_left - (uint32_t)region_left : 0u;
    out->pad_top = frame_top >= 0 ? (uint32_t)frame_top - (uint32_t)region_top : 0u;
    out->buf_width = (size_t)region_w;
    out->buf_height = (size_t)region_h;
    out->pad_right = out->buf_width - (size_t)frame_width - (size_t)out->pad_left;
    out->pad_bottom = out->buf_height - (size_t)frame_height - (size_t)out->pad_top;

    out->frame.frame_left = region_left;
    out->frame.frame_top = region_top;
}

int jxl_padded_f32_alloc(jxl_allocator_state *alloc, size_t buf_width, size_t buf_height,
                         jxl_padded_f32 *out) {
    size_t count;
    if (alloc == NULL || out == NULL || buf_width == 0 || buf_height == 0) {
        return 0;
    }

    count = buf_width * buf_height;
    float *data = jxl_alloc(alloc, count * sizeof(float));
    if (data == NULL) {
        return 0;
    }
    memset(data, 0, count * sizeof(float));

    out->padding = 0;
    out->width = buf_width;
    out->height = buf_height;
    out->stride = buf_width;
    out->data = data;
    return 1;
}

void jxl_padded_f32_free(jxl_allocator_state *alloc, jxl_padded_f32 *buf) {
    if (buf == NULL) {
        return;
    }
    jxl_free(alloc, buf->data);
    buf->data = NULL;
    buf->width = buf->height = buf->stride = buf->padding = 0;
}

int jxl_padded_f32_place(const jxl_subgrid_f32 *src, jxl_padded_f32 *dst, size_t dst_x,
                         size_t dst_y) {
                             size_t y;
    if (src == NULL || src->data == NULL || dst == NULL || dst->data == NULL) {
        return 0;
    }
    if (dst_x + src->width > dst->width || dst_y + src->height > dst->height) {
        return 0;
    }

    for (y = 0; y < src->height; ++y) {
        size_t x;
        for (x = 0; x < src->width; ++x) {
            float v = jxl_subgrid_f32_get(*src, x, y);
            dst->data[(dst_y + y) * dst->stride + (dst_x + x)] = v;
        }
    }
    return 1;
}

void jxl_padded_f32_mirror_trailing(jxl_padded_f32 *buf, size_t content_x, size_t content_y,
                                    size_t content_w, size_t content_h) {
                                        size_t y;
    size_t right;
    size_t bottom;
    if (buf == NULL || buf->data == NULL || content_w == 0 || content_h == 0) {
        return;
    }

    right = content_x + content_w;
    bottom = content_y + content_h;
    if (right > buf->width || bottom > buf->height) {
        return;
    }

    for (y = content_y; y < bottom; ++y) {
        size_t x;
        float edge = buf->data[y * buf->stride + right - 1];
        for (x = right; x < buf->width; ++x) {
            buf->data[y * buf->stride + x] = edge;
        }
    }

    for (y = bottom; y < buf->height; ++y) {
        memcpy(buf->data + y * buf->stride, buf->data + (bottom - 1) * buf->stride,
               buf->stride * sizeof(float));
    }

    for (y = 0; y < content_y; ++y) {
        memcpy(buf->data + y * buf->stride, buf->data + content_y * buf->stride,
               buf->stride * sizeof(float));
    }

    for (y = 0; y < buf->height; ++y) {
        size_t x;
        float *row = buf->data + y * buf->stride;
        float edge = row[content_x];
        for (x = 0; x < content_x; ++x) {
            row[x] = edge;
        }
    }
}

void jxl_subgrid_f32_mirror_trailing(jxl_subgrid_f32 grid, size_t content_x, size_t content_y,
                                     size_t content_w, size_t content_h, size_t buf_w,
                                     size_t buf_h) {
    size_t y;
    size_t right;
    size_t bottom;
    if (grid.data == NULL || content_w == 0 || content_h == 0 || buf_w == 0 || buf_h == 0) {
        return;
    }
    if (content_x + content_w > buf_w || content_y + content_h > buf_h || buf_w > grid.width ||
        buf_h > grid.height) {
        return;
    }

    right = content_x + content_w;
    bottom = content_y + content_h;

    for (y = content_y; y < bottom; ++y) {
        size_t x;
        float edge = jxl_subgrid_f32_get(
            jxl_subgrid_f32_sub(grid, content_x + content_w - 1, y, 1, 1), 0, 0);
        for (x = right; x < buf_w; ++x) {
            jxl_subgrid_f32_set(jxl_subgrid_f32_sub(grid, x, y, 1, 1), 0, 0, edge);
        }
    }

    for (y = bottom; y < buf_h; ++y) {
        size_t x;
        for (x = 0; x < buf_w; ++x) {
            float v = jxl_subgrid_f32_get(jxl_subgrid_f32_sub(grid, x, bottom - 1, 1, 1), 0, 0);
            jxl_subgrid_f32_set(jxl_subgrid_f32_sub(grid, x, y, 1, 1), 0, 0, v);
        }
    }

    for (y = 0; y < content_y; ++y) {
        size_t x;
        for (x = 0; x < buf_w; ++x) {
            float v = jxl_subgrid_f32_get(jxl_subgrid_f32_sub(grid, x, content_y, 1, 1), 0, 0);
            jxl_subgrid_f32_set(jxl_subgrid_f32_sub(grid, x, y, 1, 1), 0, 0, v);
        }
    }

    for (y = 0; y < buf_h; ++y) {
        size_t x;
        float edge = jxl_subgrid_f32_get(jxl_subgrid_f32_sub(grid, content_x, y, 1, 1), 0, 0);
        for (x = 0; x < content_x; ++x) {
            jxl_subgrid_f32_set(jxl_subgrid_f32_sub(grid, x, y, 1, 1), 0, 0, edge);
        }
    }
}

int jxl_padded_f32_copy_region_to(const jxl_padded_f32 *src, size_t src_x, size_t src_y,
                                  jxl_subgrid_f32 dst) {
                                      size_t y;
    if (src == NULL || src->data == NULL || dst.data == NULL) {
        return 0;
    }
    if (src_x + dst.width > src->width || src_y + dst.height > src->height) {
        return 0;
    }

    for (y = 0; y < dst.height; ++y) {
        size_t x;
        for (x = 0; x < dst.width; ++x) {
            float v = src->data[(src_y + y) * src->stride + (src_x + x)];
            jxl_subgrid_f32_set(dst, x, y, v);
        }
    }
    return 1;
}

jxl_subgrid_f32 jxl_padded_f32_subgrid(const jxl_padded_f32 *buf) {
    if (buf == NULL || buf->data == NULL) {
                jxl_subgrid_f32 result = {0};
        return result;

    }
    return jxl_subgrid_f32_from_buf(buf->data, buf->width, buf->height, buf->stride);
}
