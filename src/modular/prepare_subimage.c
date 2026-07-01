// SPDX-License-Identifier: MIT OR Apache-2.0
#include "prepare_subimage.h"

#include "modular/channel.h"
#include "modular/group_subimage.h"
#include "modular/transform/inverse.h"
#include "modular/transform/transform.h"

#include "allocator.h"
#include <string.h>

jxl_transformed_grid **jxl_modular_dest_work_grids_storage(jxl_modular_image_destination *dest) {
    if (dest != NULL && dest->subimage_grids_prepared && dest->transformed_grids != NULL) {
        return &dest->transformed_grids;
    }
    return NULL;
}

size_t *jxl_modular_dest_work_grids_len_storage(jxl_modular_image_destination *dest) {
    if (dest != NULL && dest->subimage_grids_prepared && dest->transformed_grids != NULL) {
        return &dest->transformed_grids_len;
    }
    return NULL;
}

jxl_transformed_grid *jxl_modular_dest_work_grids(jxl_modular_image_destination *dest) {
    jxl_transformed_grid **storage = jxl_modular_dest_work_grids_storage(dest);
    return storage != NULL ? *storage : NULL;
}

size_t jxl_modular_dest_work_grids_len(const jxl_modular_image_destination *dest) {
    if (dest != NULL && dest->subimage_grids_prepared && dest->transformed_grids != NULL) {
        return dest->transformed_grids_len;
    }
    if (dest != NULL) {
        return dest->image_channels_len;
    }
    return 0;
}

void jxl_modular_transformed_grids_teardown(jxl_allocator_state *alloc,
                                            jxl_modular_image_destination *dest) {
    if (dest == NULL) {
        return;
    }
    if (dest->transformed_grids != NULL) {
        jxl_transformed_grids_teardown(alloc, dest->transformed_grids, dest->transformed_grids_len);
    }
    jxl_free(alloc, dest->transformed_grids);
    dest->transformed_grids = NULL;
    dest->transformed_grids_len = 0;
    dest->subimage_grids_prepared = 0;
    dest->channel_info_transformed = 0;
}

static jxl_modular_status_t transformed_grids_init_views(jxl_allocator_state *alloc,
                                                         jxl_modular_image_destination *dest) {
                                                             size_t i;
    if (dest == NULL || dest->image_channels == NULL) {
        return JXL_MODULAR_DECODER_ERROR;
    }
    jxl_modular_transformed_grids_teardown(alloc, dest);
    if (dest->image_channels_len == 0) {
        return JXL_MODULAR_OK;
    }
    dest->transformed_grids =
        jxl_calloc(alloc, dest->image_channels_len, sizeof(*dest->transformed_grids));
    if (dest->transformed_grids == NULL) {
        return JXL_MODULAR_OUT_OF_MEMORY;
    }
    dest->transformed_grids_len = dest->image_channels_len;
    for (i = 0; i < dest->image_channels_len; ++i) {
        jxl_transformed_grid_set_single(alloc, &dest->transformed_grids[i], dest->image_channels[i]);
    }
    return JXL_MODULAR_OK;
}

void jxl_modular_dest_finalize_after_inverse(jxl_allocator_state *alloc,
                                             jxl_modular_image_destination *dest) {
    size_t i;
    size_t n;
    jxl_transformed_grid *work;
    size_t work_len;
    if (dest == NULL) {
        return;
    }
    n = dest->channels.info_len;
    work = jxl_modular_dest_work_grids(dest);
    work_len = jxl_modular_dest_work_grids_len(dest);
    if (work != NULL && n > work_len) {
        n = work_len;
    }
    for (i = 0; i < n && i < dest->image_channels_len; ++i) {
        const jxl_modular_grid *leader = jxl_transformed_grid_leader_const(&work[i]);
        if (leader != NULL) {
            dest->image_channels[i] = *leader;
        }
        if (leader != NULL) {
            dest->channels.info[i].width = (uint32_t)leader->width;
            dest->channels.info[i].height = (uint32_t)leader->height;
        }
    }
    jxl_modular_transformed_grids_teardown(alloc, dest);
}

jxl_modular_grid *jxl_modular_dest_channel_grid(jxl_modular_image_destination *dest,
                                               size_t channel_idx) {
    size_t ti;
    jxl_transformed_grid *grids;
    size_t grids_len;
    jxl_modular_grid *g;
    if (dest == NULL) {
        return NULL;
    }
    if (!dest->subimage_grids_prepared) {
        if (channel_idx >= dest->image_channels_len) {
            return NULL;
        }
        return &dest->image_channels[channel_idx];
    }
    grids = dest->transformed_grids;
    grids_len = dest->transformed_grids_len;
    if (grids == NULL || channel_idx >= grids_len) {
        return NULL;
    }
    g = jxl_transformed_grid_leader(&grids[channel_idx]);
    if (g != NULL && g->buf != NULL) {
        return g;
    }
    for (ti = 0; ti < dest->header.transform_len; ++ti) {
        const jxl_transform_palette *pal;
        size_t leader_idx;
        jxl_transformed_grid *leader_slot;
        size_t member_idx;
	if (dest->header.transform[ti].kind != JXL_TRANSFORM_KIND_PALETTE) {
            continue;
        }
        pal = &dest->header.transform[ti].u.palette;
        if (pal->num_c < 2 || channel_idx < pal->begin_c ||
            channel_idx >= pal->begin_c + pal->num_c) {
            continue;
        }
        leader_idx = (size_t)pal->begin_c + 1;
        if (leader_idx >= grids_len) {
            continue;
        }
        leader_slot = &grids[leader_idx];
        if (channel_idx == pal->begin_c) {
            jxl_modular_grid *leader = jxl_transformed_grid_leader(leader_slot);
            return leader != NULL && leader->buf != NULL ? leader : NULL;
        }
        if (leader_slot->kind != JXL_TRANSFORMED_GRID_MERGED) {
            continue;
        }
        member_idx = (size_t)channel_idx - (size_t)pal->begin_c - 1;
        if (member_idx < leader_slot->members_len) {
            return jxl_transformed_grid_leader(&leader_slot->members[member_idx]);
        }
    }
    return g;
}

static jxl_modular_status_t transformed_insert_front(jxl_allocator_state *alloc, jxl_transformed_grid **grids, size_t *len,
                                                     const jxl_modular_grid *grid) {
    jxl_transformed_grid slot;
    jxl_transformed_grid_init_empty(&slot);
    jxl_transformed_grid_set_single(alloc, &slot, *grid);
    return jxl_transformed_grids_insert_at(alloc, grids, len, 0, &slot, 1);
}

/* Rust Transform::transform_channels for palette (dest + pass-group prepare). */
static jxl_modular_status_t apply_palette_transform(jxl_allocator_state *alloc, const jxl_transform_palette *pal,
                                                    jxl_transformed_grid **grids, size_t *grids_len,
                                                    const jxl_modular_grid *meta_channels,
                                                    size_t *work_meta_len) {
                                                        size_t i;
    uint32_t begin_c;
    jxl_modular_grid palette_grid;
    uint32_t end_c;
    size_t member_count;
    jxl_transformed_grid *members;
    jxl_modular_status_t st;
    size_t leader_idx;
    if (pal == NULL || grids == NULL || grids_len == NULL || work_meta_len == NULL ||
        meta_channels == NULL) {
        return JXL_MODULAR_DECODER_ERROR;
    }
    if (*work_meta_len == 0) {
        return JXL_MODULAR_DECODER_ERROR;
    }

    begin_c = pal->begin_c;
    end_c = begin_c + pal->num_c;
    if (end_c > *grids_len) {
        return JXL_MODULAR_DECODER_ERROR;
    }

    palette_grid = meta_channels[*work_meta_len - 1];
    (*work_meta_len)--;

    if (pal->num_c < 2) {
        return transformed_insert_front(alloc, grids, grids_len, &palette_grid);
    }

    member_count = (size_t)pal->num_c - 1;
    members = jxl_calloc(alloc, member_count, sizeof(*members));
    if (members == NULL) {
        return JXL_MODULAR_OUT_OF_MEMORY;
    }
    for (i = 0; i < member_count; ++i) {
        members[i] = (*grids)[(size_t)begin_c + 1 + i];
        jxl_transformed_grid_init_empty(&(*grids)[(size_t)begin_c + 1 + i]);
    }
    jxl_transformed_grids_remove_range(alloc, *grids, grids_len, (size_t)begin_c + 1, member_count);

    st = transformed_insert_front(alloc, grids, grids_len, &palette_grid);
    if (st != JXL_MODULAR_OK) {
        jxl_free(alloc, members);
        return st;
    }

    leader_idx = (size_t)begin_c + 1;
    if (leader_idx >= *grids_len) {
        jxl_free(alloc, members);
        return JXL_MODULAR_DECODER_ERROR;
    }
    return jxl_transformed_grid_merge(alloc, &(*grids)[leader_idx], members, member_count);
}

/* Rust Transform::transform_channels for squeeze (dest + pass-group prepare). */
static jxl_modular_status_t apply_squeeze_transform(jxl_allocator_state *alloc, const jxl_transform_squeeze *sq,
                                                    jxl_transformed_grid **grids,
                                                    size_t *grids_len) {
                                                        size_t si;
    if (sq == NULL || grids == NULL || grids_len == NULL) {
        return JXL_MODULAR_DECODER_ERROR;
    }
    for (si = 0; si < sq->sp_len; ++si) {
        uint32_t idx;
        size_t tail_len;
        size_t insert_at;
        const jxl_squeeze_params *sp = &sq->sp[si];
        size_t begin = sp->begin_c;
        size_t end = begin + sp->num_c;
        size_t residu_count;
        jxl_transformed_grid *residu;
        size_t new_len;
        jxl_modular_status_t st;
        size_t copy_count;
        if (end > *grids_len) {
            return JXL_MODULAR_INVALID_SQUEEZE_PARAMS;
        }

        tail_len = 0;
        insert_at = end;
        if (sp->in_place) {
            tail_len = end < *grids_len ? *grids_len - end : 0;
        } else {
            insert_at = *grids_len;
        }
        residu_count = (size_t)sp->num_c + tail_len;

        residu = jxl_calloc(alloc, residu_count, sizeof(*residu));
        if (residu == NULL) {
            return JXL_MODULAR_OUT_OF_MEMORY;
        }

        for (idx = 0; idx < sp->num_c; ++idx) {
            size_t i = begin + idx;
            jxl_modular_grid split =
                sp->horizontal
                    ? jxl_modular_grid_split_horizontal_in_place(
                          jxl_transformed_grid_leader(&(*grids)[i]))
                    : jxl_modular_grid_split_vertical_in_place(
                          jxl_transformed_grid_leader(&(*grids)[i]));
            jxl_transformed_grid_set_single(alloc, &residu[idx], split);
        }

        if (sp->in_place && tail_len > 0) {
            memcpy(residu + sp->num_c, &(*grids)[end], tail_len * sizeof(*residu));
        }

        new_len = sp->in_place ? end + residu_count : *grids_len + sp->num_c;
        st = jxl_transformed_grids_resize(alloc, grids, grids_len, new_len);
        if (st != JXL_MODULAR_OK) {
            jxl_free(alloc, residu);
            return st;
        }
        copy_count = sp->in_place ? residu_count : (size_t)sp->num_c;
        memcpy(&(*grids)[insert_at], residu, copy_count * sizeof(*residu));
        jxl_free(alloc, residu);
    }
    return JXL_MODULAR_OK;
}

jxl_modular_status_t jxl_modular_dest_sync_image_channels(jxl_allocator_state *alloc,
                                                          jxl_modular_image_destination *dest) {
    size_t n;
    if (dest == NULL || !dest->subimage_grids_prepared) {
        return JXL_MODULAR_OK;
    }
    n = dest->transformed_channels.info_len;
    if (n == dest->transformed_grids_len) {
        return JXL_MODULAR_OK;
    }
    if (n < dest->transformed_grids_len) {
        jxl_transformed_grids_teardown(alloc, dest->transformed_grids + n,
                                     dest->transformed_grids_len - n);
        return jxl_transformed_grids_resize(alloc, &dest->transformed_grids, &dest->transformed_grids_len,
                                            n);
    }
    return JXL_MODULAR_DECODER_ERROR;
}

const jxl_modular_channels *jxl_modular_dest_subimage_channels(
    const jxl_modular_image_destination *dest) {
    if (dest != NULL && dest->subimage_grids_prepared) {
        return &dest->transformed_channels;
    }
    if (dest != NULL) {
        return &dest->channels;
    }
    return NULL;
}

static void sync_transformed_dims_from_grids(jxl_modular_image_destination *dest) {
    size_t i;
    size_t n = dest->transformed_channels.info_len;
    if (dest->transformed_grids_len < n) {
        n = dest->transformed_grids_len;
    }
    for (i = 0; i < n; ++i) {
        const jxl_modular_grid *leader =
            jxl_transformed_grid_leader_const(&dest->transformed_grids[i]);
        if (leader != NULL) {
            dest->transformed_channels.info[i].width = (uint32_t)leader->width;
            dest->transformed_channels.info[i].height = (uint32_t)leader->height;
        }
    }
}

static jxl_modular_status_t dest_init_transformed_channels(jxl_allocator_state *alloc, jxl_modular_image_destination *dest) {
    jxl_modular_channels_free(alloc, &dest->transformed_channels);
    jxl_modular_channels_init(&dest->transformed_channels);
    if (dest->channels.info_len == 0) {
        return JXL_MODULAR_OK;
    }
    if (jxl_modular_channels_reserve(alloc, &dest->transformed_channels, dest->channels.info_len) !=
        JXL_MODULAR_OK) {
        return JXL_MODULAR_OUT_OF_MEMORY;
    }
    dest->transformed_channels.info_len = dest->channels.info_len;
    dest->transformed_channels.nb_meta_channels = dest->channels.nb_meta_channels;
    memcpy(dest->transformed_channels.info, dest->channels.info,
           dest->channels.info_len * sizeof(*dest->transformed_channels.info));
    return JXL_MODULAR_OK;
}

jxl_modular_status_t jxl_modular_channels_transform_info(
    jxl_allocator_state *alloc, jxl_modular_image_destination *dest, jxl_modular_channels *out) {
    size_t i;
    const jxl_modular_channels *src;
    if (dest == NULL || out == NULL) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
    src = &dest->channels;
    jxl_modular_channels_init(out);
    if (src->info_len > 0) {
        if (jxl_modular_channels_reserve(alloc, out, src->info_len) != JXL_MODULAR_OK) {
            return JXL_MODULAR_OUT_OF_MEMORY;
        }
        out->info_len = src->info_len;
        out->nb_meta_channels = src->nb_meta_channels;
        memcpy(out->info, src->info, src->info_len * sizeof(*out->info));
    }
    for (i = 0; i < dest->header.transform_len; ++i) {
        jxl_modular_status_t st =
            jxl_transform_prepare_channel_info(alloc, &dest->header.transform[i], out);
        if (st != JXL_MODULAR_OK) {
            jxl_modular_channels_free(alloc, out);
            return st;
        }
    }
    return JXL_MODULAR_OK;
}

jxl_modular_status_t jxl_modular_image_prepare_subimage_grids(jxl_allocator_state *alloc,
                                                              jxl_modular_image_destination *dest) {
    size_t i;
    jxl_modular_status_t st;
    size_t work_meta_len;
    if (dest == NULL) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
    if (dest->subimage_grids_prepared) {
        return JXL_MODULAR_OK;
    }
    st = dest_init_transformed_channels(alloc, dest);
    if (st != JXL_MODULAR_OK) {
        return st;
    }
    st = transformed_grids_init_views(alloc, dest);
    if (st != JXL_MODULAR_OK) {
        return st;
    }
    work_meta_len = dest->meta_channels_len;
    for (i = 0; i < dest->header.transform_len; ++i) {
        jxl_transform_info *tr = &dest->header.transform[i];
        st = JXL_MODULAR_OK;
        switch (tr->kind) {
        case JXL_TRANSFORM_KIND_RCT:
            st = jxl_transform_prepare_channel_info(alloc, tr, &dest->transformed_channels);
            break;
        case JXL_TRANSFORM_KIND_PALETTE:
            st = jxl_transform_prepare_channel_info(alloc, tr, &dest->transformed_channels);
            if (st == JXL_MODULAR_OK) {
                st = apply_palette_transform(alloc, &tr->u.palette, &dest->transformed_grids,
                                             &dest->transformed_grids_len, dest->meta_channels,
                                             &work_meta_len);
            }
            break;
        case JXL_TRANSFORM_KIND_SQUEEZE:
            st = jxl_transform_prepare_channel_info(alloc, tr, &dest->transformed_channels);
            if (st == JXL_MODULAR_OK) {
                st = apply_squeeze_transform(alloc, &tr->u.squeeze, &dest->transformed_grids,
                                             &dest->transformed_grids_len);
            }
            break;
        default:
            st = JXL_MODULAR_BITSTREAM_ERROR;
            break;
        }
        if (st != JXL_MODULAR_OK) {
            return st;
        }
    }
    sync_transformed_dims_from_grids(dest);
    dest->channel_info_transformed = 1;
    dest->subimage_grids_prepared = 1;
    return jxl_modular_dest_sync_image_channels(alloc, dest);
}

size_t jxl_modular_gmodular_channel_count(jxl_modular_image_destination *dest) {
    size_t i;
    size_t count;
    const jxl_modular_channels *use;
    uint32_t gd;
    if (dest == NULL || dest->group_dim == 0) {
        return 0;
    }

    use = jxl_modular_dest_subimage_channels(dest);
    if (use == NULL) {
        return 0;
    }

    gd = dest->group_dim;
    count = 0;
    for (i = 0; i < use->info_len; ++i) {
        const jxl_modular_channel_info *info = &use->info[i];
        if (i < use->nb_meta_channels || (info->width <= gd && info->height <= gd)) {
            count = i + 1;
        } else {
            break;
        }
    }
    return count;
}

jxl_modular_status_t jxl_modular_prepare_gmodular(jxl_allocator_state *alloc,
                                                  jxl_modular_image_destination *dest) {
    if (dest == NULL || dest->group_dim == 0) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
    return jxl_modular_image_prepare_subimage_grids(alloc, dest);
}

jxl_modular_status_t jxl_modular_gmodular_finish(jxl_context *ctx, jxl_allocator_state *alloc,
                                                 jxl_modular_image_destination *dest,
                                                 uint32_t frame_width, uint32_t frame_height,
                                                 uint32_t bit_depth,
                                                 const jxl_modular_params *mod_params) {
    jxl_modular_status_t st;
    if (dest == NULL) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }
    if (!dest->subimage_grids_prepared) {
        st = jxl_modular_image_prepare_subimage_grids(alloc, dest);
        if (st != JXL_MODULAR_OK) {
            return st;
        }
    }
    st = jxl_modular_image_apply_inverse_transforms(
        ctx, alloc, dest, frame_width, frame_height, bit_depth, mod_params);
    if (st != JXL_MODULAR_OK) {
        return st;
    }
    jxl_modular_dest_finalize_after_inverse(alloc, dest);
    return JXL_MODULAR_OK;
}

jxl_modular_status_t jxl_modular_recursive_image_prepare_subimage(
    jxl_allocator_state *alloc, jxl_modular_recursive_image *recursive,
    jxl_modular_transformed_subimage *sub, jxl_modular_image_destination *dest) {
    size_t ti;
    size_t i;
    size_t work_meta_len;
    size_t n;

    if (recursive == NULL || !recursive->valid || sub == NULL || dest == NULL ||
        sub->tile_count == 0) {
        return JXL_MODULAR_BITSTREAM_ERROR;
    }

    jxl_modular_subimage_teardown_prepared(alloc, sub);

    if (sub->hm.header.transform != NULL || sub->hm.ma_owns) {
        jxl_modular_header_free(alloc, &sub->hm.header);
        if (sub->hm.ma_owns) {
            jxl_ma_config_init(&sub->hm.ma_ctx);
        }
        sub->hm.ma_owns = 0;
    }
    sub->hm = recursive->hm;
    jxl_modular_header_ma_init(&recursive->hm);

    jxl_modular_channels_free(alloc, &sub->channels);
    sub->channels = recursive->channels;
    jxl_modular_channels_init(&recursive->channels);

    sub->grids = jxl_calloc(alloc, sub->tile_count, sizeof(*sub->grids));
    if (sub->grids == NULL) {
        return JXL_MODULAR_OUT_OF_MEMORY;
    }
    sub->grids_len = sub->tile_count;
    for (ti = 0; ti < sub->tile_count; ++ti) {
        const jxl_modular_pg_tile *tl = &sub->tiles[ti];
        jxl_modular_grid *parent = jxl_modular_dest_channel_grid(dest, tl->dest_channel_idx);
        size_t w;
        size_t h;
        if (parent == NULL || parent->buf == NULL) {
            return JXL_MODULAR_DECODER_ERROR;
        }
        w = tl->info.width;
        h = tl->info.height;
        if (tl->tile_x + w > parent->width) {
            w = parent->width > tl->tile_x ? parent->width - tl->tile_x : 0;
        }
        if (tl->tile_y + h > parent->height) {
            h = parent->height > tl->tile_y ? parent->height - tl->tile_y : 0;
        }
        jxl_transformed_grid_set_single(alloc, &sub->grids[ti], jxl_modular_grid_tile_view(parent, tl->tile_x, tl->tile_y, w, h));
    }

    work_meta_len = recursive->meta_channels_len;
    for (i = 0; i < sub->hm.header.transform_len; ++i) {
        jxl_transform_info *tr = &sub->hm.header.transform[i];
        jxl_modular_status_t st = jxl_transform_prepare_channel_info(alloc, tr, &sub->channels);
        if (st != JXL_MODULAR_OK) {
            return st;
        }
        switch (tr->kind) {
        case JXL_TRANSFORM_KIND_RCT:
            break;
        case JXL_TRANSFORM_KIND_PALETTE:
            st = apply_palette_transform(alloc, &tr->u.palette, &sub->grids, &sub->grids_len,
                                         recursive->meta_channels, &work_meta_len);
            break;
        case JXL_TRANSFORM_KIND_SQUEEZE:
            st = apply_squeeze_transform(alloc, &tr->u.squeeze, &sub->grids, &sub->grids_len);
            break;
        default:
            st = JXL_MODULAR_BITSTREAM_ERROR;
            break;
        }
        if (st != JXL_MODULAR_OK) {
            return st;
        }
    }

    n = sub->channels.info_len;
    if (sub->grids_len < n) {
        n = sub->grids_len;
    }
    for (i = 0; i < n; ++i) {
        const jxl_modular_grid *leader = jxl_transformed_grid_leader_const(&sub->grids[i]);
        if (leader != NULL) {
            sub->channels.info[i].width = (uint32_t)leader->width;
            sub->channels.info[i].height = (uint32_t)leader->height;
        }
    }
    sub->prepared = 1;
    return JXL_MODULAR_OK;
}
