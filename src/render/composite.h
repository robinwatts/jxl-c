// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_COMPOSITE_H_
#define JXL_RENDER_COMPOSITE_H_

#include "frame/frame_header.h"
#include "image/image_internal.h"
#include "modular/region.h"
#include "render/patch_render.h"
#include "render/render_buffer.h"

#include "allocator.h"
#include "jxl_oxide/jxl_status.h"

typedef struct jxl_context jxl_context;

/*
 * Rust image::composite_preprocess.
 * Returns 1 when blending can be skipped, 0 when composition is required, -1 on error.
 */
int jxl_render_composite_preprocess(jxl_context *ctx, jxl_allocator_state *alloc,
                                    const jxl_parsed_image_header *parsed,
                                    const jxl_frame_header *fh, jxl_render *r);

typedef struct {
    jxl_allocator_state *alloc;
    const jxl_parsed_image_header *parsed;
    const jxl_frame_header *fh;
    const jxl_render *new_frame;
    jxl_render *canvas;
    jxl_reference_store *refs;
    int prefer_canvas_base;
    jxl_modular_region oriented_image_region;
    uint32_t num_color_channels;
    uint32_t num_extra_channels;
} jxl_render_composite_params;

/* Rust image::composite + blend::blend for flat plane buffers. */
jxl_status_t jxl_render_composite(const jxl_render_composite_params *params);

/* Copy a composited frame region from canvas into a reference slot image. */
jxl_status_t jxl_ref_image_from_canvas(jxl_allocator_state *alloc, const jxl_frame_header *fh,
                                       const jxl_render *canvas, jxl_ref_image *out);

void jxl_ref_image_release(jxl_allocator_state *alloc, jxl_ref_image *img);

#endif /* JXL_RENDER_COMPOSITE_H_ */
