// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render_buffer.h"

#include "allocator.h"
#include "jxl_oxide/jxl_context.h"
#include "frame/frame_header.h"
#include "image/image_internal.h"
#include "render/features/noise.h"
#include "render/features/spline.h"
#include "render/filter/ycbcr.h"
#include "render/modular_sample.h"
#include "render/subgrid_f32.h"

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

static uint32_t frame_upsampling_log2(uint32_t upsampling) {
    return trailing_zeros_u32(upsampling);
}

static void shift_add_upsampling(jxl_channel_shift *shift, uint32_t factor_log2) {
    if (shift == NULL || factor_log2 == 0) {
        return;
    }
    switch (shift->kind) {
    case JXL_CHANNEL_SHIFT_RAW:
        if (shift->raw_h >= 0) {
            shift->raw_h += (int32_t)factor_log2;
        }
        if (shift->raw_v >= 0) {
            shift->raw_v += (int32_t)factor_log2;
        }
        break;
    case JXL_CHANNEL_SHIFT_SHIFTS:
        shift->shift += factor_log2;
        break;
    case JXL_CHANNEL_SHIFT_JPEG_UPSAMPLING:
        if (!shift->has_h_subsample && !shift->has_v_subsample && !shift->h_subsample &&
            !shift->v_subsample) {
            *shift = jxl_channel_shift_from_shift(factor_log2);
        }
        break;
    }
}

static int plane_needs_upsample(const jxl_render *r, uint32_t plane) {
    const jxl_render_plane_meta *m;
    if (r == NULL || r->meta == NULL || plane >= r->num_planes) {
        return 0;
    }
    m = &r->meta[plane];
    if (m->buf_width != r->width || m->buf_height != r->height) {
        return 1;
    }
    if (jxl_channel_shift_hshift(&m->shift) != 0 || jxl_channel_shift_vshift(&m->shift) != 0) {
        return 1;
    }
    return 0;
}

jxl_render *jxl_render_create(jxl_allocator_state *alloc, uint32_t num_planes,
                              uint32_t color_planes, uint32_t width, uint32_t height) {
    uint32_t p;
    jxl_render *r;
    uint64_t pixels;
    size_t total_samples;
    size_t plane_stride;
    if (alloc == NULL || num_planes == 0 || width == 0 || height == 0) {
        return NULL;
    }
    r = jxl_calloc(alloc, 1, sizeof(*r));
    if (r == NULL) {
        return NULL;
    }
    r->width = width;
    r->height = height;
    r->num_planes = num_planes;
    r->color_planes = color_planes > num_planes ? num_planes : color_planes;

    r->planes = jxl_calloc(alloc, num_planes, sizeof(*r->planes));
    r->bufs = jxl_calloc(alloc, num_planes, sizeof(*r->bufs));
    r->meta = jxl_calloc(alloc, num_planes, sizeof(*r->meta));
    if (r->planes == NULL || r->bufs == NULL || r->meta == NULL) {
        jxl_render_free(alloc, r);
        return NULL;
    }

    pixels = (uint64_t)width * height;
    if (pixels > SIZE_MAX / sizeof(float) ||
        pixels > (SIZE_MAX / sizeof(float)) / (uint64_t)num_planes) {
        jxl_render_free(alloc, r);
        return NULL;
    }
    total_samples = (size_t)pixels * num_planes;
    r->samples = jxl_calloc(alloc, total_samples, sizeof(*r->samples));
    if (r->samples == NULL) {
        jxl_render_free(alloc, r);
        return NULL;
    }
    plane_stride = (size_t)pixels;
    for (p = 0; p < num_planes; ++p) {
        r->planes[p] = r->samples + p * plane_stride;
        jxl_image_buffer_bind_f32(alloc, &r->bufs[p], r->planes[p]);
    }
    return r;
}

void jxl_render_free(jxl_allocator_state *alloc, jxl_render *r) {
    if (r == NULL) {
        return;
    }
    if (alloc != NULL) {
        if (r->bufs != NULL) {
            uint32_t p;
            for (p = 0; p < r->num_planes; ++p) {
                jxl_image_buffer_destroy(alloc, &r->bufs[p]);
            }
        }
        jxl_free(alloc, r->samples);
        jxl_free(alloc, r->bufs);
        jxl_free(alloc, r->planes);
        jxl_free(alloc, r->meta);
        jxl_free(alloc, r);
    }
}

void jxl_render_destroy(jxl_context *ctx, jxl_render *r) {
    jxl_render_free(jxl_context_alloc_state(ctx), r);
}

void jxl_render_init_all_planes(jxl_render *r, const jxl_modular_region *frame_region) {
    uint32_t p;
    jxl_channel_shift zero;
    if (r == NULL || r->meta == NULL || frame_region == NULL) {
        return;
    }
    zero = jxl_channel_shift_from_shift(0);
    for (p = 0; p < r->num_planes; ++p) {
        r->meta[p].region = *frame_region;
        r->meta[p].shift = zero;
        r->meta[p].buf_width = r->width;
        r->meta[p].buf_height = r->height;
    }
}

static jxl_status_t render_materialize_all_integer_planes(jxl_allocator_state *alloc,
                                                        jxl_render *r) {
    uint32_t p;
    if (alloc == NULL || r == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    for (p = 0; p < r->num_planes; ++p) {
        if (jxl_render_plane_is_integer(r, p)) {
            jxl_status_t st = jxl_render_materialize_plane_f32(alloc, r, p);
            if (st != JXL_OK) {
                return st;
            }
        }
    }
    return JXL_OK;
}

static jxl_status_t render_rebind_plane_bufs_f32(jxl_allocator_state *alloc, jxl_render *r) {
    uint32_t p;
    if (alloc == NULL || r == NULL || r->planes == NULL || r->bufs == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    for (p = 0; p < r->num_planes; ++p) {
        if (r->planes[p] == NULL) {
            return JXL_ERROR_INVALID_INPUT;
        }
        jxl_image_buffer_bind_f32(alloc, &r->bufs[p], r->planes[p]);
    }
    return JXL_OK;
}

static jxl_status_t render_resize_bufs(jxl_allocator_state *alloc, jxl_render *r,
                                       uint32_t new_num_planes) {
    uint32_t old_num_planes;
    uint32_t p;
    jxl_image_buffer *new_bufs;

    if (alloc == NULL || r == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    if (new_num_planes == 0) {
        if (r->bufs != NULL) {
            for (p = 0; p < r->num_planes; ++p) {
                jxl_image_buffer_destroy(alloc, &r->bufs[p]);
            }
            jxl_free(alloc, r->bufs);
            r->bufs = NULL;
        }
        return JXL_OK;
    }

    old_num_planes = r->num_planes;
    if (r->bufs != NULL && new_num_planes < old_num_planes) {
        for (p = new_num_planes; p < old_num_planes; ++p) {
            jxl_image_buffer_destroy(alloc, &r->bufs[p]);
        }
    }
    new_bufs =
        jxl_realloc(alloc, r->bufs, (size_t)new_num_planes * sizeof(*new_bufs));
    if (new_bufs == NULL) {
        return JXL_ERROR_OUT_OF_MEMORY;
    }
    if (new_num_planes > old_num_planes) {
        for (p = old_num_planes; p < new_num_planes; ++p) {
            jxl_image_buffer_init_empty(&new_bufs[p]);
        }
    }
    r->bufs = new_bufs;
    return JXL_OK;
}

jxl_status_t jxl_render_clone_gray(jxl_allocator_state *alloc, jxl_render *r) {
    uint32_t p;
    uint32_t new_num_planes;
    uint32_t old_num_planes;
    size_t plane_pixels;
    size_t new_samples_count;
    float *new_samples;
    float **new_planes;
    jxl_render_plane_meta *new_meta;
    jxl_status_t buf_st;
    if (alloc == NULL || r == NULL || r->color_planes != 1u || r->planes == NULL ||
        r->meta == NULL ||
        r->planes[0] == NULL || r->width == 0 || r->height == 0) {
        return JXL_ERROR_INVALID_INPUT;
    }

    if (jxl_render_plane_is_integer(r, 0)) {
        jxl_status_t st =
            jxl_render_materialize_plane_f32(alloc, r, 0);
        if (st != JXL_OK) {
            return st;
        }
    }

    old_num_planes = r->num_planes;
    new_num_planes = old_num_planes + 2u;
    plane_pixels = (size_t)r->width * (size_t)r->height;
    if (new_num_planes > UINT32_MAX / plane_pixels ||
        plane_pixels > SIZE_MAX / sizeof(float) ||
        (uint64_t)plane_pixels * new_num_planes > SIZE_MAX / sizeof(float)) {
        return JXL_ERROR_OUT_OF_MEMORY;
    }
    new_samples_count = plane_pixels * (size_t)new_num_planes;

    new_samples =
        jxl_realloc(alloc, r->samples, new_samples_count * sizeof(float));
    if (new_samples == NULL) {
        return JXL_ERROR_OUT_OF_MEMORY;
    }
    r->samples = new_samples;

    new_planes =
        jxl_realloc(alloc, r->planes, (size_t)new_num_planes * sizeof(*new_planes));
    new_meta =
        jxl_realloc(alloc, r->meta, (size_t)new_num_planes * sizeof(*new_meta));
    buf_st = render_resize_bufs(alloc, r, new_num_planes);
    if (new_planes == NULL || new_meta == NULL || buf_st != JXL_OK) {
        jxl_free(alloc, new_planes);
        return JXL_ERROR_OUT_OF_MEMORY;
    }
    r->planes = new_planes;
    r->meta = new_meta;

    if (old_num_planes > 1u) {
        uint32_t p;
        for (p = old_num_planes - 1u; p >= 1u; --p) {
            memmove(r->samples + (size_t)(p + 2u) * plane_pixels,
                    r->samples + (size_t)p * plane_pixels, plane_pixels * sizeof(float));
            r->meta[p + 2u] = r->meta[p];
        }
    }

    memcpy(r->samples + plane_pixels, r->samples, plane_pixels * sizeof(float));
    memcpy(r->samples + 2u * plane_pixels, r->samples, plane_pixels * sizeof(float));
    r->meta[1] = r->meta[0];
    r->meta[2] = r->meta[0];

    r->num_planes = new_num_planes;
    for (p = 0; p < new_num_planes; ++p) {
        r->planes[p] = r->samples + (size_t)p * plane_pixels;
    }
    buf_st = render_rebind_plane_bufs_f32(alloc, r);
    if (buf_st != JXL_OK) {
        return buf_st;
    }

    r->color_planes = 3u;
    return JXL_OK;
}

jxl_status_t jxl_render_shrink_to_encoded_layout(jxl_allocator_state *alloc, jxl_render *r,
                                               uint32_t encoded_color, uint32_t extra_planes) {
                                                   uint32_t p;
    uint32_t want_total;
    size_t plane_pixels;
    float *new_samples;
    float **new_planes;
    jxl_render_plane_meta *new_meta;
    jxl_status_t buf_st;
    if (alloc == NULL || r == NULL || r->planes == NULL || r->meta == NULL ||
        encoded_color == 0u) {
        return JXL_ERROR_INVALID_INPUT;
    }
    want_total = encoded_color + extra_planes;
    if (want_total == 0u || r->num_planes < want_total) {
        return JXL_ERROR_INVALID_INPUT;
    }
    if (r->num_planes == want_total) {
        r->color_planes = encoded_color < 3u ? encoded_color : 3u;
        return JXL_OK;
    }

    {
        jxl_status_t st = render_materialize_all_integer_planes(alloc, r);
        if (st != JXL_OK) {
            return st;
        }
    }

    plane_pixels = (size_t)r->width * (size_t)r->height;
    if (plane_pixels == 0) {
        return JXL_ERROR_INVALID_INPUT;
    }

    new_samples =
        jxl_realloc(alloc, r->samples, plane_pixels * (size_t)want_total * sizeof(float));
    new_planes =
        jxl_realloc(alloc, r->planes, (size_t)want_total * sizeof(*new_planes));
    new_meta =
        jxl_realloc(alloc, r->meta, (size_t)want_total * sizeof(*new_meta));
    buf_st = render_resize_bufs(alloc, r, want_total);
    if (new_samples == NULL || new_planes == NULL || new_meta == NULL || buf_st != JXL_OK) {
        jxl_free(alloc, new_planes);
        jxl_free(alloc, new_meta);
        return JXL_ERROR_OUT_OF_MEMORY;
    }
    r->samples = new_samples;
    r->planes = new_planes;
    r->meta = new_meta;
    r->num_planes = want_total;

    for (p = 0; p < want_total; ++p) {
        r->planes[p] = r->samples + (size_t)p * plane_pixels;
    }
    buf_st = render_rebind_plane_bufs_f32(alloc, r);
    if (buf_st != JXL_OK) {
        return buf_st;
    }

    r->color_planes = encoded_color < 3u ? encoded_color : 3u;
    return JXL_OK;
}

jxl_status_t jxl_render_remove_color_planes(jxl_allocator_state *alloc, jxl_render *r,
                                            uint32_t keep_count) {
                                                uint32_t p;
    uint32_t new_num_planes;
    uint32_t remove_count;
    uint32_t old_num_planes;
    size_t plane_pixels;
    float *new_samples;
    float **new_planes;
    jxl_render_plane_meta *new_meta;
    jxl_status_t buf_st;
    if (alloc == NULL || r == NULL || r->planes == NULL || r->meta == NULL || keep_count == 0) {
        return JXL_ERROR_INVALID_INPUT;
    }
    if (keep_count >= r->color_planes) {
        return JXL_OK;
    }

    {
        jxl_status_t st = render_materialize_all_integer_planes(alloc, r);
        if (st != JXL_OK) {
            return st;
        }
    }

    remove_count = r->color_planes - keep_count;
    old_num_planes = r->num_planes;
    if (old_num_planes <= remove_count) {
        return JXL_ERROR_INVALID_INPUT;
    }
    new_num_planes = old_num_planes - remove_count;
    plane_pixels = (size_t)r->width * (size_t)r->height;

    for (p = keep_count + remove_count; p < old_num_planes; ++p) {
        memmove(r->samples + (size_t)(p - remove_count) * plane_pixels,
                r->samples + (size_t)p * plane_pixels, plane_pixels * sizeof(float));
        r->meta[p - remove_count] = r->meta[p];
    }

    new_samples =
        jxl_realloc(alloc, r->samples, plane_pixels * (size_t)new_num_planes * sizeof(float));
    new_planes =
        jxl_realloc(alloc, r->planes, (size_t)new_num_planes * sizeof(*new_planes));
    new_meta =
        jxl_realloc(alloc, r->meta, (size_t)new_num_planes * sizeof(*new_meta));
    buf_st = render_resize_bufs(alloc, r, new_num_planes);
    if ((new_num_planes > 0 &&
         (new_samples == NULL || new_planes == NULL || new_meta == NULL)) ||
        buf_st != JXL_OK) {
        jxl_free(alloc, new_planes);
        jxl_free(alloc, new_meta);
        return JXL_ERROR_OUT_OF_MEMORY;
    }
    r->samples = new_samples;
    r->planes = new_planes;
    r->meta = new_meta;
    r->num_planes = new_num_planes;

    for (p = 0; p < new_num_planes; ++p) {
        r->planes[p] = r->samples + (size_t)p * plane_pixels;
    }
    buf_st = render_rebind_plane_bufs_f32(alloc, r);
    if (buf_st != JXL_OK) {
        return buf_st;
    }

    r->color_planes = keep_count;
    return JXL_OK;
}

void jxl_render_blit_subgrid_to_plane(jxl_const_subgrid_f32 src, float *dst, uint32_t dst_stride) {
    size_t y;
    if (dst == NULL || src.data == NULL) {
        return;
    }
    for (y = 0; y < src.height; ++y) {
        size_t x;
        for (x = 0; x < src.width; ++x) {
            dst[y * (size_t)dst_stride + x] = jxl_const_subgrid_f32_get(src, x, y);
        }
    }
}

static int jpeg_shift_is_full_resolution(const jxl_channel_shift *shift) {
    return shift != NULL && shift->kind == JXL_CHANNEL_SHIFT_JPEG_UPSAMPLING &&
           !shift->has_h_subsample && !shift->has_v_subsample && !shift->h_subsample &&
           !shift->v_subsample;
}

static jxl_channel_shift color_shift_for_blit(const jxl_frame_header *fh, uint32_t plane) {
    jxl_channel_shift shift = jxl_channel_shift_from_jpeg_upsampling(fh->jpeg_upsampling, plane);
    if (fh->upsampling > 1u && jpeg_shift_is_full_resolution(&shift)) {
        return jxl_channel_shift_from_shift(0);
    }
    return shift;
}

static void color_buf_size(uint32_t cs_w, uint32_t cs_h, const jxl_channel_shift *shift,
                           uint32_t *out_w, uint32_t *out_h) {
    jxl_channel_shift_shift_size(shift, cs_w, cs_h, out_w, out_h);
}

jxl_status_t jxl_render_blit_fb_crop_to_plane(jxl_const_subgrid_f32 src, uint32_t src_x0,
                                              uint32_t src_y0, uint32_t blit_w, uint32_t blit_h,
                                              float *dst, uint32_t dst_stride) {
                                                  uint32_t y;
    if (dst == NULL || blit_w == 0 || blit_h == 0) {
        return JXL_OK;
    }
    for (y = 0; y < blit_h; ++y) {
        uint32_t x;
        if (src_y0 + y >= src.height) {
            break;
        }
        for (x = 0; x < blit_w; ++x) {
            if (src_x0 + x >= src.width) {
                break;
            }
            dst[(size_t)y * dst_stride + x] =
                jxl_const_subgrid_f32_get(src, (size_t)src_x0 + x, (size_t)src_y0 + y);
        }
    }
    return JXL_OK;
}

jxl_status_t jxl_render_write_color_planes_from_fb(const jxl_const_subgrid_f32 color_fb[3],
                                                 const jxl_frame_header *fh,
                                                 const jxl_modular_region *output_region,
                                                 jxl_render *r,
                                                 const jxl_modular_region *color_fb_region,
                                                 int source_jpeg_upsampled) {
    uint32_t p;
    uint32_t color_planes;
    uint32_t cs_w;
    uint32_t cs_h;
    int defer_frame_upsample;
    jxl_modular_region frame_bounds;
    jxl_modular_region crop;
    if (color_fb == NULL || fh == NULL || r == NULL || r->planes == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    if (color_fb[0].data == NULL) {
        return JXL_OK;
    }

    color_planes = r->num_planes < 3u ? r->num_planes : 3u;
    cs_w = jxl_frame_header_color_sample_width(fh);
    cs_h = jxl_frame_header_color_sample_height(fh);
    defer_frame_upsample = fh->upsampling > 1u;

    if (output_region == NULL) {
        uint32_t p;
        for (p = 0; p < color_planes; ++p) {
            if (p >= 3u || color_fb[p].data == NULL || r->planes[p] == NULL) {
                continue;
            }
            jxl_status_t st = jxl_render_write_color_plane_from_fb(color_fb[p], cs_w, cs_h, fh, p,
                                                                 r, defer_frame_upsample);
            if (st != JXL_OK) {
                return st;
            }
        }
        return JXL_OK;
    }

    frame_bounds = jxl_modular_region_with_size(fh->width, fh->height);
    crop = jxl_modular_region_intersection(*output_region, frame_bounds);
    if (crop.width == 0 || crop.height == 0) {
        return JXL_OK;
    }
    if (crop.width == r->width && crop.height == r->height &&
        color_fb[0].width == (size_t)r->width && color_fb[0].height == (size_t)r->height) {
        return jxl_render_write_color_planes_from_fb(color_fb, fh, NULL, r, color_fb_region,
                                                     source_jpeg_upsampled);
    }
    if (crop.width != r->width || crop.height != r->height) {
        return JXL_ERROR_INVALID_INPUT;
    }

    for (p = 0; p < color_planes; ++p) {
        jxl_channel_shift shift;
        uint32_t down_h;
        uint32_t down_v;
        int32_t fb_left;
        int32_t fb_top;
        uint32_t src_x0;
        uint32_t src_y0;
        uint32_t blit_w;
        uint32_t blit_h;
        if (p >= 3u || color_fb[p].data == NULL || r->planes[p] == NULL) {
            continue;
        }
        shift = jxl_channel_shift_from_jpeg_upsampling(fh->jpeg_upsampling, p);
        if (source_jpeg_upsampled ||
            (defer_frame_upsample && shift.kind == JXL_CHANNEL_SHIFT_JPEG_UPSAMPLING &&
             !shift.has_h_subsample && !shift.has_v_subsample && !shift.h_subsample &&
             !shift.v_subsample)) {
            shift = jxl_channel_shift_from_shift(0);
        }
        down_h = (uint32_t)jxl_channel_shift_hshift(&shift);
        down_v = (uint32_t)jxl_channel_shift_vshift(&shift);
        fb_left = color_fb_region != NULL ? color_fb_region->left : 0;
        fb_top = color_fb_region != NULL ? color_fb_region->top : 0;
        src_x0 = (uint32_t)((crop.left - fb_left) >> (int32_t)down_h);
        src_y0 = (uint32_t)((crop.top - fb_top) >> (int32_t)down_v);
        blit_w = crop.width >> down_h;
        blit_h = crop.height >> down_v;
        if (jxl_render_blit_fb_crop_to_plane(color_fb[p], src_x0, src_y0, blit_w, blit_h,
                                             r->planes[p], r->width) != JXL_OK) {
            return JXL_ERROR_INVALID_INPUT;
        }
        jxl_render_set_plane_meta(r, p, &crop, &shift, blit_w, blit_h);
    }
    return JXL_OK;
}

jxl_status_t jxl_render_write_color_plane_from_fb(jxl_const_subgrid_f32 src, uint32_t cs_w,
                                                  uint32_t cs_h, const jxl_frame_header *fh,
                                                  uint32_t plane, jxl_render *r,
                                                  int defer_upsampling) {
    uint32_t buf_w;
    uint32_t buf_h;
    jxl_channel_shift shift;
    jxl_modular_region region;
    uint32_t copy_w;
    uint32_t copy_h;
    jxl_const_subgrid_f32 crop;
    if (fh == NULL || r == NULL || r->planes == NULL || plane >= r->num_planes ||
        r->planes[plane] == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }

    shift = color_shift_for_blit(fh, plane);
    region = jxl_modular_region_with_size(cs_w, cs_h);

    if (!defer_upsampling && src.width == r->width && src.height == r->height) {
        size_t y;
        jxl_channel_shift zero;
        for (y = 0; y < src.height; ++y) {
            memcpy(r->planes[plane] + y * r->width, src.data + y * src.stride,
                   src.width * sizeof(float));
        }
        zero = jxl_channel_shift_from_shift(0);
        jxl_render_set_plane_meta(r, plane, &region, &zero, r->width, r->height);
        return JXL_OK;
    }

    buf_w = 0;
    buf_h = 0;
    color_buf_size(cs_w, cs_h, &shift, &buf_w, &buf_h);
    (void)defer_upsampling;
    if (buf_w == 0 || buf_h == 0) {
        return JXL_ERROR_INVALID_INPUT;
    }

    copy_w = buf_w < (uint32_t)src.width ? buf_w : (uint32_t)src.width;
    copy_h = buf_h < (uint32_t)src.height ? buf_h : (uint32_t)src.height;
    crop = jxl_const_subgrid_f32_from_buf(src.data, copy_w, copy_h, src.stride);
    jxl_render_blit_subgrid_to_plane(crop, r->planes[plane], r->width);
    jxl_render_set_plane_meta(r, plane, &region, &shift, copy_w, copy_h);
    return JXL_OK;
}

void jxl_render_set_plane_meta(jxl_render *r, uint32_t plane, const jxl_modular_region *region,
                               const jxl_channel_shift *shift, uint32_t buf_width,
                               uint32_t buf_height) {
    if (r == NULL || r->meta == NULL || region == NULL || shift == NULL || plane >= r->num_planes) {
        return;
    }
    r->meta[plane].region = *region;
    r->meta[plane].shift = *shift;
    r->meta[plane].buf_width = buf_width;
    r->meta[plane].buf_height = buf_height;
    r->meta[plane].sample_x = 0;
    r->meta[plane].sample_y = 0;
    r->meta[plane].grid_x = 0;
    r->meta[plane].grid_y = 0;
}

const jxl_render_plane_meta *jxl_render_get_plane_meta(const jxl_render *r, uint32_t plane) {
    if (r == NULL || r->meta == NULL || plane >= r->num_planes) {
        return NULL;
    }
    return &r->meta[plane];
}

void jxl_render_prepare_color_upsampling(jxl_render *r, const jxl_frame_header *fh) {
    uint32_t p;
    uint32_t factor;
    if (r == NULL || r->meta == NULL || fh == NULL || fh->upsampling <= 1u) {
        return;
    }
    factor = frame_upsampling_log2(fh->upsampling);
    if (factor == 0) {
        return;
    }
    for (p = 0; p < r->num_planes; ++p) {
        jxl_render_plane_meta *m = &r->meta[p];
        if (m->buf_width == r->width && m->buf_height == r->height) {
            continue;
        }
        if (m->shift.kind == JXL_CHANNEL_SHIFT_JPEG_UPSAMPLING &&
            (m->shift.has_h_subsample || m->shift.has_v_subsample) &&
            (m->shift.h_subsample || m->shift.v_subsample)) {
            continue;
        }
        m->region = jxl_modular_region_upsample(m->region, factor);
        shift_add_upsampling(&m->shift, factor);
    }
}

jxl_status_t jxl_render_upsample_plane_jpeg(jxl_allocator_state *alloc, jxl_render *r,
                                          uint32_t plane, uint32_t target_w, uint32_t target_h) {
    jxl_render_plane_meta *m;
    uint32_t src_w;
    uint32_t src_h;
    jxl_const_subgrid_f32 src;
    jxl_channel_shift zero;
    if (alloc == NULL || r == NULL || r->planes == NULL || r->meta == NULL ||
        plane >= r->num_planes || r->planes[plane] == NULL || target_w == 0 || target_h == 0) {
        return JXL_ERROR_INVALID_INPUT;
    }

    m = &r->meta[plane];
    if (m->shift.kind == JXL_CHANNEL_SHIFT_SHIFTS) {
        return JXL_OK;
    }
    if (m->buf_width >= target_w && m->buf_height >= target_h &&
        jxl_channel_shift_hshift(&m->shift) == 0 && jxl_channel_shift_vshift(&m->shift) == 0) {
        return JXL_OK;
    }

    src_w = m->buf_width != 0 ? m->buf_width : target_w;
    src_h = m->buf_height != 0 ? m->buf_height : target_h;
    src = jxl_const_subgrid_f32_from_buf(r->planes[plane], src_w, src_h, r->width);
    if (!jxl_apply_jpeg_upsampling_single(alloc, src, m->shift, target_w, target_h, r->planes[plane],
                                          r->width)) {
        return JXL_ERROR_OUT_OF_MEMORY;
    }

    zero = jxl_channel_shift_from_shift(0);
    m->shift = zero;
    m->buf_width = target_w;
    m->buf_height = target_h;
    return JXL_OK;
}

jxl_status_t jxl_render_upsample_plane_to_target(jxl_allocator_state *alloc, jxl_render *r,
                                                 uint32_t plane,
                                                 const jxl_upsampling_weights *weights,
                                                 uint32_t frame_upsampling) {
    int ok;
    jxl_render_plane_meta *m;
    uint32_t src_w;
    uint32_t src_h;
    jxl_const_subgrid_f32 src;
    float *tmp;
    float *dst;
    size_t dst_stride;
    jxl_channel_shift zero;
    if (alloc == NULL || r == NULL || r->planes == NULL || r->meta == NULL ||
        plane >= r->num_planes || r->planes[plane] == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    if (!plane_needs_upsample(r, plane)) {
        return JXL_OK;
    }

    m = &r->meta[plane];
    src_w = m->buf_width != 0 ? m->buf_width : r->width;
    src_h = m->buf_height != 0 ? m->buf_height : r->height;
    if (src_w == 0 || src_h == 0) {
        return JXL_ERROR_INVALID_INPUT;
    }

    src = jxl_const_subgrid_f32_from_buf(r->planes[plane], src_w, src_h,
                                                               r->width);
    tmp = NULL;
    dst = r->planes[plane];
    dst_stride = r->width;

    if (src_w == r->width && src_h == r->height) {
        tmp = jxl_alloc(alloc, (size_t)r->width * (size_t)r->height * sizeof(float));
        if (tmp == NULL) {
            return JXL_ERROR_OUT_OF_MEMORY;
        }
        memcpy(tmp, dst, (size_t)r->width * (size_t)r->height * sizeof(float));
        src = jxl_const_subgrid_f32_from_buf(tmp, src_w, src_h, r->width);
        dst = tmp;
        dst_stride = r->width;
    }

    ok = 0;
    if (frame_upsampling > 1u) {
        uint32_t factor_log2 = frame_upsampling_log2(frame_upsampling);
        ok = jxl_apply_nonseparable_upsampling_single(alloc, src, weights, factor_log2, r->width,
                                                      r->height, dst, dst_stride);
    } else {
        ok = jxl_apply_jpeg_upsampling_single(alloc, src, m->shift, r->width, r->height, dst,
                                              dst_stride);
    }

    if (tmp != NULL) {
        if (ok) {
            memcpy(r->planes[plane], tmp, (size_t)r->width * (size_t)r->height * sizeof(float));
        }
        jxl_free(alloc, tmp);
    }

    if (!ok) {
        return JXL_ERROR_OUT_OF_MEMORY;
    }

    zero = jxl_channel_shift_from_shift(0);
    m->shift = zero;
    m->buf_width = r->width;
    m->buf_height = r->height;
    return JXL_OK;
}

static void oriented_map(uint32_t orientation, uint32_t src_w, uint32_t src_h, uint32_t x,
                         uint32_t y, uint32_t *out_x, uint32_t *out_y) {
    switch (orientation) {
    case 2:
        *out_x = src_w - 1 - x;
        *out_y = y;
        break;
    case 3:
        *out_x = src_w - 1 - x;
        *out_y = src_h - 1 - y;
        break;
    case 4:
        *out_x = x;
        *out_y = src_h - 1 - y;
        break;
    case 5:
        *out_x = y;
        *out_y = x;
        break;
    case 6:
        *out_x = src_h - 1 - y;
        *out_y = x;
        break;
    case 7:
        *out_x = src_h - 1 - y;
        *out_y = src_w - 1 - x;
        break;
    case 8:
        *out_x = y;
        *out_y = src_w - 1 - x;
        break;
    default:
        *out_x = x;
        *out_y = y;
        break;
    }
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

jxl_status_t jxl_render_apply_orientation(jxl_allocator_state *alloc, jxl_render *r,
                                          uint32_t orientation) {
                                              uint32_t p;
                                              uint32_t y;
    uint32_t dst_w;
    uint32_t dst_h;
    uint32_t src_w;
    uint32_t src_h;
    uint32_t planes;
    size_t dst_pixels;
    float *new_samples;
    float **new_planes;
    jxl_render_plane_meta *new_meta;
    if (alloc == NULL || r == NULL || orientation == 1) {
        return JXL_OK;
    }
    src_w = r->width;
    src_h = r->height;
    dst_w = 0;
    dst_h = 0;
    size_with_orientation(orientation, src_w, src_h, &dst_w, &dst_h);
    planes = r->num_planes;
    dst_pixels = (size_t)dst_w * dst_h;
    new_samples = jxl_calloc(alloc, dst_pixels * planes, sizeof(*new_samples));
    if (new_samples == NULL) {
        return JXL_ERROR_OUT_OF_MEMORY;
    }
    new_planes = jxl_calloc(alloc, planes, sizeof(*new_planes));
    new_meta =
        jxl_calloc(alloc, planes, sizeof(jxl_render_plane_meta));
    if (new_planes == NULL || new_meta == NULL) {
        jxl_free(alloc, new_samples);
        jxl_free(alloc, new_planes);
        jxl_free(alloc, new_meta);
        return JXL_ERROR_OUT_OF_MEMORY;
    }
    for (p = 0; p < planes; ++p) {
        new_planes[p] = new_samples + p * dst_pixels;
        if (r->meta != NULL) {
            new_meta[p] = r->meta[p];
            if (orientation >= 5 && orientation <= 8) {
                uint32_t rw = new_meta[p].region.width;
                new_meta[p].region.width = new_meta[p].region.height;
                new_meta[p].region.height = rw;
            }
            new_meta[p].buf_width = dst_w;
            new_meta[p].buf_height = dst_h;
        }
    }
    for (y = 0; y < src_h; ++y) {
        uint32_t x;
        for (x = 0; x < src_w; ++x) {
            uint32_t p;
            uint32_t ox = 0;
            uint32_t oy = 0;
            oriented_map(orientation, src_w, src_h, x, y, &ox, &oy);
            size_t dst_idx = (size_t)oy * dst_w + ox;
            size_t src_idx = (size_t)y * src_w + x;
            for (p = 0; p < planes; ++p) {
                if (r->planes[p] != NULL) {
                    new_planes[p][dst_idx] = r->planes[p][src_idx];
                }
            }
        }
    }
    if (r->bufs != NULL) {
        for (p = 0; p < planes; ++p) {
            jxl_image_buffer_destroy(alloc, &r->bufs[p]);
        }
        jxl_free(alloc, r->bufs);
    }
    jxl_free(alloc, r->samples);
    jxl_free(alloc, r->planes);
    jxl_free(alloc, r->meta);
    r->samples = new_samples;
    r->planes = new_planes;
    r->meta = new_meta;
    r->width = dst_w;
    r->height = dst_h;
    r->bufs = jxl_calloc(alloc, planes, sizeof(*r->bufs));
    if (r->bufs == NULL) {
        return JXL_ERROR_OUT_OF_MEMORY;
    }
    return render_rebind_plane_bufs_f32(alloc, r);
}

static uint32_t abs_diff_i32(int32_t a, int32_t b) {
    return a >= b ? (uint32_t)(a - b) : (uint32_t)(b - a);
}

jxl_status_t jxl_render_upsample_nonseparable(jxl_allocator_state *alloc, jxl_render *r,
                                              const jxl_modular_region *valid_region,
                                              const jxl_frame_header *fh,
                                              const jxl_upsampling_weights *weights,
                                              int ec_to_color_only) {
                                                  uint32_t p;
    uint32_t target_factor;
    uint32_t color_shift;
    if (alloc == NULL || r == NULL || r->meta == NULL || valid_region == NULL || fh == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    color_shift = frame_upsampling_log2(fh->upsampling);
    target_factor = ec_to_color_only ? color_shift : 0u;

    for (p = 0; p < r->num_planes; ++p) {
        jxl_render_plane_meta *m;
        uint32_t upsampling_factor;
        uint32_t steps;
        jxl_modular_region down_image;
        jxl_modular_region down_valid;
        uint32_t left;
        uint32_t top;
        uint32_t sub_w;
        uint32_t sub_h;
        jxl_const_subgrid_f32 src;
	if (ec_to_color_only && p < r->color_planes) {
            continue;
        }

        m = &r->meta[p];
        if (m->shift.kind == JXL_CHANNEL_SHIFT_JPEG_UPSAMPLING) {
            if (!ec_to_color_only && plane_needs_upsample(r, p)) {
                jxl_status_t st;
                if (jxl_render_plane_is_integer(r, p)) {
                    jxl_status_t mat_st = jxl_render_materialize_plane_f32(alloc, r, p);
                    if (mat_st != JXL_OK) {
                        return mat_st;
                    }
                }
                st = jxl_render_upsample_plane_to_target(alloc, r, p, weights, fh->upsampling);
                if (st != JXL_OK) {
                    return st;
                }
            }
            continue;
        }
        if (m->shift.kind != JXL_CHANNEL_SHIFT_SHIFTS) {
            continue;
        }

        upsampling_factor = m->shift.shift;
        if (upsampling_factor == target_factor) {
            continue;
        }
        if (upsampling_factor < target_factor) {
            return JXL_ERROR_INVALID_INPUT;
        }

        if (jxl_render_plane_is_integer(r, p)) {
            jxl_status_t mat_st =
                jxl_render_materialize_plane_f32(alloc, r, p);
            if (mat_st != JXL_OK) {
                return mat_st;
            }
        }

        steps = upsampling_factor - target_factor;
        down_image =
            jxl_modular_region_downsample(m->region, upsampling_factor);
        down_valid =
            jxl_modular_region_downsample(*valid_region, upsampling_factor);
        left = abs_diff_i32(down_valid.left, down_image.left);
        top = abs_diff_i32(down_valid.top, down_image.top);
        sub_w = down_valid.width;
        sub_h = down_valid.height;
        if (left + sub_w > m->buf_width || top + sub_h > m->buf_height) {
            sub_w = m->buf_width;
            sub_h = m->buf_height;
            left = 0;
            top = 0;
        }

        src = jxl_const_subgrid_f32_from_buf(
            r->planes[p] + (size_t)top * r->width + left, sub_w, sub_h, r->width);
        if (!jxl_apply_nonseparable_upsampling_single(alloc, src, weights, steps, r->width,
                                                      r->height, r->planes[p], r->width)) {
            return JXL_ERROR_OUT_OF_MEMORY;
        }

        m->region = down_valid;
        m->shift = jxl_channel_shift_from_shift(target_factor);
        m->buf_width = r->width;
        m->buf_height = r->height;
    }
    return JXL_OK;
}

jxl_status_t jxl_render_normalize_all_planes(jxl_allocator_state *alloc, jxl_render *r,
                                             const jxl_frame_header *fh,
                                             const jxl_upsampling_weights *weights,
                                             const jxl_modular_region *valid_region) {
    uint32_t p;
    jxl_status_t st;
    if (alloc == NULL || r == NULL || fh == NULL || valid_region == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    st = jxl_render_upsample_nonseparable(alloc, r, valid_region, fh, weights, 0);
    if (st != JXL_OK) {
        return st;
    }
    for (p = 0; p < r->num_planes; ++p) {
        if (!plane_needs_upsample(r, p)) {
            continue;
        }
        const jxl_render_plane_meta *m = jxl_render_get_plane_meta(r, p);
        if (m != NULL && m->shift.kind == JXL_CHANNEL_SHIFT_SHIFTS) {
            continue;
        }
        if (jxl_render_plane_is_integer(r, p)) {
            st = jxl_render_materialize_plane_f32(alloc, r, p);
            if (st != JXL_OK) {
                return st;
            }
        }
        st = jxl_render_upsample_plane_to_target(alloc, r, p, weights, fh->upsampling);
        if (st != JXL_OK) {
            return st;
        }
    }
    return JXL_OK;
}

jxl_status_t jxl_render_features_pipeline(jxl_context *ctx, jxl_allocator_state *alloc,
                                          jxl_render *r,
                                          const jxl_parsed_image_header *parsed,
                                          const jxl_frame_header *fh,
                                          const jxl_upsampling_weights *weights,
                                          const jxl_modular_region *valid_region,
                                          const jxl_lf_global *lf_global,
                                          const jxl_reference_store *refs,
                                          uint32_t visible_frames,
                                          uint32_t invisible_frames,
                                          const jxl_modular_region *render_region) {
    if (alloc == NULL || r == NULL || parsed == NULL || fh == NULL || valid_region == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }

    jxl_render_prepare_color_upsampling(r, fh);

    if (lf_global != NULL && lf_global->has_patches &&
        !JXL_DEBUG_FLAG(ctx, skip_patches) && refs != NULL) {
        jxl_status_t st = jxl_render_upsample_nonseparable(alloc, r, valid_region, fh, weights, 1);
        if (st != JXL_OK) {
            return st;
        }
        if (jxl_render_any_plane_integer(r)) {
            st = jxl_render_convert_modular_color(alloc, r, parsed->bit_depth_bits, r->color_planes);
            if (st != JXL_OK) {
                return st;
            }
        }
        st = jxl_apply_patches(alloc, parsed, fh, &lf_global->patches, refs, r->planes,
                               r->num_planes, r->width, r->height, render_region);
        if (st != JXL_OK) {
            return st;
        }
    }

    if (lf_global != NULL && lf_global->has_splines && r->color_planes >= 3u &&
        r->planes[0] != NULL && r->planes[1] != NULL && r->planes[2] != NULL &&
        !JXL_DEBUG_FLAG(ctx, skip_splines)) {
        jxl_status_t st;
        float corr_x;
        float corr_b;
        float *color_planes[3];
        if (jxl_render_any_plane_integer(r)) {
            st = jxl_render_convert_modular_color(alloc, r, parsed->bit_depth_bits, r->color_planes);
            if (st != JXL_OK) {
                return st;
            }
        }
        corr_x = lf_global->has_vardct ? lf_global->lf_chan_corr.base_correlation_x : 0.0f;
        corr_b = lf_global->has_vardct ? lf_global->lf_chan_corr.base_correlation_b : 1.0f;
        color_planes[0] = r->planes[0];
        color_planes[1] = r->planes[1];
        color_planes[2] = r->planes[2];
        if (!jxl_render_splines(alloc, fh, &lf_global->splines, corr_x, corr_b, color_planes,
                                r->width, r->height, render_region)) {
            return JXL_ERROR_INVALID_INPUT;
        }
    }

    if (lf_global != NULL && lf_global->has_noise && r->color_planes >= 3u && r->planes[0] != NULL &&
        r->planes[1] != NULL && r->planes[2] != NULL) {
        jxl_status_t st;
        float corr_x;
        float corr_b;
        if (jxl_render_any_plane_integer(r)) {
            st = jxl_render_convert_modular_color(alloc, r, parsed->bit_depth_bits, r->color_planes);
            if (st != JXL_OK) {
                return st;
            }
        }
        corr_x = lf_global->has_vardct ? lf_global->lf_chan_corr.base_correlation_x : 0.0f;
        corr_b = lf_global->has_vardct ? lf_global->lf_chan_corr.base_correlation_b : 1.0f;
        if (!jxl_render_noise(alloc, fh, visible_frames, invisible_frames, corr_x, corr_b,
                              r->planes[0], r->planes[1], r->planes[2], r->width, r->height,
                              &lf_global->noise, render_region)) {
            return JXL_ERROR_INVALID_INPUT;
        }
    }

    return jxl_render_normalize_all_planes(alloc, r, fh, weights, valid_region);
}

jxl_status_t jxl_render_ensure_all_planes_f32(jxl_allocator_state *alloc, jxl_render *r,
                                              const jxl_parsed_image_header *parsed) {
    uint32_t p;
    jxl_status_t st;
    if (alloc == NULL || r == NULL || parsed == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    for (p = 0; p < r->num_planes; ++p) {
        uint32_t bit_depth = parsed->bit_depth_bits;
        if (p >= r->color_planes) {
            size_t ec = (size_t)p - r->color_planes;
            bit_depth = jxl_parsed_ec_bit_depth(parsed, (uint32_t)ec);
        }
        if (!jxl_render_plane_is_integer(r, p)) {
            continue;
        }
        st = jxl_render_ensure_plane_f32(alloc, r, p, bit_depth);
        if (st != JXL_OK) {
            return st;
        }
    }
    return JXL_OK;
}

int jxl_render_plane_is_integer(const jxl_render *r, uint32_t plane) {
    if (r == NULL || r->bufs == NULL || plane >= r->num_planes) {
        return 0;
    }
    return r->bufs[plane].kind != JXL_IMAGE_BUFFER_F32;
}

int jxl_render_any_plane_integer(const jxl_render *r) {
    uint32_t p;
    if (r == NULL || r->bufs == NULL) {
        return 0;
    }
    for (p = 0; p < r->num_planes; ++p) {
        if (jxl_render_plane_is_integer(r, p)) {
            return 1;
        }
    }
    return 0;
}

void jxl_render_bind_materialization(jxl_render *r, jxl_allocator_state *alloc,
                                     const jxl_parsed_image_header *parsed) {
    uint32_t i;
    if (r == NULL || parsed == NULL) {
        return;
    }
    r->materialize_alloc = alloc;
    r->color_bit_depth_bits = parsed->bit_depth_bits;
    r->num_ec_bit_depths = parsed->num_extra_channels;
    if (r->num_ec_bit_depths > JXL_RENDER_MAX_EC_BIT_DEPTHS) {
        r->num_ec_bit_depths = JXL_RENDER_MAX_EC_BIT_DEPTHS;
    }
    for (i = 0; i < r->num_ec_bit_depths; ++i) {
        r->ec_bit_depth_bits[i] = (uint8_t)jxl_parsed_ec_bit_depth(parsed, i);
    }
}

uint32_t jxl_render_plane_bit_depth(const jxl_render *r, uint32_t plane) {
    if (r == NULL) {
        return 8;
    }
    if (plane < r->color_planes) {
        return r->color_bit_depth_bits != 0 ? r->color_bit_depth_bits : 8u;
    }
    {
        uint32_t ec = plane - r->color_planes;
        if (ec < r->num_ec_bit_depths && r->ec_bit_depth_bits[ec] != 0) {
            return (uint32_t)r->ec_bit_depth_bits[ec];
        }
    }
    return r->color_bit_depth_bits != 0 ? r->color_bit_depth_bits : 8u;
}

jxl_status_t jxl_render_materialize_plane_f32(jxl_allocator_state *alloc, jxl_render *r,
                                            uint32_t plane) {
    if (alloc == NULL || r == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    if (!jxl_render_plane_is_integer(r, plane)) {
        return JXL_OK;
    }
    return jxl_render_ensure_plane_f32(alloc, r, plane, jxl_render_plane_bit_depth(r, plane));
}

jxl_status_t jxl_render_ensure_plane_f32(jxl_allocator_state *alloc, jxl_render *r, uint32_t plane,
                                         uint32_t bit_depth_bits) {
    if (alloc == NULL || r == NULL || r->bufs == NULL || r->planes == NULL ||
        plane >= r->num_planes) {
        return JXL_ERROR_INVALID_INPUT;
    }
    if (r->planes[plane] == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    if (r->bufs[plane].kind == JXL_IMAGE_BUFFER_F32) {
        r->planes[plane] = r->bufs[plane].u.f32.data;
        return JXL_OK;
    }
    {
        const jxl_render_plane_meta *m = jxl_render_get_plane_meta(r, plane);
        uint32_t gw = (uint32_t)jxl_image_buffer_width(&r->bufs[plane]);
        uint32_t gh = (uint32_t)jxl_image_buffer_height(&r->bufs[plane]);

        if (m != NULL) {
            jxl_modular_channel_info info = {0};
            jxl_modular_region region;
            uint32_t blit_w = m->buf_width != 0 ? m->buf_width : gw;
            uint32_t blit_h = m->buf_height != 0 ? m->buf_height : gh;

            info.original_width = m->region.width;
            info.original_height = m->region.height;
            info.width = m->region.width;
            info.height = m->region.height;
            info.original_shift = m->shift;
            region.left = (int32_t)m->grid_x;
            region.top = (int32_t)m->grid_y;
            region.width = blit_w;
            region.height = blit_h;
            if (!jxl_modular_blit_channel_region_to_plane(
                    &r->bufs[plane].u.grid, &info, bit_depth_bits, region, r->width,
                    r->planes[plane], m->sample_x, m->sample_y)) {
                return JXL_ERROR_INVALID_INPUT;
            }
            jxl_modular_grid_i32_destroy(alloc, &r->bufs[plane].u.grid);
            r->bufs[plane].kind = JXL_IMAGE_BUFFER_F32;
            r->bufs[plane].u.f32.data = r->planes[plane];
            r->bufs[plane].u.f32.owns = 0;
            return JXL_OK;
        }

        if (gw <= r->width && gh <= r->height) {
            return jxl_image_buffer_convert_to_float_modular(alloc, &r->bufs[plane], bit_depth_bits,
                                                           r->planes[plane], r->width, r->height);
        }
        {
            jxl_status_t st;
            float *owned = jxl_calloc(alloc, (size_t)gw * (size_t)gh, sizeof(float));
            if (owned == NULL) {
                return JXL_ERROR_OUT_OF_MEMORY;
            }
            st = jxl_image_buffer_convert_to_float_modular(alloc, &r->bufs[plane], bit_depth_bits,
                                                         owned, gw, gh);
            if (st != JXL_OK) {
                jxl_free(alloc, owned);
                return st;
            }
            r->bufs[plane].kind = JXL_IMAGE_BUFFER_F32;
            r->bufs[plane].u.f32.data = owned;
            r->bufs[plane].u.f32.owns = 1;
            r->planes[plane] = owned;
            return JXL_OK;
        }
    }
}

jxl_status_t jxl_render_convert_modular_color(jxl_allocator_state *alloc, jxl_render *r,
                                              uint32_t bit_depth_bits, uint32_t color_planes) {
    uint32_t p;
    jxl_status_t st;
    if (alloc == NULL || r == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    if (color_planes == 0 || color_planes > r->color_planes) {
        color_planes = r->color_planes;
    }
    for (p = 0; p < color_planes && p < r->num_planes; ++p) {
        st = jxl_render_ensure_plane_f32(alloc, r, p, bit_depth_bits);
        if (st != JXL_OK) {
            return st;
        }
    }
    return JXL_OK;
}

jxl_status_t jxl_render_extend_from_modular_dest(jxl_allocator_state *alloc,
                                                 jxl_modular_image_destination *dest,
                                                 const jxl_frame_header *fh, jxl_render *r,
                                                 uint32_t num_transfer_planes, int32_t ox,
                                                 int32_t oy,
                                                 const jxl_render_modular_placement *placement) {
    size_t nb_meta;
    uint32_t p;
    if (alloc == NULL || dest == NULL || fh == NULL || r == NULL || r->bufs == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    nb_meta = dest->channels.nb_meta_channels;
    for (p = 0; p < num_transfer_planes && p < r->num_planes; ++p) {
        size_t ch = nb_meta + (size_t)p;
        size_t q;
        void *buf;
        if (ch >= dest->image_channels_len) {
            break;
        }
        buf = dest->image_channels[ch].buf;
        for (q = 0; q < p && buf != NULL; ++q) {
            size_t ch2 = nb_meta + q;
            if (ch2 < dest->image_channels_len && dest->image_channels[ch2].buf == buf) {
                if (!jxl_modular_grid_clone(alloc, &dest->image_channels[ch2],
                                            &dest->image_channels[ch])) {
                    return JXL_ERROR_OUT_OF_MEMORY;
                }
                break;
            }
        }
    }
    for (p = 0; p < num_transfer_planes && p < r->num_planes; ++p) {
        size_t ch = nb_meta + (size_t)p;
        uint32_t ow;
        uint32_t oh;
        uint32_t gw;
        uint32_t gh;
        uint32_t meta_w;
        uint32_t meta_h;
        jxl_modular_region region;
        const jxl_modular_channel_info *info;

        if (ch >= dest->image_channels_len) {
            break;
        }
        info = &dest->channels.info[ch];
        jxl_image_buffer_take_grid(alloc, &r->bufs[p], &dest->image_channels[ch]);

        ow = info->original_width != 0 ? info->original_width : info->width;
        oh = info->original_height != 0 ? info->original_height : info->height;
        gw = (uint32_t)jxl_image_buffer_width(&r->bufs[p]);
        gh = (uint32_t)jxl_image_buffer_height(&r->bufs[p]);
        if (gw == 0 || gh == 0) {
            jxl_channel_shift_shift_size(&info->original_shift, ow, oh, &gw, &gh);
        }

        region.left = ox;
        region.top = oy;
        region.width = ow;
        region.height = oh;
        meta_w = gw;
        meta_h = gh;
        if (placement != NULL && placement->valid && placement->blit_w > 0 &&
            placement->blit_h > 0) {
            meta_w = placement->blit_w;
            meta_h = placement->blit_h;
        }
        jxl_render_set_plane_meta(r, p, &region, &info->original_shift, meta_w, meta_h);
        if (placement != NULL && placement->valid) {
            r->meta[p].sample_x = placement->dst_x0;
            r->meta[p].sample_y = placement->dst_y0;
            r->meta[p].grid_x = placement->src_x0;
            r->meta[p].grid_y = placement->src_y0;
        }
    }
    return JXL_OK;
}
