// SPDX-License-Identifier: MIT OR Apache-2.0
#include "decoder.h"

#include "ans.h"
#include "cdecoder_modular_internal.h"
#include "cdecoder_private.h"
#include "coding/ans_read_inline.h"
#include "coding/integer_read_inline.h"
#include "coding/internal.h"
#include "context.h"
#include "prefix.h"
#include "util.h"

#include "jxl_oxide/jxl_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct jxl_coding_prefix_rle_fast {
    uint32_t toplevel_mask;
    size_t toplevel_bits;
    size_t toplevel_len;
    size_t second_level_len;
    const jxl_prefix_entry *toplevel_entries;
    const jxl_prefix_entry *second_level_entries;
    uint32_t value_split;
    uint32_t value_msb;
    uint32_t value_lsb;
    uint32_t value_split_exp;
    uint32_t len_split;
    uint32_t len_msb;
    uint32_t len_lsb;
    uint32_t len_split_exp;
    uint32_t lz_min_symbol;
    uint32_t lz_min_length;
    uint8_t shape;
    uint32_t flat_symbol;
};

#define JXL_FAST_RLE_SHAPE_GENERIC 0u
#define JXL_FAST_RLE_SHAPE_SRGB_NESTED10 1u /* tl_bits=10, value split=1, len split=16 */
#define JXL_FAST_RLE_SHAPE_SRGB_FLAT 2u     /* single-symbol prefix, same integer config */

static uint8_t fast_rle_shape_from(const jxl_prefix_histogram *hist,
                                   const jxl_integer_config *value_config,
                                   uint32_t *flat_symbol_out) {
    if (value_config->split != 1u || value_config->split_exponent != 0u ||
        value_config->msb_in_token != 0u || value_config->lsb_in_token != 0u) {
        return JXL_FAST_RLE_SHAPE_GENERIC;
    }
    if (flat_symbol_out != NULL &&
        jxl_prefix_histogram_single_symbol(hist, flat_symbol_out)) {
        return JXL_FAST_RLE_SHAPE_SRGB_FLAT;
    }
    if (hist->toplevel_bits == 10 && hist->toplevel_len == 1024) {
        return JXL_FAST_RLE_SHAPE_SRGB_NESTED10;
    }
    return JXL_FAST_RLE_SHAPE_GENERIC;
}

static void fast_rle_dump_config(FILE *out, jxl_coding_decoder *dec, uint8_t cluster,
                                 const jxl_coding_prefix_rle_fast *fast) {
    size_t i;
    size_t nested;
    const jxl_prefix_histogram *hist;
    nested = 0;
    if (out == NULL || dec == NULL || fast == NULL) {
        return;
    }
    hist = &dec->code.u.prefix.histograms[cluster];
    for (i = 0; i < hist->toplevel_len; ++i) {
        if (hist->toplevel_entries[i].nested) {
            nested++;
        }
    }
    fprintf(out,
            "cluster=%u shape=%u flat_sym=%u tl_bits=%lu tl_len=%lu sl_len=%lu nested_tl=%lu "
            "value(split=0x%x exp=%u msb=%u lsb=%u) len(split=0x%x exp=%u msb=%u lsb=%u) "
            "lz_min_sym=%u lz_min_len=%u\n",
            (unsigned)cluster, (unsigned)fast->shape, fast->flat_symbol,
            (unsigned long)hist->toplevel_bits, (unsigned long)hist->toplevel_len,
            (unsigned long)hist->second_level_len, (unsigned long)nested, fast->value_split,
            fast->value_split_exp, fast->value_msb, fast->value_lsb, fast->len_split,
            fast->len_split_exp, fast->len_msb, fast->len_lsb, fast->lz_min_symbol,
            fast->lz_min_length);
}

jxl_inline void fast_defer_set_err(jxl_coding_status_t *err, jxl_coding_status_t code) {
    if (*err == JXL_CODING_OK) {
        *err = code;
    }
}

jxl_inline void fast_bs_consume_defer(jxl_bs *bs, size_t n, jxl_coding_status_t *err) {
    if (n > bs->remaining_buf_bits) {
        fast_defer_set_err(err, JXL_CODING_BITSTREAM_ERROR);
    } else {
        bs->remaining_buf_bits -= n;
        bs->num_read_bits += n;
        bs->buf >>= n;
    }
}

typedef struct {
    uint32_t tl_mask;
    size_t tl_bits;
    const jxl_prefix_entry *tl_entries;
    const jxl_prefix_entry *sl_entries;
    uint32_t value_split;
    uint32_t value_msb;
    uint32_t value_lsb;
    uint32_t value_split_exp;
    uint32_t len_split;
    uint32_t len_msb;
    uint32_t len_lsb;
    uint32_t len_split_exp;
    uint32_t lz_min_symbol;
    uint32_t lz_min_length;
} fast_rle_hot;

jxl_inline fast_rle_hot fast_rle_hot_from(const jxl_coding_prefix_rle_fast *fast) {
    fast_rle_hot h;
    h.tl_mask = fast->toplevel_mask;
    h.tl_bits = fast->toplevel_bits;
    h.tl_entries = fast->toplevel_entries;
    h.sl_entries = fast->second_level_entries;
    h.value_split = fast->value_split;
    h.value_msb = fast->value_msb;
    h.value_lsb = fast->value_lsb;
    h.value_split_exp = fast->value_split_exp;
    h.len_split = fast->len_split;
    h.len_msb = fast->len_msb;
    h.len_lsb = fast->len_lsb;
    h.len_split_exp = fast->len_split_exp;
    h.lz_min_symbol = fast->lz_min_symbol;
    h.lz_min_length = fast->lz_min_length;
    return h;
}

#define FAST_BS_CONSUME_DEFER(bs, n, err_p) fast_bs_consume_defer((bs), (n), (err_p))

static const int8_t JXL_SPECIAL_DISTANCES[120][2] = {
    {0, 1},   {1, 0},   {1, 1},   {-1, 1},  {0, 2},   {2, 0},   {1, 2},   {-1, 2},
    {2, 1},   {-2, 1},  {2, 2},   {-2, 2},  {0, 3},   {3, 0},   {1, 3},   {-1, 3},
    {3, 1},   {-3, 1},  {2, 3},   {-2, 3},  {3, 2},   {-3, 2},  {0, 4},   {4, 0},
    {1, 4},   {-1, 4},  {4, 1},   {-4, 1},  {3, 3},   {-3, 3},  {2, 4},   {-2, 4},
    {4, 2},   {-4, 2},  {0, 5},   {3, 4},   {-3, 4},  {4, 3},   {-4, 3},  {5, 0},
    {1, 5},   {-1, 5},  {5, 1},   {-5, 1},  {2, 5},   {-2, 5},  {5, 2},   {-5, 2},
    {4, 4},   {-4, 4},  {3, 5},   {-3, 5},  {5, 3},   {-5, 3},  {0, 6},   {6, 0},
    {1, 6},   {-1, 6},  {6, 1},   {-6, 1},  {2, 6},   {-2, 6},  {6, 2},   {-6, 2},
    {4, 5},   {-4, 5},  {5, 4},   {-5, 4},  {3, 6},   {-3, 6},  {6, 3},   {-6, 3},
    {0, 7},   {7, 0},   {1, 7},   {-1, 7},  {5, 5},   {-5, 5},  {7, 1},   {-7, 1},
    {4, 6},   {-4, 6},  {6, 4},   {-6, 4},  {2, 7},   {-2, 7},  {7, 2},   {-7, 2},
    {3, 7},   {-3, 7},  {7, 3},   {-7, 3},  {5, 6},   {-5, 6},  {6, 5},   {-6, 5},
    {8, 0},   {4, 7},   {-4, 7},  {7, 4},   {-7, 4},  {8, 1},   {8, 2},   {6, 6},
    {-6, 6},  {8, 3},   {5, 7},   {-5, 7},  {7, 5},   {-7, 5},  {8, 4},   {6, 7},
    {-6, 7},  {7, 6},   {-7, 6},  {8, 5},   {7, 7},   {-7, 7},  {8, 6},   {8, 7},
};

static void coder_destroy(jxl_allocator_state *alloc, jxl_coder *coder) {
    size_t i;
    if (coder->kind == JXL_CODER_KIND_PREFIX) {
        for (i = 0; i < coder->u.prefix.count; ++i) {
            jxl_prefix_histogram_destroy(alloc, &coder->u.prefix.histograms[i]);
        }
        jxl_free(alloc, coder->u.prefix.histograms);
        coder->u.prefix.histograms = NULL;
        coder->u.prefix.count = 0;
    } else {
        for (i = 0; i < coder->u.ans.count; ++i) {
            jxl_ans_histogram_destroy(alloc, &coder->u.ans.histograms[i]);
        }
        jxl_free(alloc, coder->u.ans.histograms);
        coder->u.ans.histograms = NULL;
        coder->u.ans.count = 0;
    }
}

static jxl_coding_status_t integer_config_parse(jxl_bs *bs, uint32_t log_alphabet_size,
                                                jxl_integer_config *out) {
    const uint32_t split_exponent_bits = jxl_coding_add_log2_ceil(log_alphabet_size);
    uint32_t split_exponent = 0;
    uint32_t msb_in_token;
    uint32_t lsb_in_token;
    JXL_CODING_TRY_BS(jxl_bs_read_bits(bs, split_exponent_bits, &split_exponent));

    msb_in_token = 0;
    lsb_in_token = 0;
    if (split_exponent != log_alphabet_size) {
        uint32_t lsb_bits;
        const uint32_t msb_bits = jxl_coding_add_log2_ceil(split_exponent);
        JXL_CODING_TRY_BS(jxl_bs_read_bits(bs, msb_bits, &msb_in_token));
        if (msb_in_token > split_exponent) {
            return JXL_CODING_INVALID_INTEGER_CONFIG;
        }
        lsb_bits = jxl_coding_add_log2_ceil(split_exponent - msb_in_token);
        JXL_CODING_TRY_BS(jxl_bs_read_bits(bs, lsb_bits, &lsb_in_token));
    }
    if (lsb_in_token + msb_in_token > split_exponent) {
        return JXL_CODING_INVALID_INTEGER_CONFIG;
    }
    out->split_exponent = split_exponent;
    out->split = 1u << split_exponent;
    out->msb_in_token = msb_in_token;
    out->lsb_in_token = lsb_in_token;
    return JXL_CODING_OK;
}

#if defined(__GNUC__) || defined(__clang__)
#define JXL_FAST_ALWAYS_INLINE static __attribute__((always_inline)) inline
#else
#define JXL_FAST_ALWAYS_INLINE jxl_inline
#endif

JXL_FAST_ALWAYS_INLINE int16_t fast_lossless_i16_unpack(uint32_t raw) {
    uint16_t bit = (uint16_t)(raw & 1u);
    uint16_t base = (uint16_t)(raw >> 1);
    return (int16_t)(base ^ (uint16_t)(0u - bit));
}

JXL_FAST_ALWAYS_INLINE int16_t fast_lossless_i16_add(int16_t a, int16_t b) {
    return (int16_t)((uint16_t)a + (uint16_t)b);
}

JXL_FAST_ALWAYS_INLINE int16_t fast_lossless_i16_grad(int16_t n, int16_t w, int16_t nw) {
    int32_t a;
    int32_t b;
    int32_t g;
    int32_t lo;
    int32_t hi;

    if (w > n) {
        a = (int32_t)w;
        b = (int32_t)n;
    } else {
        a = (int32_t)n;
        b = (int32_t)w;
    }
    g = a + b - (int32_t)nw;
    lo = b;
    hi = a;
    if (g < lo) {
        return (int16_t)lo;
    }
    if (g > hi) {
        return (int16_t)hi;
    }
    return (int16_t)g;
}

static uint32_t read_uint_prefilled(jxl_bs *bs, const jxl_integer_config *config, uint32_t token) {
    return jxl_integer_read_uint_config(bs, config, token);
}

JXL_FAST_ALWAYS_INLINE uint32_t fast_read_uint(jxl_bs *bs, uint32_t split, uint32_t msb_in_token,
                                               uint32_t lsb_in_token, uint32_t split_exponent,
                                               uint32_t token) {
    return jxl_integer_read_uint(bs, split, msb_in_token, lsb_in_token, split_exponent, token);
}

jxl_inline uint32_t fast_read_uint_deferred(jxl_bs *bs, uint32_t split, uint32_t msb_in_token,
                                            uint32_t lsb_in_token, uint32_t split_exponent,
                                            uint32_t token, jxl_coding_status_t *err) {
    uint32_t rest_bits;
    uint32_t n;
    uint32_t tok;
    if (token < split) {
        return token;
    }

    n = split_exponent - (msb_in_token + lsb_in_token) +
        ((token - split) >> (msb_in_token + lsb_in_token));
    n &= 31u;

    if (n > bs->remaining_buf_bits) {
        jxl_bs_refill(bs);
    }
    if (n > bs->remaining_buf_bits) {
        *err = JXL_CODING_BITSTREAM_ERROR;
        return 0;
    }

    rest_bits = (uint32_t)(bs->buf & ((1ull << n) - 1ull));
    bs->remaining_buf_bits -= n;
    bs->num_read_bits += n;
    bs->buf >>= n;

    {
        const uint32_t low_mask = lsb_in_token == 0 ? 0u : ((1u << lsb_in_token) - 1u);
        const uint64_t low_bits = token & low_mask;
        tok = token >> lsb_in_token;
        {
            const uint32_t msb_mask = msb_in_token == 0 ? 0u : ((1u << msb_in_token) - 1u);
            tok &= msb_mask;
            tok |= 1u << msb_in_token;
            return (uint32_t)(((((uint64_t)tok << n) | rest_bits) << lsb_in_token) | low_bits);
        }
    }
}

JXL_FAST_ALWAYS_INLINE uint32_t fast_prefix_read_trusted(jxl_bs *bs, const fast_rle_hot *h) {
    uint32_t peeked;
    uint32_t tl_off;
    uint32_t chunk;
    jxl_prefix_entry tl;
    jxl_prefix_entry sl;

    jxl_bs_refill(bs);
    peeked = (uint32_t)(bs->buf & ((1ull << JXL_PREFIX_MAX_PREFIX_BITS) - 1ull));
    tl_off = peeked & h->tl_mask;
    tl = h->tl_entries[tl_off];
    if (tl.nested) {
        chunk = (peeked >> h->tl_bits) & tl.bits_or_mask;
        sl = h->sl_entries[(size_t)tl.symbol_or_offset + (size_t)chunk];

        bs->remaining_buf_bits -= sl.bits_or_mask;
        bs->num_read_bits += sl.bits_or_mask;
        bs->buf >>= sl.bits_or_mask;
        return sl.symbol_or_offset;
    }
    bs->remaining_buf_bits -= tl.bits_or_mask;
    bs->num_read_bits += tl.bits_or_mask;
    bs->buf >>= tl.bits_or_mask;
    return tl.symbol_or_offset;
}

JXL_FAST_ALWAYS_INLINE void fast_rle_refill_trusted(jxl_bs *bs, const fast_rle_hot *h, uint32_t *repeat,
                                        int16_t *last_value) {
    uint32_t sym;
    uint32_t raw;

    sym = fast_prefix_read_trusted(bs, h);
    if (sym >= h->lz_min_symbol) {
        *repeat = fast_read_uint(bs, h->len_split, h->len_msb, h->len_lsb, h->len_split_exp,
                                 sym - h->lz_min_symbol) +
                  h->lz_min_length;
    } else {
        raw = fast_read_uint(bs, h->value_split, h->value_msb, h->value_lsb, h->value_split_exp,
                             sym);
        *last_value = fast_lossless_i16_unpack(raw);
        *repeat = 1;
    }
}

JXL_FAST_ALWAYS_INLINE int16_t fast_rle_step_trusted(jxl_bs *bs, const fast_rle_hot *h, uint32_t *repeat,
                                       int16_t *last_value) {
    if (*repeat == 0) {
        fast_rle_refill_trusted(bs, h, repeat, last_value);
    }
    *repeat -= 1u;
    return *last_value;
}

jxl_inline void fast_prefix_read_hoisted(jxl_bs *bs, const fast_rle_hot *h, uint32_t *sym_out,
                                         jxl_coding_status_t *err) {
    jxl_bs_refill(bs);
    {
        const uint32_t peeked =
            (uint32_t)(bs->buf & ((1ull << JXL_PREFIX_MAX_PREFIX_BITS) - 1ull));
        const uint32_t tl_off = peeked & h->tl_mask;
        const jxl_prefix_entry tl = h->tl_entries[tl_off];
        if (tl.nested) {
            const uint32_t chunk = (peeked >> h->tl_bits) & tl.bits_or_mask;
            const jxl_prefix_entry sl = h->sl_entries[(size_t)tl.symbol_or_offset + (size_t)chunk];
            fast_bs_consume_defer(bs, sl.bits_or_mask, err);
            *sym_out = sl.symbol_or_offset;
        } else {
            fast_bs_consume_defer(bs, tl.bits_or_mask, err);
            *sym_out = tl.symbol_or_offset;
        }
    }
}

jxl_inline void fast_rle_refill_i16(jxl_bs *bs, const fast_rle_hot *h, uint32_t *repeat,
                                    int16_t *last_value, jxl_coding_status_t *err) {
    uint32_t sym = 0;
    fast_prefix_read_hoisted(bs, h, &sym, err);
    if (sym >= h->lz_min_symbol) {
        uint32_t run = fast_read_uint_deferred(bs, h->len_split, h->len_msb, h->len_lsb,
                                               h->len_split_exp, sym - h->lz_min_symbol, err);
        if (run > UINT32_MAX - h->lz_min_length) {
            fast_defer_set_err(err, JXL_CODING_INVALID_LZ77_SYMBOL);
        } else {
            *repeat = run + h->lz_min_length;
        }
    } else {
        uint32_t raw = fast_read_uint_deferred(bs, h->value_split, h->value_msb, h->value_lsb,
                                               h->value_split_exp, sym, err);
        *last_value = fast_lossless_i16_unpack(raw);
        *repeat = 1;
    }
}

jxl_inline int16_t fast_rle_step_i16(jxl_bs *bs, const fast_rle_hot *h, uint32_t *repeat,
                                     int16_t *last_value, jxl_coding_status_t *err) {
    if (*repeat > 0) {
        (*repeat)--;
        return *last_value;
    }
    fast_rle_refill_i16(bs, h, repeat, last_value, err);
    (*repeat)--;
    return *last_value;
}

/* Trusted fast-lossless path: histogram validated at parse time (matches Rust). */
#define FAST_PREFIX_READ_SYMBOL_UNCHECKED(bs, fast, sym_out, err_p)                                \
    do {                                                                                           \
        fast_rle_hot _h = fast_rle_hot_from(&(fast));                                              \
        fast_prefix_read_hoisted((bs), &_h, &(sym_out), (err_p));                                  \
    } while (0)

#define FAST_RLE_STEP_DEFERRED(bs, fast, repeat_p, last_i16_p, err_p)                              \
    do {                                                                                           \
        fast_rle_hot _h = fast_rle_hot_from(&(fast));                                              \
        (void)fast_rle_step_i16((bs), &_h, (repeat_p), (last_i16_p), (err_p));                     \
    } while (0)

static uint8_t lz_dist_cluster(const jxl_coding_decoder *dec) {
    return dec->clusters[dec->num_dist - 1];
}

static jxl_coding_status_t coder_read_symbol(jxl_coder *coder, jxl_bs *bs, uint8_t cluster,
                                             uint32_t *symbol_out) {
    if (coder->kind == JXL_CODER_KIND_PREFIX) {
        if ((size_t)cluster >= coder->u.prefix.count) {
            return JXL_CODING_INVALID_PREFIX_HISTOGRAM;
        }
        return jxl_prefix_histogram_read_symbol(&coder->u.prefix.histograms[cluster], bs,
                                                symbol_out);
    }
    if (coder->u.ans.initial) {
        JXL_CODING_TRY_BS(jxl_bs_read_bits(bs, 32, &coder->u.ans.state));
        coder->u.ans.initial = 0;
    }
    if ((size_t)cluster >= coder->u.ans.count) {
        return JXL_CODING_INVALID_ANS_HISTOGRAM;
    }
    return jxl_ans_histogram_read_symbol(&coder->u.ans.histograms[cluster], bs, &coder->u.ans.state,
                                         symbol_out);
}

static int coder_single_symbol(const jxl_coder *coder, uint8_t cluster, uint32_t *symbol_out) {
    if (coder->kind == JXL_CODER_KIND_PREFIX) {
        if ((size_t)cluster >= coder->u.prefix.count) {
            return 0;
        }
        return jxl_prefix_histogram_single_symbol(&coder->u.prefix.histograms[cluster], symbol_out);
    }
    if ((size_t)cluster >= coder->u.ans.count) {
        return 0;
    }
    return jxl_ans_histogram_single_symbol(&coder->u.ans.histograms[cluster], symbol_out);
}

jxl_coding_status_t jxl_coding_decoder_lz77_store_at(jxl_coding_decoder *dec, uint32_t *num_decoded,
                                                     uint32_t r) {
    jxl_lz77_state *state = &dec->lz;
    const size_t offset = (size_t)((*num_decoded) & 0xfffffu);
    if (offset >= state->window_cap) {
        uint32_t *grown;
        size_t new_cap = state->window_cap == 0 ? 64 : state->window_cap;
        while (new_cap <= offset) {
            new_cap *= 2;
        }
        grown = jxl_alloc(dec->alloc, new_cap * sizeof(uint32_t));
        if (grown == NULL) {
            return JXL_CODING_OUT_OF_MEMORY;
        }
        if (state->window != NULL) {
            memcpy(grown, state->window, offset * sizeof(uint32_t));
            jxl_free(dec->alloc, state->window);
        }
        state->window = grown;
        state->window_cap = new_cap;
    }
    state->window[offset] = r;
    *num_decoded += 1;
    return JXL_CODING_OK;
}

static jxl_coding_status_t lz77_store_symbol(jxl_coding_decoder *dec, uint32_t r) {
    return jxl_coding_decoder_lz77_store_at(dec, &dec->lz.num_decoded, r);
}

jxl_coding_status_t jxl_coding_decoder_lz77_store(jxl_coding_decoder *dec, uint32_t r) {
    return lz77_store_symbol(dec, r);
}

int jxl_coding_decoder_hoist_available(const jxl_coding_decoder *dec) {
    return dec != NULL && dec->code.kind == JXL_CODER_KIND_ANS && dec->lz77_enabled;
}

int jxl_coding_decoder_hoist_begin(jxl_coding_decoder *dec, jxl_coding_hoist_slot *out) {
    if (!jxl_coding_decoder_hoist_available(dec) || out == NULL) {
        return 0;
    }
    out->ans_state = dec->code.u.ans.state;
    out->lz_num_to_copy = dec->lz.num_to_copy;
    out->lz_copy_pos = dec->lz.copy_pos;
    out->lz_num_decoded = dec->lz.num_decoded;
    return 1;
}

void jxl_coding_decoder_hoist_publish(jxl_coding_decoder *dec, const jxl_coding_hoist_slot *slot) {
    if (dec == NULL || slot == NULL) {
        return;
    }
    dec->code.u.ans.state = slot->ans_state;
    dec->lz.num_to_copy = slot->lz_num_to_copy;
    dec->lz.copy_pos = slot->lz_copy_pos;
    dec->lz.num_decoded = slot->lz_num_decoded;
}

void jxl_coding_decoder_hoist_capture(jxl_coding_decoder *dec, jxl_coding_hoist_slot *slot) {
    if (dec == NULL || slot == NULL) {
        return;
    }
    slot->ans_state = dec->code.u.ans.state;
    slot->lz_num_to_copy = dec->lz.num_to_copy;
    slot->lz_copy_pos = dec->lz.copy_pos;
    slot->lz_num_decoded = dec->lz.num_decoded;
}

void jxl_coding_decoder_hoist_commit(jxl_coding_decoder *dec, const jxl_coding_hoist_slot *slot) {
    jxl_coding_decoder_hoist_publish(dec, slot);
}

jxl_coding_status_t jxl_coding_decoder_lz77_from_repeat_token_hoisted(jxl_coding_decoder *dec,
                                                                      jxl_bs *bs,
                                                                      jxl_coding_hoist_slot *slot,
                                                                      uint32_t dist_multiplier,
                                                                      uint32_t repeat_token,
                                                                      uint32_t *out) {
    jxl_lz77_state *state = &dec->lz;
    uint8_t lz_cluster;
    uint32_t num_to_copy;
    uint32_t distance;
    uint32_t cap_dist;
    uint32_t token;
    uint32_t r;
    jxl_coding_status_t st;

    if (slot->lz_num_decoded == 0) {
        return JXL_CODING_UNEXPECTED_LZ77_REPEAT;
    }
    lz_cluster = lz_dist_cluster(dec);
    num_to_copy = read_uint_prefilled(bs, &state->lz_len_conf, repeat_token - dec->lz_min_symbol);
    if (num_to_copy > UINT32_MAX - dec->lz_min_length) {
        return JXL_CODING_INVALID_LZ77_SYMBOL;
    }
    num_to_copy += dec->lz_min_length;
    slot->lz_num_to_copy = num_to_copy;

    if ((size_t)lz_cluster >= dec->code.u.ans.count) {
        return JXL_CODING_INVALID_ANS_HISTOGRAM;
    }
    st = jxl_ans_histogram_read_symbol_inline(&dec->code.u.ans.histograms[lz_cluster], bs,
                                              &slot->ans_state, &token);
    if (st != JXL_CODING_OK) {
        return st;
    }
    distance = read_uint_prefilled(bs, &dec->configs[lz_cluster], token);
    if (dist_multiplier != 0) {
        if (distance < 120) {
            const int8_t offset = JXL_SPECIAL_DISTANCES[distance][0];
            const int8_t dist = JXL_SPECIAL_DISTANCES[distance][1];
            int32_t d = (int32_t)offset + (int32_t)dist_multiplier * (int32_t)dist;
            distance = (uint32_t)((d - 1) > 0 ? (d - 1) : 0);
        } else {
            distance -= 120;
        }
    }

    cap_dist = (1u << 20) - 1u;
    distance = (cap_dist < distance ? cap_dist : distance) + 1;
    if (distance > slot->lz_num_decoded) {
        distance = slot->lz_num_decoded;
    }
    slot->lz_copy_pos = slot->lz_num_decoded - distance;

    r = state->window[slot->lz_copy_pos & 0xfffffu];
    slot->lz_copy_pos += 1;
    slot->lz_num_to_copy -= 1;

    st = jxl_coding_decoder_lz77_store_at(dec, &slot->lz_num_decoded, r);
    if (st != JXL_CODING_OK) {
        return st;
    }
    *out = r;
    return JXL_CODING_OK;
}

jxl_coding_status_t jxl_coding_decoder_lz77_from_repeat_token(jxl_coding_decoder *dec, jxl_bs *bs,
                                                               uint32_t dist_multiplier,
                                                               uint32_t repeat_token,
                                                               uint32_t *out) {
    jxl_lz77_state *state = &dec->lz;
    uint8_t lz_cluster;
    uint32_t num_to_copy;
    uint32_t distance;
    uint32_t cap_dist;
    uint32_t token;
    uint32_t r;
    jxl_coding_status_t st;

    if (state->num_decoded == 0) {
        return JXL_CODING_UNEXPECTED_LZ77_REPEAT;
    }
    lz_cluster = lz_dist_cluster(dec);
    num_to_copy = read_uint_prefilled(bs, &state->lz_len_conf, repeat_token - dec->lz_min_symbol);
    if (num_to_copy > UINT32_MAX - dec->lz_min_length) {
        return JXL_CODING_INVALID_LZ77_SYMBOL;
    }
    num_to_copy += dec->lz_min_length;
    state->num_to_copy = num_to_copy;

    st = coder_read_symbol(&dec->code, bs, lz_cluster, &token);
    if (st != JXL_CODING_OK) {
        return st;
    }
    distance = read_uint_prefilled(bs, &dec->configs[lz_cluster], token);
    if (dist_multiplier != 0) {
        if (distance < 120) {
            const int8_t offset = JXL_SPECIAL_DISTANCES[distance][0];
            const int8_t dist = JXL_SPECIAL_DISTANCES[distance][1];
            int32_t d = (int32_t)offset + (int32_t)dist_multiplier * (int32_t)dist;
            distance = (uint32_t)((d - 1) > 0 ? (d - 1) : 0);
        } else {
            distance -= 120;
        }
    }

    cap_dist = (1u << 20) - 1u;
    distance = (cap_dist < distance ? cap_dist : distance) + 1;
    if (distance > state->num_decoded) {
        distance = state->num_decoded;
    }
    state->copy_pos = state->num_decoded - distance;

    r = state->window[state->copy_pos & 0xfffffu];
    state->copy_pos += 1;
    state->num_to_copy -= 1;

    st = lz77_store_symbol(dec, r);
    if (st != JXL_CODING_OK) {
        return st;
    }
    *out = r;
    return JXL_CODING_OK;
}

static jxl_coding_status_t decoder_read_varint_lz77(jxl_coding_decoder *dec, jxl_bs *bs,
                                                    uint8_t cluster, uint32_t dist_multiplier,
                                                    uint32_t *out) {
    uint32_t r;
    jxl_lz77_state *state = &dec->lz;
    r = 0;

    if (state->num_to_copy > 0) {
        r = state->window[state->copy_pos & 0xfffffu];
        state->copy_pos += 1;
        state->num_to_copy -= 1;
    } else {
        uint32_t token = 0;
        jxl_coding_status_t st = coder_read_symbol(&dec->code, bs, cluster, &token);
        if (st != JXL_CODING_OK) {
            return st;
        }
        if (token >= dec->lz_min_symbol) {
            return jxl_coding_decoder_lz77_from_repeat_token(dec, bs, dist_multiplier, token, out);
        } else {
            if ((size_t)cluster >= dec->num_clusters) {
                return JXL_CODING_INVALID_INTEGER_CONFIG;
            }
            r = read_uint_prefilled(bs, &dec->configs[cluster], token);
        }
    }

    jxl_coding_status_t store_st = lz77_store_symbol(dec, r);
    if (store_st != JXL_CODING_OK) {
        return store_st;
    }
    *out = r;
    return JXL_CODING_OK;
}

static jxl_coding_status_t decoder_inner_parse(jxl_allocator_state *alloc, jxl_bs *bs,
                                               uint32_t num_dist, jxl_coding_decoder *dec) {
                                                   size_t i;
    uint32_t num_clusters = 0;
    size_t clusters_len;
    int use_prefix_code;
    uint32_t log_alphabet_size;
    uint8_t *clusters = NULL;
    clusters_len = 0;
    jxl_coding_status_t st =
        jxl_coding_read_clusters(alloc, bs, num_dist, &num_clusters, &clusters, &clusters_len);
    if (st != JXL_CODING_OK) {
        return st;
    }
    dec->clusters = clusters;
    dec->num_dist = clusters_len;

    use_prefix_code = 0;
    JXL_CODING_TRY_BS(jxl_bs_read_bool(bs, &use_prefix_code));
    log_alphabet_size = 15;
    if (!use_prefix_code) {
        uint32_t extra = 0;
        JXL_CODING_TRY_BS(jxl_bs_read_bits(bs, 2, &extra));
        log_alphabet_size = extra + 5;
    }

    dec->num_clusters = num_clusters;
    dec->configs = jxl_alloc(alloc, num_clusters * sizeof(jxl_integer_config));
    if (dec->configs == NULL) {
        return JXL_CODING_OUT_OF_MEMORY;
    }
    for (i = 0; i < num_clusters; ++i) {
        st = integer_config_parse(bs, log_alphabet_size, &dec->configs[i]);
        if (st != JXL_CODING_OK) {
            return st;
        }
    }
    if (use_prefix_code) {
        size_t i;
        uint32_t *prefix_counts;
        dec->code.kind = JXL_CODER_KIND_PREFIX;
        dec->code.u.prefix.count = num_clusters;
        dec->code.u.prefix.histograms =
            jxl_alloc(alloc, num_clusters * sizeof(jxl_prefix_histogram));
        if (dec->code.u.prefix.histograms == NULL) {
            return JXL_CODING_OUT_OF_MEMORY;
        }
        memset(dec->code.u.prefix.histograms, 0,
               num_clusters * sizeof(jxl_prefix_histogram));
        prefix_counts = jxl_alloc(alloc, num_clusters * sizeof(uint32_t));
        if (prefix_counts == NULL) {
            return JXL_CODING_OUT_OF_MEMORY;
        }
        for (i = 0; i < num_clusters; ++i) {
            int has_count = 0;
            uint32_t count;
            JXL_CODING_TRY_BS(jxl_bs_read_bool(bs, &has_count));
            count = 1;
            if (has_count) {
                uint32_t n = 0;
                uint32_t extra_bits;
                JXL_CODING_TRY_BS(jxl_bs_read_bits(bs, 4, &n));
                extra_bits = 0;
                JXL_CODING_TRY_BS(jxl_bs_read_bits(bs, n, &extra_bits));
                count = 1u + (1u << n) + extra_bits;
            }
            if (count > (1u << 15)) {
                jxl_free(alloc, prefix_counts);
                return JXL_CODING_INVALID_PREFIX_HISTOGRAM;
            }
            prefix_counts[i] = count;
        }
        for (i = 0; i < num_clusters; ++i) {
            st = jxl_prefix_histogram_parse(alloc, bs, prefix_counts[i],
                                          &dec->code.u.prefix.histograms[i]);
            if (st != JXL_CODING_OK) {
                jxl_free(alloc, prefix_counts);
                return st;
            }
        }
        jxl_free(alloc, prefix_counts);
    } else {
        size_t i;
        dec->code.kind = JXL_CODER_KIND_ANS;
        dec->code.u.ans.count = num_clusters;
        dec->code.u.ans.state = 0;
        dec->code.u.ans.initial = 1;
        dec->code.u.ans.histograms = jxl_alloc(alloc, num_clusters * sizeof(jxl_ans_histogram));
        if (dec->code.u.ans.histograms == NULL) {
            return JXL_CODING_OUT_OF_MEMORY;
        }
        memset(dec->code.u.ans.histograms, 0, num_clusters * sizeof(jxl_ans_histogram));
        for (i = 0; i < num_clusters; ++i) {
            st = jxl_ans_histogram_parse(alloc, bs, log_alphabet_size, &dec->code.u.ans.histograms[i]);
            if (st != JXL_CODING_OK) {
                return st;
            }
        }
    }
    return JXL_CODING_OK;
}

static jxl_coding_status_t lz77_parse(jxl_bs *bs, jxl_coding_decoder *dec) {
    int enabled = 0;
    JXL_CODING_TRY_BS(jxl_bs_read_bool(bs, &enabled));
    const jxl_u32_spec min_symbol_specs[4] = {JXL_U32_C(224), JXL_U32_C(512), JXL_U32_C(4096),
                                              JXL_U32_BITS(8, 15)};
    const jxl_u32_spec min_length_specs[4] = {JXL_U32_C(3), JXL_U32_C(4), JXL_U32_BITS(5, 2),
                                              JXL_U32_BITS(9, 8)};
    if (!enabled) {
        dec->lz77_enabled = 0;
        return JXL_CODING_OK;
    }
    dec->lz77_enabled = 1;
    JXL_CODING_TRY_BS(jxl_bs_read_u32(bs, min_symbol_specs, &dec->lz_min_symbol));
    JXL_CODING_TRY_BS(jxl_bs_read_u32(bs, min_length_specs, &dec->lz_min_length));
    return integer_config_parse(bs, 8, &dec->lz.lz_len_conf);
}

static jxl_coding_status_t decoder_parse_assume_no_lz77(jxl_allocator_state *alloc, jxl_bs *bs,
                                                      uint32_t num_dist,
                                                      jxl_coding_decoder **out) {
    int lz77_enabled = 0;
    jxl_coding_decoder *dec;
    jxl_coding_status_t st;
    JXL_CODING_TRY_BS(jxl_bs_read_bool(bs, &lz77_enabled));
    if (lz77_enabled) {
        return JXL_CODING_LZ77_NOT_ALLOWED;
    }
    dec = jxl_alloc(alloc, sizeof(*dec));
    if (dec == NULL) {
        return JXL_CODING_OUT_OF_MEMORY;
    }
    memset(dec, 0, sizeof(*dec));
    dec->alloc = alloc;
    dec->lz77_enabled = 0;
    st = decoder_inner_parse(alloc, bs, num_dist, dec);
    if (st != JXL_CODING_OK) {
        jxl_coding_decoder_destroy(alloc, dec);
        return st;
    }
    *out = dec;
    return JXL_CODING_OK;
}

void jxl_coding_decoder_destroy(jxl_allocator_state *alloc, jxl_coding_decoder *dec) {
    if (dec == NULL) {
        return;
    }
    jxl_free(alloc, dec->clusters);
    jxl_free(alloc, dec->configs);
    jxl_free(alloc, dec->lz.window);
    jxl_free(alloc, dec->fast_rle_cache);
    coder_destroy(alloc, &dec->code);
    jxl_free(alloc, dec);
}

static jxl_coding_status_t prefix_histogram_clone(jxl_allocator_state *alloc,
                                                  const jxl_prefix_histogram *src,
                                                  jxl_prefix_histogram *out) {
    memset(out, 0, sizeof(*out));
    *out = *src;
    out->toplevel_entries = NULL;
    out->second_level_entries = NULL;
    if (src->toplevel_len > 0 && src->toplevel_entries != NULL) {
        out->toplevel_entries = jxl_alloc(alloc, src->toplevel_len * sizeof(*src->toplevel_entries));
        if (out->toplevel_entries == NULL) {
            return JXL_CODING_OUT_OF_MEMORY;
        }
        memcpy(out->toplevel_entries, src->toplevel_entries,
               src->toplevel_len * sizeof(*src->toplevel_entries));
    }
    if (src->second_level_len > 0 && src->second_level_entries != NULL) {
        out->second_level_entries =
            jxl_alloc(alloc, src->second_level_len * sizeof(*src->second_level_entries));
        if (out->second_level_entries == NULL) {
            jxl_prefix_histogram_destroy(alloc, out);
            return JXL_CODING_OUT_OF_MEMORY;
        }
        memcpy(out->second_level_entries, src->second_level_entries,
               src->second_level_len * sizeof(*src->second_level_entries));
    }
    return JXL_CODING_OK;
}

static jxl_coding_status_t coder_clone(jxl_allocator_state *alloc, const jxl_coder *src,
                                       jxl_coder *out) {
                                           size_t i;
    memset(out, 0, sizeof(*out));
    out->kind = src->kind;
    if (src->kind == JXL_CODER_KIND_PREFIX) {
        size_t i;
        out->u.prefix.count = src->u.prefix.count;
        if (src->u.prefix.count == 0) {
            return JXL_CODING_OK;
        }
        out->u.prefix.histograms =
            jxl_alloc(alloc, src->u.prefix.count * sizeof(*out->u.prefix.histograms));
        if (out->u.prefix.histograms == NULL) {
            return JXL_CODING_OUT_OF_MEMORY;
        }
        memset(out->u.prefix.histograms, 0,
               src->u.prefix.count * sizeof(*out->u.prefix.histograms));
        for (i = 0; i < src->u.prefix.count; ++i) {
            jxl_coding_status_t st =
                prefix_histogram_clone(alloc, &src->u.prefix.histograms[i], &out->u.prefix.histograms[i]);
            if (st != JXL_CODING_OK) {
                coder_destroy(alloc, out);
                return st;
            }
        }
        return JXL_CODING_OK;
    }
    out->u.ans.count = src->u.ans.count;
    out->u.ans.state = 0;
    out->u.ans.initial = 1;
    if (src->u.ans.count == 0) {
        return JXL_CODING_OK;
    }
    out->u.ans.histograms = jxl_alloc(alloc, src->u.ans.count * sizeof(*out->u.ans.histograms));
    if (out->u.ans.histograms == NULL) {
        return JXL_CODING_OUT_OF_MEMORY;
    }
    memset(out->u.ans.histograms, 0, src->u.ans.count * sizeof(*out->u.ans.histograms));
    for (i = 0; i < src->u.ans.count; ++i) {
        jxl_coding_status_t st =
            jxl_ans_histogram_clone(alloc, &src->u.ans.histograms[i], &out->u.ans.histograms[i]);
        if (st != JXL_CODING_OK) {
            coder_destroy(alloc, out);
            return st;
        }
    }
    return JXL_CODING_OK;
}

jxl_coding_status_t jxl_coding_decoder_clone(jxl_allocator_state *alloc,
                                             const jxl_coding_decoder *src,
                                             jxl_coding_decoder **out) {
    jxl_coding_status_t st;
    jxl_coding_decoder *dec;
    if (alloc == NULL || src == NULL || out == NULL) {
        return JXL_CODING_BITSTREAM_ERROR;
    }
    *out = NULL;
    dec = jxl_alloc(alloc, sizeof(*dec));
    if (dec == NULL) {
        return JXL_CODING_OUT_OF_MEMORY;
    }
    memset(dec, 0, sizeof(*dec));
    dec->alloc = alloc;
    dec->ctx = src->ctx;
    dec->lz77_enabled = src->lz77_enabled;
    dec->lz_min_symbol = src->lz_min_symbol;
    dec->lz_min_length = src->lz_min_length;
    dec->lz.lz_len_conf = src->lz.lz_len_conf;
    dec->num_dist = src->num_dist;
    dec->num_clusters = src->num_clusters;

    if (src->num_dist > 0 && src->clusters != NULL) {
        dec->clusters = jxl_alloc(alloc, src->num_dist);
        if (dec->clusters == NULL) {
            jxl_coding_decoder_destroy(alloc, dec);
            return JXL_CODING_OUT_OF_MEMORY;
        }
        memcpy(dec->clusters, src->clusters, src->num_dist);
    }
    if (src->num_clusters > 0 && src->configs != NULL) {
        dec->configs = jxl_alloc(alloc, src->num_clusters * sizeof(*src->configs));
        if (dec->configs == NULL) {
            jxl_coding_decoder_destroy(alloc, dec);
            return JXL_CODING_OUT_OF_MEMORY;
        }
        memcpy(dec->configs, src->configs, src->num_clusters * sizeof(*src->configs));
    }
    st = coder_clone(alloc, &src->code, &dec->code);
    if (st != JXL_CODING_OK) {
        jxl_coding_decoder_destroy(alloc, dec);
        return st;
    }
    *out = dec;
    return JXL_CODING_OK;
}

jxl_coding_status_t jxl_coding_decoder_parse(jxl_allocator_state *alloc, jxl_bs *bs,
                                               uint32_t num_dist, jxl_coding_decoder **out) {
    uint32_t dist_count;
    jxl_coding_decoder *dec;
    jxl_coding_status_t st;
    if (out == NULL) {
        return JXL_CODING_BITSTREAM_ERROR;
    }
    *out = NULL;

    dec = jxl_alloc(alloc, sizeof(*dec));
    if (dec == NULL) {
        return JXL_CODING_OUT_OF_MEMORY;
    }
    memset(dec, 0, sizeof(*dec));
    dec->alloc = alloc;

    st = lz77_parse(bs, dec);
    if (st != JXL_CODING_OK) {
        jxl_coding_decoder_destroy(alloc, dec);
        return st;
    }

    dist_count = num_dist;
    if (dec->lz77_enabled) {
        dist_count += 1;
    }
    st = decoder_inner_parse(alloc, bs, dist_count, dec);
    if (st != JXL_CODING_OK) {
        jxl_coding_decoder_destroy(alloc, dec);
        return st;
    }
    *out = dec;
    return JXL_CODING_OK;
}

void jxl_coding_decoder_attach_context(jxl_coding_decoder *dec, jxl_context *ctx) {
    if (dec != NULL) {
        dec->ctx = ctx;
    }
}

jxl_context *jxl_coding_decoder_context(const jxl_coding_decoder *dec) {
    if (dec == NULL) {
        return NULL;
    }
    return dec->ctx;
}

jxl_coding_status_t jxl_coding_decoder_begin(jxl_coding_decoder *dec, jxl_bs *bs) {
    if (dec == NULL) {
        return JXL_CODING_BITSTREAM_ERROR;
    }
    dec->lz.num_decoded = 0;
    dec->lz.num_to_copy = 0;
    dec->lz.copy_pos = 0;
    if (dec->code.kind != JXL_CODER_KIND_ANS) {
        return JXL_CODING_OK;
    }
    JXL_CODING_TRY_BS(jxl_bs_read_bits(bs, 32, &dec->code.u.ans.state));
    dec->code.u.ans.initial = 0;
    if (JXL_DEBUG_FLAG(dec->ctx, debug_hf_trace)) {
        fprintf(stderr, "c ans begin state=0x%x bits=%zu\n", dec->code.u.ans.state, bs->num_read_bits);
    }
    return JXL_CODING_OK;
}

jxl_coding_status_t jxl_coding_decoder_finalize(const jxl_coding_decoder *dec) {
    if (dec->code.kind != JXL_CODER_KIND_ANS) {
        return JXL_CODING_OK;
    }
    if (dec->code.u.ans.state == 0x130000u) {
        return JXL_CODING_OK;
    }
    if (JXL_DEBUG_FLAG(dec->ctx, debug_hf_coeff)) {
        fprintf(stderr, "ans finalize bad state=0x%x\n", dec->code.u.ans.state);
    }
    return JXL_CODING_INVALID_ANS_STREAM;
}

jxl_coding_status_t jxl_coding_decoder_read_varint_clustered(jxl_coding_decoder *dec, jxl_bs *bs,
                                                             uint8_t cluster,
                                                             uint32_t dist_multiplier,
                                                             uint32_t *out) {
    uint32_t token;
    jxl_coding_status_t st;
    if (dec->lz77_enabled) {
        return decoder_read_varint_lz77(dec, bs, cluster, dist_multiplier, out);
    }
    token = 0;
    st = coder_read_symbol(&dec->code, bs, cluster, &token);
    if (st != JXL_CODING_OK) {
        return st;
    }
    if ((size_t)cluster >= dec->num_clusters) {
        return JXL_CODING_INVALID_INTEGER_CONFIG;
    }
    if (JXL_DEBUG_FLAG(dec->ctx, debug_hf_trace)) {
        fprintf(stderr, "c tok cl=%u tok=%u bits=%zu\n", (unsigned)cluster, token, bs->num_read_bits);
    }
    *out = read_uint_prefilled(bs, &dec->configs[cluster], token);
    return JXL_CODING_OK;
}

jxl_coding_status_t jxl_coding_decoder_read_varint_with_multiplier(jxl_coding_decoder *dec,
                                                                   jxl_bs *bs, uint32_t ctx,
                                                                   uint32_t dist_multiplier,
                                                                   uint32_t *out) {
    if (ctx >= dec->num_dist) {
        return JXL_CODING_INVALID_CLUSTER;
    }
    return jxl_coding_decoder_read_varint_clustered(dec, bs, dec->clusters[ctx], dist_multiplier,
                                                    out);
}

jxl_coding_status_t jxl_coding_decoder_read_varint(jxl_coding_decoder *dec, jxl_bs *bs,
                                                   uint32_t ctx, uint32_t *out) {
    return jxl_coding_decoder_read_varint_with_multiplier(dec, bs, ctx, 0, out);
}

int jxl_coding_decoder_is_rle_mode(const jxl_coding_decoder *dec) {
    uint32_t sym;
    uint8_t lz_cluster;
    if (dec == NULL || !dec->lz77_enabled) {
        return 0;
    }
    lz_cluster = lz_dist_cluster(dec);
    sym = 0;
    if (!coder_single_symbol(&dec->code, lz_cluster, &sym) || sym != 1) {
        return 0;
    }
    if ((size_t)lz_cluster >= dec->num_clusters) {
        return 0;
    }
    return dec->configs[lz_cluster].split_exponent == 0;
}

int jxl_coding_prefix_rle_fast_available(jxl_coding_decoder *dec, uint8_t cluster) {
    jxl_coding_prefix_rle_fast tmp;
    return jxl_coding_prefix_rle_fast_init(dec, cluster, &tmp);
}

int jxl_coding_prefix_rle_fast_init(jxl_coding_decoder *dec, uint8_t cluster,
                                    jxl_coding_prefix_rle_fast *out) {
    const jxl_prefix_histogram *hist;
    const jxl_integer_config *value_config;
    const jxl_integer_config *len_config;
    if (dec == NULL || out == NULL || !jxl_coding_decoder_is_rle_mode(dec)) {
        return 0;
    }
    if (dec->code.kind != JXL_CODER_KIND_PREFIX) {
        return 0;
    }
    if ((size_t)cluster >= dec->num_clusters || (size_t)cluster >= dec->code.u.prefix.count) {
        return 0;
    }
    hist = &dec->code.u.prefix.histograms[cluster];
    value_config = &dec->configs[cluster];
    len_config = &dec->lz.lz_len_conf;
    out->toplevel_mask = hist->toplevel_mask;
    out->toplevel_bits = hist->toplevel_bits;
    out->toplevel_len = hist->toplevel_len;
    out->second_level_len = hist->second_level_len;
    out->toplevel_entries = hist->toplevel_entries;
    out->second_level_entries = hist->second_level_entries;
    out->value_split = value_config->split;
    out->value_msb = value_config->msb_in_token;
    out->value_lsb = value_config->lsb_in_token;
    out->value_split_exp = value_config->split_exponent;
    out->len_split = len_config->split;
    out->len_msb = len_config->msb_in_token;
    out->len_lsb = len_config->lsb_in_token;
    out->len_split_exp = len_config->split_exponent;
    out->lz_min_symbol = dec->lz_min_symbol;
    out->lz_min_length = dec->lz_min_length;
    out->flat_symbol = 0;
    out->shape = fast_rle_shape_from(hist, value_config, &out->flat_symbol);
    return 1;
}

jxl_coding_status_t jxl_coding_decoder_prepare_fast_rle(jxl_coding_decoder *dec) {
    size_t i;
    if (dec == NULL) {
        return JXL_CODING_BITSTREAM_ERROR;
    }
    dec->fast_rle_cache_ready = 0;
    if (!jxl_coding_decoder_is_rle_mode(dec) || dec->code.kind != JXL_CODER_KIND_PREFIX) {
        return JXL_CODING_OK;
    }
    if (dec->fast_rle_cache_len < dec->num_clusters) {
        jxl_coding_prefix_rle_fast *grown =
            jxl_alloc(dec->alloc, dec->num_clusters * sizeof(*dec->fast_rle_cache));
        if (grown == NULL) {
            return JXL_CODING_OUT_OF_MEMORY;
        }
        jxl_free(dec->alloc, dec->fast_rle_cache);
        dec->fast_rle_cache = grown;
        dec->fast_rle_cache_len = dec->num_clusters;
    }
    for (i = 0; i < dec->num_clusters; ++i) {
        if (!jxl_coding_prefix_rle_fast_init(dec, (uint8_t)i, &dec->fast_rle_cache[i])) {
            return JXL_CODING_BITSTREAM_ERROR;
        }
    }
    {
        if (JXL_DEBUG_FLAG(dec->ctx, dump_fast_rle)) {
            FILE *out = stderr;
            fprintf(out, "fast_rle clusters=%lu rle_mode=%d\n",
                    (unsigned long)dec->num_clusters, jxl_coding_decoder_is_rle_mode(dec));
            for (i = 0; i < dec->num_clusters; ++i) {
                fast_rle_dump_config(out, dec, (uint8_t)i, &dec->fast_rle_cache[i]);
            }
        }
    }
    dec->fast_rle_cache_ready = 1;
    return JXL_CODING_OK;
}

const jxl_coding_prefix_rle_fast *jxl_coding_decoder_fast_rle_cluster(
    const jxl_coding_decoder *dec, uint8_t cluster) {
    if (dec == NULL || !dec->fast_rle_cache_ready || (size_t)cluster >= dec->num_clusters) {
        return NULL;
    }
    return &dec->fast_rle_cache[cluster];
}

jxl_coding_status_t jxl_coding_prefix_rle_next_raw(jxl_coding_prefix_rle_fast *fast, jxl_bs *bs,
                                                   uint32_t *repeat, int16_t *last_value) {
    jxl_coding_status_t err = JXL_CODING_OK;
    if (fast == NULL || bs == NULL || repeat == NULL || last_value == NULL) {
        return JXL_CODING_BITSTREAM_ERROR;
    }
    FAST_RLE_STEP_DEFERRED(bs, *fast, repeat, last_value, &err);
    return err;
}

/*
 * Disasm note: cold-path splits (refill tail out-of-line, strided row path, deferred
 * helpers marked cold/noinline) shrank the LTO hot fn ~7.3 KB→~4.7 KB but regressed
 * srgb ~2–4 Mpix/s vs fused baseline — keep monolithic for GCC+LTO.
 */
JXL_ATTRIBUTE_HOT JXL_CODING_FAST_DECODE_NOINLINE
jxl_coding_status_t jxl_coding_decode_fast_lossless_gradient_i16(
    jxl_bs *bs, const jxl_coding_prefix_rle_fast *fast, uint32_t *repeat, int16_t *last_value,
    jxl_coding_status_t *defer_err, int16_t * jxl_restrict pixels, size_t width, size_t height,
    size_t row_stride) {
    size_t y;
    size_t x;
    fast_rle_hot hot_cfg;
    const fast_rle_hot *hot;
    int16_t w;
    int16_t sample;
    int16_t pred;
    int16_t *row;
    const int16_t *prev;
    const int16_t *p;
    int16_t *out;
    const int16_t *prev_end;

    if (bs == NULL || fast == NULL || repeat == NULL || last_value == NULL || defer_err == NULL ||
        pixels == NULL) {
        return JXL_CODING_BITSTREAM_ERROR;
    }
    if (width == 0 || height == 0) {
        return JXL_CODING_OK;
    }
    (void)defer_err;

    hot_cfg = fast_rle_hot_from(fast);
    hot = &hot_cfg;

    if (row_stride == width) {
        row = pixels;
        w = 0;
        for (x = 0; x < width; ++x) {
            sample = fast_rle_step_trusted(bs, hot, repeat, last_value);
            w = fast_lossless_i16_add(w, sample);
            row[x] = w;
        }
        for (y = 1; y < height; ++y) {
            prev = row;
            row += width;
            sample = fast_rle_step_trusted(bs, hot, repeat, last_value);
            w = fast_lossless_i16_add(sample, prev[0]);
            row[0] = w;
            p = prev + 1;
            out = row + 1;
            prev_end = prev + width;
            while (p < prev_end) {
                pred = fast_lossless_i16_grad(p[0], w, p[-1]);
                sample = fast_rle_step_trusted(bs, hot, repeat, last_value);
                w = fast_lossless_i16_add(sample, pred);
                *out++ = w;
                ++p;
            }
        }
    } else {
        w = 0;
        for (x = 0; x < width; ++x) {
            sample = fast_rle_step_trusted(bs, hot, repeat, last_value);
            w = fast_lossless_i16_add(w, sample);
            pixels[x] = w;
        }
        for (y = 1; y < height; ++y) {
            row = pixels + y * row_stride;
            prev = pixels + (y - 1) * row_stride;
            sample = fast_rle_step_trusted(bs, hot, repeat, last_value);
            w = fast_lossless_i16_add(sample, prev[0]);
            row[0] = w;
            p = prev + 1;
            out = row + 1;
            prev_end = prev + width;
            while (p < prev_end) {
                pred = fast_lossless_i16_grad(p[0], w, p[-1]);
                sample = fast_rle_step_trusted(bs, hot, repeat, last_value);
                w = fast_lossless_i16_add(sample, pred);
                *out++ = w;
                ++p;
            }
        }
    }
    return JXL_CODING_OK;
}

jxl_coding_status_t jxl_coding_decoder_read_rle_token(jxl_coding_decoder *dec, jxl_bs *bs,
                                                      uint8_t cluster,
                                                      jxl_coding_rle_token *out) {
    uint32_t token;
    jxl_coding_status_t st;
    if (dec == NULL || bs == NULL || out == NULL || !dec->lz77_enabled) {
        return JXL_CODING_BITSTREAM_ERROR;
    }
    token = 0;
    st = coder_read_symbol(&dec->code, bs, cluster, &token);
    if (st != JXL_CODING_OK) {
        return st;
    }
    if (token >= dec->lz_min_symbol) {
        uint32_t repeat =
            read_uint_prefilled(bs, &dec->lz.lz_len_conf, token - dec->lz_min_symbol);
        if (repeat > UINT32_MAX - dec->lz_min_length) {
            return JXL_CODING_INVALID_LZ77_SYMBOL;
        }
        out->kind = JXL_CODING_RLE_REPEAT;
        out->repeat = repeat + dec->lz_min_length;
        out->value = 0;
        return JXL_CODING_OK;
    }
    if ((size_t)cluster >= dec->num_clusters) {
        return JXL_CODING_INVALID_INTEGER_CONFIG;
    }
    out->kind = JXL_CODING_RLE_VALUE;
    out->value = read_uint_prefilled(bs, &dec->configs[cluster], token);
    out->repeat = 0;
    return JXL_CODING_OK;
}

int jxl_coding_decoder_single_token(const jxl_coding_decoder *dec, uint8_t cluster,
                                    uint32_t *token_out) {
    uint32_t single_symbol;
    if (dec->lz77_enabled) {
        return 0;
    }
    single_symbol = 0;
    if (!coder_single_symbol(&dec->code, cluster, &single_symbol)) {
        return 0;
    }
    if ((size_t)cluster >= dec->num_clusters) {
        return 0;
    }
    if (single_symbol >= dec->configs[cluster].split) {
        return 0;
    }
    *token_out = single_symbol;
    return 1;
}

const uint8_t *jxl_coding_decoder_cluster_map(const jxl_coding_decoder *dec, size_t *len_out) {
    if (len_out != NULL) {
        *len_out = dec->num_dist;
    }
    return dec->clusters;
}

static int cluster_set_has_hole(const uint8_t *clusters, size_t len, uint32_t num_clusters) {
    size_t i;
    uint8_t stack_seen[256];
    uint8_t *seen = NULL;
    if (num_clusters == 0) {
        return 0;
    }
    /* stack bitmap for clusters up to 256 */
    if (num_clusters > 256) {
        return 1;
    }
    seen = stack_seen;
    memset(seen, 0, num_clusters);
    for (i = 0; i < len; ++i) {
        if (clusters[i] >= num_clusters) {
            return 1;
        }
        seen[clusters[i]] = 1;
    }
    for (i = 0; i < num_clusters; ++i) {
        if (!seen[i]) {
            return 1;
        }
    }
    return 0;
}

jxl_coding_status_t jxl_coding_read_clusters(jxl_allocator_state *alloc, jxl_bs *bs,
                                             uint32_t num_dist, uint32_t *num_clusters_out,
                                             uint8_t **clusters_out, size_t *clusters_len_out) {
                                                 uint32_t i;
    int simple;
    uint32_t max_cluster;
    uint8_t *clusters;
    uint32_t num_clusters;
    if (num_clusters_out != NULL) {
        *num_clusters_out = 0;
    }
    if (clusters_out != NULL) {
        *clusters_out = NULL;
    }
    if (clusters_len_out != NULL) {
        *clusters_len_out = 0;
    }

    if (num_dist == 1) {
        uint8_t *clusters = jxl_alloc(alloc, 1);
        if (clusters == NULL) {
            return JXL_CODING_OUT_OF_MEMORY;
        }
        clusters[0] = 0;
        if (num_clusters_out != NULL) {
            *num_clusters_out = 1;
        }
        if (clusters_out != NULL) {
            *clusters_out = clusters;
        }
        if (clusters_len_out != NULL) {
            *clusters_len_out = 1;
        }
        return JXL_CODING_OK;
    }

    simple = 0;
    JXL_CODING_TRY_BS(jxl_bs_read_bool(bs, &simple));
    clusters = jxl_alloc(alloc, num_dist * sizeof(uint8_t));
    if (clusters == NULL) {
        return JXL_CODING_OUT_OF_MEMORY;
    }

    if (simple) {
        uint32_t i;
        uint32_t nbits = 0;
        JXL_CODING_TRY_BS(jxl_bs_read_bits(bs, 2, &nbits));
        for (i = 0; i < num_dist; ++i) {
            uint32_t b = 0;
            JXL_CODING_TRY_BS(jxl_bs_read_bits(bs, nbits, &b));
            if (b > 255) {
                jxl_free(alloc, clusters);
                return JXL_CODING_INVALID_CLUSTER;
            }
            clusters[i] = (uint8_t)b;
        }
    } else {
        uint32_t i;
        int use_mtf = 0;
        jxl_coding_status_t st;
        jxl_coding_decoder *decoder = NULL;
        JXL_CODING_TRY_BS(jxl_bs_read_bool(bs, &use_mtf));
        if (num_dist <= 2) {
            st = decoder_parse_assume_no_lz77(alloc, bs, 1, &decoder);
        } else {
            st = jxl_coding_decoder_parse(alloc, bs, 1, &decoder);
        }
        if (st != JXL_CODING_OK) {
            jxl_free(alloc, clusters);
            return st;
        }
        st = jxl_coding_decoder_begin(decoder, bs);
        if (st != JXL_CODING_OK) {
            jxl_coding_decoder_destroy(alloc, decoder);
            jxl_free(alloc, clusters);
            return st;
        }
        for (i = 0; i < num_dist; ++i) {
            uint32_t b = 0;
            st = jxl_coding_decoder_read_varint(decoder, bs, 0, &b);
            if (st != JXL_CODING_OK) {
                jxl_coding_decoder_destroy(alloc, decoder);
                jxl_free(alloc, clusters);
                return st;
            }
            if (b > 255) {
                jxl_coding_decoder_destroy(alloc, decoder);
                jxl_free(alloc, clusters);
                return JXL_CODING_INVALID_CLUSTER;
            }
            clusters[i] = (uint8_t)b;
        }
        st = jxl_coding_decoder_finalize(decoder);
        jxl_coding_decoder_destroy(alloc, decoder);
        if (st != JXL_CODING_OK) {
            jxl_free(alloc, clusters);
            return st;
        }

        if (use_mtf) {
            size_t idx;
            uint32_t i;
            uint8_t mtfmap[256];
            for (idx = 0; idx < 256; ++idx) {
                mtfmap[idx] = (uint8_t)idx;
            }
            for (i = 0; i < num_dist; ++i) {
                idx = clusters[i];
                clusters[i] = mtfmap[idx];
                memmove(&mtfmap[1], &mtfmap[0], idx * sizeof(uint8_t));
                mtfmap[0] = clusters[i];
            }
        }
    }

    max_cluster = 0;
    for (i = 0; i < num_dist; ++i) {
        if (clusters[i] > max_cluster) {
            max_cluster = clusters[i];
        }
    }
    num_clusters = max_cluster + 1;
    if (cluster_set_has_hole(clusters, num_dist, num_clusters)) {
        jxl_free(alloc, clusters);
        return JXL_CODING_CLUSTER_HOLE;
    }

    if (num_clusters_out != NULL) {
        *num_clusters_out = num_clusters;
    }
    if (clusters_out != NULL) {
        *clusters_out = clusters;
    } else {
        jxl_free(alloc, clusters);
    }
    if (clusters_len_out != NULL) {
        *clusters_len_out = num_dist;
    }
    return JXL_CODING_OK;
}
