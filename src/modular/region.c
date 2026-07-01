// SPDX-License-Identifier: MIT OR Apache-2.0
#include "region.h"

#include "frame/frame_header.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

jxl_modular_region jxl_modular_region_with_size(uint32_t width, uint32_t height) {
    jxl_modular_region result;
    result.left = 0;
    result.top = 0;
    result.width = width;
    result.height = height;
    return result;

}

jxl_modular_region jxl_modular_region_intersection(jxl_modular_region a, jxl_modular_region b) {
    int32_t left = a.left > b.left ? a.left : b.left;
    int32_t top = a.top > b.top ? a.top : b.top;
    int32_t a_right = a.left + (int32_t)a.width;
    int32_t a_bottom = a.top + (int32_t)a.height;
    int32_t b_right = b.left + (int32_t)b.width;
    int32_t b_bottom = b.top + (int32_t)b.height;
    int32_t right = a_right < b_right ? a_right : b_right;
    int32_t bottom = a_bottom < b_bottom ? a_bottom : b_bottom;
    jxl_modular_region result;
    if (right <= left || bottom <= top) {
        jxl_modular_region result;
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

jxl_modular_region jxl_modular_region_container_aligned(jxl_modular_region region,
                                                      uint32_t grid_dim) {
    uint32_t mask;
    uint32_t new_left;
    uint32_t new_top;
    uint32_t x_diff;
    uint32_t y_diff;
    jxl_modular_region result;
    uint32_t add;
    uint32_t ul;
    uint32_t ut;
    if (grid_dim == 0 || (grid_dim & (grid_dim - 1u)) != 0) {
        return region;
    }
    add = grid_dim - 1u;
    mask = ~add;
    ul = (uint32_t)region.left;
    ut = (uint32_t)region.top;
    new_left = ul & mask;
    new_top = ut & mask;
    x_diff = ul - new_left;
    y_diff = ut - new_top;
    result.left = (int32_t)new_left;
    result.top = (int32_t)new_top;
    result.width = (region.width + x_diff + add) & mask;
    result.height = (region.height + y_diff + add) & mask;
    return result;

}

jxl_modular_region jxl_modular_region_upsample(jxl_modular_region region, uint32_t factor_log2) {
    jxl_modular_region result;
    uint32_t mul;
    if (factor_log2 == 0) {
        return region;
    }
    mul = 1u << factor_log2;
    result.left = region.left * (int32_t)mul;
    result.top = region.top * (int32_t)mul;
    result.width = region.width * mul;
    result.height = region.height * mul;
    return result;

}

static uint32_t abs_diff_i32(int32_t a, int32_t b) {
    return a >= b ? (uint32_t)(a - b) : (uint32_t)(b - a);
}

jxl_modular_region jxl_modular_region_downsample(jxl_modular_region region, uint32_t factor_log2) {
    jxl_modular_region result;
    uint32_t add;
    int32_t new_left;
    int32_t new_top;
    uint32_t adj_width;
    uint32_t adj_height;
    if (factor_log2 == 0) {
        return region;
    }
    add = (1u << factor_log2) - 1u;
    new_left = region.left >> (int32_t)factor_log2;
    new_top = region.top >> (int32_t)factor_log2;
    adj_width =
        region.width + abs_diff_i32(region.left, new_left << (int32_t)factor_log2);
    adj_height =
        region.height + abs_diff_i32(region.top, new_top << (int32_t)factor_log2);
    result.left = new_left;
    result.top = new_top;
    result.width = (adj_width + add) >> factor_log2;
    result.height = (adj_height + add) >> factor_log2;
    return result;

}

jxl_modular_region jxl_modular_region_pad(jxl_modular_region region, uint32_t pad) {
    jxl_modular_region result;
    uint32_t w;
    uint32_t h;
    if (pad == 0) {
        return region;
    }
    w = region.width + 2u * pad;
    h = region.height + 2u * pad;
    result.left = region.left - (int32_t)pad;
    result.top = region.top - (int32_t)pad;
    result.width = w;
    result.height = h;
    return result;

}

int jxl_modular_pass_group_intersects(const jxl_frame_header *fh, uint32_t group_idx,
                                      const jxl_modular_region *filter, uint32_t group_dim) {
    jxl_modular_region group;
    uint32_t group_col;
    uint32_t group_row;
    uint32_t groups_per_row;
    if (filter == NULL) {
        return 1;
    }
    groups_per_row = jxl_frame_header_groups_per_row(fh);
    if (groups_per_row == 0 || group_dim == 0) {
        return 1;
    }
    group_col = group_idx % groups_per_row;
    group_row = group_idx / groups_per_row;
    group.left = (int32_t)(group_col * group_dim);
    group.top = (int32_t)(group_row * group_dim);
    group.width = group_dim;
    group.height = group_dim;

    return jxl_modular_region_intersects(group, *filter);
}

int jxl_modular_lf_group_intersects(const jxl_frame_header *fh, uint32_t lf_group_idx,
                                    const jxl_modular_region *filter) {
    jxl_modular_region group;
    uint32_t group_col;
    uint32_t group_row;
    uint32_t lf_groups_per_row;
    uint32_t lf_group_dim;
    if (filter == NULL) {
        return 1;
    }
    lf_groups_per_row = jxl_frame_header_lf_groups_per_row(fh);
    lf_group_dim = jxl_frame_header_lf_group_dim(fh);
    if (lf_groups_per_row == 0 || lf_group_dim == 0) {
        return 1;
    }
    group_col = lf_group_idx % lf_groups_per_row;
    group_row = lf_group_idx / lf_groups_per_row;
    group.left = (int32_t)(group_col * lf_group_dim);
    group.top = (int32_t)(group_row * lf_group_dim);
    group.width = lf_group_dim;
    group.height = lf_group_dim;

    return jxl_modular_region_intersects(group, *filter);
}

void jxl_modular_vardct_decode_regions(const jxl_frame_header *fh, jxl_modular_region request,
                                     jxl_modular_region *pass_region_out,
                                     jxl_modular_region *lf_region_out) {
    jxl_modular_region div8;
    uint32_t group_dim;
    uint32_t frame_w;
    uint32_t frame_h;
    jxl_modular_region aligned;
    jxl_modular_region frame_bounds;
    jxl_modular_region lf_bounds;
    if (fh == NULL || pass_region_out == NULL || lf_region_out == NULL) {
        return;
    }
    group_dim = jxl_frame_header_group_dim(fh);
    frame_w = jxl_frame_header_color_sample_width(fh);
    frame_h = jxl_frame_header_color_sample_height(fh);
    aligned = jxl_modular_region_container_aligned(request, group_dim);
    frame_bounds = jxl_modular_region_with_size(frame_w, frame_h);
    *pass_region_out = jxl_modular_region_intersection(aligned, frame_bounds);

    div8.left = pass_region_out->left / 8;
    div8.top = pass_region_out->top / 8;
    div8.width = (pass_region_out->width + 7u) / 8u;
    div8.height = (pass_region_out->height + 7u) / 8u;

    if (!jxl_frame_flags_skip_adaptive_lf_smoothing(&fh->flags)) {
        div8 = jxl_modular_region_pad(div8, 1);
    }
    div8 = jxl_modular_region_container_aligned(div8, group_dim);
    lf_bounds =
        jxl_modular_region_with_size((frame_w + 7u) / 8u, (frame_h + 7u) / 8u);
    *lf_region_out = jxl_modular_region_intersection(div8, lf_bounds);
}

int jxl_modular_region_intersects(jxl_modular_region a, jxl_modular_region b) {
    int32_t a_right = a.left + (int32_t)a.width;
    int32_t a_bottom = a.top + (int32_t)a.height;
    int32_t b_right = b.left + (int32_t)b.width;
    int32_t b_bottom = b.top + (int32_t)b.height;
    if (a_right <= b.left || b_right <= a.left) {
        return 0;
    }
    if (a_bottom <= b.top || b_bottom <= a.top) {
        return 0;
    }
    return 1;
}

static uint32_t saturating_add_u32(uint32_t a, int32_t b) {
    uint32_t ub;
    if (b <= 0) {
        return a;
    }
    ub = (uint32_t)b;
    if (a > UINT32_MAX - ub) {
        return UINT32_MAX;
    }
    return a + ub;
}

jxl_modular_region jxl_modular_compute_region(const jxl_frame_header *frame_header,
                                              const jxl_modular_image_destination *dest,
                                              jxl_modular_region region, int is_lf) {
    uint32_t width;
    uint32_t height;
    uint32_t need_w;
    uint32_t need_h;
    if (frame_header == NULL || dest == NULL) {
        return region;
    }
    if (!jxl_modular_image_has_palette(dest) && !jxl_modular_image_has_squeeze(dest)) {
        return region;
    }

    width = jxl_frame_header_color_sample_width(frame_header);
    height = jxl_frame_header_color_sample_height(frame_header);
    if (is_lf) {
        width = (width + 7u) / 8u;
        height = (height + 7u) / 8u;
    }

    need_w = saturating_add_u32(region.width, region.left);
    need_h = saturating_add_u32(region.height, region.top);
    if (width < need_w) {
        width = need_w;
    }
    if (height < need_h) {
        height = need_h;
    }
    return jxl_modular_region_with_size(width, height);
}
