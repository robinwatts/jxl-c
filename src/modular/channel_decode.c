// SPDX-License-Identifier: MIT OR Apache-2.0
#include "channel_decode.h"

#include "channel_decode_internal.h"
#include "modular/predictor_state.h"
#include "modular/sample.h"
#include "modular/util.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef NDEBUG
static unsigned g_pixel_ch = UINT_MAX;

static unsigned g_tok_pg;
static unsigned g_tok_pass;
static unsigned g_tok_dest_ch;
static unsigned g_tok_idx;
static unsigned g_tok_limit = 64;
static unsigned g_tok_filter_pass = UINT_MAX;
static unsigned g_tok_filter_pg = UINT_MAX;

static void debug_tok_init_filter(const jxl_context *ctx) {
    static int inited = 0;
    if (inited) {
        return;
    }
    inited = 1;
    if (!JXL_DEBUG_FLAG(ctx, debug_tokens)) {
        return;
    }
    if (JXL_DEBUG_FLAG(ctx, debug_tokens_pg_active)) {
        g_tok_filter_pass = JXL_DEBUG_FLAG(ctx, debug_tokens_pg_pass);
        g_tok_filter_pg = JXL_DEBUG_FLAG(ctx, debug_tokens_pg_group);
    }
    g_tok_limit = JXL_DEBUG_FLAG(ctx, debug_tokens_limit);
}

static int debug_tok_pg_matches(const jxl_context *ctx) {
    debug_tok_init_filter(ctx);
    if (g_tok_filter_pass == UINT_MAX) {
        return 1;
    }
    return g_tok_pass == g_tok_filter_pass && g_tok_pg == g_tok_filter_pg;
}

void (jxl_modular_debug_tokens_set_pg)(jxl_context *ctx, unsigned pass_idx, unsigned group_idx) {
    if (!JXL_DEBUG_FLAG(ctx, debug_tokens)) {
        return;
    }
    g_tok_pass = pass_idx;
    g_tok_pg = group_idx;
    g_tok_filter_pass = UINT_MAX;
    g_tok_filter_pg = UINT_MAX;
    if (JXL_DEBUG_FLAG(ctx, debug_tokens_pg_active)) {
        g_tok_filter_pass = JXL_DEBUG_FLAG(ctx, debug_tokens_pg_pass);
        g_tok_filter_pg = JXL_DEBUG_FLAG(ctx, debug_tokens_pg_group);
    }
    g_tok_limit = JXL_DEBUG_FLAG(ctx, debug_tokens_limit);
}

void (jxl_modular_debug_pixel_set_channel)(jxl_context *ctx, size_t dest_channel) {
    if (!JXL_DEBUG_FLAG(ctx, debug_pixel)) {
        return;
    }
    g_pixel_ch = (unsigned)dest_channel;
}

void (jxl_modular_debug_tokens_set_channel)(jxl_context *ctx, size_t dest_channel) {
    if (!JXL_DEBUG_FLAG(ctx, debug_tokens)) {
        return;
    }
    g_tok_dest_ch = (unsigned)dest_channel;
    g_tok_idx = 0;
}


static void debug_tok_emit(jxl_coding_decoder *decoder, jxl_bs *bs, uint8_t cluster, const char *kind,
                           uint32_t raw, uint32_t extra) {
    jxl_context *ctx = jxl_coding_decoder_context(decoder);
    if (!JXL_DEBUG_FLAG(ctx, debug_tokens)) {
        return;
    }
    if (!debug_tok_pg_matches(ctx)) {
        return;
    }
    if (g_tok_idx >= g_tok_limit) {
        return;
    }
    fprintf(stderr, "tok pass=%u pg=%u ch=%u #%u %s cluster=%u raw=%u", g_tok_pass, g_tok_pg,
            g_tok_dest_ch, g_tok_idx, kind, (unsigned)cluster, raw);
    if (extra != 0) {
        fprintf(stderr, " extra=%u", extra);
    }
    if (bs != NULL) {
        fprintf(stderr, " bits=%zu", bs->num_read_bits);
    }
    fputc('\n', stderr);
    g_tok_idx += 1;
}
#endif

static int32_t *grid_row_mut_i32(jxl_modular_grid_i32 *grid, size_t y) {
    return jxl_modular_grid_row_i32(grid, y);
}

static int16_t *grid_row_mut_i16(jxl_modular_grid_i32 *grid, size_t y) {
    return jxl_modular_grid_row_i16(grid, y);
}

typedef struct {
    jxl_modular_sample_kind kind;
} jxl_dec_sample;

static jxl_dec_sample dec_sample_for(const jxl_modular_grid_i32 *grid) {
    jxl_dec_sample ds;
    ds.kind = grid != NULL ? grid->kind : JXL_MODULAR_SAMPLE_I32;
    return ds;
}

static int32_t dec_unpacked(const jxl_dec_sample *ds, uint32_t token) {
    if (ds->kind == JXL_MODULAR_SAMPLE_I16) {
        return (int32_t)jxl_modular_unpack_signed_u32_i16(token);
    }
    return jxl_modular_unpack_signed_u32(token);
}

static int32_t dec_add(const jxl_dec_sample *ds, int32_t a, int32_t b) {
    if (ds->kind == JXL_MODULAR_SAMPLE_I16) {
        return (int32_t)jxl_modular_i16_add((int16_t)a, (int16_t)b);
    }
    return jxl_modular_i32_add(a, b);
}

static int32_t dec_wrapping_muladd(const jxl_dec_sample *ds, int32_t v, int32_t mul, int32_t off) {
    if (ds->kind == JXL_MODULAR_SAMPLE_I16) {
        return (int32_t)jxl_modular_i16_wrapping_muladd((int16_t)v, mul, off);
    }
    return jxl_modular_i32_wrapping_muladd(v, mul, off);
}

static int32_t dec_grad_clamped(const jxl_dec_sample *ds, int32_t n, int32_t w, int32_t nw) {
    if (ds->kind == JXL_MODULAR_SAMPLE_I16) {
        return (int32_t)jxl_modular_i16_grad_clamped((int16_t)n, (int16_t)w, (int16_t)nw);
    }
    return jxl_modular_i32_grad_clamped(n, w, nw);
}

static void *grid_row_mut_any(jxl_modular_grid_i32 *grid, size_t y) {
    if (grid->kind == JXL_MODULAR_SAMPLE_I16) {
        return grid_row_mut_i16(grid, y);
    }
    return grid_row_mut_i32(grid, y);
}

static int32_t row_get(const jxl_dec_sample *ds, void *row, size_t x) {
    if (ds->kind == JXL_MODULAR_SAMPLE_I16) {
        return (int32_t)((int16_t *)row)[x];
    }
    return ((int32_t *)row)[x];
}

static void row_set(const jxl_dec_sample *ds, void *row, size_t x, int32_t v) {
    if (ds->kind == JXL_MODULAR_SAMPLE_I16) {
        ((int16_t *)row)[x] = (int16_t)v;
    } else {
        ((int32_t *)row)[x] = v;
    }
}

void jxl_modular_batch_rle_begin(jxl_modular_batch_rle *batch, jxl_coding_decoder *decoder) {
    if (batch == NULL || decoder == NULL || !jxl_coding_decoder_is_rle_mode(decoder)) {
        return;
    }
    memset(&batch->state, 0, sizeof(batch->state));
    if (jxl_coding_decoder_prepare_fast_rle(decoder) != JXL_CODING_OK) {
        batch->state.error = JXL_CODING_BITSTREAM_ERROR;
    }
}

jxl_coding_status_t jxl_modular_batch_rle_error(const jxl_modular_batch_rle *batch) {
    if (batch == NULL) {
        return JXL_CODING_OK;
    }
    return batch->state.error;
}

static inline int16_t dec_i16_unpack(uint32_t token) {
    uint16_t bit = (uint16_t)(token & 1u);
    uint16_t base = (uint16_t)(token >> 1);
    return (int16_t)(base ^ (uint16_t)(0u - bit));
}

static jxl_modular_status_t rle_next_raw(jxl_modular_rle_state *st, jxl_coding_decoder *decoder,
                                          jxl_bs *bs, uint8_t cluster, uint32_t *out) {
    if (st->repeat == 0) {
        jxl_coding_rle_token tok;
        if (jxl_coding_decoder_read_rle_token(decoder, bs, cluster, &tok) != JXL_CODING_OK) {
            return JXL_MODULAR_DECODER_ERROR;
        }
        if (tok.kind == JXL_CODING_RLE_VALUE) {
            st->last_raw = tok.value;
            st->last_i16 = dec_i16_unpack(tok.value);
            st->repeat = 1;
        } else {
            st->repeat = tok.repeat;
        }
    }
    if (st->repeat > 0) {
        st->repeat -= 1;
    }
    *out = st->last_raw;
    return JXL_MODULAR_OK;
}

static jxl_modular_status_t read_token_rle(jxl_modular_rle_state *st, jxl_coding_decoder *decoder,
                                           jxl_bs *bs, uint8_t cluster, uint32_t *out) {
    jxl_modular_status_t st_code = rle_next_raw(st, decoder, bs, cluster, out);
    if (st_code != JXL_MODULAR_OK) {
        return st_code;
    }
#ifndef NDEBUG
    debug_tok_emit(decoder, bs, cluster, "RleEmit", *out, st->repeat);
#endif
    return JXL_MODULAR_OK;
}

jxl_modular_status_t jxl_modular_channel_read_token(jxl_modular_rle_state *rle,
                                                    jxl_coding_decoder *decoder, jxl_bs *bs,
                                                    uint8_t cluster, uint32_t dist_multiplier,
                                                    uint32_t *out) {
    if (decoder == NULL || out == NULL) {
        return JXL_MODULAR_DECODER_ERROR;
    }
    if (rle != NULL) {
        (void)dist_multiplier;
        return read_token_rle(rle, decoder, bs, cluster, out);
    }
    if (jxl_coding_decoder_read_varint_clustered(decoder, bs, cluster, dist_multiplier, out) !=
        JXL_CODING_OK) {
        return JXL_MODULAR_DECODER_ERROR;
    }
#ifndef NDEBUG
    debug_tok_emit(decoder, bs, cluster, "LzVarint", *out, 0);
#endif
    return JXL_MODULAR_OK;
}

#define read_token jxl_modular_channel_read_token

static inline int16_t dec_i16_add(int16_t a, int16_t b) {
    return (int16_t)((uint16_t)a + (uint16_t)b);
}

static inline int16_t dec_i16_grad_clamped(int16_t n, int16_t w, int16_t nw) {
    int32_t g = (int32_t)n + (int32_t)w - (int32_t)nw;
    int16_t lo = w < n ? w : n;
    int16_t hi = w > n ? w : n;
    if (g < lo) {
        return lo;
    }
    if (g > hi) {
        return hi;
    }
    return (int16_t)g;
}

static jxl_modular_status_t decode_fast_lossless_grad_i16_rle_slow(jxl_bs *bs,
                                                                 jxl_coding_decoder *decoder,
                                                                 uint8_t cluster,
                                                                 jxl_modular_grid_i32 *grid,
                                                                 jxl_modular_rle_state *rle) {
    size_t width = grid->width;
    size_t height = grid->height;
    size_t y;

    {
        size_t x;
        int16_t w = 0;
        int16_t *row0 = jxl_modular_grid_row_i16(grid, 0);
        if (row0 == NULL) {
            return JXL_MODULAR_DECODER_ERROR;
        }
        for (x = 0; x < width; ++x) {
            uint32_t raw;
            JXL_MODULAR_TRY(rle_next_raw(rle, decoder, bs, cluster, &raw));
            w = dec_i16_add(w, dec_i16_unpack(raw));
            row0[x] = w;
        }
    }

    for (y = 1; y < height; ++y) {
        size_t x;
        uint32_t raw;
        int16_t *row = jxl_modular_grid_row_i16(grid, y);
        const int16_t *prev = jxl_modular_grid_row_i16_const(grid, y - 1);
        int16_t w;
        if (row == NULL || prev == NULL) {
            return JXL_MODULAR_DECODER_ERROR;
        }
        JXL_MODULAR_TRY(rle_next_raw(rle, decoder, bs, cluster, &raw));
        w = dec_i16_add(dec_i16_unpack(raw), prev[0]);
        row[0] = w;
        for (x = 1; x < width; ++x) {
            int16_t nw = prev[x - 1];
            int16_t n = prev[x];
            int16_t pred = dec_i16_grad_clamped(n, w, nw);
            JXL_MODULAR_TRY(rle_next_raw(rle, decoder, bs, cluster, &raw));
            w = dec_i16_add(dec_i16_unpack(raw), pred);
            row[x] = w;
        }
    }
    return JXL_MODULAR_OK;
}

static jxl_modular_status_t decode_fast_lossless_grad_i16_rle(jxl_bs *bs, jxl_coding_decoder *decoder,
                                                              uint8_t cluster,
                                                              jxl_modular_grid_i32 *grid,
                                                              jxl_modular_rle_state *rle) {
    size_t stride;
    int16_t *pixels;
    const jxl_coding_prefix_rle_fast *fast;

    if (grid->width == 0 || grid->height == 0) {
        return JXL_MODULAR_OK;
    }
    if (grid->buf == NULL || grid->kind != JXL_MODULAR_SAMPLE_I16) {
        return JXL_MODULAR_DECODER_ERROR;
    }
    fast = jxl_coding_decoder_fast_rle_cluster(decoder, cluster);
    if (fast == NULL) {
        return decode_fast_lossless_grad_i16_rle_slow(bs, decoder, cluster, grid, rle);
    }
    pixels = (int16_t *)grid->buf + grid->offset;
    stride = jxl_modular_grid_row_stride(grid);
    (void)jxl_coding_decode_fast_lossless_gradient_i16(bs, fast, &rle->repeat, &rle->last_i16,
                                                      &rle->error, pixels, grid->width,
                                                      grid->height, stride);
    return JXL_MODULAR_OK;
}

jxl_modular_status_t jxl_modular_decode_fast_lossless_i16_rle(jxl_bs *bs, jxl_coding_decoder *decoder,
                                                              uint8_t cluster,
                                                              jxl_modular_grid_i32 *grid,
                                                              jxl_modular_rle_state *rle) {
    if (rle == NULL) {
        return JXL_MODULAR_DECODER_ERROR;
    }
    return decode_fast_lossless_grad_i16_rle(bs, decoder, cluster, grid, rle);
}

static jxl_modular_status_t decode_fast_lossless_grad_i16(jxl_bs *bs, jxl_coding_decoder *decoder,
                                                          uint8_t cluster,
                                                          jxl_modular_grid_i32 *grid,
                                                          jxl_modular_rle_state *rle) {
    size_t x;
    size_t y;
    size_t width;
    size_t height;
    int16_t w;
    int16_t *row0;

    if (rle != NULL) {
        return decode_fast_lossless_grad_i16_rle(bs, decoder, cluster, grid, rle);
    }

    width = grid->width;
    height = grid->height;

    if (width == 0 || height == 0) {
        return JXL_MODULAR_OK;
    }

    row0 = jxl_modular_grid_row_i16(grid, 0);
    if (row0 == NULL) {
        return JXL_MODULAR_DECODER_ERROR;
    }
    w = 0;
    for (x = 0; x < width; ++x) {
        uint32_t token = 0;
        JXL_MODULAR_TRY(read_token(rle, decoder, bs, cluster, 0, &token));
        w = dec_i16_add(w, dec_i16_unpack(token));
        row0[x] = w;
    }

    for (y = 1; y < height; ++y) {
        uint32_t token;
        int16_t *row = jxl_modular_grid_row_i16(grid, y);
        const int16_t *prev = jxl_modular_grid_row_i16_const(grid, y - 1);
        if (row == NULL || prev == NULL) {
            return JXL_MODULAR_DECODER_ERROR;
        }
        token = 0;
        JXL_MODULAR_TRY(read_token(rle, decoder, bs, cluster, 0, &token));
        w = dec_i16_add(dec_i16_unpack(token), prev[0]);
        row[0] = w;
        for (x = 1; x < width; ++x) {
            int16_t nw = prev[x - 1];
            int16_t n = prev[x];
            int16_t pred = dec_i16_grad_clamped(n, w, nw);
            JXL_MODULAR_TRY(read_token(rle, decoder, bs, cluster, 0, &token));
            w = dec_i16_add(dec_i16_unpack(token), pred);
            row[x] = w;
        }
    }
    return JXL_MODULAR_OK;
}

jxl_modular_status_t jxl_modular_decode_fast_lossless_grad(jxl_bs *bs, jxl_coding_decoder *decoder,
                                                           uint8_t cluster,
                                                           jxl_modular_grid_i32 *grid,
                                                           jxl_modular_rle_state *rle) {
    size_t x;
    size_t y;
    size_t width = grid->width;
    size_t height = grid->height;
    int32_t w;
    jxl_dec_sample ds;
    if (width == 0 || height == 0) {
        return JXL_MODULAR_OK;
    }
    if (grid->kind == JXL_MODULAR_SAMPLE_I16) {
        return decode_fast_lossless_grad_i16(bs, decoder, cluster, grid, rle);
    }

    ds = dec_sample_for(grid);
    w = 0;
    void *row0 = grid_row_mut_any(grid, 0);
    if (row0 == NULL) {
        return JXL_MODULAR_DECODER_ERROR;
    }
    for (x = 0; x < width; ++x) {
        uint32_t token = 0;
        JXL_MODULAR_TRY(read_token(rle, decoder, bs, cluster, 0, &token));
        w = dec_add(&ds, w, dec_unpacked(&ds, token));
        row_set(&ds, row0, x, w);
    }

    for (y = 1; y < height; ++y) {
        size_t x;
        uint32_t token;
        void *row = grid_row_mut_any(grid, y);
        void *prev = grid_row_mut_any(grid, y - 1);
        if (row == NULL || prev == NULL) {
            return JXL_MODULAR_DECODER_ERROR;
        }
        token = 0;
        JXL_MODULAR_TRY(read_token(rle, decoder, bs, cluster, 0, &token));
        w = dec_add(&ds, dec_unpacked(&ds, token), row_get(&ds, prev, 0));
        row_set(&ds, row, 0, w);
        for (x = 1; x < width; ++x) {
            int32_t nw = row_get(&ds, prev, x - 1);
            int32_t n = row_get(&ds, prev, x);
            int32_t pred = dec_grad_clamped(&ds, n, w, nw);
            JXL_MODULAR_TRY(read_token(rle, decoder, bs, cluster, 0, &token));
            w = dec_add(&ds, dec_unpacked(&ds, token), pred);
            row_set(&ds, row, x, w);
        }
    }
    return JXL_MODULAR_OK;
}

static jxl_modular_status_t decode_simple_grad(jxl_bs *bs, jxl_coding_decoder *decoder,
                                               uint8_t cluster, uint32_t dist_multiplier,
                                               jxl_modular_grid_i32 *grid,
                                               jxl_modular_rle_state *rle) {
                                                   size_t x;
                                                   size_t y;
    size_t width = grid->width;
    size_t height = grid->height;
    int32_t w;
    jxl_dec_sample ds;
    if (width == 0 || height == 0 || grid->buf == NULL) {
        return JXL_MODULAR_OK;
    }
    ds = dec_sample_for(grid);
    w = 0;
    void *row0 = grid_row_mut_any(grid, 0);
    if (row0 == NULL) {
        return JXL_MODULAR_DECODER_ERROR;
    }
    for (x = 0; x < width; ++x) {
        uint32_t token = 0;
        JXL_MODULAR_TRY(read_token(rle, decoder, bs, cluster, dist_multiplier, &token));
        w = dec_add(&ds, w, dec_unpacked(&ds, token));
        row_set(&ds, row0, x, w);
    }
    for (y = 1; y < height; ++y) {
        size_t x;
        uint32_t token;
        void *row = grid_row_mut_any(grid, y);
        void *prev = grid_row_mut_any(grid, y - 1);
        if (row == NULL || prev == NULL) {
            return JXL_MODULAR_DECODER_ERROR;
        }
        token = 0;
        JXL_MODULAR_TRY(read_token(rle, decoder, bs, cluster, dist_multiplier, &token));
        w = dec_add(&ds, dec_unpacked(&ds, token), row_get(&ds, prev, 0));
        row_set(&ds, row, 0, w);
        for (x = 1; x < width; ++x) {
            int32_t nw = row_get(&ds, prev, x - 1);
            int32_t n = row_get(&ds, prev, x);
            int32_t pred = dec_grad_clamped(&ds, n, w, nw);
            JXL_MODULAR_TRY(read_token(rle, decoder, bs, cluster, dist_multiplier, &token));
            w = dec_add(&ds, dec_unpacked(&ds, token), pred);
            row_set(&ds, row, x, w);
        }
    }
    return JXL_MODULAR_OK;
}

#ifndef NDEBUG
static void debug_pixel_hook(jxl_context *ctx, const jxl_modular_properties *props,
                             const jxl_ma_tree_leaf_clustered *leaf, uint32_t token,
                             int32_t diff, int32_t pred, int32_t out, size_t bits) {
    unsigned dy;
    unsigned dx;
    unsigned dch;
    if (!JXL_DEBUG_FLAG(ctx, debug_pixel) || props == NULL || props->state == NULL) {
        return;
    }
    if (g_pixel_ch != JXL_DEBUG_FLAG(ctx, debug_pixel_ch) ||
        props->state->y != JXL_DEBUG_FLAG(ctx, debug_pixel_y) ||
        props->state->x != JXL_DEBUG_FLAG(ctx, debug_pixel_x)) {
        return;
    }
    dy = JXL_DEBUG_FLAG(ctx, debug_pixel_y);
    dx = JXL_DEBUG_FLAG(ctx, debug_pixel_x);
    dch = JXL_DEBUG_FLAG(ctx, debug_pixel_ch);
    fprintf(stderr,
            "pixel ch=%u (%u,%u) edge=%d cluster=%u pred=%u mult=%u off=%d raw=%u diff=%d "
            "sc=%d pred_val=%d out=%d bits=%zu\n",
            g_pixel_ch, dy, dx, props->is_edge, (unsigned)leaf->cluster,
            (unsigned)leaf->predictor, (unsigned)leaf->multiplier, leaf->offset, token, diff,
            props->has_sc_prediction ? (int)props->sc_prediction.prediction : 0, pred, out, bits);
    const jxl_modular_predictor_state *st = props->state;
    fprintf(stderr, "  props y=%d x=%d n=%d w=%d nw=%d\n", (int)st->y, (int)st->x, st->n,
            st->w, st->nw);
}
#endif

static jxl_modular_status_t decode_one(jxl_context *ctx, jxl_bs *bs, jxl_coding_decoder *decoder,
                                       uint32_t dist_multiplier,
                                       const jxl_ma_tree_leaf_clustered *leaf,
                                       const jxl_modular_properties *props,
                                       const jxl_dec_sample *ds, void *row, size_t x,
                                       int32_t *out_sample, jxl_modular_rle_state *rle) {
    uint32_t token = 0;
    JXL_MODULAR_TRY(read_token(rle, decoder, bs, leaf->cluster, dist_multiplier, &token));
    int32_t diff = dec_unpacked(ds, token);
    diff = dec_wrapping_muladd(ds, diff, (int32_t)leaf->multiplier, leaf->offset);
    int32_t pred = jxl_modular_predict(leaf->predictor, props, props->is_edge);
    int32_t out = dec_add(ds, diff, pred);
    row_set(ds, row, x, out);
    if (out_sample != NULL) {
        *out_sample = out;
    }
#ifndef NDEBUG
    debug_pixel_hook(ctx, props, leaf, token, diff, pred, out, bs->num_read_bits);
#endif
    return JXL_MODULAR_OK;
}

static jxl_modular_status_t decode_single_node(jxl_context *ctx, jxl_allocator_state *alloc, jxl_bs *bs,
                                               jxl_coding_decoder *decoder,
                                               uint32_t dist_multiplier,
                                               const jxl_wp_header *wp,
                                               jxl_modular_grid_i32 *grid,
                                               const jxl_ma_tree_leaf_clustered *leaf,
                                               jxl_modular_rle_state *rle) {
                                                   size_t y;
    jxl_dec_sample ds = dec_sample_for(grid);
    jxl_modular_predictor_state pred;
    const jxl_wp_header *wp_use;
    if (leaf->predictor == JXL_PREDICTOR_ZERO && leaf->offset == 0) {
        size_t y;
        uint32_t single = 0;
        if (jxl_coding_decoder_single_token(decoder, leaf->cluster, &single)) {
            size_t y;
            int32_t value = dec_wrapping_muladd(&ds, dec_unpacked(&ds, single),
                                                  (int32_t)leaf->multiplier, leaf->offset);
            for (y = 0; y < grid->height; ++y) {
                size_t x;
                void *row = grid_row_mut_any(grid, y);
                if (row == NULL) {
                    return JXL_MODULAR_DECODER_ERROR;
                }
                for (x = 0; x < grid->width; ++x) {
                    row_set(&ds, row, x, value);
                }
            }
            return JXL_MODULAR_OK;
        }
        for (y = 0; y < grid->height; ++y) {
            size_t x;
            void *row = grid_row_mut_any(grid, y);
            if (row == NULL) {
                return JXL_MODULAR_DECODER_ERROR;
            }
            for (x = 0; x < grid->width; ++x) {
                uint32_t token = 0;
                int32_t value;
                JXL_MODULAR_TRY(read_token(rle, decoder, bs, leaf->cluster, dist_multiplier, &token));
                value = dec_wrapping_muladd(&ds, dec_unpacked(&ds, token),
                                                    (int32_t)leaf->multiplier, leaf->offset);
                row_set(&ds, row, x, value);
            }
        }
        return JXL_MODULAR_OK;
    }
    if (leaf->predictor == JXL_PREDICTOR_GRADIENT && leaf->offset == 0 && leaf->multiplier == 1) {
        return decode_simple_grad(bs, decoder, leaf->cluster, dist_multiplier, grid, rle);
    }

    jxl_modular_predictor_state_init(&pred);
    wp_use = leaf->predictor == JXL_PREDICTOR_SELF_CORRECTING ? wp : NULL;
    jxl_modular_predictor_state_reset(alloc, &pred, (uint32_t)grid->width, NULL, 0, wp_use);

    for (y = 0; y < grid->height; ++y) {
        size_t x;
        void *row = grid_row_mut_any(grid, y);
        if (row == NULL) {
            jxl_modular_predictor_state_free(alloc, &pred);
            return JXL_MODULAR_DECODER_ERROR;
        }
        for (x = 0; x < grid->width; ++x) {
            jxl_modular_properties props;
            int32_t sample;
	    jxl_modular_status_t one_st;
            jxl_modular_properties_edge(&pred, &props);
            sample = 0;
            one_st =
                decode_one(ctx, bs, decoder, dist_multiplier, leaf, &props, &ds, row, x, &sample, rle);
            JXL_MODULAR_TRY(one_st);
            jxl_modular_properties_record(&props, sample);
        }
    }
    jxl_modular_predictor_state_free(alloc, &pred);
    return JXL_MODULAR_OK;
}

static uint8_t cluster_from_table(int32_t sample, int32_t value_base, const uint8_t *cluster_table,
                                  size_t cluster_table_len) {
    int32_t index = sample - value_base;
    if (index < 0) {
        index = 0;
    }
    if ((size_t)index >= cluster_table_len) {
        index = cluster_table_len > 0 ? (int32_t)(cluster_table_len - 1) : 0;
    }
    return cluster_table[(size_t)index];
}

static jxl_modular_status_t decode_gradient_table(jxl_bs *bs, jxl_coding_decoder *decoder,
                                                  uint32_t dist_multiplier, int32_t value_base,
                                                  const uint8_t *cluster_table,
                                                  size_t cluster_table_len,
                                                  jxl_modular_grid_i32 *grid,
                                                  jxl_modular_rle_state *rle) {
                                                      size_t x;
                                                      size_t y;
    size_t width = grid->width;
    size_t height = grid->height;
    int32_t w;
    jxl_dec_sample ds;
    void *row0;
    if (width == 0 || height == 0) {
        return JXL_MODULAR_OK;
    }
    ds = dec_sample_for(grid);
    w = 0;
    row0 = grid_row_mut_any(grid, 0);
    if (row0 == NULL) {
        return JXL_MODULAR_DECODER_ERROR;
    }
    for (x = 0; x < width; ++x) {
        uint8_t cluster = cluster_from_table(w, value_base, cluster_table, cluster_table_len);
        uint32_t token = 0;
        JXL_MODULAR_TRY(read_token(rle, decoder, bs, cluster, dist_multiplier, &token));
        w = dec_add(&ds, w, dec_unpacked(&ds, token));
        row_set(&ds, row0, x, w);
    }
    for (y = 1; y < height; ++y) {
        size_t x;
        uint32_t token;
        void *row = grid_row_mut_any(grid, y);
        void *prev = grid_row_mut_any(grid, y - 1);
        uint8_t cluster;
        if (row == NULL || prev == NULL) {
            return JXL_MODULAR_DECODER_ERROR;
        }
        cluster =
            cluster_from_table(row_get(&ds, prev, 0), value_base, cluster_table, cluster_table_len);
        token = 0;
        JXL_MODULAR_TRY(read_token(rle, decoder, bs, cluster, dist_multiplier, &token));
        w = dec_add(&ds, dec_unpacked(&ds, token), row_get(&ds, prev, 0));
        row_set(&ds, row, 0, w);
        for (x = 1; x < width; ++x) {
            int32_t nw = row_get(&ds, prev, x - 1);
            int32_t n = row_get(&ds, prev, x);
            int32_t prop = dec_add(&ds, dec_add(&ds, n, w), -nw);
            int32_t pred = dec_grad_clamped(&ds, n, w, nw);
            cluster = cluster_from_table(prop, value_base, cluster_table, cluster_table_len);
            JXL_MODULAR_TRY(read_token(rle, decoder, bs, cluster, dist_multiplier, &token));
            w = dec_add(&ds, dec_unpacked(&ds, token), pred);
            row_set(&ds, row, x, w);
        }
    }
    return JXL_MODULAR_OK;
}

static jxl_modular_status_t decode_table_one(jxl_bs *bs, jxl_coding_decoder *decoder,
                                             uint32_t dist_multiplier, uint32_t decision_prop,
                                             int32_t value_base,
                                             const jxl_ma_tree_leaf_clustered *leaf,
                                             const uint8_t *cluster_table,
                                             size_t cluster_table_len,
                                             const jxl_modular_properties *props,
                                             const jxl_dec_sample *ds, void *row, size_t x,
                                             int32_t *out_sample, jxl_modular_rle_state *rle) {
    int32_t prop_value = jxl_modular_properties_get(props, decision_prop);
    uint32_t token = 0;
    uint8_t cluster =
        cluster_from_table(prop_value, value_base, cluster_table, cluster_table_len);
    int32_t diff;
    int32_t pred;
    int32_t out;
    JXL_MODULAR_TRY(read_token(rle, decoder, bs, cluster, dist_multiplier, &token));
    diff = dec_unpacked(ds, token);
    diff = dec_wrapping_muladd(ds, diff, (int32_t)leaf->multiplier, leaf->offset);
    pred = jxl_modular_predict(leaf->predictor, props, props->is_edge);
    out = dec_add(ds, diff, pred);
    row_set(ds, row, x, out);
    if (out_sample != NULL) {
        *out_sample = out;
    }
    return JXL_MODULAR_OK;
}

static jxl_modular_status_t decode_simple_table_slow(
    jxl_allocator_state *alloc, jxl_bs *bs, jxl_coding_decoder *decoder, uint32_t dist_multiplier,
    uint32_t decision_prop, int32_t value_base, const jxl_ma_tree_leaf_clustered *leaf,
    const uint8_t *cluster_table, size_t cluster_table_len, const jxl_wp_header *wp,
    jxl_modular_predictor_state *pred, jxl_modular_grid_i32 *grid, jxl_modular_rle_state *rle) {
    size_t y;
    jxl_dec_sample ds = dec_sample_for(grid);
    size_t height;
    size_t width;
    const jxl_wp_header *wp_use =
        (decision_prop == 15 || leaf->predictor == JXL_PREDICTOR_SELF_CORRECTING) ? wp : NULL;
    jxl_modular_predictor_state_reset(alloc, pred, (uint32_t)grid->width, NULL, 0, wp_use);

    height = grid->height;
    width = grid->width;
    for (y = 0; y < height && y < 2; ++y) {
        size_t x;
        void *row = grid_row_mut_any(grid, y);
        if (row == NULL) {
            return JXL_MODULAR_DECODER_ERROR;
        }
        for (x = 0; x < width; ++x) {
            jxl_modular_properties props;
            int32_t sample;
            jxl_modular_properties_edge(pred, &props);
            sample = 0;
            JXL_MODULAR_TRY(decode_table_one(bs, decoder, dist_multiplier, decision_prop,
                                             value_base, leaf, cluster_table, cluster_table_len,
                                             &props, &ds, row, x, &sample, rle));
            jxl_modular_properties_record(&props, sample);
        }
    }
    for (y = 2; y < height; ++y) {
        size_t x;
        size_t left_end;
        size_t right_start;
        void *row = grid_row_mut_any(grid, y);
        if (row == NULL) {
            return JXL_MODULAR_DECODER_ERROR;
        }
        left_end = width < 2 ? width : 2;
        right_start = width < 4 ? width : width - 2;
        for (x = 0; x < left_end; ++x) {
            jxl_modular_properties props;
            int32_t sample;
            jxl_modular_properties_edge(pred, &props);
            sample = 0;
            JXL_MODULAR_TRY(decode_table_one(bs, decoder, dist_multiplier, decision_prop,
                                             value_base, leaf, cluster_table, cluster_table_len,
                                             &props, &ds, row, x, &sample, rle));
            jxl_modular_properties_record(&props, sample);
        }
        for (x = left_end; x < right_start; ++x) {
            jxl_modular_properties props;
            int32_t sample;
            jxl_modular_properties_interior(pred, &props);
            sample = 0;
            JXL_MODULAR_TRY(decode_table_one(bs, decoder, dist_multiplier, decision_prop,
                                             value_base, leaf, cluster_table, cluster_table_len,
                                             &props, &ds, row, x, &sample, rle));
            jxl_modular_properties_record(&props, sample);
        }
        for (x = right_start; x < width; ++x) {
            jxl_modular_properties props;
            int32_t sample;
            jxl_modular_properties_edge(pred, &props);
            sample = 0;
            JXL_MODULAR_TRY(decode_table_one(bs, decoder, dist_multiplier, decision_prop,
                                             value_base, leaf, cluster_table, cluster_table_len,
                                             &props, &ds, row, x, &sample, rle));
            jxl_modular_properties_record(&props, sample);
        }
    }
    return JXL_MODULAR_OK;
}

static jxl_modular_status_t decode_simple_table(
    jxl_allocator_state *alloc, jxl_bs *bs, jxl_coding_decoder *decoder, uint32_t dist_multiplier,
    uint32_t decision_prop, int32_t value_base, const jxl_ma_tree_leaf_clustered *leaf,
    const uint8_t *cluster_table, size_t cluster_table_len, const jxl_wp_header *wp,
    jxl_modular_predictor_state *pred, jxl_modular_grid_i32 *grid, jxl_modular_rle_state *rle) {
    if (leaf->offset == 0 && leaf->multiplier == 1 && decision_prop == 9 &&
        leaf->predictor == JXL_PREDICTOR_GRADIENT) {
        return decode_gradient_table(bs, decoder, dist_multiplier, value_base, cluster_table,
                                     cluster_table_len, grid, rle);
    }
    return decode_simple_table_slow(alloc, bs, decoder, dist_multiplier, decision_prop, value_base,
                                    leaf, cluster_table, cluster_table_len, wp, pred, grid, rle);
}

jxl_modular_status_t jxl_modular_decode_channel(jxl_bs *bs, jxl_coding_decoder *decoder,
                                                const jxl_modular_channel_decode_params *params,
                                                jxl_modular_grid_i32 *grid,
                                                const jxl_modular_grid_i32 *const *prev_channels,
                                                size_t prev_channel_count) {
    jxl_modular_rle_state rle_st;
    jxl_modular_rle_state *rle;
    int owned_rle;
    uint32_t decision_prop;
    int32_t value_base;
    jxl_ma_tree_leaf_clustered table_leaf;
    uint8_t cluster_table[256];
    size_t cluster_table_len;
    jxl_modular_predictor_state local_pred;
    int owned_pred;
    jxl_modular_status_t st;
    const jxl_ma_tree_leaf_clustered *single;
    jxl_modular_predictor_state *pred;
    const jxl_wp_header *wp;
    size_t prev_depth;
    if (bs == NULL || decoder == NULL || params == NULL || grid == NULL ||
        params->ma_tree == NULL) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }

    rle = params->rle;
    owned_rle = 0;
    /* Rust only uses RLE reads on the all-fast-lossless path; otherwise LZ varints. */
    if (rle == NULL && jxl_coding_decoder_is_rle_mode(decoder) &&
        jxl_ma_flat_tree_is_fast_lossless_gradient(params->ma_tree)) {
        memset(&rle_st, 0, sizeof(rle_st));
        if (jxl_coding_decoder_prepare_fast_rle(decoder) != JXL_CODING_OK) {
            return JXL_MODULAR_DECODER_ERROR;
        }
        rle = &rle_st;
        owned_rle = 1;
    }

    st = JXL_MODULAR_OK;
    if (grid->width == 0 || grid->height == 0) {
        goto done;
    }

    single = jxl_ma_flat_tree_single_leaf(params->ma_tree);
    if (single != NULL) {
        if (rle != NULL && jxl_ma_flat_tree_is_fast_lossless_gradient(params->ma_tree)) {
            st = jxl_modular_decode_fast_lossless_grad(bs, decoder, single->cluster, grid, rle);
            goto done;
        }
        st = decode_single_node(params->ctx, params->alloc, bs, decoder, params->dist_multiplier,
                                params->wp_header, grid, single, rle);
        goto done;
    }

    decision_prop = 0;
    value_base = 0;
    cluster_table_len = 0;
    if (jxl_ma_flat_tree_simple_table(params->ma_tree, &decision_prop, &value_base, &table_leaf,
                                      cluster_table, sizeof(cluster_table), &cluster_table_len)) {
        jxl_modular_predictor_state local_pred;
        int owned_pred;
        jxl_modular_predictor_state *pred = params->predictor;
        owned_pred = 0;
        if (pred == NULL) {
            jxl_modular_predictor_state_init(&local_pred);
            pred = &local_pred;
            owned_pred = 1;
        }
        st = decode_simple_table(params->alloc, bs, decoder, params->dist_multiplier, decision_prop,
                                 value_base, &table_leaf, cluster_table, cluster_table_len,
                                 params->wp_header, pred, grid, rle);
        if (owned_pred) {
            jxl_modular_predictor_state_free(params->alloc, pred);
        }
        goto done;
    }

    pred = params->predictor;
    owned_pred = 0;
    if (pred == NULL) {
        jxl_modular_predictor_state_init(&local_pred);
        pred = &local_pred;
        owned_pred = 1;
    }
    wp = params->ma_tree->need_self_correcting ? params->wp_header : NULL;
    prev_depth = params->ma_tree->max_prev_channel_depth;
    if (prev_depth > prev_channel_count) {
        prev_depth = prev_channel_count;
    }
    /* Rust: filtered_prev[..ma_tree.max_prev_channel_depth()] — most-recent-first prefix. */
    jxl_modular_predictor_state_reset(params->alloc, pred, (uint32_t)grid->width, prev_channels,
                                      prev_depth, wp);
    st = jxl_modular_channel_decode_slow(params->ctx, bs, decoder, params->dist_multiplier,
                                         params->ma_tree, pred, grid, rle);
    if (owned_pred) {
        jxl_modular_predictor_state_free(params->alloc, pred);
    }

done:
    if (owned_rle) {
        if (st == JXL_MODULAR_OK && rle_st.error != JXL_CODING_OK) {
            st = JXL_MODULAR_DECODER_ERROR;
        }
    }
    return st;
}
