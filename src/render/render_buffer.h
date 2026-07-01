// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_RENDER_BUFFER_H_
#define JXL_RENDER_RENDER_BUFFER_H_

#include "allocator.h"
#include "frame/frame_header.h"
#include "frame/lf_global.h"
#include "image/image_internal.h"
#include "jxl_oxide/jxl_status.h"
#include "modular/image.h"
#include "modular/param.h"
#include "modular/region.h"
#include "render/features/upsampling.h"
#include "render/image_buffer.h"
#include "render/patch_render.h"
#include "render/subgrid_f32.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct jxl_render jxl_render;

/* Per-plane geometry matching Rust ImageWithRegion (Region, ChannelShift). */
typedef struct {
    jxl_modular_region region;
    jxl_channel_shift shift;
    uint32_t buf_width;
    uint32_t buf_height;
    /* Offsets when integer grids are materialized into canvas planes (modular compose blit). */
    uint32_t sample_x;
    uint32_t sample_y;
    uint32_t grid_x;
    uint32_t grid_y;
} jxl_render_plane_meta;

typedef struct {
    uint32_t src_x0;
    uint32_t src_y0;
    uint32_t dst_x0;
    uint32_t dst_y0;
    uint32_t blit_w;
    uint32_t blit_h;
    int valid;
} jxl_render_modular_placement;

#define JXL_RENDER_MAX_EC_BIT_DEPTHS 32u

struct jxl_render {
    uint32_t width;
    uint32_t height;
    uint32_t num_planes;
    uint32_t color_planes;
    uint32_t keyframe_index;
    uint32_t duration;
    int ct_done;
    float **planes;
    float *samples;
    jxl_image_buffer *bufs;
    jxl_render_plane_meta *meta;
    /* Borrowed from decoder context; enables lazy f32 materialization in jxl_render_plane. */
    jxl_allocator_state *materialize_alloc;
    uint32_t color_bit_depth_bits;
    uint32_t num_ec_bit_depths;
    uint8_t ec_bit_depth_bits[JXL_RENDER_MAX_EC_BIT_DEPTHS];
};

jxl_render *jxl_render_create(jxl_allocator_state *alloc, uint32_t num_planes,
                              uint32_t color_planes, uint32_t width, uint32_t height);
void jxl_render_free(jxl_allocator_state *alloc, jxl_render *r);

void jxl_render_init_all_planes(jxl_render *r, const jxl_modular_region *frame_region);

/*
 * Rust ImageWithRegion::clone_gray — duplicate single color plane to three for filters/CT.
 * Extra channel planes are shifted to indices 3+.
 */
jxl_status_t jxl_render_clone_gray(jxl_allocator_state *alloc, jxl_render *r);

/*
 * Rust ImageWithRegion::remove_color_channels — drop color planes above keep_count.
 */
jxl_status_t jxl_render_shrink_to_encoded_layout(jxl_allocator_state *alloc, jxl_render *r,
                                               uint32_t encoded_color, uint32_t extra_planes);

jxl_status_t jxl_render_remove_color_planes(jxl_allocator_state *alloc, jxl_render *r,
                                            uint32_t keep_count);

void jxl_render_set_plane_meta(jxl_render *r, uint32_t plane, const jxl_modular_region *region,
                               const jxl_channel_shift *shift, uint32_t buf_width,
                               uint32_t buf_height);

void jxl_render_blit_subgrid_to_plane(jxl_const_subgrid_f32 src, float *dst, uint32_t dst_stride);

jxl_status_t jxl_render_blit_fb_crop_to_plane(jxl_const_subgrid_f32 src, uint32_t src_x0,
                                              uint32_t src_y0, uint32_t blit_w, uint32_t blit_h,
                                              float *dst, uint32_t dst_stride);

/* Blit a color plane and record metadata; defers frame/jpeg upsampling when requested. */
jxl_status_t jxl_render_write_color_plane_from_fb(jxl_const_subgrid_f32 src, uint32_t cs_w,
                                                  uint32_t cs_h, const jxl_frame_header *fh,
                                                  uint32_t plane, jxl_render *r,
                                                  int defer_upsampling);

/* VarDCT: blit all color framebuffer planes into r (NULL fb[i].data skips write). */
jxl_status_t jxl_render_write_color_planes_from_fb(const jxl_const_subgrid_f32 color_fb[3],
                                                 const jxl_frame_header *fh,
                                                 const jxl_modular_region *output_region,
                                                 jxl_render *r,
                                                 const jxl_modular_region *color_fb_region,
                                                 int source_jpeg_upsampled);

const jxl_render_plane_meta *jxl_render_get_plane_meta(const jxl_render *r, uint32_t plane);

/*
 * Rust ImageWithRegion::extend_from_gmodular — move modular channel grids into render planes.
 * Transfers ownership from dest->image_channels[nb_meta + plane]; no i16→f32 copy.
 */
jxl_status_t jxl_render_extend_from_modular_dest(jxl_allocator_state *alloc,
                                                 jxl_modular_image_destination *dest,
                                                 const jxl_frame_header *fh, jxl_render *r,
                                                 uint32_t num_planes, int32_t ox, int32_t oy,
                                                 const jxl_render_modular_placement *placement);

/* Rust ImageBuffer::convert_to_float_modular for color planes. */
jxl_status_t jxl_render_convert_modular_color(jxl_allocator_state *alloc, jxl_render *r,
                                              uint32_t bit_depth_bits, uint32_t color_planes);

/* Materialize plane p as f32 (converts integer storage into render samples slot). */
jxl_status_t jxl_render_ensure_plane_f32(jxl_allocator_state *alloc, jxl_render *r, uint32_t plane,
                                         uint32_t bit_depth_bits);

jxl_status_t jxl_render_ensure_all_planes_f32(jxl_allocator_state *alloc, jxl_render *r,
                                              const jxl_parsed_image_header *parsed);

int jxl_render_plane_is_integer(const jxl_render *r, uint32_t plane);

int jxl_render_any_plane_integer(const jxl_render *r);

void jxl_render_bind_materialization(jxl_render *r, jxl_allocator_state *alloc,
                                     const jxl_parsed_image_header *parsed);

uint32_t jxl_render_plane_bit_depth(const jxl_render *r, uint32_t plane);

jxl_status_t jxl_render_materialize_plane_f32(jxl_allocator_state *alloc, jxl_render *r,
                                              uint32_t plane);

void jxl_render_prepare_color_upsampling(jxl_render *r, const jxl_frame_header *fh);

jxl_status_t jxl_render_upsample_plane_to_target(jxl_allocator_state *alloc, jxl_render *r,
                                                 uint32_t plane,
                                                 const jxl_upsampling_weights *weights,
                                                 uint32_t frame_upsampling);

/* JPEG chroma upsample to explicit dimensions (used before YCbCr when frame upsampling is deferred). */
jxl_status_t jxl_render_upsample_plane_jpeg(jxl_allocator_state *alloc, jxl_render *r,
                                          uint32_t plane, uint32_t target_w, uint32_t target_h);

jxl_status_t jxl_render_normalize_all_planes(jxl_allocator_state *alloc, jxl_render *r,
                                             const jxl_frame_header *fh,
                                             const jxl_upsampling_weights *weights,
                                             const jxl_modular_region *valid_region);

jxl_status_t jxl_render_upsample_nonseparable(jxl_allocator_state *alloc, jxl_render *r,
                                              const jxl_modular_region *valid_region,
                                              const jxl_frame_header *fh,
                                              const jxl_upsampling_weights *weights,
                                              int ec_to_color_only);

jxl_status_t jxl_render_features_pipeline(jxl_context *ctx, jxl_allocator_state *alloc, jxl_render *r,
                                          const jxl_parsed_image_header *parsed,
                                          const jxl_frame_header *fh,
                                          const jxl_upsampling_weights *weights,
                                          const jxl_modular_region *valid_region,
                                          const jxl_lf_global *lf_global,
                                          const jxl_reference_store *refs,
                                          uint32_t visible_frames,
                                          uint32_t invisible_frames,
                                          const jxl_modular_region *render_region);

jxl_status_t jxl_render_apply_orientation(jxl_allocator_state *alloc, jxl_render *r,
                                          uint32_t orientation);

#endif /* JXL_RENDER_RENDER_BUFFER_H_ */
