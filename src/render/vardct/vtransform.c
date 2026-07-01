// SPDX-License-Identifier: MIT OR Apache-2.0
#include "transform.h"

#include "render/vardct/dct_2d.h"
#include "render/vardct/transform_common.h"
#include "vardct/dct_select.h"

#include "render/simd/features.h"

typedef void (*jxl_dct_2d_fn)(jxl_allocator_state *, jxl_subgrid_f32, jxl_dct_direction);

#if defined(JXL_HAVE_SIMD_SSE2)
#include "render/vardct/transform_sse2.h"
#endif
#if defined(JXL_HAVE_SIMD_SSE41)
#include "render/vardct/transform_sse41.h"
#endif
#if defined(JXL_HAVE_SIMD_WASM128)
#include "render/vardct/transform_wasm128.h"
#endif
#if defined(JXL_HAVE_SIMD_NEON)
#include "render/vardct/transform_neon.h"
#endif

#include <assert.h>
#include <string.h>

static void aux_idct2_in_place_2(jxl_subgrid_f32 block) {
    float *base = block.data;
    float c00 = base[0];
    float c01 = base[1];
    float c10 = base[block.stride];
    float c11 = base[block.stride + 1];
    base[0] = c00 + c01 + c10 + c11;
    base[1] = c00 + c01 - c10 - c11;
    base[block.stride] = c00 - c01 + c10 - c11;
    base[block.stride + 1] = c00 - c01 - c10 + c11;
}

static void aux_idct2_in_place(jxl_subgrid_f32 block, size_t size) {
    size_t y;
    float scratch[64];
    size_t num_2x2;
    const float *base = block.data;
    size_t st = block.stride;
    assert(size >= 2 && (size & (size - 1)) == 0);
    num_2x2 = size / 2;
    assert(size * size <= sizeof(scratch) / sizeof(scratch[0]));

    for (y = 0; y < num_2x2; ++y) {
        size_t x;
        for (x = 0; x < num_2x2; ++x) {
            float c00 = base[y * st + x];
            float c01 = base[y * st + x + num_2x2];
            float c10 = base[(y + num_2x2) * st + x];
            float c11 = base[(y + num_2x2) * st + x + num_2x2];

            scratch[2 * y * size + 2 * x] = c00 + c01 + c10 + c11;
            scratch[2 * y * size + 2 * x + 1] = c00 + c01 - c10 - c11;
            scratch[(2 * y + 1) * size + 2 * x] = c00 - c01 + c10 - c11;
            scratch[(2 * y + 1) * size + 2 * x + 1] = c00 - c01 - c10 + c11;
        }
    }

    for (y = 0; y < size; ++y) {
        memcpy(block.data + y * st, &scratch[y * size], size * sizeof(float));
    }
}

static void transform_dct2(jxl_allocator_state *alloc, jxl_subgrid_f32 coeff) {
    aux_idct2_in_place_2(coeff);
    aux_idct2_in_place(coeff, 4);
    aux_idct2_in_place(coeff, 8);
}

static void transform_dct4(jxl_allocator_state *alloc, jxl_subgrid_f32 coeff, jxl_dct_2d_fn dct_2d) {
    size_t y;
    float scratch[64];
    const float *src = coeff.data;
    size_t src_st = coeff.stride;
    aux_idct2_in_place_2(coeff);

    for (y = 0; y < 2; ++y) {
        size_t x;
        for (x = 0; x < 2; ++x) {
            size_t iy;
            float *tile = &scratch[(y * 2 + x) * 16];
            for (iy = 0; iy < 4; ++iy) {
                size_t ix;
                const float *src_row = src + (y + iy * 2) * src_st + x;
                float *tile_row = tile + iy * 4;
                for (ix = 0; ix < 4; ++ix) {
                    tile_row[ix] = src_row[ix * 2];
                }
            }
            dct_2d(alloc, jxl_subgrid_f32_from_buf(tile, 4, 4, 4), JXL_DCT_INVERSE);
        }
    }

    for (y = 0; y < 2; ++y) {
        size_t x;
        for (x = 0; x < 2; ++x) {
            size_t iy;
            const float *tile = &scratch[(y * 2 + x) * 16];
            float *dst_base = coeff.data + y * 4 * coeff.stride + x * 4;
            for (iy = 0; iy < 4; ++iy) {
                size_t ix;
                float *dst_row = dst_base + iy * coeff.stride;
                const float *tile_row = tile + iy * 4;
                for (ix = 0; ix < 4; ++ix) {
                    dst_row[ix] = tile_row[ix];
                }
            }
        }
    }
}

static void transform_hornuss(jxl_subgrid_f32 coeff) {
    size_t y;
    float scratch[64];
    const float *src = coeff.data;
    size_t src_st = coeff.stride;
    float *dst = coeff.data;
    size_t dst_st = coeff.stride;
    aux_idct2_in_place_2(coeff);

    for (y = 0; y < 2; ++y) {
        size_t x;
        for (x = 0; x < 2; ++x) {
            size_t iy;
            size_t i;
            float residual_sum;
            float *tile = &scratch[(y * 2 + x) * 16];
            float avg;
            for (iy = 0; iy < 4; ++iy) {
                size_t ix;
                const float *src_row = src + (y + iy * 2) * src_st + x;
                float *tile_row = tile + iy * 4;
                for (ix = 0; ix < 4; ++ix) {
                    tile_row[ix] = src_row[ix * 2];
                }
            }
            residual_sum = 0.0f;
            for (i = 1; i < 16; ++i) {
                residual_sum += tile[i];
            }
            avg = tile[0] - residual_sum / 16.0f;
            tile[0] = tile[5];
            tile[5] = 0.0f;
            for (i = 0; i < 16; ++i) {
                tile[i] += avg;
            }
        }
    }

    for (y = 0; y < 2; ++y) {
        size_t x;
        for (x = 0; x < 2; ++x) {
            size_t iy;
            const float *tile = &scratch[(y * 2 + x) * 16];
            float *dst_base = dst + y * 4 * dst_st + x * 4;
            for (iy = 0; iy < 4; ++iy) {
                size_t ix;
                float *dst_row = dst_base + iy * dst_st;
                const float *tile_row = tile + iy * 4;
                for (ix = 0; ix < 4; ++ix) {
                    dst_row[ix] = tile_row[ix];
                }
            }
        }
    }
}

static void transform_dct4x8(jxl_allocator_state *alloc, jxl_subgrid_f32 coeff, int transpose,
                             jxl_dct_2d_fn dct_2d) {
                                 size_t idx;
    float coeff0 = jxl_subgrid_f32_get(coeff, 0, 0);
    float coeff1 = jxl_subgrid_f32_get(coeff, 0, 1);
    float scratch[64];
    jxl_subgrid_f32_set(coeff, 0, 0, coeff0 + coeff1);
    jxl_subgrid_f32_set(coeff, 0, 1, coeff0 - coeff1);

    for (idx = 0; idx < 2; ++idx) {
        size_t iy;
        float *tile = &scratch[idx * 32];
        jxl_subgrid_f32 tile_sg = jxl_subgrid_f32_from_buf(tile, 8, 4, 8);
        for (iy = 0; iy < 4; ++iy) {
            size_t ix;
            for (ix = 0; ix < 8; ++ix) {
                jxl_subgrid_f32_set(tile_sg, ix, iy, jxl_subgrid_f32_get(coeff, ix, iy * 2 + idx));
            }
        }
        dct_2d(alloc, tile_sg, JXL_DCT_INVERSE);
    }

    if (transpose) {
        size_t y;
        for (y = 0; y < 8; ++y) {
            size_t x;
            for (x = 0; x < 8; ++x) {
                jxl_subgrid_f32_set(coeff, y, x, scratch[y * 8 + x]);
            }
        }
    } else {
        size_t y;
        for (y = 0; y < 8; ++y) {
            memcpy(jxl_subgrid_f32_row_mut(coeff, y), &scratch[y * 8], 8 * sizeof(float));
        }
    }
}

static void transform_afv(jxl_allocator_state *alloc, jxl_subgrid_f32 coeff, unsigned n,
                          jxl_dct_2d_fn dct_2d) {
    size_t idx;
    size_t row;
    size_t iy;
    float coeff_afv[16];
    float samples_afv[16] = {0};
    float scratch_4x4[16];
    float scratch_4x8[32];
    assert(n < 4);
    unsigned flip_x = n % 2;
    unsigned flip_y = n / 2;

    coeff_afv[0] = (jxl_subgrid_f32_get(coeff, 0, 0) + jxl_subgrid_f32_get(coeff, 1, 0) +
                    jxl_subgrid_f32_get(coeff, 0, 1)) *
                   4.0f;
    for (idx = 1; idx < 16; ++idx) {
        size_t ix;
        iy = idx / 4;
        ix = idx % 4;
        coeff_afv[idx] = jxl_subgrid_f32_get(coeff, 2 * ix, 2 * iy);
    }

    for (row = 0; row < 16; ++row) {
        size_t col;
        const float *basis = jxl_afv_basis_row(row);
        float c = coeff_afv[row];
        for (col = 0; col < 16; ++col) {
            samples_afv[col] = c * basis[col] + samples_afv[col];
        }
    }


    scratch_4x4[0] =
        jxl_subgrid_f32_get(coeff, 0, 0) - jxl_subgrid_f32_get(coeff, 1, 0) + jxl_subgrid_f32_get(coeff, 0, 1);
    for (iy = 0; iy < 4; ++iy) {
        size_t ix;
        for (ix = 0; ix < 4; ++ix) {
            if ((ix | iy) == 0) {
                continue;
            }
            scratch_4x4[ix * 4 + iy] = jxl_subgrid_f32_get(coeff, 2 * ix + 1, 2 * iy);
        }
    }
    dct_2d(alloc, jxl_subgrid_f32_from_buf(scratch_4x4, 4, 4, 4), JXL_DCT_INVERSE);

    scratch_4x8[0] = jxl_subgrid_f32_get(coeff, 0, 0) - jxl_subgrid_f32_get(coeff, 0, 1);
    for (iy = 0; iy < 4; ++iy) {
        size_t ix;
        for (ix = 0; ix < 8; ++ix) {
            if ((ix | iy) == 0) {
                continue;
            }
            scratch_4x8[iy * 8 + ix] = jxl_subgrid_f32_get(coeff, ix, 2 * iy + 1);
        }
    }
    dct_2d(alloc, jxl_subgrid_f32_from_buf(scratch_4x8, 8, 4, 8), JXL_DCT_INVERSE);

    for (iy = 0; iy < 4; ++iy) {
        size_t ix;
        size_t afv_y = flip_y == 0 ? iy : 3 - iy;
        for (ix = 0; ix < 4; ++ix) {
            size_t afv_x = flip_x == 0 ? ix : 3 - ix;
            jxl_subgrid_f32_set(coeff, flip_x * 4 + ix, flip_y * 4 + iy,
                                samples_afv[afv_y * 4 + afv_x]);
        }
    }

    for (iy = 0; iy < 4; ++iy) {
        size_t ix;
        size_t y = flip_y * 4 + iy;
        for (ix = 0; ix < 4; ++ix) {
            size_t x = (1 - flip_x) * 4 + ix;
            jxl_subgrid_f32_set(coeff, x, y, scratch_4x4[iy * 4 + ix]);
        }
    }

    for (iy = 0; iy < 4; ++iy) {
        size_t y = (1 - flip_y) * 4 + iy;
        memcpy(jxl_subgrid_f32_row_mut(coeff, y), &scratch_4x8[iy * 8], 8 * sizeof(float));
    }
}

static void transform_dct(jxl_allocator_state *alloc, jxl_subgrid_f32 coeff, jxl_dct_2d_fn dct_2d) {
    dct_2d(alloc, coeff, JXL_DCT_INVERSE);
}

static void jxl_render_transform_varblock_impl(jxl_allocator_state *alloc, jxl_subgrid_f32 coeff,
                                               jxl_transform_type dct_select, jxl_dct_2d_fn dct_2d) {
    switch (dct_select) {
    case JXL_TRANSFORM_DCT2:
        transform_dct2(alloc, coeff);
        break;
    case JXL_TRANSFORM_DCT4:
        transform_dct4(alloc, coeff, dct_2d);
        break;
    case JXL_TRANSFORM_HORNUSS:
        transform_hornuss(coeff);
        break;
    case JXL_TRANSFORM_DCT4X8:
        transform_dct4x8(alloc, coeff, 0, dct_2d);
        break;
    case JXL_TRANSFORM_DCT8X4:
        transform_dct4x8(alloc, coeff, 1, dct_2d);
        break;
    case JXL_TRANSFORM_AFV0:
        transform_afv(alloc, coeff, 0, dct_2d);
        break;
    case JXL_TRANSFORM_AFV1:
        transform_afv(alloc, coeff, 1, dct_2d);
        break;
    case JXL_TRANSFORM_AFV2:
        transform_afv(alloc, coeff, 2, dct_2d);
        break;
    case JXL_TRANSFORM_AFV3:
        transform_afv(alloc, coeff, 3, dct_2d);
        break;
    default:
        transform_dct(alloc, coeff, dct_2d);
        break;
    }
}

void jxl_render_transform_varblock_generic(jxl_context *ctx, jxl_allocator_state *alloc,
                                           jxl_subgrid_f32 coeff, jxl_transform_type dct_select) {
    (void)ctx;
    jxl_render_transform_varblock_impl(alloc, coeff, dct_select, jxl_dct_2d_generic);
}

void jxl_render_transform_varblock_fallback(jxl_context *ctx, jxl_allocator_state *alloc,
                                              jxl_subgrid_f32 coeff, jxl_transform_type dct_select) {
    (void)ctx;
    jxl_render_transform_varblock_impl(alloc, coeff, dct_select, jxl_dct_2d);
}

void jxl_render_transform_varblock(jxl_context *ctx, jxl_allocator_state *alloc,
                                   jxl_subgrid_f32 coeff, jxl_transform_type dct_select) {
#if defined(JXL_HAVE_SIMD_WASM128)
    if (jxl_render_transform_varblock_wasm128(alloc, coeff, dct_select)) {
        return;
    }
#endif
#if defined(JXL_HAVE_SIMD_NEON)
    if (jxl_render_transform_varblock_neon(alloc, coeff, dct_select)) {
        return;
    }
#endif
#if defined(JXL_HAVE_SIMD_SSE41)
    if (jxl_context_cpu_features(ctx)->sse41 &&
        jxl_render_transform_varblock_sse41(alloc, coeff, dct_select)) {
        return;
    }
#endif
#if defined(JXL_HAVE_SIMD_SSE2)
    if (jxl_render_transform_varblock_sse2(alloc, coeff, dct_select)) {
        return;
    }
#endif
    jxl_render_transform_varblock_impl(alloc, coeff, dct_select, jxl_dct_2d);
}
