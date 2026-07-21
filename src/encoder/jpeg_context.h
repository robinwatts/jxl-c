// SPDX-License-Identifier: MIT OR Apache-2.0
// JPEG encoder session state attached to jxl_context.

#ifndef JXL_ENCODER_JPEG_CONTEXT_H_
#define JXL_ENCODER_JPEG_CONTEXT_H_

#include <jxl/memory_manager.h>

#include "lib/jxl/color_encoding_internal.h"

typedef struct jxl_context jxl_context;

typedef struct jxl_jpeg_encoder_context {
  jxl_memory_manager memory_manager;
  void* lcms;
  int srgb_ready;
  jxl_enc_color_encoding srgb[2];
} jxl_jpeg_encoder_context;

/* Called from jxl_context_init/fini when JPEG encoder is enabled. */
int jxl_jpeg_encoder_context_init(jxl_context* ctx);
void jxl_jpeg_encoder_context_fini(jxl_context* ctx);

#endif /* JXL_ENCODER_JPEG_CONTEXT_H_ */
