// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_FILTER_PADDED_F32_H_
#define JXL_RENDER_FILTER_PADDED_F32_H_

#include "allocator.h"
#include "frame/filter.h"
#include "frame/frame_header.h"
#include "render/subgrid_f32.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct {
    int32_t frame_left;
    int32_t frame_top;
    uint32_t frame_width;
    uint32_t frame_height;
} jxl_filter_frame_region;

typedef struct {
    size_t padding;
    size_t width;
    size_t height;
    size_t stride;
    float *data;
} jxl_padded_f32;

typedef struct {
    uint32_t epf_pad;
    uint32_t gab_pad;
    uint32_t pad_left;
    uint32_t pad_top;
    uint32_t pad_right;
    uint32_t pad_bottom;
    size_t buf_width;
    size_t buf_height;
    jxl_filter_frame_region frame;
} jxl_filter_pad_params;

void jxl_filter_pad_params_compute(jxl_filter_pad_params *out,
                                   const jxl_restoration_filter *restoration,
                                   uint32_t frame_width, uint32_t frame_height,
                                   int32_t frame_left, int32_t frame_top);

int jxl_padded_f32_alloc(jxl_allocator_state *alloc, size_t buf_width, size_t buf_height,
                         jxl_padded_f32 *out);

void jxl_padded_f32_free(jxl_allocator_state *alloc, jxl_padded_f32 *out);

int jxl_padded_f32_place(const jxl_subgrid_f32 *src, jxl_padded_f32 *dst, size_t dst_x,
                         size_t dst_y);

void jxl_padded_f32_mirror_trailing(jxl_padded_f32 *buf, size_t content_x, size_t content_y,
                                    size_t content_w, size_t content_h);

/* Mirror edge samples into [0,buf_w)×[0,buf_h) within an existing grid (in-place filter path). */
void jxl_subgrid_f32_mirror_trailing(jxl_subgrid_f32 grid, size_t content_x, size_t content_y,
                                     size_t content_w, size_t content_h, size_t buf_w,
                                     size_t buf_h);

int jxl_padded_f32_copy_region_to(const jxl_padded_f32 *src, size_t src_x, size_t src_y,
                                  jxl_subgrid_f32 dst);

jxl_subgrid_f32 jxl_padded_f32_subgrid(const jxl_padded_f32 *buf);

#endif /* JXL_RENDER_FILTER_PADDED_F32_H_ */
