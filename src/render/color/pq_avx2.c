// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/color/pq_internal.h"

#include "render/color/fastmath_internal.h"

#include <immintrin.h>
#include "jxl_oxide/jxl_types.h"
#include <string.h>

#if defined(JXL_HAVE_SIMD_AVX2)

static const float k_eotf_p[5] = {
    2.6297566e-4f, -6.235531e-3f, 7.386023e-1f, 2.6455317f, 5.500349e-1f,
};
static const float k_eotf_q[5] = {
    4.213501e2f, -4.2873682e2f, 1.7436467e2f, -3.3907887e1f, 2.6771877f,
};
static const float k_inv_eotf_p[5] = {
    1.351392e-2f, -1.095778f, 5.522776e1f, 1.492516e2f, 4.838434e1f,
};
static const float k_inv_eotf_q[5] = {
    1.012416f, 2.016708e1f, 9.26371e1f, 1.120607e2f, 2.590418e1f,
};
static const float k_inv_eotf_p_small[5] = {
    9.863406e-6f, 3.881234e-1f, 1.352821e2f, 6.889862e4f, -2.864824e5f,
};
static const float k_inv_eotf_q_small[5] = {
    3.371868e1f, 1.477719e3f, 1.608477e4f, -4.389884e4f, -2.072546e5f,
};

__m256 jxl_color_linear_to_pq_vec_x86_avx2(__m256 v, float intensity_target) {
    const float y_mult = intensity_target / 10000.0f;
    const __m256 v_mult = _mm256_set1_ps(y_mult);
    const __m256 sign_mask = _mm256_castsi256_ps(_mm256_set1_epi32((int32_t)0x80000000u));

    const __m256 sign = _mm256_and_ps(sign_mask, v);
    const __m256 a = _mm256_andnot_ps(sign_mask, v);
    const __m256 a_scaled = _mm256_mul_ps(a, v_mult);
    const __m256 a_1_4 = _mm256_sqrt_ps(_mm256_sqrt_ps(a_scaled));

    const __m256 v_small =
        jxl_fastmath_rational_eval5_x86_avx2(a_1_4, k_inv_eotf_p_small, k_inv_eotf_q_small);
    const __m256 v_large = jxl_fastmath_rational_eval5_x86_avx2(a_1_4, k_inv_eotf_p, k_inv_eotf_q);
    const __m256 is_small = _mm256_cmp_ps(a, _mm256_set1_ps(1e-4f), _CMP_LE_OQ);
    const __m256 y = _mm256_blendv_ps(v_large, v_small, is_small);
    return _mm256_or_ps(_mm256_andnot_ps(sign_mask, y), sign);
}

__m128 jxl_color_linear_to_pq_vec_x86_fma(__m128 v, float intensity_target) {
    const float y_mult = intensity_target / 10000.0f;
    const __m128 v_mult = _mm_set1_ps(y_mult);
    const __m128 sign_mask = _mm_castsi128_ps(_mm_set1_epi32((int32_t)0x80000000u));

    const __m128 sign = _mm_and_ps(sign_mask, v);
    const __m128 a = _mm_andnot_ps(sign_mask, v);
    const __m128 a_scaled = _mm_mul_ps(a, v_mult);
    const __m128 a_1_4 = _mm_sqrt_ps(_mm_sqrt_ps(a_scaled));

    const __m128 v_small =
        jxl_fastmath_rational_eval5_x86_fma(a_1_4, k_inv_eotf_p_small, k_inv_eotf_q_small);
    const __m128 v_large = jxl_fastmath_rational_eval5_x86_fma(a_1_4, k_inv_eotf_p, k_inv_eotf_q);
    const __m128 is_small = _mm_cmplt_ps(a, _mm_set1_ps(1e-4f));
    const __m128 y = _mm_blendv_ps(v_large, v_small, is_small);
    return _mm_or_ps(_mm_andnot_ps(sign_mask, y), sign);
}

__m256 jxl_color_pq_to_linear_vec_x86_avx2(__m256 v, float intensity_target) {
    const float y_mult = 10000.0f / intensity_target;
    const __m256 v_mult = _mm256_set1_ps(y_mult);
    const __m256 sign_mask = _mm256_castsi256_ps(_mm256_set1_epi32((int32_t)0x80000000u));

    const __m256 sign = _mm256_and_ps(sign_mask, v);
    const __m256 a = _mm256_andnot_ps(sign_mask, v);
    const __m256 x = _mm256_fmadd_ps(a, a, a);

    const __m256 y = jxl_fastmath_rational_eval5_x86_avx2(x, k_eotf_p, k_eotf_q);
    const __m256 out = _mm256_mul_ps(y, v_mult);
    return _mm256_or_ps(_mm256_andnot_ps(sign_mask, out), sign);
}

__m128 jxl_color_pq_to_linear_vec_x86_fma(__m128 v, float intensity_target) {
    const float y_mult = 10000.0f / intensity_target;
    const __m128 v_mult = _mm_set1_ps(y_mult);
    const __m128 sign_mask = _mm_castsi128_ps(_mm_set1_epi32((int32_t)0x80000000u));

    const __m128 sign = _mm_and_ps(sign_mask, v);
    const __m128 a = _mm_andnot_ps(sign_mask, v);
    const __m128 x = _mm_fmadd_ps(a, a, a);

    const __m128 y = jxl_fastmath_rational_eval5_x86_fma(x, k_eotf_p, k_eotf_q);
    const __m128 out = _mm_mul_ps(y, v_mult);
    return _mm_or_ps(_mm_andnot_ps(sign_mask, out), sign);
}

void jxl_color_linear_to_pq_x86_avx2(float *samples, size_t n, float intensity_target) {
    size_t i;
    if (samples == NULL || n == 0) {
        return;
    }
    i = 0;
    for (; i + 8 <= n; i += 8) {
        const __m256 v = _mm256_loadu_ps(samples + i);
        _mm256_storeu_ps(samples + i, jxl_color_linear_to_pq_vec_x86_avx2(v, intensity_target));
    }
    for (; i + 4 <= n; i += 4) {
        const __m128 v = _mm_loadu_ps(samples + i);
        _mm_storeu_ps(samples + i, jxl_color_linear_to_pq_vec_x86_fma(v, intensity_target));
    }
    _mm256_zeroupper();
    jxl_color_linear_to_pq_base(samples + i, n - i, intensity_target);
}

void jxl_color_linear_to_pq_x86_fma(float *samples, size_t n, float intensity_target) {
    size_t i;
    if (samples == NULL || n == 0) {
        return;
    }
    i = 0;
    for (; i + 4 <= n; i += 4) {
        const __m128 v = _mm_loadu_ps(samples + i);
        _mm_storeu_ps(samples + i, jxl_color_linear_to_pq_vec_x86_fma(v, intensity_target));
    }
    jxl_color_linear_to_pq_base(samples + i, n - i, intensity_target);
}

void jxl_color_pq_to_linear_x86_avx2(float *samples, size_t n, float intensity_target) {
    size_t i;
    if (samples == NULL || n == 0) {
        return;
    }
    i = 0;
    for (; i + 8 <= n; i += 8) {
        const __m256 v = _mm256_loadu_ps(samples + i);
        _mm256_storeu_ps(samples + i, jxl_color_pq_to_linear_vec_x86_avx2(v, intensity_target));
    }
    for (; i + 4 <= n; i += 4) {
        const __m128 v = _mm_loadu_ps(samples + i);
        _mm_storeu_ps(samples + i, jxl_color_pq_to_linear_vec_x86_fma(v, intensity_target));
    }
    _mm256_zeroupper();
    jxl_color_pq_to_linear_base(samples + i, n - i, intensity_target);
}

void jxl_color_pq_to_linear_x86_fma(float *samples, size_t n, float intensity_target) {
    size_t i;
    if (samples == NULL || n == 0) {
        return;
    }
    i = 0;
    for (; i + 4 <= n; i += 4) {
        const __m128 v = _mm_loadu_ps(samples + i);
        _mm_storeu_ps(samples + i, jxl_color_pq_to_linear_vec_x86_fma(v, intensity_target));
    }
    jxl_color_pq_to_linear_base(samples + i, n - i, intensity_target);
}

#endif /* defined(JXL_HAVE_SIMD_AVX2) */
