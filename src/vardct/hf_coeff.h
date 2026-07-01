// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_VARDCT_HF_COEFF_H_
#define JXL_VARDCT_HF_COEFF_H_

#include "bitstream/bitstream.h"
#include "context.h"
#include "modular/param.h"
#include "modular/image.h"
#include "vardct/error.h"
#include "vardct/hf_metadata.h"
#include "vardct/hf_pass.h"
#include "vardct/lf.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct {
    int32_t *data;
    size_t width;
    size_t height;
    size_t stride;
} jxl_subgrid_i32;

typedef struct {
    const void *data;
    jxl_modular_sample_kind kind;
    size_t width;
    size_t height;
    size_t stride;
} jxl_lf_quant_subgrid_u32;

int32_t jxl_lf_quant_subgrid_sample(const jxl_lf_quant_subgrid_u32 *q, size_t x, size_t y);

typedef struct {
    jxl_context *ctx;
    uint32_t num_hf_presets;
    const jxl_hf_block_context *hf_block_ctx;
    jxl_block_info_subgrid block_info;
    uint32_t jpeg_upsampling[3];
    const jxl_lf_quant_subgrid_u32 *lf_quant;
    const jxl_hf_pass *hf_pass;
    uint32_t coeff_shift;
} jxl_hf_coeff_params;

jxl_vardct_status_t jxl_write_hf_coeff(jxl_bs *bs, const jxl_hf_coeff_params *params,
                                       jxl_subgrid_i32 hf_coeff_out[3]);

#endif /* JXL_VARDCT_HF_COEFF_H_ */
