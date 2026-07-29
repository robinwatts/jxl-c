// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#ifndef JXL_ENC_DEC_ANS_H_
#define JXL_ENC_DEC_ANS_H_

// ANS / hybrid-uint / LZ77 parameter types shared with the encoder.

#include <stddef.h>
#include <stdint.h>

#include <jxl/types.h>

#include "ans_common.h"
#include "ans_params.h"
#include "base/array.h"
#include "base/bits.h"
#include "base/compiler_specific.h"
#include "base/enc_status.h"
#include "common/lz77_special_distances.h"
#include "field_encodings.h"
#include "fields.h"

// Experiments show that best performance is typically achieved for a
// split-exponent of 3 or 4. Trend seems to be that '4' is better
// for large-ish pictures, and '3' better for rather small-ish pictures.
// This is plausible - the more special symbols we have, the better
// statistics we need to get a benefit out of them.

// Our hybrid-encoding scheme has dedicated tokens for the smallest
// (1 << split_exponents) numbers, and for the rest
// encodes (number of bits) + (msb_in_token sub-leading binary digits) +
// (lsb_in_token lowest binary digits) in the token, with the remaining bits
// then being encoded as data.
typedef struct jxl_hybrid_uint_config {
  uint32_t split_exponent;
  uint32_t split_token;
  uint32_t msb_in_token;
  uint32_t lsb_in_token;
} jxl_hybrid_uint_config;

static JXL_INLINE jxl_hybrid_uint_config jxl_hybrid_uint_config_make(uint32_t split_exponent,
                                                 uint32_t msb_in_token,
                                                 uint32_t lsb_in_token) {
  JXL_DASSERT(split_exponent >= msb_in_token + lsb_in_token);
  jxl_hybrid_uint_config self;
  self.split_exponent = split_exponent;
  self.split_token = 1u << split_exponent;
  self.msb_in_token = msb_in_token;
  self.lsb_in_token = lsb_in_token;
  return self;
}

static JXL_INLINE jxl_hybrid_uint_config jxl_hybrid_uint_config_default() {
  return jxl_hybrid_uint_config_make(4, 2, 0);
}

static JXL_INLINE void jxl_hybrid_uint_config_encode(jxl_hybrid_uint_config self, uint32_t value,
                                       uint32_t* JXL_RESTRICT token,
                                       uint32_t* JXL_RESTRICT nbits,
                                       uint32_t* JXL_RESTRICT bits) {
  if (value < self.split_token) {
    *token = value;
    *nbits = 0;
    *bits = 0;
  } else {
    uint32_t n = jxl_floor_log2_nonzero32(value);
    uint32_t m = value - (1 << n);
    *token = self.split_token +
             ((n - self.split_exponent)
              << (self.msb_in_token + self.lsb_in_token)) +
             ((m >> (n - self.msb_in_token)) << self.lsb_in_token) +
             (m & ((1 << self.lsb_in_token) - 1));
    *nbits = n - self.msb_in_token - self.lsb_in_token;
    *bits = (value >> self.lsb_in_token) & ((1UL << *nbits) - 1);
  }
}

static JXL_INLINE uint32_t jxl_hybrid_uint_config_lsb_mask(jxl_hybrid_uint_config self) {
  return (1 << self.lsb_in_token) - 1;
}

JXL_DEFINE_POD_ARRAY(jxl_array_hybrid_uint_config, jxl_hybrid_uint_config)

typedef struct jxl_lz77_params {

  jxl_fields fields;
  bool enabled;

  // Symbols above min_symbol use a special hybrid uint encoding and
  // represent a length, to be added to min_length.
  uint32_t min_symbol;
  uint32_t min_length;

  // Not serialized by VisitFields.
  jxl_hybrid_uint_config length_uint_config;

  size_t nonserialized_distance_context;
} jxl_lz77_params;

jxl_enc_status jxl_lz77_params_visit_fields(jxl_lz77_params* self, jxl_visitor* JXL_RESTRICT visitor);
JXL_FIELDS_NAME(jxl_lz77_params)

static inline void jxl_lz77_params_init(jxl_lz77_params* self) {
  JXL_FIELDS_REGISTER_PTR(jxl_lz77_params, &self->fields);
  jxl_bundle_init(&self->fields);
  self->length_uint_config = jxl_hybrid_uint_config_make(0, 0, 0);
}

enum { kWindowSize = 1 << 20 };
enum { kNumSpecialDistances = JXL_NUM_SPECIAL_DISTANCES };


#endif  // JXL_ENC_DEC_ANS_H_
