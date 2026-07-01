// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_MODULAR_PREDICTOR_STATE_INLINE_H_
#define JXL_MODULAR_PREDICTOR_STATE_INLINE_H_

#include "predictor_state.h"

#include <string.h>

/* Implemented in predictor_state.c for the slow paths below. */
int32_t jxl_modular_properties_extra_prop(const jxl_modular_predictor_state *st, size_t prop_extra);
int32_t jxl_modular_ne_sample(const jxl_modular_predictor_state *st, int edge);
int32_t jxl_modular_nn_sample(const jxl_modular_predictor_state *st, int edge);
int32_t jxl_modular_ww_sample(const jxl_modular_predictor_state *st, int edge);

static const uint32_t jxl_modular_sc_div_lookup[65] = {
    0,        16777216, 8388608, 5592405, 4194304, 3355443, 2796202, 2396745, 2097152,
    1864135,  1677721,  1525201, 1398101, 1290555, 1198372, 1118481, 1048576, 986895,
    932067,   883011,   838860,  798915,  762600,  729444,  699050,  671088,  645277,
    621378,   599186,   578524,  559240,  541200,  524288,  508400,  493447,  479349,
    466033,   453438,   441505,  430185,  419430,  409200,  399457,  390167,  381300,
    372827,   364722,   356962,  349525,  342392,  335544,  328965,  322638,  316551,
    310689,   305040,   299593,  294337,  289262,  284359,  279620,  275036,  270600,
    266305,   262144,
};

JXL_ALWAYS_INLINE jxl_prediction_result jxl_modular_sc_predict(const jxl_modular_predictor_state *st,
                                                      int edge) {
    size_t i;
    uint32_t subpred_err_sum[4];
    uint32_t weight[4];
    uint32_t sum_weights;
    uint32_t log_weight;
    size_t div_idx;
    jxl_prediction_result r;
    int64_t subpred[4];
    uint32_t wp_wn[4];
    int32_t errs[3];
    int64_t true_err_w;
    int64_t true_err_nw;
    int64_t true_err_n;
    int64_t true_err_ne;
    int32_t max_error;
    const jxl_wp_header *wp = st->has_wp ? &st->wp_header : NULL;
    int64_t n3 = (int64_t)st->n << 3;
    int64_t nw3 = (int64_t)st->nw << 3;
    int64_t ne3 = (int64_t)jxl_modular_ne_sample(st, edge) << 3;
    int64_t w3 = (int64_t)st->w << 3;
    int64_t nn3 = (int64_t)jxl_modular_nn_sample(st, edge) << 3;
    uint64_t sw;
    int64_t s;
    int64_t prediction;

    true_err_w = st->true_err_w;
    true_err_nw = st->true_err_nw;
    true_err_n = st->true_err_n;
    true_err_ne = st->true_err_ne;
    subpred[0] = w3 + ne3 - n3;
    subpred[1] = n3 - (((true_err_w + true_err_n + true_err_ne) * (int64_t)wp->wp_p1) >> 5);
    subpred[2] = w3 - (((true_err_w + true_err_n + true_err_nw) * (int64_t)wp->wp_p2) >> 5);
    subpred[3] = n3 - ((true_err_nw * (int64_t)wp->wp_p3a + true_err_n * (int64_t)wp->wp_p3b +
                        true_err_ne * (int64_t)wp->wp_p3c + (nn3 - n3) * (int64_t)wp->wp_p3d +
                        (nw3 - w3) * (int64_t)wp->wp_p3e) >>
                       5);

    for (i = 0; i < 4; ++i) {
        uint32_t err_sum =
            (uint32_t)(st->subpred_err_nw_ww[i] + st->subpred_err_n_w[i] + st->subpred_err_ne[i]);
        subpred_err_sum[i] = err_sum;
    }
    wp_wn[0] = wp->wp_w0;
    wp_wn[1] = wp->wp_w1;
    wp_wn[2] = wp->wp_w2;
    wp_wn[3] = wp->wp_w3;

    for (i = 0; i < 4; ++i) {
        uint32_t err_sum = subpred_err_sum[i];
        uint32_t shift = 0;
        uint64_t v = ((uint64_t)err_sum + 1) >> 5;
        size_t idx;
        while (v > 1) {
            v >>= 1;
            ++shift;
        }
        idx = (size_t)((err_sum >> shift) + 1);
        if (idx > 64) {
            idx = 64;
        }
        weight[i] = 4 + (uint32_t)(((uint64_t)wp_wn[i] * jxl_modular_sc_div_lookup[idx]) >> shift);
    }
    sum_weights = weight[0] + weight[1] + weight[2] + weight[3];
    log_weight = 0;
    sw = (uint64_t)sum_weights >> 4;
    while (sw > 1) {
        sw >>= 1;
        ++log_weight;
    }
    for (i = 0; i < 4; ++i) {
        weight[i] >>= log_weight;
    }
    sum_weights = weight[0] + weight[1] + weight[2] + weight[3];
    s = ((int64_t)sum_weights >> 1) - 1;
    for (i = 0; i < 4; ++i) {
        s += subpred[i] * (int64_t)weight[i];
    }
    div_idx = sum_weights < 65 ? sum_weights : 64;
    prediction = (s * (int64_t)jxl_modular_sc_div_lookup[div_idx]) >> 24;
    if (((st->true_err_n ^ st->true_err_w) | (st->true_err_n ^ st->true_err_nw)) <= 0) {
        int64_t min = n3;
        int64_t max;
        if (w3 < min) {
            min = w3;
        }
        if (ne3 < min) {
            min = ne3;
        }
        max = n3;
        if (w3 > max) {
            max = w3;
        }
        if (ne3 > max) {
            max = ne3;
        }
        if (prediction < min) {
            prediction = min;
        }
        if (prediction > max) {
            prediction = max;
        }
    }
    max_error = st->true_err_w;
    errs[0] = st->true_err_n;
    errs[1] = st->true_err_nw;
    errs[2] = st->true_err_ne;

    for (i = 0; i < 3; ++i) {
        int32_t e = errs[i];
        if (e < 0) {
            e = -e;
        }
        if (e > (max_error < 0 ? -max_error : max_error)) {
            max_error = errs[i];
        }
    }
    r.prediction = prediction;
    r.max_error = max_error;
    memcpy(r.subpred, subpred, sizeof(subpred));
    return r;
}

JXL_ALWAYS_INLINE void jxl_modular_sc_record(jxl_modular_predictor_state *st,
                                      const jxl_prediction_result *pred, int32_t sample) {
    size_t i;
    uint32_t subpred_err[4];
    int64_t sample_scaled;
    int32_t true_err;
    uint32_t col;

    if (!st->has_wp || st->width == 0) {
        return;
    }
    sample_scaled = (int64_t)sample << 3;
    true_err = (int32_t)(pred->prediction - sample_scaled);
    for (i = 0; i < 4; ++i) {
        int64_t diff = pred->subpred[i] - sample_scaled;
        if (diff < 0) {
            diff = -diff;
        }
        subpred_err[i] = (uint32_t)((diff + 3) >> 3);
    }
    col = st->sc_x;
    st->true_err_row[col] = true_err;
    memcpy(&st->subpred_err_row[col * 4], subpred_err, sizeof(subpred_err));
    st->sc_x += 1;
    if (st->sc_x >= st->width) {
        st->sc_y += 1;
        st->sc_x = 0;
        st->true_err_w = 0;
        st->true_err_n = st->true_err_row[0];
        st->true_err_nw = st->true_err_n;
        memcpy(st->subpred_err_n_w, st->subpred_err_row, sizeof(st->subpred_err_n_w));
        memcpy(st->subpred_err_nw_ww, st->subpred_err_n_w, sizeof(st->subpred_err_nw_ww));
        if (st->width <= 1) {
            st->true_err_ne = st->true_err_n;
            memcpy(st->subpred_err_ne, st->subpred_err_n_w, sizeof(st->subpred_err_ne));
        } else {
            st->true_err_ne = st->true_err_row[1];
            memcpy(st->subpred_err_ne, &st->subpred_err_row[4], sizeof(st->subpred_err_ne));
        }
    } else {
        st->true_err_w = true_err;
        st->true_err_nw = st->true_err_n;
        st->true_err_n = st->true_err_ne;
        memcpy(st->subpred_err_nw_ww, st->subpred_err_n_w, sizeof(st->subpred_err_nw_ww));
        memcpy(st->subpred_err_n_w, st->subpred_err_ne, sizeof(st->subpred_err_n_w));
        for (i = 0; i < 4; ++i) {
            st->subpred_err_n_w[i] += subpred_err[i];
        }
        if (st->sc_x + 1 >= st->width) {
            st->true_err_ne = st->true_err_n;
            memcpy(st->subpred_err_ne, st->subpred_err_n_w, sizeof(st->subpred_err_ne));
        } else if (st->sc_y != 0) {
            uint32_t ne_x = st->sc_x + 1;
            st->true_err_ne = st->true_err_row[ne_x];
            memcpy(st->subpred_err_ne, &st->subpred_err_row[ne_x * 4], sizeof(st->subpred_err_ne));
        }
    }
}

JXL_ALWAYS_INLINE int32_t jxl_modular_property_from_state(const jxl_modular_predictor_state *st,
                                                   int edge, size_t property,
                                                   jxl_modular_properties *props) {
    int32_t w_nw;

    if (property >= 16) {
        return jxl_modular_properties_extra_prop(st, property - 16);
    }
    switch (property) {
    case 0:
    case 1:
        return 0;
    case 2:
        return (int32_t)st->y;
    case 3:
        return (int32_t)st->x;
    case 4:
        return st->n < 0 ? -st->n : st->n;
    case 5:
        return st->w < 0 ? -st->w : st->w;
    case 6:
        return st->n;
    case 7:
        return st->w;
    case 8:
        return st->w - st->prev_grad;
    case 9:
        w_nw = st->w - st->nw;
        return w_nw + st->n;
    case 10:
        return st->w - st->nw;
    case 11:
        return st->nw - st->n;
    case 12:
        if (!edge && st->prev_row != NULL && st->prev_row_len > 0) {
            return st->n - st->prev_row[st->x + 1];
        }
        return st->n - jxl_modular_ne_sample(st, edge);
    case 13:
        if (!edge && st->curr_row != NULL) {
            return st->n - st->curr_row[st->x];
        }
        return st->n - jxl_modular_nn_sample(st, edge);
    case 14:
        if (!edge && st->x >= 2 && st->curr_row != NULL) {
            return st->w - st->curr_row[st->x - 2];
        }
        return st->w - jxl_modular_ww_sample(st, edge);
    case 15:
        if (props != NULL && props->has_sc_prediction) {
            return props->sc_prediction.max_error;
        }
        return 0;
    default:
        return 0;
    }
}

jxl_inline int32_t jxl_modular_properties_get_fast(const jxl_modular_properties *props,
                                                   size_t property) {
    if (props == NULL) {
        return 0;
    }
    if (props->cache_filled && property < 16) {
        return props->prop_cache[property];
    }
    return jxl_modular_property_from_state(props->state, props->is_edge, property,
                                           (jxl_modular_properties *)props);
}

JXL_ALWAYS_INLINE void jxl_modular_properties_begin(jxl_modular_predictor_state *st,
                                             jxl_modular_properties *out, int edge, int need_sc) {
    out->state = st;
    out->is_edge = edge;
    out->has_sc_prediction = 0;
    out->cache_filled = 0;
    out->sc_lazy = 0;
    if (need_sc && st->has_wp) {
        out->sc_prediction = jxl_modular_sc_predict(st, edge);
        out->has_sc_prediction = 1;
    }
}

jxl_inline void jxl_modular_properties_fill(jxl_modular_predictor_state *st,
                                            jxl_modular_properties *out, int edge, int need_sc) {
    int32_t w_nw;

    out->state = st;
    out->is_edge = edge;
    out->has_sc_prediction = 0;
    out->sc_lazy = 0;
    out->cache_filled = 1;
    if (need_sc && st->has_wp) {
        out->sc_prediction = jxl_modular_sc_predict(st, edge);
        out->has_sc_prediction = 1;
    }

    w_nw = st->w - st->nw;
    out->prop_cache[0] = 0;
    out->prop_cache[1] = 0;
    out->prop_cache[2] = (int32_t)st->y;
    out->prop_cache[3] = (int32_t)st->x;
    out->prop_cache[4] = st->n < 0 ? -st->n : st->n;
    out->prop_cache[5] = st->w < 0 ? -st->w : st->w;
    out->prop_cache[6] = st->n;
    out->prop_cache[7] = st->w;
    out->prop_cache[8] = st->w - st->prev_grad;
    out->prop_cache[9] = w_nw + st->n;
    out->prop_cache[10] = w_nw;
    out->prop_cache[11] = st->nw - st->n;
    out->prop_cache[12] = st->n - jxl_modular_ne_sample(st, edge);
    out->prop_cache[13] = st->n - jxl_modular_nn_sample(st, edge);
    out->prop_cache[14] = st->w - jxl_modular_ww_sample(st, edge);
    out->prop_cache[15] = out->has_sc_prediction ? out->sc_prediction.max_error : 0;
}

jxl_inline int32_t jxl_modular_nee_sample(const jxl_modular_predictor_state *st, int edge) {
    if (st->prev_row == NULL || st->prev_row_len == 0) {
        return jxl_modular_ne_sample(st, edge);
    }
    if (!edge) {
        if (st->x + 2 >= st->prev_row_len) {
            return st->prev_row[st->prev_row_len - 1];
        }
        return st->prev_row[st->x + 2];
    }
    if (st->x + 2 >= st->width) {
        return jxl_modular_ne_sample(st, 1);
    }
    if (st->x + 2 >= st->prev_row_len) {
        return st->n;
    }
    return st->prev_row[st->x + 2];
}

JXL_ALWAYS_INLINE int32_t jxl_modular_predict(jxl_predictor pred, const jxl_modular_properties *props,
                                       int edge) {
    const jxl_modular_predictor_state *st = props->state;
    switch (pred) {
    case JXL_PREDICTOR_ZERO:
        return 0;
    case JXL_PREDICTOR_WEST:
        return st->w;
    case JXL_PREDICTOR_NORTH:
        return st->n;
    case JXL_PREDICTOR_AVG_WEST_AND_NORTH:
        return (int32_t)(((int64_t)st->w + st->n) / 2);
    case JXL_PREDICTOR_SELECT: {
        int64_t n = st->n;
        int64_t w = st->w;
        int64_t nw = st->nw;
        int64_t dn = n - nw;
        int64_t dw = w - nw;
        if (dn < 0) {
            dn = -dn;
        }
        if (dw < 0) {
            dw = -dw;
        }
        return dn < dw ? st->w : st->n;
    }
    case JXL_PREDICTOR_GRADIENT: {
        int64_t n = st->n;
        int64_t w = st->w;
        int64_t nw = st->nw;
        int64_t g = n + w - nw;
        int64_t lo = w < n ? w : n;
        int64_t hi = w > n ? w : n;
        if (g < lo) {
            return (int32_t)lo;
        }
        if (g > hi) {
            return (int32_t)hi;
        }
        return (int32_t)g;
    }
    case JXL_PREDICTOR_SELF_CORRECTING:
        return props->has_sc_prediction ? (int32_t)((props->sc_prediction.prediction + 3) >> 3) : 0;
    case JXL_PREDICTOR_NORTH_EAST:
        return jxl_modular_ne_sample(st, edge);
    case JXL_PREDICTOR_NORTH_WEST:
        return st->nw;
    case JXL_PREDICTOR_WEST_WEST:
        return jxl_modular_ww_sample(st, edge);
    case JXL_PREDICTOR_AVG_WEST_AND_NORTH_WEST:
        return (int32_t)(((int64_t)st->w + st->nw) / 2);
    case JXL_PREDICTOR_AVG_NORTH_AND_NORTH_WEST:
        return (int32_t)(((int64_t)st->n + st->nw) / 2);
    case JXL_PREDICTOR_AVG_NORTH_AND_NORTH_EAST:
        return (int32_t)(((int64_t)st->n + jxl_modular_ne_sample(st, edge)) / 2);
    case JXL_PREDICTOR_AVG_ALL: {
        int64_t n = st->n;
        int64_t w = st->w;
        int64_t nn = jxl_modular_nn_sample(st, edge);
        int64_t ww = jxl_modular_ww_sample(st, edge);
        int64_t nee = jxl_modular_nee_sample(st, edge);
        int64_t ne = jxl_modular_ne_sample(st, edge);
        return (int32_t)((6 * n - 2 * nn + 7 * w + ww + nee + 3 * ne + 8) / 16);
    }
    default:
        return 0;
    }
}

JXL_ALWAYS_INLINE void jxl_modular_predictor_state_advance(jxl_modular_predictor_state *st,
                                                    int32_t sample) {
    if (st->x < st->width) {
        st->curr_row[st->x] = sample;
        if (st->x + 1 > st->curr_row_len) {
            st->curr_row_len = st->x + 1;
        }
    }
    st->x += 1;
    if (st->x >= st->width) {
        int32_t *tmp;
        uint32_t tmp_len;

        st->y += 1;
        st->x = 0;
        tmp = st->prev_row;
        st->prev_row = st->curr_row;
        st->curr_row = tmp;
        tmp_len = st->prev_row_len;
        st->prev_row_len = st->curr_row_len;
        st->curr_row_len = tmp_len;
        st->prev_grad = 0;
        if (st->prev_row_len > 0) {
            st->n = st->prev_row[0];
            st->w = st->n;
            st->nw = st->n;
        }
    } else {
        st->prev_grad = (st->w - st->nw) + st->n;
        st->w = sample;
        if (st->prev_row_len == 0) {
            st->nw = sample;
            st->n = sample;
        } else {
            st->nw = st->n;
            st->n = st->prev_row[st->x];
        }
    }
}

jxl_inline void jxl_modular_properties_record(jxl_modular_properties *props, int32_t sample) {
    jxl_modular_predictor_state *st = props->state;

    if (props->has_sc_prediction) {
        jxl_modular_sc_record(st, &props->sc_prediction, sample);
    }
    jxl_modular_predictor_state_advance(st, sample);
}

#endif /* JXL_MODULAR_PREDICTOR_STATE_INLINE_H_ */
