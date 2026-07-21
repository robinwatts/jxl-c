// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_COMPRESSED_DC_H_
#define LIB_JXL_COMPRESSED_DC_H_

#include "lib/jxl/ac_context.h"
#include "lib/jxl/base/rect.h"
#include "lib/jxl/frame_header.h"
#include "lib/jxl/image.h"
#include "lib/jxl/modular/modular_image.h"

// Fills per-block DC context indices into quant_dc from quantized modular DC.
void jxl_fill_quant_dc(const jxl_rect* r, jxl_image_b* quant_dc, const jxl_image* in,
                 const jxl_y_cb_cr_chroma_subsampling* chroma_subsampling,
                 const jxl_block_ctx_map* bctx);


#endif  // LIB_JXL_COMPRESSED_DC_H_
