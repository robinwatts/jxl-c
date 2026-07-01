// SPDX-License-Identifier: MIT OR Apache-2.0
#include "predictor_state.h"

#include "allocator.h"
#include "modular/sample.h"
#include <string.h>

static int32_t grid_get(const jxl_modular_grid_i32 *g, size_t x, size_t y) {
    return jxl_modular_grid_sample_as_i32(g, x, y);
}

int32_t jxl_modular_properties_extra_prop(const jxl_modular_predictor_state *st, size_t prop_extra) {
    size_t prev_channel_idx = prop_extra / 4;
    size_t prop_idx = prop_extra % 4;
    int32_t g;
    size_t x;
    size_t y;
    const jxl_modular_grid_i32 *ch;
    int32_t c;
    if (prev_channel_idx >= st->prev_channel_count) {
        return 0;
    }
    ch = st->prev_channels[prev_channel_idx];
    x = st->x;
    y = st->y;
    c = grid_get(ch, x, y);
    if (prop_idx == 0) {
        return c < 0 ? -c : c;
    }
    if (prop_idx == 1) {
        return c;
    }
    g = 0;
    if (x == 0 && y == 0) {
        g = 0;
    } else if (x == 0) {
        g = grid_get(ch, 0, y - 1);
    } else if (y == 0) {
        g = grid_get(ch, x - 1, 0);
    } else {
        int32_t nw = grid_get(ch, x - 1, y - 1);
        int32_t n = grid_get(ch, x, y - 1);
        int32_t w = grid_get(ch, x - 1, y);
        g = jxl_modular_i32_grad_clamped(n, w, nw);
    }
    return prop_idx == 2 ? (c < g ? g - c : c - g) : c - g;
}

void jxl_modular_predictor_state_init(jxl_modular_predictor_state *st) {
    if (st != NULL) {
        memset(st, 0, sizeof(*st));
    }
}

void jxl_modular_predictor_state_free(jxl_allocator_state *alloc, jxl_modular_predictor_state *st) {
    if (st == NULL) {
        return;
    }
    jxl_free(alloc, st->prev_row);
    jxl_free(alloc, st->curr_row);
    jxl_free(alloc, st->true_err_row);
    jxl_free(alloc, st->subpred_err_row);
    jxl_modular_predictor_state_init(st);
}

void jxl_modular_predictor_state_reset(jxl_allocator_state *alloc, jxl_modular_predictor_state *st,
                                       uint32_t width,
                                       const jxl_modular_grid_i32 *const *prev_channels,
                                       size_t prev_channel_count, const jxl_wp_header *wp) {
    if (st == NULL) {
        return;
    }
    st->width = width;
    st->prev_channels = prev_channels;
    st->prev_channel_count = prev_channel_count;
    st->has_wp = wp != NULL;
    if (wp != NULL) {
        st->wp_header = *wp;
        st->has_wp = 1;
    }
    if (st->row_cap < width) {
        size_t cap = width > 64 ? width : 64;
        int32_t *pr = jxl_realloc(alloc, st->prev_row, cap * sizeof(int32_t));
        int32_t *cr = jxl_realloc(alloc, st->curr_row, cap * sizeof(int32_t));
        int32_t *te = jxl_realloc(alloc, st->true_err_row, cap * sizeof(int32_t));
        uint32_t *se = jxl_realloc(alloc, st->subpred_err_row, cap * 4 * sizeof(uint32_t));
        if (pr == NULL || cr == NULL || te == NULL || se == NULL) {
            return;
        }
        st->prev_row = pr;
        st->curr_row = cr;
        st->true_err_row = te;
        st->subpred_err_row = se;
        st->row_cap = cap;
    }
    memset(st->prev_row, 0, width * sizeof(int32_t));
    memset(st->curr_row, 0, width * sizeof(int32_t));
    if (st->has_wp && width > 0) {
        memset(st->true_err_row, 0, width * sizeof(int32_t));
        memset(st->subpred_err_row, 0, width * 4 * sizeof(uint32_t));
    }
    st->y = 0;
    st->x = 0;
    st->prev_row_len = 0;
    st->curr_row_len = 0;
    st->w = 0;
    st->n = 0;
    st->nw = 0;
    st->prev_grad = 0;
    st->sc_x = 0;
    st->sc_y = 0;
    st->true_err_w = 0;
    st->true_err_nw = 0;
    st->true_err_n = 0;
    st->true_err_ne = 0;
    memset(st->subpred_err_nw_ww, 0, sizeof(st->subpred_err_nw_ww));
    memset(st->subpred_err_n_w, 0, sizeof(st->subpred_err_n_w));
    memset(st->subpred_err_ne, 0, sizeof(st->subpred_err_ne));
}

static int prev_row_empty(const jxl_modular_predictor_state *st) {
    return st->prev_row_len == 0;
}

int32_t jxl_modular_ne_sample(const jxl_modular_predictor_state *st, int edge) {
    if (st->prev_row == NULL || prev_row_empty(st)) {
        return st->n;
    }
    if (!edge) {
        return st->prev_row[st->x + 1];
    }
    if (st->x + 1 >= st->width || st->x + 1 >= st->prev_row_len) {
        return st->n;
    }
    return st->prev_row[st->x + 1];
}


int32_t jxl_modular_nn_sample(const jxl_modular_predictor_state *st, int edge) {
    if (st->curr_row == NULL) {
        return st->n;
    }
    if (!edge) {
        return st->curr_row[st->x];
    }
    if (st->x >= st->curr_row_len) {
        return st->n;
    }
    return st->curr_row[st->x];
}

int32_t jxl_modular_ww_sample(const jxl_modular_predictor_state *st, int edge) {
    if (st->x < 2 || st->curr_row == NULL) {
        return st->w;
    }
    if (!edge) {
        return st->curr_row[st->x - 2];
    }
    if ((st->x - 2) >= st->curr_row_len) {
        return st->w;
    }
    return st->curr_row[st->x - 2];
}

static void fill_properties(jxl_modular_predictor_state *st, jxl_modular_properties *out, int edge) {
    jxl_modular_properties_fill(st, out, edge, st->has_wp);
}

void jxl_modular_properties_edge(jxl_modular_predictor_state *st, jxl_modular_properties *out) {
    fill_properties(st, out, 1);
}

void jxl_modular_properties_interior(jxl_modular_predictor_state *st, jxl_modular_properties *out) {
    fill_properties(st, out, 0);
}

int32_t jxl_modular_properties_get(const jxl_modular_properties *props, size_t property) {
    if (props == NULL) {
        return 0;
    }
    return jxl_modular_properties_get_fast(props, property);
}

