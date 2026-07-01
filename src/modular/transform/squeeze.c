// SPDX-License-Identifier: MIT OR Apache-2.0
#include "squeeze.h"

#include "squeeze_internal.h"
#include "allocator.h"
#include "render/simd/features.h"

#include "jxl_oxide/jxl_types.h"
#include <string.h>

jxl_inline int32_t wrap_add_i32(int32_t a, int32_t b) {
    return (int32_t)((uint32_t)a + (uint32_t)b);
}

jxl_inline int32_t wrap_sub_i32(int32_t a, int32_t b) {
    return (int32_t)((uint32_t)a - (uint32_t)b);
}

static int32_t tendency_i32(int32_t a, int32_t b, int32_t c) {
    if (a >= b && b >= c) {
        int32_t x = (4 * a - 3 * c - b + 6) / 12;
        if (x - (x & 1) > 2 * (a - b)) {
            x = 2 * (a - b) + 1;
        }
        if (x + (x & 1) > 2 * (b - c)) {
            x = 2 * (b - c);
        }
        return x;
    }
    if (a <= b && b <= c) {
        int32_t x = (4 * a - 3 * c - b - 6) / 12;
        if (x + (x & 1) < 2 * (a - b)) {
            x = 2 * (a - b) - 1;
        }
        if (x - (x & 1) < 2 * (b - c)) {
            x = 2 * (b - c);
        }
        return x;
    }
    return 0;
}

void jxl_squeeze_inverse_h_i32(jxl_allocator_state *alloc, int32_t *merged, size_t width, size_t height, size_t row_stride) {
    size_t y;
    size_t avg_width;
    int32_t *scratch;
    if (row_stride == 0) {
        row_stride = width;
    }
    avg_width = (width + 1) / 2;
    scratch = jxl_alloc(alloc, width * sizeof(*scratch));
    if (scratch == NULL) {
        return;
    }
    for (y = 0; y < height; ++y) {
        int32_t left;
        size_t x;
        int32_t *row = merged + y * row_stride;
        int32_t *avg_row = scratch;
        int32_t *residu_row = scratch + avg_width;
        int32_t avg = row[0];
        memcpy(scratch, row, width * sizeof(*scratch));

        left = avg;
        x = 0;
        for (; x + 1 < width; x += 2) {
            size_t rx = x / 2;
            int32_t residu = residu_row[rx];
            int32_t next_avg = (rx + 1 < avg_width) ? avg_row[rx + 1] : avg;
            int32_t diff = wrap_add_i32(residu, tendency_i32(left, avg, next_avg));
            int32_t first = wrap_add_i32(avg, diff / 2);
            int32_t second = wrap_sub_i32(first, diff);
            row[x] = first;
            row[x + 1] = second;
            avg = next_avg;
            left = second;
        }
        if (x < width) {
            row[x] = avg_row[avg_width - 1];
        }
    }
    jxl_free(alloc, scratch);
}

void jxl_squeeze_inverse_v_i32(jxl_allocator_state *alloc, int32_t *merged, size_t width, size_t height, size_t row_stride) {
    size_t x;
    size_t avg_height;
    int32_t *scratch;
    if (row_stride == 0) {
        row_stride = width;
    }
    avg_height = (height + 1) / 2;
    scratch = jxl_alloc(alloc, height * sizeof(*scratch));
    if (scratch == NULL) {
        return;
    }
    for (x = 0; x < width; ++x) {
        size_t y;
        int32_t top;
        int32_t *avg_col = scratch;
        int32_t *residu_col = scratch + avg_height;
        int32_t avg = merged[x];
        for (y = 0; y < height; ++y) {
            scratch[y] = merged[y * row_stride + x];
        }

        top = avg;
        y = 0;
        for (; y + 1 < height; y += 2) {
            size_t ry = y / 2;
            int32_t residu = residu_col[ry];
            int32_t next_avg = (ry + 1 < avg_height) ? avg_col[ry + 1] : avg;
            int32_t diff = wrap_add_i32(residu, tendency_i32(top, avg, next_avg));
            int32_t first = wrap_add_i32(avg, diff / 2);
            int32_t second = wrap_sub_i32(first, diff);
            merged[y * row_stride + x] = first;
            merged[(y + 1) * row_stride + x] = second;
            avg = next_avg;
            top = second;
        }
        if (y < height) {
            merged[y * row_stride + x] = avg_col[avg_height - 1];
        }
    }
    jxl_free(alloc, scratch);
}

jxl_inline int16_t wrap_add_i16(int16_t a, int16_t b) {
    return (int16_t)((uint16_t)a + (uint16_t)b);
}

jxl_inline int16_t wrap_sub_i16(int16_t a, int16_t b) {
    return (int16_t)((uint16_t)a - (uint16_t)b);
}

static int16_t tendency_i16(int16_t a, int16_t b, int16_t c) {
    if (a >= b && b >= c) {
        int16_t x = (int16_t)((4 * a - 3 * c - b + 6) / 12);
        if (x - (x & 1) > 2 * (a - b)) {
            x = (int16_t)(2 * (a - b) + 1);
        }
        if (x + (x & 1) > 2 * (b - c)) {
            x = (int16_t)(2 * (b - c));
        }
        return x;
    }
    if (a <= b && b <= c) {
        int16_t x = (int16_t)((4 * a - 3 * c - b - 6) / 12);
        if (x + (x & 1) < 2 * (a - b)) {
            x = (int16_t)(2 * (a - b) - 1);
        }
        if (x - (x & 1) < 2 * (b - c)) {
            x = (int16_t)(2 * (b - c));
        }
        return x;
    }
    return 0;
}

void jxl_squeeze_inverse_h_i16_base(jxl_allocator_state *alloc, int16_t *merged, size_t width,
                                    size_t height, size_t row_stride) {
    size_t y;
    size_t avg_width;
    int16_t *scratch;
    if (row_stride == 0) {
        row_stride = width;
    }
    avg_width = (width + 1) / 2;
    scratch = jxl_alloc(alloc, width * sizeof(*scratch));
    if (scratch == NULL) {
        return;
    }
    for (y = 0; y < height; ++y) {
        size_t x;
        int16_t *row = merged + y * row_stride;
        int16_t *avg_row = scratch;
        int16_t *residu_row = scratch + avg_width;
        int16_t avg = row[0];
        int16_t left = avg;
        memcpy(scratch, row, width * sizeof(*scratch));

        x = 0;
        for (; x + 1 < width; x += 2) {
            size_t rx = x / 2;
            int16_t residu = residu_row[rx];
            int16_t next_avg = (rx + 1 < avg_width) ? avg_row[rx + 1] : avg;
            int16_t diff = wrap_add_i16(residu, tendency_i16(left, avg, next_avg));
            int16_t first = wrap_add_i16(avg, (int16_t)(diff / 2));
            int16_t second = wrap_sub_i16(first, diff);
            row[x] = first;
            row[x + 1] = second;
            avg = next_avg;
            left = second;
        }
        if (x < width) {
            row[x] = avg_row[avg_width - 1];
        }
    }
    jxl_free(alloc, scratch);
}

void jxl_squeeze_inverse_h_i16(jxl_context *ctx, jxl_allocator_state *alloc, int16_t *merged,
                               size_t width, size_t height, size_t row_stride) {
#if defined(JXL_HAVE_SIMD_WASM128)
    jxl_squeeze_inverse_h_i16_wasm128(alloc, merged, width, height, row_stride);
    return;
#endif
#if defined(JXL_HAVE_SIMD_NEON)
    {
        const jxl_cpu_features *feat = jxl_context_cpu_features(ctx);
        if (feat->neon) {
            jxl_squeeze_inverse_h_i16_neon(alloc, merged, width, height, row_stride);
            return;
        }
    }
#endif
#if defined(JXL_HAVE_SIMD_SSE41)
    const jxl_cpu_features *feat = jxl_context_cpu_features(ctx);
    if (feat->sse41) {
#if defined(JXL_HAVE_SIMD_AVX2)
        if (feat->avx2) {
            jxl_squeeze_inverse_h_i16_x86_avx2(alloc, merged, width, height, row_stride);
            return;
        }
#endif
        jxl_squeeze_inverse_h_i16_x86_sse41(alloc, merged, width, height, row_stride);
        return;
    }
#endif
    jxl_squeeze_inverse_h_i16_base(alloc, merged, width, height, row_stride);
}

void jxl_squeeze_inverse_v_i16_base(jxl_allocator_state *alloc, int16_t *merged, size_t width,
                                    size_t height, size_t row_stride) {
                                        size_t x;
    size_t avg_height;
    int16_t *scratch;
    if (row_stride == 0) {
        row_stride = width;
    }
    avg_height = (height + 1) / 2;
    scratch = jxl_alloc(alloc, height * sizeof(*scratch));
    if (scratch == NULL) {
        return;
    }
    for (x = 0; x < width; ++x) {
        size_t y;
        int16_t *avg_col = scratch;
        int16_t *residu_col = scratch + avg_height;
        int16_t avg = merged[x];
        int16_t top = avg;
        for (y = 0; y < height; ++y) {
            scratch[y] = merged[y * row_stride + x];
        }

        y = 0;
        for (; y + 1 < height; y += 2) {
            size_t ry = y / 2;
            int16_t residu = residu_col[ry];
            int16_t next_avg = (ry + 1 < avg_height) ? avg_col[ry + 1] : avg;
            int16_t diff = wrap_add_i16(residu, tendency_i16(top, avg, next_avg));
            int16_t first = wrap_add_i16(avg, (int16_t)(diff / 2));
            int16_t second = wrap_sub_i16(first, diff);
            merged[y * row_stride + x] = first;
            merged[(y + 1) * row_stride + x] = second;
            avg = next_avg;
            top = second;
        }
        if (y < height) {
            merged[y * row_stride + x] = avg_col[avg_height - 1];
        }
    }
    jxl_free(alloc, scratch);
}

void jxl_squeeze_inverse_v_i16(jxl_context *ctx, jxl_allocator_state *alloc, int16_t *merged,
                               size_t width, size_t height, size_t row_stride) {
#if defined(JXL_HAVE_SIMD_WASM128)
    jxl_squeeze_inverse_v_i16_wasm128(alloc, merged, width, height, row_stride);
    return;
#endif
#if defined(JXL_HAVE_SIMD_NEON)
    {
        const jxl_cpu_features *feat = jxl_context_cpu_features(ctx);
        if (feat->neon) {
            jxl_squeeze_inverse_v_i16_neon(alloc, merged, width, height, row_stride);
            return;
        }
    }
#endif
#if defined(JXL_HAVE_SIMD_SSE41)
    const jxl_cpu_features *feat = jxl_context_cpu_features(ctx);
    if (feat->sse41) {
#if defined(JXL_HAVE_SIMD_AVX2)
        if (feat->avx2) {
            jxl_squeeze_inverse_v_i16_x86_avx2(alloc, merged, width, height, row_stride);
            return;
        }
#endif
        jxl_squeeze_inverse_v_i16_x86_sse41(alloc, merged, width, height, row_stride);
        return;
    }
#endif
    jxl_squeeze_inverse_v_i16_base(alloc, merged, width, height, row_stride);
}
