// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/color/opsin_internal.h"

#if defined(JXL_HAVE_SIMD_NEON)

#include <arm_neon.h>

static void opsin_xyb_to_linear_rgb_vec(float32x4_t vx, float32x4_t vy, float32x4_t vb,
                                        const float ob[3], const float cbrt_ob[3], float itscale,
                                        const float m[9], float32x4_t *out_x, float32x4_t *out_y,
                                        float32x4_t *out_b) {
    const float32x4_t v_itscale = vdupq_n_f32(itscale);
    const float32x4_t v_ob0 = vdupq_n_f32(ob[0]);
    const float32x4_t v_ob1 = vdupq_n_f32(ob[1]);
    const float32x4_t v_ob2 = vdupq_n_f32(ob[2]);
    const float32x4_t v_cbrt0 = vdupq_n_f32(cbrt_ob[0]);
    const float32x4_t v_cbrt1 = vdupq_n_f32(cbrt_ob[1]);
    const float32x4_t v_cbrt2 = vdupq_n_f32(cbrt_ob[2]);
    float32x4_t g_l;
    float32x4_t g_m;
    float32x4_t g_s;
    float32x4_t lms0;
    float32x4_t lms1;
    float32x4_t lms2;

    g_l = vsubq_f32(vaddq_f32(vy, vx), v_cbrt0);
    g_m = vsubq_f32(vsubq_f32(vy, vx), v_cbrt1);
    g_s = vsubq_f32(vb, v_cbrt2);

    lms0 = vmulq_f32(vfmaq_f32(v_ob0, vmulq_f32(g_l, g_l), g_l), v_itscale);
    lms1 = vmulq_f32(vfmaq_f32(v_ob1, vmulq_f32(g_m, g_m), g_m), v_itscale);
    lms2 = vmulq_f32(vfmaq_f32(v_ob2, vmulq_f32(g_s, g_s), g_s), v_itscale);

    *out_x = vaddq_f32(vmulq_n_f32(lms0, m[0]),
                       vfmaq_f32(vmulq_n_f32(lms2, m[2]), lms1, m[1]));
    *out_y = vaddq_f32(vmulq_n_f32(lms0, m[3]),
                       vfmaq_f32(vmulq_n_f32(lms2, m[5]), lms1, m[4]));
    *out_b = vaddq_f32(vmulq_n_f32(lms0, m[6]),
                       vfmaq_f32(vmulq_n_f32(lms2, m[8]), lms1, m[7]));
}

void jxl_color_opsin_xyb_to_linear_rgb_neon(float *x, float *y, float *b, size_t num_pixels,
                                            const jxl_opsin_inverse_parsed *opsin,
                                            float intensity_target) {
    float itscale;
    float cbrt_ob[3];
    float ob[3];
    float m[9];
    size_t i;

    if (x == NULL || y == NULL || b == NULL || opsin == NULL || num_pixels == 0) {
        return;
    }
    if (intensity_target <= 0.0f) {
        intensity_target = 255.0f;
    }
    itscale = 255.0f / intensity_target;
    ob[0] = opsin->opsin_bias[0];
    ob[1] = opsin->opsin_bias[1];
    ob[2] = opsin->opsin_bias[2];
    cbrt_ob[0] = cbrtf(ob[0]);
    cbrt_ob[1] = cbrtf(ob[1]);
    cbrt_ob[2] = cbrtf(ob[2]);
    m[0] = opsin->inv_mat[0][0];
    m[1] = opsin->inv_mat[0][1];
    m[2] = opsin->inv_mat[0][2];
    m[3] = opsin->inv_mat[1][0];
    m[4] = opsin->inv_mat[1][1];
    m[5] = opsin->inv_mat[1][2];
    m[6] = opsin->inv_mat[2][0];
    m[7] = opsin->inv_mat[2][1];
    m[8] = opsin->inv_mat[2][2];

    for (i = 0; i + 4 <= num_pixels; i += 4) {
        float32x4_t vx = vld1q_f32(x + i);
        float32x4_t vy = vld1q_f32(y + i);
        float32x4_t vb = vld1q_f32(b + i);
        float32x4_t out_x;
        float32x4_t out_y;
        float32x4_t out_b;
        opsin_xyb_to_linear_rgb_vec(vx, vy, vb, ob, cbrt_ob, itscale, m, &out_x, &out_y, &out_b);
        vst1q_f32(x + i, out_x);
        vst1q_f32(y + i, out_y);
        vst1q_f32(b + i, out_b);
    }
    jxl_color_opsin_xyb_to_linear_rgb_base(x + i, y + i, b + i, num_pixels - i, opsin,
                                           intensity_target);
}

#endif /* JXL_HAVE_SIMD_NEON */
