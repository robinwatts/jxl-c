// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/color/gamut_internal.h"

#include <immintrin.h>

#if defined(JXL_HAVE_SIMD_SSE41)

static void map_gamut_vec_sse2(__m128 vr, __m128 vg, __m128 vb, const float luminances[3],
                               float saturation_factor, __m128 *out_r, __m128 *out_g, __m128 *out_b) {
                                   size_t i;
    const __m128 vlr = _mm_set1_ps(luminances[0]);
    const __m128 vlg = _mm_set1_ps(luminances[1]);
    const __m128 vlb = _mm_set1_ps(luminances[2]);
    const __m128 y = _mm_add_ps(_mm_add_ps(_mm_mul_ps(vr, vlr), _mm_mul_ps(vg, vlg)), _mm_mul_ps(vb, vlb));

    __m128 gray_saturation = _mm_setzero_ps();
    __m128 gray_luminance = _mm_setzero_ps();
    __m128 channels[3];
    channels[0] = vr;
    channels[1] = vg;
    channels[2] = vb;

    for (i = 0; i < 3; ++i) {
        const __m128 v = channels[i];
        const __m128 v_sub_y = _mm_sub_ps(v, y);
        const __m128 zero_mask = _mm_cmpeq_ps(v_sub_y, _mm_setzero_ps());
        const __m128 inv_v_sub_y = _mm_div_ps(
            _mm_set1_ps(1.0f),
            _mm_or_ps(_mm_andnot_ps(zero_mask, v_sub_y), _mm_and_ps(zero_mask, _mm_set1_ps(1.0f))));
        const __m128 v_over_v_sub_y = _mm_mul_ps(v, inv_v_sub_y);

        const __m128 neg_mask = _mm_cmplt_ps(v_sub_y, _mm_setzero_ps());
        gray_saturation = _mm_or_ps(
            _mm_and_ps(neg_mask, _mm_max_ps(gray_saturation, v_over_v_sub_y)),
            _mm_andnot_ps(neg_mask, gray_saturation));

        const __m128 nonpos_mask = _mm_cmple_ps(v_sub_y, _mm_setzero_ps());
        const __m128 gl = _mm_or_ps(
            _mm_and_ps(nonpos_mask, gray_saturation),
            _mm_andnot_ps(nonpos_mask, _mm_sub_ps(v_over_v_sub_y, inv_v_sub_y)));
        gray_luminance = _mm_max_ps(gray_luminance, gl);
    }

    __m128 gray_mix = _mm_add_ps(
        _mm_mul_ps(_mm_set1_ps(saturation_factor), _mm_sub_ps(gray_saturation, gray_luminance)),
        gray_luminance);
    gray_mix = _mm_max_ps(_mm_setzero_ps(), _mm_min_ps(_mm_set1_ps(1.0f), gray_mix));

    const __m128 mixed_r = _mm_add_ps(_mm_mul_ps(gray_mix, _mm_sub_ps(y, vr)), vr);
    const __m128 mixed_g = _mm_add_ps(_mm_mul_ps(gray_mix, _mm_sub_ps(y, vg)), vg);
    const __m128 mixed_b = _mm_add_ps(_mm_mul_ps(gray_mix, _mm_sub_ps(y, vb)), vb);
    __m128 max_color_val = _mm_set1_ps(1.0f);
    max_color_val = _mm_max_ps(max_color_val, mixed_r);
    max_color_val = _mm_max_ps(max_color_val, mixed_g);
    max_color_val = _mm_max_ps(max_color_val, mixed_b);

    *out_r = _mm_div_ps(mixed_r, max_color_val);
    *out_g = _mm_div_ps(mixed_g, max_color_val);
    *out_b = _mm_div_ps(mixed_b, max_color_val);
}

void jxl_color_gamut_map_x86_sse2(float *r, float *g, float *b, size_t n, const float luminances[3],
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
        map_gamut_vec_sse2(vr, vg, vb, luminances, saturation_factor, &out_r, &out_g, &out_b);
        _mm_storeu_ps(r + i, out_r);
        _mm_storeu_ps(g + i, out_g);
        _mm_storeu_ps(b + i, out_b);
    }
    jxl_color_gamut_map_base(r + i, g + i, b + i, n - i, luminances, saturation_factor);
}

#endif /* defined(JXL_HAVE_SIMD_SSE41) */
