// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_RENDER_INTERNAL_H_
#define JXL_RENDER_RENDER_INTERNAL_H_

#include "context.h"
#include "image/image_internal.h"
#include "jxl_oxide/jxl_oxide.h"
#include "modular/region.h"
#include "render/patch_render.h"
#include "render/render_buffer.h"

typedef struct {
    jxl_context *ctx;
    jxl_allocator_state *alloc;
    const uint8_t *input;
    size_t input_len;
    /* Borrowed codestream from jxl_container_reader; skips container re-collection. */
    const uint8_t *codestream;
    size_t codestream_len;
    /* Borrowed parsed image header from jxl_decoder; skips header re-parse on render. */
    const jxl_parsed_image_header *parsed_header;
    size_t frames_bitstream_offset;
    char **error_out;
    uint32_t bit_depth;
    uint32_t num_color_channels;
    uint32_t num_extra_channels;
    jxl_crop crop;
    int has_crop;
    const jxl_modular_region *filter_region;
    const jxl_modular_region *output_region;
    uint32_t keyframe_index;
    /* Filled with the parsed image header used for render (color/orientation post-process). */
    jxl_parsed_image_header *parsed_out;
    /* Optional: use caller-owned stores instead of decoding prerequisites internally. */
    jxl_reference_store *external_refs;
    jxl_progressive_lf_store *external_lf_store;
    /* When set, animation ref chain is built incrementally from *animation_chain_upto + 1. */
    uint32_t *animation_chain_upto;
} jxl_keyframe_render_params;

void jxl_render_set_error(jxl_keyframe_render_params *params, const char *message);

jxl_status_t jxl_render_display_keyframe(const jxl_keyframe_render_params *params, jxl_render *r);

#endif /* JXL_RENDER_RENDER_INTERNAL_H_ */
