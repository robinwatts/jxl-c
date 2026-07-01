// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_FRAME_LF_GLOBAL_H_
#define JXL_FRAME_LF_GLOBAL_H_

#include "frame/error.h"
#include "frame/frame_header.h"
#include "frame/noise.h"
#include "frame/patch.h"
#include "frame/spline.h"
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
    jxl_grid_alloc_tracker *tracker;
    int allow_partial;
} jxl_lf_global_params;

typedef struct {
    int has_patches;
    jxl_patches patches;
    int has_noise;
    jxl_noise_parameters noise;
    int has_splines;
    jxl_splines splines;
    jxl_lf_channel_dequant lf_dequant;
    int has_vardct;
    jxl_quantizer quantizer;
    jxl_hf_block_context hf_block_ctx;
    jxl_lf_channel_correlation lf_chan_corr;
    jxl_ma_config global_ma;
    int has_global_ma;
    int global_ma_owns;
    jxl_modular_image_destination gmodular;
    int gmodular_used;
} jxl_lf_global;

void jxl_lf_global_init(jxl_lf_global *lf);
void jxl_lf_global_free(jxl_allocator_state *alloc, jxl_lf_global *lf);

jxl_frame_status_t jxl_lf_global_consume(jxl_allocator_state *alloc, jxl_bs *bs,
                                         const jxl_lf_global_params *params, jxl_lf_global *out);

/* Patches and noise precede lf_dequant in every LF global bitstream. */
jxl_frame_status_t jxl_lf_global_parse_prefix(jxl_allocator_state *alloc, jxl_bs *bs,
                                              const jxl_parsed_image_header *image,
                                              const jxl_frame_header *frame,
                                              jxl_patches *patches_out, jxl_splines *splines_out,
                                              jxl_noise_parameters *noise_out);

#endif /* JXL_FRAME_LF_GLOBAL_H_ */
