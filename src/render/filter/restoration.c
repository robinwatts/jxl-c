// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/filter/restoration.h"

#include "modular/region.h"
#include "render/filter/epf.h"
#include "render/filter/gabor.h"
#include "render/filter/padded_f32.h"

#include <stdlib.h>
#include <string.h>

static jxl_modular_region restoration_filter_area(const jxl_subgrid_f32 channels[3],
                                                  const jxl_modular_region *color_padded,
                                                  const jxl_modular_region *output_region) {
    jxl_modular_region local_padded;
    jxl_modular_region bounds;
    if (color_padded == NULL || channels[0].data == NULL) {
        jxl_modular_region result;
        result.left = 0;
        result.top = 0;
        result.width = (uint32_t)channels[0].width;
        result.height = (uint32_t)channels[0].height;
        return result;

    }
    bounds = jxl_modular_region_with_size((uint32_t)channels[0].width,
                                          (uint32_t)channels[0].height);
    if (output_region != NULL) {
        local_padded = *color_padded;
        local_padded.left -= output_region->left;
        local_padded.top -= output_region->top;
    } else {
        local_padded =
            jxl_modular_region_with_size(color_padded->width, color_padded->height);
    }
    return jxl_modular_region_intersection(local_padded, bounds);
}

static int restoration_use_inplace(const jxl_subgrid_f32 channels[3],
                                   const jxl_modular_region *filter_area,
                                   const jxl_filter_pad_params *pad,
                                   const jxl_modular_region *output_region) {
    if (channels == NULL || channels[0].data == NULL || filter_area == NULL || pad == NULL) {
        return 0;
    }
    if (output_region != NULL) {
        return 0;
    }
    if (filter_area->left < 0 || filter_area->top < 0) {
        return 0;
    }
    if ((uint32_t)filter_area->left != pad->pad_left ||
        (uint32_t)filter_area->top != pad->pad_top) {
        return 0;
    }
    if ((size_t)filter_area->left + filter_area->width > channels[0].width ||
        (size_t)filter_area->top + filter_area->height > channels[0].height) {
        return 0;
    }
    return 1;
}

static int gabor_extent_can_swap(const jxl_filter_extent *ext) {
    return ext->origin_x == 0 && ext->origin_y == 0 && ext->width == ext->full.width &&
           ext->height == ext->full.height && ext->full.stride == ext->width;
}

static void gabor_publish_extent(jxl_filter_extent *ext, float **scratch_slot, int *swapped) {
    jxl_subgrid_f32 view;
    if (gabor_extent_can_swap(ext)) {
        float *old = ext->full.data;
        ext->full.data = *scratch_slot;
        *scratch_slot = old;
        *swapped = 1;
        return;
    }
    view = jxl_filter_extent_view(ext);
    jxl_subgrid_f32_copy_from_packed(view, *scratch_slot);
    *swapped = 0;
}

static int run_restoration_on_extents(jxl_context *ctx, jxl_allocator_state *alloc,
                                      jxl_filter_extent extents[3],
                                      jxl_subgrid_f32 parent_channels[3],
                                      const jxl_restoration_filter *restoration,
                                      const jxl_frame_header *frame_header,
                                      const jxl_lf_group *lf_groups, uint32_t num_lf_groups,
                                      const jxl_filter_frame_region *filter_region) {
    size_t c;
    size_t scratch_count = extents[0].width * extents[0].height;
    int gabor_swapped[3] = {0, 0, 0};
    int ok;
    float *scratch[3] = {NULL, NULL, NULL};

    for (c = 0; c < 3; ++c) {
        scratch[c] = jxl_alloc(alloc, scratch_count * sizeof(float));
        if (scratch[c] == NULL) {
            size_t i;
            for (i = 0; i < c; ++i) {
                jxl_free(alloc, scratch[i]);
            }
            return 0;
        }
    }

    ok = 1;
    if (jxl_gabor_enabled(restoration)) {
        ok = jxl_apply_gabor_like_extent(ctx, extents, &restoration->gab, scratch);
        if (ok) {
            for (c = 0; c < 3; ++c) {
                gabor_publish_extent(&extents[c], &scratch[c], &gabor_swapped[c]);
                if (gabor_swapped[c]) {
                    parent_channels[c].data = extents[c].full.data;
                }
            }
        }
    }
    if (ok && jxl_epf_enabled(restoration)) {
        ok = jxl_apply_epf_extent(ctx, extents, &restoration->epf, frame_header, lf_groups,
                                  num_lf_groups, filter_region, scratch);
    }

    for (c = 0; c < 3; ++c) {
        if (!gabor_swapped[c]) {
            jxl_free(alloc, scratch[c]);
        }
    }
    return ok;
}

int jxl_apply_restoration_filters(jxl_context *ctx, jxl_allocator_state *alloc,
                                  jxl_subgrid_f32 channels[3],
                                  const jxl_restoration_filter *restoration,
                                  const jxl_frame_header *frame_header,
                                  const jxl_lf_group *lf_groups, uint32_t num_lf_groups,
                                  const jxl_modular_region *color_padded,
                                  const jxl_modular_region *output_region) {
                                      size_t c;
    int32_t frame_origin_left;
    int32_t frame_origin_top;
    jxl_subgrid_f32 work_channels[3];
    jxl_filter_pad_params pad;
    jxl_padded_f32 padded[3];
    jxl_filter_extent extents[3];
    int ok;
    jxl_filter_frame_region filter_region = {0};
    jxl_modular_region filter_area;
    int32_t frame_left;
    int32_t frame_top;

    if (alloc == NULL || channels == NULL || restoration == NULL || frame_header == NULL) {
        return 0;
    }
    if (!jxl_gabor_enabled(restoration) && !jxl_epf_enabled(restoration)) {
        return 1;
    }
    if (channels[0].data == NULL || channels[0].width == 0 || channels[0].height == 0) {
        return 0;
    }

    filter_area = restoration_filter_area(channels, color_padded, output_region);
    if (filter_area.width == 0 || filter_area.height == 0) {
        return 1;
    }
    if (filter_area.left < 0 || filter_area.top < 0) {
        return 0;
    }

    frame_origin_left = 0;
    frame_origin_top = 0;
    if (output_region != NULL) {
        frame_origin_left = output_region->left;
        frame_origin_top = output_region->top;
    } else if (color_padded != NULL) {
        frame_origin_left = color_padded->left;
        frame_origin_top = color_padded->top;
    }
    frame_left = filter_area.left + frame_origin_left;
    frame_top = filter_area.top + frame_origin_top;

    for (c = 0; c < 3; ++c) {
        work_channels[c] = jxl_subgrid_f32_sub(
            channels[c], (size_t)filter_area.left, (size_t)filter_area.top, filter_area.width,
            filter_area.height);
    }

    jxl_filter_pad_params_compute(&pad, restoration, filter_area.width, filter_area.height, 0, 0);

    filter_region.frame_left = (uint32_t)frame_left;
    filter_region.frame_top = (uint32_t)frame_top;
    filter_region.frame_width = filter_area.width;
    filter_region.frame_height = filter_area.height;

    if (restoration_use_inplace(channels, &filter_area, &pad, output_region)) {
        for (c = 0; c < 3; ++c) {
            if (channels[c].data == NULL) {
                return 0;
            }
            extents[c].full = channels[c];
            extents[c].origin_x = pad.pad_left;
            extents[c].origin_y = pad.pad_top;
            extents[c].width = filter_area.width;
            extents[c].height = filter_area.height;
        }
        return run_restoration_on_extents(ctx, alloc, extents, channels, restoration, frame_header,
                                          lf_groups, num_lf_groups, &filter_region);
    }

    memset(padded, 0, sizeof(padded));

    for (c = 0; c < 3; ++c) {
        if (work_channels[c].data == NULL) {
            size_t i;
            for (i = 0; i < c; ++i) {
                jxl_padded_f32_free(alloc, &padded[i]);
            }
            return 0;
        }
        if (!jxl_padded_f32_alloc(alloc, pad.buf_width, pad.buf_height, &padded[c]) ||
            !jxl_padded_f32_place(&work_channels[c], &padded[c], pad.pad_left, pad.pad_top)) {
            size_t i;
            for (i = 0; i <= c; ++i) {
                jxl_padded_f32_free(alloc, &padded[i]);
            }
            return 0;
        }
        jxl_padded_f32_mirror_trailing(&padded[c], pad.pad_left, pad.pad_top,
                                       work_channels[c].width, work_channels[c].height);
    }

    for (c = 0; c < 3; ++c) {
        jxl_filter_extent compound_tmp;
        compound_tmp.full = jxl_padded_f32_subgrid(&padded[c]);
        compound_tmp.origin_x = pad.pad_left;
        compound_tmp.origin_y = pad.pad_top;
        compound_tmp.width = work_channels[c].width;
        compound_tmp.height = work_channels[c].height;
        extents[c] = compound_tmp;

    }

    ok = run_restoration_on_extents(ctx, alloc, extents, channels, restoration, frame_header,
                                    lf_groups, num_lf_groups, &filter_region);
    if (!ok) {
        for (c = 0; c < 3; ++c) {
            jxl_padded_f32_free(alloc, &padded[c]);
        }
        return 0;
    }

    for (c = 0; c < 3; ++c) {
        if (!jxl_padded_f32_copy_region_to(&padded[c], pad.pad_left, pad.pad_top,
                                           work_channels[c])) {
            size_t i;
            for (i = 0; i < 3; ++i) {
                jxl_padded_f32_free(alloc, &padded[i]);
            }
            return 0;
        }
        jxl_padded_f32_free(alloc, &padded[c]);
    }
    return 1;
}
