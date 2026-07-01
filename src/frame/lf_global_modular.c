// SPDX-License-Identifier: MIT OR Apache-2.0
#include "lf_global_modular.h"

#include "frame/frame_header.h"
#include "frame/lf_global.h"
#include "modular/modular.h"
#include "modular/prepare_subimage.h"
#include "modular/subimage_decode.h"
#include "vardct/lf.h"

#include <string.h>

static jxl_frame_status_t modular_to_frame(jxl_modular_status_t st) {
    switch (st) {
    case JXL_MODULAR_OK:
        return JXL_FRAME_OK;
    case JXL_MODULAR_OUT_OF_MEMORY:
        return JXL_FRAME_OUT_OF_MEMORY;
    case JXL_MODULAR_BITSTREAM_ERROR:
        return JXL_FRAME_BITSTREAM_ERROR;
    default:
        return JXL_FRAME_DECODER_ERROR;
    }
}

jxl_frame_status_t jxl_lf_global_modular_consume(jxl_allocator_state *alloc, jxl_bs *bs,
                                               const jxl_lf_global_modular_params *params,
                                               jxl_ma_config *global_ma, int *has_global_ma_out) {
    jxl_lf_channel_dequant dequant;
    int has_ma;
    jxl_modular_params mod_params;
    jxl_modular_image_destination gdest;
    jxl_modular_parse_ctx ctx = {0};
    jxl_frame_status_t pst;
    jxl_modular_status_t st;
    if (alloc == NULL || bs == NULL || params == NULL || params->image == NULL ||
        params->frame == NULL || global_ma == NULL || has_global_ma_out == NULL) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }
    if (params->frame->encoding != JXL_FRAME_ENCODING_MODULAR) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }
    pst =
        jxl_lf_global_parse_prefix(alloc, bs, params->image, params->frame, NULL, NULL, NULL);
    if (pst != JXL_FRAME_OK) {
        return pst;
    }

    if (jxl_lf_channel_dequant_parse(bs, &dequant) != JXL_VARDCT_OK) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }

    has_ma = 0;
    if (jxl_bs_read_bool(bs, &has_ma) != JXL_BS_OK) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }
    *has_global_ma_out = has_ma;

    if (has_ma) {
        jxl_ma_config_params ma_params = {0};
        uint64_t num_ch = (uint64_t)params->frame->encoded_color_channels +
                          (uint64_t)params->image->num_extra_channels;
        uint64_t samples =
            (uint64_t)params->frame->width * (uint64_t)params->frame->height * num_ch / 16u;
        size_t node_limit = (size_t)(1024u + samples);
        if (node_limit > (1u << 22)) {
            node_limit = 1u << 22;
        }
        ma_params.tracker = params->tracker;
        ma_params.node_limit = node_limit;
        ma_params.depth_limit = 2048;

        st = jxl_ma_config_parse(alloc, bs, &ma_params, global_ma);
        if (st != JXL_MODULAR_OK) {
            return modular_to_frame(st);
        }
    } else {
        jxl_ma_config_init(global_ma);
    }

    jxl_modular_params_init(&mod_params);
    if (!jxl_modular_params_set_for_modular_frame(alloc, params->ctx, &mod_params, params->image,
                                                params->frame)) {
        jxl_modular_params_free(alloc, &mod_params);
        return JXL_FRAME_OUT_OF_MEMORY;
    }

    ctx.params = &mod_params;
    ctx.global_ma = has_ma ? global_ma : NULL;
    ctx.tracker = params->tracker;
    ctx.ctx = params->ctx;


    jxl_modular_image_destination_init(&gdest);
    st = jxl_modular_dest_apply_local_header(alloc, bs, &ctx, &gdest);
    if (st != JXL_MODULAR_OK) {
        jxl_modular_image_destination_free(alloc, &gdest);
        jxl_modular_params_free(alloc, &mod_params);
        return modular_to_frame(st);
    }

    st = jxl_modular_prepare_gmodular(alloc, &gdest);
    if (st != JXL_MODULAR_OK) {
        jxl_modular_image_destination_free(alloc, &gdest);
        jxl_modular_params_free(alloc, &mod_params);
        return modular_to_frame(st);
    }
    if (jxl_modular_gmodular_channel_count(&gdest) == 0) {
        jxl_modular_image_destination_free(alloc, &gdest);
        jxl_modular_params_free(alloc, &mod_params);
        return JXL_FRAME_OK;
    }
    st = jxl_modular_gmodular_decode(params->ctx, alloc, bs, &gdest, params->allow_partial);
    jxl_modular_image_destination_free(alloc, &gdest);
    jxl_modular_params_free(alloc, &mod_params);
    return modular_to_frame(st);
}

jxl_frame_status_t jxl_frame_modular_pass_group_bitstream(
    jxl_context *ctx, jxl_allocator_state *alloc, const jxl_frame *frame,
    const jxl_parsed_image_header *image, uint32_t pass_idx, uint32_t group_idx,
    jxl_ma_config *global_ma, int *has_global_ma_out, jxl_bs *out_bs, int allow_partial) {
    jxl_bs lf_bs;
    jxl_lf_global_modular_params lp = {0};
    uint32_t pg_index;
    jxl_frame_status_t st;
    const jxl_frame_group_data *lf;
    uint32_t ng;
    const jxl_frame_group_data *pg;
    if (alloc == NULL || frame == NULL || image == NULL || global_ma == NULL ||
        has_global_ma_out == NULL || out_bs == NULL) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }
    if (frame->header.encoding != JXL_FRAME_ENCODING_MODULAR) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }
    if (!jxl_frame_is_loading_done(frame)) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }

    lp.ctx = ctx;
    lp.image = image;
    lp.frame = &frame->header;
    lp.tracker = NULL;
    lp.allow_partial = allow_partial;


    if (jxl_toc_is_single_entry(&frame->toc)) {
        jxl_bs bs;
        const jxl_frame_group_data *all = &frame->data[0];
        if (frame->data_len == 0) {
            return JXL_FRAME_BITSTREAM_ERROR;
        }
        jxl_bs_init(&bs, all->bytes, all->bytes_len);
        st = jxl_lf_global_modular_consume(alloc, &bs, &lp, global_ma, has_global_ma_out);
        if (st != JXL_FRAME_OK) {
            return st;
        }
        if (bs.num_read_bits > all->bytes_len * 8) {
            return JXL_FRAME_BITSTREAM_ERROR;
        }
        *out_bs = bs;
        return JXL_FRAME_OK;
    }

    lf = jxl_frame_group_by_kind(frame, JXL_TOC_KIND_LF_GLOBAL, 0);
    ng = jxl_frame_header_num_groups(&frame->header);
    pg_index = pass_idx * ng + group_idx;
    pg = jxl_frame_group_by_kind(frame, JXL_TOC_KIND_GROUP_PASS, pg_index);
    if (lf == NULL || pg == NULL) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }

    jxl_bs_init(&lf_bs, lf->bytes, lf->bytes_len);
    st = jxl_lf_global_modular_consume(alloc, &lf_bs, &lp, global_ma, has_global_ma_out);
    if (st != JXL_FRAME_OK) {
        return st;
    }

    jxl_bs_init(out_bs, pg->bytes, pg->bytes_len);
    return JXL_FRAME_OK;
}
