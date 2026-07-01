// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_MODULAR_REGION_H_
#define JXL_MODULAR_REGION_H_

#include "frame/frame_header.h"
#include "modular/image.h"

#include "jxl_oxide/jxl_types.h"

typedef struct {
    int32_t left;
    int32_t top;
    uint32_t width;
    uint32_t height;
} jxl_modular_region;

jxl_modular_region jxl_modular_region_with_size(uint32_t width, uint32_t height);

int jxl_modular_region_intersects(jxl_modular_region a, jxl_modular_region b);

jxl_modular_region jxl_modular_region_intersection(jxl_modular_region a, jxl_modular_region b);

/* Rust Region::container_aligned(group_dim). */
jxl_modular_region jxl_modular_region_container_aligned(jxl_modular_region region,
                                                      uint32_t grid_dim);

jxl_modular_region jxl_modular_region_pad(jxl_modular_region region, uint32_t pad);

/* Multiply left/top/width/height by 2^factor_log2 (Rust Region::upsample). */
jxl_modular_region jxl_modular_region_upsample(jxl_modular_region region, uint32_t factor_log2);

/* Divide left/top/width/height by 2^factor_log2 (Rust Region::downsample). */
jxl_modular_region jxl_modular_region_downsample(jxl_modular_region region, uint32_t factor_log2);

int jxl_modular_pass_group_intersects(const jxl_frame_header *fh, uint32_t group_idx,
                                      const jxl_modular_region *filter, uint32_t group_dim);

int jxl_modular_lf_group_intersects(const jxl_frame_header *fh, uint32_t lf_group_idx,
                                    const jxl_modular_region *filter);

/* Rust vardct/mod.rs aligned_region + aligned_lf_region for a decode request. */
void jxl_modular_vardct_decode_regions(const jxl_frame_header *fh, jxl_modular_region request,
                                     jxl_modular_region *pass_region_out,
                                     jxl_modular_region *lf_region_out);

/* Rust compute_modular_region: palette/squeeze force full-frame modular decode. */
jxl_modular_region jxl_modular_compute_region(const jxl_frame_header *frame_header,
                                              const jxl_modular_image_destination *dest,
                                              jxl_modular_region region, int is_lf);

#endif /* JXL_MODULAR_REGION_H_ */
