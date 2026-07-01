// SPDX-License-Identifier: MIT OR Apache-2.0
#include "squeeze_internal.h"
#include "allocator.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

#if defined(JXL_HAVE_SIMD_WASM128)

#include <wasm_simd128.h>

typedef v128_t jxl_i16x8;

static jxl_i16x8 tendency_i16_wasm128(jxl_i16x8 a, jxl_i16x8 b, jxl_i16x8 c) {
    const jxl_i16x8 zero = wasm_i16x8_splat(0);
    const jxl_i16x8 one = wasm_i16x8_splat(1);
    jxl_i16x8 a_b = wasm_i16x8_sub(a, b);
    jxl_i16x8 b_c = wasm_i16x8_sub(b, c);
    jxl_i16x8 a_c = wasm_i16x8_sub(a, c);
    jxl_i16x8 abs_a_b = wasm_i16x8_abs(a_b);
    jxl_i16x8 abs_b_c = wasm_i16x8_abs(b_c);
    jxl_i16x8 abs_a_c = wasm_i16x8_abs(a_c);

    jxl_i16x8 monotonic = wasm_i16x8_ge(wasm_v128_xor(a_b, b_c), zero);
    jxl_i16x8 no_skip = wasm_v128_or(monotonic, wasm_i16x8_eq(a_b, zero));
    no_skip = wasm_v128_or(no_skip, wasm_i16x8_eq(b_c, zero));

    const v128_t mul_const = wasm_i32x4_splat(0x5556);
    v128_t mul_low = wasm_i32x4_mul(wasm_i32x4_extend_low_i16x8(abs_a_b), mul_const);
    v128_t mul_high = wasm_i32x4_mul(wasm_i32x4_extend_high_i16x8(abs_a_b), mul_const);
    jxl_i16x8 abs_a_b_3 = wasm_i16x8_shuffle(mul_low, mul_high, 1, 3, 5, 7, 9, 11, 13, 15);

    jxl_i16x8 x =
        wasm_i16x8_shr(wasm_i16x8_add(abs_a_b_3, wasm_i16x8_add(abs_a_c, wasm_i16x8_splat(2))), 2);

    jxl_i16x8 abs_a_b_2_add_x =
        wasm_i16x8_add(wasm_i16x8_shl(abs_a_b, 1), wasm_v128_and(x, one));
    x = wasm_v128_bitselect(wasm_i16x8_add(wasm_i16x8_shl(abs_a_b, 1), one), x,
                            wasm_i16x8_gt(x, abs_a_b_2_add_x));

    jxl_i16x8 abs_b_c_2 = wasm_i16x8_shl(abs_b_c, 1);
    x = wasm_v128_bitselect(abs_b_c_2, x,
                            wasm_i16x8_gt(wasm_i16x8_add(x, wasm_v128_and(x, one)), abs_b_c_2));

    jxl_i16x8 need_neg = wasm_i16x8_lt(a_c, zero);
    jxl_i16x8 neg_x = wasm_i16x8_neg(x);
    x = wasm_v128_bitselect(neg_x, x, need_neg);
    return wasm_v128_and(no_skip, x);
}

static jxl_i16x8 diff_half_i16(jxl_i16x8 diff) {
    return wasm_i16x8_shr(wasm_i16x8_add(diff, wasm_u16x8_shr(diff, 15)), 1);
}

static void transpose_i16x8_wasm128(const jxl_i16x8 vs_in[8], jxl_i16x8 out[8]) {
    jxl_i16x8 vs[8];
    vs[0] = wasm_i16x8_shuffle(vs_in[0], vs_in[1], 0, 8, 1, 9, 2, 10, 3, 11);
    vs[1] = wasm_i16x8_shuffle(vs_in[2], vs_in[3], 0, 8, 1, 9, 2, 10, 3, 11);
    vs[2] = wasm_i16x8_shuffle(vs_in[4], vs_in[5], 0, 8, 1, 9, 2, 10, 3, 11);
    vs[3] = wasm_i16x8_shuffle(vs_in[6], vs_in[7], 0, 8, 1, 9, 2, 10, 3, 11);
    vs[4] = wasm_i16x8_shuffle(vs_in[0], vs_in[1], 4, 12, 5, 13, 6, 14, 7, 15);
    vs[5] = wasm_i16x8_shuffle(vs_in[2], vs_in[3], 4, 12, 5, 13, 6, 14, 7, 15);
    vs[6] = wasm_i16x8_shuffle(vs_in[4], vs_in[5], 4, 12, 5, 13, 6, 14, 7, 15);
    vs[7] = wasm_i16x8_shuffle(vs_in[6], vs_in[7], 4, 12, 5, 13, 6, 14, 7, 15);

    vs[0] = wasm_i32x4_shuffle(vs[0], vs[1], 0, 4, 1, 5);
    vs[1] = wasm_i32x4_shuffle(vs[2], vs[3], 0, 4, 1, 5);
    vs[2] = wasm_i32x4_shuffle(vs[4], vs[5], 0, 4, 1, 5);
    vs[3] = wasm_i32x4_shuffle(vs[6], vs[7], 0, 4, 1, 5);
    vs[4] = wasm_i32x4_shuffle(vs[0], vs[1], 2, 6, 3, 7);
    vs[5] = wasm_i32x4_shuffle(vs[2], vs[3], 2, 6, 3, 7);
    vs[6] = wasm_i32x4_shuffle(vs[4], vs[5], 2, 6, 3, 7);
    vs[7] = wasm_i32x4_shuffle(vs[6], vs[7], 2, 6, 3, 7);

    out[0] = wasm_i64x2_shuffle(vs[0], vs[1], 0, 2);
    out[1] = wasm_i64x2_shuffle(vs[0], vs[1], 1, 3);
    out[2] = wasm_i64x2_shuffle(vs[4], vs[5], 0, 2);
    out[3] = wasm_i64x2_shuffle(vs[4], vs[5], 1, 3);
    out[4] = wasm_i64x2_shuffle(vs[2], vs[3], 0, 2);
    out[5] = wasm_i64x2_shuffle(vs[2], vs[3], 1, 3);
    out[6] = wasm_i64x2_shuffle(vs[6], vs[7], 0, 2);
    out[7] = wasm_i64x2_shuffle(vs[6], vs[7], 1, 3);
}

static void inverse_h_i16_wasm128(jxl_allocator_state *alloc, int16_t *merged, size_t width,
                                  size_t height, size_t row_stride) {
    size_t y8;
    if (width <= 8) {
        jxl_squeeze_inverse_h_i16_base(alloc, merged, width, height, row_stride);
        return;
    }

    jxl_i16x8 *scratch =
        (jxl_i16x8 *)jxl_alloc_aligned(alloc, JXL_ALLOC_ALIGN_SIMD128, width * sizeof(*scratch));
    if (scratch == NULL) {
        return;
    }

    const size_t avg_width = (width + 1) / 2;
    const size_t h8 = height / 8;

    for (y8 = 0; y8 < h8; ++y8) {
        size_t dy;
        size_t x8;
        size_t x;
        size_t idx;
        int16_t *rows[8];
        const size_t y = y8 * 8;

        for (dy = 0; dy < 8; ++dy) {
            rows[dy] = merged + (y + dy) * row_stride;
        }

        jxl_i16x8 avg = wasm_i16x8_make(rows[0][0], rows[1][0], rows[2][0], rows[3][0], rows[4][0],
                                        rows[5][0], rows[6][0], rows[7][0]);
        jxl_i16x8 left = avg;

        for (x8 = 0; x8 < (avg_width - 1) / 8; ++x8) {
            jxl_i16x8 avgs_in[8];
            jxl_i16x8 residuals_in[8];
            jxl_i16x8 avgs[8];
            jxl_i16x8 residuals[8];
            size_t dx;
            x = x8 * 8 + 1;
            for (idx = 0; idx < 8; ++idx) {
                avgs_in[idx] = wasm_v128_load((const v128_t *)(rows[idx] + x));
                residuals_in[idx] =
                    wasm_v128_load((const v128_t *)(rows[idx] + avg_width - 1 + x));
            }
            transpose_i16x8_wasm128(avgs_in, avgs);
            transpose_i16x8_wasm128(residuals_in, residuals);

            for (dx = 0; dx < 8; ++dx) {
                jxl_i16x8 diff =
                    wasm_i16x8_add(residuals[dx], tendency_i16_wasm128(left, avg, avgs[dx]));
                jxl_i16x8 first = wasm_i16x8_add(avg, diff_half_i16(diff));
                jxl_i16x8 second = wasm_i16x8_sub(first, diff);
                scratch[x8 * 16 + dx * 2] = first;
                scratch[x8 * 16 + dx * 2 + 1] = second;
                avg = avgs[dx];
                left = second;
            }
        }

        if (((avg_width - 1) % 8) != 0 || (width % 2) == 0) {
            jxl_i16x8 avgs_in[8];
            jxl_i16x8 avgs[8];
            jxl_i16x8 residuals_in[8];
            jxl_i16x8 residuals[8];
            size_t from;
            for (idx = 0; idx < 8; ++idx) {
                avgs_in[idx] = wasm_v128_load((const v128_t *)(rows[idx] + avg_width - 8));
                residuals_in[idx] = wasm_v128_load((const v128_t *)(rows[idx] + width - 8));
            }
            transpose_i16x8_wasm128(avgs_in, avgs);
            if ((width % 2) == 0) {
                jxl_i16x8 shifted[8];
                for (idx = 0; idx < 8; ++idx) {
                    shifted[idx] = (idx == 7) ? avgs[7] : avgs[idx + 1];
                }
                for (idx = 0; idx < 8; ++idx) {
                    avgs[idx] = shifted[idx];
                }
            }
            transpose_i16x8_wasm128(residuals_in, residuals);
            from = ((~(width / 2)) + 1) % 8;
            for (idx = from; idx < 8; ++idx) {
                size_t tail_dx = 8 - idx;
                jxl_i16x8 diff =
                    wasm_i16x8_add(residuals[idx], tendency_i16_wasm128(left, avg, avgs[idx]));
                jxl_i16x8 first = wasm_i16x8_add(avg, diff_half_i16(diff));
                jxl_i16x8 second = wasm_i16x8_sub(first, diff);
                scratch[width / 2 * 2 - tail_dx * 2] = first;
                scratch[width / 2 * 2 - tail_dx * 2 + 1] = second;
                avg = avgs[idx];
                left = second;
            }
        }

        if ((width % 2) == 1) {
            scratch[width - 1] = avg;
        }

        for (x = 0; x + 8 <= width; x += 8) {
            jxl_i16x8 block_in[8];
            jxl_i16x8 block_out[8];
            for (idx = 0; idx < 8; ++idx) {
                block_in[idx] = scratch[x + idx];
            }
            transpose_i16x8_wasm128(block_in, block_out);
            for (dy = 0; dy < 8; ++dy) {
                wasm_v128_store((v128_t *)(rows[dy] + x), block_out[dy]);
            }
        }
        for (; x < width; ++x) {
            jxl_i16x8 v = scratch[x];
            rows[0][x] = wasm_i16x8_extract_lane(v, 0);
            rows[1][x] = wasm_i16x8_extract_lane(v, 1);
            rows[2][x] = wasm_i16x8_extract_lane(v, 2);
            rows[3][x] = wasm_i16x8_extract_lane(v, 3);
            rows[4][x] = wasm_i16x8_extract_lane(v, 4);
            rows[5][x] = wasm_i16x8_extract_lane(v, 5);
            rows[6][x] = wasm_i16x8_extract_lane(v, 6);
            rows[7][x] = wasm_i16x8_extract_lane(v, 7);
        }
    }

    if ((height % 8) != 0) {
        jxl_squeeze_inverse_h_i16_base(alloc, merged + h8 * 8 * row_stride, width, height - h8 * 8,
                                       row_stride);
    }

    jxl_free_aligned(alloc, scratch);
}

static void inverse_v_i16_wasm128(jxl_allocator_state *alloc, int16_t *merged, size_t width,
                                  size_t height, size_t row_stride) {
    size_t x8;
    if (height <= 1) {
        return;
    }

    jxl_i16x8 *scratch =
        (jxl_i16x8 *)jxl_alloc_aligned(alloc, JXL_ALLOC_ALIGN_SIMD128, height * sizeof(*scratch));
    if (scratch == NULL) {
        return;
    }

    const size_t avg_height = (height + 1) / 2;
    const size_t w8 = width / 8;

    for (x8 = 0; x8 < w8; ++x8) {
        size_t y;
        const size_t x = x8 * 8;
        const size_t half = height / 2;

        jxl_i16x8 avg = wasm_v128_load((const v128_t *)(merged + x));
        jxl_i16x8 top = avg;

        for (y = 0; y < half; ++y) {
            jxl_i16x8 residual =
                wasm_v128_load((const v128_t *)(merged + (avg_height + y) * row_stride + x));
            jxl_i16x8 next_avg = (y + 1 < avg_height)
                                     ? wasm_v128_load(
                                           (const v128_t *)(merged + (y + 1) * row_stride + x))
                                     : avg;
            jxl_i16x8 diff =
                wasm_i16x8_add(residual, tendency_i16_wasm128(top, avg, next_avg));
            jxl_i16x8 first = wasm_i16x8_add(avg, diff_half_i16(diff));
            jxl_i16x8 second = wasm_i16x8_sub(first, diff);
            scratch[2 * y] = first;
            scratch[2 * y + 1] = second;
            avg = next_avg;
            top = second;
        }

        if ((height % 2) == 1) {
            scratch[height - 1] = avg;
        }

        for (y = 0; y < height; ++y) {
            wasm_v128_store((v128_t *)(merged + y * row_stride + x), scratch[y]);
        }
    }

    if ((width % 8) != 0) {
        jxl_squeeze_inverse_v_i16_base(alloc, merged + w8 * 8, width - w8 * 8, height, row_stride);
    }

    jxl_free_aligned(alloc, scratch);
}

void jxl_squeeze_inverse_h_i16_wasm128(jxl_allocator_state *alloc, int16_t *merged, size_t width,
                                       size_t height, size_t row_stride) {
    if (row_stride == 0) {
        row_stride = width;
    }
    inverse_h_i16_wasm128(alloc, merged, width, height, row_stride);
}

void jxl_squeeze_inverse_v_i16_wasm128(jxl_allocator_state *alloc, int16_t *merged, size_t width,
                                       size_t height, size_t row_stride) {
    if (row_stride == 0) {
        row_stride = width;
    }
    inverse_v_i16_wasm128(alloc, merged, width, height, row_stride);
}

#endif
