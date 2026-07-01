// SPDX-License-Identifier: MIT OR Apache-2.0
#include "dct_2d.h"

#include "render/simd/features.h"
#include "render/vardct/dct.h"

#include "allocator.h"
#include <string.h>

#if defined(JXL_HAVE_SIMD_SSE2)
#include "render/vardct/dct_2d_sse2.h"
#endif
#if defined(JXL_HAVE_SIMD_NEON)
#include "render/vardct/dct_2d_neon.h"
#endif
#if defined(JXL_HAVE_SIMD_WASM128)
#include "render/vardct/dct_2d_wasm128.h"
#endif

void jxl_dct_2d(jxl_allocator_state *alloc, jxl_subgrid_f32 io, jxl_dct_direction direction) {
#if defined(JXL_HAVE_SIMD_WASM128)
    if (jxl_dct_2d_wasm128(alloc, io, direction)) {
        return;
    }
#endif
#if defined(JXL_HAVE_SIMD_NEON)
    if (jxl_dct_2d_aarch64_neon(alloc, io, direction)) {
        return;
    }
#endif
#if defined(JXL_HAVE_SIMD_SSE2)
    if (jxl_dct_2d_x86_64_sse2(alloc, io, direction)) {
        return;
    }
#endif
    jxl_dct_2d_generic(alloc, io, direction);
}

void jxl_dct_2d_generic(jxl_allocator_state *alloc, jxl_subgrid_f32 io, jxl_dct_direction direction) {
    size_t y;
    size_t by;
    size_t width = io.width;
    size_t height = io.height;
    size_t buf_len;
    size_t block_size;
    float mul;
    float *buf;
    if (width * height <= 1) {
        return;
    }

    mul = direction == JXL_DCT_FORWARD ? 0.5f : 1.0f;

    if (width == 2 && height == 1) {
        float v0 = jxl_subgrid_f32_get(io, 0, 0);
        float v1 = jxl_subgrid_f32_get(io, 1, 0);
        jxl_subgrid_f32_set(io, 0, 0, (v0 + v1) * mul);
        jxl_subgrid_f32_set(io, 1, 0, (v0 - v1) * mul);
        return;
    }
    if (width == 1 && height == 2) {
        float v0 = jxl_subgrid_f32_get(io, 0, 0);
        float v1 = jxl_subgrid_f32_get(io, 0, 1);
        jxl_subgrid_f32_set(io, 0, 0, (v0 + v1) * mul);
        jxl_subgrid_f32_set(io, 0, 1, (v0 - v1) * mul);
        return;
    }
    if (width == 2 && height == 2) {
        float v00 = jxl_subgrid_f32_get(io, 0, 0);
        float v01 = jxl_subgrid_f32_get(io, 1, 0);
        float v10 = jxl_subgrid_f32_get(io, 0, 1);
        float v11 = jxl_subgrid_f32_get(io, 1, 1);
        float mm = mul * mul;
        jxl_subgrid_f32_set(io, 0, 0, (v00 + v01 + v10 + v11) * mm);
        jxl_subgrid_f32_set(io, 1, 0, (v00 - v01 + v10 - v11) * mm);
        jxl_subgrid_f32_set(io, 0, 1, (v00 + v01 - v10 - v11) * mm);
        jxl_subgrid_f32_set(io, 1, 1, (v00 - v01 - v10 + v11) * mm);
        return;
    }

    buf_len = width > height ? width : height;
    buf = (float *)jxl_alloc(alloc, buf_len * sizeof(float));
    if (buf == NULL) {
        return;
    }

    if (height == 1) {
        jxl_dct_1d(io.data, width, buf, direction);
        jxl_free(alloc, buf);
        return;
    }
    if (width == 1) {
        size_t y;
        float *col = (float *)jxl_alloc(alloc, height * sizeof(float));
        if (col == NULL) {
            jxl_free(alloc, buf);
            return;
        }
        for (y = 0; y < height; ++y) {
            col[y] = io.data[y * io.stride];
        }
        jxl_dct_1d(col, height, buf, direction);
        for (y = 0; y < height; ++y) {
            io.data[y * io.stride] = col[y];
        }
        jxl_free(alloc, col);
        jxl_free(alloc, buf);
        return;
    }

    if (height == 2) {
        size_t x;
        jxl_subgrid_f32 top;
        jxl_subgrid_f32 bottom;
        float *row0;
        float *row1;
        jxl_subgrid_f32_split_vertical(io, 1, &top, &bottom);
        row0 = top.data;
        row1 = bottom.data;
        for (x = 0; x < width; ++x) {
            float tv0 = row0[x];
            float tv1 = row1[x];
            row0[x] = (tv0 + tv1) * mul;
            row1[x] = (tv0 - tv1) * mul;
        }
        jxl_dct_1d(row0, width, buf, direction);
        jxl_dct_1d(row1, width, buf, direction);
        jxl_free(alloc, buf);
        return;
    }
    if (width == 2) {
        size_t y;
        float *tmp = (float *)jxl_alloc(alloc, height * 2 * sizeof(float));
        float *col0;
        float *col1;
        if (tmp == NULL) {
            jxl_free(alloc, buf);
            return;
        }
        col0 = tmp;
        col1 = tmp + height;
        for (y = 0; y < height; ++y) {
            const float *row = io.data + y * io.stride;
            float v0 = row[0];
            float v1 = row[1];
            col0[y] = (v0 + v1) * mul;
            col1[y] = (v0 - v1) * mul;
        }
        jxl_dct_1d(col0, height, buf, direction);
        jxl_dct_1d(col1, height, buf, direction);
        for (y = 0; y < height; ++y) {
            float *row = io.data + y * io.stride;
            row[0] = col0[y];
            row[1] = col1[y];
        }
        jxl_free(alloc, tmp);
        jxl_free(alloc, buf);
        return;
    }

    for (y = 0; y < height; ++y) {
        jxl_dct_1d(io.data + y * io.stride, width, buf, direction);
    }

    block_size = width < height ? width : height;
    for (by = 0; by < height; by += block_size) {
        size_t bx;
        for (bx = 0; bx < width; bx += block_size) {
            size_t dy;
            for (dy = 0; dy < block_size; ++dy) {
                size_t dx;
                for (dx = dy + 1; dx < block_size; ++dx) {
                    jxl_subgrid_f32_swap(io, bx + dx, by + dy, bx + dy, by + dx);
                }
            }
        }
    }

    if (block_size == height) {
        size_t y;
        for (y = 0; y < height; ++y) {
            size_t x;
            float *grouped_row = io.data + y * io.stride;
            for (x = 0; x < width; x += height) {
                jxl_dct_1d(grouped_row + x, height, buf, direction);
            }
        }
    } else {
        size_t y;
        float *row = (float *)jxl_alloc(alloc, height * sizeof(float));
        if (row == NULL) {
            jxl_free(alloc, buf);
            return;
        }
        for (y = 0; y < width; ++y) {
            size_t off;
            size_t chunk_idx = 0;
            for (off = 0; off < height; off += block_size) {
                float *chunk = row + off;
                const float *src = io.data + (y + chunk_idx * block_size) * io.stride;
                memcpy(chunk, src, block_size * sizeof(float));
                ++chunk_idx;
            }
            jxl_dct_1d(row, height, buf, direction);
            chunk_idx = 0;
            for (off = 0; off < height; off += block_size) {
                float *chunk = row + off;
                float *dst = io.data + (y + chunk_idx * block_size) * io.stride;
                memcpy(dst, chunk, block_size * sizeof(float));
                ++chunk_idx;
            }
        }
        jxl_free(alloc, row);
    }

    for (by = 0; by < height; by += block_size) {
        size_t bx;
        for (bx = 0; bx < width; bx += block_size) {
            size_t dy;
            for (dy = 0; dy < block_size; ++dy) {
                size_t dx;
                for (dx = dy + 1; dx < block_size; ++dx) {
                    jxl_subgrid_f32_swap(io, bx + dx, by + dy, bx + dy, by + dx);
                }
            }
        }
    }

    jxl_free(alloc, buf);
}
