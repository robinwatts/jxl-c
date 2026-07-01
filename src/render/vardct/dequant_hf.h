// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_VARDCT_DEQUANT_HF_H_
#define JXL_RENDER_VARDCT_DEQUANT_HF_H_

#include "frame/frame_header.h"
#include "frame/pass_group.h"
#include "modular/region.h"
#include "render/subgrid_f32.h"
#include "vardct/dequant.h"
#include "vardct/lf.h"

#include "jxl_oxide/jxl_types.h"

typedef struct {
    float quant_bias[3];
    float quant_bias_numerator;
} jxl_opsin_inverse_matrix;

typedef struct {
    jxl_context *ctx;
    const jxl_dequant_matrix_set *dequant_matrices;
    const jxl_quantizer *quantizer;
    const jxl_opsin_inverse_matrix *opsin_inverse;
} jxl_hf_global_dequant;

void jxl_dequant_hf_varblock_grouped(jxl_context *ctx, jxl_subgrid_f32 out[3], uint32_t group_idx,
                                     const jxl_frame_header *frame_header,
                                     const jxl_hf_global_dequant *hf_global,
                                     const jxl_lf_group_view *lf_group,
                                     const jxl_modular_region *lf_region);

#endif /* JXL_RENDER_VARDCT_DEQUANT_HF_H_ */
