// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_ENC_FRAME_H_
#define LIB_JXL_ENC_FRAME_H_

#include <jxl/memory_manager.h>
#include <jxl/types.h>

#include "lib/jxl/base/status.h"
#include "lib/jxl/enc_params.h"
#include "lib/jxl/encode_internal.h"
#include "lib/jxl/image_metadata.h"

// Information needed for encoding a frame that is not contained elsewhere and
// does not belong to `cparams`.
typedef struct jxl_frame_info {
  bool is_last;
} jxl_frame_info;

// Encodes a single frame (including its header) into a byte stream.
jxl_status jxl_encode_frame(jxl_memory_manager* memory_manager,
                   const jxl_compress_params* cparams_orig,
                   const jxl_frame_info* frame_info, const jxl_codec_metadata* metadata,
                   jxl_encoder_jpeg_frame_adapter* frame_data,
                   jxl_encoder_output_processor_wrapper* output_processor);


#endif  // LIB_JXL_ENC_FRAME_H_
