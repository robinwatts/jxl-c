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
#include "common/hybrid_uint.h"
#include "common/lz77_special_distances.h"
#include "field_encodings.h"
#include "fields.h"

/* Encoder-facing encode entry; implementation lives in common/hybrid_uint.h. */
static JXL_INLINE void jxl_hybrid_uint_config_encode(jxl_hybrid_uint_config self, uint32_t value,
                                                     uint32_t* JXL_RESTRICT token,
                                                     uint32_t* JXL_RESTRICT nbits,
                                                     uint32_t* JXL_RESTRICT bits) {
  jxl_hybrid_uint_encode(self, value, token, nbits, bits);
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
