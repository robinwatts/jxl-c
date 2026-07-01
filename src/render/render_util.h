// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_RENDER_UTIL_H_
#define JXL_RENDER_RENDER_UTIL_H_

#include "frame/frame_header.h"
#include "image/image_internal.h"
#include "modular/region.h"

typedef struct {
    jxl_modular_region color_padded;
    jxl_modular_region upsampling_valid;
} jxl_render_padded_regions;

/* Rust util.rs: image_region_to_frame + pad_lf + pad_color / pad_upsampling. */
void jxl_render_compute_padded_regions(const jxl_parsed_image_header *parsed,
                                       const jxl_frame_header *fh,
                                       jxl_modular_region image_region,
                                       jxl_render_padded_regions *out);

/* Rust render.rs region setup: caller filter_region or computed color_padded. */
const jxl_modular_region *jxl_render_resolve_color_filter_region_for_image(
    const jxl_parsed_image_header *parsed, const jxl_frame_header *fh,
    jxl_modular_region image_region, const jxl_modular_region *caller_filter_region,
    jxl_modular_region *scratch);

const jxl_modular_region *jxl_render_resolve_color_filter_region(
    const jxl_parsed_image_header *parsed, const jxl_frame_header *fh,
    const jxl_modular_region *output_region, const jxl_modular_region *caller_filter_region,
    jxl_modular_region *scratch);

/* Rust image::composite output_frame_region in frame coordinates. */
jxl_modular_region jxl_render_composite_frame_region(const jxl_parsed_image_header *parsed,
                                                     const jxl_frame_header *fh,
                                                     jxl_modular_region oriented_image_region);

#endif /* JXL_RENDER_RENDER_UTIL_H_ */
