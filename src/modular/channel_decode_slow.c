// SPDX-License-Identifier: MIT OR Apache-2.0
#include "channel_decode_internal.h"

#include "coding/cdecoder_hoisted_inline.h"
#include "coding/cdecoder_modular_internal.h"
#include "modular/ma_flat.h"
#include "modular/predictor_state.h"
#include "modular/sample.h"
#include "modular/util.h"

typedef struct {
    jxl_modular_sample_kind kind;
} jxl_dec_sample;

static inline jxl_dec_sample jxl_modular_dec_sample_for(const jxl_modular_grid_i32 *grid) {
    jxl_dec_sample ds;
    ds.kind = grid != NULL ? grid->kind : JXL_MODULAR_SAMPLE_I32;
    return ds;
}

static inline int32_t jxl_modular_dec_unpacked(const jxl_dec_sample *ds, uint32_t token) {
    if (ds->kind == JXL_MODULAR_SAMPLE_I16) {
        return (int32_t)jxl_modular_unpack_signed_u32_i16(token);
    }
    return jxl_modular_unpack_signed_u32(token);
}

static inline int32_t jxl_modular_dec_add(const jxl_dec_sample *ds, int32_t a, int32_t b) {
    if (ds->kind == JXL_MODULAR_SAMPLE_I16) {
        return (int32_t)jxl_modular_i16_add((int16_t)a, (int16_t)b);
    }
    return jxl_modular_i32_add(a, b);
}

static inline int32_t jxl_modular_dec_wrapping_muladd(const jxl_dec_sample *ds, int32_t v,
                                                        int32_t mul, int32_t off) {
    if (ds->kind == JXL_MODULAR_SAMPLE_I16) {
        return (int32_t)jxl_modular_i16_wrapping_muladd((int16_t)v, mul, off);
    }
    return jxl_modular_i32_wrapping_muladd(v, mul, off);
}

static inline void *jxl_modular_grid_row_mut_any(jxl_modular_grid_i32 *grid, size_t y) {
    if (grid->kind == JXL_MODULAR_SAMPLE_I16) {
        return jxl_modular_grid_row_i16(grid, y);
    }
    return jxl_modular_grid_row_i32(grid, y);
}

static inline void jxl_modular_dec_row_set(const jxl_dec_sample *ds, void *row, size_t x,
                                           int32_t v) {
    if (ds->kind == JXL_MODULAR_SAMPLE_I16) {
        ((int16_t *)row)[x] = (int16_t)v;
    } else {
        ((int32_t *)row)[x] = v;
    }
}

#define JXL_DECODE_SLOW_PIXEL(edge_val)                                                     \
    do {                                                                                    \
        jxl_modular_properties props;                                                       \
        const jxl_ma_tree_leaf_clustered *leaf;                                             \
        uint32_t token = 0;                                                                 \
        int32_t sample;                                                                     \
        int32_t diff;                                                                       \
        int32_t pred_val;                                                                   \
        jxl_modular_properties_begin(pred, &props, (edge_val), need_sc);                    \
        JXL_MA_FLAT_GET_LEAF(tree, &props, leaf);                                           \
        if (leaf == NULL) {                                                                 \
            return JXL_MODULAR_DECODER_ERROR;                                               \
        }                                                                                   \
        JXL_MODULAR_TRY(jxl_modular_channel_read_token(rle, decoder, bs, leaf->cluster,      \
                                                      dist_multiplier, &token));             \
        diff = jxl_modular_dec_unpacked(&ds, token);                                        \
        diff = jxl_modular_dec_wrapping_muladd(&ds, diff, (int32_t)leaf->multiplier,        \
                                               leaf->offset);                               \
        pred_val = jxl_modular_predict(leaf->predictor, &props, props.is_edge);              \
        sample = jxl_modular_dec_add(&ds, diff, pred_val);                                   \
        jxl_modular_dec_row_set(&ds, row, x, sample);                                       \
        if (props.has_sc_prediction) {                                                      \
            jxl_modular_sc_record(pred, &props.sc_prediction, sample);                     \
        }                                                                                   \
        jxl_modular_predictor_state_advance(pred, sample);                                 \
    } while (0)

#define JXL_DECODE_SLOW_PIXEL_HOIST(hoist_slot)                                            \
    do {                                                                                    \
        jxl_modular_properties props;                                                       \
        const jxl_ma_tree_leaf_clustered *leaf;                                             \
        uint32_t token = 0;                                                                 \
        int32_t sample;                                                                     \
        int32_t diff;                                                                       \
        int32_t pred_val;                                                                   \
        jxl_modular_properties_begin(pred, &props, 0, need_sc);                             \
        JXL_MA_FLAT_GET_LEAF(tree, &props, leaf);                                           \
        if (leaf == NULL) {                                                                 \
            return JXL_MODULAR_DECODER_ERROR;                                               \
        }                                                                                   \
        JXL_HOIST_READ_VARINT_LZ77(decoder, bs, (hoist_slot), leaf->cluster, dist_multiplier, \
                                   token, return JXL_MODULAR_DECODER_ERROR);                 \
        diff = jxl_modular_dec_unpacked(&ds, token);                                        \
        diff = jxl_modular_dec_wrapping_muladd(&ds, diff, (int32_t)leaf->multiplier,        \
                                               leaf->offset);                               \
        pred_val = jxl_modular_predict(leaf->predictor, &props, props.is_edge);              \
        sample = jxl_modular_dec_add(&ds, diff, pred_val);                                   \
        jxl_modular_dec_row_set(&ds, row, x, sample);                                       \
        if (props.has_sc_prediction) {                                                      \
            jxl_modular_sc_record(pred, &props.sc_prediction, sample);                     \
        }                                                                                   \
        jxl_modular_predictor_state_advance(pred, sample);                                 \
    } while (0)

JXL_ATTRIBUTE_HOT jxl_modular_status_t jxl_modular_channel_decode_slow(
    jxl_context *ctx, jxl_bs *bs, jxl_coding_decoder *decoder, uint32_t dist_multiplier,
    const jxl_ma_flat_tree *tree, jxl_modular_predictor_state *pred, jxl_modular_grid_i32 *grid,
    jxl_modular_rle_state *rle) {
    size_t y;
    int need_sc = tree != NULL && tree->need_self_correcting;
    jxl_dec_sample ds = jxl_modular_dec_sample_for(grid);
    size_t height = grid->height;
    size_t width = grid->width;
    (void)ctx;
    for (y = 0; y < height && y < 2; ++y) {
        size_t x;
        void *row = jxl_modular_grid_row_mut_any(grid, y);
        if (row == NULL) {
            return JXL_MODULAR_DECODER_ERROR;
        }
        for (x = 0; x < width; ++x) {
            JXL_DECODE_SLOW_PIXEL(1);
        }
    }
    for (y = 2; y < height; ++y) {
        size_t x;
        void *row = jxl_modular_grid_row_mut_any(grid, y);
        jxl_coding_hoist_slot hoist;
        int use_hoist;
        if (row == NULL) {
            return JXL_MODULAR_DECODER_ERROR;
        }
        size_t left_end = width < 2 ? width : 2;
        size_t right_start = width < 4 ? width : width - 2;
        for (x = 0; x < left_end; ++x) {
            JXL_DECODE_SLOW_PIXEL(1);
        }
        use_hoist = 0;
        if (rle == NULL && left_end < right_start &&
            jxl_coding_decoder_hoist_begin(decoder, &hoist)) {
            use_hoist = 1;
        }
        if (use_hoist) {
            for (x = left_end; x < right_start; ++x) {
                JXL_DECODE_SLOW_PIXEL_HOIST(&hoist);
            }
            jxl_coding_decoder_hoist_commit(decoder, &hoist);
        } else {
            for (x = left_end; x < right_start; ++x) {
                JXL_DECODE_SLOW_PIXEL(0);
            }
        }
        for (x = right_start; x < width; ++x) {
            JXL_DECODE_SLOW_PIXEL(1);
        }
    }
    return JXL_MODULAR_OK;
}
