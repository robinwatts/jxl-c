// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#ifndef JXL_ENC_MODULAR_ENCODING_ENC_MA_H_
#define JXL_ENC_MODULAR_ENCODING_ENC_MA_H_

#include <stddef.h>
#include <stdint.h>

#include "base/array.h"
#include "base/common.h"
#include "base/enc_status.h"
#include "enc_ans.h"
#include "modular/encoding/dec_ma.h"
#include "modular/modular_image.h"
#include "modular/options.h"


typedef struct jxl_residual_token {
  uint8_t tok;
  uint8_t nbits;
} jxl_residual_token;

static JXL_INLINE void jxl_swap_residual_token(jxl_residual_token* a, jxl_residual_token* b) {
  jxl_residual_token tmp = *a;
  *a = *b;
  *b = tmp;
}

JXL_DEFINE_POD_ARRAY(jxl_array_residual_token, jxl_residual_token)

// Struct to collect all the data needed to build a tree.
enum {
  kTreeSamplesPropertyRange = 511,
  kTreeSamplesDedupEntryUnused = (int)(-1)
};

typedef struct jxl_tree_samples {
  // Residual information: token and number of extra bits, per predictor.
  // Only the first predictors.size() entries are used.
  jxl_array_residual_token residuals[kNumModularPredictors];
  // Number of occurrences of each sample.
  jxl_array_u16 sample_counts;
  // Quantized static property values
  size_t num_static_props;
  jxl_array_u32 static_props[kNumStaticProperties];
  // Property values, quantized to at most 256 distinct values.
  // Only the first jxl_tree_samples_num_sample_props() entries are used.
  jxl_array_u8 props[kMaxSplittingHeuristicsProperties];
  // Decompactification info for `props` / static props.
  jxl_array_i32 compact_properties[kMaxSplittingHeuristicsProperties];
  // List of properties to use.
  jxl_array_u32 props_to_use;
  // List of predictors to use.
  jxl_array_predictor predictors;
  // Mapping property value -> quantized property value.
  jxl_array_u16 static_property_mapping[kNumStaticProperties];
  jxl_array_u8 property_mapping[kMaxSplittingHeuristicsProperties];
  // Number of samples seen.
  size_t num_samples;
  // Table for deduplication.
  jxl_array_u32 dedup_table_;

} jxl_tree_samples;

// Defined in enc_ma.cc
jxl_enc_status jxl_tree_samples_set_predictor(jxl_tree_samples* self, jxl_enc_predictor predictor,
                               jxl_modular_tree_mode wp_tree_mode);
jxl_enc_status jxl_tree_samples_set_properties(jxl_tree_samples* self, const uint32_t* properties,
                                size_t num_properties,
                                jxl_modular_tree_mode wp_tree_mode);
void jxl_tree_samples_prepare_for_samples(jxl_tree_samples* self, size_t extra_num_samples);
void jxl_tree_samples_add_sample(jxl_tree_samples* self, pixel_type_w pixel,
                          const jxl_properties* properties,
                          const pixel_type_w* predictions);
void jxl_tree_samples_pre_quantize_properties(
    jxl_tree_samples* self, const jxl_static_prop_range* range,
    const jxl_array_modular_multiplier_info* multiplier_info,
    const jxl_array_u32* group_pixel_count, const jxl_array_u32* channel_pixel_count,
    jxl_array_i32* pixel_samples, jxl_array_i32* diff_samples, size_t max_property_values);
void jxl_tree_samples_swap(jxl_tree_samples* self, size_t a, size_t b);
bool jxl_tree_samples_is_same_sample(const jxl_tree_samples* self, size_t a, size_t b);
size_t jxl_tree_samples_hash1(const jxl_tree_samples* self, size_t a);
size_t jxl_tree_samples_hash2(const jxl_tree_samples* self, size_t a);
void jxl_tree_samples_init_table(jxl_tree_samples* self, size_t log_size);
bool jxl_tree_samples_add_to_table_and_merge(jxl_tree_samples* self, size_t a);
void jxl_tree_samples_add_to_table(jxl_tree_samples* self, size_t a);

static inline void jxl_tree_samples_construct_empty(jxl_tree_samples* self,
                                                     jxl_context* mm) {
  for (size_t i = 0; i < kNumModularPredictors; ++i) {
    jxl_array_construct_empty(&self->residuals[i], mm);
  }
  jxl_array_construct_empty(&self->sample_counts, mm);
  self->num_static_props = 0;
  for (size_t i = 0; i < kNumStaticProperties; ++i) {
    jxl_array_construct_empty(&self->static_props[i], mm);
    jxl_array_construct_empty(&self->static_property_mapping[i], mm);
  }
  for (size_t i = 0; i < kMaxSplittingHeuristicsProperties; ++i) {
    jxl_array_construct_empty(&self->props[i], mm);
    jxl_array_construct_empty(&self->compact_properties[i], mm);
    jxl_array_construct_empty(&self->property_mapping[i], mm);
  }
  jxl_array_construct_empty(&self->props_to_use, mm);
  jxl_array_construct_empty(&self->predictors, mm);
  self->num_samples = 0;
  jxl_array_construct_empty(&self->dedup_table_, mm);
}

static inline void jxl_tree_samples_destroy(jxl_tree_samples* self) {
  if (self == NULL) return;
  for (size_t i = 0; i < kNumModularPredictors; ++i) {
    jxl_array_destroy(&self->residuals[i]);
  }
  jxl_array_destroy(&self->sample_counts);
  for (size_t i = 0; i < kNumStaticProperties; ++i) {
    jxl_array_destroy(&self->static_props[i]);
    jxl_array_destroy(&self->static_property_mapping[i]);
  }
  for (size_t i = 0; i < kMaxSplittingHeuristicsProperties; ++i) {
    jxl_array_destroy(&self->props[i]);
    jxl_array_destroy(&self->compact_properties[i]);
    jxl_array_destroy(&self->property_mapping[i]);
  }
  jxl_array_destroy(&self->props_to_use);
  jxl_array_destroy(&self->predictors);
  jxl_array_destroy(&self->dedup_table_);
  self->num_static_props = 0;
  self->num_samples = 0;
}

static inline bool jxl_tree_samples_has_samples(const jxl_tree_samples* self) {
  return !jxl_array_empty(&self->predictors) && !jxl_array_empty(&self->residuals[0]);
}
static inline size_t jxl_tree_samples_num_distinct_samples(const jxl_tree_samples* self) {
  return jxl_array_len(&self->sample_counts);
}
static inline size_t jxl_tree_samples_num_samples(const jxl_tree_samples* self) {
  return self->num_samples;
}
static inline const jxl_array_residual_token* jxl_tree_samples_r_tokens(const jxl_tree_samples* self,
                                                    size_t pred) {
  return &self->residuals[pred];
}
static inline jxl_residual_token jxl_tree_samples_r_token(const jxl_tree_samples* self, size_t pred,
                                       size_t i) {
  return *jxl_array_at_const(&self->residuals[pred], i);
}
static inline size_t jxl_tree_samples_token(const jxl_tree_samples* self, size_t pred, size_t i) {
  return jxl_array_at_const(&self->residuals[pred], i)->tok;
}
static inline size_t jxl_tree_samples_count(const jxl_tree_samples* self, size_t i) {
  return *jxl_array_at_const(&self->sample_counts, i);
}
static inline size_t jxl_tree_samples_predictor_index(const jxl_tree_samples* self,
                                        jxl_enc_predictor predictor) {
  for (size_t i = 0; i < jxl_array_len(&self->predictors); ++i) {
    if (*jxl_array_at_const(&self->predictors, i) == predictor) return i;
  }
  JXL_DASSERT(false);
  return 0;
}
static inline size_t jxl_tree_samples_num_property_values(const jxl_tree_samples* self,
                                           size_t property_index) {
  return jxl_array_len(&self->compact_properties[property_index]) + 1;
}
static inline size_t jxl_tree_samples_num_static_props(const jxl_tree_samples* self) {
  return self->num_static_props;
}
static inline size_t jxl_tree_samples_num_sample_props(const jxl_tree_samples* self) {
  return jxl_array_len(&self->props_to_use) - self->num_static_props;
}
static inline size_t jxl_tree_samples_static_property(const jxl_tree_samples* self,
                                        size_t property_index, size_t i) {
  return *jxl_array_at_const(&self->static_props[property_index], i);
}
static inline size_t jxl_tree_samples_sample_property(const jxl_tree_samples* self,
                                        size_t property_index, size_t i) {
  return *jxl_array_at_const(&self->props[property_index], i);
}
static inline int jxl_tree_samples_unquantize_property(const jxl_tree_samples* self,
                                         size_t property_index, uint32_t quant) {
  JXL_DASSERT(quant < jxl_array_len(&self->compact_properties[property_index]));
  return *jxl_array_at_const(&self->compact_properties[property_index], quant);
}
static inline jxl_enc_predictor jxl_tree_samples_predictor_from_index(const jxl_tree_samples* self,
                                               size_t index) {
  JXL_DASSERT(index < jxl_array_len(&self->predictors));
  return *jxl_array_at_const(&self->predictors, index);
}
static inline size_t jxl_tree_samples_property_from_index(const jxl_tree_samples* self,
                                           size_t index) {
  JXL_DASSERT(index < jxl_array_len(&self->props_to_use));
  return *jxl_array_at_const(&self->props_to_use, index);
}
static inline size_t jxl_tree_samples_num_predictors(const jxl_tree_samples* self) {
  return jxl_array_len(&self->predictors);
}
static inline size_t jxl_tree_samples_num_properties(const jxl_tree_samples* self) {
  return jxl_array_len(&self->props_to_use);
}
static inline void jxl_tree_samples_all_samples_done(jxl_tree_samples* self) {
  jxl_array_clear(&self->dedup_table_);
}
static inline uint32_t jxl_tree_samples_quantize_property(const jxl_tree_samples* self,
                                            uint32_t prop, pixel_type v) {
  JXL_DASSERT(prop >= self->num_static_props);
  v = jxl_clamp1_i(v, -kTreeSamplesPropertyRange, kTreeSamplesPropertyRange) +
      kTreeSamplesPropertyRange;
  return *jxl_array_at_const(&self->property_mapping[prop - self->num_static_props], v);
}
static inline uint32_t jxl_tree_samples_quantize_static_property(const jxl_tree_samples* self,
                                                  uint32_t prop, pixel_type v) {
  JXL_DASSERT(prop < self->num_static_props);
  v = jxl_clamp1_i(v, -kTreeSamplesPropertyRange, kTreeSamplesPropertyRange) +
      kTreeSamplesPropertyRange;
  return *jxl_array_at_const(&self->static_property_mapping[prop], v);
}

jxl_enc_status jxl_tokenize_tree(const jxl_tree *tree, jxl_token_stream *tokens,
                    jxl_tree *decoder_tree);

void jxl_collect_pixel_samples(const jxl_image *image, const jxl_modular_options *options,
                         uint32_t group_id, jxl_array_u32 *group_pixel_count,
                         jxl_array_u32 *channel_pixel_count,
                         jxl_array_i32 *pixel_samples, jxl_array_i32 *diff_samples);

jxl_enc_status jxl_compute_best_tree(jxl_tree_samples *tree_samples, float threshold,
                       const jxl_array_modular_multiplier_info *mul_info,
                       jxl_static_prop_range static_prop_range,
                       float fast_decode_multiplier, jxl_tree *tree);

#endif  // JXL_ENC_MODULAR_ENCODING_ENC_MA_H_
