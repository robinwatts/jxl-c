// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render_internal.h"

#include "bitstream/bitstream.h"
#include "codestream_collect.h"
#include "frame/frame.h"
#include "frame/frame_header.h"
#include "image/image_internal.h"
#include "render/patch_render.h"
#include "render/render_frame.h"

#include <string.h>

void jxl_render_set_error(jxl_keyframe_render_params *params, const char *message) {
    if (params == NULL || params->error_out == NULL || params->alloc == NULL) {
        return;
    }
    jxl_free(params->alloc, *params->error_out);
    *params->error_out = jxl_strdup(params->alloc, message);
}

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

static jxl_status_t jxl_render_display_keyframe_inner(const jxl_keyframe_render_params *params,
                                                      jxl_render *r, int populate_animation_refs);

static jxl_status_t ensure_animation_refs(const jxl_keyframe_render_params *params,
                                          jxl_reference_store *refs,
                                          jxl_progressive_lf_store *lf_store,
                                          uint32_t target_keyframe_index,
                                          const jxl_parsed_image_header *parsed) {
    uint32_t k;
    uint32_t start;
    uint32_t color_planes;
    uint32_t plane_count;
    jxl_render *temp;
    jxl_modular_region frame_region;
    jxl_status_t st;
    if (params == NULL || refs == NULL || lf_store == NULL || parsed == NULL ||
        target_keyframe_index == 0u) {
        return JXL_OK;
    }

    color_planes = parsed->xyb_encoded ? 3u : 1u;
    if (parsed->colour.colour_space == JXL_COLOUR_SPACE_RGB_I) {
        color_planes = 3u;
    }
    plane_count = color_planes + (uint32_t)parsed->num_extra_channels;
    if (plane_count == 0 || parsed->size.width == 0 || parsed->size.height == 0) {
        return JXL_ERROR_INVALID_INPUT;
    }

    temp =
        jxl_render_create(params->alloc, plane_count, color_planes, parsed->size.width,
                          parsed->size.height);
    if (temp == NULL) {
        return JXL_ERROR_OUT_OF_MEMORY;
    }

    frame_region =
        jxl_modular_region_with_size(parsed->size.width, parsed->size.height);
    st = JXL_OK;

    start = 0;
    if (params->animation_chain_upto != NULL) {
        if (*params->animation_chain_upto != UINT32_MAX) {
            start = *params->animation_chain_upto + 1u;
        }
    }

    for (k = start; k < target_keyframe_index; ++k) {
        jxl_render_init_all_planes(temp, &frame_region);
        jxl_keyframe_render_params prior = *params;
        prior.keyframe_index = k;
        prior.parsed_out = NULL;
        prior.external_refs = refs;
        prior.external_lf_store = lf_store;
        st = jxl_render_display_keyframe_inner(&prior, temp, 0);
        if (st != JXL_OK) {
            break;
        }
    }

    jxl_render_free(params->alloc, temp);
    return st;
}

jxl_status_t jxl_render_display_keyframe(const jxl_keyframe_render_params *params, jxl_render *r) {
    return jxl_render_display_keyframe_inner(params, r, 1);
}

static jxl_status_t jxl_render_display_keyframe_inner(const jxl_keyframe_render_params *params,
                                                      jxl_render *r, int populate_animation_refs) {
    size_t cs_len;
    jxl_parsed_image_header parsed_local;
    int parsed_owned;
    jxl_bs bs;
    jxl_frame keyframe;
    jxl_reference_store refs_local;
    int refs_owned;
    jxl_progressive_lf_store lf_store_local;
    int lf_store_owned;
    int prefer_canvas_base;
    jxl_bs modular_bs;
    jxl_render_frame_params frame_params = {0};
    int owns_codestream;
    const jxl_parsed_image_header *parsed;
    const uint8_t *codestream;
    uint8_t *codestream_owned = NULL;
    jxl_status_t st = JXL_OK;
    jxl_reference_store *refs;
    jxl_progressive_lf_store *lf_store;
    jxl_bs *modular_bs_ptr;
    if (params == NULL || params->alloc == NULL || r == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }

    codestream = params->codestream;
    cs_len = params->codestream_len;
    owns_codestream = 0;
    if (codestream == NULL) {
        st = jxl_collect_codestream(params->alloc, params->input, params->input_len,
                                    &codestream_owned, &cs_len);
        if (st != JXL_OK) {
            return st;
        }
        codestream = codestream_owned;
        owns_codestream = 1;
    }

    parsed_owned = 0;
    if (params->parsed_header != NULL) {
        parsed = params->parsed_header;
        jxl_bs_init_at_bit(&bs, codestream, cs_len, params->frames_bitstream_offset * 8);
    } else {
        parsed_owned = 1;
        parsed = &parsed_local;
        memset(&parsed_local, 0, sizeof(parsed_local));
        jxl_bs_init(&bs, codestream, cs_len);
        if (jxl_image_header_parse(&bs, &parsed_local) != JXL_BS_OK) {
            if (owns_codestream) {
                jxl_free(params->alloc, codestream_owned);
            }
            jxl_render_set_error((jxl_keyframe_render_params *)params,
                                 "failed to parse image header for render");
            return JXL_ERROR_INVALID_INPUT;
        }
        if (jxl_image_decode_post_header(params->alloc, &bs, &parsed_local) != JXL_BS_OK) {
            if (owns_codestream) {
                jxl_free(params->alloc, codestream_owned);
            }
            jxl_parsed_image_header_free_embedded_icc(params->alloc, &parsed_local);
            jxl_render_set_error((jxl_keyframe_render_params *)params,
                                 "failed to decode image header tail for render");
            return JXL_ERROR_INVALID_INPUT;
        }
    }

    jxl_frame_init(&keyframe);
    {
        jxl_frame_status_t fst =
            jxl_frame_parse_nth_keyframe(params->alloc, &bs, parsed, codestream, cs_len,
                                         params->keyframe_index, &keyframe);
        st = frame_to_status(fst);
        if (st != JXL_OK) {
            jxl_frame_free(params->alloc, &keyframe);
            if (parsed_owned) {
                jxl_parsed_image_header_free_embedded_icc(params->alloc, &parsed_local);
            }
            if (owns_codestream) {
                jxl_free(params->alloc, codestream_owned);
            }
            jxl_render_set_error((jxl_keyframe_render_params *)params,
                                 "failed to parse keyframe for render");
            return st;
        }
    }

    {
        size_t meta_end = bs.num_read_bits / 8;
        size_t consumed;
        if (meta_end > cs_len || keyframe.toc.total_size > cs_len - meta_end) {
            jxl_frame_free(params->alloc, &keyframe);
            if (parsed_owned) {
                jxl_parsed_image_header_free_embedded_icc(params->alloc, &parsed_local);
            }
            if (owns_codestream) {
                jxl_free(params->alloc, codestream_owned);
            }
            jxl_render_set_error((jxl_keyframe_render_params *)params,
                                 "keyframe metadata exceeds codestream");
            return JXL_ERROR_INVALID_INPUT;
        }
        consumed = 0;
        jxl_frame_feed_bytes(&keyframe, codestream + meta_end, keyframe.toc.total_size, &consumed);
        if (consumed != keyframe.toc.total_size || !jxl_frame_is_loading_done(&keyframe)) {
            jxl_frame_free(params->alloc, &keyframe);
            if (parsed_owned) {
                jxl_parsed_image_header_free_embedded_icc(params->alloc, &parsed_local);
            }
            if (owns_codestream) {
                jxl_free(params->alloc, codestream_owned);
            }
            jxl_render_set_error((jxl_keyframe_render_params *)params,
                                 "incomplete keyframe group payloads");
            return JXL_ERROR_INVALID_INPUT;
        }
    }

    refs = params->external_refs;
    refs_owned = 0;
    if (refs == NULL) {
        jxl_reference_store_init(&refs_local);
        refs = &refs_local;
        refs_owned = 1;
    }
    lf_store = params->external_lf_store;
    lf_store_owned = 0;
    if (lf_store == NULL) {
        jxl_progressive_lf_store_init(&lf_store_local);
        lf_store = &lf_store_local;
        lf_store_owned = 1;
    }
    prefer_canvas_base = 0;
    if (params->keyframe_index > 0u && !keyframe.header.resets_canvas) {
        if (populate_animation_refs) {
            st = ensure_animation_refs(params, refs, lf_store, params->keyframe_index, parsed);
            if (st != JXL_OK) {
                if (parsed_owned && params->parsed_out == NULL) {
                    jxl_parsed_image_header_free_embedded_icc(params->alloc, &parsed_local);
                }
                if (lf_store_owned) {
                    jxl_progressive_lf_store_free(params->alloc, lf_store);
                }
                if (refs_owned) {
                    jxl_reference_store_free(params->alloc, refs);
                }
                jxl_frame_free(params->alloc, &keyframe);
                if (owns_codestream) {
                    jxl_free(params->alloc, codestream_owned);
                }
                jxl_render_set_error((jxl_keyframe_render_params *)params,
                                     "failed to populate animation reference frames");
                return st;
            }
        }
        /* Rust blend uses reference_grids[blending_info.source], not the prior keyframe. */
    }

    if (refs_owned) {
        st = jxl_decode_prerequisite_frames(params->ctx, params->alloc, params->input,
                                            params->input_len, refs, lf_store,
                                            params->keyframe_index, codestream, cs_len);
        if (st != JXL_OK) {
            if (lf_store_owned) {
                jxl_progressive_lf_store_free(params->alloc, lf_store);
            }
            if (refs_owned) {
                jxl_reference_store_free(params->alloc, refs);
            }
            jxl_frame_free(params->alloc, &keyframe);
            if (parsed_owned && params->parsed_out == NULL) {
                jxl_parsed_image_header_free_embedded_icc(params->alloc, &parsed_local);
            }
            if (owns_codestream) {
                jxl_free(params->alloc, codestream_owned);
            }
            jxl_render_set_error((jxl_keyframe_render_params *)params,
                                 "failed to decode prerequisite frames");
            return st;
        }
    }

    modular_bs_ptr = NULL;
    if (keyframe.header.encoding == JXL_FRAME_ENCODING_MODULAR) {
        if (params->parsed_header != NULL) {
            jxl_bs_init_at_bit(&modular_bs, codestream, cs_len,
                               params->frames_bitstream_offset * 8);
        } else {
            jxl_bs_init(&modular_bs, codestream, cs_len);
            jxl_parsed_image_header_free_embedded_icc(params->alloc, &parsed_local);
            if (jxl_image_header_parse(&modular_bs, &parsed_local) != JXL_BS_OK ||
                jxl_image_decode_post_header(params->alloc, &modular_bs, &parsed_local) !=
                    JXL_BS_OK) {
                if (lf_store_owned) {
                    jxl_progressive_lf_store_free(params->alloc, lf_store);
                }
                if (refs_owned) {
                    jxl_reference_store_free(params->alloc, refs);
                }
                jxl_frame_free(params->alloc, &keyframe);
                jxl_parsed_image_header_free_embedded_icc(params->alloc, &parsed_local);
                if (owns_codestream) {
                    jxl_free(params->alloc, codestream_owned);
                }
                return JXL_ERROR_INVALID_INPUT;
            }
        }
        modular_bs_ptr = &modular_bs;
    }

    if (params->parsed_out != NULL && parsed_owned) {
        *params->parsed_out = parsed_local;
    }

    r->keyframe_index = params->keyframe_index;
    r->duration = keyframe.header.duration;

    frame_params.params = params;
    frame_params.parsed = parsed;
    frame_params.frame = &keyframe;
    frame_params.codestream = codestream;
    frame_params.codestream_len = cs_len;
    frame_params.modular_bitstream = modular_bs_ptr;
    frame_params.refs = refs;
    frame_params.lf_store = lf_store;
    frame_params.prefer_canvas_base = prefer_canvas_base;
    frame_params.visible_frames = params->keyframe_index + 1u;
    frame_params.invisible_frames = 0u;

    st = jxl_render_frame(&frame_params, r);

    if (st == JXL_OK && params->animation_chain_upto != NULL) {
        *params->animation_chain_upto = params->keyframe_index;
    }

    if (parsed_owned && params->parsed_out == NULL) {
        jxl_parsed_image_header_free_embedded_icc(params->alloc, &parsed_local);
    }

    if (lf_store_owned) {
        jxl_progressive_lf_store_free(params->alloc, lf_store);
    }
    if (refs_owned) {
        jxl_reference_store_free(params->alloc, refs);
    }
    jxl_frame_free(params->alloc, &keyframe);
    if (owns_codestream) {
        jxl_free(params->alloc, codestream_owned);
    }
    return st;
}
