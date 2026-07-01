// SPDX-License-Identifier: MIT OR Apache-2.0
#include "modular_parse.h"

#include "modular/channel.h"
#include "modular/util.h"

#include <stdio.h>
#include <string.h>

static const jxl_u32_spec k_nb_transforms[4] = {JXL_U32_C(0), JXL_U32_C(1), JXL_U32_BITS(2, 4),
                                                JXL_U32_BITS(18, 8)};

void jxl_modular_header_init(jxl_modular_header *h) {
    if (h != NULL) {
        memset(h, 0, sizeof(*h));
    }
}

void jxl_modular_header_free(jxl_allocator_state *alloc, jxl_modular_header *h) {
    size_t i;
    if (h == NULL) {
        return;
    }
    for (i = 0; i < h->transform_len; ++i) {
        jxl_transform_info_free(alloc, &h->transform[i]);
    }
    jxl_free(alloc, h->transform);
    h->transform = NULL;
    h->transform_len = 0;
}

jxl_modular_status_t jxl_modular_header_parse(jxl_allocator_state *alloc, jxl_bs *bs,
                                                jxl_modular_header *out) {
    int use_global;
    if (bs == NULL || out == NULL) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
    jxl_modular_header_free(alloc, out);
    jxl_modular_header_init(out);
    use_global = 0;
    JXL_MODULAR_TRY_BS(jxl_bs_read_bool(bs, &use_global));
    out->use_global_tree = use_global;
    if (jxl_wp_header_parse(bs, &out->wp_params) != JXL_MODULAR_OK) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
    JXL_MODULAR_TRY_BS(jxl_bs_read_u32(bs, k_nb_transforms, &out->nb_transforms));
    if (out->nb_transforms > 512) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
    out->transform_len = out->nb_transforms;
    if (out->transform_len > 0) {
        size_t i;
        out->transform = jxl_calloc(alloc, out->transform_len, sizeof(*out->transform));
        if (out->transform == NULL) {
            return JXL_MODULAR_OUT_OF_MEMORY;
        }
        for (i = 0; i < out->transform_len; ++i) {
            jxl_modular_status_t st =
                jxl_transform_info_parse(alloc, bs, &out->wp_params, &out->transform[i]);
            if (st != JXL_MODULAR_OK) {
                jxl_modular_header_free(alloc, out);
                return st;
            }
        }
    }
    return JXL_MODULAR_OK;
}

void jxl_modular_header_ma_init(jxl_modular_header_ma *hm) {
    if (hm != NULL) {
        memset(hm, 0, sizeof(*hm));
        jxl_ma_config_init(&hm->ma_ctx);
    }
}

void jxl_modular_header_ma_free(jxl_allocator_state *alloc, jxl_modular_header_ma *hm) {
    if (hm == NULL) {
        return;
    }
    jxl_modular_header_free(alloc, &hm->header);
    if (hm->ma_owns) {
        jxl_ma_config_destroy(alloc, &hm->ma_ctx);
    } else {
        jxl_ma_config_init(&hm->ma_ctx);
    }
    hm->ma_owns = 0;
}

jxl_modular_status_t jxl_modular_read_local_header(jxl_allocator_state *alloc, jxl_bs *bs,
                                                 const jxl_modular_parse_ctx *ctx,
                                                 jxl_modular_header_ma *out,
                                                 jxl_modular_channels *channels_out) {
                                                     size_t i;
    jxl_modular_channels transformed;
    jxl_modular_channels *layout;
    if (alloc == NULL || bs == NULL || ctx == NULL || out == NULL || channels_out == NULL) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
    if (out->header.transform != NULL || out->ma_owns) {
        jxl_modular_header_ma_free(alloc, out);
    }
    jxl_modular_header_ma_init(out);

    if (channels_out->info_len == 0) {
        if (jxl_modular_channels_from_params(alloc, ctx->params, channels_out) != JXL_MODULAR_OK) {
            return JXL_MODULAR_OUT_OF_MEMORY;
        }
    }

    if (jxl_modular_header_parse(alloc, bs, &out->header) != JXL_MODULAR_OK) {
        jxl_modular_header_ma_free(alloc, out);
        jxl_modular_header_ma_init(out);
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
    {
        if (JXL_DEBUG_FLAG(ctx->ctx, debug_bits)) {
            fprintf(stderr, "c after header_parse bits=%zu use_global=%d nb_tr=%u ch_in=%zu\n",
                    bs->num_read_bits, out->header.use_global_tree ? 1 : 0,
                    out->header.nb_transforms, channels_out->info_len);
        }
    }

    layout = channels_out;
    jxl_modular_channels_init(&transformed);
    if (ctx->retain_pretransform_channels) {
        if (channels_out->info_len > 0) {
            if (jxl_modular_channels_reserve(alloc, &transformed, channels_out->info_len) !=
                JXL_MODULAR_OK) {
                jxl_modular_header_ma_free(alloc, out);
                jxl_modular_header_ma_init(out);
                return JXL_MODULAR_OUT_OF_MEMORY;
            }
            transformed.info_len = channels_out->info_len;
            transformed.nb_meta_channels = channels_out->nb_meta_channels;
            memcpy(transformed.info, channels_out->info,
                   channels_out->info_len * sizeof(*transformed.info));
        }
        layout = &transformed;
    }
    for (i = 0; i < out->header.transform_len; ++i) {
        jxl_modular_status_t st =
            jxl_transform_prepare_channel_info(alloc, &out->header.transform[i], layout);
        if (st != JXL_MODULAR_OK) {
            jxl_modular_channels_free(alloc, &transformed);
            jxl_modular_header_ma_free(alloc, out);
            jxl_modular_header_ma_init(out);
            return st;
        }
    }

    if (layout->info_len > (1u << 16)) {
        jxl_modular_channels_free(alloc, &transformed);
        jxl_modular_header_ma_free(alloc, out);
        jxl_modular_header_ma_init(out);
        return JXL_MODULAR_BITSTREAM_ERROR;
    }

    if (out->header.use_global_tree) {
        if (ctx->global_ma == NULL) {
            jxl_modular_channels_free(alloc, &transformed);
            jxl_modular_header_ma_free(alloc, out);
            jxl_modular_header_ma_init(out);
            return JXL_MODULAR_GLOBAL_MA_TREE_NOT_AVAILABLE;
        }
        out->ma_ctx = *ctx->global_ma;
        out->ma_owns = 0;
    } else {
        size_t i;
        uint64_t local_samples = 0;
        jxl_ma_config_params ma_params = {0};
        size_t node_limit;
        for (i = 0; i < layout->info_len; ++i) {
            local_samples +=
                (uint64_t)layout->info[i].width * (uint64_t)layout->info[i].height;
        }
        node_limit = (size_t)(1024 + local_samples);
        if (node_limit > (1u << 20)) {
            node_limit = 1u << 20;
        }
        ma_params.tracker = ctx->tracker;
        ma_params.node_limit = node_limit;
        ma_params.depth_limit = 2048;

        if (jxl_ma_config_parse(alloc, bs, &ma_params, &out->ma_ctx) != JXL_MODULAR_OK) {
            jxl_modular_channels_free(alloc, &transformed);
            jxl_modular_header_ma_free(alloc, out);
            jxl_modular_header_ma_init(out);
            return JXL_MODULAR_DECODER_ERROR;
        }
        out->ma_owns = 1;
    }

    jxl_modular_channels_free(alloc, &transformed);
    return JXL_MODULAR_OK;
}
