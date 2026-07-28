// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_JPEG_ENC_JPEG_DATA_H_
#define LIB_JXL_JPEG_ENC_JPEG_DATA_H_

#include <jxl/cms_interface.h>
#include <jxl/context.h>
#include <jxl/context.h>
#include "lib/jxl/allocator.h"

#include <stdint.h>

#include "lib/jxl/base/array.h"
#include "lib/jxl/base/span.h"
#include "lib/jxl/base/status.h"
#include "lib/jxl/color_encoding_internal.h"
#include "lib/jxl/enc_params.h"
#include "lib/jxl/frame_header.h"
#include "lib/jxl/jpeg/jpeg_data.h"


// Optional text/EXIF metadata.
typedef struct jxl_jpeg_blobs {
  jxl_array_u8 exif;
  jxl_array_u8 xmp;

} jxl_jpeg_blobs;

static inline void jxl_jpeg_blobs_construct_empty(jxl_jpeg_blobs* self,
                                                  jxl_context* mm) {
  jxl_array_construct_empty(&self->exif, mm);
  jxl_array_construct_empty(&self->xmp, mm);
}

static inline void jxl_jpeg_blobs_destroy(jxl_jpeg_blobs* self) {
  jxl_array_destroy(&self->exif);
  jxl_array_destroy(&self->xmp);
}

jxl_status jxl_encode_jpeg_data(jxl_context* ctx, jxl_jpeg_data* jpeg_data,
                      jxl_array_u8* bytes, const jxl_compress_params* cparams);

jxl_status jxl_set_color_encoding_from_jpeg_data(jxl_context* ctx,
                                    const jxl_cms_interface* cms,
                                    const jxl_jpeg_data* jpg,
                                    jxl_enc_color_encoding* color_encoding);
jxl_status jxl_set_chroma_subsampling_from_jpeg_data(const jxl_jpeg_data* jpg,
                                        jxl_y_cb_cr_chroma_subsampling* cs);
jxl_status jxl_set_color_transform_from_jpeg_data(const jxl_jpeg_data* jpg,
                                     jxl_color_transform* color_transform);

/**
 * Decodes bytes containing JPEG codestream as coefficients only,
 * for lossless JPEG transcoding.
 */
jxl_status jxl_parse_jpg(jxl_context* ctx, const jxl_bytes* bytes,
                jxl_jpeg_data* out);
jxl_status jxl_set_blobs_from_jpeg_data(const jxl_jpeg_data* jpeg_data, jxl_jpeg_blobs* blobs);


#endif  // LIB_JXL_JPEG_ENC_JPEG_DATA_H_
