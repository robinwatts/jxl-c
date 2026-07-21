// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_FRAME_DIMENSIONS_H_
#define LIB_JXL_FRAME_DIMENSIONS_H_

// jxl_frame_dimensions struct, block and group dimensions constants.

#include <stddef.h>

#include "lib/jxl/base/common.h"
#include "lib/jxl/base/compiler_specific.h"
#include "lib/jxl/base/rect.h"

// Block is the square grid of pixels to which an "energy compaction"
// transformation (e.g. DCT) is applied. Each block has its own AC quantizer.
enum {
  kBlockDim = 8,
  kDCTBlockSize = kBlockDim * kBlockDim,
  kGroupDim = 256,
  kGroupDimInBlocks = kGroupDim / kBlockDim
};
JXL_STATIC_ASSERT(kGroupDim % kBlockDim == 0,
                  "Group dim should be divisible by block dim");

// Dimensions of a frame, in pixels, and other derived dimensions.
// Computed from jxl_frame_header.
typedef struct jxl_frame_dimensions {
  // jxl_image size without any upsampling, i.e. original_size / upsampling.
  size_t xsize;
  size_t ysize;
  // jxl_image size after padding to a multiple of kBlockDim (if VarDCT mode).
  size_t xsize_padded;
  size_t ysize_padded;
  // jxl_image size in kBlockDim blocks.
  size_t xsize_blocks;
  size_t ysize_blocks;
  // jxl_image size in number of groups.
  size_t xsize_groups;
  // jxl_image size in number of DC groups.
  size_t xsize_dc_groups;
  // Number of AC or DC groups.
  size_t num_groups;
  size_t num_dc_groups;
  // Size of a group.
  size_t group_dim;
} jxl_frame_dimensions;

static inline void jxl_frame_dimensions_set(jxl_frame_dimensions* self, size_t xsize_px,
                                      size_t ysize_px, size_t group_size_shift,
                                      size_t max_hshift, size_t max_vshift,
                                      bool modular_mode, size_t upsampling) {
  size_t ysize_groups;
  size_t ysize_dc_groups;
  self->group_dim = (kGroupDim >> 1) << group_size_shift;
  self->xsize = jxl_div_ceil(xsize_px, upsampling);
  self->ysize = jxl_div_ceil(ysize_px, upsampling);
  self->xsize_blocks = jxl_div_ceil(self->xsize, kBlockDim << max_hshift)
                       << max_hshift;
  self->ysize_blocks = jxl_div_ceil(self->ysize, kBlockDim << max_vshift)
                       << max_vshift;
  self->xsize_padded = self->xsize_blocks * kBlockDim;
  self->ysize_padded = self->ysize_blocks * kBlockDim;
  if (modular_mode) {
    // Modular mode doesn't have any padding.
    self->xsize_padded = self->xsize;
    self->ysize_padded = self->ysize;
  }
  self->xsize_groups = jxl_div_ceil(self->xsize, self->group_dim);
  ysize_groups = jxl_div_ceil(self->ysize, self->group_dim);
  self->xsize_dc_groups = jxl_div_ceil(self->xsize_blocks, self->group_dim);
  ysize_dc_groups = jxl_div_ceil(self->ysize_blocks, self->group_dim);
  self->num_groups = self->xsize_groups * ysize_groups;
  self->num_dc_groups = self->xsize_dc_groups * ysize_dc_groups;
}

static inline jxl_rect jxl_frame_dimensions_block_group_rect(const jxl_frame_dimensions* self,
                                                 size_t group_index) {
  const size_t gx = group_index % self->xsize_groups;
  const size_t gy = group_index / self->xsize_groups;
  return jxl_rect_make_clamped(gx * (self->group_dim >> 3), gy * (self->group_dim >> 3),
                          self->group_dim >> 3, self->group_dim >> 3,
                          self->xsize_blocks, self->ysize_blocks);
}

static inline jxl_rect jxl_frame_dimensions_dc_group_rect(const jxl_frame_dimensions* self,
                                              size_t group_index) {
  const size_t gx = group_index % self->xsize_dc_groups;
  const size_t gy = group_index / self->xsize_dc_groups;
  return jxl_rect_make_clamped(gx * self->group_dim, gy * self->group_dim,
                          self->group_dim, self->group_dim, self->xsize_blocks,
                          self->ysize_blocks);
}

#endif  // LIB_JXL_FRAME_DIMENSIONS_H_
