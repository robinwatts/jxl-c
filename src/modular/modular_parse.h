// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_MODULAR_PARSE_H_
#define JXL_MODULAR_PARSE_H_

#include "allocator.h"
#include "context.h"
#include "bitstream/bitstream.h"
#include "modular/ma.h"
#include "modular/param.h"
#include "modular/predictor.h"
#include "modular/transform/transform.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct {
    int use_global_tree;
    jxl_wp_header wp_params;
    uint32_t nb_transforms;
    jxl_transform_info *transform;
    size_t transform_len;
} jxl_modular_header;

void jxl_modular_header_init(jxl_modular_header *h);
void jxl_modular_header_free(jxl_allocator_state *alloc, jxl_modular_header *h);

jxl_modular_status_t jxl_modular_header_parse(jxl_allocator_state *alloc, jxl_bs *bs,
                                                jxl_modular_header *out);

typedef struct {
    jxl_modular_header header;
    jxl_ma_config ma_ctx;
    int ma_owns;
} jxl_modular_header_ma;

void jxl_modular_header_ma_init(jxl_modular_header_ma *hm);
void jxl_modular_header_ma_free(jxl_allocator_state *alloc, jxl_modular_header_ma *hm);

typedef struct {
    const jxl_modular_params *params;
    const jxl_ma_config *global_ma;
    jxl_grid_alloc_tracker *tracker;
    jxl_context *ctx;
    /* When set, channel layout stays pre-transform (for squeeze render); transforms are
     * validated on a scratch copy only. */
    int retain_pretransform_channels;
} jxl_modular_parse_ctx;

jxl_modular_status_t jxl_modular_read_local_header(jxl_allocator_state *alloc, jxl_bs *bs,
                                                 const jxl_modular_parse_ctx *ctx,
                                                 jxl_modular_header_ma *out,
                                                 jxl_modular_channels *channels_out);

#endif /* JXL_MODULAR_PARSE_H_ */
