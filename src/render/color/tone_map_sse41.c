// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/color/tone_map_internal.h"

#include "render/color/pq_internal.h"
#include "render/color/rec2408_internal.h"

#include <immintrin.h>

#if defined(JXL_HAVE_SIMD_SSE41)

void jxl_color_tone_map_luma_x86_sse2(float *luma, size_t n, float intensity_target,
                                      const jxl_tone_map_params *params) {
    size_t i;
    if (luma == NULL || params == NULL || n == 0) {
        return;
    }
    const __m128 vscale = _mm_set1_ps(params->scale);
    i = 0;
    for (; i + 4 <= n; i += 4) {
        const __m128 vy = _mm_loadu_ps(luma + i);
        const __m128 vy_pq = jxl_color_linear_to_pq_vec_x86_sse2(vy, intensity_target);
        const __m128 vy_mapped = jxl_rec2408_eetf_vec_x86_sse2(vy_pq, &params->eetf);
        const __m128 vy_linear = jxl_color_pq_to_linear_vec_x86_sse2(vy_mapped, intensity_target);
        _mm_storeu_ps(luma + i, _mm_mul_ps(vy_linear, vscale));
    }
    jxl_color_tone_map_luma_base(luma + i, n - i, params, intensity_target);
}

void jxl_color_tone_map_x86_sse2(float *r, float *g, float *b, size_t n, const jxl_hdr_params *hdr,
                                 const jxl_tone_map_params *params) {
    size_t i;
    if (r == NULL || g == NULL || b == NULL || hdr == NULL || params == NULL || n == 0) {
        return;
    }
    const __m128 vlr = _mm_set1_ps(hdr->luminances[0]);
    const __m128 vlg = _mm_set1_ps(hdr->luminances[1]);
    const __m128 vlb = _mm_set1_ps(hdr->luminances[2]);
    const __m128 vscale = _mm_set1_ps(params->scale);
    const __m128 absmask = _mm_castsi128_ps(_mm_set1_epi32(0x7fffffff));
    const float intensity_target = hdr->intensity_target;

    i = 0;
    for (; i + 4 <= n; i += 4) {
        const __m128 vr = _mm_loadu_ps(r + i);
        const __m128 vg = _mm_loadu_ps(g + i);
        const __m128 vb = _mm_loadu_ps(b + i);
        __m128 vy = _mm_mul_ps(vr, vlr);
        vy = _mm_add_ps(vy, _mm_mul_ps(vg, vlg));
        vy = _mm_add_ps(vy, _mm_mul_ps(vb, vlb));

        const __m128 vy_pq = jxl_color_linear_to_pq_vec_x86_sse2(vy, intensity_target);
        const __m128 vy_mapped = jxl_rec2408_eetf_vec_x86_sse2(vy_pq, &params->eetf);
        const __m128 vy_linear = jxl_color_pq_to_linear_vec_x86_sse2(vy_mapped, intensity_target);

        const __m128 is_small = _mm_cmplt_ps(_mm_and_ps(vy, absmask), _mm_set1_ps(1e-7f));
        vy = _mm_or_ps(_mm_andnot_ps(is_small, vy), _mm_and_ps(is_small, _mm_set1_ps(1.0f)));
        const __m128 ratio = _mm_div_ps(_mm_mul_ps(vy_linear, vscale), vy);

        _mm_storeu_ps(r + i, _mm_mul_ps(vr, ratio));
        _mm_storeu_ps(g + i, _mm_mul_ps(vg, ratio));
        _mm_storeu_ps(b + i, _mm_mul_ps(vb, ratio));
    }
    jxl_color_tone_map_base(r + i, g + i, b + i, n - i, hdr, params);
}

float jxl_color_detect_peak_luminance_x86_sse2(const float *r, const float *g, const float *b,
                                               size_t n, const float luminances[3]) {
    const __m128 vlr = _mm_set1_ps(luminances[0]);
    const __m128 vlg = _mm_set1_ps(luminances[1]);
    const __m128 vlb = _mm_set1_ps(luminances[2]);
    __m128 peak = _mm_setzero_ps();

    size_t i = 0;
    float peak_arr[4];
    float peak_scalar;
    for (; i + 4 <= n; i += 4) {
        const __m128 vr = _mm_loadu_ps(r + i);
        const __m128 vg = _mm_loadu_ps(g + i);
        const __m128 vb = _mm_loadu_ps(b + i);
        __m128 vy = _mm_mul_ps(vr, vlr);
        vy = _mm_add_ps(vy, _mm_mul_ps(vg, vlg));
        vy = _mm_add_ps(vy, _mm_mul_ps(vb, vlb));
        peak = _mm_max_ps(peak, vy);
    }

    _mm_storeu_ps(peak_arr, peak);
    peak_scalar = peak_arr[0];
    if (peak_arr[1] > peak_scalar) {
        peak_scalar = peak_arr[1];
    }
    if (peak_arr[2] > peak_scalar) {
        peak_scalar = peak_arr[2];
    }
    if (peak_arr[3] > peak_scalar) {
        peak_scalar = peak_arr[3];
    }

    const float lr = luminances[0];
    const float lg = luminances[1];
    const float lb = luminances[2];
    for (; i < n; ++i) {
        const float y = r[i] * lr + g[i] * lg + b[i] * lb;
        if (y > peak_scalar) {
            peak_scalar = y;
        }
    }
    return peak_scalar <= 0.0f ? 1.0f : peak_scalar;
}

#endif /* defined(JXL_HAVE_SIMD_SSE41) */
