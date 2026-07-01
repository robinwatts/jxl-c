// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_FRAME_HF_GLOBAL_H_
#define JXL_FRAME_HF_GLOBAL_H_

#include "frame/error.h"
#include "frame/frame_header.h"
#include "image/image_internal.h"
#include "modular/ma.h"
#include "vardct/dequant.h"
#include "vardct/hf_pass.h"
#include "vardct/lf.h"

#include "allocator.h"

typedef struct {
    const jxl_parsed_image_header *image;
    const jxl_frame_header *frame;
    const jxl_ma_config *global_ma;
    const jxl_hf_block_context *hf_block_ctx;
} jxl_hf_global_params;

typedef struct {
    jxl_dequant_matrix_set dequant_matrices;
    uint32_t num_hf_presets;
    jxl_hf_pass *hf_passes;
    size_t hf_pass_count;
} jxl_hf_global;

void jxl_hf_global_init(jxl_hf_global *hf);
void jxl_hf_global_free(jxl_allocator_state *alloc, jxl_hf_global *hf);

jxl_frame_status_t jxl_hf_global_parse(jxl_context *ctx, jxl_allocator_state *alloc, jxl_bs *bs,
                                       const jxl_hf_global_params *params, jxl_hf_global *out);

#endif /* JXL_FRAME_HF_GLOBAL_H_ */
