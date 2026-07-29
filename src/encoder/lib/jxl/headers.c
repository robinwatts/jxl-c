// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "lib/jxl/headers.h"

#include <stddef.h>
#include <stdint.h>

#include "lib/jxl/base/common.h"
#include "lib/jxl/base/compiler_specific.h"
#include "lib/jxl/base/enc_status.h"
#include "lib/jxl/field_encodings.h"
#include "lib/jxl/fields.h"
#include "lib/jxl/frame_dimensions.h"

typedef struct jxl_rational {
  uint32_t num;
  uint32_t den;
} jxl_rational;

static inline jxl_rational jxl_rational_make(uint32_t num, uint32_t den) {
  jxl_rational r;
  r.num = num;
  r.den = den;
  return r;
}

// Returns floor(multiplicand * rational).
static uint32_t jxl_rational_mul_truncate(jxl_rational self, uint32_t multiplicand) {
  return (uint64_t)(multiplicand) * self.num / self.den;
}

static jxl_rational jxl_fixed_aspect_ratios(uint32_t ratio) {
  JXL_DASSERT(0 != ratio && ratio < 8);
  // Other candidates: 5/4, 7/5, 14/9, 16/10, 5/3, 21/9, 12/5
  const jxl_rational kRatios[7] = {jxl_rational_make(1, 1),    // square
                                   jxl_rational_make(12, 10),  //
                                   jxl_rational_make(4, 3),    // camera
                                   jxl_rational_make(3, 2),    // mobile camera
                                   jxl_rational_make(16, 9),   // camera/display
                                   jxl_rational_make(5, 4),    //
                                   jxl_rational_make(2, 1)};   //
  return kRatios[ratio - 1];
}

static uint32_t jxl_find_aspect_ratio(uint32_t xsize, uint32_t ysize) {
  for (uint32_t r = 1; r < 8; ++r) {
    if (xsize == jxl_rational_mul_truncate(jxl_fixed_aspect_ratios(r), ysize)) {
      return r;
    }
  }
  return 0;  // Must send xsize instead
}

size_t jxl_enc_size_header_x_size(const jxl_enc_size_header* self) {
  if (self->ratio_ != 0) {
    return jxl_rational_mul_truncate(jxl_fixed_aspect_ratios(self->ratio_), 
        (uint32_t)(jxl_enc_size_header_y_size(self)));
  }
  return self->small_ ? ((self->xsize_div8_minus_1_ + 1) * 8) : self->xsize_;
}

jxl_status jxl_enc_size_header_set(jxl_enc_size_header* self, size_t xsize64, size_t ysize64) {
  const size_t kDimensionCap = UINT32_MAX;
  if (xsize64 > kDimensionCap || ysize64 > kDimensionCap) {
    return JXL_FAILURE("jxl_image too large");
  }
  const uint32_t xsize32 = (uint32_t)(xsize64);
  const uint32_t ysize32 = (uint32_t)(ysize64);
  if (xsize64 == 0 || ysize64 == 0) return JXL_FAILURE("Empty image");
  self->ratio_ = jxl_find_aspect_ratio(xsize32, ysize32);
  self->small_ = ysize64 <= 256 && (ysize64 % kBlockDim) == 0 &&
                 (self->ratio_ != 0 ||
                  (xsize64 <= 256 && (xsize64 % kBlockDim) == 0));
  if (self->small_) {
    self->ysize_div8_minus_1_ = ysize32 / 8 - 1;
  } else {
    self->ysize_ = ysize32;
  }

  if (self->ratio_ == 0) {
    if (self->small_) {
      self->xsize_div8_minus_1_ = xsize32 / 8 - 1;
    } else {
      self->xsize_ = xsize32;
    }
  }
  JXL_ENSURE(jxl_enc_size_header_x_size(self) == xsize64);
  JXL_ENSURE(jxl_enc_size_header_y_size(self) == ysize64);
  return jxl_ok_status();
}

size_t jxl_preview_header_x_size(const jxl_preview_header* self) {
  if (self->ratio_ != 0) {
    return jxl_rational_mul_truncate(jxl_fixed_aspect_ratios(self->ratio_), 
        (uint32_t)(jxl_preview_header_y_size(self)));
  }
  return self->div8_ ? (self->xsize_div8_ * 8) : self->xsize_;
}

jxl_status jxl_enc_size_header_visit_fields(jxl_enc_size_header* self, jxl_visitor* JXL_RESTRICT visitor) {
  JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bool(visitor, false, &self->small_));

  if (jxl_status_ok(jxl_visitor_conditional(visitor, self->small_))) {
    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 5, 0, &self->ysize_div8_minus_1_));
  }
  if (jxl_status_ok(jxl_visitor_conditional(visitor, !self->small_))) {
    // (Could still be small, but non-multiple of 8.)
    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_bits_offset(9, 1), jxl_bits_offset(13, 1), jxl_bits_offset(18, 1), jxl_bits_offset(30, 1)), 1, &self->ysize_));
  }

  JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 3, 0, &self->ratio_));
  if (jxl_status_ok(jxl_visitor_conditional(visitor, self->ratio_ == 0 && self->small_))) {
    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 5, 0, &self->xsize_div8_minus_1_));
  }
  if (jxl_status_ok(jxl_visitor_conditional(visitor, self->ratio_ == 0 && !self->small_))) {
    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_bits_offset(9, 1), jxl_bits_offset(13, 1), jxl_bits_offset(18, 1), jxl_bits_offset(30, 1)), 1, &self->xsize_));
  }

  return jxl_ok_status();
}

jxl_status jxl_preview_header_visit_fields(jxl_preview_header* self, jxl_visitor* JXL_RESTRICT visitor) {
  JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bool(visitor, false, &self->div8_));

  if (jxl_status_ok(jxl_visitor_conditional(visitor, self->div8_))) {
    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(16), jxl_val(32), jxl_bits_offset(5, 1), jxl_bits_offset(9, 33)), 1, &self->ysize_div8_));
  }
  if (jxl_status_ok(jxl_visitor_conditional(visitor, !self->div8_))) {
    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_bits_offset(6, 1), jxl_bits_offset(8, 65), jxl_bits_offset(10, 321), jxl_bits_offset(12, 1345)), 1, &self->ysize_));
  }

  JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 3, 0, &self->ratio_));
  if (jxl_status_ok(jxl_visitor_conditional(visitor, self->ratio_ == 0 && self->div8_))) {
    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(16), jxl_val(32), jxl_bits_offset(5, 1), jxl_bits_offset(9, 33)), 1, &self->xsize_div8_));
  }
  if (jxl_status_ok(jxl_visitor_conditional(visitor, self->ratio_ == 0 && !self->div8_))) {
    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_bits_offset(6, 1), jxl_bits_offset(8, 65), jxl_bits_offset(10, 321), jxl_bits_offset(12, 1345)), 1, &self->xsize_));
  }

  return jxl_ok_status();
}

jxl_status jxl_enc_animation_header_visit_fields(jxl_enc_animation_header* self, jxl_visitor* JXL_RESTRICT visitor) {
  JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(100), jxl_val(1000), jxl_bits_offset(10, 1), jxl_bits_offset(30, 1)), 1, &self->tps_numerator));
  JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(1), jxl_val(1001), jxl_bits_offset(8, 1), jxl_bits_offset(10, 1)), 1, &self->tps_denominator));

  JXL_QUIET_RETURN_IF_ERROR(
      jxl_visitor_u32(visitor, jxl_u32_enc_make(jxl_val(0), jxl_bits(3), jxl_bits(16), jxl_bits(32)), 0, &self->num_loops));

  JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bool(visitor, false, &self->have_timecodes));
  return jxl_ok_status();
}

