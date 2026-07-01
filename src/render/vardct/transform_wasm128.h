// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_VARDCT_TRANSFORM_WASM128_H_
#define JXL_RENDER_VARDCT_TRANSFORM_WASM128_H_

#include "allocator.h"
#include "render/subgrid_f32.h"
#include "vardct/dct_select.h"

int jxl_render_transform_varblock_wasm128(jxl_allocator_state *alloc, jxl_subgrid_f32 coeff,
                                          jxl_transform_type dct_select);

#endif /* JXL_RENDER_VARDCT_TRANSFORM_WASM128_H_ */
