// SPDX-License-Identifier: MIT OR Apache-2.0
#include "modular/transform/rct_internal.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fill_i16(int16_t *ra, int16_t *rb, int16_t *rc, size_t width, size_t seed) {
    size_t x;
    for (x = 0; x < width; ++x) {
        ra[x] = (int16_t)(x * 17 + (int16_t)seed * 3 - 200);
        rb[x] = (int16_t)(x * 11 - (int16_t)seed * 5 + 50);
        rc[x] = (int16_t)(x * 7 + (int16_t)seed * 2 - 80);
    }
}

static void fill_i32(int32_t *ra, int32_t *rb, int32_t *rc, size_t width, size_t seed) {
    size_t x;
    for (x = 0; x < width; ++x) {
        ra[x] = (int32_t)(x * 17000 + (int32_t)seed * 3000 - 200000);
        rb[x] = (int32_t)(x * 11000 - (int32_t)seed * 5000 + 50000);
        rc[x] = (int32_t)(x * 7000 + (int32_t)seed * 2000 - 80000);
    }
}

static void test_i16_row(uint32_t ty, size_t width, size_t seed) {
    size_t x;
    int16_t *base_a = (int16_t *)malloc(width * sizeof(*base_a));
    int16_t *base_b = (int16_t *)malloc(width * sizeof(*base_b));
    int16_t *base_c = (int16_t *)malloc(width * sizeof(*base_c));
    int16_t *simd_a = (int16_t *)malloc(width * sizeof(*simd_a));
    int16_t *simd_b = (int16_t *)malloc(width * sizeof(*simd_b));
    int16_t *simd_c = (int16_t *)malloc(width * sizeof(*simd_c));

    assert(base_a != NULL && base_b != NULL && base_c != NULL && simd_a != NULL && simd_b != NULL &&
           simd_c != NULL);

    fill_i16(base_a, base_b, base_c, width, seed);
    memcpy(simd_a, base_a, width * sizeof(*base_a));
    memcpy(simd_b, base_b, width * sizeof(*base_b));
    memcpy(simd_c, base_c, width * sizeof(*base_c));

    jxl_rct_inverse_row_i16_base(ty, base_a, base_b, base_c, width);
    jxl_rct_inverse_row_i16(ty, simd_a, simd_b, simd_c, width, NULL);

    for (x = 0; x < width; ++x) {
        if (base_a[x] != simd_a[x] || base_b[x] != simd_b[x] || base_c[x] != simd_c[x]) {
            fprintf(stderr,
                    "rct i16 mismatch ty=%u width=%zu x=%zu base=(%d,%d,%d) simd=(%d,%d,%d)\n", ty,
                    width, x, (int)base_a[x], (int)base_b[x], (int)base_c[x], (int)simd_a[x],
                    (int)simd_b[x], (int)simd_c[x]);
            assert(0);
        }
    }

    free(base_a);
    free(base_b);
    free(base_c);
    free(simd_a);
    free(simd_b);
    free(simd_c);
}

static void test_i32_row(uint32_t ty, size_t width, size_t seed) {
    size_t x;
    int32_t *base_a = (int32_t *)malloc(width * sizeof(*base_a));
    int32_t *base_b = (int32_t *)malloc(width * sizeof(*base_b));
    int32_t *base_c = (int32_t *)malloc(width * sizeof(*base_c));
    int32_t *simd_a = (int32_t *)malloc(width * sizeof(*simd_a));
    int32_t *simd_b = (int32_t *)malloc(width * sizeof(*simd_b));
    int32_t *simd_c = (int32_t *)malloc(width * sizeof(*simd_c));

    assert(base_a != NULL && base_b != NULL && base_c != NULL && simd_a != NULL && simd_b != NULL &&
           simd_c != NULL);

    fill_i32(base_a, base_b, base_c, width, seed);
    memcpy(simd_a, base_a, width * sizeof(*base_a));
    memcpy(simd_b, base_b, width * sizeof(*base_b));
    memcpy(simd_c, base_c, width * sizeof(*base_c));

    jxl_rct_inverse_row_i32_base(ty, base_a, base_b, base_c, width);
    jxl_rct_inverse_row_i32(ty, simd_a, simd_b, simd_c, width, NULL);

    for (x = 0; x < width; ++x) {
        if (base_a[x] != simd_a[x] || base_b[x] != simd_b[x] || base_c[x] != simd_c[x]) {
            fprintf(stderr,
                    "rct i32 mismatch ty=%u width=%zu x=%zu base=(%d,%d,%d) simd=(%d,%d,%d)\n", ty,
                    width, x, base_a[x], base_b[x], base_c[x], simd_a[x], simd_b[x], simd_c[x]);
            assert(0);
        }
    }

    free(base_a);
    free(base_b);
    free(base_c);
    free(simd_a);
    free(simd_b);
    free(simd_c);
}

int main(void) {
    uint32_t ty;
    size_t width;

    for (ty = 0; ty < 7; ++ty) {
        for (width = 1; width <= 64; ++width) {
            test_i16_row(ty, width, width + ty);
            test_i32_row(ty, width, width + ty);
        }
    }

    printf("test_rct_simd: ok\n");
    return 0;
}
