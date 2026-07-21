// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_ENC_ICC_CODEC_H_
#define LIB_JXL_ENC_ICC_CODEC_H_

// Compressed representation of ICC profiles.

#include <stdint.h>

#include "lib/jxl/base/compiler_specific.h"
#include "lib/jxl/base/span.h"
#include "lib/jxl/base/status.h"
#include "lib/jxl/enc_bit_writer.h"
#include "lib/jxl/padded_bytes.h"

#include "lib/jxl/layer_type.h"
// Should still be called if `icc.empty()` - if so, writes only 1 bit.
jxl_status jxl_write_icc(const jxl_bytes* icc, jxl_bit_writer* JXL_RESTRICT writer, jxl_layer_type layer);


#endif  // LIB_JXL_ENC_ICC_CODEC_H_
