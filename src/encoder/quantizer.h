// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#ifndef JXL_ENC_QUANTIZER_H_
#define JXL_ENC_QUANTIZER_H_

#include <stddef.h>
#include <stdint.h>

#include "base/compiler_specific.h"
#include "base/enc_status.h"
#include "fields.h"
#include "quant_weights.h"

// Quantizes DC and AC coefficients, with separate quantization tables according
// to the quant_kind (which is currently computed from the AC strategy and the
// block index inside that strategy).

enum { kGlobalScaleDenom = 1 << 16 };

// zero-biases for quantizing channels X, Y, B
static const float kZeroBiasDefault[3] = {0.5f, 0.5f, 0.5f};

static const float kDefaultQuantBias[4] = {
    1.0f - 0.05465007330715401f,
    1.0f - 0.07005449891748593f,
    1.0f - 0.049935103337343655f,
    0.145f,
};

typedef struct jxl_quantizer_params {
  jxl_fields fields;

  uint32_t global_scale;
  uint32_t quant_dc;
} jxl_quantizer_params;

jxl_enc_status jxl_quantizer_params_visit_fields(jxl_quantizer_params* self,
                                  jxl_visitor* JXL_RESTRICT visitor);
JXL_FIELDS_NAME(jxl_quantizer_params)

static inline void jxl_quantizer_params_init(jxl_quantizer_params* self) {
  JXL_FIELDS_REGISTER_PTR(jxl_quantizer_params, &self->fields);
  jxl_bundle_init(&self->fields);
}

typedef struct jxl_enc_quantizer {
  // These are serialized:
  int global_scale_;
  int quant_dc_;

  // These are derived from global_scale_:
  float inv_global_scale_;
  float global_scale_float_;  // reciprocal of inv_global_scale_
  float inv_quant_dc_;

  const jxl_dequant_matrices* dequant_;
} jxl_enc_quantizer;

void jxl_enc_quantizer_init(jxl_enc_quantizer* self, const jxl_dequant_matrices* dequant);
void jxl_enc_quantizer_init_with(jxl_enc_quantizer* self, const jxl_dequant_matrices* dequant,
                       int quant_dc, int global_scale);
jxl_quantizer_params jxl_enc_quantizer_get_params(const jxl_enc_quantizer* self);

static inline void jxl_enc_quantizer_recompute_from_global_scale(jxl_enc_quantizer* self) {
  self->global_scale_float_ = self->global_scale_ * (1.0 / kGlobalScaleDenom);
  self->inv_global_scale_ = 1.0 * kGlobalScaleDenom / self->global_scale_;
  self->inv_quant_dc_ = self->inv_global_scale_ / self->quant_dc_;
}

static inline float jxl_enc_quantizer_inv_global_scale(const jxl_enc_quantizer* self) {
  return self->inv_global_scale_;
}

static inline float jxl_enc_quantizer_get_dc_step(const jxl_enc_quantizer* self, size_t c) {
  return self->inv_quant_dc_ * jxl_dequant_matrices_dc_quant(self->dequant_, c);
}

static inline float jxl_enc_quantizer_get_inv_dc_step(const jxl_enc_quantizer* self, size_t c) {
  return jxl_dequant_matrices_inv_dc_quant(self->dequant_, c) *
         (self->global_scale_float_ * self->quant_dc_);
}

#endif  // JXL_ENC_QUANTIZER_H_
