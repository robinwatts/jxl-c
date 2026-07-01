// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_IMAGE_INTERNAL_H_
#define JXL_IMAGE_INTERNAL_H_

#include "allocator.h"
#include "context.h"
#include "bitstream/bitstream.h"
#include "bitstream/error.h"
#include "jxl_oxide/jxl_types.h"
#include "render/features/upsampling.h"

#include <stdlib.h>

typedef enum {
    JXL_COLOUR_SPACE_RGB_I = 0,
    JXL_COLOUR_SPACE_GRAY_I = 1,
    JXL_COLOUR_SPACE_XYB_I = 2,
    JXL_COLOUR_SPACE_UNKNOWN_I = 3,
} jxl_colour_space_i;

typedef struct {
    uint32_t width;
    uint32_t height;
} jxl_size_header;

typedef enum {
    JXL_WHITE_POINT_D65_I = 0,
    JXL_WHITE_POINT_DCI_I = 1,
    JXL_WHITE_POINT_E_I = 2,
    JXL_WHITE_POINT_CUSTOM_I = 3,
} jxl_white_point_i;

typedef enum {
    JXL_PRIMARIES_SRGB_I = 0,
    JXL_PRIMARIES_P3_I = 1,
    JXL_PRIMARIES_BT2100_I = 2,
    JXL_PRIMARIES_CUSTOM_I = 3,
} jxl_primaries_i;

typedef enum {
    JXL_TRANSFER_LINEAR_I = 0,
    JXL_TRANSFER_SRGB_I = 1,
    JXL_TRANSFER_BT709_I = 2,
    JXL_TRANSFER_PQ_I = 3,
    JXL_TRANSFER_HLG_I = 4,
    JXL_TRANSFER_GAMMA_I = 5,
} jxl_transfer_function_i;

typedef enum {
    JXL_RENDERING_PERCEPTUAL_I = 0,
    JXL_RENDERING_RELATIVE_I = 1,
    JXL_RENDERING_SATURATION_I = 2,
    JXL_RENDERING_ABSOLUTE_I = 3,
} jxl_rendering_intent_i;

typedef struct {
    int have_icc_profile;
    jxl_colour_space_i colour_space;
    jxl_white_point_i white_point;
    int32_t custom_white_x;
    int32_t custom_white_y;
    jxl_primaries_i primaries;
    int32_t custom_red_x;
    int32_t custom_red_y;
    int32_t custom_green_x;
    int32_t custom_green_y;
    int32_t custom_blue_x;
    int32_t custom_blue_y;
    jxl_transfer_function_i transfer;
    uint32_t gamma_1e7;
    jxl_rendering_intent_i rendering_intent;
} jxl_colour_encoding_parsed;

typedef struct {
    float inv_mat[3][3];
    float opsin_bias[3];
    float quant_bias[3];
    float quant_bias_numerator;
} jxl_opsin_inverse_parsed;

typedef struct {
    jxl_size_header size;
    uint32_t orientation;
    uint32_t bit_depth_bits;
    uint32_t num_extra_channels;
    uint8_t ec_dim_shift[256];
    uint32_t ec_dim_shift_count;
    uint32_t ec_bit_depth_count;
    uint8_t ec_bit_depth[256];
    int have_animation;
    int have_timecodes;
    int xyb_encoded;
    /* When true (default), modular samples use i16 wrapping storage (Rust narrow path). */
    int modular_16bit_buffers;
    /* First extra channel when it is Alpha; -1 if unknown or not alpha. */
    int alpha_associated;
    jxl_colour_encoding_parsed colour;
    jxl_opsin_inverse_parsed opsin_inverse;
    jxl_upsampling_weights upsampling_weights;
    uint8_t *embedded_icc;
    size_t embedded_icc_len;
} jxl_parsed_image_header;

uint32_t jxl_size_header_default_width(uint32_t ratio, uint32_t w_div8, uint32_t height);

jxl_bs_status_t jxl_size_header_parse(jxl_bs *bs, jxl_size_header *out);
jxl_bs_status_t jxl_preview_header_parse(jxl_bs *bs, jxl_size_header *out);
jxl_bs_status_t jxl_image_header_parse(jxl_bs *bs, jxl_parsed_image_header *out);

jxl_bs_status_t jxl_image_skip_post_header(jxl_allocator_state *alloc, jxl_bs *bs,
                                           const jxl_parsed_image_header *parsed);

jxl_bs_status_t jxl_image_decode_post_header(jxl_allocator_state *alloc, jxl_bs *bs,
                                             jxl_parsed_image_header *parsed);

void jxl_parsed_image_header_free_embedded_icc(jxl_allocator_state *alloc,
                                             jxl_parsed_image_header *parsed);

uint32_t jxl_parsed_ec_bit_depth(const jxl_parsed_image_header *parsed, uint32_t ec_idx);

void jxl_parsed_image_header_to_public(const jxl_parsed_image_header *parsed,
                                       jxl_image_header *out);

/* Codestream pixel grid before EXIF orientation is applied at export. */
typedef struct {
    uint32_t codestream_width;
    uint32_t codestream_height;
    uint32_t orientation;
} jxl_image_geometry;

void jxl_parsed_image_header_geometry(const jxl_parsed_image_header *parsed,
                                      jxl_image_geometry *out);

/* Rust Render::narrow_modular(); set JXL_FORCE_WIDE_BUFFERS=1 to force i32 storage. */
jxl_inline int jxl_parsed_narrow_modular(const jxl_context *ctx,
                                       const jxl_parsed_image_header *parsed) {
    if (JXL_DEBUG_FLAG(ctx, force_wide_buffers)) {
        return 0;
    }
    return parsed != NULL && parsed->modular_16bit_buffers;
}

#endif /* JXL_IMAGE_INTERNAL_H_ */
