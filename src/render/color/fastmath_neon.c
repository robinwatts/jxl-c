// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/color/fastmath.h"
#include "render/color/fastmath_neon_internal.h"

#include <stddef.h>

#if defined(JXL_HAVE_SIMD_NEON)

static const float k_pow2f_numer[3] = {1.01749063e1f, 4.88687798e1f, 9.85506591e1f};
static const float k_pow2f_denom[4] = {2.10242958e-1f, -2.22328856e-2f, -1.94414990e1f, 9.85506633e1f};
static const float k_log2f_p[3] = {
    -1.8503833400518310e-6f, 1.4287160470083755f, 7.4245873327820566e-1f,
};
static const float k_log2f_q[3] = {
    9.9032814277590719e-1f, 1.0096718572241148f, 1.7409343003366853e-1f,
};

static float32x4_t fast_pow2f_neon(float32x4_t x) {
    const float32x4_t x_floor = vrndmq_f32(x);
    const int32x4_t exp_i = vaddq_s32(vcvtq_s32_f32(x_floor), vdupq_n_s32(127));
    const float32x4_t exp = vreinterpretq_f32_s32(vshlq_n_s32(exp_i, 23));
    const float32x4_t frac = vsubq_f32(x, x_floor);

    float32x4_t num = vaddq_f32(vdupq_n_f32(k_pow2f_numer[0]), frac);
    num = vfmaq_f32(vdupq_n_f32(k_pow2f_numer[1]), frac, num);
    num = vfmaq_f32(vdupq_n_f32(k_pow2f_numer[2]), frac, num);
    num = vmulq_f32(exp, num);

    float32x4_t den = vfmaq_f32(vdupq_n_f32(k_pow2f_denom[1]), frac, vdupq_n_f32(k_pow2f_denom[0]));
    den = vfmaq_f32(vdupq_n_f32(k_pow2f_denom[2]), frac, den);
    den = vfmaq_f32(vdupq_n_f32(k_pow2f_denom[3]), frac, den);

    return vdivq_f32(num, den);
}

static float32x4_t fast_log2f_neon(float32x4_t x) {
    const int32x4_t x_bits = vreinterpretq_s32_f32(x);
    const int32x4_t exp_bits = vsubq_s32(x_bits, vdupq_n_s32(0x3f2aaaab));
    const int32x4_t exp_shifted = vshrq_n_s32(exp_bits, 23);
    const float32x4_t mantissa =
        vreinterpretq_f32_s32(vsubq_s32(x_bits, vshlq_n_s32(exp_shifted, 23)));
    const float32x4_t exp_val = vcvtq_f32_s32(exp_shifted);

    const float32x4_t xm = vsubq_f32(mantissa, vdupq_n_f32(1.0f));
    return vaddq_f32(jxl_fastmath_rational_eval3_neon(xm, k_log2f_p, k_log2f_q), exp_val);
}

float32x4_t jxl_fastmath_rational_eval3_neon(float32x4_t x, const float p[3], const float q[3]) {
    float32x4_t yp = vdupq_n_f32(p[2]);
    yp = vfmaq_f32(vdupq_n_f32(p[1]), yp, x);
    yp = vfmaq_f32(vdupq_n_f32(p[0]), yp, x);
    float32x4_t yq = vdupq_n_f32(q[2]);
    yq = vfmaq_f32(vdupq_n_f32(q[1]), yq, x);
    yq = vfmaq_f32(vdupq_n_f32(q[0]), yq, x);
    return vdivq_f32(yp, yq);
}

float32x4_t jxl_fastmath_rational_eval5_neon(float32x4_t x, const float p[5], const float q[5]) {
    float32x4_t yp = vdupq_n_f32(p[4]);
    yp = vfmaq_f32(vdupq_n_f32(p[3]), yp, x);
    yp = vfmaq_f32(vdupq_n_f32(p[2]), yp, x);
    yp = vfmaq_f32(vdupq_n_f32(p[1]), yp, x);
    yp = vfmaq_f32(vdupq_n_f32(p[0]), yp, x);
    float32x4_t yq = vdupq_n_f32(q[4]);
    yq = vfmaq_f32(vdupq_n_f32(q[3]), yq, x);
    yq = vfmaq_f32(vdupq_n_f32(q[2]), yq, x);
    yq = vfmaq_f32(vdupq_n_f32(q[1]), yq, x);
    yq = vfmaq_f32(vdupq_n_f32(q[0]), yq, x);
    return vdivq_f32(yp, yq);
}

float32x4_t jxl_fastmath_powf_vec_neon(float32x4_t base, float exponent) {
    return fast_pow2f_neon(vmulq_n_f32(fast_log2f_neon(base), exponent));
}

void jxl_fastmath_powf_f32_neon(float *samples, size_t n, float exponent) {
    size_t i;
    if (samples == NULL || n == 0) {
        return;
    }
    i = 0;
    for (; i + 4 <= n; i += 4) {
        const float32x4_t base = vld1q_f32(samples + i);
        vst1q_f32(samples + i, jxl_fastmath_powf_vec_neon(base, exponent));
    }
    for (; i < n; ++i) {
        samples[i] = jxl_fastmath_powf(samples[i], exponent);
    }
}

#endif /* defined(JXL_HAVE_SIMD_NEON) */
