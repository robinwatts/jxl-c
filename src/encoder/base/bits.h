// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#ifndef JXL_ENC_BASE_BITS_H_
#define JXL_ENC_BASE_BITS_H_

// Specialized instructions for processing register-sized bit arrays.

#include "base/compiler_specific.h"
#include "base/enc_status.h"

#if JXL_COMPILER_MSVC
#include <intrin.h>
#endif

#include <stddef.h>
#include <stdint.h>


// Undefined results for x == 0.
static JXL_INLINE JXL_MAYBE_UNUSED size_t
Num0BitsAboveMS1Bit_Nonzero32(const uint32_t x) {
  JXL_DASSERT(x != 0);
#if JXL_COMPILER_MSVC
  unsigned long index;
  _BitScanReverse(&index, x);
  return 31 - index;
#else
  return (size_t)(__builtin_clz(x));
#endif
}
static JXL_INLINE JXL_MAYBE_UNUSED size_t
Num0BitsAboveMS1Bit_Nonzero64(const uint64_t x) {
  JXL_DASSERT(x != 0);
#if JXL_COMPILER_MSVC
#if JXL_ARCH_X64
  unsigned long index;
  _BitScanReverse64(&index, x);
  return 63 - index;
#else   // JXL_ARCH_X64
  // _BitScanReverse64 not available
  uint32_t msb = (uint32_t)(x >> 32u);
  unsigned long index;
  if (msb == 0) {
    uint32_t lsb = (uint32_t)(x & 0xFFFFFFFF);
    _BitScanReverse(&index, lsb);
    return 63 - index;
  } else {
    _BitScanReverse(&index, msb);
    return 31 - index;
  }
#endif  // JXL_ARCH_X64
#else
  return (size_t)(__builtin_clzll(x));
#endif
}

// Undefined results for x == 0.
static JXL_INLINE JXL_MAYBE_UNUSED size_t
Num0BitsBelowLS1Bit_Nonzero32(uint32_t x) {
  JXL_DASSERT(x != 0);
#if JXL_COMPILER_MSVC
  unsigned long index;
  _BitScanForward(&index, x);
  return index;
#else
  return (size_t)(__builtin_ctz(x));
#endif
}
static JXL_INLINE JXL_MAYBE_UNUSED size_t
Num0BitsBelowLS1Bit_Nonzero64(uint64_t x) {
  JXL_DASSERT(x != 0);
#if JXL_COMPILER_MSVC
#if JXL_ARCH_X64
  unsigned long index;
  _BitScanForward64(&index, x);
  return index;
#else   // JXL_ARCH_64
  // _BitScanForward64 not available
  uint32_t lsb = (uint32_t)(x & 0xFFFFFFFF);
  unsigned long index;
  if (lsb == 0) {
    uint32_t msb = (uint32_t)(x >> 32u);
    _BitScanForward(&index, msb);
    return 32 + index;
  } else {
    _BitScanForward(&index, lsb);
    return index;
  }
#endif  // JXL_ARCH_X64
#else
  return (size_t)(__builtin_ctzll(x));
#endif
}

// Returns bit width for x == 0.
static JXL_INLINE JXL_MAYBE_UNUSED size_t jxl_num0_bits_above_ms1_bit32(const uint32_t x) {
  return (x == 0) ? 32 : Num0BitsAboveMS1Bit_Nonzero32(x);
}
static JXL_INLINE JXL_MAYBE_UNUSED size_t jxl_num0_bits_above_ms1_bit64(const uint64_t x) {
  return (x == 0) ? 64 : Num0BitsAboveMS1Bit_Nonzero64(x);
}

// Returns base-2 logarithm, rounded down.
static JXL_INLINE JXL_MAYBE_UNUSED size_t jxl_floor_log2_nonzero32(const uint32_t x) {
  return 31 ^ Num0BitsAboveMS1Bit_Nonzero32(x);
}
static JXL_INLINE JXL_MAYBE_UNUSED size_t jxl_floor_log2_nonzero64(const uint64_t x) {
  return 63 ^ Num0BitsAboveMS1Bit_Nonzero64(x);
}

// Returns base-2 logarithm, rounded up.
static JXL_INLINE JXL_MAYBE_UNUSED size_t jxl_ceil_log2_nonzero32(const uint32_t x) {
  const size_t floor_log2 = jxl_floor_log2_nonzero32(x);
  if ((x & (x - 1)) == 0) return floor_log2;  // power of two
  return floor_log2 + 1;
}
static JXL_INLINE JXL_MAYBE_UNUSED size_t jxl_ceil_log2_nonzero64(const uint64_t x) {
  const size_t floor_log2 = jxl_floor_log2_nonzero64(x);
  if ((x & (x - 1)) == 0) return floor_log2;  // power of two
  return floor_log2 + 1;
}

// Population count (number of 1-bits).
static JXL_INLINE JXL_MAYBE_UNUSED size_t jxl_pop_count(uint64_t x) {
#if JXL_COMPILER_MSVC
#if JXL_ARCH_X64
  return (size_t)(__popcnt64(x));
#else
  return (size_t)(__popcnt((uint32_t)(x)) +
                  __popcnt((uint32_t)(x >> 32)));
#endif
#else
  return (size_t)(__builtin_popcountll(x));
#endif
}


#endif  // JXL_ENC_BASE_BITS_H_
