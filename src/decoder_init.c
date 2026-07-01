// SPDX-License-Identifier: MIT OR Apache-2.0
#include "image/image_internal.h"
#include "codestream_collect.h"

#include "bitstream/error.h"
#include "jxl_oxide/jxl_status.h"

static jxl_status_t bs_to_jxl(jxl_bs_status_t st) {
    switch (st) {
    case JXL_BS_OK:
        return JXL_OK;
    case JXL_BS_EOF:
        return JXL_NEED_MORE_DATA;
    case JXL_BS_INVALID_BOX:
    case JXL_BS_NON_ZERO_PADDING:
    case JXL_BS_INVALID_FLOAT:
    case JXL_BS_INVALID_ENUM:
    case JXL_BS_VALIDATION_FAILED:
        return JXL_ERROR_INVALID_INPUT;
    case JXL_BS_PROFILE_CONFORMANCE:
        return JXL_ERROR_UNSUPPORTED;
    case JXL_BS_CANNOT_SKIP:
    case JXL_BS_NOT_ALIGNED:
        return JXL_ERROR_UNSUPPORTED;
    }
    return JXL_ERROR_INVALID_INPUT;
}

jxl_status_t jxl_decoder_init_from_codestream(jxl_allocator_state *alloc,
                                              const uint8_t *codestream, size_t codestream_len,
                                              jxl_image_header *header_out,
                                              uint32_t *num_color_channels_out,
                                              jxl_image_geometry *geometry_out,
                                              int *xyb_encoded_out,
                                              jxl_parsed_image_header *parsed_out,
                                              size_t *frames_bit_offset_out) {
    jxl_bs bs;
    jxl_parsed_image_header parsed;
    jxl_bs_status_t pst;
    jxl_status_t status;

    if (codestream == NULL || codestream_len < 3) {
        return JXL_NEED_MORE_DATA;
    }

    memset(&parsed, 0, sizeof(parsed));
    jxl_bs_init(&bs, codestream, codestream_len);
    pst = jxl_image_header_parse(&bs, &parsed);
    if (pst == JXL_BS_EOF) {
        return JXL_NEED_MORE_DATA;
    }
    status = bs_to_jxl(pst);
    if (status != JXL_OK) {
        return status;
    }

    if (parsed_out != NULL) {
        status = bs_to_jxl(jxl_image_decode_post_header(alloc, &bs, &parsed));
        if (status != JXL_OK) {
            jxl_parsed_image_header_free_embedded_icc(alloc, &parsed);
            return status;
        }
        *parsed_out = parsed;
        if (frames_bit_offset_out != NULL) {
            *frames_bit_offset_out = bs.num_read_bits / 8;
        }
    }

    /* Animated files may init and render keyframe 0; multi-keyframe API remains v2. */
    jxl_parsed_image_header_to_public(&parsed, header_out);
    if (num_color_channels_out != NULL) {
        *num_color_channels_out =
            parsed.colour.colour_space == JXL_COLOUR_SPACE_GRAY_I ? 1u : 3u;
    }
    if (geometry_out != NULL) {
        jxl_parsed_image_header_geometry(&parsed, geometry_out);
    }
    if (xyb_encoded_out != NULL) {
        *xyb_encoded_out = parsed.xyb_encoded;
    }
    return JXL_OK;
}

jxl_status_t jxl_decoder_init_from_input(jxl_allocator_state *alloc, const uint8_t *input,
                                         size_t input_len, jxl_image_header *header_out,
                                         uint32_t *num_color_channels_out,
                                         jxl_image_geometry *geometry_out, int *xyb_encoded_out,
                                         char **error_out) {
    size_t cs_len;
    jxl_status_t status;
    uint8_t *codestream = NULL;

    cs_len = 0;
    status = jxl_collect_codestream(alloc, input, input_len, &codestream, &cs_len);
    if (status != JXL_OK) {
        if (error_out != NULL) {
            *error_out = NULL;
        }
        return status;
    }
    status = jxl_decoder_init_from_codestream(alloc, codestream, cs_len, header_out,
                                              num_color_channels_out, geometry_out,
                                              xyb_encoded_out, NULL, NULL);
    jxl_free(alloc, codestream);
    return status;
}
