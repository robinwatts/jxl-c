// SPDX-License-Identifier: MIT OR Apache-2.0
#include "squeeze_internal.h"
#include "allocator.h"

#include <immintrin.h>
#include <smmintrin.h>
#include <emmintrin.h>
#include <string.h>
#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

#if defined(JXL_HAVE_SIMD_AVX2)

static __m256i tendency_i16_x86_64_avx2(__m256i a, __m256i b, __m256i c) {
    __m256i a_b = _mm256_sub_epi16(a, b);
    __m256i b_c = _mm256_sub_epi16(b, c);
    __m256i a_c = _mm256_sub_epi16(a, c);
    __m256i abs_a_b = _mm256_abs_epi16(a_b);
    __m256i abs_b_c = _mm256_abs_epi16(b_c);
    __m256i abs_a_c = _mm256_abs_epi16(a_c);
    __m256i non_monotonic = _mm256_cmpgt_epi16(_mm256_setzero_si256(), _mm256_xor_si256(a_b, b_c));
    __m256i skip = _mm256_andnot_si256(_mm256_cmpeq_epi16(a_b, _mm256_setzero_si256()), non_monotonic);
    skip = _mm256_andnot_si256(_mm256_cmpeq_epi16(b_c, _mm256_setzero_si256()), skip);

    __m256i abs_a_b_3 = _mm256_mulhi_epi16(abs_a_b, _mm256_set1_epi16(0x5556));
    __m256i x = _mm256_add_epi16(abs_a_b_3, _mm256_add_epi16(abs_a_c, _mm256_set1_epi16(2)));
    x = _mm256_srai_epi16(x, 2);

    __m256i abs_a_b_2_add_x = _mm256_add_epi16(_mm256_slli_epi16(abs_a_b, 1), _mm256_and_si256(x, _mm256_set1_epi16(1)));
    x = _mm256_blendv_epi8(
        x,
        _mm256_add_epi16(_mm256_slli_epi16(abs_a_b, 1), _mm256_set1_epi16(1)),
        _mm256_cmpgt_epi16(x, abs_a_b_2_add_x));

    __m256i abs_b_c_2 = _mm256_slli_epi16(abs_b_c, 1);
    x = _mm256_blendv_epi8(
        x,
        abs_b_c_2,
        _mm256_cmpgt_epi16(
            _mm256_add_epi16(x, _mm256_and_si256(x, _mm256_set1_epi16(1))),
            abs_b_c_2));

    __m256i need_neg = _mm256_cmpgt_epi16(c, a);
    __m256i mask = _mm256_andnot_si256(skip, _mm256_or_si256(_mm256_slli_epi16(need_neg, 1), _mm256_set1_epi16(1)));
    return _mm256_sign_epi16(x, mask);
}

static void transpose_i16x16(const __m256i vs_in[8], __m256i out[8]) {
    __m256i vs[8];
    __m256i us[8];
    vs[0] = _mm256_unpacklo_epi16(vs_in[0], vs_in[1]);
    vs[1] = _mm256_unpacklo_epi16(vs_in[2], vs_in[3]);
    vs[2] = _mm256_unpacklo_epi16(vs_in[4], vs_in[5]);
    vs[3] = _mm256_unpacklo_epi16(vs_in[6], vs_in[7]);
    vs[4] = _mm256_unpackhi_epi16(vs_in[0], vs_in[1]);
    vs[5] = _mm256_unpackhi_epi16(vs_in[2], vs_in[3]);
    vs[6] = _mm256_unpackhi_epi16(vs_in[4], vs_in[5]);
    vs[7] = _mm256_unpackhi_epi16(vs_in[6], vs_in[7]);

    us[0] = _mm256_unpacklo_epi32(vs[0], vs[1]);
    us[1] = _mm256_unpacklo_epi32(vs[2], vs[3]);
    us[2] = _mm256_unpacklo_epi32(vs[4], vs[5]);
    us[3] = _mm256_unpacklo_epi32(vs[6], vs[7]);
    us[4] = _mm256_unpackhi_epi32(vs[0], vs[1]);
    us[5] = _mm256_unpackhi_epi32(vs[2], vs[3]);
    us[6] = _mm256_unpackhi_epi32(vs[4], vs[5]);
    us[7] = _mm256_unpackhi_epi32(vs[6], vs[7]);

    out[0] = _mm256_unpacklo_epi64(us[0], us[1]);
    out[1] = _mm256_unpackhi_epi64(us[0], us[1]);
    out[2] = _mm256_unpacklo_epi64(us[4], us[5]);
    out[3] = _mm256_unpackhi_epi64(us[4], us[5]);
    out[4] = _mm256_unpacklo_epi64(us[2], us[3]);
    out[5] = _mm256_unpackhi_epi64(us[2], us[3]);
    out[6] = _mm256_unpacklo_epi64(us[6], us[7]);
    out[7] = _mm256_unpackhi_epi64(us[6], us[7]);
}

static void transpose_i16x8(const __m128i vs_in[8], __m128i out[8]) {
    __m128i vs[8];
    __m128i us[8];
    vs[0] = _mm_unpacklo_epi16(vs_in[0], vs_in[1]);
    vs[1] = _mm_unpacklo_epi16(vs_in[2], vs_in[3]);
    vs[2] = _mm_unpacklo_epi16(vs_in[4], vs_in[5]);
    vs[3] = _mm_unpacklo_epi16(vs_in[6], vs_in[7]);
    vs[4] = _mm_unpackhi_epi16(vs_in[0], vs_in[1]);
    vs[5] = _mm_unpackhi_epi16(vs_in[2], vs_in[3]);
    vs[6] = _mm_unpackhi_epi16(vs_in[4], vs_in[5]);
    vs[7] = _mm_unpackhi_epi16(vs_in[6], vs_in[7]);

    us[0] = _mm_unpacklo_epi32(vs[0], vs[1]);
    us[1] = _mm_unpacklo_epi32(vs[2], vs[3]);
    us[2] = _mm_unpacklo_epi32(vs[4], vs[5]);
    us[3] = _mm_unpacklo_epi32(vs[6], vs[7]);
    us[4] = _mm_unpackhi_epi32(vs[0], vs[1]);
    us[5] = _mm_unpackhi_epi32(vs[2], vs[3]);
    us[6] = _mm_unpackhi_epi32(vs[4], vs[5]);
    us[7] = _mm_unpackhi_epi32(vs[6], vs[7]);

    out[0] = _mm_unpacklo_epi64(us[0], us[1]);
    out[1] = _mm_unpackhi_epi64(us[0], us[1]);
    out[2] = _mm_unpacklo_epi64(us[4], us[5]);
    out[3] = _mm_unpackhi_epi64(us[4], us[5]);
    out[4] = _mm_unpacklo_epi64(us[2], us[3]);
    out[5] = _mm_unpackhi_epi64(us[2], us[3]);
    out[6] = _mm_unpacklo_epi64(us[6], us[7]);
    out[7] = _mm_unpackhi_epi64(us[6], us[7]);
}

static __m128i tendency_i16_x86_64_sse41_via_avx2(__m128i a, __m128i b, __m128i c) {
    __m256i av = _mm256_set_m128i(a, a);
    __m256i bv = _mm256_set_m128i(b, b);
    __m256i cv = _mm256_set_m128i(c, c);
    return _mm256_extracti128_si256(tendency_i16_x86_64_avx2(av, bv, cv), 0);
}

static void inverse_h_i16_x86_64_avx2(jxl_allocator_state *alloc, int16_t *merged, size_t width,
                                      size_t height, size_t row_stride) {
                                          size_t y8;
    size_t avg_width;
    size_t h8;
    if (width <= 32) {
        jxl_squeeze_inverse_h_i16_base(alloc, merged, width, height, row_stride);
        return;
    }

    __m128i *scratch =
        (__m128i *)jxl_alloc_aligned(alloc, JXL_ALLOC_ALIGN_SIMD128, width * sizeof(*scratch));
    if (scratch == NULL) {
        return;
    }
    avg_width = (width + 1) / 2;

    h8 = height / 8;
    for (y8 = 0; y8 < h8; ++y8) {
        size_t dy;
        size_t x16;
        size_t y = y8 * 8;
        size_t x;
        int16_t *rows[8];
        for (dy = 0; dy < 8; ++dy) {
            rows[dy] = merged + (y + dy) * row_stride;
        }

        __m128i avg = _mm_setr_epi16(rows[0][0], rows[1][0], rows[2][0], rows[3][0],
                                     rows[4][0], rows[5][0], rows[6][0], rows[7][0]);
        __m128i left = avg;

        for (x16 = 0; x16 < (avg_width - 1) / 16; ++x16) {
            size_t idx;
            size_t dx;
            __m256i avgs_in[8];
            __m256i residuals_in[8];
            __m256i avgs[8];
            __m256i residuals[8];
            x = x16 * 16 + 1;
            for (idx = 0; idx < 8; ++idx) {
                avgs_in[idx] = _mm256_loadu_si256((const __m256i *)(rows[idx] + x));
                residuals_in[idx] = _mm256_loadu_si256((const __m256i *)(rows[idx] + avg_width - 1 + x));
            }
            transpose_i16x16(avgs_in, avgs);
            transpose_i16x16(residuals_in, residuals);

            for (dx = 0; dx < 8; ++dx) {
                __m128i residual = _mm256_extracti128_si256(residuals[dx], 0);
                __m128i next_avg = _mm256_extracti128_si256(avgs[dx], 0);
                __m128i diff = _mm_add_epi16(residual, tendency_i16_x86_64_sse41_via_avx2(left, avg, next_avg));
                __m128i diff_2 = _mm_srai_epi16(_mm_add_epi16(diff, _mm_srli_epi16(diff, 15)), 1);
                __m128i first = _mm_add_epi16(avg, diff_2);
                __m128i second = _mm_sub_epi16(first, diff);
                scratch[x16 * 32 + dx * 2] = first;
                scratch[x16 * 32 + dx * 2 + 1] = second;
                avg = next_avg;
                left = second;
            }

            for (dx = 0; dx < 8; ++dx) {
                __m128i residual = _mm256_extracti128_si256(residuals[dx], 1);
                __m128i next_avg = _mm256_extracti128_si256(avgs[dx], 1);
                __m128i diff = _mm_add_epi16(residual, tendency_i16_x86_64_sse41_via_avx2(left, avg, next_avg));
                __m128i diff_2 = _mm_srai_epi16(_mm_add_epi16(diff, _mm_srli_epi16(diff, 15)), 1);
                __m128i first = _mm_add_epi16(avg, diff_2);
                __m128i second = _mm_sub_epi16(first, diff);
                scratch[x16 * 32 + 16 + dx * 2] = first;
                scratch[x16 * 32 + 16 + dx * 2 + 1] = second;
                avg = next_avg;
                left = second;
            }
        }

        if ((avg_width - 1) % 16 >= 8) {
            size_t idx;
            size_t dx;
            size_t x;
            __m128i avgs_in[8];
            __m128i residuals_in[8];
            __m128i avgs[8];
            __m128i residuals[8];
            x16 = (avg_width - 1) / 16;
            x = x16 * 16 + 1;
            for (idx = 0; idx < 8; ++idx) {
                avgs_in[idx] = _mm_loadu_si128((const __m128i *)(rows[idx] + x));
                residuals_in[idx] = _mm_loadu_si128((const __m128i *)(rows[idx] + avg_width - 1 + x));
            }
            transpose_i16x8(avgs_in, avgs);
            transpose_i16x8(residuals_in, residuals);
            for (dx = 0; dx < 8; ++dx) {
                __m128i diff = _mm_add_epi16(residuals[dx], tendency_i16_x86_64_sse41_via_avx2(left, avg, avgs[dx]));
                __m128i diff_2 = _mm_srai_epi16(_mm_add_epi16(diff, _mm_srli_epi16(diff, 15)), 1);
                __m128i first = _mm_add_epi16(avg, diff_2);
                __m128i second = _mm_sub_epi16(first, diff);
                scratch[x16 * 32 + dx * 2] = first;
                scratch[x16 * 32 + dx * 2 + 1] = second;
                avg = avgs[dx];
                left = second;
            }
        }

        if ((avg_width - 1) % 8 != 0 || width % 2 == 0) {
            size_t idx;
            __m128i avgs_in[8];
            __m128i avgs[8];
            __m128i residuals_in[8];
            __m128i residuals[8];
            size_t from;
            for (idx = 0; idx < 8; ++idx) {
                avgs_in[idx] = _mm_loadu_si128((const __m128i *)(rows[idx] + avg_width - 8));
                residuals_in[idx] = _mm_loadu_si128((const __m128i *)(rows[idx] + width - 8));
            }
            transpose_i16x8(avgs_in, avgs);
            if (width % 2 == 0) {
                size_t idx;
                __m128i shifted[8];
                for (idx = 0; idx < 8; ++idx) {
                    shifted[idx] = (idx == 7) ? avgs[7] : avgs[idx + 1];
                }
                memcpy(avgs, shifted, sizeof(avgs));
            }
            transpose_i16x8(residuals_in, residuals);
            from = ((~(width / 2)) + 1) % 8;
            for (idx = from; idx < 8; ++idx) {
                size_t dx = 8 - idx;
                __m128i diff = _mm_add_epi16(residuals[idx], tendency_i16_x86_64_sse41_via_avx2(left, avg, avgs[idx]));
                __m128i diff_2 = _mm_srai_epi16(_mm_add_epi16(diff, _mm_srli_epi16(diff, 15)), 1);
                __m128i first = _mm_add_epi16(avg, diff_2);
                __m128i second = _mm_sub_epi16(first, diff);
                scratch[(width / 2) * 2 - dx * 2] = first;
                scratch[(width / 2) * 2 - dx * 2 + 1] = second;
                avg = avgs[idx];
                left = second;
            }
        }

        if (width % 2 == 1) {
            scratch[width - 1] = avg;
        }

        x = 0;
        for (; x + 8 <= width; x += 8) {
            size_t idx;
            size_t row;
            __m128i block_in[8];
            __m128i block_out[8];
            for (idx = 0; idx < 8; ++idx) {
                block_in[idx] = scratch[x + idx];
            }
            transpose_i16x8(block_in, block_out);
            for (row = 0; row < 8; ++row) {
                _mm_storeu_si128((__m128i *)(rows[row] + x), block_out[row]);
            }
        }
        for (; x < width; ++x) {
            size_t row;
            int16_t tmp[8];
            _mm_storeu_si128((__m128i *)tmp, scratch[x]);
            for (row = 0; row < 8; ++row) {
                rows[row][x] = tmp[row];
            }
        }
    }

    if (height % 8 != 0) {
        _mm256_zeroupper();
        jxl_squeeze_inverse_h_i16_base(
            alloc,
            merged + h8 * 8 * row_stride,
            width,
            height - h8 * 8,
            row_stride);
    }

    jxl_free_aligned(alloc, scratch);
}

static void inverse_v_i16_x86_64_avx2(jxl_allocator_state *alloc, int16_t *merged, size_t width,
                                      size_t height, size_t row_stride) {
                                          size_t x16;
    if (height <= 1) {
        return;
    }

    __m256i *scratch = (__m256i *)jxl_alloc_aligned(alloc, JXL_ALLOC_ALIGN_SIMD256,
                                                    height * sizeof(*scratch));
    if (scratch == NULL) {
        return;
    }
    const size_t avg_height = (height + 1) / 2;
    const size_t w16 = width / 16;

    for (x16 = 0; x16 < w16; ++x16) {
        size_t py;
        size_t y;
        const size_t x = x16 * 16;
        __m256i avg = _mm256_loadu_si256((const __m256i *)(merged + x));
        __m256i top = avg;
        const size_t half = height / 2;
        for (py = 0; py < half; ++py) {
            const __m256i residual =
                _mm256_loadu_si256((const __m256i *)(merged + (avg_height + py) * row_stride + x));
            const __m256i next_avg = (py + 1 < avg_height)
                                         ? _mm256_loadu_si256(
                                               (const __m256i *)(merged + (py + 1) * row_stride + x))
                                         : avg;
            const __m256i diff = _mm256_add_epi16(residual, tendency_i16_x86_64_avx2(top, avg, next_avg));
            const __m256i diff_2 =
                _mm256_srai_epi16(_mm256_add_epi16(diff, _mm256_srli_epi16(diff, 15)), 1);
            const __m256i first = _mm256_add_epi16(avg, diff_2);
            const __m256i second = _mm256_sub_epi16(first, diff);
            scratch[2 * py] = first;
            scratch[2 * py + 1] = second;
            avg = next_avg;
            top = second;
        }
        if ((height % 2) != 0) {
            scratch[height - 1] = avg;
        }
        for (y = 0; y < height; ++y) {
            _mm256_storeu_si256((__m256i *)(merged + y * row_stride + x), scratch[y]);
        }
    }

    if (width % 16 != 0) {
        _mm256_zeroupper();
        jxl_squeeze_inverse_v_i16_x86_sse41(alloc, merged + w16 * 16, width - w16 * 16, height,
                                            row_stride);
    }

    jxl_free_aligned(alloc, scratch);
}

void jxl_squeeze_inverse_h_i16_x86_avx2(jxl_allocator_state *alloc, int16_t *merged, size_t width,
                                        size_t height, size_t row_stride) {
    if (row_stride == 0) {
        row_stride = width;
    }
    inverse_h_i16_x86_64_avx2(alloc, merged, width, height, row_stride);
    _mm256_zeroupper();
}

void jxl_squeeze_inverse_v_i16_x86_avx2(jxl_allocator_state *alloc, int16_t *merged, size_t width,
                                        size_t height, size_t row_stride) {
    if (row_stride == 0) {
        row_stride = width;
    }
    inverse_v_i16_x86_64_avx2(alloc, merged, width, height, row_stride);
    _mm256_zeroupper();
}

#endif /* defined(JXL_HAVE_SIMD_AVX2) */
