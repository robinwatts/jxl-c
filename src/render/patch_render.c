// SPDX-License-Identifier: MIT OR Apache-2.0
#include "patch_render.h"

#include "codestream_collect.h"
#include "context.h"
#include "frame/frame.h"
#include "frame/lf_global.h"
#include "frame/lf_global_modular.h"
#include "frame/pass_group.h"
#include "frame/toc.h"
#include "modular/group_decode.h"
#include "modular/param.h"
#include "modular/prepare_subimage.h"
#include "modular/subimage_decode.h"
#include "modular/transform/inverse.h"
#include "render/composite.h"
#include "render/modular_compose.h"
#include "render/render_buffer.h"
#include "render/subgrid_f32.h"
#include "render/vardct/frame_render.h"
#include "vardct/lf.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int32_t left;
    int32_t top;
    uint32_t width;
    uint32_t height;
} jxl_region;

static jxl_region region_intersect(jxl_region a, jxl_region b) {
    int32_t left = a.left > b.left ? a.left : b.left;
    int32_t top = a.top > b.top ? a.top : b.top;
    int32_t right_a = a.left + (int32_t)a.width;
    int32_t bottom_a = a.top + (int32_t)a.height;
    int32_t right_b = b.left + (int32_t)b.width;
    int32_t bottom_b = b.top + (int32_t)b.height;
    int32_t right = right_a < right_b ? right_a : right_b;
    int32_t bottom = bottom_a < bottom_b ? bottom_a : bottom_b;
    jxl_region result;

    if (right <= left || bottom <= top) {
        result.left = 0;
        result.top = 0;
        result.width = 0;
        result.height = 0;
        return result;
    }
    result.left = left;
    result.top = top;
    result.width = (uint32_t)(right - left);
    result.height = (uint32_t)(bottom - top);
    return result;
}

static float clamp01(float v, int clamp) {
    if (!clamp) {
        return v;
    }
    if (v < 0.0f) {
        return 0.0f;
    }
    if (v > 1.0f) {
        return 1.0f;
    }
    return v;
}

void jxl_ref_image_set_crop_from_frame(jxl_ref_image *img, const jxl_frame_header *fh) {
    if (img == NULL || fh == NULL || !img->valid) {
        return;
    }
    img->have_crop = fh->have_crop;
    img->x0 = fh->have_crop ? fh->x0 : 0;
    img->y0 = fh->have_crop ? fh->y0 : 0;
}

void jxl_reference_store_init(jxl_reference_store *store) {
    if (store != NULL) {
        memset(store, 0, sizeof(*store));
    }
}

void jxl_reference_store_free(jxl_allocator_state *alloc, jxl_reference_store *store) {
    size_t i;
    if (store == NULL) {
        return;
    }
    for (i = 0; i < 4; ++i) {
        if (store->slots[i].samples != NULL) {
            jxl_free(alloc, store->slots[i].samples);
        } else if (store->slots[i].planes != NULL) {
            uint32_t p;
            for (p = 0; p < store->slots[i].num_planes; ++p) {
                jxl_free(alloc, store->slots[i].planes[p]);
            }
        }
        jxl_free(alloc, store->slots[i].planes);
        store->slots[i].samples = NULL;
        store->slots[i].planes = NULL;
        store->slots[i].valid = 0;
    }
}

void jxl_progressive_lf_image_free(jxl_allocator_state *alloc, jxl_progressive_lf_image *lf) {
    size_t ch;
    if (lf == NULL) {
        return;
    }
    for (ch = 0; ch < 3; ++ch) {
        jxl_free(alloc, lf->plane[ch]);
    }
    jxl_free(alloc, lf->samples[0]);
    memset(lf, 0, sizeof(*lf));
}

void jxl_progressive_lf_store_init(jxl_progressive_lf_store *store) {
    if (store != NULL) {
        memset(store, 0, sizeof(*store));
    }
}

void jxl_progressive_lf_store_free(jxl_allocator_state *alloc, jxl_progressive_lf_store *store) {
    size_t i;
    if (store == NULL) {
        return;
    }
    for (i = 0; i < 4; ++i) {
        jxl_progressive_lf_image_free(alloc, &store->slots[i]);
    }
}

const jxl_progressive_lf_image *jxl_progressive_lf_store_get(const jxl_progressive_lf_store *store,
                                                             uint32_t lf_level) {
    if (store == NULL || lf_level >= 4) {
        return NULL;
    }
    return &store->slots[lf_level];
}

static jxl_status_t modular_to_status(jxl_modular_status_t st) {
    switch (st) {
    case JXL_MODULAR_OK:
        return JXL_OK;
    case JXL_MODULAR_OUT_OF_MEMORY:
        return JXL_ERROR_OUT_OF_MEMORY;
    default:
        return JXL_ERROR_INVALID_INPUT;
    }
}

static jxl_status_t frame_to_status(jxl_frame_status_t st) {
    switch (st) {
    case JXL_FRAME_OK:
        return JXL_OK;
    case JXL_FRAME_OUT_OF_MEMORY:
        return JXL_ERROR_OUT_OF_MEMORY;
    default:
        return JXL_ERROR_INVALID_INPUT;
    }
}

static float grid_sample_f(const jxl_modular_grid_i32 *g, size_t x, size_t y, float scale) {
    return (float)jxl_modular_grid_sample_as_i32(g, x, y) * scale;
}

static int32_t grid_sample_i32(const jxl_modular_grid_i32 *g, size_t x, size_t y) {
    return jxl_modular_grid_sample_as_i32(g, x, y);
}

static jxl_status_t upsample_plane_nn(jxl_allocator_state *alloc, const float *src, uint32_t sw,
                                    uint32_t sh, uint32_t shift, float **out_plane,
                                    uint32_t *out_w, uint32_t *out_h) {
                                        uint32_t y;
    uint32_t step;
    uint32_t dw;
    uint32_t dh;
    size_t dpixels;
    float *dst;
    if (src == NULL || out_plane == NULL || out_w == NULL || out_h == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    if (shift == 0) {
        size_t pixels = (size_t)sw * (size_t)sh;
        float *copy = jxl_alloc(alloc, pixels * sizeof(float));
        if (copy == NULL) {
            return JXL_ERROR_OUT_OF_MEMORY;
        }
        memcpy(copy, src, pixels * sizeof(float));
        *out_plane = copy;
        *out_w = sw;
        *out_h = sh;
        return JXL_OK;
    }

    step = 1u << shift;
    dw = sw << shift;
    dh = sh << shift;
    dpixels = (size_t)dw * (size_t)dh;
    dst = jxl_alloc(alloc, dpixels * sizeof(float));
    if (dst == NULL) {
        return JXL_ERROR_OUT_OF_MEMORY;
    }
    for (y = 0; y < sh; ++y) {
        uint32_t x;
        for (x = 0; x < sw; ++x) {
            uint32_t dy;
            float v = src[(size_t)y * sw + x];
            for (dy = 0; dy < step; ++dy) {
                uint32_t dx;
                for (dx = 0; dx < step; ++dx) {
                    dst[(size_t)(y * step + dy) * dw + (size_t)(x * step + dx)] = v;
                }
            }
        }
    }
    *out_plane = dst;
    *out_w = dw;
    *out_h = dh;
    return JXL_OK;
}

static jxl_status_t modular_dest_to_xyb_planes(jxl_context *library_ctx, jxl_allocator_state *alloc,
                                               const jxl_modular_image_destination *dest,
                                               const jxl_lf_channel_dequant *dequant,
                                               jxl_ref_image *out) {
                                                   int c;
                                                   uint32_t y;
    size_t first = dest->channels.nb_meta_channels;
    uint32_t widths[3];
    uint32_t heights[3];
    uint32_t y_hscale;
    uint32_t y_vscale;
    uint32_t plane_w[3];
    uint32_t plane_h[3];
    const jxl_modular_grid_i32 *grid_y;
    const jxl_modular_grid_i32 *grid_x;
    const jxl_modular_grid_i32 *grid_b;
    const jxl_modular_grid_i32 *grids[3];
    float m_x;
    float m_y;
    float m_b;
    float **planes;
    if (first + 3 > dest->image_channels_len || dequant == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    grid_y = &dest->image_channels[first + 0];
    grid_x = &dest->image_channels[first + 1];
    grid_b = &dest->image_channels[first + 2];
    grids[0] = grid_y;
    grids[1] = grid_x;
    grids[2] = grid_b;
    for (c = 0; c < 3; ++c) {
        if (grids[c]->width == 0 || grids[c]->height == 0) {
            return JXL_ERROR_UNSUPPORTED;
        }
        widths[c] = (uint32_t)grids[c]->width;
        heights[c] = (uint32_t)grids[c]->height;
    }

    m_x = jxl_lf_channel_dequant_m_x_unscaled(dequant);
    m_y = jxl_lf_channel_dequant_m_y_unscaled(dequant);
    m_b = jxl_lf_channel_dequant_m_b_unscaled(dequant);
    if (JXL_DEBUG_FLAG(library_ctx, debug_lf)) {
        fprintf(stderr, "modular_xyb sizes y=%ux%u x=%ux%u b=%ux%u m_x=%g m_y=%g\n", widths[0],
                heights[0], widths[1], heights[1], widths[2], heights[2], m_x, m_y);
    }

    planes = jxl_alloc(alloc, 3 * sizeof(float *));
    if (planes == NULL) {
        return JXL_ERROR_OUT_OF_MEMORY;
    }
    memset(planes, 0, 3 * sizeof(float *));

    plane_w[0] = widths[1];
    plane_w[1] = widths[0];
    plane_w[2] = widths[2];

    plane_h[0] = heights[1];
    plane_h[1] = heights[0];
    plane_h[2] = heights[2];

    for (c = 0; c < 3; ++c) {
        size_t pixels = (size_t)plane_w[c] * (size_t)plane_h[c];
        planes[c] = jxl_alloc(alloc, pixels * sizeof(float));
        if (planes[c] == NULL) {
            int k;
            for (k = 0; k < c; ++k) {
                jxl_free(alloc, planes[k]);
            }
            jxl_free(alloc, planes);
            return JXL_ERROR_OUT_OF_MEMORY;
        }
    }

    for (y = 0; y < plane_h[1]; ++y) {
        uint32_t x;
        for (x = 0; x < plane_w[1]; ++x) {
            int32_t py = grid_sample_i32(grid_y, x, y);
            size_t idx = (size_t)y * plane_w[1] + x;
            planes[1][idx] = (float)py * m_y;
        }
    }
    for (y = 0; y < plane_h[0]; ++y) {
        uint32_t x;
        for (x = 0; x < plane_w[0]; ++x) {
            int32_t px = grid_sample_i32(grid_x, x, y);
            size_t idx = (size_t)y * plane_w[0] + x;
            planes[0][idx] = (float)px * m_x;
        }
    }
    y_hscale = widths[0] / widths[2];
    y_vscale = heights[0] / heights[2];
    if (y_hscale == 0) {
        y_hscale = 1;
    }
    if (y_vscale == 0) {
        y_vscale = 1;
    }
    for (y = 0; y < plane_h[2]; ++y) {
        uint32_t x;
        for (x = 0; x < plane_w[2]; ++x) {
            int32_t pb = grid_sample_i32(grid_b, x, y);
            int32_t py =
                grid_sample_i32(grid_y, x * y_hscale, y * y_vscale);
            int32_t b_sat = pb + py;
            size_t idx = (size_t)y * plane_w[2] + x;
            planes[2][idx] = (float)b_sat * m_b;
        }
    }

    out->valid = 1;
    out->width = plane_w[1];
    out->height = plane_h[1];
    out->num_planes = 3;
    out->samples = NULL;
    out->planes = planes;
    for (c = 0; c < 3; ++c) {
        out->plane_w[c] = plane_w[c];
        out->plane_h[c] = plane_h[c];
    }
    return JXL_OK;
}

static jxl_status_t modular_dest_to_planes(jxl_allocator_state *alloc,
                                           const jxl_modular_image_destination *dest,
                                           uint32_t bit_depth, uint32_t num_planes,
                                           jxl_ref_image *out) {
    uint32_t p;
    uint32_t fw;
    uint32_t fh;
    const jxl_modular_channel_info *info;
    float scale;
    size_t pixels;
    float *samples;
    float **planes;
    size_t first = dest->channels.nb_meta_channels;
    if (first + num_planes > dest->image_channels_len) {
        return JXL_ERROR_INVALID_INPUT;
    }
    info = &dest->channels.info[first];
    fw = info->width;
    fh = info->height;
    scale = bit_depth >= 31 ? 1.0f : 1.0f / (float)((1u << bit_depth) - 1u);

    pixels = (size_t)fw * (size_t)fh;
    samples = jxl_alloc(alloc, pixels * num_planes * sizeof(float));
    if (samples == NULL) {
        return JXL_ERROR_OUT_OF_MEMORY;
    }
    planes = jxl_alloc(alloc, num_planes * sizeof(float *));
    if (planes == NULL) {
        jxl_free(alloc, samples);
        return JXL_ERROR_OUT_OF_MEMORY;
    }
    for (p = 0; p < num_planes; ++p) {
        uint32_t y;
        const jxl_modular_grid_i32 *grid;
        planes[p] = samples + (size_t)p * pixels;
        grid = &dest->image_channels[first + p];
        if (grid->width != fw || grid->height != fh) {
            jxl_free(alloc, planes);
            jxl_free(alloc, samples);
            return JXL_ERROR_UNSUPPORTED;
        }
        for (y = 0; y < fh; ++y) {
            uint32_t x;
            for (x = 0; x < fw; ++x) {
                planes[p][(size_t)y * fw + x] = grid_sample_f(grid, x, y, scale);
            }
        }
    }
    out->valid = 1;
    out->width = fw;
    out->height = fh;
    out->num_planes = num_planes;
    out->samples = samples;
    out->planes = planes;
    return JXL_OK;
}

static jxl_status_t modular_dest_append_extra_planes(jxl_allocator_state *alloc,
                                                     const jxl_modular_image_destination *dest,
                                                     uint32_t bit_depth, uint32_t color_channels,
                                                     uint32_t num_extra, jxl_ref_image *out) {
    uint32_t p;
    uint32_t ec;
    size_t first;
    size_t ec_base;
    float scale;
    uint32_t new_np;
    float **planes;
    if (num_extra == 0 || out == NULL || out->planes == NULL) {
        return JXL_OK;
    }
    first = dest->channels.nb_meta_channels;
    ec_base = first + (size_t)color_channels;
    if (ec_base + num_extra > dest->image_channels_len) {
        return JXL_ERROR_INVALID_INPUT;
    }
    scale = bit_depth >= 31 ? 1.0f : 1.0f / (float)((1u << bit_depth) - 1u);
    new_np = out->num_planes + num_extra;
    planes = jxl_alloc(alloc, new_np * sizeof(float *));
    if (planes == NULL) {
        return JXL_ERROR_OUT_OF_MEMORY;
    }
    for (p = 0; p < out->num_planes; ++p) {
        planes[p] = out->planes[p];
    }
    for (ec = 0; ec < num_extra; ++ec) {
        uint32_t y;
        const jxl_modular_grid_i32 *grid = &dest->image_channels[ec_base + ec];
        const jxl_modular_channel_info *info = &dest->channels.info[ec_base + ec];
        uint32_t fw = info->width;
        uint32_t fh = info->height;
        size_t pixels;
        float *plane;
        uint32_t pi;
        if (grid->width != fw || grid->height != fh) {
            jxl_free(alloc, planes);
            return JXL_ERROR_UNSUPPORTED;
        }
        pixels = (size_t)fw * (size_t)fh;
        planes[out->num_planes + ec] = jxl_alloc(alloc, pixels * sizeof(float));
        if (planes[out->num_planes + ec] == NULL) {
            uint32_t k;
            for (k = out->num_planes; k < out->num_planes + ec; ++k) {
                jxl_free(alloc, planes[k]);
            }
            jxl_free(alloc, planes);
            return JXL_ERROR_OUT_OF_MEMORY;
        }
        plane = planes[out->num_planes + ec];
        for (y = 0; y < fh; ++y) {
            uint32_t x;
            for (x = 0; x < fw; ++x) {
                plane[(size_t)y * fw + x] = grid_sample_f(grid, x, y, scale);
            }
        }
        pi = out->num_planes + ec;
        if (pi < 3u) {
            out->plane_w[pi] = fw;
            out->plane_h[pi] = fh;
        }
    }
    jxl_free(alloc, out->planes);
    out->planes = planes;
    out->num_planes = new_np;
    return JXL_OK;
}

jxl_status_t jxl_decode_modular_prereq_frame(jxl_context *library_ctx, jxl_allocator_state *alloc,
                                             const uint8_t *input, size_t input_len,
                                             const uint8_t *codestream, size_t cs_len, jxl_bs *bs,
                                             const jxl_parsed_image_header *parsed,
                                             jxl_frame *frame, jxl_reference_store *refs,
                                             jxl_render *canvas, jxl_ref_image *out) {
    size_t consumed;
    jxl_bs gbs;
    jxl_lf_channel_dequant dequant;
    int has_ma;
    jxl_ma_config global_ma;
    jxl_modular_params mod_params;
    jxl_modular_image_destination dest;
    jxl_modular_parse_ctx parse_ctx = {0};
    (void)input;
    (void)input_len;
    size_t meta_end = bs->num_read_bits / 8;
    const jxl_frame_group_data *src;
    int multi_group;
    jxl_modular_status_t mst;
    jxl_status_t pgst;
    uint32_t inv_w;
    uint32_t inv_h;
    jxl_status_t st;
    if (meta_end > cs_len || frame->toc.total_size > cs_len - meta_end) {
        return JXL_ERROR_INVALID_INPUT;
    }
    consumed = 0;
    jxl_frame_feed_bytes(frame, codestream + meta_end, frame->toc.total_size, &consumed);
    if (consumed != frame->toc.total_size || !jxl_frame_is_loading_done(frame)) {
        return JXL_ERROR_INVALID_INPUT;
    }

    src = jxl_toc_is_single_entry(&frame->toc)
                                          ? (frame->data_len > 0 ? &frame->data[0] : NULL)
                                          : jxl_frame_group_by_kind(frame, JXL_TOC_KIND_LF_GLOBAL, 0);
    if (src == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }

    jxl_bs_init(&gbs, src->bytes, src->bytes_len);
    {
        jxl_frame_status_t pst =
            jxl_lf_global_parse_prefix(alloc, &gbs, parsed, &frame->header, NULL, NULL, NULL);
        if (pst != JXL_FRAME_OK) {
            return JXL_ERROR_INVALID_INPUT;
        }
    }
    if (jxl_lf_channel_dequant_parse(&gbs, &dequant) != JXL_VARDCT_OK) {
        return JXL_ERROR_INVALID_INPUT;
    }

    has_ma = 0;
    if (jxl_bs_read_bool(&gbs, &has_ma) != JXL_BS_OK) {
        return JXL_ERROR_INVALID_INPUT;
    }
    jxl_ma_config_init(&global_ma);
    if (has_ma) {
        jxl_ma_config_params ma_params = {0};
        uint64_t num_ch = (uint64_t)frame->header.encoded_color_channels +
                          (uint64_t)parsed->num_extra_channels;
        uint64_t samples =
            (uint64_t)frame->header.width * (uint64_t)frame->header.height * num_ch / 16u;
        size_t node_limit = (size_t)(1024u + samples);
        if (node_limit > (1u << 22)) {
            node_limit = 1u << 22;
        }
        ma_params.tracker = NULL;
        ma_params.node_limit = node_limit;
        ma_params.depth_limit = 2048;

        if (jxl_ma_config_parse(alloc, &gbs, &ma_params, &global_ma) != JXL_MODULAR_OK) {
            return JXL_ERROR_INVALID_INPUT;
        }
    }

    jxl_modular_params_init(&mod_params);
    if (!jxl_modular_params_set_for_modular_frame(alloc, library_ctx, &mod_params, parsed,
                                                &frame->header)) {
        jxl_ma_config_destroy(alloc, &global_ma);
        return JXL_ERROR_OUT_OF_MEMORY;
    }

    jxl_modular_image_destination_init(&dest);
    multi_group = !jxl_toc_is_single_entry(&frame->toc);
    parse_ctx.params = &mod_params;
    parse_ctx.global_ma = has_ma ? &global_ma : NULL;
    parse_ctx.tracker = NULL;
    parse_ctx.ctx = library_ctx;
    parse_ctx.retain_pretransform_channels = 1;

    mst =jxl_modular_dest_apply_local_header(alloc, &gbs, &parse_ctx, &dest);
    if (mst != JXL_MODULAR_OK) {
        jxl_modular_image_destination_free(alloc, &dest);
        jxl_modular_params_free(alloc, &mod_params);
        jxl_ma_config_destroy(alloc, &global_ma);
        return modular_to_status(mst);
    }
    if (jxl_modular_image_has_squeeze(&dest)) {
        parse_ctx.retain_pretransform_channels = 1;
    }
    mst = jxl_modular_prepare_gmodular(alloc, &dest);
    if (mst != JXL_MODULAR_OK) {
        jxl_modular_image_destination_free(alloc, &dest);
        jxl_modular_params_free(alloc, &mod_params);
        jxl_ma_config_destroy(alloc, &global_ma);
        return modular_to_status(mst);
    }
    mst = jxl_modular_gmodular_decode(library_ctx, alloc, &gbs, &dest, multi_group ? 1 : 0);
    if (mst != JXL_MODULAR_OK) {
        jxl_modular_image_destination_free(alloc, &dest);
        jxl_modular_params_free(alloc, &mod_params);
        jxl_ma_config_destroy(alloc, &global_ma);
        return modular_to_status(mst);
    }
    if (JXL_DEBUG_FLAG(library_ctx, debug_fb) &&
        frame->header.frame_type == JXL_FRAME_TYPE_LF) {
        size_t first = dest.channels.nb_meta_channels;
        size_t bx = 320;
        size_t by = 150;
        const jxl_modular_grid_i32 *gy = &dest.image_channels[first + 0];
        if (by < gy->height && bx < gy->width) {
            fprintf(stderr, "prereq_gmod ch0@%zu,%zu=%d\n", bx, by,
                    grid_sample_i32(gy, bx, by));
        }
    }
    if (JXL_DEBUG_FLAG(library_ctx, debug_fb) && frame->header.frame_type == JXL_FRAME_TYPE_LF) {
        uint32_t i;
        uint32_t nlf = jxl_frame_header_num_lf_groups(&frame->header);
        uint32_t ng = jxl_frame_header_num_groups(&frame->header);
        fprintf(stderr, "lf_toc nlf=%u ng=%u\n", nlf, ng);
        for (i = 0; i < nlf && i < 4; ++i) {
            const jxl_frame_group_data *g =
                jxl_frame_group_by_kind(frame, JXL_TOC_KIND_LF_GROUP, i);
            fprintf(stderr, "  lf_group[%u] len=%zu\n", i, g != NULL ? g->bytes_len : 0);
        }
        for (i = 0; i < ng && i < 4; ++i) {
            const jxl_frame_group_data *g =
                jxl_frame_group_by_kind(frame, JXL_TOC_KIND_GROUP_PASS, i);
            fprintf(stderr, "  pg[%u] len=%zu\n", i, g != NULL ? g->bytes_len : 0);
        }
    }
    pgst = jxl_modular_decode_frame_group_coefficients(
        library_ctx, alloc, frame, parsed, &global_ma, has_ma, &mod_params, &dest, multi_group, 1,
        NULL);
    if (pgst != JXL_OK) {
        jxl_modular_image_destination_free(alloc, &dest);
        jxl_modular_params_free(alloc, &mod_params);
        jxl_ma_config_destroy(alloc, &global_ma);
        return pgst;
    }
    if (JXL_DEBUG_FLAG(library_ctx, debug_fb) &&
        frame->header.frame_type == JXL_FRAME_TYPE_LF) {
        size_t first = dest.channels.nb_meta_channels;
        size_t bx = 320;
        size_t by = 150;
        const jxl_modular_grid_i32 *gy = &dest.image_channels[first + 0];
        if (by < gy->height && bx < gy->width) {
            fprintf(stderr, "prereq_postpg ch0@%zu,%zu=%d\n", bx, by,
                    grid_sample_i32(gy, bx, by));
        }
    }

    if (JXL_DEBUG_FLAG(library_ctx, debug_lf)) {
        size_t c;
        size_t first = dest.channels.nb_meta_channels;
        fprintf(stderr, "modular frame type=%d %ux%u ch_len=%zu meta=%zu multi=%d\n",
                (int)frame->header.frame_type, dest.channels.info[first].width,
                dest.channels.info[first].height, dest.image_channels_len, first, multi_group);
        for (c = 0; c + first < dest.image_channels_len && c < 3; ++c) {
            size_t y;
            int64_t sum;
            const jxl_modular_grid_i32 *g = &dest.image_channels[first + c];
            sum = 0;
            for (y = 0; y < g->height; ++y) {
                size_t x;
                for (x = 0; x < g->width; ++x) {
                    int32_t v = grid_sample_i32(g, x, y);
                    sum += v < 0 ? -(int64_t)v : (int64_t)v;
                }
            }
            fprintf(stderr, "  pre_xform ch%zu sum_abs=%lld\n", c, (long long)sum);
        }
    }

    inv_w = jxl_frame_header_color_sample_width(&frame->header);
    inv_h = jxl_frame_header_color_sample_height(&frame->header);
    mst = jxl_modular_gmodular_finish(library_ctx, alloc, &dest, inv_w, inv_h,
                                      parsed->bit_depth_bits, &mod_params);
    if (JXL_DEBUG_FLAG(library_ctx, debug_lf) && mst == JXL_MODULAR_OK) {
        size_t c;
        size_t first = dest.channels.nb_meta_channels;
        for (c = 0; c + first < dest.image_channels_len && c < 3; ++c) {
            size_t y;
            int64_t sum;
            const jxl_modular_grid_i32 *g = &dest.image_channels[first + c];
            sum = 0;
            for (y = 0; y < g->height; ++y) {
                size_t x;
                for (x = 0; x < g->width; ++x) {
                    int32_t v = grid_sample_i32(g, x, y);
                    sum += v < 0 ? -(int64_t)v : (int64_t)v;
                }
            }
            fprintf(stderr, "  post_xform ch%zu sum_abs=%lld\n", c, (long long)sum);
        }
    }
    st = modular_to_status(mst);
    if (st == JXL_OK && JXL_DEBUG_FLAG(library_ctx, debug_fb) &&
        frame->header.frame_type == JXL_FRAME_TYPE_LF) {
        size_t first = dest.channels.nb_meta_channels;
        size_t bx = 320;
        size_t by = 150;
        const jxl_modular_grid_i32 *gy = &dest.image_channels[first + 0];
        if (by < gy->height && bx < gy->width) {
            fprintf(stderr, "prereq_postinv ch0@%zu,%zu=%d\n", bx, by,
                    grid_sample_i32(gy, bx, by));
        }
    }
    if (st == JXL_OK) {
        int use_xyb_planes = parsed->xyb_encoded && frame->header.encoded_color_channels >= 3 &&
                             frame->header.frame_type == JXL_FRAME_TYPE_LF;
        if (use_xyb_planes) {
            st = modular_dest_to_xyb_planes(library_ctx, alloc, &dest, &dequant, out);
            if (JXL_DEBUG_FLAG(library_ctx, debug_fb) && st == JXL_OK) {
                size_t bx;
                size_t c;
                uint32_t dx = 2560u;
                uint32_t dy = 1200u;
                size_t by;
                size_t first;
                const jxl_modular_grid_i32 *gy;
                if (dy < out->height && dx < out->width && out->planes[0] != NULL) {
                    size_t idx = (size_t)dy * out->width + dx;
                    fprintf(stderr, "prereq_xyb@%u,%u x=%g y=%g b=%g (lf_level=%u)\n", dx, dy,
                            out->planes[0][idx], out->planes[1][idx], out->planes[2][idx],
                            frame->header.lf_level);
                }
                first = dest.channels.nb_meta_channels;
                gy = &dest.image_channels[first + 0];
                by = 150;
                fprintf(stderr, "prereq_grid y row@%zu:", by);
                for (bx = 316; bx <= 324 && bx < gy->width; ++bx) {
                    fprintf(stderr, " %zu:%d", bx, grid_sample_i32(gy, bx, by));
                }
                fprintf(stderr, "\n");
                for (c = 0; c < 3 && first + c < dest.image_channels_len; ++c) {
                    const jxl_modular_grid_i32 *g = &dest.image_channels[first + c];
                    bx = (c == 0) ? 320 : 160;
                    if (by < g->height && bx < g->width) {
                        fprintf(stderr, "prereq_grid ch%zu@%zu,%zu=%d\n", c, bx, by,
                                grid_sample_i32(g, bx, by));
                    }
                }
            }
            if (st == JXL_OK && frame->header.frame_type == JXL_FRAME_TYPE_LF &&
                frame->header.lf_level > 0) {
                uint32_t shift = frame->header.lf_level * 3u;
                float **up_planes = jxl_alloc(alloc, out->num_planes * sizeof(float *));
                if (up_planes == NULL) {
                    st = JXL_ERROR_OUT_OF_MEMORY;
                } else {
                    uint32_t p;
                    memset(up_planes, 0, out->num_planes * sizeof(float *));
                    for (p = 0; p < out->num_planes && st == JXL_OK; ++p) {
                        uint32_t sw =
                            out->plane_w[p] != 0 ? out->plane_w[p] : out->width;
                        uint32_t sh =
                            out->plane_h[p] != 0 ? out->plane_h[p] : out->height;
                        float *old_plane = out->planes[p];
                        st = upsample_plane_nn(alloc, old_plane, sw, sh, shift, &up_planes[p],
                                               &out->plane_w[p], &out->plane_h[p]);
                        jxl_free(alloc, old_plane);
                    }
                    if (st == JXL_OK) {
                        jxl_free(alloc, out->samples);
                        jxl_free(alloc, out->planes);
                        out->samples = NULL;
                        out->planes = up_planes;
                        out->width = out->plane_w[0];
                        out->height = out->plane_h[0];
                    } else {
                        uint32_t p;
                        for (p = 0; p < out->num_planes; ++p) {
                            jxl_free(alloc, up_planes[p]);
                        }
                        jxl_free(alloc, up_planes);
                    }
                }
            }
        } else if (parsed->xyb_encoded && frame->header.encoded_color_channels >= 3 &&
                   frame->header.frame_type == JXL_FRAME_TYPE_REFERENCE_ONLY) {
            st = modular_dest_to_xyb_planes(library_ctx, alloc, &dest, &dequant, out);
            if (st == JXL_OK && parsed->num_extra_channels > 0) {
                st = modular_dest_append_extra_planes(alloc, &dest, parsed->bit_depth_bits, 3u,
                                                      (uint32_t)parsed->num_extra_channels, out);
            }
        } else if (canvas != NULL) {
            uint32_t color_planes = frame->header.encoded_color_channels;
            jxl_modular_compose_params cp = {0};
            const jxl_lf_channel_dequant *xyb_dequant;
            if (color_planes > 3u) {
                color_planes = 3u;
            }
            if (color_planes == 0u) {
                color_planes = 1u;
            }
            xyb_dequant =
                parsed->xyb_encoded && color_planes >= 3 ? &dequant : NULL;
            cp.ctx = library_ctx;
            cp.alloc = alloc;
            cp.parsed = parsed;
            cp.fh = &frame->header;
            cp.dest = &dest;
            cp.xyb_dequant = xyb_dequant;
            cp.bit_depth = parsed->bit_depth_bits;
            cp.num_color_channels = color_planes;
            cp.num_extra_channels = (uint32_t)parsed->num_extra_channels;
            cp.output_region = NULL;
            cp.has_crop = 0;
            cp.prefer_canvas_base = 0;

            st = jxl_render_compose_modular_dest(&cp, refs, canvas);
            if (out != NULL) {
                memset(out, 0, sizeof(*out));
            }
        } else {
            uint32_t color_planes = frame->header.encoded_color_channels;
            uint32_t num_planes;
            if (color_planes > 3u) {
                color_planes = 3u;
            }
            if (color_planes == 0u) {
                color_planes = 1u;
            }
            num_planes = color_planes + (uint32_t)parsed->num_extra_channels;
            st = modular_dest_to_planes(alloc, &dest, parsed->bit_depth_bits, num_planes, out);
        }
    }

    if (st == JXL_OK && out != NULL && out->valid) {
        jxl_ref_image_set_crop_from_frame(out, &frame->header);
    }

    jxl_modular_image_destination_free(alloc, &dest);
    jxl_modular_params_free(alloc, &mod_params);
    jxl_ma_config_destroy(alloc, &global_ma);
    return st;
}

static jxl_status_t decode_vardct_frame(jxl_context *library_ctx, jxl_allocator_state *alloc,
                                        const uint8_t *input, size_t input_len,
                                        const uint8_t *codestream, size_t cs_len, jxl_bs *bs,
                                        const jxl_parsed_image_header *parsed, jxl_frame *frame,
                                        jxl_reference_store *refs, jxl_progressive_lf_store *lf_store,
                                        jxl_render *canvas, jxl_ref_image *out) {
    jxl_vardct_render_params vparams = {0};
    if (!jxl_frame_is_loading_done(frame)) {
        size_t meta_end = bs->num_read_bits / 8;
        size_t consumed;
        if (meta_end > cs_len || frame->toc.total_size > cs_len - meta_end) {
            return JXL_ERROR_INVALID_INPUT;
        }
        consumed = 0;
        jxl_frame_feed_bytes(frame, codestream + meta_end, frame->toc.total_size, &consumed);
        if (consumed != frame->toc.total_size || !jxl_frame_is_loading_done(frame)) {
            return JXL_ERROR_INVALID_INPUT;
        }
    }

    vparams.ctx = library_ctx;
    vparams.alloc = alloc;
    vparams.input = input;
    vparams.input_len = input_len;
    vparams.codestream = codestream;
    vparams.codestream_len = cs_len;
    vparams.external_refs = refs;
    vparams.external_lf_store = lf_store;

    if (JXL_DEBUG_FLAG(library_ctx, debug_prereq)) {
        fprintf(stderr, "prereq vardct frame type=%d ref=%u lf_level=%u\n",
                (int)frame->header.frame_type, frame->header.save_as_reference,
                frame->header.lf_level);
    }
    if (canvas != NULL && frame->header.frame_type != JXL_FRAME_TYPE_LF) {
        return jxl_render_compose_vardct_prereq(&vparams, parsed, frame, refs, canvas);
    }
    return jxl_render_vardct_prereq_to_ref(&vparams, parsed, frame, out);
}

jxl_status_t jxl_decode_prerequisite_frames(jxl_context *library_ctx, jxl_allocator_state *alloc,
                                            const uint8_t *input, size_t input_len,
                                            jxl_reference_store *refs,
                                            jxl_progressive_lf_store *lf_store,
                                            uint32_t target_keyframe_index,
                                            const uint8_t *codestream_in, size_t codestream_in_len) {
    size_t cs_len;
    jxl_parsed_image_header parsed;
    jxl_bs bs;
    jxl_frame frame;
    uint32_t keyframes_seen;
    int owns_codestream;
    const uint8_t *codestream;
    uint8_t *codestream_owned = NULL;
    jxl_status_t st;
    uint32_t prereq_color_planes;
    uint32_t prereq_num_planes;
    jxl_render *prereq_canvas;
    if (library_ctx == NULL || alloc == NULL || input == NULL || refs == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }

    codestream = codestream_in;
    cs_len = codestream_in_len;
    owns_codestream = 0;
    if (codestream == NULL) {
        st = jxl_collect_codestream(alloc, input, input_len, &codestream_owned, &cs_len);
        if (st != JXL_OK) {
            return st;
        }
        codestream = codestream_owned;
        owns_codestream = 1;
    }
    st = JXL_OK;

    memset(&parsed, 0, sizeof(parsed));
    jxl_bs_init(&bs, codestream, cs_len);
    if (jxl_image_header_parse(&bs, &parsed) != JXL_BS_OK ||
        jxl_image_skip_post_header(alloc, &bs, &parsed) != JXL_BS_OK) {
        if (owns_codestream) {
            jxl_free(alloc, codestream_owned);
        }
        return JXL_ERROR_INVALID_INPUT;
    }

    jxl_frame_init(&frame);

    prereq_color_planes = parsed.xyb_encoded ? 3u : 1u;
    if (parsed.colour.colour_space == JXL_COLOUR_SPACE_RGB_I) {
        prereq_color_planes = 3u;
    }
    prereq_num_planes = prereq_color_planes + (uint32_t)parsed.num_extra_channels;
    prereq_canvas = NULL;
    if (parsed.size.width > 0 && parsed.size.height > 0 && prereq_num_planes > 0) {
        prereq_canvas = jxl_render_create(alloc, prereq_num_planes, prereq_color_planes,
                                          parsed.size.width, parsed.size.height);
        if (prereq_canvas != NULL) {
            jxl_modular_region frame_region =
                jxl_modular_region_with_size(parsed.size.width, parsed.size.height);
            jxl_render_init_all_planes(prereq_canvas, &frame_region);
        }
    }

    keyframes_seen = 0;
    for (;;) {
        jxl_frame_status_t fst = jxl_frame_parse(alloc, &bs, &parsed, &frame);
        st = frame_to_status(fst);
        if (st != JXL_OK) {
            break;
        }
        if (jxl_frame_header_is_keyframe(&frame.header)) {
            size_t meta_end;
            if (keyframes_seen == target_keyframe_index) {
                jxl_frame_free(alloc, &frame);
                jxl_frame_init(&frame);
                st = JXL_OK;
                break;
            }
            meta_end = bs.num_read_bits / 8;
            if (meta_end > cs_len || frame.toc.total_size > cs_len - meta_end) {
                st = JXL_ERROR_INVALID_INPUT;
                break;
            }
            if (jxl_bs_skip_bits(&bs, frame.toc.total_size * 8) != JXL_BS_OK) {
                st = JXL_ERROR_INVALID_INPUT;
                break;
            }
            jxl_frame_free(alloc, &frame);
            jxl_frame_init(&frame);
            keyframes_seen++;
            continue;
        }

        if (frame.header.encoding == JXL_FRAME_ENCODING_MODULAR) {
            jxl_ref_image decoded = {0};
            st = jxl_decode_modular_prereq_frame(library_ctx, alloc, input, input_len, codestream,
                                                 cs_len, &bs, &parsed, &frame, refs, prereq_canvas,
                                                 &decoded);
            if (st != JXL_OK) {
                break;
            }
            {
                if (frame.header.frame_type == JXL_FRAME_TYPE_LF && lf_store != NULL) {
                    uint32_t ch;
                    jxl_progressive_lf_image *lf_out;
                    uint32_t lf_level = frame.header.lf_level;
                    if (lf_level == 0 || lf_level > 4) {
                        jxl_free(alloc, decoded.samples);
                        jxl_free(alloc, decoded.planes);
                        st = JXL_ERROR_INVALID_INPUT;
                        break;
                    }
                    lf_out = &lf_store->slots[lf_level - 1u];
                    jxl_progressive_lf_image_free(alloc, lf_out);
                    lf_out->valid = 1;
                    lf_out->width = decoded.width;
                    lf_out->height = decoded.height;
                    lf_out->frame_cs_w =
                        jxl_frame_header_color_sample_width(&frame.header);
                    lf_out->frame_cs_h =
                        jxl_frame_header_color_sample_height(&frame.header);
                    lf_out->samples[0] = decoded.samples;
                    for (ch = 0; ch < 3 && ch < decoded.num_planes; ++ch) {
                        lf_out->plane[ch] = decoded.planes[ch];
                        lf_out->stride[ch] =
                            decoded.plane_w[ch] != 0 ? decoded.plane_w[ch] : decoded.width;
                        lf_out->plane_h[ch] =
                            decoded.plane_h[ch] != 0 ? decoded.plane_h[ch] : decoded.height;
                    }
                    decoded.samples = NULL;
                    jxl_free(alloc, decoded.planes);
                    decoded.planes = NULL;
                } else if (jxl_frame_header_can_reference(&frame.header)) {
                    uint32_t slot = frame.header.save_as_reference;
                    if (slot < 4) {
                        if (refs->slots[slot].valid) {
                            jxl_ref_image_release(alloc, &decoded);
                        } else if (decoded.valid) {
                            jxl_ref_image_set_crop_from_frame(&decoded, &frame.header);
                            refs->slots[slot] = decoded;
                        } else {
                            jxl_free(alloc, decoded.samples);
                            jxl_free(alloc, decoded.planes);
                            st = JXL_ERROR_INVALID_INPUT;
                            break;
                        }
                    } else {
                        jxl_ref_image_release(alloc, &decoded);
                    }
                } else {
                    jxl_free(alloc, decoded.samples);
                    jxl_free(alloc, decoded.planes);
                }
            }
        } else if (frame.header.encoding == JXL_FRAME_ENCODING_VARDCT) {
            jxl_ref_image decoded = {0};
            st = decode_vardct_frame(library_ctx, alloc, input, input_len, codestream, cs_len, &bs,
                                     &parsed, &frame, refs, lf_store, prereq_canvas, &decoded);
            if (st != JXL_OK) {
                break;
            }
            if (frame.header.frame_type == JXL_FRAME_TYPE_LF && lf_store != NULL) {
                uint32_t ch;
                uint32_t lf_level = frame.header.lf_level;
                jxl_progressive_lf_image *lf_out;
		if (lf_level == 0 || lf_level > 4) {
                    uint32_t ch;
                    jxl_free(alloc, decoded.samples);
                    for (ch = 0; ch < decoded.num_planes; ++ch) {
                        if (decoded.planes != NULL && decoded.planes[ch] != NULL) {
                            jxl_free(alloc, decoded.planes[ch]);
                        }
                    }
                    jxl_free(alloc, decoded.planes);
                    st = JXL_ERROR_INVALID_INPUT;
                    break;
                }
                lf_out = &lf_store->slots[lf_level - 1u];
                jxl_progressive_lf_image_free(alloc, lf_out);
                lf_out->valid = 1;
                lf_out->width = decoded.width;
                lf_out->height = decoded.height;
                lf_out->frame_cs_w =
                    jxl_frame_header_color_sample_width(&frame.header);
                lf_out->frame_cs_h =
                    jxl_frame_header_color_sample_height(&frame.header);
                for (ch = 0; ch < 3 && ch < decoded.num_planes; ++ch) {
                    lf_out->plane[ch] = decoded.planes[ch];
                    lf_out->stride[ch] =
                        decoded.plane_w[ch] != 0 ? decoded.plane_w[ch] : decoded.width;
                    lf_out->plane_h[ch] =
                        decoded.plane_h[ch] != 0 ? decoded.plane_h[ch] : decoded.height;
                }
                jxl_free(alloc, decoded.planes);
                decoded.planes = NULL;
            } else if (jxl_frame_header_can_reference(&frame.header)) {
                uint32_t slot = frame.header.save_as_reference;
                if (slot < 4) {
                    if (refs->slots[slot].valid) {
                        jxl_ref_image_release(alloc, &decoded);
                    } else if (decoded.valid) {
                        jxl_ref_image_set_crop_from_frame(&decoded, &frame.header);
                        refs->slots[slot] = decoded;
                    } else {
                        uint32_t ch;
                        jxl_free(alloc, decoded.samples);
                        for (ch = 0; ch < decoded.num_planes; ++ch) {
                            if (decoded.planes != NULL && decoded.planes[ch] != NULL) {
                                jxl_free(alloc, decoded.planes[ch]);
                            }
                        }
                        jxl_free(alloc, decoded.planes);
                        st = JXL_ERROR_INVALID_INPUT;
                        break;
                    }
                } else {
                    jxl_ref_image_release(alloc, &decoded);
                }
            } else {
                uint32_t ch;
                jxl_free(alloc, decoded.samples);
                for (ch = 0; ch < decoded.num_planes; ++ch) {
                    if (decoded.planes != NULL && decoded.planes[ch] != NULL) {
                        jxl_free(alloc, decoded.planes[ch]);
                    }
                }
                jxl_free(alloc, decoded.planes);
            }
        }

        if (jxl_bs_skip_bits(&bs, (uint64_t)frame.toc.total_size * 8) != JXL_BS_OK) {
            st = JXL_ERROR_INVALID_INPUT;
            break;
        }
        jxl_frame_free(alloc, &frame);
        jxl_frame_init(&frame);
    }

    jxl_frame_free(alloc, &frame);
    if (prereq_canvas != NULL) {
        jxl_render_free(alloc, prereq_canvas);
    }
    if (owns_codestream) {
        jxl_free(alloc, codestream_owned);
    }
    return st;
}

static void resize_plane_nn(const float *src, uint32_t src_w, uint32_t src_h, uint32_t src_stride,
                            float *dst, uint32_t dst_w, uint32_t dst_h, uint32_t dst_stride) {
                                uint32_t y;
    if (src == NULL || dst == NULL || src_w == 0 || src_h == 0 || dst_w == 0 || dst_h == 0) {
        return;
    }
    if (src_stride == 0) {
        src_stride = src_w;
    }
    if (dst_stride == 0) {
        dst_stride = dst_w;
    }
    for (y = 0; y < dst_h; ++y) {
        uint32_t x;
        uint32_t sy = (uint32_t)((uint64_t)y * src_h / dst_h);
        if (sy >= src_h) {
            sy = src_h - 1u;
        }
        for (x = 0; x < dst_w; ++x) {
            uint32_t sx = (uint32_t)((uint64_t)x * src_w / dst_w);
            if (sx >= src_w) {
                sx = src_w - 1u;
            }
            dst[(size_t)y * dst_stride + x] = src[(size_t)sy * src_stride + sx];
        }
    }
}

jxl_status_t jxl_copy_lf_quant_from_progressive(jxl_allocator_state *alloc,
                                              const jxl_progressive_lf_image *lf,
                                              jxl_lf_xyb_plane lf_xyb[3],
                                              const jxl_frame_header *fh) {
                                                  size_t ch;
    uint32_t kf_w;
    uint32_t kf_h;
    if (alloc == NULL || lf == NULL || !lf->valid || fh == NULL || lf_xyb == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    kf_w = jxl_frame_header_color_sample_width(fh);
    kf_h = jxl_frame_header_color_sample_height(fh);
    if (kf_w == 0 || kf_h == 0) {
        return JXL_ERROR_INVALID_INPUT;
    }
    for (ch = 0; ch < 3; ++ch) {
        uint32_t by;
        uint32_t src_w;
        uint32_t src_h;
        jxl_channel_shift shift;
        uint32_t sample_w;
        uint32_t sample_h;
        uint32_t block_w;
        uint32_t block_h;
        float *rendered;
        const float *sample_plane;
        uint32_t sample_stride;
        if (lf->plane[ch] == NULL || lf_xyb[ch].data == NULL) {
            continue;
        }
        src_w = lf->stride[ch] != 0 ? lf->stride[ch] : lf->width;
        src_h = lf->plane_h[ch] != 0 ? lf->plane_h[ch] : lf->height;
        if (src_w == 0 || src_h == 0) {
            continue;
        }
        shift = jxl_channel_shift_from_jpeg_upsampling(fh->jpeg_upsampling, ch);
        sample_w = kf_w;
        sample_h = kf_h;
        jxl_channel_shift_shift_size(&shift, sample_w, sample_h, &sample_w, &sample_h);
        block_w = lf_xyb[ch].width;
        block_h = lf_xyb[ch].height;
        if (block_w == 0 || block_h == 0) {
            continue;
        }

        rendered = NULL;
        sample_plane = lf->plane[ch];
        sample_stride = lf->stride[ch];
        if (src_w != sample_w || src_h != sample_h) {
            size_t pixels = (size_t)sample_w * (size_t)sample_h;
            rendered = jxl_alloc(alloc, pixels * sizeof(float));
            if (rendered == NULL) {
                return JXL_ERROR_OUT_OF_MEMORY;
            }
            resize_plane_nn(lf->plane[ch], src_w, src_h, lf->stride[ch], rendered, sample_w,
                            sample_h, sample_w);
            sample_plane = rendered;
            sample_stride = sample_w;
        }

        for (by = 0; by < block_h; ++by) {
            uint32_t bx;
            uint32_t py = by * 8u;
            if (py >= sample_h) {
                py = sample_h > 0 ? sample_h - 1u : 0u;
            }
            for (bx = 0; bx < block_w; ++bx) {
                uint32_t px = bx * 8u;
                if (px >= sample_w) {
                    px = sample_w > 0 ? sample_w - 1u : 0u;
                }
                lf_xyb[ch].data[(size_t)by * lf_xyb[ch].stride + bx] =
                    sample_plane[(size_t)py * sample_stride + px];
            }
        }
        if (rendered != NULL) {
            jxl_free(alloc, rendered);
        }
    }
    return JXL_OK;
}

static uint32_t div_ceil_u32(uint32_t a, uint32_t b) {
    if (b == 0) {
        return 0;
    }
    return (a + b - 1u) / b;
}

void jxl_lf_xyb_subgrid_for_group(const jxl_lf_xyb_plane lf_xyb[3], const jxl_frame_header *fh,
                                  uint32_t group_idx, int32_t lf_region_left,
                                  int32_t lf_region_top, uint32_t lf_region_width,
                                  uint32_t lf_region_height, jxl_const_subgrid_f32 lf_out[3]) {
                                      size_t ch;
    uint32_t group_w;
    uint32_t group_h;
    uint32_t group_x;
    uint32_t group_y;
    uint32_t group_dim;
    uint32_t groups_per_row;
    int32_t lf_base_left;
    int32_t lf_base_top;
    if (lf_xyb == NULL || fh == NULL || lf_out == NULL) {
        return;
    }
    group_dim = jxl_frame_header_group_dim(fh);
    groups_per_row = jxl_frame_header_groups_per_row(fh);
    group_x = group_idx % groups_per_row;
    group_y = group_idx / groups_per_row;
    lf_base_left = (int32_t)(group_x * group_dim / 8u);
    lf_base_top = (int32_t)(group_y * group_dim / 8u);
    group_w = 0;
    group_h = 0;
    jxl_frame_header_group_size_for(fh, group_idx, &group_w, &group_h);

    for (ch = 0; ch < 3; ++ch) {
        jxl_channel_shift shift;
        int32_t base_l;
        int32_t base_t;
        uint32_t lf_w;
        uint32_t lf_h;
        uint32_t group_lf_w;
        uint32_t group_lf_h;
        size_t sx;
        size_t sy;
        uint32_t sw;
        uint32_t sh;
        if (lf_xyb[ch].data == NULL) {
            lf_out[ch] = jxl_const_subgrid_f32_from_buf(NULL, 0, 0, 0);
            continue;
        }
        shift = jxl_channel_shift_from_jpeg_upsampling(fh->jpeg_upsampling, ch);

        base_l = lf_base_left - lf_region_left;
        base_t = lf_base_top - lf_region_top;
        if (base_l < 0 || base_t < 0) {
            lf_out[ch] = jxl_const_subgrid_f32_from_buf(NULL, 0, 0, 0);
            continue;
        }
        lf_w = lf_region_width > (uint32_t)base_l ? lf_region_width - (uint32_t)base_l : 0u;
        lf_h = lf_region_height > (uint32_t)base_t ? lf_region_height - (uint32_t)base_t : 0u;
        group_lf_w = div_ceil_u32(group_w, 8u);
        group_lf_h = div_ceil_u32(group_h, 8u);
        if (lf_w > group_lf_w) {
            lf_w = group_lf_w;
        }
        if (lf_h > group_lf_h) {
            lf_h = group_lf_h;
        }
        if (lf_w == 0u || lf_h == 0u) {
            lf_out[ch] = jxl_const_subgrid_f32_from_buf(NULL, 0, 0, 0);
            continue;
        }

        sx = (size_t)base_l >> (uint32_t)jxl_channel_shift_hshift(&shift);
        sy = (size_t)base_t >> (uint32_t)jxl_channel_shift_vshift(&shift);
        sw = lf_w;
        sh = lf_h;
        jxl_channel_shift_shift_size(&shift, sw, sh, &sw, &sh);

        if (sx >= lf_xyb[ch].width || sy >= lf_xyb[ch].height || sw == 0 || sh == 0) {
            lf_out[ch] = jxl_const_subgrid_f32_from_buf(NULL, 0, 0, 0);
            continue;
        }
        if (sx + sw > lf_xyb[ch].width) {
            sw = (uint32_t)(lf_xyb[ch].width - sx);
        }
        if (sy + sh > lf_xyb[ch].height) {
            sh = (uint32_t)(lf_xyb[ch].height - sy);
        }
        lf_out[ch] = jxl_const_subgrid_f32_from_buf(lf_xyb[ch].data + sy * lf_xyb[ch].stride + sx,
                                                    sw, sh, lf_xyb[ch].stride);
    }
}

void jxl_lf_rendered_subgrid_for_group(const jxl_lf_xyb_plane lf_xyb[3],
                                       const jxl_progressive_lf_image *lf_src,
                                       const jxl_frame_header *fh, uint32_t group_idx,
                                       jxl_const_subgrid_f32 lf_out[3]) {
    uint32_t lf_w = div_ceil_u32(jxl_frame_header_color_sample_width(fh), 8u);
    uint32_t lf_h = div_ceil_u32(jxl_frame_header_color_sample_height(fh), 8u);
    jxl_lf_xyb_subgrid_for_group(lf_xyb, fh, group_idx, 0, 0, lf_w, lf_h, lf_out);
    (void)lf_src;
}

static int patch_blend_uses_alpha(jxl_patch_blend_mode mode) {
    return mode == JXL_PATCH_BLEND_BLEND_ABOVE || mode == JXL_PATCH_BLEND_BLEND_BELOW ||
           mode == JXL_PATCH_BLEND_MULADD_ABOVE || mode == JXL_PATCH_BLEND_MULADD_BELOW;
}

static const jxl_patch_blending_info *patch_blending_for_channel(
    const jxl_patch_target *target, uint32_t channel_idx, uint32_t color_planes) {
    size_t bi;
    if (target->blending == NULL || target->blending_len == 0) {
        return NULL;
    }
    if (channel_idx < color_planes) {
        return &target->blending[0];
    }
    bi = 1u + (size_t)(channel_idx - color_planes);
    if (bi >= target->blending_len) {
        return NULL;
    }
    return &target->blending[bi];
}

static uint32_t ref_plane_stride(const jxl_ref_image *ref, uint32_t p) {
    if (ref->plane_w[p] != 0) {
        return ref->plane_w[p];
    }
    return ref->width;
}

static void patch_blend_channel(jxl_patch_blend_mode mode, const jxl_patch_blending_info *bi,
                                  uint32_t channel_idx, uint32_t color_planes, int premultiplied,
                                  float *dst_plane, const float *patch_plane,
                                  const float *base_alpha_plane, const float *patch_alpha_plane,
                                  size_t dst_idx, size_t patch_idx, size_t patch_alpha_idx) {
    int clamp;
    float base;
    float patch_val;
    uint32_t alpha_ch;
    if (dst_plane == NULL || patch_plane == NULL) {
        return;
    }
    clamp = bi != NULL && bi->clamp;
    base = dst_plane[dst_idx];
    patch_val = patch_plane[patch_idx];
    alpha_ch = color_planes + (bi != NULL ? bi->alpha_channel : 0u);

    switch (mode) {
    case JXL_PATCH_BLEND_NONE:
        return;
    case JXL_PATCH_BLEND_REPLACE:
        dst_plane[dst_idx] = patch_val;
        return;
    case JXL_PATCH_BLEND_ADD:
        dst_plane[dst_idx] = base + patch_val;
        return;
    case JXL_PATCH_BLEND_MUL:
        dst_plane[dst_idx] = base * clamp01(patch_val, clamp);
        return;
    case JXL_PATCH_BLEND_BLEND_ABOVE:
    case JXL_PATCH_BLEND_BLEND_BELOW: {
        int swapped = mode == JXL_PATCH_BLEND_BLEND_BELOW;
        float base_sample;
        float new_sample;
        float base_alpha;
        float new_alpha;
        if (channel_idx == alpha_ch) {
            float b = base;
            float n = patch_val;
            if (swapped) {
                float t = b;
                b = n;
                n = t;
            }
            dst_plane[dst_idx] = b + clamp01(n, clamp) * (1.0f - b);
            return;
        }
        if (swapped) {
            base_sample = patch_val;
            new_sample = base;
            base_alpha = patch_alpha_plane != NULL ? patch_alpha_plane[patch_alpha_idx] : 0.0f;
            new_alpha = base_alpha_plane != NULL ? base_alpha_plane[dst_idx] : 0.0f;
        } else {
            base_sample = base;
            new_sample = patch_val;
            base_alpha = base_alpha_plane != NULL ? base_alpha_plane[dst_idx] : 0.0f;
            new_alpha = patch_alpha_plane != NULL ? patch_alpha_plane[patch_alpha_idx] : 0.0f;
        }
        new_alpha = clamp01(new_alpha, clamp);
        if (premultiplied) {
            dst_plane[dst_idx] = new_sample + base_sample * (1.0f - new_alpha);
        } else {
            float base_alpha_rev = 1.0f - base_alpha;
            float new_alpha_rev = 1.0f - new_alpha;
            float mixed_alpha = 1.0f - new_alpha_rev * base_alpha_rev;
            float mixed_alpha_recip = mixed_alpha > 0.0f ? 1.0f / mixed_alpha : 0.0f;
            dst_plane[dst_idx] =
                (new_alpha * new_sample + base_alpha * base_sample * new_alpha_rev) *
                mixed_alpha_recip;
        }
        return;
    }
    case JXL_PATCH_BLEND_MULADD_ABOVE:
    case JXL_PATCH_BLEND_MULADD_BELOW: {
        int swapped = mode == JXL_PATCH_BLEND_MULADD_BELOW;
        float base_sample;
        float new_sample;
        float new_alpha;
        if (channel_idx == alpha_ch) {
            if (swapped) {
                dst_plane[dst_idx] = patch_val;
            }
            return;
        }
        if (swapped) {
            base_sample = patch_val;
            new_sample = base;
            new_alpha = base_alpha_plane != NULL ? base_alpha_plane[dst_idx] : 0.0f;
        } else {
            base_sample = base;
            new_sample = patch_val;
            new_alpha = patch_alpha_plane != NULL ? patch_alpha_plane[patch_alpha_idx] : 0.0f;
        }
        dst_plane[dst_idx] = base_sample + clamp01(new_alpha, clamp) * new_sample;
        return;
    }
    default:
        dst_plane[dst_idx] = patch_val;
        return;
    }
}

jxl_status_t jxl_apply_patches(jxl_allocator_state *alloc, const jxl_parsed_image_header *image,
                               const jxl_frame_header *frame, const jxl_patches *patches,
                               const jxl_reference_store *refs, float **planes,
                               uint32_t num_planes, uint32_t width, uint32_t height,
                               const jxl_modular_region *render_region) {
                                   size_t ri;
    jxl_region base_region;
    uint32_t color_planes;
    int premultiplied;
    (void)alloc;
    if (patches == NULL || frame == NULL || refs == NULL || planes == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }

    color_planes = frame->encoded_color_channels;
    if (color_planes > 3u) {
        color_planes = 3u;
    }
    if (color_planes == 0u) {
        color_planes = 1u;
    }
    premultiplied = image != NULL && image->alpha_associated > 0;

    if (render_region != NULL) {
        jxl_region compound_tmp;
        compound_tmp.left = render_region->left;
        compound_tmp.top = render_region->top;
        compound_tmp.width = render_region->width;
        compound_tmp.height = render_region->height;

        base_region = compound_tmp;

    } else {
        jxl_region compound_tmp;
        compound_tmp.left = 0;
        compound_tmp.top = 0;
        compound_tmp.width = width;
        compound_tmp.height = height;

        base_region = compound_tmp;

    }
    for (ri = 0; ri < patches->refs_len; ++ri) {
        size_t ti;
        const jxl_ref_image *ref;
        const jxl_patch_ref *pref = &patches->refs[ri];
        if (pref->ref_idx >= 4 || !refs->slots[pref->ref_idx].valid) {
            return JXL_ERROR_INVALID_INPUT;
        }
        ref = &refs->slots[pref->ref_idx];

        for (ti = 0; ti < pref->targets_len; ++ti) {
            uint32_t y;
            jxl_region target_region;
            jxl_region intersect;
            int32_t rel_left;
            int32_t rel_top;
            const jxl_patch_target *target = &pref->targets[ti];
            target_region.left = target->x;
            target_region.top = target->y;
            target_region.width = pref->width;
            target_region.height = pref->height;

            intersect = region_intersect(base_region, target_region);
            if (intersect.width == 0 || intersect.height == 0) {
                continue;
            }

            rel_left = intersect.left - target->x;
            rel_top = intersect.top - target->y;
            for (y = 0; y < intersect.height; ++y) {
                uint32_t x;
                for (x = 0; x < intersect.width; ++x) {
                    uint32_t ch;
                    uint32_t dst_x = (uint32_t)(intersect.left - base_region.left + (int32_t)x);
                    uint32_t dst_y = (uint32_t)(intersect.top - base_region.top + (int32_t)y);
                    uint32_t ref_x = pref->x0 + (uint32_t)rel_left + x;
                    uint32_t ref_y = pref->y0 + (uint32_t)rel_top + y;
                    size_t dst_idx;
                    if (ref_x >= ref->width || ref_y >= ref->height) {
                        continue;
                    }
                    dst_idx = (size_t)dst_y * width + dst_x;

                    for (ch = 0; ch < num_planes; ++ch) {
                        size_t patch_alpha_idx;
                        const jxl_patch_blending_info *bi =
                            patch_blending_for_channel(target, ch, color_planes);
                        jxl_patch_blend_mode mode;
                        uint32_t ref_stride;
                        size_t ref_idx;
                        const float *base_alpha_plane = NULL;
                        const float *patch_alpha_plane = NULL;
                        if (bi == NULL) {
                            continue;
                        }
                        mode = bi->mode;
                        if (mode == JXL_PATCH_BLEND_NONE) {
                            continue;
                        }
                        if (ch >= ref->num_planes || ref->planes[ch] == NULL ||
                            planes[ch] == NULL) {
                            continue;
                        }
                        ref_stride = ref_plane_stride(ref, ch);
                        ref_idx = (size_t)ref_y * ref_stride + ref_x;

                        patch_alpha_idx = ref_idx;
                        if (patch_blend_uses_alpha(mode)) {
                            uint32_t alpha_ch = color_planes + bi->alpha_channel;
                            if (alpha_ch < num_planes && planes[alpha_ch] != NULL) {
                                base_alpha_plane = planes[alpha_ch];
                            }
                            if (alpha_ch < ref->num_planes && ref->planes[alpha_ch] != NULL) {
                                uint32_t alpha_stride = ref_plane_stride(ref, alpha_ch);
                                patch_alpha_idx = (size_t)ref_y * alpha_stride + ref_x;
                                patch_alpha_plane = ref->planes[alpha_ch];
                            }
                        }

                        patch_blend_channel(mode, bi, ch, color_planes, premultiplied,
                                            planes[ch], ref->planes[ch], base_alpha_plane,
                                            patch_alpha_plane, dst_idx, ref_idx, patch_alpha_idx);
                    }
                }
            }
        }
    }
    return JXL_OK;
}
