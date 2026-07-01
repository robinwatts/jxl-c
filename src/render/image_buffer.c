// SPDX-License-Identifier: MIT OR Apache-2.0
#include "image_buffer.h"

#include "render/simd/features.h"

#include <string.h>

#if defined(JXL_HAVE_SIMD_AVX2)
void jxl_modular_blit_i16_row_to_plane_avx2(const int16_t *src, float *dst, size_t n, float scale);
#endif

static float modular_bit_depth_scale(uint32_t bit_depth) {
    if (bit_depth >= 31) {
        return 1.0f;
    }
    return 1.0f / (float)((1u << bit_depth) - 1u);
}

void jxl_image_buffer_init_empty(jxl_image_buffer *buf) {
    if (buf != NULL) {
        memset(buf, 0, sizeof(*buf));
    }
}

void jxl_image_buffer_bind_f32(jxl_allocator_state *alloc, jxl_image_buffer *buf, float *data) {
    if (buf == NULL) {
        return;
    }
    jxl_image_buffer_destroy(alloc, buf);
    buf->kind = JXL_IMAGE_BUFFER_F32;
    buf->u.f32.data = data;
    buf->u.f32.owns = 0;
}

void jxl_image_buffer_take_grid(jxl_allocator_state *alloc, jxl_image_buffer *buf,
                                jxl_modular_grid_i32 *src) {
    if (buf == NULL || src == NULL) {
        return;
    }
    jxl_image_buffer_destroy(alloc, buf);
    if (src->kind == JXL_MODULAR_SAMPLE_I16) {
        buf->kind = JXL_IMAGE_BUFFER_I16;
    } else {
        buf->kind = JXL_IMAGE_BUFFER_I32;
    }
    buf->u.grid = *src;
    jxl_modular_grid_i32_init_empty(src);
}

void jxl_image_buffer_destroy(jxl_allocator_state *alloc, jxl_image_buffer *buf) {
    if (buf == NULL) {
        return;
    }
    switch (buf->kind) {
    case JXL_IMAGE_BUFFER_F32:
        if (buf->u.f32.owns && alloc != NULL && buf->u.f32.data != NULL) {
            jxl_free(alloc, buf->u.f32.data);
        }
        break;
    case JXL_IMAGE_BUFFER_I16:
    case JXL_IMAGE_BUFFER_I32:
        jxl_modular_grid_i32_destroy(alloc, &buf->u.grid);
        break;
    }
    jxl_image_buffer_init_empty(buf);
}

size_t jxl_image_buffer_width(const jxl_image_buffer *buf) {
    if (buf == NULL) {
        return 0;
    }
    switch (buf->kind) {
    case JXL_IMAGE_BUFFER_F32:
        return 0;
    case JXL_IMAGE_BUFFER_I16:
    case JXL_IMAGE_BUFFER_I32:
        return buf->u.grid.width;
    }
    return 0;
}

size_t jxl_image_buffer_height(const jxl_image_buffer *buf) {
    if (buf == NULL) {
        return 0;
    }
    switch (buf->kind) {
    case JXL_IMAGE_BUFFER_F32:
        return 0;
    case JXL_IMAGE_BUFFER_I16:
    case JXL_IMAGE_BUFFER_I32:
        return buf->u.grid.height;
    }
    return 0;
}

static void convert_i16_grid_to_f32(const jxl_modular_grid_i32 *g, uint32_t bit_depth_bits,
                                    float *dst, uint32_t dst_stride, uint32_t max_h) {
    size_t row_stride;
    size_t y;
    uint32_t gw;
    uint32_t gh;
    const int16_t *base;

    if (g == NULL || g->buf == NULL || dst == NULL) {
        return;
    }
    jxl_modular_grid_normalize_stride((jxl_modular_grid_i32 *)g);
    base = (const int16_t *)g->buf + g->offset;
    row_stride = jxl_modular_grid_row_stride(g);
    gw = (uint32_t)g->width;
    gh = (uint32_t)g->height;
    if (max_h != 0 && gh > max_h) {
        gh = max_h;
    }
    if (gw > dst_stride) {
        gw = dst_stride;
    }

    if (bit_depth_bits >= 31) {
        for (y = 0; y < (size_t)gh; ++y) {
            size_t x;
            const int16_t *row = base + y * row_stride;
            for (x = 0; x < (size_t)gw; ++x) {
                union {
                    int32_t i;
                    float f;
                } u;
                u.i = (int32_t)row[x];
                dst[y * (size_t)dst_stride + x] = u.f;
            }
        }
        return;
    }

    {
        const float scale = modular_bit_depth_scale(bit_depth_bits);
#if defined(JXL_HAVE_SIMD_AVX2)
        JXL_CPU_FEATURES_LOCAL(feat);
        const int avx2 = feat->avx2 != 0;
#endif
        for (y = 0; y < (size_t)gh; ++y) {
            const int16_t *row = base + y * row_stride;
            float *dst_row = dst + y * (size_t)dst_stride;
#if defined(JXL_HAVE_SIMD_AVX2)
            if (avx2 && gw >= 8) {
                jxl_modular_blit_i16_row_to_plane_avx2(row, dst_row, gw, scale);
                continue;
            }
#endif
            {
                size_t x;
                for (x = 0; x < (size_t)gw; ++x) {
                    dst_row[x] = (float)row[x] * scale;
                }
            }
        }
    }
}

static void convert_i32_grid_to_f32(const jxl_modular_grid_i32 *g, uint32_t bit_depth_bits,
                                    float *dst, uint32_t dst_stride, uint32_t max_h) {
    size_t row_stride;
    size_t y;
    uint32_t gw;
    uint32_t gh;
    const int32_t *base;

    if (g == NULL || g->buf == NULL || dst == NULL) {
        return;
    }
    jxl_modular_grid_normalize_stride((jxl_modular_grid_i32 *)g);
    base = (const int32_t *)g->buf + g->offset;
    row_stride = jxl_modular_grid_row_stride(g);
    gw = (uint32_t)g->width;
    gh = (uint32_t)g->height;
    if (max_h != 0 && gh > max_h) {
        gh = max_h;
    }
    if (gw > dst_stride) {
        gw = dst_stride;
    }

    if (bit_depth_bits >= 31) {
        for (y = 0; y < (size_t)gh; ++y) {
            size_t x;
            const int32_t *row = base + y * row_stride;
            for (x = 0; x < (size_t)gw; ++x) {
                union {
                    int32_t i;
                    float f;
                } u;
                u.i = row[x];
                dst[y * (size_t)dst_stride + x] = u.f;
            }
        }
        return;
    }

    {
        const float scale = modular_bit_depth_scale(bit_depth_bits);
        for (y = 0; y < (size_t)gh; ++y) {
            size_t x;
            const int32_t *row = base + y * row_stride;
            float *dst_row = dst + y * (size_t)dst_stride;
            for (x = 0; x < (size_t)gw; ++x) {
                dst_row[x] = (float)row[x] * scale;
            }
        }
    }
}

jxl_status_t jxl_image_buffer_convert_to_float_modular(jxl_allocator_state *alloc,
                                                       jxl_image_buffer *buf,
                                                       uint32_t bit_depth_bits, float *out_data,
                                                       uint32_t out_stride, uint32_t out_height) {
    if (buf == NULL || out_data == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    if (buf->kind == JXL_IMAGE_BUFFER_F32) {
        buf->u.f32.data = out_data;
        buf->u.f32.owns = 0;
        return JXL_OK;
    }
    if (buf->kind == JXL_IMAGE_BUFFER_I16) {
        convert_i16_grid_to_f32(&buf->u.grid, bit_depth_bits, out_data, out_stride, out_height);
    } else if (buf->kind == JXL_IMAGE_BUFFER_I32) {
        convert_i32_grid_to_f32(&buf->u.grid, bit_depth_bits, out_data, out_stride, out_height);
    } else {
        return JXL_ERROR_INVALID_INPUT;
    }
    jxl_modular_grid_i32_destroy(alloc, &buf->u.grid);
    buf->kind = JXL_IMAGE_BUFFER_F32;
    buf->u.f32.data = out_data;
    buf->u.f32.owns = 0;
    return JXL_OK;
}
