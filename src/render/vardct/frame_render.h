// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_VARDCT_FRAME_RENDER_H_
#define JXL_RENDER_VARDCT_FRAME_RENDER_H_

#include "allocator.h"
#include "frame/frame.h"
#include "image/image_internal.h"
#include "jxl_oxide/jxl_oxide.h"
#include "context.h"
#include "modular/region.h"
#include "render/patch_render.h"

struct jxl_render;

typedef struct {
    jxl_context *ctx;
    jxl_allocator_state *alloc;
    const uint8_t *input;
    size_t input_len;
    char **error_out;
    /* NULL = full frame (Rust request_image_region = full image). */
    const jxl_modular_region *filter_region;
    /* Exact output crop; NULL = write full frame buffer. When set, r is crop-sized. */
    const jxl_modular_region *output_region;
    /* When set, used instead of decoding prerequisites inside render (Rust spawn ref render). */
    jxl_reference_store *external_refs;
    jxl_progressive_lf_store *external_lf_store;
    /* Store XYB color planes for patch reference images (skip RGB conversion). */
    int ref_image_output;
    /* Prerequisite VarDCT render: frame already parsed (and fed) by caller. */
    const jxl_parsed_image_header *parsed_header;
    jxl_frame *loaded_frame;
    const uint8_t *codestream;
    size_t codestream_len;
} jxl_vardct_render_params;

jxl_status_t jxl_render_vardct_frame(const jxl_vardct_render_params *params, struct jxl_render *r);

jxl_status_t jxl_render_vardct_prereq_to_ref(const jxl_vardct_render_params *params,
                                             const jxl_parsed_image_header *parsed,
                                             jxl_frame *frame, jxl_ref_image *out);

/* Prerequisite VarDCT: encode + ref-based composite onto prereq_canvas (non-LF frames). */
jxl_status_t jxl_render_compose_vardct_prereq(const jxl_vardct_render_params *params,
                                              const jxl_parsed_image_header *parsed,
                                              jxl_frame *frame, jxl_reference_store *refs,
                                              jxl_render *canvas);

#endif /* JXL_RENDER_VARDCT_FRAME_RENDER_H_ */
