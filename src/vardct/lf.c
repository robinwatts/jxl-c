// SPDX-License-Identifier: MIT OR Apache-2.0
#include "lf.h"

#include "bitstream/unpack.h"
#include "coding/coding.h"
#include "vardct/util.h"

#include <string.h>

static const jxl_u32_spec k_quantizer_global_scale[4] = {
    JXL_U32_BITS(1, 11), JXL_U32_BITS(2049, 11), JXL_U32_BITS(4097, 12), JXL_U32_BITS(8193, 16)};

static const jxl_u32_spec k_quantizer_quant_lf[4] = {JXL_U32_C(16), JXL_U32_BITS(1, 5),
                                                     JXL_U32_BITS(1, 8), JXL_U32_BITS(1, 16)};

static const jxl_u32_spec k_correlation_colour_factor[4] = {JXL_U32_C(84), JXL_U32_C(256),
                                                            JXL_U32_BITS(2, 8),
                                                            JXL_U32_BITS(258, 16)};

static const jxl_u32_spec k_lf_threshold[4] = {JXL_U32_BITS(0, 4), JXL_U32_BITS(16, 8),
                                               JXL_U32_BITS(272, 16), JXL_U32_BITS(65808, 32)};

static const jxl_u32_spec k_qf_threshold[4] = {JXL_U32_BITS(0, 2), JXL_U32_BITS(4, 3),
                                               JXL_U32_BITS(12, 5), JXL_U32_BITS(44, 8)};

static const uint8_t k_default_block_ctx_map[39] = {
    0, 1, 2, 2, 3, 3, 4, 5, 6, 6, 6, 6, 6, 7, 8, 9, 9, 10, 11, 12, 13, 14, 14, 14,
    14, 14, 7, 8, 9, 9, 10, 11, 12, 13, 14, 14, 14, 14, 14,
};

jxl_vardct_status_t jxl_lf_channel_dequant_parse(jxl_bs *bs, jxl_lf_channel_dequant *out) {
    int all_default;
    if (out == NULL) {
        return JXL_VARDCT_BITSTREAM_ERROR;
    }
    all_default = 1;
    JXL_VARDCT_TRY_BS(jxl_bs_read_bool(bs, &all_default));
    if (all_default) {
        out->m_x_lf = 1.0f / 32.0f;
        out->m_y_lf = 1.0f / 4.0f;
        out->m_b_lf = 1.0f / 2.0f;
        return JXL_VARDCT_OK;
    }
    JXL_VARDCT_TRY_BS(jxl_bs_read_f16_as_f32(bs, &out->m_x_lf));
    JXL_VARDCT_TRY_BS(jxl_bs_read_f16_as_f32(bs, &out->m_y_lf));
    JXL_VARDCT_TRY_BS(jxl_bs_read_f16_as_f32(bs, &out->m_b_lf));
    return JXL_VARDCT_OK;
}

jxl_vardct_status_t jxl_quantizer_parse(jxl_bs *bs, jxl_quantizer *out) {
    if (out == NULL) {
        return JXL_VARDCT_BITSTREAM_ERROR;
    }
    JXL_VARDCT_TRY_BS(jxl_bs_read_u32(bs, k_quantizer_global_scale, &out->global_scale));
    JXL_VARDCT_TRY_BS(jxl_bs_read_u32(bs, k_quantizer_quant_lf, &out->quant_lf));
    return JXL_VARDCT_OK;
}

jxl_vardct_status_t jxl_lf_channel_correlation_parse(jxl_bs *bs, jxl_lf_channel_correlation *out) {
    int all_default;
    uint32_t bits;
    if (out == NULL) {
        return JXL_VARDCT_BITSTREAM_ERROR;
    }
    all_default = 1;
    JXL_VARDCT_TRY_BS(jxl_bs_read_bool(bs, &all_default));
    if (all_default) {
        out->colour_factor = 84;
        out->base_correlation_x = 0.0f;
        out->base_correlation_b = 1.0f;
        out->x_factor_lf = 128;
        out->b_factor_lf = 128;
        return JXL_VARDCT_OK;
    }
    JXL_VARDCT_TRY_BS(jxl_bs_read_u32(bs, k_correlation_colour_factor, &out->colour_factor));
    JXL_VARDCT_TRY_BS(jxl_bs_read_f16_as_f32(bs, &out->base_correlation_x));
    JXL_VARDCT_TRY_BS(jxl_bs_read_f16_as_f32(bs, &out->base_correlation_b));
    bits = 0;
    JXL_VARDCT_TRY_BS(jxl_bs_read_bits(bs, 8, &bits));
    out->x_factor_lf = bits;
    JXL_VARDCT_TRY_BS(jxl_bs_read_bits(bs, 8, &bits));
    out->b_factor_lf = bits;
    return JXL_VARDCT_OK;
}

jxl_vardct_status_t jxl_hf_block_context_parse(jxl_allocator_state *alloc, jxl_bs *bs,
                                               jxl_hf_block_context *out) {
                                                   size_t ch;
    int use_default;
    size_t bsize;
    uint32_t num_qf;
    uint32_t num_clusters;
    size_t clusters_len;
    uint8_t *clusters;
    jxl_coding_status_t cst;
    if (alloc == NULL || out == NULL) {
        return JXL_VARDCT_BITSTREAM_ERROR;
    }
    jxl_hf_block_context_free(alloc, out);
    memset(out, 0, sizeof(*out));
    use_default = 0;
    JXL_VARDCT_TRY_BS(jxl_bs_read_bool(bs, &use_default));
    if (use_default) {
        out->num_block_clusters = 15;
        out->block_ctx_map_len = sizeof(k_default_block_ctx_map);
        out->block_ctx_map = jxl_alloc(alloc, sizeof(k_default_block_ctx_map));
        if (out->block_ctx_map == NULL) {
            return JXL_VARDCT_OUT_OF_MEMORY;
        }
        memcpy(out->block_ctx_map, k_default_block_ctx_map, sizeof(k_default_block_ctx_map));
        return JXL_VARDCT_OK;
    }

    bsize = 1;
    for (ch = 0; ch < 3; ++ch) {
        size_t i;
        uint32_t num_lf = 0;
        JXL_VARDCT_TRY_BS(jxl_bs_read_bits(bs, 4, &num_lf));
        if (bsize > SIZE_MAX / (num_lf + 1)) {
            return JXL_VARDCT_BITSTREAM_ERROR;
        }
        bsize *= (size_t)(num_lf + 1);
        out->lf_threshold_counts[ch] = num_lf;
        for (i = 0; i < num_lf; ++i) {
            uint32_t raw = 0;
            JXL_VARDCT_TRY_BS(jxl_bs_read_u32(bs, k_lf_threshold, &raw));
            out->lf_thresholds[ch][i] = jxl_unpack_signed(raw);
        }
    }

    num_qf = 0;
    JXL_VARDCT_TRY_BS(jxl_bs_read_bits(bs, 4, &num_qf));
    if (bsize > SIZE_MAX / (num_qf + 1)) {
        return JXL_VARDCT_BITSTREAM_ERROR;
    }
    bsize *= (size_t)(num_qf + 1);
    out->qf_threshold_count = num_qf;
    if (num_qf > 0) {
        size_t i;
        out->qf_thresholds = jxl_calloc(alloc, num_qf, sizeof(uint32_t));
        if (out->qf_thresholds == NULL) {
            return JXL_VARDCT_OUT_OF_MEMORY;
        }
        for (i = 0; i < num_qf; ++i) {
            uint32_t t = 0;
            JXL_VARDCT_TRY_BS(jxl_bs_read_u32(bs, k_qf_threshold, &t));
            out->qf_thresholds[i] = 1 + t;
        }
    }

    if (bsize > 64) {
        return JXL_VARDCT_BITSTREAM_ERROR;
    }

    num_clusters = 0;
    clusters = NULL;
    clusters_len = 0;
    cst = jxl_coding_read_clusters(alloc, bs, (uint32_t)(bsize * 39),
                                   &num_clusters, &clusters, &clusters_len);
    if (cst != JXL_CODING_OK) {
        jxl_free(alloc, out->qf_thresholds);
        out->qf_thresholds = NULL;
        return JXL_VARDCT_DECODER_ERROR;
    }
    if (num_clusters > 16) {
        jxl_free(alloc, clusters);
        jxl_free(alloc, out->qf_thresholds);
        out->qf_thresholds = NULL;
        return JXL_VARDCT_BITSTREAM_ERROR;
    }
    out->num_block_clusters = num_clusters;
    out->block_ctx_map_len = clusters_len;
    out->block_ctx_map = jxl_alloc(alloc, clusters_len);
    if (out->block_ctx_map == NULL) {
        jxl_free(alloc, clusters);
        jxl_free(alloc, out->qf_thresholds);
        out->qf_thresholds = NULL;
        return JXL_VARDCT_OUT_OF_MEMORY;
    }
    memcpy(out->block_ctx_map, clusters, clusters_len);
    jxl_free(alloc, clusters);
    return JXL_VARDCT_OK;
}

void jxl_hf_block_context_free(jxl_allocator_state *alloc, jxl_hf_block_context *ctx) {
    if (ctx == NULL) {
        return;
    }
    jxl_free(alloc, ctx->qf_thresholds);
    ctx->qf_thresholds = NULL;
    ctx->qf_threshold_count = 0;
    jxl_free(alloc, ctx->block_ctx_map);
    ctx->block_ctx_map = NULL;
    ctx->block_ctx_map_len = 0;
}

float jxl_lf_channel_dequant_m_x_unscaled(const jxl_lf_channel_dequant *d) {
    return d != NULL ? d->m_x_lf / 128.0f : 0.0f;
}

float jxl_lf_channel_dequant_m_y_unscaled(const jxl_lf_channel_dequant *d) {
    return d != NULL ? d->m_y_lf / 128.0f : 0.0f;
}

float jxl_lf_channel_dequant_m_b_unscaled(const jxl_lf_channel_dequant *d) {
    return d != NULL ? d->m_b_lf / 128.0f : 0.0f;
}
