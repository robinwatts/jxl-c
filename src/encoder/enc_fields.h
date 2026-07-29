// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#ifndef JXL_ENC_ENC_FIELDS_H_
#define JXL_ENC_ENC_FIELDS_H_

#include <stdint.h>

#include "base/compiler_specific.h"
#include "base/enc_status.h"
#include "enc_bit_writer.h"
#include "enc_frame_header.h"
#include "headers.h"
#include "enc_image_metadata.h"
#include "quantizer.h"

#include "layer_type.h"
// Write headers from the jxl_codec_metadata. Also may modify nonserialized_...
// fields of the metadata.
jxl_enc_status jxl_write_codestream_headers(jxl_codec_metadata* metadata, jxl_bit_writer* writer);

jxl_enc_status jxl_write_frame_header(const jxl_enc_frame_header* frame,
                        jxl_bit_writer* JXL_RESTRICT writer);

jxl_enc_status jxl_write_quantizer_params(const jxl_quantizer_params* params,
                            jxl_bit_writer* JXL_RESTRICT writer, jxl_layer_type layer);

jxl_enc_status jxl_write_size_header(const jxl_enc_size_header* size, jxl_bit_writer* JXL_RESTRICT writer,
                       jxl_layer_type layer);


#endif  // JXL_ENC_ENC_FIELDS_H_
