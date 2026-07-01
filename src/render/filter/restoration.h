// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_FILTER_RESTORATION_H_
#define JXL_RENDER_FILTER_RESTORATION_H_

#include "allocator.h"
#include "frame/frame_header.h"
#include "frame/lf_group.h"
#include "modular/region.h"
#include "render/subgrid_f32.h"

/* color_padded: Rust color_padded_region in color-sample coords; NULL filters full channels.
 * output_region: when non-NULL, channels are a crop sub-buffer; color_padded stays absolute. */
int jxl_apply_restoration_filters(jxl_context *ctx, jxl_allocator_state *alloc,
                                  jxl_subgrid_f32 channels[3],
                                  const jxl_restoration_filter *restoration,
                                  const jxl_frame_header *frame_header,
                                  const jxl_lf_group *lf_groups, uint32_t num_lf_groups,
                                  const jxl_modular_region *color_padded,
                                  const jxl_modular_region *output_region);

#endif /* JXL_RENDER_FILTER_RESTORATION_H_ */
