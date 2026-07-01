// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_FRAME_SPLINE_H_
#define JXL_FRAME_SPLINE_H_

#include "frame/error.h"
#include "frame/frame_header.h"

#include "allocator.h"
#include "bitstream/bitstream.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct {
    int64_t *points;
    size_t points_len;
    uint64_t manhattan_distance;
    int32_t xyb_dct[3][32];
    int32_t sigma_dct[32];
} jxl_quant_spline;

typedef struct {
    jxl_quant_spline *quant_splines;
    size_t quant_splines_len;
    int32_t quant_adjust;
} jxl_splines;

void jxl_splines_init(jxl_splines *s);
void jxl_splines_free(jxl_allocator_state *alloc, jxl_splines *s);

jxl_frame_status_t jxl_splines_parse(jxl_allocator_state *alloc, jxl_bs *bs,
                                     const jxl_frame_header *frame, jxl_splines *out);

uint64_t jxl_splines_estimate_area(const jxl_splines *splines, float corr_x, float corr_b);

#endif /* JXL_FRAME_SPLINE_H_ */
