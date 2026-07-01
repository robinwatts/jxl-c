// SPDX-License-Identifier: MIT OR Apache-2.0
#include "modular_sample.h"

#include "image/image_internal.h"
#include "render/simd/features.h"
#include "vardct/lf.h"

#include "jxl_oxide/jxl_types.h"

#if defined(JXL_HAVE_SIMD_AVX2)
void jxl_modular_blit_i16_row_to_plane_avx2(const int16_t *src, float *dst, size_t n,
                                            float scale);
#endif

static int32_t grid_sample_i32(const jxl_modular_grid_i32 *g, size_t x, size_t y) {
    return jxl_modular_grid_sample_as_i32(g, x, y);
}

static float modular_bit_depth_scale(uint32_t bit_depth) {
    if (bit_depth >= 31) {
        return 1.0f;
    }
    return 1.0f / (float)((1u << bit_depth) - 1u);
}

static float modular_sample_as_float(int32_t sample, uint32_t bit_depth) {
    if (bit_depth >= 31) {
        union {
            int32_t i;
            float f;
        } u;
        u.i = sample;
        return u.f;
    }
    return (float)sample * modular_bit_depth_scale(bit_depth);
}

static void blit_i16_grid_to_plane(const jxl_modular_grid_i32 *grid, uint32_t gw, uint32_t gh,
                                   uint32_t bit_depth_bits, uint32_t dst_stride, float *dst) {
    size_t y;
    const int16_t *base = (const int16_t *)grid->buf + grid->offset;
    size_t row_stride = jxl_modular_grid_row_stride(grid);

    if (bit_depth_bits >= 31) {
        for (y = 0; y < (size_t)gh; ++y) {
            size_t x;
            const int16_t *row = base + y * row_stride;
            for (x = 0; x < (size_t)gw; ++x) {
                union {
                    int32_t i;
                    float f;
                } u;
                u.i = (int32_t)row[x];
                dst[y * (size_t)dst_stride + x] = u.f;
            }
        }
        return;
    }

    {
        const float scale = modular_bit_depth_scale(bit_depth_bits);
#if defined(JXL_HAVE_SIMD_AVX2)
        JXL_CPU_FEATURES_LOCAL(feat);
        const int avx2 = feat->avx2 != 0;
#endif
        for (y = 0; y < (size_t)gh; ++y) {
            const int16_t *row = base + y * row_stride;
            float *dst_row = dst + y * (size_t)dst_stride;
#if defined(JXL_HAVE_SIMD_AVX2)
            if (avx2 && gw >= 8) {
                jxl_modular_blit_i16_row_to_plane_avx2(row, dst_row, gw, scale);
                continue;
            }
#endif
            {
                size_t x;
                for (x = 0; x < (size_t)gw; ++x) {
                    dst_row[x] = (float)row[x] * scale;
                }
            }
        }
    }
}

static void blit_i16_grid_region_to_plane(const jxl_modular_grid_i32 *grid, uint32_t grid_x0,
                                          uint32_t grid_y0, uint32_t blit_w, uint32_t blit_h,
                                          uint32_t bit_depth_bits, uint32_t dst_stride, float *dst,
                                          uint32_t dst_x0, uint32_t dst_y0) {
    uint32_t y;
    const int16_t *base = (const int16_t *)grid->buf + grid->offset;
    size_t row_stride = jxl_modular_grid_row_stride(grid);

    if (bit_depth_bits >= 31) {
        for (y = 0; y < blit_h; ++y) {
            uint32_t x;
            const int16_t *row = base + (size_t)(grid_y0 + y) * row_stride + grid_x0;
            for (x = 0; x < blit_w; ++x) {
                union {
                    int32_t i;
                    float f;
                } u;
                u.i = (int32_t)row[x];
                dst[(size_t)(dst_y0 + y) * dst_stride + (size_t)(dst_x0 + x)] = u.f;
            }
        }
        return;
    }

    {
        const float scale = modular_bit_depth_scale(bit_depth_bits);
#if defined(JXL_HAVE_SIMD_AVX2)
        JXL_CPU_FEATURES_LOCAL(feat);
        const int avx2 = feat->avx2 != 0;
#endif
        for (y = 0; y < blit_h; ++y) {
            uint32_t x;
            const int16_t *row = base + (size_t)(grid_y0 + y) * row_stride + grid_x0;
            float *dst_row = dst + (size_t)(dst_y0 + y) * dst_stride + dst_x0;
#if defined(JXL_HAVE_SIMD_AVX2)
            if (avx2 && blit_w >= 8) {
                jxl_modular_blit_i16_row_to_plane_avx2(row, dst_row, blit_w, scale);
                continue;
            }
#endif
            for (x = 0; x < blit_w; ++x) {
                dst_row[x] = (float)row[x] * scale;
            }
        }
    }
}

int jxl_modular_blit_channel_to_plane(const jxl_modular_grid_i32 *grid,
                                      const jxl_modular_channel_info *info, uint32_t bit_depth_bits,
                                      uint32_t dst_stride, float *dst, uint32_t *out_gw,
                                      uint32_t *out_gh) {
                                          size_t y;
    uint32_t gw;
    uint32_t gh;
    uint32_t ow;
    uint32_t oh;
    if (grid == NULL || info == NULL || dst == NULL || grid->buf == NULL) {
        return 0;
    }

    ow = info->original_width != 0 ? info->original_width : info->width;
    oh = info->original_height != 0 ? info->original_height : info->height;
    gw = 0;
    gh = 0;
    jxl_channel_shift_shift_size(&info->original_shift, ow, oh, &gw, &gh);
    if (grid->width != (size_t)gw || grid->height != (size_t)gh) {
        return 0;
    }

    if (grid->kind == JXL_MODULAR_SAMPLE_I16) {
        blit_i16_grid_to_plane(grid, gw, gh, bit_depth_bits, dst_stride, dst);
    } else {
        for (y = 0; y < (size_t)gh; ++y) {
            size_t x;
            for (x = 0; x < (size_t)gw; ++x) {
                int32_t sample = jxl_modular_grid_sample_as_i32(grid, x, y);
                dst[y * (size_t)dst_stride + x] = modular_sample_as_float(sample, bit_depth_bits);
            }
        }
    }
    if (out_gw != NULL) {
        *out_gw = gw;
    }
    if (out_gh != NULL) {
        *out_gh = gh;
    }
    return 1;
}

int jxl_modular_blit_channel_region_to_plane(const jxl_modular_grid_i32 *grid,
                                             const jxl_modular_channel_info *info,
                                             uint32_t bit_depth_bits, jxl_modular_region region,
                                             uint32_t dst_stride, float *dst, uint32_t dst_x0,
                                             uint32_t dst_y0) {
                                                 uint32_t y;
    uint32_t blit_w;
    uint32_t blit_h;
    uint32_t ow;
    uint32_t oh;
    jxl_modular_region ec_bounds;
    jxl_modular_region blit;
    jxl_channel_shift shift;
    uint32_t down_h;
    uint32_t down_v;
    uint32_t grid_x0;
    uint32_t grid_y0;
    if (grid == NULL || info == NULL || dst == NULL || grid->buf == NULL ||
        region.width == 0 || region.height == 0) {
        return 0;
    }

    ow = info->original_width != 0 ? info->original_width : info->width;
    oh = info->original_height != 0 ? info->original_height : info->height;
    ec_bounds = jxl_modular_region_with_size(ow, oh);
    blit = jxl_modular_region_intersection(region, ec_bounds);
    if (blit.width == 0 || blit.height == 0) {
        return 1;
    }

    shift = info->original_shift;
    down_h = (uint32_t)jxl_channel_shift_hshift(&shift);
    down_v = (uint32_t)jxl_channel_shift_vshift(&shift);
    grid_x0 = (uint32_t)(blit.left >> (int32_t)down_h);
    grid_y0 = (uint32_t)(blit.top >> (int32_t)down_v);
    blit_w = 0;
    blit_h = 0;
    jxl_channel_shift_shift_size(&shift, blit.width, blit.height, &blit_w, &blit_h);
    if (grid_x0 + blit_w > grid->width || grid_y0 + blit_h > grid->height) {
        return 0;
    }
    if (dst_x0 + blit_w > dst_stride || dst_y0 + blit_h == 0) {
        return 0;
    }

    if (grid->kind == JXL_MODULAR_SAMPLE_I16) {
        blit_i16_grid_region_to_plane(grid, grid_x0, grid_y0, blit_w, blit_h, bit_depth_bits,
                                      dst_stride, dst, dst_x0, dst_y0);
    } else {
        for (y = 0; y < blit_h; ++y) {
            uint32_t x;
            for (x = 0; x < blit_w; ++x) {
                int32_t sample = jxl_modular_grid_sample_as_i32(grid, (size_t)grid_x0 + x,
                                                                 (size_t)grid_y0 + y);
                dst[(size_t)(dst_y0 + y) * dst_stride + (size_t)(dst_x0 + x)] =
                    modular_sample_as_float(sample, bit_depth_bits);
            }
        }
    }
    return 1;
}

float jxl_modular_sample_color_float(const jxl_modular_image_destination *dest, size_t first_plane,
                                     uint32_t plane_idx, const jxl_lf_channel_dequant *xyb_dequant,
                                     const jxl_parsed_image_header *parsed, uint32_t bit_depth,
                                     size_t x, size_t y) {
    const jxl_modular_grid_i32 *grid_y;
    const jxl_modular_grid_i32 *grid_x;
    const jxl_modular_grid_i32 *grid_b;
    float m_x;
    float m_y;
    float m_b;
    if (dest == NULL || first_plane + plane_idx >= dest->image_channels_len) {
        return 0.0f;
    }

    if (xyb_dequant == NULL || plane_idx >= 3) {
        uint32_t use_depth = bit_depth;
        if (parsed != NULL && plane_idx >= 3) {
            use_depth = jxl_parsed_ec_bit_depth(parsed, plane_idx - 3);
        }
        const jxl_modular_grid_i32 *grid = &dest->image_channels[first_plane + plane_idx];
        return modular_sample_as_float(grid_sample_i32(grid, x, y), use_depth);
    }

    grid_y = &dest->image_channels[first_plane + 0];
    grid_x = &dest->image_channels[first_plane + 1];
    grid_b = &dest->image_channels[first_plane + 2];

    m_x = jxl_lf_channel_dequant_m_x_unscaled(xyb_dequant);
    m_y = jxl_lf_channel_dequant_m_y_unscaled(xyb_dequant);
    m_b = jxl_lf_channel_dequant_m_b_unscaled(xyb_dequant);

    switch (plane_idx) {
    case 0:
        return (float)grid_sample_i32(grid_x, x, y) * m_x;
    case 1:
        return (float)grid_sample_i32(grid_y, x, y) * m_y;
    case 2: {
        uint32_t y_w = grid_y->width != 0 ? grid_y->width : 1u;
        uint32_t y_h = grid_y->height != 0 ? grid_y->height : 1u;
        uint32_t b_w = grid_b->width != 0 ? grid_b->width : 1u;
        uint32_t b_h = grid_b->height != 0 ? grid_b->height : 1u;
        uint32_t y_hscale = y_w / b_w;
        uint32_t y_vscale = y_h / b_h;
        int32_t b_sat;
        int32_t pb;
        int32_t py;
        if (y_hscale == 0) {
            y_hscale = 1;
        }
        if (y_vscale == 0) {
            y_vscale = 1;
        }
        pb = grid_sample_i32(grid_b, x, y);
        py = grid_sample_i32(grid_y, x * y_hscale, y * y_vscale);
        b_sat = pb + py;
        return (float)b_sat * m_b;
    }
    default:
        return 0.0f;
    }
}
