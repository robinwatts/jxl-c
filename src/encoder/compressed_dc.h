// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#ifndef JXL_ENC_COMPRESSED_DC_H_
#define JXL_ENC_COMPRESSED_DC_H_

#include "ac_context.h"
#include "base/rect.h"
#include "enc_frame_header.h"
#include "enc_image.h"
#include "modular/modular_image.h"

// Fills per-block DC context indices into quant_dc from quantized modular DC.
void jxl_fill_quant_dc(const jxl_rect* r, jxl_image_b* quant_dc, const jxl_image* in,
                 const jxl_y_cb_cr_chroma_subsampling* chroma_subsampling,
                 const jxl_block_ctx_map* bctx);


#endif  // JXL_ENC_COMPRESSED_DC_H_
