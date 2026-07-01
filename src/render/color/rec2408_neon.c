// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/color/rec2408_internal.h"

#if defined(JXL_HAVE_SIMD_NEON)

#include <arm_neon.h>

float32x4_t jxl_rec2408_eetf_vec_neon(float32x4_t from_pq_sample,
                                      const jxl_rec2408_eetf_params *params) {
    const float32x4_t v_min_source_luminance = vdupq_n_f32(params->min_source_luminance);
    const float32x4_t v_source_pq_diff = vdupq_n_f32(params->source_pq_diff);

    const float32x4_t normalized_source_pq_sample =
        vmulq_n_f32(vsubq_f32(from_pq_sample, v_min_source_luminance), 1.0f / params->source_pq_diff);

    const float ks = params->ks;
    const float one_sub_ks = params->one_sub_ks;
    const float max_luminance = params->max_luminance;
    const float b = params->b;

    const float32x4_t t =
        vmulq_n_f32(vsubq_f32(normalized_source_pq_sample, vdupq_n_f32(ks)), 1.0f / one_sub_ks);
    const float32x4_t t_p2 = vmulq_f32(t, t);
    const float32x4_t t_p3 = vmulq_f32(t_p2, t);

    const float32x4_t compressed_pq_sample = vfmaq_n_f32(
        vfmaq_n_f32(
            vmulq_n_f32(vfmaq_n_f32(vmulq_n_f32(t_p2, 3.0f), t_p3, -2.0f), max_luminance),
            vfmaq_n_f32(vaddq_f32(t_p3, t), t_p2, -2.0f), one_sub_ks),
        vfmaq_n_f32(vfmaq_n_f32(vdupq_n_f32(1.0f), t_p2, -3.0f), t_p3, 2.0f), ks);

    const uint32x4_t is_small = vcltq_f32(normalized_source_pq_sample, vdupq_n_f32(ks));
    const float32x4_t compressed =
        vbslq_f32(is_small, normalized_source_pq_sample, compressed_pq_sample);

    const float32x4_t x = vsubq_f32(vdupq_n_f32(1.0f), compressed);
    const float32x4_t x2 = vmulq_f32(x, x);
    const float32x4_t one_sub_compressed_p4 = vmulq_f32(x2, x2);
    const float32x4_t normalized_target_pq_sample =
        vfmaq_n_f32(compressed, one_sub_compressed_p4, b);

    return vfmaq_n_f32(v_min_source_luminance, normalized_target_pq_sample, params->source_pq_diff);
}

void jxl_rec2408_eetf_pq_neon(float *samples, size_t n, const jxl_rec2408_eetf_params *params) {
    size_t i;
    if (samples == NULL || n == 0 || params == NULL) {
        return;
    }
    i = 0;
    for (; i + 4 <= n; i += 4) {
        const float32x4_t v = vld1q_f32(samples + i);
        vst1q_f32(samples + i, jxl_rec2408_eetf_vec_neon(v, params));
    }
    jxl_rec2408_eetf_pq_base(samples + i, n - i, params);
}

#endif /* defined(JXL_HAVE_SIMD_NEON) */
