// SPDX-License-Identifier: MIT OR Apache-2.0
#include "frame_render.h"

#include "bitstream/bitstream.h"
#include "codestream_collect.h"
#include "frame/frame.h"
#include "frame/frame_header.h"
#include "image/image_internal.h"
#include "render/render_buffer.h"
#include "render/render_frame.h"
#include "render/modular_compose.h"
#include "render/vardct/vardct_encode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_error(jxl_vardct_render_params *params, const char *message) {
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

jxl_status_t jxl_render_vardct_frame(const jxl_vardct_render_params *params, struct jxl_render *r) {
    int owns_codestream;
    jxl_frame frame_local;
    jxl_vardct_encode_ctx enc = {0};
    jxl_parsed_image_header parsed;
    jxl_reference_store ref_store_local;
    int ref_store_owned;
    jxl_progressive_lf_store lf_store_local;
    int lf_store_owned;
    jxl_status_t st;
    uint8_t *codestream_alloc;
    const uint8_t *codestream;
    size_t cs_len;
    jxl_frame *frame;
    int frame_owned;
    jxl_reference_store *ref_store;
    jxl_progressive_lf_store *lf_store;
    if (params == NULL || params->alloc == NULL || r == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    if (params->loaded_frame == NULL && params->input == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }

    st = JXL_ERROR_UNSUPPORTED;
    codestream_alloc = NULL;
    codestream = params->codestream;
    cs_len = params->codestream_len;
    owns_codestream = 0;
    frame = params->loaded_frame != NULL ? params->loaded_frame : &frame_local;
    frame_owned = params->loaded_frame == NULL;
    if (frame_owned) {
        jxl_frame_init(frame);
    }
    jxl_vardct_encode_ctx_init(&enc);

    if (params->parsed_header != NULL) {
        parsed = *params->parsed_header;
    } else {
        memset(&parsed, 0, sizeof(parsed));
    }
    ref_store = params->external_refs;
    ref_store_owned = 0;
    if (ref_store == NULL) {
        jxl_reference_store_init(&ref_store_local);
        ref_store = &ref_store_local;
        ref_store_owned = 1;
    }
    lf_store = params->external_lf_store;
    lf_store_owned = 0;
    if (lf_store == NULL) {
        jxl_progressive_lf_store_init(&lf_store_local);
        lf_store = &lf_store_local;
        lf_store_owned = 1;
    }

    if (params->loaded_frame == NULL) {
        jxl_bs bs;
        size_t consumed;
        size_t meta_end;
        size_t payload_avail;
        if (ref_store_owned) {
            st = jxl_decode_prerequisite_frames(params->ctx, params->alloc, params->input,
                                                params->input_len, ref_store, lf_store, 0,
                                                params->codestream, params->codestream_len);
            if (st != JXL_OK) {
                set_error((jxl_vardct_render_params *)params,
                          "failed to decode prerequisite frames");
                goto cleanup;
            }
        }

        st = jxl_collect_codestream(params->alloc, params->input, params->input_len,
                                    &codestream_alloc, &cs_len);
        if (st != JXL_OK) {
            goto cleanup;
        }
        codestream = codestream_alloc;
        owns_codestream = 1;

        jxl_bs_init(&bs, codestream, cs_len);
        if (jxl_image_header_parse(&bs, &parsed) != JXL_BS_OK) {
            st = JXL_ERROR_INVALID_INPUT;
            set_error((jxl_vardct_render_params *)params,
                      "failed to parse image header for vardct render");
            goto cleanup;
        }
        if (jxl_image_skip_post_header(params->alloc, &bs, &parsed) != JXL_BS_OK) {
            st = JXL_ERROR_INVALID_INPUT;
            set_error((jxl_vardct_render_params *)params,
                      "failed to skip image header tail for vardct render");
            goto cleanup;
        }

        {
            jxl_frame_status_t fst = jxl_frame_parse_keyframe(params->alloc, &bs, &parsed,
                                                              codestream, cs_len, frame);
            st = frame_to_status(fst);
            if (st != JXL_OK) {
                set_error((jxl_vardct_render_params *)params,
                          "failed to parse frame for vardct render");
                goto cleanup;
            }
        }

        if (frame->header.encoding != JXL_FRAME_ENCODING_VARDCT) {
            st = JXL_ERROR_INVALID_INPUT;
            set_error((jxl_vardct_render_params *)params,
                      "internal error: vardct render called on modular frame");
            goto cleanup;
        }

        meta_end = bs.num_read_bits / 8;
        if (meta_end > cs_len) {
            st = JXL_ERROR_INVALID_INPUT;
            set_error((jxl_vardct_render_params *)params, "frame metadata exceeds codestream");
            goto cleanup;
        }
        payload_avail = cs_len - meta_end;
        if (frame->toc.total_size > payload_avail) {
            st = JXL_ERROR_INVALID_INPUT;
            set_error((jxl_vardct_render_params *)params, "frame TOC payload exceeds codestream");
            goto cleanup;
        }

        consumed = 0;
        jxl_frame_feed_bytes(frame, codestream + meta_end, frame->toc.total_size, &consumed);
        if (consumed != frame->toc.total_size || !jxl_frame_is_loading_done(frame)) {
            st = JXL_ERROR_INVALID_INPUT;
            set_error((jxl_vardct_render_params *)params, "incomplete frame group payloads");
            goto cleanup;
        }
    } else {
        if (params->parsed_header == NULL || codestream == NULL) {
            st = JXL_ERROR_INVALID_INPUT;
            set_error((jxl_vardct_render_params *)params,
                      "loaded vardct frame requires parsed header and codestream");
            goto cleanup;
        }
        if (frame->header.encoding != JXL_FRAME_ENCODING_VARDCT) {
            st = JXL_ERROR_INVALID_INPUT;
            set_error((jxl_vardct_render_params *)params,
                      "internal error: vardct render called on modular frame");
            goto cleanup;
        }
        if (!jxl_frame_is_loading_done(frame)) {
            st = JXL_ERROR_INVALID_INPUT;
            set_error((jxl_vardct_render_params *)params, "loaded vardct frame is incomplete");
            goto cleanup;
        }
    }
    st = jxl_vardct_encode_frame(params, frame, &parsed, lf_store, params->filter_region, &enc);
    if (st != JXL_OK) {
        goto cleanup;
    }

    st = jxl_render_vardct_apply_color_filters(params->ctx, params->alloc, &parsed, &frame->header,
                                               &enc, params->output_region);
    if (st != JXL_OK) {
        goto cleanup;
    }

    st = jxl_render_post_encode_from_vardct_ctx(params->ctx, params->alloc, &parsed, &frame->header,
                                                &enc, params->output_region, NULL, ref_store,
                                                params->ref_image_output, 1u, 0u, r);
    if (st != JXL_OK) {
        set_error((jxl_vardct_render_params *)params, "failed post-encode render stage");
        goto cleanup;
    }
    st = jxl_render_convert_color_for_record(params->ctx, params->alloc, &parsed, &frame->header, r,
                                             params->ref_image_output);
    if (st != JXL_OK) {
        goto cleanup;
    }

    if (JXL_DEBUG_FLAG(params->ctx, debug_fb) && r->planes[0] != NULL) {
        uint32_t dx = 2560u;
        uint32_t dy = 1200u;
        if (dy < r->height && dx < r->width) {
            size_t pidx = (size_t)dy * r->width + dx;
            fprintf(stderr, "render@%u,%u x=%g y=%g b=%g\n", dx, dy, r->planes[0][pidx],
                    r->planes[1][pidx], r->planes[2][pidx]);
        }
    }

    st = JXL_OK;
    if (params->error_out != NULL && *params->error_out != NULL) {
        jxl_free(params->alloc, *params->error_out);
        *params->error_out = NULL;
    }

cleanup:
    jxl_vardct_encode_ctx_free(params->alloc, &enc);

    if (frame_owned) {
        jxl_frame_free(params->alloc, frame);
    }
    if (owns_codestream) {
        jxl_free(params->alloc, codestream_alloc);
    }
    if (lf_store_owned) {
        jxl_progressive_lf_store_free(params->alloc, lf_store);
    }
    if (ref_store_owned) {
        jxl_reference_store_free(params->alloc, ref_store);
    }
    return st;
}

static jxl_status_t render_planes_to_ref_image(jxl_allocator_state *alloc, struct jxl_render *r,
                                                 jxl_ref_image *out) {
    uint32_t p;
    uint32_t np;
    float **planes;
    size_t pixels;
    if (r == NULL || out == NULL || r->planes[0] == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    np = r->num_planes < 3u ? r->num_planes : 3u;
    planes = jxl_alloc(alloc, np * sizeof(float *));
    if (planes == NULL) {
        return JXL_ERROR_OUT_OF_MEMORY;
    }
    memset(planes, 0, np * sizeof(float *));
    pixels = (size_t)r->width * (size_t)r->height;
    for (p = 0; p < np; ++p) {
        planes[p] = jxl_alloc(alloc, pixels * sizeof(float));
        if (planes[p] == NULL) {
            uint32_t k;
            for (k = 0; k < p; ++k) {
                jxl_free(alloc, planes[k]);
            }
            jxl_free(alloc, planes);
            return JXL_ERROR_OUT_OF_MEMORY;
        }
        if (r->planes[p] != NULL) {
            memcpy(planes[p], r->planes[p], pixels * sizeof(float));
        }
    }
    out->valid = 1;
    out->width = r->width;
    out->height = r->height;
    out->num_planes = np;
    out->samples = NULL;
    out->planes = planes;
    for (p = 0; p < 3; ++p) {
        out->plane_w[p] = r->width;
        out->plane_h[p] = r->height;
    }
    return JXL_OK;
}

jxl_status_t jxl_render_vardct_prereq_to_ref(const jxl_vardct_render_params *params,
                                             const jxl_parsed_image_header *parsed,
                                             jxl_frame *frame, jxl_ref_image *out) {
                                                 uint32_t p;
    struct jxl_render r;
    jxl_vardct_render_params vp;
    uint32_t w;
    uint32_t h;
    uint32_t np;
    size_t plane_pixels;
    float *samples;
    float *plane_ptrs[4] = {NULL, NULL, NULL, NULL};
    jxl_status_t st;
    if (params == NULL || params->alloc == NULL || parsed == NULL || frame == NULL ||
        out == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }

    w = jxl_frame_header_color_sample_width(&frame->header);
    h = jxl_frame_header_color_sample_height(&frame->header);
    np = frame->header.encoded_color_channels >= 3u
                      ? 3u
                      : frame->header.encoded_color_channels > 0u
                            ? frame->header.encoded_color_channels
                            : 1u;
    plane_pixels = (size_t)w * (size_t)h;
    samples = jxl_alloc(params->alloc, plane_pixels * np * sizeof(float));
    if (samples == NULL) {
        return JXL_ERROR_OUT_OF_MEMORY;
    }
    for (p = 0; p < np; ++p) {
        plane_ptrs[p] = samples + (size_t)p * plane_pixels;
    }

    memset(&r, 0, sizeof(r));
    r.width = w;
    r.height = h;
    r.num_planes = np;
    for (p = 0; p < np; ++p) {
        r.planes[p] = plane_ptrs[p];
    }

    vp = *params;
    vp.parsed_header = parsed;
    vp.loaded_frame = frame;
    vp.ref_image_output = 1;

    st = jxl_render_vardct_frame(&vp, &r);
    if (st == JXL_OK) {
        st = render_planes_to_ref_image(params->alloc, &r, out);
        if (st == JXL_OK) {
            jxl_ref_image_set_crop_from_frame(out, &frame->header);
        }
    }
    jxl_free(params->alloc, samples);
    return st;
}

jxl_status_t jxl_render_compose_vardct_prereq(const jxl_vardct_render_params *params,
                                              const jxl_parsed_image_header *parsed,
                                              jxl_frame *frame, jxl_reference_store *refs,
                                              jxl_render *canvas) {
    jxl_vardct_encode_ctx enc = {0};
    jxl_vardct_render_params vp;
    const jxl_frame_header *fh;
    uint32_t fw;
    uint32_t fh_h;
    jxl_render *local;
    jxl_modular_region frame_region;
    jxl_status_t st;
    if (params == NULL || params->alloc == NULL || parsed == NULL || frame == NULL ||
        canvas == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }

    fh = &frame->header;
    fw = jxl_frame_header_color_sample_width(fh);
    fh_h = jxl_frame_header_color_sample_height(fh);

    jxl_vardct_encode_ctx_init(&enc);

    local =
        jxl_render_create(params->alloc, canvas->num_planes, canvas->color_planes, fw, fh_h);
    if (local == NULL) {
        jxl_vardct_encode_ctx_free(params->alloc, &enc);
        return JXL_ERROR_OUT_OF_MEMORY;
    }
    frame_region = jxl_modular_region_with_size(fw, fh_h);
    jxl_render_init_all_planes(local, &frame_region);

    vp = *params;
    vp.parsed_header = parsed;
    vp.loaded_frame = frame;

    st = jxl_vardct_encode_frame(&vp, frame, parsed, params->external_lf_store, NULL,
                                              &enc);
    if (st == JXL_OK) {
        st = jxl_render_vardct_apply_color_filters(params->ctx, params->alloc, parsed, fh, &enc,
                                                   params->output_region);
    }
    if (st == JXL_OK) {
        st = jxl_render_post_encode_from_vardct_ctx(params->ctx, params->alloc, parsed, fh, &enc,
                                                    NULL, NULL, refs, 1, 1u, 0u, local);
    }
    if (st == JXL_OK) {
        st = jxl_render_convert_color_for_record(params->ctx, params->alloc, parsed, fh, local, 1);
    }

    if (st == JXL_OK) {
        jxl_modular_compose_params cp = {0};
        uint32_t color_planes = fh->encoded_color_channels >= 3u
                                    ? 3u
                                    : fh->encoded_color_channels > 0u
                                          ? fh->encoded_color_channels
                                          : 1u;
        if (parsed->xyb_encoded ||
            parsed->colour.colour_space == JXL_COLOUR_SPACE_RGB_I) {
            color_planes = 3u;
        }

        cp.ctx = params->ctx;
        cp.alloc = params->alloc;
        cp.parsed = parsed;
        cp.fh = fh;
        cp.dest = NULL;
        cp.bit_depth = parsed->bit_depth_bits;
        cp.num_color_channels = color_planes;
        cp.num_extra_channels = (uint32_t)parsed->num_extra_channels;
        cp.output_region = params->output_region;
        cp.prefer_canvas_base = 0;

        st = jxl_render_composite_local_frame(&cp, local, refs, canvas);
    }

    jxl_render_free(params->alloc, local);
    jxl_vardct_encode_ctx_free(params->alloc, &enc);
    return st;
}
