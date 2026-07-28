// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_MODULAR_ENCODING_CONTEXT_PREDICT_H_
#define LIB_JXL_MODULAR_ENCODING_CONTEXT_PREDICT_H_

#include <stddef.h>
#include <stdint.h>

#include "lib/jxl/base/array.h"
#include "lib/jxl/base/bits.h"
#include "lib/jxl/base/compiler_specific.h"
#include "lib/jxl/base/status.h"
#include "lib/jxl/field_encodings.h"
#include "lib/jxl/fields.h"
#include "lib/jxl/modular/modular_image.h"
#include "lib/jxl/modular/options.h"
#include "lib/jxl/base/common.h"


enum {
  kWeightedNumPredictors = 4,
  kWeightedPredExtraBits = 3,
  kWeightedPredictionRound = ((1 << kWeightedPredExtraBits) >> 1) - 1,
  kWeightedNumProperties = 1
};

static inline jxl_status jxl_visit_pixel_type_bits(jxl_visitor *visitor, pixel_type default_val,
                                 pixel_type *p) {
  uint32_t up = *p;
  JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 5, default_val, &up));
  *p = up;
  return jxl_ok_status();
}

typedef struct jxl_weighted_header {
  jxl_fields fields;
  // TODO(janwas): move to cc file, avoid including fields.h.


  bool all_default;
  pixel_type p1C, p2C, p3Ca, p3Cb, p3Cc, p3Cd, p3Ce;
  uint32_t w[kWeightedNumPredictors];
} jxl_weighted_header;

static inline jxl_status jxl_weighted_header_visit_fields(jxl_weighted_header* self, jxl_visitor *JXL_RESTRICT visitor);
JXL_FIELDS_NAME(jxl_weighted_header)

static inline jxl_status jxl_weighted_header_visit_fields(jxl_weighted_header* self, jxl_visitor *JXL_RESTRICT visitor) {
    if (jxl_status_ok(jxl_visitor_all_default(visitor, &self->fields, &self->all_default))) {
      // Overwrite all serialized fields, but not any nonserialized_*.
      jxl_visitor_set_default(visitor, &self->fields);
      return jxl_ok_status();
    }
    JXL_QUIET_RETURN_IF_ERROR(jxl_visit_pixel_type_bits(visitor, 16, &self->p1C));
    JXL_QUIET_RETURN_IF_ERROR(jxl_visit_pixel_type_bits(visitor, 10, &self->p2C));
    JXL_QUIET_RETURN_IF_ERROR(jxl_visit_pixel_type_bits(visitor, 7, &self->p3Ca));
    JXL_QUIET_RETURN_IF_ERROR(jxl_visit_pixel_type_bits(visitor, 7, &self->p3Cb));
    JXL_QUIET_RETURN_IF_ERROR(jxl_visit_pixel_type_bits(visitor, 7, &self->p3Cc));
    JXL_QUIET_RETURN_IF_ERROR(jxl_visit_pixel_type_bits(visitor, 0, &self->p3Cd));
    JXL_QUIET_RETURN_IF_ERROR(jxl_visit_pixel_type_bits(visitor, 0, &self->p3Ce));
    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 4, 0xd, &self->w[0]));
    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 4, 0xc, &self->w[1]));
    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 4, 0xc, &self->w[2]));
    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 4, 0xc, &self->w[3]));
    return jxl_ok_status();
  }


static inline void jxl_weighted_header_init(jxl_weighted_header* self) {
  JXL_FIELDS_REGISTER_PTR(jxl_weighted_header, &self->fields);
  jxl_bundle_init(&self->fields);
}

typedef struct jxl_weighted_state {
  pixel_type_w prediction[kWeightedNumPredictors];
  pixel_type_w pred;  // *before* removing the added bits.
  jxl_array_u32 pred_errors[kWeightedNumPredictors];
  jxl_array_i32 error;
  const jxl_weighted_header *header;
} jxl_weighted_state;

// Allows to approximate division by a number from 1 to 64.
//  for (int i = 0; i < 64; i++) divlookup[i] = (1 << 24) / (i + 1);

static const uint32_t kWeightedDivLookup[64] = {
      16777216, 8388608, 5592405, 4194304, 3355443, 2796202, 2396745, 2097152,
      1864135,  1677721, 1525201, 1398101, 1290555, 1198372, 1118481, 1048576,
      986895,   932067,  883011,  838860,  798915,  762600,  729444,  699050,
      671088,   645277,  621378,  599186,  578524,  559240,  541200,  524288,
      508400,   493447,  479349,  466033,  453438,  441505,  430185,  419430,
      409200,   399457,  390167,  381300,  372827,  364722,  356962,  349525,
      342392,   335544,  328965,  322638,  316551,  310689,  305040,  299593,
      294337,   289262,  284359,  279620,  275036,  270600,  266305,  262144};


static inline void jxl_weighted_state_init(jxl_weighted_state* self,
                              const jxl_weighted_header* header, size_t xsize,
                              size_t ysize, jxl_context* mm) {
  self->header = header;
  self->pred = 0;
  for (size_t i = 0; i < kWeightedNumPredictors; ++i) {
    self->prediction[i] = 0;
    jxl_array_construct_empty(&self->pred_errors[i], mm);
  }
  jxl_array_construct_empty(&self->error, mm);
  // Extra margin to avoid out-of-bounds writes.
  // All have space for two rows of data.
  for (size_t pred_error_i = 0; pred_error_i < 4; ++pred_error_i) {
    jxl_array_u32* pred_error = &self->pred_errors[pred_error_i];
    if (!jxl_status_ok(jxl_array_resize_zero(pred_error, (xsize + 2) * 2))) JXL_CRASH();
  }
  if (!jxl_status_ok(jxl_array_resize_zero(&self->error, (xsize + 2) * 2))) JXL_CRASH();
}

static inline void jxl_weighted_state_destroy(jxl_weighted_state* self) {
  if (self == NULL) return;
  for (size_t i = 0; i < kWeightedNumPredictors; ++i) {
    jxl_array_destroy(&self->pred_errors[i]);
  }
  jxl_array_destroy(&self->error);
  self->header = NULL;
  self->pred = 0;
}

static JXL_INLINE pixel_type_w jxl_weighted_state_add_bits(pixel_type_w x) {
  return (uint64_t)(x) << kWeightedPredExtraBits;
}

// Approximates 4+(maxweight<<24)/(x+1), avoiding division
static JXL_INLINE uint32_t jxl_weighted_state_error_weight(const jxl_weighted_state* self, uint64_t x,
                                             uint32_t maxweight) {
  int shift = (int)(jxl_floor_log2_nonzero32(x + 1)) - 5;
  if (shift < 0) shift = 0;
  return 4 + ((maxweight * kWeightedDivLookup[x >> shift]) >> shift);
}

// Approximates the weighted average of the input values with the given
// weights, avoiding division. Weights must sum to at least 16.
static JXL_INLINE pixel_type_w jxl_weighted_state_weighted_average(
    const jxl_weighted_state* self, const pixel_type_w* JXL_RESTRICT p,
    uint32_t w[kWeightedNumPredictors]) {
  uint32_t weight_sum = 0;
  for (size_t i = 0; i < kWeightedNumPredictors; i++) {
    weight_sum += w[i];
  }
  JXL_DASSERT(weight_sum > 15);
  uint32_t log_weight = jxl_floor_log2_nonzero32(weight_sum);  // at least 4.
  weight_sum = 0;
  for (size_t i = 0; i < kWeightedNumPredictors; i++) {
    w[i] >>= log_weight - 4;
    weight_sum += w[i];
  }
  // for rounding.
  pixel_type_w sum = (weight_sum >> 1) - 1;
  for (size_t i = 0; i < kWeightedNumPredictors; i++) {
    sum += p[i] * w[i];
  }
  return (sum * kWeightedDivLookup[weight_sum - 1]) >> 24;
}

static JXL_INLINE pixel_type_w jxl_weighted_state_predict(
    jxl_weighted_state* self, size_t x, size_t y, size_t xsize, pixel_type_w N,
    pixel_type_w W, pixel_type_w NE, pixel_type_w NW, pixel_type_w NN,
    jxl_properties* properties, size_t offset, bool compute_properties) {
  size_t cur_row = y & 1 ? 0 : (xsize + 2);
  size_t prev_row = y & 1 ? (xsize + 2) : 0;
  size_t pos_N = prev_row + x;
  size_t pos_NE = x < xsize - 1 ? pos_N + 1 : pos_N;
  size_t pos_NW = x > 0 ? pos_N - 1 : pos_N;
  uint32_t weights[kWeightedNumPredictors];
  for (size_t i = 0; i < kWeightedNumPredictors; i++) {
    // pred_errors[pos_N] also contains the error of pixel W.
    // pred_errors[pos_NW] also contains the error of pixel WW.
    weights[i] = *jxl_array_at(&self->pred_errors[i], pos_N) +
                 *jxl_array_at(&self->pred_errors[i], pos_NE) +
                 *jxl_array_at(&self->pred_errors[i], pos_NW);
    weights[i] =
        jxl_weighted_state_error_weight(self, weights[i], self->header->w[i]);
  }

  N = jxl_weighted_state_add_bits(N);
  W = jxl_weighted_state_add_bits(W);
  NE = jxl_weighted_state_add_bits(NE);
  NW = jxl_weighted_state_add_bits(NW);
  NN = jxl_weighted_state_add_bits(NN);

  pixel_type_w teW = x == 0 ? 0 : *jxl_array_at(&self->error, cur_row + x - 1);
  pixel_type_w teN = *jxl_array_at(&self->error, pos_N);
  pixel_type_w teNW = *jxl_array_at(&self->error, pos_NW);
  pixel_type_w sumWN = teN + teW;
  pixel_type_w teNE = *jxl_array_at(&self->error, pos_NE);

  if (compute_properties) {
    pixel_type_w p = teW;
    if (JXL_ABS(teN) > JXL_ABS(p)) p = teN;
    if (JXL_ABS(teNW) > JXL_ABS(p)) p = teNW;
    if (JXL_ABS(teNE) > JXL_ABS(p)) p = teNE;
    *jxl_array_at(properties, offset++) = p;
  }

  self->prediction[0] = W + NE - N;
  self->prediction[1] =
      N - (((sumWN + teNE) * self->header->p1C) >> 5);
  self->prediction[2] =
      W - (((sumWN + teNW) * self->header->p2C) >> 5);
  self->prediction[3] =
      N - ((teNW * self->header->p3Ca + teN * self->header->p3Cb +
            teNE * self->header->p3Cc + (NN - N) * self->header->p3Cd +
            (NW - W) * self->header->p3Ce) >>
           5);

  self->pred = jxl_weighted_state_weighted_average(self, self->prediction, weights);

  // If all three have the same sign, skip clamping.
  if (((teN ^ teW) | (teN ^ teNW)) > 0) {
    return (self->pred + kWeightedPredictionRound) >> kWeightedPredExtraBits;
  }

  // Otherwise, clamp to min/max of neighbouring pixels (just W, NE, N).
  pixel_type_w mx = JXL_MAX(W, JXL_MAX(NE, N));
  pixel_type_w mn = JXL_MIN(W, JXL_MIN(NE, N));
  self->pred = JXL_MAX(mn, JXL_MIN(mx, self->pred));
  return (self->pred + kWeightedPredictionRound) >> kWeightedPredExtraBits;
}

static JXL_INLINE void jxl_weighted_state_update_errors(jxl_weighted_state* self, pixel_type_w val,
                                          size_t x, size_t y, size_t xsize) {
  size_t cur_row = y & 1 ? 0 : (xsize + 2);
  size_t prev_row = y & 1 ? (xsize + 2) : 0;
  val = jxl_weighted_state_add_bits(val);
  *jxl_array_at(&self->error, cur_row + x) = self->pred - val;
  for (size_t i = 0; i < kWeightedNumPredictors; i++) {
    pixel_type_w err =
        (JXL_ABS(self->prediction[i] - val) + kWeightedPredictionRound) >>
        kWeightedPredExtraBits;
    // For predicting in the next row.
    *jxl_array_at(&self->pred_errors[i], cur_row + x) = err;
    // Add the error on this pixel to the error on the NE pixel. This has the
    // effect of adding the error on this pixel to the E and EE pixels.
    *jxl_array_at(&self->pred_errors[i], prev_row + x + 1) += err;
  }
}



// Encoder helper function to set the parameters to some presets.
static inline void jxl_weighted_predictor_mode(int i, jxl_weighted_header *header) {
  switch (i) {
    case 0:
      // ~ lossless16 predictor
      header->w[0] = 0xd;
      header->w[1] = 0xc;
      header->w[2] = 0xc;
      header->w[3] = 0xc;
      header->p1C = 16;
      header->p2C = 10;
      header->p3Ca = 7;
      header->p3Cb = 7;
      header->p3Cc = 7;
      header->p3Cd = 0;
      header->p3Ce = 0;
      break;
    case 1:
      // ~ default lossless8 predictor
      header->w[0] = 0xd;
      header->w[1] = 0xc;
      header->w[2] = 0xc;
      header->w[3] = 0xb;
      header->p1C = 8;
      header->p2C = 8;
      header->p3Ca = 4;
      header->p3Cb = 0;
      header->p3Cc = 3;
      header->p3Cd = 23;
      header->p3Ce = 2;
      break;
    case 2:
      // ~ west lossless8 predictor
      header->w[0] = 0xd;
      header->w[1] = 0xc;
      header->w[2] = 0xd;
      header->w[3] = 0xc;
      header->p1C = 10;
      header->p2C = 9;
      header->p3Ca = 7;
      header->p3Cb = 0;
      header->p3Cc = 0;
      header->p3Cd = 16;
      header->p3Ce = 9;
      break;
    case 3:
      // ~ north lossless8 predictor
      header->w[0] = 0xd;
      header->w[1] = 0xd;
      header->w[2] = 0xc;
      header->w[3] = 0xc;
      header->p1C = 16;
      header->p2C = 8;
      header->p3Ca = 0;
      header->p3Cb = 16;
      header->p3Cc = 0;
      header->p3Cd = 23;
      header->p3Ce = 0;
      break;
    case 4:
    default:
      // something else, because why not
      header->w[0] = 0xd;
      header->w[1] = 0xc;
      header->w[2] = 0xc;
      header->w[3] = 0xc;
      header->p1C = 10;
      header->p2C = 10;
      header->p3Ca = 5;
      header->p3Cb = 5;
      header->p3Cc = 5;
      header->p3Cd = 12;
      header->p3Ce = 4;
      break;
  }
}

// Returns true if the (meta)predictor makes use of the weighted predictor.
static inline bool jxl_predictor_has_weighted(jxl_predictor predictor) {
  // Use a non-defaulted switch to generate a warning if a case is missing.
  switch (predictor) {
    case kPredictorZero:
    case kPredictorLeft:
    case kPredictorTop:
    case kPredictorAverage0:
    case kPredictorPredictSelect:
    case kPredictorGradient:
      return false;
    case kPredictorWeighted:
      return true;
    case kPredictorTopRight:
    case kPredictorTopLeft:
    case kPredictorLeftLeft:
    case kPredictorAverage1:
    case kPredictorAverage2:
    case kPredictorAverage3:
    case kPredictorAverage4:
      return false;
    case kPredictorBest:
    case kPredictorVariable:
      return true;
  }

  return false;
}

// Stores a node and its two children at the same time. This significantly
// reduces the number of branches needed during decoding.
typedef struct jxl_flat_decision_node {
  // Property + splitval of the top node->
  int32_t property0;  // -1 if leaf.
  union {
    jxl_property_val splitval0;
    jxl_predictor predictor;
  } top;
  // Property+splitval of the two child nodes.
  union {
    jxl_property_val splitvals[2];
    int32_t multiplier;
  } children;
  uint32_t childID;  // childID is ctx id if leaf.
  union {
    int16_t properties[2];
    int32_t predictor_offset;
  } meta;
} jxl_flat_decision_node;
JXL_DEFINE_POD_ARRAY(jxl_array_flat_decision_node, jxl_flat_decision_node)
typedef jxl_array_flat_decision_node jxl_flat_tree;

typedef struct jxl_ma_tree_lookup_result {
  uint32_t context;
  jxl_predictor predictor;
  int32_t offset;
  int32_t multiplier;
} jxl_ma_tree_lookup_result;

static inline jxl_ma_tree_lookup_result jxl_ma_tree_lookup_result_make(uint32_t context,
                                                 jxl_predictor predictor,
                                                 int32_t offset,
                                                 int32_t multiplier) {
  jxl_ma_tree_lookup_result result;
  result.context = context;
  result.predictor = predictor;
  result.offset = offset;
  result.multiplier = multiplier;
  return result;
}

typedef struct jxl_ma_tree_lookup {
  const jxl_flat_tree *nodes_;
} jxl_ma_tree_lookup;

static inline jxl_ma_tree_lookup jxl_ma_tree_lookup_make(const jxl_flat_tree* tree) {
  jxl_ma_tree_lookup lookup;
  lookup.nodes_ = tree;
  return lookup;
}

static JXL_INLINE jxl_ma_tree_lookup_result jxl_ma_tree_lookup_lookup(
    const jxl_ma_tree_lookup *self, const jxl_properties *properties) {
    uint32_t pos = 0;
    while (true) {
#define TRAVERSE_THE_TREE                                                \
  {                                                                      \
    const jxl_flat_decision_node *node = jxl_array_at_const(self->nodes_, pos);                        \
    if (node->property0 < 0) {                                            \
      return jxl_ma_tree_lookup_result_make(node->childID, node->top.predictor,   \
                                    node->meta.predictor_offset,          \
                                    node->children.multiplier);           \
    }                                                                    \
    bool p0 = *jxl_array_at_const(properties, node->property0) <= node->top.splitval0; \
    uint32_t off0 = *jxl_array_at_const(properties, node->meta.properties[0]) <= node->children.splitvals[0]; \
    uint32_t off1 =                                                      \
        2 | (int)(*jxl_array_at_const(properties, node->meta.properties[1]) <= node->children.splitvals[1]);  \
    pos = node->childID + (p0 ? off1 : off0);                             \
  }

      TRAVERSE_THE_TREE;
      TRAVERSE_THE_TREE;
    }
  }


enum {
  kExtraPropsPerChannel = 4,
  kNumNonrefProperties = kNumStaticProperties + 13 + kWeightedNumProperties,
  kWPProp = kNumNonrefProperties - kWeightedNumProperties,
  kGradientProp = 9
};

// Clamps gradient to the min/max of n, w (and l, implicitly).
static JXL_INLINE int32_t jxl_clamped_gradient(const int32_t n, const int32_t w,
                                          const int32_t l) {
  const int32_t m = JXL_MIN(n, w);
  const int32_t M = JXL_MAX(n, w);
  // The end result of this operation doesn't overflow or underflow if the
  // result is between m and M, but the intermediate value may overflow, so we
  // do the intermediate operations in uint32_t and check later if we had an
  // overflow or underflow condition comparing m, M and l directly.
  // grad = M + m - l = n + w - l
  const int32_t grad =
      (int32_t)((uint32_t)(n) + (uint32_t)(w) -
                           (uint32_t)(l));
  // We use two sets of ternary operators to force the evaluation of them in
  // any case, allowing the compiler to avoid branches and use cmovl/cmovg in
  // x86.
  const int32_t grad_clamp_M = (l < m) ? M : grad;
  return (l > M) ? m : grad_clamp_M;
}

static inline pixel_type_w jxl_select(pixel_type_w a, pixel_type_w b, pixel_type_w c) {
  pixel_type_w p = a + b - c;
  pixel_type_w pa = JXL_ABS(p - a);
  pixel_type_w pb = JXL_ABS(p - b);
  return pa < pb ? a : b;
}

typedef struct jxl_prediction_result {
  int context;
  pixel_type_w guess;
  jxl_predictor predictor;
  int32_t multiplier;
} jxl_prediction_result;
static inline void jxl_prediction_result_construct_empty(jxl_prediction_result* self) {
  self->context = 0;
  self->guess = 0;
  self->predictor = kPredictorZero;
  self->multiplier = 1;
}

static inline void jxl_init_props_row(
    jxl_properties *p,
    const pixel_type static_props[kNumStaticProperties],
    const int y) {
  for (size_t i = 0; i < kNumStaticProperties; i++) {
    *jxl_array_at(p, i) = static_props[i];
  }
  *jxl_array_at(p, 2) = y;
  *jxl_array_at(p, 9) = 0;  // local gradient.
}

typedef enum jxl_predict_mode_flags {
  kUseTree = 1,
  kUseWP = 2,
  kForceComputeProperties = 4,
  kAllPredictions = 8,
  kNoEdgeCases = 16
} jxl_predict_mode_flags;

static JXL_INLINE pixel_type_w jxl_predict_one(jxl_predictor p, pixel_type_w left,
                                   pixel_type_w top, pixel_type_w toptop,
                                   pixel_type_w topleft, pixel_type_w topright,
                                   pixel_type_w leftleft,
                                   pixel_type_w toprightright,
                                   pixel_type_w wp_pred) {
  switch (p) {
    case kPredictorZero:
      return (pixel_type_w)0;
    case kPredictorLeft:
      return left;
    case kPredictorTop:
      return top;
    case kPredictorPredictSelect:
      return jxl_select(left, top, topleft);
    case kPredictorWeighted:
      return wp_pred;
    case kPredictorGradient:
      return (pixel_type_w)jxl_clamped_gradient(left, top, topleft);
    case kPredictorTopLeft:
      return topleft;
    case kPredictorTopRight:
      return topright;
    case kPredictorLeftLeft:
      return leftleft;
    case kPredictorAverage0:
      return (left + top) / 2;
    case kPredictorAverage1:
      return (left + topleft) / 2;
    case kPredictorAverage2:
      return (topleft + top) / 2;
    case kPredictorAverage3:
      return (top + topright) / 2;
    case kPredictorAverage4:
      return (6 * top - 2 * toptop + 7 * left + 1 * leftleft +
              1 * toprightright + 3 * topright + 8) /
             16;
    default:
      return (pixel_type_w)0;
  }
}

static JXL_INLINE jxl_prediction_result jxl_predict_with_mode(
    int mode, jxl_properties *p, size_t w, const pixel_type *JXL_RESTRICT pp,
    const ptrdiff_t onerow, const size_t x, const size_t y, jxl_predictor predictor,
    const jxl_ma_tree_lookup *lookup, const jxl_channel *references,
    jxl_weighted_state *wp_state, pixel_type_w *predictions) {
  // We start in position 3 because of 2 static properties + y.
  size_t offset = 3;
  const bool compute_properties =
      !!(mode & kUseTree) || !!(mode & kForceComputeProperties);
  const bool nec = !!(mode & kNoEdgeCases);
  pixel_type_w left = (nec || x ? pp[-1] : (y ? pp[-onerow] : 0));
  pixel_type_w top = (nec || y ? pp[-onerow] : left);
  pixel_type_w topleft = (nec || (x && y) ? pp[-1 - onerow] : left);
  pixel_type_w topright = (nec || (x + 1 < w && y) ? pp[1 - onerow] : top);
  pixel_type_w leftleft = (nec || x > 1 ? pp[-2] : left);
  pixel_type_w toptop = (nec || y > 1 ? pp[-onerow - onerow] : top);
  pixel_type_w toprightright =
      (nec || (x + 2 < w && y) ? pp[2 - onerow] : topright);

  if (compute_properties) {
    // location
    *jxl_array_at(p, offset++) = x;
    // neighbors
    *jxl_array_at(p, offset++) = top > 0 ? top : -top;
    *jxl_array_at(p, offset++) = left > 0 ? left : -left;
    *jxl_array_at(p, offset++) = top;
    *jxl_array_at(p, offset++) = left;

    // local gradient
    *jxl_array_at(p, offset) = left - *jxl_array_at(p, offset + 1);
    offset++;
    // local gradient
    *jxl_array_at(p, offset++) = left + top - topleft;

    // FFV1 context properties
    *jxl_array_at(p, offset++) = left - topleft;
    *jxl_array_at(p, offset++) = topleft - top;
    *jxl_array_at(p, offset++) = top - topright;
    *jxl_array_at(p, offset++) = top - toptop;
    *jxl_array_at(p, offset++) = left - leftleft;
  }

  pixel_type_w wp_pred = 0;
  if (mode & kUseWP) {
    wp_pred = jxl_weighted_state_predict(wp_state, 
        x, y, w, top, left, topright, topleft, toptop, p, offset,
        compute_properties);
  }
  if (!nec && compute_properties) {
    offset += kWeightedNumProperties;
    // Extra properties. references stores one row per x; skip when empty
    // (JPEG trees never use reference-channel properties: iw=0, ih=1).
    if (references->w != 0) {
      const pixel_type *JXL_RESTRICT rp = jxl_channel_row_const(references, x);
      for (size_t i = 0; i < references->w; i++) {
        *jxl_array_at(p, offset++) = rp[i];
      }
    }
  }
  jxl_prediction_result result;
  jxl_prediction_result_construct_empty(&result);
  if (mode & kUseTree) {
    jxl_ma_tree_lookup_result lr = jxl_ma_tree_lookup_lookup(lookup, p);
    result.context = lr.context;
    result.guess = lr.offset;
    result.multiplier = lr.multiplier;
    predictor = lr.predictor;
  }
  if (mode & kAllPredictions) {
    for (size_t i = 0; i < kNumModularPredictors; i++) {
      predictions[i] =
          jxl_predict_one((jxl_predictor)(i), left, top, toptop, topleft,
                     topright, leftleft, toprightright, wp_pred);
    }
  }
  result.guess += jxl_predict_one(predictor, left, top, toptop, topleft, topright,
                             leftleft, toprightright, wp_pred);
  result.predictor = predictor;

  return result;
}
static inline jxl_prediction_result jxl_predict_no_tree_no_wp(size_t w,
                                          const pixel_type *JXL_RESTRICT pp,
                                          const ptrdiff_t onerow, const int x,
                                          const int y, jxl_predictor predictor) {
  return jxl_predict_with_mode(0,
      /*p=*/NULL, w, pp, onerow, x, y, predictor, /*lookup=*/NULL,
      /*references=*/NULL, /*wp_state=*/NULL, /*predictions=*/NULL);
}

static inline jxl_prediction_result jxl_predict_tree_no_wp(jxl_properties *p, size_t w,
                                        const pixel_type *JXL_RESTRICT pp,
                                        const ptrdiff_t onerow, const int x,
                                        const int y,
                                        const jxl_ma_tree_lookup *tree_lookup,
                                        const jxl_channel *references) {
  return jxl_predict_with_mode(kUseTree,
      p, w, pp, onerow, x, y, kPredictorZero, tree_lookup, references,
      /*wp_state=*/NULL, /*predictions=*/NULL);
}
// Only use for y > 1, x > 1, x < w-2, and empty references
static JXL_INLINE jxl_prediction_result
jxl_predict_tree_no_wpnec(jxl_properties *p, size_t w, const pixel_type *JXL_RESTRICT pp,
                   const ptrdiff_t onerow, const int x, const int y,
                   const jxl_ma_tree_lookup *tree_lookup, const jxl_channel *references) {
  return jxl_predict_with_mode(kUseTree | kNoEdgeCases,
      p, w, pp, onerow, x, y, kPredictorZero, tree_lookup, references,
      /*wp_state=*/NULL, /*predictions=*/NULL);
}

static inline jxl_prediction_result jxl_predict_tree_wp(jxl_properties *p, size_t w,
                                      const pixel_type *JXL_RESTRICT pp,
                                      const ptrdiff_t onerow, const int x,
                                      const int y,
                                      const jxl_ma_tree_lookup *tree_lookup,
                                      const jxl_channel *references,
                                      jxl_weighted_state *wp_state) {
  return jxl_predict_with_mode(kUseTree | kUseWP,
      p, w, pp, onerow, x, y, kPredictorZero, tree_lookup, references,
      wp_state, /*predictions=*/NULL);
}
static JXL_INLINE jxl_prediction_result jxl_predict_tree_wpnec(jxl_properties *p, size_t w,
                                             const pixel_type *JXL_RESTRICT pp,
                                             const ptrdiff_t onerow, const int x,
                                             const int y,
                                             const jxl_ma_tree_lookup *tree_lookup,
                                             const jxl_channel *references,
                                             jxl_weighted_state *wp_state) {
  return jxl_predict_with_mode(kUseTree | kUseWP |
                         kNoEdgeCases,
      p, w, pp, onerow, x, y, kPredictorZero, tree_lookup, references,
      wp_state, /*predictions=*/NULL);
}

static inline jxl_prediction_result jxl_predict_learn(jxl_properties *p, size_t w,
                                     const pixel_type *JXL_RESTRICT pp,
                                     const ptrdiff_t onerow, const int x,
                                     const int y, jxl_predictor predictor,
                                     const jxl_channel *references,
                                     jxl_weighted_state *wp_state) {
  return jxl_predict_with_mode(kForceComputeProperties | kUseWP,
      p, w, pp, onerow, x, y, predictor, /*lookup=*/NULL, references,
      wp_state, /*predictions=*/NULL);
}

static inline void jxl_predict_learn_all(jxl_properties *p, size_t w,
                            const pixel_type *JXL_RESTRICT pp,
                            const ptrdiff_t onerow, const int x, const int y,
                            const jxl_channel *references,
                            jxl_weighted_state *wp_state,
                            pixel_type_w *predictions) {
  jxl_predict_with_mode(kForceComputeProperties | kUseWP |
                  kAllPredictions,
      p, w, pp, onerow, x, y, kPredictorZero,
      /*lookup=*/NULL, references, wp_state, predictions);
}
static inline jxl_prediction_result jxl_predict_learn_nec(jxl_properties *p, size_t w,
                                        const pixel_type *JXL_RESTRICT pp,
                                        const ptrdiff_t onerow, const int x,
                                        const int y, jxl_predictor predictor,
                                        const jxl_channel *references,
                                        jxl_weighted_state *wp_state) {
  return jxl_predict_with_mode(kForceComputeProperties | kUseWP |
                         kNoEdgeCases,
      p, w, pp, onerow, x, y, predictor, /*lookup=*/NULL, references,
      wp_state, /*predictions=*/NULL);
}

static inline void jxl_predict_learn_all_nec(jxl_properties *p, size_t w,
                               const pixel_type *JXL_RESTRICT pp,
                               const ptrdiff_t onerow, const int x, const int y,
                               const jxl_channel *references,
                               jxl_weighted_state *wp_state,
                               pixel_type_w *predictions) {
  jxl_predict_with_mode(kForceComputeProperties | kUseWP |
                  kAllPredictions | kNoEdgeCases,
      p, w, pp, onerow, x, y, kPredictorZero,
      /*lookup=*/NULL, references, wp_state, predictions);
}


#endif  // LIB_JXL_MODULAR_ENCODING_CONTEXT_PREDICT_H_
