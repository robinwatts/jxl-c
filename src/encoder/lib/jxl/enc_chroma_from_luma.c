// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "lib/jxl/enc_chroma_from_luma.h"

#include "lib/jxl/base/common.h"
#include "lib/jxl/fields.h"

typedef struct jxl_color_correlation_encode_ctx {
  jxl_bit_writer* writer;
  float color_factor;
  float base_correlation_x;
  float base_correlation_b;
  int32_t ytox_dc;
  int32_t ytob_dc;
} jxl_color_correlation_encode_ctx;

static jxl_enc_status jxl_color_correlation_encode_body(void* opaque) {
  jxl_color_correlation_encode_ctx* c = (jxl_color_correlation_encode_ctx*)(opaque);
  if (c->ytox_dc == 0 && c->ytob_dc == 0 &&
      c->color_factor == kDefaultColorFactor && c->base_correlation_x == 0.0f &&
      c->base_correlation_b == kYToBRatio) {
    jxl_bit_writer_write(c->writer, 1, 1);
    return jxl_enc_ok_status();
  }
  jxl_bit_writer_write(c->writer, 1, 0);
  JXL_RETURN_IF_ERROR(
      jxl_u32_coder_write(jxl_color_factor_dist(), c->color_factor, c->writer));
  JXL_RETURN_IF_ERROR(jxl_f16_coder_write(c->base_correlation_x, c->writer));
  JXL_RETURN_IF_ERROR(jxl_f16_coder_write(c->base_correlation_b, c->writer));
  jxl_bit_writer_write(c->writer, kBitsPerByte, c->ytox_dc - INT8_MIN);
  jxl_bit_writer_write(c->writer, kBitsPerByte, c->ytob_dc - INT8_MIN);
  return jxl_enc_ok_status();
}

jxl_enc_status jxl_color_correlation_encode_dc(const jxl_color_correlation* color_correlation,
                                jxl_bit_writer* writer, jxl_layer_type layer){
  jxl_color_correlation_encode_ctx ctx = {
      writer,
      jxl_color_correlation_get_color_factor(color_correlation),
      jxl_color_correlation_get_base_correlation_x(color_correlation),
      jxl_color_correlation_get_base_correlation_b(color_correlation),
      jxl_color_correlation_get_y_to_xdc(color_correlation),
      jxl_color_correlation_get_y_to_bdc(color_correlation),
  };
  return jxl_bit_writer_with_max_bits(writer, 1 + 2 * kBitsPerByte + 12 + 32, layer,
                             jxl_color_correlation_encode_body, &ctx);
}


