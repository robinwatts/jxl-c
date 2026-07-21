// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "lib/jxl/quantizer.h"

#include <stdint.h>

#include "lib/jxl/base/status.h"
#include "lib/jxl/fields.h"
#include "lib/jxl/quant_weights.h"

static const int32_t kDefaultQuant = 64;

void jxl_quantizer_init_with(jxl_quantizer* self, const jxl_dequant_matrices* dequant,
                       int quant_dc, int global_scale) {
  self->global_scale_ = global_scale;
  self->quant_dc_ = quant_dc;
  self->dequant_ = dequant;
  jxl_quantizer_recompute_from_global_scale(self);
  self->inv_quant_dc_ = self->inv_global_scale_ / quant_dc;
}

void jxl_quantizer_init(jxl_quantizer* self, const jxl_dequant_matrices* dequant) {
  jxl_quantizer_init_with(self, dequant, kDefaultQuant,
                    kGlobalScaleDenom / kDefaultQuant);
}

jxl_status jxl_quantizer_params_visit_fields(jxl_quantizer_params* self, jxl_visitor* JXL_RESTRICT visitor) {
  JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_bits_offset(11, 1), jxl_bits_offset(11, 2049), jxl_bits_offset(12, 4097), jxl_bits_offset(16, 8193)), 1, &self->global_scale));
  JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(16), jxl_bits_offset(5, 1), jxl_bits_offset(8, 1), jxl_bits_offset(16, 1)), 1, &self->quant_dc));
  return jxl_ok_status();
}

jxl_quantizer_params jxl_quantizer_get_params(const jxl_quantizer* self) {
  jxl_quantizer_params params;
  jxl_quantizer_params_init(&params);
  params.global_scale = self->global_scale_;
  params.quant_dc = self->quant_dc_;
  return params;
}

