// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#ifndef JXL_ENC_ENC_CACHE_H_
#define JXL_ENC_ENC_CACHE_H_

#include <jxl/context.h>
#include "enc_allocator.h"

#include <stdint.h>

#include "dct_util.h"
#include "enc_ans.h"
#include "enc_params.h"
#include "passes_state.h"


// Contains encoder state.
typedef struct jxl_passes_encoder_state {
  jxl_passes_shared_state shared;

  // DCT coefficients for the image. One row per group.
  jxl_ac_image coeffs;

  jxl_compress_params cparams;

  jxl_token_streams ac_tokens;
  jxl_entropy_encoding_data ac_codes;

  // Block sizes seen so far.
  uint32_t used_acs;
  // Coefficient orders that are non-default.
  uint32_t used_orders;

} jxl_passes_encoder_state;

static inline void jxl_passes_encoder_state_init(jxl_passes_encoder_state* self,
                                          jxl_context* ctx) {
  jxl_passes_shared_state_init(&self->shared, ctx);
  jxl_ac_image_construct_empty(&self->coeffs);
  jxl_entropy_encoding_data_init(&self->ac_codes, ctx);
  jxl_token_streams_construct_empty(&self->ac_tokens);
  self->ac_tokens.ctx = ctx;
  self->used_acs = 0;
  self->used_orders = 0;
}

static inline void jxl_passes_encoder_state_destroy(jxl_passes_encoder_state* self) {
  jxl_passes_shared_state_destroy(&self->shared);
  jxl_ac_image_destroy(&self->coeffs);
  jxl_token_streams_destroy(&self->ac_tokens);
  jxl_entropy_encoding_data_destroy(&self->ac_codes);
}

static inline jxl_context* jxl_passes_encoder_state_ctx(
    const jxl_passes_encoder_state* self) {
  return self->shared.ctx;
}


#endif  // JXL_ENC_ENC_CACHE_H_
