// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/color/rec2408_internal.h"

#include <immintrin.h>
#include "jxl_oxide/jxl_types.h"

#if defined(JXL_HAVE_SIMD_SSE41)

__m128 jxl_rec2408_eetf_vec_x86_sse2(__m128 from_pq_sample, const jxl_rec2408_eetf_params *params) {
    const __m128 v_min_source_luminance = _mm_set1_ps(params->min_source_luminance);
    const __m128 v_source_pq_diff = _mm_set1_ps(params->source_pq_diff);
    const __m128 v_ks = _mm_set1_ps(params->ks);
    const __m128 v_b = _mm_set1_ps(params->b);
    const __m128 v_one_sub_ks = _mm_set1_ps(params->one_sub_ks);
    const __m128 v_max_luminance = _mm_set1_ps(params->max_luminance);

    const __m128 normalized_source_pq_sample =
        _mm_div_ps(_mm_sub_ps(from_pq_sample, v_min_source_luminance), v_source_pq_diff);

    const __m128 t = _mm_div_ps(_mm_sub_ps(normalized_source_pq_sample, v_ks), v_one_sub_ks);
    const __m128 t_p2 = _mm_mul_ps(t, t);
    const __m128 t_p3 = _mm_mul_ps(t_p2, t);

    const __m128 term_ks = _mm_add_ps(
        _mm_mul_ps(t_p3, _mm_set1_ps(2.0f)),
        _mm_add_ps(_mm_mul_ps(t_p2, _mm_set1_ps(-3.0f)), _mm_set1_ps(1.0f)));
    const __m128 inner = _mm_add_ps(_mm_mul_ps(t_p2, _mm_set1_ps(-2.0f)), _mm_add_ps(t_p3, t));
    const __m128 max_term = _mm_mul_ps(
        v_max_luminance,
        _mm_add_ps(_mm_mul_ps(t_p3, _mm_set1_ps(-2.0f)), _mm_mul_ps(t_p2, _mm_set1_ps(3.0f))));
    const __m128 compressed_pq_sample = _mm_add_ps(
        _mm_mul_ps(v_ks, term_ks), _mm_add_ps(_mm_mul_ps(v_one_sub_ks, inner), max_term));

    const __m128 is_small = _mm_cmplt_ps(normalized_source_pq_sample, v_ks);
    const __m128 compressed = _mm_or_ps(_mm_andnot_ps(is_small, compressed_pq_sample),
                                        _mm_and_ps(is_small, normalized_source_pq_sample));

    const __m128 x = _mm_sub_ps(_mm_set1_ps(1.0f), compressed);
    const __m128 x2 = _mm_mul_ps(x, x);
    const __m128 one_sub_compressed_p4 = _mm_mul_ps(x2, x2);
    const __m128 normalized_target_pq_sample =
        _mm_add_ps(_mm_mul_ps(v_b, one_sub_compressed_p4), compressed);

    return _mm_add_ps(_mm_mul_ps(normalized_target_pq_sample, v_source_pq_diff), v_min_source_luminance);
}

void jxl_rec2408_eetf_pq_x86_sse2(float *samples, size_t n, const jxl_rec2408_eetf_params *params) {
    size_t i;
    if (samples == NULL || n == 0 || params == NULL) {
        return;
    }
    i = 0;
    for (; i + 4 <= n; i += 4) {
        const __m128 v = _mm_loadu_ps(samples + i);
        _mm_storeu_ps(samples + i, jxl_rec2408_eetf_vec_x86_sse2(v, params));
    }
    jxl_rec2408_eetf_pq_base(samples + i, n - i, params);
}

#endif /* defined(JXL_HAVE_SIMD_SSE41) */
