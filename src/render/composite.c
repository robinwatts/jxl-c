// SPDX-License-Identifier: MIT OR Apache-2.0
#include "composite.h"

#include "frame/frame_header.h"
#include "render/blend.h"
#include "render/render_buffer.h"
#include "render/render_frame.h"
#include "render/render_util.h"

#include <string.h>

int jxl_render_composite_preprocess(jxl_context *ctx, jxl_allocator_state *alloc,
                                    const jxl_parsed_image_header *parsed,
                                    const jxl_frame_header *fh, jxl_render *r) {
    int skip_blending;
    if (alloc == NULL || parsed == NULL || fh == NULL || r == NULL) {
        return -1;
    }

    skip_blending =
        !jxl_frame_header_is_normal_frame(fh) || fh->resets_canvas;

    if (jxl_frame_header_can_reference(fh)) {
        if (!r->ct_done && !(fh->save_before_ct || (skip_blending && fh->is_last))) {
            if (jxl_render_any_plane_integer(r)) {
                if (jxl_render_convert_modular_color(alloc, r, parsed->bit_depth_bits,
                                                     r->color_planes) != JXL_OK) {
                    return -1;
                }
            }
            if (jxl_render_convert_color_for_record(ctx, alloc, parsed, fh, r, 0) != JXL_OK) {
                return -1;
            }
        }
    }

    return skip_blending;
}

void jxl_ref_image_release(jxl_allocator_state *alloc, jxl_ref_image *img) {
    if (alloc == NULL || img == NULL || !img->valid) {
        return;
    }
    jxl_free(alloc, img->samples);
    if (img->planes != NULL) {
        uint32_t p;
        for (p = 0; p < img->num_planes; ++p) {
            if (img->planes[p] != NULL && (img->samples == NULL || img->planes[p] != img->samples)) {
                jxl_free(alloc, img->planes[p]);
            }
        }
        jxl_free(alloc, img->planes);
    }
    memset(img, 0, sizeof(*img));
}

jxl_status_t jxl_ref_image_from_canvas(jxl_allocator_state *alloc, const jxl_frame_header *fh,
                                       const jxl_render *canvas, jxl_ref_image *out) {
                                           uint32_t p;
                                           uint32_t y;
    uint32_t fw;
    uint32_t fh_h;
    uint32_t np;
    size_t plane_pixels;
    float **planes;
    int32_t ox;
    int32_t oy;
    if (alloc == NULL || fh == NULL || canvas == NULL || out == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    jxl_ref_image_release(alloc, out);

    fw = jxl_frame_header_color_sample_width(fh);
    fh_h = jxl_frame_header_color_sample_height(fh);
    np = canvas->num_planes;
    if (np == 0) {
        return JXL_ERROR_INVALID_INPUT;
    }
    plane_pixels = (size_t)fw * (size_t)fh_h;
    if (plane_pixels == 0) {
        return JXL_ERROR_INVALID_INPUT;
    }

    planes = jxl_alloc(alloc, np * sizeof(float *));
    if (planes == NULL) {
        return JXL_ERROR_OUT_OF_MEMORY;
    }
    memset(planes, 0, np * sizeof(float *));
    for (p = 0; p < np; ++p) {
        planes[p] = jxl_alloc(alloc, plane_pixels * sizeof(float));
        if (planes[p] == NULL) {
            uint32_t k;
            for (k = 0; k < p; ++k) {
                jxl_free(alloc, planes[k]);
            }
            jxl_free(alloc, planes);
            return JXL_ERROR_OUT_OF_MEMORY;
        }
        memset(planes[p], 0, plane_pixels * sizeof(float));
    }

    ox = fh->have_crop ? fh->x0 : 0;
    oy = fh->have_crop ? fh->y0 : 0;
    for (y = 0; y < fh_h; ++y) {
        uint32_t x;
        for (x = 0; x < fw; ++x) {
            uint32_t p;
            int32_t cx = ox + (int32_t)x;
            int32_t cy = oy + (int32_t)y;
            size_t cidx;
            size_t fidx;
            if (cx < 0 || cy < 0 || (uint32_t)cx >= canvas->width || (uint32_t)cy >= canvas->height) {
                continue;
            }
            cidx = (size_t)cy * canvas->width + (size_t)cx;
            fidx = (size_t)y * fw + (size_t)x;
            for (p = 0; p < np; ++p) {
                if (canvas->planes[p] != NULL) {
                    planes[p][fidx] = canvas->planes[p][cidx];
                }
            }
        }
    }

    out->valid = 1;
    out->width = fw;
    out->height = fh_h;
    out->num_planes = np;
    out->have_crop = fh->have_crop;
    out->x0 = fh->have_crop ? fh->x0 : 0;
    out->y0 = fh->have_crop ? fh->y0 : 0;
    out->samples = NULL;
    out->planes = planes;
    for (p = 0; p < 3; ++p) {
        out->plane_w[p] = p < np ? fw : 0;
        out->plane_h[p] = p < np ? fh_h : 0;
    }
    return JXL_OK;
}

static const jxl_blending_info *blending_for_plane(const jxl_frame_header *fh, uint32_t p,
                                                   uint32_t num_color) {
    size_t ec;
    if (p < num_color) {
        return &fh->blending_info;
    }
    ec = (size_t)p - num_color;
    if (fh->ec_blending_info != NULL && ec < fh->ec_blending_info_len) {
        return &fh->ec_blending_info[ec];
    }
    return &fh->blending_info;
}

static float sample_ref_plane(const jxl_ref_image *ref, uint32_t plane, int32_t x, int32_t y) {
    uint32_t rw, rh;
    if (ref == NULL || !ref->valid || plane >= ref->num_planes || ref->planes == NULL ||
        ref->planes[plane] == NULL) {
        return 0.0f;
    }
    rw = ref->width;
    rh = ref->height;
    if (plane < 3u) {
        if (ref->plane_w[plane] != 0) {
            rw = ref->plane_w[plane];
        }
        if (ref->plane_h[plane] != 0) {
            rh = ref->plane_h[plane];
        }
    }
    if (x < 0 || y < 0 || (uint32_t)x >= rw || (uint32_t)y >= rh) {
        return 0.0f;
    }
    return ref->planes[plane][(size_t)y * rw + (size_t)x];
}

static float sample_ref_at_image(const jxl_ref_image *ref, uint32_t plane, int32_t image_x,
                                 int32_t image_y) {
    int32_t ox, oy;
    if (ref == NULL || !ref->valid) {
        return 0.0f;
    }
    ox = ref->have_crop ? ref->x0 : 0;
    oy = ref->have_crop ? ref->y0 : 0;
    return sample_ref_plane(ref, plane, image_x - ox, image_y - oy);
}

static float sample_canvas_plane(const jxl_render *canvas, uint32_t plane, int32_t canvas_x,
                                 int32_t canvas_y) {
    if (canvas == NULL || plane >= canvas->num_planes || canvas->planes[plane] == NULL ||
        canvas_x < 0 || canvas_y < 0 || (uint32_t)canvas_x >= canvas->width ||
        (uint32_t)canvas_y >= canvas->height) {
        return 0.0f;
    }
    return canvas->planes[plane][(size_t)canvas_y * canvas->width + (size_t)canvas_x];
}

static float sample_base_for_blend(const jxl_ref_image *ref, const jxl_render *canvas,
                                   uint32_t plane, int32_t image_x, int32_t image_y,
                                   int32_t canvas_x, int32_t canvas_y, int prefer_canvas_base) {
    if (prefer_canvas_base) {
        return sample_canvas_plane(canvas, plane, canvas_x, canvas_y);
    }
    if (ref != NULL && ref->valid) {
        return sample_ref_at_image(ref, plane, image_x, image_y);
    }
    return sample_canvas_plane(canvas, plane, canvas_x, canvas_y);
}

static int blend_premultiplied(const jxl_parsed_image_header *parsed, uint32_t alpha_channel) {
    if (parsed == NULL || parsed->alpha_associated < 0) {
        return 0;
    }
    (void)alpha_channel;
    return parsed->alpha_associated > 0;
}

static float sample_new_plane(const jxl_render *new_frame, uint32_t plane, uint32_t x, uint32_t y) {
    if (new_frame == NULL || plane >= new_frame->num_planes || new_frame->planes[plane] == NULL) {
        return 0.0f;
    }
    if (x >= new_frame->width || y >= new_frame->height) {
        return 0.0f;
    }
    return new_frame->planes[plane][(size_t)y * new_frame->width + (size_t)x];
}

jxl_status_t jxl_render_composite(const jxl_render_composite_params *params) {
    uint32_t p;
    const jxl_frame_header *fh;
    const jxl_parsed_image_header *parsed;
    jxl_modular_region oriented;
    jxl_modular_region output_frame_region;
    int32_t x0;
    int32_t y0;
    uint32_t num_color;
    int has_extra;
    jxl_modular_region full_frame;
    if (params == NULL || params->alloc == NULL || params->parsed == NULL || params->fh == NULL ||
        params->new_frame == NULL || params->canvas == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }

    if (jxl_render_any_plane_integer(params->canvas)) {
        jxl_status_t st =
            jxl_render_ensure_all_planes_f32(params->alloc, params->canvas, params->parsed);
        if (st != JXL_OK) {
            return st;
        }
    }
    if (jxl_render_any_plane_integer(params->new_frame)) {
        jxl_status_t st =
            jxl_render_ensure_all_planes_f32(params->alloc, params->new_frame, params->parsed);
        if (st != JXL_OK) {
            return st;
        }
    }

    fh = params->fh;
    parsed = params->parsed;
    oriented = params->oriented_image_region;
    if (oriented.width == 0 || oriented.height == 0) {
        oriented = jxl_modular_region_with_size(parsed->size.width, parsed->size.height);
    }

    output_frame_region =
        jxl_render_composite_frame_region(parsed, fh, oriented);
    if (output_frame_region.width == 0 || output_frame_region.height == 0) {
        return JXL_OK;
    }

    x0 = fh->have_crop ? fh->x0 : 0;
    y0 = fh->have_crop ? fh->y0 : 0;
    num_color = params->num_color_channels;
    has_extra = params->num_extra_channels > 0;

    full_frame = jxl_modular_region_with_size(fh->width, fh->height);

    for (p = 0; p < params->canvas->num_planes; ++p) {
        uint32_t fy;
        jxl_blend_params bp = {0};
        const jxl_blending_info *blend_info;
        uint32_t ref_idx;
        const jxl_ref_image *ref;
        uint32_t alpha_plane;
        int is_alpha_channel;
        int is_muladd_alpha_skip;
        const jxl_render_plane_meta *pm;
        jxl_modular_region plane_orig;
        jxl_modular_region original_frame_region;
        jxl_modular_region clipped;

        if (params->canvas->planes[p] == NULL) {
            continue;
        }
        blend_info = blending_for_plane(fh, p, num_color);
        ref_idx = blend_info->source;
        ref = NULL;
        if (params->refs != NULL && ref_idx < 4 && params->refs->slots[ref_idx].valid) {
            ref = &params->refs->slots[ref_idx];
        }

        bp.mode = fh->resets_canvas ? JXL_BLEND_REPLACE : blend_info->mode;
        bp.clamp = blend_info->clamp;
        bp.alpha_channel = blend_info->alpha_channel;
        bp.premultiplied = blend_premultiplied(parsed, blend_info->alpha_channel);


        alpha_plane = num_color + blend_info->alpha_channel;
        is_alpha_channel = has_extra && bp.mode == JXL_BLEND_BLEND && p == alpha_plane;
        is_muladd_alpha_skip =
            has_extra && bp.mode == JXL_BLEND_MUL_ADD && p == alpha_plane;

        pm = jxl_render_get_plane_meta(params->new_frame, p);
        plane_orig = full_frame;
        if (pm != NULL && pm->region.width > 0 && pm->region.height > 0) {
            plane_orig = pm->region;
        }
        original_frame_region =
            jxl_modular_region_intersection(plane_orig, full_frame);
        clipped =
            jxl_modular_region_intersection(original_frame_region, output_frame_region);
        if (clipped.width == 0 || clipped.height == 0) {
            continue;
        }

        for (fy = 0; fy < clipped.height; ++fy) {
            uint32_t fx;
            for (fx = 0; fx < clipped.width; ++fx) {
                int32_t frame_x = clipped.left + (int32_t)fx;
                int32_t frame_y = clipped.top + (int32_t)fy;
                float base_alpha;
                float new_alpha;
                int32_t image_x;
                int32_t image_y;
                int32_t canvas_x;
                int32_t canvas_y;
                uint32_t nx;
                uint32_t ny;
                float new_sample;
                float base_sample;
                size_t cidx;

                if (frame_x < 0 || frame_y < 0) {
                    continue;
                }
                image_x = x0 + frame_x;
                image_y = y0 + frame_y;
                canvas_x = image_x - oriented.left;
                canvas_y = image_y - oriented.top;
                if (canvas_x < 0 || canvas_y < 0 || (uint32_t)canvas_x >= params->canvas->width ||
                    (uint32_t)canvas_y >= params->canvas->height) {
                    continue;
                }

                nx = (uint32_t)frame_x;
                ny = (uint32_t)frame_y;
                new_sample = sample_new_plane(params->new_frame, p, nx, ny);
                base_sample = sample_base_for_blend(ref, params->canvas, p, image_x,
                                                          image_y, canvas_x, canvas_y,
                                                          params->prefer_canvas_base);

                cidx = (size_t)canvas_y * params->canvas->width + (size_t)canvas_x;

                if (bp.mode == JXL_BLEND_MUL_ADD && is_muladd_alpha_skip) {
                    continue;
                }
                if (bp.mode == JXL_BLEND_BLEND && is_alpha_channel) {
                    float mixed = base_sample +
                                  jxl_blend_clamp01(new_sample, bp.clamp) * (1.0f - base_sample);
                    params->canvas->planes[p][cidx] = mixed;
                    continue;
                }

                base_alpha = 0.0f;
                new_alpha = 0.0f;
                if (has_extra && (bp.mode == JXL_BLEND_BLEND || bp.mode == JXL_BLEND_MUL_ADD) &&
                    !is_alpha_channel) {
                    base_alpha = sample_base_for_blend(ref, params->canvas, alpha_plane, image_x,
                                                       image_y, canvas_x, canvas_y,
                                                       params->prefer_canvas_base);
                    new_alpha = jxl_blend_clamp01(
                        sample_new_plane(params->new_frame, alpha_plane, nx, ny), bp.clamp);
                }

                params->canvas->planes[p][cidx] =
                    jxl_blend_samples(base_sample, new_sample, base_alpha, new_alpha, &bp);
            }
        }
    }

    /* Rust blend.rs: output_grid.set_ct_done(new_grid.ct_done()). */
    if (params->new_frame->ct_done) {
        params->canvas->ct_done = 1;
    }

    if (jxl_frame_header_can_reference(fh) && params->refs != NULL) {
        uint32_t save_slot;
        if (params->parsed != NULL && jxl_render_any_plane_integer(params->canvas)) {
            jxl_status_t st =
                jxl_render_ensure_all_planes_f32(params->alloc, params->canvas, params->parsed);
            if (st != JXL_OK) {
                return st;
            }
        }
        save_slot = fh->save_as_reference;
        if (save_slot < 4) {
            if (!jxl_frame_header_is_keyframe(fh)) {
                jxl_ref_image_release(params->alloc, &params->refs->slots[save_slot]);
            }
            return jxl_ref_image_from_canvas(params->alloc, fh, params->canvas,
                                             &params->refs->slots[save_slot]);
        }
    }

    return JXL_OK;
}
