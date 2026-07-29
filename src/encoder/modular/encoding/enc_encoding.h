// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#ifndef JXL_ENC_MODULAR_ENCODING_ENC_ENCODING_H_
#define JXL_ENC_MODULAR_ENCODING_ENC_ENCODING_H_

#include <stddef.h>
#include <stdint.h>

#include "base/enc_status.h"
#include "enc_ans.h"
#include "enc_bit_writer.h"
#include "modular/encoding/dec_ma.h"
#include "modular/modular_image.h"
#include "modular/options.h"

#include "layer_type.h"
#include "modular/encoding/encoding.h"

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

#endif  // JXL_ENC_MODULAR_ENCODING_ENC_ENCODING_H_
