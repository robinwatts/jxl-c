// SPDX-License-Identifier: MIT OR Apache-2.0
#include "modular_compose.h"

#include "render/blend.h"
#include "render/composite.h"
#include "render/image_buffer.h"
#include "render/modular_sample.h"
#include "render/simd/features.h"

#include "modular/transform/transform.h"

typedef struct {
    uint32_t src_x0;
    uint32_t src_y0;
    int32_t dst_x0;
    int32_t dst_y0;
    uint32_t blit_w;
    uint32_t blit_h;
} jxl_modular_blit_layout;

static jxl_status_t modular_blit_layout(const jxl_modular_compose_params *params,
                                        const jxl_frame_header *fh, uint32_t canvas_w,
                                        uint32_t canvas_h, jxl_modular_blit_layout *out) {
    uint32_t fw;
    uint32_t fh_h;
    int32_t ox;
    int32_t oy;
    if (params == NULL || fh == NULL || out == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    fw = fh->width;
    fh_h = fh->height;
    ox = fh->have_crop ? fh->x0 : 0;
    oy = fh->have_crop ? fh->y0 : 0;

    out->src_x0 = 0;
    out->src_y0 = 0;
    out->dst_x0 = ox;
    out->dst_y0 = oy;
    out->blit_w = fw;
    out->blit_h = fh_h;

    if (params->output_region != NULL) {
        int32_t dst_left = 0;
        int32_t dst_top = 0;
        const jxl_modular_region *reg = params->output_region;
        int32_t src_left = reg->left - ox;
        int32_t src_top = reg->top - oy;
        uint32_t blit_w = reg->width;
        uint32_t blit_h = reg->height;

        if (src_left < 0) {
            dst_left = -src_left;
            blit_w -= (uint32_t)(-src_left);
            src_left = 0;
        }
        if (src_top < 0) {
            dst_top = -src_top;
            blit_h -= (uint32_t)(-src_top);
            src_top = 0;
        }
        if (blit_w == 0 || blit_h == 0) {
            out->blit_w = 0;
            out->blit_h = 0;
            return JXL_OK;
        }
        if ((uint32_t)src_left >= fw || (uint32_t)src_top >= fh_h) {
            out->blit_w = 0;
            out->blit_h = 0;
            return JXL_OK;
        }
        if ((uint32_t)src_left + blit_w > fw) {
            blit_w = fw - (uint32_t)src_left;
        }
        if ((uint32_t)src_top + blit_h > fh_h) {
            blit_h = fh_h - (uint32_t)src_top;
        }
        if ((uint32_t)dst_left + blit_w > canvas_w) {
            if ((uint32_t)dst_left >= canvas_w) {
                blit_w = 0;
            } else {
                blit_w = canvas_w - (uint32_t)dst_left;
            }
        }
        if ((uint32_t)dst_top + blit_h > canvas_h) {
            if ((uint32_t)dst_top >= canvas_h) {
                blit_h = 0;
            } else {
                blit_h = canvas_h - (uint32_t)dst_top;
            }
        }

        out->src_x0 = (uint32_t)src_left;
        out->src_y0 = (uint32_t)src_top;
        out->dst_x0 = dst_left;
        out->dst_y0 = dst_top;
        out->blit_w = blit_w;
        out->blit_h = blit_h;
        return JXL_OK;
    }

    if (params->has_crop) {
        out->src_x0 = params->crop.left;
        out->src_y0 = params->crop.top;
        out->blit_w = params->crop.width;
        out->blit_h = params->crop.height;
        out->dst_x0 = 0;
        out->dst_y0 = 0;
        if (out->src_x0 + out->blit_w > fw) {
            out->blit_w = out->src_x0 < fw ? fw - out->src_x0 : 0;
        }
        if (out->src_y0 + out->blit_h > fh_h) {
            out->blit_h = out->src_y0 < fh_h ? fh_h - out->src_y0 : 0;
        }
    } else if (ox < 0) {
        out->src_x0 = (uint32_t)(-ox);
        if (out->src_x0 >= fw) {
            return JXL_OK;
        }
        out->blit_w = fw - out->src_x0;
        out->dst_x0 = 0;
    }
    if (oy < 0) {
        out->src_y0 = (uint32_t)(-oy);
        if (out->src_y0 >= fh_h) {
            return JXL_OK;
        }
        out->blit_h = fh_h - out->src_y0;
        out->dst_y0 = 0;
    }
    if (!params->has_crop && (out->dst_x0 < 0 || out->dst_y0 < 0)) {
        return JXL_ERROR_UNSUPPORTED;
    }
    if (!params->has_crop) {
        if ((uint32_t)out->dst_x0 + out->blit_w > canvas_w) {
            if ((uint32_t)out->dst_x0 >= canvas_w) {
                out->blit_w = 0;
            } else {
                out->blit_w = canvas_w - (uint32_t)out->dst_x0;
            }
        }
        if ((uint32_t)out->dst_y0 + out->blit_h > canvas_h) {
            if ((uint32_t)out->dst_y0 >= canvas_h) {
                out->blit_h = 0;
            } else {
                out->blit_h = canvas_h - (uint32_t)out->dst_y0;
            }
        }
    } else {
        if (out->blit_w > canvas_w) {
            out->blit_w = canvas_w;
        }
        if (out->blit_h > canvas_h) {
            out->blit_h = canvas_h;
        }
    }
    return JXL_OK;
}

static float modular_bit_depth_scale(uint32_t bit_depth) {
    if (bit_depth >= 31) {
        return 1.0f;
    }
    return 1.0f / (float)((1u << bit_depth) - 1u);
}

int jxl_render_prepare_modular_float_export(const jxl_modular_compose_params *params,
                                            jxl_modular_image_destination *dest,
                                            jxl_render *canvas) {
    jxl_modular_blit_layout layout;
    uint32_t p;
    const jxl_modular_header *hdr;
    const jxl_transform_info *first_tr;

    jxl_modular_float_export_ctx_init(&dest->float_export);
    if (params == NULL || dest == NULL || canvas == NULL || params->fh == NULL ||
        params->xyb_dequant != NULL || dest->sample_kind != JXL_MODULAR_SAMPLE_I16 ||
        params->bit_depth >= 31u) {
        return 0;
    }

    {
        int composite_skip =
            jxl_render_composite_preprocess(params->ctx, params->alloc, params->parsed, params->fh, canvas);
        if (composite_skip <= 0) {
            return 0;
        }
    }

    if (modular_blit_layout(params, params->fh, canvas->width, canvas->height, &layout) !=
            JXL_OK ||
        layout.blit_w == 0 || layout.blit_h == 0) {
        return 0;
    }

    hdr = &dest->header;
    if (hdr->transform_len == 0 || hdr->transform == NULL) {
        return 0;
    }
    first_tr = &hdr->transform[0];
    if (first_tr->kind != JXL_TRANSFORM_KIND_RCT) {
        return 0;
    }
    if (first_tr->u.rct.begin_c != dest->channels.nb_meta_channels) {
        return 0;
    }
    if ((size_t)first_tr->u.rct.begin_c + 3u > dest->channels.info_len) {
        return 0;
    }

    dest->float_export.active = 1;
    dest->float_export.cpu = jxl_context_cpu_features(params->ctx);
    dest->float_export.scale = modular_bit_depth_scale(params->bit_depth);
    dest->float_export.src_x0 = layout.src_x0;
    dest->float_export.src_y0 = layout.src_y0;
    dest->float_export.dst_x0 = (uint32_t)layout.dst_x0;
    dest->float_export.dst_y0 = (uint32_t)layout.dst_y0;
    dest->float_export.blit_w = layout.blit_w;
    dest->float_export.blit_h = layout.blit_h;
    dest->float_export.canvas_stride = canvas->width;
    dest->float_export.first_plane = dest->channels.nb_meta_channels;
    dest->float_export.num_color_planes = params->fh->encoded_color_channels;
    if (dest->float_export.num_color_planes == 0u) {
        dest->float_export.num_color_planes = params->num_color_channels;
    }
    if (dest->float_export.num_color_planes > 3u) {
        dest->float_export.num_color_planes = 3u;
    }
    for (p = 0; p < dest->float_export.num_color_planes; ++p) {
        if (p >= canvas->num_planes || canvas->planes[p] == NULL) {
            dest->float_export.active = 0;
            jxl_modular_float_export_ctx_init(&dest->float_export);
            return 0;
        }
        dest->float_export.planes[p] = canvas->planes[p];
    }
    dest->float_export.rct.enabled = 1;
    dest->float_export.rct.begin_c = first_tr->u.rct.begin_c;
    return 1;
}

static void modular_extend_plane_meta(jxl_render *canvas, uint32_t plane,
                                      const jxl_modular_channel_info *info, int32_t ox,
                                      int32_t oy, const jxl_modular_blit_layout *layout) {
    jxl_modular_region region;
    uint32_t ow = info->original_width != 0 ? info->original_width : info->width;
    uint32_t oh = info->original_height != 0 ? info->original_height : info->height;
    uint32_t meta_w = layout->blit_w;
    uint32_t meta_h = layout->blit_h;

    region.left = ox;
    region.top = oy;
    region.width = ow;
    region.height = oh;
    jxl_render_set_plane_meta(canvas, plane, &region, &info->original_shift, meta_w, meta_h);
    canvas->meta[plane].sample_x =
        layout->dst_x0 >= 0 ? (uint32_t)layout->dst_x0 : 0u;
    canvas->meta[plane].sample_y =
        layout->dst_y0 >= 0 ? (uint32_t)layout->dst_y0 : 0u;
    canvas->meta[plane].grid_x = layout->src_x0;
    canvas->meta[plane].grid_y = layout->src_y0;
}

static jxl_status_t modular_dest_blit_to_canvas(const jxl_modular_compose_params *params,
                                                jxl_modular_image_destination *dest,
                                                const jxl_frame_header *fh, jxl_render *canvas,
                                                const jxl_modular_blit_layout *layout) {
    size_t first_plane = dest->channels.nb_meta_channels;
    uint32_t p;
    int32_t ox = fh->have_crop ? fh->x0 : 0;
    int32_t oy = fh->have_crop ? fh->y0 : 0;

    if (layout->blit_w == 0 || layout->blit_h == 0) {
        return JXL_OK;
    }

    if (dest->float_export.active && dest->float_export.color_exported &&
        dest->float_export.num_color_planes >= canvas->num_planes &&
        params->num_extra_channels == 0) {
        for (p = 0; p < canvas->num_planes; ++p) {
            const jxl_modular_channel_info *info;

            if (first_plane + p >= dest->image_channels_len || canvas->planes[p] == NULL) {
                break;
            }
            info = &dest->channels.info[first_plane + p];
            modular_extend_plane_meta(canvas, p, info, ox, oy, layout);
            jxl_image_buffer_bind_f32(params->alloc, &canvas->bufs[p], canvas->planes[p]);
        }
        return JXL_OK;
    }

    for (p = 0; p < canvas->num_planes; ++p) {
        size_t ch = first_plane + p;
        size_t q;
        void *buf;

        if (ch >= dest->image_channels_len || canvas->planes[p] == NULL) {
            break;
        }
        buf = dest->image_channels[ch].buf;
        for (q = 0; q < p && buf != NULL; ++q) {
            size_t ch2 = first_plane + q;
            if (ch2 < dest->image_channels_len && dest->image_channels[ch2].buf == buf) {
                if (!jxl_modular_grid_clone(params->alloc, &dest->image_channels[ch2],
                                            &dest->image_channels[ch])) {
                    return JXL_ERROR_OUT_OF_MEMORY;
                }
                break;
            }
        }
    }

    for (p = 0; p < canvas->num_planes; ++p) {
        size_t ch;
        const jxl_modular_channel_info *info;

        if (first_plane + p >= dest->image_channels_len || canvas->planes[p] == NULL) {
            break;
        }
        info = &dest->channels.info[first_plane + p];
        ch = first_plane + p;

        if (dest->float_export.active && dest->float_export.color_exported &&
            p < dest->float_export.num_color_planes && params->num_extra_channels == 0) {
            modular_extend_plane_meta(canvas, p, info, ox, oy, layout);
            jxl_image_buffer_bind_f32(params->alloc, &canvas->bufs[p], canvas->planes[p]);
            continue;
        }

        jxl_image_buffer_take_grid(params->alloc, &canvas->bufs[p], &dest->image_channels[ch]);
        modular_extend_plane_meta(canvas, p, info, ox, oy, layout);
    }

    return JXL_OK;
}

static jxl_status_t modular_dest_to_local_render(const jxl_modular_compose_params *params,
                                                 const jxl_modular_image_destination *dest,
                                                 const jxl_frame_header *fh,
                                                 const jxl_lf_channel_dequant *xyb_dequant,
                                                 jxl_render *local) {
                                                     uint32_t p;
    /* All grid reads: jxl_modular_sample_color_float or jxl_modular_blit_channel_to_plane. */
    size_t first_plane = dest->channels.nb_meta_channels;
    if (first_plane + local->num_planes > dest->image_channels_len) {
        return JXL_ERROR_UNSUPPORTED;
    }
    for (p = 0; p < local->num_planes; ++p) {
        uint32_t gw;
        uint32_t gh;
        const jxl_modular_channel_info *info = &dest->channels.info[first_plane + p];
        const jxl_modular_grid_i32 *grid = &dest->image_channels[first_plane + p];
        gw = 0;
        gh = 0;
        if (xyb_dequant != NULL && p < 3u) {
            uint32_t y;
            for (y = 0; y < fh->height; ++y) {
                uint32_t x;
                for (x = 0; x < fh->width; ++x) {
                    size_t idx = (size_t)y * local->width + (size_t)x;
                    local->planes[p][idx] = jxl_modular_sample_color_float(
                        dest, first_plane, p, xyb_dequant, params->parsed, params->bit_depth, x, y);
                }
            }
            gw = fh->width;
            gh = fh->height;
        } else if (!jxl_modular_blit_channel_to_plane(grid, info, params->bit_depth, local->width,
                                                     local->planes[p], &gw, &gh)) {
            return JXL_ERROR_UNSUPPORTED;
        }
        {
            int32_t ox = fh->have_crop ? fh->x0 : 0;
            int32_t oy = fh->have_crop ? fh->y0 : 0;
            uint32_t ow = info->original_width != 0 ? info->original_width : info->width;
            uint32_t oh = info->original_height != 0 ? info->original_height : info->height;
            jxl_modular_region region;
            region.left = ox;
            region.top = oy;
            region.width = ow;
            region.height = oh;

            jxl_render_set_plane_meta(local, p, &region, &info->original_shift, gw, gh);
        }
    }
    return JXL_OK;
}

static jxl_status_t modular_local_blit_to_render(const jxl_modular_compose_params *params,
                                                 const jxl_modular_image_destination *dest,
                                                 const jxl_frame_header *fh, jxl_render *local,
                                                 jxl_render *canvas) {
                                                     uint32_t p;
    jxl_modular_blit_layout layout;
    jxl_status_t st = modular_blit_layout(params, fh, canvas->width, canvas->height, &layout);
    int32_t ox;
    int32_t oy;
    size_t first_plane;
    if (st != JXL_OK) {
        return st;
    }
    if (layout.blit_w == 0 || layout.blit_h == 0) {
        return JXL_OK;
    }
    if (params->parsed != NULL && jxl_render_any_plane_integer(canvas)) {
        st = jxl_render_ensure_all_planes_f32(params->alloc, canvas, params->parsed);
        if (st != JXL_OK) {
            return st;
        }
    }

    ox = fh->have_crop ? fh->x0 : 0;
    oy = fh->have_crop ? fh->y0 : 0;
    first_plane = dest != NULL ? dest->channels.nb_meta_channels : 0;

    for (p = 0; p < canvas->num_planes && p < local->num_planes; ++p) {
        uint32_t y;
        for (y = 0; y < layout.blit_h; ++y) {
            uint32_t x;
            for (x = 0; x < layout.blit_w; ++x) {
                size_t dst = (size_t)(layout.dst_y0 + (int32_t)y) * canvas->width +
                             (size_t)(layout.dst_x0 + (int32_t)x);
                uint32_t sx = layout.src_x0 + x;
                uint32_t sy = layout.src_y0 + y;
                size_t src;
                if (sx >= local->width || sy >= local->height) {
                    continue;
                }
                src = (size_t)sy * local->width + (size_t)sx;
                canvas->planes[p][dst] = local->planes[p][src];
            }
        }
        if (dest != NULL && first_plane + p < dest->image_channels_len) {
            jxl_modular_region region;
            const jxl_modular_channel_info *info = &dest->channels.info[first_plane + p];
            uint32_t ow = info->original_width != 0 ? info->original_width : info->width;
            uint32_t oh = info->original_height != 0 ? info->original_height : info->height;
            region.left = ox;
            region.top = oy;
            region.width = ow;
            region.height = oh;

            jxl_render_set_plane_meta(canvas, p, &region, &info->original_shift, layout.blit_w,
                                      layout.blit_h);
        }
    }
    canvas->ct_done = local->ct_done;
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

static jxl_status_t modular_local_blend_to_render(const jxl_modular_compose_params *params,
                                                  const jxl_modular_image_destination *dest,
                                                  const jxl_parsed_image_header *parsed,
                                                  const jxl_frame_header *fh, jxl_render *local,
                                                  jxl_render *canvas) {
                                                      uint32_t p;
    jxl_modular_blit_layout layout;
    jxl_status_t st = modular_blit_layout(params, fh, canvas->width, canvas->height, &layout);
    int32_t ox;
    int32_t oy;
    uint32_t num_color;
    int has_extra;
    size_t first_plane;
    if (st != JXL_OK) {
        return st;
    }
    if (layout.blit_w == 0 || layout.blit_h == 0) {
        return JXL_OK;
    }

    ox = fh->have_crop ? fh->x0 : 0;
    oy = fh->have_crop ? fh->y0 : 0;
    num_color = params->num_color_channels;
    has_extra = params->num_extra_channels > 0;
    first_plane = dest != NULL ? dest->channels.nb_meta_channels : 0;

    if (parsed != NULL) {
        if (jxl_render_any_plane_integer(canvas)) {
            st = jxl_render_ensure_all_planes_f32(params->alloc, canvas, parsed);
            if (st != JXL_OK) {
                return st;
            }
        }
        if (jxl_render_any_plane_integer(local)) {
            st = jxl_render_ensure_all_planes_f32(params->alloc, local, parsed);
            if (st != JXL_OK) {
                return st;
            }
        }
    }

    for (p = 0; p < canvas->num_planes && p < local->num_planes; ++p) {
        uint32_t y;
        jxl_blend_params bp = {0};
        const jxl_blending_info *blend = blending_for_plane(fh, p, num_color);
        uint32_t alpha_plane;
        int is_alpha_channel;
        int is_muladd_alpha_skip;
        bp.mode = fh->resets_canvas ? JXL_BLEND_REPLACE : blend->mode;
        bp.clamp = blend->clamp;
        bp.alpha_channel = blend->alpha_channel;
        bp.premultiplied = parsed != NULL && parsed->alpha_associated > 0;

        alpha_plane = num_color + blend->alpha_channel;
        is_alpha_channel = has_extra && bp.mode == JXL_BLEND_BLEND && p == alpha_plane;
        is_muladd_alpha_skip =
            has_extra && bp.mode == JXL_BLEND_MUL_ADD && p == alpha_plane;

        for (y = 0; y < layout.blit_h; ++y) {
            uint32_t x;
            for (x = 0; x < layout.blit_w; ++x) {
                float base_alpha;
                float new_alpha;
                size_t dst = (size_t)(layout.dst_y0 + (int32_t)y) * canvas->width +
                             (size_t)(layout.dst_x0 + (int32_t)x);
                size_t src = (size_t)(layout.src_y0 + y) * local->width +
                             (size_t)(layout.src_x0 + x);
                float new_sample = local->planes[p][src];
                float *dst_px = &canvas->planes[p][dst];

                if (bp.mode == JXL_BLEND_MUL_ADD && is_muladd_alpha_skip) {
                    continue;
                }
                if (bp.mode == JXL_BLEND_BLEND && is_alpha_channel) {
                    *dst_px = *dst_px + jxl_blend_clamp01(new_sample, bp.clamp) * (1.0f - *dst_px);
                    continue;
                }

                base_alpha = 0.0f;
                new_alpha = 0.0f;
                if (has_extra && (bp.mode == JXL_BLEND_BLEND || bp.mode == JXL_BLEND_MUL_ADD) &&
                    !is_alpha_channel) {
                    base_alpha = canvas->planes[alpha_plane][dst];
                    new_alpha = jxl_blend_clamp01(local->planes[alpha_plane][src], bp.clamp);
                }

                *dst_px = jxl_blend_samples(*dst_px, new_sample, base_alpha, new_alpha, &bp);
            }
        }
        if (dest != NULL && first_plane + p < dest->image_channels_len) {
            jxl_modular_region region;
            const jxl_modular_channel_info *info = &dest->channels.info[first_plane + p];
            uint32_t ow = info->original_width != 0 ? info->original_width : info->width;
            uint32_t oh = info->original_height != 0 ? info->original_height : info->height;
            region.left = ox;
            region.top = oy;
            region.width = ow;
            region.height = oh;

            jxl_render_set_plane_meta(canvas, p, &region, &info->original_shift, layout.blit_w,
                                      layout.blit_h);
        }
    }
    canvas->ct_done = local->ct_done;
    return JXL_OK;
}

static jxl_status_t modular_save_reference(const jxl_modular_compose_params *params,
                                          jxl_render *canvas, jxl_reference_store *refs) {
    jxl_status_t st;
    const jxl_frame_header *fh;
    uint32_t save_slot;
    if (params == NULL || params->fh == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    fh = params->fh;
    if (!jxl_frame_header_can_reference(fh) || refs == NULL) {
        return JXL_OK;
    }
    st = jxl_render_ensure_all_planes_f32(params->alloc, canvas, params->parsed);
    if (st != JXL_OK) {
        return st;
    }
    save_slot = fh->save_as_reference;
    if (save_slot >= 4) {
        return JXL_OK;
    }
    if (!jxl_frame_header_is_keyframe(fh)) {
        jxl_ref_image_release(params->alloc, &refs->slots[save_slot]);
    }
    return jxl_ref_image_from_canvas(params->alloc, fh, canvas, &refs->slots[save_slot]);
}

jxl_status_t jxl_render_composite_local_frame(const jxl_modular_compose_params *params,
                                              jxl_render *local, jxl_reference_store *refs,
                                              jxl_render *canvas) {
    jxl_status_t st;
    const jxl_frame_header *fh;
    const jxl_parsed_image_header *parsed;
    int composite_skip;
    jxl_modular_region oriented;
    if (params == NULL || params->alloc == NULL || params->parsed == NULL || params->fh == NULL ||
        local == NULL || canvas == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }

    fh = params->fh;
    parsed = params->parsed;

    composite_skip = jxl_render_composite_preprocess(params->ctx, params->alloc, parsed, fh, local);
    if (composite_skip < 0) {
        return JXL_ERROR_INVALID_INPUT;
    }

    oriented =
        jxl_modular_region_with_size(parsed->size.width, parsed->size.height);
    if (params->output_region != NULL) {
        oriented = *params->output_region;
    }

    if (composite_skip) {
        st = modular_local_blit_to_render(params, params->dest, fh, local, canvas);
        if (st == JXL_OK && jxl_frame_header_can_reference(fh)) {
            st = modular_save_reference(params, canvas, refs);
        }
    } else if (params->prefer_canvas_base) {
        st = modular_local_blend_to_render(params, params->dest, parsed, fh, local, canvas);
        if (st == JXL_OK && jxl_frame_header_can_reference(fh)) {
            int pp = jxl_render_composite_preprocess(params->ctx, params->alloc, parsed, fh, canvas);
            if (pp < 0) {
                st = JXL_ERROR_INVALID_INPUT;
            } else if (st == JXL_OK) {
                st = modular_save_reference(params, canvas, refs);
            }
        }
    } else {
        jxl_render_composite_params cp = {0};
        cp.alloc = params->alloc;
        cp.parsed = parsed;
        cp.fh = fh;
        cp.new_frame = local;
        cp.canvas = canvas;
        cp.refs = refs;
        cp.prefer_canvas_base = 0;
        cp.oriented_image_region = oriented;
        cp.num_color_channels = params->num_color_channels;
        cp.num_extra_channels = params->num_extra_channels;

        st = jxl_render_composite(&cp);
    }
    return st;
}

jxl_status_t jxl_render_compose_modular_dest(const jxl_modular_compose_params *params,
                                             jxl_reference_store *refs, jxl_render *canvas) {
    uint32_t local_color_planes;
    jxl_modular_blit_layout layout;
    const jxl_frame_header *fh;
    uint32_t fw;
    uint32_t fh_h;
    uint32_t compose_color;
    uint32_t compose_planes;
    jxl_render *local;
    jxl_modular_region frame_region;
    jxl_status_t st;
    if (params == NULL || params->alloc == NULL || params->parsed == NULL || params->fh == NULL ||
        params->dest == NULL || canvas == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }

    fh = params->fh;
    fw = fh->width;
    fh_h = fh->height;

    compose_color = fh->encoded_color_channels;
    if (compose_color == 0u) {
        compose_color = params->num_color_channels;
    }
    compose_planes = compose_color + params->num_extra_channels;
    if (compose_planes == 0u) {
        compose_planes = canvas->num_planes;
    }
    local_color_planes = compose_color < 3u ? compose_color : 3u;

    {
        int composite_skip =
            jxl_render_composite_preprocess(params->ctx, params->alloc, params->parsed, fh, canvas);
        if (composite_skip < 0) {
            return JXL_ERROR_INVALID_INPUT;
        }
        if (composite_skip && params->xyb_dequant == NULL &&
            modular_blit_layout(params, fh, canvas->width, canvas->height, &layout) == JXL_OK) {
            jxl_status_t st =
                modular_dest_blit_to_canvas(params, params->dest, fh, canvas, &layout);
            if (st == JXL_OK && jxl_frame_header_can_reference(fh)) {
                st = modular_save_reference(params, canvas, refs);
            }
            return st;
        }
    }

    local = jxl_render_create(params->alloc, compose_planes, local_color_planes, fw, fh_h);
    if (local == NULL) {
        return JXL_ERROR_OUT_OF_MEMORY;
    }
    frame_region = jxl_modular_region_with_size(fw, fh_h);
    jxl_render_init_all_planes(local, &frame_region);

    st = modular_dest_to_local_render(params, params->dest, fh, params->xyb_dequant, local);
    if (st != JXL_OK) {
        jxl_render_free(params->alloc, local);
        return st;
    }

    st = jxl_render_composite_local_frame(params, local, refs, canvas);
    jxl_render_free(params->alloc, local);
    return st;
}
