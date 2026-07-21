// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_IMAGE_OPS_H_
#define LIB_JXL_IMAGE_OPS_H_

// Operations on images.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "lib/jxl/base/common.h"
#include "lib/jxl/base/compiler_specific.h"
#include "lib/jxl/base/rect.h"
#include "lib/jxl/base/status.h"
#include "lib/jxl/image.h"

static inline bool jxl_same_size_rect(const jxl_rect* image1, const jxl_rect* image2) {
  return jxl_rect_x_size(image1) == jxl_rect_x_size(image2) && jxl_rect_y_size(image1) == jxl_rect_y_size(image2);
}

// Converts int8 CfL map tiles into modular pixel_type (int32) planes.
static inline jxl_status jxl_convert_plane_and_clamp(const jxl_rect* rect_from, const jxl_image_sb* from,
                                   const jxl_rect* rect_to,
                                   jxl_image_i* JXL_RESTRICT to) {
  JXL_ENSURE(jxl_same_size_rect(rect_from, rect_to));
  for (size_t y = 0; y < jxl_rect_y_size(rect_to); ++y) {
    const int8_t* JXL_RESTRICT row_from = jxl_rect_const_row_sb(rect_from, from, y);
    int32_t* JXL_RESTRICT row_to = jxl_rect_row_i(rect_to, to, y);
    for (size_t x = 0; x < jxl_rect_x_size(rect_to); ++x) {
      row_to[x] = jxl_clamp1_i(row_from[x], INT32_MIN,
                              INT32_MAX);
    }
  }
  return jxl_ok_status();
}

static inline void jxl_fill_image_b(const uint8_t value, jxl_image_b* image) {
  for (size_t y = 0; y < jxl_image_b_y_size(image); ++y) {
    uint8_t* const JXL_RESTRICT row = jxl_image_b_row(image, y);
    for (size_t x = 0; x < jxl_image_b_x_size(image); ++x) {
      row[x] = value;
    }
  }
}

static inline void jxl_fill_image_i(const int32_t value, jxl_image_i* image) {
  for (size_t y = 0; y < jxl_image_i_y_size(image); ++y) {
    int32_t* const JXL_RESTRICT row = jxl_image_i_row(image, y);
    for (size_t x = 0; x < jxl_image_i_x_size(image); ++x) {
      row[x] = value;
    }
  }
}

static inline void jxl_zero_fill_image_sb(jxl_image_sb* image) {
  if (jxl_image_sb_x_size(image) == 0) return;
  for (size_t y = 0; y < jxl_image_sb_y_size(image); ++y) {
    int8_t* const JXL_RESTRICT row = jxl_image_sb_row(image, y);
    memset(row, 0, jxl_image_sb_x_size(image) * sizeof(int8_t));
  }
}

static inline void jxl_zero_fill_image_b(jxl_image_b* image) {
  if (jxl_image_b_x_size(image) == 0) return;
  for (size_t y = 0; y < jxl_image_b_y_size(image); ++y) {
    uint8_t* const JXL_RESTRICT row = jxl_image_b_row(image, y);
    memset(row, 0, jxl_image_b_x_size(image) * sizeof(uint8_t));
  }
}

static inline void jxl_zero_fill_image_i(jxl_image_i* image) {
  if (jxl_image_i_x_size(image) == 0) return;
  for (size_t y = 0; y < jxl_image_i_y_size(image); ++y) {
    int32_t* const JXL_RESTRICT row = jxl_image_i_row(image, y);
    memset(row, 0, jxl_image_i_x_size(image) * sizeof(int32_t));
  }
}

static inline void jxl_zero_fill_image_f(jxl_image_f* image) {
  if (jxl_image_f_x_size(image) == 0) return;
  for (size_t y = 0; y < jxl_image_f_y_size(image); ++y) {
    float* const JXL_RESTRICT row = jxl_image_f_row(image, y);
    memset(row, 0, jxl_image_f_x_size(image) * sizeof(float));
  }
}

static inline void jxl_fill_plane(const uint8_t value, jxl_image_b* image, jxl_rect rect) {
  for (size_t y = 0; y < jxl_rect_y_size(&rect); ++y) {
    uint8_t* JXL_RESTRICT row = jxl_rect_row_b(&rect, image, y);
    for (size_t x = 0; x < jxl_rect_x_size(&rect); ++x) {
      row[x] = value;
    }
  }
}

static inline void jxl_zero_fill_image3_i(jxl_image3_i* image) {
  for (size_t c = 0; c < 3; ++c) {
    for (size_t y = 0; y < jxl_image3_i_y_size(image); ++y) {
      int32_t* JXL_RESTRICT row = jxl_image3_i_plane_row(image, c, y);
      if (jxl_image3_i_x_size(image) != 0) {
        memset(row, 0, jxl_image3_i_x_size(image) * sizeof(int32_t));
      }
    }
  }
}

static inline void jxl_zero_fill_image3_f(jxl_image3_f* image) {
  for (size_t c = 0; c < 3; ++c) {
    for (size_t y = 0; y < jxl_image3_f_y_size(image); ++y) {
      float* JXL_RESTRICT row = jxl_image3_f_plane_row(image, c, y);
      if (jxl_image3_f_x_size(image) != 0) {
        memset(row, 0, jxl_image3_f_x_size(image) * sizeof(float));
      }
    }
  }
}


#endif  // LIB_JXL_IMAGE_OPS_H_
