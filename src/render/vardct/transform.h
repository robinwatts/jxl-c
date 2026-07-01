// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_VARDCT_TRANSFORM_H_
#define JXL_RENDER_VARDCT_TRANSFORM_H_

#include "allocator.h"
#include "render/subgrid_f32.h"
#include "vardct/dct_select.h"

void jxl_render_transform_varblock(jxl_context *ctx, jxl_allocator_state *alloc,
                                   jxl_subgrid_f32 coeff, jxl_transform_type dct_select);

/* Scalar reference path (uses jxl_dct_2d_generic); for tests and debugging. */
void jxl_render_transform_varblock_generic(jxl_context *ctx, jxl_allocator_state *alloc,
                                           jxl_subgrid_f32 coeff, jxl_transform_type dct_select);

/* Generic transform kernels with jxl_dct_2d (no transform SIMD); production fallback. */
void jxl_render_transform_varblock_fallback(jxl_context *ctx, jxl_allocator_state *alloc,
                                            jxl_subgrid_f32 coeff, jxl_transform_type dct_select);

#endif /* JXL_RENDER_VARDCT_TRANSFORM_H_ */
