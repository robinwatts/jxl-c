// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_COMMON_HYBRID_UINT_H_
#define JXL_COMMON_HYBRID_UINT_H_

#include "compiler.h"

#include <stddef.h>
#include <stdint.h>

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

/* Hybrid-uint config and pure encode/decode helpers (ISO/IEC 18181-1). Shared
 * by decode, the JPEG→JXL encoder, and simple lossless. */

typedef struct jxl_hybrid_uint_config {
  uint32_t split_exponent;
  uint32_t split; /* 1u << split_exponent */
  uint32_t msb_in_token;
  uint32_t lsb_in_token;
} jxl_hybrid_uint_config;

jxl_inline jxl_hybrid_uint_config jxl_hybrid_uint_config_make(uint32_t split_exponent,
                                                              uint32_t msb_in_token,
                                                              uint32_t lsb_in_token) {
  jxl_hybrid_uint_config self;
  self.split_exponent = split_exponent;
  self.split = 1u << split_exponent;
  self.msb_in_token = msb_in_token;
  self.lsb_in_token = lsb_in_token;
  return self;
}

jxl_inline jxl_hybrid_uint_config jxl_hybrid_uint_config_default(void) {
  return jxl_hybrid_uint_config_make(4, 2, 0);
}

jxl_inline uint32_t jxl_hybrid_uint_config_lsb_mask(jxl_hybrid_uint_config self) {
  return self.lsb_in_token == 0 ? 0u : ((1u << self.lsb_in_token) - 1u);
}

jxl_inline uint32_t jxl_hybrid_uint_floor_log2_nonzero(uint32_t x) {
#if defined(_MSC_VER) && !defined(__clang__)
  unsigned long index;
  _BitScanReverse(&index, x);
  return (uint32_t)index;
#else
  return 31u - (uint32_t)__builtin_clz(x);
#endif
}

jxl_inline void jxl_hybrid_uint_encode(jxl_hybrid_uint_config self, uint32_t value,
                                       uint32_t *token, uint32_t *nbits, uint32_t *bits) {
  if (value < self.split) {
    *token = value;
    *nbits = 0;
    *bits = 0;
  } else {
    uint32_t n = jxl_hybrid_uint_floor_log2_nonzero(value);
    uint32_t m = value - (1u << n);
    *token = self.split + ((n - self.split_exponent) << (self.msb_in_token + self.lsb_in_token)) +
             ((m >> (n - self.msb_in_token)) << self.lsb_in_token) +
             (m & jxl_hybrid_uint_config_lsb_mask(self));
    *nbits = n - self.msb_in_token - self.lsb_in_token;
    *bits = (value >> self.lsb_in_token) & ((1u << *nbits) - 1u);
  }
}

/* Extra bits to read after `token` when reconstructing a hybrid uint. */
jxl_inline uint32_t jxl_hybrid_uint_extra_bits(uint32_t split, uint32_t msb_in_token,
                                               uint32_t lsb_in_token, uint32_t split_exponent,
                                               uint32_t token) {
  if (token < split) {
    return 0;
  }
  return (split_exponent - (msb_in_token + lsb_in_token) +
          ((token - split) >> (msb_in_token + lsb_in_token))) &
         31u;
}

/* Reconstruct value from token + already-read `rest_bits` (low `n` bits). */
jxl_inline uint32_t jxl_hybrid_uint_decode(uint32_t split, uint32_t msb_in_token,
                                           uint32_t lsb_in_token, uint32_t token,
                                           uint32_t rest_bits, uint32_t n) {
  uint32_t tok;
  uint32_t low_mask;
  uint64_t low_bits;
  if (token < split) {
    return token;
  }
  low_mask = lsb_in_token == 0 ? 0u : ((1u << lsb_in_token) - 1u);
  low_bits = token & low_mask;
  tok = token >> lsb_in_token;
  {
    const uint32_t msb_mask = msb_in_token == 0 ? 0u : ((1u << msb_in_token) - 1u);
    tok &= msb_mask;
    tok |= 1u << msb_in_token;
    return (uint32_t)(((((uint64_t)tok << n) | rest_bits) << lsb_in_token) | low_bits);
  }
}

jxl_inline uint32_t jxl_hybrid_uint_decode_config(const jxl_hybrid_uint_config *config,
                                                  uint32_t token, uint32_t rest_bits, uint32_t n) {
  return jxl_hybrid_uint_decode(config->split, config->msb_in_token, config->lsb_in_token, token,
                                rest_bits, n);
}

#endif /* JXL_COMMON_HYBRID_UINT_H_ */
