// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/color/fastmath.h"

#include <immintrin.h>
#include <smmintrin.h>
#include "jxl_oxide/jxl_types.h"
#include <string.h>

#if defined(JXL_HAVE_SIMD_SSE41)

static const float k_pow2f_numer[3] = {1.01749063e1f, 4.88687798e1f, 9.85506591e1f};
static const float k_pow2f_denom[4] = {2.10242958e-1f, -2.22328856e-2f, -1.94414990e1f, 9.85506633e1f};
static const float k_log2f_p[3] = {
    -1.8503833400518310e-6f, 1.4287160470083755f, 7.4245873327820566e-1f,
};
static const float k_log2f_q[3] = {
    9.9032814277590719e-1f, 1.0096718572241148f, 1.7409343003366853e-1f,
};

static __m128 rational_eval3_sse41(__m128 x, const float p[3], const float q[3]) {
    __m128 yp = _mm_set1_ps(p[2]);
    yp = _mm_add_ps(_mm_mul_ps(yp, x), _mm_set1_ps(p[1]));
    yp = _mm_add_ps(_mm_mul_ps(yp, x), _mm_set1_ps(p[0]));
    __m128 yq = _mm_set1_ps(q[2]);
    yq = _mm_add_ps(_mm_mul_ps(yq, x), _mm_set1_ps(q[1]));
    yq = _mm_add_ps(_mm_mul_ps(yq, x), _mm_set1_ps(q[0]));
    return _mm_div_ps(yp, yq);
}

static __m128 fast_pow2f_sse41(__m128 x) {
    __m128 x_floor = _mm_floor_ps(x);
    __m128i exp_i = _mm_add_epi32(_mm_cvtps_epi32(x_floor), _mm_set1_epi32(127));
    __m128 exp = _mm_castsi128_ps(_mm_slli_epi32(exp_i, 23));
    __m128 frac = _mm_sub_ps(x, x_floor);

    __m128 num = _mm_add_ps(_mm_set1_ps(k_pow2f_numer[0]), frac);
    num = _mm_add_ps(_mm_set1_ps(k_pow2f_numer[1]), _mm_mul_ps(frac, num));
    num = _mm_add_ps(_mm_set1_ps(k_pow2f_numer[2]), _mm_mul_ps(frac, num));
    num = _mm_mul_ps(exp, num);

    __m128 den = _mm_add_ps(
        _mm_set1_ps(k_pow2f_denom[1]), _mm_mul_ps(frac, _mm_set1_ps(k_pow2f_denom[0])));
    den = _mm_add_ps(_mm_set1_ps(k_pow2f_denom[2]), _mm_mul_ps(frac, den));
    den = _mm_add_ps(_mm_set1_ps(k_pow2f_denom[3]), _mm_mul_ps(frac, den));

    return _mm_div_ps(num, den);
}

static __m128 fast_log2f_sse41(__m128 x) {
    __m128i x_bits = _mm_castps_si128(x);
    __m128i exp_bits = _mm_sub_epi32(x_bits, _mm_set1_epi32(0x3f2aaaab));
    __m128i exp_shifted = _mm_srai_epi32(exp_bits, 23);
    __m128 mantissa = _mm_castsi128_ps(_mm_sub_epi32(
        x_bits, _mm_slli_epi32(exp_shifted, 23)));
    __m128 exp_val = _mm_cvtepi32_ps(exp_shifted);

    __m128 xm = _mm_sub_ps(mantissa, _mm_set1_ps(1.0f));
    return _mm_add_ps(rational_eval3_sse41(xm, k_log2f_p, k_log2f_q), exp_val);
}

static __m128 fast_powf_sse41(__m128 base, __m128 exp) {
    return fast_pow2f_sse41(_mm_mul_ps(fast_log2f_sse41(base), exp));
}

__m128 jxl_fastmath_powf_vec_x86_sse41(__m128 base, __m128 exp) {
    return fast_powf_sse41(base, exp);
}

__m128 jxl_fastmath_rational_eval5_x86_sse2(__m128 x, const float p[5], const float q[5]) {
    __m128 yp = _mm_set1_ps(p[4]);
    yp = _mm_add_ps(_mm_mul_ps(yp, x), _mm_set1_ps(p[3]));
    yp = _mm_add_ps(_mm_mul_ps(yp, x), _mm_set1_ps(p[2]));
    yp = _mm_add_ps(_mm_mul_ps(yp, x), _mm_set1_ps(p[1]));
    yp = _mm_add_ps(_mm_mul_ps(yp, x), _mm_set1_ps(p[0]));
    __m128 yq = _mm_set1_ps(q[4]);
    yq = _mm_add_ps(_mm_mul_ps(yq, x), _mm_set1_ps(q[3]));
    yq = _mm_add_ps(_mm_mul_ps(yq, x), _mm_set1_ps(q[2]));
    yq = _mm_add_ps(_mm_mul_ps(yq, x), _mm_set1_ps(q[1]));
    yq = _mm_add_ps(_mm_mul_ps(yq, x), _mm_set1_ps(q[0]));
    return _mm_div_ps(yp, yq);
}

void jxl_fastmath_powf_f32_x86_sse41(float *samples, size_t n, float exponent) {
    size_t i;
    if (samples == NULL || n == 0) {
        return;
    }
    const __m128 exp_v = _mm_set1_ps(exponent);
    i = 0;
    for (; i + 4 <= n; i += 4) {
        const __m128 base = _mm_loadu_ps(samples + i);
        _mm_storeu_ps(samples + i, fast_powf_sse41(base, exp_v));
    }
    for (; i < n; ++i) {
        samples[i] = jxl_fastmath_powf(samples[i], exponent);
    }
}

#endif /* defined(JXL_HAVE_SIMD_SSE41) */
