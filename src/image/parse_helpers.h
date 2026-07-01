// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_IMAGE_PARSE_HELPERS_H_
#define JXL_IMAGE_PARSE_HELPERS_H_

#include "image_internal.h"

jxl_bs_status_t jxl_image_skip_name(jxl_bs *bs);
jxl_bs_status_t jxl_colour_encoding_parse(jxl_bs *bs, jxl_colour_encoding_parsed *out);
jxl_bs_status_t jxl_bit_depth_parse(jxl_bs *bs, uint32_t *bits_per_sample_out);
jxl_bs_status_t jxl_extensions_parse(jxl_bs *bs);
jxl_bs_status_t skip_extra_channel(jxl_bs *bs);
jxl_bs_status_t jxl_extra_channel_parse(jxl_bs *bs, int *is_alpha_out, int *alpha_associated_out,
                                        uint32_t *bit_depth_out, uint32_t *dim_shift_out);
jxl_bs_status_t skip_tone_mapping(jxl_bs *bs);
void jxl_opsin_inverse_set_defaults(jxl_opsin_inverse_parsed *out);
jxl_bs_status_t jxl_opsin_inverse_parse(jxl_bs *bs, jxl_opsin_inverse_parsed *out);
jxl_bs_status_t skip_opsin_inverse(jxl_bs *bs);
jxl_bs_status_t skip_f16_array(jxl_bs *bs, size_t count);
jxl_bs_status_t skip_animation_header(jxl_bs *bs, int *have_timecodes_out);

#endif /* JXL_IMAGE_PARSE_HELPERS_H_ */
