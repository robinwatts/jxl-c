// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_ENC_ENTROPY_CODER_H_
#define LIB_JXL_ENC_ENTROPY_CODER_H_

#include <stdint.h>

#include "lib/jxl/ac_context.h"  // jxl_block_ctx_map
#include "lib/jxl/ac_strategy.h"
#include "lib/jxl/base/compiler_specific.h"
#include "lib/jxl/base/rect.h"
#include "lib/jxl/base/enc_status.h"
#include "lib/jxl/coeff_order_fwd.h"
#include "lib/jxl/enc_ans.h"
#include "lib/jxl/enc_frame_header.h"  // jxl_y_cb_cr_chroma_subsampling
#include "lib/jxl/enc_image.h"

// jxl_entropy coding and context modeling of DC and AC coefficients, as well as AC
// strategy and quantization field.

// Generate DCT NxN quantized AC values tokens.
// Only the subset "rect" [in units of blocks] within all images.
// See also DecodeACVarBlock.
jxl_status jxl_tokenize_coefficients(const coeff_order_t* JXL_RESTRICT orders,
                            const jxl_rect* rect,
                            const int32_t* JXL_RESTRICT* JXL_RESTRICT ac_rows,
                            const jxl_ac_strategy_image* ac_strategy,
                            const jxl_y_cb_cr_chroma_subsampling* cs,
                            jxl_image3_i* JXL_RESTRICT tmp_num_nzeroes,
                            jxl_token_stream* JXL_RESTRICT output,
                            const jxl_image_b* qdc, const jxl_image_i* qf,
                            const jxl_block_ctx_map* block_ctx_map);


#endif  // LIB_JXL_ENC_ENTROPY_CODER_H_
