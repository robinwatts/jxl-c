// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_VARDCT_DCT_2D_H_
#define JXL_RENDER_VARDCT_DCT_2D_H_

#include "allocator.h"
#include "render/subgrid_f32.h"
#include "render/vardct/dct.h"

void jxl_dct_2d(jxl_allocator_state *alloc, jxl_subgrid_f32 io, jxl_dct_direction direction);
void jxl_dct_2d_generic(jxl_allocator_state *alloc, jxl_subgrid_f32 io, jxl_dct_direction direction);

#endif /* JXL_RENDER_VARDCT_DCT_2D_H_ */
