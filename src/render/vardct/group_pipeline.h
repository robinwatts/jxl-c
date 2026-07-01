// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_VARDCT_GROUP_PIPELINE_H_
#define JXL_RENDER_VARDCT_GROUP_PIPELINE_H_

#include "context.h"
#include "frame/frame_header.h"
#include "frame/pass_group.h"
#include "modular/region.h"
#include "render/subgrid_f32.h"
#include "render/vardct/dequant_hf.h"
#include "render/vardct/varblocks.h"

typedef struct {
    jxl_context *ctx;
    const jxl_frame_header *frame_header;
    const jxl_lf_group_view *lf_group;
    uint32_t group_idx;
    const jxl_hf_global_dequant *hf_global;
    const jxl_modular_region *lf_xyb_region;
    jxl_const_subgrid_f32 lf[3];
    jxl_subgrid_f32 coeff[3]; /* non-const array of subgrids */
} jxl_render_vardct_group_params;

void jxl_render_transform_with_lf_grouped(jxl_render_vardct_group_params *params);

void jxl_render_vardct_dequant_and_transform(jxl_render_vardct_group_params *params);

#endif /* JXL_RENDER_VARDCT_GROUP_PIPELINE_H_ */
