// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_MODULAR_PREDICTOR_STATE_H_
#define JXL_MODULAR_PREDICTOR_STATE_H_

#include "allocator.h"
#include "modular/image.h"
#include "modular/predictor.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct {
    int64_t prediction;
    int32_t max_error;
    int64_t subpred[4];
} jxl_prediction_result;

typedef struct jxl_modular_properties jxl_modular_properties;

struct jxl_modular_properties {
    struct jxl_modular_predictor_state *state;
    jxl_prediction_result sc_prediction;
    int has_sc_prediction;
    int sc_lazy;
    int cache_filled;
    int is_edge;
    int32_t prop_cache[16];
};

typedef struct jxl_modular_predictor_state {
    uint32_t width;
    int32_t *prev_row;
    int32_t *curr_row;
    uint32_t prev_row_len;
    uint32_t curr_row_len;
    size_t row_cap;
    const jxl_modular_grid_i32 *const *prev_channels;
    size_t prev_channel_count;
    jxl_wp_header wp_header;
    int has_wp;
    int32_t *true_err_row;
    uint32_t *subpred_err_row;
    uint32_t sc_x;
    uint32_t sc_y;
    int32_t true_err_w;
    int32_t true_err_nw;
    int32_t true_err_n;
    int32_t true_err_ne;
    uint32_t subpred_err_nw_ww[4];
    uint32_t subpred_err_n_w[4];
    uint32_t subpred_err_ne[4];
    uint32_t y;
    uint32_t x;
    int32_t w;
    int32_t n;
    int32_t nw;
    int32_t prev_grad;
} jxl_modular_predictor_state;

void jxl_modular_predictor_state_init(jxl_modular_predictor_state *st);
void jxl_modular_predictor_state_free(jxl_allocator_state *alloc, jxl_modular_predictor_state *st);

void jxl_modular_predictor_state_reset(jxl_allocator_state *alloc, jxl_modular_predictor_state *st,
                                       uint32_t width,
                                       const jxl_modular_grid_i32 *const *prev_channels,
                                       size_t prev_channel_count, const jxl_wp_header *wp);

void jxl_modular_properties_edge(jxl_modular_predictor_state *st, jxl_modular_properties *out);
void jxl_modular_properties_interior(jxl_modular_predictor_state *st, jxl_modular_properties *out);

int32_t jxl_modular_properties_get(const jxl_modular_properties *props, size_t property);

#include "predictor_state_inline.h"

#endif /* JXL_MODULAR_PREDICTOR_STATE_H_ */
