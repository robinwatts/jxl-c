// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_MODULAR_OPTIONS_H_
#define LIB_JXL_MODULAR_OPTIONS_H_

#include <stddef.h>
#include <stdint.h>

#include "base/array.h"
#include "enc_ans_params.h"

typedef int32_t jxl_property_val;
typedef jxl_array_i32 jxl_properties;

typedef enum jxl_enc_predictor {
  kPredictorZero = 0,
  kPredictorLeft = 1,
  kPredictorTop = 2,
  kPredictorAverage0 = 3,
  kPredictorPredictSelect = 4,
  kPredictorGradient = 5,
  kPredictorWeighted = 6,
  kPredictorTopRight = 7,
  kPredictorTopLeft = 8,
  kPredictorLeftLeft = 9,
  kPredictorAverage1 = 10,
  kPredictorAverage2 = 11,
  kPredictorAverage3 = 12,
  kPredictorAverage4 = 13,
  // The following predictors are encoder-only.
  kPredictorBest = 14,  // Best of Gradient and Weighted
  kPredictorVariable =
      15,  // Find the best decision tree for predictors/predictor per row
} jxl_enc_predictor;

static JXL_INLINE void jxl_swap_predictor(jxl_enc_predictor* a, jxl_enc_predictor* b) {
  jxl_enc_predictor tmp = *a;
  *a = *b;
  *b = tmp;
}

#define kUndefinedPredictor ((jxl_enc_predictor)(~0u))


enum { kNumModularPredictors = (int)kPredictorAverage4 + 1 };
enum { kNumStaticProperties = 2 };  // channel, group_id.

typedef struct jxl_static_prop_range {
  uint32_t v[kNumStaticProperties][2];
} jxl_static_prop_range;

static inline void jxl_static_prop_range_construct_empty(jxl_static_prop_range* self) {
  for (size_t i = 0; i < (size_t)kNumStaticProperties; ++i) {
    self->v[i][0] = 0;
    self->v[i][1] = 0;
  }
}

static inline uint32_t* jxl_static_prop_range_at(jxl_static_prop_range* self, size_t i) {
  return self->v[i];
}
static inline const uint32_t* jxl_static_prop_range_row_const(const jxl_static_prop_range* self,
                                               size_t i) {
  return self->v[i];
}

typedef struct jxl_modular_multiplier_info {
  jxl_static_prop_range range;
  uint32_t multiplier;
} jxl_modular_multiplier_info;

JXL_DEFINE_POD_ARRAY(jxl_array_modular_multiplier_info, jxl_modular_multiplier_info)
JXL_DEFINE_POD_ARRAY(jxl_array_predictor, jxl_enc_predictor)

// Max properties in the default / prop-order heuristics lists.
enum { kMaxSplittingHeuristicsProperties = 16 };

// Trivially copyable modular encode options (no owning Arrays).
// Forces the encoder to produce a tree that is compatible with the WP-only
// decode path (or with the no-wp path).
typedef enum jxl_modular_tree_mode { kWPOnly, kNoWP, kDefault } jxl_modular_tree_mode;

// Kind of tree to use.
// TODO(veluca): add tree kinds for JPEG recompression with CfL enabled,
// general AC metadata, different DC qualities, and others.
typedef enum jxl_modular_tree_kind {
  kLearn,
  kJpegTranscodeACMeta,
  kWPFixedDC,
  kGradientFixedDC,
} jxl_modular_tree_kind;

typedef struct jxl_modular_options {
  /// Used in both encode and decode:

  // Stop encoding/decoding when reaching a (non-meta) channel that has a
  // dimension bigger than max_chan_size.
  size_t max_chan_size;

  /// Encode options:
  // Fraction of pixels to look at to learn a MA tree
  // Number of iterations to do to learn a MA tree
  // (if zero there is no MA context model)
  float nb_repeats;

  // Alternative heuristic tweaks.
  // jxl_properties default to channel, group, weighted, gradient residual, W-NW,
  // NW-N, N-NE, N-NN
  uint32_t splitting_heuristics_properties[kMaxSplittingHeuristicsProperties];
  size_t num_splitting_heuristics_properties;
  float splitting_heuristics_node_threshold;
  size_t max_property_values;

  // jxl_enc_predictor to use for each channel.
  jxl_enc_predictor predictor;

  int wp_mode;

  float fast_decode_multiplier;

  jxl_modular_tree_mode wp_tree_mode;

  jxl_modular_tree_kind tree_kind;

  jxl_histogram_params histogram_params;
} jxl_modular_options;

static inline void jxl_modular_options_construct_empty(jxl_modular_options* self) {
  self->max_chan_size = 0xFFFFFF;
  self->nb_repeats = 0.5f;
  self->splitting_heuristics_properties[0] = 0;
  self->splitting_heuristics_properties[1] = 1;
  self->splitting_heuristics_properties[2] = 15;
  self->splitting_heuristics_properties[3] = 9;
  self->splitting_heuristics_properties[4] = 10;
  self->splitting_heuristics_properties[5] = 11;
  self->splitting_heuristics_properties[6] = 12;
  self->splitting_heuristics_properties[7] = 13;
  for (size_t i = 8; i < kMaxSplittingHeuristicsProperties; ++i) {
    self->splitting_heuristics_properties[i] = 0;
  }
  self->num_splitting_heuristics_properties = 8;
  self->splitting_heuristics_node_threshold = 96;
  self->max_property_values = 32;
  self->predictor = kUndefinedPredictor;
  self->wp_mode = 0;
  self->fast_decode_multiplier = 1.01f;
  self->wp_tree_mode = kDefault;
  self->tree_kind = kLearn;
  jxl_histogram_params_construct_empty(&self->histogram_params);
}

JXL_DEFINE_POD_ARRAY(jxl_array_modular_options, jxl_modular_options)


#endif  // LIB_JXL_MODULAR_OPTIONS_H_
