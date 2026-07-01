// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_FILTER_EPF_H_
#define JXL_RENDER_FILTER_EPF_H_

#include "allocator.h"
#include "frame/filter.h"
#include "frame/frame_header.h"
#include "frame/lf_group.h"
#include "render/filter/filter_util.h"
#include "render/filter/padded_f32.h"
#include "render/subgrid_f32.h"

int jxl_apply_epf_extent(jxl_context *ctx, jxl_filter_extent channels[3],
                       const jxl_epf_filter *epf, const jxl_frame_header *frame_header,
                       const jxl_lf_group *lf_groups, uint32_t num_lf_groups,
                       const jxl_filter_frame_region *region, float *scratch[3]);

/* Run one EPF pass (step 0/1/2); for tests and debugging. */
int jxl_apply_epf_extent_step(jxl_context *ctx, jxl_filter_extent channels[3],
                              const jxl_epf_filter *epf, const jxl_frame_header *frame_header,
                              const jxl_lf_group *lf_groups, uint32_t num_lf_groups,
                              const jxl_filter_frame_region *region, float *scratch[3],
                              unsigned step);

#endif /* JXL_RENDER_FILTER_EPF_H_ */
