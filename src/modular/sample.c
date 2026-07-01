// SPDX-License-Identifier: MIT OR Apache-2.0
#include "sample.h"

int32_t jxl_modular_i32_grad_clamped(int32_t n, int32_t w, int32_t nw) {
    int64_t g = (int64_t)n + (int64_t)w - (int64_t)nw;
    int32_t lo = w < n ? w : n;
    int32_t hi = w > n ? w : n;
    if (g < lo) {
        return lo;
    }
    if (g > hi) {
        return hi;
    }
    return (int32_t)g;
}

int16_t jxl_modular_i16_grad_clamped(int16_t n, int16_t w, int16_t nw) {
    int32_t g = (int32_t)n + (int32_t)w - (int32_t)nw;
    int16_t lo = w < n ? w : n;
    int16_t hi = w > n ? w : n;
    if (g < lo) {
        return lo;
    }
    if (g > hi) {
        return hi;
    }
    return (int16_t)g;
}
