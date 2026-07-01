// SPDX-License-Identifier: MIT OR Apache-2.0
#include "hf_pass.h"

#include "coding/decoder.h"
#include "context.h"
#include "vardct/dct_select.h"
#include "vardct/util.h"

#include <string.h>

static const jxl_u32_spec k_used_orders[4] = {JXL_U32_C(0x5F), JXL_U32_C(0x13), JXL_U32_C(0),
                                              JXL_U32_BITS(0, 13)};

static const size_t k_block_sizes[JXL_HF_PASS_ORDER_COUNT][2] = {
    {8, 8},   {8, 8},   {16, 16}, {32, 32}, {16, 8},  {32, 8},  {32, 16},
    {64, 64}, {64, 32}, {128, 128}, {128, 64}, {256, 256}, {256, 128},
};

static void fill_natural_order(size_t bw, size_t bh, jxl_coeff_order *out, size_t cap) {
    size_t dist;
    size_t y_scale = bw / bh;
    size_t idx = 0;
    size_t lbw = bw / 8;
    size_t lbh = bh / 8;

    while (idx < lbw * lbh && idx < cap) {
        out[idx].x = (uint16_t)(idx % lbw);
        out[idx].y = (uint16_t)(idx / lbw);
        idx += 1;
    }

    for (dist = 1; dist < 2 * bw && idx < cap; ++dist) {
        size_t order;
        size_t margin = dist > bw ? dist - bw : 0;
        for (order = margin; order < dist - margin; ++order) {
            size_t x;
            size_t y;
            if (dist % 2 == 1) {
                x = order;
                y = dist - 1 - order;
            } else {
                x = dist - 1 - order;
                y = order;
            }
            if (x < lbw && y < lbw) {
                continue;
            }
            if (y % y_scale != 0) {
                continue;
            }
            out[idx].x = (uint16_t)x;
            out[idx].y = (uint16_t)(y / y_scale);
            idx += 1;
        }
    }
}

static void ensure_natural_orders(jxl_context *ctx) {
    jxl_context_hf_orders *orders;
    if (ctx == NULL || ctx->hf_orders.initialized) {
        return;
    }
    orders = &ctx->hf_orders;
    fill_natural_order(8, 8, orders->natural_8x8, 64);
    fill_natural_order(16, 16, orders->natural_16x16, 256);
    fill_natural_order(32, 32, orders->natural_32x32, 1024);
    fill_natural_order(16, 8, orders->natural_16x8, 128);
    fill_natural_order(32, 8, orders->natural_32x8, 256);
    fill_natural_order(32, 16, orders->natural_32x16, 512);
    fill_natural_order(64, 64, orders->natural_64x64, 4096);
    fill_natural_order(64, 32, orders->natural_64x32, 2048);
    fill_natural_order(128, 128, orders->natural_128x128, 16384);
    orders->initialized = 1;
}

static const jxl_coeff_order *natural_order_lazy(jxl_context *ctx, size_t order_id,
                                                 size_t *len_out) {
    const jxl_context_hf_orders *orders;
    ensure_natural_orders(ctx);
    if (ctx == NULL) {
        if (len_out != NULL) {
            *len_out = 0;
        }
        return NULL;
    }
    if (len_out != NULL) {
        *len_out = k_block_sizes[order_id][0] * k_block_sizes[order_id][1];
    }
    orders = &ctx->hf_orders;
    switch (order_id) {
    case 0:
    case 1:
        return orders->natural_8x8;
    case 2:
        return orders->natural_16x16;
    case 3:
        return orders->natural_32x32;
    case 4:
        return orders->natural_16x8;
    case 5:
        return orders->natural_32x8;
    case 6:
        return orders->natural_32x16;
    case 7:
        return orders->natural_64x64;
    case 8:
        return orders->natural_64x32;
    case 9:
        return orders->natural_128x128;
    default:
        if (len_out != NULL) {
            *len_out = 0;
        }
        return NULL;
    }
}

const jxl_coeff_order *jxl_hf_pass_dct8_natural_order(jxl_context *ctx, size_t *len_out) {
    return natural_order_lazy(ctx, 0, len_out);
}

void jxl_hf_pass_init(jxl_hf_pass *pass) {
    if (pass != NULL) {
        memset(pass, 0, sizeof(*pass));
    }
}

static void free_permutations(jxl_allocator_state *alloc, jxl_hf_pass *pass) {
    size_t i;
    for (i = 0; i < JXL_HF_PASS_ORDER_COUNT; ++i) {
        size_t c;
        for (c = 0; c < JXL_HF_PASS_CHANNELS; ++c) {
            jxl_free(alloc, pass->permutation[i][c]);
            pass->permutation[i][c] = NULL;
            pass->permutation_lens[i][c] = 0;
        }
    }
}

void jxl_hf_pass_destroy(jxl_allocator_state *alloc, jxl_hf_pass *pass) {
    if (pass == NULL) {
        return;
    }
    free_permutations(alloc, pass);
    if (pass->order_decoder != NULL) {
        jxl_coding_decoder_destroy(alloc, pass->order_decoder);
    }
    if (pass->hf_dist != NULL) {
        jxl_coding_decoder_destroy(alloc, pass->hf_dist);
    }
    pass->order_decoder = NULL;
    pass->hf_dist = NULL;
    pass->alloc = NULL;
    pass->ctx = NULL;
    jxl_hf_pass_init(pass);
}

jxl_vardct_status_t jxl_hf_pass_parse(jxl_context *ctx, jxl_allocator_state *alloc, jxl_bs *bs,
                                     const jxl_hf_pass_params *params, jxl_hf_pass *out) {
    uint32_t num_dist;
    jxl_coding_status_t cst;
    if (ctx == NULL || alloc == NULL || bs == NULL || params == NULL || out == NULL ||
        params->hf_block_ctx == NULL) {
        return JXL_VARDCT_BITSTREAM_ERROR;
    }
    jxl_hf_pass_destroy(alloc, out);
    jxl_hf_pass_init(out);
    out->alloc = alloc;
    out->ctx = ctx;

    JXL_VARDCT_TRY_BS(jxl_bs_read_u32(bs, k_used_orders, &out->used_orders));

    if (out->used_orders != 0) {
        size_t idx;
        jxl_coding_status_t cst = jxl_coding_decoder_parse(alloc, bs, 8, &out->order_decoder);
        if (jxl_vardct_from_coding(cst) != JXL_VARDCT_OK) {
            jxl_hf_pass_destroy(alloc, out);
            return jxl_vardct_from_coding(cst);
        }
        uint32_t used = out->used_orders;
        for (idx = 0; idx < JXL_HF_PASS_ORDER_COUNT; ++idx) {
            size_t bw = k_block_sizes[idx][0];
            size_t bh = k_block_sizes[idx][1];
            if (used & 1) {
                size_t ch;
                uint32_t size = (uint32_t)(bw * bh);
                uint32_t skip = size / 64;
                for (ch = 0; ch < JXL_HF_PASS_CHANNELS; ++ch) {
                    size_t pi;
                    size_t perm_len;
                    size_t nat_len;
                    size_t *perm = NULL;
                    const jxl_coeff_order *nat;
                    jxl_coeff_order *mapped;
                    perm_len = 0;
                    cst = jxl_coding_read_permutation(alloc, bs, out->order_decoder, size, skip,
                                                      &perm, &perm_len);
                    if (cst != JXL_CODING_OK) {
                        jxl_hf_pass_destroy(alloc, out);
                        return jxl_vardct_from_coding(cst);
                    }
                    nat_len = 0;
                    nat = natural_order_lazy(ctx, idx, &nat_len);
                    if (nat == NULL || perm_len > nat_len) {
                        jxl_coding_permutation_destroy(alloc, perm);
                        jxl_hf_pass_destroy(alloc, out);
                        return JXL_VARDCT_VALIDATION_ERROR;
                    }
                    mapped =
                        jxl_alloc(alloc, perm_len * sizeof(*mapped));
                    if (mapped == NULL) {
                        jxl_coding_permutation_destroy(alloc, perm);
                        jxl_hf_pass_destroy(alloc, out);
                        return JXL_VARDCT_OUT_OF_MEMORY;
                    }
                    for (pi = 0; pi < perm_len; ++pi) {
                        mapped[pi] = nat[perm[pi]];
                    }
                    jxl_coding_permutation_destroy(alloc, perm);
                    out->permutation[idx][ch] = mapped;
                    out->permutation_lens[idx][ch] = perm_len;
                }
            }
            used >>= 1;
        }

        cst = jxl_coding_decoder_finalize(out->order_decoder);
        if (jxl_vardct_from_coding(cst) != JXL_VARDCT_OK) {
            jxl_hf_pass_destroy(alloc, out);
            return jxl_vardct_from_coding(cst);
        }
    }

    num_dist =
        495u * params->num_hf_presets * params->hf_block_ctx->num_block_clusters;
    cst = jxl_coding_decoder_parse(alloc, bs, num_dist, &out->hf_dist);
    if (jxl_vardct_from_coding(cst) != JXL_VARDCT_OK) {
        jxl_hf_pass_destroy(alloc, out);
        return jxl_vardct_from_coding(cst);
    }

    return JXL_VARDCT_OK;
}

const jxl_coeff_order *jxl_hf_pass_order(const jxl_hf_pass *pass, size_t order_id, size_t channel,
                                         size_t *len_out) {
    if (pass == NULL || order_id >= JXL_HF_PASS_ORDER_COUNT || channel >= JXL_HF_PASS_CHANNELS) {
        if (len_out != NULL) {
            *len_out = 0;
        }
        return NULL;
    }
    if (pass->permutation_lens[order_id][channel] > 0) {
        if (len_out != NULL) {
            *len_out = pass->permutation_lens[order_id][channel];
        }
        return pass->permutation[order_id][channel];
    }
    return natural_order_lazy(pass->ctx, order_id, len_out);
}

jxl_coding_decoder *jxl_hf_pass_hf_dist(const jxl_hf_pass *pass) {
    return pass != NULL ? pass->hf_dist : NULL;
}

jxl_allocator_state *jxl_hf_pass_alloc(const jxl_hf_pass *pass) {
    return pass != NULL ? pass->alloc : NULL;
}
