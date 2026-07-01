// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_VARDCT_LF_H_
#define JXL_VARDCT_LF_H_

#include "allocator.h"
#include "bitstream/bitstream.h"
#include "vardct/error.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct {
    float m_x_lf;
    float m_y_lf;
    float m_b_lf;
} jxl_lf_channel_dequant;

typedef struct {
    uint32_t global_scale;
    uint32_t quant_lf;
} jxl_quantizer;

typedef struct {
    uint32_t colour_factor;
    float base_correlation_x;
    float base_correlation_b;
    uint32_t x_factor_lf;
    uint32_t b_factor_lf;
} jxl_lf_channel_correlation;

typedef struct {
    uint32_t *qf_thresholds;
    size_t qf_threshold_count;
    int32_t lf_thresholds[3][32];
    size_t lf_threshold_counts[3];
    uint8_t *block_ctx_map;
    size_t block_ctx_map_len;
    uint32_t num_block_clusters;
} jxl_hf_block_context;

jxl_vardct_status_t jxl_lf_channel_dequant_parse(jxl_bs *bs, jxl_lf_channel_dequant *out);
jxl_vardct_status_t jxl_quantizer_parse(jxl_bs *bs, jxl_quantizer *out);
jxl_vardct_status_t jxl_lf_channel_correlation_parse(jxl_bs *bs, jxl_lf_channel_correlation *out);
jxl_vardct_status_t jxl_hf_block_context_parse(jxl_allocator_state *alloc, jxl_bs *bs,
                                               jxl_hf_block_context *out);

void jxl_hf_block_context_free(jxl_allocator_state *alloc, jxl_hf_block_context *ctx);

float jxl_lf_channel_dequant_m_x_unscaled(const jxl_lf_channel_dequant *d);
float jxl_lf_channel_dequant_m_y_unscaled(const jxl_lf_channel_dequant *d);
float jxl_lf_channel_dequant_m_b_unscaled(const jxl_lf_channel_dequant *d);

#endif /* JXL_VARDCT_LF_H_ */
