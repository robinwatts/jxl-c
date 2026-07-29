// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_ENC_CONTEXT_MAP_H_
#define LIB_JXL_ENC_CONTEXT_MAP_H_

#include <stddef.h>
#include <stdint.h>

#include "ac_context.h"
#include "base/array.h"
#include "base/enc_status.h"
#include "enc_bit_writer.h"

#include "layer_type.h"
// Max limit is 255 because encoding assumes numbers < 255
// More clusters can help compression, but makes encode/decode somewhat slower
static const size_t kClustersLimit = 128;

// Encodes the given context map to the bit stream. The number of different
// histogram ids is given by num_histograms.
jxl_enc_status jxl_encode_context_map(const jxl_array_u8* context_map, size_t num_histograms,
                        jxl_bit_writer* writer, jxl_layer_type layer);

jxl_enc_status jxl_encode_block_ctx_map(const jxl_block_ctx_map* block_ctx_map, jxl_bit_writer* writer);

#endif  // LIB_JXL_ENC_CONTEXT_MAP_H_
