// SPDX-License-Identifier: MIT OR Apache-2.0
#include "image_internal.h"
#include "parse_helpers.h"

#include <string.h>

jxl_bs_status_t jxl_image_metadata_parse(jxl_bs *bs, jxl_parsed_image_header *out) {
    int all_default = 1;
    int extra_fields = 0;
    int have_intr_size = 0;
    int have_preview = 0;
    int have_animation = 0;
    uint32_t num_extra = 0;
    int default_m = 0;
    uint32_t cw_mask = 0;
    jxl_bs_status_t st;

    /* Caller (image_header_parse) zeroes `out` and fills size before this runs. */
    out->orientation = 1;
    out->bit_depth_bits = 8;
    out->xyb_encoded = 1;
    out->modular_16bit_buffers = 1;
    out->alpha_associated = -1;
    jxl_opsin_inverse_set_defaults(&out->opsin_inverse);
    jxl_upsampling_weights_set_defaults(&out->upsampling_weights);

    st = jxl_bs_read_bool(bs, &all_default);
    if (st != JXL_BS_OK) {
        return st;
    }

    if (!all_default) {
        st = jxl_bs_read_bool(bs, &extra_fields);
        if (st != JXL_BS_OK) {
            return st;
        }
    }

    if (extra_fields) {
        uint32_t orientation = 0;
        st = jxl_bs_read_bits(bs, 3, &orientation);
        if (st != JXL_BS_OK) {
            return st;
        }
        out->orientation = orientation + 1;

        st = jxl_bs_read_bool(bs, &have_intr_size);
        if (st != JXL_BS_OK) {
            return st;
        }
        if (have_intr_size) {
            jxl_size_header ignored;
            st = jxl_size_header_parse(bs, &ignored);
            if (st != JXL_BS_OK) {
                return st;
            }
        }

        st = jxl_bs_read_bool(bs, &have_preview);
        if (st != JXL_BS_OK) {
            return st;
        }
        if (have_preview) {
            jxl_size_header ignored;
            st = jxl_preview_header_parse(bs, &ignored);
            if (st != JXL_BS_OK) {
                return st;
            }
        }

        st = jxl_bs_read_bool(bs, &have_animation);
        if (st != JXL_BS_OK) {
            return st;
        }
        out->have_animation = have_animation;
        if (have_animation) {
            st = skip_animation_header(bs, &out->have_timecodes);
            if (st != JXL_BS_OK) {
                return st;
            }
        }
    }

    if (!all_default) {
        uint32_t i;
        int modular_16;
        const jxl_u32_spec num_specs[4] = {JXL_U32_C(0), JXL_U32_C(1), JXL_U32_BITS(2, 4),
                                           JXL_U32_BITS(1, 12)};
        st = jxl_bit_depth_parse(bs, &out->bit_depth_bits);
        if (st != JXL_BS_OK) {
            return st;
        }

        modular_16 = 1;
        st = jxl_bs_read_bool(bs, &modular_16);
        if (st != JXL_BS_OK) {
            return st;
        }
        out->modular_16bit_buffers = modular_16;

        st = jxl_bs_read_u32(bs, num_specs, &num_extra);
        if (st != JXL_BS_OK) {
            return st;
        }
        out->num_extra_channels = num_extra;

        for (i = 0; i < num_extra; ++i) {
            int is_alpha = 0;
            int alpha_associated = 0;
            uint32_t dim_shift = 0;
            uint32_t ec_bits = 0;
            st = jxl_extra_channel_parse(bs, &is_alpha, &alpha_associated, &ec_bits, &dim_shift);
            if (st != JXL_BS_OK) {
                return st;
            }
            if (i < sizeof(out->ec_dim_shift)) {
                out->ec_dim_shift[i] = (uint8_t)dim_shift;
                out->ec_dim_shift_count = i + 1u;
            }
            if (i < sizeof(out->ec_bit_depth)) {
                out->ec_bit_depth[i] = (uint8_t)(ec_bits > 255 ? 255 : ec_bits);
                out->ec_bit_depth_count = i + 1u;
            }
            if (is_alpha && out->alpha_associated < 0) {
                out->alpha_associated = alpha_associated;
            }
        }

        st = jxl_bs_read_bool(bs, &out->xyb_encoded);
        if (st != JXL_BS_OK) {
            return st;
        }

        st = jxl_colour_encoding_parse(bs, &out->colour);
        if (st != JXL_BS_OK) {
            return st;
        }
    } else {
        out->colour.colour_space = JXL_COLOUR_SPACE_XYB_I;
    }

    if (extra_fields) {
        st = skip_tone_mapping(bs);
        if (st != JXL_BS_OK) {
            return st;
        }
    }

    if (!all_default) {
        st = jxl_extensions_parse(bs);
        if (st != JXL_BS_OK) {
            return st;
        }
    }

    st = jxl_bs_read_bool(bs, &default_m);
    if (st != JXL_BS_OK) {
        return st;
    }

    if (!default_m && out->xyb_encoded) {
        st = jxl_opsin_inverse_parse(bs, &out->opsin_inverse);
        if (st != JXL_BS_OK) {
            return st;
        }
    }

    if (!default_m) {
        st = jxl_bs_read_bits(bs, 3, &cw_mask);
        if (st != JXL_BS_OK) {
            return st;
        }
        if (cw_mask & 1) {
            size_t i;
            for (i = 0; i < 15; ++i) {
                st = jxl_bs_read_f16_as_f32(bs, &out->upsampling_weights.up2[i]);
                if (st != JXL_BS_OK) {
                    return st;
                }
            }
        }
        if (cw_mask & 2) {
            size_t i;
            for (i = 0; i < 55; ++i) {
                st = jxl_bs_read_f16_as_f32(bs, &out->upsampling_weights.up4[i]);
                if (st != JXL_BS_OK) {
                    return st;
                }
            }
        }
        if (cw_mask & 4) {
            size_t i;
            for (i = 0; i < 210; ++i) {
                st = jxl_bs_read_f16_as_f32(bs, &out->upsampling_weights.up8[i]);
                if (st != JXL_BS_OK) {
                    return st;
                }
            }
        }
    }

    return JXL_BS_OK;
}
