// SPDX-License-Identifier: MIT OR Apache-2.0
#include "image_internal.h"
#include "icc_decode.h"

#include <string.h>

extern jxl_bs_status_t jxl_image_metadata_parse(jxl_bs *bs, jxl_parsed_image_header *out);

jxl_bs_status_t jxl_image_header_parse(jxl_bs *bs, jxl_parsed_image_header *out) {
    uint32_t signature = 0;
    jxl_bs_status_t st = jxl_bs_read_bits(bs, 16, &signature);
    if (st != JXL_BS_OK) {
        return st;
    }
    if (signature != 0x0aff) {
        return JXL_BS_VALIDATION_FAILED;
    }

    memset(out, 0, sizeof(*out));
    st = jxl_size_header_parse(bs, &out->size);
    if (st != JXL_BS_OK) {
        return st;
    }
    st = jxl_image_metadata_parse(bs, out);
    if (st != JXL_BS_OK) {
        return st;
    }

    if (out->num_extra_channels > 256) {
        return JXL_BS_PROFILE_CONFORMANCE;
    }

    return JXL_BS_OK;
}

jxl_bs_status_t jxl_image_skip_post_header(jxl_allocator_state *alloc, jxl_bs *bs,
                                           const jxl_parsed_image_header *parsed) {
    if (bs == NULL || parsed == NULL) {
        return JXL_BS_VALIDATION_FAILED;
    }
    if (parsed->colour.have_icc_profile) {
        jxl_bs_status_t st = jxl_icc_skip(alloc, bs);
        if (st != JXL_BS_OK) {
            return st;
        }
    }
    return jxl_bs_zero_pad_to_byte(bs);
}

void jxl_parsed_image_header_free_embedded_icc(jxl_allocator_state *alloc,
                                             jxl_parsed_image_header *parsed) {
    if (alloc == NULL || parsed == NULL) {
        return;
    }
    if (parsed->embedded_icc != NULL) {
        jxl_free(alloc, parsed->embedded_icc);
        parsed->embedded_icc = NULL;
        parsed->embedded_icc_len = 0;
    }
}

jxl_bs_status_t jxl_image_decode_post_header(jxl_allocator_state *alloc, jxl_bs *bs,
                                             jxl_parsed_image_header *parsed) {
    if (bs == NULL || parsed == NULL) {
        return JXL_BS_VALIDATION_FAILED;
    }
    if (parsed->colour.have_icc_profile) {
        jxl_bs_status_t st;
        jxl_parsed_image_header_free_embedded_icc(alloc, parsed);
        st = jxl_icc_decode(alloc, bs, &parsed->embedded_icc, &parsed->embedded_icc_len);
        if (st != JXL_BS_OK) {
            return st;
        }
    }
    return jxl_bs_zero_pad_to_byte(bs);
}

uint32_t jxl_parsed_ec_bit_depth(const jxl_parsed_image_header *parsed, uint32_t ec_idx) {
    if (parsed == NULL) {
        return 8;
    }
    if (ec_idx < parsed->ec_bit_depth_count) {
        return parsed->ec_bit_depth[ec_idx];
    }
    return parsed->bit_depth_bits;
}

static void size_with_orientation(uint32_t orientation, uint32_t w, uint32_t h, uint32_t *out_w,
                                  uint32_t *out_h) {
    if (orientation >= 5 && orientation <= 8) {
        *out_w = h;
        *out_h = w;
    } else {
        *out_w = w;
        *out_h = h;
    }
}

void jxl_parsed_image_header_to_public(const jxl_parsed_image_header *parsed,
                                       jxl_image_header *out) {
    size_with_orientation(parsed->orientation, parsed->size.width, parsed->size.height, &out->width,
                          &out->height);
    out->bit_depth = parsed->bit_depth_bits;
    out->num_extra_channels = parsed->num_extra_channels;
    out->have_animation = parsed->have_animation;
}

void jxl_parsed_image_header_geometry(const jxl_parsed_image_header *parsed,
                                      jxl_image_geometry *out) {
    if (parsed == NULL || out == NULL) {
        return;
    }
    out->codestream_width = parsed->size.width;
    out->codestream_height = parsed->size.height;
    out->orientation = parsed->orientation;
}
