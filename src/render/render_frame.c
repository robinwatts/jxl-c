// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render_frame.h"

#include "frame/patch.h"
#include "frame/filter.h"
#include "modular/image.h"
#include "modular/prepare_subimage.h"
#include "render/modular_encode.h"
#include "render/modular_compose.h"
#include "render/modular_sample.h"
#include "render/render_internal.h"
#include "render/render_util.h"
#include "render/color_encoding_util.h"
#include "render/color_transform.h"
#include "render/color_transform_apply.h"
#include "render/filter/restoration.h"
#include "render/filter/ycbcr.h"
#include "render/subgrid_f32.h"
#include "render/vardct/vardct_encode.h"
#include "modular/param.h"

#include <stdlib.h>
#include <string.h>

static uint32_t color_filter_width(const jxl_frame_header *fh, const jxl_render *r) {
    if (fh == NULL || r == NULL) {
        return 0;
    }
    if (fh->upsampling > 1u) {
        return jxl_frame_header_color_sample_width(fh);
    }
    return r->width;
}

static uint32_t color_filter_height(const jxl_frame_header *fh, const jxl_render *r) {
    if (fh == NULL || r == NULL) {
        return 0;
    }
    if (fh->upsampling > 1u) {
        return jxl_frame_header_color_sample_height(fh);
    }
    return r->height;
}

static jxl_status_t jpeg_upsample_color_planes(jxl_allocator_state *alloc, jxl_render *r,
                                               uint32_t color_planes, const jxl_frame_header *fh) {
                                                   uint32_t p;
    uint32_t target_w = color_filter_width(fh, r);
    uint32_t target_h = color_filter_height(fh, r);
    for (p = 0; p < color_planes; ++p) {
        jxl_status_t st = jxl_render_upsample_plane_jpeg(alloc, r, p, target_w, target_h);
        if (st != JXL_OK) {
            return st;
        }
    }
    return JXL_OK;
}

static int restoration_enabled(jxl_context *ctx, const jxl_frame_header *fh) {
    if (fh == NULL || JXL_DEBUG_FLAG(ctx, skip_restoration)) {
        return 0;
    }
    return jxl_gabor_enabled(&fh->restoration) || jxl_epf_enabled(&fh->restoration);
}

static jxl_status_t apply_color_restoration(const jxl_render_pre_features_params *params);

static jxl_modular_region render_image_region(const jxl_parsed_image_header *parsed,
                                              const jxl_modular_region *output_region) {
    jxl_modular_region image_region =
        jxl_modular_region_with_size(parsed->size.width, parsed->size.height);
    if (output_region != NULL) {
        image_region = *output_region;
    }
    return image_region;
}

static jxl_status_t upsample_written_crop_planes(jxl_allocator_state *alloc, jxl_render *r,
                                                 uint32_t color_planes) {
                                                     uint32_t p;
    for (p = 0; p < color_planes; ++p) {
        jxl_status_t st = jxl_render_upsample_plane_jpeg(alloc, r, p, r->width, r->height);
        if (st != JXL_OK) {
            return st;
        }
    }
    return JXL_OK;
}

typedef struct {
    jxl_const_subgrid_f32 planes[3];
    float *owned[3];
} jxl_prepared_color_fb;

static void prepared_color_fb_free(jxl_allocator_state *alloc, jxl_prepared_color_fb *prep) {
    size_t ch;
    if (alloc == NULL || prep == NULL) {
        return;
    }
    for (ch = 0; ch < 3; ++ch) {
        jxl_free(alloc, prep->owned[ch]);
        prep->owned[ch] = NULL;
    }
    memset(prep->planes, 0, sizeof(prep->planes));
}

static jxl_modular_region modular_region_downsample_with_shift(jxl_modular_region region,
                                                               jxl_channel_shift shift) {
    uint32_t fx = (uint32_t)jxl_channel_shift_hshift(&shift);
    uint32_t fy = (uint32_t)jxl_channel_shift_vshift(&shift);
    uint32_t w = 0;
    uint32_t h = 0;
    jxl_modular_region result;
    jxl_channel_shift_shift_size(&shift, region.width, region.height, &w, &h);
    result.left = region.left >> (int32_t)fx;
    result.top = region.top >> (int32_t)fy;
    result.width = w;
    result.height = h;
    return result;

}

static jxl_status_t prepare_color_fb_jpeg_upsample(jxl_allocator_state *alloc,
                                                   const jxl_frame_header *fh,
                                                   const jxl_const_subgrid_f32 color_fb[3],
                                                   const jxl_modular_region *color_fb_region,
                                                   const jxl_modular_region *color_padded,
                                                   jxl_prepared_color_fb *out,
                                                   jxl_modular_region *out_write_extent) {
    uint32_t ch;
    jxl_modular_region valid_region;
    uint32_t cs_w;
    uint32_t cs_h;
    jxl_modular_region image_region;
    jxl_modular_region fb_extent;
    if (alloc == NULL || fh == NULL || color_fb == NULL || out == NULL ||
        out_write_extent == NULL || color_padded == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    memset(out, 0, sizeof(*out));
    if (color_fb[0].data == NULL) {
        return JXL_OK;
    }

    cs_w = jxl_frame_header_color_sample_width(fh);
    cs_h = jxl_frame_header_color_sample_height(fh);
    image_region = color_fb_region != NULL
                                        ? *color_fb_region
                                        : jxl_modular_region_with_size(cs_w, cs_h);
    valid_region = *color_padded;
    fb_extent = jxl_modular_region_intersection(valid_region, image_region);
    if (fb_extent.width == 0 || fb_extent.height == 0) {
        return JXL_ERROR_INVALID_INPUT;
    }
    *out_write_extent = fb_extent;

    for (ch = 0; ch < 3u; ++ch) {
        jxl_channel_shift shift;
        jxl_modular_region ds_image;
        jxl_modular_region ds_extent;
        int32_t src_left;
        int32_t src_top;
        size_t owned_count;
        float *owned;
        jxl_const_subgrid_f32 src;
        if (color_fb[ch].data == NULL) {
            continue;
        }
        shift = jxl_channel_shift_from_jpeg_upsampling(fh->jpeg_upsampling, ch);
        ds_image = modular_region_downsample_with_shift(image_region, shift);
        ds_extent = modular_region_downsample_with_shift(fb_extent, shift);

        src_left = ds_extent.left - ds_image.left;
        src_top = ds_extent.top - ds_image.top;
        if (src_left < 0 || src_top < 0) {
            return JXL_ERROR_INVALID_INPUT;
        }
        if ((size_t)src_left + ds_extent.width > color_fb[ch].width ||
            (size_t)src_top + ds_extent.height > color_fb[ch].height) {
            return JXL_ERROR_INVALID_INPUT;
        }

        owned_count = (size_t)fb_extent.width * (size_t)fb_extent.height;
        owned = jxl_alloc(alloc, owned_count * sizeof(float));
        if (owned == NULL) {
            prepared_color_fb_free(alloc, out);
            return JXL_ERROR_OUT_OF_MEMORY;
        }
        out->owned[ch] = owned;

        src =
            jxl_const_subgrid_f32_from_buf(color_fb[ch].data, color_fb[ch].width,
                                           color_fb[ch].height, color_fb[ch].stride);
        if (jxl_channel_shift_hshift(&shift) == 0 && jxl_channel_shift_vshift(&shift) == 0) {
            uint32_t y;
            for (y = 0; y < ds_extent.height; ++y) {
                uint32_t x;
                for (x = 0; x < ds_extent.width; ++x) {
                    owned[(size_t)y * fb_extent.width + x] =
                        jxl_const_subgrid_f32_get(src, (size_t)src_left + x, (size_t)src_top + y);
                }
            }
        } else {
            const float *src_ptr =
                src.data + (size_t)src_top * src.stride + (size_t)src_left;
            jxl_const_subgrid_f32 src_sub =
                jxl_const_subgrid_f32_from_buf(src_ptr, ds_extent.width, ds_extent.height,
                                               src.stride);
            if (!jxl_apply_jpeg_upsampling_single(alloc, src_sub, shift, fb_extent.width,
                                                  fb_extent.height, owned, fb_extent.width)) {
                prepared_color_fb_free(alloc, out);
                return JXL_ERROR_OUT_OF_MEMORY;
            }
        }
        out->planes[ch] =
            jxl_const_subgrid_f32_from_buf(owned, fb_extent.width, fb_extent.height, fb_extent.width);
    }
    return JXL_OK;
}

static jxl_modular_region color_fb_write_extent(const jxl_frame_header *fh,
                                                const jxl_modular_region *color_fb_region,
                                                const jxl_modular_region *color_padded) {
    uint32_t cs_w = jxl_frame_header_color_sample_width(fh);
    uint32_t cs_h = jxl_frame_header_color_sample_height(fh);
    jxl_modular_region image_region = color_fb_region != NULL
                                          ? *color_fb_region
                                          : jxl_modular_region_with_size(cs_w, cs_h);
    return jxl_modular_region_intersection(*color_padded, image_region);
}

static jxl_modular_region color_padded_local_in_fb(const jxl_modular_region *color_padded,
                                                   const jxl_modular_region *fb_region,
                                                   size_t fb_w, size_t fb_h) {
    jxl_modular_region local;
    jxl_modular_region bounds;
    if (color_padded == NULL) {
                jxl_modular_region result = {0};
        return result;

    }
    local = *color_padded;
    if (fb_region != NULL) {
        local.left -= fb_region->left;
        local.top -= fb_region->top;
    }
    bounds =
        jxl_modular_region_with_size((uint32_t)fb_w, (uint32_t)fb_h);
    return jxl_modular_region_intersection(local, bounds);
}

static jxl_status_t restore_color_fb_padded_local(
    jxl_context *ctx, jxl_allocator_state *alloc, const jxl_frame_header *fh,
    const jxl_lf_group *lf_groups, uint32_t num_lf_groups, jxl_subgrid_f32 channels[3],
    const jxl_modular_region *color_padded, const jxl_modular_region *fb_region) {
    size_t ch;
    jxl_subgrid_f32 work[3] = {0};
    jxl_modular_region local;
    float *owned[3] = {NULL, NULL, NULL};
    if (alloc == NULL || fh == NULL || channels == NULL || color_padded == NULL ||
        channels[0].data == NULL) {
        return JXL_OK;
    }

    local = color_padded_local_in_fb(
        color_padded, fb_region, channels[0].width, channels[0].height);
    if (local.width == 0 || local.height == 0) {
        return JXL_OK;
    }

    if (fb_region == NULL) {
        if (!jxl_apply_restoration_filters(ctx, alloc, channels, &fh->restoration, fh, lf_groups,
                                           num_lf_groups, color_padded, NULL)) {
            return JXL_ERROR_OUT_OF_MEMORY;
        }
        return JXL_OK;
    }

    for (ch = 0; ch < 3; ++ch) {
        uint32_t y;
        size_t count;
        jxl_subgrid_f32 src;
        if (channels[ch].data == NULL) {
            size_t i;
            for (i = 0; i < ch; ++i) {
                jxl_free(alloc, owned[i]);
            }
            return JXL_OK;
        }
        count = (size_t)local.width * (size_t)local.height;
        owned[ch] = jxl_alloc(alloc, count * sizeof(float));
        if (owned[ch] == NULL) {
            size_t i;
            for (i = 0; i < ch; ++i) {
                jxl_free(alloc, owned[i]);
            }
            return JXL_ERROR_OUT_OF_MEMORY;
        }
        src = jxl_subgrid_f32_sub(channels[ch], (size_t)local.left,
                                  (size_t)local.top, local.width, local.height);
        for (y = 0; y < local.height; ++y) {
            uint32_t x;
            for (x = 0; x < local.width; ++x) {
                owned[ch][(size_t)y * local.width + x] = jxl_subgrid_f32_get(src, x, y);
            }
        }
        work[ch] = jxl_subgrid_f32_from_buf(owned[ch], local.width, local.height, local.width);
    }

    if (!jxl_apply_restoration_filters(ctx, alloc, work, &fh->restoration, fh, lf_groups,
                                       num_lf_groups, color_padded, NULL)) {
                                           size_t ch;
        for (ch = 0; ch < 3; ++ch) {
            jxl_free(alloc, owned[ch]);
        }
        return JXL_ERROR_OUT_OF_MEMORY;
    }

    for (ch = 0; ch < 3; ++ch) {
        uint32_t y;
        jxl_subgrid_f32 dst = jxl_subgrid_f32_sub(channels[ch], (size_t)local.left,
                                                  (size_t)local.top, local.width, local.height);
        for (y = 0; y < local.height; ++y) {
            uint32_t x;
            for (x = 0; x < local.width; ++x) {
                jxl_subgrid_f32_set(dst, x, y, jxl_subgrid_f32_get(work[ch], x, y));
            }
        }
        jxl_free(alloc, owned[ch]);
    }
    return JXL_OK;
}

static jxl_status_t apply_color_restoration_on_fb_channels(
    jxl_context *ctx, jxl_allocator_state *alloc, const jxl_frame_header *fh,
    const jxl_lf_group *lf_groups, uint32_t num_lf_groups, const jxl_const_subgrid_f32 color_fb[3],
    const jxl_modular_region *fb_region, const jxl_modular_region *color_padded) {
    size_t ch;
    jxl_subgrid_f32 channels[3];
    uint32_t output_color_planes;
    if (!restoration_enabled(ctx, fh) || color_fb == NULL || color_fb[0].data == NULL ||
        color_padded == NULL) {
        return JXL_OK;
    }

    output_color_planes = fh->encoded_color_channels;
    if (output_color_planes == 0u) {
        output_color_planes = 3u;
    }
    if (output_color_planes < 3u) {
        return JXL_OK;
    }

    for (ch = 0; ch < 3; ++ch) {
        if (color_fb[ch].data == NULL) {
            return JXL_OK;
        }
        channels[ch] = jxl_subgrid_f32_from_buf((float *)color_fb[ch].data, color_fb[ch].width,
                                                color_fb[ch].height, color_fb[ch].stride);
    }
    return restore_color_fb_padded_local(ctx, alloc, fh, lf_groups, num_lf_groups, channels,
                                         color_padded, fb_region);
}

static void enc_take_prepared_fb(jxl_vardct_encode_ctx *enc, jxl_prepared_color_fb *prep,
                                 const jxl_modular_region *write_extent) {
    size_t ch;
    if (enc == NULL || prep == NULL || write_extent == NULL) {
        return;
    }
    for (ch = 0; ch < 3; ++ch) {
        enc->prepared_fb_data[ch] = prep->owned[ch];
        prep->owned[ch] = NULL;
    }
    enc->prepared_fb_w = write_extent->width;
    enc->prepared_fb_h = write_extent->height;
    enc->prepared_fb_region = *write_extent;
    enc->use_prepared_fb = enc->prepared_fb_data[0] != NULL;
}

jxl_status_t jxl_render_vardct_apply_color_filters(jxl_context *ctx, jxl_allocator_state *alloc,
                                                   const jxl_parsed_image_header *parsed,
                                                   const jxl_frame_header *fh,
                                                   jxl_vardct_encode_ctx *enc,
                                                   const jxl_modular_region *output_region) {
    jxl_render_padded_regions padded;
    jxl_prepared_color_fb prepared;
    jxl_modular_region write_extent = {0};
    jxl_const_subgrid_f32 color_fb[3];
    int jpeg_on_fb;
    size_t ch;
    jxl_status_t st;
    const jxl_modular_region *color_fb_region;
    const jxl_const_subgrid_f32 *filter_fb;
    const jxl_modular_region *filter_region_ptr;

    if (ctx == NULL || alloc == NULL || parsed == NULL || fh == NULL || enc == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    if (enc->color_fb_filters_applied || enc->fb_xyb[0].data == NULL) {
        return JXL_OK;
    }

    jxl_render_compute_padded_regions(parsed, fh, render_image_region(parsed, output_region),
                                      &padded);
    color_fb_region = enc->crop_sized_buffers ? &enc->fb_region : NULL;
    for (ch = 0; ch < 3; ++ch) {
        color_fb[ch] = jxl_const_subgrid_f32_from_buf(enc->fb_xyb[ch].data, enc->fb_xyb[ch].width,
                                                      enc->fb_xyb[ch].height, enc->fb_xyb[ch].stride);
    }

    memset(&prepared, 0, sizeof(prepared));
    filter_fb = color_fb;
    filter_region_ptr = color_fb_region;
    jpeg_on_fb = 0;

    if (fh->do_ycbcr && color_fb[0].data != NULL) {
        st = prepare_color_fb_jpeg_upsample(alloc, fh, color_fb, color_fb_region,
                                              &padded.color_padded, &prepared, &write_extent);
        if (st != JXL_OK) {
            prepared_color_fb_free(alloc, &prepared);
            return st;
        }
        filter_fb = prepared.planes;
        filter_region_ptr = &write_extent;
        jpeg_on_fb = 1;
    }

    if (restoration_enabled(ctx, fh) && filter_fb[0].data != NULL) {
        if (jpeg_on_fb) {
            st = apply_color_restoration_on_fb_channels(ctx, alloc, fh, enc->lf_groups,
                                                          enc->num_lf_groups, filter_fb,
                                                          filter_region_ptr, &padded.color_padded);
        } else {
            jxl_subgrid_f32 channels[3];
            for (ch = 0; ch < 3; ++ch) {
                if (color_fb[ch].data == NULL) {
                    prepared_color_fb_free(alloc, &prepared);
                    return JXL_OK;
                }
                channels[ch] = jxl_subgrid_f32_from_buf((float *)color_fb[ch].data,
                                                        color_fb[ch].width, color_fb[ch].height,
                                                        color_fb[ch].stride);
            }
            st = restore_color_fb_padded_local(ctx, alloc, fh, enc->lf_groups, enc->num_lf_groups,
                                               channels, &padded.color_padded, color_fb_region);
        }
        if (st != JXL_OK) {
            prepared_color_fb_free(alloc, &prepared);
            return st;
        }
    }

    if (jpeg_on_fb) {
        enc_take_prepared_fb(enc, &prepared, &write_extent);
    }
    enc->color_fb_filters_applied = 1;
    return JXL_OK;
}

static jxl_status_t apply_color_restoration(const jxl_render_pre_features_params *params) {
    size_t ch;
    jxl_render_padded_regions padded_local;
    jxl_subgrid_f32 filter_channels[3];
    uint32_t output_color_planes;
    uint32_t fw;
    uint32_t fh_h;
    const jxl_render_padded_regions *padded;
    if (!restoration_enabled(params->ctx, params->fh)) {
        return JXL_OK;
    }

    if (jxl_render_any_plane_integer(params->r)) {
        jxl_status_t st =
            jxl_render_ensure_all_planes_f32(params->alloc, params->r, params->parsed);
        if (st != JXL_OK) {
            return st;
        }
    }

    output_color_planes = params->fh->encoded_color_channels;
    if (output_color_planes == 0u) {
        output_color_planes = params->r->color_planes < 3u ? params->r->color_planes : 3u;
    }
    if (output_color_planes < 3u) {
        jxl_status_t st;
	if (params->r->planes[0] == NULL) {
            return JXL_OK;
        }
        st = jxl_render_clone_gray(params->alloc, params->r);
        if (st != JXL_OK) {
            return st;
        }
    } else if (params->r->planes[0] == NULL || params->r->planes[1] == NULL ||
               params->r->planes[2] == NULL) {
        return JXL_OK;
    }

    fw = color_filter_width(params->fh, params->r);
    fh_h = color_filter_height(params->fh, params->r);
    if (fw == 0 || fh_h == 0) {
        return JXL_ERROR_INVALID_INPUT;
    }

    padded = params->padded_regions;
    if (padded == NULL) {
        jxl_render_compute_padded_regions(params->parsed, params->fh,
                                          render_image_region(params->parsed, params->output_region),
                                          &padded_local);
        padded = &padded_local;
    }

    for (ch = 0; ch < 3; ++ch) {
        filter_channels[ch] =
            jxl_subgrid_f32_from_buf(params->r->planes[ch], fw, fh_h, params->r->width);
    }
    if (!jxl_apply_restoration_filters(params->ctx, params->alloc, filter_channels,
                                       &params->fh->restoration,
                                       params->fh, params->lf_groups, params->num_lf_groups,
                                       &padded->color_padded, params->output_region)) {
        return JXL_ERROR_OUT_OF_MEMORY;
    }
    for (ch = 0; ch < 3; ++ch) {
        params->r->planes[ch] = filter_channels[ch].data;
    }

    if (output_color_planes < 3u) {
        return jxl_render_remove_color_planes(params->alloc, params->r, output_color_planes);
    }
    return JXL_OK;
}

jxl_status_t jxl_render_pre_features_stage(const jxl_render_pre_features_params *params) {
    jxl_render_padded_regions padded_local;
    int restored_on_fb;
    int jpeg_on_fb;
    jxl_prepared_color_fb prepared = {0};
    jxl_modular_region write_extent = {0};
    const jxl_render_padded_regions *padded;
    jxl_status_t st;
    const jxl_const_subgrid_f32 *write_fb;
    const jxl_modular_region *write_origin;
    const jxl_modular_region *write_fb_origin;
    uint32_t color_planes;
    if (params == NULL || params->alloc == NULL || params->parsed == NULL || params->fh == NULL ||
        params->r == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }

    padded = params->padded_regions;
    if (padded == NULL) {
        jxl_render_compute_padded_regions(params->parsed, params->fh,
                                          render_image_region(params->parsed, params->output_region),
                                          &padded_local);
        padded = &padded_local;
    }

    st = JXL_OK;
    restored_on_fb = 0;
    jpeg_on_fb = 0;
    write_fb = params->color_fb;
    write_origin = params->color_fb_region;
    if (params->color_fb[0].data != NULL) {
        write_extent =
            color_fb_write_extent(params->fh, params->color_fb_region, &padded->color_padded);
        write_origin = &write_extent;
    }

    if (params->skip_fb_filters && params->color_fb[0].data != NULL) {
        if (params->fh->do_ycbcr) {
            jpeg_on_fb = 1;
            if (params->color_fb_region != NULL) {
                write_origin = params->color_fb_region;
            }
        }
        if (restoration_enabled(params->ctx, params->fh)) {
            restored_on_fb = 1;
        }
    }

    if (params->color_fb[0].data != NULL && !params->skip_fb_filters && params->fh->do_ycbcr) {
        st = prepare_color_fb_jpeg_upsample(params->alloc, params->fh, params->color_fb,
                                            params->color_fb_region, &padded->color_padded,
                                            &prepared, &write_extent);
        if (st != JXL_OK) {
            prepared_color_fb_free(params->alloc, &prepared);
            return st;
        }
        write_fb = prepared.planes;
        write_origin = &write_extent;
        jpeg_on_fb = 1;
    }

    if (params->color_fb[0].data != NULL && !params->skip_fb_filters &&
        restoration_enabled(params->ctx, params->fh)) {
        if (jpeg_on_fb) {
            st = apply_color_restoration_on_fb_channels(
                params->ctx, params->alloc, params->fh, params->lf_groups, params->num_lf_groups,
                write_fb, write_origin, &padded->color_padded);
            if (st != JXL_OK) {
                prepared_color_fb_free(params->alloc, &prepared);
                return st;
            }
        } else {
            size_t ch;
            jxl_subgrid_f32 channels[3];
            for (ch = 0; ch < 3; ++ch) {
                if (params->color_fb[ch].data == NULL) {
                    prepared_color_fb_free(params->alloc, &prepared);
                    return JXL_OK;
                }
                channels[ch] = jxl_subgrid_f32_from_buf((float *)params->color_fb[ch].data,
                                                        params->color_fb[ch].width,
                                                        params->color_fb[ch].height,
                                                        params->color_fb[ch].stride);
            }
            st = restore_color_fb_padded_local(params->ctx, params->alloc, params->fh,
                                               params->lf_groups, params->num_lf_groups, channels,
                                               &padded->color_padded, params->color_fb_region);
            if (st != JXL_OK) {
                prepared_color_fb_free(params->alloc, &prepared);
                return st;
            }
        }
        restored_on_fb = 1;
    }

    write_fb_origin =
        jpeg_on_fb ? write_origin : params->color_fb_region;
    st = jxl_render_write_color_planes_from_fb(write_fb, params->fh, params->output_region,
                                               params->r, write_fb_origin, jpeg_on_fb);
    prepared_color_fb_free(params->alloc, &prepared);
    if (st != JXL_OK) {
        return st;
    }

    color_planes = params->r->num_planes < 3u ? params->r->num_planes : 3u;
    if (params->color_fb_region != NULL && !jpeg_on_fb) {
        st = upsample_written_crop_planes(params->alloc, params->r, color_planes);
        if (st != JXL_OK) {
            return st;
        }
    }
    if (params->fh->do_ycbcr && color_planes >= 3u && !jpeg_on_fb) {
        st = jpeg_upsample_color_planes(params->alloc, params->r, color_planes, params->fh);
        if (st != JXL_OK) {
            return st;
        }
    }

    if (!restored_on_fb) {
        st = apply_color_restoration(params);
        if (st != JXL_OK) {
            return st;
        }
    }

    return JXL_OK;
}

static int blit_modular_channel_to_plane(jxl_allocator_state *alloc,
                                         const jxl_modular_grid_i32 *grid,
                                         const jxl_modular_channel_info *info, uint32_t bit_depth_bits,
                                         uint32_t dst_stride, float *dst, uint32_t *out_gw,
                                         uint32_t *out_gh) {
    (void)alloc;
    return jxl_modular_blit_channel_to_plane(grid, info, bit_depth_bits, dst_stride, dst, out_gw,
                                             out_gh);
}

static jxl_status_t extend_from_gmodular(jxl_allocator_state *alloc,
                                       jxl_modular_image_destination *dest,
                                       const jxl_parsed_image_header *parsed,
                                       const jxl_frame_header *fh, struct jxl_render *r,
                                       const jxl_modular_region *extend_region,
                                       const jxl_modular_region *output_region) {
    size_t i;
    uint32_t color_planes;
    size_t nb_meta;
    (void)fh;
    /* EC planes blitted via jxl_modular_blit_channel_to_plane (typed grid reads). */
    if (alloc == NULL || dest == NULL || r == NULL || r->planes == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    color_planes = r->num_planes < 3u ? r->num_planes : 3u;
    nb_meta = dest->channels.nb_meta_channels;
    for (i = nb_meta; i < dest->image_channels_len; ++i) {
        size_t ec_idx = i - nb_meta;
        uint32_t plane = color_planes + (uint32_t)ec_idx;
        jxl_modular_region blit_region;
        uint32_t dst_x0;
        uint32_t dst_y0;
        uint32_t gw;
        uint32_t gh;
        int blit_ok;
        uint32_t meta_gw;
        uint32_t meta_gh;
        jxl_modular_region meta_region;
        size_t info_idx;
        uint32_t ec_bit_depth;
        const jxl_modular_channel_info *info;
        uint32_t ow;
        uint32_t oh;
        jxl_modular_region ec_region;
        jxl_channel_shift shift;
	if (plane >= r->num_planes || r->planes[plane] == NULL) {
            break;
        }
        /* Rust: image_channels[i].zip(channels.info); skip meta slots when blitting EC planes. */
        info_idx = i;
        if (info_idx >= dest->channels.info_len) {
            return JXL_ERROR_INVALID_INPUT;
        }
        ec_bit_depth = parsed != NULL ? jxl_parsed_ec_bit_depth(parsed, (uint32_t)ec_idx)
                                               : dest->bit_depth;
        info = &dest->channels.info[info_idx];
        ow = info->original_width != 0 ? info->original_width : info->width;
        oh = info->original_height != 0 ? info->original_height : info->height;
        /* Rust image.rs extend_from_gmodular: Region::with_size(original_width, original_height). */
        ec_region = jxl_modular_region_with_size(ow, oh);
        blit_region = ec_region;
        if (extend_region != NULL) {
            blit_region = jxl_modular_region_intersection(ec_region, *extend_region);
        }
        if (output_region != NULL) {
            blit_region = jxl_modular_region_intersection(blit_region, *output_region);
        }
        if (blit_region.width == 0 || blit_region.height == 0) {
            continue;
        }
        dst_x0 = 0;
        dst_y0 = 0;
        if (output_region != NULL) {
            if (r->width == output_region->width && r->height == output_region->height) {
                dst_x0 = (uint32_t)(blit_region.left - output_region->left);
                dst_y0 = (uint32_t)(blit_region.top - output_region->top);
            } else {
                dst_x0 = (uint32_t)blit_region.left;
                dst_y0 = (uint32_t)blit_region.top;
            }
        }
        gw = 0;
        gh = 0;
        blit_ok = 0;
        if (output_region != NULL) {
            blit_ok = jxl_modular_blit_channel_region_to_plane(
                &dest->image_channels[i], info, ec_bit_depth, blit_region, r->width,
                r->planes[plane], dst_x0, dst_y0);
        } else {
            blit_ok = blit_modular_channel_to_plane(alloc, &dest->image_channels[i], info,
                                                    ec_bit_depth, r->width, r->planes[plane], &gw,
                                                    &gh);
        }
        if (!blit_ok) {
            return JXL_ERROR_INVALID_INPUT;
        }
        shift = info->original_shift;
        meta_gw = 0;
        meta_gh = 0;
        jxl_channel_shift_shift_size(&shift, ow, oh, &meta_gw, &meta_gh);
        meta_region = ec_region;
        if (output_region != NULL && r->width == output_region->width &&
            r->height == output_region->height) {
            meta_region = blit_region;
            meta_region.left -= output_region->left;
            meta_region.top -= output_region->top;
            jxl_channel_shift_shift_size(&shift, blit_region.width, blit_region.height, &meta_gw,
                                         &meta_gh);
        } else if (gw != 0 && gh != 0) {
            meta_gw = gw;
            meta_gh = gh;
        }
        jxl_render_set_plane_meta(r, plane, &meta_region, &info->original_shift, meta_gw, meta_gh);
    }
    return JXL_OK;
}

static jxl_status_t extend_gmodular_extra_channels(const jxl_render_post_encode_params *params) {
    jxl_render_padded_regions padded;
    if (!params->extend_gmodular || params->lf_global == NULL ||
        !params->lf_global->gmodular_used || params->mod_params == NULL || !params->has_mod_params) {
        return JXL_OK;
    }

    uint32_t color_planes =
        params->r->num_planes < 3u ? params->r->num_planes : 3u;
    jxl_modular_image_destination *dest =
        (jxl_modular_image_destination *)&params->lf_global->gmodular;
    jxl_modular_status_t mst;
    const jxl_modular_region *extend_ptr;
    if (params->r->num_planes <= color_planes || dest->image_channels_len == 0) {
        return JXL_OK;
    }

    mst = jxl_modular_gmodular_finish(params->ctx, params->alloc,
        dest, params->gmodular_cs_w, params->gmodular_cs_h, params->parsed->bit_depth_bits,
        params->mod_params);
    if (mst != JXL_MODULAR_OK) {
        return JXL_ERROR_INVALID_INPUT;
    }
    jxl_render_compute_padded_regions(params->parsed, params->fh,
                                      render_image_region(params->parsed, params->output_region),
                                      &padded);
    extend_ptr = params->gmodular_extend_region;
    if (extend_ptr == NULL) {
        extend_ptr = &padded.upsampling_valid;
    }
    return extend_from_gmodular(params->alloc, dest, params->parsed, params->fh, params->r,
                                extend_ptr, params->output_region);
}

jxl_status_t jxl_render_post_encode_stage(const jxl_render_post_encode_params *params) {
    jxl_render_padded_regions padded;
    jxl_render_pre_features_params pre = {0};
    uint32_t visible_frames;
    jxl_status_t st;
    if (params == NULL || params->alloc == NULL || params->parsed == NULL || params->fh == NULL ||
        params->r == NULL || params->lf_global == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }

    jxl_render_compute_padded_regions(params->parsed, params->fh,
                                      render_image_region(params->parsed, params->output_region),
                                      &padded);

    pre.ctx = params->ctx;
    pre.alloc = params->alloc;
    pre.parsed = params->parsed;
    pre.fh = params->fh;
    pre.r = params->r;
    pre.output_region = params->output_region;
    memcpy((void *)pre.color_fb, params->color_fb, sizeof(pre.color_fb));
    pre.lf_groups = params->lf_groups;
    pre.num_lf_groups = params->num_lf_groups;
    pre.ref_image_output = params->ref_image_output;
    pre.padded_regions = &padded;
    pre.color_fb_region = params->color_fb_region;
    pre.skip_fb_filters = params->skip_fb_filters;

    st = jxl_render_pre_features_stage(&pre);
    if (st != JXL_OK) {
        return st;
    }

    st = extend_gmodular_extra_channels(params);
    if (st != JXL_OK) {
        return st;
    }

    visible_frames = params->visible_frames != 0 ? params->visible_frames : 1u;
    st = jxl_render_keyframe_features(params->ctx, params->alloc, params->r, params->parsed,
                                      params->fh, params->lf_global, params->output_region,
                                      params->refs, &padded, visible_frames, params->invisible_frames);
    return st;
}

jxl_status_t jxl_render_keyframe_features(jxl_context *ctx, jxl_allocator_state *alloc,
                                          jxl_render *r, const jxl_parsed_image_header *parsed,
                                          const jxl_frame_header *fh,
                                          const jxl_lf_global *lf_global,
                                          const jxl_modular_region *output_region,
                                          const jxl_reference_store *refs,
                                          const jxl_render_padded_regions *padded_regions,
                                          uint32_t visible_frames, uint32_t invisible_frames) {
    jxl_render_padded_regions padded_local;
    uint32_t vis;
    const jxl_render_padded_regions *padded;
    jxl_modular_region upsampling_valid;
    if (alloc == NULL || r == NULL || parsed == NULL || fh == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }

    padded = padded_regions;
    if (padded == NULL) {
        jxl_render_compute_padded_regions(parsed, fh, render_image_region(parsed, output_region),
                                          &padded_local);
        padded = &padded_local;
    }

    upsampling_valid = padded->upsampling_valid;
    if (output_region != NULL && r->width == output_region->width &&
        r->height == output_region->height) {
        upsampling_valid.left -= output_region->left;
        upsampling_valid.top -= output_region->top;
    }

    vis = visible_frames != 0 ? visible_frames : 1u;
    return jxl_render_features_pipeline(ctx, alloc, r, parsed, fh, &parsed->upsampling_weights,
                                        &upsampling_valid, lf_global, refs, vis,
                                        invisible_frames, output_region);
}

jxl_status_t jxl_render_convert_color_for_record(jxl_context *ctx, jxl_allocator_state *alloc,
                                                 const jxl_parsed_image_header *parsed,
                                                 const jxl_frame_header *fh, jxl_render *r,
                                                 int ref_image_output) {
    uint32_t color_planes;
    uint32_t encoded_color;
    size_t pixels;
    if (alloc == NULL || parsed == NULL || fh == NULL || r == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    if (ref_image_output || fh->save_before_ct) {
        return JXL_OK;
    }

    if (fh->encoded_color_channels > 0u &&
        (fh->encoded_color_channels + parsed->num_extra_channels < r->num_planes ||
         fh->encoded_color_channels < r->color_planes)) {
        jxl_status_t shrink_st =
            jxl_render_shrink_to_encoded_layout(alloc, r, fh->encoded_color_channels,
                                                parsed->num_extra_channels);
        if (shrink_st != JXL_OK) {
            return shrink_st;
        }
    }

    encoded_color = fh->encoded_color_channels;
    if (encoded_color == 0u) {
        encoded_color = r->color_planes < 3u ? r->color_planes : 3u;
    }
    if (encoded_color < 3u && parsed->xyb_encoded) {
        jxl_status_t st = jxl_render_clone_gray(alloc, r);
        if (st != JXL_OK) {
            return st;
        }
    }

    color_planes = r->color_planes < 3u ? r->color_planes : 3u;
    if (color_planes < 3u || r->planes[0] == NULL || r->planes[1] == NULL ||
        r->planes[2] == NULL) {
        return JXL_OK;
    }

    pixels = (size_t)r->width * (size_t)r->height;
    if (fh->do_ycbcr) {
        jxl_status_t st;
        if (jxl_render_any_plane_integer(r)) {
            st = jxl_render_convert_modular_color(alloc, r, parsed->bit_depth_bits, r->color_planes);
            if (st != JXL_OK) {
                return st;
            }
        }
        jxl_ycbcr_to_rgb(ctx, r->planes[0], r->planes[1], r->planes[2], pixels);
        if (encoded_color < 3u) {
            st = jxl_render_remove_color_planes(alloc, r, encoded_color);
            if (st == JXL_OK) {
                r->ct_done = 1;
            }
            return st;
        }
        r->ct_done = 1;
        return JXL_OK;
    }

    if (!parsed->xyb_encoded || parsed->colour.colour_space == JXL_COLOUR_SPACE_XYB_I ||
        parsed->colour.colour_space == JXL_COLOUR_SPACE_UNKNOWN_I) {
        if (encoded_color < 3u) {
            return jxl_render_remove_color_planes(alloc, r, encoded_color);
        }
        return JXL_OK;
    }

    if (parsed->colour.have_icc_profile) {
        if (encoded_color < 3u) {
            return jxl_render_remove_color_planes(alloc, r, encoded_color);
        }
        return JXL_OK;
    }

    if (!jxl_colour_encoding_is_d65_srgb_fast_path(&parsed->colour)) {
        if (encoded_color < 3u) {
            return jxl_render_remove_color_planes(alloc, r, encoded_color);
        }
        return JXL_OK;
    }

    {
        jxl_status_t st;
        if (jxl_render_any_plane_integer(r)) {
            st = jxl_render_convert_modular_color(alloc, r, parsed->bit_depth_bits, r->color_planes);
            if (st != JXL_OK) {
                return st;
            }
        }
        st = jxl_color_transform_xyb_to_encoding(ctx, r->planes[0], r->planes[1], r->planes[2], pixels,
                                                 &parsed->opsin_inverse, &parsed->colour, 255.0f);
        if (st != JXL_OK) {
            return st;
        }
    }
    r->ct_done = 1;
    if (encoded_color < 3u || parsed->colour.colour_space == JXL_COLOUR_SPACE_GRAY_I) {
        uint32_t keep_color = encoded_color;
        return jxl_render_remove_color_planes(alloc, r, keep_color);
    }
    return JXL_OK;
}

jxl_status_t jxl_render_post_encode_from_modular_result(
    jxl_context *ctx, jxl_allocator_state *alloc, const jxl_parsed_image_header *parsed,
    jxl_modular_encode_result *enc, const jxl_modular_region *output_region,
    jxl_reference_store *refs, jxl_render *r) {
    jxl_const_subgrid_f32 empty_fb[3] = {{0}};
    jxl_lf_global lf_global;
    jxl_render_post_encode_params post = {0};
    jxl_status_t st;
    if (alloc == NULL || parsed == NULL || enc == NULL || r == NULL || !enc->valid) {
        return JXL_ERROR_INVALID_INPUT;
    }

    jxl_lf_global_init(&lf_global);
    if (enc->has_patches) {
        lf_global.has_patches = 1;
        lf_global.patches = enc->patches;
        jxl_patches_init(&enc->patches);
        enc->has_patches = 0;
    }
    if (enc->has_noise) {
        lf_global.has_noise = 1;
        lf_global.noise = enc->noise;
    }
    if (enc->has_splines) {
        lf_global.has_splines = 1;
        lf_global.splines = enc->splines;
        jxl_splines_init(&enc->splines);
        enc->has_splines = 0;
    }

    post.ctx = ctx;
    post.alloc = alloc;
    post.parsed = parsed;
    post.fh = &enc->fh;
    post.r = r;
    post.output_region = output_region;
    post.refs = refs;
    memcpy((void *)post.color_fb, empty_fb, sizeof(post.color_fb));
    post.lf_groups = NULL;
    post.num_lf_groups = 0;
    post.ref_image_output = 0;
    post.lf_global = &lf_global;
    post.extend_gmodular = 0;
    post.visible_frames = enc->visible_frames != 0 ? enc->visible_frames : 1u;
    post.invisible_frames = enc->invisible_frames;

    st = jxl_render_post_encode_stage(&post);
    if (lf_global.has_patches) {
        jxl_patches_free(alloc, &lf_global.patches);
    }
    if (lf_global.has_splines) {
        jxl_splines_free(alloc, &lf_global.splines);
    }
    jxl_lf_global_free(alloc, &lf_global);
    return st;
}

jxl_status_t jxl_render_post_encode_from_vardct_ctx(
    jxl_context *ctx, jxl_allocator_state *alloc, const jxl_parsed_image_header *parsed,
    const jxl_frame_header *fh, const jxl_vardct_encode_ctx *enc,
    const jxl_modular_region *output_region, const jxl_modular_region *gmodular_extend_region,
    const jxl_reference_store *refs, int ref_image_output, uint32_t visible_frames,
    uint32_t invisible_frames, jxl_render *r) {
    size_t ch;
    jxl_const_subgrid_f32 color_fb[3];
    jxl_render_post_encode_params post = {0};
    const jxl_modular_region *color_fb_region;
    if (alloc == NULL || parsed == NULL || fh == NULL || enc == NULL || r == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }

    color_fb_region = NULL;
    for (ch = 0; ch < 3; ++ch) {
        if (enc->use_prepared_fb && enc->prepared_fb_data[ch] != NULL) {
            color_fb[ch] = jxl_const_subgrid_f32_from_buf(enc->prepared_fb_data[ch],
                                                          enc->prepared_fb_w, enc->prepared_fb_h,
                                                          enc->prepared_fb_w);
        } else {
            color_fb[ch] = jxl_const_subgrid_f32_from_buf(enc->fb_xyb[ch].data, enc->fb_xyb[ch].width,
                                                          enc->fb_xyb[ch].height, enc->fb_xyb[ch].stride);
        }
    }
    if (enc->use_prepared_fb) {
        color_fb_region = &enc->prepared_fb_region;
    } else if (enc->crop_sized_buffers) {
        color_fb_region = &enc->fb_region;
    }
    post.ctx = ctx;
    post.alloc = alloc;
    post.parsed = parsed;
    post.fh = fh;
    post.r = r;
    post.output_region = output_region;
    post.refs = refs;
    memcpy((void *)post.color_fb, color_fb, sizeof(post.color_fb));
    post.lf_groups = enc->lf_groups;
    post.num_lf_groups = enc->num_lf_groups;
    post.ref_image_output = ref_image_output;
    post.lf_global = &enc->lf_global;
    post.extend_gmodular = 1;
    post.gmodular_cs_w = enc->color_sample_w;
    post.gmodular_cs_h = enc->color_sample_h;
    post.mod_params = &enc->mod_params;
    post.has_mod_params = enc->has_mod_params;
    post.visible_frames = visible_frames != 0 ? visible_frames : 1u;
    post.invisible_frames = invisible_frames;
    post.color_fb_region = color_fb_region;
    post.gmodular_extend_region = gmodular_extend_region;
    post.skip_fb_filters = enc->color_fb_filters_applied;

    return jxl_render_post_encode_stage(&post);
}

jxl_status_t jxl_render_frame(const jxl_render_frame_params *params, jxl_render *r) {
    const jxl_keyframe_render_params *kp;
    int encoding;
    if (params == NULL || params->params == NULL || params->parsed == NULL ||
        params->frame == NULL || r == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }

    kp = params->params;
    encoding = params->frame->header.encoding;

    if (encoding == JXL_FRAME_ENCODING_MODULAR) {
        jxl_modular_encode_result enc;
        jxl_status_t st;
	if (params->modular_bitstream == NULL || params->codestream == NULL) {
            return JXL_ERROR_INVALID_INPUT;
        }
        jxl_modular_encode_result_init(&enc);
        st = jxl_modular_encode_keyframe(kp, params->parsed, params->codestream,
                                                      params->codestream_len,
                                                      params->modular_bitstream, kp->filter_region,
                                                      r, &enc);
        if (st == JXL_OK && enc.valid) {
            st = jxl_render_post_encode_from_modular_result(kp->ctx, kp->alloc, params->parsed, &enc,
                                                            kp->output_region, params->refs, r);
            if (st == JXL_OK) {
                st = jxl_render_convert_color_for_record(kp->ctx, kp->alloc, params->parsed, &enc.fh, r, 0);
            } else {
                jxl_render_set_error((jxl_keyframe_render_params *)kp,
                                     "failed post-encode render stage");
            }
        }
        jxl_modular_encode_result_free(kp->alloc, &enc);
        return st;
    }

    if (encoding == JXL_FRAME_ENCODING_VARDCT) {
        jxl_modular_region filter_scratch;
        jxl_modular_region full_filter_scratch;
        jxl_vardct_encode_ctx enc = {0};
        jxl_vardct_render_params vparams = {0};
        const jxl_modular_region *filter_ptr;
        const jxl_frame_header *fh;
        const jxl_modular_region *vardct_filter;
        const jxl_modular_region *color_out;
        jxl_status_t st;
        if (params->codestream == NULL) {
            return JXL_ERROR_INVALID_INPUT;
        }
        filter_ptr = jxl_render_resolve_color_filter_region(
            params->parsed, &params->frame->header, kp->output_region, kp->filter_region,
            &filter_scratch);
        fh = &params->frame->header;
        vardct_filter = filter_ptr;
        if (kp->output_region != NULL) {
            jxl_modular_region full_image =
                jxl_modular_region_with_size(params->parsed->size.width, params->parsed->size.height);
            vardct_filter = jxl_render_resolve_color_filter_region_for_image(
                params->parsed, fh, full_image, kp->filter_region, &full_filter_scratch);
        }
        vparams.ctx = kp->ctx;
        vparams.alloc = kp->alloc;
        vparams.input = kp->input;
        vparams.input_len = kp->input_len;
        vparams.error_out = kp->error_out;
        vparams.filter_region = vardct_filter;
        vparams.output_region = kp->output_region;
        vparams.external_refs = params->refs;
        vparams.external_lf_store = params->lf_store;
        vparams.ref_image_output = 0;
        vparams.parsed_header = params->parsed;
        vparams.loaded_frame = params->frame;
        vparams.codestream = params->codestream;
        vparams.codestream_len = params->codestream_len;

        jxl_vardct_encode_ctx_init(&enc);
        color_out =
            kp->output_region != NULL ? NULL : kp->output_region;
        st = jxl_vardct_encode_frame(&vparams, params->frame, params->parsed,
                                                  params->lf_store, vardct_filter, &enc);
        if (st == JXL_OK) {
            st = jxl_render_vardct_apply_color_filters(kp->ctx, kp->alloc, params->parsed, fh, &enc,
                                                       color_out);
        }
        if (st == JXL_OK) {
            uint32_t lw = fh->width;
            uint32_t lh = fh->height;
            uint32_t compose_color = fh->encoded_color_channels;
            uint32_t local_color_planes;
            uint32_t compose_planes;
            jxl_render *local;
            if (compose_color == 0u) {
                compose_color = kp->num_color_channels;
            }
            compose_planes = compose_color + (uint32_t)params->parsed->num_extra_channels;
            if (compose_planes == 0u) {
                compose_planes = r->num_planes;
            }
            local_color_planes = compose_color < 3u ? compose_color : 3u;

            local =
                jxl_render_create(kp->alloc, compose_planes, local_color_planes, lw, lh);
            if (local == NULL) {
                st = JXL_ERROR_OUT_OF_MEMORY;
            } else {
                jxl_modular_region frame_region = jxl_modular_region_with_size(lw, lh);
                uint32_t vis;
                jxl_render_init_all_planes(local, &frame_region);

                vis = params->visible_frames != 0 ? params->visible_frames : 1u;
                st = jxl_render_post_encode_from_vardct_ctx(
                    kp->ctx, kp->alloc, params->parsed, fh, &enc, color_out, NULL, params->refs, 1,
                    vis, params->invisible_frames, local);
                /* Rust postprocess_keyframe: last keyframes are not referenceable, so composite
                 * preprocess skips convert_color_for_record; convert here before blit. */
                if (st == JXL_OK && !jxl_frame_header_can_reference(fh)) {
                    st = jxl_render_convert_color_for_record(kp->ctx, kp->alloc, params->parsed, fh, local,
                                                            0);
                }
                if (st == JXL_OK) {
                    jxl_modular_compose_params cp = {0};
                    cp.ctx = kp->ctx;
                    cp.alloc = kp->alloc;
                    cp.parsed = params->parsed;
                    cp.fh = fh;
                    cp.bit_depth = params->parsed->bit_depth_bits;
                    cp.num_color_channels = compose_color;
                    cp.num_extra_channels = (uint32_t)params->parsed->num_extra_channels;
                    cp.output_region = kp->output_region;
                    cp.prefer_canvas_base = params->prefer_canvas_base;

                    st = jxl_render_composite_local_frame(&cp, local, params->refs, r);
                }
                jxl_render_free(kp->alloc, local);
            }
            if (st != JXL_OK) {
                jxl_render_set_error((jxl_keyframe_render_params *)kp,
                                     "failed post-encode render stage");
            }
        }
        jxl_vardct_encode_ctx_free(kp->alloc, &enc);
        return st;
    }

    jxl_render_set_error((jxl_keyframe_render_params *)kp, "unsupported frame encoding");
    return JXL_ERROR_UNSUPPORTED;
}
