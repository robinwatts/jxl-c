// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/color/fastmath.h"

#include <immintrin.h>
#include "jxl_oxide/jxl_types.h"
#include <string.h>

#if defined(JXL_HAVE_SIMD_AVX2)

static const float k_pow2f_numer[3] = {1.01749063e1f, 4.88687798e1f, 9.85506591e1f};
static const float k_pow2f_denom[4] = {2.10242958e-1f, -2.22328856e-2f, -1.94414990e1f, 9.85506633e1f};
static const float k_log2f_p[3] = {
    -1.8503833400518310e-6f, 1.4287160470083755f, 7.4245873327820566e-1f,
};
static const float k_log2f_q[3] = {
    9.9032814277590719e-1f, 1.0096718572241148f, 1.7409343003366853e-1f,
};

static __m256 rational_eval3_avx2(__m256 x, const float p[3], const float q[3]) {
    __m256 yp = _mm256_set1_ps(p[2]);
    yp = _mm256_fmadd_ps(yp, x, _mm256_set1_ps(p[1]));
    yp = _mm256_fmadd_ps(yp, x, _mm256_set1_ps(p[0]));
    __m256 yq = _mm256_set1_ps(q[2]);
    yq = _mm256_fmadd_ps(yq, x, _mm256_set1_ps(q[1]));
    yq = _mm256_fmadd_ps(yq, x, _mm256_set1_ps(q[0]));
    return _mm256_div_ps(yp, yq);
}

static __m256 fast_pow2f_avx2(__m256 x) {
    __m256 x_floor = _mm256_floor_ps(x);
    __m256i exp_i = _mm256_add_epi32(_mm256_cvtps_epi32(x_floor), _mm256_set1_epi32(127));
    __m256 exp = _mm256_castsi256_ps(_mm256_slli_epi32(exp_i, 23));
    __m256 frac = _mm256_sub_ps(x, x_floor);

    __m256 num = _mm256_add_ps(_mm256_set1_ps(k_pow2f_numer[0]), frac);
    num = _mm256_fmadd_ps(frac, num, _mm256_set1_ps(k_pow2f_numer[1]));
    num = _mm256_fmadd_ps(frac, num, _mm256_set1_ps(k_pow2f_numer[2]));
    num = _mm256_mul_ps(exp, num);

    __m256 den = _mm256_fmadd_ps(frac, _mm256_set1_ps(k_pow2f_denom[0]), _mm256_set1_ps(k_pow2f_denom[1]));
    den = _mm256_fmadd_ps(frac, den, _mm256_set1_ps(k_pow2f_denom[2]));
    den = _mm256_fmadd_ps(frac, den, _mm256_set1_ps(k_pow2f_denom[3]));

    return _mm256_div_ps(num, den);
}

static __m256 fast_log2f_avx2(__m256 x) {
    __m256i x_bits = _mm256_castps_si256(x);
    __m256i exp_bits = _mm256_sub_epi32(x_bits, _mm256_set1_epi32(0x3f2aaaab));
    __m256i exp_shifted = _mm256_srai_epi32(exp_bits, 23);
    __m256 mantissa =
        _mm256_castsi256_ps(_mm256_sub_epi32(x_bits, _mm256_slli_epi32(exp_shifted, 23)));
    __m256 exp_val = _mm256_cvtepi32_ps(exp_shifted);

    __m256 xm = _mm256_sub_ps(mantissa, _mm256_set1_ps(1.0f));
    return _mm256_add_ps(rational_eval3_avx2(xm, k_log2f_p, k_log2f_q), exp_val);
}

static __m128 rational_eval3_fma(__m128 x, const float p[3], const float q[3]) {
    __m128 yp = _mm_set1_ps(p[2]);
    yp = _mm_fmadd_ps(yp, x, _mm_set1_ps(p[1]));
    yp = _mm_fmadd_ps(yp, x, _mm_set1_ps(p[0]));
    __m128 yq = _mm_set1_ps(q[2]);
    yq = _mm_fmadd_ps(yq, x, _mm_set1_ps(q[1]));
    yq = _mm_fmadd_ps(yq, x, _mm_set1_ps(q[0]));
    return _mm_div_ps(yp, yq);
}

static __m128 fast_pow2f_fma(__m128 x) {
    __m128 x_floor = _mm_floor_ps(x);
    __m128i exp_i = _mm_add_epi32(_mm_cvtps_epi32(x_floor), _mm_set1_epi32(127));
    __m128 exp = _mm_castsi128_ps(_mm_slli_epi32(exp_i, 23));
    __m128 frac = _mm_sub_ps(x, x_floor);

    __m128 num = _mm_add_ps(_mm_set1_ps(k_pow2f_numer[0]), frac);
    num = _mm_fmadd_ps(frac, num, _mm_set1_ps(k_pow2f_numer[1]));
    num = _mm_fmadd_ps(frac, num, _mm_set1_ps(k_pow2f_numer[2]));
    num = _mm_mul_ps(exp, num);

    __m128 den = _mm_fmadd_ps(frac, _mm_set1_ps(k_pow2f_denom[0]), _mm_set1_ps(k_pow2f_denom[1]));
    den = _mm_fmadd_ps(frac, den, _mm_set1_ps(k_pow2f_denom[2]));
    den = _mm_fmadd_ps(frac, den, _mm_set1_ps(k_pow2f_denom[3]));

    return _mm_div_ps(num, den);
}

static __m128 fast_log2f_fma(__m128 x) {
    __m128i x_bits = _mm_castps_si128(x);
    __m128i exp_bits = _mm_sub_epi32(x_bits, _mm_set1_epi32(0x3f2aaaab));
    __m128i exp_shifted = _mm_srai_epi32(exp_bits, 23);
    __m128 mantissa = _mm_castsi128_ps(_mm_sub_epi32(x_bits, _mm_slli_epi32(exp_shifted, 23)));
    __m128 exp_val = _mm_cvtepi32_ps(exp_shifted);

    __m128 xm = _mm_sub_ps(mantissa, _mm_set1_ps(1.0f));
    return _mm_add_ps(rational_eval3_fma(xm, k_log2f_p, k_log2f_q), exp_val);
}

static __m256 fast_powf_avx2(__m256 base, __m256 exp) {
    return fast_pow2f_avx2(_mm256_mul_ps(fast_log2f_avx2(base), exp));
}

static __m128 fast_powf_fma(__m128 base, __m128 exp) {
    return fast_pow2f_fma(_mm_mul_ps(fast_log2f_fma(base), exp));
}

__m256 jxl_fastmath_powf_vec_x86_avx2(__m256 base, __m256 exp) {
    return fast_powf_avx2(base, exp);
}

__m128 jxl_fastmath_powf_vec_x86_fma(__m128 base, __m128 exp) {
    return fast_powf_fma(base, exp);
}

__m128 jxl_fastmath_rational_eval5_x86_fma(__m128 x, const float p[5], const float q[5]) {
    __m128 yp = _mm_set1_ps(p[4]);
    yp = _mm_fmadd_ps(yp, x, _mm_set1_ps(p[3]));
    yp = _mm_fmadd_ps(yp, x, _mm_set1_ps(p[2]));
    yp = _mm_fmadd_ps(yp, x, _mm_set1_ps(p[1]));
    yp = _mm_fmadd_ps(yp, x, _mm_set1_ps(p[0]));
    __m128 yq = _mm_set1_ps(q[4]);
    yq = _mm_fmadd_ps(yq, x, _mm_set1_ps(q[3]));
    yq = _mm_fmadd_ps(yq, x, _mm_set1_ps(q[2]));
    yq = _mm_fmadd_ps(yq, x, _mm_set1_ps(q[1]));
    yq = _mm_fmadd_ps(yq, x, _mm_set1_ps(q[0]));
    return _mm_div_ps(yp, yq);
}

__m256 jxl_fastmath_rational_eval5_x86_avx2(__m256 x, const float p[5], const float q[5]) {
    __m256 yp = _mm256_set1_ps(p[4]);
    yp = _mm256_fmadd_ps(yp, x, _mm256_set1_ps(p[3]));
    yp = _mm256_fmadd_ps(yp, x, _mm256_set1_ps(p[2]));
    yp = _mm256_fmadd_ps(yp, x, _mm256_set1_ps(p[1]));
    yp = _mm256_fmadd_ps(yp, x, _mm256_set1_ps(p[0]));
    __m256 yq = _mm256_set1_ps(q[4]);
    yq = _mm256_fmadd_ps(yq, x, _mm256_set1_ps(q[3]));
    yq = _mm256_fmadd_ps(yq, x, _mm256_set1_ps(q[2]));
    yq = _mm256_fmadd_ps(yq, x, _mm256_set1_ps(q[1]));
    yq = _mm256_fmadd_ps(yq, x, _mm256_set1_ps(q[0]));
    return _mm256_div_ps(yp, yq);
}

void jxl_fastmath_powf_f32_x86_avx2(float *samples, size_t n, float exponent) {
    size_t i;
    if (samples == NULL || n == 0) {
        return;
    }
    const __m256 exp8 = _mm256_set1_ps(exponent);
    const __m128 exp4 = _mm_set1_ps(exponent);
    i = 0;
    for (; i + 8 <= n; i += 8) {
        const __m256 base = _mm256_loadu_ps(samples + i);
        _mm256_storeu_ps(samples + i, fast_powf_avx2(base, exp8));
    }
    if (i + 4 <= n) {
        const __m128 base = _mm_loadu_ps(samples + i);
        _mm_storeu_ps(samples + i, fast_powf_fma(base, exp4));
        i += 4;
    }
    for (; i < n; ++i) {
        samples[i] = jxl_fastmath_powf(samples[i], exponent);
    }
    _mm256_zeroupper();
}

#endif /* defined(JXL_HAVE_SIMD_AVX2) */
