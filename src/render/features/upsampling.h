// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_FEATURES_UPSAMPLING_H_
#define JXL_RENDER_FEATURES_UPSAMPLING_H_

#include "allocator.h"
#include "render/subgrid_f32.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct {
    float up2[15];
    float up4[55];
    float up8[210];
} jxl_upsampling_weights;

void jxl_upsampling_weights_set_defaults(jxl_upsampling_weights *out);

/* Upsample `factor_log2` times (8x per 3 steps, then optional 2x/4x). */
int jxl_apply_nonseparable_upsampling_single(jxl_allocator_state *alloc, jxl_const_subgrid_f32 src,
                                             const jxl_upsampling_weights *weights,
                                             uint32_t factor_log2, uint32_t target_w,
                                             uint32_t target_h, float *dst, size_t dst_stride);

#endif /* JXL_RENDER_FEATURES_UPSAMPLING_H_ */
