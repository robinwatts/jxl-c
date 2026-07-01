// SPDX-License-Identifier: MIT OR Apache-2.0
#include "jxl_oxide/jxl_oxide.h"

#include "allocator.h"
#include "aux_box.h"
#include "codestream_collect.h"
#include "container_reader.h"
#include "context.h"
#include "deps_check.h"
#include "frame/frame.h"
#include "frame/frame_header.h"
#include "image/icc_parse.h"
#include "image/image_internal.h"
#include "modular/image.h"
#include "modular/region.h"
#include "render/cms_lcms.h"
#include "image/icc_parse.h"
#include "render/color_encoding_util.h"
#include "render/color_transform.h"
#include "render/color_transform_apply.h"
#include "render/filter/ycbcr.h"
#include "render/render_buffer.h"
#include "render/render_internal.h"
#include "render/render_util.h"
#include "frame/filter.h"

jxl_status_t jxl_decoder_init_from_codestream(jxl_allocator_state *alloc,
                                              const uint8_t *codestream, size_t codestream_len,
                                              jxl_image_header *header_out,
                                              uint32_t *num_color_channels_out,
                                              jxl_image_geometry *geometry_out,
                                              int *xyb_encoded_out,
                                              jxl_parsed_image_header *parsed_out,
                                              size_t *frames_bit_offset_out);
jxl_status_t jxl_decoder_init_from_input(jxl_allocator_state *alloc, const uint8_t *input,
                                         size_t input_len, jxl_image_header *header_out,
                                         uint32_t *num_color_channels_out,
                                         jxl_image_geometry *geometry_out, int *xyb_encoded_out,
                                         char **error_out);

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "jxl_oxide/jxl_types.h"
#include <string.h>

static void crop_size_with_orientation(uint32_t orientation, uint32_t w, uint32_t h,
                                       uint32_t *out_w, uint32_t *out_h) {
    if (orientation >= 5u && orientation <= 8u) {
        *out_w = h;
        *out_h = w;
    } else {
        *out_w = w;
        *out_h = h;
    }
}

static void metadata_apply_orientation(uint32_t orientation, uint32_t width, uint32_t height,
                                       int32_t in_left, int32_t in_top, int inverse,
                                       int32_t *out_left, int32_t *out_top) {
    int32_t w = (int32_t)width;
    int32_t h = (int32_t)height;
    int32_t left = in_left;
    int32_t top = in_top;

    switch (orientation) {
    case 1:
        break;
    case 2:
        left = w - left - 1;
        break;
    case 3:
        left = w - left - 1;
        top = h - top - 1;
        break;
    case 4:
        top = h - top - 1;
        break;
    case 5:
        left = top;
        top = in_left;
        break;
    case 6:
        if (inverse) {
            left = top;
            top = w - in_left - 1;
        } else {
            left = h - top - 1;
            top = in_left;
        }
        break;
    case 7:
        left = h - top - 1;
        top = w - in_left - 1;
        break;
    case 8:
        if (inverse) {
            left = h - top - 1;
            top = in_left;
        } else {
            left = top;
            top = w - in_left - 1;
        }
        break;
    default:
        break;
    }

    *out_left = left;
    *out_top = top;
}

/* Maps display-space crop (Rust CropInfo / public header dimensions) to codestream space. */
static jxl_crop crop_display_to_codestream(uint32_t orientation, uint32_t cs_w, uint32_t cs_h,
                                           const jxl_crop *display) {
    uint32_t disp_w;
    uint32_t disp_h;
    int32_t left0;
    int32_t top0;
    int32_t left1;
    int32_t top1;
    jxl_crop out;
    if (orientation == 1u) {
        return *display;
    }

    disp_w = 0;
    disp_h = 0;
    crop_size_with_orientation(orientation, cs_w, cs_h, &disp_w, &disp_h);

    left0 = 0;
    top0 = 0;
    left1 = 0;
    top1 = 0;
    metadata_apply_orientation(orientation, disp_w, disp_h, (int32_t)display->left,
                               (int32_t)display->top, 1, &left0, &top0);
    metadata_apply_orientation(orientation, disp_w, disp_h,
                               (int32_t)display->left + (int32_t)display->width - 1,
                               (int32_t)display->top + (int32_t)display->height - 1, 1, &left1,
                               &top1);

    if (left0 > left1) {
        int32_t tmp = left0;
        left0 = left1;
        left1 = tmp;
    }
    if (top0 > top1) {
        int32_t tmp = top0;
        top0 = top1;
        top1 = tmp;
    }

    out.width = (uint32_t)(left1 - left0 + 1);
    out.height = (uint32_t)(top1 - top0 + 1);
    out.left = (uint32_t)left0;
    out.top = (uint32_t)top0;

    return out;
}
#include <string.h>

#include "render/patch_render.h"

struct jxl_decoder {
    jxl_context *ctx;
    char *last_error;
    uint8_t *input;
    size_t input_len;
    size_t input_cap;
    jxl_container_reader *reader;
    int initialized;
    jxl_image_header header;
    jxl_parsed_image_header parsed_header;
    size_t frames_bitstream_offset;
    uint32_t codestream_width;
    uint32_t codestream_height;
    uint32_t orientation;
    uint32_t num_color_channels;
    int xyb_encoded;
    jxl_crop crop;
    int has_crop;
    uint8_t *requested_icc;
    size_t requested_icc_len;
    jxl_color_encoding requested_color_encoding;
    int has_requested_color_encoding;
    jxl_reference_store animation_refs;
    jxl_progressive_lf_store animation_lf_store;
    uint32_t animation_chain_upto;
    int animation_cache_init;
};

static jxl_allocator_state *dec_alloc(jxl_decoder *dec) {
    /* Non-owning borrow of the context allocator (no per-decoder embed). */
    return jxl_context_alloc_state(dec->ctx);
}

typedef struct {
    int do_ycbcr;
    uint32_t encoded_color_channels;
    int needs_filter_padding;
} jxl_decoder_frame_hint;

static int decoder_borrow_codestream(const jxl_decoder *dec, const uint8_t **cs_out,
                                     size_t *cs_len_out) {
    if (dec == NULL || dec->reader == NULL || cs_out == NULL || cs_len_out == NULL) {
        return 0;
    }
    *cs_out = jxl_container_reader_codestream(dec->reader, cs_len_out);
    return *cs_out != NULL && *cs_len_out >= 3;
}

static int decoder_peek_first_frame(jxl_allocator_state *alloc, const uint8_t *codestream,
                                    size_t codestream_len, jxl_decoder_frame_hint *out) {
    jxl_bs bs;
    jxl_parsed_image_header parsed;
    jxl_frame_header fh;
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
    if (codestream == NULL || codestream_len < 3) {
        return 0;
    }

    jxl_bs_init(&bs, codestream, codestream_len);
    memset(&parsed, 0, sizeof(parsed));
    if (jxl_image_header_parse(&bs, &parsed) != JXL_BS_OK) {
        return 0;
    }
    if (jxl_image_skip_post_header(alloc, &bs, &parsed) != JXL_BS_OK) {
        return 0;
    }

    jxl_frame_header_init(&fh);
    if (jxl_frame_header_parse(alloc, &bs, &parsed, &fh) != JXL_FRAME_OK) {
        return 0;
    }
    if (out != NULL) {
        out->do_ycbcr = fh.do_ycbcr;
        out->encoded_color_channels = fh.encoded_color_channels;
        out->needs_filter_padding =
            jxl_epf_enabled(&fh.restoration) || jxl_gabor_enabled(&fh.restoration);
    }
    jxl_frame_header_free(alloc, &fh);
    return 1;
}

static int decoder_parse_for_padding(jxl_allocator_state *alloc, const uint8_t *codestream,
                                     size_t codestream_len, jxl_parsed_image_header *parsed,
                                     jxl_frame_header *fh) {
    jxl_bs bs;
    if (alloc == NULL || codestream == NULL || parsed == NULL || fh == NULL ||
        codestream_len < 3) {
        return 0;
    }

    jxl_bs_init(&bs, codestream, codestream_len);
    memset(parsed, 0, sizeof(*parsed));
    if (jxl_image_header_parse(&bs, parsed) != JXL_BS_OK) {
        return 0;
    }
    if (jxl_image_decode_post_header(alloc, &bs, parsed) != JXL_BS_OK) {
        return 0;
    }

    jxl_frame_header_init(fh);
    if (jxl_frame_header_parse(alloc, &bs, parsed, fh) != JXL_FRAME_OK) {
        return 0;
    }
    return 1;
}

static jxl_status_t decoder_materialize_render_f32(jxl_decoder *dec, jxl_render *r) {
    if (dec == NULL || r == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    return jxl_render_ensure_all_planes_f32(dec_alloc(dec), r, &dec->parsed_header);
}

static jxl_render *decoder_extract_render_subregion(jxl_allocator_state *alloc, jxl_render *src,
                                                    const jxl_modular_region *inner,
                                                    const jxl_modular_region *outer) {
    uint32_t p;
    int32_t dx;
    int32_t dy;
    jxl_render *dst;
    if (alloc == NULL || src == NULL || inner == NULL || outer == NULL || inner->width == 0 ||
        inner->height == 0) {
        return NULL;
    }
    dx = inner->left - outer->left;
    dy = inner->top - outer->top;
    if (dx < 0 || dy < 0 || (uint32_t)dx + inner->width > outer->width ||
        (uint32_t)dy + inner->height > outer->height) {
        return NULL;
    }

    dst = jxl_render_create(alloc, src->num_planes, src->color_planes, inner->width,
                            inner->height);
    if (dst == NULL) {
        return NULL;
    }
    jxl_render_init_all_planes(dst, inner);

    for (p = 0; p < src->num_planes; ++p) {
        uint32_t y;
        if (src->planes[p] == NULL || dst->planes[p] == NULL) {
            continue;
        }
        for (y = 0; y < inner->height; ++y) {
            memcpy(dst->planes[p] + (size_t)y * inner->width,
                   src->planes[p] + (size_t)(dy + (int32_t)y) * src->width + (size_t)dx,
                   inner->width * sizeof(float));
        }
        if (src->meta != NULL && dst->meta != NULL) {
            dst->meta[p] = src->meta[p];
            dst->meta[p].region = *inner;
            dst->meta[p].buf_width = inner->width;
            dst->meta[p].buf_height = inner->height;
        }
    }
    jxl_render_free(alloc, src);
    return dst;
}

static int decoder_target_matches_frame_encoding(const jxl_parsed_image_header *parsed,
                                                 const uint8_t *target_icc,
                                                 size_t target_icc_len) {
    jxl_color_encoding enc;
    jxl_colour_encoding_parsed target_parsed;
    if (parsed == NULL || target_icc == NULL || target_icc_len == 0) {
        return 0;
    }
    if (parsed->colour.have_icc_profile) {
        return parsed->embedded_icc != NULL && parsed->embedded_icc_len == target_icc_len &&
               memcmp(parsed->embedded_icc, target_icc, target_icc_len) == 0;
    }
    if (jxl_icc_parse_color_encoding(target_icc, target_icc_len, &enc) != JXL_OK) {
        return 0;
    }
    if (jxl_color_encoding_to_parsed(&enc, &target_parsed) != JXL_OK) {
        return 0;
    }
    return jxl_colour_encoding_parsed_equivalent(&parsed->colour, &target_parsed);
}

static uint32_t decoder_postprocess_output_channels(const jxl_parsed_image_header *parsed,
                                                    const jxl_decoder_frame_hint *fh) {
    if (parsed->colour.colour_space == JXL_COLOUR_SPACE_GRAY_I ||
        fh->encoded_color_channels < 3u) {
        uint32_t keep = fh->encoded_color_channels;
        return keep != 0u ? keep : 1u;
    }
    return fh->encoded_color_channels != 0u ? fh->encoded_color_channels : 3u;
}

static jxl_status_t decoder_finish_postprocess_planes(jxl_allocator_state *alloc, jxl_render *r,
                                                      const jxl_parsed_image_header *parsed,
                                                      const jxl_decoder_frame_hint *fh) {
    uint32_t keep = decoder_postprocess_output_channels(parsed, fh);
    if (keep >= r->color_planes) {
        return JXL_OK;
    }
    return jxl_render_remove_color_planes(alloc, r, keep);
}

static int decoder_postprocess_transform_is_noop(const jxl_parsed_image_header *parsed,
                                                 const uint8_t *target_icc,
                                                 size_t target_icc_len,
                                                 const jxl_decoder_frame_hint *fh) {
    if (!fh->do_ycbcr && parsed->bit_depth_bits >= 31) {
        return 1;
    }
    if (decoder_target_matches_frame_encoding(parsed, target_icc, target_icc_len)) {
        return 1;
    }
    if (!parsed->colour.have_icc_profile && !parsed->xyb_encoded) {
        return 1;
    }
    return 0;
}

static jxl_status_t decoder_postprocess_keyframe(jxl_decoder *dec, jxl_render *r,
                                               const jxl_parsed_image_header *parsed,
                                               const jxl_decoder_frame_hint *fh) {
    const uint8_t *target_icc;
    size_t target_icc_len;
    size_t px_count;
    uint32_t cms_planes;
    int transform_noop;
    if (dec == NULL || r == NULL || parsed == NULL || fh == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    target_icc = dec->requested_icc;
    target_icc_len = dec->requested_icc_len;
    if (target_icc == NULL || target_icc_len == 0) {
        return JXL_OK;
    }
    px_count = (size_t)r->width * (size_t)r->height;
    if (px_count == 0 || r->color_planes == 0) {
        return JXL_ERROR_INVALID_INPUT;
    }

    cms_planes = r->color_planes < 3u ? r->color_planes : 3u;
    if (fh->do_ycbcr && r->num_planes >= 3u) {
        cms_planes = 3u;
    }

    transform_noop =
        decoder_postprocess_transform_is_noop(parsed, target_icc, target_icc_len, fh);

    /* Rust: early return only when transform.is_noop() && !do_ycbcr. */
    if (transform_noop && !fh->do_ycbcr) {
        return decoder_finish_postprocess_planes(dec_alloc(dec), r, parsed, fh);
    }

    if (fh->do_ycbcr && r->num_planes >= 3u) {
        if (r->planes[0] != NULL && r->planes[1] != NULL && r->planes[2] != NULL) {
            jxl_ycbcr_to_rgb(dec->ctx, r->planes[0], r->planes[1], r->planes[2], px_count);
        }
    } else if (!transform_noop) {
        uint32_t p;
        if (fh->encoded_color_channels < 3u) {
            jxl_status_t st = jxl_render_clone_gray(dec_alloc(dec), r);
            if (st != JXL_OK) {
                return st;
            }
            cms_planes = r->color_planes < 3u ? r->color_planes : 3u;
        }
        for (p = 0; p < cms_planes; ++p) {
            if (r->planes[p] != NULL) {
                jxl_modular_float_normalize_plane(r->planes[p], px_count, parsed->bit_depth_bits);
            }
        }
    }

    if (transform_noop) {
        return decoder_finish_postprocess_planes(dec_alloc(dec), r, parsed, fh);
    }

    if (parsed->colour.have_icc_profile) {
        int icc_same;
	if (parsed->embedded_icc == NULL || parsed->embedded_icc_len == 0) {
            return JXL_ERROR_UNSUPPORTED;
        }
        if (r->planes[0] == NULL || (cms_planes >= 3u &&
                                      (r->planes[1] == NULL || r->planes[2] == NULL))) {
            return JXL_ERROR_INVALID_INPUT;
        }
        icc_same =
            parsed->embedded_icc_len == target_icc_len &&
            memcmp(parsed->embedded_icc, target_icc, target_icc_len) == 0;
        if (!icc_same) {
            jxl_status_t st = jxl_cms_transform_icc_to_icc(dec_alloc(dec), r->planes, cms_planes, px_count,
                                                           parsed->embedded_icc,
                                                           parsed->embedded_icc_len, target_icc,
                                                           target_icc_len);
            if (st != JXL_OK) {
                return st;
            }
        }
        return decoder_finish_postprocess_planes(dec_alloc(dec), r, parsed, fh);
    }

    return decoder_finish_postprocess_planes(dec_alloc(dec), r, parsed, fh);
}

static jxl_status_t decoder_apply_output_color_transform(jxl_decoder *dec, jxl_render *r,
                                                       const jxl_parsed_image_header *parsed,
                                                       const jxl_decoder_frame_hint *fh) {
    jxl_colour_encoding_parsed target_parsed;
    int use_cms;
    const uint8_t *target_icc;
    size_t target_icc_len;
    size_t px_count;
    if (dec == NULL || r == NULL || parsed == NULL) {
        return JXL_OK;
    }

    target_icc = dec->requested_icc;
    target_icc_len = dec->requested_icc_len;
    if (target_icc == NULL || target_icc_len == 0) {
        return JXL_OK;
    }

    if (!parsed->xyb_encoded) {
        if (fh == NULL) {
            return JXL_ERROR_INVALID_INPUT;
        }
        return decoder_postprocess_keyframe(dec, r, parsed, fh);
    }

    if (r->planes[0] == NULL || r->planes[1] == NULL || r->planes[2] == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }

    px_count = (size_t)r->width * (size_t)r->height;
    memset(&target_parsed, 0, sizeof(target_parsed));
    use_cms = 0;

    if (dec->has_requested_color_encoding) {
        jxl_status_t st =
            jxl_color_encoding_to_parsed(&dec->requested_color_encoding, &target_parsed);
        if (st != JXL_OK) {
            use_cms = 1;
        }
    } else {
        jxl_color_encoding enc;
        if (jxl_icc_parse_color_encoding(target_icc, target_icc_len, &enc) == JXL_OK) {
            jxl_status_t st = jxl_color_encoding_to_parsed(&enc, &target_parsed);
            if (st != JXL_OK) {
                use_cms = 1;
            }
        } else {
            use_cms = 1;
        }
    }

    if (use_cms) {
        jxl_color_transform_xyb_to_linear_rgb(dec->ctx, r->planes[0], r->planes[1], r->planes[2], px_count,
                                              &parsed->opsin_inverse, 255.0f);
        return jxl_cms_transform_linear_srgb_to_icc(dec_alloc(dec), r->planes[0], r->planes[1], r->planes[2],
                                                    px_count, target_icc, target_icc_len);
    }

    if (!jxl_colour_encoding_is_d65_srgb_fast_path(&target_parsed)) {
        return JXL_ERROR_UNSUPPORTED;
    }

    return jxl_color_transform_xyb_to_encoding(dec->ctx, r->planes[0], r->planes[1], r->planes[2], px_count,
                                             &parsed->opsin_inverse, &target_parsed, 255.0f);
}

static void decoder_set_error(jxl_decoder *dec, const char *message) {
    jxl_allocator_state *alloc = dec_alloc(dec);
    jxl_free(alloc, dec->last_error);
    dec->last_error = jxl_strdup(alloc, message);
}

static jxl_status_t decoder_grow_input(jxl_decoder *dec, const uint8_t *data, size_t len) {
    size_t needed;
    if (len == 0) {
        return JXL_OK;
    }
    needed = dec->input_len + len;
    if (needed < dec->input_len) {
        decoder_set_error(dec, "input length overflow");
        return JXL_ERROR_LIMIT_EXCEEDED;
    }
    if (needed > dec->input_cap) {
        size_t new_cap = dec->input_cap == 0 ? 4096 : dec->input_cap;
        uint8_t *grown;
	while (new_cap < needed) {
            if (new_cap > (SIZE_MAX / 2)) {
                decoder_set_error(dec, "input buffer too large");
                return JXL_ERROR_LIMIT_EXCEEDED;
            }
            new_cap *= 2;
        }
        grown = jxl_alloc(dec_alloc(dec), new_cap);
        if (grown == NULL) {
            decoder_set_error(dec, "out of memory");
            return JXL_ERROR_OUT_OF_MEMORY;
        }
        if (dec->input != NULL) {
            memcpy(grown, dec->input, dec->input_len);
            jxl_free(dec_alloc(dec), dec->input);
        }
        dec->input = grown;
        dec->input_cap = new_cap;
    }
    memcpy(dec->input + dec->input_len, data, len);
    dec->input_len += len;

    if (dec->reader != NULL) {
        size_t consumed = 0;
        jxl_bs_status_t feed_st =
            jxl_container_reader_feed(dec->reader, data, len, &consumed);
        if (feed_st != JXL_BS_OK) {
            decoder_set_error(dec, "invalid container input");
            return JXL_ERROR_INVALID_INPUT;
        }
        if (consumed != len) {
            decoder_set_error(dec, "unexpected partial container feed");
            return JXL_ERROR_INVALID_INPUT;
        }
    }
    return JXL_OK;
}

jxl_status_t jxl_decoder_create(jxl_context *ctx, const jxl_decoder_options *opts,
                                jxl_decoder **out) {
    jxl_status_t deps;
    jxl_decoder *dec;
    if (out == NULL || ctx == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    *out = NULL;
    if (opts != NULL && opts->reserved != 0) {
        return JXL_ERROR_INVALID_INPUT;
    }

    deps = jxl_deps_self_test();
    if (deps != JXL_OK) {
        return deps;
    }

    dec = jxl_ctx_alloc(ctx, sizeof(*dec));
    if (dec == NULL) {
        return JXL_ERROR_OUT_OF_MEMORY;
    }
    memset(dec, 0, sizeof(*dec));
    dec->ctx = ctx;
    dec->header.bit_depth = 8;
    dec->animation_chain_upto = UINT32_MAX;
    dec->reader = jxl_container_reader_create(jxl_context_alloc_state(ctx));
    if (dec->reader == NULL) {
        jxl_ctx_free(ctx, dec);
        return JXL_ERROR_OUT_OF_MEMORY;
    }
    *out = dec;
    return JXL_OK;
}

void jxl_decoder_destroy(jxl_context *ctx, jxl_decoder *dec) {
    if (dec == NULL) {
        return;
    }
    if (ctx != NULL && dec->ctx != ctx) {
        return;
    }
    jxl_ctx_free(ctx, dec->last_error);
    jxl_ctx_free(ctx, dec->input);
    jxl_ctx_free(ctx, dec->requested_icc);
    if (dec->animation_cache_init) {
        jxl_allocator_state *alloc = jxl_context_alloc_state(ctx);
        jxl_reference_store_free(alloc, &dec->animation_refs);
        jxl_progressive_lf_store_free(alloc, &dec->animation_lf_store);
    }
    jxl_container_reader_destroy(jxl_context_alloc_state(ctx), dec->reader);
    jxl_parsed_image_header_free_embedded_icc(jxl_context_alloc_state(ctx), &dec->parsed_header);
    jxl_ctx_free(ctx, dec);
}

jxl_status_t jxl_decoder_feed(jxl_decoder *dec, const uint8_t *data, size_t len) {
    if (dec == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    if (len > 0 && data == NULL) {
        decoder_set_error(dec, "feed data is null");
        return JXL_ERROR_INVALID_INPUT;
    }
    return decoder_grow_input(dec, data, len);
}

jxl_status_t jxl_decoder_try_init(jxl_decoder *dec) {
    jxl_image_geometry geometry;
    size_t cs_len;
    const uint8_t *codestream;
    jxl_status_t status;
    if (dec == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    if (dec->input_len == 0) {
        return JXL_NEED_MORE_DATA;
    }
    if (dec->initialized) {
        return JXL_OK;
    }
    if (!decoder_borrow_codestream(dec, &codestream, &cs_len)) {
        return JXL_NEED_MORE_DATA;
    }

    status = jxl_decoder_init_from_codestream(
        dec_alloc(dec), codestream, cs_len, &dec->header, &dec->num_color_channels, &geometry,
        &dec->xyb_encoded, &dec->parsed_header, &dec->frames_bitstream_offset);
    if (status == JXL_OK) {
        dec->initialized = 1;
        dec->codestream_width = geometry.codestream_width;
        dec->codestream_height = geometry.codestream_height;
        dec->orientation = geometry.orientation;
        jxl_free(dec_alloc(dec), dec->last_error);
        dec->last_error = NULL;
        return JXL_OK;
    }
    if (status == JXL_NEED_MORE_DATA) {
        /* keep prior error clear */
        jxl_free(dec_alloc(dec), dec->last_error);
        dec->last_error = NULL;
    } else {
        decoder_set_error(dec, "failed to parse image header");
    }
    return status;
}

const jxl_image_header *jxl_decoder_header(const jxl_decoder *dec) {
    if (dec == NULL || !dec->initialized) {
        return NULL;
    }
    return &dec->header;
}

jxl_status_t jxl_decoder_request_icc(jxl_decoder *dec, const uint8_t *icc, size_t len) {
    if (dec == NULL || icc == NULL || len == 0) {
        return JXL_ERROR_INVALID_INPUT;
    }
    dec->has_requested_color_encoding = 0;
    jxl_free(dec_alloc(dec), dec->requested_icc);
    dec->requested_icc = jxl_alloc(dec_alloc(dec), len);
    if (dec->requested_icc == NULL) {
        dec->requested_icc_len = 0;
        decoder_set_error(dec, "out of memory");
        return JXL_ERROR_OUT_OF_MEMORY;
    }
    memcpy(dec->requested_icc, icc, len);
    dec->requested_icc_len = len;
    jxl_free(dec_alloc(dec), dec->last_error);
    dec->last_error = NULL;
    return JXL_OK;
}

jxl_status_t jxl_decoder_request_color_encoding(jxl_decoder *dec, jxl_color_encoding enc) {
    if (dec == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    dec->requested_color_encoding = enc;
    dec->has_requested_color_encoding = 1;
    jxl_free(dec_alloc(dec), dec->requested_icc);
    dec->requested_icc = NULL;
    dec->requested_icc_len = 0;
    jxl_free(dec_alloc(dec), dec->last_error);
    dec->last_error = NULL;
    return JXL_OK;
}

jxl_status_t jxl_decoder_set_crop(jxl_decoder *dec, const jxl_crop *crop) {
    if (dec == NULL || crop == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    dec->crop = *crop;
    dec->has_crop = 1;
    return JXL_OK;
}

static jxl_status_t decoder_count_keyframes(jxl_allocator_state *alloc, const uint8_t *codestream,
                                            size_t codestream_len,
                                            const jxl_parsed_image_header *cached_parsed,
                                            size_t frames_bitstream_offset, uint32_t *count_out) {
    jxl_bs bs;
    jxl_parsed_image_header parsed;
    jxl_frame_status_t fst;

    if (codestream == NULL || codestream_len < 3) {
        return JXL_ERROR_INVALID_INPUT;
    }

    if (cached_parsed != NULL) {
        jxl_bs_init_at_bit(&bs, codestream, codestream_len, frames_bitstream_offset * 8);
        fst = jxl_count_keyframes(alloc, &bs, cached_parsed, codestream, codestream_len, count_out);
        return fst == JXL_FRAME_OK ? JXL_OK : JXL_ERROR_INVALID_INPUT;
    }

    jxl_bs_init(&bs, codestream, codestream_len);
    memset(&parsed, 0, sizeof(parsed));
    if (jxl_image_header_parse(&bs, &parsed) != JXL_BS_OK ||
        jxl_image_skip_post_header(alloc, &bs, &parsed) != JXL_BS_OK) {
        return JXL_ERROR_INVALID_INPUT;
    }

    fst = jxl_count_keyframes(alloc, &bs, &parsed, codestream, codestream_len, count_out);
    return fst == JXL_FRAME_OK ? JXL_OK : JXL_ERROR_INVALID_INPUT;
}

static jxl_status_t decoder_render_keyframe(jxl_context *ctx, jxl_decoder *dec,
                                            uint32_t keyframe_index, jxl_render **out) {
    jxl_status_t st;
    uint32_t num_keyframes;
    jxl_modular_region output_region;
    jxl_modular_region user_crop_region;
    jxl_modular_region padded_render_region;
    int render_padded_filters;
    jxl_decoder_frame_hint frame_hint;
    jxl_keyframe_render_params kparams;
    size_t cs_len;
    jxl_status_t count_st;
    uint32_t render_w;
    uint32_t render_h;
    uint32_t render_color_planes;
    uint64_t plane_count64;
    uint32_t plane_count;
    uint32_t output_color_planes;
    uint64_t pixels;
    jxl_status_t render_st;
    jxl_status_t mat_st;
    jxl_status_t color_st;
    jxl_modular_region frame_region;
    size_t px_count;
    uint32_t extra_planes;
    uint32_t new_planes;
    uint32_t p;
    const uint8_t *codestream;
    jxl_allocator_state *alloc;
    const jxl_modular_region *output_ptr;
    jxl_render *r;
    jxl_render *compact;

    st = JXL_OK;
    if (dec == NULL || out == NULL || ctx == NULL || dec->ctx != ctx) {
        return JXL_ERROR_INVALID_INPUT;
    }
    *out = NULL;
    if (!dec->initialized) {
        decoder_set_error(dec, "decoder must be initialized before render");
        return JXL_ERROR_INVALID_INPUT;
    }
    if (!decoder_borrow_codestream(dec, &codestream, &cs_len)) {
        decoder_set_error(dec, "codestream not available");
        return JXL_ERROR_INVALID_INPUT;
    }

    num_keyframes = 0;
    count_st = decoder_count_keyframes(dec_alloc(dec), codestream, cs_len,
                                       &dec->parsed_header,
                                       dec->frames_bitstream_offset, &num_keyframes);
    if (count_st != JXL_OK) {
        decoder_set_error(dec, "failed to count keyframes");
        return count_st;
    }
    if (keyframe_index >= num_keyframes) {
        decoder_set_error(dec, "keyframe index out of range");
        return JXL_ERROR_INVALID_INPUT;
    }

    alloc = jxl_context_alloc_state(ctx);

    /* Render in codestream space (Rust internal grid); orientation runs once at export. */
    render_w = dec->codestream_width;
    render_h = dec->codestream_height;
    output_ptr = NULL;
    render_padded_filters = 0;
    memset(&user_crop_region, 0, sizeof(user_crop_region));
    memset(&padded_render_region, 0, sizeof(padded_render_region));
    memset(&kparams, 0, sizeof(kparams));
    if (dec->has_crop) {
        jxl_modular_region compound_tmp;
        jxl_crop cs_crop;

        cs_crop =
            crop_display_to_codestream(dec->orientation, dec->codestream_width,
                                       dec->codestream_height, &dec->crop);
        render_w = cs_crop.width;
        render_h = cs_crop.height;
        compound_tmp.left = (int32_t)cs_crop.left;
        compound_tmp.top = (int32_t)cs_crop.top;
        compound_tmp.width = cs_crop.width;
        compound_tmp.height = cs_crop.height;
        output_region = compound_tmp;

        output_ptr = &output_region;
        user_crop_region = output_region;
    }

    memset(&frame_hint, 0, sizeof(frame_hint));
    decoder_peek_first_frame(dec_alloc(dec), codestream, cs_len, &frame_hint);

    if (output_ptr != NULL && frame_hint.needs_filter_padding) {
        jxl_parsed_image_header parsed_pad;
        jxl_frame_header fh_pad;
        jxl_render_padded_regions padded;
        jxl_modular_region contained;

        memset(&parsed_pad, 0, sizeof(parsed_pad));
        if (decoder_parse_for_padding(dec_alloc(dec), codestream, cs_len, &parsed_pad, &fh_pad)) {
            if (fh_pad.upsampling <= 1u && parsed_pad.num_extra_channels == 0) {
                jxl_render_compute_padded_regions(&parsed_pad, &fh_pad, *output_ptr, &padded);
                if (padded.color_padded.width > 0 && padded.color_padded.height > 0) {
                    contained =
                        jxl_modular_region_intersection(user_crop_region, padded.color_padded);
                    if (contained.width == user_crop_region.width &&
                        contained.height == user_crop_region.height &&
                        contained.left == user_crop_region.left &&
                        contained.top == user_crop_region.top) {
                        padded_render_region = padded.color_padded;
                        output_region = padded_render_region;
                        output_ptr = &output_region;
                        render_w = padded_render_region.width;
                        render_h = padded_render_region.height;
                        render_padded_filters = 1;
                    }
                }
            }
            jxl_frame_header_free(alloc, &fh_pad);
            jxl_parsed_image_header_free_embedded_icc(dec_alloc(dec), &parsed_pad);
        }
    }

    render_color_planes = dec->num_color_channels;
    if (render_color_planes == 1u && dec->xyb_encoded) {
        /* VarDCT XYB grayscale decodes three planes before color conversion. */
        render_color_planes = 3u;
    } else if (render_color_planes == 1u && !dec->xyb_encoded && frame_hint.do_ycbcr) {
        /* VarDCT YCbCr JPEG stores grayscale in three planes until output transform. */
        render_color_planes = 3u;
    }
    plane_count64 = (uint64_t)render_color_planes + dec->header.num_extra_channels;
    if (plane_count64 == 0 || plane_count64 > UINT32_MAX) {
        decoder_set_error(dec, "invalid render plane count");
        return JXL_ERROR_INVALID_INPUT;
    }
    plane_count = (uint32_t)plane_count64;
    output_color_planes = dec->num_color_channels;

    pixels = (uint64_t)render_w * render_h;
    if (pixels > SIZE_MAX / sizeof(float) ||
        plane_count > 0 && pixels > (SIZE_MAX / sizeof(float)) / plane_count) {
        decoder_set_error(dec, "render buffer too large");
        return JXL_ERROR_LIMIT_EXCEEDED;
    }

    /* Rust keeps three color planes through VarDCT render (ImageWithRegion::new(3, …)). */
    r = jxl_render_create(alloc, plane_count, render_color_planes, render_w, render_h);
    if (r == NULL) {
        decoder_set_error(dec, "out of memory");
        return JXL_ERROR_OUT_OF_MEMORY;
    }
    if (output_ptr != NULL) {
        jxl_render_init_all_planes(r, output_ptr);
    } else {
        frame_region = jxl_modular_region_with_size(render_w, render_h);
        jxl_render_init_all_planes(r, &frame_region);
    }

    kparams.ctx = dec->ctx;
    kparams.alloc = dec_alloc(dec);
    kparams.input = dec->input;
    kparams.input_len = dec->input_len;
    kparams.codestream = codestream;
    kparams.codestream_len = cs_len;
    kparams.parsed_header = &dec->parsed_header;
    kparams.frames_bitstream_offset = dec->frames_bitstream_offset;
    kparams.error_out = &dec->last_error;
    kparams.bit_depth = dec->header.bit_depth;
    kparams.num_color_channels = render_color_planes;
    kparams.num_extra_channels = dec->header.num_extra_channels;
    kparams.crop = dec->crop;
    kparams.has_crop = dec->has_crop && output_ptr == NULL;
    kparams.filter_region = NULL;
    kparams.output_region = output_ptr;
    kparams.keyframe_index = keyframe_index;
    kparams.parsed_out = &dec->parsed_header;

    if (dec->header.have_animation) {
        if (!dec->animation_cache_init) {
            jxl_reference_store_init(&dec->animation_refs);
            jxl_progressive_lf_store_init(&dec->animation_lf_store);
            dec->animation_cache_init = 1;
        }
        if (dec->animation_chain_upto != UINT32_MAX && keyframe_index < dec->animation_chain_upto) {
            jxl_reference_store_free(dec_alloc(dec), &dec->animation_refs);
            jxl_progressive_lf_store_free(dec_alloc(dec), &dec->animation_lf_store);
            jxl_reference_store_init(&dec->animation_refs);
            jxl_progressive_lf_store_init(&dec->animation_lf_store);
            dec->animation_chain_upto = UINT32_MAX;
        }
        kparams.external_refs = &dec->animation_refs;
        kparams.external_lf_store = &dec->animation_lf_store;
        kparams.animation_chain_upto = &dec->animation_chain_upto;
    }
    render_st = jxl_render_display_keyframe(&kparams, r);
    if (render_st != JXL_OK) {
        jxl_render_free(alloc, r);
        return render_st;
    }

    jxl_render_bind_materialization(r, alloc, &dec->parsed_header);

    if (render_padded_filters) {
        mat_st = decoder_materialize_render_f32(dec, r);
        if (mat_st != JXL_OK) {
            jxl_render_free(alloc, r);
            decoder_set_error(dec, "failed to materialize render planes");
            return mat_st;
        }
        compact =
            decoder_extract_render_subregion(dec_alloc(dec), r, &user_crop_region, &padded_render_region);
        if (compact == NULL) {
            jxl_render_free(alloc, r);
            decoder_set_error(dec, "failed to extract cropped render subregion");
            return JXL_ERROR_OUT_OF_MEMORY;
        }
        r = compact;
    }

    if (dec->parsed_header.xyb_encoded && render_color_planes >= 3) {
        if (!r->ct_done) {
            mat_st = decoder_materialize_render_f32(dec, r);
            if (mat_st != JXL_OK) {
                jxl_render_free(alloc, r);
                decoder_set_error(dec, "failed to materialize render planes");
                return mat_st;
            }
            color_st =
                decoder_apply_output_color_transform(dec, r, &dec->parsed_header, &frame_hint);
            if (color_st != JXL_OK) {
                jxl_render_free(alloc, r);
                decoder_set_error(dec, "failed to apply output color transform");
                return color_st;
            }
        }
    } else if (dec->requested_icc != NULL && dec->requested_icc_len > 0) {
        if (!r->ct_done) {
            mat_st = decoder_materialize_render_f32(dec, r);
            if (mat_st != JXL_OK) {
                jxl_render_free(alloc, r);
                decoder_set_error(dec, "failed to materialize render planes");
                return mat_st;
            }
            color_st =
                decoder_apply_output_color_transform(dec, r, &dec->parsed_header, &frame_hint);
            if (color_st != JXL_OK) {
                jxl_render_free(alloc, r);
                decoder_set_error(dec, "failed to apply output color transform");
                return color_st;
            }
        }
    }

    if (output_color_planes == 1u && render_color_planes == 3u &&
        r->num_planes >= 3u + dec->header.num_extra_channels) {
        mat_st = decoder_materialize_render_f32(dec, r);
        if (mat_st != JXL_OK) {
            jxl_render_free(alloc, r);
            decoder_set_error(dec, "failed to materialize render planes");
            return mat_st;
        }
        px_count = (size_t)r->width * r->height;
        extra_planes = dec->header.num_extra_channels;
        new_planes = 1u + extra_planes;
        compact =
            jxl_render_create(alloc, new_planes, 1u, r->width, r->height);
        if (compact == NULL) {
            jxl_render_free(alloc, r);
            decoder_set_error(dec, "out of memory");
            return JXL_ERROR_OUT_OF_MEMORY;
        }
        frame_region = jxl_modular_region_with_size(r->width, r->height);
        jxl_render_init_all_planes(compact, &frame_region);
        memcpy(compact->planes[0], r->planes[0], px_count * sizeof(float));
        for (p = 0; p < extra_planes; ++p) {
            if (r->planes[3u + p] != NULL) {
                memcpy(compact->planes[p + 1u], r->planes[3u + p], px_count * sizeof(float));
                if (r->meta != NULL && compact->meta != NULL) {
                    compact->meta[p + 1u] = r->meta[3u + p];
                }
            }
        }
        jxl_render_free(alloc, r);
        r = compact;
    }

    if (dec->orientation != 1) {
        mat_st = decoder_materialize_render_f32(dec, r);
        if (mat_st != JXL_OK) {
            jxl_render_free(alloc, r);
            decoder_set_error(dec, "failed to materialize render planes");
            return mat_st;
        }
        st = jxl_render_apply_orientation(alloc, r, dec->orientation);
        if (st != JXL_OK) {
            jxl_render_free(alloc, r);
            decoder_set_error(dec, "failed to apply image orientation");
            return st;
        }
    }

    *out = r;
    jxl_free(dec_alloc(dec), dec->last_error);
    dec->last_error = NULL;
    return JXL_OK;
}

jxl_status_t jxl_decoder_render(jxl_context *ctx, jxl_decoder *dec, jxl_render **out) {
    return decoder_render_keyframe(ctx, dec, 0, out);
}

jxl_status_t jxl_decoder_render_keyframe(jxl_context *ctx, jxl_decoder *dec, uint32_t keyframe_index,
                                       jxl_render **out) {
    return decoder_render_keyframe(ctx, dec, keyframe_index, out);
}

uint32_t jxl_decoder_num_keyframes(const jxl_decoder *dec) {
    uint32_t count;
    size_t cs_len;
    const uint8_t *codestream;
    if (dec == NULL || !dec->initialized || dec->input_len == 0 || dec->ctx == NULL) {
        return 0;
    }
    if (!decoder_borrow_codestream(dec, &codestream, &cs_len)) {
        return 0;
    }
    count = 0;
    if (decoder_count_keyframes(jxl_context_alloc_state(dec->ctx), codestream, cs_len,
                                &dec->parsed_header, dec->frames_bitstream_offset, &count) !=
        JXL_OK) {
        return 0;
    }
    return count;
}

uint32_t jxl_render_keyframe_index(const jxl_render *r) {
    return r != NULL ? r->keyframe_index : 0;
}

uint32_t jxl_render_duration(const jxl_render *r) {
    return r != NULL ? r->duration : 0;
}

uint32_t jxl_render_width(const jxl_render *r) {
    return r != NULL ? r->width : 0;
}

uint32_t jxl_render_height(const jxl_render *r) {
    return r != NULL ? r->height : 0;
}

uint32_t jxl_render_num_planes(const jxl_render *r) {
    return r != NULL ? r->num_planes : 0;
}

const float *jxl_render_plane(const jxl_render *r, uint32_t plane) {
    if (r == NULL || plane >= r->num_planes || r->planes == NULL) {
        return NULL;
    }
    if (jxl_render_plane_is_integer(r, plane) && r->materialize_alloc != NULL) {
        jxl_render *mut = (jxl_render *)r;
        if (jxl_render_materialize_plane_f32(r->materialize_alloc, mut, plane) != JXL_OK) {
            return NULL;
        }
    }
    return r->planes[plane];
}

jxl_render_plane_kind jxl_render_get_plane_kind(const jxl_render *r, uint32_t plane) {
    if (r == NULL || r->bufs == NULL || plane >= r->num_planes) {
        return JXL_RENDER_PLANE_F32;
    }
    switch (r->bufs[plane].kind) {
    case JXL_IMAGE_BUFFER_I16:
        return JXL_RENDER_PLANE_I16;
    case JXL_IMAGE_BUFFER_I32:
        return JXL_RENDER_PLANE_I32;
    case JXL_IMAGE_BUFFER_F32:
    default:
        return JXL_RENDER_PLANE_F32;
    }
}

const int16_t *jxl_render_plane_i16(const jxl_render *r, uint32_t plane, uint32_t *width,
                                    uint32_t *height) {
    const jxl_modular_grid_i32 *grid;
    if (r == NULL || r->bufs == NULL || plane >= r->num_planes ||
        r->bufs[plane].kind != JXL_IMAGE_BUFFER_I16) {
        return NULL;
    }
    grid = &r->bufs[plane].u.grid;
    if (grid->buf == NULL || grid->kind != JXL_MODULAR_SAMPLE_I16) {
        return NULL;
    }
    jxl_modular_grid_normalize_stride((jxl_modular_grid_i32 *)grid);
    if (width != NULL) {
        *width = grid->width;
    }
    if (height != NULL) {
        *height = grid->height;
    }
    return (const int16_t *)grid->buf + grid->offset;
}

const uint8_t *jxl_render_icc(const jxl_render *r, size_t *len) {
    if (len != NULL) {
        *len = 0;
    }
    (void)r;
    return NULL;
}

const char *jxl_decoder_last_error(const jxl_decoder *dec) {
    if (dec == NULL || dec->last_error == NULL) {
        return "";
    }
    return dec->last_error;
}

static jxl_aux_box_list *jxl_decoder_aux_boxes(const jxl_decoder *dec) {
    if (dec == NULL || dec->reader == NULL) {
        return NULL;
    }
    return jxl_container_reader_aux_boxes(dec->reader);
}

static jxl_status_t raw_exif_from_box(const uint8_t *box_data, size_t box_len,
                                      jxl_exif_metadata *out) {
    size_t payload_len;
    uint32_t tiff_off;
    const uint8_t *payload;
    if (box_len < 4) {
        return JXL_ERROR_INVALID_INPUT;
    }
    tiff_off =
        ((uint32_t)box_data[0] << 24) | ((uint32_t)box_data[1] << 16) |
        ((uint32_t)box_data[2] << 8) | (uint32_t)box_data[3];
    payload = box_data + 4;
    payload_len = box_len - 4;
    if (tiff_off > payload_len) {
        return JXL_ERROR_INVALID_INPUT;
    }
    out->status = JXL_EXIF_AVAILABLE;
    out->tiff_header_offset = tiff_off;
    out->payload = payload;
    out->payload_len = payload_len;
    return JXL_OK;
}

jxl_status_t jxl_decoder_first_exif(const jxl_decoder *dec, jxl_exif_metadata *out) {
    jxl_aux_box_list *aux;
    jxl_aux_box_data raw;
    if (dec == NULL || out == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    out->status = JXL_EXIF_NOT_FOUND;
    out->tiff_header_offset = 0;
    out->payload = NULL;
    out->payload_len = 0;

    aux = jxl_decoder_aux_boxes(dec);
    if (aux == NULL) {
        return JXL_OK;
    }

    raw = jxl_aux_box_list_first_exif(aux);
    switch (raw.tag) {
    case JXL_AUX_BOX_DECODING:
        out->status = JXL_EXIF_DECODING;
        return JXL_OK;
    case JXL_AUX_BOX_NOT_FOUND:
        out->status = JXL_EXIF_NOT_FOUND;
        return JXL_OK;
    case JXL_AUX_BOX_HAS_DATA:
        return raw_exif_from_box(raw.data, raw.data_len, out);
    }
    return JXL_ERROR_INVALID_INPUT;
}

#if defined(JXL_OXIDE_C_ENABLE_JBR) && JXL_OXIDE_C_ENABLE_JBR
#include "jbr/reconstruct.h"

static jxl_status_t frame_status_to_jxl(jxl_frame_status_t st) {
    if (st == JXL_FRAME_OK) {
        return JXL_OK;
    }
    if (st == JXL_FRAME_OUT_OF_MEMORY) {
        return JXL_ERROR_OUT_OF_MEMORY;
    }
    return JXL_ERROR_INVALID_INPUT;
}

static jxl_status_t decoder_load_first_frame(jxl_decoder *dec, jxl_parsed_image_header *parsed_out,
                                             jxl_frame *frame_out) {
    size_t cs_len;
    jxl_bs bs;
    jxl_parsed_image_header parsed;
    jxl_frame frame;
    size_t consumed;
    jxl_allocator_state *alloc = dec_alloc(dec);
    const uint8_t *cs = NULL;
    jxl_frame_status_t fst;
    jxl_status_t st;
    size_t meta_end;

    if (!decoder_borrow_codestream(dec, &cs, &cs_len)) {
        return JXL_NEED_MORE_DATA;
    }

    jxl_bs_init(&bs, cs, cs_len);
    memset(&parsed, 0, sizeof(parsed));
    if (jxl_image_header_parse(&bs, &parsed) != JXL_BS_OK) {
        return JXL_ERROR_INVALID_INPUT;
    }
    if (jxl_image_decode_post_header(alloc, &bs, &parsed) != JXL_BS_OK) {
        jxl_parsed_image_header_free_embedded_icc(alloc, &parsed);
        return JXL_ERROR_INVALID_INPUT;
    }

    jxl_frame_init(&frame);
    fst = jxl_frame_parse_keyframe(alloc, &bs, &parsed, cs, cs_len, &frame);
    st = frame_status_to_jxl(fst);
    if (st != JXL_OK) {
        jxl_frame_free(alloc, &frame);
        jxl_parsed_image_header_free_embedded_icc(alloc, &parsed);
        return st;
    }

    meta_end = bs.num_read_bits / 8;
    if (meta_end > cs_len || frame.toc.total_size > cs_len - meta_end) {
        jxl_frame_free(alloc, &frame);
        jxl_parsed_image_header_free_embedded_icc(alloc, &parsed);
        return JXL_ERROR_INVALID_INPUT;
    }

    consumed = 0;
    jxl_frame_feed_bytes(&frame, cs + meta_end, frame.toc.total_size, &consumed);
    if (consumed != frame.toc.total_size || !jxl_frame_is_loading_done(&frame)) {
        jxl_frame_free(alloc, &frame);
        jxl_parsed_image_header_free_embedded_icc(alloc, &parsed);
        return JXL_ERROR_INVALID_INPUT;
    }

    *parsed_out = parsed;
    *frame_out = frame;
    return JXL_OK;
}

static jxl_jpeg_reconstruction_status decoder_jbrd_status(const jxl_decoder *dec,
                                                          const jxl_jbr_data *jbrd) {
    const jxl_jbr_header *header = jxl_jbr_data_header(jbrd);
    jxl_aux_box_list *aux;
    jxl_aux_box_data exif;
    jxl_aux_box_data xml;
    size_t expected_icc;
    size_t expected_exif;
    size_t expected_xmp;
    if (header == NULL) {
        return JXL_JPEG_RECONSTRUCTION_INVALID;
    }

    aux = jxl_decoder_aux_boxes(dec);
    exif = jxl_aux_box_list_first_exif(aux);
    xml = jxl_aux_box_list_first_xml(aux);

    expected_icc = jxl_jbr_header_expected_icc_len(header);
    expected_exif = jxl_jbr_header_expected_exif_len(header);
    expected_xmp = jxl_jbr_header_expected_xmp_len(header);

    if (expected_icc > 0) {
        size_t cs_len;
        jxl_bs bs;
        jxl_parsed_image_header parsed;
        jxl_allocator_state *alloc = dec_alloc((jxl_decoder *)dec);
        const uint8_t *codestream = NULL;
        jxl_jpeg_reconstruction_status icc_st = JXL_JPEG_RECONSTRUCTION_NEED_MORE_DATA;

        if (!decoder_borrow_codestream(dec, &codestream, &cs_len)) {
            return JXL_JPEG_RECONSTRUCTION_NEED_MORE_DATA;
        }
        jxl_bs_init(&bs, codestream, cs_len);
        memset(&parsed, 0, sizeof(parsed));
        if (jxl_image_header_parse(&bs, &parsed) == JXL_BS_OK) {
            if (!parsed.colour.have_icc_profile) {
                icc_st = JXL_JPEG_RECONSTRUCTION_INVALID;
            } else if (jxl_image_decode_post_header(alloc, &bs, &parsed) == JXL_BS_OK &&
                       parsed.embedded_icc != NULL) {
                icc_st = JXL_JPEG_RECONSTRUCTION_AVAILABLE;
            }
        }
        jxl_parsed_image_header_free_embedded_icc(alloc, &parsed);
        if (icc_st != JXL_JPEG_RECONSTRUCTION_AVAILABLE) {
            return icc_st;
        }
    }
    if (expected_exif > 0) {
        if (exif.tag == JXL_AUX_BOX_DECODING) {
            return JXL_JPEG_RECONSTRUCTION_NEED_MORE_DATA;
        }
        if (exif.tag == JXL_AUX_BOX_NOT_FOUND) {
            return JXL_JPEG_RECONSTRUCTION_INVALID;
        }
    }
    if (expected_xmp > 0) {
        if (xml.tag == JXL_AUX_BOX_DECODING) {
            return JXL_JPEG_RECONSTRUCTION_NEED_MORE_DATA;
        }
        if (xml.tag == JXL_AUX_BOX_NOT_FOUND) {
            return JXL_JPEG_RECONSTRUCTION_INVALID;
        }
    }
    return JXL_JPEG_RECONSTRUCTION_AVAILABLE;
}

jxl_jpeg_reconstruction_status jxl_decoder_jpeg_reconstruction_status(const jxl_decoder *dec) {
    jxl_aux_box_list *aux;
    jxl_aux_jbrd_data jbrd_box;
    if (dec == NULL) {
        return JXL_JPEG_RECONSTRUCTION_UNAVAILABLE;
    }
    aux = jxl_decoder_aux_boxes(dec);
    if (aux == NULL) {
        return JXL_JPEG_RECONSTRUCTION_UNAVAILABLE;
    }

    jbrd_box = jxl_aux_box_list_jbrd(aux);
    switch (jbrd_box.tag) {
    case JXL_AUX_JBRD_HAS_DATA:
        return decoder_jbrd_status(dec, jbrd_box.data);
    case JXL_AUX_JBRD_DECODING: {
        uint32_t count = 0;
        size_t cs_len = 0;
        jxl_bs bs;
        jxl_parsed_image_header parsed;
        jxl_frame_header fh;
        jxl_allocator_state *alloc = dec_alloc((jxl_decoder *)dec);
        int ok;
        const uint8_t *cs = jxl_container_reader_codestream(dec->reader, &cs_len);
        if (cs == NULL || cs_len < 3) {
            return JXL_JPEG_RECONSTRUCTION_NEED_MORE_DATA;
        }
        jxl_bs_init(&bs, cs, cs_len);
        memset(&parsed, 0, sizeof(parsed));
        if (jxl_image_header_parse(&bs, &parsed) != JXL_BS_OK) {
            return JXL_JPEG_RECONSTRUCTION_NEED_MORE_DATA;
        }
        if (jxl_image_skip_post_header(alloc, &bs, &parsed) != JXL_BS_OK) {
            return JXL_JPEG_RECONSTRUCTION_INVALID;
        }
        if (jxl_count_keyframes(alloc, &bs, &parsed, cs, cs_len, &count) != JXL_FRAME_OK ||
            count >= 2) {
            return JXL_JPEG_RECONSTRUCTION_INVALID;
        }
        jxl_frame_header_init(&fh);
        if (jxl_frame_header_parse(alloc, &bs, &parsed, &fh) != JXL_FRAME_OK) {
            jxl_frame_header_free(alloc, &fh);
            return JXL_JPEG_RECONSTRUCTION_NEED_MORE_DATA;
        }
        ok = fh.encoding == JXL_FRAME_ENCODING_VARDCT &&
                 jxl_frame_header_is_normal_frame(&fh);
        jxl_frame_header_free(alloc, &fh);
        return ok ? JXL_JPEG_RECONSTRUCTION_NEED_MORE_DATA : JXL_JPEG_RECONSTRUCTION_INVALID;
    }
    case JXL_AUX_JBRD_NOT_FOUND:
        return JXL_JPEG_RECONSTRUCTION_UNAVAILABLE;
    }
    return JXL_JPEG_RECONSTRUCTION_UNAVAILABLE;
}

static jxl_status_t jbr_status_to_jxl(jxl_jbr_status st) {
    switch (st) {
    case JXL_JBR_OK:
        return JXL_OK;
    case JXL_JBR_OUT_OF_MEMORY:
        return JXL_ERROR_OUT_OF_MEMORY;
    case JXL_JBR_UNAVAILABLE:
    case JXL_JBR_DATA_INCOMPLETE:
        return JXL_ERROR_INVALID_INPUT;
    default:
        return JXL_ERROR_UNSUPPORTED;
    }
}

jxl_status_t jxl_decoder_reconstruct_jpeg(jxl_decoder *dec, uint8_t **jpeg_out, size_t *jpeg_len) {
    jxl_parsed_image_header parsed;
    jxl_frame frame;
    size_t exif_len;
    size_t xmp_len;
    jxl_jbr_output out;
    jxl_aux_jbrd_data jbrd_box;
    jxl_allocator_state *alloc;
    jxl_status_t st;
    const uint8_t *icc;
    size_t icc_len;
    const uint8_t *exif_payload;
    jxl_aux_box_data exif_box;
    const uint8_t *xmp_payload;
    jxl_jbr_status jst;
    jxl_aux_box_data xml_box;
    if (dec == NULL || jpeg_out == NULL || jpeg_len == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    *jpeg_out = NULL;
    *jpeg_len = 0;

    if (jxl_decoder_jpeg_reconstruction_status(dec) != JXL_JPEG_RECONSTRUCTION_AVAILABLE) {
        decoder_set_error(dec, "JPEG reconstruction unavailable");
        return JXL_ERROR_INVALID_INPUT;
    }

    jbrd_box = jxl_aux_box_list_jbrd(jxl_decoder_aux_boxes(dec));
    if (jbrd_box.tag != JXL_AUX_JBRD_HAS_DATA || jbrd_box.data == NULL) {
        decoder_set_error(dec, "JPEG reconstruction data incomplete");
        return JXL_ERROR_INVALID_INPUT;
    }

    alloc = dec_alloc(dec);
    st = decoder_load_first_frame(dec, &parsed, &frame);
    if (st != JXL_OK) {
        decoder_set_error(dec, "failed to load frame for JPEG reconstruction");
        return st;
    }

    icc = parsed.embedded_icc;
    icc_len = parsed.embedded_icc_len;

    exif_payload = NULL;
    exif_len = 0;
    exif_box = jxl_aux_box_list_first_exif(jxl_decoder_aux_boxes(dec));
    if (exif_box.tag == JXL_AUX_BOX_HAS_DATA && exif_box.data_len >= 4) {
        uint32_t tiff_off = ((uint32_t)exif_box.data[0] << 24) |
                            ((uint32_t)exif_box.data[1] << 16) |
                            ((uint32_t)exif_box.data[2] << 8) | (uint32_t)exif_box.data[3];
        if (4 + tiff_off <= exif_box.data_len) {
            exif_payload = exif_box.data + 4 + tiff_off;
            exif_len = exif_box.data_len - 4 - tiff_off;
        }
    }

    xmp_payload = NULL;
    xmp_len = 0;
    xml_box = jxl_aux_box_list_first_xml(jxl_decoder_aux_boxes(dec));
    if (xml_box.tag == JXL_AUX_BOX_HAS_DATA) {
        xmp_payload = xml_box.data;
        xmp_len = xml_box.data_len;
    }

    jxl_jbr_output_init(&out);
    jst = jxl_jbr_reconstruct(alloc, dec->ctx, jbrd_box.data, &frame, &parsed, icc,
                                             icc_len, exif_payload, exif_len, xmp_payload, xmp_len,
                                             &out);
    jxl_frame_free(alloc, &frame);
    jxl_parsed_image_header_free_embedded_icc(alloc, &parsed);

    st = jbr_status_to_jxl(jst);
    if (st != JXL_OK) {
        jxl_jbr_output_free(alloc, &out);
        decoder_set_error(dec, "JPEG reconstruction failed");
        return st;
    }

    *jpeg_out = out.data;
    *jpeg_len = out.len;
    jxl_free(alloc, dec->last_error);
    dec->last_error = NULL;
    return JXL_OK;
}
#endif
