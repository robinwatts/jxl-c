// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_MODULAR_SAMPLE_H_
#define JXL_MODULAR_SAMPLE_H_

#include "jxl_oxide/jxl_types.h"

#include "bitstream/unpack.h"

jxl_inline int32_t jxl_modular_unpack_signed_u32(uint32_t value) {
    return jxl_unpack_signed(value);
}

jxl_inline int16_t jxl_modular_unpack_signed_u32_i16(uint32_t value) {
    uint16_t bit = (uint16_t)(value & 1u);
    uint16_t base = (uint16_t)(value >> 1);
    uint16_t flip = (uint16_t)(0u - bit);
    return (int16_t)(base ^ flip);
}

jxl_inline int32_t jxl_modular_i32_add(int32_t a, int32_t b) {
    return a + b;
}

jxl_inline int32_t jxl_modular_i32_wrapping_muladd(int32_t v, int32_t mul, int32_t add) {
    return v * mul + add;
}

/* clamped gradient: (n + w - nw) clamped to [min(w,n), max(w,n)] */
int32_t jxl_modular_i32_grad_clamped(int32_t n, int32_t w, int32_t nw);

jxl_inline int16_t jxl_modular_i16_add(int16_t a, int16_t b) {
    return (int16_t)((uint16_t)a + (uint16_t)b);
}

jxl_inline int16_t jxl_modular_i16_wrapping_muladd(int16_t v, int32_t mul, int32_t add) {
    return (int16_t)((uint16_t)((int16_t)((uint16_t)v * (uint16_t)(int16_t)mul)) +
                     (uint16_t)(int16_t)add);
}

int16_t jxl_modular_i16_grad_clamped(int16_t n, int16_t w, int16_t nw);

#endif /* JXL_MODULAR_SAMPLE_H_ */
