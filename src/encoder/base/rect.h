// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_BASE_RECT_H_
#define LIB_JXL_BASE_RECT_H_

#include <stddef.h>
#include <stdint.h>

#include "base/compiler_specific.h"
#include "base/enc_status.h"

// Rectangular region in image(s). Factoring this out of jxl_image instead of
// shifting the pointer by x0/y0 allows this to apply to multiple images with
// different resolutions (e.g. color transform and quantization field).
typedef struct jxl_rect {
  size_t x0_;
  size_t y0_;
  size_t xsize_;
  size_t ysize_;
} jxl_rect;

static inline size_t jxl_rect_clamped_size(size_t begin, size_t size_max,
                                     size_t end) {
  return (begin + size_max <= end) ? size_max
                                   : (end > begin ? end - begin : 0);
}

static inline jxl_rect jxl_rect_make(size_t xbegin, size_t ybegin, size_t xsize,
                            size_t ysize) {
  jxl_rect self;
  self.x0_ = xbegin;
  self.y0_ = ybegin;
  self.xsize_ = xsize;
  self.ysize_ = ysize;
  return self;
}

// Most windows are xsize_max * ysize_max, except those on the borders where
// begin + size_max > end.
static inline jxl_rect jxl_rect_make_clamped(size_t xbegin, size_t ybegin,
                                   size_t xsize_max, size_t ysize_max,
                                   size_t xend, size_t yend) {
  return jxl_rect_make(xbegin, ybegin, jxl_rect_clamped_size(xbegin, xsize_max, xend),
                  jxl_rect_clamped_size(ybegin, ysize_max, yend));
}

static inline jxl_rect jxl_rect_from_size(size_t xsize, size_t ysize) {
  return jxl_rect_make(0, 0, xsize, ysize);
}

static inline size_t jxl_rect_x0(const jxl_rect* self) { return self->x0_; }
static inline size_t jxl_rect_y0(const jxl_rect* self) { return self->y0_; }
static inline size_t jxl_rect_x_size(const jxl_rect* self) { return self->xsize_; }
static inline size_t jxl_rect_y_size(const jxl_rect* self) { return self->ysize_; }

#endif  // LIB_JXL_BASE_RECT_H_
