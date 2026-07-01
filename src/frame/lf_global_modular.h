// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_FRAME_LF_GLOBAL_MODULAR_H_
#define JXL_FRAME_LF_GLOBAL_MODULAR_H_

#include "allocator.h"
#include "context.h"
#include "bitstream/bitstream.h"
#include "frame/error.h"
#include "frame/frame.h"
#include "image/image_internal.h"
#include "grid/alloc_tracker.h"
#include "modular/ma.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct {
    jxl_context *ctx;
    const jxl_parsed_image_header *image;
    const jxl_frame_header *frame;
    jxl_grid_alloc_tracker *tracker;
    int allow_partial;
} jxl_lf_global_modular_params;

/* Parse LF dequant, global MA, modular header, and gmodular (stream 0). Advances *bs. */
jxl_frame_status_t jxl_lf_global_modular_consume(jxl_allocator_state *alloc, jxl_bs *bs,
                                               const jxl_lf_global_modular_params *params,
                                               jxl_ma_config *global_ma, int *has_global_ma_out);

/*
 * Position *out_bs at the pass-group modular payload for (pass_idx, group_idx).
 * For single-entry TOC, consumes LF global from the group blob first.
 */
jxl_frame_status_t jxl_frame_modular_pass_group_bitstream(
    jxl_context *ctx, jxl_allocator_state *alloc, const jxl_frame *frame,
    const jxl_parsed_image_header *image, uint32_t pass_idx, uint32_t group_idx,
    jxl_ma_config *global_ma, int *has_global_ma_out, jxl_bs *out_bs, int allow_partial);

#endif /* JXL_FRAME_LF_GLOBAL_MODULAR_H_ */
