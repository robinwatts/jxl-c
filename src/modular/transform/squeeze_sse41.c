// SPDX-License-Identifier: MIT OR Apache-2.0
#include "squeeze_internal.h"
#include "allocator.h"

#include <smmintrin.h>
#include <emmintrin.h>
#include <string.h>
#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

#if defined(JXL_HAVE_SIMD_SSE41)
jxl_inline __m128i tendency_i16_x86_64_sse41(__m128i a, __m128i b, __m128i c) {
    __m128i a_b = _mm_sub_epi16(a, b);
    __m128i b_c = _mm_sub_epi16(b, c);
    __m128i a_c = _mm_sub_epi16(a, c);
    __m128i abs_a_b = _mm_abs_epi16(a_b);
    __m128i abs_b_c = _mm_abs_epi16(b_c);
    __m128i abs_a_c = _mm_abs_epi16(a_c);
    __m128i non_monotonic = _mm_cmpgt_epi16(_mm_setzero_si128(), _mm_xor_si128(a_b, b_c));
    __m128i skip = _mm_andnot_si128(_mm_cmpeq_epi16(a_b, _mm_setzero_si128()), non_monotonic);
    skip = _mm_andnot_si128(_mm_cmpeq_epi16(b_c, _mm_setzero_si128()), skip);

    __m128i abs_a_b_3 = _mm_mulhi_epi16(abs_a_b, _mm_set1_epi16(0x5556));
    __m128i x = _mm_add_epi16(abs_a_b_3, _mm_add_epi16(abs_a_c, _mm_set1_epi16(2)));
    x = _mm_srai_epi16(x, 2);

    __m128i abs_a_b_2_add_x = _mm_add_epi16(_mm_slli_epi16(abs_a_b, 1),
                                            _mm_and_si128(x, _mm_set1_epi16(1)));
    x = _mm_blendv_epi8(x,
                        _mm_add_epi16(_mm_slli_epi16(abs_a_b, 1), _mm_set1_epi16(1)),
                        _mm_cmpgt_epi16(x, abs_a_b_2_add_x));

    __m128i abs_b_c_2 = _mm_slli_epi16(abs_b_c, 1);
    x = _mm_blendv_epi8(
        x, abs_b_c_2,
        _mm_cmpgt_epi16(_mm_add_epi16(x, _mm_and_si128(x, _mm_set1_epi16(1))), abs_b_c_2));

    __m128i need_neg = _mm_cmpgt_epi16(c, a);
    __m128i mask = _mm_andnot_si128(skip, _mm_or_si128(_mm_slli_epi16(need_neg, 1), _mm_set1_epi16(1)));
    return _mm_sign_epi16(x, mask);
}

jxl_inline void transpose_i16x8(const __m128i in[8], __m128i out[8]) {
    __m128i t[8];

    t[0] = _mm_unpacklo_epi16(in[0], in[1]);
    t[1] = _mm_unpacklo_epi16(in[2], in[3]);
    t[2] = _mm_unpacklo_epi16(in[4], in[5]);
    t[3] = _mm_unpacklo_epi16(in[6], in[7]);
    t[4] = _mm_unpackhi_epi16(in[0], in[1]);
    t[5] = _mm_unpackhi_epi16(in[2], in[3]);
    t[6] = _mm_unpackhi_epi16(in[4], in[5]);
    t[7] = _mm_unpackhi_epi16(in[6], in[7]);

    in = t;
    t[0] = _mm_unpacklo_epi32(in[0], in[1]);
    t[1] = _mm_unpacklo_epi32(in[2], in[3]);
    t[2] = _mm_unpacklo_epi32(in[4], in[5]);
    t[3] = _mm_unpacklo_epi32(in[6], in[7]);
    t[4] = _mm_unpackhi_epi32(in[0], in[1]);
    t[5] = _mm_unpackhi_epi32(in[2], in[3]);
    t[6] = _mm_unpackhi_epi32(in[4], in[5]);
    t[7] = _mm_unpackhi_epi32(in[6], in[7]);

    in = t;
    out[0] = _mm_unpacklo_epi64(in[0], in[1]);
    out[1] = _mm_unpackhi_epi64(in[0], in[1]);
    out[2] = _mm_unpacklo_epi64(in[4], in[5]);
    out[3] = _mm_unpackhi_epi64(in[4], in[5]);
    out[4] = _mm_unpacklo_epi64(in[2], in[3]);
    out[5] = _mm_unpackhi_epi64(in[2], in[3]);
    out[6] = _mm_unpacklo_epi64(in[6], in[7]);
    out[7] = _mm_unpackhi_epi64(in[6], in[7]);
}

static void inverse_h_i16_x86_64_sse41(jxl_allocator_state *alloc, int16_t *merged, size_t width,
                                       size_t height, size_t row_stride) {
                                           size_t y8;
    if (row_stride == 0) {
        row_stride = width;
    }
    if (width <= 16) {
        jxl_squeeze_inverse_h_i16_base(alloc, merged, width, height, row_stride);
        return;
    }

    __m128i *scratch =
        (__m128i *)jxl_alloc_aligned(alloc, JXL_ALLOC_ALIGN_SIMD128, width * sizeof(*scratch));
    if (scratch == NULL) {
        return;
    }
    const size_t avg_width = (width + 1) / 2;
    const size_t h8 = height / 8;

    for (y8 = 0; y8 < h8; ++y8) {
        size_t dy;
        size_t x8;
        size_t dx;
        const size_t y = y8 * 8;
        int16_t *rows[8];
        for (dy = 0; dy < 8; ++dy) {
            rows[dy] = merged + (y + dy) * row_stride;
        }

        __m128i avg = _mm_setr_epi16(rows[0][0], rows[1][0], rows[2][0], rows[3][0], rows[4][0], rows[5][0],
                                     rows[6][0], rows[7][0]);
        __m128i left = avg;

        for (x8 = 0; x8 < (avg_width - 1) / 8; ++x8) {
            size_t dy;
            size_t dx;
            const size_t x = x8 * 8 + 1;
            __m128i in_avg[8];
            __m128i in_residual[8];
            __m128i avgs[8];
            __m128i residuals[8];
            for (dy = 0; dy < 8; ++dy) {
                in_avg[dy] = _mm_loadu_si128((const __m128i *)(rows[dy] + x));
                in_residual[dy] = _mm_loadu_si128((const __m128i *)(rows[dy] + (avg_width - 1 + x)));
            }
            transpose_i16x8(in_avg, avgs);
            transpose_i16x8(in_residual, residuals);

            for (dx = 0; dx < 8; ++dx) {
                __m128i residual = residuals[dx];
                __m128i next_avg = avgs[dx];
                __m128i diff = _mm_add_epi16(residual, tendency_i16_x86_64_sse41(left, avg, next_avg));
                __m128i diff_2 = _mm_srai_epi16(_mm_add_epi16(diff, _mm_srli_epi16(diff, 15)), 1);
                __m128i first = _mm_add_epi16(avg, diff_2);
                __m128i second = _mm_sub_epi16(first, diff);
                scratch[x8 * 16 + dx * 2] = first;
                scratch[x8 * 16 + dx * 2 + 1] = second;
                avg = next_avg;
                left = second;
            }
        }

        if (((avg_width - 1) % 8) != 0 || (width % 2) == 0) {
            size_t dy;
            size_t i;
            __m128i in_avg[8];
            __m128i in_residual[8];
            __m128i avgs[8];
            __m128i residuals[8];
            size_t from;
            for (dy = 0; dy < 8; ++dy) {
                in_avg[dy] = _mm_loadu_si128((const __m128i *)(rows[dy] + (avg_width - 8)));
                in_residual[dy] = _mm_loadu_si128((const __m128i *)(rows[dy] + (width - 8)));
            }
            transpose_i16x8(in_avg, avgs);
            transpose_i16x8(in_residual, residuals);

            if ((width % 2) == 0) {
                size_t i;
                __m128i shifted[8];
                for (i = 0; i < 8; ++i) {
                    shifted[i] = (i == 7) ? avgs[7] : avgs[i + 1];
                }
                memcpy(avgs, shifted, sizeof(avgs));
            }

            from = (~(width / 2) + 1) % 8;
            for (i = from; i < 8; ++i) {
                dx = 8 - i;
                __m128i residual = residuals[i];
                __m128i next_avg = avgs[i];
                __m128i diff = _mm_add_epi16(residual, tendency_i16_x86_64_sse41(left, avg, next_avg));
                __m128i diff_2 = _mm_srai_epi16(_mm_add_epi16(diff, _mm_srli_epi16(diff, 15)), 1);
                __m128i first = _mm_add_epi16(avg, diff_2);
                __m128i second = _mm_sub_epi16(first, diff);
                scratch[width / 2 * 2 - dx * 2] = first;
                scratch[width / 2 * 2 - dx * 2 + 1] = second;
                avg = next_avg;
                left = second;
            }
        }

        if ((width % 2) == 1) {
            scratch[width - 1] = avg;
        }

        x8 = 0;
        for (; x8 < width / 8; ++x8) {
            size_t i;
            size_t dy;
            const size_t x = x8 * 8;
            __m128i chunk[8];
            __m128i cols[8];
            for (i = 0; i < 8; ++i) {
                chunk[i] = scratch[x + i];
            }
            transpose_i16x8(chunk, cols);
            for (dy = 0; dy < 8; ++dy) {
                _mm_storeu_si128((__m128i *)(rows[dy] + x), cols[dy]);
            }
        }

        for (dx = 0; dx < (width % 8); ++dx) {
            size_t dy;
            size_t x = (width / 8) * 8 + dx;
            int16_t lanes[8];
            _mm_storeu_si128((__m128i *)lanes, scratch[x]);
            for (dy = 0; dy < 8; ++dy) {
                rows[dy][x] = lanes[dy];
            }
        }
    }

    if ((height % 8) != 0) {
        jxl_squeeze_inverse_h_i16_base(alloc, merged + h8 * 8 * row_stride, width, height - h8 * 8, row_stride);
    }
    jxl_free_aligned(alloc, scratch);
}

static void inverse_v_i16_x86_64_sse41(jxl_allocator_state *alloc, int16_t *merged, size_t width,
                                       size_t height, size_t row_stride) {
                                           size_t x8;
    if (row_stride == 0) {
        row_stride = width;
    }
    if (height <= 1) {
        return;
    }

    __m128i *scratch =
        (__m128i *)jxl_alloc_aligned(alloc, JXL_ALLOC_ALIGN_SIMD128, height * sizeof(*scratch));
    if (scratch == NULL) {
        return;
    }

    const size_t avg_height = (height + 1) / 2;
    const size_t w8 = width / 8;

    for (x8 = 0; x8 < w8; ++x8) {
        size_t y;
        const size_t x = x8 * 8;

        __m128i avg = _mm_loadu_si128((const __m128i *)(merged + x));
        __m128i top = avg;
        size_t half = height / 2;
        for (y = 0; y < half; ++y) {
            __m128i residual = _mm_loadu_si128((const __m128i *)(merged + (avg_height + y) * row_stride + x));
            __m128i next_avg = (y + 1 < avg_height)
                                   ? _mm_loadu_si128((const __m128i *)(merged + (y + 1) * row_stride + x))
                                   : avg;
            __m128i diff = _mm_add_epi16(residual, tendency_i16_x86_64_sse41(top, avg, next_avg));
            __m128i diff_2 = _mm_srai_epi16(_mm_add_epi16(diff, _mm_srli_epi16(diff, 15)), 1);
            __m128i first = _mm_add_epi16(avg, diff_2);
            __m128i second = _mm_sub_epi16(first, diff);
            scratch[2 * y] = first;
            scratch[2 * y + 1] = second;
            avg = next_avg;
            top = second;
        }

        if ((height % 2) == 1) {
            scratch[height - 1] = avg;
        }

        for (y = 0; y < height; ++y) {
            _mm_storeu_si128((__m128i *)(merged + y * row_stride + x), scratch[y]);
        }
    }

    if ((width % 8) != 0) {
        jxl_squeeze_inverse_v_i16_base(alloc, merged + w8 * 8, width - w8 * 8, height, row_stride);
    }
    jxl_free_aligned(alloc, scratch);
}

void jxl_squeeze_inverse_h_i16_x86_sse41(jxl_allocator_state *alloc, int16_t *merged, size_t width,
                                         size_t height, size_t row_stride) {
    inverse_h_i16_x86_64_sse41(alloc, merged, width, height, row_stride);
}

void jxl_squeeze_inverse_v_i16_x86_sse41(jxl_allocator_state *alloc, int16_t *merged, size_t width,
                                         size_t height, size_t row_stride) {
    inverse_v_i16_x86_64_sse41(alloc, merged, width, height, row_stride);
}
#endif
