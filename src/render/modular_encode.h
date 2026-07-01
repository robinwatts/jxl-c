// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_MODULAR_ENCODE_H_
#define JXL_RENDER_MODULAR_ENCODE_H_

#include "bitstream/bitstream.h"
#include "frame/frame_header.h"
#include "frame/lf_global.h"
#include "frame/patch.h"
#include "frame/spline.h"
#include "image/image_internal.h"
#include "modular/region.h"
#include "render/render_buffer.h"
#include "render/render_internal.h"

/* Modular encode output (Rust render_modular → ImageWithRegion before restoration). */
typedef struct {
    jxl_frame_header fh;
    uint32_t ec_upsampling_storage[256];
    jxl_patches patches;
    int has_patches;
    jxl_splines splines;
    int has_splines;
    jxl_noise_parameters noise;
    int has_noise;
    int valid;
    uint32_t visible_frames;
    uint32_t invisible_frames;
} jxl_modular_encode_result;

void jxl_modular_encode_result_init(jxl_modular_encode_result *result);
void jxl_modular_encode_result_free(jxl_allocator_state *alloc, jxl_modular_encode_result *result);

/*
 * Decode modular frames, blit planes into r (extend_from_gmodular equivalent).
 * Stops at keyframe params->keyframe_index; filter_region NULL computes color_padded locally.
 */
jxl_status_t jxl_modular_encode_keyframe(const jxl_keyframe_render_params *params,
                                         const jxl_parsed_image_header *parsed,
                                         const uint8_t *codestream, size_t cs_len, jxl_bs *bs,
                                         const jxl_modular_region *filter_region, jxl_render *r,
                                         jxl_modular_encode_result *result);

#endif /* JXL_RENDER_MODULAR_ENCODE_H_ */
