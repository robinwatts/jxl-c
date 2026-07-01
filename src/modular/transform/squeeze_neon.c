// SPDX-License-Identifier: MIT OR Apache-2.0
#include "squeeze_internal.h"
#include "allocator.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"
#include <string.h>

#if defined(JXL_HAVE_SIMD_NEON)

#include <arm_neon.h>

jxl_inline int16x4_t tendency_i16_neon(int16x4_t a, int16x4_t b, int16x4_t c) {
    const int16x4_t a_b = vsub_s16(a, b);
    const int16x4_t b_c = vsub_s16(b, c);
    const int16x4_t a_c = vsub_s16(a, c);
    const int16x4_t abs_a_b = vabs_s16(a_b);
    const int16x4_t abs_b_c = vabs_s16(b_c);
    const int16x4_t abs_a_c = vabs_s16(a_c);
    const uint16x4_t monotonic = vcgez_s16(veor_s16(a_b, b_c));
    uint16x4_t no_skip = vorr_u16(monotonic, vceqz_s16(a_b));
    no_skip = vorr_u16(no_skip, vceqz_s16(b_c));
    const int16x4_t no_skip_s16 = vreinterpret_s16_u16(no_skip);

    const int16x8_t abs_a_b_3_merged = vreinterpretq_s16_s32(vmull_n_s16(abs_a_b, 0x5556));
    const int16x4_t abs_a_b_3 =
        vuzp2_s16(vget_low_s16(abs_a_b_3_merged), vget_high_s16(abs_a_b_3_merged));

    int16x4_t x = vshr_n_s16(vadd_s16(abs_a_b_3, vadd_s16(abs_a_c, vdup_n_s16(2))), 2);

    const int16x4_t abs_a_b_2_add_x =
        vadd_s16(vshl_n_s16(abs_a_b, 1), vand_s16(x, vdup_n_s16(1)));
    x = vbsl_s16(vcgt_s16(x, abs_a_b_2_add_x), vadd_s16(vshl_n_s16(abs_a_b, 1), vdup_n_s16(1)), x);

    const int16x4_t abs_b_c_2 = vshl_n_s16(abs_b_c, 1);
    x = vbsl_s16(vcgt_s16(vadd_s16(x, vand_s16(x, vdup_n_s16(1))), abs_b_c_2), abs_b_c_2, x);

    const uint16x4_t need_neg = vcltz_s16(a_c);
    x = vbsl_s16(vreinterpret_s16_u16(need_neg), vneg_s16(x), x);
    return vand_s16(no_skip_s16, x);
}

jxl_inline void transpose_i16x4(const int16x4_t in[4], int16x4_t out[4]) {
    const int16x4x2_t tr01 = vtrn_s16(in[0], in[1]);
    const int16x4x2_t tr23 = vtrn_s16(in[2], in[3]);
    const int32x2x2_t o02 =
        vtrn_s32(vreinterpret_s32_s16(tr01.val[0]), vreinterpret_s32_s16(tr23.val[0]));
    const int32x2x2_t o13 =
        vtrn_s32(vreinterpret_s32_s16(tr01.val[1]), vreinterpret_s32_s16(tr23.val[1]));
    out[0] = vreinterpret_s16_s32(o02.val[0]);
    out[1] = vreinterpret_s16_s32(o13.val[0]);
    out[2] = vreinterpret_s16_s32(o02.val[1]);
    out[3] = vreinterpret_s16_s32(o13.val[1]);
}

jxl_inline int16x4_t diff_half_i16(int16x4_t diff) {
    return vshr_n_s16(
        vadd_s16(diff, vreinterpret_s16_u16(vshr_n_u16(vreinterpret_u16_s16(diff), 15))), 1);
}

static void inverse_h_i16_neon(jxl_allocator_state *alloc, int16_t *merged, size_t width,
                               size_t height, size_t row_stride) {
                                   size_t y4;
    if (row_stride == 0) {
        row_stride = width;
    }
    if (width <= 8) {
        jxl_squeeze_inverse_h_i16_base(alloc, merged, width, height, row_stride);
        return;
    }

    int16x4_t *scratch =
        (int16x4_t *)jxl_alloc_aligned(alloc, JXL_ALLOC_ALIGN_SIMD128, width * sizeof(*scratch));
    if (scratch == NULL) {
        return;
    }
    const size_t avg_width = (width + 1) / 2;
    const size_t h4 = height / 4;

    for (y4 = 0; y4 < h4; ++y4) {
        size_t dy;
        size_t x4;
        size_t dx;
        const size_t y = y4 * 4;
        int16_t *rows[4];
        for (dy = 0; dy < 4; ++dy) {
            rows[dy] = merged + (y + dy) * row_stride;
        }

        int16x4_t avg = vld1_lane_s16(rows[0], vdup_n_s16(0), 0);
        avg = vld1_lane_s16(rows[1], avg, 1);
        avg = vld1_lane_s16(rows[2], avg, 2);
        avg = vld1_lane_s16(rows[3], avg, 3);
        int16x4_t left = avg;

        for (x4 = 0; x4 < (avg_width - 1) / 4; ++x4) {
            size_t dy;
            size_t dx;
            const size_t x = x4 * 4 + 1;
            int16x4_t in_avg[4];
            int16x4_t in_residual[4];
            int16x4_t avgs[4];
            int16x4_t residuals[4];
            for (dy = 0; dy < 4; ++dy) {
                in_avg[dy] = vld1_s16(rows[dy] + x);
                in_residual[dy] = vld1_s16(rows[dy] + (avg_width - 1 + x));
            }
            transpose_i16x4(in_avg, avgs);
            transpose_i16x4(in_residual, residuals);

            for (dx = 0; dx < 4; ++dx) {
                const int16x4_t residual = residuals[dx];
                const int16x4_t next_avg = avgs[dx];
                const int16x4_t diff =
                    vadd_s16(residual, tendency_i16_neon(left, avg, next_avg));
                const int16x4_t first = vadd_s16(avg, diff_half_i16(diff));
                const int16x4_t second = vsub_s16(first, diff);
                scratch[x4 * 8 + dx * 2] = first;
                scratch[x4 * 8 + dx * 2 + 1] = second;
                avg = next_avg;
                left = second;
            }
        }

        if (((avg_width - 1) % 4) != 0 || (width % 2) == 0) {
            size_t dy;
            size_t i;
            int16x4_t in_avg[4];
            int16x4_t in_residual[4];
            int16x4_t avgs[4];
            int16x4_t residuals[4];
            for (dy = 0; dy < 4; ++dy) {
                in_avg[dy] = vld1_s16(rows[dy] + (avg_width - 4));
                in_residual[dy] = vld1_s16(rows[dy] + (width - 4));
            }
            transpose_i16x4(in_avg, avgs);
            transpose_i16x4(in_residual, residuals);

            if ((width % 2) == 0) {
                avgs[0] = avgs[1];
                avgs[1] = avgs[2];
                avgs[2] = avgs[3];
                avgs[3] = avgs[3];
            }

            const size_t from = (~(width / 2) + 1) % 4;
            for (i = from; i < 4; ++i) {
                dx = 4 - i;
                const int16x4_t residual = residuals[i];
                const int16x4_t next_avg = avgs[i];
                const int16x4_t diff =
                    vadd_s16(residual, tendency_i16_neon(left, avg, next_avg));
                const int16x4_t first = vadd_s16(avg, diff_half_i16(diff));
                const int16x4_t second = vsub_s16(first, diff);
                scratch[width / 2 * 2 - dx * 2] = first;
                scratch[width / 2 * 2 - dx * 2 + 1] = second;
                avg = next_avg;
                left = second;
            }
        }

        if ((width % 2) == 1) {
            scratch[width - 1] = avg;
        }

        x4 = 0;
        for (; x4 < width / 4; ++x4) {
            size_t dy;
            const size_t x = x4 * 4;
            int16x4_t chunk[4];
            chunk[0] = scratch[x];
            chunk[1] = scratch[x + 1];
            chunk[2] = scratch[x + 2];
            chunk[3] = scratch[x + 3];

            int16x4_t cols[4];
            transpose_i16x4(chunk, cols);
            for (dy = 0; dy < 4; ++dy) {
                vst1_s16(rows[dy] + x, cols[dy]);
            }
        }

        for (dx = 0; dx < (width % 4); ++dx) {
            const size_t x = (width / 4) * 4 + dx;
            const int16x4_t v = scratch[x];
            rows[0][x] = vget_lane_s16(v, 0);
            rows[1][x] = vget_lane_s16(v, 1);
            rows[2][x] = vget_lane_s16(v, 2);
            rows[3][x] = vget_lane_s16(v, 3);
        }
    }

    if ((height % 4) != 0) {
        jxl_squeeze_inverse_h_i16_base(alloc, merged + h4 * 4 * row_stride, width, height - h4 * 4,
                                       row_stride);
    }
    jxl_free_aligned(alloc, scratch);
}

static void inverse_v_i16_neon(jxl_allocator_state *alloc, int16_t *merged, size_t width,
                               size_t height, size_t row_stride) {
                                   size_t x4;
    if (row_stride == 0) {
        row_stride = width;
    }
    if (height <= 1) {
        return;
    }

    int16x4_t *scratch =
        (int16x4_t *)jxl_alloc_aligned(alloc, JXL_ALLOC_ALIGN_SIMD128, height * sizeof(*scratch));
    if (scratch == NULL) {
        return;
    }

    const size_t avg_height = (height + 1) / 2;
    const size_t w4 = width / 4;

    for (x4 = 0; x4 < w4; ++x4) {
        size_t y;
        const size_t x = x4 * 4;

        int16x4_t avg = vld1_s16(merged + x);
        int16x4_t top = avg;
        const size_t half = height / 2;
        for (y = 0; y < half; ++y) {
            const int16x4_t residual =
                vld1_s16(merged + (avg_height + y) * row_stride + x);
            const int16x4_t next_avg = (y + 1 < avg_height)
                                          ? vld1_s16(merged + (y + 1) * row_stride + x)
                                          : avg;
            const int16x4_t diff = vadd_s16(residual, tendency_i16_neon(top, avg, next_avg));
            const int16x4_t first = vadd_s16(avg, diff_half_i16(diff));
            const int16x4_t second = vsub_s16(first, diff);
            scratch[2 * y] = first;
            scratch[2 * y + 1] = second;
            avg = next_avg;
            top = second;
        }

        if ((height % 2) == 1) {
            scratch[height - 1] = avg;
        }

        for (y = 0; y < height; ++y) {
            vst1_s16(merged + y * row_stride + x, scratch[y]);
        }
    }

    if ((width % 4) != 0) {
        jxl_squeeze_inverse_v_i16_base(alloc, merged + w4 * 4, width - w4 * 4, height, row_stride);
    }
    jxl_free_aligned(alloc, scratch);
}

void jxl_squeeze_inverse_h_i16_neon(jxl_allocator_state *alloc, int16_t *merged, size_t width,
                                    size_t height, size_t row_stride) {
    inverse_h_i16_neon(alloc, merged, width, height, row_stride);
}

void jxl_squeeze_inverse_v_i16_neon(jxl_allocator_state *alloc, int16_t *merged, size_t width,
                                    size_t height, size_t row_stride) {
    inverse_v_i16_neon(alloc, merged, width, height, row_stride);
}

#endif
