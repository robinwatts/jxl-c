// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#include "modular/encoding/enc_ma.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "base/array.h"
#include "base/bits.h"
#include "base/common.h"
#include "base/compiler_specific.h"
#include "base/enc_status.h"
#include "modular/encoding/dec_ma.h"
#include "modular/encoding/ma_common.h"
#include "modular/modular_image.h"

#include "base/fast_math_scalar.h"
#include "base/random.h"
#include "enc_ans.h"
#include "modular/encoding/context_predict.h"
#include "modular/options.h"
#include "pack_signed.h"

typedef struct jxl_ma_node_info {
  size_t pos;
  size_t begin;
  size_t end;
  jxl_static_prop_range static_prop_range;
} jxl_ma_node_info;
JXL_DEFINE_POD_ARRAY(jxl_array_ma_node_info, jxl_ma_node_info)

typedef struct jxl_ma_cost_info {
  float cost;
  float extra_cost;
  jxl_enc_predictor pred;  // may be uninitialized; never used in that case.
} jxl_ma_cost_info;
static float jxl_ma_cost_info_cost(const jxl_ma_cost_info* self) {
  return self->cost + self->extra_cost;
}
JXL_DEFINE_POD_ARRAY(jxl_array_ma_cost_info, jxl_ma_cost_info)

typedef struct jxl_split_info {
  size_t prop;
  uint32_t val;
  size_t pos;
  float lcost;
  float rcost;
  jxl_enc_predictor lpred;
  jxl_enc_predictor rpred;
} jxl_split_info;
static float jxl_split_info_cost(const jxl_split_info* self) {
  return self->lcost + self->rcost;
}

static inline void jxl_split_info_construct_empty(jxl_split_info* self) {
  self->prop = 0;
  self->val = 0;
  self->pos = 0;
  self->lcost = FLT_MAX;
  self->rcost = FLT_MAX;
  self->lpred = kPredictorZero;
  self->rpred = kPredictorZero;
}

static inline void jxl_ma_cost_info_construct_empty(jxl_ma_cost_info* self) {
  self->cost = FLT_MAX;
  self->extra_cost = 0;
  self->pred = kPredictorZero;
}

// Compute entropy of the histogram, taking into account the minimum probability
// for symbols with non-zero counts.
static float jxl_estimate_bits(const int32_t* counts, size_t num_symbols) {
  int32_t total = 0;
  for (size_t i = 0; i < num_symbols; ++i) {
    total += counts[i];
  }
  const float minprob = 1.0f / ANS_TAB_SIZE;
  const float inv_total = 1.0f / total;
  float bits = 0.0f;
  for (size_t i = 0; i < num_symbols; ++i) {
    if (counts[i] == total) continue;
    const float probs = counts[i] * inv_total;
    const float mprobs = JXL_MAX(probs, minprob);
    bits -= counts[i] * jxl_fast_log2f(mprobs);
  }
  return bits;
}

static void jxl_make_split_node(size_t pos, int property, int splitval, jxl_enc_predictor lpred,
                   int64_t loff, jxl_enc_predictor rpred, int64_t roff, jxl_tree *tree) {
  // Note that the tree splits on *strictly greater*.
  jxl_array_at(tree, pos)->lchild = jxl_array_len(tree);
  jxl_array_at(tree, pos)->rchild = jxl_array_len(tree) + 1;
  jxl_array_at(tree, pos)->splitval = splitval;
  jxl_array_at(tree, pos)->property = property;
  if (!jxl_enc_status_ok(jxl_array_property_decision_node_push_back(tree, jxl_property_decision_node_leaf(rpred, roff, 1)))) JXL_CRASH();
  if (!jxl_enc_status_ok(jxl_array_property_decision_node_push_back(tree, jxl_property_decision_node_leaf(lpred, loff, 1)))) JXL_CRASH();
}

typedef enum jxl_intersection_type {
  kIntersectionNone,
  kIntersectionPartial,
  kIntersectionInside
} jxl_intersection_type;
static jxl_intersection_type jxl_box_intersects(jxl_static_prop_range needle, jxl_static_prop_range haystack,
                               uint32_t *partial_axis, uint32_t *partial_val) {
  bool partial = false;
  for (size_t i = 0; i < kNumStaticProperties; i++) {
    if (jxl_static_prop_range_at(&haystack, i)[0] >= jxl_static_prop_range_at(&needle, i)[1]) {
      return kIntersectionNone;
    }
    if (jxl_static_prop_range_at(&haystack, i)[1] <= jxl_static_prop_range_at(&needle, i)[0]) {
      return kIntersectionNone;
    }
    if (jxl_static_prop_range_at(&haystack, i)[0] <= jxl_static_prop_range_at(&needle, i)[0] && jxl_static_prop_range_at(&haystack, i)[1] >= jxl_static_prop_range_at(&needle, i)[1]) {
      continue;
    }
    partial = true;
    *partial_axis = i;
    if (jxl_static_prop_range_at(&haystack, i)[0] > jxl_static_prop_range_at(&needle, i)[0] && jxl_static_prop_range_at(&haystack, i)[0] < jxl_static_prop_range_at(&needle, i)[1]) {
      *partial_val = jxl_static_prop_range_at(&haystack, i)[0] - 1;
    } else {
      JXL_DASSERT(jxl_static_prop_range_at(&haystack, i)[1] > jxl_static_prop_range_at(&needle, i)[0] &&
                  jxl_static_prop_range_at(&haystack, i)[1] < jxl_static_prop_range_at(&needle, i)[1]);
      *partial_val = jxl_static_prop_range_at(&haystack, i)[1] - 1;
    }
  }
  return partial ? kIntersectionPartial : kIntersectionInside;
}

static void jxl_split_tree_samples_static(jxl_tree_samples *tree_samples, size_t begin, size_t pos,
                            size_t end, size_t prop, uint32_t val) {
  size_t begin_pos = begin;
  size_t end_pos = pos;
  do {
    while (begin_pos < pos &&
           jxl_tree_samples_static_property(tree_samples, prop, begin_pos) <= val) {
      ++begin_pos;
    }
    while (end_pos < end &&
           jxl_tree_samples_static_property(tree_samples, prop, end_pos) > val) {
      ++end_pos;
    }
    if (begin_pos < pos && end_pos < end) {
      jxl_tree_samples_swap(tree_samples, begin_pos, end_pos);
    }
    ++begin_pos;
    ++end_pos;
  } while (begin_pos < pos && end_pos < end);
}

static void jxl_split_tree_samples_sample(jxl_tree_samples *tree_samples, size_t begin, size_t pos,
                            size_t end, size_t prop, uint32_t val) {
  size_t begin_pos = begin;
  size_t end_pos = pos;
  do {
    while (begin_pos < pos &&
           jxl_tree_samples_sample_property(tree_samples, prop, begin_pos) <= val) {
      ++begin_pos;
    }
    while (end_pos < end &&
           jxl_tree_samples_sample_property(tree_samples, prop, end_pos) > val) {
      ++end_pos;
    }
    if (begin_pos < pos && end_pos < end) {
      jxl_tree_samples_swap(tree_samples, begin_pos, end_pos);
    }
    ++begin_pos;
    ++end_pos;
  } while (begin_pos < pos && end_pos < end);
}

static void jxl_collect_extra_bits_increase_static(jxl_tree_samples *tree_samples,
                                    const jxl_array_residual_token *rtokens,
                                    jxl_array_int *count_increase,
                                    jxl_array_size *extra_bits_increase,
                                    size_t begin, size_t end, size_t prop_idx,
                                    size_t max_symbols) {
  for (size_t i2 = begin; i2 < end; i2++) {
    const jxl_residual_token *rt = jxl_array_at_const(rtokens, i2);
    size_t cnt = jxl_tree_samples_count(tree_samples, i2);
    size_t p = jxl_tree_samples_static_property(tree_samples, prop_idx, i2);
    size_t sym = rt->tok;
    size_t ebi = rt->nbits * cnt;
    *jxl_array_at(count_increase, p * max_symbols + sym) += cnt;
    *jxl_array_at(extra_bits_increase, p) += ebi;
  }
}

static void jxl_collect_extra_bits_increase_sample(jxl_tree_samples *tree_samples,
                                    const jxl_array_residual_token *rtokens,
                                    jxl_array_int *count_increase,
                                    jxl_array_size *extra_bits_increase,
                                    size_t begin, size_t end, size_t prop_idx,
                                    size_t max_symbols) {
  for (size_t i2 = begin; i2 < end; i2++) {
    const jxl_residual_token *rt = jxl_array_at_const(rtokens, i2);
    size_t cnt = jxl_tree_samples_count(tree_samples, i2);
    size_t p = jxl_tree_samples_sample_property(tree_samples, prop_idx, i2);
    size_t sym = rt->tok;
    size_t ebi = rt->nbits * cnt;
    *jxl_array_at(count_increase, p * max_symbols + sym) += cnt;
    *jxl_array_at(extra_bits_increase, p) += ebi;
  }
}

static void jxl_find_best_split(jxl_tree_samples *tree_samples, float threshold,
                   const jxl_array_modular_multiplier_info *mul_info,
                   jxl_static_prop_range initial_static_prop_range,
                   float fast_decode_multiplier, jxl_tree *tree){
  jxl_context* mm = tree_samples->sample_counts.ctx;
  jxl_array_ma_node_info nodes;
  jxl_array_construct_empty(&nodes, mm);
  {
    jxl_ma_node_info root;
    root.pos = 0;
    root.begin = 0;
    root.end = jxl_tree_samples_num_distinct_samples(tree_samples);
    root.static_prop_range = initial_static_prop_range;
    if (!jxl_enc_status_ok(jxl_array_ma_node_info_push_back(&nodes, root))) {
      JXL_CRASH();
    }
  }

  size_t num_predictors = jxl_tree_samples_num_predictors(tree_samples);
  size_t num_properties = jxl_tree_samples_num_properties(tree_samples);

  // TODO(veluca): consider parallelizing the search (processing multiple nodes
  // at a time).
  while (!jxl_array_empty(&nodes)) {
    size_t pos = jxl_array_back_ptr(&nodes)->pos;
    size_t begin = jxl_array_back_ptr(&nodes)->begin;
    size_t end = jxl_array_back_ptr(&nodes)->end;

    jxl_static_prop_range static_prop_range = jxl_array_back_ptr(&nodes)->static_prop_range;
    jxl_array_pop_back(&nodes);
    if (begin == end) continue;

    jxl_split_info best_split_static_constant;
    jxl_split_info_construct_empty(&best_split_static_constant);
    jxl_split_info best_split_static;
    jxl_split_info_construct_empty(&best_split_static);
    jxl_split_info best_split_nonstatic;
    jxl_split_info_construct_empty(&best_split_nonstatic);
    jxl_split_info best_split_nowp;
    jxl_split_info_construct_empty(&best_split_nowp);

    JXL_DASSERT(begin <= end);
    JXL_DASSERT(end <= jxl_tree_samples_num_distinct_samples(tree_samples));

    // Compute the maximum token in the range.
    size_t max_symbols = 0;
    for (size_t pred = 0; pred < num_predictors; pred++) {
      for (size_t i = begin; i < end; i++) {
        uint32_t tok = jxl_tree_samples_token(tree_samples, pred, i);
        max_symbols = max_symbols > tok + 1 ? max_symbols : tok + 1;
      }
    }
    jxl_array_i32 counts;
    jxl_array_construct_empty(&counts, mm);
    jxl_array_u32 tot_extra_bits;
    jxl_array_construct_empty(&tot_extra_bits, mm);
    if (!jxl_enc_status_ok(jxl_array_resize_zero(&counts, max_symbols * num_predictors)) ||
        !jxl_enc_status_ok(jxl_array_resize_zero(&tot_extra_bits, num_predictors))) {
      JXL_CRASH();
    }
    for (size_t pred = 0; pred < num_predictors; pred++) {
      size_t extra_bits = 0;
      const jxl_array_residual_token* rtokens = jxl_tree_samples_r_tokens(tree_samples, pred);
      for (size_t i = begin; i < end; i++) {
        const jxl_residual_token* rt = jxl_array_at_const(rtokens, i);
        size_t count = jxl_tree_samples_count(tree_samples, i);
        size_t eb = rt->nbits * count;
        *jxl_array_at(&counts, pred * max_symbols + rt->tok) += count;
        extra_bits += eb;
      }
      *jxl_array_at(&tot_extra_bits, pred) = extra_bits;
    }

    float base_bits;
    {
      size_t pred = jxl_tree_samples_predictor_index(tree_samples, jxl_array_at(tree, pos)->predictor);
      base_bits =
          jxl_estimate_bits(jxl_array_data(&counts) + pred * max_symbols, max_symbols) +
          *jxl_array_at(&tot_extra_bits, pred);
    }

    jxl_split_info *best = &best_split_nonstatic;

    jxl_split_info forced_split;
    jxl_split_info_construct_empty(&forced_split);
    // The multiplier ranges cut halfway through the current ranges of static
    // properties. We do this even if the current node is not a leaf, to
    // minimize the number of nodes in the resulting tree.
    for (size_t mmi_i = 0; mmi_i < jxl_array_len(mul_info); ++mmi_i) {
      const jxl_modular_multiplier_info* mmi = jxl_array_at_const(mul_info, mmi_i);
      uint32_t axis;
      uint32_t val;
      jxl_intersection_type t =
          jxl_box_intersects(static_prop_range, mmi->range, &axis, &val);
      if (t == kIntersectionNone) continue;
      if (t == kIntersectionInside) {
        jxl_array_at(tree, pos)->multiplier = mmi->multiplier;
        break;
      }
      if (t == kIntersectionPartial) {
        JXL_DASSERT(axis < kNumStaticProperties);
        forced_split.val = jxl_tree_samples_quantize_static_property(tree_samples, axis, val);
        forced_split.prop = axis;
        forced_split.lcost = forced_split.rcost = base_bits / 2 - threshold;
        forced_split.lpred = forced_split.rpred = jxl_array_at(tree, pos)->predictor;
        best = &forced_split;
        best->pos = begin;
        JXL_DASSERT(best->prop == jxl_tree_samples_property_from_index(tree_samples, best->prop));
        if (best->prop < jxl_tree_samples_num_static_props(tree_samples)) {
        for (size_t x = begin; x < end; x++) {
          if (jxl_tree_samples_static_property(tree_samples, best->prop, x) <= best->val) {
            best->pos++;
          }
        }
      } else {
        size_t prop = best->prop - jxl_tree_samples_num_static_props(tree_samples);
        for (size_t x = begin; x < end; x++) {
          if (jxl_tree_samples_sample_property(tree_samples, prop, x) <= best->val) {
            best->pos++;
          }
        }
      }
        break;
      }
    }

    if (best != &forced_split) {
      jxl_array_int prop_value_used_count;
      jxl_array_construct_empty(&prop_value_used_count, mm);
      jxl_array_int count_increase;
      jxl_array_construct_empty(&count_increase, mm);
      jxl_array_size extra_bits_increase;
      jxl_array_construct_empty(&extra_bits_increase, mm);
      // For each property, compute which of its values are used, and what
      // tokens correspond to those usages. Then, iterate through the values,
      // and compute the entropy of each side of the split (of the form `prop >
      // threshold`). Finally, find the split that minimizes the cost.
      jxl_array_ma_cost_info costs_l;
      jxl_array_construct_empty(&costs_l, mm);
      jxl_array_ma_cost_info costs_r;
      jxl_array_construct_empty(&costs_r, mm);

      jxl_array_i32 counts_above;
      jxl_array_construct_empty(&counts_above, mm);
      jxl_array_i32 counts_below;
      jxl_array_construct_empty(&counts_below, mm);
      if (!jxl_enc_status_ok(jxl_array_resize_zero(&counts_above, max_symbols)) ||
          !jxl_enc_status_ok(jxl_array_resize_zero(&counts_below, max_symbols))) {
        JXL_CRASH();
      }

      // The lower the threshold, the higher the expected noisiness of the
      // estimate. Thus, discourage changing predictors.
      float change_pred_penalty = 800.0f / (100.0f + threshold);
      for (size_t prop = 0; prop < num_properties && base_bits > threshold;
           prop++) {
jxl_array_clear(&costs_l);
jxl_array_clear(&costs_r);
        size_t prop_size = jxl_tree_samples_num_property_values(tree_samples, prop);
        if (jxl_array_len(&extra_bits_increase) < prop_size) {
          if (!jxl_enc_status_ok(jxl_array_resize_zero(&count_increase, prop_size * max_symbols)) ||
              !jxl_enc_status_ok(jxl_array_resize_zero(&extra_bits_increase, prop_size))) {
            JXL_CRASH();
          }
        }
        // Clear prop_value_used_count (which cannot be cleared "on the go")
jxl_array_clear(&prop_value_used_count);
        if (!jxl_enc_status_ok(jxl_array_resize_zero(&prop_value_used_count, prop_size))) JXL_CRASH();

        size_t first_used = prop_size;
        size_t last_used = 0;

        // TODO(veluca): consider finding multiple splits along a single
        // property at the same time, possibly with a bottom-up approach.
        if (prop < jxl_tree_samples_num_static_props(tree_samples)) {
          for (size_t i = begin; i < end; i++) {
            size_t p = jxl_tree_samples_static_property(tree_samples, prop, i);
            (*jxl_array_at(&prop_value_used_count, p))++;
            last_used = JXL_MAX(last_used, p);
            first_used = JXL_MIN(first_used, p);
          }
        } else {
          size_t prop_idx = prop - jxl_tree_samples_num_static_props(tree_samples);
          for (size_t i = begin; i < end; i++) {
            size_t p = jxl_tree_samples_sample_property(tree_samples, prop_idx, i);
            (*jxl_array_at(&prop_value_used_count, p))++;
            last_used = JXL_MAX(last_used, p);
            first_used = JXL_MIN(first_used, p);
          }
        }
        {
          jxl_ma_cost_info init;
          jxl_ma_cost_info_construct_empty(&init);
          if (!jxl_enc_status_ok(jxl_array_ma_cost_info_resize_fill(&costs_l, last_used - first_used, init)) ||
              !jxl_enc_status_ok(jxl_array_ma_cost_info_resize_fill(&costs_r, last_used - first_used, init))) {
            JXL_CRASH();
          }
        }
        // For all predictors, compute the right and left costs of each split.
        for (size_t pred = 0; pred < num_predictors; pred++) {
          // Compute cost and histogram increments for each property value.
          const jxl_array_residual_token *rtokens = jxl_tree_samples_r_tokens(tree_samples, pred);
          if (prop < jxl_tree_samples_num_static_props(tree_samples)) {
            jxl_collect_extra_bits_increase_static(tree_samples, rtokens,
                                           &count_increase, &extra_bits_increase,
                                           begin, end, prop, max_symbols);
          } else {
            jxl_collect_extra_bits_increase_sample(
                tree_samples, rtokens, &count_increase, &extra_bits_increase,
                begin, end, prop - jxl_tree_samples_num_static_props(tree_samples), max_symbols);
          }
          memcpy(jxl_array_data(&counts_above), jxl_array_data(&counts) + pred * max_symbols,
                 max_symbols * sizeof(*jxl_array_at(&counts_above, 0)));
          memset(jxl_array_data(&counts_below), 0, max_symbols * sizeof(*jxl_array_at(&counts_below, 0)));
          size_t extra_bits_below = 0;
          // Exclude last used: this ensures neither counts_above nor
          // counts_below is empty.
          for (size_t i = first_used; i < last_used; i++) {
            if (!*jxl_array_at(&prop_value_used_count, i)) continue;
            extra_bits_below += *jxl_array_at(&extra_bits_increase, i);
            // The increase for this property value has been used, and will not
            // be used again: clear it. Also below.
            *jxl_array_at(&extra_bits_increase, i) = 0;
            for (size_t sym = 0; sym < max_symbols; sym++) {
              *jxl_array_at(&counts_above, sym) -= *jxl_array_at(&count_increase, i * max_symbols + sym);
              *jxl_array_at(&counts_below, sym) += *jxl_array_at(&count_increase, i * max_symbols + sym);
              *jxl_array_at(&count_increase, i * max_symbols + sym) = 0;
            }
            float rcost = jxl_estimate_bits(jxl_array_data(&counts_above), max_symbols) +
                          *jxl_array_at(&tot_extra_bits, pred) - extra_bits_below;
            float lcost = jxl_estimate_bits(jxl_array_data(&counts_below), max_symbols) +
                          extra_bits_below;
            JXL_DASSERT(extra_bits_below <= *jxl_array_at(&tot_extra_bits, pred));
            float penalty = 0;
            // Never discourage moving away from the Weighted predictor.
            if (jxl_tree_samples_predictor_from_index(tree_samples, pred) !=
                    jxl_array_at(tree, pos)->predictor &&
                jxl_array_at(tree, pos)->predictor != kPredictorWeighted) {
              penalty = change_pred_penalty;
            }
            // If everything else is equal, disfavour Weighted (slower) and
            // favour Zero (faster if it's the only predictor used in a
            // group+channel combination)
            if (jxl_tree_samples_predictor_from_index(tree_samples, pred) == kPredictorWeighted) {
              penalty += 1e-8;
            }
            if (jxl_tree_samples_predictor_from_index(tree_samples, pred) == kPredictorZero) {
              penalty -= 1e-8;
            }
            if (rcost + penalty < jxl_ma_cost_info_cost(jxl_array_at(&costs_r, i - first_used))) {
              jxl_array_at(&costs_r, i - first_used)->cost = rcost;
              jxl_array_at(&costs_r, i - first_used)->extra_cost = penalty;
              jxl_array_at(&costs_r, i - first_used)->pred =
                  jxl_tree_samples_predictor_from_index(tree_samples, pred);
            }
            if (lcost + penalty < jxl_ma_cost_info_cost(jxl_array_at(&costs_l, i - first_used))) {
              jxl_array_at(&costs_l, i - first_used)->cost = lcost;
              jxl_array_at(&costs_l, i - first_used)->extra_cost = penalty;
              jxl_array_at(&costs_l, i - first_used)->pred =
                  jxl_tree_samples_predictor_from_index(tree_samples, pred);
            }
          }
        }
        // Iterate through the possible splits and find the one with minimum sum
        // of costs of the two sides.
        size_t split = begin;
        for (size_t i = first_used; i < last_used; i++) {
          if (!*jxl_array_at(&prop_value_used_count, i)) continue;
          split += *jxl_array_at(&prop_value_used_count, i);
          float rcost = jxl_array_at(&costs_r, i - first_used)->cost;
          float lcost = jxl_array_at(&costs_l, i - first_used)->cost;

          bool uses_wp = jxl_tree_samples_property_from_index(tree_samples, prop) == kWPProp ||
                         jxl_array_at(&costs_l, i - first_used)->pred == kPredictorWeighted ||
                         jxl_array_at(&costs_r, i - first_used)->pred == kPredictorWeighted;
          bool zero_entropy_side = rcost == 0 || lcost == 0;

          jxl_split_info *best_ref =
              jxl_tree_samples_property_from_index(tree_samples, prop) < kNumStaticProperties
                  ? (zero_entropy_side ? &best_split_static_constant
                                       : &best_split_static)
                  : (uses_wp ? &best_split_nonstatic : &best_split_nowp);
          if (lcost + rcost < jxl_split_info_cost(best_ref)) {
            best_ref->prop = prop;
            best_ref->val = i;
            best_ref->pos = split;
            best_ref->lcost = lcost;
            best_ref->lpred = jxl_array_at(&costs_l, i - first_used)->pred;
            best_ref->rcost = rcost;
            best_ref->rpred = jxl_array_at(&costs_r, i - first_used)->pred;
          }
        }
        // Clear extra_bits_increase and cost_increase for last_used.
        *jxl_array_at(&extra_bits_increase, last_used) = 0;
        for (size_t sym = 0; sym < max_symbols; sym++) {
          *jxl_array_at(&count_increase, last_used * max_symbols + sym) = 0;
        }
      }

      // Try to avoid introducing WP.
      if (jxl_split_info_cost(&best_split_nowp) + threshold < base_bits &&
          jxl_split_info_cost(&best_split_nowp) <= fast_decode_multiplier * jxl_split_info_cost(best)) {
        best = &best_split_nowp;
      }
      // Split along static props if possible and not significantly more
      // expensive.
      if (jxl_split_info_cost(&best_split_static) + threshold < base_bits &&
          jxl_split_info_cost(&best_split_static) <= fast_decode_multiplier * jxl_split_info_cost(best)) {
        best = &best_split_static;
      }
      // Split along static props to create constant nodes if possible.
      if (jxl_split_info_cost(&best_split_static_constant) + threshold < base_bits) {
        best = &best_split_static_constant;
      }
      jxl_array_destroy(&prop_value_used_count);
      jxl_array_destroy(&count_increase);
      jxl_array_destroy(&extra_bits_increase);
      jxl_array_destroy(&costs_l);
      jxl_array_destroy(&costs_r);
      jxl_array_destroy(&counts_above);
      jxl_array_destroy(&counts_below);
    }

    if (jxl_split_info_cost(best) + threshold < base_bits) {
      uint32_t p = jxl_tree_samples_property_from_index(tree_samples, best->prop);
      pixel_type dequant =
          jxl_tree_samples_unquantize_property(tree_samples, best->prop, best->val);
      // Split node and try to split children.
      jxl_make_split_node(pos, p, dequant, best->lpred, 0, best->rpred, 0, tree);
      // "Sort" according to winning property
      if (best->prop < jxl_tree_samples_num_static_props(tree_samples)) {
        jxl_split_tree_samples_static(tree_samples, begin, best->pos, end, best->prop,
                               best->val);
      } else {
        jxl_split_tree_samples_sample(tree_samples, begin, best->pos, end,
                               best->prop - jxl_tree_samples_num_static_props(tree_samples),
                               best->val);
      }
      jxl_static_prop_range new_sp_range = static_prop_range;
      if (p < kNumStaticProperties) {
        JXL_DASSERT((uint32_t)(dequant + 1) <= jxl_static_prop_range_at(&new_sp_range, p)[1]);
        jxl_static_prop_range_at(&new_sp_range, p)[1] = dequant + 1;
        JXL_DASSERT(jxl_static_prop_range_at(&new_sp_range, p)[0] < jxl_static_prop_range_at(&new_sp_range, p)[1]);
      }
      {
        jxl_ma_node_info child;
        child.pos = jxl_array_at(tree, pos)->rchild;
        child.begin = begin;
        child.end = best->pos;
        child.static_prop_range = new_sp_range;
        if (!jxl_enc_status_ok(jxl_array_ma_node_info_push_back(&nodes, child))) {
          JXL_CRASH();
        }
      }
      new_sp_range = static_prop_range;
      if (p < kNumStaticProperties) {
        JXL_DASSERT(jxl_static_prop_range_at(&new_sp_range, p)[0] <= (uint32_t)(dequant + 1));
        jxl_static_prop_range_at(&new_sp_range, p)[0] = dequant + 1;
        JXL_DASSERT(jxl_static_prop_range_at(&new_sp_range, p)[0] < jxl_static_prop_range_at(&new_sp_range, p)[1]);
      }
      {
        jxl_ma_node_info child;
        child.pos = jxl_array_at(tree, pos)->lchild;
        child.begin = best->pos;
        child.end = end;
        child.static_prop_range = new_sp_range;
        if (!jxl_enc_status_ok(jxl_array_ma_node_info_push_back(&nodes, child))) {
          JXL_CRASH();
        }
      }
    }
    jxl_array_destroy(&counts);
    jxl_array_destroy(&tot_extra_bits);
  }
  jxl_array_destroy(&nodes);
}

jxl_enc_status jxl_compute_best_tree(jxl_tree_samples *tree_samples, float threshold,
                       const jxl_array_modular_multiplier_info *mul_info,
                       jxl_static_prop_range static_prop_range,
                       float fast_decode_multiplier, jxl_tree *tree){
  // TODO(veluca): take into account that different contexts can have different
  // uint configs.
  //
  // Initialize tree.
  if (!jxl_enc_status_ok(jxl_array_property_decision_node_push_back(tree, jxl_property_decision_node_leaf(
          jxl_tree_samples_predictor_from_index(tree_samples, 0), 0, 1)))) JXL_CRASH();
  JXL_ENSURE(jxl_tree_samples_num_properties(tree_samples) < 64);

  JXL_ENSURE(jxl_tree_samples_num_distinct_samples(tree_samples) <=
             UINT32_MAX);
  jxl_find_best_split(tree_samples, threshold, mul_info, static_prop_range,
                fast_decode_multiplier, tree);
  return jxl_enc_ok_status();
}

jxl_enc_status jxl_tree_samples_set_predictor(jxl_tree_samples* self, jxl_enc_predictor predictor,
                                 jxl_modular_tree_mode wp_tree_mode) {
  self->num_samples = 0;
  for (size_t i = 0; i < kNumModularPredictors; ++i) {
    jxl_array_destroy(&self->residuals[i]);
    jxl_array_construct_empty(&self->residuals[i], self->sample_counts.ctx);
  }
  if (wp_tree_mode == kWPOnly) {
jxl_array_clear(&self->predictors);
if (!jxl_enc_status_ok(jxl_array_predictor_push_back(&self->predictors, kPredictorWeighted))) JXL_CRASH();
return jxl_enc_ok_status();
  }
  if (wp_tree_mode == kNoWP &&
      predictor == kPredictorWeighted) {
    return JXL_FAILURE("Invalid predictor settings");
  }
jxl_array_clear(&self->predictors);
  if (predictor == kPredictorVariable) {
    for (size_t i = 0; i < kNumModularPredictors; i++) {
if (!jxl_enc_status_ok(jxl_array_predictor_push_back(&self->predictors, (jxl_enc_predictor)(i)))) JXL_CRASH();
}
    jxl_swap_predictor(jxl_array_at(&self->predictors, 0), jxl_array_at(&self->predictors, (int)(kPredictorWeighted)));
    jxl_swap_predictor(jxl_array_at(&self->predictors, 1), jxl_array_at(&self->predictors, (int)(kPredictorGradient)));
  } else if (predictor == kPredictorBest) {
if (!jxl_enc_status_ok(jxl_array_predictor_push_back(&self->predictors, kPredictorWeighted))) JXL_CRASH();
if (!jxl_enc_status_ok(jxl_array_predictor_push_back(&self->predictors, kPredictorGradient))) JXL_CRASH();
} else {
if (!jxl_enc_status_ok(jxl_array_predictor_push_back(&self->predictors, predictor))) JXL_CRASH();
}
  if (wp_tree_mode == kNoWP) {
    jxl_enc_predictor* dest = jxl_array_data(&self->predictors);
    for (jxl_enc_predictor* p = jxl_array_data(&self->predictors); p != jxl_array_data(&self->predictors) + jxl_array_len(&self->predictors); ++p) {
      if (*p != kPredictorWeighted) {
        if (dest != p) *dest = *p;
        ++dest;
      }
    }
    jxl_array_erase(&self->predictors, dest, jxl_array_data(&self->predictors) + jxl_array_len(&self->predictors));
  }
  JXL_ENSURE(jxl_array_len(&self->predictors) <= kNumModularPredictors);
  return jxl_enc_ok_status();
}

jxl_enc_status jxl_tree_samples_set_properties(jxl_tree_samples* self, const uint32_t* properties,
                                  size_t num_properties,
                                  jxl_modular_tree_mode wp_tree_mode) {
  for (size_t i = 0; i < kMaxSplittingHeuristicsProperties; ++i) {
    jxl_array_destroy(&self->props[i]);
    jxl_array_construct_empty(&self->props[i], self->sample_counts.ctx);
    jxl_array_destroy(&self->compact_properties[i]);
    jxl_array_construct_empty(&self->compact_properties[i], self->sample_counts.ctx);
    jxl_array_destroy(&self->property_mapping[i]);
    jxl_array_construct_empty(&self->property_mapping[i], self->sample_counts.ctx);
  }
  if (!jxl_enc_status_ok(jxl_array_assign(&self->props_to_use, properties, num_properties))) {
    return JXL_FAILURE("OOM");
  }
  if (wp_tree_mode == kWPOnly) {
jxl_array_clear(&self->props_to_use);
if (!jxl_enc_status_ok(jxl_array_u32_push_back(&self->props_to_use, (uint32_t)(kWPProp)))) JXL_CRASH();
}
  if (wp_tree_mode == kNoWP) {
    jxl_array_erase(&self->props_to_use,
               jxl_u32_remove(jxl_array_data(&self->props_to_use),
                         jxl_array_data(&self->props_to_use) + jxl_array_len(&self->props_to_use),
                         (uint32_t)(kWPProp)),
               jxl_array_data(&self->props_to_use) + jxl_array_len(&self->props_to_use));
  }
  if (jxl_array_empty(&self->props_to_use)) {
    return JXL_FAILURE("Invalid property set configuration");
  }
  JXL_ENSURE(jxl_array_len(&self->props_to_use) <= kMaxSplittingHeuristicsProperties);
  self->num_static_props = 0;
  // Check that if static properties present, then those are at the beginning.
  for (size_t i = 0; i < jxl_array_len(&self->props_to_use); ++i) {
    uint32_t prop = *jxl_array_at(&self->props_to_use, i);
    if (prop < kNumStaticProperties) {
      JXL_DASSERT(i == prop);
      self->num_static_props++;
    }
  }
  return jxl_enc_ok_status();
}

void jxl_tree_samples_init_table(jxl_tree_samples* self, size_t log_size) {
  size_t size = 1ULL << log_size;
  if (jxl_array_len(&self->dedup_table_) == size) return;
  if (!jxl_enc_status_ok(jxl_array_u32_resize_fill(&self->dedup_table_, size, kTreeSamplesDedupEntryUnused))) JXL_CRASH();
  for (size_t i = 0; i < jxl_tree_samples_num_distinct_samples(self); i++) {
    if (*jxl_array_at(&self->sample_counts, i) != UINT16_MAX) {
      jxl_tree_samples_add_to_table(self, i);
    }
  }
}

bool jxl_tree_samples_add_to_table_and_merge(jxl_tree_samples* self, size_t a) {
  size_t pos1 = jxl_tree_samples_hash1(self, a);
  size_t pos2 = jxl_tree_samples_hash2(self, a);
  if (*jxl_array_at(&self->dedup_table_, pos1) != kTreeSamplesDedupEntryUnused &&
      jxl_tree_samples_is_same_sample(self, a, *jxl_array_at(&self->dedup_table_, pos1))) {
    JXL_DASSERT(*jxl_array_at(&self->sample_counts, a) == 1);
    (*jxl_array_at(&self->sample_counts, *jxl_array_at(&self->dedup_table_, pos1)))++;
    // Remove from hash table samples that are saturated.
    if (*jxl_array_at(&self->sample_counts, *jxl_array_at(&self->dedup_table_, pos1)) ==
        UINT16_MAX) {
      *jxl_array_at(&self->dedup_table_, pos1) = kTreeSamplesDedupEntryUnused;
    }
    return true;
  }
  if (*jxl_array_at(&self->dedup_table_, pos2) != kTreeSamplesDedupEntryUnused &&
      jxl_tree_samples_is_same_sample(self, a, *jxl_array_at(&self->dedup_table_, pos2))) {
    JXL_DASSERT(*jxl_array_at(&self->sample_counts, a) == 1);
    (*jxl_array_at(&self->sample_counts, *jxl_array_at(&self->dedup_table_, pos2)))++;
    // Remove from hash table samples that are saturated.
    if (*jxl_array_at(&self->sample_counts, *jxl_array_at(&self->dedup_table_, pos2)) ==
        UINT16_MAX) {
      *jxl_array_at(&self->dedup_table_, pos2) = kTreeSamplesDedupEntryUnused;
    }
    return true;
  }
  jxl_tree_samples_add_to_table(self, a);
  return false;
}

void jxl_tree_samples_add_to_table(jxl_tree_samples* self, size_t a) {
  size_t pos1 = jxl_tree_samples_hash1(self, a);
  size_t pos2 = jxl_tree_samples_hash2(self, a);
  if (*jxl_array_at(&self->dedup_table_, pos1) == kTreeSamplesDedupEntryUnused) {
    *jxl_array_at(&self->dedup_table_, pos1) = a;
  } else if (*jxl_array_at(&self->dedup_table_, pos2) == kTreeSamplesDedupEntryUnused) {
    *jxl_array_at(&self->dedup_table_, pos2) = a;
  }
}

void jxl_tree_samples_prepare_for_samples(jxl_tree_samples* self, size_t extra_num_samples) {
  for (size_t i = 0; i < jxl_array_len(&self->predictors); ++i) {
    if (!jxl_enc_status_ok(jxl_array_reserve(&self->residuals[i], jxl_array_len(&self->residuals[i]) + extra_num_samples))) {
      JXL_CRASH();
    }
  }
  for (size_t i = 0; i < self->num_static_props; ++i) {
    if (!jxl_enc_status_ok(jxl_array_reserve(&self->static_props[i],
                      jxl_array_len(&self->static_props[i]) + extra_num_samples))) {
      JXL_CRASH();
    }
  }
  for (size_t i = 0; i < jxl_tree_samples_num_sample_props(self); ++i) {
    if (!jxl_enc_status_ok(jxl_array_reserve(&self->props[i], jxl_array_len(&self->props[i]) + extra_num_samples))) {
      JXL_CRASH();
    }
  }
  size_t total_num_samples = extra_num_samples + jxl_array_len(&self->sample_counts);
  size_t next_size = jxl_ceil_log2_nonzero64(total_num_samples * 3 / 2);
  jxl_tree_samples_init_table(self, next_size);
}

size_t jxl_tree_samples_hash1(const jxl_tree_samples* self, size_t a) {
  const uint64_t constant = 0x1e35a7bd;
  uint64_t h = constant;
  for (size_t i = 0; i < jxl_array_len(&self->predictors); ++i) {
    h = h * constant + jxl_array_at_const(&self->residuals[i], a)->tok;
    h = h * constant + jxl_array_at_const(&self->residuals[i], a)->nbits;
  }
  for (size_t i = 0; i < self->num_static_props; ++i) {
    h = h * constant + *jxl_array_at_const(&self->static_props[i], a);
  }
  for (size_t i = 0; i < jxl_tree_samples_num_sample_props(self); ++i) {
    h = h * constant + *jxl_array_at_const(&self->props[i], a);
  }
  return (h >> 16) & (jxl_array_len(&self->dedup_table_) - 1);
}
size_t jxl_tree_samples_hash2(const jxl_tree_samples* self, size_t a) {
  const uint64_t constant = 0x1e35a7bd1e35a7bd;
  uint64_t h = constant;
  for (size_t i = 0; i < self->num_static_props; ++i) {
    h = h * constant ^ *jxl_array_at_const(&self->static_props[i], a);
  }
  for (size_t i = 0; i < jxl_tree_samples_num_sample_props(self); ++i) {
    h = h * constant ^ *jxl_array_at_const(&self->props[i], a);
  }
  for (size_t i = 0; i < jxl_array_len(&self->predictors); ++i) {
    h = h * constant ^ jxl_array_at_const(&self->residuals[i], a)->tok;
    h = h * constant ^ jxl_array_at_const(&self->residuals[i], a)->nbits;
  }
  return (h >> 16) & (jxl_array_len(&self->dedup_table_) - 1);
}

bool jxl_tree_samples_is_same_sample(const jxl_tree_samples* self, size_t a, size_t b) {
  bool ret = true;
  for (size_t i = 0; i < jxl_array_len(&self->predictors); ++i) {
    if (jxl_array_at_const(&self->residuals[i], a)->tok != jxl_array_at_const(&self->residuals[i], b)->tok) {
      ret = false;
    }
    if (jxl_array_at_const(&self->residuals[i], a)->nbits != jxl_array_at_const(&self->residuals[i], b)->nbits) {
      ret = false;
    }
  }
  for (size_t i = 0; i < self->num_static_props; ++i) {
    if (*jxl_array_at_const(&self->static_props[i], a) != *jxl_array_at_const(&self->static_props[i], b)) {
      ret = false;
    }
  }
  for (size_t i = 0; i < jxl_tree_samples_num_sample_props(self); ++i) {
    if (*jxl_array_at_const(&self->props[i], a) != *jxl_array_at_const(&self->props[i], b)) {
      ret = false;
    }
  }
  return ret;
}

void jxl_tree_samples_add_sample(jxl_tree_samples* self, pixel_type_w pixel, const jxl_properties *properties,
                            const pixel_type_w *predictions){
  for (size_t i = 0; i < jxl_array_len(&self->predictors); i++) {
    pixel_type v = pixel - predictions[(int)(*jxl_array_at(&self->predictors, i))];
    uint32_t tok, nbits, bits;
    jxl_hybrid_uint_config_encode(jxl_hybrid_uint_config_make(4, 1, 2), jxl_pack_signed(v), &tok, &nbits, &bits);
    JXL_DASSERT(tok < 256);
    JXL_DASSERT(nbits < 256);
    jxl_residual_token token = {(uint8_t)(tok),
                           (uint8_t)(nbits)};
if (!jxl_enc_status_ok(jxl_array_residual_token_push_back(&self->residuals[i], token))) JXL_CRASH();
}
  for (size_t i = 0; i < self->num_static_props; ++i) {
if (!jxl_enc_status_ok(jxl_array_u32_push_back(&self->static_props[i], jxl_tree_samples_quantize_static_property(self, i, *jxl_array_at_const(properties, i))))) JXL_CRASH();
}
  for (size_t i = self->num_static_props; i < jxl_array_len(&self->props_to_use); i++) {
if (!jxl_enc_status_ok(jxl_array_u8_push_back(&self->props[i - self->num_static_props], jxl_tree_samples_quantize_property(self, i, *jxl_array_at_const(properties, *jxl_array_at(&self->props_to_use, i)))))) JXL_CRASH();
}
if (!jxl_enc_status_ok(jxl_array_u16_push_back(&self->sample_counts, 1))) JXL_CRASH();
self->num_samples++;
  if (jxl_tree_samples_add_to_table_and_merge(self, jxl_array_len(&self->sample_counts) - 1)) {
    for (size_t i = 0; i < jxl_array_len(&self->predictors); ++i) jxl_array_pop_back(&self->residuals[i]);
    for (size_t i = 0; i < self->num_static_props; ++i) jxl_array_pop_back(&self->static_props[i]);
    for (size_t i = 0; i < jxl_tree_samples_num_sample_props(self); ++i) jxl_array_pop_back(&self->props[i]);
    jxl_array_pop_back(&self->sample_counts);
  }
}

void jxl_tree_samples_swap(jxl_tree_samples* self, size_t a, size_t b) {
  if (a == b) return;
  for (size_t i = 0; i < jxl_array_len(&self->predictors); ++i) {
    jxl_swap_residual_token(jxl_array_at(&self->residuals[i], a), jxl_array_at(&self->residuals[i], b));
  }
  for (size_t i = 0; i < self->num_static_props; ++i) {
    jxl_swap(jxl_array_at(&self->static_props[i], a), jxl_array_at(&self->static_props[i], b));
  }
  for (size_t i = 0; i < jxl_tree_samples_num_sample_props(self); ++i) {
    jxl_swap(jxl_array_at(&self->props[i], a), jxl_array_at(&self->props[i], b));
  }
  jxl_swap(jxl_array_at(&self->sample_counts, a), jxl_array_at(&self->sample_counts, b));
}

static void jxl_quantize_histogram(const jxl_array_u32 *histogram, size_t num_chunks,
                       jxl_array_i32 *thresholds) {
  jxl_array_clear(thresholds);
  if (jxl_array_empty(histogram) || num_chunks == 0) return;
  uint64_t sum = jxl_u32_accumulate(jxl_array_data_const(histogram),
                              jxl_array_data_const(histogram) + jxl_array_len(histogram), 0LU);
  if (sum == 0) return;
  // TODO(veluca): selecting distinct quantiles is likely not the best
  // way to go about this.
  uint64_t cumsum = 0;
  uint64_t threshold = 1;
  for (size_t i = 0; i < jxl_array_len(histogram); i++) {
    cumsum += *jxl_array_at_const(histogram, i);
    if (cumsum * num_chunks >= threshold * sum) {
if (!jxl_enc_status_ok(jxl_array_i32_push_back(thresholds, (int32_t)(i)))) JXL_CRASH();
while (cumsum * num_chunks >= threshold * sum) threshold++;
    }
  }
  JXL_DASSERT(jxl_array_len(thresholds) <= num_chunks);
  // last value collects all histogram and is not really a threshold
jxl_array_pop_back(thresholds);
}

static void jxl_quantize_samples(const jxl_array_i32 *samples, size_t num_chunks,
                     jxl_array_i32 *thresholds) {
  jxl_array_clear(thresholds);
  if (jxl_array_empty(samples)) return;
  int min = jxl_i32_min_element(samples);
  const int kRange = 512;
  min = jxl_clamp1_i(min, -kRange, kRange);
  jxl_array_u32 counts;
  jxl_array_construct_empty(&counts, samples->ctx);
  if (!jxl_enc_status_ok(jxl_array_resize_zero(&counts, 2 * kRange + 1))) JXL_CRASH();
  for (size_t s_i = 0; s_i < jxl_array_len(samples); ++s_i) {
    int s = *jxl_array_at_const(samples, s_i);
    uint32_t sample_offset = jxl_clamp1_i(s, -kRange, kRange) - min;
    (*jxl_array_at(&counts, sample_offset))++;
  }
  jxl_quantize_histogram(&counts, num_chunks, thresholds);
  jxl_array_destroy(&counts);
  for (size_t v_i = 0; v_i < jxl_array_len(thresholds); ++v_i) {
    *jxl_array_at(thresholds, v_i) += min;
  }
}

// `to[i]` is assigned value `v` conforming `from[v] <= i && from[v-1] > i`.
// This is because the decision node in the tree splits on (property) > i,
// hence everything that is not > of a threshold should be clustered
// together.
static void jxl_quant_map_u16(const jxl_array_i32 *from, jxl_array_u16 *to,
                 size_t num_pegs, int bias) {
  if (!jxl_enc_status_ok(jxl_array_resize_zero(to, num_pegs))) JXL_CRASH();
  size_t mapped = 0;
  for (size_t i = 0; i < num_pegs; i++) {
    while (mapped < jxl_array_len(from) && (int)(i) - bias > *jxl_array_at_const(from, mapped)) {
      mapped++;
    }
    JXL_DASSERT((uint16_t)(mapped) == mapped);
    *jxl_array_at(to, i) = mapped;
  }
}

static void jxl_quant_map_u8(const jxl_array_i32 *from, jxl_array_u8 *to, size_t num_pegs,
                int bias) {
  if (!jxl_enc_status_ok(jxl_array_resize_zero(to, num_pegs))) JXL_CRASH();
  size_t mapped = 0;
  for (size_t i = 0; i < num_pegs; i++) {
    while (mapped < jxl_array_len(from) && (int)(i) - bias > *jxl_array_at_const(from, mapped)) {
      mapped++;
    }
    JXL_DASSERT((uint8_t)(mapped) == mapped);
    *jxl_array_at(to, i) = mapped;
  }
}

static void jxl_quantize_channel_thresholds(const jxl_array_i32 *multiplier_thresholds,
                               const jxl_array_u32 *pixel_count,
                               size_t max_property_values, jxl_array_i32 *out) {
  if (!jxl_array_empty(multiplier_thresholds)) {
    if (!jxl_enc_status_ok(jxl_array_copy_from(out, multiplier_thresholds))) JXL_CRASH();
    return;
  }
  jxl_quantize_histogram(pixel_count, max_property_values, out);
}

static void jxl_quantize_coordinate_thresholds(size_t max_property_values, jxl_array_i32 *out) {
  jxl_array_clear(out);
  if (!jxl_enc_status_ok(jxl_array_reserve(out, max_property_values - 1))) JXL_CRASH();
  for (size_t i = 0; i + 1 < max_property_values; i++) {
    if (!jxl_enc_status_ok(jxl_array_i32_push_back(out,
                       (int32_t)((i + 1) * 256 / max_property_values - 1)))) {
      JXL_CRASH();
    }
  }
}

static void jxl_quantize_wp_thresholds(size_t max_property_values, jxl_array_i32 *out) {
  if (max_property_values < 32) {
    static const int32_t kVals[] = {-127, -63, -31, -15, -7, -3, -1, 0,
                                    1,    3,   7,   15,  31, 63, 127};
    if (!jxl_enc_status_ok(jxl_array_assign(out, kVals, sizeof(kVals) / sizeof(kVals[0])))) {
      JXL_CRASH();
    }
    return;
  }
  if (max_property_values < 64) {
    static const int32_t kVals[] = {
        -255, -191, -127, -95, -63, -47, -31, -23, -15, -11, -7,
        -5,   -3,   -1,   0,   1,   3,   5,   7,   11,  15,  23,
        31,   47,   63,   95,  127, 191, 255};
    if (!jxl_enc_status_ok(jxl_array_assign(out, kVals, sizeof(kVals) / sizeof(kVals[0])))) {
      JXL_CRASH();
    }
    return;
  }
  static const int32_t kVals[] = {
      -255, -223, -191, -159, -127, -111, -95, -79, -63, -55, -47,
      -39,  -31,  -27,  -23,  -19,  -15,  -13, -11, -9,  -7,  -6,
      -5,   -4,   -3,   -2,   -1,   0,    1,   2,   3,   4,   5,
      6,    7,    9,    11,   13,   15,   19,  23,  27,  31,  39,
      47,   55,   63,   79,   95,   111,  127, 159, 191, 223, 255};
  if (!jxl_enc_status_ok(jxl_array_assign(out, kVals, sizeof(kVals) / sizeof(kVals[0])))) {
    JXL_CRASH();
  }
}

typedef struct jxl_sample_threshold_cache {
  jxl_array_i32 thresholds;
  jxl_array_i32 abs_thresholds;

} jxl_sample_threshold_cache;

static void jxl_sample_threshold_cache_destroy(jxl_sample_threshold_cache* cache) {
  if (cache == NULL) return;
  jxl_array_destroy(&cache->thresholds);
  jxl_array_destroy(&cache->abs_thresholds);
}

static void jxl_sample_threshold_cache_construct_empty(
    jxl_sample_threshold_cache* cache, jxl_context* mm) {
  jxl_array_construct_empty(&cache->thresholds, mm);
  jxl_array_construct_empty(&cache->abs_thresholds, mm);
}

static void jxl_cached_sample_thresholds(jxl_sample_threshold_cache *cache, jxl_array_i32 *samples,
                            size_t max_property_values, jxl_array_i32 *out) {
  if (jxl_array_empty(&cache->thresholds)) {
    jxl_quantize_samples(samples, max_property_values, &cache->thresholds);
  }
  if (!jxl_enc_status_ok(jxl_array_copy_from(out, &cache->thresholds))) JXL_CRASH();
}

static void jxl_cached_abs_sample_thresholds(jxl_sample_threshold_cache *cache, jxl_array_i32 *samples,
                               size_t max_property_values, jxl_array_i32 *out) {
  if (jxl_array_empty(&cache->abs_thresholds)) {
    // Populate non-abs thresholds first (same order as before).
    if (jxl_array_empty(&cache->thresholds)) {
      jxl_quantize_samples(samples, max_property_values, &cache->thresholds);
    }
    for (size_t i = 0; i < jxl_array_len(samples); ++i) {
      *jxl_array_at(samples, i) = JXL_ABS(*jxl_array_at(samples, i));
    }
    jxl_quantize_samples(samples, max_property_values, &cache->abs_thresholds);
  }
  if (!jxl_enc_status_ok(jxl_array_copy_from(out, &cache->abs_thresholds))) JXL_CRASH();
}

static void jxl_advance_sample_pos(const jxl_image *image, const jxl_array_size *channel_ids,
                      size_t *i, size_t *y, size_t *x, size_t amount){
  *x += amount;
  // Detect row overflow (rare).
  while (*x >= jxl_channels_at_const(&image->channel, *jxl_array_at_const(channel_ids, *i))->w) {
    *x -= jxl_channels_at_const(&image->channel, *jxl_array_at_const(channel_ids, *i))->w;
    (*y)++;
    // Detect end-of-channel (even rarer).
    if (*y == jxl_channels_at_const(&image->channel, *jxl_array_at_const(channel_ids, *i))->h) {
      (*i)++;
      *y = 0;
      if (*i >= jxl_array_len(channel_ids)) {
        return;
      }
    }
  }
}

void jxl_tree_samples_pre_quantize_properties(jxl_tree_samples* self, 
    const jxl_static_prop_range *range,
    const jxl_array_modular_multiplier_info *multiplier_info,
    const jxl_array_u32 *group_pixel_count,
    const jxl_array_u32 *channel_pixel_count,
    jxl_array_i32 *pixel_samples, jxl_array_i32 *diff_samples,
    size_t max_property_values){
  // If we have forced splits because of multipliers, choose channel and group
  // thresholds accordingly.
  jxl_context* mm = self->sample_counts.ctx;
  jxl_array_i32 group_multiplier_thresholds;
  jxl_array_construct_empty(&group_multiplier_thresholds, mm);
  jxl_array_i32 channel_multiplier_thresholds;
  jxl_array_construct_empty(&channel_multiplier_thresholds, mm);
  for (size_t v_i = 0; v_i < jxl_array_len(multiplier_info); ++v_i) {
    const jxl_modular_multiplier_info* v = jxl_array_at_const(multiplier_info, v_i);
    if (jxl_static_prop_range_row_const(&v->range, 0)[0] != jxl_static_prop_range_row_const(range, 0)[0]) {
if (!jxl_enc_status_ok(jxl_array_i32_push_back(&channel_multiplier_thresholds, jxl_static_prop_range_row_const(&v->range, 0)[0] - 1))) JXL_CRASH();
}
    if (jxl_static_prop_range_row_const(&v->range, 0)[1] != jxl_static_prop_range_row_const(range, 0)[1]) {
if (!jxl_enc_status_ok(jxl_array_i32_push_back(&channel_multiplier_thresholds, jxl_static_prop_range_row_const(&v->range, 0)[1] - 1))) JXL_CRASH();
}
    if (jxl_static_prop_range_row_const(&v->range, 1)[0] != jxl_static_prop_range_row_const(range, 1)[0]) {
if (!jxl_enc_status_ok(jxl_array_i32_push_back(&group_multiplier_thresholds, jxl_static_prop_range_row_const(&v->range, 1)[0] - 1))) JXL_CRASH();
}
    if (jxl_static_prop_range_row_const(&v->range, 1)[1] != jxl_static_prop_range_row_const(range, 1)[1]) {
if (!jxl_enc_status_ok(jxl_array_i32_push_back(&group_multiplier_thresholds, jxl_static_prop_range_row_const(&v->range, 1)[1] - 1))) JXL_CRASH();
}
  }
  jxl_i32_sort(&channel_multiplier_thresholds);
  if (!jxl_enc_status_ok(jxl_array_resize_zero(&channel_multiplier_thresholds,
                       jxl_i32_unique_in_array(&channel_multiplier_thresholds)))) {
    JXL_CRASH();
  }
  jxl_i32_sort(&group_multiplier_thresholds);
  if (!jxl_enc_status_ok(jxl_array_resize_zero(&group_multiplier_thresholds,
                       jxl_i32_unique_in_array(&group_multiplier_thresholds)))) {
    JXL_CRASH();
  }

  for (size_t i = 0; i < kMaxSplittingHeuristicsProperties; ++i) {
    jxl_array_destroy(&self->compact_properties[i]);
    jxl_array_construct_empty(&self->compact_properties[i], self->sample_counts.ctx);
  }
  jxl_sample_threshold_cache pixel_cache;
  jxl_sample_threshold_cache_construct_empty(&pixel_cache, mm);
  jxl_sample_threshold_cache diff_cache;
  jxl_sample_threshold_cache_construct_empty(&diff_cache, mm);

  for (size_t i = 0; i < kMaxSplittingHeuristicsProperties; ++i) {
    jxl_array_destroy(&self->property_mapping[i]);
    jxl_array_construct_empty(&self->property_mapping[i], self->sample_counts.ctx);
  }
  for (size_t i = 0; i < jxl_array_len(&self->props_to_use); i++) {
    if (*jxl_array_at(&self->props_to_use, i) == 0) {
      jxl_quantize_channel_thresholds(&channel_multiplier_thresholds,
                                channel_pixel_count, max_property_values,
                                &self->compact_properties[i]);
    } else if (*jxl_array_at(&self->props_to_use, i) == 1) {
      jxl_quantize_channel_thresholds(&group_multiplier_thresholds,
                                group_pixel_count, max_property_values,
                                &self->compact_properties[i]);
    } else if (*jxl_array_at(&self->props_to_use, i) == 2 || *jxl_array_at(&self->props_to_use, i) == 3) {
      jxl_quantize_coordinate_thresholds(max_property_values,
                                   &self->compact_properties[i]);
    } else if (*jxl_array_at(&self->props_to_use, i) == 6 || *jxl_array_at(&self->props_to_use, i) == 7 ||
               *jxl_array_at(&self->props_to_use, i) == 8 ||
               (*jxl_array_at(&self->props_to_use, i) >= kNumNonrefProperties &&
                (*jxl_array_at(&self->props_to_use, i) - kNumNonrefProperties) % 4 == 1)) {
      jxl_cached_sample_thresholds(&pixel_cache, pixel_samples, max_property_values,
                             &self->compact_properties[i]);
    } else if (*jxl_array_at(&self->props_to_use, i) == 4 || *jxl_array_at(&self->props_to_use, i) == 5 ||
               (*jxl_array_at(&self->props_to_use, i) >= kNumNonrefProperties &&
                (*jxl_array_at(&self->props_to_use, i) - kNumNonrefProperties) % 4 == 0)) {
      jxl_cached_abs_sample_thresholds(&pixel_cache, pixel_samples,
                                max_property_values,
                                &self->compact_properties[i]);
    } else if (*jxl_array_at(&self->props_to_use, i) >= kNumNonrefProperties &&
               (*jxl_array_at(&self->props_to_use, i) - kNumNonrefProperties) % 4 == 2) {
      jxl_cached_abs_sample_thresholds(&diff_cache, diff_samples, max_property_values,
                                &self->compact_properties[i]);
    } else if (*jxl_array_at(&self->props_to_use, i) == kWPProp) {
      jxl_quantize_wp_thresholds(max_property_values, &self->compact_properties[i]);
    } else {
      jxl_cached_sample_thresholds(&diff_cache, diff_samples, max_property_values,
                             &self->compact_properties[i]);
    }
    if (i < self->num_static_props) {
      jxl_quant_map_u16(&self->compact_properties[i], &self->static_property_mapping[i],
                  kTreeSamplesPropertyRange * 2 + 1, kTreeSamplesPropertyRange);
    } else {
      jxl_quant_map_u8(&self->compact_properties[i], &self->property_mapping[i - self->num_static_props],
                 kTreeSamplesPropertyRange * 2 + 1, kTreeSamplesPropertyRange);
    }
  }
  jxl_sample_threshold_cache_destroy(&pixel_cache);
  jxl_sample_threshold_cache_destroy(&diff_cache);
  jxl_array_destroy(&group_multiplier_thresholds);
  jxl_array_destroy(&channel_multiplier_thresholds);
}

void jxl_collect_pixel_samples(const jxl_image *image, const jxl_modular_options *options,
                         uint32_t group_id, jxl_array_u32 *group_pixel_count,
                         jxl_array_u32 *channel_pixel_count,
                         jxl_array_i32 *pixel_samples, jxl_array_i32 *diff_samples){
  if (options->nb_repeats == 0) return;
  if (jxl_array_len(group_pixel_count) <= group_id) {
    if (!jxl_enc_status_ok(jxl_array_resize_zero(group_pixel_count, group_id + 1))) JXL_CRASH();
  }
  if (jxl_array_len(channel_pixel_count) < jxl_channels_size(&image->channel)) {
    if (!jxl_enc_status_ok(jxl_array_resize_zero(channel_pixel_count, jxl_channels_size(&image->channel)))) {
      JXL_CRASH();
    }
  }
  jxl_rng rng = jxl_rng_make(group_id);
  // Sample 10% of the final number of samples for property quantization.
  float fraction = JXL_MIN(options->nb_repeats * 0.1, 0.99);
  jxl_rng_geometric_distribution dist = jxl_rng_make_geometric(fraction);
  size_t total_pixels = 0;
  jxl_array_size channel_ids;
  jxl_array_construct_empty(&channel_ids, pixel_samples->ctx);
  for (size_t i = 0; i < jxl_channels_size(&image->channel); i++) {
    if (i >= image->nb_meta_channels &&
        (jxl_channels_at_const(&image->channel, i)->w > options->max_chan_size ||
         jxl_channels_at_const(&image->channel, i)->h > options->max_chan_size)) {
      break;
    }
    if (jxl_channels_at_const(&image->channel, i)->w <= 1 || jxl_channels_at_const(&image->channel, i)->h == 0) {
      continue;  // skip empty or width-1 channels.
    }
    if (!jxl_enc_status_ok(jxl_array_size_push_back(&channel_ids, i))) JXL_CRASH();
    *jxl_array_at(group_pixel_count, group_id) +=
        jxl_channels_at_const(&image->channel, i)->w *
        jxl_channels_at_const(&image->channel, i)->h;
    *jxl_array_at(channel_pixel_count, i) +=
        jxl_channels_at_const(&image->channel, i)->w *
        jxl_channels_at_const(&image->channel, i)->h;
    total_pixels += jxl_channels_at_const(&image->channel, i)->w *
                    jxl_channels_at_const(&image->channel, i)->h;
  }
  if (jxl_array_empty(&channel_ids)) {
    jxl_array_destroy(&channel_ids);
    return;
  }
  if (!jxl_enc_status_ok(jxl_array_reserve(pixel_samples, jxl_array_len(pixel_samples) + fraction * total_pixels))) {
    JXL_CRASH();
  }
  if (!jxl_enc_status_ok(jxl_array_reserve(diff_samples, jxl_array_len(diff_samples) + fraction * total_pixels))) {
    JXL_CRASH();
  }
  size_t i = 0;
  size_t y = 0;
  size_t x = 0;
  jxl_advance_sample_pos(image, &channel_ids, &i, &y, &x, jxl_rng_geometric(&rng, dist));
  for (; i < jxl_array_len(&channel_ids);
       jxl_advance_sample_pos(image, &channel_ids, &i, &y, &x,
                        jxl_rng_geometric(&rng, dist) + 1)) {
    const pixel_type *row = jxl_channel_row_const(jxl_channels_at_const(&image->channel, *jxl_array_at(&channel_ids, i)), y);
if (!jxl_enc_status_ok(jxl_array_i32_push_back(pixel_samples, row[x]))) JXL_CRASH();
size_t xp = x == 0 ? 1 : x - 1;
if (!jxl_enc_status_ok(jxl_array_i32_push_back(diff_samples, (int64_t)(row[x]) - row[xp]))) JXL_CRASH();
}
  jxl_array_destroy(&channel_ids);
}

// TODO(veluca): very simple encoding scheme. This should be improved.
jxl_enc_status jxl_tokenize_tree(const jxl_tree *tree, jxl_token_stream *tokens,
                    jxl_tree *decoder_tree){
  JXL_ENSURE(jxl_array_len(tree) <= kMaxTreeSize);
  jxl_array_int q;
  jxl_array_construct_empty(&q, tree->ctx);
  size_t q_head = 0;
if (!jxl_enc_status_ok(jxl_array_int_push_back(&q, 0))) JXL_CRASH();
size_t leaf_id = 0;
jxl_array_clear(decoder_tree);
  while (q_head < jxl_array_len(&q)) {
    int cur = *jxl_array_at(&q, q_head++);
    JXL_ENSURE(jxl_array_at_const(tree, cur)->property >= -1);
    if (!jxl_enc_status_ok(jxl_array_token_push_back(tokens, jxl_token_make(kPropertyContext, jxl_array_at_const(tree, cur)->property + 1)))) {
      JXL_CRASH();
    }
    if (jxl_array_at_const(tree, cur)->property == -1) {
      if (!jxl_enc_status_ok(jxl_array_token_push_back(tokens,
                         jxl_token_make(kPredictorContext, (int)(jxl_array_at_const(tree, cur)->predictor))))) {
        JXL_CRASH();
      }
      if (!jxl_enc_status_ok(jxl_array_token_push_back(
              tokens, jxl_token_make(kOffsetContext,
                            jxl_pack_signed(jxl_array_at_const(tree, cur)->predictor_offset))))) {
        JXL_CRASH();
      }
      uint32_t mul_log = Num0BitsBelowLS1Bit_Nonzero32(jxl_array_at_const(tree, cur)->multiplier);
      uint32_t mul_bits = (jxl_array_at_const(tree, cur)->multiplier >> mul_log) - 1;
if (!jxl_enc_status_ok(jxl_array_token_push_back(tokens, jxl_token_make(kMultiplierLogContext, mul_log)))) JXL_CRASH();
if (!jxl_enc_status_ok(jxl_array_token_push_back(tokens, jxl_token_make(kMultiplierBitsContext, mul_bits)))) JXL_CRASH();
JXL_ENSURE(jxl_array_at_const(tree, cur)->predictor < kPredictorBest);
      {
        jxl_property_decision_node leaf = jxl_property_decision_node_leaf(
            jxl_array_at_const(tree, cur)->predictor, jxl_array_at_const(tree, cur)->predictor_offset,
            jxl_array_at_const(tree, cur)->multiplier);
        leaf.lchild = (uint32_t)(leaf_id);
if (!jxl_enc_status_ok(jxl_array_property_decision_node_push_back(decoder_tree, leaf))) JXL_CRASH();
}
      leaf_id++;
      continue;
    }
    if (!jxl_enc_status_ok(jxl_array_property_decision_node_push_back(decoder_tree,
                       jxl_property_decision_node_split(
                           jxl_array_at_const(tree, cur)->property, jxl_array_at_const(tree, cur)->splitval,
                           (int)(jxl_array_len(decoder_tree) + (jxl_array_len(&q) - q_head) + 1),
                           (int)(jxl_array_len(decoder_tree) + (jxl_array_len(&q) - q_head) + 2))))) {
      JXL_CRASH();
    }
if (!jxl_enc_status_ok(jxl_array_int_push_back(&q, jxl_array_at_const(tree, cur)->lchild))) JXL_CRASH();
if (!jxl_enc_status_ok(jxl_array_int_push_back(&q, jxl_array_at_const(tree, cur)->rchild))) JXL_CRASH();
if (!jxl_enc_status_ok(jxl_array_token_push_back(tokens, jxl_token_make(kSplitValContext, jxl_pack_signed(jxl_array_at_const(tree, cur)->splitval))))) JXL_CRASH();
}
  jxl_array_destroy(&q);
  return jxl_enc_ok_status();
}

