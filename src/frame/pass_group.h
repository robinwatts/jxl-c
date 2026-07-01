// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_FRAME_PASS_GROUP_H_
#define JXL_FRAME_PASS_GROUP_H_

#include "bitstream/bitstream.h"
#include "frame/error.h"
#include "frame/frame_header.h"
#include "allocator.h"
#include "context.h"
#include "modular/image.h"
#include "modular/region.h"
#include "modular/ma.h"
#include "modular/modular_parse.h"
#include "modular/param.h"
#include "vardct/hf_coeff.h"
#include "vardct/hf_metadata.h"
#include "vardct/hf_pass.h"
#include "vardct/lf.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct {
    size_t block_left;
    size_t block_top;
    size_t block_width;
    size_t block_height;
} jxl_pass_group_block_slice;

typedef struct {
    const int32_t *data;
    size_t width;
    size_t height;
    size_t stride;
} jxl_cfl_subgrid_i32;

typedef struct {
    const jxl_block_info *block_info_data;
    size_t block_info_width;
    size_t block_info_height;
    size_t block_info_stride;
    const jxl_lf_quant_subgrid_u32 *lf_quant;
    jxl_cfl_subgrid_i32 x_from_y;
    jxl_cfl_subgrid_i32 b_from_y;
} jxl_lf_group_view;

typedef struct {
    uint32_t num_hf_presets;
    const jxl_hf_block_context *hf_block_ctx;
    const jxl_hf_pass *hf_passes;
    size_t hf_pass_count;
} jxl_hf_global_view;

typedef struct {
    jxl_context *ctx;
    const jxl_frame_header *frame_header;
    const jxl_lf_group_view *lf_group;
    uint32_t pass_idx;
    uint32_t group_idx;
    const jxl_hf_global_view *hf_global;
    jxl_subgrid_i32 hf_coeff_out[3]; /* mutable coeff grids */
    int allow_partial;
} jxl_pass_group_vardct_params;

int jxl_pass_group_block_slice_for_group(const jxl_frame_header *frame_header, uint32_t group_idx,
                                         size_t block_info_width, size_t block_info_height,
                                         jxl_pass_group_block_slice *out);

jxl_frame_status_t jxl_decode_pass_group_vardct(jxl_bs *bs,
                                                const jxl_pass_group_vardct_params *params);

int jxl_pass_group_block_info_subgrid(const jxl_frame_header *frame_header, uint32_t group_idx,
                                      const jxl_lf_group_view *lf_group,
                                      const jxl_modular_region *lf_region,
                                      jxl_block_info_subgrid *out);

typedef struct {
    jxl_context *ctx;
    jxl_allocator_state *alloc;
    const jxl_frame_header *frame_header;
    const jxl_ma_config *global_ma;
    const jxl_modular_params *modular_params;
    jxl_modular_image_destination *modular_dest;
    uint32_t pass_idx;
    uint32_t group_idx;
    int allow_partial;
} jxl_pass_group_modular_params;

/* Consume a pass-group modular header and decode coefficients into an existing destination. */
jxl_frame_status_t jxl_decode_pass_group_modular_coefficients(
    jxl_bs *bs, const jxl_pass_group_modular_params *params);

#endif /* JXL_FRAME_PASS_GROUP_H_ */
