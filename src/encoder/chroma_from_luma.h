// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_CHROMA_FROM_LUMA_H_
#define LIB_JXL_CHROMA_FROM_LUMA_H_

// Chroma-from-luma, computed using heuristics to determine the best linear
// model for the X and B channels from the Y channel.

#include <jxl/context.h>
#include "enc_allocator.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "base/enc_status.h"
#include "cms/opsin_params.h"
#include "field_encodings.h"
#include "frame_dimensions.h"
#include "enc_image.h"
#include "base/compiler_specific.h"

// Tile is the rectangular grid of blocks that share color correlation
// parameters ("factor_x/b" such that residual_b = blue - Y * factor_b).
enum { kColorTileDim = 64 };

JXL_STATIC_ASSERT(kColorTileDim % kBlockDim == 0,
                  "jxl_color tile dim should be divisible by block dim");
enum { kColorTileDimInBlocks = kColorTileDim / kBlockDim };

JXL_STATIC_ASSERT(kGroupDimInBlocks % kColorTileDimInBlocks == 0,
                  "Group dim should be divisible by color tile dim");

enum { kDefaultColorFactor = 84 };

// JPEG DCT coefficients are at most 1024. CfL constants are at most 127, and
// the ratio of two entries in a JPEG quantization table is at most 255. Thus,
// since the CfL denominator is 84, this leaves 12 bits of mantissa to be used.
// For extra caution, we use 11.
enum { kCFLFixedPointPrecision = 11 };
enum { kCFLFixedPointRatioMax =
           ((int32_t)(256) << kCFLFixedPointPrecision) - 1 };

static inline jxl_u32_enc jxl_color_factor_dist(void) {
  return jxl_u32_enc_make(jxl_val(kDefaultColorFactor), jxl_val(256), jxl_bits_offset(8, 2),
                    jxl_bits_offset(16, 258));
}

typedef struct jxl_color_correlation {
  float dc_factors_[4];
  // range of factor: -1.51 to +1.52
  uint32_t color_factor_;
  float color_scale_;
  float base_correlation_x_;
  float base_correlation_b_;
  int32_t ytox_dc_;
  int32_t ytob_dc_;
} jxl_color_correlation;

static inline float jxl_color_correlation_yto_x_ratio(const jxl_color_correlation* self,
                                              int32_t x_factor) {
  return self->base_correlation_x_ + x_factor * self->color_scale_;
}

static inline float jxl_color_correlation_yto_b_ratio(const jxl_color_correlation* self,
                                              int32_t b_factor) {
  return self->base_correlation_b_ + b_factor * self->color_scale_;
}

static inline int32_t jxl_color_correlation_ratio_jpeg(int32_t factor) {
  return factor * (1 << kCFLFixedPointPrecision) / kDefaultColorFactor;
}

static inline int32_t jxl_color_correlation_get_y_to_xdc(const jxl_color_correlation* self) {
  return self->ytox_dc_;
}
static inline int32_t jxl_color_correlation_get_y_to_bdc(const jxl_color_correlation* self) {
  return self->ytob_dc_;
}
static inline float jxl_color_correlation_get_color_factor(const jxl_color_correlation* self) {
  return self->color_factor_;
}
static inline float jxl_color_correlation_get_base_correlation_x(
    const jxl_color_correlation* self) {
  return self->base_correlation_x_;
}
static inline float jxl_color_correlation_get_base_correlation_b(
    const jxl_color_correlation* self) {
  return self->base_correlation_b_;
}
static inline const float* jxl_color_correlation_dc_factors(
    const jxl_color_correlation* self) {
  return self->dc_factors_;
}

static inline void jxl_color_correlation_recompute_dc_factors(jxl_color_correlation* self) {
  self->dc_factors_[0] = jxl_color_correlation_yto_x_ratio(self, self->ytox_dc_);
  self->dc_factors_[2] = jxl_color_correlation_yto_b_ratio(self, self->ytob_dc_);
}

static inline void jxl_color_correlation_construct_empty(jxl_color_correlation* self) {
  memset(self->dc_factors_, 0, sizeof(self->dc_factors_));
  self->color_factor_ = kDefaultColorFactor;
  self->color_scale_ = 1.0f / kDefaultColorFactor;
  self->base_correlation_x_ = 0.0f;
  self->base_correlation_b_ = kYToBRatio;
  self->ytox_dc_ = 0;
  self->ytob_dc_ = 0;
}

typedef struct jxl_color_correlation_map {
  jxl_image_sb ytox_map;
  jxl_image_sb ytob_map;

  jxl_color_correlation base_;
} jxl_color_correlation_map;

jxl_enc_status jxl_color_correlation_map_create(jxl_context* ctx, size_t xsize,
                                 size_t ysize, jxl_color_correlation_map* out);

static inline const jxl_color_correlation* jxl_color_correlation_map_base(
    const jxl_color_correlation_map* self) {
  return &self->base_;
}
static inline void jxl_color_correlation_map_construct_empty(jxl_color_correlation_map* self) {
  jxl_image_sb_construct_empty(&self->ytox_map);
  jxl_image_sb_construct_empty(&self->ytob_map);
  jxl_color_correlation_construct_empty(&self->base_);
}
static inline void jxl_color_correlation_map_destroy(jxl_color_correlation_map* self) {
  jxl_image_sb_destroy(&self->ytox_map);
  jxl_image_sb_destroy(&self->ytob_map);
}
static inline void jxl_color_correlation_map_swap(jxl_color_correlation_map* self,
                                           jxl_color_correlation_map* other) {
  jxl_image_sb_swap(&self->ytox_map, &other->ytox_map);
  jxl_image_sb_swap(&self->ytob_map, &other->ytob_map);
  jxl_color_correlation tb = self->base_;
  self->base_ = other->base_;
  other->base_ = tb;
}

#endif  // LIB_JXL_CHROMA_FROM_LUMA_H_
