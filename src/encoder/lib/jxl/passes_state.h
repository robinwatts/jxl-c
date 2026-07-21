// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_PASSES_STATE_H_
#define LIB_JXL_PASSES_STATE_H_

#include <jxl/memory_manager.h>

#include <stddef.h>

#include "lib/jxl/ac_context.h"
#include "lib/jxl/base/array.h"
#include "lib/jxl/ac_strategy.h"
#include "lib/jxl/chroma_from_luma.h"
#include "lib/jxl/coeff_order_fwd.h"
#include "lib/jxl/frame_dimensions.h"
#include "lib/jxl/image.h"
#include "lib/jxl/image_metadata.h"
#include "lib/jxl/quant_weights.h"
#include "lib/jxl/quantizer.h"

// Structures that hold the (en/de)coder state for a JPEG XL kVarDCT
// (en/de)coder.


// State common to both encoder and decoder.
// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding)
typedef struct jxl_passes_shared_state {
  jxl_memory_manager* memory_manager;
  const jxl_codec_metadata* metadata;

  jxl_frame_dimensions frame_dim;

  // Control fields and parameters.
  jxl_ac_strategy_image ac_strategy;

  // Dequant matrices + quantizer.
  jxl_dequant_matrices matrices;
  jxl_quantizer quantizer;
  jxl_image_i raw_quant_field;

  // Per-block side information for EPF detail preservation.
  jxl_image_b epf_sharpness;

  jxl_color_correlation_map cmap;

  // Memory area for storing coefficient orders.
  // `coeff_order_size` is the size used by *one* set of coefficient orders (at
  // most kMaxCoeffOrderSize). A set of coefficient orders is present for each
  // pass.
  size_t coeff_order_size;
  jxl_array_u32 coeff_orders;

  // Per-block DC context indices for AC tokenization.
  jxl_image_b quant_dc;

  jxl_block_ctx_map block_ctx_map;

  // Number of pre-clustered set of histograms (with the same ctx map), per
  // pass. Encoded as num_histograms_ - 1.
  size_t num_histograms;

} jxl_passes_shared_state;

static inline void jxl_passes_shared_state_init(jxl_passes_shared_state* self,
                                  jxl_memory_manager* memory_manager) {
  self->memory_manager = memory_manager;
  jxl_ac_strategy_image_construct_empty(&self->ac_strategy);
  jxl_dequant_matrices_init(&self->matrices, memory_manager);
  jxl_quantizer_init(&self->quantizer, &self->matrices);
  jxl_image_i_construct_empty(&self->raw_quant_field);
  jxl_image_b_construct_empty(&self->epf_sharpness);
  jxl_color_correlation_map_construct_empty(&self->cmap);
  self->coeff_order_size = 0;
  jxl_array_construct_empty(&self->coeff_orders, memory_manager);
  jxl_image_b_construct_empty(&self->quant_dc);
  jxl_block_ctx_map_init(&self->block_ctx_map, memory_manager);
  self->num_histograms = 0;
}

static inline void jxl_passes_shared_state_destroy(jxl_passes_shared_state* self) {
  jxl_ac_strategy_image_destroy(&self->ac_strategy);
  jxl_dequant_matrices_destroy(&self->matrices);
  jxl_image_i_destroy(&self->raw_quant_field);
  jxl_image_b_destroy(&self->epf_sharpness);
  jxl_color_correlation_map_destroy(&self->cmap);
  jxl_array_destroy(&self->coeff_orders);
  jxl_image_b_destroy(&self->quant_dc);
  jxl_block_ctx_map_destroy(&self->block_ctx_map);
}


#endif  // LIB_JXL_PASSES_STATE_H_
