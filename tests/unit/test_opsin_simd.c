// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/color/opsin_internal.h"

#include <math.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static int float_eq(float a, float b) {
    if (isnan(a) && isnan(b)) {
        return 1;
    }
    float d = fabsf(a - b);
    return d <= 1e-5f * fmaxf(1.0f, fabsf(a));
}

static void default_opsin(jxl_opsin_inverse_parsed *opsin) {
    memset(opsin, 0, sizeof(*opsin));
    opsin->opsin_bias[0] = -0.0037930732552754493f;
    opsin->opsin_bias[1] = -0.0037930732552754493f;
    opsin->opsin_bias[2] = -0.0037930732552754493f;
    opsin->inv_mat[0][0] = 11.031566901960783f;
    opsin->inv_mat[0][1] = -9.866943921568629f;
    opsin->inv_mat[0][2] = -0.16462299647058826f;
    opsin->inv_mat[1][0] = -3.254147380392157f;
    opsin->inv_mat[1][1] = 4.418770392156863f;
    opsin->inv_mat[1][2] = -0.16462299647058826f;
    opsin->inv_mat[2][0] = -3.6588512862745097f;
    opsin->inv_mat[2][1] = 2.7129230470588235f;
    opsin->inv_mat[2][2] = 1.9459282392156863f;
}

static void fill_planes(float *x, float *y, float *b, size_t n, size_t seed) {
    size_t i;
    for (i = 0; i < n; ++i) {
        x[i] = (float)(i * 17 + seed * 3) * 0.001f - 0.2f;
        y[i] = (float)(i * 11 - seed * 5) * 0.001f + 0.1f;
        b[i] = (float)(i * 7 + seed * 2) * 0.001f - 0.05f;
    }
}

static void test_opsin_row(size_t width, size_t seed, float intensity_target) {
    jxl_opsin_inverse_parsed opsin;
    size_t i;
    float *base_x = (float *)malloc(width * sizeof(*base_x));
    float *base_y = (float *)malloc(width * sizeof(*base_y));
    float *base_b = (float *)malloc(width * sizeof(*base_b));
    float *simd_x = (float *)malloc(width * sizeof(*simd_x));
    float *simd_y = (float *)malloc(width * sizeof(*simd_y));
    float *simd_b = (float *)malloc(width * sizeof(*simd_b));

    if (base_x == NULL || base_y == NULL || base_b == NULL || simd_x == NULL || simd_y == NULL ||
        simd_b == NULL) {
        assert(0);
    }

    default_opsin(&opsin);
    fill_planes(base_x, base_y, base_b, width, seed);
    memcpy(simd_x, base_x, width * sizeof(*base_x));
    memcpy(simd_y, base_y, width * sizeof(*base_y));
    memcpy(simd_b, base_b, width * sizeof(*base_b));

    jxl_color_opsin_xyb_to_linear_rgb_base(base_x, base_y, base_b, width, &opsin, intensity_target);
    jxl_color_opsin_xyb_to_linear_rgb(NULL, simd_x, simd_y, simd_b, width, &opsin, intensity_target);

    for (i = 0; i < width; ++i) {
        if (!float_eq(base_x[i], simd_x[i]) || !float_eq(base_y[i], simd_y[i]) ||
            !float_eq(base_b[i], simd_b[i])) {
            fprintf(stderr,
                    "opsin mismatch width=%zu seed=%zu x=%zu base=(%g,%g,%g) simd=(%g,%g,%g)\n",
                    width, seed, i, base_x[i], base_y[i], base_b[i], simd_x[i], simd_y[i],
                    simd_b[i]);
            assert(0);
        }
    }

    free(base_x);
    free(base_y);
    free(base_b);
    free(simd_x);
    free(simd_y);
    free(simd_b);
}

int main(void) {
    size_t width;
    size_t seed;

    for (width = 1; width <= 64; ++width) {
        for (seed = 0; seed < 8; ++seed) {
            test_opsin_row(width, seed, 255.0f);
            test_opsin_row(width, seed, 10000.0f);
        }
    }

    printf("test_opsin_simd: ok\n");
    return 0;
}
