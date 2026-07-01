// SPDX-License-Identifier: MIT OR Apache-2.0
#include "dequant_hf.h"

#include "frame/pass_group.h"
#include "render/vardct/varblocks.h"
#include "vardct/dequant_expand.h"
#include "vardct/dct_select.h"

#include <math.h>
#include "jxl_oxide/jxl_types.h"

static void dequant_varblock(jxl_context *ctx, jxl_subgrid_f32 coeff, jxl_transform_type dct_select,
                             int32_t hf_mul, size_t channel,
                             const jxl_hf_global_dequant *hf_global,
                             const jxl_frame_header *frame_header) {
                                 size_t y;
    uint32_t bw = 1;
    uint32_t bh = 1;
    size_t mat_len;
    float qm_scale;
    jxl_transform_dct_select_size(dct_select, &bw, &bh);
    size_t width = (size_t)bw * 8;
    size_t height = (size_t)bh * 8;
    const float *matrix = NULL;
    float mul;
    float quant_bias;
    float quant_bias_numerator;

    size_t matrix_idx = (size_t)jxl_transform_dequant_matrix_param_index(dct_select);
    mat_len = 0;
    if (jxl_transform_need_transpose(dct_select)) {
        matrix = jxl_dequant_matrix_weights_transposed(ctx, hf_global->dequant_matrices, matrix_idx,
                                                       channel, &mat_len);
    } else {
        matrix = jxl_dequant_matrix_weights(ctx, hf_global->dequant_matrices, matrix_idx, channel,
                                            &mat_len);
    }
    if (matrix == NULL || mat_len < width * height) {
        return;
    }

    qm_scale = 1.0f;
    if (channel == 0) {
        qm_scale = powf(0.8f, (float)((int)frame_header->x_qm_scale - 2));
    } else if (channel == 2) {
        qm_scale = powf(0.8f, (float)((int)frame_header->b_qm_scale - 2));
    }

    mul = 65536.0f / ((float)hf_global->quantizer->global_scale * (float)hf_mul) * qm_scale;
    quant_bias = hf_global->opsin_inverse->quant_bias[channel];
    quant_bias_numerator = hf_global->opsin_inverse->quant_bias_numerator;

    for (y = 0; y < height; ++y) {
        size_t x;
        float *row = coeff.data + y * coeff.stride;
        const float *matrix_row = matrix + y * width;
        for (x = 0; x < width; ++x) {
            float *q = &row[x];
            float qf = *q;
            if (fabsf(qf) <= 1.0f) {
                qf *= quant_bias;
            } else {
                qf -= quant_bias_numerator / qf;
            }
            qf *= matrix_row[x];
            qf *= mul;
            *q = qf;
        }
    }
}

typedef struct {
    jxl_context *ctx;
    jxl_subgrid_f32 coeff;
    const jxl_hf_global_dequant *hf_global;
    const jxl_frame_header *frame_header;
    size_t channel;
} dequant_ctx;

static void dequant_varblock_cb(const jxl_varblock_info *info, void *ctx_void) {
    uint32_t bw;
    uint32_t bh;
    dequant_ctx *dq = (dequant_ctx *)ctx_void;
    size_t left;
    size_t top;
    jxl_subgrid_f32 block;
    bw = 1;
    bh = 1;
    jxl_transform_dct_select_size(info->dct_select, &bw, &bh);
    left = info->shifted_bx * 8;
    top = info->shifted_by * 8;
    block = jxl_subgrid_f32_sub(dq->coeff, left, top, (size_t)bw * 8, (size_t)bh * 8);
    dequant_varblock(dq->ctx, block, info->dct_select, info->hf_mul, dq->channel, dq->hf_global,
                     dq->frame_header);
}

void jxl_dequant_hf_varblock_grouped(jxl_context *library_ctx, jxl_subgrid_f32 out[3],
                                     uint32_t group_idx, const jxl_frame_header *frame_header,
                                     const jxl_hf_global_dequant *hf_global,
                                     const jxl_lf_group_view *lf_group,
                                     const jxl_modular_region *lf_region) {
    size_t channel;
    jxl_block_info_subgrid block_info;
    if (library_ctx == NULL || out == NULL || frame_header == NULL || hf_global == NULL ||
        lf_group == NULL || hf_global->dequant_matrices == NULL || hf_global->quantizer == NULL ||
        hf_global->opsin_inverse == NULL) {
        return;
    }

    if (!jxl_pass_group_block_info_subgrid(frame_header, group_idx, lf_group, lf_region,
                                         &block_info)) {
        return;
    }

    for (channel = 0; channel < 3; ++channel) {
        jxl_channel_shift shift =
            jxl_channel_shift_from_jpeg_upsampling(frame_header->jpeg_upsampling, channel);
        dequant_ctx dq;
        dq.ctx = library_ctx;
        dq.coeff = out[channel];
        dq.hf_global = hf_global;
        dq.frame_header = frame_header;
        dq.channel = channel;

        jxl_for_each_varblocks(block_info, shift, dequant_varblock_cb, &dq);
    }
}
