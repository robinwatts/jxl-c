// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_PATCH_RENDER_H_
#define JXL_RENDER_PATCH_RENDER_H_

#include "context.h"
#include "frame/frame.h"
#include "frame/patch.h"
#include "image/image_internal.h"
#include "modular/region.h"
#include "render/subgrid_f32.h"

#include "allocator.h"
#include "jxl_oxide/jxl_status.h"

#include "jxl_oxide/jxl_types.h"

typedef struct {
    int valid;
    uint32_t width;
    uint32_t height;
    uint32_t num_planes;
    int have_crop;
    int32_t x0;
    int32_t y0;
    float *samples;
    float **planes;
    uint32_t plane_w[3];
    uint32_t plane_h[3];
} jxl_ref_image;

typedef struct {
    jxl_ref_image slots[4];
} jxl_reference_store;

typedef struct {
    int valid;
    uint32_t width;
    uint32_t height;
    uint32_t frame_cs_w;
    uint32_t frame_cs_h;
    float *samples[3];
    float *plane[3];
    uint32_t stride[3];
    uint32_t plane_h[3];
} jxl_progressive_lf_image;

/* Rust RenderContext::lf_frame[4]: store at lf_level-1, consume at lf_level. */
typedef struct {
    jxl_progressive_lf_image slots[4];
} jxl_progressive_lf_store;

void jxl_ref_image_set_crop_from_frame(jxl_ref_image *img, const jxl_frame_header *fh);

void jxl_reference_store_init(jxl_reference_store *store);
void jxl_reference_store_free(jxl_allocator_state *alloc, jxl_reference_store *store);
void jxl_progressive_lf_image_free(jxl_allocator_state *alloc, jxl_progressive_lf_image *lf);
void jxl_progressive_lf_store_init(jxl_progressive_lf_store *store);
void jxl_progressive_lf_store_free(jxl_allocator_state *alloc, jxl_progressive_lf_store *store);
const jxl_progressive_lf_image *jxl_progressive_lf_store_get(const jxl_progressive_lf_store *store,
                                                             uint32_t lf_level);

jxl_status_t jxl_decode_prerequisite_frames(jxl_context *ctx, jxl_allocator_state *alloc,
                                            const uint8_t *input, size_t input_len,
                                            jxl_reference_store *refs,
                                            jxl_progressive_lf_store *lf_store,
                                            uint32_t target_keyframe_index,
                                            const uint8_t *codestream, size_t codestream_len);

jxl_status_t jxl_decode_modular_prereq_frame(jxl_context *ctx, jxl_allocator_state *alloc,
                                             const uint8_t *input,
                                             size_t input_len, const uint8_t *codestream,
                                             size_t cs_len, jxl_bs *bs,
                                             const jxl_parsed_image_header *parsed,
                                             jxl_frame *frame, jxl_reference_store *refs,
                                             jxl_render *canvas, jxl_ref_image *out);

typedef struct {
    float *data;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
} jxl_lf_xyb_plane;

/* Rust vardct Copy LFQuant: copy rendered LF frame planes into sample-resolution lf_xyb. */
jxl_status_t jxl_copy_lf_quant_from_progressive(jxl_allocator_state *alloc,
                                                const jxl_progressive_lf_image *lf,
                                                jxl_lf_xyb_plane lf_xyb[3],
                                                const jxl_frame_header *fh);

/* Rust transform_with_lf_grouped LF subgrid when lf_frame is a rendered reference. */
void jxl_lf_rendered_subgrid_for_group(const jxl_lf_xyb_plane lf_xyb[3],
                                       const jxl_progressive_lf_image *lf_src,
                                       const jxl_frame_header *fh, uint32_t group_idx,
                                       jxl_const_subgrid_f32 lf_out[3]);

/* Rust transform_with_lf_grouped LF subgrid; region size is in 8px LF blocks (luma grid). */
void jxl_lf_xyb_subgrid_for_group(const jxl_lf_xyb_plane lf_xyb[3], const jxl_frame_header *fh,
                                  uint32_t group_idx, int32_t lf_region_left,
                                  int32_t lf_region_top, uint32_t lf_region_width,
                                  uint32_t lf_region_height, jxl_const_subgrid_f32 lf_out[3]);

jxl_status_t jxl_apply_patches(jxl_allocator_state *alloc, const jxl_parsed_image_header *image,
                               const jxl_frame_header *frame, const jxl_patches *patches,
                               const jxl_reference_store *refs, float **planes,
                               uint32_t num_planes, uint32_t width, uint32_t height,
                               const jxl_modular_region *render_region);

#endif /* JXL_RENDER_PATCH_RENDER_H_ */
