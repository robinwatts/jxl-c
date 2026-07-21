// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_ENC_LZ77_H_
#define LIB_JXL_ENC_LZ77_H_

#include <stddef.h>

#include "lib/jxl/dec_ans.h"
#include "lib/jxl/enc_ans.h"
#include "lib/jxl/enc_ans_params.h"

// Writes LZ77-compressed token streams into *out. If compression is not
// beneficial, *out is left empty.
void jxl_apply_lz77(const jxl_histogram_params* params, size_t num_contexts,
               const jxl_token_streams* tokens, const jxl_lz77_params* lz77,
               const jxl_array_size* image_widths, jxl_token_streams* out);


#endif  // LIB_JXL_ENC_LZ77_H_
