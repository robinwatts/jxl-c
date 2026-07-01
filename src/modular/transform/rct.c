// SPDX-License-Identifier: MIT OR Apache-2.0
#include "modular/transform/rct_internal.h"

#include "render/simd/features.h"

void jxl_rct_inverse_row_i16_pixel(uint32_t ty, int16_t *ra, int16_t *rb, int16_t *rc) {
    int16_t a = *ra;
    int16_t b = *rb;
    int16_t c = *rc;
    int16_t d;
    int16_t e;
    int16_t f;
    if (ty == 6) {
        int16_t tmp = (int16_t)((uint16_t)a - (uint16_t)(c >> 1));
        e = (int16_t)((uint16_t)c + (uint16_t)tmp);
        f = (int16_t)((uint16_t)tmp - (uint16_t)(b >> 1));
        d = (int16_t)((uint16_t)f + (uint16_t)b);
    } else {
        d = a;
        f = (ty & 1) != 0 ? (int16_t)((uint16_t)c + (uint16_t)a) : c;
        if ((ty >> 1) == 1) {
            e = (int16_t)((uint16_t)b + (uint16_t)a);
        } else if ((ty >> 1) == 2) {
            int16_t af = (int16_t)((uint16_t)a + (uint16_t)f);
            e = (int16_t)((uint16_t)b + (uint16_t)(af >> 1));
        } else {
            e = b;
        }
    }
    *ra = d;
    *rb = e;
    *rc = f;
}

void jxl_rct_inverse_row_i32_pixel(uint32_t ty, int32_t *ra, int32_t *rb, int32_t *rc) {
    int32_t a = *ra;
    int32_t b = *rb;
    int32_t c = *rc;
    int32_t d;
    int32_t e;
    int32_t f;
    if (ty == 6) {
        int32_t tmp = a - (c >> 1);
        e = c + tmp;
        f = tmp - (b >> 1);
        d = f + b;
    } else {
        d = a;
        f = (ty & 1) != 0 ? c + a : c;
        if ((ty >> 1) == 1) {
            e = b + a;
        } else if ((ty >> 1) == 2) {
            e = b + ((a + f) >> 1);
        } else {
            e = b;
        }
    }
    *ra = d;
    *rb = e;
    *rc = f;
}

void jxl_rct_inverse_row_i16_base(uint32_t ty, int16_t *ra, int16_t *rb, int16_t *rc, size_t width) {
    size_t x;
    for (x = 0; x < width; ++x) {
        jxl_rct_inverse_row_i16_pixel(ty, &ra[x], &rb[x], &rc[x]);
    }
}

void jxl_rct_inverse_row_i32_base(uint32_t ty, int32_t *ra, int32_t *rb, int32_t *rc, size_t width) {
    size_t x;
    for (x = 0; x < width; ++x) {
        jxl_rct_inverse_row_i32_pixel(ty, &ra[x], &rb[x], &rc[x]);
    }
}

void jxl_rct_inverse_row_i16(uint32_t ty, int16_t *ra, int16_t *rb, int16_t *rc, size_t width,
                             const jxl_cpu_features *feat) {
    jxl_cpu_features local_feat;
    const jxl_cpu_features *f = feat;
    if (f == NULL) {
        jxl_cpu_features_detect(&local_feat);
        f = &local_feat;
    }
#if defined(JXL_HAVE_SIMD_AVX2)
    if (f->avx2) {
        jxl_rct_inverse_row_i16_avx2(ty, ra, rb, rc, width);
        return;
    }
#endif
#if defined(JXL_HAVE_SIMD_NEON)
    if (f->neon) {
        jxl_rct_inverse_row_i16_neon(ty, ra, rb, rc, width);
        return;
    }
#endif
    jxl_rct_inverse_row_i16_base(ty, ra, rb, rc, width);
}

void jxl_rct_inverse_row_i32(uint32_t ty, int32_t *ra, int32_t *rb, int32_t *rc, size_t width,
                             const jxl_cpu_features *feat) {
    jxl_cpu_features local_feat;
    const jxl_cpu_features *f = feat;
    if (f == NULL) {
        jxl_cpu_features_detect(&local_feat);
        f = &local_feat;
    }
#if defined(JXL_HAVE_SIMD_AVX2)
    if (f->avx2) {
        jxl_rct_inverse_row_i32_avx2(ty, ra, rb, rc, width);
        return;
    }
#endif
#if defined(JXL_HAVE_SIMD_NEON)
    if (f->neon) {
        jxl_rct_inverse_row_i32_neon(ty, ra, rb, rc, width);
        return;
    }
#endif
    jxl_rct_inverse_row_i32_base(ty, ra, rb, rc, width);
}
