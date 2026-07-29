// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#ifndef JXL_ENC_ENC_ENTROPY_CODER_H_
#define JXL_ENC_ENC_ENTROPY_CODER_H_

#include <stdint.h>

#include "ac_context.h"  // jxl_block_ctx_map
#include "ac_strategy.h"
#include "base/compiler_specific.h"
#include "base/rect.h"
#include "base/enc_status.h"
#include "coeff_order_fwd.h"
#include "enc_ans.h"
#include "enc_frame_header.h"  // jxl_y_cb_cr_chroma_subsampling
#include "enc_image.h"

// jxl_entropy coding and context modeling of DC and AC coefficients, as well as AC
// strategy and quantization field.

// Generate DCT NxN quantized AC values tokens.
// Only the subset "rect" [in units of blocks] within all images.
// See also DecodeACVarBlock.
jxl_enc_status jxl_tokenize_coefficients(const coeff_order_t* JXL_RESTRICT orders,
                            const jxl_rect* rect,
                            const int32_t* JXL_RESTRICT* JXL_RESTRICT ac_rows,
                            const jxl_ac_strategy_image* ac_strategy,
                            const jxl_y_cb_cr_chroma_subsampling* cs,
                            jxl_image3_i* JXL_RESTRICT tmp_num_nzeroes,
                            jxl_token_stream* JXL_RESTRICT output,
                            const jxl_image_b* qdc, const jxl_image_i* qf,
                            const jxl_block_ctx_map* block_ctx_map);


#endif  // JXL_ENC_ENC_ENTROPY_CODER_H_
