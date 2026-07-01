// SPDX-License-Identifier: MIT OR Apache-2.0
#include "group_subimage.h"

#include "modular/modular_parse.h"
#include "modular/prepare_subimage.h"
#include "modular/recursive_image.h"
#include "modular/subimage_decode.h"
#include "modular/transform/inverse.h"

#include "allocator.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

static uint32_t trailing_zeros_u32(uint32_t v) {
    uint32_t n;
    if (v == 0) {
        return 0;
    }
#if defined(__GNUC__) || defined(__clang__)
    return (uint32_t)__builtin_ctz(v);
#else
    n = 0;
    while ((v & 1u) == 0) {
        v >>= 1;
        ++n;
    }
    return n;
#endif
}

typedef struct {
    uint32_t pass_idx;
    int32_t min_shift;
    int32_t max_shift;
} jxl_modular_pass_shift;

static void pass_shifts_upsert(jxl_modular_pass_shift *out, size_t *n, size_t cap, uint32_t pass_idx,
                               int32_t min_shift, int32_t max_shift) {
                                   size_t i;
    for (i = 0; i < *n; ++i) {
        if (out[i].pass_idx == pass_idx) {
            out[i].min_shift = min_shift;
            out[i].max_shift = max_shift;
            return;
        }
    }
    if (*n < cap) {
        jxl_modular_pass_shift compound_tmp;
        compound_tmp.pass_idx = pass_idx;
        compound_tmp.min_shift = min_shift;
        compound_tmp.max_shift = max_shift;
        out[(*n)++] = compound_tmp;

    }
}

static size_t build_pass_shifts(const jxl_frame_passes *passes, jxl_modular_pass_shift *out,
                                size_t cap) {
                                    size_t i;
    int32_t maxshift = 3;
    size_t n = 0;
    size_t pairs = passes->downsample_len < passes->last_pass_len ? passes->downsample_len
                                                                  : passes->last_pass_len;
    for (i = 0; i < pairs; ++i) {
        int32_t minshift = (int32_t)trailing_zeros_u32(passes->downsample[i]);
        pass_shifts_upsert(out, &n, cap, passes->last_pass[i], minshift, maxshift);
        maxshift = minshift;
    }
    pass_shifts_upsert(out, &n, cap, passes->num_passes > 0 ? passes->num_passes - 1u : 0u, 0,
                       maxshift);
    return n;
}

static int pass_for_shift(const jxl_modular_pass_shift *ps, size_t ps_len, int32_t shift) {
    size_t i;
    int best = -1;
    uint32_t best_pass = UINT32_MAX;
    for (i = 0; i < ps_len; ++i) {
        if (shift >= ps[i].min_shift && shift < ps[i].max_shift && ps[i].pass_idx < best_pass) {
            best_pass = ps[i].pass_idx;
            best = (int)ps[i].pass_idx;
        }
    }
    return best;
}

static uint32_t group_dim_shift(uint32_t group_dim) {
    return trailing_zeros_u32(group_dim);
}

static size_t skip_gmodular_channels(const jxl_modular_channels *layout, uint32_t group_dim) {
    size_t i;
    for (i = 0; i < layout->info_len; ++i) {
        const jxl_modular_channel_info *info = &layout->info[i];
        if (i < layout->nb_meta_channels || (info->width <= group_dim && info->height <= group_dim)) {
            continue;
        }
        return i;
    }
    return layout->info_len;
}

void jxl_modular_transformed_subimage_init(jxl_modular_transformed_subimage *sub) {
    if (sub != NULL) {
        memset(sub, 0, sizeof(*sub));
        jxl_modular_channels_init(&sub->channels);
        jxl_modular_header_ma_init(&sub->hm);
    }
}

static void subimage_teardown_header(jxl_allocator_state *alloc,
                                     jxl_modular_transformed_subimage *sub) {
    if (sub == NULL) {
        return;
    }
    if (alloc != NULL) {
        jxl_modular_header_ma_free(alloc, &sub->hm);
    } else if (sub->hm.header.transform != NULL || sub->hm.ma_owns) {
        jxl_modular_header_free(alloc, &sub->hm.header);
        if (sub->hm.ma_owns) {
            jxl_ma_config_init(&sub->hm.ma_ctx);
        }
        sub->hm.ma_owns = 0;
    } else {
        jxl_modular_header_ma_init(&sub->hm);
    }
}

void jxl_modular_subimage_teardown_prepared(jxl_allocator_state *alloc,
                                            jxl_modular_transformed_subimage *sub) {
    if (sub == NULL) {
        return;
    }
    if (sub->grids != NULL) {
        jxl_transformed_grids_teardown(alloc, sub->grids, sub->grids_len);
    }
    jxl_free(alloc, sub->grids);
    sub->grids = NULL;
    sub->grids_len = 0;
    sub->prepared = 0;
}

int jxl_modular_transformed_subimage_is_prepared(const jxl_modular_transformed_subimage *sub) {
    return sub != NULL && sub->prepared;
}

void jxl_modular_transformed_subimage_free(jxl_allocator_state *alloc,
                                           jxl_modular_transformed_subimage *sub) {
    if (sub == NULL) {
        return;
    }
    jxl_modular_subimage_teardown_prepared(alloc, sub);
    subimage_teardown_header(NULL, sub);
    jxl_modular_channels_free(alloc, &sub->channels);
    jxl_free(alloc, sub->channel_indices);
    jxl_free(alloc, sub->tiles);
    jxl_modular_transformed_subimage_init(sub);
}

int jxl_modular_transformed_subimage_is_empty(const jxl_modular_transformed_subimage *sub) {
    return sub == NULL || sub->tile_count == 0;
}

int jxl_modular_transformed_subimage_is_partial(const jxl_modular_transformed_subimage *sub) {
    return sub != NULL && sub->partial;
}

void jxl_modular_global_groups_init(jxl_modular_global_groups *groups) {
    if (groups != NULL) {
        memset(groups, 0, sizeof(*groups));
    }
}

void jxl_modular_global_groups_free(jxl_allocator_state *alloc,
                                    jxl_modular_global_groups *groups) {
    if (groups == NULL) {
        return;
    }
    if (groups->lf_groups != NULL) {
        size_t i;
        for (i = 0; i < groups->num_lf_groups; ++i) {
            jxl_modular_transformed_subimage_free(alloc, &groups->lf_groups[i]);
        }
        jxl_free(alloc, groups->lf_groups);
    }
    if (groups->pass_groups != NULL) {
        size_t i;
        size_t n = groups->num_passes * groups->num_groups;
        for (i = 0; i < n; ++i) {
            jxl_modular_transformed_subimage_free(alloc, &groups->pass_groups[i]);
        }
        jxl_free(alloc, groups->pass_groups);
    }
    jxl_modular_global_groups_init(groups);
}

static jxl_modular_status_t subimage_reserve_tiles(jxl_allocator_state *alloc, jxl_modular_transformed_subimage *sub,
                                                   size_t need) {
    size_t cap;
    jxl_modular_pg_tile *grown;
    if (sub == NULL) {
        return JXL_MODULAR_DECODER_ERROR;
    }
    if (need <= sub->tile_cap) {
        return JXL_MODULAR_OK;
    }
    cap = sub->tile_cap == 0 ? 8 : sub->tile_cap;
    while (cap < need) {
        cap *= 2;
    }
    grown = jxl_realloc(alloc, sub->tiles, cap * sizeof(*sub->tiles));
    if (grown == NULL) {
        return JXL_MODULAR_OUT_OF_MEMORY;
    }
    sub->tiles = grown;
    sub->tile_cap = cap;
    return JXL_MODULAR_OK;
}

static jxl_modular_status_t subimage_push_tile(jxl_allocator_state *alloc, jxl_modular_transformed_subimage *sub,
                                               size_t global_channel_idx,
                                               const jxl_modular_pg_tile *tile) {
    jxl_modular_status_t st;
    size_t *grown;
    if (sub == NULL || tile == NULL) {
        return JXL_MODULAR_DECODER_ERROR;
    }
    st = subimage_reserve_tiles(alloc, sub, sub->tile_count + 1);
    if (st != JXL_MODULAR_OK) {
        return st;
    }
    st = jxl_modular_channels_push(alloc, &sub->channels, tile->info);
    if (st != JXL_MODULAR_OK) {
        return st;
    }
    grown = jxl_realloc(alloc, sub->channel_indices, (sub->channel_indices_len + 1) * sizeof(*sub->channel_indices));
    if (grown == NULL) {
        return JXL_MODULAR_OUT_OF_MEMORY;
    }
    sub->channel_indices = grown;
    sub->channel_indices[sub->channel_indices_len++] = global_channel_idx;
    sub->tiles[sub->tile_count++] = *tile;
    sub->partial = 1;
    return JXL_MODULAR_OK;
}

static jxl_modular_status_t ensure_lf_groups(jxl_allocator_state *alloc, jxl_modular_global_groups *groups, size_t count) {
    size_t i;
    jxl_modular_transformed_subimage *grown;
    if (groups->num_lf_groups >= count) {
        return JXL_MODULAR_OK;
    }
    grown = jxl_realloc(alloc, groups->lf_groups, count * sizeof(*groups->lf_groups));
    if (grown == NULL) {
        return JXL_MODULAR_OUT_OF_MEMORY;
    }
    for (i = groups->num_lf_groups; i < count; ++i) {
        jxl_modular_transformed_subimage_init(&grown[i]);
    }
    groups->lf_groups = grown;
    groups->num_lf_groups = count;
    return JXL_MODULAR_OK;
}

static jxl_modular_status_t ensure_pass_groups(jxl_allocator_state *alloc, jxl_modular_global_groups *groups,
                                               size_t num_passes, size_t num_groups) {
    size_t new_total;
    size_t new_passes;
    size_t new_groups;
    jxl_modular_transformed_subimage *grown;
    if (groups->num_passes == num_passes && groups->num_groups >= num_groups) {
        return JXL_MODULAR_OK;
    }
    new_passes = groups->num_passes > num_passes ? groups->num_passes : num_passes;
    new_groups = groups->num_groups > num_groups ? groups->num_groups : num_groups;
    new_total = new_passes * new_groups;
    grown = jxl_calloc(alloc, new_total, sizeof(*groups->pass_groups));
    if (grown == NULL) {
        return JXL_MODULAR_OUT_OF_MEMORY;
    }
    if (groups->pass_groups != NULL && groups->num_passes > 0 && groups->num_groups > 0) {
        size_t p;
        for (p = 0; p < groups->num_passes && p < new_passes; ++p) {
            size_t g;
            for (g = 0; g < groups->num_groups && g < new_groups; ++g) {
                grown[p * new_groups + g] = groups->pass_groups[p * groups->num_groups + g];
            }
        }
        jxl_free(alloc, groups->pass_groups);
    }
    groups->pass_groups = grown;
    groups->num_passes = new_passes;
    groups->num_groups = new_groups;
    return JXL_MODULAR_OK;
}

static jxl_modular_transformed_subimage *pass_group_slot(jxl_modular_global_groups *groups,
                                                         uint32_t pass_idx, uint32_t group_idx) {
    if (groups == NULL || groups->pass_groups == NULL || pass_idx >= groups->num_passes ||
        group_idx >= groups->num_groups) {
        return NULL;
    }
    return &groups->pass_groups[(size_t)pass_idx * groups->num_groups + (size_t)group_idx];
}

jxl_modular_status_t jxl_modular_prepare_global_groups(jxl_allocator_state *alloc,
                                                     jxl_modular_image_destination *dest,
                                                       const jxl_frame_header *frame_header,
                                                       jxl_modular_global_groups *out) {
                                                           size_t i;
    jxl_modular_pass_shift ps[8];
    jxl_modular_status_t st;
    const jxl_modular_channels *layout;
    size_t ps_len;
    uint32_t gd;
    uint32_t gds;
    size_t first_large;
    size_t num_lf;
    size_t num_groups;
    size_t num_passes;
    if (dest == NULL || frame_header == NULL || out == NULL || dest->group_dim == 0) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
    jxl_modular_global_groups_free(alloc, out);
    jxl_modular_global_groups_init(out);

    st = jxl_modular_image_prepare_subimage_grids(alloc, dest);
    if (st != JXL_MODULAR_OK) {
        return st;
    }

    layout = jxl_modular_dest_subimage_channels(dest);
    if (layout == NULL) {
        return JXL_MODULAR_DECODER_ERROR;
    }

    ps_len = build_pass_shifts(&frame_header->passes, ps, sizeof(ps) / sizeof(ps[0]));
    gd = dest->group_dim;
    gds = group_dim_shift(gd);
    first_large = skip_gmodular_channels(layout, gd);
    num_lf = jxl_frame_header_num_lf_groups(frame_header);
    num_groups = jxl_frame_header_num_groups(frame_header);
    num_passes = frame_header->passes.num_passes;

    st = ensure_lf_groups(alloc, out, num_lf);
    if (st != JXL_MODULAR_OK) {
        return st;
    }
    st = ensure_pass_groups(alloc, out, num_passes > 0 ? num_passes : 1, num_groups > 0 ? num_groups : 1);
    if (st != JXL_MODULAR_OK) {
        return st;
    }

    for (i = first_large; i < layout->info_len; ++i) {
        const jxl_modular_channel_info *info = &layout->info[i];
        jxl_modular_grid *parent;
        if (i < layout->nb_meta_channels) {
            continue;
        }
        if (info->hshift < 0 || info->vshift < 0) {
            continue;
        }

        parent = jxl_modular_dest_channel_grid(dest, i);
        if (parent == NULL || parent->buf == NULL) {
            continue;
        }

        if (info->hshift < 3 || info->vshift < 3) {
            size_t gi;
            uint32_t group_width;
            uint32_t group_height;
            int32_t shift = info->hshift < info->vshift ? info->hshift : info->vshift;
            uint32_t num_cols;
            uint32_t num_rows;
            size_t group_count;
	    int p = pass_for_shift(ps, ps_len, shift);
            if (p < 0) {
                continue;
            }
            group_width = gd >> (uint32_t)info->hshift;
            group_height = gd >> (uint32_t)info->vshift;
            if (group_width == 0 || group_height == 0) {
                continue;
            }
            num_cols = (info->original_width + gd - 1u) >> gds;
            num_rows = (info->original_height + gd - 1u) >> gds;
            if (num_cols == 0 || num_rows == 0) {
                continue;
            }
            group_count = (size_t)num_cols * (size_t)num_rows;
            for (gi = 0; gi < group_count; ++gi) {
                jxl_modular_grid tile;
                size_t tile_x;
                size_t tile_y;
                jxl_modular_pg_tile pg;
                jxl_modular_transformed_subimage *slot;
                if (!jxl_modular_grid_group_view_at(parent, group_width, group_height, num_cols,
                                                    num_rows, gi, &tile)) {
                    continue;
                }
                tile_x = 0;
                tile_y = 0;
                if (parent->stride > 0) {
                    tile_y = (tile.offset - parent->offset) / parent->stride;
                    tile_x = (tile.offset - parent->offset) % parent->stride;
                }
                pg.dest_channel_idx = i;
                pg.info.width = (uint32_t)tile.width;
                pg.info.height = (uint32_t)tile.height;
                pg.info.original_width = (uint32_t)tile.width << (uint32_t)info->hshift;
                pg.info.original_height = (uint32_t)tile.height << (uint32_t)info->vshift;
                pg.info.hshift = info->hshift;
                pg.info.vshift = info->vshift;
                pg.info.original_shift = info->original_shift;
                pg.tile_x = tile_x;
                pg.tile_y = tile_y;

                slot = pass_group_slot(out, (uint32_t)p, (uint32_t)gi);
                if (slot == NULL) {
                    continue;
                }
                st = subimage_push_tile(alloc, slot, i, &pg);
                if (st != JXL_MODULAR_OK) {
                    return st;
                }
            }
        } else {
            size_t gi;
            uint32_t lf_group_width = gd >> (uint32_t)(info->hshift - 3);
            uint32_t lf_group_height = gd >> (uint32_t)(info->vshift - 3);
            uint32_t num_cols;
            uint32_t num_rows;
            size_t group_count;
            if (lf_group_width == 0 || lf_group_height == 0) {
                continue;
            }
            num_cols = (info->original_width + (gd << 3) - 1u) >> (gds + 3u);
            num_rows = (info->original_height + (gd << 3) - 1u) >> (gds + 3u);
            if (num_cols == 0 || num_rows == 0) {
                continue;
            }
            group_count = (size_t)num_cols * (size_t)num_rows;
            for (gi = 0; gi < group_count; ++gi) {
                jxl_modular_grid tile;
                size_t tile_x;
                size_t tile_y;
                jxl_modular_pg_tile pg;
                if (!jxl_modular_grid_group_view_at(parent, lf_group_width, lf_group_height,
                                                    num_cols, num_rows, gi, &tile)) {
                    continue;
                }
                tile_x = 0;
                tile_y = 0;
                if (parent->stride > 0) {
                    tile_y = (tile.offset - parent->offset) / parent->stride;
                    tile_x = (tile.offset - parent->offset) % parent->stride;
                }
                pg.dest_channel_idx = i;
                pg.info.width = (uint32_t)tile.width;
                pg.info.height = (uint32_t)tile.height;
                pg.info.original_width = (uint32_t)tile.width << (uint32_t)info->hshift;
                pg.info.original_height = (uint32_t)tile.height << (uint32_t)info->vshift;
                pg.info.hshift = info->hshift;
                pg.info.vshift = info->vshift;
                pg.info.original_shift = info->original_shift;
                pg.tile_x = tile_x;
                pg.tile_y = tile_y;

                if (gi >= out->num_lf_groups) {
                    continue;
                }
                st = subimage_push_tile(alloc, &out->lf_groups[gi], i, &pg);
                if (st != JXL_MODULAR_OK) {
                    return st;
                }
            }
        }
    }
    return JXL_MODULAR_OK;
}

jxl_modular_transformed_subimage *jxl_modular_global_lf_group(jxl_modular_global_groups *groups,
                                                              uint32_t lf_group_idx) {
    if (groups == NULL || groups->lf_groups == NULL || lf_group_idx >= groups->num_lf_groups) {
        return NULL;
    }
    return &groups->lf_groups[lf_group_idx];
}

jxl_modular_transformed_subimage *
jxl_modular_global_pass_group(jxl_modular_global_groups *groups, uint32_t pass_idx,
                              uint32_t group_idx) {
    return pass_group_slot(groups, pass_idx, group_idx);
}

jxl_modular_global_groups *jxl_modular_dest_group_layout(jxl_modular_image_destination *dest) {
    if (dest == NULL || !dest->group_layout_valid) {
        return NULL;
    }
    return dest->group_layout;
}

void jxl_modular_clear_group_layout(jxl_allocator_state *alloc,
                                    jxl_modular_image_destination *dest) {
    jxl_modular_global_groups *groups;
    if (dest == NULL) {
        return;
    }
    groups = jxl_modular_dest_group_layout(dest);
    if (groups != NULL) {
        jxl_modular_global_groups_free(alloc, groups);
        jxl_free(alloc, groups);
        dest->group_layout = NULL;
    }
    dest->group_layout_valid = 0;
}

jxl_modular_status_t jxl_modular_ensure_group_layout(jxl_allocator_state *alloc,
                                                   jxl_modular_image_destination *dest,
                                                   const jxl_frame_header *frame_header) {
    jxl_modular_global_groups *groups;
    jxl_modular_status_t st;
    if (dest == NULL || frame_header == NULL) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
    if (dest->group_layout_valid && dest->group_layout != NULL) {
        return JXL_MODULAR_OK;
    }
    groups = jxl_calloc(alloc, 1, sizeof(*groups));
    if (groups == NULL) {
        return JXL_MODULAR_OUT_OF_MEMORY;
    }
    jxl_modular_global_groups_init(groups);
    st = jxl_modular_prepare_global_groups(alloc, dest, frame_header, groups);
    if (st != JXL_MODULAR_OK) {
        jxl_modular_global_groups_free(alloc, groups);
        jxl_free(alloc, groups);
        return st;
    }
    dest->group_layout = groups;
    dest->group_layout_valid = 1;
    return JXL_MODULAR_OK;
}

jxl_modular_status_t jxl_modular_subimage_recursive_decode(
    jxl_context *ctx, jxl_allocator_state *alloc, jxl_bs *bs, jxl_modular_transformed_subimage *sub,
    jxl_modular_image_destination *dest, const jxl_modular_params *mod_params,
    const jxl_ma_config *global_ma, uint32_t stream_index, int allow_partial, int *out_complete) {
    jxl_modular_recursive_image recursive;
    int decode_partial;
    jxl_modular_status_t st;
    if (out_complete != NULL) {
        *out_complete = 0;
    }
    if (alloc == NULL || bs == NULL || dest == NULL || mod_params == NULL) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
    if (jxl_modular_transformed_subimage_is_empty(sub)) {
        if (out_complete != NULL) {
            *out_complete = 1;
        }
        return JXL_MODULAR_OK;
    }

    jxl_modular_recursive_image_init(&recursive);

    st = jxl_modular_subimage_recursive(
        alloc, bs, sub, dest, mod_params, global_ma, allow_partial, &recursive);
    if (st != JXL_MODULAR_OK) {
        if (JXL_DEBUG_FLAG(ctx, debug_pg_fail)) {
            fprintf(stderr, "subimage_recursive fail st=%d bits=%zu\n", (int)st, bs->num_read_bits);
        }
        jxl_modular_recursive_image_teardown(alloc, &recursive);
        return st;
    }
    if (!jxl_modular_recursive_image_is_valid(&recursive)) {
        jxl_modular_recursive_image_teardown(alloc, &recursive);
        if (allow_partial) {
            sub->partial = 1;
        }
        if (out_complete != NULL) {
            *out_complete = 0;
        }
        return JXL_MODULAR_OK;
    }

    st = jxl_modular_recursive_image_prepare_subimage(alloc, &recursive, sub, dest);
    if (st != JXL_MODULAR_OK) {
        jxl_modular_subimage_teardown_prepared(alloc, sub);
        subimage_teardown_header(alloc, sub);
        jxl_modular_recursive_image_teardown(alloc, &recursive);
        return st;
    }

    decode_partial = 0;
    st = jxl_modular_pass_group_decode(ctx, bs, sub, stream_index, allow_partial, &decode_partial);
    if (st != JXL_MODULAR_OK) {
        if (JXL_DEBUG_FLAG(ctx, debug_pg_fail)) {
            fprintf(stderr, "pass_group_decode fail st=%d bits=%zu\n", (int)st, bs->num_read_bits);
            if (sub->channels.info_len >= 2 && sub->grids_len >= 2) {
                jxl_modular_grid *g = jxl_transformed_grid_leader(&sub->grids[1]);
                if (g != NULL && g->width == 256 && g->height == 256 && g->buf != NULL) {
                    size_t row;
                    int64_t full = 0;
                    for (row = 0; row < g->height; ++row) {
                        size_t x;
                        int64_t sum = 0;
                        for (x = 0; x < g->width; ++x) {
                            sum += jxl_modular_grid_sample_as_i32(g, x, row);
                        }
                        full += sum;
                        if (row < 32 || row >= 240) {
                            fprintf(stderr, "c ch1 row%zu_sum=%lld\n", row, (long long)sum);
                        }
                    }
                    fprintf(stderr, "c ch1 full_sum=%lld\n", (long long)full);
                }
            }
        }
    }
    if (st == JXL_MODULAR_OK) {
        /* Rust TransformedModularSubimage::finish after each pass-group modular decode. */
        uint32_t finish_bd =
            mod_params != NULL ? mod_params->bit_depth : dest->bit_depth;
        st = jxl_modular_subimage_finish(ctx, alloc, &sub->hm.header, &sub->grids, &sub->grids_len, finish_bd,
                                         NULL, NULL);
    }
    jxl_modular_subimage_teardown_prepared(alloc, sub);
    subimage_teardown_header(alloc, sub);
    jxl_modular_recursive_image_teardown(alloc, &recursive);

    if (st == JXL_MODULAR_OK) {
        if (decode_partial) {
            sub->partial = 1;
        } else {
            sub->partial = 0;
            if (out_complete != NULL) {
                *out_complete = 1;
            }
        }
        return JXL_MODULAR_OK;
    }
    if (out_complete != NULL) {
        *out_complete = 0;
    }
    return st;
}
