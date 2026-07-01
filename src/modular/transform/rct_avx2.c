// SPDX-License-Identifier: MIT OR Apache-2.0
#include "modular/transform/rct_internal.h"

#if defined(JXL_HAVE_SIMD_AVX2)

#include <immintrin.h>

static void inverse_rct_i16_x16_avx2(uint32_t ty, __m256i a, __m256i b, __m256i c, __m256i *out_d,
                                     __m256i *out_e, __m256i *out_f) {
    if (ty == 6) {
        __m256i tmp = _mm256_sub_epi16(a, _mm256_srai_epi16(c, 1));
        __m256i e = _mm256_add_epi16(c, tmp);
        __m256i f = _mm256_sub_epi16(tmp, _mm256_srai_epi16(b, 1));
        __m256i d = _mm256_add_epi16(f, b);
        *out_d = d;
        *out_e = e;
        *out_f = f;
        return;
    }

    *out_d = a;
    if ((ty & 1) != 0) {
        *out_f = _mm256_add_epi16(c, a);
    } else {
        *out_f = c;
    }
    if ((ty >> 1) == 1) {
        *out_e = _mm256_add_epi16(b, a);
    } else if ((ty >> 1) == 2) {
        *out_e = _mm256_add_epi16(b, _mm256_srai_epi16(_mm256_add_epi16(a, *out_f), 1));
    } else {
        *out_e = b;
    }
}

static void inverse_rct_i32_x8_avx2(uint32_t ty, __m256i a, __m256i b, __m256i c, __m256i *out_d,
                                    __m256i *out_e, __m256i *out_f) {
    if (ty == 6) {
        __m256i tmp = _mm256_sub_epi32(a, _mm256_srai_epi32(c, 1));
        __m256i e = _mm256_add_epi32(c, tmp);
        __m256i f = _mm256_sub_epi32(tmp, _mm256_srai_epi32(b, 1));
        __m256i d = _mm256_add_epi32(f, b);
        *out_d = d;
        *out_e = e;
        *out_f = f;
        return;
    }

    *out_d = a;
    if ((ty & 1) != 0) {
        *out_f = _mm256_add_epi32(c, a);
    } else {
        *out_f = c;
    }
    if ((ty >> 1) == 1) {
        *out_e = _mm256_add_epi32(b, a);
    } else if ((ty >> 1) == 2) {
        *out_e = _mm256_add_epi32(b, _mm256_srai_epi32(_mm256_add_epi32(a, *out_f), 1));
    } else {
        *out_e = b;
    }
}

static void store_i16_x16_as_float_avx2(__m256i v16, float *dst, __m256 scale_v) {
    const __m128i lo = _mm256_castsi256_si128(v16);
    const __m128i hi = _mm256_extracti128_si256(v16, 1);
    _mm256_storeu_ps(dst, _mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_cvtepi16_epi32(lo)), scale_v));
    _mm256_storeu_ps(dst + 8,
                     _mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_cvtepi16_epi32(hi)), scale_v));
}

void jxl_rct_inverse_export_row3_i16_avx2(uint32_t ty, int16_t *ra, int16_t *rb, int16_t *rc,
                                          size_t width, int export_row, uint32_t export_x0,
                                          uint32_t export_w, float scale, float *dst0,
                                          float *dst1, float *dst2) {
    size_t x = 0;
    const __m256 scale_v = _mm256_set1_ps(scale);

    for (; x + 16 <= width; x += 16) {
        __m256i a = _mm256_loadu_si256((const __m256i *)(ra + x));
        __m256i b = _mm256_loadu_si256((const __m256i *)(rb + x));
        __m256i c = _mm256_loadu_si256((const __m256i *)(rc + x));
        __m256i d;
        __m256i e;
        __m256i f;
        inverse_rct_i16_x16_avx2(ty, a, b, c, &d, &e, &f);
        _mm256_storeu_si256((__m256i *)(ra + x), d);
        _mm256_storeu_si256((__m256i *)(rb + x), e);
        _mm256_storeu_si256((__m256i *)(rc + x), f);
        if (export_row && x >= export_x0 && x + 16 <= export_x0 + export_w) {
            size_t out_x = x - export_x0;
            store_i16_x16_as_float_avx2(d, dst0 + out_x, scale_v);
            store_i16_x16_as_float_avx2(e, dst1 + out_x, scale_v);
            store_i16_x16_as_float_avx2(f, dst2 + out_x, scale_v);
        }
    }
    for (; x < width; ++x) {
        jxl_rct_inverse_row_i16_pixel(ty, &ra[x], &rb[x], &rc[x]);
        if (export_row && x >= export_x0 && x < export_x0 + export_w) {
            size_t out_x = x - export_x0;
            dst0[out_x] = (float)ra[x] * scale;
            dst1[out_x] = (float)rb[x] * scale;
            dst2[out_x] = (float)rc[x] * scale;
        }
    }
}

void jxl_rct_inverse_row_i16_avx2(uint32_t ty, int16_t *ra, int16_t *rb, int16_t *rc, size_t width) {
    size_t x = 0;
    for (; x + 16 <= width; x += 16) {
        __m256i a = _mm256_loadu_si256((const __m256i *)(ra + x));
        __m256i b = _mm256_loadu_si256((const __m256i *)(rb + x));
        __m256i c = _mm256_loadu_si256((const __m256i *)(rc + x));
        __m256i d;
        __m256i e;
        __m256i f;
        inverse_rct_i16_x16_avx2(ty, a, b, c, &d, &e, &f);
        _mm256_storeu_si256((__m256i *)(ra + x), d);
        _mm256_storeu_si256((__m256i *)(rb + x), e);
        _mm256_storeu_si256((__m256i *)(rc + x), f);
    }
    for (; x < width; ++x) {
        jxl_rct_inverse_row_i16_pixel(ty, &ra[x], &rb[x], &rc[x]);
    }
}

void jxl_rct_inverse_row_i32_avx2(uint32_t ty, int32_t *ra, int32_t *rb, int32_t *rc, size_t width) {
    size_t x = 0;
    for (; x + 8 <= width; x += 8) {
        __m256i a = _mm256_loadu_si256((const __m256i *)(ra + x));
        __m256i b = _mm256_loadu_si256((const __m256i *)(rb + x));
        __m256i c = _mm256_loadu_si256((const __m256i *)(rc + x));
        __m256i d;
        __m256i e;
        __m256i f;
        inverse_rct_i32_x8_avx2(ty, a, b, c, &d, &e, &f);
        _mm256_storeu_si256((__m256i *)(ra + x), d);
        _mm256_storeu_si256((__m256i *)(rb + x), e);
        _mm256_storeu_si256((__m256i *)(rc + x), f);
    }
    for (; x < width; ++x) {
        jxl_rct_inverse_row_i32_pixel(ty, &ra[x], &rb[x], &rc[x]);
    }
}

#endif /* JXL_HAVE_SIMD_AVX2 */
