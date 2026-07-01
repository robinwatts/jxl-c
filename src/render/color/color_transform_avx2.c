// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/color/color_transform_internal.h"
#include "render/color/fastmath_internal.h"

#include <immintrin.h>
#include <smmintrin.h>
#include "jxl_oxide/jxl_types.h"
#include <string.h>

#if defined(JXL_HAVE_SIMD_AVX2)

static const uint8_t k_srgb_powtable_upper[16] = {
    0x00, 0x0a, 0x19, 0x26, 0x32, 0x41, 0x4d, 0x5c,
    0x68, 0x75, 0x83, 0x8f, 0xa0, 0xaa, 0xb9, 0xc6,
};
static const uint8_t k_srgb_powtable_lower[16] = {
    0x00, 0xb7, 0x04, 0x0d, 0xcb, 0xe7, 0x41, 0x68,
    0x51, 0xd1, 0xeb, 0xf2, 0x00, 0xb7, 0x04, 0x0d,
};

void jxl_color_linear_to_srgb_x86_avx2(float *samples, size_t n) {
    size_t i;
    if (samples == NULL || n == 0) {
        return;
    }

    const __m128i tbl_up = _mm_loadu_si128((const __m128i *)k_srgb_powtable_upper);
    const __m128i tbl_lo = _mm_loadu_si128((const __m128i *)k_srgb_powtable_lower);
    const __m256i powtable_upper = _mm256_set_m128i(tbl_up, tbl_up);
    const __m256i powtable_lower = _mm256_set_m128i(tbl_lo, tbl_lo);
    const __m256 sign_mask = _mm256_set1_ps(-0.0f);

    i = 0;
    for (; i + 8 <= n; i += 8) {
        __m256 v = _mm256_loadu_ps(samples + i);
        const __m256 sign = _mm256_and_ps(sign_mask, v);
        v = _mm256_andnot_ps(sign_mask, v);

        const __m256 v_adj = _mm256_and_ps(_mm256_castsi256_ps(_mm256_set1_epi32(0x3effffff)),
                                           _mm256_or_ps(_mm256_castsi256_ps(_mm256_set1_epi32(0x3e800000)), v));

        __m256 powv = _mm256_set1_ps(0.059914046f);
        powv = _mm256_fmadd_ps(powv, v_adj, _mm256_set1_ps(-0.10889456f));
        powv = _mm256_fmadd_ps(powv, v_adj, _mm256_set1_ps(0.107963754f));
        powv = _mm256_fmadd_ps(powv, v_adj, _mm256_set1_ps(0.018092343f));

        const __m256i exp_idx = _mm256_sub_epi32(
            _mm256_srai_epi32(_mm256_castps_si256(v), 23), _mm256_set1_epi32(118));
        const __m256i pow_upper = _mm256_shuffle_epi8(powtable_upper, exp_idx);
        const __m256i pow_lower = _mm256_shuffle_epi8(powtable_lower, exp_idx);
        const __m256 mul = _mm256_castsi256_ps(_mm256_or_si256(
            _mm256_or_si256(_mm256_slli_epi32(pow_upper, 18), _mm256_slli_epi32(pow_lower, 10)),
            _mm256_set1_epi32(0x40000000)));

        const __m256 small = _mm256_mul_ps(v, _mm256_set1_ps(12.92f));
        const __m256 acc = _mm256_fmadd_ps(powv, mul, _mm256_set1_ps(-0.055f));
        const __m256 mask = _mm256_cmp_ps(v, _mm256_set1_ps(0.0031308f), _CMP_LE_OS);
        __m256 ret = _mm256_blendv_ps(acc, small, mask);
        ret = _mm256_or_ps(ret, sign);
        _mm256_storeu_ps(samples + i, ret);
    }
    _mm256_zeroupper();
    jxl_color_linear_to_srgb_base(samples + i, n - i);
}

static const float k_srgb_eotf_p[5] = {
    2.200248328e-4f, 1.043637593e-2f, 1.624820318e-1f, 7.961564959e-1f, 8.210152774e-1f,
};
static const float k_srgb_eotf_q[5] = {
    2.631846970e-1f, 1.076976492f, 4.987528350e-1f, -5.512498495e-2f, 6.521209011e-3f,
};

static __m128 srgb_to_linear_vec_fma(__m128 v) {
    const __m128 sign_mask = _mm_castsi128_ps(_mm_set1_epi32((int32_t)0x80000000u));
    const __m128 sign = _mm_and_ps(sign_mask, v);
    const __m128 a = _mm_andnot_ps(sign_mask, v);
    const __m128 small = _mm_div_ps(a, _mm_set1_ps(12.92f));
    const __m128 large = jxl_fastmath_rational_eval5_x86_fma(a, k_srgb_eotf_p, k_srgb_eotf_q);
    const __m128 mask = _mm_cmple_ps(a, _mm_set1_ps(0.04045f));
    const __m128 out = _mm_blendv_ps(large, small, mask);
    return _mm_or_ps(_mm_andnot_ps(sign_mask, out), sign);
}

static __m256 srgb_to_linear_vec_avx2(__m256 v) {
    const __m256 sign_mask = _mm256_castsi256_ps(_mm256_set1_epi32((int32_t)0x80000000u));
    const __m256 sign = _mm256_and_ps(sign_mask, v);
    const __m256 a = _mm256_andnot_ps(sign_mask, v);
    const __m256 small = _mm256_div_ps(a, _mm256_set1_ps(12.92f));
    const __m256 large = jxl_fastmath_rational_eval5_x86_avx2(a, k_srgb_eotf_p, k_srgb_eotf_q);
    const __m256 mask = _mm256_cmp_ps(a, _mm256_set1_ps(0.04045f), _CMP_LE_OS);
    const __m256 out = _mm256_blendv_ps(large, small, mask);
    return _mm256_or_ps(_mm256_andnot_ps(sign_mask, out), sign);
}

void jxl_color_srgb_to_linear_x86_avx2(float *samples, size_t n) {
    size_t i;
    if (samples == NULL || n == 0) {
        return;
    }
    i = 0;
    for (; i + 8 <= n; i += 8) {
        const __m256 v = _mm256_loadu_ps(samples + i);
        _mm256_storeu_ps(samples + i, srgb_to_linear_vec_avx2(v));
    }
    for (; i + 4 <= n; i += 4) {
        const __m128 v = _mm_loadu_ps(samples + i);
        _mm_storeu_ps(samples + i, srgb_to_linear_vec_fma(v));
    }
    _mm256_zeroupper();
    jxl_color_srgb_to_linear_base(samples + i, n - i);
}

void jxl_color_srgb_to_linear_x86_fma(float *samples, size_t n) {
    size_t i;
    if (samples == NULL || n == 0) {
        return;
    }
    i = 0;
    for (; i + 4 <= n; i += 4) {
        const __m128 v = _mm_loadu_ps(samples + i);
        _mm_storeu_ps(samples + i, srgb_to_linear_vec_fma(v));
    }
    jxl_color_srgb_to_linear_base(samples + i, n - i);
}

void jxl_color_linear_to_bt709_x86_avx2(float *samples, size_t n) {
    size_t i;
    if (samples == NULL || n == 0) {
        return;
    }
    const __m256 thresh = _mm256_set1_ps(0.018f);
    const __m256 lin_scale = _mm256_set1_ps(4.5f);
    const __m256 exp_scale = _mm256_set1_ps(1.099f);
    const __m256 exp_bias = _mm256_set1_ps(-0.099f);
    const __m256 exp8 = _mm256_set1_ps(0.45f);
    const __m128 exp4 = _mm_set1_ps(0.45f);

    i = 0;
    for (; i + 8 <= n; i += 8) {
        const __m256 v = _mm256_loadu_ps(samples + i);
        const __m256 mask = _mm256_cmp_ps(v, thresh, _CMP_LE_OS);
        const __m256 lin = _mm256_mul_ps(v, lin_scale);
        const __m256 powv = jxl_fastmath_powf_vec_x86_avx2(v, exp8);
        const __m256 hi = _mm256_fmadd_ps(powv, exp_scale, exp_bias);
        const __m256 out = _mm256_blendv_ps(hi, lin, mask);
        _mm256_storeu_ps(samples + i, out);
    }

    if (i + 4 <= n) {
        const __m128 v = _mm_loadu_ps(samples + i);
        const __m128 mask = _mm_cmple_ps(v, _mm_set1_ps(0.018f));
        const __m128 lin = _mm_mul_ps(v, _mm_set1_ps(4.5f));
        const __m128 powv = jxl_fastmath_powf_vec_x86_fma(v, exp4);
        const __m128 hi = _mm_fmadd_ps(powv, _mm_set1_ps(1.099f), _mm_set1_ps(-0.099f));
        const __m128 out = _mm_blendv_ps(hi, lin, mask);
        _mm_storeu_ps(samples + i, out);
        i += 4;
    }

    _mm256_zeroupper();
    jxl_color_linear_to_bt709_base(samples + i, n - i);
}

static void bt709_to_linear_vec_fma(__m128 v, __m128 *out) {
    const __m128 thresh = _mm_set1_ps(0.081f);
    const __m128 mask = _mm_cmple_ps(v, thresh);
    const __m128 lin = _mm_mul_ps(v, _mm_set1_ps(1.0f / 4.5f));
    const __m128 hi_in =
        _mm_add_ps(_mm_mul_ps(v, _mm_set1_ps(1.0f / 1.099f)), _mm_set1_ps(0.099f / 1.099f));
    const __m128 hi = jxl_fastmath_powf_vec_x86_fma(hi_in, _mm_set1_ps(1.0f / 0.45f));
    *out = _mm_blendv_ps(hi, lin, mask);
}

void jxl_color_bt709_to_linear_x86_fma(float *samples, size_t n) {
    size_t i;
    if (samples == NULL || n == 0) {
        return;
    }
    i = 0;
    for (; i + 4 <= n; i += 4) {
        const __m128 v = _mm_loadu_ps(samples + i);
        __m128 out;
        bt709_to_linear_vec_fma(v, &out);
        _mm_storeu_ps(samples + i, out);
    }
    jxl_color_bt709_to_linear_base(samples + i, n - i);
}

void jxl_color_bt709_to_linear_x86_avx2(float *samples, size_t n) {
    size_t i;
    if (samples == NULL || n == 0) {
        return;
    }
    const __m256 thresh = _mm256_set1_ps(0.081f);
    const __m256 lin_scale = _mm256_set1_ps(1.0f / 4.5f);
    const __m256 hi_scale = _mm256_set1_ps(1.0f / 1.099f);
    const __m256 hi_bias = _mm256_set1_ps(0.099f / 1.099f);
    const __m256 exp8 = _mm256_set1_ps(1.0f / 0.45f);
    const __m128 exp4 = _mm_set1_ps(1.0f / 0.45f);

    i = 0;
    for (; i + 8 <= n; i += 8) {
        const __m256 v = _mm256_loadu_ps(samples + i);
        const __m256 mask = _mm256_cmp_ps(v, thresh, _CMP_LE_OS);
        const __m256 lin = _mm256_mul_ps(v, lin_scale);
        const __m256 hi_in = _mm256_fmadd_ps(v, hi_scale, hi_bias);
        const __m256 hi = jxl_fastmath_powf_vec_x86_avx2(hi_in, exp8);
        const __m256 out = _mm256_blendv_ps(hi, lin, mask);
        _mm256_storeu_ps(samples + i, out);
    }
    if (i + 4 <= n) {
        const __m128 v = _mm_loadu_ps(samples + i);
        __m128 out;
        bt709_to_linear_vec_fma(v, &out);
        _mm_storeu_ps(samples + i, out);
        i += 4;
    }
    _mm256_zeroupper();
    jxl_color_bt709_to_linear_base(samples + i, n - i);
}

void jxl_color_apply_gamma_x86_fma(float *samples, size_t n, float gamma) {
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
        const __m128 powv = jxl_fastmath_powf_vec_x86_fma(v, exp_v);
        _mm_storeu_ps(samples + i, _mm_andnot_ps(mask, powv));
    }
    jxl_color_apply_gamma_base(samples + i, n - i, gamma);
}

void jxl_color_apply_gamma_x86_avx2(float *samples, size_t n, float gamma) {
    size_t i;
    if (samples == NULL || n == 0) {
        return;
    }
    const __m256 thresh = _mm256_set1_ps(1e-7f);
    const __m256 exp8 = _mm256_set1_ps(gamma);
    const __m128 exp4 = _mm_set1_ps(gamma);
    i = 0;
    for (; i + 8 <= n; i += 8) {
        const __m256 v = _mm256_loadu_ps(samples + i);
        const __m256 mask = _mm256_cmp_ps(v, thresh, _CMP_LE_OS);
        const __m256 powv = jxl_fastmath_powf_vec_x86_avx2(v, exp8);
        _mm256_storeu_ps(samples + i, _mm256_andnot_ps(mask, powv));
    }
    if (i + 4 <= n) {
        const __m128 v = _mm_loadu_ps(samples + i);
        const __m128 mask = _mm_cmple_ps(v, _mm_set1_ps(1e-7f));
        const __m128 powv = jxl_fastmath_powf_vec_x86_fma(v, exp4);
        _mm_storeu_ps(samples + i, _mm_andnot_ps(mask, powv));
        i += 4;
    }
    _mm256_zeroupper();
    jxl_color_apply_gamma_base(samples + i, n - i, gamma);
}

#endif /* defined(JXL_HAVE_SIMD_AVX2) */
