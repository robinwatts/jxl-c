// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#ifndef JXL_ENC_ENC_PARAMS_H_
#define JXL_ENC_ENC_PARAMS_H_

// Parameters and flags that govern JXL compression.

#include "common.h"
#include "enc_frame_header.h"
#include "modular/options.h"

// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding)
typedef struct jxl_compress_params {
  jxl_speed_tier speed_tier;
  int brotli_effort;

  jxl_color_transform color_transform;

  // Force usage of CfL when doing JPEG recompression. This can have unexpected
  // effects on the decoded pixels, while still being JPEG-compliant and
  // allowing reconstruction of the original JPEG.
  bool force_cfl_jpeg_recompression;

  // Use brotli compression for any boxes derived from a JPEG frame.
  bool jpeg_compress_boxes;

  // Preserve this metadata when doing JPEG recompression.
  bool jpeg_keep_exif;
  bool jpeg_keep_xmp;

  jxl_modular_options options;
} jxl_compress_params;

static inline void jxl_compress_params_construct_empty(jxl_compress_params* self) {
  self->speed_tier = kSquirrel;
  self->brotli_effort = -1;
  self->color_transform = kColorTransformXYB;
  self->force_cfl_jpeg_recompression = true;
  self->jpeg_compress_boxes = true;
  self->jpeg_keep_exif = true;
  self->jpeg_keep_xmp = true;
  jxl_modular_options_construct_empty(&self->options);
}

#endif  // JXL_ENC_ENC_PARAMS_H_
