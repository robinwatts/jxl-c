// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_VARDCT_LF_SMOOTH_H_
#define JXL_RENDER_VARDCT_LF_SMOOTH_H_

#include "allocator.h"
#include "render/subgrid_f32.h"
#include "vardct/lf.h"

void jxl_chroma_from_luma_lf(jxl_subgrid_f32 x, jxl_const_subgrid_f32 y, jxl_subgrid_f32 b,
                             const jxl_lf_channel_correlation *lf_chan_corr);

int jxl_adaptive_lf_smoothing(jxl_allocator_state *alloc, jxl_subgrid_f32 x, jxl_subgrid_f32 y,
                              jxl_subgrid_f32 b, const jxl_lf_channel_dequant *lf_dequant,
                              const jxl_quantizer *quantizer);

#endif /* JXL_RENDER_VARDCT_LF_SMOOTH_H_ */
