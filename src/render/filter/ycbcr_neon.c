// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/filter/ycbcr_internal.h"

#if defined(JXL_HAVE_SIMD_NEON)

#include <arm_neon.h>

static const float k_ycbcr_y_offset = 128.0f / 255.0f;
static const float k_ycbcr_r_cr = 1.402f;
static const float k_ycbcr_g_cb = -0.114f * 1.772f / 0.587f;
static const float k_ycbcr_g_cr = -0.299f * 1.402f / 0.587f;
static const float k_ycbcr_b_cb = 1.772f;

void jxl_ycbcr_to_rgb_neon(float *cb, float *y, float *cr, size_t count) {
    size_t i;
    if (cb == NULL || y == NULL || cr == NULL || count == 0) {
        return;
    }
    i = 0;
    for (; i + 4 <= count; i += 4) {
        const float32x4_t vcb = vld1q_f32(cb + i);
        const float32x4_t vy = vld1q_f32(y + i);
        const float32x4_t vcr = vld1q_f32(cr + i);
        const float32x4_t y_v = vaddq_f32(vy, vdupq_n_f32(k_ycbcr_y_offset));
        const float32x4_t out_r = vfmaq_n_f32(y_v, vcr, k_ycbcr_r_cr);
        const float32x4_t out_g =
            vfmaq_n_f32(vfmaq_n_f32(y_v, vcb, k_ycbcr_g_cb), vcr, k_ycbcr_g_cr);
        const float32x4_t out_b = vfmaq_n_f32(y_v, vcb, k_ycbcr_b_cb);
        vst1q_f32(cb + i, out_r);
        vst1q_f32(y + i, out_g);
        vst1q_f32(cr + i, out_b);
    }
    jxl_ycbcr_to_rgb_base(cb + i, y + i, cr + i, count - i);
}

#endif /* defined(JXL_HAVE_SIMD_NEON) */
