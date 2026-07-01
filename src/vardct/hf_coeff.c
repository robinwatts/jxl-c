// SPDX-License-Identifier: MIT OR Apache-2.0
#include "hf_coeff.h"

#include "bitstream/unpack.h"
#include "coding/decoder.h"
#include "vardct/dct_select.h"
#include "vardct/util.h"

#include <stdio.h>
#include <string.h>

static const uint32_t k_coeff_freq_context[63] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 16, 16, 17, 17, 18, 18, 19, 19,
    20, 20, 21, 21, 22, 22, 23, 23, 23, 23, 24, 24, 24, 24, 25, 25, 25, 25, 26, 26, 26, 26, 27,
    27, 27, 27, 28, 28, 28, 28, 29, 29, 29, 29, 30, 30, 30, 30,
};

static const uint32_t k_coeff_num_nonzero_context[63] = {
    0, 31, 62, 62, 93, 93, 93, 93, 123, 123, 123, 123, 152, 152, 152, 152, 152, 152, 152, 152,
    180, 180, 180, 180, 180, 180, 180, 180, 180, 180, 180, 180, 206, 206, 206, 206, 206, 206,
    206, 206, 206, 206, 206, 206, 206, 206, 206, 206, 206, 206, 206, 206, 206, 206, 206, 206,
    206, 206, 206, 206, 206, 206, 206,
};

static const size_t k_channel_map[3] = {1, 0, 2};

static size_t next_power_of_two_u32(uint32_t n) {
    uint32_t p = 1;
    while (p < n) {
        p <<= 1;
    }
    return (size_t)p;
}

static size_t trailing_zeros_u32(uint32_t n) {
    size_t c;
    if (n == 0) {
        return 0;
    }
#if defined(__GNUC__) || defined(__clang__)
    return (size_t)__builtin_ctz(n);
#else
    c = 0;
    while ((n & 1u) == 0) {
        ++c;
        n >>= 1;
    }
    return c;
#endif
}

static const jxl_block_info *block_at(jxl_block_info_subgrid sg, size_t x, size_t y) {
    return &sg.data[y * sg.stride + x];
}

static void subgrid_i32_add(jxl_subgrid_i32 sg, size_t x, size_t y, int32_t v) {
    sg.data[y * sg.stride + x] += v;
}

static int32_t lf_quant_get(const jxl_lf_quant_subgrid_u32 *lf, size_t c, size_t x, size_t y) {
    return jxl_lf_quant_subgrid_sample(&lf[c], x, y);
}

int32_t jxl_lf_quant_subgrid_sample(const jxl_lf_quant_subgrid_u32 *q, size_t x, size_t y) {
    size_t idx;
    size_t st;
    if (q == NULL || q->data == NULL || x >= q->width || y >= q->height) {
        return 0;
    }
    st = q->stride != 0 ? q->stride : q->width;
    idx = y * st + x;
    if (q->kind == JXL_MODULAR_SAMPLE_I16) {
        return (int32_t)((const int16_t *)q->data)[idx];
    }
    return (int32_t)((const int32_t *)q->data)[idx];
}

jxl_vardct_status_t jxl_write_hf_coeff(jxl_bs *bs, const jxl_hf_coeff_params *params,
                                       jxl_subgrid_i32 hf_coeff_out[3]) {
    size_t c;
    size_t idx;
    size_t y;
    size_t lf_idx_mul;
    size_t hf_idx_mul;
    jxl_channel_shift upsampling_shifts[3];
    int32_t hshifts[3];
    int32_t vshifts[3];
    uint32_t hfp;
    size_t ctx_size;
    size_t cluster_map_len;
    size_t non_zeros_lengths[3];
    size_t dbg_data_blocks;
    size_t dbg_ch_reads;
    size_t dbg_nz_positive;
    size_t dbg_coeff_reads;
    size_t dbg_order_exhausted;
    const jxl_hf_block_context *hf_block_ctx;
    jxl_allocator_state *alloc;
    const jxl_coding_decoder *hf_dist;
    jxl_coding_decoder *dist;
    jxl_coding_status_t cst;
    const uint32_t *qf_thresholds;
    size_t qf_count;
    uint32_t num_block_clusters;
    const uint8_t *block_ctx_map;
    size_t block_ctx_map_len;
    uint32_t np2;
    size_t hfp_bits;
    size_t cluster_map_off;
    const uint8_t *full_map;
    const uint8_t *cluster_map;
    size_t width;
    size_t height;
    uint32_t *non_zeros_row[3];
    jxl_vardct_status_t result;
    int dbg_on;
    if (bs == NULL || params == NULL || hf_coeff_out == NULL || params->hf_block_ctx == NULL ||
        params->hf_pass == NULL) {
        return JXL_VARDCT_BITSTREAM_ERROR;
    }

    hf_block_ctx = params->hf_block_ctx;
    alloc = jxl_hf_pass_alloc(params->hf_pass);
    hf_dist = jxl_hf_pass_hf_dist(params->hf_pass);
    if (alloc == NULL || hf_dist == NULL) {
        return JXL_VARDCT_DECODER_ERROR;
    }
    dist = NULL;
    cst = jxl_coding_decoder_clone(alloc, hf_dist, &dist);
    if (cst != JXL_CODING_OK || dist == NULL) {
        return jxl_vardct_from_coding(cst);
    }
    jxl_coding_decoder_attach_context(dist, params->ctx);

    qf_thresholds = hf_block_ctx->qf_thresholds;
    qf_count = hf_block_ctx->qf_threshold_count;
    num_block_clusters = hf_block_ctx->num_block_clusters;
    block_ctx_map = hf_block_ctx->block_ctx_map;
    block_ctx_map_len = hf_block_ctx->block_ctx_map_len;

    lf_idx_mul = 1;
    for (c = 0; c < 3; ++c) {
        lf_idx_mul *= hf_block_ctx->lf_threshold_counts[c] + 1;
    }
    hf_idx_mul = qf_count + 1;

    for (idx = 0; idx < 3; ++idx) {
        upsampling_shifts[idx] =
            jxl_channel_shift_from_jpeg_upsampling(params->jpeg_upsampling, idx);
        hshifts[idx] = jxl_channel_shift_hshift(&upsampling_shifts[idx]);
        vshifts[idx] = jxl_channel_shift_vshift(&upsampling_shifts[idx]);
    }

    np2 = (uint32_t)next_power_of_two_u32(params->num_hf_presets);
    hfp_bits = trailing_zeros_u32(np2);
    hfp = 0;
    if (hfp_bits > 0) {
        jxl_bs_status_t st = jxl_bs_read_bits(bs, hfp_bits, &hfp);
        if (st != JXL_BS_OK) {
            return jxl_vardct_from_bs(st);
        }
    }
    if (hfp >= params->num_hf_presets) {
        return JXL_VARDCT_BITSTREAM_ERROR;
    }

    ctx_size = 495 * num_block_clusters;
    cluster_map_off = ctx_size * (size_t)hfp;
    cluster_map_len = 0;
    full_map = jxl_coding_decoder_cluster_map(dist, &cluster_map_len);
    if (full_map == NULL || cluster_map_off + ctx_size > cluster_map_len) {
        return JXL_VARDCT_DECODER_ERROR;
    }
    cluster_map = full_map + cluster_map_off;

    cst = jxl_coding_decoder_begin(dist, bs);
    if (cst != JXL_CODING_OK) {
        jxl_coding_decoder_destroy(alloc, dist);
        return jxl_vardct_from_coding(cst);
    }
    if (JXL_DEBUG_FLAG(params->ctx, debug_hf_trace)) {
        fprintf(stderr, "c hf begin bits=%zu\n", bs->num_read_bits);
    }

    width = params->block_info.width;
    height = params->block_info.height;

    for (c = 0; c < 3; ++c) {
        uint32_t w = (uint32_t)width;
        uint32_t h = (uint32_t)height;
        jxl_channel_shift_shift_size(&upsampling_shifts[c], w, h, &w, &h);
        non_zeros_lengths[c] = (size_t)w;
    }

    for (c = 0; c < 3; ++c) {
        non_zeros_row[c] = (uint32_t *)jxl_calloc(alloc, non_zeros_lengths[c], sizeof(uint32_t));
        if (non_zeros_lengths[c] > 0 && non_zeros_row[c] == NULL) {
            size_t i;
            for (i = 0; i < c; ++i) {
                jxl_free(alloc, non_zeros_row[i]);
            }
            jxl_coding_decoder_finalize(dist);
            jxl_coding_decoder_destroy(alloc, dist);
            return JXL_VARDCT_BITSTREAM_ERROR;
        }
    }

    result = JXL_VARDCT_OK;
    dbg_on = JXL_DEBUG_FLAG(params->ctx, debug_hf_coeff);
    dbg_data_blocks = 0;
    dbg_ch_reads = 0;
    dbg_nz_positive = 0;
    dbg_coeff_reads = 0;
    dbg_order_exhausted = 0;

    for (y = 0; y < height && result == JXL_VARDCT_OK; ++y) {
        size_t x;
        for (x = 0; x < width && result == JXL_VARDCT_OK; ++x) {
            size_t ti;
            size_t c;
            uint32_t bw;
            uint32_t bh;
            uint32_t num_blocks;
            size_t lf_idx;
            size_t hf_idx;
            const jxl_block_info *info = block_at(params->block_info, x, y);
            jxl_transform_type dct_select;
            int32_t qf;
            size_t num_blocks_log;
            uint32_t order_id;
            if (info->kind != JXL_BLOCK_INFO_DATA) {
                continue;
            }
            if (dbg_on) {
                ++dbg_data_blocks;
            }

            dct_select = info->dct_select;
            qf = info->hf_mul;

            bw = 1;
            bh = 1;
            jxl_transform_dct_select_size(dct_select, &bw, &bh);
            num_blocks = bw * bh;
            num_blocks_log = trailing_zeros_u32(num_blocks);
            order_id = jxl_transform_order_id(dct_select);

            lf_idx = 0;
            if (params->lf_quant != NULL) {
                size_t ci;
                static const size_t lf_ch[3] = {0, 2, 1};
                for (ci = 0; ci < 3; ++ci) {
                    size_t ti;
                    c = lf_ch[ci];
                    const int32_t *thresholds = hf_block_ctx->lf_thresholds[c];
                    size_t th_count = hf_block_ctx->lf_threshold_counts[c];
                    lf_idx *= th_count + 1;

                    size_t lx = x >> (size_t)hshifts[c];
                    size_t ly = y >> (size_t)vshifts[c];
                    int32_t q = lf_quant_get(params->lf_quant, c, lx, ly);
                    for (ti = 0; ti < th_count; ++ti) {
                        if (q > thresholds[ti]) {
                            ++lf_idx;
                        }
                    }
                }
            }

            hf_idx = 0;
            for (ti = 0; ti < qf_count; ++ti) {
                if (qf > (int32_t)qf_thresholds[ti]) {
                    ++hf_idx;
                }
            }

            for (c = 0; c < 3 && result == JXL_VARDCT_OK; ++c) {
                size_t dx;
                size_t oi;
                size_t cx;
                size_t cy;
                size_t ch = k_channel_map[c];
                size_t ch_idx = c * 13 + (size_t)order_id;
                uint16_t ord_x;
                uint16_t ord_y;

                int32_t hshift = hshifts[ch];
                int32_t vshift = vshifts[ch];
                size_t sx = x >> (size_t)hshift;
                size_t sy = y >> (size_t)vshift;
                uint32_t predicted;
                uint32_t non_zeros;
                size_t order_len;
                uint32_t block_ctx;
                size_t nz_ctx_idx;
                size_t non_zeros_ctx;
                uint32_t non_zeros_val;
                jxl_subgrid_i32 coeff_grid;
                uint32_t is_prev_coeff_nonzero;
                const jxl_coeff_order *order;
                size_t coeff_ctx_base;
                const uint8_t *coeff_cluster_map;
                if (hshift != 0 || vshift != 0) {
                    if ((sx << (size_t)hshift) != x || (sy << (size_t)vshift) != y) {
                        continue;
                    }
                    if (block_at(params->block_info, sx, sy)->kind != JXL_BLOCK_INFO_DATA) {
                        continue;
                    }
                }

                idx = (ch_idx * hf_idx_mul + hf_idx) * lf_idx_mul + lf_idx;
                if (idx >= block_ctx_map_len) {
                    result = JXL_VARDCT_BITSTREAM_ERROR;
                    break;
                }
                block_ctx = block_ctx_map[idx];

                if (sy == 0) {
                    predicted = sx == 0 ? 32u : non_zeros_row[ch][sx - 1];
                } else if (sx == 0) {
                    predicted = non_zeros_row[ch][sx];
                } else {
                    predicted = (non_zeros_row[ch][sx] + non_zeros_row[ch][sx - 1] + 1) >> 1;
                }

                nz_ctx_idx = predicted >= 8 ? (size_t)(4 + predicted / 2) : (size_t)predicted;
                non_zeros_ctx = (size_t)block_ctx + nz_ctx_idx * num_block_clusters;
                if (non_zeros_ctx >= ctx_size) {
                    result = JXL_VARDCT_DECODER_ERROR;
                    break;
                }

                non_zeros = 0;
                if (dbg_on) {
                    ++dbg_ch_reads;
                }
                cst = jxl_coding_decoder_read_varint_clustered(dist, bs, cluster_map[non_zeros_ctx], 0,
                                                             &non_zeros);
                if (cst != JXL_CODING_OK) {
                    result = jxl_vardct_from_coding(cst);
                    break;
                }
                if (JXL_DEBUG_FLAG(params->ctx, debug_hf_trace)) {
                    uint8_t cluster = cluster_map[non_zeros_ctx];
                    fprintf(stderr,
                            "c nz x=%zu y=%zu ch=%zu nz=%u qf=%d oid=%u hf=%zu lf=%zu bctx=%u "
                            "pred=%u nctx=%zu cl=%u bits=%zu\n",
                            x, y, ch, non_zeros, qf, order_id, hf_idx, lf_idx, block_ctx, predicted,
                            non_zeros_ctx, (unsigned)cluster, bs->num_read_bits);
                }
                if (non_zeros > (63u << num_blocks_log)) {
                    result = JXL_VARDCT_BITSTREAM_ERROR;
                    break;
                }

                non_zeros_val = (non_zeros + num_blocks - 1) >> num_blocks_log;
                for (dx = 0; dx < (size_t)bw; ++dx) {
                    if (sx + dx < non_zeros_lengths[ch]) {
                        non_zeros_row[ch][sx + dx] = non_zeros_val;
                    }
                }
                if (non_zeros == 0) {
                    continue;
                }
                if (dbg_on) {
                    ++dbg_nz_positive;
                }

                coeff_grid = hf_coeff_out[ch];
                is_prev_coeff_nonzero = non_zeros <= num_blocks * 4 ? 1u : 0u;

                order_len = 0;
                order  =
                    jxl_hf_pass_order(params->hf_pass, (size_t)order_id, ch, &order_len);
                if (order == NULL || order_len < (size_t)num_blocks) {
                    result = JXL_VARDCT_DECODER_ERROR;
                    break;
                }

                coeff_ctx_base = (size_t)block_ctx * 458 + 37 * num_block_clusters;
                if (coeff_ctx_base + 458 > ctx_size) {
                    result = JXL_VARDCT_DECODER_ERROR;
                    break;
                }
                coeff_cluster_map = cluster_map + coeff_ctx_base;

                for (oi = (size_t)num_blocks; oi < order_len && non_zeros > 0; ++oi) {
                    size_t coeff_idx = oi - (size_t)num_blocks;
                    uint32_t nz = (non_zeros - 1) >> num_blocks_log;
                    size_t freq_idx = coeff_idx >> num_blocks_log;
                    uint32_t ucoeff;
                    size_t coeff_ctx;
                    uint8_t cluster;
                    int32_t coeff;
                    if (nz >= 63 || freq_idx >= 63) {
                        result = JXL_VARDCT_BITSTREAM_ERROR;
                        break;
                    }
                    coeff_ctx = (size_t)(k_coeff_num_nonzero_context[nz] +
                                                k_coeff_freq_context[freq_idx]) *
                                           2 +
                                       (size_t)is_prev_coeff_nonzero;
                    if (coeff_ctx >= 458) {
                        result = JXL_VARDCT_BITSTREAM_ERROR;
                        break;
                    }
                    cluster = coeff_cluster_map[coeff_ctx];

                    ucoeff = 0;
                    if (dbg_on) {
                        ++dbg_coeff_reads;
                    }
                    cst = jxl_coding_decoder_read_varint_clustered(dist, bs, cluster, 0, &ucoeff);
                    if (cst != JXL_CODING_OK) {
                        result = jxl_vardct_from_coding(cst);
                        break;
                    }
                    if (ucoeff == 0) {
                        is_prev_coeff_nonzero = 0;
                        continue;
                    }

                    coeff = jxl_unpack_signed(ucoeff) << (int32_t)params->coeff_shift;
                    ord_x = order[oi].x;
                    ord_y = order[oi].y;
                    if (jxl_transform_need_transpose(dct_select)) {
                        uint16_t t = ord_x;
                        ord_x = ord_y;
                        ord_y = t;
                    }
                    cx = sx * 8 + (size_t)ord_x;
                    cy = sy * 8 + (size_t)ord_y;
                    if (cx < coeff_grid.width && cy < coeff_grid.height) {
                        subgrid_i32_add(coeff_grid, cx, cy, coeff);
                    }
                    is_prev_coeff_nonzero = 1;
                    --non_zeros;
                }
                if (dbg_on && non_zeros > 0) {
                    ++dbg_order_exhausted;
                    fprintf(stderr,
                            "order_ex x=%zu y=%zu ch=%zu nz_left=%u order_len=%zu num_blocks=%u\n",
                            x, y, ch, non_zeros, order_len, num_blocks);
                }
            }
        }
    }

    for (c = 0; c < 3; ++c) {
        jxl_free(alloc, non_zeros_row[c]);
    }

    cst = jxl_coding_decoder_finalize(dist);
    if (dbg_on) {
        size_t rem_bits = bs != NULL ? bs->bytes_len * 8 + bs->remaining_buf_bits : 0;
        fprintf(stderr,
                "hf_coeff done result=%d cst=%d bits=%zu rem=%zu blocks=%zu ch_reads=%zu "
                "nz_pos=%zu coeff_reads=%zu order_ex=%zu %zux%zu lf_quant=%d shift=%u\n",
                (int)result, (int)cst, bs != NULL ? bs->num_read_bits : 0, rem_bits, dbg_data_blocks,
                dbg_ch_reads, dbg_nz_positive, dbg_coeff_reads, dbg_order_exhausted, width, height,
                params->lf_quant != NULL, params->coeff_shift);
    }
    if (result == JXL_VARDCT_OK && cst != JXL_CODING_OK) {
        result = jxl_vardct_from_coding(cst);
    }

    jxl_coding_decoder_destroy(alloc, dist);
    return result;
}
