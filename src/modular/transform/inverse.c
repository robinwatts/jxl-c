// SPDX-License-Identifier: MIT OR Apache-2.0
#include "inverse.h"

#include "modular/channel.h"
#include "modular/prepare_subimage.h"
#include "modular/predictor_state.h"
#include "modular/sample.h"
#include "modular/transformed_grid.h"
#include "squeeze.h"
#include "transform.h"
#include "modular/transform/rct_internal.h"
#include "modular/float_export.h"

#include "allocator.h"
#include "context.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

static jxl_transformed_grid *inv_ch(jxl_modular_image_destination *dest) {
    return jxl_modular_dest_work_grids(dest);
}

static size_t inv_len(const jxl_modular_image_destination *dest) {
    return jxl_modular_dest_work_grids_len(dest);
}

static jxl_transformed_grid **inv_ch_ptr(jxl_modular_image_destination *dest) {
    return jxl_modular_dest_work_grids_storage(dest);
}

static jxl_modular_grid_i32 *inv_leader(jxl_transformed_grid *slot) {
    return slot != NULL ? jxl_transformed_grid_leader(slot) : NULL;
}

static const jxl_modular_grid_i32 *inv_leader_const(const jxl_transformed_grid *slot) {
    return slot != NULL ? jxl_transformed_grid_leader_const(slot) : NULL;
}

static int grids_are_dest_work(const jxl_modular_image_destination *dest,
                               const jxl_transformed_grid **grids) {
    return dest != NULL && grids != NULL && dest->subimage_grids_prepared &&
           dest->transformed_grids != NULL && *grids == dest->transformed_grids;
}

static size_t *inv_len_ptr(jxl_modular_image_destination *dest) {
    return jxl_modular_dest_work_grids_len_storage(dest);
}

static void swap_rows_i16(int16_t *a, int16_t *b, size_t width) {
    size_t x;
    int16_t tmp;
    for (x = 0; x < width; ++x) {
        tmp = a[x];
        a[x] = b[x];
        b[x] = tmp;
    }
}

static void swap_rows_i32(int32_t *a, int32_t *b, size_t width) {
    size_t x;
    int32_t tmp;
    for (x = 0; x < width; ++x) {
        tmp = a[x];
        a[x] = b[x];
        b[x] = tmp;
    }
}

static void inverse_permute_row_i16(uint32_t permutation, int16_t *row0, int16_t *row1,
                                    int16_t *row2, size_t width) {
    switch (permutation) {
    case 1:
        swap_rows_i16(row0, row1, width);
        swap_rows_i16(row0, row2, width);
        break;
    case 2:
        swap_rows_i16(row0, row1, width);
        swap_rows_i16(row1, row2, width);
        break;
    case 3:
        swap_rows_i16(row1, row2, width);
        break;
    case 4:
        swap_rows_i16(row0, row1, width);
        break;
    case 5:
        swap_rows_i16(row0, row2, width);
        break;
    default:
        break;
    }
}

static void inverse_permute_row_i32(uint32_t permutation, int32_t *row0, int32_t *row1,
                                    int32_t *row2, size_t width) {
    switch (permutation) {
    case 1:
        swap_rows_i32(row0, row1, width);
        swap_rows_i32(row0, row2, width);
        break;
    case 2:
        swap_rows_i32(row0, row1, width);
        swap_rows_i32(row1, row2, width);
        break;
    case 3:
        swap_rows_i32(row1, row2, width);
        break;
    case 4:
        swap_rows_i32(row0, row1, width);
        break;
    case 5:
        swap_rows_i32(row0, row2, width);
        break;
    default:
        break;
    }
}

static jxl_modular_status_t inverse_rct_grids(jxl_modular_grid_i32 *g0, jxl_modular_grid_i32 *g1,
                                              jxl_modular_grid_i32 *g2,
                                              const jxl_transform_rct *rct,
                                              jxl_modular_image_destination *dest,
                                              int export_after_rct,
                                              const jxl_cpu_features *feat) {
    size_t y;
    uint32_t permutation;
    uint32_t ty;
    int16_t *row0_i16;
    int16_t *row1_i16;
    int16_t *row2_i16;
    int32_t *row0_i32;
    int32_t *row1_i32;
    int32_t *row2_i32;

    if (g0 == NULL || g1 == NULL || g2 == NULL || rct == NULL || g0->buf == NULL ||
        g1->buf == NULL || g2->buf == NULL) {
        return JXL_MODULAR_DECODER_ERROR;
    }
    jxl_modular_grid_normalize_stride(g0);
    jxl_modular_grid_normalize_stride(g1);
    jxl_modular_grid_normalize_stride(g2);
    if (g0->width != g1->width || g0->width != g2->width || g0->height != g1->height ||
        g0->height != g2->height) {
        return JXL_MODULAR_DECODER_ERROR;
    }

    permutation = rct->rct_type / 7u;
    ty = rct->rct_type % 7u;
    if (g0->kind == JXL_MODULAR_SAMPLE_I16) {
        const jxl_modular_float_export_ctx *export_ctx =
            export_after_rct && dest != NULL && dest->float_export.active &&
                    dest->float_export.rct.enabled
                ? &dest->float_export
                : NULL;
        for (y = 0; y < g0->height; ++y) {
            row0_i16 = jxl_modular_grid_row_i16(g0, y);
            row1_i16 = jxl_modular_grid_row_i16(g1, y);
            row2_i16 = jxl_modular_grid_row_i16(g2, y);
            if (row0_i16 == NULL || row1_i16 == NULL || row2_i16 == NULL) {
                return JXL_MODULAR_DECODER_ERROR;
            }
            if (export_ctx != NULL && permutation == 0) {
                jxl_modular_rct_inverse_export_row3_i16(ty, row0_i16, row1_i16, row2_i16,
                                                        g0->width, export_ctx, (uint32_t)y);
            } else {
                jxl_rct_inverse_row_i16(ty, row0_i16, row1_i16, row2_i16, g0->width, feat);
                if (permutation != 0) {
                    inverse_permute_row_i16(permutation, row0_i16, row1_i16, row2_i16, g0->width);
                }
                if (export_ctx != NULL) {
                    jxl_modular_export_rct_row(export_ctx, (uint32_t)y, row0_i16, row1_i16,
                                               row2_i16);
                }
            }
        }
        if (export_ctx != NULL) {
            dest->float_export.color_exported = 1;
        }
        return JXL_MODULAR_OK;
    }

    for (y = 0; y < g0->height; ++y) {
        row0_i32 = jxl_modular_grid_row_i32(g0, y);
        row1_i32 = jxl_modular_grid_row_i32(g1, y);
        row2_i32 = jxl_modular_grid_row_i32(g2, y);
        if (row0_i32 == NULL || row1_i32 == NULL || row2_i32 == NULL) {
            return JXL_MODULAR_DECODER_ERROR;
        }
        jxl_rct_inverse_row_i32(ty, row0_i32, row1_i32, row2_i32, g0->width, feat);
        if (permutation != 0) {
            inverse_permute_row_i32(permutation, row0_i32, row1_i32, row2_i32, g0->width);
        }
    }
    return JXL_MODULAR_OK;
}

static const int16_t k_delta_palette[72][3] = {
    {0, 0, 0},       {4, 4, 4},       {11, 0, 0},      {0, 0, -13},     {0, -12, 0},
    {-10, -10, -10}, {-18, -18, -18}, {-27, -27, -27}, {-18, -18, 0},   {0, 0, -32},
    {-32, 0, 0},     {-37, -37, -37}, {0, -32, -32},   {24, 24, 45},    {50, 50, 50},
    {-45, -24, -24}, {-24, -45, -45}, {0, -24, -24},   {-34, -34, 0},   {-24, 0, -24},
    {-45, -45, -24}, {64, 64, 64},    {-32, 0, -32},   {0, -32, 0},     {-32, 0, 32},
    {-24, -45, -24}, {45, 24, 45},    {24, -24, -45},  {-45, -24, 24},  {80, 80, 80},
    {64, 0, 0},      {0, 0, -64},     {0, -64, -64},   {-24, -24, 45},  {96, 96, 96},
    {64, 64, 0},     {45, -24, -24},  {34, -34, 0},    {112, 112, 112}, {24, -45, -45},
    {45, 45, -24},   {0, -32, 32},    {24, -24, 45},   {0, 96, 96},     {45, -24, 24},
    {24, -45, -24},  {-24, -45, 24},  {0, -64, 0},     {96, 0, 0},      {128, 128, 128},
    {64, 0, 64},     {144, 144, 144}, {96, 96, 0},     {-36, -36, 36},  {45, -24, -45},
    {45, -45, -24},  {0, 0, -96},     {0, 128, 128},   {0, 96, 0},      {45, 24, -45},
    {-128, 0, 0},    {24, -45, 24},   {-45, 24, -45},  {64, 0, -64},    {64, -64, -64},
    {96, 0, 96},     {45, -45, 24},   {24, 45, -45},   {64, 64, -64},   {128, 128, 0},
    {0, 0, -128},    {-24, 45, -45},
};

static int32_t palette_resolve_sample(const jxl_modular_grid_i32 *palette, int32_t idx, size_t c,
                                      uint32_t bit_depth) {
    int32_t nb_colors = (int32_t)palette->width;
    int32_t bd;
    int32_t maxv;
    if (idx >= 0 && idx < nb_colors) {
        return jxl_modular_grid_sample_as_i32(palette, (size_t)idx, c);
    }
    bd = (int32_t)bit_depth;
    maxv = bd >= 31 ? INT32_MAX : ((1 << bd) - 1);
    if (idx >= nb_colors) {
        size_t cc;
        int32_t index = idx - nb_colors;
        if (index < 64) {
            int32_t bias = 1 << (bd > 3 ? bd - 3 : 0);
            return (int32_t)((((index >> (int32_t)(2 * c)) % 4) * maxv) / 4) + bias;
        }
        index -= 64;
        for (cc = 0; cc < c; ++cc) {
            index /= 5;
        }
        return (int32_t)(((index % 5) * maxv) / 4);
    }
    if (idx < 0 && c < 3) {
        int32_t index = -(idx + 1);
        int32_t temp;
	index = index % 143;
        temp = (int32_t)k_delta_palette[(index + 1) >> 1][c];
        if ((index & 1) == 0) {
            temp = -temp;
        }
        if (bd > 8) {
            int32_t shift = bd < 24 ? bd - 8 : 16;
            temp <<= shift;
        }
        return temp;
    }
    return 0;
}

typedef struct {
    size_t x;
    size_t y;
} jxl_palette_delta_pos;

jxl_inline int32_t grid_row_get_i32(const jxl_modular_grid_i32 *g, size_t x, size_t y) {
    if (g->kind == JXL_MODULAR_SAMPLE_I16) {
        const int16_t *row = jxl_modular_grid_row_i16_const(g, y);
        return row != NULL ? (int32_t)row[x] : 0;
    }
    const int32_t *row = jxl_modular_grid_row_i32_const(g, y);
    return row != NULL ? row[x] : 0;
}

jxl_inline void grid_row_set_i32(jxl_modular_grid_i32 *g, size_t x, size_t y, int32_t v) {
    if (g->kind == JXL_MODULAR_SAMPLE_I16) {
        int16_t *row = jxl_modular_grid_row_i16(g, y);
        if (row != NULL) {
            row[x] = (int16_t)v;
        }
        return;
    }
    int32_t *row = jxl_modular_grid_row_i32(g, y);
    if (row != NULL) {
        row[x] = v;
    }
}

static void resolve_palette_pixel(const jxl_modular_grid_i32 *palette, int32_t idx,
                                  jxl_modular_grid_i32 *leader,
                                  jxl_modular_grid_i32 **members, size_t member_count,
                                  uint32_t bit_depth, size_t x, size_t y) {
                                      size_t c;
    size_t leader_stride = jxl_modular_grid_row_stride(leader);
    (void)leader_stride;
    jxl_modular_grid_store_i32(leader, x, y,
                               palette_resolve_sample(palette, idx, 0, bit_depth));
    for (c = 0; c < member_count; ++c) {
        jxl_modular_grid_i32 *grid = members[c];
        (void)jxl_modular_grid_row_stride(grid);
        jxl_modular_grid_store_i32(grid, x, y,
                                   palette_resolve_sample(palette, idx, c + 1, bit_depth));
    }
}

static jxl_modular_status_t inverse_palette_apply_delta(
    jxl_allocator_state *alloc, jxl_modular_grid_i32 *grid,
    const jxl_palette_delta_pos *positions, size_t pos_count, const jxl_transform_palette *pal) {
        size_t y;
    jxl_modular_predictor_state predictor;
    size_t delta_i;
    const jxl_wp_header *wp;
    size_t grid_stride_v;
    if (pos_count == 0) {
        return JXL_MODULAR_OK;
    }
    wp = (pal->d_pred == JXL_PREDICTOR_SELF_CORRECTING && pal->has_wp_header) ? &pal->wp_header
                                                                              : NULL;
    jxl_modular_predictor_state_init(&predictor);
    jxl_modular_predictor_state_reset(alloc, &predictor, (uint32_t)grid->width, NULL, 0, wp);

    grid_stride_v = jxl_modular_grid_row_stride(grid);
    delta_i = 0;
    for (y = 0; y < grid->height; ++y) {
        size_t x;
        for (x = 0; x < grid->width; ++x) {
            jxl_modular_properties props;
            int32_t sample;
            /* Match Rust palette inverse: properties::<true> and predict::<_, true>. */
            jxl_modular_properties_edge(&predictor, &props);
            sample = jxl_modular_grid_sample_as_i32(grid, x, y);
            if (delta_i < pos_count && positions[delta_i].x == x &&
                positions[delta_i].y == y) {
                int32_t diff = jxl_modular_predict(pal->d_pred, &props, 1);
                if (grid->kind == JXL_MODULAR_SAMPLE_I16) {
                    sample = (int32_t)jxl_modular_i16_add((int16_t)sample, (int16_t)diff);
                } else {
                    sample = jxl_modular_i32_add(sample, diff);
                }
                jxl_modular_grid_store_i32(grid, x, y, sample);
                ++delta_i;
                if (delta_i >= pos_count) {
                    jxl_modular_properties_record(&props, sample);
                    jxl_modular_predictor_state_free(alloc, &predictor);
                    return JXL_MODULAR_OK;
                }
            }
            jxl_modular_properties_record(&props, sample);
        }
    }
    jxl_modular_predictor_state_free(alloc, &predictor);
    return JXL_MODULAR_OK;
}

static int palette_indices_simple(const jxl_modular_grid_i32 *palette,
                                  const jxl_modular_grid_i32 *index);

static jxl_modular_status_t inverse_palette_simple(const jxl_modular_grid_i32 *palette,
                                                   jxl_modular_grid_i32 *index,
                                                   jxl_modular_grid_i32 **members,
                                                   size_t member_count);

static jxl_modular_status_t inverse_palette_tile(jxl_allocator_state *alloc,
                                                 const jxl_modular_grid_i32 *palette,
                                                 jxl_modular_grid_i32 *index,
                                                 jxl_modular_grid_i32 **members,
                                                 size_t member_count, uint32_t bit_depth,
                                                 const jxl_transform_palette *pal) {
                                                     size_t y;
    size_t pos_count;
    size_t num_c;
    size_t npixels;
    jxl_palette_delta_pos *positions;
    int32_t nb_deltas;
    size_t index_stride;
    jxl_modular_status_t st;
    if (palette == NULL || index == NULL || palette->height == 0) {
        return JXL_MODULAR_DECODER_ERROR;
    }
    num_c = member_count + 1;
    if (palette->height != num_c) {
        return JXL_MODULAR_DECODER_ERROR;
    }

    if (palette->width > 0 && palette_indices_simple(palette, index)) {
        return inverse_palette_simple(palette, index, members, member_count);
    }

    npixels = (size_t)index->width * index->height;
    positions = NULL;
    if (pal != NULL && npixels > 0) {
        positions = jxl_calloc(alloc, npixels, sizeof(*positions));
        if (positions == NULL) {
            return JXL_MODULAR_OUT_OF_MEMORY;
        }
    }
    pos_count = 0;
    nb_deltas = pal != NULL ? (int32_t)pal->nb_deltas : 0;
    index_stride = jxl_modular_grid_row_stride(index);
    (void)index_stride;
    for (y = 0; y < index->height; ++y) {
        size_t x;
        for (x = 0; x < index->width; ++x) {
            int32_t idx = grid_row_get_i32(index, x, y);
            if (pal != NULL && idx < nb_deltas) {
                jxl_palette_delta_pos compound_tmp;
                compound_tmp.x = x;
                compound_tmp.y = y;

                positions[pos_count++] = compound_tmp;

            }
            resolve_palette_pixel(palette, idx, index, members, member_count, bit_depth, x, y);
        }
    }

    st = JXL_MODULAR_OK;
    if (pal != NULL && pos_count > 0) {
        st = inverse_palette_apply_delta(alloc, index, positions, pos_count, pal);
        if (st == JXL_MODULAR_OK) {
            size_t c;
            for (c = 0; c < member_count; ++c) {
                st = inverse_palette_apply_delta(alloc, members[c], positions, pos_count, pal);
                if (st != JXL_MODULAR_OK) {
                    break;
                }
            }
        }
    }
    jxl_free(alloc, positions);
    return st;
}

static int palette_indices_simple(const jxl_modular_grid_i32 *palette,
                                  const jxl_modular_grid_i32 *index) {
                                      size_t y;
    int32_t nb_colors = (int32_t)palette->width;
    size_t stride = jxl_modular_grid_row_stride(index);
    (void)stride;
    for (y = 0; y < index->height; ++y) {
        size_t x;
        const int32_t *index_row = jxl_modular_grid_row_i32_const(index, y);
        const int16_t *index_row_i16 = NULL;
        if (index_row == NULL) {
            index_row_i16 = jxl_modular_grid_row_i16_const(index, y);
        }
        if (index_row == NULL && index_row_i16 == NULL) {
            return 0;
        }
        for (x = 0; x < index->width; ++x) {
            int32_t idx = index_row != NULL ? index_row[x] : (int32_t)index_row_i16[x];
            if (idx < 0 || idx >= nb_colors) {
                return 0;
            }
        }
    }
    return 1;
}

static jxl_modular_status_t inverse_palette_simple(const jxl_modular_grid_i32 *palette,
                                                   jxl_modular_grid_i32 *index,
                                                   jxl_modular_grid_i32 **members,
                                                   size_t member_count) {
                                                       size_t c;
                                                       size_t y;
    size_t num_c;
    int32_t nb_colors;
    if (palette == NULL || index == NULL || palette->width == 0 || palette->height == 0) {
        return JXL_MODULAR_DECODER_ERROR;
    }
    num_c = member_count + 1;
    if (palette->height != num_c) {
        return JXL_MODULAR_DECODER_ERROR;
    }
    if (!palette_indices_simple(palette, index)) {
        return JXL_MODULAR_DECODER_ERROR;
    }

    nb_colors = (int32_t)palette->width;
    (void)jxl_modular_grid_row_stride(palette);
    (void)jxl_modular_grid_row_stride(index);

    for (c = 0; c < member_count; ++c) {
        size_t y;
        jxl_modular_grid_i32 *grid = members[c];
        const int32_t *pal_row_i32 = NULL;
        const int16_t *pal_row_i16 = NULL;
        if (grid == NULL || grid->width != index->width || grid->height != index->height) {
            return JXL_MODULAR_DECODER_ERROR;
        }
        pal_row_i32 = jxl_modular_grid_row_i32_const(palette, c + 1);
        if (pal_row_i32 == NULL) {
            pal_row_i16 = jxl_modular_grid_row_i16_const(palette, c + 1);
        }
        if (pal_row_i32 == NULL && pal_row_i16 == NULL) {
            return JXL_MODULAR_DECODER_ERROR;
        }
        for (y = 0; y < index->height; ++y) {
            size_t x;
            const int32_t *index_row = jxl_modular_grid_row_i32_const(index, y);
            const int16_t *index_row_i16 = NULL;
            int32_t *grid_row = jxl_modular_grid_row_i32(grid, y);
            int16_t *grid_row_i16 = NULL;
            if (index_row == NULL) {
                index_row_i16 = jxl_modular_grid_row_i16_const(index, y);
            }
            if (grid_row == NULL) {
                grid_row_i16 = jxl_modular_grid_row_i16(grid, y);
            }
            if ((index_row == NULL && index_row_i16 == NULL) ||
                (grid_row == NULL && grid_row_i16 == NULL)) {
                return JXL_MODULAR_DECODER_ERROR;
            }
            for (x = 0; x < index->width; ++x) {
                int32_t idx = index_row != NULL ? index_row[x] : (int32_t)index_row_i16[x];
                int32_t val = pal_row_i32 != NULL ? pal_row_i32[idx] : (int32_t)pal_row_i16[idx];
                if (grid_row != NULL) {
                    grid_row[x] = val;
                } else {
                    grid_row_i16[x] = (int16_t)val;
                }
            }
        }
    }

    for (y = 0; y < index->height; ++y) {
        size_t x;
        const int32_t *pal0_i32 = jxl_modular_grid_row_i32_const(palette, 0);
        const int16_t *pal0_i16 = pal0_i32 == NULL ? jxl_modular_grid_row_i16_const(palette, 0) : NULL;
        const int32_t *index_row = jxl_modular_grid_row_i32_const(index, y);
        const int16_t *index_row_i16 = index_row == NULL ? jxl_modular_grid_row_i16_const(index, y) : NULL;
        int32_t *out_row = jxl_modular_grid_row_i32(index, y);
        int16_t *out_row_i16 = out_row == NULL ? jxl_modular_grid_row_i16(index, y) : NULL;
        if ((pal0_i32 == NULL && pal0_i16 == NULL) || (index_row == NULL && index_row_i16 == NULL) ||
            (out_row == NULL && out_row_i16 == NULL)) {
            return JXL_MODULAR_DECODER_ERROR;
        }
        for (x = 0; x < index->width; ++x) {
            int32_t idx = index_row != NULL ? index_row[x] : (int32_t)index_row_i16[x];
            int32_t val = pal0_i32 != NULL ? pal0_i32[idx] : (int32_t)pal0_i16[idx];
            if (out_row != NULL) {
                out_row[x] = val;
            } else {
                out_row_i16[x] = (int16_t)val;
            }
        }
    }
    return JXL_MODULAR_OK;
}

static int grid_buf_used_in_meta(const jxl_modular_image_destination *dest, const int32_t *buf) {
    size_t i;
    if (dest == NULL || buf == NULL) {
        return 0;
    }
    for (i = 0; i < dest->meta_channels_len; ++i) {
        if (dest->meta_channels[i].buf == buf) {
            return 1;
        }
    }
    return 0;
}

static int grid_buf_used_elsewhere(const jxl_modular_image_destination *dest, size_t idx) {
    size_t i;
    const jxl_modular_grid_i32 *g = inv_leader_const(&inv_ch((jxl_modular_image_destination *)dest)[idx]);
    if (g == NULL || g->buf == NULL) {
        return 0;
    }
    for (i = 0; i < inv_len(dest); ++i) {
        if (i != idx && inv_leader_const(&inv_ch((jxl_modular_image_destination *)dest)[i]) != NULL &&
            inv_leader_const(&inv_ch((jxl_modular_image_destination *)dest)[i])->buf == g->buf) {
            return 1;
        }
    }
    return grid_buf_used_in_meta(dest, g->buf);
}

static void dest_remove_image_channels(jxl_allocator_state *alloc,
                                       jxl_modular_image_destination *dest, size_t from,
                                       size_t count) {
    size_t i;
    size_t tail;
    if (count == 0 || from >= inv_len(dest)) {
        return;
    }
    if (from + count > inv_len(dest)) {
        count = inv_len(dest) - from;
    }
    for (i = from; i < from + count; ++i) {
        if (!grid_buf_used_elsewhere(dest, i)) {
            jxl_modular_grid_i32 *g = inv_leader(&inv_ch(dest)[i]);
            if (g != NULL) {
                jxl_modular_grid_i32_destroy(alloc, g);
            }
        }
        jxl_transformed_grid_teardown(alloc, &inv_ch(dest)[i]);
        jxl_transformed_grid_init_empty(&inv_ch(dest)[i]);
    }
    tail = inv_len(dest) - from - count;
    if (tail > 0) {
        memmove(&inv_ch(dest)[from], &inv_ch(dest)[from + count],
                tail * sizeof(*inv_ch(dest)));
    }
    *inv_len_ptr(dest) -= count;

    if (from + count <= dest->channels.info_len) {
        size_t info_tail = dest->channels.info_len - from - count;
        if (info_tail > 0) {
            memmove(&dest->channels.info[from], &dest->channels.info[from + count],
                    info_tail * sizeof(*dest->channels.info));
        }
        dest->channels.info_len -= count;
    }
}

static int grids_mergeable_h(const jxl_modular_grid_i32 *main, const jxl_modular_grid_i32 *residu) {
    size_t stride = jxl_modular_grid_row_stride(main);
    return main->buf != NULL && residu->buf == main->buf && jxl_modular_grid_row_stride(residu) == stride &&
           main->height == residu->height &&
           residu->offset == main->offset + main->width;
}

static int grids_mergeable_v(const jxl_modular_grid_i32 *main, const jxl_modular_grid_i32 *residu) {
    size_t stride = jxl_modular_grid_row_stride(main);
    return main->buf != NULL && residu->buf == main->buf && jxl_modular_grid_row_stride(residu) == stride &&
           main->width == residu->width &&
           residu->offset == main->offset + main->height * stride;
}

static jxl_modular_status_t inverse_squeeze_channel(jxl_context *ctx, jxl_allocator_state *alloc,
                                                    jxl_modular_grid_i32 *main,
                                                    const jxl_modular_grid_i32 *residu,
                                                    int horizontal) {
    int narrow = main->kind == JXL_MODULAR_SAMPLE_I16;
    size_t merged_w;
    size_t merged_h;
    int32_t *merged;
    size_t out_stride;
    jxl_modular_grid_normalize_stride(main);
    if (horizontal && grids_mergeable_h(main, residu)) {
        size_t stride;
	merged_w = main->width + residu->width;
        merged_h = main->height;
        stride = jxl_modular_grid_row_stride(main);
        if (narrow) {
            jxl_squeeze_inverse_h_i16(ctx, alloc, (int16_t *)main->buf + main->offset, merged_w, merged_h,
                                      stride);
        } else {
            jxl_squeeze_inverse_h_i32(alloc, (int32_t *)main->buf + main->offset, merged_w, merged_h,
                                      stride);
        }
        main->width = merged_w;
        main->stride = stride;
        return JXL_MODULAR_OK;
    }
    if (!horizontal && grids_mergeable_v(main, residu)) {
        size_t stride;
	merged_w = main->width;
        merged_h = main->height + residu->height;
        stride = jxl_modular_grid_row_stride(main);
        if (narrow) {
            jxl_squeeze_inverse_v_i16(ctx, alloc, (int16_t *)main->buf + main->offset, merged_w, merged_h,
                                      stride);
        } else {
            jxl_squeeze_inverse_v_i32(alloc, (int32_t *)main->buf + main->offset, merged_w, merged_h,
                                      stride);
        }
        main->height = merged_h;
        main->stride = stride;
        return JXL_MODULAR_OK;
    }

    if (horizontal) {
        if (main->height != residu->height) {
            return JXL_MODULAR_DECODER_ERROR;
        }
        merged_w = main->width + residu->width;
        merged_h = main->height;
    } else {
        if (main->width != residu->width) {
            return JXL_MODULAR_DECODER_ERROR;
        }
        merged_w = main->width;
        merged_h = main->height + residu->height;
    }

    if (narrow) {
        int16_t *merged = jxl_alloc(alloc, merged_w * merged_h * sizeof(*merged));
        if (merged == NULL) {
            return JXL_MODULAR_OUT_OF_MEMORY;
        }
        if (horizontal) {
            size_t y;
            for (y = 0; y < merged_h; ++y) {
                size_t x;
                const int16_t *main_row = jxl_modular_grid_row_i16_const(main, y);
                const int16_t *res_row = jxl_modular_grid_row_i16_const(residu, y);
                if (main_row == NULL || res_row == NULL) {
                    jxl_free(alloc, merged);
                    return JXL_MODULAR_DECODER_ERROR;
                }
                for (x = 0; x < main->width; ++x) {
                    merged[y * merged_w + x] = main_row[x];
                }
                for (x = 0; x < residu->width; ++x) {
                    merged[y * merged_w + main->width + x] = res_row[x];
                }
            }
            jxl_squeeze_inverse_h_i16(ctx, alloc, merged, merged_w, merged_h, merged_w);
        } else {
            size_t y;
            for (y = 0; y < main->height; ++y) {
                size_t x;
                const int16_t *main_row = jxl_modular_grid_row_i16_const(main, y);
                if (main_row == NULL) {
                    jxl_free(alloc, merged);
                    return JXL_MODULAR_DECODER_ERROR;
                }
                for (x = 0; x < merged_w; ++x) {
                    merged[y * merged_w + x] = main_row[x];
                }
            }
            for (y = 0; y < residu->height; ++y) {
                size_t x;
                const int16_t *res_row = jxl_modular_grid_row_i16_const(residu, y);
                if (res_row == NULL) {
                    jxl_free(alloc, merged);
                    return JXL_MODULAR_DECODER_ERROR;
                }
                for (x = 0; x < merged_w; ++x) {
                    merged[(main->height + y) * merged_w + x] = res_row[x];
                }
            }
            jxl_squeeze_inverse_v_i16(ctx, alloc, merged, merged_w, merged_h, merged_w);
        }
        if (horizontal) {
            size_t y;
            for (y = 0; y < merged_h; ++y) {
                int16_t *main_row = jxl_modular_grid_row_i16(main, y);
                if (main_row == NULL) {
                    jxl_free(alloc, merged);
                    return JXL_MODULAR_DECODER_ERROR;
                }
                memcpy(main_row, merged + y * merged_w, merged_w * sizeof(*merged));
            }
            main->width = merged_w;
        } else {
            size_t y;
            for (y = 0; y < merged_h; ++y) {
                int16_t *main_row = jxl_modular_grid_row_i16(main, y);
                if (main_row == NULL) {
                    jxl_free(alloc, merged);
                    return JXL_MODULAR_DECODER_ERROR;
                }
                memcpy(main_row, merged + y * merged_w, merged_w * sizeof(*merged));
            }
            main->height = merged_h;
        }
        jxl_modular_grid_normalize_stride(main);
        jxl_free(alloc, merged);
        return JXL_MODULAR_OK;
    }

    merged = jxl_alloc(alloc, merged_w * merged_h * sizeof(*merged));
    if (merged == NULL) {
        return JXL_MODULAR_OUT_OF_MEMORY;
    }

    if (horizontal) {
        size_t y;
        for (y = 0; y < merged_h; ++y) {
            size_t x;
            const int32_t *main_row = jxl_modular_grid_row_i32_const(main, y);
            const int32_t *res_row = jxl_modular_grid_row_i32_const(residu, y);
            if (main_row == NULL || res_row == NULL) {
                jxl_free(alloc, merged);
                return JXL_MODULAR_DECODER_ERROR;
            }
            for (x = 0; x < main->width; ++x) {
                merged[y * merged_w + x] = main_row[x];
            }
            for (x = 0; x < residu->width; ++x) {
                merged[y * merged_w + main->width + x] = res_row[x];
            }
        }
        jxl_squeeze_inverse_h_i32(alloc, merged, merged_w, merged_h, merged_w);
    } else {
        size_t y;
        for (y = 0; y < main->height; ++y) {
            size_t x;
            const int32_t *main_row = jxl_modular_grid_row_i32_const(main, y);
            if (main_row == NULL) {
                jxl_free(alloc, merged);
                return JXL_MODULAR_DECODER_ERROR;
            }
            for (x = 0; x < merged_w; ++x) {
                merged[y * merged_w + x] = main_row[x];
            }
        }
        for (y = 0; y < residu->height; ++y) {
            size_t x;
            const int32_t *res_row = jxl_modular_grid_row_i32_const(residu, y);
            if (res_row == NULL) {
                jxl_free(alloc, merged);
                return JXL_MODULAR_DECODER_ERROR;
            }
            for (x = 0; x < merged_w; ++x) {
                merged[(main->height + y) * merged_w + x] = res_row[x];
            }
        }
        jxl_squeeze_inverse_v_i32(alloc, merged, merged_w, merged_h, merged_w);
    }

    out_stride = jxl_modular_grid_row_stride(main);
    if (horizontal) {
        size_t y;
        for (y = 0; y < merged_h; ++y) {
            int32_t *main_row = jxl_modular_grid_row_i32(main, y);
            if (main_row == NULL) {
                jxl_free(alloc, merged);
                return JXL_MODULAR_DECODER_ERROR;
            }
            memcpy(main_row, merged + y * merged_w, merged_w * sizeof(*merged));
        }
        main->width = merged_w;
    } else {
        size_t y;
        for (y = 0; y < merged_h; ++y) {
            int32_t *main_row = jxl_modular_grid_row_i32(main, y);
            if (main_row == NULL) {
                jxl_free(alloc, merged);
                return JXL_MODULAR_DECODER_ERROR;
            }
            memcpy(main_row, merged + y * merged_w, merged_w * sizeof(*merged));
        }
        main->height = merged_h;
    }
    main->stride = out_stride;
    jxl_free(alloc, merged);
    return JXL_MODULAR_OK;
}

static jxl_modular_status_t inverse_palette_grids(jxl_allocator_state *alloc, jxl_transformed_grid **grids, size_t *grids_len,
                                                uint32_t bit_depth,
                                                const jxl_transform_palette *pal,
                                                jxl_modular_image_destination *dest) {
    int dest_work_grids = grids_are_dest_work(dest, (const jxl_transformed_grid **)grids);
    size_t members_to_insert;
    size_t count;
    const jxl_modular_grid_i32 *palette_table;
    size_t leader_idx;
    jxl_transformed_grid *leader_slot;
    jxl_modular_grid_i32 *leader;
    size_t member_count;
    jxl_transformed_grid *unmerged;
    jxl_modular_grid_i32 **member_ptrs;
    jxl_modular_status_t st;
    size_t insert_at;
    if (grids == NULL || grids_len == NULL || pal == NULL || *grids_len == 0) {
        return JXL_MODULAR_DECODER_ERROR;
    }

    count = *grids_len;
    palette_table = inv_leader_const(&(*grids)[0]);
    leader_idx = (size_t)pal->begin_c + 1;
    if (palette_table == NULL || leader_idx >= count) {
        return JXL_MODULAR_DECODER_ERROR;
    }
    leader_slot = &(*grids)[leader_idx];
    leader = inv_leader(leader_slot);
    if (leader == NULL) {
        return JXL_MODULAR_DECODER_ERROR;
    }

    member_count = pal->num_c > 1 ? (size_t)pal->num_c - 1 : 0;
    unmerged = NULL;
    member_ptrs = NULL;
    if (member_count > 0) {
        size_t m;
        jxl_modular_status_t ust =
            jxl_transformed_grid_unmerge(alloc, leader_slot, member_count, &unmerged);
        if (ust != JXL_MODULAR_OK) {
            return ust;
        }
        member_ptrs = jxl_calloc(alloc, member_count, sizeof(*member_ptrs));
        if (member_ptrs == NULL) {
            jxl_free(alloc, unmerged);
            return JXL_MODULAR_OUT_OF_MEMORY;
        }
        for (m = 0; m < member_count; ++m) {
            member_ptrs[m] = inv_leader(&unmerged[m]);
            if (member_ptrs[m] == NULL) {
                jxl_free(alloc, member_ptrs);
                jxl_free(alloc, unmerged);
                return JXL_MODULAR_DECODER_ERROR;
            }
        }
    }

    st = inverse_palette_tile(alloc, palette_table, leader, member_ptrs, member_count, bit_depth, pal);
    jxl_free(alloc, member_ptrs);
    if (st != JXL_MODULAR_OK) {
        jxl_free(alloc, unmerged);
        return st;
    }

    members_to_insert = member_count;
    insert_at = (size_t)pal->begin_c + 1;

    if (dest_work_grids && dest != NULL) {
        jxl_modular_grid_i32 *palette_g = inv_leader(&(*grids)[0]);
        if (palette_g != NULL && palette_g->buf != NULL && !grid_buf_used_elsewhere(dest, 0)) {
            jxl_modular_grid_i32_destroy(alloc, palette_g);
        }
    }
    jxl_transformed_grid_teardown(alloc, &(*grids)[0]);
    memmove(&(*grids)[0], &(*grids)[1], (count - 1) * sizeof(**grids));
    --(*grids_len);
    count = *grids_len;

    if (members_to_insert > 0) {
        if (insert_at > count) {
            jxl_free(alloc, unmerged);
            return JXL_MODULAR_DECODER_ERROR;
        }
        st = jxl_transformed_grids_insert_at(alloc, grids, grids_len, insert_at, unmerged,
                                           members_to_insert);
        jxl_free(alloc, unmerged);
        if (st != JXL_MODULAR_OK) {
            return st;
        }
    } else {
        jxl_free(alloc, unmerged);
    }

    return JXL_MODULAR_OK;
}

/* Squeeze inverse on a grid array (Rust Squeeze::inverse). */
static jxl_modular_status_t inverse_squeeze_grids(jxl_context *ctx, jxl_allocator_state *alloc,
                                                  jxl_transformed_grid **grids, size_t *grids_len,
                                                  const jxl_transform_squeeze *sq,
                                                  jxl_modular_image_destination *dest) {
                                                      size_t ti;
    if (sq == NULL || sq->sp == NULL || grids == NULL || grids_len == NULL) {
        return JXL_MODULAR_DECODER_ERROR;
    }
    int use_dest_work = grids_are_dest_work(dest, (const jxl_transformed_grid **)grids);

    for (ti = sq->sp_len; ti > 0; --ti) {
        size_t i;
        size_t end;
        size_t residual_start;
        const jxl_squeeze_params *sp = &sq->sp[ti - 1];
        size_t begin = sp->begin_c;
        size_t n = sp->num_c;
        size_t count = *grids_len;
        end = begin + n;
        if (end > count) {
            return JXL_MODULAR_DECODER_ERROR;
        }

        residual_start = end;
        if (!sp->in_place) {
            if (count < n) {
                return JXL_MODULAR_DECODER_ERROR;
            }
            residual_start = count - n;
        }
        if (residual_start + n > count) {
            return JXL_MODULAR_DECODER_ERROR;
        }

        for (i = 0; i < n; ++i) {
            jxl_modular_grid_i32 *main_g = inv_leader(&(*grids)[begin + i]);
            jxl_modular_grid_i32 *res_g = inv_leader(&(*grids)[residual_start + i]);
            jxl_modular_status_t st;
            if (main_g == NULL || res_g == NULL) {
                return JXL_MODULAR_DECODER_ERROR;
            }
            st = inverse_squeeze_channel(ctx, alloc, main_g, res_g, sp->horizontal);
            if (st != JXL_MODULAR_OK) {
                return st;
            }
            if (use_dest_work && begin + i < dest->channels.info_len) {
                dest->channels.info[begin + i].width = (uint32_t)main_g->width;
                dest->channels.info[begin + i].height = (uint32_t)main_g->height;
            }
        }

        if (use_dest_work) {
            dest_remove_image_channels(alloc, dest, residual_start, n);
        } else if (sp->in_place) {
            if (end + n <= count) {
                memmove(&(*grids)[end], &(*grids)[end + n], (count - end - n) * sizeof(**grids));
            }
            *grids_len = count - n;
        } else {
            *grids_len = count - n;
        }
    }
    return JXL_MODULAR_OK;
}

static void sync_channels_from_grids(jxl_allocator_state *alloc, jxl_modular_image_destination *dest) {
    size_t i;
    size_t n = inv_len(dest);
    if (n == 0) {
        jxl_modular_channels_free(alloc, &dest->channels);
        jxl_modular_channels_init(&dest->channels);
        return;
    }
    if (dest->channels.info_len > n) {
        dest->channels.info_len = n;
    }
    if (dest->channels.info_len < n) {
        size_t i;
        if (jxl_modular_channels_reserve(alloc, &dest->channels, n) != JXL_MODULAR_OK) {
            return;
        }
        for (i = dest->channels.info_len; i < n; ++i) {
            const jxl_modular_grid_i32 *leader = inv_leader_const(&inv_ch(dest)[i]);
            dest->channels.info[i] = jxl_modular_channel_info_new_unshiftable(
                leader != NULL ? (uint32_t)leader->width : 0,
                leader != NULL ? (uint32_t)leader->height : 0);
        }
        dest->channels.info_len = n;
    }
    dest->channels.nb_meta_channels = 0;
    for (i = 0; i < n; ++i) {
        const jxl_modular_grid_i32 *leader = inv_leader_const(&inv_ch(dest)[i]);
        uint32_t ow = dest->channels.info[i].original_width;
        uint32_t oh = dest->channels.info[i].original_height;
        jxl_channel_shift oshift = dest->channels.info[i].original_shift;
        dest->channels.info[i].width = leader != NULL ? (uint32_t)leader->width : 0;
        dest->channels.info[i].height = leader != NULL ? (uint32_t)leader->height : 0;
        dest->channels.info[i].hshift = -1;
        dest->channels.info[i].vshift = -1;
        if (ow != 0) {
            dest->channels.info[i].original_width = ow;
        }
        if (oh != 0) {
            dest->channels.info[i].original_height = oh;
        }
        dest->channels.info[i].original_shift = oshift;
    }
}

jxl_modular_status_t jxl_modular_subimage_finish(jxl_context *ctx, jxl_allocator_state *alloc,
                                                 const jxl_modular_header *header,
                                                 jxl_transformed_grid **grids, size_t *grids_len,
                                                 uint32_t bit_depth,
                                                 jxl_modular_image_destination *dest,
                                                 const jxl_modular_params *mod_params) {
                                                     size_t t;
    (void)mod_params;
    if (header == NULL || grids == NULL || grids_len == NULL) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
    if (*grids == NULL || *grids_len == 0) {
        return JXL_MODULAR_OK;
    }

    jxl_modular_status_t overall = JXL_MODULAR_OK;
    for (t = header->transform_len; t > 0 && overall == JXL_MODULAR_OK; --t) {
        const jxl_transform_info *tr = &header->transform[t - 1];
        size_t active = *grids_len;
        if (JXL_DEBUG_FLAG(ctx, debug_inverse)) {
            fprintf(stderr, "inverse t%zu kind=%d ch=%zu\n", t - 1, (int)tr->kind, active);
        }
        switch (tr->kind) {
        case JXL_TRANSFORM_KIND_RCT: {
            uint32_t begin_c = tr->u.rct.begin_c;
            if ((size_t)begin_c + 3 > active) {
                overall = JXL_MODULAR_DECODER_ERROR;
                break;
            }
            overall = inverse_rct_grids(inv_leader(&(*grids)[begin_c]),
                                        inv_leader(&(*grids)[begin_c + 1]),
                                        inv_leader(&(*grids)[begin_c + 2]), &tr->u.rct, dest,
                                        t == 1, jxl_context_cpu_features(ctx));
            break;
        }
        case JXL_TRANSFORM_KIND_PALETTE:
            overall = inverse_palette_grids(alloc, grids, grids_len, bit_depth, &tr->u.palette, dest);
            break;
        case JXL_TRANSFORM_KIND_SQUEEZE:
            overall = inverse_squeeze_grids(ctx, alloc, grids, grids_len, &tr->u.squeeze, dest);
            break;
        }
    }
    return overall;
}

jxl_modular_status_t jxl_modular_image_apply_inverse_transforms(
    jxl_context *ctx, jxl_allocator_state *alloc, jxl_modular_image_destination *dest,
    uint32_t frame_width, uint32_t frame_height, uint32_t bit_depth,
    const jxl_modular_params *mod_params) {
    jxl_transformed_grid **storage;
    size_t *len;
    jxl_modular_status_t st;
    (void)frame_width;
    (void)frame_height;
    if (dest == NULL) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }

    storage = inv_ch_ptr(dest);
    len = inv_len_ptr(dest);
    if (storage == NULL || len == NULL) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }

    st = jxl_modular_subimage_finish(
        ctx, alloc, &dest->header, storage, len, bit_depth != 0 ? bit_depth : dest->bit_depth, dest,
        mod_params);
    if (st != JXL_MODULAR_OK) {
        return st;
    }

    sync_channels_from_grids(alloc, dest);
    return JXL_MODULAR_OK;
}
