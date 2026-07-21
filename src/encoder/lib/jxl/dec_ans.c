// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "lib/jxl/dec_ans.h"

#include "lib/jxl/fields.h"

jxl_status jxl_lz77_params_visit_fields(jxl_lz77_params* self, jxl_visitor* JXL_RESTRICT visitor) {
  JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bool(visitor, false, &self->enabled));
  if (!jxl_status_ok(jxl_visitor_conditional(visitor, self->enabled))) return jxl_ok_status();
  JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(224), jxl_val(512), jxl_val(4096), jxl_bits_offset(15, 8)), 224, &self->min_symbol));
  JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(3), jxl_val(4), jxl_bits_offset(2, 5), jxl_bits_offset(8, 9)), 3, &self->min_length));
  return jxl_ok_status();
}

