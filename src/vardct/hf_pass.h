// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_VARDCT_HF_PASS_H_
#define JXL_VARDCT_HF_PASS_H_

#include "allocator.h"
#include "bitstream/bitstream.h"
#include "coding/decoder.h"
#include "vardct/error.h"
#include "vardct/lf.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct jxl_context jxl_context;

#define JXL_HF_PASS_ORDER_COUNT 13
#define JXL_HF_PASS_CHANNELS 3

typedef struct {
    uint16_t x;
    uint16_t y;
} jxl_coeff_order;

typedef struct {
    uint32_t used_orders;
    jxl_coeff_order *permutation[JXL_HF_PASS_ORDER_COUNT][JXL_HF_PASS_CHANNELS];
    size_t permutation_lens[JXL_HF_PASS_ORDER_COUNT][JXL_HF_PASS_CHANNELS];
    jxl_coding_decoder *order_decoder;
    jxl_coding_decoder *hf_dist;
    jxl_allocator_state *alloc;
    jxl_context *ctx;
} jxl_hf_pass;

typedef struct {
    const jxl_hf_block_context *hf_block_ctx;
    uint32_t num_hf_presets;
} jxl_hf_pass_params;

void jxl_hf_pass_init(jxl_hf_pass *pass);
void jxl_hf_pass_destroy(jxl_allocator_state *alloc, jxl_hf_pass *pass);

jxl_vardct_status_t jxl_hf_pass_parse(jxl_context *ctx, jxl_allocator_state *alloc, jxl_bs *bs,
                                      const jxl_hf_pass_params *params, jxl_hf_pass *out);

const jxl_coeff_order *jxl_hf_pass_order(const jxl_hf_pass *pass, size_t order_id, size_t channel,
                                         size_t *len_out);

const jxl_coeff_order *jxl_hf_pass_dct8_natural_order(jxl_context *ctx, size_t *len_out);

jxl_coding_decoder *jxl_hf_pass_hf_dist(const jxl_hf_pass *pass);

jxl_allocator_state *jxl_hf_pass_alloc(const jxl_hf_pass *pass);

#endif /* JXL_VARDCT_HF_PASS_H_ */
