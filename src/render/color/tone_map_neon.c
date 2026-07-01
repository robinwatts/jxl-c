// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/color/tone_map_internal.h"
#include "render/color/pq_internal.h"
#include "render/color/rec2408_internal.h"

#if defined(JXL_HAVE_SIMD_NEON)

#include <arm_neon.h>

void jxl_color_tone_map_luma_neon(float *luma, size_t n, float intensity_target,
                                  const jxl_tone_map_params *params) {
    size_t i;
    if (luma == NULL || params == NULL || n == 0) {
        return;
    }
    i = 0;
    for (; i + 4 <= n; i += 4) {
        const float32x4_t vy = vld1q_f32(luma + i);
        const float32x4_t vy_pq = jxl_color_linear_to_pq_vec_neon(vy, intensity_target);
        const float32x4_t vy_mapped = jxl_rec2408_eetf_vec_neon(vy_pq, &params->eetf);
        const float32x4_t vy_linear = jxl_color_pq_to_linear_vec_neon(vy_mapped, intensity_target);
        vst1q_f32(luma + i, vmulq_n_f32(vy_linear, params->scale));
    }
    jxl_color_tone_map_luma_base(luma + i, n - i, params, intensity_target);
}

void jxl_color_tone_map_neon(float *r, float *g, float *b, size_t n, const jxl_hdr_params *hdr,
                             const jxl_tone_map_params *params) {
    size_t i;
    if (r == NULL || g == NULL || b == NULL || hdr == NULL || params == NULL || n == 0) {
        return;
    }
    const float lr = hdr->luminances[0];
    const float lg = hdr->luminances[1];
    const float lb = hdr->luminances[2];
    const float intensity_target = hdr->intensity_target;

    i = 0;
    for (; i + 4 <= n; i += 4) {
        const float32x4_t vr = vld1q_f32(r + i);
        const float32x4_t vg = vld1q_f32(g + i);
        const float32x4_t vb = vld1q_f32(b + i);
        float32x4_t vy = vfmaq_n_f32(vfmaq_n_f32(vmulq_n_f32(vr, lr), vg, lg), vb, lb);

        const float32x4_t vy_pq = jxl_color_linear_to_pq_vec_neon(vy, intensity_target);
        const float32x4_t vy_mapped = jxl_rec2408_eetf_vec_neon(vy_pq, &params->eetf);
        const float32x4_t vy_linear = jxl_color_pq_to_linear_vec_neon(vy_mapped, intensity_target);

        const uint32x4_t is_small = vcleq_f32(vabsq_f32(vy), vdupq_n_f32(1e-7f));
        vy = vbslq_f32(is_small, vdupq_n_f32(1.0f), vy);
        const float32x4_t ratio = vdivq_f32(vmulq_n_f32(vy_linear, params->scale), vy);

        vst1q_f32(r + i, vmulq_f32(vr, ratio));
        vst1q_f32(g + i, vmulq_f32(vg, ratio));
        vst1q_f32(b + i, vmulq_f32(vb, ratio));
    }
    jxl_color_tone_map_base(r + i, g + i, b + i, n - i, hdr, params);
}

float jxl_color_detect_peak_luminance_neon(const float *r, const float *g, const float *b, size_t n,
                                           const float luminances[3]) {
    const float lr = luminances[0];
    const float lg = luminances[1];
    const float lb = luminances[2];
    size_t i;
    float32x4_t peak = vdupq_n_f32(0.0f);

    i = 0;
    for (; i + 4 <= n; i += 4) {
        const float32x4_t vr = vld1q_f32(r + i);
        const float32x4_t vg = vld1q_f32(g + i);
        const float32x4_t vb = vld1q_f32(b + i);
        const float32x4_t vy = vfmaq_n_f32(vfmaq_n_f32(vmulq_n_f32(vr, lr), vg, lg), vb, lb);
        peak = vmaxq_f32(peak, vy);
    }

    float peak_scalar = vmaxvq_f32(peak);
    for (; i < n; ++i) {
        const float y = r[i] * lr + g[i] * lg + b[i] * lb;
        if (y > peak_scalar) {
            peak_scalar = y;
        }
    }
    return peak_scalar <= 0.0f ? 1.0f : peak_scalar;
}

#endif /* defined(JXL_HAVE_SIMD_NEON) */
