// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.
#include "lib/jxl/quant_weights.h"

#include <stddef.h>

void jxl_dequant_matrices_init(jxl_dequant_matrices* self,
                               jxl_context* ctx) {
  jxl_array_construct_empty(&self->encodings_, ctx);
  for (size_t i = 0; i < kNumQuantTables; ++i) {
    jxl_array_construct_empty(&self->raw_qtables_[i], ctx);
  }
  self->dc_quant_[0] = kDCQuant[0];
  self->dc_quant_[1] = kDCQuant[1];
  self->dc_quant_[2] = kDCQuant[2];
  self->inv_dc_quant_[0] = kInvDCQuant[0];
  self->inv_dc_quant_[1] = kInvDCQuant[1];
  self->inv_dc_quant_[2] = kInvDCQuant[2];
  jxl_quant_encoding init = jxl_quant_encoding_library(0);
  if (!jxl_status_ok(jxl_array_quant_encoding_resize_fill(&self->encodings_, kNumQuantTables, init))) {
    JXL_CRASH();
  }
}

