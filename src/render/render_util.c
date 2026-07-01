// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render_util.h"

#include "frame/filter.h"

#include <string.h>

static uint32_t trailing_zeros_u32(uint32_t v) {
    uint32_t n;
    if (v == 0) {
        return 0;
    }
    n = 0;
    while ((v & 1u) == 0) {
        n += 1;
        v >>= 1;
    }
    return n;
}

static uint32_t ilog2_u32(uint32_t v) {
    uint32_t n = 0;
    while (v > 1u) {
        n += 1;
        v >>= 1;
    }
    return n;
}

static uint32_t sample_dim(uint32_t dim, uint32_t upsampling, uint32_t lf_level) {
    if (upsampling > 1u) {
        dim = (dim + upsampling - 1u) / upsampling;
    }
    if (lf_level > 0u) {
        uint32_t div = 1u << (3u * lf_level);
        dim = (dim + div - 1u) >> (3u * lf_level);
    }
    return dim;
}

static jxl_modular_region image_region_to_frame(const jxl_frame_header *fh,
                                                jxl_modular_region image_region) {
    jxl_modular_region translated;
    jxl_modular_region full;
    int32_t x0;
    int32_t y0;
    jxl_modular_region frame_region;
    if (fh == NULL) {
        return image_region;
    }
    full = jxl_modular_region_with_size(fh->width, fh->height);
    x0 = fh->have_crop ? fh->x0 : 0;
    y0 = fh->have_crop ? fh->y0 : 0;
    translated.left = image_region.left - x0;
    translated.top = image_region.top - y0;
    translated.width = image_region.width;
    translated.height = image_region.height;

    frame_region = jxl_modular_region_intersection(translated, full);
    if (fh->lf_level > 0u) {
        frame_region = jxl_modular_region_downsample(frame_region, fh->lf_level * 3u);
    }
    return frame_region;
}

static jxl_modular_region pad_lf_region(const jxl_frame_header *fh, jxl_modular_region frame_region) {
    if (fh == NULL || fh->lf_level == 0u) {
        return frame_region;
    }
    return jxl_modular_region_pad(frame_region, 4u * fh->lf_level + 32u);
}

static uint32_t max_upsample_factor(const jxl_parsed_image_header *parsed,
                                    const jxl_frame_header *fh) {
                                        size_t i;
    uint32_t color_factor = trailing_zeros_u32(fh != NULL ? fh->upsampling : 1u);
    uint32_t max_factor = color_factor;
    size_t ec_count;
    if (parsed == NULL || fh == NULL || fh->ec_upsampling == NULL) {
        return max_factor;
    }
    ec_count = fh->ec_upsampling_len;
    if (parsed->ec_dim_shift_count < ec_count) {
        ec_count = parsed->ec_dim_shift_count;
    }
    for (i = 0; i < ec_count; ++i) {
        uint32_t ec_factor = ilog2_u32(fh->ec_upsampling[i]) + (uint32_t)parsed->ec_dim_shift[i];
        if (ec_factor > max_factor) {
            max_factor = ec_factor;
        }
    }
    return max_factor;
}

static jxl_modular_region pad_upsampling(const jxl_parsed_image_header *parsed,
                                         const jxl_frame_header *fh,
                                         jxl_modular_region frame_region) {
    uint32_t max_factor = max_upsample_factor(parsed, fh);
    uint32_t pad_amount;
    if (max_factor == 0u) {
        return frame_region;
    }
    pad_amount = 2u + (max_factor - 1u) / 3u;
    return jxl_modular_region_upsample(
        jxl_modular_region_pad(jxl_modular_region_downsample(frame_region, max_factor), pad_amount),
        max_factor);
}

static jxl_modular_region pad_color_region(const jxl_parsed_image_header *parsed,
                                           const jxl_frame_header *fh,
                                           jxl_modular_region frame_region) {
    uint32_t color_factor;
    jxl_modular_region region;
    if (fh == NULL) {
        return frame_region;
    }
    color_factor = ilog2_u32(fh->upsampling);
    region =
        jxl_modular_region_downsample(pad_upsampling(parsed, fh, frame_region), color_factor);

    if (jxl_epf_enabled(&fh->restoration)) {
        uint32_t epf_pad = 6u;
        if (fh->restoration.epf.iters == 1u) {
            epf_pad = 2u;
        } else if (fh->restoration.epf.iters == 2u) {
            epf_pad = 5u;
        }
        region = jxl_modular_region_pad(region, epf_pad);
    }
    if (jxl_gabor_enabled(&fh->restoration)) {
        region = jxl_modular_region_pad(region, 1u);
    }
    if (fh->do_ycbcr) {
        region = jxl_modular_region_upsample(
            jxl_modular_region_downsample(jxl_modular_region_pad(region, 1u), 2u), 2u);
    }
    if (jxl_epf_enabled(&fh->restoration)) {
        region = jxl_modular_region_container_aligned(region, 8u);
    }
    return region;
}

void jxl_render_compute_padded_regions(const jxl_parsed_image_header *parsed,
                                       const jxl_frame_header *fh,
                                       jxl_modular_region image_region,
                                       jxl_render_padded_regions *out) {
    jxl_modular_region frame_region;
    jxl_modular_region color_bounds;
    jxl_modular_region upsampled_full;
    if (out == NULL) {
        return;
    }
    memset(&out->color_padded, 0, sizeof(out->color_padded));
    memset(&out->upsampling_valid, 0, sizeof(out->upsampling_valid));

    if (fh == NULL) {
        return;
    }

    frame_region = pad_lf_region(fh, image_region_to_frame(fh, image_region));

    color_bounds = jxl_modular_region_with_size(
        jxl_frame_header_color_sample_width(fh), jxl_frame_header_color_sample_height(fh));
    out->color_padded =
        jxl_modular_region_intersection(pad_color_region(parsed, fh, frame_region), color_bounds);

    upsampled_full = jxl_modular_region_with_size(
        sample_dim(fh->width, 1u, fh->lf_level), sample_dim(fh->height, 1u, fh->lf_level));
    out->upsampling_valid = jxl_modular_region_intersection(
        pad_upsampling(parsed, fh, frame_region), upsampled_full);
}

const jxl_modular_region *jxl_render_resolve_color_filter_region_for_image(
    const jxl_parsed_image_header *parsed, const jxl_frame_header *fh,
    jxl_modular_region image_region, const jxl_modular_region *caller_filter_region,
    jxl_modular_region *scratch) {
    jxl_render_padded_regions padded;
    if (caller_filter_region != NULL) {
        return caller_filter_region;
    }
    if (parsed == NULL || fh == NULL || scratch == NULL) {
        return NULL;
    }
    jxl_render_compute_padded_regions(parsed, fh, image_region, &padded);
    *scratch = padded.color_padded;
    return scratch;
}

const jxl_modular_region *jxl_render_resolve_color_filter_region(
    const jxl_parsed_image_header *parsed, const jxl_frame_header *fh,
    const jxl_modular_region *output_region, const jxl_modular_region *caller_filter_region,
    jxl_modular_region *scratch) {
    jxl_modular_region image_region;
    if (parsed == NULL || fh == NULL) {
        return caller_filter_region;
    }
    image_region =
        jxl_modular_region_with_size(parsed->size.width, parsed->size.height);
    if (output_region != NULL) {
        image_region = *output_region;
    }
    return jxl_render_resolve_color_filter_region_for_image(parsed, fh, image_region,
                                                            caller_filter_region, scratch);
}

jxl_modular_region jxl_render_composite_frame_region(const jxl_parsed_image_header *parsed,
                                                     const jxl_frame_header *fh,
                                                     jxl_modular_region oriented_image_region) {
    jxl_modular_region frame_region;
    int32_t x0;
    int32_t y0;
    uint32_t upsample_log;
    if (fh == NULL) {
        return oriented_image_region;
    }
    x0 = fh->have_crop ? fh->x0 : 0;
    y0 = fh->have_crop ? fh->y0 : 0;
    frame_region.left = oriented_image_region.left - x0;
    frame_region.top = oriented_image_region.top - y0;
    frame_region.width = oriented_image_region.width;
    frame_region.height = oriented_image_region.height;

    if (fh->lf_level > 0u) {
        frame_region = jxl_modular_region_downsample(frame_region, fh->lf_level * 3u);
    }
    frame_region = pad_lf_region(fh, frame_region);
    frame_region = pad_color_region(parsed, fh, frame_region);
    upsample_log = trailing_zeros_u32(fh->upsampling);
    if (upsample_log > 0u) {
        frame_region = jxl_modular_region_upsample(frame_region, upsample_log);
    }
    if (jxl_frame_header_is_normal_frame(fh) && parsed != NULL) {
        jxl_modular_region full_in_frame = jxl_modular_region_with_size(parsed->size.width,
                                                                        parsed->size.height);
        full_in_frame.left -= x0;
        full_in_frame.top -= y0;
        frame_region = jxl_modular_region_intersection(frame_region, full_in_frame);
    }
    return frame_region;
}
