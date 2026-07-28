// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "lib/jxl/enc_modular.h"

#include <jxl/context.h>
#include "lib/jxl/allocator.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "lib/jxl/ac_strategy.h"
#include "lib/jxl/base/array.h"
#include "lib/jxl/base/printf_macros.h"
#include "lib/jxl/base/status.h"
#include "lib/jxl/compressed_dc.h"
#include "lib/jxl/layer_type.h"
#include "lib/jxl/fields.h"
#include "lib/jxl/modular/encoding/enc_encoding.h"
#include "lib/jxl/modular/encoding/enc_ma.h"
#include "lib/jxl/modular/encoding/ma_common.h"
#include "lib/jxl/base/common.h"
#include "lib/jxl/base/compiler_specific.h"


static jxl_histogram_params jxl_modular_histogram_params(
    const jxl_compress_params* cparams,
    const jxl_array_u8* extra_dc_precision) {
  jxl_histogram_params params;
  jxl_histogram_params_construct_empty(&params);
  if (cparams->speed_tier > kKitten) {
    params.clustering = kClusteringFast;
    params.ans_histogram_strategy =
        cparams->speed_tier > kThunder
            ? kANSHistFast
            : kANSHistApproximate;
    params.lz77_method = kLZ77None;
    if (!jxl_array_empty(extra_dc_precision) && *jxl_array_at_const(extra_dc_precision, 0) != 0) {
      params.uint_method = kHybridUintFast;
    } else {
      params.uint_method = kHybridUintNone;
    }
  } else if (cparams->speed_tier <= kTortoise) {
    params.lz77_method = kLZ77Optimal;
  } else {
    params.lz77_method = kLZ77;
  }
  return params;
}

static jxl_status jxl_merge_trees(const jxl_tree* trees, size_t num_trees,
                  const jxl_array_size* tree_splits, size_t begin, size_t end,
                  jxl_tree* tree) {
  JXL_ENSURE(num_trees + 1 == jxl_array_len(tree_splits));
  JXL_ENSURE(end > begin);
  JXL_ENSURE(end <= num_trees);
  if (end == begin + 1) {
    size_t sz = jxl_array_len(tree);
    JXL_RETURN_IF_ERROR(
        jxl_array_append(tree, jxl_array_data_const(&trees[begin]), jxl_array_len(&trees[begin])));
    for (size_t i = sz; i < jxl_array_len(tree); i++) {
      jxl_array_at(tree, i)->lchild += sz;
      jxl_array_at(tree, i)->rchild += sz;
    }
    return jxl_ok_status();
  }
  size_t mid = (begin + end) / 2;
  size_t splitval = *jxl_array_at_const(tree_splits, mid) - 1;
  size_t cur = jxl_array_len(tree);
  if (!jxl_status_ok(jxl_array_property_decision_node_push_back(tree, jxl_property_decision_node_split(
                              1 /*stream_id*/, (int)(splitval), 0, 0)))) {
    JXL_CRASH();
  }
  jxl_array_at(tree, cur)->lchild = jxl_array_len(tree);
  JXL_RETURN_IF_ERROR(jxl_merge_trees(trees, num_trees, tree_splits, mid, end, tree));
  jxl_array_at(tree, cur)->rchild = jxl_array_len(tree);
  JXL_RETURN_IF_ERROR(
      jxl_merge_trees(trees, num_trees, tree_splits, begin, mid, tree));
  return jxl_ok_status();
}

static jxl_status jxl_set_splitting_heuristic_props(jxl_modular_options* options,
                                  const jxl_array_u32* prop_order,
                                  size_t n) {
  if (n > kMaxSplittingHeuristicsProperties) {
    return JXL_FAILURE("Too many splitting heuristic properties");
  }
  options->num_splitting_heuristics_properties = n;
  for (size_t i = 0; i < n; ++i) {
    options->splitting_heuristics_properties[i] = *jxl_array_at_const(prop_order, i);
  }
  return jxl_ok_status();
}

typedef struct jxl_tree_flag_ctx {
  jxl_bit_writer* writer;
  bool* skip_rest;
  bool tree_empty;
} jxl_tree_flag_ctx;

static jxl_status jxl_write_tree_flag_body(void* opaque) {
  jxl_tree_flag_ctx* c = (jxl_tree_flag_ctx*)(opaque);
  // If we are using brotli, or not using modular mode.
  if (c->tree_empty) {
    jxl_bit_writer_write(c->writer, 1, 0);
    *c->skip_rest = true;
  } else {
    jxl_bit_writer_write(c->writer, 1, 1);
  }
  return jxl_ok_status();
}

jxl_status jxl_modular_frame_encoder_create(jxl_context* ctx,
                                   const jxl_frame_header* frame_header,
                                   const jxl_compress_params* cparams_orig,
                                   jxl_modular_frame_encoder* out){
  jxl_modular_frame_encoder self;
  jxl_modular_frame_encoder_init_mm(&self, ctx);
  jxl_status status =
      jxl_modular_frame_encoder_init(&self, frame_header, cparams_orig);
  if (!jxl_status_ok(status)) {
    jxl_modular_frame_encoder_destroy(&self);
    return status;
  }
  jxl_modular_frame_encoder_swap(out, &self);
  jxl_modular_frame_encoder_destroy(&self);
  return jxl_ok_status();
}

jxl_status jxl_modular_frame_encoder_init(jxl_modular_frame_encoder* self, const jxl_frame_header* frame_header,
                                 const jxl_compress_params* cparams_orig){
  self->frame_dim_ = jxl_frame_header_to_frame_dimensions(frame_header);
  self->cparams_ = *cparams_orig;

  size_t num_streams =
      jxl_modular_stream_id_num(&self->frame_dim_, frame_header->passes.num_passes);

  for (size_t i = 0; i < num_streams; ++i) {
    JXL_RETURN_IF_ERROR(jxl_images_emplace_back(&self->stream_images_, self->ctx_));
  }

  self->cparams_.options.splitting_heuristics_node_threshold =
      75 + 14 * (int)(self->cparams_.speed_tier);

  {
    static const uint32_t kPropOrder[] = {0, 1, 15, 9, 10, 11, 12, 13, 14,
                                         2, 3, 4,  5, 6,  7,  8};
    jxl_array_u32 prop_order;
    jxl_array_construct_empty(&prop_order, self->ctx_);
    if (!jxl_status_ok(jxl_array_assign(&prop_order, kPropOrder,
                     sizeof(kPropOrder) / sizeof(kPropOrder[0])))) {
      jxl_array_destroy(&prop_order);
      return JXL_FAILURE("OOM");
    }
    if (num_streams < 30 && self->cparams_.speed_tier > kTortoise) {
      jxl_array_erase(&prop_order, jxl_array_data(&prop_order) + 1, jxl_array_data(&prop_order) + 2);
    }
    jxl_status prop_status = jxl_ok_status();
    switch (self->cparams_.speed_tier) {
      case kHare:
        prop_status =
            jxl_set_splitting_heuristic_props(&self->cparams_.options, &prop_order, 4);
        self->cparams_.options.max_property_values = 48;
        self->cparams_.options.nb_repeats *= 0.5f;
        break;
      case kWombat:
        prop_status =
            jxl_set_splitting_heuristic_props(&self->cparams_.options, &prop_order, 5);
        self->cparams_.options.max_property_values = 64;
        self->cparams_.options.nb_repeats *= 0.7f;
        break;
      case kSquirrel:
        prop_status =
            jxl_set_splitting_heuristic_props(&self->cparams_.options, &prop_order, 7);
        self->cparams_.options.max_property_values = 96;
        break;
      case kKitten:
        prop_status =
            jxl_set_splitting_heuristic_props(&self->cparams_.options, &prop_order, 10);
        self->cparams_.options.max_property_values = 128;
        self->cparams_.options.nb_repeats *= 1.1f;
        break;
      case kGlacier:
      case kTortoise:
        prop_status = jxl_set_splitting_heuristic_props(
            &self->cparams_.options, &prop_order, jxl_array_len(&prop_order));
        self->cparams_.options.max_property_values = 256;
        self->cparams_.options.nb_repeats *= 1.3f;
        break;
      default:
        prop_status =
            jxl_set_splitting_heuristic_props(&self->cparams_.options, &prop_order, 3);
        self->cparams_.options.max_property_values = 32;
        self->cparams_.options.nb_repeats *= 0.3f;
        break;
    }
    jxl_array_destroy(&prop_order);
    if (!jxl_status_ok(prop_status)) return prop_status;
  }
  self->cparams_.options.nb_repeats = JXL_MIN(1.0f, self->cparams_.options.nb_repeats);

  if (self->cparams_.options.predictor == kUndefinedPredictor) {
    self->cparams_.options.predictor = kPredictorGradient;
  }

  JXL_RETURN_IF_ERROR(jxl_array_size_push_back(&self->tree_splits_, (size_t)(0)));
  {
    jxl_modular_stream_id qt0 = jxl_modular_stream_id_quant_table(0);
    self->cparams_.options.fast_decode_multiplier = 1.0f;
    JXL_RETURN_IF_ERROR(jxl_array_size_push_back(
        &self->tree_splits_, jxl_modular_stream_id_id(jxl_modular_stream_id_var_dctdc(0), &self->frame_dim_)));
    JXL_RETURN_IF_ERROR(jxl_array_size_push_back(
        &self->tree_splits_, jxl_modular_stream_id_id(jxl_modular_stream_id_ac_metadata(0), &self->frame_dim_)));
    JXL_RETURN_IF_ERROR(jxl_array_size_push_back(&self->tree_splits_, jxl_modular_stream_id_id(qt0, &self->frame_dim_)));
    JXL_RETURN_IF_ERROR(jxl_array_resize_zero(&self->ac_metadata_size, self->frame_dim_.num_dc_groups));
    JXL_RETURN_IF_ERROR(jxl_array_resize_zero(&self->extra_dc_precision, self->frame_dim_.num_dc_groups));
  }
  JXL_RETURN_IF_ERROR(jxl_array_size_push_back(&self->tree_splits_, num_streams));
  self->cparams_.options.max_chan_size = self->frame_dim_.group_dim;

  JXL_RETURN_IF_ERROR(jxl_array_modular_options_resize_fill(&self->stream_options_, num_streams,
                                      self->cparams_.options));

  *jxl_array_at(&self->stream_options_, 0) = self->cparams_.options;
  if (self->cparams_.speed_tier == kFalcon) {
    jxl_array_at(&self->stream_options_, 0)->tree_kind = kWPFixedDC;
  } else if (self->cparams_.speed_tier == kThunder) {
    jxl_array_at(&self->stream_options_, 0)->tree_kind = kGradientFixedDC;
  }
  jxl_array_u8 empty_extra_dc_precision;
  jxl_array_construct_empty(&empty_extra_dc_precision, self->ctx_);
  jxl_array_at(&self->stream_options_, 0)->histogram_params =
      jxl_modular_histogram_params(&self->cparams_, &empty_extra_dc_precision);
  jxl_array_destroy(&empty_extra_dc_precision);
  return jxl_ok_status();
}

jxl_status jxl_modular_frame_encoder_compute_tree(jxl_modular_frame_encoder* self) {
  // Avoid creating a tree with leaves that don't correspond to any pixels.
  jxl_array_size useful_splits;
  jxl_array_construct_empty(&useful_splits, self->ctx_);
  jxl_status status =
      jxl_array_reserve(&useful_splits, jxl_array_len(&self->tree_splits_));
  if (!jxl_status_ok(status)) {
    jxl_array_destroy(&useful_splits);
    return status;
  }
  for (size_t chunk = 0; chunk < jxl_array_len(&self->tree_splits_) - 1; chunk++) {
    bool has_pixels = false;
    size_t start = *jxl_array_at(&self->tree_splits_, chunk);
    size_t stop = *jxl_array_at(&self->tree_splits_, chunk + 1);
    for (size_t i = start; i < stop; i++) {
      if (!jxl_image_empty(jxl_images_at(&self->stream_images_, i))) has_pixels = true;
    }
    if (has_pixels) {
      status = jxl_array_size_push_back(&useful_splits, *jxl_array_at(&self->tree_splits_, chunk));
      if (!jxl_status_ok(status)) {
        jxl_array_destroy(&useful_splits);
        return status;
      }
    }
  }
  if (jxl_array_empty(&useful_splits)) {
    jxl_array_destroy(&useful_splits);
    return jxl_ok_status();
  }
  status = jxl_array_size_push_back(&useful_splits, jxl_array_back(&self->tree_splits_));
  if (!jxl_status_ok(status)) {
    jxl_array_destroy(&useful_splits);
    return status;
  }

  jxl_trees trees;
  jxl_trees_create(&trees, jxl_array_len(&useful_splits) - 1,
                   self->ctx_);
  for (uint32_t chunk = 0; chunk < jxl_array_len(&useful_splits) - 1; ++chunk) {
    uint32_t start = *jxl_array_at(&useful_splits, chunk);
    uint32_t stop = *jxl_array_at(&useful_splits, chunk + 1);
    while (start < stop && jxl_image_empty(jxl_images_at(&self->stream_images_, start)))
      ++start;
    while (start < stop &&
           jxl_image_empty(jxl_images_at(&self->stream_images_, stop - 1)))
      --stop;

    if (jxl_array_at(&self->stream_options_, start)->tree_kind == kLearn) {
      jxl_array_modular_multiplier_info empty_multiplier_info;
      jxl_array_construct_empty(&empty_multiplier_info, self->ctx_);
      status = jxl_learn_tree(
          jxl_images_data(&self->stream_images_), jxl_array_data(&self->stream_options_),
          start, stop, &empty_multiplier_info, jxl_trees_at(&trees, chunk));
      jxl_array_destroy(&empty_multiplier_info);
      if (!jxl_status_ok(status)) {
        jxl_trees_destroy(&trees);
        jxl_array_destroy(&useful_splits);
        return status;
      }
    } else {
      size_t total_pixels = 0;
      for (size_t i = start; i < stop; i++) {
        for (size_t ch_i = 0;
             ch_i < jxl_channels_size(&jxl_images_at(&self->stream_images_, i)->channel);
             ++ch_i) {
          const jxl_channel* ch =
              jxl_channels_at(&jxl_images_at(&self->stream_images_, i)->channel, ch_i);
          total_pixels += ch->w * ch->h;
        }
      }
      total_pixels = JXL_MAX(total_pixels, (size_t)(1));

      jxl_predefined_tree(jxl_array_at(&self->stream_options_, start)->tree_kind,
                     total_pixels, 8, jxl_trees_at(&trees, chunk));
    }
  }
  jxl_array_clear(&self->tree_);
  status = jxl_merge_trees(jxl_trees_data(&trees), jxl_trees_size(&trees),
                             &useful_splits, 0,
                             jxl_array_len(&useful_splits) - 1, &self->tree_);
  if (!jxl_status_ok(status)) {
    jxl_trees_destroy(&trees);
    jxl_array_destroy(&useful_splits);
    return status;
  }
  status = jxl_token_streams_resize(&self->tree_tokens_, 1);
  if (!jxl_status_ok(status)) {
    jxl_trees_destroy(&trees);
    jxl_array_destroy(&useful_splits);
    return status;
  }
  jxl_array_clear(jxl_token_streams_at(&self->tree_tokens_, 0));
  jxl_tree decoded_tree;
  jxl_array_construct_empty(&decoded_tree, self->ctx_);
  status = jxl_tokenize_tree(&self->tree_,
                        jxl_token_streams_data(&self->tree_tokens_), &decoded_tree);
  if (!jxl_status_ok(status)) {
    jxl_array_destroy(&decoded_tree);
    jxl_trees_destroy(&trees);
    jxl_array_destroy(&useful_splits);
    return status;
  }
  if (jxl_array_len(&self->tree_) != jxl_array_len(&decoded_tree)) {
    jxl_array_destroy(&decoded_tree);
    jxl_trees_destroy(&trees);
    jxl_array_destroy(&useful_splits);
    return JXL_FAILURE("jxl_tree tokenization changed node count");
  }
  jxl_array_swap(&self->tree_, &decoded_tree);
  // Destroy emptied decoded_tree after swap.
  jxl_array_destroy(&decoded_tree);
  jxl_trees_destroy(&trees);
  jxl_array_destroy(&useful_splits);
  return jxl_ok_status();
}

jxl_status jxl_modular_frame_encoder_compute_tokens(jxl_modular_frame_encoder* self) {
  size_t num_streams = jxl_images_size(&self->stream_images_);
  JXL_RETURN_IF_ERROR(jxl_group_headers_resize(&self->stream_headers_, num_streams));
  JXL_RETURN_IF_ERROR(jxl_token_streams_resize(&self->tokens_, num_streams));
  JXL_RETURN_IF_ERROR(jxl_array_resize_zero(&self->image_widths_, num_streams));
  for (uint32_t stream_id = 0; stream_id < num_streams; ++stream_id) {
jxl_array_clear(jxl_token_streams_at(&self->tokens_, stream_id));
    JXL_RETURN_IF_ERROR(
        jxl_modular_compress(jxl_images_at(&self->stream_images_, stream_id), jxl_array_at(&self->stream_options_, stream_id),
                        stream_id, &self->tree_, jxl_group_headers_at(&self->stream_headers_, stream_id),
                        jxl_token_streams_at(&self->tokens_, stream_id), jxl_array_at(&self->image_widths_, stream_id)));
  }
  return jxl_ok_status();
}

jxl_status jxl_modular_frame_encoder_encode_global_info(jxl_modular_frame_encoder* self, jxl_bit_writer* writer) {
  jxl_context* ctx = jxl_bit_writer_ctx(writer);
  bool skip_rest = false;
  jxl_tree_flag_ctx tree_ctx = {writer, &skip_rest,
                          jxl_token_streams_empty(&self->tree_tokens_) || jxl_array_empty(jxl_token_streams_at(&self->tree_tokens_, 0))};
  JXL_RETURN_IF_ERROR(jxl_bit_writer_with_max_bits(writer, 1, kLayerModularTree,
                                          jxl_write_tree_flag_body, &tree_ctx));
  if (skip_rest) return jxl_ok_status();

  // Write tree
  jxl_histogram_params params =
      jxl_modular_histogram_params(&self->cparams_, &self->extra_dc_precision);
  {
    jxl_entropy_encoding_data tree_code;
    jxl_entropy_encoding_data_init(&tree_code, ctx);
    jxl_array_size empty_widths;
    jxl_array_construct_empty(&empty_widths, ctx);
    size_t cost;
    jxl_status tree_status = jxl_build_and_encode_histograms(
        ctx, &params, kNumTreeContexts, &self->tree_tokens_, &tree_code,
        writer, kLayerModularTree, &empty_widths, &cost);
    if (!jxl_status_ok(tree_status)) {
      jxl_array_destroy(&empty_widths);
      jxl_entropy_encoding_data_destroy(&tree_code);
      return tree_status;
    }
    (void)cost;
    tree_status = jxl_write_tokens(jxl_token_streams_at(&self->tree_tokens_, 0), &tree_code, 0, writer,
                                    kLayerModularTree);
    jxl_array_destroy(&empty_widths);
    jxl_entropy_encoding_data_destroy(&tree_code);
    if (!jxl_status_ok(tree_status)) return tree_status;
  }
  params = jxl_modular_histogram_params(&self->cparams_, &self->extra_dc_precision);
  // Write histograms.
  size_t cost;
  JXL_RETURN_IF_ERROR(jxl_build_and_encode_histograms(
      ctx, &params, (jxl_array_len(&self->tree_) + 1) / 2, &self->tokens_, &self->code_, writer,
      kLayerModularGlobal, &self->image_widths_, &cost));
  (void)cost;
  return jxl_ok_status();
}

jxl_status jxl_modular_frame_encoder_encode_stream(jxl_modular_frame_encoder* self, jxl_bit_writer* writer,
                                         jxl_layer_type layer,
                                         const jxl_modular_stream_id* stream){
  size_t stream_id = jxl_modular_stream_id_id(*stream, &self->frame_dim_);
  if (jxl_channels_empty(&jxl_images_at(&self->stream_images_, stream_id)->channel)) {
    JXL_DEBUG_V(10, "Modular stream %" jxl_pr_iu_s " is empty.", stream_id);
    return jxl_ok_status();  // jxl_image with no channels, header never gets decoded.
  }
  JXL_RETURN_IF_ERROR(
      jxl_bundle_write(&jxl_group_headers_at(&self->stream_headers_, stream_id)->fields, writer, layer));
  JXL_RETURN_IF_ERROR(
      jxl_write_tokens(jxl_token_streams_at(&self->tokens_, stream_id), &self->code_, 0, writer, layer));
  return jxl_ok_status();
}

jxl_status jxl_modular_frame_encoder_add_var_dctdc(jxl_modular_frame_encoder* self, const jxl_frame_header* frame_header,
                                        const jxl_image3_f* dc, const jxl_rect* r,
                                        size_t group_index,
                                        jxl_passes_encoder_state* enc_state){
  jxl_context* ctx = jxl_image3_f_ctx(dc);
  *jxl_array_at(&self->extra_dc_precision, group_index) = 0;

  size_t stream_id = jxl_modular_stream_id_id(jxl_modular_stream_id_var_dctdc(group_index), &self->frame_dim_);
  jxl_array_at(&self->stream_options_, stream_id)->max_chan_size = 0xFFFFFF;
  jxl_array_at(&self->stream_options_, stream_id)->predictor = kPredictorWeighted;
  jxl_array_at(&self->stream_options_, stream_id)->wp_tree_mode = kWPOnly;
  if (self->cparams_.speed_tier >= kSquirrel) {
    jxl_array_at(&self->stream_options_, stream_id)->tree_kind = kWPFixedDC;
  }
  if (self->cparams_.speed_tier < kSquirrel) {
    jxl_array_at(&self->stream_options_, stream_id)->predictor =
        (self->cparams_.speed_tier < kKitten ? kPredictorVariable
                                                  : kPredictorBest);
    jxl_array_at(&self->stream_options_, stream_id)->wp_tree_mode =
        kDefault;
    jxl_array_at(&self->stream_options_, stream_id)->tree_kind = kLearn;
  }
  jxl_array_at(&self->stream_options_, stream_id)->histogram_params =
      jxl_array_at(&self->stream_options_, 0)->histogram_params;

  JXL_RETURN_IF_ERROR(jxl_image_create(ctx, jxl_rect_x_size(r), jxl_rect_y_size(r), 8, 3,
                                    jxl_images_at(&self->stream_images_, stream_id)));
  const jxl_color_correlation* color_correlation = jxl_color_correlation_map_base(&enc_state->shared.cmap);
  if (jxl_y_cb_cr_chroma_subsampling_is444(&frame_header->chroma_subsampling)) {
    static const size_t kChans[] = {1, 0, 2};
    for (size_t c_i = 0; c_i < 3; ++c_i) {
      size_t c = kChans[c_i];
      float inv_factor = jxl_quantizer_get_inv_dc_step(&enc_state->shared.quantizer, c);
      float y_factor = jxl_quantizer_get_dc_step(&enc_state->shared.quantizer, 1);
      float cfl_factor = jxl_color_correlation_dc_factors(color_correlation)[c];
      for (size_t y = 0; y < jxl_rect_y_size(r); y++) {
        int32_t* quant_row =
            jxl_image_i_row(&jxl_channels_at(&jxl_images_at(&self->stream_images_, stream_id)->channel, c < 2 ? c ^ 1 : c)->plane, y);
        const float* row = jxl_rect_const_plane_row(r, dc, c, y);
        if (c == 1) {
          for (size_t x = 0; x < jxl_rect_x_size(r); x++) {
            quant_row[x] = round(row[x] * inv_factor);
          }
        } else {
          int32_t* quant_row_y =
              jxl_image_i_row(&jxl_channels_at(&jxl_images_at(&self->stream_images_, stream_id)->channel, 0)->plane, y);
          for (size_t x = 0; x < jxl_rect_x_size(r); x++) {
            quant_row[x] =
                round((row[x] - quant_row_y[x] * (y_factor * cfl_factor)) *
                           inv_factor);
          }
        }
      }
    }
  } else {
    static const size_t kChans[] = {1, 0, 2};
    for (size_t c_i = 0; c_i < 3; ++c_i) {
      size_t c = kChans[c_i];
      jxl_rect rect = jxl_rect_make(
          jxl_rect_x0(r) >> jxl_y_cb_cr_chroma_subsampling_h_shift(&frame_header->chroma_subsampling, c),
          jxl_rect_y0(r) >> jxl_y_cb_cr_chroma_subsampling_v_shift(&frame_header->chroma_subsampling, c),
          jxl_rect_x_size(r) >> jxl_y_cb_cr_chroma_subsampling_h_shift(&frame_header->chroma_subsampling, c),
          jxl_rect_y_size(r) >> jxl_y_cb_cr_chroma_subsampling_v_shift(&frame_header->chroma_subsampling, c));
      float inv_factor = jxl_quantizer_get_inv_dc_step(&enc_state->shared.quantizer, c);
      size_t ys = jxl_rect_y_size(&rect);
      size_t xs = jxl_rect_x_size(&rect);
      jxl_channel* ch = jxl_channels_at(&jxl_images_at(&self->stream_images_, stream_id)->channel, c < 2 ? c ^ 1 : c);
      ch->w = xs;
      ch->h = ys;
      JXL_RETURN_IF_ERROR(jxl_channel_shrink(ch));
      for (size_t y = 0; y < ys; y++) {
        int32_t* quant_row = jxl_image_i_row(&ch->plane, y);
        const float* row = jxl_rect_const_plane_row(&rect, dc, c, y);
        for (size_t x = 0; x < xs; x++) {
          quant_row[x] = round(row[x] * inv_factor);
        }
      }
    }
  }

  jxl_fill_quant_dc(r, &enc_state->shared.quant_dc, jxl_images_at(&self->stream_images_, stream_id),
              &frame_header->chroma_subsampling, &enc_state->shared.block_ctx_map);
  return jxl_ok_status();
}

jxl_status jxl_modular_frame_encoder_add_ac_metadata(jxl_modular_frame_encoder* self, const jxl_rect* r, size_t group_index,
                                          jxl_passes_encoder_state* enc_state){
  jxl_context* ctx = jxl_passes_encoder_state_ctx(enc_state);
  size_t stream_id = jxl_modular_stream_id_id(jxl_modular_stream_id_ac_metadata(group_index), &self->frame_dim_);
  jxl_array_at(&self->stream_options_, stream_id)->max_chan_size = 0xFFFFFF;
  if (jxl_array_at(&self->stream_options_, stream_id)->predictor != kPredictorWeighted) {
    jxl_array_at(&self->stream_options_, stream_id)->wp_tree_mode = kNoWP;
  }
  jxl_array_at(&self->stream_options_, stream_id)->tree_kind =
      kJpegTranscodeACMeta;
  if (self->cparams_.speed_tier < kSquirrel &&
      self->cparams_.force_cfl_jpeg_recompression) {
    jxl_array_at(&self->stream_options_, stream_id)->tree_kind = kLearn;
  }
  jxl_array_at(&self->stream_options_, stream_id)->histogram_params =
      jxl_array_at(&self->stream_options_, 0)->histogram_params;
  // YToX, YToB, ACS + QF, EPF
  jxl_image* image = jxl_images_at(&self->stream_images_, stream_id);
  JXL_RETURN_IF_ERROR(
      jxl_image_create(ctx, jxl_rect_x_size(r), jxl_rect_y_size(r), 8, 4, image));
  JXL_STATIC_ASSERT(kColorTileDimInBlocks == 8, "jxl_color tile size changed");
  jxl_rect cr = jxl_rect_make(jxl_rect_x0(r) >> 3, jxl_rect_y0(r) >> 3, (jxl_rect_x_size(r) + 7) >> 3,
                     (jxl_rect_y_size(r) + 7) >> 3);
  JXL_RETURN_IF_ERROR(jxl_channel_create(ctx, jxl_rect_x_size(&cr), jxl_rect_y_size(&cr), 3,
                                      3, jxl_channels_at(&image->channel, 0)));
  JXL_RETURN_IF_ERROR(jxl_channel_create(ctx, jxl_rect_x_size(&cr), jxl_rect_y_size(&cr), 3,
                                      3, jxl_channels_at(&image->channel, 1)));
  JXL_RETURN_IF_ERROR(jxl_channel_create(ctx, jxl_rect_x_size(r) * jxl_rect_y_size(r), 2,
                                      0, 0, jxl_channels_at(&image->channel, 2)));
  {
    jxl_rect to0 = jxl_rect_from_size(jxl_image_i_x_size(&jxl_channels_at(&image->channel, 0)->plane),
                            jxl_image_i_y_size(&jxl_channels_at(&image->channel, 0)->plane));
    JXL_RETURN_IF_ERROR(jxl_convert_plane_and_clamp(
        &cr, &enc_state->shared.cmap.ytox_map, &to0,
        &jxl_channels_at(&image->channel, 0)->plane));
  }
  {
    jxl_rect to1 = jxl_rect_from_size(jxl_image_i_x_size(&jxl_channels_at(&image->channel, 1)->plane),
                            jxl_image_i_y_size(&jxl_channels_at(&image->channel, 1)->plane));
    JXL_RETURN_IF_ERROR(jxl_convert_plane_and_clamp(
        &cr, &enc_state->shared.cmap.ytob_map, &to1,
        &jxl_channels_at(&image->channel, 1)->plane));
  }
  size_t num = 0;
  for (size_t y = 0; y < jxl_rect_y_size(r); y++) {
    jxl_ac_strategy_row row_acs = jxl_ac_strategy_image_const_row_rect(&enc_state->shared.ac_strategy, r, y);
    const int32_t* row_qf =
        jxl_rect_const_row_i(r, &enc_state->shared.raw_quant_field, y);
    const uint8_t* row_epf =
        jxl_rect_const_row_b(r, &enc_state->shared.epf_sharpness, y);
    int32_t* out_acs = jxl_image_i_row(&jxl_channels_at(&image->channel, 2)->plane, 0);
    int32_t* out_qf = jxl_image_i_row(&jxl_channels_at(&image->channel, 2)->plane, 1);
    int32_t* row_out_epf = jxl_image_i_row(&jxl_channels_at(&image->channel, 3)->plane, y);
    for (size_t x = 0; x < jxl_rect_x_size(r); x++) {
      row_out_epf[x] = row_epf[x];
      if (!jxl_ac_strategy_is_first_block(jxl_ac_strategy_row_at(row_acs, x))) continue;
      out_acs[num] = jxl_ac_strategy_raw_strategy(jxl_ac_strategy_row_at(row_acs, x));
      out_qf[num] = row_qf[x] - 1;
      num++;
    }
  }
  jxl_channels_at(&image->channel, 2)->w = num;
  *jxl_array_at(&self->ac_metadata_size, group_index) = num;
  return jxl_ok_status();
}

jxl_status jxl_modular_frame_encoder_encode_quant_table(
    jxl_context* ctx, size_t size_x, size_t size_y,
    jxl_bit_writer* writer, const jxl_quant_encoding* encoding, size_t idx,
    jxl_modular_frame_encoder* modular_frame_encoder){
  JXL_ENSURE(encoding->mode == kQuantModeRAW);
  JXL_ENSURE(idx < kNumQuantTables);
  JXL_ENSURE(modular_frame_encoder != NULL);
  (void)ctx;
  (void)size_x;
  (void)size_y;
  JXL_RETURN_IF_ERROR(jxl_f16_coder_write(encoding->qtable_den, writer));
  jxl_modular_stream_id qt = jxl_modular_stream_id_quant_table(idx);
  return jxl_modular_frame_encoder_encode_stream(modular_frame_encoder, writer, kLayerHeader, &qt);
}

jxl_status jxl_modular_frame_encoder_add_quant_table(jxl_modular_frame_encoder* self, size_t size_x, size_t size_y,
                                          const jxl_quant_encoding* encoding,
                                          const jxl_array_int* qtable,
                                          size_t idx){
  JXL_ENSURE(idx < kNumQuantTables);
  jxl_modular_stream_id qt = jxl_modular_stream_id_quant_table(idx);
  size_t stream_id = jxl_modular_stream_id_id(qt, &self->frame_dim_);
  JXL_ENSURE(encoding->mode == kQuantModeRAW);
  JXL_ENSURE(size_x * size_y * 3 == jxl_array_len(qtable));
  const int* qtable_data = jxl_array_data_const(qtable);
  jxl_image* image = jxl_images_at(&self->stream_images_, stream_id);
  jxl_context* ctx = jxl_image_ctx(image);
  JXL_RETURN_IF_ERROR(
      jxl_image_create(ctx, size_x, size_y, 8, 3, image));
  for (size_t c = 0; c < 3; c++) {
    for (size_t y = 0; y < size_y; y++) {
      int32_t* JXL_RESTRICT row = jxl_channel_row(jxl_channels_at(&image->channel, c), y);
      for (size_t x = 0; x < size_x; x++) {
        row[x] = qtable_data[c * size_x * size_y + y * size_x + x];
      }
    }
  }
  return jxl_ok_status();
}
