// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_FEATURES_SPLINE_H_
#define JXL_RENDER_FEATURES_SPLINE_H_

#include "allocator.h"
#include "frame/frame_header.h"
#include "frame/spline.h"
#include "modular/region.h"

#include <stddef.h>

int jxl_render_splines(jxl_allocator_state *alloc, const jxl_frame_header *fh,
                       const jxl_splines *splines, float corr_x, float corr_b, float *planes[3],
                       uint32_t width, uint32_t height, const jxl_modular_region *render_region);

#endif /* JXL_RENDER_FEATURES_SPLINE_H_ */
