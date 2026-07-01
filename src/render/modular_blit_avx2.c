// SPDX-License-Identifier: MIT OR Apache-2.0
#if defined(JXL_HAVE_SIMD_AVX2)

#include <immintrin.h>
#include <stddef.h>
#include <stdint.h>

void jxl_modular_blit_i16_row_to_plane_avx2(const int16_t *src, float *dst, size_t n,
                                            float scale) {
    size_t x = 0;
    const __m256 scale_v = _mm256_set1_ps(scale);

    for (; x + 16 <= n; x += 16) {
        const __m128i v16_lo = _mm_loadu_si128((const __m128i *)(src + x));
        const __m128i v16_hi = _mm_loadu_si128((const __m128i *)(src + x + 8));
        const __m256i v32_lo = _mm256_cvtepi16_epi32(v16_lo);
        const __m256i v32_hi = _mm256_cvtepi16_epi32(v16_hi);
        _mm256_storeu_ps(dst + x, _mm256_mul_ps(_mm256_cvtepi32_ps(v32_lo), scale_v));
        _mm256_storeu_ps(dst + x + 8, _mm256_mul_ps(_mm256_cvtepi32_ps(v32_hi), scale_v));
    }
    for (; x + 8 <= n; x += 8) {
        const __m128i v16 = _mm_loadu_si128((const __m128i *)(src + x));
        const __m256i v32 = _mm256_cvtepi16_epi32(v16);
        _mm256_storeu_ps(dst + x, _mm256_mul_ps(_mm256_cvtepi32_ps(v32), scale_v));
    }
    for (; x < n; ++x) {
        dst[x] = (float)src[x] * scale;
    }
}

#endif /* defined(JXL_HAVE_SIMD_AVX2) */
