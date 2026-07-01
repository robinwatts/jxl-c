// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/color/color_transform_internal.h"
#include "render/color/fastmath_internal.h"

#include <immintrin.h>
#include <smmintrin.h>
#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

#if defined(JXL_HAVE_SIMD_SSE41)

static const float k_srgb_eotf_p[5] = {
    2.200248328e-4f, 1.043637593e-2f, 1.624820318e-1f, 7.961564959e-1f, 8.210152774e-1f,
};
static const float k_srgb_eotf_q[5] = {
    2.631846970e-1f, 1.076976492f, 4.987528350e-1f, -5.512498495e-2f, 6.521209011e-3f,
};

static __m128 srgb_to_linear_vec_sse2(__m128 v) {
    const __m128 sign_mask = _mm_castsi128_ps(_mm_set1_epi32((int32_t)0x80000000u));
    const __m128 sign = _mm_and_ps(sign_mask, v);
    const __m128 a = _mm_andnot_ps(sign_mask, v);
    const __m128 small = _mm_div_ps(a, _mm_set1_ps(12.92f));
    const __m128 large = jxl_fastmath_rational_eval5_x86_sse2(a, k_srgb_eotf_p, k_srgb_eotf_q);
    const __m128 mask = _mm_cmple_ps(a, _mm_set1_ps(0.04045f));
    const __m128 out = _mm_or_ps(_mm_and_ps(mask, small), _mm_andnot_ps(mask, large));
    return _mm_or_ps(_mm_andnot_ps(sign_mask, out), sign);
}

void jxl_color_srgb_to_linear_x86_sse2(float *samples, size_t n) {
    size_t i;
    if (samples == NULL || n == 0) {
        return;
    }
    i = 0;
    for (; i + 4 <= n; i += 4) {
        const __m128 v = _mm_loadu_ps(samples + i);
        _mm_storeu_ps(samples + i, srgb_to_linear_vec_sse2(v));
    }
    jxl_color_srgb_to_linear_base(samples + i, n - i);
}

void jxl_color_linear_to_bt709_x86_sse41(float *samples, size_t n) {
    size_t i;
    if (samples == NULL || n == 0) {
        return;
    }
    const __m128 thresh = _mm_set1_ps(0.018f);
    const __m128 lin_scale = _mm_set1_ps(4.5f);
    const __m128 exp_scale = _mm_set1_ps(1.099f);
    const __m128 exp_bias = _mm_set1_ps(-0.099f);
    const __m128 exp_v = _mm_set1_ps(0.45f);

    i = 0;
    for (; i + 4 <= n; i += 4) {
        const __m128 v = _mm_loadu_ps(samples + i);
        const __m128 mask = _mm_cmple_ps(v, thresh);
        const __m128 lin = _mm_mul_ps(v, lin_scale);
        const __m128 powv = jxl_fastmath_powf_vec_x86_sse41(v, exp_v);
        const __m128 hi = _mm_add_ps(_mm_mul_ps(powv, exp_scale), exp_bias);
        const __m128 out = _mm_or_ps(_mm_and_ps(mask, lin), _mm_andnot_ps(mask, hi));
        _mm_storeu_ps(samples + i, out);
    }
    jxl_color_linear_to_bt709_base(samples + i, n - i);
}

void jxl_color_bt709_to_linear_x86_sse2(float *samples, size_t n) {
    size_t i;
    if (samples == NULL || n == 0) {
        return;
    }
    const __m128 thresh = _mm_set1_ps(0.081f);
    const __m128 lin_scale = _mm_set1_ps(1.0f / 4.5f);
    const __m128 hi_scale = _mm_set1_ps(1.0f / 1.099f);
    const __m128 hi_bias = _mm_set1_ps(0.099f / 1.099f);
    const __m128 exp_v = _mm_set1_ps(1.0f / 0.45f);

    i = 0;
    for (; i + 4 <= n; i += 4) {
        const __m128 v = _mm_loadu_ps(samples + i);
        const __m128 mask = _mm_cmple_ps(v, thresh);
        const __m128 lin = _mm_mul_ps(v, lin_scale);
        const __m128 hi_in = _mm_add_ps(_mm_mul_ps(v, hi_scale), hi_bias);
        const __m128 hi = jxl_fastmath_powf_vec_x86_sse41(hi_in, exp_v);
        const __m128 out = _mm_or_ps(_mm_and_ps(mask, lin), _mm_andnot_ps(mask, hi));
        _mm_storeu_ps(samples + i, out);
    }
    jxl_color_bt709_to_linear_base(samples + i, n - i);
}

void jxl_color_apply_gamma_x86_sse2(float *samples, size_t n, float gamma) {
    size_t i;
    if (samples == NULL || n == 0) {
        return;
    }
    const __m128 thresh = _mm_set1_ps(1e-7f);
    const __m128 exp_v = _mm_set1_ps(gamma);
    i = 0;
    for (; i + 4 <= n; i += 4) {
        const __m128 v = _mm_loadu_ps(samples + i);
        const __m128 mask = _mm_cmple_ps(v, thresh);
        const __m128 powv = jxl_fastmath_powf_vec_x86_sse41(v, exp_v);
        _mm_storeu_ps(samples + i, _mm_andnot_ps(mask, powv));
    }
    jxl_color_apply_gamma_base(samples + i, n - i, gamma);
}

#endif /* defined(JXL_HAVE_SIMD_SSE41) */
