// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_VARDCT_ENCODE_H_
#define JXL_RENDER_VARDCT_ENCODE_H_

#include "frame/frame.h"
#include "frame/hf_global.h"
#include "frame/lf_global.h"
#include "frame/lf_group.h"
#include "grid/aligned_grid.h"
#include "image/image_internal.h"
#include "modular/param.h"
#include "modular/region.h"
#include "render/patch_render.h"
#include "render/vardct/frame_render.h"

typedef struct {
    jxl_grid_f32 grid;
    float *data;
    size_t width;
    size_t height;
    size_t stride;
} jxl_vardct_f32_buf;

typedef struct {
    int32_t *data[3];
    size_t width[3];
    size_t height[3];
    size_t stride[3];
} jxl_vardct_group_coeff_bufs;

/* VarDCT encode output (Rust render_vardct → ImageWithRegion before restoration). */
typedef struct {
    jxl_lf_global lf_global;
    jxl_hf_global hf_global;
    jxl_lf_group *lf_groups;
    uint32_t num_lf_groups;
    jxl_vardct_group_coeff_bufs *group_coeffs;
    uint32_t num_groups;
    jxl_modular_params mod_params;
    int has_mod_params;
    jxl_vardct_f32_buf fb_xyb[3];
    uint32_t color_sample_w;
    uint32_t color_sample_h;
    jxl_vardct_f32_buf lf_xyb[3];
    jxl_grid_f32 group_coeff_grid[3];
    size_t group_coeff_stride[3];
    /* Crop decode: buffers sized to modular_region / modular_lf_region (Rust ImageWithRegion). */
    int crop_sized_buffers;
    jxl_modular_region fb_region;
    jxl_modular_region lf_xyb_region;
    /* Rust render.rs: JPEG upsample + Gabor/EPF on fb before post-encode write/features. */
    int color_fb_filters_applied;
    int use_prepared_fb;
    float *prepared_fb_data[3];
    size_t prepared_fb_w;
    size_t prepared_fb_h;
    jxl_modular_region prepared_fb_region;
} jxl_vardct_encode_ctx;

void jxl_vardct_encode_ctx_init(jxl_vardct_encode_ctx *ctx);
void jxl_vardct_encode_ctx_free(jxl_allocator_state *alloc, jxl_vardct_encode_ctx *ctx);

/*
 * Parse lf/hf metadata, decode pass groups, synthesize color framebuffer.
 * filter_region: color_padded decode extent (NULL = full frame).
 */
jxl_status_t jxl_vardct_encode_frame(const jxl_vardct_render_params *params, jxl_frame *frame,
                                     const jxl_parsed_image_header *parsed,
                                     jxl_progressive_lf_store *lf_store,
                                     const jxl_modular_region *filter_region,
                                     jxl_vardct_encode_ctx *ctx);

#endif /* JXL_RENDER_VARDCT_ENCODE_H_ */
