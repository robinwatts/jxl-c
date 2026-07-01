// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_MODULAR_SAMPLE_H_
#define JXL_RENDER_MODULAR_SAMPLE_H_

#include "modular/image.h"
#include "modular/region.h"
#include "vardct/lf.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

/* Rust ImageBuffer::convert_to_float_modular_xyb / convert_to_float_modular. */
float jxl_modular_sample_color_float(const jxl_modular_image_destination *dest, size_t first_plane,
                                     uint32_t plane_idx, const jxl_lf_channel_dequant *xyb_dequant,
                                     const jxl_parsed_image_header *parsed, uint32_t bit_depth,
                                     size_t x, size_t y);

/* Blit a modular channel grid into a float plane (Rust append_channel_shifted).
 * Reads via jxl_modular_grid_sample_as_i32 — safe for i16 and i32 grid storage. */
int jxl_modular_blit_channel_to_plane(const jxl_modular_grid_i32 *grid,
                                      const jxl_modular_channel_info *info, uint32_t bit_depth_bits,
                                      uint32_t dst_stride, float *dst, uint32_t *out_gw,
                                      uint32_t *out_gh);

/* Blit a frame-space subregion of a modular channel into a plane sub-buffer. */
int jxl_modular_blit_channel_region_to_plane(const jxl_modular_grid_i32 *grid,
                                             const jxl_modular_channel_info *info,
                                             uint32_t bit_depth_bits, jxl_modular_region region,
                                             uint32_t dst_stride, float *dst, uint32_t dst_x0,
                                             uint32_t dst_y0);

#endif /* JXL_RENDER_MODULAR_SAMPLE_H_ */
