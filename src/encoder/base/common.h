// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_BASE_COMMON_H_
#define LIB_JXL_BASE_COMMON_H_

// Shared constants and helper functions.

#include "base/compiler_specific.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#if JXL_COMPILER_MSVC
#include <intrin.h>
#endif


// Some enums and typedefs used by more than one header file.

// C-friendly min/max/abs/swap (no C++ overloads).
// JXL_MIN/JXL_MAX evaluate arguments twice; avoid side effects in args.
#define JXL_MIN(a, b) (((a) < (b)) ? (a) : (b))
#define JXL_MAX(a, b) (((a) > (b)) ? (a) : (b))
#define JXL_ABS(x) (((x) < 0) ? -(x) : (x))

/* Swap *a and *b (same pointee type/size). C-shaped; no overloads. */
#define jxl_swap(a, b)                         \
  do {                                        \
    char _jxl_swap_tmp[sizeof(*(a))];         \
    memcpy(_jxl_swap_tmp, (a), sizeof(*(a))); \
    memcpy((a), (b), sizeof(*(a)));           \
    memcpy((b), _jxl_swap_tmp, sizeof(*(a))); \
  } while (0)

// Xorshift128+ adapted from xorshift128+-inl.h
typedef struct jxl_xor_shift128_plus {
  uint64_t s[2];
} jxl_xor_shift128_plus;

static inline jxl_xor_shift128_plus jxl_xor_shift128_plus_make(uint64_t s0, uint64_t s1) {
  jxl_xor_shift128_plus self;
  self.s[0] = s0;
  self.s[1] = s1;
  return self;
}

static inline uint64_t jxl_xor_shift128_plus_next(jxl_xor_shift128_plus* self) {
  uint64_t s1 = self->s[0];
  const uint64_t s0 = self->s[1];
  const uint64_t bits = s1 + s0;
  self->s[0] = s0;
  s1 ^= s1 << 23;
  s1 ^= s0 ^ (s1 >> 18) ^ (s0 >> 5);
  self->s[1] = s1;
  return bits;
}

static inline bool jxl_xor_shift128_plus_below_threshold(jxl_xor_shift128_plus* self,
                                                 uint64_t threshold) {
  return (jxl_xor_shift128_plus_next(self) >> 32) <= threshold;
}

static const size_t kBitsPerByte = 8;  // more clear than CHAR_BIT

static inline size_t jxl_round_up_bits_to_byte_multiple(size_t bits) {
  return (bits + 7) & ~(size_t)(7);
}

static inline bool jxl_safe_add(size_t a, size_t b, size_t* sum) {
  *sum = a + b;
  return *sum >= a;  // no need to check b - either sum >= both or < both.
}

static inline bool jxl_safe_mul(size_t a, size_t b, size_t* product) {
  *product = 0;
  if (a == 0 || b == 0) return true;
  if (b > (SIZE_MAX / a)) return false;
  *product = a * b;
  return true;
}

static inline bool jxl_sub_overflow(int32_t a, int32_t b, int32_t* c) {
  // Clang 3.8+ / GCC 5.1+
#if JXL_COMPILER_GCC || JXL_COMPILER_CLANG
  return __builtin_sub_overflow(a, b, c);
#elif JXL_COMPILER_MSVC >= 1937 && (defined(_M_AMD64) || defined(_M_IX86))
  return _sub_overflow_i32(/*carry*/ 0, a, b, c);
#else
  uint32_t ua = (uint32_t)(a);
  uint32_t ub = (uint32_t)(b);
  uint32_t uc = ua - ub;
  *c = (int32_t)(uc);
  return !!(((ua ^ ub) & (ua ^ uc)) >> 31);
#endif
}

static inline size_t jxl_div_ceil(size_t a, size_t b) {
  return (a + b - 1) / b;
}

// Works for any `align`; if a power of two, compiler emits ADD+AND.
static inline size_t jxl_round_up_to(size_t what, size_t align) {
  return jxl_div_ceil(what, align) * align;
}

// `align <= 1` means no rounding.
static inline bool jxl_safe_round_up_to(size_t what, size_t align, size_t* result) {
  if (align < 2) {
    *result = what;
    return true;
  }
  size_t reminder = what % align;
  if (reminder == 0) {
    *result = what;
    return true;
  }
  return jxl_safe_add(what, align - reminder, result);
}

static const float kDefaultIntensityTarget = 255;

typedef struct jxl_color {
  float c[3];
} jxl_color;

static inline float* jxl_color_at(jxl_color* self, size_t i) { return &self->c[i]; }
static inline const float* jxl_color_at_const(const jxl_color* self, size_t i) {
  return &self->c[i];
}

static inline jxl_color jxl_color_make(float a, float b, float d) {
  jxl_color self;
  self.c[0] = a;
  self.c[1] = b;
  self.c[2] = d;
  return self;
}

static inline int32_t jxl_clamp1_i(int32_t val, int32_t low, int32_t hi) {
  return val < low ? low : val > hi ? hi : val;
}

static inline double jxl_clamp1_d(double val, double low, double hi) {
  return val < low ? low : val > hi ? hi : val;
}

// Writes a NUL-terminated %g representation into data (size >= 32).
static inline void jxl_format_number_d(char* data, size_t size, double n) {
  snprintf(data, size, "%g", n);
}
static inline void jxl_format_number_f(char* data, size_t size, float n) {
  jxl_format_number_d(data, size, (double)(n));
}

#define JXL_JOIN(x, y) JXL_DO_JOIN(x, y)
#define JXL_DO_JOIN(x, y) x##y


#endif  // LIB_JXL_BASE_COMMON_H_
