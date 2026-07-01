// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/color/color_transform_internal.h"
#include "render/color/fastmath_neon_internal.h"

#include "jxl_oxide/jxl_types.h"
#include <string.h>

#if defined(JXL_HAVE_SIMD_NEON)

#include <arm_neon.h>

static const uint8_t k_srgb_powtable_upper[16] = {
    0x00, 0x0a, 0x19, 0x26, 0x32, 0x41, 0x4d, 0x5c,
    0x68, 0x75, 0x83, 0x8f, 0xa0, 0xaa, 0xb9, 0xc6,
};
static const uint8_t k_srgb_powtable_lower[16] = {
    0x00, 0xb7, 0x04, 0x0d, 0xcb, 0xe7, 0x41, 0x68,
    0x51, 0xd1, 0xeb, 0xf2, 0x00, 0xb7, 0x04, 0x0d,
};

static const float k_srgb_eotf_p[5] = {
    2.200248328e-4f, 1.043637593e-2f, 1.624820318e-1f, 7.961564959e-1f, 8.210152774e-1f,
};
static const float k_srgb_eotf_q[5] = {
    2.631846970e-1f, 1.076976492f, 4.987528350e-1f, -5.512498495e-2f, 6.521209011e-3f,
};

void jxl_color_linear_to_srgb_neon(float *samples, size_t n) {
    size_t i;
    if (samples == NULL || n == 0) {
        return;
    }
    const uint8x16_t powtable_upper = vld1q_u8(k_srgb_powtable_upper);
    const uint8x16_t powtable_lower = vld1q_u8(k_srgb_powtable_lower);

    i = 0;
    for (; i + 4 <= n; i += 4) {
        uint32x4_t v = vld1q_u32((const uint32_t *)(samples + i));
        const uint32x4_t sign = vandq_u32(v, vdupq_n_u32(0x80000000u));
        v = vandq_u32(v, vdupq_n_u32(0x7fffffffu));

        const uint32x4_t v_adj_u = vandq_u32(vorrq_u32(v, vdupq_n_u32(0x3e800000u)),
                                            vdupq_n_u32(0x3effffffu));
        const float32x4_t v_adj = vreinterpretq_f32_u32(v_adj_u);

        float32x4_t powv = vfmaq_n_f32(vdupq_n_f32(-0.10889456f), v_adj, 0.059914046f);
        powv = vfmaq_f32(vdupq_n_f32(0.107963754f), v_adj, powv);
        powv = vfmaq_f32(vdupq_n_f32(0.018092343f), v_adj, powv);

        const uint32x4_t exp_idx = vsubq_u32(vshrq_n_u32(v, 23), vdupq_n_u32(118u));
        const uint8x16_t pow_upper = vqtbl1q_u8(powtable_upper, vreinterpretq_u8_u32(exp_idx));
        const uint8x16_t pow_lower = vqtbl1q_u8(powtable_lower, vreinterpretq_u8_u32(exp_idx));
        const uint32x4_t mul_u = vorrq_u32(
            vorrq_u32(vshlq_n_u32(vreinterpretq_u32_u8(pow_upper), 18),
                      vshlq_n_u32(vreinterpretq_u32_u8(pow_lower), 10)),
            vdupq_n_u32(0x40000000u));
        const float32x4_t mul = vreinterpretq_f32_u32(mul_u);

        const float32x4_t vf = vreinterpretq_f32_u32(v);
        const float32x4_t small = vmulq_n_f32(vf, 12.92f);
        const float32x4_t acc = vfmaq_f32(vdupq_n_f32(-0.055f), mul, powv);
        const uint32x4_t mask = vcleq_f32(vf, vdupq_n_f32(0.0031308f));
        const uint32x4_t ret = vorrq_u32(vreinterpretq_u32_f32(vbslq_f32(mask, small, acc)), sign);
        vst1q_u32((uint32_t *)(samples + i), ret);
    }
    jxl_color_linear_to_srgb_base(samples + i, n - i);
}

void jxl_color_linear_to_bt709_neon(float *samples, size_t n) {
    size_t i;
    if (samples == NULL || n == 0) {
        return;
    }
    i = 0;
    for (; i + 4 <= n; i += 4) {
        const float32x4_t v = vld1q_f32(samples + i);
        const uint32x4_t mask = vcleq_f32(v, vdupq_n_f32(0.018f));
        const float32x4_t lin = vmulq_n_f32(v, 4.5f);
        const float32x4_t powv = jxl_fastmath_powf_vec_neon(v, 0.45f);
        const float32x4_t hi = vfmaq_n_f32(vdupq_n_f32(-0.099f), powv, 1.099f);
        vst1q_f32(samples + i, vbslq_f32(mask, lin, hi));
    }
    jxl_color_linear_to_bt709_base(samples + i, n - i);
}

void jxl_color_srgb_to_linear_neon(float *samples, size_t n) {
    size_t i;
    if (samples == NULL || n == 0) {
        return;
    }
    i = 0;
    for (; i + 4 <= n; i += 4) {
        const float32x4_t v = vld1q_f32(samples + i);
        const uint32x4_t sign = vandq_u32(vreinterpretq_u32_f32(v), vdupq_n_u32(0x80000000u));
        const float32x4_t a = vreinterpretq_f32_u32(
            vandq_u32(vreinterpretq_u32_f32(v), vdupq_n_u32(0x7fffffffu)));
        const float32x4_t small = vdivq_f32(a, vdupq_n_f32(12.92f));
        const float32x4_t large = jxl_fastmath_rational_eval5_neon(a, k_srgb_eotf_p, k_srgb_eotf_q);
        const uint32x4_t mask = vcleq_f32(a, vdupq_n_f32(0.04045f));
        const float32x4_t out = vbslq_f32(mask, small, large);
        vst1q_f32(samples + i, vreinterpretq_f32_u32(vorrq_u32(vreinterpretq_u32_f32(out), sign)));
    }
    jxl_color_srgb_to_linear_base(samples + i, n - i);
}

void jxl_color_bt709_to_linear_neon(float *samples, size_t n) {
    size_t i;
    if (samples == NULL || n == 0) {
        return;
    }
    i = 0;
    for (; i + 4 <= n; i += 4) {
        const float32x4_t v = vld1q_f32(samples + i);
        const uint32x4_t mask = vcleq_f32(v, vdupq_n_f32(0.081f));
        const float32x4_t lin = vmulq_n_f32(v, 1.0f / 4.5f);
        const float32x4_t hi_in = vfmaq_n_f32(vdupq_n_f32(0.099f / 1.099f), v, 1.0f / 1.099f);
        const float32x4_t hi = jxl_fastmath_powf_vec_neon(hi_in, 1.0f / 0.45f);
        vst1q_f32(samples + i, vbslq_f32(mask, lin, hi));
    }
    jxl_color_bt709_to_linear_base(samples + i, n - i);
}

void jxl_color_apply_gamma_neon(float *samples, size_t n, float gamma) {
    size_t i;
    if (samples == NULL || n == 0) {
        return;
    }
    i = 0;
    for (; i + 4 <= n; i += 4) {
        const float32x4_t v = vld1q_f32(samples + i);
        const uint32x4_t mask = vcleq_f32(v, vdupq_n_f32(1e-7f));
        const float32x4_t powv = jxl_fastmath_powf_vec_neon(v, gamma);
        vst1q_f32(samples + i, vbslq_f32(mask, vdupq_n_f32(0.0f), powv));
    }
    jxl_color_apply_gamma_base(samples + i, n - i, gamma);
}

#endif /* defined(JXL_HAVE_SIMD_NEON) */
