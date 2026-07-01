// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_FEATURES_NOISE_H_
#define JXL_RENDER_FEATURES_NOISE_H_

#include "allocator.h"
#include "frame/frame_header.h"
#include "frame/noise.h"
#include "modular/region.h"

#include "jxl_oxide/jxl_types.h"

int jxl_render_noise(jxl_allocator_state *alloc, const jxl_frame_header *header,
                     uint32_t visible_frames, uint32_t invisible_frames, float corr_x, float corr_b,
                     float *plane_x, float *plane_y, float *plane_b, uint32_t width,
                     uint32_t height, const jxl_noise_parameters *params,
                     const jxl_modular_region *render_region);

#ifdef JXL_OXIDE_TESTING
uint64_t jxl_noise_planes_ch0_checksum(const jxl_frame_header *header, uint32_t visible_frames,
                                       uint32_t invisible_frames);
#endif

#endif /* JXL_RENDER_FEATURES_NOISE_H_ */
