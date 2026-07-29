// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

// Library to encode the context map.

#include "enc_context_map.h"

#include <jxl/context.h>
#include "enc_allocator.h"

#include <stddef.h>
#include <stdint.h>

#include "base/array.h"
#include "base/bits.h"
#include "base/enc_status.h"
#include "enc_ans.h"
#include "layer_type.h"
#include "entropy_coder.h"
#include "fields.h"
#include "pack_signed.h"


static size_t jxl_index_of(const jxl_array_u8* v, uint8_t value) {
  size_t i = 0;
  for (; i < jxl_array_len(v); ++i) {
    if (*jxl_array_at_const(v, i) == value) return i;
  }
  return i;
}

static void jxl_move_to_front(jxl_array_u8* v, size_t index) {
  uint8_t value = *jxl_array_at(v, index);
  for (size_t i = index; i != 0; --i) {
    *jxl_array_at(v, i) = *jxl_array_at(v, i - 1);
  }
  *jxl_array_at(v, 0) = value;
}

static jxl_enc_status jxl_move_to_front_transform_body(const jxl_array_u8* v, jxl_array_u8* result,
                                jxl_array_u8* mtf) {
  jxl_array_clear(result);
  if (jxl_array_empty(v)) return jxl_enc_ok_status();
  uint8_t max_value = jxl_u8_max_element(v);
  JXL_RETURN_IF_ERROR(jxl_array_resize_zero(mtf, max_value + 1));
  for (size_t i = 0; i <= max_value; ++i) *jxl_array_at(mtf, i) = (uint8_t)(i);
  JXL_RETURN_IF_ERROR(jxl_array_resize_zero(result, jxl_array_len(v)));
  for (size_t i = 0; i < jxl_array_len(v); ++i) {
    size_t index = jxl_index_of(mtf, *jxl_array_at_const(v, i));
    JXL_DASSERT(index < jxl_array_len(mtf));
    *jxl_array_at(result, i) = (uint8_t)(index);
    jxl_move_to_front(mtf, index);
  }
  return jxl_enc_ok_status();
}

static jxl_enc_status jxl_move_to_front_transform(const jxl_array_u8* v, jxl_array_u8* result) {
  jxl_array_u8 mtf;
  jxl_array_construct_empty(&mtf, result->ctx);
  jxl_enc_status status = jxl_move_to_front_transform_body(v, result, &mtf);
  jxl_array_destroy(&mtf);
  return status;
}

typedef struct jxl_simple_ctx {
  jxl_bit_writer* writer;
  size_t entry_bits;
  const jxl_array_u8* context_map;
} jxl_simple_ctx;

static jxl_enc_status jxl_encode_context_map_simple_body(void* opaque) {
  jxl_simple_ctx* c = (jxl_simple_ctx*)(opaque);
  jxl_bit_writer_write(c->writer, 1, 1);
  jxl_bit_writer_write(c->writer, 2, c->entry_bits);
  for (size_t entry_i = 0; entry_i < jxl_array_len(c->context_map); ++entry_i) {
    uint8_t entry = *jxl_array_at_const(c->context_map, entry_i);
    jxl_bit_writer_write(c->writer, c->entry_bits, entry);
  }
  return jxl_enc_ok_status();
}

typedef struct jxl_ans_ctx {
  jxl_context* ctx;
  jxl_histogram_params* params;
  jxl_token_streams* tokens;
  jxl_bit_writer* writer;
  jxl_layer_type layer;
  bool use_mtf;
} jxl_ans_ctx;

static jxl_enc_status jxl_encode_context_map_ans_body(void* opaque) {
  jxl_ans_ctx* c = (jxl_ans_ctx*)(opaque);
  jxl_bit_writer_write(c->writer, 1, 0);
  jxl_bit_writer_write(c->writer, 1, TO_JXL_BOOL(c->use_mtf));  // Use/don't use MTF.
  jxl_entropy_encoding_data codes;
  jxl_entropy_encoding_data_init(&codes, c->ctx);
  jxl_array_size empty_widths;
  jxl_array_construct_empty(&empty_widths, c->ctx);
  size_t cost;
  jxl_enc_status status = jxl_build_and_encode_histograms(
      c->ctx, c->params, 1, c->tokens, &codes, c->writer, c->layer,
      &empty_widths, &cost);
  (void)cost;
  if (jxl_enc_status_ok(status)) {
    jxl_write_tokens_with_allotment(jxl_token_streams_at(c->tokens, 0), &codes, 0, c->writer);
  }
  jxl_array_destroy(&empty_widths);
  jxl_entropy_encoding_data_destroy(&codes);
  return status;
}

typedef struct jxl_block_ctx {
  jxl_bit_writer* writer;
  const jxl_array_int* dct;
  const jxl_array_u32* qft;
  const jxl_array_u8* ctx_map;
  size_t num_ctxs;
} jxl_block_ctx;

static jxl_enc_status jxl_encode_block_ctx_map_body(void* opaque) {
  jxl_block_ctx* c = (jxl_block_ctx*)(opaque);
  if (jxl_array_empty(&c->dct[0]) && jxl_array_empty(&c->dct[1]) && jxl_array_empty(&c->dct[2]) &&
      jxl_array_empty(c->qft) && jxl_array_len(c->ctx_map) == 21 &&
      jxl_u8_array_equal(c->ctx_map, kBlockCtxMapDefault,
                   sizeof(kBlockCtxMapDefault) /
                       sizeof(kBlockCtxMapDefault[0]))) {
    jxl_bit_writer_write(c->writer, 1, 1);  // default
    return jxl_enc_ok_status();
  }
  jxl_bit_writer_write(c->writer, 1, 0);
  static const int kAxes[] = {0, 1, 2};
  for (size_t j_i = 0; j_i < 3; ++j_i) {
    int j = kAxes[j_i];
    jxl_bit_writer_write(c->writer, 4, jxl_array_len(&c->dct[j]));
    for (size_t i_i = 0; i_i < jxl_array_len(&c->dct[j]); ++i_i) {
      int i = *jxl_array_at_const(&c->dct[j], i_i);
      JXL_RETURN_IF_ERROR(
          jxl_u32_coder_write(jxl_dc_threshold_dist(), jxl_pack_signed(i), c->writer));
    }
  }
  jxl_bit_writer_write(c->writer, 4, jxl_array_len(c->qft));
  for (size_t i_i = 0; i_i < jxl_array_len(c->qft); ++i_i) {
    uint32_t i = *jxl_array_at_const(c->qft, i_i);
    JXL_RETURN_IF_ERROR(jxl_u32_coder_write(jxl_qf_threshold_dist(), i - 1, c->writer));
  }
  JXL_RETURN_IF_ERROR(
      jxl_encode_context_map(c->ctx_map, c->num_ctxs, c->writer, kLayerAc));
  return jxl_enc_ok_status();
}

jxl_enc_status jxl_encode_context_map_body_inner(const jxl_array_u8* context_map,
                                 size_t num_histograms, jxl_bit_writer* writer,
                                 jxl_layer_type layer, jxl_token_streams* tokens,
                                 jxl_token_streams* mtf_tokens,
                                 jxl_array_u8* transformed_symbols,
                                 jxl_array_size* empty_widths) {
  jxl_context* ctx = jxl_bit_writer_ctx(writer);
  JXL_RETURN_IF_ERROR(jxl_move_to_front_transform(context_map, transformed_symbols));
  for (size_t ctx_i = 0; ctx_i < jxl_array_len(context_map); ++ctx_i) {
if (!jxl_enc_status_ok(jxl_array_token_push_back(jxl_token_streams_at(tokens, 0), jxl_token_make(0, *jxl_array_at_const(context_map, ctx_i))))) JXL_CRASH();
}
  for (size_t sym_i = 0; sym_i < jxl_array_len(transformed_symbols); ++sym_i) {
if (!jxl_enc_status_ok(jxl_array_token_push_back(jxl_token_streams_at(mtf_tokens, 0), jxl_token_make(0, *jxl_array_at(transformed_symbols, sym_i))))) JXL_CRASH();
}
  jxl_histogram_params params;
  jxl_histogram_params_construct_empty(&params);
  params.uint_method = kHybridUintContextMap;
  size_t ans_cost;
  size_t mtf_cost;
  {
    jxl_entropy_encoding_data codes;
    jxl_entropy_encoding_data_init(&codes, ctx);
    jxl_enc_status hist_status = jxl_build_and_encode_histograms(
        ctx, &params, 1, tokens, &codes, NULL, kLayerHeader,
        empty_widths, &ans_cost);
    jxl_entropy_encoding_data_destroy(&codes);
    JXL_RETURN_IF_ERROR(hist_status);
  }
  {
    jxl_entropy_encoding_data codes;
    jxl_entropy_encoding_data_init(&codes, ctx);
    jxl_enc_status hist_status = jxl_build_and_encode_histograms(
        ctx, &params, 1, mtf_tokens, &codes, NULL, kLayerHeader,
        empty_widths, &mtf_cost);
    jxl_entropy_encoding_data_destroy(&codes);
    JXL_RETURN_IF_ERROR(hist_status);
  }
  bool use_mtf = mtf_cost < ans_cost;
  // Rebuild token list.
jxl_array_clear(jxl_token_streams_at(tokens, 0));
  for (size_t i = 0; i < jxl_array_len(transformed_symbols); i++) {
    if (!jxl_enc_status_ok(jxl_array_token_push_back(jxl_token_streams_at(tokens, 0),
                       jxl_token_make(0, use_mtf ? *jxl_array_at(transformed_symbols, i)
                                        : *jxl_array_at_const(context_map, i))))) {
      JXL_CRASH();
    }
  }
  size_t entry_bits = jxl_ceil_log2_nonzero64(num_histograms);
  size_t simple_cost = entry_bits * jxl_array_len(context_map);
  if (entry_bits < 4 && simple_cost < ans_cost && simple_cost < mtf_cost) {
    jxl_simple_ctx simple_ctx = {writer, entry_bits, context_map};
    JXL_RETURN_IF_ERROR(jxl_bit_writer_with_max_bits(writer, 
        3 + entry_bits * jxl_array_len(context_map), layer, jxl_encode_context_map_simple_body,
        &simple_ctx));
  } else {
    jxl_ans_ctx ans_ctx = {ctx, &params, tokens, writer, layer, use_mtf};
    JXL_RETURN_IF_ERROR(jxl_bit_writer_with_max_bits(writer, 
        2 + jxl_array_len(jxl_token_streams_at(tokens, 0)) * 24, layer, jxl_encode_context_map_ans_body,
        &ans_ctx));
  }
  return jxl_enc_ok_status();
}

jxl_enc_status jxl_encode_context_map_body(const jxl_array_u8* context_map,
                            size_t num_histograms, jxl_bit_writer* writer,
                            jxl_layer_type layer, jxl_token_streams* tokens,
                            jxl_token_streams* mtf_tokens) {
  jxl_context* mm = jxl_bit_writer_ctx(writer);
  jxl_array_u8 transformed_symbols;
  jxl_array_construct_empty(&transformed_symbols, mm);
  jxl_array_size empty_widths;
  jxl_array_construct_empty(&empty_widths, mm);
  jxl_enc_status status = jxl_encode_context_map_body_inner(
      context_map, num_histograms, writer, layer, tokens, mtf_tokens,
      &transformed_symbols, &empty_widths);
  jxl_array_destroy(&transformed_symbols);
  jxl_array_destroy(&empty_widths);
  return status;
}

jxl_enc_status jxl_encode_context_map(const jxl_array_u8* context_map,
                        size_t num_histograms, jxl_bit_writer* writer,
                        jxl_layer_type layer){
  if (num_histograms == 1) {
    // Simple code
    jxl_bit_writer_write(writer, 1, 1);
    // 0 bits per entry.
    jxl_bit_writer_write(writer, 2, 0);
    return jxl_enc_ok_status();
  }

  jxl_context* mm = jxl_bit_writer_ctx(writer);
  jxl_token_streams tokens;
  jxl_token_streams_create(&tokens, 1, mm);
  jxl_token_streams mtf_tokens;
  jxl_token_streams_create(&mtf_tokens, 1, mm);
  jxl_enc_status status =
      jxl_encode_context_map_body(context_map, num_histograms, writer, layer, &tokens,
                           &mtf_tokens);
  jxl_token_streams_destroy(&tokens);
  jxl_token_streams_destroy(&mtf_tokens);
  return status;
}

jxl_enc_status jxl_encode_block_ctx_map(const jxl_block_ctx_map* block_ctx_map, jxl_bit_writer* writer){
  const jxl_array_int* dct = block_ctx_map->dc_thresholds;
  const jxl_array_u32* qft = &block_ctx_map->qf_thresholds;
  const jxl_array_u8* ctx_map = &block_ctx_map->ctx_map;
  jxl_block_ctx ctx = {writer, dct, qft, ctx_map, block_ctx_map->num_ctxs};
  return jxl_bit_writer_with_max_bits(writer, 
      (jxl_array_len(&dct[0]) + jxl_array_len(&dct[1]) + jxl_array_len(&dct[2]) + jxl_array_len(qft)) * 34 + 1 +
          4 + 4 + jxl_array_len(ctx_map) * 10 + 1024,
      kLayerAc, jxl_encode_block_ctx_map_body, &ctx);
}
