// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_ENC_CHROMA_FROM_LUMA_H_
#define LIB_JXL_ENC_CHROMA_FROM_LUMA_H_

#include "lib/jxl/base/enc_status.h"
#include "lib/jxl/chroma_from_luma.h"
#include "lib/jxl/enc_bit_writer.h"

#include "lib/jxl/layer_type.h"
jxl_status jxl_color_correlation_encode_dc(const jxl_color_correlation* color_correlation,
                                jxl_bit_writer* writer, jxl_layer_type layer);


#endif  // LIB_JXL_ENC_CHROMA_FROM_LUMA_H_
