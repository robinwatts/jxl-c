// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_VARDCT_CFL_HF_H_
#define JXL_RENDER_VARDCT_CFL_HF_H_

#include "render/subgrid_f32.h"
#include "vardct/lf.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct {
    const int32_t *data;
    size_t width;
    size_t height;
    size_t stride;
} jxl_const_subgrid_i32;

void jxl_chroma_from_luma_hf_grouped(jxl_subgrid_f32 coeff[3], jxl_const_subgrid_i32 x_from_y,
                                     jxl_const_subgrid_i32 b_from_y,
                                     const jxl_lf_channel_correlation *lf_chan_corr);

#endif /* JXL_RENDER_VARDCT_CFL_HF_H_ */
