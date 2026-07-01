// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/filter/gabor.h"

#include "render/filter/filter_util.h"
#include "render/filter/gabor_internal.h"
#include "render/simd/features.h"
#include "render/subgrid_f32.h"

#if defined(JXL_HAVE_SIMD_SSE41)
#include "render/filter/gabor_sse41.h"
#endif
#if defined(JXL_HAVE_SIMD_AVX2)
#include "render/filter/gabor_avx2.h"
#endif
#if defined(JXL_HAVE_SIMD_NEON)
#include "render/filter/gabor_neon.h"
#endif

#include <stddef.h>
#include <string.h>

static void gabor_row_edge(const float *row_c, const float *row_a, float *out, size_t width,
                           float w0, float w1) {
    size_t x;
    float global_weight;
    float merged_w0;
    float merged_w1;

    if (width == 0) {
        return;
    }

    global_weight = 1.0f / (1.0f + w0 * 4.0f + w1 * 4.0f);

    if (row_a != NULL) {
        if (width == 1) {
            float u = row_a[0];
            float c = row_c[0];
            out[0] = (c * (1.0f + 3.0f * w0 + 2.0f * w1) + u * (w0 + 2.0f * w1)) * global_weight;
            return;
        }

        {
            float a1 = row_a[0];
            float a0 = row_a[1];
            float c1 = row_c[0];
            float c0 = row_c[1];
            out[0] = (c1 * (1.0f + 2.0f * w0 + w1) + (a1 + c0) * (w0 + w1) + a0 * w1) * global_weight;
        }

        for (x = 1; x + 1 < width; ++x) {
            float a0 = row_a[x - 1];
            float a1 = row_a[x];
            float a2 = row_a[x + 1];
            float c0 = row_c[x - 1];
            float c1 = row_c[x];
            float c2 = row_c[x + 1];
            out[x] = (c1 + (a1 + c0 + c1 + c2) * w0 + (a0 + a2 + c0 + c2) * w1) * global_weight;
        }

        {
            size_t last = width - 1;
            float a0 = row_a[last - 1];
            float a1 = row_a[last];
            float c0 = row_c[last - 1];
            float c1 = row_c[last];
            out[last] =
                (c1 * (1.0f + 2.0f * w0 + w1) + (a1 + c0) * (w0 + w1) + a0 * w1) * global_weight;
        }
        return;
    }

    if (width == 1) {
        out[0] = row_c[0];
        return;
    }

    merged_w0 = 1.0f + 2.0f + w0;
    merged_w1 = w0 + 2.0f * w1;

    out[0] = (row_c[0] * (merged_w0 + merged_w1) + row_c[1] * merged_w1) * global_weight;

    for (x = 1; x + 1 < width; ++x) {
        out[x] = (row_c[x] * merged_w0 + (row_c[x - 1] + row_c[x + 1]) * merged_w1) * global_weight;
    }

    {
        size_t last = width - 1;
        out[last] =
            (row_c[last] * (merged_w0 + merged_w1) + row_c[last - 1] * merged_w1) * global_weight;
    }
}

void jxl_gabor_row_generic(jxl_gabor_row *row) {
    size_t x;
    size_t width = row->width;
    float global_weight;
    const float *row_t = row->row_t;
    const float *row_c = row->row_c;
    const float *row_b = row->row_b;
    float *out = row->out;
    float w0 = row->w0;
    float w1 = row->w1;

    if (width == 0) {
        return;
    }

    global_weight = 1.0f / (1.0f + w0 * 4.0f + w1 * 4.0f);

    if (width == 1) {
        float t = row_t[0];
        float c = row_c[0];
        float b = row_b[0];
        float sum_side = t + 2.0f * c + b;
        float sum_diag = 2.0f * (t + b);
        out[0] = (c + sum_side * w0 + sum_diag * w1) * global_weight;
        return;
    }

    {
        float t1 = row_t[0];
        float c1 = row_c[0];
        float b1 = row_b[0];
        float t0 = row_t[1];
        float c0 = row_c[1];
        float b0 = row_b[1];
        float sum_side = t1 + c0 + c1 + b1;
        float sum_diag = t0 + t1 + b0 + b1;
        out[0] = (c1 + sum_side * w0 + sum_diag * w1) * global_weight;
    }

    for (x = 1; x + 1 < width; ++x) {
        float t0 = row_t[x - 1];
        float t1 = row_t[x];
        float t2 = row_t[x + 1];
        float c0 = row_c[x - 1];
        float c1 = row_c[x];
        float c2 = row_c[x + 1];
        float b0 = row_b[x - 1];
        float b1 = row_b[x];
        float b2 = row_b[x + 1];
        float sum_side = t1 + c0 + c2 + b1;
        float sum_diag = t0 + t2 + b0 + b2;
        out[x] = (c1 + sum_side * w0 + sum_diag * w1) * global_weight;
    }

    {
        size_t last = width - 1;
        float t1 = row_t[last];
        float c1 = row_c[last];
        float b1 = row_b[last];
        float t0 = row_t[last - 1];
        float c0 = row_c[last - 1];
        float b0 = row_b[last - 1];
        float sum_side = t1 + c0 + c1 + b1;
        float sum_diag = t0 + t1 + b0 + b1;
        out[last] = (c1 + sum_side * w0 + sum_diag * w1) * global_weight;
    }
}

static void gabor_row_dispatch(const jxl_cpu_features *feat, const float *row_t,
                               const float *row_c, const float *row_b, float *out, size_t width,
                               float w0, float w1) {
    jxl_gabor_row row;
    row.row_t = row_t;
    row.row_c = row_c;
    row.row_b = row_b;
    row.out = out;
    row.width = width;
    row.w0 = w0;
    row.w1 = w1;

#if defined(JXL_HAVE_SIMD_AVX2)
    if (feat->avx2 && feat->fma) {
        jxl_gabor_row_avx2(&row);
        return;
    }
#endif
#if defined(JXL_HAVE_SIMD_NEON)
    if (feat->neon) {
        jxl_gabor_row_neon(&row);
        return;
    }
#endif
#if defined(JXL_HAVE_SIMD_SSE41)
    if (feat->sse41) {
        jxl_gabor_row_sse41(&row);
        return;
    }
#endif
    jxl_gabor_row_generic(&row);
}

static void apply_gabor_channel_extent(const jxl_cpu_features *feat, jxl_filter_extent *ext,
                                       float *scratch, float w0, float w1) {
    size_t y;
    size_t width = ext->width;
    size_t height = ext->height;
    if (height == 0 || width == 0) {
        return;
    }

    if (height == 1) {
        const float *row_c = jxl_filter_extent_row(ext, 0);
        if (row_c == NULL) {
            return;
        }
        gabor_row_edge(row_c, NULL, scratch, width, w0, w1);
        return;
    }

    {
        const float *row_c = jxl_filter_extent_row(ext, 0);
        const float *row_b = jxl_filter_extent_row(ext, 1);
        if (row_c == NULL || row_b == NULL) {
            return;
        }
        gabor_row_edge(row_c, row_b, scratch, width, w0, w1);
    }

    for (y = 1; y + 1 < height; ++y) {
        const float *row_t = jxl_filter_extent_row(ext, y - 1);
        const float *row_c = jxl_filter_extent_row(ext, y);
        const float *row_b = jxl_filter_extent_row(ext, y + 1);
        if (row_t == NULL || row_c == NULL || row_b == NULL) {
            return;
        }
        gabor_row_dispatch(feat, row_t, row_c, row_b, scratch + y * width, width, w0, w1);
    }

    {
        const float *row_c = jxl_filter_extent_row(ext, height - 1);
        const float *row_b = jxl_filter_extent_row(ext, height - 2);
        if (row_c == NULL || row_b == NULL) {
            return;
        }
        gabor_row_edge(row_c, row_b, scratch + (height - 1) * width, width, w0, w1);
    }
}

int jxl_apply_gabor_like_extent(jxl_context *ctx, jxl_filter_extent channels[3],
                                const jxl_gabor_filter *gab, float *scratch[3]) {
    size_t ch;
    const jxl_cpu_features *feat;
    if (channels == NULL || gab == NULL || !gab->enabled || scratch == NULL) {
        return 0;
    }
    feat = jxl_context_cpu_features(ctx);
    for (ch = 0; ch < 3; ++ch) {
        if (channels[ch].full.data == NULL || scratch[ch] == NULL) {
            return 0;
        }
        apply_gabor_channel_extent(feat, &channels[ch], scratch[ch], gab->weights[ch].w0,
                                   gab->weights[ch].w1);
    }
    return 1;
}
