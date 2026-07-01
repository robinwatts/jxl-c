// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/color/pq_internal.h"
#include "render/color/fastmath_neon_internal.h"

#if defined(JXL_HAVE_SIMD_NEON)

#include <arm_neon.h>

static const float k_eotf_p[5] = {
    2.6297566e-4f, -6.235531e-3f, 7.386023e-1f, 2.6455317f, 5.500349e-1f,
};
static const float k_eotf_q[5] = {
    4.213501e2f, -4.2873682e2f, 1.7436467e2f, -3.3907887e1f, 2.6771877f,
};
static const float k_inv_eotf_p[5] = {
    1.351392e-2f, -1.095778f, 5.522776e1f, 1.492516e2f, 4.838434e1f,
};
static const float k_inv_eotf_q[5] = {
    1.012416f, 2.016708e1f, 9.26371e1f, 1.120607e2f, 2.590418e1f,
};
static const float k_inv_eotf_p_small[5] = {
    9.863406e-6f, 3.881234e-1f, 1.352821e2f, 6.889862e4f, -2.864824e5f,
};
static const float k_inv_eotf_q_small[5] = {
    3.371868e1f, 1.477719e3f, 1.608477e4f, -4.389884e4f, -2.072546e5f,
};

float32x4_t jxl_color_linear_to_pq_vec_neon(float32x4_t v, float intensity_target) {
    const float y_mult = intensity_target / 10000.0f;
    const uint32x4_t sign = vandq_u32(vreinterpretq_u32_f32(v), vdupq_n_u32(0x80000000u));
    const float32x4_t a = vabsq_f32(v);
    const float32x4_t a_scaled = vmulq_n_f32(a, y_mult);
    const float32x4_t a_1_4 = vsqrtq_f32(vsqrtq_f32(a_scaled));

    const float32x4_t v_small =
        jxl_fastmath_rational_eval5_neon(a_1_4, k_inv_eotf_p_small, k_inv_eotf_q_small);
    const float32x4_t v_large = jxl_fastmath_rational_eval5_neon(a_1_4, k_inv_eotf_p, k_inv_eotf_q);
    const uint32x4_t is_small = vcltq_f32(a, vdupq_n_f32(1e-4f));
    const float32x4_t y = vabsq_f32(vbslq_f32(is_small, v_small, v_large));
    return vreinterpretq_f32_u32(vorrq_u32(vreinterpretq_u32_f32(y), sign));
}

float32x4_t jxl_color_pq_to_linear_vec_neon(float32x4_t v, float intensity_target) {
    const float y_mult = 10000.0f / intensity_target;
    const uint32x4_t sign = vandq_u32(vreinterpretq_u32_f32(v), vdupq_n_u32(0x80000000u));
    const float32x4_t a = vabsq_f32(v);
    const float32x4_t x = vfmaq_f32(a, a, a);
    const float32x4_t y = jxl_fastmath_rational_eval5_neon(x, k_eotf_p, k_eotf_q);
    const float32x4_t out = vmulq_n_f32(y, y_mult);
    return vreinterpretq_f32_u32(vorrq_u32(vreinterpretq_u32_f32(out), sign));
}

void jxl_color_linear_to_pq_neon(float *samples, size_t n, float intensity_target) {
    size_t i;
    if (samples == NULL || n == 0) {
        return;
    }
    i = 0;
    for (; i + 4 <= n; i += 4) {
        const float32x4_t v = vld1q_f32(samples + i);
        vst1q_f32(samples + i, jxl_color_linear_to_pq_vec_neon(v, intensity_target));
    }
    jxl_color_linear_to_pq_base(samples + i, n - i, intensity_target);
}

void jxl_color_pq_to_linear_neon(float *samples, size_t n, float intensity_target) {
    size_t i;
    if (samples == NULL || n == 0) {
        return;
    }
    i = 0;
    for (; i + 4 <= n; i += 4) {
        const float32x4_t v = vld1q_f32(samples + i);
        vst1q_f32(samples + i, jxl_color_pq_to_linear_vec_neon(v, intensity_target));
    }
    jxl_color_pq_to_linear_base(samples + i, n - i, intensity_target);
}

#endif /* defined(JXL_HAVE_SIMD_NEON) */
