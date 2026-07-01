// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/color/gamut_internal.h"

#include <immintrin.h>

#if defined(JXL_HAVE_SIMD_AVX2)

static void map_gamut_vec_fma(__m128 vr, __m128 vg, __m128 vb, const float luminances[3],
                              float saturation_factor, __m128 *out_r, __m128 *out_g, __m128 *out_b) {
                                  size_t i;
    const __m128 vlr = _mm_set1_ps(luminances[0]);
    const __m128 vlg = _mm_set1_ps(luminances[1]);
    const __m128 vlb = _mm_set1_ps(luminances[2]);
    const __m128 y = _mm_fmadd_ps(vb, vlb, _mm_fmadd_ps(vg, vlg, _mm_mul_ps(vr, vlr)));

    __m128 gray_saturation = _mm_setzero_ps();
    __m128 gray_luminance = _mm_setzero_ps();
    __m128 channels[3];
    channels[0] = vr;
    channels[1] = vg;
    channels[2] = vb;

    for (i = 0; i < 3; ++i) {
        const __m128 v = channels[i];
        const __m128 v_sub_y = _mm_sub_ps(v, y);
        const __m128 inv_v_sub_y = _mm_div_ps(
            _mm_set1_ps(1.0f),
            _mm_blendv_ps(v_sub_y, _mm_set1_ps(1.0f), _mm_cmpeq_ps(v_sub_y, _mm_setzero_ps())));
        const __m128 v_over_v_sub_y = _mm_mul_ps(v, inv_v_sub_y);

        gray_saturation = _mm_blendv_ps(
            _mm_max_ps(gray_saturation, v_over_v_sub_y), gray_saturation,
            _mm_cmpge_ps(v_sub_y, _mm_setzero_ps()));
        gray_luminance = _mm_max_ps(
            _mm_blendv_ps(_mm_sub_ps(v_over_v_sub_y, inv_v_sub_y), gray_saturation,
                          _mm_cmple_ps(v_sub_y, _mm_setzero_ps())),
            gray_luminance);
    }

    __m128 gray_mix =
        _mm_fmadd_ps(_mm_set1_ps(saturation_factor), _mm_sub_ps(gray_saturation, gray_luminance),
                     gray_luminance);
    gray_mix = _mm_max_ps(_mm_setzero_ps(), _mm_min_ps(_mm_set1_ps(1.0f), gray_mix));

    const __m128 mixed_r = _mm_fmadd_ps(gray_mix, _mm_sub_ps(y, vr), vr);
    const __m128 mixed_g = _mm_fmadd_ps(gray_mix, _mm_sub_ps(y, vg), vg);
    const __m128 mixed_b = _mm_fmadd_ps(gray_mix, _mm_sub_ps(y, vb), vb);
    __m128 max_color_val = _mm_set1_ps(1.0f);
    max_color_val = _mm_max_ps(max_color_val, mixed_r);
    max_color_val = _mm_max_ps(max_color_val, mixed_g);
    max_color_val = _mm_max_ps(max_color_val, mixed_b);

    *out_r = _mm_div_ps(mixed_r, max_color_val);
    *out_g = _mm_div_ps(mixed_g, max_color_val);
    *out_b = _mm_div_ps(mixed_b, max_color_val);
}

static void map_gamut_vec_avx2(__m256 vr, __m256 vg, __m256 vb, const float luminances[3],
                               float saturation_factor, __m256 *out_r, __m256 *out_g, __m256 *out_b) {
                                   size_t i;
    const __m256 vlr = _mm256_set1_ps(luminances[0]);
    const __m256 vlg = _mm256_set1_ps(luminances[1]);
    const __m256 vlb = _mm256_set1_ps(luminances[2]);
    const __m256 y = _mm256_fmadd_ps(vb, vlb, _mm256_fmadd_ps(vg, vlg, _mm256_mul_ps(vr, vlr)));

    __m256 gray_saturation = _mm256_setzero_ps();
    __m256 gray_luminance = _mm256_setzero_ps();
    __m256 channels[3];
    channels[0] = vr;
    channels[1] = vg;
    channels[2] = vb;

    for (i = 0; i < 3; ++i) {
        const __m256 v = channels[i];
        const __m256 v_sub_y = _mm256_sub_ps(v, y);
        const __m256 inv_v_sub_y = _mm256_div_ps(
            _mm256_set1_ps(1.0f),
            _mm256_blendv_ps(v_sub_y, _mm256_set1_ps(1.0f),
                             _mm256_cmp_ps(v_sub_y, _mm256_setzero_ps(), _CMP_EQ_OQ)));
        const __m256 v_over_v_sub_y = _mm256_mul_ps(v, inv_v_sub_y);

        gray_saturation = _mm256_blendv_ps(
            _mm256_max_ps(gray_saturation, v_over_v_sub_y), gray_saturation,
            _mm256_cmp_ps(v_sub_y, _mm256_setzero_ps(), _CMP_GE_OQ));
        gray_luminance = _mm256_max_ps(
            _mm256_blendv_ps(_mm256_sub_ps(v_over_v_sub_y, inv_v_sub_y), gray_saturation,
                             _mm256_cmp_ps(v_sub_y, _mm256_setzero_ps(), _CMP_LE_OQ)),
            gray_luminance);
    }

    __m256 gray_mix = _mm256_fmadd_ps(_mm256_set1_ps(saturation_factor),
                                      _mm256_sub_ps(gray_saturation, gray_luminance), gray_luminance);
    gray_mix = _mm256_max_ps(_mm256_setzero_ps(), _mm256_min_ps(_mm256_set1_ps(1.0f), gray_mix));

    const __m256 mixed_r = _mm256_fmadd_ps(gray_mix, _mm256_sub_ps(y, vr), vr);
    const __m256 mixed_g = _mm256_fmadd_ps(gray_mix, _mm256_sub_ps(y, vg), vg);
    const __m256 mixed_b = _mm256_fmadd_ps(gray_mix, _mm256_sub_ps(y, vb), vb);
    __m256 max_color_val = _mm256_set1_ps(1.0f);
    max_color_val = _mm256_max_ps(max_color_val, mixed_r);
    max_color_val = _mm256_max_ps(max_color_val, mixed_g);
    max_color_val = _mm256_max_ps(max_color_val, mixed_b);

    *out_r = _mm256_div_ps(mixed_r, max_color_val);
    *out_g = _mm256_div_ps(mixed_g, max_color_val);
    *out_b = _mm256_div_ps(mixed_b, max_color_val);
}

void jxl_color_gamut_map_x86_avx2(float *r, float *g, float *b, size_t n, const float luminances[3],
                                  float saturation_factor) {
    size_t i;
    if (r == NULL || g == NULL || b == NULL || luminances == NULL || n == 0) {
        return;
    }
    i = 0;
    for (; i + 8 <= n; i += 8) {
        const __m256 vr = _mm256_loadu_ps(r + i);
        const __m256 vg = _mm256_loadu_ps(g + i);
        const __m256 vb = _mm256_loadu_ps(b + i);
        __m256 out_r;
        __m256 out_g;
        __m256 out_b;
        map_gamut_vec_avx2(vr, vg, vb, luminances, saturation_factor, &out_r, &out_g, &out_b);
        _mm256_storeu_ps(r + i, out_r);
        _mm256_storeu_ps(g + i, out_g);
        _mm256_storeu_ps(b + i, out_b);
    }
    for (; i + 4 <= n; i += 4) {
        const __m128 vr = _mm_loadu_ps(r + i);
        const __m128 vg = _mm_loadu_ps(g + i);
        const __m128 vb = _mm_loadu_ps(b + i);
        __m128 out_r;
        __m128 out_g;
        __m128 out_b;
        map_gamut_vec_fma(vr, vg, vb, luminances, saturation_factor, &out_r, &out_g, &out_b);
        _mm_storeu_ps(r + i, out_r);
        _mm_storeu_ps(g + i, out_g);
        _mm_storeu_ps(b + i, out_b);
    }
    _mm256_zeroupper();
    jxl_color_gamut_map_base(r + i, g + i, b + i, n - i, luminances, saturation_factor);
}

void jxl_color_gamut_map_x86_fma(float *r, float *g, float *b, size_t n, const float luminances[3],
                                 float saturation_factor) {
    size_t i;
    if (r == NULL || g == NULL || b == NULL || luminances == NULL || n == 0) {
        return;
    }
    i = 0;
    for (; i + 4 <= n; i += 4) {
        const __m128 vr = _mm_loadu_ps(r + i);
        const __m128 vg = _mm_loadu_ps(g + i);
        const __m128 vb = _mm_loadu_ps(b + i);
        __m128 out_r;
        __m128 out_g;
        __m128 out_b;
        map_gamut_vec_fma(vr, vg, vb, luminances, saturation_factor, &out_r, &out_g, &out_b);
        _mm_storeu_ps(r + i, out_r);
        _mm_storeu_ps(g + i, out_g);
        _mm_storeu_ps(b + i, out_b);
    }
    jxl_color_gamut_map_base(r + i, g + i, b + i, n - i, luminances, saturation_factor);
}

#endif /* defined(JXL_HAVE_SIMD_AVX2) */
