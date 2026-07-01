// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_FILTER_YCBCR_H_
#define JXL_RENDER_FILTER_YCBCR_H_

#include "allocator.h"
#include "modular/param.h"
#include "render/subgrid_f32.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct jxl_context jxl_context;

int jxl_apply_jpeg_upsampling_single(jxl_allocator_state *alloc, jxl_const_subgrid_f32 src,
                                     jxl_channel_shift shift, uint32_t target_w, uint32_t target_h,
                                     float *dst, size_t dst_stride);

void jxl_modular_float_normalize_plane(float *data, size_t count, uint32_t bit_depth_bits);

void jxl_ycbcr_to_rgb(jxl_context *ctx, float *cb, float *y, float *cr, size_t count);

#endif /* JXL_RENDER_FILTER_YCBCR_H_ */
