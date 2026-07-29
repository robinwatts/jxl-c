// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#ifndef JXL_ENC_ENC_CHROMA_FROM_LUMA_H_
#define JXL_ENC_ENC_CHROMA_FROM_LUMA_H_

#include "base/enc_status.h"
#include "chroma_from_luma.h"
#include "enc_bit_writer.h"

#include "layer_type.h"
jxl_enc_status jxl_color_correlation_encode_dc(const jxl_color_correlation* color_correlation,
                                jxl_bit_writer* writer, jxl_layer_type layer);


#endif  // JXL_ENC_ENC_CHROMA_FROM_LUMA_H_
