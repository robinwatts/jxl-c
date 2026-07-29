// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_BASE_RANDOM_
#define LIB_JXL_BASE_RANDOM_

// Random number generator + distributions.
// We don't use <random> because the implementation (and thus results) differs
// between libstdc++ and libc++.

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "lib/jxl/base/compiler_specific.h"
#include "lib/jxl/base/enc_status.h"

// State for geometric distributions (stored value is inv_log_1mp).
typedef float jxl_rng_geometric_distribution;

typedef struct jxl_rng {
  uint64_t s[2];
} jxl_rng;

static inline jxl_rng jxl_rng_make(uint64_t seed) {
  jxl_rng self;
  self.s[0] = (uint64_t)(0x94D049BB133111EBull);
  self.s[1] = (uint64_t)(0xBF58476D1CE4E5B9ull) + seed;
  return self;
}

// Xorshift128+ adapted from xorshift128+-inl.h
static inline uint64_t jxl_rng_next(jxl_rng* self) {
  uint64_t s1 = self->s[0];
  const uint64_t s0 = self->s[1];
  const uint64_t bits = s1 + s0;  // b, c
  self->s[0] = s0;
  s1 ^= s1 << 23;
  s1 ^= s0 ^ (s1 >> 18) ^ (s0 >> 5);
  self->s[1] = s1;
  return bits;
}

// Uniformly distributed float in [begin, end) range. Note: only 23 bits of
// randomness.
static inline float jxl_rng_uniform_f(jxl_rng* self, float begin, float end) {
  float f;
  // jxl_bits of a random [1, 2) float.
  uint32_t u = (jxl_rng_next(self) >> (64 - 23)) | 0x3F800000;
  JXL_STATIC_ASSERT(sizeof(f) == sizeof(u),
                    "Float and U32 must have the same size");
  memcpy(&f, &u, sizeof(f));
  // Note: (end-begin) * f + (2*begin-end) may fail to return a number >=
  // begin.
  return (end - begin) * (f - 1.0f) + begin;
}

static inline jxl_rng_geometric_distribution jxl_rng_make_geometric(float p) {
  return 1.0 / log(1 - p);
}

static inline uint32_t jxl_rng_geometric(jxl_rng* self, jxl_rng_geometric_distribution dist) {
  float f = jxl_rng_uniform_f(self, 0, 1);
  float inv_log_1mp = dist;
  float geo = logf(1 - f) * inv_log_1mp;
  return (uint32_t)(geo);
}

#endif  // LIB_JXL_BASE_RANDOM_
