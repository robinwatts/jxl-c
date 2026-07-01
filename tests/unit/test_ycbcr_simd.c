// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/filter/ycbcr.h"
#include "render/filter/ycbcr_internal.h"
#include <assert.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static int float_eq(float a, float b) {
    float d = fabsf(a - b);
    return d <= 1e-6f * fmaxf(1.0f, fabsf(a));
}

static void test_ycbcr_to_rgb_matches_base(void) {
    size_t i;
    float base_cb[32];
    float base_y[32];
    float base_cr[32];
    float simd_cb[32];
    float simd_y[32];
    float simd_cr[32];

    for (i = 0; i < 32; ++i) {
        base_cb[i] = (float)i * 0.03f - 0.4f;
        base_y[i] = (float)i * 0.02f;
        base_cr[i] = (float)i * 0.025f - 0.3f;
    }
    memcpy(simd_cb, base_cb, sizeof(base_cb));
    memcpy(simd_y, base_y, sizeof(base_y));
    memcpy(simd_cr, base_cr, sizeof(base_cr));

    jxl_ycbcr_to_rgb_base(base_cb, base_y, base_cr, 32);
    jxl_ycbcr_to_rgb(NULL, simd_cb, simd_y, simd_cr, 32);

    for (i = 0; i < 32; ++i) {
        if (!float_eq(base_cb[i], simd_cb[i]) || !float_eq(base_y[i], simd_y[i]) ||
            !float_eq(base_cr[i], simd_cr[i])) {
            fprintf(stderr,
                    "ycbcr mismatch at %zu: base r=%g g=%g b=%g simd r=%g g=%g b=%g\n", i,
                    base_cb[i], base_y[i], base_cr[i], simd_cb[i], simd_y[i], simd_cr[i]);
            assert(0);
        }
    }
}

int main(void) {
    test_ycbcr_to_rgb_matches_base();
    printf("test_ycbcr_simd: ok\n");
    return 0;
}
