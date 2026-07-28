// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_ENC_MODULAR_H_
#define LIB_JXL_ENC_MODULAR_H_

#include <jxl/context.h>
#include "lib/jxl/allocator.h"

#include <stddef.h>
#include <stdint.h>

#include "lib/jxl/base/array.h"
#include "lib/jxl/base/rect.h"
#include "lib/jxl/base/status.h"
#include "lib/jxl/dec_modular.h"
#include "lib/jxl/enc_ans.h"
#include "lib/jxl/enc_bit_writer.h"
#include "lib/jxl/enc_cache.h"
#include "lib/jxl/enc_params.h"
#include "lib/jxl/frame_dimensions.h"
#include "lib/jxl/frame_header.h"
#include "lib/jxl/image.h"
#include "lib/jxl/image_metadata.h"
#include "lib/jxl/modular/encoding/dec_ma.h"
#include "lib/jxl/modular/encoding/encoding.h"
#include "lib/jxl/modular/modular_image.h"
#include "lib/jxl/modular/options.h"
#include "lib/jxl/quant_weights.h"

#include "lib/jxl/layer_type.h"


typedef struct jxl_modular_frame_encoder {
  jxl_array_size ac_metadata_size;
  jxl_array_u8 extra_dc_precision;

  jxl_context* ctx_;
  jxl_images stream_images_;
  jxl_array_modular_options stream_options_;

  jxl_tree tree_;
  jxl_token_streams tree_tokens_;
  jxl_group_headers stream_headers_;
  jxl_token_streams tokens_;
  jxl_entropy_encoding_data code_;
  jxl_frame_dimensions frame_dim_;
  jxl_compress_params cparams_;
  jxl_array_size tree_splits_;
  jxl_array_size image_widths_;

} jxl_modular_frame_encoder;

jxl_status jxl_modular_frame_encoder_create(jxl_context* ctx,
                                 const jxl_frame_header* frame_header,
                                 const jxl_compress_params* cparams_orig,
                                 jxl_modular_frame_encoder* out);
jxl_status jxl_modular_frame_encoder_init(jxl_modular_frame_encoder* self,
                               const jxl_frame_header* frame_header,
                               const jxl_compress_params* cparams_orig);
jxl_status jxl_modular_frame_encoder_compute_tree(jxl_modular_frame_encoder* self);
jxl_status jxl_modular_frame_encoder_compute_tokens(jxl_modular_frame_encoder* self);
jxl_status jxl_modular_frame_encoder_encode_global_info(jxl_modular_frame_encoder* self,
                                           jxl_bit_writer* writer);
jxl_status jxl_modular_frame_encoder_encode_stream(jxl_modular_frame_encoder* self,
                                       jxl_bit_writer* writer, jxl_layer_type layer,
                                       const jxl_modular_stream_id* stream);
jxl_status jxl_modular_frame_encoder_add_var_dctdc(jxl_modular_frame_encoder* self,
                                      const jxl_frame_header* frame_header,
                                      const jxl_image3_f* dc, const jxl_rect* r,
                                      size_t group_index,
                                      jxl_passes_encoder_state* enc_state);
jxl_status jxl_modular_frame_encoder_add_ac_metadata(jxl_modular_frame_encoder* self,
                                        const jxl_rect* r, size_t group_index,
                                        jxl_passes_encoder_state* enc_state);
jxl_status jxl_modular_frame_encoder_encode_quant_table(
    jxl_context* ctx, size_t size_x, size_t size_y,
    jxl_bit_writer* writer, const jxl_quant_encoding* encoding, size_t idx,
    jxl_modular_frame_encoder* modular_frame_encoder);
jxl_status jxl_modular_frame_encoder_add_quant_table(jxl_modular_frame_encoder* self,
                                        size_t size_x, size_t size_y,
                                        const jxl_quant_encoding* encoding,
                                        const jxl_array_int* qtable, size_t idx);

static inline void jxl_modular_frame_encoder_init_mm(jxl_modular_frame_encoder* self,
                                      jxl_context* ctx) {
  self->ctx_ = ctx;
  jxl_array_construct_empty(&self->ac_metadata_size, ctx);
  jxl_array_construct_empty(&self->extra_dc_precision, ctx);
  jxl_images_construct_empty(&self->stream_images_);
  self->stream_images_.ctx = ctx;
  jxl_array_construct_empty(&self->stream_options_, ctx);
  jxl_array_construct_empty(&self->tree_, ctx);
  jxl_token_streams_construct_empty(&self->tree_tokens_);
  self->tree_tokens_.ctx = ctx;
  jxl_group_headers_construct_empty(&self->stream_headers_);
  self->stream_headers_.ctx = ctx;
  jxl_token_streams_construct_empty(&self->tokens_);
  self->tokens_.ctx = ctx;
  jxl_entropy_encoding_data_construct_empty(&self->code_, ctx);
  jxl_array_construct_empty(&self->tree_splits_, ctx);
  jxl_array_construct_empty(&self->image_widths_, ctx);
}

static inline void jxl_modular_frame_encoder_destroy(jxl_modular_frame_encoder* self) {
  jxl_array_destroy(&self->ac_metadata_size);
  jxl_array_destroy(&self->extra_dc_precision);
  jxl_images_destroy(&self->stream_images_);
  jxl_array_destroy(&self->stream_options_);
  jxl_array_destroy(&self->tree_);
  jxl_token_streams_destroy(&self->tree_tokens_);
  jxl_group_headers_destroy(&self->stream_headers_);
  jxl_token_streams_destroy(&self->tokens_);
  jxl_entropy_encoding_data_destroy(&self->code_);
  jxl_array_destroy(&self->tree_splits_);
  jxl_array_destroy(&self->image_widths_);
}

static inline jxl_context* jxl_modular_frame_encoder_ctx(
    const jxl_modular_frame_encoder* self) {
  return self->ctx_;
}

static inline void jxl_modular_frame_encoder_swap(jxl_modular_frame_encoder* self,
                                    jxl_modular_frame_encoder* other) {
  jxl_array_swap(&self->ac_metadata_size, &other->ac_metadata_size);
  jxl_array_swap(&self->extra_dc_precision, &other->extra_dc_precision);
  jxl_context* tm = self->ctx_;
  self->ctx_ = other->ctx_;
  other->ctx_ = tm;
  jxl_images_swap(&self->stream_images_, &other->stream_images_);
  jxl_array_swap(&self->stream_options_, &other->stream_options_);
  jxl_array_swap(&self->tree_, &other->tree_);
  jxl_token_streams_swap(&self->tree_tokens_, &other->tree_tokens_);
  jxl_group_headers_swap(&self->stream_headers_, &other->stream_headers_);
  jxl_token_streams_swap(&self->tokens_, &other->tokens_);
  jxl_entropy_encoding_data_swap(&self->code_, &other->code_);
  jxl_frame_dimensions td = self->frame_dim_;
  self->frame_dim_ = other->frame_dim_;
  other->frame_dim_ = td;
  jxl_compress_params tc = self->cparams_;
  self->cparams_ = other->cparams_;
  other->cparams_ = tc;
  jxl_array_swap(&self->tree_splits_, &other->tree_splits_);
  jxl_array_swap(&self->image_widths_, &other->image_widths_);
}



#endif  // LIB_JXL_ENC_MODULAR_H_
