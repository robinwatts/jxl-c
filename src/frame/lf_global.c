// SPDX-License-Identifier: MIT OR Apache-2.0
#include "lf_global.h"

#include "frame/noise.h"
#include "modular/modular.h"
#include "modular/param.h"
#include "modular/prepare_subimage.h"
#include "modular/subimage_decode.h"

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

static jxl_frame_status_t vardct_to_frame(jxl_vardct_status_t st) {
    switch (st) {
    case JXL_VARDCT_OK:
        return JXL_FRAME_OK;
    case JXL_VARDCT_OUT_OF_MEMORY:
        return JXL_FRAME_OUT_OF_MEMORY;
    case JXL_VARDCT_BITSTREAM_ERROR:
        return JXL_FRAME_BITSTREAM_ERROR;
    default:
        return JXL_FRAME_DECODER_ERROR;
    }
}

jxl_frame_status_t jxl_lf_global_parse_prefix(jxl_allocator_state *alloc, jxl_bs *bs,
                                              const jxl_parsed_image_header *image,
                                              const jxl_frame_header *frame,
                                              jxl_patches *patches_out, jxl_splines *splines_out,
                                              jxl_noise_parameters *noise_out) {
    if (alloc == NULL || bs == NULL || image == NULL || frame == NULL) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }

    if (jxl_frame_flags_patches(&frame->flags)) {
        jxl_patches tmp;
        jxl_patches *store = patches_out;
        jxl_frame_status_t pst;
        if (store == NULL) {
            jxl_patches_init(&tmp);
            store = &tmp;
        }
        pst = jxl_patches_parse(alloc, bs, image, frame, store);
        if (pst != JXL_FRAME_OK) {
            if (patches_out == NULL) {
                jxl_patches_free(alloc, &tmp);
            }
            return pst;
        }
        if (patches_out == NULL) {
            jxl_patches_free(alloc, &tmp);
        }
    }

    if (jxl_frame_flags_splines(&frame->flags)) {
        jxl_splines tmp;
        jxl_splines *store = splines_out;
        jxl_frame_status_t sst;
	if (store == NULL) {
            jxl_splines_init(&tmp);
            store = &tmp;
        }
        sst = jxl_splines_parse(alloc, bs, frame, store);
        if (sst != JXL_FRAME_OK) {
            if (splines_out == NULL) {
                jxl_splines_free(alloc, &tmp);
            }
            return sst;
        }
        if (splines_out == NULL) {
            jxl_splines_free(alloc, &tmp);
        }
    }

    if (jxl_frame_flags_noise(&frame->flags)) {
        jxl_noise_parameters noise;
        if (jxl_noise_parameters_parse(bs, &noise) != JXL_BS_OK) {
            return JXL_FRAME_BITSTREAM_ERROR;
        }
        if (noise_out != NULL) {
            *noise_out = noise;
        }
    }

    return JXL_FRAME_OK;
}

void jxl_lf_global_init(jxl_lf_global *lf) {
    if (lf == NULL) {
        return;
    }
    memset(lf, 0, sizeof(*lf));
    jxl_ma_config_init(&lf->global_ma);
    jxl_modular_image_destination_init(&lf->gmodular);
}

void jxl_lf_global_free(jxl_allocator_state *alloc, jxl_lf_global *lf) {
    if (lf == NULL) {
        return;
    }
    if (lf->global_ma_owns) {
        jxl_ma_config_destroy(alloc, &lf->global_ma);
    }
    jxl_ma_config_init(&lf->global_ma);
    if (lf->gmodular_used) {
        jxl_modular_image_destination_free(alloc, &lf->gmodular);
    }
    jxl_modular_image_destination_init(&lf->gmodular);
    lf->has_global_ma = 0;
    lf->global_ma_owns = 0;
    lf->gmodular_used = 0;
    jxl_patches_free(alloc, &lf->patches);
    lf->has_patches = 0;
    if (lf->has_vardct) {
        jxl_hf_block_context_free(alloc, &lf->hf_block_ctx);
    }
    lf->has_vardct = 0;
    lf->has_noise = 0;
    jxl_splines_free(alloc, &lf->splines);
    lf->has_splines = 0;
}

jxl_frame_status_t jxl_lf_global_consume(jxl_allocator_state *alloc, jxl_bs *bs,
                                         const jxl_lf_global_params *params, jxl_lf_global *out) {
    int has_ma;
    jxl_modular_params mod_params;
    jxl_modular_parse_ctx ctx = {0};
    jxl_frame_status_t fst;
    int params_ok;
    jxl_modular_status_t st;
    if (alloc == NULL || bs == NULL || params == NULL || params->image == NULL ||
        params->frame == NULL || out == NULL) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }
    if (jxl_frame_flags_patches(&params->frame->flags)) {
        jxl_frame_status_t pst =
            jxl_patches_parse(alloc, bs, params->image, params->frame, &out->patches);
        if (pst != JXL_FRAME_OK) {
            return pst;
        }
        out->has_patches = 1;
    }

    if (jxl_frame_flags_splines(&params->frame->flags)) {
        jxl_frame_status_t sst =
            jxl_splines_parse(alloc, bs, params->frame, &out->splines);
        if (sst != JXL_FRAME_OK) {
            return sst;
        }
        out->has_splines = 1;
    }

    if (jxl_frame_flags_noise(&params->frame->flags)) {
        if (jxl_noise_parameters_parse(bs, &out->noise) != JXL_BS_OK) {
            return JXL_FRAME_BITSTREAM_ERROR;
        }
        out->has_noise = 1;
    }

    fst =
        vardct_to_frame(jxl_lf_channel_dequant_parse(bs, &out->lf_dequant));
    if (fst != JXL_FRAME_OK) {
        return fst;
    }

    out->has_vardct = params->frame->encoding == JXL_FRAME_ENCODING_VARDCT;
    if (out->has_vardct) {
        fst = vardct_to_frame(jxl_quantizer_parse(bs, &out->quantizer));
        if (fst != JXL_FRAME_OK) {
            return fst;
        }
        fst = vardct_to_frame(jxl_hf_block_context_parse(alloc, bs, &out->hf_block_ctx));
        if (fst != JXL_FRAME_OK) {
            return fst;
        }
        fst = vardct_to_frame(jxl_lf_channel_correlation_parse(bs, &out->lf_chan_corr));
        if (fst != JXL_FRAME_OK) {
            return fst;
        }
    }

    if (out->has_splines) {
        float corr_x = out->has_vardct ? out->lf_chan_corr.base_correlation_x : 0.0f;
        float corr_b = out->has_vardct ? out->lf_chan_corr.base_correlation_b : 1.0f;
        uint64_t image_size = (uint64_t)params->frame->width * (uint64_t)params->frame->height;
        uint64_t estimated = jxl_splines_estimate_area(&out->splines, corr_x, corr_b);
        uint64_t max_estimated = (1ull << 42);
        uint64_t alt = 1024ull * image_size + (1ull << 32);
        if (alt < max_estimated) {
            max_estimated = alt;
        }
        if (estimated > max_estimated) {
            return JXL_FRAME_VALIDATION_ERROR;
        }
    }

    has_ma = 0;
    if (jxl_bs_read_bool(bs, &has_ma) != JXL_BS_OK) {
        return JXL_FRAME_BITSTREAM_ERROR;
    }
    out->has_global_ma = has_ma;
    if (has_ma) {
        jxl_ma_config_params ma_params = {0};
        uint64_t num_ch = (uint64_t)params->frame->encoded_color_channels +
                          (uint64_t)params->image->num_extra_channels;
        uint64_t samples =
            (uint64_t)params->frame->width * (uint64_t)params->frame->height * num_ch / 16u;
        size_t node_limit = (size_t)(1024u + samples);
        jxl_modular_status_t mst;
        if (node_limit > (1u << 22)) {
            node_limit = 1u << 22;
        }
        ma_params.tracker = params->tracker;
        ma_params.node_limit = node_limit;
        ma_params.depth_limit = 2048;

        mst = jxl_ma_config_parse(alloc, bs, &ma_params, &out->global_ma);
        fst = modular_to_frame(mst);
        if (fst != JXL_FRAME_OK) {
            return fst;
        }
        out->global_ma_owns = 1;
    } else {
        jxl_ma_config_init(&out->global_ma);
    }

    jxl_modular_params_init(&mod_params);
    params_ok = params->frame->encoding == JXL_FRAME_ENCODING_MODULAR
                        ? jxl_modular_params_set_for_modular_frame(alloc, params->ctx, &mod_params,
                                                                   params->image, params->frame)
                        : jxl_modular_params_set_for_vardct_frame(alloc, params->ctx, &mod_params,
                                                                  params->image, params->frame);
    if (!params_ok) {
        jxl_modular_params_free(alloc, &mod_params);
        return JXL_FRAME_OUT_OF_MEMORY;
    }

    if (mod_params.num_channels == 0) {
        jxl_modular_params_free(alloc, &mod_params);
        return JXL_FRAME_OK;
    }

    ctx.params = &mod_params;
    ctx.global_ma = has_ma ? &out->global_ma : NULL;
    ctx.tracker = params->tracker;
    ctx.ctx = params->ctx;
    ctx.retain_pretransform_channels = 1;

    st = jxl_modular_dest_apply_local_header(alloc, bs, &ctx, &out->gmodular);
    out->gmodular_used = 1;
    if (st != JXL_MODULAR_OK) {
        jxl_modular_params_free(alloc, &mod_params);
        return modular_to_frame(st);
    }

    /* Rust GlobalModular::parse: prepare_gmodular before LF-global modular decode. */
    if (out->gmodular.image_channels_len > 0) {
        st = jxl_modular_prepare_gmodular(alloc, &out->gmodular);
        if (st != JXL_MODULAR_OK) {
            jxl_modular_params_free(alloc, &mod_params);
            return modular_to_frame(st);
        }
        if (jxl_modular_gmodular_channel_count(&out->gmodular) > 0) {
            st = jxl_modular_gmodular_decode(params->ctx, alloc, bs, &out->gmodular,
                                             params->allow_partial);
            if (st != JXL_MODULAR_OK) {
                jxl_modular_params_free(alloc, &mod_params);
                return modular_to_frame(st);
            }
        }
    }

    jxl_modular_params_free(alloc, &mod_params);
    return JXL_FRAME_OK;
}
