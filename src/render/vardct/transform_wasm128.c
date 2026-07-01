// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/vardct/transform_wasm128.h"

#include "render/vardct/dct_2d_wasm128.h"
#include "render/vardct/wasm128_lane.h"
#include "render/vardct/transform.h"
#include "vardct/dct_select.h"

#include "allocator.h"

#include "jxl_oxide/jxl_types.h"

#if defined(JXL_HAVE_SIMD_WASM128)

#include <wasm_simd128.h>

jxl_inline jxl_wasm128_lane shuffle_even(jxl_wasm128_lane a, jxl_wasm128_lane b) {
    return wasm_i32x4_shuffle(a, b, 0, 2, 4, 6);
}

jxl_inline jxl_wasm128_lane shuffle_odd(jxl_wasm128_lane a, jxl_wasm128_lane b) {
    return wasm_i32x4_shuffle(a, b, 1, 3, 5, 7);
}

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
    float c00 = jxl_subgrid_f32_get(block, 0, 0);
    float c01 = jxl_subgrid_f32_get(block, 1, 0);
    float c10 = jxl_subgrid_f32_get(block, 0, 1);
    float c11 = jxl_subgrid_f32_get(block, 1, 1);
    jxl_subgrid_f32_set(block, 0, 0, c00 + c01 + c10 + c11);
    jxl_subgrid_f32_set(block, 1, 0, c00 + c01 - c10 - c11);
    jxl_subgrid_f32_set(block, 0, 1, c00 - c01 + c10 - c11);
    jxl_subgrid_f32_set(block, 1, 1, c00 - c01 - c10 + c11);
}

static void transform_dct4_wasm128(jxl_subgrid_f32 coeff) {
    size_t y2;
    size_t y;
    jxl_wasm128_lane scratch_0[4];
    jxl_wasm128_lane scratch_1[4];
    aux_idct2_in_place_2(coeff);

    for (y2 = 0; y2 < 4; ++y2) {
        const float *row_ptr = jxl_subgrid_f32_row_mut(coeff, y2 * 2);
        jxl_wasm128_lane a = wasm_v128_load(row_ptr);
        jxl_wasm128_lane b = wasm_v128_load(row_ptr + 4);
        scratch_0[y2] = shuffle_even(a, b);
        scratch_1[y2] = shuffle_odd(a, b);
    }

    jxl_wasm128_transpose_lane(scratch_0);
    jxl_wasm128_transpose_lane(scratch_1);
    jxl_wasm128_dct4_inverse_4(scratch_0);
    jxl_wasm128_dct4_inverse_4(scratch_1);
    for (y2 = 0; y2 < 4; ++y2) {
        float *row_ptr = jxl_subgrid_f32_row_mut(coeff, y2);
        wasm_v128_store(row_ptr, jxl_wasm128_dct4_vec_inverse(scratch_0[y2]));
        wasm_v128_store(row_ptr + 4, jxl_wasm128_dct4_vec_inverse(scratch_1[y2]));

        row_ptr = jxl_subgrid_f32_row_mut(coeff, y2 * 2 + 1);
        {
            jxl_wasm128_lane a = wasm_v128_load(row_ptr);
            jxl_wasm128_lane b = wasm_v128_load(row_ptr + 4);
            scratch_0[y2] = shuffle_even(a, b);
            scratch_1[y2] = shuffle_odd(a, b);
        }
    }

    jxl_wasm128_transpose_lane(scratch_0);
    jxl_wasm128_transpose_lane(scratch_1);
    jxl_wasm128_dct4_inverse_4(scratch_0);
    jxl_wasm128_dct4_inverse_4(scratch_1);
    for (y = 0; y < 4; ++y) {
        float *row_ptr = jxl_subgrid_f32_row_mut(coeff, y + 4);
        wasm_v128_store(row_ptr, jxl_wasm128_dct4_vec_inverse(scratch_0[y]));
        wasm_v128_store(row_ptr + 4, jxl_wasm128_dct4_vec_inverse(scratch_1[y]));
    }
}

static void transform_dct4x8_wasm128(jxl_subgrid_f32 coeff, int transpose) {
    float coeff0 = jxl_subgrid_f32_get(coeff, 0, 0);
    float coeff1 = jxl_subgrid_f32_get(coeff, 0, 1);
    jxl_wasm128_lane scratch_0[4];
    jxl_wasm128_lane scratch_1[4];
    jxl_subgrid_f32_set(coeff, 0, 0, coeff0 + coeff1);
    jxl_subgrid_f32_set(coeff, 0, 1, coeff0 - coeff1);

    if (transpose) {
        size_t y2;
        size_t y;
        static const size_t k_tr_rows[4] = {1, 5, 3, 7};
        for (y2 = 0; y2 < 4; ++y2) {
            const float *row_ptr = jxl_subgrid_f32_row_mut(coeff, y2 * 2);
            jxl_wasm128_lane a = wasm_v128_load(row_ptr);
            jxl_wasm128_lane b = wasm_v128_load(row_ptr + 4);
            jxl_wasm128_dct8_vec_inverse(a, b, &scratch_0[y2], &scratch_1[y2]);
        }

        jxl_wasm128_dct4_inverse_4(scratch_0);
        jxl_wasm128_dct4_inverse_4(scratch_1);
        jxl_wasm128_transpose_lane(scratch_0);
        jxl_wasm128_transpose_lane(scratch_1);
        for (y2 = 0; y2 < 4; ++y2) {
            jxl_wasm128_lane out_l;
            jxl_wasm128_lane out_r;
            y = k_tr_rows[y2];
            const float *row_ptr = jxl_subgrid_f32_row_mut(coeff, y);
            jxl_wasm128_lane a = wasm_v128_load(row_ptr);
            jxl_wasm128_lane b = wasm_v128_load(row_ptr + 4);
            jxl_wasm128_dct8_vec_inverse(a, b, &out_l, &out_r);

            {
                float *dst = jxl_subgrid_f32_row_mut(coeff, y2);
                wasm_v128_store(dst, scratch_0[y2]);
                dst = jxl_subgrid_f32_row_mut(coeff, y2 + 4);
                wasm_v128_store(dst, scratch_1[y2]);
            }

            scratch_0[y2] = out_l;
            scratch_1[y2] = out_r;
        }

        {
            jxl_wasm128_lane tmp = scratch_0[1];
            scratch_0[1] = scratch_0[2];
            scratch_0[2] = tmp;
        }
        {
            jxl_wasm128_lane tmp = scratch_1[1];
            scratch_1[1] = scratch_1[2];
            scratch_1[2] = tmp;
        }

        jxl_wasm128_dct4_inverse_4(scratch_0);
        jxl_wasm128_dct4_inverse_4(scratch_1);
        jxl_wasm128_transpose_lane(scratch_0);
        jxl_wasm128_transpose_lane(scratch_1);
        for (y = 0; y < 4; ++y) {
            float *row_ptr = jxl_subgrid_f32_row_mut(coeff, y);
            wasm_v128_store(row_ptr + 4, scratch_0[y]);
            row_ptr = jxl_subgrid_f32_row_mut(coeff, y + 4);
            wasm_v128_store(row_ptr + 4, scratch_1[y]);
        }
    } else {
        size_t y2;
        size_t y;
        for (y2 = 0; y2 < 4; ++y2) {
            const float *row_ptr = jxl_subgrid_f32_row_mut(coeff, y2 * 2);
            jxl_wasm128_lane a = wasm_v128_load(row_ptr);
            jxl_wasm128_lane b = wasm_v128_load(row_ptr + 4);
            jxl_wasm128_dct8_vec_inverse(a, b, &scratch_0[y2], &scratch_1[y2]);
        }

        jxl_wasm128_dct4_inverse_4(scratch_0);
        jxl_wasm128_dct4_inverse_4(scratch_1);
        for (y2 = 0; y2 < 4; ++y2) {
            float *row_ptr = jxl_subgrid_f32_row_mut(coeff, y2);
            wasm_v128_store(row_ptr, scratch_0[y2]);
            wasm_v128_store(row_ptr + 4, scratch_1[y2]);

            row_ptr = jxl_subgrid_f32_row_mut(coeff, y2 * 2 + 1);
            {
                jxl_wasm128_lane a = wasm_v128_load(row_ptr);
                jxl_wasm128_lane b = wasm_v128_load(row_ptr + 4);
                jxl_wasm128_dct8_vec_inverse(a, b, &scratch_0[y2], &scratch_1[y2]);
            }
        }

        jxl_wasm128_dct4_inverse_4(scratch_0);
        jxl_wasm128_dct4_inverse_4(scratch_1);
        for (y = 0; y < 4; ++y) {
            float *row_ptr = jxl_subgrid_f32_row_mut(coeff, y + 4);
            wasm_v128_store(row_ptr, scratch_0[y]);
            wasm_v128_store(row_ptr + 4, scratch_1[y]);
        }
    }
}

int jxl_render_transform_varblock_wasm128(jxl_allocator_state *alloc, jxl_subgrid_f32 coeff,
                                          jxl_transform_type dct_select) {
    switch (dct_select) {
    case JXL_TRANSFORM_DCT4:
        if (!subgrid_simd_eligible(coeff, 8, 8)) {
            return 0;
        }
        transform_dct4_wasm128(coeff);
        return 1;
    case JXL_TRANSFORM_DCT4X8:
        if (!subgrid_simd_eligible(coeff, 8, 8)) {
            return 0;
        }
        transform_dct4x8_wasm128(coeff, 0);
        return 1;
    case JXL_TRANSFORM_DCT8X4:
        if (!subgrid_simd_eligible(coeff, 8, 8)) {
            return 0;
        }
        transform_dct4x8_wasm128(coeff, 1);
        return 1;
    case JXL_TRANSFORM_DCT2:
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
        if (!jxl_dct_2d_wasm128(alloc, coeff, JXL_DCT_INVERSE)) {
            return 0;
        }
        return 1;
    }
}

#endif
