// SPDX-License-Identifier: MIT OR Apache-2.0
#include "ycbcr.h"

#include "render/filter/ycbcr_internal.h"
#include "render/simd/features.h"

#include <stddef.h>
#include <string.h>

static const float k_ycbcr_y_offset = 128.0f / 255.0f;
static const float k_ycbcr_r_cr = 1.402f;
static const float k_ycbcr_g_cb = -0.114f * 1.772f / 0.587f;
static const float k_ycbcr_g_cr = -0.299f * 1.402f / 0.587f;
static const float k_ycbcr_b_cb = 1.772f;

static void interpolate(float left, float center, float right, float *out_left,
                        float *out_right) {
    *out_left = 0.25f * left + 0.75f * center;
    *out_right = 0.75f * center + 0.25f * right;
}

void jxl_modular_float_normalize_plane(float *data, size_t count, uint32_t bit_depth_bits) {
    size_t i;
    float div;
    if (data == NULL || count == 0 || bit_depth_bits >= 31) {
        return;
    }
    div = bit_depth_bits > 0 ? (float)((1u << bit_depth_bits) - 1u) : 1.0f;
    for (i = 0; i < count; ++i) {
        data[i] /= div;
    }
}

void jxl_ycbcr_to_rgb_base(float *cb, float *y, float *cr, size_t count) {
    size_t i;
    if (cb == NULL || y == NULL || cr == NULL || count == 0) {
        return;
    }
    for (i = 0; i < count; ++i) {
        const float cb_v = cb[i];
        const float y_v = y[i] + k_ycbcr_y_offset;
        const float cr_v = cr[i];
        cb[i] = cr_v * k_ycbcr_r_cr + y_v;
        y[i] = cb_v * k_ycbcr_g_cb + cr_v * k_ycbcr_g_cr + y_v;
        cr[i] = cb_v * k_ycbcr_b_cb + y_v;
    }
}

void jxl_ycbcr_to_rgb(jxl_context *ctx, float *cb, float *y, float *cr, size_t count) {
    const jxl_cpu_features *feat;
    if (cb == NULL || y == NULL || cr == NULL || count == 0) {
        return;
    }
    feat = jxl_context_cpu_features(ctx);

#if defined(JXL_HAVE_SIMD_NEON)
    if (feat->neon) {
            jxl_ycbcr_to_rgb_neon(cb, y, cr, count);
            return;
        
    }
#endif
#if defined(JXL_HAVE_SIMD_AVX2)
    if (feat->fma && feat->sse41) {
            if (feat->avx2) {
                jxl_ycbcr_to_rgb_x86_avx2(cb, y, cr, count);
            } else {
                jxl_ycbcr_to_rgb_x86_fma(cb, y, cr, count);
            }
            return;
        
    }
#endif
#if defined(JXL_HAVE_SIMD_SSE41)
    jxl_ycbcr_to_rgb_x86_sse2(cb, y, cr, count);
    return;
#endif
    jxl_ycbcr_to_rgb_base(cb, y, cr, count);
}

static void upsample_row_horizontal(const float *row, size_t src_width, float *out_row,
                                    size_t target_width) {
                                        size_t i;
    size_t out_x;
    float prev_sample;
    if (src_width == 0) {
        memset(out_row, 0, target_width * sizeof(float));
        return;
    }

    prev_sample = row[0];
    out_x = 0;
    for (i = 0; i + 1 < src_width; ++i) {
        float curr = row[i];
        float next = row[i + 1];
        float left = 0.0f;
        float right = 0.0f;
        interpolate(prev_sample, curr, next, &left, &right);
        if (out_x < target_width) {
            out_row[out_x++] = left;
        }
        if (out_x < target_width) {
            out_row[out_x++] = right;
        }
        prev_sample = curr;
    }

    {
        float curr = row[src_width - 1];
        float next = curr;
        float left = 0.0f;
        float right = 0.0f;
        interpolate(prev_sample, curr, next, &left, &right);
        if (out_x < target_width) {
            out_row[out_x++] = left;
        }
        if (out_x < target_width) {
            out_row[out_x++] = right;
        }
    }
}

int jxl_apply_jpeg_upsampling_single(jxl_allocator_state *alloc, jxl_const_subgrid_f32 src,
                                     jxl_channel_shift shift, uint32_t target_w, uint32_t target_h,
                                     float *dst, size_t dst_stride) {
                                         size_t y;
    size_t height;
    size_t target_width;
    size_t target_height;
    int h_upsampled;
    int v_upsampled;
    float *buf;
    if (alloc == NULL || dst == NULL || src.data == NULL || target_w == 0 || target_h == 0) {
        return 0;
    }

    height = src.height;
    target_width = (size_t)target_w;
    target_height = (size_t)target_h;
    h_upsampled = jxl_channel_shift_hshift(&shift) == 0;
    v_upsampled = jxl_channel_shift_vshift(&shift) == 0;

    buf = jxl_alloc(alloc, target_width * target_height * sizeof(float));
    if (buf == NULL) {
        return 0;
    }

    for (y = 0; y < height; ++y) {
        const float *row = src.data + y * src.stride;
        float *out_row = buf + y * target_width;
        if (h_upsampled) {
            size_t copy_w = target_width < src.width ? target_width : src.width;
            memcpy(out_row, row, copy_w * sizeof(float));
            if (copy_w < target_width) {
                size_t x;
                float last = copy_w > 0 ? out_row[copy_w - 1] : 0.0f;
                for (x = copy_w; x < target_width; ++x) {
                    out_row[x] = last;
                }
            }
        } else {
            upsample_row_horizontal(row, src.width, out_row, target_width);
        }
    }

    if (!v_upsampled) {
        size_t y;
        float *prev_row = jxl_alloc(alloc, target_width * sizeof(float));
        if (prev_row == NULL) {
            jxl_free(alloc, buf);
            return 0;
        }
        if (height > 0) {
            memcpy(prev_row, buf + (height - 1) * target_width, target_width * sizeof(float));
        }

        for (y = height; y > 0; --y) {
            size_t x;
            size_t row = y - 1;
            size_t idx_base = row * target_width;
            size_t top_base = row > 0 ? idx_base - target_width : idx_base;
            for (x = 0; x < target_width; ++x) {
                float curr_sample = buf[idx_base + x];
                float top_neighbor = buf[top_base + x];
                float bottom = 0.0f;
                float top = 0.0f;
                interpolate(prev_row[x], curr_sample, top_neighbor, &bottom, &top);
                buf[row * 2 * target_width + x] = top;
                if (row * 2 + 1 < target_height) {
                    buf[(row * 2 + 1) * target_width + x] = bottom;
                }
                prev_row[x] = curr_sample;
            }
        }
        jxl_free(alloc, prev_row);
    }

    for (y = 0; y < target_height; ++y) {
        memcpy(dst + y * dst_stride, buf + y * target_width, target_width * sizeof(float));
    }

    jxl_free(alloc, buf);
    return 1;
}
