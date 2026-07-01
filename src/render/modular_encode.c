// SPDX-License-Identifier: MIT OR Apache-2.0
#include "modular_encode.h"

#include "bitstream/bitstream.h"
#include "frame/frame.h"
#include "frame/frame_header.h"
#include "frame/lf_global.h"
#include "frame/patch.h"
#include "frame/toc.h"
#include "modular/group_decode.h"
#include "modular/image.h"
#include "modular/param.h"
#include "modular/modular_parse.h"
#include "modular/prepare_subimage.h"
#include "modular/region.h"
#include "modular/subimage_decode.h"
#include "modular/transform/inverse.h"
#include "render/modular_compose.h"
#include "render/modular_sample.h"
#include "render/render_internal.h"
#include "render/render_util.h"
#include "vardct/lf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static jxl_status_t frame_to_status(jxl_frame_status_t st) {
    switch (st) {
    case JXL_FRAME_OK:
        return JXL_OK;
    case JXL_FRAME_OUT_OF_MEMORY:
        return JXL_ERROR_OUT_OF_MEMORY;
    case JXL_FRAME_BITSTREAM_ERROR:
    case JXL_FRAME_VALIDATION_ERROR:
        return JXL_ERROR_INVALID_INPUT;
    default:
        return JXL_ERROR_UNSUPPORTED;
    }
}

static jxl_status_t modular_to_status(jxl_modular_status_t st) {
    switch (st) {
    case JXL_MODULAR_OK:
        return JXL_OK;
    case JXL_MODULAR_OUT_OF_MEMORY:
        return JXL_ERROR_OUT_OF_MEMORY;
    case JXL_MODULAR_BITSTREAM_ERROR:
        return JXL_ERROR_INVALID_INPUT;
    default:
        return JXL_ERROR_UNSUPPORTED;
    }
}


static jxl_status_t modular_compose_frame(const jxl_keyframe_render_params *params,
                                         const jxl_parsed_image_header *parsed,
                                         const jxl_frame_header *fh,
                                         const jxl_modular_image_destination *dest,
                                         const jxl_lf_channel_dequant *xyb_dequant,
                                         jxl_reference_store *refs, jxl_render *canvas) {
    jxl_modular_region crop_region = {0};
    jxl_modular_compose_params cp = {0};
    if (params->has_crop) {
        jxl_modular_region compound_tmp;
        compound_tmp.left = (int32_t)params->crop.left;
        compound_tmp.top = (int32_t)params->crop.top;
        compound_tmp.width = params->crop.width;
        compound_tmp.height = params->crop.height;
        crop_region = compound_tmp;

    }
    cp.ctx = params->ctx;
    cp.alloc = params->alloc;
    cp.parsed = parsed;
    cp.fh = fh;
    cp.dest = dest;
    cp.xyb_dequant = xyb_dequant;
    cp.bit_depth = params->bit_depth;
    cp.num_color_channels = fh->encoded_color_channels != 0u ? fh->encoded_color_channels
                                                              : params->num_color_channels;
    cp.num_extra_channels = params->num_extra_channels;
    cp.output_region = params->output_region;
    cp.has_crop = params->output_region == NULL && params->has_crop;
    cp.crop = crop_region;
    cp.prefer_canvas_base = 1;

    return jxl_render_compose_modular_dest(&cp, refs, canvas);
}

jxl_status_t jxl_modular_encode_keyframe(const jxl_keyframe_render_params *params,
                                         const jxl_parsed_image_header *parsed,
                                         const uint8_t *codestream, size_t cs_len, jxl_bs *bs,
                                         const jxl_modular_region *filter_region, jxl_render *r,
                                         jxl_modular_encode_result *result) {
    jxl_frame frame;
    jxl_modular_params mod_params;
    jxl_modular_image_destination dest;
    jxl_ma_config global_ma;
    jxl_patches frame_patches;
    jxl_splines frame_splines;
    jxl_noise_parameters frame_noise_capture;
    uint32_t keyframe_count;
    jxl_status_t st = JXL_ERROR_UNSUPPORTED;
    if (params == NULL || params->alloc == NULL || parsed == NULL || codestream == NULL ||
        bs == NULL || r == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }

    jxl_render_bind_materialization(r, params->alloc, parsed);

    jxl_frame_init(&frame);
    jxl_modular_params_init(&mod_params);
    jxl_modular_image_destination_init(&dest);
    jxl_ma_config_init(&global_ma);
    jxl_patches_init(&frame_patches);
    jxl_splines_init(&frame_splines);
    memset(&frame_noise_capture, 0, sizeof(frame_noise_capture));
    keyframe_count = 0;
    for (;;) {
        size_t consumed;
        jxl_bs gbs;
        jxl_lf_channel_dequant dequant;
        int has_ma;
        jxl_modular_region pad_filter_region;
        jxl_modular_parse_ctx ctx = {0};
        jxl_frame_status_t fst;
        size_t meta_end;
        size_t payload_avail;
        const jxl_frame_group_data *src;
        int multi_group;
        jxl_modular_region image_region;
        const jxl_modular_region *filter_ptr;
        const jxl_lf_channel_dequant *xyb_dequant;
        int is_key;
        if (bs->num_read_bits >= (uint64_t)cs_len * 8) {
            break;
        }
        fst = jxl_frame_parse(params->alloc, bs, parsed, &frame);
        st = frame_to_status(fst);
        if (st != JXL_OK) {
            if (bs->num_read_bits >= (uint64_t)cs_len * 8) {
                break;
            }
            jxl_render_set_error((jxl_keyframe_render_params *)params,
                                 "failed to parse frame for render");
            goto cleanup;
        }

        if (frame.header.encoding != JXL_FRAME_ENCODING_MODULAR) {
            st = JXL_ERROR_UNSUPPORTED;
            jxl_render_set_error((jxl_keyframe_render_params *)params,
                                 "modular render requires modular frames");
            goto cleanup;
        }

        meta_end = bs->num_read_bits / 8;
        if (meta_end > cs_len) {
            st = JXL_ERROR_INVALID_INPUT;
            jxl_render_set_error((jxl_keyframe_render_params *)params,
                                 "frame metadata exceeds codestream");
            goto cleanup;
        }
        payload_avail = cs_len - meta_end;
        if (frame.toc.total_size > payload_avail) {
            st = JXL_ERROR_INVALID_INPUT;
            jxl_render_set_error((jxl_keyframe_render_params *)params,
                                 "frame TOC payload exceeds codestream");
            goto cleanup;
        }

        consumed = 0;
        jxl_frame_feed_bytes(&frame, codestream + meta_end, frame.toc.total_size, &consumed);
        if (consumed != frame.toc.total_size || !jxl_frame_is_loading_done(&frame)) {
            st = JXL_ERROR_INVALID_INPUT;
            jxl_render_set_error((jxl_keyframe_render_params *)params,
                                 "incomplete frame group payloads");
            goto cleanup;
        }

        src = NULL;
        if (jxl_toc_is_single_entry(&frame.toc)) {
            src = frame.data_len > 0 ? &frame.data[0] : NULL;
        } else {
            src = jxl_frame_group_by_kind(&frame, JXL_TOC_KIND_LF_GLOBAL, 0);
        }
        if (src == NULL) {
            st = JXL_ERROR_INVALID_INPUT;
            jxl_render_set_error((jxl_keyframe_render_params *)params, "missing lf-global group");
            goto cleanup;
        }

        jxl_bs_init(&gbs, src->bytes, src->bytes_len);
        jxl_patches_free(params->alloc, &frame_patches);
        jxl_patches_init(&frame_patches);
        jxl_splines_free(params->alloc, &frame_splines);
        jxl_splines_init(&frame_splines);
        memset(&frame_noise_capture, 0, sizeof(frame_noise_capture));
        {
            jxl_frame_status_t pst = jxl_lf_global_parse_prefix(params->alloc, &gbs, parsed,
                                                               &frame.header, &frame_patches,
                                                               &frame_splines, &frame_noise_capture);
            st = frame_to_status(pst);
            if (st != JXL_OK) {
                jxl_patches_free(params->alloc, &frame_patches);
                jxl_render_set_error((jxl_keyframe_render_params *)params,
                                     "failed to parse lf-global prefix");
                goto cleanup;
            }
        }
        if (jxl_lf_channel_dequant_parse(&gbs, &dequant) != JXL_VARDCT_OK) {
            jxl_patches_free(params->alloc, &frame_patches);
            st = JXL_ERROR_INVALID_INPUT;
            jxl_render_set_error((jxl_keyframe_render_params *)params, "failed to parse lf dequant");
            goto cleanup;
        }

        has_ma = 0;
        if (jxl_bs_read_bool(&gbs, &has_ma) != JXL_BS_OK) {
            st = JXL_ERROR_INVALID_INPUT;
            jxl_render_set_error((jxl_keyframe_render_params *)params,
                                 "failed to parse modular MA flag");
            goto cleanup;
        }
        if (has_ma) {
            jxl_ma_config_params ma_params = {0};
            uint64_t num_ch = (uint64_t)frame.header.encoded_color_channels +
                              (uint64_t)parsed->num_extra_channels;
            uint64_t samples =
                (uint64_t)frame.header.width * (uint64_t)frame.header.height * num_ch / 16u;
            jxl_modular_status_t mst;
            size_t node_limit = (size_t)(1024u + samples);
            if (node_limit > (1u << 22)) {
                node_limit = 1u << 22;
            }
            ma_params.tracker = NULL;
            ma_params.node_limit = node_limit;
            ma_params.depth_limit = 2048;

            mst = jxl_ma_config_parse(params->alloc, &gbs, &ma_params, &global_ma);
            st = modular_to_status(mst);
            if (st != JXL_OK) {
                jxl_render_set_error((jxl_keyframe_render_params *)params,
                                     "failed to parse modular MA tree");
                goto cleanup;
            }
        }

        if (!jxl_modular_params_set_for_modular_frame(params->alloc, params->ctx, &mod_params,
                                                      parsed, &frame.header)) {
            st = JXL_ERROR_OUT_OF_MEMORY;
            jxl_render_set_error((jxl_keyframe_render_params *)params,
                                 "failed to build modular params");
            goto cleanup;
        }

        multi_group = !jxl_toc_is_single_entry(&frame.toc);
        ctx.ctx = params->ctx;
        ctx.params = &mod_params;
        ctx.global_ma = has_ma ? &global_ma : NULL;
        ctx.tracker = NULL;
        ctx.retain_pretransform_channels = 1;

        {
            jxl_modular_status_t mst =
                jxl_modular_dest_apply_local_header(params->alloc, &gbs, &ctx, &dest);
            st = modular_to_status(mst);
            if (st != JXL_OK) {
                jxl_render_set_error((jxl_keyframe_render_params *)params,
                                     "failed to parse modular local header");
                goto cleanup;
            }
        }

        if (jxl_modular_image_has_squeeze(&dest)) {
            ctx.retain_pretransform_channels = 1;
        }

        {
            jxl_modular_status_t pst = jxl_modular_prepare_gmodular(params->alloc, &dest);
            st = modular_to_status(pst);
            if (st != JXL_OK) {
                jxl_render_set_error((jxl_keyframe_render_params *)params,
                                     "failed to prepare modular gmodular layout");
                goto cleanup;
            }
        }

        {
            jxl_modular_status_t mst =
                jxl_modular_gmodular_decode(params->ctx, params->alloc, &gbs, &dest,
                                            multi_group ? 1 : 0);
            st = modular_to_status(mst);
            if (st != JXL_OK) {
                jxl_render_set_error((jxl_keyframe_render_params *)params,
                                     "failed to decode modular global channels");
                goto cleanup;
            }
        }

        image_region = jxl_modular_region_with_size(
            jxl_frame_header_color_sample_width(&frame.header),
            jxl_frame_header_color_sample_height(&frame.header));
        if (params->output_region != NULL) {
            image_region = *params->output_region;
        } else if (params->has_crop) {
            jxl_modular_region compound_tmp;
            compound_tmp.left = (int32_t)params->crop.left;
            compound_tmp.top = (int32_t)params->crop.top;
            compound_tmp.width = params->crop.width;
            compound_tmp.height = params->crop.height;
            image_region = compound_tmp;

        }
        filter_ptr = jxl_render_resolve_color_filter_region_for_image(
            parsed, &frame.header, image_region, filter_region, &pad_filter_region);
        if (multi_group) {
            st = jxl_modular_decode_frame_group_coefficients(
                params->ctx, params->alloc, &frame, parsed, &global_ma, has_ma, &mod_params, &dest,
                multi_group, 1, filter_ptr);
            if (st != JXL_OK) {
                jxl_render_set_error((jxl_keyframe_render_params *)params,
                                     "failed to decode modular pass groups");
                goto cleanup;
            }
        }

        {
            /* Keep inverse RCT in-place on i16 grids; compose transfers via take_grid. */
            jxl_modular_status_t mst;
            jxl_modular_float_export_ctx_init(&dest.float_export);

            mst = jxl_modular_gmodular_finish(params->ctx, params->alloc,
                &dest, frame.header.width, frame.header.height, parsed->bit_depth_bits, &mod_params);
            st = modular_to_status(mst);
            if (st != JXL_OK) {
                jxl_render_set_error((jxl_keyframe_render_params *)params,
                                     "failed to apply modular inverse transforms");
                goto cleanup;
            }
        }

        xyb_dequant =
            parsed->xyb_encoded && params->num_color_channels >= 3 ? &dequant : NULL;
        st = modular_compose_frame(params, parsed, &frame.header, &dest, xyb_dequant,
                                   params->external_refs, r);
        if (st != JXL_OK) {
            jxl_patches_free(params->alloc, &frame_patches);
            goto cleanup;
        }

        is_key = jxl_frame_header_is_keyframe(&frame.header);
        if (is_key && result != NULL) {
            result->fh = frame.header;
            if (frame.header.ec_upsampling != NULL && frame.header.ec_upsampling_len > 0) {
                size_t ec_len = frame.header.ec_upsampling_len;
                if (ec_len > sizeof(result->ec_upsampling_storage) /
                                sizeof(result->ec_upsampling_storage[0])) {
                    ec_len = sizeof(result->ec_upsampling_storage) /
                             sizeof(result->ec_upsampling_storage[0]);
                }
                memcpy(result->ec_upsampling_storage, frame.header.ec_upsampling,
                       ec_len * sizeof(result->ec_upsampling_storage[0]));
                result->fh.ec_upsampling = result->ec_upsampling_storage;
                result->fh.ec_upsampling_len = ec_len;
            } else {
                result->fh.ec_upsampling = NULL;
                result->fh.ec_upsampling_len = 0;
            }
            result->valid = 1;
            result->visible_frames = keyframe_count + 1u;
            result->invisible_frames = 0u;
            if (jxl_frame_flags_noise(&frame.header.flags)) {
                result->noise = frame_noise_capture;
                result->has_noise = 1;
            }
            if (jxl_frame_flags_patches(&frame.header.flags) && frame_patches.refs_len > 0) {
                jxl_patches_free(params->alloc, &result->patches);
                result->patches = frame_patches;
                jxl_patches_init(&frame_patches);
                result->has_patches = 1;
            }
            if (jxl_frame_flags_splines(&frame.header.flags) &&
                frame_splines.quant_splines_len > 0) {
                jxl_splines_free(params->alloc, &result->splines);
                result->splines = frame_splines;
                jxl_splines_init(&frame_splines);
                result->has_splines = 1;
            }
        }
        jxl_patches_free(params->alloc, &frame_patches);
        jxl_patches_init(&frame_patches);
        jxl_splines_free(params->alloc, &frame_splines);
        jxl_splines_init(&frame_splines);

        if (jxl_bs_skip_bits(bs, (uint64_t)frame.toc.total_size * 8) != JXL_BS_OK) {
            st = JXL_ERROR_INVALID_INPUT;
            jxl_render_set_error((jxl_keyframe_render_params *)params,
                                 "failed to advance past frame payload");
            goto cleanup;
        }

        jxl_modular_image_destination_free(params->alloc, &dest);
        jxl_modular_image_destination_init(&dest);
        jxl_ma_config_destroy(params->alloc, &global_ma);
        jxl_ma_config_init(&global_ma);
        jxl_modular_params_free(params->alloc, &mod_params);
        jxl_modular_params_init(&mod_params);
        jxl_frame_free(params->alloc, &frame);
        jxl_frame_init(&frame);
        if (is_key) {
            if (keyframe_count == params->keyframe_index) {
                break;
            }
            keyframe_count++;
        }
    }

    return JXL_OK;

cleanup:
    jxl_splines_free(params->alloc, &frame_splines);
    jxl_patches_free(params->alloc, &frame_patches);
    jxl_modular_image_destination_free(params->alloc, &dest);
    jxl_modular_params_free(params->alloc, &mod_params);
    jxl_ma_config_destroy(params->alloc, &global_ma);
    jxl_frame_free(params->alloc, &frame);
    return st;
}

void jxl_modular_encode_result_init(jxl_modular_encode_result *result) {
    if (result == NULL) {
        return;
    }
    memset(result, 0, sizeof(*result));
    jxl_patches_init(&result->patches);
    jxl_splines_init(&result->splines);
}

void jxl_modular_encode_result_free(jxl_allocator_state *alloc, jxl_modular_encode_result *result) {
    if (alloc == NULL || result == NULL) {
        return;
    }
    jxl_splines_free(alloc, &result->splines);
    jxl_patches_free(alloc, &result->patches);
    memset(result, 0, sizeof(*result));
}

