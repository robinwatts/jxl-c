// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_COMMON_PACK_SIGNED_H_
#define JXL_COMMON_PACK_SIGNED_H_

#include "compiler.h"

#include <stdint.h>

#if defined(__has_attribute)
#if __has_attribute(no_sanitize)
#define JXL_PACK_NO_SANITIZE(x) __attribute__((no_sanitize(x)))
#endif
#endif
#ifndef JXL_PACK_NO_SANITIZE
#define JXL_PACK_NO_SANITIZE(x)
#endif

/* Encodes non-negative X as 2*X, negative -X as 2*X-1 (JPEG XL signed pack). */
jxl_inline JXL_PACK_NO_SANITIZE("unsigned-integer-overflow") uint32_t
jxl_pack_signed(int32_t value) {
  return ((uint32_t)(value) << 1) ^ (((uint32_t)(~value) >> 31) - 1);
}

/* Inverse of jxl_pack_signed. */
jxl_inline int32_t jxl_unpack_signed(uint32_t x) {
  uint32_t bit = x & 1u;
  uint32_t base = x >> 1;
  uint32_t flip = 0u - bit;
  return (int32_t)(base ^ flip);
}

jxl_inline int64_t jxl_unpack_signed_u64(uint64_t x) {
  uint64_t bit = x & 1ull;
  uint64_t base = x >> 1;
  uint64_t flip = 0ull - bit;
  return (int64_t)(base ^ flip);
}

#endif /* JXL_COMMON_PACK_SIGNED_H_ */
