// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#ifndef JXL_ENC_ENC_QUANT_WEIGHTS_H_
#define JXL_ENC_ENC_QUANT_WEIGHTS_H_

#include <jxl/context.h>
#include "enc_allocator.h"

#include <stdint.h>

#include "base/array.h"
#include "base/enc_status.h"
#include "enc_bit_writer.h"
#include "quant_weights.h"

#include "layer_type.h"
struct jxl_modular_frame_encoder;

jxl_enc_status jxl_dequant_matrices_encode(jxl_context* ctx,
                             const jxl_dequant_matrices* matrices, jxl_bit_writer* writer,
                             jxl_layer_type layer,
                             jxl_modular_frame_encoder* modular_frame_encoder);
jxl_enc_status jxl_dequant_matrices_encode_dc(const jxl_dequant_matrices* matrices,
                               jxl_bit_writer* writer, jxl_layer_type layer);
// Applies F16 wire quantization to DC dequant (same as a decoder would).
jxl_enc_status jxl_dequant_matrices_set_custom_dc(jxl_dequant_matrices* matrices, const float* dc);

jxl_enc_status jxl_dequant_matrices_set_custom(
    jxl_dequant_matrices* matrices, const jxl_array_quant_encoding* encodings,
    jxl_array_int* raw_qtables,
    jxl_modular_frame_encoder* encoder);


#endif  // JXL_ENC_ENC_QUANT_WEIGHTS_H_
