// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <jxl/memory_manager.h>

#include <stddef.h>
#include <stdint.h>

#include "lib/jxl/base/array.h"
#include "lib/jxl/base/bits.h"
#include "lib/jxl/base/common.h"
#include "lib/jxl/base/compiler_specific.h"
#include "lib/jxl/base/printf_macros.h"
#include "lib/jxl/base/status.h"
#include "lib/jxl/enc_ans.h"
#include "lib/jxl/layer_type.h"
#include "lib/jxl/enc_bit_writer.h"
#include "lib/jxl/enc_fields.h"
#include "lib/jxl/fields.h"
#include "lib/jxl/modular/encoding/context_predict.h"
#include "lib/jxl/modular/encoding/dec_ma.h"
#include "lib/jxl/modular/encoding/enc_ma.h"
#include "lib/jxl/modular/encoding/encoding.h"
#include "lib/jxl/modular/encoding/ma_common.h"
#include "lib/jxl/modular/modular_image.h"
#include "lib/jxl/modular/options.h"
#include "lib/jxl/pack_signed.h"


typedef struct jxl_fixed_tree_node_info {
  size_t begin, end, pos;
} jxl_fixed_tree_node_info;
JXL_DEFINE_POD_ARRAY(jxl_array_fixed_tree_node_info, jxl_fixed_tree_node_info)

static inline jxl_fixed_tree_node_info jxl_fixed_tree_node_info_make(size_t begin, size_t end,
                                               size_t pos) {
  jxl_fixed_tree_node_info info;
  info.begin = begin;
  info.end = end;
  info.pos = pos;
  return info;
}

// `cutoffs` must be sorted.
static void jxl_make_fixed_tree(int property, const jxl_array_i32 *cutoffs, jxl_predictor pred,
                   size_t num_pixels, int bitdepth, jxl_tree *out) {
  size_t log_px = jxl_ceil_log2_nonzero64(num_pixels);
  size_t min_gap = 0;
  // Reduce fixed tree height when encoding small images.
  if (log_px < 14) {
    min_gap = 8 * (14 - log_px);
  }
  const int shift = bitdepth > 11 ? JXL_MIN(4, bitdepth - 11) : 0;
  const int mul = 1 << shift;
  jxl_memory_manager* mm = out->memory_manager;
  jxl_tree tree;
  jxl_array_construct_empty(&tree, mm);
  jxl_array_fixed_tree_node_info q;
  jxl_array_construct_empty(&q, mm);
  size_t q_head = 0;
  // Leaf IDs will be set by roundtrip decoding the tree.
if (!jxl_status_ok(jxl_array_property_decision_node_push_back(&tree, jxl_property_decision_node_leaf(pred, 0, 1)))) JXL_CRASH();
if (!jxl_status_ok(jxl_array_fixed_tree_node_info_push_back(&q, jxl_fixed_tree_node_info_make(0, jxl_array_len(cutoffs), 0)))) JXL_CRASH();
while (q_head < jxl_array_len(&q)) {
    jxl_fixed_tree_node_info info = *jxl_array_at(&q, q_head++);
    if (info.begin + min_gap >= info.end) continue;
    uint32_t split = (info.begin + info.end) / 2;
    int32_t cutoff = *jxl_array_at_const(cutoffs, split) * mul;
    *jxl_array_at(&tree, info.pos) = jxl_property_decision_node_split(property, cutoff, jxl_array_len(&tree), -1);
if (!jxl_status_ok(jxl_array_fixed_tree_node_info_push_back(&q, jxl_fixed_tree_node_info_make(split + 1, info.end, jxl_array_len(&tree))))) JXL_CRASH();
if (!jxl_status_ok(jxl_array_property_decision_node_push_back(&tree, jxl_property_decision_node_leaf(pred, 0, 1)))) JXL_CRASH();
if (!jxl_status_ok(jxl_array_fixed_tree_node_info_push_back(&q, jxl_fixed_tree_node_info_make(info.begin, split, jxl_array_len(&tree))))) JXL_CRASH();
if (!jxl_status_ok(jxl_array_property_decision_node_push_back(&tree, jxl_property_decision_node_leaf(pred, 0, 1)))) JXL_CRASH();
}
  jxl_array_destroy(&q);
  jxl_array_swap(out, &tree);
  jxl_array_destroy(&tree);
}

typedef struct jxl_gather_sample_ctx {
  jxl_properties *properties;
  size_t w;
  ptrdiff_t onerow;
  jxl_channel *references;
  jxl_weighted_state *wp_state;
  jxl_tree_samples *tree_samples;
  size_t *total_pixels;
  jxl_xor_shift128_plus *rng;
  uint64_t threshold;
  bool multiple_predictors;
} jxl_gather_sample_ctx;

static void jxl_compute_tree_sample(jxl_gather_sample_ctx *ctx, const pixel_type *p, size_t x,
                       size_t y) {
  pixel_type_w pred[kNumModularPredictors];
  if (ctx->multiple_predictors) {
    jxl_predict_learn_all(ctx->properties, ctx->w, p + x, ctx->onerow, x, y,
                    ctx->references, ctx->wp_state, pred);
  } else {
    pred[(int)(jxl_tree_samples_predictor_from_index(ctx->tree_samples, 0))] =
        jxl_predict_learn(ctx->properties, ctx->w, p + x, ctx->onerow, x, y,
                     jxl_tree_samples_predictor_from_index(ctx->tree_samples, 0), ctx->references,
                     ctx->wp_state)
            .guess;
  }
  (*ctx->total_pixels)++;
  if (jxl_xor_shift128_plus_below_threshold(ctx->rng, ctx->threshold)) {
    jxl_tree_samples_add_sample(ctx->tree_samples, p[x], ctx->properties, pred);
  }
  jxl_weighted_state_update_errors(ctx->wp_state, p[x], x, y, ctx->w);
}

static jxl_status jxl_gather_tree_data(const jxl_image *image, pixel_type chan, size_t group_id,
                      const jxl_weighted_header *wp_header,
                      const jxl_modular_options *options, jxl_tree_samples *tree_samples,
                      size_t *total_pixels) {
  const jxl_channel *channel = jxl_channels_at_const(&image->channel, chan);
  jxl_memory_manager *memory_manager = jxl_channel_memory_manager(channel);

  JXL_DEBUG_V(7, "Learning %" jxl_pr_iu_s "x%" jxl_pr_iu_s " channel %d", channel->w,
              channel->h, chan);

  pixel_type static_props[kNumStaticProperties] = {
      chan, (int)(group_id)};
  jxl_properties properties;
  jxl_array_construct_empty(&properties, memory_manager);
  jxl_status status = jxl_array_resize_zero(&properties, kNumNonrefProperties);
  if (!jxl_status_ok(status)) {
    jxl_array_destroy(&properties);
    return status;
  }
  double pixel_fraction = JXL_MIN(1.0f, options->nb_repeats);
  // a fraction of 0 is used to disable learning entirely.
  if (pixel_fraction > 0) {
    pixel_fraction = JXL_MAX(pixel_fraction,
                              JXL_MIN(1.0, 1024.0 / (channel->w * channel->h)));
  }
  uint64_t threshold =
      (UINT64_MAX >> 32) * pixel_fraction;
  jxl_xor_shift128_plus rng = jxl_xor_shift128_plus_make(0x94D049BB133111EBull, 0xBF58476D1CE4E5B9ull);

  const ptrdiff_t onerow = jxl_image_i_pixels_per_row(&channel->plane);
  // JPEG encoder trees never use reference-channel properties.
  jxl_channel references;
  jxl_channel_construct_empty(&references);
  status = jxl_channel_create(memory_manager, /*iw=*/0, /*ih=*/1, 0, 0,
                                &references);
  if (!jxl_status_ok(status)) {
    jxl_array_destroy(&properties);
    jxl_channel_destroy(&references);
    return status;
  }
  jxl_weighted_state wp_state;
  jxl_weighted_state_init(&wp_state, wp_header, channel->w, channel->h,
                          memory_manager);
  jxl_tree_samples_prepare_for_samples(tree_samples, pixel_fraction * channel->h * channel->w + 64);
  jxl_gather_sample_ctx sample_ctx = {
      &properties,
      channel->w,
      onerow,
      &references,
      &wp_state,
      tree_samples,
      total_pixels,
      &rng,
      threshold,
      jxl_tree_samples_num_predictors(tree_samples) != 1,
  };

  for (size_t y = 0; y < channel->h; y++) {
    const pixel_type *JXL_RESTRICT p = jxl_channel_row_const(channel, y);
    jxl_init_props_row(&properties, static_props, y);

    // TODO(veluca): avoid computing WP if we don't use its property or
    // predictions.
    if (y > 1 && channel->w > 8) {
      for (size_t x = 0; x < 2; x++) {
        jxl_compute_tree_sample(&sample_ctx, p, x, y);
      }
      for (size_t x = 2; x < channel->w - 2; x++) {
        pixel_type_w pred[kNumModularPredictors];
        if (sample_ctx.multiple_predictors) {
          jxl_predict_learn_all_nec(&properties, channel->w, p + x, onerow, x, y,
                             &references, &wp_state, pred);
        } else {
          pred[(int)(jxl_tree_samples_predictor_from_index(tree_samples, 0))] =
              jxl_predict_learn_nec(&properties, channel->w, p + x, onerow, x, y,
                              jxl_tree_samples_predictor_from_index(tree_samples, 0), &references,
                              &wp_state)
                  .guess;
        }
        (*total_pixels)++;
        if (jxl_xor_shift128_plus_below_threshold(&rng, threshold)) {
          jxl_tree_samples_add_sample(tree_samples, p[x], &properties, pred);
        }
        jxl_weighted_state_update_errors(&wp_state, p[x], x, y, channel->w);
      }
      for (size_t x = channel->w - 2; x < channel->w; x++) {
        jxl_compute_tree_sample(&sample_ctx, p, x, y);
      }
    } else {
      for (size_t x = 0; x < channel->w; x++) {
        jxl_compute_tree_sample(&sample_ctx, p, x, y);
      }
    }
  }
  jxl_channel_destroy(&references);
  jxl_weighted_state_destroy(&wp_state);
  jxl_array_destroy(&properties);
  return jxl_ok_status();
}

static jxl_status jxl_learn_tree_from_samples(jxl_tree_samples *tree_samples, size_t total_pixels,
                 const jxl_modular_options *options,
                 const jxl_array_modular_multiplier_info *multiplier_info,
                 jxl_static_prop_range static_prop_range, jxl_tree *out) {
  jxl_tree tree;
  jxl_array_construct_empty(&tree, out->memory_manager);
  for (size_t i = 0; i < kNumStaticProperties; i++) {
    if (jxl_static_prop_range_at(&static_prop_range, i)[1] == 0) {
      jxl_static_prop_range_at(&static_prop_range, i)[1] = UINT32_MAX;
    }
  }
  if (!jxl_tree_samples_has_samples(tree_samples)) {
    if (!jxl_status_ok(jxl_array_property_decision_node_push_back(&tree, jxl_property_decision_node_leaf(
            jxl_tree_samples_predictor_from_index(tree_samples, 0), 0, 1)))) JXL_CRASH();
    jxl_array_swap(out, &tree);
    jxl_array_destroy(&tree);
    return jxl_ok_status();
  }
  float pixel_fraction = jxl_tree_samples_num_samples(tree_samples) * 1.0f / total_pixels;
  float required_cost = pixel_fraction * 0.9 + 0.1;
  jxl_tree_samples_all_samples_done(tree_samples);
  jxl_status status = jxl_compute_best_tree(
      tree_samples, options->splitting_heuristics_node_threshold * required_cost,
      multiplier_info, static_prop_range, options->fast_decode_multiplier,
      &tree);
  if (!jxl_status_ok(status)) {
    jxl_array_destroy(&tree);
    return status;
  }
  jxl_array_swap(out, &tree);
  jxl_array_destroy(&tree);
  return jxl_ok_status();
}

static jxl_status jxl_encode_modular_channel_maans(const jxl_image *image, pixel_type chan,
                                 const jxl_weighted_header *wp_header,
                                 const jxl_tree *global_tree, jxl_token **tokenpp,
                                 size_t group_id) {
  const jxl_channel *channel = jxl_channels_at_const(&image->channel, chan);
  jxl_memory_manager *memory_manager = jxl_channel_memory_manager(channel);
  jxl_token *tokenp = *tokenpp;
  JXL_ENSURE(channel->w != 0 && channel->h != 0);

  JXL_DEBUG_V(6,
              "Encoding %" jxl_pr_iu_s "x%" jxl_pr_iu_s
              " channel %d, "
              "(shift=%i,%i)",
              channel->w, channel->h, chan, channel->hshift, channel->vshift);

  pixel_type static_props[kNumStaticProperties] = {
      chan, (int)(group_id)};
  bool use_wp;
  bool is_gradient_only;
  size_t num_props;
  jxl_flat_tree tree;
  jxl_array_construct_empty(&tree, memory_manager);
  jxl_filter_tree(global_tree, static_props, &num_props, &use_wp,
             &is_gradient_only, &tree);
  jxl_ma_tree_lookup tree_lookup = jxl_ma_tree_lookup_make(&tree);
  JXL_DEBUG_V(3, "Encoding using a MA tree with %" jxl_pr_iu_s " nodes", jxl_array_len(&tree));

  // Initialized to avoid clang-tidy complaining.
  jxl_tree_lut tree_lut;
  jxl_array_construct_empty(&tree_lut.context_lookup, memory_manager);
  if (is_gradient_only) {
    jxl_status lut_status = jxl_tree_lut_init(&tree_lut, memory_manager);
    if (!jxl_status_ok(lut_status)) {
      jxl_tree_lut_destroy(&tree_lut);
      jxl_array_destroy(&tree);
      return lut_status;
    }
    is_gradient_only = jxl_tree_to_lookup_table(&tree, &tree_lut);
  }

  if (jxl_array_len(&tree) == 1 && jxl_array_at(&tree, 0)->top.predictor == kPredictorGradient &&
             jxl_array_at(&tree, 0)->children.multiplier == 1 && jxl_array_at(&tree, 0)->meta.predictor_offset == 0) {
    const ptrdiff_t onerow = jxl_image_i_pixels_per_row(&channel->plane);
    bool unhealthy = false;
    for (size_t y = 0; y < channel->h; y++) {
      const pixel_type *JXL_RESTRICT r = jxl_channel_row_const(channel, y);
      for (size_t x = 0; x < channel->w; x++) {
        pixel_type_w left = (x ? r[x - 1] : y ? *(r + x - onerow) : 0);
        pixel_type_w top = (y ? *(r + x - onerow) : left);
        pixel_type_w topleft = (x && y ? *(r + x - 1 - onerow) : left);
        int32_t guess = jxl_clamped_gradient(top, left, topleft);
        int32_t residual;
        unhealthy |= jxl_sub_overflow(r[x], guess, &residual);
        *tokenp++ = jxl_token_make(jxl_array_at(&tree, 0)->childID, jxl_pack_signed(residual));
      }
    }
    if (unhealthy) {
      jxl_tree_lut_destroy(&tree_lut);
      jxl_array_destroy(&tree);
      return JXL_FAILURE("Residual overflow");
    }
  } else if (is_gradient_only) {
    const ptrdiff_t onerow = jxl_image_i_pixels_per_row(&channel->plane);
    bool unhealthy = false;
    for (size_t y = 0; y < channel->h; y++) {
      const pixel_type *JXL_RESTRICT r = jxl_channel_row_const(channel, y);
      for (size_t x = 0; x < channel->w; x++) {
        pixel_type_w left = (x ? r[x - 1] : y ? *(r + x - onerow) : 0);
        pixel_type_w top = (y ? *(r + x - onerow) : left);
        pixel_type_w topleft = (x && y ? *(r + x - 1 - onerow) : left);
        int32_t guess = jxl_clamped_gradient(top, left, topleft);
        uint32_t pos =
            kPropRangeFast +
            JXL_MIN(
                JXL_MAX((pixel_type_w)(-kPropRangeFast),
                        top + left - topleft),
                (pixel_type_w)(kPropRangeFast - 1));
        uint32_t ctx_id = *jxl_array_at(&tree_lut.context_lookup, pos);
        int32_t residual;
        unhealthy |= jxl_sub_overflow(r[x], guess, &residual);
        *tokenp++ = jxl_token_make(ctx_id, jxl_pack_signed(residual));
      }
    }
    if (unhealthy) {
      jxl_tree_lut_destroy(&tree_lut);
      jxl_array_destroy(&tree);
      return JXL_FAILURE("Residual overflow");
    }
  } else if (jxl_array_len(&tree) == 1 && jxl_array_at(&tree, 0)->top.predictor == kPredictorZero &&
             jxl_array_at(&tree, 0)->children.multiplier == 1 && jxl_array_at(&tree, 0)->meta.predictor_offset == 0) {
    for (size_t y = 0; y < channel->h; y++) {
      const pixel_type *JXL_RESTRICT p = jxl_channel_row_const(channel, y);
      for (size_t x = 0; x < channel->w; x++) {
        *tokenp++ = jxl_token_make(jxl_array_at(&tree, 0)->childID, jxl_pack_signed(p[x]));
      }
    }
  } else if (jxl_array_len(&tree) == 1 && jxl_array_at(&tree, 0)->top.predictor != kPredictorWeighted &&
             (jxl_array_at(&tree, 0)->children.multiplier & (jxl_array_at(&tree, 0)->children.multiplier - 1)) == 0 &&
             jxl_array_at(&tree, 0)->meta.predictor_offset == 0) {
    // multiplier is a power of 2.
    uint32_t mul_shift =
        jxl_floor_log2_nonzero32((uint32_t)(jxl_array_at(&tree, 0)->children.multiplier));
    const ptrdiff_t onerow = jxl_image_i_pixels_per_row(&channel->plane);
    for (size_t y = 0; y < channel->h; y++) {
      const pixel_type *JXL_RESTRICT r = jxl_channel_row_const(channel, y);
      for (size_t x = 0; x < channel->w; x++) {
        jxl_prediction_result pred = jxl_predict_no_tree_no_wp(channel->w, r + x, onerow, x,
                                                  y, jxl_array_at(&tree, 0)->top.predictor);
        pixel_type_w residual = r[x] - pred.guess;
        JXL_DASSERT((residual >> mul_shift) * jxl_array_at(&tree, 0)->children.multiplier == residual);
        *tokenp++ = jxl_token_make(jxl_array_at(&tree, 0)->childID, jxl_pack_signed(residual >> mul_shift));
      }
    }

  } else if (!use_wp) {
    const ptrdiff_t onerow = jxl_image_i_pixels_per_row(&channel->plane);
    jxl_properties properties;
    jxl_array_construct_empty(&properties, memory_manager);
    jxl_status status = jxl_array_resize_zero(&properties, num_props);
    if (!jxl_status_ok(status)) {
      jxl_array_destroy(&properties);
      jxl_tree_lut_destroy(&tree_lut);
      jxl_array_destroy(&tree);
      return status;
    }
    jxl_channel references;
    jxl_channel_construct_empty(&references);
    status = jxl_channel_create(memory_manager, /*iw=*/0, /*ih=*/1, 0, 0,
                                  &references);
    if (!jxl_status_ok(status)) {
      jxl_array_destroy(&properties);
      jxl_channel_destroy(&references);
      jxl_tree_lut_destroy(&tree_lut);
      jxl_array_destroy(&tree);
      return status;
    }
    for (size_t y = 0; y < channel->h; y++) {
      const pixel_type *JXL_RESTRICT p = jxl_channel_row_const(channel, y);
      jxl_init_props_row(&properties, static_props, y);
      for (size_t x = 0; x < channel->w; x++) {
        jxl_prediction_result res =
            jxl_predict_tree_no_wp(&properties, channel->w, p + x, onerow, x, y,
                            &tree_lookup, &references);
        pixel_type_w residual = p[x] - res.guess;
        JXL_DASSERT(residual % res.multiplier == 0);
        *tokenp++ = jxl_token_make(res.context, jxl_pack_signed(residual / res.multiplier));
      }
    }
    jxl_array_destroy(&properties);
    jxl_channel_destroy(&references);
  } else {
    const ptrdiff_t onerow = jxl_image_i_pixels_per_row(&channel->plane);
    jxl_properties properties;
    jxl_array_construct_empty(&properties, memory_manager);
    jxl_status status = jxl_array_resize_zero(&properties, num_props);
    if (!jxl_status_ok(status)) {
      jxl_array_destroy(&properties);
      jxl_tree_lut_destroy(&tree_lut);
      jxl_array_destroy(&tree);
      return status;
    }
    jxl_channel references;
    jxl_channel_construct_empty(&references);
    status = jxl_channel_create(memory_manager, /*iw=*/0, /*ih=*/1, 0, 0,
                                  &references);
    if (!jxl_status_ok(status)) {
      jxl_array_destroy(&properties);
      jxl_channel_destroy(&references);
      jxl_tree_lut_destroy(&tree_lut);
      jxl_array_destroy(&tree);
      return status;
    }
    jxl_weighted_state wp_state;
    jxl_weighted_state_init(&wp_state, wp_header, channel->w, channel->h,
                          memory_manager);
    for (size_t y = 0; y < channel->h; y++) {
      const pixel_type *JXL_RESTRICT p = jxl_channel_row_const(channel, y);
      jxl_init_props_row(&properties, static_props, y);
      for (size_t x = 0; x < channel->w; x++) {
        jxl_prediction_result res =
            jxl_predict_tree_wp(&properties, channel->w, p + x, onerow, x, y,
                          &tree_lookup, &references, &wp_state);
        pixel_type_w residual = p[x] - res.guess;
        JXL_DASSERT(residual % res.multiplier == 0);
        *tokenp++ = jxl_token_make(res.context, jxl_pack_signed(residual / res.multiplier));
        jxl_weighted_state_update_errors(&wp_state, p[x], x, y, channel->w);
      }
    }
    jxl_array_destroy(&properties);
    jxl_channel_destroy(&references);
    jxl_weighted_state_destroy(&wp_state);
  }
  jxl_tree_lut_destroy(&tree_lut);
  jxl_array_destroy(&tree);
  *tokenpp = tokenp;
  return jxl_ok_status();
}

void jxl_predefined_tree_impl(jxl_modular_tree_kind tree_kind, size_t total_pixels,
                    int bitdepth, jxl_tree *out) {
  jxl_memory_manager* mm = out->memory_manager;
  switch (tree_kind) {
    case kJpegTranscodeACMeta: {
      // All the data is 0, so no need for a fancy tree.
      jxl_tree tree;
      jxl_array_construct_empty(&tree, mm);
if (!jxl_status_ok(jxl_array_property_decision_node_push_back(&tree, jxl_property_decision_node_leaf(kPredictorZero, 0, 1)))) JXL_CRASH();
      jxl_array_swap(out, &tree);
      jxl_array_destroy(&tree);
      return;
    }
    case kWPFixedDC: {
      static const int32_t kCutoffs[] = {
          -500, -392, -255, -191, -127, -95, -63, -47, -31, -23, -15,
          -11,  -7,   -4,   -3,   -1,   0,   1,   3,   5,   7,   11,
          15,   23,   31,   47,   63,   95,  127, 191, 255, 392, 500};
      jxl_array_i32 cutoffs;
      jxl_array_construct_empty(&cutoffs, mm);
      if (!jxl_status_ok(jxl_array_assign(&cutoffs, kCutoffs,
                       sizeof(kCutoffs) / sizeof(kCutoffs[0])))) {
        JXL_CRASH();
      }
      jxl_make_fixed_tree(kWPProp, &cutoffs, kPredictorWeighted, total_pixels,
                    bitdepth, out);
      jxl_array_destroy(&cutoffs);
      return;
    }
    case kGradientFixedDC: {
      static const int32_t kCutoffs[] = {
          -500, -392, -255, -191, -127, -95, -63, -47, -31, -23, -15,
          -11,  -7,   -4,   -3,   -1,   0,   1,   3,   5,   7,   11,
          15,   23,   31,   47,   63,   95,  127, 191, 255, 392, 500};
      jxl_array_i32 cutoffs;
      jxl_array_construct_empty(&cutoffs, mm);
      if (!jxl_status_ok(jxl_array_assign(&cutoffs, kCutoffs,
                       sizeof(kCutoffs) / sizeof(kCutoffs[0])))) {
        JXL_CRASH();
      }
      jxl_make_fixed_tree(kGradientProp, &cutoffs, kPredictorGradient, total_pixels,
                    bitdepth, out);
      jxl_array_destroy(&cutoffs);
      return;
    }
    case kLearn: {
      JXL_DEBUG_ABORT("internal: kLearn is not predefined tree");
      jxl_array_clear(out);
      return;
    }
  }
  JXL_DEBUG_ABORT("internal: unexpected jxl_tree_kind: %d",
                  (int)(tree_kind));
  jxl_array_clear(out);
}

jxl_status jxl_learn_tree_impl(const jxl_image *images, const jxl_modular_options *options,
                 const uint32_t start, const uint32_t stop,
                 const jxl_array_modular_multiplier_info *multiplier_info,
                 jxl_tree *out) {
  jxl_memory_manager* mm = out->memory_manager;
  jxl_tree_samples tree_samples;
  jxl_tree_samples_construct_empty(&tree_samples, mm);
  jxl_array_i32 pixel_samples;
  jxl_array_construct_empty(&pixel_samples, mm);
  jxl_array_i32 diff_samples;
  jxl_array_construct_empty(&diff_samples, mm);
  jxl_array_u32 group_pixel_count;
  jxl_array_construct_empty(&group_pixel_count, mm);
  jxl_array_u32 channel_pixel_count;
  jxl_array_construct_empty(&channel_pixel_count, mm);
  jxl_tree tree;
  jxl_array_construct_empty(&tree, mm);
  jxl_status status = jxl_tree_samples_set_predictor(&tree_samples, options[start].predictor,
                                                options[start].wp_tree_mode);
  if (!jxl_status_ok(status)) {
    jxl_array_destroy(&tree);
    jxl_array_destroy(&pixel_samples);
    jxl_array_destroy(&diff_samples);
    jxl_array_destroy(&group_pixel_count);
    jxl_array_destroy(&channel_pixel_count);
    jxl_tree_samples_destroy(&tree_samples);
    return status;
  }
  status = jxl_tree_samples_set_properties(&tree_samples, 
      options[start].splitting_heuristics_properties,
      options[start].num_splitting_heuristics_properties,
      options[start].wp_tree_mode);
  if (!jxl_status_ok(status)) {
    jxl_array_destroy(&tree);
    jxl_array_destroy(&pixel_samples);
    jxl_array_destroy(&diff_samples);
    jxl_array_destroy(&group_pixel_count);
    jxl_array_destroy(&channel_pixel_count);
    jxl_tree_samples_destroy(&tree_samples);
    return status;
  }
  uint32_t max_c = 0;
  for (uint32_t i = start; i < stop; i++) {
    max_c = JXL_MAX((uint32_t)(jxl_channels_size(&images[i].channel)), max_c);
    jxl_collect_pixel_samples(&images[i], &options[i], i, &group_pixel_count,
                        &channel_pixel_count, &pixel_samples, &diff_samples);
  }
  jxl_static_prop_range range;
  jxl_static_prop_range_construct_empty(&range);
  jxl_static_prop_range_at(&range, 0)[0] = 0;
  jxl_static_prop_range_at(&range, 0)[1] = max_c;
  jxl_static_prop_range_at(&range, 1)[0] = start;
  jxl_static_prop_range_at(&range, 1)[1] = stop;

  jxl_tree_samples_pre_quantize_properties(&tree_samples, 
      &range, multiplier_info, &group_pixel_count, &channel_pixel_count,
      &pixel_samples, &diff_samples, options[start].max_property_values);

  size_t total_pixels = 0;
  for (size_t i = 0; i < jxl_channels_size(&images[start].channel); i++) {
    if (i >= images[start].nb_meta_channels &&
        (jxl_channels_at_const(&images[start].channel, i)->w > options[start].max_chan_size ||
         jxl_channels_at_const(&images[start].channel, i)->h > options[start].max_chan_size)) {
      break;
    }
    total_pixels += jxl_channels_at_const(&images[start].channel, i)->w * jxl_channels_at_const(&images[start].channel, i)->h;
  }
  total_pixels = JXL_MAX(total_pixels, (size_t)(1));

  jxl_weighted_header wp_header;
  jxl_weighted_header_init(&wp_header);

  for (size_t i = start; i < stop; i++) {
    size_t nb_channels = jxl_channels_size(&images[i].channel);

    if (images[i].w == 0 || images[i].h == 0 || nb_channels < 1)
      continue;  // is there any use for a zero-channel image?
    if (images[i].error) {
      jxl_array_destroy(&tree);
      jxl_array_destroy(&pixel_samples);
      jxl_array_destroy(&diff_samples);
      jxl_array_destroy(&group_pixel_count);
      jxl_array_destroy(&channel_pixel_count);
      jxl_tree_samples_destroy(&tree_samples);
      return JXL_FAILURE("Invalid image");
    }
    JXL_ENSURE(options[i].tree_kind == kLearn);

    JXL_DEBUG_V(
        2, "Encoding %" jxl_pr_iu_s "-channel, %i-bit, %" jxl_pr_iu_s "x%" jxl_pr_iu_s " image.",
        nb_channels, images[i].bitdepth, images[i].w, images[i].h);

    // encode transforms
    jxl_bundle_init(&wp_header.fields);
    if (jxl_predictor_has_weighted(options[i].predictor)) {
      jxl_weighted_predictor_mode(options[i].wp_mode, &wp_header);
    }

    // Gather tree data
    for (size_t c = 0; c < nb_channels; c++) {
      if (c >= images[i].nb_meta_channels &&
          (jxl_channels_at_const(&images[i].channel, c)->w > options[i].max_chan_size ||
           jxl_channels_at_const(&images[i].channel, c)->h > options[i].max_chan_size)) {
        break;
      }
      if (!jxl_channels_at_const(&images[i].channel, c)->w || !jxl_channels_at_const(&images[i].channel, c)->h) {
        continue;  // skip empty channels
      }
      status = jxl_gather_tree_data(&images[i], c, i, &wp_header, &options[i],
                                         &tree_samples, &total_pixels);
      if (!jxl_status_ok(status)) {
        jxl_array_destroy(&tree);
        jxl_array_destroy(&pixel_samples);
        jxl_array_destroy(&diff_samples);
        jxl_array_destroy(&group_pixel_count);
        jxl_array_destroy(&channel_pixel_count);
        jxl_tree_samples_destroy(&tree_samples);
        return status;
      }
    }
  }

  // TODO(veluca): parallelize more.
  status = jxl_learn_tree_from_samples(&tree_samples, total_pixels, &options[start],
                                multiplier_info, range, &tree);
  if (!jxl_status_ok(status)) {
    jxl_array_destroy(&tree);
    jxl_array_destroy(&pixel_samples);
    jxl_array_destroy(&diff_samples);
    jxl_array_destroy(&group_pixel_count);
    jxl_array_destroy(&channel_pixel_count);
    jxl_tree_samples_destroy(&tree_samples);
    return status;
  }
  jxl_array_swap(out, &tree);
  jxl_array_destroy(&tree);
  jxl_array_destroy(&pixel_samples);
  jxl_array_destroy(&diff_samples);
  jxl_array_destroy(&group_pixel_count);
  jxl_array_destroy(&channel_pixel_count);
  jxl_tree_samples_destroy(&tree_samples);
  return jxl_ok_status();
}

jxl_status jxl_modular_compress_impl(const jxl_image *image, const jxl_modular_options *options,
                       size_t group_id, const jxl_tree *tree, jxl_group_header *header,
                       jxl_token_stream *tokens, size_t *width){
  size_t nb_channels = jxl_channels_size(&image->channel);

  if (image->w == 0 || image->h == 0 || nb_channels < 1)
    return jxl_ok_status();  // is there any use for a zero-channel image?
  if (image->error) return JXL_FAILURE("Invalid image");

  JXL_DEBUG_V(
      2, "Encoding %" jxl_pr_iu_s "-channel, %i-bit, %" jxl_pr_iu_s "x%" jxl_pr_iu_s " image.",
      nb_channels, image->bitdepth, image->w, image->h);

  // encode transforms
  jxl_bundle_init(&header->fields);
  if (jxl_predictor_has_weighted(options->predictor)) {
    jxl_weighted_predictor_mode(options->wp_mode, &header->wp_header);
  }
  header->use_global_tree = true;

  size_t image_width = 0;
  size_t total_tokens = 0;
  for (size_t i = 0; i < nb_channels; i++) {
    if (i >= image->nb_meta_channels &&
        (jxl_channels_at_const(&image->channel, i)->w > options->max_chan_size ||
         jxl_channels_at_const(&image->channel, i)->h > options->max_chan_size)) {
      break;
    }
    if (jxl_channels_at_const(&image->channel, i)->w > image_width) image_width = jxl_channels_at_const(&image->channel, i)->w;
    total_tokens += jxl_channels_at_const(&image->channel, i)->w * jxl_channels_at_const(&image->channel, i)->h;
  }
  // Do one big allocation for all the tokens we'll need,
  // to avoid reallocs that might require copying.
  size_t pos = jxl_array_len(tokens);
  JXL_RETURN_IF_ERROR(jxl_array_resize_zero(tokens, pos + total_tokens));
  jxl_token *tokenp = jxl_array_data(tokens) + pos;
  for (size_t i = 0; i < nb_channels; i++) {
    if (i >= image->nb_meta_channels &&
        (jxl_channels_at_const(&image->channel, i)->w > options->max_chan_size ||
         jxl_channels_at_const(&image->channel, i)->h > options->max_chan_size)) {
      break;
    }
    if (!jxl_channels_at_const(&image->channel, i)->w || !jxl_channels_at_const(&image->channel, i)->h) {
      continue;  // skip empty channels
    }
    JXL_RETURN_IF_ERROR(jxl_encode_modular_channel_maans(
        image, i, &header->wp_header, tree, &tokenp, group_id));
  }
  // Make sure we actually wrote all tokens
  JXL_ENSURE(tokenp == jxl_array_data(tokens) + jxl_array_len(tokens));

  *width = image_width;

  return jxl_ok_status();
}

jxl_status jxl_modular_generic_compress_impl(const jxl_image *image, const jxl_modular_options *opts,
                              jxl_bit_writer *writer, jxl_layer_type layer){
  size_t nb_channels = jxl_channels_size(&image->channel);

  if (image->w == 0 || image->h == 0 || nb_channels < 1)
    return jxl_ok_status();  // is there any use for a zero-channel image?
  if (image->error) return JXL_FAILURE("Invalid image");

  jxl_modular_options options = *opts;  // Make a copy to modify it.
  if (options.predictor == kUndefinedPredictor) {
    options.predictor = kPredictorGradient;
  }

  size_t bits = jxl_bit_writer_bits_written(writer);

  jxl_memory_manager *memory_manager = jxl_image_memory_manager(image);
  JXL_DEBUG_V(
      2, "Encoding %" jxl_pr_iu_s "-channel, %i-bit, %" jxl_pr_iu_s "x%" jxl_pr_iu_s " image.",
      nb_channels, image->bitdepth, image->w, image->h);

  // encode transforms
  jxl_group_header header;
  jxl_group_header_init(&header);
  if (jxl_predictor_has_weighted(options.predictor)) {
    jxl_weighted_predictor_mode(options.wp_mode, &header.wp_header);
  }

  JXL_RETURN_IF_ERROR(jxl_bundle_write(&header.fields, writer, layer));

  // Compute tree.
  jxl_memory_manager* mm = jxl_bit_writer_memory_manager(writer);
  jxl_tree tree;
  jxl_array_construct_empty(&tree, mm);
  if (options.tree_kind == kLearn) {
    jxl_array_modular_multiplier_info empty_multiplier_info;
    jxl_array_construct_empty(&empty_multiplier_info, mm);
    jxl_status learn_status =
        jxl_learn_tree_impl(image, &options, 0, 1, &empty_multiplier_info, &tree);
    jxl_array_destroy(&empty_multiplier_info);
    if (!jxl_status_ok(learn_status)) {
      jxl_array_destroy(&tree);
      return learn_status;
    }
  } else {
    size_t total_pixels = 0;
    for (size_t i = 0; i < nb_channels; i++) {
      if (i >= image->nb_meta_channels &&
          (jxl_channels_at_const(&image->channel, i)->w > options.max_chan_size ||
           jxl_channels_at_const(&image->channel, i)->h > options.max_chan_size)) {
        break;
      }
      total_pixels += jxl_channels_at_const(&image->channel, i)->w * jxl_channels_at_const(&image->channel, i)->h;
    }
    total_pixels = JXL_MAX(total_pixels, (size_t)(1));

    jxl_predefined_tree_impl(options.tree_kind, total_pixels, image->bitdepth, &tree);
  }

  jxl_tree decoded_tree;
  jxl_array_construct_empty(&decoded_tree, mm);
  jxl_token_streams tree_tokens;
  jxl_token_streams_create(&tree_tokens, 1, mm);
  jxl_token_streams tokens;
  jxl_token_streams_create(&tokens, 1, mm);
  jxl_entropy_encoding_data code;
  jxl_entropy_encoding_data_init(&code, memory_manager);
  jxl_array_size empty_widths;
  jxl_array_construct_empty(&empty_widths, memory_manager);
  jxl_array_size image_widths;
  jxl_array_construct_empty(&image_widths, mm);
  jxl_status status = jxl_tokenize_tree(&tree, jxl_token_streams_data(&tree_tokens), &decoded_tree);
  if (!jxl_status_ok(status)) {
    jxl_array_destroy(&tree);
    jxl_array_destroy(&decoded_tree);
    jxl_array_destroy(&empty_widths);
    jxl_array_destroy(&image_widths);
    jxl_entropy_encoding_data_destroy(&code);
    jxl_token_streams_destroy(&tree_tokens);
    jxl_token_streams_destroy(&tokens);
    return status;
  }
  JXL_ENSURE(jxl_array_len(&tree) == jxl_array_len(&decoded_tree));
  jxl_array_swap(&tree, &decoded_tree);

  // Write tree
  size_t cost;
  status =
      jxl_build_and_encode_histograms(memory_manager, &options.histogram_params,
                               kNumTreeContexts, &tree_tokens, &code, writer,
                               kLayerModularTree, &empty_widths, &cost);
  if (jxl_status_ok(status)) {
    status = jxl_write_tokens(jxl_token_streams_at(&tree_tokens, 0), &code, 0, writer,
                         kLayerModularTree);
  }

  size_t image_width = 0;
  // it puts `use_global_tree = true` in the header, but this is not used
  // further
  if (jxl_status_ok(status)) {
    status = jxl_modular_compress_impl(image, &options, /*group_id=*/0, &tree, &header,
                             jxl_token_streams_at(&tokens, 0), &image_width);
  }

  // Write data
  if (jxl_status_ok(status)) {
    jxl_entropy_encoding_data_destroy(&code);
    jxl_entropy_encoding_data_construct_empty(&code, memory_manager);
    jxl_entropy_encoding_data_init(&code, memory_manager);
    jxl_histogram_params histo_params = options.histogram_params;
    status = jxl_array_size_push_back(&image_widths, image_width);
    if (jxl_status_ok(status)) {
      status = jxl_build_and_encode_histograms(
          memory_manager, &histo_params, (jxl_array_len(&tree) + 1) / 2, &tokens,
          &code, writer, layer, &image_widths, &cost);
    }
    if (jxl_status_ok(status)) {
      status = jxl_write_tokens(jxl_token_streams_at(&tokens, 0), &code, 0, writer, layer);
    }
  }
  (void)cost;
  jxl_array_destroy(&tree);
  jxl_array_destroy(&decoded_tree);
  jxl_array_destroy(&empty_widths);
  jxl_array_destroy(&image_widths);
  jxl_entropy_encoding_data_destroy(&code);
  jxl_token_streams_destroy(&tree_tokens);
  jxl_token_streams_destroy(&tokens);
  if (!jxl_status_ok(status)) return status;

  bits = jxl_bit_writer_bits_written(writer) - bits;
  JXL_DEBUG_V(4,
              "Modular-encoded a %" jxl_pr_iu_s "x%" jxl_pr_iu_s
              " bitdepth=%i nbchans=%" jxl_pr_iu_s " image in %" jxl_pr_iu_s " bytes",
              image->w, image->h, image->bitdepth, jxl_channels_size(&image->channel), bits / 8);
  (void)bits;

  return jxl_ok_status();
}


void jxl_predefined_tree(jxl_modular_tree_kind tree_kind, size_t total_pixels, int bitdepth, jxl_tree *out) {
  jxl_predefined_tree_impl(tree_kind, total_pixels, bitdepth, out);
}

jxl_status jxl_learn_tree(const jxl_image *images, const jxl_modular_options *opts, uint32_t start, uint32_t stop, const jxl_array_modular_multiplier_info *multiplier_info, jxl_tree *out) {
  return jxl_learn_tree_impl(images, opts, start, stop, multiplier_info, out);
}

jxl_status jxl_modular_generic_compress(const jxl_image *image, const jxl_modular_options *opts, jxl_bit_writer *writer, jxl_layer_type layer) {
  return jxl_modular_generic_compress_impl(image, opts, writer, layer);
}

jxl_status jxl_modular_compress(const jxl_image *image, const jxl_modular_options *opts, size_t group_id, const jxl_tree *tree, jxl_group_header *header, jxl_token_stream *tokens, size_t *width) {
  return jxl_modular_compress_impl(image, opts, group_id, tree, header, tokens, width);
}
