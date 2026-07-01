// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/vardct/transform_sse41.h"

#include "render/vardct/dct_2d_sse2.h"
#include "render/vardct/sse2_lane.h"
#include "render/vardct/transform.h"
#include "vardct/dct_select.h"

#include "allocator.h"

#include <assert.h>
#include <emmintrin.h>
#include <string.h>
#include "jxl_oxide/jxl_types.h"

static int subgrid_simd_eligible(jxl_subgrid_f32 sg, size_t min_w, size_t min_h) {
    const uintptr_t ptr = (uintptr_t)sg.data;
    if (sg.data == NULL || sg.width < min_w || sg.height < min_h) {
        return 0;
    }
    if ((ptr & 15u) != 0 || (sg.width & 3u) != 0 || (sg.height & 3u) != 0 || (sg.stride & 3u) != 0) {
        return 0;
    }
    return 1;
}

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
    num_2x2 = size / 2;

    assert(size >= 2 && (size & (size - 1)) == 0);
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

static void transform_dct2_sse41(jxl_subgrid_f32 coeff) {
    aux_idct2_in_place_2(coeff);
    aux_idct2_in_place(coeff, 4);
    aux_idct2_in_place(coeff, 8);
}

static void transform_dct4_sse41(jxl_subgrid_f32 coeff) {
    size_t y2;
    size_t y;
    jxl_sse2_lane scratch_0[4];
    jxl_sse2_lane scratch_1[4];
    aux_idct2_in_place_2(coeff);

    for (y2 = 0; y2 < 4; ++y2) {
        const float *row_ptr = jxl_subgrid_f32_row_mut(coeff, y2 * 2);
        jxl_sse2_lane a = _mm_loadu_ps(row_ptr);
        jxl_sse2_lane b = _mm_loadu_ps(row_ptr + 4);
        scratch_0[y2] = _mm_shuffle_ps(a, b, JXL_SSE2_SHUFFLE_A0B0);
        scratch_1[y2] = _mm_shuffle_ps(a, b, JXL_SSE2_SHUFFLE_A1B1);
    }

    jxl_sse2_transpose_lane(scratch_0);
    jxl_sse2_transpose_lane(scratch_1);
    jxl_sse2_dct4_inverse_4(scratch_0);
    jxl_sse2_dct4_inverse_4(scratch_1);
    for (y2 = 0; y2 < 4; ++y2) {
        float *row_ptr = jxl_subgrid_f32_row_mut(coeff, y2);
        _mm_storeu_ps(row_ptr, jxl_sse2_dct4_vec_inverse(scratch_0[y2]));
        _mm_storeu_ps(row_ptr + 4, jxl_sse2_dct4_vec_inverse(scratch_1[y2]));

        row_ptr = jxl_subgrid_f32_row_mut(coeff, y2 * 2 + 1);
        {
            jxl_sse2_lane a = _mm_loadu_ps(row_ptr);
            jxl_sse2_lane b = _mm_loadu_ps(row_ptr + 4);
            scratch_0[y2] = _mm_shuffle_ps(a, b, JXL_SSE2_SHUFFLE_A0B0);
            scratch_1[y2] = _mm_shuffle_ps(a, b, JXL_SSE2_SHUFFLE_A1B1);
        }
    }

    jxl_sse2_transpose_lane(scratch_0);
    jxl_sse2_transpose_lane(scratch_1);
    jxl_sse2_dct4_inverse_4(scratch_0);
    jxl_sse2_dct4_inverse_4(scratch_1);
    for (y = 0; y < 4; ++y) {
        float *row_ptr = jxl_subgrid_f32_row_mut(coeff, y + 4);
        _mm_storeu_ps(row_ptr, jxl_sse2_dct4_vec_inverse(scratch_0[y]));
        _mm_storeu_ps(row_ptr + 4, jxl_sse2_dct4_vec_inverse(scratch_1[y]));
    }
}

static void transform_dct4x8_sse41(jxl_subgrid_f32 coeff, int transpose) {
    float coeff0 = jxl_subgrid_f32_get(coeff, 0, 0);
    float coeff1 = jxl_subgrid_f32_get(coeff, 0, 1);
    jxl_sse2_lane scratch_0[4];
    jxl_sse2_lane scratch_1[4];
    jxl_subgrid_f32_set(coeff, 0, 0, coeff0 + coeff1);
    jxl_subgrid_f32_set(coeff, 0, 1, coeff0 - coeff1);

    if (transpose) {
        size_t y2;
        size_t y;
        static const size_t k_tr_rows[4] = {1, 5, 3, 7};
        for (y2 = 0; y2 < 4; ++y2) {
            const float *row_ptr = jxl_subgrid_f32_row_mut(coeff, y2 * 2);
            jxl_sse2_lane a = _mm_loadu_ps(row_ptr);
            jxl_sse2_lane b = _mm_loadu_ps(row_ptr + 4);
            jxl_sse2_dct8_vec_inverse(a, b, &scratch_0[y2], &scratch_1[y2]);
        }

        jxl_sse2_dct4_inverse_4(scratch_0);
        jxl_sse2_dct4_inverse_4(scratch_1);
        jxl_sse2_transpose_lane(scratch_0);
        jxl_sse2_transpose_lane(scratch_1);
        for (y2 = 0; y2 < 4; ++y2) {
            jxl_sse2_lane out_l;
            jxl_sse2_lane out_r;
            y = k_tr_rows[y2];
            const float *row_ptr = jxl_subgrid_f32_row_mut(coeff, y);
            jxl_sse2_lane a = _mm_loadu_ps(row_ptr);
            jxl_sse2_lane b = _mm_loadu_ps(row_ptr + 4);
            jxl_sse2_dct8_vec_inverse(a, b, &out_l, &out_r);

            float *dst = jxl_subgrid_f32_row_mut(coeff, y2);
            _mm_storeu_ps(dst, scratch_0[y2]);
            dst = jxl_subgrid_f32_row_mut(coeff, y2 + 4);
            _mm_storeu_ps(dst, scratch_1[y2]);

            scratch_0[y2] = out_l;
            scratch_1[y2] = out_r;
        }

        {
            jxl_sse2_lane tmp = scratch_0[1];
            scratch_0[1] = scratch_0[2];
            scratch_0[2] = tmp;
        }
        {
            jxl_sse2_lane tmp = scratch_1[1];
            scratch_1[1] = scratch_1[2];
            scratch_1[2] = tmp;
        }

        jxl_sse2_dct4_inverse_4(scratch_0);
        jxl_sse2_dct4_inverse_4(scratch_1);
        jxl_sse2_transpose_lane(scratch_0);
        jxl_sse2_transpose_lane(scratch_1);
        for (y = 0; y < 4; ++y) {
            float *row_ptr = jxl_subgrid_f32_row_mut(coeff, y);
            _mm_storeu_ps(row_ptr + 4, scratch_0[y]);
            row_ptr = jxl_subgrid_f32_row_mut(coeff, y + 4);
            _mm_storeu_ps(row_ptr + 4, scratch_1[y]);
        }
    } else {
        size_t y2;
        size_t y;
        for (y2 = 0; y2 < 4; ++y2) {
            const float *row_ptr = jxl_subgrid_f32_row_mut(coeff, y2 * 2);
            jxl_sse2_lane a = _mm_loadu_ps(row_ptr);
            jxl_sse2_lane b = _mm_loadu_ps(row_ptr + 4);
            jxl_sse2_dct8_vec_inverse(a, b, &scratch_0[y2], &scratch_1[y2]);
        }

        jxl_sse2_dct4_inverse_4(scratch_0);
        jxl_sse2_dct4_inverse_4(scratch_1);
        for (y2 = 0; y2 < 4; ++y2) {
            float *row_ptr = jxl_subgrid_f32_row_mut(coeff, y2);
            _mm_storeu_ps(row_ptr, scratch_0[y2]);
            _mm_storeu_ps(row_ptr + 4, scratch_1[y2]);

            row_ptr = jxl_subgrid_f32_row_mut(coeff, y2 * 2 + 1);
            jxl_sse2_lane a = _mm_loadu_ps(row_ptr);
            jxl_sse2_lane b = _mm_loadu_ps(row_ptr + 4);
            jxl_sse2_dct8_vec_inverse(a, b, &scratch_0[y2], &scratch_1[y2]);
        }

        jxl_sse2_dct4_inverse_4(scratch_0);
        jxl_sse2_dct4_inverse_4(scratch_1);
        for (y = 0; y < 4; ++y) {
            float *row_ptr = jxl_subgrid_f32_row_mut(coeff, y + 4);
            _mm_storeu_ps(row_ptr, scratch_0[y]);
            _mm_storeu_ps(row_ptr + 4, scratch_1[y]);
        }
    }
}

int jxl_render_transform_varblock_sse41(jxl_allocator_state *alloc, jxl_subgrid_f32 coeff,
                                        jxl_transform_type dct_select) {
    switch (dct_select) {
    case JXL_TRANSFORM_DCT2:
        if (!subgrid_simd_eligible(coeff, 8, 8)) {
            return 0;
        }
        transform_dct2_sse41(coeff);
        return 1;
    case JXL_TRANSFORM_DCT4:
        if (!subgrid_simd_eligible(coeff, 8, 8)) {
            return 0;
        }
        transform_dct4_sse41(coeff);
        return 1;
    case JXL_TRANSFORM_DCT4X8:
        if (!subgrid_simd_eligible(coeff, 8, 8)) {
            return 0;
        }
        transform_dct4x8_sse41(coeff, 0);
        return 1;
    case JXL_TRANSFORM_DCT8X4:
        if (!subgrid_simd_eligible(coeff, 8, 8)) {
            return 0;
        }
        transform_dct4x8_sse41(coeff, 1);
        return 1;
    case JXL_TRANSFORM_HORNUSS:
    case JXL_TRANSFORM_AFV0:
    case JXL_TRANSFORM_AFV1:
    case JXL_TRANSFORM_AFV2:
    case JXL_TRANSFORM_AFV3:
        return 0;
    default:
        if (!subgrid_simd_eligible(coeff, 4, 4)) {
            return 0;
        }
        if (!jxl_dct_2d_x86_64_sse2(alloc, coeff, JXL_DCT_INVERSE)) {
            return 0;
        }
        return 1;
    }
}
