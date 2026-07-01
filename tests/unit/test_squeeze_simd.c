// SPDX-License-Identifier: MIT OR Apache-2.0
#include "modular/transform/squeeze.h"
#include "modular/transform/squeeze_internal.h"

#include "allocator.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static jxl_allocator_state *test_alloc(void) {
    static jxl_allocator_state alloc;
    static int init;
    if (!init) {
        jxl_allocator_init(&alloc, NULL);
        init = 1;
    }
    return &alloc;
}

static void fill(int16_t *buf, size_t width, size_t height, size_t stride) {
    size_t y;
    for (y = 0; y < height; ++y) {
        size_t x;
        for (x = 0; x < width; ++x) {
            buf[y * stride + x] = (int16_t)(x * 13 + y * 7 - 100);
        }
    }
}

static void test_h(size_t width, size_t height) {
    size_t y;
    size_t stride = width;
    size_t n = stride * height;
    int16_t *base = (int16_t *)malloc(n * sizeof(*base));
    int16_t *simd = (int16_t *)malloc(n * sizeof(*simd));
    assert(base != NULL && simd != NULL);
    fill(base, width, height, stride);
    memcpy(simd, base, n * sizeof(*base));
    jxl_squeeze_inverse_h_i16_base(test_alloc(), base, width, height, stride);
    jxl_squeeze_inverse_h_i16(NULL, test_alloc(), simd, width, height, stride);
    for (y = 0; y < height; ++y) {
        size_t x;
        for (x = 0; x < width; ++x) {
            if (base[y * stride + x] != simd[y * stride + x]) {
                fprintf(stderr, "h mismatch %zux%zu at %zu,%zu base=%d simd=%d\n", width, height, x, y,
                        (int)base[y * stride + x], (int)simd[y * stride + x]);
                assert(0);
            }
        }
    }
    free(base);
    free(simd);
}

static void test_v(size_t width, size_t height) {
    size_t y;
    size_t stride = width;
    size_t n = stride * height;
    int16_t *base = (int16_t *)malloc(n * sizeof(*base));
    int16_t *simd = (int16_t *)malloc(n * sizeof(*simd));
    assert(base != NULL && simd != NULL);
    fill(base, width, height, stride);
    memcpy(simd, base, n * sizeof(*base));
    jxl_squeeze_inverse_v_i16_base(test_alloc(), base, width, height, stride);
    jxl_squeeze_inverse_v_i16(NULL, test_alloc(), simd, width, height, stride);
    for (y = 0; y < height; ++y) {
        size_t x;
        for (x = 0; x < width; ++x) {
            if (base[y * stride + x] != simd[y * stride + x]) {
                fprintf(stderr, "v mismatch %zux%zu at %zu,%zu base=%d simd=%d\n", width, height, x, y,
                        (int)base[y * stride + x], (int)simd[y * stride + x]);
                assert(0);
            }
        }
    }
    free(base);
    free(simd);
}

int main(void) {
    size_t i;
    static const size_t sizes[][2] = {
        {4, 1}, {8, 8}, {17, 9}, {24, 16}, {33, 17}, {48, 24}, {64, 32}, {80, 40},
    };
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        test_h(sizes[i][0], sizes[i][1]);
        test_v(sizes[i][0], sizes[i][1]);
    }
    printf("test_squeeze_simd: ok\n");
    return 0;
}
