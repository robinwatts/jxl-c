// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_VARDCT_DCT_2D_WASM128_H_
#define JXL_RENDER_VARDCT_DCT_2D_WASM128_H_

#include "allocator.h"
#include "render/subgrid_f32.h"
#include "render/vardct/dct.h"

int jxl_dct_2d_wasm128(jxl_allocator_state *alloc, jxl_subgrid_f32 io, jxl_dct_direction direction);

#endif /* JXL_RENDER_VARDCT_DCT_2D_WASM128_H_ */
