// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_AC_STRATEGY_H_
#define LIB_JXL_AC_STRATEGY_H_

#include <jxl/context.h>
#include "enc_allocator.h"

#include <stddef.h>
#include <stdint.h>
#include "simd_util.h"  // kJxlMaxVectorSize

#include "base/compiler_specific.h"
#include "base/rect.h"
#include "base/enc_status.h"
#include "coeff_order_fwd.h"
#include "frame_dimensions.h"
#include "enc_image.h"
#include "image_ops.h"

// Defines the different kinds of transforms, and heuristics to choose between
// them.
// `jxl_ac_strategy` represents what transform should be used, and which sub-block of
// that transform we are currently in. Note that DCT4x4 is applied on all four
// 4x4 sub-blocks of an 8x8 block.
// `jxl_ac_strategy_image` defines which strategy should be used for each 8x8 block
// of the image. The highest 4 bits represent the strategy to be used, the
// lowest 4 represent the index of the block inside that strategy.


// Raw strategy types.
typedef enum jxl_ac_strategy_type {
  // Regular block size DCT
  kAcStrategyDCT = 0,
  // Encode pixels without transforming
  kAcStrategyIDENTITY = 1,
  // Use 2-by-2 DCT
  kAcStrategyDCT2X2 = 2,
  // Use 4-by-4 DCT
  kAcStrategyDCT4X4 = 3,
  // Use 16-by-16 DCT
  kAcStrategyDCT16X16 = 4,
  // Use 32-by-32 DCT
  kAcStrategyDCT32X32 = 5,
  // Use 16-by-8 DCT
  kAcStrategyDCT16X8 = 6,
  // Use 8-by-16 DCT
  kAcStrategyDCT8X16 = 7,
  // Use 32-by-8 DCT
  kAcStrategyDCT32X8 = 8,
  // Use 8-by-32 DCT
  kAcStrategyDCT8X32 = 9,
  // Use 32-by-16 DCT
  kAcStrategyDCT32X16 = 10,
  // Use 16-by-32 DCT
  kAcStrategyDCT16X32 = 11,
  // 4x8 and 8x4 DCT
  kAcStrategyDCT4X8 = 12,
  kAcStrategyDCT8X4 = 13,
  // Corner-DCT.
  kAcStrategyAFV0 = 14,
  kAcStrategyAFV1 = 15,
  kAcStrategyAFV2 = 16,
  kAcStrategyAFV3 = 17,
  // Larger DCTs
  kAcStrategyDCT64X64 = 18,
  kAcStrategyDCT64X32 = 19,
  kAcStrategyDCT32X64 = 20,
  // No transforms smaller than 64x64 are allowed below.
  kAcStrategyDCT128X128 = 21,
  kAcStrategyDCT128X64 = 22,
  kAcStrategyDCT64X128 = 23,
  kAcStrategyDCT256X256 = 24,
  kAcStrategyDCT256X128 = 25,
  kAcStrategyDCT128X256 = 26
} jxl_ac_strategy_type;

// Extremal values for the number of blocks/coefficients of a single strategy.
enum {
  kAcStrategyMaxCoeffBlocks = 32,
  kAcStrategyMaxBlockDim = kBlockDim * kAcStrategyMaxCoeffBlocks,
  kAcStrategyMaxCoeffArea = kAcStrategyMaxBlockDim * kAcStrategyMaxBlockDim
};
JXL_STATIC_ASSERT(
    (kAcStrategyMaxCoeffArea * sizeof(float)) % kJxlMaxVectorSize == 0,
    "Coefficient area is not a multiple of vector size");

enum { kAcStrategyNumValidStrategies = (int)kAcStrategyDCT128X256 + 1 };

typedef struct jxl_ac_strategy {
  jxl_ac_strategy_type strategy_;
  bool is_first_;
} jxl_ac_strategy;

void jxl_ac_strategy_compute_natural_coeff_order(jxl_ac_strategy self, coeff_order_t* order);
void jxl_ac_strategy_compute_natural_coeff_order_lut(jxl_ac_strategy self, coeff_order_t* lut);

static inline jxl_ac_strategy jxl_ac_strategy_make(jxl_ac_strategy_type strategy, bool is_first) {
  jxl_ac_strategy self;
  self.strategy_ = strategy;
  self.is_first_ = is_first;
  return self;
}

static inline bool jxl_ac_strategy_is_first_block(jxl_ac_strategy self) { return self.is_first_; }

static inline uint8_t jxl_ac_strategy_raw_strategy(jxl_ac_strategy self) {
  return (uint8_t)(self.strategy_);
}

static inline jxl_ac_strategy jxl_ac_strategy_from_raw_strategy(jxl_ac_strategy_type raw_strategy) {
  JXL_DASSERT((uint32_t)(raw_strategy) < kAcStrategyNumValidStrategies);
  return jxl_ac_strategy_make(raw_strategy, /*is_first=*/true);
}

static inline jxl_ac_strategy jxl_ac_strategy_from_raw_strategy_u8(uint8_t raw_strategy) {
  return jxl_ac_strategy_from_raw_strategy((jxl_ac_strategy_type)(raw_strategy));
}

static const uint8_t kAcStrategyCoveredBlocksX[kAcStrategyNumValidStrategies] = {
    1, 1, 1, 1,  2, 4,  1,  2,  1, 4, 2, 4, 1,  1, 1,  1,  1,  1,
    8, 4, 8, 16, 8, 16, 32, 16, 32};
static const uint8_t kAcStrategyCoveredBlocksY[kAcStrategyNumValidStrategies] = {
    1, 1, 1, 1,  2,  4, 2,  1,  4, 1, 4, 2, 1,  1,  1, 1,  1,  1,
    8, 8, 4, 16, 16, 8, 32, 32, 16};
static const uint8_t kAcStrategyLog2CoveredBlocks[kAcStrategyNumValidStrategies] = {
    0, 0, 0, 0, 2, 4, 1,  1, 2, 2, 3, 3, 0, 0, 0, 0,  0, 0,
    6, 5, 5, 8, 7, 7, 10, 9, 9};
JXL_STATIC_ASSERT(
    sizeof(kAcStrategyCoveredBlocksX) / sizeof(kAcStrategyCoveredBlocksX[0]) ==
        kAcStrategyNumValidStrategies,
    "Update covered-blocks-X LUT");
JXL_STATIC_ASSERT(
    sizeof(kAcStrategyCoveredBlocksY) / sizeof(kAcStrategyCoveredBlocksY[0]) ==
        kAcStrategyNumValidStrategies,
    "Update covered-blocks-Y LUT");
JXL_STATIC_ASSERT(
    sizeof(kAcStrategyLog2CoveredBlocks) /
            sizeof(kAcStrategyLog2CoveredBlocks[0]) ==
        kAcStrategyNumValidStrategies,
    "Update log2-covered-blocks LUT");

static inline size_t jxl_ac_strategy_covered_blocks_x(jxl_ac_strategy self) {
  return kAcStrategyCoveredBlocksX[(size_t)(self.strategy_)];
}

static inline size_t jxl_ac_strategy_covered_blocks_y(jxl_ac_strategy self) {
  return kAcStrategyCoveredBlocksY[(size_t)(self.strategy_)];
}

static inline size_t jxl_ac_strategy_log2_covered_blocks(jxl_ac_strategy self) {
  return kAcStrategyLog2CoveredBlocks[(size_t)(self.strategy_)];
}

// Class to use a certain row of the AC strategy.
typedef struct jxl_ac_strategy_row {
  const uint8_t* JXL_RESTRICT row_;
} jxl_ac_strategy_row;

static inline jxl_ac_strategy_row jxl_ac_strategy_row_make(const uint8_t* row) {
  jxl_ac_strategy_row self;
  self.row_ = row;
  return self;
}

static inline jxl_ac_strategy jxl_ac_strategy_row_at(jxl_ac_strategy_row self, size_t x) {
  jxl_ac_strategy_type strategy = (jxl_ac_strategy_type)(self.row_[x] >> 1);
  bool is_first = (bool)(self.row_[x] & 1);
  return jxl_ac_strategy_make(strategy, is_first);
}

typedef struct jxl_ac_strategy_image {
  jxl_image_b layers_;
} jxl_ac_strategy_image;

jxl_enc_status jxl_ac_strategy_image_create(jxl_context* ctx, size_t xsize,
                             size_t ysize, jxl_ac_strategy_image* out);
static inline void jxl_ac_strategy_image_construct_empty(jxl_ac_strategy_image* self) {
  jxl_image_b_construct_empty(&self->layers_);
}
static inline void jxl_ac_strategy_image_destroy(jxl_ac_strategy_image* self) {
  jxl_image_b_destroy(&self->layers_);
}
static inline void jxl_ac_strategy_image_fill_dct8(jxl_ac_strategy_image* self) {
  jxl_fill_plane(((uint8_t)(kAcStrategyDCT) << 1) | 1, &self->layers_,
            jxl_rect_from_size(jxl_image_b_x_size(&self->layers_), jxl_image_b_y_size(&self->layers_)));
}
static inline jxl_context* jxl_ac_strategy_image_ctx(
    const jxl_ac_strategy_image* self) {
  return jxl_image_b_ctx(&self->layers_);
}
static inline jxl_ac_strategy_row jxl_ac_strategy_image_const_row(const jxl_ac_strategy_image* self,
                                             size_t y, size_t x_prefix) {
  return jxl_ac_strategy_row_make(jxl_image_b_const_row(&self->layers_, y) + x_prefix);
}
static inline jxl_ac_strategy_row jxl_ac_strategy_image_const_row_rect(const jxl_ac_strategy_image* self,
                                                 const jxl_rect* rect, size_t y) {
  return jxl_ac_strategy_image_const_row(self, jxl_rect_y0(rect) + y, jxl_rect_x0(rect));
}
static inline size_t jxl_ac_strategy_image_x_size(const jxl_ac_strategy_image* self) {
  return jxl_image_b_x_size(&self->layers_);
}
static inline size_t jxl_ac_strategy_image_y_size(const jxl_ac_strategy_image* self) {
  return jxl_image_b_y_size(&self->layers_);
}
static inline void jxl_ac_strategy_image_swap(jxl_ac_strategy_image* self, jxl_ac_strategy_image* other) {
  jxl_image_b_swap(&self->layers_, &other->layers_);
}



#endif  // LIB_JXL_AC_STRATEGY_H_
