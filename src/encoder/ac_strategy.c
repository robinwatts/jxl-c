// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#include "ac_strategy.h"

#include <jxl/context.h>
#include "enc_allocator.h"

#include "base/bits.h"
#include "base/common.h"
#include "base/enc_status.h"
#include "coeff_order_fwd.h"
#include "frame_dimensions.h"
#include "enc_image.h"


// Tries to generalize zig-zag order to non-square blocks. Surprisingly, in
// square block frequency along the (i + j == const) diagonals is roughly the
// same. For historical reasons, consecutive diagonals are traversed
// in alternating directions - so called "zig-zag" (or "snake") order.
static void jxl_coeff_order_and_lut(bool is_lut, jxl_ac_strategy acs, coeff_order_t* out) {
  size_t cx = jxl_ac_strategy_covered_blocks_x(acs);
  size_t cy = jxl_ac_strategy_covered_blocks_y(acs);
  jxl_coefficient_layout(&cy, &cx);

  // jxl_coefficient_layout ensures cx >= cy.
  // We compute the zigzag order for a cx x cx block, then discard all the
  // lines that are not multiple of the ratio between cx and cy.
  size_t xs = cx / cy;
  size_t xsm = xs - 1;
  size_t xss = jxl_ceil_log2_nonzero32((uint32_t)(xs));
  // First half of the block
  size_t cur = cx * cy;
  for (size_t i = 0; i < cx * kBlockDim; i++) {
    for (size_t j = 0; j <= i; j++) {
      size_t x = j;
      size_t y = i - j;
      if (i % 2) jxl_swap(&x, &y);
      if ((y & xsm) != 0) continue;
      y >>= xss;
      size_t val = 0;
      if (x < cx && y < cy) {
        val = y * cx + x;
      } else {
        val = cur++;
      }
      if (is_lut) {
        out[y * cx * kBlockDim + x] = val;
      } else {
        out[val] = y * cx * kBlockDim + x;
      }
    }
  }
  // Second half
  for (size_t ip = cx * kBlockDim - 1; ip > 0; ip--) {
    size_t i = ip - 1;
    for (size_t j = 0; j <= i; j++) {
      size_t x = cx * kBlockDim - 1 - (i - j);
      size_t y = cx * kBlockDim - 1 - j;
      if (i % 2) jxl_swap(&x, &y);
      if ((y & xsm) != 0) continue;
      y >>= xss;
      size_t val = cur++;
      if (is_lut) {
        out[y * cx * kBlockDim + x] = val;
      } else {
        out[val] = y * cx * kBlockDim + x;
      }
    }
  }
}

void jxl_ac_strategy_compute_natural_coeff_order(jxl_ac_strategy self, coeff_order_t* order) {
  jxl_coeff_order_and_lut(/*is_lut=*/false, self, order);
}
void jxl_ac_strategy_compute_natural_coeff_order_lut(jxl_ac_strategy self,
                                           coeff_order_t* lut) {
  jxl_coeff_order_and_lut(/*is_lut=*/true, self, lut);
}

jxl_enc_status jxl_ac_strategy_image_create(jxl_context* ctx, size_t xsize,
                               size_t ysize, jxl_ac_strategy_image* out) {
  jxl_ac_strategy_image img;
  jxl_ac_strategy_image_construct_empty(&img);
  jxl_enc_status status =
      jxl_image_b_create(ctx, xsize, ysize, 0, &img.layers_);
  if (!jxl_enc_status_ok(status)) {
    jxl_ac_strategy_image_destroy(&img);
    return status;
  }
  jxl_ac_strategy_image_swap(out, &img);
  jxl_ac_strategy_image_destroy(&img);
  return jxl_enc_ok_status();
}

