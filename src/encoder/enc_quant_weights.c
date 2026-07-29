// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "enc_quant_weights.h"

#include <jxl/context.h>
#include "enc_allocator.h"

#include "base/enc_status.h"
#include "layer_type.h"
#include "enc_modular.h"
#include "fields.h"
#include "quant_weights.h"


static const float kAlmostZero = 1e-8f;

typedef struct jxl_encode_mats_ctx {
  jxl_context* ctx;
  const jxl_array_quant_encoding* encodings;
  jxl_bit_writer* writer;
  jxl_modular_frame_encoder* modular_frame_encoder;
  bool all_default;
} jxl_encode_mats_ctx;

typedef struct jxl_encode_dc_ctx {
  jxl_bit_writer* writer;
  const float* dc_quant;
  bool all_default;
} jxl_encode_dc_ctx;

static jxl_enc_status jxl_encode_dc_body(void* opaque) {
  jxl_encode_dc_ctx* c = (jxl_encode_dc_ctx*)(opaque);
  jxl_bit_writer_write(c->writer, 1, TO_JXL_BOOL(c->all_default));
  if (!c->all_default) {
    for (size_t ch = 0; ch < 3; ch++) {
      JXL_RETURN_IF_ERROR(jxl_f16_coder_write(c->dc_quant[ch] * 128.0f, c->writer));
    }
  }
  return jxl_enc_ok_status();
}

static jxl_enc_status jxl_encode_quant(jxl_context* ctx,
                   const jxl_quant_encoding* encoding, size_t idx, size_t size_x,
                   size_t size_y, jxl_bit_writer* writer,
                   jxl_modular_frame_encoder* modular_frame_encoder) {
  JXL_ENSURE(modular_frame_encoder != NULL);
  jxl_bit_writer_write(writer, kLog2NumQuantModes, encoding->mode);
  size_x *= kBlockDim;
  size_y *= kBlockDim;
  switch (encoding->mode) {
    case kQuantModeLibrary: {
      jxl_bit_writer_write(writer, kCeilLog2NumPredefinedTables, encoding->predefined);
      break;
    }
    case kQuantModeRAW: {
      JXL_RETURN_IF_ERROR(jxl_modular_frame_encoder_encode_quant_table(
          ctx, size_x, size_y, writer, encoding, idx,
          modular_frame_encoder));
      break;
    }
    default:
      return JXL_FAILURE("Unsupported quant encoding mode for JPEG encoder");
  }
  return jxl_enc_ok_status();
}

static jxl_enc_status jxl_encode_mats_body(void* opaque) {
  jxl_encode_mats_ctx* c = (jxl_encode_mats_ctx*)(opaque);
  jxl_bit_writer_write(c->writer, 1, TO_JXL_BOOL(c->all_default));
  if (!c->all_default) {
    for (size_t i = 0; i < jxl_array_len(c->encodings); i++) {
      JXL_RETURN_IF_ERROR(jxl_encode_quant(
          c->ctx, jxl_array_at_const(c->encodings, i), i,
          kDequantMatricesRequiredSizeX[i],
          kDequantMatricesRequiredSizeY[i], c->writer,
          c->modular_frame_encoder));
    }
  }
  return jxl_enc_ok_status();
}

jxl_enc_status jxl_dequant_matrices_encode(jxl_context* ctx,
                             const jxl_dequant_matrices* matrices, jxl_bit_writer* writer,
                             jxl_layer_type layer,
                             jxl_modular_frame_encoder* modular_frame_encoder){
  bool all_default = true;
  const jxl_array_quant_encoding* encodings = jxl_dequant_matrices_encodings(matrices);

  for (size_t encoding_i = 0; encoding_i < jxl_array_len(encodings); ++encoding_i) {
    const jxl_quant_encoding* encoding = jxl_array_at_const(encodings, encoding_i);
    if (encoding->mode != kQuantModeLibrary ||
        encoding->predefined != 0) {
      all_default = false;
    }
  }
  // TODO(janwas): better bound
  jxl_encode_mats_ctx mats_ctx = {ctx, encodings, writer,
                            modular_frame_encoder, all_default};
  return jxl_bit_writer_with_max_bits(writer, 512 * 1024, layer, jxl_encode_mats_body, &mats_ctx);
}

jxl_enc_status jxl_dequant_matrices_encode_dc(const jxl_dequant_matrices* matrices,
                               jxl_bit_writer* writer, jxl_layer_type layer){
  bool all_default = true;
  const float* dc_quant = jxl_dequant_matrices_dc_quants(matrices);
  for (size_t c = 0; c < 3; c++) {
    if (dc_quant[c] != kDCQuant[c]) {
      all_default = false;
    }
  }
  jxl_encode_dc_ctx dc_ctx = {writer, dc_quant, all_default};
  return jxl_bit_writer_with_max_bits(writer, 1 + sizeof(float) * kBitsPerByte * 3, layer,
                             jxl_encode_dc_body, &dc_ctx);
}

jxl_enc_status jxl_dequant_matrices_set_custom_dc(jxl_dequant_matrices* matrices, const float* dc) {
  // Match decoder F16 wire quantization without BitReader roundtrip:
  // Write(dc_quant*128) then Read then *1/128.
  float dc_quant[3];
  for (size_t c = 0; c < 3; c++) {
    float projected;
    JXL_RETURN_IF_ERROR(jxl_f16_coder_project((1.0f / dc[c]) * 128.0f, &projected));
    dc_quant[c] = projected * (1.0f / 128.0f);
    if (dc_quant[c] < kAlmostZero) {
      return JXL_FAILURE("Invalid dc_quant: coefficient is too small.");
    }
  }
  jxl_dequant_matrices_set_dc_quant_decoded(matrices, dc_quant);
  return jxl_enc_ok_status();
}

jxl_enc_status jxl_dequant_matrices_set_custom(
    jxl_dequant_matrices* matrices, const jxl_array_quant_encoding* encodings,
    jxl_array_int* raw_qtables,
    jxl_modular_frame_encoder* encoder){
  JXL_ENSURE(encoder != NULL);
  JXL_ENSURE(jxl_array_len(encodings) == kNumQuantTables);
  jxl_dequant_matrices_set_encodings(matrices, encodings, raw_qtables);
  for (size_t i = 0; i < jxl_array_len(encodings); i++) {
    if (jxl_array_at_const(encodings, i)->mode == kQuantModeRAW) {
      JXL_RETURN_IF_ERROR(jxl_modular_frame_encoder_add_quant_table(encoder, 
          kDequantMatricesRequiredSizeX[i] * kBlockDim,
          kDequantMatricesRequiredSizeY[i] * kBlockDim, jxl_array_at_const(encodings, i),
          jxl_dequant_matrices_raw_q_table(matrices, i), i));
    }
  }
  return jxl_enc_ok_status();
}
