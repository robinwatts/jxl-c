// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_MODULAR_ENCODING_ENC_ENCODING_H_
#define LIB_JXL_MODULAR_ENCODING_ENC_ENCODING_H_

#include <stddef.h>
#include <stdint.h>

#include "lib/jxl/base/enc_status.h"
#include "lib/jxl/enc_ans.h"
#include "lib/jxl/enc_bit_writer.h"
#include "lib/jxl/modular/encoding/dec_ma.h"
#include "lib/jxl/modular/modular_image.h"
#include "lib/jxl/modular/options.h"

#include "lib/jxl/layer_type.h"
#include "lib/jxl/modular/encoding/encoding.h"

void jxl_predefined_tree(jxl_modular_tree_kind tree_kind, size_t total_pixels,
                    int bitdepth, jxl_tree *out);

jxl_enc_status jxl_learn_tree(const jxl_image *images, const jxl_modular_options *opts,
                 uint32_t start, uint32_t stop,
                 const jxl_array_modular_multiplier_info *multiplier_info,
                 jxl_tree *out);

// Default single-image compress.
jxl_enc_status jxl_modular_generic_compress(const jxl_image *image, const jxl_modular_options *opts,
                              jxl_bit_writer *writer, jxl_layer_type layer);

// For encoding with a given tree.
jxl_enc_status jxl_modular_compress(const jxl_image *image, const jxl_modular_options *opts,
                       size_t group_id, const jxl_tree *tree, jxl_group_header *header,
                       jxl_token_stream *tokens, size_t *width);

#endif  // LIB_JXL_MODULAR_ENCODING_ENC_ENCODING_H_
