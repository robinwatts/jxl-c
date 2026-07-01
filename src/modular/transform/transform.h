// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_MODULAR_TRANSFORM_H_
#define JXL_MODULAR_TRANSFORM_H_

#include "allocator.h"
#include "bitstream/bitstream.h"
#include "modular/channel.h"
#include "modular/param.h"
#include "modular/error.h"
#include "modular/predictor.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct {
    uint32_t begin_c;
    uint32_t rct_type;
} jxl_transform_rct;

typedef struct {
    uint32_t begin_c;
    uint32_t num_c;
    uint32_t nb_colours;
    uint32_t nb_deltas;
    jxl_predictor d_pred;
    int has_wp_header;
    jxl_wp_header wp_header;
} jxl_transform_palette;

typedef struct {
    int horizontal;
    int in_place;
    uint32_t begin_c;
    uint32_t num_c;
} jxl_squeeze_params;

typedef struct {
    uint32_t num_sq;
    jxl_squeeze_params *sp;
    size_t sp_len;
} jxl_transform_squeeze;

typedef enum {
    JXL_TRANSFORM_KIND_RCT = 0,
    JXL_TRANSFORM_KIND_PALETTE = 1,
    JXL_TRANSFORM_KIND_SQUEEZE = 2,
} jxl_transform_kind;

typedef struct {
    jxl_transform_kind kind;
    union {
        jxl_transform_rct rct;
        jxl_transform_palette palette;
        jxl_transform_squeeze squeeze;
    } u;
} jxl_transform_info;

void jxl_transform_rct_init_defaults(jxl_transform_rct *rct);
void jxl_transform_squeeze_init(jxl_transform_squeeze *sq);
void jxl_transform_squeeze_free(jxl_allocator_state *alloc, jxl_transform_squeeze *sq);
void jxl_transform_squeeze_rebuild_default_params(jxl_allocator_state *alloc,
                                                jxl_transform_squeeze *sq,
                                                const jxl_modular_channels *channels);

typedef struct {
    size_t begin;
    size_t num_c;
    size_t residual_start;
    int horizontal;
} jxl_squeeze_inverse_step;

size_t jxl_transform_squeeze_inverse_steps(jxl_allocator_state *alloc,
                                           const jxl_transform_squeeze *sq,
                                           const jxl_modular_params *params,
                                           jxl_squeeze_inverse_step *steps, size_t steps_cap);
void jxl_transform_info_free(jxl_allocator_state *alloc, jxl_transform_info *tr);

jxl_modular_status_t jxl_transform_info_parse(jxl_allocator_state *alloc, jxl_bs *bs,
                                                const jxl_wp_header *wp,
                                              jxl_transform_info *out);

jxl_modular_status_t jxl_transform_prepare_channel_info(jxl_allocator_state *alloc,
                                                      jxl_transform_info *tr,
                                                      jxl_modular_channels *channels);

int jxl_transform_is_palette(const jxl_transform_info *tr);
int jxl_transform_is_squeeze(const jxl_transform_info *tr);

jxl_modular_status_t jxl_transform_prepare_meta_channels(const jxl_transform_info *tr,
                                                         size_t *meta_palette_w,
                                                         size_t *meta_palette_h);

#endif /* JXL_MODULAR_TRANSFORM_H_ */
