// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/filter/gabor_internal.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fill_row(float *row, size_t width, size_t seed) {
    size_t x;
    for (x = 0; x < width; ++x) {
        row[x] = sinf((float)(x * 13 + seed * 7)) * 50.0f + (float)seed;
    }
}

static void test_row(size_t width, float w0, float w1, size_t seed) {
    jxl_gabor_row row;
    size_t x;
    float *row_t = (float *)malloc(width * sizeof(*row_t));
    float *row_c = (float *)malloc(width * sizeof(*row_c));
    float *row_b = (float *)malloc(width * sizeof(*row_b));
    float *base = (float *)malloc(width * sizeof(*base));
    float *simd = (float *)malloc(width * sizeof(*simd));

    assert(row_t != NULL && row_c != NULL && row_b != NULL && base != NULL && simd != NULL);
    fill_row(row_t, width, seed);
    fill_row(row_c, width, seed + 1);
    fill_row(row_b, width, seed + 2);

    row.row_t = row_t;
    row.row_c = row_c;
    row.row_b = row_b;
    row.width = width;
    row.w0 = w0;
    row.w1 = w1;

    row.out = base;
    jxl_gabor_row_generic(&row);

    row.out = simd;
#if defined(JXL_HAVE_SIMD_AVX2)
    jxl_gabor_row_avx2(&row);
#elif defined(JXL_HAVE_SIMD_SSE41)
    jxl_gabor_row_sse41(&row);
#elif defined(JXL_HAVE_SIMD_NEON)
    jxl_gabor_row_neon(&row);
#else
    memcpy(simd, base, width * sizeof(*base));
#endif

    for (x = 0; x < width; ++x) {
        float diff = fabsf(base[x] - simd[x]);
        if (diff > 1e-3f) {
            fprintf(stderr, "gabor mismatch width=%zu x=%zu base=%g simd=%g w0=%g w1=%g\n", width, x,
                    (double)base[x], (double)simd[x], (double)w0, (double)w1);
            assert(0);
        }
    }

    free(row_t);
    free(row_c);
    free(row_b);
    free(base);
    free(simd);
}

int main(void) {
    size_t width;
    float w0_vals[] = {-0.4f, -0.1f, 0.0f, 0.15f, 0.35f};
    float w1_vals[] = {-0.35f, -0.05f, 0.0f, 0.2f, 0.4f};
    size_t i;
    size_t j;

    for (width = 1; width <= 64; ++width) {
        for (i = 0; i < sizeof(w0_vals) / sizeof(w0_vals[0]); ++i) {
            for (j = 0; j < sizeof(w1_vals) / sizeof(w1_vals[0]); ++j) {
                test_row(width, w0_vals[i], w1_vals[j], width + i + j);
            }
        }
    }

    printf("test_gabor_simd: ok\n");
    return 0;
}
