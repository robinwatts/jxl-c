// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_FILTER_GABOR_H_
#define JXL_RENDER_FILTER_GABOR_H_

#include "allocator.h"
#include "frame/filter.h"
#include "render/filter/filter_util.h"
#include "render/subgrid_f32.h"

int jxl_apply_gabor_like_extent(jxl_context *ctx, jxl_filter_extent channels[3],
                                const jxl_gabor_filter *gab, float *scratch[3]);

#endif /* JXL_RENDER_FILTER_GABOR_H_ */
