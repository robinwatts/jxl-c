// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "lib/jxl/chroma_from_luma.h"

#include <jxl/context.h>
#include "lib/jxl/enc_allocator.h"

#include <stddef.h>

#include "lib/jxl/base/common.h"
#include "lib/jxl/base/enc_status.h"
#include "lib/jxl/enc_image.h"
#include "lib/jxl/image_ops.h"

jxl_enc_status jxl_color_correlation_map_create(jxl_context* ctx,
                                 size_t xsize, size_t ysize,
                                 jxl_color_correlation_map* out) {
  jxl_color_correlation_map result;
  jxl_color_correlation_map_construct_empty(&result);
  size_t xblocks = jxl_div_ceil(xsize, kColorTileDim);
  size_t yblocks = jxl_div_ceil(ysize, kColorTileDim);
  jxl_enc_status status =
      jxl_image_sb_create(ctx, xblocks, yblocks, 0, &result.ytox_map);
  if (!jxl_enc_status_ok(status)) {
    jxl_color_correlation_map_destroy(&result);
    return status;
  }
  status =
      jxl_image_sb_create(ctx, xblocks, yblocks, 0, &result.ytob_map);
  if (!jxl_enc_status_ok(status)) {
    jxl_color_correlation_map_destroy(&result);
    return status;
  }
  jxl_zero_fill_image_sb(&result.ytox_map);
  jxl_zero_fill_image_sb(&result.ytob_map);
  // No-op CfL for JPEG: clear the default B-channel correlation.
  result.base_.base_correlation_b_ = 0;
  jxl_color_correlation_recompute_dc_factors(&result.base_);
  jxl_color_correlation_map_swap(out, &result);
  jxl_color_correlation_map_destroy(&result);
  return jxl_enc_ok_status();
}

