// SPDX-License-Identifier: MIT OR Apache-2.0
#include "param.h"

#include "allocator.h"
#include "frame/frame_header.h"
#include "image/image_internal.h"

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

jxl_channel_shift jxl_channel_shift_from_shift(uint32_t shift) {
    jxl_channel_shift s;
    memset(&s, 0, sizeof(s));
    s.kind = JXL_CHANNEL_SHIFT_SHIFTS;
    s.shift = shift;
    return s;
}

jxl_channel_shift jxl_channel_shift_from_jpeg_upsampling(const uint32_t jpeg_upsampling[3],
                                                           size_t idx) {
                                                               size_t i;
    jxl_channel_shift s = {0};
    int hscale;
    int vscale;
    uint32_t upsampling;
    s.kind = JXL_CHANNEL_SHIFT_JPEG_UPSAMPLING;
    if (jpeg_upsampling == NULL || idx > 2) {
        return s;
    }
    hscale = 0;
    vscale = 0;
    for (i = 0; i < 3; ++i) {
        uint32_t v = jpeg_upsampling[i];
        if (v == 1 || v == 2) {
            hscale = 1;
        }
        if (v == 1 || v == 3) {
            vscale = 1;
        }
    }
    s.has_h_subsample = hscale;
    s.has_v_subsample = vscale;
    upsampling = jpeg_upsampling[idx];
    switch (upsampling) {
    case 0:
        s.h_subsample = hscale;
        s.v_subsample = vscale;
        break;
    case 1:
        s.h_subsample = 0;
        s.v_subsample = 0;
        break;
    case 2:
        s.h_subsample = 0;
        s.v_subsample = vscale;
        break;
    case 3:
        s.h_subsample = hscale;
        s.v_subsample = 0;
        break;
    default:
        break;
    }
    return s;
}

int32_t jxl_channel_shift_hshift(const jxl_channel_shift *s) {
    if (s == NULL) {
        return 0;
    }
    switch (s->kind) {
    case JXL_CHANNEL_SHIFT_JPEG_UPSAMPLING:
        return s->h_subsample ? 1 : 0;
    case JXL_CHANNEL_SHIFT_SHIFTS:
        return (int32_t)s->shift;
    case JXL_CHANNEL_SHIFT_RAW:
        return s->raw_h;
    }
    return 0;
}

int32_t jxl_channel_shift_vshift(const jxl_channel_shift *s) {
    if (s == NULL) {
        return 0;
    }
    switch (s->kind) {
    case JXL_CHANNEL_SHIFT_JPEG_UPSAMPLING:
        return s->v_subsample ? 1 : 0;
    case JXL_CHANNEL_SHIFT_SHIFTS:
        return (int32_t)s->shift;
    case JXL_CHANNEL_SHIFT_RAW:
        return s->raw_v;
    }
    return 0;
}

static uint32_t div_ceil_u32(uint32_t a, uint32_t b) {
    return b == 0 ? a : (a + b - 1) / b;
}

void jxl_channel_shift_shift_size(const jxl_channel_shift *s, uint32_t width, uint32_t height,
                                  uint32_t *out_w, uint32_t *out_h) {
    if (out_w != NULL) {
        *out_w = width;
    }
    if (out_h != NULL) {
        *out_h = height;
    }
    if (s == NULL) {
        return;
    }
    switch (s->kind) {
    case JXL_CHANNEL_SHIFT_JPEG_UPSAMPLING: {
        uint32_t w = width;
        uint32_t h = height;
        if (s->has_h_subsample) {
            uint32_t size = div_ceil_u32(w, 2);
            w = s->h_subsample ? size : size * 2;
        }
        if (s->has_v_subsample) {
            uint32_t size = div_ceil_u32(h, 2);
            h = s->v_subsample ? size : size * 2;
        }
        if (out_w != NULL) {
            *out_w = w;
        }
        if (out_h != NULL) {
            *out_h = h;
        }
        break;
    }
    case JXL_CHANNEL_SHIFT_SHIFTS: {
        uint32_t add = (1u << s->shift) - 1u;
        if (out_w != NULL) {
            *out_w = (width + add) >> s->shift;
        }
        if (out_h != NULL) {
            *out_h = (height + add) >> s->shift;
        }
        break;
    }
    case JXL_CHANNEL_SHIFT_RAW: {
        int32_t h = s->raw_h;
        int32_t v = s->raw_v;
        uint32_t add_h = h >= 0 ? ((1u << (uint32_t)h) - 1u) : 0;
        uint32_t add_v = v >= 0 ? ((1u << (uint32_t)v) - 1u) : 0;
        if (out_w != NULL) {
            *out_w = h >= 0 ? (width + add_h) >> (uint32_t)h : width;
        }
        if (out_h != NULL) {
            *out_h = v >= 0 ? (height + add_v) >> (uint32_t)v : height;
        }
        break;
    }
    }
}

jxl_channel_shift jxl_channel_shift_raw(int32_t h, int32_t v) {
    jxl_channel_shift s = {0};
    s.kind = JXL_CHANNEL_SHIFT_RAW;
    s.raw_h = h;
    s.raw_v = v;
    return s;
}

void jxl_modular_params_init(jxl_modular_params *p) {
    memset(p, 0, sizeof(*p));
}

void jxl_modular_params_free(jxl_allocator_state *alloc, jxl_modular_params *p) {
    if (p == NULL || alloc == NULL) {
        return;
    }
    jxl_free(alloc, p->channels);
    p->channels = NULL;
    p->num_channels = 0;
}

int jxl_modular_params_set_channels(jxl_allocator_state *alloc, jxl_modular_params *p,
                                    uint32_t width, uint32_t height, uint32_t group_dim,
                                    uint32_t bit_depth, const jxl_channel_shift *shifts,
                                    size_t num_shifts) {
                                        size_t i;
    if (alloc == NULL || p == NULL) {
        return 0;
    }
    jxl_modular_params_free(alloc, p);
    p->group_dim = group_dim;
    p->bit_depth = bit_depth;
    p->narrow_buffer = 0;
    p->num_channels = num_shifts;
    if (num_shifts == 0) {
        return 1;
    }
    p->channels = jxl_calloc(alloc, num_shifts, sizeof(*p->channels));
    if (p->channels == NULL) {
        return 0;
    }
    for (i = 0; i < num_shifts; ++i) {
        p->channels[i].width = width;
        p->channels[i].height = height;
        p->channels[i].shift = shifts[i];
    }
    return 1;
}

int jxl_modular_params_set_for_modular_frame(jxl_allocator_state *alloc, jxl_context *ctx,
                                             jxl_modular_params *p,
                                             const jxl_parsed_image_header *image,
                                             const jxl_frame_header *frame) {
                                                 size_t ec;
    jxl_channel_shift shifts[64];
    size_t n;
    uint32_t cw;
    uint32_t ch;
    uint32_t gd;
    uint32_t color_us;
    size_t ec_count;
    if (alloc == NULL || p == NULL || image == NULL || frame == NULL) {
        return 0;
    }
    if (frame->encoding != JXL_FRAME_ENCODING_MODULAR) {
        return 0;
    }

    cw = jxl_frame_header_color_sample_width(frame);
    ch = jxl_frame_header_color_sample_height(frame);
    gd = jxl_frame_header_group_dim(frame);

    n = 0;

    if (frame->do_ycbcr) {
        size_t i;
        for (i = 0; i < 3 && n < sizeof(shifts) / sizeof(shifts[0]); ++i) {
            shifts[n++] = jxl_channel_shift_from_jpeg_upsampling(frame->jpeg_upsampling, i);
        }
    } else {
        uint32_t i;
        for (i = 0; i < frame->encoded_color_channels && n < sizeof(shifts) / sizeof(shifts[0]);
             ++i) {
            shifts[n++] = jxl_channel_shift_from_shift(0);
        }
    }

    color_us = trailing_zeros_u32(frame->upsampling);
    ec_count = image->num_extra_channels;
    if (ec_count > frame->ec_upsampling_len) {
        ec_count = frame->ec_upsampling_len;
    }
    for (ec = 0; ec < ec_count && n < sizeof(shifts) / sizeof(shifts[0]); ++ec) {
        uint32_t ec_us = trailing_zeros_u32(frame->ec_upsampling[ec]);
        uint32_t dim_shift =
            ec < image->ec_dim_shift_count ? (uint32_t)image->ec_dim_shift[ec] : 0u;
        int32_t actual = (int32_t)ec_us + (int32_t)dim_shift - (int32_t)color_us;
        if (actual < 0) {
            actual = 0;
        }
        shifts[n++] = jxl_channel_shift_from_shift((uint32_t)actual);
    }

    if (!jxl_modular_params_set_channels(alloc, p, cw, ch, gd, image->bit_depth_bits, shifts, n)) {
        return 0;
    }
    p->narrow_buffer = jxl_parsed_narrow_modular(ctx, image);
    return 1;
}

int jxl_modular_params_set_for_vardct_frame(jxl_allocator_state *alloc, jxl_context *ctx,
                                              jxl_modular_params *p,
                                              const jxl_parsed_image_header *image,
                                              const jxl_frame_header *frame) {
                                                size_t ec;
    jxl_channel_shift shifts[64];
    size_t n;
    uint32_t cw;
    uint32_t ch;
    uint32_t gd;
    uint32_t color_us;
    size_t ec_count;
    if (alloc == NULL || p == NULL || image == NULL || frame == NULL) {
        return 0;
    }
    if (frame->encoding != JXL_FRAME_ENCODING_VARDCT) {
        return 0;
    }

    cw = jxl_frame_header_color_sample_width(frame);
    ch = jxl_frame_header_color_sample_height(frame);
    gd = jxl_frame_header_group_dim(frame);

    n = 0;

    color_us = trailing_zeros_u32(frame->upsampling);
    ec_count = image->num_extra_channels;
    if (ec_count > frame->ec_upsampling_len) {
        ec_count = frame->ec_upsampling_len;
    }
    for (ec = 0; ec < ec_count && n < sizeof(shifts) / sizeof(shifts[0]); ++ec) {
        uint32_t ec_us = trailing_zeros_u32(frame->ec_upsampling[ec]);
        uint32_t dim_shift =
            ec < image->ec_dim_shift_count ? (uint32_t)image->ec_dim_shift[ec] : 0u;
        int32_t actual = (int32_t)ec_us + (int32_t)dim_shift - (int32_t)color_us;
        if (actual < 0) {
            actual = 0;
        }
        shifts[n++] = jxl_channel_shift_from_shift((uint32_t)actual);
    }

    if (!jxl_modular_params_set_channels(alloc, p, cw, ch, gd, image->bit_depth_bits, shifts, n)) {
        return 0;
    }
    p->narrow_buffer = jxl_parsed_narrow_modular(ctx, image);
    return 1;
}
