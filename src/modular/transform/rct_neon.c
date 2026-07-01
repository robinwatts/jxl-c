// SPDX-License-Identifier: MIT OR Apache-2.0
#include "modular/transform/rct_internal.h"

#if defined(JXL_HAVE_SIMD_NEON)

#include <arm_neon.h>

static int16x8_t inverse_rct_i16_x8_neon(uint32_t ty, int16x8_t a, int16x8_t b, int16x8_t c,
                                         int16x8_t *out_e, int16x8_t *out_f) {
    if (ty == 6) {
        int16x8_t tmp = vsubq_s16(a, vshrq_n_s16(c, 1));
        int16x8_t e = vaddq_s16(c, tmp);
        int16x8_t f = vsubq_s16(tmp, vshrq_n_s16(b, 1));
        int16x8_t d = vaddq_s16(f, b);
        *out_e = e;
        *out_f = f;
        return d;
    }

    int16x8_t d = a;
    int16x8_t f = ((ty & 1) != 0) ? vaddq_s16(c, a) : c;
    int16x8_t e;
    if ((ty >> 1) == 1) {
        e = vaddq_s16(b, a);
    } else if ((ty >> 1) == 2) {
        e = vaddq_s16(b, vshrq_n_s16(vaddq_s16(a, f), 1));
    } else {
        e = b;
    }
    *out_e = e;
    *out_f = f;
    return d;
}

static int32x4_t inverse_rct_i32_x4_neon(uint32_t ty, int32x4_t a, int32x4_t b, int32x4_t c,
                                         int32x4_t *out_e, int32x4_t *out_f) {
    if (ty == 6) {
        int32x4_t tmp = vsubq_s32(a, vshrq_n_s32(c, 1));
        int32x4_t e = vaddq_s32(c, tmp);
        int32x4_t f = vsubq_s32(tmp, vshrq_n_s32(b, 1));
        int32x4_t d = vaddq_s32(f, b);
        *out_e = e;
        *out_f = f;
        return d;
    }

    int32x4_t d = a;
    int32x4_t f = ((ty & 1) != 0) ? vaddq_s32(c, a) : c;
    int32x4_t e;
    if ((ty >> 1) == 1) {
        e = vaddq_s32(b, a);
    } else if ((ty >> 1) == 2) {
        e = vaddq_s32(b, vshrq_n_s32(vaddq_s32(a, f), 1));
    } else {
        e = b;
    }
    *out_e = e;
    *out_f = f;
    return d;
}

void jxl_rct_inverse_row_i16_neon(uint32_t ty, int16_t *ra, int16_t *rb, int16_t *rc, size_t width) {
    size_t x = 0;
    for (; x + 8 <= width; x += 8) {
        int16x8_t a = vld1q_s16(ra + x);
        int16x8_t b = vld1q_s16(rb + x);
        int16x8_t c = vld1q_s16(rc + x);
        int16x8_t e;
        int16x8_t f;
        int16x8_t d = inverse_rct_i16_x8_neon(ty, a, b, c, &e, &f);
        vst1q_s16(ra + x, d);
        vst1q_s16(rb + x, e);
        vst1q_s16(rc + x, f);
    }
    for (; x < width; ++x) {
        jxl_rct_inverse_row_i16_pixel(ty, &ra[x], &rb[x], &rc[x]);
    }
}

void jxl_rct_inverse_row_i32_neon(uint32_t ty, int32_t *ra, int32_t *rb, int32_t *rc, size_t width) {
    size_t x = 0;
    for (; x + 4 <= width; x += 4) {
        int32x4_t a = vld1q_s32(ra + x);
        int32x4_t b = vld1q_s32(rb + x);
        int32x4_t c = vld1q_s32(rc + x);
        int32x4_t e;
        int32x4_t f;
        int32x4_t d = inverse_rct_i32_x4_neon(ty, a, b, c, &e, &f);
        vst1q_s32(ra + x, d);
        vst1q_s32(rb + x, e);
        vst1q_s32(rc + x, f);
    }
    for (; x < width; ++x) {
        jxl_rct_inverse_row_i32_pixel(ty, &ra[x], &rb[x], &rc[x]);
    }
}

#endif /* JXL_HAVE_SIMD_NEON */
