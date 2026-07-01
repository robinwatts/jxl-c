// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_MODULAR_COMPOSE_H_
#define JXL_RENDER_MODULAR_COMPOSE_H_

#include "frame/frame_header.h"
#include "image/image_internal.h"
#include "modular/image.h"
#include "modular/region.h"
#include "render/patch_render.h"
#include "render/render_buffer.h"
#include "vardct/lf.h"

#include "allocator.h"
#include "jxl_oxide/jxl_status.h"

typedef struct {
    jxl_context *ctx;
    jxl_allocator_state *alloc;
    const jxl_parsed_image_header *parsed;
    const jxl_frame_header *fh;
    jxl_modular_image_destination *dest;
    const jxl_lf_channel_dequant *xyb_dequant;
    uint32_t bit_depth;
    uint32_t num_color_channels;
    uint32_t num_extra_channels;
    const jxl_modular_region *output_region;
    int has_crop;
    jxl_modular_region crop;
    /* When set, blend against canvas pixels; otherwise use ref slots via jxl_render_composite. */
    int prefer_canvas_base;
} jxl_modular_compose_params;

/*
 * Decode modular coefficients in dest onto canvas using composite_preprocess and either
 * canvas accumulation (prefer_canvas_base) or ref-slot blending (animation prereq path).
 */
jxl_status_t jxl_render_compose_modular_dest(const jxl_modular_compose_params *params,
                                             jxl_reference_store *refs, jxl_render *canvas);

/*
 * Run composite_preprocess on local and composite/blit onto canvas (shared by modular and VarDCT).
 */
jxl_status_t jxl_render_composite_local_frame(const jxl_modular_compose_params *params,
                                              jxl_render *local, jxl_reference_store *refs,
                                              jxl_render *canvas);

/*
 * Optional fused inverse-RCT + float canvas export. Unused on the default modular path,
 * which keeps i16 grids through inverse transforms and take_grid compose.
 * Returns 1 when export is active on dest->float_export.
 */
int jxl_render_prepare_modular_float_export(const jxl_modular_compose_params *params,
                                            jxl_modular_image_destination *dest,
                                            jxl_render *canvas);

#endif /* JXL_RENDER_MODULAR_COMPOSE_H_ */
