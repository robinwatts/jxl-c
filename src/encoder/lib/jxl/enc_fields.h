// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_ENC_FIELDS_H_
#define LIB_JXL_ENC_FIELDS_H_

#include <stdint.h>

#include "lib/jxl/base/compiler_specific.h"
#include "lib/jxl/base/status.h"
#include "lib/jxl/enc_bit_writer.h"
#include "lib/jxl/frame_header.h"
#include "lib/jxl/headers.h"
#include "lib/jxl/image_metadata.h"
#include "lib/jxl/quantizer.h"

#include "lib/jxl/layer_type.h"
// Write headers from the jxl_codec_metadata. Also may modify nonserialized_...
// fields of the metadata.
jxl_status jxl_write_codestream_headers(jxl_codec_metadata* metadata, jxl_bit_writer* writer);

jxl_status jxl_write_frame_header(const jxl_frame_header* frame,
                        jxl_bit_writer* JXL_RESTRICT writer);

jxl_status jxl_write_quantizer_params(const jxl_quantizer_params* params,
                            jxl_bit_writer* JXL_RESTRICT writer, jxl_layer_type layer);

jxl_status jxl_write_size_header(const jxl_size_header* size, jxl_bit_writer* JXL_RESTRICT writer,
                       jxl_layer_type layer);


#endif  // LIB_JXL_ENC_FIELDS_H_
