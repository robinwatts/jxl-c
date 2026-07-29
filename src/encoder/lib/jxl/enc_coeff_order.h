// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_ENC_COEFF_ORDER_H_
#define LIB_JXL_ENC_COEFF_ORDER_H_

#include <stdint.h>

#include "lib/jxl/base/compiler_specific.h"
#include "lib/jxl/base/rect.h"
#include "lib/jxl/base/enc_status.h"
#include "lib/jxl/coeff_order_fwd.h"
#include "lib/jxl/common.h"
#include "lib/jxl/dct_util.h"
#include "lib/jxl/enc_bit_writer.h"
#include "lib/jxl/frame_dimensions.h"

#include "lib/jxl/layer_type.h"
#include "lib/jxl/ac_strategy.h"

// Orders that are actually used in part of image. `rect` is in block units.
typedef struct jxl_used_orders {
  uint32_t used;       // orders that are used
  uint32_t customize;  // orders that might be made non-default
} jxl_used_orders;

static inline void jxl_used_orders_set(jxl_used_orders* self, uint32_t used, uint32_t customize) {
  self->used = used;
  self->customize = customize;
}

static inline jxl_used_orders jxl_used_orders_make(uint32_t used, uint32_t customize) {
  jxl_used_orders out;
  jxl_used_orders_set(&out, used, customize);
  return out;
}

jxl_used_orders jxl_compute_used_orders(jxl_speed_tier speed,
                             const jxl_ac_strategy_image* ac_strategy,
                             const jxl_rect* rect);

// Modify zig-zag order, so that DCT bands with more zeros go later.
// Order of DCT bands with same number of zeros is untouched, so
// permutation will be cheaper to encode.
jxl_enc_status jxl_compute_coeff_order(jxl_context* ctx,
                              jxl_speed_tier speed, const jxl_ac_image* acs,
                         const jxl_ac_strategy_image* ac_strategy,
                         const jxl_frame_dimensions* frame_dim,
                         uint32_t* all_used_orders, uint32_t prev_used_acs,
                         uint32_t current_used_acs,
                         uint32_t current_used_orders,
                         coeff_order_t* JXL_RESTRICT order);

jxl_enc_status jxl_encode_coeff_orders(uint16_t used_orders,
                         const coeff_order_t* JXL_RESTRICT order,
                         jxl_bit_writer* writer, jxl_layer_type layer);


#endif  // LIB_JXL_ENC_COEFF_ORDER_H_
