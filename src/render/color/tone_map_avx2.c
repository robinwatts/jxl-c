// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/color/tone_map_internal.h"

#include "render/color/pq_internal.h"
#include "render/color/rec2408_internal.h"

#include <immintrin.h>

#if defined(JXL_HAVE_SIMD_AVX2)

static void tone_map_luma_fma(float *luma, size_t n, float intensity_target,
                              const jxl_tone_map_params *params) {
    const __m128 vscale = _mm_set1_ps(params->scale);
    size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        const __m128 vy = _mm_loadu_ps(luma + i);
        const __m128 vy_pq = jxl_color_linear_to_pq_vec_x86_fma(vy, intensity_target);
        const __m128 vy_mapped = jxl_rec2408_eetf_vec_x86_fma(vy_pq, &params->eetf);
        const __m128 vy_linear = jxl_color_pq_to_linear_vec_x86_fma(vy_mapped, intensity_target);
        _mm_storeu_ps(luma + i, _mm_mul_ps(vy_linear, vscale));
    }
    jxl_color_tone_map_luma_base(luma + i, n - i, params, intensity_target);
}

void jxl_color_tone_map_luma_x86_avx2(float *luma, size_t n, float intensity_target,
                                      const jxl_tone_map_params *params) {
    size_t i;
    if (luma == NULL || params == NULL || n == 0) {
        return;
    }
    const __m256 vscale = _mm256_set1_ps(params->scale);
    i = 0;
    for (; i + 8 <= n; i += 8) {
        const __m256 vy = _mm256_loadu_ps(luma + i);
        const __m256 vy_pq = jxl_color_linear_to_pq_vec_x86_avx2(vy, intensity_target);
        const __m256 vy_mapped = jxl_rec2408_eetf_vec_x86_avx2(vy_pq, &params->eetf);
        const __m256 vy_linear = jxl_color_pq_to_linear_vec_x86_avx2(vy_mapped, intensity_target);
        _mm256_storeu_ps(luma + i, _mm256_mul_ps(vy_linear, vscale));
    }
    tone_map_luma_fma(luma + i, n - i, intensity_target, params);
    _mm256_zeroupper();
}

void jxl_color_tone_map_luma_x86_fma(float *luma, size_t n, float intensity_target,
                                     const jxl_tone_map_params *params) {
    tone_map_luma_fma(luma, n, intensity_target, params);
}

static void tone_map_rgb_fma(float *r, float *g, float *b, size_t n, const jxl_hdr_params *hdr,
                             const jxl_tone_map_params *params) {
    const __m128 vlr = _mm_set1_ps(hdr->luminances[0]);
    const __m128 vlg = _mm_set1_ps(hdr->luminances[1]);
    const __m128 vlb = _mm_set1_ps(hdr->luminances[2]);
    const __m128 vscale = _mm_set1_ps(params->scale);
    const __m128 absmask = _mm_castsi128_ps(_mm_set1_epi32(0x7fffffff));
    const float intensity_target = hdr->intensity_target;

    size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        const __m128 vr = _mm_loadu_ps(r + i);
        const __m128 vg = _mm_loadu_ps(g + i);
        const __m128 vb = _mm_loadu_ps(b + i);
        const __m128 vy = _mm_fmadd_ps(vb, vlb, _mm_fmadd_ps(vg, vlg, _mm_mul_ps(vr, vlr)));

        const __m128 vy_pq = jxl_color_linear_to_pq_vec_x86_fma(vy, intensity_target);
        const __m128 vy_mapped = jxl_rec2408_eetf_vec_x86_fma(vy_pq, &params->eetf);
        const __m128 vy_linear = jxl_color_pq_to_linear_vec_x86_fma(vy_mapped, intensity_target);

        const __m128 is_small = _mm_cmplt_ps(_mm_and_ps(vy, absmask), _mm_set1_ps(1e-7f));
        const __m128 vy_safe = _mm_blendv_ps(vy, _mm_set1_ps(1.0f), is_small);
        const __m128 ratio = _mm_div_ps(_mm_mul_ps(vy_linear, vscale), vy_safe);

        _mm_storeu_ps(r + i, _mm_mul_ps(vr, ratio));
        _mm_storeu_ps(g + i, _mm_mul_ps(vg, ratio));
        _mm_storeu_ps(b + i, _mm_mul_ps(vb, ratio));
    }
    jxl_color_tone_map_base(r + i, g + i, b + i, n - i, hdr, params);
}

void jxl_color_tone_map_x86_avx2(float *r, float *g, float *b, size_t n, const jxl_hdr_params *hdr,
                                 const jxl_tone_map_params *params) {
    size_t i;
    if (r == NULL || g == NULL || b == NULL || hdr == NULL || params == NULL || n == 0) {
        return;
    }
    const __m256 vlr = _mm256_set1_ps(hdr->luminances[0]);
    const __m256 vlg = _mm256_set1_ps(hdr->luminances[1]);
    const __m256 vlb = _mm256_set1_ps(hdr->luminances[2]);
    const __m256 vscale = _mm256_set1_ps(params->scale);
    const __m256 absmask = _mm256_castsi256_ps(_mm256_set1_epi32(0x7fffffff));
    const float intensity_target = hdr->intensity_target;

    i = 0;
    for (; i + 8 <= n; i += 8) {
        const __m256 vr = _mm256_loadu_ps(r + i);
        const __m256 vg = _mm256_loadu_ps(g + i);
        const __m256 vb = _mm256_loadu_ps(b + i);
        const __m256 vy = _mm256_fmadd_ps(vb, vlb, _mm256_fmadd_ps(vg, vlg, _mm256_mul_ps(vr, vlr)));

        const __m256 vy_pq = jxl_color_linear_to_pq_vec_x86_avx2(vy, intensity_target);
        const __m256 vy_mapped = jxl_rec2408_eetf_vec_x86_avx2(vy_pq, &params->eetf);
        const __m256 vy_linear = jxl_color_pq_to_linear_vec_x86_avx2(vy_mapped, intensity_target);

        const __m256 is_small =
            _mm256_cmp_ps(_mm256_and_ps(vy, absmask), _mm256_set1_ps(1e-7f), _CMP_LT_OQ);
        const __m256 vy_safe = _mm256_blendv_ps(vy, _mm256_set1_ps(1.0f), is_small);
        const __m256 ratio = _mm256_div_ps(_mm256_mul_ps(vy_linear, vscale), vy_safe);

        _mm256_storeu_ps(r + i, _mm256_mul_ps(vr, ratio));
        _mm256_storeu_ps(g + i, _mm256_mul_ps(vg, ratio));
        _mm256_storeu_ps(b + i, _mm256_mul_ps(vb, ratio));
    }
    tone_map_rgb_fma(r + i, g + i, b + i, n - i, hdr, params);
    _mm256_zeroupper();
}

void jxl_color_tone_map_x86_fma(float *r, float *g, float *b, size_t n, const jxl_hdr_params *hdr,
                                const jxl_tone_map_params *params) {
    tone_map_rgb_fma(r, g, b, n, hdr, params);
}

float jxl_color_detect_peak_luminance_x86_avx2(const float *r, const float *g, const float *b,
                                               size_t n, const float luminances[3]) {
    const __m256 vlr = _mm256_set1_ps(luminances[0]);
    const __m256 vlg = _mm256_set1_ps(luminances[1]);
    const __m256 vlb = _mm256_set1_ps(luminances[2]);
    __m256 peak = _mm256_setzero_ps();

    size_t i = 0;
    float peak_arr[4];
    float peak_scalar;
    for (; i + 8 <= n; i += 8) {
        const __m256 vr = _mm256_loadu_ps(r + i);
        const __m256 vg = _mm256_loadu_ps(g + i);
        const __m256 vb = _mm256_loadu_ps(b + i);
        const __m256 vy = _mm256_fmadd_ps(vb, vlb, _mm256_fmadd_ps(vg, vlg, _mm256_mul_ps(vr, vlr)));
        peak = _mm256_max_ps(peak, vy);
    }

    __m128 peak128 = _mm_max_ps(_mm256_extractf128_ps(peak, 0), _mm256_extractf128_ps(peak, 1));

    if (i + 4 <= n) {
        const __m128 vlr128 = _mm256_extractf128_ps(vlr, 0);
        const __m128 vlg128 = _mm256_extractf128_ps(vlg, 0);
        const __m128 vlb128 = _mm256_extractf128_ps(vlb, 0);
        const __m128 vr = _mm_loadu_ps(r + i);
        const __m128 vg = _mm_loadu_ps(g + i);
        const __m128 vb = _mm_loadu_ps(b + i);
        __m128 vy = _mm_mul_ps(vr, vlr128);
        vy = _mm_add_ps(vy, _mm_mul_ps(vg, vlg128));
        vy = _mm_add_ps(vy, _mm_mul_ps(vb, vlb128));
        peak128 = _mm_max_ps(peak128, vy);
        i += 4;
    }

    _mm_storeu_ps(peak_arr, peak128);
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
    _mm256_zeroupper();
    return peak_scalar <= 0.0f ? 1.0f : peak_scalar;
}

#endif /* defined(JXL_HAVE_SIMD_AVX2) */
