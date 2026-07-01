// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_FRAME_LF_GROUP_H_
#define JXL_FRAME_LF_GROUP_H_

#include "frame/error.h"
#include "frame/frame_header.h"
#include "frame/pass_group.h"
#include "image/image_internal.h"
#include "modular/image.h"
#include "modular/ma.h"
#include "vardct/lf.h"

#include "allocator.h"
#include "context.h"
#include "grid/alloc_tracker.h"

typedef struct {
    jxl_context *ctx;
    const jxl_parsed_image_header *image;
    const jxl_frame_header *frame;
    const jxl_quantizer *quantizer;
    const jxl_ma_config *global_ma;
    jxl_modular_image_destination *gmodular;
    uint32_t lf_group_idx;
    jxl_grid_alloc_tracker *tracker;
    int allow_partial;
    int modular_from_pass_group;
} jxl_lf_group_params;

typedef struct {
    uint8_t extra_precision;
    jxl_modular_image_destination lf_quant;
    int has_lf_coeff;

    jxl_block_info *block_info;
    size_t block_info_width;
    size_t block_info_height;
    size_t block_info_stride;

    jxl_lf_quant_subgrid_u32 lf_quant_view[3];

    int32_t *x_from_y;
    int32_t *b_from_y;
    size_t cfl_width;
    size_t cfl_height;
    size_t cfl_stride;

    float *epf_sigma;
    size_t epf_sigma_width;
    size_t epf_sigma_height;
    size_t epf_sigma_stride;

    int has_hf_meta;
    int partial;
} jxl_lf_group;

void jxl_lf_group_init(jxl_lf_group *g);
void jxl_lf_group_free(jxl_allocator_state *alloc, jxl_lf_group *g);

jxl_frame_status_t jxl_lf_group_parse(jxl_allocator_state *alloc, jxl_bs *bs,
                                      const jxl_lf_group_params *params, jxl_lf_group *out);

jxl_frame_status_t jxl_decode_lf_group_modular_coefficients(jxl_allocator_state *alloc,
                                                            jxl_bs *bs,
                                                            const jxl_lf_group_params *params);

void jxl_lf_group_fill_view(const jxl_lf_group *g, jxl_lf_group_view *out);

#endif /* JXL_FRAME_LF_GROUP_H_ */
