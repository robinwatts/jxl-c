// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_FRAME_H_
#define JXL_RENDER_FRAME_H_

#include "bitstream/bitstream.h"
#include "frame/frame.h"
#include "frame/frame_header.h"
#include "frame/lf_global.h"
#include "frame/lf_group.h"
#include "image/image_internal.h"
#include "render/render_internal.h"
#include "modular/param.h"
#include "modular/region.h"
#include "render/render_buffer.h"
#include "render/patch_render.h"
#include "render/color_transform.h"
#include "render/modular_encode.h"
#include "render/render_util.h"
#include "render/subgrid_f32.h"
#include "render/vardct/vardct_encode.h"

#include "allocator.h"
#include "jxl_oxide/jxl_status.h"

/*
 * Pre-features stage (Rust render.rs after encode, before prepare_color_upsampling):
 * write color planes, JPEG upsample (YCbCr), Gabor/EPF restoration, YCbCr→RGB.
 * Pass color_fb[0].data == NULL when planes are already in r (modular path).
 */
typedef struct {
    jxl_context *ctx;
    jxl_allocator_state *alloc;
    const jxl_parsed_image_header *parsed;
    const jxl_frame_header *fh;
    jxl_render *r;
    const jxl_modular_region *output_region;
    const jxl_const_subgrid_f32 color_fb[3];
    const jxl_lf_group *lf_groups;
    uint32_t num_lf_groups;
    int ref_image_output;
    /* NULL: compute from output_region; else reuse (post_encode passes shared regions). */
    const jxl_render_padded_regions *padded_regions;
    /* Frame coords of color_fb when it is a crop-sized VarDCT buffer (not r-sized). */
    const jxl_modular_region *color_fb_region;
    /* VarDCT: JPEG upsample + restoration already applied on color_fb. */
    int skip_fb_filters;
} jxl_render_pre_features_params;

jxl_status_t jxl_render_pre_features_stage(const jxl_render_pre_features_params *params);

/*
 * Post-encode stage (Rust render.rs after encode, through render_features):
 * pre-features, optional opaque-alpha fill, optional gmodular EC extend (VarDCT),
 * then keyframe features pipeline.
 */
typedef struct {
    jxl_context *ctx;
    jxl_allocator_state *alloc;
    const jxl_parsed_image_header *parsed;
    const jxl_frame_header *fh;
    jxl_render *r;
    const jxl_modular_region *output_region;
    const jxl_reference_store *refs;
    const jxl_const_subgrid_f32 color_fb[3];
    const jxl_lf_group *lf_groups;
    uint32_t num_lf_groups;
    int ref_image_output;
    const jxl_lf_global *lf_global;
    /* VarDCT: finish + blit gmodular extra channels. Modular sets extend_gmodular=0. */
    int extend_gmodular;
    uint32_t gmodular_cs_w;
    uint32_t gmodular_cs_h;
    const jxl_modular_params *mod_params;
    int has_mod_params;
    /* Rust render_features noise seeding (get_previous_frames_visibility). */
    uint32_t visible_frames;
    uint32_t invisible_frames;
    const jxl_modular_region *color_fb_region;
    /* EC blit region when output_region targets a crop-sized buffer but gmodular extends full. */
    const jxl_modular_region *gmodular_extend_region;
    /* VarDCT: skip JPEG upsample + restoration in pre_features (already on fb). */
    int skip_fb_filters;
} jxl_render_post_encode_params;

/*
 * Rust render.rs after render_vardct: JPEG upsample (YCbCr), Gabor/EPF on fb_xyb.
 * Call immediately after jxl_vardct_encode_frame; post_encode skips duplicate filter work.
 */
jxl_status_t jxl_render_vardct_apply_color_filters(jxl_context *ctx, jxl_allocator_state *alloc,
                                                   const jxl_parsed_image_header *parsed,
                                                   const jxl_frame_header *fh,
                                                   jxl_vardct_encode_ctx *enc,
                                                   const jxl_modular_region *output_region);

jxl_status_t jxl_render_post_encode_stage(const jxl_render_post_encode_params *params);

/*
 * Rust util::convert_color_for_record — XYB/YCbCr to display space for intermediate
 * animation frames stored as references (!save_before_ct && !is_last).
 */
jxl_status_t jxl_render_convert_color_for_record(jxl_context *ctx, jxl_allocator_state *alloc,
                                                 const jxl_parsed_image_header *parsed,
                                                 const jxl_frame_header *fh, jxl_render *r,
                                                 int ref_image_output);

jxl_status_t jxl_render_post_encode_from_modular_result(
    jxl_context *ctx, jxl_allocator_state *alloc, const jxl_parsed_image_header *parsed,
    jxl_modular_encode_result *enc, const jxl_modular_region *output_region,
    jxl_reference_store *refs, jxl_render *r);

jxl_status_t jxl_render_post_encode_from_vardct_ctx(
    jxl_context *ctx, jxl_allocator_state *alloc, const jxl_parsed_image_header *parsed,
    const jxl_frame_header *fh, const jxl_vardct_encode_ctx *enc,
    const jxl_modular_region *output_region, const jxl_modular_region *gmodular_extend_region,
    const jxl_reference_store *refs, int ref_image_output, uint32_t visible_frames,
    uint32_t invisible_frames, jxl_render *r);

/*
 * Unified frame render (Rust render.rs): encode by encoding type, then post-encode.
 * Frame must be parsed and group payloads fed. modular_bitstream required for modular.
 */
typedef struct {
    const jxl_keyframe_render_params *params;
    const jxl_parsed_image_header *parsed;
    jxl_frame *frame;
    const uint8_t *codestream;
    size_t codestream_len;
    jxl_bs *modular_bitstream;
    jxl_reference_store *refs;
    jxl_progressive_lf_store *lf_store;
    /* When set, composite blends onto existing canvas pixels (animated keyframes). */
    int prefer_canvas_base;
    /* Rust get_previous_frames_visibility for noise seeding. */
    uint32_t visible_frames;
    uint32_t invisible_frames;
} jxl_render_frame_params;

jxl_status_t jxl_render_frame(const jxl_render_frame_params *params, jxl_render *r);

/* Shared keyframe tail: features pipeline. padded_regions NULL computes from output_region. */
jxl_status_t jxl_render_keyframe_features(jxl_context *ctx, jxl_allocator_state *alloc, jxl_render *r,
                                          const jxl_parsed_image_header *parsed,
                                          const jxl_frame_header *fh,
                                          const jxl_lf_global *lf_global,
                                          const jxl_modular_region *output_region,
                                          const jxl_reference_store *refs,
                                          const jxl_render_padded_regions *padded_regions,
                                          uint32_t visible_frames, uint32_t invisible_frames);

#endif /* JXL_RENDER_FRAME_H_ */
