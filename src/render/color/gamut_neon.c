// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/color/gamut_internal.h"

#if defined(JXL_HAVE_SIMD_NEON)

#include <arm_neon.h>

static void map_gamut_vec_neon(float32x4_t vr, float32x4_t vg, float32x4_t vb,
                               const float luminances[3], float saturation_factor,
                               float32x4_t *out_r, float32x4_t *out_g, float32x4_t *out_b) {
                                   size_t i;
    const float32x4_t y = vfmaq_n_f32(vfmaq_n_f32(vmulq_n_f32(vr, luminances[0]), vg, luminances[1]),
                                      vb, luminances[2]);

    float32x4_t gray_saturation = vdupq_n_f32(0.0f);
    float32x4_t gray_luminance = vdupq_n_f32(0.0f);
    float32x4_t channels[3];
    channels[0] = vr;
    channels[1] = vg;
    channels[2] = vb;

    for (i = 0; i < 3; ++i) {
        const float32x4_t v = channels[i];
        const float32x4_t v_sub_y = vsubq_f32(v, y);
        const float32x4_t inv_v_sub_y =
            vdivq_f32(vdupq_n_f32(1.0f), vbslq_f32(vceqzq_f32(v_sub_y), vdupq_n_f32(1.0f), v_sub_y));
        const float32x4_t v_over_v_sub_y = vmulq_f32(v, inv_v_sub_y);

        gray_saturation = vbslq_f32(vcgeq_f32(v_sub_y, vdupq_n_f32(0.0f)), gray_saturation,
                                  vmaxq_f32(gray_saturation, v_over_v_sub_y));
        gray_luminance = vmaxq_f32(
            vbslq_f32(vclezq_f32(v_sub_y), gray_saturation, vsubq_f32(v_over_v_sub_y, inv_v_sub_y)),
            gray_luminance);
    }

    float32x4_t gray_mix =
        vfmaq_n_f32(gray_luminance, vsubq_f32(gray_saturation, gray_luminance), saturation_factor);
    gray_mix = vmaxq_f32(vdupq_n_f32(0.0f), vminq_f32(vdupq_n_f32(1.0f), gray_mix));

    const float32x4_t mixed_r = vfmaq_f32(vr, gray_mix, vsubq_f32(y, vr));
    const float32x4_t mixed_g = vfmaq_f32(vg, gray_mix, vsubq_f32(y, vg));
    const float32x4_t mixed_b = vfmaq_f32(vb, gray_mix, vsubq_f32(y, vb));
    float32x4_t max_color_val = vdupq_n_f32(1.0f);
    max_color_val = vmaxq_f32(max_color_val, mixed_r);
    max_color_val = vmaxq_f32(max_color_val, mixed_g);
    max_color_val = vmaxq_f32(max_color_val, mixed_b);

    *out_r = vdivq_f32(mixed_r, max_color_val);
    *out_g = vdivq_f32(mixed_g, max_color_val);
    *out_b = vdivq_f32(mixed_b, max_color_val);
}

void jxl_color_gamut_map_neon(float *r, float *g, float *b, size_t n, const float luminances[3],
                              float saturation_factor) {
    size_t i;
    if (r == NULL || g == NULL || b == NULL || luminances == NULL || n == 0) {
        return;
    }
    i = 0;
    for (; i + 4 <= n; i += 4) {
        const float32x4_t vr = vld1q_f32(r + i);
        const float32x4_t vg = vld1q_f32(g + i);
        const float32x4_t vb = vld1q_f32(b + i);
        float32x4_t out_r;
        float32x4_t out_g;
        float32x4_t out_b;
        map_gamut_vec_neon(vr, vg, vb, luminances, saturation_factor, &out_r, &out_g, &out_b);
        vst1q_f32(r + i, out_r);
        vst1q_f32(g + i, out_g);
        vst1q_f32(b + i, out_b);
    }
    jxl_color_gamut_map_base(r + i, g + i, b + i, n - i, luminances, saturation_factor);
}

#endif /* defined(JXL_HAVE_SIMD_NEON) */
